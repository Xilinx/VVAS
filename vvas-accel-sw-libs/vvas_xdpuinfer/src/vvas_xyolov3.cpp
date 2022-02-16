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

#include "vvas_xyolov3.hpp"
#include <algorithm>

vvas_xyolov3::vvas_xyolov3 (vvas_xkpriv * kpriv, const std::string & model_name,
    bool need_preprocess)
{
  log_level = kpriv->log_level;
  kpriv->labelflags = VVAS_XLABEL_REQUIRED;
  LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");

  if (kpriv->labelptr == NULL) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "label not found");
    kpriv->labelflags |= VVAS_XLABEL_NOT_FOUND;
  } else
    kpriv->labelflags |= VVAS_XLABEL_FOUND;


  model = vitis::ai::YOLOv3::create (model_name, need_preprocess);
}

bool compare_by_area (const vitis::ai::YOLOv3Result::BoundingBox &box1, const vitis::ai::YOLOv3Result::BoundingBox &box2)
{
  float area1 = (box1.width * box1.height);
  float area2 = (box2.width * box2.height);
  return (area1 > area2);
}

int
vvas_xyolov3::run (vvas_xkpriv * kpriv, std::vector<cv::Mat>& images,
    GstInferencePrediction **predictions)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter batch");
  auto results = model->run (images);

  labels *lptr;
  char *pstr;                   /* prediction string */

  if (kpriv->labelptr == NULL) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "label not found");
    return false;
  }

  if (kpriv->objs_detection_max > 0) {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "sort detected objects based on bbox area");

    /* sort objects based on dimension to pick objects with bigger bbox */
    for (unsigned int i = 0u; i < results.size(); i++) {
      std::sort(results[i].bboxes.begin(), results[i].bboxes.end(), compare_by_area);
    }
  } else {
    LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level, "max-objects count is zero. So, not doing any metadata processing");
    return true;
  }

  for (auto i = 0u; i < results.size(); i++) {
    GstInferencePrediction *parent_predict = NULL;
    unsigned int cur_objs = 0;

    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "objects detected %lu",
        results[i].bboxes.size());

    if (results[i].bboxes.size()) {
      BoundingBox parent_bbox;
      int cols = images[i].cols;
      int rows = images[i].rows;

      parent_predict = predictions[i];

      for (auto & box:results[i].bboxes) {

        lptr = kpriv->labelptr + box.label;
        if (kpriv->filter_labels.size()) {
          bool found_label = false;

          for (unsigned int n = 0; n < kpriv->filter_labels.size(); n++) {
            const char *filter_label = kpriv->filter_labels[n].c_str();
            const char *current_label = lptr->display_name.c_str();
            if (!strncmp (current_label, filter_label, strlen (filter_label))) {
              LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "current label %s is in filter_label list", current_label);
              found_label = true;
            }
          }

          if (!found_label)
            continue;
        }

        if (!parent_predict) {
          parent_bbox.x = parent_bbox.y = 0;
          parent_bbox.width = cols;
          parent_bbox.height = rows;
          parent_predict = gst_inference_prediction_new_full (&parent_bbox);
        }
        int label = box.label;
        float xmin = box.x * cols + 1;
        float ymin = box.y * rows + 1;
        float xmax = xmin + box.width * cols;
        float ymax = ymin + box.height * rows;
        if (xmin < 0.)
          xmin = 1.;
        if (ymin < 0.)
          ymin = 1.;
        if (xmax > cols)
          xmax = cols;
        if (ymax > rows)
          ymax = rows;
        float confidence = box.score;

        BoundingBox bbox;
        GstInferencePrediction *predict;
        GstInferenceClassification *c = NULL;

        bbox.x = xmin;
        bbox.y = ymin;
        bbox.width = xmax - xmin;
        bbox.height = ymax - ymin;

        predict = gst_inference_prediction_new_full (&bbox);

        c = gst_inference_classification_new_full (label, confidence,
            lptr->display_name.c_str (), 0, NULL, NULL, NULL);
        gst_inference_prediction_append_classification (predict, c);

        if (parent_predict->predictions == NULL)
          LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "parent_predict->predictions is NULL");
        gst_inference_prediction_append (parent_predict, predict);

        LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level,
            "RESULT: %s(%d) %f %f %f %f (%f)", lptr->display_name.c_str (), label,
            xmin, ymin, xmax, ymax, confidence);

        cur_objs++;
        if (cur_objs == kpriv->objs_detection_max) {
          LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "reached max limit of objects to add to metadata");
          break;
        }
      }

      if (parent_predict) {
        pstr = gst_inference_prediction_to_string (parent_predict);
        LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "prediction tree : \n%s",
            pstr);
        free(pstr);
      }
    }
    predictions[i] = parent_predict;
  }

  LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, " ");

  return true;
}

int
vvas_xyolov3::requiredwidth (void)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
  return model->getInputWidth ();
}

int
vvas_xyolov3::requiredheight (void)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
  return model->getInputHeight ();
}

int
vvas_xyolov3::supportedbatchsz (void)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
  return model->get_input_batch ();
}

int
vvas_xyolov3::close (void)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
  return true;
}

vvas_xyolov3::~vvas_xyolov3 ()
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
}
