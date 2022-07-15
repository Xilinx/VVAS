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

/*
 * TODO:
 * - do_not_encode support not added. need to check how gst-omx is doing
 * - pts is overriden by videoencoder base class, need to check how to send proper pts
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define HDR_DATA_SUPPORT

#include "gstvvas_xvcuenc.h"
#include <gst/vvas/gstvvasallocator.h>
#include <gst/vvas/gstvvasbufferpool.h>
#include <vvas/xrt_utils.h>
#ifdef HDR_DATA_SUPPORT
#include <gst/vvas/mpsoc_vcu_hdr.h>
#include <gst/vvas/gstvvashdrmeta.h>
#endif
#include <gst/vvas/gstvvaslameta.h>
#include <experimental/xrt-next.h>

#ifdef ENABLE_XRM_SUPPORT
#include <xrm.h>
#include <xrm_limits.h>
#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))
#endif

GST_DEBUG_CATEGORY_STATIC (gst_vvas_xvcuenc_debug_category);
#define GST_CAT_DEFAULT gst_vvas_xvcuenc_debug_category
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

#define VVAS_LOOKAHEAD_QUERY_NAME "VVASLookaheadQuery"
#define VVAS_ENCODER_QUERY_NAME "VVASEncoderQuery"

#define GST_CAPS_FEATURE_MEMORY_XLNX "memory:XRT"
static const int ERT_CMD_SIZE = 4096;
#define CMD_EXEC_TIMEOUT 1000   // 1 sec
#define MIN_POOL_BUFFERS 1
#define STOP_COMMAND ((gpointer)GINT_TO_POINTER (g_quark_from_string("STOP")))

#define ENABLE_DMABUF 0
//#define WIDTH_ALIGN 256
//#define HEIGHT_ALIGN 64
#define VVAS_XVCUENC_LOOKAHEAD_MIN_QP 20
#define H264_STRIDE_ALIGN 32
#define H265_STRIDE_ALIGN 32
#define HEIGHT_ALIGN 32
#define ALIGN(size,align) (((size) + (align) - 1) & ~((align) - 1))
//////////////// FROM VCU XMA Plugin START //////////////////////
#define MAX_OUT_BUFF_COUNT 50
#define MIN_LOOKAHEAD_DEPTH (1)
#define MAX_LOOKAHEAD_DEPTH (30)
/* App using this plugin is suggested to have >= below size.
   MAX_EXTRADATA_SIZE should be consistent with AL_ENC_MAX_CONFIG_HEADER_SIZE on device */
#define MAX_EXTRADATA_SIZE  (2 * 1024)
#define MAX_ERR_STRING 1024
#define ENC_SK_CU_START_OFFSET 32
#define ENC_MIN_SK_CU_INDEX 32
#define ENC_MAX_SK_CU_INDEX 63
#define WARN_BUFF_MAX_SIZE  (4 * 1024)
enum cmd_type
{
  VCU_PREINIT = 0,
  VCU_INIT,
  VCU_PUSH,
  VCU_RECEIVE,
  VCU_FLUSH,
  VCU_DEINIT,
};

enum rc_mode
{
  AL_RC_CONST_QP = 0x00,
  AL_RC_CBR = 0x01,
  AL_RC_VBR = 0x02,
  AL_RC_LOW_LATENCY = 0x03,
  AL_RC_CAPPED_VBR = 0x04,
  AL_RC_BYPASS = 0x3F,
  AL_RC_PLUGIN = 0x40,
  AL_RC_MAX_ENUM,
};

enum e_SliceType
{
  SLICE_SI = 4,                 /*!< AVC SI Slice */
  SLICE_SP = 3,                 /*!< AVC SP Slice */
  SLICE_GOLDEN = 3,             /*!< Golden Slice */
  SLICE_I = 2,                  /*!< I Slice (can contain I blocks) */
  SLICE_P = 1,                  /*!< P Slice (can contain I and P blocks) */
  SLICE_B = 0,                  /*!< B Slice (can contain I, P and B blocks) */
  SLICE_CONCEAL = 6,            /*!< Conceal Slice (slice was concealed) */
  SLICE_SKIP = 7,               /*!< Skip Slice */
  SLICE_REPEAT = 8,             /*!< VP9 Repeat Slice (repeats the content of its reference) */
  SLICE_MAX_ENUM,               /* sentinel */
};

typedef struct enc_dynamic_params
{
  uint16_t width;
  uint16_t height;
  double framerate;
  uint16_t rc_mode;
} enc_dynamic_params_t;

typedef struct _vcu_enc_usermeta
{
  int64_t pts;
  int frame_type;
} vcu_enc_usermeta;

typedef struct __obuf_info
{
  uint32_t obuff_index;
  uint32_t recv_size;
  vcu_enc_usermeta obuf_meta;
} obuf_info;

typedef struct xlnx_rc_fsfa
{
  uint32_t fs;
  uint32_t fa;
} xlnx_rc_fsfa_t;

typedef struct enc_dyn_params
{
  bool is_bitrate_changed;
  uint32_t bit_rate;
  bool is_bframes_changed;
  uint8_t num_b_frames;
} enc_dyn_params_t;

typedef struct host_dev_data
{
  uint32_t cmd_id;
  uint32_t cmd_rsp;
  uint32_t ibuf_size;
  uint32_t ibuf_count;
  uint32_t ibuf_index;
  uint64_t ibuf_paddr;
  uint32_t qpbuf_size;
  uint32_t qpbuf_count;
  uint32_t qpbuf_index;
  uint32_t obuf_size;
  uint32_t obuf_count;
  uint32_t freed_ibuf_index;
  uint32_t freed_qpbuf_index;
  uint16_t extradata_size;
  vcu_enc_usermeta ibuf_meta;
  obuf_info obuf_info_data[MAX_OUT_BUFF_COUNT];
  uint32_t freed_index_cnt;
  uint32_t obuf_indexes_to_release[MAX_OUT_BUFF_COUNT];
  uint32_t obuf_indexes_to_release_valid_cnt;
  bool is_idr;
  bool end_encoding;
  bool is_dyn_params_valid;
  enc_dyn_params_t dyn_params;
  uint8_t extradata[MAX_EXTRADATA_SIZE];
  uint32_t frame_sad[MAX_LOOKAHEAD_DEPTH];
  uint32_t frame_activity[MAX_LOOKAHEAD_DEPTH];
  uint32_t la_depth;
  char dev_err[MAX_ERR_STRING];
  uint32_t stride_width;
  uint32_t stride_height;
  uint32_t warn_buf_size;
#ifdef HDR_DATA_SUPPORT
  vcu_hdr_data hdr_data;
#endif
  bool duplicate_frame;
} sk_payload_data;

//////////////// FROM VCU XMA Plugin END //////////////////////
typedef struct mem_to_buff
{
  GQueue *bufq;
  gint memcount;                // num of GstBuffer of same GstMemory
} mem_to_bufpool;

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/x-h264, stream-format=(string)byte-stream, alignment=(string)au;"
        "video/x-h265, stream-format=(string)byte-stream, alignment=(string)au"));

#define VVAS_VCUENC_SINK_CAPS \
    "video/x-raw, " \
    "format = (string) {NV12, NV12_10LE32}, " \
    "width = (int) [ 1, 3840 ], " \
    "height = (int) [ 1, 2160 ], " \
    "framerate = " GST_VIDEO_FPS_RANGE

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VVAS_VCUENC_SINK_CAPS));

#define GST_TYPE_VVAS_VIDEO_ENC_CONTROL_RATE (gst_vvas_video_enc_control_rate_get_type ())
typedef enum
{
  RC_CONST_QP,
  RC_CBR,
  RC_VBR,
  RC_LOW_LATENCY,
} GstVvasVideoEncControlRate;

typedef struct _vvas_enc_copy_ob
{
  gboolean is_force_keyframe;
  GstBuffer *inbuf;
  GstBuffer *copy_inbuf;
} VvasEncCopyObject;

static guint
set_align_param (GstVvasXVCUEnc * enc, GstVideoInfo * info,
    GstVideoAlignment * align)
{
  gint width, height;
  guint stride = 0;

  if (!(GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_NV12_10LE32
          || GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_NV12))
    return FALSE;

  gst_video_alignment_reset (align);
  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);

  if (GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_NV12_10LE32) {
    guint tmp_stride;

    tmp_stride = (width + 2) / 3 * 4;
    stride = ALIGN (tmp_stride, enc->stride_align);

    align->padding_top = 0;
    align->padding_left = 0;
    align->padding_bottom = ALIGN (height, enc->height_align) - height;
    align->padding_right = (stride - tmp_stride) * 3 / 4;
  } else {
    align->padding_top = 0;
    align->padding_left = 0;
    align->padding_bottom = ALIGN (height, enc->height_align) - height;
    align->padding_right = ALIGN (width, enc->stride_align) - width;
    stride = ALIGN (width, enc->stride_align);
  }

  GST_LOG_OBJECT (enc, "fmt = %d width = %d height = %d",
      GST_VIDEO_INFO_FORMAT (info), width, height);
  GST_LOG_OBJECT (enc, "align top %d bottom %d right %d left =%d",
      align->padding_top, align->padding_bottom, align->padding_right,
      align->padding_left);
  GST_LOG_OBJECT (enc, "size = %u", (guint) ((stride * ALIGN (height,
                  enc->height_align)) * 1.5));
  return (stride * ALIGN (height, enc->height_align)) * 1.5;
}

static GType
gst_vvas_video_enc_control_rate_get_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue values[] = {
      {RC_CONST_QP, "Disable", "disable"},
      {RC_CBR, "Constant", "constant"},
      {RC_VBR, "Variable", "variable"},
      {RC_LOW_LATENCY, "Low Latency", "low-latency"},
      {0, NULL, NULL}
    };
    qtype = g_enum_register_static ("GstVvasVideoEncControlRate", values);
  }
  return qtype;
}

#define GST_TYPE_VVAS_VIDEO_ENC_QP_MODE (gst_vvas_video_enc_qp_mode_get_type ())
typedef enum
{
  UNIFORM_QP,
  AUTO_QP,
  ROI_QP,
  RELATIVE_LOAD,
} GstVvasVideoEncQpMode;

static GType
gst_vvas_video_enc_qp_mode_get_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue values[] = {
      {UNIFORM_QP, "Use the same QP for all coding units of the frame",
          "uniform"},
      {AUTO_QP,
            "Let the VCU encoder change the QP for each coding unit according to its content",
          "auto"},
      {ROI_QP,
            "Adjust QP according to the regions of interest defined on each frame. Must be set to handle ROI metadata.",
          "roi"},
      {RELATIVE_LOAD,
            "Use the information gathered in the lookahead to calculate the best QP",
          "relative-load"},
      {0, NULL, NULL}
    };
    qtype = g_enum_register_static ("GstVvasVideoEncQpMode", values);
  }
  return qtype;
}

#define GST_TYPE_VVAS_VIDEO_ENC_GOP_MODE (gst_vvas_video_enc_gop_mode_get_type ())
typedef enum
{
  DEFAULT_GOP,
  PYRAMIDAL_GOP,
  LOW_DELAY_P,
  LOW_DELAY_B,
} GstVvasVideoEncGopMode;

static GType
gst_vvas_video_enc_gop_mode_get_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue values[] = {
      {DEFAULT_GOP, "Basic GOP settings", "basic"},
      {PYRAMIDAL_GOP, "Advanced GOP pattern with hierarchical B-frames",
          "pyramidal"},
      {LOW_DELAY_P, "Single I-frame followed by P-frames only", "low-delay-p"},
      {LOW_DELAY_B, "Single I-frame followed by B-frames only", "low-delay-b"},
      {0, NULL, NULL}
    };
    qtype = g_enum_register_static ("GstVvasVideoEncGopMode", values);
  }
  return qtype;
}

#define GST_TYPE_VVAS_VIDEO_ENC_GDR_MODE (gst_vvas_video_enc_gdr_mode_get_type ())
typedef enum
{
  GDR_DISABLE,
  GDR_VERTICAL,
  GDR_HORIZONTAL,
} GstVvasVideoEncGdrMode;

static GType
gst_vvas_video_enc_gdr_mode_get_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue values[] = {
      {GDR_DISABLE, "No GDR", "disable"},
      {GDR_VERTICAL,
            "Gradual refresh using a vertical bar moving from left to right",
          "vertical"},
      {GDR_HORIZONTAL,
            "Gradual refresh using a horizontal bar moving from top to bottom",
          "horizontal"},
      {0, NULL, NULL}
    };
    qtype = g_enum_register_static ("GstVvasVideoEncGdrMode", values);
  }
  return qtype;
}

#define GST_TYPE_VVAS_VIDEO_ENC_SCALING_LIST (gst_vvas_video_enc_scaling_list_get_type ())
typedef enum
{
  SCALING_LIST_FLAT,
  SCALING_LIST_DEFAULT,
} GstVvasVideoEncScalingList;

static GType
gst_vvas_video_enc_scaling_list_get_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue values[] = {
      {SCALING_LIST_FLAT, "Flat scaling list mode", "flat"},
      {SCALING_LIST_DEFAULT, "Default scaling list mode", "default"},
      {0, NULL, NULL}
    };
    qtype = g_enum_register_static ("GstVvasVideoEncScalingList", values);
  }
  return qtype;
}

#define GST_TYPE_VVAS_VIDEO_ENC_ASPECT_RATIO (gst_vvas_video_enc_aspect_ratio_get_type ())
typedef enum
{
  ASPECT_RATIO_AUTO,
  ASPECT_RATIO_4_3,
  ASPECT_RATIO_16_9,
  ASPECT_RATIO_NONE,
} GstVvasVideoEncAspectRatio;

static GType
gst_vvas_video_enc_aspect_ratio_get_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue values[] = {
      {ASPECT_RATIO_AUTO,
            "4:3 for SD video,16:9 for HD video,unspecified for unknown format",
          "auto"},
      {ASPECT_RATIO_4_3, "4:3 aspect ratio", "4-3"},
      {ASPECT_RATIO_16_9, "16:9 aspect ratio", "16-9"},
      {ASPECT_RATIO_NONE,
          "Aspect ratio information is not present in the stream", "none"},
      {0, NULL, NULL}
    };
    qtype = g_enum_register_static ("GstVvasVideoEncAspectRatio", values);
  }
  return qtype;
}

#define GST_TYPE_VVAS_ENC_ENTROPY_MODE (gst_vvas_enc_entropy_mode_get_type ())
typedef enum
{
  MODE_CAVLC,
  MODE_CABAC,
} GstVvasEncEntropyMode;

static GType
gst_vvas_enc_entropy_mode_get_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue values[] = {
      {MODE_CAVLC, "CAVLC entropy mode", "CAVLC"},
      {MODE_CABAC, "CABAC entropy mode", "CABAC"},
      {0, NULL, NULL}
    };
    qtype = g_enum_register_static ("GstVvasEncEntropyMode", values);
  }
  return qtype;
}

#define GST_TYPE_VVAS_ENC_LOOP_FILTER_MODE (gst_vvas_enc_loop_filter_mode_get_type ())
typedef enum
{
  LOOP_FILTER_ENABLE,
  LOOP_FILTER_DISABLE,
  LOOP_FILTER_DISALE_SLICE_BOUNDARY,
} GstVvasEncLoopFilter;

static GType
gst_vvas_enc_loop_filter_mode_get_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue values[] = {
      {LOOP_FILTER_ENABLE, "Enable deblocking filter", "enable"},
      {LOOP_FILTER_DISABLE, "Disable deblocking filter", "disable"},
      {LOOP_FILTER_DISALE_SLICE_BOUNDARY,
            "Disables deblocking filter on slice boundary",
          "disable-slice-boundary"},
      {0, NULL, NULL}
    };
    qtype = g_enum_register_static ("GstVvasEncLoopFilter", values);
  }
  return qtype;
}

/*Defaults*/
#define GST_VVAS_VIDEO_ENC_ASPECT_RATIO_DEFAULT               ASPECT_RATIO_AUTO
#define GST_VVAS_VIDEO_ENC_CONTROL_RATE_DEFAULT                          RC_CBR
#define GST_VVAS_VIDEO_ENC_TARGET_BITRATE_DEFAULT                          5000
#define GST_VVAS_VIDEO_ENC_QP_MODE_DEFAULT                              AUTO_QP
#define GST_VVAS_VIDEO_ENC_MIN_QP_DEFAULT                                     0
#define GST_VVAS_VIDEO_ENC_MAX_QP_DEFAULT                                    51
#define GST_VVAS_VIDEO_ENC_GOP_MODE_DEFAULT                         DEFAULT_GOP
#define GST_VVAS_VIDEO_ENC_GDR_MODE_DEFAULT                         GDR_DISABLE
#define GST_VVAS_VIDEO_ENC_INITIAL_DELAY_DEFAULT                           1000
#define GST_VVAS_VIDEO_ENC_CPB_SIZE_DEFAULT                                2000
#define GST_VVAS_VIDEO_ENC_SCALING_LIST_DEFAULT            SCALING_LIST_DEFAULT
#define GST_VVAS_VIDEO_ENC_MAX_BITRATE_DEFAULT                             5000
#define GST_VVAS_VIDEO_ENC_MAX_QUALITY_DEFAULT                               14
#define GST_VVAS_VIDEO_ENC_FILLER_DATA_DEFAULT                            FALSE
#define GST_VVAS_VIDEO_ENC_NUM_SLICES_DEFAULT                                 1
#define GST_VVAS_VIDEO_ENC_SLICE_QP_DEFAULT                                  -1
#define GST_VVAS_VIDEO_ENC_SLICE_SIZE_DEFAULT                                 0
#define GST_VVAS_VIDEO_ENC_PREFETCH_BUFFER_DEFAULT                         TRUE
#define GST_VVAS_VIDEO_ENC_LONGTERM_REF_DEFAULT                           FALSE
#define GST_VVAS_VIDEO_ENC_LONGTERM_FREQUENCY_DEFAULT                         0
#define GST_VVAS_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT          G_MAXUINT
#define GST_VVAS_VIDEO_ENC_B_FRAMES_DEFAULT                                   2
#define GST_VVAS_VIDEO_ENC_GOP_LENGTH_DEFAULT                               120
#define GST_VVAS_VIDEO_ENC_ENTROPY_MODE_DEFAULT                      MODE_CABAC
#define GST_VVAS_VIDEO_ENC_CONSTRAINED_INTRA_PREDICTION_DEFAULT           FALSE
#define GST_VVAS_VIDEO_ENC_LOOP_FILTER_MODE_DEFAULT          LOOP_FILTER_ENABLE
#define GST_VVAS_VIDEO_ENC_LOW_BANDWIDTH_DEFAULT                          FALSE
#define GST_VVAS_VCU_ENC_SK_DEFAULT_NAME                   "kernel_vcu_encoder"
#define GST_VVAS_VIDEO_ENC_RC_MODE_DEFAULT                                 TRUE
#define GST_VVAS_VIDEO_ENC_TUNE_METRICS_DEFAULT                           FALSE
#define GST_VVAS_VIDEO_ENC_DEPENDENT_SLICE_DEFAULT                        FALSE
#define GST_VVAS_VIDEO_ENC_ULTRA_LOW_LATENCY_DEFAULT                      FALSE
#define GST_VVAS_VIDEO_ENC_AVC_LOWLAT_DEFAULT                             FALSE
#define GST_VVAS_VIDEO_ENC_IP_DELTA_DEFAULT                                  -1
#define GST_VVAS_VIDEO_ENC_PB_DELTA_DEFAULT                                  -1
#define GST_VVAS_VIDEO_ENC_LOOP_FILTER_BETA_DEFAULT                          -1
#define GST_VVAS_VIDEO_ENC_LOOP_FILTER_TC_DEFAULT                            -1
#define VVAS_VCUENC_KERNEL_NAME_DEFAULT                   "encoder:{encoder_1}"
#define DEFAULT_DEVICE_INDEX                                                 -1
#define DEFAULT_SK_CURRENT_INDEX                                             -1
#define UNSET_NUM_B_FRAMES                                                   -1

/* Properties */
enum
{
  PROP_0,
  PROP_XCLBIN_LOCATION,
  PROP_SK_NAME,
  PROP_SK_LIB_PATH,
  PROP_SK_START_INDEX,
  PROP_SK_CURRENT_INDEX,
  PROP_DEVICE_INDEX,
  PROP_TUNE_METRICS,
#ifdef ENABLE_XRM_SUPPORT
  PROP_RESERVATION_ID,
#endif
  /* VCU specific properties START */
  PROP_ASPECT_RATIO,
  PROP_B_FRAMES,
  PROP_CONSTRAINED_INTRA_PREDICTION,
  PROP_CONTROL_RATE,
  PROP_CPB_SIZE,
  PROP_ENTROPY_MODE,
  PROP_FILLER_DATA,
  PROP_GDR_MODE,
  PROP_GOP_LENGTH,
  PROP_GOP_MODE,
  PROP_INITIAL_DELAY,
  PROP_LOOP_FILTER_MODE,
  PROP_LOW_BANDWIDTH,
  PROP_MAX_BITRATE,
  PROP_MAX_QP,
  PROP_MIN_QP,
  PROP_NUM_SLICES,
  PROP_IDR_PERIODICITY,
  PROP_PREFETCH_BUFFER,
  PROP_QP_MODE,
  PROP_SCALING_LIST,
  PROP_SLICE_QP,
  PROP_SLICE_SIZE,
  PROP_TARGET_BITRATE,
  PROP_NUM_CORES,
  PROP_RATE_CONTROL_MODE,
  PROP_KERNEL_NAME,
  PROP_DEPENDENT_SLICE,
  PROP_IP_DELTA,
  PROP_PB_DELTA,
  PROP_LOOP_FILTER_BETA_OFFSET,
  PROP_LOOK_FILTER_TC_OFFSET,
  PROP_ENABLE_PIPELINE,
  PROP_ULTRA_LOW_LATENCY,
  PROP_AVC_LOWLAT,
  PROP_IN_MEM_BANK,
  PROP_OUT_MEM_BANK,
};

typedef struct _enc_obuf_info
{
  gint idx;
  gint size;
} enc_obuf_info;

struct _GstVvasXVCUEncPrivate
{
  GstBufferPool *input_pool;
  GHashTable *in_idx_hash;
  GHashTable *qbuf_hash;
  vvasDeviceHandle dev_handle;
  vvasKernelHandle kern_handle;
  uuid_t xclbinId;
  gint cu_idx;
  xrt_buffer *ert_cmd_buf;
  xrt_buffer *sk_payload_buf;
  xrt_buffer *static_cfg_buf;
  xrt_buffer *dyn_cfg_buf;
  GArray *out_xrt_bufs;
  GArray *qp_xrt_bufs;
  xrt_buffer *out_bufs_handle;
  xrt_buffer *qp_bufs_handle;
  gint num_in_idx;
  GList *read_oidx_list;
  gboolean init_done;
  gboolean flush_done;          /* to make sure FLUSH cmd issued to softkernel while exiting */
  gboolean deinit_done;         // TODO: instead maintain state of softkernel
  guint min_num_inbufs;
  guint in_buf_size;
  guint cur_qp_idx;
  gboolean intial_qpbufs_consumed;
  guint qpbuf_count;
  GstVideoAlignment in_align;
  sk_payload_data last_rcvd_payload;
  guint last_rcvd_oidx;
  uint64_t timestamp;           /* get current time when sending PREINIT command */
  gboolean has_error;
  gboolean allocated_intr_bufs;
  GThread *input_copy_thread;
  GAsyncQueue *copy_inqueue;
  GAsyncQueue *copy_outqueue;
  gboolean is_first_frame;
#ifdef ENABLE_XRM_SUPPORT
  xrmContext xrm_ctx;
  xrmCuListResourceV2 *cu_list_res;
  xrmCuResource *cu_res[2];
  gint cur_load;
  guint64 reservation_id;
#endif
  xrt_buffer *warn_buff_obj;
  gboolean is_dyn_params_valid;
  gboolean is_bitrate_changed;
  gboolean is_bframes_changed;
  gint64 retry_timeout;
  GCond timeout_cond;
  GMutex timeout_lock;
  xclDeviceHandle *xcl_dev_handle;
  gboolean xcl_ctx_valid;
};
#define gst_vvas_xvcuenc_parent_class parent_class

G_DEFINE_TYPE_WITH_PRIVATE (GstVvasXVCUEnc, gst_vvas_xvcuenc,
    GST_TYPE_VIDEO_ENCODER);
#define GST_VVAS_XVCUENC_PRIVATE(enc) \
    (GstVvasXVCUEncPrivate *) (gst_vvas_xvcuenc_get_instance_private (enc))

static void
vvas_xenc_copy_object_unref (gpointer data)
{
  VvasEncCopyObject *copy_obj = (VvasEncCopyObject *) data;
  if (copy_obj->inbuf)
    gst_buffer_unref (copy_obj->inbuf);
  if (copy_obj->copy_inbuf)
    gst_buffer_unref (copy_obj->copy_inbuf);
  g_slice_free (VvasEncCopyObject, copy_obj);
}

static const gchar *
vvas_get_vcu_level_string (const gchar * level, VvasCodecType codec_type)
{
  if (codec_type == VVAS_CODEC_H264) {
    if (!g_strcmp0 ("1", level)) {
      return level;
    } else if (!g_strcmp0 ("1.1", level)) {
      return level;
    } else if (!g_strcmp0 ("1.2", level)) {
      return level;
    } else if (!g_strcmp0 ("1.3", level)) {
      return level;
    } else if (!g_strcmp0 ("2", level)) {
      return level;
    } else if (!g_strcmp0 ("2.1", level)) {
      return level;
    } else if (!g_strcmp0 ("2.2", level)) {
      return level;
    } else if (!g_strcmp0 ("3", level)) {
      return level;
    } else if (!g_strcmp0 ("3.1", level)) {
      return level;
    } else if (!g_strcmp0 ("3.2", level)) {
      return level;
    } else if (!g_strcmp0 ("4", level)) {
      return level;
    } else if (!g_strcmp0 ("4.1", level)) {
      return level;
    } else if (!g_strcmp0 ("4.2", level)) {
      return level;
    } else if (!g_strcmp0 ("5", level)) {
      return level;
    } else if (!g_strcmp0 ("5.1", level)) {
      return level;
    } else if (!g_strcmp0 ("5.2", level)) {
      return level;
    }
  } else if (codec_type == VVAS_CODEC_H265) {
    if (!g_strcmp0 ("1", level)) {
      return level;
    } else if (!g_strcmp0 ("2", level)) {
      return level;
    } else if (!g_strcmp0 ("2.1", level)) {
      return level;
    } else if (!g_strcmp0 ("3", level)) {
      return level;
    } else if (!g_strcmp0 ("3.1", level)) {
      return level;
    } else if (!g_strcmp0 ("4", level)) {
      return level;
    } else if (!g_strcmp0 ("4.1", level)) {
      return level;
    } else if (!g_strcmp0 ("5", level)) {
      return level;
    } else if (!g_strcmp0 ("5.1", level)) {
      return level;
    } else if (!g_strcmp0 ("5.2", level)) {
      return level;
    }
  }
  return NULL;
}

static const gchar *
vvas_get_vcu_h264_profile_string (const gchar * profile)
{
  if (!g_strcmp0 ("baseline", profile)) {
    return "AVC_BASELINE";
  } else if (!g_strcmp0 ("main", profile)) {
    return "AVC_MAIN";
  } else if (!g_strcmp0 ("high", profile)) {
    return "AVC_HIGH";
  } else if (!g_strcmp0 ("high-10", profile)) {
    return "AVC_HIGH10";
  } else if (!g_strcmp0 ("high-10-intra", profile)) {
    return "AVC_HIGH10_INTRA";
  }
  return NULL;
}

static const gchar *
vvas_get_vcu_h265_profile_string (const gchar * profile)
{
  if (!g_strcmp0 ("main", profile)) {
    return "HEVC_MAIN";
  } else if (!g_strcmp0 ("main-10", profile)) {
    return "HEVC_MAIN10";
  } else if (!g_strcmp0 ("main-intra", profile)) {
    return "HEVC_MAIN_INTRA";
  } else if (!g_strcmp0 ("main-10-intra", profile)) {
    return "HEVC_MAIN10_INTRA";
  }
  return NULL;
}

static gboolean
gst_vvas_xvcuenc_map_params (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  gchar params[2048];
  gint width, height;
  const gchar *RateCtrlMode = "CONST_QP";
  const gchar *PrefetchBuffer = "DISABLE";
  const gchar *format = enc->bit_depth == 10 ? "NV12_10LE32" : "NV12";
//  const gchar *format = "NV12_10LE32";
  const gchar *GopCtrlMode = "DEFAULT_GOP";
  const gchar *EntropyMode = "MODE_CABAC";
  const gchar *QPCtrlMode = "UNIFORM_QP";
  const gchar *ScalingList = "DEFAULT";
  const gchar *LoopFilter = "ENABLE";
  const gchar *AspectRatio = "ASPECT_RATIO_AUTO";
  const gchar *EnableFillerData = "ENABLE";
  const gchar *GDRMode = "DISABLE";
  const gchar *ConstIntraPred = "DISABLE";
  const gchar *AvcLowLat = "DISABLE";
  const gchar *Profile = "AVC_HIGH";
  const gchar *Level = "1";
  const gchar *DependentSlice = "FALSE";
  gchar SliceQP[10];
  GstVideoInfo in_vinfo;
  gboolean bret = FALSE;
  gboolean iret;
  unsigned int fsize;
  gst_video_info_init (&in_vinfo);
  bret = gst_video_info_from_caps (&in_vinfo, enc->input_state->caps);
  if (!bret) {
    GST_ERROR_OBJECT (enc, "failed to get video info from input caps");
    return FALSE;
  }
  width = GST_VIDEO_INFO_WIDTH (&in_vinfo);
  height = GST_VIDEO_INFO_HEIGHT (&in_vinfo);

  switch (enc->control_rate) {
    case RC_CONST_QP:
      RateCtrlMode = "CONST_QP";
      break;
    case RC_VBR:
      RateCtrlMode = "VBR";
      break;
    case RC_CBR:
      RateCtrlMode = "CBR";
      break;
    case RC_LOW_LATENCY:
      RateCtrlMode = "LOW_LATENCY";
      break;
  }
  switch (enc->gop_mode) {
    case DEFAULT_GOP:
      GopCtrlMode = "DEFAULT_GOP";
      break;
    case PYRAMIDAL_GOP:
      GopCtrlMode = "PYRAMIDAL_GOP";
      break;
    case LOW_DELAY_P:
      GopCtrlMode = "LOW_DELAY_P";
      break;
    case LOW_DELAY_B:
      GopCtrlMode = "LOW_DELAY_B";
      break;
  }
  switch (enc->entropy_mode) {
    case MODE_CAVLC:
      EntropyMode = "MODE_CAVLC";
      break;
    case MODE_CABAC:
      EntropyMode = "MODE_CABAC";
      break;
  }
  switch (enc->qp_mode) {
    case UNIFORM_QP:
      QPCtrlMode = "UNIFORM_QP";
      break;
    case AUTO_QP:
      QPCtrlMode = "AUTO_QP";
      break;
    case ROI_QP:
      QPCtrlMode = "ROI_QP";
      break;
    case RELATIVE_LOAD:
      QPCtrlMode = "LOAD_QP | RELATIVE_QP";
      break;
  }
  switch (enc->scaling_list) {
    case SCALING_LIST_FLAT:
      ScalingList = "FLAT";
      break;
    case SCALING_LIST_DEFAULT:
      ScalingList = "DEFAULT";
      break;
  }
  switch (enc->loop_filter_mode) {
    case LOOP_FILTER_ENABLE:
      LoopFilter = "ENABLE";
      break;
    case LOOP_FILTER_DISABLE:
      LoopFilter = "DISABLE";
      break;
    case LOOP_FILTER_DISALE_SLICE_BOUNDARY:
      LoopFilter = "DISALE_SLICE_BOUNDARY";
      break;
  }
  switch (enc->prefetch_buffer) {
    case FALSE:
      PrefetchBuffer = "DISABLE";
      break;
    case TRUE:
      PrefetchBuffer = "ENABLE";
      break;
  }
  switch (enc->aspect_ratio) {
    case ASPECT_RATIO_AUTO:
      AspectRatio = "ASPECT_RATIO_AUTO";
      break;
    case ASPECT_RATIO_4_3:
      AspectRatio = "ASPECT_RATIO_4_3";
      break;
    case ASPECT_RATIO_16_9:
      AspectRatio = "ASPECT_RATIO_16_9";
      break;
    case ASPECT_RATIO_NONE:
      AspectRatio = "ASPECT_RATIO_NONE";
      break;
  }
  switch (enc->filler_data) {
    case FALSE:
      EnableFillerData = "DISABLE";
      break;
    case TRUE:
      EnableFillerData = "ENABLE";
      break;
  }
  switch (enc->gdr_mode) {
    case GDR_DISABLE:
      GDRMode = "DISABLE";
      break;
    case GDR_VERTICAL:
      GDRMode = "GDR_VERTICAL";
      break;
    case GDR_HORIZONTAL:
      GDRMode = "GDR_HORIZONTAL";
      break;
  }
  switch (enc->constrained_intra_prediction) {
    case FALSE:
      ConstIntraPred = "DISABLE";
      break;
    case TRUE:
      ConstIntraPred = "ENABLE";
      break;
  }

  switch (enc->avc_lowlat) {
    case FALSE:
      AvcLowLat = "DISABLE";
      break;
    case TRUE:
      AvcLowLat = "ENABLE";
      break;
  }

  if (enc->slice_qp == -1)
    strcpy (SliceQP, "AUTO");
  else
    sprintf (SliceQP, "%d", enc->slice_qp);

  if (!enc->level) {
    Level = "1";
    GST_WARNING_OBJECT (enc, "level not received from downstream, "
        "using default %s", Level);
  } else {
    Level = vvas_get_vcu_level_string (enc->level, enc->codec_type);
    if (!Level) {
      GST_ELEMENT_ERROR (enc, STREAM, WRONG_TYPE, NULL,
          ("level %s provided by downstream is not supported", enc->level));
      return FALSE;
    }
  }
  if (enc->periodicity_idr) {
    if (enc->gop_length > 0) {
      enc->periodicity_idr = enc->gop_length;
    }
  }

  if (enc->codec_type == VVAS_CODEC_H264) {
    Profile = vvas_get_vcu_h264_profile_string (enc->profile);
    if (!Profile) {
      g_warning ("profile %s not valid... using default AVC_HIGH",
          enc->profile);
      Profile = "AVC_HIGH";
      GST_WARNING_OBJECT (enc, "wrong profile %s received using default %s",
          enc->profile, Profile);
    }
    GST_LOG_OBJECT (enc, "profile = %s and level = %s", Profile, enc->level);

    sprintf (params, "[INPUT]\n"
        "Width = %d\n"
        "Height = %d\n"
        "Format = %s\n"
        "[RATE_CONTROL]\n"
        "RateCtrlMode = %s\n"
        "FrameRate = %d/%d\n"
        "BitRate = %d\n"
        "MaxBitRate = %d\n"
        "SliceQP = %s\n"
        "MaxQP = %u\n"
        "MinQP = %u\n"
        "IPDelta = %d\n"
        "PBDelta = %d\n"
        "CPBSize = %f\n"
        "InitialDelay = %f\n"
        "[GOP]\n"
        "GopCtrlMode = %s\n"
        "Gop.GdrMode = %s\n"
        "Gop.Length = %u\n"
        "Gop.NumB = %u\n"
        "Gop.FreqIDR = %d\n"
        "[SETTINGS]\n"
        "Profile = %s\n"
        "Level = %s\n"
        "ChromaMode = CHROMA_4_2_0\n"
        "BitDepth = %d\n"
        "NumSlices = %d\n"
        "QPCtrlMode = %s\n"
        "SliceSize = %d\n"
        "EnableFillerData = %s\n"
        "AspectRatio = %s\n"
        "ColourDescription = %s\n"
        "TransferCharac = %s\n"
        "ColourMatrix = %s\n"
        "ScalingList = %s\n"
        "EntropyMode = %s\n"
        "LoopFilter = %s\n"
        "LoopFilter.BetaOffset = %d\n"
        "LoopFilter.TcOffset = %d\n"
        "ConstrainedIntraPred = %s\n"
        "LambdaCtrlMode = DEFAULT_LDA\n"
        "CacheLevel2 = %s\n"
        "NumCore = %d\n"
        "AvcLowLat = %s\n",
        width, height, format, RateCtrlMode,
        GST_VIDEO_INFO_FPS_N (&in_vinfo), GST_VIDEO_INFO_FPS_D (&in_vinfo),
        enc->target_bitrate, enc->max_bitrate, SliceQP,
        enc->max_qp, enc->min_qp, enc->ip_delta, enc->pb_delta,
        (double) (enc->cpb_size) / 1000,
        (double) (enc->initial_delay) / 1000, GopCtrlMode, GDRMode,
        enc->gop_length, enc->b_frames, enc->periodicity_idr, Profile,
        Level, enc->bit_depth, enc->num_slices,
        QPCtrlMode, enc->slice_size, EnableFillerData, AspectRatio,
        enc->color_description, enc->transfer_characteristics,
        enc->color_matrix, ScalingList, EntropyMode, LoopFilter,
        enc->loop_filter_beta_offset, enc->loop_filter_tc_offset,
        ConstIntraPred, PrefetchBuffer, enc->num_cores, AvcLowLat);
  } else if (enc->codec_type == VVAS_CODEC_H265) {
    Profile = vvas_get_vcu_h265_profile_string (enc->profile);
    if (!Profile) {
      g_warning ("profile %s not valid... using default HEVC_MAIN",
          enc->profile);
      Profile = "HEVC_MAIN";
      GST_WARNING_OBJECT (enc, "wrong profile %s received using default %s",
          enc->profile, Profile);
    }

    switch (enc->dependent_slice) {
      case FALSE:
        DependentSlice = "FALSE";
        break;
      case TRUE:
        DependentSlice = "TRUE";
        break;
    }

    if (!enc->tier) {
      enc->tier = g_strdup ("MAIN_TIER");
    } else {
      if (g_strcmp0 (enc->tier, "high")
          && g_strcmp0 (enc->tier, "main")) {
        GST_ERROR_OBJECT (enc, "wrong tier %s received", enc->tier);
        goto error;
      }
      enc->tier =
          g_strdup (g_strcmp0 (enc->tier, "high") ? "MAIN_TIER" : "HIGH_TIER");
    }
    GST_LOG_OBJECT (enc, "profile = %s and level = %s and tier = %s", Profile,
        enc->level, enc->tier);

    sprintf (params, "[INPUT]\n"
        "Width = %d\n"
        "Height = %d\n"
        "Format = %s\n"
        "[RATE_CONTROL]\n"
        "RateCtrlMode = %s\n"
        "FrameRate = %d/%d\n"
        "BitRate = %d\n"
        "MaxBitRate = %d\n"
        "SliceQP = %s\n"
        "MaxQP = %u\n"
        "MinQP = %u\n"
        "IPDelta = %d\n"
        "PBDelta = %d\n"
        "CPBSize = %f\n"
        "InitialDelay = %f\n"
        "[GOP]\n"
        "GopCtrlMode = %s\n"
        "Gop.GdrMode = %s\n"
        "Gop.Length = %u\n"
        "Gop.NumB = %u\n"
        "Gop.FreqIDR = %d\n"
        "[SETTINGS]\n"
        "Profile = %s\n"
        "Level = %s\n"
        "Tier = %s\n"
        "BitDepth = %d\n"
        "NumSlices = %d\n"
        "QPCtrlMode = %s\n"
        "SliceSize = %d\n"
        "DependentSlice = %s\n"
        "EnableFillerData = %s\n"
        "AspectRatio = %s\n"
        "ColourDescription = %s\n"
        "TransferCharac = %s\n"
        "ColourMatrix = %s\n"
        "ScalingList = %s\n"
        "LoopFilter = %s\n"
        "LoopFilter.BetaOffset = %d\n"
        "LoopFilter.TcOffset = %d\n"
        "ConstrainedIntraPred = %s\n"
        "LambdaCtrlMode = DEFAULT_LDA\n"
        "CacheLevel2 = %s\n"
        "NumCore = %d\n",
        width, height, format, RateCtrlMode,
        GST_VIDEO_INFO_FPS_N (&in_vinfo), GST_VIDEO_INFO_FPS_D (&in_vinfo),
        enc->target_bitrate, enc->max_bitrate, SliceQP,
        enc->max_qp, enc->min_qp, enc->ip_delta, enc->pb_delta,
        (double) (enc->cpb_size) / 1000,
        (double) (enc->initial_delay) / 1000, GopCtrlMode, GDRMode,
        enc->gop_length, enc->b_frames, enc->periodicity_idr, Profile,
        Level, enc->tier, enc->bit_depth, enc->num_slices, QPCtrlMode,
        enc->slice_size, DependentSlice, EnableFillerData, AspectRatio,
        enc->color_description, enc->transfer_characteristics,
        enc->color_matrix, ScalingList, LoopFilter,
        enc->loop_filter_beta_offset, enc->loop_filter_tc_offset,
        ConstIntraPred, PrefetchBuffer, enc->num_cores);
  }

  priv->static_cfg_buf = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
  if (priv->static_cfg_buf == NULL) {
    GST_ERROR_OBJECT (enc, "failed to allocate encoder config memory handle");
    return FALSE;
  }
  fsize = strlen (params);

  GST_LOG_OBJECT (enc, "enocder params\n %s", params);
  iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle, fsize,
      VVAS_BO_FLAGS_NONE, enc->in_mem_bank, priv->static_cfg_buf);
  if (iret < 0) {
    GST_ERROR_OBJECT (enc, "failed to allocate encoder config buffer..");
    goto error;
  }
  strncpy (priv->static_cfg_buf->user_ptr, params, fsize);
  if (vvas_xrt_sync_bo (priv->static_cfg_buf->bo,
          VVAS_BO_SYNC_BO_TO_DEVICE, priv->static_cfg_buf->size, 0)) {
    GST_ERROR_OBJECT (enc, "unable to sync to static configuration to device");
    GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
        ("failed to sync static configuration to device. reason : %s",
            strerror (errno)));
    goto error1;
  }
  return TRUE;

error1:
  vvas_xrt_free_xrt_buffer (priv->static_cfg_buf);

error:
  free (priv->static_cfg_buf);
  priv->static_cfg_buf = NULL;
  return FALSE;
}

static gboolean
vvas_xvcuenc_check_softkernel_response (GstVvasXVCUEnc * enc,
    sk_payload_data * payload_buf)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  int iret;

  memset (payload_buf, 0, priv->sk_payload_buf->size);
  iret =
      vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_FROM_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (enc, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
        ("failed to sync response from encoder softkernel. reason : %s",
            strerror (errno)));
    return FALSE;
  }

  /* check response from softkernel */
  if (!payload_buf->cmd_rsp)
    return FALSE;

  return TRUE;
}

#ifdef ENABLE_XRM_SUPPORT
static gchar *
vvas_xvcuenc_prepare_request_json_string (GstVvasXVCUEnc * enc)
{
  json_t *req_obj;
  gchar *req_str;
  guint fps_n, fps_d;
  GstVideoInfo vinfo;
  guint in_width, in_height;
  gboolean bret;

  bret = gst_video_info_from_caps (&vinfo, enc->input_state->caps);
  if (!bret) {
    GST_ERROR_OBJECT (enc, "failed to get video info from caps");
    return FALSE;
  }

  in_width = GST_VIDEO_INFO_WIDTH (&vinfo);
  in_height = GST_VIDEO_INFO_HEIGHT (&vinfo);

  if (!in_width || !in_height) {
    GST_WARNING_OBJECT (enc, "input width & height not available. returning");
    return FALSE;
  }

  fps_n = GST_VIDEO_INFO_FPS_N (&vinfo);
  fps_d = GST_VIDEO_INFO_FPS_D (&vinfo);

  if (!fps_n) {
    g_warning ("frame rate not available in caps, taking default fps as 60");
    fps_n = 60;
    fps_d = 1;
  }

  req_obj = json_pack ("{s:{s:{s:[{s:s,s:s,s:{s:{s:i,s:i,s:{s:i,s:i}}}}]}}}",
      "request", "parameters", "resources", "function", "ENCODER",
      "format", enc->codec_type == VVAS_CODEC_H264 ? "H264" : "H265",
      "resolution", "input", "width", in_width, "height", in_height,
      "frame-rate", "num", fps_n, "den", fps_d);

  req_str = json_dumps (req_obj, JSON_DECODE_ANY);
  json_decref (req_obj);

  GST_LOG_OBJECT (enc, "prepared xrm request %s", req_str);

  return req_str;
}

static gboolean
vvas_xvcuenc_calculate_load (GstVvasXVCUEnc * enc, gint * load)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  int iret = -1, func_id = 0;
  gchar *req_str;
  char plugin_name[XRM_MAX_NAME_LEN];
  xrmPluginFuncParam param;

  if (!priv->xrm_ctx) {
    GST_ERROR_OBJECT (enc, "xrm context not created");
    return FALSE;
  }

  /* prepare json string to request xrm for load */
  req_str = vvas_xvcuenc_prepare_request_json_string (enc);
  if (!req_str) {
    GST_ERROR_OBJECT (enc, "failed to prepare xrm json request string");
    return FALSE;
  }

  memset (&param, 0x0, sizeof (xrmPluginFuncParam));
  memset (plugin_name, 0x0, XRM_MAX_NAME_LEN);

  strcpy (plugin_name, "xrmU30EncPlugin");

  if (strlen (req_str) > (XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1)) {
    GST_ERROR_OBJECT (enc, "request input string length %lu > max allowed %d",
        strlen (req_str), XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1);
    free (req_str);
    return FALSE;
  }

  strncpy (param.input, req_str, XRM_MAX_PLUGIN_FUNC_PARAM_LEN);
  free (req_str);

  iret = xrmExecPluginFunc (priv->xrm_ctx, plugin_name, func_id, &param);
  if (iret != XRM_SUCCESS) {
    GST_ERROR_OBJECT (enc, "failed to get load from xrm plugin. err : %d",
        iret);
    GST_ELEMENT_ERROR (enc, RESOURCE, FAILED,
        ("failed to get load from xrm plugin %s", plugin_name), NULL);
    priv->has_error = TRUE;
    return FALSE;
  }

  *load = atoi ((char *) (strtok (param.output, " ")));

  if (*load <= 0 || *load > XRM_MAX_CU_LOAD_GRANULARITY_1000000) {
    GST_ERROR_OBJECT (enc, "not an allowed encoder load %d", *load);
    GST_ELEMENT_ERROR (enc, RESOURCE, SETTINGS,
        ("wrong encoder load %d", *load), NULL);
    return FALSE;
  }

  GST_INFO_OBJECT (enc, "need %d%% device's load",
      (*load * 100) / XRM_MAX_CU_LOAD_GRANULARITY_1000000);
  return TRUE;
}

static gboolean
vvas_xvcuenc_allocate_resource (GstVvasXVCUEnc * enc, gint enc_load)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  int iret = -1;
  size_t num_hard_cus = -1;

  GST_INFO_OBJECT (enc, "going to request %d%% load using xrm",
      (enc_load * 100) / XRM_MAX_CU_LOAD_GRANULARITY_1000000);

  if (getenv ("XRM_RESERVE_ID") || enc->priv->reservation_id) { /* use reservation_id to allocate decoder */
    guint64 xrm_reserve_id = 0;
    xrmCuListPropertyV2 cu_list_prop;
    xrmCuListResourceV2 *cu_list_resource;

    if (!priv->cu_list_res) {
      cu_list_resource =
          (xrmCuListResourceV2 *) calloc (1, sizeof (xrmCuListResourceV2));
      if (!cu_list_resource) {
        GST_ERROR_OBJECT (enc, "failed to allocate memory");
        return FALSE;
      }
    } else {
      cu_list_resource = priv->cu_list_res;
    }

    memset (&cu_list_prop, 0, sizeof (xrmCuListPropertyV2));

    /* element property value takes higher priority than env variable */
    if (enc->priv->reservation_id)
      xrm_reserve_id = enc->priv->reservation_id;
    else
      xrm_reserve_id = atoi (getenv ("XRM_RESERVE_ID"));

    GST_INFO_OBJECT (enc, "going to request %d%% load using xrm with "
        "reservation id %lu", enc_load, xrm_reserve_id);

    cu_list_prop.cuNum = 2;
    strcpy (cu_list_prop.cuProps[0].kernelName, "encoder");
    strcpy (cu_list_prop.cuProps[0].kernelAlias, "ENCODER_MPSOC");
    cu_list_prop.cuProps[0].devExcl = false;
    cu_list_prop.cuProps[0].requestLoad =
        XRM_PRECISION_1000000_BIT_MASK (enc_load);
    cu_list_prop.cuProps[0].poolId = xrm_reserve_id;

    strcpy (cu_list_prop.cuProps[1].kernelName, "kernel_vcu_encoder");
    cu_list_prop.cuProps[1].devExcl = false;
    cu_list_prop.cuProps[1].requestLoad =
        XRM_PRECISION_1000000_BIT_MASK (XRM_MAX_CU_LOAD_GRANULARITY_1000000);
    cu_list_prop.cuProps[1].poolId = xrm_reserve_id;

    if (enc->dev_index != -1) {
      uint64_t deviceInfoContraintType =
          XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX;
      uint64_t deviceInfoDeviceIndex = enc->dev_index;

      cu_list_prop.cuProps[0].deviceInfo =
          (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) |
          (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
      cu_list_prop.cuProps[1].deviceInfo =
          (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) |
          (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
    }
    iret = xrmCuListAllocV2 (priv->xrm_ctx, &cu_list_prop, cu_list_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (enc, "failed to do CU list allocation using XRM");
      GST_ELEMENT_ERROR (enc, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from reservation id %lu",
              xrm_reserve_id), NULL);
      return FALSE;
    }

    num_hard_cus =
        vvas_xrt_get_num_compute_units (cu_list_resource->
        cuResources[0].xclbinFileName);

    if (num_hard_cus == -1) {
      GST_ERROR_OBJECT (enc, "failed to get number of cus in xclbin: %s",
          cu_list_resource->cuResources[0].xclbinFileName);
      return FALSE;
    }

    GST_DEBUG_OBJECT (enc, "Total Number of Compute Units: %ld in xclbin:%s",
        num_hard_cus, cu_list_resource->cuResources[0].xclbinFileName);

    priv->cu_list_res = cu_list_resource;
    enc->dev_index = cu_list_resource->cuResources[0].deviceId;
    priv->cu_idx = cu_list_resource->cuResources[0].cuId;
    enc->sk_cur_idx = cu_list_resource->cuResources[1].cuId - num_hard_cus;
    uuid_copy (priv->xclbinId, cu_list_resource->cuResources[0].uuid);

    GST_INFO_OBJECT (enc, "xrm CU list allocation success: dev-idx = %d, "
        "sk-cur-idx = %d and softkernel plugin name %s",
        enc->dev_index, enc->sk_cur_idx,
        cu_list_resource->cuResources[0].kernelPluginFileName);

  } else {                      /* use device ID to allocate decoder resource */
    xrmCuProperty cu_hw_prop, cu_sw_prop;
    xrmCuResource *cu_hw_resource, *cu_sw_resource;

    memset (&cu_hw_prop, 0, sizeof (xrmCuProperty));
    memset (&cu_sw_prop, 0, sizeof (xrmCuProperty));

    if (!priv->cu_res[0]) {
      cu_hw_resource = (xrmCuResource *) calloc (1, sizeof (xrmCuResource));
      if (!cu_hw_resource) {
        GST_ERROR_OBJECT (enc, "failed to allocate memory for hardCU resource");
        return FALSE;
      }
    } else {
      cu_hw_resource = priv->cu_res[0];
    }

    if (!priv->cu_res[1]) {
      cu_sw_resource = (xrmCuResource *) calloc (1, sizeof (xrmCuResource));
      if (!cu_sw_resource) {
        GST_ERROR_OBJECT (enc, "failed to allocate memory for softCU resource");
        return FALSE;
      }
    } else {
      cu_sw_resource = priv->cu_res[1];
    }


    GST_INFO_OBJECT (enc, "going to request %d%% load from device %d",
        enc_load, enc->dev_index);

    strcpy (cu_hw_prop.kernelName, "encoder");
    strcpy (cu_hw_prop.kernelAlias, "ENCODER_MPSOC");
    cu_hw_prop.devExcl = false;
    cu_hw_prop.requestLoad = XRM_PRECISION_1000000_BIT_MASK (enc_load);

    strcpy (cu_sw_prop.kernelName, "kernel_vcu_encoder");
    cu_sw_prop.devExcl = false;
    cu_sw_prop.requestLoad =
        XRM_PRECISION_1000000_BIT_MASK (XRM_MAX_CU_LOAD_GRANULARITY_1000000);

    /* allocate hardware resource */
    iret = xrmCuAllocFromDev (priv->xrm_ctx, enc->dev_index, &cu_hw_prop,
        cu_hw_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (enc, "failed to do hard CU allocation using XRM");
      GST_ELEMENT_ERROR (enc, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from device id %d", enc->dev_index),
          NULL);
      return FALSE;
    }

    /* allocate softkernel resource */
    iret = xrmCuAllocFromDev (priv->xrm_ctx, enc->dev_index, &cu_sw_prop,
        cu_sw_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (enc, "failed to do soft CU allocation using XRM");
      GST_ELEMENT_ERROR (enc, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from device id %d", enc->dev_index),
          NULL);
      return FALSE;
    }

    num_hard_cus =
        vvas_xrt_get_num_compute_units (cu_hw_resource->xclbinFileName);

    if (num_hard_cus == -1) {
      GST_ERROR_OBJECT (enc, "failed to get number of cus in xclbin: %s",
          cu_hw_resource->xclbinFileName);
      return FALSE;
    }

    GST_DEBUG_OBJECT (enc, "Total Number of Compute Units: %ld in xclbin:%s",
        num_hard_cus, cu_hw_resource->xclbinFileName);

    priv->cu_res[0] = cu_hw_resource;
    priv->cu_res[1] = cu_sw_resource;
    enc->dev_index = cu_hw_resource->deviceId;
    priv->cu_idx = cu_hw_resource->cuId;
    enc->sk_cur_idx = cu_sw_resource->cuId - num_hard_cus;
    uuid_copy (priv->xclbinId, cu_hw_resource->uuid);

    GST_INFO_OBJECT (enc, "xrm CU list allocation success: dev-idx = %d, "
        "cu-idx = %d, sk-cur-idx = %d and softkernel plugin name %s",
        enc->dev_index, priv->cu_idx, enc->sk_cur_idx,
        cu_hw_resource->kernelPluginFileName);
  }

  return TRUE;
}

#endif

static gboolean
vvas_xvcuenc_create_context (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
#ifdef ENABLE_XRM_SUPPORT
  gboolean bret;

  /* gets cu index & device id (using reservation id) */
  bret = vvas_xvcuenc_allocate_resource (enc, priv->cur_load);
  if (!bret)
    return FALSE;

#endif

  if (enc->sk_cur_idx < 0 || enc->sk_cur_idx < ENC_MIN_SK_CU_INDEX ||
      enc->sk_cur_idx > ENC_MAX_SK_CU_INDEX) {
    GST_ERROR_OBJECT (enc,
        "softkernel cu index %d is not valid. valid range %d -> %d",
        enc->sk_cur_idx ==
        DEFAULT_SK_CURRENT_INDEX ? enc->sk_cur_idx : enc->sk_cur_idx -
        ENC_SK_CU_START_OFFSET, ENC_MIN_SK_CU_INDEX, ENC_MAX_SK_CU_INDEX);
    GST_ELEMENT_ERROR (enc, RESOURCE, FAILED, (NULL),
        ("softkernel cu index %d is not valid. valid range %d -> %d",
            enc->sk_cur_idx ==
            DEFAULT_SK_CURRENT_INDEX ? enc->sk_cur_idx : enc->sk_cur_idx -
            ENC_SK_CU_START_OFFSET, ENC_MIN_SK_CU_INDEX, ENC_MAX_SK_CU_INDEX));
    return FALSE;
  }
  enc->priv->xcl_dev_handle = xclOpen (enc->dev_index, NULL, XCL_INFO);
  if (!(enc->priv->xcl_dev_handle)) {
    GST_ERROR_OBJECT (enc, "failed to open device index %u", enc->dev_index);
    return FALSE;;
  }

  if (!vvas_softkernel_xrt_open_device (enc->dev_index,
          enc->priv->xcl_dev_handle, &priv->dev_handle)) {
    GST_ERROR_OBJECT (enc, "failed to open device index %u", enc->dev_index);
    return FALSE;
  }

  /* TODO: Need to uncomment after CR-1122125 is resolved */
//#ifndef ENABLE_XRM_SUPPORT
  if (!enc->xclbin_path) {
    GST_ERROR_OBJECT (enc, "invalid xclbin path %s", enc->xclbin_path);
    GST_ELEMENT_ERROR (enc, RESOURCE, NOT_FOUND, (NULL),
        ("xclbin path not set"));
    return FALSE;
  }

  /* We have to download the xclbin irrespective of XRM or not as there
   * mismatch of UUID between XRM and XRT Native. CR-1122125 raised */
  if (vvas_xrt_download_xclbin (enc->xclbin_path,
          priv->dev_handle, &(priv->xclbinId))) {
    GST_ERROR_OBJECT (enc, "failed to initialize XRT");
    GST_ELEMENT_ERROR (enc, RESOURCE, FAILED, (NULL),
        ("xclbin download failed"));
    return FALSE;
  }
//#endif

  if (!enc->kernel_name)
    enc->kernel_name = g_strdup (VVAS_VCUENC_KERNEL_NAME_DEFAULT);

  GST_INFO_OBJECT (enc, "creating xrt conext : device index = %d, "
      "cu index = %d, sk index = %d",
      enc->dev_index, priv->cu_idx, enc->sk_cur_idx);
  if (xclOpenContext (priv->xcl_dev_handle, priv->xclbinId, priv->cu_idx, true)) {
    GST_ERROR_OBJECT (enc, "failed to open XRT context ...");
    return FALSE;
  }

  return TRUE;
}

static gboolean
vvas_xvcuenc_destroy_context (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  gboolean has_error = FALSE;
  gint iret;
#ifdef ENABLE_XRM_SUPPORT
  gboolean bret;

  if (priv->cu_list_res) {
    bret = xrmCuListReleaseV2 (priv->xrm_ctx, priv->cu_list_res);
    if (!bret) {
      GST_ERROR_OBJECT (enc, "failed to release resource");
      has_error = TRUE;
    }
    free (priv->cu_list_res);
    priv->cu_list_res = NULL;
  }

  if (priv->cu_res[0]) {
    bret = xrmCuRelease (priv->xrm_ctx, priv->cu_res[0]);
    if (!bret) {
      GST_ERROR_OBJECT (enc, "failed to release hardCU resource");
      has_error = TRUE;
    }

    free (priv->cu_res[0]);
    priv->cu_res[0] = NULL;
  }

  if (priv->cu_res[1]) {
    bret = xrmCuRelease (priv->xrm_ctx, priv->cu_res[1]);
    if (!bret) {
      GST_ERROR_OBJECT (enc, "failed to release softCU resource");
      has_error = TRUE;
    }

    free (priv->cu_res[1]);
    priv->cu_res[1] = NULL;
  }
#endif

  if (priv->xcl_ctx_valid) {
    iret = xclCloseContext (priv->xcl_dev_handle, priv->xclbinId, priv->cu_idx);
    if (iret < 0) {
      GST_ERROR_OBJECT (enc, "failed to close xrt context");
      has_error = TRUE;
    } else {
      GST_INFO_OBJECT (enc, "closed xrt context");
    }
  }

  if (priv->dev_handle) {
    vvas_xrt_close_device (priv->dev_handle);
    priv->dev_handle = NULL;
  }

  if (priv->xcl_dev_handle) {
    xclClose (priv->xcl_dev_handle);
    priv->xcl_dev_handle = NULL;
  }

  return has_error ? FALSE : TRUE;
}

static gboolean
vvas_xvcuenc_allocate_internal_buffers (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  int iret = 0;
  xclBufferHandle bh;
  xclBufferHandle *bh_ptr;

  if (priv->allocated_intr_bufs)
    return TRUE;                /* return if already allocated */

  GST_DEBUG_OBJECT (enc, "allocated internal buffers");

  priv->ert_cmd_buf = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
  if (priv->ert_cmd_buf == NULL) {
    GST_ERROR_OBJECT (enc, "failed to allocate ert cmd memory");
    goto error;
  }

  priv->sk_payload_buf = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
  if (priv->sk_payload_buf == NULL) {
    GST_ERROR_OBJECT (enc, "failed to allocate sk payload memory");
    goto error;
  }


  priv->dyn_cfg_buf = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
  if (priv->dyn_cfg_buf == NULL) {
    GST_ERROR_OBJECT (enc,
        "failed to allocate encoder dyncamic config memory handle");
    goto error;
  }

  priv->warn_buff_obj = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
  if (priv->warn_buff_obj == NULL) {
    GST_ERROR_OBJECT (enc,
        "failed to allocate encoder warning buffer memory handle");
    goto error;
  }
  bh = xclAllocBO (priv->xcl_dev_handle, ERT_CMD_SIZE, XCL_BO_MIRRORED_VIRTUAL,
      enc->in_mem_bank | XCL_BO_FLAGS_EXECBUF);
  if (bh == NULLBO) {
    GST_ERROR_OBJECT (enc, "failed to allocate Device BO...");
    goto error;
  }

  bh_ptr = (xclBufferHandle *) calloc (1, sizeof (xclBufferHandle));
  if (bh_ptr == NULL) {
    xclFreeBO (enc->priv->xcl_dev_handle, bh);
    GST_ERROR_OBJECT (enc, "failed to allocate Device BO...");
    goto error;
  }

  priv->ert_cmd_buf->user_ptr = xclMapBO (priv->xcl_dev_handle, bh, true);
  if (priv->ert_cmd_buf->user_ptr == NULL) {
    GST_ERROR_OBJECT (enc, "failed to map BO...");
    xclFreeBO (priv->xcl_dev_handle, bh);
    free (bh_ptr);
    goto error;
  }

  *(bh_ptr) = bh;
  priv->ert_cmd_buf->bo = bh_ptr;
  priv->ert_cmd_buf->size = ERT_CMD_SIZE;
  priv->ert_cmd_buf->phy_addr = 0;

  /* allocate softkernel payload buffer */
  iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
      sizeof (sk_payload_data), VVAS_BO_FLAGS_NONE,
      enc->in_mem_bank, priv->sk_payload_buf);
  if (iret < 0) {
    GST_ERROR_OBJECT (enc, "failed to allocate softkernel payload buffer..");
    goto error;
  }

  /* allocate encoder config buffer */
  iret =
      vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
      sizeof (enc_dynamic_params_t),
      VVAS_BO_FLAGS_NONE, enc->in_mem_bank, priv->dyn_cfg_buf);
  if (iret < 0) {
    GST_ERROR_OBJECT (enc,
        "failed to allocate encoder dynamic config buffer..");
    goto error;
  }

  /* allocate Warning buffer */
  iret =
      vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
      WARN_BUFF_MAX_SIZE, VVAS_BO_FLAGS_NONE,
      enc->in_mem_bank, priv->warn_buff_obj);
  if (iret < 0) {
    GST_ERROR_OBJECT (enc, "failed to allocate warning buffer..");
    goto error;
  }


  priv->allocated_intr_bufs = TRUE;

  return TRUE;

error:
  return FALSE;
}

static void
vvas_xvcuenc_free_internal_buffers (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;

  if (!priv->allocated_intr_bufs)
    return;

  GST_DEBUG_OBJECT (enc, "freeing internal buffers");

  if (priv->warn_buff_obj) {
    vvas_xrt_free_xrt_buffer (priv->warn_buff_obj);
    free (priv->warn_buff_obj);
    priv->warn_buff_obj = NULL;
  }

  if (priv->dyn_cfg_buf) {
    vvas_xrt_free_xrt_buffer (priv->dyn_cfg_buf);
    free (priv->dyn_cfg_buf);
    priv->dyn_cfg_buf = NULL;
  }

  if (priv->static_cfg_buf) {
    vvas_xrt_free_xrt_buffer (priv->static_cfg_buf);
    free (priv->static_cfg_buf);
    priv->static_cfg_buf = NULL;
  }

  if (priv->sk_payload_buf) {
    vvas_xrt_free_xrt_buffer (priv->sk_payload_buf);
    free (priv->sk_payload_buf);
    priv->sk_payload_buf = NULL;
  }

  if (priv->ert_cmd_buf) {
    vvas_free_ert_xcl_xrt_buffer (priv->xcl_dev_handle, priv->ert_cmd_buf);
    free (priv->ert_cmd_buf);
    priv->ert_cmd_buf = NULL;
  }

  priv->allocated_intr_bufs = FALSE;
}

static gboolean
vvas_xvcuenc_allocate_output_buffers (GstVvasXVCUEnc * enc, guint num_out_bufs,
    guint out_buf_size)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  uint64_t *out_bufs_addr;
  int iret = 0, i;

  GST_INFO_OBJECT (enc,
      "output buffer allocation: nbuffers = %u and output buffer size = %u",
      num_out_bufs, out_buf_size);

  priv->out_bufs_handle = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
  if (priv->out_bufs_handle == NULL) {
    GST_ERROR_OBJECT (enc,
        "failed to allocate encoder output buffers structure");
    goto error;
  }

  iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
      num_out_bufs * sizeof (uint64_t),
      VVAS_BO_FLAGS_NONE, enc->out_mem_bank, priv->out_bufs_handle);
  if (iret < 0) {
    GST_ERROR_OBJECT (enc, "failed to allocate encoder out buffers handle..");
    goto error;
  }

  out_bufs_addr = (uint64_t *) (priv->out_bufs_handle->user_ptr);

  for (i = 0; i < num_out_bufs; i++) {
    xrt_buffer *out_xrt_buf = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
    if (out_xrt_buf == NULL) {
      GST_ERROR_OBJECT (enc, "failed to allocate encoder output buffer");
      goto error;
    }

    iret =
        vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
        out_buf_size, VVAS_BO_FLAGS_NONE, enc->out_mem_bank, out_xrt_buf);
    if (iret < 0) {
      GST_ERROR_OBJECT (enc, "failed to allocate encoder output buffer..");
      goto error;
    }
    /* store each out physical address in priv strucuture */
    out_bufs_addr[i] = out_xrt_buf->phy_addr;
    g_array_append_val (priv->out_xrt_bufs, out_xrt_buf);
  }

  iret = vvas_xrt_sync_bo (priv->out_bufs_handle->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->out_bufs_handle->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (enc, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
        ("failed to sync output buffers handles to device. reason : %s",
            strerror (errno)));
    goto error;
  }

  return TRUE;
error:
  return FALSE;
}

static gboolean
vvas_xvcuenc_allocate_qp_buffers (GstVvasXVCUEnc * enc, guint num_qp_bufs,
    guint qp_buf_size)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  uint64_t *qp_bufs_addr;
  int iret = 0, i;

  GST_INFO_OBJECT (enc,
      "qp buffer allocation: nbuffers = %u and qp buffer size = %u",
      num_qp_bufs, qp_buf_size);

  priv->qp_bufs_handle = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
  if (priv->out_bufs_handle == NULL) {
    GST_ERROR_OBJECT (enc, "failed to allocate encoder qp buffers structure");
    goto error;
  }

  iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
      num_qp_bufs * sizeof (uint64_t),
      VVAS_BO_FLAGS_NONE, enc->in_mem_bank, priv->qp_bufs_handle);
  if (iret < 0) {
    GST_ERROR_OBJECT (enc, "failed to allocate encoder qp buffers handle..");
    goto error;
  }

  qp_bufs_addr = (uint64_t *) (priv->qp_bufs_handle->user_ptr);

  for (i = 0; i < num_qp_bufs; i++) {
    xrt_buffer *qp_xrt_buf = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
    if (qp_xrt_buf == NULL) {
      GST_ERROR_OBJECT (enc, "failed to allocate encoder qp buffer");
      goto error;
    }

    iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
        qp_buf_size, VVAS_BO_FLAGS_NONE, enc->in_mem_bank, qp_xrt_buf);
    if (iret < 0) {
      GST_ERROR_OBJECT (enc, "failed to allocate encoder qp buffer..");
      goto error;
    }
    /* store each out physical address in priv strucuture */
    qp_bufs_addr[i] = qp_xrt_buf->phy_addr;
    g_array_append_val (priv->qp_xrt_bufs, qp_xrt_buf);
  }

  iret =
      vvas_xrt_sync_bo (priv->qp_bufs_handle->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->qp_bufs_handle->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (enc, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
        ("failed to sync QP buffers handles to device. reason : %s",
            strerror (errno)));
    goto error;
  }

  return TRUE;
error:
  return FALSE;
}

static void
vvas_xvcuenc_free_output_buffers (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  xrt_buffer *out_xrt_buf = NULL;
  guint num_out_bufs;
  int i;

  num_out_bufs = priv->out_xrt_bufs->len;
  for (i = (num_out_bufs - 1); i >= 0; i--) {
    out_xrt_buf = g_array_index (priv->out_xrt_bufs, xrt_buffer *, i);
    g_array_remove_index (priv->out_xrt_bufs, i);
    vvas_xrt_free_xrt_buffer (out_xrt_buf);
    free (out_xrt_buf);
  }

  if (priv->out_bufs_handle) {
    vvas_xrt_free_xrt_buffer (priv->out_bufs_handle);
    free (priv->out_bufs_handle);
  }
}

static void
vvas_xvcuenc_free_qp_buffers (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  xrt_buffer *qp_xrt_buf = NULL;
  guint num_qp_bufs;
  int i;

  num_qp_bufs = priv->qp_xrt_bufs->len;
  for (i = (num_qp_bufs - 1); i >= 0; i--) {
    qp_xrt_buf = g_array_index (priv->qp_xrt_bufs, xrt_buffer *, i);
    g_array_remove_index (priv->qp_xrt_bufs, i);
    vvas_xrt_free_xrt_buffer (qp_xrt_buf);
    free (qp_xrt_buf);
  }

  if (priv->qp_bufs_handle) {
    vvas_xrt_free_xrt_buffer (priv->qp_bufs_handle);
    free (priv->qp_bufs_handle);
  }
}

static gboolean
vvas_xvcuenc_preinit (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  sk_payload_data *payload_buf;
  unsigned int payload_data[1024];
  unsigned int num_idx = 0;
  GstVideoInfo in_vinfo;
  gboolean bret = FALSE;
  enc_dynamic_params_t *dyn_cfg_params;
  int iret = -1;
  struct timespec init_time;

  gst_video_info_init (&in_vinfo);
  bret = gst_video_info_from_caps (&in_vinfo, enc->input_state->caps);
  if (!bret) {
    GST_ERROR_OBJECT (enc, "failed to get video info from input caps");
    goto error;
  }

  dyn_cfg_params = (enc_dynamic_params_t *) (priv->dyn_cfg_buf->user_ptr);
  memset (dyn_cfg_params, 0, priv->dyn_cfg_buf->size);

  dyn_cfg_params->width = GST_VIDEO_INFO_WIDTH (&in_vinfo);
  dyn_cfg_params->height = GST_VIDEO_INFO_HEIGHT (&in_vinfo);
  dyn_cfg_params->framerate =
      ((double) GST_VIDEO_INFO_FPS_N (&in_vinfo)) /
      GST_VIDEO_INFO_FPS_D (&in_vinfo);

  if (enc->rc_mode) {
    if (enc->control_rate == RC_CBR) {
      dyn_cfg_params->rc_mode = AL_RC_PLUGIN;
    } else {
      dyn_cfg_params->rc_mode = AL_RC_CONST_QP;
      g_warning ("disabling custom rate control as control-rate is not in CBR");
      enc->rc_mode = FALSE;
    }
  } else {
    dyn_cfg_params->rc_mode = AL_RC_CONST_QP;
  }

  GST_INFO_OBJECT (enc,
      "dynamic parameters to enc sk : w = %d, h = %d, fps = %f, rc_mode = %d",
      dyn_cfg_params->width, dyn_cfg_params->height, dyn_cfg_params->framerate,
      dyn_cfg_params->rc_mode);

  // for WItcher Stream
  if (dyn_cfg_params->framerate == 0)
    dyn_cfg_params->framerate = 30;

  iret = vvas_xrt_sync_bo (priv->dyn_cfg_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->dyn_cfg_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (enc, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
        ("failed to sync dynamic configuration to device. reason : %s",
            strerror (errno)));
    goto error;
  }

  bret = gst_vvas_xvcuenc_map_params (enc);
  if (!bret) {
    GST_ERROR_OBJECT (enc,
        "Failed to map encoder user parameters to device parameters");
    goto error;
  }

  /* update payload buf */
  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);
  memset (payload_buf, 0, priv->sk_payload_buf->size);

  payload_buf->cmd_id = VCU_PREINIT;

  iret = vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (enc, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
        ("failed to sync VCU_PREINIT command payload to device. reason : %s",
            strerror (errno)));
    goto error;
  }

  memset (payload_data, 0, 1024 * sizeof (int));
  clock_gettime (CLOCK_MONOTONIC, &init_time);
  priv->timestamp = ((init_time.tv_sec * 1e6) + (init_time.tv_nsec / 1e3));

  payload_data[num_idx++] = 0;
  payload_data[num_idx++] = VCU_PREINIT;
  payload_data[num_idx++] = getpid ();
  payload_data[num_idx++] = priv->timestamp & 0xFFFFFFFF;
  payload_data[num_idx++] = (priv->timestamp >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->sk_payload_buf->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->sk_payload_buf->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = sizeof (sk_payload_data);
  payload_data[num_idx++] = priv->static_cfg_buf->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->static_cfg_buf->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->static_cfg_buf->size;
  payload_data[num_idx++] = priv->dyn_cfg_buf->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->dyn_cfg_buf->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->dyn_cfg_buf->size;
  payload_data[num_idx++] = 0;  // TODO: Lambda not supported yet
  payload_data[num_idx++] = 0;  // TODO: Lambda not supported yet
  payload_data[num_idx++] = 0;  // TODO: Lambda not supported yet

  payload_data[num_idx++] = priv->warn_buff_obj->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->warn_buff_obj->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = WARN_BUFF_MAX_SIZE;

  GST_INFO_OBJECT (enc, "sending pre-init command to softkernel");

  iret =
      vvas_xrt_send_softkernel_command (priv->xcl_dev_handle, priv->ert_cmd_buf,
      payload_data, num_idx, enc->sk_cur_idx, CMD_EXEC_TIMEOUT);
  if (iret < 0) {
    GST_ERROR_OBJECT (enc,
        "failed to send VCU_PREINIT command to softkernel - %d, reason : %s",
        iret, strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, FAILED, NULL,
        ("failed to issue VCU_PREINIT command. reason : %s", strerror (errno)));
    goto error;
  } else {
    bret = vvas_xvcuenc_check_softkernel_response (enc, payload_buf);
    if (!bret) {
      GST_ERROR_OBJECT (enc, "softkernel pre-initialization failed");
      GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, NULL,
          ("encoder softkernel pre-initialization failed. reason : %s",
              payload_buf->dev_err));
      goto error;
    }
  }

  if (payload_buf->warn_buf_size
      && payload_buf->warn_buf_size < WARN_BUFF_MAX_SIZE) {
    memset (priv->warn_buff_obj->user_ptr, 0, WARN_BUFF_MAX_SIZE);

    iret =
        vvas_xrt_sync_bo (priv->warn_buff_obj->bo,
        VVAS_BO_SYNC_BO_FROM_DEVICE, payload_buf->warn_buf_size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (enc, "synbo failed - %d, reason : %s", iret,
          strerror (errno));
      GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
          ("failed to sync response Warning buffer. reason : %s",
              strerror (errno)));
      goto error;
    }
    GST_WARNING_OBJECT (enc, "device warning: %s",
        (char *) priv->warn_buff_obj->user_ptr);
  }


  enc->priv->min_num_inbufs = payload_buf->ibuf_count;
  enc->priv->in_buf_size = payload_buf->ibuf_size;
  enc->priv->qpbuf_count = payload_buf->qpbuf_count;

  GST_INFO_OBJECT (enc,
      "minimum input buffers required by encoder %u and input buffer size %u",
      payload_buf->ibuf_count, payload_buf->ibuf_size);
  GST_INFO_OBJECT (enc,
      "minimum output buffers required by encoder %u and output buffer size %u",
      payload_buf->obuf_count, payload_buf->obuf_size);
  GST_DEBUG_OBJECT (enc, "qp buffer count %u and size %u",
      payload_buf->qpbuf_count, payload_buf->qpbuf_size);

  if (!payload_buf->obuf_count || !payload_buf->obuf_size) {
    GST_ERROR_OBJECT (enc,
        "invalid params received from softkernel : outbuf count %u, outbuf size %u",
        payload_buf->obuf_count, payload_buf->obuf_size);
    goto error;
  }

  /* allocate number of output buffers based on softkernel requirement */
  bret = vvas_xvcuenc_allocate_output_buffers (enc, payload_buf->obuf_count,
      payload_buf->obuf_size);
  if (!bret)
    goto error;

  if (payload_buf->qpbuf_count && payload_buf->qpbuf_size) {
    /* allocate number of qp buffers based on softkernel requirement */
    bret = vvas_xvcuenc_allocate_qp_buffers (enc, payload_buf->qpbuf_count,
        payload_buf->qpbuf_size);
    if (!bret)
      goto error;
  }

  GST_INFO_OBJECT (enc, "Successfully pre-initialized softkernel");
  return TRUE;

error:
  return FALSE;
}

static void
vvas_xvcuenc_reset (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = GST_VVAS_XVCUENC_PRIVATE (enc);
  priv->num_in_idx = 0;
  priv->init_done = FALSE;
  priv->deinit_done = FALSE;
  priv->flush_done = FALSE;
  priv->read_oidx_list = NULL;
  priv->cur_qp_idx = 0;
  priv->qpbuf_count = 0;
  priv->has_error = FALSE;
  priv->allocated_intr_bufs = FALSE;
  priv->is_bframes_changed = FALSE;
  priv->is_bitrate_changed = FALSE;
  priv->retry_timeout = 0.1 * G_TIME_SPAN_MILLISECOND;

#ifdef ENABLE_XRM_SUPPORT
  priv->xrm_ctx = NULL;
  priv->cu_list_res = NULL;
  priv->cu_res[0] = priv->cu_res[1] = NULL;
  priv->cur_load = 0;
  priv->xcl_ctx_valid = FALSE;
#endif
}

static gboolean
vvas_xvcuenc_init (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  sk_payload_data *payload_buf;
  unsigned int payload_data[1024];
  unsigned int num_idx = 0;
  int iret = 0;
  gboolean bret = FALSE;

  /* update payload buf */
  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);
  memset (payload_buf, 0, priv->sk_payload_buf->size);

  payload_buf->cmd_id = VCU_INIT;
  iret = vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (enc, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
        ("failed to sync VCU_INIT command payload to device. reason : %s",
            strerror (errno)));
    goto error;
  }

  memset (payload_data, 0, 1024 * sizeof (int));
  payload_data[num_idx++] = 0;
  payload_data[num_idx++] = VCU_INIT;
  payload_data[num_idx++] = getpid ();
  payload_data[num_idx++] = priv->timestamp & 0xFFFFFFFF;
  payload_data[num_idx++] = (priv->timestamp >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->sk_payload_buf->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->sk_payload_buf->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = sizeof (sk_payload_data);
  payload_data[num_idx++] = 0;
  payload_data[num_idx++] = 0;
  payload_data[num_idx++] = 0;
  payload_data[num_idx++] = priv->out_bufs_handle->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->out_bufs_handle->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->out_bufs_handle->size;

  if (priv->qp_bufs_handle) {
    payload_data[num_idx++] = priv->qp_bufs_handle->phy_addr & 0xFFFFFFFF;
    payload_data[num_idx++] =
        ((uint64_t) (priv->qp_bufs_handle->phy_addr) >> 32) & 0xFFFFFFFF;
    payload_data[num_idx++] = priv->qp_bufs_handle->size;
  } else {
    payload_data[num_idx++] = 0;
    payload_data[num_idx++] = 0;
    payload_data[num_idx++] = 0;
  }

  iret =
      vvas_xrt_send_softkernel_command (priv->xcl_dev_handle, priv->ert_cmd_buf,
      payload_data, num_idx, enc->sk_cur_idx, CMD_EXEC_TIMEOUT);
  if (iret < 0) {
    GST_ERROR_OBJECT (enc,
        "failed to send VCU_INIT command to softkernel - %d, reason : %s",
        iret, strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, FAILED, NULL,
        ("failed to issue VCU_INIT command. reason : %s", strerror (errno)));
    goto error;
  } else {
    bret = vvas_xvcuenc_check_softkernel_response (enc, payload_buf);
    if (!bret) {
      GST_ERROR_OBJECT (enc, "softkernel initialization failed");
      GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, NULL,
          ("softkernel initialization failed. reason : %s",
              payload_buf->dev_err));
      goto error;
    }
  }

  GST_INFO_OBJECT (enc, "Successfully initialized softkernel");
  return TRUE;

error:
  return FALSE;
}

static gboolean
vvas_xvcuenc_allocate_internal_pool (GstVvasXVCUEnc * enc, GstCaps * caps)
{
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstVideoAlignment align;
  GstAllocationParams alloc_params;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (enc, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  pool = gst_vvas_buffer_pool_new (enc->stride_align, enc->height_align);

  allocator = gst_vvas_allocator_new (enc->dev_index,
      ENABLE_DMABUF, enc->in_mem_bank, enc->priv->kern_handle);

  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
  alloc_params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  set_align_param (enc, &info, &align);

  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_set_video_alignment (config, &align);

  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  gst_buffer_pool_config_set_params (config, caps, enc->priv->in_buf_size,
      enc->priv->min_num_inbufs, enc->priv->min_num_inbufs + 1);
  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (enc, "Failed to set config on input pool");
    goto error;
  }

  if (enc->priv->input_pool)
    gst_object_unref (enc->priv->input_pool);

  enc->priv->input_pool = pool;

  GST_INFO_OBJECT (enc, "allocated %" GST_PTR_FORMAT " pool",
      enc->priv->input_pool);

  if (allocator)
    gst_object_unref (allocator);

  return TRUE;

error:
  if (allocator)
    gst_object_unref (allocator);
  return FALSE;
}

static gboolean
vvas_xvcuenc_validate_buffer_import (GstVvasXVCUEnc * enc, GstBuffer * inbuf,
    gboolean * use_inpool)
{
  GstStructure *in_config = NULL;
  gboolean bret = TRUE;
  GstMemory *in_mem = NULL;
  GstVideoAlignment in_align = { 0, };
  GstVideoMeta *vmeta;

  in_mem = gst_buffer_get_memory (inbuf, 0);
  if (in_mem == NULL) {
    GST_ERROR_OBJECT (enc, "failed to get memory from input buffer");
    bret = FALSE;
    goto exit;
  }

  if (gst_is_vvas_memory (in_mem) &&
      gst_vvas_memory_can_avoid_copy (in_mem, enc->dev_index,
          enc->in_mem_bank)) {
    if (inbuf->pool && (in_config = gst_buffer_pool_get_config (inbuf->pool)) &&
        gst_buffer_pool_config_get_video_alignment (in_config, &in_align)) {

      /* buffer belonged pool, so get alignement info */
      if (in_align.padding_top || in_align.padding_left) {
        bret = FALSE;
        g_error ("padding top and padding left should be zero");
        goto exit;
      }
    }

    vmeta = gst_buffer_get_video_meta (inbuf);
    if (vmeta) {
      gint align_elevation;

      GST_LOG_OBJECT (enc, "input buffer offset[0] = %lu, offset[1] = %lu, "
          "stride[0] = %d, stride[1] = %d, size = %lu", vmeta->offset[0],
          vmeta->offset[1], vmeta->stride[0], vmeta->stride[1],
          gst_buffer_get_size (inbuf));

      align_elevation =
          (vmeta->offset[1] - vmeta->offset[0]) / vmeta->stride[0];

      if (vmeta->stride[0] % enc->stride_align
          || align_elevation % enc->height_align) {
        *use_inpool = TRUE;
        GST_DEBUG_OBJECT (enc,
            "strides & offsets are not matching, use our internal pool");
        goto exit;
      }
    } else {
      *use_inpool = TRUE;
    }
  } else {
    /* allocate internal pool to copy input buffer */
    *use_inpool = TRUE;
  }

  GST_INFO_OBJECT (enc, "going to use %s pool as input pool",
      *use_inpool ? "internal" : "upstream");

exit:
  if (in_mem)
    gst_memory_unref (in_mem);
  if (in_config)
    gst_structure_free (in_config);
  return bret;
}

static gboolean
set_align_param_sk (GstVvasXVCUEnc * enc, sk_payload_data * payload_buf,
    GstBuffer * inbuf)
{
  GstVideoMeta *vmeta;
  GstStructure *config = NULL;
  gboolean ret = FALSE;

  payload_buf->stride_width = 0;
  payload_buf->stride_height = 0;

  vmeta = gst_buffer_get_video_meta (inbuf);
  if (!vmeta)
    goto done;

  payload_buf->stride_width = vmeta->stride[0];
  payload_buf->stride_height =
      (vmeta->offset[1] - vmeta->offset[0]) / vmeta->stride[0];
  ret = TRUE;

done:
  if (config)
    gst_structure_free (config);
  GST_LOG_OBJECT (enc, "payload_buf->stride_width = %d",
      payload_buf->stride_width);
  GST_LOG_OBJECT (enc, "payload_buf->stride_height = %d",
      payload_buf->stride_height);
  return ret;
}

static gpointer
vvas_xvcuenc_input_copy_thread (gpointer data)
{
  GstVvasXVCUEnc *enc = GST_VVAS_XVCUENC (data);
  GstVvasXVCUEncPrivate *priv = enc->priv;

  while (1) {
    VvasEncCopyObject *inobj = NULL;
    GstBuffer *inbuf = NULL, *own_inbuf = NULL;
    GstVideoFrame own_vframe, in_vframe;
    GstFlowReturn fret = GST_FLOW_OK;

    inobj = (VvasEncCopyObject *) g_async_queue_pop (priv->copy_inqueue);
    if (inobj == STOP_COMMAND) {
      GST_DEBUG_OBJECT (enc, "received stop command. exit copy thread");
      break;
    }

    inbuf = inobj->inbuf;

    /* acquire buffer from own input pool */
    fret =
        gst_buffer_pool_acquire_buffer (enc->priv->input_pool, &own_inbuf,
        NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (enc, "failed to allocate buffer from pool %p",
          enc->priv->input_pool);
      goto error;
    }
    GST_LOG_OBJECT (enc, "acquired buffer %p from own pool", own_inbuf);

    /* map internal buffer in write mode */
    if (!gst_video_frame_map (&own_vframe, &enc->input_state->info, own_inbuf,
            GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (enc, "failed to map internal input buffer");
      goto error;
    }

    /* map input buffer in read mode */
    if (!gst_video_frame_map (&in_vframe, &enc->input_state->info,
            inbuf, GST_MAP_READ)) {
      GST_ERROR_OBJECT (enc, "failed to map input buffer");
      goto error;
    }
    gst_video_frame_copy (&own_vframe, &in_vframe);

    GST_LOG_OBJECT (enc, "copy buffer %p to %p", inbuf, own_inbuf);

    gst_video_frame_unmap (&in_vframe);
    gst_video_frame_unmap (&own_vframe);
    gst_buffer_copy_into (own_inbuf, inbuf,
        (GstBufferCopyFlags) GST_BUFFER_COPY_METADATA, 0, -1);

    gst_buffer_unref (inbuf);
    inobj->inbuf = NULL;
    inobj->copy_inbuf = own_inbuf;
    g_async_queue_push (priv->copy_outqueue, inobj);
  }

error:
  return NULL;
}

static gboolean
vvas_xvcuenc_process_input_frame (GstVvasXVCUEnc * enc,
    GstVideoCodecFrame * frame, GstBuffer ** inbuf,
    gboolean * is_force_keyframe)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  gboolean use_inpool = FALSE;
  gboolean bret = FALSE;

  bret = vvas_xvcuenc_validate_buffer_import (enc, frame->input_buffer,
      &use_inpool);
  if (!bret)
    goto error;

  if (G_UNLIKELY (use_inpool)) {
    GstVideoFrame in_vframe, own_vframe;
    GstFlowReturn fret;
    GstBuffer *own_inbuf = NULL;

    memset (&in_vframe, 0x0, sizeof (GstVideoFrame));
    memset (&own_vframe, 0x0, sizeof (GstVideoFrame));

    if (!priv->input_pool) {
      /* allocate internal buffer pool to copy input frames */
      bret = vvas_xvcuenc_allocate_internal_pool (enc, enc->input_state->caps);
      if (!bret)
        goto error;

      if (!gst_buffer_pool_is_active (priv->input_pool))
        gst_buffer_pool_set_active (priv->input_pool, TRUE);
    }

    if (enc->enabled_pipeline) {
      VvasEncCopyObject *inobj = NULL;
      VvasEncCopyObject *outobj = NULL;

      inobj = g_slice_new0 (VvasEncCopyObject);
      inobj->inbuf = gst_buffer_ref (frame->input_buffer);
      inobj->is_force_keyframe =
          GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame);

      outobj = g_async_queue_try_pop (priv->copy_outqueue);
      if (!outobj && !priv->is_first_frame) {
        outobj = g_async_queue_pop (priv->copy_outqueue);
      }

      priv->is_first_frame = FALSE;

      g_async_queue_push (priv->copy_inqueue, inobj);

      if (!outobj) {
        GST_LOG_OBJECT (enc, "copied input buffer is not available. return");
        *inbuf = NULL;
        *is_force_keyframe = FALSE;
        return TRUE;
      } else {
        own_inbuf = outobj->copy_inbuf;
        *is_force_keyframe = outobj->is_force_keyframe;
        g_slice_free (VvasEncCopyObject, outobj);
      }
    } else {
      /* acquire buffer from own input pool */
      fret =
          gst_buffer_pool_acquire_buffer (enc->priv->input_pool, &own_inbuf,
          NULL);
      if (fret != GST_FLOW_OK) {
        GST_ERROR_OBJECT (enc, "failed to allocate buffer from pool %p",
            enc->priv->input_pool);
        goto error;
      }
      GST_LOG_OBJECT (enc, "acquired buffer %p from own pool", own_inbuf);

      /* map internal buffer in write mode */
      if (!gst_video_frame_map (&own_vframe, &enc->input_state->info, own_inbuf,
              GST_MAP_WRITE)) {
        GST_ERROR_OBJECT (enc, "failed to map internal input buffer");
        goto error;
      }

      /* map input buffer in read mode */
      if (!gst_video_frame_map (&in_vframe, &enc->input_state->info,
              frame->input_buffer, GST_MAP_READ)) {
        GST_ERROR_OBJECT (enc, "failed to map input buffer");
        goto error;
      }

      GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, enc,
          "slow copy data from %p to %p", frame->input_buffer, own_inbuf);
      gst_video_frame_copy (&own_vframe, &in_vframe);

      gst_video_frame_unmap (&in_vframe);
      gst_video_frame_unmap (&own_vframe);
      gst_buffer_copy_into (own_inbuf, frame->input_buffer,
          (GstBufferCopyFlags) GST_BUFFER_COPY_METADATA, 0, -1);

      *is_force_keyframe = GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame);
    }

    *inbuf = own_inbuf;
  } else {
    *is_force_keyframe = GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame);
    *inbuf = gst_buffer_ref (frame->input_buffer);
  }

  return TRUE;

error:
  *inbuf = NULL;
  return FALSE;
}

static gboolean
vvas_xvcuenc_send_frame (GstVvasXVCUEnc * enc, GstBuffer * inbuf,
    gboolean is_force_keyframe)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  sk_payload_data *payload_buf;
  unsigned int payload_data[1024];
  unsigned int num_idx = 0;
  int iret = 0, i;
  gboolean bret = FALSE;
  GstMemory *in_mem = NULL;
  guint cur_in_idx = 0xBAD;
  GstVvasLAMeta *lameta = NULL;
#ifdef HDR_DATA_SUPPORT
  GstVvasHdrMeta *hdr_meta = NULL;
#endif
  gpointer in_paddr = NULL;
  gboolean force_key_frame = FALSE;
  gboolean is_duplicate = FALSE;

  if (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP)) {
    is_duplicate = TRUE;
    GST_DEBUG_OBJECT (enc, "duplicate buffer %" GST_PTR_FORMAT, inbuf);
  }

  in_mem = gst_buffer_get_memory (inbuf, 0);
  if (in_mem == NULL) {
    GST_ERROR_OBJECT (enc, "failed to get memory from internal input buffer");
    goto error;
  }
  in_paddr = (gpointer *) gst_vvas_allocator_get_paddr (in_mem);

  bret = gst_vvas_memory_sync_bo (in_mem);
  if (!bret) {
    GST_ERROR_OBJECT (enc, "failed to sync data");
    goto error;
  }

  if (!g_hash_table_contains (priv->in_idx_hash, in_paddr)) {
    g_hash_table_insert (priv->in_idx_hash, in_paddr,
        GINT_TO_POINTER (priv->num_in_idx));
    GST_DEBUG_OBJECT (enc,
        "insert new index %d with physical address %p and buffer %p",
        priv->num_in_idx, in_paddr, inbuf);
    cur_in_idx = priv->num_in_idx++;
  } else {
    cur_in_idx =
        GPOINTER_TO_INT (g_hash_table_lookup (priv->in_idx_hash, in_paddr));
    GST_DEBUG_OBJECT (enc,
        "acquired index %d with physical address %p and buffer %p", cur_in_idx,
        in_paddr, inbuf);
  }

  inbuf = gst_buffer_ref (inbuf);

  if (!g_hash_table_contains (priv->qbuf_hash, GINT_TO_POINTER (cur_in_idx))) {
    mem_to_bufpool *bufpool = g_new (mem_to_bufpool, 1);
    bufpool->bufq = g_queue_new ();
    g_queue_push_tail (bufpool->bufq, inbuf);
    bufpool->memcount = 1;
    g_hash_table_insert (priv->qbuf_hash, GINT_TO_POINTER (cur_in_idx),
        bufpool);

    GST_LOG_OBJECT (enc, "new queue (%p) created at hash index %d",
        bufpool->bufq, cur_in_idx);
    GST_LOG_OBJECT (enc, "insert buf (%p) at location %d of hash index %d",
        inbuf, bufpool->memcount, cur_in_idx);
  } else {
    mem_to_bufpool *bufpool =
        g_hash_table_lookup (priv->qbuf_hash, GINT_TO_POINTER (cur_in_idx));

    g_queue_push_tail (bufpool->bufq, inbuf);
    bufpool->memcount++;
    GST_LOG_OBJECT (enc, "acquire queue (%p) at hash index %d",
        bufpool->bufq, cur_in_idx);
    GST_LOG_OBJECT (enc, "insert buf (%p) at location %d of hash index %d",
        inbuf, bufpool->memcount, cur_in_idx);
  }

  /* update payload buf */
  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);
  memset (payload_buf, 0, priv->sk_payload_buf->size);

  payload_buf->cmd_id = VCU_PUSH;
  payload_buf->ibuf_index = cur_in_idx;
  payload_buf->ibuf_size = gst_buffer_get_size (inbuf);
  payload_buf->ibuf_paddr = gst_vvas_allocator_get_paddr (in_mem);
  payload_buf->ibuf_meta.pts = GST_BUFFER_PTS (inbuf);
  payload_buf->obuf_indexes_to_release_valid_cnt =
      g_list_length (priv->read_oidx_list);
  payload_buf->duplicate_frame = is_duplicate;

  set_align_param_sk (enc, payload_buf, inbuf);

  /* Force Key frame */
  force_key_frame = is_force_keyframe;
  if (force_key_frame) {
    payload_buf->is_idr = 1;
    GST_DEBUG_OBJECT (enc, "Setting IDR frame for PTS = %ld\n",
        GST_BUFFER_PTS (inbuf));
  }
#ifdef HDR_DATA_SUPPORT
  hdr_meta = gst_buffer_get_vvas_hdr_meta (inbuf);
  if (hdr_meta) {
    memcpy (&payload_buf->hdr_data, &hdr_meta->hdr_metadata,
        sizeof (vcu_hdr_data));
  }
#endif

  /* Update Encoder dynamic parameters */
  GST_OBJECT_LOCK (enc);
  payload_buf->is_dyn_params_valid = priv->is_dyn_params_valid;

  payload_buf->dyn_params.is_bitrate_changed = priv->is_bitrate_changed;
  payload_buf->dyn_params.bit_rate = enc->target_bitrate * 1000;

  payload_buf->dyn_params.is_bframes_changed = priv->is_bframes_changed;
  payload_buf->dyn_params.num_b_frames = enc->b_frames;

  priv->is_dyn_params_valid = FALSE;
  priv->is_bframes_changed = FALSE;
  priv->is_bitrate_changed = FALSE;

  lameta = gst_buffer_get_vvas_la_meta (inbuf);
  if (lameta) {
    gboolean invalid_param = FALSE;

    /* compare lookahead & encoder parameters for validation */
    if (lameta->gop_length != enc->gop_length) {
      GST_ERROR_OBJECT (enc, "gop length in LAMeta (%u) is not equal to "
          "encoder's gop length (%u)", lameta->gop_length, enc->gop_length);
      invalid_param = TRUE;
    }

    if ((lameta->num_bframes != enc->b_frames) && (!enc->ultra_low_latency)) {
      if (enc->gop_mode == DEFAULT_GOP) {
        payload_buf->dyn_params.is_bframes_changed = TRUE;
        payload_buf->dyn_params.num_b_frames = lameta->num_bframes;
        enc->b_frames = lameta->num_bframes;
        payload_buf->is_dyn_params_valid = TRUE;
      } else {
        GST_ERROR_OBJECT (enc, "b-frames in LAMeta (%u) cannot be changed as "
            "encoder GOP mode (%u) is not basic ", lameta->gop_length,
            enc->gop_mode);
        invalid_param = TRUE;
      }
    } else if ((lameta->num_bframes != enc->b_frames)
        && (enc->ultra_low_latency)) {
      GST_ERROR_OBJECT (enc,
          "b-frames in LAMeta (%u) cannot be changed as "
          "encoder ultra-low-latency mode is enabled", lameta->num_bframes);
      invalid_param = TRUE;
    }

    if (lameta->codec_type != enc->codec_type) {
      GST_ERROR_OBJECT (enc, "LAMeta code type (%u) is not equal to "
          "encoder's codec type (%u)", lameta->codec_type, enc->codec_type);
      invalid_param = TRUE;
    }

    if (lameta->is_idr == TRUE) {
      payload_buf->is_idr = 1;
      GST_DEBUG_OBJECT (enc, "LaMeta enabled IDR frame for PTS = %ld\n",
          GST_BUFFER_PTS (inbuf));
    }
    GST_OBJECT_UNLOCK (enc);

    if (invalid_param) {
      GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
          ("Properites gop-length/b-frames/codec-type of lookahead are not "
              "matching with encoder properties"));
      goto error;
    }

    if (enc->rc_mode && (lameta->lookahead_depth < 2)) {
      GST_ERROR_OBJECT (enc,
          "can't do custom rate control as lookahead depth < 2");
      GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
          ("can't do custom rate control as lookahead depth < 2"));
      goto error;
    }

    if (lameta->qpmap && priv->qpbuf_count) {
      GstMapInfo qpinfo = GST_MAP_INFO_INIT;
      gboolean bret = FALSE;
      xrt_buffer *qp_xrt_buf = NULL;

      payload_buf->qpbuf_index = priv->cur_qp_idx;

      qp_xrt_buf = g_array_index (priv->qp_xrt_bufs, xrt_buffer *,
          priv->cur_qp_idx);

      bret = gst_buffer_map (lameta->qpmap, &qpinfo, GST_MAP_READ);
      if (!bret) {
        GST_ERROR_OBJECT (enc, "failed to map qpmap buffer");
        goto error;
      }

      if (qp_xrt_buf->size - 64 < qpinfo.size) {
        GST_ERROR_OBJECT (enc, "qpinfo size (%lu) > device qp buffer size (%d)",
            qpinfo.size, qp_xrt_buf->size - 64);
        goto error;
      }

      memcpy ((gchar *) qp_xrt_buf->user_ptr + 64, qpinfo.data, qpinfo.size);
      gst_buffer_unmap (lameta->qpmap, &qpinfo);

      iret = vvas_xrt_sync_bo (qp_xrt_buf->bo,
          VVAS_BO_SYNC_BO_TO_DEVICE, qp_xrt_buf->size, 0);
      if (iret != 0) {
        GST_ERROR_OBJECT (enc, "synbo failed - %d, reason : %s", iret,
            strerror (errno));
        GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
            ("failed to sync QP buffer data to device. reason : %s",
                strerror (errno)));
        goto error;
      }
      GST_LOG_OBJECT (enc, "sent qpmap buffer idx %u of size %u to device",
          priv->cur_qp_idx, qp_xrt_buf->size);
    } else {
      payload_buf->qpbuf_index = 0xBAD;
    }

    /* send rate control data i.e. FSFA data if enabled */
    if (enc->rc_mode && lameta->rc_fsfa) {
      GstMapInfo fsfa_info = GST_MAP_INFO_INIT;
      gboolean bret = FALSE;
      xlnx_rc_fsfa_t *fsfa_ptr;
      guint fsfa_num;
      int i;

      bret = gst_buffer_map (lameta->rc_fsfa, &fsfa_info, GST_MAP_READ);
      if (!bret) {
        GST_ERROR_OBJECT (enc, "failed to map fsfa buffer");
        goto error;
      }

      fsfa_num = fsfa_info.size / sizeof (xlnx_rc_fsfa_t);

      if (fsfa_num != lameta->lookahead_depth) {
        GST_ERROR_OBJECT (enc, "lookahead depth %d not equal to numbfer "
            "of fsfa entries %d", lameta->lookahead_depth, fsfa_num);
        goto error;
      }

      if (fsfa_num < MIN_LOOKAHEAD_DEPTH || fsfa_num > MAX_LOOKAHEAD_DEPTH) {
        GST_ERROR_OBJECT (enc, "number of fsfa entries should be between %d to "
            "%d", MIN_LOOKAHEAD_DEPTH, MAX_LOOKAHEAD_DEPTH);
        goto error;
      }
      fsfa_ptr = (xlnx_rc_fsfa_t *) fsfa_info.data;

      /* Custom RC */
      for (i = 0; i < fsfa_num; i++) {
        payload_buf->frame_sad[i] = fsfa_ptr[i].fs;
        payload_buf->frame_activity[i] = fsfa_ptr[i].fa;
      }
      gst_buffer_unmap (lameta->rc_fsfa, &fsfa_info);

      payload_buf->la_depth = lameta->lookahead_depth;

      GST_LOG_OBJECT (enc, "sending fsfa data with LA depth %d",
          payload_buf->la_depth);
    } else {
      /* Default RC */
      memset (payload_buf->frame_sad, 0,
          MAX_LOOKAHEAD_DEPTH * sizeof (payload_buf->frame_sad[0]));
      memset (payload_buf->frame_activity, 0,
          MAX_LOOKAHEAD_DEPTH * sizeof (payload_buf->frame_activity[0]));
    }
  } else {
    GST_OBJECT_UNLOCK (enc);
    payload_buf->qpbuf_index = 0xBAD;
    /* Default RC */
    memset (payload_buf->frame_sad, 0,
        MAX_LOOKAHEAD_DEPTH * sizeof (payload_buf->frame_sad[0]));
    memset (payload_buf->frame_activity, 0,
        MAX_LOOKAHEAD_DEPTH * sizeof (payload_buf->frame_activity[0]));
  }

  for (i = 0; i < payload_buf->obuf_indexes_to_release_valid_cnt; i++) {
    gpointer read_oidx = g_list_first (priv->read_oidx_list)->data;
    payload_buf->obuf_indexes_to_release[i] = GPOINTER_TO_INT (read_oidx);
    priv->read_oidx_list = g_list_remove (priv->read_oidx_list, read_oidx);
    GST_DEBUG_OBJECT (enc, "sending back outbuf index to sk = %d",
        GPOINTER_TO_INT (read_oidx));
  }

  GST_LOG_OBJECT (enc, "sending input index %d : size = %lu, paddr = %p, "
      "la_depth = %d", cur_in_idx, gst_buffer_get_size (inbuf),
      (void *) gst_vvas_allocator_get_paddr (in_mem), payload_buf->la_depth);

  /* transfer payload settings to device */
  iret = vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (enc, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
        ("failed to sync VCU_PUSH command payload to device. reason : %s",
            strerror (errno)));
    goto error;
  }

  memset (payload_data, 0, 1024 * sizeof (int));
  payload_data[num_idx++] = 0;
  payload_data[num_idx++] = VCU_PUSH;
  payload_data[num_idx++] = getpid ();
  payload_data[num_idx++] = priv->timestamp & 0xFFFFFFFF;
  payload_data[num_idx++] = (priv->timestamp >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->sk_payload_buf->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->sk_payload_buf->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = sizeof (sk_payload_data);

  GST_LOG_OBJECT (enc, "sending VCU_PUSH command to softkernel");

  /* send command to softkernel */
  iret =
      vvas_xrt_send_softkernel_command (priv->xcl_dev_handle, priv->ert_cmd_buf,
      payload_data, num_idx, enc->sk_cur_idx, CMD_EXEC_TIMEOUT);
  if (iret < 0) {
    GST_ERROR_OBJECT (enc,
        "failed to send VCU_PUSH command to softkernel - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, FAILED, NULL,
        ("failed to issue VCU_PUSH command. reason : %s", strerror (errno)));
    goto error;
  } else {
    bret = vvas_xvcuenc_check_softkernel_response (enc, payload_buf);
    if (!bret) {
      GST_ERROR_OBJECT (enc, "softkernel send frame failed");
      GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, NULL,
          ("softkernel send frame failed. reason : %s", payload_buf->dev_err));
      goto error;
    }
  }

  GST_DEBUG_OBJECT (enc,
      "successfully completed VCU_PUSH command : input buffer index freed %d",
      payload_buf->freed_ibuf_index);

  if (payload_buf->freed_ibuf_index != 0xBAD) {
    mem_to_bufpool *bufpool;

    if (!g_hash_table_contains (priv->qbuf_hash,
            GINT_TO_POINTER (payload_buf->freed_ibuf_index))) {
      GST_ERROR_OBJECT (enc, "wrong index received %d as no hash found",
          payload_buf->freed_ibuf_index);
      goto error;
    }

    bufpool = g_hash_table_lookup (priv->qbuf_hash,
        GINT_TO_POINTER (payload_buf->freed_ibuf_index));
    if (g_queue_is_empty (bufpool->bufq)) {     //should not come here
      GST_ERROR_OBJECT (enc, "wrong index received %d",
          payload_buf->freed_ibuf_index);
      return FALSE;
    }

    GST_LOG_OBJECT (enc,
        "decrement memcount from %d to %d corresponding to index %d",
        bufpool->memcount, bufpool->memcount - 1,
        payload_buf->freed_ibuf_index);
    bufpool->memcount--;
    if (!bufpool->memcount) {
      gint j = g_queue_get_length (bufpool->bufq);
      GST_LOG_OBJECT (enc, "memcount is 0 corresponding to index %d",
          payload_buf->freed_ibuf_index);
      GST_LOG_OBJECT (enc, "freeing %d buffer corresponding to index %d",
          j, payload_buf->freed_ibuf_index);
      while (j) {
        GstBuffer *buf = g_queue_pop_head (bufpool->bufq);
        GST_LOG_OBJECT (enc, "free buffer %p corresponding to index %d",
            buf, payload_buf->freed_ibuf_index);
        if (buf != NULL)
          gst_buffer_unref (buf);
        j--;
      }
    }
  }

  if (priv->qpbuf_count) {
    if (priv->intial_qpbufs_consumed) {
      if (payload_buf->freed_qpbuf_index != 0xBAD) {
        priv->cur_qp_idx = payload_buf->freed_qpbuf_index;
        GST_DEBUG_OBJECT (enc, "received freed qpbuf index %u from device",
            priv->cur_qp_idx);
      } else {
        GST_ERROR_OBJECT (enc, "unexpected error...freed_qpbuf_index is wrong");
        GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, NULL,
            ("encoder softkernel sent wrong qp buffer index"));
      }
    } else {
      priv->cur_qp_idx++;
      if (priv->cur_qp_idx == (priv->qpbuf_count - 1))
        priv->intial_qpbufs_consumed = TRUE;
    }
  }

  if (in_mem)
    gst_memory_unref (in_mem);

  gst_buffer_unref (inbuf);

  return TRUE;

error:
  if (in_mem)
    gst_memory_unref (in_mem);

  gst_buffer_unref (inbuf);
  return FALSE;
}

static gboolean
vvas_xvcuenc_read_output_frame (GstVvasXVCUEnc * enc, GstBuffer * outbuf,
    gint oidx, gint outsize)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  xrt_buffer *out_xrt_buf = NULL;
  GstMapInfo map_info = GST_MAP_INFO_INIT;
  int iret = 0;

  out_xrt_buf = g_array_index (priv->out_xrt_bufs, xrt_buffer *, oidx);

  if (outsize > out_xrt_buf->size) {
    GST_ERROR_OBJECT (enc,
        "received out frame size %d greater than allocated xrt buffer size %d",
        outsize, out_xrt_buf->size);
    goto error;
  }

  iret = vvas_xrt_sync_bo (out_xrt_buf->bo,
      VVAS_BO_SYNC_BO_FROM_DEVICE, outsize, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (enc,
        "vvas_xrt_sync_bo failed for output buffer. error = %d", iret);
    GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
        ("failed to sync encoded data from device. reason : %s",
            strerror (errno)));
    goto error;
  }

  if (!gst_buffer_map (outbuf, &map_info, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (enc, "failed to map output buffer!");
    goto error;
  }
  //TODO: vvas_xrt_read_bo can be avoided if we allocate buffers using GStreamer pool
  iret = vvas_xrt_read_bo (out_xrt_buf->bo, map_info.data, outsize, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (enc, "failed to read output buffer. reason : %s",
        strerror (errno));
    goto error;
  }

  gst_buffer_unmap (outbuf, &map_info);
  return TRUE;

error:
  if (map_info.data)
    gst_buffer_unmap (outbuf, &map_info);
  return FALSE;
}

static GstFlowReturn
vvas_xvcuenc_receive_out_frame (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  GstVideoCodecFrame *frame = NULL;
  sk_payload_data *payload_buf;
  unsigned int payload_data[1024];
  unsigned int num_idx = 0;
  int iret = 0;
  gboolean bret = FALSE;
  gint oidx, outsize;
  GstFlowReturn fret = GST_FLOW_ERROR;

  if (priv->last_rcvd_payload.freed_index_cnt) {
    oidx =
        priv->last_rcvd_payload.obuf_info_data[priv->
        last_rcvd_oidx].obuff_index;
    if (oidx == 0xBAD) {
      GST_ERROR_OBJECT (enc, "received bad index from softkernel");
      fret = GST_FLOW_ERROR;
      goto error;
    }

    outsize =
        priv->last_rcvd_payload.obuf_info_data[priv->last_rcvd_oidx].recv_size;
    frame = gst_video_encoder_get_oldest_frame (GST_VIDEO_ENCODER (enc));

    fret = gst_video_encoder_allocate_output_frame (GST_VIDEO_ENCODER (enc),
        frame, outsize);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (enc, "Could not allocate output buffer");
      goto error;
    }

    GST_LOG_OBJECT (enc, "reading encoded output at index %d with size %d",
        oidx, outsize);

    bret =
        vvas_xvcuenc_read_output_frame (enc, frame->output_buffer, oidx,
        outsize);
    if (!bret) {
      fret = GST_FLOW_ERROR;
      goto error;
    }

    frame->pts =
        priv->last_rcvd_payload.obuf_info_data[priv->last_rcvd_oidx].
        obuf_meta.pts;
    if (priv->last_rcvd_payload.obuf_info_data[priv->last_rcvd_oidx].
        obuf_meta.frame_type == SLICE_I) {
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
      GST_DEBUG_OBJECT (enc, "Setting Sync point for PTS = %ld\n",
          GST_BUFFER_PTS (frame->output_buffer));
    }

    GST_LOG_OBJECT (enc, "pushing output buffer %p with pts %" GST_TIME_FORMAT,
        frame->output_buffer,
        GST_TIME_ARGS (GST_BUFFER_PTS (frame->output_buffer)));
    priv->read_oidx_list =
        g_list_append (priv->read_oidx_list, GINT_TO_POINTER (oidx));

    priv->last_rcvd_payload.freed_index_cnt--;
    priv->last_rcvd_oidx++;

    return gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (enc), frame);
  }

  /* update payload buf */
  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);
  memset (payload_buf, 0, priv->sk_payload_buf->size);

  payload_buf->cmd_id = VCU_RECEIVE;
  iret =
      vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (enc, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
        ("failed to sync VCU_RECEIVE command payload to device. reason : %s",
            strerror (errno)));
    fret = GST_FLOW_ERROR;
    goto error;
  }

  memset (payload_data, 0, 1024 * sizeof (int));
  payload_data[num_idx++] = 0;
  payload_data[num_idx++] = VCU_RECEIVE;
  payload_data[num_idx++] = getpid ();
  payload_data[num_idx++] = priv->timestamp & 0xFFFFFFFF;
  payload_data[num_idx++] = (priv->timestamp >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->sk_payload_buf->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->sk_payload_buf->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = sizeof (sk_payload_data);

try_again:

  GST_LOG_OBJECT (enc, "sending VCU_RECEIVE command to softkernel");

  /* send command to softkernel */
  iret =
      vvas_xrt_send_softkernel_command (priv->xcl_dev_handle, priv->ert_cmd_buf,
      payload_data, num_idx, enc->sk_cur_idx, CMD_EXEC_TIMEOUT);
  if (iret < 0) {
    GST_ERROR_OBJECT (enc,
        "failed to send VCU_RECEIVE command to softkernel - %d, reason : %s",
        iret, strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, FAILED, NULL,
        ("failed to issue VCU_RECEIVE command. reason : %s", strerror (errno)));
    fret = GST_FLOW_ERROR;
    goto error;
  } else {
    bret = vvas_xvcuenc_check_softkernel_response (enc, payload_buf);
    if (!bret) {
      GST_ERROR_OBJECT (enc, "softkernel receive frame failed");
      GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, NULL,
          ("softkernel receive frame failed. reason : %s",
              payload_buf->dev_err));
      fret = GST_FLOW_ERROR;
      goto error;
    }
  }
  GST_LOG_OBJECT (enc, "successfully completed VCU_RECEIVE command");

  GST_LOG_OBJECT (enc, "freed index count received from softkernel = %d",
      payload_buf->freed_index_cnt);

  g_mutex_lock (&priv->timeout_lock);
  if (payload_buf->freed_index_cnt == 0) {
    if (payload_buf->end_encoding) {
      GST_INFO_OBJECT (enc, "received EOS from softkernel");
      g_mutex_unlock (&priv->timeout_lock);
      return GST_FLOW_EOS;
    }
    GST_DEBUG_OBJECT (enc, "no encoded buffers to consume");
    if (enc->ultra_low_latency) {
      gint64 end_time = g_get_monotonic_time () + priv->retry_timeout;
      if (!g_cond_wait_until (&priv->timeout_cond, &priv->timeout_lock,
              end_time)) {
        GST_LOG_OBJECT (enc, "timeout occured, try receive command again");
      }
      g_mutex_unlock (&priv->timeout_lock);
      goto try_again;
    } else {
      g_mutex_unlock (&priv->timeout_lock);
      return GST_FLOW_OK;
    }
  }
  g_mutex_unlock (&priv->timeout_lock);

  memcpy (&priv->last_rcvd_payload, payload_buf, sizeof (sk_payload_data));

  priv->last_rcvd_oidx = 0;

  oidx =
      priv->last_rcvd_payload.obuf_info_data[priv->last_rcvd_oidx].obuff_index;
  if (oidx == 0xBAD) {
    GST_ERROR_OBJECT (enc, "received bad index from softkernel");
    fret = GST_FLOW_ERROR;
    goto error;
  }

  outsize =
      priv->last_rcvd_payload.obuf_info_data[priv->last_rcvd_oidx].recv_size;
  frame = gst_video_encoder_get_oldest_frame (GST_VIDEO_ENCODER (enc));

  fret = gst_video_encoder_allocate_output_frame (GST_VIDEO_ENCODER (enc),
      frame, outsize);
  if (fret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (enc, "Could not allocate output buffer");
    goto error;
  }

  GST_LOG_OBJECT (enc, "reading encoded output at index %d with size %d",
      oidx, outsize);

  bret = vvas_xvcuenc_read_output_frame (enc, frame->output_buffer, oidx,
      outsize);
  if (!bret) {
    fret = GST_FLOW_ERROR;
    goto error;
  }

  frame->pts =
      priv->last_rcvd_payload.obuf_info_data[priv->last_rcvd_oidx].
      obuf_meta.pts;
  if (priv->last_rcvd_payload.obuf_info_data[priv->last_rcvd_oidx].
      obuf_meta.frame_type == SLICE_I) {
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    GST_DEBUG_OBJECT (enc, "Setting Sync point for PTS = %ld\n",
        GST_BUFFER_PTS (frame->output_buffer));
  }

  priv->read_oidx_list =
      g_list_append (priv->read_oidx_list, GINT_TO_POINTER (oidx));

  GST_LOG_OBJECT (enc, "pushing output buffer %p with pts %" GST_TIME_FORMAT,
      frame->output_buffer,
      GST_TIME_ARGS (GST_BUFFER_PTS (frame->output_buffer)));

  priv->last_rcvd_payload.freed_index_cnt--;
  priv->last_rcvd_oidx++;

  fret = gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (enc), frame);
  if (fret != GST_FLOW_OK) {
    if (fret == GST_FLOW_EOS)
      GST_DEBUG_OBJECT (enc, "failed to push frame. reason %s",
          gst_flow_get_name (fret));
    else
      GST_ERROR_OBJECT (enc, "failed to push frame. reason %s",
          gst_flow_get_name (fret));

    frame = NULL;
    goto error;
  }

  return fret;

error:
  if (frame)
    gst_video_codec_frame_unref (frame);

  return fret;
}

static gboolean
vvas_xvcuenc_send_flush (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  sk_payload_data *payload_buf;
  unsigned int payload_data[1024];
  gboolean bret = FALSE;
  int iret = 0;
  unsigned int num_idx = 0;

  if (priv->flush_done) {
    GST_WARNING_OBJECT (enc,
        "flush already issued to softkernel, hence returning");
    return TRUE;
  }
  /* update payload buf */
  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);
  memset (payload_buf, 0, priv->sk_payload_buf->size);

  payload_buf->cmd_id = VCU_FLUSH;
  iret = vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (enc, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
        ("failed to sync VCU_FLUSH command payload to device. reason : %s",
            strerror (errno)));
    goto error;
  }
  memset (payload_data, 0, 1024 * sizeof (int));
  payload_data[num_idx++] = 0;
  payload_data[num_idx++] = VCU_FLUSH;
  payload_data[num_idx++] = getpid ();
  payload_data[num_idx++] = priv->timestamp & 0xFFFFFFFF;
  payload_data[num_idx++] = (priv->timestamp >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->sk_payload_buf->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->sk_payload_buf->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = sizeof (sk_payload_data);

  iret =
      vvas_xrt_send_softkernel_command (priv->xcl_dev_handle, priv->ert_cmd_buf,
      payload_data, num_idx, enc->sk_cur_idx, CMD_EXEC_TIMEOUT);
  if (iret < 0) {
    GST_ERROR_OBJECT (enc,
        "failed to send VCU_FLUSH command to softkernel - %d, reason : %s",
        iret, strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, FAILED, NULL,
        ("failed to issue VCU_FLUSH command. reason : %s", strerror (errno)));
    goto error;
  } else {
    bret = vvas_xvcuenc_check_softkernel_response (enc, payload_buf);
    if (!bret) {
      GST_ERROR_OBJECT (enc, "softkernel flush failed");
      GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, NULL,
          ("softkernel flush failed. reason : %s", payload_buf->dev_err));
      goto error;
    }
  }
  GST_DEBUG_OBJECT (enc, "successfully sent flush command");
  priv->flush_done = TRUE;
  return TRUE;

error:
  return FALSE;
}

static gboolean
vvas_xvcuenc_deinit (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = enc->priv;
  sk_payload_data *payload_buf;
  unsigned int payload_data[1024];
  unsigned int num_idx = 0;
  int iret = 0, i;

  if (priv->deinit_done) {
    GST_WARNING_OBJECT (enc,
        "deinit already issued to softkernel, hence returning");
    return TRUE;
  }

  /* update payload buf */
  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);
  memset (payload_buf, 0, priv->sk_payload_buf->size);

  payload_buf->cmd_id = VCU_DEINIT;
  payload_buf->obuf_indexes_to_release_valid_cnt =
      g_list_length (priv->read_oidx_list);

  GST_INFO_OBJECT (enc, "released buffers sending to deinit %d",
      payload_buf->obuf_indexes_to_release_valid_cnt);
  for (i = 0; i < payload_buf->obuf_indexes_to_release_valid_cnt; i++) {
    gpointer read_oidx = g_list_first (priv->read_oidx_list)->data;
    payload_buf->obuf_indexes_to_release[i] = GPOINTER_TO_INT (read_oidx);
    priv->read_oidx_list = g_list_remove (priv->read_oidx_list, read_oidx);
    GST_LOG_OBJECT (enc, "sending read output index %d to softkernel",
        GPOINTER_TO_INT (read_oidx));
  }

  iret = vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (enc, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, SYNC, NULL,
        ("failed to sync VCU_DEINIT command payload to device. reason : %s",
            strerror (errno)));
    goto error;
  }
  memset (payload_data, 0, 1024 * sizeof (int));
  payload_data[num_idx++] = 0;
  payload_data[num_idx++] = VCU_DEINIT;
  payload_data[num_idx++] = getpid ();
  payload_data[num_idx++] = priv->timestamp & 0xFFFFFFFF;
  payload_data[num_idx++] = (priv->timestamp >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->sk_payload_buf->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->sk_payload_buf->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = sizeof (sk_payload_data);

  priv->deinit_done = TRUE;     // irrespective of error

  iret =
      vvas_xrt_send_softkernel_command (priv->xcl_dev_handle, priv->ert_cmd_buf,
      payload_data, num_idx, enc->sk_cur_idx, CMD_EXEC_TIMEOUT);
  if (iret < 0) {
    GST_ERROR_OBJECT (enc,
        "failed to send VCU_DEINIT command to softkernel - %d, reason : %s",
        iret, strerror (errno));
    GST_ELEMENT_ERROR (enc, RESOURCE, FAILED, NULL,
        ("failed to issue VCU_DEINIT command. reason : %s", strerror (errno)));
    goto error;
  }
  GST_INFO_OBJECT (enc, "completed de-initialization");
  return TRUE;

error:
  return FALSE;
}

static gboolean
gst_vvas_xvcuenc_start (GstVideoEncoder * encoder)
{
  GstVvasXVCUEnc *enc = GST_VVAS_XVCUENC (encoder);
  GstVvasXVCUEncPrivate *priv = GST_VVAS_XVCUENC_PRIVATE (enc);

  vvas_xvcuenc_reset (enc);

  priv->in_idx_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
  priv->qbuf_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
  priv->out_xrt_bufs = g_array_new (FALSE, TRUE, sizeof (xrt_buffer *));
  priv->qp_xrt_bufs = g_array_new (FALSE, TRUE, sizeof (xrt_buffer *));
#ifdef ENABLE_XRM_SUPPORT

  enc->priv->xrm_ctx = (xrmContext *) xrmCreateContext (XRM_API_VERSION_1);
  if (!enc->priv->xrm_ctx) {
    GST_ERROR_OBJECT (enc, "create XRM context failed");
    return FALSE;
  }

  GST_INFO_OBJECT (enc, "successfully created xrm context");
#endif
  enc->priv->has_error = FALSE;

  if (enc->enabled_pipeline) {
    priv->copy_inqueue = g_async_queue_new_full (vvas_xenc_copy_object_unref);
    priv->copy_outqueue = g_async_queue_new_full (vvas_xenc_copy_object_unref);

    priv->is_first_frame = TRUE;

    priv->input_copy_thread = g_thread_new ("enc-input-copy-thread",
        vvas_xvcuenc_input_copy_thread, enc);
  }

  return TRUE;
}

static gboolean
vvas_remove_qpool (gpointer key, gpointer value, gpointer user_data)
{
  mem_to_bufpool *bufpool = (mem_to_bufpool *) value;
  GstVvasXVCUEnc *enc = user_data;
  gint j = 0;

  j = g_queue_get_length (bufpool->bufq);
  GST_LOG_OBJECT (enc, "freeing %d buffer corresponding to index %d",
      j, GPOINTER_TO_INT (key));
  while (j) {
    GstBuffer *buf = g_queue_pop_head (bufpool->bufq);
    GST_LOG_OBJECT (enc, "free buffer %p corresponding to index %d",
        buf, GPOINTER_TO_INT (key));
    if (buf != NULL) {
      gst_buffer_unref (buf);
    }
    j--;
  }
  g_queue_free (bufpool->bufq);
  g_free (bufpool);
  return TRUE;
}

static gboolean
gst_vvas_xvcuenc_stop (GstVideoEncoder * encoder)
{
  gboolean bret = TRUE;
  GstVvasXVCUEnc *enc = GST_VVAS_XVCUENC (encoder);
  GstVvasXVCUEncPrivate *priv = enc->priv;

  GST_DEBUG_OBJECT (GST_VVAS_XVCUENC (encoder), "stop");

  if (priv->init_done) {
    bret = vvas_xvcuenc_send_flush (GST_VVAS_XVCUENC (encoder));
    if (!bret)
      return bret;

    bret = vvas_xvcuenc_deinit (GST_VVAS_XVCUENC (encoder));
    priv->init_done = FALSE;
  }

  if (priv->input_pool) {
    if (gst_buffer_pool_is_active (priv->input_pool))
      gst_buffer_pool_set_active (priv->input_pool, FALSE);
    gst_clear_object (&priv->input_pool);
  }

  /* free all output buffers allocated */
  vvas_xvcuenc_free_output_buffers (enc);

  vvas_xvcuenc_free_qp_buffers (enc);

  /* free all internal buffers */
  vvas_xvcuenc_free_internal_buffers (enc);

  vvas_xvcuenc_destroy_context (enc);

#ifdef ENABLE_XRM_SUPPORT
  if (xrmDestroyContext (priv->xrm_ctx) != XRM_SUCCESS)
    GST_ERROR_OBJECT (enc, "failed to destroy XRM context");
#endif

  // TODO: array elements need to be freed before this
  g_hash_table_unref (enc->priv->in_idx_hash);
  g_hash_table_foreach_remove (enc->priv->qbuf_hash, vvas_remove_qpool, enc);
  g_hash_table_unref (enc->priv->qbuf_hash);
  g_array_free (enc->priv->out_xrt_bufs, TRUE);
  g_array_free (enc->priv->qp_xrt_bufs, TRUE);

  if (enc->input_state) {
    gst_video_codec_state_unref (enc->input_state);
    enc->input_state = NULL;
  }

  if (enc->profile) {
    g_free (enc->profile);
    enc->profile = NULL;
  }
  if (enc->level) {
    g_free (enc->level);
    enc->level = NULL;
  }
  if (enc->tier) {
    g_free (enc->tier);
    enc->tier = NULL;
  }

  if (enc->enabled_pipeline) {
    if (priv->input_copy_thread) {
      g_async_queue_push (priv->copy_inqueue, STOP_COMMAND);
      GST_LOG_OBJECT (enc, "waiting for copy input thread join");
      g_thread_join (priv->input_copy_thread);
      priv->input_copy_thread = NULL;
    }

    if (priv->copy_inqueue) {
      g_async_queue_unref (priv->copy_inqueue);
      priv->copy_inqueue = NULL;
    }

    if (priv->copy_outqueue) {
      g_async_queue_unref (priv->copy_outqueue);
      priv->copy_outqueue = NULL;
    }
  }

  return bret;
}

static void
gst_video_enc_set_color_primaries (GstVvasXVCUEnc * enc,
    GstVideoColorPrimaries primaries)
{
  switch (primaries) {
    case GST_VIDEO_COLOR_PRIMARIES_BT709:
      strcpy (enc->color_description, "COLOUR_DESC_BT_709");
      break;
    case GST_VIDEO_COLOR_PRIMARIES_BT470M:
      strcpy (enc->color_description, "COLOUR_DESC_BT_470_NTSC");
      break;
    case GST_VIDEO_COLOR_PRIMARIES_BT470BG:
      strcpy (enc->color_description, "COLOUR_DESC_UNSPECIFIED");
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTE170M:
      strcpy (enc->color_description, "COLOUR_DESC_UNSPECIFIED");
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTE240M:
      strcpy (enc->color_description, "COLOUR_DESC_SMPTE_240M");
      break;
    case GST_VIDEO_COLOR_PRIMARIES_FILM:
      strcpy (enc->color_description, "COLOUR_DESC_GENERIC_FILM");
      break;
    case GST_VIDEO_COLOR_PRIMARIES_BT2020:
      strcpy (enc->color_description, "COLOUR_DESC_BT_2020");
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTEST428:
      strcpy (enc->color_description, "COLOUR_DESC_SMPTE_ST_428");
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTERP431:
      strcpy (enc->color_description, "COLOUR_DESC_SMPTE_RP_431");
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTEEG432:
      strcpy (enc->color_description, "COLOUR_DESC_SMPTE_EG_432");
      break;
    case GST_VIDEO_COLOR_PRIMARIES_EBU3213:
      strcpy (enc->color_description, "COLOUR_DESC_EBU_3213");
      break;
    case GST_VIDEO_COLOR_PRIMARIES_ADOBERGB:
    case GST_VIDEO_COLOR_PRIMARIES_UNKNOWN:
    default:
      strcpy (enc->color_description, "COLOUR_DESC_UNSPECIFIED");
  }

  return;
}

static void
gst_video_enc_set_transfer_characteristics (GstVvasXVCUEnc * enc,
    GstVideoTransferFunction transfer)
{
  switch (transfer) {
    case GST_VIDEO_TRANSFER_SMPTE2084:
      strcpy (enc->transfer_characteristics, "TRANSFER_BT_2100_PQ");
      break;
    case GST_VIDEO_TRANSFER_ARIB_STD_B67:
      strcpy (enc->transfer_characteristics, "TRANSFER_BT_2100_HLG");
      break;
    case GST_VIDEO_TRANSFER_BT2020_10:
    case GST_VIDEO_TRANSFER_UNKNOWN:
    case GST_VIDEO_TRANSFER_GAMMA10:
    case GST_VIDEO_TRANSFER_GAMMA18:
    case GST_VIDEO_TRANSFER_GAMMA20:
    case GST_VIDEO_TRANSFER_GAMMA22:
    case GST_VIDEO_TRANSFER_BT709:
    case GST_VIDEO_TRANSFER_SMPTE240M:
    case GST_VIDEO_TRANSFER_SRGB:
    case GST_VIDEO_TRANSFER_GAMMA28:
    case GST_VIDEO_TRANSFER_LOG100:
    case GST_VIDEO_TRANSFER_LOG316:
    case GST_VIDEO_TRANSFER_BT2020_12:
    case GST_VIDEO_TRANSFER_ADOBERGB:
    default:
      strcpy (enc->transfer_characteristics, "TRANSFER_UNSPECIFIED");
  }

  return;
}

static void
gst_video_enc_set_color_matrix (GstVvasXVCUEnc * enc,
    GstVideoColorMatrix matrix)
{
  switch (matrix) {
    case GST_VIDEO_COLOR_MATRIX_BT2020:
      strcpy (enc->color_matrix, "COLOUR_MAT_BT_2100_YCBCR");
      break;
    case GST_VIDEO_COLOR_MATRIX_UNKNOWN:
    case GST_VIDEO_COLOR_MATRIX_RGB:
    case GST_VIDEO_COLOR_MATRIX_FCC:
    case GST_VIDEO_COLOR_MATRIX_BT709:
    case GST_VIDEO_COLOR_MATRIX_BT601:
    case GST_VIDEO_COLOR_MATRIX_SMPTE240M:
    default:
      strcpy (enc->color_matrix, "COLOUR_MAT_UNSPECIFIED");
  }

  return;
}

static gboolean
gst_vvas_xvcuenc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstVvasXVCUEnc *enc = GST_VVAS_XVCUENC (encoder);
  GstVvasXVCUEncPrivate *priv = enc->priv;
  gboolean bret = TRUE;
  GstVideoCodecState *output_state;
  GstCaps *outcaps = NULL;
  GstCaps *allowed_caps = NULL;
  GstCaps *peercaps = NULL;
  gchar *caps_str = NULL;
  gboolean do_reconfigure = FALSE;
  GstVideoInfo in_vinfo;
  GstVideoColorimetry cinfo;
  GstStructure *structure, *qstruct;
  GstQuery *la_query;
  const gchar *cinfo_string = NULL;
  gchar *mime_type = NULL;
  int i;

  GST_DEBUG_OBJECT (enc, "input caps: %" GST_PTR_FORMAT, state->caps);

  if (priv->has_error)
    return FALSE;

  if (!enc->input_state ||
      !gst_caps_is_equal (enc->input_state->caps, state->caps))
    do_reconfigure = TRUE;

  // TODO: add support for reconfiguration .e.g. deinit the encoder
  if (enc->input_state) {
    gst_video_codec_state_unref (enc->input_state);
    enc->input_state = NULL;
  }
  enc->input_state = gst_video_codec_state_ref (state);

  structure = gst_caps_get_structure (enc->input_state->caps, 0);

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));

  for (i = 0; i < gst_caps_get_size (allowed_caps); i++) {
    GstStructure *structure = gst_caps_get_structure (allowed_caps, i);
    gchar *cur_mime_type = (gchar *) gst_structure_get_name (structure);

    if (!mime_type)
      mime_type = cur_mime_type;

    if (g_strcmp0 (cur_mime_type, mime_type)) {
      GST_ELEMENT_ERROR (enc, STREAM, FORMAT, NULL,
          ("different mime types detected in source pad's allowed caps. "
              "unable to select encoder type"));
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (enc, "allowed caps: %" GST_PTR_FORMAT, allowed_caps);

  caps_str = gst_caps_to_string (allowed_caps);
  gst_caps_unref (allowed_caps);

  gst_video_info_init (&in_vinfo);
  bret = gst_video_info_from_caps (&in_vinfo, enc->input_state->caps);
  if (!bret) {
    GST_ERROR_OBJECT (enc, "failed to get video info from input caps");
    return FALSE;
  }

  cinfo = GST_VIDEO_INFO_COLORIMETRY (&in_vinfo);

  if ((cinfo_string = gst_structure_get_string (structure, "colorimetry"))) {
    if (!gst_video_colorimetry_from_string (&cinfo, cinfo_string)) {
      GST_WARNING_OBJECT (enc, "Could not parse colorimetry %s", cinfo_string);
    }
    /* In case if one of the parameters is zero, base class will override with default
     * values. To avoid this, we are passing on the same incoming colorimetry
     * information to downstream as well. Refer https://jira.xilinx.com/browse/CR-1114507
     * for more information 
     */
    state->info.colorimetry.range = cinfo.range;
    state->info.colorimetry.matrix = cinfo.matrix;
    state->info.colorimetry.transfer = cinfo.transfer;
    state->info.colorimetry.primaries = cinfo.primaries;

    gst_video_enc_set_color_primaries (enc, cinfo.primaries);
    gst_video_enc_set_transfer_characteristics (enc, cinfo.transfer);
    gst_video_enc_set_color_matrix (enc, cinfo.matrix);
  }
  enc->bit_depth =
      GST_VIDEO_INFO_FORMAT (&in_vinfo) ==
      GST_VIDEO_FORMAT_NV12_10LE32 ? 10 : 8;

  if (g_str_has_prefix (caps_str, "video/x-h264")) {
    enc->codec_type = VVAS_CODEC_H264;
    enc->stride_align = H264_STRIDE_ALIGN;
  } else if (g_str_has_prefix (caps_str, "video/x-h265")) {
    enc->codec_type = VVAS_CODEC_H265;
    enc->stride_align = H265_STRIDE_ALIGN;
  }
  enc->height_align = HEIGHT_ALIGN;
  g_free (caps_str);

  GST_INFO_OBJECT (enc, "encoder type selected = %s",
      enc->codec_type == VVAS_CODEC_H264 ? "h264" : "h265");

  outcaps = gst_caps_copy (gst_static_pad_template_get_caps (&src_template));

  // TODO: add case for 10-bit as well once encoder support 10-bit formats
  if (enc->bit_depth == 10)
    gst_caps_set_simple (outcaps, "chroma-format", G_TYPE_STRING, "4:2:0",
        "bit-depth-luma", G_TYPE_UINT, 10, "bit-depth-chroma", G_TYPE_UINT, 10,
        NULL);
  else
    gst_caps_set_simple (outcaps, "chroma-format", G_TYPE_STRING, "4:2:0",
        "bit-depth-luma", G_TYPE_UINT, 8, "bit-depth-chroma", G_TYPE_UINT, 8,
        NULL);

  GST_DEBUG_OBJECT (enc, "output caps: %" GST_PTR_FORMAT, outcaps);

  /* Set profile and level */
  peercaps = gst_pad_peer_query_caps (GST_VIDEO_ENCODER_SRC_PAD (enc),
      gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (enc)));

  if (peercaps && !gst_caps_is_empty (peercaps)) {
    GstStructure *s;

    s = gst_caps_get_structure (peercaps, 0);
    enc->profile = g_strdup (gst_structure_get_string (s, "profile"));
    enc->level = g_strdup (gst_structure_get_string (s, "level"));
    GST_INFO_OBJECT (enc,
        "profile = %s and level = %s received from downstream", enc->profile,
        enc->level);

    if (enc->codec_type == VVAS_CODEC_H265) {
      enc->tier = g_strdup (gst_structure_get_string (s, "tier"));
      GST_INFO_OBJECT (enc, "Tier %s received from downstream", enc->tier);
    }
  }

  if (peercaps)
    gst_caps_unref (peercaps);

  if (enc->codec_type == VVAS_CODEC_H264) {
    if (g_strcmp0 (enc->profile, "") == -1) {
      g_free (enc->profile);
      GST_INFO_OBJECT (enc, "picking H264 default profile high");
      enc->profile = g_strdup ("high");
    } else if (g_str_has_prefix (enc->profile, "HEVC")) {
      GST_ERROR_OBJECT (enc, "Codec Type and Profile donot match");
      return TRUE;
    }
    gst_caps_remove_structure (outcaps, 1);
  } else if (enc->codec_type == VVAS_CODEC_H265) {
    if (g_strcmp0 (enc->profile, "") == -1) {
      g_free (enc->profile);
      GST_INFO_OBJECT (enc, "picking H265 default profile main");
      enc->profile = g_strdup ("main");
    } else if (g_str_has_prefix (enc->profile, "AVC")) {
      GST_ERROR_OBJECT (enc, "Codec Type and Profile donot match");
      return FALSE;
    }
    gst_caps_remove_structure (outcaps, 0);
  } else {
    GST_ERROR_OBJECT (enc, "Encoder Type not specified");
    return FALSE;
  }

  if (enc->ultra_low_latency) {
    if (enc->b_frames == UNSET_NUM_B_FRAMES) {
      enc->b_frames = 0;
      GST_DEBUG_OBJECT (enc,
          "ultra-low-latency mode enabled, seting b-frames to 0");
    } else if (enc->b_frames != 0) {
      GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
          ("Cannot encode b-frames in ultra-low-latency mode"));
      return FALSE;
    }
  }
  enc->b_frames =
      enc->b_frames ==
      UNSET_NUM_B_FRAMES ? GST_VVAS_VIDEO_ENC_B_FRAMES_DEFAULT : enc->b_frames;

  switch (enc->gop_mode) {
    case DEFAULT_GOP:
      if (enc->b_frames < 0 || enc->b_frames > 4) {
        GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
            ("In default gop-mode, b-frames allowed is between 0 and 4"));
        return FALSE;
      }
      break;
    case PYRAMIDAL_GOP:
      if (!(enc->b_frames == 3 || enc->b_frames == 5 || enc->b_frames == 7 ||
              enc->b_frames == 15)) {
        GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
            ("In pyramidal gop-mode, b-frames allowed is 3/5/7/15"));
        return FALSE;
      }
  }

  if ((enc->gdr_mode == GDR_VERTICAL || enc->gdr_mode == GDR_HORIZONTAL) &&
      (enc->gop_mode == DEFAULT_GOP || enc->gop_mode == PYRAMIDAL_GOP)) {
    GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
        ("gdr-mode can't be used when gop-mode is not LOW_DELAY_P or LOW_DELAY_B"));
    return FALSE;
  }

  if (enc->control_rate == RC_LOW_LATENCY && enc->b_frames) {
    GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
        ("b-frames should zero when control-rate is set to low-latency"));
    return FALSE;
  }

  if (enc->codec_type == VVAS_CODEC_H264) {
    if (!enc->ultra_low_latency && enc->avc_lowlat) {
      GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
          ("avc-lowlat flag is not needed when ulta-low-latency mode is disabled"));
      return FALSE;
    } else if (enc->ultra_low_latency && !enc->avc_lowlat) {
      if (enc->num_cores > 1) {
        GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
            ("Set avc-lowlat to allow for multiple cores with ultra-low-latency"));
        return FALSE;
      }
      if ((GST_VIDEO_INFO_WIDTH (&in_vinfo) > 1920)
          || (GST_VIDEO_INFO_HEIGHT (&in_vinfo) > 1920)) {
        GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
            ("Set avc-lowlat to disable pipelining for higher resolutions"));
        return FALSE;
      }
    }
  }

  if (do_reconfigure) {
#ifdef ENABLE_XRM_SUPPORT
    gint load = -1;
#endif

    /*Deinitialise and intialise again as caps changed */
    if (priv->init_done) {
      bret = vvas_xvcuenc_send_flush (enc);
      priv->flush_done = FALSE;

      bret = vvas_xvcuenc_deinit (enc);
      priv->init_done = FALSE;
      priv->deinit_done = FALSE;

      vvas_xrt_free_xrt_buffer (priv->static_cfg_buf);
      free (priv->static_cfg_buf);
      priv->static_cfg_buf = NULL;

      vvas_xvcuenc_free_output_buffers (enc);
      vvas_xvcuenc_free_qp_buffers (enc);
    }
#ifdef ENABLE_XRM_SUPPORT
    bret = vvas_xvcuenc_calculate_load (enc, &load);
    if (!bret) {
      priv->has_error = TRUE;
      return FALSE;
    }

    if (priv->cur_load != load) {

      priv->cur_load = load;

      /* destroy XRT context as new load received */
      bret = vvas_xvcuenc_destroy_context (enc);
      if (!bret) {
        priv->has_error = TRUE;
        return FALSE;
      }

      /* create XRT context */
      priv->xcl_ctx_valid = vvas_xvcuenc_create_context (enc);
      if (!priv->xcl_ctx_valid) {
        priv->has_error = TRUE;
        return FALSE;
      }

      /* free resources as device idx might change */
      vvas_xvcuenc_free_internal_buffers (enc);
    }
#else
    if (!priv->dev_handle) {
      /* create XRT context */
      bret = vvas_xvcuenc_create_context (enc);
      if (!bret) {
        priv->has_error = TRUE;
        return FALSE;
      }
    }
#endif
  }

  bret = vvas_xvcuenc_allocate_internal_buffers (enc);
  if (bret == FALSE) {
    GST_ERROR_OBJECT (enc, "failed to allocate internal buffers");
    return FALSE;
  }

  /* query lookahead parameters */
  qstruct = gst_structure_new (VVAS_LOOKAHEAD_QUERY_NAME,
      "b-frames", G_TYPE_INT, 0, "la-depth", G_TYPE_UINT, 0, NULL);
  la_query = gst_query_new_custom (GST_QUERY_CUSTOM, qstruct);
  bret = gst_pad_query (encoder->srcpad, la_query);
  if (bret) {
    const GstStructure *mod_qstruct = gst_query_get_structure (la_query);
    guint la_depth = 0;
    gint b_frames = 0;

    bret = gst_structure_get_uint (mod_qstruct, "la-depth", &la_depth);
    if (!bret) {
      GST_ERROR_OBJECT (enc, "failed to get la-depth from query");
      gst_query_unref (la_query);
      return FALSE;
    }

    GST_INFO_OBJECT (enc, "received lookahead-depth %u"
        " from upstream", la_depth);

    if (!la_depth || enc->control_rate != RC_CBR) {
      GST_INFO_OBJECT (enc, "disabling custom rate control");
      enc->rc_mode = FALSE;
    }

    if (la_depth > enc->gop_length) {
      GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
          ("lookahead-depth %u received from upstream query can't be greater "
              "than encoder's gop-length %u", la_depth, enc->gop_length));
      return FALSE;
    }

    if (la_depth > 0 && !enc->min_qp) {
      GST_INFO_OBJECT (enc, "updating min-qp to %u for better performance, "
          "when lookahead depth > 0", VVAS_XVCUENC_LOOKAHEAD_MIN_QP);
      enc->min_qp = 20;
    }

    if (la_depth > 0 && enc->tune_metrics == FALSE) {
      GST_INFO_OBJECT (enc,
          "setting qp_mode to RELATIVE_LOAD as lookahead > 0 and tune-metrics not enabled");
      enc->qp_mode = RELATIVE_LOAD;
    }

    bret = gst_structure_get_int (mod_qstruct, "b-frames", &b_frames);
    if (!bret) {
      GST_ERROR_OBJECT (enc, "failed to get b-frames from query");
      gst_query_unref (la_query);
      return FALSE;
    }

    GST_INFO_OBJECT (enc, "received b-frames %d" " from upstream", b_frames);

    if (enc->b_frames != b_frames) {
      GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
          ("lookahead (%d) and encoder (%d) are configured with "
              "different b-frames", b_frames, enc->b_frames));
      gst_query_unref (la_query);
      return FALSE;
    }
  } else {
    GST_INFO_OBJECT (enc, "failed to query lookahead depth, "
        "so setting rc_mode to FALSE");
    enc->rc_mode = FALSE;
  }
  gst_query_unref (la_query);

  if (enc->tune_metrics == TRUE) {
    GST_INFO_OBJECT (enc,
        "setting qp_mode to UNIFORM_QP and scaling_list to FLAT as tune-metrics enabled");
    enc->qp_mode = UNIFORM_QP;
    enc->scaling_list = SCALING_LIST_FLAT;
  }
  if (!priv->init_done) {
    bret = vvas_xvcuenc_preinit (enc);
    if (!bret)
      return FALSE;

    bret = vvas_xvcuenc_init (enc);
    if (!bret)
      return FALSE;

    enc->priv->init_done = TRUE;
    priv->intial_qpbufs_consumed = FALSE;
    memset (&enc->priv->last_rcvd_payload, 0x00, sizeof (sk_payload_data));
    enc->priv->last_rcvd_oidx = 0;
  }

  GST_DEBUG_OBJECT (enc, "output caps modified : %" GST_PTR_FORMAT, outcaps);

  output_state = gst_video_encoder_set_output_state (encoder, outcaps, state);
  gst_video_codec_state_unref (output_state);

  return TRUE;
}

static GstStructure *
get_allocation_video_meta (GstVvasXVCUEnc * enc, GstVideoInfo * info)
{
  GstStructure *result;
  GstVideoAlignment *alig;
  GstVideoInfo new_info;

  /* Create a copy of @info without any offset/stride as we need a
   * 'standard' version to compute the paddings. */
  gst_video_info_init (&new_info);
  gst_video_info_set_interlaced_format (&new_info,
      GST_VIDEO_INFO_FORMAT (info),
      GST_VIDEO_INFO_INTERLACE_MODE (info),
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));

  set_align_param (enc, info, &enc->priv->in_align);
  alig = &enc->priv->in_align;

  result = gst_structure_new_empty ("video-meta");

  gst_structure_set (result, "padding-top", G_TYPE_UINT, alig->padding_top,
      "padding-bottom", G_TYPE_UINT, alig->padding_bottom,
      "padding-left", G_TYPE_UINT, alig->padding_left,
      "padding-right", G_TYPE_UINT, alig->padding_right, NULL);

  /* Encoder doesn't support splitting planes on multiple buffers */
  gst_structure_set (result, "single-allocation", G_TYPE_BOOLEAN, TRUE, NULL);

  GST_LOG_OBJECT (enc, "Request buffer layout to producer: %" GST_PTR_FORMAT,
      result);

  return result;
}

static gboolean
gst_vvas_xvcuenc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query)
{
  GstVvasXVCUEnc *enc = GST_VVAS_XVCUENC (encoder);
  GstStructure *meta_param;
  GstAllocationParams alloc_params;
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;

  gst_query_parse_allocation (query, &caps, NULL);

  if (!caps) {
    GST_WARNING_OBJECT (enc, "allocation query does not contain caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (enc, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  meta_param = get_allocation_video_meta (enc, &info);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, meta_param);
  gst_structure_free (meta_param);

  pool = gst_vvas_buffer_pool_new (enc->stride_align, enc->height_align);

#ifdef ENABLE_XRM_SUPPORT
  if (!enc->priv->dev_handle) {
    GST_ERROR_OBJECT (enc, "xrt handle not created");
    return FALSE;
  }
#endif

  allocator = gst_vvas_allocator_new (enc->dev_index,
      ENABLE_DMABUF, enc->in_mem_bank, enc->priv->kern_handle);

  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
  alloc_params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  gst_buffer_pool_config_set_video_alignment (config, &enc->priv->in_align);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_set_params (config, caps, enc->priv->in_buf_size,
      enc->priv->min_num_inbufs, 0);
  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_INFO_OBJECT (enc, "Failed to set config on input pool");
    goto error;
  }
  gst_query_add_allocation_pool (query, pool, enc->priv->in_buf_size,
      enc->priv->min_num_inbufs, 0);
  gst_query_add_allocation_param (query, allocator, &alloc_params);

  GST_DEBUG_OBJECT (enc, "query updated %" GST_PTR_FORMAT, query);
  gst_object_unref (allocator);
  gst_object_unref (pool);
  return TRUE;

error:
  if (pool)
    gst_object_unref (pool);
  return FALSE;
}

static gboolean
gst_vvas_xvcuenc_sink_query (GstVideoEncoder * encoder, GstQuery * query)
{
  GstVvasXVCUEnc *enc = GST_VVAS_XVCUENC (encoder);
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:{
      GstStructure *qstruct = NULL;

      GST_DEBUG_OBJECT (enc, "received CUSTOM query");
      if (enc->control_rate != RC_CBR) {
        GST_INFO_OBJECT (enc, "disabling custom rate control");
        enc->rc_mode = FALSE;
      }

      qstruct = gst_query_writable_structure (query);
      if (qstruct && !g_strcmp0 (gst_structure_get_name (qstruct),
              VVAS_ENCODER_QUERY_NAME)) {
        gst_structure_set (qstruct,
            "gop-length", G_TYPE_UINT, enc->gop_length,
            "ultra-low-latency", G_TYPE_BOOLEAN, enc->ultra_low_latency,
            "rc-mode", G_TYPE_BOOLEAN, enc->rc_mode, NULL);
        GST_INFO_OBJECT (enc,
            "updating gop-length %u rc-mode %u in query",
            enc->gop_length, enc->rc_mode);
        return TRUE;
      }
    }
    default:
      break;
  }

  return GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (encoder, query);
}

static void
gst_vvas_xvcuenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvasXVCUEnc *enc = GST_VVAS_XVCUENC (object);
  GstVvasXVCUEncPrivate *priv = enc->priv;

  switch (prop_id) {
    case PROP_XCLBIN_LOCATION:
      if (enc->xclbin_path)
        g_free (enc->xclbin_path);
      enc->xclbin_path = g_value_dup_string (value);
      break;
#ifndef ENABLE_XRM_SUPPORT
    case PROP_SK_CURRENT_INDEX:
      enc->sk_cur_idx = g_value_get_int (value);
      /* Encode sk cu index are starting from 32 in xclbin while user will
       * provide sk_ cu index from 0. So we are translating to correct cu
       * index here
       */
      enc->sk_cur_idx = enc->sk_cur_idx + ENC_SK_CU_START_OFFSET;
      break;
#endif
    case PROP_DEVICE_INDEX:
      enc->dev_index = g_value_get_int (value);
      break;
    case PROP_ASPECT_RATIO:
      enc->aspect_ratio = g_value_get_enum (value);
      break;
    case PROP_B_FRAMES:
      GST_OBJECT_LOCK (enc);
      if (GST_STATE (enc) == GST_STATE_NULL
          || GST_STATE (enc) == GST_STATE_READY) {
        enc->b_frames = g_value_get_int (value);
      } else if (GST_STATE (enc) > GST_STATE_READY
          && enc->gop_mode == DEFAULT_GOP) {
        if (enc->ultra_low_latency) {
          GST_WARNING_OBJECT (enc, "ultra-low-latency mode is enabled."
              "Cannot encode b-frames");
        } else {
          priv->is_bframes_changed = TRUE;
          priv->is_dyn_params_valid = TRUE;
          enc->b_frames = g_value_get_int (value);
        }
      } else if (GST_STATE (enc) > GST_STATE_READY) {
        GST_WARNING_OBJECT (enc,
            "Dynamic configuration of b-frames is supported"
            "only in DEFAULT_GOP mode");
      }
      GST_OBJECT_UNLOCK (enc);
      break;
    case PROP_CONSTRAINED_INTRA_PREDICTION:
      enc->constrained_intra_prediction = g_value_get_boolean (value);
      break;
    case PROP_CONTROL_RATE:
      enc->control_rate = g_value_get_enum (value);
      break;
    case PROP_CPB_SIZE:
      enc->cpb_size = g_value_get_uint (value);
      break;
      /*only for H.264 */
    case PROP_ENTROPY_MODE:
      enc->entropy_mode = g_value_get_enum (value);
      break;
    case PROP_FILLER_DATA:
      enc->filler_data = g_value_get_boolean (value);
      break;
    case PROP_GDR_MODE:
      enc->gdr_mode = g_value_get_enum (value);
      break;
    case PROP_GOP_LENGTH:
      enc->gop_length = g_value_get_uint (value);
      break;
    case PROP_GOP_MODE:
      enc->gop_mode = g_value_get_enum (value);
      break;
    case PROP_INITIAL_DELAY:
      enc->initial_delay = g_value_get_uint (value);
      break;
    case PROP_LOOP_FILTER_MODE:
      enc->loop_filter_mode = g_value_get_enum (value);
      break;
    case PROP_MAX_BITRATE:
      enc->max_bitrate = g_value_get_uint (value);
      break;
    case PROP_MAX_QP:
      enc->max_qp = g_value_get_uint (value);
      break;
    case PROP_MIN_QP:
      enc->min_qp = g_value_get_uint (value);
      break;
    case PROP_NUM_SLICES:
      enc->num_slices = g_value_get_uint (value);
      break;
    case PROP_IDR_PERIODICITY:
      enc->periodicity_idr = g_value_get_uint (value);
      break;
    case PROP_PREFETCH_BUFFER:
      enc->prefetch_buffer = g_value_get_boolean (value);
      break;
    case PROP_QP_MODE:
      enc->qp_mode = g_value_get_enum (value);
      break;
    case PROP_SCALING_LIST:
      enc->scaling_list = g_value_get_enum (value);
      break;
    case PROP_SLICE_QP:
      enc->slice_qp = g_value_get_int (value);
      break;
    case PROP_SLICE_SIZE:
      enc->slice_size = g_value_get_uint (value);
      break;
    case PROP_TARGET_BITRATE:
      GST_OBJECT_LOCK (enc);
      enc->target_bitrate = g_value_get_uint (value);
      priv->is_bitrate_changed = TRUE;
      priv->is_dyn_params_valid = TRUE;
      GST_OBJECT_UNLOCK (enc);
      break;
    case PROP_NUM_CORES:
      enc->num_cores = g_value_get_uint (value);
      break;
    case PROP_RATE_CONTROL_MODE:
      enc->rc_mode = g_value_get_boolean (value);
      break;
    case PROP_KERNEL_NAME:
      if (enc->kernel_name)
        g_free (enc->kernel_name);

      enc->kernel_name = g_value_dup_string (value);
      break;
    case PROP_DEPENDENT_SLICE:
      enc->dependent_slice = g_value_get_boolean (value);
      break;
    case PROP_IP_DELTA:
      enc->ip_delta = g_value_get_int (value);
      break;
    case PROP_PB_DELTA:
      enc->pb_delta = g_value_get_int (value);
      break;
    case PROP_LOOP_FILTER_BETA_OFFSET:
      enc->loop_filter_beta_offset = g_value_get_int (value);
      break;
    case PROP_LOOK_FILTER_TC_OFFSET:
      enc->loop_filter_tc_offset = g_value_get_int (value);
      break;
    case PROP_ENABLE_PIPELINE:
      enc->enabled_pipeline = g_value_get_boolean (value);
      break;
    case PROP_TUNE_METRICS:
      enc->tune_metrics = g_value_get_boolean (value);
      break;
    case PROP_IN_MEM_BANK:
      enc->in_mem_bank = g_value_get_uint (value);
      break;
    case PROP_OUT_MEM_BANK:
      enc->out_mem_bank = g_value_get_uint (value);
      break;
    case PROP_ULTRA_LOW_LATENCY:
      enc->ultra_low_latency = g_value_get_boolean (value);
      break;
    case PROP_AVC_LOWLAT:
      enc->avc_lowlat = g_value_get_boolean (value);
      break;
#ifdef ENABLE_XRM_SUPPORT
    case PROP_RESERVATION_ID:
      enc->priv->reservation_id = g_value_get_uint64 (value);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_xvcuenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvasXVCUEnc *enc = GST_VVAS_XVCUENC (object);

  switch (prop_id) {
#ifndef ENABLE_XRM_SUPPORT
    case PROP_SK_CURRENT_INDEX:
      g_value_set_int (value, enc->sk_cur_idx);
      break;
#endif
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, enc->dev_index);
      break;
    case PROP_ASPECT_RATIO:
      g_value_set_enum (value, enc->aspect_ratio);
      break;
    case PROP_B_FRAMES:
      g_value_set_int (value, enc->b_frames);
      break;
    case PROP_CONSTRAINED_INTRA_PREDICTION:
      g_value_set_boolean (value, enc->constrained_intra_prediction);
      break;
    case PROP_CONTROL_RATE:
      g_value_set_enum (value, enc->control_rate);
      break;
    case PROP_CPB_SIZE:
      g_value_set_uint (value, enc->cpb_size);
      break;
    case PROP_ENTROPY_MODE:    /*only for H.264 */
      g_value_set_enum (value, enc->entropy_mode);
      break;
    case PROP_FILLER_DATA:
      g_value_set_boolean (value, enc->filler_data);
      break;
    case PROP_GDR_MODE:
      g_value_set_enum (value, enc->gdr_mode);
      break;
    case PROP_GOP_LENGTH:
      g_value_set_uint (value, enc->gop_length);
      break;
    case PROP_GOP_MODE:
      g_value_set_enum (value, enc->gop_mode);
      break;
    case PROP_INITIAL_DELAY:
      g_value_set_uint (value, enc->initial_delay);
      break;
    case PROP_LOOP_FILTER_MODE:
      g_value_set_enum (value, enc->loop_filter_mode);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_uint (value, enc->max_bitrate);
      break;
    case PROP_MAX_QP:
      g_value_set_uint (value, enc->max_qp);
      break;
    case PROP_MIN_QP:
      g_value_set_uint (value, enc->min_qp);
      break;
    case PROP_NUM_SLICES:
      g_value_set_uint (value, enc->num_slices);
      break;
    case PROP_IDR_PERIODICITY:
      g_value_set_uint (value, enc->periodicity_idr);
      break;
    case PROP_PREFETCH_BUFFER:
      g_value_set_boolean (value, enc->prefetch_buffer);
      break;
    case PROP_QP_MODE:
      g_value_set_enum (value, enc->qp_mode);
      break;
    case PROP_SCALING_LIST:
      g_value_set_enum (value, enc->scaling_list);
      break;
    case PROP_SLICE_QP:
      g_value_set_int (value, enc->slice_qp);
      break;
    case PROP_SLICE_SIZE:
      g_value_set_uint (value, enc->slice_size);
      break;
    case PROP_TARGET_BITRATE:
      g_value_set_uint (value, enc->target_bitrate);
      break;
    case PROP_NUM_CORES:
      g_value_set_uint (value, enc->num_cores);
      break;
    case PROP_RATE_CONTROL_MODE:
      g_value_set_boolean (value, enc->rc_mode);
      break;
    case PROP_DEPENDENT_SLICE:
      g_value_set_boolean (value, enc->dependent_slice);
      break;
    case PROP_IP_DELTA:
      g_value_set_int (value, enc->ip_delta);
      break;
    case PROP_PB_DELTA:
      g_value_set_int (value, enc->pb_delta);
      break;
    case PROP_LOOP_FILTER_BETA_OFFSET:
      g_value_set_int (value, enc->loop_filter_beta_offset);
      break;
    case PROP_LOOK_FILTER_TC_OFFSET:
      g_value_set_int (value, enc->loop_filter_tc_offset);
      break;
    case PROP_ENABLE_PIPELINE:
      g_value_set_boolean (value, enc->enabled_pipeline);
      break;
    case PROP_TUNE_METRICS:
      g_value_set_boolean (value, enc->tune_metrics);
      break;
    case PROP_IN_MEM_BANK:
      g_value_set_uint (value, enc->in_mem_bank);
      break;
    case PROP_OUT_MEM_BANK:
      g_value_set_uint (value, enc->out_mem_bank);
      break;
    case PROP_ULTRA_LOW_LATENCY:
      g_value_set_boolean (value, enc->ultra_low_latency);
      break;
    case PROP_AVC_LOWLAT:
      g_value_set_boolean (value, enc->avc_lowlat);
      break;
#ifdef ENABLE_XRM_SUPPORT
    case PROP_RESERVATION_ID:
      g_value_set_uint64 (value, enc->priv->reservation_id);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_vvas_xvcuenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstVvasXVCUEnc *enc = GST_VVAS_XVCUENC (encoder);
  gboolean bret = FALSE;
  gboolean is_force_keyframe = FALSE;
  GstBuffer *inbuf = NULL;

  bret = vvas_xvcuenc_process_input_frame (enc, frame, &inbuf,
      &is_force_keyframe);

  gst_video_codec_frame_unref (frame);

  if (!bret)
    goto error;

  if (inbuf) {
    bret = vvas_xvcuenc_send_frame (enc, inbuf, is_force_keyframe);
    if (!bret)
      goto error;

    return vvas_xvcuenc_receive_out_frame (enc);
  } else {
    return GST_FLOW_OK;
  }

error:
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_vvas_xvcuenc_finish (GstVideoEncoder * encoder)
{
  GstVvasXVCUEnc *enc = GST_VVAS_XVCUENC (encoder);
  GstVvasXVCUEncPrivate *priv = enc->priv;
  GstFlowReturn fret = GST_FLOW_OK;
  gboolean bret = FALSE;

  if (!enc->priv->init_done)
    return GST_FLOW_OK;

  GST_DEBUG_OBJECT (enc, "finish");

  if (enc->enabled_pipeline) {
    GstFlowReturn fret = GST_FLOW_OK;
    GST_INFO_OBJECT (enc, "input copy queue has %d pending buffers",
        g_async_queue_length (priv->copy_outqueue));
    while (g_async_queue_length (priv->copy_outqueue) > 0) {
      gboolean bret = FALSE;
      VvasEncCopyObject *copy_outobj = g_async_queue_pop (priv->copy_outqueue);

      bret = vvas_xvcuenc_send_frame (enc, copy_outobj->copy_inbuf,
          copy_outobj->is_force_keyframe);
      if (!bret)
        goto error;

      copy_outobj->copy_inbuf = NULL;
      g_slice_free (VvasEncCopyObject, copy_outobj);

      fret = vvas_xvcuenc_receive_out_frame (enc);
      if (fret != GST_FLOW_OK)
        return fret;
    }
  }
  // TODO: add support when encoder not negotiated
  bret = vvas_xvcuenc_send_flush (enc);
  if (!bret)
    goto error;

  do {
    fret = vvas_xvcuenc_receive_out_frame (enc);
  } while (fret == GST_FLOW_OK);

  return fret;

error:
  return GST_FLOW_ERROR;
}

static void
gst_vvas_xvcuenc_finalize (GObject * object)
{
  GstVvasXVCUEnc *enc = GST_VVAS_XVCUENC (object);
  GstVvasXVCUEncPrivate *priv = enc->priv;

  g_cond_clear (&priv->timeout_cond);
  g_mutex_clear (&priv->timeout_lock);

  if (enc->input_state) {
    gst_video_codec_state_unref (enc->input_state);
    enc->input_state = NULL;
  }
#ifndef ENABLE_XRM_SUPPORT
  if (enc->xclbin_path)
    g_free (enc->xclbin_path);
#endif

  if (enc->kernel_name) {
    g_free (enc->kernel_name);
    enc->kernel_name = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vvas_xvcuenc_class_init (GstVvasXVCUEncClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstVideoEncoderClass *enc_class = GST_VIDEO_ENCODER_CLASS (klass);

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &sink_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &src_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Xilinx VCU H264/H265 encoder", "Encoder/Video",
      "Xilinx H264/H265 Encoder", "Xilinx Inc., https://www.xilinx.com");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xvcuenc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xvcuenc_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_vvas_xvcuenc_finalize);
  enc_class->start = GST_DEBUG_FUNCPTR (gst_vvas_xvcuenc_start);
  enc_class->stop = GST_DEBUG_FUNCPTR (gst_vvas_xvcuenc_stop);
  enc_class->set_format = GST_DEBUG_FUNCPTR (gst_vvas_xvcuenc_set_format);
  enc_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_vvas_xvcuenc_propose_allocation);
  enc_class->finish = GST_DEBUG_FUNCPTR (gst_vvas_xvcuenc_finish);
  enc_class->handle_frame = GST_DEBUG_FUNCPTR (gst_vvas_xvcuenc_handle_frame);
  enc_class->sink_query = GST_DEBUG_FUNCPTR (gst_vvas_xvcuenc_sink_query);

  g_object_class_install_property (gobject_class, PROP_XCLBIN_LOCATION,
      g_param_spec_string ("xclbin-location", "xclbin file location",
          "Location of the xclbin to program device", NULL,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

#ifndef ENABLE_XRM_SUPPORT
  /* softkernel current index = sk-start-idx + instance_number */
  g_object_class_install_property (gobject_class, PROP_SK_CURRENT_INDEX,
      g_param_spec_int ("sk-cur-idx", "Current softkernel index",
          "Current softkernel index", -1, 31, DEFAULT_SK_CURRENT_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
#endif

  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("dev-idx", "Device index",
          "Valid Device index is 0 to 31. Default value is set to -1 intentionally so that user provides the correct device index.",
          -1, 31, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_KERNEL_NAME,
      g_param_spec_string ("kernel-name", "VCU Encoder kernel name",
          "VCU Encoder kernel name", VVAS_VCUENC_KERNEL_NAME_DEFAULT,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

#ifdef ENABLE_XRM_SUPPORT
  g_object_class_install_property (gobject_class, PROP_RESERVATION_ID,
      g_param_spec_uint64 ("reservation-id", "XRM reservation id",
          "Resource Pool Reservation id", 0, G_MAXUINT64, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
#endif

  /*******************************************************
   * VCU Encoder specific parameters
   *******************************************************/

  /* VCU AspectRatio */
  g_object_class_install_property (gobject_class, PROP_ASPECT_RATIO,
      g_param_spec_enum ("aspect-ratio", "Aspect ratio",
          "Display aspect ratio of the video sequence to be written in SPS/VUI",
          GST_TYPE_VVAS_VIDEO_ENC_ASPECT_RATIO,
          GST_VVAS_VIDEO_ENC_ASPECT_RATIO_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU Gop.NumB */
  g_object_class_install_property (gobject_class, PROP_B_FRAMES,
      g_param_spec_int ("b-frames", "Number of B-frames",
          "Number of B-frames between two consecutive P-frames. "
          "By default, internally set to 0 for ultra-low-latency mode, 2 otherwise if not configured or configured with -1",
          -1, G_MAXINT, UNSET_NUM_B_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* VCU ConstrainedIntraPred */
  g_object_class_install_property (gobject_class,
      PROP_CONSTRAINED_INTRA_PREDICTION,
      g_param_spec_boolean ("constrained-intra-prediction",
          "Constrained Intra Prediction",
          "If enabled, prediction only uses residual data and decoded samples "
          "from neighbouring coding blocks coded using intra prediction modes. This property is experimental.",
          GST_VVAS_VIDEO_ENC_CONSTRAINED_INTRA_PREDICTION_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU RateCtrlMode */
  g_object_class_install_property (gobject_class, PROP_CONTROL_RATE,
      g_param_spec_enum ("control-rate", "Control Rate",
          "Bitrate control method", GST_TYPE_VVAS_VIDEO_ENC_CONTROL_RATE,
          GST_VVAS_VIDEO_ENC_CONTROL_RATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU RateCtrlMode */
  g_object_class_install_property (gobject_class, PROP_CPB_SIZE,
      g_param_spec_uint ("cpb-size", "CPB size",
          "Coded Picture Buffer as specified in the HRD model in msec. "
          "Not used when control-rate=disable. This property is experimental.",
          0, G_MAXUINT, GST_VVAS_VIDEO_ENC_CPB_SIZE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU EntropyMode (only for H.264) */
  g_object_class_install_property (gobject_class, PROP_ENTROPY_MODE,
      g_param_spec_enum ("entropy-mode", "H264 Entropy Mode.",
          "Entropy mode for encoding process (only in H264). This property is experimental.",
          GST_TYPE_VVAS_ENC_ENTROPY_MODE,
          GST_VVAS_VIDEO_ENC_ENTROPY_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU EnableFillerData */
  g_object_class_install_property (gobject_class, PROP_FILLER_DATA,
      g_param_spec_boolean ("filler-data", "Filler Data.",
          "Enable/Disable Filler Data NAL units for CBR rate control. This property is experimental.",
          GST_VVAS_VIDEO_ENC_FILLER_DATA_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU Gop.GdrMode */
  g_object_class_install_property (gobject_class, PROP_GDR_MODE,
      g_param_spec_enum ("gdr-mode", "GDR mode",
          "Gradual Decoder Refresh scheme mode. Only used if "
          "gop-mode=low-delay-p/low-delay-b. This property is experimental.",
          GST_TYPE_VVAS_VIDEO_ENC_GDR_MODE,
          GST_VVAS_VIDEO_ENC_GDR_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU Gop.Length */
  g_object_class_install_property (gobject_class, PROP_GOP_LENGTH,
      g_param_spec_uint ("gop-length", "Gop Length",
          "Number of all frames in 1 GOP, Must be in multiple of (b-frames+1), "
          "Distance between two consecutive I frames", 0, 1000,
          GST_VVAS_VIDEO_ENC_GOP_LENGTH_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU GopCtrlMode */
  g_object_class_install_property (gobject_class, PROP_GOP_MODE,
      g_param_spec_enum ("gop-mode", "GOP mode",
          "Group Of Pictures mode. This property is experimental.",
          GST_TYPE_VVAS_VIDEO_ENC_GOP_MODE,
          GST_VVAS_VIDEO_ENC_GOP_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU GopCtrlMode */
  g_object_class_install_property (gobject_class, PROP_INITIAL_DELAY,
      g_param_spec_uint ("initial-delay", "Initial Delay",
          "The initial removal delay as specified in the HRD model in msec. "
          "Not used when control-rate=disable. This property is experimental.",
          0, G_MAXUINT, GST_VVAS_VIDEO_ENC_INITIAL_DELAY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU LoopFilter */
  g_object_class_install_property (gobject_class, PROP_LOOP_FILTER_MODE,
      g_param_spec_enum ("loop-filter-mode", "Loop Filter mode.",
          "Enable or disable the deblocking filter. This property is experimental.",
          GST_TYPE_VVAS_ENC_LOOP_FILTER_MODE,
          GST_VVAS_VIDEO_ENC_LOOP_FILTER_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU MaxBitRate */
  g_object_class_install_property (gobject_class, PROP_MAX_BITRATE,
      g_param_spec_uint ("max-bitrate", "Max Bitrate",
          "Max bitrate in Kbps, only used if control-rate=variable",
          0, 35000000, GST_VVAS_VIDEO_ENC_MAX_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU MaxQP */
  g_object_class_install_property (gobject_class, PROP_MAX_QP,
      g_param_spec_uint ("max-qp", "max Quantization value",
          "Maximum QP value allowed for the rate control",
          0, 51, GST_VVAS_VIDEO_ENC_MAX_QP_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU MinQP */
  g_object_class_install_property (gobject_class, PROP_MIN_QP,
      g_param_spec_uint ("min-qp", "min Quantization value",
          "Minimum QP value allowed for the rate control",
          0, 51, GST_VVAS_VIDEO_ENC_MIN_QP_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU NumSlices */
  g_object_class_install_property (gobject_class, PROP_NUM_SLICES,
      g_param_spec_uint ("num-slices", "Number of slices",
          "Number of slices produced for each frame. Each slice contains one or more complete macroblock/CTU row(s). "
          "Slices are distributed over the frame as regularly as possible. If slice-size is defined as well more slices "
          "may be produced to fit the slice-size requirement. "
          "In low-latency mode  H.264(AVC): 32,  H.265 (HEVC): 22 and "
          "In normal latency-mode H.264(AVC): picture_height/16, H.265(HEVC): "
          "minimum of picture_height/32",
          1, 68, GST_VVAS_VIDEO_ENC_NUM_SLICES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU Gop.FreqIDR */
  g_object_class_install_property (gobject_class, PROP_IDR_PERIODICITY,
      g_param_spec_uint ("periodicity-idr", "IDR periodicity",
          "Periodicity of IDR frames",
          0, G_MAXUINT, GST_VVAS_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU CacheLevel2 */
  g_object_class_install_property (gobject_class, PROP_PREFETCH_BUFFER,
      g_param_spec_boolean ("prefetch-buffer", "L2Cache buffer.",
          "Enable/Disable L2Cache buffer in encoding process. This property is experimental.",
          GST_VVAS_VIDEO_ENC_PREFETCH_BUFFER_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU QpCtrlMode */
  g_object_class_install_property (gobject_class, PROP_QP_MODE,
      g_param_spec_enum ("qp-mode", "QP mode",
          "QP control mode used by the VCU encoder",
          GST_TYPE_VVAS_VIDEO_ENC_QP_MODE, GST_VVAS_VIDEO_ENC_QP_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU ScalingList */
  g_object_class_install_property (gobject_class, PROP_SCALING_LIST,
      g_param_spec_enum ("scaling-list", "Scaling List",
          "Scaling list mode",
          GST_TYPE_VVAS_VIDEO_ENC_SCALING_LIST,
          GST_VVAS_VIDEO_ENC_SCALING_LIST_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU SliceQP */
  g_object_class_install_property (gobject_class, PROP_SLICE_QP,
      g_param_spec_int ("slice-qp", "Quantization parameter",
          "When RateCtrlMode = CONST_QP the specified QP is applied to all "
          "slices. When RateCtrlMode = CBR the specified QP is used as initial QP",
          -1, 51, GST_VVAS_VIDEO_ENC_SLICE_QP_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU SliceSize */
  g_object_class_install_property (gobject_class, PROP_SLICE_SIZE,
      g_param_spec_uint ("slice-size", "Target slice size",
          "Target slice size (in bytes) that the encoder uses to automatically "
          " split the bitstream into approximately equally-sized slices. This property is experimental.",
          0, 65535, GST_VVAS_VIDEO_ENC_SLICE_SIZE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU BitRate */
  g_object_class_install_property (gobject_class, PROP_TARGET_BITRATE,
      g_param_spec_uint ("target-bitrate", "Target Bitrate",
          "Target bitrate in Kbps (5000 Kbps = component default)",
          0, G_MAXUINT, GST_VVAS_VIDEO_ENC_TARGET_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* VCU Encoder NumCore */
  g_object_class_install_property (gobject_class, PROP_NUM_CORES,
      g_param_spec_uint ("num-cores", "Number of cores",
          "Number of Encoder Cores to be used for current Stream. There are 4 "
          "Encoder cores. Value  0 => AUTO, VCU Encoder will autometically decide the"
          " number of cores for the current stream."
          " Value 1 to 4 => number of cores to be used",
          0, 4, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* VCU Enable RateControl Mode */
  g_object_class_install_property (gobject_class, PROP_RATE_CONTROL_MODE,
      g_param_spec_boolean ("rc-mode", "Rate Control mode",
          "VCU Custom rate control mode",
          GST_VVAS_VIDEO_ENC_RC_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /*VCU Dependent Slice */
  g_object_class_install_property (gobject_class, PROP_DEPENDENT_SLICE,
      g_param_spec_boolean ("dependent-slice", "Dependent Slice",
          "Specifies whether the additional slices are dependent on"
          "other slice segments or regular slices in multiple slices"
          "encoding sessions.Used in H.265 (HEVC) encoding only.",
          GST_VVAS_VIDEO_ENC_DEPENDENT_SLICE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_IP_DELTA,
      g_param_spec_int ("ip-delta", "IP Delta",
          "IP Delta", -1, 51, GST_VVAS_VIDEO_ENC_IP_DELTA_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_PB_DELTA,
      g_param_spec_int ("pb-delta", "PB Delta",
          "PB Delta", -1, 51, GST_VVAS_VIDEO_ENC_PB_DELTA_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_LOOP_FILTER_BETA_OFFSET,
      g_param_spec_int ("loop-filter-beta-offset", "loop filter beta offset",
          "loop filter beta offset", -6, 6,
          GST_VVAS_VIDEO_ENC_LOOP_FILTER_BETA_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_LOOK_FILTER_TC_OFFSET,
      g_param_spec_int ("loop-filter-tc-offset", "loop filter tc offset",
          "loop filter tc offset", -6, 6,
          GST_VVAS_VIDEO_ENC_LOOP_FILTER_TC_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_ENABLE_PIPELINE,
      g_param_spec_boolean ("enable-pipeline",
          "Enable pipelining",
          "Enable buffer pipelining to improve performance in non zero-copy use cases",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  /* Tune Metrics */
  g_object_class_install_property (gobject_class, PROP_TUNE_METRICS,
      g_param_spec_boolean ("tune-metrics", "Tune Metrics",
          "Tunes Encoder's video quality for objective metrics ",
          GST_VVAS_VIDEO_ENC_TUNE_METRICS_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_IN_MEM_BANK,
      g_param_spec_uint ("in-mem-bank", "VVAS Input Memory Bank",
          "VVAS input memory bank to allocate memory",
          0, G_MAXUSHORT, DEFAULT_MEM_BANK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_OUT_MEM_BANK,
      g_param_spec_uint ("out-mem-bank", "VVAS Output Memory Bank",
          "VVAS output memory bank to allocate memory",
          0, G_MAXUSHORT, DEFAULT_MEM_BANK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_ULTRA_LOW_LATENCY,
      g_param_spec_boolean ("ultra-low-latency",
          "Serialize encoding",
          "Serializes encoding when b-frames=0",
          GST_VVAS_VIDEO_ENC_ULTRA_LOW_LATENCY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_AVC_LOWLAT,
      g_param_spec_boolean ("avc-lowlat",
          "Enable AVC low latency flag for H264 to run on multiple cores",
          "Enable AVC low latency flag for H264 to run on multiple cores in ultra-low-latency mode",
          GST_VVAS_VIDEO_ENC_AVC_LOWLAT_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xvcuenc_debug_category, "vvas_xvcuenc", 0,
      "debug category for vcu h264/h265 encoder element");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
}

static void
gst_vvas_xvcuenc_init (GstVvasXVCUEnc * enc)
{
  GstVvasXVCUEncPrivate *priv = GST_VVAS_XVCUENC_PRIVATE (enc);
  enc->priv = priv;
  enc->sk_start_idx = -1;
  enc->codec_type = VVAS_CODEC_NONE;
  enc->dev_index = DEFAULT_DEVICE_INDEX;
  enc->kernel_name = g_strdup (VVAS_VCUENC_KERNEL_NAME_DEFAULT);
  enc->sk_cur_idx = DEFAULT_SK_CURRENT_INDEX;
  enc->enabled_pipeline = 0;
  enc->control_rate = GST_VVAS_VIDEO_ENC_CONTROL_RATE_DEFAULT;
  enc->target_bitrate = GST_VVAS_VIDEO_ENC_TARGET_BITRATE_DEFAULT;
  enc->qp_mode = GST_VVAS_VIDEO_ENC_QP_MODE_DEFAULT;
  enc->min_qp = GST_VVAS_VIDEO_ENC_MIN_QP_DEFAULT;
  enc->max_qp = GST_VVAS_VIDEO_ENC_MAX_QP_DEFAULT;
  enc->gop_mode = GST_VVAS_VIDEO_ENC_GOP_MODE_DEFAULT;
  enc->gdr_mode = GST_VVAS_VIDEO_ENC_GDR_MODE_DEFAULT;
  enc->initial_delay = GST_VVAS_VIDEO_ENC_INITIAL_DELAY_DEFAULT;
  enc->cpb_size = GST_VVAS_VIDEO_ENC_CPB_SIZE_DEFAULT;
  enc->scaling_list = GST_VVAS_VIDEO_ENC_SCALING_LIST_DEFAULT;
  enc->max_bitrate = GST_VVAS_VIDEO_ENC_MAX_BITRATE_DEFAULT;
  enc->aspect_ratio = GST_VVAS_VIDEO_ENC_ASPECT_RATIO_DEFAULT;
  enc->filler_data = GST_VVAS_VIDEO_ENC_FILLER_DATA_DEFAULT;
  enc->num_slices = GST_VVAS_VIDEO_ENC_NUM_SLICES_DEFAULT;
  enc->slice_size = GST_VVAS_VIDEO_ENC_SLICE_SIZE_DEFAULT;
  enc->prefetch_buffer = GST_VVAS_VIDEO_ENC_PREFETCH_BUFFER_DEFAULT;
  enc->periodicity_idr = GST_VVAS_VIDEO_ENC_PERIODICITY_OF_IDR_FRAMES_DEFAULT;
  enc->b_frames = UNSET_NUM_B_FRAMES;
  enc->gop_length = GST_VVAS_VIDEO_ENC_GOP_LENGTH_DEFAULT;
  enc->entropy_mode = GST_VVAS_VIDEO_ENC_ENTROPY_MODE_DEFAULT;
  enc->slice_qp = GST_VVAS_VIDEO_ENC_SLICE_QP_DEFAULT;
  enc->constrained_intra_prediction =
      GST_VVAS_VIDEO_ENC_CONSTRAINED_INTRA_PREDICTION_DEFAULT;
  enc->loop_filter_mode = GST_VVAS_VIDEO_ENC_LOOP_FILTER_MODE_DEFAULT;
  enc->level = NULL;
  enc->tier = NULL;
  enc->num_cores = 0;
  enc->rc_mode = GST_VVAS_VIDEO_ENC_RC_MODE_DEFAULT;
  enc->tune_metrics = GST_VVAS_VIDEO_ENC_TUNE_METRICS_DEFAULT;
  enc->dependent_slice = GST_VVAS_VIDEO_ENC_DEPENDENT_SLICE_DEFAULT;
  enc->ip_delta = GST_VVAS_VIDEO_ENC_IP_DELTA_DEFAULT;
  enc->pb_delta = GST_VVAS_VIDEO_ENC_PB_DELTA_DEFAULT;
  enc->loop_filter_beta_offset = GST_VVAS_VIDEO_ENC_LOOP_FILTER_BETA_DEFAULT;
  enc->loop_filter_tc_offset = GST_VVAS_VIDEO_ENC_LOOP_FILTER_TC_DEFAULT;
  enc->ultra_low_latency = GST_VVAS_VIDEO_ENC_ULTRA_LOW_LATENCY_DEFAULT;
  enc->avc_lowlat = GST_VVAS_VIDEO_ENC_AVC_LOWLAT_DEFAULT;
  strcpy (enc->color_description, "COLOUR_DESC_UNSPECIFIED");
  strcpy (enc->transfer_characteristics, "TRANSFER_UNSPECIFIED");
  strcpy (enc->color_matrix, "COLOUR_MAT_UNSPECIFIED");
  enc->in_mem_bank = DEFAULT_MEM_BANK;
  enc->out_mem_bank = DEFAULT_MEM_BANK;

#ifdef ENABLE_XRM_SUPPORT
  priv->reservation_id = 0;
#endif
  g_mutex_init (&priv->timeout_lock);
  g_cond_init (&priv->timeout_cond);
  vvas_xvcuenc_reset (enc);
}

#ifndef PACKAGE
#define PACKAGE "vvas_xvcuenc"
#endif

static gboolean
vcu_enc_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "vvas_xvcuenc", GST_RANK_NONE,
      GST_TYPE_VVAS_XVCUENC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xvcuenc,
    "Xilinx VCU H264/H264 Encoder plugin", vcu_enc_init, VVAS_API_VERSION,
    GST_LICENSE_UNKNOWN, "GStreamer Xilinx", "http://xilinx.com/")
