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
#include <vvas_core/vvas_device.h>
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

/** @def DEFAULT_KERNEL_NAME
 *  @brief Default kernel name of optical flow IP
 */
#define DEFAULT_KERNEL_NAME "dense_non_pyr_of_accel:{dense_non_pyr_of_accel_1}"

#ifdef XLNX_PCIe_PLATFORM
/**
 *  @brief Default value of the device index in case user has not provided
 */
#define DEFAULT_DEVICE_INDEX -1
/**
 *  @brief Default no dma buffer in PCIe
 */
#define USE_DMABUF 0
#else
/**
 *  @brief On Embedded only one device i.e. device 0
 */
#define DEFAULT_DEVICE_INDEX 0
/**
 *  @brief Default use dma buffer in Embedded
 */
#define USE_DMABUF 1
#endif

/** @def MIN_POOL_BUFFERS
 *  @brief Allocates minimum number of meta buffers during initialization
*/

#define MIN_POOL_BUFFERS 2

/** @def OPT_FLOW_TIMEOUT
 *  @brief Default timeout for optical flow IP
 */
#define OPT_FLOW_TIMEOUT 1000   // 1 sec

/**
 *  @brief Defines a static GstDebugCategory global variable "gst_vvas_xoptflow_debug"
 */
GST_DEBUG_CATEGORY_STATIC (gst_vvas_xoptflow_debug);

/** @def GST_CAT_DEFAULT
 *  @brief Setting gst_vvas_xoptflow_debug as default debug category for logging
 */
#define GST_CAT_DEFAULT gst_vvas_xoptflow_debug

/**
 *  @brief Defines a static GstDebugCategory global variable with name GST_CAT_PERFORMANCE for
 *         performance logging purpose
 */
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

typedef struct _GstVvas_XOptflowPrivate GstVvas_XOptflowPrivate;

/** @enum
 *  @brief  Contains properties related to optical flow configuration
 */
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
};

/**
 *  @brief Defines sink pad template
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("NV12")));

/**
 *  @brief Defines source pad template
 */
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("NV12")));

/** @struct _GstVvas_XOptflowPrivate
 *  @brief  Holds private members related optical flow
 */
struct _GstVvas_XOptflowPrivate
{
  /** Index of the device on which memory is going to allocated */
  guint dev_idx;
  /** Name of the optical flow kernel */
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
  /** flag to indicate first processing frame or not */
  gboolean first_frame;
  /** reference to previous frame GstBuffer pointer */
  GstBuffer *preserve_buf;
  /** Current frame physical address */
  volatile guint64 img_curr_phy_addr;
  /** Previous frame physical address */
  volatile guint64 img_prev_phy_addr;
  /** Two buffers physical address for storing x and y displacement */
  volatile guint64 optflow_disp_phy_addr[2];
  /** Two buffers virtual address for storing x and y displacement */
  GstBuffer *outbufs[2];
  /** Pointer to the info of input caps */
  GstVideoInfo *in_vinfo;
  /** Pointer to the input buffer pool */
  GstBufferPool *in_pool;
  /** Pointer to the optical flow metadata buffer pool*/
  GstBufferPool *meta_pool;
  uint32_t stride;
};

/** @brief  Glib's convenience macro for GstVvas_XOptflow type implementation.
 *  @details This macro does below tasks:\n
 *           - Declares a class initialization function with prefix gst_vvas_xoptflow
 */
#define gst_vvas_xoptflow_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_XOptflow, gst_vvas_xoptflow,
    GST_TYPE_BASE_TRANSFORM);

/** @def GST_VVAS_XOPTFLOW_PRIVATE(self)
 *  @brief Get instance of GstVvas_XOptflowPrivate structure
 */
#define GST_VVAS_XOPTFLOW_PRIVATE(self) (GstVvas_XOptflowPrivate *) (gst_vvas_xoptflow_get_instance_private (self))

/* Functions declaration */
static void gst_vvas_xoptflow_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_vvas_xoptflow_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean vvas_xoptflow_allocate_meta_output_pool (GstVvas_XOptflow *
    self);

static gboolean vvas_xoptflow_prepare_meta_buffers (GstVvas_XOptflow * self);

/**
 *  @fn gboolean gst_vvas_xoptflow_set_caps (GstBaseTransform * trans,
 *                                           GstCaps * incaps,
 *                                           GstCaps * outcaps)
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
gst_vvas_xoptflow_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVvas_XOptflow *self = GST_VVAS_XOPTFLOW (trans);
  gboolean bret = TRUE;
  GstVvas_XOptflowPrivate *priv = self->priv;

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
 *  @fn gboolean vvas_xoptflow_allocate_meta_output_pool (GstVvas_XOptflow * self)
 *  @param [inout] self - Pointer to GstVvas_XOptflow structure.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Creates buffer pool for allocating memory for optical flow metadata.
 *  @note  Create optical flow meta pool structure based on frame dimensions. Since
 *         metadata buffers required by optical flow IP is of type float, the size allocated
 *         will be four times the actual size.
 *
 */
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

  /* Create caps based on info from sink pad */
  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "GRAY8",
      "width", G_TYPE_INT, self->priv->stride,
      "height", G_TYPE_INT, GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo), NULL);

  /* Parses caps and updates info */
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  /* size of each buffer. Here input frame type is float */
  size = self->priv->stride * priv->in_vinfo->height * sizeof (float);  //4

  /* create allocator to allocate from specific memory bank for the device */
  allocator = gst_vvas_allocator_new (priv->dev_idx, USE_DMABUF,
      self->in_mem_bank);
  params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
  params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;

  /* Creates a buffer pool for allocating memory to metadata frames */
  priv->meta_pool = gst_vvas_buffer_pool_new (1, 1);

  GST_LOG_OBJECT (self, "allocated preprocess output pool %" GST_PTR_FORMAT
      "output allocator %" GST_PTR_FORMAT, priv->meta_pool, allocator);

  /* Gets a copy of the current configuration of the pool */
  structure = gst_buffer_pool_get_config (priv->meta_pool);

  /* Updates structure with parameters */
  gst_buffer_pool_config_set_params (structure, caps, size, 4, 0);
  gst_buffer_pool_config_add_option (structure,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  /* Updates structure with allocator and allocator parameters */
  gst_buffer_pool_config_set_allocator (structure, allocator, &params);

  if (allocator)
    gst_object_unref (allocator);

  if (caps)
    gst_caps_unref (caps);

  /* Sets the configuration of the pool */
  if (!gst_buffer_pool_set_config (priv->meta_pool, structure)) {
    GST_ERROR_OBJECT (self, "failed to configure pool");
    GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to configure pool."),
        ("failed to configure preprocess output pool"));
    return FALSE;
  }

  /* make pool active for allocating buffers */
  if (!gst_buffer_pool_set_active (priv->meta_pool, TRUE)) {
    GST_ERROR_OBJECT (self, "failed to activate preprocess output pool");
    GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to activate pool."),
        ("failed to activate preprocess output pool"));
    return FALSE;
  }

  return TRUE;
}

/**
 *  @fn gboolean vvas_xoptflow_init (GstVvas_XOptflow * self)
 *  @param [inout] self - Pointer to GstVvas_XOptflow structure.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Opens XRT context for optical flow kernel
 *  @note  Using opticalflow kernel name and location downloads xclbin.
 *         Opens a context for XRT
 *
 */
static gboolean
vvas_xoptflow_init (GstVvas_XOptflow * self)
{
  GstVvas_XOptflowPrivate *priv = self->priv;
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

  /* create FPGA kernel object instance of optical flow */
  iret = vvas_xrt_open_context (priv->dev_handle, priv->xclbinId,
      &priv->kern_handle, priv->kern_name, true);
  if (iret) {
    GST_ERROR_OBJECT (self, "failed to open XRT context ...:%d", iret);
    return FALSE;
  }

  return TRUE;
}

/**
 *  @fn gboolean gst_vvas_xoptflow_start (GstBaseTransform * trans)
 *  @param [in] trans - Pointer to GstBaseTransform object.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Opens device handle and creates context for XRT.
 *  @details This API is registered with GObjectClass by overriding GstBaseTransform::start function pointer and
 *          this will be called when element start processing. It opens device context and allocates memory.
 */
static gboolean
gst_vvas_xoptflow_start (GstBaseTransform * trans)
{
  GstVvas_XOptflow *self = GST_VVAS_XOPTFLOW (trans);
  GstVvas_XOptflowPrivate *priv = self->priv;
  self->priv = priv;
  priv->in_vinfo = gst_video_info_new ();

  /* create XRT object instance */
  if (!vvas_xrt_open_device (priv->dev_idx, &priv->dev_handle)) {
    GST_ERROR_OBJECT (self, "failed to open device index %u", priv->dev_idx);
    return FALSE;
  }

  /* initialize opticalflow context */
  if (!vvas_xoptflow_init (self)) {
    GST_ERROR_OBJECT (self, "unable to initialize opticalflow context");
    return FALSE;
  }

  GST_INFO_OBJECT (self, "start completed");
  return TRUE;
}

/**
 *  @fn gboolean vvas_xoptflow_deinit (GstVvas_XOptflow * self)
 *  @param [inout] self - Pointer to GstVvas_XOptflow structure.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  API frees memory allocated and closes device context
 *  @note  Frees all memories allocated including input and metadata pools and
 *         closes context of XRT.
 *
 */
static gboolean
vvas_xoptflow_deinit (GstVvas_XOptflow * self)
{
  GstVvas_XOptflowPrivate *priv = self->priv;
  gint cu_idx = -1;

  if (priv->kern_name)
    free (priv->kern_name);

  /* free the input buffer pool */
  if (priv->in_pool && gst_buffer_pool_is_active (priv->in_pool)) {
    if (!gst_buffer_pool_set_active (priv->in_pool, FALSE)) {
      GST_ERROR_OBJECT (self, "failed to deactivate internal input pool");
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to deactivate pool."),
          ("failed to deactivate internal input pool"));
      return FALSE;
    }
  }

  /* free the metadata buffer pool */
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

/**
 *  @fn gboolean gst_vvas_xoptflow_stop (GstBaseTransform * trans)
 *  @param [in] trans - Pointer to GstBaseTransform object.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Free up allocates memory and unref the previous buffer.
 *  @details This API is registered with GObjectClass by overriding GstBaseTransform::stop function pointer and
 *          this will be called when element stops processing.
 *          It invokes optical flow de-initialization and free up allocated memory.
 *
 */
static gboolean
gst_vvas_xoptflow_stop (GstBaseTransform * trans)
{
  GstVvas_XOptflow *self = GST_VVAS_XOPTFLOW (trans);

  GST_DEBUG_OBJECT (self, "stopping");

  /* free the reference of previous buffer required to free the memory */
  if (self->priv->preserve_buf)
    gst_buffer_unref (self->priv->preserve_buf);

  if (self->priv->in_vinfo) {
    gst_video_info_free (self->priv->in_vinfo);
    self->priv->in_vinfo = NULL;
  }
  vvas_xoptflow_deinit (self);

  return TRUE;
}

/**
 *  @fn static void gst_vvas_xoptflow_finalize (GObject * obj)
 *  @param [in] Handle to GstVvas_XOptflow typecast to GObject
 *  @return None
 *  @brief This API will be called during GstVvas_XOptflow object's destruction phase. Close references
 *         to devices and free memories if any
 *  @note After this API GstVvas_XOptflow object \p obj will be destroyed completely. So free all internal
 *        memories held by current object
 */
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

/**
 *  @fn gboolean vvas_xoptflow_allocate_internal_pool (GstVvas_XOptflow * self)
 *  @param [inout] self - Pointer to GstVvas_XOptflow structure.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Creates buffer pool for allocating memory when required for input buffers.
 *  @note  Create input pool structure based on info from sink pad caps.
 *
 */
static gboolean
vvas_xoptflow_allocate_internal_pool (GstVvas_XOptflow * self)
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
  alloc_params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;
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

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "failed to activate preprocess input pool");
    GST_ELEMENT_ERROR (self, STREAM, FAILED, ("failed to activate pool."),
        ("failed to activate preprocess input pool"));
    return FALSE;
  }

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
 *  @fn gboolean vvas_xoptflow_prepare_input_buffer (GstVvas_XOptflow * self,
 *                           GstBuffer ** inbuf)
 *  @param [inout] self - Pointer to GstVvas_XOptflow structure.
 *  @param [in] inbuf - Pointer to input buffer of type GstBuffer.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Prepares input buffer required for hardware IP.
 *  @note  If input buffer is hardware buffer (vvas or DMA)physical address is obtain from memory structure.
 *         If not a hardware buffer will be created from input pool to copy input buffer.
 *
 */
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

  /* Get GstMemory info from input buffer */
  in_mem = gst_buffer_get_memory (*inbuf, 0);
  if (!in_mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    goto error;
  }

  /* check for vvas memory type */
  if (gst_is_vvas_memory (in_mem)
      && gst_vvas_memory_can_avoid_copy (in_mem, self->priv->dev_idx,
          self->in_mem_bank)) {
    phy_addr = gst_vvas_allocator_get_paddr (in_mem);
  } else if (gst_is_dmabuf_memory (in_mem)) {
    /* check for dma buffer type */
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
    /* set to allocate memory for pool if memory is not of type vvas or dma */
    use_inpool = TRUE;
  }

  if (use_inpool) {
    /* Check if input pool created If not creates input pool */
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
      gst_video_frame_unmap (&inpool_vframe);
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

  self->priv->img_curr_phy_addr = phy_addr;

  return TRUE;

error:
  if (in_mem)
    gst_memory_unref (in_mem);

  return FALSE;
}

/**
 *  @fn gboolean vvas_xoptflow_prepare_meta_buffers (GstVvas_XOptflow * self)
 *  @param [inout] self - Pointer to GstVvas_XOptflow structure.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Prepares meta buffer required for hardware IP.
 *  @details  Creates two meta buffers of type float for meta pool for
 *         storing x and y displacements.
 *
 */
static gboolean
vvas_xoptflow_prepare_meta_buffers (GstVvas_XOptflow * self)
{
  GstMemory *mem = NULL;
  GstVvas_XOptflowPrivate *priv = self->priv;
  guint flow_id;

  /* Create new meta buffer pool if it is not created */
  if (!priv->meta_pool) {
    if (!vvas_xoptflow_allocate_meta_output_pool (self)) {
      GST_ERROR_OBJECT (self, "failed to create meta buffer pool");
      goto error;
    }
  }

  /* for loop to create two meta buffers */
  for (flow_id = 0; flow_id < 2; flow_id++) {
    GstBuffer *outbuf = NULL;
    guint64 phy_addr = -1;
    GstFlowReturn fret;

    /* acquire buffer from the pool */
    fret = gst_buffer_pool_acquire_buffer (priv->meta_pool, &outbuf, NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
          priv->meta_pool);
      goto error;
    }
    GST_LOG_OBJECT (self, "acquired buffer %p from pool %p", outbuf,
        priv->meta_pool);

    /* Add dimensions of buffers as metadata */
    gst_buffer_add_video_meta (outbuf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_FORMAT_GRAY8, priv->stride, priv->in_vinfo->height);

    priv->outbufs[flow_id] = outbuf;

    /* Get GstMemory object from input GstBuffer */
    mem = gst_buffer_get_memory (outbuf, 0);
    if (mem == NULL) {
      GST_ERROR_OBJECT (self, "failed to get memory from output buffer");
      goto error;
    }

    /* Get physical address of buffer */
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

/**
 *  @fn int32_t vvas_xoptflow_exec_buf (vvasDeviceHandle dev_handle,
 *      vvasKernelHandle kern_handle, vvasRunHandle * run_handle,
 *            const char *format, ...)
 *  @param [in] dev_handle - Handle to XRT device object created for specified device index
 *  @param [in] kern_handle - Handle to FPGA kernel object instance created for optical flow
 *  @param [in] vvasRunHandle - Handle to kernel execution object
 *  @param [in] format - arguments descriptor
 *  @return return 0 on success\n
 *          1 on failure.
 *
 *  @brief  Function to call execution of optical flow hardware IP.
 *  @details Calls execution function using argument list as input
 */
static int32_t
vvas_xoptflow_exec_buf (vvasDeviceHandle dev_handle,
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
 *  @fn gboolean vvas_xoptflow_process (GstVvas_XOptflow * self)
 *  @param [inout] self - Handle to GstVvas_XOptflow instance
 *  @return TRUE on success\n
 *          FALSE on failure.
 *
 *  @brief  Programmes hardware IP to estimate optical flow data.
 *  @details Programmes hardware IP with current and previous frame.
 *           Waits for buffer to process and generate x and y direction
 *           optical flow data.  It process time exceeds OPT_FLOW_TIMEOUT
 *           return FALSE.
 */
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

  /* check return value from hardware execution */
  if (iret) {
    GST_ERROR_OBJECT (self, "failed to execute command %d", iret);
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, NULL,
        ("failed to issue execute command. reason : %s", strerror (errno)));
    return FALSE;
  }

  /* Checks for completion of processing.  If processing is taking
     more than predefined time return FALSE */
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

  /* for loop to check validity of buffers from hardware IP and
     syncs them from device */
  for (flow_id = 0; flow_id < 2; flow_id++) {
    mem = gst_buffer_get_memory (priv->outbufs[flow_id], 0);
    if (mem == NULL) {
      GST_ERROR_OBJECT (self,
          "flow-%d : failed to get memory from output buffer", flow_id);
      return FALSE;
    }
    gst_vvas_memory_set_sync_flag (mem, VVAS_SYNC_FROM_DEVICE);
    gst_memory_unref (mem);
  }
  return TRUE;
}

/**
 *  @fn gboolean vvas_xoptflow_add_as_meta (GstVvas_XOptflow * self, GstBuffer * outbuf)
 *  @param [in] self - Handle to GstVvas_XOptflow instance
 *  @param [out] outbuf - outbuf added with optical flow metadata.
 *  @return TRUE on success\n
 *          FALSE on failure.
 *
 *  @brief  Adds x and y displacement buffers of optical flow as metadata to outbuf.
 *          These buffers are added as type GstVvasOFMeta.
 */
static gboolean
vvas_xoptflow_add_as_meta (GstVvas_XOptflow * self, GstBuffer * outbuf)
{
  GstVvasOFMeta *of_meta;

  /* creates structure GstVvasOFMeta to add optical flow meta */
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

/**
 *  @fn gboolean gst_vvas_xoptflow_generate_output (GstBaseTransform * base, GstBuffer ** outbuf)
 *  @param [in] base - Pointer to GstBaseTransform object.
 *  @param [out] outbuf - Pointer to output buffer of type GstBuffer.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  This API estimates opticalflow using current and previous frame.
 *  @details This API is registered with GObjectClass by overriding GstBaseTransform::generate_output
 *          function pointer and this will be called for every frame. It prepares the input buffer and two meta buffers
 *          for storing x and y displacement.  It estimates optical flow using hardware IP and
 *          attaches the generated displacement data as opticalflow metadata.
 */
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

  /* copies address from img_curr_phy_addr to img_prev_phy_addr */
  priv->img_prev_phy_addr = priv->img_curr_phy_addr;

  /* prepares input buffer for processing */
  if (!vvas_xoptflow_prepare_input_buffer (self, &inbuf)) {
    GST_ERROR_OBJECT (self, "failed to prepare input buffer");
    goto error;
  }

  /* prepares metadata buffer for storing optical flow */
  if (!vvas_xoptflow_prepare_meta_buffers (self)) {
    GST_ERROR_OBJECT (self, "failed to prepare meta buffers");
    goto error;
  }

  if (priv->first_frame) {
    /* For first frame no metadata is available hence set metadata
       buffers to zero */
    if (!gst_buffer_map (priv->outbufs[0], &info, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "failed to make meta buffer writeable");
      goto error;
    }
    memset (info.data, 0x0, info.size);

    gst_buffer_unmap (priv->outbufs[0], &info);

    if (!gst_buffer_map (priv->outbufs[1], &info, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "failed to make meta buffer writeable");
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

  /* Make input buffer writeable to attach metadata. If unable to make
     input buffer writeable, creates a copy of input buffer */
  w_buf = gst_buffer_make_writable (inbuf);
  bret = vvas_xoptflow_add_as_meta (self, w_buf);

  /* preserve previous buffer required for processing next frame by
     keeping a reference of it */
  priv->preserve_buf = gst_buffer_ref (w_buf);
  GST_DEBUG_OBJECT (self, "prev-buffer ref %" GST_PTR_FORMAT,
      priv->preserve_buf);

  *outbuf = w_buf;
error:
  return fret;
}

/**
 *  @fn static void gst_vvas_xoptflow_class_init (GstVvas_XOptflowClass * klass)
 *  @param [in]klass  - Handle to GstVvas_XOptflowClass
 *  @return None
 *  @brief  Add properties and signals of GstVvas_XOptflow to parent GObjectClass \n
 *          and overrides function pointers present in itself and/or its parent class structures
 *  @details This function publishes properties those can be set/get from application on GstVvas_XOptflow object.
 *           And, while publishing a property it also declares type, range of acceptable values, default value,
 *           readability/writability and in which GStreamer state a property can be changed.
 */
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

  /* Get device index */
  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("dev-idx", "Device index",
          "Valid Device index is 0 to 31. Default value is set to -1 for PCI and 0 for embedded",
          -1, 31, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* xclbin location */
  g_object_class_install_property (gobject_class, PROP_XCLBIN_LOCATION,
      g_param_spec_string ("xclbin-location", "xclbin file location",
          "Location of the xclbin to program device", NULL,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  /* memory bank to allocate memory */
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

/**
 *  @fn static void gst_vvas_xoptflow_init (GstVvas_XOptflow * self)
 *  @param [in] self  - Handle to GstVvas_XOptflow instance
 *  @return None
 *  @brief  Initializes GstVvas_XOptflow member variables to default values
 *
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

/**
 *  @fn static void gst_vvas_xoptflow_set_property (GObject * object, guint prop_id,
 *                                              const GValue * value, GParamSpec * pspec)
 *  @param [in] object - Handle to GstVvas_XOptflow typecast to GObject
 *  @param [in] prop_id - Property ID as defined in enum
 *  @param [in] value - GValue which holds property value set by user
 *  @param [in] pspec - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief This API stores values sent from the user in GstVvas_XOptflow object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::set_property function pointer and
 *            this will be invoked when developer sets properties on GstVvas_XOptflow object.
 *            Based on property value type, corresponding g_value_get_xxx API will be called to
 *            get property value from GValue handle.
 */
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

/**
 *  @fn static void gst_vvas_xoptflow_get_property (GObject * object, guint prop_id,
 *                                                  const GValue * value, GParamSpec * pspec)
 *  @param [in] object - Handle to GstVvas_XOptflow typecast to GObject
 *  @param [in] prop_id - Property ID as defined in properties enum
 *  @param [in] value - value GValue which holds property value set by user
 *  @param [in] pspec - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief This API gets values from GstVvas_XOptflow object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::get_property function pointer and
 *           this will be invoked when developer want gets properties from GstVvas_XOptflow object.
 *           Based on property value type,corresponding g_value_set_xxx API will be called to set value of GValue type.
 */
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

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
plugin_init (GstPlugin * vvas_xoptflow)
{
  return gst_element_register (vvas_xoptflow, "vvas_xoptflow", GST_RANK_PRIMARY,
      GST_TYPE_VVAS_XOPTFLOW);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vvas_xoptflow,
    "GStreamer VVAS plug-in for optical flow",
    plugin_init, "1.0", "MIT/X11",
    "Xilinx VVAS SDK plugin", "http://xilinx.com/")
