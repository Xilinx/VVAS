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

#include <gstvvaslameta.h>
#include <stdio.h>

GType
gst_vvas_la_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] =
      { GST_META_TAG_VIDEO_STR, GST_META_TAG_VIDEO_SIZE_STR, NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstVvasLAMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }

  return type;
}

static gboolean
gst_vvas_la_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstVvasLAMeta *vvasmeta = (GstVvasLAMeta *) meta;

  vvasmeta->gop_length = 0;
  vvasmeta->num_bframes = 0;
  vvasmeta->lookahead_depth = 0;
  vvasmeta->codec_type = VVAS_CODEC_NONE;
  vvasmeta->is_idr = FALSE;
  vvasmeta->spatial_aq = FALSE;
  vvasmeta->temporal_aq = FALSE;
  vvasmeta->spatial_aq_gain = 0;
  vvasmeta->qpmap = NULL;
  vvasmeta->rc_fsfa = NULL;
  return TRUE;
}

static gboolean
gst_vvas_la_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVvasLAMeta *vvasmeta = (GstVvasLAMeta *) meta;

  if (vvasmeta->qpmap) {
    gst_buffer_unref (vvasmeta->qpmap);
    vvasmeta->qpmap = NULL;
  }

  if (vvasmeta->rc_fsfa) {
    gst_buffer_unref (vvasmeta->rc_fsfa);
    vvasmeta->rc_fsfa = NULL;
  }

  return TRUE;
}

static gboolean
gst_vvas_la_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVvasLAMeta *dmeta, *smeta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {

    smeta = (GstVvasLAMeta *) meta;
    dmeta = gst_buffer_add_vvas_la_meta (dest);

    if (!dmeta)
      return FALSE;

    GST_LOG ("copy metadata from %p -> %p buffer %p -> %p", smeta, dmeta,
        buffer, dest);

    if (smeta->qpmap) {
      dmeta->qpmap = gst_buffer_copy (smeta->qpmap);
    }

    if (smeta->rc_fsfa) {
      dmeta->rc_fsfa = gst_buffer_copy (smeta->rc_fsfa);
    }

    dmeta->gop_length = smeta->gop_length;
    dmeta->num_bframes = smeta->num_bframes;
    dmeta->lookahead_depth = smeta->lookahead_depth;
    dmeta->codec_type = smeta->codec_type;
    dmeta->is_idr = smeta->is_idr;
    dmeta->spatial_aq = smeta->spatial_aq;
    dmeta->temporal_aq = smeta->temporal_aq;
    dmeta->spatial_aq_gain = smeta->spatial_aq_gain;

  } else {
    GST_ERROR ("unsupported transform type : %s", g_quark_to_string (type));
    return FALSE;
  }

  return TRUE;
}

const GstMetaInfo *
gst_vvas_la_meta_get_info (void)
{
  static const GstMetaInfo *vvas_la_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & vvas_la_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_VVAS_LA_META_API_TYPE, "GstVvasLAMeta",
        sizeof (GstVvasLAMeta), (GstMetaInitFunction) gst_vvas_la_meta_init,
        (GstMetaFreeFunction) gst_vvas_la_meta_free,
        gst_vvas_la_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & vvas_la_meta_info,
        (GstMetaInfo *) meta);
  }
  return vvas_la_meta_info;
}
