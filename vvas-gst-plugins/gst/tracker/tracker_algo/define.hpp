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

#pragma once

#ifndef _DEFINE_HPP_
#define _DEFINE_HPP_

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#include "tracker.hpp"

#define DRAW_FILLED -1
#define ANTI_ALIAS 16

#define PI    3.1415926535897932384626433832795

#if defined(_MSC_VER)
#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif
#endif

typedef struct {
  short width;
  short height;
  float *data;
} fMat;

typedef struct{
  int x;
  int y;
  int width;
  int height;
} Rect;

typedef struct {
  float x;
  float y;
  float width;
  float height;
  unsigned int map_id; //to map prediction id
} Rectf;


typedef struct {
  int width;
  int height;
} Size;

typedef struct {
  int64_t width;
  int64_t height;
} Size2l;

typedef struct {
  float width;
  float height;
} Size2f;

typedef struct {
  double width;
  double height;
} Size2d;

typedef struct {
  float x;
  float y;
} Point2f;

typedef struct {
  double x;
  double y;
} Point2d;

typedef struct {
  int x;
  int y;
} Point;

typedef struct {
  int64_t x;
  int64_t y;
} Point2l;

#endif /* _DEFINE_HPP_ */
