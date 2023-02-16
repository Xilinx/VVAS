/*
 * Copyright 2020 - 2022 Xilinx, Inc.
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
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

#ifndef __VVAS_META_H__
#define __VVAS_META_H__

/* Update of this file by the user is not encouraged */
#include <stdint.h>

#define MAX_NAME_LENGTH 256

typedef struct _VvasColorMetadata {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t alpha;
} VvasColorMetadata;

typedef struct _VvasTextMetadata {

  int8_t disp_text[MAX_NAME_LENGTH];

  VvasColorMetadata text_color;
} VvasTextMetadata;


typedef struct _XVABBoxMeta {
  float xmin;
  float ymin;
  float xmax;
  float ymax;

  VvasColorMetadata box_color;
} VvasBBoxMetadata;

typedef struct _VvasObjectMetadata {
  int32_t obj_id;
  int8_t obj_class[MAX_NAME_LENGTH];
  double obj_prob;
  VvasBBoxMetadata bbox_meta;

  VvasTextMetadata text_meta;

  GList *obj_list;
} VvasObjectMetadata;

#endif
