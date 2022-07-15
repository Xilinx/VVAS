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

#ifndef _TRACKER_INT_H_
#define _TRACKER_INT_H_

#pragma once

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string>
#include "define.hpp"
#include "correlation_filter.hpp"
#include "mosse_filter.hpp"

#define OBJ_DIR 8
#define HIST_BIN 2
 //#define OCC_THR 0.4
 //y - 64 u - 32 v - 32 with HIST_BIN 2
 //192 - RGB 128 -  YUV
#define CORR_HIST_SIZE 192 //384 //192 - RGB 128 - YUV
//Number of frames for calcuating moving avarage

#define MAE_FRM 15

#define UPDATE_INTRVL 3

typedef struct {
  float cx;
  float cy;
  float w;
  float h;
  float dcx;
  float dcy;
  float dw;
  float dh;
  float d2cx;
  float d2cy;
  float d2w;
  float d2h;
  float vel;
} bbox_info;

typedef struct {
  int idx;
  bbox_info bbox_data[MAE_FRM];
  bbox_info bbox_sum;
  unsigned char complete;
} MAE_info;

//status -3 not initalized and allocated memory
//status -2 tracker is free for use
//status -1 object is inactive 
//status 0 confidence building mode
//status 1 tracker is active

class vvas_tracker {
public:
  vvas_tracker() { };
  int id;
  int status;
  MAE_info movingAvg_info;
  float hist_map_st[CORR_HIST_SIZE];
  float hist_map[CORR_HIST_SIZE];
  int cnt_inactive;
  int cnt_confidence;
  float conf_score;
  Rectf obj_rect;
  Rectf prev_pos;
  correlation_filter corr_tracker;
  mosse_filter mosse_tracker;
  int tracker_type;
  int cnt_update;

  void init_tracker(track_config tconfig, Rectf bbox, Mat_img img, float *hist_map);
  void track_update(Mat_img img, track_config tconfig);
  void detect_update(Rectf bbox, Mat_img img, float *hist_map);
  void deinit_tracker();
};

//vector<vvas_tracker> tracker;

#endif /* _TRACKER_INT_H_ */



