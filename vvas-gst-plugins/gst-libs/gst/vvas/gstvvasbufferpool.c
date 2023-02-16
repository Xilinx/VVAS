/*
 * Copyright 2020 - 2022 Xilinx, Inc.
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/gstvideometa.h>
#include <gst/vvas/gstvvasallocator.h>
#include "gstvvasbufferpool.h"

/**
 *  @brief Defines a static GstDebugCategory global variable "vvasallocator_debug"
 */
GST_DEBUG_CATEGORY_STATIC (gst_vvas_buffer_pool_debug);

/** @def GST_CAT_DEFAULT
 *  @brief Setting gst_vvas_buffer_pool_debug as default debug category for logging
 */
#define GST_CAT_DEFAULT gst_vvas_buffer_pool_debug

/** @def ALIGN(size,align)
 *  @param [in] size - Size value to be aligned
 *  @param [in] align - Alignment value
 *  @returns  Aligned size
 */
#define ALIGN(size,align) ((((size) + (align) - 1) / align) * align)

enum
{
  PROP_0,
  /** Stride alignment value property ID */
  PROP_STRIDE_ALIGN,
  /** Elevation alignment value property ID */
  PROP_ELEVATION_ALIGN,
};

/** @struct _GstVvasBufferPoolPrivate
 *  @brief  Holds private members related VVAS bufferpool instance
 */
struct _GstVvasBufferPoolPrivate
{
  /**Video information used to allocate memory of specific size */
  GstVideoInfo vinfo;
  /**Allocator object to allocate memory */
  GstAllocator *allocator;
  /**If TRUE, GstVideoMeta will be attached to each GstBuffer which is produced by this pool */
  gboolean add_videometa;
  /**Will be set TRUE to enable extra padding */
  gboolean need_alignment;
  /**Parameters going to be used while allocating memory */
  GstAllocationParams params;
  /**Stride value by which video frame has to be allocated */
  guint stride_align;
  /**Elevation (height alignment) value by which video frame has to be allocated */
  guint elevation_align;
  /**Video Alignment Info */
  GstVideoAlignment video_align;
};

#define parent_class gst_vvas_buffer_pool_parent_class

/** @brief  Glib's convenience macro for GstVvasBufferPool type implementation.
 *  @details This macro does below tasks:\n
 *             - Declares a class initialization function with prefix gst_vvas_buffer_pool \n
 *             - Declares an instance initialization function\n
 *             - A static variable named gst_vvas_buffer_pool_parent_class pointing to the parent class\n
 *             - Defines a gst_vvas_buffer_pool_get_type() function with below tasks\n
 *               - Initializes GTypeInfo function pointers\n
 *               - Registers GstVvasBufferPoolPrivate as private structure to GstVvasBufferPool type\n
 *               - Initialize new debug category vvasbufferpool for logging\n
 */
G_DEFINE_TYPE_WITH_CODE (GstVvasBufferPool, gst_vvas_buffer_pool,
    GST_TYPE_VIDEO_BUFFER_POOL, G_ADD_PRIVATE (GstVvasBufferPool);
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vvasbufferpool", 0,
        "VVAS buffer pool"));

/** @struct planes_vals
 *  @brief  Holds information related to video plane
 */
typedef struct
{
  /**stride value after alignment */
  gint align_stride0;
  /**elevation value after alignment */
  gint align_elevation;
  /**size of video frame after stride and elevation alignment */
  gint align_size;
  /**offsets of each plane in a video frame */
  gsize offset[GST_VIDEO_MAX_PLANES];
  /**stride value of each plane */
  gint stride[GST_VIDEO_MAX_PLANES];
} planes_vals;

/**
 *  @fn static gboolean gst_vvas_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
 *  @param [in] pool - Buffer pool pointer to get pool's stride and elevation configuration
 *  @param [in] config - Pointer which holds information calculated based on GstVideoInfo and pool
 *                       parameters
 *  @return TRUE on success\n FALSE on failure
 *  @brief Configures buffer pool with values received by parsing @config parameter
 *  @details Parses @config param to get size of each buffer in @pool, minimum and maximum buffers
 *                  required from pool and etc and updates the pool accordingly
 */
static gboolean
gst_vvas_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstVvasBufferPool *xpool;
  GstVvasBufferPoolPrivate *priv;
  GstCaps *caps;
  GstVideoInfo vinfo;
  GstAllocator *allocator;
  GstAllocationParams params;
  guint size, min_buffers, max_buffers;

  xpool = GST_VVAS_BUFFER_POOL_CAST (pool);
  priv = xpool->priv;

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers))
    goto wrong_config;

  if (!caps)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&vinfo, caps))
    goto wrong_caps;

  allocator = NULL;
  gst_buffer_pool_config_get_allocator (config, &allocator, &params);

  priv->params = params;

  /* consider allocator if its a VVAS allocator  */
  if (allocator && GST_IS_VVAS_ALLOCATOR (allocator)) {
    if (priv->allocator)
      gst_object_unref (priv->allocator);
    priv->allocator = gst_object_ref (allocator);
  }
  if (!priv->allocator)
    goto no_allocator;

  /* Attach GstVideoMeta to each buffer based on config of the pool */
  priv->add_videometa = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  /* parse extra alignment info */
  priv->need_alignment = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (priv->need_alignment && priv->add_videometa) {
    gst_buffer_pool_config_get_video_alignment (config, &priv->video_align);

    /* apply the alignment to the info */
    if (!gst_video_info_align (&vinfo, &priv->video_align))
      goto failed_to_align;

    gst_buffer_pool_config_set_video_alignment (config, &priv->video_align);
  }
  GST_LOG_OBJECT (pool,
      "fmt = %d, stride = %d, align_size = %lu",
      GST_VIDEO_INFO_FORMAT (&vinfo), vinfo.stride[0], vinfo.size);

  vinfo.size = MAX (size, vinfo.size);

  GST_LOG_OBJECT (pool, "max buffer size %lu", vinfo.size);

  gst_buffer_pool_config_set_params (config, caps, vinfo.size, min_buffers,
      max_buffers);

  priv->vinfo = vinfo;

  /* call parent GstVideoBufferPool to configure parameters */
  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);

  /* ERRORS */
wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }
wrong_caps:
  {
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
no_allocator:
  {
    GST_WARNING_OBJECT (pool, "no valid allocator in pool");
    return FALSE;
  }
failed_to_align:
  {
    GST_WARNING_OBJECT (pool, "Failed to align");
    return FALSE;
  }
}

/**
 *  @fn static GstFlowReturn gst_vvas_buffer_pool_alloc_buffer (GstBufferPool * pool,
 *                                                              GstBuffer ** buffer,
 *                                                              GstBufferPoolAcquireParams * params)
 *  @param [in] pool - VVAS buffer pool instance handle
 *  @param [out] buffer - Handle to buffer upon successful allocation
 *  @param [in] params - Parameters to be used while allocating buffer from pool
 *  @return GST_FLOW_OK on success\nGST_FLOW_ERROR on failure
 *  @brief Allocates a buffer from pool using allocator configured via gst_vvas_buffer_pool_set_config().
 *         This API will be invoked by GstBufferPool parent class and maximum number of buffers can be
 *         allocated is controlled by parameters passed to gst_vvas_buffer_pool_set_config()
 */
static GstFlowReturn
gst_vvas_buffer_pool_alloc_buffer (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstVvasBufferPool *xpool;
  GstVvasBufferPoolPrivate *priv;
  GstVideoInfo *info;

  xpool = GST_VVAS_BUFFER_POOL_CAST (pool);
  priv = xpool->priv;
  info = &priv->vinfo;

  GST_LOG_OBJECT (pool,
      "fmt = %d, stride = %d, align_elevation = %d, size = %lu",
      GST_VIDEO_INFO_FORMAT (info), info->stride[0],
      ALIGN (GST_VIDEO_INFO_HEIGHT (info), priv->elevation_align), info->size);
  GST_DEBUG_OBJECT (pool, "alloc %lu", info->size);

  /* allocate buffer using allocator received in _set_config() API */
  *buffer =
      gst_buffer_new_allocate (priv->allocator, info->size, &priv->params);
  if (*buffer == NULL) {
    if (params->flags & GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT &&
       priv->params.flags & GST_VVAS_ALLOCATOR_FLAG_DONTWAIT) {
      GST_LOG_OBJECT (pool, "unable to allocate memory as memory pool does not have memory objects");
      return GST_FLOW_EOS;
    }
    goto no_memory;
  }

  /* Attach GstVideoMeta to buffer, so that buffer users can get plane memory offsets */
  if (priv->add_videometa) {
    GstVideoAlignment alignment = { 0 };
    GstVideoMeta *video_meta;
    GstStructure *config = gst_buffer_pool_get_config (pool);

    GST_DEBUG_OBJECT (pool, "adding GstVideoMeta");

    video_meta =
        gst_buffer_add_video_meta_full (*buffer, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        info->offset, info->stride);

    if (gst_buffer_pool_config_get_video_alignment (config, &alignment)) {
      /* Set Alignment info in GstVideoMeta */
      gst_video_meta_set_alignment (video_meta, alignment);
    }

    if (config) {
      gst_structure_free (config);
    }
  }
  return GST_FLOW_OK;

  /* ERROR */
no_memory:
  {
    GST_WARNING_OBJECT (pool, "can't create memory");
    return GST_FLOW_ERROR;
  }
}

/**
 *  @fn static void gst_vvas_buffer_pool_release_buffer (GstBufferPool * pool, GstBuffer * buffer)
 *  @param [in] pool - VVAS buffer pool instance handle
 *  @param [in] buffer - Buffer to be sent back to free buffers pool
 *  @return None
 *  @brief This API calls pre and post release callbacks when the buffer is going back to pool or when
 *         it is getting unreffed
 */
static void
gst_vvas_buffer_pool_release_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  GstVvasBufferPool *xpool = GST_VVAS_BUFFER_POOL_CAST (pool);

  /* invokes user callback before buffer is going back to pool or before freeing.
   * Note : This callback is mainly useful for VCU decoder plugin to do some operations
   */
  if (xpool->pre_release_cb)
    xpool->pre_release_cb (buffer, xpool->pre_cb_user_data);

  GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (pool, buffer);

  /* invokes user callback after buffer sent back to pool or after freeing.
   * Note : This callback is mainly useful for VCU decoder plugin to do some operations
   */
  if (xpool->post_release_cb)
    xpool->post_release_cb (buffer, xpool->post_cb_user_data);

}

/**
 *  @fn static void gst_vvas_buffer_pool_set_property (GObject * object,
 *                                                     guint prop_id,
 *                                                     const GValue * value,
 *                                                     GParamSpec * pspec)
 *  @param [in] Handle to GstVvasAllocator typecasted to GObject
 *  @param [in] prop_id - Property ID value
 *  @param [in] value - GValue which holds property value set by user
 *  @param [in] pspec - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief This API stores values sent from the user in GstVvasBufferPool object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::set_property
 *           function pointer and this will be invoked when developer sets properties on
 *           GstVvasAllocator object. Based on property value type, corresponding g_value_get_xxx API
 *           will be called to get property value from GValue handle.
 */
static void
gst_vvas_buffer_pool_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvasBufferPool *xpool = GST_VVAS_BUFFER_POOL (object);

  switch (prop_id) {
    case PROP_STRIDE_ALIGN:
      xpool->priv->stride_align = g_value_get_uint (value);
      break;
    case PROP_ELEVATION_ALIGN:
      xpool->priv->elevation_align = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 *  @fn static gboolean gst_vvas_buffer_pool_start (GstBufferPool * bpool)
 *  @param [in] bpool - VVAS buffer pool instance handle
 *  @return TRUE on success\n FALSE on failure
 *  @brief Calls vvas allocator and preallocates the minimum number of memory objects
 *         and then calls parent GstBufferPool start function pointer
 */
static gboolean
gst_vvas_buffer_pool_start (GstBufferPool * bpool)
{
  GstVvasBufferPool *vvas_pool = GST_VVAS_BUFFER_POOL_CAST (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);
  GstVvasAllocator *vvas_alloc = NULL;
  GstAllocationParams *params = NULL;
  GstStructure *config = NULL;
  guint size, min_buffers, max_buffers;

  GST_DEBUG_OBJECT (bpool, "starting pool");

  /* get parameters configured on the pool in _set_config() API */
  config = gst_buffer_pool_get_config (bpool);
  if (!gst_buffer_pool_config_get_params (config, NULL, &size, &min_buffers,
          &max_buffers)) {
    GST_ERROR_OBJECT (bpool, "failed to get config from pool");
    goto error;
  }

  vvas_alloc = (GstVvasAllocator *) vvas_pool->priv->allocator;
  params = &vvas_pool->priv->params;

  /* calls vvas allocator start function to preallocate minimum number of
   * memory object */
  if (!gst_vvas_allocator_start (vvas_alloc, min_buffers, max_buffers, size,
          params)) {
    GST_ERROR_OBJECT (bpool, "failed to start buffer pool");
    goto error;
  }

  /* invoke parent start function to preallocate minimum number of buffers */
  if (!pclass->start (bpool))
    goto error;

  GST_DEBUG_OBJECT (bpool, "successfully started pool %" GST_PTR_FORMAT, bpool);
  gst_structure_free (config);
  return TRUE;

error:
  if (config)
    gst_structure_free (config);

  return FALSE;
}

/**
 *  @fn static gboolean gst_vvas_buffer_pool_stop (GstBufferPool * bpool)
 *  @param [in] bpool - VVAS buffer pool instance handle
 *  @return TRUE on success\n FALSE on failure
 *  @brief Invokes parent GstBufferPool's stop and stop on vvas allocator instance
 */
static gboolean
gst_vvas_buffer_pool_stop (GstBufferPool * bpool)
{
  GstVvasBufferPool *vvas_pool = GST_VVAS_BUFFER_POOL_CAST (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);
  gboolean bret;

  GST_DEBUG_OBJECT (bpool, "stopping pool");

  bret = pclass->stop (bpool);
  if (bret && vvas_pool->priv->allocator) {
    bret =
        gst_vvas_allocator_stop (GST_VVAS_ALLOCATOR (vvas_pool->
            priv->allocator));
  }

  return bret;
}

/**
 *  @fn static void gst_vvas_buffer_pool_reset_buffer (GstBufferPool * bpool, GstBuffer * buffer)
 *  @param [in] bpool - VVAS buffer pool instance handle
 *  @param [in] buffer - VVAS buffer
 *  @return None
 *  @brief Reset the buffer before returning it into the buffer pool.
 */
static void
gst_vvas_buffer_pool_reset_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstMemory *mem = NULL;
  GST_BUFFER_FLAGS (buffer) &= GST_BUFFER_FLAG_TAG_MEMORY;

  mem = gst_buffer_get_memory (buffer, 0);

#ifdef XLNX_PCIe_PLATFORM
  /* Let's reset the sync flags on buffers before sending them back to pool */
  if (GST_IS_VVAS_ALLOCATOR (mem->allocator)) {
    gst_vvas_memory_reset_sync_flag (mem);
  }
#endif

  gst_memory_unref (mem);

  /* call parent GstVideoBufferPool to reset buffer */
  GST_BUFFER_POOL_CLASS (parent_class)->reset_buffer (bpool, buffer);
}


/**
 *  @fn static void gst_vvas_buffer_pool_init (GstVvasBufferPool * pool)
 *  @param [in] pool - VVAS buffer pool instance handle
 *  @return None
 */
static void
gst_vvas_buffer_pool_init (GstVvasBufferPool * pool)
{
  pool->priv = gst_vvas_buffer_pool_get_instance_private (pool);
}

/**
 *  @fn static void gst_vvas_buffer_pool_finalize (GObject * object)
 *  @param [in] object - Handle to GstVvasBufferPool typecasted to GObject
 *  @return None
 *  @brief This API will be called during GstVvasBufferPool object's destruction phase.
 *         Close references to devices and free memories if any
 *  @note After this API GstVvasBufferPool object \p obj will be destroyed completely.
 *        So free all internal memories held by current object
 */
static void
gst_vvas_buffer_pool_finalize (GObject * object)
{
  GstVvasBufferPool *pool = GST_VVAS_BUFFER_POOL_CAST (object);

  GST_LOG_OBJECT (pool, "finalize vvas buffer pool %p", pool);

  if (pool->priv->allocator)
    gst_object_unref (pool->priv->allocator);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 *  @fn static void gst_vvas_buffer_pool_class_init (GstVvasBufferPoolClass * klass)
 *  @param [in]klass  - Handle to GstVvasAllocatorClass
 *  @return None
 *  @brief Add properties and signals of GstVvasBufferPool to parent GObjectClass and ovverrides
 *         function pointers present in itself and/or its parent class structures
 *  @details This function publishes properties those can be set/get from application on
 *           GstBufferPool object. And, while publishing a property it also declares type, range of
 *           acceptable values, default value, readability/writability and in which GStreamer state
 *           a property can be changed.
 */
static void
gst_vvas_buffer_pool_class_init (GstVvasBufferPoolClass * klass)
{
  GObjectClass *gobject_class;
  GstBufferPoolClass *gstbufferpool_class;

  /* override GObject function pointers */
  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = gst_vvas_buffer_pool_set_property;
  gobject_class->finalize = gst_vvas_buffer_pool_finalize;

  /* override GstBufferPool function pointers */
  gstbufferpool_class = (GstBufferPoolClass *) klass;
  gstbufferpool_class->set_config = gst_vvas_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_vvas_buffer_pool_alloc_buffer;
  gstbufferpool_class->release_buffer = gst_vvas_buffer_pool_release_buffer;
  gstbufferpool_class->start = gst_vvas_buffer_pool_start;
  gstbufferpool_class->stop = gst_vvas_buffer_pool_stop;
  gstbufferpool_class->reset_buffer = gst_vvas_buffer_pool_reset_buffer;

  /* add stride-align property */
  g_object_class_install_property (gobject_class, PROP_STRIDE_ALIGN,
      g_param_spec_uint ("stride-align", "Stride alignment of buffer",
          "Stride alignment of buffer", 0, G_MAXUINT,
          0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  /* add elevation-align property */
  g_object_class_install_property (gobject_class, PROP_ELEVATION_ALIGN,
      g_param_spec_uint ("elevation-align", "Elevation alignment of buffer",
          "Elevation alignment of buffer", 0, G_MAXUINT,
          0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
}

/**
 *  @fn GstBufferPool *gst_vvas_buffer_pool_new (guint stride_align, guint elevation_align)
 *  @param [in] stride_align - Stride alignment value to be used while allocating video frames
 *  @param [in] elevation_align - Height alignment value to be used while allocating video frames
 *  @return GstBufferPool pointer on success\n NULL on failure
 *  @brief  Allocates GstVvasBufferPool object using parameters passed to this API.
 */
GstBufferPool *
gst_vvas_buffer_pool_new (guint stride_align, guint elevation_align)
{
  GstBufferPool *pool;

  pool =
      g_object_new (GST_TYPE_VVAS_BUFFER_POOL, "stride-align", stride_align,
      "elevation-align", elevation_align, NULL);
  gst_object_ref_sink (pool);

  return pool;
}

/**
 *  @fn void gst_vvas_buffer_pool_set_pre_release_buffer_cb (GstVvasBufferPool * xpool,
 *                                                           PreReleaseBufferCallback release_buf_cb,
 *                                                           gpointer user_data)
 *  @param [in] xpool - VVAS buffer pool instance handle
 *  @param [in] release_buf_cb - Pre-release buffer back function
 *  @param [in] user_data - User data to be passed while calling @release_buf_cb
 *  @return None
 *  @brief Sets callback function to be called before releasing buffer
 */
void
gst_vvas_buffer_pool_set_pre_release_buffer_cb (GstVvasBufferPool * xpool,
    PreReleaseBufferCallback release_buf_cb, gpointer user_data)
{
  xpool->pre_release_cb = release_buf_cb;
  xpool->pre_cb_user_data = user_data;
}

/**
 *  @fn void gst_vvas_buffer_pool_set_post_release_buffer_cb (GstVvasBufferPool * xpool,
 *                                                            PostReleaseBufferCallback release_buf_cb,
 *                                                            gpointer user_data)
 *  @param [in] xpool - VVAS buffer pool instance handle
 *  @param [in] release_buf_cb - Post-release buffer back function
 *  @param [in] user_data - User data to be passed while calling @release_buf_cb
 *  @return None
 *  @brief Sets callback function to be called after releasing buffer
 */
void
gst_vvas_buffer_pool_set_post_release_buffer_cb (GstVvasBufferPool * xpool,
    PostReleaseBufferCallback release_buf_cb, gpointer user_data)
{
  xpool->post_release_cb = release_buf_cb;
  xpool->post_cb_user_data = user_data;
}
