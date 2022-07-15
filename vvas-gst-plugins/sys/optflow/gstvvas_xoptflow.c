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
#include <vvas/xrt_utils.h>
#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include <vvas/vvas_kernel.h>
#include "gstvvas_xoptflow.h"
#include <gst/vvas/gstvvasutils.h>
#include <gst/vvas/gstinferencemeta.h>
#include <gst/vvas/gstvvasofmeta.h>


#define DEFAULT_KERNEL_NAME "dense_non_pyr_of_accel:{dense_non_pyr_of_accel_1}"

#ifdef XLNX_PCIe_PLATFORM
#define DEFAULT_DEVICE_INDEX -1
#define USE_DMABUF 0
#else /* Embedded */
#define DEFAULT_DEVICE_INDEX 0  /* on Embedded only one device i.e. device 0 */
#define  USE_DMABUF 1
#endif
#define MIN_POOL_BUFFERS 2
#define OPT_FLOW_TIMEOUT 1000   // 1 sec

GST_DEBUG_CATEGORY_STATIC (gst_vvas_xoptflow_debug);
#define GST_CAT_DEFAULT gst_vvas_xoptflow_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

typedef struct _GstVvas_XOptflowPrivate GstVvas_XOptflowPrivate;

enum
{
  PROP_0,
  PROP_DEVICE_INDEX,
  PROP_XCLBIN_LOCATION,
  PROP_IN_MEM_BANK,
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("NV12")));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("NV12")));

struct _GstVvas_XOptflowPrivate
{
  guint dev_idx;
  gchar *kern_name;
  xclDeviceHandle dev_handle;
  vvasKernelHandle kern_handle;
  vvasRunHandle run_handle;
  gchar *xclbin_loc;
  uuid_t xclbinId;
  gint cu_idx;
  gboolean first_frame;
  GstBuffer *preserve_buf;
  volatile guint64 img_curr_phy_addr;
  volatile guint64 img_prev_phy_addr;
  volatile guint64 optflow_disp_phy_addr[2];
  GstBuffer *outbufs[2];
  GstVideoInfo *in_vinfo;
  GstBufferPool *in_pool;
  GstBufferPool *meta_pool;
  uint32_t stride;
};

#define gst_vvas_xoptflow_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_XOptflow, gst_vvas_xoptflow,
    GST_TYPE_BASE_TRANSFORM);

#define GST_VVAS_XOPTFLOW_PRIVATE(self) (GstVvas_XOptflowPrivate *) (gst_vvas_xoptflow_get_instance_private (self))

static void gst_vvas_xoptflow_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_vvas_xoptflow_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean vvas_xoptflow_allocate_meta_output_pool (GstVvas_XOptflow *
    self);

static gboolean vvas_xoptflow_prepare_meta_buffers (GstVvas_XOptflow * self);


static gboolean
gst_vvas_xoptflow_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVvas_XOptflow *self = GST_VVAS_XOPTFLOW (trans);
  gboolean bret = TRUE;
  GstVvas_XOptflowPrivate *priv = self->priv;

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
vvas_xoptflow_allocate_meta_output_pool (GstVvas_XOptflow * self)
{
  GstVvas_XOptflowPrivate *priv = self->priv;
  gsize size;
  GstAllocator *allocator = NULL;
  GstAllocationParams params =
      { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0, 0 };
  GstStructure *structure;
  GstCaps *caps;
  GstVideoInfo info;

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "GRAY8",
      "width", G_TYPE_INT, self->priv->stride,
      "height", G_TYPE_INT, GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo), NULL);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  size = self->priv->stride * priv->in_vinfo->height * 4;       //float buffer

  allocator = gst_vvas_allocator_new (priv->dev_idx, USE_DMABUF,
      self->in_mem_bank, self->priv->kern_handle);
  params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
  params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;

  priv->meta_pool = gst_vvas_buffer_pool_new (1, 1);

  GST_LOG_OBJECT (self, "allocated preprocess output pool %" GST_PTR_FORMAT
      "output allocator %" GST_PTR_FORMAT, priv->meta_pool, allocator);

  structure = gst_buffer_pool_get_config (priv->meta_pool);

  gst_buffer_pool_config_set_params (structure, caps, size, 4, 0);
  gst_buffer_pool_config_add_option (structure,
      GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_allocator (structure, allocator, &params);

  if (allocator)
    gst_object_unref (allocator);

  if (caps)
    gst_caps_unref (caps);

  if (!gst_buffer_pool_set_config (priv->meta_pool, structure)) {
    GST_ERROR_OBJECT (self, "failed to configure pool");
    GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to configure pool."),
        ("failed to configure preprocess output pool"));
    return FALSE;
  }

  if (!gst_buffer_pool_set_active (priv->meta_pool, TRUE)) {
    GST_ERROR_OBJECT (self, "failed to activate preprocess output pool");
    GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to activate pool."),
        ("failed to activate preprocess output pool"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
vvas_xoptflow_init (GstVvas_XOptflow * self)
{
  GstVvas_XOptflowPrivate *priv = self->priv;
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

  return TRUE;
}

static gboolean
gst_vvas_xoptflow_start (GstBaseTransform * trans)
{
  GstVvas_XOptflow *self = GST_VVAS_XOPTFLOW (trans);
  GstVvas_XOptflowPrivate *priv = self->priv;
  self->priv = priv;
  priv->in_vinfo = gst_video_info_new ();

  if (!vvas_xrt_open_device (priv->dev_idx, &priv->dev_handle)) {
    GST_ERROR_OBJECT (self, "failed to open device index %u", priv->dev_idx);
    return FALSE;
  }

  if (!vvas_xoptflow_init (self)) {
    GST_ERROR_OBJECT (self, "unable to initalize opticalflow context");
    return FALSE;
  }

  GST_INFO_OBJECT (self, "start completed");
  return TRUE;
}

static gboolean
vvas_xoptflow_deinit (GstVvas_XOptflow * self)
{
  GstVvas_XOptflowPrivate *priv = self->priv;
  gint cu_idx = -1;

  if (priv->kern_name)
    free (priv->kern_name);

  if (priv->in_pool && gst_buffer_pool_is_active (priv->in_pool)) {
    if (!gst_buffer_pool_set_active (priv->in_pool, FALSE)) {
      GST_ERROR_OBJECT (self, "failed to deactivate internal input pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to deactivate pool."),
          ("failed to deactivate internal input pool"));
      return FALSE;
    }
  }

  if (priv->meta_pool && gst_buffer_pool_is_active (priv->meta_pool)) {
    if (!gst_buffer_pool_set_active (priv->meta_pool, FALSE)) {
      GST_ERROR_OBJECT (self, "failed to deactivate internal meta out pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED,
          ("failed to deactivate out pool."),
          ("failed to deactivate internal meta out pool"));
      return FALSE;
    }
  }

  if (priv->xclbin_loc)
    free (priv->xclbin_loc);

  if (priv->dev_handle) {
    if (cu_idx >= 0) {
      GST_INFO_OBJECT (self, "closing context for cu_idx %d", cu_idx);
      vvas_xrt_close_context (priv->kern_handle);
    }
    vvas_xrt_close_device (priv->dev_handle);
  }

  return TRUE;
}

static gboolean
gst_vvas_xoptflow_stop (GstBaseTransform * trans)
{
  GstVvas_XOptflow *self = GST_VVAS_XOPTFLOW (trans);

  GST_DEBUG_OBJECT (self, "stopping");

  if (self->priv->preserve_buf)
    gst_buffer_unref (self->priv->preserve_buf);

  if (self->priv->in_vinfo) {
    gst_video_info_free (self->priv->in_vinfo);
    self->priv->in_vinfo = NULL;
  }
  vvas_xoptflow_deinit (self);

  return TRUE;
}

static void
gst_vvas_xoptflow_finalize (GObject * obj)
{
  GstVvas_XOptflow *self = GST_VVAS_XOPTFLOW (obj);

  if (self->priv->in_pool)
    gst_object_unref (self->priv->in_pool);
  if (self->priv->in_vinfo)
    gst_video_info_free (self->priv->in_vinfo);

  if (self->priv->meta_pool)
    gst_object_unref (self->priv->meta_pool);
}

static gboolean
vvas_xoptflow_allocate_internal_pool (GstVvas_XOptflow * self)
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
  alloc_params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;
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
vvas_xoptflow_prepare_input_buffer (GstVvas_XOptflow * self, GstBuffer ** inbuf)
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
      bret = vvas_xoptflow_allocate_internal_pool (self);
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

  self->priv->img_curr_phy_addr = phy_addr;

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

static gboolean
vvas_xoptflow_prepare_meta_buffers (GstVvas_XOptflow * self)
{
  GstMemory *mem = NULL;
  GstVvas_XOptflowPrivate *priv = self->priv;
  guint flow_id;

  if (!priv->meta_pool) {
    /* Create new meta buffer pool */
    if (!vvas_xoptflow_allocate_meta_output_pool (self)) {
      GST_ERROR_OBJECT (self, "failed to create meta buffer pool");
      goto error;
    }
  }

  for (flow_id = 0; flow_id < 2; flow_id++) {
    GstBuffer *outbuf = NULL;
    guint64 phy_addr = -1;
    GstFlowReturn fret;

    fret = gst_buffer_pool_acquire_buffer (priv->meta_pool, &outbuf, NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
          priv->meta_pool);
      goto error;
    }
    GST_LOG_OBJECT (self, "acquired buffer %p from pool %p", outbuf,
        priv->meta_pool);

    gst_buffer_add_video_meta (outbuf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_FORMAT_GRAY8, priv->stride, priv->in_vinfo->height);

    priv->outbufs[flow_id] = outbuf;

    mem = gst_buffer_get_memory (outbuf, 0);
    if (mem == NULL) {
      GST_ERROR_OBJECT (self, "failed to get memory from output buffer");
      goto error;
    }

    if (gst_is_vvas_memory (mem)) {
      phy_addr = gst_vvas_allocator_get_paddr (mem);
    } else {
      GST_ERROR_OBJECT (self, "Unsupported mem");
      goto error;
    }

    priv->optflow_disp_phy_addr[flow_id] = phy_addr;
    gst_memory_unref (mem);
  }

  return TRUE;

error:
  if (mem)
    gst_memory_unref (mem);

  return FALSE;
}

static int32_t
vvas_xoptflow_exec_buf (vvasDeviceHandle dev_handle,
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
vvas_xoptflow_process (GstVvas_XOptflow * self)
{
  GstVvas_XOptflowPrivate *priv = self->priv;
  uint32_t flow_id = 0;
  int iret;
  uint32_t width = priv->stride;
  uint32_t height = priv->in_vinfo->height;
  int retry_count = MAX_EXEC_WAIT_RETRY_CNT;

  GstMemory *mem = NULL;

  iret = vvas_xoptflow_exec_buf (priv->dev_handle, priv->kern_handle,
      &priv->run_handle,
      "ppppii", priv->img_prev_phy_addr,
      priv->img_curr_phy_addr,
      priv->optflow_disp_phy_addr[0],
      priv->optflow_disp_phy_addr[1], height, width);
  if (iret) {
    GST_ERROR_OBJECT (self, "failed to execute command %d", iret);
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, NULL,
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

  for (flow_id = 0; flow_id < 2; flow_id++) {
    mem = gst_buffer_get_memory (priv->outbufs[flow_id], 0);
    if (mem == NULL) {
      GST_ERROR_OBJECT (self,
          "flow-%d : failed to get memory from output buffer", flow_id);
      return FALSE;
    }
#ifdef XLNX_PCIe_PLATFORM
    gst_vvas_memory_set_sync_flag (mem, VVAS_SYNC_FROM_DEVICE);
#endif
    gst_memory_unref (mem);
  }
  return TRUE;
}

static gboolean
vvas_xoptflow_add_as_meta (GstVvas_XOptflow * self, GstBuffer * outbuf)
{
  GstVvasOFMeta *of_meta;

  of_meta = gst_buffer_add_vvas_of_meta (outbuf);

  if (!of_meta) {
    GST_ERROR_OBJECT (vvas_xoptflow_add_as_meta,
        "unable to add optical flow meta to buffer");
    return false;
  }

  of_meta->x_displ = self->priv->outbufs[0];
  of_meta->y_displ = self->priv->outbufs[1];

  return TRUE;
}

static GstFlowReturn
gst_vvas_xoptflow_generate_output (GstBaseTransform * trans,
    GstBuffer ** outbuf)
{
  GstVvas_XOptflow *self = GST_VVAS_XOPTFLOW (trans);
  GstVvas_XOptflowPrivate *priv = self->priv;
  GstFlowReturn fret = GST_FLOW_OK;
  gboolean bret = FALSE;
  GstMapInfo info;
  GstBuffer *inbuf = NULL;
  GstBuffer *w_buf;

  inbuf = trans->queued_buf;
  trans->queued_buf = NULL;

  if (inbuf == NULL)
    return GST_FLOW_OK;

  GST_DEBUG_OBJECT (self, "received buffer %" GST_PTR_FORMAT, inbuf);

  priv->img_prev_phy_addr = priv->img_curr_phy_addr;
  if (!vvas_xoptflow_prepare_input_buffer (self, &inbuf)) {
    GST_ERROR_OBJECT (self, "failed to prepare input buffer");
    goto error;
  }

  if (!vvas_xoptflow_prepare_meta_buffers (self)) {
    GST_ERROR_OBJECT (self, "failed to prepare meta buffers");
    goto error;
  }

  if (priv->first_frame) {
    if (!gst_buffer_map (priv->outbufs[0], &info, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "failed to make meta buffer writable");
      goto error;
    }
    memset (info.data, 0x0, info.size);

    gst_buffer_unmap (priv->outbufs[0], &info);

    if (!gst_buffer_map (priv->outbufs[1], &info, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "failed to make meta buffer writable");
      goto error;
    }
    memset (info.data, 0x0, info.size);
    gst_buffer_unmap (priv->outbufs[1], &info);

    GST_INFO_OBJECT (self, "first frame meta buffers are set to zero");

    priv->first_frame = FALSE;
  } else {
    bret = vvas_xoptflow_process (self);
    if (!bret)
      goto error;

    if (priv->preserve_buf)
      gst_buffer_unref (priv->preserve_buf);
  }

  w_buf = gst_buffer_make_writable (inbuf);
  bret = vvas_xoptflow_add_as_meta (self, w_buf);

  priv->preserve_buf = gst_buffer_ref (w_buf);
  GST_DEBUG_OBJECT (self, "prev-buffer ref %" GST_PTR_FORMAT,
      priv->preserve_buf);

  *outbuf = w_buf;
error:
  return fret;
}

static void
gst_vvas_xoptflow_class_init (GstVvas_XOptflowClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *transform_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_vvas_xoptflow_set_property;
  gobject_class->get_property = gst_vvas_xoptflow_get_property;
  gobject_class->finalize = gst_vvas_xoptflow_finalize;

  transform_class->start = gst_vvas_xoptflow_start;
  transform_class->stop = gst_vvas_xoptflow_stop;
  transform_class->set_caps = gst_vvas_xoptflow_set_caps;
  transform_class->generate_output = gst_vvas_xoptflow_generate_output;

  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("dev-idx", "Device index",
          "Valid Device index is 0 to 31. Default value is set to -1 for PCI and 0 for embedded",
          -1, 31, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_XCLBIN_LOCATION,
      g_param_spec_string ("xclbin-location", "xclbin file location",
          "Location of the xclbin to program device", NULL,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_IN_MEM_BANK,
      g_param_spec_uint ("in-mem-bank", "VVAS Input Memory Bank",
          "VVAS input memory bank to allocate memory",
          0, G_MAXUSHORT, DEFAULT_MEM_BANK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_set_details_simple (gstelement_class,
      "VVAS Generic Optflow Plugin",
      "Filter/Effect/Video",
      "Performs operations on HW IP/SW IP/Softkernel using VVAS library APIs",
      "Xilinx Inc <www.xilinx.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xoptflow_debug, "vvas_xoptflow", 0,
      "VVAS optical flow plugin");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_vvas_xoptflow_init (GstVvas_XOptflow * self)
{
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (self);
  GstVvas_XOptflowPrivate *priv = GST_VVAS_XOPTFLOW_PRIVATE (self);

  self->priv = priv;
  priv->dev_idx = DEFAULT_DEVICE_INDEX;
  priv->first_frame = TRUE;
  priv->img_curr_phy_addr = 0;
  priv->img_prev_phy_addr = 0;
  self->priv->in_pool = NULL;
  self->priv->meta_pool = NULL;

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (btrans), TRUE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (btrans), TRUE);
}

static void
gst_vvas_xoptflow_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_XOptflow *self = GST_VVAS_XOPTFLOW (object);

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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_xoptflow_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVvas_XOptflow *self = GST_VVAS_XOPTFLOW (object);

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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * vvas_xoptflow)
{
  return gst_element_register (vvas_xoptflow, "vvas_xoptflow", GST_RANK_NONE,
      GST_TYPE_VVAS_XOPTFLOW);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vvas_xoptflow,
    "GStreamer VVAS plug-in for optical flow",
    plugin_init, "1.0", "MIT/X11",
    "Xilinx VVAS SDK plugin", "http://xilinx.com/")
