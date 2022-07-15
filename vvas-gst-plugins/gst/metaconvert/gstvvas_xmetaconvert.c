/*
 * Copyright (C) 2020 - 2022 Xilinx, Inc.  All rights reserved.
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
#include "gstvvas_xmetaconvert.h"

#define MAX_CLASS_LEN 1024
#define MAX_LABEL_LEN 1024
#define MAX_ALLOWED_CLASS 20
#define MAX_ALLOWED_LABELS 20

#define DEFAULT_FONT_SIZE 0.5
#define DEFAULT_FONT VVAS_FONT_HERSHEY_SIMPLEX
#define DEFAULT_THICKNESS 1
#define DEFAULT_MASK_LEVEL 0

GST_DEBUG_CATEGORY_STATIC (gst_vvas_xmetaconvert_debug_category);
#define GST_CAT_DEFAULT gst_vvas_xmetaconvert_debug_category

/* prototypes */


static void gst_vvas_xmetaconvert_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_vvas_xmetaconvert_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_vvas_xmetaconvert_finalize (GObject * object);

static gboolean gst_vvas_xmetaconvert_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_vvas_xmetaconvert_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);

enum
{
  PROP_0,
  PROP_CONFIG_LOCATION,
  PROP_FONT_SIZE,
  PROP_FONT,
  PROP_THICKNESS
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

typedef enum
{
  VVAS_FONT_HERSHEY_SIMPLEX,
  VVAS_FONT_HERSHEY_PLAIN,
  VVAS_FONT_HERSHEY_DUPLEX,
  VVAS_FONT_HERSHEY_COMPLEX,
  VVAS_FONT_HERSHEY_TRIPLEX,
  VVAS_FONT_HERSHEY_COMPLEX_SMALL,
  VVAS_FONT_HERSHEY_SCRIPT_SIMPLEX,
  VVAS_FONT_HERSHEY_SCRIPT_COMPLEX
} GstVvas_XmetaconvertFont;


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
  float font_size;
  unsigned int font;
  int line_thickness;
  int mask_level;
  int y_offset;
  int display_level;
  color label_color;
  GstVideoInfo *in_vinfo;
  char label_filter[MAX_ALLOWED_LABELS][MAX_LABEL_LEN];
  unsigned char label_filter_cnt;
  unsigned short classes_count;
  vvass_xclassification class_list[MAX_ALLOWED_CLASS];
  struct overlayframe_info frameinfo;
  struct parentlabel_info plabel;
  GstVvasOverlayMeta *out_meta;
  unsigned int rect_index;
  unsigned int text_index;
};

/* class initialization */

#define gst_vvas_xmetaconvert_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstVvas_Xmetaconvert, gst_vvas_xmetaconvert,
    GST_TYPE_BASE_TRANSFORM);
#define GST_VVAS_XMETACONVERT_PRIVATE(self) (GstVvas_XmetaconvertPrivate *) (gst_vvas_xmetaconvert_get_instance_private (self))


#define GST_TYPE_FONT (gst_vvas_xmetaconvert_font_get_type ())
static GType
gst_vvas_xmetaconvert_font_get_type (void)
{
  static GType vvas_xmetaconvert_font_type = 0;
  static const GEnumValue font_types[] = {
    {VVAS_FONT_HERSHEY_SIMPLEX, "Hershey Simplex", "Hershey Simplex"},
    {VVAS_FONT_HERSHEY_PLAIN, "Hershey Plain", "Hershey Plain"},
    {VVAS_FONT_HERSHEY_DUPLEX, "Hershey Duplex", "Hershey Duplex"},
    {VVAS_FONT_HERSHEY_COMPLEX, "Hershey Complex", "Hershey Complex"},
    {VVAS_FONT_HERSHEY_TRIPLEX, "Hershey Triplex", "Hershey Triplex"},
    {VVAS_FONT_HERSHEY_COMPLEX_SMALL, "Hershey Complex Small",
        "Hershey Complex Small"},
    {VVAS_FONT_HERSHEY_SCRIPT_SIMPLEX, "Hershey Script Simplex",
        "Hershey Script Simplex"},
    {VVAS_FONT_HERSHEY_SCRIPT_COMPLEX, "Hershey Script Complex",
        "Hershey Script Complex"},
    {0, NULL, NULL}
  };

  if (!vvas_xmetaconvert_font_type) {
    vvas_xmetaconvert_font_type =
        g_enum_register_static ("GstVvas_XmetaconvertFont", font_types);
  }
  return vvas_xmetaconvert_font_type;
}

static gboolean
get_label_text (GstInferenceClassification * c, GstInferencePrediction * p,
    GstVvas_XmetaconvertPrivate * priv, char *label_string)
{
  unsigned char idx = 0, buffIdx = 0;
  if ((!c->class_label || !strlen ((char *) c->class_label)) &&
      (!p->obj_track_label || !strlen ((char *) p->obj_track_label)))
    return FALSE;

  for (idx = 0; idx < priv->label_filter_cnt; idx++) {
    if (!strcmp (priv->label_filter[idx], "class")) {
      if (c->class_label) {
        snprintf (label_string + buffIdx, (MAX_LABEL_LEN - buffIdx), "%s",
            (char *) c->class_label);
        buffIdx = strlen (label_string);
      }
    } else if (!strcmp (priv->label_filter[idx], "tracker-id")) {
      if (p->obj_track_label) {
        if (strlen (label_string)) {
          snprintf (label_string + buffIdx, (MAX_LABEL_LEN - buffIdx), " : ");
          buffIdx = strlen (label_string);
        }
        snprintf (label_string + buffIdx, (MAX_LABEL_LEN - buffIdx), "%s",
            (char *) p->obj_track_label);
        buffIdx = strlen (label_string);
      }
    } else if (!strcmp (priv->label_filter[idx], "probability")) {
      if (strlen (label_string)) {
        snprintf (label_string + buffIdx, (MAX_LABEL_LEN - buffIdx), " : ");
        buffIdx = strlen (label_string);
      }
      snprintf (label_string + buffIdx, (MAX_LABEL_LEN - buffIdx), "%.2f ",
          c->class_prob);
      buffIdx = strlen (label_string);
    }
  }
  return TRUE;
}

/* Check if the given classification is to be filtered */
static int
vvas_classification_is_allowed (char *cls_name,
    GstVvas_XmetaconvertPrivate * priv)
{
  unsigned int idx;

  if (cls_name == NULL)
    return -1;

  for (idx = 0;
      idx < sizeof (priv->class_list) / sizeof (priv->class_list[0]); idx++) {
    if (!strcmp (cls_name, priv->class_list[idx].class_name)) {
      return idx;
    }
  }
  return -1;
}

static gboolean
overlay_node_foreach (GNode * node, gpointer priv_ptr)
{
  GstVvas_XmetaconvertPrivate *priv = (GstVvas_XmetaconvertPrivate *) priv_ptr;
  struct overlayframe_info *frameinfo = &(priv->frameinfo);
  struct parentlabel_info *plabel = &(priv->plabel);
  int level = g_node_depth (node) - 1, idx = 0;
  gboolean draw = FALSE;
  GList *classes;
  GstInferenceClassification *classification = NULL;
  GstInferencePrediction *prediction = (GstInferencePrediction *) node->data;
  char label_string[MAX_LABEL_LEN];
  gboolean label_present;
  color clr;
  int masking_flag = 0;

  if (!priv->display_level || (level == priv->display_level))
    draw = TRUE;

  /* On each children, iterate through the different associated classes */
  for (classes = prediction->classifications;
      classes; classes = g_list_next (classes)) {
    /* Check to not to traverse through the nodes that exceeds overlay meta data limit */
    if (priv->out_meta->num_text >= VVAS_MAX_OVERLAY_DATA
        || priv->out_meta->num_rects >= VVAS_MAX_OVERLAY_DATA) {
      GST_INFO_OBJECT (node,
          "Number of predictions are more than the maximum limit (%d) of overlay meta. So not converting the excess meta to not to exceed maximum overlay meta limit %d",
          VVAS_MAX_OVERLAY_DATA, VVAS_MAX_OVERLAY_DATA);
      return TRUE;
    }
    classification = (GstInferenceClassification *) classes->data;
    masking_flag = 0;

    idx = vvas_classification_is_allowed ((char *)
        classification->class_label, priv);

    if (idx == -1 && classification->class_label == NULL
        && prediction->obj_track_label != NULL)
      idx = 0;

    if (priv->classes_count && idx == -1)
      continue;

    if (priv->classes_count) {
      clr.blue = priv->class_list[idx].class_color.blue;
      clr.green = priv->class_list[idx].class_color.green;
      clr.red = priv->class_list[idx].class_color.red;
      masking_flag = priv->class_list[idx].masking_flag;
    } else {
      /* If there are no classes specified, assign based on level */
      if (level == 1) {
        clr.blue = 255;         /* blue */
        clr.green = 0;
        clr.red = 0;
      } else if (level == 2) {
        clr.blue = 0;
        clr.green = 255;        /* green */
        clr.red = 0;
      } else if (level == 3) {
        clr.blue = 0;
        clr.green = 0;
        clr.red = 255;          /* red */
      } else {
        clr.blue = 255;
        clr.green = 255;
        clr.red = 0;            /* aqua */
      }
    }

    memset (label_string, 0, MAX_LABEL_LEN);
    label_present =
        get_label_text (classification, prediction, priv, label_string);

    if (label_present) {
      /* Get y offset to use in case of classification model */
      frameinfo->y_offset = 0;
      if ((prediction->bbox.height < 1) && (prediction->bbox.width < 1)) {
        if (priv->y_offset) {
          frameinfo->y_offset = priv->y_offset;
        } else {
          frameinfo->y_offset = (GST_VIDEO_INFO_HEIGHT (priv->in_vinfo) * 0.10);
        }
      }
    }

    if (!(!prediction->bbox.x && !prediction->bbox.y) && draw) {

      /* last parent coordinates */
      plabel->x = prediction->bbox.x;
      plabel->y = prediction->bbox.y;

      priv->out_meta->num_rects++;
      /* Update the meta data with rectangle attributes */
      priv->out_meta->rects[priv->rect_index].offset.x = prediction->bbox.x;
      priv->out_meta->rects[priv->rect_index].offset.y = prediction->bbox.y;
      priv->out_meta->rects[priv->rect_index].width = prediction->bbox.width;
      priv->out_meta->rects[priv->rect_index].height = prediction->bbox.height;
      priv->out_meta->rects[priv->rect_index].thickness = priv->line_thickness;
      priv->out_meta->rects[priv->rect_index].rect_color.red = clr.red;
      priv->out_meta->rects[priv->rect_index].rect_color.green = clr.green;
      priv->out_meta->rects[priv->rect_index].rect_color.blue = clr.blue;
      priv->out_meta->rects[priv->rect_index].apply_bg_color = 0;

      if (masking_flag || ((priv->mask_level) && (level == priv->mask_level))) {
        priv->out_meta->rects[priv->rect_index].apply_bg_color = 1;
        priv->out_meta->rects[priv->rect_index].bg_color.red = 0;
        priv->out_meta->rects[priv->rect_index].bg_color.green = 0;
        priv->out_meta->rects[priv->rect_index].bg_color.blue = 0;
      }

      priv->rect_index++;
    }

    if (label_present) {
      /* Update the label string information */
      priv->out_meta->num_text++;
      priv->out_meta->text[priv->text_index].bottom_left_origin = 1;
      priv->out_meta->text[priv->text_index].offset.x = prediction->bbox.x;
      priv->out_meta->text[priv->text_index].offset.y = prediction->bbox.y +
          frameinfo->y_offset;
      priv->out_meta->text[priv->text_index].text_font.font_size =
          priv->font_size;
      priv->out_meta->text[priv->text_index].text_font.font_num = priv->font;
      strcpy (priv->out_meta->text[priv->text_index].disp_text, label_string);
      priv->out_meta->text[priv->text_index].apply_bg_color = 1;
      priv->out_meta->text[priv->text_index].bg_color.blue = clr.blue;
      priv->out_meta->text[priv->text_index].bg_color.green = clr.green;
      priv->out_meta->text[priv->text_index].bg_color.red = clr.red;
      priv->text_index++;
    }
  }

  return FALSE;
}


/* GObject vmethod implementations */
static gboolean
gst_vvas_xmetaconvert_parse_config (GstVvas_Xmetaconvert * filter)
{
  GstVvas_XmetaconvertPrivate *priv = filter->priv;
  json_t *root = NULL, *config = NULL, *val = NULL;
  json_error_t error;
  json_t *karray = NULL, *classes = NULL;
  unsigned int index = 0;

  /* Initialize config params with default values */
  priv->font_size = 0.5;
  priv->font = VVAS_FONT_HERSHEY_SIMPLEX;
  priv->line_thickness = DEFAULT_THICKNESS;
  priv->y_offset = 0;
  strcpy (priv->label_filter[0], "class");
  strcpy (priv->label_filter[1], "tracker-id");
  strcpy (priv->label_filter[2], "probability");
  priv->label_filter_cnt = 3;
  priv->classes_count = 0;

  /* get root json object */
  root = json_load_file (filter->json_file, JSON_DECODE_ANY, &error);
  if (!root) {
    GST_ERROR_OBJECT (filter, "failed to load json file. reason %s",
        error.text);
    goto error;
  }

  val = json_object_get (root, "config");
  if (!json_is_object (val)) {
    GST_ERROR_OBJECT (filter, "config is not of object type");
    goto error;
  }

  config = json_deep_copy (val);

  val = json_object_get (config, "display_level");
  if (!val || !json_is_integer (val)) {
    priv->display_level = 0;
  } else {
    priv->display_level = json_integer_value (val);
    if (priv->display_level < 0) {
      priv->display_level = 0;
      GST_WARNING_OBJECT (filter,
          "display level should be greater than or equal to 0");
    }
  }

  val = json_object_get (config, "font_size");
  if (!val || !json_is_integer (val))
    priv->font_size = 0.5;
  else
    priv->font_size = json_integer_value (val);

  val = json_object_get (config, "font");
  if (!val || !json_is_integer (val))
    priv->font = VVAS_FONT_HERSHEY_SIMPLEX;
  else
    priv->font = json_integer_value (val);

  val = json_object_get (config, "thickness");
  if (!val || !json_is_integer (val))
    priv->line_thickness = DEFAULT_THICKNESS;
  else
    priv->line_thickness = json_integer_value (val);

  val = json_object_get (config, "mask_level");
  if (!val || !json_is_integer (val))
    priv->mask_level = DEFAULT_MASK_LEVEL;
  else
    priv->mask_level = json_integer_value (val);

  val = json_object_get (config, "y_offset");
  if (!val || !json_is_integer (val))
    priv->y_offset = 0;
  else
    priv->y_offset = json_integer_value (val);

  /* get label color array */
  karray = json_object_get (config, "label_color");
  if (!karray) {
    GST_INFO_OBJECT (filter, "label_color not set, going with default, black");
    priv->label_color.blue = 0;
    priv->label_color.green = 0;
    priv->label_color.red = 0;
  } else {
    priv->label_color.blue =
        json_integer_value (json_object_get (karray, "blue"));
    priv->label_color.green =
        json_integer_value (json_object_get (karray, "green"));
    priv->label_color.red =
        json_integer_value (json_object_get (karray, "red"));
  }

  karray = json_object_get (config, "label_filter");

  if (!json_is_array (karray)) {
    GST_INFO_OBJECT (filter, "label_filter not set, adding only class name\n");
    strcpy (priv->label_filter[0], "class");
  } else {
    priv->label_filter_cnt = 0;
    for (index = 0; index < json_array_size (karray); index++) {
      strcpy (priv->label_filter[index],
          json_string_value (json_array_get (karray, index)));
      priv->label_filter_cnt++;
    }
  }

  /* get classes array */
  karray = json_object_get (config, "classes");
  if (!karray) {
    GST_INFO_OBJECT (filter,
        "classification filtering not found, allowing all classes");
    priv->classes_count = 0;
  } else {
    if (!json_is_array (karray)) {
      GST_ERROR_OBJECT (filter, "labels key is not of array type");
      goto error;
    }
    priv->classes_count = json_array_size (karray);
    for (index = 0; index < priv->classes_count; index++) {
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
        strncpy (priv->class_list[index].class_name,
            (char *) json_string_value (val), MAX_CLASS_LEN - 1);
        GST_DEBUG_OBJECT (filter, "name %s",
            priv->class_list[index].class_name);
      }

      val = json_object_get (classes, "green");
      if (!val || !json_is_integer (val))
        priv->class_list[index].class_color.green = 0;
      else
        priv->class_list[index].class_color.green = json_integer_value (val);

      val = json_object_get (classes, "blue");
      if (!val || !json_is_integer (val))
        priv->class_list[index].class_color.blue = 0;
      else
        priv->class_list[index].class_color.blue = json_integer_value (val);

      val = json_object_get (classes, "red");
      if (!val || !json_is_integer (val))
        priv->class_list[index].class_color.red = 0;
      else
        priv->class_list[index].class_color.red = json_integer_value (val);

      val = json_object_get (classes, "masking");
      if (!val || !json_is_integer (val))
        priv->class_list[index].masking_flag = 0;
      else
        priv->class_list[index].masking_flag = json_integer_value (val);
    }
  }

  if (root)
    json_decref (root);

  if (config)
    json_decref(config);

  return TRUE;
error:
  if (root)
    json_decref(root);

  if (config)
    json_decref(config);

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

  g_object_class_install_property (gobject_class, PROP_FONT_SIZE,
      g_param_spec_float ("font-size",
          "Font size of labels to be used",
          "Font size of labels to be used", 0.5, 1.0, DEFAULT_FONT_SIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_FONT,
      g_param_spec_enum ("font", "Font",
          "Types of fonts available.", GST_TYPE_FONT,
          DEFAULT_FONT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_THICKNESS,
      g_param_spec_int ("thickness",
          "Thickness of lines",
          "Thickness of the line that makes up the rectangle",
          1, 3, DEFAULT_THICKNESS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  base_transform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_vvas_xmetaconvert_set_caps);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_vvas_xmetaconvert_transform_ip);

}

static void
gst_vvas_xmetaconvert_init (GstVvas_Xmetaconvert * vvasxmetaconvert)
{
  vvasxmetaconvert->priv = GST_VVAS_XMETACONVERT_PRIVATE (vvasxmetaconvert);

  vvasxmetaconvert->json_file = NULL;
  vvasxmetaconvert->priv->rect_index = 0;
  vvasxmetaconvert->priv->text_index = 0;
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
      gst_vvas_xmetaconvert_parse_config (vvasxmetaconvert);
      break;
    case PROP_FONT_SIZE:
      vvasxmetaconvert->priv->font_size = g_value_get_float (value);
      break;
    case PROP_FONT:
      vvasxmetaconvert->priv->font = g_value_get_enum (value);
      break;
    case PROP_THICKNESS:
      vvasxmetaconvert->priv->line_thickness = g_value_get_int (value);
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
    case PROP_FONT_SIZE:
      g_value_set_float (value, vvasxmetaconvert->priv->font_size);
      break;
    case PROP_FONT:
      g_value_set_enum (value, vvasxmetaconvert->priv->font);
      break;
    case PROP_THICKNESS:
      g_value_set_int (value, vvasxmetaconvert->priv->line_thickness);
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
  struct overlayframe_info *frameinfo = &(priv->frameinfo);
  struct parentlabel_info *plabel = &(priv->plabel);
  GstInferenceMeta *infer_meta = NULL;
  char *pstr;

  GST_DEBUG_OBJECT (vvasxmetaconvert, "transform_ip");

  plabel->x = plabel->y = plabel->text_height = 0;
  plabel->prev_x = plabel->prev_y = 0;
  frameinfo->y_offset = 0;
  priv->rect_index = 0;
  priv->text_index = 0;

  infer_meta = ((GstInferenceMeta *) gst_buffer_get_meta (buf,
          gst_inference_meta_api_get_type ()));
  if (infer_meta == NULL) {
    GST_WARNING_OBJECT (vvasxmetaconvert,
        "ML inference meta data is not available");
    return GST_FLOW_OK;
  } else {
    GST_DEBUG_OBJECT (vvasxmetaconvert, "vvas_mata ptr %p", infer_meta);
  }

  priv->out_meta = gst_buffer_add_vvas_overlay_meta (buf);

  /* Print the entire prediction tree */
  pstr = gst_inference_prediction_to_string (infer_meta->prediction);
  GST_DEBUG_OBJECT (vvasxmetaconvert, "Prediction tree: \n%s", pstr);
  free (pstr);

  g_node_traverse (infer_meta->prediction->predictions, G_PRE_ORDER,
      G_TRAVERSE_ALL, -1, overlay_node_foreach, priv);

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * vvas_xmetaconvert)
{
  return gst_element_register (vvas_xmetaconvert, "vvas_xmetaconvert",
      GST_RANK_NONE, GST_TYPE_VVAS_XMETACONVERT);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, vvas_xmetaconvert,
    "GStreamer VVAS plug-in for filters", plugin_init, VVAS_API_VERSION,
    "MIT/X11", "Xilinx VVAS SDK plugin", "http://xilinx.com/")
