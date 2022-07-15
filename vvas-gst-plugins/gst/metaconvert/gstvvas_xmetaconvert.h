/*
 * Copyright (C) 2020 - 2021 Xilinx, Inc.  All rights reserved.
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

#ifndef _GST_VVAS_XMETACONVERT_H_
#define _GST_VVAS_XMETACONVERT_H_

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS
#define GST_TYPE_VVAS_XMETACONVERT   (gst_vvas_xmetaconvert_get_type())
#define GST_VVAS_XMETACONVERT(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XMETACONVERT,GstVvas_Xmetaconvert))
#define GST_VVAS_XMETACONVERT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XMETACONVERT,GstVvas_XmetaconvertClass))
#define GST_IS_VVAS_XMETACONVERT(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XMETACONVERT))
#define GST_IS_VVAS_XMETACONVERT_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XMETACONVERT))
typedef struct _GstVvas_Xmetaconvert GstVvas_Xmetaconvert;
typedef struct _GstVvas_XmetaconvertClass GstVvas_XmetaconvertClass;
typedef struct _GstVvas_XmetaconvertPrivate GstVvas_XmetaconvertPrivate;

struct _GstVvas_Xmetaconvert
{
  GstBaseTransform base_vvasxmetaconvert;
  GstVvas_XmetaconvertPrivate *priv;

  gchar *json_file;
};

struct _GstVvas_XmetaconvertClass
{
  GstBaseTransformClass base_vvasxmetaconvert_class;
};

GType gst_vvas_xmetaconvert_get_type (void);

G_END_DECLS
#endif
