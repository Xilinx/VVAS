/*
 * Copyright 2021 Xilinx, Inc.
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

#include "vvas_xsegmentation.hpp"

vvas_xsegmentation::vvas_xsegmentation (vvas_xkpriv * kpriv,
    const std::string & model_name, bool need_preprocess)
{
  log_level = kpriv->log_level;
  LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");

  model = vitis::ai::Segmentation::create (model_name, need_preprocess);
}

int
vvas_xsegmentation::run (vvas_xkpriv * kpriv, std::vector<cv::Mat>& images,
    GstInferencePrediction **predictions)
{
  std::vector<vitis::ai::SegmentationResult> results;

  LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "enter");
  if (kpriv->segoutfmt == VVAS_VFMT_BGR8)
    results = model->run_8UC3 (images);
  else if (kpriv->segoutfmt == VVAS_VFMT_Y8) {
    results = model->run_8UC1 (images);
    for (auto i = 0u; i < results.size(); i++) {
      if ( !(kpriv->segoutfactor == 0 || kpriv->segoutfactor == 1)) {
        for (auto y = 0; y < results[i].segmentation.rows; y++) {
          for (auto x = 0; x < results[i].segmentation.cols; x++) {
            results[i].segmentation.at < uchar > (y, x) *= kpriv->segoutfactor;
          }
        }
      }
    }
  } else {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "unsupported fmt");
    return false;
  }
  for (auto i = 0u; i < results.size(); i++) {
    BoundingBox parent_bbox;
    GstInferencePrediction *parent_predict = NULL;
    int cols = results[i].segmentation.cols;
    int rows = results[i].segmentation.rows;

    parent_predict = predictions[i];

    if (!parent_predict) {
      parent_bbox.x = parent_bbox.y = 0;
      parent_bbox.width = cols;
      parent_bbox.height = rows;
      parent_predict = gst_inference_prediction_new_full (&parent_bbox);
    }

    {
      gint size;
      Segmentation *seg;
      GstInferencePrediction *predict;
      predict = gst_inference_prediction_new ();
      char *pstr;                   /* prediction string */

      seg = &predict->segmentation;
      seg->width = cols;
      seg->height = rows;
      kpriv->segoutfmt == VVAS_VFMT_BGR8 ? strcpy (seg->fmt,
          "BGR") : strcpy (seg->fmt, "GRAY8");
      if (!strcmp (seg->fmt, "BGR"))
        size = (seg->width * seg->height * 3);
      else
        size = (seg->width * seg->height);

      seg->data = g_memdup ((void *) (results[i].segmentation.data), size);
      seg->buffer = gst_buffer_new_wrapped_full ((GstMemoryFlags)0, seg->data,
	  size, 0, size, seg->data, g_free);

      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
          "gstBuffer = %p append to metadata", seg->buffer);

      gst_inference_prediction_append (parent_predict, predict);

      pstr = gst_inference_prediction_to_string (parent_predict);
      LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "prediction tree : \n%s",
          pstr);
      free (pstr);

      predictions[i] = parent_predict;
    }
  }
  LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, " ");

  return true;
}


int
vvas_xsegmentation::requiredwidth (void)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
  return model->getInputWidth ();
}

int
vvas_xsegmentation::requiredheight (void)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
  return model->getInputHeight ();
}

int
vvas_xsegmentation::supportedbatchsz (void)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
  return model->get_input_batch ();
}

int
vvas_xsegmentation::close (void)
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
  return true;
}

vvas_xsegmentation::~vvas_xsegmentation ()
{
  LOG_MESSAGE (LOG_LEVEL_DEBUG, log_level, "enter");
}
