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

#ifndef _GST_VVAS_XMULTICROP_H_
#define _GST_VVAS_XMULTICROP_H_

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

/** @def GST_TYPE_VVAS_XMULTICROP
 *  @brief Macro to get GstVvasXMultiCrop object type
 */
#define GST_TYPE_VVAS_XMULTICROP  (gst_vvas_xmulticrop_get_type())

/** @def GST_VVAS_XMULTICROP
 *  @brief Macro to typecast parent object to GstVvasXMultiCrop object
 */
#define GST_VVAS_XMULTICROP(obj)  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XMULTICROP,GstVvasXMultiCrop))

/** @def GST_VVAS_XMULTICROP_CLASS
 *  @brief Macro to typecast parent class object to GstVvasXMultiCropClass object
 */
#define GST_VVAS_XMULTICROP_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XMULTICROP,GstVvasXMultiCropClass))

/** @def GST_IS_VVAS_XMULTICROP
 *  @brief Macro to validate whether object is of GstVvasXMultiCrop type
 */
#define GST_IS_VVAS_XMULTICROP(obj)  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XMULTICROP))

/** @def GST_IS_VVAS_XMULTICROP_CLASS
 *  @brief Macro to validate whether object class is of GstVvasXMultiCropClass type
 */
#define GST_IS_VVAS_XMULTICROP_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XMULTICROP))

typedef struct _GstVvasXMultiCrop GstVvasXMultiCrop;
typedef struct _GstVvasXMultiCropClass GstVvasXMultiCropClass;

/** @def MAX_CHANNELS
 *  @brief Maximum number of channel that can be processed using MultiScaler IP
 */
#define MAX_CHANNELS 40

typedef struct _GstVvasXMultiCropPrivate GstVvasXMultiCropPrivate;

/** @enum VvasXMultiCropCoefType
 *  @brief  Contains enums for coefficients loading type
 */
typedef enum {
  /** Fixed Coefficient type */
  COEF_FIXED,
  /** Auto generate Coefficient type */
  COEF_AUTO_GENERATE,
} VvasXMultiCropCoefType;

/** @struct VvasCropParams
 *  @brief  Contains information of crop parameters
 */
typedef struct _crop_params {
  /** Crop X coordinate */
  guint x;
  /** Crop Y coordinate */
  guint y;
  /** Crop Width */
  guint width;
  /** Crop Height */
  guint height;
} VvasCropParams;

/** @struct GstVvasXMultiCrop
 *  @brief  Contains context of vvas_xmulticrop instance
 */
struct _GstVvasXMultiCrop
{
  /** parent */
  GstBaseTransform base_vvasxmulticrop;
  /** private data */
  GstVvasXMultiCropPrivate *priv;
  /** kernel name */
  gchar *kern_name;
  /** XCLBIN location */
  gchar *xclbin_path;
  /** Device Index */
  gint dev_index;
  /** Input memory bank */
  guint in_mem_bank;
  /** Output memory bank */
  guint out_mem_bank;
  /** Pixels per clock */
  gint ppc;
  /** Scaling mode */
  gint scale_mode;
  /** Output stride alignment */
  guint out_stride_align;
  /** Input stride alignment */
  guint out_elevation_align;
  /** Coefficient load type */
  VvasXMultiCropCoefType coef_load_type;
  /** Number of taps */
  guint num_taps;
  /** Avoid output copy */
  gboolean avoid_output_copy;
  /** Enable pipeline */
  gboolean enabled_pipeline;
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
#endif
  /** Crop parameters */
  VvasCropParams crop_params;
  /** Dynamic crop buffer's width */
  guint subbuffer_width;
  /** Dynamic crop buffer's height */
  guint subbuffer_height;
  /** Dynamic crop buffer's format */
  GstVideoFormat subbuffer_format;
  /** Apply pre-processing on main/output buffer */
  gboolean ppe_on_main_buffer;
  /** Flag to enable software scaling flow */
  gboolean software_scaling;
};

/** @struct GstVvasXMultiCropClass
 *  @brief  Contains context of GstVvasXMultiCropClass
 */
struct _GstVvasXMultiCropClass
{
  /** parent class */
  GstBaseTransformClass base_vvasxmulticrop_class;
};

/**
 *  @brief Get GST_TYPE_VVAS_XMULTICROP GType
 */
GType gst_vvas_xmulticrop_get_type (void);

G_END_DECLS

#endif
