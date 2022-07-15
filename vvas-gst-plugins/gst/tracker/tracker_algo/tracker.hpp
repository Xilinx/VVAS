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

#ifndef _TRACKER_H_
#define _TRACKER_H_

#pragma once

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string>


//#define TRACKER_OP //print output

//Maximum number of objects can be tracked
#define MAX_OBJ_TRACK 16

//Spatial distance threshold percentage of previous distance for object matching
#define SP_DIST_THR 0.6

//Scale learning rate for exponential moving avarage
#define LRN_SCALE 0.4
//Position learning rate for exponential moving avarage
#define LRN_POS 0.6
//Scale learning rate for exponential moving avarage
#define LRN_DIFF_SCALE 0.3
//Position learning rate for exponential moving avarage
#define LRN_DIFF_POS 0.4
//Position learning rate for exponential moving avarage
#define LRN_VEL 0.7

//detection inverval
#define	DET_INTVL 5
//For 15 frames constrain time_con will be 15/DET_INTVL i.e 3
#define NUM_FRM_INACTIVE 300 
//Number of detection frames confidence before assigning id
#define NUM_FRM_CONFIDENCE 3
//Padding over object boundaries
#define PADDING 1.5
//Minimum height for considering as noise
#define MIN_HEIGHT 60
//Minimum width for considering as noise
#define MIN_WIDTH 20
//Correlation threshold for histogram matching
#define CORR_THR 0.7

#define OVERLAP_THR 0

#define SCALE_CHG_THR 0.7

#define DIST_CORR_WT 0.7

#define DIST_OVERLAP_WT 0.2

#define DIST_SCALE_CHG_WT 0.1

//confidence score
#define CONF_SCORE 0.25
//confidence score under occlusion
#define CONF_OCC_SCORE 0.15

#define OCC_THR 0.4

typedef enum
{
  ALGO_IOU,
  ALGO_MOSSE,
  ALGO_KCF,
  ALGO_NONE,
} tracker_algo_type;

typedef enum
{
  USE_RGB,
  USE_HSV,
} tracker_color_space;

typedef struct {
  tracker_algo_type tracker_type; //0:IOU 1:KCF Tracker 2:MOSSE Tracker
  bool iou_use_color;
  tracker_color_space obj_match_color; //0:RGB 1:HSV
  unsigned int fixed_window;
  unsigned int det_intvl;
  unsigned int multiscale;
  unsigned int fet_length;
  unsigned int hog_feature;
  unsigned int min_width; //Minimum width for considering as noise
  unsigned int min_height; //Minimum height for considering as noise
  unsigned int max_width; //Minimum width for considering as noise
  unsigned int max_height; //Minimum height for considering as noise
  int num_inactive_frames; //For 15 frames constrain time_con will be 15/detection_interval
  int num_frames_confidence; //Number of detection frames confidence before assigning id
  float padding; // extra area surrounding the target
  float obj_match_search_region;
  float dist_correlation_threshold;
  float dist_overlap_threshold;
  float dist_scale_change_threshold;
  float dist_correlation_weight;
  float dist_overlap_weight;
  float dist_scale_change_weight;
  float occlusion_threshold;
  float confidence_score; //confidence score
  float confidence_occlusion_score; //confidence score under occlusion
  float lrn_scale; //Scale learning rate for exponential moving avarage
  float lrn_pos; //Position learning rate for exponential moving avarage
  float lrn_vel; //Position learning rate for exponential moving avarage
} track_config;

typedef struct {
  short x;
  short y;
  short width;
  short height;
  unsigned short status;
  unsigned int trk_id; //Unique id given by tracker
  unsigned int map_id; //to map prediction id
} obj_info;

typedef struct {
  int num_objs;
  obj_info objs[MAX_OBJ_TRACK];
} objs_data;

typedef struct {
  short width;
  short height;
  short channels;
  unsigned char *data;
} Mat_img;

typedef struct {
  char *tracker_info;
  objs_data new_objs;
  objs_data trk_objs;
  int ids;
} tracker_handle;

int init_tracker(tracker_handle *tracker_data, track_config *config);

int run_tracker(Mat_img img, track_config tconfig, tracker_handle *tracker_data, bool detect_flag);

int deinit_tracker(tracker_handle *tracker_data);

#endif /* _TRACKER_H_ */



