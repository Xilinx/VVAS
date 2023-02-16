/*
 * Copyright 2022 Xilinx, Inc.
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
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

#include <vvas/vvas_kernel.h>
#include "vvas_accel_utils.h"
#include <stdio.h>

static VvasVideoFormat
get_vvas_video_format (VVASVideoFormat fmt)
{
  VvasVideoFormat ret;

  switch (fmt) {
    case VVAS_VFMT_RGBX8:
      ret = VVAS_VIDEO_FORMAT_RGBx;
      break;
    case VVAS_VFMT_Y_UV8_420:
      ret = VVAS_VIDEO_FORMAT_Y_UV8_420;
      break;
    case VVAS_VFMT_RGB8:
      ret = VVAS_VIDEO_FORMAT_RGB;
      break;
    case VVAS_VFMT_BGR8:
      ret = VVAS_VIDEO_FORMAT_BGR;
      break;
    default:
      ret = VVAS_VIDEO_FORMAT_UNKNOWN;
  }

  return ret;
}

VvasVideoFrame *
vvas_videoframe_from_vvasframe (VvasContext * vvas_ctx,
    int8_t mbank_idx, VVASFrame * vframe)
{
  VvasVideoFramePriv *priv = NULL;
  VvasVideoInfo vinfo;
  uint8_t pidx;

  priv = (VvasVideoFramePriv *) calloc (1, sizeof (VvasVideoFramePriv));
  if (priv == NULL) {
    printf ("failed to allocate memory for  VvasVideoFrame");
    goto error;
  }

  priv->log_level = vvas_ctx->log_level;
  priv->num_planes = vframe->n_planes;
  priv->width = vinfo.width = vframe->props.width;
  priv->height = vinfo.height = vframe->props.height;
  priv->fmt = vinfo.fmt = get_vvas_video_format (vframe->props.fmt);
  vinfo.alignment.padding_left = vframe->props.alignment.padding_left;
  vinfo.alignment.padding_right = vframe->props.alignment.padding_right;
  vinfo.alignment.padding_top = vframe->props.alignment.padding_top;
  vinfo.alignment.padding_bottom = vframe->props.alignment.padding_bottom;

  priv->ctx = vvas_ctx;

  if (vvas_fill_planes (&vinfo, priv) < 0) {
    printf ("failed to do prepare plane info");
    goto error;
  }

  if (vvas_ctx->dev_handle) {
    priv->boh = vvas_xrt_create_sub_bo (vframe->pbo, priv->size, 0);
  }

  for (pidx = 0; pidx < priv->num_planes; pidx++) {
    if (vvas_ctx->dev_handle) {
      priv->planes[pidx].boh =
          vvas_xrt_create_sub_bo (priv->boh,
          priv->planes[pidx].size, priv->planes[pidx].offset);
    }
    priv->planes[pidx].data = vframe->vaddr[pidx];
  }

  /* Using the 0th index plane data ptr know if the virtual address is populated,
   * in which case it will be host buffer.
   */
  if (!priv->planes[0].data) {
    priv->mem_info.alloc_type = VVAS_ALLOC_TYPE_CMA;
  } else {
    priv->mem_info.alloc_type = VVAS_ALLOC_TYPE_NON_CMA;
  }

  priv->mem_info.alloc_flags = VVAS_ALLOC_FLAG_NONE;    /* currently gstreamer allocator supports both device and host memory allocation */
  priv->mem_info.mbank_idx = mbank_idx;
  priv->mem_info.sync_flags = VVAS_DATA_SYNC_NONE;
  priv->mem_info.map_flags = VVAS_DATA_MAP_NONE;

  return (VvasVideoFrame *) priv;

error:
  if (priv) {
    free (priv);
  }
  return NULL;

}
