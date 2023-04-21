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

#ifndef __GST_VVAS_XCOMPOSITOR_H__
#define __GST_VVAS_XCOMPOSITOR_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/** @def GST_TYPE_VVAS_XCOMPOSITOR
 *  @brief Macro to get GstVvasXCompositor object type
 */
#define GST_TYPE_VVAS_XCOMPOSITOR (gst_vvas_xcompositor_get_type())

/** @def GST_VVAS_XCOMPOSITOR
 *  @brief Macro to typecast parent object to GstVvasXCompositor object
 */
#define GST_VVAS_XCOMPOSITOR(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XCOMPOSITOR, GstVvasXCompositor))

/** @def GST_VVAS_XCOMPOSITOR_CLASS
 *  @brief Macro to typecast parent class object to GstVvasXCompositorClass object
 */
#define GST_VVAS_XCOMPOSITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XCOMPOSITOR, GstVvasXCompositor))

/** @def GST_VVAS_XCOMPOSITOR_GET_CLASS
 *  @brief Macro to get object GstVvasXCompositorClass object from GstVvasXCompositor object
 */
#define GST_VVAS_XCOMPOSITOR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_VVAS_XCOMPOSITOR,GstVvasXCompositor))

/** @def GST_IS_VVAS_XCOMPOSITOR
 *  @brief Macro to validate whether object is of GstVvasXCompositor type
 */
#define GST_IS_VVAS_XCOMPOSITOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XCOMPOSITOR))

/** @def GST_IS_VVAS_XCOMPOSITOR_CLASS
 *  @brief Macro to validate whether object class  is of GstVvasXCompositorClass type
 */
#define GST_IS_VVAS_XCOMPOSITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XCOMPOSITOR))

/** @def MAX_CHANNELS
 *  @brief Macro to set maximum number of channels that GstVvasXCompositor can support
 */
#define MAX_CHANNELS 16

typedef struct _GstVvasXCompositor GstVvasXCompositor;
typedef struct _GstVvasXCompositorClass GstVvasXCompositorClass;
typedef struct _GstVvasXCompositorPrivate GstVvasXCompositorPrivate;

/** @enum VvasXCompositorCoefType
 *  @brief  Contains Multi scaler coefficient type
 */
typedef enum
{
  /** Fixed Coefficients */
  COEF_FIXED,
  /** Auto Generate Coefficients */
  COEF_AUTO_GENERATE,
} VvasXCompositorCoefType;

struct _GstVvasXCompositor
{
  /** parent of GstVvasXCompositor object */
  GstVideoAggregator compositor;
  /** Pointer instance's private structure for each of access */
  GstVvasXCompositorPrivate *priv;
  /** Pointer to the list of sink pads */
  GList *sinkpads;
  /* * POinter to the source pad */
  GstPad *srcpad;
  /* *Counter for request pads */
  guint num_request_pads;
  /** Has table for pad indexes */
  GHashTable *pad_indexes;
  /** Name of the kernel */
  gchar *kern_name;
  /** Path to the xclbin */
  gchar *xclbin_path;
  /** Device Index */
  gint dev_index;
  /** Pixels Per Clock configured in Multiscaler kernel */
  gint ppc;
  /** Scale Mode configured in Multiscaler kernel */
  gint scale_mode;
  /** Output stride align */
  guint out_stride_align;
  /** Output elevation align */
  guint out_elevation_align;
  /** input memory bank to allocate memory */
  guint in_mem_bank;
  /** output memory bank to allocate memory */
  guint out_mem_bank;
  /** Filter coefficients loading type */
  VvasXCompositorCoefType coef_load_type;
  /** Numbre of filter taps */
  guint num_taps;
  /** Flag to enable/disable best fit the input video on output screen */
  gboolean best_fit;
  /** Flag to enable/disable the copying of buffer to downstream */
  gboolean avoid_output_copy;
  /** Flag to enable/disable the buffer pipelining to improve performance in non zero-copy use cases */
  gboolean enabled_pipeline;
  /** Flag to enable software scaling flow */
  gboolean software_scaling;
};

struct _GstVvasXCompositorClass
{
  /** parent class */
  GstVideoAggregatorClass compositor_class;
};

GType gst_vvas_xcompositor_get_type (void);

G_END_DECLS
#endif /* __GST_VVAS_XCOMPOSITOR_H__ */
