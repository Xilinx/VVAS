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
#ifndef XLNX_AQ_CORE_H
#define XLNX_AQ_CORE_H

#include <inttypes.h>
#include "xlnx_la_plg_ext.h"
#include "xlnx_aq_common.h"

typedef void *xlnx_aq_core_t;

typedef struct
{
  uint64_t frame_num;
  size_t num_blocks;
  const uint16_t *sad;
  const uint32_t *var;
  const uint16_t *act;
  const uint32_t *mv;
} xlnx_frame_stats;

typedef struct xlnx_aq_info
{
  xlnx_aq_buf_t qpmap;
  xlnx_aq_buf_t fsfa;
  uint64_t frame_num;
} xlnx_aq_info_t;

typedef struct xlnx_aq_dump_cfg
{
  uint8_t dumpDeltaQpMap;
  uint8_t dumpDeltaQpMapHex;
  uint8_t dumpBlockSAD;
  uint8_t dumpFrameSAD;
  const char *outPath;
} xlnx_aq_dump_cfg;

xlnx_aq_core_t create_aq_core (aq_config_t * cfg, xlnx_aq_dump_cfg * dumpCfg);
void update_aq_modes (xlnx_aq_core_t handle, aq_config_t * cfg);
void destroy_aq_core (xlnx_aq_core_t handle);
xlnx_status send_frame_stats (xlnx_aq_core_t handle, uint32_t dynamic_gop,
    uint64_t frame_num, xlnx_frame_stats * stats, uint32_t isLastFrame,
    int32_t is_idr);
xlnx_status recv_frame_aq_info (xlnx_aq_core_t handle, xlnx_aq_info_t * vqInfo,
    uint64_t frame_num, int32_t is_idr);
xlnx_status generate_mv_histogram (xlnx_aq_core_t handle, uint64_t frame_num,
    const uint32_t * mvIn, uint32_t isLastFrame,
    int32_t * frame_complexity, int32_t is_idr);
void update_num_b_frames (xlnx_aq_core_t handle, aq_config_t * cfg);

#endif //XLNX_AQ_CORE_H
