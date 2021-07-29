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
#ifndef __GST_IVAS_XMSRC_H__
#define __GST_IVAS_XMSRC_H__

#include <gst/gst.h>
#include <gst/ivas/gstivasallocator.h>
#include <ivas/ivas_kernel.h>

G_BEGIN_DECLS
#define GST_TYPE_IVAS_XMSRC (gst_ivas_xmultisrc_get_type())
#define GST_IVAS_XMSRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IVAS_XMSRC,GstIvasXMSRC))
#define GST_IVAS_XMSRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IVAS_XMSRC,GstIvasXMSRCClass))
#define GST_IVAS_XMSRC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_IVAS_XMSRC,GstIvasXMSRCClass))
#define GST_IS_IVAS_XMSRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IVAS_XMSRC))
#define GST_IS_IVAS_XMSRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IVAS_XMSRC))
#define MAX_CHANNELS 16
typedef struct _GstIvasXMSRC GstIvasXMSRC;
typedef struct _GstIvasXMSRCClass GstIvasXMSRCClass;
typedef struct _GstIvasXMSRCPrivate GstIvasXMSRCPrivate;

struct _GstIvasXMSRC
{
  GstElement element;
  GstIvasXMSRCPrivate *priv;
  GstPad *sinkpad;
  GList *srcpads;

  GHashTable *pad_indexes;
  guint num_request_pads;

  gchar *xclbin_path;
  gchar *config_file;

};

struct _GstIvasXMSRCClass
{
  GstElementClass parent_class;
};

GType gst_ivas_xmultisrc_get_type (void);

G_END_DECLS
#endif /* __GST_IVAS_XMSRC_H__ */
