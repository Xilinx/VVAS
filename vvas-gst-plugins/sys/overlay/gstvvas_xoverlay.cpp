/*
 * Copyright (C) 2022 Xilinx, Inc.  All rights reserved.
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
#include <gst/gst.h>
#include <gst/vvas/gstvvasallocator.h>
#include <gst/vvas/gstvvasbufferpool.h>
#include <gst/allocators/gstdmabuf.h>
#include <dlfcn.h>              /* for dlXXX APIs */
#include <sys/mman.h>           /* for munmap */
#include <jansson.h>
#include <time.h>
#include <vvas/xrt_utils.h>
#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include <vvas/vvas_kernel.h>
#include "gstvvas_xoverlay.h"
#include <gst/vvas/gstvvasutils.h>
#include <gst/vvas/gstvvasoverlaymeta.h>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#ifdef XLNX_PCIe_PLATFORM
#define DEFAULT_DEVICE_INDEX -1
#define DEFAULT_KERNEL_NAME "boundingbox_accel:{boundingbox_accel_1}"
#define USE_DMABUF 0
#else /* Embedded */
#define DEFAULT_DEVICE_INDEX 0  /* on Embedded only one device i.e. device 0 */
#define DEFAULT_KERNEL_NAME "boundingbox_accel:{boundingbox_accel_1}"
#define  USE_DMABUF 1
#endif
#define MIN_POOL_BUFFERS 2
#define OPT_FLOW_TIMEOUT 2000   // 1 sec
#define ALIGN(size,align) (((size) + (align) - 1) & ~((align) - 1))

#define MAX_BOXES 5
#define BBOX_TIMEOUT 1000
#define DEFAULT_THICKNESS_LEVEL 1

GST_DEBUG_CATEGORY_STATIC (gst_vvas_xoverlay_debug);
#define GST_CAT_DEFAULT gst_vvas_xoverlay_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

typedef struct _GstVvas_XOverlayPrivate GstVvas_XOverlayPrivate;

using namespace cv;

enum
{
  PROP_0,
  PROP_DEVICE_INDEX,
  PROP_XCLBIN_LOCATION,
  PROP_IN_MEM_BANK,
  PROP_DISPLAY_CLOCK,
  PROP_USE_BBOX_ACCEL,
  PROP_CLOCK_FONT_NAME,
  PROP_CLOCK_FONT_COLOR,
  PROP_CLOCK_FONT_SCALE,
  PROP_CLOCK_X_OFFSET,
  PROP_CLOCK_Y_OFFSET,
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{NV12, RGB, BGR, GRAY8}")));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{NV12, RGB, BGR, GRAY8}")));

typedef struct _vvas_bbox_acc_roi
{
  uint32_t nobj;
  xrt_buffer roi;
} vvas_bbox_acc_roi;

struct _GstVvas_XOverlayPrivate
{
  guint dev_idx;
  gchar *kern_name;
  xclDeviceHandle dev_handle;
  vvasKernelHandle kern_handle;
  vvasRunHandle run_handle;
  gchar *xclbin_loc;
  uuid_t xclbinId;
  gint cu_idx;
  guint64 img_p1_phy_addr;
  guint64 img_p2_phy_addr;
  GstVideoInfo *in_vinfo;
  GstBufferPool *in_pool;
  gboolean display_clock;
  gchar clock_time_string[256];
  gboolean use_bbox_accel;
  gint clock_font_name;
  gfloat clock_font_scale;
  guint clock_font_color;
  gint clock_x_offset;
  gint clock_y_offset;
  vvas_bbox_acc_roi roi_data;
  uint32_t stride;
};

#define gst_vvas_xoverlay_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_XOverlay, gst_vvas_xoverlay,
    GST_TYPE_BASE_TRANSFORM);

#define GST_VVAS_XOVERLAY_PRIVATE(self) (GstVvas_XOverlayPrivate *) (gst_vvas_xoverlay_get_instance_private (self))

static void gst_vvas_xoverlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_vvas_xoverlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static uint32_t
xlnx_bbox_align (uint32_t stride_in)
{
  return stride_in / 4;
}

static gboolean
gst_vvas_xoverlay_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVvas_XOverlay *self = GST_VVAS_XOVERLAY (trans);
  gboolean bret = TRUE;
  GstVvas_XOverlayPrivate *priv = self->priv;

  GST_INFO_OBJECT (self,
      "incaps = %" GST_PTR_FORMAT "and outcaps = %" GST_PTR_FORMAT, incaps,
      outcaps);

  if (!gst_video_info_from_caps (priv->in_vinfo, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse input caps");
    return FALSE;
  }

  return bret;
}

static gboolean
vvas_xoverlay_init (GstVvas_XOverlay * self)
{
  GstVvas_XOverlayPrivate *priv = self->priv;
  int iret;

  if (!priv->kern_name)
    priv->kern_name = g_strdup (DEFAULT_KERNEL_NAME);

  if (!priv->kern_name) {
    GST_ERROR_OBJECT (self, "kernel name is not set");
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("kernel name is not set"));
    return FALSE;
  }

  if (!priv->xclbin_loc) {
    GST_ERROR_OBJECT (self, "invalid xclbin location %s", priv->xclbin_loc);
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, (NULL),
        ("xclbin path not set"));
    return FALSE;
  }

  if (vvas_xrt_download_xclbin (priv->xclbin_loc,
          priv->dev_handle, &priv->xclbinId)) {
    GST_ERROR_OBJECT (self, "failed to initialize XRT");
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("xclbin download failed"));
    return FALSE;
  }

  iret = vvas_xrt_open_context (priv->dev_handle, priv->xclbinId,
      &priv->kern_handle, priv->kern_name, true);
  if (iret) {
    GST_ERROR_OBJECT (self, "failed to open XRT context ...:%d", iret);
    return FALSE;
  }

  iret =
      vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
      MAX_BOXES * 5 * sizeof (int), (vvas_bo_flags) XRT_BO_FLAGS_NONE,
      self->in_mem_bank, &self->priv->roi_data.roi);
  if (iret < 0) {
    GST_ERROR_OBJECT (self, "failed to allocate memory for roi info storing..");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vvas_xoverlay_start (GstBaseTransform * trans)
{
  GstVvas_XOverlay *self = GST_VVAS_XOVERLAY (trans);
  GstVvas_XOverlayPrivate *priv = self->priv;

  self->priv = priv;
  priv->in_vinfo = gst_video_info_new ();

  if (self->priv->use_bbox_accel) {
    if (!vvas_xrt_open_device (priv->dev_idx, &priv->dev_handle)) {
      GST_ERROR_OBJECT (self, "failed to open device index %u", priv->dev_idx);
      return FALSE;
    }

    if (!vvas_xoverlay_init (self)) {
      GST_ERROR_OBJECT (self, "unable to initalize opticalflow context");
      return FALSE;
    }
  }

  GST_INFO_OBJECT (self, "start completed");
  return TRUE;
}

static gboolean
vvas_xoverlay_deinit (GstVvas_XOverlay * self)
{
  GstVvas_XOverlayPrivate *priv = self->priv;
  gint cu_idx = -1;

  if (priv->in_pool && gst_buffer_pool_is_active (priv->in_pool)) {
    if (!gst_buffer_pool_set_active (priv->in_pool, FALSE)) {
      GST_ERROR_OBJECT (self, "failed to deactivate internal input pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to deactivate pool."),
          ("failed to deactivate internal input pool"));
      return FALSE;
    }
  }

  if (self->priv->use_bbox_accel) {
    if (priv->kern_name)
      free (priv->kern_name);

    if (priv->xclbin_loc)
      free (priv->xclbin_loc);

    if (priv->roi_data.roi.user_ptr)
      vvas_xrt_free_xrt_buffer (&priv->roi_data.roi);

    if (priv->dev_handle) {
      if (cu_idx >= 0) {
        GST_INFO_OBJECT (self, "closing context for cu_idx %d", cu_idx);
        vvas_xrt_close_context (priv->kern_handle);
      }
      vvas_xrt_close_device (priv->dev_handle);
    }
  }

  return TRUE;
}

static gboolean
gst_vvas_xoverlay_stop (GstBaseTransform * trans)
{
  GstVvas_XOverlay *self = GST_VVAS_XOVERLAY (trans);

  GST_DEBUG_OBJECT (self, "stopping");

  if (self->priv->in_vinfo) {
    gst_video_info_free (self->priv->in_vinfo);
    self->priv->in_vinfo = NULL;
  }

  vvas_xoverlay_deinit (self);

  return TRUE;
}

static void
gst_vvas_xoverlay_finalize (GObject * obj)
{
  GstVvas_XOverlay *self = GST_VVAS_XOVERLAY (obj);

  if (self->priv->in_pool)
    gst_object_unref (self->priv->in_pool);

  self->priv->display_clock = 0;
  self->priv->use_bbox_accel = 0;
}

static gboolean
vvas_xoverlay_allocate_internal_pool (GstVvas_XOverlay * self)
{
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstCaps *caps = NULL;

  caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM (self)->sinkpad);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  pool = gst_video_buffer_pool_new ();
  GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

  allocator = gst_vvas_allocator_new (self->priv->dev_idx, USE_DMABUF,
      self->in_mem_bank, self->priv->kern_handle);
  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
  GST_INFO_OBJECT (self, "allocated %" GST_PTR_FORMAT " allocator", allocator);

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (&info),
      4, 5);

  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config on input pool");
    goto error;
  }

  if (self->priv->in_pool)
    gst_object_unref (self->priv->in_pool);

  self->priv->in_pool = pool;

  GST_INFO_OBJECT (self, "allocated %" GST_PTR_FORMAT " pool",
      self->priv->in_pool);
  gst_caps_unref (caps);

  return TRUE;

error:
  gst_caps_unref (caps);
  return FALSE;
}

static gboolean
vvas_xoverlay_prepare_input_buffer (GstVvas_XOverlay * self, GstBuffer ** inbuf)
{
  GstMemory *in_mem = NULL;
  GstVideoFrame in_vframe, inpool_vframe;
  guint64 phy_addr = -1;
  vvasBOHandle bo_handle = NULL;
  GstVideoMeta *vmeta = NULL;
  gboolean bret;
  GstBuffer *inpool_buf;
  GstFlowReturn fret;
  gboolean use_inpool = FALSE;
  gsize offset = 0;

  memset (&in_vframe, 0x0, sizeof (GstVideoFrame));
  memset (&inpool_vframe, 0x0, sizeof (GstVideoFrame));

  in_mem = gst_buffer_get_memory (*inbuf, 0);
  if (!in_mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    goto error;
  }

  if (gst_is_vvas_memory (in_mem)
      && gst_vvas_memory_can_avoid_copy (in_mem, self->priv->dev_idx,
          self->in_mem_bank)) {
    phy_addr = gst_vvas_allocator_get_paddr (in_mem);
  } else if (gst_is_dmabuf_memory (in_mem)) {
    gint dma_fd = -1;

    dma_fd = gst_dmabuf_memory_get_fd (in_mem);
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

    GST_DEBUG_OBJECT (self, "received dma fd %d and its xrt BO = %p", dma_fd,
        bo_handle);
    phy_addr = vvas_xrt_get_bo_phy_addres (bo_handle);

    if (bo_handle != NULL)
      vvas_xrt_free_bo (bo_handle);

  } else {
    use_inpool = TRUE;
  }

  if (use_inpool) {
    if (!self->priv->in_pool) {
      bret = vvas_xoverlay_allocate_internal_pool (self);
      if (!bret)
        goto error;
    }
    /* acquire buffer from own input pool */
    fret =
        gst_buffer_pool_acquire_buffer (self->priv->in_pool, &inpool_buf, NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
          self->priv->in_pool);
      goto error;
    }
    GST_LOG_OBJECT (self, "acquired buffer %p from own pool", inpool_buf);

    /* map internal buffer in write mode */
    if (!gst_video_frame_map (&inpool_vframe, self->priv->in_vinfo, inpool_buf,
            GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "failed to map internal input buffer");
      goto error;
    }

    /* map input buffer in read mode */
    if (!gst_video_frame_map (&in_vframe, self->priv->in_vinfo, *inbuf,
            GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "failed to map input buffer");
      goto error;
    }
    gst_video_frame_copy (&inpool_vframe, &in_vframe);

    gst_video_frame_unmap (&in_vframe);
    gst_video_frame_unmap (&inpool_vframe);
    gst_buffer_copy_into (inpool_buf, *inbuf,
        (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS |
            GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_METADATA), 0, -1);
    gst_buffer_unref (*inbuf);
    *inbuf = inpool_buf;
  }

  vmeta = gst_buffer_get_video_meta (*inbuf);
  if (vmeta == NULL) {
    GST_ERROR_OBJECT (self, "video meta not present in buffer");
    goto error;
  }

  if (phy_addr == (uint64_t) - 1) {

    if (in_mem)
      gst_memory_unref (in_mem);

    in_mem = gst_buffer_get_memory (*inbuf, 0);
    if (in_mem == NULL) {
      GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
      goto error;
    }
    phy_addr = gst_vvas_allocator_get_paddr (in_mem);
  }
#ifdef XLNX_PCIe_PLATFORM
  /* syncs data when XLNX_SYNC_TO_DEVICE flag is enabled */
  bret = gst_vvas_memory_sync_bo (in_mem);
  if (!bret)
    goto error;
#endif

  self->priv->stride = *(vmeta->stride);
  gst_memory_unref (in_mem);

  GST_LOG_OBJECT (self, "input paddr %p", (void *) phy_addr);

  self->priv->img_p1_phy_addr = phy_addr;

  if (GST_VIDEO_INFO_N_PLANES (self->priv->in_vinfo) == 2) {
    offset = GST_VIDEO_INFO_PLANE_OFFSET (self->priv->in_vinfo, 1);
    self->priv->img_p2_phy_addr = phy_addr + offset;
  }

  return TRUE;

error:
  if (in_vframe.data)
    gst_video_frame_unmap (&in_vframe);
  if (inpool_vframe.data)
    gst_video_frame_unmap (&inpool_vframe);
  if (in_mem)
    gst_memory_unref (in_mem);

  return FALSE;
}

static int32_t
vvas_xoverlay_exec_buf (vvasDeviceHandle dev_handle,
    vvasKernelHandle kern_handle, vvasRunHandle * run_handle,
    const char *format, ...)
{
  va_list args;
  int32_t iret;

  va_start (args, format);

  iret = vvas_xrt_exec_buf (dev_handle, kern_handle, run_handle, format, args);

  va_end (args);

  return iret;
}

static gboolean
vvas_xoverlay_process (GstVvas_XOverlay * self)
{
  GstVvas_XOverlayPrivate *priv = self->priv;
  uint32_t stride;
  int iret;
  GstVideoFormat gst_fmt = GST_VIDEO_INFO_FORMAT (priv->in_vinfo);
  uint32_t height = priv->in_vinfo->height;
  int retry_count = MAX_EXEC_WAIT_RETRY_CNT;

  if (gst_fmt == GST_VIDEO_FORMAT_BGR || gst_fmt == GST_VIDEO_FORMAT_RGB) {
    stride = GST_VIDEO_INFO_PLANE_STRIDE (priv->in_vinfo, 0);
    stride = xlnx_bbox_align (priv->stride);

    iret = vvas_xoverlay_exec_buf (priv->dev_handle, priv->kern_handle,
        &priv->run_handle,
        "ppuuuu", priv->img_p1_phy_addr,
        priv->roi_data.roi.phy_addr,
        height, stride, priv->roi_data.nobj, DEFAULT_THICKNESS_LEVEL);
  } else if (gst_fmt == GST_VIDEO_FORMAT_NV12) {
    stride = priv->stride;
    iret = vvas_xoverlay_exec_buf (priv->dev_handle, priv->kern_handle,
        &priv->run_handle,
        "pppuuuu", priv->img_p1_phy_addr,
        self->priv->img_p2_phy_addr,
        priv->roi_data.roi.phy_addr,
        height, stride, priv->roi_data.nobj, DEFAULT_THICKNESS_LEVEL);
  } else {
    GST_ERROR_OBJECT (self, "format is not supported for bbox acceleration");
    return FALSE;
  }

  if (iret) {
    GST_ERROR_OBJECT (self, "failed to execute command %d", iret);
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("failed to issue execute command. reason : %s", strerror (errno)));
    return FALSE;
  }

  do {
    iret = vvas_xrt_exec_wait (priv->dev_handle, priv->run_handle,
        OPT_FLOW_TIMEOUT);
    if (iret == ERT_CMD_STATE_TIMEOUT) {
      GST_WARNING_OBJECT (self, "Timeout...retry execwait");
      if (retry_count-- <= 0) {
        GST_ERROR_OBJECT (self,
            "Max retry count %d reached..returning error",
            MAX_EXEC_WAIT_RETRY_CNT);
        vvas_xrt_free_run_handle (priv->run_handle);
        return FALSE;
      }
    } else if (iret == ERT_CMD_STATE_ERROR) {
      GST_ERROR_OBJECT (self, "ExecWait ret = %d", iret);
      vvas_xrt_free_run_handle (priv->run_handle);
      return FALSE;
    }
  } while (iret != ERT_CMD_STATE_COMPLETED);

  vvas_xrt_free_run_handle (priv->run_handle);

  return TRUE;
}

/* Get y and uv color components corresponding to givne RGB color */
void
convert_rgb_to_yuv_clrs (VvasColorMetadata clr, unsigned char *y,
    unsigned short *uv)
{
  Mat YUVmat;
  Mat BGRmat (2, 2, CV_8UC3, Scalar (clr.red, clr.green, clr.blue));
  cvtColor (BGRmat, YUVmat, cv::COLOR_BGR2YUV_I420);
  *y = YUVmat.at < uchar > (0, 0);
  *uv = YUVmat.at < uchar > (2, 0) << 8 | YUVmat.at < uchar > (2, 1);
  return;
}

void
vvas_draw_meta_gray (GstVvas_XOverlay * self,
    GstVideoFrame * in_vframe, GstVvasOverlayMeta * overlay_meta)
{
  gint32 idx, mid_x, mid_y;
  guint32 gray_val;
  gint32 thickness, baseline;
  Size textsize;
  guint8 *in_plane1 = NULL;
  guint img_height, img_width;

  int stride, pos_y;
  time_t curtime;
  guint32 v1, v2, v3, val;

  GST_INFO_OBJECT (self, "Overlaying on Gray image");

  img_height = GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo);
  img_width = GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo);
  in_plane1 = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (in_vframe, 0);
  stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_vframe, 0);

  Mat img (img_height, img_width, CV_8UC1, in_plane1, stride);

  if (self->priv->display_clock) {
    time (&curtime);
    sprintf (self->priv->clock_time_string, "%s", ctime (&curtime));

    val = self->priv->clock_font_color;
    val = val >> 8;
    v3 = val & 0xff;
    val = val >> 8;
    v2 = val & 0xff;
    val = val >> 8;
    v1 = val & 0xff;

    gray_val = (v1 + v2 + v3) / 3;

    putText (img, self->priv->clock_time_string,
        Point (self->priv->clock_x_offset, self->priv->clock_y_offset),
        self->priv->clock_font_name, self->priv->clock_font_scale,
        Scalar (gray_val), 1, 1);
  }
  //Drawing rectangles
  if (overlay_meta->num_rects && !self->priv->use_bbox_accel) {
    vvas_rect_params rect;
    for (idx = 0; idx < overlay_meta->num_rects; idx++) {
      rect = overlay_meta->rects[idx];
      if (rect.apply_bg_color) {
        thickness = FILLED;
        gray_val = (rect.bg_color.red +
            rect.bg_color.green + rect.bg_color.blue) / 3;
      } else {
        thickness = rect.thickness;
        gray_val = (rect.rect_color.red +
            rect.rect_color.green + rect.rect_color.blue) / 3;
      }
      rectangle (img, Rect (Point (rect.offset.x,
                  rect.offset.y), Size (rect.width, rect.height)),
          Scalar (gray_val), thickness, 1, 0);
    }
  }
  //Drawing text
  if (overlay_meta->num_text) {
    vvas_text_params text_info;

    for (idx = 0; idx < overlay_meta->num_text; idx++) {
      text_info = overlay_meta->text[idx];

      baseline = 0;
      textsize = getTextSize (text_info.disp_text,
          text_info.text_font.font_num,
          text_info.text_font.font_size, 1, &baseline);

      if (text_info.bottom_left_origin)
        pos_y = text_info.offset.y - textsize.height;
      else
        pos_y = text_info.offset.y;

      if (text_info.apply_bg_color) {
        gray_val = (text_info.bg_color.red +
            text_info.bg_color.green + text_info.bg_color.blue) / 3;

        rectangle (img, Rect (Point (text_info.offset.x,
                    pos_y), textsize), Scalar (gray_val), FILLED, 1, 0);
      }

      gray_val = (text_info.text_font.font_color.red +
          text_info.text_font.font_color.green +
          text_info.text_font.font_color.blue) / 3;

      if (!text_info.bottom_left_origin)
        pos_y = text_info.offset.y + textsize.height + text_info.text_font.font_size - 1;
      else
        pos_y = text_info.offset.y - text_info.text_font.font_size - 1;

      putText (img, text_info.disp_text, Point (text_info.offset.x,
              pos_y), text_info.text_font.font_num,
          text_info.text_font.font_size, Scalar (gray_val), 1, 1);
    }
  }
  //Drawing lines
  if (overlay_meta->num_lines) {
    vvas_line_params line_info;
    for (idx = 0; idx < overlay_meta->num_lines; idx++) {
      line_info = overlay_meta->lines[idx];
      gray_val = (line_info.line_color.red +
          line_info.line_color.green + line_info.line_color.blue) / 3;
      line (img, Point (line_info.start_pt.x,
              line_info.start_pt.y), Point (line_info.end_pt.x,
              line_info.end_pt.y),
          Scalar (gray_val), line_info.thickness, 1, 0);
    }
  }
  //Drawing arrows
  if (overlay_meta->num_arrows) {
    vvas_arrow_params arrow_info;
    for (idx = 0; idx < overlay_meta->num_arrows; idx++) {
      arrow_info = overlay_meta->arrows[idx];
      gray_val = (arrow_info.line_color.red +
          arrow_info.line_color.green + arrow_info.line_color.blue) / 3;

      thickness = arrow_info.thickness;
      switch (arrow_info.arrow_direction) {
        case AT_START:
          arrowedLine (img, Point (arrow_info.end_pt.x,
                  arrow_info.end_pt.y), Point (arrow_info.start_pt.x,
                  arrow_info.start_pt.y), Scalar (gray_val),
              thickness, 1, 0, arrow_info.tipLength);
          break;
        case AT_END:
          arrowedLine (img, Point (arrow_info.start_pt.x,
                  arrow_info.start_pt.y), Point (arrow_info.end_pt.x,
                  arrow_info.end_pt.y), Scalar (gray_val),
              thickness, 1, 0, arrow_info.tipLength);
          break;
        case BOTH_ENDS:
          if (arrow_info.end_pt.x >= arrow_info.start_pt.x)
            mid_x = arrow_info.start_pt.x + (arrow_info.end_pt.x -
                arrow_info.start_pt.x) / 2;
          else
            mid_x = arrow_info.end_pt.x + (arrow_info.start_pt.x -
                arrow_info.end_pt.x) / 2;

          if (arrow_info.end_pt.y >= arrow_info.start_pt.y)
            mid_y = arrow_info.start_pt.y + (arrow_info.end_pt.y -
                arrow_info.start_pt.y) / 2;
          else
            mid_y = arrow_info.end_pt.y + (arrow_info.start_pt.y -
                arrow_info.end_pt.y) / 2;

          arrowedLine (img, Point (mid_x, mid_y),
              Point (arrow_info.end_pt.x, arrow_info.end_pt.y),
              Scalar (gray_val), thickness, 1, 0, arrow_info.tipLength / 2);

          arrowedLine (img, Point (mid_x, mid_y),
              Point (arrow_info.start_pt.x, arrow_info.start_pt.y),
              Scalar (gray_val), thickness, 1, 0, arrow_info.tipLength / 2);
          break;
        default:
          GST_ERROR_OBJECT (overlay_meta, "Arrow type is not supported");
          break;
      }
    }
  }
  //Drawing cicles
  if (overlay_meta->num_circles) {
    vvas_circle_params circle_info;
    for (idx = 0; idx < overlay_meta->num_circles; idx++) {
      circle_info = overlay_meta->circles[idx];
      gray_val = (circle_info.circle_color.red +
          circle_info.circle_color.green + circle_info.circle_color.blue) / 3;
      circle (img, Point (circle_info.center_pt.x,
              circle_info.center_pt.y), circle_info.radius,
          Scalar (gray_val), circle_info.thickness, 1, 0);
    }
  }
  //Drawing polygons
  if (overlay_meta->num_polys) {
    vvas_polygon_params poly_info;
    std::vector < Point > poly_pts;
    gint32 np;
    const Point *pts;
    for (idx = 0; idx < overlay_meta->num_polys; idx++) {
      poly_info = overlay_meta->polygons[idx];
      gray_val = (poly_info.poly_color.red +
          poly_info.poly_color.green + poly_info.poly_color.blue) / 3;

      poly_pts.clear ();
      for (np = 0; np < poly_info.num_pts; np++)
        poly_pts.push_back (Point (poly_info.poly_pts[np].x,
                poly_info.poly_pts[np].y));

      pts = (const Point *) Mat (poly_pts).data;
      polylines (img, &pts, &poly_info.num_pts, 1, true,
          Scalar (gray_val), poly_info.thickness, 1, 0);
    }
  }
}

void
vvas_draw_meta_rgb (GstVvas_XOverlay * self,
    GstVideoFrame * in_vframe, GstVvasOverlayMeta * overlay_meta)
{
  gint32 idx;
  gint32 thickness;
  VvasColorMetadata ol_color;
  gint32 mid_x, mid_y;
  gint32 baseline;
  Size textsize;
  guint8 *in_plane1 = NULL;
  guint img_height, img_width;
  GstVideoFormat video_fmt;

  int stride, pos_y;
  time_t curtime;
  guint32 v1, v2, v3, val;

  GST_INFO_OBJECT (self, "Overlaying on RGB or BGR image");

  video_fmt = GST_VIDEO_INFO_FORMAT (self->priv->in_vinfo);
  img_height = GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo);
  img_width = GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo);
  in_plane1 = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (in_vframe, 0);
  stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_vframe, 0);
  Mat img (img_height, img_width, CV_8UC3, in_plane1, stride);

  if (self->priv->display_clock) {
    time (&curtime);
    sprintf (self->priv->clock_time_string, "%s", ctime (&curtime));

    val = self->priv->clock_font_color;
    val = val >> 8;

    v3 = val & 0xff;
    val = val >> 8;
    v2 = val & 0xff;
    val = val >> 8;
    v1 = val & 0xff;

    putText (img, self->priv->clock_time_string,
        Point (self->priv->clock_x_offset, self->priv->clock_y_offset),
        self->priv->clock_font_name, self->priv->clock_font_scale, Scalar (v1,
            v2, v3), 1, 1);
  }
  //Drawing rectangles
  if (overlay_meta->num_rects && !self->priv->use_bbox_accel) {
    vvas_rect_params rect;
    for (idx = 0; idx < overlay_meta->num_rects; idx++) {
      rect = overlay_meta->rects[idx];
      if (rect.apply_bg_color) {
        thickness = FILLED;
        ol_color = rect.bg_color;
      } else {
        thickness = rect.thickness;
        ol_color = rect.rect_color;
      }

      if (GST_VIDEO_FORMAT_BGR == video_fmt) {
        v1 = ol_color.blue;
        v2 = ol_color.green;
        v3 = ol_color.red;
      } else {
        v1 = ol_color.red;
        v2 = ol_color.green;
        v3 = ol_color.blue;
      }

      rectangle (img, Rect (Point (rect.offset.x,
                  rect.offset.y), Size (rect.width, rect.height)),
          Scalar (v1, v2, v3), thickness, 1, 0);
    }
  }
  //Drawing text
  if (overlay_meta->num_text) {
    vvas_text_params text_info;

    for (idx = 0; idx < overlay_meta->num_text; idx++) {
      text_info = overlay_meta->text[idx];
      baseline = 0;
      textsize = getTextSize (text_info.disp_text,
          text_info.text_font.font_num,
          text_info.text_font.font_size, 1, &baseline);

      if (text_info.bottom_left_origin)
        pos_y = text_info.offset.y - textsize.height;
      else
        pos_y = text_info.offset.y;

      if (text_info.apply_bg_color) {
        ol_color = text_info.bg_color;

        if (GST_VIDEO_FORMAT_BGR == video_fmt) {
          v1 = ol_color.blue;
          v2 = ol_color.green;
          v3 = ol_color.red;
        } else {
          v1 = ol_color.red;
          v2 = ol_color.green;
          v3 = ol_color.blue;
        }

        rectangle (img, Rect (Point (text_info.offset.x,
                    pos_y), textsize), Scalar (v1, v2, v3), FILLED, 1, 0);
      }

      ol_color = text_info.text_font.font_color;

      if (GST_VIDEO_FORMAT_BGR == video_fmt) {
        v1 = ol_color.blue;
        v2 = ol_color.green;
        v3 = ol_color.red;
      } else {
        v1 = ol_color.red;
        v2 = ol_color.green;
        v3 = ol_color.blue;
      }

      if (!text_info.bottom_left_origin)
        pos_y = text_info.offset.y + textsize.height + text_info.text_font.font_size - 1;
      else
        pos_y = text_info.offset.y - text_info.text_font.font_size - 1;

      putText (img, text_info.disp_text, Point (text_info.offset.x,
              pos_y), text_info.text_font.font_num,
          text_info.text_font.font_size, Scalar (v1, v2, v3), 1, 1);
    }
  }
  //Drawing lines
  if (overlay_meta->num_lines) {
    vvas_line_params line_info;
    for (idx = 0; idx < overlay_meta->num_lines; idx++) {
      line_info = overlay_meta->lines[idx];
      ol_color = line_info.line_color;

      if (GST_VIDEO_FORMAT_BGR == video_fmt) {
        v1 = ol_color.blue;
        v2 = ol_color.green;
        v3 = ol_color.red;
      } else {
        v1 = ol_color.red;
        v2 = ol_color.green;
        v3 = ol_color.blue;
      }
      line (img, Point (line_info.start_pt.x,
              line_info.start_pt.y), Point (line_info.end_pt.x,
              line_info.end_pt.y),
          Scalar (v1, v2, v3), line_info.thickness, 1, 0);
    }
  }
  //Drawing arrows
  if (overlay_meta->num_arrows) {
    vvas_arrow_params arrow_info;
    for (idx = 0; idx < overlay_meta->num_arrows; idx++) {
      arrow_info = overlay_meta->arrows[idx];

      ol_color = arrow_info.line_color;

      if (GST_VIDEO_FORMAT_BGR == video_fmt) {
        v1 = ol_color.blue;
        v2 = ol_color.green;
        v3 = ol_color.red;
      } else {
        v1 = ol_color.red;
        v2 = ol_color.green;
        v3 = ol_color.blue;
      }
      thickness = arrow_info.thickness;
      switch (arrow_info.arrow_direction) {
        case AT_START:
          arrowedLine (img, Point (arrow_info.end_pt.x,
                  arrow_info.end_pt.y), Point (arrow_info.start_pt.x,
                  arrow_info.start_pt.y), Scalar (v1, v2, v3),
              thickness, 1, 0, arrow_info.tipLength);
          break;
        case AT_END:
          arrowedLine (img, Point (arrow_info.start_pt.x,
                  arrow_info.start_pt.y), Point (arrow_info.end_pt.x,
                  arrow_info.end_pt.y), Scalar (v1, v2, v3),
              thickness, 1, 0, arrow_info.tipLength);
          break;
        case BOTH_ENDS:
          if (arrow_info.end_pt.x >= arrow_info.start_pt.x)
            mid_x = arrow_info.start_pt.x + (arrow_info.end_pt.x -
                arrow_info.start_pt.x) / 2;
          else
            mid_x = arrow_info.end_pt.x + (arrow_info.start_pt.x -
                arrow_info.end_pt.x) / 2;

          if (arrow_info.end_pt.y >= arrow_info.start_pt.y)
            mid_y = arrow_info.start_pt.y + (arrow_info.end_pt.y -
                arrow_info.start_pt.y) / 2;
          else
            mid_y = arrow_info.end_pt.y + (arrow_info.start_pt.y -
                arrow_info.end_pt.y) / 2;

          arrowedLine (img, Point (mid_x, mid_y),
              Point (arrow_info.end_pt.x, arrow_info.end_pt.y),
              Scalar (v1, v2, v3), thickness, 1, 0, arrow_info.tipLength / 2);

          arrowedLine (img, Point (mid_x, mid_y),
              Point (arrow_info.start_pt.x, arrow_info.start_pt.y),
              Scalar (v1, v2, v3), thickness, 1, 0, arrow_info.tipLength / 2);
          break;
        default:
          GST_ERROR_OBJECT (overlay_meta, "Arrow type is not supported");
          break;
      }
    }
  }
  //Drawing cicles
  if (overlay_meta->num_circles) {
    vvas_circle_params circle_info;
    for (idx = 0; idx < overlay_meta->num_circles; idx++) {
      circle_info = overlay_meta->circles[idx];
      ol_color = circle_info.circle_color;

      if (GST_VIDEO_FORMAT_BGR == video_fmt) {
        v1 = ol_color.blue;
        v2 = ol_color.green;
        v3 = ol_color.red;
      } else {
        v1 = ol_color.red;
        v2 = ol_color.green;
        v3 = ol_color.blue;
      }

      circle (img, Point (circle_info.center_pt.x,
              circle_info.center_pt.y), circle_info.radius,
          Scalar (v1, v2, v3), circle_info.thickness, 1, 0);
    }
  }
  //Drawing polygons
  if (overlay_meta->num_polys) {
    vvas_polygon_params poly_info;
    std::vector < Point > poly_pts;
    gint32 np;
    const Point *pts;
    for (idx = 0; idx < overlay_meta->num_polys; idx++) {
      poly_info = overlay_meta->polygons[idx];
      ol_color = poly_info.poly_color;

      if (GST_VIDEO_FORMAT_BGR == video_fmt) {
        v1 = ol_color.blue;
        v2 = ol_color.green;
        v3 = ol_color.red;
      } else {
        v1 = ol_color.red;
        v2 = ol_color.green;
        v3 = ol_color.blue;
      }

      poly_pts.clear ();
      for (np = 0; np < poly_info.num_pts; np++)
        poly_pts.push_back (Point (poly_info.poly_pts[np].x,
                poly_info.poly_pts[np].y));

      pts = (const Point *) Mat (poly_pts).data;
      polylines (img, &pts, &poly_info.num_pts, 1, true,
          Scalar (v1, v2, v3), poly_info.thickness, 1, 0);
    }
  }
}

void
vvas_draw_meta_nv12 (GstVvas_XOverlay * self,
    GstVideoFrame * in_vframe, GstVvasOverlayMeta * overlay_meta)
{
  gint32 idx;
  guint8 yScalar;
  guint16 uvScalar;
  gint32 xmin, ymin, xmax, ymax;
  int thickness;
  gint32 mid_x, mid_y;
  gint32 radius;
  float tiplength;
  gint32 baseline;
  Size textsize;
  guint8 *in_plane1 = NULL, *in_plane2 = NULL;
  guint img_height, img_width;
  int stride, pos_y;
  time_t curtime;
  guint32 v1, v2, v3, val;
  VvasColorMetadata clr;

  GST_INFO_OBJECT (self, "Overlaying on NV12 format image");

  img_height = GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo);
  img_width = GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo);
  in_plane1 = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (in_vframe, 0);
  stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_vframe, 0);

  Mat img_y (img_height, img_width, CV_8UC1, in_plane1, stride);

  in_plane2 = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (in_vframe, 1);
  stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_vframe, 1);
  Mat img_uv (img_height / 2, img_width / 2, CV_16UC1, in_plane2, stride);

  if (self->priv->display_clock) {
    time (&curtime);
    sprintf (self->priv->clock_time_string, "%s", ctime (&curtime));

    val = self->priv->clock_font_color;
    //v4 = val & 0xff;
    val = val >> 8;
    v3 = val & 0xff;
    val = val >> 8;
    v2 = val & 0xff;
    val = val >> 8;
    v1 = val & 0xff;

    clr.red = v1;
    clr.green = v2;
    clr.blue = v3;

    convert_rgb_to_yuv_clrs (clr, &yScalar, &uvScalar);

    xmin = floor (self->priv->clock_x_offset / 2) * 2;
    ymin = floor (self->priv->clock_y_offset / 2) * 2;

    putText (img_y, self->priv->clock_time_string, Point (xmin, ymin),
        self->priv->clock_font_name, self->priv->clock_font_scale,
        Scalar (yScalar), 1, 1);

    putText (img_uv, self->priv->clock_time_string, Point (xmin / 2, ymin / 2),
        self->priv->clock_font_name, self->priv->clock_font_scale / 2,
        Scalar (uvScalar), 1, 1);
  }
  //Drawing rectangles
  if (overlay_meta->num_rects && !self->priv->use_bbox_accel) {
    vvas_rect_params rect;
    for (idx = 0; idx < overlay_meta->num_rects; idx++) {
      rect = overlay_meta->rects[idx];
      xmin = floor (rect.offset.x / 2) * 2;
      ymin = floor (rect.offset.y / 2) * 2;
      xmax = floor ((rect.width + rect.offset.x) / 2) * 2;
      ymax = floor ((rect.height + rect.offset.y) / 2) * 2;
      if (rect.apply_bg_color) {
        thickness = FILLED;
        convert_rgb_to_yuv_clrs (rect.bg_color, &yScalar, &uvScalar);
      } else {
        thickness = rect.thickness;
        convert_rgb_to_yuv_clrs (rect.rect_color, &yScalar, &uvScalar);
      }

      rectangle (img_y, Rect (Point (xmin, ymin),
              Point (xmax, ymax)), Scalar (yScalar), thickness, 1, 0);

      rectangle (img_uv, Rect (Point (xmin / 2, ymin / 2),
              Point (xmax / 2, ymax / 2)), Scalar (uvScalar), thickness, 1, 0);
    }
  }
  //Drawing text
  if (overlay_meta->num_text) {
    vvas_text_params text_info;
    for (idx = 0; idx < overlay_meta->num_text; idx++) {
      text_info = overlay_meta->text[idx];
      xmin = floor (text_info.offset.x / 2) * 2;
      ymin = floor (text_info.offset.y / 2) * 2;

      baseline = 0;
      textsize = getTextSize (text_info.disp_text,
          text_info.text_font.font_num,
          text_info.text_font.font_size, 1, &baseline);

      if (text_info.bottom_left_origin) {
        pos_y = ymin - textsize.height;
        pos_y = floor (pos_y / 2) * 2;
      } else
        pos_y = ymin;

      if (text_info.apply_bg_color) {
        convert_rgb_to_yuv_clrs (text_info.bg_color, &yScalar, &uvScalar);
        rectangle (img_y, Rect (Point (xmin,
                    pos_y), textsize), Scalar (yScalar), FILLED, 1, 0);

        textsize.height /= 2;
        textsize.width /= 2;
        rectangle (img_uv, Rect (Point (xmin / 2,
                    pos_y / 2), textsize), Scalar (uvScalar), FILLED, 1, 0);
      }

      if (!text_info.bottom_left_origin)
        pos_y = ymin + textsize.height + text_info.text_font.font_size - 1;
      else
        pos_y = ymin - text_info.text_font.font_size - 1;

      pos_y = floor (pos_y / 2) * 2;

      convert_rgb_to_yuv_clrs (text_info.text_font.font_color, &yScalar,
          &uvScalar);
      putText (img_y, text_info.disp_text, Point (xmin, pos_y),
          text_info.text_font.font_num, text_info.text_font.font_size,
          Scalar (yScalar), 1, 1);

      putText (img_uv, text_info.disp_text, Point (xmin / 2, pos_y / 2),
          text_info.text_font.font_num, text_info.text_font.font_size / 2,
          Scalar (uvScalar), 1, 1);
    }
  }
  //Drawing lines
  if (overlay_meta->num_lines) {
    vvas_line_params line_info;
    for (idx = 0; idx < overlay_meta->num_lines; idx++) {
      line_info = overlay_meta->lines[idx];
      convert_rgb_to_yuv_clrs (line_info.line_color, &yScalar, &uvScalar);
      xmin = floor (line_info.start_pt.x / 2) * 2;
      ymin = floor (line_info.start_pt.y / 2) * 2;
      xmax = floor (line_info.end_pt.x / 2) * 2;
      ymax = floor (line_info.end_pt.y / 2) * 2;

      line (img_y, Point (xmin, ymin), Point (xmax, ymax),
          Scalar (yScalar), line_info.thickness, 1, 0);

      line (img_uv, Point (xmin / 2, ymin / 2), Point (xmax / 2, ymax / 2),
          Scalar (uvScalar), line_info.thickness, 1, 0);
    }
  }
  //Drawing arrows
  if (overlay_meta->num_arrows) {
    vvas_arrow_params arrow_info;
    for (idx = 0; idx < overlay_meta->num_arrows; idx++) {
      arrow_info = overlay_meta->arrows[idx];
      convert_rgb_to_yuv_clrs (arrow_info.line_color, &yScalar, &uvScalar);
      xmin = floor (arrow_info.start_pt.x / 2) * 2;
      ymin = floor (arrow_info.start_pt.y / 2) * 2;
      xmax = floor (arrow_info.end_pt.x / 2) * 2;
      ymax = floor (arrow_info.end_pt.y / 2) * 2;
      tiplength = arrow_info.tipLength; //floor(arrow_info.tipLength / 2) * 2;

      thickness = arrow_info.thickness;
      switch (arrow_info.arrow_direction) {
        case AT_START:
          arrowedLine (img_y, Point (xmax, ymax), Point (xmin, ymin),
              Scalar (yScalar), thickness, 1, 0, tiplength);

          arrowedLine (img_uv, Point (xmax / 2, ymax / 2), Point (xmin / 2,
                  ymin / 2), Scalar (uvScalar), thickness, 1, 0, tiplength);
          break;
        case AT_END:
          arrowedLine (img_y, Point (xmin, ymin), Point (xmax, ymax),
              Scalar (yScalar), thickness, 1, 0, tiplength);

          arrowedLine (img_uv, Point (xmin / 2, ymin / 2), Point (xmax / 2,
                  ymax / 2), Scalar (uvScalar), thickness, 1, 0, tiplength);
          break;
        case BOTH_ENDS:
          if (xmax >= xmin)
            mid_x = floor ((xmin + (xmax - xmin) / 2) / 2) * 2;
          else
            mid_x = floor ((xmax + (xmin - xmax) / 2) / 2) * 2;

          if (ymax >= ymin)
            mid_y = floor ((ymin + (ymax - ymin) / 2) / 2) * 2;
          else
            mid_y = floor ((ymax + (ymin - ymax) / 2) / 2) * 2;

          arrowedLine (img_y, Point (mid_x, mid_y),
              Point (xmax, ymax), Scalar (yScalar),
              thickness, 1, 0, tiplength / 2);

          arrowedLine (img_y, Point (mid_x, mid_y),
              Point (xmin, ymin), Scalar (yScalar),
              thickness, 1, 0, tiplength / 2);

          arrowedLine (img_uv, Point (mid_x / 2, mid_y / 2),
              Point (xmax / 2, ymax / 2), Scalar (uvScalar),
              thickness, 1, 0, tiplength / 2);

          arrowedLine (img_uv, Point (mid_x / 2, mid_y / 2),
              Point (xmin / 2, ymin / 2), Scalar (uvScalar),
              thickness, 1, 0, tiplength / 2);
          break;
        default:
          GST_ERROR_OBJECT (overlay_meta, "Arrow type is not supported");
          break;
      }
    }
  }
  //Drawing cicles
  if (overlay_meta->num_circles) {
    vvas_circle_params circle_info;
    for (idx = 0; idx < overlay_meta->num_circles; idx++) {
      circle_info = overlay_meta->circles[idx];
      convert_rgb_to_yuv_clrs (circle_info.circle_color, &yScalar, &uvScalar);
      xmin = floor (circle_info.center_pt.x / 2) * 2;
      ymin = floor (circle_info.center_pt.y / 2) * 2;
      radius = floor (circle_info.radius / 2) * 2;

      circle (img_y, Point (xmin, ymin), radius,
          Scalar (yScalar), circle_info.thickness, 1, 0);

      circle (img_uv, Point (xmin / 2, ymin / 2), radius / 2,
          Scalar (uvScalar), circle_info.thickness, 1, 0);
    }
  }
  //Drawing polygons
  if (overlay_meta->num_polys) {
    vvas_polygon_params poly_info;
    std::vector < Point > poly_pts_y;
    std::vector < Point > poly_pts_uv;
    gint32 np;
    const Point *pts;
    for (idx = 0; idx < overlay_meta->num_polys; idx++) {
      poly_info = overlay_meta->polygons[idx];

      convert_rgb_to_yuv_clrs (poly_info.poly_color, &yScalar, &uvScalar);

      poly_pts_y.clear ();
      poly_pts_uv.clear ();
      for (np = 0; np < poly_info.num_pts; np++) {
        xmin = floor (poly_info.poly_pts[np].x / 2) * 2;
        ymin = floor (poly_info.poly_pts[np].y / 2) * 2;
        poly_pts_y.push_back (Point (xmin, ymin));
        poly_pts_uv.push_back (Point (xmin / 2, ymin / 2));
      }

      pts = (const Point *) Mat (poly_pts_y).data;
      polylines (img_y, &pts, &poly_info.num_pts, 1, true,
          Scalar (yScalar), poly_info.thickness, 1, 0);

      pts = (const Point *) Mat (poly_pts_uv).data;
      polylines (img_uv, &pts, &poly_info.num_pts, 1, true,
          Scalar (uvScalar), poly_info.thickness, 1, 0);
    }
  }
}

void
prepare_bbox_data (GstVvas_XOverlay * self,
    GstVvasOverlayMeta * overlay_meta, GstVideoFormat gst_fmt, gint32 str_idx)
{
  gint32 idx, end_idx, loop;
  GstVvas_XOverlayPrivate *priv = self->priv;
  gint32 *roi = (gint32 *) priv->roi_data.roi.user_ptr;

  if ((str_idx + MAX_BOXES) > overlay_meta->num_rects)
    end_idx = overlay_meta->num_rects;
  else
    end_idx = str_idx + MAX_BOXES;

  priv->roi_data.nobj = 0;
  idx = 0;
  for (loop = str_idx; loop < end_idx; loop++) {
    roi[priv->roi_data.nobj * 5] = overlay_meta->rects[idx].offset.x;
    roi[(priv->roi_data.nobj * 5) + 1] = overlay_meta->rects[idx].offset.y;
    roi[(priv->roi_data.nobj * 5) + 2] = overlay_meta->rects[idx].width;
    roi[(priv->roi_data.nobj * 5) + 3] = overlay_meta->rects[idx].height;

    if (gst_fmt == GST_VIDEO_FORMAT_RGB) {
      roi[(priv->roi_data.nobj * 5) + 4] =
          (overlay_meta->rects[idx].rect_color.red & 0xFF) << 24;
      roi[(priv->roi_data.nobj * 5) + 4] |=
          (overlay_meta->rects[idx].rect_color.green & 0xFF) << 16;
      roi[(priv->roi_data.nobj * 5) + 4] |=
          (overlay_meta->rects[idx].rect_color.blue & 0xFF) << 8;
      roi[(priv->roi_data.nobj * 5) + 4] |=
          (overlay_meta->rects[idx].rect_color.alpha & 0xFF);
    } else {
      roi[(priv->roi_data.nobj * 5) + 4] =
          (overlay_meta->rects[idx].rect_color.blue & 0xFF) << 24;
      roi[(priv->roi_data.nobj * 5) + 4] |=
          (overlay_meta->rects[idx].rect_color.green & 0xFF) << 16;
      roi[(priv->roi_data.nobj * 5) + 4] |=
          (overlay_meta->rects[idx].rect_color.red & 0xFF) << 8;
      roi[(priv->roi_data.nobj * 5) + 4] |=
          (overlay_meta->rects[idx].rect_color.alpha & 0xFF);
    }
    idx++;
    priv->roi_data.nobj++;
  }
}

static GstFlowReturn
gst_vvas_xoverlay_generate_output (GstBaseTransform * trans,
    GstBuffer ** outbuf)
{
  GstVvas_XOverlay *self = GST_VVAS_XOVERLAY (trans);
  GstVvas_XOverlayPrivate *priv = self->priv;
  GstFlowReturn fret = GST_FLOW_OK;
  gboolean bret = FALSE;
  GstVvasOverlayMeta *overlay_meta;
  GstMapFlags map_flags;
  GstVideoFrame in_vframe;
  GstVideoFormat gst_fmt;
  GstBuffer *inbuf = NULL;
  gint32 idx;

  inbuf = trans->queued_buf;
  trans->queued_buf = NULL;

  if (inbuf == NULL)
    return GST_FLOW_OK;

  GST_DEBUG_OBJECT (self, "received buffer %" GST_PTR_FORMAT, inbuf);

  gst_fmt = GST_VIDEO_INFO_FORMAT (self->priv->in_vinfo);

  overlay_meta = gst_buffer_get_vvas_overlay_meta (inbuf);

  if (!overlay_meta) {
    *outbuf = inbuf;
    GST_LOG_OBJECT (self, "unable to get overlaymeta from input buffer");
    return GST_FLOW_OK;
  }

  if (priv->use_bbox_accel && overlay_meta->num_rects > 0) {
    bret = vvas_xoverlay_prepare_input_buffer (self, &inbuf);
    if (!bret)
      goto error;

    for (idx = 0; idx < overlay_meta->num_rects; idx += MAX_BOXES) {
      prepare_bbox_data (self, overlay_meta, gst_fmt, idx);

      bret = vvas_xoverlay_process (self);
      if (!bret)
        goto error;
    }
  }

  map_flags =
      (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF |
      GST_MAP_WRITE);

  memset (&in_vframe, 0x0, sizeof (GstVideoFrame));
  if (!gst_video_frame_map (&in_vframe, self->priv->in_vinfo, inbuf, map_flags)) {
    GST_ERROR_OBJECT (self, "failed to map input buffer");
    goto error;
  }
  if (gst_fmt == GST_VIDEO_FORMAT_GRAY8) {
    vvas_draw_meta_gray (self, &in_vframe, overlay_meta);
  } else if (gst_fmt == GST_VIDEO_FORMAT_BGR || gst_fmt == GST_VIDEO_FORMAT_RGB) {
    vvas_draw_meta_rgb (self, &in_vframe, overlay_meta);
  } else if (gst_fmt == GST_VIDEO_FORMAT_NV12) {
    vvas_draw_meta_nv12 (self, &in_vframe, overlay_meta);
  } else {
    GST_ERROR_OBJECT (self, "Video format not supported");
    goto error;
  }

  gst_video_frame_unmap (&in_vframe);

  *outbuf = inbuf;

error:
  return fret;
}

static void
gst_vvas_xoverlay_class_init (GstVvas_XOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *transform_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_vvas_xoverlay_set_property;
  gobject_class->get_property = gst_vvas_xoverlay_get_property;
  transform_class->set_caps = gst_vvas_xoverlay_set_caps;
  gobject_class->finalize = gst_vvas_xoverlay_finalize;

  transform_class->start = gst_vvas_xoverlay_start;
  transform_class->stop = gst_vvas_xoverlay_stop;
  transform_class->generate_output = gst_vvas_xoverlay_generate_output;

  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("dev-idx", "Device index",
          "Valid Device index is 0 to 31. Default value is set to -1 for PCI and 0 for embedded",
          -1, 31, DEFAULT_DEVICE_INDEX,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_XCLBIN_LOCATION,
      g_param_spec_string ("xclbin-location", "xclbin file location",
          "Location of the xclbin to program device", NULL,
          (GParamFlags) (G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_IN_MEM_BANK,
      g_param_spec_uint ("in-mem-bank", "VVAS Input Memory Bank",
          "VVAS input memory bank to allocate memory",
          0, G_MAXUSHORT, DEFAULT_MEM_BANK,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_DISPLAY_CLOCK,
      g_param_spec_boolean ("display-clock", "display clock flag",
          "flag to display time stamp on frames", 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_USE_BBOX_ACCEL,
      g_param_spec_boolean ("use-bbox-accel", "bbox hw acclerator flag",
          "flag for whether to use hw accelerator or not", 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CLOCK_FONT_NAME,
      g_param_spec_uint ("clock-fontname", "clock display font number",
          "font number for displaying time stamp as given in opencv", 0,
          7, 0, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_CLOCK_FONT_SCALE,
      g_param_spec_float ("clock-fontscale", "clock display font size",
          "font size to be used for displaying time stamp on frames in pixels",
          0, 1.0, 0.5,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CLOCK_FONT_COLOR,
      g_param_spec_uint ("clock-fontcolor", "clock display font color",
          "font color to be used for displaying time stamp on frames as rgba",
          0, 4294967295, 0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CLOCK_X_OFFSET,
      g_param_spec_uint ("clock-xoffset", "clock x offset location",
          "column location of displaying time stamp on frame", 0, 512,
          450, (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CLOCK_Y_OFFSET,
      g_param_spec_uint ("clock-yoffset", "clock y offset location",
          "row location of displaying time stamp on frame", 0, 512,
          450, (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple (gstelement_class,
      "VVAS Generic Overlay Plugin",
      "Filter/Effect/Video",
      "Performs operations on HW IP/SW IP/Softkernel using VVAS library APIs",
      "Xilinx Inc <www.xilinx.com>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xoverlay_debug, "vvas_xoverlay", 0,
      "VVAS optical flow plugin");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_vvas_xoverlay_init (GstVvas_XOverlay * self)
{
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (self);
  GstVvas_XOverlayPrivate *priv = GST_VVAS_XOVERLAY_PRIVATE (self);

  self->priv = priv;
  priv->dev_idx = DEFAULT_DEVICE_INDEX;
  self->priv->display_clock = 0;
  self->priv->use_bbox_accel = 0;

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (btrans), TRUE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (btrans), TRUE);
}

static void
gst_vvas_xoverlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_XOverlay *self = GST_VVAS_XOVERLAY (object);

  switch (prop_id) {
    case PROP_DEVICE_INDEX:
      self->priv->dev_idx = g_value_get_int (value);
      break;
    case PROP_XCLBIN_LOCATION:
      if (self->priv->xclbin_loc)
        g_free (self->priv->xclbin_loc);
      self->priv->xclbin_loc = g_value_dup_string (value);
      GST_DEBUG_OBJECT (self, "xclbin is %s", self->priv->xclbin_loc);
      break;
    case PROP_IN_MEM_BANK:
      self->in_mem_bank = g_value_get_uint (value);
      break;
    case PROP_DISPLAY_CLOCK:
      self->priv->display_clock = g_value_get_boolean (value);
      break;
    case PROP_USE_BBOX_ACCEL:
      self->priv->use_bbox_accel = g_value_get_boolean (value);
      break;
    case PROP_CLOCK_FONT_NAME:
      self->priv->clock_font_name = g_value_get_uint (value);
      break;
    case PROP_CLOCK_FONT_SCALE:
      self->priv->clock_font_scale = g_value_get_float (value);
      break;
    case PROP_CLOCK_FONT_COLOR:
      self->priv->clock_font_color = g_value_get_uint (value);
      break;
    case PROP_CLOCK_X_OFFSET:
      self->priv->clock_x_offset = g_value_get_uint (value);
      break;
    case PROP_CLOCK_Y_OFFSET:
      self->priv->clock_y_offset = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_xoverlay_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVvas_XOverlay *self = GST_VVAS_XOVERLAY (object);

  switch (prop_id) {
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, self->priv->dev_idx);
      break;
    case PROP_XCLBIN_LOCATION:
      g_value_set_string (value, self->priv->xclbin_loc);
      break;
    case PROP_IN_MEM_BANK:
      g_value_set_uint (value, self->in_mem_bank);
      break;
    case PROP_DISPLAY_CLOCK:
      g_value_set_boolean (value, self->priv->display_clock);
      break;
    case PROP_USE_BBOX_ACCEL:
      g_value_set_boolean (value, self->priv->use_bbox_accel);
      break;
    case PROP_CLOCK_FONT_NAME:
      g_value_set_uint (value, self->priv->clock_font_name);
      break;
    case PROP_CLOCK_FONT_SCALE:
      g_value_set_float (value, self->priv->clock_font_scale);
      break;
    case PROP_CLOCK_FONT_COLOR:
      g_value_set_uint (value, self->priv->clock_font_color);
      break;
    case PROP_CLOCK_X_OFFSET:
      g_value_set_uint (value, self->priv->clock_x_offset);
      break;
    case PROP_CLOCK_Y_OFFSET:
      g_value_set_uint (value, self->priv->clock_y_offset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#ifndef PACKAGE
#define PACKAGE "vvas_xoverlay"
#endif

static gboolean
plugin_init (GstPlugin * vvas_xoverlay)
{
  return gst_element_register (vvas_xoverlay, "vvas_xoverlay", GST_RANK_NONE,
      GST_TYPE_VVAS_XOVERLAY);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vvas_xoverlay,
    "GStreamer VVAS plug-in for overlaying display text and geometric shapes",
    plugin_init, VVAS_API_VERSION, "MIT/X11", "Xilinx VVAS SDK plugin",
    "http://xilinx.com/")
