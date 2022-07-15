/*
* Copyright (C) 2020 - 2022 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software
* is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
* KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
* EVENT SHALL XILINX BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
* OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE. Except as contained in this notice, the name of the Xilinx shall
* not be used in advertising or otherwise to promote the sale, use or other
* dealings in this Software without prior written authorization from Xilinx.
*/

/*
 * TODO:
 *
 * - Improvement : instead of multiple memories in vqinfo single memory with offsets
 * - Question : lookahead accepting stride & elevation alignment like VCU. How about input source is not VCU ?
 * - XRM integration
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#undef USE_XRM                  // for PCie based platforms use XRM
#define USE_DMABUF 0

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/vvas/gstvvasallocator.h>
#include <gst/vvas/gstvvaslameta.h>
#include <gst/vvas/gstvvascommon.h>
#include "gstvvas_xlookahead.h"
#include <experimental/xrt-next.h>
#include "krnl_mot_est_hw.h"
#include "la_algo/xlnx_aq_core.h"
#include "la_algo/xlnx_la_defines.h"

#ifdef ENABLE_XRM_SUPPORT
#include <xrm.h>
#include <dlfcn.h>
#include <jansson.h>
#include <xrm_limits.h>
#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))
#endif

GST_DEBUG_CATEGORY_STATIC (gst_vvas_xlookahead_debug);
#define GST_CAT_DEFAULT gst_vvas_xlookahead_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

#undef DUMP_LA_META
#ifdef DUMP_LA_META
#include <stdio.h>
FILE *qp_fp = NULL;
FILE *fsfa_fp = NULL;
#endif
/*******************************************
 ************** Default Values *************
 *******************************************/
#define DEFAULT_SPATIAL_AQ TRUE
#define DEFAULT_TEMPORAL_AQ TRUE
#define DEFAULT_RATE_CONTROL FALSE
#define DEFAULT_SPATIAL_AQ_GAIN_PERCENT 50
#define DEFAULT_CODEC_TYPE VVAS_CODEC_NONE
#define DEFAULT_NUM_B_FRAMES 2
#define UNSET_NUM_B_FRAMES -1
#define DEFAULT_GOP_SIZE 120
#define DEFAULT_DEVICE_INDEX -1
#define VVAS_LOOKAHEAD_KERNEL_NAME_DEFAULT "lookahead:{lookahead_1}"
#define VVAS_LOOKAHEAD_QUERY_NAME "VVASLookaheadQuery"
#define VVAS_ENCODER_QUERY_NAME "VVASEncoderQuery"
#define VVAS_LOOKAHEAD_DEFAULT_ENABLE_PIPLINE FALSE
#define VVAS_LOOKAHEAD_DEFAULT_DYNAMIC_GOP FALSE
#define STOP_COMMAND ((gpointer)GINT_TO_POINTER (g_quark_from_string("STOP")))

#define CMD_EXEC_TIMEOUT 1000   // 1 sec
#define MIN_POOL_BUFFERS 2
#define LINMEM_ALIGN_4K    4096
#define SCLEVEL1 2
#define XLNX_MAX_LOOKAHEAD_DEPTH 20
#define DEFAULT_LOOK_AHEAD_DEPTH 8
#define XLNX_ALIGN(x,LINE_SIZE) (((((size_t)x) + ((size_t)LINE_SIZE - 1)) & (~((size_t)LINE_SIZE - 1))))
#define BLOCK_WIDTH 4
#define BLOCK_HEIGHT 4
#define DYNAMIC_GOP_MIN_LOOKAHEAD_DEPTH 5

static const char *outFilePath = "delta_qpmap";

#include "vvas/xrt_utils.h"

typedef struct _GstVvas_XLookAheadPrivate GstVvas_XLookAheadPrivate;
typedef struct _Vvvas_incopy_object VvasIncopyObject;

enum
{
  PROP_0,
  PROP_DEVICE_INDEX,
  PROP_XCLBIN_LOCATION,
  PROP_LOOKAHEAD_DEPTH,
  PROP_SPATIAL_AQ,
  PROP_TEMPORAL_AQ,
  PROP_SPATIAL_AQ_GAIN,
  PROP_CODEC_TYPE,
  PROP_NUM_BFRAMES,
  PROP_KERNEL_NAME,
  PROP_ENABLE_PIPELINE,
#ifdef ENABLE_XRM_SUPPORT
  PROP_RESERVATION_ID,
#endif
  PROP_IN_MEM_BANK,
  PROP_DYNAMIC_GOP,
};

struct _Vvvas_incopy_object
{
  GstBuffer *inbuf;
  GstBuffer *copy_inbuf;
};

typedef struct
{
  int width;
  int height;
  int stride;
  int write_mv;
  void *frm_buffer_ref_v;
  void *frm_buffer_srch_v;
  void *sad_v;
  void *mv_v;
  int skip_l2;
  void *var_v;
  void *act_v;
  int pixfmt;
} kernel_arg_list;

struct _GstVvas_XLookAheadPrivate
{
  kernel_arg_list arg_list;
  gint dev_idx;
  vvasDeviceHandle dev_handle;
  vvasKernelHandle kern_handle;
  vvasRunHandle run_handle;
  gchar *xclbin_path;
  gchar *kernel_name;
  GstFlowReturn last_fret;
  uuid_t xclbinId;
  GstVideoInfo *in_vinfo;
  GstBufferPool *input_pool;
  GstBufferPool *stats_pool;
  GstBufferPool *qpmap_pool;
  GstBufferPool *fsfa_pool;
  size_t min_offset, max_offset;
  GThread *postproc_thread;
  GQueue *inbuf_queue;
  GQueue *stats_queue;
  GMutex proc_lock;
  GCond proc_cond;
  guint num_mb;
  aq_config_t qpmap_cfg;
  guint gop_size;
  guint out_size;
  guint lookahead_depth;
  gboolean spatial_aq;
  gboolean temporal_aq;
  gboolean enable_rate_control;
  guint spatial_aq_gain;
  VvasCodecType codec_type;
  gint num_bframes;
  xlnx_aq_core_t qp_handle;
  GstBuffer *prev_inbuf;
  guint var_off;
  guint sad_off;
  guint act_off;
  guint mv_off;
  guint frame_num;
  gboolean stop;
  guint qpmap_out_size;
  gboolean is_eos;
  gboolean has_error;
  gboolean is_idr;
  gboolean enabled_pipeline;
  gboolean ultra_low_latency;
  GThread *input_copy_thread;
  GAsyncQueue *copy_inqueue;
  GAsyncQueue *copy_outqueue;
  gboolean is_first_frame;
#ifdef ENABLE_XRM_SUPPORT
  xrmContext xrm_ctx;
  xrmCuResource *cu_resource;
  xrmCuResourceV2 *cu_resource_v2;
  gint reservation_id;
  gint cur_load;
#endif
  guint in_mem_bank;
  gboolean dynamic_gop;
  guint bframes[XLNX_DYNAMIC_GOP_CACHE];
  gint frame_complexity[XLNX_DYNAMIC_GOP_INTERVAL];
};

#define VVAS_LOOKAHEAD_CAPS \
    "video/x-raw, " \
    "format = (string) {NV12, NV12_10LE32}, " \
    "width = (int) [ 1, 3840 ], " \
    "height = (int) [ 1, 2160 ], " \
    "framerate = " GST_VIDEO_FPS_RANGE

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VVAS_LOOKAHEAD_CAPS));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VVAS_LOOKAHEAD_CAPS));

#define gst_vvas_xlookahead_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_XLookAhead, gst_vvas_xlookahead,
    GST_TYPE_BASE_TRANSFORM);
#define GST_VVAS_XLOOKAHEAD_PRIVATE(self) (GstVvas_XLookAheadPrivate *) (gst_vvas_xlookahead_get_instance_private (self))

static void gst_vvas_xlookahead_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vvas_xlookahead_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_vvas_xlookahead_finalize (GObject * obj);
static GstFlowReturn gst_vvas_xlookahead_submit_input_buffer (GstBaseTransform *
    trans, gboolean discont, GstBuffer * inbuf);

GType
vvas_xlookahead_get_codec_type (void)
{
  static const GEnumValue codec_types[] = {
    {VVAS_CODEC_NONE, "VVAS_CODEC_NONE", "none"},
    {VVAS_CODEC_H264, "VVAS_CODEC_H264", "h264"},
    {VVAS_CODEC_H265, "VVAS_CODEC_H265", "h265"},
    {0, NULL, NULL}
  };
  static volatile GType codec_type = 0;

  if (g_once_init_enter ((gsize *) & codec_type)) {
    GType _id;

    _id = g_enum_register_static ("VvasLACodecType", codec_types);

    g_once_init_leave ((gsize *) & codec_type, _id);
  }
  return codec_type;
}

static inline size_t
xlnx_align_to_base (size_t s, size_t align_base)
{
  return ((s + align_base - 1) & (~(align_base - 1)));
}

static void
vvas_xlookahead_copy_object_unref (gpointer data)
{
  VvasIncopyObject *copy_obj = (VvasIncopyObject *) data;
  if (copy_obj->inbuf)
    gst_buffer_unref (copy_obj->inbuf);
  if (copy_obj->copy_inbuf)
    gst_buffer_unref (copy_obj->copy_inbuf);
  g_slice_free (VvasIncopyObject, copy_obj);
}

static size_t
vvas_xlookahead_get_outsize (GstVvas_XLookAheadPrivate * priv, uint32_t num_b,
    size_t * length)
{
  uint32_t Bpb = 0;
  size_t totalSize = 0;

  priv->sad_off = 0;
  Bpb = 2;
  totalSize = xlnx_align_to_base (num_b * Bpb, LINMEM_ALIGN_4K);

  priv->act_off = totalSize;
  totalSize *= 2;

  priv->var_off = totalSize;
  Bpb = 4;
  totalSize += xlnx_align_to_base (num_b * Bpb, LINMEM_ALIGN_4K);

  priv->mv_off = totalSize;
  totalSize += xlnx_align_to_base (num_b * Bpb, LINMEM_ALIGN_4K);
  *length = totalSize;

  return totalSize;
}

static gboolean
vvas_xlookahead_allocate_input_pool (GstVvas_XLookAhead * self)
{
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstCaps *caps = NULL;

  caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM (self)->sinkpad);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  pool = gst_video_buffer_pool_new ();
  GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

  allocator = gst_vvas_allocator_new (self->priv->dev_idx,
      USE_DMABUF, self->priv->in_mem_bank, self->priv->kern_handle);
  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (&info),
      MIN_POOL_BUFFERS, 0);
  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config on input pool");
    goto error;
  }

  if (allocator)
    gst_object_unref (allocator);

  if (self->priv->input_pool)
    gst_object_unref (self->priv->input_pool);

  self->priv->input_pool = pool;

  GST_INFO_OBJECT (self, "allocated %" GST_PTR_FORMAT " pool",
      self->priv->input_pool);
  gst_caps_unref (caps);

  return TRUE;

error:
  if (allocator)
    gst_object_unref (allocator);
  gst_caps_unref (caps);
  return FALSE;
}

static gboolean
vvas_xlookahead_allocate_stats_pool (GstVvas_XLookAhead * self, guint out_size)
{
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;

  pool = gst_buffer_pool_new ();

  allocator = gst_vvas_allocator_new (self->priv->dev_idx,
      USE_DMABUF, self->priv->in_mem_bank, self->priv->kern_handle);
  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, NULL, out_size, MIN_POOL_BUFFERS,
      self->priv->lookahead_depth + 1);
  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config on stats pool");
    return FALSE;
  }

  if (self->priv->stats_pool)
    gst_object_unref (self->priv->stats_pool);

  self->priv->stats_pool = pool;

  gst_object_unref (allocator);
  GST_INFO_OBJECT (self, "allocated %" GST_PTR_FORMAT " stats pool",
      self->priv->stats_pool);

  return TRUE;
}

static gboolean
vvas_xlookahead_allocate_metadata_pools (GstVvas_XLookAhead * self)
{
  GstStructure *config;
  GstAllocationParams alloc_params;

  if (self->priv->qpmap_pool) {
    if (!gst_buffer_pool_is_active (self->priv->qpmap_pool))
      gst_buffer_pool_set_active (self->priv->qpmap_pool, FALSE);
    gst_object_unref (self->priv->qpmap_pool);
  }

  self->priv->qpmap_pool = gst_buffer_pool_new ();
  config = gst_buffer_pool_get_config (self->priv->qpmap_pool);

  gst_allocation_params_init (&alloc_params);
  gst_buffer_pool_config_set_params (config, NULL, self->priv->qpmap_out_size,
      self->priv->lookahead_depth, 0);
  gst_buffer_pool_config_set_allocator (config, NULL, &alloc_params);

  if (!gst_buffer_pool_set_config (self->priv->qpmap_pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config on qpmap pool");
    return FALSE;
  }

  if (!gst_buffer_pool_is_active (self->priv->qpmap_pool))
    gst_buffer_pool_set_active (self->priv->qpmap_pool, TRUE);

  GST_INFO_OBJECT (self, "allocated qpmap buffer pool %" GST_PTR_FORMAT,
      self->priv->qpmap_pool);

  if (self->priv->enable_rate_control) {

    if (self->priv->fsfa_pool) {
      if (!gst_buffer_pool_is_active (self->priv->fsfa_pool))
        gst_buffer_pool_set_active (self->priv->fsfa_pool, FALSE);
      gst_object_unref (self->priv->fsfa_pool);
    }

    self->priv->fsfa_pool = gst_buffer_pool_new ();
    config = gst_buffer_pool_get_config (self->priv->fsfa_pool);

    gst_allocation_params_init (&alloc_params);
    gst_buffer_pool_config_set_params (config, NULL,
        sizeof (xlnx_rc_fsfa_t) * self->priv->lookahead_depth,
        self->priv->lookahead_depth + 1, 0);
    gst_buffer_pool_config_set_allocator (config, NULL, &alloc_params);

    if (!gst_buffer_pool_set_config (self->priv->fsfa_pool, config)) {
      GST_ERROR_OBJECT (self, "Failed to set config on fsfa pool");
      return FALSE;
    }

    if (!gst_buffer_pool_is_active (self->priv->fsfa_pool))
      gst_buffer_pool_set_active (self->priv->fsfa_pool, TRUE);

    GST_INFO_OBJECT (self, "allocated fsfa buffer pool %" GST_PTR_FORMAT,
        self->priv->fsfa_pool);
  }

  return TRUE;
}

#ifdef ENABLE_XRM_SUPPORT
static gchar *
vvas_xlookahead_prepare_request_json_string (GstVvas_XLookAhead * self,
    GstVideoInfo * in_vinfo)
{
  json_t *req_obj;
  gchar *req_str;
  guint fps_n, fps_d;
  guint in_width, in_height;

  in_width = GST_VIDEO_INFO_WIDTH (in_vinfo);
  in_height = GST_VIDEO_INFO_HEIGHT (in_vinfo);

  if (!in_width || !in_height) {
    GST_WARNING_OBJECT (self, "input width & height not available. returning");
    return FALSE;
  }

  fps_n = GST_VIDEO_INFO_FPS_N (in_vinfo);
  fps_d = GST_VIDEO_INFO_FPS_D (in_vinfo);

  if (!fps_n) {
    g_warning ("frame rate not available in caps, taking default fps as 60");
    fps_n = 60;
    fps_d = 1;
  }

  req_obj = json_pack ("{s:{s:{s:[{s:s,s:s,s:{s:{s:i,s:i,s:{s:i,s:i}}}}]}}}",
      "request", "parameters", "resources", "function", "ENCODER",
      "format", self->priv->codec_type == VVAS_CODEC_H264 ? "H264" : "H265",
      "resolution", "input", "width", in_width, "height", in_height,
      "frame-rate", "num", fps_n, "den", fps_d);

  req_str = json_dumps (req_obj, JSON_DECODE_ANY);
  json_decref (req_obj);

  GST_LOG_OBJECT (self, "prepared xrm request %s", req_str);

  return req_str;
}

static gboolean
vvas_xlookahead_calculate_load (GstVvas_XLookAhead * self, gint * load,
    GstVideoInfo * vinfo)
{
  GstVvas_XLookAheadPrivate *priv = self->priv;
  int iret = -1, func_id = 0;
  gchar *req_str, *save_ptr = NULL;
  char plugin_name[XRM_MAX_NAME_LEN];
  xrmPluginFuncParam param;

  if (!priv->xrm_ctx) {
    GST_ERROR_OBJECT (self, "xrm context not created");
    return FALSE;
  }

  /* prepare json string to request xrm for load */
  req_str = vvas_xlookahead_prepare_request_json_string (self, vinfo);
  if (!req_str) {
    GST_ERROR_OBJECT (self, "failed to prepare xrm json request string");
    return FALSE;
  }

  memset (&param, 0x0, sizeof (xrmPluginFuncParam));
  memset (plugin_name, 0x0, XRM_MAX_NAME_LEN);

  strcpy (plugin_name, "xrmU30EncPlugin");

  if (strlen (req_str) > (XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1)) {
    GST_ERROR_OBJECT (self, "request input string length %lu > max allowed %d",
        strlen (req_str), XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1);
    return FALSE;
  }

  strncpy (param.input, req_str, XRM_MAX_PLUGIN_FUNC_PARAM_LEN);
  free (req_str);

  iret = xrmExecPluginFunc (priv->xrm_ctx, plugin_name, func_id, &param);
  if (iret != XRM_SUCCESS) {
    GST_ERROR_OBJECT (self, "failed to get load from xrm plugin. err : %d",
        iret);
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to get load from xrm plugin"), NULL);
    return FALSE;
  }

  /* skip encoder load & number of encoders in payload */
  strtok_r (param.output, " ", &save_ptr);
  strtok_r (NULL, " ", &save_ptr);

  /* taking la load in param.output */
  *load = atoi ((char *) (strtok_r (NULL, " ", &save_ptr)));

  if (*load <= 0 || *load > XRM_MAX_CHAN_LOAD_GRANULARITY_1000000) {
    GST_ERROR_OBJECT (self, "not an allowed lookahead load %d", *load);
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
        ("wrong lookahead load %d", *load), NULL);
    return FALSE;
  }

  GST_INFO_OBJECT (self, "need %d%% device's load",
      (*load * 100) / XRM_MAX_CHAN_LOAD_GRANULARITY_1000000);
  return TRUE;
}

static gboolean
vvas_xlookahead_allocate_resource (GstVvas_XLookAhead * self, gint la_load)
{
  GstVvas_XLookAheadPrivate *priv = self->priv;
  int iret = -1;

  GST_INFO_OBJECT (self, "going to request %d%% load using xrm",
      (la_load * 100) / XRM_MAX_CHAN_LOAD_GRANULARITY_1000000);

  if (getenv ("XRM_RESERVE_ID") || priv->reservation_id) {      /* use reservation_id to allocate LA */
    int xrm_reserve_id = 0;
    xrmCuPropertyV2 la_prop;
    xrmCuResourceV2 *cu_resource;

    memset (&la_prop, 0, sizeof (xrmCuPropertyV2));

    if (!priv->cu_resource_v2) {
      cu_resource = (xrmCuResourceV2 *) calloc (1, sizeof (xrmCuResourceV2));
      if (!cu_resource) {
        GST_ERROR_OBJECT (self,
            "failed to allocate memory for hardCU resource");
        return FALSE;
      }
    } else {
      cu_resource = priv->cu_resource_v2;
    }

    /* element property value takes higher priority than env variable */
    if (priv->reservation_id)
      xrm_reserve_id = priv->reservation_id;
    else
      xrm_reserve_id = atoi (getenv ("XRM_RESERVE_ID"));

    la_prop.poolId = xrm_reserve_id;
    strcpy (la_prop.kernelName, strtok (priv->kernel_name, ":"));
    strcpy (la_prop.kernelAlias, "LOOKAHEAD_MPSOC");
    la_prop.devExcl = false;
    la_prop.requestLoad = XRM_PRECISION_1000000_BIT_MASK (la_load);

    if (priv->dev_idx != -1) {
      uint64_t deviceInfoContraintType =
          XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX;
      uint64_t deviceInfoDeviceIndex = priv->dev_idx;

      la_prop.deviceInfo =
          (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) |
          (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
    }

    iret = xrmCuAllocV2 (priv->xrm_ctx, &la_prop, cu_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (self,
          "failed to allocate resources from reservation id %d",
          xrm_reserve_id);
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from reservation id %d",
              xrm_reserve_id), NULL);
      return FALSE;
    }
    priv->dev_idx = cu_resource->deviceId;
    uuid_copy (priv->xclbinId, cu_resource->uuid);
    priv->cu_resource_v2 = cu_resource;

  } else {                      /* use user specified device to allocate scaler */
    xrmCuProperty la_prop;
    xrmCuResource *cu_resource;

    memset (&la_prop, 0, sizeof (xrmCuProperty));

    if (!priv->cu_resource) {
      cu_resource = (xrmCuResource *) calloc (1, sizeof (xrmCuResource));
      if (!cu_resource) {
        GST_ERROR_OBJECT (self,
            "failed to allocate memory for hardCU resource");
        return FALSE;
      }
    } else {
      cu_resource = priv->cu_resource;
    }

    strcpy (la_prop.kernelName, strtok (priv->kernel_name, ":"));
    strcpy (la_prop.kernelAlias, "LOOKAHEAD_MPSOC");
    la_prop.devExcl = false;
    la_prop.requestLoad = XRM_PRECISION_1000000_BIT_MASK (la_load);

    iret = xrmCuAllocFromDev (priv->xrm_ctx, priv->dev_idx, &la_prop,
        cu_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (self, "failed to allocate resources from device id %d. "
          "error: %d", priv->dev_idx, iret);
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from device id %d", priv->dev_idx),
          NULL);
      return FALSE;
    }
    priv->dev_idx = cu_resource->deviceId;
    uuid_copy (priv->xclbinId, cu_resource->uuid);
    priv->cu_resource = cu_resource;

    if (priv->dev_idx != cu_resource->deviceId) {
      GST_ERROR_OBJECT (self, "invalid parameters received from XRM");
      return FALSE;
    }
  }

  GST_INFO_OBJECT (self, "dev_idx = %d, cu_idx = %d & load = %d",
      priv->dev_idx, priv->cu_resource->cuId,
      (la_load * 100) / XRM_MAX_CHAN_LOAD_GRANULARITY_1000000);

  return TRUE;
}

#endif



static gboolean
vvas_xlookahead_create_context (GstVvas_XLookAhead * self)
{
  GstVvas_XLookAheadPrivate *priv = self->priv;
#ifdef ENABLE_XRM_SUPPORT
  gboolean bret;

  /* gets cu index & device id (using reservation id) */
  bret = vvas_xlookahead_allocate_resource (self, priv->cur_load);
  if (!bret)
    return FALSE;
#endif

  if (!vvas_xrt_open_device (priv->dev_idx, &priv->dev_handle)) {
    GST_ERROR_OBJECT (self, "failed to open device index %u", priv->dev_idx);
    return FALSE;
  }

  if (!priv->xclbin_path) {
    GST_ERROR_OBJECT (self, "invalid xclbin path %s", priv->xclbin_path);
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, (NULL),
        ("xclbin path not set"));
    return FALSE;
  }

  /* TODO: Need to uncomment after CR-1122125 is resolved */
//#ifndef ENABLE_XRM_SUPPORT
  /* We have to download the xclbin irrespective of XRM or not as there
   * mismatch of UUID between XRM and XRT Native. CR-1122125 raised */
  if (vvas_xrt_download_xclbin (priv->xclbin_path,
          priv->dev_handle, &(priv->xclbinId))) {
    GST_ERROR_OBJECT (self, "failed to initialize XRT");
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("xclbin download failed"));
    return FALSE;
  }
//#endif

  if (vvas_xrt_open_context (priv->dev_handle, priv->xclbinId,
          &priv->kern_handle, priv->kernel_name, true)) {
    GST_ERROR_OBJECT (self, "failed to open XRT context ...");
    return FALSE;
  }

  return TRUE;
}

static gboolean
vvas_xlookahead_destroy_context (GstVvas_XLookAhead * self)
{
  GstVvas_XLookAheadPrivate *priv = self->priv;
  gboolean has_error = FALSE;
  gint iret;

#ifdef ENABLE_XRM_SUPPORT
  if (priv->cu_resource_v2) {
    gboolean bret;

    bret = xrmCuReleaseV2 (priv->xrm_ctx, priv->cu_resource_v2);
    if (!bret) {
      GST_ERROR_OBJECT (self, "failed to release CU");
      has_error = TRUE;
    }

    iret = xrmDestroyContext (priv->xrm_ctx);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (self, "failed to destroy xrm context");
      has_error = TRUE;
    }
    free (priv->cu_resource_v2);
    priv->cu_resource_v2 = NULL;
    GST_INFO_OBJECT (self, "released CU and destroyed xrm context");
  }

  if (priv->cu_resource) {
    gboolean bret;

    bret = xrmCuRelease (priv->xrm_ctx, priv->cu_resource);
    if (!bret) {
      GST_ERROR_OBJECT (self, "failed to release CU");
      has_error = TRUE;
    }

    iret = xrmDestroyContext (priv->xrm_ctx);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (self, "failed to destroy xrm context");
      has_error = TRUE;
    }
    free (priv->cu_resource);
    priv->cu_resource = NULL;
    GST_INFO_OBJECT (self, "released CU and destroyed xrm context");
  }
#endif

  if (priv->dev_handle) {
    iret = vvas_xrt_close_context (priv->kern_handle);
    if (iret != 0) {
      GST_ERROR_OBJECT (self, "failed to close xrt context");
      has_error = TRUE;
    }
    vvas_xrt_close_device (priv->dev_handle);
    priv->dev_handle = NULL;
    GST_INFO_OBJECT (self, "closed xrt context");
  }

  return has_error ? FALSE : TRUE;
}

static gpointer
vvas_xlookahead_input_copy_thread (gpointer data)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (data);
  GstVvas_XLookAheadPrivate *priv = self->priv;

  while (!priv->stop) {
    GstBuffer *inbuf = NULL, *own_inbuf = NULL;
    GstVideoFrame own_vframe, in_vframe;
    GstFlowReturn fret = GST_FLOW_OK;
    VvasIncopyObject *copy_inobj = NULL;

    copy_inobj = (VvasIncopyObject *) g_async_queue_pop (priv->copy_inqueue);
    if (copy_inobj == STOP_COMMAND) {
      GST_DEBUG_OBJECT (self, "received stop command. exit copy thread");
      break;
    }
    inbuf = copy_inobj->inbuf;

    /* acquire buffer from own input pool */
    fret =
        gst_buffer_pool_acquire_buffer (self->priv->input_pool, &own_inbuf,
        NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
          self->priv->input_pool);
      goto error;
    }
    GST_LOG_OBJECT (self, "acquired buffer %p from own pool", own_inbuf);

    /* map internal buffer in write mode */
    if (!gst_video_frame_map (&own_vframe, self->priv->in_vinfo, own_inbuf,
            GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "failed to map internal input buffer");
      goto error;
    }

    /* map input buffer in read mode */
    if (!gst_video_frame_map (&in_vframe, self->priv->in_vinfo, inbuf,
            GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "failed to map input buffer");
      goto error;
    }
    gst_video_frame_copy (&own_vframe, &in_vframe);
    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, self,
        "slow copy to internal input pool buffer");

    gst_video_frame_unmap (&in_vframe);
    gst_video_frame_unmap (&own_vframe);
    gst_buffer_copy_into (own_inbuf, inbuf,
        (GstBufferCopyFlags) GST_BUFFER_COPY_METADATA, 0, -1);

    copy_inobj->copy_inbuf = own_inbuf;
    g_async_queue_push (priv->copy_outqueue, copy_inobj);
  }

error:
  return NULL;
}

static gboolean
gst_vvas_xlookahead_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (trans);
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool;
  guint size;
  guint min = 0, max = 0;

  GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
      decide_query, query);

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);

  if (gst_query_get_n_allocation_pools (query)) {
    guint downsize;

    gst_query_parse_nth_allocation_pool (query, 0, &pool, &downsize, &min,
        &max);

    GST_DEBUG_OBJECT (self, "received pool %" GST_PTR_FORMAT, pool);
    GST_LOG_OBJECT (self, "pool %p requires min buffers %u and max buffers %u",
        pool, min, max);

    min = min + self->priv->lookahead_depth + 1;

    if (max)
      max = max + self->priv->lookahead_depth + 1;

    GST_INFO_OBJECT (self, "updated min buffers %u and max buffers = %u", min,
        max);

    /* update min and max buffers */
    gst_query_set_nth_allocation_pool (query, 0, pool, downsize, min, max);
    gst_object_unref (pool);
  } else {
    GstStructure *structure;
    GstAllocator *allocator = NULL;
    GstAllocationParams params =
        { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0, 0 };

    if (gst_query_get_n_allocation_params (query) > 0) {
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    } else {
      allocator = gst_vvas_allocator_new (self->priv->dev_idx,
          USE_DMABUF, self->priv->in_mem_bank, self->priv->kern_handle);
      gst_query_add_allocation_param (query, allocator, &params);
    }

    pool = gst_video_buffer_pool_new ();

    GST_LOG_OBJECT (self, "allocated internal pool %p", pool);

    structure = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (structure, caps, size,
        self->priv->lookahead_depth + 1, 0);
    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_set_allocator (structure, allocator, &params);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      gst_object_unref (pool);
      goto config_failed;
    }

    GST_OBJECT_LOCK (self);
    gst_query_add_allocation_pool (query, pool, size,
        self->priv->lookahead_depth + 1, 0);
    GST_OBJECT_UNLOCK (self);

    if (self->priv->input_pool)
      gst_object_unref (self->priv->input_pool);

    self->priv->input_pool = pool;
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

    GST_DEBUG_OBJECT (self, "prepared query %" GST_PTR_FORMAT, query);

    if (allocator)
      gst_object_unref (allocator);
  }

  return TRUE;

  /* ERRORS */
config_failed:
  {
    GST_ERROR_OBJECT (self, "failed to set config");
    return FALSE;
  }
}

static void
dynamic_gop_update_b_frames (GstVvas_XLookAhead * self)
{
  GstVvas_XLookAheadPrivate *priv = self->priv;
  guint b_frames = 1, it = 0;
  guint countLow = 0, countHigh = 0;

  /* Update for every fourth frame. */
  if (priv->frame_num <= 0
      || priv->frame_num % XLNX_DYNAMIC_GOP_INTERVAL !=
      (XLNX_DYNAMIC_GOP_INTERVAL - 1)) {
    return;
  }

  for (it = 0; it < XLNX_DYNAMIC_GOP_INTERVAL; it++) {
    if (priv->frame_complexity[it] == LOW_MOTION)
      countLow = countLow + 1;
    if (priv->frame_complexity[it] == HIGH_MOTION)
      countHigh = countHigh + 1;
  }

  if (priv->codec_type == VVAS_CODEC_H264) {
    if (countLow == 4) {        /* all frames are low motion */
      b_frames = 2;
    } else if (countHigh >= 1) {        /* atleast one of the frames is high motion */
      b_frames = 0;
    }
  } else {
    if (countLow >= 2) {        /* atleast 2 frames are low motion */
      b_frames = 2;
    } else if (countHigh == 4) {        /* all frames are high motion */
      b_frames = 0;
    }
  }

  priv->bframes[(priv->frame_num / XLNX_DYNAMIC_GOP_INTERVAL) %
      XLNX_DYNAMIC_GOP_CACHE] = b_frames;
  priv->qpmap_cfg.num_b_frames = b_frames;
  return;
}

static gpointer
gst_vvas_xlookahead_postproc_loop (gpointer data)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (data);
  GstVvas_XLookAheadPrivate *priv = self->priv;

  while (1) {
    GstBuffer *stats_buf = NULL;
    GstObject *obj = NULL;
    GstFlowReturn fret = GST_FLOW_OK;
    xlnx_frame_stats stats = { 0, };
    GstMapInfo map_info = GST_MAP_INFO_INIT;
    gboolean is_last_stat = FALSE;
    xlnx_status algo_status = EXlnxSuccess;
    xlnx_status mv_status = EXlnxSuccess;
    GstEvent *eos_event = NULL;
    GstVvasLAMeta *statslameta = NULL;

    /* Wait till queue has some thing */
    g_mutex_lock (&priv->proc_lock);

    if (priv->stop) {
      GST_DEBUG_OBJECT (self, "exiting processing loop...");
      g_mutex_unlock (&priv->proc_lock);
      break;
    }

    if (g_queue_is_empty (priv->stats_queue)) {
      GST_LOG_OBJECT (self, "stats queue is empty...wait for buffers");
      g_cond_wait (&priv->proc_cond, &priv->proc_lock);

      if (priv->stop) {
        GST_DEBUG_OBJECT (self, "exiting processing loop...");
        g_mutex_unlock (&priv->proc_lock);
        break;
      }
    }

    obj = g_queue_pop_head (priv->stats_queue);
    g_mutex_unlock (&priv->proc_lock);

    if (GST_IS_EVENT (obj)) {
      if (GST_EVENT_TYPE (GST_EVENT (obj)) == GST_EVENT_EOS) {
        priv->is_eos = TRUE;
        eos_event = GST_EVENT (obj);
        if (!priv->qp_handle)
          goto send_eos;
      }
    } else {
      stats_buf = GST_BUFFER (obj);
    }

    if (stats_buf) {
      statslameta = gst_buffer_get_vvas_la_meta (stats_buf);
      if (!statslameta) {
        GST_ERROR_OBJECT (self, "failed to get stats metadata");
        priv->last_fret = GST_FLOW_ERROR;
        gst_buffer_unref (stats_buf);
        continue;
      }

      priv->qpmap_cfg.spatial_aq_mode =
          statslameta->spatial_aq ? XLNX_AQ_SPATIAL_AUTOVARIANCE : XLNX_AQ_NONE;
      priv->qpmap_cfg.temporal_aq_mode =
          statslameta->temporal_aq ? XLNX_AQ_TEMPORAL_LINEAR : XLNX_AQ_NONE;
      priv->qpmap_cfg.spatial_aq_gain = statslameta->spatial_aq_gain;
      if (!priv->dynamic_gop) {
        priv->qpmap_cfg.num_b_frames = statslameta->num_bframes;
      }

      update_aq_modes (priv->qp_handle, &priv->qpmap_cfg);

      if (!gst_buffer_map (stats_buf, &map_info, GST_MAP_READ)) {
        GST_ERROR_OBJECT (self, "failed to map stats buffer!");
        priv->last_fret = GST_FLOW_ERROR;
        GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ,
            ("failed to map stats buffer."), ("failed to map stats buffer"));
        gst_buffer_unref (stats_buf);
        continue;
      }

      /* process stats buffer & attach AQ info as metadata to GstBuffer */
      stats.num_blocks = priv->num_mb;

      /* only SPATIAL_AQ_AUTOVARIANCE is supported */
      if (priv->spatial_aq)
        stats.var = (uint32_t *) (map_info.data + priv->var_off);

      /* Custom rate control uses frame sad + frame activity */
      if (priv->enable_rate_control)
        stats.act = (uint16_t *) (map_info.data + priv->act_off);

      stats.sad = (uint16_t *) (map_info.data + priv->sad_off);

      stats.mv = (uint32_t *) (map_info.data + priv->mv_off);

      if (priv->dynamic_gop) {
        if (stats.mv != NULL) {
          mv_status =
              generate_mv_histogram (priv->qp_handle, priv->frame_num, stats.mv,
              is_last_stat,
              &priv->frame_complexity[priv->frame_num %
                  XLNX_DYNAMIC_GOP_INTERVAL], statslameta->is_idr);
          if (mv_status != EXlnxSuccess && !is_last_stat) {
            priv->last_fret = GST_FLOW_ERROR;
            return NULL;
          }
        }
        dynamic_gop_update_b_frames (self);
      }

      if (send_frame_stats (priv->qp_handle, priv->dynamic_gop, priv->frame_num,
              &stats, is_last_stat, statslameta->is_idr) == EXlnxError) {
        priv->last_fret = GST_FLOW_ERROR;
        GST_ELEMENT_ERROR (self, STREAM, FAILED, ("FAILED QP map generation"),
            ("FAILED QP map generation"));
        gst_buffer_unmap (stats_buf, &map_info);
        gst_buffer_unref (stats_buf);
        break;
      }
      gst_buffer_unmap (stats_buf, &map_info);
      gst_buffer_unref (stats_buf);
    } else if (priv->is_eos) {
      GST_INFO_OBJECT (self, "sending last frame to lookahead on EOS");
      if (send_frame_stats (priv->qp_handle, priv->dynamic_gop, priv->frame_num,
              NULL, 1, 0) == EXlnxError) {
        priv->last_fret = GST_FLOW_ERROR;
        GST_ELEMENT_ERROR (self, STREAM, FAILED, ("FAILED QP map generation"),
            ("FAILED QP map generation"));
        break;
      }
    } else {
      priv->last_fret = GST_FLOW_ERROR;
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("unexpected operation."),
          ("unexpected operation"));
      break;
    }

    priv->frame_num++;

    do {
      GstBuffer *inbuf = NULL;
      xlnx_aq_info_t vqInfo;
      GstBuffer *qpmap_buf = NULL;
      GstBuffer *fsfa_buf = NULL;
      GstMapInfo qpmap_info = GST_MAP_INFO_INIT;
      GstMapInfo fsfa_info = GST_MAP_INFO_INIT;
      gboolean is_idr = 0;

      memset (&vqInfo, 0x0, sizeof (xlnx_aq_info_t));

      if (priv->qpmap_pool) {
        /* acquire buffer from own input pool */
        fret =
            gst_buffer_pool_acquire_buffer (self->priv->qpmap_pool, &qpmap_buf,
            NULL);
        if (fret != GST_FLOW_OK) {
          GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
              self->priv->qpmap_pool);
          priv->last_fret = fret;
          continue;
        }

        if (!gst_buffer_map (qpmap_buf, &qpmap_info, GST_MAP_WRITE)) {
          GST_ERROR_OBJECT (self, "failed to map qpmap buffer!");
          priv->last_fret = GST_FLOW_ERROR;
          GST_ELEMENT_ERROR (self, RESOURCE, OPEN_WRITE,
              ("failed to map qpmap buffer."), ("failed to map qpmap buffer"));
          gst_buffer_unref (qpmap_buf);
          continue;
        }

        vqInfo.qpmap.ptr = qpmap_info.data;
        vqInfo.qpmap.size = qpmap_info.size;
      }

      if (priv->fsfa_pool) {
        /* acquire buffer from own input pool */
        fret =
            gst_buffer_pool_acquire_buffer (self->priv->fsfa_pool, &fsfa_buf,
            NULL);
        if (fret != GST_FLOW_OK) {
          GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
              self->priv->fsfa_pool);
          priv->last_fret = fret;
          return NULL;
        }

        if (!gst_buffer_map (fsfa_buf, &fsfa_info, GST_MAP_WRITE)) {
          GST_ERROR_OBJECT (self, "failed to map fsfa_buf buffer!");
          priv->last_fret = GST_FLOW_ERROR;
          GST_ELEMENT_ERROR (self, RESOURCE, OPEN_WRITE,
              ("failed to map fsfa buffer."), ("failed to map fsfa buffer"));
          gst_buffer_unref (fsfa_buf);
          continue;
        }

        vqInfo.fsfa.ptr = fsfa_info.data;
        vqInfo.fsfa.size = fsfa_info.size;
      }

      if (statslameta)
        is_idr = statslameta->is_idr;
      algo_status =
          recv_frame_aq_info (priv->qp_handle, &vqInfo, priv->frame_num,
          is_idr);

      if (qpmap_buf) {
        gst_buffer_unmap (qpmap_buf, &qpmap_info);
      }
      if (fsfa_buf) {
        gst_buffer_unmap (fsfa_buf, &fsfa_info);
      }

      GST_LOG_OBJECT (self, "lookahead post processing returned %d",
          algo_status);

      if (EXlnxSuccess == algo_status) {
        GstVvasLAMeta *lameta = NULL;

        GST_DEBUG_OBJECT (self, "qpmaps generated for frame %lu",
            vqInfo.frame_num);

        g_mutex_lock (&priv->proc_lock);

        if (priv->stop) {
          GST_DEBUG_OBJECT (self, "exiting processing loop...");
          if (qpmap_buf)
            gst_buffer_unref (qpmap_buf);
          if (fsfa_buf)
            gst_buffer_unref (fsfa_buf);
          g_mutex_unlock (&priv->proc_lock);
          continue;
        }

        inbuf = (GstBuffer *) g_queue_pop_head (priv->inbuf_queue);
        g_cond_broadcast (&priv->proc_cond);
        g_mutex_unlock (&priv->proc_lock);

        /* attach vqinfo as metadata to buffer */
        lameta = gst_buffer_get_vvas_la_meta (inbuf);
        if (!lameta) {
          GST_ERROR_OBJECT (self, "failed to add metadata");
          priv->last_fret = GST_FLOW_ERROR;
          continue;
        }

        if (!priv->enable_rate_control) {
          lameta->rc_fsfa = NULL;
          if (fsfa_buf)
            gst_buffer_unref (fsfa_buf);
          fsfa_buf = NULL;
        }

        lameta->qpmap = qpmap_buf;
        lameta->rc_fsfa = fsfa_buf;
        if (priv->dynamic_gop)
          lameta->num_bframes =
              priv->bframes[(vqInfo.frame_num / XLNX_DYNAMIC_GOP_INTERVAL) %
              XLNX_DYNAMIC_GOP_CACHE];

#ifdef DUMP_LA_META
        {
          GstMapInfo qpmap_info = GST_MAP_INFO_INIT;
          GstMapInfo fsfa_info = GST_MAP_INFO_INIT;

          if (qpmap_buf) {
            if (!gst_buffer_map (qpmap_buf, &qpmap_info, GST_MAP_WRITE)) {
              GST_ERROR_OBJECT (self, "failed to map qpmap_buf buffer!");
              priv->last_fret = GST_FLOW_ERROR;
              GST_ELEMENT_ERROR (self, RESOURCE, OPEN_WRITE,
                  ("failed to map qpmap buffer."), ("failed to map qp buffer"));
              gst_buffer_unref (fsfa_buf);
              continue;
            }
            fwrite (qpmap_info.data, 1, qpmap_info.size, qp_fp);
            gst_buffer_unmap (qpmap_buf, &qpmap_info);
          }

          if (fsfa_buf) {
            if (!gst_buffer_map (fsfa_buf, &fsfa_info, GST_MAP_WRITE)) {
              GST_ERROR_OBJECT (self, "failed to map fsfa_buf buffer!");
              priv->last_fret = GST_FLOW_ERROR;
              GST_ELEMENT_ERROR (self, RESOURCE, OPEN_WRITE,
                  ("failed to map fsfa buffer."),
                  ("failed to map fsfa buffer"));
              gst_buffer_unref (fsfa_buf);
              continue;
            }
            fwrite (fsfa_info.data, 1, fsfa_info.size, fsfa_fp);
            gst_buffer_unmap (fsfa_buf, &fsfa_info);
          }
        }
#endif

        GST_LOG_OBJECT (self,
            "pushing buffer %" GST_PTR_FORMAT
            " with qpmap buffer %p and fsfa buffer %p", inbuf, lameta->qpmap,
            lameta->rc_fsfa);

        fret = gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (self), inbuf);

        if (priv->last_fret != fret) {
          switch (fret) {
            case GST_FLOW_ERROR:
            case GST_FLOW_NOT_LINKED:
            case GST_FLOW_NOT_NEGOTIATED:
              /* post error to application about error */
              GST_ELEMENT_ERROR (self, STREAM, FAILED,
                  ("failed to push buffer."),
                  ("failed to push buffer, reason %s (%d)",
                      gst_flow_get_name (fret), fret));
              break;
            default:
              break;
          }
        }
        priv->last_fret = fret;
      } else {
        if (qpmap_buf)
          gst_buffer_unref (qpmap_buf);
        if (fsfa_buf)
          gst_buffer_unref (fsfa_buf);
      }
    } while (EXlnxSuccess == algo_status);

  send_eos:
    if (eos_event) {
      gboolean bret;

      GST_INFO_OBJECT (self, "pushing event %" GST_PTR_FORMAT, eos_event);

      bret = gst_pad_push_event (GST_BASE_TRANSFORM_SRC_PAD (self), eos_event);
      if (!bret) {
        GST_ERROR_OBJECT (self, "failed to push event %" GST_PTR_FORMAT,
            eos_event);
      }
      g_mutex_lock (&priv->proc_lock);
      priv->stop = TRUE;
      g_mutex_unlock (&priv->proc_lock);
    }
  }

  return NULL;
}

static gboolean
gst_vvas_xlookahead_start (GstBaseTransform * trans)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (trans);
  GstVvas_XLookAheadPrivate *priv = self->priv;

  gst_video_info_init (priv->in_vinfo);
  priv->frame_num = 0;
  priv->is_first_frame = TRUE;

  if (!priv->lookahead_depth) {
    GST_ERROR_OBJECT (self, "lookahead-depth property is not set or zero."
        "valid range 1 to %d", XLNX_MAX_LOOKAHEAD_DEPTH);
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS, (NULL),
        ("lookahead-depth property is not set or zero. valid range 1 to %d",
            XLNX_MAX_LOOKAHEAD_DEPTH));
    return FALSE;
  }

  if (!priv->kernel_name) {
    GST_ERROR_OBJECT (self, "kernel name is not set");
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("kernel name is not set"));
    return FALSE;
  }
#ifdef ENABLE_XRM_SUPPORT
  priv->xrm_ctx = xrmCreateContext (XRM_API_VERSION_1);
  if (priv->xrm_ctx == NULL) {
    GST_ERROR_OBJECT (self, "failed to create context");
    return FALSE;
  }
#endif

  priv->stop = FALSE;

  g_mutex_init (&priv->proc_lock);
  g_cond_init (&priv->proc_cond);
  priv->inbuf_queue = g_queue_new ();

  /* Irrespective of whether spatial_aq/temporal_aq are enabled or disabled,
   * do post processing as they can be dynamically enabled or disabled
   */
  priv->stats_queue = g_queue_new ();

  /* start thread to process statsbuf &
   * attach Adaptive quantization info as metadata to input buffer */
  priv->postproc_thread = g_thread_new ("lookahead-postprocess",
      gst_vvas_xlookahead_postproc_loop, self);

  if (priv->enabled_pipeline) {
    priv->copy_inqueue =
        g_async_queue_new_full (vvas_xlookahead_copy_object_unref);
    priv->copy_outqueue =
        g_async_queue_new_full (vvas_xlookahead_copy_object_unref);

    priv->input_copy_thread = g_thread_new ("la-input-copy-thread",
        vvas_xlookahead_input_copy_thread, self);
  }
#ifdef DUMP_LA_META
  qp_fp = fopen ("/tmp/vvas_qpmap.dmp", "w+");
  fsfa_fp = fopen ("/tmp/vvas_fsfamap.dmp", "w+");
#endif

  return TRUE;
}

static void
vvas_xlookahead_free_allocated_pools (GstVvas_XLookAhead * self)
{
  GstVvas_XLookAheadPrivate *priv = self->priv;

  if (priv->stats_pool) {
    gst_buffer_pool_set_active (priv->stats_pool, FALSE);
    gst_clear_object (&priv->stats_pool);
    priv->stats_pool = NULL;
  }

  if (priv->input_pool) {
    gst_buffer_pool_set_active (priv->input_pool, FALSE);
    gst_clear_object (&priv->input_pool);
    priv->input_pool = NULL;
  }

  if (priv->qpmap_pool) {
    gst_buffer_pool_set_active (priv->qpmap_pool, FALSE);
    gst_clear_object (&priv->qpmap_pool);
    priv->qpmap_pool = NULL;
  }

  if (priv->fsfa_pool) {
    gst_buffer_pool_set_active (priv->fsfa_pool, FALSE);
    gst_clear_object (&priv->fsfa_pool);
    priv->fsfa_pool = NULL;
  }
}

static gboolean
gst_vvas_xlookahead_stop (GstBaseTransform * trans)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (trans);
  GstVvas_XLookAheadPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "stopping");

  g_mutex_lock (&priv->proc_lock);

  if (priv->is_eos) {
    /* wait till all frames are processed */
    while (!g_queue_is_empty (priv->inbuf_queue)) {
      GST_LOG_OBJECT (self, "inbuf queue not empty...wait");
      g_cond_wait (&priv->proc_cond, &priv->proc_lock);
    }
  } else {
    GstBuffer *buf;
    while (!g_queue_is_empty (priv->inbuf_queue)) {
      buf = (GstBuffer *) g_queue_pop_head (priv->inbuf_queue);
      gst_buffer_unref (buf);
    }

    while (priv->stats_queue && !g_queue_is_empty (priv->stats_queue)) {
      buf = (GstBuffer *) g_queue_pop_head (priv->stats_queue);
      gst_buffer_unref (buf);
    }
  }
  priv->stop = TRUE;
  g_cond_broadcast (&priv->proc_cond);
  g_mutex_unlock (&priv->proc_lock);

  if (priv->postproc_thread) {
    GST_LOG_OBJECT (self, "waiting for post-process thread join");
    g_thread_join (priv->postproc_thread);
    priv->postproc_thread = NULL;
  }
  priv->stop = FALSE;

  if (priv->prev_inbuf) {
    gst_buffer_unref (priv->prev_inbuf);
    priv->prev_inbuf = NULL;
  }

  if (priv->qp_handle) {
    destroy_aq_core (priv->qp_handle);
    priv->qp_handle = NULL;
  }

  vvas_xlookahead_free_allocated_pools (self);

  /* destroy xrm context */
  vvas_xlookahead_destroy_context (self);

  if (priv->stats_queue && !g_queue_is_empty (priv->stats_queue)) {
    GST_ERROR_OBJECT (self,
        "stats buffer queue is not empty.. unexpected behavior");
    return FALSE;
  }

  if (priv->stats_queue) {
    g_queue_free (priv->stats_queue);
    priv->stats_queue = NULL;
  }

  if (priv->inbuf_queue) {
    g_queue_free (priv->inbuf_queue);
    priv->inbuf_queue = NULL;
  }

  g_mutex_clear (&priv->proc_lock);
  g_cond_clear (&priv->proc_cond);

  if (priv->enabled_pipeline) {
    if (priv->input_copy_thread) {
      g_async_queue_push (priv->copy_inqueue, STOP_COMMAND);
      GST_LOG_OBJECT (self, "waiting for copy input thread join");
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
  GST_DEBUG_OBJECT (self, "stopped");

#ifdef DUMP_LA_META
  fclose (qp_fp);
  fclose (fsfa_fp);
#endif

  return TRUE;
}

static gboolean
vvas_xlookahead_init_core (GstVvas_XLookAhead * self, guint in_width,
    guint in_height)
{
  GstVvas_XLookAheadPrivate *priv = self->priv;
  guint out_width, out_height;
  aq_config_t *qpmapcfg = NULL;
  guint actual_mb_w, actual_mb_h;
  size_t length;
  guint qpmap_size, idx;
  xlnx_aq_dump_cfg dumpCfg;
  gboolean bret = FALSE;

  if (priv->codec_type == VVAS_CODEC_NONE) {
    GST_ERROR_OBJECT (self,
        "Not selected codec type. select codec type (h264 / h265)");
    return FALSE;
  }

  if (!priv->lookahead_depth && priv->temporal_aq) {
    GST_ERROR_OBJECT (self, "Invalid params: temporal AQ can't be enabled "
        " with lookahead depth = 0");
    return FALSE;
  }

  if (priv->dynamic_gop) {
    if (priv->lookahead_depth < DYNAMIC_GOP_MIN_LOOKAHEAD_DEPTH) {
      GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, NULL,
          ("Lookahead depth should be atleast %d when dynamic-gop is enabled",
              DYNAMIC_GOP_MIN_LOOKAHEAD_DEPTH));
      GST_ERROR_OBJECT (self,
          "Lookahead depth should be atleast %d when dynamic-gop is enabled",
          DYNAMIC_GOP_MIN_LOOKAHEAD_DEPTH);
      return FALSE;
    }
  }

  for (idx = 0; idx < XLNX_DYNAMIC_GOP_CACHE; idx++) {
    priv->bframes[idx] = priv->num_bframes;
  }

  out_width = XLNX_ALIGN ((in_width), 64) >> SCLEVEL1;
  out_height = XLNX_ALIGN ((in_height), 64) >> SCLEVEL1;
  actual_mb_w = xlnx_align_to_base (in_width, 16) / 16;
  actual_mb_h = xlnx_align_to_base (in_height, 16) / 16;
  qpmap_size = actual_mb_w * actual_mb_h;

  if (self->priv->codec_type == VVAS_CODEC_H265) {
    priv->qpmap_out_size = (xlnx_align_to_base (in_width, 32) *
        xlnx_align_to_base (in_height, 32)) / (32 * 32);
  } else {
    priv->qpmap_out_size = qpmap_size;
  }

  priv->num_mb = (out_width * out_height) / (BLOCK_WIDTH * BLOCK_HEIGHT);

  /* Initialize QP map config object */
  qpmapcfg = &priv->qpmap_cfg;
  qpmapcfg->width = in_width;
  qpmapcfg->height = in_height;
  qpmapcfg->actual_mb_w = actual_mb_w;
  qpmapcfg->actual_mb_h = actual_mb_h;
  qpmapcfg->outWidth = out_width;
  qpmapcfg->outHeight = out_height;
  qpmapcfg->blockWidth = BLOCK_WIDTH;
  qpmapcfg->blockHeight = BLOCK_HEIGHT;
  qpmapcfg->padded_mb_w = out_width / qpmapcfg->blockWidth;
  qpmapcfg->padded_mb_h = out_height / qpmapcfg->blockHeight;
  qpmapcfg->intraPeriod = priv->gop_size;
  qpmapcfg->la_depth = priv->lookahead_depth;
  qpmapcfg->spatial_aq_mode = priv->spatial_aq ? XLNX_AQ_SPATIAL_AUTOVARIANCE :
      XLNX_AQ_NONE;
  qpmapcfg->spatial_aq_gain = priv->spatial_aq_gain;
  qpmapcfg->temporal_aq_mode = priv->temporal_aq ? XLNX_AQ_TEMPORAL_LINEAR :
      XLNX_AQ_NONE;
  qpmapcfg->rate_control_mode = priv->enable_rate_control;
  qpmapcfg->num_mb = priv->num_mb;
  qpmapcfg->qpmap_size = qpmap_size;
  qpmapcfg->num_b_frames = priv->num_bframes;
  qpmapcfg->codec_type = priv->codec_type;

  dumpCfg.dumpDeltaQpMap = PRINT_FRAME_DELTAQP_MAP;
  dumpCfg.dumpDeltaQpMapHex = PRINT_HEX_FRAME_DELTAQP_MAP;
  dumpCfg.dumpBlockSAD = PRINT_BLOCK_SAD;
  dumpCfg.dumpFrameSAD = PRINT_FRAME_SAD;
  dumpCfg.outPath = outFilePath;

  priv->qp_handle = create_aq_core (qpmapcfg, &dumpCfg);
  if (!priv->qp_handle) {
    GST_ERROR_OBJECT (self, "failed to initialize AQ core");
    return FALSE;
  }

  GST_INFO_OBJECT (self, "AQ core init is success");

  priv->out_size = vvas_xlookahead_get_outsize (priv, priv->num_mb, &length);
  GST_INFO_OBJECT (self, "stats buffer size required %d and length %lu",
      priv->out_size, length);

  bret = vvas_xlookahead_allocate_metadata_pools (self);
  if (!bret) {
    GST_ERROR_OBJECT (self, "failed to create metadata pools");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vvas_xlookahead_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (trans);
  GstVvas_XLookAheadPrivate *priv = self->priv;
  gboolean bret = TRUE;
  GstVideoInfo vinfo;
  guint in_width, in_height, in_stride, in_bit_depth;
  GstQuery *enc_query;
  GstStructure *qstruct;
#ifdef ENABLE_XRM_SUPPORT
  gint load;

  if (priv->has_error)
    return FALSE;
#endif

  GST_INFO_OBJECT (self,
      "incaps = %" GST_PTR_FORMAT "and outcaps = %" GST_PTR_FORMAT, incaps,
      outcaps);

  if (priv->codec_type == VVAS_CODEC_NONE) {
    GST_ERROR_OBJECT (self, "codec type is not set");
    GST_ELEMENT_ERROR (self, STREAM, WRONG_TYPE,
        ("codec-type is not set. set it to either H264/H265"), NULL);
    return FALSE;
  }

  if (!gst_video_info_from_caps (&vinfo, incaps)) {
    GST_ERROR_OBJECT (self, "failed to parse input caps");
    return FALSE;
  }

  qstruct = gst_structure_new (VVAS_ENCODER_QUERY_NAME,
      "gop-length", G_TYPE_UINT, 0,
      "ultra-low-latency", G_TYPE_BOOLEAN, FALSE,
      "rc-mode", G_TYPE_BOOLEAN, FALSE, NULL);
  enc_query = gst_query_new_custom (GST_QUERY_CUSTOM, qstruct);
  bret = gst_pad_query (trans->sinkpad, enc_query);
  if (bret) {
    const GstStructure *mod_qstruct = gst_query_get_structure (enc_query);

    bret = gst_structure_get_uint (mod_qstruct, "gop-length", &priv->gop_size);
    if (!bret) {
      GST_ERROR_OBJECT (self, "failed to get gop-size from query");
      gst_query_unref (enc_query);
      return FALSE;
    }
    GST_INFO_OBJECT (self, "received gop-size %u from downstream",
        priv->gop_size);

    bret =
        gst_structure_get_boolean (mod_qstruct, "ultra-low-latency",
        &priv->ultra_low_latency);
    if (!bret) {
      GST_ERROR_OBJECT (self, "failed to get ultra-low-latency from query");
      gst_query_unref (enc_query);
      return FALSE;
    }
    GST_INFO_OBJECT (self, "received ultra-low-latency %u from downstream",
        priv->ultra_low_latency);

    bret =
        gst_structure_get_boolean (mod_qstruct, "rc-mode",
        &priv->enable_rate_control);
    if (!bret) {
      GST_ERROR_OBJECT (self, "failed to get rate-control from query");
      gst_query_unref (enc_query);
      return FALSE;
    }
    GST_INFO_OBJECT (self, "received rc-mode %u from downstream",
        priv->enable_rate_control);
  } else {
    GST_ERROR_OBJECT (self, "failed to send custom query");
    gst_query_unref (enc_query);
    return FALSE;
  }
  gst_query_unref (enc_query);

  if (priv->dynamic_gop && (priv->num_bframes != UNSET_NUM_B_FRAMES)) {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, NULL,
        ("dynamic gop is enabled, b-frames cannot be set"));
    return FALSE;
  }

  if (priv->ultra_low_latency && (priv->num_bframes == UNSET_NUM_B_FRAMES)) {
    priv->num_bframes = 0;
    GST_DEBUG_OBJECT (self,
        "setting b-frames to 0 as ultra-low-latency mode is enabled");
  }

  priv->num_bframes =
      priv->num_bframes ==
      UNSET_NUM_B_FRAMES ? DEFAULT_NUM_B_FRAMES : priv->num_bframes;

  if (priv->lookahead_depth > priv->gop_size) {
    GST_ERROR_OBJECT (self, "invalid parameters : lookahead depth %d is "
        "greater than gop size %d", priv->lookahead_depth, priv->gop_size);
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS,
        ("lookahead depth is greater than gop size"), NULL);
    return FALSE;
  }

  if (priv->num_bframes && priv->ultra_low_latency) {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS,
        ("b-frames cannot be encoded in ultra-low-latency mode"), NULL);
    return FALSE;
  }
#ifdef ENABLE_XRM_SUPPORT
  bret = vvas_xlookahead_calculate_load (self, &load, &vinfo);
  if (!bret) {
    priv->has_error = TRUE;
    return FALSE;
  }
  priv->cur_load = load;
#endif

  if (!priv->dev_handle) {
    /* create XRT context */
    bret = vvas_xlookahead_create_context (self);
    if (!bret) {
      priv->has_error = TRUE;
      return FALSE;
    }
  }

  self->priv->arg_list.write_mv = 1;

  in_width = GST_VIDEO_INFO_WIDTH (&vinfo);
  in_height = GST_VIDEO_INFO_HEIGHT (&vinfo);
  in_stride = GST_VIDEO_INFO_PLANE_STRIDE (&vinfo, 0);
  in_bit_depth = GST_VIDEO_INFO_COMP_DEPTH (&vinfo, 0);

  if ((GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo) != in_width) &&
      (GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo) != in_height) &&
      (GST_VIDEO_INFO_PLANE_STRIDE (self->priv->in_vinfo, 0) != in_stride)) {
    uint32_t skip_l2 = 0;
    uint32_t pixel_fmt = 0;

    GST_INFO_OBJECT (self,
        "new capabilities received : width = %u, height = %u," "stride = %u",
        in_width, in_height, in_stride);

    if (in_bit_depth == 10) {
      pixel_fmt = 1;
    } else if (in_bit_depth == 8) {
      pixel_fmt = 0;
    } else {
      GST_ERROR_OBJECT (self, "Unsupported bit depth");
      return FALSE;
    }

    if (priv->stats_queue) {
      if ((in_width >= MAX_HOR_RES) && (in_height >= MAX_VERT_RES)) {
        skip_l2 = 0;
      } else {
        skip_l2 = 1;
      }

      self->priv->arg_list.width = in_width;
      self->priv->arg_list.height = in_height;
      self->priv->arg_list.stride = in_stride;
      self->priv->arg_list.skip_l2 = skip_l2;
      self->priv->arg_list.pixfmt = pixel_fmt;

      bret = vvas_xlookahead_init_core (self, in_width, in_height);
      if (!bret)
        goto error;

      /* allocate pool for internal stats buffer pool */
      bret = vvas_xlookahead_allocate_stats_pool (self, priv->out_size);
      if (!bret)
        goto error;

      if (!gst_buffer_pool_is_active (priv->stats_pool))
        gst_buffer_pool_set_active (priv->stats_pool, TRUE);
    }

    if (self->priv->in_vinfo)
      gst_video_info_free (self->priv->in_vinfo);

    self->priv->in_vinfo = gst_video_info_copy (&vinfo);
  }

  return bret;

error:
  GST_ELEMENT_ERROR (self, STREAM, FAILED,
      ("failed to initialize lookahead core"), NULL);
  return FALSE;
}

static gboolean
gst_vvas_xlookahead_query (GstBaseTransform * trans, GstPadDirection direction,
    GstQuery * query)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (trans);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:{
      GstStructure *qstruct = NULL;

      GST_DEBUG_OBJECT (self, "received CUSTOM query");

      qstruct = gst_query_writable_structure (query);
      if (qstruct && !g_strcmp0 (gst_structure_get_name (qstruct),
              VVAS_LOOKAHEAD_QUERY_NAME)) {
        gst_structure_set (qstruct,
            "la-depth", G_TYPE_UINT, self->priv->lookahead_depth,
            "b-frames", G_TYPE_INT, self->priv->num_bframes, NULL);
        GST_INFO_OBJECT (self, "updating lookahead depth %u in query",
            self->priv->lookahead_depth);
        GST_INFO_OBJECT (self, "updating b-frames %d in query",
            self->priv->num_bframes);
        return TRUE;
      }
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

static gboolean
gst_vvas_xlookahead_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (trans);
  GstVvas_XLookAheadPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "received event: %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (priv->enabled_pipeline) {
        GstFlowReturn fret = GST_FLOW_OK;
        GST_INFO_OBJECT (self, "input copy queue has %d pending buffers",
            g_async_queue_length (priv->copy_outqueue));
        while (g_async_queue_length (priv->copy_outqueue) > 0) {
          fret = gst_vvas_xlookahead_submit_input_buffer (trans, FALSE, NULL);
          if (fret != GST_FLOW_OK)
            return FALSE;
        }
      }
      if (priv->stats_queue) {
        g_mutex_lock (&priv->proc_lock);
        if (priv->prev_inbuf) {
          gst_buffer_unref (priv->prev_inbuf);
          priv->prev_inbuf = NULL;
        }
        /* push EOS event to queue */
        g_queue_push_tail (priv->stats_queue, event);
        g_cond_broadcast (&priv->proc_cond);
        g_mutex_unlock (&priv->proc_lock);
        return TRUE;
      } else {
        break;
      }
    case GST_EVENT_CUSTOM_DOWNSTREAM:
      if (gst_video_event_is_force_key_unit (event)) {
        GstClockTime running_time;
        gboolean all_headers;
        guint count;

        if (gst_video_event_parse_downstream_force_key_unit (event,
                NULL, NULL, &running_time, &all_headers, &count)) {
          priv->is_idr = TRUE;
        }
        return TRUE;
      }
      break;
    default:
      break;
  }
  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static gboolean
vvas_xlookahead_copy_input_buffer (GstVvas_XLookAhead * self,
    GstBuffer ** inbuf, GstBuffer ** internal_inbuf)
{
  GstVvas_XLookAheadPrivate *priv = self->priv;
  GstBuffer *new_inbuf;
  GstFlowReturn fret;
  GstVideoFrame in_vframe, new_vframe;
  gboolean bret;

  memset (&in_vframe, 0x0, sizeof (GstVideoFrame));
  memset (&new_vframe, 0x0, sizeof (GstVideoFrame));

  if (!self->priv->input_pool) {
    /* allocate input internal pool */
    bret = vvas_xlookahead_allocate_input_pool (self);
    if (!bret)
      goto error;
  }

  if (!gst_buffer_pool_is_active (self->priv->input_pool))
    gst_buffer_pool_set_active (self->priv->input_pool, TRUE);

  if (priv->enabled_pipeline) {
    VvasIncopyObject *copy_outobj = NULL;
    VvasIncopyObject *copy_inobj = NULL;

    copy_outobj = g_async_queue_try_pop (priv->copy_outqueue);
    if (!copy_outobj && !priv->is_first_frame) {
      copy_outobj = g_async_queue_pop (priv->copy_outqueue);
    }

    priv->is_first_frame = FALSE;

    copy_inobj = g_slice_new0 (VvasIncopyObject);
    copy_inobj->inbuf = *inbuf;

    g_async_queue_push (priv->copy_inqueue, copy_inobj);

    if (!copy_outobj) {
      GST_LOG_OBJECT (self, "copied input buffer is not available. return");
      *internal_inbuf = NULL;
      return TRUE;
    }
    *internal_inbuf = copy_outobj->copy_inbuf;
    *inbuf = copy_outobj->inbuf;
    g_slice_free (VvasIncopyObject, copy_outobj);
  } else {
    /* acquire buffer from own input pool */
    fret = gst_buffer_pool_acquire_buffer (self->priv->input_pool, &new_inbuf,
        NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
          self->priv->input_pool);
      goto error;
    }
    GST_LOG_OBJECT (self, "acquired buffer %p from own pool", new_inbuf);

    /* map internal buffer in write mode */
    if (!gst_video_frame_map (&new_vframe, self->priv->in_vinfo, new_inbuf,
            GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "failed to map internal input buffer");
      goto error;
    }

    /* map input buffer in read mode */
    if (!gst_video_frame_map (&in_vframe, self->priv->in_vinfo, *inbuf,
            GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "failed to map input buffer");
      goto error;
    }
    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, self,
        "slow copy to internal input pool buffer");

    gst_video_frame_copy (&new_vframe, &in_vframe);
    gst_video_frame_unmap (&in_vframe);
    gst_video_frame_unmap (&new_vframe);
    gst_buffer_copy_into (new_inbuf, *inbuf,
        (GstBufferCopyFlags) GST_BUFFER_COPY_METADATA, 0, -1);
    *internal_inbuf = new_inbuf;
  }

  return TRUE;

error:
  if (in_vframe.data[0]) {
    gst_video_frame_unmap (&in_vframe);
  }
  if (new_vframe.data[0]) {
    gst_video_frame_unmap (&new_vframe);
  }
  return FALSE;
}

static int32_t
vvas_xlookahead_exec_buf (vvasDeviceHandle dev_handle,
    vvasKernelHandle kern_handle, vvasRunHandle * run_handle,
    const char *format, ...)
{
  va_list args;
  int32_t iret;

  va_start (args, format);

  iret = vvas_xrt_exec_buf (dev_handle, kern_handle, run_handle, format, args);

  va_end (args);

  return iret;
}



static GstFlowReturn
gst_vvas_xlookahead_submit_input_buffer (GstBaseTransform * trans,
    gboolean discont, GstBuffer * inbuf)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (trans);
  GstVvas_XLookAheadPrivate *priv = self->priv;
  GstMemory *in_mem = NULL, *stats_mem = NULL, *prev_in_mem = NULL;
  guint64 in_paddr = -1, stats_paddr = -1, prev_in_paddr;
  guint64 mv_paddr, var_paddr, act_paddr;
  gboolean bret = FALSE;
  GstVideoMeta *vmeta = NULL;
  GstBuffer *stats_buf, *kernel_inbuf = NULL;
  GstBuffer *w_inbuf = NULL, *w_stats_buf = NULL;
  GstVvasLAMeta *lameta = NULL;
  GstFlowReturn fret = GST_FLOW_ERROR;
  int iret = -1;
  guint num_bframes = 0, spatial_aq_gain = 0;
  gboolean spatial_aq = FALSE, temporal_aq = FALSE;
  gboolean unref_prev_buf = TRUE;
  int retry_count = MAX_EXEC_WAIT_RETRY_CNT;

  GST_LOG_OBJECT (self, "received %" GST_PTR_FORMAT, inbuf);

  if (inbuf) {
    in_mem = gst_buffer_get_memory (inbuf, 0);
    if (in_mem == NULL) {
      GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
      goto error;
    }

    if (gst_is_vvas_memory (in_mem)
        && gst_vvas_memory_can_avoid_copy (in_mem, priv->dev_idx,
            priv->in_mem_bank)) {
      kernel_inbuf = inbuf;
    } else {
      GST_DEBUG_OBJECT (self, "copy input buffer to internal pool buffer");
      bret = vvas_xlookahead_copy_input_buffer (self, &inbuf, &kernel_inbuf);
      if (!bret)
        goto error;

      gst_memory_unref (in_mem);

      if (!kernel_inbuf) {
        return GST_FLOW_OK;
      }

      in_mem = gst_buffer_get_memory (kernel_inbuf, 0);
      if (in_mem == NULL) {
        GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
        goto error;
      }
    }
  } else if (priv->enabled_pipeline &&
      g_async_queue_length (priv->copy_outqueue)) {
    VvasIncopyObject *copy_outobj = g_async_queue_pop (priv->copy_outqueue);

    inbuf = copy_outobj->inbuf;
    kernel_inbuf = copy_outobj->copy_inbuf;

    g_slice_free (VvasIncopyObject, copy_outobj);

    in_mem = gst_buffer_get_memory (kernel_inbuf, 0);
    if (in_mem == NULL) {
      GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
      goto error;
    }
  } else {
    GST_ERROR_OBJECT (self, "condition not handled");
    GST_ELEMENT_ERROR (self, STREAM, NOT_IMPLEMENTED, NULL,
        ("unexpected behaviour"));
    return GST_FLOW_ERROR;
  }

  vmeta = gst_buffer_get_video_meta (kernel_inbuf);
  if (vmeta) {
    /* update stride from videometa */
    self->priv->arg_list.stride = vmeta->stride[0];
  }

  bret = gst_vvas_memory_sync_bo (in_mem);
  if (!bret) {
    GST_ERROR_OBJECT (self, "failed to sync data");
    GST_ELEMENT_ERROR (self, RESOURCE, SYNC, NULL,
        ("failed to sync memory to device. reason : %s", strerror (errno)));
    goto error;
  }

  /* acquire buffer from stats pool */
  fret = gst_buffer_pool_acquire_buffer (priv->stats_pool, &stats_buf, NULL);
  if (fret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "failed to allocate buffer from stats pool %p",
        priv->stats_pool);
    goto error;
  }

  stats_mem = gst_buffer_get_memory (stats_buf, 0);
  if (stats_mem == NULL) {
    GST_ERROR_OBJECT (self, "failed to get memory from stats buffer");
    goto error;
  }

  stats_paddr = gst_vvas_allocator_get_paddr (stats_mem);
  if (!stats_paddr) {
    GST_ERROR_OBJECT (self, "failed to get physical address");
    goto error;
  }

  in_paddr = gst_vvas_allocator_get_paddr (in_mem);
  if (!in_paddr) {
    GST_ERROR_OBJECT (self, "failed to get physical address");
    goto error;
  }

  if (!priv->prev_inbuf) {
    priv->prev_inbuf = kernel_inbuf;
    unref_prev_buf = FALSE;
  }

  prev_in_mem = gst_buffer_get_memory (priv->prev_inbuf, 0);
  if (!prev_in_mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from previous input buffer");
    goto error;
  }

  prev_in_paddr = gst_vvas_allocator_get_paddr (prev_in_mem);
  if (!prev_in_paddr) {
    GST_ERROR_OBJECT (self, "failed to get physical address");
    goto error;
  }

  mv_paddr = stats_paddr + priv->mv_off;
  var_paddr = stats_paddr + priv->var_off;
  act_paddr = stats_paddr + priv->act_off;

  self->priv->arg_list.frm_buffer_srch_v = (void *) prev_in_paddr;
  self->priv->arg_list.frm_buffer_ref_v = (void *) in_paddr;
  self->priv->arg_list.sad_v = (void *) stats_paddr;
  self->priv->arg_list.mv_v = (void *) mv_paddr;
  self->priv->arg_list.var_v = (void *) var_paddr;
  self->priv->arg_list.act_v = (void *) act_paddr;


  /* submit command to XRT for kernel execution */
  iret = vvas_xlookahead_exec_buf (priv->dev_handle, priv->kern_handle,
      &priv->run_handle, "iiiippppippi",
      //"iiiibbbpippi",
      self->priv->arg_list.width,
      self->priv->arg_list.height,
      self->priv->arg_list.stride,
      self->priv->arg_list.write_mv,
      self->priv->arg_list.frm_buffer_ref_v,
      self->priv->arg_list.frm_buffer_srch_v,
      self->priv->arg_list.sad_v,
      self->priv->arg_list.mv_v,
      self->priv->arg_list.skip_l2,
      self->priv->arg_list.var_v,
      self->priv->arg_list.act_v, self->priv->arg_list.pixfmt);
  if (iret) {
    GST_ERROR_OBJECT (self, "failed to execute command %d", iret);
    goto error;
  }

  do {
    iret = vvas_xrt_exec_wait (priv->dev_handle, priv->run_handle,
        CMD_EXEC_TIMEOUT);
    /* Lets try for MAX count unless there is a error or completion */
    if (iret == ERT_CMD_STATE_TIMEOUT) {
      GST_WARNING_OBJECT (self, "Timeout...retry execwait");
      if (retry_count-- <= 0) {
        GST_ERROR_OBJECT (self,
            "Max retry count %d reached..returning error",
            MAX_EXEC_WAIT_RETRY_CNT);
        vvas_xrt_free_run_handle (priv->run_handle);
        goto error;
      }
    } else if (iret == ERT_CMD_STATE_ERROR) {
      GST_ERROR_OBJECT (self, "ExecWait ret = %d", iret);
      vvas_xrt_free_run_handle (priv->run_handle);
      goto error;
    }
  } while (iret != ERT_CMD_STATE_COMPLETED);

  vvas_xrt_free_run_handle (priv->run_handle);

  /* need to sync data from device */
  gst_vvas_memory_set_sync_flag (stats_mem, VVAS_SYNC_FROM_DEVICE);
  gst_memory_unref (in_mem);
  gst_memory_unref (prev_in_mem);
  gst_memory_unref (stats_mem);

  if (G_LIKELY (unref_prev_buf))
    gst_buffer_unref (priv->prev_inbuf);

  priv->prev_inbuf = gst_buffer_ref (kernel_inbuf);

  if (inbuf != kernel_inbuf)
    gst_buffer_unref (kernel_inbuf);

  num_bframes = priv->num_bframes;
  spatial_aq = priv->spatial_aq;
  temporal_aq = priv->temporal_aq;
  spatial_aq_gain = priv->spatial_aq_gain;

  /* Update LA meta on input buffer */
  w_inbuf = gst_buffer_make_writable (inbuf);

  lameta = gst_buffer_add_vvas_la_meta (w_inbuf);
  if (!lameta) {
    GST_ERROR_OBJECT (self, "failed to add metadata on input buffer");
    goto error;
  }

  lameta->codec_type = priv->codec_type;
  lameta->gop_length = priv->gop_size;
  lameta->num_bframes = num_bframes;
  lameta->lookahead_depth = priv->lookahead_depth;
  lameta->is_idr = priv->is_idr;
  lameta->spatial_aq = spatial_aq;
  lameta->temporal_aq = temporal_aq;
  lameta->spatial_aq_gain = spatial_aq_gain;

  /* Update LA meta on stats buffer */
  w_stats_buf = gst_buffer_make_writable (stats_buf);

  lameta = gst_buffer_add_vvas_la_meta (w_stats_buf);
  if (!lameta) {
    GST_ERROR_OBJECT (self, "failed to add metadata on stats buffer");
    goto error;
  }

  lameta->codec_type = priv->codec_type;
  lameta->gop_length = priv->gop_size;
  lameta->num_bframes = num_bframes;
  lameta->lookahead_depth = priv->lookahead_depth;
  lameta->is_idr = priv->is_idr;
  lameta->spatial_aq = spatial_aq;
  lameta->temporal_aq = temporal_aq;
  lameta->spatial_aq_gain = spatial_aq_gain;

  priv->is_idr = FALSE;
  g_mutex_lock (&priv->proc_lock);
  g_queue_push_tail (priv->inbuf_queue, w_inbuf);
  g_queue_push_tail (priv->stats_queue, w_stats_buf);
  g_cond_broadcast (&priv->proc_cond);
  g_mutex_unlock (&priv->proc_lock);

  GST_LOG_OBJECT (self, "successfully executed kernel command");

  return GST_FLOW_OK;

error:
  if (in_mem)
    gst_memory_unref (in_mem);
  if (prev_in_mem)
    gst_memory_unref (prev_in_mem);
  if (stats_mem)
    gst_memory_unref (stats_mem);

  return fret;
}

static GstFlowReturn
gst_vvas_xlookahead_generate_output (GstBaseTransform * trans,
    GstBuffer ** outbuf)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (trans);
  GstVvas_XLookAheadPrivate *priv = self->priv;

  return priv->last_fret;
}

static void
gst_vvas_xlookahead_class_init (GstVvas_XLookAheadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *transform_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_vvas_xlookahead_set_property;
  gobject_class->get_property = gst_vvas_xlookahead_get_property;
  gobject_class->finalize = gst_vvas_xlookahead_finalize;

  transform_class->start = gst_vvas_xlookahead_start;
  transform_class->stop = gst_vvas_xlookahead_stop;
  transform_class->query = gst_vvas_xlookahead_query;
  transform_class->set_caps = gst_vvas_xlookahead_set_caps;
  transform_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_vvas_xlookahead_sink_event);
  transform_class->propose_allocation = gst_vvas_xlookahead_propose_allocation;
  transform_class->submit_input_buffer =
      GST_DEBUG_FUNCPTR (gst_vvas_xlookahead_submit_input_buffer);
  transform_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_vvas_xlookahead_generate_output);

  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("dev-idx", "Device index",
          "Valid Device index is 0 to 31. Default value is set to -1 intentionally so that user provides the correct device index.",
          -1, 31, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_NUM_BFRAMES,
      g_param_spec_int ("b-frames", "Number of B frames",
          "Number of B-frames between two consecutive P-frames. "
          "By default, internally set to 0 for ultra-low-latency mode, 2 otherwise if not configured or configured with -1",
          -1, G_MAXINT, UNSET_NUM_B_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SPATIAL_AQ,
      g_param_spec_boolean ("spatial-aq", "Spatial AQ",
          "Enable/Disable Spatial AQ activity", DEFAULT_SPATIAL_AQ,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TEMPORAL_AQ,
      g_param_spec_boolean ("temporal-aq", "Temporal AQ",
          "Enable/Disable Temporal AQ linear", DEFAULT_TEMPORAL_AQ,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SPATIAL_AQ_GAIN,
      g_param_spec_uint ("spatial-aq-gain", "Percentage of Spatial AQ gain",
          "Percentage of Spatial AQ gain, applied when spatial-aq is true",
          0, 100, DEFAULT_SPATIAL_AQ_GAIN_PERCENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOOKAHEAD_DEPTH,
      g_param_spec_uint ("lookahead-depth", "Lookahead depth",
          "Lookahead depth", 1, XLNX_MAX_LOOKAHEAD_DEPTH,
          DEFAULT_LOOK_AHEAD_DEPTH,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_XCLBIN_LOCATION,
      g_param_spec_string ("xclbin-location", "xclbin file location",
          "Location of the xclbin to program device", NULL,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_CODEC_TYPE,
      g_param_spec_enum ("codec-type", "Codec type H264/H265",
          "Codec type H264/H265", GST_TYPE_VVAS_XLOOKAHEAD_CODEC_TYPE,
          DEFAULT_CODEC_TYPE, G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_KERNEL_NAME,
      g_param_spec_string ("kernel-name", "Lookahead kernel name",
          "Lookahead kernel name", VVAS_LOOKAHEAD_KERNEL_NAME_DEFAULT,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_ENABLE_PIPELINE,
      g_param_spec_boolean ("enable-pipeline",
          "Enable pipelining",
          "Enable buffer pipelining to improve performance in non zero-copy use cases",
          VVAS_LOOKAHEAD_DEFAULT_ENABLE_PIPLINE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_DYNAMIC_GOP,
      g_param_spec_boolean ("dynamic-gop", "Enable dynamic GOP",
          "Automatically change B-frame structure based on motion vectors. Requires Lookahead depth of at least 5",
          VVAS_LOOKAHEAD_DEFAULT_DYNAMIC_GOP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

#ifdef ENABLE_XRM_SUPPORT
  g_object_class_install_property (gobject_class, PROP_RESERVATION_ID,
      g_param_spec_uint64 ("reservation-id", "XRM reservation id",
          "Resource Pool Reservation id", 0, G_MAXUINT64, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
#endif

  g_object_class_install_property (gobject_class, PROP_IN_MEM_BANK,
      g_param_spec_uint ("in-mem-bank", "VVAS Input Memory Bank",
          "VVAS input memory bank to allocate memory",
          0, G_MAXUSHORT, DEFAULT_MEM_BANK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_set_details_simple (gstelement_class,
      "VVAS LookAhead Filter Plugin",
      "Filter/Effect/Video",
      "Lookahead plugin using Xilinx Lookahead IP",
      "Xilinx Inc <www.xilinx.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xlookahead_debug, "vvas_xlookahead", 0,
      "VVAS Generic Filter plugin");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_vvas_xlookahead_init (GstVvas_XLookAhead * self)
{
  GstVvas_XLookAheadPrivate *priv = GST_VVAS_XLOOKAHEAD_PRIVATE (self);

  self->priv = priv;
  priv->dev_idx = DEFAULT_DEVICE_INDEX;
  priv->kernel_name = g_strdup (VVAS_LOOKAHEAD_KERNEL_NAME_DEFAULT);
  priv->dev_handle = NULL;
  priv->in_vinfo = gst_video_info_new ();
  priv->last_fret = GST_FLOW_OK;
  priv->min_offset = priv->max_offset = 0;
  priv->xclbin_path = NULL;
  priv->lookahead_depth = DEFAULT_LOOK_AHEAD_DEPTH;
  priv->codec_type = DEFAULT_CODEC_TYPE;
  priv->spatial_aq = DEFAULT_SPATIAL_AQ;
  priv->temporal_aq = DEFAULT_TEMPORAL_AQ;
  priv->enable_rate_control = DEFAULT_RATE_CONTROL;
  priv->spatial_aq_gain = DEFAULT_SPATIAL_AQ_GAIN_PERCENT;
  priv->num_bframes = UNSET_NUM_B_FRAMES;
  priv->prev_inbuf = NULL;
  priv->num_mb = 0;
  priv->frame_num = 0;
  priv->sad_off = priv->act_off = priv->var_off = priv->mv_off = 0;
  priv->gop_size = DEFAULT_GOP_SIZE;
  priv->qpmap_pool = priv->fsfa_pool = NULL;
  priv->is_eos = FALSE;
  priv->stats_queue = NULL;
  priv->postproc_thread = NULL;
  priv->qp_handle = NULL;
  priv->has_error = FALSE;
  priv->enabled_pipeline = VVAS_LOOKAHEAD_DEFAULT_ENABLE_PIPLINE;
  priv->stop = FALSE;
  priv->in_mem_bank = DEFAULT_MEM_BANK;

#ifdef ENABLE_XRM_SUPPORT
  priv->xrm_ctx = NULL;
  priv->cu_resource = NULL;
  priv->cu_resource_v2 = NULL;
  priv->reservation_id = 0;
  priv->cur_load = 0;
#endif
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (self), TRUE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), TRUE);
}

static void
gst_vvas_xlookahead_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (object);

  switch (prop_id) {
    case PROP_DEVICE_INDEX:
      self->priv->dev_idx = g_value_get_int (value);
      break;
    case PROP_XCLBIN_LOCATION:
      if (self->priv->xclbin_path)
        g_free (self->priv->xclbin_path);

      self->priv->xclbin_path = g_value_dup_string (value);
      break;
    case PROP_LOOKAHEAD_DEPTH:
      self->priv->lookahead_depth = g_value_get_uint (value);
      break;
    case PROP_SPATIAL_AQ:
      self->priv->spatial_aq = g_value_get_boolean (value);
      break;
    case PROP_TEMPORAL_AQ:
      self->priv->temporal_aq = g_value_get_boolean (value);
      break;
    case PROP_SPATIAL_AQ_GAIN:
      self->priv->spatial_aq_gain = g_value_get_uint (value);
      break;
    case PROP_CODEC_TYPE:
      self->priv->codec_type = g_value_get_enum (value);
      break;
    case PROP_KERNEL_NAME:
      if (self->priv->kernel_name)
        g_free (self->priv->kernel_name);

      self->priv->kernel_name = g_value_dup_string (value);
      break;
    case PROP_NUM_BFRAMES:
      GST_OBJECT_LOCK (self);
      if (GST_STATE (self) == GST_STATE_NULL
          || GST_STATE (self) == GST_STATE_READY) {
        self->priv->num_bframes = g_value_get_int (value);
      } else if (GST_STATE (self) > GST_STATE_READY) {
        if (!self->priv->ultra_low_latency && !self->priv->dynamic_gop) {
          self->priv->num_bframes = g_value_get_int (value);
        } else {
          if (self->priv->ultra_low_latency) {
            GST_WARNING_OBJECT (self,
                "Dynamic configuration of b-frames is not supported"
                "in ultra-low-latency mode");
          } else if (self->priv->dynamic_gop) {
            GST_WARNING_OBJECT (self,
                "Dynamic configuration of b-frames is not supported"
                "when dynamic-gop is enabled");
          }
        }
      }
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_ENABLE_PIPELINE:
      self->priv->enabled_pipeline = g_value_get_boolean (value);
      break;
    case PROP_DYNAMIC_GOP:
      self->priv->dynamic_gop = g_value_get_boolean (value);
      break;
#ifdef ENABLE_XRM_SUPPORT
    case PROP_RESERVATION_ID:
      self->priv->reservation_id = g_value_get_uint64 (value);
      break;
#endif
    case PROP_IN_MEM_BANK:
      self->priv->in_mem_bank = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_xlookahead_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (object);

  switch (prop_id) {
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, self->priv->dev_idx);
      break;
    case PROP_LOOKAHEAD_DEPTH:
      g_value_set_uint (value, self->priv->lookahead_depth);
      break;
    case PROP_SPATIAL_AQ:
      g_value_set_boolean (value, self->priv->spatial_aq);
      break;
    case PROP_TEMPORAL_AQ:
      g_value_set_boolean (value, self->priv->temporal_aq);
      break;
    case PROP_SPATIAL_AQ_GAIN:
      g_value_set_uint (value, self->priv->spatial_aq_gain);
      break;
    case PROP_CODEC_TYPE:
      g_value_set_enum (value, self->priv->codec_type);
      break;
    case PROP_NUM_BFRAMES:
      g_value_set_int (value, self->priv->num_bframes);
      break;
    case PROP_ENABLE_PIPELINE:
      g_value_set_boolean (value, self->priv->enabled_pipeline);
      break;
    case PROP_DYNAMIC_GOP:
      g_value_set_boolean (value, self->priv->dynamic_gop);
      break;
#ifdef ENABLE_XRM_SUPPORT
    case PROP_RESERVATION_ID:
      g_value_set_uint64 (value, self->priv->reservation_id);
      break;
#endif
    case PROP_IN_MEM_BANK:
      g_value_set_uint (value, self->priv->in_mem_bank);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_xlookahead_finalize (GObject * obj)
{
  GstVvas_XLookAhead *self = GST_VVAS_XLOOKAHEAD (obj);
  GstVvas_XLookAheadPrivate *priv = self->priv;

  gst_video_info_free (priv->in_vinfo);

  if (priv->stats_queue)
    g_queue_free (priv->stats_queue);

  if (priv->xclbin_path)
    g_free (priv->xclbin_path);

  if (priv->input_pool)
    gst_object_unref (priv->input_pool);

  if (priv->kernel_name)
    g_free (priv->kernel_name);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static gboolean
plugin_init (GstPlugin * vvas_xlookahead)
{
  return gst_element_register (vvas_xlookahead, "vvas_xlookahead",
      GST_RANK_NONE, GST_TYPE_VVAS_XLOOKAHEAD);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xlookahead,
    "GStreamer VVAS plug-in for LookAhead IP", plugin_init, VVAS_API_VERSION,
    "Proprietary", "Xilinx VVAS SDK plugin", "http://xilinx.com/")
