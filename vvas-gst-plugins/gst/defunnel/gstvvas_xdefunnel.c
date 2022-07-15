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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstvvas_xdefunnel.h"
#include <gst/vvas/gstvvassrcidmeta.h>

GST_DEBUG_CATEGORY_STATIC (gst_vvas_xdefunnel_debug_category);
#define GST_CAT_DEFAULT gst_vvas_xdefunnel_debug_category

enum
{
  PROP_0,
  PROP_ACTIVE_PAD,
  PROP_LAST
};

static GstStaticPadTemplate gst_vvas_xdefunnel_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_vvas_xdefunnel_src_factory =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

#define _do_init \
GST_DEBUG_CATEGORY_INIT (gst_vvas_xdefunnel_debug_category, \
        "vvas_xdefunnel", 0, "Stream demuxer");
#define gst_vvas_xdefunnel_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVvas_XDeFunnel, gst_vvas_xdefunnel,
    GST_TYPE_ELEMENT, _do_init);

static void gst_vvas_xdefunnel_dispose (GObject * object);
static void gst_vvas_xdefunnel_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_vvas_xdefunnel_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static gboolean gst_vvas_xdefunnel_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstStateChangeReturn gst_vvas_xdefunnel_change_state (GstElement *
    element, GstStateChange transition);
static GstPad *gst_vvas_xdefunnel_get_srcpad_by_source_id (GstVvas_XDeFunnel *
    demux, guint source_id);
static gboolean gst_vvas_xdefunnel_srcpad_create (GstVvas_XDeFunnel * demux,
    guint source_id);
static void gst_vvas_xdefunnel_reset (GstVvas_XDeFunnel * demux);

static void
gst_vvas_xdefunnel_class_init (GstVvas_XDeFunnelClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->get_property = gst_vvas_xdefunnel_get_property;
  gobject_class->dispose = gst_vvas_xdefunnel_dispose;

  g_object_class_install_property (gobject_class, PROP_ACTIVE_PAD,
      g_param_spec_object ("active-pad", "Active pad",
          "The currently active src pad", GST_TYPE_PAD,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class, "Stream Demuxer",
      "Generic", "1-to-N output stream demuxer based on source id metadata",
      "Xilinx Inc <www.xilinx.com>");
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_vvas_xdefunnel_sink_factory);

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_vvas_xdefunnel_src_factory);

  gstelement_class->change_state = gst_vvas_xdefunnel_change_state;
}

static void
gst_vvas_xdefunnel_init (GstVvas_XDeFunnel * demux)
{
  demux->sinkpad =
      gst_pad_new_from_static_template (&gst_vvas_xdefunnel_sink_factory,
      "sink");
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xdefunnel_chain));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xdefunnel_event));

  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  /* srcpad management */
  demux->active_srcpad = NULL;
  demux->nb_srcpads = 0;
  demux->sink_caps = NULL;

  /* initialize hash table for srcpad */
  demux->source_id_pairs =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) gst_object_unref);
}

static void
gst_vvas_xdefunnel_dispose (GObject * object)
{
  GstVvas_XDeFunnel *demux = GST_VVAS_XDEFUNNEL (object);

  GST_DEBUG_OBJECT (demux, "dispose");

  if (demux->sink_caps) {
    gst_caps_unref (demux->sink_caps);
    demux->sink_caps = NULL;
  }
  gst_vvas_xdefunnel_reset (demux);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_vvas_xdefunnel_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvas_XDeFunnel *demux = GST_VVAS_XDEFUNNEL (object);

  switch (prop_id) {
    case PROP_ACTIVE_PAD:
      GST_OBJECT_LOCK (demux);
      g_value_set_object (value, demux->active_srcpad);
      GST_OBJECT_UNLOCK (demux);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
forward_caps (GstPad * pad, gpointer user_data)
{
  gboolean bret;
  bret = gst_pad_set_caps (pad, GST_CAPS_CAST (user_data));
  if (!bret)
    GST_ERROR_OBJECT (pad, "forward caps failed");
  return FALSE;
}

static gboolean
gst_vvas_xdefunnel_srcpad_create (GstVvas_XDeFunnel * demux, guint source_id)
{
  gchar *padname = NULL;
  GstPad *srcpad = NULL;
  GstPadTemplate *pad_tmpl = NULL;

  padname = g_strdup_printf ("src_%u", source_id);
  pad_tmpl = gst_static_pad_template_get (&gst_vvas_xdefunnel_src_factory);
  demux->nb_srcpads++;

  GST_LOG_OBJECT (demux, "generating a srcpad:%s", padname);
  srcpad = gst_pad_new_from_template (pad_tmpl, padname);
  gst_object_unref (pad_tmpl);
  g_free (padname);
  g_return_val_if_fail (srcpad != NULL, FALSE);

  demux->active_srcpad = srcpad;
  g_hash_table_insert (demux->source_id_pairs, GUINT_TO_POINTER (source_id),
      gst_object_ref (srcpad));

  return TRUE;
}

static GstFlowReturn
gst_vvas_xdefunnel_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFlowReturn fret = GST_FLOW_OK;
  GstVvas_XDeFunnel *demux = NULL;
  GstPad *srcpad = NULL;
  GstVvasSrcIDMeta *srcid_meta;

  demux = GST_VVAS_XDEFUNNEL (parent);

  srcid_meta = gst_buffer_get_vvas_srcid_meta (buf);
  GST_OBJECT_LOCK (demux);
  if (srcid_meta) {
    srcpad =
        gst_vvas_xdefunnel_get_srcpad_by_source_id (demux, srcid_meta->src_id);
    demux->active_srcpad = srcpad;
    srcpad = gst_object_ref (demux->active_srcpad);
    GST_OBJECT_UNLOCK (demux);
    GST_LOG_OBJECT (demux, "pushing buffer to %" GST_PTR_FORMAT,
        demux->active_srcpad);
    fret = gst_pad_push (srcpad, buf);
    gst_object_unref (srcpad);
  } else {
    GST_ERROR_OBJECT (demux, "source id metadata is not present...");
    GST_OBJECT_UNLOCK (demux);
    goto no_active_srcpad;
  }

  GST_LOG_OBJECT (demux, "handled buffer %s", gst_flow_get_name (fret));
  return fret;

no_active_srcpad:
  {
    GST_WARNING_OBJECT (demux, "srcpad is not initialized");
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstPad *
gst_vvas_xdefunnel_get_srcpad_by_source_id (GstVvas_XDeFunnel * demux,
    guint source_id)
{
  GstPad *srcpad = NULL;

  GST_DEBUG_OBJECT (demux, "source_id = %d", source_id);
  if (demux->source_id_pairs == NULL) {
    GST_ERROR_OBJECT (demux, "source id pairs not available");
    goto done;
  }

  srcpad =
      g_hash_table_lookup (demux->source_id_pairs,
      GUINT_TO_POINTER (source_id));

  if (srcpad) {
    GST_DEBUG_OBJECT (demux, "srcpad = %s:%s matched",
        GST_DEBUG_PAD_NAME (srcpad));
  }

done:
  return srcpad;
}

static gboolean
gst_vvas_xdefunnel_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstVvas_XDeFunnel *demux;
  const gchar *stream_id = NULL;
  const GstStructure *structure = NULL;
  GstPad *srcpad = NULL;
  gboolean bret = TRUE;
  guint pad_idx;

  demux = GST_VVAS_XDEFUNNEL (parent);

  GST_DEBUG_OBJECT (demux, "received event = %s, sticky = %d",
      GST_EVENT_TYPE_NAME (event), GST_EVENT_IS_STICKY (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:{
      structure = gst_event_get_structure (event);
      gst_structure_get_uint (structure, "pad-index", &pad_idx);

      if (!g_strcmp0 (gst_structure_get_name (structure), "pad-removed")) {
        GST_DEBUG_OBJECT (demux, "Got pad-removed event on pad %u", pad_idx);
        srcpad = gst_vvas_xdefunnel_get_srcpad_by_source_id (demux, pad_idx);
        if (srcpad) {
          if (!gst_pad_push_event (srcpad, gst_event_new_eos ())) {
            GST_ERROR_OBJECT (demux, "failed to push eos event on pad %s:%s",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
          if (!gst_pad_set_active (srcpad, FALSE)) {
            GST_ERROR_OBJECT (demux, "failed to deactivate pad %s:%s",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
          if (!gst_element_remove_pad (GST_ELEMENT_CAST (demux), srcpad)) {
            GST_ERROR_OBJECT (demux, "failed to remove pad %s:%s from element",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
          bret =
              g_hash_table_remove (demux->source_id_pairs,
              GINT_TO_POINTER (pad_idx));
          GST_DEBUG_OBJECT (demux, "source pad with id %u removed", pad_idx);
        }
      } else if (!g_strcmp0 (gst_structure_get_name (structure), "pad-eos")) {
        GST_DEBUG_OBJECT (demux, "Got pad-eos event on pad %u", pad_idx);
        gst_event_unref (event);
        srcpad = gst_vvas_xdefunnel_get_srcpad_by_source_id (demux, pad_idx);
        if (srcpad) {
          if (!gst_pad_push_event (srcpad, gst_event_new_eos ())) {
            GST_ERROR_OBJECT (demux, "failed to push eos event on pad %s:%s",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
        }
      } else if (!g_strcmp0 (gst_structure_get_name (structure), "segment")) {
        GstSegment *segment = NULL;
        GstEvent *seg_event = NULL;
        GST_DEBUG_OBJECT (demux, "Got segment event on pad %u", pad_idx);
        if (!gst_structure_get (structure, "segment_struct", GST_TYPE_SEGMENT,
                &segment, NULL)) {
          GST_DEBUG_OBJECT (demux, "failed to read segment_struct field");
          bret = FALSE;
          break;
        } else {
          GST_DEBUG_OBJECT (demux, "segment_struct parsed success");
        }
        srcpad = gst_vvas_xdefunnel_get_srcpad_by_source_id (demux, pad_idx);
        if (!srcpad) {
          GST_ERROR_OBJECT (demux, "source pad with id %u is not present",
              pad_idx);
          gst_event_unref (event);
          bret = FALSE;
          break;
        }
        seg_event = gst_event_new_segment (segment);
        GST_DEBUG_OBJECT (demux, "sending event %" GST_PTR_FORMAT, seg_event);
        if (!gst_pad_push_event (srcpad, seg_event)) {
          GST_ERROR_OBJECT (demux, "failed to push segment event on pad %s:%s",
              GST_DEBUG_PAD_NAME (srcpad));
          bret = FALSE;
          break;
        }
        gst_event_unref (event);
        gst_segment_free (segment);
      }
      break;
    }
    case GST_EVENT_STREAM_START:{
      gst_event_parse_stream_start (event, &stream_id);
      structure = gst_event_get_structure (event);
      gst_structure_get_uint (structure, "pad-index", &pad_idx);
      srcpad = gst_vvas_xdefunnel_get_srcpad_by_source_id (demux, pad_idx);

      if (!srcpad) {
        GST_INFO_OBJECT (demux, "source pad is not present...");
        GST_INFO_OBJECT (demux, "got new source id %u", pad_idx);

        GST_OBJECT_LOCK (demux);
        /* try to generate a srcpad */
        if (gst_vvas_xdefunnel_srcpad_create (demux, pad_idx)) {
          srcpad = demux->active_srcpad;
          GST_OBJECT_UNLOCK (demux);
          if (!gst_pad_set_active (srcpad, TRUE)) {
            GST_ERROR_OBJECT (demux, "failed to activate pad %s:%s",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
          if (!gst_pad_push_event (srcpad, event)) {
            GST_ERROR_OBJECT (demux,
                "failed to push stream start event on pad %s:%s",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
          if (!gst_element_add_pad (GST_ELEMENT_CAST (demux), srcpad)) {
            GST_ERROR_OBJECT (demux, "failed to add pad %s:%s to element",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
          GST_DEBUG_OBJECT (demux, "source pad with id %u added", pad_idx);
        } else {
          GST_OBJECT_UNLOCK (demux);
          GST_ELEMENT_ERROR (demux, STREAM, FAILED,
              ("Error occurred trying to create a srcpad"),
              ("Failed to create a srcpad via source-id"));
          bret = FALSE;
        }
      }
      /* Set caps on new pad */
      if (demux->sink_caps) {
        if (!gst_pad_set_caps (srcpad, demux->sink_caps)) {
          GST_ERROR_OBJECT (demux, "caps could not be set on pad %u", pad_idx);
          bret = FALSE;
        }
      }
      GST_DEBUG_OBJECT (demux, "stream start event on pad %s:%s",
          GST_DEBUG_PAD_NAME (srcpad));
      break;
    }
    case GST_EVENT_CAPS:{
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);
      demux->sink_caps = gst_caps_copy (caps);
      gst_event_unref (event);
      gst_pad_forward (pad, forward_caps, demux->sink_caps);
      break;
    }
    case GST_EVENT_SEGMENT:
    case GST_EVENT_EOS:
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:{
      /* segment and eos events sent as part of custom events */
      gst_event_unref (event);
      break;
    }
    default:
      structure = gst_event_get_structure (event);
      if (structure) {
        gst_structure_get_uint (structure, "pad-index", &pad_idx);
        srcpad = gst_vvas_xdefunnel_get_srcpad_by_source_id (demux, pad_idx);
      }
      if (srcpad) {
        gst_pad_push_event (srcpad, event);
      } else {
        bret = gst_pad_event_default (pad, parent, event);
      }
      break;
  }
  return bret;
}

static gboolean
gst_vvas_xdefunnel_release_srcpad (gpointer key, gpointer value,
    gpointer user_data)
{
  GstPad *pad = (GstPad *) value;
  GstVvas_XDeFunnel *demux;
  demux = GST_VVAS_XDEFUNNEL (user_data);

  if (pad != NULL) {
    if (!gst_pad_set_active (pad, FALSE)) {
      GST_ERROR_OBJECT (demux, "failed to deactivate pad %s:%s",
          GST_DEBUG_PAD_NAME (pad));
      return FALSE;
    }
    if (!gst_element_remove_pad (GST_ELEMENT_CAST (demux), pad)) {
      GST_ERROR_OBJECT (demux, "failed to remove pad %s:%s from element",
          GST_DEBUG_PAD_NAME (pad));
      return FALSE;
    }
    GST_DEBUG_OBJECT (demux, "Removed pad %u", GPOINTER_TO_UINT (key));
  }
  return TRUE;
}

static void
gst_vvas_xdefunnel_reset (GstVvas_XDeFunnel * demux)
{
  GST_DEBUG_OBJECT (demux, "reset..");
  GST_OBJECT_LOCK (demux);
  if (demux->active_srcpad != NULL)
    demux->active_srcpad = NULL;

  demux->nb_srcpads = 0;
  GST_OBJECT_UNLOCK (demux);

  if (demux->source_id_pairs != NULL) {
    g_hash_table_foreach_remove (demux->source_id_pairs,
        gst_vvas_xdefunnel_release_srcpad, demux);
    g_hash_table_unref (demux->source_id_pairs);
    demux->source_id_pairs = NULL;
  }
}

static GstStateChangeReturn
gst_vvas_xdefunnel_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVvas_XDeFunnel *demux;
  GstStateChangeReturn result;

  demux = GST_VVAS_XDEFUNNEL (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_vvas_xdefunnel_reset (demux);
      break;
    default:
      break;
  }

  return result;
}

#ifndef PACKAGE
#define PACKAGE "vvas_xdefunnel"
#endif

static gboolean
vvas_xdefunnel_init (GstPlugin * vvas_xdefunnel)
{
  return gst_element_register (vvas_xdefunnel, "vvas_xdefunnel",
      GST_RANK_NONE, GST_TYPE_VVAS_XDEFUNNEL);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xdefunnel,
    "Xilinx Stream Demuxer based on source id metadata", vvas_xdefunnel_init,
    VVAS_API_VERSION, "MIT/X11", "Xilinx VVAS SDK plugin", "http://xilinx.com/")
