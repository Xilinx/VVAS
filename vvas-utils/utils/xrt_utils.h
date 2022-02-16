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

#ifndef __XRT_UTILS_H__
#define __XRT_UTILS_H__

/* Update of this file by the user is not encouraged */
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <vvas/vvas_kernel.h>


//#include "xclhal2.h"
#include <uuid/uuid.h>
#ifdef XLNX_PCIe_PLATFORM
#include <xrt.h>
#include <ert.h>
#else
#include <xrt/xrt.h>
#include <xrt/ert.h>
#endif
#define __COUNT                 (1024)
#define DATA_SIZE               (__COUNT * sizeof(int))

typedef enum
{
  VVAS_BO_SYNC_BO_TO_DEVICE = XCL_BO_SYNC_BO_TO_DEVICE,
  VVAS_BO_SYNC_BO_FROM_DEVICE = XCL_BO_SYNC_BO_FROM_DEVICE,
  VVAS_BO_SYNC_BO_GMIO_TO_AIE = XCL_BO_SYNC_BO_GMIO_TO_AIE,
  VVAS_BO_SYNC_BO_AIE_TO_GMIO = XCL_BO_SYNC_BO_AIE_TO_GMIO
} vvas_bo_sync_direction;

typedef enum
{
  VVAS_BO_SHARED_VIRTUAL = XCL_BO_SHARED_VIRTUAL,
  VVAS_BO_SHARED_PHYSICAL = XCL_BO_SHARED_PHYSICAL,
  VVAS_BO_MIRRORED_VIRTUAL = XCL_BO_MIRRORED_VIRTUAL,
  VVAS_BO_DEVICE_RAM = XCL_BO_DEVICE_RAM,
  VVAS_BO_DEVICE_BRAM = XCL_BO_DEVICE_BRAM,
  VVAS_BO_DEVICE_PREALLOCATED_BRAM = XCL_BO_DEVICE_PREALLOCATED_BRAM,
} vvas_bo_kind;

#define VVAS_BO_FLAGS_MEMIDX_MASK XRT_BO_FLAGS_MEMIDX_MASK
#define VVAS_BO_FLAGS_NONE XCL_BO_FLAGS_NONE
#define VVAS_BO_FLAGS_CACHEABLE XCL_BO_FLAGS_CACHEABLE
#define VVAS_BO_FLAGS_KERNBUF XCL_BO_FLAGS_KERNBUF
#define VVAS_BO_FLAGS_SGL XCL_BO_FLAGS_SGL
#define VVAS_BO_FLAGS_SVM XCL_BO_FLAGS_SVM
#define VVAS_BO_FLAGS_DEV_ONLY XCL_BO_FLAGS_DEV_ONLY
#define VVAS_BO_FLAGS_HOST_ONLY XCL_BO_FLAGS_HOST_ONLY
#define VVAS_BO_FLAGS_P2P XCL_BO_FLAGS_P2P
#define VVAS_BO_FLAGS_EXECBUF XCL_BO_FLAGS_EXECBUF

typedef void *vvasDeviceHandle;


/* Kernel APIs */
int32_t vvas_xrt_exec_buf (vvasDeviceHandle dev_handle, uint32_t bo);

int32_t vvas_xrt_exec_wait (vvasDeviceHandle dev_handle, int32_t timeout);


/* BO Related APIs */
int32_t
vvas_xrt_get_bo_properties (vvasDeviceHandle dev_handle, uint32_t bo,
    struct xclBOProperties *prop);

uint32_t vvas_xrt_import_bo (vvasDeviceHandle dev_handle, int32_t fd,
    uint32_t flags);

int32_t vvas_xrt_export_bo (vvasDeviceHandle dev_handle, uint32_t bo);

uint32_t
vvas_xrt_alloc_bo (vvasDeviceHandle dev_handle, size_t size, int32_t unused,
    uint32_t flags);

void vvas_xrt_free_bo (vvasDeviceHandle dev_handle, uint32_t bo);

int vvas_xrt_unmap_bo (vvasDeviceHandle dev_handle, uint32_t bo, void *addr);

void *vvas_xrt_map_bo (vvasDeviceHandle dev_handle, uint32_t bo, bool write);

void vvas_xrt_close_device (vvasDeviceHandle dev_handle);

/* Device APIs */
int32_t
vvas_xrt_close_context (vvasDeviceHandle dev_handle, uuid_t xclbinId,
    int32_t cu_idx);

int32_t vvas_xrt_sync_bo (vvasDeviceHandle dev_handle, uint32_t bo,
    vvas_bo_sync_direction dir, size_t size, size_t offset);

int32_t vvas_xrt_write_bo (vvasDeviceHandle dev_handle, uint32_t bo,
    const void *src, size_t size, size_t seek);

int32_t vvas_xrt_read_bo (vvasDeviceHandle dev_handle, uint32_t bo, void *dst,
    size_t size, size_t skip);

int32_t vvas_xrt_open_context (vvasDeviceHandle dev_handle, uuid_t xclbinId,
    int32_t cu_idx, bool shared);

uint32_t vvas_xrt_probe (void);

int32_t vvas_xrt_open_device (int32_t dev_idx, vvasDeviceHandle * dev_handle);

uint32_t vvas_xrt_ip_name2_index (vvasDeviceHandle dev_handle, char *kernel_name);

int vvas_xrt_alloc_xrt_buffer (vvasDeviceHandle handle, unsigned int size,
    vvas_bo_kind bo_kind, unsigned flags, xrt_buffer * buffer);

void vvas_xrt_free_xrt_buffer (vvasDeviceHandle handle, xrt_buffer * buffer);

int vvas_xrt_download_xclbin (const char *bit, unsigned deviceIndex,
    const char *halLog, vvasDeviceHandle handle, uuid_t * xclbinId);

int vvas_xrt_send_softkernel_command (vvasDeviceHandle handle,
    xrt_buffer * sk_buf, unsigned int *payload, unsigned int num_idx,
    unsigned int cu_mask, int timeout);
#ifdef XLNX_PCIe_PLATFORM
size_t utils_get_num_compute_units (const char *xclbin_filename);
size_t utils_get_num_kernels (const char *xclbin_filename);
#endif
#endif
