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
#ifndef XLNX_LA_DEFINES_H
#define XLNX_LA_DEFINES_H

#define XLNX_AQ_NONE                 0
#define XLNX_AQ_SPATIAL_AUTOVARIANCE 1
#define XLNX_AQ_SPATIAL_ACTIVITY     2
#define XLNX_AQ_TEMPORAL_LINEAR      1

#define BLOCK_WIDTH 4
#define BLOCK_HEIGHT 4

#define XLNX_OUT_STATS_HW_BUF_Q_SIZE 2
#define LINMEM_ALIGN_4K    4096

#define PRINT_FRAME_SAD 0       // Print frame sad into csv
#define PRINT_BLOCK_SAD 0       // Print block sad for each frame
#define PRINT_FRAME_DELTAQP_MAP 0       // Print in decimal delta qp map for all the blocks in a frame
#define PRINT_HEX_FRAME_DELTAQP_MAP 0   // Print in hex delta qp map for all the blocks in a frame
#define MULTI_CHANNEL_DUMP 1
//#define ENABLE_YUV_DUMP

#define MAX_HOR_RES                    1920
#define MAX_VERT_RES                   1080
#define XLNX_DEFAULT_INTRA_PERIOD      120
#define XLNX_DEFAULT_AQ_MODE           0
#define XLNX_DEFAULT_ENABLE_HW_IN_BUF  0
#define XLNX_DEFAULT_RATE_CONTROL_MODE 0
#define XLNX_DEFAULT_LA_DEPTH          20
#define XLNX_DEFAULT_SPATIAL_AQ_MODE   0
#define XLNX_DEFAULT_SPATIAL_AQ_GAIN   50
#define XLNX_DEFAULT_TEMPORAL_AQ_MODE  0
#define XLNX_DEFAULT_NUM_OF_B_FRAMES   2
#define XLNX_DEFAULT_DYNAMIC_GOP       0
#define XLNX_DEFAULT_CODEC_TYPE        EXlnxAvc
#define XLNX_DEFAULT_LATENCY_LOGGING   0

#define XLNX_DYNAMIC_GOP_INTERVAL      4
#define XLNX_DYNAMIC_GOP_CACHE         (XLNX_DEFAULT_LA_DEPTH / XLNX_DYNAMIC_GOP_INTERVAL) + 1

#define LOW_MOTION                     0
#define MEDIUM_MOTION                  1
#define HIGH_MOTION                    2

#define XLNX_MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#define VCU_STRIDE_ALIGN 32
#define VCU_HEIGHT_ALIGN 32

#endif //XLNX_LA_DEFINES_H
