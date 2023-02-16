/*
 * Copyright (C) 2020 - 2022 Xilinx, Inc.  All rights reserved.
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

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <jansson.h>
#include <gst/vvas/gstinferencemeta.h>
#include <gst/vvas/gstvvasoverlaymeta.h>
#include <gst/vvas/gstvvascoreutils.h>
#include <vvas_core/vvas_common.h>
#include <vvas_core/vvas_metaconvert.h>
#include "gstvvas_xmetaconvert.h"

#define MAX_CLASS_LEN 1024
#define MAX_LABEL_LEN 1024
#define MAX_ALLOWED_CLASS 20
#define MAX_ALLOWED_LABELS 20

#define DEFAULT_FONT_SIZE 0.5
#define DEFAULT_FONT VVAS_FONT_HERSHEY_SIMPLEX
#define DEFAULT_THICKNESS 1
#define DEFAULT_RADIUS 3
#define DEFAULT_MASK_LEVEL 0

GST_DEBUG_CATEGORY_STATIC (gst_vvas_xmetaconvert_debug_category);
#define GST_CAT_DEFAULT gst_vvas_xmetaconvert_debug_category

/* prototypes */

static void gst_vvas_xmetaconvert_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_vvas_xmetaconvert_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_vvas_xmetaconvert_finalize (GObject * object);

static gboolean gst_vvas_xmetaconvert_start (GstBaseTransform * trans);
static gboolean gst_vvas_xmetaconvert_stop (GstBaseTransform * trans);
static gboolean gst_vvas_xmetaconvert_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_vvas_xmetaconvert_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);

enum
{
  PROP_0,
  PROP_CONFIG_LOCATION,
};


/* pad templates */

static GstStaticPadTemplate gst_vvas_xmetaconvert_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate gst_vvas_xmetaconvert_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

typedef struct
{
  unsigned int blue;
  unsigned int green;
  unsigned int red;
} color;

typedef struct
{
  color class_color;
  int masking_flag;
  char class_name[MAX_CLASS_LEN];
} vvass_xclassification;

struct overlayframe_info
{
  GstVideoInfo *in_vinfo;
  int y_offset;
};

struct parentlabel_info
{
  unsigned int x;
  unsigned int y;
  unsigned int prev_x;
  unsigned int prev_y;
  int y_offset;
  int text_height;
};

struct _GstVvas_XmetaconvertPrivate
{
  VvasContext *vvas_ctx;
  VvasMetaConvert *core_convert;
  VvasMetaConvertConfig cfg;
  GstVideoInfo *in_vinfo;
  struct overlayframe_info frameinfo;
};

/* class initialization */

#define gst_vvas_xmetaconvert_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_Xmetaconvert, gst_vvas_xmetaconvert,
    GST_TYPE_BASE_TRANSFORM);
#define GST_VVAS_XMETACONVERT_PRIVATE(self) (GstVvas_XmetaconvertPrivate *) (gst_vvas_xmetaconvert_get_instance_private (self))

/* GObject vmethod implementations */
static gboolean
gst_vvas_xmetaconvert_parse_config (GstVvas_Xmetaconvert * filter)
{
  GstVvas_XmetaconvertPrivate *priv = filter->priv;
  json_t *root = NULL, *config = NULL, *val = NULL, *karray = NULL;
  json_error_t error;
  uint32_t index;

  /* get root json object */
  root = json_load_file (filter->json_file, JSON_DECODE_ANY, &error);
  if (!root) {
    GST_ERROR_OBJECT (filter, "failed to load json file. reason %s",
        error.text);
    goto error;
  }

  config = json_object_get (root, "config");
  if (!json_is_object (config)) {
    GST_ERROR_OBJECT (filter, "config is not of object type");
    goto error;
  }

  val = json_object_get (config, "display-level");
  if (!val || !json_is_integer (val)) {
    priv->cfg.level = 0;
    GST_INFO_OBJECT (filter,
        "display_level is not set, so process all nodes at all levels");
  } else {
    priv->cfg.level = json_integer_value (val);
    if (priv->cfg.level < 0) {
      GST_ERROR_OBJECT (filter,
          "display level should be greater than or equal to 0");
      goto error;
    }
  }

  val = json_object_get (config, "font-size");
  if (!val || !json_number_value (val))
    priv->cfg.font_size = 0.5;
  else
    priv->cfg.font_size = json_number_value (val);

  val = json_object_get (config, "font");
  if (!val || !json_is_integer (val))
    priv->cfg.font_type = VVAS_FONT_HERSHEY_SIMPLEX;
  else {
    index = json_integer_value (val);
    if (index >= 0 && index <= 5)
      priv->cfg.font_type = index;
    else {
      GST_WARNING_OBJECT (filter, "font value out of range. Setting default\n");
      priv->cfg.font_type = VVAS_FONT_HERSHEY_SIMPLEX;
    }
  }

  val = json_object_get (config, "thickness");
  if (!val || !json_is_integer (val))
    priv->cfg.line_thickness = DEFAULT_THICKNESS;
  else
    priv->cfg.line_thickness = json_integer_value (val);

  val = json_object_get (config, "radius");
  if (!val || !json_is_integer (val))
    priv->cfg.radius = DEFAULT_RADIUS;
  else
    priv->cfg.radius = json_integer_value (val);


  val = json_object_get (config, "mask-level");
  if (!val || !json_is_integer (val))
    priv->cfg.mask_level = DEFAULT_MASK_LEVEL;
  else
    priv->cfg.mask_level = json_integer_value (val);

  val = json_object_get (config, "y-offset");
  if (!val || !json_is_integer (val))
    priv->cfg.y_offset = 0;
  else
    priv->cfg.y_offset = json_integer_value (val);

  val = json_object_get (config, "draw-above-bbox-flag");
  if (!val || !json_is_boolean (val))
    priv->cfg.draw_above_bbox_flag = true;
  else
    priv->cfg.draw_above_bbox_flag = json_boolean_value (val);

  karray = json_object_get (config, "label-filter");
  if (!json_is_array (karray)) {
    GST_INFO_OBJECT (filter, "label-filter not set, adding only class name");
    priv->cfg.allowed_labels_count = 1;
    priv->cfg.allowed_labels =
        (char **) calloc (priv->cfg.allowed_labels_count, sizeof (char *));
    priv->cfg.allowed_labels[0] = g_strdup ("class");
  } else {
    priv->cfg.allowed_labels_count = json_array_size (karray);
    priv->cfg.allowed_labels =
        (char **) calloc (priv->cfg.allowed_labels_count, sizeof (char *));
    for (index = 0; index < json_array_size (karray); index++) {
      priv->cfg.allowed_labels[index] =
          g_strdup (json_string_value (json_array_get (karray, index)));
    }
  }

  /* get classes array */
  karray = json_object_get (config, "classes");
  if (!karray) {
    GST_INFO_OBJECT (filter,
        "classification filtering not found, allowing all classes");
    priv->cfg.allowed_classes_count = 0;
  } else {
    if (!json_is_array (karray)) {
      GST_ERROR_OBJECT (filter, "classes key is not of array type");
      goto error;
    }
    priv->cfg.allowed_classes_count = json_array_size (karray);
    priv->cfg.allowed_classes =
        (VvasFilterObjectInfo **) calloc (priv->cfg.allowed_classes_count,
        sizeof (VvasFilterObjectInfo *));

    for (index = 0; index < priv->cfg.allowed_classes_count; index++) {
      VvasFilterObjectInfo *allowed_class =
          (VvasFilterObjectInfo *) calloc (1, sizeof (VvasFilterObjectInfo));
      json_t *classes;

      classes = json_array_get (karray, index);
      if (!classes) {
        GST_ERROR_OBJECT (filter, "failed to get class object");
        goto error;
      }

      val = json_object_get (classes, "name");
      if (!json_is_string (val)) {
        GST_ERROR_OBJECT (filter, "name is not found for array %d", index);
        goto error;
      } else {
        strncpy (allowed_class->name,
            (char *) json_string_value (val), META_CONVERT_MAX_STR_LENGTH - 1);
        allowed_class->name[META_CONVERT_MAX_STR_LENGTH - 1] = '\0';
        GST_DEBUG_OBJECT (filter, "name %s", allowed_class->name);
      }

      val = json_object_get (classes, "green");
      if (!val || !json_is_integer (val))
        allowed_class->color.green = 0;
      else
        allowed_class->color.green = json_integer_value (val);

      val = json_object_get (classes, "blue");
      if (!val || !json_is_integer (val))
        allowed_class->color.blue = 0;
      else
        allowed_class->color.blue = json_integer_value (val);

      val = json_object_get (classes, "red");
      if (!val || !json_is_integer (val))
        allowed_class->color.red = 0;
      else
        allowed_class->color.red = json_integer_value (val);

      val = json_object_get (classes, "masking");
      if (!val || !json_is_integer (val))
        allowed_class->do_mask = 0;
      else
        allowed_class->do_mask = json_integer_value (val);

      priv->cfg.allowed_classes[index] = allowed_class;
    }
  }

  if (root)
    json_decref (root);

  return TRUE;

error:
  if (root)
    json_decref (root);

  return FALSE;
}

static void
gst_vvas_xmetaconvert_class_init (GstVvas_XmetaconvertClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_vvas_xmetaconvert_src_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_vvas_xmetaconvert_sink_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "VVAS Plugin to convert ML infer meta to overlay meta",
      "Generic",
      "VVAS Plugin to convert ML infer meta to overlay meta",
      "Xilinx Inc <www.xilinx.com>");

  GST_DEBUG_CATEGORY_INIT (gst_vvas_xmetaconvert_debug_category,
      "vvas_xmetaconvert", 0, "debug category for vvasxmetaconvert element");

  gobject_class->set_property = gst_vvas_xmetaconvert_set_property;
  gobject_class->get_property = gst_vvas_xmetaconvert_get_property;
  gobject_class->finalize = gst_vvas_xmetaconvert_finalize;

  g_object_class_install_property (gobject_class, PROP_CONFIG_LOCATION,
      g_param_spec_string ("config-location",
          "JSON config file location",
          "Location of the config file in json format", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              GST_PARAM_MUTABLE_READY)));

  base_transform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_vvas_xmetaconvert_set_caps);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_vvas_xmetaconvert_transform_ip);

  base_transform_class->start = gst_vvas_xmetaconvert_start;
  base_transform_class->stop = gst_vvas_xmetaconvert_stop;
}

static void
gst_vvas_xmetaconvert_init (GstVvas_Xmetaconvert * vvasxmetaconvert)
{
  vvasxmetaconvert->priv = GST_VVAS_XMETACONVERT_PRIVATE (vvasxmetaconvert);

  vvasxmetaconvert->json_file = NULL;
  vvasxmetaconvert->priv->in_vinfo = gst_video_info_new ();

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (vvasxmetaconvert), TRUE);
}

void
gst_vvas_xmetaconvert_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvas_Xmetaconvert *vvasxmetaconvert = GST_VVAS_XMETACONVERT (object);

  GST_DEBUG_OBJECT (vvasxmetaconvert, "set_property");

  switch (property_id) {
    case PROP_CONFIG_LOCATION:
      if (vvasxmetaconvert->json_file)
        g_free (vvasxmetaconvert->json_file);

      vvasxmetaconvert->json_file = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_vvas_xmetaconvert_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVvas_Xmetaconvert *vvasxmetaconvert = GST_VVAS_XMETACONVERT (object);

  GST_DEBUG_OBJECT (vvasxmetaconvert, "get_property");

  switch (property_id) {
    case PROP_CONFIG_LOCATION:
      g_value_set_string (value, vvasxmetaconvert->json_file);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_vvas_xmetaconvert_finalize (GObject * object)
{
  GstVvas_Xmetaconvert *vvasxmetaconvert = GST_VVAS_XMETACONVERT (object);

  GST_DEBUG_OBJECT (vvasxmetaconvert, "finalize");

  if (vvasxmetaconvert->priv->in_vinfo) {
    gst_video_info_free (vvasxmetaconvert->priv->in_vinfo);
    vvasxmetaconvert->priv->in_vinfo = NULL;
  }

  if (vvasxmetaconvert->json_file) {
    g_free (vvasxmetaconvert->json_file);
    vvasxmetaconvert->json_file = NULL;
  }
  /* clean up object here */
  G_OBJECT_CLASS (gst_vvas_xmetaconvert_parent_class)->finalize (object);
}

static gboolean
gst_vvas_xmetaconvert_start (GstBaseTransform * trans)
{
  GstVvas_Xmetaconvert *vvasxmetaconvert = GST_VVAS_XMETACONVERT (trans);
  GstVvas_XmetaconvertPrivate *priv = vvasxmetaconvert->priv;
  VvasLogLevel core_log_level =
      vvas_get_core_log_level (gst_debug_category_get_threshold
      (gst_vvas_xmetaconvert_debug_category));

  if (!gst_vvas_xmetaconvert_parse_config (vvasxmetaconvert)) {
    GST_ERROR_OBJECT (vvasxmetaconvert, "failed to parse configuration file");
    return FALSE;
  }

  priv->vvas_ctx = vvas_context_create (-1, NULL, core_log_level, NULL);
  if (!priv->vvas_ctx) {
    GST_ERROR_OBJECT (vvasxmetaconvert, "failed to create vvas context");
    return FALSE;
  }
  priv->core_convert = vvas_metaconvert_create (priv->vvas_ctx, &priv->cfg,
      core_log_level, NULL);
  if (!priv->core_convert) {
    GST_ERROR_OBJECT (vvasxmetaconvert,
        "failed to create core metaconvert context ");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vvas_xmetaconvert_stop (GstBaseTransform * trans)
{
  GstVvas_Xmetaconvert *vvasxmetaconvert = GST_VVAS_XMETACONVERT (trans);
  GstVvas_XmetaconvertPrivate *priv = vvasxmetaconvert->priv;
  guint idx;

  if (priv->cfg.allowed_labels_count) {
    for (idx = 0; idx < priv->cfg.allowed_labels_count; idx++)
      free (priv->cfg.allowed_labels[idx]);
    free (priv->cfg.allowed_labels);
  }

  if (priv->cfg.allowed_classes_count) {
    for (idx = 0; idx < priv->cfg.allowed_classes_count; idx++)
      free (priv->cfg.allowed_classes[idx]);
    free (priv->cfg.allowed_classes);
  }

  if (priv->core_convert)
    vvas_metaconvert_destroy (priv->core_convert);
  if (priv->vvas_ctx)
    vvas_context_destroy (priv->vvas_ctx);

  return TRUE;
}

static gboolean
gst_vvas_xmetaconvert_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVvas_Xmetaconvert *vvasxmetaconvert = GST_VVAS_XMETACONVERT (trans);
  GstVvas_XmetaconvertPrivate *priv = vvasxmetaconvert->priv;

  GST_DEBUG_OBJECT (vvasxmetaconvert, "set_caps");

  if (!gst_video_info_from_caps (priv->in_vinfo, incaps)) {
    GST_ERROR_OBJECT (vvasxmetaconvert, "Failed to parse input caps");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_vvas_xmetaconvert_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstVvas_Xmetaconvert *vvasxmetaconvert = GST_VVAS_XMETACONVERT (trans);
  GstVvas_XmetaconvertPrivate *priv = vvasxmetaconvert->priv;
  GstVvasOverlayMeta *out_meta;
  struct overlayframe_info *frameinfo = &(priv->frameinfo);
  GstInferenceMeta *infer_meta = NULL;
  char *pstr;
  VvasReturnType vret = VVAS_RET_SUCCESS;
  VvasInferPrediction *core_pred = NULL;

  GST_DEBUG_OBJECT (vvasxmetaconvert, "transform_ip");

  frameinfo->y_offset = 0;

  infer_meta = ((GstInferenceMeta *) gst_buffer_get_meta (buf,
          gst_inference_meta_api_get_type ()));
  if (infer_meta == NULL) {
    GST_WARNING_OBJECT (vvasxmetaconvert,
        "ML inference meta data is not available");
    return GST_FLOW_OK;
  } else {
    GST_DEBUG_OBJECT (vvasxmetaconvert, "vvas_mata ptr %p", infer_meta);
  }

  /* Print the entire prediction tree */
  pstr = gst_inference_prediction_to_string (infer_meta->prediction);
  GST_DEBUG_OBJECT (vvasxmetaconvert, "Prediction tree: \n%s", pstr);
  free (pstr);

  out_meta = gst_buffer_add_vvas_overlay_meta (buf);

  core_pred = vvas_infer_from_gstinfer (infer_meta->prediction);

  /* Convert GstInferencePrediction to VvasInferPrediction */
  vret = vvas_metaconvert_prepare_overlay_metadata (priv->core_convert,
      core_pred->node, &out_meta->shape_info);
  if (VVAS_IS_ERROR (vret)) {
    GST_DEBUG_OBJECT (vvasxmetaconvert, "failed to convert metadata");
    GST_ELEMENT_ERROR (vvasxmetaconvert, LIBRARY, FAILED,
        ("failed to convert metadata"), NULL);
    vvas_inferprediction_free (core_pred);
    return GST_FLOW_ERROR;
  }
  // TODO: Crashing here, might be some shared pointers between gst and core prediction
  vvas_inferprediction_free (core_pred);

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * vvas_xmetaconvert)
{
  return gst_element_register (vvas_xmetaconvert, "vvas_xmetaconvert",
      GST_RANK_PRIMARY, GST_TYPE_VVAS_XMETACONVERT);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xmetaconvert,
    "GStreamer VVAS plug-in for filters", plugin_init, VVAS_API_VERSION,
    "MIT/X11", "Xilinx VVAS SDK plugin", "http://xilinx.com/")
