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

#ifndef _GST_VVAS_XFUNNEL_H_
#define _GST_VVAS_XFUNNEL_H_

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_VVAS_XFUNNEL   (gst_vvas_xfunnel_get_type())
#define GST_VVAS_XFUNNEL(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XFUNNEL,GstVvas_Xfunnel))
#define GST_VVAS_XFUNNEL_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XFUNNEL,GstVvas_XfunnelClass))
#define GST_IS_VVAS_XFUNNEL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XFUNNEL))
#define GST_IS_VVAS_XFUNNEL_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XFUNNEL))

typedef struct _GstVvas_Xfunnel GstVvas_Xfunnel;
typedef struct _GstVvas_XfunnelClass GstVvas_XfunnelClass;

struct _GstVvas_Xfunnel
{
  GstElement base_vvasxfunnel;

  GstPad *srcpad;
  GThread *processing_thread;
  GMutex mutex_lock;
  GstCaps *sink_caps;
  guint queue_size;
  guint sink_wait_timeout;
  guint sink_pad_idx;
  GstFlowReturn last_fret;
  gboolean is_exit_thread;
  gboolean is_user_timeout;
};

struct _GstVvas_XfunnelClass
{
  GstElementClass base_vvasxfunnel_class;
};

GType gst_vvas_xfunnel_get_type (void);


GType gst_vvas_xfunnel_pad_get_type (void);
#define GST_TYPE_VVAS_XFUNNEL_PAD (gst_vvas_xfunnel_pad_get_type())
#define GST_VVAS_XFUNNEL_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VVAS_XFUNNEL_PAD, GstVvas_XfunnelPad))
#define GST_VVAS_XFUNNEL_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VVAS_XFUNNEL_PAD, GstVvas_XfunnelPadClass))
#define GST_IS_VVAS_XFUNNEL_PAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VVAS_XFUNNEL_PAD))
#define GST_IS_VVAS_XFUNNEL_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VVAS_XFUNNEL_PAD))
#define GST_VVAS_XFUNNEL_PAD_CAST(obj) ((GstVvas_XfunnelPad *)(obj))

typedef struct _GstVvas_XfunnelPad GstVvas_XfunnelPad;
typedef struct _GstVvas_XfunnelPadClass GstVvas_XfunnelPadClass;

struct _GstVvas_XfunnelPad
{
  GstPad parent;

  GQueue *queue;
  GMutex lock;
  GCond cond;
  guint pad_idx;
  gboolean got_eos;
  gboolean is_eos_sent;
  gchar *name;
  gint64 time;
};

struct _GstVvas_XfunnelPadClass
{
  GstPadClass parent;
};

G_DEFINE_TYPE (GstVvas_XfunnelPad, gst_vvas_xfunnel_pad, GST_TYPE_PAD);

G_END_DECLS

#endif
