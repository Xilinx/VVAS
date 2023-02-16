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

#ifndef __GST_VVAS_INP_INFER_META_H__
#define __GST_VVAS_INP_INFER_META_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>
#include <vvas_core/vvas_dpucommon.h>

G_BEGIN_DECLS

typedef struct _GstVvasInpInferMeta GstVvasInpInferMeta;

struct _GstVvasInpInferMeta {
  GstMeta meta;
  VvasClass ml_class;
  gchar *model_name;
};

GST_EXPORT
GstVvasInpInferMeta *
gst_buffer_add_vvas_inp_infer_meta (GstBuffer *buffer, VvasClass ml_class, gchar *model_name);

GType gst_vvas_inp_infer_meta_api_get_type (void);
#define GST_VVAS_INP_INFER_META_API_TYPE (gst_vvas_inp_infer_meta_api_get_type())

GST_EXPORT
const GstMetaInfo *gst_vvas_inp_infer_meta_get_info (void);
#define GST_VVAS_INP_INFER_EXAMPLE_META_INFO ((gst_vvas_inp_infer_meta_get_info()))

#define gst_buffer_get_vvas_inp_infer_meta(b) ((GstVvasInpInferMeta*)gst_buffer_get_meta((b),GST_VVAS_INP_INFER_META_API_TYPE))

G_END_DECLS
#endif /* __GST_VVAS_INP_INFER_META_H__  */
