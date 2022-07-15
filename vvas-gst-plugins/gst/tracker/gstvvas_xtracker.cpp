/*
 * Copyright 2022 Xilinx, Inc.
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

#ifdef XLNX_PCIe_PLATFORM
#include <experimental/xrt-next.h>
#else
#include <xrt/experimental/xrt-next.h>
#endif
#ifdef XLNX_EMBEDDED_PLATFORM
#include <arm_neon.h>
#include "NE10.h"
#endif

#include "gstvvas_xtracker.h"
#include "tracker_algo/tracker.hpp"
#include "tracker_algo/correlation_filter.hpp"

#include "vvas/xrt_utils.h"

extern "C"
{
#include <gst/vvas/gstvvasutils.h>
}

GST_DEBUG_CATEGORY_STATIC (gst_vvas_xtracker_debug);
#define GST_CAT_DEFAULT gst_vvas_xtracker_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

pthread_mutex_t count_mutex;

typedef struct _GstVvas_XTrackerPrivate GstVvas_XTrackerPrivate;

enum
{
  PROP_0,
  PROP_TRACKER_TYPE,
  PROP_IOU_USE_COLOR,
  PROP_USE_MATCHING_COLOR_SPACE,
  PROP_FEATURE_LENGTH,
  PROP_SEARCH_SCALE,
  PROP_DETECTION_INTERVAL,
  PROP_INACTIVE_WAIT_INTERVAL,
  PROP_MIN_OBJECT_WIDTH,
  PROP_MIN_OBJECT_HEIGHT,
  PROP_MAX_OBJECT_WIDTH,
  PROP_MAX_OBJECT_HEIGHT,
  PROP_NUM_FRAMES_CONFIDENCE,
  PROP_MATCHING_SEARCH_REGION,
  PROP_RELATIVE_SEARCH_REGION,
  PROP_CORRELATION_THRESHOLD,
  PROP_OVERLAP_THRESHOLD,
  PROP_SCALE_CHANGE_THRESHOLD,
  PROP_CORRELATION_WEIGHT,
  PROP_OVERLAP_WEIGHT,
  PROP_SCALE_CHANGE_WEIGHT,
  PROP_OCCLUSION_THRESHOLD,
  PROP_CONFIDENCE_SCORE_THRESHOLD,
};

struct _GstVvas_XTrackerPrivate
{
  GstVideoInfo *in_vinfo;
  GstVideoInfo *out_vinfo;
  GstBufferPool *input_pool;
  GstVideoFrame in_vframe;
  VVASFrame *input[MAX_NUM_OBJECT];
  int ids;
  objs_data trk_objs;
  int fr_count;
  GstInferencePrediction *pr;
  char *img_data;
  tracker_handle trackers_data;
  track_config tconfig;
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{NV12}")));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{NV12}")));

#define gst_vvas_xtracker_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_XTracker, gst_vvas_xtracker,
    GST_TYPE_BASE_TRANSFORM);
#define GST_VVAS_XTRACKER_PRIVATE(self) (GstVvas_XTrackerPrivate *) (gst_vvas_xtracker_get_instance_private (self))

static void gst_vvas_xtracker_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vvas_xtracker_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_vvas_xtracker_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);
static void gst_vvas_xtracker_finalize (GObject * obj);

#define GST_TYPE_VVAS_TRACKER_ALGO_TYPE (gst_vvas_tracker_tracker_algo_type ())
typedef enum
{
  TRACKER_ALGO_IOU,
  TRACKER_ALGO_MOSSE,
  TRACKER_ALGO_KCF,
  TRACKER_ALGO_NONE,
} GstVvasTrackerAlgoType;

static GType
gst_vvas_tracker_tracker_algo_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue tracker_algo_type[] = {
      {TRACKER_ALGO_IOU, "Tracker IOU Algorithm", "IOU"},
      {TRACKER_ALGO_MOSSE, "Tracker MOSSE Algorithm", "MOSSE"},
      {TRACKER_ALGO_KCF, "Tracker KCF Algorithm", "KCF"},
      {0, NULL, NULL}
    };
    qtype =
        g_enum_register_static ("GstVvasTrackerAlgoType", tracker_algo_type);
  }
  return qtype;
}

#define GST_TYPE_VVAS_TRACKER_FEATURE_LENGTH (gst_vvas_tracker_feature_length_type ())
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

#define GST_TYPE_VVAS_TRACKER_SEARCH_SCALE (gst_vvas_tracker_search_scale_type ())
typedef enum
{
  SEARCH_SCALE_ALL,
  SEARCH_SCALE_UP,
  SEARCH_SCALE_DOWN,
  SEARCH_SCALE_NONE,
} GstVvasTrackerSearchScale;

static GType
gst_vvas_tracker_search_scale_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue search_scale_type[] = {
      {SEARCH_SCALE_ALL, "Search all scales (up, down and same)", "all"},
      {SEARCH_SCALE_UP, "Search up and same scale", "up"},
      {SEARCH_SCALE_DOWN, "Search down and same scale", "down"},
      {0, NULL, NULL}
    };
    qtype =
        g_enum_register_static ("GstVvasTrackerSearchScale", search_scale_type);
  }
  return qtype;
}

#define GST_TYPE_VVAS_TRACKER_MATCHING_COLOR_SPACE (gst_vvas_tracker_match_color_space ())
typedef enum
{
  TRACKER_USE_RGB,
  TRACKER_USE_HSV,
} GstVVasTrackerMatchColorSpace;

static GType
gst_vvas_tracker_match_color_space (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue match_color_space_type[] = {
      {TRACKER_USE_RGB, "Uses rgb color space for matching", "rgb"},
      {TRACKER_USE_HSV, "Uses hsv color space for matching", "hsv"},
      {0, NULL, NULL}
    };
    qtype =
        g_enum_register_static ("GstVVasTrackerMatchColorSpace",
        match_color_space_type);
  }
  return qtype;
}

#define GST_VVAS_TRACKER_TRACKER_ALGO_DEFAULT           (TRACKER_ALGO_KCF)
#define GST_VVAS_TRACKER_IOU_USE_COLOR_FEATURE          (1)
#define GST_VVAS_TRACKER_USE_MATCHING_COLOR_SPACE       (USE_HSV)
#define GST_VVAS_TRACKER_FEATURE_LENGTH_DEFAULT         (31)
#define GST_VVAS_TRACKER_SEARCH_SCALE_DEFAULT           (SEARCH_SCALE_ALL)
#define GST_VVAS_TRACKER_DETECTION_INTERVAL_DEFAULT     (5)
#define GST_VVAS_TRACKER_INACTIVE_WAIT_INTERVAL_DEFAULT (200)
#define GST_VVAS_TRACKER_MIN_OBJECT_WIDTH_DEFAULT       (20)
#define GST_VVAS_TRACKER_MIN_OBJECT_HEIGHT_DEFAULT      (60)
#define GST_VVAS_TRACKER_MAX_OBJECT_WIDTH_DEFAULT       (200)
#define GST_VVAS_TRACKER_MAX_OBJECT_HEIGHT_DEFAULT      (360)
#define GST_VVAS_TRACKER_NUM_FRAMES_CONFIDENCE_DEFAULT  (3)
#define GST_VVAS_TRACKER_MATCHING_SEARCH_REGION_DEFAULT (1.5)
#define GST_VVAS_TRACKER_RELATIVE_SEARCH_REGION_DEFAULT (1.5)
#define GST_VVAS_TRACKER_CORRELATION_THRESHOLD_DEFAULT  (0.7)
#define GST_VVAS_TRACKER_OVERLAP_THRESHOLD_DEFAULT      (0.0)
#define GST_VVAS_TRACKER_SCALE_CHANGE_THRESHOLD_DEFAULT (0.7)
#define GST_VVAS_TRACKER_CORRELATION_WEIGHT_DEFAULT      (0.7)
#define GST_VVAS_TRACKER_OVERLAP_WEIGHT_DEFAULT          (0.2)
#define GST_VVAS_TRACKER_SCALE_CHANGE_WEIGHT_DEFAULT     (0.1)
#define GST_VVAS_TRACKER_OCCLUSION_THRESHOLD_DEFAULT    (0.4)
#define GST_VVAS_TRACKER_CONFIDENCE_SCORE_THRESHOLD_DEFAULT (0.25)

static inline VVASVideoFormat
get_vvas_format (GstVideoFormat gst_fmt)
{
  switch (gst_fmt) {
    case GST_VIDEO_FORMAT_GRAY8:
      return VVAS_VFMT_Y8;
    case GST_VIDEO_FORMAT_NV12:
      return VVAS_VFMT_Y_UV8_420;
    case GST_VIDEO_FORMAT_BGR:
      return VVAS_VFMT_BGR8;
    case GST_VIDEO_FORMAT_RGB:
      return VVAS_VFMT_RGB8;
    case GST_VIDEO_FORMAT_YUY2:
      return VVAS_VFMT_YUYV8;
    case GST_VIDEO_FORMAT_r210:
      return VVAS_VFMT_RGBX10;
    case GST_VIDEO_FORMAT_v308:
      return VVAS_VFMT_YUV8;
    case GST_VIDEO_FORMAT_GRAY10_LE32:
      return VVAS_VFMT_Y10;
    case GST_VIDEO_FORMAT_ABGR:
      return VVAS_VFMT_ABGR8;
    case GST_VIDEO_FORMAT_ARGB:
      return VVAS_VFMT_ARGB8;
    default:
      GST_ERROR ("Not supporting %s yet", gst_video_format_to_string (gst_fmt));
      return VVAS_VMFT_UNKNOWN;
  }
}

static gboolean
input_each_node_to_tracker (GNode * node, gpointer new_objs_ptr)
{
  objs_data *ptr = (objs_data *) new_objs_ptr;
  GstInferencePrediction *prediction = (GstInferencePrediction *) node->data;

  if (ptr->num_objs >= MAX_OBJ_TRACK)
    return FALSE;
  else if (!node->parent)
    return FALSE;
  else {
    int i = ptr->num_objs;
    ptr->objs[i].x = prediction->bbox.x;
    ptr->objs[i].y = prediction->bbox.y;
    ptr->objs[i].width = prediction->bbox.width;
    ptr->objs[i].height = prediction->bbox.height;
    ptr->objs[i].map_id = prediction->prediction_id;
    ptr->num_objs = i + 1;
  }

  return FALSE;
}

static gboolean
update_each_node_with_results (GNode * node, gpointer kpriv_ptr)
{
  GstVvas_XTrackerPrivate *priv = (GstVvas_XTrackerPrivate *) kpriv_ptr;
  GList *classes;
  GstInferenceClassification *classification;
  GstInferencePrediction *prediction = (GstInferencePrediction *) node->data;
  bool flag = true;

  if (!node->parent)
    return FALSE;

  for (int i = 0; i < MAX_OBJ_TRACK && flag; i++) {
    if ((priv->trackers_data.trk_objs.objs[i].status == 1) &&
        (prediction->prediction_id ==
            priv->trackers_data.trk_objs.objs[i].map_id)) {
      prediction->bbox.x = round (priv->trackers_data.trk_objs.objs[i].x);
      prediction->bbox.y = round (priv->trackers_data.trk_objs.objs[i].y);
      prediction->bbox.width =
          round (priv->trackers_data.trk_objs.objs[i].width);
      prediction->bbox.height =
          round (priv->trackers_data.trk_objs.objs[i].height);
      flag = false;
      string str = to_string (priv->trackers_data.trk_objs.objs[i].trk_id);
      prediction->obj_track_label = g_strdup (str.c_str ());
    }
  }

  if (flag == true) {
    prediction->bbox.x = 0;
    prediction->bbox.y = 0;
    prediction->bbox.width = 0;
    prediction->bbox.height = 0;
    classes = prediction->classifications;
    classification = (GstInferenceClassification *) classes->data;
    g_free (classification->class_label);
    classification->class_label = NULL;
  }

  return FALSE;
}

static uint32_t
vvas_tracker_start (GstVvas_XTracker * self, VVASFrame * input[MAX_NUM_OBJECT])
{
  GstVvas_XTrackerPrivate *priv = self->priv;
  GstInferenceMeta *infer_meta = NULL;
  Mat_img img;
  VVASFrame *inframe = input[0];
  int data_size = 0, half_data_size = 0;
  GstMemory *in_mem = NULL;

  img.width = input[0]->props.stride;   //width;
  img.height = input[0]->props.height;
  img.channels = 1;

  if (priv->tconfig.tracker_type == ALGO_IOU) {
    GST_ERROR_OBJECT (self, "tracker start failed");
    return 0;
  }

  if (priv->tconfig.det_intvl <= 1 && priv->tconfig.tracker_type != ALGO_IOU) {
    GST_WARNING ("If detection interval is one tracker type must set ALGO_IOU");
  }

  if (priv->tconfig.tracker_type == ALGO_IOU && !priv->tconfig.iou_use_color) {
    img.data = NULL;
  } else {
    in_mem = gst_buffer_get_memory ((GstBuffer *) inframe->app_priv, 0);
    if (in_mem == NULL) {
      GST_ERROR_OBJECT (self, "failed to get memory from input buffer");
      return 0;
    }

    data_size = input[0]->props.stride * input[0]->props.height;
    half_data_size = data_size >> 1;
    if (gst_is_vvas_memory (in_mem)) {
      if (priv->img_data == NULL)
        priv->img_data = (char *) malloc (data_size + half_data_size);

      memcpy (priv->img_data, inframe->vaddr[0], data_size);

      if (!(priv->fr_count % priv->tconfig.det_intvl)) {
        memcpy (priv->img_data + data_size, inframe->vaddr[1], half_data_size);
        img.channels = 2;
      }

      img.data = (unsigned char *) priv->img_data;
    } else {
      if (((char *) inframe->vaddr[0] + data_size) !=
          ((char *) inframe->vaddr[1])) {
        GST_ERROR_OBJECT (self,
            "Tracker requires image data continuous memory");
        return 0;
      }
      img.data = (unsigned char *) inframe->vaddr[0];
    }
  }

  if (!(priv->fr_count % priv->tconfig.det_intvl)) {
    priv->trackers_data.new_objs.num_objs = 0;
    objs_data *ptr = &priv->trackers_data.new_objs;

    infer_meta = ((GstInferenceMeta *) gst_buffer_get_meta ((GstBuffer *)
            inframe->app_priv, gst_inference_meta_api_get_type ()));
    if (infer_meta != NULL) {
      if (priv->fr_count != 0)
        gst_inference_prediction_unref (priv->pr);

      priv->pr = gst_inference_prediction_copy (infer_meta->prediction);
      GST_DEBUG_OBJECT (self, "vvas_meta ptr %p", infer_meta);
      g_node_traverse (infer_meta->prediction->predictions, G_PRE_ORDER,
          G_TRAVERSE_LEAVES, -1, input_each_node_to_tracker, ptr);
    }

    run_tracker (img, priv->tconfig, &priv->trackers_data, true);
    priv->fr_count = 0;
  } else {
    infer_meta = ((GstInferenceMeta *) gst_buffer_get_meta ((GstBuffer *)
            inframe->app_priv, gst_inference_meta_api_get_type ()));
    if (infer_meta != NULL) {
      gst_inference_prediction_unref (infer_meta->prediction);
      infer_meta->prediction = gst_inference_prediction_copy (priv->pr);
    }

    run_tracker (img, priv->tconfig, &priv->trackers_data, false);
  }

  priv->fr_count++;

  infer_meta = ((GstInferenceMeta *) gst_buffer_get_meta ((GstBuffer *)
          inframe->app_priv, gst_inference_meta_api_get_type ()));
  if (infer_meta == NULL) {
    GST_WARNING ("vvas meta data is not available");
    return 1;
  } else {
    g_node_traverse (infer_meta->prediction->predictions, G_PRE_ORDER,
        G_TRAVERSE_LEAVES, -1, update_each_node_with_results, priv);
  }

  return 1;
}

static gboolean
vvas_xtracker_init (GstVvas_XTracker * self)
{
  GstVvas_XTrackerPrivate *priv = self->priv;
  int iret;

  priv->ids = 0;
  priv->fr_count = 0;
  priv->img_data = NULL;
  priv->trackers_data.ids = priv->ids;
  priv->tconfig.fixed_window = 1;
  priv->tconfig.hog_feature = 1;

  iret = init_tracker (&priv->trackers_data, &priv->tconfig);
  if (iret < 0) {
    GST_ERROR_OBJECT (self, "failed to do tracker init..");
    return FALSE;
  }

  GST_INFO_OBJECT (self, "completed tracker init");

  return TRUE;
}

static gboolean
vvas_xtracker_deinit (GstVvas_XTracker * self)
{
  GstVvas_XTrackerPrivate *priv = self->priv;
  int iret;

  if (priv->input[0])
    free (priv->input[0]);

  iret = deinit_tracker (&priv->trackers_data);
  if (iret < 0) {
    GST_ERROR_OBJECT (self, "failed to do tracker deinit..");
  }
  free (priv->img_data);
  priv->img_data = NULL;

  gst_inference_prediction_unref (priv->pr);

  GST_DEBUG_OBJECT (self, "successfully completed tracker deinit");

  return TRUE;
}

static gboolean
gst_vvas_xtracker_start (GstBaseTransform * trans)
{
  GstVvas_XTracker *self = GST_VVAS_XTRACKER (trans);
  GstVvas_XTrackerPrivate *priv = self->priv;

  self->priv = priv;
  priv->in_vinfo = gst_video_info_new ();
  priv->out_vinfo = gst_video_info_new ();

  gst_base_transform_set_in_place (trans, true);

  if (!vvas_xtracker_init (self))
    goto error;

  memset (priv->input, 0x0, sizeof (VVASFrame *) * MAX_NUM_OBJECT);
  priv->input[0] = (VVASFrame *) calloc (1, sizeof (VVASFrame));
  if (NULL == priv->input[0]) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    return FALSE;
  }

  return TRUE;

error:
  return FALSE;
}

static gboolean
gst_vvas_xtracker_stop (GstBaseTransform * trans)
{
  GstVvas_XTracker *self = GST_VVAS_XTRACKER (trans);
  GST_DEBUG_OBJECT (self, "stopping");
  gst_video_info_free (self->priv->out_vinfo);
  gst_video_info_free (self->priv->in_vinfo);
  vvas_xtracker_deinit (self);
  return TRUE;
}

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

  if (!gst_video_info_from_caps (priv->in_vinfo, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse input caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (priv->out_vinfo, outcaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse output caps");
    return FALSE;
  }

  return bret;
}

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
  gobject_class->finalize = gst_vvas_xtracker_finalize;

  transform_class->start = gst_vvas_xtracker_start;
  transform_class->stop = gst_vvas_xtracker_stop;
  transform_class->set_caps = gst_vvas_xtracker_set_caps;
  transform_class->transform_ip = gst_vvas_xtracker_transform_ip;

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
          "To speicfiy whether to use color feature with IOU or not", 0,
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

  /* Detection interval */
  g_object_class_install_property (gobject_class, PROP_DETECTION_INTERVAL,
      g_param_spec_uint ("detection-interval", "Object detection interval",
          "Object detection interval in number of frames",
          1, G_MAXUINT, GST_VVAS_TRACKER_DETECTION_INTERVAL_DEFAULT,
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
          "Object overlap threshold to consider for matching",
          0.001, 1.0, GST_VVAS_TRACKER_OVERLAP_THRESHOLD_DEFAULT,
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

/* initialize the new element
 * initialize instance structure
 */
static void
gst_vvas_xtracker_init (GstVvas_XTracker * self)
{
  GstVvas_XTrackerPrivate *priv = GST_VVAS_XTRACKER_PRIVATE (self);
  self->priv = priv;

  self->tracker_algo = GST_VVAS_TRACKER_TRACKER_ALGO_DEFAULT;
  self->search_scale = GST_VVAS_TRACKER_SEARCH_SCALE_DEFAULT;
  self->match_color = GST_VVAS_TRACKER_USE_MATCHING_COLOR_SPACE;

  if (self->tracker_algo == TRACKER_ALGO_IOU)
    priv->tconfig.tracker_type = ALGO_IOU;
  else if (self->tracker_algo == TRACKER_ALGO_MOSSE)
    priv->tconfig.tracker_type = ALGO_MOSSE;
  else if (self->tracker_algo == TRACKER_ALGO_KCF)
    priv->tconfig.tracker_type = ALGO_KCF;

  if (self->search_scale == SEARCH_SCALE_ALL)
    priv->tconfig.multiscale = 1;
  else if (self->search_scale == SEARCH_SCALE_UP)
    priv->tconfig.multiscale = 2;
  else if (self->search_scale == SEARCH_SCALE_DOWN)
    priv->tconfig.multiscale = 3;

  if (self->match_color == TRACKER_USE_RGB)
    priv->tconfig.obj_match_color = USE_RGB;
  else if (self->match_color == TRACKER_USE_HSV)
    priv->tconfig.obj_match_color = USE_HSV;

  priv->tconfig.iou_use_color = GST_VVAS_TRACKER_IOU_USE_COLOR_FEATURE;
  priv->tconfig.fet_length = GST_VVAS_TRACKER_FEATURE_LENGTH_DEFAULT;
  priv->tconfig.det_intvl = GST_VVAS_TRACKER_DETECTION_INTERVAL_DEFAULT;
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
}

static void
gst_vvas_xtracker_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_XTracker *self = GST_VVAS_XTRACKER (object);
  GstVvas_XTrackerPrivate *priv = self->priv;

  switch (prop_id) {
    case PROP_TRACKER_TYPE:
      self->tracker_algo = g_value_get_enum (value);
      if (self->tracker_algo == TRACKER_ALGO_IOU)
        priv->tconfig.tracker_type = ALGO_IOU;
      else if (self->tracker_algo == TRACKER_ALGO_MOSSE)
        priv->tconfig.tracker_type = ALGO_MOSSE;
      else if (self->tracker_algo == TRACKER_ALGO_KCF)
        priv->tconfig.tracker_type = ALGO_KCF;
      else
        GST_ERROR_OBJECT (self, "Invalid Tracker type %d set\n",
            self->tracker_algo);
      break;
    case PROP_IOU_USE_COLOR:
      priv->tconfig.iou_use_color = g_value_get_boolean (value);
      break;
    case PROP_USE_MATCHING_COLOR_SPACE:
      self->match_color = g_value_get_enum (value);
      if (self->match_color == TRACKER_USE_RGB)
        priv->tconfig.obj_match_color = USE_RGB;
      else if (self->match_color == TRACKER_USE_HSV)
        priv->tconfig.obj_match_color = USE_HSV;
      break;
    case PROP_FEATURE_LENGTH:
      priv->tconfig.fet_length = g_value_get_enum (value);
      break;
    case PROP_SEARCH_SCALE:
      self->search_scale = g_value_get_enum (value);
      if (self->search_scale == SEARCH_SCALE_ALL)
        priv->tconfig.multiscale = 1;
      else if (self->search_scale == SEARCH_SCALE_UP)
        priv->tconfig.multiscale = 2;
      else if (self->search_scale == SEARCH_SCALE_DOWN)
        priv->tconfig.multiscale = 3;
      else
        GST_ERROR_OBJECT (self, "Invalid Search scale %d set\n",
            self->search_scale);
      break;
    case PROP_DETECTION_INTERVAL:
      priv->tconfig.det_intvl = g_value_get_uint (value);
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

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
    case PROP_DETECTION_INTERVAL:
      g_value_set_uint (value, priv->tconfig.det_intvl);
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_xtracker_finalize (GObject * obj)
{
  GstVvas_XTracker *self = GST_VVAS_XTRACKER (obj);

  if (self->priv->input_pool)
    gst_object_unref (self->priv->input_pool);
}

static gboolean
vvas_xtracker_prepare_input_frame (GstVvas_XTracker * self, GstBuffer * inbuf)
{
  GstVvas_XTrackerPrivate *priv = self->priv;
  VVASFrame *vvas_frame = NULL;
  guint plane_id;
  GstVideoMeta *vmeta = NULL;
  GstMapFlags map_flags;
  gsize offset;
  gint8 *base_vaddr;

  vvas_frame = priv->input[0];

  /* Soft IP */
  map_flags = (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF
      | GST_MAP_WRITE);

  if (!gst_video_frame_map (&(priv->in_vframe), self->priv->in_vinfo,
          inbuf, map_flags)) {
    GST_ERROR_OBJECT (self, "failed to map input buffer");
    return FALSE;
  }

  vmeta = gst_buffer_get_video_meta (inbuf);
  if (vmeta) {
    vvas_frame->props.stride = vmeta->stride[0];
  } else {
    GST_DEBUG_OBJECT (self, "video meta not present in buffer");
    vvas_frame->props.stride =
        GST_VIDEO_INFO_PLANE_STRIDE (self->priv->in_vinfo, 0);
  }

  vvas_frame->props.width = GST_VIDEO_INFO_WIDTH (self->priv->in_vinfo);
  vvas_frame->props.height = GST_VIDEO_INFO_HEIGHT (self->priv->in_vinfo);
  vvas_frame->props.fmt =
      get_vvas_format (GST_VIDEO_INFO_FORMAT (self->priv->in_vinfo));
  vvas_frame->n_planes = GST_VIDEO_INFO_N_PLANES (self->priv->in_vinfo);
  vvas_frame->app_priv = inbuf;

  base_vaddr = (gint8*) GST_VIDEO_FRAME_PLANE_DATA (&(priv->in_vframe), 0);
  for (plane_id = 0;
      plane_id < GST_VIDEO_INFO_N_PLANES (self->priv->in_vinfo); plane_id++) {
    if (vmeta) {
      offset = vmeta->offset[plane_id];
    } else {
      offset = GST_VIDEO_INFO_PLANE_OFFSET (self->priv->in_vinfo, plane_id);
    }
    vvas_frame->vaddr[plane_id] = base_vaddr + offset;
    GST_LOG_OBJECT (self, "inbuf plane[%d] : offset = %lu, vaddr = %p",
        plane_id, offset, vvas_frame->vaddr[plane_id]);
  }

  GST_LOG_OBJECT (self, "successfully prepared input vvas frame");
  return TRUE;
}

static GstFlowReturn
gst_vvas_xtracker_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  GstVvas_XTracker *self = GST_VVAS_XTRACKER (base);
  int ret;
  gboolean bret = FALSE;

  bret = vvas_xtracker_prepare_input_frame (self, buf);
  if (!bret)
    goto error;

  ret = vvas_tracker_start (self, self->priv->input);
  if (!ret) {
    GST_ERROR_OBJECT (self, "tracker start failed");
    goto error;
  }

  if (self->priv->in_vframe.data[0]) {
    gst_video_frame_unmap (&(self->priv->in_vframe));
  }

  GST_LOG_OBJECT (self, "processed buffer %p", buf);

  return GST_FLOW_OK;

error:
  return GST_FLOW_ERROR;
}

static gboolean
plugin_init (GstPlugin * vvas_xtracker)
{
  return gst_element_register (vvas_xtracker, "vvas_xtracker", GST_RANK_NONE,
      GST_TYPE_VVAS_XTRACKER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vvas_xtracker,
    "GStreamer VVAS plug-in for tracker",
    plugin_init, "0.1", GST_LICENSE_UNKNOWN,
    "GStreamer Xilinx Tracker", "http://xilinx.com/")
