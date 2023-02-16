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

/* GstVvas_Xskipframe
 * The vvas_xskipframe element pushes frame to inference source based on infer-interval */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/vvas/gstvvassrcidmeta.h>
#include "gstvvas_xskipframe.h"

/* These might be added to glib in the future, but in the meantime they're defined here. */
#ifndef GULONG_TO_POINTER
#define GULONG_TO_POINTER(ul) ((gpointer)(gulong)(ul))
#endif

/** @def GST_CAT_DEFAULT
 *  @brief Setting gst_vvas_xskipframe_debug as default a debug category for logging
 */
#define GST_CAT_DEFAULT gst_vvas_xskipframe_debug

/**
 *  @brief Defines a static GstDebugCategory global variable "gst_vvas_xskipframe_debug"
 */
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/** @def DEFAULT_INFER_INTERVAL
 *  @brief Default infer-interval property value
 */
#define DEFAULT_INFER_INTERVAL 1

/** @def MAX_INFER_INTERVAL
 *  @brief maximum infer-interval property value
 */
#define MAX_INFER_INTERVAL 7

/** @enum VvasXskipframeProperties
 *  @brief  Contains property related to VVAS Xskipframe
 */
enum
{
  PROP_0,                       /*! Gstreamer default added dummy property */
  PROP_INFER_INTERVAL,          /*!< Property to set/get infer interval */
};

/* Static function's prototype */
static void gst_vvas_xskipframe_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_vvas_xskipframe_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static gboolean gst_vvas_xskipframe_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_vvas_xskipframe_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

/**
 *  @brief Defines sink pad's template
 */
static GstStaticPadTemplate vvas_xskipframe_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/**
 *  @brief Defines inference source pad's template
 */
static GstStaticPadTemplate vvas_xskipframe_src_template_1 =
GST_STATIC_PAD_TEMPLATE ("src_0",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/**
 *  @brief Defines skip source pad's template
 */
static GstStaticPadTemplate vvas_xskipframe_src_template_2 =
GST_STATIC_PAD_TEMPLATE ("src_1",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* class initialization */

/** @brief   Glib's convenience macro for GstVvas_Xskipframe type implementation.
 *  @details This macro does below tasks:\n
 *           - Declares a class initialization function with prefix gst_vvas_xskipframe \n
 *           - Declares an instance initialization function\n
 *           - A static variable named gst_vvas_xskipframe_parent_class pointing to the parent class\n
 *           - Defines a gst_vvas_xskipframe_get_type() function with below tasks\n
 *           - Initializes GTypeInfo function pointers\n
 *           - Registers GstVvas_XskipframePrivate as private structure to GstVvas_Xskipframe type\n
 *           - Initialize new debug category vvas_xskipframe for logging\n
 */
G_DEFINE_TYPE_WITH_CODE (GstVvas_Xskipframe, gst_vvas_xskipframe,
    GST_TYPE_ELEMENT, GST_DEBUG_CATEGORY_INIT (gst_vvas_xskipframe_debug,
        "vvas_xskipframe", 0, "debug category for vvas_xskipframe element"));

/**
 *  @fn static void gst_vvas_skipframe_class_init (GstVvas_XskipframeClass * klass)
 *  @param [in] klass  - Handle to GstVvas_XskipframeClass
 *  @return 	None
 *  @brief  	Add property of GstVvas_Xskipframe to parent GObjectClass and overrides function pointers
 *          	present in itself and/or its parent class structures
 *  @details    This function publishes property that can be set/get from application on GstVvas_Xskipframe object.
 *              And, while publishing a property it also declares type, range of acceptable values, default value,
 *              readability/writability and in which GStreamer state a property can be changed.
 */
static void
gst_vvas_xskipframe_class_init (GstVvas_XskipframeClass * klass)
{
  /* GObject class init, override VMethods and install properties */
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* override GObject class vmethods */
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xskipframe_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xskipframe_get_property);

  /*install infer-interval property */
  g_object_class_install_property (gobject_class, PROP_INFER_INTERVAL,
      g_param_spec_uint ("infer-interval",
          "Every nth frame will be pushed to inference source pad based on interval",
          "No. of frames for inference interval", 1, MAX_INFER_INTERVAL,
          DEFAULT_INFER_INTERVAL, G_PARAM_READWRITE));

  /* set plugin's metadata */
  gst_element_class_set_static_metadata (element_class,
      "Xilinx Inference interval skipframe plugin", "Generic",
      "Plugin to pushes frame to inference source based on interval",
      "Xilinx Inc <https://www.xilinx.com/>");

  /* Add sink and source pad templates */
  gst_element_class_add_static_pad_template (element_class,
      &vvas_xskipframe_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &vvas_xskipframe_src_template_1);
  gst_element_class_add_static_pad_template (element_class,
      &vvas_xskipframe_src_template_2);
}

/**
 *  @fn static void gst_vvas_xskipframe_init (GstVvas_Xskipframe * vvas_xskipframe)
 *  @param [in] vvas_xskipframe - Handle to GstVvas_Xskipframe instance
 *  @return None
 *  @brief  Initializes GstVvas_Xskipframe member variables to default and does one time 
 *          object/memory allocations in object's lifecycle
 */
static void
gst_vvas_xskipframe_init (GstVvas_Xskipframe * vvas_xskipframe)
{
  /* Add SINK pad to skipframe element */
  vvas_xskipframe->sinkpad = gst_pad_new_from_static_template
      (&vvas_xskipframe_sink_template, "sink");
  GST_PAD_SET_PROXY_CAPS (vvas_xskipframe->sinkpad);

  /* Override event function for sink pad */
  gst_pad_set_event_function (vvas_xskipframe->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xskipframe_sink_event));

  /* Override chain function for sink pad */
  gst_pad_set_chain_function (vvas_xskipframe->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xskipframe_sink_chain));

  gst_element_add_pad (GST_ELEMENT (vvas_xskipframe), vvas_xskipframe->sinkpad);

  /* Add Inference SRC pad to skipframe element */
  vvas_xskipframe->inference_srcpad = gst_pad_new_from_static_template
      (&vvas_xskipframe_src_template_1, "src_0");

  GST_PAD_SET_PROXY_CAPS (vvas_xskipframe->inference_srcpad);

  gst_element_add_pad (GST_ELEMENT (vvas_xskipframe),
      vvas_xskipframe->inference_srcpad);

  /* Add Skip SRC pad to skipframe element */
  vvas_xskipframe->skip_srcpad = gst_pad_new_from_static_template
      (&vvas_xskipframe_src_template_2, "src_1");

  GST_PAD_SET_PROXY_CAPS (vvas_xskipframe->skip_srcpad);

  gst_element_add_pad (GST_ELEMENT (vvas_xskipframe),
      vvas_xskipframe->skip_srcpad);

  /* Create a hash table for maintain src_id and frame_id mapping */
  vvas_xskipframe->frameid_pair =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, NULL);

  /* Create a hash table for maintain inference pending for each src_id */
  vvas_xskipframe->infer_pair =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, NULL);

  /* Initialize property to their default value */
  vvas_xskipframe->infer_interval = DEFAULT_INFER_INTERVAL;

  /* Keep track of previous source id to maintain a batch in case of frame drop */
  vvas_xskipframe->prev_src_id = G_MAXUINT;

  vvas_xskipframe->batch_id = -1;
}

/**
 *  @fn static void gst_vvas_xskipframe_set_property (GObject * object, 
 *  		      guint property_id, const GValue * value, GParamSpec * pspec)
 *  @param [in] object      - GstVvas_Xskipframe typecasted to GObject
 *  @param [in] property_id - ID as defined in Vvas_XSkipframe_Properties enum
 *  @param [in] value       - GValue which holds property value set by user
 *  @param [in] pspec       - Metadata of a property with property ID \p property_id
 *  @return None
 *  @brief  This API stores values sent from the user in GstVvas_Xskipframe object members.
 *  @details    This API is registered with GObjectClass by overriding GObjectClass::set_property function pointer and
 *              this will be invoked when developer sets properties on GstVvas_Xskipframe object. Based on property value type,
 *              corresponding g_value_get_xxx API will be called to get property value from GValue handle.
 */
static void
gst_vvas_xskipframe_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_Xskipframe *vvas_xskipframe = GST_VVAS_XSKIPFRAME (object);

  switch (property_id) {
    case PROP_INFER_INTERVAL:
      /* set infer_interval from the user */
      vvas_xskipframe->infer_interval = g_value_get_uint (value);
      GST_DEBUG_OBJECT (vvas_xskipframe, "infer-interval set to %u",
          vvas_xskipframe->infer_interval);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/**
 *  @fn static void gst_vvas_xskipframe_get_property (GObject * object, 
 *                    guint property_id, GValue * value, GParamSpec * pspec)
 *  @param [in] object      - GstVvas_Xskipframe typecasted to GObject
 *  @param [in] property_id - ID as defined in Vvas_XSkipframe_Properties enum
 *  @param [out] value      - GValue which holds property value set by user
 *  @param [in] pspec       - Metadata of a property with property ID \p property_id
 *  @return None
 *  @brief  This API stores values from the GstVvas_Xskipframe object members into the value for user.
 *  @details    This API is registered with GObjectClass by overriding GObjectClass::get_property function pointer and
 *              this will be invoked when developer gets properties on GstVvas_Xskipframe object. Based on property value type,
 *              corresponding g_value_set_xxx API will be called to set property value to GValue handle.
 */
static void
gst_vvas_xskipframe_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvas_Xskipframe *vvas_xskipframe = GST_VVAS_XSKIPFRAME (object);

  switch (property_id) {
    case PROP_INFER_INTERVAL:
      /* Get current infer_interval value for the user */
      GST_DEBUG_OBJECT (vvas_xskipframe, "infer-interval = %u",
          vvas_xskipframe->infer_interval);
      g_value_set_uint (value, vvas_xskipframe->infer_interval);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/**
 *  @fn static GstFlowReturn gst_vvas_xskipframe_sink_chain (GstPad * pad,
 *                             GstObject * parent, GstBuffer * buffer)
 *  @param [in] pad     - GstPad onto which data was pushed
 *  @param [in] parent  - Handle to GstVvas_Xskipframe instance
 *  @param [in] buffer  - Chained GstBuffer
 *  @return GST_FLOW_OK when buffer was successfully handled error otherwise
 *  @brief  This function will get invoked whenever a buffer is chained onto the pad.
 *  @details    The chain function is the function in which all data processing takes place.
 */
static GstFlowReturn
gst_vvas_xskipframe_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstPad *srcpad = NULL;
  GstVvas_Xskipframe *vvas_xskipframe = GST_VVAS_XSKIPFRAME (parent);
  GstVvasSrcIDMeta *meta;
  guint src_id;

  /* Get srcid meta from Defunnel to uniquely identify the pad to which this
   * buffer belongs
   */
  meta = gst_buffer_get_vvas_srcid_meta (buffer);
  if (!meta) {
    GST_DEBUG_OBJECT (vvas_xskipframe,
        "Meta is not available in received buffer.");
    buffer = gst_buffer_make_writable (buffer);
    meta = gst_buffer_add_vvas_srcid_meta (buffer);
    meta->src_id = 0;
  }

  src_id = meta->src_id;

  /* If current source id is less than or equal to previous source id than update the current batch */
  if (src_id <= vvas_xskipframe->prev_src_id) {
    GST_DEBUG_OBJECT (vvas_xskipframe,
        "Update batch: batch_id - %d current src_id - %u prev_src_id - %u",
        vvas_xskipframe->batch_id, src_id, vvas_xskipframe->prev_src_id);
    vvas_xskipframe->batch_id =
        (vvas_xskipframe->batch_id + 1) % vvas_xskipframe->infer_interval;
    /* if current batch is for an inference processing than mark a infer pending flag as a TRUE for all the src_id */
    if (!vvas_xskipframe->batch_id) {
      GHashTableIter iter;
      gpointer key;
      gpointer value;
      g_hash_table_iter_init (&iter, vvas_xskipframe->infer_pair);
      while (g_hash_table_iter_next (&iter, &key, &value)) {
        guint flag = 1;
        g_hash_table_insert (vvas_xskipframe->infer_pair, key,
            GUINT_TO_POINTER (flag));
      }
    }
  }

  /* update the prev_src_id as current source */
  vvas_xskipframe->prev_src_id = src_id;

  /* Get current frame id from frameid_pair hash table for src-id and add as a frame-id meta */
  if (g_hash_table_lookup_extended (vvas_xskipframe->frameid_pair,
          GUINT_TO_POINTER (src_id), NULL, (gpointer) (&meta->frame_id))) {

    /* Increment the current frame id in hash table for src-id */
    g_hash_table_insert (vvas_xskipframe->frameid_pair,
        GUINT_TO_POINTER (src_id), GULONG_TO_POINTER (meta->frame_id + 1));

    /* Select the srcpad based onthe current batch id and infer-interval */
    if (vvas_xskipframe->batch_id > 0) {
      /* Check the inference pending flag for current src_id and select the inference src pad if it is pending. This scenarion occurs when frame is dropped in inference batch. */
      guint infer = 0;
      if (g_hash_table_lookup_extended (vvas_xskipframe->infer_pair,
              GUINT_TO_POINTER (src_id), NULL, (gpointer) (&infer))) {
        if (infer) {
          GST_DEBUG_OBJECT (vvas_xskipframe,
              "Dropped Inference: source id - %u frame id - %lu batch_id - %d",
              meta->src_id, meta->frame_id, vvas_xskipframe->batch_id);
          GST_DEBUG_OBJECT (vvas_xskipframe, "Pushing infer  %" GST_PTR_FORMAT,
              buffer);
          srcpad = vvas_xskipframe->inference_srcpad;
          /* Clear the inference pending flag */
          g_hash_table_insert (vvas_xskipframe->infer_pair,
              GUINT_TO_POINTER (src_id), 0);
        } else {
          GST_DEBUG_OBJECT (vvas_xskipframe,
              "Skip: source id - %u frame id - %lu batch_id - %d", meta->src_id,
              meta->frame_id, vvas_xskipframe->batch_id);
          GST_DEBUG_OBJECT (vvas_xskipframe, "Pushing skipper %" GST_PTR_FORMAT,
              buffer);
          srcpad = vvas_xskipframe->skip_srcpad;
        }
      } else {
        goto error;
      }
    } else {
      GST_DEBUG_OBJECT (vvas_xskipframe,
          "Inference: source id - %u frame id - %lu batch_id - %d",
          meta->src_id, meta->frame_id, vvas_xskipframe->batch_id);
      GST_DEBUG_OBJECT (vvas_xskipframe, "Pushing infer %" GST_PTR_FORMAT,
          buffer);
      srcpad = vvas_xskipframe->inference_srcpad;

      /* Clear the inference pending flag */
      g_hash_table_insert (vvas_xskipframe->infer_pair,
          GUINT_TO_POINTER (src_id), 0);
    }
  } else {
    goto error;
  }

  /* pushes the buffer on selected srcpad */
  return gst_pad_push (srcpad, buffer);

error:
  return GST_FLOW_ERROR;
}

/**
 *  @fn static gboolean gst_vvas_xskipframe_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
 *  @param [in] pad       - The GstPad to handle the event.
 *  @param [in] parent    - The parent of the pad, which is the GstVvas_Xskipframe handle, typecasted to GstObject
 *  @param [in] event     - The GstEvent to handle.
 *  @return On Success returns TRUE
 *          On Failure returns FALSE
 *  @brief  Handles GstEvent coming over the sink pad. Ex : EOS, New caps etc.
 *  @details    This function is a callback function for any new event coming on the sink pad.
 */
static gboolean
gst_vvas_xskipframe_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstVvas_Xskipframe *vvas_xskipframe = GST_VVAS_XSKIPFRAME (parent);

  GST_DEBUG_OBJECT (pad, "Received event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);
      gst_pad_set_caps (vvas_xskipframe->inference_srcpad, caps);
      gst_pad_set_caps (vvas_xskipframe->skip_srcpad, caps);
    }

    case GST_EVENT_CUSTOM_DOWNSTREAM:{
      /* Remove the src-id entry from frameid_pair hash table for src pad index in case of pad-eos event */
      const GstStructure *structure = NULL;
      guint pad_idx;
      structure = gst_event_get_structure (event);
      if (structure) {
        gst_structure_get_uint (structure, "pad-index", &pad_idx);
        if (!g_strcmp0 (gst_structure_get_name (structure), "pad-eos")) {
          GST_DEBUG_OBJECT (vvas_xskipframe, "Got pad-eos event on pad %u",
              pad_idx);
          g_hash_table_remove (vvas_xskipframe->frameid_pair,
              GINT_TO_POINTER (pad_idx));
          g_hash_table_remove (vvas_xskipframe->infer_pair,
              GINT_TO_POINTER (pad_idx));
        }
      }
      break;
    }

    case GST_EVENT_STREAM_START:
    {

      /* Got stream start event. stream_id will be extracted from event and get the src pad index 
       * from the structure and one entry will be added in frameid_pair hash table for current pad index 
       * with initial frameid value as zero for first frame*/
      const GstStructure *structure = NULL;
      guint pad_idx;

      structure = gst_event_get_structure (event);
      if (!gst_structure_get_uint (structure, "pad-index", &pad_idx)) {
        pad_idx = 0;
      }
      GST_DEBUG_OBJECT (vvas_xskipframe, "pad index - %u", pad_idx);
      g_hash_table_insert (vvas_xskipframe->frameid_pair,
          GUINT_TO_POINTER (pad_idx), 0);
      g_hash_table_insert (vvas_xskipframe->infer_pair,
          GUINT_TO_POINTER (pad_idx), 0);
      break;
    }

    case GST_EVENT_EOS:
    {
      /* Sink pad has sent EOS, detroy the frameid_pair hash table entry and forward the event */
      g_hash_table_destroy (vvas_xskipframe->frameid_pair);
      g_hash_table_destroy (vvas_xskipframe->infer_pair);
      break;
    }

    default:
      break;
  }

  /* forward the event to all src pads */
  return gst_pad_event_default (pad, parent, event);
}

/**
 *  @fn static gboolean vvas_xskipframe_plugin_init (GstPlugin * plugin)
 *  @param [in] plugin - Handle to vvas_xskipframe plugin
 *  @return TRUE if plugin initialized successfully
 *  @brief  This is a callback function that will be called by the loader at startup to register the plugin
 *  @note   It create a new element factory capable of instantiating objects of the type
 *          'GST_TYPE_VVAS_XSKIPFRAME' and adds the factory to plugin 'vvas_xskipframe'
 */
static gboolean
vvas_xskipframe_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_vvas_xskipframe_debug, "vvas_xskipframe",
      0, "Template vvas_xskipframe");

  /* register vvas_xskipframe plugin */
  return gst_element_register (plugin, "vvas_xskipframe", GST_RANK_PRIMARY,
      GST_TYPE_VVAS_XSKIPFRAME);
}

/**
 *  @brief This macro is used to define the entry point and meta data of a plugin.
 *         This macro exports a plugin, so that it can be used by other applications
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xskipframe,
    "Xilinx skip frame plugin to skip the incoming frames from different "
    "sources for inference based on configured interval",
    vvas_xskipframe_plugin_init, VVAS_API_VERSION, "MIT/X11",
    "Xilinx VVAS SDK plugin", "https://www.xilinx.com/")
