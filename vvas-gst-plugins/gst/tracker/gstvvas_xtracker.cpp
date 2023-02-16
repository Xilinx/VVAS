/*
 * Copyright 2022 Xilinx, Inc.
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/vvas/gstinferencemeta.h>
#include <gst/vvas/gstinferenceprediction.h>
#include <gst/vvas/gstvvasallocator.h>
#include <gst/vvas/gstvvascoreutils.h>
#include <gst/vvas/gstvvassrcidmeta.h>
#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#endif

#include "gstvvas_xtracker.h"
#include <vvas_core/vvas_context.h>
#include <vvas_core/vvas_tracker.hpp>
#include <vvas_utils/vvas_node.h>
extern "C"
{
#include <gst/vvas/gstvvasutils.h>
}

/**
 *  @brief Defines a static GstDebugCategory global variable "gst_vvas_xtracker_debug"
 */
GST_DEBUG_CATEGORY_STATIC (gst_vvas_xtracker_debug);

/** @def GST_CAT_DEFAULT
 *  @brief Setting gst_vvas_xtracker_debug as default debug category for logging
 */
#define GST_CAT_DEFAULT gst_vvas_xtracker_debug
/**
 *  @brief Defines a static GstDebugCategory global variable with name
 *         GST_CAT_PERFORMANCE for performance logging purpose
 */
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

typedef struct _GstVvas_XTrackerPrivate GstVvas_XTrackerPrivate;

/** @enum
 *  @brief  Contains properties related to tracker configuration
 */
enum
{
  /** default */
  PROP_0,
  /** Tracker algorithm type */
  PROP_TRACKER_TYPE,
  /** flag to use color based matching or not in IOU algorithm */
  PROP_IOU_USE_COLOR,
  /** Color space to be used for object matching */
  PROP_USE_MATCHING_COLOR_SPACE,
  /** Feature length for KCF tracker */
  PROP_FEATURE_LENGTH,
  /** Enum to set search scales to be used in KCF tracker */
  PROP_SEARCH_SCALE,
  /** Inactive time period for objects to stop tracking */
  PROP_INACTIVE_WAIT_INTERVAL,
  /** Minimum object width for tracking */
  PROP_MIN_OBJECT_WIDTH,
  /** Minimum object height for tracking */
  PROP_MIN_OBJECT_HEIGHT,
  /** Maximum width above which objects are not tracked */
  PROP_MAX_OBJECT_WIDTH,
  /** Maximum height above which objects are not tracked */
  PROP_MAX_OBJECT_HEIGHT,
  /** Number of consecutive frames detection for considering tracking */
  PROP_NUM_FRAMES_CONFIDENCE,
  /** IOU search scale for object matching */
  PROP_MATCHING_SEARCH_REGION,
  /** Search scale for KCF tracker */
  PROP_RELATIVE_SEARCH_REGION,
  /** Correlation threshold for object matching */
  PROP_CORRELATION_THRESHOLD,
  /** Overlap threshold for object matching */
  PROP_OVERLAP_THRESHOLD,
  /** Scale change threshold for object matching */
  PROP_SCALE_CHANGE_THRESHOLD,
  /** Correlation weightage for object matching */
  PROP_CORRELATION_WEIGHT,
  /** Overlap weightage for object matching */
  PROP_OVERLAP_WEIGHT,
  /** Scale change wieghtage for object matching */
  PROP_SCALE_CHANGE_WEIGHT,
  /** Occlusion threshold for considering objects are under occlusion */
  PROP_OCCLUSION_THRESHOLD,
  /** Tracker confidence threshold to consider object tracked properly */
  PROP_CONFIDENCE_SCORE_THRESHOLD,
  /** Flag to enable marking of inactive objects */
  PROP_SKIP_INACTIVE_OBJS,
};

/** @struct TrackerInstances
 *  @brief  Holds tracker instances
 */
struct TrackerInstances
{
  /** pointer to base tracker */
  VvasTracker *vvasbase_tracker;
};

/** @struct _GstVvas_XTrackerPrivate
 *  @brief  Holds private members related tracker
 */
struct _GstVvas_XTrackerPrivate
{
  /** Contains image properties from input caps */
  GstVideoInfo *in_vinfo;
  /** Contains tracker configure information */
  VvasTrackerconfig tconfig;
  /** contains sourceId and tracker instances mapping */
  GHashTable *tracker_instances_hash;
  /** global context for vvas tracker */
  VvasContext *vvas_gctx;
};

/**
 *  @brief Defines sink pad template
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{NV12}")));

/**
 *  @brief Defines source pad template
 */
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{NV12}")));

#define gst_vvas_xtracker_parent_class parent_class

/** @brief  Glib's convenience macro for GstVvas_XTracker type implementation.
 *  @details This macro does below tasks:\n
 *           - Declares a class initialization function with prefix gst_vvas_xtracker \n
 */
G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_XTracker, gst_vvas_xtracker,
    GST_TYPE_BASE_TRANSFORM);

/** @def GST_VVAS_XTRACKER_PRIVATE(self)
 *  @brief Get instance of GstVvas_XTrackerPrivate structure
 */
#define GST_VVAS_XTRACKER_PRIVATE(self) (GstVvas_XTrackerPrivate *) (gst_vvas_xtracker_get_instance_private (self))

/* Funtion declrations */
static void gst_vvas_xtracker_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vvas_xtracker_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_vvas_xtracker_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);
static gboolean gst_vvas_xtracker_sink_event(GstBaseTransform *trans,
    GstEvent *event);

/** @def GST_TYPE_VVAS_TRACKER_ALGO_TYPE
 *  @brief Registers a new static enumeration type with the name GstVvasTrackerAlgoType
 */
#define GST_TYPE_VVAS_TRACKER_ALGO_TYPE (gst_vvas_tracker_tracker_algo_type ())

/** @enum GstVvasTrackerAlgoType
 *  @brief Enum representing tracker algorithm type
 *  @note Algorithm to be used for tracking objects across frames
 */
typedef enum
{
  /** Intersection-Over-Union algorithm */
  GST_TRACKER_ALGO_IOU,
  /** Minimum Output Sum of Squared Error algorithm */
  GST_TRACKER_ALGO_MOSSE,
  /** Kernelized Correlation Filter algorithm */
  GST_TRACKER_ALGO_KCF,
  /** No Algorithm is specified. \p TRACKER_ALGO_KCF will be
     set as default algorithm */
  GST_TRACKER_ALGO_NONE,
} GstVvasTrackerAlgoType;

/**
 *  @fn GType gst_vvas_tracker_tracker_algo_type (void)
 *  @return enumeration identifier type
 *  @brief  Registers a new static enumeration type with the GstVvasTrackerAlgoType
 */
static GType
gst_vvas_tracker_tracker_algo_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue tracker_algo_type[] = {
      {GST_TRACKER_ALGO_IOU, "Tracker IOU Algorithm", "IOU"},
      {GST_TRACKER_ALGO_MOSSE, "Tracker MOSSE Algorithm", "MOSSE"},
      {GST_TRACKER_ALGO_KCF, "Tracker KCF Algorithm", "KCF"},
      {0, NULL, NULL}
    };
    qtype =
        g_enum_register_static ("GstVvasTrackerAlgoType", tracker_algo_type);
  }
  return qtype;
}

/** @def GST_TYPE_VVAS_TRACKER_FEATURE_LENGTH
 *  @brief Registers a new static enumeration type with the name GstVvasTrackerFeatureLength
 */
#define GST_TYPE_VVAS_TRACKER_FEATURE_LENGTH (gst_vvas_tracker_feature_length_type ())

/**
 *  @fn GType gst_vvas_tracker_feature_length_type (void)
 *  @return enumeration identifier type
 *  @brief  Registers a new static enumeration type with the GstVvasTrackerFeatureLength
 */
static GType
gst_vvas_tracker_feature_length_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue feature_length_type[] = {
      {22, "Feature length of 22", "22"},
      {31, "Feature length of 31", "31"},
      {0, NULL, NULL}
    };
    qtype =
        g_enum_register_static ("GstVvasTrackerFeatureLength",
        feature_length_type);
  }
  return qtype;
}

/** @def GST_TYPE_VVAS_TRACKER_SEARCH_SCALE
 *  @brief Registers a new static enumeration type with the name GstVvasTrackerSearchScale
 */
#define GST_TYPE_VVAS_TRACKER_SEARCH_SCALE (gst_vvas_tracker_search_scale_type ())

/** @enum GstVvasTrackerSearchScale
 *  @brief Enum representing search scales to be used for tracking
 *  @note To set search scale to be used during tracking. Default searches in all scales.
 */
typedef enum
{
  /** Search for object both in up, same and down scale */
  GST_SEARCH_SCALE_ALL,
   /** Search for object in up and same scale only */
  GST_SEARCH_SCALE_UP,
  /** Search for object in down and same scale only */
  GST_SEARCH_SCALE_DOWN,
  /** Search for in same scale */
  GST_SEARCH_SCALE_NONE,
} GstVvasTrackerSearchScale;

/**
 *  @fn GType gst_vvas_tracker_search_scale_type (void)
 *  @return enumeration identifier type
 *  @brief  Registers a new static enumeration type with the GstVvasTrackerSearchScale
 */
static GType
gst_vvas_tracker_search_scale_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue search_scale_type[] = {
      {GST_SEARCH_SCALE_ALL, "Search all scales (up, down and same)", "all"},
      {GST_SEARCH_SCALE_UP, "Search up and same scale", "up"},
      {GST_SEARCH_SCALE_DOWN, "Search down and same scale", "down"},
      {0, NULL, NULL}
    };
    qtype =
        g_enum_register_static ("GstVvasTrackerSearchScale", search_scale_type);
  }
  return qtype;
}

/** @def GST_TYPE_VVAS_TRACKER_MATCHING_COLOR_SPACE
 *  @brief Registers a new static enumeration type with the name GstVVasTrackerMatchColorSpace
 */
#define GST_TYPE_VVAS_TRACKER_MATCHING_COLOR_SPACE (gst_vvas_tracker_match_color_space ())

/** @enum GstVVasTrackerMatchColorSpace
 *  @brief Enum representing color space used for object matching
 *  @note Color space to be used during object matching.  RGB is less complex
 *        compare to HSV.
 */
typedef enum
{
  /** Use RGB color space for object matching */
  GST_TRACKER_USE_RGB,
  /** Use HSV (Hue-Saturation-Value) color space for object matching */
  GST_TRACKER_USE_HSV,
} GstVVasTrackerMatchColorSpace;

/**
 *  @fn GType gst_vvas_tracker_match_color_space (void)
 *  @return enumeration identifier type
 *  @brief  Registers a new static enumeration type with the GstVVasTrackerMatchColorSpace
 */
static GType
gst_vvas_tracker_match_color_space (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue match_color_space_type[] = {
      {GST_TRACKER_USE_RGB, "Uses rgb color space for matching", "rgb"},
      {GST_TRACKER_USE_HSV, "Uses hsv color space for matching", "hsv"},
      {0, NULL, NULL}
    };
    qtype =
        g_enum_register_static ("GstVVasTrackerMatchColorSpace",
        match_color_space_type);
  }
  return qtype;
}

/** @def GST_VVAS_TRACKER_TRACKER_ALGO_DEFAULT
 *  @brief Sets default algorithm for object tracking.
 */
#define GST_VVAS_TRACKER_TRACKER_ALGO_DEFAULT           (GST_TRACKER_ALGO_KCF)
/** @def GST_VVAS_TRACKER_IOU_USE_COLOR_FEATURE
 *  @brief Default setting to use color matching or not during IOU based tracking.
 */
#define GST_VVAS_TRACKER_IOU_USE_COLOR_FEATURE          (1)
/** @def GST_VVAS_TRACKER_USE_MATCHING_COLOR_SPACE
 *  @brief Default color space for matching objects.
 */
#define GST_VVAS_TRACKER_USE_MATCHING_COLOR_SPACE       (GST_TRACKER_USE_HSV)
/** @def GST_VVAS_TRACKER_FEATURE_LENGTH_DEFAULT
 *  @brief Default feature length for KCF tracker.
 */
#define GST_VVAS_TRACKER_FEATURE_LENGTH_DEFAULT         (31)
/** @def GST_VVAS_TRACKER_SEARCH_SCALE_DEFAULT
 *  @brief Default search scales to be used during KCF tracking.
 */
#define GST_VVAS_TRACKER_SEARCH_SCALE_DEFAULT           (GST_SEARCH_SCALE_ALL)

/** @def GST_VVAS_TRACKER_INACTIVE_WAIT_INTERVAL_DEFAULT
 *  @brief To set maximum inactive time period in terms of frames.
 */
#define GST_VVAS_TRACKER_INACTIVE_WAIT_INTERVAL_DEFAULT (200)

/** @def GST_VVAS_TRACKER_MIN_OBJECT_WIDTH_DEFAULT
 *  @brief Default minimum width of object for tracking.
 */
#define GST_VVAS_TRACKER_MIN_OBJECT_WIDTH_DEFAULT       (20)

/** @def GST_VVAS_TRACKER_MIN_OBJECT_HEIGHT_DEFAULT
 *  @brief Default minimum height of object for tracking.
 */
#define GST_VVAS_TRACKER_MIN_OBJECT_HEIGHT_DEFAULT      (60)

/** @def GST_VVAS_TRACKER_MAX_OBJECT_WIDTH_DEFAULT
 *  @brief Default maximum width above which object consider as noise.
 */
#define GST_VVAS_TRACKER_MAX_OBJECT_WIDTH_DEFAULT       (200)

/** @def GST_VVAS_TRACKER_MAX_OBJECT_HEIGHT_DEFAULT
 *  @brief Default maximum height above which object consider as noise.
 */
#define GST_VVAS_TRACKER_MAX_OBJECT_HEIGHT_DEFAULT      (360)

/** @def GST_VVAS_TRACKER_NUM_FRAMES_CONFIDENCE_DEFAULT
 *  @brief Default consecutive number of frame object need
 *         to be detected for tracking.
 */
#define GST_VVAS_TRACKER_NUM_FRAMES_CONFIDENCE_DEFAULT  (3)

/** @def GST_VVAS_TRACKER_MATCHING_SEARCH_REGION_DEFAULT
 *  @brief Default scale for matching objects using IOU.
 */
#define GST_VVAS_TRACKER_MATCHING_SEARCH_REGION_DEFAULT (1.5)

/** @def GST_VVAS_TRACKER_RELATIVE_SEARCH_REGION_DEFAULT
 *  @brief Sets default search scale factor for KCF tracker.
 */
#define GST_VVAS_TRACKER_RELATIVE_SEARCH_REGION_DEFAULT (1.5)

/** @def GST_VVAS_TRACKER_CORRELATION_THRESHOLD_DEFAULT
 *  @brief Default correlation threshold for object matching.
 */
#define GST_VVAS_TRACKER_CORRELATION_THRESHOLD_DEFAULT  (0.7)

/** @def GST_VVAS_TRACKER_OVERLAP_THRESHOLD_DEFAULT
 *  @brief SDefault overlap threshold for object matching.
 */
#define GST_VVAS_TRACKER_OVERLAP_THRESHOLD_DEFAULT      (0.0)

/** @def GST_VVAS_TRACKER_SCALE_CHANGE_THRESHOLD_DEFAULT
 *  @brief Default scale change threshold for object matching.
 */
#define GST_VVAS_TRACKER_SCALE_CHANGE_THRESHOLD_DEFAULT (0.7)

/** @def GST_VVAS_TRACKER_CORRELATION_WEIGHT_DEFAULT
 *  @brief Default weightage for correlation during objects matching.
 */
#define GST_VVAS_TRACKER_CORRELATION_WEIGHT_DEFAULT      (0.7)

/** @def GST_VVAS_TRACKER_OVERLAP_WEIGHT_DEFAULT
 *  @brief Default weightage for overlap during objects matching.
 */
#define GST_VVAS_TRACKER_OVERLAP_WEIGHT_DEFAULT          (0.2)

/** @def GST_VVAS_TRACKER_SCALE_CHANGE_WEIGHT_DEFAULT
 *  @brief Default weightage for object scale change during objects matching.
 */
#define GST_VVAS_TRACKER_SCALE_CHANGE_WEIGHT_DEFAULT     (0.1)

/** @def GST_VVAS_TRACKER_OCCLUSION_THRESHOLD_DEFAULT
 *  @brief Default value to consider objects under occlusion.
 */
#define GST_VVAS_TRACKER_OCCLUSION_THRESHOLD_DEFAULT    (0.4)

/** @def GST_VVAS_TRACKER_CONFIDENCE_SCORE_THRESHOLD_DEFAULT
 *  @brief Default confidence threshold to consider object is tracked properly.
 */
#define GST_VVAS_TRACKER_CONFIDENCE_SCORE_THRESHOLD_DEFAULT (0.25)

/** @def GST_VVAS_TRACKER_SKIP_INACTIVE_OBJS_DEFAULT
 *  @brief Default confidence threshold to consider object is tracked properly.
 */
#define GST_VVAS_TRACKER_SKIP_INACTIVE_OBJS_DEFAULT FALSE

/**
 *  @fn gboolean vvas_xtracker_deinit (GstVvas_XTracker * self)
 *  @param [inout] self - Pointer to GstVvas_XTracker structure.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Free all allocated memory of trackers and image buffers
 *  @note  Calls deinit_tracker function from tracker library to deallocate memory
 *         of tracker objects.
 *
 */
static gboolean
vvas_xtracker_deinit (GstVvas_XTracker * self)
{
  bool iret = TRUE;
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init(&iter, self->priv->tracker_instances_hash);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    struct TrackerInstances *instance;
    instance = (struct TrackerInstances *) value;
    if (instance && instance->vvasbase_tracker) {
      /* calling tracker deinitialization function */
      iret = vvas_tracker_destroy(instance->vvasbase_tracker);
      if (iret)
        GST_DEBUG_OBJECT (self, "successfully completed tracker deinit");
      else
        GST_ERROR_OBJECT (self, "Failed to free tracker instances");
    }
  }

  g_hash_table_unref(self->priv->tracker_instances_hash);
  return iret;
}

/**
 *  @fn gboolean gst_vvas_xtracker_start (GstBaseTransform * trans)
 *  @param [in] trans - Pointer to GstBaseTransform object.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  This API Allocates memory and sets the transform type.
 *  @details This API is registered with GObjectClass by overriding GstBaseTransform::start function pointer and
 *          this will be called when element start processing. It invokes tracker initialization and allocates memory.
 */
static gboolean
gst_vvas_xtracker_start (GstBaseTransform * trans)
{
  GstVvas_XTracker *self = GST_VVAS_XTRACKER (trans);
  GstVvas_XTrackerPrivate *priv = self->priv;
  VvasReturnType vret;

  self->priv = priv;
  priv->in_vinfo = gst_video_info_new ();

  /* Create global context for vvas core */
  priv->vvas_gctx = vvas_context_create (0, NULL, LOG_LEVEL_ERROR, &vret);
  if (!priv->vvas_gctx || VVAS_IS_ERROR (vret)) {
    GST_ERROR_OBJECT (self,
        "ERROR: Failed to create vvas global context for tracker\n");
  }

  gst_base_transform_set_in_place (trans, true);

  return TRUE;
}

/**
 *  @fn gboolean gst_vvas_xtracker_stop (GstBaseTransform * trans)
 *  @param [in] trans - Pointer to GstBaseTransform object.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  Free up Allocates memory.
 *  @details This API is registered with GObjectClass by overriding GstBaseTransform::stop function pointer and
 *          this will be called when element stops processing.
 *          It invokes tracker de-initialization and free up allocated memory.
 *
 */
static gboolean
gst_vvas_xtracker_stop (GstBaseTransform * trans)
{
  GstVvas_XTracker *self = GST_VVAS_XTRACKER (trans);
  GST_DEBUG_OBJECT (self, "stopping");

  if (self->priv->vvas_gctx) {
    vvas_context_destroy (self->priv->vvas_gctx);
  }
  gst_video_info_free (self->priv->in_vinfo);
  vvas_xtracker_deinit (self);
  return TRUE;
}

/**
 *  @fn gboolean gst_vvas_xtracker_set_caps (GstBaseTransform * trans,
 *                                GstCaps * incaps, GstCaps * outcaps)
 *  @param [in] trans - Pointer to GstBaseTransform object.
 *  @param [in] incaps - Pointer to input caps of GstCaps object.
 *  @param [in] outcaps - Pointer to output caps  of GstCaps object.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  API to get input and output capabilities.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::set_caps function pointer and
 *          this will be called to get the input and output capabilities.
 */
static gboolean
gst_vvas_xtracker_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVvas_XTracker *self = GST_VVAS_XTRACKER (trans);
  gboolean bret = TRUE;
  GstVvas_XTrackerPrivate *priv = self->priv;

  GST_INFO_OBJECT (self,
      "incaps = %" GST_PTR_FORMAT "and outcaps = %" GST_PTR_FORMAT, incaps,
      outcaps);

  /* Reading input caps into in_vinfo */
  if (!gst_video_info_from_caps (priv->in_vinfo, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse input caps");
    return FALSE;
  }

  return bret;
}

/**
 *  @fn static void gst_vvas_xtracker_class_init (GstVvas_XTrackerClass * klass)
 *  @param [in]klass  - Handle to GstVvas_XTrackerClass
 *  @return None
 *  @brief  Add properties and signals of GstVvas_XTracker to parent GObjectClass \n
 *          and overrides function pointers present in itself and/or its parent class structures
 *  @details This function publishes properties those can be set/get from application on GstVvas_XTracker object.
 *           And, while publishing a property it also declares type, range of acceptable values, default value,
 *           readability/writability and in which GStreamer state a property can be changed.
 */
static void
gst_vvas_xtracker_class_init (GstVvas_XTrackerClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *transform_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_vvas_xtracker_set_property;
  gobject_class->get_property = gst_vvas_xtracker_get_property;

  transform_class->start = gst_vvas_xtracker_start;
  transform_class->stop = gst_vvas_xtracker_stop;
  transform_class->set_caps = gst_vvas_xtracker_set_caps;
  transform_class->transform_ip = gst_vvas_xtracker_transform_ip;
  transform_class->sink_event = gst_vvas_xtracker_sink_event;

  /* Tracker algorithm */
  g_object_class_install_property (gobject_class, PROP_TRACKER_TYPE,
      g_param_spec_enum ("tracker-algo", "Tracker algorithm name",
          "Tracker algorithm to use",
          GST_TYPE_VVAS_TRACKER_ALGO_TYPE,
          GST_VVAS_TRACKER_TRACKER_ALGO_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* IOU with or without color feature */
  g_object_class_install_property (gobject_class, PROP_IOU_USE_COLOR,
      g_param_spec_boolean ("IOU-with-color", "IOU algorithm with color info",
          "To specify whether to use color feature with IOU or not", 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /* color space to be used for matching objects during detection */
  g_object_class_install_property (gobject_class, PROP_USE_MATCHING_COLOR_SPACE,
      g_param_spec_enum ("obj-match-color-space", "objects matching color space info", "color space to be used for matching objects during detection. \
		HSV is more accurate with minor performance impact", GST_TYPE_VVAS_TRACKER_MATCHING_COLOR_SPACE, GST_VVAS_TRACKER_USE_MATCHING_COLOR_SPACE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  /* Feature length */
  g_object_class_install_property (gobject_class, PROP_FEATURE_LENGTH,
      g_param_spec_enum ("feature-length", "Object feature length",
          "Object feature length (required only for KCF algorithm)",
          GST_TYPE_VVAS_TRACKER_FEATURE_LENGTH,
          GST_VVAS_TRACKER_FEATURE_LENGTH_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Scales to search */
  g_object_class_install_property (gobject_class, PROP_SEARCH_SCALE,
      g_param_spec_enum ("search-scale", "Scale type for object localization",
          "Scales to verify to localize the object",
          GST_TYPE_VVAS_TRACKER_SEARCH_SCALE,
          GST_VVAS_TRACKER_SEARCH_SCALE_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Number of inactive frames */
  g_object_class_install_property (gobject_class, PROP_INACTIVE_WAIT_INTERVAL,
      g_param_spec_uint ("inactive-wait-interval",
          "Wait interval for inactive objects",
          "Number of detection frames to wait before stopping tracking of inactive objects",
          1, G_MAXUINT, GST_VVAS_TRACKER_INACTIVE_WAIT_INTERVAL_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Minimum object width */
  g_object_class_install_property (gobject_class, PROP_MIN_OBJECT_WIDTH,
      g_param_spec_uint ("min-object-width", "Minimum object width",
          "Minimum object width in pixels to consider for tracking",
          1, G_MAXUINT, GST_VVAS_TRACKER_MIN_OBJECT_WIDTH_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Minimum object height */
  g_object_class_install_property (gobject_class, PROP_MIN_OBJECT_HEIGHT,
      g_param_spec_uint ("min-object-height", "Minimum object height",
          "Minimum object height in pixels to consider for tracking",
          1, G_MAXUINT, GST_VVAS_TRACKER_MIN_OBJECT_HEIGHT_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Maximum object width */
  g_object_class_install_property (gobject_class, PROP_MAX_OBJECT_WIDTH,
      g_param_spec_uint ("max-object-width", "Maximum object width",
          "Objects with more than maximum width are considered as noise",
          1, G_MAXUINT, GST_VVAS_TRACKER_MAX_OBJECT_WIDTH_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Maximum object height */
  g_object_class_install_property (gobject_class, PROP_MAX_OBJECT_HEIGHT,
      g_param_spec_uint ("max-object-height", "Maximum object height",
          "Objects with more than maximum height are considered as noise",
          1, G_MAXUINT, GST_VVAS_TRACKER_MAX_OBJECT_HEIGHT_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Number of frames to enable tracking - based on detection cycle and camera position */
  g_object_class_install_property (gobject_class, PROP_NUM_FRAMES_CONFIDENCE,
      g_param_spec_uint ("num-frames-confidence",
          "Number of frames to enable tracking",
          "Number of times object need to be detected continuously before tracking",
          1, G_MAXUINT, GST_VVAS_TRACKER_NUM_FRAMES_CONFIDENCE_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Search scale factor to be used for matching objects during detection interval */
  g_object_class_install_property (gobject_class, PROP_MATCHING_SEARCH_REGION,
      g_param_spec_float ("match-search-region",
          "Object search region to match with detected objects",
          "Object search region to match with detected objects",
          1, 2.0, GST_VVAS_TRACKER_MATCHING_SEARCH_REGION_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Relative search region - Based on camera position and average object speed */
  g_object_class_install_property (gobject_class, PROP_RELATIVE_SEARCH_REGION,
      g_param_spec_float ("relative-search-region",
          "Object search region with respect to detection coordinates",
          "Object search region with respect to detection coordinates",
          1, 2.5, GST_VVAS_TRACKER_RELATIVE_SEARCH_REGION_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Correlation threshold */
  g_object_class_install_property (gobject_class, PROP_CORRELATION_THRESHOLD,
      g_param_spec_float ("correlation-threshold",
          "Object correlation threshold value for matching",
          "Object correlation threshold value for matching",
          0.1, 1.0, GST_VVAS_TRACKER_CORRELATION_THRESHOLD_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Overlap threshold */
  g_object_class_install_property (gobject_class, PROP_OVERLAP_THRESHOLD,
      g_param_spec_float ("overlap-threshold",
          "Object overlap threshold to consider for matching",
          "Percentage of objects overlap should be above overlap-threshold to consider for matching",
          0.0, 1.0, GST_VVAS_TRACKER_OVERLAP_THRESHOLD_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Object scale change threshold */
  g_object_class_install_property (gobject_class, PROP_SCALE_CHANGE_THRESHOLD,
      g_param_spec_float ("scale-change-threshold",
          "Maximum object scale change threshold",
          "Maximum object scale change threshold to consider for matching."
          "Value of 1 means double the scale.",
          0.001, 1.0, GST_VVAS_TRACKER_SCALE_CHANGE_THRESHOLD_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Weightage for correlation */
  g_object_class_install_property (gobject_class, PROP_CORRELATION_WEIGHT,
      g_param_spec_float ("correlation-weight",
          "Weightage for correlation value",
          "Weightage for correlation value",
          0.0, 1.0, GST_VVAS_TRACKER_CORRELATION_WEIGHT_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Weightage for overlap */
  g_object_class_install_property (gobject_class, PROP_OVERLAP_WEIGHT,
      g_param_spec_float ("overlap-weight",
          "Weightage for overlap value",
          "Weightage for overlap value",
          0.0, 1.0, GST_VVAS_TRACKER_OVERLAP_WEIGHT_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Weightage for scale change */
  g_object_class_install_property (gobject_class, PROP_SCALE_CHANGE_WEIGHT,
      g_param_spec_float ("scale-change-weight",
          "Weightage for change in scale",
          "Weightage for change in scale",
          0.0, 1.0, GST_VVAS_TRACKER_SCALE_CHANGE_WEIGHT_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Occlusion threshold */
  g_object_class_install_property (gobject_class, PROP_OCCLUSION_THRESHOLD,
      g_param_spec_float ("occlusion-threshold",
          "Threshold for considering object as occluded",
          "Threshold for considering object as occluded",
          0.0, 1.0, GST_VVAS_TRACKER_OCCLUSION_THRESHOLD_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  /* Confidence score threshold */
  g_object_class_install_property (gobject_class,
      PROP_CONFIDENCE_SCORE_THRESHOLD,
      g_param_spec_float ("confidence-score-threshold",
          "Tracker confidence score threshold",
          "Confidence score of tracker to be consider for tracking object", 0.0,
          1.0, GST_VVAS_TRACKER_CONFIDENCE_SCORE_THRESHOLD_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));
  /* Enabling or disabling inactive objects */
  g_object_class_install_property (gobject_class, PROP_SKIP_INACTIVE_OBJS,
      g_param_spec_boolean ("skip-inactive-objs",
          "Flag to enable marking of inactive objects",
          "Flag to enable or disable marking of inactive objects. This marking of \
           inactive objects helps downstream plugins to process further or not",
          GST_VVAS_TRACKER_SKIP_INACTIVE_OBJS_DEFAULT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple (gstelement_class,
      "VVAS Tracker Plugin",
      "Object Tracking",
      "Performs Object tracking based on feature map",
      "Xilinx Inc <www.xilinx.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xtracker_debug, "vvas_xtracker", 0,
      "VVAS Tracker Plugin");
  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
}

/**
 *  @fn static void gst_vvas_xtracker_init (GstVvas_XTracker * self)
 *  @param [in] self  - Handle to GstVvas_XTracker instance
 *  @return None
 *  @brief  Initializes GstVvas_XTracker member variables to default values
 *
 */
static void
gst_vvas_xtracker_init (GstVvas_XTracker * self)
{
  GstVvas_XTrackerPrivate *priv = GST_VVAS_XTRACKER_PRIVATE (self);
  self->priv = priv;

  self->tracker_algo = GST_VVAS_TRACKER_TRACKER_ALGO_DEFAULT;
  self->search_scale = GST_VVAS_TRACKER_SEARCH_SCALE_DEFAULT;
  self->match_color = GST_VVAS_TRACKER_USE_MATCHING_COLOR_SPACE;

  if (self->tracker_algo == GST_TRACKER_ALGO_IOU)
    priv->tconfig.tracker_type = TRACKER_ALGO_IOU;
  else if (self->tracker_algo == GST_TRACKER_ALGO_MOSSE)
    priv->tconfig.tracker_type = TRACKER_ALGO_MOSSE;
  else if (self->tracker_algo == GST_TRACKER_ALGO_KCF)
    priv->tconfig.tracker_type = TRACKER_ALGO_KCF;

  if (self->search_scale == GST_SEARCH_SCALE_ALL)
    priv->tconfig.search_scales = SEARCH_SCALE_ALL;
  else if (self->search_scale == GST_SEARCH_SCALE_UP)
    priv->tconfig.search_scales = SEARCH_SCALE_UP;
  else if (self->search_scale == GST_SEARCH_SCALE_DOWN)
    priv->tconfig.search_scales = SEARCH_SCALE_DOWN;

  if (self->match_color == GST_TRACKER_USE_RGB)
    priv->tconfig.obj_match_color = TRACKER_USE_RGB;
  else if (self->match_color == GST_TRACKER_USE_HSV)
    priv->tconfig.obj_match_color = TRACKER_USE_HSV;

  priv->tconfig.iou_use_color = GST_VVAS_TRACKER_IOU_USE_COLOR_FEATURE;
  priv->tconfig.fet_length = GST_VVAS_TRACKER_FEATURE_LENGTH_DEFAULT;
  priv->tconfig.min_width = GST_VVAS_TRACKER_MIN_OBJECT_WIDTH_DEFAULT;
  priv->tconfig.min_height = GST_VVAS_TRACKER_MIN_OBJECT_HEIGHT_DEFAULT;
  priv->tconfig.max_width = GST_VVAS_TRACKER_MAX_OBJECT_WIDTH_DEFAULT;
  priv->tconfig.max_height = GST_VVAS_TRACKER_MAX_OBJECT_HEIGHT_DEFAULT;
  priv->tconfig.num_inactive_frames =
      GST_VVAS_TRACKER_INACTIVE_WAIT_INTERVAL_DEFAULT;
  priv->tconfig.num_frames_confidence =
      GST_VVAS_TRACKER_NUM_FRAMES_CONFIDENCE_DEFAULT;
  priv->tconfig.obj_match_search_region =
      GST_VVAS_TRACKER_MATCHING_SEARCH_REGION_DEFAULT;
  priv->tconfig.padding = GST_VVAS_TRACKER_RELATIVE_SEARCH_REGION_DEFAULT;
  priv->tconfig.dist_correlation_threshold =
      GST_VVAS_TRACKER_CORRELATION_THRESHOLD_DEFAULT;
  priv->tconfig.dist_overlap_threshold =
      GST_VVAS_TRACKER_OVERLAP_THRESHOLD_DEFAULT;
  priv->tconfig.dist_scale_change_threshold =
      GST_VVAS_TRACKER_SCALE_CHANGE_THRESHOLD_DEFAULT;
  priv->tconfig.dist_correlation_weight =
      GST_VVAS_TRACKER_CORRELATION_WEIGHT_DEFAULT;
  priv->tconfig.dist_overlap_weight = GST_VVAS_TRACKER_OVERLAP_WEIGHT_DEFAULT;
  priv->tconfig.dist_scale_change_weight =
      GST_VVAS_TRACKER_SCALE_CHANGE_WEIGHT_DEFAULT;
  priv->tconfig.occlusion_threshold =
      GST_VVAS_TRACKER_OCCLUSION_THRESHOLD_DEFAULT;
  priv->tconfig.confidence_score =
      GST_VVAS_TRACKER_CONFIDENCE_SCORE_THRESHOLD_DEFAULT;
  priv->tconfig.skip_inactive_objs =
      GST_VVAS_TRACKER_SKIP_INACTIVE_OBJS_DEFAULT;
  priv->tracker_instances_hash =
      g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
}

/**
 *  @fn static void gst_vvas_xtracker_set_property (GObject * object, guint prop_id,
 *                                                  const GValue * value, GParamSpec * pspec)
 *  @param [in] object - Handle to GstVvas_XTracker typecast to GObject
 *  @param [in] prop_id - Property ID as defined in properties enum
 *  @param [in] value - value GValue which holds property value set by user
 *  @param [in] pspec - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief This API stores values sent from the user in GstVvas_XTracker object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::set_property function pointer and
 *           this will be invoked when developer sets properties on GstVvas_XTracker object.
 *           Based on property value type, corresponding g_value_get_xxx API will be called to get
 *           property value from GValue handle.
 */
static void
gst_vvas_xtracker_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_XTracker *self = GST_VVAS_XTRACKER (object);
  GstVvas_XTrackerPrivate *priv = self->priv;

  switch (prop_id) {
    case PROP_TRACKER_TYPE:
      self->tracker_algo = g_value_get_enum (value);
      if (self->tracker_algo == GST_TRACKER_ALGO_IOU)
        priv->tconfig.tracker_type = TRACKER_ALGO_IOU;
      else if (self->tracker_algo == GST_TRACKER_ALGO_MOSSE)
        priv->tconfig.tracker_type = TRACKER_ALGO_MOSSE;
      else if (self->tracker_algo == GST_TRACKER_ALGO_KCF)
        priv->tconfig.tracker_type = TRACKER_ALGO_KCF;
      else
        GST_ERROR_OBJECT (self, "Invalid Tracker type %d set\n",
            self->tracker_algo);
      break;
    case PROP_IOU_USE_COLOR:
      priv->tconfig.iou_use_color = g_value_get_boolean (value);
      break;
    case PROP_USE_MATCHING_COLOR_SPACE:
      self->match_color = g_value_get_enum (value);
      if (self->match_color == GST_TRACKER_USE_RGB)
        priv->tconfig.obj_match_color = TRACKER_USE_RGB;
      else if (self->match_color == GST_TRACKER_USE_HSV)
        priv->tconfig.obj_match_color = TRACKER_USE_HSV;
      break;
    case PROP_FEATURE_LENGTH:
      priv->tconfig.fet_length = g_value_get_enum (value);
      break;
    case PROP_SEARCH_SCALE:
      self->search_scale = g_value_get_enum (value);
      if (self->search_scale == GST_SEARCH_SCALE_ALL)
        priv->tconfig.search_scales = SEARCH_SCALE_ALL;
      else if (self->search_scale == GST_SEARCH_SCALE_UP)
        priv->tconfig.search_scales = SEARCH_SCALE_UP;
      else if (self->search_scale == GST_SEARCH_SCALE_DOWN)
        priv->tconfig.search_scales = SEARCH_SCALE_DOWN;
      else
        GST_ERROR_OBJECT (self, "Invalid Search scale %d set\n",
            self->search_scale);
      break;
    case PROP_INACTIVE_WAIT_INTERVAL:
      priv->tconfig.num_inactive_frames = g_value_get_uint (value);
      break;
    case PROP_MIN_OBJECT_WIDTH:
      priv->tconfig.min_width = g_value_get_uint (value);
      break;
    case PROP_MIN_OBJECT_HEIGHT:
      priv->tconfig.min_height = g_value_get_uint (value);
      break;
    case PROP_MAX_OBJECT_WIDTH:
      priv->tconfig.max_width = g_value_get_uint (value);
      break;
    case PROP_MAX_OBJECT_HEIGHT:
      priv->tconfig.max_height = g_value_get_uint (value);
      break;
    case PROP_NUM_FRAMES_CONFIDENCE:
      priv->tconfig.num_frames_confidence = g_value_get_uint (value);
      break;
    case PROP_MATCHING_SEARCH_REGION:
      priv->tconfig.obj_match_search_region = g_value_get_float (value);
      break;
    case PROP_RELATIVE_SEARCH_REGION:
      priv->tconfig.padding = g_value_get_float (value);
      break;
    case PROP_CORRELATION_THRESHOLD:
      priv->tconfig.dist_correlation_threshold = g_value_get_float (value);
      break;
    case PROP_OVERLAP_THRESHOLD:
      priv->tconfig.dist_overlap_threshold = g_value_get_float (value);
      break;
    case PROP_SCALE_CHANGE_THRESHOLD:
      priv->tconfig.dist_scale_change_threshold = g_value_get_float (value);
      break;
    case PROP_CORRELATION_WEIGHT:
      priv->tconfig.dist_correlation_weight = g_value_get_float (value);
      break;
    case PROP_OVERLAP_WEIGHT:
      priv->tconfig.dist_overlap_weight = g_value_get_float (value);
      break;
    case PROP_SCALE_CHANGE_WEIGHT:
      priv->tconfig.dist_scale_change_weight = g_value_get_float (value);
      break;
    case PROP_OCCLUSION_THRESHOLD:
      priv->tconfig.occlusion_threshold = g_value_get_float (value);
      break;
    case PROP_CONFIDENCE_SCORE_THRESHOLD:
      priv->tconfig.confidence_score = g_value_get_float (value);
      break;
    case PROP_SKIP_INACTIVE_OBJS:
      priv->tconfig.skip_inactive_objs = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 *  @fn static void gst_vvas_xtracker_get_property (GObject * object, guint prop_id,
 *                                                  const GValue * value, GParamSpec * pspec)
 *  @param [in] object - Handle to GstVvas_XTracker typecasted to GObject
 *  @param [in] prop_id - Property ID as defined in properties enum
 *  @param [in] value - value GValue which holds property value set by user
 *  @param [in] pspec - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief This API gets values from GstVvas_XTracker object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::get_property function pointer and
 *           this will be invoked when developer want gets properties from GstVvas_XTracker object.
 *           Based on property value type,corresponding g_value_set_xxx API will be called to set value of GValue type.
 */
static void
gst_vvas_xtracker_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVvas_XTracker *self = GST_VVAS_XTRACKER (object);
  GstVvas_XTrackerPrivate *priv = self->priv;

  switch (prop_id) {
    case PROP_TRACKER_TYPE:
      g_value_set_enum (value, self->tracker_algo);
      break;
    case PROP_IOU_USE_COLOR:
      g_value_set_boolean (value, priv->tconfig.iou_use_color);
      break;
    case PROP_USE_MATCHING_COLOR_SPACE:
      g_value_set_enum (value, priv->tconfig.obj_match_color);
      break;
    case PROP_FEATURE_LENGTH:
      g_value_set_enum (value, priv->tconfig.fet_length);
      break;
    case PROP_SEARCH_SCALE:
      g_value_set_enum (value, self->search_scale);
      break;
    case PROP_INACTIVE_WAIT_INTERVAL:
      g_value_set_uint (value, priv->tconfig.num_inactive_frames);
      break;
    case PROP_MIN_OBJECT_WIDTH:
      g_value_set_uint (value, priv->tconfig.min_width);
      break;
    case PROP_MIN_OBJECT_HEIGHT:
      g_value_set_uint (value, priv->tconfig.min_height);
      break;
    case PROP_MAX_OBJECT_WIDTH:
      g_value_set_uint (value, priv->tconfig.max_width);
      break;
    case PROP_MAX_OBJECT_HEIGHT:
      g_value_set_uint (value, priv->tconfig.max_height);
      break;
    case PROP_NUM_FRAMES_CONFIDENCE:
      g_value_set_uint (value, priv->tconfig.num_frames_confidence);
      break;
    case PROP_MATCHING_SEARCH_REGION:
      g_value_set_float (value, priv->tconfig.obj_match_search_region);
      break;
    case PROP_RELATIVE_SEARCH_REGION:
      g_value_set_float (value, priv->tconfig.padding);
      break;
    case PROP_CORRELATION_THRESHOLD:
      g_value_set_float (value, priv->tconfig.dist_correlation_threshold);
      break;
    case PROP_OVERLAP_THRESHOLD:
      g_value_set_float (value, priv->tconfig.dist_overlap_threshold);
      break;
    case PROP_SCALE_CHANGE_THRESHOLD:
      g_value_set_float (value, priv->tconfig.dist_scale_change_threshold);
      break;
    case PROP_CORRELATION_WEIGHT:
      g_value_set_float (value, priv->tconfig.dist_correlation_weight);
      break;
    case PROP_OVERLAP_WEIGHT:
      g_value_set_float (value, priv->tconfig.dist_overlap_weight);
      break;
    case PROP_SCALE_CHANGE_WEIGHT:
      g_value_set_float (value, priv->tconfig.dist_scale_change_weight);
      break;
    case PROP_OCCLUSION_THRESHOLD:
      g_value_set_float (value, priv->tconfig.occlusion_threshold);
      break;
    case PROP_CONFIDENCE_SCORE_THRESHOLD:
      g_value_set_float (value, priv->tconfig.confidence_score);
      break;
   case PROP_SKIP_INACTIVE_OBJS:
      g_value_set_boolean (value, priv->tconfig.skip_inactive_objs);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 *  @fn static gboolean gst_vvas_xtracker_sink_event (GstBaseTransform * trans, GstEvent * event)
 *  @param [in] trans - xtracker's parents instance handle which will be type casted to xtracker instance
 *  @param [in] event - GstEvent received by xtracker instance on sink pad
 *  @return TRUE if event handled successfully
 *          FALSE if event is not handled
 *  @brief Handle the GstEvent and invokes parent's event vmethod if event is not handled by xtracker
 */

static gboolean gst_vvas_xtracker_sink_event(GstBaseTransform *trans,
                                            GstEvent *event)
{
  GstVvas_XTracker *self = GST_VVAS_XTRACKER(trans);
  GstVvas_XTrackerPrivate *priv = self->priv;
  gboolean bret = TRUE;
  const GstStructure *structure = NULL;
  guint pad_idx;

  switch (GST_EVENT_TYPE(event))
  {
    case GST_EVENT_STREAM_START:
    {
      struct TrackerInstances *instance;
      structure = gst_event_get_structure(event);
      gst_structure_get_uint(structure, "pad-index", &pad_idx);
      if (!gst_structure_get_uint(structure, "pad-index", &pad_idx))
      {
        /* may be without funnel i.e., single stream */
        pad_idx = 0;
      }
      instance = (struct TrackerInstances *)malloc(sizeof(struct TrackerInstances));

      /* calling tracker initialization function */
      instance->vvasbase_tracker = vvas_tracker_create(priv->vvas_gctx, &priv->tconfig);
      if (instance->vvasbase_tracker == NULL)
      {
        GST_ERROR_OBJECT(self, "Failed to create tracker instance");
        return FALSE;
      }
      g_hash_table_insert(priv->tracker_instances_hash, GUINT_TO_POINTER(pad_idx), instance);

      bret = gst_pad_event_default(trans->sinkpad,
	            gst_element_get_parent(GST_ELEMENT(trans)), event);
      break;
    }

    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      const GstStructure *structure = NULL;
      guint pad_idx;
      int iret;
      structure = gst_event_get_structure(event);
      if (!gst_structure_get_uint(structure, "pad-index", &pad_idx))
      {
        /* may be without funnel i.e., single stream */
        pad_idx = 0;
      }
      if (!g_strcmp0(gst_structure_get_name(structure), "pad-eos"))
      {
        struct TrackerInstances *instance =
	        (struct TrackerInstances *)g_hash_table_lookup(priv->tracker_instances_hash,
		  GUINT_TO_POINTER(pad_idx));
        GST_LOG_OBJECT(self, "received pad-eos");
        if (instance)
        {
          /* calling tracker deinitialization function */
          iret = vvas_tracker_destroy(instance->vvasbase_tracker);

          if (iret)
            GST_DEBUG_OBJECT(self, "successfully completed tracker deinit");
          else
            GST_ERROR_OBJECT(self, "Failed to free tracker instances");

          g_hash_table_remove(priv->tracker_instances_hash,
                              GINT_TO_POINTER(pad_idx));
        }
      }
    }

    default:
    {
      bret = gst_pad_event_default(trans->sinkpad,
	            gst_element_get_parent(GST_ELEMENT(trans)), event);
      break;
    }
  }
  return bret;
}

/**
 *  @fn gboolean gst_vvas_xtracker_transform_ip (GstBaseTransform * base, GstBuffer * buf)
 *  @param [inout] base - Pointer to GstBaseTransform object.
 *  @param [in] buf - Pointer to input buffer of type GstBuffer.
 *  @return TRUE on success \n
 *          FALSE on failure
 *  @brief  This API called every frame for inplace processing to updates the tracking objects info
 *  @details This API is registered with GObjectClass by overriding GstBaseTransform::transform_ip function pointer and
 *          this will be called for every frame for inplace processing. It prepares the input buffer
 *          for processing then invokes tracker. Upon processing updates prediction metadata with tracked objects.
 */
static GstFlowReturn
gst_vvas_xtracker_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  GstVvas_XTracker *self = GST_VVAS_XTRACKER (base);
  VvasReturnType vvas_ret = VVAS_RET_ERROR;
  VvasVideoFrame *pFrame;
  GstInferenceMeta *infer_meta = NULL;
  VvasInferPrediction *vvas_infer_meta = NULL;
  GstVvasSrcIDMeta *srcId_meta = NULL;
  struct TrackerInstances *instance;

  /* Get inference metadata from the Gstbuffer */
  infer_meta = ((GstInferenceMeta *) gst_buffer_get_meta (buf,
          gst_inference_meta_api_get_type ()));

  /* Convert gstinference meta to inference meta if Gstinference meta
     avalable. Else create inference meta structure */
  if (infer_meta != NULL)
    vvas_infer_meta = vvas_infer_from_gstinfer (infer_meta->prediction);

  /* Get SrcId metadata from the Gstbuffer */
  srcId_meta = ((GstVvasSrcIDMeta *)gst_buffer_get_meta(buf,
                        gst_vvas_srcid_meta_api_get_type()));

  if (srcId_meta)
  {
    instance = (struct TrackerInstances *)g_hash_table_lookup(
		    self->priv->tracker_instances_hash,
		    GUINT_TO_POINTER(srcId_meta->src_id));
  }
  else
  {
    /* may be single source... use index 0 */
    instance = (struct TrackerInstances *)g_hash_table_lookup(
		    self->priv->tracker_instances_hash, 0);
  }

  /* Check if buffer is from pool.  If buffer is from pool use aligments from
     pool to create vvas frame buffer */
  pFrame = vvas_videoframe_from_gstbuffer (self->priv->vvas_gctx, -1, buf,
      self->priv->in_vinfo, GST_MAP_READ);
  if (pFrame == NULL) {
    GST_ERROR_OBJECT (self, "Failed to convert gstbuffer to vvas video frame");
    return GST_FLOW_ERROR;
  }

  /* Calling vvas-core tracker function for frame processing */
  if (instance && instance->vvasbase_tracker) {
    vvas_ret = vvas_tracker_process (instance->vvasbase_tracker,
	                                 pFrame, &vvas_infer_meta);
  }
  else {
    GST_ERROR_OBJECT (self, "Tracker instance is not created");
    vvas_video_frame_free (pFrame);
    return GST_FLOW_ERROR;
  }

  if (VVAS_IS_ERROR (vvas_ret)) {
    GST_ERROR_OBJECT (self, "Failed to process frame");
    vvas_video_frame_free (pFrame);
    return GST_FLOW_ERROR;
  }

  vvas_video_frame_free (pFrame);

  if (vvas_infer_meta != NULL) {
    GstInferencePrediction *new_gst_pred = NULL;
    VvasList *iter = NULL;
    VvasList *pred_nodes = NULL;

    if (infer_meta == NULL) {
      infer_meta = (GstInferenceMeta *) gst_buffer_add_meta (buf,
          gst_inference_meta_get_info (), NULL);
    }

    pred_nodes = vvas_inferprediction_get_nodes (vvas_infer_meta);
    /** Convert root node */
    new_gst_pred = gst_infer_node_from_vvas_infer (vvas_infer_meta);
    /** Convert all leaf nodes and append to root */
    for (iter = pred_nodes; iter != NULL; iter = iter->next) {
      VvasInferPrediction *leaf = (VvasInferPrediction *) iter->data;
      gst_inference_prediction_append (new_gst_pred,
          gst_infer_node_from_vvas_infer (leaf));
    }
    vvas_list_free (pred_nodes);
    if (infer_meta->prediction)
      gst_inference_prediction_unref (infer_meta->prediction);
    infer_meta->prediction = new_gst_pred;
  }

  if (vvas_infer_meta != NULL) {
    vvas_inferprediction_free (vvas_infer_meta);
    vvas_infer_meta = NULL;
  }

  GST_LOG_OBJECT (self, "processed buffer %p", buf);

  return GST_FLOW_OK;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
plugin_init (GstPlugin * vvas_xtracker)
{
  return gst_element_register (vvas_xtracker, "vvas_xtracker", GST_RANK_PRIMARY,
      GST_TYPE_VVAS_XTRACKER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vvas_xtracker,
    "GStreamer VVAS plug-in for tracker",
    plugin_init, "0.1", GST_LICENSE_UNKNOWN,
    "GStreamer Xilinx Tracker", "http://xilinx.com/")
