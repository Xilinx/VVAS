/*
 * Copyright (C) 2020 - 2022 Xilinx, Inc.  All rights reserved.
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

/* TODO:
 * 1. XRM support
 *    a. for PPE XRM is required
 *    b. for inference we dont have control as we use Vitis AI APIs
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
#include <gst/base/base.h>
#include <gst/vvas/gstvvasallocator.h>
#include <gst/vvas/gstvvasbufferpool.h>
#include <gst/allocators/gstdmabuf.h>
#include <dlfcn.h>              /* for dlXXX APIs */
#include <sys/mman.h>           /* for munmap */
#include <jansson.h>
#include <math.h>

#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include <vvas/vvas_kernel.h>
#include "gstvvas_xinfer.h"
#include <gst/vvas/gstvvasutils.h>
#include <gst/vvas/gstinferencemeta.h>

GST_DEBUG_CATEGORY_STATIC (gst_vvas_xinfer_debug);
#define GST_CAT_DEFAULT gst_vvas_xinfer_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

static const int ERT_CMD_SIZE = 4096;
#define CMD_EXEC_TIMEOUT 1000   // 1 sec
#define MIN_POOL_BUFFERS 2
#define PRINT_METADATA_TREE     /* prints inference metadat in GST_ERROR */

#undef DUMP_INFER_INPUT
//#define DUMP_INFER_INPUT
#define DEFAULT_INFER_LEVEL 1
#define BATCH_SIZE_ZERO 0

// TODO: On embedded also installation path also should go to /opt area
#ifdef XLNX_PCIe_PLATFORM
#define DEFAULT_VVAS_LIB_PATH "/opt/xilinx/vvas/lib/"
#define DEFAULT_DEVICE_INDEX -1
#else
#define DEFAULT_VVAS_LIB_PATH "/usr/lib/"
#define DEFAULT_DEVICE_INDEX 0
#endif
#define MAX_PRIV_POOLS 10
#define ALIGN(size,align) (((size) + (align) - 1) & ~((align) - 1))

#include "vvas/xrt_utils.h"

GQuark _scale_quark;
GQuark _copy_quark;

typedef struct _GstVvas_XInferPrivate GstVvas_XInferPrivate;
typedef struct _Vvas_XInferFrame Vvas_XInferFrame;

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
  json_t *lib_config;
  json_t *root_config;
  gchar *vvas_lib_path;
  void *lib_fd;
  gint cu_idx;
  VVASKernelInit kernel_init_func;
  VVASKernelStartFunc kernel_start_func;
  VVASKernelDoneFunc kernel_done_func;
  VVASKernelDeInit kernel_deinit_func;
  VVASKernel *vvas_handle;
  VVASFrame *input[MAX_NUM_OBJECT];
  VVASFrame *output[MAX_NUM_OBJECT];
} Vvas_XInfer;

enum
{
  PROP_0,
  PROP_PPE_CONFIG_LOCATION,
  PROP_INFER_CONFIG_LOCATION,
  PROP_DYNAMIC_CONFIG,
};

typedef enum
{
  VVAS_THREAD_NOT_CREATED,
  VVAS_THREAD_RUNNING,
  VVAS_THREAD_EXITED,
} VvasThreadState;

typedef struct _vvas_xinfer_nodeinfo
{
  GstVvas_XInfer *self;
  GstVideoInfo *parent_vinfo;
  GstVideoInfo *child_vinfo;
} Vvas_XInferNodeInfo;

typedef struct _vvas_xinfer_numsubs
{
  GstVvas_XInfer *self;
  gboolean available;
} Vvas_XInferNumSubs;

struct _Vvas_XInferFrame
{
  GstBuffer *parent_buf;
  GstVideoInfo *parent_vinfo;
  gboolean last_parent_buf;     /* useful to avoid pushing duplicate parent buffers in inference_level > 1 */
  GstBuffer *child_buf;
  GstVideoInfo *child_vinfo;
  GstVideoFrame *in_vframe;
  VVASFrame *vvas_frame;
  gboolean skip_processing;
  GstEvent *event;
};

struct _GstVvas_XInferPrivate
{
  /*common members */
  gboolean do_init;
  GstVideoInfo *in_vinfo;
  GstBufferPool *input_pool;
  json_t *dyn_json_config;
  gboolean do_preprocess;
  gboolean stop;
  GstFlowReturn last_fret;
  gboolean is_eos;
  gboolean is_error;

  /* preprocessing members */
  gint ppe_dev_idx;
  vvasDeviceHandle ppe_dev_handle;
  Vvas_XInfer *ppe_kernel;
  uuid_t ppe_xclbinId;
  GMutex ppe_lock;
  GCond ppe_has_input;
  GCond ppe_need_input;
  GThread *ppe_thread;
  gchar *ppe_xclbin_loc;
  Vvas_XInferFrame *ppe_frame;
  gboolean ppe_need_data;
  GstVideoInfo *ppe_out_vinfo;
  GstBufferPool *ppe_outpool;
  guint nframes_in_level;       /* number of bbox/sub_buffer at inference level */
  VvasThreadState ppe_thread_state;

  /* inference members */
  Vvas_XInfer *infer_kernel;
  gint infer_level;
  GMutex infer_lock;
  GCond infer_cond;
  GCond infer_batch_full;
  GThread *infer_thread;
  guint infer_batch_size;
  guint max_infer_queue;
  GQueue *infer_batch_queue;
  gboolean low_latency_infer;
  gint pref_infer_width;
  gint pref_infer_height;
  GstVideoFormat pref_infer_format;
  gboolean bbox_preprocess;
  GQueue *infer_sub_buffers;
  gboolean infer_attach_ppebuf; /* decides whether sub_buffers need to be attached with metadata or not */
  VvasThreadState infer_thread_state;
#ifdef DUMP_INFER_INPUT
  FILE *fp;
#endif
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

#define gst_vvas_xinfer_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_XInfer, gst_vvas_xinfer,
    GST_TYPE_BASE_TRANSFORM);
#define GST_VVAS_XINFER_PRIVATE(self) (GstVvas_XInferPrivate *) (gst_vvas_xinfer_get_instance_private (self))

static gboolean
vvas_xinfer_prepare_ppe_output_frame (GstVvas_XInfer * self, GstBuffer * outbuf,
    GstVideoInfo * out_vinfo, VVASFrame * vvas_frame);
static void gst_vvas_xinfer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vvas_xinfer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_vvas_xinfer_finalize (GObject * obj);

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

static gboolean
find_kernel_lib_symbols (GstVvas_XInfer * self, Vvas_XInfer * kernel)
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
        "could not find vvas_xinfer_init function. reason : %s", dlerror ());
    return FALSE;
  }

  kernel->kernel_start_func = (VVASKernelStartFunc) dlsym (kernel->lib_fd,
      "xlnx_kernel_start");
  if (kernel->kernel_start_func == NULL) {
    GST_ERROR_OBJECT (self,
        "could not find vvas_xinfer_start function. reason : %s", dlerror ());
    return FALSE;
  }

  kernel->kernel_done_func = (VVASKernelDoneFunc) dlsym (kernel->lib_fd,
      "xlnx_kernel_done");
  if (kernel->kernel_done_func == NULL) {
    GST_ERROR_OBJECT (self,
        "could not find vvas_xinfer_done function. reason : %s", dlerror ());
    return FALSE;
  }

  kernel->kernel_deinit_func = (VVASKernelDeInit) dlsym (kernel->lib_fd,
      "xlnx_kernel_deinit");
  if (kernel->kernel_deinit_func == NULL) {
    GST_ERROR_OBJECT (self,
        "could not find vvas_xinfer_deinit function. reason : %s", dlerror ());
    return FALSE;
  }
  return TRUE;
}

static gboolean
check_bbox_buffers_availability (GNode * node, gpointer data)
{
  Vvas_XInferNumSubs *pNumSubs = (Vvas_XInferNumSubs *) data;
  GstVvas_XInfer *self = pNumSubs->self;
  GstInferencePrediction *prediction = NULL;

  if (g_node_depth (node) != self->priv->infer_level) {
    GST_LOG_OBJECT (self, "ignoring node %p at level %d", node,
        g_node_depth (node));
    return FALSE;
  }
  prediction = (GstInferencePrediction *) node->data;
  if (prediction->sub_buffer) {
    GstVideoMeta *vmeta;

    GST_LOG_OBJECT (self, "bbox buffer availble for node %p", node);
    vmeta = gst_buffer_get_video_meta (prediction->sub_buffer);
    if (vmeta) {
      GST_LOG_OBJECT (self, "bbox width = %d, height = %d and format = %s",
          vmeta->width, vmeta->height,
          gst_video_format_to_string (vmeta->format));
      GST_LOG_OBJECT (self,
          "infer preffed width = %d, height = %d and format = %s",
          self->priv->pref_infer_width, self->priv->pref_infer_height,
          gst_video_format_to_string (self->priv->pref_infer_format));
    }
    if (vmeta && (vmeta->width == self->priv->pref_infer_width) &&
        vmeta->height == self->priv->pref_infer_height &&
        vmeta->format == self->priv->pref_infer_format) {
      pNumSubs->available = TRUE;
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean
prepare_inference_sub_buffers (GNode * node, gpointer data)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (data);
  GstInferencePrediction *prediction = NULL;
  GstVvas_XInferPrivate *priv = self->priv;

  if (g_node_depth (node) != self->priv->infer_level) {
    GST_LOG_OBJECT (self, "ignoring node %p at level %d", node,
        g_node_depth (node));
    return FALSE;
  }

  prediction = (GstInferencePrediction *) node->data;
  if (!prediction->sub_buffer) {
    GST_LOG_OBJECT (self, "bbox buffer not availble for node %p", node);
  } else {
    GstVideoMeta *vmeta;

    vmeta = gst_buffer_get_video_meta (prediction->sub_buffer);
    if (vmeta) {
      GST_LOG_OBJECT (self, "bbox width = %d, height = %d and format = %s",
          vmeta->width, vmeta->height,
          gst_video_format_to_string (vmeta->format));
      GST_LOG_OBJECT (self,
          "infer preffed width = %d, height = %d and format = %s",
          priv->pref_infer_width, priv->pref_infer_height,
          gst_video_format_to_string (priv->pref_infer_format));
    }
    if (vmeta && (vmeta->width == priv->pref_infer_width) &&
        vmeta->height == priv->pref_infer_height &&
        vmeta->format == priv->pref_infer_format) {
      GstInferenceMeta *sub_meta;
      GstInferencePrediction *parent_prediction =
          (GstInferencePrediction *) node->parent->data;

      GST_DEBUG_OBJECT (self, "queueing subbuffer %p", prediction->sub_buffer);
      g_queue_push_tail (self->priv->infer_sub_buffers, prediction->sub_buffer);

      sub_meta =
          ((GstInferenceMeta *) gst_buffer_get_meta (prediction->sub_buffer,
              gst_inference_meta_api_get_type ()));
      if (!sub_meta) {
        GST_LOG_OBJECT (self, "add inference metadata to %p",
            prediction->sub_buffer);
        sub_meta =
            (GstInferenceMeta *) gst_buffer_add_meta (prediction->sub_buffer,
            gst_inference_meta_get_info (), NULL);
      }

      gst_inference_prediction_unref (sub_meta->prediction);
      gst_inference_prediction_ref (parent_prediction);
      sub_meta->prediction = prediction;
    }
  }

  return FALSE;
}

#ifdef PRINT_METADATA_TREE
static gboolean
printf_all_nodes (GNode * node, gpointer data)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (data);

  GST_LOG_OBJECT (self, "node = %p at level %d", node, g_node_depth (node));
  return FALSE;
}
#endif

static gboolean
prepare_ppe_outbuf_at_level (GNode * node, gpointer data)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (data);
  GstVvas_XInferPrivate *priv = self->priv;

  GST_LOG_OBJECT (self, "node = %p at level %d", node, g_node_depth (node));

  if (g_node_depth (node) == priv->infer_level) {
    GstBuffer *outbuf;
    GstFlowReturn fret;
    VVASFrame *out_vvas_frame;
    GstInferencePrediction *prediction = (GstInferencePrediction *) node->data;
    GstInferencePrediction *parent_prediction =
        (GstInferencePrediction *) node->parent->data;
    GstInferenceMeta *infer_meta;
    gboolean bret;

    GST_LOG_OBJECT (self, "found node %p at level inference level %d", node,
        priv->infer_level);

    /* acquire ppe output buffer */
    fret = gst_buffer_pool_acquire_buffer (priv->ppe_outpool, &outbuf, NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
          priv->ppe_outpool);
      priv->is_error = TRUE;
      return TRUE;
    }

    out_vvas_frame = g_slice_new0 (VVASFrame);

    bret =
        vvas_xinfer_prepare_ppe_output_frame (self, outbuf, priv->ppe_out_vinfo,
        out_vvas_frame);
    if (!bret) {
      priv->is_error = TRUE;
      return TRUE;
    }

    infer_meta =
        ((GstInferenceMeta *) gst_buffer_get_meta (outbuf,
            gst_inference_meta_api_get_type ()));
    if (!infer_meta) {
      GST_LOG_OBJECT (self, "add inference metadata to %p", outbuf);
      infer_meta =
          (GstInferenceMeta *) gst_buffer_add_meta (outbuf,
          gst_inference_meta_get_info (), NULL);
    }

    gst_inference_prediction_unref (infer_meta->prediction);
    gst_inference_prediction_ref (parent_prediction);
    infer_meta->prediction = prediction;

    if (priv->infer_attach_ppebuf)
      prediction->sub_buffer = gst_buffer_ref (outbuf);

    GST_DEBUG_OBJECT (self,
        "acquired PPE output buffer %p and %s as sub_buffer", outbuf,
        priv->infer_attach_ppebuf ? "attached" : "NOT attached");

    priv->ppe_kernel->output[priv->nframes_in_level] = out_vvas_frame;
    priv->nframes_in_level++;
  }

  return FALSE;
}

static gboolean
vvas_xinfer_is_sub_buffer_useful (GstVvas_XInfer * self, GstBuffer * buf)
{
  GstVideoMeta *vmeta = NULL;

  if (!buf)
    return FALSE;

  vmeta = gst_buffer_get_video_meta (buf);
  if (vmeta && (vmeta->width == self->priv->pref_infer_width) &&
      vmeta->height == self->priv->pref_infer_height &&
      vmeta->format == self->priv->pref_infer_format) {
    GST_LOG_OBJECT (self,
        "buffer parameters matched with inference requirement");
    return TRUE;
  }
  return FALSE;
}

static gboolean
vvas_xinfer_allocate_sink_internal_pool (GstVvas_XInfer * self)
{
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstCaps *caps = NULL;
  VVASKernel *vvas_handle = self->priv->ppe_kernel->vvas_handle;

  caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM (self)->sinkpad);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }
  // TODO: get stride requirements from PPE
  pool = gst_vvas_buffer_pool_new (1, 1);
  GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

  allocator = gst_vvas_allocator_new (self->priv->ppe_dev_idx,
		                      USE_DMABUF, vvas_handle->in_mem_bank);
  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;

  config = gst_buffer_pool_get_config (pool);
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

  GST_INFO_OBJECT (self, "allocated %" GST_PTR_FORMAT " pool", pool);
  gst_caps_unref (caps);
  if (allocator)
    gst_object_unref (allocator);

  return TRUE;

error:
  gst_caps_unref (caps);
  return FALSE;
}

static gboolean
vvas_xinfer_copy_input_buffer (GstVvas_XInfer * self, GstBuffer * inbuf,
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
    bret = vvas_xinfer_allocate_sink_internal_pool (self);
    if (!bret)
      goto error;

    if (!gst_buffer_pool_is_active (self->priv->input_pool)) {
      gst_buffer_pool_set_active (self->priv->input_pool, TRUE);
    }
  }

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
  if (!gst_video_frame_map (&new_vframe, self->priv->in_vinfo, new_inbuf,
          GST_MAP_WRITE)) {
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
      "slow copy to internal input pool buffer");

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
vvas_xinfer_ppe_init (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  int iret;
  struct ert_start_kernel_cmd *ert_cmd = NULL;
  VVASKernel *vvas_handle = priv->ppe_kernel->vvas_handle;

  if (vvas_xrt_download_xclbin (self->priv->ppe_xclbin_loc, priv->ppe_dev_idx,
                       NULL, priv->ppe_dev_handle, &priv->ppe_xclbinId))
    return FALSE;

  if (priv->ppe_kernel->name) {
    priv->ppe_kernel->cu_idx =
        vvas_xrt_ip_name2_index (priv->ppe_dev_handle, priv->ppe_kernel->name);
    if (priv->ppe_kernel->cu_idx < 0) {
      GST_ERROR_OBJECT (self, "failed to get cu index for IP name %s",
          priv->ppe_kernel->name);
      return FALSE;
    }

    GST_INFO_OBJECT (self, "cu_idx for kernel %s is %d", priv->ppe_kernel->name,
        priv->ppe_kernel->cu_idx);
  }

  if (priv->ppe_kernel->cu_idx >= 0) {
    if (vvas_xrt_open_context (priv->ppe_dev_handle, priv->ppe_xclbinId,
            priv->ppe_kernel->cu_idx, true)) {
      GST_ERROR_OBJECT (self, "failed to open XRT context ...");
      return FALSE;
    }
  }

  /* allocate ert command buffer */
  iret =
      vvas_xrt_alloc_xrt_buffer (priv->ppe_dev_handle, ERT_CMD_SIZE,
      VVAS_BO_SHARED_VIRTUAL, VVAS_BO_FLAGS_EXECBUF | DEFAULT_MEM_BANK,
      vvas_handle->ert_cmd_buf);
  if (iret < 0) {
    GST_ERROR_OBJECT (self, "failed to allocate ert command buffer..");
    return FALSE;
  }

  ert_cmd =
      (struct ert_start_kernel_cmd *) (vvas_handle->ert_cmd_buf->user_ptr);
  memset (ert_cmd, 0x0, ERT_CMD_SIZE);

  /* Populate the kernel name */
  vvas_handle->name =
      (uint8_t *)g_strdup_printf("libkrnl_%s", GST_ELEMENT_NAME (self));

  vvas_handle->dev_handle = priv->ppe_dev_handle;
#if defined(XLNX_PCIe_PLATFORM)
  vvas_handle->is_softkernel = FALSE;
#endif
  vvas_handle->cu_idx = priv->ppe_kernel->cu_idx;
  vvas_handle->kernel_config = priv->ppe_kernel->lib_config;

  GST_INFO_OBJECT (self, "preprocess cu_idx = %d", vvas_handle->cu_idx);

  iret = priv->ppe_kernel->kernel_init_func (vvas_handle);
  if (iret < 0) {
    GST_ERROR_OBJECT (self, "failed to do preprocess kernel init..");
    return FALSE;
  }
  GST_INFO_OBJECT (self, "completed preprocess kernel init");

  priv->ppe_frame = g_slice_new0 (Vvas_XInferFrame);
  priv->ppe_need_data = TRUE;
  priv->ppe_out_vinfo = gst_video_info_new ();

  g_mutex_init (&priv->ppe_lock);
  g_cond_init (&priv->ppe_has_input);
  g_cond_init (&priv->ppe_need_input);

  return TRUE;
}

static gboolean
vvas_xinfer_infer_init (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  int iret;
  VVASKernel *vvas_handle = priv->infer_kernel->vvas_handle;

  vvas_handle->kernel_config = priv->infer_kernel->lib_config;

  iret = priv->infer_kernel->kernel_init_func (vvas_handle);
  if (iret < 0) {
    GST_ERROR_OBJECT (self, "failed to do inference kernel init..");
    return FALSE;
  }
  GST_INFO_OBJECT (self, "completed inference kernel init");

  g_mutex_init (&priv->infer_lock);
  g_cond_init (&priv->infer_cond);
  g_cond_init (&priv->infer_batch_full);

  priv->infer_sub_buffers = g_queue_new ();
  if (priv->infer_batch_size == BATCH_SIZE_ZERO ||
      priv->infer_batch_size > vvas_kernel_get_batch_size(vvas_handle)) {
      GST_WARNING_OBJECT (self, "infer_batch_size (%d) can't be zero"
          "or greater than model supported batch size."
	  "taking batch-size %ld",
          priv->infer_batch_size,
	  vvas_kernel_get_batch_size(vvas_handle));
    priv->infer_batch_size = vvas_kernel_get_batch_size(vvas_handle);
  }

  if (priv->max_infer_queue < priv->infer_batch_size) {
      GST_WARNING_OBJECT (self, "inference-max-queue can't be less than "
          "batch-size. taking batch-size %d as default queue length",
          priv->infer_batch_size);
      priv->max_infer_queue = priv->infer_batch_size;
  }
  return TRUE;
}

static gboolean
vvas_xinfer_read_ppe_config (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  json_t *root = NULL, *karray, *kernel, *value;
  json_error_t error;
  gchar *lib_path = NULL;
  guint kernel_count;

  /* get root json object */
  root = json_load_file (self->ppe_json_file, JSON_DECODE_ANY, &error);
  if (!root) {
    GST_ERROR_OBJECT (self, "failed to load json file. reason %s", error.text);
    goto error;
  }

  /* get xclbin location */
  // TODO: xclbin-loc is optional as XRM might have downloaded
  value = json_object_get (root, "xclbin-location");
  if (json_is_string (value)) {
    priv->ppe_xclbin_loc = g_strdup (json_string_value (value));
    GST_INFO_OBJECT (self, "xclbin location to download %s",
        priv->ppe_xclbin_loc);
  } else {
    priv->ppe_xclbin_loc = NULL;
    GST_INFO_OBJECT (self, "xclbin path is not set");
  }

#if defined(XLNX_PCIe_PLATFORM)
  value = json_object_get (root, "device-index");
  if (!json_is_integer (value)) {
    GST_ERROR_OBJECT (self,
        "device-index is not set in preprocess json file %s",
        self->ppe_json_file);
    goto error;
  }
  priv->ppe_dev_idx = json_integer_value (value);
  GST_INFO_OBJECT (self, "preprocess device index %d", priv->ppe_dev_idx);
#endif

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

  /* get kernels array */
  karray = json_object_get (root, "kernels");
  if (!karray) {
    GST_ERROR_OBJECT (self, "failed to find key kernels");
    goto error;
  }

  if (!json_is_array (karray)) {
    GST_ERROR_OBJECT (self, "kernels key is not of array type");
    goto error;
  }

  kernel_count = json_array_size (karray);
  if (kernel_count > 1) {
    GST_WARNING_OBJECT (self,
        "number of kernels > %d not supported. taking first one only",
        kernel_count);
  }

  kernel = json_array_get (karray, 0);
  if (!kernel) {
    GST_ERROR_OBJECT (self, "failed to get kernel object");
    goto error;
  }

  priv->ppe_kernel = (Vvas_XInfer *) calloc (1, sizeof (Vvas_XInfer));
  if (!priv->ppe_kernel) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }
  priv->ppe_kernel->cu_idx = -1;

  value = json_object_get (kernel, "library-name");
  if (!json_is_string (value)) {
    GST_ERROR_OBJECT (self, "library name is not of string type");
    goto error;
  }

  /* absolute path VVAS library */
  priv->ppe_kernel->vvas_lib_path =
      g_strconcat (lib_path, json_string_value (value), NULL);
  GST_DEBUG_OBJECT (self, "vvas preprocess library path %s",
      priv->ppe_kernel->vvas_lib_path);

  /* get kernel name */
  value = json_object_get (kernel, "kernel-name");
  if (value) {
    if (!json_is_string (value)) {
      GST_ERROR_OBJECT (self, "primary kernel name is not of string type");
      goto error;
    }
    priv->ppe_kernel->name = g_strdup (json_string_value (value));
  } else {
    priv->ppe_kernel->name = NULL;
  }
  GST_INFO_OBJECT (self, "Primary kernel name %s", priv->ppe_kernel->name);

  /* get vvas kernel lib internal configuration */
  value = json_object_get (kernel, "config");
  if (!json_is_object (value)) {
    GST_ERROR_OBJECT (self, "config is not of object type");
    goto error;
  }

  priv->ppe_kernel->lib_config = value;

  GST_DEBUG_OBJECT (self, "preprocess kernel config size = %lu",
      json_object_size (value));

  /* find whether required symbols present or not */
  if (!find_kernel_lib_symbols (self, priv->ppe_kernel)) {
    GST_ERROR_OBJECT (self, "failed find symbols in kernel lib...");
    goto error;
  }

  if (lib_path)
    g_free (lib_path);

  priv->ppe_kernel->root_config = root;
  return TRUE;

error:
  if (lib_path)
    g_free (lib_path);
  if (root)
    json_decref (root);
  return FALSE;
}

static gboolean
vvas_xinfer_read_infer_config (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  json_t *root = NULL, *kernel, *value;
  json_error_t error;
  gchar *lib_path = NULL;

  priv->infer_level = DEFAULT_INFER_LEVEL;
  priv->infer_batch_size = BATCH_SIZE_ZERO;
  priv->low_latency_infer = TRUE;
  priv->infer_attach_ppebuf = FALSE;

  /* get root json object */
  root = json_load_file (self->infer_json_file, JSON_DECODE_ANY, &error);
  if (!root) {
    GST_ERROR_OBJECT (self, "failed to load json file. reason %s", error.text);
    goto error;
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

  /* get kernels object */
  kernel = json_object_get (root, "kernel");
  if (!json_is_object (kernel)) {
    GST_ERROR_OBJECT (self, "failed to find kernel object");
    goto error;
  }

  priv->infer_kernel = (Vvas_XInfer *) calloc (1, sizeof (Vvas_XInfer));
  if (!priv->infer_kernel) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }
  priv->infer_kernel->cu_idx = -1;

  value = json_object_get (kernel, "library-name");
  if (!json_is_string (value)) {
    GST_ERROR_OBJECT (self, "library name is not of string type");
    goto error;
  }

  /* absolute path VVAS library */
  priv->infer_kernel->vvas_lib_path =
      g_strconcat (lib_path, json_string_value (value), NULL);
  GST_DEBUG_OBJECT (self, "vvas library path %s",
      priv->infer_kernel->vvas_lib_path);

  /* find whether required symbols present or not */
  if (!find_kernel_lib_symbols (self, priv->infer_kernel)) {
    GST_ERROR_OBJECT (self, "failed find symbols in kernel lib...");
    goto error;
  }

  /* get vvas kernel lib internal configuration */
  value = json_object_get (kernel, "config");
  if (!json_is_object (value)) {
    GST_ERROR_OBJECT (self, "config is not of object type");
    goto error;
  }

  priv->infer_kernel->lib_config = value;
  GST_DEBUG_OBJECT (self, "kernel config size = %lu", json_object_size (value));

  value = json_object_get (priv->infer_kernel->lib_config, "batch-size");
  if (json_is_integer (value)) {
    priv->infer_batch_size = json_integer_value (value);
    if (priv->infer_batch_size >= MAX_NUM_OBJECT) {
      GST_ERROR_OBJECT (self, "batch-size should not >= %d",
          MAX_NUM_OBJECT - 1);
      goto error;
    }
  }

  value = json_object_get (root, "inference-level");
  if (json_is_integer (value)) {
    priv->infer_level = json_integer_value (value);
    if (priv->infer_level < 1) {
      GST_ERROR_OBJECT (self,
          "inference-level %d can't be less than 1", priv->infer_level);
      goto error;
    }
  }

  /* making batch size as default max-queue-size */
  priv->max_infer_queue = priv->infer_batch_size;

  value = json_object_get (root, "low-latency");
  if (value) {
    if (!json_is_boolean (value)) {
      GST_ERROR_OBJECT (self, "low-latency is not a boolean type");
      goto error;
    }

    priv->low_latency_infer = json_boolean_value (value);
    GST_INFO_OBJECT (self, "setting low-latency to %d",
        priv->low_latency_infer);
  }

  value = json_object_get (root, "inference-max-queue");
  if (!json_is_integer (value)) {
    GST_WARNING_OBJECT (self, "inference-max-queue is not set."
        "taking batch-size %d as default", priv->infer_batch_size);
  } else {
    priv->max_infer_queue = json_integer_value (value);
    if (priv->max_infer_queue < priv->infer_batch_size) {
      GST_WARNING_OBJECT (self, "inference-max-queue can't be less than "
          "batch-size. taking batch-size %d as default queue length",
          priv->infer_batch_size);
      priv->max_infer_queue = priv->infer_batch_size;
    } else {
      GST_INFO_OBJECT (self, "setting inference-max-queue to %d",
          priv->max_infer_queue);
    }
  }

  value = json_object_get (root, "attach-ppe-outbuf");
  if (value) {
    if (!json_is_boolean (value)) {
      GST_ERROR_OBJECT (self, "attach-ppe-outbuf is not a boolean type");
      goto error;
    }

    priv->infer_attach_ppebuf = json_boolean_value (value);
    GST_INFO_OBJECT (self, "setting attach-ppe-outbuf to %d",
        priv->infer_attach_ppebuf);
  }

  GST_INFO_OBJECT (self, "inference-level = %d and batch-size = %d",
      priv->infer_level, priv->infer_batch_size);

  if (lib_path)
    g_free (lib_path);

  priv->infer_kernel->root_config = root;

  return TRUE;

error:
  if (lib_path)
    g_free (lib_path);
  if (root)
    json_decref (root);
  return FALSE;
}

static gboolean
vvas_xinfer_ppe_deinit (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  Vvas_XInfer *kernel = priv->ppe_kernel;
  int iret;
  gint cu_idx = -1;

  if (kernel) {
    cu_idx = kernel->cu_idx;

    if (kernel->kernel_deinit_func) {
      iret = kernel->kernel_deinit_func (kernel->vvas_handle);
      if (iret < 0) {
        GST_ERROR_OBJECT (self, "failed to do preprocess kernel deinit..");
      }
      GST_DEBUG_OBJECT (self, "successfully completed preprocess deinit");
    }

    if (kernel->vvas_handle) {
      if (kernel->vvas_handle->ert_cmd_buf) {
        vvas_xrt_free_xrt_buffer (priv->ppe_dev_handle,
            kernel->vvas_handle->ert_cmd_buf);
        free (kernel->vvas_handle->ert_cmd_buf);
      }

      /* De-allocate the name */
      if(kernel->vvas_handle->name) {
        g_free(kernel->vvas_handle->name);
      }

      free (kernel->vvas_handle);
    }

    if (kernel->lib_fd)
      dlclose (kernel->lib_fd);

    if (kernel->root_config)
      json_decref (kernel->root_config);

    if (kernel->vvas_lib_path)
      g_free (kernel->vvas_lib_path);

    if (kernel->name)
      free (kernel->name);

    free (priv->ppe_kernel);
    priv->ppe_kernel = NULL;
  }

  g_slice_free1 (sizeof (Vvas_XInferFrame), priv->ppe_frame);

  g_mutex_clear (&self->priv->ppe_lock);
  g_cond_clear (&self->priv->ppe_has_input);
  g_cond_clear (&self->priv->ppe_need_input);

  if (priv->ppe_outpool && gst_buffer_pool_is_active (priv->ppe_outpool)) {
    if (!gst_buffer_pool_set_active (priv->ppe_outpool, FALSE)) {
      GST_ERROR_OBJECT (self, "failed to deactivate preprocess output pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to deactivate pool."),
          ("failed to deactivate preprocess output pool"));
      return FALSE;
    }
  }

  if (priv->input_pool && gst_buffer_pool_is_active (priv->input_pool)) {
    if (!gst_buffer_pool_set_active (priv->input_pool, FALSE)) {
      GST_ERROR_OBJECT (self, "failed to deactivate PPE internal input pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to deactivate pool."),
          ("failed to deactivate PPE internal input pool"));
      return FALSE;
    }
  }

  if (priv->ppe_outpool) {
    gst_object_unref (priv->ppe_outpool);
    priv->ppe_outpool = NULL;
  }
  if (priv->ppe_out_vinfo) {
    gst_video_info_free (priv->ppe_out_vinfo);
    priv->ppe_out_vinfo = NULL;
  }

  if (priv->ppe_xclbin_loc)
    free (priv->ppe_xclbin_loc);

  if (priv->ppe_dev_handle) {
    if (cu_idx >= 0) {
      GST_INFO_OBJECT (self, "closing context for cu_idx %d", cu_idx);
      vvas_xrt_close_context (priv->ppe_dev_handle, priv->ppe_xclbinId, cu_idx);
    }
    vvas_xrt_close_device (priv->ppe_dev_handle);
  }

  return TRUE;
}

static gboolean
vvas_xinfer_infer_deinit (GstVvas_XInfer * self)
{
  GstVvas_XInferPrivate *priv = self->priv;
  Vvas_XInfer *kernel = priv->infer_kernel;
  int iret;

  if (kernel) {

    if (kernel->kernel_deinit_func) {
      iret = kernel->kernel_deinit_func (kernel->vvas_handle);
      if (iret < 0) {
        GST_ERROR_OBJECT (self, "failed to do inference kernel deinit..");
      }
      GST_DEBUG_OBJECT (self, "successfully completed inference deinit");
    }

    if (kernel->lib_fd)
      dlclose (kernel->lib_fd);

    if (kernel->vvas_handle)
      free (kernel->vvas_handle);

    if (kernel->root_config)
      json_decref (kernel->root_config);

    if (kernel->vvas_lib_path)
      g_free (kernel->vvas_lib_path);

    if (kernel->name)
      free (kernel->name);

    free (priv->infer_kernel);
    priv->infer_kernel = NULL;
  }

  GST_INFO_OBJECT (self, "free infer batch queue of size %d",
      g_queue_get_length (priv->infer_batch_queue));

  g_mutex_lock (&priv->infer_lock);

  while (!g_queue_is_empty (priv->infer_batch_queue)) {
    Vvas_XInferFrame *frame = g_queue_pop_head (priv->infer_batch_queue);

    if (frame->in_vframe) {
      gst_video_frame_unmap (frame->in_vframe);
      g_slice_free1 (sizeof (GstVideoFrame), frame->in_vframe);
    }

    if (frame->last_parent_buf)
      gst_buffer_unref (frame->parent_buf);

    if (frame->parent_vinfo)
      gst_video_info_free (frame->parent_vinfo);

    if (priv->infer_level > 1) {
      if (frame->child_buf) {
        GstInferenceMeta *child_meta = NULL;

        child_meta = (GstInferenceMeta *) gst_buffer_get_meta (frame->child_buf,
            gst_inference_meta_api_get_type ());
        child_meta->prediction = gst_inference_prediction_new ();
      }
    }

    if (frame->child_buf)
      gst_buffer_unref (frame->child_buf);

    if (frame->child_vinfo)
      gst_video_info_free (frame->child_vinfo);
    if (frame->vvas_frame)
      g_slice_free1 (sizeof (VVASFrame), frame->vvas_frame);
    if (frame->event)
      gst_event_unref (frame->event);

    g_slice_free1 (sizeof (VVASFrame), frame);
  }
  g_mutex_unlock (&priv->infer_lock);

  g_mutex_clear (&priv->infer_lock);
  g_cond_clear (&priv->infer_cond);
  g_queue_free (priv->infer_batch_queue);

  g_queue_free (priv->infer_sub_buffers);

  return TRUE;
}

static gboolean
vvas_xinfer_prepare_ppe_input_frame (GstVvas_XInfer * self, GstBuffer * inbuf,
    GstVideoInfo * in_vinfo, GstBuffer ** new_inbuf, VVASFrame * vvas_frame)
{
  GstVvas_XInferPrivate *priv = self->priv;
  VVASKernel *vvas_handle = priv->ppe_kernel->vvas_handle;
  guint64 phy_addr = 0;
  guint plane_id;
  gboolean bret = FALSE;
  GstVideoMeta *vmeta = NULL;
  GstMemory *in_mem = NULL;

  in_mem = gst_buffer_get_memory (inbuf, 0);
  if (in_mem == NULL) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    goto error;
  }

  /* prepare HW input buffer to send it to preprocess */
  if (gst_is_vvas_memory (in_mem) &&
      gst_vvas_memory_can_avoid_copy (in_mem, priv->ppe_dev_idx,
				      vvas_handle->in_mem_bank)) {
    phy_addr = gst_vvas_allocator_get_paddr (in_mem);
  } else if (gst_is_dmabuf_memory (in_mem)) {
    guint bo = NULLBO;
    gint dma_fd = -1;
    struct xclBOProperties p;

    dma_fd = gst_dmabuf_memory_get_fd (in_mem);
    if (dma_fd < 0) {
      GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
      goto error;
    }

    /* dmabuf but not from vvas allocator */
    bo = vvas_xrt_import_bo (priv->ppe_dev_handle, dma_fd, 0);
    if (bo == NULLBO) {
      GST_WARNING_OBJECT (self,
          "failed to get XRT BO...fall back to copy input");
    }

    GST_DEBUG_OBJECT (self, "received dma fd %d and its xrt BO = %u", dma_fd,
        bo);
    if (!vvas_xrt_get_bo_properties (priv->ppe_dev_handle, bo, &p)) {
      phy_addr = p.paddr;
    } else {
      GST_WARNING_OBJECT (self,
          "failed to get physical address...fall back to copy input");
    }

    if (bo != NULLBO)
      vvas_xrt_free_bo (priv->ppe_dev_handle, bo);
  }

  if (!phy_addr) {
    GST_DEBUG_OBJECT (self,
        "could not get phy_addr, copy input buffer to internal pool buffer");
    bret = vvas_xinfer_copy_input_buffer (self, inbuf, new_inbuf);
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
    inbuf = *new_inbuf;
  }
#ifdef XLNX_PCIe_PLATFORM
  /* syncs data when VVAS_SYNC_TO_DEVICE flag is enabled */
  bret = gst_vvas_memory_sync_bo (in_mem);
  if (!bret)
    goto error;
#endif

  GST_LOG_OBJECT (self, "input paddr %p", (void *) phy_addr);

  gst_memory_unref (in_mem);
  in_mem = NULL;

  vmeta = gst_buffer_get_video_meta (inbuf);
  if (vmeta) {
    vvas_frame->props.stride = vmeta->stride[0];
  } else {
    GST_DEBUG_OBJECT (self, "video meta not present in buffer");
    vvas_frame->props.stride = GST_VIDEO_INFO_PLANE_STRIDE (in_vinfo, 0);
  }

  vvas_frame->props.width = GST_VIDEO_INFO_WIDTH (in_vinfo);
  vvas_frame->props.height = GST_VIDEO_INFO_HEIGHT (in_vinfo);
  vvas_frame->props.fmt =
      get_kernellib_format (GST_VIDEO_INFO_FORMAT (in_vinfo));
  vvas_frame->n_planes = GST_VIDEO_INFO_N_PLANES (in_vinfo);
  vvas_frame->app_priv = inbuf;

  for (plane_id = 0; plane_id < GST_VIDEO_INFO_N_PLANES (in_vinfo); plane_id++) {
    gsize offset;

    if (vmeta) {
      offset = vmeta->offset[plane_id];
    } else {
      offset = GST_VIDEO_INFO_PLANE_OFFSET (in_vinfo, plane_id);
    }
    vvas_frame->paddr[plane_id] = phy_addr + offset;
    GST_LOG_OBJECT (self,
        "inbuf plane[%d] : paddr = %p, offset = %lu, stride = %d", plane_id,
        (void *) vvas_frame->paddr[plane_id], offset, vvas_frame->props.stride);
  }

  GST_LOG_OBJECT (self, "successfully prepared ppe input vvas frame");
  return TRUE;

error:
  if (in_mem)
    gst_memory_unref (in_mem);

  return FALSE;
}

static gboolean
vvas_xinfer_prepare_infer_input_frame (GstVvas_XInfer * self, GstBuffer * inbuf,
    GstVideoInfo * in_vinfo, VVASFrame * vvas_frame, GstVideoFrame * in_vframe)
{
  guint plane_id;
  GstVideoMeta *vmeta = NULL;
  GstMemory *in_mem = NULL;
  GstMapFlags map_flags;

  map_flags = GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF;
  if (!gst_video_frame_map (in_vframe, in_vinfo, inbuf, map_flags)) {
    GST_ERROR_OBJECT (self, "failed to map input buffer");
    goto error;
  }

  for (plane_id = 0; plane_id < GST_VIDEO_INFO_N_PLANES (in_vinfo); plane_id++) {
    vvas_frame->vaddr[plane_id] =
        GST_VIDEO_FRAME_PLANE_DATA (in_vframe, plane_id);
    GST_LOG_OBJECT (self, "inbuf plane[%d] : vaddr = %p", plane_id,
        vvas_frame->vaddr[plane_id]);
  }

  vmeta = gst_buffer_get_video_meta (inbuf);
  if (vmeta) {
    vvas_frame->props.stride = vmeta->stride[0];
  } else {
    GST_DEBUG_OBJECT (self, "video meta not present in buffer");
    vvas_frame->props.stride = GST_VIDEO_INFO_PLANE_STRIDE (in_vinfo, 0);
  }

  vvas_frame->props.width = GST_VIDEO_INFO_WIDTH (in_vinfo);
  vvas_frame->props.height = GST_VIDEO_INFO_HEIGHT (in_vinfo);
  vvas_frame->props.fmt =
      get_kernellib_format (GST_VIDEO_INFO_FORMAT (in_vinfo));
  vvas_frame->n_planes = GST_VIDEO_INFO_N_PLANES (in_vinfo);
  vvas_frame->app_priv = inbuf;

#ifdef DUMP_INFER_INPUT
  {
    int ret;
    char str[100];
    sprintf(str, "inferinput_%d_%dx%d.bgr",
      self->priv->infer_level,
      vvas_frame->props.width,vvas_frame->props.height);

    if (self->priv->fp == NULL) {
      self->priv->fp = fopen (str, "w+");
      printf("opened %s\n", str);
    }

    ret =
        fwrite (vvas_frame->vaddr[0], 1,
        vvas_frame->props.width * vvas_frame->props.height * 3, self->priv->fp);
    printf ("written %s infer input frame size = %d  %dx%d\n", str,ret,vvas_frame->props.width,
      vvas_frame->props.height);
  }
#endif

  GST_LOG_OBJECT (self, "successfully prepared inference input vvas frame");
  return TRUE;

error:
  if (in_mem)
    gst_memory_unref (in_mem);

  return FALSE;
}

static gboolean
vvas_xinfer_prepare_ppe_output_frame (GstVvas_XInfer * self, GstBuffer * outbuf,
    GstVideoInfo * out_vinfo, VVASFrame * vvas_frame)
{
  GstMemory *out_mem = NULL;
  guint64 phy_addr = -1;
  guint plane_id;
  GstVideoMeta *vmeta = NULL;

  out_mem = gst_buffer_get_memory (outbuf, 0);
  if (out_mem == NULL) {
    GST_ERROR_OBJECT (self, "failed to get memory from output buffer");
    goto error;
  }

  phy_addr = gst_vvas_allocator_get_paddr (out_mem);

  vmeta = gst_buffer_get_video_meta (outbuf);
  if (vmeta == NULL) {
    GST_ERROR_OBJECT (self, "video meta not present in output buffer");
    goto error;
  }

  vvas_frame->props.width = GST_VIDEO_INFO_WIDTH (out_vinfo);
  vvas_frame->props.height = GST_VIDEO_INFO_HEIGHT (out_vinfo);
  vvas_frame->props.stride = vmeta->stride[0];
  vvas_frame->props.fmt =
      get_kernellib_format (GST_VIDEO_INFO_FORMAT (out_vinfo));
  vvas_frame->n_planes = GST_VIDEO_INFO_N_PLANES (out_vinfo);
  vvas_frame->app_priv = outbuf;

  for (plane_id = 0; plane_id < GST_VIDEO_INFO_N_PLANES (out_vinfo); plane_id++) {
    vvas_frame->paddr[plane_id] = phy_addr + vmeta->offset[plane_id];

    GST_LOG_OBJECT (self,
        "outbuf plane[%d] : paddr = %p, offset = %lu, stride = %d", plane_id,
        (void *) vvas_frame->paddr[plane_id], vmeta->offset[plane_id],
        vvas_frame->props.stride);
  }

#ifdef XLNX_PCIe_PLATFORM
  /* setting SYNC_FROM_DEVICE here to avoid work by kernel lib */
  gst_vvas_memory_set_sync_flag (out_mem, VVAS_SYNC_FROM_DEVICE);
#endif
  gst_memory_unref (out_mem);
  GST_LOG_OBJECT (self, "successfully prepared output vvas frame");
  return TRUE;

error:
  if (out_mem)
    gst_memory_unref (out_mem);

  return FALSE;
}

static gpointer
vvas_xinfer_ppe_loop (gpointer data)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (data);
  GstVvas_XInferPrivate *priv = self->priv;
  Vvas_XInfer *kernel = priv->ppe_kernel;

  priv->ppe_thread_state = VVAS_THREAD_RUNNING;

  while (!priv->stop) {
    int ret;
    GstFlowReturn fret = GST_FLOW_OK;
    gboolean bret;
    GstInferenceMeta *parent_meta = NULL;
    guint out_frames_count = 0, oidx;
    gboolean do_ppe = FALSE;

    g_mutex_lock (&priv->ppe_lock);
    if (priv->ppe_need_data && !priv->stop) {
      /* wait for input to PPE */
      g_cond_wait (&priv->ppe_has_input, &priv->ppe_lock);
    }
    g_mutex_unlock (&priv->ppe_lock);

    if (priv->stop)
      goto exit;

    if (priv->ppe_frame->event &&
        GST_EVENT_TYPE (priv->ppe_frame->event) == GST_EVENT_EOS) {
      Vvas_XInferFrame *event_frame;

      event_frame = g_slice_new0 (Vvas_XInferFrame);
      event_frame->event = priv->ppe_frame->event;
      event_frame->skip_processing = TRUE;

      g_mutex_lock (&priv->infer_lock);
      g_queue_push_tail (priv->infer_batch_queue, event_frame);
      g_mutex_unlock (&priv->infer_lock);
      GST_INFO_OBJECT (self, "received EOS event, push frame %p and exit",
          event_frame);
      goto exit;
    }

    parent_meta = (GstInferenceMeta *) gst_buffer_get_meta
        (priv->ppe_frame->parent_buf, gst_inference_meta_api_get_type ());

    if (priv->infer_level == 1) {
      GstBuffer *child_buf = NULL;
      GstVideoInfo *child_vinfo = NULL;

      if (parent_meta &&
          vvas_xinfer_is_sub_buffer_useful (self,
              parent_meta->prediction->sub_buffer)) {
        GstVideoMeta *vmeta = NULL;
        GstVideoFrame *infer_vframe;
        Vvas_XInferFrame *infer_frame;
        VVASFrame *vvas_frame;

        child_buf = gst_buffer_ref (parent_meta->prediction->sub_buffer);
        child_vinfo = gst_video_info_new ();
        vmeta = gst_buffer_get_video_meta (child_buf);
        gst_video_info_set_format (child_vinfo, vmeta->format,
            vmeta->width, vmeta->height);
        /* use sub_buffer directly in inference stage. No need of PPE */
        out_frames_count = 0;

        infer_frame = g_slice_new0 (Vvas_XInferFrame);
        infer_vframe = g_slice_new0 (GstVideoFrame);
        vvas_frame = g_slice_new0 (VVASFrame);

        bret = vvas_xinfer_prepare_infer_input_frame (self, child_buf,
            child_vinfo, vvas_frame, infer_vframe);
        if (!bret) {
          g_print ("failed outbuf loop count\n\n");
          goto error;
        }

        infer_frame->parent_buf = priv->ppe_frame->parent_buf;
        infer_frame->parent_vinfo =
            gst_video_info_copy (priv->ppe_frame->parent_vinfo);
        infer_frame->last_parent_buf = TRUE;
        infer_frame->vvas_frame = vvas_frame;
        infer_frame->in_vframe = infer_vframe;
        infer_frame->child_buf = vvas_frame->app_priv;
        infer_frame->child_vinfo = gst_video_info_copy (child_vinfo);
        infer_frame->skip_processing = FALSE;

        g_mutex_lock (&priv->infer_lock);
        g_queue_push_tail (priv->infer_batch_queue, infer_frame);
        g_mutex_unlock (&priv->infer_lock);

        gst_video_info_free (child_vinfo);
        do_ppe = FALSE;

      } else {
        GstBuffer *outbuf;
        VVASFrame *out_vvas_frame;

        /* first level of inference will be on full frame */
        out_frames_count = 1;
        /* acquire ppe output buffer */
        fret =
            gst_buffer_pool_acquire_buffer (priv->ppe_outpool, &outbuf, NULL);
        if (fret != GST_FLOW_OK) {
          GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
              priv->ppe_outpool);
          goto error;
        }

        GST_LOG_OBJECT (self, "acquired PPE output buffer %p", outbuf);

        if (parent_meta) { /* level-1 has metadata already, make current inference as sibiling */
          GstInferenceMeta *out_meta = NULL;

          out_meta =
              ((GstInferenceMeta *) gst_buffer_get_meta (outbuf,
                  gst_inference_meta_api_get_type ()));
          if (!out_meta) {
            GST_LOG_OBJECT (self, "add inference metadata to %p", outbuf);
            out_meta =
                (GstInferenceMeta *) gst_buffer_add_meta (outbuf,
                gst_inference_meta_get_info (), NULL);
          }

          gst_inference_prediction_unref (out_meta->prediction);
          gst_inference_prediction_ref (parent_meta->prediction);
          out_meta->prediction = parent_meta->prediction;
        }

        out_vvas_frame = g_slice_new0 (VVASFrame);

        bret =
            vvas_xinfer_prepare_ppe_output_frame (self, outbuf,
            priv->ppe_out_vinfo, out_vvas_frame);
        if (!bret)
          goto error;

        kernel->output[0] = out_vvas_frame;
        do_ppe = TRUE;
      }
    } else {                    /* level > 1 */
      if (parent_meta) {
        guint sub_bufs_len = 0;

        GST_LOG_OBJECT (self, "infer_level = %d & meta data depth = %d",
            priv->infer_level,
            g_node_max_height (parent_meta->prediction->predictions));

	if (priv->infer_level >
	    g_node_max_height (parent_meta->prediction->predictions)) {
	  goto skipframe;
	}

        if (priv->infer_level <=
            g_node_max_height (parent_meta->prediction->predictions)) {
          g_node_traverse (parent_meta->prediction->predictions, G_PRE_ORDER,
              G_TRAVERSE_ALL, -1, prepare_inference_sub_buffers, self);
        }

        sub_bufs_len = g_queue_get_length (priv->infer_sub_buffers);

        if (sub_bufs_len > 0) {
          GstVideoInfo *child_vinfo = NULL;
          GstBuffer *child_buf = NULL;

          /* use sub_buffer directly in inference stage. No need of PPE */
          out_frames_count = 0;

          child_vinfo = gst_video_info_new ();

          GST_DEBUG_OBJECT (self,
              "input buffer %p has %u in inference level %u",
              priv->ppe_frame->parent_buf, sub_bufs_len, priv->infer_level);

          for (oidx = 0; oidx < sub_bufs_len; oidx++) {
            GstVideoMeta *vmeta;
            GstVideoFrame *infer_vframe;
            Vvas_XInferFrame *infer_frame;
            VVASFrame *vvas_frame;

            child_buf =
                gst_buffer_ref (g_queue_pop_head (priv->infer_sub_buffers));
            vmeta = gst_buffer_get_video_meta (child_buf);
            gst_video_info_set_format (child_vinfo, vmeta->format,
                vmeta->width, vmeta->height);

            infer_frame = g_slice_new0 (Vvas_XInferFrame);
            infer_vframe = g_slice_new0 (GstVideoFrame);
            vvas_frame = g_slice_new0 (VVASFrame);

            bret = vvas_xinfer_prepare_infer_input_frame (self, child_buf,
                child_vinfo, vvas_frame, infer_vframe);
            if (!bret) {
              g_print ("failed outbuf loop count\n\n");
              goto error;
            }

            infer_frame->parent_buf = priv->ppe_frame->parent_buf;
            infer_frame->parent_vinfo =
                gst_video_info_copy (priv->ppe_frame->parent_vinfo);
            infer_frame->last_parent_buf =
                (oidx == (sub_bufs_len - 1)) ? TRUE : FALSE;
            infer_frame->vvas_frame = vvas_frame;
            infer_frame->in_vframe = infer_vframe;
            infer_frame->child_buf = vvas_frame->app_priv;
            infer_frame->child_vinfo = gst_video_info_copy (child_vinfo);
            infer_frame->skip_processing = FALSE;

            g_mutex_lock (&priv->infer_lock);
            g_queue_push_tail (priv->infer_batch_queue, infer_frame);
            g_mutex_unlock (&priv->infer_lock);
          }
          gst_video_info_free (child_vinfo);
          do_ppe = FALSE;
        } else {
          priv->nframes_in_level = 0;

          /* ppe_kernel->output array will be filled on node traversal */
          g_node_traverse (parent_meta->prediction->predictions, G_PRE_ORDER,
              G_TRAVERSE_ALL, priv->infer_level, prepare_ppe_outbuf_at_level,
              self);
          if (priv->is_error)
            goto error;

          GST_DEBUG_OBJECT (self, "number of nodes at level-%d = %d",
              priv->infer_level, priv->nframes_in_level);

          out_frames_count = priv->nframes_in_level;
          do_ppe = TRUE;
        }
      } else { /* no level-1 inference available, skip this frame */
        Vvas_XInferFrame *infer_frame;

skipframe:
        out_frames_count = 0;
        do_ppe = FALSE;

        infer_frame = g_slice_new0 (Vvas_XInferFrame);
        infer_frame->parent_buf = priv->ppe_frame->parent_buf;
        infer_frame->parent_vinfo =
            gst_video_info_copy (priv->ppe_frame->parent_vinfo);
        infer_frame->last_parent_buf = TRUE;
        infer_frame->vvas_frame = NULL;
        infer_frame->in_vframe = NULL;
        infer_frame->child_buf = NULL;
        infer_frame->child_vinfo = NULL;
        infer_frame->skip_processing = TRUE;

        GST_DEBUG_OBJECT (self, "skipping buffer %p in ppe & inference stage",
            infer_frame->parent_buf);
        g_mutex_lock (&priv->infer_lock);
        if (priv->stop) {
          g_slice_free1 (sizeof (Vvas_XInferFrame), infer_frame);
          g_mutex_unlock (&priv->infer_lock);
          goto exit;
        }
        /* send input frame to inference thread */
        g_queue_push_tail (priv->infer_batch_queue, infer_frame);

        if (priv->infer_batch_size ==
            g_queue_get_length (priv->infer_batch_queue)) {
          g_cond_signal (&priv->infer_cond);
        }
        g_mutex_unlock (&priv->infer_lock);
      }
    }

    if (do_ppe) {
      kernel->input[0] = priv->ppe_frame->vvas_frame;

      ret = kernel->kernel_start_func (kernel->vvas_handle, 0, kernel->input,
          kernel->output);
      if (ret < 0) {
        GST_ERROR_OBJECT (self, "kernel start failed");
        goto error;
      }

      ret = kernel->kernel_done_func (kernel->vvas_handle);
      if (ret < 0) {
        GST_ERROR_OBJECT (self, "kernel done failed");
        goto error;
      }

      GST_DEBUG_OBJECT (self, "completed preprocessing of %d output frames",
          out_frames_count);

      for (oidx = 0; oidx < out_frames_count; oidx++) {
        GstVideoFrame *in_vframe;
        Vvas_XInferFrame *infer_frame;
        GstBuffer *inbuf;
        VVASFrame *in_vvas_frame = kernel->output[oidx]; /* input to inference stage */

        in_vframe = g_slice_new0 (GstVideoFrame);
        infer_frame = g_slice_new0 (Vvas_XInferFrame);
        inbuf = in_vvas_frame->app_priv;

        /* output of PPE will be input of inference stage */
        bret = vvas_xinfer_prepare_infer_input_frame (self, inbuf,
            priv->ppe_out_vinfo, in_vvas_frame, in_vframe);
        if (!bret) {
          goto error;
        }

        infer_frame->parent_buf = priv->ppe_frame->parent_buf;
        infer_frame->parent_vinfo =
            gst_video_info_copy (priv->ppe_frame->parent_vinfo);
        infer_frame->last_parent_buf =
            (oidx == (out_frames_count - 1)) ? TRUE : FALSE;
        infer_frame->vvas_frame = in_vvas_frame;
        infer_frame->in_vframe = in_vframe;
        infer_frame->child_buf = in_vvas_frame->app_priv;
        infer_frame->child_vinfo = gst_video_info_copy (priv->ppe_out_vinfo);
        infer_frame->skip_processing = FALSE;

        /* send input frame to inference thread */
        g_mutex_lock (&priv->infer_lock);
        GST_LOG_OBJECT (self, "pushing child_buf %p in infer_frame %p to queue",
            infer_frame->child_buf, infer_frame);
        g_queue_push_tail (priv->infer_batch_queue, infer_frame);
        g_mutex_unlock (&priv->infer_lock);
      }
    }

    g_mutex_lock (&priv->infer_lock);
    if ((priv->infer_level > 1 && priv->low_latency_infer) ||
        (g_queue_get_length (priv->infer_batch_queue) >=
            priv->infer_batch_size)) {
      g_cond_signal (&priv->infer_cond);
    }
    g_mutex_unlock (&priv->infer_lock);

    /* free PPE input members */
    if (priv->ppe_frame->child_buf)
      gst_buffer_unref (priv->ppe_frame->child_buf);
    if (priv->ppe_frame->child_vinfo)
      gst_video_info_free (priv->ppe_frame->child_vinfo);
    g_slice_free1 (sizeof (VVASFrame), priv->ppe_frame->vvas_frame);

    if (priv->ppe_frame->parent_vinfo) {
      gst_video_info_free (priv->ppe_frame->parent_vinfo);
      priv->ppe_frame->parent_vinfo = NULL;
    }

    memset (kernel->input, 0x0, sizeof (VVASFrame *) * MAX_NUM_OBJECT);
    memset (kernel->output, 0x0, sizeof (VVASFrame *) * MAX_NUM_OBJECT);

    g_mutex_lock (&priv->ppe_lock);
    priv->ppe_need_data = TRUE;
    g_cond_signal (&priv->ppe_need_input);
    g_mutex_unlock (&priv->ppe_lock);
  }

error:
  GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to process frame in PPE."),
      ("failed to process frame in PPE."));
  priv->last_fret = GST_FLOW_ERROR;

exit:
  priv->ppe_thread_state = VVAS_THREAD_EXITED;

  g_mutex_lock (&priv->infer_lock);
  g_cond_signal (&priv->infer_cond);
  g_mutex_unlock (&priv->infer_lock);
  return NULL;
}

#ifdef XLNX_PCIe_PLATFORM
#define ALIGN_VAL 32
#else
#define ALIGN_VAL 16
#endif
static void
update_child_bbox (GNode * node, gpointer data)
{
  Vvas_XInferNodeInfo *node_info = (Vvas_XInferNodeInfo *) data;
  GstVvas_XInfer *self = node_info->self;
  gdouble hfactor, vfactor;
  int align = ALIGN_VAL;
  gint fw = 1, fh = 1, tw, th;
  GstInferencePrediction *cur_prediction =
      (GstInferencePrediction *) node->data;
  GstInferencePrediction *parent_prediction =
      (GstInferencePrediction *) node->parent->data;

  if (cur_prediction->bbox_scaled)
    return;

  if (cur_prediction->bbox.width && cur_prediction->bbox.height) {
    tw = GST_VIDEO_INFO_WIDTH (node_info->parent_vinfo);
    th = GST_VIDEO_INFO_HEIGHT (node_info->parent_vinfo);
    fw = GST_VIDEO_INFO_WIDTH (node_info->child_vinfo);
    fh = GST_VIDEO_INFO_HEIGHT (node_info->child_vinfo);

    GST_LOG_OBJECT (self, "bbox : x = %d, y = %d, w = %d, h = %d",
        cur_prediction->bbox.x, cur_prediction->bbox.y,
        cur_prediction->bbox.width, cur_prediction->bbox.height);

    if (!parent_prediction->bbox.width && !parent_prediction->bbox.height) {
      hfactor = tw * 1.0 / fw;
      vfactor = th * 1.0 / fh;
    } else {
      hfactor = parent_prediction->bbox.width * 1.0 / fw;
      vfactor = parent_prediction->bbox.height * 1.0 / fh;
    }

    cur_prediction->bbox.x = nearbyintf (cur_prediction->bbox.x * hfactor);
    cur_prediction->bbox.y = nearbyintf (cur_prediction->bbox.y * vfactor);
    cur_prediction->bbox.width =
        nearbyintf (cur_prediction->bbox.width * hfactor);
    cur_prediction->bbox.height =
        nearbyintf (cur_prediction->bbox.height * vfactor);

    GST_LOG_OBJECT (self, "hfactor = %f vfactor = %f", hfactor, vfactor);
    GST_LOG_OBJECT (self, "bbox add factor : x = %d, y = %d, w = %d, h = %d",
        cur_prediction->bbox.x, cur_prediction->bbox.y,
        cur_prediction->bbox.width, cur_prediction->bbox.height);


    GST_LOG_OBJECT (self, "current node %p (x,y) : (%d, %d) -> (%d, %d)",
        node, cur_prediction->bbox.x, cur_prediction->bbox.y,
        cur_prediction->bbox.x + parent_prediction->bbox.x,
        cur_prediction->bbox.y + parent_prediction->bbox.y);

    /* scale x offset to parent bbox resolution */
    cur_prediction->bbox.x += parent_prediction->bbox.x;
    cur_prediction->bbox.y += parent_prediction->bbox.y;

    /* Do alignment */
    cur_prediction->bbox.x = (cur_prediction->bbox.x / align) * align ;
    cur_prediction->bbox.y = (cur_prediction->bbox.y / 2) * 2 ;
    cur_prediction->bbox.width =
        ALIGN(cur_prediction->bbox.width, align);
    cur_prediction->bbox.height =
        ALIGN (cur_prediction->bbox.height, align);
    if (cur_prediction->bbox.width > GST_VIDEO_INFO_WIDTH (node_info->parent_vinfo))
      cur_prediction->bbox.width = GST_VIDEO_INFO_WIDTH (node_info->parent_vinfo);
    if (cur_prediction->bbox.height > GST_VIDEO_INFO_HEIGHT (node_info->parent_vinfo))
      cur_prediction->bbox.height = GST_VIDEO_INFO_HEIGHT (node_info->parent_vinfo);

    GST_LOG_OBJECT (self, "modified bbox : x = %d, y = %d, w = %d, h = %d",
        cur_prediction->bbox.x, cur_prediction->bbox.y,
        cur_prediction->bbox.width, cur_prediction->bbox.height);
  }

  cur_prediction->bbox_scaled = TRUE;
}

static gboolean
vvas_xinfer_add_metadata_at_level_1 (GstVvas_XInfer * self,
    GstBuffer * parent_buf, GstVideoInfo * parent_vinfo,
    GstBuffer * infer_inbuf)
{
  GstInferenceMeta *parent_meta = NULL;
  GstInferenceMeta *child_meta = NULL;

  parent_meta = (GstInferenceMeta *) gst_buffer_get_meta (parent_buf,
      gst_inference_meta_api_get_type ());

  if (parent_buf != infer_inbuf) {
    child_meta = (GstInferenceMeta *) gst_buffer_add_meta (infer_inbuf,
        gst_inference_meta_get_info (), NULL);
    if (!child_meta) {
      GST_ERROR_OBJECT (self, "failed to add metadata to buffer %p",
          infer_inbuf);
      return FALSE;
    }

    if (parent_meta) {          /* assign parent prediction to inference */
      gst_inference_prediction_unref (child_meta->prediction);
      child_meta->prediction =
          gst_inference_prediction_ref (parent_meta->prediction);
    }

    if (self->priv->infer_attach_ppebuf) {
      if (child_meta->prediction->sub_buffer != infer_inbuf) {
        if (child_meta->prediction->sub_buffer)
          gst_buffer_unref (child_meta->prediction->sub_buffer);
        GST_LOG_OBJECT (self, "attaching %p as sub_buffer", infer_inbuf);
        child_meta->prediction->sub_buffer = gst_buffer_ref (infer_inbuf);
      } else {
        gst_buffer_ref (infer_inbuf);
      }
    }
  } else { /* parent buffer as inference buffer i.e. No PPE */
    if (!parent_meta) {
      child_meta = (GstInferenceMeta *) gst_buffer_add_meta (infer_inbuf,
          gst_inference_meta_get_info (), NULL);
      if (!child_meta) {
        GST_ERROR_OBJECT (self, "failed to add metadata to buffer %p",
            infer_inbuf);
        return FALSE;
      }
      child_meta->prediction->bbox.width = GST_VIDEO_INFO_WIDTH (parent_vinfo);
      child_meta->prediction->bbox.height =
          GST_VIDEO_INFO_HEIGHT (parent_vinfo);
    } else {
      /* no need to add metadata as it is already present */
    }
  }

  return TRUE;
}

static gpointer
vvas_xinfer_infer_loop (gpointer data)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (data);
  GstVvas_XInferPrivate *priv = self->priv;
  Vvas_XInfer *kernel = priv->infer_kernel;
  gint batch_len = 0;
  GstVideoFrame **in_vframes = NULL;
  GstBuffer **parent_bufs = NULL, **child_bufs = NULL;
  GstVideoInfo **parent_vinfos = NULL, **child_vinfos = NULL;
  GstEvent **events = NULL;
  VVASFrame **vvas_frames = NULL;
  guint cur_batch_size = 0;
  gint total_queued_size = 0, cur_queued_size = 0;
  gboolean sent_eos = FALSE;
  gboolean *push_parent_bufs = NULL;

  priv->infer_thread_state = VVAS_THREAD_RUNNING;

  in_vframes =
      (GstVideoFrame **) calloc (priv->max_infer_queue,
      sizeof (GstVideoFrame *));
  if (in_vframes == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  parent_bufs =
      (GstBuffer **) calloc (priv->max_infer_queue, sizeof (GstBuffer *));
  if (parent_bufs == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  parent_vinfos =
      (GstVideoInfo **) calloc (priv->max_infer_queue, sizeof (GstVideoInfo *));
  if (parent_vinfos == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  child_bufs =
      (GstBuffer **) calloc (priv->max_infer_queue, sizeof (GstBuffer *));
  if (child_bufs == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  child_vinfos =
      (GstVideoInfo **) calloc (priv->max_infer_queue, sizeof (GstVideoInfo *));
  if (child_vinfos == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  vvas_frames =
      (VVASFrame **) calloc (priv->max_infer_queue, sizeof (VVASFrame *));
  if (vvas_frames == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  events = (GstEvent **) calloc (priv->max_infer_queue, sizeof (GstEvent *));
  if (events == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  push_parent_bufs =
      (gboolean *) calloc (priv->max_infer_queue, sizeof (gboolean));
  if (push_parent_bufs == NULL) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  while (!priv->stop) {
    Vvas_XInferFrame *inframe = NULL;
    guint idx;
    guint min_batch = 0;
    int ret;

    g_mutex_lock (&priv->infer_lock);
    batch_len = g_queue_get_length (priv->infer_batch_queue);

    g_cond_signal (&priv->infer_batch_full);
    if (batch_len < priv->infer_batch_size &&
        priv->ppe_thread_state != VVAS_THREAD_EXITED && !priv->is_eos) {
      /* wait for batch size frames */
      GST_DEBUG_OBJECT (self, "wait for the next batch");
      g_cond_wait (&priv->infer_cond, &priv->infer_lock);
    }
    g_mutex_unlock (&priv->infer_lock);

    if (priv->stop)
      goto exit;

    if (priv->infer_level == 1 || !priv->low_latency_infer) {
      if ((g_queue_get_length (priv->infer_batch_queue) <
              priv->infer_batch_size)
          && priv->ppe_thread_state != VVAS_THREAD_EXITED && !priv->is_eos) {
        GST_ERROR_OBJECT (self, "unexpected behaviour!!! "
            "batch length (%d) < required batch size %d",
            g_queue_get_length (priv->infer_batch_queue),
            priv->infer_batch_size);
        goto error;
      }
    }

    /* get updated batch length */
    g_mutex_lock (&priv->infer_lock);
    batch_len = g_queue_get_length (priv->infer_batch_queue);
    g_mutex_unlock (&priv->infer_lock);
    GST_LOG_OBJECT (self, "batch length = %d & ppe_thread state = %d",
        batch_len, priv->ppe_thread_state);

  infer_pending:

    min_batch = batch_len > priv->infer_batch_size ?
        priv->infer_batch_size : batch_len;
    min_batch = priv->max_infer_queue - total_queued_size < min_batch ?
        priv->max_infer_queue - total_queued_size : min_batch;
    GST_DEBUG_OBJECT (self, "preparing batch of %d frames", min_batch);

    cur_queued_size = 0;

    for (idx = 0; idx < min_batch; idx++) {
      g_mutex_lock (&priv->infer_lock);
      inframe = g_queue_pop_head (priv->infer_batch_queue);
      GST_LOG_OBJECT (self,
          "popped frame %p from batch queue with skip_processing = %d", inframe,
          inframe->skip_processing);

      g_mutex_unlock (&priv->infer_lock);

      if (!inframe->skip_processing) {
        kernel->input[cur_batch_size] = inframe->vvas_frame;
        cur_batch_size++;

        if (priv->infer_level == 1) {
          /* inference input buffer will come from either submit_input_buffer or
           * PPE_thread in case of level-1, so metadata is not added in ppe_thread
           */
          vvas_xinfer_add_metadata_at_level_1 (self, inframe->parent_buf,
              inframe->parent_vinfo, inframe->vvas_frame->app_priv);
        }
      } else {
        GST_LOG_OBJECT (self, "skipping frame %p from inference", inframe);
      }

      vvas_frames[total_queued_size + idx] = inframe->vvas_frame;
      in_vframes[total_queued_size + idx] = inframe->in_vframe;
      parent_bufs[total_queued_size + idx] = inframe->parent_buf;
      parent_vinfos[total_queued_size + idx] = inframe->parent_vinfo;
      child_bufs[total_queued_size + idx] = inframe->child_buf;
      child_vinfos[total_queued_size + idx] = inframe->child_vinfo;
      events[total_queued_size + idx] = inframe->event;
      push_parent_bufs[total_queued_size + idx] = inframe->last_parent_buf;

      g_slice_free1 (sizeof (Vvas_XInferFrame), inframe);

      if (cur_batch_size == priv->infer_batch_size) {
        GST_LOG_OBJECT (self, "input batch is ready for inference");
        idx++;                  /* incrementing to represent number elements popped */
        break;
      }

      if (priv->infer_level > 1 && priv->low_latency_infer) {
        if (inframe->last_parent_buf) {
          GST_LOG_OBJECT (self, "in low latency mode, push current batch");
          idx++;                /* incrementing to represent number elements popped */
          break;
        }
      }
    }

    cur_queued_size = idx;
    total_queued_size += cur_queued_size;

    if (!priv->low_latency_infer && cur_batch_size < priv->infer_batch_size &&
        !priv->is_eos && total_queued_size < priv->max_infer_queue) {
      GST_DEBUG_OBJECT (self, "current batch %d is not enough. "
          "continue to fetch data", cur_batch_size);
      continue;
    }

    GST_LOG_OBJECT (self, "sending batch of %u frames", cur_batch_size);

    if (cur_batch_size && priv->last_fret == GST_FLOW_OK) {
      /* update dynamic json config to kernel */
      // TODO: do we need to protect dyn_json_config using OBJECT_LOCK ?
      kernel->vvas_handle->kernel_dyn_config = priv->dyn_json_config;

      ret = kernel->kernel_start_func (kernel->vvas_handle, 0, kernel->input,
          kernel->output);
      if (ret < 0) {
        GST_ERROR_OBJECT (self, "kernel start failed");
        goto error;
      }

      ret = kernel->kernel_done_func (kernel->vvas_handle);
      if (ret < 0) {
        GST_ERROR_OBJECT (self, "kernel done failed");
        goto error;
      }

      /* signal listeners that we have processed one batch */
      g_signal_emit (self, vvas_signals[SIGNAL_VVAS], 0);
    }

    for (idx = 0; idx < total_queued_size; idx++) {

      if (in_vframes[idx]) {
        gst_video_frame_unmap (in_vframes[idx]);
        g_slice_free1 (sizeof (GstVideoFrame), in_vframes[idx]);
        in_vframes[idx] = NULL;
      }

      if (vvas_frames[idx]) {
        g_slice_free1 (sizeof (VVASFrame), vvas_frames[idx]);
        vvas_frames[idx] = NULL;
      }

      if (child_bufs[idx]) {
        if (priv->infer_level == 1) {
          if (parent_bufs[idx] != child_bufs[idx]) {
            /* parent_buf and child_buf same, then dpu library itself will
             * provide scaled metadata. So scaling is requuired like below
             * when parent_buf != child_buf
             */
            GstInferenceMeta *child_meta;
            Vvas_XInferNodeInfo node_info = { self,
              parent_vinfos[idx], child_vinfos[idx]
            };

            /* child_buf received from PPE, so update metadata in parent buf */
            child_meta =
                (GstInferenceMeta *) gst_buffer_get_meta (child_bufs[idx],
                gst_inference_meta_api_get_type ());
            if (child_meta) {
              if (g_node_n_children (child_meta->prediction->predictions)) {
                GstBuffer *writable_buf = NULL;
                GstInferenceMeta *parent_meta;

                /*scale child prediction to match with parent */
                g_node_children_foreach (child_meta->prediction->predictions,
                    G_TRAVERSE_ALL, update_child_bbox, &node_info);

                if (!gst_buffer_is_writable (parent_bufs[idx])) {
                  GST_DEBUG_OBJECT (self, "create writable buffer of %p",
                      parent_bufs[idx]);
                  writable_buf = gst_buffer_make_writable (parent_bufs[idx]);
                  parent_bufs[idx] = writable_buf;
                }

                parent_meta =
                    (GstInferenceMeta *) gst_buffer_get_meta (parent_bufs[idx],
                    gst_inference_meta_api_get_type ());
                if (!parent_meta) {
                  parent_meta = (GstInferenceMeta *)
                      gst_buffer_add_meta (parent_bufs[idx],
                      gst_inference_meta_get_info (), NULL);
                  if (!parent_meta) {
                    GST_ERROR_OBJECT (self,
                        "failed to add metadata to parent buffer");
                    goto error;
                  }
                  /* assigning childmeta to parent metadata prediction */
                  gst_inference_prediction_unref (parent_meta->prediction);
                  parent_meta->prediction = child_meta->prediction;
                  child_meta->prediction = gst_inference_prediction_new ();

                  parent_meta->prediction->bbox.width =
                      GST_VIDEO_INFO_WIDTH (parent_vinfos[idx]);
                  parent_meta->prediction->bbox.height =
                      GST_VIDEO_INFO_HEIGHT (parent_vinfos[idx]);
                  GST_LOG_OBJECT (self, "add inference metadata to %p",
                      parent_bufs[idx]);
                } else {
                  gst_inference_prediction_unref (child_meta->prediction);
                  child_meta->prediction = gst_inference_prediction_new ();
                }
              }
              if (self->priv->infer_attach_ppebuf) {
                /* remove metadata as child_buf will be attached as sub_buffer */
                gst_buffer_unref (child_bufs[idx]);
                gst_buffer_remove_meta (child_bufs[idx],
                    GST_META_CAST (child_meta));
                child_bufs[idx] = NULL;
              }
            }
          }
        } else {                /* inference level > 1 */
          GstInferenceMeta *child_meta = NULL;
          GstInferencePrediction *parent_prediction = NULL;
          Vvas_XInferNodeInfo node_info = { self,
            parent_vinfos[idx], child_vinfos[idx]
          };

          child_meta =
              (GstInferenceMeta *) gst_buffer_get_meta (child_bufs[idx],
              gst_inference_meta_api_get_type ());
          if (child_meta) {
            parent_prediction = (GstInferencePrediction *)
                child_meta->prediction->predictions->parent->data;

            g_node_children_foreach (child_meta->prediction->predictions,
                G_TRAVERSE_ALL, update_child_bbox, &node_info);
            gst_inference_prediction_unref (parent_prediction);
            child_meta->prediction = gst_inference_prediction_new ();
          }
        }
      }

      if (child_bufs[idx]) {
        gst_buffer_unref (child_bufs[idx]);
        child_bufs[idx] = NULL;
      }
      if (child_vinfos[idx]) {
        gst_video_info_free (child_vinfos[idx]);
        child_vinfos[idx] = NULL;
      }
      if (parent_vinfos[idx]) {
        gst_video_info_free (parent_vinfos[idx]);
        parent_vinfos[idx] = NULL;
      }

      if (push_parent_bufs[idx]) {
        if (priv->last_fret == GST_FLOW_OK) {
#ifdef PRINT_METADATA_TREE
          GstInferenceMeta *parent_meta = NULL;
          gchar *infer_meta_str;
          /* convert metadata to string for debug log */
          parent_meta =
              (GstInferenceMeta *) gst_buffer_get_meta (parent_bufs[idx],
              gst_inference_meta_api_get_type ());
          if (parent_meta) {
            infer_meta_str =
                gst_inference_prediction_to_string (parent_meta->prediction);
            GST_DEBUG_OBJECT (self, "output inference metadata : %s",
                infer_meta_str);
            g_free (infer_meta_str);

            g_node_traverse (parent_meta->prediction->predictions, G_PRE_ORDER,
                G_TRAVERSE_ALL, -1, printf_all_nodes, self);
          }
#endif
          GST_DEBUG_OBJECT (self, "pushing %" GST_PTR_FORMAT, parent_bufs[idx]);

          priv->last_fret = gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (self),
              parent_bufs[idx]);
          if (priv->last_fret < GST_FLOW_OK) {
            switch (priv->last_fret) {
              case GST_FLOW_FLUSHING:
              case GST_FLOW_EOS:
                GST_WARNING_OBJECT (self, "failed to push buffer. reason %s",
                    gst_flow_get_name (priv->last_fret));
                break;
              default:
                GST_ELEMENT_ERROR (self, STREAM, FAILED,
                    ("failed to push buffer."),
                    ("failed to push buffer. reason %s (%d)",
                        gst_flow_get_name (priv->last_fret), priv->last_fret));
                goto exit;
            }
          }
        } else {
          gst_buffer_unref (parent_bufs[idx]);
        }
      }

      if (events[idx]) {
        GST_INFO_OBJECT (self, "pushing event %" GST_PTR_FORMAT, events[idx]);

        if (GST_EVENT_TYPE (events[idx]) == GST_EVENT_EOS)
          sent_eos = TRUE;

        if (!gst_pad_push_event (GST_BASE_TRANSFORM_SRC_PAD (self),
                events[idx])) {
          GST_ERROR_OBJECT (self, "failed to push event %" GST_PTR_FORMAT,
              events[idx]);
          goto error;
        }
      }
    }

    memset (kernel->input, 0x0, sizeof (VVASFrame *) * MAX_NUM_OBJECT);

    if (priv->infer_level > 1 && (batch_len - cur_queued_size > 0)) {
      GST_LOG_OBJECT (self, "processing pending %d inference frames",
          batch_len - cur_queued_size);
      batch_len = batch_len - cur_queued_size;
      /* reset variables for next batch */
      total_queued_size = 0;
      cur_batch_size = 0;
      goto infer_pending;
    }
    /* reset variables for next batch */
    total_queued_size = 0;
    cur_batch_size = 0;

    if (sent_eos)
      goto exit;
  }

error:
  GST_ELEMENT_ERROR (self, STREAM, FAILED,
      ("failed to process frame in inference."),
      ("failed to process frame in inference."));
  priv->last_fret = GST_FLOW_ERROR;

exit:
  if (in_vframes)
    free (in_vframes);
  if (parent_bufs)
    free (parent_bufs);
  if (parent_vinfos)
    free (parent_vinfos);
  if (child_bufs)
    free (child_bufs);
  if (child_vinfos)
    free (child_vinfos);
  if (vvas_frames)
    free (vvas_frames);
  if (events)
    free (events);
  if (push_parent_bufs)
    free (push_parent_bufs);
  priv->infer_thread_state = VVAS_THREAD_EXITED;

  return NULL;
}

static GstCaps *
vvas_kernelcap_to_gst_cap (kernelcaps * kcap)
{
  GValue list = { 0, };
  GValue aval = { 0, };
  uint8_t i;
  GstCaps *cap = gst_caps_new_empty ();
  GstStructure *s;

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

static inline gboolean
vvas_xinfer_send_ppe_frame (GstVvas_XInfer * self, GstBuffer * parent_buf,
    GstVideoInfo * parent_vinfo, GstVideoFrame * in_vframe,
    GstBuffer * child_buf, GstVideoInfo * child_vinfo,
    VVASFrame * vvas_frame, gboolean skip_process, gboolean is_first_parent,
    GstEvent * event)
{
  GstVvas_XInferPrivate *priv = self->priv;

  g_mutex_lock (&priv->ppe_lock);
  if (!priv->ppe_need_data && !priv->stop) {
    /* ppe inbuf is not consumed wait till thread consumes it */
    g_cond_wait (&priv->ppe_need_input, &priv->ppe_lock);
  }

  if (priv->stop) {
    g_mutex_unlock (&priv->ppe_lock);
    return FALSE;
  }
  priv->ppe_frame->parent_buf = parent_buf;
  priv->ppe_frame->parent_vinfo = parent_vinfo;
  priv->ppe_frame->last_parent_buf = is_first_parent;
  priv->ppe_frame->vvas_frame = vvas_frame;
  priv->ppe_frame->in_vframe = in_vframe;
  priv->ppe_frame->child_buf = child_buf;
  priv->ppe_frame->child_vinfo = child_vinfo;
  priv->ppe_frame->event = NULL;
  priv->ppe_frame->skip_processing = skip_process;
  priv->ppe_need_data = FALSE;

  GST_LOG_OBJECT (self, "send frame to ppe loop with skip_processing %d",
      skip_process);
  g_cond_signal (&priv->ppe_has_input);
  g_mutex_unlock (&priv->ppe_lock);
  return TRUE;
}

static GstFlowReturn
gst_vvas_xinfer_submit_input_buffer (GstBaseTransform * trans,
    gboolean is_discont, GstBuffer * inbuf)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  GstVvas_XInferPrivate *priv = self->priv;
  gboolean bret = FALSE;

  GST_LOG_OBJECT (self, "received %" GST_PTR_FORMAT, inbuf);

  g_mutex_lock (&priv->infer_lock);
  if (g_queue_get_length (priv->infer_batch_queue) >=
      (priv->infer_batch_size << 1)) {
    GST_LOG_OBJECT (self, "inference batch queue is full. wait for free space");
    g_cond_wait (&priv->infer_batch_full, &priv->infer_lock);
  }
  g_mutex_unlock (&priv->infer_lock);

  if (priv->stop)
    return GST_FLOW_OK;

  if (priv->do_preprocess) {    /* send frames to PPE thread */
    GstBuffer *new_inbuf = NULL;
    VVASFrame *vvas_frame = NULL;
    GstInferenceMeta *parent_meta = NULL;
    GstBuffer *child_buf = NULL;
    GstVideoInfo *child_vinfo = NULL;

    parent_meta = (GstInferenceMeta *) gst_buffer_get_meta (inbuf,
        gst_inference_meta_api_get_type ());

    if (priv->infer_level == 1) {
      if (!parent_meta
          || !vvas_xinfer_is_sub_buffer_useful (self,
              parent_meta->prediction->sub_buffer)) {
        vvas_frame = g_slice_new0 (VVASFrame);
        /* PPE needs HW buffer as input */
        bret = vvas_xinfer_prepare_ppe_input_frame (self, inbuf,
            priv->in_vinfo, &new_inbuf, vvas_frame);
        if (!bret) {
          return GST_FLOW_ERROR;
        }
        child_buf = new_inbuf == NULL ? gst_buffer_ref (inbuf) : new_inbuf;
        child_vinfo = gst_video_info_copy (priv->in_vinfo);
      }
    } else {
      // TODO: find is it needed to call prepare_ppe_input_frame API as it
      // involves memory copy
      if (parent_meta) {
        Vvas_XInferNumSubs numSubs = { self, FALSE };

        if (priv->infer_level <=
            g_node_max_height (parent_meta->prediction->predictions)) {
          g_node_traverse (parent_meta->prediction->predictions, G_PRE_ORDER,
              G_TRAVERSE_ALL, -1, check_bbox_buffers_availability, &numSubs);
        }

        if (!numSubs.available) {
          vvas_frame = g_slice_new0 (VVASFrame);
          /* PPE needs HW buffer as input */
          bret =
              vvas_xinfer_prepare_ppe_input_frame (self, inbuf, priv->in_vinfo,
              &new_inbuf, vvas_frame);
          if (!bret) {
            return GST_FLOW_ERROR;
          }
        }
      }
      child_buf = new_inbuf == NULL ? gst_buffer_ref (inbuf) : new_inbuf;
      child_vinfo = gst_video_info_copy (priv->in_vinfo);
    }

    bret = vvas_xinfer_send_ppe_frame (self, inbuf,
        gst_video_info_copy (priv->in_vinfo), NULL, child_buf, child_vinfo,
        vvas_frame, FALSE, TRUE, NULL);
    if (!bret) {
      gst_video_info_free (child_vinfo);
      return GST_FLOW_OK;
    }
  } else {                      /* send frames to inference thread directly */
    Vvas_XInferFrame *infer_frame = NULL;
    VVASFrame *vvas_frame = NULL;
    GstInferenceMeta *infer_meta = NULL;
    GstVideoFrame *in_vframe = NULL;

    infer_meta =
        (GstInferenceMeta *) gst_buffer_get_meta (inbuf,
        gst_inference_meta_api_get_type ());

    if (priv->infer_level == 1) {
      GstBuffer *infer_buf = NULL, *child_buf = NULL;
      GstVideoInfo *infer_vinfo = NULL, *child_vinfo = NULL;

      infer_frame = g_slice_new0 (Vvas_XInferFrame);
      in_vframe = g_slice_new0 (GstVideoFrame);
      vvas_frame = g_slice_new0 (VVASFrame);

      if (infer_meta && infer_meta->prediction->sub_buffer &&
          vvas_xinfer_is_sub_buffer_useful (self,
              infer_meta->prediction->sub_buffer)) {
        GstVideoMeta *vmeta = NULL;

        child_vinfo = infer_vinfo = gst_video_info_new ();
        child_buf = infer_buf =
            gst_buffer_ref (infer_meta->prediction->sub_buffer);

        vmeta = gst_buffer_get_video_meta (child_buf);
        gst_video_info_set_format (child_vinfo, vmeta->format,
            vmeta->width, vmeta->height);
      } else {
        child_buf = NULL;       /* inference operates on big buffer, no child_buf */
        child_vinfo = NULL;
        infer_vinfo = priv->in_vinfo;
        infer_buf = inbuf;
      }

      bret = vvas_xinfer_prepare_infer_input_frame (self, infer_buf,
          infer_vinfo, vvas_frame, in_vframe);
      if (!bret) {
        return GST_FLOW_ERROR;
      }

      infer_frame->parent_buf = inbuf;
      infer_frame->parent_vinfo = gst_video_info_copy (priv->in_vinfo);
      infer_frame->last_parent_buf = TRUE;
      infer_frame->child_buf = child_buf;
      infer_frame->child_vinfo = child_vinfo;
      infer_frame->vvas_frame = vvas_frame;
      infer_frame->in_vframe = in_vframe;
      infer_frame->skip_processing = FALSE;
      infer_frame->event = NULL;

      GST_LOG_OBJECT (self, "send frame %p to level-%d inference", infer_frame,
          priv->infer_level);

      /* send input frame to inference thread */
      g_mutex_lock (&priv->infer_lock);
      g_queue_push_tail (priv->infer_batch_queue, infer_frame);
      g_mutex_unlock (&priv->infer_lock);
    } else {
      if (infer_meta) {
        guint sub_bufs_len = 0;
        guint idx;

        GST_LOG_OBJECT (self, "infer_level = %d & meta data depth = %d",
            priv->infer_level,
            g_node_max_height (infer_meta->prediction->predictions));

        if (priv->infer_level <=
            g_node_max_height (infer_meta->prediction->predictions)) {
          g_node_traverse (infer_meta->prediction->predictions, G_PRE_ORDER,
              G_TRAVERSE_ALL, -1, prepare_inference_sub_buffers, self);
        }

        sub_bufs_len = g_queue_get_length (priv->infer_sub_buffers);

        if (sub_bufs_len > 0) {
          GST_DEBUG_OBJECT (self, "input buffer %p has %u sub buffers"
              " in inference level %u", inbuf, sub_bufs_len, priv->infer_level);

          for (idx = 0; idx < sub_bufs_len; idx++) {
            GstVideoMeta *vmeta = NULL;

            infer_frame = g_slice_new0 (Vvas_XInferFrame);
            vvas_frame = g_slice_new0 (VVASFrame);

            infer_frame->parent_buf = inbuf;
            infer_frame->parent_vinfo = gst_video_info_copy (priv->in_vinfo);
            infer_frame->last_parent_buf = (idx == 0) ? TRUE : FALSE;

            infer_frame->vvas_frame = vvas_frame;
            infer_frame->in_vframe = NULL;
            infer_frame->skip_processing = FALSE;
            infer_frame->child_buf =
                gst_buffer_ref (g_queue_pop_head (priv->infer_sub_buffers));
            infer_frame->event = NULL;

            infer_frame->child_vinfo = gst_video_info_new ();
            vmeta = gst_buffer_get_video_meta (infer_frame->child_buf);
            gst_video_info_set_format (infer_frame->child_vinfo, vmeta->format,
                vmeta->width, vmeta->height);

            GST_LOG_OBJECT (self, "send frame %p for level-%d inference",
                infer_frame, priv->infer_level);

            /* send sub-buffers to inference thread */
            g_mutex_lock (&priv->infer_lock);
            g_queue_push_tail (priv->infer_batch_queue, infer_frame);
            g_mutex_unlock (&priv->infer_lock);

          }
        } else {
          /* sub-buffers are not present and preprocess not enabled, hence
           * skip this frame
           */
          infer_frame = g_slice_new0 (Vvas_XInferFrame);
          infer_frame->parent_buf = inbuf;
          infer_frame->parent_vinfo = gst_video_info_copy (priv->in_vinfo);
          infer_frame->last_parent_buf = TRUE;
          infer_frame->vvas_frame = NULL;
          infer_frame->in_vframe = NULL;
          infer_frame->skip_processing = TRUE;
          infer_frame->child_buf = NULL;
          infer_frame->child_vinfo = NULL;
          infer_frame->event = NULL;

          GST_LOG_OBJECT (self, "skip frame %p in level-%d inference",
              infer_frame, priv->infer_level);

          /* send input frame to inference thread */
          g_mutex_lock (&priv->infer_lock);
          g_queue_push_tail (priv->infer_batch_queue, infer_frame);
          g_mutex_unlock (&priv->infer_lock);
        }
      } else {
        /* skip this frame during inference */
        infer_frame = g_slice_new0 (Vvas_XInferFrame);
        infer_frame->parent_buf = inbuf;
        infer_frame->parent_vinfo = gst_video_info_copy (priv->in_vinfo);
        infer_frame->last_parent_buf = TRUE;
        infer_frame->vvas_frame = NULL;
        infer_frame->in_vframe = NULL;
        infer_frame->skip_processing = TRUE;
        infer_frame->child_buf = NULL;
        infer_frame->child_vinfo = NULL;
        infer_frame->event = NULL;

        GST_LOG_OBJECT (self, "skip frame %p in level-%d inference",
            infer_frame, priv->infer_level);

        /* send input frame to inference thread */
        g_mutex_lock (&priv->infer_lock);
        g_queue_push_tail (priv->infer_batch_queue, infer_frame);
        g_mutex_unlock (&priv->infer_lock);
      }
    }

    if (g_queue_get_length (priv->infer_batch_queue) >= priv->infer_batch_size) {
      g_mutex_lock (&priv->infer_lock);
      GST_LOG_OBJECT (self, "signal inference thread as queue size reached %u",
          g_queue_get_length (priv->infer_batch_queue));
      g_cond_signal (&priv->infer_cond);
      g_mutex_unlock (&priv->infer_lock);
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vvas_xinfer_generate_output (GstBaseTransform * trans, GstBuffer ** outbuf)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);

  return self->priv->last_fret;
}

static void
gst_vvas_xinfer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (object);

  switch (prop_id) {
    case PROP_PPE_CONFIG_LOCATION:
      if (GST_STATE (self) != GST_STATE_NULL) {
        g_warning
            ("can't set preprocess json_file path when instance is NOT in NULL state");
        return;
      }
      if (self->ppe_json_file)
        g_free (self->ppe_json_file);

      self->ppe_json_file = g_value_dup_string (value);
      break;
    case PROP_INFER_CONFIG_LOCATION:
      if (GST_STATE (self) != GST_STATE_NULL) {
        g_warning
            ("can't set inference json_file path when instance is NOT in NULL state");
        return;
      }
      if (self->infer_json_file)
        g_free (self->infer_json_file);
      self->infer_json_file = g_value_dup_string (value);
      break;
    case PROP_DYNAMIC_CONFIG:
      self->dyn_config = g_value_dup_string (value);
      if (self->priv->dyn_json_config) {
        json_decref (self->priv->dyn_json_config);
      }
      self->priv->dyn_json_config =
          json_loads (self->dyn_config, JSON_DECODE_ANY, NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_xinfer_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (object);

  switch (prop_id) {
    case PROP_PPE_CONFIG_LOCATION:
      g_value_set_string (value, self->ppe_json_file);
      break;
    case PROP_INFER_CONFIG_LOCATION:
      g_value_set_string (value, self->infer_json_file);
      break;
    case PROP_DYNAMIC_CONFIG:
      g_value_set_string (value, self->dyn_config);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_vvas_xinfer_start (GstBaseTransform * trans)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  GstVvas_XInferPrivate *priv = self->priv;
  gboolean bret = FALSE;

  self->priv = priv;
  priv->ppe_dev_idx = DEFAULT_DEVICE_INDEX;
  priv->in_vinfo = gst_video_info_new ();
  priv->do_init = TRUE;
  priv->dyn_json_config = NULL;
  priv->stop = FALSE;

  if (self->ppe_json_file) {
    bret = vvas_xinfer_read_ppe_config (self);
    if (!bret)
      goto error;
  } else {
    GST_INFO_OBJECT (self, "preprocess config file is not present");
  }

  bret = vvas_xinfer_read_infer_config (self);
  if (!bret)
    goto error;

  if (priv->infer_level > 1 && !self->ppe_json_file) {
    GST_ERROR_OBJECT (self, "PPE is not availble, when inference-level(%d) > 1",
        priv->infer_level);
    goto error;
  }

  if (self->ppe_json_file) {
    priv->ppe_kernel->vvas_handle =
        (VVASKernel *) calloc (1, sizeof (VVASKernel));
    if (!priv->ppe_kernel->vvas_handle) {
      GST_ERROR_OBJECT (self, "failed to allocate memory");
      goto error;
    }

    priv->ppe_kernel->vvas_handle->ert_cmd_buf =
        (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
    if (priv->ppe_kernel->vvas_handle->ert_cmd_buf == NULL) {
      GST_ERROR_OBJECT (self, "failed to allocate ert cmd memory");
      goto error;
    }
    priv->ppe_kernel->vvas_handle->in_mem_bank = DEFAULT_MEM_BANK;
    priv->ppe_kernel->vvas_handle->out_mem_bank = DEFAULT_MEM_BANK;
    memset (priv->ppe_kernel->input, 0x0,
        sizeof (VVASFrame *) * MAX_NUM_OBJECT);
    memset (priv->ppe_kernel->output, 0x0,
        sizeof (VVASFrame *) * MAX_NUM_OBJECT);
  }

  priv->infer_kernel->vvas_handle =
      (VVASKernel *) calloc (1, sizeof (VVASKernel));
  if (!priv->infer_kernel->vvas_handle) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  } else {
    priv->infer_kernel->vvas_handle->in_mem_bank = DEFAULT_MEM_BANK;
    priv->infer_kernel->vvas_handle->out_mem_bank = DEFAULT_MEM_BANK;
  }

  memset (priv->infer_kernel->input, 0x0,
      sizeof (VVASFrame *) * MAX_NUM_OBJECT);
  memset (priv->infer_kernel->output, 0x0,
      sizeof (VVASFrame *) * MAX_NUM_OBJECT);

  if (priv->do_init) {          /*TODO: why it is protected under do_init */

    if (self->ppe_json_file) {
      if(!vvas_xrt_open_device (self->priv->ppe_dev_idx,
                                &self->priv->ppe_dev_handle))
        goto error;

      if (!vvas_xinfer_ppe_init (self))
        goto error;
    }

    if (!vvas_xinfer_infer_init (self))
      goto error;

    priv->do_init = FALSE;
  }

  GST_INFO_OBJECT (self, "start completed");
  return TRUE;

error:
  return FALSE;
}

static gboolean
gst_vvas_xinfer_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  Vvas_XInfer *infer_kernel = self->priv->infer_kernel;
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
      GstCaps *newcap, *allcaps, *filter = NULL;

      if (self->priv->do_init == TRUE)
        return FALSE;

      vvas_handle = infer_kernel->vvas_handle;

      if (!vvas_handle->padinfo ||
          vvas_handle->padinfo->nature == VVAS_PAD_DEFAULT) {
        /* Do default handling */
        GST_DEBUG_OBJECT (self,
            "padinfo == NULL || nature == VVAS_PAD_DEFAULT, Do default handling");
        break;
      }

      gst_query_parse_caps (query, &filter);

      /* Same buffer for sink and src,
       * so the same caps for sink and src pads
       * and for all pads
       */
      kernel_pads = vvas_handle->padinfo->sinkpads;
      nu_pads = (direction == GST_PAD_SRC) ?
          vvas_handle->padinfo->nu_srcpad : vvas_handle->padinfo->nu_sinkpad;

      GST_INFO_OBJECT (self, "nu_pads %d %s",
          nu_pads, direction == GST_PAD_SRC ? "SRC" : "SINK");

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

      if (self->ppe_json_file) {
        GstCaps *templ_caps;
        if (direction == GST_PAD_SRC) {
          templ_caps = gst_pad_get_pad_template_caps (trans->srcpad);
        } else {
          templ_caps = gst_pad_get_pad_template_caps (trans->sinkpad);
        }

        gst_caps_append (allcaps, templ_caps);
      }

      if (filter) {
        GstCaps *tmp = allcaps;
        allcaps =
            gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (tmp);
      }

      {
        gchar *str = gst_caps_to_string (allcaps);
        GST_INFO_OBJECT (self, "returning caps = %s", str);
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
gst_vvas_xinfer_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  GstVvas_XInferPrivate *priv = self->priv;

  GST_LOG_OBJECT (self, "received sink event: %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:{
      GST_INFO_OBJECT (self, "received EOS event");
      priv->is_eos = TRUE;

      if (priv->ppe_thread) {
        g_mutex_lock (&priv->ppe_lock);
        if (!priv->ppe_need_data && !priv->stop) {
          /* ppe inbuf is not consumed wait till thread consumes it */
          g_cond_wait (&priv->ppe_need_input, &priv->ppe_lock);
        }

        if (priv->stop) {
          g_mutex_unlock (&priv->ppe_lock);
          gst_event_unref (event);
          return TRUE;
        }

        memset (priv->ppe_frame, 0x0, sizeof (Vvas_XInferFrame));
        priv->ppe_frame->skip_processing = TRUE;
        priv->ppe_frame->event = event;
        priv->ppe_need_data = FALSE;

        GST_INFO_OBJECT (self, "send event %p to preprocess thread", event);

        g_cond_signal (&priv->ppe_has_input);
        g_mutex_unlock (&priv->ppe_lock);
      } else {
        Vvas_XInferFrame *event_frame = NULL;

        event_frame = g_slice_new0 (Vvas_XInferFrame);
        event_frame->skip_processing = TRUE;
        event_frame->event = event;

        GST_INFO_OBJECT (self, "send event %p to inference thread",
            event_frame);

        g_mutex_lock (&self->priv->infer_lock);
        g_cond_signal (&self->priv->infer_cond);
        GST_INFO_OBJECT (self, "signalled infer thread to exit");
        /* send input frame to inference thread */
        g_queue_push_tail (priv->infer_batch_queue, event_frame);
        g_mutex_unlock (&self->priv->infer_lock);
      }

      if (priv->infer_thread) {
        GST_DEBUG_OBJECT (self, "waiting for inference thread to exit");
        g_thread_join (priv->infer_thread);
        GST_DEBUG_OBJECT (self, "inference thread exited");
        priv->infer_thread = NULL;
      }
      if (priv->ppe_thread) {
        GST_DEBUG_OBJECT (self, "waiting for inference thread to exit");
        g_thread_join (priv->ppe_thread);
        GST_DEBUG_OBJECT (self, "inference thread exited");
        priv->ppe_thread = NULL;
      }
      return TRUE;
    }
    case GST_EVENT_FLUSH_STOP:{
      GST_INFO_OBJECT (self, "freeing internal queues");
      // TODO: handle flush
      //while (g_queue_get_length (self->priv->infer_batch_queue))
      // gst_buffer_unref (g_queue_pop_head (self->priv->infer_batch_queue));
      break;
    }
    default:
      break;
  }
  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static gboolean
gst_vvas_xinfer_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool;
  guint size;

  GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
      decide_query, query);

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstStructure *structure;
    GstAllocator *allocator = NULL;
    GstAllocationParams params = { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0,
      0
    };

    if (gst_query_get_n_allocation_params (query) > 0) {
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    } else {
      if (self->priv->do_preprocess) {
        VVASKernel *vvas_handle = self->priv->ppe_kernel->vvas_handle;
        allocator =
            gst_vvas_allocator_new (self->priv->ppe_dev_idx,
			            USE_DMABUF, vvas_handle->in_mem_bank);
      } else {
        GST_FIXME_OBJECT (self,
            "need to create allocator based on inference device id");
        // TODO: currently not aware of api to get device id, so allocator is NULL (ie. system/default gst allocator)
        allocator = NULL;
      }
      gst_query_add_allocation_param (query, allocator, &params);
    }

    // TODO: once vvas sw libs mentions stride information use that info to configure pool
    if (GST_IS_VVAS_ALLOCATOR (allocator))
      pool = gst_vvas_buffer_pool_new (1, 1);
    else
      pool = gst_video_buffer_pool_new ();

    structure = gst_buffer_pool_get_config (pool);

    // TODO: check whether propose_allocation will be called after set_caps or not */
    gst_buffer_pool_config_set_params (structure, caps, size, self->priv->infer_batch_size + 1, /* one extra for preprocessing */
        0);

    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    if (allocator)
      gst_buffer_pool_config_set_allocator (structure, allocator, &params);

    if (!gst_buffer_pool_set_config (pool, structure))
      goto config_failed;

    GST_OBJECT_LOCK (self);
    gst_query_add_allocation_pool (query, pool, size,
        self->priv->infer_batch_size + 1, 0);
    GST_OBJECT_UNLOCK (self);

    GST_INFO_OBJECT (self, "allocated internal pool %" GST_PTR_FORMAT, pool);

    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

    GST_DEBUG_OBJECT (self, "prepared query %" GST_PTR_FORMAT, query);

    if (allocator)
      gst_object_unref (allocator);
    if (pool)
      gst_object_unref (pool);
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
gst_vvas_xinfer_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  gboolean bret = TRUE;
  GstVvas_XInferPrivate *priv = self->priv;
  kernelpads **kernel_pads = NULL, *kernel_pad = NULL;
  kernelcaps **kcaps = NULL;
  GstCaps *gst_kcaps = NULL;
  gchar *thread_name = NULL;
  GstCaps *newcap;
  GstStructure *structure;
  const gchar *format;

  GST_INFO_OBJECT (self,
      "incaps = %" GST_PTR_FORMAT " and outcaps = %" GST_PTR_FORMAT, incaps,
      outcaps);

  if (!gst_video_info_from_caps (priv->in_vinfo, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse input caps");
    return FALSE;
  }

  kernel_pads = self->priv->infer_kernel->vvas_handle->padinfo->sinkpads;

  kernel_pad = (kernel_pads[0]);        /* plugin works on single pad, hence 0th index */
  kcaps = kernel_pad->kcaps;    /* Base of pad's caps */

  gst_kcaps = gst_caps_new_empty ();
  /* 0th element has high priority */
  newcap = vvas_kernelcap_to_gst_cap (kcaps[0]);
  gst_caps_append (gst_kcaps, newcap);

  structure = gst_caps_get_structure (gst_kcaps, 0);
  bret = gst_structure_get_int (structure, "width", &priv->pref_infer_width);
  bret = gst_structure_get_int (structure, "height", &priv->pref_infer_height);
  priv->pref_infer_format = get_gst_format (kcaps[0]->fmt[0]);
  format = gst_video_format_to_string (priv->pref_infer_format);

  GST_INFO_OBJECT (self,
      "inference preferred caps : width = %d, height = %d, format = %s",
      priv->pref_infer_width, priv->pref_infer_height, format);

  if (self->ppe_json_file) {
    GstCaps *ppe_out_caps = NULL;
    gchar *caps_str = NULL;
    gint width, height;
    GstAllocator *allocator = NULL;
    VVASKernel *vvas_handle = self->priv->ppe_kernel->vvas_handle;
    GstAllocationParams params =
        { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0, 0 };
    gsize size;

    priv->do_preprocess = TRUE;
    GST_INFO_OBJECT (self,
        "ppe configuration is available, enable preprocessing");

    structure = gst_caps_get_structure (gst_kcaps, 0);
    bret = gst_structure_get_int (structure, "width", &width);
    bret = gst_structure_get_int (structure, "height", &height);
    format = gst_video_format_to_string (get_gst_format (kcaps[0]->fmt[0]));

    ppe_out_caps = gst_caps_new_simple ("video/x-raw",
        "width", G_TYPE_INT, width, "height", G_TYPE_INT, height,
        "format", G_TYPE_STRING, format, NULL);

    caps_str = gst_caps_to_string (ppe_out_caps);
    GST_INFO_OBJECT (self, "pre processing output caps %s", caps_str);
    g_free (caps_str);

    if (!gst_video_info_from_caps (priv->ppe_out_vinfo, ppe_out_caps)) {
      GST_ERROR_OBJECT (self, "Failed to parse ppe out caps");
      if (ppe_out_caps)
        gst_caps_unref (ppe_out_caps);
      return FALSE;
    }

    size = GST_VIDEO_INFO_SIZE (priv->ppe_out_vinfo);

    allocator = gst_vvas_allocator_new (self->priv->ppe_dev_idx,
		                        USE_DMABUF, vvas_handle->out_mem_bank);
    params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
    params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;

    priv->ppe_outpool = gst_vvas_buffer_pool_new (1, 1);

    GST_LOG_OBJECT (self, "allocated preprocess output pool %" GST_PTR_FORMAT
        "output allocator %" GST_PTR_FORMAT, priv->ppe_outpool, allocator);

    structure = gst_buffer_pool_get_config (priv->ppe_outpool);

    gst_buffer_pool_config_set_params (structure, ppe_out_caps, size,
        self->priv->infer_batch_size, 0);
    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_set_allocator (structure, allocator, &params);

    if (allocator)
      gst_object_unref (allocator);

    if (ppe_out_caps)
      gst_caps_unref (ppe_out_caps);

    if (!gst_buffer_pool_set_config (priv->ppe_outpool, structure)) {
      GST_ERROR_OBJECT (self, "failed to configure pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to configure pool."),
          ("failed to configure preprocess output pool"));
      return FALSE;
    }

    if (!gst_buffer_pool_set_active (priv->ppe_outpool, TRUE)) {
      GST_ERROR_OBJECT (self, "failed to activate preprocess output pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to activate pool."),
          ("failed to activate preprocess output pool"));
      return FALSE;
    }
  } else {
    const gchar *infer_format = NULL;
    const gchar *in_format = NULL;
    GstStructure *structure;

    /* TODO: loop through different formats accepted by DPU */
    infer_format =
        gst_video_format_to_string (get_gst_format (kcaps[0]->fmt[0]));
    structure = gst_caps_get_structure (incaps, 0);
    in_format = gst_structure_get_string (structure, "format");

    if (g_strcmp0 (infer_format, in_format)) {
      GST_ERROR_OBJECT (self, "input format %s is not acceptable for inference",
          in_format);
      return FALSE;
    }
    priv->do_preprocess = FALSE;
    GST_INFO_OBJECT (self, "software preprocessing will be done by DPU");
  }
#if 0
  if (json_object_set (priv->infer_kernel->lib_config, "need_preprocess",
          json_boolean (priv->do_preprocess))) {
    GST_ERROR_OBJECT (self, "failed to set need-process %d on kernel's config",
        priv->do_preprocess);
    return FALSE;
  }
#endif
  GST_INFO_OBJECT (self, "%s to preprocess inference input frames",
      priv->do_preprocess ? "need" : "not needed");

  priv->ppe_thread_state = VVAS_THREAD_NOT_CREATED;
  priv->infer_thread_state = VVAS_THREAD_NOT_CREATED;

  if (priv->do_preprocess) {
    thread_name = g_strdup_printf ("%s-ppe-thread", GST_ELEMENT_NAME (self));
    priv->ppe_thread = g_thread_new (thread_name, vvas_xinfer_ppe_loop, self);
    g_free (thread_name);
  }

  thread_name = g_strdup_printf ("%s-infer-thread", GST_ELEMENT_NAME (self));
  priv->infer_thread = g_thread_new (thread_name, vvas_xinfer_infer_loop, self);
  g_free (thread_name);

  gst_caps_unref (gst_kcaps);

  return bret;
}

static gboolean
gst_vvas_xinfer_stop (GstBaseTransform * trans)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (trans);
  GST_INFO_OBJECT (self, "stopping");
  self->priv->stop = TRUE;

  gst_video_info_free (self->priv->in_vinfo);

  if (self->priv->infer_thread) {
    g_mutex_lock (&self->priv->infer_lock);
    g_cond_broadcast (&self->priv->infer_cond);
    g_cond_broadcast (&self->priv->infer_batch_full);
    GST_INFO_OBJECT (self, "signalled infer thread to exit");
    g_mutex_unlock (&self->priv->infer_lock);
  }

  if (self->priv->ppe_thread) {
    g_mutex_lock (&self->priv->ppe_lock);
    self->priv->ppe_need_data = FALSE;
    g_cond_broadcast (&self->priv->ppe_has_input);
    g_cond_broadcast (&self->priv->ppe_need_input);
    GST_INFO_OBJECT (self, "signalled ppe thread to exit");
    g_mutex_unlock (&self->priv->ppe_lock);
  }

  if (self->priv->infer_thread) {
    GST_DEBUG_OBJECT (self, "waiting for inference thread to exit");
    g_thread_join (self->priv->infer_thread);
    GST_DEBUG_OBJECT (self, "inference thread exited. sending EOS downstream");
    self->priv->infer_thread = NULL;
  }

  if (self->priv->ppe_thread) {
    GST_DEBUG_OBJECT (self, "waiting for inference thread to exit");
    g_thread_join (self->priv->ppe_thread);
    GST_DEBUG_OBJECT (self, "inference thread exited");
    self->priv->ppe_thread = NULL;
  }

#ifdef DUMP_INFER_INPUT
  if (self->priv->fp)
    fclose (self->priv->fp);
#endif
  if (self->ppe_json_file)
    vvas_xinfer_ppe_deinit (self);

  vvas_xinfer_infer_deinit (self);

  return TRUE;
}

static void
gst_vvas_xinfer_finalize (GObject * obj)
{
  GstVvas_XInfer *self = GST_VVAS_XINFER (obj);

  if (self->priv->input_pool)
    gst_object_unref (self->priv->input_pool);

  if (self->ppe_json_file) {
    g_free (self->ppe_json_file);
    self->ppe_json_file = NULL;
  }

  if (self->infer_json_file) {
    g_free (self->infer_json_file);
    self->infer_json_file = NULL;
  }
}

static void
gst_vvas_xinfer_class_init (GstVvas_XInferClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *transform_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_vvas_xinfer_set_property;
  gobject_class->get_property = gst_vvas_xinfer_get_property;
  gobject_class->finalize = gst_vvas_xinfer_finalize;

  transform_class->start = gst_vvas_xinfer_start;
  transform_class->stop = gst_vvas_xinfer_stop;
  transform_class->set_caps = gst_vvas_xinfer_set_caps;
  transform_class->query = gst_vvas_xinfer_query;
  transform_class->sink_event = gst_vvas_xinfer_sink_event;
  transform_class->propose_allocation = gst_vvas_xinfer_propose_allocation;
  transform_class->submit_input_buffer = gst_vvas_xinfer_submit_input_buffer;
  transform_class->generate_output = gst_vvas_xinfer_generate_output;

  g_object_class_install_property (gobject_class, PROP_PPE_CONFIG_LOCATION,
      g_param_spec_string ("preprocess-config",
          "VVAS Preprocessing library json config file location",
          "Location of the kernels config file in json format", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_INFER_CONFIG_LOCATION,
      g_param_spec_string ("infer-config",
          "VVAS inference library json config file location",
          "Location of the kernels config file in json format", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_DYNAMIC_CONFIG,
      g_param_spec_string ("dynamic-config",
          "Kernel's dynamic json config string",
          "String contains dynamic json configuration of kernel", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

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

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xinfer_debug, "vvas_xinfer", 0,
      "VVAS Inference plugin");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");

  _scale_quark = gst_video_meta_transform_scale_get_quark ();
  _copy_quark = g_quark_from_static_string ("gst-copy");
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_vvas_xinfer_init (GstVvas_XInfer * self)
{
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (self);
  GstVvas_XInferPrivate *priv = GST_VVAS_XINFER_PRIVATE (self);

  self->priv = priv;
  priv->ppe_dev_idx = DEFAULT_DEVICE_INDEX;
  priv->do_init = TRUE;
  priv->dyn_json_config = NULL;
  priv->do_preprocess = FALSE;
  priv->is_error = FALSE;
  priv->infer_batch_queue = g_queue_new ();

  priv->last_fret = GST_FLOW_OK;
#ifdef DUMP_INFER_INPUT
  priv->fp = NULL;
#endif

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (btrans), TRUE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (btrans), TRUE);
}

static gboolean
plugin_init (GstPlugin * vvas_xinfer)
{
  return gst_element_register (vvas_xinfer, "vvas_xinfer", GST_RANK_NONE,
      GST_TYPE_VVAS_XINFER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vvas_xinfer,
    "GStreamer VVAS plug-in for filters",
    plugin_init, "1.0", "MIT/X11",
    "Xilinx VVAS SDK plugin", "http://xilinx.com/")
