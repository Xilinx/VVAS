/*
 * Copyright (C) 2019, Xilinx Inc - All rights reserved
 * Xilinx Lookahead Plugin
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */
#ifndef XLNX_LA_PLG_EXT_H
#define XLNX_LA_PLG_EXT_H

#define XLNX_LA_PLG_NUM_EXT_PARAMS 10

typedef enum
{
  EParamIntraPeriod,
  EParamLADepth,
  EParamEnableHwInBuf,
  EParamSpatialAQMode,
  EParamTemporalAQMode,
  EParamRateControlMode,
  EParamSpatialAQGain,
  EParamNumBFrames,
  EParamDynamicGop,
  EParamCodecType,
  EParamLatencyLogging
} xlnx_la_ext_params_t;

typedef enum
{
  EXlnxAvc,
  EXlnxHevc
} xlnx_codec_type_t;
#endif //XLNX_LA_PLG_EXT_H
