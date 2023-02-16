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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstvvas_xdefunnel.h"
#include <gst/vvas/gstvvassrcidmeta.h>

/**
 *  @brief Defines a static GstDebugCategory global variable "gst_vvas_xdefunnel_debug_category"
 */
GST_DEBUG_CATEGORY_STATIC (gst_vvas_xdefunnel_debug_category);

/** @def GST_CAT_DEFAULT
 *  @brief Setting gst_vvas_xdefunnel_debug_category as default debug category
 *  for logging
 */
#define GST_CAT_DEFAULT gst_vvas_xdefunnel_debug_category

/** @enum VvasXDefunnel_Properties
 *  @brief  Vvas_XDeFunnel properties
 */
typedef enum
{
  /** Gstreamer default added dummy property */
  PROP_0,
  /** Property to get active pad */
  PROP_ACTIVE_PAD,
  PROP_LAST
} VvasXDefunnel_Properties;

/**
 *  @brief Defines sink pad's template
 */
static GstStaticPadTemplate gst_vvas_xdefunnel_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/**
 *  @brief Defines source pad's template
 */
static GstStaticPadTemplate gst_vvas_xdefunnel_src_factory =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

#define _do_init \
GST_DEBUG_CATEGORY_INIT (gst_vvas_xdefunnel_debug_category, \
        "vvas_xdefunnel", 0, "Stream demuxer");
#define gst_vvas_xdefunnel_parent_class parent_class

/** @brief  Glib's convenience macro for GstVvas_XDeFunnel type implementation.
 *  @details This macro does below tasks:\n
 *           - Declares a class initialization function with prefix gst_vvas_xdefunnel \n
 *           - Declares an instance initialization function\n
 *           - A static variable named gst_vvas_xdefunnel_parent_class pointing to the parent class\n
 *           - Defines a gst_vvas_xdefunnel_get_type() function with below tasks\n
 *           - Initializes GTypeInfo function pointers\n
 *           - Registers GstVvas_XDefunnelPrivate as private structure to GstVvas_XDeFunnel type\n
 *           - Initialize new debug category vvas_xdefunnel for logging
 */
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

/**
 *  @fn static void gst_vvas_xdefunnel_class_init (GstVvas_XDeFunnelClass * klass)
 *  @param [in] klass  - Handle to GstVvas_XDeFunnelClass
 *  @return None
 *  @brief  Add properties and signals of GstVvas_XDeFunnel to parent GObjectClass
 *          and overrides function pointers present in itself and/or its parent class structures
 *  @details  This function publishes properties those can be set/get from application on
 *            GstVvas_XDeFunnel object. And, while publishing a property it also declares type,
 *            range of acceptable values, default value, readability/writability and in which
 *            GStreamer state a property can be changed.
 */
static void
gst_vvas_xdefunnel_class_init (GstVvas_XDeFunnelClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  /* Override GObject's virtual methods */
  gobject_class->get_property = gst_vvas_xdefunnel_get_property;
  gobject_class->dispose = gst_vvas_xdefunnel_dispose;

  /* Install active-pad property */
  g_object_class_install_property (gobject_class, PROP_ACTIVE_PAD,
      g_param_spec_object ("active-pad", "Active pad",
          "The currently active src pad", GST_TYPE_PAD,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /* Set Static metadata of the elements */
  gst_element_class_set_static_metadata (gstelement_class, "Stream Demuxer",
      "Generic", "1-to-N output stream demuxer based on source id metadata",
      "Xilinx Inc <www.xilinx.com>");

  /* Add Source and Sink pad's templates to the element */
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_vvas_xdefunnel_sink_factory);

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_vvas_xdefunnel_src_factory);

  /* Override GstElement's virtual methods */
  gstelement_class->change_state = gst_vvas_xdefunnel_change_state;
}

/**
 *  @fn static void gst_vvas_xdefunnel_init (GstVvas_XDeFunnel * demux)
 *  @param [in] demux  - Handle to GstVvas_XDeFunnel instance
 *  @return None
 *  @brief  Initializes GstVvas_XDeFunnel member variables to default and does
 *          one time object/memory allocations in object's lifecycle.
 */
static void
gst_vvas_xdefunnel_init (GstVvas_XDeFunnel * demux)
{
  demux->sinkpad =
      gst_pad_new_from_static_template (&gst_vvas_xdefunnel_sink_factory,
      "sink");
  /* Sink pad created, add chain and event function to this */
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xdefunnel_chain));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xdefunnel_event));

  /* Add this pad to vvas_xdefunnel element */
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  /* srcpad management */
  demux->active_srcpad = NULL;
  demux->nb_srcpads = 0;
  demux->sink_caps = NULL;

  /* initialize hash table while move from READY to PAUSE state */
  demux->source_id_pairs = NULL;
}

/**
 *  @fn static void gst_vvas_xdefunnel_dispose (GObject * object)
 *  @param [in, out] object - GObject instance
 *  @return None
 *  @brief  This API will be called during element destruction phase, free all
 *          resources allocated for the element.
 *  @note   This API should chain up to the dispose method of the parent class
 *          after freeing it's own resources.
 */
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

/**
 *  @fn static void gst_vvas_xdefunnel_get_property (GObject * object,
 *                                                   guint prop_id,
 *                                                   GValue * value,
 *                                                   GParamSpec * pspec)
 *  @param [in] object      - GstVvas_XDeFunnel typecasted to GObject
 *  @param [in] prop_id     - ID as defined in VvasXDefunnel_Properties enum
 *  @param [out] value      - GValue which holds property value set by user
 *  @param [in] pspec       - Metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief  This API stores values from the GstVvas_XDeFunnel object members into the value for user.
 *  @details  This API is registered with GObjectClass by overriding GObjectClass::get_property function pointer
 *            and this will be invoked when developer gets properties on GstVvas_XDeFunnel object. Based on
 *            property value type, corresponding g_value_set_xxx API will be called to set property value to GValue
 *            handle.
 */
static void
gst_vvas_xdefunnel_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvas_XDeFunnel *demux = GST_VVAS_XDEFUNNEL (object);

  switch (prop_id) {
    case PROP_ACTIVE_PAD:
      GST_OBJECT_LOCK (demux);
      /* Set active pad info for the user */
      g_value_set_object (value, demux->active_srcpad);
      GST_OBJECT_UNLOCK (demux);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 *  @fn static gboolean forward_caps (GstPad * pad, gpointer user_data)
 *  @param [in] pad         - GstPad
 *  @param [in] user_data   - User data passed
 *  @return FALSE always so that it is called for all the pads
 *  @brief  This function set the caps (user_data) to the pad
 */
static gboolean
forward_caps (GstPad * pad, gpointer user_data)
{
  gboolean bret;
  bret = gst_pad_set_caps (pad, GST_CAPS_CAST (user_data));
  if (!bret)
    GST_ERROR_OBJECT (pad, "forward caps failed");
  return FALSE;
}

/**
 *  @fn static gboolean gst_vvas_xdefunnel_srcpad_create (GstVvas_XDeFunnel * demux, guint source_id)
 *  @param [in] demux       - Handle to GstVvas_XDeFunnel instance
 *  @param [in] source_id   - pad index
 *  @return TRUE on success, FALSE on failure
 *  @brief  This function creates a new source pad with given pad index(source_id).
 */
static gboolean
gst_vvas_xdefunnel_srcpad_create (GstVvas_XDeFunnel * demux, guint source_id)
{
  gchar *padname = NULL;
  GstPad *srcpad = NULL;
  GstPadTemplate *pad_tmpl = NULL;

  /* Prepare source pad name */
  padname = g_strdup_printf ("src_%u", source_id);
  pad_tmpl = gst_static_pad_template_get (&gst_vvas_xdefunnel_src_factory);
  demux->nb_srcpads++;

  GST_LOG_OBJECT (demux, "generating a srcpad:%s", padname);
  /* create new pad */
  srcpad = gst_pad_new_from_template (pad_tmpl, padname);
  gst_object_unref (pad_tmpl);
  g_free (padname);
  g_return_val_if_fail (srcpad != NULL, FALSE);

  demux->active_srcpad = srcpad;
  /* Inser this key(source_id) and value (srcpad) into the Hash table */
  g_hash_table_insert (demux->source_id_pairs, GUINT_TO_POINTER (source_id),
      gst_object_ref (srcpad));

  return TRUE;
}

/**
 *  @fn static GstFlowReturn gst_vvas_xdefunnel_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
 *  @param [in] pad     - GstPad onto which data was pushed
 *  @param [in] parent  - Handle to GstVvas_XDeFunnel instance typecasted to GstObject
 *  @param [in] buf     - Chained GstBuffer
 *  @return GST_FLOW_OK when buffer was successfully handled, error otherwise
 *  @brief  This function will get invoked whenever a buffer is chained onto the pad.
 *  @details  The chain function is the function in which all data processing takes place.
 */
static GstFlowReturn
gst_vvas_xdefunnel_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFlowReturn fret = GST_FLOW_OK;
  GstVvas_XDeFunnel *demux = NULL;
  GstPad *srcpad = NULL;
  GstVvasSrcIDMeta *srcid_meta;

  demux = GST_VVAS_XDEFUNNEL (parent);

  /* Get SrcId_Meta to know which source pad this buffer corresponds to */
  srcid_meta = gst_buffer_get_vvas_srcid_meta (buf);
  GST_OBJECT_LOCK (demux);
  if (srcid_meta) {
    /* Get source pad corresponding to this pad index */
    srcpad =
        gst_vvas_xdefunnel_get_srcpad_by_source_id (demux, srcid_meta->src_id);
    if (srcpad) {
      if (!GST_PAD_IS_EOS (srcpad)) {
        demux->active_srcpad = srcpad;
        srcpad = gst_object_ref (demux->active_srcpad);
        GST_OBJECT_UNLOCK (demux);
        GST_LOG_OBJECT (demux, "pushing buffer to %" GST_PTR_FORMAT,
            demux->active_srcpad);
        /* Push buffer to the source pad */
        fret = gst_pad_push (srcpad, buf);
        gst_object_unref (srcpad);
      } else {
        gst_buffer_unref (buf);
        GST_OBJECT_UNLOCK (demux);
        GST_WARNING_OBJECT (srcpad, "Got buffer, but srcpad is at EOS");
      }
    } else {
      gst_buffer_unref (buf);
      GST_OBJECT_UNLOCK (demux);
    }
  } else {
    /* Couldn't get source id meta */
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

/**
 *  @fn static  GstPad * gst_vvas_xdefunnel_get_srcpad_by_source_id (GstVvas_XDeFunnel * demux, guint source_id)
 *  @param [in] demux       -  GstVvas_XDeFunnel handle
 *  @param [in] source_id   -  Pad index for which source pad is to be find
 *  @return GstPad if found in HashTable or NULL
 *  @brief  This function checks the internal hash table for the given key (source_id)
 *          and returns the found value (source pad).
 */
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

  /* Lookup HashTable for source_id key */
  srcpad =
      g_hash_table_lookup (demux->source_id_pairs,
      GUINT_TO_POINTER (source_id));

  if (srcpad) {
    /* Found the matching value pair */
    GST_DEBUG_OBJECT (demux, "srcpad = %s:%s matched",
        GST_DEBUG_PAD_NAME (srcpad));
  }

done:
  return srcpad;
}

/**
 *  @fn static gboolean gst_vvas_xdefunnel_event (GstPad * pad, GstObject * parent, GstEvent * event)
 *  @param [in] pad       - The GstPad to handle the event.
 *  @param [in] parent    - The parent of the pad, which is the GstVvas_XDeFunnel handle, typecasted to GstObject
 *  @param [in] event     - The GstEvent to handle.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  Handles GstEvent coming over the sink pad. Ex : EOS, New caps etc.
 *  @details    This function performs below tasks based on the events types
 *              It creates new source pad when it receives STREAM_START event,
 *              It sends GST_EVENT_SEGMENT when it receives custom segment event
 *              It sends GST_EVENT_EOS  event when it gets custom pad-eos event, and
 *              It removes the source pad when it gets custom pad-removed event
 */
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
      /* Got Custom downstream event */
      structure = gst_event_get_structure (event);

      /* Get index of sender pad of this event */
      gst_structure_get_uint (structure, "pad-index", &pad_idx);

      if (!g_strcmp0 (gst_structure_get_name (structure), "pad-eos")) {
        /* Got custom pad-eos event */
        GST_DEBUG_OBJECT (demux, "Got pad-eos event on pad %u", pad_idx);
        gst_event_unref (event);
        /* Need to remove the source pad corresponding to the pad-index */
        srcpad = gst_vvas_xdefunnel_get_srcpad_by_source_id (demux, pad_idx);
        if (srcpad) {
          /* Stop downstream elements by sending EOS downstream */
          if (!gst_pad_push_event (srcpad, gst_event_new_eos ())) {
            GST_ERROR_OBJECT (demux, "failed to push eos event on pad %s:%s",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
        }
      } else if (!g_strcmp0 (gst_structure_get_name (structure), "pad-removed")) {
        /* Got custom Pad-REMOVED event */
        GST_DEBUG_OBJECT (demux, "Got pad-removed event on pad %u", pad_idx);

        srcpad = gst_vvas_xdefunnel_get_srcpad_by_source_id (demux, pad_idx);
        if (srcpad) {
          /* Deactivate this source pad */
          if (!gst_pad_set_active (srcpad, FALSE)) {
            GST_ERROR_OBJECT (demux, "failed to deactivate pad %s:%s",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
          /* Remove this source pad */
          if (!gst_element_remove_pad (GST_ELEMENT_CAST (demux), srcpad)) {
            GST_ERROR_OBJECT (demux, "failed to remove pad %s:%s from element",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
          /* Remove the entry of this pad-index from the HashTable also */
          bret =
              g_hash_table_remove (demux->source_id_pairs,
              GINT_TO_POINTER (pad_idx));
          GST_DEBUG_OBJECT (demux, "source pad with id %u removed", pad_idx);
        }
        gst_event_unref (event);
      } else if (!g_strcmp0 (gst_structure_get_name (structure), "segment")) {
        /* Got custom Segment event */
        GstSegment *segment = NULL;
        GstEvent *seg_event = NULL;
        GST_DEBUG_OBJECT (demux, "Got segment event on pad %u", pad_idx);

        /* vvas_xfunnel element keeps the GstSegment into this event structure,
         * with name segment-struct and type as GST_TYPE_SEGMENT, need to extract
         * it first. */
        if (!gst_structure_get (structure, "segment_struct", GST_TYPE_SEGMENT,
                &segment, NULL)) {
          GST_DEBUG_OBJECT (demux, "failed to read segment_struct field");
          bret = FALSE;
          break;
        } else {
          /* Got the GstSegment strucutre */
          GST_DEBUG_OBJECT (demux, "segment_struct parsed success");
        }
        /* Get Source pad corresponding to the pad index */
        srcpad = gst_vvas_xdefunnel_get_srcpad_by_source_id (demux, pad_idx);
        if (!srcpad) {
          GST_ERROR_OBJECT (demux, "source pad with id %u is not present",
              pad_idx);
          gst_event_unref (event);
          bret = FALSE;
          break;
        }
        /* Create new GST_SEGMENT_EVENT from this segment structure */
        seg_event = gst_event_new_segment (segment);
        GST_DEBUG_OBJECT (demux, "sending event %" GST_PTR_FORMAT, seg_event);

        /* Push this event to the source pad */
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
      /* Got STREAM_START event */
      gst_event_parse_stream_start (event, &stream_id);
      structure = gst_event_get_structure (event);

      /* Get index of pad which has sent this event */
      gst_structure_get_uint (structure, "pad-index", &pad_idx);

      /* Check if we already have any source pad corresponding to this pad_idx */
      srcpad = gst_vvas_xdefunnel_get_srcpad_by_source_id (demux, pad_idx);

      if (!srcpad) {
        /* Need to create new source pad */
        GST_INFO_OBJECT (demux, "source pad is not present...");
        GST_INFO_OBJECT (demux, "got new source id %u", pad_idx);

        GST_OBJECT_LOCK (demux);
        /* try to generate a srcpad */
        if (gst_vvas_xdefunnel_srcpad_create (demux, pad_idx)) {
          srcpad = demux->active_srcpad;
          GST_OBJECT_UNLOCK (demux);
          /* Activate Source Pad */
          if (!gst_pad_set_active (srcpad, TRUE)) {
            GST_ERROR_OBJECT (demux, "failed to activate pad %s:%s",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
          /* Push this STREAM_START event onto this source pad */
          if (!gst_pad_push_event (srcpad, event)) {
            GST_ERROR_OBJECT (demux,
                "failed to push stream start event on pad %s:%s",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
          /* Add this pad to vvas_xdefunnel element */
          if (!gst_element_add_pad (GST_ELEMENT_CAST (demux), srcpad)) {
            GST_ERROR_OBJECT (demux, "failed to add pad %s:%s to element",
                GST_DEBUG_PAD_NAME (srcpad));
            bret = FALSE;
            break;
          }
          GST_DEBUG_OBJECT (demux, "source pad with id %u added", pad_idx);
        } else {
          /* Couldn't create source pad */
          GST_OBJECT_UNLOCK (demux);
          GST_ELEMENT_ERROR (demux, STREAM, FAILED,
              ("Error occurred trying to create a srcpad"),
              ("Failed to create a srcpad via source-id"));
          bret = FALSE;
        }
      }
      /* Set caps on new pad */
      if (demux->sink_caps) {
        /* Set caps for this pad */
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
      /* Caps Event */
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);
      /* Store a copy of this event to configure it on the SRC pads, caps on
       * all the source pads will be the same */
      demux->sink_caps = gst_caps_copy (caps);
      gst_event_unref (event);
      /* Configure this caps on all source pads */
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
        /* Get pad index of the sender of this event */
        gst_structure_get_uint (structure, "pad-index", &pad_idx);
        /* Get SRC pad corresponding to this pad index */
        srcpad = gst_vvas_xdefunnel_get_srcpad_by_source_id (demux, pad_idx);
      }
      if (srcpad) {
        /* Push event to this source pad */
        gst_pad_push_event (srcpad, event);
      } else {
        /* Couldn't find any source pad, invoke default pad handler */
        bret = gst_pad_event_default (pad, parent, event);
      }
      break;
  }
  return bret;
}

/**
 *  @fn static gboolean gst_vvas_xdefunnel_release_srcpad (gpointer key, gpointer value, gpointer user_data)
 *  @param [in] key         - key for which this function is called
 *  @param [in] value       - Value (GstPad object to be released)
 *  @param [in] user_data   - User Data passed
 *  @return TRUE on success, FALSE on failure
 *  @brief  This function is called for each key/value pair in the hash table, this function
 *          will deactivate the source pad and remove it from the element.
 */
static gboolean
gst_vvas_xdefunnel_release_srcpad (gpointer key, gpointer value,
    gpointer user_data)
{
  GstPad *pad = (GstPad *) value;
  GstVvas_XDeFunnel *demux;
  demux = GST_VVAS_XDEFUNNEL (user_data);

  if (pad != NULL) {
    /* Deactivate SRC pad */
    if (!gst_pad_set_active (pad, FALSE)) {
      GST_ERROR_OBJECT (demux, "failed to deactivate pad %s:%s",
          GST_DEBUG_PAD_NAME (pad));
      return FALSE;
    }
    /* Remove pad from the element */
    if (!gst_element_remove_pad (GST_ELEMENT_CAST (demux), pad)) {
      GST_ERROR_OBJECT (demux, "failed to remove pad %s:%s from element",
          GST_DEBUG_PAD_NAME (pad));
      return FALSE;
    }
    GST_DEBUG_OBJECT (demux, "Removed pad %u", GPOINTER_TO_UINT (key));
  }
  return TRUE;
}

/**
 *  @fn static void gst_vvas_xdefunnel_reset (GstVvas_XDeFunnel * demux)
 *  @param [in] demux    - GstVvas_XDeFunnel Handle
 *  @return None
 *  @brief  This function resets internal variables and releases all source pads.
 */
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
    /* Release all source pads */
    g_hash_table_foreach_remove (demux->source_id_pairs,
        gst_vvas_xdefunnel_release_srcpad, demux);
    g_hash_table_unref (demux->source_id_pairs);
    demux->source_id_pairs = NULL;
  }
}

/**
 *  @fn static GstStateChangeReturn gst_vvas_xdefunnel_change_state (GstElement * element, GstStateChange transition)
 *  @param [in] element       - Handle to GstVvas_XDeFunnel typecasted to GstElement.
 *  @param [in] transition    - The requested state transition.
 *  @return Status of the state transition.
 *  @brief  This API will be invoked whenever the pipeline is going into a state transition
 *          and in this function the element can can initialize any sort of specific data
 *          needed by the element.
 *  @details  This API is registered with GstElementClass by overriding GstElementClass::change_state
 *            function pointer and this will be invoked whenever the pipeline is going into a state transition.
 */
static GstStateChangeReturn
gst_vvas_xdefunnel_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVvas_XDeFunnel *demux;
  GstStateChangeReturn result;

  demux = GST_VVAS_XDEFUNNEL (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (demux->source_id_pairs == NULL) {
        /* initialize hash table for srcpad */
        demux->source_id_pairs =
            g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
            (GDestroyNotify) gst_object_unref);
      }
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

/**
 *  @fn static gboolean vvas_xdefunnel_init (GstPlugin * vvas_xdefunnel)
 *  @param [in] vvas_xdefunnel - Handle to vvas_xdefunnel plugin
 *  @return TRUE if plugin initialized successfully
 *  @brief  This is a callback function that will be called by the loader at startup to register the plugin
 *  @note   It create a new element factory capable of instantiating objects of the type
 *          'GST_TYPE_VVAS_XDEFUNNEL' and adds the factory to plugin 'vvas_xdefunnel'
 */
static gboolean
vvas_xdefunnel_init (GstPlugin * vvas_xdefunnel)
{
  return gst_element_register (vvas_xdefunnel, "vvas_xdefunnel",
      GST_RANK_PRIMARY, GST_TYPE_VVAS_XDEFUNNEL);
}

/**
 *  @brief This macro is used to define the entry point and meta data of a plugin.
 *         This macro exports a plugin, so that it can be used by other applications
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xdefunnel,
    "Xilinx Stream Demuxer based on source id metadata", vvas_xdefunnel_init,
    VVAS_API_VERSION, "MIT/X11", "Xilinx VVAS SDK plugin", "http://xilinx.com/")
