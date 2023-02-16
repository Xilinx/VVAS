/*
 * Copyright 2022 Xilinx, Inc.
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

#include "gstvvasoverlaymeta.h"
#include <stdio.h>
#include <math.h>

GType
gst_vvas_overlay_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] =
      { GST_META_TAG_VIDEO_STR, GST_META_TAG_VIDEO_SIZE_STR,
    GST_META_TAG_VIDEO_ORIENTATION_STR, NULL
  };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstVvasOverlayMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }

  return type;
}

static gboolean
gst_vvas_overlay_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstVvasOverlayMeta *vvasmeta = (GstVvasOverlayMeta *) meta;

  vvas_overlay_shape_info_init (&vvasmeta->shape_info);
  return TRUE;
}

static gboolean
gst_vvas_overlay_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVvasOverlayMeta *vvasmeta = (GstVvasOverlayMeta *) meta;

  vvas_overlay_shape_info_free (&vvasmeta->shape_info);
  return TRUE;
}

static gboolean
transform_overlay_meta (GstVvasOverlayMeta * dmeta, gpointer data)
{
  GstVideoMetaTransform *trans;
  gdouble hfactor = 1, vfactor = 1;
  gint fw, fh, tw, th;
  VvasList *head, *pt_head = NULL;

  trans = (GstVideoMetaTransform *) data;
  fw = GST_VIDEO_INFO_WIDTH (trans->in_info);
  tw = GST_VIDEO_INFO_WIDTH (trans->out_info);
  fh = GST_VIDEO_INFO_HEIGHT (trans->in_info);
  th = GST_VIDEO_INFO_HEIGHT (trans->out_info);

  if (0 == fw || 0 == fh) {
    return FALSE;
  }

  hfactor = tw * 1.0 / fw;
  vfactor = th * 1.0 / fh;

  GST_LOG ("Xform from %dx%d -> %dx%d, hfactor: %lf, vfactor: %lf",
      fw, fh, tw, th, hfactor, vfactor);

  if (dmeta->shape_info.num_rects) {
    head = dmeta->shape_info.rect_params;
    while (head) {
      VvasOverlayRectParams *rect_params = (VvasOverlayRectParams *) head->data;
      rect_params->points.x *= hfactor;
      rect_params->points.y *= vfactor;
      rect_params->width *= hfactor;
      rect_params->height *= vfactor;
      head = head->next;
    }
  }

  if (dmeta->shape_info.num_text) {
    head = dmeta->shape_info.text_params;
    while (head) {
      VvasOverlayTextParams *text_params = (VvasOverlayTextParams *) head->data;
      text_params->points.x *= hfactor;
      text_params->points.y *= vfactor;
      head = head->next;
    }
  }


  if (dmeta->shape_info.num_lines) {
    head = dmeta->shape_info.line_params;
    while (head) {
      VvasOverlayLineParams *line_params = (VvasOverlayLineParams *) head->data;
      line_params->start_pt.x *= hfactor;
      line_params->start_pt.y *= vfactor;
      line_params->end_pt.x *= hfactor;
      line_params->end_pt.y *= vfactor;
      head = head->next;
    }
  }

  if (dmeta->shape_info.num_arrows) {
    head = dmeta->shape_info.arrow_params;
    while (head) {
      VvasOverlayArrowParams *arrow_params =
          (VvasOverlayArrowParams *) head->data;
      arrow_params->start_pt.x *= hfactor;
      arrow_params->start_pt.y *= vfactor;
      arrow_params->end_pt.x *= hfactor;
      arrow_params->end_pt.y *= vfactor;
      head = head->next;
    }
  }

  if (dmeta->shape_info.num_circles) {
    double from_area, to_area;

    from_area = fw * fh;
    to_area = tw * th;
    head = dmeta->shape_info.circle_params;
    while (head) {
      VvasOverlayCircleParams *circle_params =
          (VvasOverlayCircleParams *) head->data;
      double from_circle_area, to_circle_area, to_radius;

      from_circle_area = M_PI * (circle_params->radius * circle_params->radius);
      to_circle_area = (to_area * from_circle_area) / from_area;
      to_radius = to_circle_area / M_PI;
      to_radius = sqrt (to_radius);
      GST_LOG ("radius %d -> %lf", circle_params->radius, to_radius);

      circle_params->center_pt.x *= hfactor;
      circle_params->center_pt.y *= vfactor;
      circle_params->radius = to_radius;
      head = head->next;
    }
  }

  head = dmeta->shape_info.polygn_params;
  while (head) {
    VvasOverlayPolygonParams *polygn_params =
        (VvasOverlayPolygonParams *) head->data;
    pt_head = polygn_params->poly_pts;
    while (pt_head) {
      VvasOverlayCoordinates *poly_pts =
          (VvasOverlayCoordinates *) pt_head->data;
      poly_pts->x *= hfactor;
      poly_pts->y *= vfactor;
      pt_head = pt_head->next;
    }
    head = head->next;
  }
  return TRUE;
}

static gboolean
add_and_copy_overlay_meta (GstBuffer * dest, GstVvasOverlayMeta * smeta)
{
  GstVvasOverlayMeta *dmeta;
  dmeta = gst_buffer_add_vvas_overlay_meta (dest);
  if (!dmeta) {
    GST_ERROR ("Unable to add meta to buffer");
    return FALSE;
  }

  vvas_overlay_shape_info_copy (&dmeta->shape_info, &smeta->shape_info);
  return TRUE;
}

static gboolean
gst_vvas_overlay_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVvasOverlayMeta *dmeta, *smeta;
  gboolean ret = FALSE;

  smeta = (GstVvasOverlayMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    ret = add_and_copy_overlay_meta (dest, smeta);
  } else if (GST_VIDEO_META_TRANSFORM_IS_SCALE (type)) {
    dmeta = gst_buffer_get_vvas_overlay_meta (dest);
    if (!dmeta) {
      if (!add_and_copy_overlay_meta (dest, smeta)) {
        return ret;
      }
      dmeta = gst_buffer_get_vvas_overlay_meta (dest);
    }
    ret = transform_overlay_meta (dmeta, data);
  }
  return ret;
}

const GstMetaInfo *
gst_vvas_overlay_meta_get_info (void)
{
  static const GstMetaInfo *vvas_overlay_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & vvas_overlay_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_VVAS_OVERLAY_META_API_TYPE, "GstVvasOverlayMeta",
        sizeof (GstVvasOverlayMeta),
        (GstMetaInitFunction) gst_vvas_overlay_meta_init,
        (GstMetaFreeFunction) gst_vvas_overlay_meta_free,
        gst_vvas_overlay_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & vvas_overlay_meta_info,
        (GstMetaInfo *) meta);
  }
  return vvas_overlay_meta_info;
}
