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
#include <gst/vvas/gstvvasbufferpool.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <jansson.h>
#include "gstvvas_xmultisrc.h"
#include <vvas_core/vvas_device.h>
#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#include "gstvvas_xmultisrc.h"

/**
 *  @brief Setting minimum number of buffers in the pool
 */
#define MIN_POOL_BUFFERS 3

/**
 *  @brief Maximum number of allowed kernels
 */
int f_num = 0;
int fps = 0;

/**
 *  @brief Enable/disable profile
 */
#define PROFILING 1
struct timespec start, end;

pthread_mutex_t count_mutex;

/**
 *  @brief Defines a static GstDebugCategory global variable "gst_vvas_xmultisrc_debug"
 */
GST_DEBUG_CATEGORY_STATIC (gst_vvas_xmultisrc_debug);

/** @def GST_CAT_DEFAULT
 *  @brief Setting gst_vvas_xmultisrc_debug as default debug category for logging
 */
#define GST_CAT_DEFAULT gst_vvas_xmultisrc_debug

/**
 *  @brief Repository path, where kernel library
 */
#define REPO_PATH  "/usr/lib"
#define DEBUG

/**
 *  @brief Default value of the device index in case user has not provided
 */
#define DEFAULT_DEVICE_INDEX 0

/** @def ALIGN(size,align)
 *  @brief Macro to align the size of bytes into upper bounded multiples of align number of bytes
 */
#define ALIGN(size,align) ((((size) + (align) - 1) / align) * align)

enum
{
  /**Default */
  PROP_0,
  /**Property for xclbin location */
  PROP_XCLBIN_LOCATION,
  /**Property for config file */
  PROP_CONFIG_LOCATION,
  /**Property for dynamic configuration */
  PROP_DYNAMIC_CONFIG,
};

/**
 *  @brief Defines sink pad template
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGB, YUY2, NV16, GRAY8, BGRx, UYVY, BGR, NV12, BGRx, RGBx, RGBA, BGRA}")));

/**
 *  @brief Defines src pad template
 */
static GstStaticPadTemplate src_request_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{RGB, YUY2, NV16, GRAY8, BGRx, UYVY, BGR, NV12, BGRx, RGBx, RGBA, BGRA}")));

GType gst_vvas_xmultisrc_pad_get_type (void);

typedef struct _GstVvasXMSRCPad GstVvasXMSRCPad;
typedef struct _GstVvasXMSRCPadClass GstVvasXMSRCPadClass;

/** @struct _GstVvasXMSRCPad
 *  @brief  Structure storing pad information
 */
struct _GstVvasXMSRCPad
{
  /**Parent object */
  GstPad parent;
  /**Pad index */
  guint index;
  /**Buffer pool for allocating buffers for this pad */
  GstBufferPool *pool;
  /**Video information for the buffer on this pad */
  GstVideoInfo *out_vinfo;
};

struct _GstVvasXMSRCPadClass
{
  /**Parent class */
  GstPadClass parent;
};

/** @def GST_TYPE_VVAS_XMSRC_PAD
 *  @brief Macro to get GstVvasXMSRCPad object type
 */
#define GST_TYPE_VVAS_XMSRC_PAD \
  (gst_vvas_xmultisrc_pad_get_type())

/** @def GST_VVAS_XMSRC_PAD
 *  @brief Macro to typecast parent object to GstVvasXMSRCPad object
 */
#define GST_VVAS_XMSRC_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VVAS_XMSRC_PAD, \
             GstVvasXMSRCPad))

/** @def GST_VVAS_XMSRC_PAD_CLASS
 *  @brief Macro to typecast parent class object to GstVvasXMSRCPadClass object
 */
#define GST_VVAS_XMSRC_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VVAS_XMSRC_PAD, \
          GstVvasXMSRCPadClass))

/** @def GST_IS_VVAS_XMSRC_PAD
 *  @brief Macro to validate whether object is of GstVvasXMSRCPad type
 */
#define GST_IS_VVAS_XMSRC_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VVAS_XMSRC_PAD))

/** @def GST_IS_VVAS_XMSRC_PAD_CLASS
 *  @brief Macro to validate whether object class is of GstVvasXMSRCPadClass type
 */
#define GST_IS_VVAS_XMSRC_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VVAS_XMSRC_PAD))

/** @def GST_VVAS_XMSRC_PAD_CAST
 *  @brief Macro to type cast the object to GstVvasXMSRCPad type object
 */
#define GST_VVAS_XMSRC_PAD_CAST(obj) \
  ((GstVvasXMSRCPad *)(obj))

G_DEFINE_TYPE (GstVvasXMSRCPad, gst_vvas_xmultisrc_pad, GST_TYPE_PAD);

/** @def gst_vvas_xmultisrc_srcpad_at_index
 *  @brief Get the srcpad object from pad index.
 */
#define gst_vvas_xmultisrc_srcpad_at_index(self, idx) ((GstVvasXMSRCPad *)(g_list_nth ((self)->srcpads, idx))->data)

static void
gst_vvas_xmultisrc_pad_class_init (GstVvasXMSRCPadClass * klass)
{
  /* nothing */
}

static void
gst_vvas_xmultisrc_pad_init (GstVvasXMSRCPad * pad)
{
}

static void gst_vvas_xmultisrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vvas_xmultisrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_vvas_xmultisrc_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_vvas_xmultisrc_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static gboolean gst_vvas_xmultisrc_sink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GstStateChangeReturn gst_vvas_xmultisrc_change_state
    (GstElement * element, GstStateChange transition);
static GstCaps *gst_vvas_xmultisrc_transform_caps (GstVvasXMSRC * self,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstPad *gst_vvas_xmultisrc_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name_templ, const GstCaps * caps);
static void gst_vvas_xmultisrc_release_pad (GstElement * element, GstPad * pad);

/** @struct GstVvasXMSRCKernel
 *  @brief  Holds information about VVAS kernel
 */
typedef struct _GstVvasXMSRCKernel
{
  /** acceleration library to be loaded for VVAS kernel */
  gchar *lib_path;
  /** handle storing complete context of the kernel */
  VVASKernel *vvas_handle;
  /** File descriptor received after loading accel. library  */
  void *lib_fd;
  /** Pointer to function to initialize kernel */
  VVASKernelInit kernel_init_func;
  /** Pointer to function called for each buffer processing */
  VVASKernelStartFunc kernel_start_func;
  /** Pointer to a function to de-initialize kernel */
  VVASKernelDeInit kernel_deinit_func;
  /** Function pointer to check if kernel has finished processing or not */
  VVASKernelDoneFunc kernel_done_func;
} GstVvasXMSRCKernel;

/** @struct _GstVvasXMSRCPrivate
 *  @brief  Holds private members related GstVvasXMSRC plugin instance
 */
struct _GstVvasXMSRCPrivate
{
  /** Pointer to vinfo structure */
  GstVideoInfo *in_vinfo;
  /** Handle to device */
  vvasDeviceHandle dev_handle;
  /** Handle to Kernel */
  vvasKernelHandle kern_handle;
  /** UUID of xclbin */
  uuid_t xclbinId;
  /** Array of output buffers */
  GstBuffer *outbufs[MAX_CHANNELS];
  /** Buffer pool for input data */
  GstBufferPool *input_pool;
  /** Whether input buffer needs to be imported or not */
  gboolean validate_import;
  /** XRT command buffer */
  xrt_buffer *ert_cmd_buf;
  /** Pointer to input VVAS frame */
  VVASFrame *input;
  /** Pointer to output VVAS frames */
  VVASFrame *output[MAX_CHANNELS];
  /** Kernel information */
  GstVvasXMSRCKernel kernel;
  /** Dynamically changed kernel configuration */
  json_t *dyn_json_config;
  /** Dynamically changed kernel configuration for each kernel */
  json_t *dyn_kernel_config;
  /** Flag indicating kernel configuration is changed dynamically */
  gboolean dyn_config_changed;
};

/**
 *  @brief Redefining the gst_vvas_xmultisrc_parent_class to parent_class
 */
#define gst_vvas_xmultisrc_parent_class parent_class

/**
 *  @brief   A convenience macro for type implementations
 *  @details This macro declares a class initialization function, an instance initialization function,
 *           a static variable named t_n_parent_class pointing to the parent class, and adds private
 *           instance data to the type.
 */
G_DEFINE_TYPE_WITH_PRIVATE (GstVvasXMSRC, gst_vvas_xmultisrc, GST_TYPE_ELEMENT);

/**
 *  @brief  To get private member from the GstVvasXMSRC object
 */
#define GST_VVAS_XMSRC_PRIVATE(self) (GstVvasXMSRCPrivate *) (gst_vvas_xmultisrc_get_instance_private (self))

/**
 *  @fn static gboolean vvas_xmultisrc_register_prep_write_with_caps (GstVvasXMSRC * self,
 *                                                                    guint chan_id,
 *                                                                    GstCaps * in_caps,
 *                                                                    GstCaps * out_caps)
 *  @param [in] self     - Pointer to GstVvasXMSRC object
 *  @param [in] chan_id  - src pad index
 *  @param [in] in_caps  - capabilities on input pad
 *  @param [in] out_caps - capabilities on output pad
 *  @return TRUE is success
 *          FALSE in case of error
 *  @brief Populate vinfo structure from the input and output caps
 */
static gboolean
vvas_xmultisrc_register_prep_write_with_caps (GstVvasXMSRC * self,
    guint chan_id, GstCaps * in_caps, GstCaps * out_caps)
{
  GstVvasXMSRCPad *srcpad = NULL;
  GstVideoInfo in_vinfo = { 0, };

  /* Populate in_vinfo from in_caps */
  if (!gst_video_info_from_caps (&in_vinfo, in_caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from input caps");
    return FALSE;
  }

  srcpad = gst_vvas_xmultisrc_srcpad_at_index (self, chan_id);

  /* Populate out_vinfo from out_caps */
  if (!gst_video_info_from_caps (srcpad->out_vinfo, out_caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from output caps");
    return FALSE;
  }

  /* Activate output buffer pool */
  if (!gst_buffer_pool_set_active (srcpad->pool, TRUE)) {
    GST_ERROR_OBJECT (srcpad, "failed to activate pool");
    goto error;
  }

  return TRUE;

error:
  return FALSE;
}

/**
 *  @fn static inline VVASVideoFormat get_kernellib_format (GstVideoFormat gst_fmt)
 *  @param [in] gst_fmt - GstVideoFormat object
 *  @return VVASVideoFormat
 *  @brief Convert GstVideoFormat to VVASVideoFormat
 */
static inline VVASVideoFormat
get_kernellib_format (GstVideoFormat gst_fmt)
{
  switch (gst_fmt) {
    case GST_VIDEO_FORMAT_GRAY8:
      return VVAS_VFMT_Y8;
    case GST_VIDEO_FORMAT_NV12:
      return VVAS_VFMT_Y_UV8_420;
    case GST_VIDEO_FORMAT_BGR:
      return VVAS_VFMT_BGR8;
    case GST_VIDEO_FORMAT_RGB:
      return VVAS_VFMT_RGB8;
    case GST_VIDEO_FORMAT_YUY2:
      return VVAS_VFMT_YUYV8;
    case GST_VIDEO_FORMAT_r210:
      return VVAS_VFMT_RGBX10;
    case GST_VIDEO_FORMAT_v308:
      return VVAS_VFMT_YUV8;
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      return VVAS_VFMT_Y10;
    default:
      GST_ERROR ("Not supporting %s yet", gst_video_format_to_string (gst_fmt));
      return VVAS_VMFT_UNKNOWN;
  }
}

/**
 *  @fn static unsigned int vvas_xmultisrc_cal_stride (unsigned int width, GstVideoFormat fmt, gint align)
 *  @param [in] width  - Width of the video.
 *  @param [in] fmt  -  Format of the video.
 *  @param [in] align - Alignment value
 *  @return  on Success return stride\n
 *           on Failure return 0
 *  @brief  This API calculates video stride based on width and video format.
 */
static unsigned int
vvas_xmultisrc_cal_stride (unsigned int width, GstVideoFormat fmt, gint align)
{
  unsigned int stride;
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

  /* Stride in Bytes = (Width × Bytes per Pixel); */
  switch (fmt) {
    case GST_VIDEO_FORMAT_r210:
      stride = width * 4;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      stride = width * 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_GRAY8:
      stride = width * 1;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_v308:
    case GST_VIDEO_FORMAT_BGR:
      stride = width * 3;
      break;
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      /* 4 bytes per 3 pixels */
      stride = DIV_ROUND_UP (width * 4, 3);
      break;
    default:
      stride = 0;
  }

  stride = ALIGN (stride, align);

  return stride;
}

/**
 *  @fn static guint vvas_xmultisrc_get_padding_right (GstVvasXMSRC * self, GstVideoInfo * info, guint stride_align)
 *  @param [in] self  - Handle that holds the GstVvasXMSRC  instance.
 *  @param [in] info  -  Pointer that holds video information.
 *  @return  on Success return number of padding bytes\n
 *           on Failure return -1
 *  @brief  This API calculates number of padding bytes at right side based on video format and video stride.
 */
static guint
vvas_xmultisrc_get_padding_right (GstVvasXMSRC * self,
    GstVideoInfo * info, guint stride_align)
{
  guint padding_pixels = -1;
  guint plane_stride = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  guint padding_bytes = ALIGN (plane_stride, stride_align) - plane_stride;
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

/**
 *  @fn static gboolean vvas_xmultisrc_open (GstVvasXMSRC * self)
 *  @param [in] self - GstVvasXMSRC object
 *  @return TRUE if success
 *          FALSE if error
 *  @brief This function perform parsing kernel configuration file, open device, download xclbin,
 *         loads accelaration library.
 */
static gboolean
vvas_xmultisrc_open (GstVvasXMSRC * self)
{
  GstVvasXMSRCPrivate *priv = self->priv;
  json_t *root, *kernel, *value;
  gchar *lib_path = NULL;
  char *error;
  json_error_t jerror;
  VVASKernel *vvas_handle;
  char kernel_name[1024];
  bool shared_access = true;

  GST_DEBUG_OBJECT (self, "vvas MSRC open");

  root = json_load_file (self->config_file, JSON_DECODE_ANY, &jerror);
  if (!root) {
    GST_ERROR_OBJECT (self, "failed to load json file. reason %s", jerror.text);
    json_decref (root);
    goto error;
  }

  value = json_object_get (root, "element-mode");
  if (!json_is_string (value)) {
    GST_ERROR_OBJECT (self,
        "\"element-mode\" not set. Possible mode is transform mode)");
    goto error;
  }

  /* Check the element mode in which user wants to operate this plug-in */
  if (g_strcmp0 ("transform", json_string_value (value))) {
    GST_ERROR_OBJECT (self, "unsupported element-mode %s",
        json_string_value (value));
    goto error;
  }

  /* Open device with device index */
  if (!vvas_xrt_open_device (DEFAULT_DEVICE_INDEX, &priv->dev_handle)) {
    GST_ERROR_OBJECT (self, "failed to open device index %u",
        DEFAULT_DEVICE_INDEX);
    return FALSE;
  }

  value = json_object_get (root, "xclbin-location");
  if (json_is_string (value)) {
    self->xclbin_path = (gchar *) json_string_value (value);
    GST_INFO_OBJECT (self, "xclbin path %s", self->xclbin_path);

    /* Download xclbin on the device */
    if (vvas_xrt_download_xclbin (self->xclbin_path,
            priv->dev_handle, &priv->xclbinId)) {
      GST_ERROR_OBJECT (self, "failed to initialize XRT");
      goto error;
    }

  }

  value = json_object_get (root, "vvas-library-repo");
  if (!value) {
    GST_DEBUG_OBJECT (self,
        "library repo path does not exist.taking default %s", REPO_PATH);
    lib_path = g_strdup (REPO_PATH);
  } else {
    gchar *path = g_strdup (json_string_value (value));

    if (!g_str_has_suffix (path, "/")) {
      lib_path = g_strconcat (path, "/", NULL);
      g_free (path);
    } else {
      lib_path = path;
    }
  }

  kernel = json_object_get (root, "kernel");
  if (!json_is_object (kernel)) {
    GST_ERROR_OBJECT (self, "failed to find kernel object");
    goto error;
  }

  self->priv->kernel.vvas_handle =
      (VVASKernel *) calloc (1, sizeof (VVASKernel));
  vvas_handle = self->priv->kernel.vvas_handle;
  if (!vvas_handle) {
    GST_ERROR_OBJECT (self, "ERROR: failed to allocate vvas_handles memory\n");
    exit (EXIT_FAILURE);
  }

  /* Populate the kernel name */
  vvas_handle->name =
      (uint8_t *) g_strdup_printf ("libkrnl_%s", GST_ELEMENT_NAME (self));

  vvas_handle->dev_handle = (void *) priv->dev_handle;

  /* kernel name */
  value = json_object_get (kernel, "kernel-name");
  if (json_is_string (value)) {
    strcpy (kernel_name, json_string_value (value));
  } else {
    GST_INFO_OBJECT (self,
        "kernel name is not available, kernel lib is non-XRT based one");
  }

  /* kernel access mode */
  value = json_object_get (kernel, "kernel-access-mode");
  if (json_is_string (value)) {
    /* check what access type user has seleced, "shared" or "exclusive" */
    if (!strcmp (json_string_value (value), "shared")) {
      shared_access = true;
    } else if (!strcmp (json_string_value (value), "exclusive")) {
      shared_access = false;
    } else {
      GST_INFO_OBJECT (self,
          "Invalid kernel access mode, using default \"shared\" mode");
      shared_access = true;
    }
  } else {
    GST_INFO_OBJECT (self,
        "\"kernel-access-mode\" is not set, can be \"shared\" or \"exclusive\", \
          using default option \"shared\"");
    /* Chosing "shared" as the default mode */
    shared_access = true;
  }

  /* Open context for the kernel */
  if (vvas_xrt_open_context (priv->dev_handle, priv->xclbinId,
          &priv->kern_handle, kernel_name, shared_access)) {
    GST_ERROR_OBJECT (self, "failed to open XRT context ...");
    goto error;
  }

  vvas_handle->kern_handle = (void *) priv->kern_handle;

  /* library name */
  value = json_object_get (kernel, "library-name");
  if (!json_is_string (value)) {
    GST_ERROR_OBJECT (self, "library name is not of string type");
    goto error;
  }
  priv->kernel.lib_path =
      (gchar *) g_strconcat (lib_path, json_string_value (value), NULL);
  GST_DEBUG_OBJECT (self, "library path : %s", priv->kernel.lib_path);

  /* kernel config reading done */
  value = json_object_get (kernel, "config");
  if (json_is_object (value)) {
    vvas_handle->kernel_config = json_deep_copy (value);
    GST_DEBUG_OBJECT (self, "kernel config size = %lu",
        json_object_size (value));
  }
#ifdef XLNX_PCIe_PLATFORM
  value = json_object_get (kernel, "soft-kernel");
  if (value) {
    if (!json_is_string (value)) {
      GST_ERROR_OBJECT (self, "failed to find soft-kernel");
      GST_ERROR_OBJECT (self, "soft-kernel is not of string type");
      goto error;
    } else {
      GST_INFO_OBJECT (self, "Currently not handling soft-kernel");
      goto error;
    }
  }
#endif

  /* Load acceleration s/w library */
  priv->kernel.lib_fd = dlopen (priv->kernel.lib_path, RTLD_LAZY);
  if (!priv->kernel.lib_fd) {
    GST_ERROR_OBJECT (self, " unable to open shared library %s",
        priv->kernel.lib_path);
    goto error;
  }

  /* Clear any existing error */
  dlerror ();
  self->priv->kernel.kernel_start_func =
      (VVASKernelStartFunc) dlsym (priv->kernel.lib_fd, "xlnx_kernel_start");
  error = dlerror ();
  if (error != NULL) {
    GST_ERROR_OBJECT (self, "ERROR: %s", error);
    goto error;
  }
  self->priv->kernel.kernel_done_func =
      (VVASKernelDoneFunc) dlsym (priv->kernel.lib_fd, "xlnx_kernel_done");
  self->priv->kernel.kernel_init_func =
      (VVASKernelInit) dlsym (priv->kernel.lib_fd, "xlnx_kernel_init");
  self->priv->kernel.kernel_deinit_func =
      (VVASKernelInit) dlsym (priv->kernel.lib_fd, "xlnx_kernel_deinit");
  if (self->priv->kernel.kernel_init_func) {
    self->priv->kernel.kernel_init_func (self->priv->kernel.vvas_handle);
  }
#ifdef DEBUG
  GST_DEBUG_OBJECT (self, "Lib path ==> %s\n", priv->kernel.lib_path);
  GST_DEBUG_OBJECT (self, "kernel index => %d\n",
      priv->kernel.vvas_handle->cu_idx);
  GST_DEBUG_OBJECT (self, "\n---------\n");
#endif
  if (lib_path) {
    g_free (lib_path);
  }
  if (root) {
    json_decref (root);
  }

  return TRUE;
error:
  if (lib_path) {
    g_free (lib_path);
  }
  if (root) {
    json_decref (root);
  }
  return FALSE;
}

/**
 *  @fn static gboolean vvas_xmultisrc_allocate_internal_pool (GstVvasXMSRC * self)
 *  @param [in] self - GstVvasXMSRC
 *  @return Returns TRUE if pool and allocators are created successfully, else return FALSE
 *  @brief  Creates pool and allocator to allocate XRT buffers
 */
static gboolean
vvas_xmultisrc_allocate_internal_pool (GstVvasXMSRC * self)
{
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstAllocationParams alloc_params;
  GstCaps *caps = NULL;
  guint size = 0;
  VVASKernel *vvas_handle = self->priv->kernel.vvas_handle;

  /* Get input pad caps */
  caps = gst_pad_get_current_caps (self->sinkpad);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  /* Create a new buffer pool */
  pool = gst_video_buffer_pool_new ();
  GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

  /* Create VVAS Allocator to allocate XRT buffers */
  allocator = gst_vvas_allocator_new (DEFAULT_DEVICE_INDEX,
      TRUE, DEFAULT_MEM_BANK);
  gst_allocation_params_init (&alloc_params);

  /* Set pool configuration parameters */
  alloc_params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;

  config = gst_buffer_pool_get_config (pool);
  if (vvas_caps_get_sink_stride_align (vvas_handle) > 0 ||
      vvas_caps_get_sink_height_align (vvas_handle) > 0) {
    GstVideoAlignment video_align = { 0, };

    gst_video_alignment_reset (&video_align);
    video_align.padding_top = 0;
    video_align.padding_left = 0;
    video_align.padding_right = vvas_xmultisrc_get_padding_right (self, &info,
        vvas_caps_get_sink_stride_align (vvas_handle));
    video_align.padding_bottom = ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
        vvas_caps_get_sink_height_align (vvas_handle)) -
        GST_VIDEO_INFO_HEIGHT (&info);
    GST_LOG_OBJECT (self, "padding_right = %d padding_bottom = %d",
        video_align.padding_right, video_align.padding_bottom);
    gst_video_info_align (&info, &video_align);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, &video_align);
    size = GST_VIDEO_INFO_SIZE (&info);
  }

  GST_LOG_OBJECT (self, "allocated internal sink pool %p with size = %d", pool,
      size);
  gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (&info),
      3, 0);

  /* Set VVAS allocator in the pool */
  gst_buffer_pool_config_set_allocator (config, allocator, &alloc_params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Failed to set config on input pool");
    goto error;
  }

  /* If the input pool is already set during previous configuration, unref that pool */
  if (self->priv->input_pool)
    gst_object_unref (self->priv->input_pool);

  /* Set this pool as new input_pool */
  self->priv->input_pool = pool;

  GST_INFO_OBJECT (self, "allocated %" GST_PTR_FORMAT " pool",
      self->priv->input_pool);
  gst_caps_unref (caps);

  if (allocator)
    gst_object_unref (allocator);

  return TRUE;

error:
  gst_caps_unref (caps);
  return FALSE;
}

/**
 *  @fn static gboolean vvas_xmultisrc_write_input_registers (GstVvasXMSRC * self, GstBuffer ** inbuf)
 *  @param [in] self - Pointer to GstVvasXMSRC instance
 *  @param [in] inbuf - Pointer to GstVvasXMSRC instance
 *  @return Returns TRUE if buffer can be passed to kernel else return FALSE
 *  @brief This function ensures that input buffer is suitable to be passed to kernel
 *  @details  - This function checks if the memory is VVAS memory or dmabuf memory
 *            - Otherwise allocate new buffer from internal pool and copy data from input buffer into new VVAS buffer
 */
static gboolean
vvas_xmultisrc_write_input_registers (GstVvasXMSRC * self, GstBuffer ** inbuf)
{
  GstMemory *in_mem = NULL;
  GstVideoFrame in_vframe, own_vframe;
  guint64 phy_addr = -1;
  GstVideoMeta *vmeta = NULL;
  gboolean bret;
  guint plane_id;
  gboolean use_inpool = FALSE;
  GstBuffer *own_inbuf;
  GstFlowReturn fret;

  /* Get GstMemory object from the GstBuffer object */
  in_mem = gst_buffer_get_memory (*inbuf, 0);
  if (!in_mem) {
    GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
    goto error;
  }

  /* Check if this is VVAS memory and allocated on the same device
     as this kernel is running on so that memory copy can be avoided */
  if (gst_is_vvas_memory (in_mem)
      && gst_vvas_memory_can_avoid_copy (in_mem, DEFAULT_DEVICE_INDEX,
          DEFAULT_MEM_BANK)) {
    phy_addr = gst_vvas_allocator_get_paddr (in_mem);
    /* syncs data when XLNX_SYNC_TO_DEVICE flag is enabled */
    bret = gst_vvas_memory_sync_bo (in_mem);
    if (!bret)
      goto error;

  } else if (gst_is_dmabuf_memory (in_mem)) {
    /* This is dmabuf memory */
    vvasBOHandle bo = NULL;
    gint dma_fd = -1;

    dma_fd = gst_dmabuf_memory_get_fd (in_mem);
    if (dma_fd < 0) {
      GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
      goto error;
    }

    /* dmabuf but, not from xrt so import this buffer into XRT */
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
    /* Neither VVAS memory nor dmabuf memory. Allocate buffer
     * from internal pool */
    use_inpool = TRUE;
  }
  gst_memory_unref (in_mem);

  if (use_inpool) {
    if (!self->priv->input_pool) {
      /* Internal pool is not created, so create now */
      bret = vvas_xmultisrc_allocate_internal_pool (self);
      if (!bret)
        goto error;
    }

    if (!gst_buffer_pool_is_active (self->priv->input_pool))
      gst_buffer_pool_set_active (self->priv->input_pool, TRUE);

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

  if (phy_addr == (uint64_t) - 1) {
    in_mem = gst_buffer_get_memory (*inbuf, 0);
    if (in_mem == NULL) {
      GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
      goto error;
    }
    phy_addr = gst_vvas_allocator_get_paddr (in_mem);
    gst_memory_unref (in_mem);
  }
  GST_LOG_OBJECT (self, "input paddr %p", (void *) phy_addr);
  self->priv->input = (VVASFrame *) calloc (1, sizeof (VVASFrame));
  if (NULL == self->priv->input) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto error;
  }

  vmeta = gst_buffer_get_video_meta (*inbuf);
  if (vmeta == NULL) {
    GST_DEBUG_OBJECT (self,
        "video meta not present in buffer. taking information from vinfo");
    self->priv->input->props.width =
        GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo);
    self->priv->input->props.height =
        GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo);
    self->priv->input->props.stride =
        GST_VIDEO_INFO_PLANE_STRIDE (self->priv->in_vinfo, 0);
    self->priv->input->props.fmt =
        get_kernellib_format (GST_VIDEO_INFO_FORMAT (self->priv->in_vinfo));
    self->priv->input->n_planes =
        GST_VIDEO_INFO_N_PLANES (self->priv->in_vinfo);
    self->priv->input->app_priv = *inbuf;
    for (plane_id = 0;
        plane_id < GST_VIDEO_INFO_N_PLANES (self->priv->in_vinfo); plane_id++) {
      self->priv->input->paddr[plane_id] =
          phy_addr + GST_VIDEO_INFO_PLANE_OFFSET (self->priv->in_vinfo,
          plane_id);
      GST_LOG_OBJECT (self, "inbuf plane[%d] : paddr = %p, offset = %lu",
          plane_id, (void *) self->priv->input->paddr[plane_id],
          GST_VIDEO_INFO_PLANE_OFFSET (self->priv->in_vinfo, plane_id));
    }
  } else {
    self->priv->input->props.width = vmeta->width;
    self->priv->input->props.height = vmeta->height;
    self->priv->input->props.stride = *(vmeta->stride);
    self->priv->input->props.fmt = get_kernellib_format (vmeta->format);
    self->priv->input->n_planes =
        GST_VIDEO_INFO_N_PLANES (self->priv->in_vinfo);
    self->priv->input->app_priv = *inbuf;
    for (plane_id = 0;
        plane_id < GST_VIDEO_INFO_N_PLANES (self->priv->in_vinfo); plane_id++) {
      self->priv->input->paddr[plane_id] = phy_addr + vmeta->offset[plane_id];
      GST_LOG_OBJECT (self, "inbuf plane[%d] : paddr = %p, offset = %lu",
          plane_id, (void *) self->priv->input->paddr[plane_id],
          vmeta->offset[plane_id]);
    }
  }

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
 *  @fn static gboolean vvas_xmultisrc_write_output_registers (GstVvasXMSRC * self)
 *  @param [in] self - Pointer to GstVvasXMSRC object
 *  @return Returns TRUE if success else return FALSE
 *  @brief Requests buffer from pool for each output pad and populates the properties
 */
static gboolean
vvas_xmultisrc_write_output_registers (GstVvasXMSRC * self)
{
  guint chan_id, plane_id;
  GstMemory *mem = NULL;
  GstVvasXMSRCPad *srcpad = NULL;

  /* Iterate through all the output pads */
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    GstBuffer *outbuf = NULL;
    GstFlowReturn fret;
    GstVideoMeta *vmeta;
    guint64 phy_addr = -1;

    /* Get output pad object from index */
    srcpad = gst_vvas_xmultisrc_srcpad_at_index (self, chan_id);

    /* Get a free buffer from pool to store output */
    fret = gst_buffer_pool_acquire_buffer (srcpad->pool, &outbuf, NULL);
    if (fret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (srcpad, "failed to allocate buffer from pool %p",
          srcpad->pool);
      goto error;
    }
    GST_LOG_OBJECT (srcpad, "acquired buffer %p from pool", outbuf);

    self->priv->outbufs[chan_id] = outbuf;

    /* Get GstMemory object from GstBuffer object */
    mem = gst_buffer_get_memory (outbuf, 0);
    if (mem == NULL) {
      GST_ERROR_OBJECT (srcpad,
          "chan-%d : failed to get memory from output buffer", chan_id);
      goto error;
    }

    /* Check if the memory is VVAS memory or dmabuf memory */
    if (gst_is_vvas_memory (mem)
        && gst_vvas_memory_can_avoid_copy (mem, DEFAULT_DEVICE_INDEX,
            DEFAULT_MEM_BANK)) {
      phy_addr = gst_vvas_allocator_get_paddr (mem);

    } else if (gst_is_dmabuf_memory (mem)) {
      vvasBOHandle bo = NULL;
      gint dma_fd = -1;

      dma_fd = gst_dmabuf_memory_get_fd (mem);
      if (dma_fd < 0) {
        GST_ERROR_OBJECT (self, "failed to get DMABUF FD");
        goto error;
      }

      /* This is dmabuf but not from xrt, hence import this memory into xrt
       *  and get corresponding xrt bo */
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
    if (phy_addr == (uint64_t) - 1) {
      phy_addr = gst_vvas_allocator_get_paddr (mem);
      if (!phy_addr)
        goto error;
    }


    vmeta = gst_buffer_get_video_meta (outbuf);
    if (vmeta == NULL) {
      GST_ERROR_OBJECT (srcpad, "video meta not present in buffer");
      goto error;
    }

    self->priv->output[chan_id] = (VVASFrame *) calloc (1, sizeof (VVASFrame));
    if (NULL == self->priv->output[chan_id]) {
      GST_ERROR_OBJECT (self, "failed to allocate memory");
      goto error;
    }

    /* Populate the output frame properties */
    self->priv->output[chan_id]->props.width = vmeta->width;
    self->priv->output[chan_id]->props.height = vmeta->height;
    self->priv->output[chan_id]->props.stride = *(vmeta->stride);
    self->priv->output[chan_id]->props.fmt =
        get_kernellib_format (vmeta->format);
    self->priv->output[chan_id]->n_planes = vmeta->n_planes;
    for (plane_id = 0; plane_id < vmeta->n_planes; plane_id++) {
      self->priv->output[chan_id]->paddr[plane_id] =
          phy_addr + vmeta->offset[plane_id];
      GST_LOG_OBJECT (self, "Outbuf plane[%d] : paddr = %p, offset = %lu",
          plane_id, (void *) self->priv->output[chan_id]->paddr[plane_id],
          vmeta->offset[plane_id]);
    }

    /* app_priv is used by kernel for getting gstreamer specific data */
    self->priv->output[chan_id]->app_priv = outbuf;
    /* If below mentioned flag is set then when plugins/app request to map this
     * memory, sync will occur from device to host in PCIe platform
     * gst_xrt_memory_set_sync_flag (mem, XLNX_SYNC_FROM_DEVICE);
     */
    gst_memory_unref (mem);
  }

  return TRUE;

error:
  if (mem)
    gst_memory_unref (mem);

  return FALSE;
}

/**
 *  @fn static gboolean vvas_xmultisrc_process (GstVvasXMSRC * self)
 *  @param [in] self - Pointer to GstVvasXMSRC object
 *  @return Returns TRUE if success else return FALSE
 *  @brief  Triggers the processing of buffer by starting kernel and then waits
 *          waits for completion of the process.
 */
static gboolean
vvas_xmultisrc_process (GstVvasXMSRC * self)
{
  size_t ret, start = 0x1;
  json_t *value, *kernel;

  /* Update the kernel parameters if changed dynamically through
   * plug-in properties */
  if (self->priv->dyn_json_config && self->priv->dyn_config_changed == TRUE) {

    kernel = json_object_get (self->priv->dyn_json_config, "kernel");
    if (!json_is_object (kernel)) {
      GST_ERROR_OBJECT (self, "failed to find kernel object");
      goto error;
    }

    value = json_object_get (kernel, "config");
    if (json_is_object (value)) {
      if (self->priv->dyn_kernel_config) {
        json_decref (self->priv->dyn_kernel_config);
      }
      self->priv->dyn_kernel_config = json_deep_copy (value);
      GST_DEBUG_OBJECT (self, "kernel config size = %lu",
          json_object_size (value));
    }
#ifdef XLNX_PCIe_PLATFORM
    value = json_object_get (kernel, "soft-kernel");
    if (value) {
      if (!json_is_string (value)) {
        GST_ERROR_OBJECT (self, "failed to find soft-kernel");
        GST_ERROR_OBJECT (self, "soft-kernel is not of string type");
        goto error;
      } else {
        GST_INFO_OBJECT (self, "Currently not handling soft-kernel");
        goto error;
      }
    }
#endif
  }

  /* Update kernel configuration and start kernel */
  /* updating dynamic configuration of kernels */
  if (self->priv->dyn_config_changed == TRUE) {
    self->priv->kernel.vvas_handle->kernel_dyn_config =
        self->priv->dyn_kernel_config;
    GST_INFO_OBJECT (self, "Updated dynamic kernel configurations\n");
  }

  /* Start kernel for processing of current buffer */
  ret =
      self->priv->kernel.kernel_start_func (self->priv->kernel.vvas_handle,
      start, &(self->priv->input), self->priv->output);
  if (ret < 0) {
    GST_ERROR_OBJECT (self, "kernel start failed");
    goto error;
  }

  self->priv->dyn_config_changed = FALSE;

  /* Check if the processing of the input buffer is completed or not */
  ret = self->priv->kernel.kernel_done_func (self->priv->kernel.vvas_handle);
  if (ret < 0) {
    GST_ERROR_OBJECT (self, "kernel done failed");
    goto error;
  }
  return TRUE;

error:
  if (self->priv->dyn_json_config) {
    json_decref (self->priv->dyn_json_config);
  }

  self->priv->dyn_config_changed = FALSE;
  return FALSE;
}

/**
 *  @fn static void vvas_xmultisrc_close (GstVvasXMSRC * self)
 *  @param [in] self - Pointer to GstVvasXMSRC object
 *  @return Returns None
 *  @brief  Function to perform cleanup when plug-in state changes from READY to NULL.
 */
static void
vvas_xmultisrc_close (GstVvasXMSRC * self)
{
  GstVvasXMSRCPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "Closing");

  if (self->priv->kernel.kernel_deinit_func)
    self->priv->kernel.kernel_deinit_func (self->priv->kernel.vvas_handle);
  if (priv->kernel.lib_fd)
    dlclose (priv->kernel.lib_fd);

  vvas_xrt_close_context (priv->kern_handle);
  vvas_xrt_close_device (priv->dev_handle);
}

/**
 *  @fn static void gst_vvas_xmultisrc_finalize (GObject * object)
 *  @param [In] object - Handle to GstVvasXMSRC instance typecasted as GstObject
 *  @return None
 *  @brief   This API will be called during GstVvasXMSRC object's destruction phase.
 *           Close references to devices and free memories if any
 *  @note After this API GstVvasXMSRC object \p obj will be destroyed completely.
 *        So free all internal memories held by current object
 *
 */
static void
gst_vvas_xmultisrc_finalize (GObject * object)
{
  GstVvasXMSRC *self = GST_VVAS_XMSRC (object);

  g_hash_table_unref (self->pad_indexes);
  gst_video_info_free (self->priv->in_vinfo);
  g_free (self->config_file);
  g_free (self->dyn_config);

  /* If pool for input buffers is created, release it */
  if (self->priv->input_pool)
    gst_object_unref (self->priv->input_pool);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 *  @fn static void gst_vvas_xmultisrc_class_init (GstVvasXMSRCClass * klass)
 *  @param [in]klass  - Pointer to GstVvasXMSRCClass object
 *  @return None
 *  @brief   Add properties and signals of GstVvasXMSRC to parent GObjectClass and overrides
 *           function pointers present in itself and/or its parent class structures
 *  @details This function publishes properties those can be set/get from application on GstVvasXMSRC object. While
 *           publishing a property it also declares type, range of acceptable values, default value,
 *           readability/writability and in which GStreamer state a property can be changed.
 */
static void
gst_vvas_xmultisrc_class_init (GstVvasXMSRCClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xmultisrc_debug, "vvas_xmultisrc",
      0, "Xilinx's MSRC plugin");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xmultisrc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xmultisrc_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_vvas_xmultisrc_finalize);

  /* Set the detailed information for a GstElementClass */
  gst_element_class_set_details_simple (gstelement_class,
      "Xilinx MSRC plugin",
      "Generic MSRC xrt plugin for vitis kernels",
      "Generic MSRC plugin using XRT", "Xilinx Inc <www.xilinx.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &src_request_template);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vvas_xmultisrc_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_vvas_xmultisrc_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_vvas_xmultisrc_release_pad);

  g_object_class_install_property (gobject_class, PROP_CONFIG_LOCATION,
      g_param_spec_string ("kconfig", "config file location",
          "Location of the kernel config to program devices", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));
  g_object_class_install_property (gobject_class, PROP_DYNAMIC_CONFIG,
      g_param_spec_string ("dynamic-config",
          "Kernel's dynamic json config string",
          "String contains dynamic json configuration of kernel", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

}

/**
 *  @fn static void gst_vvas_xmultisrc_init (GstVvasXMSRC * self)
 *  @param [in] self - Handle to GstVvasXMSRC instance
 *  @return None
 *  @brief   Initilialize GstVvasXMSRC member variables to default and does one time object/memory
 *           allocations in object's lifecycle
 *  @details Overrides GstVvasXMSRC object's base class function pointers so that GstVvasXMSRC APIs will be invoked.
 *           Ex: Chain function, Event function, Query function etc.
 */
static void
gst_vvas_xmultisrc_init (GstVvasXMSRC * self)
{
  self->priv = GST_VVAS_XMSRC_PRIVATE (self);

  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xmultisrc_sink_event));
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xmultisrc_chain));
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xmultisrc_sink_query));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->num_request_pads = 0;
  self->pad_indexes = g_hash_table_new (NULL, NULL);
  self->srcpads = NULL;
  self->priv->in_vinfo = gst_video_info_new ();
  gst_video_info_init (self->priv->in_vinfo);

}

/**
 *  @fn static GstPad * gst_vvas_xmultisrc_request_new_pad (GstElement * element,
                                                             GstPadTemplate * templ,
                                                             const gchar * name_templ,
                                                             const GstCaps * caps)
 *  @param [in] element    - pointer to GstVvasXMSRC
 *  @param [in] templ      - pointer to src pad template
 *  @param [in] name_templ - name of the pad to be create
 *  @param [in] caps       - Capabilities of the data received on this pad
 *  @return pointer to pad if pad is created successfully
            NULL in case pad could not be created
 *  @brief  This function is called by the framework when a new pad of type "request pad"
 *          is to be created. This request comes for src pad.
 */
static GstPad *
gst_vvas_xmultisrc_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name_templ, const GstCaps * caps)
{
  GstVvasXMSRC *self = GST_VVAS_XMSRC (element);
  gchar *name = NULL;
  GstPad *srcpad;
  guint index = 0;

  GST_DEBUG_OBJECT (self, "requesting pad");


  GST_OBJECT_LOCK (self);

  if (GST_STATE (self) > GST_STATE_NULL) {
    GST_ERROR_OBJECT (self, "adding pads is supported only when state is NULL");
    return NULL;
  }

  /* Check if number of pads requested do not exceed the MAX supported
   * number of pads */
  if (self->num_request_pads == MAX_CHANNELS) {
    GST_ERROR_OBJECT (self, "reached maximum supported channels");
    GST_OBJECT_UNLOCK (self);
    return NULL;
  }

  if (name_templ && sscanf (name_templ, "src_%u", &index) == 1) {
    GST_LOG_OBJECT (element, "name: %s (index %d)", name_templ, index);

    /* Check if pad name is unique or not */
    if (g_hash_table_contains (self->pad_indexes, GUINT_TO_POINTER (index))) {
      GST_ERROR_OBJECT (element, "pad name %s is not unique", name_templ);
      GST_OBJECT_UNLOCK (self);
      return NULL;
    }
  } else {
    /* Pad name specified is not in the expected format */
    if (name_templ) {
      GST_ERROR_OBJECT (element, "incorrect padname : %s", name_templ);
      GST_OBJECT_UNLOCK (self);
      return NULL;
    }
  }

  g_hash_table_insert (self->pad_indexes, GUINT_TO_POINTER (index), NULL);

  name = g_strdup_printf ("src_%u", index);

  /* Create pad */
  srcpad = GST_PAD_CAST (g_object_new (GST_TYPE_VVAS_XMSRC_PAD,
          "name", name, "direction", templ->direction, "template", templ,
          NULL));
  GST_VVAS_XMSRC_PAD_CAST (srcpad)->index = index;
  g_free (name);

  /* Create Video info object and initialize for this pad */
  GST_VVAS_XMSRC_PAD_CAST (srcpad)->out_vinfo = gst_video_info_new ();
  gst_video_info_init (GST_VVAS_XMSRC_PAD_CAST (srcpad)->out_vinfo);

  /* Add the newly created pad in srcpad list */
  self->srcpads = g_list_append (self->srcpads,
      GST_VVAS_XMSRC_PAD_CAST (srcpad));
  self->num_request_pads++;

  GST_OBJECT_UNLOCK (self);

  /* Add pad to this plug-in */
  gst_element_add_pad (GST_ELEMENT_CAST (self), srcpad);

  return srcpad;
}

/**
 *  @fn static void gst_vvas_xmultisrc_release_pad (GstElement * element, GstPad * pad)
 *  @param [in] element - pointer to GstVvasXMSRC
 *  @param [in] pad     - pointer to pad
 *  @return None
 *  @brief This function is called by the framework when a pad is released. Cleanup related to pad is performed here.
 */
static void
gst_vvas_xmultisrc_release_pad (GstElement * element, GstPad * pad)
{
  GstVvasXMSRC *self;
  guint index;
  GList *lsrc = NULL;

  self = GST_VVAS_XMSRC (element);

  GST_OBJECT_LOCK (self);

  if (GST_STATE (self) > GST_STATE_NULL) {
    GST_ERROR_OBJECT (self,
        "Releasing of pads is supported only when state is NULL");
    return;
  }

  /* Find the pad in the list */
  lsrc = g_list_find (self->srcpads, GST_VVAS_XMSRC_PAD_CAST (pad));
  if (!lsrc) {
    GST_ERROR_OBJECT (self, "could not find pad to release");
    return;
  }

  gst_video_info_free (GST_VVAS_XMSRC_PAD_CAST (pad)->out_vinfo);

  self->srcpads = g_list_remove (self->srcpads, GST_VVAS_XMSRC_PAD_CAST (pad));
  index = GST_VVAS_XMSRC_PAD_CAST (pad)->index;
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

/**
 *  @fn static void gst_vvas_xmultisrc_set_property (GObject * object,
 *                                                   guint prop_id,
 *                                                   const GValue * value,
 *                                                   GParamSpec * pspec)
 *  @param [in] element - pointer to GstVvasXMSRC
 *  @param [in] pad     - pointer to pad
 *  @return None
 *  @brief This function is called by the framework when a pad is released. Cleanup related to pad is performed here.
 */
static void
gst_vvas_xmultisrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvasXMSRC *self = GST_VVAS_XMSRC (object);

  switch (prop_id) {
    case PROP_CONFIG_LOCATION:
      if (GST_STATE (self) != GST_STATE_NULL) {
        g_warning
            ("can't set config_file path when instance is NOT in NULL state");
        return;
      }
      self->config_file = g_value_dup_string (value);
      break;
    case PROP_DYNAMIC_CONFIG:
      self->dyn_config = g_value_dup_string (value);
      if (self->priv->dyn_json_config) {
        json_decref (self->priv->dyn_json_config);
      }

      self->priv->dyn_json_config =
          json_loads (self->dyn_config, JSON_DECODE_ANY, NULL);
      if (!self->priv->dyn_json_config) {
        g_warning
            ("dynamic config json string is incorrect. Unable to update dynamic configurations");
        return;
      }

      GST_INFO_OBJECT (self, "kernel dynamic parameters are %s\n",
          self->dyn_config);
      self->priv->dyn_config_changed = TRUE;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 *  @fn static void gst_vvas_xmultisrc_get_property (GObject * object,
 *                                                   guint prop_id,
 *                                                   GValue * value,
 *                                                   GParamSpec * pspec)
 *  @param [in] object  - GstVvasXMSRC typecasted to GObject
 *  @param [in] prop_id - ID of the property
 *  @param [out] value  - GValue which holds property value set by user
 *  @param [in] pspec   - Metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief   This API stores values from the GstVvasXMSRC object members into the value for user.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::get_property function pointer and
 *           this will be invoked when developer gets properties on GstVvasXMultiCrop object. Based on property value
 *           type, corresponding g_value_set_xxx API will be called to set property value to GValue handle.
 */
static void
gst_vvas_xmultisrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvasXMSRC *self = GST_VVAS_XMSRC (object);

  switch (prop_id) {
    case PROP_CONFIG_LOCATION:
      g_value_set_string (value, self->config_file);
      break;
    case PROP_DYNAMIC_CONFIG:
      g_value_set_string (value, self->dyn_config);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 *  @fn static void vvas_xmultisrc_deinit (GstVvasXMSRC * self)
 *  @param [in] self - Pointer to GstVvasXMSRC object
 *  @return Returns None
 *  @brief  Function to perform cleanup when plug-in state changes from READY to NULL.
 */
static void
vvas_xmultisrc_deinit (GstVvasXMSRC * self)
{
  guint idx = 0;
  GstVvasXMSRCPad *srcpad = NULL;

  for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
    srcpad = gst_vvas_xmultisrc_srcpad_at_index (self, idx);
    gst_buffer_pool_set_active (srcpad->pool, FALSE);
    gst_object_unref (srcpad->pool);
  }

  if (self->priv->kernel.vvas_handle->kernel_config)
    json_decref (self->priv->kernel.vvas_handle->kernel_config);
  if (self->priv->kernel.vvas_handle->kernel_dyn_config)
    json_decref (self->priv->kernel.vvas_handle->kernel_dyn_config);
  if (self->priv->kernel.vvas_handle) {
    /* De-allocate the name */
    if (self->priv->kernel.vvas_handle->name) {
      g_free (self->priv->kernel.vvas_handle->name);
    }

    free (self->priv->kernel.vvas_handle);
  }
  free (self->priv->kernel.lib_path);

  if (self->priv->dyn_json_config) {
    json_decref (self->priv->dyn_json_config);
  }
  self->num_request_pads = 0;
}

/**
 *  @fn static GstStateChangeReturn gst_vvas_xmultisrc_change_state (GstElement * element, GstStateChange transition)
 *  @param [in] element    - Handle to GstVvasXMSRC typecasted to GstElement.
 *  @param [in] transition - The requested state transition.
 *  @return Status of the state transition.
 *  @brief   This API will be invoked whenever the pipeline is going into a state transition and in this function the
 *           element can initialize any sort of specific data needed by the element.
 *  @details This API is registered with GstElementClass by overriding GstElementClass::change_state function pointer and
 *           this will be invoked whenever the pipeline is going into a state transition.
 */
static GstStateChangeReturn
gst_vvas_xmultisrc_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVvasXMSRC *self = GST_VVAS_XMSRC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!vvas_xmultisrc_open (self))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      vvas_xmultisrc_close (self);
      vvas_xmultisrc_deinit (self);
      break;
    default:
      break;
  }

  return ret;
}

/**
 *  @fn static GstCaps * gst_vvas_xmultisrc_fixate_caps (GstVvasXMSRC * self,
 *                                                       GstPadDirection direction,
 *                                                       GstCaps * caps,
 *                                                       GstCaps * othercaps)
 *  @param [in] self        - Handle to GstVvasXMSRC typecasted to GstBaseTransform.
 *  @param [in] direction   - Direction of the pad (src or sink)
 *  @param [in] caps        - Given caps
 *  @param [in] othercaps   - Caps to fixate
 *  @return  Fixated version of "othercaps"
 *  @brief   Given the pad in this direction and the given caps, fixate the caps on the other pad.
 *  @details The function takes ownership of othercaps and returns a fixated version of othercaps. othercaps
 *           is not guaranteed to be writable.
 */
static GstCaps *
gst_vvas_xmultisrc_fixate_caps (GstVvasXMSRC * self,
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

/**
 *  @fn static GstCaps * gst_vvas_xmultisrc_transform_caps (GstVvasXMSRC * self,
 *                                                          GstPadDirection direction,
 *                                                          GstCaps * caps,
 *                                                          GstCaps * filter)
 *  @param [in] self        - Pointer to GstVvasXMSRC object.
 *  @param [in] direction   - Direction of the pad (src or sink)
 *  @param [in] caps        - Given caps
 *  @param [in] filter      - Caps to filter out, when given
 *  @return transformed caps
 *  @brief  Given the pad in this direction and the given caps, this function will find what caps are allowed
 *          on the other pad in this element.
 */
static GstCaps *
gst_vvas_xmultisrc_transform_caps (GstVvasXMSRC * self,
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

/**
 *  @fn static GstCaps * gst_vvas_xmultisrc_find_transform(GstVvasXMSRC * self,
 *                                                         GstPad * pad,
 *                                                         GstPad * otherpad,
 *                                                         GstCaps * caps)
 *  @param [in] self      - Pointer to GstVvasXMSRC object.
 *  @param [in] pad       - Pad with known caps.
 *  @param [in] otherpad  - Pad for which transform caps needed.
 *  @param [in] caps      - Caps for known pad.
 *  @return Found transform caps.
 *  @brief  This function finds out the fixated transform caps for the otherpad.
 *
 */
static GstCaps *
gst_vvas_xmultisrc_find_transform (GstVvasXMSRC * self, GstPad * pad,
    GstPad * otherpad, GstCaps * caps)
{
  GstPad *otherpeer;
  GstCaps *othercaps;
  gboolean is_fixed;

  /* caps must be fixed here, this is a programming error if it's not */
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);

  otherpeer = gst_pad_get_peer (otherpad);

  /* see how we can transform the input caps. We need to do this even for
   * passthrough because it might be possible that this element cannot support
   * passthrough at all. */
  othercaps = gst_vvas_xmultisrc_transform_caps (self,
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
      gst_vvas_xmultisrc_fixate_caps (self, GST_PAD_DIRECTION (pad), caps,
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
 *  @fn static gboolean vvas_xmultisrc_decide_allocation (GstVvasXMSRC * self,
 * 							  GstVvasXMSRCPad * srcpad,
 *                                                        GstQuery * query,
 *                                                        GstCaps * outcaps)
 *  @param [in] self  	- Pointer to GstVvasXMSRC instance.
 *  @param [in] srcpad  - Pointer to GstVvasXMSRCPad object for which allocation has to be decided.
 *  @param [in] query   - Response for the allocation query.
 *  @param [in] outcaps - Currently set caps on the srcpad
 *  @param [in]
 *  @return  On Success returns TRUE
 *           On Failure returns FALSE
 *  @brief   This function will decide allocation strategy based on the preference from downstream element.
 *  @details The proposed query will be parsed through, verified if the proposed pool is VVAS and alignments
 *           are quoted. Otherwise it will be discarded and new pool and allocator will be created.
 */
static gboolean
vvas_xmultisrc_decide_allocation (GstVvasXMSRC * self, GstVvasXMSRCPad * srcpad,
    GstQuery * query, GstCaps * outcaps)
{
  GstAllocator *allocator = NULL;
  GstVideoInfo info;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  guint size, min, max;
  gboolean update_allocator, update_pool, bret;
  GstStructure *config = NULL;
  VVASKernel *vvas_handle = self->priv->kernel.vvas_handle;

  if (outcaps && !gst_video_info_from_caps (&info, outcaps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from outcaps");
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
    /* Downstream element has not proposed any allocator */
    allocator = NULL;
    update_allocator = FALSE;
    gst_allocation_params_init (&params);
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    /* Get pool parameters at the index 0 */
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
    if (min == 0)
      min = 3;
  } else {
    /* Downstream element has not suggested any pool */
    pool = NULL;
    min = 3;
    max = 0;
    size = info.size;
    update_pool = FALSE;
  }
  if (pool) {

    GstVideoAlignment video_align = { 0, };

    config = gst_buffer_pool_get_config (pool);

    GST_DEBUG_OBJECT (srcpad, "got pool");
    if ((vvas_caps_get_src_stride_align (vvas_handle) > 0
            || vvas_caps_get_src_height_align (vvas_handle) > 0)
        && gst_buffer_pool_config_get_video_alignment (config,
            &video_align) == TRUE) {

      guint padded_width = 0;
      guint padded_height = 0;
      guint kernel_stride;
      guint kernel_req_elevation;
      guint downstream_stride;
      guint downstream_elevation;

      printf ("stride align %d\n",
          vvas_caps_get_src_stride_align (vvas_handle));
      kernel_stride =
          vvas_xmultisrc_cal_stride (GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_FORMAT (&info),
          vvas_caps_get_src_stride_align (vvas_handle));

      padded_width =
          info.width + video_align.padding_right + video_align.padding_left;
      downstream_stride =
          vvas_xmultisrc_cal_stride (padded_width,
          GST_VIDEO_INFO_FORMAT (&info), 1);
      kernel_req_elevation = ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
          vvas_caps_get_src_height_align (vvas_handle));
      padded_height =
          info.height + video_align.padding_top + video_align.padding_bottom;
      downstream_elevation = ALIGN (padded_height, 1);


      GST_DEBUG_OBJECT (srcpad, "downstream stride = %d, kernel stride = %d",
          downstream_stride, kernel_stride);
      GST_DEBUG_OBJECT (srcpad,
          "downstream elevation = %d, kernel elevation = %d",
          downstream_elevation, kernel_req_elevation);
      if (kernel_stride != downstream_stride
          || kernel_req_elevation != downstream_elevation) {
        /* Alignment for kernel and downstream not matching
         * Go for new pool */
        GST_INFO_OBJECT (srcpad,
            "stride requirement for kernel and downstream not matching");

        gst_query_remove_nth_allocation_pool (query, 0);
        gst_object_unref (pool);
        pool = NULL;
        min = 3;
        max = 0;
        update_pool = FALSE;
      } else {
        GST_DEBUG_OBJECT (srcpad,
            "Alignment requirement for kernel and downstream are matching");
      }
    } else {
      GST_INFO_OBJECT (srcpad, "kernel do not have any alignment requirement");
    }

    gst_structure_free (config);
    config = NULL;
  }
#ifdef XLNX_EMBEDDED_PLATFORM
  /* TODO: Currently Kms buffer are not supported in PCIe platform */
  if (pool) {
    GstStructure *config = gst_buffer_pool_get_config (pool);

    if (gst_buffer_pool_config_has_option (config,
            "GstBufferPoolOptionKMSPrimeExport")) {
      /* kmssink exports dma buffer which can be used by this plug-in
       * so continue using same pool and allocator */
      gst_structure_free (config);
      goto next;
    }

    gst_structure_free (config);
    gst_object_unref (pool);
    pool = NULL;
    GST_DEBUG_OBJECT (srcpad, "pool deos not have the KMSPrimeExport option, \
        unref the pool and create vvas allocator");
  }
#endif

  /* If allocator is not VVAS allocator or allocatore is not on
   *  the same device as kernel  managed by this plug-in, then release this
   * allocator */
  if (allocator && (!GST_IS_VVAS_ALLOCATOR (allocator) ||
          gst_vvas_allocator_get_device_idx (allocator) !=
          DEFAULT_DEVICE_INDEX)) {
    GST_DEBUG_OBJECT (srcpad, "replace %" GST_PTR_FORMAT " to xrt allocator",
        allocator);
    gst_object_unref (allocator);
    gst_allocation_params_init (&params);
    allocator = NULL;
  } else {
    gst_allocation_params_init (&params);
    allocator = NULL;
  }

  if (!allocator) {
    /* Create VVAS allocator to allocate XRT buffers */
    allocator = gst_vvas_allocator_new (DEFAULT_DEVICE_INDEX,
        TRUE, DEFAULT_MEM_BANK);
    params.flags = GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS;
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

  if (pool == NULL) {
    GstVideoAlignment video_align = { 0, };
    gst_video_alignment_reset (&video_align);

    GST_DEBUG_OBJECT (srcpad, "no pool, making new pool");
    if (vvas_caps_get_src_stride_align (vvas_handle) > 0
        || vvas_caps_get_src_height_align (vvas_handle) > 0) {

      pool =
          gst_vvas_buffer_pool_new (vvas_caps_get_src_stride_align
          (vvas_handle), vvas_caps_get_src_height_align (vvas_handle));
      GST_INFO_OBJECT (srcpad, "created new pool %p %" GST_PTR_FORMAT, pool,
          pool);
      GST_INFO_OBJECT (srcpad, "kernel stride = %d",
          vvas_xmultisrc_cal_stride (GST_VIDEO_INFO_WIDTH (&info),
              GST_VIDEO_INFO_FORMAT (&info),
              vvas_caps_get_src_stride_align (vvas_handle)));
      config = gst_buffer_pool_get_config (pool);
      video_align.padding_top = 0;
      video_align.padding_left = 0;
      video_align.padding_right = vvas_xmultisrc_get_padding_right (self, &info,
          vvas_caps_get_src_stride_align (vvas_handle));
      video_align.padding_bottom = ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
          vvas_caps_get_src_height_align (vvas_handle)) -
          GST_VIDEO_INFO_HEIGHT (&info);

      gst_video_info_align (&info, &video_align);

      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
      gst_buffer_pool_config_set_allocator (config, allocator, &params);
      gst_buffer_pool_config_set_video_alignment (config, &video_align);
      size = GST_VIDEO_INFO_SIZE (&info);
    } else {
      GST_INFO_OBJECT (srcpad,
          "kernel do not have any stride alignment requirement");
      pool = gst_video_buffer_pool_new ();
    }
  }

  /* Configure buffer pool */
  if (config == NULL)
    config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_set_allocator (config, allocator, &params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  bret = gst_buffer_pool_set_config (pool, config);
  if (!bret) {
    GST_ERROR_OBJECT (srcpad, "failed configure pool");
    goto error;
  }

  if (allocator)
    gst_object_unref (allocator);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

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
 *  @fn static gboolean gst_vvas_xmultisrc_sink_setcaps (GstVvasXMSRC * self, GstPad * sinkpad, GstCaps * in_caps)
 *  @param [in] self    - Pointer to GstVvasXMSRC.
 *  @param [in] sinkpad - Src pad object for which allocation has to be decided
 *  @param [in] in_caps - New caps on the input pad.
 *  @return  On Success returns TRUE
 *           On Failure returns FALSE
 *  @brief   Everytime new caps are set on the sink pad, this function will be called.
 *  @details This function will find the caps for every source pad and update corresponding video info.
 */
static gboolean
gst_vvas_xmultisrc_sink_setcaps (GstVvasXMSRC * self, GstPad * sinkpad,
    GstCaps * in_caps)
{
  GstCaps *outcaps = NULL, *prev_incaps = NULL, *prev_outcaps = NULL;
  gboolean bret = TRUE;
  guint idx = 0;
  GstVvasXMSRCPad *srcpad = NULL;
  GstCaps *incaps = gst_caps_copy (in_caps);

  GST_DEBUG_OBJECT (self, "have new sink caps %p %" GST_PTR_FORMAT, incaps,
      incaps);

  /* store sinkpad info */
  if (!gst_video_info_from_caps (self->priv->in_vinfo, in_caps)) {
    GST_ERROR_OBJECT (self, "failed to get video info from input caps");
    return FALSE;
  }

  prev_incaps = gst_pad_get_current_caps (self->sinkpad);

  /* Loop through all output pads */
  for (idx = 0; idx < g_list_length (self->srcpads); idx++) {
    srcpad = gst_vvas_xmultisrc_srcpad_at_index (self, idx);

    /* find best possible caps for the other pad */
    outcaps =
        gst_vvas_xmultisrc_find_transform (self, sinkpad,
        GST_PAD_CAST (srcpad), incaps);
    if (!outcaps || gst_caps_is_empty (outcaps))
      goto no_transform_possible;

    prev_outcaps = gst_pad_get_current_caps (GST_PAD_CAST (srcpad));

    /* Check if previous caps are same as new caps */
    bret = prev_incaps && prev_outcaps
        && gst_caps_is_equal (prev_incaps, incaps)
        && gst_caps_is_equal (prev_outcaps, outcaps);

    if (bret) {
      GST_DEBUG_OBJECT (self,
          "New caps equal to old ones: %" GST_PTR_FORMAT " -> %" GST_PTR_FORMAT,
          incaps, outcaps);
    } else {
      GstQuery *query = NULL;

      if (!prev_outcaps || !gst_caps_is_equal (outcaps, prev_outcaps)) {
        /* let downstream know about our caps */
        bret = gst_pad_set_caps (GST_PAD_CAST (srcpad), outcaps);
        if (!bret)
          goto failed_configure;
      }

      GST_DEBUG_OBJECT (self, "doing allocation query");
      query = gst_query_new_allocation (outcaps, TRUE);
      if (!gst_pad_peer_query (GST_PAD (srcpad), query)) {
        /* not a problem, just debug a little */
        GST_DEBUG_OBJECT (self, "peer ALLOCATION query failed");
      }

      /* Decide pool/Allocator for this output pad */
      bret = vvas_xmultisrc_decide_allocation (self, srcpad, query, outcaps);
      if (!bret)
        goto failed_configure;

      gst_query_unref (query);

      /* Populate vinfo for the output pad */
      bret =
          vvas_xmultisrc_register_prep_write_with_caps (self, idx, incaps,
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

done:
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
    goto done;
  }
failed_configure:
  {
    GST_ERROR_OBJECT (self, "FAILED to configure incaps %" GST_PTR_FORMAT
        " and outcaps %" GST_PTR_FORMAT, incaps, outcaps);
    bret = FALSE;
    goto done;
  }
}

/**
 *  @fn static gboolean gst_vvas_xmultisrc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
 *  @param [in] pad     - The GstPad on which event has been received.
 *  @param [in] parent  - The parent of the pad, which is the GstVvasXMSRC, typecasted to GstObject
 *  @param [in] event   - The GstEvent to handle.
 *  @return  On Success returns TRUE
 *           On Failure returns FALSE
 *  @brief   Handles GstEvent coming over the sink pad. Ex : EOS, New caps etc.
 *  @details This function will be set as the callback function for any new event coming on the sink pad
 *           using gst_pad_set_event_function.
 */
static gboolean
gst_vvas_xmultisrc_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstVvasXMSRC *self = GST_VVAS_XMSRC (parent);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (pad, "received event '%s' %p %" GST_PTR_FORMAT,
      gst_event_type_get_name (GST_EVENT_TYPE (event)), event, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps;

      /* Parse the incoming media caps */
      gst_event_parse_caps (event, &caps);

      /* Upstream element has set the caps, now lets find out if these caps are fixed,
       * and also find transform caps for all the source pads */
      ret = gst_vvas_xmultisrc_sink_setcaps (self, self->sinkpad, caps);
      gst_event_unref (event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

/**
 *  @fn static gboolean vvas_xmultisrc_propose_allocation (GstVvasXMSRC * self, GstQuery * query)
 *  @param [in] self   - Pointer to GstVvasXMSRC object.
 *  @param [out] query - Allocation Query to be answered.
 *  @return On Success returns TRUE
 *          On Failure returns FALSE
 *  @brief  This function will propose allocation strategy to upstream element based on the kernel requirements.
 *
 */
static gboolean
vvas_xmultisrc_propose_allocation (GstVvasXMSRC * self, GstQuery * query)
{
  GstCaps *caps;
  GstVideoInfo info;
  GstBufferPool *pool;
  guint size;
  VVASKernel *vvas_handle = self->priv->kernel.vvas_handle;

  /* Parse the incoming query and read the requested caps */
  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  /* Get video info from caps */
  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstStructure *structure;
    GstAllocator *allocator = NULL;
    GstAllocationParams params = { GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, 0, 0,
      0
    };

    if (gst_query_get_n_allocation_params (query) > 0) {
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    } else {
      /* Create VVAS Allocator */
      allocator = gst_vvas_allocator_new (DEFAULT_DEVICE_INDEX,
          TRUE, DEFAULT_MEM_BANK);
      gst_query_add_allocation_param (query, allocator, &params);
    }

    pool = gst_video_buffer_pool_new ();
    GST_LOG_OBJECT (self, "allocated internal sink pool %p", pool);

    structure = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (structure, caps, size, MIN_POOL_BUFFERS,
        0);
    if (vvas_caps_get_sink_stride_align (vvas_handle) > 0
        || vvas_caps_get_sink_height_align (vvas_handle) > 0) {
      GstVideoAlignment video_align = { 0, };

      gst_video_alignment_reset (&video_align);
      video_align.padding_top = 0;
      video_align.padding_left = 0;
      video_align.padding_right = vvas_xmultisrc_get_padding_right (self, &info,
          vvas_caps_get_sink_stride_align (vvas_handle));
      video_align.padding_bottom = ALIGN (GST_VIDEO_INFO_HEIGHT (&info),
          vvas_caps_get_sink_height_align (vvas_handle)) -
          GST_VIDEO_INFO_HEIGHT (&info);
      gst_video_info_align (&info, &video_align);

      gst_buffer_pool_config_add_option (structure,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
      gst_buffer_pool_config_set_video_alignment (structure, &video_align);
      size = GST_VIDEO_INFO_SIZE (&info);
      GST_LOG_OBJECT (self, "kernel stride align %d, size = %d",
          vvas_caps_get_sink_stride_align (vvas_handle), size);
    }
    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_set_allocator (structure, allocator, &params);

    if (allocator)
      gst_object_unref (allocator);

    if (!gst_buffer_pool_set_config (pool, structure))
      goto config_failed;

    GST_OBJECT_LOCK (self);
    gst_query_add_allocation_pool (query, pool, size, MIN_POOL_BUFFERS, 0);
    GST_OBJECT_UNLOCK (self);

    if (self->priv->input_pool)
      gst_object_unref (self->priv->input_pool);

    self->priv->input_pool = pool;
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
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
 *  @fn static gboolean gst_vvas_xmultisrc_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
 *  @param [in] pad          - Pointer to sink GstPad.
 *  @param [in] parent       - Pointer to GstVvasXMSRC object typecasted as GstObject
 *  @param [out] query       - Allocation Query to be answered/executed.
 *  @return  On Success returns TRUE
 *           On Failure returns FALSE
 *  @brief   This function will be invoked whenever an upstream element sends query on the sink pad.
 *  @details This function will be registered using function gst_pad_set_query_function as a query handler.
 *
 */
static gboolean
gst_vvas_xmultisrc_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstVvasXMSRC *self = GST_VVAS_XMSRC (parent);
  gboolean ret = TRUE;
  GstVvasXMSRCPad *srcpad = NULL;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:{
      ret = vvas_xmultisrc_propose_allocation (self, query);
      break;
    }
    case GST_QUERY_CAPS:{
      GstCaps *filter, *caps = NULL, *result = NULL;

      gst_query_parse_caps (query, &filter);

      /* TODO: Query caps only going to src pad 0 and check for others */
      if (!g_list_length (self->srcpads)) {
        GST_DEBUG_OBJECT (pad, "0 source pads in list");
        return FALSE;
      }

      srcpad = gst_vvas_xmultisrc_srcpad_at_index (self, 0);
      if (!srcpad) {
        GST_ERROR_OBJECT (pad, "source pads not available..");
        return FALSE;
      }

      caps = gst_pad_get_pad_template_caps (pad);

      if (filter) {
        GstCaps *tmp = caps;
        /* Creates a new GstCaps that contains all the formats that
         *  are common to both caps */
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

/**
 *  @fn static GstFlowReturn gst_vvas_xmultisrc_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
 *  @param [in] pad          - Pointer to sink GstPad.
 *  @param [in] parent       - Pointer to GstVvasXMSRC object typecasted as GstObject
 *  @param [in] inbuf        - Incoming buffer to be processed.
 *  @return  On Success returns GST_FLOW_OK
 *           On Failure returns Corresponding error code.
 *  @brief   This function will be invoked whenever an upstream element pushes buffers on the sinkpad.
 *  @details This function will be registered using function gst_pad_set_chain_function.
 *
 */
static GstFlowReturn
gst_vvas_xmultisrc_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
{
  GstVvasXMSRC *self = GST_VVAS_XMSRC (parent);
  GstFlowReturn fret = GST_FLOW_OK;
  guint chan_id = 0;
  gboolean bret = FALSE;
#if PROFILING
  uint64_t delta_us;
#endif

  bret = vvas_xmultisrc_write_input_registers (self, &inbuf);
  if (!bret)
    goto error;

  bret = vvas_xmultisrc_write_output_registers (self);
  if (!bret)
    goto error;

  pthread_mutex_lock (&count_mutex);    /* lock for TDM */
#if PROFILING
  f_num++;
  if (f_num == 1)
    clock_gettime (CLOCK_MONOTONIC_RAW, &start);
  if (f_num == 1000) {
    clock_gettime (CLOCK_MONOTONIC_RAW, &end);
    delta_us =
        (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec -
        start.tv_nsec) / 1000;
    GST_INFO_OBJECT (self, "VVAS MSRC %d fps %ld\n", f_num,
        1000000 / (delta_us / 1000));
    f_num = 0;
  }
#endif
  bret = vvas_xmultisrc_process (self);
  if (!bret)
    goto error;

  pthread_mutex_unlock (&count_mutex);

  /* pad push of each output buffer to respective srcpad */
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    GstBuffer *outbuf = self->priv->outbufs[chan_id];
    GstVvasXMSRCPad *srcpad =
        gst_vvas_xmultisrc_srcpad_at_index (self, chan_id);

    gst_buffer_copy_into (outbuf, inbuf,
        (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS |
            GST_BUFFER_COPY_TIMESTAMPS), 0, -1);
    GST_LOG_OBJECT (srcpad,
        "pushing outbuf %p with pts = %" GST_TIME_FORMAT " dts = %"
        GST_TIME_FORMAT " duration = %" GST_TIME_FORMAT, outbuf,
        GST_TIME_ARGS (GST_BUFFER_PTS (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DTS (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));
    fret = gst_pad_push (GST_PAD_CAST (srcpad), outbuf);
    if (G_UNLIKELY (fret != GST_FLOW_OK)) {
      GST_ERROR_OBJECT (self, "failed with reason : %s",
          gst_flow_get_name (fret));
      goto error2;
    }
  }
  if (self->priv->input) {
    free (self->priv->input);
  }
  for (chan_id = 0; chan_id < self->num_request_pads; chan_id++) {
    if (self->priv->output[chan_id])
      free (self->priv->output[chan_id]);
  }

  gst_buffer_unref (inbuf);
  return fret;

error:
  pthread_mutex_unlock (&count_mutex);
error2:
  gst_buffer_unref (inbuf);
  return fret;
}

/**
 *  @fn static gboolean vvas_xmultisrc_init (GstPlugin * multisrc)
 *  @param [in] multisrc     - Pointer to sink GstPad.
 *  @return  On Success returns TRUE
 *           On Failure returns FALSE.
 *  @brief   Entry point to initialize the plug-in.
 *  @details Register the element factories and other features
 *
 */
static gboolean
vvas_xmultisrc_init (GstPlugin * multisrc)
{
  return gst_element_register (multisrc, "vvas_xmultisrc", GST_RANK_PRIMARY,
      GST_TYPE_VVAS_XMSRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xmultisrc,
    "Xilinx generic MultiSrc plugin", vvas_xmultisrc_init, VVAS_API_VERSION,
    "LGPL", "GStreamer xrt", "http://xilinx.com/")
