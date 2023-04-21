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

#ifndef _GST_VVAS_XVIDEODEC_H_
#define _GST_VVAS_XVIDEODEC_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>

G_BEGIN_DECLS

/** @def GST_TYPE_VVAS_XVIDEODEC
 *  @brief Macro to get GstVvas_XVideoDec object type
 */
#define GST_TYPE_VVAS_XVIDEODEC          (gstvvas_xvideodec_get_type())

/** @def GST_VVAS_XVIDEODEC
 *  @brief Macro to typecast parent object to GstVvas_XVideoDec object
 */
#define GST_VVAS_XVIDEODEC(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XVIDEODEC,GstVvas_XVideoDec))

/** @def GST_VVAS_XVIDEODEC_CLASS
 *  @brief Macro to typecast parent class object to GstVvas_XVideoDecClass object
 */
#define GST_VVAS_XVIDEODEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XVIDEODEC,GstVvas_XVideoDecClass))

/** @def GST_IS_VVAS_XVIDEODEC
 *  @brief Macro to validate whether object is of GstVvas_XVideoDec type
 */
#define GST_IS_VVAS_XVIDEODEC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XVIDEODEC))

/** @def GST_IS_VVAS_XVIDEODEC_CLASS
 *  @brief Macro to validate whether object class  is of GstVvas_XVideoDecClass type
 */
#define GST_IS_VVAS_XVIDEODEC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XVIDEODEC))

typedef struct _GstVvas_XVideoDec GstVvas_XVideoDec;
typedef struct _GstVvas_XVideoDecClass GstVvas_XVideoDecClass;
typedef struct _GstVvas_XVideoDecPrivate GstVvas_XVideoDecPrivate;

typedef enum _xlnx_codec_type{
  XLNX_CODEC_H264,
  XLNX_CODEC_H265
} XlnxCodecType;

/** @struct _GstVvas_XVideoDec
 *  @brief  Represents plugin instance
 */
struct _GstVvas_XVideoDec
{
  /** decoder parent object */
  GstVideoDecoder parent;
  /** plugin private data */
  GstVvas_XVideoDecPrivate *priv;
  /** H264/HEVC codec type */
  XlnxCodecType codec_type;
  /** decoder configuration */
  GstVideoCodecState *input_state;
  /** video frame attributes */
  GstVideoInfo out_vinfo;

  /* properties */
  /* #ifndef ENABLE_XRM_SUPPORT */
  /** path of XCLBIN binary */
  gchar *xclbin_path;
  /* #endif */
  /** low latency flag */
  gboolean low_latency;
  /** Number of decoder internal entropy buffers */
  guint num_entropy_bufs;
  /** bit depth */
  guint bit_depth;
  /** soft kernel start index */
  gint sk_start_idx;
#ifdef XLNX_U30_PLATFORM
  /** soft kernel current index */
  gint sk_cur_idx;
#endif
  /** device index */
  gint dev_index;
  /** decoder kernel name */
  gchar *kernel_name;
  /** flag to configure decoder output buffer copy */
  gboolean avoid_output_copy;
  /** flag to enable decoder splitbuffer mode */
  gboolean splitbuff_mode;
  /** flag to avoid output buffer allocation at run time */
  gboolean avoid_dynamic_alloc;
  /** flag to disable HDR10 SEI */
  gboolean disable_hdr10_sei;
  /** input memory bank */
  guint in_mem_bank;
  /** output memory bank */
  guint out_mem_bank;
  /** flag to interpolate timestamps */
  gboolean interpolate_timestamps;
  /** number of additional output buffers */
  guint additional_output_buffers;
#ifdef XLNX_V70_PLATFORM
  /** VDU instance id */
  guint hw_instance_id;
  /** flag to enable I-frame only decode */
  gboolean i_frame_only;
  /** forced framerate value */
  guint force_decode_rate;
#endif
};

struct _GstVvas_XVideoDecClass
{
    /*!< parent class */
    GstVideoDecoderClass parent_class;
};

GType gstvvas_xvideodec_get_type(void);

G_END_DECLS
#endif /* _GST_VVAS_XVIDEODEC_H_ */
