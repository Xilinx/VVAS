/*
 * Copyright (C) 2022, Xilinx Inc - All rights reserved
 * Xilinx Resource Manger U30 Encoder Plugin
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
#ifndef _CODEC_ENCODER_XRM_PLG_U30_HPP_
#define _CODEC_ENCODER_XRM_PLG_U30_HPP_

//#include <string.h>
#include <stdio.h>
#include <syslog.h>
#include <vector>
#include <tuple>
#include <string>

#include <xrm.h>
#include <xrm_error.h>
#include <xrm_limits.h>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/map.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree_serialization.hpp>

#define XRM_PLUGIN_U30_ENC_ID 4
#define MAX_CH_SIZE 4096
#define MAX_OUT_ELEMENTS 64
#define U30_ENC_MAXCAPACITY (1920*1080*60*4)
#define U30_LA_MAXCAPACITY (1920*1080*60*4)

namespace pt = boost::property_tree;
using namespace std;

#ifdef __cplusplus
extern "C" {
#endif

struct FrameRate
{
   int32_t numerator;
   int32_t denominator;
};

struct Resolution
{
   int32_t width;
   int32_t height;
   FrameRate frame_rate;
};

struct ResourceData
{
   string               function;
   string               format;
   int32_t              channel_load;
   int32_t              lookahead_load;
   Resolution           in_res;
   vector<Resolution>   out_res;
};

struct ParamsData
{
   int32_t             job_count;
};

int32_t xrmU30EncPlugin_api_version(void);
int32_t xrmU30EncPlugin_get_plugin_id(void);
int32_t xrmU30EncPlugin_calcPercent(xrmPluginFuncParam* param);
uint32_t extData[4];

xrmPluginData xrmU30EncPlugin{xrmU30EncPlugin_get_plugin_id, xrmU30EncPlugin_api_version, xrmU30EncPlugin_calcPercent, NULL, NULL,NULL,NULL,NULL,NULL,NULL,extData[4] };

#ifdef __cplusplus
}
#endif

#endif //_CODEC_ENCODER_XRM_PLG_U30_HPP_
