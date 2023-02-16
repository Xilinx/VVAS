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

#ifndef __GST_VVAS_XABRSCALER_H__
#define __GST_VVAS_XABRSCALER_H__

#include <gst/gst.h>

#ifdef ENABLE_XRM_SUPPORT
#include <xrm.h>
#endif


G_BEGIN_DECLS
/** @def GST_TYPE_VVAS_XABRSCALER
 *  @brief Macro to get GstVvasXAbrScaler object type
 */
#define GST_TYPE_VVAS_XABRSCALER (gst_vvas_xabrscaler_get_type())

/** @def GST_VVAS_XABRSCALER
 *  @brief Macro to typecast parent object to GstVvasXAbrScaler object
 */
#define GST_VVAS_XABRSCALER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XABRSCALER,GstVvasXAbrScaler))

/** @def GST_VVAS_XABRSCALER_CLASS
 *  @brief Macro to typecast parent class to GstVvasXAbrScalerClass
 */
#define GST_VVAS_XABRSCALER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XABRSCALER,GstVvasXAbrScalerClass))

/** @def GST_VVAS_XABRSCALER_GET_CLASS
 *  @brief Macro to get GstVvasXAbrScalerClass type
 */
#define GST_VVAS_XABRSCALER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_VVAS_XABRSCALER,GstVvasXAbrScalerClass))

/** @def GST_IS_VVAS_XABRSCALER
 *  @brief Macro to validate whether object is of GstVvasXAbrScaler type
 */
#define GST_IS_VVAS_XABRSCALER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XABRSCALER))

/** @def GST_IS_VVAS_XABRSCALER_CLASS
 *  @brief Macro to validate whether class is of GstVvasXAbrScalerClass type
 */
#define GST_IS_VVAS_XABRSCALER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XABRSCALER))

/** @def MAX_CHANNELS
 *  @brief Maximumx output channels possible.
 */
#define MAX_CHANNELS 16

typedef struct _GstVvasXAbrScaler GstVvasXAbrScaler;
typedef struct _GstVvasXAbrScalerClass GstVvasXAbrScalerClass;
typedef struct _GstVvasXAbrScalerPrivate GstVvasXAbrScalerPrivate;

/** @enum    VvasXAbrScalerCoefType
 *  @brief   Enum for Scaler IP co-efficients type
 */
typedef enum {
  /** Fixed co-efficients are used for scaler operations */
  COEF_FIXED,
  /** Co-efficients will be auto generated */
  COEF_AUTO_GENERATE,
} VvasXAbrScalerCoefType;

/** @struct _GstVvasXAbrScaler
 *  @brief  Structure for plugins instance.
 */
struct _GstVvasXAbrScaler
{
  /** parent of GstVvasXAbrScaler object */
  GstElement element;
  /** Pointer to instance's private structure */
  GstVvasXAbrScalerPrivate *priv;
  /** Sink pads instance */
  GstPad *sinkpad;
  /** List of source pad instances */
  GList *srcpads;

  /** Hash table to look up for source pad index */
  GHashTable *pad_indexes;
  /** Number of request source pad created */
  guint num_request_pads;

  /** FPGA kernel name for scaler IP */
  gchar *kern_name;
  /** Path for XCLBIN file */
  gchar *xclbin_path;
  /** Device index */
  gint dev_index;
  /** Input memory bank that IP is connected to */
  guint in_mem_bank;
  /** Output memory bank that IP is connected to */
  guint out_mem_bank;
  /** Pixel Per Clock that the scaler IP is currently working */
  gint ppc;
  /** Scale Mode configured in Multiscaler kernel, Ex bilinear, bicubic, polyphase */
  gint scale_mode;
  /** Output stride requirement for scaler IP */
  guint out_stride_align[MAX_CHANNELS];
  /** Output elevation requirement for scaler IP */
  guint out_elevation_align[MAX_CHANNELS];
  /** Scaler co-efficients type */
  VvasXAbrScalerCoefType coef_load_type;
  /** Number of taps in co-efficients */
  guint num_taps;
  /** Avoid copy before pushing the buffer to downstream */
  gboolean avoid_output_copy;
  /** Enable buffer pipelining to improve performance in non zero-copy use cases */
  gboolean enabled_pipeline;
  /** Flag to enable software scaling flow */
  gboolean software_scaling;
#ifdef ENABLE_PPE_SUPPORT
  /** PreProcessing parameter alpha red channel value */
  gfloat alpha_r;
  /** PreProcessing parameter alpha green channel value */
  gfloat alpha_g;
  /** PreProcessing parameter alpha blue channel value */
  gfloat alpha_b;
  /** PreProcessing parameter beta red channel value */
  gfloat beta_r;
  /** PreProcessing parameter beta green channel value */
  gfloat beta_g;
  /** PreProcessing parameter beta blue channel value */
  gfloat beta_b;
  gboolean get_pp_config;
#endif
  /** Crop X coordinate */
  guint crop_x;
  /** Crop Y coordinate */
  guint crop_y;
  /** Width of the cropping rectangle */
  guint crop_width;
  /** Height of the cropping rectangle */
  guint crop_height;
};

/** @struct _GstVvasXAbrScalerClass
 *  @brief  Structure for plugins class.
 */
struct _GstVvasXAbrScalerClass
{
  /** parent class */
  GstElementClass parent_class;
};

GType gst_vvas_xabrscaler_get_type (void);

G_END_DECLS
#endif /* __GST_VVAS_XABRSCALER_H__ */
