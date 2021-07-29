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

#ifndef __GST_IVAS_XABRSCALER_H__
#define __GST_IVAS_XABRSCALER_H__

#include <gst/gst.h>

#ifdef ENABLE_XRM_SUPPORT
#include <xrm.h>
#endif

G_BEGIN_DECLS
#define GST_TYPE_IVAS_XABRSCALER (gst_ivas_xabrscaler_get_type())
#define GST_IVAS_XABRSCALER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IVAS_XABRSCALER,GstIvasXAbrScaler))
#define GST_IVAS_XABRSCALER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IVAS_XABRSCALER,GstIvasXAbrScalerClass))
#define GST_IVAS_XABRSCALER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_IVAS_XABRSCALER,GstIvasXAbrScalerClass))
#define GST_IS_IVAS_XABRSCALER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IVAS_XABRSCALER))
#define GST_IS_IVAS_XABRSCALER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IVAS_XABRSCALER))

#define ENABLE_PPE_SUPPORT
#define MAX_CHANNELS 16

typedef struct _GstIvasXAbrScaler GstIvasXAbrScaler;
typedef struct _GstIvasXAbrScalerClass GstIvasXAbrScalerClass;
typedef struct _GstIvasXAbrScalerPrivate GstIvasXAbrScalerPrivate;

typedef enum {
  COEF_FIXED,
  COEF_AUTO_GENERATE,
} IvasXAbrScalerCoefType;

struct _GstIvasXAbrScaler
{
  GstElement element;
  GstIvasXAbrScalerPrivate *priv;
  GstPad *sinkpad;
  GList *srcpads;

  GHashTable *pad_indexes;
  guint num_request_pads;

  gchar *kern_name;
  gchar *xclbin_path;
  gint dev_index;
  gint ppc;
  gint scale_mode;
  guint out_stride_align;
  guint out_elevation_align;
  IvasXAbrScalerCoefType coef_load_type;
  guint num_taps;
  gboolean avoid_output_copy;
#ifdef ENABLE_PPE_SUPPORT
  gfloat alpha_r;
  gfloat alpha_g;
  gfloat alpha_b;
  gfloat beta_r;
  gfloat beta_g;
  gfloat beta_b;
#endif
};

struct _GstIvasXAbrScalerClass
{
  GstElementClass parent_class;
};

GType gst_ivas_xabrscaler_get_type (void);

G_END_DECLS
#endif /* __GST_IVAS_XABRSCALER_H__ */
