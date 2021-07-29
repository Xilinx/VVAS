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

/* This is Sample file for Csim and cosim */
#ifndef _V_MULTI_SCALER_CONFIG_H_
#define _V_MULTI_SCALER_CONFIG_H_

/* Constant parameters defined for this IP */
#define HSC_BILINEAR                0
#define HSC_BICUBIC                 1
#define HSC_POLYPHASE               2
#define	AXIMM_INTERFACE             1
#define	AXI_STREAM_INTERFACE        0

#define CPW                         3
#define OPMODE                      1
#define NORMALIZATION               1

/* Below parameters are coming from user selection */
#define HSC_SAMPLES_PER_CLOCK       2  // 1, 2, 4
#define HSC_MAX_WIDTH               1920           // Determines BRAM usage
#define HSC_MAX_HEIGHT              1080           // No impact on resources
#define HSC_BITS_PER_COMPONENT      8     // 8, 10
#define HSC_SCALE_MODE              0         // 0 - Bilinear 1 - Bicubic  2 - Polyphase
#if (HSC_SCALE_MODE==HSC_BILINEAR)
#define HSC_TAPS                    2               // Fixed to 2
#elif (HSC_SCALE_MODE==HSC_BICUBIC)
#define HSC_TAPS                    4               // Fixed to 4
#elif(HSC_SCALE_MODE==HSC_POLYPHASE)
#define HSC_TAPS                    6   // 6, 8, 10, 12
#endif

#define MAX_OUTS                    1

#define HAS_RGBX8_YUVX8         0
#define HAS_YUYV8               0
#define HAS_RGBA8_YUVA8         0
#define HAS_RGBX10_YUVX10       0
#define HAS_Y_UV8_Y_UV8_420     1
#define HAS_RGB8_YUV8           0
#define HAS_Y_UV10_Y_UV10_420   0
#define HAS_Y8                  0
#define HAS_Y10                 0
#define HAS_BGRA8               0
#define HAS_BGRX8               0
#define HAS_UYVY8               0
#define HAS_BGR8                1
#define HAS_R_G_B8              0
#define HAS_Y_U_V8_420          0

/* Memory Ports */
#define AXIMM_NUM_OUTSTANDING       4
#define AXIMM_BURST_LENGTH          16

/* Constant parameter coming from coreinfo.yml */
#define HSC_PHASE_SHIFT         6        // Number of bits used for phase
#define USE_URAM                0
#define INPUT_INTERFACE		AXIMM_INTERFACE
#define	OUTPUT_INTERFACE	AXIMM_INTERFACE

#endif // _V_MULTI_SCALER_CONFIG_H_
