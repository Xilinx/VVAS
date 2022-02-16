/*
 * Copyright 2020 Xilinx, Inc.
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

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <math.h>
#include <vvas/vvas_kernel.h>
#include <gst/vvas/gstinferencemeta.h>
#include <gst/vvas/gstvvasallocator.h>
#include "vvas_xboundingbox.hpp"

int log_level = LOG_LEVEL_WARNING;

using namespace cv;
using namespace std;

#define MAX_CLASS_LEN 1024
#define MAX_LABEL_LEN 1024
#define MAX_ALLOWED_CLASS 20
#define MAX_ALLOWED_LABELS 20

struct color
{
  unsigned int blue;
  unsigned int green;
  unsigned int red;
};

struct vvass_xclassification
{
  color class_color;
  char class_name[MAX_CLASS_LEN];
};

struct overlayframe_info
{
  VVASFrame *inframe;
  Mat image;
  Mat I420image;
  Mat NV12image;
  Mat lumaImg;
  Mat chromaImg;
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

struct vvas_xoverlaypriv
{
  float font_size;
  unsigned int font;
  int line_thickness;
  int y_offset;
  int display_level;
  color label_color;
  char label_filter[MAX_ALLOWED_LABELS][MAX_LABEL_LEN];
  unsigned char label_filter_cnt;
  unsigned short classes_count;
  vvass_xclassification class_list[MAX_ALLOWED_CLASS];
  struct overlayframe_info frameinfo;
  struct parentlabel_info plabel;
};

/* Check if the given classification is to be filtered */
int
vvas_classification_is_allowed (char *cls_name, vvas_xoverlaypriv * kpriv)
{
  unsigned int idx;

  if (cls_name == NULL)
    return -1;

  for (idx = 0;
      idx < sizeof (kpriv->class_list) / sizeof (kpriv->class_list[0]); idx++) {
    if (!strcmp (cls_name, kpriv->class_list[idx].class_name)) {
      return idx;
    }
  }
  return -1;
}

/* Get y and uv color components corresponding to givne RGB color */
void
convert_rgb_to_yuv_clrs (color clr, unsigned char *y, unsigned short *uv)
{
  Mat YUVmat;
  Mat BGRmat (2, 2, CV_8UC3, Scalar (clr.red, clr.green, clr.blue));
  cvtColor (BGRmat, YUVmat, cv::COLOR_BGR2YUV_I420);
  *y = YUVmat.at < uchar > (0, 0);
  *uv = YUVmat.at < uchar > (2, 0) << 8 | YUVmat.at < uchar > (2, 1);
  return;
}

/* Compose label text based on config json */
bool
get_label_text (GstInferenceClassification * c, GstInferencePrediction *p, vvas_xoverlaypriv * kpriv,
    char *label_string)
{
  unsigned char idx = 0, buffIdx = 0;
  if ((!c->class_label || !strlen ((char *) c->class_label)) && 
      (!p->obj_track_label || !strlen ((char *) p->obj_track_label)))
    return false;

  for (idx = 0; idx < kpriv->label_filter_cnt; idx++) {
    if (!strcmp (kpriv->label_filter[idx], "class")) {
      if (c->class_label) {
        snprintf (label_string + buffIdx, (MAX_LABEL_LEN - buffIdx), "%s", (char *)c->class_label);
        buffIdx = strlen (label_string);
      }
    } else if (!strcmp (kpriv->label_filter[idx], "tracker-id")) {
      if (p->obj_track_label) {
        if (strlen (label_string)) {
          snprintf (label_string + buffIdx, (MAX_LABEL_LEN - buffIdx), " : ");
          buffIdx = strlen (label_string);
        }
        snprintf (label_string + buffIdx, (MAX_LABEL_LEN - buffIdx), "%s", (char *)p->obj_track_label);
        buffIdx = strlen (label_string);
      }
    } else if (!strcmp (kpriv->label_filter[idx], "probability")) {
      if (strlen (label_string)) {
        snprintf (label_string + buffIdx, (MAX_LABEL_LEN - buffIdx), " : ");
        buffIdx = strlen (label_string);
      }
      snprintf (label_string + buffIdx, (MAX_LABEL_LEN - buffIdx), "%.2f ", c->class_prob);
      buffIdx = strlen (label_string);
    }
  }
  return true;
}

static gboolean
overlay_node_foreach (GNode * node, gpointer kpriv_ptr)
{
  vvas_xoverlaypriv *kpriv = (vvas_xoverlaypriv *) kpriv_ptr;
  struct overlayframe_info *frameinfo = &(kpriv->frameinfo);
  struct parentlabel_info *plabel = &(kpriv->plabel);
  bool append_label = FALSE;
  bool draw = FALSE;
  LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");

  GList *classes;
  GstInferenceClassification *classification;
  GstInferencePrediction *prediction = (GstInferencePrediction *) node->data;
  int level = g_node_depth (node) - 1;

  if (!kpriv->display_level || (level == kpriv->display_level))
    draw = TRUE;

  /* On each children, iterate through the different associated classes */
  for (classes = prediction->classifications;
      classes; classes = g_list_next (classes)) {
    classification = (GstInferenceClassification *) classes->data;

    int idx = vvas_classification_is_allowed ((char *)
        classification->class_label, kpriv);

    if (idx == -1 && classification->class_label == NULL && prediction->obj_track_label != NULL)
      idx = 0;

    if (kpriv->classes_count && idx == -1)
      continue;

    color clr;
    if (kpriv->classes_count) {
      clr = {
      kpriv->class_list[idx].class_color.blue,
            kpriv->class_list[idx].class_color.green,
            kpriv->class_list[idx].class_color.red};
    } else {
      /* If there are no classes specified, assign based on level */
      if (level == 1)
        clr = {255, 0, 0}; /* blue */
      else if (level == 2)
        clr = {0, 255, 0}; /* green */
      else if (level == 3)
        clr = {0, 0, 255}; /* red */
      else
        clr = {255, 255, 0}; /* aqua */
    }

    char label_string[MAX_LABEL_LEN];
    bool label_present;
    Size textsize;
    memset(label_string, 0, MAX_LABEL_LEN);
    label_present = get_label_text (classification, prediction, kpriv, label_string);

    if (label_present) {
      int baseline;
      textsize = getTextSize (label_string, kpriv->font,
          kpriv->font_size, 1, &baseline);
      /* Get y offset to use in case of classification model */
      if ((prediction->bbox.height < 1) && (prediction->bbox.width < 1)) {
        if (kpriv->y_offset) {
          frameinfo->y_offset = kpriv->y_offset;
        } else {
          frameinfo->y_offset = (frameinfo->inframe->props.height * 0.10);
        }
        if (plabel->prev_x || plabel->prev_y)
          append_label = TRUE;
      }
    }

    if (draw) {
      LOG_MESSAGE (LOG_LEVEL_INFO,
          "RESULT: (prediction node %ld) %s(%d) %d %d %d %d (%f)",
          prediction->prediction_id,
          label_present ? classification->class_label : NULL,
          classification->class_id, prediction->bbox.x, prediction->bbox.y,
          prediction->bbox.width + prediction->bbox.x,
          prediction->bbox.height + prediction->bbox.y,
          classification->class_prob);
    }

    /* Check whether the frame is NV12 or BGR and act accordingly */
    if (frameinfo->inframe->props.fmt == VVAS_VFMT_Y_UV8_420) {
      unsigned char yScalar;
      unsigned short uvScalar;
      convert_rgb_to_yuv_clrs (clr, &yScalar, &uvScalar);
      /* Draw rectangle on y an uv plane */
      int new_xmin = floor (prediction->bbox.x / 2) * 2;
      int new_ymin = floor (prediction->bbox.y / 2) * 2;
      int new_xmax =
          floor ((prediction->bbox.width + prediction->bbox.x) / 2) * 2;
      int new_ymax =
          floor ((prediction->bbox.height + prediction->bbox.y) / 2) * 2;
      Size test_rect (new_xmax - new_xmin, new_ymax - new_ymin);

      if (!(!prediction->bbox.x && !prediction->bbox.y) && draw) {
        LOG_MESSAGE (LOG_LEVEL_INFO, "Drawing rectangle for NV12 image");
        /* last parent coordinates */
        plabel->x = new_xmin;
        plabel->y = new_ymin;
        rectangle (frameinfo->lumaImg, Point (new_xmin,
              new_ymin), Point (new_xmax,
              new_ymax), Scalar (yScalar), kpriv->line_thickness, 1, 0);
        rectangle (frameinfo->chromaImg, Point (new_xmin / 2,
              new_ymin / 2), Point (new_xmax / 2,
              new_ymax / 2), Scalar (uvScalar), kpriv->line_thickness, 1, 0);
      }

      if (label_present) {
        if (append_label) {
          /* moving to next line if not sufficient */
          if (plabel->prev_x + textsize.width > frameinfo->inframe->props.width) {
            plabel->prev_x = plabel->x;
            /* some gap to previous label */
            plabel->prev_y += 0.01 * frameinfo->inframe->props.height + plabel->text_height;
          }
          new_xmin = plabel->prev_x;
          new_ymin = plabel->prev_y;
          frameinfo->y_offset = plabel->y_offset;
        }
        if (draw) {
          /* Draw filled rectangle for labelling, both on y and uv plane */
          rectangle (frameinfo->lumaImg, Rect (Point (new_xmin,
                      new_ymin + frameinfo->y_offset - textsize.height), textsize),
              Scalar (yScalar), FILLED, 1, 0);
        }
        textsize.height /= 2;
        textsize.width /= 2;
        if (draw) {
          rectangle (frameinfo->chromaImg, Rect (Point (new_xmin / 2,
                      new_ymin / 2 + frameinfo->y_offset / 2 - textsize.height), textsize),
              Scalar (uvScalar), FILLED, 1, 0);

          /* Draw label text on the filled rectanngle */
          convert_rgb_to_yuv_clrs (kpriv->label_color, &yScalar, &uvScalar);
          putText (frameinfo->lumaImg, label_string, cv::Point (new_xmin,
                  new_ymin + frameinfo->y_offset), kpriv->font, kpriv->font_size,
              Scalar (yScalar), 1, 1);
          putText (frameinfo->chromaImg, label_string, cv::Point (new_xmin / 2,
                  new_ymin / 2 + frameinfo->y_offset / 2), kpriv->font,
              kpriv->font_size / 2, Scalar (uvScalar), 1, 1);
        }

        /* store the parent label info */
        if (draw)
          plabel->prev_x = 0.01 * frameinfo->inframe->props.width + new_xmin + textsize.width * 2;
        else
          plabel->prev_x = new_xmin;
        plabel->prev_y = new_ymin;
        plabel->text_height = textsize.height * 2;
        plabel->y_offset = frameinfo->y_offset;
      }
    } else if (frameinfo->inframe->props.fmt == VVAS_VFMT_BGR8) {

      int new_xmin = prediction->bbox.x;
      int new_ymin = prediction->bbox.y;
      int new_xmax = prediction->bbox.width + prediction->bbox.x;
      int new_ymax = prediction->bbox.height + prediction->bbox.y;

      if (!(!prediction->bbox.x && !prediction->bbox.y) && draw) {
        LOG_MESSAGE (LOG_LEVEL_DEBUG, "Drawing rectangle for BGR image");
        /* last parent coordinates */
        plabel->x = new_xmin;
        plabel->y = new_ymin;
        /* Draw rectangle over the dectected object */
        rectangle (frameinfo->image, Point (new_xmin, new_ymin),
          Point (new_xmax, new_ymax), Scalar (clr.blue,
              clr.green, clr.red), kpriv->line_thickness, 1, 0);
      }

      if (label_present) {
        if (append_label) {
          /* moving to next line if not sufficient */
          if (plabel->prev_x + textsize.width > frameinfo->inframe->props.width) {
            plabel->prev_x = plabel->x;
            /* some gap to previous label */
            plabel->prev_y += 0.01 * frameinfo->inframe->props.height + plabel->text_height;
          }
          new_xmin = plabel->prev_x;
          new_ymin = plabel->prev_y;
          frameinfo->y_offset = plabel->y_offset;
        }
        if (draw) {
          /* Draw filled rectangle for label */
          rectangle (frameinfo->image, Rect (Point (new_xmin,
                      new_ymin + frameinfo->y_offset - textsize.height), textsize),
              Scalar (clr.blue, clr.green, clr.red), FILLED, 1, 0);

          /* Draw label text on the filled rectanngle */
          putText (frameinfo->image, label_string,
              cv::Point (new_xmin,
                  new_ymin + frameinfo->y_offset), kpriv->font,
              kpriv->font_size, Scalar (kpriv->label_color.blue,
                  kpriv->label_color.green, kpriv->label_color.red), 1, 1);
        }

        /* store the parent label info */
        if (draw)
          plabel->prev_x = 0.01 * frameinfo->inframe->props.width + new_xmin + textsize.width;
        else
          plabel->prev_x = new_xmin;
        plabel->prev_y = new_ymin;
        plabel->text_height = textsize.height;
        plabel->y_offset = frameinfo->y_offset;
      }
    }
  }



  return FALSE;
}

extern "C"
{
  int32_t xlnx_kernel_init (VVASKernel * handle)
  {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");

    vvas_xoverlaypriv *kpriv =
        (vvas_xoverlaypriv *) calloc (1, sizeof (vvas_xoverlaypriv));

    json_t *jconfig = handle->kernel_config;
    json_t *val, *karray = NULL, *classes = NULL;

    /* Initialize config params with default values */
    log_level = LOG_LEVEL_WARNING;
    kpriv->font_size = 0.5;
    kpriv->font = 0;
    kpriv->line_thickness = 1;
    kpriv->y_offset = 0;
    kpriv->label_color = {0, 0, 0};
    strcpy(kpriv->label_filter[0], "class");
    strcpy(kpriv->label_filter[1], "tracker-id");
    strcpy(kpriv->label_filter[2], "probability");
    kpriv->label_filter_cnt = 3;
    kpriv->classes_count = 0;


     val = json_object_get (jconfig, "display_level");
    if (!val || !json_is_integer (val))
        kpriv->display_level = 0;
    else {
        kpriv->display_level = json_integer_value (val);
        if (kpriv->display_level < 0) {
          kpriv->display_level = 0;
          LOG_MESSAGE (LOG_LEVEL_WARNING, "display level should be greater than or equal to 0");
        }
    }

     val = json_object_get (jconfig, "debug_level");
    if (!val || !json_is_integer (val))
        log_level = LOG_LEVEL_WARNING;
    else
        log_level = json_integer_value (val);

      val = json_object_get (jconfig, "font_size");
    if (!val || !json_is_integer (val))
        kpriv->font_size = 0.5;
    else
        kpriv->font_size = json_integer_value (val);

      val = json_object_get (jconfig, "font");
    if (!val || !json_is_integer (val))
        kpriv->font = 0;
    else
        kpriv->font = json_integer_value (val);

      val = json_object_get (jconfig, "thickness");
    if (!val || !json_is_integer (val))
        kpriv->line_thickness = 1;
    else
        kpriv->line_thickness = json_integer_value (val);

      val = json_object_get (jconfig, "y_offset");
    if (!val || !json_is_integer (val))
        kpriv->y_offset = 0;
    else
        kpriv->y_offset = json_integer_value (val);

    /* get label color array */
      karray = json_object_get (jconfig, "label_color");
    if (!karray)
    {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to find label_color");
      return -1;
    } else
    {
      kpriv->label_color.blue =
          json_integer_value (json_object_get (karray, "blue"));
      kpriv->label_color.green =
          json_integer_value (json_object_get (karray, "green"));
      kpriv->label_color.red =
          json_integer_value (json_object_get (karray, "red"));
    }

    karray = json_object_get (jconfig, "label_filter");

    if (!json_is_array (karray)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "label_filter not found in the config\n");
      return -1;
    }
    kpriv->label_filter_cnt = 0;
    for (unsigned int index = 0; index < json_array_size (karray); index++) {
      strcpy (kpriv->label_filter[index],
          json_string_value (json_array_get (karray, index)));
      kpriv->label_filter_cnt++;
    }

    /* get classes array */
    karray = json_object_get (jconfig, "classes");
    if (!karray) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to find key labels");
      return -1;
    }

    if (!json_is_array (karray)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "labels key is not of array type");
      return -1;
    }
    kpriv->classes_count = json_array_size (karray);
    for (unsigned int index = 0; index < kpriv->classes_count; index++) {
      classes = json_array_get (karray, index);
      if (!classes) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to get class object");
        return -1;
      }

      val = json_object_get (classes, "name");
      if (!json_is_string (val)) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, "name is not found for array %d", index);
        return -1;
      } else {
        strncpy (kpriv->class_list[index].class_name,
            (char *) json_string_value (val), MAX_CLASS_LEN - 1);
        LOG_MESSAGE (LOG_LEVEL_DEBUG, "name %s",
            kpriv->class_list[index].class_name);
      }

      val = json_object_get (classes, "green");
      if (!val || !json_is_integer (val))
        kpriv->class_list[index].class_color.green = 0;
      else
        kpriv->class_list[index].class_color.green = json_integer_value (val);

      val = json_object_get (classes, "blue");
      if (!val || !json_is_integer (val))
        kpriv->class_list[index].class_color.blue = 0;
      else
        kpriv->class_list[index].class_color.blue = json_integer_value (val);

      val = json_object_get (classes, "red");
      if (!val || !json_is_integer (val))
        kpriv->class_list[index].class_color.red = 0;
      else
        kpriv->class_list[index].class_color.red = json_integer_value (val);
    }

    handle->kernel_priv = (void *) kpriv;
    return 0;
  }

  uint32_t xlnx_kernel_deinit (VVASKernel * handle)
  {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");
    vvas_xoverlaypriv *kpriv = (vvas_xoverlaypriv *) handle->kernel_priv;
    struct overlayframe_info *frameinfo = &(kpriv->frameinfo);

    if (frameinfo->lumaImg.data) {
      frameinfo->lumaImg.release ();
      frameinfo->chromaImg.release ();
    }
    if (frameinfo->image.data) {
      frameinfo->image.release ();
    }

    if (kpriv)
      free (kpriv);

    return 0;
  }


  uint32_t xlnx_kernel_start (VVASKernel * handle, int start,
      VVASFrame * input[MAX_NUM_OBJECT], VVASFrame * output[MAX_NUM_OBJECT])
  {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");
    GstInferenceMeta *infer_meta = NULL;
    char *pstr;
#ifdef XLNX_PCIe_PLATFORM
    GstMemory *in_mem;
#endif
    vvas_xoverlaypriv *kpriv = (vvas_xoverlaypriv *) handle->kernel_priv;
    struct overlayframe_info *frameinfo = &(kpriv->frameinfo);
    struct parentlabel_info *plabel = &(kpriv->plabel);

    plabel->x = plabel->y = plabel->text_height = 0;
    plabel->prev_x = plabel->prev_y = 0;
    frameinfo->y_offset = 0;
    frameinfo->inframe = input[0];

#ifdef XLNX_PCIe_PLATFORM
    in_mem = gst_buffer_get_memory ((GstBuffer *)frameinfo->inframe->app_priv, 0);
    gst_vvas_memory_unset_flag (in_mem, VVAS_SYNC_TO_DEVICE);
    gst_memory_unref(in_mem);
#endif

    char *indata = (char *) frameinfo->inframe->vaddr[0];
    char *lumaBuf = (char *) frameinfo->inframe->vaddr[0];
    char *chromaBuf = (char *) frameinfo->inframe->vaddr[1];
    infer_meta = ((GstInferenceMeta *) gst_buffer_get_meta ((GstBuffer *)
            frameinfo->inframe->app_priv, gst_inference_meta_api_get_type ()));
    if (infer_meta == NULL) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG,
          "vvas meta data is not available for postdpu");
      return false;
    } else {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "vvas_mata ptr %p", infer_meta);
    }

    if (frameinfo->inframe->props.fmt == VVAS_VFMT_Y_UV8_420) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "Input frame is in NV12 format\n");
      frameinfo->lumaImg.create (input[0]->props.height, input[0]->props.stride,
          CV_8UC1);
      frameinfo->lumaImg.data = (unsigned char *) lumaBuf;
      frameinfo->chromaImg.create (input[0]->props.height / 2,
          input[0]->props.stride / 2, CV_16UC1);
      frameinfo->chromaImg.data = (unsigned char *) chromaBuf;
    } else if (frameinfo->inframe->props.fmt == VVAS_VFMT_BGR8) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "Input frame is in BGR format\n");
      frameinfo->image.create (input[0]->props.height,
          input[0]->props.stride / 3, CV_8UC3);
      frameinfo->image.data = (unsigned char *) indata;
    } else {
      LOG_MESSAGE (LOG_LEVEL_WARNING, "Unsupported color format\n");
      return 0;
    }


    /* Print the entire prediction tree */
    pstr = gst_inference_prediction_to_string (infer_meta->prediction);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "Prediction tree: \n%s", pstr);
    free (pstr);

    g_node_traverse (infer_meta->prediction->predictions, G_PRE_ORDER,
        G_TRAVERSE_ALL, -1, overlay_node_foreach, kpriv);

    return 0;
  }


  int32_t xlnx_kernel_done (VVASKernel * handle)
  {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");
    return 0;
  }
}
