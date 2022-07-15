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

#ifndef __GST_VVAS_XDEFUNNEL_H__
#define __GST_VVAS_XDEFUNNEL_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_VVAS_XDEFUNNEL \
  (gst_vvas_xdefunnel_get_type())
#define GST_VVAS_XDEFUNNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VVAS_XDEFUNNEL, GstVvas_XDeFunnel))
#define GST_VVAS_XDEFUNNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VVAS_XDEFUNNEL, GstVvas_XDeFunnelClass))
#define GST_IS_VVAS_XDEFUNNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VVAS_XDEFUNNEL))
#define GST_IS_VVAS_XDEFUNNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VVAS_XDEFUNNEL))
typedef struct _GstVvas_XDeFunnel GstVvas_XDeFunnel;
typedef struct _GstVvas_XDeFunnelClass GstVvas_XDeFunnelClass;

/**
 * GstVvas_XDeFunnel:
 *
 * The opaque #GstVvas_XDeFunnel data structure.
 */
struct _GstVvas_XDeFunnel
{
  GstElement element;

  GstPad *sinkpad;

  guint nb_srcpads;
  GstPad *active_srcpad;

  /* This table contains srcpad and source ids */
  GHashTable *source_id_pairs;
  GstCaps *sink_caps;
};

struct _GstVvas_XDeFunnelClass
{
  GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_vvas_xdefunnel_get_type (void);

G_END_DECLS
#endif /* __GST_DEFUNNEL_H__ */
