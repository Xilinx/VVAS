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

#ifndef __IVAS_KERNEL_H__
#define __IVAS_KERNEL_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <jansson.h>

/* Update of this file by the user is not encouraged */
#define MAX_NUM_OBJECT 512
#define MAX_EXEC_WAIT_RETRY_CNT 10
#define VIDEO_MAX_PLANES 4

typedef enum
{
  IVAS_UNKNOWN_MEMORY,
  IVAS_FRAME_MEMORY,
  IVAS_INTERNAL_MEMORY,
} IVASMemoryType;

#ifdef XLNX_PCIe_PLATFORM
typedef enum
{
  IVAS_SYNC_DATA_TO_DEVICE,
  IVAS_SYNC_DATA_FROM_DEVICE
} IVASSyncDataFlag;
#endif

typedef enum
{
  IVAS_VMFT_UNKNOWN = 0,
  IVAS_VFMT_RGBX8,
  IVAS_VFMT_YUVX8,
  IVAS_VFMT_YUYV8,
  IVAS_VFMT_ABGR8,
  IVAS_VFMT_RGBX10,
  IVAS_VFMT_YUVX10,
  IVAS_VFMT_Y_UV8,
  IVAS_VFMT_Y_UV8_420,
  IVAS_VFMT_RGB8,
  IVAS_VFMT_YUVA8,
  IVAS_VFMT_YUV8,
  IVAS_VFMT_Y_UV10,
  IVAS_VFMT_Y_UV10_420,
  IVAS_VFMT_Y8,
  IVAS_VFMT_Y10,
  IVAS_VFMT_ARGB8,
  IVAS_VFMT_BGRX8,
  IVAS_VFMT_UYVY8,
  IVAS_VFMT_BGR8,
  IVAS_VFMT_RGBX12,
  IVAS_VFMT_RGB16
} IVASVideoFormat;

typedef struct _ivas_kernel IVASKernel;
typedef struct _ivas_frame IVASFrame;
typedef struct _ivas_frame_props IVASFrameProps;

typedef int32_t (*IVASBufAllocCBFunc) (IVASKernel * handle,
    IVASFrame * ivas_frame, void *user_data);
typedef void (*IVASBufFreeCBFunc) (IVASKernel * handle, IVASFrame * ivas_frame,
    void *user_data);


typedef int32_t (*IVASKernelInit) (IVASKernel * handle);
typedef int32_t (*IVASKernelDeInit) (IVASKernel * handle);
typedef int32_t (*IVASKernelStartFunc) (IVASKernel * handle, int32_t start,
    IVASFrame * input[MAX_NUM_OBJECT], IVASFrame * output[MAX_NUM_OBJECT]);
typedef int32_t (*IVASKernelDoneFunc) (IVASKernel * handle);

struct _ivas_frame_props
{
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  IVASVideoFormat fmt;
};

struct _ivas_frame
{
  uint32_t bo[VIDEO_MAX_PLANES];
  void *vaddr[VIDEO_MAX_PLANES];
  uint64_t paddr[VIDEO_MAX_PLANES];
  uint32_t size[VIDEO_MAX_PLANES];
  void *meta_data;
  IVASFrameProps props;

  void *app_priv;
  IVASMemoryType mem_type;

  uint32_t n_planes;
};

typedef enum
{
  IVAS_PAD_RIGID,
  IVAS_PAD_FLEXIBLE,
  IVAS_PAD_DEFAULT
} padsnature;

typedef struct caps
{
  uint8_t range_height;
  uint32_t lower_height;
  uint32_t upper_height;

  uint8_t range_width;
  uint32_t lower_width;
  uint32_t upper_width;

  uint8_t num_fmt;
  IVASVideoFormat *fmt;
} kernelcaps;

typedef struct kernelpad
{
  uint8_t nu_caps;
  kernelcaps **kcaps;
} kernelpads;

typedef struct alignparm
{
  unsigned int stride_align;
  unsigned int height_align;
} alignparms;

typedef struct ivaspads
{
  padsnature nature;
  uint8_t nu_sinkpad;
  uint8_t nu_srcpad;
  alignparms sinkpad_align;
  alignparms srcpad_align;
  kernelpads **sinkpads;
  kernelpads **srcpads;
} ivaspads;

typedef struct buffer
{
  unsigned int bo;
  void *user_ptr;
  uint64_t phy_addr;
  unsigned int size;
} xrt_buffer;

struct _ivas_kernel
{
  void *xcl_handle;
  uint32_t cu_idx;
  json_t *kernel_config;
  json_t *kernel_dyn_config;
  void *kernel_priv;
  xrt_buffer *ert_cmd_buf;
  size_t min_offset;
  size_t max_offset;
  IVASBufAllocCBFunc alloc_func;
  IVASBufFreeCBFunc free_func;
  void *cb_user_data;
  ivaspads *padinfo;
#ifdef XLNX_PCIe_PLATFORM
  uint32_t is_softkernel;
#endif
  uint8_t is_multiprocess;
};


IVASFrame *ivas_alloc_buffer (IVASKernel * handle, uint32_t size,
    IVASMemoryType mem_type, IVASFrameProps * props);
void ivas_free_buffer (IVASKernel * handle, IVASFrame * ivas_frame);
void ivas_register_write (IVASKernel * handle, void *src, size_t size,
    size_t offset);
void ivas_register_read (IVASKernel * handle, void *src, size_t size,
    size_t offset);
int32_t ivas_kernel_start (IVASKernel * handle);
int32_t ivas_kernel_done (IVASKernel * handle, int32_t timeout);

#ifdef XLNX_PCIe_PLATFORM

int32_t ivas_sync_data (IVASKernel * handle, IVASSyncDataFlag flag,
    IVASFrame * frame);
#endif

/***************** ivas caps utils ********************/
typedef enum
{
  SINK,
  SRC
} paddir;

bool ivas_caps_set_pad_nature (IVASKernel * handle, padsnature nature);
padsnature ivas_caps_get_pad_nature (IVASKernel * handle);
bool ivas_caps_set_stride_align (IVASKernel * handle, paddir dir,
    unsigned int stride_align);
bool ivas_caps_set_sink_stride_align (IVASKernel * handle,
    unsigned int stride_align);
bool ivas_caps_set_src_stride_align (IVASKernel * handle,
    unsigned int stride_align);
unsigned int ivas_caps_get_stride_align (IVASKernel * handle, paddir dir);
unsigned int ivas_caps_get_sink_stride_align (IVASKernel * handle);
unsigned int ivas_caps_get_src_stride_align (IVASKernel * handle);
bool ivas_caps_set_height_align (IVASKernel * handle, paddir dir,
    unsigned int height_align);
bool ivas_caps_set_sink_height_align (IVASKernel * handle,
    unsigned int height_align);
bool ivas_caps_set_src_height_align (IVASKernel * handle,
    unsigned int height_align);
unsigned int ivas_caps_get_height_align (IVASKernel * handle, paddir dir);
unsigned int ivas_caps_get_sink_height_align (IVASKernel * handle);
unsigned int ivas_caps_get_src_height_align (IVASKernel * handle);
kernelcaps *ivas_caps_new (uint8_t range_height, uint32_t lower_height,
    uint32_t upper_height, uint8_t range_width, uint32_t lower_width,
    uint32_t upper_width, ...);
bool ivas_caps_add (IVASKernel * handle, kernelcaps * kcaps, paddir dir,
    int sinkpad_num);
bool ivas_caps_add_to_sink (IVASKernel * handle, kernelcaps * kcaps,
    int sinkpad_num);
bool ivas_caps_add_to_src (IVASKernel * handle, kernelcaps * kcaps,
    int srcpad_num);
int ivas_caps_get_num_caps (IVASKernel * handle, int sinkpad_num);
int ivas_caps_get_num_sinkpads (IVASKernel * handle);
int ivas_caps_get_num_srcpads (IVASKernel * handle);
void ivas_caps_free (IVASKernel * handle);
void ivas_caps_print (IVASKernel * handle);

#endif
