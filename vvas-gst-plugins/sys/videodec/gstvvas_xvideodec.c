/*
 * Copyright 2020-2022 Xilinx, Inc.
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

#define HDR_DATA_SUPPORT

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvvas_xvideodec.h"
#include <gst/vvas/gstvvasbufferpool.h>
#include <gst/vvas/gstvvasallocator.h>
#ifdef HDR_DATA_SUPPORT
#include <gst/vvas/mpsoc_vcu_hdr.h>
#endif
#include <gst/vvas/gstvvashdrmeta.h>
#include <experimental/xrt-next.h>

#include <gst/vvas/gstvvascoreutils.h>
#include <vvas_core/vvas_context.h>
#include <vvas_core/vvas_common.h>
#include <vvas_core/vvas_decoder.h>

#ifdef ENABLE_XRM_SUPPORT
#include <xrm.h>
#include <xrm_limits.h>
#include <dlfcn.h>
#include <jansson.h>
#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))
#endif

#define DEFAULT_MEM_BANK 0

GST_DEBUG_CATEGORY_STATIC (gstvvas_xvideodec_debug_category);
#define GST_CAT_DEFAULT gstvvas_xvideodec_debug_category

/** @def DEFAULT_DEVICE_INDEX
 *  @brief Default accelrator device index.
 */
#define DEFAULT_DEVICE_INDEX -1

/** @def DEFAULT_SK_CURRENT_INDEX
 *  @brief Default current softkernel  index.
 */
#define DEFAULT_SK_CURRENT_INDEX -1

/** @def VVAS_VIDEODEC_AVOID_OUTPUT_COPY_DEFAULT
 *  @brief Enable output buffer copy by default
 */
#define VVAS_VIDEODEC_AVOID_OUTPUT_COPY_DEFAULT FALSE

/** @def DEC_MAX_SK_CU_INDEX
 *  @brief Maximum softkernel CUs are 32 (0-31). Set max CU index to 31.
 */
#define DEC_MAX_SK_CU_INDEX 31

/** @def ENABLE_DMABUF
 *  @brief DMABUF support is disabled by default
 */
#define ENABLE_DMABUF 0

/** @def WIDTH_ALIGN
 *  @brief Decoder width alignment.
 */
#define WIDTH_ALIGN 256

/** @def HEIGHT_ALIGN
 *  @brief Decoder height alignment.
 */
#define HEIGHT_ALIGN 64

/** @def MAX_ERR_STRING
 *  @brief Maximum error string size.
 */
#define MAX_ERR_STRING 1024

#ifdef XLNX_V70_PLATFORM
/** @def DEFAULT_VDU_INSTANCE_ID
 *  @brief Default decoder instance id.
 */
#define DEFAULT_VDU_INSTANCE_ID 0
#endif

/* AVC Profiles */
#define AVC_PROFILE_IDC_BASELINE 66
#define AVC_PROFILE_IDC_CONSTRAINED_BASELINE (AVC_PROFILE_IDC_BASELINE | (1<<9))
#define AVC_PROFILE_IDC_MAIN     77
#define AVC_PROFILE_IDC_HIGH     100
#define AVC_PROFILE_IDC_HIGH10   110
#define AVC_PROFILE_IDC_HIGH10_INTRA (AVC_PROFILE_IDC_HIGH10 | (1<<11))

/* HEVC Profiles */
#define HEVC_PROFILE_IDC_MAIN    1
#define HEVC_PROFILE_IDC_MAIN10  2
#define HEVC_PROFILE_IDC_RExt 4


static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/x-h264, stream-format=(string)byte-stream, alignment=(string)au;"
        "video/x-h265, stream-format=(string)byte-stream, alignment=(string)au;"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ NV12, NV12_10LE32 }")));

/** @struct _xlnx_output_buf
 *  @brief  Decoder output buffer metadata.
 */
typedef struct _xlnx_output_buf
{
  /** Output buffer index */
  guint idx;
  /**  XRT buffer metadata object */
  xrt_buffer xrt_buf;
  /** weak reference for pointer comparison */
  GstBuffer *gstbuf;
} XlnxOutputBuffer;

/** @struct _GstVvas_XVideoDecPrivate
 *  @brief  Decoder private data.
 */
struct _GstVvas_XVideoDecPrivate
{
  /** number of output buffers */
  guint num_out_bufs;
  /** xclbin Id */
  uuid_t xclbinId;
  /** decoder compute unit index */
  gint cu_idx;
   /** Buffer pool */
  GstBufferPool *pool;
  /** Lock used while using free output buffer list */
  GMutex obuf_lock;
  /** Condition variable for output buffer availability */
  GCond obuf_cond;
  /** Custom allocator handle */
  GstAllocator *allocator;
  /** Flag to indicate whether to copy output decoded frames */
  gboolean need_copy;
  /** Retry timeout in micro sec */
  gint64 retry_timeout;
  /** PTS of earlier frame */
  GstClockTime last_pts;
  /** Linear incremental used in PTS interpolation */
  gint genpts;
  gboolean init_done;

#ifdef ENABLE_XRM_SUPPORT
  /** XRM context handle */
  xrmContext xrm_ctx;
   /** Allocated/released compute resource list version 2 */
  xrmCuListResourceV2 *cu_list_res;
  /** XRM Compute resource handle */
  xrmCuResource *cu_res[2];
  /** Holds the current Compute Unit load requirement */
  gint cur_load;
  /** Reservation id is used to allocate cu from specified resource pool */
  uint64_t reservation_id;
#endif

  /** VVAS Core objects */
  VvasContext *vvas_ctx;
  VvasDecoder *vvas_dec;
  GHashTable *vf_to_gstbuf_map;
  GList *free_vframe_list;
  VvasCodecType dec_type;
};
#define gstvvas_xvideodec_parent_class parent_class

G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_XVideoDec, gstvvas_xvideodec,
    GST_TYPE_VIDEO_DECODER);
#define GST_VVAS_XVIDEODEC_PRIVATE(dec) (GstVvas_XVideoDecPrivate *) \
                    (gstvvas_xvideodec_get_instance_private (dec))
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

static gboolean vvas_xvideodec_destroy_context (GstVvas_XVideoDec * dec);

/**
 * @brief Contains properties related to Decoder plugin
 */
enum
{
  /** default */
  PROP_0,
  /** Property to set xclbin path*/
  PROP_XCLBIN_LOCATION,
  /** Property to set number of decoder internal buffers */
  PROP_NUM_ENTROPY_BUFFER,
  /** Property to set decoder latency mode */
  PROP_LOW_LATENCY,
#ifndef ENABLE_XRM_SUPPORT
#ifdef XLNX_U30_PLATFORM
  /** Property to set soft kernel CU current index */
  PROP_SK_CURRENT_INDEX,
#endif
#endif
  /** Property to set device index */
  PROP_DEVICE_INDEX,
  /** Property to set kernel name */
  PROP_KERNEL_NAME,
  /** Property to set output buffer copy flag */
  PROP_AVOID_OUTPUT_COPY,
#ifdef ENABLE_XRM_SUPPORT
  /** Property to set reservation ID for XRM */
  PROP_RESERVATION_ID,
#endif
  /** Property to set decoder input mode of operation flag */
  PROP_SPLITBUFF_MODE,
  /** Property to set dynamic output buffer allocation flag */
  PROP_AVOID_DYNAMIC_ALLOC,
  /** Property to set PTS interpolation flag */
  PROP_INTERPOLATE_TIMESTAMPS,
  /** Property to configure additional output buffers */
  PROP_ADDITIONAL_OUTPUT_BUFFERS,
#ifdef XLNX_V70_PLATFORM
  /** Property to set VDU instance id */
  PROP_VDU_INSTANCE_ID,
  /** Property to set I-Frame only decode */
  PROP_I_FRAME_ONLY,
  /** Property to set decoding rate */
  PROP_FORCE_DECODE_RATE,
#endif
};

#ifdef XLNX_V70_PLATFORM
#define VVAS_VIDEODEC_KERNEL_NAME_DEFAULT "kernel_vdu_decoder:{kernel_vdu_decoder_0}"
#else
#define VVAS_VIDEODEC_KERNEL_NAME_DEFAULT "decoder:{decoder_1}"
#endif

/** @fn guint set_align_param (GstVvas_XVideoDec * dec,
                               GstVideoInfo * info,
                               GstVideoAlignment * align)
 *  @param [in] dec - decoder context
 *  @param [in] info - video frame format information such as color format,
                width, height etc.
 *  @param [out] align - frame padding alignment information
 *
 *  @return On Success returns the size of the padded frame on success
 *          On Failure returns false
 *
 *  @brief  Assigns video frame alignment parameters
 *
 *  @details Based on the video format information, width, height parameters
 *           are obtained and padding alignment is determined as per Video
 *           decoder.
 */
static guint
set_align_param (GstVvas_XVideoDec * dec, GstVideoInfo * info,
    GstVideoAlignment * align)
{
#define DIV_AND_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
  gint width, height;
  guint stride = 0;


  if (!(GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_NV12_10LE32
          || GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_NV12))
    return FALSE;

  gst_video_alignment_reset (align);
  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);

  stride = ALIGN (GST_VIDEO_INFO_PLANE_STRIDE (info, 0), WIDTH_ALIGN);
  align->padding_bottom = ALIGN (height, HEIGHT_ALIGN) - height;

  if (stride > GST_VIDEO_INFO_PLANE_STRIDE (info, 0))
    align->padding_right = stride - GST_VIDEO_INFO_PLANE_STRIDE (info, 0);

  /* Stride is in bytes while align->padding params are in pixels so we need to do manual
   * conversions for complex formats. */
  if (GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_NV12_10LE32) {
    align->padding_right *= 0.75;
  }

  GST_LOG_OBJECT (dec, "fmt = %d width = %d height = %d stride = %d",
      GST_VIDEO_INFO_FORMAT (info), width, height, stride);
  GST_LOG_OBJECT (dec, "align top %d bottom %d right %d left =%d",
      align->padding_top, align->padding_bottom,
      align->padding_right, align->padding_left);
  GST_LOG_OBJECT (dec, "size = %d",
      (guint) ((stride * ALIGN (height, HEIGHT_ALIGN)) * 1.5));

  return (stride * ALIGN (height, HEIGHT_ALIGN)) * 1.5;
}

/** @fn guint get_profile_value (const gchar * profile, guint codec_type)
 *
 *  @param [in] profile - codec profile string such as "main"/"high" etc.
 *  @param [in] codec type - 0: H264, 1: HEVC
 *
 *  @return On Success returns the mapped profile as integer
 *          On Failure returns 0
 *
 *  @brief Maps the profle value
 */
static guint
get_profile_value (const gchar * profile, guint codec_type)
{
  if (codec_type == 0) {        /* H264 profiles */
    if (g_str_equal (profile, "baseline"))
      return AVC_PROFILE_IDC_BASELINE;
    else if (g_str_equal (profile, "constrained-baseline"))
      return AVC_PROFILE_IDC_CONSTRAINED_BASELINE;
    else if (g_str_equal (profile, "main"))
      return AVC_PROFILE_IDC_MAIN;
    else if (g_str_equal (profile, "high"))
      return AVC_PROFILE_IDC_HIGH;
    else if (g_str_equal (profile, "high-10"))
      return AVC_PROFILE_IDC_HIGH10;
    else if (g_str_equal (profile, "high-10-intra"))
      return AVC_PROFILE_IDC_HIGH10_INTRA;
  } else if (codec_type == 1) { /* H265 profiles */
    if (g_str_equal (profile, "main"))
      return HEVC_PROFILE_IDC_MAIN;
    else if (g_str_equal (profile, "main-10"))
      return HEVC_PROFILE_IDC_MAIN10;
    else if (g_str_equal (profile, "main-intra") ||
        g_str_equal (profile, "main-10-intra"))
      return HEVC_PROFILE_IDC_RExt;
  }
  return 0;
}

/** @fn guint get_level_value (const gchar * level)
 *
 *  @param [in] level - codec's level value as string
 *
 *  @return On Success returns the mapped level value as integer
 *          On Failure returns 0
 *
 *  @brief Maps the level value
 */
static guint
get_level_value (const gchar * level)
{
  /* use higher level (6.2) if not specified (invalid stream) so we can
     decode it */
  if (!level) {
    g_warning ("Level is not specified, proceding with higher level 6.2\n");
    return 62;
  }

  if (!g_strcmp0 (level, "1b"))
    return 9;
  else if (!g_strcmp0 (level, "1"))
    return 10;
  else if (!g_strcmp0 (level, "1.1"))
    return 11;
  else if (!g_strcmp0 (level, "1.2"))
    return 12;
  else if (!g_strcmp0 (level, "1.3"))
    return 13;
  else if (!g_strcmp0 (level, "2"))
    return 20;
  else if (!g_strcmp0 (level, "2.1"))
    return 21;
  else if (!g_strcmp0 (level, "2.2"))
    return 22;
  else if (!g_strcmp0 (level, "3"))
    return 30;
  else if (!g_strcmp0 (level, "3.1"))
    return 31;
  else if (!g_strcmp0 (level, "3.2"))
    return 32;
  else if (!g_strcmp0 (level, "4"))
    return 40;
  else if (!g_strcmp0 (level, "4.1"))
    return 41;
  else if (!g_strcmp0 (level, "4.2"))
    return 42;
  else if (!g_strcmp0 (level, "5"))
    return 50;
  else if (!g_strcmp0 (level, "5.1"))
    return 51;
  else if (!g_strcmp0 (level, "5.2"))
    return 52;
  else if (!g_strcmp0 (level, "6"))
    return 60;
  else if (!g_strcmp0 (level, "6.1"))
    return 61;
  else if (!g_strcmp0 (level, "6.2"))
    return 62;
  else {
    GST_ERROR ("unsupported level string %s", level);
    return 0;
  }
}

/** @fn gboolean is_level_supported (guint level, guint codec_type)
 *
 *  @param [in] level - codec's level value as integer
 *  @param [in] codec_type - 0: H264, 1: HEVC
 *
 *  @return On Success returns true
 *          On Failure returns false
 *
 *  @brief Verify the supported levels.
 */
static gboolean
is_level_supported (guint level, guint codec_type)
{
  if (codec_type == 0) {        /* H264 */
    switch (level) {
      case 9:
      case 10:
      case 11:
      case 12:
      case 13:
      case 20:
      case 21:
      case 22:
      case 30:
      case 31:
      case 32:
      case 40:
      case 41:
      case 42:
      case 50:
      case 51:
      case 52:
      case 60:
      case 61:
      case 62:
        return TRUE;
      default:
        return FALSE;
    }
  } else {                      /* HEVC */
    switch (level) {
      case 10:
      case 20:
      case 21:
      case 30:
      case 31:
      case 40:
      case 41:
      case 50:
      case 51:
      case 52:
      case 60:
      case 61:
      case 62:
        return TRUE;
      default:
        return FALSE;
    }
  }
  return FALSE;
}

/** @fn guint get_color_format_from_chroma (const gchar * chroma_format,
 *                                          guint bit_depth_luma,
 *                                          guint bit_depth_chroma)
 *
 *  @param [in]  chroma_format - in string format
 *  @param [in]  bit_depth_luma - luma component bit depth
 *  @param [in]  bit_depth_chroma - chroma component bit depth
 *
 *  @return On Success returns chroma in integer format
 *          On Failure returns 420
 *
 *  @brief  Maps chroma format from string to integer.
 *
*/
static guint
get_color_format_from_chroma (const gchar * chroma_format,
    guint bit_depth_luma, guint bit_depth_chroma)
{
  if (!g_strcmp0 (chroma_format, "4:0:0"))
    return 0;
  else if (!g_strcmp0 (chroma_format, "4:2:0"))
    return 420;
  else if (!g_strcmp0 (chroma_format, "4:2:2"))
    return 0;
  else {
    g_warning ("chroma format is not available, taking default as 4:2:0");
    return 420;
  }
}

/** @fn gint64 vvas_xvideodec_get_push_command_retry_timeout (guint width,
 *                                                          guint height,
 *                                                          guint fps_n,
 *                                                          guint fps_d)
 *
 *  @param [in] width - width of the input video frame
 *  @param [in] height - height of the input video frame
 *  @param [in] fps_n - FPS numerator
 *  @param [in] fps_d - FPS denominator
 *
 *  @return returns time(milliseconds) to wait and retry to push data to device
 *
 *  @brief Calculates retry timeout based on input video information
 */
static gint64
vvas_xvideodec_get_push_command_retry_timeout (guint width, guint height,
    guint fps_n, guint fps_d)
{
  guint pixel_rate = (width * height * fps_n) / fps_d;
  /* Video decoder maximum capacity of 4K@60 is considered in below calculation */
  guint max_pixel_rate = 3840 * 2160 * 60;
  gint64 max_timeout_ms = 15 * G_TIME_SPAN_MILLISECOND; /* 15 milli seconds */

  return max_timeout_ms / (max_pixel_rate / pixel_rate);
}

/** @fn gboolean vvas_video_dec_outbuffer_alloc_and_map (GstVvas_XVideoDec * dec,
 *                                                     GstVideoInfo *vinfo)
 *
 *  @param [in] dec - Decoder context
 *  @param [in] vinfo - Gst Video Info
 *
 *  @return On Success returns true
 *          On Failure returns false
 *
 *  @brief   Allocates decoder output and HDR buffers
 *
 *  @details Allocates decoder output buffers and HDR buffers. Also creates a
 *           hash table of buffer metadata with buffer pointer for internal
 *           buffer management.
 */
static gboolean
vvas_video_dec_outbuffer_alloc_and_map (GstVvas_XVideoDec * dec,
    GstVideoInfo * vinfo)
{
  GstVvas_XVideoDecPrivate *priv = dec->priv;
  VvasVideoFrame *output_frame = NULL;
  int i;

  if (!priv->num_out_bufs) {
    GST_ERROR_OBJECT (dec, "invalid output allocation parameters : "
        "num_out_bufs = %d ", priv->num_out_bufs);
    return FALSE;
  }

  GST_INFO_OBJECT (dec,
      "minimum number of output buffers required by video decoder = %d ",
      priv->num_out_bufs);

  /* Allocate 'num_out_bufs' number of output and HDR XRT buffers.
   * Each of the buffer allocated is stored in an XRT buffer array and address
   * of the array is passed to device instead of each buffer address.
   */
  for (i = 0; i < priv->num_out_bufs; i++) {
    GstBuffer *outbuf = NULL;

    if (gst_buffer_pool_acquire_buffer (priv->pool, &outbuf,
            NULL) != GST_FLOW_OK) {
      GST_INFO_OBJECT (dec, "Failed to acquire %d-th buffer", i);
      goto error;
    }

    /* create a video-frame from the gstbuf as video-frame is expected by
       the decoder library */
    output_frame = vvas_videoframe_from_gstbuffer (priv->vvas_ctx,
        dec->out_mem_bank, outbuf, vinfo, GST_MAP_READ);
    if (!output_frame) {
      GST_ERROR_OBJECT (dec, "Could convert input GstBuffer to VvasVideoFrame");
      goto error;
    }

    /* Insert the newly allocated frame and gstbuf into the hash table for
       reverse lookup of gstbuf once vvas-video frame is received from the
       decoder library */
    g_hash_table_insert (priv->vf_to_gstbuf_map, output_frame, outbuf);

    /* Insert all of these allocated video-frame in to the free list, this
       would be used to setup the decoder library at very first invocation of
       vvas_decoder_submit_frames */
    g_mutex_lock (&priv->obuf_lock);
    priv->free_vframe_list
        = g_list_append (priv->free_vframe_list, output_frame);
    g_mutex_unlock (&priv->obuf_lock);

    GST_DEBUG_OBJECT (dec,
        "output [%d] : mapping memory %p with vframe = %p", i,
        outbuf, output_frame);
  }

  return TRUE;

error:
  return FALSE;
}

/** @fn void vvas_xvideodec_reset(GstVvas_XVideoDec * dec)
 *
 *  @param [in] dec - Decoder context
 *  @return returns void
 *
 *  @brief reset the variables which are used throughout the plugin lifecycle.
 */
static void
vvas_xvideodec_reset (GstVvas_XVideoDec * dec)
{
  GstVvas_XVideoDecPrivate *priv = GST_VVAS_XVIDEODEC_PRIVATE (dec);

  priv->pool = NULL;
  priv->init_done = FALSE;
  priv->need_copy = TRUE;
  priv->last_pts = GST_CLOCK_TIME_NONE;
  priv->genpts = 0;
#ifdef ENABLE_XRM_SUPPORT
  priv->xrm_ctx = NULL;
  priv->cu_list_res = NULL;
  priv->cu_res[0] = priv->cu_res[1] = NULL;
  priv->cur_load = 0;
#endif
}

/** @fn void vvas_xvideodec_set_pts (GstVvas_XVideoDec * dec,
 *                                 GstVideoCodecFrame * frame,
 *                                 GstClockTime out_ts)
 *
 *  @param [in]  dec - Decoder context
 *  @param [in]  frame - Input video frame information
 *  @param [in]  out_ts - timestamp
 *
 *  @return void
 *
 *  @brief Calculates the PTS based on framerate and output frame number when
 *         'interpolate-timestamps' property is set
 *  @details First timestamp (timebase) is same as the timestamp returned by
 *           softkernel on device. If it is not valid then timebase starts from
 *           0. From there on PTS is incremented by duration : current
 *           timestamp=prev timestamp + duration.
 */
static void
vvas_xvideodec_set_pts (GstVvas_XVideoDec * dec, GstVideoCodecFrame * frame,
    GstClockTime out_ts)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (dec);
  GstVvas_XVideoDecPrivate *priv = dec->priv;
  GstVideoCodecState *state = gst_video_decoder_get_output_state (decoder);

  if (state == NULL) {
    frame->pts = GST_CLOCK_TIME_NONE;
    return;
  }

  if (state->info.fps_d == 0 || state->info.fps_n == 0) {
    frame->pts = GST_CLOCK_TIME_NONE;
    return;
  }

  if (priv->last_pts == GST_CLOCK_TIME_NONE) {
    if (out_ts == GST_CLOCK_TIME_NONE) {
      frame->pts = 0;
      priv->genpts = 0;
    } else {
      frame->pts = out_ts;
      priv->genpts =
          frame->pts / gst_util_uint64_scale (GST_SECOND, state->info.fps_d,
          state->info.fps_n);
    }
  } else {
    frame->pts =
        gst_util_uint64_scale (priv->genpts * GST_SECOND, state->info.fps_d,
        state->info.fps_n);
  }

  priv->last_pts = frame->pts;
  priv->genpts++;
  return;
}

/** @fn GstFlowReturn receive_out_frame (GstVvas_XVideoDec * dec,
 *                                       VvasVideoFrame *voframe)
 *
 *  @param [in] dec - Decoder context
 *  @param [in] voframe - Output video frame
 *
 *  @return On Success returns GST_FLOW_OK
 *          On Failure returns error code other than GST_FLOW_OK

 *  @brief get the decoder output buffers updated with decoded content
 *  @details output buffers on host are updated with output buffers on device
 *           in this function i.e voframe
 */
static GstFlowReturn
receive_out_frame (GstVvas_XVideoDec * dec, VvasVideoFrame * voframe)
{
  GstVideoCodecState *out_state = NULL;
  GstVideoCodecFrame *frame;
  GstFlowReturn fret;
  GstBuffer *outbuf;
  GstMemory *outmem;
  VvasMetadata vmeta;
  // TODO: get width and height from softkernel
  if (gst_pad_is_active (GST_VIDEO_DECODER_SRC_PAD (dec)) &&
      !gst_pad_has_current_caps (GST_VIDEO_DECODER_SRC_PAD (dec))) {
    GstVideoInfo vinfo;
    GstCaps *outcaps = NULL;
    // TODO: add check for resolution change

    /* HACK: taking input resolution and setting instead of taking from
       softkernel output */
    vinfo = dec->input_state->info;

    if (dec->bit_depth == 10)
      out_state =
          gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
          GST_VIDEO_FORMAT_NV12_10LE32,
          GST_VIDEO_INFO_WIDTH (&vinfo),
          GST_VIDEO_INFO_HEIGHT (&vinfo), dec->input_state);
    else
      out_state =
          gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
          GST_VIDEO_FORMAT_NV12,
          GST_VIDEO_INFO_WIDTH (&vinfo),
          GST_VIDEO_INFO_HEIGHT (&vinfo), dec->input_state);

    /* In case if one of the parameters is zero, base class will override with
       default values. To avoid this, we are passing on the same incoming
       colorimetry information to downstream as well.
       Refer https://jira.xilinx.com/browse/CR-1114507 for more information */
    out_state->info.colorimetry.range =
        dec->input_state->info.colorimetry.range;
    out_state->info.colorimetry.matrix =
        dec->input_state->info.colorimetry.matrix;
    out_state->info.colorimetry.transfer =
        dec->input_state->info.colorimetry.transfer;
    out_state->info.colorimetry.primaries =
        dec->input_state->info.colorimetry.primaries;

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (dec))) {
      GST_ERROR_OBJECT (dec, "Failed to negotiate with downstream elements");
      gst_video_codec_state_unref (out_state);
      return GST_FLOW_NOT_NEGOTIATED;
    }

    outcaps = gst_pad_get_current_caps (GST_VIDEO_DECODER_SRC_PAD (dec));

    if (!gst_video_info_from_caps (&dec->out_vinfo, outcaps)) {
      GST_ERROR_OBJECT (dec, "failed to get out video info from caps");
      fret = GST_FLOW_ERROR;
      goto error;
    }
    GST_INFO_OBJECT (dec,
        "negotiated caps on source pad : %" GST_PTR_FORMAT, outcaps);
  } else {
    out_state = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (dec));
  }

  gst_video_codec_state_unref (out_state);

  frame = gst_video_decoder_get_oldest_frame (GST_VIDEO_DECODER (dec));
  if (!frame) {
    /* Can only happen in finish() */
    GST_INFO_OBJECT (dec, "no input frames available...returning EOS");
    return GST_FLOW_EOS;
  }

  vvas_video_frame_get_metadata (voframe, &vmeta);

  if (dec->interpolate_timestamps) {
    vvas_xvideodec_set_pts (dec, frame, vmeta.pts);
  } else {
    frame->pts = vmeta.pts;
  }

  /* get the Gstbuf(outbuf) corresponding to voframe */
  outbuf = g_hash_table_lookup (dec->priv->vf_to_gstbuf_map, voframe);

  /* Remove from hash table */
  g_hash_table_remove (dec->priv->vf_to_gstbuf_map, voframe);

  if (dec->priv->need_copy) {
    GstBuffer *new_outbuf;
    GstVideoFrame new_frame, out_frame;

    outmem = gst_buffer_get_memory (outbuf, 0);
    /* when plugins/app request to map this memory, sync will occur */
    gst_vvas_memory_set_sync_flag (outmem, VVAS_SYNC_FROM_DEVICE);
    gst_memory_unref (outmem);

    new_outbuf =
        gst_buffer_new_and_alloc (GST_VIDEO_INFO_SIZE (&dec->out_vinfo));
    if (!new_outbuf) {
      GST_ERROR_OBJECT (dec, "failed to allocate output buffer");
      return GST_FLOW_ERROR;
    }

    gst_video_frame_map (&out_frame, &dec->out_vinfo, outbuf, GST_MAP_READ);
    gst_video_frame_map (&new_frame, &dec->out_vinfo, new_outbuf,
        GST_MAP_WRITE);
    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, dec,
        "slow copy data from %p to %p", outbuf, new_outbuf);
    gst_video_frame_copy (&new_frame, &out_frame);
    gst_video_frame_unmap (&out_frame);
    gst_video_frame_unmap (&new_frame);

    gst_buffer_copy_into (new_outbuf, outbuf, GST_BUFFER_COPY_FLAGS, 0, -1);
    gst_buffer_unref (outbuf);

    frame->output_buffer = new_outbuf;
  } else {
    frame->output_buffer = outbuf;
    outmem = gst_buffer_get_memory (frame->output_buffer, 0);

    /* when plugins/app request to map this memory, sync will occur */
    gst_vvas_memory_set_sync_flag (outmem, VVAS_SYNC_FROM_DEVICE);
    gst_memory_unref (outmem);
  }

  /* free so that associated gstbuf can be unref'ed */
  vvas_video_frame_free (voframe);

  GST_LOG_OBJECT (dec, "processing buffer %" GST_PTR_FORMAT,
      frame->output_buffer);

  fret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (dec), frame);

  if (fret != GST_FLOW_OK) {
    if (fret == GST_FLOW_EOS || fret == GST_FLOW_FLUSHING)
      GST_DEBUG_OBJECT (dec, "failed to push frame. reason %s",
          gst_flow_get_name (fret));
    else
      GST_ERROR_OBJECT (dec, "failed to push frame. reason %s",
          gst_flow_get_name (fret));

    frame = NULL;
    goto error;
  }

  return fret;

error:
  return fret;
}

/** @fn gboolean gstvvas_xvideodec_start (GstVideoDecoder * decoder)
 *
 *  @param [in] decoder - Decoder context
 *
 *  @return On Success returns true
 *          On Failure returns false

 *  @brief  reset and intialize the decoder session
 */
static gboolean
gstvvas_xvideodec_start (GstVideoDecoder * decoder)
{
  GstVvas_XVideoDec *dec = GST_VVAS_XVIDEODEC (decoder);

  vvas_xvideodec_reset (dec);

  dec->priv->vf_to_gstbuf_map
      = g_hash_table_new (g_direct_hash, g_direct_equal);
#ifdef ENABLE_XRM_SUPPORT

  /* create XRM context for managing the decode instances as device has limited
   * resources
   */
  dec->priv->xrm_ctx = (xrmContext *) xrmCreateContext (XRM_API_VERSION_1);
  if (!dec->priv->xrm_ctx) {
    GST_ERROR_OBJECT (dec, "create XRM context failed");
    return FALSE;
  }

  GST_INFO_OBJECT (dec, "successfully created xrm context");
#endif

  return TRUE;
}

static gboolean
free_output_bufs (gpointer key, gpointer value, gpointer user_data)
{
  vvas_video_frame_free (key);
  gst_buffer_unref (value);
  return TRUE;
}

/** @fn gstvvas_xvideodec_stop (GstVideoDecoder * decoder)
 *
 *  @param [in] decoder - Decoder context
 *
 *  @return On Success returns true
 *          On Failure returns false
 *
 *  @brief stop the current decoder session
 *  @details As per the state machine of decoder, if session is already started,
 *           to stop the decoder session on device vvas_decoder_submit_frames is
 *           called with nalu as NULL to signify the flush. Followed by
 *           vvas_decoder_destroy to deinit the device and free the decoder
 *           context of this function.
 */
static gboolean
gstvvas_xvideodec_stop (GstVideoDecoder * decoder)
{
  GstVvas_XVideoDec *dec = GST_VVAS_XVIDEODEC (decoder);
  GstVvas_XVideoDecPrivate *priv = dec->priv;

  GST_DEBUG_OBJECT (GST_VVAS_XVIDEODEC (decoder), "stop");

  if (priv->init_done) {
    /* Issue flush to the ongoing decoding */
    vvas_decoder_submit_frames (dec->priv->vvas_dec, NULL, NULL);

    priv->init_done = FALSE;
  }

  g_hash_table_foreach_remove (priv->vf_to_gstbuf_map, free_output_bufs, dec);
  g_hash_table_destroy (priv->vf_to_gstbuf_map);
  gst_clear_object (&priv->allocator);

  if (priv->pool)
    gst_clear_object (&priv->pool);

  priv->pool = NULL;
  priv->allocator = NULL;

  vvas_xvideodec_destroy_context (dec);

#ifdef ENABLE_XRM_SUPPORT
  if (xrmDestroyContext (priv->xrm_ctx) != XRM_SUCCESS)
    GST_ERROR_OBJECT (dec, "failed to destroy XRM context");
#endif

  if (dec->input_state) {
    gst_video_codec_state_unref (dec->input_state);
    dec->input_state = NULL;
  }

  return TRUE;
}

#ifdef ENABLE_XRM_SUPPORT
/** @fn gchar * vvas_xvideodec_prepare_request_json_string (GstVvas_XVideoDec * dec)
 *
 *  @param [in] dec - Decoder context
 *
 *  @return On Success returns valid json string
 *          On Failure returns false

 *  @brief prepares a JSON string to represent a decoder job for XRM
*/
static gchar *
vvas_xvideodec_prepare_request_json_string (GstVvas_XVideoDec * dec)
{
  json_t *req_obj;
  gchar *req_str;
  guint fps_n, fps_d;
  GstVideoInfo vinfo;
  guint in_width, in_height;
  const gchar *mimetype;
  const GstStructure *structure;

  vinfo = dec->input_state->info;

  in_width = GST_VIDEO_INFO_WIDTH (&vinfo);
  in_height = GST_VIDEO_INFO_HEIGHT (&vinfo);

  if (!in_width || !in_height) {
    GST_WARNING_OBJECT (dec, "input width & height not available. returning");
    return FALSE;
  }

  fps_n = GST_VIDEO_INFO_FPS_N (&vinfo);
  fps_d = GST_VIDEO_INFO_FPS_D (&vinfo);

  if (!fps_n) {
    g_warning ("frame rate not available in caps, taking default fps as 60");
    fps_n = 60;
    fps_d = 1;
  }

  structure = gst_caps_get_structure (dec->input_state->caps, 0);
  mimetype = gst_structure_get_name (structure);

  /* Fields used below in creating JSON value are needed by XRM component to
   * manage the instances used
   */
  req_obj = json_pack ("{s:{s:{s:[{s:s,s:s,s:{s:{s:i,s:i,s:{s:i,s:i}}}}]}}}",
      "request", "parameters", "resources", "function",
      "DECODER", "format", strcmp (mimetype,
          "video/x-h264") ? "H265" :
      "H264", "resolution", "input", "width", in_width,
      "height", in_height, "frame-rate", "num", fps_n, "den", fps_d);

  req_str = json_dumps (req_obj, JSON_DECODE_ANY);
  json_decref (req_obj);

  GST_LOG_OBJECT (dec, "prepared xrm request %s", req_str);

  return req_str;
}

/** @fn gboolean vvas_xvideodec_calculate_load (GstVvas_XVideoDec * dec, gint * load)
 *
 *  @param [in] dec - Decoder context
 *  @param [out] load - Calculated XRM load
 *
 *  @return On Success returns true
 *          On Failure returns false

 *  @brief   calculate XRM load for the current session.
 *  @details Based on XRM load calculation using decoder configuration, XRM component decides whether device is capable
 *           to run current session.
*/
static gboolean
vvas_xvideodec_calculate_load (GstVvas_XVideoDec * dec, gint * load)
{
  GstVvas_XVideoDecPrivate *priv = dec->priv;
  int iret = -1, func_id = 0;
  gchar *req_str;
  char plugin_name[XRM_MAX_NAME_LEN];
  xrmPluginFuncParam param;

  if (!priv->xrm_ctx) {
    GST_ERROR_OBJECT (dec, "xrm context not created");
    return FALSE;
  }

  /* prepare json string to request xrm for load */
  req_str = vvas_xvideodec_prepare_request_json_string (dec);
  if (!req_str) {
    GST_ERROR_OBJECT (dec, "failed to prepare xrm json request string");
    return FALSE;
  }

  memset (&param, 0x0, sizeof (xrmPluginFuncParam));
  memset (plugin_name, 0x0, XRM_MAX_NAME_LEN);

  strcpy (plugin_name, "xrmU30DecPlugin");

  if (strlen (req_str) > (XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1)) {
    GST_ERROR_OBJECT (dec,
        "request input string length %lu > max allowed %d",
        strlen (req_str), XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1);
    free (req_str);
    return FALSE;
  }

  strncpy (param.input, req_str, XRM_MAX_PLUGIN_FUNC_PARAM_LEN);
  free (req_str);

  /* execute xrmU30DecPlugin() plugin with JSON sting input to get the decoder
   * load
   */
  iret = xrmExecPluginFunc (priv->xrm_ctx, plugin_name, func_id, &param);
  if (iret != XRM_SUCCESS) {
    GST_ERROR_OBJECT (dec, "failed to get load from xrm plugin. err : %d",
        iret);
    GST_ELEMENT_ERROR (dec, RESOURCE, FAILED,
        ("failed to get load from xrm plugin"), NULL);
    return FALSE;
  }

  *load = atoi ((char *) (strtok (param.output, " ")));

  if (*load <= 0 || *load > XRM_MAX_CU_LOAD_GRANULARITY_1000000) {
    GST_ERROR_OBJECT (dec, "not an allowed decoder load %d", *load);
    GST_ELEMENT_ERROR (dec, RESOURCE, SETTINGS,
        ("wrong decoder load %d", *load), NULL);
    return FALSE;
  }

  GST_INFO_OBJECT (dec, "need %d%% device's load",
      (*load * 100) / XRM_MAX_CU_LOAD_GRANULARITY_1000000);
  return TRUE;
}

/** @fn gboolean vvas_xvideodec_allocate_resource (GstVvas_XVideoDec * dec, gint dec_load)
 *
 *  @param [in] dec - Decoder context
 *  @param [in] dec_load - Decoder load for current session
 *
 *  @return On Success returns true
 *          On Failure returns false

 *  @brief  Allocates hardware and softkernel resources for current session.
*/
static gboolean
vvas_xvideodec_allocate_resource (GstVvas_XVideoDec * dec, gint dec_load)
{
  GstVvas_XVideoDecPrivate *priv = dec->priv;
  int iret = -1;
  size_t num_hard_cus = -1;

  if (getenv ("XRM_RESERVE_ID") || dec->priv->reservation_id) { /* use reservation_id to allocate decoder */
    guint64 xrm_reserve_id = 0;
    xrmCuListPropertyV2 cu_list_prop;
    xrmCuListResourceV2 *cu_list_resource;

    if (!priv->cu_list_res) {
      cu_list_resource =
          (xrmCuListResourceV2 *) calloc (1, sizeof (xrmCuListResourceV2));
      if (!cu_list_resource) {
        GST_ERROR_OBJECT (dec, "failed to allocate memory");
        return FALSE;
      }
    } else {
      cu_list_resource = priv->cu_list_res;
    }

    memset (&cu_list_prop, 0, sizeof (xrmCuListPropertyV2));

    /* element property value takes higher priority than env variable */
    if (dec->priv->reservation_id)
      xrm_reserve_id = dec->priv->reservation_id;
    else
      xrm_reserve_id = atoi (getenv ("XRM_RESERVE_ID"));

    GST_INFO_OBJECT (dec, "going to request %d%% load using xrm with "
        "reservation id %lu", dec_load, xrm_reserve_id);

    cu_list_prop.cuNum = 2;
    strcpy (cu_list_prop.cuProps[0].kernelName, "decoder");
    strcpy (cu_list_prop.cuProps[0].kernelAlias, "DECODER_MPSOC");
    cu_list_prop.cuProps[0].devExcl = false;
    cu_list_prop.cuProps[0].requestLoad =
        XRM_PRECISION_1000000_BIT_MASK (dec_load);
    cu_list_prop.cuProps[0].poolId = xrm_reserve_id;

    strcpy (cu_list_prop.cuProps[1].kernelName, "kernel_vdu_decoder");
    cu_list_prop.cuProps[1].devExcl = false;
    cu_list_prop.cuProps[1].requestLoad =
        XRM_PRECISION_1000000_BIT_MASK (XRM_MAX_CU_LOAD_GRANULARITY_1000000);
    cu_list_prop.cuProps[1].poolId = xrm_reserve_id;

    if (dec->dev_index != -1) {
      uint64_t deviceInfoContraintType =
          XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX;
      uint64_t deviceInfoDeviceIndex = dec->dev_index;

      cu_list_prop.cuProps[0].deviceInfo =
          (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) |
          (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
      cu_list_prop.cuProps[1].deviceInfo =
          (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) |
          (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
    }

    iret = xrmCuListAllocV2 (priv->xrm_ctx, &cu_list_prop, cu_list_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (dec, "failed to do CU list allocation using XRM");
      GST_ELEMENT_ERROR (dec, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from reservation id %lu",
              xrm_reserve_id), NULL);
      return FALSE;
    }

    num_hard_cus =
        vvas_xrt_get_num_compute_units (cu_list_resource->cuResources[0].
        xclbinFileName);

    if (num_hard_cus == -1) {
      GST_ERROR_OBJECT (dec, "failed to get number of cus in xclbin: %s",
          cu_list_resource->cuResources[0].xclbinFileName);
      return FALSE;
    }

    GST_DEBUG_OBJECT (dec,
        "Total Number of Compute Units: %ld in xclbin:%s",
        num_hard_cus, cu_list_resource->cuResources[0].xclbinFileName);

    priv->cu_list_res = cu_list_resource;
    dec->dev_index = cu_list_resource->cuResources[0].deviceId;
    priv->cu_idx = cu_list_resource->cuResources[0].cuId;
#ifdef XLNX_U30_PLATFORM
    dec->sk_cur_idx = cu_list_resource->cuResources[1].cuId - num_hard_cus;

    GST_INFO_OBJECT (dec, "xrm CU list allocation success: dev-idx = %d, "
        "sk-cur-idx = %d and softkernel plugin name %s",
        dec->dev_index, dec->sk_cur_idx,
        priv->cu_list_res->cuResources[0].kernelPluginFileName);
#else
    GST_INFO_OBJECT (dec, "xrm CU list allocation success: dev-idx = %d, "
        "softkernel plugin name %s",
        dec->dev_index, priv->cu_list_res->cuResources[0].kernelPluginFileName);
#endif
    uuid_copy (priv->xclbinId, cu_list_resource->cuResources[0].uuid);


  } else {                      /* use device ID to allocate decoder resource */
    xrmCuProperty cu_hw_prop, cu_sw_prop;
    xrmCuResource *cu_hw_resource, *cu_sw_resource;

    memset (&cu_hw_prop, 0, sizeof (xrmCuProperty));
    memset (&cu_sw_prop, 0, sizeof (xrmCuProperty));

    if (!priv->cu_res[0]) {
      cu_hw_resource = (xrmCuResource *) calloc (1, sizeof (xrmCuResource));
      if (!cu_hw_resource) {
        GST_ERROR_OBJECT (dec, "failed to allocate memory for hardCU resource");
        return FALSE;
      }
    } else {
      cu_hw_resource = priv->cu_res[0];
    }

    if (!priv->cu_res[1]) {
      cu_sw_resource = (xrmCuResource *) calloc (1, sizeof (xrmCuResource));
      if (!cu_sw_resource) {
        GST_ERROR_OBJECT (dec, "failed to allocate memory for softCU resource");
        return FALSE;
      }
    } else {
      cu_sw_resource = priv->cu_res[1];
    }

    GST_INFO_OBJECT (dec, "going to request %d%% load from device %d",
        dec_load, dec->dev_index);

    strcpy (cu_hw_prop.kernelName, "decoder");
    strcpy (cu_hw_prop.kernelAlias, "DECODER_MPSOC");
    cu_hw_prop.devExcl = false;
    cu_hw_prop.requestLoad = XRM_PRECISION_1000000_BIT_MASK (dec_load);

    strcpy (cu_sw_prop.kernelName, "kernel_vdu_decoder");
    cu_sw_prop.devExcl = false;
    cu_sw_prop.requestLoad =
        XRM_PRECISION_1000000_BIT_MASK (XRM_MAX_CU_LOAD_GRANULARITY_1000000);

    /* allocate hardware resource */
    iret = xrmCuAllocFromDev (priv->xrm_ctx, dec->dev_index, &cu_hw_prop,
        cu_hw_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (dec, "failed to do hard CU allocation using XRM");
      GST_ELEMENT_ERROR (dec, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from device id %d",
              dec->dev_index), NULL);
      return FALSE;
    }

    /* allocate softkernel resource */
    iret = xrmCuAllocFromDev (priv->xrm_ctx, dec->dev_index, &cu_sw_prop,
        cu_sw_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (dec, "failed to do soft CU allocation using XRM");
      GST_ELEMENT_ERROR (dec, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from device id %d",
              dec->dev_index), NULL);
      return FALSE;
    }

    num_hard_cus =
        vvas_xrt_get_num_compute_units (cu_hw_resource->xclbinFileName);

    if (num_hard_cus == -1) {
      GST_ERROR_OBJECT (dec, "failed to get number of cus in xclbin: %s",
          cu_hw_resource->xclbinFileName);
      return FALSE;
    }

    GST_DEBUG_OBJECT (dec,
        "Total Number of Compute Units: %ld in xclbin:%s",
        num_hard_cus, cu_hw_resource->xclbinFileName);

    priv->cu_res[0] = cu_hw_resource;
    priv->cu_res[1] = cu_sw_resource;
    dec->dev_index = cu_hw_resource->deviceId;
    priv->cu_idx = cu_hw_resource->cuId;
#ifdef XLNX_U30_PLATFORM
    dec->sk_cur_idx = cu_sw_resource->cuId - num_hard_cus;

    GST_INFO_OBJECT (dec, "xrm CU list allocation success: dev-idx = %d, "
        "cu-idx = %d, sk-cur-idx = %d and softkernel plugin name %s",
        dec->dev_index, priv->cu_idx, dec->sk_cur_idx,
        cu_hw_resource->kernelPluginFileName);
#else
    GST_INFO_OBJECT (dec, "xrm CU list allocation success: dev-idx = %d, "
        "cu-idx = %d, softkernel plugin name %s",
        dec->dev_index, priv->cu_idx, cu_hw_resource->kernelPluginFileName);
#endif
    uuid_copy (priv->xclbinId, cu_hw_resource->uuid);
  }

  return TRUE;
}
#endif

static void
recycle_vframe (GstVvas_XVideoDec * dec)
{
  GstFlowReturn fret = GST_FLOW_OK;
  GstVvas_XVideoDecPrivate *priv = dec->priv;
  guint i = 0;
  /* re-alloc the free'ed buffer */
  do {
    VvasVideoFrame *output_frame = NULL;
    GstBuffer *outbuf = NULL;
    GstBufferPoolAcquireParams params;
    params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;

    /* Keep allocating until there are frames available in the pool */
    fret = gst_buffer_pool_acquire_buffer (priv->pool, &outbuf, &params);
    if (fret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (dec, "Failed to acquire %d-th buffer", i);
      break;
    }

    output_frame = vvas_videoframe_from_gstbuffer (priv->vvas_ctx,
        dec->out_mem_bank, outbuf, &(dec->out_vinfo), GST_MAP_READ);
    if (!output_frame) {
      GST_ERROR_OBJECT (dec, "Could convert input GstBuffer to VvasVideoFrame");
      gst_buffer_unref (outbuf);
      fret = GST_FLOW_ERROR;
      break;
    }

    /* Insert the newly allocated frame and gstbuf into the hash table for
       reverse lookup of gstbuf once vvas-video frame is received from the
       decoder library */
    g_mutex_lock (&priv->obuf_lock);
    g_hash_table_insert (priv->vf_to_gstbuf_map, output_frame, outbuf);
    priv->free_vframe_list
        = g_list_append (priv->free_vframe_list, output_frame);
    g_mutex_unlock (&priv->obuf_lock);
    i++;
  }
  while (fret == GST_FLOW_OK);

  return;
}

/** @fn compose_dec_config(GstVvas_XVideoDec *dec, VvasDecoderInCfg* dec_cfg)
 *
 *  @param [in] dec - Decoder context
 *  @param [in] dec_cfg - Decoder configuration input
 *
 *  @return On Success returns true
 *          On Failure returns false
 *
 *  @brief compose the the decoder configuration
 *  @details parses the capabalities and construct the decoder configuration
 *           required to be set to decode stream.
 */
static bool
compose_dec_config (GstVvas_XVideoDec * dec, VvasDecoderInCfg * dec_cfg)
{
  GstVvas_XVideoDecPrivate *priv = dec->priv;
  const GstStructure *structure;
  const gchar *mimetype;
  GstVideoInfo vinfo;
  const gchar *chroma_format;
  guint bit_depth_luma, bit_depth_chroma;

  structure = gst_caps_get_structure (dec->input_state->caps, 0);
  mimetype = gst_structure_get_name (structure);

  if (!strcmp (mimetype, "video/x-h264")) {
    dec_cfg->codec_type = 0;
    GST_INFO_OBJECT (dec, "input stream is H264");
  } else {
    dec_cfg->codec_type = 1;
    GST_INFO_OBJECT (dec, "input stream is H265");
  }
  dec_cfg->low_latency = dec->low_latency;
  dec_cfg->entropy_buffers_count = dec->num_entropy_bufs;

  if (!dec->input_state || !dec->input_state->caps) {
    GST_ERROR_OBJECT (dec, "Frame resolution not available. Exiting");
    return FALSE;
  }

  vinfo = dec->input_state->info;

  dec_cfg->frame_rate = GST_VIDEO_INFO_FPS_N (&vinfo);
  dec_cfg->clk_ratio = GST_VIDEO_INFO_FPS_D (&vinfo);

#ifdef XLNX_V70_PLATFORM
  if (dec->force_decode_rate) {
    /* use user provided fps, irrespective of i_frame_only flag */
    dec_cfg->frame_rate = dec->force_decode_rate;
    dec_cfg->clk_ratio = 1000;
  } else {
    if (dec->i_frame_only) {
      /* Fixed usecase of max. 25 streams per VDU with each max. fps = 9600/1000 = 9.6 fps.
       * so that, 25 * 9.6 = 240 fps which is max. load a VDU can process. */
      dec_cfg->frame_rate = 9600;
      dec_cfg->clk_ratio = 1000;
    } else {
      /* default parser provided fps */
    }
  }
  GST_INFO_OBJECT (dec,
      "framerate to use : %d clock ratio %d", dec_cfg->frame_rate,
      dec_cfg->clk_ratio);
#endif

  /* assign retry timeout based on resolution & framerate */
  priv->retry_timeout =
      vvas_xvideodec_get_push_command_retry_timeout (GST_VIDEO_INFO_WIDTH
      (&vinfo),
      GST_VIDEO_INFO_HEIGHT (&vinfo), dec_cfg->frame_rate, dec_cfg->clk_ratio);

  GST_INFO_OBJECT (dec,
      "retry timeout for push command %" G_GINT64_FORMAT
      " micro sec", priv->retry_timeout);

  dec_cfg->width = GST_VIDEO_INFO_WIDTH (&vinfo);
  dec_cfg->height = GST_VIDEO_INFO_HEIGHT (&vinfo);
  dec_cfg->level =
      get_level_value (gst_structure_get_string (structure, "level"));
  if (!is_level_supported (dec_cfg->level, dec_cfg->codec_type)) {
    GST_ERROR_OBJECT (dec, "level %s not supported",
        gst_structure_get_string (structure, "level"));
    GST_ELEMENT_ERROR (dec, STREAM, WRONG_TYPE, NULL,
        ("unsupported stream level : %s",
            gst_structure_get_string (structure, "level")));
    return FALSE;
  }

  dec_cfg->profile =
      get_profile_value (gst_structure_get_string (structure, "profile"),
      dec_cfg->codec_type);
  if (!dec_cfg->profile) {
    GST_ERROR_OBJECT (dec, "unsupported stream profile %s",
        gst_structure_get_string (structure, "profile"));
    GST_ELEMENT_ERROR (dec, STREAM, WRONG_TYPE, NULL,
        ("unsupported stream profile : %s",
            gst_structure_get_string (structure, "profile")));
    return FALSE;
  }
  dec_cfg->scan_type = 1;       // progressive
  dec_cfg->splitbuff_mode = dec->splitbuff_mode;        // Enable splitbuff

  chroma_format = gst_structure_get_string (structure, "chroma-format");
  if (structure
      && gst_structure_get_uint (structure, "bit-depth-luma", &bit_depth_luma)
      && gst_structure_get_uint (structure, "bit-depth-chroma",
          &bit_depth_chroma)) {

    dec_cfg->chroma_mode =
        get_color_format_from_chroma (chroma_format, bit_depth_luma,
        bit_depth_chroma);
    if (!dec_cfg->chroma_mode) {
      GST_ERROR_OBJECT (dec, "chroma_mode %s not supported", chroma_format);
      return FALSE;
    }
    dec_cfg->bitdepth = dec->bit_depth = bit_depth_luma;
  }

  GST_INFO_OBJECT (dec, "bitdepth:%d", dec->bit_depth);
  GST_INFO_OBJECT (dec, "frame rate:%d  clock rate:%d", dec_cfg->frame_rate,
      dec_cfg->clk_ratio);

  // Tmp hack
  if (dec_cfg->frame_rate == 0) {
    dec_cfg->frame_rate = 60;
    GST_INFO_OBJECT (dec, "frame rate not received, assuming it to be 60 fps");
  }

  if (dec_cfg->clk_ratio == 0)
    dec_cfg->clk_ratio = 1;

#ifdef XLNX_V70_PLATFORM
  /* decode only I frames, if true */
  dec_cfg->i_frame_only = dec->i_frame_only;
#endif
  return TRUE;
}

/** @fn gboolean vvas_xvideodec_create_context (GstVvas_XVideoDec * dec)
 *
 *  @param [in] dec Decoder - context
 *
 *  @return  On Success returns true
 *           On Failure returns false

 *  @brief   Reserves a CU resource and creates XRT context for the decoder session.
 *  @details CU (compute unit) is the entity on device which handles the host commands. XRM component allocates a CU
 *           based on given load to handle. Without XRM, user should provide the CU index. Apart from that, this
 *           function allocates XRT context for the current decoder session and updates decoder context parameter
 *           'dec' with it.
*/
static gboolean
vvas_xvideodec_create_context (GstVvas_XVideoDec * dec)
{
  VvasReturnType vret;
  VvasDecoderInCfg incfg;
  VvasDecoderOutCfg outcfg;
  GstVvas_XVideoDecPrivate *priv = dec->priv;
  VvasLogLevel core_log_level =
      vvas_get_core_log_level (gst_debug_category_get_threshold
      (gstvvas_xvideodec_debug_category));
#ifdef ENABLE_XRM_SUPPORT
  gboolean bret;

  /* gets cu index & device id (using reservation id) */
  bret = vvas_xvideodec_allocate_resource (dec, priv->cur_load);
  if (!bret)
    return FALSE;

#endif

#ifdef XLNX_U30_PLATFORM
  if (dec->sk_cur_idx < 0) {
    GST_ERROR_OBJECT (dec, "softkernel cu index %d is not valid",
        dec->sk_cur_idx);
    GST_ELEMENT_ERROR (dec, RESOURCE, FAILED, (NULL),
        ("softkernel cu index %d is not valid", dec->sk_cur_idx));
    return FALSE;
  }

  if (dec->sk_cur_idx > DEC_MAX_SK_CU_INDEX) {
    GST_ERROR_OBJECT (dec,
        "softkernel cu index %d is exceeding max allowed:%d value",
        dec->sk_cur_idx, DEC_MAX_SK_CU_INDEX);
    GST_ELEMENT_ERROR (dec, RESOURCE, FAILED, (NULL),
        ("softkernel cu index %d is not valid", dec->sk_cur_idx));
    return FALSE;
  }

  if (!dec->kernel_name)
    dec->kernel_name = g_strdup (VVAS_VIDEODEC_KERNEL_NAME_DEFAULT);
#else
  if (!dec->kernel_name) {
    GST_ERROR_OBJECT (dec, "kernel name is mandatory as arguement. Ex: %s\n",
        VVAS_VIDEODEC_KERNEL_NAME_DEFAULT);
    return FALSE;
  }
#endif

  if (!dec->xclbin_path) {
    GST_ERROR_OBJECT (dec, "XCLBIN path is not set");
    return FALSE;
  }

  if (DEFAULT_DEVICE_INDEX == dec->dev_index) {
    GST_ERROR_OBJECT (dec, "device index is not set");
    return FALSE;
  }

  /* create a vvas context */
  priv->vvas_ctx = vvas_context_create (dec->dev_index, dec->xclbin_path,
      core_log_level, &vret);
  if (vret != VVAS_RET_SUCCESS) {
    GST_ERROR_OBJECT (dec, "Couldn't create VVAS context");
    return FALSE;
  }

  /* Create a decode instance */
  GST_DEBUG_OBJECT (dec, "Creating VVAS Decoder");
#ifdef XLNX_V70_PLATFORM
  priv->vvas_dec = vvas_decoder_create (priv->vvas_ctx,
      (uint8_t *) dec->kernel_name, priv->dec_type,
      dec->hw_instance_id, core_log_level);
#else
  priv->vvas_dec = vvas_decoder_create (priv->vvas_ctx,
      (uint8_t *) dec->kernel_name, priv->dec_type,
      dec->sk_cur_idx, core_log_level);
#endif
  if (!priv->vvas_dec) {
    GST_ERROR_OBJECT (dec, "Couldn't create VVAS Decoder");
    vvas_context_destroy (priv->vvas_ctx);
    priv->vvas_ctx = NULL;
    return FALSE;
  }

  /* Compose the decoder config */
  memset (&incfg, 0, sizeof (incfg));
  compose_dec_config (dec, &incfg);

  /* Configure decoder */
  vret = vvas_decoder_config (priv->vvas_dec, &incfg, &outcfg);
  if (vret != VVAS_RET_SUCCESS) {
    vvas_decoder_destroy (priv->vvas_dec);
    vvas_context_destroy (priv->vvas_ctx);
    priv->vvas_dec = NULL;
    priv->vvas_ctx = NULL;
    GST_ERROR_OBJECT (dec, "vvas_decoder_config Failed vret = %d", vret);
    return FALSE;
  }

  /* outcfg returns the video-frame info and number of such frame needed by
     decoder */
  priv->num_out_bufs = outcfg.min_out_buf;

  return TRUE;
}

/** @fn gboolean vvas_xvideodec_destroy_context (GstVvas_XVideoDec * dec)
 *
 *  @param [in] dec - Decoder context
 *
 *  @return  On Success returns true
 *           On Failure returns false

 *  @brief   Releases the reserved CU and destroys the XRT context
 *  @details Releases the CU reserved during 'vvas_xvideodec_create_context()'. The XRM and XRT context is also destroyed
 *           for the given decoder session.
*/
static gboolean
vvas_xvideodec_destroy_context (GstVvas_XVideoDec * dec)
{
  GstVvas_XVideoDecPrivate *priv = dec->priv;
  gboolean has_error = FALSE;
  VvasReturnType vret;

#ifdef ENABLE_XRM_SUPPORT
  gboolean bret;

  if (priv->cu_list_res) {
    bret = xrmCuListReleaseV2 (priv->xrm_ctx, priv->cu_list_res);
    if (!bret) {
      GST_ERROR_OBJECT (dec, "failed to release resource");
      has_error = TRUE;
    }
    free (priv->cu_list_res);
    priv->cu_list_res = NULL;
  }

  if (priv->cu_res[0]) {
    bret = xrmCuRelease (priv->xrm_ctx, priv->cu_res[0]);
    if (!bret) {
      GST_ERROR_OBJECT (dec, "failed to release hardCU resource");
      has_error = TRUE;
    }

    free (priv->cu_res[0]);
    priv->cu_res[0] = NULL;
  }

  if (priv->cu_res[1]) {
    bret = xrmCuRelease (priv->xrm_ctx, priv->cu_res[1]);
    if (!bret) {
      GST_ERROR_OBJECT (dec, "failed to release softCU resource");
      has_error = TRUE;
    }

    free (priv->cu_res[1]);
    priv->cu_res[1] = NULL;
  }
#endif
  /* Release XRT handle to the device */

  if (priv->vvas_dec) {
    vret = vvas_decoder_destroy (priv->vvas_dec);
    if (vret != VVAS_RET_SUCCESS) {
      GST_ERROR_OBJECT (dec, "failed to destroy vvas-core decoder, vret=%d",
          vret);
      has_error = TRUE;
    } else {
      priv->vvas_dec = NULL;
    }
  }

  if (priv->vvas_ctx) {
    vret = vvas_context_destroy (priv->vvas_ctx);
    if (vret != VVAS_RET_SUCCESS) {
      GST_ERROR_OBJECT (dec, "failed to destroy vvas-core context, vret=%d",
          vret);
      has_error = TRUE;
    } else {
      priv->vvas_ctx = NULL;
    }
  }

  return has_error ? FALSE : TRUE;
}

/** @fn gboolean gstvvas_xvideodec_set_format (GstVideoDecoder * decoder,
 *                                           GstVideoCodecState * state)
 *
 *  @param [in] decoder - Decoder context
 *  @param [in] state - Video stream format to be set
 *
 *  @return  On Success returns
 *           On Failure returns

 *  @brief   Sets the current decoder format and reconfigures it if changed.
 *  @details If the new format is different from current one(if present), all
 *           the buffers are freed, context is destroyed.
 */
static gboolean
gstvvas_xvideodec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstVvas_XVideoDec *dec = GST_VVAS_XVIDEODEC (decoder);
  GstVvas_XVideoDecPrivate *priv = dec->priv;
  const gchar *mimetype;
  gboolean bret = TRUE;
  gboolean do_reconfigure = FALSE;

  GST_DEBUG_OBJECT (dec, "input caps: %" GST_PTR_FORMAT, state->caps);

  if (!dec->input_state ||
      !gst_caps_is_equal (dec->input_state->caps, state->caps))
    do_reconfigure = TRUE;

  if (dec->input_state) {
    gst_video_codec_state_unref (dec->input_state);
    dec->input_state = NULL;
  }

  dec->input_state = gst_video_codec_state_ref (state);

  mimetype =
      gst_structure_get_name (gst_caps_get_structure
      (dec->input_state->caps, 0));

  if (!strcmp (mimetype, "video/x-h264")) {
    priv->dec_type = VVAS_CODEC_H264;
  } else if (!strcmp (mimetype, "video/x-h265")) {
    priv->dec_type = VVAS_CODEC_H265;
  } else {
    GST_ERROR_OBJECT (dec, "mimetype=%s is not supported", mimetype);
    goto error;
  }

  /* Check for "profile" info in the caps for H264 case */
  if (!gst_structure_get_string
      (gst_caps_get_structure (dec->input_state->caps, 0), "profile")) {
    GST_WARNING_OBJECT (dec, "Profile info not present in the caps");
    goto error;
  }

  if (!GST_VIDEO_INFO_FPS_N (&dec->input_state->info)) {
    g_warning ("frame rate not available in caps, taking default fps as 60/1");
    GST_VIDEO_INFO_FPS_N (&dec->input_state->info) = 60;
    GST_VIDEO_INFO_FPS_D (&dec->input_state->info) = 1;
  }

  if (do_reconfigure) {
#ifdef ENABLE_XRM_SUPPORT
    gint load = -1;
#endif

#ifdef ENABLE_XRM_SUPPORT
    bret = vvas_xvideodec_calculate_load (dec, &load);
    if (!bret) {
      goto error;
    }

    if (priv->cur_load != load) {

      priv->cur_load = load;

      /* destroy XRT context as new load received */
      bret = vvas_xvideodec_destroy_context (dec);
      if (!bret) {
        goto error;
      }

      /* create XRT context */
      bret = vvas_xvideodec_create_context (dec);
      if (!bret) {
        goto error;
      }
    }
#else
    if (!priv->vvas_ctx) {
      /* create vvas core context and decoder */
      bret = vvas_xvideodec_create_context (dec);
      if (!bret) {
        goto error;
      }
    }
#endif
  }

  return TRUE;

error:
  gst_video_codec_state_unref (dec->input_state);
  dec->input_state = NULL;
  return FALSE;
}

/** @fn void gstvvas_xvideodec_set_property (GObject * object,
 *                                         guint prop_id,
 *                                         const GValue * value,
 *                                         GParamSpec * pspec)
 *  @param [in] object - Decoder object instance
 *  @param [in] prop_id - Plugin property id
 *  @param [in] value - Value of the property id
 *  @param [in] pspec - Metadata to specify object parameters
 *
 *  @return void
 *
 *  @brief Assigns the property with the value specified as input.
 */
static void
gstvvas_xvideodec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_XVideoDec *dec = GST_VVAS_XVIDEODEC (object);

  switch (prop_id) {
    case PROP_XCLBIN_LOCATION:
      dec->xclbin_path = g_value_dup_string (value);
      break;
#ifndef ENABLE_XRM_SUPPORT
#ifdef XLNX_U30_PLATFORM
    case PROP_SK_CURRENT_INDEX:
      dec->sk_cur_idx = g_value_get_int (value);
      break;
#endif
#endif
    case PROP_NUM_ENTROPY_BUFFER:
      dec->num_entropy_bufs = g_value_get_uint (value);
      break;
    case PROP_LOW_LATENCY:
      dec->low_latency = g_value_get_boolean (value);
      break;
    case PROP_DEVICE_INDEX:
      dec->dev_index = g_value_get_int (value);
      break;
    case PROP_KERNEL_NAME:
      if (dec->kernel_name)
        g_free (dec->kernel_name);

      dec->kernel_name = g_value_dup_string (value);
      break;
    case PROP_AVOID_OUTPUT_COPY:
      dec->avoid_output_copy = g_value_get_boolean (value);
      break;
    case PROP_AVOID_DYNAMIC_ALLOC:
      dec->avoid_dynamic_alloc = g_value_get_boolean (value);
      break;
#ifdef ENABLE_XRM_SUPPORT
    case PROP_RESERVATION_ID:
      dec->priv->reservation_id = g_value_get_uint64 (value);
      break;
#endif
    case PROP_SPLITBUFF_MODE:
      dec->splitbuff_mode = g_value_get_boolean (value);
      break;
    case PROP_INTERPOLATE_TIMESTAMPS:
      dec->interpolate_timestamps = g_value_get_boolean (value);
      break;
    case PROP_ADDITIONAL_OUTPUT_BUFFERS:
      dec->additional_output_buffers = g_value_get_uint (value);
      break;
#ifdef XLNX_V70_PLATFORM
    case PROP_VDU_INSTANCE_ID:
      dec->hw_instance_id = g_value_get_int (value);
      break;
    case PROP_I_FRAME_ONLY:
      dec->i_frame_only = g_value_get_boolean (value);
      break;
    case PROP_FORCE_DECODE_RATE:
      dec->force_decode_rate = g_value_get_uint (value);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/** @fn void gstvvas_xvideodec_get_property(GObject * object,
 *                                        guint prop_id,
 *                                        GValue * value,
 *                                        GParamSpec * pspec)
 *
 *  @param [in]  object - Decoder object instance
 *  @param [in]  prop_id - Plugin property id
 *  @param [out] value - Value of the property id
 *  @param [in]  pspec - Metadata to specify object parameters
 *
 *  @return void
 *
 *  @brieif Gets the value of the given property id.
 */
static void
gstvvas_xvideodec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvas_XVideoDec *dec = GST_VVAS_XVIDEODEC (object);

  switch (prop_id) {
    case PROP_NUM_ENTROPY_BUFFER:
      g_value_set_uint (value, dec->num_entropy_bufs);
      break;
    case PROP_LOW_LATENCY:
      g_value_set_boolean (value, dec->low_latency);
      break;
#ifndef ENABLE_XRM_SUPPORT
#ifdef XLNX_U30_PLATFORM
    case PROP_SK_CURRENT_INDEX:
      g_value_set_int (value, dec->sk_cur_idx);
      break;
#endif
#endif
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, dec->dev_index);
      break;
    case PROP_AVOID_OUTPUT_COPY:
      g_value_set_boolean (value, dec->avoid_output_copy);
      break;
    case PROP_AVOID_DYNAMIC_ALLOC:
      g_value_set_boolean (value, dec->avoid_dynamic_alloc);
      break;
#ifdef ENABLE_XRM_SUPPORT
    case PROP_RESERVATION_ID:
      g_value_set_uint64 (value, dec->priv->reservation_id);
      break;
#endif
    case PROP_SPLITBUFF_MODE:
      g_value_set_boolean (value, dec->splitbuff_mode);
      break;
    case PROP_INTERPOLATE_TIMESTAMPS:
      g_value_set_boolean (value, dec->interpolate_timestamps);
      break;
    case PROP_ADDITIONAL_OUTPUT_BUFFERS:
      g_value_set_uint (value, dec->additional_output_buffers);
      break;
#ifdef XLNX_V70_PLATFORM
    case PROP_VDU_INSTANCE_ID:
      g_value_set_int (value, dec->hw_instance_id);
      break;
    case PROP_I_FRAME_ONLY:
      g_value_set_boolean (value, dec->i_frame_only);
      break;
    case PROP_FORCE_DECODE_RATE:
      g_value_set_uint (value, dec->force_decode_rate);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/** @fn gboolean gstvvas_xvideodec_decide_allocation (GstVideoDecoder * decoder,
                                                    GstQuery * query)
 *
 *  @param [in] decoder - Decoder context
 *  @param [in] query - Object which can be queried to decide allocation
                        strategy.
 *
 *  @return On Success returns true
 *          On Failure returns false
 *
 *  @brief  Decides buffer allocation strategy
 *  @details In a given pipeline, plugins involved should agree on allocation
 *           strategy so that buffers can be accessible in those plugins.
 *           Various factors such as device index, type of allocator (VVAS or
 *           not) influence the allocation. Shared buffer pool paramters were
 *           also calculated and applied in this function.
 */
static gboolean
gstvvas_xvideodec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstVvas_XVideoDec *dec = GST_VVAS_XVIDEODEC (decoder);
  GstCaps *outcaps = NULL;
  GstBufferPool *pool = NULL, *ownpool = NULL;
  guint size, min, max;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstStructure *config = NULL;
  gboolean update_pool, update_allocator;
  GstVideoInfo vinfo;
  GstVideoAlignment align;

  gst_query_parse_allocation (query, &outcaps, NULL);
  gst_video_info_init (&vinfo);
  if (outcaps && !gst_video_info_from_caps (&vinfo, outcaps)) {
    GST_ERROR_OBJECT (dec, "failed to get out video info from caps");
    return FALSE;
  }

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0) {
    /* try the allocator */
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    GST_DEBUG_OBJECT (dec,
        "received allocator %" GST_PTR_FORMAT " from downstream", allocator);
    update_allocator = TRUE;
  } else {
    allocator = NULL;
    gst_allocation_params_init (&params);
    update_allocator = FALSE;
  }

  if (allocator) {
    if (!GST_IS_VVAS_ALLOCATOR (allocator)) {
      gst_object_unref (allocator);
      gst_allocation_params_init (&params);
      allocator = NULL;
    } else if (gst_vvas_allocator_get_device_idx (allocator) != dec->dev_index) {
      GST_INFO_OBJECT (dec,
          "downstream allocator (%d) and decoder (%d) are on different devices",
          gst_vvas_allocator_get_device_idx (allocator), dec->dev_index);
      gst_object_unref (allocator);
      gst_allocation_params_init (&params);
      allocator = NULL;
    }
  } else {
    gst_allocation_params_init (&params);
    allocator = NULL;
  }

  if (!allocator) {
    allocator = gst_vvas_allocator_new (dec->dev_index,
        ENABLE_DMABUF, dec->out_mem_bank);
    params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
    GST_DEBUG_OBJECT (dec, "created vvasallocator %p %" GST_PTR_FORMAT
        " at mem bank: %u", allocator, allocator, dec->out_mem_bank);
  }

  if (dec->priv->allocator)
    gst_object_unref (dec->priv->allocator);

  dec->priv->allocator = allocator;

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    size = MAX (size, vinfo.size);
    if (min == 0)
      min = 2;
    update_pool = TRUE;
    GST_DEBUG_OBJECT (dec,
        "received pool %" GST_PTR_FORMAT " from downstream", pool);
  } else {
    pool = NULL;
    size = vinfo.size;
    min = 2;
    if (dec->avoid_dynamic_alloc) {
      if (dec->additional_output_buffers)
        max = dec->additional_output_buffers;
      else
        max = 4;
    } else {
      max = 0;                  // resets to FRM_BUF_POOL_SIZE - 1 below
    }
    update_pool = FALSE;
  }

  if (pool) {
    if (!GST_IS_VVAS_BUFFER_POOL (pool)) {
      /* create own pool */
      gst_object_unref (pool);
      pool = NULL;
    } else {
      /* set stride alignment & elevation alignement values */
      g_object_set (pool, "stride-align", WIDTH_ALIGN, "elevation-align",
          HEIGHT_ALIGN, NULL);
    }
  }

  if (!pool) {
    pool = gst_vvas_buffer_pool_new (WIDTH_ALIGN, HEIGHT_ALIGN);
    GST_INFO_OBJECT (dec, "created new pool %p %" GST_PTR_FORMAT, pool, pool);
  }

  /* Not always downstream will have requirement of min buffers,
   * lets add min decoder required buffers */
  min = dec->priv->num_out_bufs + min;

  if (max) {
    max = dec->priv->num_out_bufs + max + 1;
    if (max >= FRM_BUF_POOL_SIZE) {
      gst_object_unref (allocator);
      gst_object_unref (pool);
      GST_ERROR_OBJECT (dec, "max pool size cannot be greater than %d",
          FRM_BUF_POOL_SIZE - 1);
      return FALSE;
    }
  } else {
    if (dec->avoid_dynamic_alloc) {
      if (dec->additional_output_buffers) {
        max = dec->priv->num_out_bufs + dec->additional_output_buffers + 1;
        if (min > max) {
          /* Giving preference to number of additional buffers
           * that user has set rather than what downstream elements
           * require. */
          min = max;
        }
      } else {
        max = dec->priv->num_out_bufs + min + 1;
      }
    } else {
      max = FRM_BUF_POOL_SIZE - 1;
    }
  }

  if (min >= FRM_BUF_POOL_SIZE || max >= FRM_BUF_POOL_SIZE) {
    gst_object_unref (allocator);
    gst_object_unref (pool);
    GST_ERROR_OBJECT (dec, "min or max pool size cannot be greater than %d",
        FRM_BUF_POOL_SIZE - 1);
    return FALSE;
  }

  if (min > max) {
    gst_object_unref (allocator);
    gst_object_unref (pool);
    GST_ERROR_OBJECT (dec, "min : %d is greater than max : %d", min, max);
    return FALSE;
  }

  if (dec->avoid_dynamic_alloc)
    dec->priv->num_out_bufs = max;
  else
    dec->priv->num_out_bufs = min;

  config = gst_buffer_pool_get_config (pool);
  size = set_align_param (dec, &vinfo, &align);

  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_video_alignment (config, &align);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  params.flags |= GST_VVAS_ALLOCATOR_FLAG_DONTWAIT;
  gst_buffer_pool_config_set_allocator (config, allocator, &params);

  GST_DEBUG_OBJECT (decoder,
      "setting config %" GST_PTR_FORMAT " in pool %"
      GST_PTR_FORMAT, config, pool);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (dec, "failed to set config on own pool %p", ownpool);
    goto config_failed;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (dec, "failed to activate pool");
    return FALSE;
  }

  if (dec->priv->pool)
    gst_object_unref (dec->priv->pool);

  dec->priv->pool = pool;

  if (!vvas_video_dec_outbuffer_alloc_and_map (dec, &vinfo)) {
    GST_ERROR_OBJECT (dec, "failed to allocate & map output buffers");
    return FALSE;
  }

  /* avoid output frames copy when downstream supports video meta */
  if (!dec->avoid_output_copy) {
    if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
      GST_INFO_OBJECT (dec, "Downstream support GstVideoMeta metadata."
          "Don't copy output decoded frames");
      dec->priv->need_copy = FALSE;
    }
  } else {
    dec->priv->need_copy = FALSE;
    GST_INFO_OBJECT (dec, "Don't copy output decoded frames");
  }

  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  return TRUE;

config_failed:
  if (allocator)
    gst_object_unref (allocator);
  if (config)
    gst_structure_free (config);
  if (pool)
    gst_object_unref (pool);
  GST_ELEMENT_ERROR (decoder, RESOURCE, SETTINGS,
      ("Failed to configure the buffer pool"),
      ("Configuration is most likely invalid, please report this issue."));
  return FALSE;
}

/** @fn GstFlowReturn gstvvas_xvideodec_handle_frame (GstVideoDecoder * decoder,
 *                                                  GstVideoCodecFrame * frame)
 *
 *  @param [in] decoder - Decoder context
 *  @param [in] frame - Input frame with encoded content
 *
 *  @return On Success GST_FLOW_OK
 *          On Failure GST_* error code

 *  @brief   Copies the input compressed frame and pushes it to device.
 */
static GstFlowReturn
gstvvas_xvideodec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstVvas_XVideoDec *dec = GST_VVAS_XVIDEODEC (decoder);
  GstVvas_XVideoDecPrivate *priv = dec->priv;
  GstFlowReturn fret = GST_FLOW_OK;
  gboolean send_again = FALSE;
  GstBuffer *inbuf = NULL;
  VvasMemory *in_mem = NULL;
  VvasVideoFrame *voframe = NULL;
  VvasMetadata in_meta_data = { 0 };
  VvasReturnType vret = VVAS_RET_SUCCESS;

  GST_LOG_OBJECT (dec, "input %" GST_PTR_FORMAT, frame ? frame->input_buffer :
      NULL);
  if (gst_pad_is_active (GST_VIDEO_DECODER_SRC_PAD (dec)) &&
      !gst_pad_has_current_caps (GST_VIDEO_DECODER_SRC_PAD (dec))) {
    GstVideoInfo vinfo;
    GstCaps *outcaps = NULL;
    GstVideoCodecState *out_state = NULL;

    // TODO: add check for resolution change
    vinfo = dec->input_state->info;

    if (dec->bit_depth == 10)
      out_state =
          gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
          GST_VIDEO_FORMAT_NV12_10LE32,
          GST_VIDEO_INFO_WIDTH (&vinfo),
          GST_VIDEO_INFO_HEIGHT (&vinfo), dec->input_state);
    else
      out_state =
          gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
          GST_VIDEO_FORMAT_NV12,
          GST_VIDEO_INFO_WIDTH (&vinfo),
          GST_VIDEO_INFO_HEIGHT (&vinfo), dec->input_state);

    /* In case if one of the parameters is zero, base class will override with
       default default values. To avoid this, we are passing on the same
       incoming colorimetry information to downstream as well.
       Refer https://jira.xilinx.com/browse/CR-1114507 for more information */
    out_state->info.colorimetry.range =
        dec->input_state->info.colorimetry.range;
    out_state->info.colorimetry.matrix =
        dec->input_state->info.colorimetry.matrix;
    out_state->info.colorimetry.transfer =
        dec->input_state->info.colorimetry.transfer;
    out_state->info.colorimetry.primaries =
        dec->input_state->info.colorimetry.primaries;

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (dec))) {
      GST_ERROR_OBJECT (dec, "Failed to negotiate with downstream elements");
      gst_video_codec_state_unref (out_state);
      return GST_FLOW_NOT_NEGOTIATED;
    }
    outcaps = gst_pad_get_current_caps (GST_VIDEO_DECODER_SRC_PAD (dec));
    if (!outcaps || !gst_video_info_from_caps (&dec->out_vinfo, outcaps)) {
      GST_ERROR_OBJECT (dec, "failed to get out video info from caps");
      return GST_FLOW_NOT_NEGOTIATED;
    }
    GST_INFO_OBJECT (dec,
        "negotiated caps on source pad : %" GST_PTR_FORMAT, outcaps);
    gst_video_codec_state_unref (out_state);
    gst_caps_unref (outcaps);
  }

  if (!frame) {
    frame = gst_video_decoder_get_oldest_frame (GST_VIDEO_DECODER (dec));
    if (!frame) {
      /* Can only happen in finish() */
      GST_INFO_OBJECT (dec, "no input frames available...returning EOS");
      fret = GST_FLOW_EOS;
      goto exit;
    }
  }

  inbuf = gst_buffer_ref (frame->input_buffer);
  gst_video_codec_frame_unref (frame);
  frame = NULL;

  in_mem = vvas_memory_from_gstbuffer (priv->vvas_ctx, dec->in_mem_bank, inbuf);
  if (!in_mem) {
    GST_ERROR_OBJECT (dec, "Failed to convert the input GstBuf to VvasMemory");
    fret = GST_FLOW_ERROR;
    goto exit;
  }

  /* Extract pts and set that as meta-data in the VvasMemory */
  in_meta_data.pts = inbuf ? GST_BUFFER_PTS (inbuf) : -1;
  vvas_memory_set_metadata (in_mem, &in_meta_data);

try_again:
  /* Sumbit the encoded frame to decoder */
  vret = vvas_decoder_submit_frames (priv->vvas_dec, in_mem,
      (VvasList *) priv->free_vframe_list);
  if (vret == VVAS_RET_SEND_AGAIN) {
    /* Decoder didn't consume the encoded frame, may be bacause there are no
       room for output buffer. encoded frames required to be sent again once
       frames are free'ed up */
    send_again = TRUE;
  } else if (vret != VVAS_RET_SUCCESS) {
    GST_ERROR_OBJECT (dec, "submit_frames() failed vret=%d", vret);
    fret = GST_FLOW_ERROR;
    goto exit;
  }

  dec->priv->init_done = TRUE;

  g_mutex_lock (&priv->obuf_lock);
  g_list_free (priv->free_vframe_list);
  priv->free_vframe_list = NULL;
  g_mutex_unlock (&priv->obuf_lock);

  /* Get the decoded frame from decoder, one frame at a time  */
  vret = vvas_decoder_get_decoded_frame (priv->vvas_dec, &voframe);
  if (vret == VVAS_RET_SUCCESS) {
    fret = receive_out_frame (dec, voframe);
  } else if (vret == VVAS_RET_ERROR) {
    GST_ERROR_OBJECT (dec, "get_decoded_frame failed inside handle_frame\n");
    goto exit;
  }

  /* Logic to handle the send again, either collect free frames or wait */
  if (send_again == TRUE) {
    guint num_free_obuf = 0;

    recycle_vframe (dec);

    /* Reset the status as above loop might have set it */
    fret = GST_FLOW_OK;

    g_mutex_lock (&dec->priv->obuf_lock);
    num_free_obuf = g_list_length (priv->free_vframe_list);

    if (num_free_obuf) {
      /* send again may get success when free outbufs available */
      GST_LOG_OBJECT (dec, "send free output buffers %d back to decoder",
          num_free_obuf);
      send_again = FALSE;
      g_mutex_unlock (&dec->priv->obuf_lock);
      goto try_again;
    } else {
      gint64 end_time = g_get_monotonic_time () + priv->retry_timeout;
      if (!g_cond_wait_until (&priv->obuf_cond, &priv->obuf_lock, end_time)) {
        GST_LOG_OBJECT (dec, "timeout occured, try PUSH command again");
      } else {
        GST_LOG_OBJECT (dec, "received free output buffer(s), "
            "send PUSH command again");
      }
      send_again = FALSE;
      g_mutex_unlock (&dec->priv->obuf_lock);
      goto try_again;
    }
  }
exit:
  if (in_mem)
    vvas_memory_free (in_mem);

  if (inbuf)
    gst_buffer_unref (inbuf);
  return fret;
}

/** @fn gboolean gstvvas_xvideodec_flush (GstVideoDecoder * decoder)
 *
 *  @param [in] decoder - Decoder context
 *
 *  @return On Success returns true
 *          On Failure returns false
 *
 *  @brief Sends flush to decoder command
 */
static gboolean
gstvvas_xvideodec_flush (GstVideoDecoder * decoder)
{
  GstVvas_XVideoDec *dec = GST_VVAS_XVIDEODEC (decoder);
  VvasReturnType vret = VVAS_RET_SUCCESS;
  GST_ERROR_OBJECT (dec, "gstvvas_xvideodec_flush Called");

  /* Invoke decoder flush */
  vret = vvas_decoder_submit_frames (dec->priv->vvas_dec, NULL, NULL);
  if (vret != VVAS_RET_SUCCESS) {
    GST_ERROR_OBJECT (dec, "Failed to FLUSH vret=%d", vret);
  }
  return TRUE;
}

/** @fn GstFlowReturn gstvvas_xvideodec_finish (GstVideoDecoder * decoder)
 *
 *  @param [in] decoder - Decoder context
 *
 *  @return On Success or if initialization is not done, returns GST_FLOW_OK
 *          On Failure returns GST_* error code
 *
 *  @brief  Sends flush command and receives all the remaining output buffers
 */
static GstFlowReturn
gstvvas_xvideodec_finish (GstVideoDecoder * decoder)
{
  GstVvas_XVideoDec *dec = GST_VVAS_XVIDEODEC (decoder);
  VvasReturnType vret;
  VvasVideoFrame *voframe = NULL;
  guint num_free_obuf = 0;
  GstVvas_XVideoDecPrivate *priv = dec->priv;


  GST_DEBUG_OBJECT (dec, "finish");

  if (!priv->init_done)
    return GST_FLOW_OK;

  /* Consume all the available output frame available */
  do {
    vret = vvas_decoder_get_decoded_frame (priv->vvas_dec, &voframe);
    if (vret == VVAS_RET_SUCCESS) {
      if (receive_out_frame (dec, voframe) != GST_FLOW_OK)
        goto error;
    } else if (vret == VVAS_RET_ERROR) {
      GST_ERROR_OBJECT (dec, "get_decoded_frame failed inside finish\n");
      goto error;
    }
  }
  while (vret != VVAS_RET_NEED_MOREDATA);

  recycle_vframe (dec);

  g_mutex_lock (&priv->obuf_lock);
  num_free_obuf = g_list_length (priv->free_vframe_list);
  g_mutex_unlock (&priv->obuf_lock);

  /* Invoke decoder flush */
  vret = vvas_decoder_submit_frames (priv->vvas_dec, NULL,
      (VvasList *) priv->free_vframe_list);
  if (vret != VVAS_RET_SUCCESS) {
    GST_ERROR_OBJECT (dec, "submit_frames/FLUSH Failed vret = %d", vret);
    goto error;
  }

  g_mutex_lock (&priv->obuf_lock);
  g_list_free (priv->free_vframe_list);
  priv->free_vframe_list = NULL;
  g_mutex_unlock (&priv->obuf_lock);

  /* Collect all the output video-frame post flush call untill output EOS
     stream is received */
  do {
    vret = vvas_decoder_get_decoded_frame (priv->vvas_dec, &voframe);
    if (vret == VVAS_RET_SUCCESS) {
      if (receive_out_frame (dec, voframe) != GST_FLOW_OK)
        goto error;
    } else if (vret == VVAS_RET_ERROR) {
      GST_ERROR_OBJECT (dec, "get_decoded_frame failed inside finish loop\n");
      goto error;
    }

    recycle_vframe (dec);

    g_mutex_lock (&priv->obuf_lock);
    num_free_obuf = g_list_length (priv->free_vframe_list);
    g_mutex_unlock (&priv->obuf_lock);

    if (num_free_obuf) {
      vvas_decoder_submit_frames (priv->vvas_dec, NULL,
          (VvasList *) priv->free_vframe_list);
      g_mutex_lock (&priv->obuf_lock);
      g_list_free (priv->free_vframe_list);
      priv->free_vframe_list = NULL;
      g_mutex_unlock (&priv->obuf_lock);
    }
  }
  while (vret != VVAS_RET_EOS);

  return GST_FLOW_OK;

error:
  return GST_FLOW_ERROR;
}

/** @fn gboolean gstvvas_xvideodec_src_event_default (GstVideoDecoder * decoder, GstEvent * event)
 *
 *  @param [in] decoder	- Decoder instance.
 *  @param [in] event	- The GstEvent to handle.
 *
 *  @return On Success returns TRUE
 *          On Failure returns FALSE
 *
 *  @brief  Handles GstEvent coming over the src pad. Ex : SEEK,CAPS,RECONFIGURE etc.,
 *  @details  This function is a callback function for any new event coming on the src pad.
 */

static gboolean
gstvvas_xvideodec_src_event_default (GstVideoDecoder * decoder,
    GstEvent * event)
{
  GST_DEBUG_OBJECT (decoder,
      "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_RECONFIGURE:
      GST_INFO_OBJECT (decoder, "dropping reconfigure event");
      GST_OBJECT_FLAG_UNSET (decoder->srcpad, GST_PAD_FLAG_NEED_RECONFIGURE);
      gst_event_unref (event);
      return TRUE;
    default:
      return GST_VIDEO_DECODER_CLASS (parent_class)->src_event (decoder, event);
  }
  return TRUE;
}

/** @fn void gstvvas_xvideodec_finalize (GObject * object)
 *
 *  @param [in] object - Decoder object instance
 *
 *  @return void
 *
 *  @brief  Frees decoder private data members
 */
static void
gstvvas_xvideodec_finalize (GObject * object)
{
  GstVvas_XVideoDec *dec = GST_VVAS_XVIDEODEC (object);
  GstVvas_XVideoDecPrivate *priv = dec->priv;
  if (dec->xclbin_path)
    g_free (dec->xclbin_path);

  g_cond_clear (&priv->obuf_cond);
  g_mutex_clear (&priv->obuf_lock);

  if (dec->input_state) {
    gst_video_codec_state_unref (dec->input_state);
    dec->input_state = NULL;
  }
  if (dec->kernel_name) {
    g_free (dec->kernel_name);
    dec->kernel_name = NULL;
  }
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/** @fn void gstvvas_xvideodec_class_init (GstVvas_XVideoDecClass * klass)
 *
 *  @param [out] klass - Gstreamer decoder class object
 *
 *  @return void
 *
 *  @brief  Add properties and signals of GstVvas_XVideoDec to parent GObjectClass
 *          and ovverrides function pointers present in itself and/or its parent
 *          class structures.
 *  @details This function publishes properties those can be set/get from
 *           application on GstVvas_XVideoDec object. And, while publishing a
 *           property it also declares type, range of acceptable values, default
 *           value, readability/writability and in which GStreamer state a
 *           property can be changed.
 */
static void
gstvvas_xvideodec_class_init (GstVvas_XVideoDecClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstVideoDecoderClass *dec_class = GST_VIDEO_DECODER_CLASS (klass);
  char str[200] = { 0 };

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &sink_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &src_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Xilinx Video H264/H265 decoder",
      "Decoder/Video",
      "Xilinx H264/H265 Decoder", "Xilinx Inc., https://www.xilinx.com");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gstvvas_xvideodec_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gstvvas_xvideodec_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gstvvas_xvideodec_finalize);
  dec_class->start = GST_DEBUG_FUNCPTR (gstvvas_xvideodec_start);
  dec_class->stop = GST_DEBUG_FUNCPTR (gstvvas_xvideodec_stop);
  dec_class->set_format = GST_DEBUG_FUNCPTR (gstvvas_xvideodec_set_format);
  dec_class->finish = GST_DEBUG_FUNCPTR (gstvvas_xvideodec_finish);
  dec_class->flush = GST_DEBUG_FUNCPTR (gstvvas_xvideodec_flush);
  dec_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gstvvas_xvideodec_decide_allocation);
  dec_class->handle_frame = GST_DEBUG_FUNCPTR (gstvvas_xvideodec_handle_frame);
  dec_class->src_event =
      GST_DEBUG_FUNCPTR (gstvvas_xvideodec_src_event_default);

  g_object_class_install_property (gobject_class, PROP_NUM_ENTROPY_BUFFER,
      g_param_spec_uint ("num-entropy-buf",
          "Number of entropy buffers",
          "Number of entropy buffers",
          2, 10, 2, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOW_LATENCY,
      g_param_spec_boolean ("low-latency",
          "Low latency enabled or not",
          "Whether to enable low latency or not",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_XCLBIN_LOCATION,
      g_param_spec_string ("xclbin-location",
          "xclbin file location",
          "Location of the xclbin to program device",
          NULL,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

#ifndef ENABLE_XRM_SUPPORT
#ifdef XLNX_U30_PLATFORM
  g_object_class_install_property (gobject_class, PROP_SK_CURRENT_INDEX,
      g_param_spec_int ("sk-cur-idx",
          "Current softkernel index",
          "Current softkernel index",
          -1, 31,
          DEFAULT_SK_CURRENT_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif
#endif

  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("dev-idx",
          "Device index",
          "Valid Device index is 0 to 31. Default value is set to -1 "
          "intentionally so that user provides the correct device index.",
          -1, 31,
          DEFAULT_DEVICE_INDEX, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_KERNEL_NAME,
      g_param_spec_string ("kernel-name",
          "Video Decoder kernel name",
          "Video Decoder kernel name",
          VVAS_VIDEODEC_KERNEL_NAME_DEFAULT,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_AVOID_OUTPUT_COPY,
      g_param_spec_boolean ("avoid-output-copy",
          "Avoid output frames copy",
          "Avoid output frames copy even when downstream does not support "
          "GstVideoMeta metadata",
          VVAS_VIDEODEC_AVOID_OUTPUT_COPY_DEFAULT,
          G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  /* Fixed number of output buffers will be pre-allocated if this is TRUE */
  g_object_class_install_property (gobject_class, PROP_AVOID_DYNAMIC_ALLOC,
      g_param_spec_boolean
      ("avoid-dynamic-alloc",
          "Avoid dynamic allocation",
          "Avoid dynamic allocation of output buffers",
          TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
          | GST_PARAM_MUTABLE_READY));

#ifdef ENABLE_XRM_SUPPORT
  g_object_class_install_property (gobject_class, PROP_RESERVATION_ID,
      g_param_spec_uint64 ("reservation-id",
          "XRM reservation id",
          "Resource Pool Reservation id",
          0, G_MAXUINT64, 0,
          G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
#endif

  g_object_class_install_property (gobject_class, PROP_SPLITBUFF_MODE,
      g_param_spec_boolean ("splitbuff-mode",
          "Whether to enable splitbuff mode or not",
          "Whether to enable splitbuff mode or not",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_INTERPOLATE_TIMESTAMPS,
      g_param_spec_boolean
      ("interpolate-timestamps",
          "Interpolate timestamps",
          "Interpolate PTS of output buffers",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  snprintf (str, 200,
      "Based on pipeline requirement, use this property to allocate additional decoder output buffers. Decoder can allocate maximum %d output buffers including its buffer requirements.",
      FRM_BUF_POOL_SIZE);

  g_object_class_install_property (gobject_class,
      PROP_ADDITIONAL_OUTPUT_BUFFERS,
      g_param_spec_uint ("additional-output-buffers",
          "Additional Pipeline buffer requirement",
          str, 0, FRM_BUF_POOL_SIZE - 1, 0,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY));

#ifdef XLNX_V70_PLATFORM
  g_object_class_install_property (gobject_class, PROP_VDU_INSTANCE_ID,
      g_param_spec_int ("instance-id", "VDU HW instance Id",
          "VDU instance Id selected by user",
          0, (MAX_VDU_HW_INSTANCES - 1), DEFAULT_VDU_INSTANCE_ID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject_class, PROP_I_FRAME_ONLY,
      g_param_spec_boolean ("i-frame-only", "I-frame only",
          "Enable I-frame decode only",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject_class, PROP_FORCE_DECODE_RATE,
      g_param_spec_uint ("force-decode-rate", "decoding rate for current input",
          "Multiply the desired framerate by 1000 and set the parameter as integer. Ex: if user wants to set the frame rate = 29.97 then She/He should specify 29.97*1000 = 29970. This parameter will influence how many hardware resources are allocated for this stream decoding. Decoding may happen at a rate different than specified here as decoding rate is dependent on the allocated hardware resources. It is highly recommended to let the underlying decoder logic to decide the hardware allocation strategy based on the encoded parameters. For example if the stream is encoded @60 fps and if user has specified force-decode-rate=30, then the hardware resources required to decode this stream @30 fps may be less than the resources required for decoding @60 fps and the same hardware can decode more such streams.",
          0, 60000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
#endif
  GST_DEBUG_CATEGORY_INIT (gstvvas_xvideodec_debug_category, "vvas_xvideodec",
      0, "debug category for video h264/h265 decoder element");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
}

/** @fn void gstvvas_xvideodec_init (GstVvas_XVideoDec * dec)
 *
 *  @param [out] dec - Decoder context
 *
 *  @return void
 *
 *  @brief Initializes decoder context with default values.
 */
static void
gstvvas_xvideodec_init (GstVvas_XVideoDec * dec)
{
  GstVvas_XVideoDecPrivate *priv = GST_VVAS_XVIDEODEC_PRIVATE (dec);
  dec->priv = priv;
  dec->bit_depth = 8;
  dec->num_entropy_bufs = 2;
  dec->dev_index = DEFAULT_DEVICE_INDEX;
#ifdef XLNX_U30_PLATFORM
  dec->sk_cur_idx = DEFAULT_SK_CURRENT_INDEX;
#endif
  dec->xclbin_path = NULL;
  dec->kernel_name = g_strdup (VVAS_VIDEODEC_KERNEL_NAME_DEFAULT);
  dec->avoid_output_copy = VVAS_VIDEODEC_AVOID_OUTPUT_COPY_DEFAULT;
  dec->input_state = NULL;
  dec->splitbuff_mode = FALSE;
  dec->avoid_dynamic_alloc = TRUE;
  dec->in_mem_bank = DEFAULT_MEM_BANK;
  dec->out_mem_bank = DEFAULT_MEM_BANK;
  dec->interpolate_timestamps = FALSE;
  dec->additional_output_buffers = 0;
  priv->allocator = NULL;
  g_mutex_init (&priv->obuf_lock);
  g_cond_init (&priv->obuf_cond);
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (dec), TRUE);
  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (dec), TRUE);
#ifdef ENABLE_XRM_SUPPORT
  priv->reservation_id = 0;
#endif
#ifdef XLNX_V70_PLATFORM
  dec->hw_instance_id = DEFAULT_VDU_INSTANCE_ID;
  dec->i_frame_only = FALSE;
  dec->force_decode_rate = 0;
#endif
  /* Initialize the vvas core objects */
  priv->vvas_ctx = NULL;
  priv->vvas_dec = NULL;
  priv->vf_to_gstbuf_map = NULL;
  priv->free_vframe_list = NULL;
  priv->dec_type = VVAS_CODEC_UNKNOWN;
  vvas_xvideodec_reset (dec);
}

#ifndef PACKAGE
#define PACKAGE "vvas_xvideodec"
#endif

/**
 *  @fn static gboolean video_dec_init (GstPlugin * plugin)
 *
 *  @param [in] plugin - Handle to vvas_xvideodec plugin
 *
 *  @return TRUE if plugin initialized successfully
 *
 *  @brief This is a callback function that will be called by the loader at
 *         startup to register the plugin
 *
 *  @note It create a new element factory capable of instantiating objects of
 *        the type 'GST_TYPE_VVAS_XVIDEODEC' and add the factory to plugin
 *        'vvas_xvideodec'
 */
static gboolean
video_dec_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "vvas_xvideodec", GST_RANK_PRIMARY,
      GST_TYPE_VVAS_XVIDEODEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xvideodec,
    "Xilinx VIDEO H264/H264 Decoder plugin", video_dec_init,
    VVAS_API_VERSION, GST_LICENSE_UNKNOWN, "GStreamer Xilinx",
    "http://xilinx.com/")
