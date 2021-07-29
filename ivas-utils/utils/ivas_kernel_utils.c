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
#include <ivas/ivas_kernel.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#ifdef XLNX_PCIe_PLATFORM
#include <xrt.h>
#include <ert.h>
#include <xclhal2.h>
#else
#include <xrt/xrt.h>
#include <xrt/ert.h>
#include <xrt/xclhal2.h>
#endif
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

#undef DUMP_REG                 // dump reg_map just before sending ert cmd

enum
{
  LOG_LEVEL_ERROR,
  LOG_LEVEL_WARNING,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG
};

static int log_level = LOG_LEVEL_WARNING;

#define LOG_MESSAGE(level, ...) {\
  do {\
    char *str; \
    if (level == LOG_LEVEL_ERROR)\
      str = (char*)"ERROR";\
    else if (level == LOG_LEVEL_WARNING)\
      str = (char*)"WARNING";\
    else if (level == LOG_LEVEL_INFO)\
      str = (char*)"INFO";\
    else if (level == LOG_LEVEL_DEBUG)\
      str = (char*)"DEBUG";\
    if (level <= log_level) {\
      printf("[%s:%d] %s: ", __func__, __LINE__, str);\
      printf(__VA_ARGS__);\
      printf("\n");\
    }\
  } while (0); \
}


static int
alloc_xrt_buffer (xclDeviceHandle handle, IVASFrame * frame,
    enum xclBOKind bo_kind, unsigned flags)
{
  frame->bo[0] = xclAllocBO (handle, frame->size[0], bo_kind, flags);
  if (frame->bo[0] == NULLBO) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to allocate Device BO...");
    return -1;
  }

  frame->vaddr[0] = xclMapBO (handle, frame->bo[0], true);
  if (frame->vaddr[0] == NULL) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to map BO...");
    xclFreeBO (handle, frame->bo[0]);
    return -1;
  }

  if (bo_kind != XCL_BO_SHARED_VIRTUAL) {
    struct xclBOProperties p;
    if (xclGetBOProperties (handle, frame->bo[0], &p)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to get physical address...");
      munmap (frame->vaddr[0], frame->size[0]);
      xclFreeBO (handle, frame->bo[0]);
      return -1;
    }
    frame->paddr[0] = p.paddr;
  }

  frame->meta_data = NULL;
  frame->app_priv = NULL;
  frame->n_planes = 1;

  LOG_MESSAGE (LOG_LEVEL_DEBUG,
      "allocated xrt buffer : bo = %d, paddr = %p, vaddr = %p and size = %d",
      frame->bo[0], (void *) frame->paddr[0], frame->vaddr[0], frame->size[0]);

  return 0;
}

static void
free_xrt_buffer (xclDeviceHandle handle, IVASFrame * frame)
{
  if (!handle || !frame) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, "invalid arguments : handle %p, frame %p",
        handle, frame);
    return;
  }
  if (frame->vaddr[0] && frame->size[0])
    munmap (frame->vaddr[0], frame->size[0]);
  if (handle && frame->bo[0] > 0)
    xclFreeBO (handle, frame->bo[0]);
  memset (frame, 0x00, sizeof (IVASFrame));
}

IVASFrame *
ivas_alloc_buffer (IVASKernel * handle, uint32_t size, IVASMemoryType mem_type,
    IVASFrameProps * props)
{
  IVASFrame *frame = NULL;
  int iret = -1;

  if (!handle) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, "invalid arguments : handle %p", handle);
    goto error;
  }

  frame = (IVASFrame *) calloc (1, sizeof (IVASFrame));
  if (!frame) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to allocate ivas_frame");
    goto error;
  }
  frame->mem_type = mem_type;

  if (mem_type == IVAS_INTERNAL_MEMORY) {
    frame->size[0] = size;

    iret = alloc_xrt_buffer (handle->xcl_handle, frame, XCL_BO_DEVICE_RAM, 0);
    if (iret < 0) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to allocate internal memory");
      goto error;
    }
  } else {
    if (!props || !props->width || !props->height
        || (props->fmt == IVAS_VMFT_UNKNOWN)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "invalid arguments for properties");
      goto error;
    }

    memcpy (&(frame->props), props, sizeof (IVASFrameProps));
    if (!handle->alloc_func) {
      LOG_MESSAGE (LOG_LEVEL_ERROR,
          "app did not set alloc_func callback function");
      goto error;
    }

    iret = handle->alloc_func (handle, frame, handle->cb_user_data);
    if (iret < 0) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to allocate frame memory");
      goto error;
    }
  }
  return frame;

error:
  if (frame) {
    ivas_free_buffer (handle, frame);
    free (frame);
  }
  return NULL;
}

void
ivas_free_buffer (IVASKernel * handle, IVASFrame * ivas_frame)
{
  if (ivas_frame->mem_type == IVAS_INTERNAL_MEMORY) {
    free_xrt_buffer (handle, ivas_frame);
  } else {
    if (!handle->free_func) {
      LOG_MESSAGE (LOG_LEVEL_ERROR,
          "app did not set free_func callback function");
    } else {
      handle->free_func (handle, ivas_frame, handle->cb_user_data);
    }
  }
  free (ivas_frame);
}

void
ivas_register_write (IVASKernel * handle, void *src, size_t size, size_t offset)
{
  if (handle->is_multiprocess) {
    struct ert_start_kernel_cmd *ert_cmd =
        (struct ert_start_kernel_cmd *) (handle->ert_cmd_buf->user_ptr);
    uint32_t *src_array = (uint32_t *) src;
    size_t cur_min = offset;
    size_t cur_max = offset + size;
    int32_t entries = size / sizeof (uint32_t);
    int32_t start = offset / sizeof (uint32_t);
    int32_t i;

    for (i = 0; i < entries; i++)
      ert_cmd->data[start + i] = src_array[i];

    if (cur_max > handle->max_offset)
      handle->max_offset = cur_max;

    if (cur_min < handle->min_offset)
      handle->min_offset = cur_min;
  } else {
    uint32_t *value = (uint32_t *) src;
    xclRegWrite ((xclDeviceHandle) handle->xcl_handle, handle->cu_idx, offset,
        value[0]);
  }
  return;
}

void
ivas_register_read (IVASKernel * handle, void *src, size_t size, size_t offset)
{
  uint32_t *value = (uint32_t *) src;
  xclRegRead ((xclDeviceHandle) handle->xcl_handle, handle->cu_idx, offset,
      &(value[0]));
}

int32_t
ivas_kernel_start (IVASKernel * handle)
{
  struct ert_start_kernel_cmd *ert_cmd =
      (struct ert_start_kernel_cmd *) (handle->ert_cmd_buf->user_ptr);

  ert_cmd->state = ERT_CMD_STATE_NEW;

#ifdef XLNX_PCIe_PLATFORM
  ert_cmd->opcode = handle->is_softkernel ? ERT_SK_START : ERT_START_CU;
#else
  ert_cmd->opcode = ERT_START_CU;
#endif
  ert_cmd->cu_mask = 1 << handle->cu_idx;
  ert_cmd->count = (handle->max_offset >> 2) + 1;

#ifdef DUMP_REG
  {
    int i;
    for (i = 0; i < ert_cmd->count; i++)
      printf ("index 0x%x - 0x%x\n", i * sizeof (uint32_t), ert_cmd->data[i]);
  }
#endif

  if (xclExecBuf (handle->xcl_handle, handle->ert_cmd_buf->bo)) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to issue XRT command");
    return -1;
  }
  LOG_MESSAGE (LOG_LEVEL_DEBUG, "Submitted command to kernel");

  return 0;
}

int32_t
ivas_kernel_done (IVASKernel * handle, int32_t timeout)
{
  struct ert_start_kernel_cmd *ert_cmd =
      (struct ert_start_kernel_cmd *) (handle->ert_cmd_buf->user_ptr);
  int ret;
  int retry_count = MAX_EXEC_WAIT_RETRY_CNT;

  LOG_MESSAGE (LOG_LEVEL_DEBUG, "Going to wait for kernel command to finish");

  do {
    ret = xclExecWait (handle->xcl_handle, timeout);
    if (ret < 0) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "ExecWait ret = %d. reason : %s", ret,
          strerror (errno));
      return -1;
    } else if (!ret) {
      LOG_MESSAGE (LOG_LEVEL_WARNING, "timeout...retry execwait");
      if (retry_count-- <= 0) {
        LOG_MESSAGE (LOG_LEVEL_ERROR,
            "max retry count %d reached..returning error",
            MAX_EXEC_WAIT_RETRY_CNT);
        return -1;
      }
    }
  } while (ert_cmd->state != ERT_CMD_STATE_COMPLETED);

  LOG_MESSAGE (LOG_LEVEL_DEBUG, "successfully completed kernel command");

  return 0;
}

#ifdef XLNX_PCIe_PLATFORM
int32_t
ivas_sync_data (IVASKernel * handle, IVASSyncDataFlag flag, IVASFrame * frame)
{
  int iret, plane_id;
  enum xclBOSyncDirection sync_flag =
      (flag ==
      IVAS_SYNC_DATA_TO_DEVICE) ? XCL_BO_SYNC_BO_TO_DEVICE :
      IVAS_SYNC_DATA_FROM_DEVICE;

  for (plane_id = 0; plane_id < frame->n_planes; plane_id++) {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "plane %d syncing %s : bo = %d, size = %d",
        plane_id,
        sync_flag == XCL_BO_SYNC_BO_TO_DEVICE ? "to device" : "from device",
        frame->bo[plane_id], frame->size[plane_id]);

    iret =
        xclSyncBO (handle->xcl_handle, frame->bo[plane_id], sync_flag,
        frame->size[plane_id], 0);
    if (iret != 0) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "xclSyncBO failed %d, reason : %s", iret,
          strerror (errno));
      return iret;
    }
  }
  return 0;
}
#endif

/***************** ivas caps utils ********************/
bool
ivas_caps_set_pad_nature (IVASKernel * handle, padsnature nature)
{
  if (!handle->padinfo)
    handle->padinfo = (ivaspads *) calloc (1, sizeof (ivaspads));
  if (handle->padinfo) {
    handle->padinfo->nature = nature;
  } else
    return false;
  return true;
}

padsnature
ivas_caps_get_pad_nature (IVASKernel * handle)
{
  if (!handle->padinfo)
    return IVAS_PAD_DEFAULT;
  else
    return handle->padinfo->nature;
}

bool
ivas_caps_set_stride_align (IVASKernel * handle, paddir dir,
    unsigned int stride_align)
{
  if (!handle->padinfo)
    handle->padinfo = (ivaspads *) calloc (1, sizeof (ivaspads));
  if (handle->padinfo) {
    if (dir == SINK)
      handle->padinfo->sinkpad_align.stride_align = stride_align;
    else
      handle->padinfo->srcpad_align.stride_align = stride_align;
  } else
    return false;
  return true;
}

bool
ivas_caps_set_sink_stride_align (IVASKernel * handle, unsigned int stride_align)
{
  return ivas_caps_set_stride_align (handle, SINK, stride_align);
}

bool
ivas_caps_set_src_stride_align (IVASKernel * handle, unsigned int stride_align)
{
  return ivas_caps_set_stride_align (handle, SRC, stride_align);
}

unsigned int
ivas_caps_get_stride_align (IVASKernel * handle, paddir dir)
{
  if (!handle->padinfo)
    return 0;
  else {
    if (dir == SINK)
      return handle->padinfo->sinkpad_align.stride_align;
    else
      return handle->padinfo->srcpad_align.stride_align;
  }
}

unsigned int
ivas_caps_get_sink_stride_align (IVASKernel * handle)
{
  return ivas_caps_get_stride_align (handle, SINK);
}

unsigned int
ivas_caps_get_src_stride_align (IVASKernel * handle)
{
  return ivas_caps_get_stride_align (handle, SRC);
}

bool
ivas_caps_set_height_align (IVASKernel * handle, paddir dir,
    unsigned int height_align)
{
  if (!handle->padinfo)
    handle->padinfo = (ivaspads *) calloc (1, sizeof (ivaspads));
  if (handle->padinfo) {
    if (dir == SINK)
      handle->padinfo->sinkpad_align.height_align = height_align;
    else
      handle->padinfo->srcpad_align.height_align = height_align;
  } else
    return false;
  return true;
}

bool
ivas_caps_set_sink_height_align (IVASKernel * handle, unsigned int height_align)
{
  return ivas_caps_set_height_align (handle, SINK, height_align);
}

bool
ivas_caps_set_src_height_align (IVASKernel * handle, unsigned int height_align)
{
  return ivas_caps_set_height_align (handle, SRC, height_align);
}

unsigned int
ivas_caps_get_height_align (IVASKernel * handle, paddir dir)
{
  if (!handle->padinfo)
    return 0;
  else {
    if (dir == SINK)
      return handle->padinfo->sinkpad_align.height_align;
    else
      return handle->padinfo->srcpad_align.height_align;
  }
}

unsigned int
ivas_caps_get_sink_height_align (IVASKernel * handle)
{
  return ivas_caps_get_height_align (handle, SINK);
}

unsigned int
ivas_caps_get_src_height_align (IVASKernel * handle)
{
  return ivas_caps_get_height_align (handle, SRC);
}

/**
 * ivas_caps_new() - Create new caps with input parameters
 * range_height
 * 	- true  : if kernel support range of height
 * 	- false : if kernel support fixed height
 * lower_height : lower value of height supported by kernel
 *                if range_height is false, this holds the fixed value
 * upper_height : higher value of hight supported by kernel
 *                if range_height is false, this should be 0
 *
 * range_width : same as above
 * lower_width :
 * upper_width :
 *
 * ...         : variable range of format supported terminated by 0
 *               make sure to add 0 at end otherwise it
 *               code will take format till it get 0
 *
 */

kernelcaps *
ivas_caps_new (uint8_t range_height, uint32_t lower_height,
    uint32_t upper_height, uint8_t range_width, uint32_t lower_width,
    uint32_t upper_width, ...)
{

  va_list valist;
  int i, num_fmt = 0;
  kernelcaps *kcaps;

  if (lower_height == 0 && lower_width == 0) {
    LOG_MESSAGE (LOG_LEVEL_ERROR,
        "Wrong parameter lower_width = %d, lower_width = %d\n", lower_height,
        lower_width);
    return NULL;
  }

  if ((range_height == true && (upper_height == 0 || lower_height == 0)) &&
      (range_width == true && (upper_width == 0 || lower_width == 0)) &&
      (lower_height == 0 && lower_width == 0)) {

    LOG_MESSAGE (LOG_LEVEL_ERROR, "ivas_caps_new: Wrong parameters:");
    LOG_MESSAGE (LOG_LEVEL_ERROR,
        "range_height = %d, lower_height = %d upper_height = %d, ",
        range_height, lower_height, upper_height);
    LOG_MESSAGE (LOG_LEVEL_ERROR,
        "range_width = %d, lower_width = %d upper_width = %d, ", range_height,
        lower_height, upper_height);
    return NULL;
  }

  kcaps = (kernelcaps *) calloc (1, sizeof (kernelcaps));

  kcaps->range_height = range_height;
  kcaps->lower_height = lower_height;
  kcaps->upper_height = upper_height;
  kcaps->range_width = range_width;
  kcaps->lower_width = lower_width;
  kcaps->upper_width = upper_width;

  va_start (valist, upper_width);
  while (va_arg (valist, int) != 0)
      num_fmt++;
  va_end (valist);

  if (!num_fmt) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, "ivas_caps_new: No format provided\n");
    free (kcaps);
    return NULL;
  }

  kcaps->fmt = (IVASVideoFormat *) calloc (num_fmt, sizeof (IVASVideoFormat));
  kcaps->num_fmt = num_fmt;

  va_start (valist, upper_width);
  for (i = 0; i < num_fmt; i++) {
    kcaps->fmt[i] = va_arg (valist, int);
  }
  va_end (valist);

  return kcaps;
}

bool
ivas_caps_add (IVASKernel * handle, kernelcaps * kcaps, paddir dir,
    int sinkpad_num)
{
  kernelpads **pads;

  if (sinkpad_num != 0) {
    LOG_MESSAGE (LOG_LEVEL_ERROR,
        "ivas_caps_add_to_sink: only one pad supported yet\n");
    return false;
  }

  if (!handle->padinfo)
    ivas_caps_set_pad_nature (handle, IVAS_PAD_DEFAULT);

  if (dir == SINK) {
    if (!handle->padinfo->sinkpads)
      handle->padinfo->sinkpads =
          (kernelpads **) calloc (1, sizeof (kernelpads *));

    pads = handle->padinfo->sinkpads;
  } else {
    if (!handle->padinfo->srcpads)
      handle->padinfo->srcpads =
          (kernelpads **) calloc (1, sizeof (kernelpads *));

    pads = handle->padinfo->srcpads;
  }

  if (!pads[sinkpad_num]) {
    pads[sinkpad_num] = (kernelpads *) calloc (1, sizeof (kernelpads));
    handle->padinfo->nu_sinkpad++;
  }

  pads[sinkpad_num]->kcaps =
      (kernelcaps **) realloc (pads[sinkpad_num]->kcaps,
      (pads[sinkpad_num]->nu_caps + 1) * sizeof (kernelcaps *));
  pads[sinkpad_num]->kcaps[pads[sinkpad_num]->nu_caps] = kcaps;

  pads[sinkpad_num]->nu_caps++;

  return true;
}

/**
 * ivas_caps_add_to_sink() - add new caps created by ivas_caps_new() to sink
 *                          these caps are used by xfilter to negosiate 
 *                          with upstream plugin
 *                          Only one pad is supported yet
 */
bool
ivas_caps_add_to_sink (IVASKernel * handle, kernelcaps * kcaps, int sinkpad_num)
{
  return ivas_caps_add (handle, kcaps, 0, sinkpad_num);
}

/**
 * ivas_caps_add_to_sink() - add new caps created by ivas_caps_new() to src
 *                          these caps are used by xfilter to negosiate 
 *                          with downstream plugin
 *                          Only one pad is supported yet
 */
bool
ivas_caps_add_to_src (IVASKernel * handle, kernelcaps * kcaps, int srcpad_num)
{
  return ivas_caps_add (handle, kcaps, 1, srcpad_num);
}

int
ivas_caps_get_num_caps (IVASKernel * handle, int sinkpad_num)
{
  if (handle->padinfo->sinkpads[sinkpad_num])
    return handle->padinfo->sinkpads[sinkpad_num]->nu_caps;
  else
    return 0;
}

int
ivas_caps_get_num_sinkpads (IVASKernel * handle)
{
  if (handle->padinfo)
    return handle->padinfo->nu_sinkpad;
  else
    return 0;
}

int
ivas_caps_get_num_srcpads (IVASKernel * handle)
{
  if (handle->padinfo)
    return handle->padinfo->nu_srcpad;
  else
    return 0;
}

void
ivas_caps_free (IVASKernel * handle)
{
  int i, j;
  kernelpads **pads;

  if (!handle || !handle->padinfo)
    return;

  pads = handle->padinfo->sinkpads;
  if (pads) {
    for (i = 0; i < handle->padinfo->nu_sinkpad; i++) {
      for (j = 0; j < pads[i]->nu_caps; j++) {
        free (pads[i]->kcaps[j]->fmt);
        free (pads[i]->kcaps[j]);
      }
    }
    free (pads);
  }

  pads = handle->padinfo->srcpads;
  if (pads) {
    for (i = 0; i < handle->padinfo->nu_sinkpad; i++) {
      for (j = 0; j < pads[i]->nu_caps; j++) {
        free (pads[i]->kcaps[j]->fmt);
        free (pads[i]->kcaps[j]);
      }
    }
    free (pads);
  }

  free (handle->padinfo);
}

void
ivas_caps_print (IVASKernel * handle)
{
  int i, j, k;
  kernelpads **pads;
  if (!handle || !handle->padinfo)
    return;

  pads = handle->padinfo->sinkpads;
  LOG_MESSAGE (LOG_LEVEL_DEBUG, "total sinkpad %d",
      handle->padinfo->nu_sinkpad);
  if (pads) {
    for (i = 0; i < handle->padinfo->nu_sinkpad; i++) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "nu of caps for sinkpad[%d] = %d", i,
          pads[i]->nu_caps);
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "value are");
      LOG_MESSAGE (LOG_LEVEL_DEBUG,
          "range_height\tlower_height\tupper_height\trange_width\tlower_width\tupper_width\tfmt...");
      for (j = 0; j < pads[i]->nu_caps; j++) {

        LOG_MESSAGE (LOG_LEVEL_DEBUG, "%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t ",
            pads[i]->kcaps[j]->range_height, pads[i]->kcaps[j]->lower_height,
            pads[i]->kcaps[j]->upper_height, pads[i]->kcaps[j]->range_width,
            pads[i]->kcaps[j]->lower_width, pads[i]->kcaps[j]->upper_width);
        for (k = 0; k < pads[i]->kcaps[j]->num_fmt; k++)
          LOG_MESSAGE (LOG_LEVEL_DEBUG, "%d ", pads[i]->kcaps[j]->fmt[k]);
      }
    }
  }

  pads = handle->padinfo->srcpads;
  LOG_MESSAGE (LOG_LEVEL_DEBUG, "total srcpad %d", handle->padinfo->nu_srcpad);
  if (pads) {
    for (i = 0; i < handle->padinfo->nu_srcpad; i++) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "nu of caps for srcpad[%d] = %d", i,
          pads[i]->nu_caps);
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "value are");
      LOG_MESSAGE (LOG_LEVEL_DEBUG,
          "range_height\tlower_height\tupper_height\trange_width\tlower_width\tupper_width\tfmt...");
      for (j = 0; j < pads[i]->nu_caps; j++) {

        LOG_MESSAGE (LOG_LEVEL_DEBUG, "%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t ",
            pads[i]->kcaps[j]->range_height, pads[i]->kcaps[j]->lower_height,
            pads[i]->kcaps[j]->upper_height, pads[i]->kcaps[j]->range_width,
            pads[i]->kcaps[j]->lower_width, pads[i]->kcaps[j]->upper_width);
        for (k = 0; k < pads[i]->kcaps[j]->num_fmt; k++)
          LOG_MESSAGE (LOG_LEVEL_DEBUG, "%d ", pads[i]->kcaps[j]->fmt[k]);
      }
    }
  }
}
