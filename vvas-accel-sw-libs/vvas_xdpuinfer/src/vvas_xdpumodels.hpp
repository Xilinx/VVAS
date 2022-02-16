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
#ifndef DPUMODELS_H
#define DPUMODELS_H

#if 0
enum
{
  VVAS_XCLASS_YOLOV3,
  VVAS_XCLASS_FACEDETECT,
  VVAS_XCLASS_CLASSIFICATION,
  VVAS_XCLASS_SSD,
  VVAS_XCLASS_REID,
  VVAS_XCLASS_REFINEDET,
  VVAS_XCLASS_TFSSD,
  VVAS_XCLASS_YOLOV2,
  VVAS_XCLASS_SEGMENTATION,

  VVAS_XCLASS_NOTFOUND
};
#endif

static const char *vvas_xmodelclass[VVAS_XCLASS_NOTFOUND + 1] = {
  [VVAS_XCLASS_YOLOV3] = "YOLOV3",
  [VVAS_XCLASS_FACEDETECT] = "FACEDETECT",
  [VVAS_XCLASS_CLASSIFICATION] = "CLASSIFICATION",
  [VVAS_XCLASS_SSD] = "SSD",
  [VVAS_XCLASS_REID] = "REID",
  [VVAS_XCLASS_REFINEDET] = "REFINEDET",
  [VVAS_XCLASS_TFSSD] = "TFSSD",
  [VVAS_XCLASS_YOLOV2] = "YOLOV2",
  [VVAS_XCLASS_SEGMENTATION] = "SEGMENTATION",
  [VVAS_XCLASS_PLATEDETECT] = "PLATEDETECT",
  [VVAS_XCLASS_PLATENUM] = "PLATENUM",

  /* Add model above this */
  [VVAS_XCLASS_NOTFOUND] = ""
};

#endif
