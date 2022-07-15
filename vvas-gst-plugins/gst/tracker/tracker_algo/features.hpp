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

#ifndef _FEATURES_H_
#define _FEATURES_H_

#include <stdio.h>
#include "simd.hpp"

#define CORR_CELL_SIZE 4

void ResizeBilinear(
  const uint8_t *src, size_t srcWidth, size_t srcHeight, size_t srcStride,
  uint8_t *dst, size_t dstWidth, size_t dstHeight, size_t dstStride, size_t channelCount);

void HogExtractFeatures(const uint8_t * src, size_t stride, size_t width,
  size_t height, float * features, int cell_size, int fet_len);
#endif /* _FEATURES_H_ */
