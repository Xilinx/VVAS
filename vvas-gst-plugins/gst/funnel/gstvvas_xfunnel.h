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

#ifndef _GST_VVAS_XFUNNEL_H_
#define _GST_VVAS_XFUNNEL_H_

#include <gst/gst.h>

G_BEGIN_DECLS

/** @def GST_TYPE_VVAS_XFUNNEL
 *  @brief Macro to get GstVvas_Xfunnel object type
 */
#define GST_TYPE_VVAS_XFUNNEL   (gst_vvas_xfunnel_get_type())

/** @def GST_VVAS_XFUNNEL
 *  @brief Macro to typecast parent object to GstVvas_Xfunnel object
 */
#define GST_VVAS_XFUNNEL(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XFUNNEL,GstVvas_Xfunnel))

/** @def GST_VVAS_XFUNNEL_CLASS
 *  @brief Macro to typecast parent class object to GstVvas_XfunnelClass object
 */
#define GST_VVAS_XFUNNEL_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XFUNNEL,GstVvas_XfunnelClass))

/** @def GST_IS_VVAS_XFUNNEL
 *  @brief Macro to validate whether object is of GstVvas_Xfunnel type
 */
#define GST_IS_VVAS_XFUNNEL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XFUNNEL))

/** @def GST_IS_VVAS_XFUNNEL_CLASS
 *  @brief Macro to validate whether object class is of GstVvas_XfunnelClass type
 */
#define GST_IS_VVAS_XFUNNEL_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XFUNNEL))

typedef struct _GstVvas_Xfunnel GstVvas_Xfunnel;
typedef struct _GstVvas_XfunnelClass GstVvas_XfunnelClass;

struct _GstVvas_Xfunnel
{
  /** parent */
  GstElement base_vvasxfunnel;
  /** Source Pad */
  GstPad *srcpad;
  /** Processing thread to send buffers in Round robin order */
  GThread *processing_thread;
  /** Mutex lock for processing_thread */
  GMutex mutex_lock;
  /** Sink caps info */
  GstCaps *sink_caps;
  /** size of pad's queue */
  guint queue_size;
  /** Time to wait before switching to the next sink in milliseconds */
  guint sink_wait_timeout;
  /** Total sink pad counter */
  guint sink_pad_idx;
  /** GstFlowreturn info  */
  GstFlowReturn last_fret;
  /** should \p processing_thread exit */
  gboolean is_exit_thread;
  /** To know whether user has provided sink_wait_timeout */
  gboolean is_user_timeout;
};

struct _GstVvas_XfunnelClass
{
  /** parent class */
  GstElementClass base_vvasxfunnel_class;
};

GType gst_vvas_xfunnel_get_type (void);


GType gst_vvas_xfunnel_pad_get_type (void);

/** @def GST_TYPE_VVAS_XFUNNEL_PAD
 *  @brief Macro to get GstVvas_XfunnelPad object type
 */
#define GST_TYPE_VVAS_XFUNNEL_PAD (gst_vvas_xfunnel_pad_get_type())

/** @def GST_VVAS_XFUNNEL_PAD
 *  @brief Macro to typecast parent object to GstVvas_XfunnelPad object
 */
#define GST_VVAS_XFUNNEL_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VVAS_XFUNNEL_PAD, GstVvas_XfunnelPad))

/** @def GST_VVAS_XFUNNEL_PAD_CLASS
 *  @brief Macro to typecast parent class object to GstVvas_XfunnelPadClass object
 */
#define GST_VVAS_XFUNNEL_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VVAS_XFUNNEL_PAD, GstVvas_XfunnelPadClass))

/** @def GST_IS_VVAS_XFUNNEL_PAD
 *  @brief Macro to validate whether object is of GstVvas_XfunnelPad type
 */
#define GST_IS_VVAS_XFUNNEL_PAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VVAS_XFUNNEL_PAD))

/** @def GST_IS_VVAS_XFUNNEL_PAD_CLASS
 *  @brief Macro to validate whether object class  is of GstVvas_XfunnelPadClass type
 */
#define GST_IS_VVAS_XFUNNEL_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VVAS_XFUNNEL_PAD))
#define GST_VVAS_XFUNNEL_PAD_CAST(obj) ((GstVvas_XfunnelPad *)(obj))

typedef struct _GstVvas_XfunnelPad GstVvas_XfunnelPad;
typedef struct _GstVvas_XfunnelPadClass GstVvas_XfunnelPadClass;

struct _GstVvas_XfunnelPad
{
  /** parent */
  GstPad parent;
  /** Queue for storing buffer */
  GQueue *queue;
  /** Mutex lock to protect concurrent access to Queue */
  GMutex lock;
  /** Condition variable to signal waiting thread */
  GCond cond;
  /** Pad index */
  guint pad_idx;
  /** To store EOS info */
  gboolean got_eos;
  /** To know whether EOS is sent downstream or not */
  gboolean is_eos_sent;
  /** Name of the pad */
  gchar *name;
  /** Time when last buffer was popped from the queue */
  gint64 time;
};

struct _GstVvas_XfunnelPadClass
{
  /** parent class */
  GstPadClass parent;
};

G_DEFINE_TYPE (GstVvas_XfunnelPad, gst_vvas_xfunnel_pad, GST_TYPE_PAD);

G_END_DECLS

#endif
