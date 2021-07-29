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

#include <gstivaslameta.h>
#include <stdio.h>

GType
gst_ivas_la_meta_api_get_type (void)
{
  static volatile GType type = 0;
  static const gchar *tags[] =
      { GST_META_TAG_VIDEO_STR, GST_META_TAG_VIDEO_SIZE_STR, NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstIvasLAMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }

  return type;
}

static gboolean
gst_ivas_la_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstIvasLAMeta *ivasmeta = (GstIvasLAMeta *) meta;

  ivasmeta->gop_length = 0;
  ivasmeta->num_bframes = 0;
  ivasmeta->lookahead_depth = 0;
  ivasmeta->codec_type = IVAS_CODEC_NONE;
  ivasmeta->qpmap = NULL;
  ivasmeta->rc_fsfa = NULL;
  return TRUE;
}

static gboolean
gst_ivas_la_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstIvasLAMeta *ivasmeta = (GstIvasLAMeta *) meta;

  if (ivasmeta->qpmap) {
    gst_buffer_unref (ivasmeta->qpmap);
    ivasmeta->qpmap = NULL;
  }

  if (ivasmeta->rc_fsfa) {
    gst_buffer_unref (ivasmeta->rc_fsfa);
    ivasmeta->rc_fsfa = NULL;
  }

  return TRUE;
}

static gboolean
gst_ivas_la_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstIvasLAMeta *dmeta, *smeta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {

    smeta = (GstIvasLAMeta *) meta;
    dmeta = gst_buffer_add_ivas_la_meta (dest);

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
#if 0
    bret =
        gst_buffer_copy_into (dmeta->qpmap, smeta->qpmap, GST_BUFFER_COPY_ALL,
        0, -1);
    if (!bret)
      return bret;

    bret =
        gst_buffer_copy_into (dmeta->rc_fsfa, smeta->rc_fsfa,
        GST_BUFFER_COPY_ALL, 0, -1);
    if (!bret)
      return bret;
#endif
  } else {
    GST_ERROR ("unsupported transform type : %s", g_quark_to_string (type));
    return FALSE;
  }

  return TRUE;
}

const GstMetaInfo *
gst_ivas_la_meta_get_info (void)
{
  static const GstMetaInfo *ivas_la_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & ivas_la_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_IVAS_LA_META_API_TYPE, "GstIvasLAMeta",
        sizeof (GstIvasLAMeta), (GstMetaInitFunction) gst_ivas_la_meta_init,
        (GstMetaFreeFunction) gst_ivas_la_meta_free,
        gst_ivas_la_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & ivas_la_meta_info,
        (GstMetaInfo *) meta);
  }
  return ivas_la_meta_info;
}
