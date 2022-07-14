/*
 * Copyright (C) 2021 - 2022 Xilinx, Inc.  All rights reserved.
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

/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005-2012 David Schleef <ds@schleef.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <stdio.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/video/video.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <gst/vvas/gstvvasallocator.h>
#include <gst/vvas/gstvvasbufferpool.h>
#include <vvas/xrt_utils.h>
#include <inttypes.h>
#include <stdint.h>
#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include "multi_scaler_hw.h"
#include "gstvvas_xcompositor.h"
#include <math.h>
#ifdef ENABLE_XRM_SUPPORT
#include <xrm.h>
#include <xrm_limits.h>
#include <jansson.h>
#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))
#endif

#ifdef XLNX_PCIe_PLATFORM
#define DEFAULT_DEVICE_INDEX -1
#define DEFAULT_KERNEL_NAME "scaler:{scaler_1}"
#define NEED_DMABUF 0
#else
#define DEFAULT_DEVICE_INDEX 0  /* on Embedded only one device i.e. device 0 */
#define DEFAULT_KERNEL_NAME "v_multi_scaler:{v_multi_scaler_1}"
#define NEED_DMABUF 1
#endif

#define MULTI_SCALER_TIMEOUT 1000
#define ALIGN(size,align) (((size) + (align) - 1) & ~((align) - 1))
#define DIV_AND_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define VVAS_XCOMPOSITOR_BEST_FIT_DEFAULT FALSE
#define VVAS_XCOMPOSITOR_AVOID_OUTPUT_COPY_DEFAULT FALSE
#define VVAS_XCOMPOSITOR_ENABLE_PIPELINE_DEFAULT FALSE
#define STOP_COMMAND ((gpointer)GINT_TO_POINTER (g_quark_from_string("STOP")))

/*256x64 for specific use-case only*/
#ifdef XLNX_PCIe_PLATFORM
#define WIDTH_ALIGN 256
#define HEIGHT_ALIGN 64
#else
#define WIDTH_ALIGN (8 * self->ppc)
#define HEIGHT_ALIGN 1
#endif

#define MEM_BANK 0
#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0
#define DEFAULT_PAD_WIDTH  -1
#define DEFAULT_PAD_HEIGHT -1
#define DEFAULT_PAD_ZORDER -1
#define DEFAULT_PAD_EOS false

#define GST_TYPE_VVAS_XCOMPOSITOR_PAD \
    (gst_vvas_xcompositor_pad_get_type())
#define GST_VVAS_XCOMPOSITOR_PAD(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XCOMPOSITOR_PAD, \
				GstVvasXCompositorPad))
#define GST_VVAS_XCOMPOSITOR_PAD_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XCOMPOSITOR_PAD,\
			     GstVvasXCompositorPadClass))
#define GST_IS_VVAS_XCOMPOSITOR_PAD(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XCOMPOSITOR_PAD))
#define GST_IS_VVAS_XCOMPOSITOR_PAD_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XCOMPOSITOR_PAD))
#define GST_VVAS_XCOMPOSITOR_PAD_CAST(obj) \
    ((GstVvasXCompositorPad *)(obj))

#ifdef XLNX_PCIe_PLATFORM       /* default taps for PCIe platform 12 */
#define VVAS_XCOMPOSITOR_DEFAULT_NUM_TAPS 12
#define VVAS_XCOMPOSITOR_DEFAULT_PPC 4
#define VVAS_XCOMPOSITOR_SCALE_MODE 2
#else /* default taps for Embedded platform 6 */
#define VVAS_XCOMPOSITOR_DEFAULT_NUM_TAPS 6
#define VVAS_XCOMPOSITOR_DEFAULT_PPC 2
#define VVAS_XCOMPOSITOR_SCALE_MODE 0
#endif

#define VVAS_XCOMPOSITOR_NUM_TAPS_TYPE (vvas_xcompositor_num_taps_type ())
#define VVAS_XCOMPOSITOR_PPC_TYPE (vvas_xcompositor_ppc_type ())
#ifdef XLNX_PCIe_PLATFORM
#define VVAS_XCOMPOSITOR_DEFAULT_COEF_LOAD_TYPE COEF_AUTO_GENERATE
#else
#define VVAS_XCOMPOSITOR_DEFAULT_COEF_LOAD_TYPE COEF_FIXED
#endif

#define VVAS_XCOMPOSITOR_COEF_LOAD_TYPE (vvas_xcompositor_coef_load_type ())
#define gst_vvas_xcompositor_sinkpad_at_index(self, idx) ((GstVvasXCompositorPad *)(g_list_nth ((self)->sinkpads, idx))->data)
#define gst_vvas_xcompositor_parent_class parent_class
#define GST_CAT_DEFAULT gst_vvas_xcompositor_debug

typedef struct _GstVvasXCompositorPad GstVvasXCompositorPad;
typedef struct _GstVvasXCompositorPadClass GstVvasXCompositorPadClass;

/*properties*/
enum
{
  PROP_0,
  PROP_XCLBIN_LOCATION,
  PROP_KERN_NAME,
#ifdef XLNX_PCIe_PLATFORM
  PROP_DEVICE_INDEX,
#endif
  PROP_IN_MEM_BANK,
  PROP_OUT_MEM_BANK,
  PROP_PPC,
  PROP_SCALE_MODE,
  PROP_NUM_TAPS,
  PROP_COEF_LOADING_TYPE,
  PROP_BEST_FIT,
  PROP_AVOID_OUTPUT_COPY,
  PROP_ENABLE_PIPELINE,
#ifdef ENABLE_XRM_SUPPORT
  PROP_RESERVATION_ID,
#endif
};

/* pad properties */
enum
{
  PROP_PAD_0,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_WIDTH,
  PROP_PAD_HEIGHT,
  PROP_PAD_ZORDER
};

struct _GstVvasXCompositorPad
{
  GstVideoAggregatorPad compositor_pad;
  guint xpos, ypos;
  gint width, height;
  guint index;
  gint zorder;
  gboolean is_eos;
  GstBufferPool *pool;
  GstVideoInfo *in_vinfo;
  gboolean validate_import;
};

struct _GstVvasXCompositorPadClass
{
  GstVideoAggregatorPadClass compositor_pad_class;
};

struct _GstVvasXCompositorPrivate
{
  gboolean is_internal_buf_allocated;
  GstVideoInfo *in_vinfo[MAX_CHANNELS];
  GstVideoInfo *out_vinfo;
  uint32_t meta_in_stride[MAX_CHANNELS];
  vvasDeviceHandle dev_handle;
  vvasKernelHandle kern_handle;
  vvasRunHandle run_handle;
  uint32_t cu_index;
  bool is_coeff;
  uuid_t xclbinId;
  xrt_buffer Hcoff[MAX_CHANNELS];
  xrt_buffer Vcoff[MAX_CHANNELS];
  GstBuffer *outbuf;
  GstBuffer *inbufs[MAX_CHANNELS];      //to store input buffers from aggregate_frames
  xrt_buffer msPtr[MAX_CHANNELS];
  guint64 phy_in_0[MAX_CHANNELS];
  guint64 phy_in_1[MAX_CHANNELS];
  guint64 phy_in_2[MAX_CHANNELS];
  gint pad_of_zorder[MAX_CHANNELS];
  guint64 phy_out_0;
  guint64 phy_out_1;
  guint64 phy_out_2;
  guint64 out_offset;
  GstBufferPool *output_pool;
  gboolean need_copy;
  GThread *input_copy_thread;
  GAsyncQueue *copy_inqueue[MAX_CHANNELS];
  GAsyncQueue *copy_outqueue[MAX_CHANNELS];
  gboolean is_first_frame[MAX_CHANNELS];

#ifdef ENABLE_PPE_SUPPORT
  gfloat alpha_r;
  gfloat alpha_g;
  gfloat alpha_b;
  gfloat beta_r;
  gfloat beta_g;
  gfloat beta_b;
#endif

#ifdef ENABLE_XRM_SUPPORT
  xrmContext xrm_ctx;
  xrmCuResource *cu_resource;
  xrmCuResourceV2 *cu_resource_v2;
  gint cur_load;
  guint64 reservation_id;
  gboolean has_error;
#endif
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGBx, YUY2, r210, Y410, NV16, NV12, RGB, v308, I422_10LE, GRAY8, \
	NV12_10LE32, BGRx, GRAY10_LE32, BGRx, UYVY, BGR, RGBA, BGRA, I420, GBR}")));

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGBx, YUY2, r210, Y410, NV16, NV12, RGB, v308, I422_10LE, GRAY8, \
	NV12_10LE32, BGRx, GRAY10_LE32, BGRx, UYVY, BGR, RGBA, BGRA, I420, GBR}")));

static void gst_vvas_xcompositor_child_proxy_init (gpointer g_iface,
    gpointer iface_data);

static GstPad *gst_vvas_xcompositor_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * req_name, const GstCaps * caps);
static void gst_vvas_xcompositor_release_pad (GstElement * element,
    GstPad * pad);
static gboolean gst_vvas_xcompositor_stop (GstAggregator * agg);
static gboolean gst_vvas_xcompositor_start (GstAggregator * agg);
static gboolean gst_vvas_xcompositor_src_query (GstAggregator * agg,
    GstQuery * query);
static gboolean gst_vvas_xcompositor_sink_query (GstAggregator * agg,
    GstAggregatorPad * bpad, GstQuery * query);

static void gst_vvas_xcompositor_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_vvas_xcompositor_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_vvas_xcompositor_finalize (GObject * object);
static GstFlowReturn gst_vvas_xcompositor_aggregate_frames (GstVideoAggregator
    * vagg, GstBuffer * outbuffer);
static GstFlowReturn
gst_vvas_xcompositor_create_output_buffer (GstVideoAggregator * videoaggregator,
    GstBuffer ** outbuffer);
void copy_filt_set (int16_t dest_filt[64][12], int set);
void Generate_cardinal_cubic_spline (int src, int dst, int filterSize,
    int64_t B, int64_t C, int16_t * CCS_filtCoeff);
static gboolean vvas_xcompositor_open (GstVvasXCompositor * self);
static void
xlnx_multiscaler_coff_fill (void *Hcoeff_BufAddr, void *Vcoeff_BufAddr,
    float scale);
static gboolean
vvas_xcompositor_prepare_coefficients_with_12tap (GstVvasXCompositor * self,
    guint chan_id);
static gpointer vvas_xcompositor_input_copy_thread (gpointer data);
static guint
vvas_xcompositor_get_padding_right (GstVvasXCompositor * self,
    GstVideoInfo * info);
static gboolean
vvas_xcompositor_set_zorder_from_pads (GstVvasXCompositor * self);
static void
vvas_xcompositor_adjust_zorder_after_eos (GstVvasXCompositor * self,
    guint index);

#ifdef XLNX_PCIe_PLATFORM
static gboolean xlnx_abr_coeff_syncBO (GstVvasXCompositor * self);
#endif

GType gst_vvas_xcompositor_pad_get_type (void);

G_DEFINE_TYPE_WITH_CODE (GstVvasXCompositor, gst_vvas_xcompositor,
    GST_TYPE_VIDEO_AGGREGATOR, G_ADD_PRIVATE (GstVvasXCompositor)
    G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_vvas_xcompositor_child_proxy_init));

G_DEFINE_TYPE (GstVvasXCompositorPad, gst_vvas_xcompositor_pad,
    GST_TYPE_VIDEO_AGGREGATOR_PAD);

GST_DEBUG_CATEGORY (gst_vvas_xcompositor_debug);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

static GType
vvas_xcompositor_num_taps_type (void)
{
  static GType num_tap = 0;

  if (!num_tap) {
    static const GEnumValue taps[] = {
      {6, "6 taps filter for scaling", "6"},
      {8, "8 taps filter for scaling", "8"},
      {10, "10 taps filter for scaling", "10"},
      {12, "12 taps filter for scaling", "12"},
      {0, NULL, NULL}
    };
    num_tap = g_enum_register_static ("GstVvasXCompositorNumTapsType", taps);
  }
  return num_tap;
}

static GType
vvas_xcompositor_ppc_type (void)
{
  static GType num_ppc = 0;

  if (!num_ppc) {
    static const GEnumValue ppc[] = {
      {1, "1 Pixel Per Clock", "1"},
      {2, "2 Pixels Per Clock", "2"},
      {4, "4 Pixels Per Clock", "4"},
      {0, NULL, NULL}
    };
    num_ppc = g_enum_register_static ("GstVvasXCompositorPPCType", ppc);
  }
  return num_ppc;
}

static GType
vvas_xcompositor_coef_load_type (void)
{
  static GType load_type = 0;

  if (!load_type) {
    static const GEnumValue load_types[] = {
      {COEF_FIXED, "Use fixed filter coefficients", "fixed"},
      {COEF_AUTO_GENERATE, "Auto generate filter coefficients", "auto"},
      {0, NULL, NULL}
    };
    load_type =
        g_enum_register_static ("GstVvasXCompositorCoefLoadType", load_types);
  }
  return load_type;
}

static guint
vvas_xcompositor_get_stride (GstVideoInfo * info, guint width)
{
  guint stride = 0;

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_I420:
      stride = ALIGN (width * 1, 4);
      break;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_v308:
      stride = ALIGN (width * 3, 4);
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      stride = ALIGN (width * 2, 4);
      break;
    case GST_VIDEO_FORMAT_r210:
    case GST_VIDEO_FORMAT_Y410:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      stride = width * 4;
      break;
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_NV12_10LE32:
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      /* 4 bytes per 3 pixels */
      stride = (width + 2) / 3 * 4;
      break;
    default:
      GST_ERROR ("Not supporting %s yet",
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)));
      stride = 0;
      break;
  }
  return stride;
}

static gboolean
vvas_xcompositor_decide_allocation (GstAggregator * agg, GstQuery * query)
{
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (agg);
  GstVvasXCompositorPrivate *priv = self->priv;
  GstCaps *outcaps;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  guint size, min, max;
  gboolean update_allocator, update_pool, bret, have_new_allocator = FALSE;
  GstStructure *config = NULL;
  GstVideoInfo out_vinfo;
  guint chan_id;

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0) {
    /* try the allocator */
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    GST_DEBUG_OBJECT (self, "has allocator %p", allocator);
    update_allocator = TRUE;
  } else {
    allocator = NULL;
    update_allocator = FALSE;
    gst_allocation_params_init (&params);
  }

  gst_query_parse_allocation (query, &outcaps, NULL);
  if (outcaps && !gst_video_info_from_caps (&out_vinfo, outcaps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from outcaps");
    goto error;
  }
  self->priv->out_vinfo = gst_video_info_copy (&out_vinfo);
  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    size = MAX (size, out_vinfo.size);
    update_pool = TRUE;
    if (min == 0)
      min = 3;
  } else {
    pool = NULL;
    min = 3;
    max = 0;
    size = out_vinfo.size;
    update_pool = FALSE;
  }

  /* Check if the proposed pool is VVAS Buffer Pool and stride is aligned with (8 * ppc)
   * If otherwise, discard the pool. Will create a new one */
  if (pool) {
    GstVideoAlignment video_align = { 0, };
    guint padded_width = 0;
    guint stride = 0, multiscaler_req_stride;

    config = gst_buffer_pool_get_config (pool);
    if (config && gst_buffer_pool_config_has_option (config,
            GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
      gst_buffer_pool_config_get_video_alignment (config, &video_align);

      /* We have adding padding_right and padding_left in pixels.
       * We need to convert them to bytes for finding out the complete stride with alignment */
      padded_width =
          out_vinfo.width + video_align.padding_right +
          video_align.padding_left;
      stride = vvas_xcompositor_get_stride (&out_vinfo, padded_width);

      GST_INFO_OBJECT (self, "output stride = %u", stride);

      if (!stride)
        return FALSE;
      gst_structure_free (config);
      config = NULL;
      multiscaler_req_stride = 8 * self->ppc;

      if (stride % multiscaler_req_stride) {
        GST_WARNING_OBJECT (self, "Discarding the propsed pool, "
            "Alignment not matching with 8 * self->ppc");
        gst_object_unref (pool);
        pool = NULL;
        update_pool = FALSE;

        self->out_stride_align = multiscaler_req_stride;
        self->out_elevation_align = 1;
        GST_DEBUG_OBJECT (self, "Going to allocate pool with stride_align %d and \
			elevation_align %d", self->out_stride_align,
            self->out_elevation_align);
      }
    } else {
      /* VVAS Buffer Pool but no alignment information.
       * Discard to create pool with scaler alignment requirements*/
      if (config) {
        gst_structure_free (config);
        config = NULL;
      }
      gst_object_unref (pool);
      pool = NULL;
      update_pool = FALSE;
    }
  }
#ifdef XLNX_EMBEDDED_PLATFORM
  /* TODO: Currently Kms buffer are not supported in PCIe platform */
  if (pool) {
    GstStructure *config = gst_buffer_pool_get_config (pool);

    if (gst_buffer_pool_config_has_option (config,
            "GstBufferPoolOptionKMSPrimeExport")) {
      gst_structure_free (config);
      goto next;
    }

    gst_structure_free (config);
    gst_object_unref (pool);
    pool = NULL;
    GST_DEBUG_OBJECT (self, "pool deos not have the KMSPrimeExport option, \
		unref the pool and create vvas allocator");
  }
#endif

  if (pool && !GST_IS_VVAS_BUFFER_POOL (pool)) {
    /* create own pool */
    gst_object_unref (pool);
    pool = NULL;
    update_pool = FALSE;
  }

  if (allocator && (!GST_IS_VVAS_ALLOCATOR (allocator) ||
          gst_vvas_allocator_get_device_idx (allocator) != self->dev_index)) {
    GST_DEBUG_OBJECT (self, "replace %" GST_PTR_FORMAT " to xrt allocator",
        allocator);
    gst_object_unref (allocator);
    gst_allocation_params_init (&params);
    allocator = NULL;
  }

  if (!allocator) {
    /* making vvas allocator for the HW mode without dmabuf */
    allocator = gst_vvas_allocator_new (self->dev_index,
        NEED_DMABUF, self->out_mem_bank, self->priv->kern_handle);
    params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
    params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;
    GST_INFO_OBJECT (self, "creating new xrt allocator %" GST_PTR_FORMAT
        "at mem bank %d", allocator, self->out_mem_bank);
    have_new_allocator = TRUE;
  }
#ifdef XLNX_EMBEDDED_PLATFORM
next:
#endif
  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);


  /* If there is no pool allignment requirement from downstream or if scaling dimention
   * is not aligned to (8 * ppc), then we will create a new pool*/

  if (!pool && (self->out_stride_align == 1)
      && ((out_vinfo.stride[0] % WIDTH_ALIGN)
          || (out_vinfo.height % HEIGHT_ALIGN))) {

    self->out_stride_align = WIDTH_ALIGN;
    self->out_elevation_align = HEIGHT_ALIGN;
  }

  if (!pool) {
    GstVideoAlignment align;

    pool =
        gst_vvas_buffer_pool_new (self->out_stride_align,
        self->out_elevation_align);
    GST_INFO_OBJECT (self, "created new pool %p %" GST_PTR_FORMAT, pool, pool);

    config = gst_buffer_pool_get_config (pool);
    gst_video_alignment_reset (&align);
    align.padding_top = 0;
    align.padding_left = 0;
    align.padding_right = vvas_xcompositor_get_padding_right (self, &out_vinfo);
    align.padding_bottom =
        ALIGN (GST_VIDEO_INFO_HEIGHT (&out_vinfo),
        self->out_elevation_align) - GST_VIDEO_INFO_HEIGHT (&out_vinfo);
    if (align.padding_right == -1)
      goto error;
    gst_video_info_align (&out_vinfo, &align);
    /* size updated in vinfo based on alignment */
    size = out_vinfo.size;

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
    gst_buffer_pool_config_set_allocator (config, allocator, &params);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_set_video_alignment (config, &align);
    bret = gst_buffer_pool_set_config (pool, config);
    if (!bret) {
      GST_ERROR_OBJECT (self, "failed configure pool");
      goto error;
    }
  } else if (have_new_allocator) {
    /* update newly allocator on downstream pool */
    config = gst_buffer_pool_get_config (pool);

    GST_INFO_OBJECT (self, "updating allocator %" GST_PTR_FORMAT
        " on pool %" GST_PTR_FORMAT, allocator, pool);

    gst_buffer_pool_config_set_allocator (config, allocator, &params);
    bret = gst_buffer_pool_set_config (pool, config);
    if (!bret) {
      GST_ERROR_OBJECT (self, "failed configure pool");
      goto error;
    }
  }
  /*  Since self->out_stride_align is common for all channels
   *  reset the output stride to 1 (its default), so that other channels are not affected */
  self->out_stride_align = 1;
  self->out_elevation_align = 1;
/* avoid output frames copy when downstream supports video meta */
  if (!self->avoid_output_copy) {
    if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
      self->priv->need_copy = FALSE;
      GST_INFO_OBJECT (self, "no need to copy output frames");
    }
  } else {
    self->priv->need_copy = FALSE;
    GST_INFO_OBJECT (self, "Don't copy output frames");
  }
  GST_INFO_OBJECT (self,
      "allocated pool %p with parameters : size %u, min_buffers = %u, max_buffers = %u",
      pool, size, min, max);

  if (allocator)
    gst_object_unref (allocator);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);
/* unreffing the old pool if already present */
  if (self->priv->output_pool)
    gst_object_unref (self->priv->output_pool);

  self->priv->output_pool = pool;

  self->srcpad = agg->srcpad;

#ifdef ENABLE_XRM_SUPPORT
  self->priv->xrm_ctx = (xrmContext *) xrmCreateContext (XRM_API_VERSION_1);
  if (!self->priv->xrm_ctx) {
    GST_ERROR_OBJECT (self, "create XRM context failed");
    return FALSE;
  }
  GST_INFO_OBJECT (self, "successfully created xrm context");
  self->priv->has_error = FALSE;
#endif
  if (self->enabled_pipeline) {
    for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
      priv->is_first_frame[chan_id] = TRUE;
      priv->copy_inqueue[chan_id] =
          g_async_queue_new_full ((void (*)(void *)) gst_buffer_unref);
      priv->copy_outqueue[chan_id] =
          g_async_queue_new_full ((void (*)(void *)) gst_buffer_unref);
    }
    priv->input_copy_thread = g_thread_new ("compositor-input-copy-thread",
        vvas_xcompositor_input_copy_thread, self);
  }
  if (!vvas_xcompositor_open (self))
    return FALSE;


  GST_DEBUG_OBJECT (self,
      "done decide allocation with query %" GST_PTR_FORMAT, query);
  return TRUE;

error:
  if (allocator)
    gst_object_unref (allocator);
  if (pool)
    gst_object_unref (pool);
  if (self->priv->output_pool)
    gst_object_unref (self->priv->output_pool);
  self->priv->output_pool = NULL;
  return FALSE;
}

#ifdef ENABLE_XRM_SUPPORT
static gchar *
vvas_xcompositor_prepare_request_json_string (GstVvasXCompositor * compositor)
{
  json_t *out_jobj, *jarray, *fps_jobj = NULL, *tmp_jobj, *tmp2_jobj, *res_jobj;
  guint out_width, out_height;
  guint out_fps_n, out_fps_d;
  GstVvasXCompositorPad *srcpad;
  GstCaps *out_caps = NULL;
  GstVideoInfo out_vinfo;
  gint idx;
  gchar *req_str = NULL;
  gboolean bret;
  json_t *in_jobj = NULL;
  jarray = json_array ();

  srcpad = GST_VVAS_XCOMPOSITOR_PAD_CAST (compositor->srcpad);
  if (!srcpad) {
    GST_ERROR_OBJECT (compositor, "failed to get srcpad");
    goto out_error;
  }

  out_caps = gst_pad_get_current_caps ((GstPad *) srcpad);
  if (!out_caps) {
    GST_ERROR_OBJECT (compositor, "failed to get output caps ");
    goto out_error;
  }

  bret = gst_video_info_from_caps (&out_vinfo, out_caps);
  if (!bret) {
    GST_ERROR_OBJECT (compositor, "failed to get video info from caps");
    goto out_error;
  }
  out_width = GST_VIDEO_INFO_WIDTH (&out_vinfo);
  out_height = GST_VIDEO_INFO_HEIGHT (&out_vinfo);
  out_fps_n = GST_VIDEO_INFO_FPS_N (&out_vinfo);
  out_fps_d = GST_VIDEO_INFO_FPS_D (&out_vinfo);

  if (!out_fps_n) {
    GST_WARNING_OBJECT (compositor,
        "out fps is not available, taking default 60/1");
    out_fps_n = 60;
    out_fps_d = 1;
  }

  out_jobj = json_object ();
  if (!out_jobj)
    goto out_error;

  json_object_set_new (out_jobj, "width", json_integer (out_width));
  json_object_set_new (out_jobj, "height", json_integer (out_height));

  fps_jobj = json_object ();
  if (!fps_jobj)
    goto out_error;

  json_object_set_new (fps_jobj, "num", json_integer (out_fps_n));
  json_object_set_new (fps_jobj, "den", json_integer (out_fps_d));
  json_object_set_new (out_jobj, "frame-rate", fps_jobj);

  json_array_append_new (jarray, out_jobj);

  if (out_caps)
    gst_caps_unref (out_caps);


  in_jobj = json_object ();
  if (!in_jobj)
    goto error;

  for (idx = 0; idx < compositor->num_request_pads; idx++) {
    guint in_fps_n, in_fps_d;
    guint in_width, in_height;
    in_width = GST_VIDEO_INFO_WIDTH (compositor->priv->in_vinfo[idx]);
    in_height = GST_VIDEO_INFO_HEIGHT (compositor->priv->in_vinfo[idx]);
    in_fps_n = GST_VIDEO_INFO_FPS_N (compositor->priv->in_vinfo[idx]);
    in_fps_d = GST_VIDEO_INFO_FPS_D (compositor->priv->in_vinfo[idx]);
    if (!in_fps_n) {
      GST_WARNING_OBJECT (compositor,
          "in fps is not available, taking default 60/1");
      in_fps_n = 60;
      in_fps_d = 1;
    }

    json_object_set_new (in_jobj, "width", json_integer (in_width));
    json_object_set_new (in_jobj, "height", json_integer (in_height));

    fps_jobj = json_object ();
    if (!fps_jobj)
      goto error;

    json_object_set_new (fps_jobj, "num", json_integer (in_fps_n));
    json_object_set_new (fps_jobj, "den", json_integer (in_fps_d));
    json_object_set_new (in_jobj, "frame-rate", fps_jobj);
  }
  res_jobj = json_object ();
  if (!res_jobj)
    goto error;

  json_object_set_new (res_jobj, "input", in_jobj);
  json_object_set_new (res_jobj, "output", jarray);

  tmp_jobj = json_object ();
  if (!tmp_jobj)
    goto error;

  json_object_set_new (tmp_jobj, "function", json_string ("SCALER"));
  json_object_set_new (tmp_jobj, "format", json_string ("yuv420p"));
  json_object_set_new (tmp_jobj, "resolution", res_jobj);

  jarray = json_array ();
  if (!jarray)
    goto error;

  json_array_append_new (jarray, tmp_jobj);

  tmp_jobj = json_object ();
  if (!tmp_jobj)
    goto error;

  json_object_set_new (tmp_jobj, "resources", jarray);

  tmp2_jobj = json_object ();
  if (!tmp2_jobj)
    goto error;

  json_object_set_new (tmp2_jobj, "parameters", tmp_jobj);

  tmp_jobj = json_object ();
  if (!tmp_jobj)
    goto error;

  json_object_set_new (tmp_jobj, "request", tmp2_jobj);

  req_str = json_dumps (tmp_jobj, JSON_DECODE_ANY);
  GST_LOG_OBJECT (compositor, "prepared xrm request %s", req_str);
  json_decref (tmp_jobj);

  return req_str;

out_error:
  if (out_caps)
    gst_caps_unref (out_caps);
  if (out_jobj)
    json_decref (out_jobj);
  if (fps_jobj)
    json_decref (fps_jobj);
  goto error;
error:
  return NULL;
}

static gboolean
vvas_xcompositor_calculate_load (GstVvasXCompositor * self, gint * load)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  int iret = -1, func_id = 0;
  gchar *req_str;
  char plugin_name[XRM_MAX_NAME_LEN];
  xrmPluginFuncParam param;

  if (!priv->xrm_ctx) {
    GST_ERROR_OBJECT (self, "xrm context not created");
    return FALSE;
  }

  /* prepare json string to request xrm for load */
  req_str = vvas_xcompositor_prepare_request_json_string (self);
  if (!req_str) {
    GST_ERROR_OBJECT (self, "failed to prepare xrm json request string");
    return FALSE;
  }

  memset (plugin_name, 0x0, XRM_MAX_NAME_LEN);

  strcpy (plugin_name, "xrmU30ScalPlugin");

  if (strlen (req_str) > (XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1)) {
    GST_ERROR_OBJECT (self,
        "request input string length %lu > max allowed %d",
        strlen (req_str), XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1);
    free (req_str);
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
    priv->has_error = TRUE;
    return FALSE;
  }

  *load = atoi ((char *) (strtok (param.output, " ")));

  if (*load <= 0 || *load > XRM_MAX_CU_LOAD_GRANULARITY_1000000) {
    GST_ERROR_OBJECT (self, "not an allowed multiscaler load %d", *load);
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
        ("wrong multiscaler load %d", *load), NULL);
    return FALSE;
  }

  GST_INFO_OBJECT (self, "need %d%% device's load",
      (*load * 100) / XRM_MAX_CU_LOAD_GRANULARITY_1000000);
  return TRUE;
}

static gboolean
vvas_xcompositor_allocate_resource (GstVvasXCompositor * self,
    gint compositor_load)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  int iret = -1;

  GST_INFO_OBJECT (self, "going to request %d%% load using xrm",
      (compositor_load * 100) / XRM_MAX_CU_LOAD_GRANULARITY_1000000);

  if (getenv ("XRM_RESERVE_ID") || priv->reservation_id) {      /* use reservation_id to allocate compositor */
    int xrm_reserve_id = 0;
    xrmCuPropertyV2 compositor_prop;
    xrmCuResourceV2 *cu_resource;

    memset (&compositor_prop, 0, sizeof (xrmCuPropertyV2));

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

    compositor_prop.poolId = xrm_reserve_id;

    strcpy (compositor_prop.kernelName, strtok (self->kern_name, ":"));
    strcpy (compositor_prop.kernelAlias, "SCALER_MPSOC");
    compositor_prop.devExcl = false;
    compositor_prop.requestLoad =
        XRM_PRECISION_1000000_BIT_MASK (compositor_load);

    if (self->dev_index != -1) {
      uint64_t deviceInfoContraintType =
          XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX;
      uint64_t deviceInfoDeviceIndex = self->dev_index;

      compositor_prop.deviceInfo =
          (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) |
          (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
    }
    iret = xrmCuAllocV2 (priv->xrm_ctx, &compositor_prop, cu_resource);

    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (self,
          "failed to allocate resources from reservation id %d",
          xrm_reserve_id);
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from reservation id %d",
              xrm_reserve_id), NULL);
      return FALSE;
    }

    self->dev_index = cu_resource->deviceId;
    priv->cu_index = cu_resource->cuId;
    uuid_copy (priv->xclbinId, cu_resource->uuid);
    priv->cu_resource_v2 = cu_resource;

  } else {                      /* use user specified device to allocate compositor */
    xrmCuProperty compositor_prop;
    xrmCuResource *cu_resource;

    memset (&compositor_prop, 0, sizeof (xrmCuProperty));

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


    strcpy (compositor_prop.kernelName, strtok (self->kern_name, ":"));
    strcpy (compositor_prop.kernelAlias, "SCALER_MPSOC");
    compositor_prop.devExcl = false;
    compositor_prop.requestLoad =
        XRM_PRECISION_1000000_BIT_MASK (compositor_load);
    iret =
        xrmCuAllocFromDev (priv->xrm_ctx, self->dev_index, &compositor_prop,
        cu_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (self,
          "failed to allocate resources from device id %d. "
          "error: %d", self->dev_index, iret);
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from device id %d",
              self->dev_index), NULL);
      return FALSE;
    }

    if (self->dev_index != cu_resource->deviceId) {
      GST_ERROR_OBJECT (self, "invalid parameters received from XRM");
      return FALSE;
    }

    self->dev_index = cu_resource->deviceId;
    priv->cu_index = cu_resource->cuId;
    uuid_copy (priv->xclbinId, cu_resource->uuid);
    priv->cu_resource = cu_resource;
  }


  return TRUE;
}
#endif

static gboolean
vvas_xcompositor_allocate_internal_buffers (GstVvasXCompositor * self)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  gint chan_id, iret;

  GST_INFO_OBJECT (self, "allocating internal buffers at mem bank %d",
      self->in_mem_bank);

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    iret =
        vvas_xrt_alloc_xrt_buffer (priv->dev_handle, COEFF_SIZE,
        VVAS_BO_FLAGS_NONE, self->in_mem_bank, &priv->Hcoff[chan_id]);
    if (iret < 0) {
      GST_ERROR_OBJECT (self,
          "failed to allocate horizontal coefficients command buffer..");
      goto error;
    }

    iret =
        vvas_xrt_alloc_xrt_buffer (priv->dev_handle, COEFF_SIZE,
        VVAS_BO_FLAGS_NONE, self->in_mem_bank, &priv->Vcoff[chan_id]);
    if (iret < 0) {
      GST_ERROR_OBJECT (self,
          "failed to allocate vertical coefficients command buffer..");
      goto error;
    }

    iret =
        vvas_xrt_alloc_xrt_buffer (priv->dev_handle, DESC_SIZE,
        VVAS_BO_FLAGS_NONE, self->in_mem_bank, &priv->msPtr[chan_id]);
    if (iret < 0) {
      GST_ERROR_OBJECT (self,
          "failed to allocate vertical coefficients command buffer..");
      goto error;
    }
  }
#ifdef DEBUG
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    printf ("DESC phy %lx  virt  %p \n", priv->msPtr[chan_id].phy_addr,
        priv->msPtr[chan_id].user_ptr);
    printf ("HCoef phy %lx  virt  %p \n", priv->Hcoff[chan_id].phy_addr,
        priv->Hcoff[i].user_ptr);
    printf ("VCoef phy %lx  virt  %p \n", priv->Vcoff[chan_id].phy_addr,
        priv->Vcoff[chan_id].user_ptr);
  }
#endif
  return TRUE;

error:
  GST_ELEMENT_ERROR (self, RESOURCE, NO_SPACE_LEFT, NULL,
      ("failed to allocate memory"));
  return FALSE;
}

static void
vvas_xcompositor_adjust_zorder_after_eos (GstVvasXCompositor * self,
    guint pad_index)
{
  guint zorder_index = 0;
/* get the zorder of pad that got eos */
  while (self->priv->pad_of_zorder[zorder_index] != pad_index)
    zorder_index++;
/* Increase the zorder of pads that are behind the eos pad by 1 */
  for (guint i = zorder_index; i > 0; i--)
    self->priv->pad_of_zorder[i] = self->priv->pad_of_zorder[i - 1];
/* Set the zorder of pad that got eos to 0 */
  self->priv->pad_of_zorder[0] = pad_index;

  for (zorder_index = 0; zorder_index < self->num_request_pads; zorder_index++)
    GST_INFO_OBJECT (self, "pad after adjustment at zorder %d is %d ",
        zorder_index, self->priv->pad_of_zorder[zorder_index]);
}

static gboolean
vvas_xcompositor_set_zorder_from_pads (GstVvasXCompositor * self)
{
  gint chan_id, index;
  GList *unfilled_pads_list = NULL, *missing_zorders_list = NULL;
  GstVvasXCompositorPad *pad;
  gint zorder[MAX_CHANNELS];

  gint *missing_zorders_data =
      (gint *) malloc (sizeof (gint) * self->num_request_pads);
  gint *unfilled_pads_data =
      (gint *) malloc (sizeof (gint) * self->num_request_pads);
  /* Initialising  missing_zorders_list list from 0 to number of request pads */
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    missing_zorders_data[chan_id] = chan_id;
    missing_zorders_list =
        g_list_append (missing_zorders_list,
        (gpointer) (&missing_zorders_data[chan_id]));
  }

  for (index = 0; index < self->num_request_pads; index++) {
    pad = gst_vvas_xcompositor_sinkpad_at_index (self, index);
    if ((pad->zorder) < (int) (self->num_request_pads))
      zorder[index] = pad->zorder;
    else {
      GST_ERROR_OBJECT (self,
          "zorder value %d of pad sink_%d exceeding current limit %d or invalid",
          pad->zorder, index, self->num_request_pads - 1);
      g_free (missing_zorders_data);
      g_free (unfilled_pads_data);
      return FALSE;
    }

    unfilled_pads_data[index] = index;
    /* Creating a list of unfilled pads with zorder */
    if (pad->zorder == -1)
      unfilled_pads_list =
          g_list_append (unfilled_pads_list,
          (gpointer) (&unfilled_pads_data[index]));

    /* Removing already assigned zorders from already initialised missing list */
    else {
      missing_zorders_list =
          g_list_remove (missing_zorders_list,
          (gpointer) (&missing_zorders_data[pad->zorder]));
    }
  }

  /* Filling the unfilled pads with missing zorders */
  for (index = 0; index < g_list_length (unfilled_pads_list); index++) {

    chan_id = *((gint *) g_list_nth_data (unfilled_pads_list, index));

    zorder[chan_id] = *((gint *) g_list_nth_data (missing_zorders_list, index));
  }
  /* Mapping a pad for each zorder */
  for (index = 0; index < self->num_request_pads; index++)
    self->priv->pad_of_zorder[zorder[index]] = index;
  g_free (missing_zorders_data);
  g_free (unfilled_pads_data);
  for (index = 0; index < self->num_request_pads; index++)
    GST_INFO_OBJECT (self, "Final zorder at pad %d is %d ", index,
        zorder[index]);

  return TRUE;
}

static gboolean
vvas_xcompositor_open (GstVvasXCompositor * self)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  guint chan_id = 0;
  gboolean bret;
#ifdef ENABLE_XRM_SUPPORT
  gint load = -1;
#endif

  self->priv->is_coeff = true;
  GST_DEBUG_OBJECT (self, "Xlnx Compositor open");

  if (!self->kern_name) {
    GST_ERROR_OBJECT (self, "kernel name is not set");
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("kernel name is not set"));
    return FALSE;
  }
#ifdef ENABLE_XRM_SUPPORT
  bret = vvas_xcompositor_calculate_load (self, &load);
  if (!bret)
    return FALSE;

  priv->cur_load = load;
  /* gets cu index & device id (using reservation id) */
  bret = vvas_xcompositor_allocate_resource (self, priv->cur_load);
  if (!bret)
    return FALSE;
#endif

#ifdef XLNX_PCIe_PLATFORM
  if (self->dev_index < 0) {
    GST_ERROR_OBJECT (self, "device index %d is not valid", self->dev_index);
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("device index %d is not valid", self->dev_index));
    return FALSE;
  }
#endif

  if (!vvas_xrt_open_device (self->dev_index, &priv->dev_handle)) {
    GST_ERROR_OBJECT (self, "failed to open device index %u", self->dev_index);
    return FALSE;
  }
/* TODO: Need to uncomment after CR-1122125 is resolved */
//#ifndef ENABLE_XRM_SUPPORT

  if (!self->xclbin_path) {
    GST_ERROR_OBJECT (self, "invalid xclbin path %s", self->xclbin_path);
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, (NULL),
        ("xclbin path not set"));
    return FALSE;
  }


  GST_INFO_OBJECT (self, "xclbin path %s", self->xclbin_path);
/* We have to download the xclbin irrespective of XRM or not as there
   * mismatch of UUID between XRM and XRT Native. CR-1122125 raised */
  if (vvas_xrt_download_xclbin (self->xclbin_path,
          priv->dev_handle, &(priv->xclbinId))) {
    GST_ERROR_OBJECT (self, "failed to initialize XRT");
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("xclbin download failed"));
    return FALSE;
  }

  if (!self->kern_name)
    self->kern_name = g_strdup (DEFAULT_KERNEL_NAME);


//#endif
  if (vvas_xrt_open_context (priv->dev_handle, priv->xclbinId,
          &priv->kern_handle, self->kern_name, true)) {

    GST_ERROR_OBJECT (self, "failed to open XRT context ...");
    return FALSE;
  }
  if (!priv->is_internal_buf_allocated) {
    /* allocate internal buffers */
    bret = vvas_xcompositor_allocate_internal_buffers (self);
    if (!bret)
      return FALSE;
    priv->is_internal_buf_allocated = TRUE;
  }
  bret = vvas_xcompositor_set_zorder_from_pads (self);
  if (!bret)
    return FALSE;
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    if (GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo[chan_id]) % self->ppc) {
      GST_ERROR_OBJECT (self, "Unsupported input resolution at sink_%d,"
          "width must be multiple of ppc i.e, %d", chan_id, self->ppc);
      return FALSE;
    }

    if (self->num_taps == 12) {
      vvas_xcompositor_prepare_coefficients_with_12tap (self, chan_id);
    } else {
      if (self->scale_mode == POLYPHASE) {
        float scale =
            (float) GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo[chan_id]) /
            (float) GST_VIDEO_INFO_HEIGHT (self->priv->out_vinfo);
        GST_INFO_OBJECT (self,
            "preparing coefficients with scaling ration %f and taps %d",
            scale, self->num_taps);
        xlnx_multiscaler_coff_fill (priv->Hcoff[chan_id].user_ptr,
            priv->Vcoff[chan_id].user_ptr, scale);
      }
    }
  }

  if (GST_VIDEO_INFO_WIDTH (self->priv->out_vinfo) % self->ppc) {
    GST_ERROR_OBJECT (self, "Unsupported output resolution,"
        "width must be multiple of ppc i.e, %d", self->ppc);
    return FALSE;
  }
#ifdef XLNX_PCIe_PLATFORM
  if (!xlnx_abr_coeff_syncBO (self))
    return FALSE;
#endif
  return TRUE;
}

static void
vvas_xcompositor_free_internal_buffers (GstVvasXCompositor * self)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  guint chan_id;

  GST_DEBUG_OBJECT (self, "freeing internal buffers");

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    if (priv->Hcoff[chan_id].user_ptr) {
      vvas_xrt_free_xrt_buffer (&priv->Hcoff[chan_id]);
      memset (&(self->priv->Hcoff[chan_id]), 0x0, sizeof (xrt_buffer));
    }
    if (priv->Vcoff[chan_id].user_ptr) {
      vvas_xrt_free_xrt_buffer (&priv->Vcoff[chan_id]);
      memset (&(self->priv->Vcoff[chan_id]), 0x0, sizeof (xrt_buffer));
    }
    if (priv->msPtr[chan_id].user_ptr) {
      vvas_xrt_free_xrt_buffer (&priv->msPtr[chan_id]);
      memset (&(self->priv->msPtr[chan_id]), 0x0, sizeof (xrt_buffer));
    }
  }
}

static void
vvas_xcompositor_close (GstVvasXCompositor * self)
{
  guint chan_id;
  GstVvasXCompositorPad *sinkpad;
  GstVvasXCompositorPrivate *priv = self->priv;
  gboolean iret = FALSE;
  GST_DEBUG_OBJECT (self, "Closing");

  vvas_xcompositor_free_internal_buffers (self);
  self->priv->is_internal_buf_allocated = FALSE;

  //clear all buffer pools here
  gst_clear_object (&self->priv->output_pool);

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    sinkpad = gst_vvas_xcompositor_sinkpad_at_index (self, chan_id);
    if (sinkpad && sinkpad->pool) {
      if (gst_buffer_pool_is_active (sinkpad->pool))
        gst_buffer_pool_set_active (sinkpad->pool, FALSE);
      gst_clear_object (&sinkpad->pool);
    }
  }

  if (priv->dev_handle) {
    iret = vvas_xrt_close_context (priv->kern_handle);
    if (iret != 0) {
      GST_ERROR_OBJECT (self, "failed to close xrt context");
    }
    vvas_xrt_close_device (priv->dev_handle);
    priv->dev_handle = NULL;
    GST_INFO_OBJECT (self, "closed xrt context");
  }

}

static gpointer
vvas_xcompositor_input_copy_thread (gpointer data)
{
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (data);
  GstVvasXCompositorPrivate *priv = self->priv;
  guint chan_id;
  while (1) {
    for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {

      GstBuffer *inbuf = NULL;
      GstBuffer *own_inbuf = NULL;
      GstVideoFrame in_vframe, own_vframe;
      GstFlowReturn fret = GST_FLOW_OK;
      GstVvasXCompositorPad *sinkpad;
      sinkpad = gst_vvas_xcompositor_sinkpad_at_index (self, chan_id);
      inbuf = (GstBuffer *) g_async_queue_pop (priv->copy_inqueue[chan_id]);
      if (inbuf == STOP_COMMAND) {
        GST_DEBUG_OBJECT (sinkpad, "received stop command. exit copy thread");
        goto exit_loop;
      }

      /* acquire buffer from own input pool */
      fret = gst_buffer_pool_acquire_buffer (sinkpad->pool, &own_inbuf, NULL);
      if (fret != GST_FLOW_OK) {
        GST_ERROR_OBJECT (sinkpad, "failed to allocate buffer from pool %p",
            sinkpad->pool);
        goto error;
      }
      GST_LOG_OBJECT (sinkpad, "acquired buffer %p from own pool", own_inbuf);

      /* map internal buffer in write mode */
      if (!gst_video_frame_map (&own_vframe, priv->in_vinfo[chan_id], own_inbuf,
              GST_MAP_WRITE)) {
        GST_ERROR_OBJECT (self, "failed to map internal input buffer");
        goto error;
      }

      /* map input buffer in read mode */
      if (!gst_video_frame_map (&in_vframe, priv->in_vinfo[chan_id], inbuf,
              GST_MAP_READ)) {
        GST_ERROR_OBJECT (sinkpad, "failed to map input buffer");
        goto error;
      }
      gst_video_frame_copy (&own_vframe, &in_vframe);

      gst_video_frame_unmap (&in_vframe);
      gst_video_frame_unmap (&own_vframe);
      gst_buffer_copy_into (own_inbuf, inbuf,
          (GstBufferCopyFlags) (GST_BUFFER_COPY_METADATA), 0, -1);
      GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, self,
          "slow copy to internal input pool buffer");
      gst_buffer_unref (inbuf);
      g_async_queue_push (priv->copy_outqueue[chan_id], own_inbuf);
    }
  exit_loop:
    break;
  }


error:
  return NULL;
}

static GstStateChangeReturn
gst_vvas_xcompositor_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      vvas_xcompositor_close (self);
      break;

    default:
      break;
  }

  return ret;
}

static void
gst_vvas_xcompositor_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{

  GstVvasXCompositorPad *pad = GST_VVAS_XCOMPOSITOR_PAD (object);

  switch (prop_id) {
    case PROP_PAD_XPOS:
      g_value_set_uint (value, pad->xpos);
      break;
    case PROP_PAD_YPOS:
      g_value_set_uint (value, pad->ypos);
      break;
    case PROP_PAD_WIDTH:
      g_value_set_int (value, pad->width);
      break;
    case PROP_PAD_HEIGHT:
      g_value_set_int (value, pad->height);
      break;
    case PROP_PAD_ZORDER:
      g_value_set_int (value, pad->zorder);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_xcompositor_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvasXCompositorPad *pad = GST_VVAS_XCOMPOSITOR_PAD (object);

  switch (prop_id) {
    case PROP_PAD_XPOS:
      pad->xpos = g_value_get_uint (value);
      break;
    case PROP_PAD_YPOS:
      pad->ypos = g_value_get_uint (value);
      break;
    case PROP_PAD_WIDTH:
      pad->width = g_value_get_int (value);
      break;
    case PROP_PAD_HEIGHT:
      pad->height = g_value_get_int (value);
      break;
    case PROP_PAD_ZORDER:
      pad->zorder = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_vvas_xcompositor_pad_class_init (GstVvasXCompositorPadClass * klass)
{
  GObjectClass *gobject_class;
  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_vvas_xcompositor_pad_set_property;
  gobject_class->get_property = gst_vvas_xcompositor_pad_get_property;
  g_object_class_install_property (gobject_class, PROP_PAD_XPOS,
      g_param_spec_uint ("xpos", "X Position",
          "X Position of the picture",
          0, G_MAXINT,
          DEFAULT_PAD_XPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_YPOS,
      g_param_spec_uint ("ypos", "Y Position",
          "Y Position of the picture",
          0, G_MAXINT,
          DEFAULT_PAD_YPOS,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_WIDTH,
      g_param_spec_int ("width", "Width",
          "Width of the picture",
          -1, G_MAXINT,
          DEFAULT_PAD_WIDTH,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_HEIGHT,
      g_param_spec_int ("height", "Height",
          "Height of the picture",
          -1, G_MAXINT,
          DEFAULT_PAD_HEIGHT,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_ZORDER,
      g_param_spec_int ("zorder", "Zorder",
          "zorder of the input stream",
          -1, MAX_CHANNELS,
          DEFAULT_PAD_ZORDER,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

}

static void
gst_vvas_xcompositor_pad_init (GstVvasXCompositorPad * pad)
{
  pad->xpos = DEFAULT_PAD_XPOS;
  pad->ypos = DEFAULT_PAD_YPOS;
  pad->width = DEFAULT_PAD_WIDTH;
  pad->height = DEFAULT_PAD_HEIGHT;
  pad->zorder = DEFAULT_PAD_ZORDER;
  pad->is_eos = DEFAULT_PAD_EOS;
}


static void
gst_vvas_xcompositor_class_init (GstVvasXCompositorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstVideoAggregatorClass *videoaggregator_class =
      (GstVideoAggregatorClass *) klass;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vvas_xcompositor", 0,
      "VVAS Compositor");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_vvas_xcompositor_finalize);

  gobject_class->get_property = gst_vvas_xcompositor_get_property;
  gobject_class->set_property = gst_vvas_xcompositor_set_property;

  gst_element_class_set_metadata (element_class, "VVAS Compositor",
      "Vvas Compositor", "VVAS Compositor", "Xilinx Inc <www.xilinx.com>");

  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &src_factory, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &sink_factory, GST_TYPE_VVAS_XCOMPOSITOR_PAD);

  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_vvas_xcompositor_request_new_pad);
  element_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_vvas_xcompositor_release_pad);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vvas_xcompositor_change_state);
  agg_class->stop = gst_vvas_xcompositor_stop;
  agg_class->start = gst_vvas_xcompositor_start;

  agg_class->src_query = gst_vvas_xcompositor_src_query;
  agg_class->sink_query = gst_vvas_xcompositor_sink_query;

  agg_class->decide_allocation = vvas_xcompositor_decide_allocation;

  videoaggregator_class->aggregate_frames =
      gst_vvas_xcompositor_aggregate_frames;
  videoaggregator_class->create_output_buffer =
      gst_vvas_xcompositor_create_output_buffer;
  g_object_class_install_property (gobject_class, PROP_XCLBIN_LOCATION,
      g_param_spec_string ("xclbin-location", "xclbin file location",
          "Location of the xclbin to program devices", NULL, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_KERN_NAME,
      g_param_spec_string ("kernel-name",
          "kernel name and instance",
          "String defining the kernel name and instance as mentioned in xclbin",
          NULL, (GParamFlags)
          (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

#ifdef XLNX_PCIe_PLATFORM
  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("dev-idx",
          "Device index",
          "Device index", -1, 31,
          DEFAULT_DEVICE_INDEX, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif


  g_object_class_install_property (gobject_class, PROP_PPC,
      g_param_spec_enum ("ppc",
          "pixel per clock",
          "Pixel per clock configured in Multiscaler kernel",
          VVAS_XCOMPOSITOR_PPC_TYPE,
          VVAS_XCOMPOSITOR_DEFAULT_PPC,
          G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject_class, PROP_SCALE_MODE,
      g_param_spec_int ("scale-mode", "Scaling Mode", "Scale Mode configured in Multiscaler kernel. 	\
		0: BILINEAR \n 1: BICUBIC \n2: POLYPHASE", 0, 2, VVAS_XCOMPOSITOR_SCALE_MODE, G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_IN_MEM_BANK,
      g_param_spec_uint ("in-mem-bank",
          "VVAS Input Memory Bank",
          "VVAS input memory bank to allocate memory",
          0, G_MAXUSHORT,
          DEFAULT_MEM_BANK,
          G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_OUT_MEM_BANK,
      g_param_spec_uint ("out-mem-bank",
          "VVAS Output Memory Bank",
          "VVAS output memory bank to allocate memory",
          0, G_MAXUSHORT,
          DEFAULT_MEM_BANK,
          G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_NUM_TAPS,
      g_param_spec_enum ("num-taps",
          "Number filter taps",
          "Number of filter taps to be used for scaling",
          VVAS_XCOMPOSITOR_NUM_TAPS_TYPE,
          VVAS_XCOMPOSITOR_DEFAULT_NUM_TAPS,
          G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_COEF_LOADING_TYPE,
      g_param_spec_enum ("coef-load-type",
          "Coefficients loading type",
          "coefficients loading type for scaling",
          VVAS_XCOMPOSITOR_COEF_LOAD_TYPE,
          VVAS_XCOMPOSITOR_DEFAULT_COEF_LOAD_TYPE,
          G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_AVOID_OUTPUT_COPY,
      g_param_spec_boolean ("avoid-output-copy",
          "Avoid output frames copy",
          "Avoid output frames copy on all source pads even when downstream"
          " does not support GstVideoMeta metadata",
          VVAS_XCOMPOSITOR_AVOID_OUTPUT_COPY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject_class, PROP_ENABLE_PIPELINE,
      g_param_spec_boolean ("enable-pipeline",
          "Enable pipelining",
          "Enable buffer pipelining to improve performance in non zero-copy use cases",
          VVAS_XCOMPOSITOR_ENABLE_PIPELINE_DEFAULT,
          G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_BEST_FIT,
      g_param_spec_boolean ("best-fit",
          "Enables best fit of the input video",
          "downscale/upscale the input video to best-fit in each window",
          VVAS_XCOMPOSITOR_BEST_FIT_DEFAULT,
          G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

}

static void
gst_vvas_xcompositor_init (GstVvasXCompositor * self)
{
  self->xclbin_path = NULL;
  self->num_request_pads = 0;
  self->sinkpads = NULL;
  self->out_stride_align = 1;
  self->out_elevation_align = 1;
  self->priv = gst_vvas_xcompositor_get_instance_private (self);
  self->priv->output_pool = NULL;
  self->num_taps = VVAS_XCOMPOSITOR_DEFAULT_NUM_TAPS;
  self->coef_load_type = VVAS_XCOMPOSITOR_DEFAULT_COEF_LOAD_TYPE;
  self->best_fit = VVAS_XCOMPOSITOR_BEST_FIT_DEFAULT;
  self->dev_index = DEFAULT_DEVICE_INDEX;
  self->ppc = VVAS_XCOMPOSITOR_DEFAULT_PPC;
  self->scale_mode = VVAS_XCOMPOSITOR_SCALE_MODE;
  self->in_mem_bank = DEFAULT_MEM_BANK;
  self->out_mem_bank = DEFAULT_MEM_BANK;
  self->avoid_output_copy = VVAS_XCOMPOSITOR_AVOID_OUTPUT_COPY_DEFAULT;
  self->enabled_pipeline = VVAS_XCOMPOSITOR_ENABLE_PIPELINE_DEFAULT;
#ifdef ENABLE_PPE_SUPPORT
  self->priv->alpha_r = 0;
  self->priv->alpha_g = 0;
  self->priv->alpha_b = 0;
  self->priv->beta_r = 1;
  self->priv->beta_g = 1;
  self->priv->beta_b = 1;
#endif
#ifdef ENABLE_XRM_SUPPORT
  self->priv->xrm_ctx = NULL;
  self->priv->cu_resource = NULL;
  self->priv->cur_load = 0;
  self->priv->reservation_id = 0;
  self->priv->has_error = FALSE;
#endif
  self->priv->is_internal_buf_allocated = FALSE;
  self->kern_name = g_strdup (DEFAULT_KERNEL_NAME);
  self->priv->need_copy = TRUE;
  for (int chan_id = 0; chan_id < MAX_CHANNELS; chan_id++) {
    self->priv->in_vinfo[chan_id] = gst_video_info_new ();
    self->priv->pad_of_zorder[chan_id] = chan_id;
  }
}

static void
gst_vvas_xcompositor_finalize (GObject * object)
{
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (object);
  for (int chan_id = 0; chan_id < MAX_CHANNELS; chan_id++)
    gst_video_info_free (self->priv->in_vinfo[chan_id]);
  gst_video_info_free (self->priv->out_vinfo);
  g_free (self->xclbin_path);
  g_free (self->kern_name);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_vvas_xcompositor_query_caps (GstPad * pad, GstAggregator * agg,
    GstQuery * query)
{
  GstCaps *filter, *caps;
  gst_query_parse_caps (query, &filter);

  caps = gst_pad_get_current_caps (agg->srcpad);
  if (caps == NULL) {
    caps = gst_pad_get_pad_template_caps (agg->srcpad);
  }

  if (filter)
    caps = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);

  gst_query_set_caps_result (query, caps);

  gst_caps_unref (caps);

  return TRUE;
}

static gboolean
gst_vvas_xcompositor_src_query (GstAggregator * agg, GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {

    case GST_QUERY_CAPS:
      return gst_vvas_xcompositor_query_caps (agg->srcpad, agg, query);
      break;

    case GST_QUERY_ALLOCATION:
      return TRUE;
      break;

    default:
      return TRUE;              // ??
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_query (agg, query);
}

static gboolean
gst_vvas_xcompositor_pad_sink_acceptcaps (GstPad * pad,
    GstVvasXCompositor * compositor, GstCaps * caps)
{
  gboolean ret;
  GstCaps *template_caps;

  GST_DEBUG_OBJECT (pad, "try accept caps of %" GST_PTR_FORMAT, caps);

  template_caps = gst_pad_get_pad_template_caps (pad);
  template_caps = gst_caps_make_writable (template_caps);

  ret = gst_caps_can_intersect (caps, template_caps);
  GST_DEBUG_OBJECT (pad, "%saccepted caps %" GST_PTR_FORMAT,
      (ret ? "" : "not "), caps);
  gst_caps_unref (template_caps);

  return ret;
}

static guint
vvas_xcompositor_get_padding_right (GstVvasXCompositor * self,
    GstVideoInfo * info)
{
  guint padding_pixels = -1;
  guint plane_stride = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  guint padding_bytes =
      ALIGN (plane_stride, self->out_stride_align) - plane_stride;

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_NV12:
      padding_pixels = padding_bytes;
      break;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_r210:
    case GST_VIDEO_FORMAT_Y410:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
      padding_pixels = padding_bytes / 4;
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      padding_pixels = padding_bytes / 2;
      break;
    case GST_VIDEO_FORMAT_NV16:
      padding_pixels = padding_bytes;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_v308:
    case GST_VIDEO_FORMAT_BGR:
      padding_pixels = padding_bytes / 3;
      break;
    case GST_VIDEO_FORMAT_I422_10LE:
      padding_pixels = padding_bytes / 2;
      break;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      padding_pixels = (padding_bytes * 3) / 4;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      padding_pixels = padding_bytes;
      break;
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      padding_pixels = (padding_bytes * 3) / 4;
      break;
    case GST_VIDEO_FORMAT_I420:
      padding_pixels = padding_bytes;
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
      padding_pixels = padding_bytes / 2;
      break;
    default:
      GST_ERROR_OBJECT (self, "not yet supporting format %d",
          GST_VIDEO_INFO_FORMAT (info));
  }
  return padding_pixels;
}

static gboolean
vvas_xcompositor_propose_allocation (GstVvasXCompositor * self,
    GstQuery * query)
{
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool;
  guint size;
  GstVideoAlignment align;

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstStructure *structure;
    GstAllocator *allocator = NULL;
    GstAllocationParams params =
        { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0, 0 };

    if (gst_query_get_n_allocation_params (query) > 0) {
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    } else {
      /*#ifdef ENABLE_XRM_SUPPORT
         if (!(self->priv->cu_resource || self->priv->cu_resource_v2))  {
         GST_ERROR_OBJECT (self, "scaler resource not yet allocated");
         return FALSE;
         }
         #endif */
      allocator = gst_vvas_allocator_new (self->dev_index,
          NEED_DMABUF, self->in_mem_bank, self->priv->kern_handle);
      GST_INFO_OBJECT (self, "creating new xrt allocator %" GST_PTR_FORMAT
          "at mem bank %d", allocator, self->in_mem_bank);

      gst_query_add_allocation_param (query, allocator, &params);
    }

    pool = gst_vvas_buffer_pool_new (WIDTH_ALIGN, HEIGHT_ALIGN);
    GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

    structure = gst_buffer_pool_get_config (pool);

    gst_video_alignment_reset (&align);
    align.padding_top = 0;
    align.padding_left = 0;
    align.padding_right = vvas_xcompositor_get_padding_right (self, &info);
    align.padding_bottom =
        ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
        HEIGHT_ALIGN) - GST_VIDEO_INFO_HEIGHT (&info);
    gst_video_info_align (&info, &align);

    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (structure, &align);

    size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (structure, caps, size, 2, 0);

    gst_buffer_pool_config_set_allocator (structure, allocator, &params);

    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    if (allocator)
      gst_object_unref (allocator);

    if (!gst_buffer_pool_set_config (pool, structure))
      goto config_failed;

    GST_OBJECT_LOCK (self);
    gst_query_add_allocation_pool (query, pool, size, 2, 0);

    GST_OBJECT_UNLOCK (self);

    gst_query_add_allocation_pool (query, pool, size, 2, 0);
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
    gst_object_unref (pool);
  }

  return TRUE;

  /* ERRORS */
config_failed:
  {
    GST_ERROR_OBJECT (self, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }
}

static gboolean
gst_vvas_xcompositor_sink_query (GstAggregator * agg, GstAggregatorPad * bpad,
    GstQuery * query)
{
  GstCaps *tmp;
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (agg);
  GstVvasXCompositorPad *sinkpad = (GstVvasXCompositorPad *) bpad;
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {

    case GST_QUERY_CAPS:
    {
      GstCaps *mycaps;
      gst_query_parse_caps (query, &tmp);
      mycaps = gst_pad_get_pad_template_caps (GST_PAD (bpad));
      gst_query_set_caps_result (query, mycaps);
      return TRUE;
    }

    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;
      int pad_idx;
      gst_query_parse_accept_caps (query, &caps);

      ret =
          gst_vvas_xcompositor_pad_sink_acceptcaps (GST_PAD (bpad), self, caps);
      if (ret) {
        pad_idx = sinkpad->index;
        gst_video_info_from_caps (self->priv->in_vinfo[pad_idx], caps);
      }
      gst_query_set_accept_caps_result (query, ret);
    }
      return TRUE;
      break;

    case GST_QUERY_ALLOCATION:
    {
      ret = vvas_xcompositor_propose_allocation (self, query);
      return ret;
    }
      break;

    default:
      return GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, bpad, query);
  }
}

static gboolean
vvas_xcompositor_allocate_internal_pool (GstVvasXCompositor * self,
    GstVvasXCompositorPad * sinkpad)
{
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstCaps *caps = NULL;
  GstVideoAlignment align;

  caps = gst_pad_get_current_caps (GST_PAD (sinkpad));

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  pool = gst_vvas_buffer_pool_new (WIDTH_ALIGN, HEIGHT_ALIGN);
  GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

  config = gst_buffer_pool_get_config (pool);
  gst_video_alignment_reset (&align);
  align.padding_top = 0;
  align.padding_left = 0;
  align.padding_right = vvas_xcompositor_get_padding_right (self, &info);
  align.padding_bottom =
      ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
      HEIGHT_ALIGN) - GST_VIDEO_INFO_HEIGHT (&info);
  gst_video_info_align (&info, &align);

  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_set_video_alignment (config, &align);

  allocator = gst_vvas_allocator_new (self->dev_index,
      NEED_DMABUF, self->in_mem_bank, self->priv->kern_handle);
  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
  alloc_params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;
  GST_INFO_OBJECT (self,
      "allocated %" GST_PTR_FORMAT " allocator at mem bank %d",
      allocator, self->in_mem_bank);

  gst_buffer_pool_config_set_params (config, caps,
      GST_VIDEO_INFO_SIZE (&info), 3, 5);

  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (allocator)
    gst_object_unref (allocator);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config on input pool");
    goto error;
  }

  if (sinkpad->pool)
    gst_object_unref (sinkpad->pool);

  sinkpad->pool = pool;

  GST_INFO_OBJECT (self, "allocated %" GST_PTR_FORMAT " pool", sinkpad->pool);
  gst_caps_unref (caps);


  return TRUE;

error:
  gst_caps_unref (caps);
  return FALSE;
}

#ifdef XLNX_PCIe_PLATFORM
static gboolean
xlnx_abr_coeff_syncBO (GstVvasXCompositor * self)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  int chan_id;
  int iret;

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {

    iret = vvas_xrt_write_bo (priv->Hcoff[chan_id].bo,
        priv->Hcoff[chan_id].user_ptr, priv->Hcoff[chan_id].size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (self,
          "failed to write horizontal coefficients. reason : %s",
          strerror (errno));
      return FALSE;
    }
    iret = vvas_xrt_sync_bo (priv->Hcoff[chan_id].bo,
        VVAS_BO_SYNC_BO_TO_DEVICE, priv->Hcoff[chan_id].size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (self,
          "failed to sync horizontal coefficients. reason : %s",
          strerror (errno));
      GST_ELEMENT_ERROR (self, RESOURCE, SYNC, NULL,
          ("failed to sync horizontal coefficients to device. reason : %s",
              strerror (errno)));
      return FALSE;
    }

    iret = vvas_xrt_write_bo (priv->Vcoff[chan_id].bo,
        priv->Vcoff[chan_id].user_ptr, priv->Vcoff[chan_id].size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (self,
          "failed to write vertical coefficients. reason : %s",
          strerror (errno));
      return FALSE;
    }
    iret = vvas_xrt_sync_bo (priv->Vcoff[chan_id].bo,
        VVAS_BO_SYNC_BO_TO_DEVICE, priv->Vcoff[chan_id].size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (self,
          "failed to sync vertical coefficients. reason : %s",
          strerror (errno));
      GST_ELEMENT_ERROR (self, RESOURCE, SYNC, NULL,
          ("failed to sync vertical coefficients to device. reason : %s",
              strerror (errno)));
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean
xlnx_abr_desc_syncBO (GstVvasXCompositor * self)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  int chan_id;
  int iret;
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {

    iret = vvas_xrt_sync_bo (priv->msPtr[chan_id].bo,
        VVAS_BO_SYNC_BO_TO_DEVICE, priv->msPtr[chan_id].size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (self,
          "failed to sync horizontal coefficients. reason : %s",
          strerror (errno));
      GST_ELEMENT_ERROR (self, RESOURCE, SYNC, NULL,
          ("failed to sync  horizontal coefficients to device. reason : %s",
              strerror (errno)));
      return FALSE;
    }

  }
  return TRUE;
}
#endif

static gboolean
vvas_xcompositor_prepare_input_buffer (GstVvasXCompositor * self,
    GstVvasXCompositorPad * sinkpad, GstBuffer ** inbuf)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  GstMemory *in_mem = NULL;
  GstVideoFrame in_vframe, own_vframe;
  guint64 phy_addr = -1;
  GstVideoMeta *vmeta = NULL;
  gboolean bret;
  GstBuffer *own_inbuf;
  GstFlowReturn fret;
  gboolean use_inpool = FALSE;
  guint pad_idx = sinkpad->index;

  memset (&in_vframe, 0x0, sizeof (GstVideoFrame));
  memset (&own_vframe, 0x0, sizeof (GstVideoFrame));

  in_mem = gst_buffer_get_memory (*inbuf, 0);
  if (!in_mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    goto error;
  }

  if (gst_is_vvas_memory (in_mem)
      && gst_vvas_memory_can_avoid_copy (in_mem, self->dev_index,
          self->in_mem_bank)) {
    phy_addr = gst_vvas_allocator_get_paddr (in_mem);
  } else if (gst_is_dmabuf_memory (in_mem)) {
    vvasBOHandle bo = NULL;
    gint dma_fd = -1;

    dma_fd = gst_dmabuf_memory_get_fd (in_mem);
    if (dma_fd < 0) {
      GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
      goto error;
    }

    /* dmabuf but not from xrt */
    bo = vvas_xrt_import_bo (self->priv->dev_handle, dma_fd);
    if (bo == NULL) {
      GST_WARNING_OBJECT (self,
          "failed to get XRT BO...fall back to copy input");
    }

    GST_DEBUG_OBJECT (self, "received dma fd %d and its xrt BO = %p", dma_fd,
        bo);
    phy_addr = vvas_xrt_get_bo_phy_addres (bo);

    if (bo != NULL)
      vvas_xrt_free_bo (bo);

  } else {
    use_inpool = TRUE;
  }

  gst_memory_unref (in_mem);
  in_mem = NULL;

  if (use_inpool) {
    if (sinkpad->validate_import) {
      if (!sinkpad->pool) {
        bret = vvas_xcompositor_allocate_internal_pool (self, sinkpad);
        if (!bret)
          goto error;
      }
      if (!gst_buffer_pool_is_active (sinkpad->pool))
        gst_buffer_pool_set_active (sinkpad->pool, TRUE);
      sinkpad->validate_import = FALSE;
    }

    if (self->enabled_pipeline) {
      own_inbuf = g_async_queue_try_pop (priv->copy_outqueue[sinkpad->index]);
      if (!own_inbuf && !priv->is_first_frame) {
        own_inbuf = g_async_queue_pop (priv->copy_outqueue[sinkpad->index]);
      }

      priv->is_first_frame[sinkpad->index] = FALSE;
      g_async_queue_push (priv->copy_inqueue[sinkpad->index], *inbuf);

      if (!own_inbuf) {
        GST_LOG_OBJECT (self, "copied input buffer is not available. return");
        *inbuf = NULL;
        return TRUE;
      }

      *inbuf = own_inbuf;
    } else {
      /* acquire buffer from own input pool */
      fret = gst_buffer_pool_acquire_buffer (sinkpad->pool, &own_inbuf, NULL);
      if (fret != GST_FLOW_OK) {
        GST_ERROR_OBJECT (sinkpad,
            "failed to allocate buffer from pool %p", sinkpad->pool);
        goto error;
      }
      GST_LOG_OBJECT (sinkpad, "acquired buffer %p from own pool", own_inbuf);

      /* map internal buffer in write mode */
      if (!gst_video_frame_map
          (&own_vframe, self->priv->in_vinfo[pad_idx], own_inbuf,
              GST_MAP_WRITE)) {
        GST_ERROR_OBJECT (self, "failed to map internal input buffer");
        goto error;
      }

      /* map input buffer in read mode */
      if (!gst_video_frame_map
          (&in_vframe, self->priv->in_vinfo[pad_idx], *inbuf, GST_MAP_READ)) {
        GST_ERROR_OBJECT (self, "failed to map input buffer");
        goto error;
      }
      gst_video_frame_copy (&own_vframe, &in_vframe);

      gst_video_frame_unmap (&in_vframe);
      gst_video_frame_unmap (&own_vframe);
      gst_buffer_copy_into (own_inbuf, *inbuf,
          (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS |
              GST_BUFFER_COPY_TIMESTAMPS), 0, -1);
      *inbuf = own_inbuf;
    }

  } else {
    gst_buffer_ref (*inbuf);
  }

  vmeta = gst_buffer_get_video_meta (*inbuf);
  if (vmeta == NULL) {
    GST_ERROR_OBJECT (self, "video meta not present in buffer");
    goto error;
  }

  in_mem = gst_buffer_get_memory (*inbuf, 0);
  if (!in_mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    goto error;
  }

  if (phy_addr == (uint64_t) - 1) {
    phy_addr = gst_vvas_allocator_get_paddr (in_mem);
  }
#ifdef XLNX_PCIe_PLATFORM
  /* syncs data when XLNX_SYNC_TO_DEVICE flag is enabled */
  bret = gst_vvas_memory_sync_bo (in_mem);
  if (!bret)
    goto error;
#endif

  gst_memory_unref (in_mem);

  GST_LOG_OBJECT (self, "input paddr %p", (void *) phy_addr);
  self->priv->phy_in_2[pad_idx] = 0;
  self->priv->phy_in_1[pad_idx] = 0;
  self->priv->phy_in_0[pad_idx] = phy_addr;
  if (vmeta->n_planes > 1)
    self->priv->phy_in_1[pad_idx] = phy_addr + vmeta->offset[1];
  if (vmeta->n_planes > 2)
    self->priv->phy_in_2[pad_idx] = phy_addr + vmeta->offset[2];

  self->priv->meta_in_stride[pad_idx] = *(vmeta->stride);

  return TRUE;

error:
  if (in_vframe.data)
    gst_video_frame_unmap (&in_vframe);
  if (own_vframe.data)
    gst_video_frame_unmap (&own_vframe);
  if (in_mem)
    gst_memory_unref (in_mem);

  return FALSE;
}

static gboolean
vvas_xcompositor_prepare_output_buffer (GstVvasXCompositor * self,
    GstBuffer * outbuf)
{
  GstMemory *mem = NULL;
  GstVideoMeta *vmeta;
  guint64 phy_addr = -1;


  self->priv->outbuf = outbuf;
  mem = gst_buffer_get_memory (outbuf, 0);
  if (mem == NULL) {
    GST_ERROR_OBJECT (self, "failed to get memory from output buffer");
    goto error;
  }
  /* No need to check whether memory is from device or not here.
   * Because, we have made sure memory is allocated from device in decide_allocation
   */
  if (gst_is_vvas_memory (mem)) {
    phy_addr = gst_vvas_allocator_get_paddr (mem);
  } else if (gst_is_dmabuf_memory (mem)) {
    vvasBOHandle bo = NULL;
    gint dma_fd = -1;

    dma_fd = gst_dmabuf_memory_get_fd (mem);
    if (dma_fd < 0) {
      GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
      goto error;
    }

    /* dmabuf but not from xrt */
    bo = vvas_xrt_import_bo (self->priv->dev_handle, dma_fd);
    if (bo == NULL) {
      GST_WARNING_OBJECT (self,
          "failed to get XRT BO...fall back to copy input");
    }
    GST_INFO_OBJECT (self, "received dma fd %d and its xrt BO = %p", dma_fd,
        bo);

    phy_addr = vvas_xrt_get_bo_phy_addres (bo);
    if (bo != NULL)
      vvas_xrt_free_bo (bo);
  }

  vmeta = gst_buffer_get_video_meta (outbuf);
  if (vmeta == NULL) {
    GST_ERROR_OBJECT (self, "video meta not present in buffer");
    goto error;
  }

  self->priv->phy_out_0 = phy_addr;
  self->priv->phy_out_1 = 0;
  self->priv->phy_out_2 = 0;
  if (vmeta->n_planes > 1)
    self->priv->phy_out_1 = phy_addr + vmeta->offset[1];
  if (vmeta->n_planes > 2)
    self->priv->phy_out_2 = phy_addr + vmeta->offset[2];

  gst_memory_unref (mem);

  return TRUE;

error:
  if (mem)
    gst_memory_unref (mem);

  return FALSE;
}

static uint32_t
xlnx_multiscaler_colorformat (uint32_t col)
{
  switch (col) {
    case GST_VIDEO_FORMAT_RGBx:
      return XV_MULTI_SCALER_RGBX8;
    case GST_VIDEO_FORMAT_YUY2:
      return XV_MULTI_SCALER_YUYV8;
    case GST_VIDEO_FORMAT_r210:
      return XV_MULTI_SCALER_RGBX10;
    case GST_VIDEO_FORMAT_Y410:
      return XV_MULTI_SCALER_YUVX10;
    case GST_VIDEO_FORMAT_NV16:
      return XV_MULTI_SCALER_Y_UV8;
    case GST_VIDEO_FORMAT_NV12:
      return XV_MULTI_SCALER_Y_UV8_420;
    case GST_VIDEO_FORMAT_RGB:
      return XV_MULTI_SCALER_RGB8;
    case GST_VIDEO_FORMAT_v308:
      return XV_MULTI_SCALER_YUV8;
    case GST_VIDEO_FORMAT_I422_10LE:
      return XV_MULTI_SCALER_Y_UV10;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      return XV_MULTI_SCALER_Y_UV10_420;
    case GST_VIDEO_FORMAT_GRAY8:
      return XV_MULTI_SCALER_Y8;
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      return XV_MULTI_SCALER_Y10;
    case GST_VIDEO_FORMAT_BGRx:
      return XV_MULTI_SCALER_BGRX8;
    case GST_VIDEO_FORMAT_UYVY:
      return XV_MULTI_SCALER_UYVY8;
    case GST_VIDEO_FORMAT_BGR:
      return XV_MULTI_SCALER_BGR8;
    case GST_VIDEO_FORMAT_BGRA:
      return XV_MULTI_SCALER_BGRA8;
    case GST_VIDEO_FORMAT_RGBA:
      return XV_MULTI_SCALER_RGBA8;
    default:
      GST_ERROR ("Not supporting %s yet",
          gst_video_format_to_string ((GstVideoFormat) col));
      return XV_MULTI_SCALER_NONE;
  }
}

static uint32_t
xlnx_multiscaler_stride_align (uint32_t stride_in, uint16_t AXIMMDataWidth)
{
  uint32_t stride;
  stride =
      (((stride_in) + AXIMMDataWidth - 1) / AXIMMDataWidth) * AXIMMDataWidth;
  return stride;
}

static void
xlnx_multiscaler_coff_fill (void *Hcoeff_BufAddr, void *Vcoeff_BufAddr,
    float scale)
{
  uint16_t *hpoly_coeffs, *vpoly_coeffs;        /* int need to check */
  int uy = 0;
  int temp_p;
  int temp_t;

  hpoly_coeffs = (uint16_t *) Hcoeff_BufAddr;
  vpoly_coeffs = (uint16_t *) Vcoeff_BufAddr;

  if ((scale >= 2) && (scale < 2.5)) {
    if (XPAR_V_MULTI_SCALER_0_TAPS == 6) {
      for (temp_p = 0; temp_p < 64; temp_p++)
        for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
          *(hpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps6_6C[temp_p][temp_t];
          *(vpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps6_6C[temp_p][temp_t];
          uy = uy + 1;
        }
    } else {
      for (temp_p = 0; temp_p < 64; temp_p++)
        for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
          *(hpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps8_12C[temp_p][temp_t];
          *(vpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps8_12C[temp_p][temp_t];
          uy = uy + 1;
        }
    }
  }

  if ((scale >= 2.5) && (scale < 3)) {
    if (XPAR_V_MULTI_SCALER_0_TAPS >= 10) {
      for (temp_p = 0; temp_p < 64; temp_p++)
        for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
          *(hpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps10_12C[temp_p][temp_t];
          *(vpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps10_12C[temp_p][temp_t];
          uy = uy + 1;
        }
    } else {
      if (XPAR_V_MULTI_SCALER_0_TAPS == 6) {
        for (temp_p = 0; temp_p < 64; temp_p++)
          for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
            *(hpoly_coeffs + uy) =
                XV_multiscaler_fixedcoeff_taps6_6C[temp_p][temp_t];
            *(vpoly_coeffs + uy) =
                XV_multiscaler_fixedcoeff_taps6_6C[temp_p][temp_t];
            uy = uy + 1;
          }
      } else {
        for (temp_p = 0; temp_p < 64; temp_p++)
          for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
            *(hpoly_coeffs + uy) =
                XV_multiscaler_fixedcoeff_taps8_8C[temp_p][temp_t];
            *(vpoly_coeffs + uy) =
                XV_multiscaler_fixedcoeff_taps8_8C[temp_p][temp_t];
            uy = uy + 1;
          }
      }
    }
  }

  if ((scale >= 3) && (scale < 3.5)) {
    if (XPAR_V_MULTI_SCALER_0_TAPS == 12) {
      for (temp_p = 0; temp_p < 64; temp_p++)
        for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
          *(hpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps12_12C[temp_p][temp_t];
          *(vpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps12_12C[temp_p][temp_t];
          uy = uy + 1;
        }
    } else {
      if (XPAR_V_MULTI_SCALER_0_TAPS == 6) {
        for (temp_p = 0; temp_p < 64; temp_p++)
          for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
            *(hpoly_coeffs + uy) =
                XV_multiscaler_fixedcoeff_taps6_6C[temp_p][temp_t];
            *(vpoly_coeffs + uy) =
                XV_multiscaler_fixedcoeff_taps6_6C[temp_p][temp_t];
            uy = uy + 1;
          }
      }
      if (XPAR_V_MULTI_SCALER_0_TAPS == 8) {
        for (temp_p = 0; temp_p < 64; temp_p++)
          for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
            *(hpoly_coeffs + uy) =
                XV_multiscaler_fixedcoeff_taps8_8C[temp_p][temp_t];
            *(vpoly_coeffs + uy) =
                XV_multiscaler_fixedcoeff_taps8_8C[temp_p][temp_t];
            uy = uy + 1;
          }
      }
      if (XPAR_V_MULTI_SCALER_0_TAPS == 10) {
        for (temp_p = 0; temp_p < 64; temp_p++)
          for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
            *(hpoly_coeffs + uy) =
                XV_multiscaler_fixedcoeff_taps10_10C[temp_p][temp_t];
            *(vpoly_coeffs + uy) =
                XV_multiscaler_fixedcoeff_taps10_10C[temp_p][temp_t];
            uy = uy + 1;
          }
      }
    }
  }

  if ((scale >= 3.5) || (scale < 2 && scale >= 1)) {
    if (XPAR_V_MULTI_SCALER_0_TAPS == 6) {
      for (temp_p = 0; temp_p < 64; temp_p++) {
        if (temp_p > 60) {
        }
        for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
          *(hpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps6_6C[temp_p][temp_t];
          *(vpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps6_6C[temp_p][temp_t];
          uy = uy + 1;
        }
      }
    }
    if (XPAR_V_MULTI_SCALER_0_TAPS == 8) {
      for (temp_p = 0; temp_p < 64; temp_p++)
        for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
          *(hpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps8_8C[temp_p][temp_t];
          *(vpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps8_8C[temp_p][temp_t];
          uy = uy + 1;
        }
    }
    if (XPAR_V_MULTI_SCALER_0_TAPS == 10) {
      for (temp_p = 0; temp_p < 64; temp_p++)
        for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
          *(hpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps10_10C[temp_p][temp_t];
          *(vpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps10_10C[temp_p][temp_t];
          uy = uy + 1;
        }
    }
    if (XPAR_V_MULTI_SCALER_0_TAPS == 12) {
      for (temp_p = 0; temp_p < 64; temp_p++)
        for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
          *(hpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps12_12C[temp_p][temp_t];
          *(vpoly_coeffs + uy) =
              XV_multiscaler_fixedcoeff_taps12_12C[temp_p][temp_t];
          uy = uy + 1;
        }
    }
  }

  if (scale < 1) {
    for (temp_p = 0; temp_p < 64; temp_p++)
      for (temp_t = 0; temp_t < XPAR_V_MULTI_SCALER_0_TAPS; temp_t++) {
        *(hpoly_coeffs + uy) =
            XV_multiscaler_fixedcoeff_taps6_6C[temp_p][temp_t];
        *(vpoly_coeffs + uy) =
            XV_multiscaler_fixedcoeff_taps6_6C[temp_p][temp_t];
        uy = uy + 1;
      }
  }

}

static int
log2_val (unsigned int val)
{
  int cnt = 0;
  while (val > 1) {
    val = val >> 1;
    cnt++;
  }
  return cnt;
}

static gboolean
feasibilityCheck (int src, int dst, int *filterSize)
{
  int sizeFactor = 4;
  int xInc = (((int64_t) src << 16) + (dst >> 1)) / dst;
  if (xInc <= 1 << 16)
    *filterSize = 1 + sizeFactor;       // upscale
  else
    *filterSize = 1 + (sizeFactor * src + dst - 1) / dst;

  if (*filterSize > MAX_FILTER_SIZE) {
    GST_ERROR
        ("FilterSize %d for %d to %d is greater than maximum taps(%d)",
        *filterSize, src, dst, MAX_FILTER_SIZE);
    return FALSE;
  }

  return TRUE;
}

void
copy_filt_set (int16_t dest_filt[64][12], int set)
{
  int i = 0, j = 0;

  for (i = 0; i < 64; i++) {
    for (j = 0; j < 12; j++) {
      switch (set) {
        case XLXN_FIXED_COEFF_SR13:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR13_0[i][j];    //<1.5SR
          break;
        case XLXN_FIXED_COEFF_SR15:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR15_0[i][j];    //1.5SR
          break;
        case XLXN_FIXED_COEFF_SR2:
          dest_filt[i][j] = XV_multiscaler_fixedcoeff_taps8_12C[i][j];  //2SR //8tap
          break;
        case XLXN_FIXED_COEFF_SR25:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR25_0[i][j];    //2.5SR
          break;
        case XLXN_FIXED_COEFF_TAPS_10:
          dest_filt[i][j] = XV_multiscaler_fixedcoeff_taps10_12C[i][j]; //10tap
          break;
        case XLXN_FIXED_COEFF_TAPS_12:
          dest_filt[i][j] = XV_multiscaler_fixedcoeff_taps12_12C[i][j]; //12tap
          break;
        case XLXN_FIXED_COEFF_TAPS_6:
          dest_filt[i][j] = XV_multiscaler_fixedcoeff_taps6_12C[i][j];  //6tap: Always used for up scale
          break;
        default:
          dest_filt[i][j] = XV_multiscaler_fixedcoeff_taps12_12C[i][j]; //12tap
          break;
      }
    }
  }
}

void
Generate_cardinal_cubic_spline (int src, int dst, int filterSize, int64_t B,
    int64_t C, int16_t * CCS_filtCoeff)
{
#ifdef COEFF_DUMP
  FILE *fp;
  char fname[512];
  sprintf (fname, "coeff_%dTO%d.csv", src, dst);
  fp = fopen (fname, "w");
  /*FILE *fph;
     sprintf(fname,"phase_%dTO%d_2Inc.txt",src,dst);
     fph=fopen(fname,"w"); */
  //fprintf(fp,"src:%d => dst:%d\n\n",src,dst);
#endif
  int one = (1 << 14);
  int64_t *coeffFilter = NULL;
  int64_t *coeffFilter_reduced = NULL;
  int16_t *outFilter = NULL;
  int16_t *coeffFilter_normalized = NULL;
  int lumXInc = (((int64_t) src << 16) + (dst >> 1)) / dst;
  int srt = src / dst;
  int lval = log2_val (srt);
  int th0 = 8;
  int lv0 = MIN (lval, th0);
  const int64_t fone = (int64_t) 1 << (54 - lv0);
  int64_t thr1 = ((int64_t) 1 << 31);
  int64_t thr2 = ((int64_t) 1 << 54) / fone;
  int i, xInc, outFilterSize;
  int num_phases = 64;
  int phase_set[64] = { 0 };
  int64_t xDstInSrc;
  int xx, j, p, t;
  int64_t d = 0, coeff = 0, dd = 0, ddd = 0;
  int phase_cnt = 0;
  int64_t error = 0, sum = 0, v = 0;
  int intV = 0;
  int fstart_Idx = 0, fend_Idx = 0, half_Idx = 0, middleIdx = 0;
  unsigned int PhaseH = 0, offset = 0, WriteLoc = 0, WriteLocNext =
      0, ReadLoc = 0, OutputWrite_En = 0;
  int OutPixels = dst;
  int PixelRate =
      (int) ((float) ((src * STEP_PRECISION) + (dst / 2)) / (float) dst);
  int ph_max_sum = 1 << MAX_FILTER_SIZE;
  int sumVal = 0, maxIdx = 0, maxVal = 0, diffVal = 0;

  xInc = lumXInc;
  filterSize = MAX (filterSize, 1);
  coeffFilter = (int64_t *) calloc (num_phases * filterSize, sizeof (int64_t));
  xDstInSrc = xInc - (1 << 16);

  // coefficient generation based on scaler IP
  for (i = 0; i < src; i++) {
    PhaseH =
        ((offset >> (STEP_PRECISION_SHIFT - NR_PHASE_BITS))) & (NR_PHASES - 1);
    WriteLoc = WriteLocNext;

    if ((offset >> STEP_PRECISION_SHIFT) != 0) {
      // Take a new sample from input, but don't process anything
      ReadLoc++;
      offset = offset - (1 << STEP_PRECISION_SHIFT);
      OutputWrite_En = 0;
      WriteLocNext = WriteLoc;
    }

    if (((offset >> STEP_PRECISION_SHIFT) == 0) && (WriteLoc < OutPixels)) {
      // Produce a new output sample
      offset += PixelRate;
      OutputWrite_En = 1;
      WriteLocNext = WriteLoc + 1;
    }

    if (OutputWrite_En) {
      xDstInSrc = ReadLoc * (1 << 17) + PhaseH * (1 << 11);
      xx = ReadLoc - (filterSize - 2) / 2;

      d = (ABS (((int64_t) xx * (1 << 17)) - xDstInSrc)) << 13;

      //count number of phases used for this SR
      if (phase_set[PhaseH] == 0)
        phase_cnt += 1;

      //Filter coeff generation
      for (j = 0; j < filterSize; j++) {
        d = (ABS (((int64_t) xx * (1 << 17)) - xDstInSrc)) << 13;
        if (xInc > 1 << 16) {
          d = (int64_t) (d * dst / src);
        }

        if (d >= thr1) {
          coeff = 0.0;
        } else {
          dd = (int64_t) (d * d) >> 30;
          ddd = (int64_t) (dd * d) >> 30;
          if (d < 1 << 30) {
            coeff = (12 * (1 << 24) - 9 * B - 6 * C) * ddd +
                (-18 * (1 << 24) + 12 * B + 6 * C) * dd +
                (6 * (1 << 24) - 2 * B) * (1 << 30);
          } else {
            coeff = (-B - 6 * C) * ddd +
                (6 * B + 30 * C) * dd +
                (-12 * B - 48 * C) * d + (8 * B + 24 * C) * (1 << 30);
          }
        }

        coeff = coeff / thr2;
        coeffFilter[PhaseH * filterSize + j] = coeff;
        xx++;
      }
      if (phase_set[PhaseH] == 0) {
        phase_set[PhaseH] = 1;
      }
    }
  }

  coeffFilter_reduced =
      (int64_t *) calloc ((num_phases * filterSize), sizeof (int64_t));
  memcpy (coeffFilter_reduced, coeffFilter,
      sizeof (int64_t) * num_phases * filterSize);
  outFilterSize = filterSize;
  outFilter =
      (int16_t *) calloc ((num_phases * outFilterSize), sizeof (int16_t));
  coeffFilter_normalized =
      (int16_t *) calloc ((num_phases * outFilterSize), sizeof (int16_t));

  /* normalize & store in outFilter */
  for (i = 0; i < num_phases; i++) {
    error = 0;
    sum = 0;

    for (j = 0; j < filterSize; j++) {
      sum += coeffFilter_reduced[i * filterSize + j];
    }
    sum = (sum + one / 2) / one;
    if (!sum) {
      sum = 1;
    }
    for (j = 0; j < outFilterSize; j++) {
      v = coeffFilter_reduced[i * filterSize + j] + error;
      intV = ROUNDED_DIV (v, sum);
      coeffFilter_normalized[i * (outFilterSize) + j] = intV;
      coeffFilter_normalized[i * (outFilterSize) + j] = coeffFilter_normalized[i * (outFilterSize) + j] >> 2;   //added to negate double increment and match our precision
      error = v - intV * sum;
    }
  }

  for (p = 0; p < num_phases; p++) {
    for (t = 0; t < filterSize; t++) {
      outFilter[p * filterSize + t] =
          coeffFilter_normalized[p * filterSize + t];
    }
  }

  /*incorporate filter less than 12 tap into a 12 tap */
  fstart_Idx = 0, fend_Idx = 0, half_Idx = 0;
  middleIdx = (MAX_FILTER_SIZE / 2);    //center location for 12 tap
  half_Idx = (outFilterSize / 2);
  if ((outFilterSize - (half_Idx << 1)) == 0) { //evenOdd
    fstart_Idx = middleIdx - half_Idx;
    fend_Idx = middleIdx + half_Idx;
  } else {
    fstart_Idx = middleIdx - (half_Idx);
    fend_Idx = middleIdx + half_Idx + 1;
  }

  for (i = 0; i < num_phases; i++) {
    for (j = 0; j < MAX_FILTER_SIZE; j++) {

      CCS_filtCoeff[i * MAX_FILTER_SIZE + j] = 0;
      if ((j >= fstart_Idx) && (j < fend_Idx))
        CCS_filtCoeff[i * MAX_FILTER_SIZE + j] =
            outFilter[i * (outFilterSize) + (j - fstart_Idx)];
    }
  }

  /*Make sure filterCoeffs within a phase sum to 4096 */
  for (i = 0; i < num_phases; i++) {
    sumVal = 0;
    maxVal = 0;
    for (j = 0; j < MAX_FILTER_SIZE; j++) {
      sumVal += CCS_filtCoeff[i * MAX_FILTER_SIZE + j];
      if (CCS_filtCoeff[i * MAX_FILTER_SIZE + j] > maxVal) {
        maxVal = CCS_filtCoeff[i * MAX_FILTER_SIZE + j];
        maxIdx = j;
      }
    }
    diffVal = ph_max_sum - sumVal;
    if (diffVal > 0)
      CCS_filtCoeff[i * MAX_FILTER_SIZE + maxIdx] =
          CCS_filtCoeff[i * MAX_FILTER_SIZE + maxIdx] + diffVal;
  }


#ifdef COEFF_DUMP
  fprintf (fp, "taps/phases, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12\n");
  for (i = 0; i < num_phases; i++) {
    fprintf (fp, "%d, ", i + 1);
    for (j = 0; j < MAX_FILTER_SIZE; j++) {
      fprintf (fp, "%d,  ", CCS_filtCoeff[i * MAX_FILTER_SIZE + j]);
    }
    fprintf (fp, "\n");
  }
#endif

  free (coeffFilter);
  free (coeffFilter_reduced);
  free (outFilter);
  free (coeffFilter_normalized);
#ifdef COEFF_DUMP
  fclose (fp);
#endif
}

static gboolean
vvas_xcompositor_prepare_coefficients_with_12tap (GstVvasXCompositor * self,
    guint chan_id)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  GstVvasXCompositorPad *srcpad = NULL;
  GstCaps *out_caps;
  GstVideoInfo out_vinfo;
  gint filter_size;
  guint in_width, in_height, out_width, out_height;
  int64_t B = 0 * (1 << 24);
  int64_t C = 0.6 * (1 << 24);
  float scale_ratio[2] = { 0, 0 };
  int upscale_enable[2] = { 0, 0 };
  int filterSet[2] = { 0, 0 };
  guint d;
  gboolean bret;
  srcpad = GST_VVAS_XCOMPOSITOR_PAD_CAST (self->srcpad);
  if (!srcpad) {
    GST_ERROR_OBJECT (self, "failed to get srcpad");
    return FALSE;
  }

  out_caps = gst_pad_get_current_caps ((GstPad *) srcpad);
  if (!out_caps) {
    GST_ERROR_OBJECT (self, "failed to get output caps ");
    return FALSE;
  }

  bret = gst_video_info_from_caps (&out_vinfo, out_caps);
  if (!bret) {
    GST_ERROR_OBJECT (self, "failed to get videoinfo from output caps ");
    gst_caps_unref (out_caps);
    return FALSE;
  }

  in_width = GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo[chan_id]);
  in_height = GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo[chan_id]);
  out_width = GST_VIDEO_INFO_WIDTH (&out_vinfo);
  out_height = GST_VIDEO_INFO_HEIGHT (&out_vinfo);

  /* store width scaling ratio  */
  if (in_width >= out_width) {
    scale_ratio[0] = (float) in_width / (float) out_width;      //downscale
  } else {
    scale_ratio[0] = (float) out_width / (float) in_width;      //upscale
    upscale_enable[0] = 1;
  }

  /* store height scaling ratio */
  if (in_height >= out_height) {
    scale_ratio[1] = (float) in_height / (float) out_height;    //downscale
  } else {
    scale_ratio[1] = (float) out_height / (float) in_height;    //upscale
    upscale_enable[1] = 1;
  }

  for (d = 0; d < 2; d++) {
    if (upscale_enable[d] == 1) {
      /* upscaling default use 6 taps */
      filterSet[d] = XLXN_FIXED_COEFF_TAPS_6;
    } else {
      /* Get index of downscale fixed filter */
      if (scale_ratio[d] < 1.5)
        filterSet[d] = XLXN_FIXED_COEFF_SR13;
      else if ((scale_ratio[d] >= 1.5) && (scale_ratio[d] < 2))
        filterSet[d] = XLXN_FIXED_COEFF_SR15;
      else if ((scale_ratio[d] >= 2) && (scale_ratio[d] < 2.5))
        filterSet[d] = XLXN_FIXED_COEFF_SR2;
      else if ((scale_ratio[d] >= 2.5) && (scale_ratio[d] < 3))
        filterSet[d] = XLXN_FIXED_COEFF_SR25;
      else if ((scale_ratio[d] >= 3) && (scale_ratio[d] < 3.5))
        filterSet[d] = XLXN_FIXED_COEFF_TAPS_10;
      else
        filterSet[d] = XLXN_FIXED_COEFF_TAPS_12;
    }
    GST_INFO_OBJECT (self,
        "%s scaling ratio = %f and chosen filter type = %d",
        d == 0 ? "width" : "height", scale_ratio[d], filterSet[d]);
  }

  if (self->coef_load_type == COEF_AUTO_GENERATE) {
    /* prepare horizontal coefficients */
    bret = feasibilityCheck (in_width, out_width, &filter_size);
    if (bret && !upscale_enable[0]) {
      GST_INFO_OBJECT (self,
          "Generate cardinal cubic horizontal coefficients "
          "with filter size %d", filter_size);
      Generate_cardinal_cubic_spline (in_width, out_width, filter_size, B,
          C, (int16_t *) priv->Hcoff[chan_id].user_ptr);
    } else {
      /* get fixed horizontal filters */
      GST_INFO_OBJECT (self,
          "Consider predefined horizontal filter coefficients");
      copy_filt_set (priv->Hcoff[chan_id].user_ptr, filterSet[0]);
    }

    /* prepare vertical coefficients */
    bret = feasibilityCheck (in_height, out_height, &filter_size);
    if (bret && !upscale_enable[1]) {
      GST_INFO_OBJECT (self,
          "Generate cardinal cubic vertical coefficients "
          "with filter size %d", filter_size);
      Generate_cardinal_cubic_spline (in_height, out_height, filter_size,
          B, C, (int16_t *) priv->Vcoff[chan_id].user_ptr);
    } else {
      /* get fixed vertical filters */
      GST_INFO_OBJECT (self,
          "Consider predefined vertical filter coefficients");
      copy_filt_set (priv->Vcoff[chan_id].user_ptr, filterSet[1]);
    }
  } else if (self->coef_load_type == COEF_FIXED) {
    /* get fixed horizontal filters */
    GST_INFO_OBJECT (self,
        "Consider predefined horizontal filter coefficients");
    copy_filt_set (priv->Hcoff[chan_id].user_ptr, filterSet[0]);

    /* get fixed vertical filters */
    GST_INFO_OBJECT (self, "Consider predefined vertical filter coefficients");
    copy_filt_set (priv->Vcoff[chan_id].user_ptr, filterSet[1]);
  }
  gst_caps_unref (out_caps);
  return TRUE;
}

static bool
xlnx_multiscaler_descriptor_create (GstVvasXCompositor * self)
{
  GstVideoMeta *meta_out;
  MULTI_SCALER_DESC_STRUCT *msPtr;
  guint chan_id;
  guint quad_in_each_row_column = (guint) ceil (sqrt (self->num_request_pads));
  gfloat in_width_scale_factor = 1, in_height_scale_factor = 1;
  GstVvasXCompositorPrivate *priv = self->priv;
  guint quadrant_height, quadrant_width;
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    /* setting input output values based on zoder for that pad */
    GstVvasXCompositorPad *pad;
    uint64_t phy_in_0 = priv->phy_in_0[self->priv->pad_of_zorder[chan_id]];
    uint64_t phy_in_1 = priv->phy_in_1[self->priv->pad_of_zorder[chan_id]];
    uint64_t phy_in_2 = priv->phy_in_2[self->priv->pad_of_zorder[chan_id]];
    uint32_t in_width =
        priv->in_vinfo[self->priv->pad_of_zorder[chan_id]]->width;
    uint32_t in_height =
        priv->in_vinfo[self->priv->pad_of_zorder[chan_id]]->height;
    uint32_t xpos_offset = 0;
    uint32_t ypos_offset = 0;

    uint32_t msc_inPixelFmt =
        xlnx_multiscaler_colorformat (priv->in_vinfo[self->
            priv->pad_of_zorder[chan_id]]->finfo->format);
    uint32_t stride =
        self->priv->meta_in_stride[self->priv->pad_of_zorder[chan_id]];
#ifdef ENABLE_PPE_SUPPORT
    uint32_t val;
#endif
    if (stride != xlnx_multiscaler_stride_align (stride, WIDTH_ALIGN)) {
      GST_WARNING_OBJECT (self, "input stide alignment mismatch");
      GST_WARNING_OBJECT (self, "required  stride = %d in_stride = %d",
          xlnx_multiscaler_stride_align (stride, WIDTH_ALIGN), stride);
    }
    pad =
        gst_vvas_xcompositor_sinkpad_at_index (self,
        self->priv->pad_of_zorder[chan_id]);
    msPtr = (MULTI_SCALER_DESC_STRUCT *) (priv->msPtr[chan_id].user_ptr);

    msPtr->msc_widthIn = in_width;
    msPtr->msc_heightIn = in_height;
    msPtr->msc_inPixelFmt = msc_inPixelFmt;
    msPtr->msc_strideIn = stride;
    meta_out = gst_buffer_get_video_meta (priv->outbuf);
    quadrant_width = (int) (meta_out->width / quad_in_each_row_column);
    quadrant_height = (int) (meta_out->height / quad_in_each_row_column);

    if (self->best_fit) {
      msPtr->msc_widthOut = quadrant_width;
      msPtr->msc_heightOut = quadrant_height;
    } else {
      if (pad->width != -1) {
        msPtr->msc_widthOut = pad->width;
        if (pad->width < -1 || pad->width > meta_out->width || pad->width == 0) {
          GST_ERROR_OBJECT (self, "width of sink_%d is invalid",
              self->priv->pad_of_zorder[chan_id]);
          return false;
        }
      } else
        msPtr->msc_widthOut = in_width;
      if (pad->height != -1) {
        msPtr->msc_heightOut = pad->height;
        if (pad->height < -1 || pad->height > meta_out->height
            || pad->height == 0) {
          GST_ERROR_OBJECT (self, "height of sink_%d is invalid",
              self->priv->pad_of_zorder[chan_id]);
          return false;
        }
      } else
        msPtr->msc_heightOut = in_height;
      xpos_offset = pad->xpos;
      ypos_offset = pad->ypos;
      if (xpos_offset < 0 || xpos_offset > meta_out->width) {
        GST_ERROR_OBJECT (self, "xpos of sink_%d is invalid",
            self->priv->pad_of_zorder[chan_id]);
        return false;
      }
      if (ypos_offset < 0 || ypos_offset > meta_out->height) {
        GST_ERROR_OBJECT (self, "ypos of sink_%d is invalid",
            self->priv->pad_of_zorder[chan_id]);
        return false;
      }
      /* Aligning xpos and ypos as per IP requirement before calculating cropping params */
      xpos_offset = (xpos_offset / (8 * self->ppc)) * (8 * self->ppc);
      ypos_offset = (ypos_offset / 2) * 2;
      /* cropping the image  at right corner if xpos exceeds output width */
      if (xpos_offset + msPtr->msc_widthOut > meta_out->width) {
        in_width_scale_factor = ((float) in_width / msPtr->msc_widthOut);
        msPtr->msc_widthIn =
            (int) ((meta_out->width - xpos_offset) * in_width_scale_factor);
        msPtr->msc_widthOut = meta_out->width - xpos_offset;
      }
      /* cropping the image  at bottom corner if xpos exceeds output width */
      if (ypos_offset + msPtr->msc_heightOut > meta_out->height) {
        in_height_scale_factor = ((float) in_height / msPtr->msc_heightOut);
        msPtr->msc_heightIn =
            (int) ((meta_out->height - ypos_offset) * in_height_scale_factor);
        msPtr->msc_heightOut = meta_out->height - ypos_offset;
      }

    }
    /* Align values as per the IP requirement */
    msPtr->msc_widthIn = (msPtr->msc_widthIn / self->ppc) * self->ppc;
    msPtr->msc_heightIn = (msPtr->msc_heightIn / 2) * 2;
    msPtr->msc_widthOut =
        xlnx_multiscaler_stride_align (msPtr->msc_widthOut, self->ppc);
    msPtr->msc_heightOut =
        xlnx_multiscaler_stride_align (msPtr->msc_heightOut, 2);

    GST_INFO_OBJECT (self,
        "Height scale factor %f and Width scale factor %f ",
        in_height_scale_factor, in_width_scale_factor);
    GST_INFO_OBJECT (self, "Input height %d and width %d ", msPtr->msc_heightIn,
        msPtr->msc_widthIn);
    GST_INFO_OBJECT (self,
        "Aligned params are for sink_%d xpos %d ypos %d out_width %d out_height %d ",
        self->priv->pad_of_zorder[chan_id], xpos_offset, ypos_offset,
        msPtr->msc_widthOut, msPtr->msc_heightOut);

#ifdef ENABLE_PPE_SUPPORT
    msPtr->msc_alpha_0 = self->priv->alpha_r;
    msPtr->msc_alpha_1 = self->priv->alpha_g;
    msPtr->msc_alpha_2 = self->priv->alpha_b;
    val = (self->priv->beta_r * (1 << 16));
    msPtr->msc_beta_0 = val;
    val = (self->priv->beta_g * (1 << 16));
    msPtr->msc_beta_1 = val;
    val = (self->priv->beta_b * (1 << 16));
    msPtr->msc_beta_2 = val;
#endif

    msPtr->msc_lineRate =
        (uint32_t) ((float) ((msPtr->msc_heightIn * STEP_PRECISION) +
            ((msPtr->msc_heightOut) / 2)) / (float) msPtr->msc_heightOut);
    msPtr->msc_pixelRate = (uint32_t) ((float)
        (((msPtr->msc_widthIn) * STEP_PRECISION) +
            ((msPtr->msc_widthOut) / 2)) / (float) msPtr->msc_widthOut);
    msPtr->msc_outPixelFmt = xlnx_multiscaler_colorformat (meta_out->format);

    msPtr->msc_strideOut = (*(meta_out->stride));

    if (msPtr->msc_strideOut !=
        xlnx_multiscaler_stride_align (*(meta_out->stride), WIDTH_ALIGN)) {
      GST_WARNING_OBJECT (self, "output stide alignment mismatch");
      GST_WARNING_OBJECT (self, "required  stride = %d out_stride = %d",
          xlnx_multiscaler_stride_align (*
              (meta_out->stride), WIDTH_ALIGN), msPtr->msc_strideOut);
    }

    msPtr->msc_srcImgBuf0 = (uint64_t) phy_in_0;        /* plane 0 */
    msPtr->msc_srcImgBuf1 = (uint64_t) phy_in_1;        /* plane 1 */
    msPtr->msc_srcImgBuf2 = (uint64_t) phy_in_2;        /* plane 2 */
    if (self->best_fit) {
      msPtr->msc_dstImgBuf0 =
          (uint64_t) priv->phy_out_0 +
          quadrant_width * (chan_id % quad_in_each_row_column)
          + (msPtr->msc_strideOut * quadrant_height) * (chan_id / quad_in_each_row_column);     /* plane 0 */
      msPtr->msc_dstImgBuf1 =
          (uint64_t) priv->phy_out_1 +
          quadrant_width * (chan_id % quad_in_each_row_column)
          + (msPtr->msc_strideOut * quadrant_height / 2) * (chan_id / quad_in_each_row_column); /* plane 1 */

      msPtr->msc_dstImgBuf2 =
          (uint64_t) priv->phy_out_2 +
          quadrant_width * (chan_id % quad_in_each_row_column)
          + (msPtr->msc_strideOut * quadrant_height / 2) * (chan_id / quad_in_each_row_column); /* plane 2 */
    } else {
      msPtr->msc_dstImgBuf0 = (uint64_t) priv->phy_out_0 + xpos_offset + ypos_offset * msPtr->msc_strideOut;    /* plane 0 */

      msPtr->msc_dstImgBuf1 = (uint64_t) priv->phy_out_1 + xpos_offset + ypos_offset * msPtr->msc_strideOut / 2;        /* plane 1 */

      msPtr->msc_dstImgBuf2 = (uint64_t) priv->phy_out_2 + xpos_offset + ypos_offset * msPtr->msc_strideOut / 2;        /* plane 2 */

    }

    msPtr->msc_blkmm_hfltCoeff = 0;
    msPtr->msc_blkmm_vfltCoeff = 0;
    if (chan_id == (self->num_request_pads - 1))
      msPtr->msc_nxtaddr = 0;
    else
      msPtr->msc_nxtaddr = priv->msPtr[chan_id + 1].phy_addr;
    msPtr->msc_blkmm_hfltCoeff = priv->Hcoff[chan_id].phy_addr;
    msPtr->msc_blkmm_vfltCoeff = priv->Vcoff[chan_id].phy_addr;

  }
  priv->is_coeff = false;
  return TRUE;
}

static int32_t
vvas_xcompositor_exec_buf (vvasDeviceHandle dev_handle,
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

static gboolean
vvas_xcompositor_process (GstVvasXCompositor * self)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  int iret;
  uint64_t desc_addr = 0;
  bool ret;
  GstMemory *mem = NULL;
  unsigned int ms_status = 0;
  int retry_count = MAX_EXEC_WAIT_RETRY_CNT;
  ret = xlnx_multiscaler_descriptor_create (self);
  if (!ret)
    return FALSE;
#ifdef XLNX_PCIe_PLATFORM
  ret = xlnx_abr_desc_syncBO (self);
  if (!ret)
    return FALSE;
#endif

  desc_addr = self->priv->msPtr[0].phy_addr;
  iret = vvas_xcompositor_exec_buf (priv->dev_handle, priv->kern_handle,
      &priv->run_handle,
      "ulppu", self->num_request_pads, desc_addr, NULL, NULL, ms_status);

  if (iret) {
    GST_ERROR_OBJECT (self, "failed to execute command %d", iret);
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, NULL,
        ("failed to issue execute command. reason : %s", strerror (errno)));
    return FALSE;
  }

  do {
    iret = vvas_xrt_exec_wait (priv->dev_handle, priv->run_handle,
        MULTI_SCALER_TIMEOUT);
    /* Lets try for MAX count unless there is a error or completion */
    if (iret == ERT_CMD_STATE_TIMEOUT) {
      GST_WARNING_OBJECT (self, "Timeout...retry execwait");
      if (retry_count-- <= 0) {
        GST_ERROR_OBJECT (self,
            "Max retry count %d reached..returning error",
            MAX_EXEC_WAIT_RETRY_CNT);
        vvas_xrt_free_run_handle (priv->run_handle);
        return FALSE;
      }
    } else if (iret == ERT_CMD_STATE_ERROR) {
      GST_ERROR_OBJECT (self, "ExecWait ret = %d", iret);
      vvas_xrt_free_run_handle (priv->run_handle);

      return FALSE;
    }
  } while (iret != ERT_CMD_STATE_COMPLETED);
  vvas_xrt_free_run_handle (priv->run_handle);
  mem = gst_buffer_get_memory (priv->outbuf, 0);
  if (mem == NULL) {
    GST_ERROR_OBJECT (self,
        "chan-%d : failed to get memory from output buffer", 0);
    return FALSE;
  }
#ifdef XLNX_PCIe_PLATFORM
  gst_vvas_memory_set_sync_flag (mem, VVAS_SYNC_FROM_DEVICE);
#endif
  gst_memory_unref (mem);

  return TRUE;
}

static void
vvas_xcompositor_get_empty_buffer (GstVvasXCompositor * self,
    GstVvasXCompositorPad * sinkpad, GstBuffer ** inbuf)
{
  GstFlowReturn fret;
  gboolean bret;
  GstBuffer *ownbuf;

  if (!sinkpad->pool) {
    bret = vvas_xcompositor_allocate_internal_pool (self, sinkpad);
    if (!bret)
      return;
  }
  if (!gst_buffer_pool_is_active (sinkpad->pool))
    gst_buffer_pool_set_active (sinkpad->pool, TRUE);

  /* acquire buffer from own input pool */
  fret = gst_buffer_pool_acquire_buffer (sinkpad->pool, &ownbuf, NULL);
  if (fret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
        sinkpad->pool);
    return;
  }
  GST_LOG_OBJECT (self, "acquired buffer %p from own pool", ownbuf);
  *inbuf = ownbuf;
  gst_buffer_unref (*inbuf);
}

static GstFlowReturn
gst_vvas_xcompositor_create_output_buffer (GstVideoAggregator * videoaggregator,
    GstBuffer ** outbuf)
{
  GstAggregator *aggregator = GST_AGGREGATOR (videoaggregator);
  GstBufferPool *pool;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (videoaggregator);

  pool = gst_aggregator_get_buffer_pool (aggregator);

  if (pool && !self->priv->need_copy) {
    if (!gst_buffer_pool_is_active (pool)) {
      if (!gst_buffer_pool_set_active (pool, TRUE)) {
        GST_ELEMENT_ERROR (videoaggregator, RESOURCE, SETTINGS,
            ("failed to activate bufferpool"),
            ("failed to activate bufferpool"));
        return GST_FLOW_ERROR;
      }
    }

    ret = gst_buffer_pool_acquire_buffer (pool, outbuf, NULL);
    gst_object_unref (pool);
  } else {                      /* going to allocate the sw buffers of size out_vinfo which are not padded */
    if (pool)
      gst_object_unref (pool);
    *outbuf =
        gst_buffer_new_and_alloc (GST_VIDEO_INFO_SIZE (&videoaggregator->info));


    if (*outbuf == NULL) {
      GST_ELEMENT_ERROR (self, RESOURCE, NO_SPACE_LEFT,
          (NULL), ("Could not acquire buffer of size: %d",
              (gint) GST_VIDEO_INFO_SIZE (&videoaggregator->info)));
      ret = GST_FLOW_ERROR;
    }
  }
  return ret;
}

static GstFlowReturn
gst_vvas_xcompositor_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuf)
{
  GList *list;
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (vagg);

  GstFlowReturn fret = GST_FLOW_OK;
  gboolean bret = FALSE;
  guint pad_id = 0;
  for (list = self->sinkpads; list; list = list->next) {
    GstVideoAggregatorPad *pad = list->data;
    GstVvasXCompositorPad *sinkpad = (GstVvasXCompositorPad *) pad;
    GstVideoFrame *prepared_frame;
    if (!sinkpad->is_eos)
      if (gst_aggregator_pad_is_eos ((GstAggregatorPad *) pad)) {
        vvas_xcompositor_adjust_zorder_after_eos (self, sinkpad->index);
        sinkpad->is_eos = true;
      }
    prepared_frame = gst_video_aggregator_pad_get_prepared_frame (pad);

    if (!prepared_frame) {
      /*to pass and empty buffer in case we get eos on some of the pads */
      vvas_xcompositor_get_empty_buffer (self, sinkpad,
          &self->priv->inbufs[pad_id]);
    } else {
      self->priv->inbufs[pad_id] = prepared_frame->buffer;
    }

    bret =
        vvas_xcompositor_prepare_input_buffer (self, sinkpad,
        &self->priv->inbufs[pad_id]);
    if (!bret)
      goto error;

    pad_id++;
  }
  if (self->priv->need_copy && self->priv->output_pool) {
    GstBuffer *aligned_buffer = NULL;
    if (!gst_buffer_pool_is_active (self->priv->output_pool)) {
      if (!gst_buffer_pool_set_active (self->priv->output_pool, TRUE)) {
        GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
            ("failed to activate bufferpool"),
            ("failed to activate bufferpool"));
        return GST_FLOW_ERROR;
      }
    }

    fret =
        gst_buffer_pool_acquire_buffer (self->priv->output_pool,
        &aligned_buffer, NULL);
    gst_buffer_copy_into (aligned_buffer, outbuf,
        GST_BUFFER_COPY_METADATA | GST_BUFFER_COPY_TIMESTAMPS |
        GST_BUFFER_COPY_FLAGS, 0, -1);
    bret = vvas_xcompositor_prepare_output_buffer (self, aligned_buffer);
  } else {
    bret = vvas_xcompositor_prepare_output_buffer (self, outbuf);
  }
  if (!bret)
    goto error;

  bret = vvas_xcompositor_process (self);
  if (!bret)
    goto error;
  if (self->priv->need_copy) {
    GstVideoFrame new_frame, out_frame;
    /* aligned buffer pointer is assigned to self->priv->outbuf so using for a slow copy */
    gst_video_frame_map (&out_frame, self->priv->out_vinfo, self->priv->outbuf,
        GST_MAP_READ);
    gst_video_frame_map (&new_frame, self->priv->out_vinfo, outbuf,
        GST_MAP_WRITE);
    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, self,
        "slow copy data from %p to %p", self->priv->outbuf, outbuf);
    gst_video_frame_copy (&new_frame, &out_frame);
    gst_video_frame_unmap (&out_frame);
    gst_video_frame_unmap (&new_frame);

    gst_buffer_copy_into (outbuf, self->priv->outbuf,
        GST_BUFFER_COPY_METADATA | GST_BUFFER_COPY_TIMESTAMPS |
        GST_BUFFER_COPY_FLAGS, 0, -1);
    if (self->priv->outbuf)
      gst_buffer_unref (self->priv->outbuf);
  }
  GST_LOG_OBJECT (self,
      "pushing buffer %p with pts = %" GST_TIME_FORMAT " dts = %"
      GST_TIME_FORMAT " duration = %" GST_TIME_FORMAT "size is %ld", outbuf,
      GST_TIME_ARGS (GST_BUFFER_PTS (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DTS (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
      gst_buffer_get_size (outbuf));
  for (pad_id = 0; pad_id < self->num_request_pads; pad_id++)
    gst_buffer_unref (self->priv->inbufs[pad_id]);
  return GST_FLOW_OK;

error:
  fret = GST_FLOW_ERROR;
  for (pad_id = 0; pad_id < self->num_request_pads; pad_id++)
    gst_buffer_unref (self->priv->inbufs[pad_id]);
  return fret;
}

static void
gst_vvas_xcompositor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (object);

  switch (prop_id) {
    case PROP_KERN_NAME:
      g_value_set_string (value, self->kern_name);
      break;
    case PROP_XCLBIN_LOCATION:
      g_value_set_string (value, self->xclbin_path);
      break;
#ifdef XLNX_PCIe_PLATFORM
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, self->dev_index);
      break;
#endif
    case PROP_IN_MEM_BANK:
      g_value_set_uint (value, self->in_mem_bank);
      break;
    case PROP_OUT_MEM_BANK:
      g_value_set_uint (value, self->out_mem_bank);
      break;
    case PROP_PPC:
      g_value_set_enum (value, self->ppc);
      break;
    case PROP_SCALE_MODE:
      g_value_set_int (value, self->scale_mode);
      break;
    case PROP_NUM_TAPS:
      g_value_set_enum (value, self->num_taps);
      break;
    case PROP_COEF_LOADING_TYPE:
      g_value_set_enum (value, self->coef_load_type);
      break;
    case PROP_BEST_FIT:
      g_value_set_boolean (value, self->best_fit);
      break;
    case PROP_AVOID_OUTPUT_COPY:
      g_value_set_boolean (value, self->avoid_output_copy);
      break;
    case PROP_ENABLE_PIPELINE:
      g_value_set_boolean (value, self->enabled_pipeline);
      break;
#ifdef ENABLE_XRM_SUPPORT
    case PROP_RESERVATION_ID:
      g_value_set_uint64 (value, self->priv->reservation_id);
      break;
#endif

      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_xcompositor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (object);

  switch (prop_id) {
    case PROP_KERN_NAME:
      if (GST_STATE (self) != GST_STATE_NULL) {
        g_warning
            ("can't set kern_name path when instance is NOT in NULL state");
        return;
      }

      if (self->kern_name)
        g_free (self->kern_name);

      self->kern_name = g_value_dup_string (value);
      break;
    case PROP_XCLBIN_LOCATION:
      if (GST_STATE (self) != GST_STATE_NULL) {
        g_warning
            ("can't set config_file path when instance is NOT in NULL state");
        return;
      }
      self->xclbin_path = g_value_dup_string (value);
      break;
#ifdef XLNX_PCIe_PLATFORM
    case PROP_DEVICE_INDEX:
      self->dev_index = g_value_get_int (value);
      break;
#endif
    case PROP_IN_MEM_BANK:
      self->in_mem_bank = g_value_get_uint (value);
      break;
    case PROP_OUT_MEM_BANK:
      self->out_mem_bank = g_value_get_uint (value);
      break;
    case PROP_PPC:
      self->ppc = g_value_get_enum (value);
      break;
    case PROP_SCALE_MODE:
      self->scale_mode = g_value_get_int (value);
      break;
    case PROP_NUM_TAPS:
      self->num_taps = g_value_get_enum (value);
      break;
    case PROP_COEF_LOADING_TYPE:
      self->coef_load_type = g_value_get_enum (value);
      break;
    case PROP_BEST_FIT:
      self->best_fit = g_value_get_boolean (value);
      break;
    case PROP_AVOID_OUTPUT_COPY:
      self->avoid_output_copy = g_value_get_boolean (value);
      break;
    case PROP_ENABLE_PIPELINE:
      self->enabled_pipeline = g_value_get_boolean (value);
      break;
#ifdef ENABLE_XRM_SUPPORT
    case PROP_RESERVATION_ID:
      self->priv->reservation_id = g_value_get_uint64 (value);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPad *
gst_vvas_xcompositor_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstVvasXCompositorPad *newpad = NULL;
  GstPad *pad;
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (element);

  if (self->num_request_pads > (MAX_CHANNELS - 1)) {
    GST_DEBUG_OBJECT (element, "Maximum num of pads supported is only 16");
    return NULL;
  }

  pad = (GstPad *)
      GST_ELEMENT_CLASS (parent_class)->request_new_pad (element,
      templ, req_name, caps);

  if (pad == NULL)
    goto could_not_create;

  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  newpad = (GstVvasXCompositorPad *) pad;

  newpad->index = self->num_request_pads;
  newpad->validate_import = TRUE;
  newpad->pool = NULL;
  self->sinkpads = g_list_append (self->sinkpads, pad);

  self->num_request_pads++;
  return GST_PAD_CAST (newpad);

could_not_create:
  {
    GST_DEBUG_OBJECT (element, "could not create/add pad");
    return NULL;
  }
}

static void
gst_vvas_xcompositor_release_pad (GstElement * element, GstPad * pad)
{
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (element);
  GstVvasXCompositorPad *compositor_pad = (GstVvasXCompositorPad *) pad;
  GList *lsrc = NULL;
  guint index;
  if (!compositor_pad)
    return;

  lsrc = g_list_find (self->sinkpads, compositor_pad);
  if (!lsrc) {
    GST_ERROR_OBJECT (self, "could not find pad to release");
    return;
  }
  gst_video_info_free (compositor_pad->in_vinfo);
  self->sinkpads = g_list_remove (self->sinkpads, compositor_pad);
  index = compositor_pad->index;
  GST_DEBUG_OBJECT (self, "releasing pad with index = %d", index);

  gst_child_proxy_child_removed (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_ELEMENT_CLASS (parent_class)->release_pad (element, pad);

  self->num_request_pads--;

}

static gboolean
gst_vvas_xcompositor_start (GstAggregator * agg)
{
  return TRUE;
}

static gboolean
gst_vvas_xcompositor_stop (GstAggregator * agg)
{
  return TRUE;
}


/* GstChildProxy implementation */
static GObject *
gst_vvas_xcompositor_child_proxy_get_child_by_index (GstChildProxy *
    child_proxy, guint index)
{
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (child_proxy);
  GObject *obj = NULL;

  GST_OBJECT_LOCK (self);
  obj = g_list_nth_data (GST_ELEMENT_CAST (self)->sinkpads, index);
  if (obj)
    gst_object_ref (obj);
  GST_OBJECT_UNLOCK (self);

  return obj;
}

static guint
gst_vvas_xcompositor_child_proxy_get_children_count (GstChildProxy *
    child_proxy)
{
  guint count = 0;
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (child_proxy);

  GST_OBJECT_LOCK (self);
  count = GST_ELEMENT_CAST (self)->numsinkpads;
  GST_OBJECT_UNLOCK (self);
  GST_INFO_OBJECT (self, "Children Count: %d", count);

  return count;
}

static void
gst_vvas_xcompositor_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  iface->get_child_by_index =
      gst_vvas_xcompositor_child_proxy_get_child_by_index;
  iface->get_children_count =
      gst_vvas_xcompositor_child_proxy_get_children_count;
}

static gboolean
vvas_xcompositor_init (GstPlugin * vvas_xcompositor)
{
  return gst_element_register (vvas_xcompositor, "vvas_xcompositor",
      GST_RANK_NONE, GST_TYPE_VVAS_XCOMPOSITOR);
}



GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xcompositor,
    "Xilinx VVAS Compositor plugin", vvas_xcompositor_init, VVAS_API_VERSION,
    "LGPL", "Xilinx VVAS SDK plugin", "http://xilinx.com/")
