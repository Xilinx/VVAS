/*
 * Copyright (C) 2020 - 2022 Xilinx, Inc.  All rights reserved.
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
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
#include <vvas/vvas_structure.h>
#include <gst/vvas/gstvvasallocator.h>
#include <gst/vvas/gstvvasbufferpool.h>
#include <gst/vvas/gstinferencemeta.h>
#include <gst/vvas/gstvvashdrmeta.h>
#include <gst/vvas/gstvvasoverlaymeta.h>
#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include "gstvvas_xabrscaler.h"

#include <gst/vvas/gstvvascoreutils.h>
#include <vvas_core/vvas_context.h>
#include <vvas_core/vvas_common.h>
#include <vvas_core/vvas_scaler.h>

#ifdef ENABLE_XRM_SUPPORT
#include <xrm.h>
#include <xrm_limits.h>
#include <jansson.h>
/** @def XRM_PRECISION_1000000_BIT_MASK
 *  @brief Request load of the CU must be in the granularity of 1000000 and should in bit[27 -  8]
 */
#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))
#endif

#ifdef XLNX_PCIe_PLATFORM
/** @def DEFAULT_DEVICE_INDEX
 *  @brief Default device index value.
 */
#define DEFAULT_DEVICE_INDEX -1
/** @def DEFAULT_KERNEL_NAME
 *  @brief Default FPGA kernel name for scaler IP.
 */
#define DEFAULT_KERNEL_NAME "image_processing:{image_processing_1}"
/** @def NEED_DMABUF
 *  @brief Default value of option for exporting as DMA buffer in VVAS allocator.
 */
#define NEED_DMABUF 0
#else /* Embedded Platform */
/** @def DEFAULT_DEVICE_INDEX
 *  @brief Default device index value.
 */
#define DEFAULT_DEVICE_INDEX 0  /* on Embedded only one device i.e. device 0 */
/** @def DEFAULT_KERNEL_NAME
 *  @brief Default FPGA kernel name for scaler IP.
 */
#define DEFAULT_KERNEL_NAME "image_processing:{image_processing_1}"
/** @def NEED_DMABUF
 *  @brief Default option for exporint DMA buffer in VVAS allocator.
 */
#define NEED_DMABUF 1
#endif

/** @def VVAS_XABRSCALER_AVOID_OUTPUT_COPY_DEFAULT
 *  @brief Default value for avoid-output-copy property.
 */
#define VVAS_XABRSCALER_AVOID_OUTPUT_COPY_DEFAULT FALSE
/** @def VVAS_XABRSCALER_ENABLE_PIPELINE_DEFAULT
 *  @brief Default value for enable-pipeline property.
 */
#define VVAS_XABRSCALER_ENABLE_PIPELINE_DEFAULT FALSE
/** @def VVAS_XABRSCALER_ENABLE_SOFTWARE_SCALING_DEFAULT
 *  @brief Default value for software-scaling property.
 */
#define VVAS_XABRSCALER_ENABLE_SOFTWARE_SCALING_DEFAULT FALSE
/** @def STOP_COMMAND
 *  @brief Command to stop the input copy thread.
 */
#define STOP_COMMAND ((gpointer)GINT_TO_POINTER (g_quark_from_string("STOP")))

/** @def WIDTH_ALIGN
 *  @brief Alignment for width must be 8 * pixel per clock in case of embedded.
 */
#define WIDTH_ALIGN (8 * self->ppc)
/** @def HEIGHT_ALIGN
 *  @brief Alignment for height in case of embedded.
 */
#define HEIGHT_ALIGN 1

/** @def MIN_SCALAR_WIDTH
 *  @brief Minimum width for cropping, as per the alignment requirement of Scaler IP.
 */
#define MIN_SCALAR_WIDTH    16
/** @def MIN_SCALAR_HEIGHT
 *  @brief Minimum height for cropping, as per the alignment requirement of Scaler IP.
 */
#define MIN_SCALAR_HEIGHT   16

/** @def PP_ALPHA_DEFAULT_VALUE 
 *  @brief Default value for pre-processing alpha value.
 */
#define PP_ALPHA_DEFAULT_VALUE  0

/** @def PP_BETA_DEFAULT_VALUE 
 *  @brief Default value for pre-processing beta value.
 */
#define PP_BETA_DEFAULT_VALUE   1

#ifndef DEFAULT_MEM_BANK
/** @def DEFAULT_MEM_BANK
 *  @brief Default memory bank.
 */
#define DEFAULT_MEM_BANK 0
#endif

/** @def DEFAULT_VVAS_DEBUG_LEVEL
 *  @brief Default debug level for VVAS CORE.
 */
#define DEFAULT_VVAS_DEBUG_LEVEL        2

/** @def DEFAULT_VVAS_SCALER_DEBUG_LEVEL
 *  @brief Default debug level for VVAS CORE Scaler.
 */
#define DEFAULT_VVAS_SCALER_DEBUG_LEVEL 2

/**
 *  @brief Defines a static GstDebugCategory global variable "gst_vvas_xabrscaler_debug"
 */
GST_DEBUG_CATEGORY_STATIC (gst_vvas_xabrscaler_debug);
#define GST_CAT_DEFAULT gst_vvas_xabrscaler_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

/** @enum    VvasXAbrscalerProperties
 *  @brief   Enum for plugin properties.
 *  @note   Enum for plugin properties.
 */
typedef enum
{
  /** Gstreamer default added dummy property */
  PROP_0,
  /** Location of the xclbin to program devices */
  PROP_XCLBIN_LOCATION,
  /** String defining the kernel name and instance as mentioned in xclbin */
  PROP_KERN_NAME,
#ifdef XLNX_PCIe_PLATFORM
  /** Property to set device index, when there are multiple devices in a given card */
  PROP_DEVICE_INDEX,
#endif
  /** VVAS input memory bank to allocate memory */
  PROP_IN_MEM_BANK,
  /** VVAS output memory bank to allocate memory */
  PROP_OUT_MEM_BANK,
  /** Pixel per clock configured in Scaler kernel */
  PROP_PPC,
  /** Scale Mode configured in Scaler kernel. */
  PROP_SCALE_MODE,
  /** Number of filter taps to be used for scaling */
  PROP_NUM_TAPS,
  /** coefficients loading type for scaling */
  PROP_COEF_LOADING_TYPE,
  /** Avoid output frames copy on all source pads even when downstream does not support GstVideoMeta metadata */
  PROP_AVOID_OUTPUT_COPY,
  /** Enable buffer pipelining to improve performance in non zero-copy use cases */
  PROP_ENABLE_PIPELINE,
#ifdef ENABLE_PPE_SUPPORT
  /** PreProcessing parameter alpha red channel value */
  PROP_ALPHA_R,
  /** PreProcessing parameter alpha green channel value */
  PROP_ALPHA_G,
  /** PreProcessing parameter alpha blue channel value */
  PROP_ALPHA_B,
  /** PreProcessing parameter beta red channel value */
  PROP_BETA_R,
  /** PreProcessing parameter beta green channel value */
  PROP_BETA_G,
  /** PreProcessing parameter beta blue channel value */
  PROP_BETA_B,
#endif
#ifdef ENABLE_XRM_SUPPORT
  /** XRM Compute Unit Resource Pool Reservation id */
  PROP_RESERVATION_ID,
#endif
  /** Crop X coordinate */
  PROP_CROP_X,
  /** Crop Y coordinate */
  PROP_CROP_Y,
  /** Width of the cropping segment */
  PROP_CROP_WIDTH,
  /** Height of the cropping segment */
  PROP_CROP_HEIGHT,
  /** Software scaling */
  PROP_SOFTWARE_SCALING,
} VvasXAbrscalerProperties;

/** @enum ColorDomain
 *  @brief Enum for representing different color domain based input/output format.
 *  @note Enum for representing different color domain based input/output format.
 */
typedef enum ColorDomain
{
  /** YUV color format */
  COLOR_DOMAIN_YUV,
  /** RGB color format */
  COLOR_DOMAIN_RGB,
  /** GRAY color format */
  COLOR_DOMAIN_GRAY,
  /** When its none of above formats */
  COLOR_DOMAIN_UNKNOWN,
} ColorDomain;

/**
 *  @brief Defines plugin's sink pad template.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGBx, YUY2, r210, Y410, NV16, NV12, RGB, v308, I422_10LE, GRAY8, \
	NV12_10LE32, BGRx, GRAY10_LE32, BGRx, UYVY, BGR, RGBA, BGRA, I420, GBR}")));

/**
 *  @brief Defines plugin's source pad template.
 */
static GstStaticPadTemplate src_request_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGBx, YUY2, r210, Y410, NV16, NV12, RGB, v308, I422_10LE, GRAY8, \
      NV12_10LE32, BGRx, GRAY10_LE32, BGRx, UYVY, BGR, RGBA, BGRA, I420, GBR}")));

static GType gst_vvas_xabrscaler_pad_get_type (void);

typedef struct _GstVvasXAbrScalerPad GstVvasXAbrScalerPad;
typedef struct _GstVvasXAbrScalerPadClass GstVvasXAbrScalerPadClass;

/** @struct _GstVvasXAbrScalerPad
 *  @brief  Structure for GstVvasXAbrScaler's source pad of GstPad parent.
 */
struct _GstVvasXAbrScalerPad
{
  GstPad parent;
  /** Index of this pad in the list of source pads created */
  guint index;
  /** Pool of output buffers */
  GstBufferPool *pool;
  /** Video Info of input buffer */
  GstVideoInfo *in_vinfo;
  /** Video Info of output buffer */
  GstVideoInfo *out_vinfo;
};

/** @struct _GstVvasXAbrScalerPadClass
 *  @brief  Structure for GstVvasXAbrScalerPad class of GstPadClass parent class type.
 */
struct _GstVvasXAbrScalerPadClass
{
  GstPadClass parent;
};

/** @def GST_TYPE_VVAS_XABRSCALER_PAD
 *  @brief Macro to get GstVvasXAbrScalerPad object type
 */
#define GST_TYPE_VVAS_XABRSCALER_PAD \
  (gst_vvas_xabrscaler_pad_get_type())

/** @def GST_VVAS_XABRSCALER_PAD
 *  @brief Macro to typecast parent object to GstVvasXAbrScalerPad object
 */
#define GST_VVAS_XABRSCALER_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VVAS_XABRSCALER_PAD, \
             GstVvasXAbrScalerPad))

/** @def GST_VVAS_XABRSCALER_PAD_CLASS
 *  @brief Macro to typecast parent class to GstVvasXAbrScalerPadClass
 */
#define GST_VVAS_XABRSCALER_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VVAS_XABRSCALER_PAD, \
          GstVvasXAbrScalerPadClass))

/** @def GST_IS_VVAS_XABRSCALER_PAD
 *  @brief Macro to validate whether object is of GstVvasXAbrScalerPad type
 */
#define GST_IS_VVAS_XABRSCALER_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VVAS_XABRSCALER_PAD))

/** @def GST_IS_VVAS_XABRSCALER_PAD_CLASS
 *  @brief Macro to validate whether class is of GstVvasXAbrScalerPadClass type
 */
#define GST_IS_VVAS_XABRSCALER_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VVAS_XABRSCALER_PAD))

/** @def GST_VVAS_XABRSCALER_PAD_CAST
 *  @brief Macro to typecast parent object to GstVvasXAbrScalerPad object
 */
#define GST_VVAS_XABRSCALER_PAD_CAST(obj) \
  ((GstVvasXAbrScalerPad *)(obj))

/** @def G_DEFINE_TYPE
 *  @brief Macro to define a new type GstVvasXAbrScalerPad, of parent type GST_TYPE_PAD
 */
G_DEFINE_TYPE (GstVvasXAbrScalerPad, gst_vvas_xabrscaler_pad, GST_TYPE_PAD);

/** @def gst_vvas_xabrscaler_srcpad_at_index
 *  @brief Macro function to return source pad (GstVvasXAbrScalerPad) at index "idx" in the list.
 */
#define gst_vvas_xabrscaler_srcpad_at_index(self, idx) ((GstVvasXAbrScalerPad *)(g_list_nth ((self)->srcpads, idx))->data)

/** @def gst_vvas_xabrscaler_srcpad_get_index
 *  @brief Macro function to return the index of source pad (GstVvasXAbrScalerPad) in the list.
 */
#define gst_vvas_xabrscaler_srcpad_get_index(self, srcpad) (g_list_index ((self)->srcpads, (gconstpointer)srcpad))

/** @def DEFAULT_CROP_PARAMS
 *  @brief Macro for default value of crop parameters.
 */
#define DEFAULT_CROP_PARAMS 0

/**
 *  @fn static void gst_vvas_xabrscaler_pad_class_init (GstVvasXAbrScalerPadClass * klass)
 *  @param [in] klass  - Handle to GstVvasXAbrScalerPadClass
 *  @return None
 *  @brief  One of the constructor functions called for GstVvasXAbrScalerPad
 */
static void
gst_vvas_xabrscaler_pad_class_init (GstVvasXAbrScalerPadClass * klass)
{
  /* nothing */
}

/**
 *  @fn static void gst_vvas_xabrscaler_pad_init (GstVvasXAbrScalerPad * pad)
 *  @param [in] pad  - Handle to GstVvasXAbrScalerPad instance
 *  @return None
 *  @brief  One of the constructor functions called for GstVvasXAbrScalerPad
 */
static void
gst_vvas_xabrscaler_pad_init (GstVvasXAbrScalerPad * pad)
{
  /* nothing */
}

static void gst_vvas_xabrscaler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vvas_xabrscaler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_vvas_xabrscaler_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_vvas_xabrscaler_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static gboolean gst_vvas_xabrscaler_sink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GstStateChangeReturn gst_vvas_xabrscaler_change_state
    (GstElement * element, GstStateChange transition);
static GstCaps *gst_vvas_xabrscaler_transform_caps (GstVvasXAbrScaler * self,
    GstPadDirection direction, GstCaps * caps, GstCaps * peercaps,
    GstCaps * filter);
static GstPad *gst_vvas_xabrscaler_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name_templ, const GstCaps * caps);
static void gst_vvas_xabrscaler_release_pad (GstElement * element,
    GstPad * pad);

static gboolean remove_infer_meta (GstBuffer * buffer, GstMeta ** meta,
    gpointer user_data);

/** @struct _GstVvasXAbrScalerPrivate
 *  @brief  Structure with internal private data members for abrscaler plugin.
 */
struct _GstVvasXAbrScalerPrivate
{
  /** Holds the Video info of input video buffers */
  GstVideoInfo *in_vinfo;
  /** Handle to XRT device object created for specified device index */
  vvasDeviceHandle dev_handle;
  /** Array of output buffers to be pushed out for each input */
  GstBuffer *outbufs[MAX_CHANNELS];
  /** Input buffer pool */
  GstBufferPool *input_pool;
  /** Flag to check if the internal buffer pool is set to active */
  gboolean validate_import;
  /** Boolean flag to specify if buffer copy is required before pushing the buffer */
  gboolean need_copy[MAX_CHANNELS];
  /** Thread ID for thread doing the input buffer copy to internal buffers */
  GThread *input_copy_thread;
  /** Queue to hold incoming buffers */
  GAsyncQueue *copy_inqueue;
  /** Queue to hold input internal buffers  */
  GAsyncQueue *copy_outqueue;
  /** Flag to mark first frame arrival */
  gboolean is_first_frame;
  /** XCLBIN Identifier, which will be available after xclbin download */
  uuid_t xclbinId;
  /** VVAS Core Context */
  VvasContext *vvas_ctx;
  /** VVAS Core Scaler Context */
  VvasScaler *vvas_scaler;
  /** Reference of input VvasVideoFrames */
  VvasVideoFrame *input_frame[MAX_CHANNELS];
  /** Reference of output VvasVideoFrames */
  VvasVideoFrame *output_frame[MAX_CHANNELS];
#ifdef ENABLE_XRM_SUPPORT
  /** XRM Context handle */
  xrmContext xrm_ctx;
  /** XRM Compute Resource handle */
  xrmCuResource *cu_resource;
  /** XRM Compute Resource handle version 2 */
  xrmCuResourceV2 *cu_resource_v2;
  /** Holds the current Compute Unit load requirement */
  gint cur_load;
  /** Reservation id is to allocate cu from specified resource pool. */
  guint64 reservation_id;
  /** Flag to mark if any error during XRM APIs*/
  gboolean has_error;
#endif
};

/** @def gst_vvas_xabrscaler_parent_class
 *  @brief Macro to declare a static variable named parent_class pointing to the parent class
 */
#define gst_vvas_xabrscaler_parent_class parent_class

/** @def G_DEFINE_TYPE_WITH_PRIVATE
 *  @brief Macro to define a new type GstVvasXAbrScaler, of parent type GST_TYPE_ELEMENT
 */
G_DEFINE_TYPE_WITH_PRIVATE (GstVvasXAbrScaler, gst_vvas_xabrscaler,
    GST_TYPE_ELEMENT);

/** @def GST_VVAS_XABRSCALER_PRIVATE
 *  @brief Macro to get GstVvasXAbrScalerPrivate object instance of GstVvasXAbrScaler
 */
#define GST_VVAS_XABRSCALER_PRIVATE(self) (GstVvasXAbrScalerPrivate *) (gst_vvas_xabrscaler_get_instance_private (self))

#ifdef XLNX_PCIe_PLATFORM
/** @def VVAS_XABRSCALER_DEFAULT_NUM_TAPS
 *  @brief Default taps for PCIe platform 12
 */
#define VVAS_XABRSCALER_DEFAULT_NUM_TAPS 12

/** @def VVAS_XABRSCALER_DEFAULT_PPC
 *  @brief Default PPC for PCIe platform 4
 */
#define VVAS_XABRSCALER_DEFAULT_PPC 4

/** @def VVAS_XABRSCALER_SCALE_MODE
 *  @brief Default scaling mode set to POLYPHASE
 */
#define VVAS_XABRSCALER_SCALE_MODE 2

#else /* Xilinx Embedded Platform */

/** @def VVAS_XABRSCALER_DEFAULT_NUM_TAPS
 *  @brief Default taps for Embedded platform 6
 */
#define VVAS_XABRSCALER_DEFAULT_NUM_TAPS 6

/** @def VVAS_XABRSCALER_DEFAULT_PPC
 *  @brief Default PPC for Embedded platform 2
 */
#define VVAS_XABRSCALER_DEFAULT_PPC 2

/** @def VVAS_XABRSCALER_SCALE_MODE
 *  @brief Default scaling mode set to BILINEAR
 */
#define VVAS_XABRSCALER_SCALE_MODE 0

#endif /* End of XLNX_PCIe_PLATFORM */

/** @def VVAS_XABRSCALER_NUM_TAPS_TYPE
 *  @brief Macro just for the replaement of function call vvas_xabrscaler_num_taps_type ()
 */
#define VVAS_XABRSCALER_NUM_TAPS_TYPE (vvas_xabrscaler_num_taps_type ())

/** @def VVAS_XABRSCALER_PPC_TYPE
 *  @brief Macro just for the replaement of function call vvas_xabrscaler_ppc_type ()
 */
#define VVAS_XABRSCALER_PPC_TYPE (vvas_xabrscaler_ppc_type ())

/**
 *  @fn static GstCaps *vvas_xabrscaler_generate_caps (void)
 *  @return Returns GstCaps pointer.
 *  @brief This function generates GstCaps based on scaler capabilities
 */
static GstCaps *
vvas_xabrscaler_generate_caps (void)
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
 *  @fn static guint vvas_xabrscaler_get_stride (GstVideoInfo * info, guint width)
 *  @param [in] info           - Video Info
 *  @param [in] width          - Width of the frame
 *  @return Returns stride for given width.
 *  @brief  This function calculates the stride for given width as per the format of the
 *          buffer.
 */
static guint
vvas_xabrscaler_get_stride (GstVideoInfo * info, guint width)
{
  guint stride = 0;

  switch (GST_VIDEO_INFO_FORMAT (info)) {
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
      GST_ERROR ("Not supporting %s yet",
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)));
      stride = 0;
      break;
  }
  return stride;
}

/**
 *  @fn static ColorDomain vvas_xabrscaler_get_color_domain (const gchar * color_format)
 *  @param [in] color_format           - Gstreamer format string.
 *  @return Returns ColorDomain for corresponding format string passed.
 *  @brief  This function maps the Gstreamer format string to ColorDomain enum.
 *
 */
static ColorDomain
vvas_xabrscaler_get_color_domain (const gchar * color_format)
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

/**
 *  @fn static GType vvas_xabrscaler_num_taps_type (void)
 *  @param void
 *  @return Returns the GEnumValue for all supported scaler filter co-efficient taps
 *  @brief  This function just returns the GEnumValue for all taps supported by scaler IP filter
 *          co-efficients.
 */
static GType
vvas_xabrscaler_num_taps_type (void)
{
  static GType num_tap = 0;

  if (!num_tap) {
    /* List of number of taps to be chosen for co-efficients */
    static const GEnumValue taps[] = {
      {6, "6 taps filter for scaling", "6"},
      {8, "8 taps filter for scaling", "8"},
      {10, "10 taps filter for scaling", "10"},
      {12, "12 taps filter for scaling", "12"},
      {0, NULL, NULL}
    };
    /* Registers a new static enumeration type with the name GstVvasXAbrScalerNumTapsType. */
    num_tap = g_enum_register_static ("GstVvasXAbrScalerNumTapsType", taps);
  }
  return num_tap;
}

/**
 *  @fn static GType vvas_xabrscaler_ppc_type (void)
 *  @param void
 *  @return Returns the GEnumValue for all PPC supported by scaler IP.
 *  @brief  This function just returns the GEnumValue for all pixel per clock (PPC) supported by scaler IP.
 *          co-efficients.
 */
static GType
vvas_xabrscaler_ppc_type (void)
{
  static GType num_ppc = 0;

  if (!num_ppc) {
    /* List of PPC supported by scaler IP */
    static const GEnumValue ppc[] = {
      {1, "1 Pixel Per Clock", "1"},
      {2, "2 Pixels Per Clock", "2"},
      {4, "4 Pixels Per Clock", "4"},
      {0, NULL, NULL}
    };
    /* Registers a new static enumeration type with the name GstVvasXAbrScalerPPCType. */
    num_ppc = g_enum_register_static ("GstVvasXAbrScalerPPCType", ppc);
  }
  return num_ppc;
}

#ifdef XLNX_PCIe_PLATFORM
/** @def VVAS_XABRSCALER_DEFAULT_COEF_LOAD_TYPE
 *  @brief Default coefficient loading type
 */
#define VVAS_XABRSCALER_DEFAULT_COEF_LOAD_TYPE COEF_AUTO_GENERATE
#else
/** @def VVAS_XABRSCALER_DEFAULT_COEF_LOAD_TYPE
 *  @brief Default coefficient loading type
 */
#define VVAS_XABRSCALER_DEFAULT_COEF_LOAD_TYPE COEF_FIXED
#endif

/** @def VVAS_XABRSCALER_COEF_LOAD_TYPE
 *  @brief Macro just for the replacement of function call vvas_xabrscaler_coef_load_type ()
 */
#define VVAS_XABRSCALER_COEF_LOAD_TYPE (vvas_xabrscaler_coef_load_type ())

/**
 *  @fn static GType vvas_xabrscaler_coef_load_type (void)
 *  @param void
 *  @return Returns the GEnumValue for co-efficients types supported by scaler IP.
 *  @brief  This function just returns the GEnumValue for co-efficients types supported by scaler IP.
 */
static GType
vvas_xabrscaler_coef_load_type (void)
{
  static GType load_type = 0;

  if (!load_type) {
    /* List of co-efficient load types supported */
    static const GEnumValue load_types[] = {
      {COEF_FIXED, "Use fixed filter coefficients", "fixed"},
      {COEF_AUTO_GENERATE, "Auto generate filter coefficients", "auto"},
      {0, NULL, NULL}
    };
    /* Registers a new static enumeration type with the name GstVvasXAbrScalerCoefLoadType. */
    load_type =
        g_enum_register_static ("GstVvasXAbrScalerCoefLoadType", load_types);
  }
  return load_type;
}

/**
 *  @fn static uint32_t xlnx_multiscaler_stride_align (uint32_t stride_in, uint16_t AXIMMDataWidth)
 *  @param [in] stride_in           - Stride to verify.
 *  @param [in] AXIMMDataWidth      - Scaler's AXI memory mapped data width.
 *  @return Returns the stride aligned to AXIMMDataWidth.
 *  @brief  Stride must be aligned to AXIMMDataWidth, this function will make it aligned to
 *          AXIMMDataWidth.
 */
static uint32_t
xlnx_multiscaler_stride_align (uint32_t stride_in, uint16_t AXIMMDataWidth)
{
  uint32_t stride;
  stride =
      (((stride_in) + AXIMMDataWidth - 1) / AXIMMDataWidth) * AXIMMDataWidth;
  return stride;
}

/**
 *  @fn static gboolean vvas_xabrscaler_register_prep_write_with_caps (GstVvasXAbrScaler * self,
 *                        guint chan_id, GstCaps * in_caps, GstCaps * out_caps)
 *  @param [in] self              - Handle to GstVvasXAbrScaler instance.
 *  @param [in] chan_id           - Source pad index for which the co-efficients are prepared.
 *  @param [in] in_caps           - Input caps.
 *  @param [in] out_caps          - Output caps.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief This function updates video info for input and output.
 *
 */
static gboolean
vvas_xabrscaler_register_prep_write_with_caps (GstVvasXAbrScaler * self,
    guint chan_id, GstCaps * in_caps, GstCaps * out_caps)
{
  GstVvasXAbrScalerPad *srcpad = NULL;
  guint width = 0;

  srcpad = gst_vvas_xabrscaler_srcpad_at_index (self, chan_id);

  /* Update video info for input */
  if (!gst_video_info_from_caps (srcpad->in_vinfo, in_caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from input caps");
    return FALSE;
  }

  /* Update video info for output */
  if (!gst_video_info_from_caps (srcpad->out_vinfo, out_caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from output caps");
    return FALSE;
  }

  /* Verify if the output width is aligned to PPC */
  width = GST_VIDEO_INFO_WIDTH (srcpad->out_vinfo);
  if (width % self->ppc) {
    GST_ERROR_OBJECT (self, "Unsupported output resolution,"
        "width must be multiple of ppc i.e, %d", self->ppc);
    return FALSE;
  }

  return TRUE;
}

#ifdef ENABLE_XRM_SUPPORT
/**
 *  @fn static gchar *vvas_xabrscaler_prepare_request_json_string (GstVvasXAbrScaler * scaler)
 *  @param [in] scaler           - Handle to GstVvasXAbrScaler instance.
 *  @return On Success returns the composed string for requesting processing load.\n On Failure returns NULL
 *  @brief  This function will compose the request string required to be sent for XRM plguins
 *          for getting required load based on input and output caps.
 */
static gchar *
vvas_xabrscaler_prepare_request_json_string (GstVvasXAbrScaler * scaler)
{
  json_t *in_jobj, *jarray, *fps_jobj = NULL, *tmp_jobj, *tmp2_jobj, *res_jobj;
  guint in_width, in_height;
  guint in_fps_n, in_fps_d;
  gint idx;
  gchar *req_str = NULL;

  jarray = json_array ();

  /* Runs for all output source pads */
  for (idx = 0; idx < scaler->num_request_pads; idx++) {
    GstVvasXAbrScalerPad *srcpad;
    GstCaps *out_caps = NULL;
    guint out_fps_n, out_fps_d;
    GstVideoInfo out_vinfo;
    guint out_width, out_height;
    gboolean bret;
    json_t *out_jobj = NULL;
    /* Get the source pad at index "idx" */
    srcpad = gst_vvas_xabrscaler_srcpad_at_index (scaler, idx);
    if (!srcpad) {
      GST_ERROR_OBJECT (scaler, "failed to get srcpad at index %d", idx);
      goto out_error;
    }
    /* Get current set caps on this source pad */
    out_caps = gst_pad_get_current_caps ((GstPad *) srcpad);
    if (!out_caps) {
      GST_ERROR_OBJECT (scaler, "failed to get output caps at srcpad index %d",
          idx);
      goto out_error;
    }
    /* Get the video info output of caps, which has information regarding the
     * width, height etc */
    bret = gst_video_info_from_caps (&out_vinfo, out_caps);
    if (!bret) {
      GST_ERROR_OBJECT (scaler, "failed to get video info from caps");
      goto out_error;
    }

    /* Fetch output height, width and fps details from Gstreamer video info */
    out_width = GST_VIDEO_INFO_WIDTH (&out_vinfo);
    out_height = GST_VIDEO_INFO_HEIGHT (&out_vinfo);
    out_fps_n = GST_VIDEO_INFO_FPS_N (&out_vinfo);
    out_fps_d = GST_VIDEO_INFO_FPS_D (&out_vinfo);

    /* Taking the worst case fps for the load calculation, when fps info is not
     * available in the output caps */
    if (!out_fps_n) {
      GST_WARNING_OBJECT (scaler,
          "out fps is not available, taking default 60/1");
      out_fps_n = 60;
      out_fps_d = 1;
    }
    /* Start building the JSON stype string */
    out_jobj = json_object ();
    if (!out_jobj)
      goto out_error;
    /* Update output height and width */
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
  /* After adding all the output caps of all output source pads, now add details regarding the
   * input video info */
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

  json_object_set_new (in_jobj, "width", json_integer (in_width));
  json_object_set_new (in_jobj, "height", json_integer (in_height));

  fps_jobj = json_object ();
  if (!fps_jobj)
    goto error;

  json_object_set_new (fps_jobj, "num", json_integer (in_fps_n));
  json_object_set_new (fps_jobj, "den", json_integer (in_fps_d));
  json_object_set_new (in_jobj, "frame-rate", fps_jobj);

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
  GST_LOG_OBJECT (scaler, "prepared xrm request %s", req_str);
  json_decref (tmp_jobj);

  return req_str;

error:
  return NULL;
}

/**
 *  @fn static gboolean vvas_xabrscaler_calculate_load (GstVvasXAbrScaler * self, gint * load)
 *  @param [in] self             - Handle to GstVvasXAbrScaler instance.
 *  @param [out] load            - FPGA load needed for the given scaling operation.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  This function will calculate the processing load required from FPGA
 *          for performing the scaling operation, which will be calculated based on
 *          input and output caps.
 */
static gboolean
vvas_xabrscaler_calculate_load (GstVvasXAbrScaler * self, gint * load)
{
  GstVvasXAbrScalerPrivate *priv = self->priv;
  int iret = -1, func_id = 0;
  gchar *req_str;
  char plugin_name[XRM_MAX_NAME_LEN];
  xrmPluginFuncParam param;
  if (!priv->xrm_ctx) {
    GST_ERROR_OBJECT (self, "xrm context not created");
    return FALSE;
  }

  /* prepare json string to request xrm for load */
  req_str = vvas_xabrscaler_prepare_request_json_string (self);
  if (!req_str) {
    GST_ERROR_OBJECT (self, "failed to prepare xrm json request string");
    return FALSE;
  }

  memset (plugin_name, 0x0, XRM_MAX_NAME_LEN);

  strcpy (plugin_name, "xrmU30ScalPlugin");

  if (strlen (req_str) > (XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1)) {
    GST_ERROR_OBJECT (self, "request input string length %lu > max allowed %d",
        strlen (req_str), XRM_MAX_PLUGIN_FUNC_PARAM_LEN - 1);
    free (req_str);
    return FALSE;
  }
  /* Copy the prepared JSON load request string to XRM plugin parameter */
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
  /* Check if the load calculated is valid */
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

/**
 *  @fn static gboolean vvas_xabrscaler_allocate_xrm_resource (GstVvasXAbrScaler * self, gint scaler_load)
 *  @param [in] self             - Handle to GstVvasXAbrScaler instance.
 *  @param [in] scaler_load      - Requested load in the granularity of 1000000
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  This function will allocate the requested processing load from FPGA using XRM (Xilinx FPGA Resource Manager).
 */
static gboolean
vvas_xabrscaler_allocate_xrm_resource (GstVvasXAbrScaler * self,
    gint scaler_load)
{
  GstVvasXAbrScalerPrivate *priv = self->priv;
  int iret = -1;
  GST_INFO_OBJECT (self, "going to request %d%% load using xrm",
      (scaler_load * 100) / XRM_MAX_CU_LOAD_GRANULARITY_1000000);

  /* Reservation id can be read from the environment variable or from the
   * plugin property
   * Reservation id is to allocate cu from specified resource pool.
   */
  if (getenv ("XRM_RESERVE_ID") || priv->reservation_id) {
    /* use reservation_id to allocate scaler */
    int xrm_reserve_id = 0;
    xrmCuPropertyV2 scaler_prop;
    xrmCuResourceV2 *cu_resource;
    guint k_name_len;

    memset (&scaler_prop, 0, sizeof (xrmCuPropertyV2));

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

    scaler_prop.poolId = xrm_reserve_id;

    /* the kernel name requested. */
    k_name_len = strchr (self->kern_name, ':') - self->kern_name;

    if (!k_name_len || (k_name_len >= XRM_MAX_NAME_LEN)) {
      return FALSE;
    }
    strncpy (scaler_prop.kernelName, self->kern_name, k_name_len);
    strcpy (scaler_prop.kernelAlias, "SCALER_MPSOC");
    /* request exclusive device usage for this client. */
    scaler_prop.devExcl = false;

    /* requestLoad: request load, only one type granularity at one time.
     *        bit[31 - 28] reserved
     *        bit[27 -  8] granularity of 1000000 (0 - 1000000)
     *        bit[ 7 -  0] granularity of 100 (0 - 100)
     *
     *   Hence the bit mask macro XRM_PRECISION_1000000_BIT_MASK
     */
    scaler_prop.requestLoad = XRM_PRECISION_1000000_BIT_MASK (scaler_load);

    /* This condition is when the user asks for resource from a specific device */
    if (self->dev_index != -1) {
      uint64_t deviceInfoContraintType =
          XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX;
      uint64_t deviceInfoDeviceIndex = self->dev_index;

      scaler_prop.deviceInfo =
          (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) |
          (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
    }
    /* Allocates compute unit with a device */
    iret = xrmCuAllocV2 (priv->xrm_ctx, &scaler_prop, cu_resource);

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
    uuid_copy (priv->xclbinId, cu_resource->uuid);
    priv->cu_resource_v2 = cu_resource;

  } else {
    /* use user specified device to allocate scaler */
    xrmCuProperty scaler_prop;
    xrmCuResource *cu_resource;
    guint k_name_len;

    memset (&scaler_prop, 0, sizeof (xrmCuProperty));

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

    k_name_len = strchr (self->kern_name, ':') - self->kern_name;

    if (!k_name_len || (k_name_len >= XRM_MAX_NAME_LEN)) {
      return FALSE;
    }
    strncpy (scaler_prop.kernelName, self->kern_name, k_name_len);
    strcpy (scaler_prop.kernelAlias, "SCALER_MPSOC");
    scaler_prop.devExcl = false;
    scaler_prop.requestLoad = XRM_PRECISION_1000000_BIT_MASK (scaler_load);
    iret = xrmCuAllocFromDev (priv->xrm_ctx, self->dev_index, &scaler_prop,
        cu_resource);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (self, "failed to allocate resources from device id %d. "
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

    self->dev_index = cu_resource->deviceId;
    uuid_copy (priv->xclbinId, cu_resource->uuid);
    priv->cu_resource = cu_resource;
  }
  return TRUE;
}

/**
 *  @fn static gboolean vvas_xabrscaler_destroy_xrm_resource (GstVvasXAbrScaler * self)
 *  @param [in] self  - Handle to GstVvasXAbrScaler instance.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  Cleans up XRM and XRT context.
 *  @details Cleans up XRM and XRT context.
 */
static gboolean
vvas_xabrscaler_destroy_xrm_resource (GstVvasXAbrScaler * self)
{
  GstVvasXAbrScalerPrivate *priv = self->priv;
  gboolean has_error = FALSE;
  gint iret;


  if (priv->cu_resource_v2) {
    gboolean bret;
    /* Release XRM Compute Unit resource back to pool */
    bret = xrmCuReleaseV2 (priv->xrm_ctx, priv->cu_resource_v2);
    if (!bret) {
      GST_ERROR_OBJECT (self, "failed to release CU");
      has_error = TRUE;
    }
    /* Clean up XRM context */
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
    /* Release XRM Compute Unit resource back to pool */
    bret = xrmCuRelease (priv->xrm_ctx, priv->cu_resource);
    if (!bret) {
      GST_ERROR_OBJECT (self, "failed to release CU");
      has_error = TRUE;
    }
    /* Clean up XRM context */
    iret = xrmDestroyContext (priv->xrm_ctx);
    if (iret != XRM_SUCCESS) {
      GST_ERROR_OBJECT (self, "failed to destroy xrm context");
      has_error = TRUE;
    }
    free (priv->cu_resource);
    priv->cu_resource = NULL;
    GST_INFO_OBJECT (self, "released CU and destroyed xrm context");
  }

  return has_error ? FALSE : TRUE;
}
#endif

/**
 *  @fn static gboolean vvas_xabrscaler_allocate_internal_pool (GstVvasXAbrScaler * self)
 *
 *  @param [in] self  - Handle to GstVvasXAbrScaler instance.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  Allocates internal buffer pool.
 *  @details This function will be invoked to create internal buffer pool when
 *           the received buffer is non VVAS buffer or non DMA buffer.
 */
static gboolean
vvas_xabrscaler_allocate_internal_pool (GstVvasXAbrScaler * self)
{
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstCaps *caps = NULL;
  GstVideoAlignment align;

  /* Get current set caps on the sink pad */
  caps = gst_pad_get_current_caps (self->sinkpad);

  if (caps == NULL) {
    GST_WARNING_OBJECT (self, "Failed to get caps on sinkpad");
    return FALSE;
  }

  /* Get video info from caps */
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }
  /* Create VVAS buffer pool with aligned width and height */
  pool = gst_vvas_buffer_pool_new (WIDTH_ALIGN, HEIGHT_ALIGN);
  GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

  /* Get config of the pool and populate with padding and alignment information */
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

  allocator = gst_vvas_allocator_new (self->dev_index,
      NEED_DMABUF, self->in_mem_bank);
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

/** @fn gboolean vvas_xabrscaler_validate_buffer_import (GstVvasXAbrScaler * scaler,
 *                                                       GstBuffer * inbuf,
 *                                                       gboolean * use_inpool)
 *
 *  @param [in] scaler - scaler context
 *  @param [in] inbuf - Input buffer
 *  @param [out] use_inpool - Set to true to use internal pool, false to use upstream pool
 *
 *  @return On Success returns true
 *          On Failure returns false
 *
 *  @brief Validates input buffer so as to decide internal pool or upstream pool.
*/
static gboolean
vvas_xabrscaler_validate_buffer_import (GstVvasXAbrScaler * self,
    GstBuffer * inbuf, gboolean * use_inpool)
{
  gboolean bret = TRUE;
  GstMemory *in_mem = NULL;
  GstVideoMeta *vmeta;

  in_mem = gst_buffer_get_memory (inbuf, 0);

  if (in_mem == NULL) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    bret = FALSE;
    goto exit;
  }

  /* First check the type of memory */
  if (gst_is_vvas_memory (in_mem)) {
    if (!gst_vvas_memory_can_avoid_copy (in_mem, self->dev_index,
            self->in_mem_bank)) {
      /* VVAS memory, but can't avoid copy, so use internal pool */
      *use_inpool = TRUE;
      goto exit;
    }
  } else if (gst_is_dmabuf_memory (in_mem)) {
#ifdef XLNX_PCIe_PLATFORM
    /* In case of PCIe, there are issues in DMA buffer import BO
     * Hence copy the data into vvas buffer */
    *use_inpool = TRUE;
    goto exit;
#else
    /* Embedded Platform */
    vvasBOHandle bo = NULL;
    gint dma_fd = -1;

    /* Get the dma descriptor assiciated with in_mem */
    dma_fd = gst_dmabuf_memory_get_fd (in_mem);

    if (dma_fd < 0) {
      GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
      *use_inpool = FALSE;
      bret = FALSE;
      goto exit;
    }

    /* Get XRT BO handle for dma memory */
    bo = vvas_xrt_import_bo (self->priv->dev_handle, dma_fd);

    if (bo == NULL) {
      GST_WARNING_OBJECT (self,
          "failed to get XRT BO...fall back to copy input");
      *use_inpool = TRUE;
      goto exit;
    }

    /* Free bo */
    vvas_xrt_free_bo (bo);
#endif
  } else {
    /* Software buffer, so need to copy */
    *use_inpool = TRUE;
    goto exit;
  }

  /* So far, same input buffer seems to be usable. Now check if it
   * meets alignment requirements */

  vmeta = gst_buffer_get_video_meta (inbuf);

  if (vmeta) {
    gint align_elevation;

    GST_LOG_OBJECT (self, "input buffer offset[0] = %lu, offset[1] = %lu, "
        "stride[0] = %d, stride[1] = %d, size = %lu", vmeta->offset[0],
        vmeta->offset[1], vmeta->stride[0], vmeta->stride[1],
        gst_buffer_get_size (inbuf));

    align_elevation = (vmeta->offset[1] - vmeta->offset[0]) / vmeta->stride[0];

    if (vmeta->stride[0] % WIDTH_ALIGN || align_elevation % HEIGHT_ALIGN) {
      *use_inpool = TRUE;
      GST_DEBUG_OBJECT (self,
          "strides & offsets are not matching, use our internal pool");
      goto exit;
    }
  } else {
    /* vmeta not present, so use internal pool */
    *use_inpool = TRUE;
  }

exit:
  GST_INFO_OBJECT (self, "going to use %s pool as input pool",
      *use_inpool ? "internal" : "upstream");

  if (in_mem)
    gst_memory_unref (in_mem);
  return bret;
}

/**
 *  @fn gboolean vvas_xabrscaler_prepare_input_buffer (GstVvasXAbrScaler * self,
 *  						       GstBuffer ** inbuf)
 *
 *  @param [in] self  - Handle to GstVvasXAbrScaler instance.
 *  @param [in] inbuf  - input buffer coming from upstream.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief   Decides and prepares internal buffer pool if necessary.
 *  @details Checks if the incoing buffer is VVAS memory or DMA buffer, if
 *           neighther of them, then it creates an internal buffer pool.
 */
static gboolean
vvas_xabrscaler_prepare_input_buffer (GstVvasXAbrScaler * self,
    GstBuffer ** inbuf)
{
  GstVvasXAbrScalerPrivate *priv = self->priv;
  GstMemory *in_mem = NULL;
  GstVideoFrame in_vframe, own_vframe;
  gboolean bret;
  GstBuffer *own_inbuf;
  GstFlowReturn fret;
  gboolean use_inpool = FALSE;

  memset (&in_vframe, 0x0, sizeof (GstVideoFrame));
  memset (&own_vframe, 0x0, sizeof (GstVideoFrame));

  /* Get memory object from GstBuffer */
  in_mem = gst_buffer_get_memory (*inbuf, 0);
  if (!in_mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    goto error;
  }

  gst_memory_unref (in_mem);
  in_mem = NULL;

  /* Check if input buffer is meeting hardware requirements or not */
  bret = vvas_xabrscaler_validate_buffer_import (self, *inbuf, &use_inpool);

  if (!bret)
    goto error;

  /* Check if we have to create an internal buffer pool */
  if (use_inpool) {
    if (self->priv->validate_import) {
      if (!self->priv->input_pool) {
        /* Create an internal buffer pool */
        bret = vvas_xabrscaler_allocate_internal_pool (self);
        if (!bret)
          goto error;
      }

      if (!gst_buffer_pool_is_active (self->priv->input_pool))
        gst_buffer_pool_set_active (self->priv->input_pool, TRUE);

      self->priv->validate_import = FALSE;
    }
    /* If the pipelining is enabled, pop the buffer from output queue */
    if (self->enabled_pipeline) {
      own_inbuf = g_async_queue_try_pop (priv->copy_outqueue);
      if (!own_inbuf && !priv->is_first_frame) {
        own_inbuf = g_async_queue_pop (priv->copy_outqueue);
      }

      priv->is_first_frame = FALSE;
      /* Push the incoming buffer to input queue */
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
        gst_video_frame_unmap (&own_vframe);
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
  /* syncs data when XLNX_SYNC_TO_DEVICE flag is enabled */

  /* Get memory object from GstBuffer */
  in_mem = gst_buffer_get_memory (*inbuf, 0);

  if (!in_mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    goto error;
  }

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
 *  @fn static gboolean vvas_xabrscaler_prepare_output_buffer (GstVvasXAbrScaler * self)
 *
 *  @param [in] self  - Handle to GstVvasXAbrScaler instance.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief   Prepare the output buffer as per its type.
 *  @details Acquire the output buffer from pool and get its physical address
 *           based on whether it's a VVAS allocator memory or DMA memory.
 */
static gboolean
vvas_xabrscaler_prepare_output_buffer (GstVvasXAbrScaler * self)
{
  guint chan_id;
  GstMemory *mem = NULL;
  GstVvasXAbrScalerPad *srcpad = NULL;
  /* Run for all source pads/output pads */
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    GstBuffer *outbuf = NULL;
    GstFlowReturn fret;
    guint64 phy_addr = -1;

    /* Get the source pad of the of chan_id */
    srcpad = gst_vvas_xabrscaler_srcpad_at_index (self, chan_id);

    /* Take out a buffer from pool */
    fret = gst_buffer_pool_acquire_buffer (srcpad->pool, &outbuf, NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (srcpad, "failed to allocate buffer from pool %p",
          srcpad->pool);
      goto error;
    }
    GST_LOG_OBJECT (srcpad, "acquired buffer %p from pool", outbuf);

    /* Store the output buffer in array with respect to its output channel id */
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
    if (gst_is_vvas_memory (mem)) {
      phy_addr = gst_vvas_allocator_get_paddr (mem);
    } else if (gst_is_dmabuf_memory (mem)) {
      vvasBOHandle bo = NULL;
      gint dma_fd = -1;
      /* Get DMA fd for the memory */
      dma_fd = gst_dmabuf_memory_get_fd (mem);
      if (dma_fd < 0) {
        GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
        goto error;
      }

      /* Get the XRT bo corresponding to the DMA fd */
      bo = vvas_xrt_import_bo (self->priv->dev_handle, dma_fd);
      if (bo == NULL) {
        GST_WARNING_OBJECT (self,
            "failed to get XRT BO...fall back to copy input");
      }

      GST_INFO_OBJECT (self, "received dma fd %d and its xrt BO = %p", dma_fd,
          bo);
      /* Get the physical address of the XRT bo */
      phy_addr = vvas_xrt_get_bo_phy_addres (bo);

      if (bo != NULL)
        vvas_xrt_free_bo (bo);
    }

    GST_DEBUG_OBJECT (self, "Output physical address: %lu", phy_addr);
    gst_memory_unref (mem);
  }

  return TRUE;

error:
  if (mem)
    gst_memory_unref (mem);

  return FALSE;
}

/**
 *  @fn static gpointer vvas_xabrscaler_input_copy_thread (gpointer data)
 *
 *  @param [In] data  - Handle to GstVvasXAbrScaler instance, passed as callback argument.
 *  @return Returns NULL on error else continue untill STOP_COMMAND is received.
 *
 *  @brief   Thread to optimize input buffer copy into input buffer pool.
 *  @details This thread is started while transiting from READY to PAUSE state
 *           if ENABLE_PIPELINE property is set.
 *
 */
static gpointer
vvas_xabrscaler_input_copy_thread (gpointer data)
{
  GstVvasXAbrScaler *self = GST_VVAS_XABRSCALER (data);
  GstVvasXAbrScalerPrivate *priv = self->priv;

  while (1) {
    GstBuffer *inbuf = NULL;
    GstBuffer *own_inbuf = NULL;
    GstVideoFrame in_vframe, own_vframe;
    GstFlowReturn fret = GST_FLOW_OK;

    /* Pop out the input buffer coming from upstream */
    inbuf = (GstBuffer *) g_async_queue_pop (priv->copy_inqueue);
    /* We run until STOP_COMMAND is received */
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
    /* copy the content into our internal buffer */
    gst_video_frame_copy (&own_vframe, &in_vframe);

    gst_video_frame_unmap (&in_vframe);
    gst_video_frame_unmap (&own_vframe);
    /* Copy if input buffer is carrying some meta data with it */
    gst_buffer_copy_into (own_inbuf, inbuf,
        (GstBufferCopyFlags) (GST_BUFFER_COPY_METADATA), 0, -1);
    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, self,
        "slow copy to internal input pool buffer");
    gst_buffer_unref (inbuf);
    /* Pushed the prepared buffer into queue */
    g_async_queue_push (priv->copy_outqueue, own_inbuf);
  }

error:
  return NULL;
}

/**
 *  @fn static gboolean vvas_xabrscaler_sync_buffer (GstVvasXAbrScaler * self)
 *  @param [In] self   - Handle to GstVvasXAbrScaler instance.
 *
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *
 *  @brief   This function sets the SYNC_FROM_DEVICE on all the output buffers.
 *  @details SYNC_FROM_DEVICE flag is needed for syncing data to the host side memory.
 *           This flag may be used when this buffer is mapped in reading mode.
 *
 */
static gboolean
vvas_xabrscaler_sync_buffer (GstVvasXAbrScaler * self)
{
  GstVvasXAbrScalerPrivate *priv = self->priv;
  uint32_t chan_id = 0;
  GstMemory *mem = NULL;

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    mem = gst_buffer_get_memory (priv->outbufs[chan_id], 0);
    if (mem == NULL) {
      GST_ERROR_OBJECT (self,
          "chan-%d : failed to get memory from output buffer", chan_id);
      return FALSE;
    }
    gst_vvas_memory_set_sync_flag (mem, VVAS_SYNC_FROM_DEVICE);
    gst_memory_unref (mem);
  }
  return TRUE;
}

/**
 *  @fn static void gst_vvas_xabrscaler_finalize (GObject * object)
 *  @param [In] object   - Handle to GstVvasXAbrScaler instance typecasted as GstObject
 *
 *  @return None
 *
 *  @brief  This API will be called during GstVvasXAbrScaler object's destruction phase.
 *          Close references to devices and free memories if any
 *  @note After this API GstVvasXAbrScaler object \p obj will be destroyed completely.
 *        So free all internal memories held by current object
 *
 */
static void
gst_vvas_xabrscaler_finalize (GObject * object)
{
  GstVvasXAbrScaler *self = GST_VVAS_XABRSCALER (object);

  g_hash_table_unref (self->pad_indexes);
  gst_video_info_free (self->priv->in_vinfo);

  g_free (self->kern_name);
  g_free (self->xclbin_path);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 *  @fn static void gst_vvas_xabrscaler_class_init (GstVvasXAbrScalerClass * klass)
 *  @param [in]klass  - Handle to GstVvasXAbrScalerClass
 *  @return None
 *  @brief  Add properties and signals of GstVvasXAbrScaler to parent GObjectClass and
 *          ovverrides function pointers present in itself and/or its parent class structures
 *  @details This function publishes properties those can be set/get from application on GstVvasXAbrScaler object.
 *                  And, while publishing a property it also declares type, range of acceptable values, default value,
 *                  readability/writability and in which GStreamer state a property can be changed.
 */
static void
gst_vvas_xabrscaler_class_init (GstVvasXAbrScalerClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstCaps *caps;
  GstPadTemplate *pad_templ;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xabrscaler_debug, "vvas_xabrscaler",
      0, "Xilinx's Multiscaler 2.0 plugin");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");

  /* Set callback functions to be called when a plugin property is set or get */
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xabrscaler_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xabrscaler_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_vvas_xabrscaler_finalize);

  /* Update details like description, long name, author */
  gst_element_class_set_details_simple (gstelement_class,
      "Xilinx XlnxAbrScaler Scaler plugin",
      "Multi scaler 2.0 xrt plugin for vitis kernels",
      "Multi Scaler 2.0 plugin using XRT", "Xilinx Inc <www.xilinx.com>");

  caps = vvas_xabrscaler_generate_caps ();
  if (caps) {
    pad_templ = gst_pad_template_new ("sink",
        GST_PAD_SINK, GST_PAD_ALWAYS, caps);
    /* Add sink and source templates to element based scaler core supported formats */
    gst_element_class_add_pad_template (gstelement_class, pad_templ);
    pad_templ = gst_pad_template_new ("src_%u",
        GST_PAD_SRC, GST_PAD_REQUEST, caps);
    gst_element_class_add_pad_template (gstelement_class, pad_templ);
    gst_caps_unref (caps);
  } else {
    /* Add sink and source templates to element based on static templates */
    gst_element_class_add_static_pad_template (gstelement_class,
        &sink_template);
    gst_element_class_add_static_pad_template (gstelement_class,
        &src_request_template);
  }

  /* Assign callback to notify state changes in the pipeline */
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vvas_xabrscaler_change_state);
  /* Assign callback to notify whenever a new request source pad is requested */
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_vvas_xabrscaler_request_new_pad);
  /* Assign callback to notify whenever a request source pad is released */
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_vvas_xabrscaler_release_pad);

  /* Install all properties listed in VvasXAbrscalerProperties */
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
          "Valid Device index is 0 to 31. Default value is set to -1 intentionally so that user provides the correct device index.",
          -1, 31, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

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

  g_object_class_install_property (gobject_class, PROP_OUT_MEM_BANK,
      g_param_spec_uint ("out-mem-bank", "VVAS Output Memory Bank",
          "VVAS output memory bank to allocate memory",
          0, G_MAXUSHORT, DEFAULT_MEM_BANK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_PPC,
      g_param_spec_enum ("ppc", "pixel per clock",
          "Pixel per clock configured in Multiscaler kernel",
          VVAS_XABRSCALER_PPC_TYPE, VVAS_XABRSCALER_DEFAULT_PPC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject_class, PROP_SCALE_MODE,
      g_param_spec_int ("scale-mode", "Scaling Mode",
          "Scale Mode configured in Multiscaler kernel.   \
  0: BILINEAR \n 1: BICUBIC \n2: POLYPHASE", 0, 2, VVAS_XABRSCALER_SCALE_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_NUM_TAPS,
      g_param_spec_enum ("num-taps", "Number filter taps",
          "Number of filter taps to be used for scaling",
          VVAS_XABRSCALER_NUM_TAPS_TYPE, VVAS_XABRSCALER_DEFAULT_NUM_TAPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_COEF_LOADING_TYPE,
      g_param_spec_enum ("coef-load-type", "Coefficients loading type",
          "coefficients loading type for scaling",
          VVAS_XABRSCALER_COEF_LOAD_TYPE,
          VVAS_XABRSCALER_DEFAULT_COEF_LOAD_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_AVOID_OUTPUT_COPY,
      g_param_spec_boolean ("avoid-output-copy",
          "Avoid output frames copy",
          "Avoid output frames copy on all source pads even when downstream"
          " does not support GstVideoMeta metadata",
          VVAS_XABRSCALER_AVOID_OUTPUT_COPY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_ENABLE_PIPELINE,
      g_param_spec_boolean ("enable-pipeline",
          "Enable pipelining",
          "Enable buffer pipelining to improve performance in non zero-copy use cases",
          VVAS_XABRSCALER_ENABLE_PIPELINE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_CROP_X,
      g_param_spec_uint ("crop-x", "Crop X coordinate",
          "Crop X coordinate",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_CROP_Y,
      g_param_spec_uint ("crop-y", "Crop Y coordinate",
          "Crop Y coordinate",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_CROP_WIDTH,
      g_param_spec_uint ("crop-width", "Crop width",
          "Crop width (minimum: 16), if crop-x is set, but crop-width is 0 or not set,"
          "\n\t\t\tcrop-width will be calculated as input width - crop-x",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_CROP_HEIGHT,
      g_param_spec_uint ("crop-height", "Crop height",
          "Crop height (minimum: 16), if crop-y is set, but crop-height is 0 or not set,"
          "\n\t\t\tcrop-height will be calculated as input height - crop-y",
          0, G_MAXUINT, DEFAULT_CROP_PARAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_SOFTWARE_SCALING,
      g_param_spec_boolean ("software-scaling",
          "Flag to to enable software scaling flow.",
          "Set this flag to true in case of software scaling.",
          VVAS_XABRSCALER_ENABLE_SOFTWARE_SCALING_DEFAULT,
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

/**
 *  @fn static void gst_vvas_xabrscaler_init (GstVvasXAbrScaler * self)
 *  @param [in] self  - Handle to GstVvasXAbrScaler instance
 *  @return None
 *  @brief  Initilizes GstVvasXAbrScaler member variables to default and does one
 *          time object/memory allocations in object's lifecycle
 *  @details Overrides GstVvasXAbrScaler object's base class function pointers so that
 *           GstVvasXAbrScaler APIs will be invoked.
 *           Ex: Chain function, Event function, Query function etc.
 */
static void
gst_vvas_xabrscaler_init (GstVvasXAbrScaler * self)
{
  gint idx;
  GstVvasXAbrScalerClass *klass;
  GstPadTemplate *pad_template;

  self->priv = GST_VVAS_XABRSCALER_PRIVATE (self);
  klass = GST_VVAS_XABRSCALER_GET_CLASS (self);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  /* Create GstPad instance from the sink template */
  self->sinkpad = gst_pad_new_from_template (pad_template, "sink");

  /* Assign callback to be called whenever an event is pushed over sink pad */
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xabrscaler_sink_event));
  /* Assign callback to be called whenever a GstBuffer is pushed over sink pad */
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xabrscaler_chain));
  /* Assign callback to be called whenever a query is pushed over sink pad */
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xabrscaler_sink_query));
  /* Add the created pad to element */
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  /* Initialize all internal structure members */
  self->xclbin_path = NULL;
  self->num_request_pads = 0;
  self->pad_indexes = g_hash_table_new (NULL, NULL);
  self->srcpads = NULL;
  self->num_taps = VVAS_XABRSCALER_DEFAULT_NUM_TAPS;
  self->coef_load_type = VVAS_XABRSCALER_DEFAULT_COEF_LOAD_TYPE;
  self->avoid_output_copy = VVAS_XABRSCALER_AVOID_OUTPUT_COPY_DEFAULT;
  self->software_scaling = VVAS_XABRSCALER_ENABLE_SOFTWARE_SCALING_DEFAULT;
#ifdef ENABLE_PPE_SUPPORT
  self->alpha_r = PP_ALPHA_DEFAULT_VALUE;
  self->alpha_g = PP_ALPHA_DEFAULT_VALUE;
  self->alpha_b = PP_ALPHA_DEFAULT_VALUE;
  self->beta_r = PP_BETA_DEFAULT_VALUE;
  self->beta_g = PP_BETA_DEFAULT_VALUE;
  self->beta_b = PP_BETA_DEFAULT_VALUE;
  self->get_pp_config = TRUE;
#endif
  self->dev_index = DEFAULT_DEVICE_INDEX;
  self->ppc = VVAS_XABRSCALER_DEFAULT_PPC;
  self->scale_mode = VVAS_XABRSCALER_SCALE_MODE;
  self->in_mem_bank = DEFAULT_MEM_BANK;
  self->out_mem_bank = DEFAULT_MEM_BANK;
  self->crop_x = DEFAULT_CROP_PARAMS;
  self->crop_y = DEFAULT_CROP_PARAMS;
  self->crop_width = DEFAULT_CROP_PARAMS;
  self->crop_height = DEFAULT_CROP_PARAMS;

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
  self->priv->vvas_ctx = NULL;
  self->priv->vvas_scaler = NULL;
  self->kern_name = g_strdup (DEFAULT_KERNEL_NAME);
  gst_video_info_init (self->priv->in_vinfo);

  /* To begin with, lets just say, buffer needs to be copied
   * to host side before pushing out the output buffer */
  for (idx = 0; idx < MAX_CHANNELS; idx++) {
    self->priv->need_copy[idx] = TRUE;
    /* Let's just consider there is no alignment requirements to begin with */
    self->out_stride_align[idx] = 1;
    self->out_elevation_align[idx] = 1;
  }
}

/**
 *  @fn static GstPad *gst_vvas_xabrscaler_request_new_pad (GstElement * element,
 *    GstPadTemplate * templ, const gchar * name_templ, const GstCaps * caps)
 *
 *  @param [in] element      - Handle to GstVvasXAbrScaler instance
 *  @param [in] templ        - Pad template
 *  @param [in] name_templ   - Pad name.
 *  @param [in] caps         - Pad capabilities.
 *
 *  @return Returns the newly created pad.
 *  @brief   This function will get invoked whenever user requests for a new source pad.
 *  @details Create the pad as per input values and add to the element.
 *
 */
static GstPad *
gst_vvas_xabrscaler_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name_templ, const GstCaps * caps)
{
  GstVvasXAbrScaler *self = GST_VVAS_XABRSCALER (element);
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

  /* Check if we have reached the limit of maximum outputs from scaler */
  if (self->num_request_pads == MAX_CHANNELS) {
    GST_ERROR_OBJECT (self, "reached maximum supported channels");
    GST_OBJECT_UNLOCK (self);
    return NULL;
  }

  /* Create the pad name, it must be unique, src_0, src_1, src_2 .... */
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
  /* Add the index of the created pad to the has table */
  g_hash_table_insert (self->pad_indexes, GUINT_TO_POINTER (index), NULL);

  name = g_strdup_printf ("src_%u", index);

  srcpad = GST_PAD_CAST (g_object_new (GST_TYPE_VVAS_XABRSCALER_PAD,
          "name", name, "direction", templ->direction, "template", templ,
          NULL));
  GST_VVAS_XABRSCALER_PAD_CAST (srcpad)->index = index;
  g_free (name);

  GST_VVAS_XABRSCALER_PAD_CAST (srcpad)->in_vinfo = gst_video_info_new ();
  gst_video_info_init (GST_VVAS_XABRSCALER_PAD_CAST (srcpad)->in_vinfo);

  GST_VVAS_XABRSCALER_PAD_CAST (srcpad)->out_vinfo = gst_video_info_new ();
  gst_video_info_init (GST_VVAS_XABRSCALER_PAD_CAST (srcpad)->out_vinfo);

  /* Add the newly created source pad to the list */
  self->srcpads = g_list_append (self->srcpads,
      GST_VVAS_XABRSCALER_PAD_CAST (srcpad));
  self->num_request_pads++;

  GST_OBJECT_UNLOCK (self);
  /* Add newly created pad to the element */
  gst_element_add_pad (GST_ELEMENT_CAST (self), srcpad);

  return srcpad;
}

/**
 *  @fn static void gst_vvas_xabrscaler_release_pad (GstElement * element, GstPad * pad)
 *
 *  @param [in] element      - Handle to GstVvasXAbrScaler instance
 *  @param [in] pad          - GsPad object to be released.
 *
 *  @return None
 *
 *  @brief   This function will get invoked whenever a request pad is to be released.
 *  @details Detaches the pad from the element and will be freed up.
 *
 */
static void
gst_vvas_xabrscaler_release_pad (GstElement * element, GstPad * pad)
{
  GstVvasXAbrScaler *self;
  GstVvasXAbrScalerPad *srcpad;
  guint index;
  GList *lsrc = NULL;

  self = GST_VVAS_XABRSCALER (element);
  srcpad = GST_VVAS_XABRSCALER_PAD_CAST (pad);

  GST_OBJECT_LOCK (self);

  if (GST_STATE (self) > GST_STATE_NULL) {
    GST_ERROR_OBJECT (self,
        "releasing pads is supported only when state is NULL");
    GST_OBJECT_UNLOCK (self);
    return;
  }

  /* Check if the pad to be released is in our list */
  lsrc = g_list_find (self->srcpads, srcpad);
  if (!lsrc) {
    GST_ERROR_OBJECT (self, "could not find pad to release");
    GST_OBJECT_UNLOCK (self);
    return;
  }

  gst_video_info_free (srcpad->in_vinfo);
  gst_video_info_free (srcpad->out_vinfo);

  /* Remove the pad from the list */
  self->srcpads = g_list_remove (self->srcpads, srcpad);
  index = srcpad->index;
  GST_DEBUG_OBJECT (self, "releasing pad with index = %d", index);
  g_hash_table_remove (self->pad_indexes, GUINT_TO_POINTER (index));
  /* Update the number of output sourc pads */
  self->num_request_pads--;

  GST_OBJECT_UNLOCK (self);

  gst_object_ref (pad);
  /* Detach the pad from the element */
  gst_element_remove_pad (GST_ELEMENT_CAST (self), pad);

  gst_pad_set_active (pad, FALSE);

  gst_object_unref (pad);
}

/**
 *  @fn static void gst_vvas_xabrscaler_set_property (GObject * object,
 *                                                    guint prop_id,
 *                                                    const GValue * value,
 *                                                    GParamSpec * pspec)
 *  @param [in] object     - Handle to GstVvasXAbrScaler typecasted to GObject
 *  @param [in] prop_id    - Property ID as defined in VvasXAbrscalerProperties enum
 *  @param [in] value      - GValue which holds property value set by user
 *  @param [in] pspec      - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief   This API stores values sent from the user in GstVvasXAbrScaler object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::set_property function pointer and
 *           this will be invoked when developer sets properties on GstVvasXAbrScaler object. Based on property
 *           value type, corresponding g_value_get_xxx API will be called to get property value from GValue handle.
 */
static void
gst_vvas_xabrscaler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvasXAbrScaler *self = GST_VVAS_XABRSCALER (object);

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
#ifdef ENABLE_XRM_SUPPORT
    case PROP_RESERVATION_ID:
      self->priv->reservation_id = g_value_get_uint64 (value);
      break;
#endif
    case PROP_CROP_X:
      self->crop_x = g_value_get_uint (value);
      break;
    case PROP_CROP_Y:
      self->crop_y = g_value_get_uint (value);
      break;
    case PROP_CROP_WIDTH:
      self->crop_width = g_value_get_uint (value);
      break;
    case PROP_CROP_HEIGHT:
      self->crop_height = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 *  @fn static void gst_vvas_xabrscaler_get_property (GObject * object,
 *                                                    guint prop_id,
 *                                                    const GValue * value,
 *                                                    GParamSpec * pspec)
 *  @param [in] object     - Handle to GstVvasXAbrScaler typecasted to GObject
 *  @param [in] prop_id    - Property ID as defined in VvasXAbrscalerProperties enum
 *  @param [out] value      - GValue which holds property value set by user
 *  @param [in] pspec      - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief   This API gives out values asked from the user in GstVvasXAbrScaler object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::get_property function pointer
 *           and this will be invoked when developer gets properties on GstVvasXAbrScaler object. Based on property
 *           value type, corresponding g_value_get_xxx API will be called to get property value from GValue handle.
 */
static void
gst_vvas_xabrscaler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvasXAbrScaler *self = GST_VVAS_XABRSCALER (object);

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
#ifdef ENABLE_XRM_SUPPORT
    case PROP_RESERVATION_ID:
      g_value_set_uint64 (value, self->priv->reservation_id);
      break;
#endif
    case PROP_CROP_X:
      g_value_set_uint (value, self->crop_x);
      break;
    case PROP_CROP_Y:
      g_value_set_uint (value, self->crop_y);
      break;
    case PROP_CROP_WIDTH:
      g_value_set_uint (value, self->crop_width);
      break;
    case PROP_CROP_HEIGHT:
      g_value_set_uint (value, self->crop_height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 *  @fn static GstStateChangeReturn gst_vvas_xabrscaler_change_state (GstElement * element, GstStateChange transition)
 *  @param [in] element       - Handle to GstVvasXAbrScaler typecasted to GObject.
 *  @param [in] transition    - The requested state transition.
 *  @return Status of the state transition.
 *  @brief   This API will be invoked whenever the pipeline is going into a state transition and in this function the
 *          element can can initialize any sort of specific data needed by the element.
 *  @details This API is registered with GstElementClass by overriding GstElementClass::change_state function pointer and
 *           this will be invoked whenever the pipeline is going into a state transition.
 *
 */
static GstStateChangeReturn
gst_vvas_xabrscaler_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVvasXAbrScaler *self = GST_VVAS_XABRSCALER (element);
  GstVvasXAbrScalerPrivate *priv = self->priv;
  GstStateChangeReturn ret;
  guint idx = 0;
  VvasReturnType vret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{

      VvasLogLevel core_log_level =
          vvas_get_core_log_level (gst_debug_category_get_threshold
          (gst_vvas_xabrscaler_debug));

      /* Use the xclbin only in hw processing */
      if (self->software_scaling) {
        if (self->xclbin_path)
          g_free (self->xclbin_path);
        self->xclbin_path = NULL;
      } else {
        if (self->xclbin_path == NULL) {
          GST_ERROR_OBJECT (self, "xclbin-location is not set");
          return GST_STATE_CHANGE_FAILURE;
        }
      }

      /*
       * Create VVAS Context, Create Scaler context and
       * Set Scaler Properties
       */
      GST_DEBUG_OBJECT (self, "Creating VVAS context, XCLBIN: %s",
          self->xclbin_path);
      priv->vvas_ctx =
          vvas_context_create (self->dev_index, self->xclbin_path,
          core_log_level, &vret);
      if (!priv->vvas_ctx) {
        GST_ERROR_OBJECT (self, "Couldn't create VVAS context");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
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
        priv->is_first_frame = TRUE;
        priv->copy_inqueue =
            g_async_queue_new_full ((void (*)(void *)) gst_buffer_unref);
        priv->copy_outqueue =
            g_async_queue_new_full ((void (*)(void *)) gst_buffer_unref);

        /* Creat the thread to optimize input buffer copy into input buffer pool. */
        priv->input_copy_thread = g_thread_new ("abr-input-copy-thread",
            vvas_xabrscaler_input_copy_thread, self);
      }
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:

      priv->is_first_frame = FALSE;

      if (self->enabled_pipeline) {
        /* Terminate the copy thread */
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

      for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
        GstVvasXAbrScalerPad *srcpad =
            gst_vvas_xabrscaler_srcpad_at_index (self, idx);
        if (srcpad->pool && gst_buffer_pool_is_active (srcpad->pool)) {
          if (!gst_buffer_pool_set_active (srcpad->pool, FALSE))
            GST_ERROR_OBJECT (self,
                "failed to deactivate pool %" GST_PTR_FORMAT, srcpad->pool);
          gst_clear_object (&srcpad->pool);
          srcpad->pool = NULL;
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
#ifdef ENABLE_XRM_SUPPORT
      vvas_xabrscaler_destroy_xrm_resource (self);
#endif

      /*  Destroy Scaler and Vvas Context */
      if (self->priv->vvas_scaler) {
        vvas_scaler_destroy (self->priv->vvas_scaler);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (self->priv->vvas_ctx) {
        vvas_context_destroy (self->priv->vvas_ctx);
      }
      break;
    default:
      break;
  }

  return ret;
}

/**
 *  @fn static GstCaps *gst_vvas_xabrscaler_fixate_caps (GstVvasXAbrScaler * self,
 *                                      GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
 *  @param [in] self                 - Handle to GstVvasXAbrScaler typecasted to GObject.
 *  @param [in] direction            - Direction of the pad (src or sink)
 *  @param [in] caps                 - Given caps
 *  @param [in] othercaps            - Caps to fixate
 *  @return Fixated version of "othercaps"
 *  @brief  Given the pad in this direction and the given caps, fixate the caps on the other pad.
 *  @details The function takes ownership of othercaps and returns a fixated version of othercaps.
 *           othercaps is not guaranteed to be writable.
 *
 */
static GstCaps *
gst_vvas_xabrscaler_fixate_caps (GstVvasXAbrScaler * self,
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
 *  @fn static GstCaps *gst_vvas_xabrscaler_transform_caps (GstVvasXAbrScaler * self,
 *                                                          GstPadDirection direction,
 *                                                          GstCaps * caps, GstCaps * peercaps,
 *                                                          GstCaps * filter)
 *  @param [in] self                 - Handle to GstVvasXAbrScaler typecasted to GObject.
 *  @param [in] direction            - Direction of the pad (src or sink)
 *  @param [in] caps                 - Given caps
 *  @param [in] peercaps             - Caps from the peer pad.
 *  @param [in] filter               - Caps to filter out, when given
 *  @return transformed caps of peercaps
 *  @brief  Given the pad in this direction and the given caps, this function will find what caps are
 *          allowed on the other pad in this element.
 *
 */
static GstCaps *
gst_vvas_xabrscaler_transform_caps (GstVvasXAbrScaler * self,
    GstPadDirection direction, GstCaps * caps, GstCaps * peercaps,
    GstCaps * filter)
{
  GstCaps *ret;
  GstStructure *structure, *peer_structure;
  GstCapsFeatures *features;
  gint i, n, j, pn;
  const gchar *in_format, *out_format;
  ColorDomain in_color_domain, out_color_domain = COLOR_DOMAIN_UNKNOWN;
  gboolean color_domain_match = FALSE;

  GST_DEBUG_OBJECT (self,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  ret = gst_caps_new_empty ();
  n = gst_caps_get_size (caps);
  pn = gst_caps_get_size (peercaps);
  /* Run for all structures in the input caps */
  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure (caps, i);
    features = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (ret, structure, features))
      continue;

    /* make copy */
    structure = gst_structure_copy (structure);

    /* Find the color domain on source pad to find
     * if colorimetry information can be preserved in the caps */
    in_format = gst_structure_get_string (structure, "format");
    in_color_domain = vvas_xabrscaler_get_color_domain (in_format);

    if (in_color_domain != COLOR_DOMAIN_UNKNOWN) {
      /* Run for all structures in the peer caps */
      for (j = 0; j < pn; j++) {
        const GValue *targets;
        peer_structure = gst_caps_get_structure (peercaps, j);
        targets = gst_structure_get_value (peer_structure, "format");

        if (G_TYPE_CHECK_VALUE_TYPE (targets, G_TYPE_STRING)) {

          out_format = g_value_get_string (targets);
          out_color_domain = vvas_xabrscaler_get_color_domain (out_format);

          if (out_color_domain == in_color_domain) {
            GST_DEBUG_OBJECT (self, "color_domain_matched to %s", out_format);
            color_domain_match = TRUE;
          }

        } else if (G_TYPE_CHECK_VALUE_TYPE (targets, GST_TYPE_LIST)) {
          gint j, m;

          m = gst_value_list_get_size (targets);
          for (j = 0; j < m; j++) {
            const GValue *val = gst_value_list_get_value (targets, j);

            out_format = g_value_get_string (val);
            out_color_domain = vvas_xabrscaler_get_color_domain (out_format);

            if (out_color_domain == in_color_domain) {
              GST_DEBUG_OBJECT (self, "color_domain_matched to %s", out_format);
              color_domain_match = TRUE;
              break;
            }
          }
        }
      }
    }

    /* If the features are non-sysmem we can only do passthrough */
    if (!gst_caps_features_is_any (features)
        && gst_caps_features_is_equal (features,
            GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY)) {
      gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

      /* If color domain matches, no need to remove colorimetry information */
      if (color_domain_match == FALSE) {
        gst_structure_remove_fields (structure, "format", "colorimetry",
            "chroma-site", NULL);
      } else {
        gst_structure_remove_fields (structure, "format", "chroma-site", NULL);
      }

      /* if pixel aspect ratio, make a range of it */
      if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
        gst_structure_set (structure, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
      }
    }
    /* Append the finalized structure to caps that needs to be returned. */
    gst_caps_append_structure_full (ret, structure,
        gst_caps_features_copy (features));
  }

  /* If we have a caps of interest then intersect between "filter" caps and "ret"
   * caps that we have composed. */
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

/**
 *  @fn static GstCaps *gst_vvas_xabrscaler_find_transform (GstVvasXAbrScaler * self,
 *                                                          GstPad * pad,
 *                                                          GstPad * otherpad,
 *                                                          GstCaps * caps)
 *  @param [in] self                 - Handle to GstVvasXAbrScaler typecasted to GObject.
 *  @param [in] pad                  - Pad with known caps.
 *  @param [in] otherpad             - Pad for which transform caps needed.
 *  @param [in] caps                 - Caps for known pad.
 *  @return Found transform caps.
 *  @brief  This function finds out the fixated transform caps for the otherpad.
 *
 */
static GstCaps *
gst_vvas_xabrscaler_find_transform (GstVvasXAbrScaler * self, GstPad * pad,
    GstPad * otherpad, GstCaps * caps)
{
  GstPad *otherpeer;
  GstCaps *othercaps;
  gboolean is_fixed;
  GstCaps *peercaps;

  /* Received caps on the sink pad must be fixed, error our otherwise */
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);

  otherpeer = gst_pad_get_peer (otherpad);
  peercaps = gst_pad_query_caps (otherpeer, NULL);

  /* see how we can transform the input caps. We need to do this even for
   * passthrough because it might be possible that this element STAMP support
   * passthrough at all. */
  othercaps = gst_vvas_xabrscaler_transform_caps (self,
      GST_PAD_DIRECTION (pad), caps, peercaps, NULL);
  gst_caps_unref (peercaps);

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
      gst_vvas_xabrscaler_fixate_caps (self, GST_PAD_DIRECTION (pad), caps,
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

/**
 *  @fn static gboolean vvas_xabrscaler_decide_allocation (GstVvasXAbrScaler * self,
 *                                                         GstVvasXAbrScalerPad * srcpad,
 *                                                         GstQuery * query,
 *                                                         GstCaps * outcaps)
 *  @param [in] self        - Handle to GstVvasXAbrScaler typecasted to GObject.
 *  @param [in] srcpad      - Src pad GstVvasXAbrScalerPad object for which allocation has to be decided
 *  @param [in] query       - Response for the allocation query.
 *  @param [in] outcaps     - Currently set caps on the srcpad
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  This function will decide allocation strategy based on the preference from downstream element.
 *  @details The proposed allocation from downstream query response will be parsed through,
 *           verified if the proposed pool is VVAS and alignments are quoated. Otherwise it will be
 *           discarded and new pool and allocator will be created.
 */
static gboolean
vvas_xabrscaler_decide_allocation (GstVvasXAbrScaler * self,
    GstVvasXAbrScalerPad * srcpad, GstQuery * query, GstCaps * outcaps)
{
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  guint size, min, max;
  gboolean update_allocator, update_pool, bret, have_new_allocator = FALSE;
  GstStructure *config = NULL;
  GstVideoInfo out_vinfo;
  gint srcpadIdx = gst_vvas_xabrscaler_srcpad_get_index (self, srcpad);

  if (!outcaps) {
    GST_ERROR_OBJECT (srcpad, "out caps null...");
    goto error;
  }

  /* Get video info (GstVideoInfo) corresponding to the caps  */
  if (!gst_video_info_from_caps (&out_vinfo, outcaps)) {
    GST_ERROR_OBJECT (srcpad, "failed to get video info from outcaps");
    goto error;
  }

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0) {
    /* try the allocator */
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    GST_DEBUG_OBJECT (srcpad, "has allocator %p", allocator);
    update_allocator = TRUE;
  } else {
    /* This is when downstream has not proposed any allocators */
    allocator = NULL;
    update_allocator = FALSE;
    gst_allocation_params_init (&params);
  }

  /* Check if we have any pool proposed from downstream peer */
  if (gst_query_get_n_allocation_pools (query) > 0) {
    /* Get pool parameters at the index 0 */
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    size = MAX (size, out_vinfo.size);
    update_pool = TRUE;
    if (min == 0)
      min = 3;
  } else {
    /* Condition when the downstream element has not suggested any pool */
    pool = NULL;
    min = 3;
    max = 0;
    size = out_vinfo.size;
    update_pool = FALSE;
  }

  if (!self->software_scaling) {
    /* Check if the proposed pool stride is aligned with (8 * ppc)
     * If otherwise, discard the pool. Will create a new one */
    if (pool) {
      GstVideoAlignment video_align = { 0, };
      guint padded_width = 0;
      guint padded_height = 0;
      guint stride = 0, multiscaler_req_stride;

      /* Check if the proposed pool has alignment information */
      config = gst_buffer_pool_get_config (pool);
      if (config && gst_buffer_pool_config_has_option (config,
              GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
        gst_buffer_pool_config_get_video_alignment (config, &video_align);

        /* We have adding padding_right and padding_left in pixels.
         * We need to convert them to bytes for finding out the complete stride with alignment */
        padded_width =
            out_vinfo.width + video_align.padding_right +
            video_align.padding_left;
        padded_height =
            out_vinfo.height + video_align.padding_top +
            video_align.padding_bottom;

        stride = vvas_xabrscaler_get_stride (&out_vinfo, padded_width);

        GST_INFO_OBJECT (srcpad, "output stride = %u", stride);
        /* Stride can't be zero here */
        if (!stride)
          return FALSE;
        gst_structure_free (config);
        config = NULL;
        multiscaler_req_stride = WIDTH_ALIGN;

        /* Check if the stride is aligned to 8 ppc, as required by scaler, discard otherwise */
        if (stride % multiscaler_req_stride) {
          GST_WARNING_OBJECT (self, "Discarding the proposed pool, "
              "Alignment not matching with 8 * self->ppc");
          gst_object_unref (pool);
          pool = NULL;
          update_pool = FALSE;
          /* Store scaler alignment and elevation required for further reference */
          self->out_stride_align[srcpadIdx] = multiscaler_req_stride;
          self->out_elevation_align[srcpadIdx] = 1;
          GST_DEBUG_OBJECT (self, "Going to allocate pool with stride_align %d and \
              elevation_align %d", self->out_stride_align[srcpadIdx],
              self->out_elevation_align[srcpadIdx]);
        } else {
          /* We don't have the stride alignment from downstream pool, but the
           * stride derived is aligned to our requirement, lets take the stride
           * it self as the alignment.
           */
          self->out_stride_align[srcpadIdx] = stride;
          self->out_elevation_align[srcpadIdx] = padded_height;
        }
      } else {
        /* Proposed pool has no alignment information.
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
      GST_DEBUG_OBJECT (srcpad, "pool deos not have the KMSPrimeExport option, \
          unref the pool and create sdx allocator");
    }
#endif

    if (pool && !GST_IS_VVAS_BUFFER_POOL (pool)) {
      /* create own pool */
      gst_object_unref (pool);
      pool = NULL;
      update_pool = FALSE;
    }
    /* Check if the allocator is a VVAS allocator and should be on the same device  */
    if (allocator && (!GST_IS_VVAS_ALLOCATOR (allocator)
            || gst_vvas_allocator_get_device_idx (allocator) !=
            self->dev_index)) {
      GST_DEBUG_OBJECT (srcpad, "replace %" GST_PTR_FORMAT " to xrt allocator",
          allocator);
      gst_object_unref (allocator);
      gst_allocation_params_init (&params);
      allocator = NULL;
    }

    if (!allocator) {
      /* making sdx allocator for the HW mode without dmabuf */
      allocator = gst_vvas_allocator_new (self->dev_index,
          NEED_DMABUF, self->out_mem_bank);
      params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
      params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;
      GST_INFO_OBJECT (srcpad, "creating new xrt allocator %" GST_PTR_FORMAT
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
    if (!pool && (self->out_stride_align[srcpadIdx] == 1)
        && ((out_vinfo.stride[0] % WIDTH_ALIGN)
            || (out_vinfo.height % HEIGHT_ALIGN))) {
      self->out_stride_align[srcpadIdx] = WIDTH_ALIGN;
      self->out_elevation_align[srcpadIdx] = HEIGHT_ALIGN;
    }

    if (!pool) {
      GstVideoAlignment align;

      pool =
          gst_vvas_buffer_pool_new (self->out_stride_align[srcpadIdx],
          self->out_elevation_align[srcpadIdx]);
      GST_INFO_OBJECT (srcpad, "created new pool %p %" GST_PTR_FORMAT, pool,
          pool);
      /* Update the padding info based on scaler IP alignment requirement */
      config = gst_buffer_pool_get_config (pool);
      gst_video_alignment_reset (&align);
      align.padding_bottom =
          ALIGN (GST_VIDEO_INFO_HEIGHT (&out_vinfo),
          self->out_elevation_align[srcpadIdx]) -
          GST_VIDEO_INFO_HEIGHT (&out_vinfo);
      for (int idx = 0; idx < GST_VIDEO_INFO_N_PLANES (&out_vinfo); idx++) {
        align.stride_align[idx] = (self->out_stride_align[srcpadIdx] - 1);
      }
      /* Adjust output video info with respect to new alignment. Stride, offset etc will be
       * ajusted accordingly */
      gst_video_info_align (&out_vinfo, &align);
      /* size updated in vinfo based on alignment */
      size = out_vinfo.size;

      /* A bufferpool option to enable extra padding */
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
      /* Set "max" maxinum number of buffers and "min" minimum number of buffers */
      gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
      gst_buffer_pool_config_set_allocator (config, allocator, &params);
      /* An option that can be activated on bufferpool to request video metadata on buffers from the pool. */
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_META);
      /* Set the new alignment on pool config */
      gst_buffer_pool_config_set_video_alignment (config, &align);
      /* Update the pool config on the pool */
      bret = gst_buffer_pool_set_config (pool, config);
      if (!bret) {
        GST_ERROR_OBJECT (srcpad, "failed configure pool");
        goto error;
      }
    } else if (have_new_allocator) {
      /* update newly allocator on downstream pool */
      config = gst_buffer_pool_get_config (pool);

      GST_INFO_OBJECT (srcpad, "updating allocator %" GST_PTR_FORMAT
          " on pool %" GST_PTR_FORMAT, allocator, pool);

      gst_buffer_pool_config_set_allocator (config, allocator, &params);
      bret = gst_buffer_pool_set_config (pool, config);
      if (!bret) {
        GST_ERROR_OBJECT (srcpad, "failed configure pool");
        goto error;
      }
    }
  } else {
    /* If we have a pool, lets just configure it */
    if (pool) {
      GstVideoAlignment video_align = { 0, };
      guint padded_width = 0;
      guint padded_height = 0;
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
        padded_height =
            out_vinfo.height + video_align.padding_top +
            video_align.padding_bottom;
        gst_video_info_align (&out_vinfo, &video_align);
        stride = vvas_xabrscaler_get_stride (&out_vinfo, padded_width);

        GST_INFO_OBJECT (srcpad, "output stride = %u", stride);
        /* Stride can't be zero here */
        if (!stride) {
          gst_structure_free (config);
          config = NULL;
          gst_object_unref (pool);
          pool = NULL;
          return FALSE;
        }
        /* We don't have stride alignment here, but the stride calculated here
         * itself can be passed.
         */
        self->out_stride_align[srcpadIdx] = stride;
        self->out_elevation_align[srcpadIdx] = padded_height;
      } else {
        /* Looks like we don't have any alignment requirement from downstream */
        self->out_stride_align[srcpadIdx] = 1;
        self->out_elevation_align[srcpadIdx] = 1;
        self->avoid_output_copy = TRUE;
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
          GST_DEBUG_OBJECT (srcpad, "unsupported pool, making new pool");

          gst_object_unref (pool);
          pool = NULL;
        }
      }
    }

    /* Looks like we have to create our own pool */
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
        GST_ERROR_OBJECT (srcpad, "failed configure pool");
        goto error;
      }
      /* We are using a video buffer pool, no need of copy even if
       * downstream element is non video intelligent.
       */
      self->avoid_output_copy = TRUE;
      update_pool = FALSE;
      /* We have created a pool without any stride/elevation alignment */
      self->out_stride_align[srcpadIdx] = 1;
      self->out_elevation_align[srcpadIdx] = 1;
    }
  }

  /* avoid output frames copy when downstream supports video meta */
  if (!self->avoid_output_copy && !self->software_scaling) {
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

  /* Update pool parameters in the query */
  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  /* Store the finalized pool into pad structure */
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

/**
 *  @fn static gboolean gst_vvas_xabrscaler_sink_setcaps (GstVvasXAbrScaler * self,
 *                                                        GstPad * sinkpad,
 *                                                        GstCaps * in_caps)
 *  @param [in] self        - Handle to GstVvasXAbrScaler typecasted to GObject.
 *  @param [in] sinkpad      - Src pad GstVvasXAbrScalerPad object for which allocation has to be decided
 *  @param [in] in_caps       - Response for the allocation query.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief   Everytime a new caps are set on the sink pad, this function will be called.
 *  @details This function will find the find out the caps for every source pad as well and update
 *           corresponding video info.
 *
 */
static gboolean
gst_vvas_xabrscaler_sink_setcaps (GstVvasXAbrScaler * self, GstPad * sinkpad,
    GstCaps * in_caps)
{
  GstVvasXAbrScalerPrivate *priv = self->priv;
  GstCaps *outcaps = NULL, *prev_incaps = NULL, *prev_outcaps = NULL;
  gboolean bret = TRUE;
  guint idx = 0;
#ifdef ENABLE_XRM_SUPPORT
  gint load = -1;
#endif
  GstVvasXAbrScalerPad *srcpad = NULL;
  GstCaps *incaps = gst_caps_copy (in_caps);
  GstQuery *query = NULL;
  VvasReturnType vret;
  VvasScalerProp scaler_prop = { 0 };
  VvasLogLevel core_log_level =
      vvas_get_core_log_level (gst_debug_category_get_threshold
      (gst_vvas_xabrscaler_debug));

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

  if ((self->priv->in_vinfo->width % self->ppc)) {
    GST_ERROR_OBJECT (self, "Unsupported input resolution,"
        "width must be multiple of ppc i.e, %d", self->ppc);
    return FALSE;
  }

  if (self->crop_x || self->crop_y || self->crop_width || self->crop_height) {
    guint input_width, input_height, x_aligned, crop_width;

    input_width = self->priv->in_vinfo->width;
    input_height = self->priv->in_vinfo->height;

    GST_INFO_OBJECT (self, "crop in params: x:%u, y:%u, width:%u, height: %u",
        self->crop_x, self->crop_y, self->crop_width, self->crop_height);

    if ((self->crop_x >= input_width) || (self->crop_y >= input_height)) {
      GST_ERROR_OBJECT (self, "crop x or y coordinate can't be >= "
          " input width and height");
      return FALSE;
    }

    if (!self->crop_width) {
      self->crop_width = input_width - self->crop_x;
    }

    if (!self->crop_height) {
      self->crop_height = input_height - self->crop_y;
    }

    crop_width = self->crop_width;
    if (!self->software_scaling) {
      /* Align values as per the IP requirement
       * Align x by 8 * PPC, width by PPC, y and height by 2
       */
      x_aligned = (self->crop_x / (8 * self->ppc)) * (8 * self->ppc);
      self->crop_width = self->crop_x + self->crop_width - x_aligned;
      self->crop_width =
          xlnx_multiscaler_stride_align (self->crop_width, self->ppc);
    } else {
      /* In case of software scaling there is not alignment requirement */
      x_aligned = self->crop_x;
    }

    self->crop_x = x_aligned;
    self->crop_y = (self->crop_y / 2) * 2;
    self->crop_height = xlnx_multiscaler_stride_align (self->crop_height, 2);

    GST_INFO_OBJECT (self, "crop aligned params: x:%u, y:%u, width:%u, "
        "height: %u, extra width: %u",
        self->crop_x, self->crop_y, self->crop_width, self->crop_height,
        (self->crop_width - crop_width));

    if (((self->crop_x + self->crop_width) > input_width) ||
        ((self->crop_y + self->crop_height) > input_height)) {
      GST_ERROR_OBJECT (self, "x + width or y + height can't be greater "
          "than input width and height");
      return FALSE;
    }

    if ((self->crop_width < MIN_SCALAR_WIDTH) ||
        (self->crop_height < MIN_SCALAR_HEIGHT)) {
      GST_ERROR_OBJECT (self, "crop width/height must be at least %u",
          MIN_SCALAR_WIDTH);
      return FALSE;
    }
  }

  prev_incaps = gst_pad_get_current_caps (self->sinkpad);

  for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
    srcpad = gst_vvas_xabrscaler_srcpad_at_index (self, idx);

    /* find best possible caps for the other pad */
    outcaps = gst_vvas_xabrscaler_find_transform (self, sinkpad,
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
          vvas_xabrscaler_register_prep_write_with_caps (self, idx, incaps,
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
  bret = vvas_xabrscaler_calculate_load (self, &load);
  if (!bret)
    goto failed_configure;

  priv->cur_load = load;
  /* Allocate XRM resources */
  bret = vvas_xabrscaler_allocate_xrm_resource (self, priv->cur_load);
  if (!bret)
    goto failed_configure;
#endif
  GST_DEBUG_OBJECT (self, "Creating VVAS Scaler, kernel_name: %s",
      self->kern_name);


  priv->vvas_scaler =
      vvas_scaler_create (priv->vvas_ctx, self->kern_name, core_log_level);
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

  for (idx = 0; idx < g_list_length (self->srcpads); idx++) {

    srcpad = gst_vvas_xabrscaler_srcpad_at_index (self, idx);

    if (!srcpad->pool) {

      outcaps = gst_pad_get_current_caps ((GstPad *) srcpad);

      if (!outcaps)
        goto failed_configure;

      GST_DEBUG_OBJECT (self, "doing allocation query");
      query = gst_query_new_allocation (outcaps, TRUE);
      if (!gst_pad_peer_query (GST_PAD (srcpad), query)) {
        /* not a problem, just debug a little */
        GST_DEBUG_OBJECT (self, "peer ALLOCATION query failed");
      }

      bret = vvas_xabrscaler_decide_allocation (self, srcpad, query, outcaps);
      if (!bret)
        goto failed_configure;

      /* activate output buffer pool */
      if (!gst_buffer_pool_set_active (srcpad->pool, TRUE)) {
        GST_ERROR_OBJECT (srcpad, "failed to activate pool");
        goto failed_configure;
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

done:
  if (query)
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

/**
 *  @fn static gboolean gst_vvas_xabrscaler_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
 *  @param [in] pad       - The GstPad to handle the event.
 *  @param [in] parent    - The parent of the pad, which is the GstVvasXAbrScaler handle, typecasted to GstObject
 *  @param [in] event     - The GstEvent to handle.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  Handles GstEvent coming over the sink pad. Ex : EOS, New caps etc.
 *  @details This function will be set as the callback function for any new event coming on the sink pad using
 *           gst_pad_set_event_function.
 *
 */
static gboolean
gst_vvas_xabrscaler_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstVvasXAbrScaler *self = GST_VVAS_XABRSCALER (parent);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (pad, "received event '%s' %p %" GST_PTR_FORMAT,
      gst_event_type_get_name (GST_EVENT_TYPE (event)), event, event);

  switch (GST_EVENT_TYPE (event)) {
      /* Notify the pad of a new media type. */
    case GST_EVENT_CAPS:{
      GstCaps *caps;
      /* Parse the incoming media caps */
      gst_event_parse_caps (event, &caps);
      /* Upstream element has set the caps, now lets find out if these caps are fixed,
       * and also find transform caps for all the source pads */
      ret = gst_vvas_xabrscaler_sink_setcaps (self, self->sinkpad, caps);
      gst_event_unref (event);
      break;
    }
      /* End of Stream Event */
    case GST_EVENT_EOS:{
      if (self->enabled_pipeline) {
        GstFlowReturn fret = GST_FLOW_OK;
        GstBuffer *inbuf = NULL;

        GST_INFO_OBJECT (self, "input copy queue has %d pending buffers",
            g_async_queue_length (self->priv->copy_outqueue));
        /* Process remaining buffers in the queue before returning OK for EOS event */
        while (g_async_queue_length (self->priv->copy_outqueue) > 0) {
          inbuf = g_async_queue_pop (self->priv->copy_outqueue);

          fret = gst_vvas_xabrscaler_chain (pad, parent, inbuf);
          if (fret != GST_FLOW_OK)
            return FALSE;
        }
      }
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

/**
 *  @fn static gboolean vvas_xabrscaler_propose_allocation (GstVvasXAbrScaler * self, GstQuery * query)
 *  @param [in] self        - Handle to GstVvasXAbrScaler typecasted to GObject.
 *  @param [out] query      - Allocation Query to be answered.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  This function will propose allocation strategy to upstream element based on the scaler IP requirements.
 *
 */
static gboolean
vvas_xabrscaler_propose_allocation (GstVvasXAbrScaler * self, GstQuery * query)
{
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool;
  guint size;
  GstVideoAlignment align;

  /* Parse the incoming query and read the negotiated caps */
  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;
  /* Get video info from caps */
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
#ifdef ENABLE_XRM_SUPPORT
      if (!(self->priv->cu_resource || self->priv->cu_resource_v2)) {
        GST_ERROR_OBJECT (self, "scaler resource not yet allocated");
        return FALSE;
      }
#endif
      /* Create a new VVAS allocator with with memory bank info to which Scaler IP
       * input lines are connected */
      allocator = gst_vvas_allocator_new (self->dev_index,
          NEED_DMABUF, self->in_mem_bank);
      GST_INFO_OBJECT (self, "creating new xrt allocator %" GST_PTR_FORMAT
          "at mem bank %d", allocator, self->in_mem_bank);
      /* Add allocator object to query */
      gst_query_add_allocation_param (query, allocator, &params);
    }
    /* Create pool with aligned width and height */
    pool = gst_vvas_buffer_pool_new (WIDTH_ALIGN, HEIGHT_ALIGN);
    GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

    structure = gst_buffer_pool_get_config (pool);
    /* Get padding information based on the scaler IP alignment requirements
     * and fill in the details */
    gst_video_alignment_reset (&align);
    align.padding_bottom =
        ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
        HEIGHT_ALIGN) - GST_VIDEO_INFO_HEIGHT (&info);
    for (int idx = 0; idx < GST_VIDEO_INFO_N_PLANES (&info); idx++) {
      align.stride_align[idx] = (WIDTH_ALIGN - 1);
    }

    gst_video_info_align (&info, &align);

    /* Add the prepared alignment details to pool config */
    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (structure, &align);

    size = GST_VIDEO_INFO_SIZE (&info);
    /* Setting minimum of 2 buffers and maximux as 0 (unlimited) */
    gst_buffer_pool_config_set_params (structure, caps, size, 2, 0);

    gst_buffer_pool_config_set_allocator (structure, allocator, &params);
    /* Setting below option will allow requesting video metadata on buffers from the pool. */
    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    if (allocator)
      gst_object_unref (allocator);

    if (!gst_buffer_pool_set_config (pool, structure))
      goto config_failed;

    GST_OBJECT_LOCK (self);
    gst_query_add_allocation_pool (query, pool, size, 2, 0);
    GST_OBJECT_UNLOCK (self);
    /* Update the query with pool information,
     * setting minimum of 2 buffer and maximux set to 0, which means unlimited
     * each of size "size" */
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

/**
 *  @fn static gboolean gst_vvas_xabrscaler_sink_query (GstPad * pad, GstObject * parent,
 *                                               GstQuery * query)
 *  @param [in] pad          - Handle to sink GstPad.
 *  @param [in] parent       - Handle to GstVvasXAbrScaler object typecasted as GstObject
 *  @param [out] query       - Allocation Query to be answered/executed.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  This function will be invoked whenever an upstream element sends query on the sink pad.
 *  @details This function will be registered using function gst_pad_set_query_function as a query handler.
 *
 */
static gboolean
gst_vvas_xabrscaler_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstVvasXAbrScaler *self = GST_VVAS_XABRSCALER (parent);
  gboolean ret = TRUE;
  GstVvasXAbrScalerPad *srcpad = NULL;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:{
      /* Let's propose an allocation strategy to upstream element */
      if (!self->software_scaling) {
        ret = vvas_xabrscaler_propose_allocation (self, query);
      }
      break;
    }
    case GST_QUERY_CAPS:{
      GstCaps *filter, *caps = NULL, *result = NULL;
      /* Get the "filter" caps set from the upstream peer while
       * creating the query, caps that we anser to the query must be matching with this caps
       * for succesfull negotiation */
      gst_query_parse_caps (query, &filter);

      // TODO: Query caps only going to src pad 0 and check for others
      if (!g_list_length (self->srcpads)) {
        GST_DEBUG_OBJECT (pad, "0 source pads in list");
        return FALSE;
      }

      srcpad = gst_vvas_xabrscaler_srcpad_at_index (self, 0);
      if (!srcpad) {
        GST_ERROR_OBJECT (pad, "source pads not available..");
        return FALSE;
      }
      /* Get the template caps of the sink pad (sink_template) of this scaler element */
      caps = gst_pad_get_pad_template_caps (pad);

      /* Get the intersection of "filter" caps and sink pad template caps */
      if (filter) {
        GstCaps *tmp = caps;
        caps = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (tmp);
      }

      if (srcpad) {
        /* Query caps from the downstream element's sink pad */
        result = gst_pad_peer_query_caps (GST_PAD_CAST (srcpad), caps);
        result = gst_caps_make_writable (result);
        /* Append result with intersected caps */
        gst_caps_append (result, caps);

        GST_DEBUG_OBJECT (self, "Returning %s caps %" GST_PTR_FORMAT,
            GST_PAD_NAME (pad), result);
        /* Set the final caps to query response */
        gst_query_set_caps_result (query, result);
        gst_caps_unref (result);
      }
      break;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;

      gst_query_parse_accept_caps (query, &caps);

      ret = gst_caps_can_intersect (caps, gst_pad_get_pad_template_caps (pad));
      /* Set the intersection result on the qeury */
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

#ifdef ENABLE_PPE_SUPPORT
/**
 *  @fn static gboolean vvas_xabrscaler_is_pp_param_default (GstVvasXAbrScaler * self) 
 *  @param [in] self           - Handle to GstVvasXAbrScaler instance.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  This function will check if user has set any alpha and beta values for pre-processing.
 *
 */
static inline gboolean
vvas_xabrscaler_is_pp_param_default (GstVvasXAbrScaler * self)
{
  gboolean ret;
  ret = (PP_ALPHA_DEFAULT_VALUE == self->alpha_r) &&
      (PP_ALPHA_DEFAULT_VALUE == self->alpha_g) &&
      (PP_ALPHA_DEFAULT_VALUE == self->alpha_b) &&
      (PP_BETA_DEFAULT_VALUE == self->beta_r) &&
      (PP_BETA_DEFAULT_VALUE == self->beta_g) &&
      (PP_BETA_DEFAULT_VALUE == self->beta_b);
  return ret;
}

/**
 *  @fn static gboolean vvas_xabrscaler_get_dpu_kernel_config (GstVvasXAbrScaler * self) 
 *  @param [in] self           - Handle to GstVvasXAbrScaler instance.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  This function will query downstream for getting vvas kernel cofiguration for
 *          pre-processing (alpha and beta values) 
 */
static gboolean
vvas_xabrscaler_get_dpu_kernel_config (GstVvasXAbrScaler * self)
{
  /* Send a custom query downstream, if there is any downstream ML plug-in,
   * it will respond with pre-processing values
   */
  const GstStructure *st;
  GstStructure *strct;
  GstQuery *query;
  VvasStructure *kernel_config = NULL;
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (self, "Doing vvas-kernel-config query");

  strct = gst_structure_new_empty ("vvas-kernel-config");
  if (!strct) {
    return FALSE;
  }

  /* Create a custom query for getting kernel config from down stream */
  query = gst_query_new_custom (GST_QUERY_CUSTOM, strct);
  if (!query) {
    GST_ERROR_OBJECT (self, "couldn't create custom query");
    gst_structure_free (strct);
    return FALSE;
  }

  if (!gst_pad_query (GST_PAD_CAST (self->sinkpad), query)) {
    GST_WARNING_OBJECT (self, "gst_pad_query failed");
    goto error;
  }

  st = gst_query_get_structure (query);
  if (!st) {
    goto error;
  }
  /* Parse the response */
  if (!gst_structure_get (st,
          "pp_config", G_TYPE_POINTER, &kernel_config, NULL)) {
    GST_DEBUG_OBJECT (self, "couldn't get kernel config");
    ret = TRUE;
    goto error;
  }

  if (kernel_config) {
    gfloat alpha_r, alpha_g, alpha_b;
    gfloat beta_r, beta_g, beta_b;
    gboolean res;
    /* Get all alpha and beta values from the response structure */
    res = vvas_structure_get (kernel_config,
        "alpha_r", G_TYPE_FLOAT, &alpha_r,
        "alpha_g", G_TYPE_FLOAT, &alpha_g,
        "alpha_b", G_TYPE_FLOAT, &alpha_b,
        "beta_r", G_TYPE_FLOAT, &beta_r,
        "beta_g", G_TYPE_FLOAT, &beta_g, "beta_b", G_TYPE_FLOAT, &beta_b, NULL);

    if (res) {
      GST_DEBUG_OBJECT (self,
          "Got PP config: alpha[%f, %f, %f], beta[%f, %f, %f]", alpha_r,
          alpha_g, alpha_b, beta_r, beta_g, beta_b);
      /* Check if User has set any values for alpha and beta pre-processing */
      if (vvas_xabrscaler_is_pp_param_default (self)) {
        /* User has not set the PP configuration, using DPU suggested values */
        self->alpha_r = alpha_r;
        self->alpha_g = alpha_g;
        self->alpha_b = alpha_b;
        self->beta_r = beta_r;
        self->beta_g = beta_g;
        self->beta_b = beta_b;
        if (!vvas_xabrscaler_is_pp_param_default (self)) {
          GST_DEBUG_OBJECT (self, "User hasn't set PP configuration, but "
              "DPU has suggested them, using DPU suggested values for PP");
        }
      } else {
        /* Check if the user set alpha and beta values are in line with what 
         * downstream kernel configuration suggest */
        if (self->alpha_r != alpha_r) {
          GST_WARNING_OBJECT (self, "alpha_r: %f is not as per "
              "DPU suggestion: %f", self->alpha_r, alpha_r);
        }
        if (self->alpha_g != alpha_g) {
          GST_WARNING_OBJECT (self, "alpha_g: %f is not as per "
              "DPU suggestion: %f", self->alpha_g, alpha_g);
        }
        if (self->alpha_b != alpha_b) {
          GST_WARNING_OBJECT (self, "alpha_b: %f is not as per "
              "DPU suggestion: %f", self->alpha_b, alpha_b);
        }
        if (self->beta_r != beta_r) {
          GST_WARNING_OBJECT (self, "beta_r: %f is not as per "
              "DPU suggestion: %f", self->beta_r, beta_r);
        }
        if (self->beta_g != beta_g) {
          GST_WARNING_OBJECT (self, "beta_g: %f is not as per "
              "DPU suggestion: %f", self->beta_g, beta_g);
        }
        if (self->beta_b != beta_b) {
          GST_WARNING_OBJECT (self, "beta_b: %f is not as per "
              "DPU suggestion: %f", self->beta_b, beta_b);
        }
      }
    }
  } else {
    GST_DEBUG_OBJECT (self, "No kernel configuration from downstream");
  }

  ret = TRUE;
error:
  gst_query_unref (query);
  return ret;
}
#endif

/**
 *  @fn static gboolean vvas_xabrscaler_add_scaler_processing_chnnels (GstVvasXAbrScaler * self, GstBuffer * inbuf)
 *  @param [in] self    - Handle to GstVvasXAbrScaler instance.
 *  @param [in] inbuf   - Input buffer
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  This function will add the channels into the VVAS CORE Scaler library for processing.
 */
static gboolean
vvas_xabrscaler_add_scaler_processing_chnnels (GstVvasXAbrScaler * self,
    GstBuffer * inbuf)
{
  GstVvasXAbrScalerPrivate *priv = self->priv;
  guint idx;
  VvasVideoFrame *input_frame, *output_frame;
  GstBuffer *input_buffer, *output_buffer;
  guint input_memory_bank = self->in_mem_bank;

#ifdef ENABLE_PPE_SUPPORT
  VvasScalerPpe ppe = { 0 };
  ppe.mean_r = self->alpha_r;
  ppe.mean_g = self->alpha_g;
  ppe.mean_b = self->alpha_b;
  ppe.scale_r = self->beta_r;
  ppe.scale_g = self->beta_g;
  ppe.scale_b = self->beta_b;
#endif
  input_buffer = inbuf;

  /* Add channels for all the request pads */
  for (idx = 0; idx < self->num_request_pads; idx++) {
    VvasScalerRect src_rect = { 0 };
    VvasScalerRect dst_rect = { 0 };
    VvasReturnType vret;

    GstVvasXAbrScalerPad *srcpad =
        gst_vvas_xabrscaler_srcpad_at_index (self, idx);
    if (!srcpad) {
      return FALSE;
    }

    output_buffer = priv->outbufs[idx];

    /* Convert GstBuffer to VvasVideoFrame required from Vvas Core Scaler */
    input_frame = vvas_videoframe_from_gstbuffer (self->priv->vvas_ctx,
        input_memory_bank, input_buffer, srcpad->in_vinfo, GST_MAP_READ);
    if (!input_frame) {
      GST_ERROR_OBJECT (self,
          "Could convert input GstBuffer to VvasVideoFrame");
      return FALSE;
    }

    priv->input_frame[idx] = input_frame;

    /* Convert GstBuffer to VvasVideoFrame required from Vvas Core Scaler */
    if (!self->software_scaling) {
      output_frame = vvas_videoframe_from_gstbuffer (self->priv->vvas_ctx,
          self->out_mem_bank, output_buffer, srcpad->out_vinfo, GST_MAP_READ);
    } else {
      output_frame = vvas_videoframe_from_gstbuffer (self->priv->vvas_ctx,
          self->out_mem_bank, output_buffer, srcpad->out_vinfo,
          (GST_MAP_READ | GST_MAP_WRITE));
    }
    if (!output_frame) {
      GST_ERROR_OBJECT (self,
          "Could convert output GstBuffer to VvasVideoFrame");
      return FALSE;
    }

    priv->output_frame[idx] = output_frame;

    /* Fill rect parameters */
    if ((0 == idx) && (self->crop_width && self->crop_height)) {
      src_rect.x = self->crop_x;
      src_rect.y = self->crop_y;
      src_rect.width = self->crop_width;
      src_rect.height = self->crop_height;
    } else {
      src_rect.x = 0;
      src_rect.y = 0;
      src_rect.width = GST_VIDEO_INFO_WIDTH (srcpad->in_vinfo);
      src_rect.height = GST_VIDEO_INFO_HEIGHT (srcpad->in_vinfo);
    }
    src_rect.frame = input_frame;

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = GST_VIDEO_INFO_WIDTH (srcpad->out_vinfo);
    dst_rect.height = GST_VIDEO_INFO_HEIGHT (srcpad->out_vinfo);
    dst_rect.frame = output_frame;

    /* Add processing channel into Core Scaler */
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

    /* Chain this output buffer as input buffer for next ladder */
    input_buffer = output_buffer;
    input_memory_bank = self->out_mem_bank;
  }
  return TRUE;
}

/**
 *  @fn static void gst_vvas_xabrscaler_free_vvas_video_frame (GstVvasXAbrScaler * self)
 *  @param [in] self           - Handle to GstVvasXAbrScaler instance.
 *  @return None
 *  @brief  This function will free all the VvasVideoFrames.
 */
static inline void
gst_vvas_xabrscaler_free_vvas_video_frame (GstVvasXAbrScaler * self)
{
  guint chan_id = 0;

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    if (self->priv->input_frame[chan_id]) {
      vvas_video_frame_free (self->priv->input_frame[chan_id]);
      self->priv->input_frame[chan_id] = NULL;
    }

    if (self->priv->output_frame[chan_id]) {
      vvas_video_frame_free (self->priv->output_frame[chan_id]);
      self->priv->output_frame[chan_id] = NULL;
    }
  }
}



/**
 *  @fn static gboolean remove_infer_meta (GstBuffer * buffer, GstMeta ** meta, gpointer user_data) 
 *  @param [in] buffer       - Buffer handle to which meta has to be removed
 *  @param [in] meta         - meta to be removed
 *  @param [in] user_data    - user data to be passed
 *  @return On Success returns TRUE, indicates meta data has been removed.
 *  @brief  This is a callback function registered for removing meta data.
 *  @details This function will be triggered via gst_buffer_foreach_meta.
 *
 */
static gboolean
remove_infer_meta (GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
  if (meta && *meta && (*meta)->info->api == GST_INFERENCE_META_API_TYPE) {
    *meta = NULL;
  }

  return TRUE;
}

/**
 *  @fn static GstFlowReturn gst_vvas_xabrscaler_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
 *  @param [in] pad          - Handle to sink GstPad.
 *  @param [in] parent       - Handle to GstVvasXAbrScaler object typecasted as GstObject
 *  @param [in] inbuf        - Incoming buffer to be processed.
 *  @return On Success returns GST_FLOW_OK\n On Failure returns Corresponding error code.
 *  @brief  This function will be invoked whenever an upstream element pushes buffers on the sinkpad.
 *  @details This function will be registered using function gst_pad_set_chain_function.
 *
 */
static GstFlowReturn
gst_vvas_xabrscaler_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
{
  GstVvasXAbrScaler *self = GST_VVAS_XABRSCALER (parent);
  GstFlowReturn fret = GST_FLOW_OK;
  guint chan_id = 0;
  gboolean bret = FALSE;
  VvasReturnType vret;

#ifdef ENABLE_PPE_SUPPORT
  if (G_UNLIKELY (self->get_pp_config)) {
    vvas_xabrscaler_get_dpu_kernel_config (self);
    self->get_pp_config = FALSE;
  }
#endif

  /* Lets go with incoming buffer itself, as we don't have any stride alignment
   * requirement for software scaling.
   */
  if (!self->software_scaling) {
    /* Prepare the internal buffer pool if the inbuf is neither
     * VVAS allocator buffer nor DMA buffer and copy into it */
    bret = vvas_xabrscaler_prepare_input_buffer (self, &inbuf);
    if (!bret)
      goto error;
  }
  /* Nothing to process */
  if (!inbuf)
    return GST_FLOW_OK;
  /* Prepare an output buffer from output pool to be used for operation */
  bret = vvas_xabrscaler_prepare_output_buffer (self);
  if (!bret) {
    fret = GST_FLOW_ERROR;
    goto error;
  }

  /*
   * Convert GstBuffer to VvasVideoFrame, fill VvasRect info and fed it to
   * vvas_scaler_channel_add().
   * Once all channels are fed call vvas_scaler_process_frame() for
   * processing.
   */
  if (!vvas_xabrscaler_add_scaler_processing_chnnels (self, inbuf)) {
    GST_ERROR_OBJECT (self, "Failed to add processing channels on scaler");
    fret = GST_FLOW_ERROR;
    goto error;
  }

  vret = vvas_scaler_process_frame (self->priv->vvas_scaler);
  if (VVAS_IS_ERROR (vret)) {
    GST_ERROR_OBJECT (self, "Failed to process frame in scaler");
    fret = GST_FLOW_ERROR;
    goto error;
  }

  /* Release input and output VVAS Video frames */
  gst_vvas_xabrscaler_free_vvas_video_frame (self);

  if (self->priv->vvas_ctx->dev_handle) {
    bret = vvas_xabrscaler_sync_buffer (self);
    if (!bret)
      goto error;
  }

  /* pad push of each output buffer to respective srcpad */
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    GstBuffer *outbuf = self->priv->outbufs[chan_id];
    GstVvasXAbrScalerPad *srcpad =
        gst_vvas_xabrscaler_srcpad_at_index (self, chan_id);
    GstMeta *in_meta;

    gst_buffer_copy_into (outbuf, inbuf,
        (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_META |
            GST_BUFFER_COPY_TIMESTAMPS), 0, -1);

    /* we are removing infer meta data from outbuf because while performing
     * transform operation, both inbuf & outbuf meta data are same
     * and due to that "need_scale" flag is not set and scaling is not happening
     */
    gst_buffer_foreach_meta (outbuf, remove_infer_meta, NULL);

    /* Scaling of input vvas metadata based on output resolution */
    in_meta = gst_buffer_get_meta (inbuf, gst_inference_meta_api_get_type ());

    if (in_meta) {
      GstVideoMetaTransform trans = { self->priv->in_vinfo, srcpad->out_vinfo };
      GQuark scale_quark = gst_video_meta_transform_scale_get_quark ();

      GST_DEBUG_OBJECT (srcpad, "attaching scaled inference metadata");
      in_meta->info->transform_func (outbuf, (GstMeta *) in_meta,
          inbuf, scale_quark, &trans);
    }

    in_meta = gst_buffer_get_meta (inbuf, GST_VVAS_OVERLAY_META_API_TYPE);
    if (in_meta) {
      GstVideoMetaTransform trans = { self->priv->in_vinfo, srcpad->out_vinfo };
      GQuark scale_quark = gst_video_meta_transform_scale_get_quark ();

      GST_DEBUG_OBJECT (srcpad, "scaling overlay meta");
      in_meta->info->transform_func (outbuf, (GstMeta *) in_meta,
          inbuf, scale_quark, &trans);
    }

    /* Copying of input HDR metadata */
    in_meta = (GstMeta *) gst_buffer_get_vvas_hdr_meta (inbuf);
    if (in_meta) {
      GstMetaTransformCopy copy_data = { FALSE, 0, -1 };

      GST_DEBUG_OBJECT (srcpad, "copying input HDR metadata");
      in_meta->info->transform_func (outbuf, (GstMeta *) in_meta,
          inbuf, _gst_meta_transform_copy, &copy_data);
    }

    /* If the "need_copy" flag is enabled for the channel, create new host
     * buffer and copy the content and meta data into it from device buffer
     * and push out the host buffer */
    if (self->priv->need_copy[chan_id]) {
      GstBuffer *new_outbuf;
      GstVideoFrame new_frame, out_frame;
      new_outbuf =
          gst_buffer_new_and_alloc (GST_VIDEO_INFO_SIZE (srcpad->out_vinfo));
      if (!new_outbuf) {
        GST_ERROR_OBJECT (srcpad, "failed to allocate output buffer");
        fret = GST_FLOW_ERROR;
        goto error2;
      }

      /* Map the new buffers in write mode and output buffer (out_frame) in read mode
       * for enabling copy */
      gst_video_frame_map (&out_frame, srcpad->out_vinfo, outbuf, GST_MAP_READ);
      gst_video_frame_map (&new_frame, srcpad->out_vinfo, new_outbuf,
          GST_MAP_WRITE);
      GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, srcpad,
          "slow copy data from %p to %p", outbuf, new_outbuf);
      gst_video_frame_copy (&new_frame, &out_frame);
      gst_video_frame_unmap (&out_frame);
      gst_video_frame_unmap (&new_frame);

      /* Copy all the meta data as well */
      gst_buffer_copy_into (new_outbuf, outbuf, GST_BUFFER_COPY_METADATA, 0,
          -1);
      gst_buffer_unref (outbuf);
      GST_LOG_OBJECT (srcpad,
          "pushing outbuf %p with pts = %" GST_TIME_FORMAT " dts = %"
          GST_TIME_FORMAT " duration = %" GST_TIME_FORMAT, new_outbuf,
          GST_TIME_ARGS (GST_BUFFER_PTS (new_outbuf)),
          GST_TIME_ARGS (GST_BUFFER_DTS (new_outbuf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (new_outbuf)));

      /* Push out the newly created and copied buffer to source pad */
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

      /* Push out the output buffer onto source pad */
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
  gst_vvas_xabrscaler_free_vvas_video_frame (self);
  return fret;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
vvas_xabrscaler_init (GstPlugin * vvas_xabrscaler)
{
  /* Register the scaler element with name "vvas_xabrscaler" */
  return gst_element_register (vvas_xabrscaler, "vvas_xabrscaler",
      GST_RANK_PRIMARY, GST_TYPE_VVAS_XABRSCALER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xabrscaler,
    "Xilinx Multiscaler 2.0 plugin", vvas_xabrscaler_init, VVAS_API_VERSION,
    "LGPL", "GStreamer xrt", "http://xilinx.com/")
