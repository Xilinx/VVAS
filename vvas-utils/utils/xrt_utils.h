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
#include <xrt/xrt_device.h>
#include <xrt/xrt_kernel.h>


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
  VVAS_BO_FLAGS_NONE = XRT_BO_FLAGS_NONE,
  VVAS_BO_FLAGS_CACHEABLE = XRT_BO_FLAGS_CACHEABLE,
  VVAS_BO_FLAGS_DEV_ONLY = XRT_BO_FLAGS_DEV_ONLY,
  VVAS_BO_FLAGS_HOST_ONLY = XRT_BO_FLAGS_HOST_ONLY,
  VVAS_BO_FLAGS_P2P = XRT_BO_FLAGS_P2P,
  VVAS_BO_FLAGS_SVM = XRT_BO_FLAGS_SVM
} vvas_bo_flags;

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
typedef void *vvasKernelHandle;
typedef void *vvasRunHandle;
typedef void *vvasBOHandle;

#ifdef __cplusplus
extern "C"
{
#endif

/* Kernel APIs */
/* ========================================================================
Please follow below format specifiers for kernel arguments for "format" argument
"c" : Char argument.
"C" : Unsigned char argument.
"S" : Short argument.
"U" : Unsigned short argument.
"i" : Signed int argument.
"u" : Unsigned int argument.
"l" : Unsigned long long argument.
"d" : long long argument.
"p" : Any pointer argument.
"b" : Buffer Object argument.
"f" : Float argument.
"F" : Double argument.
"s" : If you want to skip the argument.

Ex : For passing 3 arguments of types int, unsigned int and a pointer,
     then the format specifier string would be "iup"
	 
	 If you want to skip the middler argument in the above case, then
	 the format specifier string would be "isp"
==========================================================================
*/

  int32_t
      vvas_xrt_exec_buf (vvasDeviceHandle dev_handle,
      vvasKernelHandle kern_handle, vvasRunHandle * run_handle,
      const char *format, va_list args);

  int32_t
      vvas_xrt_exec_wait (vvasDeviceHandle dev_handle, vvasRunHandle run_handle,
      int32_t timeout);

  void vvas_xrt_free_run_handle (vvasRunHandle run_handle);

/* BO Related APIs */
  uint64_t vvas_xrt_get_bo_phy_addres (vvasBOHandle bo);

  vvasBOHandle vvas_xrt_import_bo (vvasDeviceHandle dev_handle, int32_t fd);

  int32_t vvas_xrt_export_bo (vvasBOHandle bo);

    vvasBOHandle
      vvas_xrt_alloc_bo (vvasDeviceHandle dev_handle, size_t size,
      vvas_bo_flags flags, uint32_t mem_bank);

    vvasBOHandle
      vvas_xrt_create_sub_bo (vvasBOHandle parent, size_t size, size_t offset);

  void vvas_xrt_free_bo (vvasBOHandle bo);

  int vvas_xrt_unmap_bo (vvasBOHandle bo, void *addr);

  void *vvas_xrt_map_bo (vvasBOHandle bo, bool write);

  void vvas_xrt_close_device (vvasDeviceHandle dev_handle);

/* Device APIs */
  int32_t vvas_xrt_close_context (vvasKernelHandle kern_handle);

  int32_t vvas_xrt_sync_bo (vvasBOHandle bo,
      vvas_bo_sync_direction dir, size_t size, size_t offset);

  int32_t vvas_xrt_write_bo (vvasBOHandle bo,
      const void *src, size_t size, size_t seek);

  int32_t vvas_xrt_read_bo (vvasBOHandle bo, void *dst, size_t size,
      size_t skip);

    int32_t
      vvas_xrt_open_context (vvasDeviceHandle handle, uuid_t xclbinId,
      vvasKernelHandle * kernelHandle, char *kernel_name, bool shared);

  int32_t vvas_xrt_open_device (int32_t dev_idx, vvasDeviceHandle * xcl_handle);

  void vvas_xrt_write_reg (vvasKernelHandle kern_handle,
      uint32_t offset, uint32_t data);

  void vvas_xrt_read_reg (vvasKernelHandle kern_handle,
      uint32_t offset, uint32_t * data);

  int vvas_xrt_alloc_xrt_buffer (vvasDeviceHandle dev_handle,
      unsigned int size, vvas_bo_flags bo_flags,
      unsigned int mem_bank, xrt_buffer * buffer);

  void vvas_xrt_free_xrt_buffer (xrt_buffer * buffer);

  int vvas_xrt_download_xclbin (const char *bit,
      vvasDeviceHandle handle, uuid_t * xclbinId);
  int vvas_xrt_get_xclbin_uuid (vvasDeviceHandle handle, uuid_t * xclbinId);

  int vvas_xrt_send_softkernel_command (vvasKernelHandle kern_handle,
      xrt_buffer * sk_ert_buf, unsigned int *payload, unsigned int num_idx,
      unsigned int cu_mask, int timeout);

  int vvas_softkernel_xrt_open_device (int32_t dev_idx, xclDeviceHandle xcl_dev_hdl, vvasDeviceHandle *xcl_handle);
  void vvas_free_ert_xcl_xrt_buffer(xclDeviceHandle xcl_dev_handle, xrt_buffer *buffer);
#ifdef XLNX_PCIe_PLATFORM
  size_t vvas_xrt_get_num_compute_units (const char *xclbin_filename);
  size_t vvas_xrt_get_num_kernels (const char *xclbin_filename);
#endif
#ifdef __cplusplus
}
#endif
#endif
