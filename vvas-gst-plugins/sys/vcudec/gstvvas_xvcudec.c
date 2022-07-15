/*
 * Copyright 2020-2022 Xilinx, Inc.
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

#include "gstvvas_xvcudec.h"
#include <gst/vvas/gstvvasbufferpool.h>
#include <gst/vvas/gstvvasallocator.h>
#ifdef HDR_DATA_SUPPORT
#include <gst/vvas/mpsoc_vcu_hdr.h>
#endif
#include <gst/vvas/gstvvashdrmeta.h>
#include <vvas/xrt_utils.h>
#include <experimental/xrt-next.h>

#ifdef ENABLE_XRM_SUPPORT
#include <xrm.h>
#include <xrm_limits.h>
#include <dlfcn.h>
#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))
#endif

GST_DEBUG_CATEGORY_STATIC (gstvvas_xvcudec_debug_category);
#define GST_CAT_DEFAULT gstvvas_xvcudec_debug_category

#define DEFAULT_DEVICE_INDEX -1
#define DEFAULT_SK_CURRENT_INDEX -1
#define VVAS_VCUDEC_AVOID_OUTPUT_COPY_DEFAULT FALSE
/* Maximum vcu dec softkernel cu indexes supported in xclbin */
#define DEC_MAX_SK_CU_INDEX 31
static const int ERT_CMD_SIZE = 4096;
#define ERT_CMD_DATA_LEN 1024
#define CMD_EXEC_TIMEOUT 1000   // 1 sec
#define MIN_POOL_BUFFERS 1
#define MAX_IBUFFS 2

#define ENABLE_DMABUF 0
#define WIDTH_ALIGN 256
#define HEIGHT_ALIGN 64
#define ALIGN(size,align) (((size) + (align) - 1) & ~((align) - 1))

//////////////// FROM VCU XMA Plugin START //////////////////////
/* FRM_BUF_POOL_SIZE value comes from vcu control software which is 50 as of 2019.2 release.
   This  value is used in corresponding decoder softkernel
*/
#define FRM_BUF_POOL_SIZE 50
#define MAX_ERR_STRING 1024

#define AVC_PROFILE_IDC_BASELINE 66
#define AVC_PROFILE_IDC_CONSTRAINED_BASELINE (AVC_PROFILE_IDC_BASELINE | (1<<9))
#define AVC_PROFILE_IDC_MAIN     77
#define AVC_PROFILE_IDC_HIGH     100
#define AVC_PROFILE_IDC_HIGH10   110
#define AVC_PROFILE_IDC_HIGH10_INTRA (AVC_PROFILE_IDC_HIGH10 | (1<<11))
#define HEVC_PROFILE_IDC_MAIN    1
#define HEVC_PROFILE_IDC_MAIN10  2
#define HEVC_PROFILE_IDC_RExt 4

enum cmd_type
{
  VCU_PREINIT = 0,
  VCU_INIT,
  VCU_PUSH,
  VCU_RECEIVE,
  VCU_FLUSH,
  VCU_DEINIT,
};

typedef struct dec_params
{
  uint32_t bitdepth;
  uint32_t codec_type;
  uint32_t low_latency;
  uint32_t entropy_buffers_count;
  uint32_t frame_rate;
  uint32_t clk_ratio;
  uint32_t profile;
  uint32_t level;
  uint32_t height;
  uint32_t width;
  uint32_t chroma_mode;
  uint32_t scan_type;
  uint32_t splitbuff_mode;
} dec_params_t;

typedef struct _vcu_dec_in_usermeta
{
  int64_t pts;
} vcu_dec_in_usermeta;

typedef struct _vcu_dec_out_usermeta
{
  int64_t pts;
#ifdef HDR_DATA_SUPPORT
  bool is_hdr_present;
#endif
} vcu_dec_out_usermeta;

typedef struct _out_buf_info
{
  uint64_t freed_obuf_paddr;
  size_t freed_obuf_size;
  uint32_t freed_obuf_index;
} out_buf_info;

typedef struct host_dev_data
{
  uint32_t cmd_id;
  uint32_t cmd_rsp;
  uint32_t obuff_size;
  uint32_t obuff_num;
  uint32_t obuff_index[FRM_BUF_POOL_SIZE];
  uint32_t ibuff_valid_size;
  uint32_t host_to_dev_ibuf_idx;
  uint32_t dev_to_host_ibuf_idx;
  bool last_ibuf_copied;
  bool resolution_found;
  vcu_dec_in_usermeta ibuff_meta;
  vcu_dec_out_usermeta obuff_meta[FRM_BUF_POOL_SIZE];
  bool end_decoding;
  uint32_t free_index_cnt;
  int valid_oidxs;
  out_buf_info obuf_info[FRM_BUF_POOL_SIZE];
#ifdef HDR_DATA_SUPPORT
  out_buf_info hdrbuf_info[FRM_BUF_POOL_SIZE];
#endif
  char dev_err[MAX_ERR_STRING];
} sk_payload_data;

//////////////// FROM VCU XMA Plugin END //////////////////////

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/x-h264, stream-format=(string)byte-stream, alignment=(string)au;"
        "video/x-h265, stream-format=(string)byte-stream, alignment=(string)au;"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ NV12, NV12_10LE32 }"))
    );

typedef struct _xlnx_output_buf
{
  guint idx;
  xrt_buffer xrt_buf;
  GstBuffer *gstbuf;            /* weak reference for pointer comparison */
} XlnxOutputBuffer;

struct _GstVvas_XVCUDecPrivate
{
  GstBufferPool *downstream_pool;
  gboolean output_xrt_mime;
  guint num_out_bufs;
  gsize out_buf_size;
  GstBuffer **out_bufs_arr;
  vvasDeviceHandle dev_handle;
  vvasKernelHandle kern_handle;
  uuid_t xclbinId;
  gint cu_idx;
  xrt_buffer *ert_cmd_buf;
  xrt_buffer *sk_payload_buf;
  xrt_buffer *in_xrt_bufs[MAX_IBUFFS];  /* input encoded stream will be copied to this */
  xrt_buffer *dec_cfg_buf;
  gboolean outbufs_allocated;
  GHashTable *oidx_hash;
  xrt_buffer *dec_out_bufs_handle;
#ifdef HDR_DATA_SUPPORT
  xrt_buffer *hdr_out_bufs_handle;
  xrt_buffer *hdr_bufs_arr;
#endif
  GList *free_oidx_list;
  GList *pre_free_oidx_list;
  gboolean init_done;
  gboolean flush_done;          /* to make sure FLUSH cmd issued to softkernel while exiting */
  gboolean deinit_done;
  GstBufferPool *pool;
  guint max_ibuf_size;
  uint32_t host_to_dev_ibuf_idx;
  GMutex obuf_lock;
  GMutex pre_obuf_lock;
  GCond obuf_cond;
  gulong mem_released_handler;
  GstAllocator *allocator;
  gboolean need_copy;
  GHashTable *out_buf_hash;
  sk_payload_data last_rcvd_payload;
  guint last_rcvd_oidx;
  uint64_t timestamp;           /* get current time when sending PREINIT command */
  gboolean has_error;
  gboolean allocated_intr_bufs;
  gint64 retry_timeout;
  gint min_skbuf_count;         /* minimum output buffers required by soft kernel */
  gint cur_skbuf_count;         /* current output buffers held by soft kernel */
#ifdef ENABLE_XRM_SUPPORT
  xrmContext xrm_ctx;
  xrmCuListResourceV2 *cu_list_res;
  xrmCuResource *cu_res[2];
  gint cur_load;
  uint64_t reservation_id;
#endif
  xclDeviceHandle *xcl_dev_handle;
  GstClockTime last_pts;
  gint genpts;
  gboolean xcl_ctx_valid;
};
#define gstvvas_xvcudec_parent_class parent_class

G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_XVCUDec, gstvvas_xvcudec,
    GST_TYPE_VIDEO_DECODER);
#define GST_VVAS_XVCUDEC_PRIVATE(dec) (GstVvas_XVCUDecPrivate *) (gstvvas_xvcudec_get_instance_private (dec))
static void vvas_vcu_dec_pre_release_buffer_cb (GstBuffer * buf,
    gpointer user_data);
static void
vvas_vcu_dec_post_release_buffer_cb (GstBuffer * outbuf, gpointer user_data);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

static gboolean vvas_xvcudec_destroy_context (GstVvas_XVCUDec * dec);

/* Properties */
enum
{
  PROP_0,
  PROP_XCLBIN_LOCATION,
  PROP_SK_NAME,
  PROP_SK_LIB_PATH,
  PROP_NUM_ENTROPY_BUFFER,
  PROP_LOW_LATENCY,
  PROP_SK_START_INDEX,
  PROP_SK_CURRENT_INDEX,
  PROP_DEVICE_INDEX,
  PROP_KERNEL_NAME,
  PROP_AVOID_OUTPUT_COPY,
#ifdef ENABLE_XRM_SUPPORT
  PROP_RESERVATION_ID,
#endif
  PROP_SPLITBUFF_MODE,
  PROP_AVOID_DYNAMIC_ALLOC,
  PROP_DISABLE_HDR_SEI,
  PROP_IN_MEM_BANK,
  PROP_OUT_MEM_BANK,
  PROP_INTERPOLATE_TIMESTAMPS,
};

#define VVAS_VCUDEC_KERNEL_NAME_DEFAULT "decoder:{decoder_1}"

static guint
set_align_param (GstVvas_XVCUDec * dec, GstVideoInfo * info,
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

  align->padding_top = 0;
  align->padding_left = 0;
  align->padding_bottom = ALIGN (height, HEIGHT_ALIGN) - height;
  align->padding_right = ALIGN (width, WIDTH_ALIGN) - width;

  if (GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_NV12_10LE32
      || dec->bit_depth == 10) {
    stride = DIV_AND_ROUND_UP (width * 4, 3);
    //align->padding_right = ALIGN (stride, WIDTH_ALIGN) - stride;
    stride = ALIGN (stride, WIDTH_ALIGN);
    //align->padding_right *= 0.75;
    align->padding_right = 0;

  } else
    stride = ALIGN (width, WIDTH_ALIGN);

  GST_LOG_OBJECT (dec, "fmt = %d width = %d height = %d",
      GST_VIDEO_INFO_FORMAT (info), width, height);
  GST_LOG_OBJECT (dec, "align top %d bottom %d right %d left =%d",
      align->padding_top, align->padding_bottom, align->padding_right,
      align->padding_left);
  GST_LOG_OBJECT (dec, "size = %d", (guint) ((stride * ALIGN (height,
                  HEIGHT_ALIGN)) * 1.5));

  return (stride * ALIGN (height, HEIGHT_ALIGN)) * 1.5;
}

#if 0
static GType
gstvvas_xvcudec_bit_depth_type (void)
{
  static GType depth = 0;

  if (!depth) {
    static const GEnumValue depths[] = {
      {8, "XLNX_VCU_DEC_BITDEPTH_8", "8"},
      {10, "XLNX_VCU_DEC_BITDEPTH_10", "10"},
      {0, NULL, NULL}
    };
    depth = g_enum_register_static ("GstVvas_XVCUDecBitDepth", depths);
  }
  return depth;
}
#endif

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

static guint
get_level_value (const gchar * level)
{
  /* use higher level (6.2) if not specified (invalid stream) so we can decode it */
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

static gboolean
is_level_supported (guint level, guint codec_type)
{
  if (codec_type == 0) {
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
  } else {
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

static gint64
vvas_xvcudec_get_push_command_retry_timeout (guint width, guint height,
    guint fps_n, guint fps_d)
{
  guint pixel_rate = (width * height * fps_n) / fps_d;
  guint max_pixel_rate = 3840 * 2160 * 60;      // 4K @60
  gint64 max_timeout_ms = 15 * G_TIME_SPAN_MILLISECOND; // 15 milli seconds

  return max_timeout_ms / (max_pixel_rate / pixel_rate);
}

static gboolean
vvas_free_output_hash_value (gpointer key, gpointer value, gpointer user_data)
{
  gst_buffer_unref (value);
  return TRUE;
}

static gboolean
vvas_xvcudec_check_softkernel_response (GstVvas_XVCUDec * dec,
    sk_payload_data * payload_buf)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  int iret;

  memset (payload_buf, 0, priv->sk_payload_buf->size);
  iret = vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_FROM_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (dec, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (dec, RESOURCE, SYNC, NULL,
        ("failed to sync response from softkernel. reason : %s",
            strerror (errno)));
    return FALSE;
  }

  /* check response from softkernel */
  if (!payload_buf->cmd_rsp)
    return FALSE;

  return TRUE;
}

static gboolean
vvas_xvcudec_allocate_internal_buffers (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  int iret = 0, i;
  xclBufferHandle bh;
  xclBufferHandle *bh_ptr;

  if (priv->allocated_intr_bufs)
    return TRUE;                /* return if already allocated */

  priv->ert_cmd_buf = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
  if (priv->ert_cmd_buf == NULL) {
    GST_ERROR_OBJECT (dec, "failed to allocate ert cmd memory");
    goto error;
  }

  priv->sk_payload_buf = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
  if (priv->sk_payload_buf == NULL) {
    GST_ERROR_OBJECT (dec, "failed to allocate sk payload memory");
    goto error;
  }

  for (i = 0; i < MAX_IBUFFS; i++) {
    priv->in_xrt_bufs[i] = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
    if (priv->in_xrt_bufs[i] == NULL) {
      GST_ERROR_OBJECT (dec, "failed to allocate sk payload memory");
      goto error;
    }
  }

  priv->dec_cfg_buf = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
  if (priv->dec_cfg_buf == NULL) {
    GST_ERROR_OBJECT (dec, "failed to allocate decoder config memory handle");
    goto error;
  }

  bh = xclAllocBO (dec->priv->xcl_dev_handle, ERT_CMD_SIZE,
      XCL_BO_MIRRORED_VIRTUAL, dec->in_mem_bank | XCL_BO_FLAGS_EXECBUF);
  if (bh == NULLBO) {
    GST_ERROR_OBJECT (dec, "failed to allocate Device BO...");
    goto error;
  }

  bh_ptr = (xclBufferHandle *) calloc (1, sizeof (xclBufferHandle));
  if (bh_ptr == NULL) {
    xclFreeBO (dec->priv->xcl_dev_handle, bh);
    dec->priv->xcl_dev_handle = NULL;
    GST_ERROR_OBJECT (dec, "failed to allocate Device BO...");
    goto error;
  }

  priv->ert_cmd_buf->user_ptr = xclMapBO (dec->priv->xcl_dev_handle, bh, true);
  if (priv->ert_cmd_buf->user_ptr == NULL) {
    GST_ERROR_OBJECT (dec, "failed to map BO...");
    xclFreeBO (dec->priv->xcl_dev_handle, bh);
    dec->priv->xcl_dev_handle = NULL;
    free (bh_ptr);
    goto error;
  }

  *(bh_ptr) = bh;
  priv->ert_cmd_buf->bo = bh_ptr;
  priv->ert_cmd_buf->size = ERT_CMD_SIZE;
  priv->ert_cmd_buf->phy_addr = 0;

  /* allocate softkernel payload buffer */
  iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
      sizeof (sk_payload_data),
      VVAS_BO_FLAGS_NONE, dec->in_mem_bank, priv->sk_payload_buf);
  if (iret < 0) {
    GST_ERROR_OBJECT (dec, "failed to allocate softkernel payload buffer..");
    goto error;
  }

  /* allocate decoder config buffer */
  iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
      sizeof (dec_params_t), VVAS_BO_FLAGS_NONE,
      dec->in_mem_bank, priv->dec_cfg_buf);
  if (iret < 0) {
    GST_ERROR_OBJECT (dec, "failed to allocate decoder config buffer..");
    goto error;
  }

  priv->allocated_intr_bufs = TRUE;
  return TRUE;

error:
  return FALSE;
}

static void
vvas_xvcudec_free_internal_buffers (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  int i;

  if (!priv->allocated_intr_bufs)
    return;

  if (priv->dec_cfg_buf) {
    vvas_xrt_free_xrt_buffer (priv->dec_cfg_buf);
    free (priv->dec_cfg_buf);
    priv->dec_cfg_buf = NULL;
  }

  for (i = 0; i < MAX_IBUFFS; i++) {
    if (priv->in_xrt_bufs[i]) {
      vvas_xrt_free_xrt_buffer (priv->in_xrt_bufs[i]);
      free (priv->in_xrt_bufs[i]);
      priv->in_xrt_bufs[i] = NULL;
    }
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
vvas_vcu_dec_outbuffer_alloc_and_map (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  uint64_t *out_bufs_addr;
#ifdef HDR_DATA_SUPPORT
  uint64_t *hdr_bufs_addr;
#endif
  int iret = 0, i;

  if (!priv->num_out_bufs || !priv->out_buf_size) {
    GST_ERROR_OBJECT (dec, "invalid output allocation parameters : "
        "num_out_bufs = %d & out_buf_size = %lu", priv->num_out_bufs,
        priv->out_buf_size);
    return FALSE;
  }

  GST_INFO_OBJECT (dec,
      "minimum number of output buffers required by vcu decoder = %d "
      "and output buffer size = %lu", priv->num_out_bufs, priv->out_buf_size);

  gst_vvas_buffer_pool_set_pre_release_buffer_cb ((GstVvasBufferPool *)
      priv->pool, vvas_vcu_dec_pre_release_buffer_cb, dec);

  gst_vvas_buffer_pool_set_post_release_buffer_cb ((GstVvasBufferPool *)
      priv->pool, vvas_vcu_dec_post_release_buffer_cb, dec);

  priv->dec_out_bufs_handle = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
  if (priv->dec_out_bufs_handle == NULL) {
    GST_ERROR_OBJECT (dec,
        "failed to allocate decoder output buffers structure");
    goto error;
  }

  iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
      priv->num_out_bufs * sizeof (uint64_t), VVAS_BO_FLAGS_NONE,
      dec->out_mem_bank, priv->dec_out_bufs_handle);
  if (iret < 0) {
    GST_ERROR_OBJECT (dec, "failed to allocate decoder out buffers handle..");
    goto error;
  }

  out_bufs_addr = (uint64_t *) (priv->dec_out_bufs_handle->user_ptr);

  if (priv->out_bufs_arr)
    free (priv->out_bufs_arr);

  priv->out_bufs_arr =
      (GstBuffer **) calloc (priv->num_out_bufs, sizeof (GstBuffer *));
  if (!priv->out_bufs_arr) {
    GST_ERROR_OBJECT (dec, "failed to allocate memory");
    goto error;
  }
#ifdef HDR_DATA_SUPPORT

  priv->hdr_out_bufs_handle = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
  if (priv->hdr_out_bufs_handle == NULL) {
    GST_ERROR_OBJECT (dec, "failed to allocate HDR output buffers structure");
    goto error;
  }

  iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
      priv->num_out_bufs * sizeof (uint64_t), VVAS_BO_FLAGS_NONE,
      dec->out_mem_bank, priv->hdr_out_bufs_handle);
  if (iret < 0) {
    GST_ERROR_OBJECT (dec, "failed to allocate HDR out buffers handle..");
    goto error;
  }

  hdr_bufs_addr = (uint64_t *) (priv->hdr_out_bufs_handle->user_ptr);

  if (priv->hdr_bufs_arr)
    free (priv->hdr_bufs_arr);

  priv->hdr_bufs_arr =
      (xrt_buffer *) calloc (priv->num_out_bufs, sizeof (xrt_buffer));
  if (!priv->hdr_bufs_arr) {
    GST_ERROR_OBJECT (dec, "failed to allocate memory for HDR buffers array");
    goto error;
  }
#endif

  for (i = 0; i < priv->num_out_bufs; i++) {
    GstMemory *outmem = NULL;
    GstBuffer *outbuf = NULL;

    XlnxOutputBuffer *xlnx_buf =
        (XlnxOutputBuffer *) calloc (1, sizeof (XlnxOutputBuffer));
    if (xlnx_buf == NULL) {
      GST_ERROR_OBJECT (dec, "failed to allocate decoder output buffer");
      goto error;
    }

    if (gst_buffer_pool_acquire_buffer (priv->pool, &outbuf,
            NULL) != GST_FLOW_OK) {
      GST_INFO_OBJECT (dec, "Failed to acquire %d-th buffer", i);
      goto error;
    }

    outmem = gst_buffer_get_memory (outbuf, 0);

    if (!gst_is_vvas_memory (outmem)) {
      GST_ERROR_OBJECT (dec, "not an xrt memory");
      gst_memory_unref (outmem);
      gst_buffer_unref (outbuf);
      goto error;
    }

    xlnx_buf->idx = i;
    xlnx_buf->xrt_buf.phy_addr = gst_vvas_allocator_get_paddr (outmem);
    xlnx_buf->xrt_buf.size = gst_buffer_get_size (outbuf);
    xlnx_buf->xrt_buf.bo = gst_vvas_allocator_get_bo (outmem);
    xlnx_buf->gstbuf = outbuf;

    out_bufs_addr[i] = xlnx_buf->xrt_buf.phy_addr;

    g_hash_table_insert (priv->oidx_hash, outmem, xlnx_buf);
    priv->out_bufs_arr[i] = outbuf;
    g_hash_table_insert (priv->out_buf_hash, GINT_TO_POINTER (i), outbuf);

    GST_DEBUG_OBJECT (dec, "output [%d] : mapping memory %p with paddr = %p", i,
        outmem, (void *) xlnx_buf->xrt_buf.phy_addr);

    gst_memory_unref (outmem);
    priv->cur_skbuf_count++;

#ifdef HDR_DATA_SUPPORT
    /* allocate HDR buffers */
    iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
        sizeof (vcu_hdr_data), VVAS_BO_FLAGS_NONE,
        dec->out_mem_bank, &priv->hdr_bufs_arr[i]);
    if (iret < 0) {
      GST_ERROR_OBJECT (dec, "failed to allocate HDR buffer at index %d", i);
      goto error;
    }
    hdr_bufs_addr[i] = priv->hdr_bufs_arr[i].phy_addr;
#endif
  }

  iret = vvas_xrt_sync_bo (priv->dec_out_bufs_handle->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->dec_out_bufs_handle->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (dec, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (dec, RESOURCE, SYNC, NULL,
        ("failed to sync output buffers' handles to device. reason : %s",
            strerror (errno)));
    goto error;
  }
#ifdef HDR_DATA_SUPPORT
  iret = vvas_xrt_sync_bo (priv->hdr_out_bufs_handle->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->hdr_out_bufs_handle->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (dec, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (dec, RESOURCE, SYNC, NULL,
        ("failed to sync HDR buffers' handles to device. reason : %s",
            strerror (errno)));
    goto error;
  }
#endif

  priv->outbufs_allocated = TRUE;

  return TRUE;

error:
  return FALSE;
}

static void
vvas_xvcudec_free_output_buffers (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;

  if (priv->dec_out_bufs_handle) {
    vvas_xrt_free_xrt_buffer (priv->dec_out_bufs_handle);
    free (priv->dec_out_bufs_handle);
  }
#ifdef HDR_DATA_SUPPORT
  if (priv->hdr_out_bufs_handle) {
    vvas_xrt_free_xrt_buffer (priv->hdr_out_bufs_handle);
    free (priv->hdr_out_bufs_handle);
  }
#endif
}

static gboolean
vvas_xvcudec_preinit (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  sk_payload_data *payload_buf;
  dec_params_t *dec_cfg;
  const gchar *mimetype;
  const GstStructure *structure;
  unsigned int payload_data[ERT_CMD_DATA_LEN];
  unsigned int num_idx = 0;
  int iret = 0;
  gboolean bret = FALSE;
  GstVideoInfo vinfo;
  const gchar *chroma_format;
  guint bit_depth_luma, bit_depth_chroma;
  struct timespec init_time;

  /* update payload buf */
  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);
  memset (payload_buf, 0, priv->sk_payload_buf->size);

  payload_buf->cmd_id = VCU_PREINIT;
  iret =
      vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (dec, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (dec, RESOURCE, SYNC, NULL,
        ("failed to sync PREINIT command payload to device. reason : %s",
            strerror (errno)));
    goto error;
  }

  /* update decoder config params */
  dec_cfg = (dec_params_t *) (priv->dec_cfg_buf->user_ptr);
  memset (dec_cfg, 0, priv->dec_cfg_buf->size);

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

  /* assign retry timeout based on resolution & framerate */
  priv->retry_timeout =
      vvas_xvcudec_get_push_command_retry_timeout (GST_VIDEO_INFO_WIDTH
      (&vinfo), GST_VIDEO_INFO_HEIGHT (&vinfo), GST_VIDEO_INFO_FPS_N (&vinfo),
      GST_VIDEO_INFO_FPS_D (&vinfo));

  GST_INFO_OBJECT (dec,
      "retry timeout for push command %" G_GINT64_FORMAT " micro sec",
      priv->retry_timeout);

  dec_cfg->frame_rate = GST_VIDEO_INFO_FPS_N (&vinfo);
  dec_cfg->clk_ratio = GST_VIDEO_INFO_FPS_D (&vinfo);
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
    goto error;
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
    goto error;
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
      goto error;
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

  iret =
      vvas_xrt_sync_bo (priv->dec_cfg_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->dec_cfg_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (dec, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (dec, RESOURCE, SYNC, NULL,
        ("failed to sync decoder configuration to device. reason : %s",
            strerror (errno)));
    goto error;
  }

  memset (payload_data, 0, ERT_CMD_DATA_LEN * sizeof (int));
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
  payload_data[num_idx++] = priv->dec_cfg_buf->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->dec_cfg_buf->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->dec_cfg_buf->size;

  iret =
      vvas_xrt_send_softkernel_command (dec->priv->xcl_dev_handle,
      priv->ert_cmd_buf, payload_data, num_idx, dec->sk_cur_idx,
      CMD_EXEC_TIMEOUT);
  if (iret < 0) {
    GST_ERROR_OBJECT (dec,
        "failed to send VCU_PREINIT command to softkernel - %d, reason : %s",
        iret, strerror (errno));
    goto error;
  } else {
    bret = vvas_xvcudec_check_softkernel_response (dec, payload_buf);
    if (!bret) {
      GST_ERROR_OBJECT (dec, "softkernel pre-initialization failed");
      GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, NULL,
          ("decoder softkernel pre-initialization failed. reason : %s",
              payload_buf->dev_err));
      goto error;
    }
  }

  priv->num_out_bufs = payload_buf->obuff_num;
  priv->min_skbuf_count = payload_buf->obuff_num;
  priv->out_buf_size = payload_buf->obuff_size;

  GST_DEBUG_OBJECT (dec,
      "min output buffers required by softkernel %d and outbuf size %lu",
      priv->num_out_bufs, priv->out_buf_size);

  return TRUE;

error:
  return FALSE;
}

static void
vvas_xvcudec_reset (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = GST_VVAS_XVCUDEC_PRIVATE (dec);

  priv->out_bufs_arr = NULL;
#ifdef HDR_DATA_SUPPORT
  priv->hdr_bufs_arr = NULL;
#endif
  priv->pool = NULL;
  priv->outbufs_allocated = FALSE;
  priv->free_oidx_list = NULL;
  priv->pre_free_oidx_list = NULL;
  priv->init_done = FALSE;
  priv->flush_done = FALSE;
  priv->max_ibuf_size = 0;
  priv->host_to_dev_ibuf_idx = 0;
  priv->need_copy = TRUE;
  priv->allocated_intr_bufs = FALSE;
  priv->min_skbuf_count = 0;
  priv->cur_skbuf_count = 0;
#ifdef ENABLE_XRM_SUPPORT
  priv->xrm_ctx = NULL;
  priv->cu_list_res = NULL;
  priv->cu_res[0] = priv->cu_res[1] = NULL;
  priv->cur_load = 0;
#endif
  priv->last_pts = GST_CLOCK_TIME_NONE;
  priv->genpts = 0;
  priv->xcl_ctx_valid = FALSE;

}

static gboolean
vvas_xvcudec_init (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  GstVideoInfo vinfo;
  sk_payload_data *payload_buf;
  unsigned int payload_data[ERT_CMD_DATA_LEN];
  unsigned int num_idx = 0;
  int iret = 0, i;
  gboolean bret = FALSE;

  vinfo = dec->input_state->info;

  priv->max_ibuf_size = GST_VIDEO_INFO_WIDTH (&vinfo) *
      GST_VIDEO_INFO_HEIGHT (&vinfo);

  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);
  payload_buf->cmd_id = VCU_INIT;
  payload_buf->obuff_num = priv->num_out_bufs;

  iret = vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (dec, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (dec, RESOURCE, SYNC, NULL,
        ("failed to sync INIT command payload to device. reason : %s",
            strerror (errno)));
    goto error;
  }

  memset (payload_data, 0, ERT_CMD_DATA_LEN * sizeof (int));

  payload_data[num_idx++] = 0;
  payload_data[num_idx++] = VCU_INIT;
  payload_data[num_idx++] = getpid ();
  payload_data[num_idx++] = priv->timestamp & 0xFFFFFFFF;
  payload_data[num_idx++] = (priv->timestamp >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->sk_payload_buf->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->sk_payload_buf->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = sizeof (sk_payload_data);

  for (i = 0; i < MAX_IBUFFS; i++) {
    /* allocate input buffer */
    iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle,
        priv->max_ibuf_size, VVAS_BO_FLAGS_NONE,
        dec->in_mem_bank, priv->in_xrt_bufs[i]);
    if (iret < 0) {
      GST_ERROR_OBJECT (dec, "failed to allocate input buffer..");
      goto error;
    }

    payload_data[num_idx++] = priv->in_xrt_bufs[i]->phy_addr & 0xFFFFFFFF;
    payload_data[num_idx++] =
        ((uint64_t) (priv->in_xrt_bufs[i]->phy_addr) >> 32) & 0xFFFFFFFF;
    payload_data[num_idx++] = priv->in_xrt_bufs[i]->size;
  }

  payload_data[num_idx++] = priv->dec_out_bufs_handle->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->dec_out_bufs_handle->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->dec_out_bufs_handle->size;

#ifdef HDR_DATA_SUPPORT
  payload_data[num_idx++] = priv->hdr_out_bufs_handle->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->hdr_out_bufs_handle->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->hdr_out_bufs_handle->size;
#endif

  iret =
      vvas_xrt_send_softkernel_command (dec->priv->xcl_dev_handle,
      priv->ert_cmd_buf, payload_data, num_idx, dec->sk_cur_idx,
      CMD_EXEC_TIMEOUT);
  if (iret < 0) {
    GST_ERROR_OBJECT (dec,
        "failed to send VCU_INIT command to softkernel - %d, reason : %s",
        iret, strerror (errno));
    goto error;
  } else {
    bret = vvas_xvcudec_check_softkernel_response (dec, payload_buf);
    if (!bret) {
      GST_ERROR_OBJECT (dec, "softkernel initialization failed");
      GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, NULL,
          ("decoder softkernel initialization failed. reason : %s",
              payload_buf->dev_err));
      goto error;
    }
  }

  priv->init_done = TRUE;
  return TRUE;

error:
  return FALSE;
}

static void
vvas_xvcudec_set_pts (GstVvas_XVCUDec * dec, GstVideoCodecFrame * frame,
    GstClockTime out_ts)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (dec);
  GstVvas_XVCUDecPrivate *priv = dec->priv;
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

static GstFlowReturn
vvas_xvcudec_read_out_buffer (GstVvas_XVCUDec * dec, guint idx,
    GstVideoCodecState * out_state, GstClockTime out_ts)
{
  GstVideoCodecFrame *frame = NULL;
  GstFlowReturn fret = GST_FLOW_ERROR;
  GstMemory *outmem = NULL;

  if (idx == 0xBAD) {
    GST_ERROR_OBJECT (dec, "bad output index received...");
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (dec, "reading output buffer at index %d", idx);

  frame = gst_video_decoder_get_oldest_frame (GST_VIDEO_DECODER (dec));
  if (!frame) {
    /* Can only happen in finish() */
    GST_INFO_OBJECT (dec, "no input frames available...returning EOS");
    return GST_FLOW_EOS;
  }

  if (dec->interpolate_timestamps) {
    vvas_xvcudec_set_pts (dec, frame, out_ts);
  } else {
    frame->pts = out_ts;
  }

  if (dec->priv->need_copy) {
    GstBuffer *new_outbuf, *outbuf;
    GstVideoFrame new_frame, out_frame;
    GstMemory *outmem;

    outbuf = dec->priv->out_bufs_arr[idx];
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
    frame->output_buffer = dec->priv->out_bufs_arr[idx];
    outmem = gst_buffer_get_memory (frame->output_buffer, 0);

    /* when plugins/app request to map this memory, sync will occur */
    gst_vvas_memory_set_sync_flag (outmem, VVAS_SYNC_FROM_DEVICE);
    gst_memory_unref (outmem);
  }

  g_hash_table_remove (dec->priv->out_buf_hash, GINT_TO_POINTER (idx));

  GST_LOG_OBJECT (dec, "processing index %d buffer %" GST_PTR_FORMAT, idx,
      frame->output_buffer);

#ifdef HDR_DATA_SUPPORT
  if ((dec->disable_hdr10_sei == FALSE) &&
      (dec->priv->last_rcvd_payload.obuff_meta[dec->priv->
              last_rcvd_oidx].is_hdr_present)) {
    GstVvasHdrMeta *hdr_meta = NULL;
    xrt_buffer *hdr_buffer;
    int iret;

    /* Add HDR metadata to buffer */
    hdr_meta = gst_buffer_add_vvas_hdr_meta (frame->output_buffer);
    if (!hdr_meta) {
      GST_ERROR_OBJECT (dec, "Failed to add HDR metadata");
      return GST_FLOW_ERROR;
    }

    hdr_buffer = &dec->priv->hdr_bufs_arr[idx];
    memset (hdr_buffer->user_ptr, 0, hdr_buffer->size);

    iret = vvas_xrt_sync_bo (hdr_buffer->bo,
        VVAS_BO_SYNC_BO_FROM_DEVICE, hdr_buffer->size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (dec, "HDR buffer synbo failed - %d, reason : %s", iret,
          strerror (errno));
      GST_ELEMENT_ERROR (dec, RESOURCE, SYNC, NULL,
          ("failed to sync HDR buffer from softkernel. reason : %s",
              strerror (errno)));
      return GST_FLOW_ERROR;
    }

    memcpy ((char *) &hdr_meta->hdr_metadata, (char *) hdr_buffer->user_ptr,
        sizeof (vcu_hdr_data));
    dec->priv->last_rcvd_payload.obuff_meta[dec->priv->
        last_rcvd_oidx].is_hdr_present = 0;
  }
#endif

  fret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (dec), frame);

  if (fret != GST_FLOW_OK) {
    if (fret == GST_FLOW_EOS)
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

static GstFlowReturn
vvas_xvcudec_receive_out_frames (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  sk_payload_data *payload_buf;
  GstVideoCodecState *out_state = NULL;
  unsigned int payload_data[ERT_CMD_DATA_LEN];
  unsigned int num_idx = 0;
  GstFlowReturn fret = GST_FLOW_OK;
  int iret = 0;
  gboolean bret = FALSE;
  GstClockTime out_ts;

  if (priv->last_rcvd_payload.free_index_cnt) {
    out_state = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (dec));

    GST_LOG_OBJECT (dec, "receiving cached output frames count %d",
        priv->last_rcvd_payload.free_index_cnt);
    out_ts = priv->last_rcvd_payload.obuff_meta[priv->last_rcvd_oidx].pts;
    fret =
        vvas_xvcudec_read_out_buffer (dec,
        priv->last_rcvd_payload.obuff_index[priv->last_rcvd_oidx], out_state,
        out_ts);

    priv->last_rcvd_payload.free_index_cnt--;
    priv->last_rcvd_oidx++;
    gst_video_codec_state_unref (out_state);
    return fret;
  }

  /* update payload buf */
  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);
  memset (payload_buf, 0, priv->sk_payload_buf->size);

  payload_buf->cmd_id = VCU_RECEIVE;
  iret =
      vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (dec, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (dec, RESOURCE, SYNC, NULL,
        ("failed to sync RECEIVE command payload to device. reason : %s",
            strerror (errno)));
    fret = GST_FLOW_ERROR;
    goto exit;
  }

  memset (payload_data, 0, ERT_CMD_DATA_LEN * sizeof (int));
  payload_data[num_idx++] = 0;
  payload_data[num_idx++] = VCU_RECEIVE;
  payload_data[num_idx++] = getpid ();
  payload_data[num_idx++] = priv->timestamp & 0xFFFFFFFF;
  payload_data[num_idx++] = (priv->timestamp >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->sk_payload_buf->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->sk_payload_buf->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = sizeof (sk_payload_data);

  GST_LOG_OBJECT (dec, "sending VCU_RECEIVE command to softkernel");
  /* send command to softkernel */
  iret =
      vvas_xrt_send_softkernel_command (dec->priv->xcl_dev_handle,
      priv->ert_cmd_buf, payload_data, num_idx, dec->sk_cur_idx,
      CMD_EXEC_TIMEOUT);
  if (iret < 0) {
    GST_ERROR_OBJECT (dec,
        "failed to send VCU_RECEIVE command to softkernel - %d, reason : %s",
        iret, strerror (errno));
    fret = GST_FLOW_ERROR;
    goto exit;
  } else {
    bret = vvas_xvcudec_check_softkernel_response (dec, payload_buf);
    if (!bret) {
      GST_ERROR_OBJECT (dec, "softkernel receive frame failed");
      GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, NULL,
          ("decoder softkernel receive frame failed. reason : %s",
              payload_buf->dev_err));
      fret = GST_FLOW_ERROR;
      goto exit;
    }
  }

  GST_LOG_OBJECT (dec, "successfully completed VCU_RECEIVE command");

  // TODO: get width and height from softkernel
  if (!gst_pad_has_current_caps (GST_VIDEO_DECODER_SRC_PAD (dec))) {
    GstVideoInfo vinfo;
    GstCaps *outcaps = NULL;
    // TODO: add check for resolution change

    // HACK: taking input resolution and setting instead of taking from softkernel output
    vinfo = dec->input_state->info;

    if (dec->bit_depth == 10)
      out_state =
          gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
          GST_VIDEO_FORMAT_NV12_10LE32, GST_VIDEO_INFO_WIDTH (&vinfo),
          GST_VIDEO_INFO_HEIGHT (&vinfo), dec->input_state);
    else
      out_state =
          gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
          GST_VIDEO_FORMAT_NV12, GST_VIDEO_INFO_WIDTH (&vinfo),
          GST_VIDEO_INFO_HEIGHT (&vinfo), dec->input_state);

    /* In case if one of the parameters is zero, base class will override with default
     * values. To avoid this, we are passing on the same incoming colorimetry
     * information to downstream as well. Refer https://jira.xilinx.com/browse/CR-1114507
     * for more information 
     */
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
      goto exit;
    }
    GST_INFO_OBJECT (dec, "negotiated caps on source pad : %" GST_PTR_FORMAT,
        outcaps);
  } else {
    out_state = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (dec));
  }

  GST_LOG_OBJECT (dec, "number of available output buffers %d for consumption",
      payload_buf->free_index_cnt);
  priv->cur_skbuf_count -= payload_buf->free_index_cnt;

  memcpy (&priv->last_rcvd_payload, payload_buf, sizeof (sk_payload_data));

  if (priv->last_rcvd_payload.free_index_cnt) {
    priv->last_rcvd_oidx = 0;
    out_ts = priv->last_rcvd_payload.obuff_meta[priv->last_rcvd_oidx].pts;
    fret =
        vvas_xvcudec_read_out_buffer (dec,
        priv->last_rcvd_payload.obuff_index[priv->last_rcvd_oidx], out_state,
        out_ts);

    priv->last_rcvd_payload.free_index_cnt--;
    priv->last_rcvd_oidx++;

    if (fret != GST_FLOW_OK)
      goto exit;

  } else if (payload_buf->end_decoding) {
    GST_INFO_OBJECT (dec, "EOS recevied from softkernel");
    fret = GST_FLOW_EOS;
    goto exit;
  }

  GST_LOG_OBJECT (dec, "softkernel receive successful");

exit:
  if (out_state)
    gst_video_codec_state_unref (out_state);
  return fret;
}

static gboolean
vvas_xvcudec_send_flush (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  sk_payload_data *payload_buf;
  unsigned int payload_data[ERT_CMD_DATA_LEN];
  gboolean bret = FALSE;
  int iret = 0;
  unsigned int num_idx = 0;

  if (priv->flush_done) {
    GST_WARNING_OBJECT (dec,
        "flush already issued to softkernel, hence returning");
    return TRUE;
  }
  /* update payload buf */

  if ((priv->sk_payload_buf == NULL)
      || (priv->sk_payload_buf->user_ptr == NULL)) {
    return FALSE;
  }

  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);
  memset (payload_buf, 0, priv->sk_payload_buf->size);

  payload_buf->cmd_id = VCU_FLUSH;
  iret =
      vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (dec, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (dec, RESOURCE, SYNC, NULL,
        ("failed to sync FLUSH command payload to device. reason : %s",
            strerror (errno)));
    goto error;
  }

  memset (payload_data, 0, ERT_CMD_DATA_LEN * sizeof (int));
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
      vvas_xrt_send_softkernel_command (dec->priv->xcl_dev_handle,
      priv->ert_cmd_buf, payload_data, num_idx, dec->sk_cur_idx,
      CMD_EXEC_TIMEOUT);
  if (iret < 0) {
    GST_ERROR_OBJECT (dec,
        "failed to send VCU_FLUSH command to softkernel - %d", iret);
    goto error;
  } else {
    bret = vvas_xvcudec_check_softkernel_response (dec, payload_buf);
    if (!bret) {
      GST_ERROR_OBJECT (dec, "softkernel flush failed");
      GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, NULL,
          ("decoder softkernel flush failed. reason : %s",
              payload_buf->dev_err));
      goto error;
    }
  }
  GST_DEBUG_OBJECT (dec, "successfully sent flush command");
  priv->flush_done = TRUE;
  return TRUE;

error:
  priv->flush_done = TRUE;
  return FALSE;
}

static gboolean
vvas_xvcudec_deinit (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  sk_payload_data *payload_buf;
  unsigned int payload_data[ERT_CMD_DATA_LEN];
  unsigned int num_idx = 0;
  int iret = 0;

  if (priv->deinit_done) {
    GST_WARNING_OBJECT (dec,
        "deinit already issued to softkernel, hence returning");
    return TRUE;
  }

  /* update payload buf */
  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);
  memset (payload_buf, 0, priv->sk_payload_buf->size);

  payload_buf->cmd_id = VCU_DEINIT;
  iret = vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (dec, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (dec, RESOURCE, SYNC, NULL,
        ("failed to sync DEINIT command payload to device. reason : %s",
            strerror (errno)));
    goto error;
  }
  memset (payload_data, 0, ERT_CMD_DATA_LEN * sizeof (int));
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
      vvas_xrt_send_softkernel_command (dec->priv->xcl_dev_handle,
      priv->ert_cmd_buf, payload_data, num_idx, dec->sk_cur_idx,
      CMD_EXEC_TIMEOUT);
  if (iret < 0) {
    GST_ERROR_OBJECT (dec,
        "failed to send VCU_DEINIT command to softkernel - %d, reason : %s",
        iret, strerror (errno));
    goto error;
  }

  GST_INFO_OBJECT (dec, "Successfully deinitialized softkernel");
  return TRUE;

error:
  return FALSE;
}

static gboolean
gstvvas_xvcudec_start (GstVideoDecoder * decoder)
{
  GstVvas_XVCUDec *dec = GST_VVAS_XVCUDEC (decoder);
  GstVvas_XVCUDecPrivate *priv = GST_VVAS_XVCUDEC_PRIVATE (dec);

  vvas_xvcudec_reset (dec);

  priv->oidx_hash =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, free);
  priv->out_buf_hash = g_hash_table_new (g_direct_hash, g_direct_equal);

#ifdef ENABLE_XRM_SUPPORT

  dec->priv->xrm_ctx = (xrmContext *) xrmCreateContext (XRM_API_VERSION_1);
  if (!dec->priv->xrm_ctx) {
    GST_ERROR_OBJECT (dec, "create XRM context failed");
    return FALSE;
  }

  GST_INFO_OBJECT (dec, "successfully created xrm context");
#endif
  dec->priv->has_error = FALSE;

  return TRUE;
}

static gboolean
gstvvas_xvcudec_stop (GstVideoDecoder * decoder)
{
  GstVvas_XVCUDec *dec = GST_VVAS_XVCUDEC (decoder);
  GstVvas_XVCUDecPrivate *priv = dec->priv;

  GST_DEBUG_OBJECT (GST_VVAS_XVCUDEC (decoder), "stop");

  if (priv->init_done) {
    vvas_xvcudec_send_flush (GST_VVAS_XVCUDEC (decoder));

    vvas_xvcudec_deinit (GST_VVAS_XVCUDEC (decoder));

    /* release output buffers already sent to device */
    g_hash_table_foreach_remove (priv->out_buf_hash,
        vvas_free_output_hash_value, dec);

    priv->init_done = FALSE;
  }

  if (priv->mem_released_handler > 0) {
    g_signal_handler_disconnect (priv->allocator, priv->mem_released_handler);
    priv->mem_released_handler = 0;
  }

  gst_clear_object (&priv->allocator);

  if (priv->pool)
    gst_clear_object (&priv->pool);

  priv->pool = NULL;
  priv->allocator = NULL;

  /* free all output buffers allocated */
  vvas_xvcudec_free_output_buffers (dec);

  /* free all internal buffers */
  vvas_xvcudec_free_internal_buffers (dec);

  /* freeing XlnxOutputBuffer memory i.e. value in hash table */
  g_hash_table_remove_all (priv->oidx_hash);
  g_hash_table_unref (priv->oidx_hash);
  g_hash_table_destroy (priv->out_buf_hash);
  if (priv->out_bufs_arr)
    free (priv->out_bufs_arr);
#ifdef HDR_DATA_SUPPORT
  if (priv->hdr_bufs_arr) {
    int i;

    for (i = (priv->num_out_bufs - 1); i >= 0; i--) {
      vvas_xrt_free_xrt_buffer (&priv->hdr_bufs_arr[i]);
    }
    free (priv->hdr_bufs_arr);
    priv->hdr_bufs_arr = NULL;

  }
#endif
  vvas_xvcudec_destroy_context (dec);

#ifdef ENABLE_XRM_SUPPORT
  if (xrmDestroyContext (priv->xrm_ctx) != XRM_SUCCESS)
    GST_ERROR_OBJECT (dec, "failed to destroy XRM context");
#endif

  if (dec->input_state) {
    gst_video_codec_state_unref (dec->input_state);
    dec->input_state = NULL;
  }

  /* Elements are release in g_hash_table_remove_all (priv->oidx_hash)
   * So here we just need to frees all of the memory used by a GList.
   */
  if (priv->free_oidx_list)
    g_list_free (priv->free_oidx_list);
  if (priv->pre_free_oidx_list)
    g_list_free (priv->pre_free_oidx_list);

  return TRUE;
}

#ifdef ENABLE_XRM_SUPPORT
static gchar *
vvas_xvcudec_prepare_request_json_string (GstVvas_XVCUDec * dec)
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

  req_obj = json_pack ("{s:{s:{s:[{s:s,s:s,s:{s:{s:i,s:i,s:{s:i,s:i}}}}]}}}",
      "request", "parameters", "resources", "function", "DECODER",
      "format", strcmp (mimetype, "video/x-h264") ? "H265" : "H264",
      "resolution", "input", "width", in_width, "height", in_height,
      "frame-rate", "num", fps_n, "den", fps_d);

  req_str = json_dumps (req_obj, JSON_DECODE_ANY);
  json_decref (req_obj);

  GST_LOG_OBJECT (dec, "prepared xrm request %s", req_str);

  return req_str;
}

static gboolean
vvas_xvcudec_calculate_load (GstVvas_XVCUDec * dec, gint * load)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  int iret = -1, func_id = 0;
  gchar *req_str;
  char plugin_name[XRM_MAX_NAME_LEN];
  xrmPluginFuncParam param;

  if (!priv->xrm_ctx) {
    GST_ERROR_OBJECT (dec, "xrm context not created");
    return FALSE;
  }

  /* prepare json string to request xrm for load */
  req_str = vvas_xvcudec_prepare_request_json_string (dec);
  if (!req_str) {
    GST_ERROR_OBJECT (dec, "failed to prepare xrm json request string");
    return FALSE;
  }

  memset (&param, 0x0, sizeof (xrmPluginFuncParam));
  memset (plugin_name, 0x0, XRM_MAX_NAME_LEN);

  strcpy (plugin_name, "xrmU30DecPlugin");

  if (strlen (req_str) > (XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1)) {
    GST_ERROR_OBJECT (dec, "request input string length %lu > max allowed %d",
        strlen (req_str), XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1);
    free (req_str);
    return FALSE;
  }

  strncpy (param.input, req_str, XRM_MAX_PLUGIN_FUNC_PARAM_LEN);
  free (req_str);

  iret = xrmExecPluginFunc (priv->xrm_ctx, plugin_name, func_id, &param);
  if (iret != XRM_SUCCESS) {
    GST_ERROR_OBJECT (dec, "failed to get load from xrm plugin. err : %d",
        iret);
    GST_ELEMENT_ERROR (dec, RESOURCE, FAILED,
        ("failed to get load from xrm plugin"), NULL);
    priv->has_error = TRUE;
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

static gboolean
vvas_xvcudec_allocate_resource (GstVvas_XVCUDec * dec, gint dec_load)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
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

    strcpy (cu_list_prop.cuProps[1].kernelName, "kernel_vcu_decoder");
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
      priv->has_error = TRUE;
      return FALSE;
    }

    num_hard_cus =
        vvas_xrt_get_num_compute_units (cu_list_resource->
        cuResources[0].xclbinFileName);

    if (num_hard_cus == -1) {
      GST_ERROR_OBJECT (dec, "failed to get number of cus in xclbin: %s",
          cu_list_resource->cuResources[0].xclbinFileName);
      return FALSE;
    }

    GST_DEBUG_OBJECT (dec, "Total Number of Compute Units: %ld in xclbin:%s",
        num_hard_cus, cu_list_resource->cuResources[0].xclbinFileName);

    priv->cu_list_res = cu_list_resource;
    dec->dev_index = cu_list_resource->cuResources[0].deviceId;
    priv->cu_idx = cu_list_resource->cuResources[0].cuId;
    dec->sk_cur_idx = cu_list_resource->cuResources[1].cuId - num_hard_cus;
    uuid_copy (priv->xclbinId, cu_list_resource->cuResources[0].uuid);

    GST_INFO_OBJECT (dec, "xrm CU list allocation success: dev-idx = %d, "
        "sk-cur-idx = %d and softkernel plugin name %s",
        dec->dev_index, dec->sk_cur_idx,
        priv->cu_list_res->cuResources[0].kernelPluginFileName);

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

    strcpy (cu_sw_prop.kernelName, "kernel_vcu_decoder");
    cu_sw_prop.devExcl = false;
    cu_sw_prop.requestLoad =
        XRM_PRECISION_1000000_BIT_MASK (XRM_MAX_CU_LOAD_GRANULARITY_1000000);

    /* allocate hardware resource */
    iret = xrmCuAllocFromDev (priv->xrm_ctx, dec->dev_index, &cu_hw_prop,
        cu_hw_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (dec, "failed to do hard CU allocation using XRM");
      GST_ELEMENT_ERROR (dec, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from device id %d", dec->dev_index),
          NULL);
      return FALSE;
    }

    /* allocate softkernel resource */
    iret = xrmCuAllocFromDev (priv->xrm_ctx, dec->dev_index, &cu_sw_prop,
        cu_sw_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (dec, "failed to do soft CU allocation using XRM");
      GST_ELEMENT_ERROR (dec, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from device id %d", dec->dev_index),
          NULL);
      return FALSE;
    }

    num_hard_cus =
        vvas_xrt_get_num_compute_units (cu_hw_resource->xclbinFileName);

    if (num_hard_cus == -1) {
      GST_ERROR_OBJECT (dec, "failed to get number of cus in xclbin: %s",
          cu_hw_resource->xclbinFileName);
      return FALSE;
    }

    GST_DEBUG_OBJECT (dec, "Total Number of Compute Units: %ld in xclbin:%s",
        num_hard_cus, cu_hw_resource->xclbinFileName);

    priv->cu_res[0] = cu_hw_resource;
    priv->cu_res[1] = cu_sw_resource;
    dec->dev_index = cu_hw_resource->deviceId;
    priv->cu_idx = cu_hw_resource->cuId;
    dec->sk_cur_idx = cu_sw_resource->cuId - num_hard_cus;
    uuid_copy (priv->xclbinId, cu_hw_resource->uuid);

    GST_INFO_OBJECT (dec, "xrm CU list allocation success: dev-idx = %d, "
        "cu-idx = %d, sk-cur-idx = %d and softkernel plugin name %s",
        dec->dev_index, priv->cu_idx, dec->sk_cur_idx,
        cu_hw_resource->kernelPluginFileName);
  }

  return TRUE;
}
#endif

static gboolean
vvas_xvcudec_create_context (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
#ifdef ENABLE_XRM_SUPPORT
  gboolean bret;

  /* gets cu index & device id (using reservation id) */
  bret = vvas_xvcudec_allocate_resource (dec, priv->cur_load);
  if (!bret)
    return FALSE;

#endif

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

  dec->priv->xcl_dev_handle = xclOpen (dec->dev_index, NULL, XCL_INFO);
  if (!(dec->priv->xcl_dev_handle)) {
    GST_ERROR_OBJECT (dec, "failed to open device index %u", dec->dev_index);
    return FALSE;;
  }

  if (!vvas_softkernel_xrt_open_device (dec->dev_index,
          dec->priv->xcl_dev_handle, &priv->dev_handle)) {
    GST_ERROR_OBJECT (dec, "failed to open device index %u", dec->dev_index);
    return FALSE;
  }

  /* TODO: Need to uncomment after CR-1122125 is resolved */
//#ifndef ENABLE_XRM_SUPPORT
  if (!dec->xclbin_path) {
    GST_ERROR_OBJECT (dec, "invalid xclbin path %s", dec->xclbin_path);
    GST_ELEMENT_ERROR (dec, RESOURCE, NOT_FOUND, (NULL),
        ("xclbin path not set"));
    return FALSE;
  }

  /* We have to download the xclbin irrespective of XRM or not as there
   * mismatch of UUID between XRM and XRT Native. CR-1122125 raised */
  if (vvas_xrt_download_xclbin (dec->xclbin_path,
          priv->dev_handle, &(priv->xclbinId))) {
    GST_ERROR_OBJECT (dec, "failed to initialize XRT");
    GST_ELEMENT_ERROR (dec, RESOURCE, FAILED, (NULL),
        ("xclbin download failed"));
    return FALSE;
  }
//#endif

  if (!dec->kernel_name)
    dec->kernel_name = g_strdup (VVAS_VCUDEC_KERNEL_NAME_DEFAULT);

  if (xclOpenContext (priv->xcl_dev_handle, priv->xclbinId, priv->cu_idx, true)) {
    GST_ERROR_OBJECT (dec, "failed to open XRT context ...");
    return FALSE;
  }

  return TRUE;
}

static gboolean
vvas_xvcudec_destroy_context (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  gboolean has_error = FALSE;
  gint iret;
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

  if (priv->xcl_ctx_valid) {
    iret = xclCloseContext (priv->xcl_dev_handle, priv->xclbinId, priv->cu_idx);
    if (iret < 0) {
      GST_ERROR_OBJECT (dec, "failed to close dec xrt context");
      has_error = TRUE;
    } else {
      GST_INFO_OBJECT (dec, "closed xrt context");
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
gstvvas_xvcudec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstVvas_XVCUDec *dec = GST_VVAS_XVCUDEC (decoder);
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  gboolean bret = TRUE;
  gboolean do_reconfigure = FALSE;

  GST_DEBUG_OBJECT (dec, "input caps: %" GST_PTR_FORMAT, state->caps);

  if (priv->has_error)
    return FALSE;

  if (!dec->input_state ||
      !gst_caps_is_equal (dec->input_state->caps, state->caps))
    do_reconfigure = TRUE;

  if (dec->input_state) {
    gst_video_codec_state_unref (dec->input_state);
    dec->input_state = NULL;
  }
  dec->input_state = gst_video_codec_state_ref (state);

  /* Check for "profile" info in the caps for H264 case */
  if (!gst_structure_get_string (gst_caps_get_structure (dec->input_state->caps,
              0), "profile")) {
    GST_WARNING_OBJECT (dec, "Profile info not present in the caps");
    return FALSE;
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
    bret = vvas_xvcudec_calculate_load (dec, &load);
    if (!bret) {
      priv->has_error = TRUE;
      return FALSE;
    }

    if (priv->cur_load != load) {

      priv->cur_load = load;

      /* destroy XRT context as new load received */
      bret = vvas_xvcudec_destroy_context (dec);
      if (!bret) {
        priv->has_error = TRUE;
        return FALSE;
      }

      /* create XRT context */
      priv->xcl_ctx_valid = vvas_xvcudec_create_context (dec);
      if (!priv->xcl_ctx_valid) {
        priv->has_error = TRUE;
        return FALSE;
      }

      /* free resources as device idx might change */
      vvas_xvcudec_free_internal_buffers (dec);
    }
#else
    if (!priv->dev_handle) {
      /* create XRT context */
      bret = vvas_xvcudec_create_context (dec);
      if (!bret) {
        priv->has_error = TRUE;
        return FALSE;
      }
    }
#endif
  }

  bret = vvas_xvcudec_allocate_internal_buffers (dec);
  if (bret == FALSE) {
    GST_ERROR_OBJECT (dec, "failed to allocate internal buffers");
    return FALSE;
  }
  // TODO: add support for reconfiguration .e.g. deinit softkernel decoder
  bret = vvas_xvcudec_preinit (dec);
  if (!bret) {
    return FALSE;
  }

  return TRUE;
}

static void
gstvvas_xvcudec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_XVCUDec *dec = GST_VVAS_XVCUDEC (object);

  switch (prop_id) {
    case PROP_XCLBIN_LOCATION:
      dec->xclbin_path = g_value_dup_string (value);
      break;
#ifndef ENABLE_XRM_SUPPORT
    case PROP_SK_CURRENT_INDEX:
      dec->sk_cur_idx = g_value_get_int (value);
      break;
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
    case PROP_DISABLE_HDR_SEI:
      dec->disable_hdr10_sei = g_value_get_boolean (value);
      break;
    case PROP_IN_MEM_BANK:
      dec->in_mem_bank = g_value_get_uint (value);
      break;
    case PROP_OUT_MEM_BANK:
      dec->out_mem_bank = g_value_get_uint (value);
      break;
    case PROP_INTERPOLATE_TIMESTAMPS:
      dec->interpolate_timestamps = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gstvvas_xvcudec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvas_XVCUDec *dec = GST_VVAS_XVCUDEC (object);

  switch (prop_id) {
    case PROP_NUM_ENTROPY_BUFFER:
      g_value_set_uint (value, dec->num_entropy_bufs);
      break;
    case PROP_LOW_LATENCY:
      g_value_set_boolean (value, dec->low_latency);
      break;
#ifndef ENABLE_XRM_SUPPORT
    case PROP_SK_CURRENT_INDEX:
      g_value_set_int (value, dec->sk_cur_idx);
      break;
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
    case PROP_DISABLE_HDR_SEI:
      g_value_set_boolean (value, dec->disable_hdr10_sei);
      break;
    case PROP_IN_MEM_BANK:
      g_value_set_uint (value, dec->in_mem_bank);
      break;
    case PROP_OUT_MEM_BANK:
      g_value_set_uint (value, dec->out_mem_bank);
      break;
    case PROP_INTERPOLATE_TIMESTAMPS:
      g_value_set_boolean (value, dec->interpolate_timestamps);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
vvas_vcu_dec_pre_release_buffer_cb (GstBuffer * outbuf, gpointer user_data)
{
  GstVvas_XVCUDec *dec = GST_VVAS_XVCUDEC (user_data);
  GstMemory *outmem = NULL;
  XlnxOutputBuffer *xlnx_buf = NULL;

  if (G_LIKELY (gst_buffer_is_all_memory_writable (outbuf))) {

    g_mutex_lock (&dec->priv->pre_obuf_lock);
    outmem = gst_buffer_get_memory (outbuf, 0);

    xlnx_buf = g_hash_table_lookup (dec->priv->oidx_hash, outmem);

    if (xlnx_buf) {
      GST_DEBUG_OBJECT (dec,
          "output memory %p with mapped index %d can be sent to vcu", outmem,
          xlnx_buf->idx);
      xlnx_buf->gstbuf = outbuf;
      dec->priv->pre_free_oidx_list =
          g_list_append (dec->priv->pre_free_oidx_list, xlnx_buf);
      gst_memory_unref (outmem);
      //g_cond_signal (&dec->priv->obuf_cond);
    } else {
      GST_ERROR_OBJECT (dec, "buffer %p not found in hash table", outmem);
      GST_ELEMENT_ERROR (dec, STREAM, FAILED, ("unexpected behaviour"),
          ("unexpected behaviour: buffer %p not found in hash table", outmem));
      gst_memory_unref (outmem);
    }
    g_mutex_unlock (&dec->priv->pre_obuf_lock);
  } else {
    outmem = gst_buffer_get_memory (outbuf, 0);
    GST_WARNING_OBJECT (dec, "buffer %p and memory %p not writable", outbuf,
        outmem);
    g_mutex_lock (&dec->priv->pre_obuf_lock);
    xlnx_buf = g_hash_table_lookup (dec->priv->oidx_hash, outmem);
    xlnx_buf->gstbuf = NULL;
    gst_memory_unref (outmem);
    g_mutex_unlock (&dec->priv->pre_obuf_lock);
  }
}

static void
vvas_vcu_dec_post_release_buffer_cb (GstBuffer * outbuf, gpointer user_data)
{
  GstVvas_XVCUDec *dec = GST_VVAS_XVCUDEC (user_data);
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  guint free_cnt, i;

  g_mutex_lock (&priv->pre_obuf_lock);

  free_cnt = g_list_length (priv->pre_free_oidx_list);

  for (i = 0; i < free_cnt; i++) {
    XlnxOutputBuffer *xlnxbuf;

    xlnxbuf = g_list_nth_data (priv->pre_free_oidx_list, i);
    if (xlnxbuf->gstbuf == outbuf) {
      GST_LOG_OBJECT (dec, "moving index %d buffer to free output index list",
          xlnxbuf->idx);
      priv->pre_free_oidx_list =
          g_list_remove (priv->pre_free_oidx_list, xlnxbuf);
      g_mutex_lock (&priv->obuf_lock);
      priv->free_oidx_list = g_list_append (priv->free_oidx_list, xlnxbuf);
      g_mutex_unlock (&priv->obuf_lock);
      break;
    }
  }

  g_mutex_unlock (&priv->pre_obuf_lock);
}

static void
vvas_allocator_mem_released (GstVvasAllocator * alloc, GstMemory * outmem,
    GstVvas_XVCUDec * dec)
{
  XlnxOutputBuffer *xlnx_buf = NULL;

  g_mutex_lock (&dec->priv->obuf_lock);
  xlnx_buf = g_hash_table_lookup (dec->priv->oidx_hash, outmem);
  if (xlnx_buf) {
    GST_DEBUG_OBJECT (dec,
        "output memory %p with mapped index %d can be sent to vcu", outmem,
        xlnx_buf->idx);
    dec->priv->free_oidx_list =
        g_list_append (dec->priv->free_oidx_list, xlnx_buf);
    //g_cond_signal (&dec->priv->obuf_cond);
  } else {
    GST_ERROR_OBJECT (dec, "memory %p not found in hash table", outmem);
    GST_ELEMENT_ERROR (dec, STREAM, FAILED, ("unexpected behaviour"),
        ("unexpected behaviour: buffer %p not found in hash table", outmem));
  }
  g_mutex_unlock (&dec->priv->obuf_lock);
}

static gboolean
gstvvas_xvcudec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstVvas_XVCUDec *dec = GST_VVAS_XVCUDEC (decoder);
  GstCaps *outcaps = NULL;
  GstBufferPool *pool = NULL, *ownpool = NULL;
  guint size, min, max;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstStructure *config = NULL, *tmp_config = NULL;
  gboolean update_pool, update_allocator;
  GstVideoInfo vinfo;
  GstVideoAlignment align;

  gst_query_parse_allocation (query, &outcaps, NULL);
  gst_video_info_init (&vinfo);
  if (outcaps)
    gst_video_info_from_caps (&vinfo, outcaps);

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
        ENABLE_DMABUF, dec->out_mem_bank, dec->priv->kern_handle);
    params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
    GST_DEBUG_OBJECT (dec, "created vvasallocator %p %" GST_PTR_FORMAT
        " at mem bank: %u", allocator, allocator, dec->out_mem_bank);
  }

  if (dec->priv->allocator)
    gst_object_unref (dec->priv->allocator);

  dec->priv->allocator = allocator;

  dec->priv->mem_released_handler = g_signal_connect_object (allocator,
      "vvas-mem-released", (GCallback) vvas_allocator_mem_released, dec, 0);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    size = MAX (size, vinfo.size);
    if (min == 0)
      min = 2;
    update_pool = TRUE;
    GST_DEBUG_OBJECT (dec, "received pool %" GST_PTR_FORMAT " from downstream",
        pool);
  } else {
    pool = NULL;
    size = vinfo.size;
    min = 2;
    if (dec->avoid_dynamic_alloc)
      max = 4;
    else
      max = FRM_BUF_POOL_SIZE - 1;
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
      max = min;
      max = dec->priv->num_out_bufs + max + 1;
    } else {
      max = FRM_BUF_POOL_SIZE - 1;
    }
  }

  min = dec->priv->num_out_bufs + min;
  if (min >= FRM_BUF_POOL_SIZE) {
    gst_object_unref (allocator);
    gst_object_unref (pool);
    GST_ERROR_OBJECT (dec, "min pool size cannot be greater than %d",
        FRM_BUF_POOL_SIZE - 1);
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
  gst_buffer_pool_config_set_allocator (config, allocator, &params);

  GST_DEBUG_OBJECT (decoder,
      "setting config %" GST_PTR_FORMAT " in pool %" GST_PTR_FORMAT, config,
      pool);

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

  if (!vvas_vcu_dec_outbuffer_alloc_and_map (dec)) {
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
  if (tmp_config)
    gst_structure_free (tmp_config);
  if (pool)
    gst_object_unref (pool);
  GST_ELEMENT_ERROR (decoder, RESOURCE, SETTINGS,
      ("Failed to configure the buffer pool"),
      ("Configuration is most likely invalid, please report this issue."));
  return FALSE;
}

static gboolean
vvas_xvcudec_prepare_send_frame (GstVvas_XVCUDec * dec, GstBuffer * inbuf,
    gsize insize, guint * payload_data, guint * payload_num_idx)
{
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  sk_payload_data *payload_buf;
  int iret = 0, i;
  guint num_idx = 0;
  gboolean has_free_oidx = TRUE;

  GST_LOG_OBJECT (dec, "sending input buffer index %d with size %lu",
      priv->host_to_dev_ibuf_idx, insize);

  /* update payload buf */
  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);
  memset (payload_buf, 0, priv->sk_payload_buf->size);

  payload_buf->cmd_id = VCU_PUSH;
  payload_buf->ibuff_valid_size = insize;
  payload_buf->ibuff_meta.pts = inbuf ? GST_BUFFER_PTS (inbuf) : -1;
  payload_buf->host_to_dev_ibuf_idx = priv->host_to_dev_ibuf_idx;

  memset (payload_data, 0, ERT_CMD_DATA_LEN * sizeof (int));
  payload_data[num_idx++] = 0;
  payload_data[num_idx++] = VCU_PUSH;
  payload_data[num_idx++] = getpid ();
  payload_data[num_idx++] = priv->timestamp & 0xFFFFFFFF;
  payload_data[num_idx++] = (priv->timestamp >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->sk_payload_buf->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->sk_payload_buf->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = sizeof (sk_payload_data);
  payload_data[num_idx++] =
      priv->in_xrt_bufs[priv->host_to_dev_ibuf_idx]->phy_addr & 0xFFFFFFFF;
  payload_data[num_idx++] =
      ((uint64_t) (priv->in_xrt_bufs[priv->
              host_to_dev_ibuf_idx]->phy_addr) >> 32) & 0xFFFFFFFF;
  payload_data[num_idx++] = priv->max_ibuf_size;

  GST_LOG_OBJECT (dec, "sending VCU_PUSH command to softkernel");

  /* reset all free out buf indexes */
  for (i = 0; i < FRM_BUF_POOL_SIZE; i++)
    payload_buf->obuf_info[i].freed_obuf_index = 0xBAD;

#ifdef HDR_DATA_SUPPORT
  /* reset all HDR buf indexes */
  for (i = 0; i < FRM_BUF_POOL_SIZE; i++) {
    payload_buf->hdrbuf_info[i].freed_obuf_index = 0xBAD;
  }
#endif

  g_mutex_lock (&priv->obuf_lock);

  payload_buf->valid_oidxs = g_list_length (priv->free_oidx_list);
  if (!dec->avoid_dynamic_alloc && !payload_buf->valid_oidxs
      && (priv->cur_skbuf_count < priv->min_skbuf_count)) {
    has_free_oidx = FALSE;
    payload_buf->valid_oidxs = priv->min_skbuf_count - priv->cur_skbuf_count;
    GST_LOG_OBJECT (dec, "allocating %d additional buffers",
        payload_buf->valid_oidxs);
  }

  for (i = 0; i < payload_buf->valid_oidxs; i++) {
    XlnxOutputBuffer *xlnxbuf = NULL;
    XlnxOutputBuffer *tmpbuf = NULL;
    XlnxOutputBuffer *newbuf = NULL;
    GstBuffer *outbuf = NULL;
    GstFlowReturn fret = GST_FLOW_OK;
    GstMemory *outmem = NULL;

    if (has_free_oidx)
      xlnxbuf = g_list_first (priv->free_oidx_list)->data;
    fret = gst_buffer_pool_acquire_buffer (priv->pool, &outbuf, NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (dec, "failed to acquire buffer from pool %p",
          priv->pool);
      g_mutex_unlock (&priv->obuf_lock);
      goto error;
    }

    outmem = gst_buffer_get_memory (outbuf, 0);
    if (!g_hash_table_contains (dec->priv->oidx_hash, outmem)) {
      if (dec->avoid_dynamic_alloc) {
        GST_ERROR_OBJECT (dec, "new output memory received %p", outmem);
        GST_ELEMENT_ERROR (dec, STREAM, FAILED, ("unexpected behaviour"),
            ("unexpected behaviour: new output memory received %p", outmem));
        gst_memory_unref (outmem);
        g_mutex_unlock (&priv->obuf_lock);
        goto error;
      } else {
        GST_LOG_OBJECT (dec, "new output memory received %p", outmem);
        newbuf = (XlnxOutputBuffer *) calloc (1, sizeof (XlnxOutputBuffer));
        if (newbuf == NULL) {
          GST_ERROR_OBJECT (dec, "failed to allocate decoder output buffer");
          g_mutex_unlock (&priv->obuf_lock);
          goto error;
        }

        newbuf->idx = g_hash_table_size (priv->oidx_hash);
        newbuf->xrt_buf.phy_addr = gst_vvas_allocator_get_paddr (outmem);
        newbuf->xrt_buf.size = gst_buffer_get_size (outbuf);
        newbuf->xrt_buf.bo = gst_vvas_allocator_get_bo (outmem);
        newbuf->gstbuf = outbuf;

        g_hash_table_insert (priv->oidx_hash, outmem, newbuf);
        priv->out_bufs_arr = (GstBuffer **) realloc (priv->out_bufs_arr,
            sizeof (GstBuffer *) * g_hash_table_size (priv->oidx_hash));
        if (!priv->out_bufs_arr) {
          GST_ERROR_OBJECT (dec, "failed to allocate memory");
          g_mutex_unlock (&priv->obuf_lock);
          goto error;
        }
        priv->out_bufs_arr[newbuf->idx] = outbuf;

        GST_DEBUG_OBJECT (dec,
            "output [%d] : mapping memory %p with paddr = %p", newbuf->idx,
            outmem, (void *) newbuf->xrt_buf.phy_addr);
      }
    }

    tmpbuf = g_hash_table_lookup (dec->priv->oidx_hash, outmem);
    priv->out_bufs_arr[tmpbuf->idx] = outbuf;
    GST_DEBUG_OBJECT (dec, "filling addr %p free out index %d in SEND command",
        (void *) tmpbuf->xrt_buf.phy_addr, tmpbuf->idx);

    payload_buf->obuf_info[i].freed_obuf_index = tmpbuf->idx;
    payload_buf->obuf_info[i].freed_obuf_paddr = tmpbuf->xrt_buf.phy_addr;
    payload_buf->obuf_info[i].freed_obuf_size = tmpbuf->xrt_buf.size;
    if (has_free_oidx)
      priv->free_oidx_list = g_list_remove (priv->free_oidx_list, xlnxbuf);
    gst_memory_unref (outmem);
    g_hash_table_insert (priv->out_buf_hash, GINT_TO_POINTER (tmpbuf->idx),
        outbuf);
    priv->cur_skbuf_count++;
  }

  /* Making NULL as we consumed all indexes */
  g_list_free (priv->free_oidx_list);
  priv->free_oidx_list = NULL;
  g_mutex_unlock (&priv->obuf_lock);

  /* transfer payload settings to device */
  iret = vvas_xrt_sync_bo (priv->sk_payload_buf->bo,
      VVAS_BO_SYNC_BO_TO_DEVICE, priv->sk_payload_buf->size, 0);
  if (iret != 0) {
    GST_ERROR_OBJECT (dec, "synbo failed - %d, reason : %s", iret,
        strerror (errno));
    GST_ELEMENT_ERROR (dec, RESOURCE, SYNC, NULL,
        ("failed to sync PUSH command payload to device. reason : %s",
            strerror (errno)));
    goto error;
  }

  *payload_num_idx = num_idx;

  return TRUE;

error:
  return FALSE;
}

static GstFlowReturn
gstvvas_xvcudec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstVvas_XVCUDec *dec = GST_VVAS_XVCUDEC (decoder);
  GstVvas_XVCUDecPrivate *priv = dec->priv;
  guint8 *indata = NULL;
  gsize insize = 0;
  GstFlowReturn fret = GST_FLOW_OK;
  sk_payload_data *payload_buf = NULL;
  unsigned int payload_data[ERT_CMD_DATA_LEN];
  guint num_idx = 0;
  int iret = 0;
  gboolean send_again = FALSE;
  gboolean bret = TRUE;
  GstBuffer *inbuf = NULL;

  GST_LOG_OBJECT (dec, "input %" GST_PTR_FORMAT, frame ? frame->input_buffer :
      NULL);
  if (!gst_pad_has_current_caps (GST_VIDEO_DECODER_SRC_PAD (dec))) {
    GstVideoInfo vinfo;
    GstCaps *outcaps = NULL;
    GstVideoCodecState *out_state = NULL;
    gboolean bret = TRUE;

    // TODO: add check for resolution change
    vinfo = dec->input_state->info;

    if (dec->bit_depth == 10)
      out_state =
          gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
          GST_VIDEO_FORMAT_NV12_10LE32, GST_VIDEO_INFO_WIDTH (&vinfo),
          GST_VIDEO_INFO_HEIGHT (&vinfo), dec->input_state);
    else
      out_state =
          gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
          GST_VIDEO_FORMAT_NV12, GST_VIDEO_INFO_WIDTH (&vinfo),
          GST_VIDEO_INFO_HEIGHT (&vinfo), dec->input_state);

    /* In case if one of the parameters is zero, base class will override with default
     * default values. To avoid this, we are passing on the same incoming colorimetry
     * information to downstream as well. Refer https://jira.xilinx.com/browse/CR-1114507
     * for more information 
     */
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
    GST_INFO_OBJECT (dec, "negotiated caps on source pad : %" GST_PTR_FORMAT,
        outcaps);
    gst_video_codec_state_unref (out_state);
    gst_caps_unref (outcaps);

    bret = vvas_xvcudec_init (dec);
    if (!bret) {
      GST_ERROR_OBJECT (dec, "%s :: vvas_xvcudec_init() failed \n", __func__);
      return GST_FLOW_NOT_NEGOTIATED;
    }

    memset (&dec->priv->last_rcvd_payload, 0x00, sizeof (sk_payload_data));
  }

  if (frame) {
    GstMapInfo map_info = GST_MAP_INFO_INIT;

    if (!gst_buffer_map (frame->input_buffer, &map_info, GST_MAP_READ)) {
      GST_ERROR_OBJECT (dec, "failed to map input buffer!");
      fret = GST_FLOW_ERROR;
      goto exit;
    }

    indata = map_info.data;
    insize = map_info.size;

    /* copy input frame to xrt memory */
    iret = vvas_xrt_write_bo (priv->in_xrt_bufs[priv->host_to_dev_ibuf_idx]->bo,
        indata, insize, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (dec, "failed to write input frame to xrt memory. "
          "reason : %s", strerror (errno));
      fret = GST_FLOW_ERROR;
      goto exit;
    }

    /* transfer input frame contents to device */
    iret = vvas_xrt_sync_bo (priv->in_xrt_bufs[priv->host_to_dev_ibuf_idx]->bo,
        VVAS_BO_SYNC_BO_TO_DEVICE, insize, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (dec, "failed to sync input frame. reason : %s",
          strerror (errno));
      GST_ELEMENT_ERROR (dec, RESOURCE, SYNC, NULL,
          ("failed to sync input buffer to device. reason : %s",
              strerror (errno)));
      fret = GST_FLOW_ERROR;
      goto exit;
    }

    gst_buffer_unmap (frame->input_buffer, &map_info);
  } else {
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

try_again:

  bret = vvas_xvcudec_prepare_send_frame (dec, inbuf, insize, payload_data,
      &num_idx);
  if (!bret) {
    GST_ERROR_OBJECT (dec, "failed to prepare send frame command");
    fret = GST_FLOW_ERROR;
    goto exit;
  }

  send_again = FALSE;
  payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);

  iret =
      vvas_xrt_send_softkernel_command (dec->priv->xcl_dev_handle,
      priv->ert_cmd_buf, payload_data, num_idx, dec->sk_cur_idx,
      CMD_EXEC_TIMEOUT);
  if (iret < 0) {
    GST_ERROR_OBJECT (dec,
        "failed to send VCU_PUSH command to softkernel - %d, " "reason : %s",
        iret, strerror (errno));
    fret = GST_FLOW_ERROR;
    goto exit;
  } else {
    bret = vvas_xvcudec_check_softkernel_response (dec, payload_buf);
    if (!bret) {
      GST_ERROR_OBJECT (dec, "softkernel send frame failed");
      GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, NULL,
          ("decoder softkernel send frame failed. reason : %s",
              payload_buf->dev_err));
      fret = GST_FLOW_ERROR;
      goto exit;
    }
    GST_LOG_OBJECT (dec, "successfully completed VCU_PUSH command");
  }

  if (payload_buf->dev_to_host_ibuf_idx != 0xBAD) {
    priv->host_to_dev_ibuf_idx = payload_buf->dev_to_host_ibuf_idx;
    GST_DEBUG_OBJECT (dec, "input buffer index %d consumed",
        priv->host_to_dev_ibuf_idx);
  } else {
    GST_DEBUG_OBJECT (dec, "input buffer index %d not consumed, try again...",
        priv->host_to_dev_ibuf_idx);
    send_again = TRUE;
  }

  fret = vvas_xvcudec_receive_out_frames (dec);
  if (fret != GST_FLOW_OK) {
    goto exit;
  }

  if (send_again) {
    guint num_free_obuf = 0;

    g_mutex_lock (&dec->priv->obuf_lock);
    num_free_obuf = g_list_length (dec->priv->free_oidx_list);

    if (num_free_obuf) {
      /* send again may get success when free outbufs available */
      GST_LOG_OBJECT (dec, "send free output buffers %d back to decoder",
          num_free_obuf);
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
      g_mutex_unlock (&dec->priv->obuf_lock);
      goto try_again;
    }
  }

exit:
  if (inbuf)
    gst_buffer_unref (inbuf);

  if (frame)
    gst_video_codec_frame_unref (frame);
  return fret;
}

static gboolean
gstvvas_xvcudec_flush (GstVideoDecoder * decoder)
{
  return vvas_xvcudec_send_flush (GST_VVAS_XVCUDEC (decoder));
}

static GstFlowReturn
gstvvas_xvcudec_finish (GstVideoDecoder * decoder)
{
  GstVvas_XVCUDec *dec = GST_VVAS_XVCUDEC (decoder);
  GstFlowReturn fret = GST_FLOW_OK;
  gboolean bret = FALSE;
  guint len = 0;
  GstVvas_XVCUDecPrivate *priv = dec->priv;

  GST_DEBUG_OBJECT (dec, "finish");

  if (!dec->priv->init_done)
    return GST_FLOW_OK;

  // TODO: add support when decoder not negotiated
  bret = vvas_xvcudec_send_flush (dec);
  if (!bret)
    goto error;

  do {
    g_mutex_lock (&dec->priv->obuf_lock);
    len = g_list_length (dec->priv->free_oidx_list);
    g_mutex_unlock (&dec->priv->obuf_lock);

    if (len) {
      GstVideoCodecFrame *frame =
          gst_video_decoder_get_oldest_frame (GST_VIDEO_DECODER (dec));
      unsigned int payload_data[ERT_CMD_DATA_LEN];
      sk_payload_data *payload_buf = NULL;
      guint num_idx = 0;
      gint iret = 0;

      if (!frame) {
        GST_WARNING_OBJECT (dec, "failed to get frame");
        break;
      }

      bret =
          vvas_xvcudec_prepare_send_frame (dec, frame->input_buffer, 0,
          payload_data, &num_idx);
      if (!bret)
        break;

      payload_buf = (sk_payload_data *) (priv->sk_payload_buf->user_ptr);

      iret =
          vvas_xrt_send_softkernel_command (dec->priv->xcl_dev_handle,
          priv->ert_cmd_buf, payload_data, num_idx, dec->sk_cur_idx,
          CMD_EXEC_TIMEOUT);
      if (iret < 0) {
        GST_ERROR_OBJECT (dec,
            "failed to send VCU_PUSH command to softkernel - %d, reason : %s",
            iret, strerror (errno));
        break;
      } else {
        bret = vvas_xvcudec_check_softkernel_response (dec, payload_buf);
        if (!bret) {
          GST_ERROR_OBJECT (dec, "softkernel send frame failed");
          GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, NULL,
              ("decoder softkernel send frame failed. reason : %s",
                  payload_buf->dev_err));
          break;
        }
      }
      gst_video_codec_frame_unref (frame);
      GST_LOG_OBJECT (dec, "successfully completed VCU_PUSH command");
    }

    fret = vvas_xvcudec_receive_out_frames (dec);
  } while (fret == GST_FLOW_OK);

  /* release output buffers already sent to device */
  g_hash_table_foreach_remove (dec->priv->out_buf_hash,
      vvas_free_output_hash_value, dec);

  return fret;

error:
  return GST_FLOW_ERROR;
}

static void
gstvvas_xvcudec_finalize (GObject * object)
{
  GstVvas_XVCUDec *dec = GST_VVAS_XVCUDEC (object);
  GstVvas_XVCUDecPrivate *priv = dec->priv;
#ifndef ENABLE_XRM_SUPPORT
  if (dec->xclbin_path)
    g_free (dec->xclbin_path);
#endif

  g_cond_clear (&priv->obuf_cond);
  g_mutex_clear (&priv->obuf_lock);
  g_mutex_clear (&priv->pre_obuf_lock);

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

static void
gstvvas_xvcudec_class_init (GstVvas_XVCUDecClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstVideoDecoderClass *dec_class = GST_VIDEO_DECODER_CLASS (klass);

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &sink_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &src_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Xilinx VCU H264/H265 decoder", "Decoder/Video",
      "Xilinx H264/H265 Decoder", "Xilinx Inc., https://www.xilinx.com");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gstvvas_xvcudec_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gstvvas_xvcudec_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gstvvas_xvcudec_finalize);
  dec_class->start = GST_DEBUG_FUNCPTR (gstvvas_xvcudec_start);
  dec_class->stop = GST_DEBUG_FUNCPTR (gstvvas_xvcudec_stop);
  dec_class->set_format = GST_DEBUG_FUNCPTR (gstvvas_xvcudec_set_format);
  dec_class->finish = GST_DEBUG_FUNCPTR (gstvvas_xvcudec_finish);
  dec_class->flush = GST_DEBUG_FUNCPTR (gstvvas_xvcudec_flush);
  dec_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gstvvas_xvcudec_decide_allocation);
  dec_class->handle_frame = GST_DEBUG_FUNCPTR (gstvvas_xvcudec_handle_frame);

  g_object_class_install_property (gobject_class, PROP_NUM_ENTROPY_BUFFER,
      g_param_spec_uint ("num-entropy-buf", "Number of entropy buffers",
          "Number of entropy buffers", 2, 10, 2,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOW_LATENCY,
      g_param_spec_boolean ("low-latency", "Low latency enabled or not",
          "Whether to enable low latency or not",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_XCLBIN_LOCATION,
      g_param_spec_string ("xclbin-location", "xclbin file location",
          "Location of the xclbin to program device", NULL,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

#ifndef ENABLE_XRM_SUPPORT
  g_object_class_install_property (gobject_class, PROP_SK_CURRENT_INDEX,
      g_param_spec_int ("sk-cur-idx", "Current softkernel index",
          "Current softkernel index", -1, 31, DEFAULT_SK_CURRENT_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("dev-idx", "Device index",
          "Valid Device index is 0 to 31. Default value is set to -1 intentionally so that user provides the correct device index.",
          -1, 31, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_KERNEL_NAME,
      g_param_spec_string ("kernel-name", "VCU Decoder kernel name",
          "VCU Decoder kernel name", VVAS_VCUDEC_KERNEL_NAME_DEFAULT,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_AVOID_OUTPUT_COPY,
      g_param_spec_boolean ("avoid-output-copy",
          "Avoid output frames copy",
          "Avoid output frames copy even when downstream does not support "
          "GstVideoMeta metadata",
          VVAS_VCUDEC_AVOID_OUTPUT_COPY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Fixed number of output buffers will be pre-allocated if this is TRUE */
  g_object_class_install_property (gobject_class, PROP_AVOID_DYNAMIC_ALLOC,
      g_param_spec_boolean ("avoid-dynamic-alloc",
          "Avoid dynamic allocation",
          "Avoid dynamic allocation of output buffers", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
#ifdef ENABLE_XRM_SUPPORT
  g_object_class_install_property (gobject_class, PROP_RESERVATION_ID,
      g_param_spec_uint64 ("reservation-id", "XRM reservation id",
          "Resource Pool Reservation id", 0, G_MAXUINT64, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
#endif

  g_object_class_install_property (gobject_class, PROP_SPLITBUFF_MODE,
      g_param_spec_boolean ("splitbuff-mode",
          "Whether to enable splitbuff mode or not",
          "Whether to enable splitbuff mode or not", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DISABLE_HDR_SEI,
      g_param_spec_boolean ("disable-hdr10-sei",
          "Whether to passthrough HDR10/10+ SEI messages or not",
          "Whether to passthrough HDR10/10+ SEI messages or not", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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

  g_object_class_install_property (gobject_class, PROP_INTERPOLATE_TIMESTAMPS,
      g_param_spec_boolean ("interpolate-timestamps", "Interpolate timestamps",
          "Interpolate PTS of output buffers",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gstvvas_xvcudec_debug_category, "vvas_xvcudec", 0,
      "debug category for vcu h264/h265 decoder element");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
}

static void
gstvvas_xvcudec_init (GstVvas_XVCUDec * dec)
{
  GstVvas_XVCUDecPrivate *priv = GST_VVAS_XVCUDEC_PRIVATE (dec);
  dec->priv = priv;
  dec->bit_depth = 8;
  dec->num_entropy_bufs = 2;
  dec->sk_start_idx = -1;
  dec->dev_index = DEFAULT_DEVICE_INDEX;
  dec->sk_cur_idx = DEFAULT_SK_CURRENT_INDEX;
  dec->kernel_name = g_strdup (VVAS_VCUDEC_KERNEL_NAME_DEFAULT);
  dec->avoid_output_copy = VVAS_VCUDEC_AVOID_OUTPUT_COPY_DEFAULT;
  dec->input_state = NULL;
  dec->splitbuff_mode = FALSE;
  dec->avoid_dynamic_alloc = TRUE;
  dec->disable_hdr10_sei = FALSE;
  dec->in_mem_bank = DEFAULT_MEM_BANK;
  dec->out_mem_bank = DEFAULT_MEM_BANK;
  dec->interpolate_timestamps = FALSE;
  priv->cu_idx = -1;
  priv->mem_released_handler = 0;
  priv->allocator = NULL;
  priv->dec_out_bufs_handle = NULL;
#ifdef HDR_DATA_SUPPORT
  priv->hdr_out_bufs_handle = NULL;
#endif
  priv->dev_handle = NULL;
  g_mutex_init (&priv->pre_obuf_lock);
  g_mutex_init (&priv->obuf_lock);
  g_cond_init (&priv->obuf_cond);
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (dec), TRUE);
  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (dec), TRUE);
#ifdef ENABLE_XRM_SUPPORT
  priv->reservation_id = 0;
#endif
  vvas_xvcudec_reset (dec);
}

#ifndef PACKAGE
#define PACKAGE "vvas_xvcudec"
#endif

static gboolean
vcu_dec_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "vvas_xvcudec", GST_RANK_NONE,
      GST_TYPE_VVAS_XVCUDEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xvcudec,
    "Xilinx VCU H264/H264 Decoder plugin", vcu_dec_init, VVAS_API_VERSION,
    GST_LICENSE_UNKNOWN, "GStreamer Xilinx", "http://xilinx.com/")
