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

#ifndef __GST_VVAS_OF_META_H__
#define __GST_VVAS_OF_META_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>
#include <gst/vvas/gstvvascommon.h>
#include <gst/vvas/gstinferenceprediction.h>

G_BEGIN_DECLS

#define GST_VVAS_OF_META_API_TYPE  (gst_vvas_of_meta_api_get_type())
#define GST_VVAS_OF_META_INFO  (gst_vvas_of_meta_get_info())

#define DIR_NAME_SZ 128

typedef struct _vvas_obj_motinfo vvas_obj_motinfo;
typedef struct _GstVvasOFMeta GstVvasOFMeta;

struct _vvas_obj_motinfo
{
  float mean_x_displ;
  float mean_y_displ;
  float angle;
  float dist;
  char dirc_name[DIR_NAME_SZ];
  BoundingBox bbox;
};

struct _GstVvasOFMeta {
  GstMeta meta;

  guint num_objs;
  GList *obj_mot_infos;

  GstBuffer *x_displ;
  GstBuffer *y_displ;
};

GST_EXPORT
GType gst_vvas_of_meta_api_get_type (void);

GST_EXPORT
const GstMetaInfo * gst_vvas_of_meta_get_info (void);

#define gst_buffer_get_vvas_of_meta(b) ((GstVvasOFMeta*)gst_buffer_get_meta((b), GST_VVAS_OF_META_API_TYPE))
#define gst_buffer_add_vvas_of_meta(b) ((GstVvasOFMeta*)gst_buffer_add_meta((b), GST_VVAS_OF_META_INFO, NULL))

G_END_DECLS

#endif /* __GST_VVAS_OF_META_H__ */
