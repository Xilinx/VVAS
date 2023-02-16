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
#ifndef __GST_VVAS_XMSRC_H__
#define __GST_VVAS_XMSRC_H__

#include <gst/gst.h>
#include <gst/vvas/gstvvasallocator.h>
#include <vvas/vvas_kernel.h>

G_BEGIN_DECLS
/** @def GST_TYPE_VVAS_XMSRC
 *  @brief Macro to get GstVvasXMSRC object type
 */
#define GST_TYPE_VVAS_XMSRC (gst_vvas_xmultisrc_get_type())
/** @def GST_VVAS_XMSRC
 *  @brief Macro to typecast parent object to GstVvasXMSRC object
 */
#define GST_VVAS_XMSRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XMSRC,GstVvasXMSRC))
/** @def GST_VVAS_XMSRC_CLASS
 *  @brief Macro to typecast parent class object to GstVvasXMSRCClass object
 */
#define GST_VVAS_XMSRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XMSRC,GstVvasXMSRCClass))
/** @def GST_VVAS_XMSRC_GET_CLASS
 *  @brief Macro to get object GstVvasXMSRCClass object from GstVvasXMSRC object
 */
#define GST_VVAS_XMSRC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_VVAS_XMSRC,GstVvasXMSRCClass))
/** @def GST_IS_VVAS_XMSRC
 *  @brief Macro to validate whether object is of GstVvasXMSRC type
 */
#define GST_IS_VVAS_XMSRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XMSRC))
/** @def GST_IS_VVAS_XMSRC_CLASS
 *  @brief Macro to validate whether object class is of GstVvasXMSRCClass type
 */
#define GST_IS_VVAS_XMSRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XMSRC))
#define MAX_CHANNELS 16
typedef struct _GstVvasXMSRC GstVvasXMSRC;
typedef struct _GstVvasXMSRCClass GstVvasXMSRCClass;
typedef struct _GstVvasXMSRCPrivate GstVvasXMSRCPrivate;

struct _GstVvasXMSRC
{
  /** Parent of GstVvasXMSRC */
  GstElement element;
  /** Pointer to instance's private structure */
  GstVvasXMSRCPrivate *priv;
  /** Pointer to sink pad */
  GstPad *sinkpad;
  /** List of pointer to src pads */
  GList *srcpads;
  /** Hash table to store pad indexes */
  GHashTable *pad_indexes;
  /** number of request pads created */
  guint num_request_pads;
  /** Complete path of xclbin */
  gchar *xclbin_path;
  /** json file describing kernel configuration */
  gchar *config_file;
  /** Dynamically changed kernel configuration */
  gchar *dyn_config;
};

struct _GstVvasXMSRCClass
{
  GstElementClass parent_class;
};

GType gst_vvas_xmultisrc_get_type (void);

G_END_DECLS
#endif /* __GST_VVAS_XMSRC_H__ */
