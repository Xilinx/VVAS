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

#include <gstvvassrcidmeta.h>
#include <stdio.h>

GType
gst_vvas_srcid_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { GST_META_TAG_VIDEO_STR, NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstVvasSrcIDMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }

  return type;
}

static gboolean
gst_vvas_srcid_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstVvasSrcIDMeta *vvasmeta = (GstVvasSrcIDMeta *) meta;

  vvasmeta->src_id = -1;
  return TRUE;
}

static gboolean
gst_vvas_srcid_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVvasSrcIDMeta *dmeta, *smeta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {

    smeta = (GstVvasSrcIDMeta *) meta;
    dmeta = gst_buffer_add_vvas_srcid_meta (dest);

    if (!dmeta)
      return FALSE;

    GST_LOG ("assign source id %d buffer %p -> %p", smeta->src_id, buffer,
        dest);

    dmeta->src_id = smeta->src_id;
  } else {
    GST_ERROR ("unsupported transform type : %s", g_quark_to_string (type));
    return FALSE;
  }

  return TRUE;
}

const GstMetaInfo *
gst_vvas_srcid_meta_get_info (void)
{
  static const GstMetaInfo *vvas_srcid_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & vvas_srcid_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_VVAS_SRCID_META_API_TYPE, "GstVvasSrcIDMeta",
        sizeof (GstVvasSrcIDMeta),
        (GstMetaInitFunction) gst_vvas_srcid_meta_init,
        NULL,
        gst_vvas_srcid_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & vvas_srcid_meta_info,
        (GstMetaInfo *) meta);
  }
  return vvas_srcid_meta_info;
}
