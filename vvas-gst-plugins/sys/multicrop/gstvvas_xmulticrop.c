/*
 * Copyright (C) 2022 Xilinx, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:
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

/**
 * The vvas_xmulticrop element reads RegionOfIntresetMeta and based on it
 * crops multiple sub-buffers from the main buffer, cropped sub-buffers are
 * attached in the same RegionOfIntrestMeta metadata.
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
#include <gst/vvas/gstinferencemeta.h>
#include <gst/vvas/gstvvashdrmeta.h>
#include <vvas/xrt_utils.h>
#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include "gstvvas_xmulticrop.h"
#include "multi_scaler_hw.h"

#ifdef XLNX_PCIe_PLATFORM
#define DEFAULT_DEVICE_INDEX -1
#define DEFAULT_KERNEL_NAME "scaler:scaler_1"
#define NEED_DMABUF 0
#else
#define DEFAULT_DEVICE_INDEX 0  /* on Embedded only one device i.e. device 0 */
#define DEFAULT_KERNEL_NAME "v_multi_scaler:{v_multi_scaler_1}"
#define NEED_DMABUF 1
#endif

#define MULTI_SCALER_TIMEOUT 1000       // 1 sec
#define ALIGN(size,align) (((size) + (align) - 1) & ~((align) - 1))
#define DIV_AND_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define VVAS_XMULTICROP_AVOID_OUTPUT_COPY_DEFAULT FALSE
#define VVAS_XMULTICROP_ENABLE_PIPELINE_DEFAULT FALSE
#define STOP_COMMAND ((gpointer)GINT_TO_POINTER (g_quark_from_string("STOP")))
#define VVAS_XMULTICROP_PPE_ON_MAIN_BUF_DEFAULT FALSE

#ifdef XLNX_PCIe_PLATFORM
#define WIDTH_ALIGN 256
#define HEIGHT_ALIGN 64
#else
#define WIDTH_ALIGN (8 * self->ppc)
#define HEIGHT_ALIGN 1
#endif

#define DEFAULT_CROP_PARAMS 0
#define MAX_SUBBUFFER_POOLS 10
#define MIN_SCALAR_WIDTH    64
#define MIN_SCALAR_HEIGHT   64

GST_DEBUG_CATEGORY_STATIC (gst_vvas_xmulticrop_debug_category);
#define GST_CAT_DEFAULT gst_vvas_xmulticrop_debug_category
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

static GstStateChangeReturn gst_vvas_xmulticrop_change_state
    (GstElement * element, GstStateChange transition);
static void gst_vvas_xmulticrop_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_vvas_xmulticrop_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_vvas_xmulticrop_finalize (GObject * object);
static GstCaps *gst_vvas_xmulticrop_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_vvas_xmulticrop_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_vvas_xmulticrop_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_vvas_xmulticrop_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static gboolean gst_vvas_xmulticrop_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_vvas_xmulticrop_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static GstFlowReturn gst_vvas_xmulticrop_generate_output (GstBaseTransform *
    trans, GstBuffer ** outbuf);
static GstFlowReturn gst_vvas_xmulticrop_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean vvas_xmulticrop_allocate_internal_buffers (GstVvasXMultiCrop *
    self, guint buffer_count);
static void xlnx_multiscaler_coff_fill (void *Hcoeff_BufAddr,
    void *Vcoeff_BufAddr, float scale);
static GstBufferPool *vvas_xmulticrop_allocate_buffer_pool (GstVvasXMultiCrop *
    self, guint width, guint height, GstVideoFormat format);

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
  PROP_AVOID_OUTPUT_COPY,
  PROP_ENABLE_PIPELINE,
#ifdef ENABLE_PPE_SUPPORT
  PROP_ALPHA_R,
  PROP_ALPHA_G,
  PROP_ALPHA_B,
  PROP_BETA_R,
  PROP_BETA_G,
  PROP_BETA_B,
#endif
  PROP_CROP_X,
  PROP_CROP_Y,
  PROP_CROP_WIDTH,
  PROP_CROP_HEIGHT,
  PROP_SUBBUFFER_WIDTH,
  PROP_SUBBUFFER_HEIGHT,
  PROP_SUBBUFFER_FORMAT,
  PROP_PPE_ON_MAIN_BUFFER,
};

typedef enum ColorDomain
{
  COLOR_DOMAIN_YUV,
  COLOR_DOMAIN_RGB,
  COLOR_DOMAIN_GRAY,
  COLOR_DOMAIN_UNKNOWN,
} ColorDomain;

struct _GstVvasXMultiCropPrivate
{
  gboolean is_internal_buf_allocated;
  GstVideoInfo *in_vinfo;
  GstVideoInfo *out_vinfo;
  uint32_t meta_in_stride;
  vvasKernelHandle kern_handle;
  vvasRunHandle run_handle;
  vvasDeviceHandle dev_handle;
  uuid_t xclbinId;
  xrt_buffer Hcoff[MAX_CHANNELS];       //0th for main buffer
  xrt_buffer Vcoff[MAX_CHANNELS];
  xrt_buffer msPtr[MAX_CHANNELS];
  guint internal_buf_counter;

  GstBuffer *outbuf;
  GstBuffer *sub_buf[MAX_CHANNELS - 1];
  guint sub_buffer_counter;

  /* physical address for input buffer */
  guint64 phy_in_0;
  guint64 phy_in_1;
  guint64 phy_in_2;
  /* physical address for output buffer */
  guint64 phy_out_0;
  guint64 phy_out_1;
  guint64 phy_out_2;

  GstBufferPool *input_pool;
  /* when user has set sub buffer(dynamically cropped buffer) output resolution,
   * we only need 1 buffer pool, hence index 0 will only be used, otherwise
   * all MAX_SUBBUFFER_POOLS pools will be created as and when
   * need arises.
   */
  GstBufferPool *subbuffer_pools[MAX_SUBBUFFER_POOLS];
  guint subbuffer_pool_size[MAX_SUBBUFFER_POOLS];

  gboolean validate_import;
  gboolean need_copy;
  GThread *input_copy_thread;
  GAsyncQueue *copy_inqueue;
  GAsyncQueue *copy_outqueue;
  gboolean is_first_frame;
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGBx, YUY2, r210, Y410, NV16, NV12, RGB, v308, I422_10LE, GRAY8, \
  NV12_10LE32, BGRx, GRAY10_LE32, BGRx, UYVY, BGR, RGBA, BGRA, I420, GBR}")));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGBx, YUY2, r210, Y410, NV16, NV12, RGB, v308, I422_10LE, GRAY8, \
  NV12_10LE32, BGRx, GRAY10_LE32, BGRx, UYVY, BGR, RGBA, BGRA, I420, GBR}")));


/* class initialization */
G_DEFINE_TYPE_WITH_PRIVATE (GstVvasXMultiCrop, gst_vvas_xmulticrop,
    GST_TYPE_BASE_TRANSFORM);
#define GST_VVAS_XMULTICROP_PRIVATE(self) (GstVvasXMultiCropPrivate *) \
                      (gst_vvas_xmulticrop_get_instance_private (self))

#ifdef XLNX_PCIe_PLATFORM       /* default taps for PCIe platform 12 */
#define VVAS_XMULTICROP_DEFAULT_NUM_TAPS 12
#define VVAS_XMULTICROP_DEFAULT_PPC 4
#define VVAS_XMULTICROP_SCALE_MODE 2
#else /* default taps for Embedded platform 6 */
#define VVAS_XMULTICROP_DEFAULT_NUM_TAPS 6
#define VVAS_XMULTICROP_DEFAULT_PPC 2
#define VVAS_XMULTICROP_SCALE_MODE 0
#endif

#define VVAS_XMULTICROP_NUM_TAPS_TYPE (vvas_xmulticrop_num_taps_type ())
#define VVAS_XMULTICROP_PPC_TYPE (vvas_xmulticrop_ppc_type ())
#define VVAS_XMULTICROP_SUBBUFFER_FORMAT_TYPE (vvas_xmulticrop_subbuffer_format_type ())

static guint
vvas_xmulticrop_get_stride (GstVideoFormat format, guint width)
{
  guint stride = 0;

  switch (format) {
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_GBR:
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
      GST_ERROR ("Not supporting %d yet", format);
      stride = 0;
      break;
  }
  return stride;
}

static guint
vvas_xmulticrop_get_padding_right (GstVvasXMultiCrop * self,
    GstVideoInfo * info)
{
  guint padding_pixels = -1;
  guint plane_stride = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  guint padding_bytes =
      ALIGN (plane_stride, self->out_stride_align) - plane_stride;

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_GBR:
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

static ColorDomain
vvas_xmulticrop_get_color_domain (const gchar * color_format)
{
  GstVideoFormat format;
  const GstVideoFormatInfo *format_info;
  ColorDomain color_domain = COLOR_DOMAIN_UNKNOWN;

  format = gst_video_format_from_string (color_format);
  format_info = gst_video_format_get_info (format);

  if (GST_VIDEO_FORMAT_INFO_IS_YUV (format_info)) {
    color_domain = COLOR_DOMAIN_YUV;
  } else if (GST_VIDEO_FORMAT_INFO_IS_RGB (format_info)) {
    color_domain = COLOR_DOMAIN_RGB;
  } else if (GST_VIDEO_FORMAT_INFO_IS_GRAY (format_info)) {
    color_domain = COLOR_DOMAIN_GRAY;
  }

  return color_domain;
}

static GType
vvas_xmulticrop_num_taps_type (void)
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
    num_tap = g_enum_register_static ("GstVvasXMultiCropNumTapsType", taps);
  }
  return num_tap;
}

static GType
vvas_xmulticrop_ppc_type (void)
{
  static GType num_ppc = 0;

  if (!num_ppc) {
    static const GEnumValue ppc[] = {
      {1, "1 Pixel Per Clock", "1"},
      {2, "2 Pixels Per Clock", "2"},
      {4, "4 Pixels Per Clock", "4"},
      {0, NULL, NULL}
    };
    num_ppc = g_enum_register_static ("GstVvasXMultiCropPPCType", ppc);
  }
  return num_ppc;
}

static GType
vvas_xmulticrop_subbuffer_format_type (void)
{
  static GType sub_buffer_fromat = 0;

  if (!sub_buffer_fromat) {
    static const GEnumValue subbuffer_format[] = {
      {0, "GST_VIDEO_FORMAT_UNKNOWN", "0"},
      {2, "GST_VIDEO_FORMAT_I420", "2"},
      {4, "GST_VIDEO_FORMAT_YUY2", "4"},
      {5, "GST_VIDEO_FORMAT_UYVY", "5"},
      {7, "GST_VIDEO_FORMAT_RGBx", "7"},
      {8, "GST_VIDEO_FORMAT_BGRx", "8"},
      {11, "GST_VIDEO_FORMAT_RGBA", "11"},
      {12, "GST_VIDEO_FORMAT_BGRA", "12"},
      {15, "GST_VIDEO_FORMAT_RGB", "15"},
      {16, "GST_VIDEO_FORMAT_BGR", "16"},
      {23, "GST_VIDEO_FORMAT_NV12", "23"},
      {25, "GST_VIDEO_FORMAT_GRAY8", "25"},
      {41, "GST_VIDEO_FORMAT_r210", "41"},
      {45, "GST_VIDEO_FORMAT_I422_10LE", "45"},
      {48, "GST_VIDEO_FORMAT_GBR", "48"},
      {51, "GST_VIDEO_FORMAT_NV16", "51"},
      {78, "GST_VIDEO_FORMAT_GRAY10_LE32", "78"},
      {79, "GST_VIDEO_FORMAT_NV12_10LE32", "79"},
      {83, "GST_VIDEO_FORMAT_Y410", "83"},
      {0, NULL, NULL}
    };
    sub_buffer_fromat =
        g_enum_register_static ("GstVvasXMultiCropSUBBufferFromatType",
        subbuffer_format);
  }
  return sub_buffer_fromat;
}

#ifdef XLNX_PCIe_PLATFORM
#define VVAS_XMULTICROP_DEFAULT_COEF_LOAD_TYPE COEF_AUTO_GENERATE
#else
#define VVAS_XMULTICROP_DEFAULT_COEF_LOAD_TYPE COEF_FIXED
#endif

#define VVAS_XMULTICROP_COEF_LOAD_TYPE (vvas_xmulticrop_coef_load_type ())

static GType
vvas_xmulticrop_coef_load_type (void)
{
  static GType load_type = 0;

  if (!load_type) {
    static const GEnumValue load_types[] = {
      {COEF_FIXED, "Use fixed filter coefficients", "fixed"},
      {COEF_AUTO_GENERATE, "Auto generate filter coefficients", "auto"},
      {0, NULL, NULL}
    };
    load_type =
        g_enum_register_static ("GstVvasXMultiCropCoefLoadType", load_types);
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
    case GST_VIDEO_FORMAT_I420:
      return XV_MULTI_SCALER_I420;
    case GST_VIDEO_FORMAT_GBR:
      return XV_MULTI_SCALER_R_G_B8;
    default:
      GST_ERROR ("Not supporting %s yet",
          gst_video_format_to_string ((GstVideoFormat) col));
      return XV_MULTI_SCALER_NONE;
  }
}

static inline uint32_t
xlnx_multiscaler_stride_align (uint32_t stride_in, uint16_t alignment)
{
  uint32_t stride;
  stride = (((stride_in) + alignment - 1) / alignment) * alignment;
  return stride;
}

static inline gint
log2_val (guint val)
{
  gint cnt = 0;
  while (val > 1) {
    val = val >> 1;
    cnt++;
  }
  return cnt;
}

static gboolean
feasibility_check (int src, int dst, int *filterSize)
{
  int sizeFactor = 4;
  int xInc = (((int64_t) src << 16) + (dst >> 1)) / dst;
  if (xInc <= 1 << 16)
    *filterSize = 1 + sizeFactor;       // upscale
  else
    *filterSize = 1 + (sizeFactor * src + dst - 1) / dst;

  if (*filterSize > MAX_FILTER_SIZE) {
    GST_ERROR ("FilterSize %d for %d to %d is greater than maximum taps(%d)",
        *filterSize, src, dst, MAX_FILTER_SIZE);
    return FALSE;
  }

  return TRUE;
}

static void
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
generate_cardinal_cubic_spline (int src, int dst, int filterSize, int64_t B,
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
  unsigned int PhaseH = 0, offset = 0, WriteLoc = 0, WriteLocNext = 0, ReadLoc =
      0, OutputWrite_En = 0;
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
      //added to negate double increment and match our precision
      coeffFilter_normalized[i * (outFilterSize) + j] =
          coeffFilter_normalized[i * (outFilterSize) + j] >> 2;
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

static void
vvas_xmulticrop_prepare_coefficients_with_12tap (GstVvasXMultiCrop * self,
    guint chain_id, guint in_width, guint in_height,
    guint out_width, guint out_height)
{
  GstVvasXMultiCropPrivate *priv = self->priv;
  gint filter_size;
  int64_t B = 0 * (1 << 24);
  int64_t C = 0.6 * (1 << 24);
  float scale_ratio[2] = { 0, 0 };
  int upscale_enable[2] = { 0, 0 };
  int filterSet[2] = { 0, 0 };
  guint d;
  gboolean bret;

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
    GST_INFO_OBJECT (self, "%s scaling ratio = %f and chosen filter type = %d",
        d == 0 ? "width" : "height", scale_ratio[d], filterSet[d]);
  }

  if (self->coef_load_type == COEF_AUTO_GENERATE) {
    /* prepare horizontal coefficients */
    bret = feasibility_check (in_width, out_width, &filter_size);
    if (bret && !upscale_enable[0]) {
      GST_INFO_OBJECT (self, "Generate cardinal cubic horizontal coefficients "
          "with filter size %d", filter_size);
      generate_cardinal_cubic_spline (in_width, out_width, filter_size, B, C,
          (int16_t *) priv->Hcoff[chain_id].user_ptr);
    } else {
      /* get fixed horizontal filters */
      GST_INFO_OBJECT (self,
          "Consider predefined horizontal filter coefficients");
      copy_filt_set (priv->Hcoff[chain_id].user_ptr, filterSet[0]);
    }

    /* prepare vertical coefficients */
    bret = feasibility_check (in_height, out_height, &filter_size);
    if (bret && !upscale_enable[1]) {
      GST_INFO_OBJECT (self, "Generate cardinal cubic vertical coefficients "
          "with filter size %d", filter_size);
      generate_cardinal_cubic_spline (in_height, out_height, filter_size, B, C,
          (int16_t *) priv->Vcoff[chain_id].user_ptr);
    } else {
      /* get fixed vertical filters */
      GST_INFO_OBJECT (self,
          "Consider predefined vertical filter coefficients");
      copy_filt_set (priv->Vcoff[chain_id].user_ptr, filterSet[1]);
    }
  } else if (self->coef_load_type == COEF_FIXED) {
    /* get fixed horizontal filters */
    GST_INFO_OBJECT (self,
        "Consider predefined horizontal filter coefficients");
    copy_filt_set (priv->Hcoff[chain_id].user_ptr, filterSet[0]);

    /* get fixed vertical filters */
    GST_INFO_OBJECT (self, "Consider predefined vertical filter coefficients");
    copy_filt_set (priv->Vcoff[chain_id].user_ptr, filterSet[1]);
  }
}

static gboolean
is_colordomain_matching_with_peer (GstCaps * peercaps,
    ColorDomain in_color_domain)
{
  GstStructure *peer_structure;
  const gchar *out_format;
  ColorDomain out_color_domain = COLOR_DOMAIN_UNKNOWN;
  gint j, pn;
  gboolean color_domain_matched = FALSE;

  pn = gst_caps_get_size (peercaps);

  for (j = 0; j < pn; j++) {
    const GValue *targets;
    peer_structure = gst_caps_get_structure (peercaps, j);
    targets = gst_structure_get_value (peer_structure, "format");

    if (G_TYPE_CHECK_VALUE_TYPE (targets, G_TYPE_STRING)) {

      out_format = g_value_get_string (targets);
      out_color_domain = vvas_xmulticrop_get_color_domain (out_format);

      if (out_color_domain == in_color_domain) {
        color_domain_matched = TRUE;
      }

    } else if (G_TYPE_CHECK_VALUE_TYPE (targets, GST_TYPE_LIST)) {
      gint j, m;

      m = gst_value_list_get_size (targets);
      for (j = 0; j < m; j++) {
        const GValue *val = gst_value_list_get_value (targets, j);

        out_format = g_value_get_string (val);
        out_color_domain = vvas_xmulticrop_get_color_domain (out_format);

        if (out_color_domain == in_color_domain) {
          color_domain_matched = TRUE;
          break;
        }
      }
    }
  }
  return color_domain_matched;
}

static gboolean
vvas_xmulticrop_align_crop_params (GstBaseTransform * trans,
    VvasCropParams * crop_params, guint width_alignment)
{

  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);

  if (crop_params->x || crop_params->y ||
      crop_params->width || crop_params->height) {

    guint input_width, input_height, x_aligned, crop_width;

    input_width = self->priv->in_vinfo->width;
    input_height = self->priv->in_vinfo->height;

    if ((crop_params->x >= input_width) || (crop_params->y >= input_height)) {
      GST_ERROR_OBJECT (self, "crop x or y coordinate can't be >= "
          " input width and height");
      return FALSE;
    }

    if (crop_params->x && !crop_params->width) {
      crop_params->width = input_width - crop_params->x;
    }

    if (crop_params->y && !crop_params->height) {
      crop_params->height = input_height - crop_params->y;
    }
    crop_width = crop_params->width;
    /* Align values as per the IP requirement
     * Align x by 8 * PPC, width by PPC, y and height by 2
     */
    x_aligned = (crop_params->x / (8 * self->ppc)) * (8 * self->ppc);
    crop_params->width = crop_params->x + crop_params->width - x_aligned;
    crop_params->width = xlnx_multiscaler_stride_align (crop_params->width,
        width_alignment);

    crop_params->x = x_aligned;
    crop_params->y = (crop_params->y / 2) * 2;
    crop_params->height =
        xlnx_multiscaler_stride_align (crop_params->height, 2);

    GST_INFO_OBJECT (self, "crop aligned params: x:%u, y:%u, width:%u, "
        "height: %u, extra width: %u",
        crop_params->x, crop_params->y, crop_params->width, crop_params->height,
        (crop_params->width - crop_width));

    if (((crop_params->x + crop_params->width) > input_width) ||
        ((crop_params->y + crop_params->height) > input_height)) {
      GST_ERROR_OBJECT (self, "x + width or y + height can't be greater "
          "than input width and height");
      return FALSE;
    }
  }
  return TRUE;
}

static int32_t
vvas_xmulticrop_exec_buf (vvasDeviceHandle dev_handle,
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

#ifdef XLNX_PCIe_PLATFORM
static gboolean
xlnx_multicrop_coeff_sync_bo (GstVvasXMultiCrop * self)
{
  GstVvasXMultiCropPrivate *priv = self->priv;
  int iret;
  guint idx;

  for (idx = 0; idx < (self->priv->sub_buffer_counter + 1); idx++) {

    iret = vvas_xrt_write_bo (priv->Hcoff[idx].bo,
        priv->Hcoff[idx].user_ptr, priv->Hcoff[idx].size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (self,
          "failed to write horizontal coefficients. reason : %s",
          strerror (errno));
      return FALSE;
    }

    iret = vvas_xrt_sync_bo (priv->Hcoff[idx].bo,
        VVAS_BO_SYNC_BO_TO_DEVICE, priv->Hcoff[idx].size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (self,
          "failed to sync horizontal coefficients. reason : %s",
          strerror (errno));
      GST_ELEMENT_ERROR (self, RESOURCE, SYNC, NULL,
          ("failed to sync horizontal coefficients to device. reason : %s",
              strerror (errno)));
      return FALSE;
    }

    iret = vvas_xrt_write_bo (priv->Vcoff[idx].bo,
        priv->Vcoff[idx].user_ptr, priv->Vcoff[idx].size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (self,
          "failed to write vertical coefficients. reason : %s",
          strerror (errno));
      return FALSE;
    }

    iret = vvas_xrt_sync_bo (priv->Vcoff[idx].bo,
        VVAS_BO_SYNC_BO_TO_DEVICE, priv->Vcoff[idx].size, 0);
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
xlnx_multicrop_desc_sync_bo (GstVvasXMultiCrop * self)
{
  GstVvasXMultiCropPrivate *priv = self->priv;
  int chan_id;
  int iret;

  for (chan_id = 0; chan_id < (self->priv->sub_buffer_counter + 1); chan_id++) {

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
vvas_xmulticrop_create_context (GstVvasXMultiCrop * self)
{
  GstVvasXMultiCropPrivate *priv = self->priv;
  if (!self->kern_name) {
    GST_ERROR_OBJECT (self, "kernel name is not set");
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("kernel name is not set"));
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

  /* We have to download the xclbin irrespective of XRM or not as there
   * mismatch of UUID between XRM and XRT Native. CR-1122125 raised */
  if (vvas_xrt_download_xclbin (self->xclbin_path,
          priv->dev_handle, &(priv->xclbinId))) {
    GST_ERROR_OBJECT (self, "failed to initialize XRT");
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, (NULL),
        ("xclbin download failed"));
    return FALSE;
  }
//#endif

  if (vvas_xrt_open_context (priv->dev_handle, priv->xclbinId,
          &priv->kern_handle, self->kern_name, true)) {
    GST_ERROR_OBJECT (self, "failed to open XRT context ...");
    return FALSE;
  }
  return TRUE;
}

static void
vvas_xmulticrop_free_internal_buffers (GstVvasXMultiCrop * self)
{
  GstVvasXMultiCropPrivate *priv = self->priv;
  guint idx;

  GST_DEBUG_OBJECT (self, "freeing internal buffers");
  for (idx = 0; idx < priv->internal_buf_counter; idx++) {
    if (priv->Hcoff[idx].user_ptr) {
      vvas_xrt_free_xrt_buffer (&priv->Hcoff[idx]);
      memset (&(self->priv->Hcoff[idx]), 0x0, sizeof (xrt_buffer));
    }
    if (priv->Vcoff[idx].user_ptr) {
      vvas_xrt_free_xrt_buffer (&priv->Vcoff[idx]);
      memset (&(self->priv->Vcoff[idx]), 0x0, sizeof (xrt_buffer));
    }
    if (priv->msPtr[idx].user_ptr) {
      vvas_xrt_free_xrt_buffer (&priv->msPtr[idx]);
      memset (&(self->priv->msPtr[idx]), 0x0, sizeof (xrt_buffer));
    }
  }
  priv->internal_buf_counter = 0;
}

static gboolean
vvas_xmulticrop_destroy_context (GstVvasXMultiCrop * self)
{
  GstVvasXMultiCropPrivate *priv = self->priv;
  gboolean has_error = FALSE;
  gint iret;

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

static void
gst_vvas_xmulticrop_class_init (GstVvasXMultiCropClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *xform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &src_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &sink_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Xilinx XlnxMultiCrop plugin",
      "Filter/Converter/Video/Scaler",
      "MulitCrop plugin based on Multi scaler using XRT",
      "Xilinx Inc <www.xilinx.com>");

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xmulticrop_debug_category,
      "vvas_xmulticrop", 0, "Xilinx's MultiCrop plugin");

  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_finalize);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_change_state);

  g_object_class_install_property (gobject_class, PROP_XCLBIN_LOCATION,
      g_param_spec_string ("xclbin-location", "xclbin file location",
          "Location of the xclbin to program devices", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));
  g_object_class_install_property (gobject_class, PROP_KERN_NAME,
      g_param_spec_string ("kernel-name", "kernel name and instance",
          "String defining the kernel name and instance as mentioned in xclbin",
          DEFAULT_KERNEL_NAME,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));
#ifdef XLNX_PCIe_PLATFORM
  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("dev-idx", "Device index",
          "Valid Device index is 0 to 31. Default value is set to -1 intentionally"
          "\n\t\t\tso that user provides the correct device index.",
          -1, 31, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

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

  g_object_class_install_property (gobject_class, PROP_PPC,
      g_param_spec_enum ("ppc", "pixel per clock",
          "Pixel per clock configured in Multiscaler kernel",
          VVAS_XMULTICROP_PPC_TYPE, VVAS_XMULTICROP_DEFAULT_PPC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject_class, PROP_SCALE_MODE,
      g_param_spec_int ("scale-mode", "Scaling Mode",
          "Scale Mode configured in Multiscaler kernel.  "
          "0: BILINEAR 1: BICUBIC 2: POLYPHASE", 0, 2,
          VVAS_XMULTICROP_SCALE_MODE, G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_NUM_TAPS,
      g_param_spec_enum ("num-taps", "Number filter taps",
          "Number of filter taps to be used for scaling",
          VVAS_XMULTICROP_NUM_TAPS_TYPE, VVAS_XMULTICROP_DEFAULT_NUM_TAPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_COEF_LOADING_TYPE,
      g_param_spec_enum ("coef-load-type", "Coefficients loading type",
          "coefficients loading type for scaling",
          VVAS_XMULTICROP_COEF_LOAD_TYPE,
          VVAS_XMULTICROP_DEFAULT_COEF_LOAD_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_AVOID_OUTPUT_COPY,
      g_param_spec_boolean ("avoid-output-copy",
          "Avoid output frames copy",
          "Avoid output frames copy even when downstream"
          " does not support GstVideoMeta metadata",
          VVAS_XMULTICROP_AVOID_OUTPUT_COPY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_ENABLE_PIPELINE,
      g_param_spec_boolean ("enable-pipeline",
          "Enable pipelining",
          "Enable buffer pipelining to improve performance in non zero-copy use cases",
          VVAS_XMULTICROP_ENABLE_PIPELINE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_CROP_X,
      g_param_spec_uint ("s-crop-x", "Crop X coordinate for static cropping",
          "Crop X coordinate for static cropping",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_CROP_Y,
      g_param_spec_uint ("s-crop-y", "Crop Y coordinate for static cropping",
          "Crop Y coordinate for static cropping",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_CROP_WIDTH,
      g_param_spec_uint ("s-crop-width", "Crop width for static cropping",
          "Crop width (minimum: 64) for static cropping, if s-crop-x is given, but "
          "s-crop-width is 0 or not specified,"
          "\n\t\t\ts-crop-width will be calculated as input width - s-crop-x",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_CROP_HEIGHT,
      g_param_spec_uint ("s-crop-height", "Crop height for static cropping",
          "Crop height (minimum: 64) for static cropping, if s-crop-y is given, but "
          "s-crop-height is 0 or not specified,"
          "\n\t\t\ts-crop-height will be calculated as input height - s-crop-y",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_SUBBUFFER_WIDTH,
      g_param_spec_uint ("d-width", "Width of dynamically cropped buffers",
          "Width of dynamically cropped buffers, if set all dynamically "
          "\n\t\t\tcropped buffer will be scaled to this width",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_SUBBUFFER_HEIGHT,
      g_param_spec_uint ("d-height", "Height of dynamically cropped buffers",
          "Height of dynamically cropped buffers, if set all dynamically"
          "\n\t\t\tcropped buffers will be scaled to this height",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_SUBBUFFER_FORMAT,
      g_param_spec_enum ("d-format", "Format of dynamically cropped buffers",
          "Format of dynamically cropped buffers, by default it will be same as"
          " input buffer",
          VVAS_XMULTICROP_SUBBUFFER_FORMAT_TYPE, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_PPE_ON_MAIN_BUFFER,
      g_param_spec_boolean ("ppe-on-main-buffer",
          "Post processing on main buffer",
          "If set, PP will be applied to main buffer also, otherwise it will be"
          " applied only on dynamically cropped buffers",
          VVAS_XMULTICROP_PPE_ON_MAIN_BUF_DEFAULT,
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

  xform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_transform_caps);
  xform_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_fixate_caps);
  xform_class->set_caps = GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_set_caps);
  xform_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_decide_allocation);
  xform_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_propose_allocation);
  xform_class->sink_event = GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_sink_event);
  xform_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_generate_output);
  xform_class->transform = GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_transform);

  xform_class->passthrough_on_same_caps = FALSE;
}

static void
gst_vvas_xmulticrop_init (GstVvasXMultiCrop * self)
{
  self->priv = GST_VVAS_XMULTICROP_PRIVATE (self);

  self->xclbin_path = NULL;
  self->out_stride_align = 1;
  self->out_elevation_align = 1;
  self->num_taps = VVAS_XMULTICROP_DEFAULT_NUM_TAPS;
  self->coef_load_type = VVAS_XMULTICROP_DEFAULT_COEF_LOAD_TYPE;
  self->avoid_output_copy = VVAS_XMULTICROP_AVOID_OUTPUT_COPY_DEFAULT;
#ifdef ENABLE_PPE_SUPPORT
  self->alpha_r = 0;
  self->alpha_g = 0;
  self->alpha_b = 0;
  self->beta_r = 1;
  self->beta_g = 1;
  self->beta_b = 1;
#endif
  self->dev_index = DEFAULT_DEVICE_INDEX;
  self->ppc = VVAS_XMULTICROP_DEFAULT_PPC;
  self->scale_mode = VVAS_XMULTICROP_SCALE_MODE;
  self->in_mem_bank = DEFAULT_MEM_BANK;
  self->out_mem_bank = DEFAULT_MEM_BANK;
  self->crop_params.x = DEFAULT_CROP_PARAMS;
  self->crop_params.y = DEFAULT_CROP_PARAMS;
  self->crop_params.width = DEFAULT_CROP_PARAMS;
  self->crop_params.height = DEFAULT_CROP_PARAMS;

  memset (self->priv, 0, sizeof (GstVvasXMultiCropPrivate));

  self->subbuffer_format = 0;
  self->subbuffer_width = 0;
  self->subbuffer_height = 0;
  self->ppe_on_main_buffer = VVAS_XMULTICROP_PPE_ON_MAIN_BUF_DEFAULT;
  self->priv->sub_buffer_counter = 0;
  self->priv->internal_buf_counter = 0;
  self->priv->in_vinfo = gst_video_info_new ();
  self->priv->out_vinfo = gst_video_info_new ();
  self->priv->validate_import = TRUE;
  self->priv->input_pool = NULL;
  self->priv->is_internal_buf_allocated = FALSE;
  self->kern_name = g_strdup (DEFAULT_KERNEL_NAME);
  gst_video_info_init (self->priv->in_vinfo);
  gst_video_info_init (self->priv->out_vinfo);
  self->priv->need_copy = TRUE;
  self->enabled_pipeline = VVAS_XMULTICROP_ENABLE_PIPELINE_DEFAULT;
}

/* decide allocation query for output buffers */
static gboolean
gst_vvas_xmulticrop_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  GstVideoInfo info;
  GstCaps *outcaps;
  guint size, min, max;
  gboolean update_allocator, update_pool, bret, have_new_allocator = FALSE;
  GstStructure *config = NULL;
  GstVideoInfo out_vinfo;

  gst_query_parse_allocation (query, &outcaps, NULL);
  if (outcaps && !gst_video_info_from_caps (&info, outcaps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from outcaps");
    goto error;
  }

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

  if (outcaps && !gst_video_info_from_caps (&out_vinfo, outcaps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from outcaps");
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
      stride =
          vvas_xmulticrop_get_stride (GST_VIDEO_INFO_FORMAT (&out_vinfo),
          padded_width);
      //stride = vvas_xmulticrop_get_stride (&out_vinfo, padded_width);

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

  /* If there is no pool allignment requirement from downstream or if scaling dimension
   * is not aligned to (8 * ppc), then we will create a new pool*/
  if (!pool && (self->out_stride_align == 1)
      && ((out_vinfo.stride[0] % WIDTH_ALIGN)
          || (out_vinfo.height % HEIGHT_ALIGN))) {
    self->out_stride_align = WIDTH_ALIGN;
    self->out_elevation_align = HEIGHT_ALIGN;
  }

  if (!pool) {
    GstVideoAlignment align;

    pool = gst_vvas_buffer_pool_new (self->out_stride_align,
        self->out_elevation_align);
    GST_INFO_OBJECT (self, "created new pool %p %" GST_PTR_FORMAT, pool, pool);

    config = gst_buffer_pool_get_config (pool);
    gst_video_alignment_reset (&align);
    align.padding_top = 0;
    align.padding_left = 0;
    align.padding_right = vvas_xmulticrop_get_padding_right (self, &out_vinfo);
    align.padding_bottom = ALIGN (GST_VIDEO_INFO_HEIGHT (&out_vinfo),
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

    GST_DEBUG_OBJECT (self,
        "Align padding: top:%d, bottom:%d, left:%d, right:%d",
        align.padding_top, align.padding_bottom, align.padding_left,
        align.padding_right);

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

  gst_object_unref (pool);
  GST_DEBUG_OBJECT (self,
      "done decide allocation with query %" GST_PTR_FORMAT, query);
  return TRUE;

error:
  if (allocator)
    gst_object_unref (allocator);
  if (pool)
    gst_object_unref (pool);
  return FALSE;
}

/* propose allocation query parameters for input buffers */
static gboolean
gst_vvas_xmulticrop_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{

  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool;
  guint size;
  GstVideoAlignment align;

  GST_BASE_TRANSFORM_CLASS
      (gst_vvas_xmulticrop_parent_class)->propose_allocation (trans,
      decide_query, query);

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
    align.padding_right = vvas_xmulticrop_get_padding_right (self, &info);
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
  return TRUE;
}

/* sink and src pad event handlers */
static gboolean
gst_vvas_xmulticrop_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (self, "received %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {

    case GST_EVENT_EOS:{
      if (self->enabled_pipeline) {
        GstFlowReturn fret = GST_FLOW_OK;
        GstBuffer *inbuf = NULL;
        GstBaseTransformClass *klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

        GST_INFO_OBJECT (self, "input copy queue has %d pending buffers",
            g_async_queue_length (self->priv->copy_outqueue));

        while (g_async_queue_length (self->priv->copy_outqueue) > 0) {
          GstBuffer *outbuf = NULL;
          inbuf = g_async_queue_pop (self->priv->copy_outqueue);

          if (GST_FLOW_OK != klass->submit_input_buffer (trans, FALSE, inbuf)) {
            ret = FALSE;
            break;
          }

          if (GST_FLOW_OK != klass->generate_output (trans, &outbuf)) {
            ret = FALSE;
            break;
          }

          if (outbuf) {
            GST_DEBUG_OBJECT (self, "output %" GST_PTR_FORMAT, outbuf);
            fret = gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (trans), outbuf);
          }
          if (GST_FLOW_OK != fret) {
            ret = FALSE;
          }
        }
      }
      break;
    }
    default:{
      break;
    }
  }
  ret =
      GST_BASE_TRANSFORM_CLASS (gst_vvas_xmulticrop_parent_class)->sink_event
      (trans, event);
  return ret;
}

static gboolean
vvas_xmulticrop_calculate_subbuffer_pool_size (GstVvasXMultiCrop * self)
{

  GstVvasXMultiCropPrivate *priv = self->priv;
  GstVideoInfo out_info = { 0 };
  guint max_out_w, max_out_h;
  gsize size, maximum_out_size;
  gint idx;

  max_out_w = vvas_xmulticrop_get_stride (self->subbuffer_format,
      GST_VIDEO_INFO_WIDTH (priv->in_vinfo));
  max_out_w = ALIGN (max_out_w, WIDTH_ALIGN);
  max_out_h = ALIGN (GST_VIDEO_INFO_HEIGHT (priv->in_vinfo), HEIGHT_ALIGN);

  if (!gst_video_info_set_format (&out_info, self->subbuffer_format,
          max_out_w, max_out_h)) {
    GST_ERROR_OBJECT (self, "failed to set video info");
    return FALSE;
  }

  maximum_out_size = GST_VIDEO_INFO_SIZE (&out_info);
  GST_DEBUG_OBJECT (self, "max_out_w:%u, max_out_h:%u, maximum_out_size: %ld",
      max_out_w, max_out_h, maximum_out_size);

  size = (maximum_out_size / 10) + 1;

  for (idx = 0; idx < MAX_SUBBUFFER_POOLS; idx++) {
    priv->subbuffer_pool_size[idx] = size * (idx + 1);
#ifdef DEBUG
    GST_DEBUG_OBJECT (self, "size[%d]: %u", idx,
        priv->subbuffer_pool_size[idx]);
#endif
  }

  return TRUE;
}

static GstCaps *
gst_vvas_xmulticrop_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{

  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstCaps *ret;
  GstStructure *structure;
  GstCapsFeatures *features;
  gint i, n;
  GstPad *otherpeer = NULL;
  GstCaps *peercaps = NULL;
  const gchar *in_format;
  ColorDomain in_color_domain;
  gboolean color_domain_matched = FALSE;

  GST_DEBUG_OBJECT (self,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (gst_caps_is_fixed (caps)) {
    GST_DEBUG_OBJECT (trans, "caps is fixed now");
    otherpeer = gst_pad_get_peer (GST_BASE_TRANSFORM_SRC_PAD (trans));
    peercaps = gst_pad_query_caps (otherpeer, NULL);
    gst_object_unref (otherpeer);

    GST_DEBUG_OBJECT (self, "peercaps: %" GST_PTR_FORMAT, peercaps);
  }

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

    if (peercaps) {
      /* Find the color domain on source pad to find
       * if colorimetry information can be preserved in the caps */
      in_format = gst_structure_get_string (structure, "format");
      in_color_domain = vvas_xmulticrop_get_color_domain (in_format);

      if (COLOR_DOMAIN_UNKNOWN != in_color_domain) {
        color_domain_matched = is_colordomain_matching_with_peer (peercaps,
            in_color_domain);
      }
    }

    /* If the features are non-sysmem we can only do passthrough */
    if (!gst_caps_features_is_any (features)
        && gst_caps_features_is_equal (features,
            GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY)) {
      gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

      if (!peercaps) {
        gst_structure_remove_fields (structure, "format", "colorimetry",
            "chroma-site", NULL);
      }

      if (peercaps) {
        /* If color domain matches, no need to remove colorimetry information */
        if (color_domain_matched == FALSE) {
          gst_structure_remove_fields (structure, "format", "colorimetry",
              "chroma-site", NULL);
        } else {
          gst_structure_remove_fields (structure, "format", "chroma-site",
              NULL);
        }
      }

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

  if (peercaps) {
    gst_caps_unref (peercaps);
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static GstCaps *
gst_vvas_xmulticrop_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{

  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
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

static gboolean
gst_vvas_xmulticrop_set_caps (GstBaseTransform * trans, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstVvasXMultiCropPrivate *priv = self->priv;
  guint input_width, input_height;
  gboolean bret = TRUE;

  GST_DEBUG_OBJECT (self, "in_caps %p %" GST_PTR_FORMAT, in_caps, in_caps);
  GST_DEBUG_OBJECT (self, "out_caps %p %" GST_PTR_FORMAT, out_caps, out_caps);

  /* store sinkpad info */
  if (!gst_video_info_from_caps (priv->in_vinfo, in_caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from input caps");
    return FALSE;
  }

  if ((priv->in_vinfo->width % self->ppc)) {
    GST_ERROR_OBJECT (self, "Unsupported input resolution,"
        "width must be multiple of ppc i.e, %d", self->ppc);
    return FALSE;
  }

  if (!gst_video_info_from_caps (priv->out_vinfo, out_caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from output caps");
    return FALSE;
  }

  if (priv->out_vinfo->width % self->ppc) {
    GST_ERROR_OBJECT (self, "Unsupported output resolution,"
        "width must be multiple of ppc i.e, %d", self->ppc);
    return FALSE;
  }

  if (TRUE != vvas_xmulticrop_align_crop_params (trans, &self->crop_params,
          self->ppc)) {
    return FALSE;
  }

  if (self->crop_params.width || self->crop_params.height) {
    if ((self->crop_params.width < MIN_SCALAR_WIDTH) ||
        (self->crop_params.height < MIN_SCALAR_HEIGHT)) {
      GST_ERROR_OBJECT (self, "static crop width/height must be at least %u",
          MIN_SCALAR_WIDTH);
      return FALSE;
    }
  }

  if (self->crop_params.width && self->crop_params.height) {
    input_width = self->crop_params.width;
    input_height = self->crop_params.height;
  } else {
    input_width = GST_VIDEO_INFO_WIDTH (priv->in_vinfo);
    input_height = GST_VIDEO_INFO_HEIGHT (priv->in_vinfo);
  }

  if (!priv->dev_handle) {
    /* create XRT context */
    bret = vvas_xmulticrop_create_context (self);
    if (!bret) {
      GST_ERROR_OBJECT (self, "couldn't create XRT context");
      return FALSE;
    } else {
      GST_DEBUG_OBJECT (self, "XRT context created");
    }
  }

  if (!priv->is_internal_buf_allocated) {
    /* allocate internal buffers for processing of main buffer */
    bret = vvas_xmulticrop_allocate_internal_buffers (self, 1);
    if (!bret) {
      GST_ERROR_OBJECT (self, "couldn't allocate internal buffers");
      return FALSE;
    }
    priv->is_internal_buf_allocated = TRUE;
  }

  /* If user don't give sub-buffer's format, we will consider it same as input */
  self->subbuffer_format = self->subbuffer_format ? self->subbuffer_format :
      GST_VIDEO_INFO_FORMAT (priv->in_vinfo);

  GST_DEBUG_OBJECT (self, "sub buffer format is %d", self->subbuffer_format);

  if (self->subbuffer_width && self->subbuffer_height) {
    /* User has specified sub buffer's resolution, this means all cropped
     * buffers will be resized to this resolution, hence we need to only
     * allocate one buffer pool
     */

    if (self->subbuffer_width % self->ppc) {
      GST_ERROR_OBJECT (self, "width of dynamically cropped buffers"
          " must be multiple of ppc[%d]", self->ppc);
      return FALSE;
    }

    priv->subbuffer_pools[0] = vvas_xmulticrop_allocate_buffer_pool (self,
        self->subbuffer_width, self->subbuffer_height, self->subbuffer_format);
    if (!priv->subbuffer_pools[0]) {
      GST_ERROR_OBJECT (self, "failed to allocate pool");
      return FALSE;
    }
  } else {
    if (!vvas_xmulticrop_calculate_subbuffer_pool_size (self)) {
      return FALSE;
    }
  }

  if (self->num_taps == 12) {
    vvas_xmulticrop_prepare_coefficients_with_12tap (self, 0, input_width,
        input_height,
        GST_VIDEO_INFO_WIDTH (priv->out_vinfo),
        GST_VIDEO_INFO_HEIGHT (priv->out_vinfo));
  } else {
    if (self->scale_mode == POLYPHASE) {
      float scale = (float) input_height
          / (float) GST_VIDEO_INFO_HEIGHT (priv->out_vinfo);
      GST_INFO_OBJECT (self,
          "preparing coefficients with scaling ration %f and taps %d", scale,
          self->num_taps);
      xlnx_multiscaler_coff_fill (priv->Hcoff[0].user_ptr,
          priv->Vcoff[0].user_ptr, scale);
    }
  }

#ifdef XLNX_PCIe_PLATFORM
  if (!xlnx_multicrop_coeff_sync_bo (self))
    return FALSE;
#endif

  return TRUE;
}

static gpointer
vvas_xmulticrop_input_copy_thread (gpointer data)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (data);
  GstVvasXMultiCropPrivate *priv = self->priv;

  while (1) {
    GstBuffer *inbuf = NULL;
    GstBuffer *own_inbuf = NULL;
    GstVideoFrame in_vframe, own_vframe;
    GstFlowReturn fret = GST_FLOW_OK;

    inbuf = (GstBuffer *) g_async_queue_pop (priv->copy_inqueue);
    if (inbuf == STOP_COMMAND) {
      GST_DEBUG_OBJECT (self, "received stop command. exit copy thread");
      break;
    }

    /* acquire buffer from own input pool */
    fret = gst_buffer_pool_acquire_buffer (priv->input_pool, &own_inbuf, NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
          priv->input_pool);
      goto error;
    }
    GST_LOG_OBJECT (self, "acquired buffer %p from own pool", own_inbuf);

    /* map internal buffer in write mode */
    if (!gst_video_frame_map (&own_vframe, priv->in_vinfo, own_inbuf,
            GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "failed to map internal input buffer");
      goto error;
    }

    /* map input buffer in read mode */
    if (!gst_video_frame_map (&in_vframe, priv->in_vinfo, inbuf, GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "failed to map input buffer");
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
    g_async_queue_push (priv->copy_outqueue, own_inbuf);
  }

error:
  return NULL;
}

static gboolean
vvas_xmulticrop_allocate_internal_pool (GstBaseTransform * trans)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstCaps *caps = NULL;
  GstVideoAlignment align;

  caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM_SINK_PAD (trans));

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
  align.padding_right = vvas_xmulticrop_get_padding_right (self, &info);
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
      "allocated %" GST_PTR_FORMAT " allocator at mem bank %d", allocator,
      self->in_mem_bank);

  gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (&info),
      3, 5);

  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (allocator)
    gst_object_unref (allocator);

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

static gboolean
vvas_xmulticrop_allocate_internal_buffers (GstVvasXMultiCrop * self,
    guint buffer_count)
{
  GstVvasXMultiCropPrivate *priv = self->priv;
  gint iret;
  guint idx = 0;
  guint offset = priv->internal_buf_counter;

  for (idx = 0; idx < buffer_count; idx++) {
    iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle, COEFF_SIZE,
        VVAS_BO_FLAGS_NONE, self->in_mem_bank, &priv->Hcoff[offset + idx]);
    if (iret < 0) {
      GST_ERROR_OBJECT (self,
          "failed to allocate horizontal coefficients command buffer..");
      goto error;
    }

    iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle, COEFF_SIZE,
        VVAS_BO_FLAGS_NONE, self->in_mem_bank, &priv->Vcoff[offset + idx]);
    if (iret < 0) {
      GST_ERROR_OBJECT (self,
          "failed to allocate vertical coefficients command buffer..");
      goto error;
    }

    iret = vvas_xrt_alloc_xrt_buffer (priv->dev_handle, COEFF_SIZE,
        VVAS_BO_FLAGS_NONE, self->in_mem_bank, &priv->msPtr[offset + idx]);
    if (iret < 0) {
      GST_ERROR_OBJECT (self, "failed to allocate descriptor command buffer..");
      goto error;
    }
#ifdef DEBUG
    printf ("[%u] DESC phy %lx  virt  %p \n", offset + idx,
        priv->msPtr[offset + idx].phy_addr, priv->msPtr[offset + idx].user_ptr);
    printf ("[%u] HCoef phy %lx  virt  %p \n", offset + idx,
        priv->Hcoff[offset + idx].phy_addr, priv->Hcoff[offset + idx].user_ptr);
    printf ("[%u] VCoef phy %lx  virt  %p \n", offset + idx,
        priv->Vcoff[offset + idx].phy_addr, priv->Vcoff[offset + idx].user_ptr);
#endif

    priv->internal_buf_counter++;
    GST_LOG_OBJECT (self, "allocated internal buffer for %u, total: %u",
        offset + idx, priv->internal_buf_counter);
  }
  return TRUE;

error:
  GST_ELEMENT_ERROR (self, RESOURCE, NO_SPACE_LEFT, NULL,
      ("failed to allocate memory"));
  return FALSE;
}

static gboolean
vvas_xmulticrop_prepare_input_buffer (GstBaseTransform * trans,
    GstBuffer ** inbuf)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstVvasXMultiCropPrivate *priv = self->priv;
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
    if (self->priv->validate_import) {
      if (!self->priv->input_pool) {
        bret = vvas_xmulticrop_allocate_internal_pool (trans);
        if (!bret)
          goto error;
      }
      if (!gst_buffer_pool_is_active (self->priv->input_pool))
        gst_buffer_pool_set_active (self->priv->input_pool, TRUE);
      self->priv->validate_import = FALSE;
    }

    if (self->enabled_pipeline) {
      own_inbuf = g_async_queue_try_pop (priv->copy_outqueue);
      if (!own_inbuf && !priv->is_first_frame) {
        own_inbuf = g_async_queue_pop (priv->copy_outqueue);
      }

      priv->is_first_frame = FALSE;
      g_async_queue_push (priv->copy_inqueue, *inbuf);

      if (!own_inbuf) {
        GST_LOG_OBJECT (self, "copied input buffer is not available. return");
        *inbuf = NULL;
        return TRUE;
      }

      *inbuf = own_inbuf;
    } else {
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
          (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_METADATA
              | GST_BUFFER_COPY_TIMESTAMPS), 0, -1);

      gst_buffer_unref (*inbuf);
      *inbuf = own_inbuf;
    }
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

  self->priv->phy_in_2 = 0;
  self->priv->phy_in_1 = 0;
  self->priv->phy_in_0 = phy_addr;
  if (vmeta->n_planes > 1)
    self->priv->phy_in_1 = phy_addr + vmeta->offset[1];
  if (vmeta->n_planes > 2)
    self->priv->phy_in_2 = phy_addr + vmeta->offset[2];

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
vvas_xmulticrop_prepare_output_buffer (GstVvasXMultiCrop * self,
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

static gboolean
vvas_xmulticrop_prepare_output_descriptor (GstVvasXMultiCrop * self)
{

  MULTI_SCALER_DESC_STRUCT *msPtr;
  GstVideoMeta *meta_out;
  GstVvasXMultiCropPrivate *priv = self->priv;
  uint32_t width = priv->in_vinfo->width;
  uint32_t height = priv->in_vinfo->height;

  guint plane0_offset = 0, plane1_offset = 0, plane2_offset = 0;

#ifdef ENABLE_PPE_SUPPORT
  uint32_t val;
#endif
  uint32_t msc_inPixelFmt =
      xlnx_multiscaler_colorformat (priv->in_vinfo->finfo->format);
  uint32_t stride = self->priv->meta_in_stride;

  if (stride != xlnx_multiscaler_stride_align (stride, WIDTH_ALIGN)) {
    GST_WARNING_OBJECT (self, "input stide alignment mismatch");
    GST_WARNING_OBJECT (self, "required  stride = %d in_stride = %d",
        xlnx_multiscaler_stride_align (stride, WIDTH_ALIGN), stride);
  }

  if (self->crop_params.width && self->crop_params.height) {
    guint x_cord, y_cord;

    x_cord = self->crop_params.x;
    y_cord = self->crop_params.y;
    width = self->crop_params.width;
    height = self->crop_params.height;

    GST_DEBUG_OBJECT (self, "doing crop for x:%u, y:%u, width:%u, height:%u",
        x_cord, y_cord, width, height);

    switch (priv->in_vinfo->finfo->format) {
      case GST_VIDEO_FORMAT_RGB:
      case GST_VIDEO_FORMAT_BGR:
        x_cord *= 3;
        plane0_offset = ((stride * y_cord) + x_cord);
        break;

      case GST_VIDEO_FORMAT_NV12:
        plane0_offset = ((stride * y_cord) + x_cord);
        plane1_offset =
            xlnx_multiscaler_stride_align (((stride * y_cord / 2) + x_cord),
            (8 * self->ppc));
        break;

      case GST_VIDEO_FORMAT_GBR:
        plane0_offset = ((stride * y_cord) + x_cord);
        plane1_offset = ((stride * y_cord) + x_cord);
        plane2_offset = ((stride * y_cord) + x_cord);
        break;

      default:{
        GST_WARNING_OBJECT (self, "crop not supported for %d format",
            priv->in_vinfo->finfo->format);
        width = priv->in_vinfo->width;
        height = priv->in_vinfo->height;
      }
        break;
    }
  }

  msPtr = (MULTI_SCALER_DESC_STRUCT *) (priv->msPtr[0].user_ptr);
  if (GST_VIDEO_FORMAT_GBR == priv->in_vinfo->finfo->format) {
    //Multi-Scaler supports R_G_B8 only
    msPtr->msc_srcImgBuf0 = (uint64_t) priv->phy_in_0 + plane2_offset;  /* plane 2 */
    msPtr->msc_srcImgBuf1 = (uint64_t) priv->phy_in_1 + plane0_offset;  /* plane 0 */
    msPtr->msc_srcImgBuf2 = (uint64_t) priv->phy_in_2 + plane1_offset;  /* plane 1 */
  } else {
    msPtr->msc_srcImgBuf0 = (uint64_t) priv->phy_in_0 + plane0_offset;  /* plane 0 */
    msPtr->msc_srcImgBuf1 = (uint64_t) priv->phy_in_1 + plane1_offset;  /* plane 1 */
    msPtr->msc_srcImgBuf2 = (uint64_t) priv->phy_in_2 + plane2_offset;  /* plane 2 */
  }

  meta_out = gst_buffer_get_video_meta (priv->outbuf);
  if (GST_VIDEO_FORMAT_GBR == meta_out->format) {
    msPtr->msc_dstImgBuf0 = (uint64_t) priv->phy_out_2; /* plane 2 */
    msPtr->msc_dstImgBuf1 = (uint64_t) priv->phy_out_0; /* plane 0 */
    msPtr->msc_dstImgBuf2 = (uint64_t) priv->phy_out_1; /* plane 1 */
  } else {
    msPtr->msc_dstImgBuf0 = (uint64_t) priv->phy_out_0; /* plane 0 */
    msPtr->msc_dstImgBuf1 = (uint64_t) priv->phy_out_1; /* plane 1 */
    msPtr->msc_dstImgBuf2 = (uint64_t) priv->phy_out_2; /* plane 2 */
  }

  msPtr->msc_widthIn = width;   /* 4 settings from pads */
  msPtr->msc_heightIn = height;
  msPtr->msc_inPixelFmt = msc_inPixelFmt;
  msPtr->msc_strideIn = stride;
  msPtr->msc_widthOut = meta_out->width;
  msPtr->msc_heightOut = meta_out->height;

#ifdef ENABLE_PPE_SUPPORT
  if (self->ppe_on_main_buffer) {
    msPtr->msc_alpha_0 = self->alpha_r;
    msPtr->msc_alpha_1 = self->alpha_g;
    msPtr->msc_alpha_2 = self->alpha_b;
    val = (self->beta_r * (1 << 16));
    msPtr->msc_beta_0 = val;
    val = (self->beta_g * (1 << 16));
    msPtr->msc_beta_1 = val;
    val = (self->beta_b * (1 << 16));
    msPtr->msc_beta_2 = val;
  } else {
    msPtr->msc_alpha_0 = 0;
    msPtr->msc_alpha_1 = 0;
    msPtr->msc_alpha_2 = 0;
    val = (1 * (1 << 16));
    msPtr->msc_beta_0 = val;
    msPtr->msc_beta_1 = val;
    msPtr->msc_beta_2 = val;
  }
#endif
  msPtr->msc_lineRate =
      (uint32_t) ((float) ((msPtr->msc_heightIn * STEP_PRECISION) +
          ((msPtr->msc_heightOut) / 2)) / (float) msPtr->msc_heightOut);
  msPtr->msc_pixelRate =
      (uint32_t) ((float) (((msPtr->msc_widthIn) * STEP_PRECISION) +
          ((msPtr->msc_widthOut) / 2)) / (float) msPtr->msc_widthOut);
  msPtr->msc_outPixelFmt = xlnx_multiscaler_colorformat (meta_out->format);

  msPtr->msc_strideOut = (*(meta_out->stride));

  if (msPtr->msc_strideOut !=
      xlnx_multiscaler_stride_align (*(meta_out->stride), WIDTH_ALIGN)) {
    GST_WARNING_OBJECT (self, "output stide alignment mismatch");
    GST_WARNING_OBJECT (self, "required  stride = %d out_stride = %d",
        xlnx_multiscaler_stride_align (*(meta_out->stride), WIDTH_ALIGN),
        msPtr->msc_strideOut);
  }

  msPtr->msc_blkmm_hfltCoeff = priv->Hcoff[0].phy_addr;
  msPtr->msc_blkmm_vfltCoeff = priv->Vcoff[0].phy_addr;
  msPtr->msc_nxtaddr = priv->msPtr[1].phy_addr;

#ifdef DEGUB
  GST_DEBUG_OBJECT (self, "Input width: %d, height:%d stride:%d format: %d",
      msPtr->msc_widthIn, msPtr->msc_heightIn, msPtr->msc_strideIn,
      msPtr->msc_inPixelFmt);
  GST_DEBUG_OBJECT (self, "Output width: %d, height:%d stride:%d format: %d",
      msPtr->msc_widthOut, msPtr->msc_heightOut, msPtr->msc_strideOut,
      msPtr->msc_outPixelFmt);
  GST_DEBUG_OBJECT (self, "msPtr->msc_nxtaddr: %lx", msPtr->msc_nxtaddr);
#endif
  return TRUE;
}

static gboolean
vvas_xmulticrop_process (GstVvasXMultiCrop * self)
{
  GstVvasXMultiCropPrivate *priv = self->priv;
  int iret;
  uint32_t chan_id = 0;
  uint64_t desc_addr = 0;
  bool ret;
  GstMemory *mem = NULL;
  unsigned int ms_status = 0;
  int retry_count = MAX_EXEC_WAIT_RETRY_CNT;
  guint buffer_count = self->priv->sub_buffer_counter + 1;      //+1 for main buffer

  /* set for output descriptor */
  ret = vvas_xmulticrop_prepare_output_descriptor (self);
  if (!ret)
    return FALSE;

#ifdef XLNX_PCIe_PLATFORM
  ret = xlnx_multicrop_desc_sync_bo (self);
  if (!ret)
    return FALSE;

  if (!xlnx_multicrop_coeff_sync_bo (self))
    return FALSE;
#endif

  GST_LOG_OBJECT (self, "processing %u buffers", buffer_count);

  desc_addr = priv->msPtr[0].phy_addr;
  iret = vvas_xmulticrop_exec_buf (priv->dev_handle, priv->kern_handle,
      &priv->run_handle, "ulppu", buffer_count, desc_addr, NULL, NULL,
      ms_status);
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

  for (chan_id = 0; chan_id < buffer_count; chan_id++) {
    if (0 == chan_id) {
      mem = gst_buffer_get_memory (priv->outbuf, 0);
    } else {
      mem = gst_buffer_get_memory (priv->sub_buf[chan_id - 1], 0);
    }
    if (mem == NULL) {
      GST_ERROR_OBJECT (self,
          "chan-%d : failed to get memory from output buffer", chan_id);
      return FALSE;
    }
#ifdef XLNX_PCIe_PLATFORM
    gst_vvas_memory_set_sync_flag (mem, VVAS_SYNC_FROM_DEVICE);
#endif
    gst_memory_unref (mem);
  }
  return TRUE;
}


static GstBufferPool *
gst_vvas_xmulticrop_get_suitable_buffer_pool (GstBaseTransform * trans,
    GstVideoRegionOfInterestMeta * roi_meta)
{

  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstVvasXMultiCropPrivate *priv = self->priv;
  GstBufferPool *sub_pool = NULL;
  GstVideoInfo out_info = { 0 };
  gsize size_requested;
  gint idx, pool_idx = -1;
  guint out_width, out_height;

  out_width = vvas_xmulticrop_get_stride (self->subbuffer_format, roi_meta->w);
  out_width = ALIGN (out_width, WIDTH_ALIGN);
  out_height = ALIGN (roi_meta->h, HEIGHT_ALIGN);

  GST_DEBUG_OBJECT (self, "aligned width:%u, height:%u", out_width, out_height);

  if (!gst_video_info_set_format (&out_info, self->subbuffer_format,
          out_width, out_height)) {
    GST_ERROR_OBJECT (self, "failed to set video info");
    return NULL;
  }

  size_requested = GST_VIDEO_INFO_SIZE (&out_info);

  for (idx = 0; idx < MAX_SUBBUFFER_POOLS; idx++) {
    if (priv->subbuffer_pool_size[idx] > size_requested) {
      pool_idx = idx;
      break;
    }
  }

  if ((-1) == pool_idx) {
    GST_ERROR_OBJECT (self, "couldn't find any suitable subbuffer_pool");
    return NULL;
  }

  sub_pool = self->priv->subbuffer_pools[pool_idx];

  GST_DEBUG_OBJECT (self,
      "chosen sub buffer pool %p at index %d for requested buffer size %lu",
      sub_pool, pool_idx, size_requested);

  if (sub_pool == NULL) {
    GstAllocator *allocator;
    GstCaps *caps;
    gsize pool_buf_size;
    gboolean bret;
    GstStructure *config;
    GstAllocationParams alloc_params;

    caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING,
        gst_video_format_to_string (self->subbuffer_format),
        "width", G_TYPE_INT, out_width, "height", G_TYPE_INT, out_height, NULL);

    pool_buf_size = priv->subbuffer_pool_size[pool_idx];

    sub_pool = gst_vvas_buffer_pool_new (1, 1);
    GST_INFO_OBJECT (self, "allocated internal private pool %p with size %lu",
        sub_pool, pool_buf_size);

    /* Here frame buffer is required from in_mem_bank as it is expected that
     * input port is attached the bank where IP access internal data too */
    allocator = gst_vvas_allocator_new (self->dev_index, NEED_DMABUF,
        self->out_mem_bank, self->priv->kern_handle);

    config = gst_buffer_pool_get_config (sub_pool);

    gst_allocation_params_init (&alloc_params);
    alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
    alloc_params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;

    gst_buffer_pool_config_set_params (config, caps, pool_buf_size, 2, 0);
    gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
    gst_caps_unref (caps);

    if (allocator)
      gst_object_unref (allocator);

    if (!gst_buffer_pool_set_config (sub_pool, config)) {
      GST_ERROR_OBJECT (self, "failed to configure  pool");
      goto error;
    }

    GST_INFO_OBJECT (self,
        "setting config %" GST_PTR_FORMAT " on private pool %" GST_PTR_FORMAT,
        config, sub_pool);

    bret = gst_buffer_pool_set_active (sub_pool, TRUE);
    if (!bret) {
      gst_object_unref (sub_pool);
      GST_ERROR_OBJECT (self, "failed to active private pool");
      goto error;
    }
    self->priv->subbuffer_pools[pool_idx] = sub_pool;
  }

  return sub_pool;

error:
  return NULL;

}

static gboolean
gst_vvas_xmulticrop_fill_video_meta (GstBaseTransform * trans,
    const GstVideoRegionOfInterestMeta * roi_meta, GstVideoMeta * vmeta)
{

  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  guint align_stride0, align_elevation;

  align_stride0 =
      vvas_xmulticrop_get_stride (self->subbuffer_format, roi_meta->w);
  align_stride0 = ALIGN (align_stride0, WIDTH_ALIGN);
  align_elevation = ALIGN (roi_meta->h, HEIGHT_ALIGN);

  switch (self->subbuffer_format) {
    case GST_VIDEO_FORMAT_NV12:
      vmeta->stride[0] = vmeta->stride[1] = align_stride0;
      vmeta->offset[0] = 0;
      vmeta->offset[1] = vmeta->offset[0] + vmeta->stride[0] * align_elevation;
      break;

    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      vmeta->stride[0] = align_stride0;
      vmeta->offset[0] = 0;
      break;

    case GST_VIDEO_FORMAT_GBR:
      vmeta->stride[0] = vmeta->stride[1] = vmeta->stride[2] = align_stride0;
      vmeta->offset[0] = 0;
      vmeta->offset[1] = vmeta->offset[0] + vmeta->stride[0] * align_elevation;
      vmeta->offset[2] = vmeta->offset[1] + vmeta->stride[1] * align_elevation;
      break;

    default:
      GST_ERROR_OBJECT (self, "not yet supporting format %d",
          self->subbuffer_format);
      return FALSE;
  }
  return TRUE;
}

static GstBuffer *
gst_xmulticrop_prepare_subbuffer (GstBaseTransform * trans,
    GstVideoRegionOfInterestMeta * roi_meta)
{

  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstVvasXMultiCropPrivate *priv = self->priv;
  GstBufferPool *subbuffer_pool = NULL;
  GstBuffer *sub_buffer = NULL;
  GstVideoMeta *vmeta = NULL;
  GstFlowReturn fret;
  gboolean add_meta = FALSE;

  if (self->subbuffer_width && self->subbuffer_height) {
    //User has provided subbuffer's width and height, we need only 1 buffer pool
    subbuffer_pool = self->priv->subbuffer_pools[0];
  } else {
    /* In this case we need to create pool of buffer pools for different sizes
     * And then create the buffers from the most suitable pool and attach
     * metadata manually based on roi_meta
     */
    add_meta = TRUE;
    subbuffer_pool =
        gst_vvas_xmulticrop_get_suitable_buffer_pool (trans, roi_meta);
  }

  if (!subbuffer_pool) {
    GST_ERROR_OBJECT (self, "couldn't get sub buffer pool");
    return NULL;
  }

  fret = gst_buffer_pool_acquire_buffer (subbuffer_pool, &sub_buffer, NULL);
  if (fret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
        subbuffer_pool);
    goto error;
  }

  GST_DEBUG_OBJECT (self, "Got sub buffer %p from pool %p with size: %ld",
      sub_buffer, subbuffer_pool, gst_buffer_get_size (sub_buffer));

  if (add_meta) {
    vmeta = gst_buffer_add_video_meta (sub_buffer, GST_VIDEO_FRAME_FLAG_NONE,
        self->subbuffer_format, roi_meta->w, roi_meta->h);
    if (vmeta)
      gst_vvas_xmulticrop_fill_video_meta (trans, roi_meta, vmeta);
  }

  vmeta = gst_buffer_get_video_meta (sub_buffer);
  if (vmeta == NULL) {
    GST_ERROR_OBJECT (self, "video meta not present in sub buffer");
    goto error;
  }
#ifdef DEBUG
  GST_DEBUG_OBJECT (self, "vmeta: width: %d, height: %d, nplanes: %d",
      vmeta->width, vmeta->height, vmeta->n_planes);
  GST_DEBUG_OBJECT (self,
      "vmeta alignment: top:%d, bottom:%d, left:%d, right:%d",
      vmeta->alignment.padding_top, vmeta->alignment.padding_bottom,
      vmeta->alignment.padding_left, vmeta->alignment.padding_right);
  for (gint i = 0; i < vmeta->n_planes; i++) {
    GST_DEBUG_OBJECT (self, "offset: %lu, stride: %d", vmeta->offset[i],
        vmeta->stride[i]);
  }
#endif

  priv->sub_buf[priv->sub_buffer_counter] = sub_buffer;
  priv->sub_buffer_counter++;
  return sub_buffer;

error:
  if (sub_buffer) {
    gst_buffer_unref (sub_buffer);
  }
  return NULL;
}

static gboolean
vvas_xmulticrop_prepare_subbuffer_descriptor (GstVvasXMultiCrop * self,
    GstBuffer * sub_buffer, const GstVideoRegionOfInterestMeta * roi_meta,
    guint idx)
{

  MULTI_SCALER_DESC_STRUCT *msPtr;
  GstVideoMeta *meta_out;
  GstVvasXMultiCropPrivate *priv = self->priv;
  uint32_t width;
  uint32_t height;
  guint plane0_offset = 0, plane1_offset = 0, plane2_offset = 0;
  guint msptr_idx = 0;
  GstMemory *mem = NULL;
  guint64 buf_phy_addr = -1;
  guint64 phy_addr[3] = { 0 };

#ifdef ENABLE_PPE_SUPPORT
  uint32_t val;
#endif
  uint32_t msc_inPixelFmt =
      xlnx_multiscaler_colorformat (priv->in_vinfo->finfo->format);
  uint32_t stride = self->priv->meta_in_stride;

  if (roi_meta->w && roi_meta->h) {
    guint x_cord, y_cord;

    x_cord = roi_meta->x;
    y_cord = roi_meta->y;
    width = roi_meta->w;
    height = roi_meta->h;

    GST_DEBUG_OBJECT (self, "doing crop for x:%u, y:%u, width:%u, height:%u",
        x_cord, y_cord, width, height);

    switch (priv->in_vinfo->finfo->format) {
      case GST_VIDEO_FORMAT_RGB:
      case GST_VIDEO_FORMAT_BGR:
        x_cord *= 3;
        plane0_offset = ((stride * y_cord) + x_cord);
        break;

      case GST_VIDEO_FORMAT_NV12:
        plane0_offset = ((stride * y_cord) + x_cord);
        plane1_offset =
            xlnx_multiscaler_stride_align (((stride * y_cord / 2) + x_cord),
            (8 * self->ppc));
        break;

      case GST_VIDEO_FORMAT_GBR:
        plane0_offset = ((stride * y_cord) + x_cord);
        plane1_offset = ((stride * y_cord) + x_cord);
        plane2_offset = ((stride * y_cord) + x_cord);
        break;

      default:{
        GST_WARNING_OBJECT (self, "crop not supported for %d format",
            priv->in_vinfo->finfo->format);
        return FALSE;
      }
        break;
    }
  } else {
    GST_DEBUG_OBJECT (self, "width and height not specified");
    return TRUE;
  }

  meta_out = gst_buffer_get_video_meta (sub_buffer);

  msptr_idx = idx + 1;          //+1 for main buffer

  if (NULL == priv->msPtr[msptr_idx].user_ptr) {
    GST_DEBUG_OBJECT (self, "impossible");
    return FALSE;
  }

  if (self->num_taps == 12) {
    vvas_xmulticrop_prepare_coefficients_with_12tap (self, msptr_idx,
        roi_meta->w, roi_meta->h, meta_out->width, meta_out->height);
  } else {
    if (self->scale_mode == POLYPHASE) {
      float scale = (float) roi_meta->h / (float) meta_out->height;
      GST_INFO_OBJECT (self, "preparing coefficients with scaling ration %f"
          " and taps %d", scale, self->num_taps);
      xlnx_multiscaler_coff_fill (priv->Hcoff[msptr_idx].user_ptr,
          priv->Vcoff[msptr_idx].user_ptr, scale);
    }
  }

  msPtr = (MULTI_SCALER_DESC_STRUCT *) (priv->msPtr[msptr_idx].user_ptr);
  if (GST_VIDEO_FORMAT_GBR == priv->in_vinfo->finfo->format) {
    //Multi-Scaler supports R_G_B8 only
    msPtr->msc_srcImgBuf0 = (uint64_t) priv->phy_in_2 + plane2_offset;  /* plane 2 */
    msPtr->msc_srcImgBuf1 = (uint64_t) priv->phy_in_0 + plane0_offset;  /* plane 0 */
    msPtr->msc_srcImgBuf2 = (uint64_t) priv->phy_in_1 + plane1_offset;  /* plane 1 */
  } else {
    msPtr->msc_srcImgBuf0 = (uint64_t) priv->phy_in_0 + plane0_offset;  /* plane 0 */
    msPtr->msc_srcImgBuf1 = (uint64_t) priv->phy_in_1 + plane1_offset;  /* plane 1 */
    msPtr->msc_srcImgBuf2 = (uint64_t) priv->phy_in_2 + plane2_offset;  /* plane 2 */
  }

  mem = gst_buffer_get_memory (sub_buffer, 0);
  if (!mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from sub_buffer");
    return FALSE;
  }

  if (gst_is_vvas_memory (mem)) {
    buf_phy_addr = gst_vvas_allocator_get_paddr (mem);
  } else if (gst_is_dmabuf_memory (mem)) {
    vvasBOHandle bo = NULL;
    gint dma_fd = -1;

    dma_fd = gst_dmabuf_memory_get_fd (mem);
    if (dma_fd < 0) {
      GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
      return FALSE;
    }

    /* dmabuf but not from xrt */
    bo = vvas_xrt_import_bo (self->priv->dev_handle, dma_fd);
    if (bo == NULL) {
      GST_WARNING_OBJECT (self,
          "failed to get XRT BO...fall back to copy input");
    }

    GST_INFO_OBJECT (self, "received dma fd %d and its xrt BO = %p", dma_fd,
        bo);

    buf_phy_addr = vvas_xrt_get_bo_phy_addres (bo);

    if (bo != NULL)
      vvas_xrt_free_bo (bo);
  }

  buf_phy_addr = gst_vvas_allocator_get_paddr (mem);

  phy_addr[0] = buf_phy_addr;
  phy_addr[1] = 0;
  phy_addr[2] = 0;
  if (meta_out->n_planes > 1)
    phy_addr[1] = buf_phy_addr + meta_out->offset[1];
  if (meta_out->n_planes > 2)
    phy_addr[2] = buf_phy_addr + meta_out->offset[2];

  if (GST_VIDEO_FORMAT_GBR == meta_out->format) {
    msPtr->msc_dstImgBuf0 = (uint64_t) phy_addr[2];     /* plane 2 */
    msPtr->msc_dstImgBuf1 = (uint64_t) phy_addr[0];     /* plane 0 */
    msPtr->msc_dstImgBuf2 = (uint64_t) phy_addr[1];     /* plane 1 */
  } else {
    msPtr->msc_dstImgBuf0 = (uint64_t) phy_addr[0];     /* plane 0 */
    msPtr->msc_dstImgBuf1 = (uint64_t) phy_addr[1];     /* plane 1 */
    msPtr->msc_dstImgBuf2 = (uint64_t) phy_addr[2];     /* plane 2 */
  }

  msPtr->msc_widthIn = width;
  msPtr->msc_heightIn = height;
  msPtr->msc_inPixelFmt = msc_inPixelFmt;
  msPtr->msc_strideIn = stride;
  msPtr->msc_widthOut = meta_out->width;
  msPtr->msc_heightOut = meta_out->height;
#ifdef ENABLE_PPE_SUPPORT
  msPtr->msc_alpha_0 = self->alpha_r;
  msPtr->msc_alpha_1 = self->alpha_g;
  msPtr->msc_alpha_2 = self->alpha_b;
  val = (self->beta_r * (1 << 16));
  msPtr->msc_beta_0 = val;
  val = (self->beta_g * (1 << 16));
  msPtr->msc_beta_1 = val;
  val = (self->beta_b * (1 << 16));
  msPtr->msc_beta_2 = val;
#endif
  msPtr->msc_lineRate =
      (uint32_t) ((float) ((msPtr->msc_heightIn * STEP_PRECISION) +
          ((msPtr->msc_heightOut) / 2)) / (float) msPtr->msc_heightOut);
  msPtr->msc_pixelRate =
      (uint32_t) ((float) (((msPtr->msc_widthIn) * STEP_PRECISION) +
          ((msPtr->msc_widthOut) / 2)) / (float) msPtr->msc_widthOut);
  msPtr->msc_outPixelFmt = xlnx_multiscaler_colorformat (meta_out->format);
  msPtr->msc_strideOut = (*(meta_out->stride));
  msPtr->msc_blkmm_hfltCoeff = priv->Hcoff[msptr_idx].phy_addr;
  msPtr->msc_blkmm_vfltCoeff = priv->Vcoff[msptr_idx].phy_addr;
  msPtr->msc_nxtaddr = priv->msPtr[msptr_idx + 1].phy_addr;

  gst_memory_unref (mem);

#ifdef DEBUG
  GST_DEBUG_OBJECT (self, "Input width: %d, height:%d stride:%d format: %d",
      msPtr->msc_widthIn, msPtr->msc_heightIn, msPtr->msc_strideIn,
      msPtr->msc_inPixelFmt);
  GST_DEBUG_OBJECT (self, "Output width: %d, height:%d stride:%d format: %d",
      msPtr->msc_widthOut, msPtr->msc_heightOut, msPtr->msc_strideOut,
      msPtr->msc_outPixelFmt);
  GST_DEBUG_OBJECT (self, "msPtr->msc_nxtaddr: %lx", msPtr->msc_nxtaddr);
#endif
  return TRUE;
}


static gboolean
vvas_xmulticrop_prepare_crop_buffers (GstBaseTransform * trans,
    GstBuffer * inbuf)
{

  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstBuffer *sub_buffer = NULL;
  GstStructure *s;
  guint roi_meta_counter = 0;

  gpointer state = NULL;
  GstMeta *_meta;

  while ((_meta = gst_buffer_iterate_meta_filtered (inbuf, &state,
              GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE))) {

    GstVideoRegionOfInterestMeta *roi_meta;
    VvasCropParams subcrop_params = { 0 };

    if (roi_meta_counter >= (MAX_CHANNELS - 1)) {
      GST_DEBUG_OBJECT (self, "we can process only %d crop meta",
          (MAX_CHANNELS - 1));
      break;
    }

    roi_meta = (GstVideoRegionOfInterestMeta *) _meta;

    if (g_strcmp0 ("roi-crop-meta", g_quark_to_string (roi_meta->roi_type))) {
      //This is not the metadata we are looking for
      continue;
    }
    //Got the ROI crop metadata, prepare output buffer
    GST_DEBUG_OBJECT (self, "Got roi-crop-meta[%u], parent_id:%d, id:%d, "
        "x:%u, y:%u, w:%u, h:%u",
        roi_meta_counter, roi_meta->parent_id, roi_meta->id, roi_meta->x,
        roi_meta->y, roi_meta->w, roi_meta->h);

    //Adjust crop parameters;
    subcrop_params.x = roi_meta->x;
    subcrop_params.y = roi_meta->y;
    subcrop_params.width = roi_meta->w;
    subcrop_params.height = roi_meta->h;

    if (TRUE != vvas_xmulticrop_align_crop_params (trans, &subcrop_params, 4)) {
      //crop params for sub buffer are not proper, skip to next
      GST_DEBUG_OBJECT (self, "crop params are not proper, skipping meta %d",
          roi_meta->id);
      continue;
    }

    if ((subcrop_params.width < MIN_SCALAR_WIDTH) ||
        (subcrop_params.height < MIN_SCALAR_HEIGHT)) {
      GST_DEBUG_OBJECT (self, "dynamic crop width/height must be at least %u, "
          "skipping meta %d", MIN_SCALAR_HEIGHT, roi_meta->id);
      continue;
    }

    roi_meta->x = subcrop_params.x;
    roi_meta->y = subcrop_params.y;
    roi_meta->w = subcrop_params.width;
    roi_meta->h = subcrop_params.height;

    sub_buffer = gst_xmulticrop_prepare_subbuffer (trans, roi_meta);
    if (!sub_buffer) {
      GST_ERROR_OBJECT (self, "couldn't get sub buffer");
      break;
    }
    //Attach this sub_buffer into metadata
    s = gst_structure_new ("roi-buffer", "sub-buffer", GST_TYPE_BUFFER,
        sub_buffer, NULL);
    if (!s) {
      break;
    }

    /* GstStructure will take the ownership of buffer, hence sub_buffer must
     * be unrefd, As sub_buffer/priv->sub_buf[x] is needed, once it is used,
     * unref it
     */
    gst_video_region_of_interest_meta_add_param (roi_meta, s);

    //prepare descriptor
    vvas_xmulticrop_prepare_subbuffer_descriptor (self, sub_buffer, roi_meta,
        roi_meta_counter);

    roi_meta_counter++;
  }
  return TRUE;
}

static inline guint
gst_vvas_xmultirop_get_roi_meta_count (GstBuffer * buffer)
{
  gpointer state = NULL;
  GstMeta *_meta;
  guint crop_roi_meta_count = 0;

  while ((_meta = gst_buffer_iterate_meta_filtered (buffer, &state,
              GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE))) {
    GstVideoRegionOfInterestMeta *roi_meta;
    roi_meta = (GstVideoRegionOfInterestMeta *) _meta;
    if (!g_strcmp0 ("roi-crop-meta", g_quark_to_string (roi_meta->roi_type))) {
      crop_roi_meta_count++;
    }
  }
  return crop_roi_meta_count;
}

static GstFlowReturn
gst_vvas_xmulticrop_generate_output (GstBaseTransform * trans,
    GstBuffer ** outbuf)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstVvasXMultiCropPrivate *priv = self->priv;
  GstBuffer *inbuf = NULL;
  GstBuffer *cur_outbuf = NULL;
  GstMeta *in_meta;
  GstFlowReturn fret = GST_FLOW_OK;
  guint roi_meta_count, idx;
  gboolean bret;

  *outbuf = NULL;
  if (NULL == trans->queued_buf) {
    return GST_FLOW_OK;
  }

  inbuf = trans->queued_buf;
  trans->queued_buf = NULL;

  GST_DEBUG_OBJECT (self, "received %" GST_PTR_FORMAT, inbuf);

  //Prepare input buffer
  bret = vvas_xmulticrop_prepare_input_buffer (trans, &inbuf);
  if (!bret) {
    goto error;
  }

  if (!inbuf)
    return GST_FLOW_OK;

  fret =
      GST_BASE_TRANSFORM_CLASS
      (gst_vvas_xmulticrop_parent_class)->prepare_output_buffer (trans, inbuf,
      &cur_outbuf);
  if (fret != GST_FLOW_OK)
    goto error;

  /* prepare output buffer for main buffer processing */
  bret = vvas_xmulticrop_prepare_output_buffer (self, cur_outbuf);
  if (!bret)
    goto error;

  roi_meta_count =
      gst_buffer_get_n_meta (inbuf, GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);

  if (roi_meta_count) {
    roi_meta_count = gst_vvas_xmultirop_get_roi_meta_count (inbuf);
    GST_DEBUG_OBJECT (self, "input buffer has %u roi_meta", roi_meta_count);

    //we can handle only MAX_CHANNELS - 1 roi_meta
    roi_meta_count = (roi_meta_count > (MAX_CHANNELS - 1))
        ? (MAX_CHANNELS - 1) : roi_meta_count;

    if (roi_meta_count >= priv->internal_buf_counter) {
      /* Need to allocate more internal buffers, till now we have allocated
       * internal_buf_counter number of buffers
       */
      guint required_internal_buffers =
          (roi_meta_count - priv->internal_buf_counter) + 1;

      GST_LOG_OBJECT (self, "Need to allocate %u more internal buffers",
          required_internal_buffers);
      if (FALSE == vvas_xmulticrop_allocate_internal_buffers (self,
              required_internal_buffers)) {
        GST_ERROR_OBJECT (self, "failed to allocate internal buffer");
        fret = GST_FLOW_ERROR;
        goto error;
      }
    }
    vvas_xmulticrop_prepare_crop_buffers (trans, inbuf);
  }

  bret = vvas_xmulticrop_process (self);
  if (!bret)
    goto error;

  GST_DEBUG_OBJECT (self, "copying the metadta");
  gst_buffer_copy_into (cur_outbuf, inbuf,
      (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_META |
          GST_BUFFER_COPY_TIMESTAMPS), 0, -1);

  /* Scaling of input vvas metadata based on output resolution */
  in_meta = gst_buffer_get_meta (inbuf, gst_inference_meta_api_get_type ());

  if (in_meta) {
    GstVideoMetaTransform trans = { self->priv->in_vinfo, priv->out_vinfo };
    GQuark scale_quark = gst_video_meta_transform_scale_get_quark ();

    GST_DEBUG_OBJECT (self, "attaching scaled inference metadata");
    in_meta->info->transform_func (cur_outbuf, (GstMeta *) in_meta,
        inbuf, scale_quark, &trans);
  }

  /* Copying of input HDR metadata */
  in_meta = (GstMeta *) gst_buffer_get_vvas_hdr_meta (inbuf);
  if (in_meta) {
    GstMetaTransformCopy copy_data = { FALSE, 0, -1 };

    GST_DEBUG_OBJECT (self, "copying input HDR metadata");
    in_meta->info->transform_func (cur_outbuf, (GstMeta *) in_meta,
        inbuf, _gst_meta_transform_copy, &copy_data);
  }

  if (self->priv->need_copy) {
    GstBuffer *new_outbuf;
    GstVideoFrame new_frame, out_frame;
    new_outbuf =
        gst_buffer_new_and_alloc (GST_VIDEO_INFO_SIZE (priv->out_vinfo));
    if (!new_outbuf) {
      GST_ERROR_OBJECT (self, "failed to allocate output buffer");
      fret = GST_FLOW_ERROR;
      goto error;
    }

    gst_video_frame_map (&out_frame, priv->out_vinfo, cur_outbuf, GST_MAP_READ);
    gst_video_frame_map (&new_frame, priv->out_vinfo, new_outbuf,
        GST_MAP_WRITE);
    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, self,
        "slow copy data from %p to %p", cur_outbuf, new_outbuf);
    gst_video_frame_copy (&new_frame, &out_frame);
    gst_video_frame_unmap (&out_frame);
    gst_video_frame_unmap (&new_frame);

    gst_buffer_copy_into (new_outbuf, cur_outbuf, GST_BUFFER_COPY_METADATA, 0,
        -1);
    gst_buffer_unref (cur_outbuf);
    cur_outbuf = new_outbuf;
  }

  GST_DEBUG_OBJECT (self, "output %" GST_PTR_FORMAT, cur_outbuf);

  *outbuf = cur_outbuf;
  fret = GST_FLOW_OK;

error:
  gst_buffer_unref (inbuf);
  /* sub_buffer's were attached into the ROI metadata's structure, hence
   * structure has taken the reference to these buffers, we can safely
   * unref them to avoid any memory leak.
   */
  for (idx = 0; idx < priv->sub_buffer_counter; idx++) {
    if (priv->sub_buf[idx]) {
      gst_buffer_unref (priv->sub_buf[idx]);
      priv->sub_buf[idx] = NULL;
    }
  }
  priv->sub_buffer_counter = 0;
  return fret;
}

static GstFlowReturn
gst_vvas_xmulticrop_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  return GST_FLOW_OK;
}

static void
gst_vvas_xmulticrop_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (object);
  GST_DEBUG_OBJECT (self, "set_property, id: %u", property_id);

  switch (property_id) {
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
    case PROP_AVOID_OUTPUT_COPY:
      self->avoid_output_copy = g_value_get_boolean (value);
      break;
    case PROP_ENABLE_PIPELINE:
      self->enabled_pipeline = g_value_get_boolean (value);
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

    case PROP_CROP_X:
      self->crop_params.x = g_value_get_uint (value);
      break;
    case PROP_CROP_Y:
      self->crop_params.y = g_value_get_uint (value);
      break;
    case PROP_CROP_WIDTH:
      self->crop_params.width = g_value_get_uint (value);
      break;
    case PROP_CROP_HEIGHT:
      self->crop_params.height = g_value_get_uint (value);
      break;
    case PROP_SUBBUFFER_WIDTH:
      self->subbuffer_width = g_value_get_uint (value);
      break;
    case PROP_SUBBUFFER_HEIGHT:
      self->subbuffer_height = g_value_get_uint (value);
      break;
    case PROP_SUBBUFFER_FORMAT:
      self->subbuffer_format = g_value_get_enum (value);
      break;
    case PROP_PPE_ON_MAIN_BUFFER:
      self->ppe_on_main_buffer = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_vvas_xmulticrop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (object);

  GST_DEBUG_OBJECT (self, "get_property, id: %u", prop_id);

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
    case PROP_AVOID_OUTPUT_COPY:
      g_value_set_boolean (value, self->avoid_output_copy);
      break;
    case PROP_ENABLE_PIPELINE:
      g_value_set_boolean (value, self->enabled_pipeline);
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

    case PROP_CROP_X:
      g_value_set_uint (value, self->crop_params.x);
      break;
    case PROP_CROP_Y:
      g_value_set_uint (value, self->crop_params.y);
      break;
    case PROP_CROP_WIDTH:
      g_value_set_uint (value, self->crop_params.width);
      break;
    case PROP_CROP_HEIGHT:
      g_value_set_uint (value, self->crop_params.height);
      break;
    case PROP_SUBBUFFER_WIDTH:
      g_value_set_uint (value, self->subbuffer_width);
      break;
    case PROP_SUBBUFFER_HEIGHT:
      g_value_set_uint (value, self->subbuffer_height);
      break;
    case PROP_SUBBUFFER_FORMAT:
      g_value_set_enum (value, self->subbuffer_format);
      break;
    case PROP_PPE_ON_MAIN_BUFFER:
      g_value_set_boolean (value, self->ppe_on_main_buffer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_xmulticrop_finalize (GObject * object)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (object);

  GST_DEBUG_OBJECT (self, "finalize");

  if (self->kern_name) {
    g_free (self->kern_name);
  }

  if (self->xclbin_path) {
    g_free (self->xclbin_path);
  }

  if (self->priv->in_vinfo) {
    gst_video_info_free (self->priv->in_vinfo);
  }

  if (self->priv->out_vinfo) {
    gst_video_info_free (self->priv->out_vinfo);
  }

  G_OBJECT_CLASS (gst_vvas_xmulticrop_parent_class)->finalize (object);
}

static GstBufferPool *
vvas_xmulticrop_allocate_buffer_pool (GstVvasXMultiCrop * self, guint width,
    guint height, GstVideoFormat format)
{

  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstCaps *caps = NULL;
  GstVideoAlignment align;

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, gst_video_format_to_string (format),
      "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  pool = gst_vvas_buffer_pool_new (WIDTH_ALIGN, HEIGHT_ALIGN);
  GST_LOG_OBJECT (self, "allocated buffer pool %p", pool);

  config = gst_buffer_pool_get_config (pool);
  gst_video_alignment_reset (&align);
  align.padding_top = 0;
  align.padding_left = 0;
  align.padding_right = vvas_xmulticrop_get_padding_right (self, &info);
  align.padding_bottom = ALIGN (GST_VIDEO_INFO_HEIGHT (&info), HEIGHT_ALIGN)
      - GST_VIDEO_INFO_HEIGHT (&info);
  gst_video_info_align (&info, &align);

  GST_DEBUG_OBJECT (self, "Align padding: top:%d, bottom:%d, left:%d, right:%d",
      align.padding_top, align.padding_bottom, align.padding_left,
      align.padding_right);

  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_set_video_alignment (config, &align);

  allocator = gst_vvas_allocator_new (self->dev_index,
      NEED_DMABUF, self->out_mem_bank, self->priv->kern_handle);

  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
  alloc_params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;
  GST_INFO_OBJECT (self,
      "allocated %" GST_PTR_FORMAT " allocator at mem bank %d", allocator,
      self->in_mem_bank);

  gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (&info),
      3, 0);

  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (allocator)
    gst_object_unref (allocator);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config on input pool");
    goto error;
  }

  GST_INFO_OBJECT (self, "allocated %p sub_buffer pool with size: %lu",
      pool, GST_VIDEO_INFO_SIZE (&info));

  gst_caps_unref (caps);

  /* activate cropped buffer pool */
  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "failed to activate pool");
    goto error;
  } else {
    GST_LOG_OBJECT (self, "%p buffer pool activated", pool);
  }
  return pool;

error:
  if (pool) {
    gst_clear_object (&pool);
  }
  gst_caps_unref (caps);
  return NULL;
}

static GstStateChangeReturn
gst_vvas_xmulticrop_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (element);
  GstVvasXMultiCropPrivate *priv = self->priv;
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
      if (self->enabled_pipeline) {
        priv->is_first_frame = TRUE;
        priv->copy_inqueue =
            g_async_queue_new_full ((void (*)(void *)) gst_buffer_unref);
        priv->copy_outqueue =
            g_async_queue_new_full ((void (*)(void *)) gst_buffer_unref);

        priv->input_copy_thread = g_thread_new ("multicrop-input-copy-thread",
            vvas_xmulticrop_input_copy_thread, self);
      }
      break;
    }
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_vvas_xmulticrop_parent_class)->change_state
      (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      gint idx;
      priv->is_first_frame = FALSE;

      if (self->enabled_pipeline) {
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

      if (self->priv->input_pool
          && gst_buffer_pool_is_active (self->priv->input_pool)) {
        if (!gst_buffer_pool_set_active (self->priv->input_pool, FALSE))
          GST_ERROR_OBJECT (self, "failed to deactivate pool %" GST_PTR_FORMAT,
              self->priv->input_pool);
        gst_clear_object (&self->priv->input_pool);
        self->priv->input_pool = NULL;
      }

      for (idx = 0; idx < MAX_SUBBUFFER_POOLS; idx++) {
        GstBufferPool *sub_pool = priv->subbuffer_pools[idx];
        if (sub_pool && gst_buffer_pool_is_active (sub_pool)) {
          if (!gst_buffer_pool_set_active (sub_pool, FALSE))
            GST_ERROR_OBJECT (self,
                "failed to deactivate pool %" GST_PTR_FORMAT, sub_pool);
          gst_clear_object (&sub_pool);
          priv->subbuffer_pools[idx] = NULL;
        }
      }

      vvas_xmulticrop_free_internal_buffers (self);
      self->priv->is_internal_buf_allocated = FALSE;
      vvas_xmulticrop_destroy_context (self);
      break;
    }

    default:
      break;
  }
  return ret;
}

static gboolean
vvas_xmulticrop_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "vvas_xmulticrop", GST_RANK_NONE,
      GST_TYPE_VVAS_XMULTICROP);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xmulticrop,
    "Xilinx multi crop plugin to crop buffers statically and dynamically",
    vvas_xmulticrop_plugin_init, VVAS_API_VERSION, "MIT/X11",
    "Xilinx VVAS SDK plugin", "https://www.xilinx.com/")
