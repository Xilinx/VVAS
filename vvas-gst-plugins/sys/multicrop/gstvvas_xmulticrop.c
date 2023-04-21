/*
 * Copyright (C) 2022 Xilinx, Inc.  All rights reserved.
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
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
#include <vvas_core/vvas_device.h>
#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include "gstvvas_xmulticrop.h"

#include <gst/vvas/gstvvascoreutils.h>
#include <vvas_core/vvas_context.h>
#include <vvas_core/vvas_common.h>
#include <vvas_core/vvas_scaler.h>

#ifdef XLNX_PCIe_PLATFORM
/** @def DEFAULT_DEVICE_INDEX
 *  @brief Default device index
 */
#define DEFAULT_DEVICE_INDEX -1

/** @def DEFAULT_KERNEL_NAME
 *  @brief Default kernel name
 */
#define DEFAULT_KERNEL_NAME "image_processing:{image_processing_1}"

/** @def NEED_DMABUF
 *  @brief Need DMA buffer
 */
#define NEED_DMABUF 0
#else

/** @def DEFAULT_DEVICE_INDEX
 *  @brief Default device index
 */
#define DEFAULT_DEVICE_INDEX 0

/** @def DEFAULT_KERNEL_NAME
 *  @brief Default kernel name
 */
#define DEFAULT_KERNEL_NAME "image_processing:{image_processing_1}"

/** @def NEED_DMABUF
 *  @brief Need DMA buffer
 */
#define NEED_DMABUF 1
#endif

#ifndef DEFAULT_MEM_BANK
/** @def DEFAULT_MEM_BANK
 *  @brief Default memory bank
 */
#define DEFAULT_MEM_BANK 0
#endif

/** @def VVAS_XMULTICROP_AVOID_OUTPUT_COPY_DEFAULT
 *  @brief Default value for avoiding copy of output buffer
 */
#define VVAS_XMULTICROP_AVOID_OUTPUT_COPY_DEFAULT FALSE

/** @def VVAS_XMULTICROP_ENABLE_PIPELINE_DEFAULT
 *  @brief Default value for enabling pipeline
 */
#define VVAS_XMULTICROP_ENABLE_PIPELINE_DEFAULT FALSE
/** @def VVAS_XMULTICROP_ENABLE_SOFTWARE_SCALING_DEFAULT
 *  @brief Default value for software-scaling property.
 */
#define VVAS_XMULTICROP_ENABLE_SOFTWARE_SCALING_DEFAULT FALSE

/** @def STOP_COMMAND
 *  @brief Stop command
 */
#define STOP_COMMAND ((gpointer)GINT_TO_POINTER (g_quark_from_string("STOP")))

/** @def VVAS_XMULTICROP_PPE_ON_MAIN_BUF_DEFAULT
 *  @brief Default value for Need pre-processing on main/output buffer
 */
#define VVAS_XMULTICROP_PPE_ON_MAIN_BUF_DEFAULT FALSE

/** @def WIDTH_ALIGN
 *  @brief Width alignment requirement for Scaler IP
 */
#define WIDTH_ALIGN (8 * self->ppc)

/** @def HEIGHT_ALIGN
 *  @brief Height alignment requirement for Scaler IP
 */
#define HEIGHT_ALIGN 1

/** @def DEFAULT_CROP_PARAMS
 *  @brief Default crop parameters (x, y, width, height)
 */
#define DEFAULT_CROP_PARAMS 0

/** @def MAX_SUBBUFFER_POOLS
 *  @brief Maximum Sub Buffer pools for dynamic crop buffers
 */
#define MAX_SUBBUFFER_POOLS 10

/** @def MIN_SCALAR_WIDTH
 *  @brief This is the minimum width which can be processed using MultiScaler IP
 */
#define MIN_SCALAR_WIDTH    16

/** @def MIN_SCALAR_HEIGHT
 *  @brief This is the minimum height which can be processed using MultiScaler IP
 */
#define MIN_SCALAR_HEIGHT   16

/** @def DEFAULT_VVAS_DEBUG_LEVEL
 *  @brief Default debug level for VVAS CORE.
 */
#define DEFAULT_VVAS_DEBUG_LEVEL        2

/** @def DEFAULT_VVAS_SCALER_DEBUG_LEVEL
 *  @brief Default debug level for VVAS CORE Scaler.
 */
#define DEFAULT_VVAS_SCALER_DEBUG_LEVEL 2

/**
 *  @brief Defines a static GstDebugCategory global variable "gst_vvas_xmulticrop_debug_category"
 */
GST_DEBUG_CATEGORY_STATIC (gst_vvas_xmulticrop_debug_category);

/** @def GST_CAT_DEFAULT
 *  @brief Setting gst_vvas_xmulticrop_debug_category as default debug category for logging
 */
#define GST_CAT_DEFAULT gst_vvas_xmulticrop_debug_category
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

/* Function prototypes */
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
static GstBufferPool *vvas_xmulticrop_allocate_buffer_pool (GstVvasXMultiCrop *
    self, guint width, guint height, GstVideoFormat format);

/** @enum VvasXMultiCropProperties
 *  @brief Contains properties related to vvas_xmulticrop
 */
typedef enum
{
  /** Default property installed by GStreamer */
  PROP_0,
  /** Property to set/get XCLBIN location */
  PROP_XCLBIN_LOCATION,
  /** Property to set/get kernel name */
  PROP_KERN_NAME,
#ifdef XLNX_PCIe_PLATFORM
  /** Property to set/get device index */
  PROP_DEVICE_INDEX,
#endif
  /** Property to set/get input memory bank */
  PROP_IN_MEM_BANK,
  /** Property to set/get output memory bank */
  PROP_OUT_MEM_BANK,
  /** Property to set/get PPC */
  PROP_PPC,
  /** Property to set/get scale mode */
  PROP_SCALE_MODE,
  /** Property to set/get num of taps */
  PROP_NUM_TAPS,
  /** Property to set/get coeff loading type */
  PROP_COEF_LOADING_TYPE,
  /** Property to set/get avoid output copy flag */
  PROP_AVOID_OUTPUT_COPY,
  /** Property to set/get Enable pipeline mode */
  PROP_ENABLE_PIPELINE,
#ifdef ENABLE_PPE_SUPPORT
  /** Property to set/get pre-processing alpha r value */
  PROP_ALPHA_R,
  /** Property to set/get pre-processing alpha g value */
  PROP_ALPHA_G,
  /** Property to set/get pre-processing alpha b value */
  PROP_ALPHA_B,
  /** Property to set/get pre-processing beta r value */
  PROP_BETA_R,
  /** Property to set/get pre-processing beta g value */
  PROP_BETA_G,
  /** Property to set/get pre-processing beta b value */
  PROP_BETA_B,
#endif
  /** Property to set/get static crop x */
  PROP_CROP_X,
  /** Property to set/get static crop y */
  PROP_CROP_Y,
  /** Property to set/get static crop width */
  PROP_CROP_WIDTH,
  /** Property to set/get static crop height */
  PROP_CROP_HEIGHT,
  /** Property to set/get width of dynamically cropped buffers */
  PROP_SUBBUFFER_WIDTH,
  /** Property to set/get height of dynamically cropped buffers */
  PROP_SUBBUFFER_HEIGHT,
  /** Property to set/get format of dynamically cropped buffers */
  PROP_SUBBUFFER_FORMAT,
  /** Property to set/get Enable pre-processing on main buffer mode */
  PROP_PPE_ON_MAIN_BUFFER,
  /** Software scaling */
  PROP_SOFTWARE_SCALING,
} VvasXMultiCropProperties;

/** @enum ColorDomain
 *  @brief Contains enum for different color domains
 */
typedef enum ColorDomain
{
  /** YUV color domain */
  COLOR_DOMAIN_YUV,
  /** RGB color domain */
  COLOR_DOMAIN_RGB,
  /** GRAY color domain */
  COLOR_DOMAIN_GRAY,
  /** Unknown color domain */
  COLOR_DOMAIN_UNKNOWN,
} ColorDomain;

/** @struct GstVvasXMultiCropPrivate
 *  @brief  Holds private members related VVAS MultiCrop instance
 */
struct _GstVvasXMultiCropPrivate
{
  /** Input Video info */
  GstVideoInfo *in_vinfo;
  /** Output video info */
  GstVideoInfo *out_vinfo;
  /** device handle */
  vvasDeviceHandle dev_handle;
  /** Output buffer */
  GstBuffer *outbuf;
  /** Dynamically cropped buffers */
  GstBuffer *sub_buf[MAX_CHANNELS - 1];
  /** dynamic crop buffer counter */
  guint sub_buffer_counter;
  /** Input buffer pool */
  GstBufferPool *input_pool;
  /* when user has set sub buffer(dynamically cropped buffer) output resolution,
   * we only need 1 buffer pool, hence index 0 will only be used, otherwise
   * all MAX_SUBBUFFER_POOLS pools will be created as and when
   * need arises.
   */
  /** buffer pools for dynamic crop */
  GstBufferPool *subbuffer_pools[MAX_SUBBUFFER_POOLS];
  /** size of buffer pools for dynamic crop */
  guint subbuffer_pool_size[MAX_SUBBUFFER_POOLS];
  /** validate import */
  gboolean validate_import;
  /** Need to copy output buffer */
  gboolean need_copy;
  /** Thread for copying input buffer */
  GThread *input_copy_thread;
  /** input queue for \p input_copy_thread */
  GAsyncQueue *copy_inqueue;
  /** output queue for \p input_copy_thread */
  GAsyncQueue *copy_outqueue;
  /** First frame */
  gboolean is_first_frame;

  /** VVAS Core Context */
  VvasContext *vvas_ctx;
  /** VVAS Core Scaler Context */
  VvasScaler *vvas_scaler;
  /** Reference of input VvasVideoFrame */
  VvasVideoFrame *input_frame;
  /** Reference of output VvasVideoFrames */
  VvasVideoFrame *output_frames[MAX_CHANNELS];
};

/**
 *  @brief Defines sink pad's template
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGBx, YUY2, r210, Y410, NV16, NV12, RGB, v308, I422_10LE, GRAY8, \
  NV12_10LE32, BGRx, GRAY10_LE32, BGRx, UYVY, BGR, RGBA, BGRA, I420, GBR}")));

/**
 *  @brief Defines source pad's template
 */
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
/** @def VVAS_XMULTICROP_DEFAULT_NUM_TAPS
 *  @brief Default number of tap points
 */
#define VVAS_XMULTICROP_DEFAULT_NUM_TAPS 12

/** @def VVAS_XMULTICROP_DEFAULT_PPC
 *  @brief Default PPC(pixel per clock) values
 */
#define VVAS_XMULTICROP_DEFAULT_PPC 4

/** @def VVAS_XMULTICROP_SCALE_MODE
 *  @brief Default Scale Mode
 */
#define VVAS_XMULTICROP_SCALE_MODE 2
#else /* default taps for Embedded platform 6 */
/** @def VVAS_XMULTICROP_DEFAULT_NUM_TAPS
 *  @brief Default number of tap points
 */
#define VVAS_XMULTICROP_DEFAULT_NUM_TAPS 6

/** @def VVAS_XMULTICROP_DEFAULT_PPC
 *  @brief Default PPC(pixel per clock) values
 */
#define VVAS_XMULTICROP_DEFAULT_PPC 2

/** @def VVAS_XMULTICROP_SCALE_MODE
 *  @brief Default Scale Mode
 */
#define VVAS_XMULTICROP_SCALE_MODE 0
#endif

/**
 *  @brief Get VVAS_XMULTICROP_NUM_TAPS_TYPE GType
 */
#define VVAS_XMULTICROP_NUM_TAPS_TYPE (vvas_xmulticrop_num_taps_type ())

/**
 *  @brief Get VVAS_XMULTICROP_PPC_TYPE GType
 */
#define VVAS_XMULTICROP_PPC_TYPE (vvas_xmulticrop_ppc_type ())

/**
 *  @brief Get VVAS_XMULTICROP_SUBBUFFER_FORMAT_TYPE GType
 */
#define VVAS_XMULTICROP_SUBBUFFER_FORMAT_TYPE (vvas_xmulticrop_subbuffer_format_type ())

/**
 *  @fn static GstCaps *vvas_xmulticrop_generate_caps (void)
 *  @return Returns GstCaps pointer.
 *  @brief This function generates GstCaps based on scaler capabilities
 */
static GstCaps *
vvas_xmulticrop_generate_caps (void)
{
  GstStructure *s;
  GValue format = G_VALUE_INIT;
  GstCaps *caps = NULL;
  int i;
  VvasScalerProp sc_prop = { 0, };
  VvasReturnType vret;

  vret = vvas_scaler_prop_get (NULL, &sc_prop);
  if (VVAS_IS_ERROR (vret)) {
    return caps;
  }

  if (sc_prop.n_fmts) {
    g_value_init (&format, GST_TYPE_LIST);

    s = gst_structure_new ("video/x-raw",
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

    for (i = 0; i < sc_prop.n_fmts; i++) {
      GValue v = G_VALUE_INIT;
      g_value_init (&v, G_TYPE_STRING);

      g_value_set_static_string (&v,
          gst_video_format_to_string (gst_coreutils_get_gst_fmt_from_vvas
              (sc_prop.supported_fmts[i])));
      gst_value_list_append_and_take_value (&format, &v);
    }

    gst_structure_take_value (s, "format", &format);
    caps = gst_caps_new_full (s, NULL);

  }

  return caps;
}

/**
 *  @fn static guint vvas_xmulticrop_get_stride (GstVideoFormat format, guint width)
 *  @param [in] format  - GstVideoFormat
 *  @param [in] width   - width
 *  @return stride
 *  @brief  This function calculates stride for given color format and width and
 *          aligns them by 4.
 */
static guint
vvas_xmulticrop_get_stride (GstVideoFormat format, guint width)
{
  guint stride = 0;
  /* Get the stride for different color format, also align this stride value
   * by 4 bytes
   */
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

/**
 *  @fn static ColorDomain vvas_xmulticrop_get_color_domain (const gchar * color_format)
 *  @param [in] color_format    - GStreamer color format
 *  @return ColorDomain value
 *  @brief  This function find the color domain from the passed GStreamer video format
 */
static ColorDomain
vvas_xmulticrop_get_color_domain (const gchar * color_format)
{
  GstVideoFormat format;
  const GstVideoFormatInfo *format_info;
  ColorDomain color_domain = COLOR_DOMAIN_UNKNOWN;

  format = gst_video_format_from_string (color_format);
  format_info = gst_video_format_get_info (format);
  /* check whether video format belongs to YUV, RGB or GRAY color domain */
  if (GST_VIDEO_FORMAT_INFO_IS_YUV (format_info)) {
    color_domain = COLOR_DOMAIN_YUV;
  } else if (GST_VIDEO_FORMAT_INFO_IS_RGB (format_info)) {
    color_domain = COLOR_DOMAIN_RGB;
  } else if (GST_VIDEO_FORMAT_INFO_IS_GRAY (format_info)) {
    color_domain = COLOR_DOMAIN_GRAY;
  }

  return color_domain;
}

/**
 *  @fn static GType vvas_xmulticrop_num_taps_type (void)
 *  @param void
 *  @return Returns the GEnumValue for all scaler filter co-efficient taps.
 *  @brief  This function just returns the GEnumValue for all taps supported by
 *          scaler IP filter co-efficients.
 */
static GType
vvas_xmulticrop_num_taps_type (void)
{
  static GType num_tap = 0;
  /* Register number of taps enum type */
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

/**
 *  @fn static GType vvas_xmulticrop_ppc_type (void)
 *  @param void
 *  @return Returns the GEnumValue for all PPC supported by scaler IP.
 *  @brief  This function just returns the GEnumValue for all pixel per clock(PPC)
 *          supported by scaler IP co-efficients.
 */
static GType
vvas_xmulticrop_ppc_type (void)
{
  static GType num_ppc = 0;
  /* Register PPC (pixel per clock) enum type */
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

/**
 *  @fn static GType vvas_xmulticrop_subbuffer_format_type (void)
 *  @param void
 *  @return Returns the GEnumValue for all supported dynamic buffers format(sub-buffer format).
 *  @brief  This function just returns the GEnumValue for all supported sub-buffer formats.
 */
static GType
vvas_xmulticrop_subbuffer_format_type (void)
{
  static GType sub_buffer_fromat = 0;
  /* Register subbuffer (dynamic crop buffer) format enum type */
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
/** @def VVAS_XMULTICROP_DEFAULT_COEF_LOAD_TYPE
 *  @brief   Default value for coefficients loading type
 */
#define VVAS_XMULTICROP_DEFAULT_COEF_LOAD_TYPE COEF_AUTO_GENERATE
#else
/** @def VVAS_XMULTICROP_DEFAULT_COEF_LOAD_TYPE
 *  @brief   Default value for coefficients loading type
 */
#define VVAS_XMULTICROP_DEFAULT_COEF_LOAD_TYPE COEF_FIXED
#endif

/**
 *  @brief Get VVAS_XMULTICROP_COEF_LOAD_TYPE GType
 */
#define VVAS_XMULTICROP_COEF_LOAD_TYPE (vvas_xmulticrop_coef_load_type ())

/**
 *  @fn static GType vvas_xmulticrop_coef_load_type (void)
 *  @param void
 *  @return Returns the GEnumValue for showing co-efficients types supported by scaler IP.
 *  @brief  This function just returns the GEnumValue for showing co-efficients types supported by scaler IP.
 */
static GType
vvas_xmulticrop_coef_load_type (void)
{
  static GType load_type = 0;
  /* Register filter co-efficient load type enum type */
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

/**
 *  @fn static uint32_t xlnx_multiscaler_stride_align (uint32_t stride_in, uint16_t alignment)
 *  @param [in] stride_in   - Input stride
 *  @param [in] alignment   - alignment requirement
 *  @return aligned value
 *  @brief  This function aligns the stride_in value to the next aligned integer value
 */
static inline uint32_t
xlnx_multiscaler_stride_align (uint32_t stride_in, uint16_t alignment)
{
  uint32_t stride;
  /* Align the passed value (stride_in) by passed alignment value, this will
   * return the next aligned integer */
  stride = (((stride_in) + alignment - 1) / alignment) * alignment;
  return stride;
}

/**
 *  @fn static gboolean is_colordomain_matching_with_peer (GstCaps * peercaps, ColorDomain in_color_domain)
 *  @param [in] peercaps        - Peer's GstCaps
 *  @param [in] in_color_domain - input color domain
 *  @return TRUE if color domain matches FALSE otherwise
 *  @brief  This function checks whether in_color_domain matches with the color domain of peer or not.
 */
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
        /* color domain matched with input color domain */
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
          /* color domain matched with input color domain */
          break;
        }
      }
    }
  }
  return color_domain_matched;
}

/**
 *  @fn static gboolean vvas_xmulticrop_align_crop_params (GstBaseTransform * trans,
 *                                                         VvasCropParams * crop_params,
 *                                                         guint width_alignment)
 *  @param [in] trans               - GstVvasXMultiCrop object typecasted to GstBaseTransform
 *  @param [in, out] crop_params    - crop parameter to be validated and aligned
 *  @param [in] width_alignment     - width alignment
 *  @return TRUE on success\n FALSE on failure
 *  @brief  This function validates and aligns the given crop_params to the given width_alignment.
 *  @details  This function will validate crop parameters only when any of the
 *            parameters are non zero.
 *            If x is non zero, and width is 0; width will be auto calculated,
 *            If y is non zero, and height is 0; height will be auto calculated.
 */
static gboolean
vvas_xmulticrop_align_crop_params (GstBaseTransform * trans,
    VvasCropParams * crop_params, guint width_alignment)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);

  /* Validate and align crops parameters only when x, y, width or
   * height value is set */
  if (crop_params->x || crop_params->y ||
      crop_params->width || crop_params->height) {

    guint input_width, input_height, x_aligned, crop_width;

    input_width = self->priv->in_vinfo->width;
    input_height = self->priv->in_vinfo->height;

    if ((crop_params->x >= input_width) || (crop_params->y >= input_height)) {
      /* x can't be greater than the input width and y can't be greater than
       * the input height */
      GST_ERROR_OBJECT (self, "crop x or y coordinate can't be >= "
          " input width and height");
      return FALSE;
    }

    /* If user hasn't set crop width, we will auto consider
     * the crop width */
    if (!crop_params->width) {
      crop_params->width = input_width - crop_params->x;
    }

    /* If user hasn't set crop height, we will auto consider
     * the crop height */
    if (!crop_params->height) {
      crop_params->height = input_height - crop_params->y;
    }
    crop_width = crop_params->width;
    if (!self->software_scaling) {
      /* Align values as per the IP requirement
       * Align x by 8 * PPC, width by PPC, y and height by 2
       */
      x_aligned = (crop_params->x / (8 * self->ppc)) * (8 * self->ppc);
      crop_params->width = crop_params->x + crop_params->width - x_aligned;
      crop_params->width = xlnx_multiscaler_stride_align (crop_params->width,
          width_alignment);
    } else {
      /* Align x to an even number for computational ease */
      x_aligned = (crop_params->x / 2) * 2;
      crop_params->width = crop_params->x + crop_params->width - x_aligned;
      crop_params->width = xlnx_multiscaler_stride_align (crop_params->width,
          2);
    }

    crop_params->x = x_aligned;
    crop_params->y = (crop_params->y / 2) * 2;
    crop_params->height =
        xlnx_multiscaler_stride_align (crop_params->height, 2);

    GST_INFO_OBJECT (self, "crop aligned params: x:%u, y:%u, width:%u, "
        "height: %u, extra width: %u",
        crop_params->x, crop_params->y, crop_params->width, crop_params->height,
        (crop_params->width - crop_width));

    /* aligned values must not go beyond boundaries */
    if (((crop_params->x + crop_params->width) > input_width) ||
        ((crop_params->y + crop_params->height) > input_height)) {
      GST_ERROR_OBJECT (self, "x + width or y + height can't be greater "
          "than input width and height");
      return FALSE;
    }
  }
  return TRUE;
}

/**
 *  @fn static void gst_vvas_xmulticrop_class_init (GstVvasXMultiCropClass * klass)
 *  @param [in] klass   - Handle to GstVvasXMultiCropClass
 *  @return None
 *  @brief  Add properties and signals of GstVvasXMultiCrop to parent GObjectClass
 *          and overrides function pointers present in itself and/or its parent class structures
 *  @details  This function publishes properties those can be set/get from application
 *            on GstVvasXMultiCrop object. And, while publishing a property it also declares
 *            type, range of acceptable values, default value, readability/writability and in
 *            which GStreamer state a property can be changed.
 */
static void
gst_vvas_xmulticrop_class_init (GstVvasXMultiCropClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *xform_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstCaps *caps;
  GstPadTemplate *pad_templ;

  caps = vvas_xmulticrop_generate_caps ();
  if (caps) {
    pad_templ = gst_pad_template_new ("sink",
        GST_PAD_SINK, GST_PAD_ALWAYS, caps);
    /* Add sink and source templates to element based scaler core supported formats */
    gst_element_class_add_pad_template (gstelement_class, pad_templ);
    pad_templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
    gst_element_class_add_pad_template (gstelement_class, pad_templ);
    gst_caps_unref (caps);
  } else {
    /* Add sink and source templates to element based on static templates */
    gst_element_class_add_static_pad_template (gstelement_class,
        &sink_template);
    gst_element_class_add_static_pad_template (gstelement_class, &src_template);
  }
  /* Add element's metadata */
  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Xilinx XlnxMultiCrop plugin",
      "Filter/Converter/Video/Scaler",
      "MulitCrop plugin based on Multi scaler using XRT",
      "Xilinx Inc <www.xilinx.com>");

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xmulticrop_debug_category,
      "vvas_xmulticrop", 0, "Xilinx's MultiCrop plugin");

  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");

  /* Override GObject class' virtual methods */
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_finalize);
  /* Override GstElement class' virtual methods */
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vvas_xmulticrop_change_state);

  /* Install xclbin-location property */
  g_object_class_install_property (gobject_class, PROP_XCLBIN_LOCATION,
      g_param_spec_string ("xclbin-location", "xclbin file location",
          "Location of the xclbin to program devices", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Install kernel-name property */
  g_object_class_install_property (gobject_class, PROP_KERN_NAME,
      g_param_spec_string ("kernel-name", "kernel name and instance",
          "String defining the kernel name and instance as mentioned in xclbin",
          DEFAULT_KERNEL_NAME,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

#ifdef XLNX_PCIe_PLATFORM
  /* Install dev-idx property */
  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("dev-idx", "Device index",
          "Valid Device index is 0 to 31. Default value is set to -1 intentionally"
          "\n\t\t\tso that user provides the correct device index.",
          -1, 31, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  /* Install in-mem-bank property */
  g_object_class_install_property (gobject_class, PROP_IN_MEM_BANK,
      g_param_spec_uint ("in-mem-bank", "VVAS Input Memory Bank",
          "VVAS input memory bank to allocate memory",
          0, G_MAXUSHORT, DEFAULT_MEM_BANK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install out-mem-bak property */
  g_object_class_install_property (gobject_class, PROP_OUT_MEM_BANK,
      g_param_spec_uint ("out-mem-bank", "VVAS Output Memory Bank",
          "VVAS output memory bank to allocate memory",
          0, G_MAXUSHORT, DEFAULT_MEM_BANK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install ppc (pixel per clock) property */
  g_object_class_install_property (gobject_class, PROP_PPC,
      g_param_spec_enum ("ppc", "pixel per clock",
          "Pixel per clock configured in Multiscaler kernel",
          VVAS_XMULTICROP_PPC_TYPE, VVAS_XMULTICROP_DEFAULT_PPC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install scale-mode property */
  g_object_class_install_property (gobject_class, PROP_SCALE_MODE,
      g_param_spec_int ("scale-mode", "Scaling Mode",
          "Scale Mode configured in Multiscaler kernel.  "
          "0: BILINEAR 1: BICUBIC 2: POLYPHASE", 0, 2,
          VVAS_XMULTICROP_SCALE_MODE, G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  /* Install num-taps property */
  g_object_class_install_property (gobject_class, PROP_NUM_TAPS,
      g_param_spec_enum ("num-taps", "Number filter taps",
          "Number of filter taps to be used for scaling",
          VVAS_XMULTICROP_NUM_TAPS_TYPE, VVAS_XMULTICROP_DEFAULT_NUM_TAPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install coefficient load type property */
  g_object_class_install_property (gobject_class, PROP_COEF_LOADING_TYPE,
      g_param_spec_enum ("coef-load-type", "Coefficients loading type",
          "coefficients loading type for scaling",
          VVAS_XMULTICROP_COEF_LOAD_TYPE,
          VVAS_XMULTICROP_DEFAULT_COEF_LOAD_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install avoid-output-copy property */
  g_object_class_install_property (gobject_class, PROP_AVOID_OUTPUT_COPY,
      g_param_spec_boolean ("avoid-output-copy",
          "Avoid output frames copy",
          "Avoid output frames copy even when downstream"
          " does not support GstVideoMeta metadata",
          VVAS_XMULTICROP_AVOID_OUTPUT_COPY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install enable-pipeline property */
  g_object_class_install_property (gobject_class, PROP_ENABLE_PIPELINE,
      g_param_spec_boolean ("enable-pipeline",
          "Enable pipelining",
          "Enable buffer pipelining to improve performance in non zero-copy use cases",
          VVAS_XMULTICROP_ENABLE_PIPELINE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install static crop x property */
  g_object_class_install_property (gobject_class, PROP_CROP_X,
      g_param_spec_uint ("s-crop-x", "Crop X coordinate for static cropping",
          "Crop X coordinate for static cropping",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install static crop y property */
  g_object_class_install_property (gobject_class, PROP_CROP_Y,
      g_param_spec_uint ("s-crop-y", "Crop Y coordinate for static cropping",
          "Crop Y coordinate for static cropping",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install static crop width property */
  g_object_class_install_property (gobject_class, PROP_CROP_WIDTH,
      g_param_spec_uint ("s-crop-width", "Crop width for static cropping",
          "Crop width (minimum: 16) for static cropping, if s-crop-x is set, but "
          "s-crop-width is 0 or not set,"
          "\n\t\t\ts-crop-width will be calculated as input width - s-crop-x",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install static crop height property */
  g_object_class_install_property (gobject_class, PROP_CROP_HEIGHT,
      g_param_spec_uint ("s-crop-height", "Crop height for static cropping",
          "Crop height (minimum: 16) for static cropping, if s-crop-y is set, but "
          "s-crop-height is 0 or not set,"
          "\n\t\t\ts-crop-height will be calculated as input height - s-crop-y",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install dynamic crop width property */
  g_object_class_install_property (gobject_class, PROP_SUBBUFFER_WIDTH,
      g_param_spec_uint ("d-width", "Width of dynamically cropped buffers",
          "Width of dynamically cropped buffers, if set all dynamically "
          "\n\t\t\tcropped buffer will be scaled to this width",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install dynamic crop height property */
  g_object_class_install_property (gobject_class, PROP_SUBBUFFER_HEIGHT,
      g_param_spec_uint ("d-height", "Height of dynamically cropped buffers",
          "Height of dynamically cropped buffers, if set all dynamically"
          "\n\t\t\tcropped buffers will be scaled to this height",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install dynamic crop buffer format property */
  g_object_class_install_property (gobject_class, PROP_SUBBUFFER_FORMAT,
      g_param_spec_enum ("d-format", "Format of dynamically cropped buffers",
          "Format of dynamically cropped buffers, by default it will be same as"
          " input buffer",
          VVAS_XMULTICROP_SUBBUFFER_FORMAT_TYPE, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Install pre-process on main buffer property */
  g_object_class_install_property (gobject_class, PROP_PPE_ON_MAIN_BUFFER,
      g_param_spec_boolean ("ppe-on-main-buffer",
          "Post processing on main buffer",
          "If set, PP will be applied to main buffer also, otherwise it will be"
          " applied only on dynamically cropped buffers",
          VVAS_XMULTICROP_PPE_ON_MAIN_BUF_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

#ifdef ENABLE_PPE_SUPPORT
  /* Install alpha red property */
  g_object_class_install_property (gobject_class, PROP_ALPHA_R,
      g_param_spec_float ("alpha-r",
          "PreProcessing parameter alpha red channel value",
          "PreProcessing parameter alpha red channel value", 0, 128,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Install alpha green property */
  g_object_class_install_property (gobject_class, PROP_ALPHA_G,
      g_param_spec_float ("alpha-g",
          "PreProcessing parameter alpha green channel value",
          "PreProcessing parameter alpha green channel value", 0, 128,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Install alpha blue property */
  g_object_class_install_property (gobject_class, PROP_ALPHA_B,
      g_param_spec_float ("alpha-b",
          "PreProcessing parameter alpha blue channel value",
          "PreProcessing parameter alpha blue channel value", 0, 128,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Install beta red property */
  g_object_class_install_property (gobject_class, PROP_BETA_R,
      g_param_spec_float ("beta-r",
          "PreProcessing parameter beta red channel value",
          "PreProcessing parameter beta red channel value", 0, 1,
          1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Install beta green property */
  g_object_class_install_property (gobject_class, PROP_BETA_G,
      g_param_spec_float ("beta-g",
          "PreProcessing parameter beta green channel value",
          "PreProcessing parameter beta green channel value", 0, 1,
          1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Install beta blue property */
  g_object_class_install_property (gobject_class, PROP_BETA_B,
      g_param_spec_float ("beta-b",
          "PreProcessing parameter beta blue channel value",
          "PreProcessing parameter beta blue channel value", 0, 1,
          1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  g_object_class_install_property (gobject_class, PROP_SOFTWARE_SCALING,
      g_param_spec_boolean ("software-scaling",
          "Flag to to enable software scaling flow.",
          "Set this flag to true in case of software scaling.",
          VVAS_XMULTICROP_ENABLE_SOFTWARE_SCALING_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* Override GstBaseTransoform class' virtual methods */
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

/**
 *  @fn static void gst_vvas_xmulticrop_init (GstVvasXMultiCrop * self)
 *  @param [in] self    - Handle to GstVvasXMultiCrop instance
 *  @return None
 *  @brief  Initializes GstVvasXMultiCrop member variables to default and does
 *          one time object/memory allocations in object's lifecycle
 */
static void
gst_vvas_xmulticrop_init (GstVvasXMultiCrop * self)
{
  self->priv = GST_VVAS_XMULTICROP_PRIVATE (self);
  /* Initialize context to their default values */
  self->xclbin_path = NULL;
  self->out_stride_align = 1;
  self->out_elevation_align = 1;
  self->num_taps = VVAS_XMULTICROP_DEFAULT_NUM_TAPS;
  self->coef_load_type = VVAS_XMULTICROP_DEFAULT_COEF_LOAD_TYPE;
  self->avoid_output_copy = VVAS_XMULTICROP_AVOID_OUTPUT_COPY_DEFAULT;
  self->software_scaling = VVAS_XMULTICROP_ENABLE_SOFTWARE_SCALING_DEFAULT;
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
  self->priv->in_vinfo = gst_video_info_new ();
  self->priv->out_vinfo = gst_video_info_new ();
  self->priv->validate_import = TRUE;
  self->priv->input_pool = NULL;
  self->priv->vvas_ctx = NULL;
  self->priv->vvas_scaler = NULL;
  self->kern_name = g_strdup (DEFAULT_KERNEL_NAME);
  gst_video_info_init (self->priv->in_vinfo);
  gst_video_info_init (self->priv->out_vinfo);
  self->priv->need_copy = TRUE;
  self->enabled_pipeline = VVAS_XMULTICROP_ENABLE_PIPELINE_DEFAULT;
}

/**
 *  @fn static gboolean gst_vvas_xmulticrop_decide_allocation (GstBaseTransform * trans, GstQuery * query)
 *  @param [in] trans   - Handle to GstVvasXMultiCrop typecasted to GstBaseTransform.
 *  @param [in] query   - Response for the allocation query.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  This function will decide allocation strategy based on the preference
 *          from downstream element.
 *  @details  The proposed query will be parsed through, verified if the proposed pool is VVAS and alignments
 *            are quoted. Otherwise it will be discarded and new pool, allocator will be created.
 */
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
  GstVideoInfo out_vinfo = { 0 };

  /* Get output caps from the query */
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
    /* No allocator params from the peer */
    allocator = NULL;
    update_allocator = FALSE;
    gst_allocation_params_init (&params);
  }

  if (!outcaps) {
    GST_ERROR_OBJECT (self, "failed to parse outcaps from the query");
    goto error;
  }

  if (!gst_video_info_from_caps (&out_vinfo, outcaps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from outcaps");
    goto error;
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    /* Got pool from the peer */
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    GST_DEBUG_OBJECT (self, "has pool %p", pool);
    size = MAX (size, out_vinfo.size);
    update_pool = TRUE;
    /* minimum buffers from the pool should be 3 */
    if (min == 0)
      min = 3;
  } else {
    /* No pools from the peer */
    pool = NULL;
    min = 3;
    max = 0;
    size = out_vinfo.size;
    update_pool = FALSE;
  }

  if (!self->software_scaling) {
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

        GST_INFO_OBJECT (self, "output stride = %u", stride);

        if (!stride)
          return FALSE;
        gst_structure_free (config);
        config = NULL;
        multiscaler_req_stride = WIDTH_ALIGN;

        if (stride % multiscaler_req_stride) {
          GST_WARNING_OBJECT (self, "Discarding the propsed pool, "
              "Alignment not matching with 8 * self->ppc");
          gst_object_unref (pool);
          pool = NULL;
          /* stride of proposed pool and that of Multi scaler IP is not matching,
           * we can't use this pool, free it and allocate new buffer pool */
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
        GST_DEBUG_OBJECT (self, "Suggested pool has no alignment info, "
            "discarding this pool: %p", pool);
        gst_object_unref (pool);
        pool = NULL;
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
      GST_DEBUG_OBJECT (self, "pool is not VVAS buffer pool");
    }

    /* If proposed allocator is not VVAS allocator or if it is not created for same device,
     * we can't use it, free it and allocate new VVAS allocator */
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
          NEED_DMABUF, self->out_mem_bank);
      params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
      params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;
      GST_INFO_OBJECT (self, "creating new xrt allocator %" GST_PTR_FORMAT
          "at mem bank %d", allocator, self->out_mem_bank);
      have_new_allocator = TRUE;
    }
#ifdef XLNX_EMBEDDED_PLATFORM
  next:
#endif
    if (update_allocator) {
      GST_DEBUG_OBJECT (self, "Updating allocator in query");
      gst_query_set_nth_allocation_param (query, 0, allocator, &params);
    } else {
      GST_DEBUG_OBJECT (self, "Adding allocator in query");
      gst_query_add_allocation_param (query, allocator, &params);
    }
    /* If there is no pool alignment requirement from downstream or if scaling dimension
     * is not aligned to (8 * ppc), then we will create a new pool*/
    if (!pool && (self->out_stride_align == 1)
        && ((out_vinfo.stride[0] % WIDTH_ALIGN)
            || (out_vinfo.height % HEIGHT_ALIGN))) {
      self->out_stride_align = WIDTH_ALIGN;
      self->out_elevation_align = HEIGHT_ALIGN;
    }

    if (!pool) {
      GstVideoAlignment align;
      /* Allocate new VVAS buffer pool as per the IP alignment requirement */
      pool = gst_vvas_buffer_pool_new (self->out_stride_align,
          self->out_elevation_align);
      GST_INFO_OBJECT (self, "created new pool %p %" GST_PTR_FORMAT, pool,
          pool);

      config = gst_buffer_pool_get_config (pool);
      gst_video_alignment_reset (&align);
      align.padding_bottom = ALIGN (GST_VIDEO_INFO_HEIGHT (&out_vinfo),
          self->out_elevation_align) - GST_VIDEO_INFO_HEIGHT (&out_vinfo);

      for (int idx = 0; idx < GST_VIDEO_INFO_N_PLANES (&out_vinfo); idx++) {
        align.stride_align[idx] = (self->out_stride_align - 1);
      }
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
      /* We already have a pool, but the allocator is new.
       * Update the newly created allocator in the downstream pool*/
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
  } else {
    /* We have a pool, lets just configure it */
    if (pool) {
      GstVideoAlignment video_align = { 0, };
      guint padded_width = 0;
      guint stride = 0;

      config = gst_buffer_pool_get_config (pool);

      /* Check if the proposed buffer pool has alignment information */
      if (config && gst_buffer_pool_config_has_option (config,
              GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
        gst_buffer_pool_config_get_video_alignment (config, &video_align);

        /* We have padding_right and padding_left in pixels.
         * We need to convert them to bytes for finding out the complete stride with alignment */
        padded_width =
            out_vinfo.width + video_align.padding_right +
            video_align.padding_left;
        gst_video_info_align (&out_vinfo, &video_align);
        stride = vvas_xmulticrop_get_stride (GST_VIDEO_INFO_FORMAT (&out_vinfo),
            padded_width);

        GST_INFO_OBJECT (self, "output stride = %u", stride);
        /* Stride can't be zero here */
        if (!stride) {
          gst_structure_free (config);
          config = NULL;
          gst_object_unref (pool);
          pool = NULL;
          return FALSE;
        }
      }
      min = 3;
      max = 0;
      size = out_vinfo.size;
      gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
      gst_buffer_pool_config_set_allocator (config, allocator, &params);

      /* buffer pool may have to do some changes */
      if (!gst_buffer_pool_set_config (pool, config)) {
        config = gst_buffer_pool_get_config (pool);

        /* If change are not acceptable, fallback to video buffer pool */
        if (!gst_buffer_pool_config_validate_params (config, outcaps, size, min,
                max)) {
          GST_DEBUG_OBJECT (self, "unsupported pool, making new pool");

          gst_object_unref (pool);
          pool = NULL;
        }
      }
    }

    /* Either the downstream has not proposed a pool or the proposed pool is not
     * usable for us. Let't create a new video pool with host memory, as we are
     * not doing the accelerated scaling.  */
    if (!pool) {
      pool = gst_video_buffer_pool_new ();
      size = out_vinfo.size;
      min = 3;
      max = 0;
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_META);
      bret = gst_buffer_pool_set_config (pool, config);
      if (!bret) {
        GST_ERROR_OBJECT (self, "failed configure pool");
        goto error;
      }
      /* We are using a video buffer pool, no need of copy even if
       * downstream element is non video intelligent.
       */
      self->avoid_output_copy = TRUE;
    }
  }

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

  if (update_pool) {
    GST_DEBUG_OBJECT (self, "Updating pool in query");
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  } else {
    GST_DEBUG_OBJECT (self, "Adding pool in query");
    gst_query_add_allocation_pool (query, pool, size, min, max);
  }

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

/**
 *  @fn static gboolean gst_vvas_xmulticrop_propose_allocation (GstBaseTransform * trans,
 *                                                              GstQuery * decide_query,
 *                                                              GstQuery * query)
 *  @param [in] trans           - Handle to GstVvasXMultiCrop typecasted to GstBaseTransform.
 *  @param [in] decide_query    - decide GstQuery
 *  @param [in] query           - Src pad object for which allocation has to be decided
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  This function proposes buffer allocation parameters for upstream elements.
 */
static gboolean
gst_vvas_xmulticrop_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  /* Propose buffer allocation mechanism to the upstream plugin */
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool;
  guint size;
  GstVideoAlignment align;

  GST_BASE_TRANSFORM_CLASS
      (gst_vvas_xmulticrop_parent_class)->propose_allocation (trans,
      decide_query, query);

  if (!self->software_scaling) {
    /* parse caps from query */
    gst_query_parse_allocation (query, &caps, NULL);

    if (caps == NULL)
      return FALSE;

    if (!gst_video_info_from_caps (&info, caps))
      return FALSE;

    /* Get pools information from the query */
    if (gst_query_get_n_allocation_pools (query) == 0) {
      GstStructure *structure;
      GstAllocator *allocator = NULL;
      GstAllocationParams params =
          { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0, 0 };

      if (gst_query_get_n_allocation_params (query) > 0) {
        gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
      } else {
        /* Suggest VVAS allocator as allocator */
        allocator = gst_vvas_allocator_new (self->dev_index,
            NEED_DMABUF, self->in_mem_bank);
        GST_INFO_OBJECT (self, "creating new xrt allocator %" GST_PTR_FORMAT
            "at mem bank %d", allocator, self->in_mem_bank);

        gst_query_add_allocation_param (query, allocator, &params);
      }

      /* suggest VVAS buffer pool */
      pool = gst_vvas_buffer_pool_new (WIDTH_ALIGN, HEIGHT_ALIGN);
      GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

      structure = gst_buffer_pool_get_config (pool);

      gst_video_alignment_reset (&align);
      align.padding_bottom =
          ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
          HEIGHT_ALIGN) - GST_VIDEO_INFO_HEIGHT (&info);
      for (int idx = 0; idx < GST_VIDEO_INFO_N_PLANES (&info); idx++) {
        align.stride_align[idx] = (WIDTH_ALIGN - 1);
      }
      gst_video_info_align (&info, &align);

      gst_buffer_pool_config_add_option (structure,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
      gst_buffer_pool_config_set_video_alignment (structure, &align);

      size = GST_VIDEO_INFO_SIZE (&info);
      gst_buffer_pool_config_set_params (structure, caps, size, 2, 0);
      /* set allocator the the buffer pool */
      gst_buffer_pool_config_set_allocator (structure, allocator, &params);
      /* set option to attach GstVideoMeta */
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

/**
 *  @fn static gboolean gst_vvas_xmulticrop_sink_event (GstBaseTransform * trans, GstEvent * event)
 *  @param [in] trans   - Handle to GstVvasXMultiCrop typecasted to GstBaseTransform.
 *  @param [in] event   - The GstEvent to handle.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  Handles GstEvent coming over the sink pad. Ex : EOS, New caps etc.
 */
static gboolean
gst_vvas_xmulticrop_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (self, "received %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {

    case GST_EVENT_EOS:{
      if (self->enabled_pipeline) {
        /* Got EOS and pipeline is enabled, need to process all pending
         * buffers */
        GstFlowReturn fret = GST_FLOW_OK;
        GstBuffer *inbuf = NULL;
        GstBaseTransformClass *klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

        GST_INFO_OBJECT (self, "input copy queue has %d pending buffers",
            g_async_queue_length (self->priv->copy_outqueue));

        /* process buffers pending in output copy queue */
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
            /* push buffer downstream */
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

/**
 *  @fn static gboolean vvas_xmulticrop_calculate_subbuffer_pool_size (GstVvasXMultiCrop * self)
 *  @param [in] self    - GstVvasXMultiCrop object
 *  @return TRUE on success\n FALSE on failure
 *  @brief  This function it to calculate different sizes for different buffer pools
 *  @details  This function divides the maximum input size into 10 parts, this info
 *            will be used in gst_vvas_xmulticrop_get_suitable_buffer_pool()
 */
static gboolean
vvas_xmulticrop_calculate_subbuffer_pool_size (GstVvasXMultiCrop * self)
{
  GstVvasXMultiCropPrivate *priv = self->priv;
  GstVideoInfo out_info = { 0 };
  guint max_out_w, max_out_h;
  gsize size, maximum_out_size;
  gint idx;

  /* Cropping will happen from the input buffer, hence maximum buffer size
   * of the cropped buffer is the size of input buffer itself including
   * the alignment requirement */

  max_out_w = vvas_xmulticrop_get_stride (self->subbuffer_format,
      GST_VIDEO_INFO_WIDTH (priv->in_vinfo));
  if (!self->software_scaling) {
    max_out_w = ALIGN (max_out_w, WIDTH_ALIGN);
    max_out_h = ALIGN (GST_VIDEO_INFO_HEIGHT (priv->in_vinfo), HEIGHT_ALIGN);
  } else {
    max_out_h = GST_VIDEO_INFO_HEIGHT (priv->in_vinfo);
  }

  if (!gst_video_info_set_format (&out_info, self->subbuffer_format,
          max_out_w, max_out_h)) {
    GST_ERROR_OBJECT (self, "failed to set video info");
    return FALSE;
  }

  /* This is the maximum size of cropped buffer */
  maximum_out_size = GST_VIDEO_INFO_SIZE (&out_info);
  GST_DEBUG_OBJECT (self, "max_out_w:%u, max_out_h:%u, maximum_out_size: %ld",
      max_out_w, max_out_h, maximum_out_size);

  /* 1/10th of maximum size */
  size = (maximum_out_size / 10) + 1;

  /* Divide the maximum size in 10 equal parts, with these size we'll
   * create 10 buffer pools based on the requirement of size of the crop
   * buffer, if size of buffer falls into range of already allocated pool,
   * same pool will be used.
   * Note that the size of crop buffer and the size of buffers from pool
   * will not match, size of pool's buffer will always be higher.
   * GstVideoMeta will be attached manually into the cropped buffer */
  for (idx = 0; idx < MAX_SUBBUFFER_POOLS; idx++) {
    priv->subbuffer_pool_size[idx] = size * (idx + 1);
#ifdef DEBUG
    GST_DEBUG_OBJECT (self, "size[%d]: %u", idx,
        priv->subbuffer_pool_size[idx]);
#endif
  }

  return TRUE;
}

/**
 *  @fn static GstCaps *gst_vvas_xmulticrop_transform_caps (GstBaseTransform * trans,
 *                                                          GstPadDirection direction,
 *                                                          GstCaps * caps,
 *                                                          GstCaps * filter)
 *  @param [in] self        - Handle to GstVvasXMultiCrop typecasted to GstBaseTransform.
 *  @param [in] direction   - Direction of the pad (src or sink)
 *  @param [in] caps        - Given caps
 *  @param [in] filter      - Caps to filter out, when given
 *  @return transformed caps
 *  @brief  Given the pad in this direction and the given caps, this function will find
 *          what caps are allowed on the other pad in this element
 */
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

/**
 *  @fn static GstCaps *gst_vvas_xmulticrop_fixate_caps (GstBaseTransform * trans,
 *                                                       GstPadDirection direction,
 *                                                       GstCaps * caps,
 *                                                       GstCaps * othercaps)
 *  @param [in] trans       - Handle to GstVvasXMultiCrop typecasted to GstBaseTransform.
 *  @param [in] direction   - Direction of the pad (src or sink)
 *  @param [in] caps        - Given caps
 *  @param [in] othercaps   - Caps to fixate
 *  @return Fixated version of "othercaps"
 *  @brief  Given the pad in this direction and the given caps, fixate the caps on
 *          the other pad.
 *  @details  The function takes ownership of othercaps and returns a fixated
 *            version of othercaps. Othercaps is not guaranteed to be writable.
 */
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
      GST_DEBUG_OBJECT (self, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        /* Since we don't know the PAR, lets use 1:1 as the PAR as it is the most
         * commonly used. Its also used by some opensource plugins like videotestsrc,
         * compositor etc _fixate_caps.*/
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
            NULL);
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

/**
 *  @fn static gboolean gst_vvas_xmulticrop_set_caps (GstBaseTransform * trans, GstCaps * in_caps, GstCaps * out_caps)
 *  @param [in] trans       - Handle to GstVvasXMultiCrop typecasted to GstBaseTransform.
 *  @param [in] in_caps     - Direction of the pad (src or sink)
 *  @param [in] out_caps    - Given caps
 *  @return TRUE on success\n FALSE on failure
 *  @brief  Allows the subclass to be notified of the actual caps set.
 *  @note   Returning FALSE from this function will cause caps negotiation failure.
 */
static gboolean
gst_vvas_xmulticrop_set_caps (GstBaseTransform * trans, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstVvasXMultiCropPrivate *priv = self->priv;
  VvasReturnType vret;
  VvasScalerProp scaler_prop = { 0 };
  VvasLogLevel core_log_level =
      vvas_get_core_log_level (gst_debug_category_get_threshold
      (gst_vvas_xmulticrop_debug_category));

  GST_DEBUG_OBJECT (self, "in_caps %p %" GST_PTR_FORMAT, in_caps, in_caps);
  GST_DEBUG_OBJECT (self, "out_caps %p %" GST_PTR_FORMAT, out_caps, out_caps);

  /* Use the xclbin only in hw processing */
  if (self->software_scaling) {
    if (self->xclbin_path)
      g_free (self->xclbin_path);
    self->xclbin_path = NULL;
  } else {
    if (self->xclbin_path == NULL) {
      GST_ERROR_OBJECT (self, "xclbin-location is not set");
      return FALSE;
    }
  }

  /* store sinkpad info */
  if (!gst_video_info_from_caps (priv->in_vinfo, in_caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from input caps");
    return FALSE;
  }

  /* In case of hardware accelerated scaler, the input width must be multiple
   * of PPC(Pixel per clock) */
  if (!self->software_scaling && (priv->in_vinfo->width % self->ppc)) {
    GST_ERROR_OBJECT (self, "Unsupported input resolution,"
        "width must be multiple of ppc i.e, %d", self->ppc);
    return FALSE;
  }

  if (!gst_video_info_from_caps (priv->out_vinfo, out_caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from output caps");
    return FALSE;
  }

  /* In case of hardware accelerated scaler, output width must also be multiple
   * of PPC(Pixel per clock) */
  if (!self->software_scaling && priv->out_vinfo->width % self->ppc) {
    GST_ERROR_OBJECT (self, "Unsupported output resolution,"
        "width must be multiple of ppc i.e, %d", self->ppc);
    return FALSE;
  }

  /* If static crop values are provided, need to validate them and also
   * align them as per the IP requirement */
  if (TRUE != vvas_xmulticrop_align_crop_params (trans, &self->crop_params,
          self->ppc)) {
    return FALSE;
  }

  /* The crop width/height must not be below a limit */
  if (self->crop_params.width || self->crop_params.height) {
    if ((self->crop_params.width < MIN_SCALAR_WIDTH) ||
        (self->crop_params.height < MIN_SCALAR_HEIGHT)) {
      GST_ERROR_OBJECT (self, "static crop width/height must be at least %u",
          MIN_SCALAR_WIDTH);
      return FALSE;
    }
  }

  /* Create VVAS Context, Scaler context and Set Scaler Properties */
  GST_DEBUG_OBJECT (self, "Creating VVAS context");
  priv->vvas_ctx = vvas_context_create (self->dev_index, self->xclbin_path,
      core_log_level, &vret);
  if (!priv->vvas_ctx) {
    GST_ERROR_OBJECT (self, "Couldn't create VVAS context");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Creating VVAS Scaler");
  priv->vvas_scaler = vvas_scaler_create (priv->vvas_ctx, self->kern_name,
      core_log_level);
  if (!priv->vvas_scaler) {
    GST_ERROR_OBJECT (self, "Couldn't create Scaler");
    return FALSE;
  }

  vret = vvas_scaler_prop_get (priv->vvas_scaler, &scaler_prop);
  if (VVAS_IS_ERROR (vret)) {
    GST_ERROR_OBJECT (self, "Couldn't get scaler props");
  }

  scaler_prop.coef_load_type = (VvasScalerCoefLoadType) self->coef_load_type;
  scaler_prop.smode = (VvasScalerMode) self->scale_mode;
  scaler_prop.ftaps = (VvasScalerFilterTaps) self->num_taps;
  scaler_prop.ppc = self->ppc;
  scaler_prop.mem_bank = self->in_mem_bank;

  vret = vvas_scaler_prop_set (priv->vvas_scaler, &scaler_prop);
  if (VVAS_IS_ERROR (vret)) {
    GST_ERROR_OBJECT (self, "Couldn't set scaler props");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Scaler: coef_load_type: %d",
      scaler_prop.coef_load_type);
  GST_DEBUG_OBJECT (self, "Scaler: scaling mode: %d", scaler_prop.smode);
  GST_DEBUG_OBJECT (self, "Scaler: filter taps: %d", scaler_prop.ftaps);
  GST_DEBUG_OBJECT (self, "Scaler: ppc: %d", scaler_prop.ppc);
  GST_DEBUG_OBJECT (self, "Scaler: mem_bank: %d", scaler_prop.mem_bank);

  /* Get the Device handle from VVAS context for local use */
  priv->dev_handle = priv->vvas_ctx->dev_handle;

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
    /* Allocate buffer pool for holding dynamically cropped sub buffers */
    priv->subbuffer_pools[0] = vvas_xmulticrop_allocate_buffer_pool (self,
        self->subbuffer_width, self->subbuffer_height, self->subbuffer_format);
    if (!priv->subbuffer_pools[0]) {
      GST_ERROR_OBJECT (self, "failed to allocate pool");
      return FALSE;
    }
  } else {
    /* User hasn't specified resolution of dynamically cropped buffers,
     * this means we don't have to do scaling of these buffers, they should
     * have their original resolution (with IP alignment adjustment).
     * We can't use single buffer pool in this case, as the resolution of
     * cropped buffers will be different even in a frame.
     * For this we are dividing the input buffer size into 10 parts, more
     * detail on this is in vvas_xmulticrop_calculate_subbuffer_pool_size()
     */
    if (!vvas_xmulticrop_calculate_subbuffer_pool_size (self)) {
      return FALSE;
    }
  }
  return TRUE;
}

/**
 *  @fn static gpointer vvas_xmulticrop_input_copy_thread (gpointer data)
 *  @param [in] data    - Data passed to this thread while creating it
 *  @return Exit Status (NULL)
 *  @brief  This thread makes non VVAS GstBuffer into VVAS GstBuffer
 *  @details  This thread reads non VVAS buffer from copy_inqueue; copies it to
 *            the VVAS GstBuffer and pushes it to copy_outqueue.
 */
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

    /* Get buffer from input copy queue, block until we get any buffer */
    inbuf = (GstBuffer *) g_async_queue_pop (priv->copy_inqueue);
    if (inbuf == STOP_COMMAND) {
      /* Exit this thread, when we get stop command */
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
    /* slow copy data from in_vframe to own_vframe */
    gst_video_frame_copy (&own_vframe, &in_vframe);

    gst_video_frame_unmap (&in_vframe);
    gst_video_frame_unmap (&own_vframe);
    gst_buffer_copy_into (own_inbuf, inbuf,
        (GstBufferCopyFlags) (GST_BUFFER_COPY_METADATA), 0, -1);
    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, self,
        "slow copy to internal input pool buffer");
    gst_buffer_unref (inbuf);
    /* Push VVAS buffer (own_inbuf) to output copy queue */
    g_async_queue_push (priv->copy_outqueue, own_inbuf);
  }

error:
  return NULL;
}

/**
 *  @fn static gboolean vvas_xmulticrop_allocate_internal_pool (GstBaseTransform * trans)
 *  @param [in, out] trans  - GstVvasXMultiCrop object typecasted to GstBaseTransform
 *  @return TRUE on success\n FALSE on failure
 *  @brief  This function allocates GstBufferPool for internal use.
 */
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

  /* Allocate internal buffer pool for converting non VVAS input memory
   * to VVAS memory */
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
  align.padding_bottom =
      ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
      HEIGHT_ALIGN) - GST_VIDEO_INFO_HEIGHT (&info);
  for (int idx = 0; idx < GST_VIDEO_INFO_N_PLANES (&info); idx++) {
    align.stride_align[idx] = WIDTH_ALIGN - 1;
  }
  gst_video_info_align (&info, &align);

  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_set_video_alignment (config, &align);

  /* Allocate new VVAS allocator for internal buffer pool */
  allocator = gst_vvas_allocator_new (self->dev_index,
      NEED_DMABUF, self->in_mem_bank);
  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
  alloc_params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;
  GST_INFO_OBJECT (self,
      "allocated %" GST_PTR_FORMAT " allocator at mem bank %d", allocator,
      self->in_mem_bank);

  /* number of buffers this internal pool allocated is limited to 5 */
  gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (&info),
      3, 5);

  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  /* set option to add GstVideoMeta into the buffers */
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

/**
 *  @fn static gboolean vvas_xmulticrop_prepare_input_buffer (GstBaseTransform * trans, GstBuffer ** inbuf)
 *  @param [in] self            - GstVvasXMultiCrop object
 *  @param [in, out] inbuf      - Input GstBuffer
 *  @return TRUE on success\n FALSE on failure
 *  @brief  This function prepares the input buffer for processing using IP.
 *  @details  This function checks whether input memory is VVAS memory or not, if not it converts it to
 *            VVAS memory be copying the data into new VVAS buffer. It also avoids copy of data if input
 *            memory is VVAS memory and lies into the same device.
 */
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
    /* Input memory is VVAS memory, and lies on the same device and same
     * memory bank, no need to copy anything */
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
    /* Input memory is neither VVAS nor DMA buf memory, need to make it VVAS
     * memory */
    use_inpool = TRUE;
  }

  gst_memory_unref (in_mem);
  in_mem = NULL;

  if (use_inpool) {
    if (self->priv->validate_import) {
      if (!self->priv->input_pool) {
        /* Allocate internal VVAS buffer pool */
        bret = vvas_xmulticrop_allocate_internal_pool (trans);
        if (!bret)
          goto error;
      }
      if (!gst_buffer_pool_is_active (self->priv->input_pool))
        /* Activate internal buffer pool */
        gst_buffer_pool_set_active (self->priv->input_pool, TRUE);
      self->priv->validate_import = FALSE;
    }

    if (self->enabled_pipeline) {
      /* Pipeline is enabled, Check if we have any buffer in the output
       * queue which was submitted in the last call to this function */
      own_inbuf = g_async_queue_try_pop (priv->copy_outqueue);
      if (!own_inbuf && !priv->is_first_frame) {
        /* output queue has buffer, we need to process this buffer */
        own_inbuf = g_async_queue_pop (priv->copy_outqueue);
      }

      priv->is_first_frame = FALSE;
      /* Push current buffer to input copy queue, copy thread will make it
       * VVAS buffer and push it to output copy queue */
      g_async_queue_push (priv->copy_inqueue, *inbuf);

      if (!own_inbuf) {
        /* Don't have any buffer */
        GST_LOG_OBJECT (self, "copied input buffer is not available. return");
        *inbuf = NULL;
        return TRUE;
      }

      *inbuf = own_inbuf;
    } else {
      /* Pipeline is not enabled, need to do slow copy of input buffer into
       * VVAS buffer */
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
        gst_video_frame_unmap (&own_vframe);
        goto error;
      }
      /* slow copy data from input buffer to newly acquired buffer */
      gst_video_frame_copy (&own_vframe, &in_vframe);

      gst_video_frame_unmap (&in_vframe);
      gst_video_frame_unmap (&own_vframe);
      gst_buffer_copy_into (own_inbuf, *inbuf,
          (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_METADATA
              | GST_BUFFER_COPY_TIMESTAMPS), 0, -1);
      /* As we have copied inbuf to own_inbuf, free inbuf */
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
  /* Get physical address of input memory */
  if (phy_addr == (uint64_t) - 1) {
    phy_addr = gst_vvas_allocator_get_paddr (in_mem);
  }
  /* syncs data when XLNX_SYNC_TO_DEVICE flag is enabled */
  bret = gst_vvas_memory_sync_bo (in_mem);
  if (!bret)
    goto error;

  gst_memory_unref (in_mem);

  return TRUE;

error:
  if (in_mem)
    gst_memory_unref (in_mem);

  return FALSE;
}

/**
 *  @fn static gboolean vvas_xmulticrop_prepare_output_buffer (GstVvasXMultiCrop * self, GstBuffer * outbuf)
 *  @param [in] self            - GstVvasXMultiCrop object
 *  @param [in, out] outbuf     - Output GstBuffer
 *  @return TRUE on success\n FALSE on failure
 *  @brief  This function prepares the output buffer for processing using IP
 */
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

  GST_DEBUG_OBJECT (self, "Output buffer physical address 0x%lX", phy_addr);

  gst_memory_unref (mem);
  return TRUE;

error:
  if (mem)
    gst_memory_unref (mem);

  return FALSE;
}

/**
 *  @fn static gboolean vvas_xmulticrop_process (GstVvasXMultiCrop * self)
 *  @param [in] self    - GstVvasXMultiCrop object
 *  @return TRUE on success\n FALSE on failure
 *  @brief  This function processes the output and dynamically crop buffers using IP.
 */
static gboolean
vvas_xmulticrop_process (GstVvasXMultiCrop * self)
{
  GstVvasXMultiCropPrivate *priv = self->priv;
  uint32_t chan_id = 0;
  GstMemory *mem = NULL;
  VvasReturnType vret;

  /* We will be always processing number of dynamic crop buffers + input buffer
   * And at max it will be 40 */
  guint buffer_count = self->priv->sub_buffer_counter + 1;      //+1 for main buffer

  vret = vvas_scaler_process_frame (self->priv->vvas_scaler);
  if (VVAS_IS_ERROR (vret)) {
    GST_ERROR_OBJECT (self, "Failed to process frame in scaler");
    return FALSE;
  }
  for (chan_id = 0; chan_id < buffer_count; chan_id++) {
    if (0 == chan_id) {
      mem = gst_buffer_get_memory (priv->outbuf, 0);
      /* When we are using software scaler and working on device memory proposed by downstream
       * we have to sync the data back to device side to make data available for next plugin.
       */
      if (self->software_scaling && gst_is_vvas_memory (mem)) {
        gst_vvas_memory_set_sync_flag (mem, VVAS_SYNC_TO_DEVICE);
      }
    } else {
      mem = gst_buffer_get_memory (priv->sub_buf[chan_id - 1], 0);
    }
    if (mem == NULL) {
      GST_ERROR_OBJECT (self,
          "chan-%d : failed to get memory from output buffer", chan_id);
      return FALSE;
    }
    if (!self->software_scaling) {
      gst_vvas_memory_set_sync_flag (mem, VVAS_SYNC_FROM_DEVICE);
    }
    gst_memory_unref (mem);
  }
  return TRUE;
}

/**
 *  @fn static GstBufferPool * gst_vvas_xmulticrop_get_suitable_buffer_pool (GstBaseTransform * trans,
 *                                                                           GstVideoRegionOfInterestMeta * roi_meta)
 *  @param [in] trans       - GstVvasXMultiCrop object typecasted to GstBaseTransform
 *  @param [in] roi_meta    - dynamic crop meta (GstVideoRegionOfInterestMeta meta)
 *  @return GstBufferPool or NULL
 *  @brief  This function allocates a GstBufferPool (if not already allocated) based on the size
 *          requirement of roi_meta and returns it.
 */
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
  if (!self->software_scaling) {
    /* As MultiScaler IP has alignment requirement, need to ensure that the
     * width, height and size of the buffer are proper */
    out_width = ALIGN (out_width, WIDTH_ALIGN);
    out_height = ALIGN (roi_meta->h, HEIGHT_ALIGN);
  } else {
    out_width = roi_meta->w;
    out_height = roi_meta->h;
  }

  GST_DEBUG_OBJECT (self, "aligned width:%u, height:%u", out_width, out_height);

  if (!gst_video_info_set_format (&out_info, self->subbuffer_format,
          out_width, out_height)) {
    GST_ERROR_OBJECT (self, "failed to set video info");
    return NULL;
  }

  /* Get the required size of buffer */
  size_requested = GST_VIDEO_INFO_SIZE (&out_info);

  /* Find the suitable buffer pool for this requested size */
  for (idx = 0; idx < MAX_SUBBUFFER_POOLS; idx++) {
    if (priv->subbuffer_pool_size[idx] > size_requested) {
      pool_idx = idx;
      break;
    }
  }

  /* If we can't find suitable pool, we can't proceed ahead */
  if ((-1) == pool_idx) {
    GST_ERROR_OBJECT (self, "couldn't find any suitable subbuffer_pool");
    return NULL;
  }

  sub_pool = self->priv->subbuffer_pools[pool_idx];
  GST_DEBUG_OBJECT (self,
      "chosen sub buffer pool %p at index %d for requested buffer size %lu",
      sub_pool, pool_idx, size_requested);

  if (sub_pool == NULL) {
    /* pool is not allocated */
    GstAllocator *allocator = NULL;
    GstCaps *caps = NULL;
    gsize pool_buf_size;
    gboolean bret;
    GstStructure *config;
    GstAllocationParams alloc_params;

    caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING,
        gst_video_format_to_string (self->subbuffer_format),
        "width", G_TYPE_INT, out_width, "height", G_TYPE_INT, out_height, NULL);

    pool_buf_size = priv->subbuffer_pool_size[pool_idx];

    /* We are creating the buffer pool with width and height alignment as 1,
     * because we have already aligned it. */
    if (!self->software_scaling) {
      sub_pool = gst_vvas_buffer_pool_new (1, 1);

      /* Here frame buffer is required from in_mem_bank as it is expected that
       * input port is attached the bank where IP access internal data too */
      allocator = gst_vvas_allocator_new (self->dev_index, NEED_DMABUF,
          self->out_mem_bank);

      config = gst_buffer_pool_get_config (sub_pool);

      gst_allocation_params_init (&alloc_params);
      alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
      alloc_params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;

      /* Size of buffer allocated by the pool will always be greater than the
       * size requested by the user, requesting minimum 2 buffers from the pool,
       * but have no restriction on maximum buffers */
      gst_buffer_pool_config_set_params (config, caps, pool_buf_size, 2, 0);
      gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);

    } else {
      /* Let't create a Gstreamer video pool with host memory */
      sub_pool = gst_video_buffer_pool_new ();
      config = gst_buffer_pool_get_config (sub_pool);
      gst_buffer_pool_config_set_params (config, caps, pool_buf_size, 2, 0);
    }
    GST_INFO_OBJECT (self, "allocated internal private pool %p with size %lu",
        sub_pool, pool_buf_size);

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

    /* Activate the buffer pool */
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

/**
 *  @fn static gboolean gst_vvas_xmulticrop_fill_video_meta (GstBaseTransform * trans,
 *                                                           const GstVideoRegionOfInterestMeta * roi_meta,
 *                                                           GstVideoMeta * vmeta)
 *  @param [in] trans       - GstVvasXMultiCrop object typecasted to GstBaseTransform
 *  @param [in] roi_meta    - dynamic crop meta (GstVideoRegionOfInterestMeta meta)
 *  @param [out] vmeta      - GstVideoMeta to be filled
 *  @return TRUE on success\n FALSE on failure
 *  @brief  This function fills stride and offset values in vmeta based on dynamic buffer's format
 */
static gboolean
gst_vvas_xmulticrop_fill_video_meta (GstBaseTransform * trans,
    const GstVideoRegionOfInterestMeta * roi_meta, GstVideoMeta * vmeta)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstVideoAlignment align_info;
  GstVideoInfo vinfo;

  gst_video_info_init (&vinfo);
  gst_video_info_set_format (&vinfo, self->subbuffer_format, roi_meta->w,
      roi_meta->h);

  /* Add padding information */
  gst_video_alignment_reset (&align_info);
  if (!self->software_scaling) {
    align_info.padding_bottom = ALIGN (roi_meta->h, HEIGHT_ALIGN) - roi_meta->h;
    for (int idx = 0; idx < GST_VIDEO_INFO_N_PLANES (&vinfo); idx++) {
      align_info.stride_align[idx] = (WIDTH_ALIGN - 1);
    }
    gst_video_info_align (&vinfo, &align_info);
  }

  for (int idx = 0; idx < GST_VIDEO_INFO_N_PLANES (&vinfo); idx++) {
    vmeta->stride[idx] = vinfo.stride[idx];
    vmeta->offset[idx] = vinfo.offset[idx];
  }

  GST_DEBUG_OBJECT (self,
      "Align info: top: %u, bottom: %u, left: %u, right: %u",
      align_info.padding_top, align_info.padding_bottom,
      align_info.padding_left, align_info.padding_right);

  gst_video_meta_set_alignment (vmeta, align_info);

  return TRUE;
}

/**
 *  @fn static GstBuffer * gst_xmulticrop_prepare_subbuffer (GstBaseTransform * trans,
 *                                                           GstVideoRegionOfInterestMeta * roi_meta)
 *  @param [in] trans       - GstVvasXMultiCrop object typecasted to GstBaseTransform
 *  @param [in] roi_meta    - dynamic crop meta (GstVideoRegionOfInterestMeta meta)
 *  @return Allocated SubBuffer or NULL on failure
 *  @brief  This function gets sub buffer from buffer pool and fills the required metadata
 */
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
    /* User has provided subbuffer's width and height,
     * we need only 1 buffer pool */
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

  /* Acquire the sub buffer from the selected buffer pool */
  fret = gst_buffer_pool_acquire_buffer (subbuffer_pool, &sub_buffer, NULL);
  if (fret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "failed to allocate buffer from pool %p",
        subbuffer_pool);
    goto error;
  }

  GST_DEBUG_OBJECT (self, "Got sub buffer %p from pool %p with size: %ld",
      sub_buffer, subbuffer_pool, gst_buffer_get_size (sub_buffer));

  if (add_meta) {
    /* In case where user wants the dynamically cropped buffers not to be
     * scaled to any resolution, we need to add the GstVideoMeta metadata
     * manually as buffer pool will not do that
     */
    vmeta = gst_buffer_add_video_meta (sub_buffer, GST_VIDEO_FRAME_FLAG_NONE,
        self->subbuffer_format, roi_meta->w, roi_meta->h);
    if (vmeta)
      gst_vvas_xmulticrop_fill_video_meta (trans, roi_meta, vmeta);
  }

  /* Just a sanity check, sub_buffer must have GstVideoMeta */
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

  /* store this sub_buffer into our context and keep track of number of
   * such buffers allocated */
  priv->sub_buf[priv->sub_buffer_counter] = sub_buffer;
  priv->sub_buffer_counter++;
  return sub_buffer;

error:
  if (sub_buffer) {
    gst_buffer_unref (sub_buffer);
  }
  return NULL;
}

/**
 *  @fn static gboolean vvas_xmulticrop_add_scaler_output_buffer_chnnels (GstVvasXMultiCrop * self)
 *  @param [in] self       - GstVvasXMultiCrop instance
 *  @return TRUE on success.\n FALSE on failure.
 *  @brief  This function will add the channel in Core Scaler for processing of output buffer.
 */
static gboolean
vvas_xmulticrop_add_scaler_output_buffer_chnnels (GstVvasXMultiCrop * self)
{
  GstVvasXMultiCropPrivate *priv = self->priv;
  VvasVideoFrame *output_frame;
  VvasScalerRect src_rect = { 0 };
  VvasScalerRect dst_rect = { 0 };
  VvasReturnType vret;

#ifdef ENABLE_PPE_SUPPORT
  VvasScalerPpe ppe = { 0 };
  ppe.mean_r = self->alpha_r;
  ppe.mean_g = self->alpha_g;
  ppe.mean_b = self->alpha_b;
  ppe.scale_r = self->beta_r;
  ppe.scale_g = self->beta_g;
  ppe.scale_b = self->beta_b;
#endif

  /* Convert GstBuffer to VvasVideoFrame required for Vvas Core Scaler */
  output_frame = vvas_videoframe_from_gstbuffer (self->priv->vvas_ctx,
      self->out_mem_bank, priv->outbuf, priv->out_vinfo, GST_MAP_READ);
  if (!output_frame) {
    GST_ERROR_OBJECT (self,
        "Could not convert output GstBuffer to VvasVideoFrame");
    return FALSE;
  }

  /* fill rect parameters */
  if ((self->crop_params.width && self->crop_params.height)) {
    src_rect.x = self->crop_params.x;
    src_rect.y = self->crop_params.y;
    src_rect.width = self->crop_params.width;
    src_rect.height = self->crop_params.height;
  } else {
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = GST_VIDEO_INFO_WIDTH (priv->in_vinfo);
    src_rect.height = GST_VIDEO_INFO_HEIGHT (priv->in_vinfo);
  }
  src_rect.frame = priv->input_frame;

  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.width = GST_VIDEO_INFO_WIDTH (priv->out_vinfo);
  dst_rect.height = GST_VIDEO_INFO_HEIGHT (priv->out_vinfo);;
  dst_rect.frame = output_frame;

  /* Add processing channel in Core Scaler */
#ifdef ENABLE_PPE_SUPPORT
  vret = vvas_scaler_channel_add (priv->vvas_scaler, &src_rect, &dst_rect,
      self->ppe_on_main_buffer ? &ppe : NULL, NULL);
#else
  vret =
      vvas_scaler_channel_add (priv->vvas_scaler, &src_rect, &dst_rect, NULL,
      NULL);
#endif
  if (VVAS_IS_ERROR (vret)) {
    GST_ERROR_OBJECT (self, "failed to add processing channel in scaler");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Added processing channel for idx: 0");

  priv->output_frames[0] = output_frame;
  return TRUE;
}

/**
 *  @fn static gboolean vvas_xmulticrop_add_scaler_processing_chnnels (GstVvasXMultiCrop * self,
 *                                                                     GstBuffer * sub_buffer,
 *                                                                     const GstVideoRegionOfInterestMeta * roi_meta,
 *                                                                     guint idx)
 *  @param [in] self        - GstVvasXMultiCrop instance
 *  @param [in] sub_buffer  - sub buffer (dynamic crop buffer)
 *  @param [in] roi_meta    - dynamic crop meta (GstVideoRegionOfInterestMeta meta)
 *  @param [in] idx         - Index
 *  @return TRUE on success\n FALSE on failure.
 *  @brief  This function prepares and adds this dynamic buffer/sub buffers to be processed by the Core Scaler.
 */
static gboolean
vvas_xmulticrop_add_scaler_processing_chnnels (GstVvasXMultiCrop * self,
    GstBuffer * sub_buffer, const GstVideoRegionOfInterestMeta * roi_meta,
    guint idx)
{
  GstVvasXMultiCropPrivate *priv = self->priv;
  VvasVideoFrame *output_frame;
  VvasScalerRect src_rect = { 0 };
  VvasScalerRect dst_rect = { 0 };
  VvasReturnType vret;
  GstVideoInfo vinfo = { 0 };
  GstVideoMeta *meta_out;
  GstCaps *caps;

#ifdef ENABLE_PPE_SUPPORT
  VvasScalerPpe ppe = { 0 };
  ppe.mean_r = self->alpha_r;
  ppe.mean_g = self->alpha_g;
  ppe.mean_b = self->alpha_b;
  ppe.scale_r = self->beta_r;
  ppe.scale_g = self->beta_g;
  ppe.scale_b = self->beta_b;
#endif

  meta_out = gst_buffer_get_video_meta (sub_buffer);
  if (!meta_out) {
    return FALSE;
  }

  /* This caps creation is just to get video info */
  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, gst_video_format_to_string (meta_out->format),
      "width", G_TYPE_INT, meta_out->width,
      "height", G_TYPE_INT, meta_out->height, NULL);

  if (!gst_video_info_from_caps (&vinfo, caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from caps");
    gst_caps_unref (caps);
    return FALSE;
  }
  gst_caps_unref (caps);

  /* Convert GstBuffer to VvasVideoFrame which is needed by VVAS Core Scaler */
  output_frame = vvas_videoframe_from_gstbuffer (self->priv->vvas_ctx,
      self->out_mem_bank, sub_buffer, &vinfo, GST_MAP_READ);
  if (!output_frame) {
    GST_ERROR_OBJECT (self,
        "Could not convert output GstBuffer to VvasVideoFrame");
    return FALSE;
  }

  /* 0th index for main buffer processing */
  idx++;
  priv->output_frames[idx] = output_frame;

  /* Prepare Source and Destination rects */
  src_rect.x = roi_meta->x;
  src_rect.y = roi_meta->y;
  src_rect.width = roi_meta->w;
  src_rect.height = roi_meta->h;
  src_rect.frame = priv->input_frame;

  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.width = self->subbuffer_width ? self->subbuffer_width : roi_meta->w;
  dst_rect.height =
      self->subbuffer_height ? self->subbuffer_height : roi_meta->h;
  dst_rect.frame = output_frame;

  /* Add processing channel to the Core Scaler */
#ifdef ENABLE_PPE_SUPPORT
  vret =
      vvas_scaler_channel_add (priv->vvas_scaler, &src_rect, &dst_rect, &ppe,
      NULL);
#else
  vret =
      vvas_scaler_channel_add (priv->vvas_scaler, &src_rect, &dst_rect, NULL,
      NULL);
#endif
  if (VVAS_IS_ERROR (vret)) {
    GST_ERROR_OBJECT (self, "failed to add processing channel in scaler");
    return FALSE;
  }
  GST_DEBUG_OBJECT (self, "Added processing channel for idx: %u", idx);

  return TRUE;
}

/**
 *  @fn static gboolean vvas_xmulticrop_prepare_crop_buffers (GstBaseTransform * trans, GstBuffer * inbuf)
 *  @param [in] trans   - GstVvasXMultiCrop object typecasted to GstBaseTransform
 *  @param [in] inbuf   - Input GstBuffer
 *  @return TRUE on success\n FALSE on failure.
 *  @brief  This function validates all the dynamic crop metadata (GstVideoRegionOfInterestMeta)
 *          and prepares the output buffers for storing them, attaches them into the
 *          same dynamic crop meta and prepares the descriptor for processing them using IP.
 */
static gboolean
vvas_xmulticrop_prepare_crop_buffers (GstBaseTransform * trans,
    GstBuffer * inbuf)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (trans);
  GstBuffer *sub_buffer = NULL;
  GstStructure *s;
  guint roi_meta_counter = 0;
  gboolean ret = TRUE;
  VvasReturnType vret;

  gpointer state = NULL;
  GstMeta *_meta;

  /* Iterate all GstVideoRegionOfInterestMeta */
  while ((_meta = gst_buffer_iterate_meta_filtered (inbuf, &state,
              GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE))) {

    GstVideoRegionOfInterestMeta *roi_meta;
    VvasCropParams subcrop_params = { 0 };

    /* we can process only limited number of dynamic crops, if use has given
     * more number of dynamic crops, we can't handle them
     */
    if (roi_meta_counter >= (MAX_CHANNELS - 1)) {
      GST_DEBUG_OBJECT (self, "we can process only %d crop meta",
          (MAX_CHANNELS - 1));
      break;
    }

    roi_meta = (GstVideoRegionOfInterestMeta *) _meta;

    if (g_strcmp0 ("roi-crop-meta", g_quark_to_string (roi_meta->roi_type))) {
      /* This is not the metadata we are looking for */
      continue;
    }
    /* Got the ROI crop metadata, prepare output buffer */
    GST_DEBUG_OBJECT (self, "Got roi-crop-meta[%u], parent_id:%d, id:%d, "
        "x:%u, y:%u, w:%u, h:%u",
        roi_meta_counter, roi_meta->parent_id, roi_meta->id, roi_meta->x,
        roi_meta->y, roi_meta->w, roi_meta->h);

    subcrop_params.x = roi_meta->x;
    subcrop_params.y = roi_meta->y;
    subcrop_params.width = roi_meta->w;
    subcrop_params.height = roi_meta->h;

    /* Crop parameters must be validate and aligned as per the MultiScaler IP
     * requirement */
    if (TRUE != vvas_xmulticrop_align_crop_params (trans, &subcrop_params, 4)) {
      /* crop params for sub buffer are not proper, skip to next */
      GST_DEBUG_OBJECT (self, "crop params are not proper, skipping meta %d",
          roi_meta->id);
      continue;
    }

    /* Multiscaler IP can't process video below a limited range */
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

    /* Prepare sub buffers to store dynamically cropped buffers */
    sub_buffer = gst_xmulticrop_prepare_subbuffer (trans, roi_meta);
    if (!sub_buffer) {
      GST_ERROR_OBJECT (self, "couldn't get sub buffer");
      ret = FALSE;
      break;
    }
    /* Attach this sub_buffer into metadata */
    s = gst_structure_new ("roi-buffer", "sub-buffer", GST_TYPE_BUFFER,
        sub_buffer, NULL);
    if (!s) {
      ret = FALSE;
      break;
    }

    /* GstStructure will take the ownership of buffer, hence sub_buffer must
     * be unrefd, As sub_buffer/priv->sub_buf[x] is needed, once it is used,
     * unref it
     */
    gst_video_region_of_interest_meta_add_param (roi_meta, s);

    /* Add Processing channels to the Core Scaler */
    vret =
        vvas_xmulticrop_add_scaler_processing_chnnels (self, sub_buffer,
        roi_meta, roi_meta_counter);
    if (VVAS_IS_ERROR (vret)) {
      GST_ERROR_OBJECT (self, "Failed to process frame in scaler");
      return FALSE;
    }
    roi_meta_counter++;
  }
  return ret;
}

/**
 *  @fn static guint gst_vvas_xmultirop_get_roi_meta_count (GstBuffer * buffer)
 *  @param [in] buffer  - GstBuffer
 *  @return Total number of GstVideoRegionOfInterestMeta having their roi_type
 *          field set to roi-crop-meta.
 *  @brief  This function gets the total number of GstVideoRegionOfInterestMeta
 *          having their roi_type field set to roi-crop-meta.
 */
static inline guint
gst_vvas_xmultirop_get_roi_meta_count (GstBuffer * buffer)
{
  gpointer state = NULL;
  GstMeta *_meta;
  guint crop_roi_meta_count = 0;
  /* Get total number of GstVideoRegionOfInterestMeta having their roi_type
   * field set to roi-crop-meta.
   * We expect user to send GstVideoRegionOfInterestMeta with its roi_type set
   * to roi-crop-meta to consider it as metadata for dynamic cropping
   */
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

/**
 *  @fn static GstFlowReturn gst_vvas_xmulticrop_generate_output (GstBaseTransform * trans, GstBuffer ** outbuf)
 *  @param [in] trans   - GstVvasXMultiCrop handle typecasted to GstBaseTransform
 *  @param [out] outbuf - Output buffer
 *  @return GstFlowReturn, GST_FLOW_OK on success.
 *  @brief  Called after each new input buffer is submitted repeatedly until it
 *          either generates an error or fails to generate an output buffer.
 */
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
    /* If there is no buffer from baseclass (input buffer), nothing can be done */
    return GST_FLOW_OK;
  }
  /* Get input buffer from the baseclass */
  inbuf = trans->queued_buf;
  trans->queued_buf = NULL;

  GST_DEBUG_OBJECT (self, "received %" GST_PTR_FORMAT, inbuf);

  /* Prepare input buffer */
  if (!self->software_scaling) {
    bret = vvas_xmulticrop_prepare_input_buffer (trans, &inbuf);
    if (!bret) {
      goto error;
    }
  }

  if (!inbuf)
    return GST_FLOW_OK;

  /* Get the output buffer */
  fret =
      GST_BASE_TRANSFORM_CLASS
      (gst_vvas_xmulticrop_parent_class)->prepare_output_buffer (trans, inbuf,
      &cur_outbuf);
  if (fret != GST_FLOW_OK)
    goto error;

  /* prepare output buffer for main buffer processing */
  if (!self->software_scaling) {
    bret = vvas_xmulticrop_prepare_output_buffer (self, cur_outbuf);
    if (!bret)
      goto error;
  } else {
    self->priv->outbuf = cur_outbuf;
  }

  /* Convert GstBuffer to VvasVideoFrame for Core Scaler */
  priv->input_frame = vvas_videoframe_from_gstbuffer (self->priv->vvas_ctx,
      self->in_mem_bank, inbuf, priv->in_vinfo, GST_MAP_READ);
  if (!priv->input_frame) {
    GST_ERROR_OBJECT (self,
        "Could not convert input GstBuffer to VvasVideoFrame");
    fret = GST_FLOW_ERROR;
    goto error;
  }

  /* Add Output buffer to Core Scaler for processing */
  bret = vvas_xmulticrop_add_scaler_output_buffer_chnnels (self);
  if (!bret) {
    GST_ERROR_OBJECT (self, "Failed to add sub buffer channel");
    fret = GST_FLOW_ERROR;
    goto error;
  }

  /* Get total number of GstVideoRegionOfInterestMeta  metadata */
  roi_meta_count =
      gst_buffer_get_n_meta (inbuf, GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);

  if (roi_meta_count) {
    /* Get total count of roi-crop-meta metadata for dynamic crop */
    roi_meta_count = gst_vvas_xmultirop_get_roi_meta_count (inbuf);
    GST_DEBUG_OBJECT (self, "input buffer has %u roi_meta", roi_meta_count);
    bret = vvas_xmulticrop_prepare_crop_buffers (trans, inbuf);
    if (!bret) {
      fret = GST_FLOW_ERROR;
      goto error;
    }
  }

  /* Process output and dynamic buffers */
  bret = vvas_xmulticrop_process (self);
  if (!bret) {
    fret = GST_FLOW_ERROR;
    goto error;
  }

  /* Free all the VvasVideoFrames, as only GstBuffer is going to be pushed
   * downstream */
  for (idx = 0; idx < priv->sub_buffer_counter + 1; idx++) {
    if (self->priv->output_frames[idx]) {
      vvas_video_frame_free (self->priv->output_frames[idx]);
      self->priv->output_frames[idx] = NULL;
    }
  }
  if (self->priv->input_frame) {
    vvas_video_frame_free (self->priv->input_frame);
    self->priv->input_frame = NULL;
  }

  /* Copy metadta from input buffer to the output buffer */
  GST_DEBUG_OBJECT (self, "copying the metadta");
  gst_buffer_copy_into (cur_outbuf, inbuf,
      (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_META |
          GST_BUFFER_COPY_TIMESTAMPS), 0, -1);

  /* Scaling of input vvas metadata based on output resolution */
  in_meta = gst_buffer_get_meta (inbuf, gst_inference_meta_api_get_type ());

  if (in_meta) {
    GstVideoMetaTransform meta_trans =
        { self->priv->in_vinfo, priv->out_vinfo };
    GQuark scale_quark = gst_video_meta_transform_scale_get_quark ();

    GST_DEBUG_OBJECT (self, "attaching scaled inference metadata");
    in_meta->info->transform_func (cur_outbuf, (GstMeta *) in_meta,
        inbuf, scale_quark, &meta_trans);
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
    /* Need to copy output buffer to another small buffer as downstream
     * element doesn't understand GstVideoMeta */
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
    /* copy data from output buffer to the newly allocated buffer */
    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, self,
        "slow copy data from %p to %p", cur_outbuf, new_outbuf);
    gst_video_frame_copy (&new_frame, &out_frame);
    gst_video_frame_unmap (&out_frame);
    gst_video_frame_unmap (&new_frame);

    gst_buffer_copy_into (new_outbuf, cur_outbuf, GST_BUFFER_COPY_METADATA, 0,
        -1);
    /* free current output buffer as we have copied data to the new buffer */
    gst_buffer_unref (cur_outbuf);
    cur_outbuf = new_outbuf;
  }

  GST_DEBUG_OBJECT (self, "output %" GST_PTR_FORMAT, cur_outbuf);

  /* Return output buffer to the base class */
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

/**
 *  @fn static GstFlowReturn gst_vvas_xmulticrop_transform (GstBaseTransform * trans,
 *                                                          GstBuffer * inbuf,
 *                                                          GstBuffer * outbuf)
 *  @param [in] trans       - GstVvasXMultiCrop handle typecasted to GstBaseTransform
 *  @param [in] inbuf       - Input GstBuffer
 *  @param [out] outbuf     - Output buffer
 *  @return GstFlowReturn, GST_FLOW_OK on success.
 *  @brief  Transforms one incoming buffer to one outgoing buffer.
 *          The function is allowed to change size/timestamp/duration of the outgoing buffer.
 */
static GstFlowReturn
gst_vvas_xmulticrop_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  /* All functionality is implemented in gst_vvas_xmulticrop_generate_output(),
   * this method is registered to configure vvas_xmulticrop in transform mode
   */
  return GST_FLOW_OK;
}

/**
 *  @fn static void gst_vvas_xmulticrop_set_property (GObject * object,
 *                                                    guint property_id,
 *                                                    const GValue * value,
 *                                                    GParamSpec * pspec)
 *  @param [in] object      - GstVvasXMultiCrop typecasted to GObject
 *  @param [in] property_id - ID as defined in VvasXMultiCropProperties enum
 *  @param [in] value       - GValue which holds property value set by user
 *  @param [in] pspec       - Metadata of a property with property ID \p property_id
 *  @return None
 *  @brief  This API stores values sent from the user in GstVvasXMultiCrop object members.
 *  @details  This API is registered with GObjectClass by overriding GObjectClass::set_property
 *            function pointer and this will be invoked when developer sets properties on GstVvasXMultiCrop
 *            object. Based on property value type, corresponding g_value_get_xxx API will be called to get
 *            property value from GValue handle.
 */
static void
gst_vvas_xmulticrop_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (object);
  GST_DEBUG_OBJECT (self, "set_property, id: %u", property_id);
  /* User has set the value, store it in our context */
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
    case PROP_SOFTWARE_SCALING:
      self->software_scaling = g_value_get_boolean (value);
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

/**
 *  @fn static void gst_vvas_xmulticrop_get_property (GObject * object,
 *                                                    guint prop_id,
 *                                                    GValue * value,
 *                                                    GParamSpec * pspec)
 *  @param [in] object  - GstVvasXMultiCrop typecasted to GObject
 *  @param [in] prop_id - ID as defined in VvasXMultiCropProperties enum
 *  @param [out] value  - GValue which holds property value set by user
 *  @param [in] pspec   - Metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief  This API stores values from the GstVvasXMultiCrop object members into the value for user.
 *  @details  This API is registered with GObjectClass by overriding GObjectClass::get_property
 *            function pointer and this will be invoked when developer gets properties on GstVvasXMultiCrop
 *            object. Based on property value type, corresponding g_value_set_xxx API will be called to set
 *            property value to GValue handle.
 */
static void
gst_vvas_xmulticrop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (object);

  GST_DEBUG_OBJECT (self, "get_property, id: %u", prop_id);
  /* Based on the property asked by user set the values and return it */

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
    case PROP_SOFTWARE_SCALING:
      g_value_set_boolean (value, self->software_scaling);
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

/**
 *  @fn void gst_vvas_xmulticrop_finalize (GObject * object)
 *  @param [in] object  - GstVvasXMultiCrop handle typecasted to GObject.
 *  @return None
 *  @brief  This API will be called during GstVvasXMultiCrop object's destruction phase.
 *          Close references to devices and free memories if any.
 *  @note   After this API GstVvasXMultiCrop object \p object will be destroyed completely.
 *          So free all the internal memories held by current object.
 */
static void
gst_vvas_xmulticrop_finalize (GObject * object)
{
  GstVvasXMultiCrop *self = GST_VVAS_XMULTICROP (object);

  GST_DEBUG_OBJECT (self, "finalize");
  /* vvas_xmulticrop is getting de-structed, free the allocated resources */
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

  /* Destroy Core Scaler and VVAS Context */
  if (self->priv->vvas_scaler) {
    vvas_scaler_destroy (self->priv->vvas_scaler);
  }

  if (self->priv->vvas_ctx) {
    vvas_context_destroy (self->priv->vvas_ctx);
  }

  G_OBJECT_CLASS (gst_vvas_xmulticrop_parent_class)->finalize (object);
}

/**
 *  @fn static GstBufferPool * vvas_xmulticrop_allocate_buffer_pool (GstVvasXMultiCrop * self,
 *                                                                   guint width,
 *                                                                   guint height,
 *                                                                   GstVideoFormat format)
 *  @param [in] self    - Handle to GstVvasXMultiCrop instance
 *  @param [in] width   - width of the buffer
 *  @param [in] height  - height of the buffer
 *  @param [in] format  - format of the buffer
 *  @return Allocated GstBufferPool
 *  @brief  Allocates new VVAS buffer pool based on the width, height and format.
 */
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

  /* Create GstCaps from the given width, height and video format */
  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, gst_video_format_to_string (format),
      "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  if (!self->software_scaling) {
    /* Create new VVAS buffer pool with width and height alignement */
    pool = gst_vvas_buffer_pool_new (WIDTH_ALIGN, HEIGHT_ALIGN);
    GST_LOG_OBJECT (self, "allocated buffer pool %p", pool);

    config = gst_buffer_pool_get_config (pool);
    gst_video_alignment_reset (&align);
    /* Add padding info */
    align.padding_bottom = ALIGN (GST_VIDEO_INFO_HEIGHT (&info), HEIGHT_ALIGN)
        - GST_VIDEO_INFO_HEIGHT (&info);
    for (int idx = 0; idx < GST_VIDEO_INFO_N_PLANES (&info); idx++) {
      align.stride_align[idx] = (WIDTH_ALIGN - 1);
    }
    gst_video_info_align (&info, &align);

    GST_DEBUG_OBJECT (self,
        "Align padding: top:%d, bottom:%d, left:%d, right:%d",
        align.padding_top, align.padding_bottom, align.padding_left,
        align.padding_right);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, &align);

    /* Allocate new VVAS allocator */
    allocator = gst_vvas_allocator_new (self->dev_index,
        NEED_DMABUF, self->out_mem_bank);

    gst_allocation_params_init (&alloc_params);
    alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
    alloc_params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;
    GST_INFO_OBJECT (self,
        "allocated %" GST_PTR_FORMAT " allocator at mem bank %d", allocator,
        self->in_mem_bank);

    /* Set configuration to the buffer pool, 3 minimum buffers are required, there
     * is no max limit on the number of buffers, if there is no free buffer,
     * the pool will allocate new buffer
     */
    gst_buffer_pool_config_set_params (config, caps,
        GST_VIDEO_INFO_SIZE (&info), 3, 0);

    gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
    /* Add option to insert GstVideoMeta */
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    if (allocator)
      gst_object_unref (allocator);
  } else {
    pool = gst_video_buffer_pool_new ();
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps,
        GST_VIDEO_INFO_SIZE (&info), 3, 0);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

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

/**
 *  @fn static GstStateChangeReturn gst_vvas_xmulticrop_change_state (GstElement * element, GstStateChange transition)
 *  @param [in] element     - Handle to GstVvasXMultiCrop typecasted to GstElement.
 *  @param [in] transition  - The requested state transition.
 *  @return Status of the state transition.
 *  @brief  This API will be invoked whenever the pipeline is going into a state transition and in this function the
 *          element can can initialize any sort of specific data needed by the element.
 *  @details  This API is registered with GstElementClass by overriding GstElementClass::change_state function pointer
 *            and this will be invoked whenever the pipeline is going into a state transition.
 */
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
        /* READY -> PAUSED transition, pipeline is enabled by user, create
         * input, output queue and input_copy_thread
         */
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
          /* PAUSED -> READY transition, stop the input_copy_thread */
          g_async_queue_push (priv->copy_inqueue, STOP_COMMAND);
          GST_LOG_OBJECT (self, "waiting for copy input thread join");
          g_thread_join (priv->input_copy_thread);
          priv->input_copy_thread = NULL;
        }

        /* Free input copy queue */
        if (priv->copy_inqueue) {
          g_async_queue_unref (priv->copy_inqueue);
          priv->copy_inqueue = NULL;
        }

        /* Free input copy queue */
        if (priv->copy_outqueue) {
          g_async_queue_unref (priv->copy_outqueue);
          priv->copy_outqueue = NULL;
        }
      }

      /* If input pool was active, stop and free it */
      if (self->priv->input_pool
          && gst_buffer_pool_is_active (self->priv->input_pool)) {
        if (!gst_buffer_pool_set_active (self->priv->input_pool, FALSE))
          GST_ERROR_OBJECT (self, "failed to deactivate pool %" GST_PTR_FORMAT,
              self->priv->input_pool);
        gst_clear_object (&self->priv->input_pool);
        self->priv->input_pool = NULL;
      }

      /* Stop and free all the sub buffer pools */
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
      break;
    }

    default:
      break;
  }
  return ret;
}

/**
 *  @fn static gboolean vvas_xmulticrop_plugin_init (GstPlugin * plugin)
 *  @param [in] plugin - Handle to vvas_xmulticrop plugin
 *  @return TRUE if plugin initialized successfully
 *  @brief  This is a callback function that will be called by the loader at startup to register the plugin
 *  @note   It create a new element factory capable of instantiating objects of the type
 *          'GST_TYPE_VVAS_XMULTICROP' and adds the factory to plugin 'vvas_xmulticrop'
 */
static gboolean
vvas_xmulticrop_plugin_init (GstPlugin * plugin)
{
  /* Register vvas_xmulticrop plugin */
  return gst_element_register (plugin, "vvas_xmulticrop", GST_RANK_PRIMARY,
      GST_TYPE_VVAS_XMULTICROP);
}

/**
 *  @brief This macro is used to define the entry point and meta data of a plugin.
 *         This macro exports a plugin, so that it can be used by other applications
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xmulticrop,
    "Xilinx multi crop plugin to crop buffers statically and dynamically",
    vvas_xmulticrop_plugin_init, VVAS_API_VERSION, "MIT/X11",
    "Xilinx VVAS SDK plugin", "https://www.xilinx.com/")
