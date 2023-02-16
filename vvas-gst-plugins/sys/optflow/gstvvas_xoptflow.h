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

#ifndef __GSTVVAS_XOPTFLOW_H__
#define __GSTVVAS_XOPTFLOW_H__
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/** @def GST_TYPE_VVAS_XOPTFLOW
 *  @brief Macro to get GstVvas_XOptflow object type
 */
#define GST_TYPE_VVAS_XOPTFLOW (gst_vvas_xoptflow_get_type())

/** @def GST_VVAS_XOPTFLOW
 *  @brief Macro to typecast parent object to GstVvas_XOptflow object
 */
#define GST_VVAS_XOPTFLOW(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_VVAS_XOPTFLOW, GstVvas_XOptflow))

/** @def GST_VVAS_XOPTFLOW_CLASS
 *  @brief Macro to typecast parent class object to GstVvas_XOptflowClass object
 */
#define GST_VVAS_XOPTFLOW_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_VVAS_XOPTFLOW, GstVvas_XOptflowClass))

/** @def GST_IS_VVAS_XOPTFLOW
 *  @brief Macro to validate whether object is of GstVvas_XOptflow type
 */
#define GST_IS_VVAS_XOPTFLOW(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VVAS_XOPTFLOW))

/** @def GST_IS_VVAS_XOPTFLOW_CLASS
 *  @brief Macro to validate whether object class  is of GstVvas_XOptflowClass type
 */
#define GST_IS_VVAS_XOPTFLOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VVAS_XOPTFLOW))

typedef struct _GstVvas_XOptflow GstVvas_XOptflow;
typedef struct _GstVvas_XOptflowClass GstVvas_XOptflowClass;
typedef struct _GstVvas_XOptflowPrivate GstVvas_XOptflowPrivate;

/** @struct _GstVvas_XOptflow
 *  @brief Structure to create GstVvas_XOptflow object from GstBaseTransform
 */
struct  _GstVvas_XOptflow {
  /** parent of GstVvas_XOptflow object */
  GstBaseTransform element;

  /** Pointer instance's private structure for each of access */
  GstVvas_XOptflowPrivate *priv;
  guint in_mem_bank;
};

/** @struct _GstVvas_XOptflowClass
 *  @brief Structure to create GstVvas_XOptflowClass from GstBaseTransformClass
 */
struct _GstVvas_XOptflowClass{
  /** parent class */
  GstBaseTransformClass parent_class;
};

GType gst_vvas_xoptflow_get_type(void);

G_END_DECLS


#endif /*  __GSTVVAS_XOPTFLOW_H__ */
