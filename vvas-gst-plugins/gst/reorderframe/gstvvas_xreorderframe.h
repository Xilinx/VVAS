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

#ifndef __GST_VVAS_XREORDERFRAME_H__
#define __GST_VVAS_XREORDERFRAME_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_VVAS_XREORDERFRAME (gst_vvas_xreorderframe_get_type())
G_DECLARE_FINAL_TYPE(Gstvvasxreorderframe, gst_vvas_xreorderframe,
                     GST, VVAS_XREORDERFRAME, GstElement)

/* These might be added to glib in the future, but in the meantime they're defined here. */
#ifndef GULONG_TO_POINTER
#define GULONG_TO_POINTER(ul) ((gpointer)(gulong)(ul))
#endif

struct _Gstvvasxreorderframe
{

    /* parent */
    GstElement element;

    /* sink pads and source pad */
    GstPad *infer_sinkpad, *skip_sinkpad, *srcpad;

    /* Hash tables for maintaining skip buffers, infer buffers, next valid frameId and pad eos recieved */
    GHashTable *skip_hash, *infer_hash, *frameId_hash, *pad_eos_hash;

    /* should \p processing_thread exit */
    gboolean is_exit_thread;

    /** Processing thread to reorder buffers and push them */
    GThread *processing_thread;

    /** mutex lock for infer queues, skip queues and thread flag */
    GMutex infer_lock, skip_lock, thread_lock, pad_eos_lock;

    /** GCond for infer buffers length */
    GCond infer_cond;

    /** Flag to set when processing thread is waiting for infer buffers */
    gboolean is_waiting_for_buffer;

    /** current infer buffers length */
    guint infer_buffers_len;

    /** Flag to set when GStreamer EOS is recieved */
    gboolean is_eos;
};

G_END_DECLS
#endif /* __GST_VVAS_XREORDERFRAME_H__ */
