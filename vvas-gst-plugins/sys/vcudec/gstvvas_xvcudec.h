/*
 * Copyright 2020 - 2022 Xilinx, Inc.
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

#ifndef _GST_VVAS_XVCUDEC_H_
#define _GST_VVAS_XVCUDEC_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>

G_BEGIN_DECLS

#define GST_TYPE_VVAS_XVCUDEC          (gstvvas_xvcudec_get_type())
#define GST_VVAS_XVCUDEC(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XVCUDEC,GstVvas_XVCUDec))
#define GST_VVAS_XVCUDEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XVCUDEC,GstVvas_XVCUDecClass))
#define GST_IS_VVAS_XVCUDEC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XVCUDEC))
#define GST_IS_VVAS_XVCUDEC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XVCUDEC))

typedef struct _GstVvas_XVCUDec GstVvas_XVCUDec;
typedef struct _GstVvas_XVCUDecClass GstVvas_XVCUDecClass;
typedef struct _GstVvas_XVCUDecPrivate GstVvas_XVCUDecPrivate;

typedef enum _xlnx_codec_type{
  XLNX_CODEC_H264,
  XLNX_CODEC_H265
} XlnxCodecType;

struct _GstVvas_XVCUDec
{
  GstVideoDecoder parent;
  GstVvas_XVCUDecPrivate *priv;
  XlnxCodecType codec_type;
  GstVideoCodecState *input_state;
  GstVideoInfo out_vinfo;

  /* properties */
//#ifndef ENABLE_XRM_SUPPORT
  gchar *xclbin_path;
//#endif
  gboolean low_latency;
  guint num_entropy_bufs;
  guint bit_depth;
  gint sk_start_idx;
  gint sk_cur_idx;
  gint dev_index;
  gchar *kernel_name;
  gboolean avoid_output_copy;
  gboolean splitbuff_mode;
  gboolean avoid_dynamic_alloc;
  gboolean disable_hdr10_sei;
  guint in_mem_bank;
  guint out_mem_bank;
  gboolean interpolate_timestamps;
};

struct _GstVvas_XVCUDecClass
{
    GstVideoDecoderClass parent_class;
};

GType gstvvas_xvcudec_get_type(void);

G_END_DECLS
#endif /* _GST_VVAS_XVCUDEC_H_ */
