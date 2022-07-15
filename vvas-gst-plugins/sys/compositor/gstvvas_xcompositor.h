/*
 * Copyright (C) 2021 - 2022 Xilinx, Inc.  All rights reserved.
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
#define GST_TYPE_VVAS_XCOMPOSITOR (gst_vvas_xcompositor_get_type())
#define GST_VVAS_XCOMPOSITOR(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XCOMPOSITOR, GstVvasXCompositor))
#define GST_VVAS_XCOMPOSITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XCOMPOSITOR, GstVvasXCompositor))
#define GST_VVAS_XCOMPOSITOR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_VVAS_XCOMPOSITOR,GstVvasXCompositor))
#define GST_IS_VVAS_XCOMPOSITOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XCOMPOSITOR))
#define GST_IS_VVAS_XCOMPOSITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XCOMPOSITOR))
#define MAX_CHANNELS 16
typedef struct _GstVvasXCompositor GstVvasXCompositor;
typedef struct _GstVvasXCompositorClass GstVvasXCompositorClass;
typedef struct _GstVvasXCompositorPrivate GstVvasXCompositorPrivate;

typedef enum
{
  COEF_FIXED,
  COEF_AUTO_GENERATE,
} VvasXCompositorCoefType;

struct _GstVvasXCompositor
{
  GstVideoAggregator compositor;
  GstVvasXCompositorPrivate *priv;
  GList *sinkpads;
  GstPad *srcpad;
  guint num_request_pads;
  GHashTable *pad_indexes;
  gchar *kern_name;
  gchar *xclbin_path;
  gint dev_index;
  gint ppc;
  gint scale_mode;
  guint out_stride_align;
  guint out_elevation_align;
  guint in_mem_bank;
  guint out_mem_bank;
  VvasXCompositorCoefType coef_load_type;
  guint num_taps;
  gboolean best_fit;
  gboolean avoid_output_copy;
  gboolean enabled_pipeline;
};

struct _GstVvasXCompositorClass
{
  GstVideoAggregatorClass compositor_class;
};

GType gst_vvas_xcompositor_get_type (void);

G_END_DECLS
#endif /* __GST_VVAS_XCOMPOSITOR_H__ */
