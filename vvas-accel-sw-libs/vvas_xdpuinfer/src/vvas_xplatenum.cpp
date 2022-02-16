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

#include "vvas_xplatenum.hpp"


vvas_xplatenum::vvas_xplatenum (vvas_xkpriv * kpriv,
    const std::string & model_name, bool need_preprocess)
{
  log_level = kpriv->log_level;
  LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");
  model = vitis::ai::PlateNum::create (model_name, need_preprocess);
}

int
vvas_xplatenum::run (vvas_xkpriv * kpriv, std::vector<cv::Mat>& images,
    GstInferencePrediction **predictions)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");

  auto results = model->run (images);

  char *pstr;                   /* prediction string */

  for (auto i = 0u; i < results.size(); i++) {
    GstInferencePrediction *parent_predict = NULL;

    //if (results[i].scores.size()) //TODO
    {
      BoundingBox parent_bbox;
      BoundingBox child_bbox;
      GstInferencePrediction *child_predict;

      int cols = images[i].cols;
      int rows = images[i].rows;

      parent_bbox.x = parent_bbox.y = 0;
      parent_bbox.width = cols;
      parent_bbox.height = rows;

      parent_predict = predictions[i];

      if (!parent_predict)
        parent_predict = gst_inference_prediction_new_full (&parent_bbox);

      child_bbox.x = 0;
      child_bbox.y = 0;
      child_bbox.width = 0;
      child_bbox.height = 0;
      child_predict = gst_inference_prediction_new_full (&child_bbox);

      GstInferenceClassification *c = NULL;

      c = gst_inference_classification_new_full (-1, 1,
            results[i].plate_number.c_str (), 0, NULL, NULL, NULL);
      gst_inference_prediction_append_classification (child_predict, c);

      if (parent_predict->predictions == NULL)
        LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "parent_predict->predictions is NULL");

      LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level,
        " num %s, color %s", results[i].plate_number.c_str (),
        results[i].plate_color.c_str ());
      gst_inference_prediction_append (parent_predict, child_predict);

      pstr = gst_inference_prediction_to_string (parent_predict);
      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "prediction tree : \n%s", pstr);
      free(pstr);
    }

    predictions[i] = parent_predict;
  }
  LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, " ");
  return true;
}

int
vvas_xplatenum::requiredwidth (void)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
  return model->getInputWidth ();
}

int
vvas_xplatenum::requiredheight (void)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
  return model->getInputHeight ();
}

int
vvas_xplatenum::supportedbatchsz (void)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
  return model->get_input_batch ();
}

int
vvas_xplatenum::close (void)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
  return true;
}

vvas_xplatenum::~vvas_xplatenum ()
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
}
