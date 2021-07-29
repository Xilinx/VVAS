/*
 * Copyright 2020 Xilinx, Inc.
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
#include <gst/ivas/gstivasallocator.h>
#include "gstivasbufferpool.h"

GST_DEBUG_CATEGORY_STATIC (gst_ivas_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_ivas_buffer_pool_debug

#define ALIGN(size,align) (((size) + (align) - 1) & ~((align) - 1))

enum
{
  PROP_0,
  PROP_STRIDE_ALIGN,
  PROP_ELEVATION_ALIGN,
};

struct _GstIvasBufferPoolPrivate
{
  GstVideoInfo vinfo;
  GstAllocator *allocator;
  gboolean add_videometa;
  GstAllocationParams params;
  guint stride_align;
  guint elevation_align;
};

#define parent_class gst_ivas_buffer_pool_parent_class
G_DEFINE_TYPE_WITH_CODE (GstIvasBufferPool, gst_ivas_buffer_pool,
    GST_TYPE_VIDEO_BUFFER_POOL, G_ADD_PRIVATE (GstIvasBufferPool);
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "ivasbufferpool", 0,
        "IVAS buffer pool"));

typedef struct {
  gint align_stride0;
  gint align_elevation;
  gint align_size;
  gsize offset[GST_VIDEO_MAX_PLANES];
  gint stride[GST_VIDEO_MAX_PLANES];
} planes_vals;

/* size is calculated as per fill_planes() in video-info.c */
static gboolean
fill_planes_vals (GstVideoInfo * info, GstBufferPool * pool, planes_vals *plane)
{
  GstIvasBufferPool *xpool = GST_IVAS_BUFFER_POOL_CAST (pool);

  plane->align_stride0 =
      ALIGN (GST_VIDEO_INFO_PLANE_STRIDE (info, 0),
      xpool->priv->stride_align);
  plane->align_elevation =
      ALIGN (GST_VIDEO_INFO_HEIGHT (info), xpool->priv->elevation_align);

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_NV12:
      plane->stride[0] = plane->stride[1] = plane->align_stride0;
      plane->offset[0] = 0;
      plane->offset[1] = plane->offset[0] + plane->stride[0] * plane->align_elevation;
      plane->align_size = (plane->align_stride0 * plane->align_elevation * 3) >> 1;
      break;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_r210:
    case GST_VIDEO_FORMAT_Y410:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
      plane->stride[0] = plane->align_stride0;
      plane->offset[0] = 0;
      plane->align_size = plane->stride[0] * plane->align_elevation;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      plane->stride[0] = plane->align_stride0;
      plane->offset[0] = 0;
      plane->align_size = plane->stride[0] * plane->align_elevation;
      break;
    case GST_VIDEO_FORMAT_NV16:
      plane->stride[0] = plane->align_stride0;
      plane->stride[1] = plane->stride[0];
      plane->offset[0] = 0;
      plane->offset[1] = plane->stride[0] * plane->align_elevation;
      plane->align_size = plane->stride[0] * plane->align_elevation * 2;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_v308:
    case GST_VIDEO_FORMAT_BGR:
      plane->stride[0] = plane->align_stride0;
      plane->offset[0] = 0;
      plane->align_size = plane->stride[0] * plane->align_elevation;
      break;
    case GST_VIDEO_FORMAT_I422_10LE:
      plane->stride[0] = plane->align_stride0;
      plane->stride[1] = ALIGN (GST_VIDEO_INFO_PLANE_STRIDE (info, 1),
		      xpool->priv->stride_align);
      plane->stride[2] = plane->stride[1];
      plane->offset[0] = 0;
      plane->offset[1] = plane->stride[0] * plane->align_elevation;
      plane->offset[2] = plane->offset[1] + plane->stride[1] * plane->align_elevation;
      plane->align_size = plane->offset[2] + plane->stride[2] * plane->align_elevation;
      break;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      plane->stride[0] = plane->stride[1] = plane->align_stride0;
      plane->offset[0] = 0;
      plane->offset[1] = plane->offset[0] + plane->stride[0] * plane->align_elevation;
      plane->align_size = (plane->align_stride0 * plane->align_elevation * 3) >> 1;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      plane->stride[0] = plane->align_stride0;
      plane->offset[0] = 0;
      plane->align_size = plane->stride[0] * plane->align_elevation;
      break;
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      plane->stride[0] = plane->align_stride0;
      plane->offset[0] = 0;
      plane->align_size = plane->stride[0] * GST_ROUND_UP_2(plane->align_elevation);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      plane->stride[0] = plane->align_stride0;
      plane->offset[0] = 0;
      plane->align_size = plane->stride[0] * plane->align_elevation;
      break;
    case GST_VIDEO_FORMAT_I420:
      plane->stride[0] = plane->align_stride0;
      plane->stride[1] = ALIGN (GST_VIDEO_INFO_PLANE_STRIDE (info, 1),
		             xpool->priv->stride_align);
      plane->stride[2] = plane->stride[1];
      plane->offset[0] = 0;
      plane->offset[1] = plane->stride[0] * plane->align_elevation;
      plane->offset[2] = plane->offset[1] + plane->stride[1] * (plane->align_elevation / 2);
      plane->align_size = plane->offset[2] + plane->stride[2] * (plane->align_elevation / 2);
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
      plane->stride[0] = plane->align_stride0;
      plane->stride[1] = ALIGN (GST_VIDEO_INFO_PLANE_STRIDE (info, 1),
		             xpool->priv->stride_align);
      plane->stride[2] = plane->stride[1];
      plane->offset[0] = 0;
      plane->offset[1] = plane->stride[0] * (plane->align_elevation);
      plane->offset[2] = plane->offset[1] + plane->stride[1] * (plane->align_elevation / 2);
      plane->align_size = plane->offset[2] + plane->stride[2] * (plane->align_elevation / 2);
      break;
    default:
      GST_ERROR_OBJECT (pool, "not yet supporting format %d", GST_VIDEO_INFO_FORMAT (info));
      return FALSE;
  }

 return TRUE;

}

static gboolean
gst_ivas_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstIvasBufferPool *xpool;
  GstIvasBufferPoolPrivate *priv;
  GstCaps *caps;
  GstVideoInfo vinfo;
  GstAllocator *allocator;
  GstAllocationParams params;
  guint size, min_buffers, max_buffers;
  planes_vals plane;

  xpool = GST_IVAS_BUFFER_POOL_CAST (pool);
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

  /* not our allocator, not our buffers */
  if (allocator && GST_IS_IVAS_ALLOCATOR (allocator)) {
    if (priv->allocator)
      gst_object_unref (priv->allocator);
    priv->allocator = gst_object_ref (allocator);
  }
  if (!priv->allocator)
    goto no_allocator;

  /* enable metadata based on config of the pool */
  priv->add_videometa = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  vinfo.size = MAX (size, vinfo.size);

  if (fill_planes_vals (&vinfo, pool, &plane) != TRUE)
    return FALSE;

  vinfo.size = MAX (plane.align_size, vinfo.size);

  GST_LOG_OBJECT (pool,
      "fmt = %d, stride = %d, align_elevation = %d, align_size = %d",
      GST_VIDEO_INFO_FORMAT (&vinfo), plane.stride[0],
      plane.align_elevation, plane.align_size);
  GST_LOG_OBJECT (pool, "max buffer size %lu", vinfo.size);

  gst_buffer_pool_config_set_params (config, caps, vinfo.size, min_buffers,
      max_buffers);

  priv->vinfo = vinfo;

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
}

static GstFlowReturn
gst_ivas_buffer_pool_alloc_buffer (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstIvasBufferPool *xpool;
  GstIvasBufferPoolPrivate *priv;
  GstVideoInfo *info;
  planes_vals plane;

  xpool = GST_IVAS_BUFFER_POOL_CAST (pool);
  priv = xpool->priv;
  info = &priv->vinfo;

  if (fill_planes_vals (info, pool, &plane) != TRUE)
    g_assert_not_reached ();

  GST_LOG_OBJECT (pool,
      "fmt = %d, stride = %d, align_elevation = %d, size = %lu",
      GST_VIDEO_INFO_FORMAT (info), plane.stride[0], plane.align_elevation, info->size);
  GST_DEBUG_OBJECT (pool, "alloc %lu", info->size);

  *buffer =
      gst_buffer_new_allocate (priv->allocator, info->size, &priv->params);
  if (*buffer == NULL)
    goto no_memory;

  if (priv->add_videometa) {
    GST_DEBUG_OBJECT (pool, "adding GstVideoMeta");

    gst_buffer_add_video_meta_full (*buffer, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info),
        GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info),
        GST_VIDEO_INFO_N_PLANES (info), plane.offset, plane.stride);
  }
  return GST_FLOW_OK;

  /* ERROR */
no_memory:
  {
    GST_WARNING_OBJECT (pool, "can't create memory");
    return GST_FLOW_ERROR;
  }
}

static void
gst_ivas_buffer_pool_release_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  GstIvasBufferPool *xpool = GST_IVAS_BUFFER_POOL_CAST (pool);

  if (xpool->pre_release_cb)
    xpool->pre_release_cb (buffer, xpool->pre_cb_user_data);

  GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (pool, buffer);

  if (xpool->post_release_cb)
    xpool->post_release_cb (buffer, xpool->post_cb_user_data);

}

static void
gst_ivas_buffer_pool_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIvasBufferPool *xpool = GST_IVAS_BUFFER_POOL (object);

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

static gboolean
gst_ivas_buffer_pool_start (GstBufferPool * bpool)
{
  GstIvasBufferPool *ivas_pool = GST_IVAS_BUFFER_POOL_CAST (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);
  GstIvasAllocator *ivas_alloc = NULL;
  GstAllocationParams *params = NULL;
  GstStructure *config = NULL;
  guint size, min_buffers, max_buffers;

  GST_DEBUG_OBJECT (bpool, "starting pool");

  config = gst_buffer_pool_get_config (bpool);
  if (!gst_buffer_pool_config_get_params (config, NULL, &size, &min_buffers,
          &max_buffers)) {
    GST_ERROR_OBJECT (bpool, "failed to get config from pool");
    goto error;
  }

  ivas_alloc = (GstIvasAllocator *) ivas_pool->priv->allocator;
  params = &ivas_pool->priv->params;

  if (!gst_ivas_allocator_start (ivas_alloc, min_buffers, max_buffers, size,
          params)) {
    GST_ERROR_OBJECT (bpool, "failed to start buffer pool");
    goto error;
  }

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

static gboolean
gst_ivas_buffer_pool_stop (GstBufferPool * bpool)
{
  GstIvasBufferPool *ivas_pool = GST_IVAS_BUFFER_POOL_CAST (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);
  gboolean bret;

  GST_DEBUG_OBJECT (bpool, "stopping pool");

  bret = pclass->stop (bpool);
  if (bret && ivas_pool->priv->allocator) {
    bret =
        gst_ivas_allocator_stop (GST_IVAS_ALLOCATOR (ivas_pool->
            priv->allocator));
  }

  return bret;
}

static void
gst_ivas_buffer_pool_init (GstIvasBufferPool * pool)
{
  pool->priv = gst_ivas_buffer_pool_get_instance_private (pool);
}

static void
gst_ivas_buffer_pool_finalize (GObject * object)
{
  GstIvasBufferPool *pool = GST_IVAS_BUFFER_POOL_CAST (object);

  GST_LOG_OBJECT (pool, "finalize ivas buffer pool %p", pool);

  if (pool->priv->allocator)
    gst_object_unref (pool->priv->allocator);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ivas_buffer_pool_class_init (GstIvasBufferPoolClass * klass)
{
  GObjectClass *gobject_class;
  GstBufferPoolClass *gstbufferpool_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = gst_ivas_buffer_pool_set_property;
  gobject_class->finalize = gst_ivas_buffer_pool_finalize;

  gstbufferpool_class = (GstBufferPoolClass *) klass;
  gstbufferpool_class->set_config = gst_ivas_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_ivas_buffer_pool_alloc_buffer;
  gstbufferpool_class->release_buffer = gst_ivas_buffer_pool_release_buffer;
  gstbufferpool_class->start = gst_ivas_buffer_pool_start;
  gstbufferpool_class->stop = gst_ivas_buffer_pool_stop;

  g_object_class_install_property (gobject_class, PROP_STRIDE_ALIGN,
      g_param_spec_uint ("stride-align", "Stride alignment of buffer",
          "Stride alignment of buffer", 0, G_MAXUINT,
          0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ELEVATION_ALIGN,
      g_param_spec_uint ("elevation-align", "Elevation alignment of buffer",
          "Elevation alignment of buffer", 0, G_MAXUINT,
          0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
}

GstBufferPool *
gst_ivas_buffer_pool_new (guint stride_align, guint elevation_align)
{
  GstBufferPool *pool;

  pool =
      g_object_new (GST_TYPE_IVAS_BUFFER_POOL, "stride-align", stride_align,
      "elevation-align", elevation_align, NULL);
  gst_object_ref_sink (pool);

  return pool;
}

void
gst_ivas_buffer_pool_set_pre_release_buffer_cb (GstIvasBufferPool * xpool,
    PreReleaseBufferCallback release_buf_cb, gpointer user_data)
{
  xpool->pre_release_cb = release_buf_cb;
  xpool->pre_cb_user_data = user_data;
}

void
gst_ivas_buffer_pool_set_post_release_buffer_cb (GstIvasBufferPool * xpool,
    PostReleaseBufferCallback release_buf_cb, gpointer user_data)
{
  xpool->post_release_cb = release_buf_cb;
  xpool->post_cb_user_data = user_data;
}
