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

/* Gstvvasxreorderframe
 * The vvas_xreorderframe element rearranges the buffers and pushes to defunnel element
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/vvas/gstvvassrcidmeta.h>
#include "gstvvas_xreorderframe.h"

/** @def GST_CAT_DEFAULT
 *  @brief Setting gst_vvas_xreorderframe_debug as default debug category for logging
 */
#define GST_CAT_DEFAULT gst_vvas_xreorderframe_debug

/**
 *  @brief Defines a static GstDebugCategory global variable "gst_vvas_xreorderframe_debug"
 */
GST_DEBUG_CATEGORY_STATIC (gst_vvas_xreorderframe_debug);

/**
 *  @brief Defines sink pad's template
 */
static GstStaticPadTemplate infer_sink_factory =
GST_STATIC_PAD_TEMPLATE ("infer_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate skip_sink_factory =
GST_STATIC_PAD_TEMPLATE ("skip_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/**
 *  @brief Defines source pad's template
 */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define gst_vvas_xreorderframe_parent_class parent_class
G_DEFINE_TYPE (Gstvvasxreorderframe, gst_vvas_xreorderframe, GST_TYPE_ELEMENT);

/* Static function's prototype */
static gboolean gst_vvas_xreorderframe_infer_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_vvas_xreorderframe_skip_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_vvas_xreorderframe_infer_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static GstFlowReturn gst_vvas_xreorderframe_skip_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static GstStateChangeReturn gst_vvas_xreorderframe_change_state (GstElement *
    element, GstStateChange transition);
static void gst_vvas_xreorderframe_dispose (GObject * object);

/**
 *  @fn static void gst_vvas_xreorderframe_class_init (GstvvasxreorderframeClass * klass)
 *  @param [in] klass  - Handle to GstvvasxreorderframeClass
 *  @return 	None
 *  @brief  	Add property of Gstvvasxreorderframe to parent GObjectClass and overrides function pointers
 *          	present in itself and/or its parent class structures
 *  @details    This function publishes property that can be set/get from application on Gstvvasxreorderframe object.
 *              And, while publishing a property it also declares type, range of acceptable values, default value,
 *              readability/writability and in which GStreamer state a property can be changed.
 */
static void
gst_vvas_xreorderframe_class_init (GstvvasxreorderframeClass * klass)
{
  /* GObject class init, override VMethods and install properties */
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  /* override GObject class vmethods */
  gstelement_class->change_state = gst_vvas_xreorderframe_change_state;
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_vvas_xreorderframe_dispose);

  /* set plugin's metadata */
  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Xilinx frame reorder plugin", "Generic",
      "Element rearranges the buffers and pushes to downstream",
      "Xilinx Inc <https://www.xilinx.com/>");

  /* Add sink and source pad templates */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&infer_sink_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&skip_sink_factory));
}

/**
 *  @fn static void gst_vvas_xreorderframe_init(Gstvvasxreorderframe *reorderframe)
 *  @param [in] reorderframe - Handle to Gstvvasxreorderframe instance
 *  @return None
 *  @brief  Initializes Gstvvasxreorderframe member variables to default and does one time
 *          object/memory allocations in object's lifecycle
 */
static void
gst_vvas_xreorderframe_init (Gstvvasxreorderframe * reorderframe)
{
  /* Add infer sink pad to reorderframe element */
  reorderframe->infer_sinkpad =
      gst_pad_new_from_static_template (&infer_sink_factory, "infer_sink");
  /* Override event function for infer sink pad */
  gst_pad_set_event_function (reorderframe->infer_sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xreorderframe_infer_sink_event));
  /* Override chain function for infer sink pad */
  gst_pad_set_chain_function (reorderframe->infer_sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xreorderframe_infer_chain));
  GST_PAD_SET_PROXY_CAPS (reorderframe->infer_sinkpad);
  gst_element_add_pad (GST_ELEMENT (reorderframe), reorderframe->infer_sinkpad);

  /* Add skip sink pad to reorderframe element */
  reorderframe->skip_sinkpad =
      gst_pad_new_from_static_template (&skip_sink_factory, "skip_sink");
  /* Override chain function for skip sink pad */
  gst_pad_set_chain_function (reorderframe->skip_sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xreorderframe_skip_chain));
  /* Override event function for skip sink pad */
  gst_pad_set_event_function (reorderframe->skip_sinkpad,
      GST_DEBUG_FUNCPTR (gst_vvas_xreorderframe_skip_sink_event));
  GST_PAD_SET_PROXY_CAPS (reorderframe->skip_sinkpad);
  gst_element_add_pad (GST_ELEMENT (reorderframe), reorderframe->skip_sinkpad);

  /* Add src pad to reorderframe element */
  reorderframe->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (reorderframe->srcpad);
  gst_element_add_pad (GST_ELEMENT (reorderframe), reorderframe->srcpad);

  /* Initialize the parameters */
  reorderframe->processing_thread = NULL;
  reorderframe->is_exit_thread = FALSE;
  reorderframe->is_waiting_for_buffer = FALSE;
  reorderframe->infer_buffers_len = 0;
  reorderframe->is_eos = FALSE;
  /* Create a hash table for maintain src_id and infer buffers queue mapping */
  reorderframe->infer_hash =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_queue_free);
  /* Create a hash table for maintain src_id and skip buffers queue mapping */
  reorderframe->skip_hash =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_queue_free);
  /* Create a hash table for maintain src_id and next valid FrameId mapping */
  reorderframe->frameId_hash =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, NULL);
  reorderframe->pad_eos_hash =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, NULL);

  /* Initialize the mutex locks */
  g_mutex_init (&reorderframe->infer_lock);
  g_mutex_init (&reorderframe->skip_lock);
  g_mutex_init (&reorderframe->thread_lock);
  g_mutex_init (&reorderframe->pad_eos_lock);
  g_cond_init (&reorderframe->infer_cond);
}

/**
 *  @fn static void gst_vvas_xreorderframe_dispose(GObject *object)
 *  @param [in] object       - The handle for Gstvvasxreorderframe.
 *  @brief  This function unref/free all the resources used by Gstvvasxreorderframe.
 *  @details    This function is a callback function for disposing Gstvvasxreorderframe.
 */
static void
gst_vvas_xreorderframe_dispose (GObject * object)
{
  Gstvvasxreorderframe *vvas_xreorderframe = GST_VVAS_XREORDERFRAME (object);
  GST_DEBUG_OBJECT (vvas_xreorderframe, "dispose");

  /* clear all the hash maps */
  g_hash_table_unref (vvas_xreorderframe->frameId_hash);
  g_hash_table_unref (vvas_xreorderframe->skip_hash);
  g_hash_table_unref (vvas_xreorderframe->infer_hash);
  g_hash_table_unref (vvas_xreorderframe->pad_eos_hash);

  /* clear all locks and cond */
  g_mutex_clear (&vvas_xreorderframe->infer_lock);
  g_mutex_clear (&vvas_xreorderframe->skip_lock);
  g_mutex_clear (&vvas_xreorderframe->thread_lock);
  g_mutex_clear (&vvas_xreorderframe->pad_eos_lock);
  g_cond_clear (&vvas_xreorderframe->infer_cond);

  G_OBJECT_CLASS (gst_vvas_xreorderframe_parent_class)->dispose (object);
}

/**
 *  @fn static gpointer gst_vvas_xreorderframe_processing_thread(gpointer data)
 *  @param [in] data       - The handle for Gstvvasxreorderframe.
 *  @brief  This thread handles pushing of buffers in proper order.
 *  @details    This function is a Thread callback created on GST_STATE_CHANGE_READY_TO_PAUSED and exited on GST_STATE_CHANGE_PAUSED_TO_READY.
 */
static gpointer
gst_vvas_xreorderframe_processing_thread (gpointer data)
{
  Gstvvasxreorderframe *reorderframe = GST_VVAS_XREORDERFRAME (data);
  GstFlowReturn res;

  while (TRUE) {
    gboolean break_loop = FALSE;
    GHashTableIter iter;
    gpointer key, value;

    if (reorderframe->infer_buffers_len == 0) {
      /* Infer buffers length is 0, wait for new infer buffers */
      g_mutex_lock (&reorderframe->infer_lock);
      reorderframe->is_waiting_for_buffer = TRUE;
      GST_DEBUG_OBJECT (reorderframe, "waiting for infer buffers");
      g_cond_wait (&reorderframe->infer_cond, &reorderframe->infer_lock);
      g_mutex_unlock (&reorderframe->infer_lock);
    }

    /* Check for is_exit_thread, if TRUE then exit from processing thread */
    g_mutex_lock (&reorderframe->thread_lock);
    break_loop = reorderframe->is_exit_thread;
    g_mutex_unlock (&reorderframe->thread_lock);
    if (break_loop) {
      break;
    }

    /* iterate infer queues first */
    g_mutex_lock (&reorderframe->infer_lock);
    g_hash_table_iter_init (&iter, reorderframe->infer_hash);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
      GQueue *infer_queue = NULL;
      gboolean isValidBuffer = TRUE;
      /* push all valid buffers from infer queue */
      infer_queue = value;
      while (infer_queue && g_queue_get_length (infer_queue) > 0
          && isValidBuffer) {
        GstBuffer *buf = NULL;
        GstVvasSrcIDMeta *srcId_meta = NULL;
        gulong frame_id;
        buf = g_queue_peek_head (infer_queue);
        if (!GST_IS_BUFFER (buf)) {
          GST_DEBUG_OBJECT (reorderframe, "invalid buffer");
          break;
        }
        srcId_meta = gst_buffer_get_vvas_srcid_meta (buf);
        if (srcId_meta
            && g_hash_table_lookup_extended (reorderframe->frameId_hash,
                GUINT_TO_POINTER (srcId_meta->src_id), NULL,
                (gpointer) & frame_id)) {
          /* if frameId matches push the buffer and pop it from queue */
          if (srcId_meta->frame_id == frame_id) {
            GST_DEBUG_OBJECT (reorderframe, "Pushing infer %" GST_PTR_FORMAT,
                buf);
            res = gst_pad_push (reorderframe->srcpad, buf);
            if (res < GST_FLOW_OK) {
              switch (res) {
                case GST_FLOW_FLUSHING:
                case GST_FLOW_EOS:
                  GST_DEBUG_OBJECT (reorderframe,
                      "failed to push buffer. reason EOS");
                  break;
                case GST_FLOW_ERROR:
                  GST_ERROR_OBJECT (reorderframe,
                      "Got GST_FLOW_ERROR from downstream. Exiting processing thread");
                  g_mutex_lock (&reorderframe->thread_lock);
                  reorderframe->is_exit_thread = TRUE;
                  g_mutex_unlock (&reorderframe->thread_lock);
                  break;
                default:
                  GST_DEBUG_OBJECT (reorderframe, "failed to push buffer.");
              }
              break;
            } else if (res == GST_FLOW_OK) {
              /* after pushing buffer increment the frame id and update the frameId_hash */
              frame_id++;
              g_hash_table_insert (reorderframe->frameId_hash,
                  GUINT_TO_POINTER (GUINT_TO_POINTER (srcId_meta->src_id)),
                  GULONG_TO_POINTER (frame_id));
              /* pop the buffer */
              if (infer_queue) {
                g_queue_pop_head (infer_queue);
                reorderframe->infer_buffers_len--;
              }
            }
          } else {
            /* if frameId didn't matched quit the loop and check for next source infer queue */
            isValidBuffer = FALSE;
          }
        } else {
          GST_ERROR_OBJECT (reorderframe,
              "something went wrong...could not be here");
          break;
        }
      }
    }
    g_mutex_unlock (&reorderframe->infer_lock);

    /* iterate skip queues here */
    g_mutex_lock (&reorderframe->skip_lock);
    g_hash_table_iter_init (&iter, reorderframe->skip_hash);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
      GQueue *skip_queue = NULL;
      gboolean isValidBuffer = TRUE;
      /* iterate one skip queue and push all valid buffers */
      skip_queue = value;
      while (skip_queue && g_queue_get_length (skip_queue) > 0 && isValidBuffer) {
        GstBuffer *buf = NULL;
        GstVvasSrcIDMeta *srcId_meta = NULL;
        gulong frame_id;
        buf = g_queue_peek_head (skip_queue);
        if (!GST_IS_BUFFER (buf)) {
          GST_DEBUG_OBJECT (reorderframe, "invalid buffer");
          break;
        }
        srcId_meta = gst_buffer_get_vvas_srcid_meta (buf);
        if (srcId_meta
            && g_hash_table_lookup_extended (reorderframe->frameId_hash,
                GUINT_TO_POINTER (srcId_meta->src_id), NULL,
                (gpointer) & frame_id)) {
          /* if frame id matches push the buffer and pop it from queue */
          if (srcId_meta->frame_id == frame_id) {
            GST_DEBUG_OBJECT (reorderframe,
                "Pushing skip buffer %" GST_PTR_FORMAT, buf);
            res = gst_pad_push (reorderframe->srcpad, buf);
            if (res < GST_FLOW_OK) {
              switch (res) {
                case GST_FLOW_FLUSHING:
                case GST_FLOW_EOS:
                  GST_DEBUG_OBJECT (reorderframe,
                      "failed to push buffer. reason %s",
                      gst_flow_get_name (res));
                  break;
                case GST_FLOW_ERROR:
                  GST_ERROR_OBJECT (reorderframe,
                      "Got GST_FLOW_ERROR from downstream. Exiting processing thread");
                  g_mutex_lock (&reorderframe->thread_lock);
                  reorderframe->is_exit_thread = TRUE;
                  g_mutex_unlock (&reorderframe->thread_lock);
                  break;
                default:
                  GST_DEBUG_OBJECT (reorderframe, "failed to push buffer.");
              }
              break;
            } else if (res == GST_FLOW_OK) {
              /* after pushing buffer increement frame id and update frameId hash */
              frame_id++;
              g_hash_table_insert (reorderframe->frameId_hash,
                  GUINT_TO_POINTER (GUINT_TO_POINTER (srcId_meta->src_id)),
                  GULONG_TO_POINTER (frame_id));
              /* pop the buffer */
              if (skip_queue) {
                g_queue_pop_head (skip_queue);
              }
            }
          } else {
            /* FrameId didn't matched quit the loop and iterate next src skip queue */
            isValidBuffer = FALSE;
          }
        } else {
          GST_ERROR_OBJECT (reorderframe,
              "something went wrong...could not be here");
          break;
        }
      }
    }
    g_mutex_unlock (&reorderframe->skip_lock);

    g_mutex_lock (&reorderframe->pad_eos_lock);
    g_hash_table_iter_init (&iter, reorderframe->pad_eos_hash);
    while (g_hash_table_iter_next (&iter, &key, &value)) {
      gboolean is_eos = GPOINTER_TO_INT (value);
      GQueue *infer_queue, *skip_queue;
      if (is_eos) {
        if (g_hash_table_lookup_extended (reorderframe->infer_hash,
                GUINT_TO_POINTER (key), NULL,
                (gpointer) & infer_queue) &&
            g_hash_table_lookup_extended (reorderframe->infer_hash,
                GUINT_TO_POINTER (key), NULL, (gpointer) & skip_queue)) {
          if (!g_queue_get_length (infer_queue)
              && !g_queue_get_length (skip_queue)) {
            GstStructure *event_struct = NULL;
            GstEvent *event = NULL;
            gboolean res = FALSE;
            g_hash_table_remove (reorderframe->infer_hash,
                GINT_TO_POINTER (key));
            g_hash_table_remove (reorderframe->skip_hash,
                GINT_TO_POINTER (key));
            g_hash_table_remove (reorderframe->frameId_hash,
                GINT_TO_POINTER (key));
            event_struct =
                gst_structure_new ("pad-eos", "pad-index", G_TYPE_UINT, key,
                NULL);
            if (event_struct) {
              event =
                  gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
                  event_struct);
            }
            GST_PAD_STREAM_LOCK (reorderframe->srcpad);
            GST_DEBUG_OBJECT (reorderframe, "sending event: %" GST_PTR_FORMAT,
                event);
            res = gst_pad_push_event (reorderframe->srcpad, event);
            GST_PAD_STREAM_UNLOCK (reorderframe->srcpad);
            if (!res) {
              GST_ERROR_OBJECT (reorderframe, "Failed to send event");
            } else {
              g_hash_table_remove (reorderframe->pad_eos_hash,
                  GINT_TO_POINTER (key));
            }
          }
        }
      }
    }
    g_mutex_unlock (&reorderframe->pad_eos_lock);

    /* check for final GStreamer EOS */
    if (reorderframe->is_eos) {
      gboolean is_queues_empty = TRUE;

      /* check whether infer queues are empty or not */
      g_mutex_lock (&reorderframe->infer_lock);
      g_hash_table_iter_init (&iter, reorderframe->infer_hash);
      while (g_hash_table_iter_next (&iter, &key, &value)) {
        GQueue *infer_queue = NULL;
        infer_queue = value;
        if (g_queue_get_length (infer_queue))
          is_queues_empty = FALSE;
      }
      g_mutex_unlock (&reorderframe->infer_lock);

      /* check whether skip queues are empty or not */
      g_mutex_lock (&reorderframe->skip_lock);
      g_hash_table_iter_init (&iter, reorderframe->skip_hash);
      while (g_hash_table_iter_next (&iter, &key, &value)) {
        GQueue *skip_queue = NULL;
        skip_queue = value;
        if (g_queue_get_length (skip_queue))
          is_queues_empty = FALSE;
      }
      g_mutex_unlock (&reorderframe->skip_lock);

      /* if both queues are empty then create GStreamer EOS and send it to downstream */
      if (is_queues_empty) {
        GstEvent *eos_event;
        gboolean res = FALSE;
        GST_DEBUG_OBJECT (reorderframe, "Creating GStreamer EOS");
        eos_event = gst_event_new_eos ();
        if (eos_event) {
          GST_PAD_STREAM_LOCK (reorderframe->srcpad);
          GST_DEBUG_OBJECT (reorderframe, "sending event: %" GST_PTR_FORMAT,
              eos_event);
          res = gst_pad_push_event (reorderframe->srcpad, eos_event);
          GST_PAD_STREAM_UNLOCK (reorderframe->srcpad);

          if (!res) {
            GST_ERROR_OBJECT (reorderframe, "Failed to send eos event");
          } else {
            GST_DEBUG_OBJECT (reorderframe,
                "done with reorder frame processing thread");
            break;
          }
        }
      }
    }
  }

  /* Exiting thread */
  GST_DEBUG_OBJECT (reorderframe, "exiting thread");
  return NULL;
}

/**
 *  @fn static GstStateChangeReturn gst_vvas_xreorderframe_change_state(GstElement *element, GstStateChange transition)
 *  @param [in] element       - The handle for Gstvvasxreorderframe.
 *  @param [in] transition    - The handle for GstStateChange
 *  @return On Success returns GST_STATE_CHANGE_SUCCESS
 *          On Failure returns GST_STATE_CHANGE_FAILURE
 *  @brief  Handles GstStateChange transition of  Gstvvasxreorderframe.
 *  @details    This function is a callback function for any new state change happening on Gstvvasxreorderframe.
 */
static GstStateChangeReturn
gst_vvas_xreorderframe_change_state (GstElement * element,
    GstStateChange transition)
{
  Gstvvasxreorderframe *reorderframe = GST_VVAS_XREORDERFRAME (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* start the reorder buffer processing thread */
      GST_DEBUG_OBJECT (reorderframe,
          "Starting reorder buffer processing thread");
      g_mutex_lock (&reorderframe->thread_lock);
      reorderframe->is_exit_thread = FALSE;

      reorderframe->processing_thread =
          g_thread_new ("vvas_xreorderframe-thread",
          gst_vvas_xreorderframe_processing_thread, reorderframe);
      g_mutex_unlock (&reorderframe->thread_lock);
      break;

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (reorderframe->processing_thread) {
        /* exit the thread */
        g_mutex_lock (&reorderframe->thread_lock);
        reorderframe->is_exit_thread = TRUE;
        g_cond_signal (&reorderframe->infer_cond);
        g_mutex_unlock (&reorderframe->thread_lock);

        GST_LOG_OBJECT (reorderframe,
            "waiting for reorder buffer processing thread to join");
        g_thread_join (reorderframe->processing_thread);
        reorderframe->processing_thread = NULL;
        GST_LOG_OBJECT (reorderframe, "processing thread joined");
      }
      break;

    default:
      break;
  }
  return ret;
}

/**
 *  @fn static gboolean gst_vvas_xreorderframe_skip_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
 *  @param [in] pad       - The GstPad to handle the event.
 *  @param [in] parent    - The parent of the pad, which is the Gstvvasxreorderframe handle, typecasted to GstObject
 *  @param [in] event     - The GstEvent to handle.
 *  @return On Success returns TRUE
 *          On Failure returns FALSE
 *  @brief  Handles GstEvent coming over the skip sink pad. Ex : EOS, New caps etc.
 *  @details    This function is a callback function for any new event coming on the sink pad.
 */
static gboolean
gst_vvas_xreorderframe_skip_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  Gstvvasxreorderframe *reorderframe;
  gboolean ret = FALSE;

  reorderframe = GST_VVAS_XREORDERFRAME (parent);

  GST_LOG_OBJECT (reorderframe, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      const GstStructure *structure = NULL;
      guint pad_idx;
      structure = gst_event_get_structure (event);
      if (!gst_structure_get_uint (structure, "pad-index", &pad_idx)) {
        pad_idx = 0;
      }
      if (!g_strcmp0 (gst_structure_get_name (structure), "pad-eos")) {
        GST_DEBUG_OBJECT (reorderframe,
            "unreffering pad-eos event on skip pad %u", pad_idx);
        gst_event_unref (event);
        return TRUE;
      }
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    case GST_EVENT_EOS:
    {
      GST_DEBUG_OBJECT (reorderframe, "unreffering eos event on skip pad");
      gst_event_unref (event);
      return TRUE;
      break;
    }
    default:
    {
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
  }

  return ret;
}

/**
 *  @fn static gboolean gst_vvas_xreorderframe_infer_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
 *  @param [in] pad       - The GstPad to handle the event.
 *  @param [in] parent    - The parent of the pad, which is the Gstvvasxreorderframe handle, typecasted to GstObject
 *  @param [in] event     - The GstEvent to handle.
 *  @return On Success returns TRUE
 *          On Failure returns FALSE
 *  @brief  Handles GstEvent coming over the infer sink pad. Ex : EOS, New caps etc.
 *  @details    This function is a callback function for any new event coming on the sink pad.
 */
static gboolean
gst_vvas_xreorderframe_infer_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  Gstvvasxreorderframe *reorderframe;
  gboolean ret = FALSE;

  reorderframe = GST_VVAS_XREORDERFRAME (parent);

  GST_LOG_OBJECT (reorderframe, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);
      /* set the caps to source pad */
      gst_pad_set_caps (reorderframe->srcpad, caps);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }

    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      const GstStructure *structure = NULL;
      guint pad_idx;
      structure = gst_event_get_structure (event);
      if (!gst_structure_get_uint (structure, "pad-index", &pad_idx)) {
        pad_idx = 0;
      }
      if (!g_strcmp0 (gst_structure_get_name (structure), "pad-eos")) {
        gboolean is_eos = TRUE;
        GST_DEBUG_OBJECT (reorderframe, "Got pad-eos event on pad %u", pad_idx);
        g_mutex_lock (&reorderframe->pad_eos_lock);
        /* got eos on infer sink pad. So, update pad_eos_hash entry for corrosponding srcId */
        g_hash_table_insert (reorderframe->pad_eos_hash,
            GUINT_TO_POINTER (pad_idx), GUINT_TO_POINTER (is_eos));
        g_mutex_unlock (&reorderframe->pad_eos_lock);
        gst_event_unref (event);
        return TRUE;
      }
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }

    case GST_EVENT_STREAM_START:
    {
      /* new src added, add entries in frameId_hash, infer_hash, skip_hash */
      GQueue *infer_queue, *skip_queue;
      const GstStructure *structure = NULL;
      guint pad_idx;
      gulong frame_id = 0;

      structure = gst_event_get_structure (event);
      if (!gst_structure_get_uint (structure, "pad-index", &pad_idx)) {
        pad_idx = 0;
      }
      g_hash_table_insert (reorderframe->frameId_hash,
          GUINT_TO_POINTER (pad_idx), GULONG_TO_POINTER (frame_id));
      g_hash_table_insert (reorderframe->pad_eos_hash,
          GUINT_TO_POINTER (pad_idx), FALSE);
      infer_queue = g_queue_new ();
      g_hash_table_insert (reorderframe->infer_hash, GUINT_TO_POINTER (pad_idx),
          (gpointer) infer_queue);
      skip_queue = g_queue_new ();
      g_hash_table_insert (reorderframe->skip_hash, GUINT_TO_POINTER (pad_idx),
          (gpointer) skip_queue);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    case GST_EVENT_EOS:
    {
      GST_DEBUG_OBJECT (reorderframe, "unreffering eos event on infer pad");
      gst_event_unref (event);
      reorderframe->is_eos = TRUE;
      return TRUE;
      break;
    }
    default:
    {
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
  }
  return ret;
}

/**
 *  @fn static GstFlowReturn gst_vvas_xreorderframe_infer_chain(GstPad *pad,
 *                             GstObject * parent, GstBuffer * buffer)
 *  @param [in] pad     - GstPad onto which data was pushed
 *  @param [in] parent  - Handle to Gstvvasxreorderframe instance
 *  @param [in] buffer  - Chained GstBuffer
 *  @return GST_FLOW_OK when buffer was successfully handled error otherwise
 *  @brief  This function will get invoked whenever a buffer is chained onto the infer pad.
 *  @details    The chain function is the function in which all data processing takes place.
 */
static GstFlowReturn
gst_vvas_xreorderframe_infer_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  Gstvvasxreorderframe *reorderframe;
  GQueue *infer_queue;
  GstVvasSrcIDMeta *srcId_meta;
  GstFlowReturn res = GST_FLOW_OK;

  reorderframe = GST_VVAS_XREORDERFRAME (parent);

  GST_DEBUG_OBJECT (reorderframe, "Received infer %" GST_PTR_FORMAT, buf);

  g_mutex_lock (&reorderframe->thread_lock);
  if (reorderframe->is_exit_thread) {
    GST_ERROR_OBJECT (reorderframe, " Xreorder processig thread exited.");
    g_mutex_unlock (&reorderframe->thread_lock);
    return GST_FLOW_ERROR;
  }
  g_mutex_unlock (&reorderframe->thread_lock);

  /* fill up the infer queues here based on sourceId */
  srcId_meta = gst_buffer_get_vvas_srcid_meta (buf);

  if (srcId_meta) {
    /* srcId meta is available. Get the infer queue corrosponding to srcId */
    g_mutex_lock (&reorderframe->infer_lock);
    if (g_hash_table_lookup_extended (reorderframe->infer_hash,
            GUINT_TO_POINTER (srcId_meta->src_id), NULL,
            (gpointer) & infer_queue)) {
      if (infer_queue) {
        /* Infer queue is available for that srcId. Push the buffer to infer queue */
        reorderframe->infer_buffers_len++;
        g_queue_push_tail (infer_queue, buf);
        /* Unblock the processing thread, if it is waiting for infer buffers */
        if (reorderframe->is_waiting_for_buffer) {
          g_cond_signal (&reorderframe->infer_cond);
          reorderframe->is_waiting_for_buffer = FALSE;
        }
      }
    } else {
      GST_ERROR_OBJECT (reorderframe,
          "infer queue is not yet created for this srcID");
      res = GST_FLOW_ERROR;
    }
    g_mutex_unlock (&reorderframe->infer_lock);
  } else {
    /* just push out the incoming buffer if srcid_meta is not available. Let defunnel handle it. */
    res = gst_pad_push (reorderframe->srcpad, buf);
  }

  return res;
}

/**
 *  @fn static GstFlowReturn gst_vvas_xreorderframe_skip_chain(GstPad *pad,
 *                             GstObject * parent, GstBuffer * buffer)
 *  @param [in] pad     - GstPad onto which data was pushed
 *  @param [in] parent  - Handle to Gstvvasxreorderframe instance
 *  @param [in] buffer  - Chained GstBuffer
 *  @return GST_FLOW_OK when buffer was successfully handled, otherwise returns error.
 *  @brief  This function will get invoked whenever a buffer is chained onto the skip pad.
 *  @details    The chain function is the function in which all data processing takes place.
 */
static GstFlowReturn
gst_vvas_xreorderframe_skip_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  Gstvvasxreorderframe *reorderframe;
  GQueue *skip_queue;
  GstVvasSrcIDMeta *srcId_meta;
  GstFlowReturn res = GST_FLOW_OK;

  reorderframe = GST_VVAS_XREORDERFRAME (parent);

  GST_DEBUG_OBJECT (reorderframe, "Received skip buffer  %" GST_PTR_FORMAT,
      buf);

  g_mutex_lock (&reorderframe->thread_lock);
  if (reorderframe->is_exit_thread) {
    GST_ERROR_OBJECT (reorderframe, " Xreorder processig thread exited.");
    g_mutex_unlock (&reorderframe->thread_lock);
    return GST_FLOW_ERROR;
  }
  g_mutex_unlock (&reorderframe->thread_lock);

  /* fill up the skip queues here based on sourceId */
  srcId_meta = gst_buffer_get_vvas_srcid_meta (buf);
  if (srcId_meta) {
    /* sourceid meta is available. Get the skip queuue for that sourceId */
    g_mutex_lock (&reorderframe->skip_lock);
    if (g_hash_table_lookup_extended (reorderframe->skip_hash,
            GUINT_TO_POINTER (srcId_meta->src_id), NULL,
            (gpointer) & skip_queue)) {
      if (skip_queue) {
        /* Push the buffer to skip queue */
        g_queue_push_tail (skip_queue, buf);
      }
    } else {
      GST_ERROR_OBJECT (reorderframe,
          "skip queue is not yet created for this srcID");
      res = GST_FLOW_ERROR;
    }
    g_mutex_unlock (&reorderframe->skip_lock);
  } else {
    /* just push out the incoming buffer if srcid_meta is not available. Let defunnel handle it. */
    res = gst_pad_push (reorderframe->srcpad, buf);
  }

  return res;
}

static gboolean
vvas_xreorderframe_init (GstPlugin * vvas_xreorderframe)
{
  GST_DEBUG_CATEGORY_INIT (gst_vvas_xreorderframe_debug, "vvas_xreorderframe",
      0, "debug category for vvas_xreorderframe");

  /* Register vvas_xreorderframe */
  return gst_element_register (vvas_xreorderframe, "vvas_xreorderframe",
      GST_RANK_NONE, GST_TYPE_VVAS_XREORDERFRAME);
}

#ifndef PACKAGE
#define PACKAGE "vvas_xreorderframe"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vvas_xreorderframe,
    "Xilinx reorder frame plugin to reorder and push out the incoming frames of different sources received from skipframe and infer plugins in the correct order of frame number",
    vvas_xreorderframe_init, "VVAS_API_VERSION",
    "MIT/X11", "Xilinx VVAS SDK plugin", "https://www.xilinx.com/")
