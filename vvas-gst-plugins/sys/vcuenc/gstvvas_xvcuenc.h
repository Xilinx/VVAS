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

#ifndef _GST_VVAS_XVCUENC_H_
#define _GST_VVAS_XVCUENC_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include <gst/vvas/gstvvascommon.h>

G_BEGIN_DECLS

#define GST_TYPE_VVAS_XVCUENC          (gst_vvas_xvcuenc_get_type())
#define GST_VVAS_XVCUENC(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XVCUENC,GstVvasXVCUEnc))
#define GST_VVAS_XVCUENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XVCUENC,GstVvasXVCUEncClass))
#define GST_IS_VVAS_XVCUENC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XVCUENC))
#define GST_IS_VVAS_XVCUENC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XVCUENC))

typedef struct _GstVvasXVCUEnc GstVvasXVCUEnc;
typedef struct _GstVvasXVCUEncClass GstVvasXVCUEncClass;
typedef struct _GstVvasXVCUEncPrivate GstVvasXVCUEncPrivate;

#define VUI_PARAMS_SIZE (30)

struct _GstVvasXVCUEnc
{
  GstVideoEncoder parent;
  GstVvasXVCUEncPrivate *priv;
  VvasCodecType codec_type;
  guint stride_align;
  guint height_align;
  GstVideoCodecState *input_state;
  GstVideoInfo out_vinfo;
  gchar *profile;
  gchar *level;
  /*only for H.265*/
  gchar *tier;

  /* properties */
  /* TODO: Need to uncomment after CR-1122125 is resolved */
//#ifndef ENABLE_XRM_SUPPORT
  gchar *xclbin_path;
//#endif
  gint sk_start_idx;
  gint sk_cur_idx;
  gint dev_index;
  guint32 control_rate;
  guint32 target_bitrate;
  gint32 slice_qp;
  guint32 qp_mode;
  guint32 min_qp;
  guint32 max_qp;
  guint32 gop_mode;
  guint32 gdr_mode;
  guint32 initial_delay;
  guint32 cpb_size;
  guint32 scaling_list;
  guint32 max_bitrate;
  guint32 aspect_ratio;
  guint32 bit_depth;
  gboolean filler_data;
  guint32 num_slices;
  guint32 slice_size;
  gboolean prefetch_buffer;
  guint32 periodicity_idr;
  gint b_frames;
  gboolean constrained_intra_prediction;
  guint32 loop_filter_mode;
  guint32 gop_length;
  gboolean enabled_pipeline;
  gint ip_delta;
  gint pb_delta;
  gint loop_filter_beta_offset;
  gint loop_filter_tc_offset;
  /*only for H.264*/
  guint32 entropy_mode;
  guint32 num_cores;
  gboolean rc_mode;
  gchar *kernel_name;
  gboolean tune_metrics;
  gboolean dependent_slice;
  char color_description[VUI_PARAMS_SIZE];
  char transfer_characteristics[VUI_PARAMS_SIZE];
  char color_matrix[VUI_PARAMS_SIZE];
  guint in_mem_bank;
  guint out_mem_bank;
  gboolean ultra_low_latency;
  gboolean avc_lowlat;
};

struct _GstVvasXVCUEncClass
{
  GstVideoEncoderClass parent_class;
};

GType gst_vvas_xvcuenc_get_type(void);

G_BEGIN_DECLS
#endif /* _GST_VVAS_XVCUENC_H_ */
