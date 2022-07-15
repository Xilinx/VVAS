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

#include "gstvvasofmeta.h"
#include <stdio.h>

vvas_obj_motinfo *gst_vvas_obj_motinfo_new (void);
void gst_vvas_obj_motinfo_unref (vvas_obj_motinfo * self);

GType
gst_vvas_of_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] =
      { GST_META_TAG_VIDEO_STR, GST_META_TAG_VIDEO_SIZE_STR,
    GST_META_TAG_VIDEO_ORIENTATION_STR, NULL
  };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstVvasOFMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }

  return type;
}

static gboolean
gst_vvas_of_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstVvasOFMeta *vvasmeta = (GstVvasOFMeta *) meta;

  vvasmeta->num_objs = 0;
  vvasmeta->x_displ = NULL;
  vvasmeta->y_displ = NULL;
  vvasmeta->obj_mot_infos = NULL;
  return TRUE;
}

vvas_obj_motinfo *
gst_vvas_obj_motinfo_new (void)
{
  vvas_obj_motinfo *obj_motinfo = NULL;
  obj_motinfo = (vvas_obj_motinfo *) calloc (1, sizeof (vvas_obj_motinfo));

  obj_motinfo->mean_x_displ = 0;
  obj_motinfo->mean_y_displ = 0;
  obj_motinfo->angle = 0;
  obj_motinfo->dist = 0;

  obj_motinfo->bbox.x = 0;
  obj_motinfo->bbox.y = 0;
  obj_motinfo->bbox.width = 0;
  obj_motinfo->bbox.height = 0;

  obj_motinfo->bbox.box_color.red = 0;
  obj_motinfo->bbox.box_color.green = 0;
  obj_motinfo->bbox.box_color.blue = 0;
  obj_motinfo->bbox.box_color.alpha = 0;

  return obj_motinfo;
}

void
gst_vvas_obj_motinfo_unref (vvas_obj_motinfo * self)
{
  free (self);
}

static gboolean
gst_vvas_of_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVvasOFMeta *vvasmeta = (GstVvasOFMeta *) meta;

  if (vvasmeta->x_displ) {
    GST_DEBUG("OfMeta: Unref x_displ buffer of GstBuffer %p", buffer);
    gst_buffer_unref (vvasmeta->x_displ);
    vvasmeta->x_displ = NULL;
  }

  if (vvasmeta->y_displ) {
    GST_DEBUG("OfMeta: Unref y_displ buffer of GstBuffer %p", buffer);
    gst_buffer_unref (vvasmeta->y_displ);
    vvasmeta->y_displ = NULL;
  }

  g_list_free_full (vvasmeta->obj_mot_infos,
      (GDestroyNotify) gst_vvas_obj_motinfo_unref);

  return TRUE;
}

static vvas_obj_motinfo *
ofmeta_copy (const vvas_obj_motinfo * self)
{
  vvas_obj_motinfo *obj_motinfo = NULL;

  if (!self)
    return NULL;

  obj_motinfo = gst_vvas_obj_motinfo_new ();
  obj_motinfo->mean_x_displ = self->mean_x_displ;
  obj_motinfo->mean_y_displ = self->mean_y_displ;
  obj_motinfo->angle = self->angle;
  obj_motinfo->dist = self->dist;
  snprintf (obj_motinfo->dirc_name, DIR_NAME_SZ, "%s", self->dirc_name);

  obj_motinfo->bbox.x = self->bbox.x;
  obj_motinfo->bbox.y = self->bbox.y;
  obj_motinfo->bbox.width = self->bbox.width;
  obj_motinfo->bbox.height = self->bbox.height;

  obj_motinfo->bbox.box_color.red = self->bbox.box_color.red;
  obj_motinfo->bbox.box_color.green = self->bbox.box_color.green;
  obj_motinfo->bbox.box_color.blue = self->bbox.box_color.blue;
  obj_motinfo->bbox.box_color.alpha = self->bbox.box_color.alpha;

  return obj_motinfo;
}

static gboolean
gst_vvas_of_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVvasOFMeta *dmeta, *smeta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    smeta = (GstVvasOFMeta *) meta;
    dmeta = gst_buffer_add_vvas_of_meta (dest);

    if (!dmeta) {
      GST_ERROR ("Unable to add meta to buffer");
      return FALSE;
    }

    if (smeta->x_displ) {
      dmeta->x_displ = gst_buffer_ref (smeta->x_displ);
    }

    if (smeta->y_displ) {
      dmeta->y_displ = gst_buffer_ref (smeta->y_displ);
    }

    dmeta->num_objs = smeta->num_objs;
    dmeta->obj_mot_infos = g_list_copy_deep (smeta->obj_mot_infos,
        (GCopyFunc) ofmeta_copy, NULL);
  } else if (GST_VIDEO_META_TRANSFORM_IS_SCALE (type)) {
    GST_LOG ("Scaling of opticalflow metadata is not supported");
    return FALSE;
  }

  return TRUE;
}

const GstMetaInfo *
gst_vvas_of_meta_get_info (void)
{
  static const GstMetaInfo *vvas_of_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & vvas_of_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_VVAS_OF_META_API_TYPE, "GstVvasOFMeta",
        sizeof (GstVvasOFMeta), (GstMetaInitFunction) gst_vvas_of_meta_init,
        (GstMetaFreeFunction) gst_vvas_of_meta_free,
        gst_vvas_of_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & vvas_of_meta_info,
        (GstMetaInfo *) meta);
  }
  return vvas_of_meta_info;
}
