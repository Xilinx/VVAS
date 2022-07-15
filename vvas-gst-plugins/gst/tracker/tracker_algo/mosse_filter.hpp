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

#pragma once

#ifndef _MOSSE_FILTER_HPP_
#define _MOSSE_FILTER_HPP_

#include "define.hpp"
#include <vector>
using namespace std;

//scaling factor for matching at multiple scales
#define MOSSE_SCALING_FACT 1.05

//Weightage for detected confidence value at differnt scale
#define MOSSE_SCALE_WT 0.95

//Sigma value for Gaussian weights estimation
#define MOSSE_GUS_SIGMA 0.125

//Template size of objects
#define MOSSE_TMPL_SZ 96

//Learning rate for HoG features
#define MOSSE_LRN_RATE 0.08

//Learning rate for pixel based features
#define MOSSE_PIX_LRN_RATE 0.075

class mosse_filter
{
  Rectf bbox;
  Size obj_size;
  Size prev_obj_size;
  float scale_factor_x;
  float scale_factor_y;
  fMat hann_wts;
  
  fMat gau_wt;
  fMat in_F;
  fMat filter_H;
  fMat resp_G;
  fMat A;
  fMat B;
  Mat_img z;
  fMat rsz_z;
  void *head_ptr;
  
  // Gaussian weigt map is created based on size of object.
  void est_gaussian_wts(int rows, int cols, fMat *dst);

  // create Hanning mat for data smoothing. Created first time for each object
  void hanning_weights(int rows, int cols, fMat *hann_mat);

  // Detect object in the current frame.
  Point2f locate_new_position(float &peak_value);

  // finding sub_pixel accuracy
  float sub_pix_estimation(float left, float center, float right);

  void extract_object(Mat_img frame, bool first, float scale, fMat *obj);

  void estimate_fourier(fMat in, fMat out, bool fft_type);

  void apply_Hanning_window(fMat hann_win, fMat *inout);

  void filter_response();

  void filter_response_update();

  void train_update();

public:
  float padding_scale; // area around the object based on size of object
  float lrn_rate; // learning rate for updation
  float inv_lrn_rate;
  int tmpl_sz; // template size
  float scaling; // scale step for multi-scale estimation
  int scale_type; //1: all scales, 2: up and same scale, 3: down and same scale

   // Initialize tracker 
  void init_cf(track_config tconfig, Rectf bbox, Mat_img frame);
  void deinit_cf();
  void detect_update(Rectf bbox, Mat_img frame);
  // Update position based on the new frame
  float update_position(Mat_img frame, Rectf *bbox, float conf_score);
};
#endif /* _MOSSE_FILTER_HPP_ */
