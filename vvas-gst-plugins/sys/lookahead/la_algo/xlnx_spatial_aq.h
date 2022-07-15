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
#ifndef XLNX_SPATIAL_AQ_H
#define XLNX_SPATIAL_AQ_H

#include <inttypes.h>
#include "xlnx_la_plg_ext.h"
#include "xlnx_aq_common.h"


typedef void *xlnx_spatial_aq_t;
typedef struct spatial_qpmap
{
  float *fPtr;
  size_t size;
  uint64_t frame_num;
} spatial_qpmap_t;

xlnx_spatial_aq_t xlnx_spatial_create (aq_config_t * cfg);
void update_aq_gain (xlnx_spatial_aq_t sp, aq_config_t * cfg);
void xlnx_spatial_destroy (xlnx_spatial_aq_t sp);
xlnx_status xlnx_spatial_gen_qpmap (xlnx_spatial_aq_t sp,
    const uint32_t * var_energy_map, const uint16_t * act_energy_map,
    uint64_t frame_num, uint32_t * frame_activity);
xlnx_status xlnx_spatial_recv_qpmap (xlnx_spatial_aq_t sp,
    spatial_qpmap_t * qpmap);
xlnx_status xlnx_spatial_release_qpmap (xlnx_spatial_aq_t sp,
    spatial_qpmap_t * qpmap);
int xlnx_spatial_is_ready (xlnx_spatial_aq_t sp);
int xlnx_spatial_frame_activity (aq_config_t * cfg,
    const uint16_t * act_energy_map, uint32_t * fa);

#endif //XLNX_SPATIAL_AQ_H
