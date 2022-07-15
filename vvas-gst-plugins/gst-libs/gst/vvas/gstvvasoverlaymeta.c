/*
 * Copyright 2022 Xilinx, Inc.
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

  vvasmeta->num_rects = 0;
  vvasmeta->num_text = 0;
  vvasmeta->num_lines = 0;
  vvasmeta->num_arrows = 0;
  vvasmeta->num_circles = 0;
  vvasmeta->num_polys = 0;
  return TRUE;
}

static gboolean
gst_vvas_overlay_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  return TRUE;
}

static gboolean
transform_overlay_meta (GstVvasOverlayMeta * dmeta, gpointer data)
{
  GstVideoMetaTransform *trans;
  gdouble hfactor = 1, vfactor = 1;
  gint idx;
  gint fw, fh, tw, th;

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

  for (idx = 0; idx < dmeta->num_rects; idx++) {
    dmeta->rects[idx].offset.x *= hfactor;
    dmeta->rects[idx].offset.y *= vfactor;
    dmeta->rects[idx].width *= hfactor;
    dmeta->rects[idx].height *= vfactor;
  }
  for (idx = 0; idx < dmeta->num_text; idx++) {
    dmeta->text[idx].offset.x *= hfactor;
    dmeta->text[idx].offset.y *= vfactor;
  }
  for (idx = 0; idx < dmeta->num_lines; idx++) {
    dmeta->lines[idx].start_pt.x *= hfactor;
    dmeta->lines[idx].start_pt.y *= vfactor;
    dmeta->lines[idx].end_pt.x *= hfactor;
    dmeta->lines[idx].end_pt.y *= vfactor;
  }
  for (idx = 0; idx < dmeta->num_arrows; idx++) {
    dmeta->arrows[idx].start_pt.x *= hfactor;
    dmeta->arrows[idx].start_pt.y *= vfactor;
    dmeta->arrows[idx].end_pt.x *= hfactor;
    dmeta->arrows[idx].end_pt.y *= vfactor;
  }

  if (dmeta->num_circles) {
    double from_area, to_area;

    from_area = fw * fh;
    to_area = tw * th;
    for (idx = 0; idx < dmeta->num_circles; idx++) {
      double from_circle_area, to_circle_area, to_radius;

      from_circle_area = M_PI * (dmeta->circles[idx].radius
          * dmeta->circles[idx].radius);
      to_circle_area = (to_area * from_circle_area) / from_area;
      to_radius = to_circle_area / M_PI;
      to_radius = sqrt (to_radius);
      GST_LOG ("radius %d -> %lf", dmeta->circles[idx].radius, to_radius);

      dmeta->circles[idx].center_pt.x *= hfactor;
      dmeta->circles[idx].center_pt.y *= vfactor;
      dmeta->circles[idx].radius = to_radius;
    }
  }

  for (idx = 0; idx < dmeta->num_polys; idx++) {
    gint i;
    for (i = 0; i < dmeta->polygons[idx].num_pts; i++) {
      dmeta->polygons[idx].poly_pts[i].x *= hfactor;
      dmeta->polygons[idx].poly_pts[i].y *= vfactor;
    }
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

  dmeta->num_rects = smeta->num_rects;
  dmeta->num_text = smeta->num_text;
  dmeta->num_lines = smeta->num_lines;
  dmeta->num_arrows = smeta->num_arrows;
  dmeta->num_circles = smeta->num_circles;
  dmeta->num_polys = smeta->num_polys;

  memcpy (dmeta->rects, smeta->rects,
      (VVAS_MAX_OVERLAY_DATA * sizeof (vvas_rect_params)));
  memcpy (dmeta->text, smeta->text,
      (VVAS_MAX_OVERLAY_DATA * sizeof (vvas_text_params)));
  memcpy (dmeta->lines, smeta->lines,
      (VVAS_MAX_OVERLAY_DATA * sizeof (vvas_line_params)));
  memcpy (dmeta->arrows, smeta->arrows,
      (VVAS_MAX_OVERLAY_DATA * sizeof (vvas_arrow_params)));
  memcpy (dmeta->circles, smeta->circles,
      (VVAS_MAX_OVERLAY_DATA * sizeof (vvas_circle_params)));
  memcpy (dmeta->polygons, smeta->polygons,
      (VVAS_MAX_OVERLAY_DATA * sizeof (vvas_polygon_params)));
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
