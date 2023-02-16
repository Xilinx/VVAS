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

#ifndef __GST_VVAS_XDEFUNNEL_H__
#define __GST_VVAS_XDEFUNNEL_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/** @def GST_TYPE_VVAS_XDEFUNNEL
 *  @brief Macro to get GstVvas_XDeFunnel object type
 */
#define GST_TYPE_VVAS_XDEFUNNEL \
  (gst_vvas_xdefunnel_get_type())

/** @def GST_VVAS_XDEFUNNEL
 *  @brief Macro to typecast parent object to GstVvas_XDeFunnel object
 */
#define GST_VVAS_XDEFUNNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VVAS_XDEFUNNEL, GstVvas_XDeFunnel))

/** @def GST_VVAS_XDEFUNNEL_CLASS
 *  @brief Macro to typecast parent class object to GstVvas_XDeFunnelClass object
 */
#define GST_VVAS_XDEFUNNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VVAS_XDEFUNNEL, GstVvas_XDeFunnelClass))

/** @def GST_IS_VVAS_XDEFUNNEL
 *  @brief Macro to validate whether object is of GstVvas_XDeFunnel type
 */
#define GST_IS_VVAS_XDEFUNNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VVAS_XDEFUNNEL))

/** @def GST_IS_VVAS_XDEFUNNEL_CLASS
 *  @brief Macro to validate whether object class is of GstVvas_XDeFunnelClass type
 */
#define GST_IS_VVAS_XDEFUNNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VVAS_XDEFUNNEL))

typedef struct _GstVvas_XDeFunnel GstVvas_XDeFunnel;
typedef struct _GstVvas_XDeFunnelClass GstVvas_XDeFunnelClass;

/** @struct GstVvas_XDeFunnel
 *  @brief  Contains context of GstVvas_XDeFunnel object
 */
struct _GstVvas_XDeFunnel
{
  /** parent */
  GstElement element;
  /** sink pad */
  GstPad *sinkpad;
  /** Number of source pads */
  guint nb_srcpads;
  /** Active pad */
  GstPad *active_srcpad;
  /** Hash Table to keep pad-index/source_pad info */
  GHashTable *source_id_pairs;
  /** Sink pad's caps */
  GstCaps *sink_caps;
};

struct _GstVvas_XDeFunnelClass
{
  /** parent class */
  GstElementClass parent_class;
};

G_GNUC_INTERNAL GType gst_vvas_xdefunnel_get_type (void);

G_END_DECLS
#endif /* __GST_DEFUNNEL_H__ */
