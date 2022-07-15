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

#ifndef __VVAS_KERNEL_H__
#define __VVAS_KERNEL_H__

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
#define DEFAULT_MEM_BANK 0

typedef enum
{
  VVAS_UNKNOWN_MEMORY,
  VVAS_FRAME_MEMORY,
  VVAS_INTERNAL_MEMORY,
} VVASMemoryType;

#ifdef XLNX_PCIe_PLATFORM
typedef enum
{
  VVAS_SYNC_DATA_TO_DEVICE,
  VVAS_SYNC_DATA_FROM_DEVICE
} VVASSyncDataFlag;
#endif

typedef enum
{
  VVAS_VMFT_UNKNOWN = 0,
  VVAS_VFMT_RGBX8,
  VVAS_VFMT_YUVX8,
  VVAS_VFMT_YUYV8,
  VVAS_VFMT_ABGR8,
  VVAS_VFMT_RGBX10,
  VVAS_VFMT_YUVX10,
  VVAS_VFMT_Y_UV8,
  VVAS_VFMT_Y_UV8_420,
  VVAS_VFMT_RGB8,
  VVAS_VFMT_YUVA8,
  VVAS_VFMT_YUV8,
  VVAS_VFMT_Y_UV10,
  VVAS_VFMT_Y_UV10_420,
  VVAS_VFMT_Y8,
  VVAS_VFMT_Y10,
  VVAS_VFMT_ARGB8,
  VVAS_VFMT_BGRX8,
  VVAS_VFMT_UYVY8,
  VVAS_VFMT_BGR8,
  VVAS_VFMT_RGBX12,
  VVAS_VFMT_RGB16,
  VVAS_VFMT_I420
} VVASVideoFormat;

typedef struct _vvas_kernel VVASKernel;
typedef struct _vvas_frame VVASFrame;
typedef struct _vvas_frame_props VVASFrameProps;

typedef int32_t (*VVASBufAllocCBFunc) (VVASKernel * handle,
    VVASFrame * vvas_frame, void *user_data);
typedef void (*VVASBufFreeCBFunc) (VVASKernel * handle, VVASFrame * vvas_frame,
    void *user_data);


typedef int32_t (*VVASKernelInit) (VVASKernel * handle);
typedef int32_t (*VVASKernelDeInit) (VVASKernel * handle);
typedef int32_t (*VVASKernelStartFunc) (VVASKernel * handle, int32_t start,
    VVASFrame * input[MAX_NUM_OBJECT], VVASFrame * output[MAX_NUM_OBJECT]);
typedef int32_t (*VVASKernelDoneFunc) (VVASKernel * handle);

struct _vvas_frame_props
{
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  VVASVideoFormat fmt;
};

struct _vvas_frame
{
  void* bo[VIDEO_MAX_PLANES];
  void *vaddr[VIDEO_MAX_PLANES];
  uint64_t paddr[VIDEO_MAX_PLANES];
  uint32_t size[VIDEO_MAX_PLANES];
  void *meta_data;
  VVASFrameProps props;

  void *app_priv;
  VVASMemoryType mem_type;

  uint32_t n_planes;
};

typedef enum
{
  VVAS_PAD_RIGID,
  VVAS_PAD_FLEXIBLE,
  VVAS_PAD_DEFAULT
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
  VVASVideoFormat *fmt;
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

typedef struct vvaspads
{
  padsnature nature;
  uint8_t nu_sinkpad;
  uint8_t nu_srcpad;
  alignparms sinkpad_align;
  alignparms srcpad_align;
  kernelpads **sinkpads;
  kernelpads **srcpads;
} vvaspads;

typedef struct buffer
{
  void* bo;
  void *user_ptr;
  uint64_t phy_addr;
  unsigned int size;
} xrt_buffer;

struct _vvas_kernel
{
  void* dev_handle;
  void* kern_handle;
  void* run_handle;
  uint32_t cu_idx;
  json_t *kernel_config;
  json_t *kernel_dyn_config;
  void *kernel_priv;
  size_t min_offset;
  size_t max_offset;
  VVASBufAllocCBFunc alloc_func;
  VVASBufFreeCBFunc free_func;
  void *cb_user_data;
  vvaspads *padinfo;
  size_t kernel_batch_sz; /* supported by kernel */
#ifdef XLNX_PCIe_PLATFORM
  uint32_t is_softkernel;
#endif
  uint8_t is_multiprocess;
  uint8_t  *name;
  uint16_t in_mem_bank;
  uint16_t out_mem_bank;
};


VVASFrame *vvas_alloc_buffer (VVASKernel * handle, uint32_t size,
    VVASMemoryType mem_type, uint16_t mem_bank, VVASFrameProps * props);
void vvas_free_buffer (VVASKernel * handle, VVASFrame * vvas_frame);
void vvas_register_write (VVASKernel * handle, void *src, size_t size,
    size_t offset);
void vvas_register_read (VVASKernel * handle, void *src, size_t size,
    size_t offset);
/* ========================================================================
Please follow below format specifiers for kernel arguments for "format" argument

"i" : Signed int argument.
"u" : Unsigned int argument.
"l" : Unsigned long long argument.
"p" : Any pointer argument.
"b" : Buffer Object argument.
"s" : If you want to skip the argument.

Ex : For passing 3 arguments of types int, unsigned int and a pointer,
     then the format specifier string would be "iup"
	 
	 If you want to skip the middler argument in the above case, then
	 the format specifier string would be "isp"
==========================================================================
*/
int32_t vvas_kernel_start (VVASKernel * handle, const char *format, ...);
int32_t vvas_kernel_done (VVASKernel * handle, int32_t timeout);

#ifdef XLNX_PCIe_PLATFORM

int32_t vvas_sync_data (VVASKernel * handle, VVASSyncDataFlag flag,
    VVASFrame * frame);
#endif

/***************** vvas caps utils ********************/
typedef enum
{
  SINK,
  SRC
} paddir;

bool vvas_caps_set_pad_nature (VVASKernel * handle, padsnature nature);
padsnature vvas_caps_get_pad_nature (VVASKernel * handle);
size_t vvas_kernel_get_batch_size (VVASKernel * handle);
bool vvas_caps_set_stride_align (VVASKernel * handle, paddir dir,
    unsigned int stride_align);
bool vvas_caps_set_sink_stride_align (VVASKernel * handle,
    unsigned int stride_align);
bool vvas_caps_set_src_stride_align (VVASKernel * handle,
    unsigned int stride_align);
unsigned int vvas_caps_get_stride_align (VVASKernel * handle, paddir dir);
unsigned int vvas_caps_get_sink_stride_align (VVASKernel * handle);
unsigned int vvas_caps_get_src_stride_align (VVASKernel * handle);
bool vvas_caps_set_height_align (VVASKernel * handle, paddir dir,
    unsigned int height_align);
bool vvas_caps_set_sink_height_align (VVASKernel * handle,
    unsigned int height_align);
bool vvas_caps_set_src_height_align (VVASKernel * handle,
    unsigned int height_align);
unsigned int vvas_caps_get_height_align (VVASKernel * handle, paddir dir);
unsigned int vvas_caps_get_sink_height_align (VVASKernel * handle);
unsigned int vvas_caps_get_src_height_align (VVASKernel * handle);
kernelcaps *vvas_caps_new (uint8_t range_height, uint32_t lower_height,
    uint32_t upper_height, uint8_t range_width, uint32_t lower_width,
    uint32_t upper_width, ...);
bool vvas_caps_add (VVASKernel * handle, kernelcaps * kcaps, paddir dir,
    int sinkpad_num);
bool vvas_caps_add_to_sink (VVASKernel * handle, kernelcaps * kcaps,
    int sinkpad_num);
bool vvas_caps_add_to_src (VVASKernel * handle, kernelcaps * kcaps,
    int srcpad_num);
int vvas_caps_get_num_caps (VVASKernel * handle, int sinkpad_num);
int vvas_caps_get_num_sinkpads (VVASKernel * handle);
int vvas_caps_get_num_srcpads (VVASKernel * handle);
void vvas_caps_free (VVASKernel * handle);
void vvas_caps_print (VVASKernel * handle);

#endif
