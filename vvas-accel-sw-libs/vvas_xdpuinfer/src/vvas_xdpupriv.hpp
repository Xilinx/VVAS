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

#pragma once

#ifndef DPU2_H
#define DPU2_H

#include <vector>
#include <stdio.h>
#include <string>
#include <opencv2/opencv.hpp>

#include <vvas/vvas_kernel.h>
#include <vvas/vvaslogs.h>
#include <gst/vvas/gstinferencemeta.h>
#include <gst/vvas/gstvvasinpinfer.h>

using namespace cv;
using namespace std;

/*  #define INT_MAX 2147483647 */

struct vvas_xkpriv;

class vvas_xdpumodel
{
public:
  virtual int run (vvas_xkpriv * kpriv, std::vector<cv::Mat>& images,
      GstInferencePrediction **predictions) = 0;
  virtual int requiredwidth (void) = 0;
  virtual int requiredheight (void) = 0;
  virtual int supportedbatchsz (void) = 0;
  virtual int close (void) = 0;
  virtual ~vvas_xdpumodel () = 0;
};

struct performance_test
{
  int test_started = 0;
  unsigned long frames = 0;
  unsigned long last_displayed_frame = 0;
  long long timer_start;
  long long last_displayed_time;
};
typedef struct performance_test vvas_perf;

struct labels
{
  std::string name;
  int label;
  std::string display_name;
};
typedef struct labels labels;

enum
{
  VVAS_XLABEL_NOT_REQUIRED = 0,
  VVAS_XLABEL_REQUIRED = 1,
  VVAS_XLABEL_NOT_FOUND = 2,
  VVAS_XLABEL_FOUND = 4
};

struct model_list
{
  vvas_xdpumodel *model;
  std::string modelname;
  labels *labelptr;
  int modelclass;
  VVASVideoFormat segoutfmt;    /* Required Segmentation output fmt */
  int segoutfactor;	        /* Multiply factor for Y8 output to look brightly */
};
typedef struct model_list model_list;

/**
 * struct vvas_xkpriv - Keep private data of vvas_xdpuinfer
 */
struct vvas_xkpriv
{
  vector < model_list > mlist;
  vvas_xdpumodel *model;        /* current dpu handler */
  VVASKernel *handle;           /* vvas kernel handler */
  int modelclass;               /* Class of model, from Json file */
  int modelnum;                 /* map class to number vvas_xmodelclass[] */
  int log_level;                /* LOG_LEVEL_ERROR=0, LOG_LEVEL_WARNING=1,
                                   LOG_LEVEL_INFO=2, LOG_LEVEL_DEBUG=3 */
  bool need_preprocess;         /* enable/disable pre-processing of DPU */
  bool performance_test;        /* enable/disable performance */
  bool run_time_model;          /* enable model load on every frame */
  unsigned int num_labels;
  labels *labelptr;             /* contain label array */
  int labelflags;               /* VVAS_XLABEL_NOT_REQUIRED, VVAS_XLABEL_REQUIRED,
                                   VVAS_XLABEL_NOT_FOUND, VVAS_XLABEL_FOUND */
  int max_labels;               /* number of labels in label.json */
  vvas_perf pf;                 /* required for performance */
  std::string modelpath;      /* contain model files path from json */
  std::string modelname;      /* contain name of model from json */
  std::string elfname;        /* contail model elf name */
  VVASVideoFormat modelfmt;     /* Format requirement of model */
  unsigned int batch_size;
  std::vector <string> filter_labels;
  unsigned int objs_detection_max; /* max objects metadata to be added to GstInferenceMeta */
  VVASVideoFormat segoutfmt;    /* Required Segmentation output fmt */
  int segoutfactor;         /* Multiply factor for Y8 output to look brightly */
};
typedef struct vvas_xkpriv vvas_xkpriv;

#endif
