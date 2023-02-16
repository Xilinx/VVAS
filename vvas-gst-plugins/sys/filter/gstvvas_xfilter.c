/*
 * Copyright 2020 - 2022 Xilinx, Inc.
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
 * KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
 * EVENT SHALL XILINX BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. Except as contained in this notice, the name of the Xilinx shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from Xilinx.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef XLNX_PCIe_PLATFORM
#define USE_DMABUF 0
#else /* Embedded */
#define USE_DMABUF 1
#endif

#include <gst/gst.h>
#include <gst/vvas/gstvvasallocator.h>
#include <gst/vvas/gstvvasbufferpool.h>
#include <gst/allocators/gstdmabuf.h>
#include <dlfcn.h>              /* for dlXXX APIs */
#include <sys/mman.h>           /* for munmap */
#include <jansson.h>
#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include <vvas/vvas_kernel.h>
#include "gstvvas_xfilter.h"
#include <gst/vvas/gstvvasutils.h>

GST_DEBUG_CATEGORY_STATIC (gst_vvas_xfilter_debug);
#define GST_CAT_DEFAULT gst_vvas_xfilter_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

pthread_mutex_t count_mutex;

#define CMD_EXEC_TIMEOUT 1000   // 1 sec
#define MIN_POOL_BUFFERS 2
#define DEFAULT_VVAS_LIB_PATH "/usr/lib/"
#define DEFAULT_DEVICE_INDEX 0
#define MAX_PRIV_POOLS 10
#define ALIGN(size,align) ((((size) + (align) - 1) / align) * align)

#include <vvas_core/vvas_device.h>

typedef struct _GstVvas_XFilterPrivate GstVvas_XFilterPrivate;

enum
{
  SIGNAL_VVAS,

  /* add more signal above this */
  SIGNAL_LAST
};

static guint vvas_signals[SIGNAL_LAST] = { 0 };

typedef struct
{
  gchar *skname;
} VvasSoftKernelInfo;

typedef struct
{
  gchar *name;
  bool shared_access;
  json_t *config;
  gchar *vvas_lib_path;
  void *lib_fd;
  gint cu_idx;
  VVASKernelInit kernel_init_func;
  VVASKernelStartFunc kernel_start_func;
  VVASKernelDoneFunc kernel_done_func;
  VVASKernelDeInit kernel_deinit_func;
  VVASKernel *vvas_handle;
  GstVideoFrame in_vframe;
  GstVideoFrame out_vframe;
  VVASFrame *input[MAX_NUM_OBJECT];
  VVASFrame *output[MAX_NUM_OBJECT];
  gboolean is_softkernel;
#ifdef XLNX_PCIe_PLATFORM
  VvasSoftKernelInfo *skinfo;
#endif
} Vvas_XFilter;

enum
{
  PROP_0,
  PROP_CONFIG_LOCATION,
  PROP_DYNAMIC_CONFIG,
#if defined(XLNX_PCIe_PLATFORM)
  PROP_DEVICE_INDEX,
  PROP_SK_CURRENT_INDEX,
#endif
};

typedef enum
{
  VVAS_ELEMENT_MODE_NOT_SUPPORTED,
  VVAS_ELEMENT_MODE_PASSTHROUGH,        /* does not alter input buffer */
  VVAS_ELEMENT_MODE_IN_PLACE,   /* going to change input buffer content */
  VVAS_ELEMENT_MODE_TRANSFORM,  /* input and output buffers are different */
} Vvas_XFilterMode;

struct _GstVvas_XFilterPrivate
{
  gint dev_idx;
  vvasDeviceHandle dev_handle;
  vvasKernelHandle kern_handle;
  gchar *xclbin_loc;
  json_t *root;
  Vvas_XFilter *kernel;
  Vvas_XFilterMode element_mode;
  gboolean do_init;
  GstVideoInfo *in_vinfo;
  GstVideoInfo *internal_in_vinfo;
  GstVideoInfo *out_vinfo;
  GstVideoInfo *internal_out_vinfo;
  uuid_t xclbinId;
  GstBufferPool *input_pool;
  gboolean need_copy;
  GstBufferPool *priv_pools[MAX_PRIV_POOLS];
  json_t *dyn_json_config;
#ifdef XLNX_PCIe_PLATFORM
  gint sk_cur_idx;
#endif                          /* XLNX_PCIe_PLATFORM */
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{GRAY8, NV12, BGR, RGB, YUY2,"
            "r210, v308, GRAY10_LE32, ABGR, ARGB}")));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{GRAY8, NV12, BGR, RGB, YUY2,"
            "r210, v308, GRAY10_LE32, ABGR, ARGB}")));

#define gst_vvas_xfilter_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_XFilter, gst_vvas_xfilter,
    GST_TYPE_BASE_TRANSFORM);
#define GST_VVAS_XFILTER_PRIVATE(self) (GstVvas_XFilterPrivate *) (gst_vvas_xfilter_get_instance_private (self))

int32_t vvas_buffer_alloc (VVASKernel * handle, VVASFrame * vvas_frame,
    void *data);
void vvas_buffer_free (VVASKernel * handle, VVASFrame * vvas_frame, void *data);
static void gst_vvas_xfilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vvas_xfilter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn
gst_vvas_xfilter_submit_input_buffer (GstBaseTransform * trans,
    gboolean is_discont, GstBuffer * inbuf);
static GstFlowReturn
gst_vvas_xfilter_generate_output (GstBaseTransform * trans,
    GstBuffer ** outbuf);

static GstFlowReturn gst_vvas_xfilter_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);
static GstFlowReturn
gst_vvas_xfilter_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf);
static void gst_vvas_xfilter_finalize (GObject * obj);

static Vvas_XFilterMode
get_kernel_mode (const gchar * mode)
{
  if (!g_strcmp0 ("passthrough", mode))
    return VVAS_ELEMENT_MODE_PASSTHROUGH;
  else if (!g_strcmp0 ("inplace", mode))
    return VVAS_ELEMENT_MODE_IN_PLACE;
  else if (!g_strcmp0 ("transform", mode))
    return VVAS_ELEMENT_MODE_TRANSFORM;
  else
    return VVAS_ELEMENT_MODE_NOT_SUPPORTED;
}

static inline VVASVideoFormat
get_kernellib_format (GstVideoFormat gst_fmt)
{
  switch (gst_fmt) {
    case GST_VIDEO_FORMAT_GRAY8:
      return VVAS_VFMT_Y8;
    case GST_VIDEO_FORMAT_NV12:
      return VVAS_VFMT_Y_UV8_420;
    case GST_VIDEO_FORMAT_BGR:
      return VVAS_VFMT_BGR8;
    case GST_VIDEO_FORMAT_RGB:
      return VVAS_VFMT_RGB8;
    case GST_VIDEO_FORMAT_YUY2:
      return VVAS_VFMT_YUYV8;
    case GST_VIDEO_FORMAT_r210:
      return VVAS_VFMT_RGBX10;
    case GST_VIDEO_FORMAT_v308:
      return VVAS_VFMT_YUV8;
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      return VVAS_VFMT_Y10;
    case GST_VIDEO_FORMAT_ABGR:
      return VVAS_VFMT_ABGR8;
    case GST_VIDEO_FORMAT_ARGB:
      return VVAS_VFMT_ARGB8;
    default:
      GST_ERROR ("Not supporting %s yet", gst_video_format_to_string (gst_fmt));
      return VVAS_VMFT_UNKNOWN;
  }
}

static inline GstVideoFormat
get_gst_format (VVASVideoFormat kernel_fmt)
{
  switch (kernel_fmt) {
    case VVAS_VFMT_Y8:
      return GST_VIDEO_FORMAT_GRAY8;
    case VVAS_VFMT_Y_UV8_420:
      return GST_VIDEO_FORMAT_NV12;
    case VVAS_VFMT_BGR8:
      return GST_VIDEO_FORMAT_BGR;
    case VVAS_VFMT_RGB8:
      return GST_VIDEO_FORMAT_RGB;
    case VVAS_VFMT_YUYV8:
      return GST_VIDEO_FORMAT_YUY2;
    case VVAS_VFMT_RGBX10:
      return GST_VIDEO_FORMAT_r210;
    case VVAS_VFMT_YUV8:
      return GST_VIDEO_FORMAT_v308;
    case VVAS_VFMT_Y10:
      return GST_VIDEO_FORMAT_GRAY10_LE32;
    case VVAS_VFMT_ABGR8:
      return GST_VIDEO_FORMAT_ABGR;
    case VVAS_VFMT_ARGB8:
      return GST_VIDEO_FORMAT_ARGB;
    default:
      GST_ERROR ("Not supporting kernel format %d yet", kernel_fmt);
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
}

static unsigned int
vvas_xfilter_cal_stride (unsigned int width, GstVideoFormat fmt, gint align)
{
  unsigned int stride;
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
//      u32 align;

  /* Stride in Bytes = (Width × Bytes per Pixel); */
  switch (fmt) {
    case GST_VIDEO_FORMAT_r210:
      stride = width * 4;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      stride = width * 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_GRAY8:
      stride = width * 1;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_v308:
    case GST_VIDEO_FORMAT_BGR:
      stride = width * 3;
      break;
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      /* 4 bytes per 3 pixels */
      stride = DIV_ROUND_UP (width * 4, 3);
      break;
    default:
      stride = 0;
  }

  stride = ALIGN (stride, align);

  return stride;
}

static guint
vvas_xfilter_get_padding_right (GstVvas_XFilter * self,
    GstVideoInfo * info, guint stride_align)
{
  guint padding_pixels = -1;
  guint plane_stride = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  guint padding_bytes = ALIGN (plane_stride, stride_align) - plane_stride;

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_NV12:
      padding_pixels = padding_bytes;
      break;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_r210:
    case GST_VIDEO_FORMAT_Y410:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
      padding_pixels = padding_bytes / 4;
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      padding_pixels = padding_bytes / 2;
      break;
    case GST_VIDEO_FORMAT_NV16:
      padding_pixels = padding_bytes;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_v308:
    case GST_VIDEO_FORMAT_BGR:
      padding_pixels = padding_bytes / 3;
      break;
    case GST_VIDEO_FORMAT_I422_10LE:
      padding_pixels = padding_bytes / 2;
      break;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      padding_pixels = (padding_bytes * 3) / 4;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      padding_pixels = padding_bytes;
      break;
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      padding_pixels = (padding_bytes * 3) / 4;
      break;
    case GST_VIDEO_FORMAT_I420:
      padding_pixels = padding_bytes;
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
      padding_pixels = padding_bytes / 2;
      break;
    default:
      GST_ERROR_OBJECT (self, "not yet supporting format %d",
          GST_VIDEO_INFO_FORMAT (info));
  }
  return padding_pixels;
}

int32_t
vvas_buffer_alloc (VVASKernel * handle, VVASFrame * vvas_frame, void *data)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (data);
  GstBuffer *outbuf = NULL;
  GstMemory *out_mem = NULL;
  GstFlowReturn fret;
  guint64 phy_addr;
  vvasBOHandle bo_handle = NULL;
  guint plane_id;
  GstVideoMeta *vmeta;
  GstVideoInfo out_info, max_info;
  gsize size_requested, max_size;
  GstBufferPool *priv_pool = NULL;
  int oidx;

  if (!gst_video_info_set_format (&out_info,
          get_gst_format (vvas_frame->props.fmt), vvas_frame->props.width,
          vvas_frame->props.height)) {
    GST_ERROR_OBJECT (self, "failed to get videoinfo");
    return -1;
  }

  /* max video info from input width & height */
  // TODO: Better to take from out info if available
  if (!gst_video_info_set_format (&max_info,
          get_gst_format (vvas_frame->props.fmt),
          GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo),
          GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo))) {
    GST_ERROR_OBJECT (self, "failed to get max videoinfo");
    return -1;
  }

  size_requested = GST_VIDEO_INFO_SIZE (&out_info);
  max_size = GST_VIDEO_INFO_SIZE (&max_info);

  if (size_requested > max_size) {
    GST_FIXME_OBJECT (self,
        "requested output buffer size %lu greater than max size %lu",
        size_requested, max_size);
    return -1;
  }

  oidx = 1;
  while (size_requested * MAX_PRIV_POOLS > oidx * max_size) {
    oidx++;
  }
  priv_pool = self->priv->priv_pools[oidx - 1];

  GST_DEBUG_OBJECT (self,
      "choosen outpool %p at index %d for requested buffer size %lu", priv_pool,
      oidx - 1, size_requested);

  if (priv_pool == NULL) {
    GstAllocator *allocator;
    GstCaps *caps;
    gsize pool_buf_size;
    gboolean bret;
    GstAllocationParams params =
        { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0, 0 };
    GstStructure *config;
    GstVideoInfo tmp_info;

    // TODO: taking width & height from input caps to allocate max size buffer pool
    // need to improve to take it from nearest pool
    caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING,
        gst_video_format_to_string (get_gst_format (vvas_frame->props.fmt)),
        "width", G_TYPE_INT, GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo),
        "height", G_TYPE_INT,
        (oidx * GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo)) / MAX_PRIV_POOLS,
        NULL);

    if (!gst_video_info_from_caps (&tmp_info, caps))
      return -1;

    pool_buf_size = GST_VIDEO_INFO_SIZE (&tmp_info);

    priv_pool = gst_video_buffer_pool_new ();
    pool_buf_size = ALIGN (pool_buf_size, 4096);
    GST_INFO_OBJECT (self, "allocated internal private pool %p with size %lu",
        priv_pool, pool_buf_size);

    /* Here frame buffer is required from in_mem_bank as it is expected that
     * input port is attached the bank where IP access internal data too */
    allocator = gst_vvas_allocator_new (self->priv->dev_idx,
        USE_DMABUF, handle->in_mem_bank);

    config = gst_buffer_pool_get_config (priv_pool);
    gst_buffer_pool_config_set_params (config, caps, pool_buf_size, 2, 0);
    gst_buffer_pool_config_set_allocator (config, allocator, &params);

    if (allocator)
      gst_object_unref (allocator);

    if (!gst_buffer_pool_set_config (priv_pool, config)) {
      GST_ERROR_OBJECT (self, "failed to configure  pool");
      return -1;
    }

    GST_INFO_OBJECT (self,
        "setting config %" GST_PTR_FORMAT " on private pool %" GST_PTR_FORMAT,
        config, priv_pool);

    bret = gst_buffer_pool_set_active (priv_pool, TRUE);
    if (!bret) {
      GST_ERROR_OBJECT (self, "failed to active private pool");
      return -1;
    }
    self->priv->priv_pools[oidx - 1] = priv_pool;
  }

  fret = gst_buffer_pool_acquire_buffer (priv_pool, &outbuf, NULL);
  if (fret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
        priv_pool);
    goto error;
  }
  GST_LOG_OBJECT (self, "acquired buffer %p from private pool %p", outbuf,
      priv_pool);

  gst_buffer_add_video_meta (outbuf, GST_VIDEO_FRAME_FLAG_NONE,
      get_gst_format (vvas_frame->props.fmt), vvas_frame->props.width,
      vvas_frame->props.height);

  out_mem = gst_buffer_get_memory (outbuf, 0);
  if (out_mem == NULL) {
    GST_ERROR_OBJECT (self, "failed to get memory from  buffer");
    goto error;
  }

  phy_addr = gst_vvas_allocator_get_paddr (out_mem);
  bo_handle = gst_vvas_allocator_get_bo (out_mem);

  vmeta = gst_buffer_get_video_meta (outbuf);
  if (vmeta == NULL) {
    GST_ERROR_OBJECT (self, "video meta not present in buffer");
    goto error;
  }
  // TODO: check whether we can use buffer pool memory size with requested size
  // return error when size > pool_mem_size

  for (plane_id = 0; plane_id < GST_VIDEO_INFO_N_PLANES (&out_info); plane_id++) {
    size_t plane_size;
    guint plane_height;
    gint comp[GST_VIDEO_MAX_COMPONENTS];

    vvas_frame->paddr[plane_id] = phy_addr + vmeta->offset[plane_id];
    GST_LOG_OBJECT (self,
        "outbuf plane[%d] : paddr = %p, offset = %lu", plane_id,
        (void *) vvas_frame->paddr[plane_id], vmeta->offset[plane_id]);

    /* Convert plane index to component index */
    gst_video_format_info_component (out_info.finfo, plane_id, comp);
    plane_height = GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (out_info.finfo,
        comp[0], GST_VIDEO_INFO_FIELD_HEIGHT (&out_info));
    plane_size = plane_height *
        GST_VIDEO_INFO_PLANE_STRIDE (&out_info, plane_id);

    vvas_frame->bo[plane_id] = vvas_xrt_create_sub_bo (bo_handle,
        plane_size, vmeta->offset[plane_id]);
  }

  /* setting SYNC_FROM_DEVICE here to avoid work by kernel lib */
  gst_vvas_memory_set_sync_flag (out_mem, VVAS_SYNC_FROM_DEVICE);

  vvas_frame->app_priv = outbuf;

  GST_LOG_OBJECT (self, "associating vvasframe %p with outbuf %p", vvas_frame,
      outbuf);

  gst_memory_unref (out_mem);

  return 0;

error:
  if (out_mem)
    gst_memory_unref (out_mem);

  return -1;
}

void
vvas_buffer_free (VVASKernel * handle, VVASFrame * vvas_frame, void *data)
{
  guint plane_id = 0;

  if (vvas_frame->app_priv) {
    gst_buffer_unref ((GstBuffer *) vvas_frame->app_priv);
  }

  for (plane_id = 0; plane_id < vvas_frame->n_planes; plane_id++) {
    vvas_xrt_free_bo (vvas_frame->bo[plane_id]);
  }

  memset (vvas_frame, 0x0, sizeof (VVASFrame));
}

static gboolean
find_kernel_lib_symbols (GstVvas_XFilter * self, Vvas_XFilter * kernel)
{
  kernel->lib_fd = dlopen (kernel->vvas_lib_path, RTLD_LAZY);
  if (!kernel->lib_fd) {
    GST_ERROR_OBJECT (self, " unable to open shared library %s",
        kernel->vvas_lib_path);
    return FALSE;
  }

  GST_INFO_OBJECT (self,
      "opened kernel library %s successfully with fd %p",
      kernel->vvas_lib_path, kernel->lib_fd);

  /* Clear any existing error */
  dlerror ();

  kernel->kernel_init_func = (VVASKernelInit) dlsym (kernel->lib_fd,
      "xlnx_kernel_init");
  if (kernel->kernel_init_func == NULL) {
    GST_ERROR_OBJECT (self,
        "could not find vvas_xfilter_init function. reason : %s", dlerror ());
    return FALSE;
  }

  kernel->kernel_start_func = (VVASKernelStartFunc) dlsym (kernel->lib_fd,
      "xlnx_kernel_start");
  if (kernel->kernel_start_func == NULL) {
    GST_ERROR_OBJECT (self,
        "could not find vvas_xfilter_start function. reason : %s", dlerror ());
    return FALSE;
  }

  kernel->kernel_done_func = (VVASKernelDoneFunc) dlsym (kernel->lib_fd,
      "xlnx_kernel_done");
  if (kernel->kernel_done_func == NULL) {
    GST_ERROR_OBJECT (self,
        "could not find vvas_xfilter_done function. reason : %s", dlerror ());
    return FALSE;
  }

  kernel->kernel_deinit_func = (VVASKernelDeInit) dlsym (kernel->lib_fd,
      "xlnx_kernel_deinit");
  if (kernel->kernel_deinit_func == NULL) {
    GST_ERROR_OBJECT (self,
        "could not find vvas_xfilter_deinit function. reason : %s", dlerror ());
    return FALSE;
  }
  return TRUE;
}

static gboolean
vvas_xfilter_allocate_sink_internal_pool (GstVvas_XFilter * self)
{
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstCaps *caps = NULL;
  guint size;
  Vvas_XFilter *kernel = self->priv->kernel;
  VVASKernel *vvas_handle = kernel->vvas_handle;

  caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM (self)->sinkpad);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  size = GST_VIDEO_INFO_SIZE (&info);

  if (kernel->name) {           /* HW IP */
    /* Use vvas pool and allocator if HW IP */
    allocator = gst_vvas_allocator_new (self->priv->dev_idx,
        USE_DMABUF, vvas_handle->in_mem_bank);
    pool = gst_vvas_buffer_pool_new (1, 1);

  } else {                      /* SW IP */
    /* Use video buffer pool and default allocator in SW ip */
    pool = gst_video_buffer_pool_new ();
  }

  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;

  config = gst_buffer_pool_get_config (pool);
  if (vvas_caps_get_sink_stride_align (vvas_handle) > 0 ||
      vvas_caps_get_sink_height_align (vvas_handle) > 0) {
    GstVideoAlignment video_align = { 0, };

    gst_video_alignment_reset (&video_align);
    video_align.padding_top = 0;
    video_align.padding_left = 0;
    video_align.padding_right = vvas_xfilter_get_padding_right (self, &info,
        vvas_caps_get_sink_stride_align (vvas_handle));
    video_align.padding_bottom =
        ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
        vvas_caps_get_src_height_align (vvas_handle)) -
        GST_VIDEO_INFO_HEIGHT (&info);
    GST_LOG_OBJECT (self, "padding_right = %d padding_bottom = %d",
        video_align.padding_right, video_align.padding_bottom);
    gst_video_info_align (&info, &video_align);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, &video_align);
    size = GST_VIDEO_INFO_SIZE (&info);
  }

  GST_LOG_OBJECT (self, "allocated internal sink pool %p with size = %d", pool,
      size);
  gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (&info),
      3, 0);
  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config on input pool");
    goto error;
  }

  if (self->priv->input_pool)
    gst_object_unref (self->priv->input_pool);

  self->priv->input_pool = pool;
  self->priv->internal_in_vinfo = gst_video_info_copy (&info);

  GST_INFO_OBJECT (self, "allocated %" GST_PTR_FORMAT " pool",
      self->priv->input_pool);
  gst_caps_unref (caps);
  if (allocator)
    gst_object_unref (allocator);

  return TRUE;

error:
  gst_caps_unref (caps);
  return FALSE;
}

static gboolean
gst_vvas_xfilter_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (trans);
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool;
  guint size;
  Vvas_XFilter *kernel = self->priv->kernel;
  VVASKernel *vvas_handle = kernel->vvas_handle;

  GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
      decide_query, query);

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstStructure *structure = NULL;
    GstAllocator *allocator = NULL;
    GstAllocationParams params = { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0,
      0
    };

    if (gst_query_get_n_allocation_params (query) > 0) {
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    } else {
      if (kernel->name) {
        /* Create a vvas allocator if it is a HW IP */
        allocator = gst_vvas_allocator_new (self->priv->dev_idx,
            USE_DMABUF, vvas_handle->in_mem_bank);
      }
      gst_query_add_allocation_param (query, allocator, &params);
    }

    if (kernel->name) {         /* HW IP */
      pool = gst_vvas_buffer_pool_new (1, 1);
    } else {                    /* SW IP */
      pool = gst_video_buffer_pool_new ();
    }

    GST_LOG_OBJECT (self, "allocated internal pool %p", pool);

    structure = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_set_params (structure, caps, size, MIN_POOL_BUFFERS,
        0);

    if (vvas_caps_get_sink_stride_align (vvas_handle) > 0
        || vvas_caps_get_sink_height_align (vvas_handle) > 0) {
      GstVideoAlignment video_align = { 0, };

      gst_video_alignment_reset (&video_align);
      video_align.padding_top = 0;
      video_align.padding_left = 0;
      video_align.padding_right = vvas_xfilter_get_padding_right (self, &info,
          vvas_caps_get_sink_stride_align (vvas_handle));
      video_align.padding_bottom = ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
          vvas_caps_get_sink_height_align (vvas_handle)) -
          GST_VIDEO_INFO_HEIGHT (&info);
      gst_video_info_align (&info, &video_align);

      gst_buffer_pool_config_add_option (structure,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
      gst_buffer_pool_config_set_video_alignment (structure, &video_align);
      size = GST_VIDEO_INFO_SIZE (&info);
      GST_LOG_OBJECT (self, "kernel stride align %d, size = %d",
          vvas_caps_get_sink_stride_align (vvas_handle), size);
    }

    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_set_allocator (structure, allocator, &params);


    if (!gst_buffer_pool_set_config (pool, structure))
      goto config_failed;

    GST_OBJECT_LOCK (self);
    gst_query_add_allocation_pool (query, pool, size, MIN_POOL_BUFFERS, 0);
    GST_OBJECT_UNLOCK (self);

    gst_object_unref (pool);
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

    GST_DEBUG_OBJECT (self, "prepared query %" GST_PTR_FORMAT, query);

    if (allocator)
      gst_object_unref (allocator);
  }

  return TRUE;

  /* ERRORS */
config_failed:
  {
    GST_ERROR_OBJECT (self, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }
}

static gboolean
gst_vvas_xfilter_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (trans);
  GstVideoInfo info;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  guint size, min, max;
  GstCaps *outcaps;
  gboolean update_allocator;
  gboolean alignment_matched, metadata_supported;
  gboolean update_pool;
  GstStructure *config = NULL;
  Vvas_XFilter *kernel = self->priv->kernel;
  VVASKernel *vvas_handle = kernel->vvas_handle;

  alignment_matched = metadata_supported = FALSE;

  gst_query_parse_allocation (query, &outcaps, NULL);
  if (outcaps && !gst_video_info_from_caps (&info, outcaps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from outcaps");
    goto error;
  }

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0) {
    /* try the allocator */
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    update_allocator = TRUE;
  } else {
    allocator = NULL;
    update_allocator = FALSE;
    gst_allocation_params_init (&params);
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
    if (min == 0)
      min = 2;
  } else {
    pool = NULL;
    min = 2;
    max = 0;
    size = info.size;

    update_pool = FALSE;
  }

  if (pool) {

    GstVideoAlignment video_align = { 0, };

    config = gst_buffer_pool_get_config (pool);

    GST_DEBUG_OBJECT (self, "got pool");
    if ((vvas_caps_get_src_stride_align (vvas_handle) > 0
            || vvas_caps_get_src_height_align (vvas_handle) > 0)
        && gst_buffer_pool_config_get_video_alignment (config,
            &video_align) == TRUE) {

      guint padded_width = 0;
      guint padded_height = 0;
      guint kernel_stride;
      guint kernel_req_elevation;
      guint downstream_stride;
      guint downstream_elevation;

      kernel_stride = vvas_xfilter_cal_stride (GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_FORMAT (&info),
          vvas_caps_get_src_stride_align (vvas_handle));

      padded_width =
          info.width + video_align.padding_right + video_align.padding_left;
      downstream_stride =
          vvas_xfilter_cal_stride (padded_width, GST_VIDEO_INFO_FORMAT (&info),
          1);

      kernel_req_elevation = ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
          vvas_caps_get_src_height_align (vvas_handle));
      padded_height =
          info.height + video_align.padding_top + video_align.padding_bottom;
      downstream_elevation = ALIGN (padded_height, 1);

      GST_DEBUG_OBJECT (self, "downstream stride = %d, kernel stride = %d",
          downstream_stride, kernel_stride);
      GST_DEBUG_OBJECT (self,
          "downstream elevation = %d, kernel elevation = %d",
          downstream_elevation, kernel_req_elevation);

      if (kernel_stride != downstream_stride
          || kernel_req_elevation != downstream_elevation) {
        /* stride requirement for kernel and downstrem not matching
         * Go for new pool */
        GST_INFO_OBJECT (self,
            "stride requirement for kernel and downstrem not matching");

        gst_query_remove_nth_allocation_pool (query, 0);
        gst_object_unref (pool);
        pool = NULL;
        min = 2;
        max = 0;

        update_pool = FALSE;
        alignment_matched = FALSE;
      } else {
        GST_DEBUG_OBJECT (self,
            "stride requirement for kernel and downstrem are matching");
        alignment_matched = TRUE;
      }
    } else {
      GST_INFO_OBJECT (self,
          "kernel do not have any stride alignment requirement");
      alignment_matched = TRUE;
    }

    gst_structure_free (config);
    config = NULL;
  }
#ifdef XLNX_EMBEDDED_PLATFORM
  /* TODO: Currently Kms buffer are not supported in PCIe platform */
  if (pool) {
    GstStructure *config = gst_buffer_pool_get_config (pool);

    if (gst_buffer_pool_config_has_option (config,
            "GstBufferPoolOptionKMSPrimeExport")) {
      gst_structure_free (config);
      goto next;
    }

    gst_structure_free (config);
    config = NULL;
    gst_object_unref (pool);
    pool = NULL;
    GST_DEBUG_OBJECT (self, "pool deos not have the KMSPrimeExport option, \
        unref the pool and create sdx allocator");
  }
#endif

  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    GST_INFO_OBJECT (self, "Downstream support GstVideoMeta metadata."
        "Copy output frame might not needed");

    metadata_supported = TRUE;
  }

  if (allocator) {
    if (!GST_IS_VVAS_ALLOCATOR (allocator)) {
      GST_DEBUG_OBJECT (self, "replace %" GST_PTR_FORMAT " to VVAS allocator",
          allocator);
      gst_object_unref (allocator);
      gst_allocation_params_init (&params);
      allocator = NULL;
    } else if (gst_vvas_allocator_get_device_idx (allocator) !=
        self->priv->dev_idx) {
      GST_INFO_OBJECT (self,
          "downstream allocator (%d) and filter (%d) are on different devices",
          gst_vvas_allocator_get_device_idx (allocator), self->priv->dev_idx);
      gst_object_unref (allocator);
      gst_allocation_params_init (&params);
      allocator = NULL;
    }
  } else {
    gst_allocation_params_init (&params);
    allocator = NULL;
  }

  if (!allocator) {
    /* making sdx allocator for the HW mode without dmabuf */
    allocator = gst_vvas_allocator_new (self->priv->dev_idx,
        USE_DMABUF, vvas_handle->out_mem_bank);

    GST_DEBUG_OBJECT (self, "Creating new vvas allocator %p", allocator);
    //params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
    // TODO: Need to add XRT related flags here
  }
#ifdef XLNX_EMBEDDED_PLATFORM
next:
#endif
  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);

  if (allocator)
    gst_object_unref (allocator);

  if (pool == NULL) {
    GstVideoAlignment video_align = { 0, };
    gst_video_alignment_reset (&video_align);

    GST_DEBUG_OBJECT (self, "no pool, making new pool");
    if (kernel->name) {         /* Hardware IP */
      if (vvas_caps_get_src_stride_align (vvas_handle) > 0
          || vvas_caps_get_src_height_align (vvas_handle) > 0) {
        pool =
            gst_vvas_buffer_pool_new (vvas_caps_get_src_stride_align
            (vvas_handle), vvas_caps_get_src_height_align (vvas_handle));
        GST_INFO_OBJECT (self, "created new pool %p %" GST_PTR_FORMAT, pool,
            pool);
        GST_INFO_OBJECT (self, "kernel stride = %d",
            vvas_xfilter_cal_stride (GST_VIDEO_INFO_WIDTH (&info),
                GST_VIDEO_INFO_FORMAT (&info),
                vvas_caps_get_src_stride_align (vvas_handle)));

        config = gst_buffer_pool_get_config (pool);
        video_align.padding_top = 0;
        video_align.padding_left = 0;
        video_align.padding_right = vvas_xfilter_get_padding_right (self, &info,
            vvas_caps_get_src_stride_align (vvas_handle));
        video_align.padding_bottom = ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
            vvas_caps_get_src_height_align (vvas_handle)) -
            GST_VIDEO_INFO_HEIGHT (&info);

        gst_video_info_align (&info, &video_align);

        gst_buffer_pool_config_add_option (config,
            GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
        gst_buffer_pool_config_set_allocator (config, allocator, &params);
        gst_buffer_pool_config_set_video_alignment (config, &video_align);
        size = GST_VIDEO_INFO_SIZE (&info);

      } else {                  /* vvas pool with no alignment */

        GST_INFO_OBJECT (self, "Creating a vvas buffer pool without alignment");
        pool = gst_vvas_buffer_pool_new (1, 1);
        GST_INFO_OBJECT (self,
            "kernel do not have any stride alignment requirement");
        alignment_matched = TRUE;
        config = gst_buffer_pool_get_config (pool);
        gst_buffer_pool_config_set_allocator (config, allocator, &params);
      }
    } else {                    /* Software IP */

      if (vvas_caps_get_src_stride_align (vvas_handle) > 0
          || vvas_caps_get_src_height_align (vvas_handle) > 0) {
        pool = gst_video_buffer_pool_new ();
        GST_INFO_OBJECT (self, "created new pool %p %" GST_PTR_FORMAT, pool,
            pool);
        GST_INFO_OBJECT (self, "kernel stride = %d",
            vvas_xfilter_cal_stride (GST_VIDEO_INFO_WIDTH (&info),
                GST_VIDEO_INFO_FORMAT (&info),
                vvas_caps_get_src_stride_align (vvas_handle)));

        config = gst_buffer_pool_get_config (pool);
        video_align.padding_top = 0;
        video_align.padding_left = 0;
        video_align.padding_right = vvas_xfilter_get_padding_right (self, &info,
            vvas_caps_get_src_stride_align (vvas_handle));
        video_align.padding_bottom = ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
            vvas_caps_get_src_height_align (vvas_handle)) -
            GST_VIDEO_INFO_HEIGHT (&info);
        gst_video_info_align (&info, &video_align);

        gst_buffer_pool_config_add_option (config,
            GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
        gst_buffer_pool_config_set_video_alignment (config, &video_align);
        size = GST_VIDEO_INFO_SIZE (&info);

      } else {                  /* SW pool with no alignment */

        GST_INFO_OBJECT (self, "Creating a video buffer pool");
        pool = gst_video_buffer_pool_new ();
        GST_INFO_OBJECT (self,
            "kernel do not have any stride alignment requirement");
        alignment_matched = TRUE;
      }


    }
  }
  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  if (config == NULL)
    config = gst_buffer_pool_get_config (pool);


  GST_DEBUG_OBJECT (self, "set config of pool %p with size %d", pool, size);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "failed to set config on own pool %p", pool);
    goto error;
  }

  if (alignment_matched == FALSE && metadata_supported == FALSE) {
    GST_ERROR_OBJECT (self, "slow copy to external buffer will happened");
    self->priv->need_copy = TRUE;
  }

  self->priv->internal_out_vinfo = gst_video_info_copy (&info);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);

error:
  GST_ERROR_OBJECT (trans, "Invalid video caps %" GST_PTR_FORMAT, outcaps);

  if (allocator)
    gst_object_unref (allocator);
  if (pool)
    gst_object_unref (pool);

  return FALSE;
}

static gboolean
vvas_xfilter_init (GstVvas_XFilter * self)
{
  GstVvas_XFilterPrivate *priv = self->priv;
  int iret;
  VVASKernel *vvas_handle = priv->kernel->vvas_handle;
  int i;

  if (priv->kernel->name || priv->kernel->is_softkernel) {
    /* xclbin need to be downloaded only when hardkernel or softkernel is used */
    if (vvas_xrt_download_xclbin (self->priv->xclbin_loc,
            self->priv->dev_handle, &self->priv->xclbinId))
      return FALSE;
  }

  if (priv->kernel->name) {
    iret = vvas_xrt_open_context (priv->dev_handle, priv->xclbinId,
        &priv->kern_handle, priv->kernel->name, priv->kernel->shared_access);
    if (iret) {
      GST_ERROR_OBJECT (self, "failed to open XRT context ...:%d", iret);
      return FALSE;
    }
    /* Assigning kernel handle */
    vvas_handle->kern_handle = priv->kern_handle;
  }

  /* Populate the kernel name */
  vvas_handle->name =
      (uint8_t *) g_strdup_printf ("libkrnl_%s", GST_ELEMENT_NAME (self));

  vvas_handle->dev_handle = priv->dev_handle;
#if defined(XLNX_PCIe_PLATFORM)
  vvas_handle->is_softkernel = priv->kernel->is_softkernel;
  if (priv->kernel->is_softkernel) {
    vvas_handle->cu_idx = priv->sk_cur_idx;
  } else {
    vvas_handle->cu_idx = priv->kernel->cu_idx;
  }
#else
  vvas_handle->cu_idx = priv->kernel->cu_idx;
#endif
  vvas_handle->kernel_config = priv->kernel->config;

  GST_INFO_OBJECT (self, "vvas library cu_idx = %d", vvas_handle->cu_idx);

  /* no need to do alloc in vvas library, so no callbacks required */
  vvas_handle->alloc_func = vvas_buffer_alloc;
  vvas_handle->free_func = vvas_buffer_free;
  vvas_handle->cb_user_data = self;

  pthread_mutex_lock (&count_mutex);    /* lock for TDM */
  iret = priv->kernel->kernel_init_func (vvas_handle);
  if (iret < 0) {
    GST_ERROR_OBJECT (self, "failed to do kernel init..");
    return FALSE;
  }
  pthread_mutex_unlock (&count_mutex);
  GST_INFO_OBJECT (self, "completed kernel init");

  for (i = 0; i < MAX_PRIV_POOLS; i++)
    priv->priv_pools[i] = NULL;

  return TRUE;
}

static gboolean
vvas_xfilter_deinit (GstVvas_XFilter * self)
{
  GstVvas_XFilterPrivate *priv = self->priv;
  int iret, i;
  gint cu_idx = -1;
  gboolean has_error = FALSE;

  if (priv->kernel) {
    if (priv->kernel->input[0])
      free (priv->kernel->input[0]);

    if (priv->kernel->output[0])
      free (priv->kernel->output[0]);

    cu_idx = priv->kernel->cu_idx;

    if (priv->kernel->kernel_deinit_func) {
      iret = priv->kernel->kernel_deinit_func (priv->kernel->vvas_handle);
      if (iret < 0) {
        GST_ERROR_OBJECT (self, "failed to do kernel deinit..");
      }
      GST_DEBUG_OBJECT (self, "successfully completed deinit");
    }

    if (priv->kernel->vvas_handle) {
      /* De-allocate the name */
      if (priv->kernel->vvas_handle->name) {
        g_free (priv->kernel->vvas_handle->name);
      }

      free (priv->kernel->vvas_handle);
    }
#ifdef XLNX_PCIe_PLATFORM
    if (priv->kernel->skinfo) {
      if (priv->kernel->skinfo->skname) {
        g_free (priv->kernel->skinfo->skname);
        priv->kernel->skinfo->skname = NULL;
      }
      free (priv->kernel->skinfo);
    }
#endif

    if (priv->kernel->config)
      json_decref (priv->kernel->config);

    if (priv->kernel->vvas_lib_path)
      g_free (priv->kernel->vvas_lib_path);

    if (priv->kernel->name)
      free (priv->kernel->name);

    free (priv->kernel);
    priv->kernel = NULL;
  }

  for (i = 0; i < MAX_PRIV_POOLS; i++) {
    if (priv->priv_pools[i]) {
      gst_buffer_pool_set_active (priv->priv_pools[i], FALSE);
      gst_object_unref (priv->priv_pools[i]);
      priv->priv_pools[i] = NULL;
    }
  }

  if (priv->xclbin_loc)
    free (priv->xclbin_loc);

  if (priv->dev_handle) {
    if (priv->kern_handle) {
      GST_INFO_OBJECT (self, "closing context for cu_idx %d", cu_idx);
      vvas_xrt_close_context (priv->kern_handle);
    }
    vvas_xrt_close_device (priv->dev_handle);
  }
  return has_error ? FALSE : TRUE;
}

static gboolean
gst_vvas_xfilter_start (GstBaseTransform * trans)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (trans);
  GstVvas_XFilterPrivate *priv = self->priv;
  json_t *root = NULL, *kernel, *value;
  json_error_t error;
  gchar *lib_path = NULL;
  VVASKernel *vvas_handle;

  self->priv = priv;
  priv->dev_idx = DEFAULT_DEVICE_INDEX;
  priv->in_vinfo = gst_video_info_new ();
  priv->out_vinfo = gst_video_info_new ();
  priv->element_mode = VVAS_ELEMENT_MODE_NOT_SUPPORTED;
  priv->do_init = TRUE;

  /* get root json object */
  root = json_load_file (self->json_file, JSON_DECODE_ANY, &error);
  if (!root) {
    GST_ERROR_OBJECT (self, "failed to load json file. reason %s", error.text);
    goto error;
  }

  /* get xclbin location */
  value = json_object_get (root, "xclbin-location");
  if (json_is_string (value)) {
    priv->xclbin_loc = g_strdup (json_string_value (value));
    GST_INFO_OBJECT (self, "xclbin location to download %s", priv->xclbin_loc);
  } else {
    priv->xclbin_loc = NULL;
    GST_INFO_OBJECT (self, "xclbin path is not set");
  }

  /* get VVAS library repository path */
  value = json_object_get (root, "vvas-library-repo");
  if (!value) {
    GST_DEBUG_OBJECT (self,
        "library repo path does not exist.taking default %s",
        DEFAULT_VVAS_LIB_PATH);
    lib_path = g_strdup (DEFAULT_VVAS_LIB_PATH);
  } else {
    gchar *path = g_strdup (json_string_value (value));

    if (!g_str_has_suffix (path, "/")) {
      lib_path = g_strconcat (path, "/", NULL);
      g_free (path);
    } else {
      lib_path = path;
    }
  }

  /* get kernel mode : passthrough/inplace/transform */
  value = json_object_get (root, "element-mode");
  if (!json_is_string (value)) {
    GST_ERROR_OBJECT (self,
        "\"kernel-mode\" not set. possible values (passthrough/inplace/transform)");
    goto error;
  }

  GST_INFO_OBJECT (self, "element is going to operate in %s",
      json_string_value (value));
  priv->element_mode = get_kernel_mode (json_string_value (value));
  if (priv->element_mode == VVAS_ELEMENT_MODE_NOT_SUPPORTED) {
    GST_ERROR_OBJECT (self, "unsupported element-mode %s",
        json_string_value (value));
    goto error;
  }

  gst_base_transform_set_passthrough (trans,
      priv->element_mode == VVAS_ELEMENT_MODE_PASSTHROUGH);
  gst_base_transform_set_in_place (trans,
      priv->element_mode == VVAS_ELEMENT_MODE_IN_PLACE);

  kernel = json_object_get (root, "kernel");
  if (!json_is_object (kernel)) {
    GST_ERROR_OBJECT (self, "failed to find kernel object");
    goto error;
  }

  priv->kernel = (Vvas_XFilter *) calloc (1, sizeof (Vvas_XFilter));
  if (!priv->kernel) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }
  priv->kernel->is_softkernel = FALSE;
  priv->kernel->cu_idx = -1;

  value = json_object_get (kernel, "library-name");
  if (!json_is_string (value)) {
    GST_ERROR_OBJECT (self, "library name is not of string type");
    goto error;
  }

  /* absolute path VVAS library */
  priv->kernel->vvas_lib_path =
      g_strconcat (lib_path, json_string_value (value), NULL);
  GST_DEBUG_OBJECT (self, "vvas library path %s", priv->kernel->vvas_lib_path);

  /* get kernel name */
  value = json_object_get (kernel, "kernel-name");
  if (value) {
    if (!json_is_string (value)) {
      GST_ERROR_OBJECT (self, "primary kernel name is not of string type");
      goto error;
    }
    priv->kernel->name = g_strdup (json_string_value (value));
  } else {
    priv->kernel->name = NULL;
  }
  GST_INFO_OBJECT (self, "Primary kernel name %s", priv->kernel->name);

#ifdef XLNX_PCIe_PLATFORM
  /* softkernels exist in PCIe based platforms only */
  value = json_object_get (kernel, "softkernel");
  if (json_is_object (value)) {
    json_t *skernel = value;

    value = json_object_get (skernel, "name");
    if (!json_is_string (value)) {
      GST_ERROR_OBJECT (self, "softkernel name is not of string type");
      goto error;
    }

    priv->kernel->skinfo =
        (VvasSoftKernelInfo *) calloc (1, sizeof (VvasSoftKernelInfo));
    if (!priv->kernel->skinfo) {
      GST_ERROR_OBJECT (self, "failed to allocate memory");
      goto error;
    }

    priv->kernel->skinfo->skname = g_strdup (json_string_value (value));
    GST_INFO_OBJECT (self, "received kernel is a softkernel with name %s",
        priv->kernel->skinfo->skname);
    priv->kernel->is_softkernel = TRUE;
  }
#endif
  /* get kernel access type */
  value = json_object_get (kernel, "kernel-access-mode");
  if (value) {
    if (!json_is_string (value)) {
      GST_WARNING_OBJECT (self, "kernel-access-mode is not of string type, \
                               it can be \"shared\" or \"exclusive\", \
                               considering \"shared\" as default");
      /* Lets go with default "shared" */
      priv->kernel->shared_access = true;
    } else {
      /* check what access type user has seleced, "shared" or "exclusive" */
      if (!strcmp (json_string_value (value), "shared")) {
        priv->kernel->shared_access = true;
      } else {
        priv->kernel->shared_access = false;
      }
    }
  } else {
    GST_INFO_OBJECT (self, "kernel-access-mode not set, using \"shared\" mode");
    /* Lets go with defalt "shared" */
    priv->kernel->shared_access = true;
  }

  /* get vvas kernel lib internal configuration */
  value = json_object_get (kernel, "config");
  if (!json_is_object (value)) {
    GST_ERROR_OBJECT (self, "config is not of object type");
    goto error;
  }

  priv->kernel->config = json_deep_copy (value);
  GST_DEBUG_OBJECT (self, "kernel config size = %lu", json_object_size (value));

  /* find whether required symbols present or not */
  if (!find_kernel_lib_symbols (self, priv->kernel)) {
    GST_ERROR_OBJECT (self, "failed find symbols in kernel lib...");
    goto error;
  }

  vvas_handle = (VVASKernel *) calloc (1, sizeof (VVASKernel));
  if (!vvas_handle) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  } else {
    vvas_handle->in_mem_bank = DEFAULT_MEM_BANK;
    vvas_handle->out_mem_bank = DEFAULT_MEM_BANK;
  }

  priv->kernel->vvas_handle = vvas_handle;

  if (priv->do_init) {

    if (!vvas_xrt_open_device (self->priv->dev_idx, &self->priv->dev_handle))
      goto error;

    if (!vvas_xfilter_init (self))
      goto error;

    priv->do_init = FALSE;
  }

  memset (priv->kernel->input, 0x0, sizeof (VVASFrame *) * MAX_NUM_OBJECT);
  memset (priv->kernel->output, 0x0, sizeof (VVASFrame *) * MAX_NUM_OBJECT);

  priv->kernel->input[0] = (VVASFrame *) calloc (1, sizeof (VVASFrame));
  if (NULL == priv->kernel->input[0]) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    return FALSE;
  }

  if (priv->element_mode == VVAS_ELEMENT_MODE_TRANSFORM) {
    priv->kernel->output[0] = (VVASFrame *) calloc (1, sizeof (VVASFrame));
    if (NULL == priv->kernel->output[0]) {
      GST_ERROR_OBJECT (self, "failed to allocate memory");
      return FALSE;
    }
  }

  if (lib_path)
    g_free (lib_path);
  if (root)
    json_decref (root);
  return TRUE;

error:
  if (lib_path)
    g_free (lib_path);
  if (root)
    json_decref (root);
  return FALSE;
}

static gboolean
gst_vvas_xfilter_stop (GstBaseTransform * trans)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (trans);
  GST_DEBUG_OBJECT (self, "stopping");
  gst_video_info_free (self->priv->out_vinfo);
  gst_video_info_free (self->priv->in_vinfo);
  vvas_xfilter_deinit (self);
  return TRUE;
}

static GstCaps *
vvas_kernelcap_to_gst_cap (kernelcaps * kcap)
{
  GValue list = { 0, };
  GValue aval = { 0, };
  uint8_t i;
  GstCaps *cap = gst_caps_new_empty ();
  GstStructure *s;              // = gst_structure_new ("video/x-raw", NULL);

  if (kcap == NULL)
    return cap;

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&aval, G_TYPE_STRING);

  if (kcap->range_height == true) {
    s = gst_structure_new ("video/x-raw", "height", GST_TYPE_INT_RANGE,
        kcap->lower_height, kcap->upper_height, NULL);
  } else {
    s = gst_structure_new ("video/x-raw", "height", G_TYPE_INT,
        kcap->lower_height, NULL);
  }

  if (kcap->range_width == true) {
    gst_structure_set (s, "width", GST_TYPE_INT_RANGE, kcap->lower_width,
        kcap->upper_width, NULL);
  } else {
    gst_structure_set (s, "width", G_TYPE_INT, kcap->lower_width, NULL);
  }

  for (i = 0; i < kcap->num_fmt; i++) {
    const char *fourcc =
        gst_video_format_to_string (get_gst_format (kcap->fmt[i]));

    g_value_set_string (&aval, fourcc);
    gst_value_list_append_value (&list, &aval);
    gst_structure_set_value (s, "format", &list);

    g_value_reset (&aval);
  }

  gst_caps_append_structure (cap, s);

  g_value_reset (&aval);
  g_value_unset (&list);
  return cap;
}

static gboolean
gst_vvas_xfilter_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (trans);
  Vvas_XFilter *kernel = self->priv->kernel;
  Vvas_XFilterMode element_mode = self->priv->element_mode;
  VVASKernel *vvas_handle = NULL;
  kernelpads **kernel_pads, *kernel_pad;
  uint8_t nu_pads;              /* number of sink/src pads suported by kenrel */
  uint8_t pad_index;            /* incomming pad index */
  uint8_t nu_caps;              /* number of caps supported by one pad */
  kernelcaps **kcaps;
  gboolean ret = TRUE;
  uint8_t i;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:{
      GstCaps *newcap, *allcaps;

      if (self->priv->do_init == TRUE)
        return FALSE;

      vvas_handle = kernel->vvas_handle;

      if (!vvas_handle->padinfo ||
          vvas_handle->padinfo->nature == VVAS_PAD_DEFAULT) {
        /* Do default handling */
        GST_DEBUG_OBJECT (self,
            "padinfo == NULL || nature == VVAS_PAD_DEFAULT, Do default handling");
        break;
      }

      if (element_mode == VVAS_ELEMENT_MODE_PASSTHROUGH
          || element_mode == VVAS_ELEMENT_MODE_IN_PLACE) {
        /* Same buffer for sink and src,
         * so the same caps for sink and src pads
         * and for all pads
         */
        kernel_pads = vvas_handle->padinfo->sinkpads;
        nu_pads = (direction == GST_PAD_SRC) ?
            vvas_handle->padinfo->nu_srcpad : vvas_handle->padinfo->nu_sinkpad;
      } else {
        kernel_pads = (direction == GST_PAD_SRC) ?
            vvas_handle->padinfo->srcpads : vvas_handle->padinfo->sinkpads;
        nu_pads = (direction == GST_PAD_SRC) ?
            vvas_handle->padinfo->nu_srcpad : vvas_handle->padinfo->nu_sinkpad;
      }
      GST_INFO_OBJECT (self, "element_mode %d nu_pads %d %s", element_mode,
          nu_pads, direction == GST_PAD_SRC ? "SRC" : "SINK");

      if (!kernel_pads) {
        GST_DEBUG_OBJECT (self,
            "Kernel has not set any pads information, so doing default handling");
        break;
      }

      pad_index = 0;            /* TODO: how to get incoming pad number */
      kernel_pad = (kernel_pads[pad_index]);
      nu_caps = kernel_pad->nu_caps;
      kcaps = kernel_pad->kcaps;        /* Base of pad's caps */
      GST_DEBUG_OBJECT (self, "nu_caps = %d", nu_caps);


      allcaps = gst_caps_new_empty ();
      /* 0th element has high priority */
      for (i = 0; i < nu_caps; i++) {
        kernelcaps *kcap = (kcaps[i]);

        newcap = vvas_kernelcap_to_gst_cap (kcap);
        gst_caps_append (allcaps, newcap);
      }

      if ((vvas_handle->padinfo->nature != VVAS_PAD_RIGID)) {
        GstCaps *padcaps;
        GST_DEBUG_OBJECT (self, "nature != VVAS_PAD_RIGID");

        if (direction == GST_PAD_SRC) {
          padcaps = gst_pad_get_pad_template_caps (trans->srcpad);
        } else {
          padcaps = gst_pad_get_pad_template_caps (trans->sinkpad);
        }

        gst_caps_append (allcaps, padcaps);
      }

      {
        gchar *str = gst_caps_to_string (allcaps);
        GST_INFO_OBJECT (self, "caps from kernel = %s", str);
        g_free (str);
      }

      gst_query_set_caps_result (query, allcaps);
      gst_caps_unref (allcaps);

      return TRUE;
    }
    default:
      ret = TRUE;
      break;
  }

  GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction, query);
  return ret;
}

static gboolean
gst_vvas_xfilter_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (trans);
  gboolean bret = TRUE;
  GstVvas_XFilterPrivate *priv = self->priv;

  GST_INFO_OBJECT (self,
      "incaps = %" GST_PTR_FORMAT "and outcaps = %" GST_PTR_FORMAT, incaps,
      outcaps);

  if (!gst_video_info_from_caps (priv->in_vinfo, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse input caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (priv->out_vinfo, outcaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse output caps");
    return FALSE;
  }
  return bret;
}

static GstCaps *
gst_vvas_xfilter_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (trans);
  GstCaps *othercaps, *tmp;
  GstCaps *result;
  guint ncaps, idx;
  GstCapsFeatures *feature;

  othercaps = gst_caps_new_empty ();

  ncaps = gst_caps_get_size (caps);

  for (idx = 0; idx < ncaps; idx++) {
    GstStructure *st;

    st = gst_caps_get_structure (caps, idx);
    feature = gst_caps_get_features (caps, idx);

    if (idx > 0 && gst_caps_is_subset_structure_full (othercaps, st, feature))
      continue;

    st = gst_structure_copy (st);
    if (self->priv->element_mode == VVAS_ELEMENT_MODE_TRANSFORM) {
      if (!gst_caps_features_is_any (feature)
          && gst_caps_features_is_equal (feature,
              GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY))
        gst_structure_remove_fields (st, "format", "colorimetry", "chroma-site",
            "width", "height", "pixel-aspect-ratio", NULL);
    }

    gst_caps_append_structure_full (othercaps, st,
        gst_caps_features_copy (feature));
  }

  if (filter) {
    /* get first intersected caps */
    tmp = gst_caps_intersect_full (filter, othercaps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (othercaps);
    othercaps = tmp;
  }

  result = othercaps;

  GST_DEBUG_OBJECT (trans, "transformed caps from %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

static GstCaps *
gst_vvas_xfilter_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  return gst_vvas_utils_fixate_caps (GST_ELEMENT (trans), direction, caps,
      othercaps);
}

static void
gst_vvas_xfilter_class_init (GstVvas_XFilterClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *transform_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_vvas_xfilter_set_property;
  gobject_class->get_property = gst_vvas_xfilter_get_property;
  gobject_class->finalize = gst_vvas_xfilter_finalize;

  transform_class->start = gst_vvas_xfilter_start;
  transform_class->stop = gst_vvas_xfilter_stop;
  transform_class->set_caps = gst_vvas_xfilter_set_caps;
  transform_class->transform_caps = gst_vvas_xfilter_transform_caps;
  transform_class->fixate_caps = gst_vvas_xfilter_fixate_caps;
  transform_class->query = gst_vvas_xfilter_query;
  transform_class->decide_allocation = gst_vvas_xfilter_decide_allocation;
  transform_class->propose_allocation = gst_vvas_xfilter_propose_allocation;
  transform_class->submit_input_buffer = gst_vvas_xfilter_submit_input_buffer;
  transform_class->generate_output = gst_vvas_xfilter_generate_output;
  transform_class->transform_ip = gst_vvas_xfilter_transform_ip;
  transform_class->transform = gst_vvas_xfilter_transform;

  g_object_class_install_property (gobject_class, PROP_CONFIG_LOCATION,
      g_param_spec_string ("kernels-config",
          "VVAS kernels json config file location",
          "Location of the kernels config file in json format", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_DYNAMIC_CONFIG,
      g_param_spec_string ("dynamic-config",
          "Kernel's dynamic json config string",
          "String contains dynamic json configuration of kernel", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

#if defined(XLNX_PCIe_PLATFORM)
  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("dev-idx", "Device index",
          "Index of the device on which IP/kernel resides", 0, 31,
          DEFAULT_DEVICE_INDEX, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SK_CURRENT_INDEX,
      g_param_spec_int ("sk-cur-idx", "Current softkernel index",
          "Current softkernel index", 0, 31, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  gst_element_class_set_details_simple (gstelement_class,
      "VVAS Generic Filter Plugin",
      "Filter/Effect/Video",
      "Performs operations on HW IP/SW IP/Softkernel using VVAS library APIs",
      "Xilinx Inc <www.xilinx.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  /*
   * Will be emitted when kernel is sucessfully done.
   */
  vvas_signals[SIGNAL_VVAS] =
      g_signal_new ("vvas-kernel-done", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xfilter_debug, "vvas_xfilter", 0,
      "VVAS Generic Filter plugin");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_vvas_xfilter_init (GstVvas_XFilter * self)
{
  GstVvas_XFilterPrivate *priv = GST_VVAS_XFILTER_PRIVATE (self);
  self->priv = priv;
  priv->dev_idx = DEFAULT_DEVICE_INDEX;
  priv->element_mode = VVAS_ELEMENT_MODE_NOT_SUPPORTED;
  priv->do_init = TRUE;
  priv->need_copy = FALSE;
  priv->dyn_json_config = NULL;
  priv->kern_handle = NULL;
}

static void
gst_vvas_xfilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (object);

  switch (prop_id) {
    case PROP_CONFIG_LOCATION:
      if (GST_STATE (self) != GST_STATE_NULL) {
        g_warning
            ("can't set json_file path when instance is NOT in NULL state");
        return;
      }
      self->json_file = g_value_dup_string (value);
      break;
    case PROP_DYNAMIC_CONFIG:
      self->dyn_config = g_value_dup_string (value);
      if (self->priv->dyn_json_config) {
        json_decref (self->priv->dyn_json_config);
      }
      self->priv->dyn_json_config =
          json_loads (self->dyn_config, JSON_DECODE_ANY, NULL);
      break;
#if defined(XLNX_PCIe_PLATFORM)
    case PROP_SK_CURRENT_INDEX:
      self->priv->sk_cur_idx = g_value_get_int (value);
      break;
    case PROP_DEVICE_INDEX:
      self->priv->dev_idx = g_value_get_int (value);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_xfilter_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (object);

  switch (prop_id) {
    case PROP_CONFIG_LOCATION:
      g_value_set_string (value, self->json_file);
      break;
    case PROP_DYNAMIC_CONFIG:
      g_value_set_string (value, self->dyn_config);
      break;
#if defined(XLNX_PCIe_PLATFORM)
    case PROP_SK_CURRENT_INDEX:
      g_value_set_int (value, self->priv->sk_cur_idx);
      break;
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, self->priv->dev_idx);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_xfilter_finalize (GObject * obj)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (obj);

  if (self->priv->input_pool)
    gst_object_unref (self->priv->input_pool);
  if (self->priv->internal_out_vinfo)
    gst_video_info_free (self->priv->internal_out_vinfo);
  if (self->priv->internal_in_vinfo)
    gst_video_info_free (self->priv->internal_in_vinfo);

  g_free (self->json_file);
  if (self->dyn_config)
    g_free (self->dyn_config);
  if (self->priv->dyn_json_config)
    json_decref (self->priv->dyn_json_config);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static gboolean
vvas_xfilter_copy_input_buffer (GstVvas_XFilter * self, GstBuffer * inbuf,
    GstBuffer ** internal_inbuf)
{
  GstBuffer *new_inbuf;
  GstFlowReturn fret;
  GstVideoFrame in_vframe, new_vframe;
  gboolean bret;

  memset (&in_vframe, 0x0, sizeof (GstVideoFrame));
  memset (&new_vframe, 0x0, sizeof (GstVideoFrame));

  if (!self->priv->input_pool) {
    /* allocate input internal pool */
    bret = vvas_xfilter_allocate_sink_internal_pool (self);
    if (!bret)
      goto error;

  }
  if (!gst_buffer_pool_is_active (self->priv->input_pool))
    gst_buffer_pool_set_active (self->priv->input_pool, TRUE);

  /* acquire buffer from own input pool */
  fret =
      gst_buffer_pool_acquire_buffer (self->priv->input_pool, &new_inbuf, NULL);
  if (fret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
        self->priv->input_pool);
    goto error;
  }
  GST_LOG_OBJECT (self, "acquired buffer %p from own pool", new_inbuf);

  /* map internal buffer in write mode */
  if (!gst_video_frame_map (&new_vframe, self->priv->internal_in_vinfo,
          new_inbuf, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "failed to map internal input buffer");
    goto error;
  }

  /* map input buffer in read mode */
  if (!gst_video_frame_map (&in_vframe, self->priv->in_vinfo, inbuf,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "failed to map input buffer");
    goto error;
  }
  GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, self,
      "slow copy to internal buffer");

  gst_video_frame_copy (&new_vframe, &in_vframe);
  gst_video_frame_unmap (&in_vframe);
  gst_video_frame_unmap (&new_vframe);
  gst_buffer_copy_into (new_inbuf, inbuf,
      (GstBufferCopyFlags) GST_BUFFER_COPY_METADATA, 0, -1);
  *internal_inbuf = new_inbuf;

  return TRUE;

error:
  if (in_vframe.data[0]) {
    gst_video_frame_unmap (&in_vframe);
  }
  if (new_vframe.data[0]) {
    gst_video_frame_unmap (&new_vframe);
  }
  return FALSE;
}

static gboolean
vvas_xfilter_validate_inbuf (GstVvas_XFilter * self, GstBuffer * inbuf,
    guint * stride)
{
  GstStructure *in_config = NULL;
  GstVideoAlignment in_align = { 0, };
  gboolean use_inpool = FALSE;
  Vvas_XFilter *kernel = self->priv->kernel;
  VVASKernel *vvas_handle = kernel->vvas_handle;
  guint kernel_stride, kernel_elev;
  GstVideoMeta *vmeta = NULL;

  kernel_stride =
      vvas_xfilter_cal_stride (GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo),
      GST_VIDEO_INFO_FORMAT (self->priv->in_vinfo),
      vvas_caps_get_sink_stride_align (vvas_handle));
  kernel_elev =
      ALIGN (GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo),
      vvas_caps_get_sink_height_align (vvas_handle));

  vmeta = gst_buffer_get_video_meta (inbuf);

  if (vvas_caps_get_sink_stride_align (vvas_handle) > 0 ||
      vvas_caps_get_sink_height_align (vvas_handle) > 0) {

    if (inbuf->pool && (in_config = gst_buffer_pool_get_config (inbuf->pool)) &&
        gst_buffer_pool_config_get_video_alignment (in_config, &in_align) &&
        (in_align.padding_right || in_align.padding_bottom)) {
      guint kernel_padding_right, kernel_padding_bottom;
      kernel_padding_right = !kernel_stride ? 0 :
          kernel_stride - GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo);
      kernel_padding_bottom = !kernel_elev ? 0 :
          kernel_elev - GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo);

      GST_DEBUG_OBJECT (self,
          "padding alignment not matching, in_align.padding_right = %d in_align.padding_bottom =%d",
          in_align.padding_right, in_align.padding_bottom);
      GST_DEBUG_OBJECT (self,
          "sink_stride_align = %d, kernel_padding_right = %d, kernel_padding_bottom = %d",
          vvas_caps_get_sink_stride_align (vvas_handle),
          kernel_padding_right, kernel_padding_bottom);
      if ((in_align.padding_right != kernel_padding_right) ||
          in_align.padding_bottom != kernel_padding_bottom) {

        use_inpool = TRUE;
        GST_INFO_OBJECT (self,
            "padding alignment not matching, use our internal pool");
      }
    } else if (vmeta) {

      GST_LOG_OBJECT (self,
          "kernel required stride = %u & vmeta stride = %ul", kernel_stride,
          vmeta->stride[0]);
      if (vmeta->stride[0] != kernel_stride) {
        use_inpool = TRUE;
        GST_INFO_OBJECT (self,
            "vmeta stride are not matching, use our internal pool");
      }
    } else {
      GST_DEBUG_OBJECT (self,
          "kernel required stride = %u & info stride = %u", kernel_stride,
          GST_VIDEO_INFO_PLANE_STRIDE (self->priv->in_vinfo, 0));
      if (GST_VIDEO_INFO_PLANE_STRIDE (self->priv->in_vinfo,
              0) != kernel_stride) {
        use_inpool = TRUE;
        GST_LOG_OBJECT (self,
            "kernel required stride = %u & info stride = %u", kernel_stride,
            GST_VIDEO_INFO_PLANE_STRIDE (self->priv->in_vinfo, 0));
        GST_INFO_OBJECT (self,
            "strides are not matching, use our internal pool");
      }
    }
  }

  if (use_inpool == TRUE) {
    *stride = kernel_stride;
  } else if (vmeta) {
    *stride = vmeta->stride[0];
  } else {
    GST_DEBUG_OBJECT (self, "video meta not present in buffer");
    *stride = GST_VIDEO_INFO_PLANE_STRIDE (self->priv->in_vinfo, 0);
  }

  return use_inpool;
}

static gboolean
vvas_xfilter_prepare_input_frame (GstVvas_XFilter * self, GstBuffer * inbuf,
    GstBuffer ** new_inbuf, VVASFrame * vvas_frame, GstVideoFrame * in_vframe)
{
  GstVvas_XFilterPrivate *priv = self->priv;
  VVASKernel *vvas_handle = priv->kernel->vvas_handle;
  guint64 phy_addr = 0;
  vvasBOHandle bo_handle = NULL;
  gboolean free_bo = FALSE;
  guint plane_id;
  gboolean bret = FALSE;
  GstVideoMeta *vmeta = NULL;
  GstMemory *in_mem = NULL;
  GstMapFlags map_flags;
  guint stride;
  gboolean use_inpool = FALSE;

  use_inpool = vvas_xfilter_validate_inbuf (self, inbuf, &stride);
  GST_LOG_OBJECT (self, "use inpool = %d and stride = %d", use_inpool, stride);

//  vvas_frame = priv->kernel->input[0];
  if (priv->kernel->name || priv->kernel->is_softkernel) {      /*HW IP/softkernel */
    in_mem = gst_buffer_get_memory (inbuf, 0);
    if (in_mem == NULL) {
      GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
      goto error;
    }

    vmeta = gst_buffer_get_video_meta (inbuf);
    if (vmeta == NULL) {
      GST_DEBUG_OBJECT (self, "video meta not present in buffer");
    }

    GST_LOG_OBJECT (self, "getting physical address for kernel library");

    if (gst_is_vvas_memory (in_mem)
        && gst_vvas_memory_can_avoid_copy (in_mem, priv->dev_idx,
            vvas_handle->in_mem_bank)) {

      if (use_inpool == TRUE) {
        bret = vvas_xfilter_copy_input_buffer (self, inbuf, new_inbuf);
        if (!bret)
          goto error;

        gst_memory_unref (in_mem);

        in_mem = gst_buffer_get_memory (*new_inbuf, 0);
        if (in_mem == NULL) {
          GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
          goto error;
        }

        phy_addr = gst_vvas_allocator_get_paddr (in_mem);
        if (!phy_addr) {
          GST_ERROR_OBJECT (self, "failed to get physical address");
          goto error;
        }
        bo_handle = gst_vvas_allocator_get_bo (in_mem);
        inbuf = *new_inbuf;
        /* syncs data when VVAS_SYNC_TO_DEVICE flag is enabled */
        bret = gst_vvas_memory_sync_bo (in_mem);
        if (!bret)
          goto error;

      } else {
        phy_addr = gst_vvas_allocator_get_paddr (in_mem);
        bo_handle = gst_vvas_allocator_get_bo (in_mem);
      }
    } else if (gst_is_dmabuf_memory (in_mem)) {
      gint dma_fd = -1;

      dma_fd = gst_dmabuf_memory_get_fd (in_mem);
      if (dma_fd < 0) {
        GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
        goto error;
      }

      /* dmabuf but not from vvas allocator */
      bo_handle = vvas_xrt_import_bo (priv->dev_handle, dma_fd);
      if (bo_handle == NULL) {
        GST_WARNING_OBJECT (self,
            "failed to get XRT BO...fall back to copy input");
      }
      /* Lets free the bo_handle after sub bo creation */
      free_bo = TRUE;

      GST_DEBUG_OBJECT (self, "received dma fd %d and its xrt BO = %p", dma_fd,
          bo_handle);

      phy_addr = vvas_xrt_get_bo_phy_addres (bo_handle);
    }

    if (!phy_addr || use_inpool == TRUE) {
      GST_DEBUG_OBJECT (self,
          "could not get phy_addr, copy input buffer to internal pool buffer");
      bret = vvas_xfilter_copy_input_buffer (self, inbuf, new_inbuf);
      if (!bret)
        goto error;

      gst_memory_unref (in_mem);

      in_mem = gst_buffer_get_memory (*new_inbuf, 0);
      if (in_mem == NULL) {
        GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
        goto error;
      }

      phy_addr = gst_vvas_allocator_get_paddr (in_mem);
      if (!phy_addr) {
        GST_ERROR_OBJECT (self, "failed to get physical address");
        goto error;
      }
      bo_handle = gst_vvas_allocator_get_bo (in_mem);
      inbuf = *new_inbuf;
    }
    /* syncs data when VVAS_SYNC_TO_DEVICE flag is enabled */
    bret = gst_vvas_memory_sync_bo (in_mem);
    if (!bret)
      goto error;

    GST_LOG_OBJECT (self, "input paddr %p", (void *) phy_addr);

    gst_memory_unref (in_mem);
    in_mem = NULL;
  } else {                      /* Soft IP */

    if (use_inpool) {
      bret = vvas_xfilter_copy_input_buffer (self, inbuf, new_inbuf);
      if (!bret) {
        GST_ERROR_OBJECT (self, "failed to copy input buffer");
        goto error;
        inbuf = *new_inbuf;
      }

      in_mem = gst_buffer_get_memory (*new_inbuf, 0);
      if (in_mem == NULL) {
        GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
        goto error;
      }

      inbuf = *new_inbuf;
      /* syncs data when VVAS_SYNC_TO_DEVICE flag is enabled */
      bret = gst_vvas_memory_sync_bo (in_mem);
      if (!bret)
        goto error;

    }

    map_flags = GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF;

    if (priv->element_mode == VVAS_ELEMENT_MODE_IN_PLACE)
      map_flags = map_flags | GST_MAP_WRITE;

    memset (in_vframe, 0x0, sizeof (GstVideoFrame));
    if (!gst_video_frame_map (in_vframe, self->priv->in_vinfo,
            inbuf, map_flags)) {
      GST_ERROR_OBJECT (self, "failed to map input buffer");
      goto error;
    }

    for (plane_id = 0;
        plane_id < GST_VIDEO_INFO_N_PLANES (self->priv->in_vinfo); plane_id++) {
      vvas_frame->vaddr[plane_id] =
          GST_VIDEO_FRAME_PLANE_DATA (in_vframe, plane_id);
      GST_LOG_OBJECT (self, "inbuf plane[%d] : vaddr = %p", plane_id,
          vvas_frame->vaddr[plane_id]);
    }
  }

  vvas_frame->props.width = GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo);
  vvas_frame->props.height = GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo);
  vvas_frame->props.fmt =
      get_kernellib_format (GST_VIDEO_INFO_FORMAT (self->priv->in_vinfo));
  vvas_frame->n_planes = GST_VIDEO_INFO_N_PLANES (self->priv->in_vinfo);
  vvas_frame->app_priv = inbuf;
  vvas_frame->props.stride = stride;

  if (phy_addr) {
    for (plane_id = 0;
        plane_id < GST_VIDEO_INFO_N_PLANES (self->priv->in_vinfo); plane_id++) {
      gsize offset;
      size_t plane_size;
      guint plane_height;
      gint comp[GST_VIDEO_MAX_COMPONENTS];

      if (use_inpool) {
        /* TODO offset set as per new stride and size */
        offset = GST_VIDEO_INFO_PLANE_OFFSET (self->priv->in_vinfo, plane_id);
      } else if (vmeta) {
        offset = vmeta->offset[plane_id];
      } else {
        offset = GST_VIDEO_INFO_PLANE_OFFSET (self->priv->in_vinfo, plane_id);
      }
      vvas_frame->paddr[plane_id] = phy_addr + offset;

      /* Convert plane index to component index */
      gst_video_format_info_component (self->priv->in_vinfo->finfo,
          plane_id, comp);
      plane_height =
          GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (self->priv->in_vinfo->finfo,
          comp[0], GST_VIDEO_INFO_FIELD_HEIGHT (self->priv->in_vinfo));
      plane_size =
          plane_height * GST_VIDEO_INFO_PLANE_STRIDE (self->priv->in_vinfo,
          plane_id);
      vvas_frame->bo[plane_id] =
          vvas_xrt_create_sub_bo (bo_handle, plane_size, offset);

      GST_LOG_OBJECT (self,
          "inbuf plane[%d] : paddr = %p, offset = %lu, stride = %d", plane_id,
          (void *) vvas_frame->paddr[plane_id], offset,
          vvas_frame->props.stride);
    }
  }

  if (free_bo && bo_handle) {
    vvas_xrt_free_bo (bo_handle);
  }

  GST_LOG_OBJECT (self, "successfully prepared input vvas frame");
  return TRUE;

error:
  if (in_mem)
    gst_memory_unref (in_mem);

  return FALSE;
}

static gboolean
vvas_xfilter_prepare_output_frame (GstVvas_XFilter * self, GstBuffer * outbuf,
    VVASFrame * vvas_frame, GstVideoFrame * out_vframe)
{
  GstVvas_XFilterPrivate *priv = self->priv;
  GstMemory *out_mem = NULL;
  guint64 phy_addr = -1;
  vvasBOHandle bo_handle = NULL;
  gboolean free_bo = FALSE;
  guint plane_id;
  GstVideoMeta *vmeta = NULL;
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, };

  Vvas_XFilter *kernel = self->priv->kernel;
  VVASKernel *vvas_handle = kernel->vvas_handle;
  guint kernel_stride;

  kernel_stride =
      vvas_xfilter_cal_stride (GST_VIDEO_INFO_WIDTH (self->priv->out_vinfo),
      GST_VIDEO_INFO_FORMAT (self->priv->out_vinfo),
      vvas_caps_get_sink_stride_align (vvas_handle));

  out_mem = gst_buffer_get_memory (outbuf, 0);
  if (out_mem == NULL) {
    GST_ERROR_OBJECT (self, "failed to get memory from output buffer");
    goto error;
  }

  if (gst_is_vvas_memory (out_mem)) {
    phy_addr = gst_vvas_allocator_get_paddr (out_mem);
    bo_handle = gst_vvas_allocator_get_bo (out_mem);
  } else if (gst_is_dmabuf_memory (out_mem)) {
    gint dma_fd = -1;

    dma_fd = gst_dmabuf_memory_get_fd (out_mem);
    if (dma_fd < 0) {
      GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
      goto error;
    }

    /* dmabuf but not from xrt */
    bo_handle = vvas_xrt_import_bo (self->priv->dev_handle, dma_fd);
    if (bo_handle == NULL) {
      GST_WARNING_OBJECT (self,
          "failed to get XRT BO...fall back to copy input");
    }
    /* Lets free the bo_handle after sub bo creation */
    free_bo = TRUE;

    GST_INFO_OBJECT (self, "received dma fd %d and its xrt BO = %p", dma_fd,
        bo_handle);

    phy_addr = vvas_xrt_get_bo_phy_addres (bo_handle);
  } else {
    GST_ERROR_OBJECT (self, "Unsupported mem");
    goto error;
  }

  vmeta = gst_buffer_get_video_meta (outbuf);
  if (vmeta) {
    for (plane_id = 0;
        plane_id < GST_VIDEO_INFO_N_PLANES (self->priv->out_vinfo);
        plane_id++) {
      offset[plane_id] = offset[plane_id] + vmeta->offset[plane_id];
    }
  } else {
    GST_ERROR_OBJECT (self, "video meta not present in output buffer");
    for (plane_id = 0;
        plane_id < GST_VIDEO_INFO_N_PLANES (self->priv->internal_out_vinfo);
        plane_id++) {
      offset[plane_id] = offset[plane_id] +
          GST_VIDEO_INFO_PLANE_OFFSET (self->priv->internal_out_vinfo,
          plane_id);
    }
  }

  //vvas_frame = priv->kernel->output[0];
  vvas_frame->props.width = GST_VIDEO_INFO_WIDTH (self->priv->out_vinfo);
  vvas_frame->props.height = GST_VIDEO_INFO_HEIGHT (self->priv->out_vinfo);
  if (kernel_stride > 0)
    vvas_frame->props.stride = kernel_stride;
  else if (vmeta)
    vvas_frame->props.stride = vmeta->stride[0];
  else
    vvas_frame->props.stride =
        GST_VIDEO_INFO_PLANE_STRIDE (self->priv->out_vinfo, 0);;

  vvas_frame->props.fmt =
      get_kernellib_format (GST_VIDEO_INFO_FORMAT (self->priv->out_vinfo));
  vvas_frame->n_planes = GST_VIDEO_INFO_N_PLANES (self->priv->out_vinfo);
  vvas_frame->app_priv = outbuf;

  if (!(priv->kernel->name || priv->kernel->is_softkernel)) {
    memset (out_vframe, 0x0, sizeof (GstVideoFrame));
    /* software lib mode */
    if (!gst_video_frame_map (out_vframe, self->priv->out_vinfo,
            outbuf, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "failed to map output buffer");
      goto error;
    }

    for (plane_id = 0;
        plane_id < GST_VIDEO_INFO_N_PLANES (self->priv->out_vinfo);
        plane_id++) {
      vvas_frame->vaddr[plane_id] =
          GST_VIDEO_FRAME_PLANE_DATA (out_vframe, plane_id);
      GST_LOG_OBJECT (self, "outbuf plane[%d] : vaddr = %p", plane_id,
          vvas_frame->vaddr[plane_id]);
    }
  }

  for (plane_id = 0;
      plane_id < GST_VIDEO_INFO_N_PLANES (self->priv->out_vinfo); plane_id++) {
    size_t plane_size;
    guint plane_height;
    gint comp[GST_VIDEO_MAX_COMPONENTS];

    vvas_frame->paddr[plane_id] = phy_addr + offset[plane_id];

    GST_LOG_OBJECT (self,
        "outbuf plane[%d] : paddr = %p, offset = %lu, stride = %d", plane_id,
        (void *) vvas_frame->paddr[plane_id], offset[plane_id],
        vvas_frame->props.stride);

    /* Convert plane index to component index */
    gst_video_format_info_component (self->priv->out_vinfo->finfo,
        plane_id, comp);
    plane_height =
        GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (self->priv->out_vinfo->finfo,
        comp[0], GST_VIDEO_INFO_FIELD_HEIGHT (self->priv->out_vinfo));
    plane_size =
        plane_height * GST_VIDEO_INFO_PLANE_STRIDE (self->priv->out_vinfo,
        plane_id);
    vvas_frame->bo[plane_id] =
        vvas_xrt_create_sub_bo (bo_handle, plane_size, offset[plane_id]);

  }

  if (free_bo && bo_handle) {
    vvas_xrt_free_bo (bo_handle);
  }

  gst_memory_unref (out_mem);

  GST_LOG_OBJECT (self, "successfully prepared output vvas frame");
  return TRUE;

error:
  if (out_mem)
    gst_memory_unref (out_mem);

  return FALSE;
}

static GstFlowReturn
gst_vvas_xfilter_submit_input_buffer (GstBaseTransform * trans,
    gboolean is_discont, GstBuffer * inbuf)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (trans);

  GST_LOG_OBJECT (self, "received %" GST_PTR_FORMAT, inbuf);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->submit_input_buffer (trans,
      is_discont, inbuf);
}

static GstFlowReturn
gst_vvas_xfilter_generate_output (GstBaseTransform * trans, GstBuffer ** outbuf)
{
  GstVvas_XFilter *self = GST_VVAS_XFILTER (trans);
  GstVvas_XFilterPrivate *priv = self->priv;
  GstFlowReturn fret = GST_FLOW_OK;
  Vvas_XFilter *kernel = priv->kernel;
  GstBuffer *new_inbuf = NULL;
  int ret;
  gboolean bret = FALSE;
  GstBuffer *inbuf = NULL;
  GstBuffer *cur_outbuf = NULL;
  guint plane_id = 0;

  *outbuf = NULL;

  inbuf = trans->queued_buf;
  trans->queued_buf = NULL;

  /* This default processing method needs one input buffer to feed to
   * the transform functions, we can't do anything without it */
  if (inbuf == NULL)
    return GST_FLOW_OK;

  fret =
      GST_BASE_TRANSFORM_CLASS (parent_class)->prepare_output_buffer (trans,
      inbuf, &cur_outbuf);
  if (fret != GST_FLOW_OK)
    goto exit;

  if (gst_base_transform_is_in_place (trans)) {
    if (inbuf != cur_outbuf) {
      /* prepare_output_buffer() has returned new output buffer,
       * this is because inbuf was not writable. */
      gst_buffer_unref (inbuf);
      inbuf = cur_outbuf;
    }
  }

  bret =
      vvas_xfilter_prepare_input_frame (self, inbuf, &new_inbuf,
      kernel->input[0], &kernel->in_vframe);
  if (!bret) {
    fret = GST_FLOW_ERROR;
    goto exit;
  }

  if (new_inbuf && inbuf != new_inbuf) {
    gst_buffer_unref (inbuf);
    inbuf = new_inbuf;
  }

  if (priv->element_mode == VVAS_ELEMENT_MODE_TRANSFORM) {
    bret =
        vvas_xfilter_prepare_output_frame (self, cur_outbuf, kernel->output[0],
        &kernel->out_vframe);
    if (!bret) {
      fret = GST_FLOW_ERROR;
      goto exit;
    }
    {
      GstMemory *outmem = NULL;
      outmem = gst_buffer_get_memory (cur_outbuf, 0);
      if (outmem == NULL) {
        GST_ERROR_OBJECT (self, "failed to get memory from output buffer");
        fret = GST_FLOW_ERROR;
        goto exit;
      }
      if (!(self->priv->kernel->name || self->priv->kernel->is_softkernel)) {
        gst_vvas_memory_set_sync_flag (outmem, VVAS_SYNC_TO_DEVICE);
        bret = gst_vvas_memory_sync_bo (outmem);
        if (!bret)
          goto exit;
      } else
        gst_vvas_memory_set_sync_flag (outmem, VVAS_SYNC_FROM_DEVICE);

      gst_memory_unref (outmem);
    }
  }

  /* update dynamic json config to kernel */
  kernel->vvas_handle->kernel_dyn_config = priv->dyn_json_config;

  ret = kernel->kernel_start_func (kernel->vvas_handle, 0, kernel->input,
      kernel->output);
  if (ret < 0) {
    GST_ERROR_OBJECT (self, "kernel start failed");
    fret = GST_FLOW_ERROR;
    goto exit;
  }

  ret = kernel->kernel_done_func (kernel->vvas_handle);
  if (ret < 0) {
    GST_ERROR_OBJECT (self, "kernel done failed");
    fret = GST_FLOW_ERROR;
    goto exit;
  }
#ifdef XLNX_PCIe_PLATFORM
  /* If Hard IP/Soft kernel is accessing the buffer in place, then
     sync the buffer from device as the buffer has been modified by the IP */
  if ((priv->element_mode == VVAS_ELEMENT_MODE_IN_PLACE) &&
      ((self->priv->kernel->name || self->priv->kernel->is_softkernel))) {
    GstMemory *outmem = NULL;
    outmem = gst_buffer_get_memory (cur_outbuf, 0);

    /* when plugins/app request to map this memory, sync will occur */
    gst_vvas_memory_set_sync_flag (outmem, VVAS_SYNC_FROM_DEVICE);
    gst_memory_unref (outmem);
  }
#endif

  g_signal_emit (self, vvas_signals[SIGNAL_VVAS], 0);

  if (!(priv->kernel->name || priv->kernel->is_softkernel)) {
    if (kernel->in_vframe.data[0]) {
      gst_video_frame_unmap (&kernel->in_vframe);
    }

    if (priv->element_mode == VVAS_ELEMENT_MODE_TRANSFORM &&
        kernel->out_vframe.data[0]) {
      gst_video_frame_unmap (&kernel->out_vframe);
    }
  }

  for (plane_id = 0; plane_id < priv->kernel->input[0]->n_planes; plane_id++) {
    vvas_xrt_free_bo (priv->kernel->input[0]->bo[plane_id]);
  }

  if (priv->element_mode == VVAS_ELEMENT_MODE_TRANSFORM) {
    for (plane_id = 0; plane_id < priv->kernel->output[0]->n_planes; plane_id++) {
      vvas_xrt_free_bo (priv->kernel->output[0]->bo[plane_id]);
    }
  }

  if (priv->need_copy) {
    GstBuffer *new_outbuf;
    GstVideoFrame new_frame, out_frame;
    if ((self->priv->kernel->name || self->priv->kernel->is_softkernel)) {
      GstMemory *outmem;
      outmem = gst_buffer_get_memory (cur_outbuf, 0);
      /* when plugins/app request to map this memory, sync will occur */
      gst_vvas_memory_set_sync_flag (outmem, VVAS_SYNC_FROM_DEVICE);
      gst_memory_unref (outmem);
    }

    new_outbuf =
        gst_buffer_new_and_alloc (GST_VIDEO_INFO_SIZE (priv->out_vinfo));
    if (!new_outbuf) {
      GST_ERROR_OBJECT (self, "failed to allocate output buffer");
      return GST_FLOW_ERROR;
    }

    gst_video_frame_map (&out_frame, self->priv->internal_out_vinfo, cur_outbuf,
        GST_MAP_READ);
    gst_video_frame_map (&new_frame, priv->out_vinfo, new_outbuf,
        GST_MAP_WRITE);
    GST_LOG_OBJECT (self, "slow copy data to output");
    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, self,
        "slow copy data from %p to %p", cur_outbuf, new_outbuf);
    gst_video_frame_copy (&new_frame, &out_frame);
    gst_video_frame_unmap (&out_frame);
    gst_video_frame_unmap (&new_frame);

    gst_buffer_copy_into (new_outbuf, cur_outbuf, GST_BUFFER_COPY_FLAGS, 0, -1);
//      gst_buffer_unref (*outbuf);

    *outbuf = new_outbuf;
  } else
    *outbuf = cur_outbuf;
  if (priv->element_mode == VVAS_ELEMENT_MODE_TRANSFORM)
    gst_buffer_unref (inbuf);
  GST_LOG_OBJECT (self, "pushing output %" GST_PTR_FORMAT, *outbuf);

exit:
  return fret;
}

static GstFlowReturn
gst_vvas_xfilter_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  /*
   * As generate_output vmethod is implemented; transform_ip or transform will
   * never be called from the base class, yet these two functions are registered,
   * this is because the Base class will configure itself in inplace and transform
   * mode only when transform_ip and transform is overridden respectively.
   * Once base class is configured it takes care of few things for example doing
   * allocation_query on srcpad and deciding buffer pool and allocator is not
   * required when the mode of operation is passthrough or inplace hence base class
   * avoids it.
   */
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vvas_xfilter_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * vvas_xfilter)
{
  return gst_element_register (vvas_xfilter, "vvas_xfilter", GST_RANK_PRIMARY,
      GST_TYPE_VVAS_XFILTER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xfilter,
    "GStreamer VVAS plug-in for filters", plugin_init, VVAS_API_VERSION,
    "MIT/X11", "Xilinx VVAS SDK plugin", "http://xilinx.com/")
