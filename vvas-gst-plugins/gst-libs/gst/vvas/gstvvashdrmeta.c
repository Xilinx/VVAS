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

#include <gstvvashdrmeta.h>
#include <stdio.h>

GType
gst_vvas_hdr_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { GST_META_TAG_VIDEO_STR, NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstVvasHDRMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }

  return type;
}

static gboolean
gst_vvas_hdr_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstVvasHdrMeta *vvasmeta = (GstVvasHdrMeta *) meta;

  memset (&vvasmeta->hdr_metadata, 0, sizeof (vcu_hdr_data));
  return TRUE;
}

static void
gst_vvas_hdr_meta_free (GstMeta * meta, GstBuffer * buffer)
{

}

static gboolean
gst_vvas_hdr_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVvasHdrMeta *dmeta, *smeta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {

    smeta = (GstVvasHdrMeta *) meta;
    dmeta = gst_buffer_add_vvas_hdr_meta (dest);
    if (!dmeta)
      return FALSE;

    GST_LOG ("copy metadata from %p -> %p buffer %p -> %p", smeta, dmeta,
        buffer, dest);
    memcpy (&dmeta->hdr_metadata, &smeta->hdr_metadata, sizeof (vcu_hdr_data));
  } else {
    GST_ERROR ("Unsupported transform type : %s", g_quark_to_string (type));
    return FALSE;
  }

  return TRUE;
}

const GstMetaInfo *
gst_vvas_hdr_meta_get_info (void)
{
  static const GstMetaInfo *vvas_hdr_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & vvas_hdr_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_VVAS_HDR_META_API_TYPE, "GstVvasHDRMeta",
        sizeof (GstVvasHdrMeta),
        (GstMetaInitFunction) gst_vvas_hdr_meta_init,
        (GstMetaFreeFunction) gst_vvas_hdr_meta_free,
        gst_vvas_hdr_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & vvas_hdr_meta_info,
        (GstMetaInfo *) meta);
  }
  return vvas_hdr_meta_info;
}
