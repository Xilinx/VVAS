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

#ifndef _CORRELATION_FILTER_HPP_
#define _CORRELATION_FILTER_HPP_

#include "define.hpp"
#if !defined(XLNX_EMBEDDED_PLATFORM) && !defined(XLNX_PCIe_PLATFORM)
#include "features.hpp"
#endif
#include <vector>
using namespace std;

//scaling factor for matching at multiple scales
#define CORR_SCALING_FACT 1.05

//Weightage for detected confidence value at differnt scale
#define CORR_SCALE_WT 0.95

//Regularization
#define CORR_LAMBDA 0.0001

//Sigma value for Gaussian weights estimation
#define CORR_GUS_SIGMA 0.125

//Template size of objects
#define CORR_HOG_TMPL_SZ 96

//Sigma value for Gaussian Kernel
#define CORR_GUS_KRNL_SIGMA 0.6

//Learning rate for HoG features
#define CORR_HOG_LRN_RATE 0.012

//Learning rate for pixel based features
#define CORR_PIX_LRN_RATE 0.075

#if defined(XLNX_EMBEDDED_PLATFORM) || defined(XLNX_PCIe_PLATFORM)
#define CORR_CELL_SIZE 4
#endif

class correlation_filter
{
  Rectf bbox;
  Size obj_size;
  float scale_factor;
  float norm_factor;
  bool use_hog;
  fMat hann_wts;
  int fet_vec_size[3];
  fMat alphaf;
  fMat gau_wt;
  fMat obj_tmpl;
  Mat_img z;
  Mat_img rsz_z;
  fMat cur_tmpl;
  fMat comp1;
  fMat comp2;
  fMat comp3;
  fMat comp4;
  fMat rdata;
  fMat prev_fft;
  void *head_ptr;
  float *hog_feat;
  fMat tMat;

  // Gaussian weigt map is created based on size of object.
  void est_gaussian_wts(int rows, int cols);

  // create Hanning mat for data smoothing. Created first time for each object
  void hanning_weights();

  // For feature extraction from roi. HoG or pixel based feature are extracted
  void extract_feature(Mat_img frame, fMat *tmpl, bool first, float scale);

  // online training of object based on extracted features
  void train_features(fMat x, float lrn_rate);

  //Estimate correlation between two rois.
  void correlation_filtering(fMat x1, fMat x2, float sum_sqr, fMat *res, bool auto_corr);

  // Detect object in the current frame.
  Point2f locate_new_position(fMat z, fMat x, float sum_sqr, float &peak_value);

  // finding sub_pixel accuracy
  float sub_pix_estimation(float left, float center, float right);

public:
  int fet_length; //length of feature vector
  int cell_size; // HOG cell size
  float padding_scale; // area around the object based on size of object
  float lrn_rate; // learning rate for updation
  float inv_sigma2;
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
#endif /* _CORRELATION_FILTER_HPP_ */
