/*
* Copyright (C) 2020 - 2022 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software
* is furnished to do so, subject to the following conditions:
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
* KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
* EVENT SHALL XILINX BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
* OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE. Except as contained in this notice, the name of the Xilinx shall
* not be used in advertising or otherwise to promote the sale, use or other
* dealings in this Software without prior written authorization from Xilinx.
*/

#include <stdio.h>
#include <stdint.h>
#include "vvas_multi_scaler_hw.h"
#include <vvas/vvas_kernel.h>
#include <gst/vvas/gstinferencemeta.h>
#include <gst/vvas/gstvvasinpinfer.h>
#include <vvas/vvaslogs.h>
#include <unistd.h>

#define MULTI_SCALER_TIMEOUT 1000       // 1 sec
#define MAX_CHANNELS  40
#define MAX_ROI MAX_CHANNELS

/* preprocessor defaults : no effect on preprocessing */
#define DEFAULT_ALPHA_R 0
#define DEFAULT_ALPHA_G 0
#define DEFAULT_ALPHA_B 0
#define DEFAULT_BETA_R 1
#define DEFAULT_BETA_G 1
#define DEFAULT_BETA_B 1

struct _roi
{
  uint32_t y_cord;
  uint32_t x_cord;
  uint32_t height;
  uint32_t width;
};

typedef struct _vvas_ms_roi
{
  uint32_t nobj;
  struct _roi roi[MAX_ROI];
} vvas_ms_roi;

typedef struct _kern_priv
{
  VVASFrame *msPtr[MAX_CHANNELS];
  int inference_level;
  vvas_ms_roi roi_data;
  uint32_t msPtr_index;
  int log_level;
  int ppc;
  float alpha_r;
  float alpha_g;
  float alpha_b;
  float beta_r;
  float beta_g;
  float beta_b;
  uint16_t in_mem_bank;
  uint16_t out_mem_bank;
} vvasMSKernelPriv;

int32_t xlnx_kernel_start (VVASKernel * handle, int start,
    VVASFrame * input[MAX_NUM_OBJECT], VVASFrame * output[MAX_NUM_OBJECT]);
int32_t xlnx_kernel_done (VVASKernel * handle);
int32_t xlnx_kernel_init (VVASKernel * handle);
uint32_t xlnx_kernel_deinit (VVASKernel * handle);

static uint32_t
xlnx_multiscaler_align (uint32_t stride_in, uint16_t MMWidthBytes)
{
  uint32_t stride;

  stride = (((stride_in) + MMWidthBytes - 1) / MMWidthBytes) * MMWidthBytes;
  return stride;
}

static uint32_t
xlnx_multiscaler_colorformat (uint32_t col)
{
  switch (col) {
    case VVAS_VFMT_RGBX8:
      return XV_MULTI_SCALER_RGBX8;
    case VVAS_VFMT_YUYV8:
      return XV_MULTI_SCALER_YUYV8;
    case VVAS_VFMT_RGBX10:
      return XV_MULTI_SCALER_RGBX10;
    case VVAS_VFMT_YUVX10:
      return XV_MULTI_SCALER_YUVX10;
    case VVAS_VFMT_Y_UV8:
      return XV_MULTI_SCALER_Y_UV8;
    case VVAS_VFMT_Y_UV8_420:
      return XV_MULTI_SCALER_Y_UV8_420;
    case VVAS_VFMT_RGB8:
      return XV_MULTI_SCALER_RGB8;
    case VVAS_VFMT_YUV8:
      return XV_MULTI_SCALER_YUV8;
    case VVAS_VFMT_Y_UV10:
      return XV_MULTI_SCALER_Y_UV10;
    case VVAS_VFMT_Y_UV10_420:
      return XV_MULTI_SCALER_Y_UV10_420;
    case VVAS_VFMT_Y8:
      return XV_MULTI_SCALER_Y8;
    case VVAS_VFMT_Y10:
      return XV_MULTI_SCALER_Y10;
    case VVAS_VFMT_BGRX8:
      return XV_MULTI_SCALER_BGRX8;
    case VVAS_VFMT_UYVY8:
      return XV_MULTI_SCALER_UYVY8;
    case VVAS_VFMT_BGR8:
      return XV_MULTI_SCALER_BGR8;
    case VVAS_VFMT_ABGR8:
      return XV_MULTI_SCALER_BGRA8;
    case VVAS_VFMT_ARGB8:
      return XV_MULTI_SCALER_RGBA8;
    case VVAS_VFMT_I420:
      return XV_MULTI_SCALER_I420;
    default:
      printf ("ERROR: VVAS PPE: Color format not supported\n");
      return XV_MULTI_SCALER_NONE;
  }
}

#define MIN_SCALAR_INPUT_WIDTH 64
#define MIN_SCALAR_INPUT_HEIGHT 64

static int
xlnx_multiscaler_descriptor_create (VVASKernel * handle,
    VVASFrame * input[MAX_NUM_OBJECT], VVASFrame * output[MAX_NUM_OBJECT],
    vvas_ms_roi roi_data)
{
  MULTISCALER_DESC_STRUCT *msPtr;
  guint chan_id;
  vvasMSKernelPriv *kpriv = (vvasMSKernelPriv *) handle->kernel_priv;
  VVASFrame *in_vvas_frame, *out_vvas_frame;
  VVASFrameProps out_props = { 0, }, in_props = {
  0,};
  uint32_t y_cord, x_cord;
  uint32_t stride;
  uint16_t stride_align = (8 * kpriv->ppc);     /* Stride (input and output both) must be multiple of 8*PPC */
  uint val, plane0_offset = 0, plane1_offset = 0, plane2_offset = 0;
  int32_t nx, nw, ny, nh;
  gfloat scale_factor_x;
  gfloat scale_factor_y;
  GstVideoMeta *vmeta;
  GstVideoAlignment *padding;

  in_vvas_frame = input[0];
  stride = xlnx_multiscaler_align (in_vvas_frame->props.stride, stride_align);

  for (chan_id = 0; chan_id < roi_data.nobj; chan_id++) {
    out_vvas_frame = output[chan_id];

    if (!out_vvas_frame) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "output frame for channel-%d not available", chan_id);
      return -1;
    }

    /* check for minimum size according to scalar requirements */
    if ((roi_data.roi[chan_id].width < MIN_SCALAR_INPUT_WIDTH) ||
        (roi_data.roi[chan_id].height < MIN_SCALAR_INPUT_HEIGHT)) {
      LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
          "roi sizes are small for processing");
      continue;
    }

    /* Starting location (DDR address) of the input image,
     * cropped input location in DDR and output image store location
     * in DDR must be aligned to 8*PPC */
    nx = (roi_data.roi[chan_id].x_cord / stride_align) * stride_align;
    nw = roi_data.roi[chan_id].x_cord + roi_data.roi[chan_id].width - nx;
    nw = xlnx_multiscaler_align (nw, kpriv->ppc);
    if (in_vvas_frame->props.fmt == VVAS_VFMT_Y_UV8_420)
      ny = (roi_data.roi[chan_id].y_cord / 2) * 2;
    else
      ny = roi_data.roi[chan_id].y_cord;

    nh = roi_data.roi[chan_id].y_cord + roi_data.roi[chan_id].height - ny;
    nh = xlnx_multiscaler_align (nh, 2);

    in_props.width = nw;
    in_props.height = nh;
    y_cord = ny;
    x_cord = nx;

    if (x_cord) {
      switch (in_vvas_frame->props.fmt) {
        case VVAS_VFMT_RGB8:
        case VVAS_VFMT_BGR8:
          x_cord *= 3;
          plane0_offset = ((stride * y_cord) + x_cord);
          break;
        case VVAS_VFMT_Y_UV8_420:
          plane0_offset = ((stride * y_cord) + x_cord);
          plane1_offset =
              xlnx_multiscaler_align (((stride * y_cord / 2) + x_cord),
              stride_align);
          break;
        default:
          LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
              "Crop for %d fmt not supported", in_vvas_frame->props.fmt);
      }
    }

    /* Setting output buffer */
    scale_factor_x =
        ((float) out_vvas_frame->props.width) / roi_data.roi[chan_id].width;
    nw = (int) (in_props.width * scale_factor_x);
    scale_factor_y =
        ((float) out_vvas_frame->props.height) / roi_data.roi[chan_id].height;
    nh = (int) (in_props.height * scale_factor_y);

    out_props.width = xlnx_multiscaler_align (nw, kpriv->ppc);
    out_props.height = xlnx_multiscaler_align (nh, 2);
    out_props.fmt = out_vvas_frame->props.fmt;
    out_props.stride =
        xlnx_multiscaler_align ((out_props.width * 3), stride_align);

    /* Setting proper roi offsets and strides in videometa alighment to use in infer */
    nx = (int) ((roi_data.roi[chan_id].x_cord - nx) * scale_factor_x);
    ny = (int) ((roi_data.roi[chan_id].y_cord - ny) * scale_factor_y);

    vmeta = gst_buffer_get_video_meta ((GstBuffer *) out_vvas_frame->app_priv);
    padding = &vmeta->alignment;
    padding->padding_top = ny;
    padding->padding_bottom =
        out_props.height - (ny + out_vvas_frame->props.height);
    padding->padding_left = nx;
    padding->padding_right =
        out_props.width - (nx + out_vvas_frame->props.width);
    padding->stride_align[0] = out_props.stride;

    msPtr = kpriv->msPtr[kpriv->msPtr_index]->vaddr[0];
    msPtr->msc_srcImgBuf0 = (uint64_t) in_vvas_frame->paddr[0] + plane0_offset; /* plane 1 */
    msPtr->msc_srcImgBuf1 = (uint64_t) in_vvas_frame->paddr[1] + plane1_offset; /* plane 2 */
    msPtr->msc_srcImgBuf2 = (uint64_t) in_vvas_frame->paddr[2] + plane2_offset; /* plane 3 */
    msPtr->msc_dstImgBuf0 = (uint64_t) out_vvas_frame->paddr[0];
    msPtr->msc_dstImgBuf1 = (uint64_t) out_vvas_frame->paddr[1];
    msPtr->msc_dstImgBuf2 = (uint64_t) 0;

    msPtr->msc_widthIn = in_props.width;
    msPtr->msc_heightIn = in_props.height;
    msPtr->msc_alpha_0 = kpriv->alpha_r;
    msPtr->msc_alpha_1 = kpriv->alpha_g;
    msPtr->msc_alpha_2 = kpriv->alpha_b;
    val = (kpriv->beta_r * (1 << 16));
    msPtr->msc_beta_0 = val;
    val = (kpriv->beta_g * (1 << 16));
    msPtr->msc_beta_1 = val;
    val = (kpriv->beta_b * (1 << 16));
    msPtr->msc_beta_2 = val;

    msPtr->msc_inPixelFmt =
        xlnx_multiscaler_colorformat (in_vvas_frame->props.fmt);
    msPtr->msc_strideIn = stride;
    msPtr->msc_widthOut = out_props.width;
    msPtr->msc_heightOut = out_props.height;
    msPtr->msc_lineRate =
        (uint32_t) ((float) ((msPtr->msc_heightIn * STEP_PRECISION) +
            ((msPtr->msc_heightOut) / 2)) / (float) msPtr->msc_heightOut);
    msPtr->msc_pixelRate =
        (uint32_t) ((float) (((msPtr->msc_widthIn) * STEP_PRECISION) +
            ((msPtr->msc_widthOut) / 2)) / (float) msPtr->msc_widthOut);
    msPtr->msc_outPixelFmt = xlnx_multiscaler_colorformat (out_props.fmt);

    msPtr->msc_strideOut = out_props.stride;

    msPtr->msc_blkmm_hfltCoeff = 0;
    msPtr->msc_blkmm_vfltCoeff = 0;
    msPtr->msc_nxtaddr = kpriv->msPtr[kpriv->msPtr_index + 1]->paddr[0];

#ifdef XLNX_PCIe_PLATFORM
    vvas_sync_data (handle, VVAS_SYNC_DATA_TO_DEVICE,
        kpriv->msPtr[kpriv->msPtr_index]);
#endif
    kpriv->msPtr_index++;
  }

  return 0;
}

static gboolean
preprocessor_node_foreach (GNode * node, gpointer kpriv_ptr)
{
  vvasMSKernelPriv *kpriv = (vvasMSKernelPriv *) kpriv_ptr;

  if (g_node_depth (node) == kpriv->inference_level) {
    GstInferencePrediction *pred = (GstInferencePrediction *) node->data;

    if (kpriv->roi_data.nobj == MAX_ROI) {
      LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level, "reached max ROI "
          "supported by preprocessor i.e. %d", MAX_ROI);
      return TRUE;
    }

    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level, "Got node %p at level %d",
        node, kpriv->inference_level);

    kpriv->roi_data.roi[kpriv->roi_data.nobj].x_cord = pred->bbox.x;
    kpriv->roi_data.roi[kpriv->roi_data.nobj].y_cord = pred->bbox.y;
    kpriv->roi_data.roi[kpriv->roi_data.nobj].width = pred->bbox.width;
    kpriv->roi_data.roi[kpriv->roi_data.nobj].height = pred->bbox.height;

    LOG_MESSAGE (LOG_LEVEL_DEBUG, kpriv->log_level,
        "bbox : x = %d, y = %d, width = %d, height = %d", pred->bbox.x,
        pred->bbox.y, pred->bbox.width, pred->bbox.height);
    kpriv->roi_data.nobj++;
  }

  return FALSE;
}

int32_t
xlnx_kernel_start (VVASKernel * handle, int start /*unused */ ,
    VVASFrame * input[MAX_NUM_OBJECT], VVASFrame * output[MAX_NUM_OBJECT])
{
  int ret;
  uint32_t value = 0;
  uint64_t value1 = 0;
  VVASFrame *in_vvas_frame;
  GstInferenceMeta *vvas_meta = NULL;
  vvasMSKernelPriv *kpriv = (vvasMSKernelPriv *) handle->kernel_priv;
  GNode *root;
  unsigned int ms_status = 0;

  in_vvas_frame = input[0];
  kpriv->roi_data.nobj = 0;
  kpriv->msPtr_index = 0;

  vvas_meta = ((GstInferenceMeta *) gst_buffer_get_meta ((GstBuffer *)
          in_vvas_frame->app_priv, gst_inference_meta_api_get_type ()));

  if (kpriv->inference_level != 1) {
    if (!vvas_meta) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level, "metadata not available "
          "to extract bbox co-ordinates");
      return 0;
    }

    root = vvas_meta->prediction->predictions;
    g_node_traverse ((GNode *) root, G_PRE_ORDER, G_TRAVERSE_ALL,
        kpriv->inference_level, preprocessor_node_foreach, (gpointer) kpriv);
  } else {
    /* Input Params : in level-1 scale up/down entire frames */
    kpriv->roi_data.roi[kpriv->roi_data.nobj].x_cord = 0;
    kpriv->roi_data.roi[kpriv->roi_data.nobj].y_cord = 0;
    kpriv->roi_data.roi[kpriv->roi_data.nobj].width =
        in_vvas_frame->props.width;
    kpriv->roi_data.roi[kpriv->roi_data.nobj].height =
        in_vvas_frame->props.height;
    kpriv->roi_data.nobj = 1;
  }

  /* set descriptor */
  ret =
      xlnx_multiscaler_descriptor_create (handle, input, output,
      kpriv->roi_data);
  if (ret < 0) {
    return ret;
  }
  /* we are not starting the kernel if there is no valid roi */
  if (kpriv->msPtr_index == 0) {
    LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
        "PPE: No valid roi data is present, so returning");
    return 0;
  }

  /* program registers */
  value = kpriv->msPtr_index;   /* Number of outputs */
  value1 = kpriv->msPtr[0]->paddr[0];
  /* submit kernel command */
  ret =
      vvas_kernel_start (handle, "ulppu", value, value1, NULL, NULL, ms_status);
  if (ret < 0) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "PPE: failed to issue execute command");
    return ret;
  }

  /* wait for kernel completion */
  ret = vvas_kernel_done (handle, MULTI_SCALER_TIMEOUT);
  if (ret < 0) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "PPE: failed to receive response from kernel");
    return ret;
  }

  return 0;
}

int32_t
xlnx_kernel_init (VVASKernel * handle)
{
  uint8_t chan_id;
  json_t *jconfig = handle->kernel_config;
  json_t *val;                  /* kernel config from app */

  vvasMSKernelPriv *kpriv =
      (vvasMSKernelPriv *) calloc (1, sizeof (vvasMSKernelPriv));
  if (!kpriv) {
    printf ("ERROR: PPE: failed to allocate kernelprivate memory");
    return -1;
  }
  val = json_object_get (jconfig, "debug_level");
  if (!val || !json_is_integer (val))
    kpriv->log_level = LOG_LEVEL_WARNING;
  else
    kpriv->log_level = json_integer_value (val);
  LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "debug_level %d",
      kpriv->log_level);

  val = json_object_get (jconfig, "inference-level");
  if (!val || !json_is_integer (val))
    kpriv->inference_level = 1;
  else
    kpriv->inference_level = json_integer_value (val);
  if (kpriv->inference_level < 1) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
        "PPE: incorrect inference_level level %d", kpriv->inference_level);
    return -1;
  }
  LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "inference_level level : %d",
      kpriv->inference_level);

  val = json_object_get (jconfig, "alpha_r");
  if (!val || !json_is_number (val)) {
    kpriv->alpha_r = DEFAULT_ALPHA_R;
    LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level, "alpha_r not set."
        " taking default : %f", kpriv->alpha_r);
  } else {
    kpriv->alpha_r = json_number_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "alpha_r : %f",
        kpriv->alpha_r);
  }
  val = json_object_get (jconfig, "alpha_g");
  if (!val || !json_is_number (val)) {
    kpriv->alpha_g = DEFAULT_ALPHA_G;
    LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level, "alpha_g not set."
        " taking default : %f", kpriv->alpha_g);
  } else {
    kpriv->alpha_g = json_number_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "alpha_g : %f",
        kpriv->alpha_g);
  }
  val = json_object_get (jconfig, "alpha_b");
  if (!val || !json_is_number (val)) {
    kpriv->alpha_b = DEFAULT_ALPHA_B;
    LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level, "alpha_b not set."
        " taking default : %f", kpriv->alpha_b);
  } else {
    kpriv->alpha_b = json_number_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "alpha_b : %f",
        kpriv->alpha_b);
  }
  val = json_object_get (jconfig, "beta_r");
  if (!val || !json_is_number (val)) {
    kpriv->beta_r = DEFAULT_BETA_R;
    LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level, "beta_r not set."
        " taking default : %f", kpriv->beta_r);
  } else {
    kpriv->beta_r = json_number_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "beta_r : %f",
        kpriv->beta_r);
  }
  val = json_object_get (jconfig, "beta_g");
  if (!val || !json_is_number (val)) {
    kpriv->beta_g = DEFAULT_BETA_G;
    LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level, "beta_g not set."
        " taking default : %f", kpriv->beta_g);
  } else {
    kpriv->beta_g = json_number_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "beta_g : %f",
        kpriv->beta_g);
  }
  val = json_object_get (jconfig, "beta_b");
  if (!val || !json_is_number (val)) {
    kpriv->beta_b = DEFAULT_BETA_B;
    LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level, "beta_b not set."
        " taking default : %f", kpriv->beta_b);
  } else {
    kpriv->beta_b = json_number_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "beta_b : %f",
        kpriv->beta_b);
  }

  val = json_object_get (jconfig, "ppc");
  if (!val || !json_is_integer (val)) {
    kpriv->ppc = DEFAULT_PPC;
    LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level, "PPC not set."
        " taking default : %d", kpriv->ppc);
  } else {
    kpriv->ppc = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level, "PPC : %d", kpriv->ppc);
  }

  val = json_object_get (jconfig, "in_mem_bank");
  if (!val || !json_is_integer (val)) {
    kpriv->in_mem_bank = DEFAULT_MEM_BANK;
    LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
        "Input memory bank not set" " taking default : %d", kpriv->in_mem_bank);
  } else {
    kpriv->in_mem_bank = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level,
        "Input Memory Bank : %d", kpriv->in_mem_bank);
  }

  val = json_object_get (jconfig, "out_mem_bank");
  if (!val || !json_is_integer (val)) {
    kpriv->out_mem_bank = DEFAULT_MEM_BANK;
    LOG_MESSAGE (LOG_LEVEL_WARNING, kpriv->log_level,
        "Output memory bank not set" " taking default : %d",
        kpriv->out_mem_bank);
  } else {
    kpriv->out_mem_bank = json_integer_value (val);
    LOG_MESSAGE (LOG_LEVEL_INFO, kpriv->log_level,
        "Output Memory Bank : %d", kpriv->out_mem_bank);
  }

  for (chan_id = 0; chan_id < MAX_CHANNELS; chan_id++) {
    kpriv->msPtr[chan_id] = vvas_alloc_buffer (handle, DESC_SIZE,
        VVAS_INTERNAL_MEMORY, kpriv->in_mem_bank, NULL);
    if (!kpriv->msPtr[chan_id]) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, kpriv->log_level,
          "ERROR: VVAS MS: failed to allocate descriptor memory");
      return -1;
    }
  }

  handle->kernel_priv = (void *) kpriv;
  handle->is_multiprocess = 1;
  handle->in_mem_bank = kpriv->in_mem_bank;
  handle->out_mem_bank = kpriv->out_mem_bank;

  return 0;
}

uint32_t
xlnx_kernel_deinit (VVASKernel * handle)
{
  uint8_t chan_id;
  vvasMSKernelPriv *kpriv = (vvasMSKernelPriv *) handle->kernel_priv;

  for (chan_id = 0; chan_id < MAX_CHANNELS; chan_id++)
    vvas_free_buffer (handle, kpriv->msPtr[chan_id]);

  free (kpriv);
  handle->kernel_priv = NULL;

  return 0;
}

int32_t
xlnx_kernel_done (VVASKernel * handle)
{
  return 0;
}
