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

#ifndef __GST_VVAS_XMETAAFFIXER_H__
#define __GST_VVAS_XMETAAFFIXER_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <gst/video/video.h>
#include <gst/base/gstflowcombiner.h>
#include <gst/vvas/gstinferencemeta.h>

G_BEGIN_DECLS
/** @def GST_TYPE_VVAS_XMETAAFFIXER
 *  @brief Macro to get GstVvas_XMetaAffixer object type
 */
#define GST_TYPE_VVAS_XMETAAFFIXER \
  (gst_vvas_xmetaaffixer_get_type())
/** @def GST_VVAS_XMETAAFFIXER
 *  @brief Macro to typecast parent object to GstVvas_XMetaAffixer object
 */
#define GST_VVAS_XMETAAFFIXER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XMETAAFFIXER,GstVvas_XMetaAffixer))
/** @def GST_VVAS_XMETAAFFIXER_CLASS
 *  @brief Macro to typecast parent class object to GstVvas_XMetaAffixerClass object
 */
#define GST_VVAS_XMETAAFFIXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VVAS_XMETAAFFIXER,GstVvas_XMetaAffixerClass))
/** @def GST_IS_VVAS_XMETAAFFIXER
 *  @brief Macro to validate whether object is of GstVvas_XMetaAffixer type
 */
#define GST_IS_VVAS_XMETAAFFIXER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XMETAAFFIXER))
/** @def GST_IS_VVAS_XMETAAFFIXER_CLASS
 *  @brief Macro to validate whether object class is of GstVvas_XMetaAffixerClass type
 */
#define GST_IS_VVAS_XMETAAFFIXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XMETAAFFIXER))
/** @def GST_VVAS_XMETAAFFIXER_GET_CLASS
 *  @brief Macro to get object GstVvas_XMetaAffixerClass object from GstVvas_XMetaAffixer object
 */
#define GST_VVAS_XMETAAFFIXER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VVAS_XMETAAFFIXER, GstVvas_XMetaAffixerClass))
/** @def GST_TYPE_VVAS_XMETAAFFIXER_PAD
 *  @brief Macro to get GstVvas_XMetaAffixerPad object type
 */
#define GST_TYPE_VVAS_XMETAAFFIXER_PAD (gst_vvas_xmetaaffixer_pad_get_type())
/** @def GST_VVAS_XMETAAFFIXER_PAD
 *  @brief Macro to typecast parent object to GstVvas_XMetaAffixerPad object
 */
#define GST_VVAS_XMETAAFFIXER_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VVAS_XMETAAFFIXER_PAD, GstVvas_XMetaAffixerPad))
/** @def GST_VVAS_XMETAAFFIXER_PAD_CLASS
 *  @brief Macro to typecast parent class object to GstVvas_XMetaAffixerPadClass object
 */
#define GST_VVAS_XMETAAFFIXER_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_MIXER_PAD, GstVvas_XMetaAffixerPadClass))
/** @def GST_IS_VVAS_XMETAAFFIXER_PAD
 *  @brief Macro to validate whether object is of GstVvas_XMetaAffixerPad type
 */
#define GST_IS_VVAS_XMETAAFFIXER_PAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VVAS_XMETAAFFIXER_PAD))
/** @def GST_IS_VVAS_XMETAAFFIXER_PAD_CLASS
 *  @brief Macro to validate whether object class is of GstVvas_XMetaAffixerPadClass type
 */
#define GST_IS_VVAS_XMETAAFFIXER_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VVAS_XMETAAFFIXER_PAD))
typedef struct _GstVvas_XMetaAffixer GstVvas_XMetaAffixer;
typedef struct _GstVvas_XMetaAffixerClass GstVvas_XMetaAffixerClass;
typedef struct _GstVvas_XMetaAffixerPad GstVvas_XMetaAffixerPad;
typedef struct _GstVvas_XMetaAffixerPadClass GstVvas_XMetaAffixerPadClass;
typedef struct _GstVvas_XMetaAffixerCollectData GstVvas_XMetaAffixerCollectData;

typedef enum
{
  VVAS_XMETAAFFIXER_STATE_FIRST_BUFFER,
  VVAS_XMETAAFFIXER_STATE_DROP_BUFFER,
  VVAS_XMETAAFFIXER_STATE_PROCESS_BUFFER,
} VVAS_XMETAAFFIXER_STREAM_STATE;

struct _GstVvas_XMetaAffixerCollectData
{
  /* we extend the CollectData */
  GstCollectData collectdata;
  GstVvas_XMetaAffixerPad *sinkpad;
};

struct _GstVvas_XMetaAffixerPad
{
  /** parent of GstVvas_XMetaAffixerPad object */
  GstPad parent;
  /** Pointer to GstVvas_XMetaAffixerCollectData object */
  GstVvas_XMetaAffixerCollectData *collect;
  /** Pointer to src pad object corresponding sink pad */
  GstPad *srcpad;
  /** Width of the input frame */
  guint width;
  /** Height of the input frame */
  guint height;
  /** Frame duration */
  GstClockTime duration;
  /** Presentation timestamp of the current frame */
  GstClockTime curr_pts;
  /** flag indicating EOS received on this pad */
  gboolean eos_received;
  /** GstVideoInfo object */
  GstVideoInfo vinfo;
  /** Status of last operation on this pad */
  GstFlowReturn fret;
  /** Flag indicates if EOS event is sent on the corresponding src pad */
  gboolean sent_eos;
  /** State of the data on this pad */
  VVAS_XMETAAFFIXER_STREAM_STATE stream_state;
};

#define MAX_SLAVE_SOURCES 16

struct _GstVvas_XMetaAffixer
{
  /** Base class object */
  GstElement element;
  /** Pointer to GstCollectPads object */
  GstCollectPads *collect;
  /** Pointer to master sink pad object */
  GstVvas_XMetaAffixerPad *sink_master;
  /** Array of slave sink pad object */
  GstVvas_XMetaAffixerPad *sink_slave[MAX_SLAVE_SOURCES];
  /** Pointer to GstFlowCombiner object */
  GstFlowCombiner *flowcombiner;
  /** Number of slave sink pads created */
  guint num_slaves;
  /** Flag indicating use of PTS for meta data attachment */
  gboolean sync;
  /** End time of previous buffer on master sink pad */
  GstClockTime prev_m_end_ts;
  /** Buffer holding meta data of previous buffer on master sink pad
   * buffer holds metadata only but not data */
  GstBuffer *prev_meta_buf;
  /** Pointer to GThread onject */
  GThread *timeout_thread;
  /** Duration to wait before triggering recovery action in case
   *  data flow getting stuck */
  gint64 retry_timeout;
  /** Flag indicating timeout has hit */
  gboolean timeout_issued;
  /** Flag indicating thread to be started */
  gboolean start_thread;
  /** Flag indicating thread to be exited */
  gboolean stop_thread;
  /** Condition for timeout */
  GCond timeout_cond;
  /** Lock to protect handling related to timeout hit condition */
  GMutex timeout_lock;
  /** Lock to protect handling related to all pads */
  GMutex collected_lock;
};

struct _GstVvas_XMetaAffixerPadClass
{
  /** Parent class object */
  GstPadClass parent_class;
};

struct _GstVvas_XMetaAffixerClass
{
  /** Parent class object */
  GstElementClass parent_class;
};

GType gst_vvas_xmetaaffixer_get_type (void);
GType gst_vvas_xmetaaffixer_pad_get_type (void);

G_END_DECLS
#endif /* __GST_VVAS_XMETAAFFIXER_H__ */
