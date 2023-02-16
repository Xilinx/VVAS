/*
 * Copyright (C) 2022 Xilinx, Inc.  All rights reserved.
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
#include <gst/gst.h>
#include <gst/vvas/gstvvasallocator.h>
#include <gst/vvas/gstvvasbufferpool.h>
#include <gst/allocators/gstdmabuf.h>
#include <time.h>
#include <vvas_core/vvas_device.h>
#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include <vvas/vvas_kernel.h>
#include <vvas_core/vvas_overlay.h>
#include "gstvvas_xoverlay.h"
#include <gst/vvas/gstvvasutils.h>
#include <gst/vvas/gstvvasoverlaymeta.h>
#include <gst/vvas/gstvvascoreutils.h>

/** @def DEFAULT_KERNEL_NAME
 *  @brief Default kernel name of boundingbox IP
 */
#define DEFAULT_KERNEL_NAME "boundingbox_accel:{boundingbox_accel_1}"

#ifdef XLNX_PCIe_PLATFORM
/**
 * @brief Default value of the device index in case user has not provided
 */
#define DEFAULT_DEVICE_INDEX -1
/**
 * @brief Default no dma buffer in PCIe
 */
#define USE_DMABUF 0
#else
/**
 * @brief On Embedded only one device i.e. device 0
 */
#define DEFAULT_DEVICE_INDEX 0
/**
 * @brief Default use dma buffer in Embedded
 */
#define USE_DMABUF 1
#endif

/** @def OVERLAY_FLOW_TIMEOUT
 *  @brief Default timeout for overlay bbox IP
 */
#define OVERLAY_BBOX_TIMEOUT 1000

/** @def MAX_BOXES
 *  @brief Maximum number of bounding boxes hardware can draw
 */
#define MAX_BOXES 5

/** @def DEFAULT_THICKNESS_LEVEL
 *  @brief default thickness of bounding boxes for hardware ip
 */
#define DEFAULT_THICKNESS_LEVEL 1

/**
 *  @brief Defines a static GstDebugCategory global variable "gst_vvas_xoverlay_debug"
 */
GST_DEBUG_CATEGORY_STATIC (gst_vvas_xoverlay_debug);

/** @def GST_CAT_DEFAULT
 *  @brief Setting gst_vvas_xoverlay_debug as default debug category for logging
 */
#define GST_CAT_DEFAULT gst_vvas_xoverlay_debug
/**
 *  @brief Defines a static GstDebugCategory global variable with name GST_CAT_PERFORMANCE for
 *         performance logging purpose
 */
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);


/**
 * @brief Default width align for Edge
 */
#define WIDTH_ALIGN 1
/**
 * @brief Default height align for Edge
 */
#define HEIGHT_ALIGN 1

typedef struct _GstVvas_XOverlayPrivate GstVvas_XOverlayPrivate;

enum
{
  /** default */
  PROP_0,
  /** Index of the device */
  PROP_DEVICE_INDEX,
  /** path of the xclbin */
  PROP_XCLBIN_LOCATION,
  /** Memory bank from which memory need to be allocated */
  PROP_IN_MEM_BANK,
  /** bool property to show clock on frame or not */
  PROP_DISPLAY_CLOCK,
  /** Font name from opencv to be used for clock display */
  PROP_CLOCK_FONT_NAME,
  /** color to be used for clock display */
  PROP_CLOCK_FONT_COLOR,
  /** Font scale to be used for clock display */
  PROP_CLOCK_FONT_SCALE,
  /** Column start point of clock display */
  PROP_CLOCK_X_OFFSET,
  /** Row start point of clock display */
  PROP_CLOCK_Y_OFFSET,
};

/**
 *  @brief Defines sink pad template
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{NV12, RGB, BGR, GRAY8}")));

/**
 *  @brief Defines source pad template
 */
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{NV12, RGB, BGR, GRAY8}")));


/** @struct vvas_bbox_acc_roi
 *  @brief  Holds number of bboxes and their info
 */
typedef struct _vvas_bbox_acc_roi
{
  /** Number of bbox to be draw */
  uint32_t nobj;
  /** bbox info as xrt_buffer */
  xrt_buffer roi;
} vvas_bbox_acc_roi;

/** @struct _GstVvas_XOverlayPrivate
 *  @brief  Holds private members related overlay
 */
struct _GstVvas_XOverlayPrivate
{
  /** Index of the device on which memory is going to allocated */
  guint dev_idx;
  /** Name of the bbox kernel */
  gchar *kern_name;
  /** Handle to FPGA device with index \p dev_idx */
  xclDeviceHandle dev_handle;
  /** Handle to FPGA kernel object instance */
  vvasKernelHandle kern_handle;
  /** Handle to kernel execution object */
  vvasRunHandle run_handle;
  /** Location of xclbin for downloading*/
  gchar *xclbin_loc;
  /** xclbin id  */
  uuid_t xclbinId;
  /** Index of the device on which memory is going to allocated */
  gint cu_idx;
  /** Physical address of first plane of input buf */
  guint64 img_p1_phy_addr;
  /** Physical address of second plane of input buf */
  guint64 img_p2_phy_addr;
  /** Pointer to the info of input caps */
  GstVideoInfo *in_vinfo;
  /** Pointer to the input buffer pool */
  GstBufferPool *in_pool;
  /** flag to display clock or not  */
  gboolean display_clock;
  /** To store clock data as string */
  gchar clock_time_string[256];
  /** flag to use bbox accelerator or not  */
  gboolean use_bbox_accel;
  /** display clock font name from opencv */
  gint clock_font_name;
  /** display clock font scale */
  gfloat clock_font_scale;
  /** display clock font color */
  guint clock_font_color;
  /** display clock column start position in frame */
  gint clock_x_offset;
  /** display clock row start position in frame */
  gint clock_y_offset;
  /** To store bbox information for bbox hardware IP */
  vvas_bbox_acc_roi roi_data;
  /** stride information */
  uint32_t stride;
        /** VVAS context handle  */
  VvasContext *vvas_ctx;
};

#define gst_vvas_xoverlay_parent_class parent_class

/** @brief  Glib's convenience macro for GstVvas_XOverlay type implementation.
 *  @details This macro does below tasks:\n
 *           - Declares a class initialization function with prefix gst_vvas_xoverlay
 */
G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_XOverlay, gst_vvas_xoverlay,
    GST_TYPE_BASE_TRANSFORM);

/** @def GST_VVAS_XOVERLAY_PRIVATE(self)
 *  @brief Get instance of GstVvas_XOverlayPrivate structure
 */
#define GST_VVAS_XOVERLAY_PRIVATE(self) (GstVvas_XOverlayPrivate *) (gst_vvas_xoverlay_get_instance_private (self))

/* Functions declaration */
static void gst_vvas_xoverlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_vvas_xoverlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/**
 *  @fn uint32_t xlnx_bbox_align (uint32_t stride_in)
 *  @return enumeration identifier type
 */
static uint32_t
xlnx_bbox_align (uint32_t stride_in)
{
  return stride_in / 4;
}

/**
 *  @fn gboolean gst_vvas_xoverlay_set_caps (GstBaseTransform * trans,
 *                                GstCaps * incaps, GstCaps * outcaps)
 *  @param [in] trans - Pointer to GstBaseTransform object.
 *  @param [in] incaps - Pointer to input caps of GstCaps object.
 *  @param [in] outcaps - Pointer to output caps of GstCaps object.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  API to get input and output capabilities.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::set_caps function pointer and
 *          this will be called to get the input and output capabilities.
 */
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

  /* Update in_vinfo from input caps */
  if (!gst_video_info_from_caps (priv->in_vinfo, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse input caps");
    return FALSE;
  }

  return bret;
}

/**
 *  @fn gboolean vvas_xoverlay_init (GstVvas_XOverlay * self)
 *  @param [inout] self - Pointer to GstVvas_XOverlay structure.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Opens XRT context for bbox kernel
 *  @note  Using bbox kernel name and location downloads xclbin.
 *         Opens a context for XRT
 *
 */
static gboolean
vvas_xoverlay_init (GstVvas_XOverlay * self)
{
  GstVvas_XOverlayPrivate *priv = self->priv;
  int iret;

  /* get name of the optical flow kernel from default set */
  if (!priv->kern_name)
    priv->kern_name = g_strdup (DEFAULT_KERNEL_NAME);

  if (!priv->kern_name) {
    GST_ERROR_OBJECT (self, "kernel name is not set");
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("kernel name is not set"));
    return FALSE;
  }

  /* get xclbin location */
  if (!priv->xclbin_loc) {
    GST_ERROR_OBJECT (self, "invalid xclbin location %s", priv->xclbin_loc);
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, (NULL),
        ("xclbin path not set"));
    return FALSE;
  }

  /* download xclbin using kernel name and location */
  if (vvas_xrt_download_xclbin (priv->xclbin_loc,
          priv->dev_handle, &priv->xclbinId)) {
    GST_ERROR_OBJECT (self, "failed to initialize XRT");
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("xclbin download failed"));
    return FALSE;
  }

  /* create FPGA kernel object instance of bbox */
  iret = vvas_xrt_open_context (priv->dev_handle, priv->xclbinId,
      &priv->kern_handle, priv->kern_name, true);
  if (iret) {
    GST_ERROR_OBJECT (self, "failed to open XRT context ...:%d", iret);
    return FALSE;
  }

  /* allocates memory from in_mem_bank for storing bbox info */
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

/**
 *  @fn gboolean gst_vvas_xoverlay_start (GstBaseTransform * trans)
 *  @param [in] trans - Pointer to GstBaseTransform object.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Opens device handle and creates context for XRT.
 *  @details This API is registered with GObjectClass by overriding GstBaseTransform::start function pointer and
 *          this will be called when element start processing. It opens device context and allocates memory.
 */
static gboolean
gst_vvas_xoverlay_start (GstBaseTransform * trans)
{
  GstVvas_XOverlay *self = GST_VVAS_XOVERLAY (trans);
  GstVvas_XOverlayPrivate *priv = self->priv;
  VvasReturnType vret;

  self->priv = priv;
  priv->in_vinfo = gst_video_info_new ();

  VvasLogLevel core_log_level =
      vvas_get_core_log_level (gst_debug_category_get_threshold
      (gst_vvas_xoverlay_debug));

  /* checks whether bbox accelerator enabled */
  if (self->priv->use_bbox_accel) {
    /* get handle to XRT for the given device */
    if (!vvas_xrt_open_device (priv->dev_idx, &priv->dev_handle)) {
      GST_ERROR_OBJECT (self, "failed to open device index %u", priv->dev_idx);
      return FALSE;
    }
    /* initialize bbox overlay context */
    if (!vvas_xoverlay_init (self)) {
      GST_ERROR_OBJECT (self, "unable to initialize overlay context");
      return FALSE;
    }
  }

  /* create a vvas context */
  priv->vvas_ctx = vvas_context_create (priv->dev_idx, priv->xclbin_loc,
      core_log_level, &vret);
  if (vret != VVAS_RET_SUCCESS) {
    GST_ERROR_OBJECT (self, "Couldn't create VVAS context");
    return FALSE;
  }

  GST_INFO_OBJECT (self, "start completed");
  return TRUE;
}

/**
 *  @fn gboolean vvas_xoverlay_deinit (GstVvas_XOverlay * self)
 *  @param [inout] self - Pointer to GstVvas_XOverlay structure.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  API frees memory allocated and closes device context
 *  @note  Frees all memories allocated including input pools and
 *         closes context of XRT.
 *
 */
static gboolean
vvas_xoverlay_deinit (GstVvas_XOverlay * self)
{
  GstVvas_XOverlayPrivate *priv = self->priv;
  gint cu_idx = -1;

  /* free the input buffer pool */
  if (priv->in_pool && gst_buffer_pool_is_active (priv->in_pool)) {
    if (!gst_buffer_pool_set_active (priv->in_pool, FALSE)) {
      GST_ERROR_OBJECT (self, "failed to deactivate internal input pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to deactivate pool."),
          ("failed to deactivate internal input pool"));
      return FALSE;
    }
  }

  /* if use_bbox_accel true free all memory and closes XRT context */
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

/**
 *  @fn gboolean gst_vvas_xoverlay_stop (GstBaseTransform * trans)
 *  @param [in] trans - Pointer to GstBaseTransform object.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Free up allocates memory and invokes vvas_xoverlay_deinit.
 *  @details This API is registered with GObjectClass by overriding GstBaseTransform::stop function pointer and
 *          this will be called when element stops processing.
 *          It invokes vvas_xoverlay_deinit to free up allocated memory.
 *
 */
static gboolean
gst_vvas_xoverlay_stop (GstBaseTransform * trans)
{
  GstVvas_XOverlay *self = GST_VVAS_XOVERLAY (trans);
  GstVvas_XOverlayPrivate *priv = self->priv;
  GST_DEBUG_OBJECT (self, "stopping");

  if (self->priv->in_vinfo) {
    gst_video_info_free (self->priv->in_vinfo);
    self->priv->in_vinfo = NULL;
  }

  if (priv->vvas_ctx) {
    vvas_context_destroy (priv->vvas_ctx);
  }
  vvas_xoverlay_deinit (self);

  return TRUE;
}

/**
 *  @fn static void gst_vvas_xoverlay_finalize (GObject * obj)
 *  @param [in] Handle to GstVvas_XOverlay typecast to GObject
 *  @return None
 *  @brief This API will be called during GstVvas_XOverlay object's destruction phase. Close references
 *         to devices and free memories if any
 *  @note After this API GstVvas_XOverlay object \p obj will be destroyed completely. So free all internal
 *        memories held by current object
 */
static void
gst_vvas_xoverlay_finalize (GObject * obj)
{
  GstVvas_XOverlay *self = GST_VVAS_XOVERLAY (obj);

  if (self->priv->in_pool)
    gst_object_unref (self->priv->in_pool);

  self->priv->display_clock = 0;
  self->priv->use_bbox_accel = 0;

  G_OBJECT_CLASS (gst_vvas_xoverlay_parent_class)->finalize (obj);
}

/**
 *  @fn gboolean vvas_xoverlay_allocate_internal_pool (GstVvas_XOverlay * self)
 *  @param [inout] self - Pointer to GstVvas_XOverlay structure.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Creates buffer pool for allocating memory when required for input buffers.
 *  @note  Create input pool structure based on info from sink pad caps.
 *
 */
static gboolean
vvas_xoverlay_allocate_internal_pool (GstVvas_XOverlay * self)
{
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstCaps *caps = NULL;

  /* get caps from sink pad */
  caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM (self)->sinkpad);

  /* Parses caps and updates info */
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  /* Creates a buffer pool for allocating memory to video frames */
  pool = gst_video_buffer_pool_new ();
  GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

  /* create allocator to allocate from specific memory bank for the device */
  allocator = gst_vvas_allocator_new (self->priv->dev_idx, USE_DMABUF,
      self->in_mem_bank);

  /* Initializes allocator params with default value */
  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
  GST_INFO_OBJECT (self, "allocated %" GST_PTR_FORMAT " allocator", allocator);

  /* Gets a copy of the current configuration of the pool */
  config = gst_buffer_pool_get_config (pool);

  /* Updates config with parameters */
  gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (&info),
      4, 5);
  /* Updates config with allocator and allocator parameters */
  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  /* Sets the configuration of the pool */
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

/**
 *  @fn gboolean vvas_xoverlay_prepare_input_buffer (GstVvas_XOverlay * self,
 *                           GstBuffer ** inbuf)
 *  @param [inout] self - Pointer to GstVvas_XOverlay structure.
 *  @param [in] inbuf - Pointer to input buffer of type GstBuffer.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Prepares input buffer required for bbox hardware IP.
 *  @note  If input buffer is hardware buffer (VVAS or DMA)physical address is obtain from memory structure.
 *         If not a hardware buffer will be created from input pool and input buffer will be copied.
 *
 */
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

  /* Get GstMemory object from input GstBuffer */
  in_mem = gst_buffer_get_memory (*inbuf, 0);
  if (!in_mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    goto error;
  }

  /* check if input buffer is allocated from VVAS memory */
  if (gst_is_vvas_memory (in_mem)
      && gst_vvas_memory_can_avoid_copy (in_mem, self->priv->dev_idx,
          self->in_mem_bank)) {
    phy_addr = gst_vvas_allocator_get_paddr (in_mem);
  } else if (gst_is_dmabuf_memory (in_mem)) {
    /* check for dma buffer type */
    gint dma_fd = -1;
    /* Get the dma descriptor associated with in_mem */
    dma_fd = gst_dmabuf_memory_get_fd (in_mem);
    if (dma_fd < 0) {
      GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
      goto error;
    }

    /* Get XRT BO handle for DMA memory */
    bo_handle = vvas_xrt_import_bo (self->priv->dev_handle, dma_fd);
    if (bo_handle == NULL) {
      GST_WARNING_OBJECT (self,
          "failed to get XRT BO...fall back to copy input");
    }

    GST_DEBUG_OBJECT (self, "received dma fd %d and its xrt BO = %p", dma_fd,
        bo_handle);
    /* Get physical address of the xrt bo */
    phy_addr = vvas_xrt_get_bo_phy_addres (bo_handle);

    if (bo_handle != NULL)
      vvas_xrt_free_bo (bo_handle);

  } else {
    /* set to allocate memory for pool if memory is not of type VVAS or DMA */
    use_inpool = TRUE;
  }

  if (use_inpool) {
    /* Check if input pool created. If not creates input pool */
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

    /* unref the input buffer and assign internal buffer as input buffer */
    gst_buffer_unref (*inbuf);
    *inbuf = inpool_buf;
  }

  vmeta = gst_buffer_get_video_meta (*inbuf);
  if (vmeta == NULL) {
    GST_ERROR_OBJECT (self, "video meta not present in buffer");
    goto error;
  }

  /* check for validity of physical address */
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
  /* syncs data when XLNX_SYNC_TO_DEVICE flag is enabled */
  bret = gst_vvas_memory_sync_bo (in_mem);
  if (!bret)
    goto error;

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

/**
 *  @fn int32_t vvas_xoverlay_exec_buf (vvasDeviceHandle dev_handle,
 *      vvasKernelHandle kern_handle, vvasRunHandle * run_handle,
 *            const char *format, ...)
 *  @param [in] dev_handle - Handle to XRT device object created for specified device index
 *  @param [in] kern_handle - Handle to FPGA kernel object instance created for optical flow
 *  @param [in] vvasRunHandle - Handle to kernel execution object
 *  @param [in] format - arguments descriptor
 *  @return return 0 on success\n
 *          1 on failure.
 *
 *  @brief  Function to call execution of bbox hardware IP.
 *  @details Calls execution function using argument list as input
 */
static int32_t
vvas_xoverlay_exec_buf (vvasDeviceHandle dev_handle,
    vvasKernelHandle kern_handle, vvasRunHandle * run_handle,
    const char *format, ...)
{
  va_list args;
  int32_t iret;

  va_start (args, format);
  /* Calls VVAS XRT utility function for kernel execution */
  iret = vvas_xrt_exec_buf (dev_handle, kern_handle, run_handle, format, args);

  va_end (args);

  return iret;
}

/**
 *  @fn gboolean vvas_xoverlay_process (GstVvas_XOverlay * self)
 *  @param [inout] self - Handle to GstVvas_XOverlay instance
 *  @return TRUE on success\n
 *          FALSE on failure.
 *
 *  @brief  Programmes hardware IP to draw bounding boxes in frame.
 *  @details Programmes hardware IP to draw bounding boxes in frame.
 *           Waits for buffer to process if process time exceeds OVERLAY_BBOX_TIMEOUT
 *           return FALSE.
 */
static gboolean
vvas_xoverlay_process (GstVvas_XOverlay * self)
{
  GstVvas_XOverlayPrivate *priv = self->priv;
  uint32_t stride;
  int iret;
  GstVideoFormat gst_fmt = GST_VIDEO_INFO_FORMAT (priv->in_vinfo);
  uint32_t height = priv->in_vinfo->height;
  int retry_count = MAX_EXEC_WAIT_RETRY_CNT;

  /* check format of input frame.  RGB and BGR different set of descriptor from
     NV12 format */
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

  /* check return value from hardware execution */
  if (iret) {
    GST_ERROR_OBJECT (self, "failed to execute command %d", iret);
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("failed to issue execute command. reason : %s", strerror (errno)));
    return FALSE;
  }

  /* Checks for completion of processing.  If processing is taking
     more than predefined time function return FALSE */
  do {
    iret = vvas_xrt_exec_wait (priv->dev_handle, priv->run_handle,
        OVERLAY_BBOX_TIMEOUT);
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

/**
 *  @fn void prepare_bbox_data (GstVvas_XOverlay * self,
 *          GstVvasOverlayMeta * overlay_meta, GstVideoFormat gst_fmt, gint32 str_idx)
 *  @param [inout] self - Handle to GstVvas_XOverlay instance
 *  @param [in] overlay_meta - Pointer to metadata type GstVvasOverlayMeta
 *  @param [in] gst_fmt - Gstreamer based frame format
 *  @param [in] str_idx - Starting point in a array bboxes to draw on frame
 *  @return None
 *
 *  @brief  This API converts bounding box metadata from overlay meta to
 *          sequence data as per expectations of bbox accelerator.
 */
void
prepare_bbox_data (GstVvas_XOverlay * self,
    GstVvasOverlayMeta * overlay_meta, GstVideoFormat gst_fmt, guint32 str_idx)
{
  guint32 idx, end_idx, loop;
  GstVvas_XOverlayPrivate *priv = self->priv;
  gint32 *roi = (gint32 *) priv->roi_data.roi.user_ptr;
  VvasList *head;

  /* check if number of bbox are less or more than MAX_BOXES */
  if ((str_idx + MAX_BOXES) > overlay_meta->shape_info.num_rects)
    end_idx = overlay_meta->shape_info.num_rects;
  else
    end_idx = str_idx + MAX_BOXES;

  priv->roi_data.nobj = 0;
  idx = 1;
  VvasOverlayRectParams *rect_params;

  /* while loop to get VvasList pointer of nth element */
  head = overlay_meta->shape_info.rect_params;
  while (idx != str_idx && head) {
    idx++;
    head = head->next;
  }
  /* for loop to read bounding box info from starting at str_idx to end_idx */
  for (loop = str_idx; (loop < end_idx) && (head != NULL); loop++) {
    rect_params = (VvasOverlayRectParams *) head->data;
    roi[priv->roi_data.nobj * 5] = rect_params->points.x;
    roi[(priv->roi_data.nobj * 5) + 1] = rect_params->points.y;
    roi[(priv->roi_data.nobj * 5) + 2] = rect_params->width;
    roi[(priv->roi_data.nobj * 5) + 3] = rect_params->height;

    if (gst_fmt == GST_VIDEO_FORMAT_RGB) {
      roi[(priv->roi_data.nobj * 5) + 4] =
          (rect_params->rect_color.red & 0xFF) << 24;
      roi[(priv->roi_data.nobj * 5) + 4] |=
          (rect_params->rect_color.green & 0xFF) << 16;
      roi[(priv->roi_data.nobj * 5) + 4] |=
          (rect_params->rect_color.blue & 0xFF) << 8;
      roi[(priv->roi_data.nobj * 5) + 4] |=
          (rect_params->rect_color.alpha & 0xFF);
    } else {
      roi[(priv->roi_data.nobj * 5) + 4] =
          (rect_params->rect_color.blue & 0xFF) << 24;
      roi[(priv->roi_data.nobj * 5) + 4] |=
          (rect_params->rect_color.green & 0xFF) << 16;
      roi[(priv->roi_data.nobj * 5) + 4] |=
          (rect_params->rect_color.red & 0xFF) << 8;
      roi[(priv->roi_data.nobj * 5) + 4] |=
          (rect_params->rect_color.alpha & 0xFF);
    }
    head = head->next;
    priv->roi_data.nobj++;
  }
}

/**
 *  @fn gboolean gst_vvas_xoverlay_generate_output (GstBaseTransform * base, GstBuffer ** outbuf)
 *  @param [in] base - Pointer to GstBaseTransform object.
 *  @param [out] outbuf - Pointer to output buffer of type GstBuffer.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  This API to draw overlay metadata on frames.  If use_bbox_accel set
 *          uses bbox accelerator IP for drawing bounding boxes.
 *  @details This API is registered with GObjectClass by overriding GstBaseTransform::generate_output
 *           function pointer and this will be called for every frame. Bases on overlay metadata it
 *           draws different geometric shapes, text and clock on frames.
 */
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
  GstVideoFormat gst_fmt;
  GstBuffer *inbuf = NULL;
  guint32 idx;
  VvasOverlayFrameInfo *ovlinfo;
  VvasVideoFrame *vframe = NULL;


  inbuf = trans->queued_buf;
  trans->queued_buf = NULL;

  if (inbuf == NULL)
    return GST_FLOW_OK;

  GST_DEBUG_OBJECT (self, "received buffer %" GST_PTR_FORMAT, inbuf);

  gst_fmt = GST_VIDEO_INFO_FORMAT (self->priv->in_vinfo);

  /* Read overlay metadata from inbuf */
  overlay_meta = gst_buffer_get_vvas_overlay_meta (inbuf);

  /* If no overlay metadata return without further processing */
  if (!overlay_meta) {
    *outbuf = inbuf;
    GST_LOG_OBJECT (self, "unable to get overlaymeta from input buffer");
    return GST_FLOW_OK;
  }

  /* Calls bbox hardware accelerator if use_bbox_accel true
     and bbox metadata available */
  if (priv->use_bbox_accel && overlay_meta->shape_info.num_rects > 0) {
    /* prepares input buffer for bbox hardware accelerator */
    bret = vvas_xoverlay_prepare_input_buffer (self, &inbuf);
    if (!bret)
      goto error;

    /* for loop to call bbox accelerator to draw in groups of MAX_BOXES
       because hardware can draw MAX_BOXES boxes at a time. */
    for (idx = 0; idx < overlay_meta->shape_info.num_rects; idx += MAX_BOXES) {
      /* Prepares bounding box info as per hardware IP */
      prepare_bbox_data (self, overlay_meta, gst_fmt, idx);

      bret = vvas_xoverlay_process (self);
      if (!bret)
        goto error;
    }
  }

  /*Allocate memory for ovlerlay */
  ovlinfo = (VvasOverlayFrameInfo *) calloc (1, sizeof (VvasOverlayFrameInfo));

  map_flags =
      (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF |
      GST_MAP_WRITE);
  /* get vvasframe form gst buffer */
  vframe = vvas_videoframe_from_gstbuffer (priv->vvas_ctx, DEFAULT_MEM_BANK,
      inbuf, self->priv->in_vinfo, map_flags);
  if (NULL == vframe) {
    GST_ERROR_OBJECT (self, "Cannot convert input GstBuffer to VvasVideoFrame");
    fret = GST_FLOW_ERROR;
    goto error;
  }

  /* Update video frame */
  ovlinfo->frame_info = vframe;

  /* Update shape info */
  memcpy (&ovlinfo->shape_info, &overlay_meta->shape_info,
      sizeof (VvasOverlayShapeInfo));

  /* update clock data */
  ovlinfo->clk_info.display_clock = self->priv->display_clock;
  ovlinfo->clk_info.clock_font_name = self->priv->clock_font_name;
  ovlinfo->clk_info.clock_font_scale = self->priv->clock_font_scale;
  ovlinfo->clk_info.clock_font_color = self->priv->clock_font_color;
  ovlinfo->clk_info.clock_x_offset = self->priv->clock_x_offset;
  ovlinfo->clk_info.clock_y_offset = self->priv->clock_y_offset;

  /* draw requested pattern on the image */
  if (VVAS_RET_SUCCESS == vvas_overlay_process_frame (ovlinfo)) {
    GST_DEBUG_OBJECT (self, "ovl process ret success");
  } else {
    /*do we need to update fret here ?  */
    GST_DEBUG_OBJECT (self, "ovl process failure");
  }

  vvas_video_frame_free (vframe);

  if (NULL != ovlinfo) {
    free (ovlinfo);
  }

  *outbuf = inbuf;

error:
  return fret;
}

/**
 *  @fn static void gst_vvas_xoverlay_class_init (GstVvas_XOverlayClass * klass)
 *  @param [in]klass  - Handle to GstVvas_XOverlayClass
 *  @return None
 *  @brief  Add properties and signals of GstVvas_XOverlay to parent GObjectClass \n
 *          and overrides function pointers present in itself and/or its parent class structures
 *  @details This function publishes properties those can be set/get from application on GstVvas_XOverlay object.
 *           And, while publishing a property it also declares type, range of acceptable values, default value,
 *           readability/writability and in which GStreamer state a property can be changed.
 */
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

/**
 *  @fn static void gst_vvas_xoverlay_init (GstVvas_XOverlay * self)
 *  @param [in] self  - Handle to GstVvas_XOverlay instance
 *  @return None
 *  @brief  Initilizes GstVvas_XOverlay member variables to default values
 *
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

/**
 *  @fn static void gst_vvas_xoverlay_set_property (GObject * object, guint prop_id,
 *                                                  const GValue * value, GParamSpec * pspec)
 *  @param [in] object - Handle to GstVvas_XOverlay typecast to GObject
 *  @param [in] prop_id - Property ID as defined in enum
 *  @param [in] value - GValue which holds property value set by user
 *  @param [in] pspec - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief This API stores values sent from the user in GstVvas_XOverlay object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::set_property function pointer and
 *           this will be invoked when developer sets properties on GstVvas_XOverlay object.
 *           Based on property value type, corresponding g_value_get_xxx API will be called to get 
 *           property value from GValue handle.
 */
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

/**
 *  @fn static void gst_vvas_xoverlay_get_property (GObject * object, guint prop_id,
 *                                                  const GValue * value, GParamSpec * pspec)
 *  @param [in] object - Handle to GstVvas_XOverlay typecast to GObject
 *  @param [in] prop_id - Property ID as defined in properties enum
 *  @param [in] value - value GValue which holds property value set by user
 *  @param [in] pspec - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief This API gets values from GstVvas_XOverlay object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::get_property function pointer and
 *           this will be invoked when developer want gets properties from GstVvas_XOverlay object.
 *           Based on property value type,corresponding g_value_set_xxx API will be called to set value of GValue type.
 */
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

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
plugin_init (GstPlugin * vvas_xoverlay)
{
  return gst_element_register (vvas_xoverlay, "vvas_xoverlay", GST_RANK_PRIMARY,
      GST_TYPE_VVAS_XOVERLAY);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vvas_xoverlay,
    "GStreamer VVAS plug-in for overlaying display text and geometric shapes",
    plugin_init, VVAS_API_VERSION, "MIT/X11", "Xilinx VVAS SDK plugin",
    "http://xilinx.com/")
