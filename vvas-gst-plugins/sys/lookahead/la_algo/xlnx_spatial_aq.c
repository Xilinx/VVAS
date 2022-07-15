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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include "xlnx_queue.h"
#include "xlnx_spatial_aq.h"
#include "xlnx_la_defines.h"

/*#define XLNX_MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
static const uint32_t XLNX_MIN_QP = 9;
static const uint32_t XLNX_MAX_QP = 0;
*/
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define X264_MAX(a,b) ( (a)>(b) ? (a) : (b) )
#define X264_MIN(a,b) ( (a)<(b) ? (a) : (b) )

#if defined(__GNUC__) && (__GNUC__ > 3 || __GNUC__ == 3 && __GNUC_MINOR__ > 3)
#define x264_clz(x) __builtin_clz(x)
#define x264_ctz(x) __builtin_ctz(x)
#else
static ALWAYS_INLINE int
x264_clz (uint32_t x)
{
  static uint8_t lut[16] = { 4, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
  int y, z = (((x >> 16) - 1) >> 27) & 16;
  x >>= z ^ 16;
  z += y = ((x - 0x100) >> 28) & 8;
  x >>= y ^ 8;
  z += y = ((x - 0x10) >> 29) & 4;
  x >>= y ^ 4;
  return z + lut[x];
}

static ALWAYS_INLINE int
x264_ctz (uint32_t x)
{
  static uint8_t lut[16] = { 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0 };
  int y, z = (((x & 0xffff) - 1) >> 27) & 16;
  x >>= z;
  z += y = (((x & 0xff) - 1) >> 28) & 8;
  x >>= y;
  z += y = (((x & 0xf) - 1) >> 29) & 4;
  x >>= y;
  return z + lut[x & 0xf];
}
#endif

/* Avoid an int/float conversion. */
const float x264_log2_lz_lut[32] = {
  31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13,
  12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
};

const float x264_log2_lut[128] = {
  0.00000, 0.01123, 0.02237, 0.03342, 0.04439, 0.05528, 0.06609, 0.07682,
  0.08746, 0.09803, 0.10852, 0.11894, 0.12928, 0.13955, 0.14975, 0.15987,
  0.16993, 0.17991, 0.18982, 0.19967, 0.20945, 0.21917, 0.22882, 0.23840,
  0.24793, 0.25739, 0.26679, 0.27612, 0.28540, 0.29462, 0.30378, 0.31288,
  0.32193, 0.33092, 0.33985, 0.34873, 0.35755, 0.36632, 0.37504, 0.38370,
  0.39232, 0.40088, 0.40939, 0.41785, 0.42626, 0.43463, 0.44294, 0.45121,
  0.45943, 0.46761, 0.47573, 0.48382, 0.49185, 0.49985, 0.50779, 0.51570,
  0.52356, 0.53138, 0.53916, 0.54689, 0.55459, 0.56224, 0.56986, 0.57743,
  0.58496, 0.59246, 0.59991, 0.60733, 0.61471, 0.62205, 0.62936, 0.63662,
  0.64386, 0.65105, 0.65821, 0.66534, 0.67243, 0.67948, 0.68650, 0.69349,
  0.70044, 0.70736, 0.71425, 0.72110, 0.72792, 0.73471, 0.74147, 0.74819,
  0.75489, 0.76155, 0.76818, 0.77479, 0.78136, 0.78790, 0.79442, 0.80090,
  0.80735, 0.81378, 0.82018, 0.82655, 0.83289, 0.83920, 0.84549, 0.85175,
  0.85798, 0.86419, 0.87036, 0.87652, 0.88264, 0.88874, 0.89482, 0.90087,
  0.90689, 0.91289, 0.91886, 0.92481, 0.93074, 0.93664, 0.94251, 0.94837,
  0.95420, 0.96000, 0.96578, 0.97154, 0.97728, 0.98299, 0.98868, 0.99435,
};

static ALWAYS_INLINE float
x264_log2 (uint32_t x)
{
  int lz = x264_clz (x);
  return x264_log2_lut[(x << lz >> 24) & 0x7f] + x264_log2_lz_lut[lz];
}

typedef struct spatial_cfg
{
  uint32_t width;
  uint32_t height;
  //int32_t outWidth;
  //int32_t outHeight;
  uint32_t actual_mb_w;
  uint32_t actual_mb_h;
  uint32_t padded_mb_w;
  uint32_t padded_mb_h;
  uint32_t blockWidth;
  uint32_t blockHeight;
  uint32_t la_depth;
  uint32_t spatial_aq_mode;
  float spatial_aq_gain;
  uint32_t num_mb;
  uint32_t qpmap_size;
} spatial_cfg_t;

typedef struct xlnx_spatial_ctx
{
  spatial_cfg_t cfg;
  xlnx_queue free_map_q;
  xlnx_queue ready_map_q;
} xlnx_spatial_ctx_t;

void
update_aq_gain (xlnx_spatial_aq_t sp, aq_config_t * cfg)
{
  xlnx_spatial_ctx_t *ctx = (xlnx_spatial_ctx_t *) sp;
  spatial_cfg_t *mycfg = &ctx->cfg;
  mycfg->spatial_aq_mode = cfg->spatial_aq_mode;
  if (mycfg->height < 720) {
    mycfg->spatial_aq_gain = 1;
  } else {
    mycfg->spatial_aq_gain = (float) (3.0 * cfg->spatial_aq_gain) / 100;
  }
  return;
}

static void
copy_config (xlnx_spatial_ctx_t * ctx, aq_config_t * cfg)
{
  spatial_cfg_t *mycfg = &ctx->cfg;
  mycfg->width = cfg->width;
  mycfg->height = cfg->height;
  //mycfg->outWidth = cfg->outWidth;
  //mycfg->outHeight = cfg->outHeight;
  mycfg->actual_mb_w = cfg->actual_mb_w;
  mycfg->actual_mb_h = cfg->actual_mb_h;
  mycfg->padded_mb_w = cfg->padded_mb_w;
  mycfg->padded_mb_h = cfg->padded_mb_h;
  mycfg->blockWidth = cfg->blockWidth;
  mycfg->blockHeight = cfg->blockHeight;
  mycfg->la_depth = cfg->la_depth;
  mycfg->spatial_aq_mode = cfg->spatial_aq_mode;
  if (mycfg->height < 720) {
    mycfg->spatial_aq_gain = 1;
  } else {
    mycfg->spatial_aq_gain = (float) (3.0 * cfg->spatial_aq_gain) / 100;
  }
  mycfg->num_mb = cfg->num_mb;
  mycfg->qpmap_size = cfg->qpmap_size;
}

xlnx_spatial_aq_t
xlnx_spatial_create (aq_config_t * cfg)
{
  uint32_t numL1Lcu;
  xlnx_spatial_ctx_t *ctx = NULL;
  uint32_t la_depth;
  spatial_qpmap_t smap;
  uint32_t i;

  ctx = calloc (1, sizeof (xlnx_spatial_ctx_t));
  if (!ctx) {
    printf ("%s OOM", __FUNCTION__);
    return NULL;
  }
  copy_config (ctx, cfg);
  numL1Lcu = cfg->num_mb;
  la_depth = ctx->cfg.la_depth;
  ctx->free_map_q = createQueue (la_depth + 1, sizeof (spatial_qpmap_t));
  if (ctx->free_map_q == NULL) {
    xlnx_spatial_destroy (ctx);
    printf ("%s OOM", __FUNCTION__);
    return NULL;
  }
  for (i = 0; i <= la_depth; i++) {
    int ass;
    smap.fPtr = (float *) calloc (1, numL1Lcu * sizeof (float));
    if (smap.fPtr == NULL) {
      xlnx_spatial_destroy (ctx);
      printf ("%s OOM", __FUNCTION__);
      return NULL;
    }
    smap.size = numL1Lcu * sizeof (float);
    smap.frame_num = 0;
    ass = PushQ (ctx->free_map_q, &smap);
    assert (0 == ass);
  }
  ctx->ready_map_q = createQueue (la_depth + 1, sizeof (spatial_qpmap_t));
  if (ctx->ready_map_q == NULL) {
    xlnx_spatial_destroy (ctx);
    printf ("%s OOM", __FUNCTION__);
    return NULL;
  }
  return ctx;
}

static void
free_map_q (xlnx_queue aQ)
{
  spatial_qpmap_t smap;

  if (aQ == NULL) {
    return;
  }
  while (!PopQ (aQ, &smap)) {
    free (smap.fPtr);
  }
  destroyQueue (aQ);
}

void
xlnx_spatial_destroy (xlnx_spatial_aq_t sp)
{
  xlnx_spatial_ctx_t *ctx = (xlnx_spatial_ctx_t *) sp;
  free_map_q (ctx->free_map_q);
  ctx->free_map_q = NULL;
  free_map_q (ctx->ready_map_q);
  ctx->ready_map_q = NULL;
  free (ctx);
}

xlnx_status
xlnx_spatial_gen_qpmap (xlnx_spatial_aq_t sp,
    const uint32_t * var_energy_map, const uint16_t * act_energy_map,
    uint64_t frame_num, uint32_t * frame_activity)
{
  xlnx_spatial_ctx_t *ctx = (xlnx_spatial_ctx_t *) sp;
  spatial_cfg_t *cfg = &ctx->cfg;
  float strength;
  float avg_adj = 0.f;
  uint32_t frameActivity = 0;
  int block_width = cfg->blockWidth;
  int div_factor = (block_width * block_width) / 4;
  uint32_t aq_mode = cfg->spatial_aq_mode;
  int i_decimate_factor = 2;
  int i_actual_mb_width = cfg->actual_mb_w;
  int i_actual_mb_height = cfg->actual_mb_h;
  int i_padded_mb_width = cfg->padded_mb_w;

  int i_mb_stride = i_padded_mb_width;
  int actual_mb_count = i_actual_mb_width * i_actual_mb_height;
  float f_aq_strength = cfg->spatial_aq_gain;
  int i_aq_prev_frame_enable = 0;
  float *spatial_aq_map;
  spatial_qpmap_t smap;
  int mb_x, mb_y;

  *frame_activity = 0;

  //Handle EOS
  if (((aq_mode == XLNX_AQ_SPATIAL_AUTOVARIANCE) && !var_energy_map) ||
      ((aq_mode == XLNX_AQ_SPATIAL_ACTIVITY) && !frame_activity)) {
    return EXlnxSuccess;
  }

  PopQ (ctx->free_map_q, &smap);
  spatial_aq_map = smap.fPtr;

  if ((i_decimate_factor == 2) && (aq_mode == XLNX_AQ_SPATIAL_ACTIVITY)) {
    div_factor = div_factor / 2;
  }

  if ((aq_mode == XLNX_AQ_SPATIAL_AUTOVARIANCE) ||
      (aq_mode == XLNX_AQ_SPATIAL_ACTIVITY)) {
    float avg_adj_pow2 = 0.f;
    for (mb_y = 0; mb_y < i_actual_mb_height; mb_y++) {
      for (mb_x = 0; mb_x < i_actual_mb_width; mb_x++) {
        uint32_t energy;
        float qp_adj;
        if (aq_mode == XLNX_AQ_SPATIAL_ACTIVITY) {
          energy = (uint32_t) act_energy_map[(i_padded_mb_width * mb_y) + mb_x];
          frameActivity += energy;      // ramdas
          qp_adj = x264_log2 (X264_MAX (energy, 1));
        } else if (aq_mode == XLNX_AQ_SPATIAL_AUTOVARIANCE) {
          //energy = ac_energy_mb( h, mb_x, mb_y, frame );
          energy = var_energy_map[(i_padded_mb_width * mb_y) + mb_x];
          qp_adj = (x264_log2 (X264_MAX (energy / div_factor, 1)) * 2) / 5;     //powf( energy * bit_depth_correction*mul_factor + 1, 0.125f );
        }
        spatial_aq_map[mb_x + mb_y * i_mb_stride] = qp_adj;
        avg_adj += qp_adj;
        avg_adj_pow2 += qp_adj * qp_adj;
      }
    }
    avg_adj /= actual_mb_count;
    avg_adj_pow2 /= actual_mb_count;
    /*printf("activity[]= %ld i_actual_mb_width=%d i_actual_mb_height=%d\n",
       frameActivity, i_actual_mb_width, i_actual_mb_height);// ramdas */

    strength = f_aq_strength;
    if (aq_mode == XLNX_AQ_SPATIAL_ACTIVITY) {
      if (i_decimate_factor == 2) {
        strength = strength / 1.15;
      }
      strength = strength * avg_adj;

      //vijayb: Below code to have good visual quality.
      //but this will make PSNR look bad on few streams and average figures may go bad.
      //Disable it if better PSNR numbers required
      /*if(strength<3.0) {
         strength = 3.0;
         } */
    } else if (aq_mode == XLNX_AQ_SPATIAL_AUTOVARIANCE) {
      strength = f_aq_strength * avg_adj;
      /*if(strength<3.0) {
         strength = 3.0;
         } */
      //printf("prev avg_adj=%f, avg_adj_pow2=%f h->actual_mb_count=%d\n",avg_adj, avg_adj_pow2, h->actual_mb_count);
      //avg_adj = avg_adj - 0.5f * (avg_adj_pow2 - modeTwoConst) / avg_adj;
      //printf("avg_adj=%f, avg_adj_pow2=%f h->actual_mb_count=%d\n",avg_adj, avg_adj_pow2, h->actual_mb_count);
    }
  } else {
    PushQ (ctx->free_map_q, &smap);
    return EXlnxSuccess;
  }

  for (mb_y = 0; mb_y < i_actual_mb_height; mb_y++) {
    for (mb_x = 0; mb_x < i_actual_mb_width; mb_x++) {
      float qp_adj;
      int mb_xy = mb_x + mb_y * i_mb_stride;

      if (i_aq_prev_frame_enable != 0) {
        /*printf("Error : Unsupported i_aq_prev_frame_enable=%d\n",
           i_aq_prev_frame_enable); */
        PushQ (ctx->free_map_q, &smap);
        return EXlnxError;
      }
      if (aq_mode == XLNX_AQ_SPATIAL_AUTOVARIANCE) {
        qp_adj = spatial_aq_map[mb_xy];
        qp_adj = strength * (qp_adj - avg_adj);
        //clipping
        if (qp_adj > 10.0f) {
          qp_adj = 10.0f;
        } else if (qp_adj < -10.0f) {
          qp_adj = -10.0f;
        }

      } else if (aq_mode == XLNX_AQ_SPATIAL_ACTIVITY) {
        qp_adj = spatial_aq_map[mb_xy];
        qp_adj = strength * (qp_adj - avg_adj);
        //clipping
        if (qp_adj > 10.0f) {
          qp_adj = 10.0f;
        } else if (qp_adj < -10.0f) {
          qp_adj = -10.0f;
        }
      }

      /*if( quant_offsets ) {
         qp_adj += quant_offsets[mb_xy];
         } */
      spatial_aq_map[mb_xy] = qp_adj;
    }
  }
  smap.frame_num = frame_num;
  *frame_activity = frameActivity;
  if (PushQ (ctx->ready_map_q, &smap)) {
    //printf("Error : PushQ ctx->ready_map_q\n");
  }
  /*printf("Spatial IN [%lu] out updated = %lu\n", frame_num,
     getSize(ctx->ready_map_q)); */
  return EXlnxSuccess;
}

xlnx_status
xlnx_spatial_recv_qpmap (xlnx_spatial_aq_t sp, spatial_qpmap_t * qpmap)
{
  xlnx_spatial_ctx_t *ctx = (xlnx_spatial_ctx_t *) sp;
  xlnx_status ret = EXlnxTryAgain;
  if (getSize (ctx->ready_map_q) > 0) {
    if (PopQ (ctx->ready_map_q, qpmap)) {
      //printf("Error : PopQ ctx->ready_map_q\n\n\n");
    }
    ret = EXlnxSuccess;
  }
  /*printf("Spatial OUT [%lu] out left = %lu\n", qpmap->frame_num,
     getSize(ctx->ready_map_q)); */
  return ret;
}

xlnx_status
xlnx_spatial_release_qpmap (xlnx_spatial_aq_t sp, spatial_qpmap_t * qpmap)
{
  xlnx_spatial_ctx_t *ctx = (xlnx_spatial_ctx_t *) sp;
  if (!ctx || !qpmap) {
    printf ("%s qpmap %p", __FUNCTION__, qpmap);
    return EXlnxError;
  }
  PushQ (ctx->free_map_q, qpmap);
  return EXlnxSuccess;
}

int
xlnx_spatial_is_ready (xlnx_spatial_aq_t sp)
{
  xlnx_spatial_ctx_t *ctx = (xlnx_spatial_ctx_t *) sp;
  if (!ctx) {
    printf ("%s ctx %p", __FUNCTION__, ctx);
    return -1;
  }
  return (getSize (ctx->ready_map_q) > 0);
}

int
xlnx_spatial_frame_activity (aq_config_t * cfg,
    const uint16_t * act_energy_map, uint32_t * fa)
{
  int i_actual_mb_width = cfg->actual_mb_w;
  int i_actual_mb_height = cfg->actual_mb_h;
  int i_padded_mb_width = cfg->padded_mb_w;
  uint32_t frameActivity = 0;
  uint32_t energy = 0;
  int mb_y, mb_x;

  if (!act_energy_map) {
    *fa = 0;
    return -1;
  }

  for (mb_y = 0; mb_y < i_actual_mb_height; mb_y++) {
    for (mb_x = 0; mb_x < i_actual_mb_width; mb_x++) {
      energy = (uint32_t) act_energy_map[(i_padded_mb_width * mb_y) + mb_x];
      frameActivity += energy;
    }
  }
  *fa = frameActivity;
  return 0;
}
