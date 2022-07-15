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

#ifndef __GST_VVAS_XLOOKAHEAD_H__
#define __GST_VVAS_XLOOKAHEAD_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_VVAS_XLOOKAHEAD (gst_vvas_xlookahead_get_type())
#define GST_VVAS_XLOOKAHEAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XLOOKAHEAD,GstVvas_XLookAhead))
#define GST_VVAS_XLOOKAHEAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XLOOKAHEAD,GstVvas_XLookAheadClass))
#define GST_IS_VVAS_XLOOKAHEAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XLOOKAHEAD))
#define GST_IS_VVAS_XLOOKAHEAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XLOOKAHEAD))

typedef struct _GstVvas_XLookAhead GstVvas_XLookAhead;
typedef struct _GstVvas_XLookAheadClass GstVvas_XLookAheadClass;
typedef struct _GstVvas_XLookAheadPrivate GstVvas_XLookAheadPrivate;

struct _GstVvas_XLookAhead {
  GstBaseTransform element;
  GstVvas_XLookAheadPrivate *priv;
};

struct _GstVvas_XLookAheadClass {
  GstBaseTransformClass parent_class;
};

typedef enum
{
  VVAS_LA_SPATIAL_AQ_NONE,
  VVAS_LA_SPATIAL_AQ_AUTOVARIANCE,
  VVAS_LA_SPATIAL_AQ_ACTIVITY
} VvasLASpatialMode;

typedef enum
{
  VVAS_LA_TEMPORAL_AQ_NONE,
  VVAS_LA_TEMPORAL_AQ_LINEAR,
} VvasLATemporalMode;

#define GST_TYPE_VVAS_XLOOKAHEAD_SPATIAL_AQ_MODE (vvas_xlookahead_get_spatial_aq_mode ())
#define GST_TYPE_VVAS_XLOOKAHEAD_TEMPORAL_AQ_MODE (vvas_xlookahead_get_temporal_aq_mode ())
#define GST_TYPE_VVAS_XLOOKAHEAD_CODEC_TYPE (vvas_xlookahead_get_codec_type ())

GType gst_vvas_xlookahead_get_type (void);

GType vvas_xlookahead_get_spatial_aq_mode (void);
GType vvas_xlookahead_get_temporal_aq_mode (void);
GType vvas_xlookahead_get_codec_type (void);

G_END_DECLS

#endif /* __GST_VVAS_XLOOKAHEAD_H__ */
