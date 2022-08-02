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

/* Update of this file by the user is not encouraged */
#include <assert.h>
#include "xrt_utils.h"
#include <unistd.h>
#include <iostream>
#include <cstdarg>

#ifdef XLNX_PCIe_PLATFORM
#include "xclbin.h"
#include <experimental/xrt_xclbin.h>
#endif

#define ERROR_PRINT(...) {\
  do {\
    printf("[%s:%d] ERROR : ",__func__, __LINE__);\
    printf(__VA_ARGS__);\
    printf("\n");\
  } while(0);\
}

#undef DEBUG_XRT_UTILS

#ifdef DEBUG_XRT_UTILS
#define DEBUG_PRINT(...) {\
  do {\
    printf("[%s:%d] ",__func__, __LINE__);\
    printf(__VA_ARGS__);\
    printf("\n");\
  } while(0);\
}
#else
#define DEBUG_PRINT(...) ((void)0)
#endif

struct sk_device_info
{
  int pid;
  void *device_handle;
  uint32_t dev_index;
};

#define MAX_DEVICES   (32)
#define P2ROUNDUP(x, align) (-(-(x) & -(align)))
#define WAIT_TIMEOUT 1000       // 1sec
#define ERT_START_CMD_PAYLOAD_SIZE ((1024 * sizeof(unsigned int)) - 2)  // ERT cmd can carry max 4088 bytes
#define MEM_BANK 0

extern "C"
{
/* Kernel APIs */
int32_t
  vvas_xrt_exec_buf (vvasDeviceHandle dev_handle,
  vvasKernelHandle kern_handle, vvasRunHandle* run_handle,
  const char *format, va_list args)
{
  xrt::kernel * kernel = (xrt::kernel *) kern_handle;
  auto kernel_hand = *kernel;
  xrt::run* run = new xrt::run (kernel_hand);
  int i = 0, arg_index = 0;
  char c;

  while((c=format[i++]) != '\0')
  {
    switch(c) {
      case 'i':
      {
        int i_value;
        i_value = va_arg(args, int);
        run->set_arg(arg_index, i_value);
        arg_index++;
      }
      break;
      case 'u':
      {
        unsigned int u_value;
        u_value = va_arg(args, unsigned int);
        run->set_arg(arg_index, u_value);
        arg_index++;
      }
      break;
      case 'f':
      {
        float f_value;
        /* va_arg doesn't support float.
         * If float is passed it will be automatically promoted to double */
        f_value = (float) va_arg(args, double);
        run->set_arg(arg_index, f_value);
        arg_index++;
      }
      break;
      case 'F':
      {
        double d_value;
        d_value = va_arg(args, double);
        run->set_arg(arg_index, d_value);
        arg_index++;
      }
      break;
      case 'c':
      {
        char c_value;
        c_value = (char)va_arg(args, int);
        run->set_arg(arg_index, c_value);
        arg_index++;
      }
      break;
      case 'C':
      {
        unsigned char c_value;
        c_value = (unsigned char)va_arg(args, unsigned int);
        run->set_arg(arg_index, c_value);
        arg_index++;
      }
      break;
      case 'S':
      {
        short s_value;
        s_value = (short)va_arg(args, int);
        run->set_arg(arg_index, s_value);
        arg_index++;
      }
      break;
      case 'U':
      {
        unsigned short s_value;
        s_value = (unsigned short)va_arg(args, unsigned int);
        run->set_arg(arg_index, s_value);
        arg_index++;
      }
      break;
      case 'l':
      {
        unsigned long long l_value;
        l_value = va_arg(args, unsigned long long);
        run->set_arg(arg_index, l_value);
        arg_index++;
      }
      break;
      case 'd':
      {
        long long d_value;
        d_value = va_arg(args, long long);
        run->set_arg(arg_index, d_value);
        arg_index++;
      }
      break;
      case 'p':
      {
        void* p_value;
        p_value = va_arg(args, void*);
        if(p_value) {
          run->set_arg(arg_index, p_value);
        }
        arg_index++;
      }
      break;
      case 's':
      {
        arg_index++;
      }
      break;
      case 'b':
      {
        void* b_value = NULL;
        b_value = va_arg(args, void*);

        if(b_value) {
          run->set_arg(arg_index, *((xrt::bo *)b_value));
        }
        arg_index++;
      }
      break;
      default:
        ERROR_PRINT ("Wrong format specifier");
        delete run;
        return -1;
    }
  }
  run->start();

  *run_handle = run;
  return 0;
}

int32_t
vvas_xrt_exec_wait (vvasDeviceHandle dev_handle, vvasRunHandle run_handle,
                    int32_t timeout)
{
  xrt::run* handle = (xrt::run*) run_handle;
  int iret = 0;

  iret = handle->wait(timeout);

  return iret;
}

void vvas_xrt_free_run_handle (vvasRunHandle run_handle)
{
  xrt::run* handle = (xrt::run*) run_handle;

  delete handle;
  run_handle = NULL;

  return;
}

/* BO Related APIs */
uint64_t
vvas_xrt_get_bo_phy_addres (vvasBOHandle bo)
{
  xrt::bo* bo_handle = (xrt::bo *) bo;
  
  return bo_handle->address();
}

vvasBOHandle
vvas_xrt_import_bo (vvasDeviceHandle dev_handle, int32_t fd) 
{
  xrt::device * device = (xrt::device *) dev_handle;
  
  auto bo_handle = new xrt::bo (*device, fd);

  return bo_handle;
}

int32_t vvas_xrt_export_bo (vvasBOHandle bo)
{
  xrt::bo * bo_handle = (xrt::bo *) bo;

  return bo_handle->export_buffer ();
}

vvasBOHandle
vvas_xrt_alloc_bo (vvasDeviceHandle dev_handle, size_t size,
                   vvas_bo_flags flags, uint32_t mem_bank)
{
  xrt::device * device = (xrt::device *) dev_handle;

  auto bo_handle = new xrt::bo (*device, size, flags, mem_bank);
  
  return (vvasBOHandle) bo_handle;
}

vvasBOHandle
vvas_xrt_create_sub_bo (vvasBOHandle parent, size_t size, size_t offset)
{
  xrt::bo *bo_handle = (xrt::bo *) parent;

  auto sub_bo_handle = new xrt::bo (*bo_handle, size, offset);

  return sub_bo_handle;
}

void vvas_xrt_free_bo (vvasBOHandle bo)
{
  xrt::bo * bo_handle = (xrt::bo *) bo;

  if (bo_handle) {
    delete bo_handle;
    bo = NULL;
  }

  return;
}

int
vvas_xrt_unmap_bo (vvasBOHandle bo, void *addr)
{
  /* As of now there is no API in XRT Native for unmout. Just keeping 
   * as a placeholder for any future API addition */
  DEBUG_PRINT ("Currently unmap is a dummy function, free will do unmap as well");
  return 0;
}

void *vvas_xrt_map_bo (vvasBOHandle bo,
      bool write/* Not used as of now, as its always read and write*/)
{
  xrt::bo * bo_handle = (xrt::bo *) bo;
  void *data = NULL;

  data = bo_handle->map ();
  return data;
}

int32_t vvas_xrt_write_bo (vvasBOHandle bo, 
                           const void *src, size_t size, size_t seek)
{
  xrt::bo * bo_handle = (xrt::bo *) bo;

  bo_handle->write (src, size, seek);

  return 0;
}

int32_t vvas_xrt_sync_bo (vvasBOHandle bo,
                        vvas_bo_sync_direction dir, size_t size, size_t offset)
{
  xrt::bo * bo_handle = (xrt::bo *) bo;

  bo_handle->sync ((xclBOSyncDirection)dir, size, offset);

  return 0;
}

int32_t vvas_xrt_read_bo (vvasBOHandle bo, void *dst, size_t size, size_t skip)
{
  xrt::bo * bo_handle = (xrt::bo *) bo;

  bo_handle->read (dst, size, skip);

  return 0;
}


/* Device APIs */
int32_t
    vvas_xrt_close_context (vvasKernelHandle kern_handle) 
{
  xrt::kernel * kernel = (xrt::kernel *) kern_handle;

  delete kernel;
  kern_handle = NULL;

  return 0;
}


int32_t
    vvas_xrt_open_context (vvasDeviceHandle handle, uuid_t xclbinId,
    vvasKernelHandle * kernelHandle, char *kernel_name, bool shared)
{
  xrt::device * device = (xrt::device *) handle;
  xrt::kernel::cu_access_mode mode;

  if (shared) {
    mode = xrt::kernel::cu_access_mode::shared;
  } else {
    mode = xrt::kernel::cu_access_mode::exclusive;
  }

  auto kern_handle = new xrt::kernel (*device, xclbinId, kernel_name, mode);
  
  *kernelHandle = kern_handle;
  return 0;
}

void vvas_xrt_close_device (vvasDeviceHandle dev_handle)
{
  xrt::device * device = (xrt::device *) dev_handle;

  delete device;
  dev_handle = NULL;

  return;
}

int32_t vvas_xrt_open_device (int32_t dev_idx, vvasDeviceHandle * dev_handle)
{
  auto device = new xrt::device (dev_idx);
   *dev_handle = device;

    return 1;
}

void
vvas_xrt_write_reg (vvasKernelHandle kern_handle, uint32_t offset, uint32_t data)
{
  xrt::kernel * kernel = (xrt::kernel *) kern_handle;

  kernel->write_register (offset, data);

  return;
}

void
vvas_xrt_read_reg (vvasKernelHandle kern_handle, uint32_t offset, uint32_t *data)
{
  xrt::kernel * kernel = (xrt::kernel *) kern_handle;

  *data = kernel->read_register (offset);

  return;
}

int vvas_xrt_alloc_xrt_buffer (vvasDeviceHandle dev_handle,
    unsigned int size,
    vvas_bo_flags bo_flags,
    unsigned int mem_bank, xrt_buffer* buffer)
{
  xrt::device * device = (xrt::device *) dev_handle;

  auto bo_handle = new xrt::bo (*device, size, bo_flags, mem_bank);

  buffer->user_ptr = bo_handle->map ();
  buffer->bo = bo_handle;
  buffer->size = size;
  buffer->phy_addr = bo_handle->address ();
  
  return 0;
}

void vvas_xrt_free_xrt_buffer (xrt_buffer * buffer)
{
  xrt::bo * bo_handle = (xrt::bo *) buffer->bo;

  delete bo_handle;
}

int vvas_xrt_download_xclbin (const char *bit,
                              vvasDeviceHandle handle, uuid_t * xclbinId)
{
  std::string xclbin_fnm (bit);
  xrt::xclbin* xclbin = new xrt::xclbin (xclbin_fnm);
  xrt::device * device = (xrt::device *) handle;

  auto uuid = device->load_xclbin (*xclbin);
  uuid_copy (*xclbinId, uuid.get ());

  delete xclbin;
  return 0;
}

int vvas_xrt_get_xclbin_uuid (vvasDeviceHandle handle, uuid_t * xclbinId)
{
  xrt::device * device = (xrt::device *) handle;

  auto uuid = device->get_xclbin_uuid();
  uuid_copy (*xclbinId, uuid.get ());
  return 0; 
} 


void vvas_free_ert_xcl_xrt_buffer (xclDeviceHandle handle, xrt_buffer *buffer)
{
  xclBufferHandle bh = 0;
  if (buffer->bo)
    bh = *((xclBufferHandle *)(buffer->bo));

  if (buffer->user_ptr && buffer->size)
    xclUnmapBO (handle, bh, buffer->user_ptr);
  if (handle && bh > 0)
    xclFreeBO (handle, bh);

  if (buffer->bo)
    free(buffer->bo);
  memset (buffer, 0x00, sizeof (xrt_buffer));
}

int32_t vvas_softkernel_xrt_open_device (int32_t dev_idx, xclDeviceHandle xcl_dev_hdl, vvasDeviceHandle * dev_handle)
{
   xrt::device *device = new xrt::device {xcl_dev_hdl};
   *dev_handle = device;

    return 1;
}

/* TODO: vvas_xrt_send_softkernel_command() will be updated in 2022.1 XRT */
int vvas_xrt_send_softkernel_command (xclDeviceHandle handle,
  xrt_buffer * sk_ert_buf, unsigned int *payload, unsigned int num_idx,
  unsigned int cu_mask, int timeout)
{
  struct ert_start_kernel_cmd *ert_cmd =
    (struct ert_start_kernel_cmd *) (sk_ert_buf->user_ptr);
  int ret = 0;
  int retry_cnt = 0;

  if (NULL == sk_ert_buf || NULL == payload
    || (num_idx * sizeof (unsigned int)) > ERT_START_CMD_PAYLOAD_SIZE
    || !num_idx) {
    //ut<<"Invalid argument";
    //     //cout<<     ("invalid arguments. sk_buf = %p, payload = %p and num idx = %d",
    //          //     sk_ert_buf, payload, num_idx);
     return -1;
  }

  ert_cmd->state = ERT_CMD_STATE_NEW;
  ert_cmd->opcode = ERT_SK_START;

  ert_cmd->extra_cu_masks = 3;

  if (cu_mask > 31) {
    ert_cmd->cu_mask = 0;
    if (cu_mask > 63) {
    ert_cmd->data[0] = 0;
    if (cu_mask > 96) {
      ert_cmd->data[1] = 0;
      ert_cmd->data[2] = (1 << (cu_mask - 96));
    } else {
      ert_cmd->data[1] = (1 << (cu_mask - 64));
      ert_cmd->data[2] = 0;
    }
    } else {
    ert_cmd->data[0] = (1 << (cu_mask - 32));
    }
  } else {
    ert_cmd->cu_mask = (1 << cu_mask);
    ert_cmd->data[0] = 0;
    ert_cmd->data[1] = 0;
    ert_cmd->data[2] = 0;
  }

  ert_cmd->count = num_idx + 4;
  memcpy (&ert_cmd->data[3], payload, num_idx * sizeof (unsigned int));

  xclBufferHandle *hdl = (xclBufferHandle *)(sk_ert_buf->bo);
    ret = xclExecBuf (handle, *hdl);
    if (ret < 0) {
      ERROR_PRINT
          ("[handle %p & bo %p] ExecBuf failed with ret = %d. reason : %s",
          handle, sk_ert_buf->bo, ret, strerror (errno));
      return ret;
    }
    do {
      ret = xclExecWait (handle, timeout);
      if (ret < 0) {
        ERROR_PRINT ("ExecWait ret = %d. reason : %s", ret, strerror (errno));
        return ret;
      } else if (!ret) {
        if (retry_cnt++ >= 10) {
          ERROR_PRINT ("[handle %p] ExecWait ret = %d. reason : %s", handle,
              ret, strerror (errno));
          return -1;
        }
        ERROR_PRINT ("[handle %p & bo %p] timeout...retry execwait\n", handle,
            sk_ert_buf->bo);
      }
    } while (ert_cmd->state != ERT_CMD_STATE_COMPLETED);

  return 0;
}


#ifdef XLNX_PCIe_PLATFORM
size_t
vvas_xrt_get_num_compute_units (const char *xclbin_filename)
{
  std::string xclbin_fnm (xclbin_filename);
  xrt::xclbin* xclbin = new xrt::xclbin (xclbin_fnm);
  size_t num_cus = 0;

  auto kernels = xclbin->get_kernels();

  for (auto& it : kernels) {
    num_cus += it.get_cus().size();
  }
  return num_cus; 
}

size_t
vvas_xrt_get_num_kernels (const char *xclbin_filename)
{
  std::string xclbin_fnm (xclbin_filename);
  xrt::xclbin* xclbin = new xrt::xclbin (xclbin_fnm);

  return xclbin->get_kernels().size();
}
#endif
} /* End of extern C */
