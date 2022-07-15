/*
 * Copyright (C) 2022 Xilinx, Inc.  All rights reserved.
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

/* GstVvas_Xfunnel
 * The vvas_xfunnel element serializes streams following round robin algorithm
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/vvas/gstvvassrcidmeta.h>
#include "gstvvas_xfunnel.h"

GST_DEBUG_CATEGORY_STATIC (gst_vvas_xfunnel_debug_category);
#define gst_vvas_xfunnel_parent_class parent_class
#define pad_parent_class gst_vvas_xfunnel_pad_parent_class
#define GST_CAT_DEFAULT gst_vvas_xfunnel_debug_category

#define MAX_SINK_PADS     256
#define DEFAULT_SINK_QUEUE_SIZE   2
#define DEFAULT_SINKWAIT_TIMEOUT  33

enum
{
  PROP_0,
  PROP_QUEUE_SIZE,
  PROP_SINK_WAIT_TIMEOUT,
};

typedef enum
{
  CUSTOM_EVENT_PAD_ADDED,
  CUSTOM_EVENT_PAD_REMOVED,
  CUSTOM_EVENT_PAD_EOS,
  CUSTOM_EVENT_SEGMENT
} Custom_Events;


static void gst_vvas_xfunnel_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vvas_xfunnel_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec);
static void gst_vvas_xfunnel_dispose (GObject * object);
static void gst_vvas_xfunnel_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn gst_vvas_xfunnel_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_vvas_xfunnel_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_vvas_xfunnel_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstPad *gst_vvas_xfunnel_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static GstFlowReturn gst_vvas_xfunnel_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static inline void gst_vvas_xfunnel_add_sink_pad_to_event (GstVvas_Xfunnel *
    funnel, GstEvent * event, guint pad_index);
static GstEvent *gst_vvas_xfunnel_create_custom_event (Custom_Events event_type,
    guint pad_index);
static gboolean gst_vvas_xfunnel_send_event (const GstVvas_Xfunnel *
    vvas_xfunnel, GstEvent * event);
static gboolean gst_vvas_xfunnel_is_all_pad_got_eos (GstVvas_Xfunnel *
    vvas_xfunnel);
static void gst_vvas_xfunnelpad_dispose (GObject * object);

/* pad templates */
static GstStaticPadTemplate vvas_xfunnel_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate vvas_xfunnel_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void
gst_vvas_xfunnelpad_dispose (GObject * object)
{
  GstVvas_XfunnelPad *vvas_xfunnelpad = GST_VVAS_XFUNNEL_PAD (object);

  GST_DEBUG_OBJECT (vvas_xfunnelpad, "disposing pad: %s, queue_len: %u",
      vvas_xfunnelpad->name, g_queue_get_length (vvas_xfunnelpad->queue));
  g_free (vvas_xfunnelpad->name);
  g_queue_free (vvas_xfunnelpad->queue);
  g_mutex_clear (&vvas_xfunnelpad->lock);
  g_cond_clear (&vvas_xfunnelpad->cond);
  vvas_xfunnelpad->name = NULL;
  vvas_xfunnelpad->queue = NULL;
  G_OBJECT_CLASS (pad_parent_class)->dispose (object);
}

static void
gst_vvas_xfunnel_pad_class_init (GstVvas_XfunnelPadClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose = gst_vvas_xfunnelpad_dispose;
}

static void
gst_vvas_xfunnel_pad_init (GstVvas_XfunnelPad * pad)
{
  pad->got_eos = FALSE;
  pad->pad_idx = 0;
  pad->is_eos_sent = FALSE;
  pad->name = NULL;
  pad->time = -1;
  pad->queue = g_queue_new ();
  g_mutex_init (&pad->lock);
  g_cond_init (&pad->cond);
}

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstVvas_Xfunnel, gst_vvas_xfunnel, GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (gst_vvas_xfunnel_debug_category, "vvas_xfunnel", 0,
        "debug category for vvas_xfunnel element"));

static void
gst_vvas_xfunnel_class_init (GstVvas_XfunnelClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_get_property);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_dispose);

  /*install properties */
  g_object_class_install_property (gobject_class, PROP_QUEUE_SIZE,
      g_param_spec_uint ("queue-size", "size of the queue on each pad",
          "Queue size for each sink pad", 1, 100, DEFAULT_SINK_QUEUE_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_SINK_WAIT_TIMEOUT,
      g_param_spec_uint ("sink-wait-timeout",
          "sink wait timeout in milliseconds",
          "time to wait before switching to the next sink in milliseconds, "
          "default will be calculated as 1/FPS", 1, 1000,
          DEFAULT_SINKWAIT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Xilinx round robin funnel plugin", "Generic",
      "Funnel to serialize streams using round robin algorithm(N-to-1 pipe fitting)",
      "Xilinx Inc <https://www.xilinx.com/>");

  gst_element_class_add_static_pad_template (element_class,
      &vvas_xfunnel_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &vvas_xfunnel_src_template);

  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_release_pad);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_change_state);
}

static void
gst_vvas_xfunnel_init (GstVvas_Xfunnel * vvas_xfunnel)
{

  vvas_xfunnel->srcpad = gst_pad_new_from_static_template
      (&vvas_xfunnel_src_template, "src");
  gst_pad_use_fixed_caps (vvas_xfunnel->srcpad);

  gst_element_add_pad (GST_ELEMENT (vvas_xfunnel), vvas_xfunnel->srcpad);

  gst_pad_set_event_function (vvas_xfunnel->srcpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_src_event));

  vvas_xfunnel->processing_thread = NULL;
  vvas_xfunnel->sink_pad_idx = 0;
  vvas_xfunnel->is_exit_thread = FALSE;
  vvas_xfunnel->queue_size = DEFAULT_SINK_QUEUE_SIZE;
  vvas_xfunnel->sink_wait_timeout = DEFAULT_SINKWAIT_TIMEOUT;
  vvas_xfunnel->last_fret = GST_FLOW_OK;
  vvas_xfunnel->sink_caps = NULL;
  vvas_xfunnel->is_user_timeout = FALSE;
  g_mutex_init (&vvas_xfunnel->mutex_lock);
}

static void
gst_vvas_xfunnel_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{

  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (object);

  switch (property_id) {
    case PROP_QUEUE_SIZE:
      vvas_xfunnel->queue_size = g_value_get_uint (value);
      GST_DEBUG_OBJECT (vvas_xfunnel, "queue_size set to %u",
          vvas_xfunnel->queue_size);
      break;

    case PROP_SINK_WAIT_TIMEOUT:
      vvas_xfunnel->sink_wait_timeout = g_value_get_uint (value);
      GST_DEBUG_OBJECT (vvas_xfunnel, "sink_wait_timeout set to %u",
          vvas_xfunnel->sink_wait_timeout);
      vvas_xfunnel->is_user_timeout = TRUE;
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_vvas_xfunnel_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{

  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (object);

  switch (property_id) {
    case PROP_QUEUE_SIZE:
      g_value_set_uint (value, vvas_xfunnel->queue_size);
      break;

    case PROP_SINK_WAIT_TIMEOUT:
      g_value_set_uint (value, vvas_xfunnel->sink_wait_timeout);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_vvas_xfunnel_dispose (GObject * object)
{

  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (object);
  GList *item;

  GST_DEBUG_OBJECT (vvas_xfunnel, "dispose");
  if (vvas_xfunnel->sink_caps) {
    gst_caps_unref (vvas_xfunnel->sink_caps);
    vvas_xfunnel->sink_caps = NULL;
  }
  g_mutex_clear (&vvas_xfunnel->mutex_lock);

restart:
  for (item = GST_ELEMENT_PADS (object); item; item = g_list_next (item)) {
    GstPad *pad = GST_PAD (item->data);
    if (GST_PAD_IS_SINK (pad)) {
      gst_element_release_request_pad (GST_ELEMENT (object), pad);
      goto restart;
    }
  }
  G_OBJECT_CLASS (gst_vvas_xfunnel_parent_class)->dispose (object);
}

static inline void
gst_vvas_xfunnel_add_sink_pad_to_event (GstVvas_Xfunnel * funnel,
    GstEvent * event, guint pad_index)
{

  GstStructure *st = gst_event_writable_structure (event);
  if (st) {
    gst_structure_set (st, "pad-index", G_TYPE_UINT, pad_index, NULL);
  }
}

static GstEvent *
gst_vvas_xfunnel_create_custom_event (Custom_Events event_type, guint pad_idx)
{

  GstStructure *event_struct = NULL;
  GstEvent *event = NULL;

  switch (event_type) {
    case CUSTOM_EVENT_PAD_ADDED:
      event_struct = gst_structure_new ("pad-added", "pad-index", G_TYPE_UINT,
          pad_idx, NULL);
      break;

    case CUSTOM_EVENT_PAD_REMOVED:
      event_struct = gst_structure_new ("pad-removed", "pad-index", G_TYPE_UINT,
          pad_idx, NULL);
      break;

    case CUSTOM_EVENT_PAD_EOS:
      event_struct = gst_structure_new ("pad-eos", "pad-index", G_TYPE_UINT,
          pad_idx, NULL);
      break;

    case CUSTOM_EVENT_SEGMENT:
      event_struct = gst_structure_new ("segment", "pad-index", G_TYPE_UINT,
          pad_idx, NULL);
      break;

    default:
      break;
  }

  if (event_struct) {
    event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, event_struct);
  }
  return event;
}

static gboolean
gst_vvas_xfunnel_send_event (const GstVvas_Xfunnel * funnel, GstEvent * event)
{

  gboolean res = FALSE;

  GST_PAD_STREAM_LOCK (funnel->srcpad);
  GST_DEBUG_OBJECT (funnel, "sending event: %" GST_PTR_FORMAT, event);
  res = gst_pad_push_event (funnel->srcpad, event);
  GST_PAD_STREAM_UNLOCK (funnel->srcpad);

  if (!res) {
    GST_ERROR_OBJECT (funnel, "Failed to send event");
  }
  return res;
}

static gboolean
gst_vvas_xfunnel_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  /*
   * TODO: currently dropping all upstream events as pad-index is not available
   * in event, so to which sink pad to forward this event?
   * When this events needs to be supported, event should have info of pad-index
   * to which it should be forwarded.
   */

  GST_DEBUG_OBJECT (pad, "Dropping event %" GST_PTR_FORMAT, event);
  gst_event_unref (event);
  return TRUE;
}

static GstPad *
gst_vvas_xfunnel_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstPad *sinkpad = NULL;
  GstVvas_XfunnelPad *fpad;
  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (element);
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  guint serial = 0;
  gchar *pad_name = NULL;

  GST_DEBUG_OBJECT (element, "requesting pad, name: %s", name);

  if (element->numsinkpads > MAX_SINK_PADS) {
    GST_ERROR_OBJECT (vvas_xfunnel, "Request pad limit reached");
    GST_ELEMENT_ERROR (vvas_xfunnel, STREAM, FAILED,
        ("Request pad limit reached!"), (NULL));
    return NULL;
  }

  if (templ != gst_element_class_get_pad_template (klass, "sink_%u"))
    return NULL;

  g_mutex_lock (&vvas_xfunnel->mutex_lock);
  if (name == NULL || strlen (name) < 6 || !g_str_has_prefix (name, "sink_")) {
    /* no name given when requesting the pad, use next available int */
    serial = vvas_xfunnel->sink_pad_idx;
  } else {
    /* parse serial number from requested padname */
    serial = g_ascii_strtoull (&name[5], NULL, 10);
    if (serial >= vvas_xfunnel->sink_pad_idx)
      vvas_xfunnel->sink_pad_idx = serial;
  }

  /* create new pad with the name */
  pad_name = g_strdup_printf ("sink_%u", serial);
  sinkpad = GST_PAD_CAST (g_object_new (GST_TYPE_VVAS_XFUNNEL_PAD,
          "name", pad_name, "direction", templ->direction, "template",
          templ, NULL));
  g_free (pad_name);

  fpad = GST_VVAS_XFUNNEL_PAD_CAST (sinkpad);
  fpad->pad_idx = vvas_xfunnel->sink_pad_idx++;
  g_mutex_unlock (&vvas_xfunnel->mutex_lock);

  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_sink_chain));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_sink_event));

  GST_OBJECT_FLAG_SET (sinkpad, GST_PAD_FLAG_PROXY_CAPS);
  GST_OBJECT_FLAG_SET (sinkpad, GST_PAD_FLAG_PROXY_ALLOCATION);

  gst_pad_set_active (sinkpad, TRUE);
  gst_element_add_pad (element, sinkpad);

  GST_DEBUG_OBJECT (element, "requested pad %s:%s, pad_idx: %u",
      GST_DEBUG_PAD_NAME (sinkpad), fpad->pad_idx);

  fpad->name = gst_pad_get_name (sinkpad);

#if 0
  if (GST_PAD_IS_ACTIVE (vvas_xfunnel->srcpad)) {
    /* If pad is added before src_pad is activated, send_event will fail.
     * As of now, defunnel is using STREAM_START event to create new pad
     */
    GstEvent *pad_added_event = NULL;
    pad_added_event =
        gst_vvas_xfunnel_create_custom_event (CUSTOM_EVENT_PAD_ADDED,
        fpad->pad_idx);

    if (pad_added_event) {
      gst_vvas_xfunnel_send_event (vvas_xfunnel, pad_added_event);
    }
  } else {
    GST_DEBUG_OBJECT (vvas_xfunnel,
        "src_pad is not active, unable to send custom events");
  }
#endif

  return sinkpad;
}

static void
gst_vvas_xfunnel_release_pad (GstElement * element, GstPad * pad)
{

  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (element);
  GstVvas_XfunnelPad *fpad = GST_VVAS_XFUNNEL_PAD_CAST (pad);
  GstEvent *pad_removed_event = NULL, *custom_eos_event = NULL;
  gboolean send_eos_event = FALSE;

  GST_DEBUG_OBJECT (vvas_xfunnel, "releasing pad %s:%s, pad_idx: %u",
      GST_DEBUG_PAD_NAME (pad), fpad->pad_idx);

  g_mutex_lock (&fpad->lock);
  if (!fpad->got_eos) {
    //Pad is removed, but we didn't get EOS, need to send custom EOS from here
    fpad->got_eos = TRUE;
    send_eos_event = TRUE;
  }

  if (GST_PAD_IS_ACTIVE (vvas_xfunnel->srcpad)) {
    if (send_eos_event) {
      custom_eos_event =
          gst_vvas_xfunnel_create_custom_event (CUSTOM_EVENT_PAD_EOS,
          fpad->pad_idx);
    }

    pad_removed_event =
        gst_vvas_xfunnel_create_custom_event (CUSTOM_EVENT_PAD_REMOVED,
        fpad->pad_idx);

    if (custom_eos_event) {
      gst_vvas_xfunnel_send_event (vvas_xfunnel, custom_eos_event);
    }

    if (pad_removed_event) {
      /* If sink_pad is removed after pipeline EOS, this send event will fail,
       * because src_pad will be marked for flushing. */
      gst_vvas_xfunnel_send_event (vvas_xfunnel, pad_removed_event);
    }
  }
  g_mutex_unlock (&fpad->lock);
  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (GST_ELEMENT_CAST (vvas_xfunnel), pad);

}

static GstFlowReturn
gst_vvas_xfunnel_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{

  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (parent);
  GstVvas_XfunnelPad *fpad = GST_VVAS_XFUNNEL_PAD_CAST (pad);
  GstVvasSrcIDMeta *meta;
  guint queue_len = 0;

  GST_LOG_OBJECT (fpad, "pad_%u received %" GST_PTR_FORMAT, fpad->pad_idx,
      buffer);

  buffer = gst_buffer_make_writable (buffer);

  meta = gst_buffer_add_vvas_srcid_meta (buffer);
  meta->src_id = fpad->pad_idx;

  g_mutex_lock (&fpad->lock);
  queue_len = g_queue_get_length (fpad->queue);
  if (queue_len >= vvas_xfunnel->queue_size) {
    GST_LOG_OBJECT (fpad, "wait for free space in the queue[%u]", queue_len);
    g_cond_wait (&fpad->cond, &fpad->lock);
  }
  g_queue_push_tail (fpad->queue, buffer);
  g_cond_signal (&fpad->cond);
  g_mutex_unlock (&fpad->lock);

  return vvas_xfunnel->last_fret;
}

static gboolean
gst_vvas_xfunnel_is_all_pad_got_eos (GstVvas_Xfunnel * vvas_xfunnel)
{

  GstElement *element = GST_ELEMENT_CAST (vvas_xfunnel);
  GstIterator *itr;
  GValue item = { 0 };
  gboolean all_eos = FALSE;
  gboolean done = FALSE;

  GST_OBJECT_LOCK (vvas_xfunnel);
  if (element->numsinkpads == 0) {
    GST_OBJECT_UNLOCK (vvas_xfunnel);
    goto done;
  }
  GST_OBJECT_UNLOCK (vvas_xfunnel);

  itr = gst_element_iterate_sink_pads (element);

  while (!done) {
    switch (gst_iterator_next (itr, &item)) {
      case GST_ITERATOR_OK:{
        GstVvas_XfunnelPad *sinkpad;
        GstPad *pad;

        pad = g_value_get_object (&item);
        sinkpad = GST_VVAS_XFUNNEL_PAD_CAST (pad);

        g_mutex_lock (&sinkpad->lock);
        if (!sinkpad->got_eos || !sinkpad->is_eos_sent) {
          done = TRUE;
        }
        g_mutex_unlock (&sinkpad->lock);
        g_value_reset (&item);
      }
        break;

      case GST_ITERATOR_DONE:{
        done = TRUE;
        all_eos = TRUE;
      }
        break;

      case GST_ITERATOR_RESYNC:{
        gst_iterator_resync (itr);
        GST_DEBUG_OBJECT (element, "itr resync");
      }
        break;

      default:{
        done = TRUE;
      }
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (itr);

done:
  return all_eos;
}

static gboolean
gst_vvas_xfunnel_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{

  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (parent);
  GstVvas_XfunnelPad *fpad = GST_VVAS_XFUNNEL_PAD_CAST (pad);
  gboolean forward = TRUE;
  gboolean res = TRUE;
  GstEvent *local_event = gst_event_copy (event);

  gst_event_unref (event);
  event = local_event;

  GST_DEBUG_OBJECT (pad, "received event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {

    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:{
      forward = FALSE;
    }
      break;

    case GST_EVENT_CAPS:{
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);
      if (!vvas_xfunnel->sink_caps) {
        //first caps event, need to forward this downstream
        vvas_xfunnel->sink_caps = gst_caps_copy (caps);

        if (!vvas_xfunnel->is_user_timeout) {
          GstVideoInfo vinfo = { 0 };
          gint fps_d, fps_n;
          gint delta;

          if (!gst_video_info_from_caps (&vinfo, caps)) {
            GST_ERROR_OBJECT (pad, "Failed to parse caps");
            return FALSE;
          }

          fps_d = GST_VIDEO_INFO_FPS_D (&vinfo);
          fps_n = GST_VIDEO_INFO_FPS_N (&vinfo);
          vvas_xfunnel->sink_wait_timeout =
              (guint) ((fps_d * 1000) / (float) fps_n);
          delta = (vvas_xfunnel->sink_wait_timeout * 10) / 100;
          //consider 10% as delta
          vvas_xfunnel->sink_wait_timeout += delta;
          GST_DEBUG_OBJECT (vvas_xfunnel, "sink_wait_timeout: %u ms",
              vvas_xfunnel->sink_wait_timeout);
        }
      } else {
        GstStructure *caps_struct;
        GstStructure *new_struct;
        gint caps_width, new_width, caps_height, new_height;
        gint caps_fps_num, caps_fps_den, new_fps_num, new_fps_den;
        const gchar *caps_format, *new_format;
        forward = FALSE;

        caps_struct = gst_caps_get_structure (vvas_xfunnel->sink_caps, 0);
        new_struct = gst_caps_get_structure (caps, 0);
        if (!caps_struct || !new_struct) {
          goto error;
        }
        caps_format = gst_structure_get_string (caps_struct, "format");
        new_format = gst_structure_get_string (new_struct, "format");
        if (!caps_format || !new_format) {
          goto error;
        }
        if (!gst_structure_get_int (caps_struct, "width", &caps_width)) {
          goto error;
        }
        if (!gst_structure_get_int (caps_struct, "height", &caps_height)) {
          goto error;
        }
        if (!gst_structure_get_fraction (caps_struct, "framerate",
                &caps_fps_num, &caps_fps_den)) {
          goto error;
        }
        if (!gst_structure_get_int (new_struct, "width", &new_width)) {
          goto error;
        }
        if (!gst_structure_get_int (new_struct, "height", &new_height)) {
          goto error;
        }
        if (!gst_structure_get_fraction (new_struct, "framerate", &new_fps_num,
                &new_fps_den)) {
          goto error;
        }

        if ((caps_width != new_width) || (caps_height != new_height)
            || (caps_fps_num != new_fps_num) || (caps_fps_den != new_fps_den)
            || g_strcmp0 (caps_format, new_format)) {
          /* caps are not equal, this is not expected! */
          GST_ERROR_OBJECT (vvas_xfunnel,
              "caps on all sink pads must be same!");
          GST_ELEMENT_ERROR (vvas_xfunnel, STREAM, FAILED,
              ("caps on all sink pads must be same!"), (NULL));
          gst_event_unref (event);
          return FALSE;
        }
      }
    }
      break;
    error:
      GST_ERROR_OBJECT (vvas_xfunnel,
          "video resolution/format not available in caps");
      gst_event_unref (event);
      return FALSE;
      break;

    case GST_EVENT_SEGMENT:{
      const GstSegment *segment;
      GstStructure *seg_struct;
      GstEvent *seg_event;

      //Need to drop this event and create a custom segment event
      forward = FALSE;

      gst_event_parse_segment (event, &segment);
      seg_event = gst_vvas_xfunnel_create_custom_event (CUSTOM_EVENT_SEGMENT,
          fpad->pad_idx);
      if (seg_event) {
        seg_struct = gst_event_writable_structure (seg_event);
        gst_structure_set (seg_struct, "segment_struct", GST_TYPE_SEGMENT,
            segment, NULL);
        res = gst_vvas_xfunnel_send_event (vvas_xfunnel, seg_event);
      }
    }
      break;

    case GST_EVENT_EOS:{
      //Custom pad-eos and EOS will be sent from processing_thread
      g_mutex_lock (&fpad->lock);
      fpad->got_eos = TRUE;
      g_mutex_unlock (&fpad->lock);
      forward = FALSE;
    }
      break;

    default:
      break;
  }

  if (forward) {
    gst_vvas_xfunnel_add_sink_pad_to_event (vvas_xfunnel, event, fpad->pad_idx);
    res = gst_vvas_xfunnel_send_event (vvas_xfunnel, event);
  } else {
    gst_event_unref (event);
  }
  return res;
}

static gpointer
gst_vvas_xfunnel_processing_thread (gpointer data)
{

  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (data);
  GstElement *element = GST_ELEMENT_CAST (vvas_xfunnel);
  gboolean send_segment_event = TRUE;

  GST_DEBUG_OBJECT (vvas_xfunnel, "thread started");

  while (TRUE) {
    GstIterator *itr = NULL;
    GValue item = { 0 };
    gboolean done = FALSE;
    gboolean break_loop = FALSE;

    g_mutex_lock (&vvas_xfunnel->mutex_lock);
    break_loop = vvas_xfunnel->is_exit_thread;
    g_mutex_unlock (&vvas_xfunnel->mutex_lock);

    if (break_loop) {
      GST_DEBUG_OBJECT (vvas_xfunnel, "breaking thread's main loop");
      break;
    }

    GST_OBJECT_LOCK (vvas_xfunnel);
    if (element->numsinkpads == 0) {
      GST_DEBUG_OBJECT (vvas_xfunnel, "no sinkpad yet, waiting");
      GST_OBJECT_UNLOCK (vvas_xfunnel);
      g_usleep (1000);
      /* wait till at least one sink_pad available */
      continue;
    }
    GST_OBJECT_UNLOCK (vvas_xfunnel);

    itr = gst_element_iterate_sink_pads (element);
    while (!done) {
      switch (gst_iterator_next (itr, &item)) {
        case GST_ITERATOR_OK:{
          GstVvas_XfunnelPad *fpad;
          GstPad *pad;
          GstBuffer *buffer = NULL;
          guint queue_len = 0;
          gboolean is_send_pad_eos = FALSE;

          pad = g_value_get_object (&item);
          fpad = GST_VVAS_XFUNNEL_PAD_CAST (pad);

          g_mutex_lock (&fpad->lock);
          queue_len = g_queue_get_length (fpad->queue);

          GST_DEBUG_OBJECT (fpad,
              "pad_%u: queue_len= %u, eos: %d, eos_sent: %d", fpad->pad_idx,
              queue_len, fpad->got_eos, fpad->is_eos_sent);

          if (queue_len > 0) {
            buffer = g_queue_pop_head (fpad->queue);
            fpad->time = g_get_monotonic_time ();
          } else {
            if (!fpad->got_eos) {
              gint64 current_time, elapsed_time;
              gint64 elapsed_miliseconds;
              current_time = g_get_monotonic_time ();

              elapsed_time =
                  (fpad->time >= 0) ? (current_time - fpad->time) : 0;
              elapsed_miliseconds = elapsed_time / G_TIME_SPAN_MILLISECOND;

              if (elapsed_miliseconds < vvas_xfunnel->sink_wait_timeout) {
                //Need to wait still
                gboolean is_signalled;
                gint64 end_time;
                gint64 wait_time = (vvas_xfunnel->sink_wait_timeout *
                    G_TIME_SPAN_MILLISECOND) - elapsed_time;
                GST_DEBUG_OBJECT (fpad, "elapsed: %ld, waiting for %ld in us",
                    elapsed_time, wait_time);

                end_time = g_get_monotonic_time () + wait_time;
                is_signalled =
                    g_cond_wait_until (&fpad->cond, &fpad->lock, end_time);
                if (is_signalled) {
                  if (g_queue_get_length (fpad->queue) > 0) {
                    buffer = g_queue_pop_head (fpad->queue);
                    GST_DEBUG_OBJECT (fpad, "Signaled in %ld in us",
                        (g_get_monotonic_time () - current_time));
                    fpad->time = g_get_monotonic_time ();
                  }
                } else {
                  GST_LOG_OBJECT (fpad, "timeout, skipping pad_%u",
                      fpad->pad_idx);
                }
              } else {
                GST_LOG_OBJECT (fpad,
                    "wait time already elapsed, skipping pad_%u",
                    fpad->pad_idx);
              }
            } else {
              if (!fpad->is_eos_sent) {
                is_send_pad_eos = TRUE;
              }
            }
          }
          g_cond_signal (&fpad->cond);
          g_mutex_unlock (&fpad->lock);

          if (G_LIKELY (buffer)) {
            if (G_UNLIKELY (send_segment_event)) {
              GstSegment *segment;
              GstEvent *seg_event;

              segment = gst_segment_new ();
              gst_segment_init (segment, GST_FORMAT_TIME);
              seg_event = gst_event_new_segment (segment);

              gst_vvas_xfunnel_send_event (vvas_xfunnel, seg_event);

              gst_segment_free (segment);
              send_segment_event = FALSE;
            }

            GST_DEBUG_OBJECT (fpad, "pad_%u: pushing %" GST_PTR_FORMAT,
                fpad->pad_idx, buffer);

            GST_PAD_STREAM_LOCK (vvas_xfunnel->srcpad);
            vvas_xfunnel->last_fret =
                gst_pad_push (vvas_xfunnel->srcpad, buffer);
            GST_PAD_STREAM_UNLOCK (vvas_xfunnel->srcpad);
            GST_DEBUG_OBJECT (vvas_xfunnel, "buffer push res: %s",
                gst_flow_get_name (vvas_xfunnel->last_fret));
          }

          if (G_UNLIKELY (is_send_pad_eos)) {
            GstEvent *custom_eos_event;
            GST_LOG_OBJECT (fpad, "pad_%u is at EOS", fpad->pad_idx);
            custom_eos_event =
                gst_vvas_xfunnel_create_custom_event (CUSTOM_EVENT_PAD_EOS,
                fpad->pad_idx);
            if (custom_eos_event) {
              GST_LOG_OBJECT (fpad, "pad_%u sending pad EOS", fpad->pad_idx);
              if (gst_vvas_xfunnel_send_event (vvas_xfunnel, custom_eos_event)) {
                fpad->is_eos_sent = TRUE;
              }
            }
          }
          g_value_reset (&item);
        }
          break;

        case GST_ITERATOR_DONE:{
          done = TRUE;
        }
          break;

        case GST_ITERATOR_RESYNC:{
          gst_iterator_resync (itr);
          GST_DEBUG_OBJECT (vvas_xfunnel, "itr resync");
        }
          break;

        default:{
          done = TRUE;
        }
          break;
      }
    }
    g_value_unset (&item);
    gst_iterator_free (itr);

    if (G_UNLIKELY (gst_vvas_xfunnel_is_all_pad_got_eos (vvas_xfunnel))) {
      /* all pads are at EOS, send EOS now. */
      GstEvent *eos_event;
      GST_DEBUG_OBJECT (vvas_xfunnel, "all sink pads are at EOS");
      eos_event = gst_event_new_eos ();
      if (eos_event) {
        if (gst_vvas_xfunnel_send_event (vvas_xfunnel, eos_event)) {
          GST_DEBUG_OBJECT (vvas_xfunnel, "all EOS, exiting thread");
          break;
        }
      }
    }
  }
  GST_DEBUG_OBJECT (vvas_xfunnel, "exiting thread");
  return NULL;
}

static GstStateChangeReturn
gst_vvas_xfunnel_change_state (GstElement * element, GstStateChange transition)
{

  GstVvas_Xfunnel *vvas_xfunnel;
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_VVAS_XFUNNEL (element),
      GST_STATE_CHANGE_FAILURE);
  vvas_xfunnel = GST_VVAS_XFUNNEL (element);

  GST_DEBUG_OBJECT (vvas_xfunnel, "transition: %d", transition);

  switch (transition) {

    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG_OBJECT (vvas_xfunnel, "Starting processing thread");
      vvas_xfunnel->is_exit_thread = FALSE;
      vvas_xfunnel->processing_thread = g_thread_new ("vvas_xfunnel-thread",
          gst_vvas_xfunnel_processing_thread, vvas_xfunnel);
      break;

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (vvas_xfunnel->processing_thread) {
        g_mutex_lock (&vvas_xfunnel->mutex_lock);
        vvas_xfunnel->is_exit_thread = TRUE;
        g_mutex_unlock (&vvas_xfunnel->mutex_lock);

        GST_LOG_OBJECT (vvas_xfunnel, "waiting for processing thread to join");
        g_thread_join (vvas_xfunnel->processing_thread);
        vvas_xfunnel->processing_thread = NULL;
        GST_LOG_OBJECT (vvas_xfunnel, "processing thread joined");
      }
      break;

    default:
      break;
  }
  return ret;
}

static gboolean
vvas_xfunnel_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "vvas_xfunnel", GST_RANK_NONE,
      GST_TYPE_VVAS_XFUNNEL);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xfunnel,
    "Xilinx funnel plugin to serialize streams using round robin algorithm",
    vvas_xfunnel_plugin_init, VVAS_API_VERSION, "MIT/X11",
    "Xilinx VVAS SDK plugin", "https://www.xilinx.com/")
