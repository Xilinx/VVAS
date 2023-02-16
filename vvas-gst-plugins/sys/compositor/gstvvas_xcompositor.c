/*
 * Copyright 2020 - 2022 Xilinx, Inc.
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
#include <gst/vvas/gstvvasallocator.h>
#include <gst/vvas/gstvvasbufferpool.h>
#include <vvas_core/vvas_device.h>
#include <inttypes.h>
#include <stdint.h>
#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include "gstvvas_xcompositor.h"
#include <math.h>

#include <gst/vvas/gstvvascoreutils.h>
#include <vvas_core/vvas_context.h>
#include <vvas_core/vvas_common.h>
#include <vvas_core/vvas_scaler.h>

#ifdef ENABLE_XRM_SUPPORT
#include <xrm.h>
#include <xrm_limits.h>
#include <jansson.h>
#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))
#endif

#ifdef XLNX_PCIe_PLATFORM

/** @def DEFAULT_DEVICE_INDEX
 *  @brief Setting default device index as -1 in PCIe Platform.
 */
#define DEFAULT_DEVICE_INDEX -1

/** @def DEFAULT_KERNEL_NAME
 *  @brief Setting default kernel name as image_processing:{image_processing_1} in PCIe Platform.
 */
#define DEFAULT_KERNEL_NAME "image_processing:{image_processing_1}"

/** @def NEED_DMABUF
 *  @brief Disabling DMA buffers in PCIe Platform.
 */
#define NEED_DMABUF 0
#else

/** @def DEFAULT_DEVICE_INDEX
 *  @brief Setting default device index as 0 in EDGE Platform.
 */
#define DEFAULT_DEVICE_INDEX 0

/** @def DEFAULT_KERNEL_NAME
 *  @brief Setting default kernel name as image_processing:{image_processing_1} in EDGE Platform.
 */
#define DEFAULT_KERNEL_NAME "image_processing:{image_processing_1}"

/** @def NEED_DMABUF
 *  @brief Enabling DMA buffers in EDGE Platform.
 */
#define NEED_DMABUF 1
#endif

#ifndef DEFAULT_MEM_BANK
/** @def DEFAULT_MEM_BANK
 *  @brief Default Memory bank
 */
#define DEFAULT_MEM_BANK 0
#endif

/** @def VVAS_XCOMPOSITOR_BEST_FIT_DEFAULT
 *  @brief Disabling best fit by default.
 */
#define VVAS_XCOMPOSITOR_BEST_FIT_DEFAULT FALSE

/** @def VVAS_XCOMPOSITOR_AVOID_OUTPUT_COPY_DEFAULT
 *  @brief Setting avoid output copy to false by default.
 */
#define VVAS_XCOMPOSITOR_AVOID_OUTPUT_COPY_DEFAULT FALSE

/** @def VVAS_XCOMPOSITOR_ENABLE_PIPELINE_DEFAULT
 *  @brief Setting the enable pipeline off by default.
 */
#define VVAS_XCOMPOSITOR_ENABLE_PIPELINE_DEFAULT FALSE

/** @def STOP_COMMAND
 *  @brief Macro to replace the STOP quark
 */
#define STOP_COMMAND ((gpointer)GINT_TO_POINTER (g_quark_from_string("STOP")))

/** @def WIDTH_ALIGN
 *  @brief Alignment for width must be 8 * pixel per clock.
 */
#define WIDTH_ALIGN (8 * self->ppc)

/** @def HEIGHT_ALIGN
 *  @brief Alignment for height.
 */
#define HEIGHT_ALIGN 1

#define MEM_BANK 0

/** @def DEFAULT_PAD_XPOS
 *  @brief Setting default x position to 0.
 */
#define DEFAULT_PAD_XPOS   0

/** @def DEFAULT_PAD_YPOS
 *  @brief Setting default y position to 0.
 */
#define DEFAULT_PAD_YPOS   0

/** @def DEFAULT_PAD_WIDTH
 *  @brief Setting default width to -1.
 */
#define DEFAULT_PAD_WIDTH  -1

/** @def DEFAULT_PAD_HEIGHT
 *  @brief Setting default height to -1.
 */
#define DEFAULT_PAD_HEIGHT -1

/** @def DEFAULT_PAD_ZORDER
 *  @brief Setting default zorder to -1.
 */
#define DEFAULT_PAD_ZORDER -1

/** @def DEFAULT_PAD_EOS
 *  @brief Macro to disable the is_eos to false at initialization.
 */
#define DEFAULT_PAD_EOS false

/** @def DEFAULT_VVAS_DEBUG_LEVEL
 *  @brief Default debug level for VVAS Core
 */
#define DEFAULT_VVAS_DEBUG_LEVEL        2

/** @def DEFAULT_VVAS_SCALER_DEBUG_LEVEL
 *  @brief Default debug level for VVAS Core Scaler
 */
#define DEFAULT_VVAS_SCALER_DEBUG_LEVEL 2

/** @def GST_TYPE_VVAS_XCOMPOSITOR_PAD
 *  @brief Macro to get GstVvasXCompositorPad object type
 */
#define GST_TYPE_VVAS_XCOMPOSITOR_PAD \
    (gst_vvas_xcompositor_pad_get_type())

/** @def GST_VVAS_XCOMPOSITOR_PAD
 *  @brief Macro to typecast parent object to GstVvasXCompositorPad object
 */
#define GST_VVAS_XCOMPOSITOR_PAD(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XCOMPOSITOR_PAD, \
				GstVvasXCompositorPad))

/** @def GST_VVAS_XCOMPOSITOR_PAD_CLASS
 *  @brief Macro to typecast parent class object to GstVvasXCompositorPadClass object
 */
#define GST_VVAS_XCOMPOSITOR_PAD_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XCOMPOSITOR_PAD,\
			     GstVvasXCompositorPadClass))

/** @def GST_IS_VVAS_XCOMPOSITOR_PAD
 *  @brief Macro to validate whether object is of GstVvasXCompositorPad type
 */
#define GST_IS_VVAS_XCOMPOSITOR_PAD(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XCOMPOSITOR_PAD))

/** @def GST_IS_VVAS_XCOMPOSITOR_PAD_CLASS
 *  @brief Macro to validate whether object class  is of GstVvasXCompositorPadClass type
 */
#define GST_IS_VVAS_XCOMPOSITOR_PAD_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XCOMPOSITOR_PAD))

/** @def GST_VVAS_XCOMPOSITOR_PAD_CAST
 *  @brief Macro to typecast parent object to GstVvasXCompositorPad object
 */
#define GST_VVAS_XCOMPOSITOR_PAD_CAST(obj) \
    ((GstVvasXCompositorPad *)(obj))

#ifdef XLNX_PCIe_PLATFORM

/** @def VVAS_XCOMPOSITOR_DEFAULT_NUM_TAPS
 *  @brief Setting number of taps value to 12 in PCIe Platform.
 */
#define VVAS_XCOMPOSITOR_DEFAULT_NUM_TAPS 12

/** @def VVAS_XCOMPOSITOR_DEFAULT_PPC
 *  @brief Setting pixels per clock value to 4 in PCIe Platform.
 */
#define VVAS_XCOMPOSITOR_DEFAULT_PPC 4

/** @def VVAS_XCOMPOSITOR_SCALE_MODE
 *  @brief Setting sacle mode value to 2 (Polyphase) in PCIe Platform.
 */
#define VVAS_XCOMPOSITOR_SCALE_MODE 2
#else

/** @def VVAS_XCOMPOSITOR_DEFAULT_NUM_TAPS
 *  @brief Setting number of taps value to 6 in EDGE Platform.
 */
#define VVAS_XCOMPOSITOR_DEFAULT_NUM_TAPS 6

/** @def VVAS_XCOMPOSITOR_DEFAULT_PPC
 *  @brief Setting pixels per clock value to 2 in EDGE Platform.
 */
#define VVAS_XCOMPOSITOR_DEFAULT_PPC 2

/** @def VVAS_XCOMPOSITOR_SCALE_MODE
 *  @brief Setting sacle mode value to 0 (Bilinear) in EDGE Platform.
 */
#define VVAS_XCOMPOSITOR_SCALE_MODE 0
#endif

/** @def VVAS_XCOMPOSITOR_NUM_TAPS_TYPE
 *  @brief Macro to get GstVvasXCompositorNumTaps object type
 */
#define VVAS_XCOMPOSITOR_NUM_TAPS_TYPE (vvas_xcompositor_num_taps_type ())

/** @def VVAS_XCOMPOSITOR_PPC_TYPE
 *  @brief Macro to get GstVvasXCompositorPPC object type
 */
#define VVAS_XCOMPOSITOR_PPC_TYPE (vvas_xcompositor_ppc_type ())
#ifdef XLNX_PCIe_PLATFORM

/** @def VVAS_XCOMPOSITOR_DEFAULT_COEF_LOAD_TYPE
 *  @brief Setting default coefficient load type to Auto Generate in PCIe platform.
 */
#define VVAS_XCOMPOSITOR_DEFAULT_COEF_LOAD_TYPE COEF_AUTO_GENERATE
#else

/** @def VVAS_XCOMPOSITOR_DEFAULT_COEF_LOAD_TYPE
 *  @brief Setting default coefficient load type to Fixed in EDGE platform.
 */
#define VVAS_XCOMPOSITOR_DEFAULT_COEF_LOAD_TYPE COEF_FIXED
#endif

/** @def VVAS_XCOMPOSITOR_COEF_LOAD_TYPE
 *  @brief Macro to get GstVvasXCompositorCoefLoad object type
 */
#define VVAS_XCOMPOSITOR_COEF_LOAD_TYPE (vvas_xcompositor_coef_load_type ())

/** @def gst_vvas_xcompositor_sinkpad_at_index(self, idx)
 *  @brief Macro to get the sinkpad at the provided index
 */
#define gst_vvas_xcompositor_sinkpad_at_index(self, idx) \
	((GstVvasXCompositorPad *)(g_list_nth ((self)->sinkpads, idx))->data)

/** @def gst_vvas_xcompositor_parent_class
 *  @brief Macro to declare a static variable named parent_class pointing to the parent class
 */
#define gst_vvas_xcompositor_parent_class parent_class

/** @def GST_CAT_DEFAULT
 *  @brief Defines a static GstDebugCategory global variable "gst_vvas_xcompositor_debug"
 */
#define GST_CAT_DEFAULT gst_vvas_xcompositor_debug

typedef struct _GstVvasXCompositorPad GstVvasXCompositorPad;
typedef struct _GstVvasXCompositorPadClass GstVvasXCompositorPadClass;

/** @enum VvasXCompositorProperties
 *  @brief  Contains properties related to VVAS Compositor
 */
enum
{
  /** Default */
  PROP_0,
  /** Property to set xclbin path */
  PROP_XCLBIN_LOCATION,
  /** Property to set kernel name */
  PROP_KERN_NAME,
#ifdef XLNX_PCIe_PLATFORM
  /** Property to set device index */
  PROP_DEVICE_INDEX,
#endif
  /** Property to set input memory bank */
  PROP_IN_MEM_BANK,
  /** Property to set output memory bank */
  PROP_OUT_MEM_BANK,
  /** Property to set pixels per clock */
  PROP_PPC,
  /** Property to set scale mode */
  PROP_SCALE_MODE,
  /** Property to set number of taps */
  PROP_NUM_TAPS,
  /** Property to set coefficient loading type */
  PROP_COEF_LOADING_TYPE,
  /** Property to set best fit */
  PROP_BEST_FIT,
  /** Property to set avoid output copy */
  PROP_AVOID_OUTPUT_COPY,
  /** Property to set enable pipeline */
  PROP_ENABLE_PIPELINE,
#ifdef ENABLE_XRM_SUPPORT
  /** Property to set xrm reservation id */
  PROP_RESERVATION_ID,
#endif
};

/** @enum VvasXCompositorPadProperties
 *  @brief  Contains properties related to VVAS Compositor Pad
 */
enum
{
  /** Default */
  PROP_PAD_0,
  /** Pad property to set x position */
  PROP_PAD_XPOS,
  /** Pad property to set y position */
  PROP_PAD_YPOS,
  /** Pad property to set width */
  PROP_PAD_WIDTH,
  /** Pad property to set height */
  PROP_PAD_HEIGHT,
  /** Pad property to set zorder */
  PROP_PAD_ZORDER
};

/** @struct _GstVvasXCompositorPad
 *  @brief  The opaque GstVavsXCompositorPad structure.
 */
struct _GstVvasXCompositorPad
{
  /** Parent Pad */
  GstVideoAggregatorPad compositor_pad;
  /** Points to current frame's x position on output frame */
  guint xpos;
  /** Points to current frame's y position on output frame */
  guint ypos;
  /** Points to current frame's width on output frame */
  gint width;
  /** Points to current frame's height on output frame */
  gint height;
  /** Points to current pad's index */
  guint index;
  /** Points to current frame's zorder on output frame */
  gint zorder;
  /** Flag indicating End Of Stream */
  gboolean is_eos;
  /** Pointer holding current pad's pool */
  GstBufferPool *pool;
  /** Pointer holding current pad's video info */
  GstVideoInfo *in_vinfo;
  /** Whether input buffer needs to be imported or not */
  gboolean validate_import;
};

/** @struct _GstVvasXCompositorPadClass
 *  @brief  GstVvasXCompositorPadClass subclass for pads managed by GstVideoAggregatorPadClass
 */
struct _GstVvasXCompositorPadClass
{
  /** subclass */
  GstVideoAggregatorPadClass compositor_pad_class;
};

/** @struct _GstVvasXCompositorPrivate
 *  @brief  Holds private members related VVAS Compositor instance
 */
struct _GstVvasXCompositorPrivate
{
  /** TRUE if internal buffers are allocated */
  gboolean is_internal_buf_allocated;
  /** Video Information of the incoming streams */
  GstVideoInfo *in_vinfo[MAX_CHANNELS];
  /** Video Information of the outgoing streams */
  GstVideoInfo *out_vinfo;
  /** Handle to FPGA device */
  vvasDeviceHandle dev_handle;
  /** xclbin Id */
  uuid_t xclbinId;
  /** Output Buffer to store the aggregated frame */
  GstBuffer *outbuf;
  /** To store input buffers from incoming streams */
  GstBuffer *inbufs[MAX_CHANNELS];
  /** Pad index of the Z-order */
  gint pad_of_zorder[MAX_CHANNELS];
  /** Pool of output buffers */
  GstBufferPool *output_pool;
  /** Flag to copy the buffer to downstream element */
  gboolean need_copy;
  /** Thread to copy the input to speed up the pipeline */
  GThread *input_copy_thread;
  /** Array of input queues */
  GAsyncQueue *copy_inqueue[MAX_CHANNELS];
  /** Array of output queues */
  GAsyncQueue *copy_outqueue[MAX_CHANNELS];
  /** Flag indicating first frame or not */
  gboolean is_first_frame[MAX_CHANNELS];
  /** VVAS Core Context */
  VvasContext *vvas_ctx;
  /** VVAS Core Scaler Context */
  VvasScaler *vvas_scaler;
  /** Reference of input VvasVideoFrames */
  VvasVideoFrame *input_frames[MAX_CHANNELS];
  /** Reference of output VvasVideoFrame */
  VvasVideoFrame *output_frame;

#ifdef ENABLE_XRM_SUPPORT
  /** XRM Context */
  xrmContext xrm_ctx;
  /** Pointer to structure holding compute resource */
  xrmCuResource *cu_resource;
  /** Pointer to structure (version 2) holding compute resource */
  xrmCuResourceV2 *cu_resource_v2;
  /** Current load for the kernel */
  gint cur_load;
  /** XRM reservation id */
  guint64 reservation_id;
  /** Flag indicating whether xrm has error or not */
  gboolean has_error;
#endif
};

/**
 *  @brief Defines plugin's source pad template.
 */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGBx, YUY2, r210, Y410, NV16, NV12, RGB, v308, I422_10LE, GRAY8, \
	NV12_10LE32, BGRx, GRAY10_LE32, BGRx, UYVY, BGR, RGBA, BGRA, I420, GBR}")));

/**
 *  @brief Defines plugin's sink pad template.
 */
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
static gboolean vvas_xcompositor_open (GstVvasXCompositor * self);
static gpointer vvas_xcompositor_input_copy_thread (gpointer data);
static gboolean
vvas_xcompositor_set_zorder_from_pads (GstVvasXCompositor * self);
static void
vvas_xcompositor_adjust_zorder_after_eos (GstVvasXCompositor * self,
    guint index);

GType gst_vvas_xcompositor_pad_get_type (void);

/** @brief  Glib's convenience macro for GstVvasXCompositor type implementation.
 *  @details This macro does below tasks:\n
 *                  - Declares a class initialization function with prefix gst_vvas_xcompositor \n
 *                  - Declares an instance initialization function\n
 *                  - A static variable named gst_vvas_allocator_parent_class pointing to the GST_VIDEO_AGGREGATOR
 *                  parrnt class\n
 *                  - Defines a gst_vvas_xcompositor_get_type() function with below tasks\n
 *                  - Initializes GTypeInfo function pointers\n
 *                  - Registers GstVvasXCompositorPrivate as private structure to GstVvasXCompositor type\n
 *                  - Adds interface to access the child properties from the parent element
 */

G_DEFINE_TYPE_WITH_CODE (GstVvasXCompositor, gst_vvas_xcompositor,
    GST_TYPE_VIDEO_AGGREGATOR, G_ADD_PRIVATE (GstVvasXCompositor)
    G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_vvas_xcompositor_child_proxy_init));

/** @brief  Glib's convenience macro for GstVvasXCompositorPad type implementation.
 *  @details This macro does below tasks:\n
 *                  - Declares a class initialization function with prefix gst_vvas_xcompositor_pad \n
 *                  - Declares an instance initialization function\n
 */
G_DEFINE_TYPE (GstVvasXCompositorPad, gst_vvas_xcompositor_pad,
    GST_TYPE_VIDEO_AGGREGATOR_PAD);

/**
 *  @brief *  Initialize new debug category vvas_xcompositor for logging
 */
GST_DEBUG_CATEGORY (gst_vvas_xcompositor_debug);

/**
 *  @brief Defines a static GstDebugCategory global variable with name GST_CAT_PERFORMANCE for
 *  performance logging purpose
 */
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

/**
 *  @fn static GType vvas_xcompositor_num_taps_type (void)
 *  @return registered GType
 *  @brief  This API registers a new static enumeration type with the name GstVvasXCompositorNumTapsType
 */
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

/**
 *  @fn static GType vvas_xcompositor_ppc_type (void)
 *  @return registered GType
 *  @brief  This API registers a new static enumeration type with the name GstVvasXCompositorPPCType
 */
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

/**
 *  @fn static GType vvas_xcompositor_coef_load_type (void)
 *  @return registered GType
 *  @brief  This API registers a new static enumeration type with the name GstVvasXCompositorCoefLoadType
 */
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

/**
 *  @fn static guint vvas_xcompositor_get_stride (GstVideoInfo * info, guint width)
 *  @param [in] *info	- Information describing video properties.
 *  @param [in] width	- width of the video.
 *  @return on Success stride value\n 
 *          on Failure 0.
 *  @brief  This API calculates the stride value based on video format and video width.
 */
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

/**
 *  @fn static gboolean vvas_xcompositor_decide_allocation (GstAggregator * agg, GstQuery * query)
 *  @param [in] agg         - Handle to GstAggregator typecasted to GObject.
 *  @param [in] query       - Response for the allocation query.
 *  @return On Success returns TRUE
 *          On Failure returns FALSE
 *  @brief  This function will decide allocation strategy based on the preference from downstream element.
 *  @details The proposed query will be parsed through, verified if the proposed pool is VVAS and alignments
 *           are quoted. Otherwise it will be discarded and new pool,allocator will be created.
 */
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

 /** we got configuration from our peer or the decide_allocation method,
  *  parse them 
  */
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

  /* Fetch the video information from requested caps */
  gst_query_parse_allocation (query, &outcaps, NULL);
  if (outcaps && !gst_video_info_from_caps (&out_vinfo, outcaps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from outcaps");
    goto error;
  }
  self->priv->out_vinfo = gst_video_info_copy (&out_vinfo);
  /* Get the pool parameters in query */
  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    size = MAX (size, out_vinfo.size);
    update_pool = TRUE;
    if (min == 0)
      min = 3;
  } else {
    /* If there are no proposed pools create a new pool */
    pool = NULL;
    min = 3;
    max = 0;
    size = out_vinfo.size;
    update_pool = FALSE;
  }

 /** Check if the proposed pool is VVAS Buffer Pool and stride
  *  is aligned with (8 * ppc)
  *  If otherwise, discard the pool. Will create a new one
  */
  if (pool) {
    GstVideoAlignment video_align = { 0, };
    guint padded_width = 0;
    guint stride = 0, multiscaler_req_stride;

    config = gst_buffer_pool_get_config (pool);
    if (config && gst_buffer_pool_config_has_option (config,
            GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
      gst_buffer_pool_config_get_video_alignment (config, &video_align);

      /** We have adding padding_right and padding_left in pixels.
       *  We need to convert them to bytes for finding out the complete stride
       *  with alignment
       */
      padded_width =
          out_vinfo.width + video_align.padding_right +
          video_align.padding_left;
      stride = vvas_xcompositor_get_stride (&out_vinfo, padded_width);

      GST_INFO_OBJECT (self, "output stride = %u", stride);

      if (!stride)
        return FALSE;
      gst_structure_free (config);
      config = NULL;
      multiscaler_req_stride = WIDTH_ALIGN;
      /* Discard the pool to create a new pool with alignment requirements */
      if (stride % multiscaler_req_stride) {
        GST_WARNING_OBJECT (self, "Discarding the propsed pool, "
            "Alignment not matching with 8 * self->ppc");
        gst_object_unref (pool);
        pool = NULL;
        update_pool = FALSE;

        self->out_stride_align = multiscaler_req_stride;
        self->out_elevation_align = 1;
        GST_DEBUG_OBJECT (self, "Going to allocate pool with stride_align %d"
            "and elevation_align %d", self->out_stride_align,
            self->out_elevation_align);
      }
    } else {
      /** VVAS Buffer Pool but no alignment information.
       *  Discard to create pool with scaler alignment requirements
       */
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
    /* If pool is not a vvas pool , discard it to create a new pool */
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
  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);


  /** If there is no pool allignment requirement from downstream or
   *  if scaling dimension
   *  is not aligned to (8 * ppc), then we will create a new pool
   */

  if (!pool && (self->out_stride_align == 1)
      && ((out_vinfo.stride[0] % WIDTH_ALIGN)
          || (out_vinfo.height % HEIGHT_ALIGN))) {

    self->out_stride_align = WIDTH_ALIGN;
    self->out_elevation_align = HEIGHT_ALIGN;
  }

  if (!pool) {
    GstVideoAlignment align;
    /* Create a new pool, if pool is discarded */
    pool =
        gst_vvas_buffer_pool_new (self->out_stride_align,
        self->out_elevation_align);
    GST_INFO_OBJECT (self, "created new pool %p %" GST_PTR_FORMAT, pool, pool);

    /* Update the newly created pool at 0th index,
     * if there are more pools in query */
    update_pool = TRUE;
    config = gst_buffer_pool_get_config (pool);
    gst_video_alignment_reset (&align);
    align.padding_bottom =
        ALIGN (GST_VIDEO_INFO_HEIGHT (&out_vinfo),
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
  /**  Since self->out_stride_align is common for all channels
   *   reset the output stride to 1 (its default), so that other channels
   *   are not affected
   */
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
      "allocated pool %p with parameters : size %u, min_buffers = %u, "
      "max_buffers = %u", pool, size, min, max);

  if (allocator)
    gst_object_unref (allocator);

  if (update_pool && (gst_query_get_n_allocation_pools (query) > 0))
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
    /** create a new copy thread to copy the input buffers for
     *  improving performance
     */
    priv->input_copy_thread = g_thread_new ("compositor-input-copy-thread",
        vvas_xcompositor_input_copy_thread, self);
  }

  /** When all the pools are configured successfully, open the resources for
   *  compositor plugin
   */
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

/**
 *  @fn static gchar * vvas_xcompositor_prepare_request_json_string (GstVvasXCompositor * compositor)
 *  @param [in] compositor  - Handle to GstVvasXCompositor object.
 *  @return On Success returns prepared json string\n
 *          On Failure returns NULL
 *  @brief  This API prepares a json string based on input and output video information, which is later used
 *          in calculating xrm load.
 */
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

/**
 *  @fn static gchar * vvas_xcompositor_calculate_load (GstVvasXCompositor * self, gint * load)
 *  @param [in] self    - Handle to GstVvasXCompositor object.
 *  @parm [out] load    - Pointer to store the calculated load.
 *  @return On Success returns True
 *          On Failure returns False
 *  @brief  This API calculates the XRM load
 */
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

/**
 *  @fn static gboolean vvas_xcompositor_allocate_xrm_resource (GstVvasXCompositor * self, gint compositor_load)
 *  @param [in] self                - Handle to GstVvasXCompositor object.
 *  @param [in] compositor_load     - calculated xrm load
 *  @return On Success returns True
 *          On Failure returns False
 *  @brief  This API allocates the compute unit resources using XRM APIs.
 *  @details This API allocates the compute unit resources on scaler kernel on different
 *            available devices using XRM APIS.
 */
static gboolean
vvas_xcompositor_allocate_xrm_resource (GstVvasXCompositor * self,
    gint compositor_load)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  int iret = -1;

  GST_INFO_OBJECT (self, "going to request %d%% load using xrm",
      (compositor_load * 100) / XRM_MAX_CU_LOAD_GRANULARITY_1000000);

  if (getenv ("XRM_RESERVE_ID") || priv->reservation_id) {
    /* use reservation_id to allocate compositor */
    int xrm_reserve_id = 0;
    xrmCuPropertyV2 compositor_prop;
    xrmCuResourceV2 *cu_resource;
    guint k_name_len;

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

    k_name_len = strchr (self->kern_name, ':') - self->kern_name;

    if (!k_name_len || (k_name_len >= XRM_MAX_NAME_LEN)) {
      return FALSE;
    }
    strncpy (compositor_prop.kernelName, self->kern_name, k_name_len);
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
    uuid_copy (priv->xclbinId, cu_resource->uuid);
    priv->cu_resource_v2 = cu_resource;

  } else {
    /* use user specified device to allocate compositor */
    xrmCuProperty compositor_prop;
    xrmCuResource *cu_resource;
    guint k_name_len;

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

    k_name_len = strchr (self->kern_name, ':') - self->kern_name;

    if (!k_name_len || (k_name_len >= XRM_MAX_NAME_LEN)) {
      return FALSE;
    }
    strncpy (compositor_prop.kernelName, self->kern_name, k_name_len);
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
    uuid_copy (priv->xclbinId, cu_resource->uuid);
    priv->cu_resource = cu_resource;
  }

  return TRUE;
}

/**
 *  @fn static gboolean vvas_xcompositor_destroy_xrm_resource (GstVvasXCompositor * self)
 *  @param [in] self  - Handle to GstVvasXCompositor instance.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  Cleans up XRM and XRT context.
 *  @details Cleans up XRM and XRT context.
 */
static gboolean
vvas_xcompositor_destroy_xrm_resource (GstVvasXCompositor * self)
{
  GstVvasXCompositorPrivate *priv = self->priv;
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
 *  @fn static void vvas_xcompositor_adjust_zorder_after_eos (GstVvasXCompositor * self, guint pad_index)
 *  @param [in] self  		- pointer to the vvas compositor instance.
 *  @param [in] pad_index 	- the index of the pad that got eos.
 *  @return None.
 *  @brief  This API adjusts the Z-order of the pads when a pad receives eos.
 *          It moves the eos pad to bottom of all the pads.
 */
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

/**
 *  @fn static gboolean vvas_xcompositor_set_zorder_from_pads (GstVvasXCompositor * self)
 *  @param [in] *self  - pointer to the vvas compositor instance.
 *  @return TRUE on success.\nFALSE on failure.
 *  @brief  This API fills the Z-order if it is not set from the command line pad properties .
 */
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
  /* Initializing  missing_zorders_list list from 0 to number of request pads */
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
          "zorder value %d of pad sink_%d exceeding current limit %d "
          "or invalid", pad->zorder, index, self->num_request_pads - 1);
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

    /** Removing already assigned z-orders from already
     *  initialized missing list
     */
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
  /* Finalizing a pad for each zorder */
  for (index = 0; index < self->num_request_pads; index++)
    self->priv->pad_of_zorder[zorder[index]] = index;
  g_free (missing_zorders_data);
  g_free (unfilled_pads_data);
  for (index = 0; index < self->num_request_pads; index++)
    GST_INFO_OBJECT (self, "Final zorder at pad %d is %d ", index,
        zorder[index]);

  g_list_free (missing_zorders_list);
  g_list_free (unfilled_pads_list);
  return TRUE;
}

/**
 *  @fn static gboolean vvas_xcompositor_open (GstVvasXCompositor * self)
 *  @param [in] self  - pointer to the vvas compositor instance.
 *  @return TRUE on Success.\nFALSE on Failure.
 *  @brief  This API opens all the resources and sets all the required parameters for the plugin
 *  @details It opens the xrt context, kernel handle and device handle. Also downloads the xclbin to the FPGA
 */
static gboolean
vvas_xcompositor_open (GstVvasXCompositor * self)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  guint chan_id = 0;
  gboolean bret;
  VvasReturnType vret;
  VvasScalerProp scaler_prop = { 0 };
#ifdef ENABLE_XRM_SUPPORT
  gint load = -1;
#endif

  VvasLogLevel core_log_level =
      vvas_get_core_log_level (gst_debug_category_get_threshold
      (GST_CAT_DEFAULT));

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
  bret = vvas_xcompositor_allocate_xrm_resource (self, priv->cur_load);
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


  /* Create VVAS Context, Create Scaler context and Set Scaler Properties */
  GST_DEBUG_OBJECT (self, "Creating VVAS context, XclBin: %s",
      self->xclbin_path);
  priv->vvas_ctx =
      vvas_context_create (self->dev_index, self->xclbin_path,
      core_log_level, &vret);
  if (!priv->vvas_ctx) {
    GST_ERROR_OBJECT (self, "Couldn't create VVAS context");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Creating VVAS Scaler, kernel: %s", self->kern_name);
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

  bret = vvas_xcompositor_set_zorder_from_pads (self);
  if (!bret)
    return FALSE;
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    if (GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo[chan_id]) % self->ppc) {
      GST_ERROR_OBJECT (self, "Unsupported input resolution at sink_%d,"
          "width must be multiple of ppc i.e, %d", chan_id, self->ppc);
      return FALSE;
    }
  }

  if (GST_VIDEO_INFO_WIDTH (self->priv->out_vinfo) % self->ppc) {
    GST_ERROR_OBJECT (self, "Unsupported output resolution,"
        "width must be multiple of ppc i.e, %d", self->ppc);
    return FALSE;
  }
  return TRUE;
}

/**
 *  @fn static void vvas_xcompositor_close (GstVvasXCompositor * self)
 *  @param [in] self  - pointer to the vvas compositor instance.
 *  @return None.
 *  @brief  This API closes/frees the resources that are opened during vvas_xcompositor_open call.
 *  @details This API closes the xrm/xrt context, device handle and kernel handle.
 */
static void
vvas_xcompositor_close (GstVvasXCompositor * self)
{
  guint chan_id;
  GstVvasXCompositorPad *sinkpad;
  GST_DEBUG_OBJECT (self, "Closing");

  /* clear output buffer pool */
  gst_clear_object (&self->priv->output_pool);

  /* clear all input buffer pools */
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    sinkpad = gst_vvas_xcompositor_sinkpad_at_index (self, chan_id);
    if (sinkpad && sinkpad->pool) {
      if (gst_buffer_pool_is_active (sinkpad->pool))
        gst_buffer_pool_set_active (sinkpad->pool, FALSE);
      gst_clear_object (&sinkpad->pool);
    }
  }
#ifdef ENABLE_XRM_SUPPORT
  vvas_xcompositor_destroy_xrm_resource (self);
#endif
}

/**
 *  @fn static gpointer vvas_xcompositor_input_copy_thread (gpointer data)
 *  @param [in] data  - pointer to the vvas compositor instance.
 *  @brief  This API is a thread function that copies input buffer to plugins internal
 *          pool in separate thread to improve performance.
 */
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

/**
 *  @fn static GstStateChangeReturn gst_vvas_xcompositor_change_state (GstElement * element, GstStateChange transition)
 *  @param [in] element       - Handle to GstVvasXCompositor typecasted to GObject.
 *  @param [in] transition    - The requested state transition.
 *  @return Status of the state transition.
 *  @brief  This API will be invoked whenever the pipeline is going into a state transition and in this function
 *          the element can initialize/free up any sort of specific data needed by the element.
 *  @details This API is registered with GstElementClass by overriding GstElementClass::change_state function
 *           pointer and this will be invoked whenever the pipeline is going into a state transition.
 */
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

  /* getting the GstStateChangeReturn from parent class */
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* Closing the compositor resources if state is changing to NULL */
      vvas_xcompositor_close (self);
      break;

    default:
      break;
  }

  return ret;
}

/**
 *  @fn static void gst_vvas_xcompositor_pad_get_property (GObject * object,
 *                                                         guint prop_id,
 *                                                         GValue * value,
 *                                                         GParamSpec * pspec)
 *  @param [in] object     - Handle to GstVvasXCompositorPad typecasted to GObject
 *  @param [in] prop_id    - Property ID as defined in VvasXCompositorPad proprties enum
 *  @param [out] value     - GValue which holds property value set by user
 *  @param [in] pspec      - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief   This API gives out values asked from the user in GstVvasXCompositorPad object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::get_property function pointer and
 *           this will be invoked when developer gets properties on GstVvasXCompositorPad object. Based on
 *           property value type, corresponding g_value_get_xxx API will be called to get property
 *           value from GValue handle.
 */
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


/**
 *  @fn static void gst_vvas_xcompositor_pad_set_property (GObject * object,
 *                                                         guint prop_id,
 *                                                         const GValue * value,
 *                                                         GParamSpec * pspec)
 *  @param [in] object     - Handle to GstVvasXCompositorPad typecasted to GObject
 *  @param [in] prop_id    - Property ID as defined in VvasXCompositor properties enum
 *  @param [in] value      - GValue which holds property value set by user
 *  @param [in] pspec      - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief   This API stores values sent from the user in GstVvasXCompositorPad object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::set_property function pointer and
 *           this will be invoked when developer sets properties on GstVvasXCompositorPad object. Based on
 *           property value type, corresponding g_object_get_xxx API will be called to
 *           get property value from GValue handle.
 */
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

/**
 *  @fn static void gst_vvas_xcompositor_pad_class_init (GstVvasXCompositorPadClass * klass)
 *  @param [in]klass  - Handle to GstVvasXAbrScalerClass
 *  @return None
 *  @brief  Add properties and signals of GstVvasXCompositorPadClass to parent GObjectClass and ovverrides
 *          function pointers present in itself and/or its parent class structures
 *  @details This function publishes properties those can be set/get from application on GstVvasXCompositorPad object.
 *           And, while publishing a property it also declares type, range of acceptable values, default value,
 *           readability/writability and in which GStreamer state a property can be changed.
 */
static void
gst_vvas_xcompositor_pad_class_init (GstVvasXCompositorPadClass * klass)
{
  GObjectClass *gobject_class;
  gobject_class = (GObjectClass *) klass;

  /* Update GobjectClass callback functions */
  gobject_class->set_property = gst_vvas_xcompositor_pad_set_property;
  gobject_class->get_property = gst_vvas_xcompositor_pad_get_property;

  /* Install GstVvasXCompositorPad properties */
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

/**
 *  @fn static void gst_vvas_xcompositor_pad_init (GstVvasXCompositorPad * pad)
 *  @param [in] pad - Handle to GstVvasXCompositorPad instance
 *  @return None
 *  @brief  Initializes GstVvasXCompositorPad member variables to default and does one time object/memory
 *          allocations in object's lifecycle.
 */
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

/**
 *  @fn static void gst_vvas_xcompositor_class_init (GstVvasXCompositorClass * klass)
 *  @param [in] klass  - Handle to GstVvasXCompositorClass
 *  @return None
 *  @brief  Add properties and signals of GstGstVvasXCompositorClass to parent GObjectClass and ovverrides
 *          function pointers present in itself and/or its parent class structures
 *  @details This function publishes properties those can be set/get from application on GstVvasXCompositor object.
 *           And, while publishing a property it also declares type, range of acceptable values, default value,
 *           readability/writability and in which GStreamer state a property can be changed.
 */
static void
gst_vvas_xcompositor_class_init (GstVvasXCompositorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstVideoAggregatorClass *videoaggregator_class =
      (GstVideoAggregatorClass *) klass;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;

  /* Initilizing debug category structure */

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vvas_xcompositor", 0,
      "VVAS Compositor");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");

  /* Update gobject class callback functions */
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_vvas_xcompositor_finalize);

  gobject_class->get_property = gst_vvas_xcompositor_get_property;
  gobject_class->set_property = gst_vvas_xcompositor_set_property;

  gst_element_class_set_metadata (element_class, "VVAS Compositor",
      "Vvas Compositor", "VVAS Compositor", "Xilinx Inc <www.xilinx.com>");

  /* Add src pad template */
  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &src_factory, GST_TYPE_AGGREGATOR_PAD);
  /* Add sink pad template */
  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &sink_factory, GST_TYPE_VVAS_XCOMPOSITOR_PAD);

  /* Update element class callback functions */
  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_vvas_xcompositor_request_new_pad);
  element_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_vvas_xcompositor_release_pad);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vvas_xcompositor_change_state);

  /* Update aggregator class callback functions */
  agg_class->stop = gst_vvas_xcompositor_stop;
  agg_class->start = gst_vvas_xcompositor_start;

  agg_class->src_query = gst_vvas_xcompositor_src_query;
  agg_class->sink_query = gst_vvas_xcompositor_sink_query;

  agg_class->decide_allocation = vvas_xcompositor_decide_allocation;

  /* Update video aggregator class callback functions */
  videoaggregator_class->aggregate_frames =
      gst_vvas_xcompositor_aggregate_frames;
  videoaggregator_class->create_output_buffer =
      gst_vvas_xcompositor_create_output_buffer;

  /* Install Vvas_XCompositor pad properties */
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
      g_param_spec_int ("scale-mode", "Scaling Mode", "Scale Mode configured "
          "in Multiscaler kernel. 	\
		0: BILINEAR \n 1: BICUBIC \n2: POLYPHASE", 0, 2, VVAS_XCOMPOSITOR_SCALE_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

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
          "Enable buffer pipelining to improve performance in non "
          "zero-copy use cases",
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

/**
 *  @fn static void gst_vvas_xcompositor_init (GstVvasXCompositor * self)
 *  @param [in] self - Handle to GstVvasXCompositor instance
 *  @return None
 *  @brief  Initializes GstVvasXCompositor member variables to default and does one time object/memory
 *          allocations in object's lifecycle
 */
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
#ifdef ENABLE_XRM_SUPPORT
  self->priv->xrm_ctx = NULL;
  self->priv->cu_resource = NULL;
  self->priv->cur_load = 0;
  self->priv->reservation_id = 0;
  self->priv->has_error = FALSE;
#endif
  self->kern_name = g_strdup (DEFAULT_KERNEL_NAME);
  self->priv->need_copy = TRUE;
  self->priv->vvas_ctx = NULL;
  self->priv->vvas_scaler = NULL;
  for (int chan_id = 0; chan_id < MAX_CHANNELS; chan_id++) {
    self->priv->in_vinfo[chan_id] = gst_video_info_new ();
    self->priv->pad_of_zorder[chan_id] = chan_id;
  }
}

/**
 *  @fn static void gst_vvas_xcompositor_finalize (GObject * object)
 *  @param [in] object - Handle to GstVvasXCompositor typecasted to GObject
 *  @return None
 *  @brief This API will be called during GstVvasXCompositor object's destruction phase. Close references to devices
 *         and free memories if any
 *  @note After this API GstVvasXCompositor object \p obj will be destroyed completely. So free all internal memories
 *        held by current object
 */
static void
gst_vvas_xcompositor_finalize (GObject * object)
{
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (object);
  for (int chan_id = 0; chan_id < MAX_CHANNELS; chan_id++)
    gst_video_info_free (self->priv->in_vinfo[chan_id]);
  gst_video_info_free (self->priv->out_vinfo);
  g_free (self->xclbin_path);
  g_free (self->kern_name);

  /* Destroy Scaler and VVAS Context */
  if (self->priv->vvas_scaler) {
    vvas_scaler_destroy (self->priv->vvas_scaler);
  }
  if (self->priv->vvas_ctx) {
    vvas_context_destroy (self->priv->vvas_ctx);
  }
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 *  @fn static gboolean gst_vvas_xcompositor_query_caps (GstPad * pad, GstAggregator * agg, GstQuery * query)
 *  @param [in] pad     - Pointer to GstPad on which query has been received.
 *  @param [in] agg     - Pointer to GstAggregator holding compositor instance pointer
 *  @param [in] query   - Query requested on the plugin
 *  @return Return always TRUE.
 *  @brief  This API checks whether the requested caps are supported by the plugin mentioned in pad template
 *          and give response to the query in gst_query_set_caps_result during negotiation.
 */
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

  /* Checking caps are supported by plugin from pad template */
  if (filter)
    caps = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);

  /* setting the response to query */
  gst_query_set_caps_result (query, caps);

  gst_caps_unref (caps);

  return TRUE;
}

/**
 *  @fn static gboolean gst_vvas_xcompositor_src_query (GstAggregator * agg, GstQuery * query)
 *  @param [in] agg     - Pointer to GstAggregator holding compositor instance pointer
 *  @param [in] query   - Query requested on the plugin
 *  @return TRUE on success\n
            FALSE on failure
 *  @brief  Callback function called when a query is received on the src pad.
 */
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

/**
 *  @fn static gboolean gst_vvas_xcompositor_pad_sink_acceptcaps (GstPad * pad,
 *                                                                GstVvasXCompositor * compositor,
 *                                                                GstCaps * caps)
 *  @param [in] pad   		    - Pointer to GstPad on which query has been received.
 *  @param [in] compositor	    - Pointer to GstVvasXCompositor instance
 *  @param [in] caps     	    - caps that are received on the pad
 *  @return TRUE on success\n
 *          FALSE on failure.
 *  @brief  This API checks whether the requested caps are supported by the plugin mentioned in pad template
 */
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

/**
 *  @fn static gboolean vvas_xcompositor_propose_allocation (GstVvasXCompositor * self, GstQuery * query)
 *  @param [in] self    - Handle that is holding GstVvasXCompositor instance
 *  @param [out] query	- Return the query with proposed allocation parameters
 *  @return TRUE on success\n
 *          FALSE on failure.
 *  @brief  Propose buffer allocation parameters for upstream element
 *  @details The proposed query will be parsed through, verified if the proposed pool is VVAS and alignments
 *           are quoted. Otherwise it will be discarded and new pool, allocator will be created.
 */
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

  /** Parse the received query to find the pool array size
   *  of the query's structure
   */
  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstStructure *structure;
    GstAllocator *allocator = NULL;
    GstAllocationParams params =
        { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0, 0 };
    /* Get the allocator and its params from the query */
    if (gst_query_get_n_allocation_params (query) > 0) {
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    } else {
      /*#ifdef ENABLE_XRM_SUPPORT
         if (!(self->priv->cu_resource || self->priv->cu_resource_v2))  {
         GST_ERROR_OBJECT (self, "scaler resource not yet allocated");
         return FALSE;
         }
         #endif */
      /** Create a new allocator if the query doesn't
       *  have any allocator params
       */
      allocator = gst_vvas_allocator_new (self->dev_index,
          NEED_DMABUF, self->in_mem_bank);
      GST_INFO_OBJECT (self, "creating new xrt allocator %" GST_PTR_FORMAT
          "at mem bank %d", allocator, self->in_mem_bank);

      gst_query_add_allocation_param (query, allocator, &params);
    }

    /* Create new video buffer pool */
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

    gst_buffer_pool_config_set_allocator (structure, allocator, &params);

    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    if (allocator)
      gst_object_unref (allocator);

    if (!gst_buffer_pool_set_config (pool, structure))
      goto config_failed;

    GST_OBJECT_LOCK (self);
    /* Set the pool parameters in query */
    gst_query_add_allocation_pool (query, pool, size, 2, 0);

    GST_OBJECT_UNLOCK (self);

    gst_query_add_allocation_pool (query, pool, size, 2, 0);
    /* Add GST_VIDEO_META_API_TYPE as one of supported metadata API to query */
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
 *  @fn static gboolean gst_vvas_xcompositor_sink_query (GstAggregator * agg,
 *                                                       GstAggregatorPad * bpad,
 *                                                       GstQuery * query)
 *  @param [in] agg     - Pointer to GstAggregator holding compositor instance pointer
 *  @param [in] bpad    - Pointer to GstAggregatorPad holding GstVvasXCompositorPad instance pointer
 *  @param [in] query   - Query requested on the pad
 *  @return TRUE on success\n
 *          FALSE on failure
 *  @brief  Callback function called when a query is received on the sink pad.
 */
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
      /* set supported caps response to the query */
      gst_query_set_caps_result (query, mycaps);
      return TRUE;
    }

    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;
      int pad_idx;
      gst_query_parse_accept_caps (query, &caps);

      /* Accept the supported caps */
      ret =
          gst_vvas_xcompositor_pad_sink_acceptcaps (GST_PAD (bpad), self, caps);
      if (ret) {
        pad_idx = sinkpad->index;
        /* store the incoming caps video information of the current pad  */
        gst_video_info_from_caps (self->priv->in_vinfo[pad_idx], caps);
      }
      /* set the response to the accep caps query */
      gst_query_set_accept_caps_result (query, ret);
    }
      return TRUE;
      break;

    case GST_QUERY_ALLOCATION:
    {
      /* Propose allocation if the query is allocation type */
      ret = vvas_xcompositor_propose_allocation (self, query);
      return ret;
    }
      break;

      /* call the parent class sink_query callback in default case */
    default:
      return GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, bpad, query);
  }
}

/**
 *  @fn gboolean vvas_xcompositor_allocate_internal_pool (GstVvasXCompositor * self, GstVvasXCompositorPad * sinkpad)
 *  @param [in] self  	- GstVvasXCompositor plugin handle.
 *  @param [in] sinkpad - Pointer that holds GstVvasXCompositorPad.
 *  @return On Success returns TRUE\n
 *          On Failure returns FALSE
 *  @brief  Allocates internal buffer pool.
 *  @details This function will be invoked to create internal buffer pool when
 *           the received buffer is non VVAS buffer or non DMA buffer.
 */
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

  /* Create a new vvas buffer pool */
  pool = gst_vvas_buffer_pool_new (WIDTH_ALIGN, HEIGHT_ALIGN);
  GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

  config = gst_buffer_pool_get_config (pool);
  /* Align the buffers */
  gst_video_alignment_reset (&align);
  align.padding_bottom =
      ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
      HEIGHT_ALIGN) - GST_VIDEO_INFO_HEIGHT (&info);
  for (int idx = 0; idx < GST_VIDEO_INFO_N_PLANES (&info); idx++) {
    align.stride_align[idx] = (WIDTH_ALIGN - 1);
  }

  gst_video_info_align (&info, &align);

  /* Add the updated alignment parameters to pool */
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_set_video_alignment (config, &align);

  /* Create a new allocator */
  allocator = gst_vvas_allocator_new (self->dev_index,
      NEED_DMABUF, self->in_mem_bank);
  gst_allocation_params_init (&alloc_params);
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
  alloc_params.flags |= GST_VVAS_ALLOCATOR_FLAG_MEM_INIT;
  GST_INFO_OBJECT (self,
      "allocated %" GST_PTR_FORMAT " allocator at mem bank %d",
      allocator, self->in_mem_bank);

  /* Configure the Min Max buffers of the pool */
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

  /* Unreff the older pool if any and assign the new pool */
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

/**
 *  @fn gboolean vvas_xcompositor_prepare_input_buffer (GstVvasXCompositor * self,
 *                                                      GstVvasXCompositorPad * sinkpad,
 *                                                      GstBuffer ** inbuf)
 *  @param [in] self  		- Handle that holds the GstVvasXCompositor instance.
 *  @param [in] sinkpad 	- Pointer that holds the GstVvasXCompositorPad object.
 *  @param [in] inbuf  		- input buffer coming from upstream.
 *  @return On Success returns TRUE\n
 *          On Failure returns FALSE
 *  @brief   Decides and prepares internal buffer pool if necessary.
 *  @details Checks if the incoming buffer is VVAS memory or DMA buffer, if
 *           neither of them, then it creates an internal buffer pool.
 */
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

  /* Clear the video frames */
  memset (&in_vframe, 0x0, sizeof (GstVideoFrame));
  memset (&own_vframe, 0x0, sizeof (GstVideoFrame));

  /* Get memory from the input buffer */
  in_mem = gst_buffer_get_memory (*inbuf, 0);
  if (!in_mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    goto error;
  }

  /* Check whether the input memory is a vvas memory */
  if (gst_is_vvas_memory (in_mem)
      && gst_vvas_memory_can_avoid_copy (in_mem, self->dev_index,
          self->in_mem_bank)) {

    /* Fetch the physical address from vvas memory */
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

    /* Fetch the physical address from dma memory */
    phy_addr = vvas_xrt_get_bo_phy_addres (bo);

    if (bo != NULL)
      vvas_xrt_free_bo (bo);

  } else {
    /* If not a vvas or dma memory, use internal pool buffers */
    use_inpool = TRUE;
  }

  gst_memory_unref (in_mem);
  in_mem = NULL;

  /* If not a vvas or dma memory use internal pool buffers */
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

    /* if pipeline is enabled deal with the already copied input buffer */
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
      /* if pipeline is disabled acquire buffer from own input pool */
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

      /* copy the input buffer to own buffer from internal pool and reassign to inbuf */
      gst_buffer_copy_into (own_inbuf, *inbuf,
          (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS |
              GST_BUFFER_COPY_TIMESTAMPS), 0, -1);
      *inbuf = own_inbuf;
    }

  } else {
    /* Use the incoming input buffer if it is vvas buffer or dma buffer */
    gst_buffer_ref (*inbuf);
  }

  /* Get the video data from input buffer */
  vmeta = gst_buffer_get_video_meta (*inbuf);
  if (vmeta == NULL) {
    GST_ERROR_OBJECT (self, "video meta not present in buffer");
    goto error;
  }

  /* Get the memory from decided input buffer */
  in_mem = gst_buffer_get_memory (*inbuf, 0);
  if (!in_mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    goto error;
  }

  /* Get the physical address of the decided input buffer's memory */
  if (phy_addr == (uint64_t) - 1) {
    phy_addr = gst_vvas_allocator_get_paddr (in_mem);
  }
  /* syncs data when XLNX_SYNC_TO_DEVICE flag is enabled */
  bret = gst_vvas_memory_sync_bo (in_mem);
  if (!bret)
    goto error;

  gst_memory_unref (in_mem);

  GST_LOG_OBJECT (self, "input paddr %p", (void *) phy_addr);

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

/**
 *  @fn gboolean vvas_xcompositor_prepare_output_buffer (GstVvasXCompositor * self, GstBuffer * outbuf)
 *
 *  @param [in] self  	- Handle that holds GstVvasXCompositor instance.
 *  @param [in] outbuf	- pointer to output buffer.
 *  @return On Success returns TRUE\n
 *          On Failure returns FALSE
 *  @brief   Prepare the output buffer as per its type.
 *  @details This API gets physical address of output buffer
 *           based on whether it's a VVAS allocator memory or DMA memory.
 */
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
  /** No need to check whether memory is from device or not here.
   *  Because, we have made sure memory is allocated from device in
   *  decide_allocation
   */
  if (gst_is_vvas_memory (mem)) {

    /* Get the physical address of the output buffer */
    phy_addr = gst_vvas_allocator_get_paddr (mem);
  } else if (gst_is_dmabuf_memory (mem)) {
    vvasBOHandle bo = NULL;
    gint dma_fd = -1;

    dma_fd = gst_dmabuf_memory_get_fd (mem);
    if (dma_fd < 0) {
      GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
      goto error;
    }

    /* Get the dmabuf but not from xrt */
    bo = vvas_xrt_import_bo (self->priv->dev_handle, dma_fd);
    if (bo == NULL) {
      GST_WARNING_OBJECT (self,
          "failed to get XRT BO...fall back to copy input");
    }
    GST_INFO_OBJECT (self, "received dma fd %d and its xrt BO = %p", dma_fd,
        bo);

    /* Get the physical address of dma buf */
    phy_addr = vvas_xrt_get_bo_phy_addres (bo);
    if (bo != NULL)
      vvas_xrt_free_bo (bo);
  }

  /* Get the video meta data of the output buffer */
  vmeta = gst_buffer_get_video_meta (outbuf);
  if (vmeta == NULL) {
    GST_ERROR_OBJECT (self, "video meta not present in buffer");
    goto error;
  }

  GST_DEBUG_OBJECT (self, "Output buffer phy address: %lu", phy_addr);

  gst_memory_unref (mem);

  return TRUE;

error:
  if (mem)
    gst_memory_unref (mem);

  return FALSE;
}

/**
 *  @fn static void vvas_xcompositor_free_vvas_video_frame (GstVvasXCompositor * self)
 *  @param [in] self    - Handle to GstVvasXCompositor instance.
 *  @return None
 *  @brief  This function will free all the VvasVideoFrames.
 */
static inline void
vvas_xcompositor_free_vvas_video_frame (GstVvasXCompositor * self)
{
  guint chan_id = 0;
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    if (self->priv->input_frames[chan_id]) {
      vvas_video_frame_free (self->priv->input_frames[chan_id]);
      self->priv->input_frames[chan_id] = NULL;
    }
  }

  if (self->priv->output_frame) {
    vvas_video_frame_free (self->priv->output_frame);
    self->priv->output_frame = NULL;
  }
}

/**
 *  @fn static bool vvas_xcompsitor_add_processing_channels (GstVvasXCompositor * self)
 *  @param [in] self    - Handle to GstVvasXCompositor instance.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  This function will add the channels into the VVAS CORE Scaler library for processing.
 */
static bool
vvas_xcompsitor_add_processing_channels (GstVvasXCompositor * self)
{
  GstVideoMeta *meta_out;
  guint chan_id;
  guint quad_in_each_row_column = (guint) ceil (sqrt (self->num_request_pads));
  gfloat in_width_scale_factor = 1, in_height_scale_factor = 1;
  GstVvasXCompositorPrivate *priv = self->priv;

  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    /* setting input output values based on zoder for that pad */
    GstVvasXCompositorPad *pad;
    uint32_t out_width = 0, out_height = 0;
    uint32_t in_width, in_height;
    uint32_t xpos_offset = 0;
    uint32_t ypos_offset = 0;
    GstBuffer *input_buffer;
    GstVideoInfo *vinfo;
    VvasVideoFrame *input_frame;
    VvasReturnType vret;
    VvasScalerRect src_rect = { 0 };
    VvasScalerRect dst_rect = { 0 };

    in_width = priv->in_vinfo[self->priv->pad_of_zorder[chan_id]]->width;
    in_height = priv->in_vinfo[self->priv->pad_of_zorder[chan_id]]->height;

    input_buffer = priv->inbufs[self->priv->pad_of_zorder[chan_id]];
    vinfo = priv->in_vinfo[self->priv->pad_of_zorder[chan_id]];

    /* Convert GstBuffer to VvasVideoFrame required for Vvas Core Scaler */
    input_frame = vvas_videoframe_from_gstbuffer (self->priv->vvas_ctx,
        self->in_mem_bank, input_buffer, vinfo, GST_MAP_READ);
    if (!input_frame) {
      GST_ERROR_OBJECT (self,
          "[%u] Couldn't convert input GstBuffer to VvasVideoFrame", chan_id);
      return FALSE;
    }

    priv->input_frames[self->priv->pad_of_zorder[chan_id]] = input_frame;

    pad = gst_vvas_xcompositor_sinkpad_at_index (self,
        self->priv->pad_of_zorder[chan_id]);

    meta_out = gst_buffer_get_video_meta (priv->outbuf);
    if (!meta_out) {
      return FALSE;
    }

    if (self->best_fit) {
      guint quadrant_height, quadrant_width;
      quadrant_width = (int) (meta_out->width / quad_in_each_row_column);
      quadrant_height = (int) (meta_out->height / quad_in_each_row_column);
      out_width = quadrant_width;
      out_height = quadrant_height;
      xpos_offset = quadrant_width * (chan_id % quad_in_each_row_column);
      ypos_offset = quadrant_height * (chan_id / quad_in_each_row_column);
    } else {
      if (pad->width != -1) {
        out_width = pad->width;
        if (pad->width < -1 || pad->width > meta_out->width || pad->width == 0) {
          GST_ERROR_OBJECT (self, "width of sink_%d is invalid",
              self->priv->pad_of_zorder[chan_id]);
          return false;
        }
      } else {
        out_width = in_width;
      }

      if (pad->height != -1) {
        out_height = pad->height;
        if (pad->height < -1 || pad->height > meta_out->height
            || pad->height == 0) {
          GST_ERROR_OBJECT (self, "height of sink_%d is invalid",
              self->priv->pad_of_zorder[chan_id]);
          return false;
        }
      } else {
        out_height = in_height;
      }

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

      /* cropping the image  at right corner if xpos exceeds output width */
      if (xpos_offset + out_width > meta_out->width) {
        in_width_scale_factor = ((float) in_width / out_width);
        in_width =
            (int) ((meta_out->width - xpos_offset) * in_width_scale_factor);
        out_width = meta_out->width - xpos_offset;
      }
      /* cropping the image  at bottom corner if ypos exceeds output height */
      if (ypos_offset + out_height > meta_out->height) {
        in_height_scale_factor = ((float) in_height / out_height);
        in_height =
            (int) ((meta_out->height - ypos_offset) * in_height_scale_factor);
        out_height = meta_out->height - ypos_offset;
      }

    }

    GST_INFO_OBJECT (self,
        "Height scale factor %f and Width scale factor %f ",
        in_height_scale_factor, in_width_scale_factor);
    GST_INFO_OBJECT (self, "Input height %d and width %d ", in_height,
        in_width);
    GST_INFO_OBJECT (self,
        "Aligned params are for sink_%d xpos %d ypos %d out_width %d out_height %d ",
        self->priv->pad_of_zorder[chan_id], xpos_offset, ypos_offset,
        out_width, out_height);

    /* Fill rect parameters */
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = in_width;
    src_rect.height = in_height;
    src_rect.frame = input_frame;

    dst_rect.x = xpos_offset;
    dst_rect.y = ypos_offset;
    dst_rect.width = out_width;
    dst_rect.height = out_height;
    dst_rect.frame = priv->output_frame;

    /* Add processing channel into Core Scaler */
    vret =
        vvas_scaler_channel_add (priv->vvas_scaler, &src_rect, &dst_rect, NULL,
        NULL);
    if (VVAS_IS_ERROR (vret)) {
      GST_ERROR_OBJECT (self, "[%u] failed to add processing channel in scaler",
          chan_id);
      return FALSE;
    }
    GST_DEBUG_OBJECT (self, "Added processing channel for idx: %u", chan_id);
  }
  return TRUE;
}

/**
 *  @fn gboolean vvas_xcompositor_process (GstVvasXCompositor * self)
 *  @param [In] self   - Handle to GstVvasXCompositor instance.
 *  @return On Success returns TRUE\n
 *          On Failure returns FALSE
 *  @brief   This function does the processing of the incoming buffers using Core Scaler.
 */
static gboolean
vvas_xcompositor_process (GstVvasXCompositor * self)
{
  GstVvasXCompositorPrivate *priv = self->priv;
  bool ret;
  GstMemory *mem = NULL;
  VvasReturnType vret;

  priv->output_frame = vvas_videoframe_from_gstbuffer (self->priv->vvas_ctx,
      self->out_mem_bank, priv->outbuf, priv->out_vinfo, GST_MAP_READ);
  if (!priv->output_frame) {
    GST_ERROR_OBJECT (self, "Could convert output GstBuffer to VvasVideoFrame");
    return FALSE;
  }

  ret = vvas_xcompsitor_add_processing_channels (self);
  if (!ret) {
    GST_ERROR_OBJECT (self, "couldn't add processing channels");
    return FALSE;
  }

  vret = vvas_scaler_process_frame (self->priv->vvas_scaler);
  if (VVAS_IS_ERROR (vret)) {
    GST_ERROR_OBJECT (self, "Failed to process frame in scaler");
    return FALSE;
  }

  mem = gst_buffer_get_memory (priv->outbuf, 0);
  if (mem == NULL) {
    GST_ERROR_OBJECT (self,
        "chan-%d : failed to get memory from output buffer", 0);
    return FALSE;
  }
  /* sync from device to host */
  gst_vvas_memory_set_sync_flag (mem, VVAS_SYNC_FROM_DEVICE);
  gst_memory_unref (mem);

  return TRUE;
}


/**
 *  @fn void vvas_xcompositor_get_empty_buffer (GstVvasXCompositor * self,
 *               GstVvasXCompositorPad * sinkpad, GstBuffer ** inbuf)
 *  @param [in] self  		- Handle that holds the GstVvasXCompositor instance.
 *  @param [in] sinkpad 	- Pointer that holds the GstVvasXCompositorPad object.
 *  @param [out] inbuf  	- pointer to input buffer.
 *  @return None
 *  @brief  This API assigns an empty buffer to input buffer
 *  @details It acquires an empty buffer from own pool and assigns it to input buffer.
 */
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

/**
 *  @fn static GstFlowReturn  gst_vvas_xcompositor_create_output_buffer (GstVideoAggregator * videoaggregator,
 *                                                                       GstBuffer ** outbuf)
 *  @param [in] videoaggregator - GstVideoAggregator pointer that holds the GstVvasXCompositor instance.
 *  @param [out] outbuf         - pointer to output buffer.
 *  @return GST_FLOW_OK on Success.\n
 *          GST_FLOW_ERROR on Failure.
 *  @brief  This is a callback function which provides output buffer.
 *  @details This is a callback function which is called by
 *           VideoAggregator class provides output buffer to be used as @outbuffer of
 *           the gst_vvas_xcompositor_aggregate_frames method.
 */
static GstFlowReturn
gst_vvas_xcompositor_create_output_buffer (GstVideoAggregator * videoaggregator,
    GstBuffer ** outbuf)
{
  GstAggregator *aggregator = GST_AGGREGATOR (videoaggregator);
  GstBufferPool *pool;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (videoaggregator);

  pool = gst_aggregator_get_buffer_pool (aggregator);

  /* Allocate buffers from the pool */
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
  } else {
    /** going to allocate the sw buffers of size out_vinfo
     *  which are not padded
     */
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

/**
 *  @fn static GstFlowReturn gst_vvas_xcompositor_aggregate_frames (GstVideoAggregator * vagg, GstBuffer * outbuf)
 *  @param [in] *vagg   - VideoAggregator instance.
 *  @param [in] *outbuf - output buffer to store the aggregated frame.
 *  @return GstFlowOK on Success.\n
 *          GstFlowError on Failure.
 *
 *  @ brief It is a callback function which aggregate the frames.
 *  @details It is a callback function that called from the GstVideoAggregator class
 *           when the data on all the sinkpads are ready. It internally calls the
 *           prepare_input_buffer , prepare_output_buffer and process/aggregate the frames.
 */
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
        if (!self->best_fit) {
          /* When Best Filt is enabled, zorder will not change after EOS */
          vvas_xcompositor_adjust_zorder_after_eos (self, sinkpad->index);
        }
        sinkpad->is_eos = true;
      }
    /** Fetch the currently prepared video frame of the pad
     *  that has to be aggregated into the current output frame.
     */
    prepared_frame = gst_video_aggregator_pad_get_prepared_frame (pad);

    if (!prepared_frame) {
      /* to pass and empty buffer in case we get eos on some of the pads */
      vvas_xcompositor_get_empty_buffer (self, sinkpad,
          &self->priv->inbufs[pad_id]);
    } else {
      self->priv->inbufs[pad_id] = prepared_frame->buffer;
    }

    /* prepare input buffer from the data on sinkpad */
    bret =
        vvas_xcompositor_prepare_input_buffer (self, sinkpad,
        &self->priv->inbufs[pad_id]);
    if (!bret)
      goto error;

    pad_id++;
  }
  /** Creating a multi scaler aligned buffer, as the default buffer allocated is
   *  unaligned to multiscaler if the downstream does not understand video meta
   */
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
    /* Prepare aligned output buffer in case of no video meta data */
    bret = vvas_xcompositor_prepare_output_buffer (self, aligned_buffer);
  } else {
    /* Prepare output buffer which is acquired from the pool */
    bret = vvas_xcompositor_prepare_output_buffer (self, outbuf);
  }
  if (!bret)
    goto error;
  /** Aggregate the frames using multi scaler kernel after
   *  preparing i/p and o/p buffers
   */
  bret = vvas_xcompositor_process (self);
  if (!bret)
    goto error;
  /* If copy is needed, copy the aligned buffer' s data to unaligned buffer */
  if (self->priv->need_copy) {
    GstVideoFrame new_frame, out_frame;
    /** aligned buffer pointer is assigned to
     *  self->priv->outbuf so using for a slow copy
     */
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

  vvas_xcompositor_free_vvas_video_frame (self);
  return GST_FLOW_OK;

error:
  fret = GST_FLOW_ERROR;
  vvas_xcompositor_free_vvas_video_frame (self);
  for (pad_id = 0; pad_id < self->num_request_pads; pad_id++)
    gst_buffer_unref (self->priv->inbufs[pad_id]);
  return fret;
}

/**
 *  @fn static void gst_vvas_xcompositor_get_property (GObject * object,
 *                                                     guint prop_id,
 *                                                     const GValue * value,
 *                                                     GParamSpec * pspec)
 *  @param [in] object  - Handle to GstVvasXCompositor typecasted to GObject
 *  @param [in] prop_id - Property ID as defined in GstVvasXCompositor properties enum
 *  @param [in] value   - GValue which holds property value returned by the function
 *  @param [in] pspec   - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief This API returns values of the property with property ID \p prop_id.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::get_property function pointer and
 *           this will be invoked when user requests to get properties of GstVvasXCompositor object.
 */
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 *  @fn static void gst_vvas_xcompositor_set_property (GObject * object,
 *                                                     guint prop_id,
 *                                                     const GValue * value,
 *                                                     GParamSpec * pspec)
 *  @param [in] object  - Handle to GstVvasXCompositor typecasted to GObject
 *  @param [in] prop_id - Property ID as defined in GstVvasXCompositor properties enum
 *  @param [in] value   - GValue which holds property value set by user
 *  @param [in] pspec   - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief This API stores values sent from the user in GstVvasXCompositor object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::set_property function pointer and
 *           this will be invoked when user sets properties on GstVvasXCompositor object. Based on property
 *           value type, corresponding g_value_get_xxx API will be called to get property value
 *           from GValue handle.
 */
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

/**
 *  @def static GstPad * gst_vvas_xcompositor_request_new_pad (GstElement * element,
 *  					                                       GstPadTemplate * templ,
 *  					                                       const gchar * req_name,
 *  					                                       const GstCaps * caps)
 *  @param [in] element 	- pointer to GstVvasXCompositor
 *  @param [in] templ 		- pointer to sink pad template
 *  @param [in] req_name 	- name of the pad to be create
 *  @param [in] caps 		- Capabilities of the data received on this pad
 *  @return Pointer to pad on SUCCESS\n
            NULL on FAILURE
 *  @brief This function is called by the framework when a new pad of type "request pad" is to be created.
 *         This request comes for sink pad. Once sink pad is created, corresponding src pad is also created.
 */
static GstPad *
gst_vvas_xcompositor_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstVvasXCompositorPad *newpad = NULL;
  GstPad *pad;
  GstVvasXCompositor *self = GST_VVAS_XCOMPOSITOR (element);

  /* Check for the maximum number of supported channels */
  if (self->num_request_pads > (MAX_CHANNELS - 1)) {
    GST_DEBUG_OBJECT (element, "Maximum num of pads supported is only 16");
    return NULL;
  }

  pad = (GstPad *)
      GST_ELEMENT_CLASS (parent_class)->request_new_pad (element,
      templ, req_name, caps);

  if (pad == NULL)
    goto could_not_create;

  /*Emit the child-added signal */
  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  newpad = (GstVvasXCompositorPad *) pad;

  /* Update the pad parameters */
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

/**
 *  @def static void gst_vvas_xcompositor_release_pad (GstElement * element, GstPad * pad)
 *  @param [in] element 	- pointer to GstVvasXCompositor
 *  @param [in] pad 		- pad to be released
 *  @return none
 *  @brief This function is called by the framework when a request pad is to be released.
 */
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
  /* Clear the pad parameters */
  gst_video_info_free (compositor_pad->in_vinfo);
  self->sinkpads = g_list_remove (self->sinkpads, compositor_pad);
  index = compositor_pad->index;
  GST_DEBUG_OBJECT (self, "releasing pad with index = %d", index);

  /* Emits the child-removed signal. */
  gst_child_proxy_child_removed (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_ELEMENT_CLASS (parent_class)->release_pad (element, pad);

  self->num_request_pads--;

}

/**
 *  @def static gboolean gst_vvas_xcompositor_start (GstAggregator * agg)
 *  @param [in] agg - pointer to GstAggregator.
 *  @return TRUE
 *  @brief This function is called by the framework when the element goes from READY to PAUSED.
 */
static gboolean
gst_vvas_xcompositor_start (GstAggregator * agg)
{
  return TRUE;
}

/**
 *  @def static gboolean gst_vvas_xcompositor_stop (GstAggregator * agg)
 *  @param [in] agg - pointer to GstAggregator.
 *  @return TRUE
 *  @brief This function is called by the framework when the element goes from PAUSED to READY.
 */
static gboolean
gst_vvas_xcompositor_stop (GstAggregator * agg)
{
  return TRUE;
}


/**
 *  @def static GObject * gst_vvas_xcompositor_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
 *                                                                             guint index)
 *  @param [in] child_proxy     - The parent object to get the child from.
 *  @param [in] index           - The child's position in the child list.
 *  @return the child object or NULL if not found.
 *  @brief This function fetches a child by its number.
 *  @details Fetches a child by its number.
 */
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

/**
 *  @def guint gst_vvas_xcompositor_child_proxy_get_children_count (GstChildProxy * child_proxy)
 *  @param [in] child_proxy -  the parent object to get the child from.
 *  @return the number of child objects
 *  @brief This function gets the number of child objects this parent contains.
 *  @details Gets the number of child objects this parent contains.
 */
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

/**
 *  @def static void gst_vvas_xcompositor_child_proxy_init (gpointer g_iface, gpointer iface_data)
 *  @param [in] g_iface     - The interface structure to initialize.
 *  @param [in] iface_data  - The interface_data supplied via the GInterfaceInfo structure.
 *  @return None
 *  @brief A callback function used by the type system to initialize a new interface.
 *  @details A callback function used by the type system to initialize a new interface.
 */
static void
gst_vvas_xcompositor_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  iface->get_child_by_index =
      gst_vvas_xcompositor_child_proxy_get_child_by_index;
  iface->get_children_count =
      gst_vvas_xcompositor_child_proxy_get_children_count;
}

/**
 *  @fn static gboolean vvas_xcompositor_init (GstPlugin * vvas_xcompositor)
 *  @param [in] vvas_xlookahead - Handle to vvas_xcompositor plugin
 *  @return TRUE if plugin initialized successfully
 *  @brief This is a callback function that will be called by the loader at startup to register the plugin
 *  @note It create a new element factory capable of instantiating objects of the type
 *        'GST_TYPE_VVAS_XCOMPOSITOR' and add the factory to plugin 'vvas_xcompositor'
 */
static gboolean
vvas_xcompositor_init (GstPlugin * vvas_xcompositor)
{
  return gst_element_register (vvas_xcompositor, "vvas_xcompositor",
      GST_RANK_PRIMARY, GST_TYPE_VVAS_XCOMPOSITOR);
}

/**
 *  @brief This macro is used to define the entry point and meta data of a plugin.
 *         This macro exports a plugin, so that it can be used by other applications
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xcompositor,
    "Xilinx VVAS Compositor plugin", vvas_xcompositor_init, VVAS_API_VERSION,
    "LGPL", "Xilinx VVAS SDK plugin", "http://xilinx.com/")
