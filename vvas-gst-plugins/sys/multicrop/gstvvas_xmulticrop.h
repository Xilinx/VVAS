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

#ifndef _GST_VVAS_XMULTICROP_H_
#define _GST_VVAS_XMULTICROP_H_

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_VVAS_XMULTICROP  (gst_vvas_xmulticrop_get_type())
#define GST_VVAS_XMULTICROP(obj)  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XMULTICROP,GstVvasXMultiCrop))
#define GST_VVAS_XMULTICROP_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XMULTICROP,GstVvasXMultiCropClass))
#define GST_IS_VVAS_XMULTICROP(obj)  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XMULTICROP))
#define GST_IS_VVAS_XMULTICROP_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XMULTICROP))

typedef struct _GstVvasXMultiCrop GstVvasXMultiCrop;
typedef struct _GstVvasXMultiCropClass GstVvasXMultiCropClass;

#define MAX_CHANNELS 40

typedef struct _GstVvasXMultiCropPrivate GstVvasXMultiCropPrivate;

typedef enum {
  COEF_FIXED,
  COEF_AUTO_GENERATE,
} VvasXMultiCropCoefType;

typedef struct _crop_params {
  guint x;
  guint y;
  guint width;
  guint height;
} VvasCropParams;

struct _GstVvasXMultiCrop
{
  GstBaseTransform base_vvasxmulticrop;
  GstVvasXMultiCropPrivate *priv;

  gchar *kern_name;
  gchar *xclbin_path;
  gint dev_index;
  guint in_mem_bank;
  guint out_mem_bank;
  gint ppc;
  gint scale_mode;
  guint out_stride_align;
  guint out_elevation_align;
  VvasXMultiCropCoefType coef_load_type;
  guint num_taps;
  gboolean avoid_output_copy;
  gboolean enabled_pipeline;
#ifdef ENABLE_PPE_SUPPORT
  gfloat alpha_r;
  gfloat alpha_g;
  gfloat alpha_b;
  gfloat beta_r;
  gfloat beta_g;
  gfloat beta_b;
#endif
  VvasCropParams crop_params;
  guint subbuffer_width;
  guint subbuffer_height;
  GstVideoFormat subbuffer_format;
  gboolean ppe_on_main_buffer;
};

struct _GstVvasXMultiCropClass
{
  GstBaseTransformClass base_vvasxmulticrop_class;
};

GType gst_vvas_xmulticrop_get_type (void);

G_END_DECLS

#endif
