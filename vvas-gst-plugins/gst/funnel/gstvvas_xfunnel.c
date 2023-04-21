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

/* GstVvas_Xfunnel
 * The vvas_xfunnel element serializes streams following round robin algorithm
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/vvas/gstvvassrcidmeta.h>
#include "gstvvas_xfunnel.h"

/** @def GST_CAT_DEFAULT
 *  @brief Setting gst_vvas_xfunnel_debug_category as default debug category
 *  for logging
 */
#define GST_CAT_DEFAULT gst_vvas_xfunnel_debug_category

/**
 *  @brief Defines a static GstDebugCategory global variable "gst_vvas_xfunnel_debug_category"
 */
GST_DEBUG_CATEGORY_STATIC (gst_vvas_xfunnel_debug_category);

/**
 *  @brief Defines gst_vvas_xfunnel_parent_class as parent class variable
 */
#define gst_vvas_xfunnel_parent_class parent_class

/**
 *  @brief Defines pad_parent_class
 */
#define pad_parent_class gst_vvas_xfunnel_pad_parent_class

/** @def MAX_SINK_PADS
 *  @brief Maximum sink pad to vvas_funell is limited to 256
 */
#define MAX_SINK_PADS     256

/** @def DEFAULT_SINK_QUEUE_SIZE
 *  @brief Default size of sink pad's queue
 */
#define DEFAULT_SINK_QUEUE_SIZE   2

/**
 *  1. Default timeout is infinite.
 *  2. If user provided timeout, use that timeout
 *     whether pipeline is in live or non-live.
 *  3. If any live source is connected and user not
 *     provided the timeout, use the timeout that is
 *     calculated using framerate.
 */

/** @def DEFAULT_SINKWAIT_TIMEOUT
 *  @brief Default time to wait for sink's buffer before switching to
 *  the next sink pad
 */
#define DEFAULT_SINKWAIT_TIMEOUT  G_MAXUINT/G_TIME_SPAN_MILLISECOND

/** @enum Vvas_XFunnel_Properties
 *  @brief  Vvas_XFunnel properties
 */
typedef enum
{
  /** GStreamer default added dummy property */
  PROP_0,
  /** Property to set/get queue size */
  PROP_QUEUE_SIZE,
  /** Property to set/get sink wait timeout */
  PROP_SINK_WAIT_TIMEOUT,
} Vvas_XFunnel_Properties;

/** @enum Custom_Events
 *  @brief Enum for custom Events
 */
typedef enum
{
  /** Custom pad added event */
  CUSTOM_EVENT_PAD_ADDED,
  /** Custom pad removed event */
  CUSTOM_EVENT_PAD_REMOVED,
  /** Custom pad EOS event */
  CUSTOM_EVENT_PAD_EOS,
  /** Custom Segment Event */
  CUSTOM_EVENT_SEGMENT
} Custom_Events;

/* Static function's prototype */
static void gst_vvas_xfunnel_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vvas_xfunnel_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec);
static void gst_vvas_xfunnel_dispose (GObject * object);
static void gst_vvas_xfunnel_release_pad (GstElement * element, GstPad * pad);
static gboolean gst_vvas_xfunnel_pad_is_live (GstVvas_Xfunnel *
    vvas_xfunnel, GstPad * pad);
static void
gst_vvas_xfunnel_update_sink_wait_timeout (GstVvas_Xfunnel * vvas_xfunnel);
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
static gboolean gst_vvas_xfunnel_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

/**
 *  @brief Defines sink pad's template
 */
static GstStaticPadTemplate vvas_xfunnel_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

/**
 *  @brief Defines source pad's template
 */
static GstStaticPadTemplate vvas_xfunnel_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/**
 *  @fn static void gst_vvas_xfunnelpad_dispose (GObject * object)
 *  @param [in, out]    - object GObject instance
 *  @return None
 *  @brief  This API will be called during pad destruction phase, free all
 *          resources allocated for pad.
 *  @note   This API should chain up to the dispose method of the parent class after it's own resources.
 */
static void
gst_vvas_xfunnelpad_dispose (GObject * object)
{
  GstVvas_XfunnelPad *vvas_xfunnelpad = GST_VVAS_XFUNNEL_PAD (object);
  /* Pad is getting disposed, release all resources allocated for pad */
  GST_DEBUG_OBJECT (vvas_xfunnelpad, "disposing pad: %s, queue_len: %u",
      vvas_xfunnelpad->name, g_queue_get_length (vvas_xfunnelpad->queue));
  /* Lets clear out if any buffers remaining in the queue */
  while (g_queue_get_length (vvas_xfunnelpad->queue)) {
    GstBuffer *buffer = NULL;
    buffer = g_queue_pop_head (vvas_xfunnelpad->queue);
    gst_buffer_unref (buffer);
  }
  g_free (vvas_xfunnelpad->name);
  g_queue_free (vvas_xfunnelpad->queue);
  g_mutex_clear (&vvas_xfunnelpad->lock);
  g_cond_clear (&vvas_xfunnelpad->cond);
  vvas_xfunnelpad->name = NULL;
  vvas_xfunnelpad->queue = NULL;
  /* Inform BaseClass also */
  G_OBJECT_CLASS (pad_parent_class)->dispose (object);
}

/**
 *  @fn static void gst_vvas_xfunnel_pad_class_init (GstVvas_XfunnelPadClass * klass)
 *  @param [in] klass  - Handle to GstVvas_XfunnelPadClass
 *  @return None
 *  @brief  Add properties and signals of GstVvas_XfunnelPad to parent GObjectClass
 *          and overrides function pointers present in itself and/or its parent class structures
 *  @details  This function publishes properties those can be set/get from application on GstVvas_XfunnelPad object.
 *            And, while publishing a property it also declares type, range of acceptable values, default value,
 *            readability/writability and in which GStreamer state a property can be changed.
 */
static void
gst_vvas_xfunnel_pad_class_init (GstVvas_XfunnelPadClass * klass)
{
  /* Pad Class Init, Override pad class vmethods */
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose = gst_vvas_xfunnelpad_dispose;
}

/**
 *  @fn static gst_vvas_xfunnel_pad_init (GstVvas_XfunnelPad * pad)
 *  @param [in] pad  - Handle to GstVvas_XfunnelPad instance
 *  @return None
 *  @brief  Initializes GstVvas_XfunnelPad member variables to default and does one time object/memory allocations
 *          in object's lifecycle.
 */
static void
gst_vvas_xfunnel_pad_init (GstVvas_XfunnelPad * pad)
{
  /* Initialize Pad variables */
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

/** @brief  Glib's convenience macro for GstVvas_Xfunnel type implementation.
 *  @details This macro does below tasks:\n
 *           - Declares a class initialization function with prefix gst_vvas_xfunnel \n
 *           - Declares an instance initialization function\n
 *           - A static variable named gst_vvas_xfunnel_parent_class pointing to the parent class\n
 *           - Defines a gst_vvas_xfunnel_get_type() function with below tasks\n
 *           - Initializes GTypeInfo function pointers\n
 *           - Registers GstVvas_XfunnelPrivate as private structure to GstVvas_Xfunnel type\n
 *           - Initialize new debug category vvas_xfunnel for logging\n
 */
G_DEFINE_TYPE_WITH_CODE (GstVvas_Xfunnel, gst_vvas_xfunnel, GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (gst_vvas_xfunnel_debug_category, "vvas_xfunnel", 0,
        "debug category for vvas_xfunnel element"));


/**
 *  @fn static void gst_vvas_xfunnel_class_init (GstVvas_XfunnelClass * klass)
 *  @param [in] klass  - Handle to GstVvas_XfunnelClass
 *  @return None
 *  @brief  Add properties and signals of GstVvas_Xfunnel to parent GObjectClass and overrides function pointers
 *          present in itself and/or its parent class structures
 *  @details  This function publishes properties those can be set/get from application on GstVvas_Xfunnel object.
 *            And, while publishing a property it also declares type, range of acceptable values, default value,
 *            readability/writability and in which GStreamer state a property can be changed.
 */
static void
gst_vvas_xfunnel_class_init (GstVvas_XfunnelClass * klass)
{
  /* GObject class init, override VMethods and install properties */
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* override GObject class vmethods */
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_get_property);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_dispose);

  /* install queue-size property */
  g_object_class_install_property (gobject_class, PROP_QUEUE_SIZE,
      g_param_spec_uint ("queue-size", "size of the queue on each pad",
          "Queue size for each sink pad", 1, 100, DEFAULT_SINK_QUEUE_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* install sink-wait-timeout property */
  g_object_class_install_property (gobject_class, PROP_SINK_WAIT_TIMEOUT,
      g_param_spec_uint ("sink-wait-timeout",
          "sink wait timeout in milliseconds",
          "time to wait before switching to the next sink in milliseconds, "
          "default will be calculated as 1/FPS in live mode", 1,
          DEFAULT_SINKWAIT_TIMEOUT, DEFAULT_SINKWAIT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /* set plugin's metadata */
  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Xilinx round robin funnel plugin", "Generic",
      "Funnel to serialize streams using round robin algorithm(N-to-1 pipe fitting)",
      "Xilinx Inc <https://www.xilinx.com/>");

  /* Add sink and source pad templates */
  gst_element_class_add_static_pad_template (element_class,
      &vvas_xfunnel_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &vvas_xfunnel_src_template);

  /* override GstElement class vmethods */
  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_release_pad);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_change_state);
}

/**
 *  @fn static void gst_vvas_xfunnel_init (GstVvas_Xfunnel * vvas_xfunnel)
 *  @param [in] vvas_xfunnel  - Handle to GstVvas_Xfunnel instance
 *  @return None
 *  @brief  Initializes GstVvas_Xfunnel member variables to default and does one time object/memory
 *          allocations in object's lifecycle.
 */
static void
gst_vvas_xfunnel_init (GstVvas_Xfunnel * vvas_xfunnel)
{
  /* Add SRC pad to funnel element */
  vvas_xfunnel->srcpad = gst_pad_new_from_static_template
      (&vvas_xfunnel_src_template, "src");
  gst_pad_use_fixed_caps (vvas_xfunnel->srcpad);

  gst_element_add_pad (GST_ELEMENT (vvas_xfunnel), vvas_xfunnel->srcpad);

  /* Override event function for src pad */
  gst_pad_set_event_function (vvas_xfunnel->srcpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_src_event));

  gst_pad_set_query_function (vvas_xfunnel->srcpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_src_query));

  /* Initialize variables to their default values */
  vvas_xfunnel->processing_thread = NULL;
  vvas_xfunnel->sink_pad_idx = 0;
  vvas_xfunnel->is_exit_thread = FALSE;
  vvas_xfunnel->queue_size = DEFAULT_SINK_QUEUE_SIZE;
  vvas_xfunnel->sink_wait_timeout = DEFAULT_SINKWAIT_TIMEOUT;
  vvas_xfunnel->last_fret = GST_FLOW_OK;
  vvas_xfunnel->sink_caps = NULL;
  vvas_xfunnel->is_user_timeout = FALSE;
  g_mutex_init (&vvas_xfunnel->mutex_lock);
  vvas_xfunnel->live_pad_hash = g_hash_table_new (g_direct_hash, g_int_equal);
}

/**
 *  @fn static void gst_vvas_xfunnel_set_property (GObject * object,
 *                                                 guint property_id,
 *                                                 const GValue * value,
 *                                                 GParamSpec * pspec)
 *  @param [in] object      - GstVvas_Xfunnel typecasted to GObject
 *  @param [in] property_id - ID as defined in Vvas_XFunnel_Properties enum
 *  @param [in] value       - GValue which holds property value set by user
 *  @param [in] pspec       - Metadata of a property with property ID \p property_id
 *  @return None
 *  @brief  This API stores values sent from the user in GstVvas_Xfunnel object members.
 *  @details    This API is registered with GObjectClass by overriding GObjectClass::set_property function pointer
 *              and this will be invoked when developer sets properties on GstVvas_Xfunnel object. Based on property
 *              value type, corresponding g_value_get_xxx API will be called to get property value from GValue handle.
 */
static void
gst_vvas_xfunnel_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (object);

  switch (property_id) {
    case PROP_QUEUE_SIZE:{
      /* got queue size from the user */
      GstState state;
      state = GST_STATE (GST_ELEMENT (vvas_xfunnel));
      /* set the queue size value during NULL or READY state */
      if (state <= GST_STATE_READY) {
        vvas_xfunnel->queue_size = g_value_get_uint (value);
        GST_DEBUG_OBJECT (vvas_xfunnel, "queue_size set to %u",
            vvas_xfunnel->queue_size);
      } else {
        GST_WARNING_OBJECT (vvas_xfunnel,
            "queue size value can be set during NULL or READY state only, so using previous value %d",
            vvas_xfunnel->queue_size);
      }

      break;
    }
    case PROP_SINK_WAIT_TIMEOUT:{
      /* got sink_wait_timeout from the user */
      GstState state;
      state = GST_STATE (GST_ELEMENT (vvas_xfunnel));

      GST_OBJECT_LOCK (vvas_xfunnel);
      /* set the timeout value during NULL or READY state */
      if (state <= GST_STATE_READY) {
        vvas_xfunnel->sink_wait_timeout = g_value_get_uint (value);
        GST_DEBUG_OBJECT (vvas_xfunnel, "sink_wait_timeout set to %u",
            vvas_xfunnel->sink_wait_timeout);
        vvas_xfunnel->is_user_timeout = TRUE;
      } else {
        GST_WARNING_OBJECT (vvas_xfunnel,
            "timeout value can be set during NULL or READY state only, so using previous value %d",
            vvas_xfunnel->sink_wait_timeout);
      }
      GST_OBJECT_UNLOCK (vvas_xfunnel);
      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/**
 *  @fn static void gst_vvas_xfunnel_get_property (GObject * object,
 *                                                 guint property_id,
 *                                                 GValue * value,
 *                                                 GParamSpec * pspec)
 *  @param [in] object      - GstVvas_Xfunnel typecasted to GObject
 *  @param [in] property_id - ID as defined in Vvas_XFunnel_Properties enum
 *  @param [out] value      - GValue which holds property value set by user
 *  @param [in] pspec       - Metadata of a property with property ID \p property_id
 *  @return None
 *  @brief  This API stores values from the GstVvas_Xfunnel object members into the value for user.
 *  @details  This API is registered with GObjectClass by overriding GObjectClass::get_property function pointer and
 *            this will be invoked when developer gets properties on GstVvas_Xfunnel object. Based on property value
 *            type, corresponding g_value_set_xxx API will be called to set property value to GValue handle.
 */
static void
gst_vvas_xfunnel_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (object);

  switch (property_id) {
    case PROP_QUEUE_SIZE:
      /* Set queue_size value for the user */
      g_value_set_uint (value, vvas_xfunnel->queue_size);
      break;

    case PROP_SINK_WAIT_TIMEOUT:
      /* Set sink_wait_timeout value for the user */
      GST_OBJECT_LOCK (vvas_xfunnel);
      g_value_set_uint (value, vvas_xfunnel->sink_wait_timeout);
      GST_OBJECT_UNLOCK (vvas_xfunnel);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/**
 *  @fn static void gst_vvas_xfunnel_dispose (GObject * object)
 *  @param [in, out] - object GObject instance
 *  @return None
 *  @brief  This API will be called during element destruction phase, free all resources allocated for the element.
 *  @note   This API should chain up to the dispose method of the parent class after it's own resources.
 */
static void
gst_vvas_xfunnel_dispose (GObject * object)
{
  /* Disposing funnel instance */
  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (object);
  GList *item;

  GST_DEBUG_OBJECT (vvas_xfunnel, "dispose");
  if (vvas_xfunnel->sink_caps) {
    gst_caps_unref (vvas_xfunnel->sink_caps);
    vvas_xfunnel->sink_caps = NULL;
  }
  g_mutex_clear (&vvas_xfunnel->mutex_lock);

restart:
  /* Release all requested sink pads */
  for (item = GST_ELEMENT_PADS (object); item; item = g_list_next (item)) {
    GstPad *pad = GST_PAD (item->data);
    if (GST_PAD_IS_SINK (pad)) {
      gst_element_release_request_pad (GST_ELEMENT (object), pad);
      goto restart;
    }
  }
  g_hash_table_destroy (vvas_xfunnel->live_pad_hash);
  G_OBJECT_CLASS (gst_vvas_xfunnel_parent_class)->dispose (object);
}

/**
 *  @fn static void gst_vvas_xfunnel_add_sink_pad_to_event (GstVvas_Xfunnel * funnel,
 *                                                          GstEvent * event,
 *                                                          guint pad_index)
 *  @param [in] funnel      - GstVvas_Xfunnel instance
 *  @param [in, out] event  - GstEvent pointer
 *  @param [in] pad_index   - Sink pad index
 *  @return None
 *  @brief  This API adds one pad-index field of type unsigned integer into the event structure.
 */
static inline void
gst_vvas_xfunnel_add_sink_pad_to_event (GstVvas_Xfunnel * funnel,
    GstEvent * event, guint pad_index)
{
  /* Add pad index to the event */
  GstStructure *st = gst_event_writable_structure (event);
  if (st) {
    gst_structure_set (st, "pad-index", G_TYPE_UINT, pad_index, NULL);
  }
}

/**
 *  @fn static GstEvent * gst_vvas_xfunnel_create_custom_event (Custom_Events event_type, guint pad_idx)
 *  @param [in] event_type  - Enum of type Custom_Events
 *  @param [in] pad_idx     - Sink pad index
 *  @return Allocated GstEvent
 *  @brief  This function creates custom downstream event, Event will have a GstStructure with
 *          pad-index as a unsigned integer field in it.
 */
static GstEvent *
gst_vvas_xfunnel_create_custom_event (Custom_Events event_type, guint pad_idx)
{
  GstStructure *event_struct = NULL;
  GstEvent *event = NULL;

  /* Create custom downstream event, Event will have a GstStructure with
   * pad-index as a unsigned integer field it it.
   */
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

/**
 *  @fn static gboolean gst_vvas_xfunnel_send_event (const GstVvas_Xfunnel * funnel, GstEvent * event)
 *  @param [in] funnel  - GstVvas_Xfunnel instance
 *  @param [in] event   - GstEvent to be sent
 *  @return TRUE if event was sent successfully onto the source pad, FALSE otherwise.
 *  @brief  This function sends the event onto the source pad of vvas_xfunnel.
 */
static gboolean
gst_vvas_xfunnel_send_event (const GstVvas_Xfunnel * funnel, GstEvent * event)
{
  gboolean res = FALSE;
  /* Take pad's stream lock and send Event on SRC pad */
  GST_PAD_STREAM_LOCK (funnel->srcpad);
  GST_DEBUG_OBJECT (funnel, "sending event: %" GST_PTR_FORMAT, event);
  res = gst_pad_push_event (funnel->srcpad, event);
  GST_PAD_STREAM_UNLOCK (funnel->srcpad);

  if (!res) {
    GST_ERROR_OBJECT (funnel, "Failed to send event");
  }
  return res;
}

/**
 *  @fn static gboolean gst_vvas_xfunnel_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
 *  @param [in] pad          - Pointer to src GstPad.
 *  @param [in] parent       - Pointer to GstObject
 *  @param [out] query       - Allocation Query to be answered/executed.
 *  @return  On Success returns TRUE
 *           On Failure returns FALSE
 *  @brief   This function will be invoked whenever an downstream element sends query on the src pad.
 *  @details This function will be registered using function gst_pad_set_query_function as a query handler.
 *
 */
static gboolean
gst_vvas_xfunnel_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
      gst_query_unref (query);
      ret = FALSE;
      break;

    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

/**
 *  @fn static gboolean gst_vvas_xfunnel_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
 *  @param [in] pad       - The GstPad to handle the event.
 *  @param [in] parent    - The parent of the pad, which is the GstVvas_Xfunnel handle, typecasted to GstObject
 *  @param [in] event     - The GstEvent to handle.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  Handles upstream events coming over source pad
 *  @details  This function is a callback function for any new event coming on the source pad.
 */
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

/**
 *  @fn static GstPad *gst_vvas_xfunnel_request_new_pad (GstElement * element,
 *                                                       GstPadTemplate * templ,
 *                                                       const gchar * name,
 *                                                       const GstCaps * caps)
 *  @param [in] element - Handle to GstVvas_Xfunnel instance
 *  @param [in] templ   - Pad template
 *  @param [in] name    - Pad name.
 *  @param [in] caps    - Pad capabilities.
 *  @return Returns the newly created pad.
 *  @brief  This function will get invoked whenever user requests for a new sink pad.
 *  @details  Create the pad as per input values and adds it to the element and
 *            sends custom events if needed.
 */
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
    /* Funnel supports limited sink pads if that limit is reached, deny new request */
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
  /* Assign unique pad index to this sink pad */
  fpad->pad_idx = vvas_xfunnel->sink_pad_idx++;
  g_mutex_unlock (&vvas_xfunnel->mutex_lock);

  /* register chain and event function */
  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_sink_chain));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xfunnel_sink_event));

  GST_OBJECT_FLAG_SET (sinkpad, GST_PAD_FLAG_PROXY_CAPS);

  /* Activate and add the pad to the funnel element */
  gst_pad_set_active (sinkpad, TRUE);
  gst_element_add_pad (element, sinkpad);

  GST_DEBUG_OBJECT (element, "requested pad %s:%s, pad_idx: %u",
      GST_DEBUG_PAD_NAME (sinkpad), fpad->pad_idx);

  fpad->name = gst_pad_get_name (sinkpad);
  return sinkpad;
}

/**
 *  @fn static void gst_vvas_xfunnel_release_pad (GstElement * element, GstPad * pad)
 *  @param [in] element - Handle to GstVvas_Xfunnel instance
 *  @param [in] pad     - GsPad object to be released.
 *  @return None
 *  @brief  This function will get invoked whenever a request pad is to be released.
 *  @details  This function sends the needed custom events and detaches the pad
 *            from the element and that will be freed up.
 */
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
      /* If pad is released without sending EOS event, send custom EOS first */
      custom_eos_event =
          gst_vvas_xfunnel_create_custom_event (CUSTOM_EVENT_PAD_EOS,
          fpad->pad_idx);
    }
    /* Inform Defunnel that this sink pad is removed by sending custom event */
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
  /* Mark this sink pad as inactive and remove it from the element */
  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (GST_ELEMENT_CAST (vvas_xfunnel), pad);
  if (!vvas_xfunnel->is_user_timeout) {
    /* Remove the pad from hash table and update the
     * wait timeout value based on status of remaining pads
     */
    GST_OBJECT_LOCK (vvas_xfunnel);
    g_hash_table_remove (vvas_xfunnel->live_pad_hash, pad);
    gst_vvas_xfunnel_update_sink_wait_timeout (vvas_xfunnel);
    GST_OBJECT_UNLOCK (vvas_xfunnel);
  }

}

/**
 *  @fn static void gst_vvas_xfunnel_signal_all_pads (GstVvas_Xfunnel * vvas_xfunnel)
 *  @param [in] vvas_xfunnel	- Handle to GstVvas_Xfunnel
 *  @return None
 *  @brief  This function will get invoked whenever vvas_xfunnel needs to
 *          exit its processing thread.
 *  @details  This function signals all the vvas_xfunnel sinkpads which are
 *            waiting in processing thread which is anticipated to exit.
 */

static void
gst_vvas_xfunnel_signal_all_pads (GstVvas_Xfunnel * vvas_xfunnel)
{
  GstIterator *itr = NULL;
  GValue item = G_VALUE_INIT;
  gboolean done = FALSE;
  itr = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (vvas_xfunnel));
  while (!done) {
    switch (gst_iterator_next (itr, &item)) {
      case GST_ITERATOR_OK:{
        GstVvas_XfunnelPad *fpad;
        GstPad *pad;
        pad = g_value_get_object (&item);
        fpad = GST_VVAS_XFUNNEL_PAD_CAST (pad);

        g_mutex_lock (&fpad->lock);
        /* signal the pad that is waiting in processing thread
         * which is anticipated to exit */
        g_cond_signal (&fpad->cond);
        g_mutex_unlock (&fpad->lock);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (itr);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (itr);
  return;
}

/**
 *  @fn static void
 *	gst_vvas_xfunnel_update_sink_wait_timeout (GstVvas_Xfunnel * vvas_xfunnel)
 *  @param [in] vvas_xfunnel	- Handle to GstVvas_Xfunnel
 *  @return None.
 *  @brief  This function adjust the sink wait timeout value based on
 *          mode (live/non-live) the pads are running.
 *  @details  This function adjust the sink wait timeout value based on
 *            mode (live/non-live) the pads are running. If any pad is live it adjusts the
 *            wait timeout value based on framerate else it sets the timeout to infinite.
 */
static void
gst_vvas_xfunnel_update_sink_wait_timeout (GstVvas_Xfunnel * vvas_xfunnel)
{
  GList *live_pads = g_hash_table_get_values (vvas_xfunnel->live_pad_hash);
  /* g_list_find (live_pads, GINT_TO_POINTER(TRUE)) returns non NULL
   * if value of any pad from the list is live */
  if (g_list_find (live_pads, GINT_TO_POINTER (TRUE))) {
    /* If any pad is live set the wait timeout value based on framerate
     * which was already calculated during GST_EVENT_CAPS.
     * All the input caps to funnel are expected to receive streams
     * of same framerate. So calculated timeout is same for all pads.
     */
    vvas_xfunnel->sink_wait_timeout =
        vvas_xfunnel->sink_wait_timeout_calculated;
  } else {
    /* Set infinite timeout if none of the pads are live */
    vvas_xfunnel->sink_wait_timeout = DEFAULT_SINKWAIT_TIMEOUT;
  }
  GST_DEBUG_OBJECT (vvas_xfunnel, "updated timeout value to %d",
      vvas_xfunnel->sink_wait_timeout);

  g_list_free (live_pads);
}

/**
 *  @fn static gboolean gst_vvas_xfunnel_pad_is_live (GstVvas_Xfunnel * vvas_xfunnel
 *                                                    GstPad * pad)
 *  @param [in] vvas_xfunnel	- Handle to GstVvas_Xfunnel
 *  @param [in] pad		- reference to a pad on which query to be performed
 *  @return TRUE on success.
 *          FALSE on failure.
 *  @brief  This function will get invoked whenever a new pad is added to the funnel.
 *  @details  This function queries the pad whether it is running in live mode.
 */
static gboolean
gst_vvas_xfunnel_pad_is_live (GstVvas_Xfunnel * vvas_xfunnel, GstPad * pad)
{
  GstQuery *query;
  gboolean query_ret = FALSE, live = FALSE;

  query = gst_query_new_latency ();

  /* Query the peer pad to know whether it is running in live */
  query_ret = gst_pad_peer_query (pad, query);
  if (!query_ret) {
    GST_DEBUG_OBJECT (vvas_xfunnel, "Latency query failed on pad %p", pad);
  } else {
    gst_query_parse_latency (query, &live, NULL, NULL);
  }
  gst_query_unref (query);
  return live;
}

/**
 *  @fn static GstFlowReturn gst_vvas_xfunnel_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
 *  @param [in] pad     - GstPad onto which data was pushed
 *  @param [in] parent  - Handle to GstVvas_Xfunnel instance
 *  @param [in] buffer  - Chained GstBuffer
 *  @return GST_FLOW_OK when buffer was successfully handled error otherwise
 *  @brief  This function will get invoked whenever a buffer is chained onto the pad.
 *  @details  The chain function is the function in which all data processing takes place.
 */
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
  /* Add SRCID metadata for Defunnel to uniquely identify the pad to which this
   * buffer belongs
   */
  meta = gst_buffer_add_vvas_srcid_meta (buffer);
  meta->src_id = fpad->pad_idx;

  g_mutex_lock (&fpad->lock);
  /* Add buffer to the pad's queue */
  queue_len = g_queue_get_length (fpad->queue);
  if (queue_len >= vvas_xfunnel->queue_size) {
    /* Pad's queue is full, wait till some space becomes available,
     * processing_thread will unblock us when the space becomes available */
    GST_LOG_OBJECT (fpad, "wait for free space in the queue[%u]", queue_len);
    g_cond_wait (&fpad->cond, &fpad->lock);
  }
  g_queue_push_tail (fpad->queue, buffer);
  /* If processing thread is waiting for buffer, unblock it */
  g_cond_signal (&fpad->cond);
  g_mutex_unlock (&fpad->lock);

  return vvas_xfunnel->last_fret;
}

/**
 *  @fn static gboolean gst_vvas_xfunnel_is_all_pad_got_eos (GstVvas_Xfunnel * vvas_xfunnel)
 *  @param [in] vvas_xfunnel -  GstVvas_Xfunnel instance
 *  @return TRUE if all of the sink pads are at EOS, FALSE otherwise.
 *  @brief  This function checks if all of the sink pads has got EOS event and the custom EOS
 *          events are sent downstream or not.
 */
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
  /* Iterate all the sink pads and check whether all of them are at received
   * EOS or not.
   */

  while (!done) {
    switch (gst_iterator_next (itr, &item)) {
      case GST_ITERATOR_OK:{
        GstVvas_XfunnelPad *sinkpad;
        GstPad *pad;

        pad = g_value_get_object (&item);
        sinkpad = GST_VVAS_XFUNNEL_PAD_CAST (pad);

        g_mutex_lock (&sinkpad->lock);
        if (!sinkpad->got_eos || !sinkpad->is_eos_sent) {
          /* the sink pad will be considered at EOS only when it has got EOS
           * event and custom EOS is sent.
           */
          done = TRUE;
        }
        g_mutex_unlock (&sinkpad->lock);
        g_value_reset (&item);
      }
        break;

      case GST_ITERATOR_DONE:{
        /* Done with iterating all the elements */
        done = TRUE;
        all_eos = TRUE;
      }
        break;

      case GST_ITERATOR_RESYNC:{
        /* Sink element list is updated, re-sunc to the new list */
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

/**
 *  @fn static gboolean gst_vvas_xfunnel_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
 *  @param [in] pad       - The GstPad to handle the event.
 *  @param [in] parent    - The parent of the pad, which is the GstVvas_Xfunnel handle, typecasted to GstObject
 *  @param [in] event     - The GstEvent to handle.
 *  @return On Success returns TRUE\n On Failure returns FALSE
 *  @brief  Handles GstEvent coming over the sink pad. Ex : EOS, New caps etc.
 *  @details  This function is a callback function for any new event coming on the sink pad.
 */
static gboolean
gst_vvas_xfunnel_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (parent);
  GstVvas_XfunnelPad *fpad = GST_VVAS_XFUNNEL_PAD_CAST (pad);
  gboolean forward = TRUE;
  gboolean res = TRUE;
  gboolean live = FALSE;
  GstEvent *local_event = gst_event_copy (event);
  /* We need to add pad-index info into the outgoing event, by making copy
   * we are insuring that it becomes writable
   */
  gst_event_unref (event);
  event = local_event;

  GST_DEBUG_OBJECT (pad, "received event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {

    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:{
      /* We are not handling FLUSH_START and FLUSH_STOP event */
      forward = FALSE;
    }
      break;

    case GST_EVENT_CAPS:{
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);

      GST_OBJECT_LOCK (vvas_xfunnel);
      if (!vvas_xfunnel->sink_caps) {
        //first caps event, need to forward this downstream
        vvas_xfunnel->sink_caps = gst_caps_copy (caps);
        GST_OBJECT_UNLOCK (vvas_xfunnel);

        if (!vvas_xfunnel->is_user_timeout) {
          GstVideoInfo vinfo = { 0 };
          gint fps_d, fps_n;
          gint delta;

          if (!gst_video_info_from_caps (&vinfo, caps)) {
            GST_ERROR_OBJECT (pad, "Failed to parse caps");
            return FALSE;
          }

          /* User hasn't set the sink_wait_timeout, calculate it as
           * 1/FPS + 10% of (1/FPS).
           * This delta is needed to avoid restrict check of sink_wait_timeout.
           * This will avoid skipping of sink pad operating in live mode.
           */

          fps_d = GST_VIDEO_INFO_FPS_D (&vinfo);
          fps_n = GST_VIDEO_INFO_FPS_N (&vinfo);
          if (!fps_n || !fps_d) {
            GST_ERROR_OBJECT (pad, "Invalid framerate %d/%d", fps_n, fps_d);
            return FALSE;
          }

          vvas_xfunnel->sink_wait_timeout_calculated =
              (guint) ((fps_d * 1000) / (float) fps_n);
          delta = (vvas_xfunnel->sink_wait_timeout_calculated * 10) / 100;
          //consider 10% as delta
          vvas_xfunnel->sink_wait_timeout_calculated += delta;
          GST_DEBUG_OBJECT (vvas_xfunnel, "sink_wait_timeout: %u ms",
              vvas_xfunnel->sink_wait_timeout_calculated);
        }

      } else {
        /* For funnel, caps on all sink pads must be same, this is to avoid
         * re-negotiation of caps on SRC pad always before sending buffer with
         * different caps.
         * We already have caps from first sink pad, compare it with current sink
         * pad's caps, deny this caps if they don't match.
         */
        GstStructure *caps_struct;
        GstStructure *new_struct;
        gint caps_width, new_width, caps_height, new_height;
        gint caps_fps_num, caps_fps_den, new_fps_num, new_fps_den;
        const gchar *caps_format, *new_format;
        forward = FALSE;

        GST_OBJECT_UNLOCK (vvas_xfunnel);
        caps_struct = gst_caps_get_structure (vvas_xfunnel->sink_caps, 0);
        new_struct = gst_caps_get_structure (caps, 0);
        if (!caps_struct || !new_struct) {
          goto error;
        }
        /* extract format, width, height and framerate info from older and
         * new sink pad's caps */
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
        /* compare the extracted informations */
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
      if (!vvas_xfunnel->is_user_timeout) {
        /* If user did not set any timeout, check whether
         * any upstream element is live */
        live = gst_vvas_xfunnel_pad_is_live (vvas_xfunnel, pad);
        /* Insert the pad's response in Hash table and
         * adjust the timeout value based on mode of the
         * pads whether they are in live/non-live
         */
        GST_OBJECT_LOCK (vvas_xfunnel);
        g_hash_table_insert (vvas_xfunnel->live_pad_hash, pad,
            GINT_TO_POINTER (live));
        gst_vvas_xfunnel_update_sink_wait_timeout (vvas_xfunnel);
        GST_OBJECT_UNLOCK (vvas_xfunnel);
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

      /* Each sink pad will send segment event, if we simply forward all of them
       * downstream; there will be multiple segment events. To avoid this we
       * will send only one default segment event and
       * we will wrap this segment event into custom segment event.
       * Defunnel will convert this custom event back to gstreamer segment event
       * and forward it to the respective source pad.
       * With this method there won't be multiple segment events in between funnel
       * and defunnel and segment event will be forward to the respective sink element
       */

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
      /* Sink pad has sent EOS, as there can be other sink pad still playing,
       * we can't forward this event downstream as it will mark other elements
       * also for EOS. We will store this info and will send custom EOS for
       * defunnel, once all the sink pads are at EOS, final gstreamer EOS will
       * be sent from the processing_thread.
       */

      //Custom pad-eos and EOS will be sent from processing_thread
      g_mutex_lock (&fpad->lock);
      fpad->got_eos = TRUE;
      /* signal the pad that is waiting in processing thread
       * which is anticipated to exit */
      g_cond_signal (&fpad->cond);
      g_mutex_unlock (&fpad->lock);
      forward = FALSE;
    }
      break;

    default:
      break;
  }

  if (forward) {
    /* To identify which sink pad has sent this event, we are adding unique
     * sink pad info to all the events, defunnel will use this info to decide
     * to which source pad it should forward this event.
     */
    gst_vvas_xfunnel_add_sink_pad_to_event (vvas_xfunnel, event, fpad->pad_idx);
    res = gst_vvas_xfunnel_send_event (vvas_xfunnel, event);
  } else {
    /* We don't want to send this event downstream, freeing it */
    gst_event_unref (event);
  }
  return res;
}

/**
 *  @fn static gpointer gst_vvas_xfunnel_processing_thread (gpointer data)
 *  @param [in] data    - User data passed while creating thread
 *  @return Exit status of thread
 *  @brief  This function is the main thread which sends buffers from all the sink pads
 *          in round robin order.
 *  @details  This main thread iterates all the sink pads in round robin order,
 *            waits for the sink to submit data, switches to next sink pad if
 *            current sink pad is not able to provide data in the predefined time,
 *            It pushes buffer and required events downstream.
 */
static gpointer
gst_vvas_xfunnel_processing_thread (gpointer data)
{
  GstVvas_Xfunnel *vvas_xfunnel = GST_VVAS_XFUNNEL (data);
  GstElement *element = GST_ELEMENT_CAST (vvas_xfunnel);
  gboolean send_segment_event = TRUE;

  GST_DEBUG_OBJECT (vvas_xfunnel, "thread started");

  /* processing_thread will run until the thread is stopped explicitly from
   * the state_change function or when any fatal error occurs or when all of
   * the sink pad are at EOS.
   */
  while (TRUE) {
    GstIterator *itr = NULL;
    GValue item = { 0 };
    gboolean done = FALSE;
    gboolean break_loop = FALSE;

    g_mutex_lock (&vvas_xfunnel->mutex_lock);
    break_loop = vvas_xfunnel->is_exit_thread;
    g_mutex_unlock (&vvas_xfunnel->mutex_lock);
    /* Stop processing_thread */
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

    /* Iterate all sink pads */
    itr = gst_element_iterate_sink_pads (element);
    while (!done) {
      switch (gst_iterator_next (itr, &item)) {
        case GST_ITERATOR_OK:{
          GstVvas_XfunnelPad *fpad;
          GstPad *pad;
          GstBuffer *buffer = NULL;
          guint queue_len = 0;
          gboolean is_send_pad_eos = FALSE;
          g_mutex_lock (&vvas_xfunnel->mutex_lock);
          break_loop = vvas_xfunnel->is_exit_thread;
          g_mutex_unlock (&vvas_xfunnel->mutex_lock);

          /* Stop processing_thread */
          if (break_loop) {
            GST_DEBUG_OBJECT (vvas_xfunnel, "exit thread");
            break;
          }
          pad = g_value_get_object (&item);
          fpad = GST_VVAS_XFUNNEL_PAD_CAST (pad);
          g_mutex_lock (&fpad->lock);
          /* Get sink's queue length */
          queue_len = g_queue_get_length (fpad->queue);

          GST_DEBUG_OBJECT (fpad,
              "pad_%u: queue_len= %u, eos: %d, eos_sent: %d", fpad->pad_idx,
              queue_len, fpad->got_eos, fpad->is_eos_sent);
          if (queue_len > 0) {
            /* Sink pad's queue has buffer, pop it */
            buffer = g_queue_pop_head (fpad->queue);
            fpad->time = g_get_monotonic_time ();
          } else {
            /* Sink pad doesn't have data */
            if (!fpad->got_eos) {
              gint64 current_time, elapsed_time;
              gint64 elapsed_miliseconds;
              guint64 sink_wait_timeout;
              current_time = g_get_monotonic_time ();

              GST_OBJECT_LOCK (vvas_xfunnel);
              sink_wait_timeout = vvas_xfunnel->sink_wait_timeout;
              GST_OBJECT_UNLOCK (vvas_xfunnel);
              /* sink pad hasn't submitted the buffer to its queue, need to wait
               * till sink_wait_timeout is elapsed before switching to the next
               * sink pad.
               */
              elapsed_time =
                  (fpad->time >= 0) ? (current_time - fpad->time) : 0;
              elapsed_miliseconds = elapsed_time / G_TIME_SPAN_MILLISECOND;
              if (elapsed_miliseconds < sink_wait_timeout) {
                //Need to wait still
                gboolean is_signalled;
                gint64 end_time;
                gint64 wait_time;

                wait_time = (sink_wait_timeout *
                    G_TIME_SPAN_MILLISECOND) - elapsed_time;
                GST_DEBUG_OBJECT (fpad, "elapsed: %ld, waiting for %ld in us",
                    elapsed_time, wait_time);
                end_time = g_get_monotonic_time () + wait_time;
                is_signalled =
                    g_cond_wait_until (&fpad->cond, &fpad->lock, end_time);
                if (is_signalled) {
                  if (g_queue_get_length (fpad->queue) > 0) {
                    /* sink has submitted the buffer, pop it */
                    buffer = g_queue_pop_head (fpad->queue);
                    GST_DEBUG_OBJECT (fpad, "Signaled in %ld us",
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
              /* sink pad has got EOS, send custom EOS event if not sent already */
              if (!fpad->is_eos_sent) {
                is_send_pad_eos = TRUE;
              }
            }
          }
          /* signal sink_chain function if it is waiting */
          g_cond_signal (&fpad->cond);
          g_mutex_unlock (&fpad->lock);

          if (G_LIKELY (buffer)) {
            /* We have got the buffer, send it on SRC pad */
            if (G_UNLIKELY (send_segment_event)) {
              GstSegment *segment;
              GstEvent *seg_event;

              /* SEGMENT event must be sent before sending the first buffer */
              segment = gst_segment_new ();
              gst_segment_init (segment, GST_FORMAT_TIME);
              seg_event = gst_event_new_segment (segment);

              gst_vvas_xfunnel_send_event (vvas_xfunnel, seg_event);

              gst_segment_free (segment);
              /* No need to send Segment event now */
              send_segment_event = FALSE;
            }

            GST_DEBUG_OBJECT (fpad, "pad_%u: pushing %" GST_PTR_FORMAT,
                fpad->pad_idx, buffer);

            /* Push the buffer downstream */
            GST_PAD_STREAM_LOCK (vvas_xfunnel->srcpad);
            vvas_xfunnel->last_fret =
                gst_pad_push (vvas_xfunnel->srcpad, buffer);
            GST_PAD_STREAM_UNLOCK (vvas_xfunnel->srcpad);
            GST_DEBUG_OBJECT (vvas_xfunnel, "buffer push res: %s",
                gst_flow_get_name (vvas_xfunnel->last_fret));
          }

          if (G_UNLIKELY (is_send_pad_eos)) {
            /* Sink pad hsa got EOS, send the custom EOS for defunnel */
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
          /* done iterating all the sink elements */
          done = TRUE;
        }
          break;

        case GST_ITERATOR_RESYNC:{
          /* sink pad's list has changed, re-sync to the new list */
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
          /* done with processing, exit the processing thread now */
          break;
        }
      }
    }
  }
  GST_DEBUG_OBJECT (vvas_xfunnel, "exiting thread");
  return NULL;
}

/**
 *  @fn static GstStateChangeReturn gst_vvas_xmulticrop_change_state (GstElement * element, GstStateChange transition)
 *  @param [in] element       - Handle to GstVvas_Xfunnel typecasted to GstElement.
 *  @param [in] transition    - The requested state transition.
 *  @return Status of the state transition.
 *  @brief  This API will be invoked whenever the pipeline is going into a state transition and in this function the
 *          element can can initialize any sort of specific data needed by the element.
 *  @details  This API is registered with GstElementClass by overriding GstElementClass::change_state function pointer
 *            and this will be invoked whenever the pipeline is going into a state transition.
 */
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

  /* Inform parent_class about the state transition */
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* PAUSED -> READY transition, stop processing_thread */
      if (vvas_xfunnel->processing_thread) {
        g_mutex_lock (&vvas_xfunnel->mutex_lock);
        vvas_xfunnel->is_exit_thread = TRUE;
        g_mutex_unlock (&vvas_xfunnel->mutex_lock);
        gst_vvas_xfunnel_signal_all_pads (vvas_xfunnel);
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

/**
 *  @fn static gboolean vvas_xfunnel_plugin_init (GstPlugin * plugin)
 *  @param [in] plugin - Handle to vvas_xfunnel plugin
 *  @return TRUE if plugin initialized successfully
 *  @brief  This is a callback function that will be called by the loader at startup to register the plugin
 *  @note   It create a new element factory capable of instantiating objects of the type
 *          'GST_TYPE_VVAS_XFUNNEL' and adds the factory to plugin 'vvas_xfunnel'
 */
static gboolean
vvas_xfunnel_plugin_init (GstPlugin * plugin)
{
  /* register vvas_xfunnel plugin */
  return gst_element_register (plugin, "vvas_xfunnel", GST_RANK_PRIMARY,
      GST_TYPE_VVAS_XFUNNEL);
}

/**
 *  @brief This macro is used to define the entry point and meta data of a plugin.
 *         This macro exports a plugin, so that it can be used by other applications
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xfunnel,
    "Xilinx funnel plugin to serialize streams using round robin algorithm",
    vvas_xfunnel_plugin_init, VVAS_API_VERSION, "MIT/X11",
    "Xilinx VVAS SDK plugin", "https://www.xilinx.com/")
