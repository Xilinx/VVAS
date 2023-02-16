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

#ifndef _GST_VVAS_XSKIPFRAME_H_
#define _GST_VVAS_XSKIPFRAME_H_

#include <gst/gst.h>

G_BEGIN_DECLS


/** @def GST_TYPE_VVAS_XSKIPFRAME
 *  @brief Macro to get GstVvas_Xskipframe object type
 */
#define GST_TYPE_VVAS_XSKIPFRAME (gst_vvas_xskipframe_get_type()) 

G_DECLARE_FINAL_TYPE (GstVvas_Xskipframe, gst_vvas_xskipframe, 
                      GST, VVAS_XSKIPFRAME, GstElement)

typedef struct _GstVvas_Xskipframe GstVvas_Xskipframe;

struct _GstVvas_Xskipframe
{
  /** parent of _GstVvas_Xskipframe object structure */
  GstElement element;
  /** sinkpad */
  GstPad *sinkpad;
  /** Inference source pad */
  GstPad *inference_srcpad;
  /** Skip source pad */
  GstPad *skip_srcpad;
  /** src_id and frame_id pair hash table */
  GHashTable *frameid_pair;
  /** Inference pending flag and src_id pair hash table */
  GHashTable *infer_pair;
  /** Previous srouce id to maintain batch */
  guint prev_src_id;
  /** maintain batch for all the sources from funnel */
  gint batch_id;
  /** no. of skip frames */
  guint infer_interval;
};

G_END_DECLS

#endif
