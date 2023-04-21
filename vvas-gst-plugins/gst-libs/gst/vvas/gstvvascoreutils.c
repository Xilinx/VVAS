#include <gst/vvas/gstvvascoreutils.h>


typedef struct
{
  GstBuffer *buf;
  GstMapInfo map_info;
  GstVideoFrame *vframe;
} VvasGstUserData;


VvasLogLevel
vvas_get_core_log_level (GstDebugLevel gst_level)
{
  switch (gst_level) {
    case GST_LEVEL_NONE:
    case GST_LEVEL_ERROR:
      return LOG_LEVEL_ERROR;
    case GST_LEVEL_WARNING:
    case GST_LEVEL_FIXME:
      return LOG_LEVEL_WARNING;
    case GST_LEVEL_INFO:
      return LOG_LEVEL_INFO;
    default:
      return LOG_LEVEL_DEBUG;
  }
}

static bool
prediction_node_assign (const VvasTreeNode * node, void *data)
{
  VvasInferPrediction *dmeta = NULL;
  if (NULL == node) {
    return false;
  }

  dmeta = (VvasInferPrediction *) (node->data);
  if (dmeta->node) {
    //vvas_treenode_destroy (dmeta->node);
    vvas_treenode_destroy (dmeta->node);
  }
  dmeta->node = (VvasTreeNode *) node;

  return false;
}


static void *
prediction_classification_copy (const void *classification, void *data)
{
  GstInferenceClassification *self =
      (GstInferenceClassification *) classification;

  return vvas_inferclassification_copy (&(self->classification));
}

static void *
prediction_node_copy (const void *infer, void *data)
{
  VvasInferPrediction *dmeta = NULL;
  GstInferencePrediction *gst_infer = (GstInferencePrediction *) infer;
  VvasInferPrediction *smeta = NULL;

  if (NULL != gst_infer) {
    smeta = &(gst_infer->prediction);
    dmeta = vvas_inferprediction_new ();
  }

  if ((NULL != dmeta) && (NULL != smeta)) {
    dmeta->prediction_id = smeta->prediction_id;
    dmeta->enabled = smeta->enabled;
    dmeta->model_class = smeta->model_class;
    dmeta->count = smeta->count;

    if (smeta->model_name)
      dmeta->model_name = strdup (smeta->model_name);

    if (smeta->obj_track_label)
      dmeta->obj_track_label = strdup (smeta->obj_track_label);

    memcpy (&dmeta->bbox, &smeta->bbox, sizeof (VvasBoundingBox));
    memcpy (&dmeta->feature, &smeta->feature, sizeof (Feature));
    memcpy (&dmeta->pose14pt, &smeta->pose14pt, sizeof (Pose14Pt));

    memcpy (&dmeta->reid, &smeta->reid, sizeof (Reid));
    if (smeta->reid.data && smeta->reid.copy) {
      smeta->reid.copy (&smeta->reid, &dmeta->reid);
    }

    memcpy (&dmeta->segmentation, &smeta->segmentation, sizeof (Segmentation));
    if (smeta->segmentation.data && smeta->segmentation.copy) {
      smeta->segmentation.copy (&smeta->segmentation, &dmeta->segmentation);
    }

    if (smeta->tb) {
      dmeta->tb = smeta->tb;
      if (smeta->tb->copy)
        smeta->tb->copy ((void **) &smeta->tb, (void **) &dmeta->tb);
    }

    dmeta->classifications = vvas_list_copy_deep (smeta->classifications,
        prediction_classification_copy, NULL);
  }
  return (void *) dmeta;
}

VvasInferPrediction *
vvas_infer_from_gstinfer (GstInferencePrediction * pred)
{
  VvasTreeNode *Node = NULL;

  if (NULL != pred) {

    Node =
        vvas_treenode_copy_deep (pred->prediction.node, prediction_node_copy,
        NULL);

    vvas_treenode_traverse (Node, G_IN_ORDER,
        G_TRAVERSE_ALL, -1, prediction_node_assign, NULL);

    return Node->data;
  }

  return NULL;
}

static GstInferenceClassification *
classification_copy (const void *classification, void *data)
{
  GstInferenceClassification *self = NULL;
  VvasInferClassification *cls = (VvasInferClassification *) classification;
  int size = 0;

  if (cls) {
    self = gst_inference_classification_new ();
    self->classification.class_id = cls->class_id;
    self->classification.class_prob = cls->class_prob;
    self->classification.num_classes = cls->num_classes;
    self->classification.label_color.red = cls->label_color.red;
    self->classification.label_color.green = cls->label_color.green;
    self->classification.label_color.blue = cls->label_color.blue;
    self->classification.label_color.alpha = cls->label_color.alpha;
    if (cls->class_label)
      self->classification.class_label = g_strdup (cls->class_label);
    if (cls->labels)
      self->classification.labels = g_strdupv (cls->labels);
    if (cls->probabilities) {
      size = cls->num_classes * sizeof (double);
      self->classification.probabilities = (double *) malloc (size);
      memcpy (self->classification.probabilities, cls->probabilities, size);
    }
  }

  return self;
}

GstInferencePrediction *
gst_infer_node_from_vvas_infer (VvasInferPrediction * vinfer)
{
  GstInferencePrediction *self = NULL;

  if (vinfer) {
    self = gst_inference_prediction_new ();

    if (NULL == self) {
      return NULL;
    }

    self->prediction.prediction_id = vinfer->prediction_id;

    self->prediction.model_class = vinfer->model_class;
    if (vinfer->model_name)
      self->prediction.model_name = g_strdup (vinfer->model_name);
    if (vinfer->obj_track_label)
      self->prediction.obj_track_label = g_strdup (vinfer->obj_track_label);
    self->prediction.feature = vinfer->feature;
    self->prediction.pose14pt = vinfer->pose14pt;
    self->prediction.count = vinfer->count;
    self->prediction.bbox = vinfer->bbox;
    self->prediction.bbox_scaled = vinfer->bbox_scaled;
    self->prediction.enabled = vinfer->enabled;
    if (vinfer->classifications)
      self->prediction.classifications =
          vvas_list_copy_deep (vinfer->classifications,
          (vvas_list_copy_func) classification_copy, NULL);
    if (vinfer->segmentation.data != NULL) {
      if (vinfer->segmentation.copy) {
        vinfer->segmentation.copy (&vinfer->segmentation,
            &self->prediction.segmentation);
      }
    }
    if (vinfer->reid.data != NULL) {
      if (vinfer->reid.copy) {
        vinfer->reid.copy (&vinfer->reid, &self->prediction.reid);
      }
    }
    if (vinfer->tb != NULL) {
      if (vinfer->tb->copy) {
        vinfer->tb->copy ((void **) &vinfer->tb,
            (void **) &self->prediction.tb);
      }
    }
  }

  return self;
}

static void
vvas_inferprediction_get_childnodes (VvasTreeNode * node, void *data)
{
  VvasList **children = (VvasList **) data;
  VvasInferPrediction *prediction;

  if ((NULL == node) || (NULL == data)) {
    return;
  }

  prediction = (VvasInferPrediction *) node->data;

  *children = vvas_list_append (*children, prediction);
}

VvasList *
vvas_inferprediction_get_nodes (VvasInferPrediction * self)
{
  VvasList *children = NULL;

  if (NULL == self) {
    return NULL;
  }

  if (self->node) {
    vvas_treenode_traverse_child (self->node, G_TRAVERSE_ALL,
        vvas_inferprediction_get_childnodes, &children);
  }

  return children;
}

static void
free_gst_buffer (void *data, void *user_data)
{
  VvasGstUserData *gst_data = (VvasGstUserData *) user_data;
  gst_buffer_unmap (gst_data->buf, &gst_data->map_info);
  gst_buffer_unref (gst_data->buf);
  free (gst_data);
}

static void
free_gst_buffer_from_vvas_frame (void *data[], void *user_data)
{
  VvasGstUserData *gst_data = (VvasGstUserData *) user_data;
  gst_video_frame_unmap (gst_data->vframe);
  g_free (gst_data->vframe);
  free (gst_data);
}

static VvasVideoFormat
get_vvas_fmt_from_gst (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      return VVAS_VIDEO_FORMAT_Y_UV8_420;
    case GST_VIDEO_FORMAT_I420:
      return VVAS_VIDEO_FORMAT_I420;
    case GST_VIDEO_FORMAT_BGR: /* BGR 8-bit */
      return VVAS_VIDEO_FORMAT_BGR;
    case GST_VIDEO_FORMAT_RGB: /* RGB 8-bit */
      return VVAS_VIDEO_FORMAT_RGB;
    case GST_VIDEO_FORMAT_YUY2:        /* YUYV */
      return VVAS_VIDEO_FORMAT_YUY2;
    case GST_VIDEO_FORMAT_r210:
      return VVAS_VIDEO_FORMAT_r210;
    case GST_VIDEO_FORMAT_v308:
      return VVAS_VIDEO_FORMAT_v308;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      return VVAS_VIDEO_FORMAT_NV12_10LE32;
    case GST_VIDEO_FORMAT_I422_10LE:
      return VVAS_VIDEO_FORMAT_I422_10LE;
    case GST_VIDEO_FORMAT_GRAY8:
      return VVAS_VIDEO_FORMAT_GRAY8;
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      return VVAS_VIDEO_FORMAT_GRAY10_LE32;
    default:
      GST_ERROR ("Not supporting %s yet", gst_video_format_to_string (format));
      return VVAS_VIDEO_FORMAT_UNKNOWN;
  }
}

GstVideoFormat
gst_coreutils_get_gst_fmt_from_vvas (VvasVideoFormat format)
{
  switch (format) {
    case VVAS_VIDEO_FORMAT_Y_UV8_420:
      return GST_VIDEO_FORMAT_NV12;
    case VVAS_VIDEO_FORMAT_I420:
      return GST_VIDEO_FORMAT_I420;
    case VVAS_VIDEO_FORMAT_BGR:        /* BGR 8-bit */
      return GST_VIDEO_FORMAT_BGR;
    case VVAS_VIDEO_FORMAT_RGB:        /* RGB 8-bit */
      return GST_VIDEO_FORMAT_RGB;
    case VVAS_VIDEO_FORMAT_YUY2:       /* YUYV */
      return GST_VIDEO_FORMAT_YUY2;
    case VVAS_VIDEO_FORMAT_r210:
      return GST_VIDEO_FORMAT_r210;
    case VVAS_VIDEO_FORMAT_v308:
      return GST_VIDEO_FORMAT_v308;
    case VVAS_VIDEO_FORMAT_NV12_10LE32:
      return GST_VIDEO_FORMAT_NV12_10LE32;
    case VVAS_VIDEO_FORMAT_I422_10LE:
      return GST_VIDEO_FORMAT_I422_10LE;
    case VVAS_VIDEO_FORMAT_GRAY8:
      return GST_VIDEO_FORMAT_GRAY8;
    case VVAS_VIDEO_FORMAT_GRAY10_LE32:
      return GST_VIDEO_FORMAT_GRAY10_LE32;
    case VVAS_VIDEO_FORMAT_RGBx:
      return GST_VIDEO_FORMAT_RGBx;
    case VVAS_VIDEO_FORMAT_BGRx:
      return GST_VIDEO_FORMAT_BGRx;
    case VVAS_VIDEO_FORMAT_BGRA:
      return GST_VIDEO_FORMAT_BGRA;
    case VVAS_VIDEO_FORMAT_RGBA:
      return GST_VIDEO_FORMAT_RGBA;
    case VVAS_VIDEO_FORMAT_NV16:
      return GST_VIDEO_FORMAT_NV16;
    case VVAS_VIDEO_FORMAT_Y410:
      return GST_VIDEO_FORMAT_Y410;
    default:
      GST_ERROR ("Not supporting %d yet", format);
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
}

VvasMemory *
vvas_memory_from_gstbuffer (VvasContext * vvas_ctx, uint8_t mbank_idx,
    GstBuffer * buf)
{
  VvasMemoryPrivate *priv = NULL;
  GstMemory *mem = NULL;
  VvasAllocationType alloc_type = VVAS_ALLOC_TYPE_UNKNOWN;
  gboolean bret = FALSE;
  GstMapInfo ginfo;

  if (!vvas_ctx || !buf) {
    GST_ERROR ("Invalid arguments");
    return NULL;
  }

  mem = gst_buffer_get_memory (buf, 0);
  if (mem == NULL) {
    GST_ERROR ("failed to get memory from  buffer");
    goto error;
  }

  if (vvas_ctx->dev_handle && gst_is_vvas_memory (mem)) {

    /* validate whether VvasContext's device index and GstMemory's device index is same or not */
    if (gst_vvas_memory_can_avoid_copy (mem, vvas_ctx->dev_idx, mbank_idx)) {
#ifdef XLNX_PCIe_PLATFORM
      VvasSyncFlags gst_syncflag;
#endif
      GST_DEBUG ("buffer and vvascontext are same device and memory bank");

      priv = (VvasMemoryPrivate *) calloc (1, sizeof (VvasMemoryPrivate));
      if (priv == NULL) {
        GST_ERROR ("failed to allocate memory for VvasMemory");
        goto error;
      }

      priv->boh = gst_vvas_allocator_get_bo (mem);
      alloc_type = VVAS_ALLOC_TYPE_CMA;
      priv->size = gst_buffer_get_sizes (buf, NULL, NULL);
      priv->free_cb = NULL;
      priv->ctx = vvas_ctx;

      priv->mem_info.alloc_type = alloc_type;
      priv->mem_info.alloc_flags = VVAS_ALLOC_FLAG_NONE;        /* currently gstreamer allocator supports both device and host memory allocation */
      priv->mem_info.mbank_idx = mbank_idx;     // TODO: need to identity memory bank index from gstmemory

#ifdef XLNX_PCIe_PLATFORM
      gst_syncflag = gst_vvas_memory_get_sync_flag (mem);
      priv->mem_info.sync_flags = VVAS_DATA_SYNC_NONE;

      if (gst_syncflag & VVAS_SYNC_TO_DEVICE)
        priv->mem_info.sync_flags |= VVAS_DATA_SYNC_TO_DEVICE;

      if (gst_syncflag & VVAS_SYNC_FROM_DEVICE)
        priv->mem_info.sync_flags |= VVAS_DATA_SYNC_FROM_DEVICE;
#endif
      priv->mem_info.map_flags = VVAS_DATA_MAP_NONE;
    } else {
      gsize gst_buf_size = gst_buffer_get_sizes (buf, NULL, NULL);
      VvasReturnType vret;
      VvasMemoryMapInfo vinfo;

      priv =
          vvas_memory_alloc (vvas_ctx, VVAS_ALLOC_TYPE_CMA,
          VVAS_ALLOC_FLAG_NONE, mbank_idx, gst_buf_size, NULL);
      if (!priv) {
        GST_ERROR ("failed to allocate CMA memory on bank index %d", mbank_idx);
        goto error;
      }

      /*get vaddr of vvas memory to copy data from GstBuffer */
      vret = vvas_memory_map (priv, VVAS_DATA_MAP_WRITE, &vinfo);
      if (VVAS_IS_ERROR (vret)) {
        GST_ERROR ("failed to map vvas memory in write mode");
        goto error;
      }

      /* map GstBuffer to read data */
      bret = gst_buffer_map (buf, &ginfo, GST_MAP_READ);
      if (!bret) {
        GST_ERROR ("failed to map buffer in read mode");
        goto error;
      }

      /* copy data */
      memcpy (vinfo.data, ginfo.data, gst_buf_size);

      gst_buffer_unmap (buf, &ginfo);

      vret = vvas_memory_unmap (priv, &vinfo);
      if (VVAS_IS_ERROR (vret)) {
        GST_ERROR ("failed to map vvas memory in write mode");
        goto error;
      }
    }
  } else {
    VvasGstUserData *user_data = calloc (1, sizeof (VvasGstUserData));

    /* Memory is not allocated from GstVvasAllocator object */
    bret = gst_buffer_map (buf, &user_data->map_info, GST_MAP_READ);
    if (!bret) {
      GST_ERROR ("failed to map buffer in read mode");
      goto error;
    }
    // TODO: To be on safer side taking reference, need to check further
    user_data->buf = gst_buffer_ref (buf);

    /* allocate memory mapped virtual pointer */
    priv =
        vvas_memory_alloc_from_data (vvas_ctx, user_data->map_info.data,
        user_data->map_info.size, free_gst_buffer, user_data, NULL);
    if (!priv) {
      GST_ERROR ("failed to allocate non-CMA memory from GstBuffer");
      goto error;
    }
  }

  gst_memory_unref (mem);
  return (VvasMemory *) priv;

error:
  if (priv)
    free (priv);

  if (mem)
    gst_memory_unref (mem);

  return NULL;
}

VvasVideoFrame *
vvas_videoframe_from_gstbuffer (VvasContext * vvas_ctx, int8_t mbank_idx,
    GstBuffer * buf, GstVideoInfo * gst_vinfo, GstMapFlags flags)
{
  VvasVideoFramePriv *priv = NULL;
  GstMemory *mem = NULL;
  VvasVideoInfo vinfo = { 0, };
  VvasReturnType vret;
  uint8_t pidx;
  gboolean free_mem = TRUE;
  GstVideoMeta *vmeta;

  if (!vvas_ctx || !buf || !gst_vinfo) {
    GST_ERROR ("Invalid arguments");
    return NULL;
  }

  mem = gst_buffer_get_memory (buf, 0);
  if (mem == NULL) {
    GST_ERROR ("failed to get memory from  buffer");
    goto error;
  }

  vmeta = gst_buffer_get_video_meta (buf);
  if (vmeta) {
    vinfo.alignment.padding_bottom = vmeta->alignment.padding_bottom;
    vinfo.alignment.padding_top = vmeta->alignment.padding_top;
    vinfo.alignment.padding_left = vmeta->alignment.padding_left;
    vinfo.alignment.padding_right = vmeta->alignment.padding_right;
  }

  vinfo.width = GST_VIDEO_INFO_WIDTH (gst_vinfo);
  vinfo.height = GST_VIDEO_INFO_HEIGHT (gst_vinfo);
  vinfo.fmt = get_vvas_fmt_from_gst (GST_VIDEO_INFO_FORMAT (gst_vinfo));
  vinfo.n_planes = GST_VIDEO_INFO_N_PLANES (gst_vinfo);
  if (vvas_ctx->dev_handle && mbank_idx >= 0) {

    /* from inbuf get the pool handle */
    /* height & stride alignment  using Gobect get */

#ifdef XLNX_PCIe_PLATFORM
    VvasSyncFlags gst_syncflag;
#endif

    priv = (VvasVideoFramePriv *) calloc (1, sizeof (VvasVideoFramePriv));
    if (priv == NULL) {
      GST_ERROR ("failed to allocate memory for  VvasVideoFrame");
      goto error;
    }

    priv->log_level = vvas_ctx->log_level;
    priv->num_planes = vinfo.n_planes;
    priv->width = vinfo.width;
    priv->height = vinfo.height;
    priv->fmt = vinfo.fmt;
    priv->ctx = vvas_ctx;

    if (vvas_fill_planes (&vinfo, priv) < 0) {
      GST_ERROR ("failed to do prepare plane info");
      goto error;
    }

    if (gst_is_vvas_memory (mem)) {
      priv->boh =
          vvas_xrt_create_sub_bo (gst_vvas_allocator_get_bo (mem), priv->size,
          0);
    }
#ifndef XLNX_PCIe_PLATFORM
    else if (gst_is_dmabuf_memory (mem)) {
      gint dma_fd = -1;
      dma_fd = gst_dmabuf_memory_get_fd (mem);
      if (dma_fd < 0) {
        GST_ERROR ("failed to get DMABUF FD");
        goto error;
      }
      priv->boh = vvas_xrt_import_bo (vvas_ctx->dev_handle, dma_fd);
    }
#endif
    if (!priv->boh) {
      GST_ERROR ("failed to update bo handle");
      goto error;
    }

    for (pidx = 0; pidx < priv->num_planes; pidx++) {
      priv->planes[pidx].boh =
          vvas_xrt_create_sub_bo (priv->boh, priv->planes[pidx].size,
          priv->planes[pidx].offset);
      if (priv->planes[pidx].boh == NULL) {
        GST_ERROR ("failed to allocate sub BO with size %zu and offset %zu",
            priv->planes[pidx].size, priv->planes[pidx].offset);
        goto error;
      }
    }

    priv->mem_info.alloc_type = VVAS_ALLOC_TYPE_CMA;
    /* currently gstreamer allocator supports both device and host memory allocation */
    priv->mem_info.alloc_flags = VVAS_ALLOC_FLAG_NONE;
    priv->mem_info.mbank_idx = mbank_idx;
    priv->mem_info.sync_flags = VVAS_DATA_SYNC_NONE;
#ifdef XLNX_PCIe_PLATFORM

    /* In case of PCIe platform, its application's responsibility
     * to copy dma buffer into vvas buffer before calling this function.
     * When control comes here, it will always be VVAS buffer.
     */

    gst_syncflag = gst_vvas_memory_get_sync_flag (mem);

    if (gst_syncflag & VVAS_SYNC_TO_DEVICE)
      priv->mem_info.sync_flags |= VVAS_DATA_SYNC_TO_DEVICE;
    if (gst_syncflag & VVAS_SYNC_FROM_DEVICE)
      priv->mem_info.sync_flags |= VVAS_DATA_SYNC_FROM_DEVICE;
#endif
    priv->mem_info.map_flags = VVAS_DATA_MAP_NONE;
  } else {                      /* This is a software buffer */
    VvasGstUserData *user_data = calloc (1, sizeof (VvasGstUserData));
    void *data[VVAS_VIDEO_MAX_PLANES];
    GstVideoFrame *vframe = NULL;

    if (NULL == user_data) {
      GST_ERROR ("failed to allocate memory -user_data");
      goto error;
    }

    vframe = g_malloc0 (sizeof (GstVideoFrame));
    if (NULL == vframe) {
      free (user_data);
      GST_ERROR ("failed to allocate memory -vframe");
      goto error;
    }

    /* map input buffer in read mode */
    if (!gst_video_frame_map (vframe, gst_vinfo, buf, flags)) {
      GST_ERROR ("failed to map input buffer");
      g_free (vframe);
      free (user_data);
      goto error;
    }

    /* This vframe info will be unmapped and freed in callback */
    user_data->vframe = vframe;

    for (pidx = 0; pidx < GST_VIDEO_FRAME_N_PLANES (vframe); pidx++) {
      data[pidx] = GST_VIDEO_FRAME_PLANE_DATA (vframe, pidx);
    }

    priv = vvas_video_frame_alloc_from_data (vvas_ctx, &vinfo, data,
        free_gst_buffer_from_vvas_frame, user_data, &vret);
    if (!priv) {
      GST_ERROR ("Failed to allocate VVAS Video Frame from data");
      g_free (vframe);
      free (user_data);
      goto error;
    }
  }
  free_mem = FALSE;

error:
  if (free_mem && priv) {
    free (priv);
    priv = NULL;
  }

  if (mem)
    gst_memory_unref (mem);

  return (VvasVideoFrame *) priv;
}
