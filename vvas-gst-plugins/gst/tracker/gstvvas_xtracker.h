/*
 * Copyright 2020 - 2022 Xilinx, Inc.
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

#ifndef __GST_VVAS_XTRACKER_H__
#define __GST_VVAS_XTRACKER_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/** @def GST_TYPE_VVAS_XTRACKER
 *  @brief Macro to get GstVvas_XTracker object type
 */
#define GST_TYPE_VVAS_XTRACKER (gst_vvas_xtracker_get_type())

/** @def GST_VVAS_XTRACKER
 *  @brief Macro to typecast parent object to GstVvas_XTracker object
 */
#define GST_VVAS_XTRACKER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XTRACKER,GstVvas_XTracker))

/** @def GST_VVAS_XTRACKER_CLASS
 *  @brief Macro to typecast parent class object to GstVvas_XTrackerClass object
 */
#define GST_VVAS_XTRACKER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XTRACKER,GstVvas_XTrackerClass))

/** @def GST_IS_VVAS_XTRACKER
 *  @brief Macro to validate whether object is of GstVvas_XTracker type
 */
#define GST_IS_VVAS_XTRACKER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XTRACKER))

/** @def GST_IS_VVAS_XTRACKER_CLASS
 *  @brief Macro to validate whether object class  is of GstVvas_XTrackerClass type
 */
#define GST_IS_VVAS_XTRACKER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XTRACKER))

typedef struct _GstVvas_XTracker GstVvas_XTracker;
typedef struct _GstVvas_XTrackerClass GstVvas_XTrackerClass;
typedef struct _GstVvas_XTrackerPrivate GstVvas_XTrackerPrivate;

/** @struct _GstVvas_XTracker
 *  @brief Structure to create GstVvas_XTracker object from GstBaseTransform
 */
struct _GstVvas_XTracker {
  /** parent of GstVvas_XTracker object */
  GstBaseTransform element;
  /** Pointer instance's private structure for each of access */
  GstVvas_XTrackerPrivate *priv;
  /** Algorithm to be used for tracking objects */
  guint32 tracker_algo;
  /** Search scales to be used for object matching */
  guint32 search_scale;
  /** Color space used for matching objects */
  guint32 match_color;
};

/** @struct _GstVvas_XTrackerClass
 *  @brief Structure to create GstVvas_XTrackerClass from GstBaseTransformClass
 */
struct _GstVvas_XTrackerClass {
  /** parent class */
  GstBaseTransformClass parent_class;
};

GType gst_vvas_xtracker_get_type (void);

G_END_DECLS

#endif /* __GST_VVAS_XTRACKER_H__ */
