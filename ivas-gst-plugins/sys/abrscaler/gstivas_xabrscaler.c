/*
 * Copyright (C) 2020 - 2021 Xilinx, Inc.  All rights reserved.
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

/* #define ENABLE_PPE_SUPPORT 1 */
#include <gst/gst.h>
#include <stdio.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/video/video.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <gst/ivas/gstivasallocator.h>
#include <gst/ivas/gstivasbufferpool.h>
#include <gst/ivas/gstinferencemeta.h>
#include <ivas/xrt_utils.h>
#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include "gstivas_xabrscaler.h"
#include "multi_scaler_hw.h"

#ifdef ENABLE_XRM_SUPPORT
#include <xrm.h>
#include <xrm_limits.h>
#include <jansson.h>
#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))
#endif

#ifdef XLNX_PCIe_PLATFORM
#define DEFAULT_DEVICE_INDEX -1
#define NEED_DMABUF 0
#else
#define DEFAULT_DEVICE_INDEX 0 /* on Embedded only one device i.e. device 0 */
#define NEED_DMABUF 1
#endif
static const int ERT_CMD_SIZE = 4096;
#define MULTI_SCALER_TIMEOUT 1000       // 1 sec
#define ALIGN(size,align) (((size) + (align) - 1) & ~((align) - 1))
#define DIV_AND_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define IVAS_XABRSCALER_AVOID_OUTPUT_COPY_DEFAULT FALSE
#define MEM_BANK 0

/*256x64 for AWS use-case only*/
#ifdef XLNX_PCIe_PLATFORM
#define WIDTH_ALIGN 256
#define HEIGHT_ALIGN 64
#else
#define WIDTH_ALIGN (8 * self->ppc)
#define HEIGHT_ALIGN 1
#endif

pthread_mutex_t count_mutex;
GST_DEBUG_CATEGORY_STATIC (gst_ivas_xabrscaler_debug);
#define GST_CAT_DEFAULT gst_ivas_xabrscaler_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);
enum
{
  PROP_0,
  PROP_XCLBIN_LOCATION,
  PROP_KERN_NAME,
#ifdef XLNX_PCIe_PLATFORM
  PROP_DEVICE_INDEX,
#endif
  PROP_PPC,
  PROP_SCALE_MODE,
  PROP_NUM_TAPS,
  PROP_COEF_LOADING_TYPE,
  PROP_AVOID_OUTPUT_COPY,
#ifdef ENABLE_PPE_SUPPORT
  PROP_ALPHA_R,
  PROP_ALPHA_G,
  PROP_ALPHA_B,
  PROP_BETA_R,
  PROP_BETA_G,
  PROP_BETA_B,
#endif
#ifdef ENABLE_XRM_SUPPORT
  PROP_RESERVATION_ID,
#endif
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGBx, YUY2, r210, Y410, NV16, NV12, RGB, v308, I422_10LE, GRAY8, \
	NV12_10LE32, BGRx, GRAY10_LE32, BGRx, UYVY, BGR, RGBA, BGRA}")));

static GstStaticPadTemplate src_request_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGBx, YUY2, r210, Y410, NV16, NV12, RGB, v308, I422_10LE, GRAY8, \
	NV12_10LE32, BGRx, GRAY10_LE32, BGRx, UYVY, BGR, RGBA, BGRA}")));

GType gst_ivas_xabrscaler_pad_get_type (void);

typedef struct _GstIvasXAbrScalerPad GstIvasXAbrScalerPad;
typedef struct _GstIvasXAbrScalerPadClass GstIvasXAbrScalerPadClass;

struct _GstIvasXAbrScalerPad
{
  GstPad parent;
  guint index;
  GstBufferPool *pool;
  GstVideoInfo *in_vinfo;
  GstVideoInfo *out_vinfo;
  xrt_buffer *out_xrt_buf;
};

struct _GstIvasXAbrScalerPadClass
{
  GstPadClass parent;
};

#define GST_TYPE_IVAS_XABRSCALER_PAD \
	(gst_ivas_xabrscaler_pad_get_type())
#define GST_IVAS_XABRSCALER_PAD(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_IVAS_XABRSCALER_PAD, \
				     GstIvasXAbrScalerPad))
#define GST_IVAS_XABRSCALER_PAD_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_IVAS_XABRSCALER_PAD, \
				  GstIvasXAbrScalerPadClass))
#define GST_IS_IVAS_XABRSCALER_PAD(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_IVAS_XABRSCALER_PAD))
#define GST_IS_IVAS_XABRSCALER_PAD_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_IVAS_XABRSCALER_PAD))
#define GST_IVAS_XABRSCALER_PAD_CAST(obj) \
	((GstIvasXAbrScalerPad *)(obj))

G_DEFINE_TYPE (GstIvasXAbrScalerPad, gst_ivas_xabrscaler_pad, GST_TYPE_PAD);

#define gst_ivas_xabrscaler_srcpad_at_index(self, idx) ((GstIvasXAbrScalerPad *)(g_list_nth ((self)->srcpads, idx))->data)
#define gst_ivas_xabrscaler_srcpad_get_index(self, srcpad) (g_list_index ((self)->srcpads, (gconstpointer)srcpad))

static void
gst_ivas_xabrscaler_pad_class_init (GstIvasXAbrScalerPadClass * klass)
{
  /* nothing */
}

static void
gst_ivas_xabrscaler_pad_init (GstIvasXAbrScalerPad * pad)
{
}

static void gst_ivas_xabrscaler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ivas_xabrscaler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_ivas_xabrscaler_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_ivas_xabrscaler_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static gboolean gst_ivas_xabrscaler_sink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GstStateChangeReturn gst_ivas_xabrscaler_change_state
    (GstElement * element, GstStateChange transition);
static GstCaps *gst_ivas_xabrscaler_transform_caps (GstIvasXAbrScaler * self,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstPad *gst_ivas_xabrscaler_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name_templ, const GstCaps * caps);
static void gst_ivas_xabrscaler_release_pad (GstElement * element,
    GstPad * pad);
void copy_filt_set(int16_t dest_filt[64][12], int set);
int Select_filt_id(float down_scale_ratio);
void Generate_cardinal_cubic_spline(int src, int dst, int filterSize,
    int64_t B, int64_t C, int16_t* CCS_filtCoeff);

struct _GstIvasXAbrScalerPrivate
{
  GstVideoInfo *in_vinfo;
  uint32_t meta_in_stride;
  xclDeviceHandle xcl_handle;
  uint32_t cu_index;
  bool is_coeff;
  uuid_t xclbinId;
  xrt_buffer *ert_cmd_buf;
  size_t min_offset, max_offset;
  xrt_buffer Hcoff[MAX_CHANNELS];
  xrt_buffer Vcoff[MAX_CHANNELS];
  GstBuffer *outbufs[MAX_CHANNELS];
  xrt_buffer msPtr[MAX_CHANNELS];
  guint64 phy_in_0;
  guint64 phy_in_1;
  unsigned long int phy_out[MAX_CHANNELS];
  guint64 out_offset[MAX_CHANNELS];
  GstBufferPool *input_pool;
  gboolean validate_import;
  gboolean need_copy[MAX_CHANNELS];
#ifdef ENABLE_XRM_SUPPORT
  xrmContext xrm_ctx;
  xrmCuResource *cu_resource;
  gint cur_load;
  guint64 reservation_id;
  gboolean has_error;
#endif
};

#define gst_ivas_xabrscaler_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstIvasXAbrScaler, gst_ivas_xabrscaler,
    GST_TYPE_ELEMENT);
#define GST_IVAS_XABRSCALER_PRIVATE(self) (GstIvasXAbrScalerPrivate *) (gst_ivas_xabrscaler_get_instance_private (self))

#ifdef XLNX_PCIe_PLATFORM /* default taps for PCIe platform 12 */
#define IVAS_XABRSCALER_DEFAULT_NUM_TAPS 12
#else /* default taps for Embedded platform 6 */
#define IVAS_XABRSCALER_DEFAULT_NUM_TAPS 6
#endif

#define IVAS_XABRSCALER_NUM_TAPS_TYPE (ivas_xabrscaler_num_taps_type ())

static guint
ivas_xabrscaler_get_stride (GstVideoInfo *info, guint width)
{
  guint stride = 0;

  switch (GST_VIDEO_INFO_FORMAT(info)) {
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV16:
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
      stride = DIV_AND_ROUND_UP(width * 4, 3);
      break;
    default:
      GST_ERROR ("Not supporting %s yet",
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT(info)));
      stride = 0;
      break;
  }
  return stride;
}

static guint
ivas_xabrscaler_get_padding_right (GstIvasXAbrScaler * self,
    GstVideoInfo * info)
{
  guint padding_pixels = -1;
  guint plane_stride = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  guint padding_bytes = ALIGN (plane_stride, self->out_stride_align) - plane_stride;

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

static GType
ivas_xabrscaler_num_taps_type (void)
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
    num_tap = g_enum_register_static ("GstIvasXAbrScalerNumTapsType", taps);
  }
  return num_tap;
}

#ifdef XLNX_PCIe_PLATFORM
#define IVAS_XABRSCALER_DEFAULT_COEF_LOAD_TYPE COEF_AUTO_GENERATE
#else
#define IVAS_XABRSCALER_DEFAULT_COEF_LOAD_TYPE COEF_FIXED
#endif

#define IVAS_XABRSCALER_COEF_LOAD_TYPE (ivas_xabrscaler_coef_load_type ())

static GType
ivas_xabrscaler_coef_load_type (void)
{
  static GType load_type = 0;

  if (!load_type) {
    static const GEnumValue load_types[] = {
      {COEF_FIXED, "Use fixed filter coefficients", "fixed"},
      {COEF_AUTO_GENERATE, "Auto generate filter coefficients", "auto"},
      {0, NULL, NULL}
    };
    load_type = g_enum_register_static ("GstIvasXAbrScalerCoefLoadType", load_types);
  }
  return load_type;
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
  uint16_t MMWidthBytes = AXIMMDataWidth / 8;
  stride = (((stride_in) + MMWidthBytes - 1) / MMWidthBytes) * MMWidthBytes;
  return stride;
}

static int
log2_val(unsigned int val)
{
  int cnt = 0;
  while(val > 1) {
   val=val>>1;
   cnt++;
  }
  return cnt;
}

static gboolean
feasibilityCheck(int src, int dst, int* filterSize)
{
  int sizeFactor = 4;
  int xInc = (((int64_t)src << 16) + (dst >> 1)) / dst;
  if (xInc <= 1 << 16)
    *filterSize = 1 + sizeFactor;    // upscale
  else
    *filterSize = 1 + (sizeFactor * src + dst - 1) / dst;

  if (*filterSize > MAX_FILTER_SIZE) {
    GST_ERROR("FilterSize %d for %d to %d is greater than maximum taps(%d)",
        *filterSize, src, dst, MAX_FILTER_SIZE);
    return FALSE;
  }

  return TRUE;
}

void copy_filt_set(int16_t dest_filt[64][12], int set)
{
  int i=0, j=0;

  for (i=0; i<64; i++) {
    for (j=0; j<12; j++) {
      switch(set) {
        case XLXN_FIXED_COEFF_SR1:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR1_0[i][j];//SR1.0
          break;
        case XLXN_FIXED_COEFF_SR105:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR1_05[i][j]; //SR1.05
          break;
        case XLXN_FIXED_COEFF_SR115:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR1_15[i][j]; //SR1.15
          break;
        case XLXN_FIXED_COEFF_SR125:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR1_25[i][j]; //SR2.5
          break;
        case XLXN_FIXED_COEFF_SR14:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR1_4[i][j]; //SR1.4
          break;
        case XLXN_FIXED_COEFF_SR175:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR1_75[i][j]; //SR1.75
          break;
        case XLXN_FIXED_COEFF_SR19:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR1_9[i][j]; //SR1.9
          break;
        case XLXN_FIXED_COEFF_SR2:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR2_0[i][j]; //SR2.0
          break;
        case XLXN_FIXED_COEFF_SR225:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR2_25[i][j]; //SR2.25
          break;
        case XLXN_FIXED_COEFF_SR25:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR2_5[i][j]; //SR2.5
          break;
        case XLXN_FIXED_COEFF_SR3:
          dest_filt[i][j] = XV_multiscaler_fixed_coeff_SR3_0[i][j]; //SR3.0
          break;
        case XLXN_FIXED_COEFF_TAPS_6:
          dest_filt[i][j] = XV_multiscaler_fixedcoeff_taps6_12C[i][j]; //6tap: Always used for up scale
          break;
        case XLXN_FIXED_COEFF_TAPS_12:
          dest_filt[i][j] = XV_multiscaler_fixedcoeff_taps12_12C[i][j]; //smooth filter
          break;
        default:
          dest_filt[i][j] = XV_multiscaler_fixedcoeff_taps12_12C[i][j]; //12tap
          break;
      }
    }
  }
}

int Select_filt_id(float down_scale_ratio)
{
    int filter_id = XLXN_FIXED_COEFF_TAPS_12;

    if (down_scale_ratio == 1)
       filter_id = XLXN_FIXED_COEFF_SR1;
    else if (down_scale_ratio < 1.10)
       filter_id = XLXN_FIXED_COEFF_SR105;
    else if (down_scale_ratio < 1.20)
       filter_id = XLXN_FIXED_COEFF_SR115;
    else if (down_scale_ratio < 1.30)
       filter_id = XLXN_FIXED_COEFF_SR125;
    else if (down_scale_ratio < 1.45)
       filter_id = XLXN_FIXED_COEFF_SR14;
    else if (down_scale_ratio < 1.60)
       filter_id = XLXN_FIXED_COEFF_SR15;
    else if (down_scale_ratio < 1.80)
       filter_id = XLXN_FIXED_COEFF_SR175;
    else if (down_scale_ratio < 1.95)
       filter_id = XLXN_FIXED_COEFF_SR19;
    else if (down_scale_ratio < 2.15)
       filter_id = XLXN_FIXED_COEFF_SR2;
    else if (down_scale_ratio < 2.24)
       filter_id = XLXN_FIXED_COEFF_SR225;
    else if (down_scale_ratio < 2.6)
       filter_id = XLXN_FIXED_COEFF_SR25;
    else if (down_scale_ratio < 3.5)
       filter_id = XLXN_FIXED_COEFF_SR3;

    return filter_id;
}

void Generate_cardinal_cubic_spline(int src, int dst, int filterSize, int64_t B, int64_t C, int16_t* CCS_filtCoeff)
{
#ifdef COEFF_DUMP
  FILE *fp;
  char fname[512];
  sprintf(fname,"coeff_%dTO%d.csv",src,dst);
  fp=fopen(fname,"w");
  /*FILE *fph;
  sprintf(fname,"phase_%dTO%d_2Inc.txt",src,dst);
  fph=fopen(fname,"w");*/
  //fprintf(fp,"src:%d => dst:%d\n\n",src,dst);
#endif
  int one= (1<<14);
  int64_t *coeffFilter =NULL;
  int64_t *coeffFilter_reduced = NULL;
  int16_t *outFilter = NULL;
  int16_t *coeffFilter_normalized = NULL;
  int lumXInc      = (((int64_t)src << 16) + (dst >> 1)) / dst;
  int srt = src/dst;
  int lval = log2_val(srt);
  int th0 = 8;
  int lv0 = MIN(lval, th0);
  const int64_t fone = (int64_t)1 << (54-lv0);
  int64_t thr1 = ((int64_t)1 << 31);
  int64_t thr2 = ((int64_t)1<<54)/fone;
  int i, xInc, outFilterSize;
  int num_phases = 64;
  int phase_set[64] ={0};
  int64_t xDstInSrc;
  int xx, j, p, t;
  int64_t d=0, coeff=0, dd=0, ddd=0;
  int phase_cnt =0;
  int64_t error = 0, sum = 0, v = 0;
  int intV =0;
  int fstart_Idx=0, fend_Idx=0, half_Idx=0, middleIdx = 0;
  unsigned int PhaseH = 0, offset = 0, WriteLoc = 0, WriteLocNext = 0, ReadLoc=0, OutputWrite_En = 0;
  int OutPixels = dst;
  int PixelRate = (int)((float)((src*STEP_PRECISION) + (dst/2))/(float)dst);
  int ph_max_sum = 1<<MAX_FILTER_SIZE;
  int sumVal= 0 , maxIdx=0, maxVal=0, diffVal=0;

  xInc = lumXInc;
  filterSize = MAX(filterSize, 1);
  coeffFilter = (int64_t*) calloc(num_phases*filterSize, sizeof(int64_t));
  xDstInSrc = xInc - (1<<16);

  // coefficient generation based on scaler IP
  for (i = 0; i < src; i++) {
    PhaseH = ((offset>>(STEP_PRECISION_SHIFT-NR_PHASE_BITS))) & (NR_PHASES-1);
    WriteLoc = WriteLocNext;

    if ((offset >> STEP_PRECISION_SHIFT) != 0) {
      // Take a new sample from input, but don't process anything
      ReadLoc++;
      offset = offset - (1<<STEP_PRECISION_SHIFT);
      OutputWrite_En   = 0;
      WriteLocNext = WriteLoc;
    }

    if (((offset >> STEP_PRECISION_SHIFT) == 0) && (WriteLoc<OutPixels)) {
      // Produce a new output sample
      offset += PixelRate;
      OutputWrite_En   = 1;
      WriteLocNext = WriteLoc+1;
    }

    if (OutputWrite_En) {
      xDstInSrc = ReadLoc*(1<<17) + PhaseH*(1<<11);
      xx = ReadLoc - (filterSize - 2)/2;

      d = (ABS(((int64_t)xx * (1 << 17)) - xDstInSrc)) << 13;

      //count number of phases used for this SR
      if (phase_set[PhaseH] == 0)
        phase_cnt+=1;

      //Filter coeff generation
      for (j = 0; j < filterSize; j++) {
        d = (ABS(((int64_t)xx * (1 << 17)) - xDstInSrc)) << 13;
        if (xInc > 1 << 16) {
          d = (int64_t)(d *dst/ src);
        }

        if (d >= thr1) {
          coeff = 0.0;
        } else {
          dd  = (int64_t)(d  * d) >> 30;
          ddd = (int64_t) (dd * d) >> 30;
          if (d < 1 << 30) {
            coeff =  (12 * (1 << 24) -  9 * B - 6 * C) * ddd +
                (-18 * (1 << 24) + 12 * B + 6 * C) *  dd +
                (6 * (1 << 24) -  2 * B) * (1 << 30);
          } else {
            coeff =  (-B -  6 * C) * ddd +
                (6 * B + 30 * C) * dd  +
                (-12 * B - 48 * C) * d   +
                (8 * B + 24 * C) * (1 << 30);
          }
        }

        coeff = coeff/thr2;
        coeffFilter[PhaseH * filterSize + j] = coeff;
        xx++;
      }
      if (phase_set[PhaseH] == 0) {
        phase_set[PhaseH] = 1;
      }
    }
  }

  coeffFilter_reduced = (int64_t*) calloc((num_phases*filterSize), sizeof(int64_t));
  memcpy(coeffFilter_reduced, coeffFilter, sizeof(int64_t)*num_phases*filterSize);
  outFilterSize = filterSize;
  outFilter = (int16_t*) calloc((num_phases*outFilterSize),sizeof(int16_t));
  coeffFilter_normalized = (int16_t*) calloc((num_phases*outFilterSize),sizeof(int16_t));

  /* normalize & store in outFilter */
  for ( i = 0; i < num_phases; i++) {
    error = 0;
    sum   = 0;

    for (j = 0; j < filterSize; j++) {
      sum += coeffFilter_reduced[i * filterSize + j];
    }
    sum = (sum + one / 2) / one;
    if (!sum) {
      sum = 1;
    }
    for (j = 0; j < outFilterSize; j++) {
      v = coeffFilter_reduced[i * filterSize + j] + error;
      intV  = ROUNDED_DIV(v, sum);
      coeffFilter_normalized[i * (outFilterSize) + j] = intV;
      coeffFilter_normalized[i * (outFilterSize) + j] =
      coeffFilter_normalized[i * (outFilterSize) + j]>>2; //added to negate double increment and match our precision
      error = v - intV * sum;
    }
  }

  for (p=0; p<num_phases; p++) {
    for (t=0; t<filterSize; t++) {
      outFilter[p*filterSize + t] = coeffFilter_normalized[p*filterSize + t];
    }
  }

  /*incorporate filter less than 12 tap into a 12 tap*/
  fstart_Idx=0, fend_Idx=0, half_Idx=0;
  middleIdx = (MAX_FILTER_SIZE/2); //center location for 12 tap
  half_Idx = (outFilterSize/2);
  if ( (outFilterSize - (half_Idx<<1)) ==0) { //evenOdd
    fstart_Idx = middleIdx - half_Idx;
    fend_Idx = middleIdx + half_Idx;
  } else {
    fstart_Idx = middleIdx - (half_Idx);
    fend_Idx = middleIdx + half_Idx + 1;
  }

  for ( i = 0; i < num_phases; i++) {
    for ( j = 0; j < MAX_FILTER_SIZE; j++) {

      CCS_filtCoeff[i*MAX_FILTER_SIZE + j] = 0;
      if ((j >= fstart_Idx) && (j< fend_Idx))
        CCS_filtCoeff[i*MAX_FILTER_SIZE + j] = outFilter[i * (outFilterSize) + (j-fstart_Idx)];
    }
  }

  /*Make sure filterCoeffs within a phase sum to 4096*/
  for ( i = 0; i < num_phases; i++) {
    sumVal = 0;
    maxVal = 0;
    for ( j = 0; j < MAX_FILTER_SIZE; j++) {
      sumVal+=CCS_filtCoeff[i*MAX_FILTER_SIZE + j];
      if (CCS_filtCoeff[i*MAX_FILTER_SIZE + j] > maxVal) {
        maxVal = CCS_filtCoeff[i*MAX_FILTER_SIZE + j];
        maxIdx = j;
      }
    }
    diffVal = ph_max_sum - sumVal ;
    if (diffVal>0)
      CCS_filtCoeff[i*MAX_FILTER_SIZE + maxIdx] = CCS_filtCoeff[i*MAX_FILTER_SIZE + maxIdx]+diffVal;
  }


#ifdef COEFF_DUMP
  fprintf(fp,"taps/phases, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12\n");
  for ( i = 0; i < num_phases; i++) {
    fprintf(fp,"%d, ", i+1);
    for ( j = 0; j < MAX_FILTER_SIZE; j++) {
      fprintf(fp,"%d,  ",CCS_filtCoeff[i*MAX_FILTER_SIZE + j]);
    }
    fprintf(fp,"\n");
  }
#endif

  free(coeffFilter);
  free(coeffFilter_reduced);
  free(outFilter);
  free(coeffFilter_normalized);
#ifdef COEFF_DUMP
  fclose(fp);
#endif
}

static void
ivas_xabrscaler_prepare_coefficients_with_12tap (GstIvasXAbrScaler * self, guint chan_id)
{
  GstIvasXAbrScalerPrivate *priv = self->priv;
  GstIvasXAbrScalerPad *srcpad = NULL;
  gint filter_size;
  guint in_width, in_height, out_width, out_height;
  int64_t B = 0 * (1 << 24);
  int64_t C = 0.6 * (1 << 24);
  float scale_ratio[2] = {0, 0};
  int upscale_enable[2] = {0, 0};
  int filterSet[2] = {0, 0};
  guint d;
  gboolean bret;

  srcpad = gst_ivas_xabrscaler_srcpad_at_index (self, chan_id);

  in_width = GST_VIDEO_INFO_WIDTH (srcpad->in_vinfo);
  in_height = GST_VIDEO_INFO_HEIGHT (srcpad->in_vinfo);
  out_width = GST_VIDEO_INFO_WIDTH (srcpad->out_vinfo);
  out_height = GST_VIDEO_INFO_HEIGHT (srcpad->out_vinfo);

  /* store width scaling ratio  */
  if (in_width >= out_width) {
    scale_ratio[0] = (float)in_width/(float)out_width; //downscale
  } else {
    scale_ratio[0] = (float)out_width/(float)in_width; //upscale
    upscale_enable[0] = 1;
  }

  /* store height scaling ratio */
  if (in_height >= out_height) {
    scale_ratio[1] = (float)in_height/(float)out_height; //downscale
  } else {
    scale_ratio[1] = (float)out_height/(float)in_height;//upscale
    upscale_enable[1] = 1;
  }

  for (d=0; d<2; d++) {
    if (upscale_enable[d] == 1) {
      /* upscaling default use 6 taps */
      filterSet[d] = XLXN_FIXED_COEFF_TAPS_6;
    } else {
      /* Get index of downscale fixed filter*/
      filterSet[d] = Select_filt_id(scale_ratio[d]);
    }
    GST_INFO_OBJECT (self, "%s scaling ratio = %f and chosen filter type = %d",
        d == 0 ? "width" : "height", scale_ratio[d], filterSet[d]);
  }

  if (self->coef_load_type == COEF_AUTO_GENERATE) {
    /* prepare horizontal coefficients */
    bret = feasibilityCheck (in_width, out_width, &filter_size);
    if (bret && !upscale_enable[0]) {
      GST_INFO_OBJECT (self, "Generate cardinal cubic horizontal coefficients "
          "with filter size %d", filter_size);
      Generate_cardinal_cubic_spline(in_width, out_width, filter_size, B, C,
          (int16_t *)priv->Hcoff[chan_id].user_ptr);
    } else {
      /* get fixed horizontal filters*/
      GST_INFO_OBJECT (self, "Consider predefined horizontal filter coefficients");
      copy_filt_set(priv->Hcoff[chan_id].user_ptr, filterSet[0]);
    }

    /* prepare vertical coefficients */
    bret = feasibilityCheck(in_height, out_height, &filter_size);
    if (bret && !upscale_enable[1]) {
      GST_INFO_OBJECT (self, "Generate cardinal cubic vertical coefficients "
          "with filter size %d", filter_size);
      Generate_cardinal_cubic_spline(in_height, out_height, filter_size, B, C,
          (int16_t *)priv->Vcoff[chan_id].user_ptr);
    } else {
      /* get fixed vertical filters*/
      GST_INFO_OBJECT (self, "Consider predefined vertical filter coefficients");
      copy_filt_set(priv->Vcoff[chan_id].user_ptr, filterSet[1]);
    }
  } else if (self->coef_load_type == COEF_FIXED){
    /* get fixed horizontal filters*/
    GST_INFO_OBJECT (self, "Consider predefined horizontal filter coefficients");
    copy_filt_set(priv->Hcoff[chan_id].user_ptr, filterSet[0]);

    /* get fixed vertical filters*/
    GST_INFO_OBJECT (self, "Consider predefined vertical filter coefficients");
    copy_filt_set(priv->Vcoff[chan_id].user_ptr, filterSet[1]);
  }
}

static gboolean
ivas_xabrscaler_register_prep_write_with_caps (GstIvasXAbrScaler * self,
    guint chan_id, GstCaps * in_caps, GstCaps * out_caps)
{
  GstIvasXAbrScalerPad *srcpad = NULL;

  srcpad = gst_ivas_xabrscaler_srcpad_at_index (self, chan_id);

  if (!gst_video_info_from_caps (srcpad->in_vinfo, in_caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from input caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (srcpad->out_vinfo, out_caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from output caps");
    return FALSE;
  }

  return TRUE;
}

#ifdef ENABLE_XRM_SUPPORT
static gchar *
ivas_xabrscaler_prepare_request_json_string (GstIvasXAbrScaler *scaler)
{
  json_t *in_jobj, *jarray, *fps_jobj, *tmp_jobj, *tmp2_jobj, *res_jobj;
  guint in_width, in_height;
  guint in_fps_n, in_fps_d;
  gint idx;
  gchar *req_str = NULL;

  jarray = json_array();

  for (idx = 0; idx < scaler->num_request_pads; idx++) {
    GstIvasXAbrScalerPad *srcpad;
    GstCaps *out_caps = NULL;
    guint out_fps_n, out_fps_d;
    GstVideoInfo out_vinfo;
    guint out_width, out_height;
    gboolean bret;
    json_t *out_jobj = NULL;

    srcpad = gst_ivas_xabrscaler_srcpad_at_index (scaler, idx);
    if (!srcpad) {
      GST_ERROR_OBJECT (scaler, "failed to get srcpad at index %d", idx);
      goto out_error;
    }

    out_caps = gst_pad_get_current_caps ((GstPad *) srcpad);
    if (!out_caps) {
      GST_ERROR_OBJECT (scaler, "failed to get output caps at srcpad index %d",
          idx);
      goto out_error;
    }

    bret = gst_video_info_from_caps (&out_vinfo, out_caps);
    if (!bret) {
      GST_ERROR_OBJECT (scaler, "failed to get video info from caps");
      goto out_error;
    }

    out_width = GST_VIDEO_INFO_WIDTH (&out_vinfo);
    out_height = GST_VIDEO_INFO_HEIGHT (&out_vinfo);
    out_fps_n = GST_VIDEO_INFO_FPS_N (&out_vinfo);
    out_fps_d = GST_VIDEO_INFO_FPS_D (&out_vinfo);

    if (!out_fps_n) {
      GST_WARNING_OBJECT (scaler, "out fps is not available, taking default 60/1");
      out_fps_n = 60;
      out_fps_d = 1;
    }

    out_jobj = json_object ();
    if (!out_jobj)
      goto out_error;

    json_object_set_new (out_jobj, "width", json_integer(out_width));
    json_object_set_new (out_jobj, "height", json_integer(out_height));

    fps_jobj = json_object ();
    if (!fps_jobj)
      goto out_error;

    json_object_set_new (fps_jobj, "num", json_integer(out_fps_n));
    json_object_set_new (fps_jobj, "den", json_integer(out_fps_d));
    json_object_set_new (out_jobj, "frame-rate", fps_jobj);

    json_array_append_new (jarray, out_jobj);
    continue;

out_error:
    if (out_caps)
      gst_caps_unref (out_caps);
    if (out_jobj)
      json_decref (out_jobj);
    if (fps_jobj)
      json_decref (fps_jobj);
    goto error;
  }

  in_jobj = json_object ();
  if (!in_jobj)
    goto error;

  in_width = GST_VIDEO_INFO_WIDTH (scaler->priv->in_vinfo);
  in_height = GST_VIDEO_INFO_HEIGHT (scaler->priv->in_vinfo);
  in_fps_n = GST_VIDEO_INFO_FPS_N (scaler->priv->in_vinfo);
  in_fps_d = GST_VIDEO_INFO_FPS_D (scaler->priv->in_vinfo);

  if (!in_fps_n) {
    GST_WARNING_OBJECT (scaler, "in fps is not available, taking default 60/1");
    in_fps_n = 60;
    in_fps_d = 1;
  }

  json_object_set_new (in_jobj, "width", json_integer(in_width));
  json_object_set_new (in_jobj, "height", json_integer(in_height));

  fps_jobj = json_object ();
  if (!fps_jobj)
    goto error;

  json_object_set_new (fps_jobj, "num", json_integer(in_fps_n));
  json_object_set_new (fps_jobj, "den", json_integer(in_fps_d));
  json_object_set_new (in_jobj, "frame-rate", fps_jobj);

  res_jobj = json_object ();
  if (!res_jobj)
    goto error;

  json_object_set_new (res_jobj, "input", in_jobj);
  json_object_set_new (res_jobj, "output", jarray);

  tmp_jobj = json_object ();
  if (!tmp_jobj)
    goto error;

  json_object_set_new (tmp_jobj, "function", json_string("SCALER"));
  json_object_set_new (tmp_jobj, "format", json_string("yuv420p"));
  json_object_set_new (tmp_jobj, "resolution", res_jobj);

  jarray = json_array();
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
  GST_LOG_OBJECT (scaler, "prepared xrm request %s", req_str);
  json_decref (tmp_jobj);

  return req_str;

error:
  return NULL;
}

static gboolean
ivas_xabrscaler_calculate_load (GstIvasXAbrScaler * self, gint *load)
{
  GstIvasXAbrScalerPrivate *priv = self->priv;
  int iret = -1, func_id = 0;
  gchar *req_str;
  char plugin_name[XRM_MAX_NAME_LEN];
  xrmPluginFuncParam param;

  if (!priv->xrm_ctx) {
    GST_ERROR_OBJECT (self, "xrm context not created");
    return FALSE;
  }

  /* prepare json string to request xrm for load */
  req_str = ivas_xabrscaler_prepare_request_json_string (self);
  if (!req_str) {
    GST_ERROR_OBJECT (self, "failed to prepare xrm json request string");
    return FALSE;
  }

  memset (plugin_name, 0x0, XRM_MAX_NAME_LEN);

  strcpy(plugin_name, "xrmU30ScalPlugin");

  if (strlen (req_str) > (XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1)) {
    GST_ERROR_OBJECT(self, "request input string length %lu > max allowed %d",
        strlen (req_str), XRM_MAX_PLUGIN_FUNC_PARAM_LEN-1);
    free (req_str);
    return FALSE;
  }

  strncpy(param.input, req_str, strlen (req_str));
  free (req_str);

  iret = xrmExecPluginFunc(priv->xrm_ctx, plugin_name, func_id, &param);
  if (iret != XRM_SUCCESS) {
    GST_ERROR_OBJECT(self, "failed to get load from xrm plugin. err : %d",
        iret);
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("failed to get load from xrm plugin"), NULL);
    priv->has_error = TRUE;
    return FALSE;
  }

  *load = atoi((char*)(strtok(param.output, " ")));

  if (*load <= 0 || *load > XRM_MAX_CU_LOAD_GRANULARITY_1000000) {
    GST_ERROR_OBJECT (self, "not an allowed multiscaler load %d", *load);
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
        ("wrong multiscaler load %d", *load), NULL);
    return FALSE;
  }

  GST_INFO_OBJECT (self, "need %d%% device's load", (*load * 100) / XRM_MAX_CU_LOAD_GRANULARITY_1000000);
  return TRUE;
}

static gboolean
ivas_xabrscaler_allocate_resource (GstIvasXAbrScaler * self, gint scaler_load)
{
  GstIvasXAbrScalerPrivate *priv = self->priv;
  xrmCuProperty scaler_prop;
  xrmCuResource *cu_resource;
  int iret = -1;

  GST_INFO_OBJECT (self, "going to request %d%% load using xrm", (scaler_load * 100) / XRM_MAX_CU_LOAD_GRANULARITY_1000000);

  memset (&scaler_prop, 0, sizeof (xrmCuProperty));

  if (!priv->cu_resource) {
    cu_resource = (xrmCuResource *) calloc (1, sizeof (xrmCuResource));
    if (!cu_resource) {
      GST_ERROR_OBJECT (self, "failed to allocate memory for hardCU resource");
      return FALSE;
    }
  } else {
    cu_resource = priv->cu_resource;
  }

  strcpy(scaler_prop.kernelName, strtok(self->kern_name, ":"));
  strcpy(scaler_prop.kernelAlias, "SCALER_MPSOC");
  scaler_prop.devExcl = false;
  scaler_prop.requestLoad = XRM_PRECISION_1000000_BIT_MASK(scaler_load);

  if (getenv("XRM_RESERVE_ID") || priv->reservation_id) { /* use reservation_id to allocate scaler */
    int xrm_reserve_id = 0;

    /* element property value takes higher priority than env variable */
    if (priv->reservation_id)
      xrm_reserve_id = priv->reservation_id;
    else
      xrm_reserve_id = atoi(getenv("XRM_RESERVE_ID"));

    scaler_prop.poolId = xrm_reserve_id;

    iret = xrmCuAlloc (priv->xrm_ctx, &scaler_prop, cu_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT(self, "failed to allocate resources from reservation id %d", xrm_reserve_id);
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from reservation id %d",
              xrm_reserve_id), NULL);
      return FALSE;
    }
  } else { /* use user specified device to allocate scaler */

    if (self->dev_index >= xclProbe ()) {
      GST_ERROR_OBJECT (self, "Cannot find device index %d", self->dev_index);
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
          ("failed to find device index %d", self->dev_index), NULL);
      return FALSE;
    }

    iret = xrmCuAllocFromDev(priv->xrm_ctx, self->dev_index, &scaler_prop,
        cu_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT(self, "failed to allocate resources from device id %d. "
          "error: %d", self->dev_index, iret);
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
          ("failed to allocate resources from device id %d", self->dev_index),
          NULL);
      return FALSE;
    }

    if (self->dev_index != cu_resource->deviceId) {
      GST_ERROR_OBJECT (self, "invalid parameters received from XRM");
      return FALSE;
    }
  }

  self->dev_index = cu_resource->deviceId;
  priv->cu_index = cu_resource->cuId;
  uuid_copy (priv->xclbinId, cu_resource->uuid);
  priv->cu_resource = cu_resource;

  return TRUE;
}
#endif

static gboolean
ivas_xabrscaler_create_context (GstIvasXAbrScaler * self)
{
  GstIvasXAbrScalerPrivate *priv = self->priv;
#ifdef ENABLE_XRM_SUPPORT
  gboolean bret;
#endif

  if (!self->kern_name){
    GST_ERROR_OBJECT (self, "kernel name is not set");
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("kernel name is not set"));
    return FALSE;
  }

#ifdef ENABLE_XRM_SUPPORT

  /* gets cu index & device id (using reservation id) */
  bret = ivas_xabrscaler_allocate_resource (self, priv->cur_load);
  if (!bret)
    return FALSE;

  if (self->dev_index >= xclProbe ()) {
    GST_ERROR_OBJECT (self, "Cannot find device index %d", self->dev_index);
    return FALSE;
  }

  priv->xcl_handle = xclOpen (self->dev_index, NULL, XCL_INFO);
  if (!priv->xcl_handle) {
    GST_ERROR_OBJECT (self, "failed to open device index %u",
        self->dev_index);
    return FALSE;
  }
#else

  if (!self->xclbin_path) {
    GST_ERROR_OBJECT (self, "invalid xclbin path %s", self->xclbin_path);
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, (NULL),
        ("xclbin path not set"));
    return FALSE;
  }

#ifdef XLNX_PCIe_PLATFORM
  if (self->dev_index < 0) {
    GST_ERROR_OBJECT (self, "device index %d is not valid", self->dev_index);
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("device index %d is not valid", self->dev_index));
    return FALSE;
  }
#endif

  if (download_xclbin (self->xclbin_path, self->dev_index, NULL,
          &(priv->xcl_handle), &(priv->xclbinId))) {
    GST_ERROR_OBJECT (self, "failed to initialize XRT");
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("xclbin download failed"));
    return FALSE;
  }


  priv->cu_index = xclIPName2Index (priv->xcl_handle, self->kern_name);
#endif

  GST_INFO_OBJECT (self, "device index = %d, cu index = %d", self->dev_index,
      priv->cu_index);

  if (xclOpenContext (priv->xcl_handle, priv->xclbinId, priv->cu_index, true)) {
    GST_ERROR_OBJECT (self, "failed to do xclOpenContext...");
    return FALSE;
  }

  return TRUE;
}

static gboolean
ivas_xabrscaler_allocate_internal_buffers (GstIvasXAbrScaler * self)
{
  GstIvasXAbrScalerPrivate *priv = self->priv;
  gint chan_id, iret;

  GST_INFO_OBJECT (self, "allocating internal buffers");

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    iret =
        alloc_xrt_buffer (priv->xcl_handle, COEFF_SIZE, XCL_BO_DEVICE_RAM,
        MEM_BANK, &priv->Hcoff[chan_id]);
    if (iret < 0) {
      GST_ERROR_OBJECT (self,
          "failed to allocate horizontal coefficients command buffer..");
      goto error;
    }

    iret =
        alloc_xrt_buffer (priv->xcl_handle, COEFF_SIZE, XCL_BO_DEVICE_RAM,
        MEM_BANK, &priv->Vcoff[chan_id]);
    if (iret < 0) {
      GST_ERROR_OBJECT (self,
          "failed to allocate vertical coefficients command buffer..");
      goto error;
    }

    iret =
        alloc_xrt_buffer (priv->xcl_handle, DESC_SIZE, XCL_BO_DEVICE_RAM,
        MEM_BANK, &priv->msPtr[chan_id]);
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
ivas_xabrscaler_free_internal_buffers (GstIvasXAbrScaler * self)
{
  GstIvasXAbrScalerPrivate *priv = self->priv;
  guint chan_id;

  GST_DEBUG_OBJECT (self, "freeing internal buffers");

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    if (priv->Hcoff[chan_id].user_ptr) {
      free_xrt_buffer (priv->xcl_handle, &priv->Hcoff[chan_id]);
      memset (&(self->priv->Hcoff[chan_id]), 0x0, sizeof(xrt_buffer));
    }
    if (priv->Vcoff[chan_id].user_ptr) {
      free_xrt_buffer (priv->xcl_handle, &priv->Vcoff[chan_id]);
      memset (&(self->priv->Vcoff[chan_id]), 0x0, sizeof(xrt_buffer));
    }
    if (priv->msPtr[chan_id].user_ptr) {
      free_xrt_buffer (priv->xcl_handle, &priv->msPtr[chan_id]);
      memset (&(self->priv->msPtr[chan_id]), 0x0, sizeof(xrt_buffer));
    }
  }
}

static gboolean
ivas_xabrscaler_destroy_context (GstIvasXAbrScaler * self)
{
  GstIvasXAbrScalerPrivate *priv = self->priv;
  gboolean has_error = FALSE;
  gint iret;

#ifdef ENABLE_XRM_SUPPORT
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
    free(priv->cu_resource);
    priv->cu_resource = NULL;
    GST_INFO_OBJECT (self, "released CU and destroyed xrm context");
  }
#endif

  if (priv->xcl_handle) {
    iret = xclCloseContext (priv->xcl_handle, priv->xclbinId, priv->cu_index);
    if (iret != 0) {
      GST_ERROR_OBJECT (self, "failed to close xrt context");
      has_error = TRUE;
    }
    xclClose (priv->xcl_handle);
    priv->xcl_handle = NULL;
    GST_INFO_OBJECT (self, "closed xrt context");
  }

  return has_error ? FALSE : TRUE;
}

static gboolean
ivas_xabrscaler_allocate_internal_pool (GstIvasXAbrScaler * self)
{
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstCaps *caps = NULL;

  caps = gst_pad_get_current_caps (self->sinkpad);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  pool = gst_video_buffer_pool_new ();
  GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

  allocator = gst_ivas_allocator_new (self->dev_index, NEED_DMABUF);
  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
  alloc_params.flags |= GST_IVAS_ALLOCATOR_FLAG_MEM_INIT;
  GST_INFO_OBJECT (self, "allocated %" GST_PTR_FORMAT " allocator", allocator);

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (&info),
      3, 5);

  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config on input pool");
    goto error;
  }

  if (self->priv->input_pool)
    gst_object_unref (self->priv->input_pool);

  self->priv->input_pool = pool;

  GST_INFO_OBJECT (self, "allocated %" GST_PTR_FORMAT " pool",
      self->priv->input_pool);
  gst_caps_unref (caps);

  return TRUE;

error:
  gst_caps_unref (caps);
  return FALSE;
}

#ifdef XLNX_PCIe_PLATFORM
static gboolean
xlnx_abr_coeff_syncBO (GstIvasXAbrScaler * self)
{
  GstIvasXAbrScalerPrivate *priv = self->priv;
  int chan_id;
  int iret;

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {

    iret = xclWriteBO (priv->xcl_handle, priv->Hcoff[chan_id].bo,
        priv->Hcoff[chan_id].user_ptr, priv->Hcoff[chan_id].size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (self,
          "failed to write horizontal coefficients. reason : %s",
          strerror (errno));
      return FALSE;
    }
    iret = xclSyncBO (priv->xcl_handle, priv->Hcoff[chan_id].bo,
        XCL_BO_SYNC_BO_TO_DEVICE, priv->Hcoff[chan_id].size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (self,
          "failed to sync horizontal coefficients. reason : %s",
          strerror (errno));
      GST_ELEMENT_ERROR (self, RESOURCE, SYNC, NULL,
          ("failed to sync horizontal coefficients to device. reason : %s",
              strerror (errno)));
      return FALSE;
    }

    iret = xclWriteBO (priv->xcl_handle, priv->Vcoff[chan_id].bo,
        priv->Vcoff[chan_id].user_ptr, priv->Vcoff[chan_id].size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (self,
          "failed to write vertical coefficients. reason : %s",
          strerror (errno));
      return FALSE;
    }
    iret = xclSyncBO (priv->xcl_handle, priv->Vcoff[chan_id].bo,
        XCL_BO_SYNC_BO_TO_DEVICE, priv->Vcoff[chan_id].size, 0);
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
xlnx_abr_desc_syncBO (GstIvasXAbrScaler * self)
{
  GstIvasXAbrScalerPrivate *priv = self->priv;
  int chan_id;
  int iret;

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {

    iret = xclWriteBO (priv->xcl_handle, priv->msPtr[chan_id].bo,
        priv->msPtr[chan_id].user_ptr, priv->msPtr[chan_id].size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (self,
          "failed to write horizontal coefficients. reason : %s",
          strerror (errno));
      return FALSE;
    }
    iret = xclSyncBO (priv->xcl_handle, priv->msPtr[chan_id].bo,
        XCL_BO_SYNC_BO_TO_DEVICE, priv->msPtr[chan_id].size, 0);
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
ivas_xabrscaler_prepare_input_buffer (GstIvasXAbrScaler * self,
    GstBuffer ** inbuf)
{
  GstMemory *in_mem = NULL;
  GstVideoFrame in_vframe, own_vframe;
  guint64 phy_addr = -1;
  GstVideoMeta *vmeta = NULL;
  gboolean bret;
  GstBuffer *own_inbuf;
  GstFlowReturn fret;
  gboolean use_inpool = FALSE;

  memset (&in_vframe, 0x0, sizeof (GstVideoFrame));
  memset (&own_vframe, 0x0, sizeof (GstVideoFrame));

  in_mem = gst_buffer_get_memory (*inbuf, 0);
  if (!in_mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    goto error;
  }

  if (gst_is_ivas_memory (in_mem)
      && gst_ivas_memory_can_avoid_copy (in_mem, self->dev_index)) {
    phy_addr = gst_ivas_allocator_get_paddr (in_mem);
  } else if (gst_is_dmabuf_memory (in_mem)) {
    guint bo = NULLBO;
    gint dma_fd = -1;
    struct xclBOProperties p;

    dma_fd = gst_dmabuf_memory_get_fd (in_mem);
    if (dma_fd < 0) {
      GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
      goto error;
    }

    /* dmabuf but not from xrt */
    bo = xclImportBO (self->priv->xcl_handle, dma_fd, 0);
    if (bo == NULLBO) {
      GST_WARNING_OBJECT (self,
          "failed to get XRT BO...fall back to copy input");
    }

    GST_DEBUG_OBJECT (self, "received dma fd %d and its xrt BO = %u", dma_fd,
        bo);

    if (!xclGetBOProperties (self->priv->xcl_handle, bo, &p)) {
      phy_addr = p.paddr;
    } else {
      GST_WARNING_OBJECT (self,
          "failed to get physical address...fall back to copy input");
      use_inpool = TRUE;
    }

    if (bo != NULLBO)
       xclFreeBO(self->priv->xcl_handle, bo);

  } else {
    use_inpool = TRUE;
  }

  if (use_inpool) {
    if (self->priv->validate_import) {
      if (!self->priv->input_pool) {
        bret = ivas_xabrscaler_allocate_internal_pool (self);
        if (!bret)
          goto error;
      }
      if (!gst_buffer_pool_is_active (self->priv->input_pool))
        gst_buffer_pool_set_active (self->priv->input_pool, TRUE);
      self->priv->validate_import = FALSE;
    }

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
    if (!gst_video_frame_map (&in_vframe, self->priv->in_vinfo, *inbuf,
            GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "failed to map input buffer");
      goto error;
    }
    gst_video_frame_copy (&own_vframe, &in_vframe);

    gst_video_frame_unmap (&in_vframe);
    gst_video_frame_unmap (&own_vframe);
    gst_buffer_copy_into (own_inbuf, *inbuf,
        (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS |
            GST_BUFFER_COPY_TIMESTAMPS), 0, -1);
    gst_buffer_unref (*inbuf);
    *inbuf = own_inbuf;
  }

  vmeta = gst_buffer_get_video_meta (*inbuf);
  if (vmeta == NULL) {
    GST_ERROR_OBJECT (self, "video meta not present in buffer");
    goto error;
  }

  if (phy_addr == (uint64_t) - 1) {

    if (in_mem)
      gst_memory_unref (in_mem);

    in_mem = gst_buffer_get_memory (*inbuf, 0);
    if (in_mem == NULL) {
      GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
      goto error;
    }
    phy_addr = gst_ivas_allocator_get_paddr (in_mem);
  }
#ifdef XLNX_PCIe_PLATFORM
  /* syncs data when XLNX_SYNC_TO_DEVICE flag is enabled */
  bret = gst_ivas_memory_sync_bo (in_mem);
  if (!bret)
    goto error;
#endif

  gst_memory_unref (in_mem);

  GST_LOG_OBJECT (self, "input paddr %p", (void *) phy_addr);
  self->priv->phy_in_1 = 0;
  self->priv->phy_in_0 = phy_addr;
  if (vmeta->n_planes == 2)     /* Only support plane 1 and 2 */
    self->priv->phy_in_1 = phy_addr + vmeta->offset[1];

  self->priv->meta_in_stride = *(vmeta->stride);

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
ivas_xabrscaler_prepare_output_buffer (GstIvasXAbrScaler * self)
{
  guint chan_id;
  GstMemory *mem = NULL;
  GstIvasXAbrScalerPad *srcpad = NULL;

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    GstBuffer *outbuf = NULL;
    GstFlowReturn fret;
    GstVideoMeta *vmeta;
    guint64 phy_addr = -1;

    srcpad = gst_ivas_xabrscaler_srcpad_at_index (self, chan_id);

    fret = gst_buffer_pool_acquire_buffer (srcpad->pool, &outbuf, NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (srcpad, "failed to allocate buffer from pool %p",
          srcpad->pool);
      goto error;
    }
    GST_LOG_OBJECT (srcpad, "acquired buffer %p from pool", outbuf);

    self->priv->outbufs[chan_id] = outbuf;
    mem = gst_buffer_get_memory (outbuf, 0);
    if (mem == NULL) {
      GST_ERROR_OBJECT (srcpad,
          "chan-%d : failed to get memory from output buffer", chan_id);
      goto error;
    }
    /* No need to check whether memory is from device or not here.
     * Because, we have made sure memory is allocated from device in decide_allocation
     */
    if (gst_is_ivas_memory (mem)) {
      phy_addr = gst_ivas_allocator_get_paddr (mem);
    } else if (gst_is_dmabuf_memory (mem)) {
      guint bo = NULLBO;
      gint dma_fd = -1;
      struct xclBOProperties p;

      dma_fd = gst_dmabuf_memory_get_fd (mem);
      if (dma_fd < 0) {
        GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
        goto error;
      }

      /* dmabuf but not from xrt */
      bo = xclImportBO (self->priv->xcl_handle, dma_fd, 0);
      if (bo == NULLBO) {
        GST_WARNING_OBJECT (self,
            "failed to get XRT BO...fall back to copy input");
      }

      GST_INFO_OBJECT (self, "received dma fd %d and its xrt BO = %u", dma_fd,
          bo);

      if (!xclGetBOProperties (self->priv->xcl_handle, bo, &p)) {
        phy_addr = p.paddr;
      } else {
        GST_WARNING_OBJECT (self,
            "failed to get physical address...fall back to copy input");
      }

      if (bo != NULLBO)
        xclFreeBO(self->priv->xcl_handle, bo);
    }

    vmeta = gst_buffer_get_video_meta (outbuf);
    if (vmeta == NULL) {
      GST_ERROR_OBJECT (srcpad, "video meta not present in buffer");
      goto error;
    }

    self->priv->phy_out[chan_id] = phy_addr;
    self->priv->out_offset[chan_id] = 0;
    if (vmeta->n_planes == 2)   /* Supports 2nd plane only */
      self->priv->out_offset[chan_id] = phy_addr + vmeta->offset[1];

    gst_memory_unref (mem);
  }

  return TRUE;

error:
  if (mem)
    gst_memory_unref (mem);

  return FALSE;
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

static void
ivas_xabrscaler_reg_write (GstIvasXAbrScaler * self, void *src, size_t size,
    size_t offset)
{
  unsigned int *src_array = (unsigned int *) src;
  size_t cur_min = offset;
  size_t cur_max = offset + size;
  unsigned int entries = size / sizeof (uint32_t);
  unsigned int start = offset / sizeof (uint32_t), i;
  struct ert_start_kernel_cmd *ert_cmd =
      (struct ert_start_kernel_cmd *) (self->priv->ert_cmd_buf->user_ptr);

  for (i = 0; i < entries; i++)
    ert_cmd->data[start + i] = src_array[i];


  if (cur_max > self->priv->max_offset)
    self->priv->max_offset = cur_max;

  if (cur_min < self->priv->min_offset)
    self->priv->min_offset = cur_min;
}

static bool
xlnx_multiscaler_descriptor_create (GstIvasXAbrScaler * self)
{
  GstVideoMeta *meta_out;
  MULTI_SCALER_DESC_STRUCT *msPtr;
  guint chan_id;
  GstIvasXAbrScalerPrivate *priv = self->priv;

  /* First descriptor from input caps */
  uint64_t phy_in_0 = priv->phy_in_0;
  uint64_t phy_in_1 = priv->phy_in_1;
  uint32_t width = priv->in_vinfo->width;
  uint32_t height = priv->in_vinfo->height;
#ifdef ENABLE_PPE_SUPPORT
  uint32_t val;
#endif
  uint32_t msc_inPixelFmt =
      xlnx_multiscaler_colorformat (priv->in_vinfo->finfo->format);
  uint32_t stride = xlnx_multiscaler_stride_align (self->priv->meta_in_stride,
      self->ppc * 64);

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {

    msPtr = (MULTI_SCALER_DESC_STRUCT *) (priv->msPtr[chan_id].user_ptr);
    msPtr->msc_srcImgBuf0 = (uint64_t) phy_in_0;
    msPtr->msc_srcImgBuf1 = (uint64_t) phy_in_1;        /* plane 2 */
    msPtr->msc_srcImgBuf2 = (uint64_t) 0;

    msPtr->msc_dstImgBuf0 = (uint64_t) priv->phy_out[chan_id];
    msPtr->msc_dstImgBuf1 = (uint64_t) priv->out_offset[chan_id];       /* plane 2 */
    msPtr->msc_dstImgBuf2 = (uint64_t) 0;

    msPtr->msc_widthIn = width; /* 4 settings from pads */
    msPtr->msc_heightIn = height;
    msPtr->msc_inPixelFmt = msc_inPixelFmt;
    msPtr->msc_strideIn = stride;
    meta_out = gst_buffer_get_video_meta (priv->outbufs[chan_id]);
    msPtr->msc_widthOut = meta_out->width;
    msPtr->msc_heightOut = meta_out->height;
#ifdef ENABLE_PPE_SUPPORT
    msPtr->msc_alpha_b = self->alpha_b;
    msPtr->msc_alpha_g = self->alpha_g;
    msPtr->msc_alpha_r = self->alpha_r;
    val = (self->beta_b * (1 << 16));
    msPtr->msc_beta_b = val;
    val = (self->beta_g * (1 << 16));
    msPtr->msc_beta_g = val;
    val = (self->beta_r * (1 << 16));
    msPtr->msc_beta_r = val;
#endif
    msPtr->msc_lineRate =
        (uint32_t) ((float) ((msPtr->msc_heightIn * STEP_PRECISION) +
            ((msPtr->msc_heightOut) / 2)) / (float) msPtr->msc_heightOut);
    msPtr->msc_pixelRate =
        (uint32_t) ((float) (((msPtr->msc_widthIn) * STEP_PRECISION) +
            ((msPtr->msc_widthOut) / 2)) / (float) msPtr->msc_widthOut);
    msPtr->msc_outPixelFmt = xlnx_multiscaler_colorformat (meta_out->format);

    msPtr->msc_strideOut =
        xlnx_multiscaler_stride_align (*(meta_out->stride), self->ppc * 64);

    msPtr->msc_blkmm_hfltCoeff = 0;
    msPtr->msc_blkmm_vfltCoeff = 0;
    if (chan_id == (self->num_request_pads - 1))
      msPtr->msc_nxtaddr = 0;
    else
      msPtr->msc_nxtaddr = priv->msPtr[chan_id + 1].phy_addr;
    msPtr->msc_blkmm_hfltCoeff = priv->Hcoff[chan_id].phy_addr;
    msPtr->msc_blkmm_vfltCoeff = priv->Vcoff[chan_id].phy_addr;

    /* set the output as input for next descripto if any */
    phy_in_0 = msPtr->msc_dstImgBuf0;
    phy_in_1 = msPtr->msc_dstImgBuf1;
    width = msPtr->msc_widthOut;
    height = msPtr->msc_heightOut;
    msc_inPixelFmt = msPtr->msc_outPixelFmt;
    stride = msPtr->msc_strideOut;
  }
  priv->is_coeff = false;
  return TRUE;
}

static gboolean
ivas_xabrscaler_process (GstIvasXAbrScaler * self)
{
  GstIvasXAbrScalerPrivate *priv = self->priv;
  struct ert_start_kernel_cmd *ert_cmd =
      (struct ert_start_kernel_cmd *) (priv->ert_cmd_buf->user_ptr);
  int iret;
  uint32_t value = 0, chan_id = 0;
  uint64_t desc_addr = 0;
  uint32_t payload_offset = 0;
  bool ret;
  GstMemory *mem = NULL;

  /* set descriptor */
  ret = xlnx_multiscaler_descriptor_create (self);
  if (!ret)
    return FALSE;
#ifdef XLNX_PCIe_PLATFORM
  ret = xlnx_abr_desc_syncBO (self);
  if (!ret)
    return FALSE;
#endif

  /*to support 128 cu index we need 3 extra cu mask in ert command buffer */
  ert_cmd->extra_cu_masks = 3;
  payload_offset = ert_cmd->extra_cu_masks * 4; /* 4 bytes per mask */

  /* prgram registers */
  value = self->num_request_pads;
  ivas_xabrscaler_reg_write (self, &value, sizeof (value),
      (XV_MULTI_SCALER_CTRL_ADDR_HWREG_NUM_OUTS_DATA + payload_offset));
  desc_addr = priv->msPtr[0].phy_addr;
  ivas_xabrscaler_reg_write (self, &desc_addr, sizeof (desc_addr),
      (XV_MULTI_SCALER_CTRL_ADDR_HWREG_START_ADDR_DATA + payload_offset));

  /* start ert command */
  ert_cmd->state = ERT_CMD_STATE_NEW;
  ert_cmd->opcode = ERT_START_CU;

  if ( priv->cu_index > 31) {
     ert_cmd->cu_mask = 0;
     if (priv->cu_index > 63) {
       ert_cmd->data[0] = 0;
       if (priv->cu_index > 96) {
         ert_cmd->data[1] = 0;
         ert_cmd->data[2] = (1 << (priv->cu_index - 96));
       } else {
         ert_cmd->data[1] = (1 << (priv->cu_index - 64));
         ert_cmd->data[2] = 0;
       }
    } else {
       ert_cmd->data[0] = (1 << (priv->cu_index - 32));
   }
 } else {
      ert_cmd->cu_mask = (1 << priv->cu_index);
      ert_cmd->data[0] = 0;
      ert_cmd->data[1] = 0;
      ert_cmd->data[2] = 0;
 }

 ert_cmd->count = (self->priv->max_offset >> 2) + 1;
 iret = xclExecBuf (priv->xcl_handle, priv->ert_cmd_buf->bo);

 if (iret) {
    GST_ERROR_OBJECT (self, "failed to execute command %d", iret);
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, NULL,
        ("failed to issue execute command. reason : %s", strerror (errno)));
    return FALSE;
 }

  do {
    iret = xclExecWait (priv->xcl_handle, MULTI_SCALER_TIMEOUT);
    if (iret < 0) {
      GST_ERROR_OBJECT (self, "ExecWait ret = %d. reason : %s", iret,
          strerror (errno));
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED, NULL,
          ("error in processing a frame. reason : %s", strerror (errno)));
      return FALSE;
    } else if (!iret) {
      GST_ERROR_OBJECT (self, "timeout on execwait");
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED, NULL,
          ("timeout occured in processing a frame. reason : %s", strerror (errno)));
      return FALSE;
    }
  } while (ert_cmd->state != ERT_CMD_STATE_COMPLETED);

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    mem = gst_buffer_get_memory (priv->outbufs[chan_id], 0);
    if (mem == NULL) {
      GST_ERROR_OBJECT (self,
          "chan-%d : failed to get memory from output buffer", chan_id);
      return FALSE;
    }
#ifdef XLNX_PCIe_PLATFORM
    gst_ivas_memory_set_sync_flag (mem, IVAS_SYNC_FROM_DEVICE);
#endif
    gst_memory_unref (mem);
  }
  return TRUE;
}

static void
gst_ivas_xabrscaler_finalize (GObject * object)
{
  GstIvasXAbrScaler *self = GST_IVAS_XABRSCALER (object);

  g_hash_table_unref (self->pad_indexes);
  gst_video_info_free (self->priv->in_vinfo);

  g_free (self->kern_name);
  g_free (self->xclbin_path);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ivas_xabrscaler_class_init (GstIvasXAbrScalerClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_ivas_xabrscaler_debug, "ivas_xabrscaler",
      0, "Xilinx's Multiscaler 2.0 plugin");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_ivas_xabrscaler_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_ivas_xabrscaler_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_ivas_xabrscaler_finalize);

  gst_element_class_set_details_simple (gstelement_class,
      "Xilinx XlnxAbrScaler Scaler plugin",
      "Multi scaler 2.0 xrt plugin for vitis kernels",
      "Multi Scaler 2.0 plugin using XRT", "Xilinx Inc <www.xilinx.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &src_request_template);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_ivas_xabrscaler_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_ivas_xabrscaler_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_ivas_xabrscaler_release_pad);

  g_object_class_install_property (gobject_class, PROP_XCLBIN_LOCATION,
      g_param_spec_string ("xclbin-location", "xclbin file location",
          "Location of the xclbin to program devices", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));
  g_object_class_install_property (gobject_class, PROP_KERN_NAME,
      g_param_spec_string ("kernel-name", "kernel name and instance",
          "String defining the kernel name and instance as mentioned in xclbin",
          NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));
#ifdef XLNX_PCIe_PLATFORM
  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("dev-idx", "Device index",
          "Device index", -1, 31, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

#ifdef ENABLE_XRM_SUPPORT
  g_object_class_install_property (gobject_class, PROP_RESERVATION_ID,
      g_param_spec_uint64 ("reservation-id", "XRM reservation id",
          "Resource Pool Reservation id", 0, G_MAXUINT64, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
#endif

  g_object_class_install_property (gobject_class, PROP_PPC,
      g_param_spec_int ("ppc", "pixel per clock",
          "Pixel per clock configured in Multiscaler kernel", 1, 4, 2,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SCALE_MODE,
      g_param_spec_int ("scale-mode", "Scaling Mode",
          "Scale Mode configured in Multiscaler kernel. 	\
	0: BILINEAR \n 1: BICUBIC \n2: POLYPHASE", 0, 2, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NUM_TAPS,
      g_param_spec_enum ("num-taps", "Number filter taps",
          "Number of filter taps to be used for scaling",
          IVAS_XABRSCALER_NUM_TAPS_TYPE, IVAS_XABRSCALER_DEFAULT_NUM_TAPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_COEF_LOADING_TYPE,
      g_param_spec_enum ("coef-load-type", "Coefficients loading type",
          "coefficients loading type for scaling",
          IVAS_XABRSCALER_COEF_LOAD_TYPE, IVAS_XABRSCALER_DEFAULT_COEF_LOAD_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_AVOID_OUTPUT_COPY,
      g_param_spec_boolean ("avoid-output-copy",
          "Avoid output frames copy",
          "Avoid output frames copy on all source pads even when downstream"
          " does not support GstVideoMeta metadata",
          IVAS_XABRSCALER_AVOID_OUTPUT_COPY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

#ifdef ENABLE_PPE_SUPPORT
  g_object_class_install_property (gobject_class, PROP_ALPHA_R,
      g_param_spec_float ("alpha-r",
          "PreProcessing parameter alpha red channel value",
          "PreProcessing parameter alpha red channel value", 0, 128,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ALPHA_G,
      g_param_spec_float ("alpha-g",
          "PreProcessing parameter alpha green channel value",
          "PreProcessing parameter alpha green channel value", 0, 128,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ALPHA_B,
      g_param_spec_float ("alpha-b",
          "PreProcessing parameter alpha blue channel value",
          "PreProcessing parameter alpha blue channel value", 0, 128,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BETA_R,
      g_param_spec_float ("beta-r",
          "PreProcessing parameter beta red channel value",
          "PreProcessing parameter beta red channel value", 0, 1,
          1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BETA_G,
      g_param_spec_float ("beta-g",
          "PreProcessing parameter beta green channel value",
          "PreProcessing parameter beta green channel value", 0, 1,
          1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BETA_B,
      g_param_spec_float ("beta-b",
          "PreProcessing parameter beta blue channel value",
          "PreProcessing parameter beta blue channel value", 0, 1,
          1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif
}

static void
gst_ivas_xabrscaler_init (GstIvasXAbrScaler * self)
{
  gint idx;

  self->priv = GST_IVAS_XABRSCALER_PRIVATE (self);

  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ivas_xabrscaler_sink_event));
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ivas_xabrscaler_chain));
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ivas_xabrscaler_sink_query));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->num_request_pads = 0;
  self->pad_indexes = g_hash_table_new (NULL, NULL);
  self->srcpads = NULL;
  self->out_stride_align = 1;
  self->out_elevation_align = 1;
  self->num_taps = IVAS_XABRSCALER_DEFAULT_NUM_TAPS;
  self->coef_load_type = IVAS_XABRSCALER_DEFAULT_COEF_LOAD_TYPE;
  self->avoid_output_copy = IVAS_XABRSCALER_AVOID_OUTPUT_COPY_DEFAULT;
#ifdef ENABLE_PPE_SUPPORT
  self->alpha_r = 0;
  self->alpha_g = 0;
  self->alpha_b = 0;
  self->beta_r = 1;
  self->beta_g = 1;
  self->beta_b = 1;
#endif
  self->dev_index = DEFAULT_DEVICE_INDEX;
  self->ppc = 2;

#ifdef ENABLE_XRM_SUPPORT
  self->priv->xrm_ctx = NULL;
  self->priv->cu_resource = NULL;
  self->priv->cur_load = 0;
  self->priv->reservation_id = 0;
  self->priv->has_error = FALSE;
#endif
  self->priv->in_vinfo = gst_video_info_new ();
  self->priv->validate_import = TRUE;
  self->priv->input_pool = NULL;
  self->priv->ert_cmd_buf = NULL;
  gst_video_info_init (self->priv->in_vinfo);

  for (idx = 0; idx < MAX_CHANNELS; idx++) {
    self->priv->need_copy[idx] = TRUE;
  }
}

static GstPad *
gst_ivas_xabrscaler_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name_templ, const GstCaps * caps)
{
  GstIvasXAbrScaler *self = GST_IVAS_XABRSCALER (element);
  gchar *name = NULL;
  GstPad *srcpad;
  guint index = 0;

  GST_DEBUG_OBJECT (self, "requesting pad");

  GST_OBJECT_LOCK (self);

  if (GST_STATE (self) > GST_STATE_NULL) {
    GST_ERROR_OBJECT (self, "adding pads is supported only when state is NULL");
    GST_OBJECT_UNLOCK (self);
    return NULL;
  }

  if (self->num_request_pads == MAX_CHANNELS) {
    GST_ERROR_OBJECT (self, "reached maximum supported channels");
    GST_OBJECT_UNLOCK (self);
    return NULL;
  }

  if (name_templ && sscanf (name_templ, "src_%u", &index) == 1) {
    GST_LOG_OBJECT (element, "name: %s (index %d)", name_templ, index);
    if (g_hash_table_contains (self->pad_indexes, GUINT_TO_POINTER (index))) {
      GST_ERROR_OBJECT (element, "pad name %s is not unique", name_templ);
      GST_OBJECT_UNLOCK (self);
      return NULL;
    }
  } else {
    if (name_templ) {
      GST_ERROR_OBJECT (element, "incorrect padname : %s", name_templ);
      GST_OBJECT_UNLOCK (self);
      return NULL;
    }
  }

  g_hash_table_insert (self->pad_indexes, GUINT_TO_POINTER (index), NULL);

  name = g_strdup_printf ("src_%u", index);

  srcpad = GST_PAD_CAST (g_object_new (GST_TYPE_IVAS_XABRSCALER_PAD,
          "name", name, "direction", templ->direction, "template", templ,
          NULL));
  GST_IVAS_XABRSCALER_PAD_CAST (srcpad)->index = index;
  g_free (name);

  GST_IVAS_XABRSCALER_PAD_CAST (srcpad)->in_vinfo = gst_video_info_new ();
  gst_video_info_init (GST_IVAS_XABRSCALER_PAD_CAST (srcpad)->in_vinfo);

  GST_IVAS_XABRSCALER_PAD_CAST (srcpad)->out_vinfo = gst_video_info_new ();
  gst_video_info_init (GST_IVAS_XABRSCALER_PAD_CAST (srcpad)->out_vinfo);

  self->srcpads = g_list_append (self->srcpads,
      GST_IVAS_XABRSCALER_PAD_CAST (srcpad));
  self->num_request_pads++;

  GST_OBJECT_UNLOCK (self);

  gst_element_add_pad (GST_ELEMENT_CAST (self), srcpad);

  return srcpad;
}

static void
gst_ivas_xabrscaler_release_pad (GstElement * element, GstPad * pad)
{
  GstIvasXAbrScaler *self;
  GstIvasXAbrScalerPad *srcpad;
  guint index;
  GList *lsrc = NULL;

  self = GST_IVAS_XABRSCALER (element);
  srcpad = GST_IVAS_XABRSCALER_PAD_CAST (pad);

  GST_OBJECT_LOCK (self);

  if (GST_STATE (self) > GST_STATE_NULL) {
    GST_ERROR_OBJECT (self, "adding pads is supported only when state is NULL");
    GST_OBJECT_UNLOCK (self);
    return;
  }

  lsrc = g_list_find (self->srcpads, srcpad);
  if (!lsrc) {
    GST_ERROR_OBJECT (self, "could not find pad to release");
    GST_OBJECT_UNLOCK (self);
    return;
  }

  gst_video_info_free (srcpad->in_vinfo);
  gst_video_info_free (srcpad->out_vinfo);

  self->srcpads = g_list_remove (self->srcpads, srcpad);
  index = srcpad->index;
  GST_DEBUG_OBJECT (self, "releasing pad with index = %d", index);

  GST_OBJECT_UNLOCK (self);

  gst_object_ref (pad);
  gst_element_remove_pad (GST_ELEMENT_CAST (self), pad);

  gst_pad_set_active (pad, FALSE);

  gst_object_unref (pad);

  self->num_request_pads--;

  GST_OBJECT_LOCK (self);
  g_hash_table_remove (self->pad_indexes, GUINT_TO_POINTER (index));
  GST_OBJECT_UNLOCK (self);
}

static void
gst_ivas_xabrscaler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIvasXAbrScaler *self = GST_IVAS_XABRSCALER (object);

  switch (prop_id) {
    case PROP_KERN_NAME:
      if (GST_STATE (self) != GST_STATE_NULL) {
        g_warning
            ("can't set kern_name path when instance is NOT in NULL state");
        return;
      }
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
    case PROP_PPC:
      self->ppc = g_value_get_int (value);
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
    case PROP_AVOID_OUTPUT_COPY:
      self->avoid_output_copy = g_value_get_boolean (value);
      break;
#ifdef ENABLE_PPE_SUPPORT
    case PROP_ALPHA_R:
      self->alpha_r = g_value_get_float (value);
      break;
    case PROP_ALPHA_G:
      self->alpha_g = g_value_get_float (value);
      break;
    case PROP_ALPHA_B:
      self->alpha_b = g_value_get_float (value);
      break;
    case PROP_BETA_R:
      self->beta_r = g_value_get_float (value);
      break;
    case PROP_BETA_G:
      self->beta_g = g_value_get_float (value);
      break;
    case PROP_BETA_B:
      self->beta_b = g_value_get_float (value);
      break;
#endif
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

static void
gst_ivas_xabrscaler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstIvasXAbrScaler *self = GST_IVAS_XABRSCALER (object);

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
    case PROP_PPC:
      g_value_set_int (value, self->ppc);
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
    case PROP_AVOID_OUTPUT_COPY:
      g_value_set_boolean (value, self->avoid_output_copy);
      break;
#ifdef ENABLE_PPE_SUPPORT
    case PROP_ALPHA_R:
      g_value_set_float (value, self->alpha_r);
      break;
    case PROP_ALPHA_G:
      g_value_set_float (value, self->alpha_g);
      break;
    case PROP_ALPHA_B:
      g_value_set_float (value, self->alpha_b);
      break;
    case PROP_BETA_R:
      g_value_set_float (value, self->beta_r);
      break;
    case PROP_BETA_G:
      g_value_set_float (value, self->beta_g);
      break;
    case PROP_BETA_B:
      g_value_set_float (value, self->beta_b);
      break;
#endif
#ifdef ENABLE_XRM_SUPPORT
    case PROP_RESERVATION_ID:
      g_value_set_uint64 (value, self->priv->reservation_id);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_ivas_xabrscaler_change_state (GstElement * element,
    GstStateChange transition)
{
  GstIvasXAbrScaler *self = GST_IVAS_XABRSCALER (element);
  GstStateChangeReturn ret;
  guint idx = 0;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED: {

#ifdef ENABLE_XRM_SUPPORT
      self->priv->xrm_ctx = (xrmContext *) xrmCreateContext (XRM_API_VERSION_1);
      if (!self->priv->xrm_ctx) {
        GST_ERROR_OBJECT (self, "create XRM context failed");
        return FALSE;
      }
      GST_INFO_OBJECT (self, "successfully created xrm context");
      self->priv->has_error = FALSE;
#endif
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:

      for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
        GstIvasXAbrScalerPad *srcpad = gst_ivas_xabrscaler_srcpad_at_index (self, idx);
        if (srcpad->pool && gst_buffer_pool_is_active (srcpad->pool)) {
          if (!gst_buffer_pool_set_active (srcpad->pool, FALSE))
            GST_ERROR_OBJECT (self, "failed to deactivate pool %" GST_PTR_FORMAT,
              srcpad->pool);
          gst_clear_object (&srcpad->pool);
          srcpad->pool = NULL;
        }
      }

      if (self->priv->input_pool && gst_buffer_pool_is_active (self->priv->input_pool)) {
        if (!gst_buffer_pool_set_active (self->priv->input_pool, FALSE))
          GST_ERROR_OBJECT (self, "failed to deactivate pool %" GST_PTR_FORMAT,
              self->priv->input_pool);
        gst_clear_object (&self->priv->input_pool);
        self->priv->input_pool = NULL;
      }

      ivas_xabrscaler_free_internal_buffers (self);

      if (self->priv->ert_cmd_buf) {
        free_xrt_buffer (self->priv->xcl_handle, self->priv->ert_cmd_buf);
        free (self->priv->ert_cmd_buf);
        self->priv->ert_cmd_buf = NULL;
      }

      ivas_xabrscaler_destroy_context (self);
      break;
    default:
      break;
  }

  return ret;
}

static GstCaps *
gst_ivas_xabrscaler_fixate_caps (GstIvasXAbrScaler * self,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = G_VALUE_INIT, tpar = G_VALUE_INIT;

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);

  GST_DEBUG_OBJECT (self, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  {
    const gchar *in_format;

    in_format = gst_structure_get_string (ins, "format");
    if (in_format) {
      /* Try to set output format for pass through */
      gst_structure_fixate_field_string (outs, "format", in_format);
    }
  }

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  /* If we're fixating from the sinkpad we always set the PAR and
   * assume that missing PAR on the sinkpad means 1/1 and
   * missing PAR on the srcpad means undefined
   */
  if (direction == GST_PAD_SINK) {
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION_RANGE);
      gst_value_set_fraction_range_full (&tpar, 1, G_MAXINT, G_MAXINT, 1);
      to_par = &tpar;
    }
  } else {
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&tpar, 1, 1);
      to_par = &tpar;

      gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
          NULL);
    }
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
  }

  /* we have both PAR but they might not be fixated */
  {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint w = 0, h = 0;
    gint from_dar_n, from_dar_d;
    gint num, den;

    /* from_par should be fixed */
    g_return_val_if_fail (gst_value_is_fixed (from_par), othercaps);

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    gst_structure_get_int (outs, "width", &w);
    gst_structure_get_int (outs, "height", &h);

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (w && h) {
      guint n, d;

      GST_DEBUG_OBJECT (self, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        if (gst_video_calculate_display_ratio (&n, &d, from_w, from_h,
                from_par_n, from_par_d, w, h)) {
          GST_DEBUG_OBJECT (self, "fixating to_par to %dx%d", n, d);
          if (gst_structure_has_field (outs, "pixel-aspect-ratio"))
            gst_structure_fixate_field_nearest_fraction (outs,
                "pixel-aspect-ratio", n, d);
          else if (n != d)
            gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                n, d, NULL);
        }
      }
      goto done;
    }

    /* Calculate input DAR */
    if (!gst_util_fraction_multiply (from_w, from_h, from_par_n, from_par_d,
            &from_dar_n, &from_dar_d)) {
      GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      goto done;
    }

    GST_DEBUG_OBJECT (self, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      GstStructure *tmp;
      gint set_w, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (self, "height is fixed (%d)", h);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the width that is nearest to the
       * width with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (self, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        w = (guint) gst_util_uint64_scale_int (h, num, den);
        gst_structure_fixate_field_nearest_int (outs, "width", w);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input width */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "width", G_TYPE_INT, set_w,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the width to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int (h, num, den);
      gst_structure_fixate_field_nearest_int (outs, "width", w);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (w) {
      GstStructure *tmp;
      gint set_h, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (self, "width is fixed (%d)", w);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the height that is nearest to the
       * height with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (self, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        h = (guint) gst_util_uint64_scale_int (w, den, num);
        gst_structure_fixate_field_nearest_int (outs, "height", h);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input height */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }
      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "height", G_TYPE_INT, set_h,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the height to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      h = (guint) gst_util_uint64_scale_int (w, den, num);
      gst_structure_fixate_field_nearest_int (outs, "height", h);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (gst_value_is_fixed (to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;

      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      GST_DEBUG_OBJECT (self, "PAR is fixed %d/%d", to_par_n, to_par_d);

      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
              to_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      /* Try to keep the input height (because of interlacing) */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      w = (guint) gst_util_uint64_scale_int (set_h, num, den);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &set_w);
      gst_structure_free (tmp);

      /* We kept the DAR and the height is nearest to the original height */
      if (set_w == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      f_h = set_h;
      f_w = set_w;

      /* If the former failed, try to keep the input width at least */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      h = (guint) gst_util_uint64_scale_int (set_w, den, num);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_free (tmp);

      /* We kept the DAR and the width is nearest to the original width */
      if (set_h == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      /* If all this failed, keep the height that was nearest to the orignal
       * height and the nearest possible width. This changes the DAR but
       * there's not much else to do here.
       */
      gst_structure_set (outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT,
          f_h, NULL);
      goto done;
    } else {
      GstStructure *tmp;
      gint set_h, set_w, set_par_n, set_par_d, tmp2;

      /* width, height and PAR are not fixed but passthrough is not possible */

      /* First try to keep the height and width as good as possible
       * and scale PAR */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);

        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* Otherwise try to scale width to keep the DAR with the set
       * PAR and height */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int (set_h, num, den);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, tmp2, "height",
            G_TYPE_INT, set_h, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* ... or try the same with the height */
      h = (guint) gst_util_uint64_scale_int (set_w, den, num);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, tmp2, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* If all fails we can't keep the DAR and take the nearest values
       * for everything from the first try */
      gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, NULL);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);
    }
  }

done:
  GST_DEBUG_OBJECT (self, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  if (from_par == &fpar)
    g_value_unset (&fpar);
  if (to_par == &tpar)
    g_value_unset (&tpar);

  /* fixate remaining fields */
  othercaps = gst_caps_fixate (othercaps);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, othercaps)) {
      gst_caps_replace (&othercaps, caps);
    }
  }

  return othercaps;
}

static GstCaps *
gst_ivas_xabrscaler_transform_caps (GstIvasXAbrScaler * self,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret;
  GstStructure *structure;
  GstCapsFeatures *features;
  gint i, n;

  GST_DEBUG_OBJECT (self,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  ret = gst_caps_new_empty ();
  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure (caps, i);
    features = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (ret, structure, features))
      continue;

    /* make copy */
    structure = gst_structure_copy (structure);

    /* If the features are non-sysmem we can only do passthrough */
    if (!gst_caps_features_is_any (features)
        && gst_caps_features_is_equal (features,
            GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY)) {
      gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

      gst_structure_remove_fields (structure, "format", "colorimetry",
          "chroma-site", NULL);
      /* if pixel aspect ratio, make a range of it */
      if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
        gst_structure_set (structure, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
      }
    }
    gst_caps_append_structure_full (ret, structure,
        gst_caps_features_copy (features));
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = intersection;
  }

  GST_DEBUG_OBJECT (self, "returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static GstCaps *
gst_ivas_xabrscaler_find_transform (GstIvasXAbrScaler * self, GstPad * pad,
    GstPad * otherpad, GstCaps * caps)
{
  GstPad *otherpeer;
  GstCaps *othercaps;
  gboolean is_fixed;

  /* caps must be fixed here, this is a programming error if it's not */
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);

  otherpeer = gst_pad_get_peer (otherpad);

  /* see how we can transform the input caps. We need to do this even for
   * passthrough because it might be possible that this element STAMP support
   * passthrough at all. */
  othercaps = gst_ivas_xabrscaler_transform_caps (self,
      GST_PAD_DIRECTION (pad), caps, NULL);

  /* The caps we can actually output is the intersection of the transformed
   * caps with the pad template for the pad */
  if (othercaps && !gst_caps_is_empty (othercaps)) {
    GstCaps *intersect, *templ_caps;

    templ_caps = gst_pad_get_pad_template_caps (otherpad);
    GST_DEBUG_OBJECT (self,
        "intersecting against padtemplate %" GST_PTR_FORMAT, templ_caps);

    intersect =
        gst_caps_intersect_full (othercaps, templ_caps,
        GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (othercaps);
    gst_caps_unref (templ_caps);
    othercaps = intersect;
  }

  /* check if transform is empty */
  if (!othercaps || gst_caps_is_empty (othercaps))
    goto no_transform;

  /* if the othercaps are not fixed, we need to fixate them, first attempt
   * is by attempting passthrough if the othercaps are a superset of caps. */
  /* FIXME. maybe the caps is not fixed because it has multiple structures of
   * fixed caps */
  is_fixed = gst_caps_is_fixed (othercaps);
  if (!is_fixed) {
    GST_DEBUG_OBJECT (self,
        "transform returned non fixed  %" GST_PTR_FORMAT, othercaps);

    /* Now let's see what the peer suggests based on our transformed caps */
    if (otherpeer) {
      GstCaps *peercaps, *intersection, *templ_caps;

      GST_DEBUG_OBJECT (self,
          "Checking peer caps with filter %" GST_PTR_FORMAT, othercaps);

      peercaps = gst_pad_query_caps (otherpeer, othercaps);
      GST_DEBUG_OBJECT (self, "Resulted in %" GST_PTR_FORMAT, peercaps);
      if (!gst_caps_is_empty (peercaps)) {
        templ_caps = gst_pad_get_pad_template_caps (otherpad);

        GST_DEBUG_OBJECT (self,
            "Intersecting with template caps %" GST_PTR_FORMAT, templ_caps);

        intersection =
            gst_caps_intersect_full (peercaps, templ_caps,
            GST_CAPS_INTERSECT_FIRST);
        GST_DEBUG_OBJECT (self, "Intersection: %" GST_PTR_FORMAT, intersection);
        gst_caps_unref (peercaps);
        gst_caps_unref (templ_caps);
        peercaps = intersection;

        GST_DEBUG_OBJECT (self,
            "Intersecting with transformed caps %" GST_PTR_FORMAT, othercaps);
        intersection =
            gst_caps_intersect_full (peercaps, othercaps,
            GST_CAPS_INTERSECT_FIRST);
        GST_DEBUG_OBJECT (self, "Intersection: %" GST_PTR_FORMAT, intersection);
        gst_caps_unref (peercaps);
        gst_caps_unref (othercaps);
        othercaps = intersection;
      } else {
        gst_caps_unref (othercaps);
        othercaps = peercaps;
      }

      is_fixed = gst_caps_is_fixed (othercaps);
    } else {
      goto no_transform_possible;
    }
  }
  if (gst_caps_is_empty (othercaps))
    goto no_transform_possible;

  GST_DEBUG ("have %s fixed caps %" GST_PTR_FORMAT, (is_fixed ? "" : "non-"),
      othercaps);

  /* second attempt at fixation, call the fixate vmethod */
  /* caps could be fixed but the subclass may want to add fields */
  GST_DEBUG_OBJECT (self, "calling fixate_caps for %" GST_PTR_FORMAT
      " using caps %" GST_PTR_FORMAT " on pad %s:%s", othercaps, caps,
      GST_DEBUG_PAD_NAME (otherpad));
  /* note that we pass the complete array of structures to the fixate
   * function, it needs to truncate itself */
  othercaps =
      gst_ivas_xabrscaler_fixate_caps (self, GST_PAD_DIRECTION (pad), caps,
      othercaps);
  is_fixed = gst_caps_is_fixed (othercaps);
  GST_DEBUG_OBJECT (self, "after fixating %" GST_PTR_FORMAT, othercaps);

  /* caps should be fixed now, if not we have to fail. */
  if (!is_fixed)
    goto could_not_fixate;

  /* and peer should accept */
  if (otherpeer && !gst_pad_query_accept_caps (otherpeer, othercaps))
    goto peer_no_accept;

  GST_DEBUG_OBJECT (self, "Input caps were %" GST_PTR_FORMAT
      ", and got final caps %" GST_PTR_FORMAT, caps, othercaps);

  if (otherpeer)
    gst_object_unref (otherpeer);

  return othercaps;

  /* ERRORS */
no_transform:
  {
    GST_DEBUG_OBJECT (self,
        "transform returned useless  %" GST_PTR_FORMAT, othercaps);
    goto error_cleanup;
  }
no_transform_possible:
  {
    GST_DEBUG_OBJECT (self,
        "transform could not transform %" GST_PTR_FORMAT
        " in anything we support", caps);
    goto error_cleanup;
  }
could_not_fixate:
  {
    GST_DEBUG_OBJECT (self, "FAILED to fixate %" GST_PTR_FORMAT, othercaps);
    goto error_cleanup;
  }
peer_no_accept:
  {
    GST_DEBUG_OBJECT (self, "FAILED to get peer of %" GST_PTR_FORMAT
        " to accept %" GST_PTR_FORMAT, otherpad, othercaps);
    goto error_cleanup;
  }
error_cleanup:
  {
    if (otherpeer)
      gst_object_unref (otherpeer);
    if (othercaps)
      gst_caps_unref (othercaps);
    return NULL;
  }
}

static gboolean
ivas_xabrscaler_decide_allocation (GstIvasXAbrScaler * self,
    GstIvasXAbrScalerPad * srcpad, GstQuery * query, GstCaps * outcaps)
{
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  guint size, min, max;
  gboolean update_allocator, update_pool, bret;
  GstStructure *config = NULL;
  GstVideoInfo out_vinfo;
  gint srcpadIdx = gst_ivas_xabrscaler_srcpad_get_index(self, srcpad);

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0) {
    /* try the allocator */
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    GST_DEBUG_OBJECT (srcpad, "has allocator %p", allocator);
    update_allocator = TRUE;
  } else {
    allocator = NULL;
    update_allocator = FALSE;
    gst_allocation_params_init (&params);
  }

  if (outcaps && !gst_video_info_from_caps (&out_vinfo, outcaps)) {
    GST_ERROR_OBJECT (srcpad, "failed to get video info from outcaps");
    goto error;
  }

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

  /* Check if the proposed pool is IVAS Buffer Pool and stride is aligned with (8 * ppc)
   * If otherwise, discard the pool. Will create a new one */
  if (pool) {
    GstVideoAlignment video_align = { 0, };
    guint padded_width = 0;
    guint stride = 0;

    config = gst_buffer_pool_get_config (pool);
    if (config && gst_buffer_pool_config_has_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
      gst_buffer_pool_config_get_video_alignment (config, &video_align);

      /* We have adding padding_right and padding_left in pixels.
       * We need to convert them to bytes for finding out the complete stride with alignment */
      padded_width =
          out_vinfo.width + video_align.padding_right +
          video_align.padding_left;
      stride = ivas_xabrscaler_get_stride (&out_vinfo, padded_width);

      if(!stride)
        return FALSE;
      gst_structure_free (config);
      config = NULL;
      if (stride % (8 * self->ppc)) {
        GST_WARNING_OBJECT (self, "Discarding the propsed pool, "
           "Alignment not matching with 8 * self->ppc");
        gst_object_unref (pool);
        pool = NULL;
        update_pool = FALSE;

        self->out_stride_align = 8 * self->ppc;
        self->out_elevation_align = 1;
	GST_DEBUG_OBJECT (self, "Going to allocate pool with stride_align %d and \
            elevation_align %d", self->out_stride_align, self->out_elevation_align);
      }
    } else {
      /* IVAS Buffer Pool but no alignment information.
       * Discard to create pool with scaler alignment requirements*/
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
    GST_DEBUG_OBJECT (srcpad, "pool deos not have the KMSPrimeExport option, \
				unref the pool and create sdx allocator");
  }
#endif

  if (pool && !GST_IS_IVAS_BUFFER_POOL (pool)) {
    /* create own pool */
    gst_object_unref (pool);
    pool = NULL;
    update_pool = FALSE;
  }

  if (allocator && (!GST_IS_IVAS_ALLOCATOR (allocator) ||
          gst_ivas_allocator_get_device_idx (allocator) != self->dev_index)) {
    GST_DEBUG_OBJECT (srcpad, "replace %" GST_PTR_FORMAT " to xrt allocator",
        allocator);
    gst_object_unref (allocator);
    gst_allocation_params_init (&params);
    allocator = NULL;
  }

  if (!allocator) {
    /* making sdx allocator for the HW mode without dmabuf */
    allocator = gst_ivas_allocator_new (self->dev_index, NEED_DMABUF);
    params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
    params.flags |= GST_IVAS_ALLOCATOR_FLAG_MEM_INIT;
    GST_INFO_OBJECT (srcpad, "creating new xrt allocator %" GST_PTR_FORMAT,
        allocator);
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
        gst_ivas_buffer_pool_new (self->out_stride_align,
        self->out_elevation_align);
    GST_INFO_OBJECT (srcpad, "created new pool %p %" GST_PTR_FORMAT, pool,
        pool);

    config = gst_buffer_pool_get_config (pool);
    gst_video_alignment_reset (&align);
    align.padding_top = 0;
    align.padding_left = 0;
    align.padding_right = ivas_xabrscaler_get_padding_right (self, &out_vinfo);
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
      GST_ERROR_OBJECT (srcpad, "failed configure pool");
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
      self->priv->need_copy[srcpadIdx] = FALSE;
      GST_INFO_OBJECT (srcpad, "no need to copy output frames");
    }
  } else {
    self->priv->need_copy[srcpadIdx] = FALSE;
    GST_INFO_OBJECT (srcpad, "Don't copy output frames");
  }

  GST_INFO_OBJECT (srcpad,
      "allocated pool %p with parameters : size %u, min_buffers = %u, max_buffers = %u",
      pool, size, min, max);

  if (allocator)
    gst_object_unref (allocator);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  srcpad->pool = pool;

  GST_DEBUG_OBJECT (srcpad,
      "done decide allocation with query %" GST_PTR_FORMAT, query);
  return TRUE;

error:
  if (allocator)
    gst_object_unref (allocator);
  if (pool)
    gst_object_unref (pool);
  return FALSE;
}

static gboolean
gst_ivas_xabrscaler_sink_setcaps (GstIvasXAbrScaler * self, GstPad * sinkpad,
    GstCaps * in_caps)
{
  GstIvasXAbrScalerPrivate *priv = self->priv;
  GstCaps *outcaps = NULL, *prev_incaps = NULL, *prev_outcaps = NULL;
  gboolean bret = TRUE;
  guint idx = 0;
#ifdef ENABLE_XRM_SUPPORT
  gint load = -1;
#endif
  GstIvasXAbrScalerPad *srcpad = NULL;
  GstCaps *incaps = gst_caps_copy (in_caps);
  GstQuery *query = NULL;

#ifdef ENABLE_XRM_SUPPORT
  if (priv->has_error)
    return FALSE;
#endif

  GST_DEBUG_OBJECT (self, "have new sink caps %p %" GST_PTR_FORMAT, incaps,
      incaps);

  self->priv->validate_import = TRUE;

  /* store sinkpad info */
  if (!gst_video_info_from_caps (self->priv->in_vinfo, in_caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from input caps");
    return FALSE;
  }

  prev_incaps = gst_pad_get_current_caps (self->sinkpad);

  for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
    srcpad = gst_ivas_xabrscaler_srcpad_at_index (self, idx);

    /* find best possible caps for the other pad */
    outcaps = gst_ivas_xabrscaler_find_transform (self, sinkpad,
        GST_PAD_CAST (srcpad), incaps);
    if (!outcaps || gst_caps_is_empty (outcaps))
      goto no_transform_possible;

    prev_outcaps = gst_pad_get_current_caps (GST_PAD_CAST (srcpad));

    bret = prev_incaps && prev_outcaps
        && gst_caps_is_equal (prev_incaps, incaps)
        && gst_caps_is_equal (prev_outcaps, outcaps);

    if (bret) {
      GST_DEBUG_OBJECT (self,
          "New caps equal to old ones: %" GST_PTR_FORMAT " -> %" GST_PTR_FORMAT,
          incaps, outcaps);
    } else {
      if (!prev_outcaps || !gst_caps_is_equal (outcaps, prev_outcaps)) {
        /* let downstream know about our caps */
        bret = gst_pad_set_caps (GST_PAD_CAST (srcpad), outcaps);
        if (!bret)
          goto failed_configure;
      }

      bret =
          ivas_xabrscaler_register_prep_write_with_caps (self, idx, incaps,
          outcaps);
      if (!bret)
        goto failed_configure;
    }

    gst_caps_unref (incaps);
    incaps = outcaps;

    if (prev_outcaps) {
      gst_caps_unref (prev_outcaps);
      prev_outcaps = NULL;
    }
  }
  gst_caps_unref (outcaps);
  outcaps = NULL;
#ifdef ENABLE_XRM_SUPPORT
  bret = ivas_xabrscaler_calculate_load (self, &load);
  if (!bret)
    goto failed_configure;

  priv->cur_load = load;

  /* create XRT context */
  bret = ivas_xabrscaler_create_context (self);
  if (!bret)
    goto failed_configure;
#else
  if (!priv->xcl_handle) {
    /* create XRT context */
    bret = ivas_xabrscaler_create_context (self);
    if (!bret)
      goto failed_configure;
  }
#endif

  if (!priv->ert_cmd_buf) { /* one time allocation memory */
    gint iret;

    priv->ert_cmd_buf = (xrt_buffer *) calloc (1, sizeof (xrt_buffer));
    if (priv->ert_cmd_buf == NULL) {
      GST_ERROR_OBJECT (self, "failed to allocate ert cmd memory");
      goto failed_configure;
    }

    /* allocate ert command buffer */
    iret = alloc_xrt_buffer (priv->xcl_handle, ERT_CMD_SIZE,
            XCL_BO_SHARED_VIRTUAL, XCL_BO_FLAGS_EXECBUF, priv->ert_cmd_buf);
    if (iret < 0) {
      GST_ERROR_OBJECT (self, "failed to allocate ert command buffer..");
      goto failed_configure;
    }

    /* allocate internal buffers */
    bret = ivas_xabrscaler_allocate_internal_buffers (self);
    if (!bret)
      goto failed_configure;

  }

  for (idx = 0; idx < g_list_length (self->srcpads); idx++) {

    srcpad = gst_ivas_xabrscaler_srcpad_at_index (self, idx);

    if (!srcpad->pool) {

      outcaps = gst_pad_get_current_caps ((GstPad *) srcpad);

      GST_DEBUG_OBJECT (self, "doing allocation query");
      query = gst_query_new_allocation (outcaps, TRUE);
      if (!gst_pad_peer_query (GST_PAD (srcpad), query)) {
        /* not a problem, just debug a little */
        GST_DEBUG_OBJECT (self, "peer ALLOCATION query failed");
      }

      bret = ivas_xabrscaler_decide_allocation (self, srcpad, query, outcaps);
      if (!bret)
        goto failed_configure;

      /* activate output buffer pool */
      if (!gst_buffer_pool_set_active (srcpad->pool, TRUE)) {
        GST_ERROR_OBJECT (srcpad, "failed to activate pool");
        goto failed_configure;
      }
    }

    if (self->num_taps == 12) {
      ivas_xabrscaler_prepare_coefficients_with_12tap (self, idx);
    } else {
      if (self->scale_mode == POLYPHASE) {
        float scale = (float) GST_VIDEO_INFO_HEIGHT (srcpad->in_vinfo)
            / (float) GST_VIDEO_INFO_HEIGHT (srcpad->out_vinfo);
        GST_INFO_OBJECT (self, "preparing coefficients with scaling ration %f and taps %d",
            scale, self->num_taps);
        xlnx_multiscaler_coff_fill (priv->Hcoff[idx].user_ptr,
            priv->Vcoff[idx].user_ptr, scale);
      }
    }

    if (query) {
      gst_query_unref (query);
      query = NULL;
    }
    if (outcaps) {
      gst_caps_unref (outcaps);
      outcaps = NULL;
    }
  }

#ifdef XLNX_PCIe_PLATFORM
  if (!xlnx_abr_coeff_syncBO (self))
    return FALSE;
#endif

done:
  if(query)
    gst_query_unref (query);
  if (outcaps)
    gst_caps_unref (outcaps);
  if (prev_incaps)
    gst_caps_unref (prev_incaps);
  if (prev_outcaps)
    gst_caps_unref (prev_outcaps);

  return bret;

  /* ERRORS */
no_transform_possible:
  {
    GST_ERROR_OBJECT (self,
        "transform could not transform %" GST_PTR_FORMAT
        " in anything we support", incaps);
    bret = FALSE;
#ifdef ENABLE_XRM_SUPPORT
    priv->has_error = TRUE;
#endif
    goto done;
  }
failed_configure:
  {
#ifdef ENABLE_XRM_SUPPORT
    priv->has_error = TRUE;
#endif
    GST_ERROR_OBJECT (self, "FAILED to configure incaps %" GST_PTR_FORMAT
        " and outcaps %" GST_PTR_FORMAT, incaps, outcaps);
    bret = FALSE;
    goto done;
  }
}

static gboolean
gst_ivas_xabrscaler_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstIvasXAbrScaler *self = GST_IVAS_XABRSCALER (parent);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (pad, "received event '%s' %p %" GST_PTR_FORMAT,
      gst_event_type_get_name (GST_EVENT_TYPE (event)), event, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_ivas_xabrscaler_sink_setcaps (self, self->sinkpad, caps);
      gst_event_unref (event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
ivas_xabrscaler_propose_allocation (GstIvasXAbrScaler * self, GstQuery * query)
{
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool;
  guint size;

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstStructure *structure;
    GstAllocator *allocator = NULL;
    GstAllocationParams params =
        { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0, 0 };

    if (gst_query_get_n_allocation_params (query) > 0) {
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    } else {
#ifdef ENABLE_XRM_SUPPORT
      if (!self->priv->cu_resource) {
        GST_ERROR_OBJECT (self, "scaler resource not yet allocated");
        return FALSE;
      }
#endif
      allocator = gst_ivas_allocator_new (self->dev_index, NEED_DMABUF);
      GST_INFO_OBJECT (self, "creating new xrt allocator %" GST_PTR_FORMAT,
          allocator);

      gst_query_add_allocation_param (query, allocator, &params);
    }

    pool = gst_video_buffer_pool_new ();
    GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

    structure = gst_buffer_pool_get_config (pool);

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
gst_ivas_xabrscaler_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstIvasXAbrScaler *self = GST_IVAS_XABRSCALER (parent);
  gboolean ret = TRUE;
  GstIvasXAbrScalerPad *srcpad = NULL;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:{
      ret = ivas_xabrscaler_propose_allocation (self, query);
      break;
    }
    case GST_QUERY_CAPS:{
      GstCaps *filter, *caps = NULL, *result = NULL;

      gst_query_parse_caps (query, &filter);

      // TODO: Query caps only going to src pad 0 and check for others
      if (!g_list_length (self->srcpads)) {
        GST_DEBUG_OBJECT (pad, "0 source pads in list");
        return FALSE;
      }

      srcpad = gst_ivas_xabrscaler_srcpad_at_index (self, 0);
      if (!srcpad) {
        GST_ERROR_OBJECT (pad, "source pads not available..");
        return FALSE;
      }

      caps = gst_pad_get_pad_template_caps (pad);

      if (filter) {
        GstCaps *tmp = caps;
        caps = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (tmp);
      }

      if (srcpad) {
        result = gst_pad_peer_query_caps (GST_PAD_CAST (srcpad), caps);
        result = gst_caps_make_writable (result);
        gst_caps_append (result, caps);

        GST_DEBUG_OBJECT (self, "Returning %s caps %" GST_PTR_FORMAT,
            GST_PAD_NAME (pad), result);

        gst_query_set_caps_result (query, result);
        gst_caps_unref (result);
      }
      break;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;

      gst_query_parse_accept_caps (query, &caps);

      gst_query_set_accept_caps_result (query, ret);
      /* return TRUE, we answered the query */
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

static GstFlowReturn
gst_ivas_xabrscaler_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
{
  GstIvasXAbrScaler *self = GST_IVAS_XABRSCALER (parent);
  GstFlowReturn fret = GST_FLOW_OK;
  guint chan_id = 0;
  gboolean bret = FALSE;

  bret = ivas_xabrscaler_prepare_input_buffer (self, &inbuf);
  if (!bret)
    goto error;

  bret = ivas_xabrscaler_prepare_output_buffer (self);
  if (!bret)
    goto error;

  bret = ivas_xabrscaler_process (self);
  if (!bret)
    goto error;


  /* pad push of each output buffer to respective srcpad */
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    GstBuffer *outbuf = self->priv->outbufs[chan_id];
    GstIvasXAbrScalerPad *srcpad =
        gst_ivas_xabrscaler_srcpad_at_index (self, chan_id);
    GstMeta *in_meta;

    gst_buffer_copy_into (outbuf, inbuf,
        (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS |
            GST_BUFFER_COPY_TIMESTAMPS), 0, -1);

    /* Scaling of input ivas metadata based on output resolution */
    in_meta = gst_buffer_get_meta (inbuf, gst_inference_meta_api_get_type ());

    if (in_meta) {
      GstVideoMetaTransform trans = { self->priv->in_vinfo, srcpad->out_vinfo };
      GQuark scale_quark = gst_video_meta_transform_scale_get_quark ();

      GST_DEBUG_OBJECT (srcpad, "attaching scaled inference metadata");
      in_meta->info->transform_func (outbuf, (GstMeta *) in_meta,
          inbuf, scale_quark, &trans);
    }

    if (self->priv->need_copy[chan_id]) {
      GstBuffer *new_outbuf;
      GstVideoFrame new_frame, out_frame;
      new_outbuf =
        gst_buffer_new_and_alloc (GST_VIDEO_INFO_SIZE (srcpad->out_vinfo));
      if (!new_outbuf) {
        GST_ERROR_OBJECT (srcpad, "failed to allocate output buffer");
        fret =  GST_FLOW_ERROR;
        goto error2;
      }

      gst_video_frame_map (&out_frame, srcpad->out_vinfo, outbuf, GST_MAP_READ);
      gst_video_frame_map (&new_frame, srcpad->out_vinfo, new_outbuf,
        GST_MAP_WRITE);
      GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, srcpad,
        "slow copy data from %p to %p", outbuf, new_outbuf);
      gst_video_frame_copy (&new_frame, &out_frame);
      gst_video_frame_unmap (&out_frame);
      gst_video_frame_unmap (&new_frame);

      gst_buffer_copy_into (new_outbuf, outbuf, GST_BUFFER_COPY_METADATA, 0, -1);
      gst_buffer_unref (outbuf);
      GST_LOG_OBJECT (srcpad,
        "pushing outbuf %p with pts = %" GST_TIME_FORMAT " dts = %"
        GST_TIME_FORMAT " duration = %" GST_TIME_FORMAT, new_outbuf,
        GST_TIME_ARGS (GST_BUFFER_PTS (new_outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DTS (new_outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (new_outbuf)));

      fret = gst_pad_push (GST_PAD_CAST (srcpad), new_outbuf);
      if (G_UNLIKELY (fret != GST_FLOW_OK)) {
        if (fret == GST_FLOW_EOS)
	  GST_DEBUG_OBJECT (self, "failed to push buffer. reason : %s",
          gst_flow_get_name (fret));
	else
          GST_ERROR_OBJECT (self, "failed to push buffer. reason : %s",
          gst_flow_get_name (fret));
        goto error2;
      }

    } else {
      GST_LOG_OBJECT (srcpad,
          "pushing outbuf %p with pts = %" GST_TIME_FORMAT " dts = %"
          GST_TIME_FORMAT " duration = %" GST_TIME_FORMAT, outbuf,
          GST_TIME_ARGS (GST_BUFFER_PTS (outbuf)),
          GST_TIME_ARGS (GST_BUFFER_DTS (outbuf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

      fret = gst_pad_push (GST_PAD_CAST (srcpad), outbuf);
      if (G_UNLIKELY (fret != GST_FLOW_OK)) {
        if (fret == GST_FLOW_EOS)
	  GST_DEBUG_OBJECT (self, "failed to push buffer. reason : %s",
          gst_flow_get_name (fret));
	else
          GST_ERROR_OBJECT (self, "failed to push buffer. reason : %s",
          gst_flow_get_name (fret));
        goto error2;
      }
    }
  }

  gst_buffer_unref (inbuf);
  return fret;

error:
error2:
  gst_buffer_unref (inbuf);
  return fret;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
ivas_xabrscaler_init (GstPlugin * ivas_xabrscaler)
{
  return gst_element_register (ivas_xabrscaler, "ivas_xabrscaler",
      GST_RANK_NONE, GST_TYPE_IVAS_XABRSCALER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ivas_xabrscaler,
    "Xilinx Multiscaler 2.0 plugin",
    ivas_xabrscaler_init, "0.1", "LGPL", "GStreamer xrt", "http://xilinx.com/")
