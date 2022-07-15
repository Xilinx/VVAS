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
#include "xlnx_aq_core.h"
#include "xlnx_rc_aq.h"
#include "xlnx_spatial_aq.h"
#include "xlnx_la_defines.h"

#define MAX(a,b) ( (a)>(b) ? (a) : (b) )
#define MIN(a,b) ( (a)<(b) ? (a) : (b) )
#define MV_HIST_BINS 64

static const uint32_t XLNX_MIN_QP = 9;
static const uint32_t XLNX_MAX_QP = 0;

struct xlnx_aq_core_ctx;
typedef struct xlnx_tp_qpmap
{
  xlnx_aq_buf_t qpmap;
  uint32_t inUse;
  uint64_t frame_num;
} xlnx_tp_qpmap_t;

typedef void *xlnx_aq_dump_t;
typedef void *xlnx_qpmap_store_t;

static int32_t xlnx_temporal_gen_qpmap (struct xlnx_aq_core_ctx *ctx,
    const uint16_t * sadIn, uint32_t dynamic_gop, uint64_t frame_num,
    uint32_t * frameSAD, xlnx_tp_qpmap_t * outMapCtx);
static int32_t xlnx_temporal_gen_la_qpmap (struct xlnx_aq_core_ctx *ctx,
    xlnx_tp_qpmap_t * outMapCtx);

typedef enum
{
  ENoneType,
  EIType,
  EPType
} QpMapType;

typedef struct xlnx_qpmap_store_ctx
{
  xlnx_tp_qpmap_t *maps;
  uint32_t numQPMaps;
} xlnx_qpmap_store_ctx_t;

typedef struct xlnx_ap_dump_ctx
{
  xlnx_aq_dump_cfg dumpCfg;
  aq_config_t cfg;
} xlnx_ap_dump_ctx_t;

typedef struct xlnx_aq_core_ctx
{
  aq_config_t *cfg;
  uint64_t accumulatedSadFrames;
  uint8_t isDeltaQpMapLAPending;
  uint32_t *collocatedSadLA;
  xlnx_qpmap_store_t qpStore;
  xlnx_aq_dump_t dump_handle;
  uint32_t write_idx;
  uint32_t read_idx;
  xlnx_tp_qpmap_t *laMapCtx;
  uint32_t spatial_aq_mode[XLNX_DEFAULT_LA_DEPTH + 1];
  uint32_t temporal_aq_mode[XLNX_DEFAULT_LA_DEPTH + 1];
  uint32_t in_frame_num;
  uint32_t out_frame_num;
  uint32_t rate_control_mode;
  uint32_t num_b_frames[XLNX_DEFAULT_LA_DEPTH + 1];
  uint32_t init_b_frames;
  uint32_t prev_p;
  uint32_t num_mb;
  uint32_t lastIntraFrame;
  uint32_t nextIntraFrame;
  uint8_t qpmaps_enabled;
  xlnx_rc_aq_t rc_h;
  xlnx_spatial_aq_t sp_h;
  xlnx_codec_type_t codec_type;
  int32_t *tmp_hevc_map;
  int32_t mv_histogram[MV_HIST_BINS];
} xlnx_aq_core_ctx_t;

static xlnx_qpmap_store_t
create_qp_map_store (aq_config_t * cfg)
{
  xlnx_tp_qpmap_t *maps;
  xlnx_tp_qpmap_t *tp_qpmap;
  uint32_t numL1Lcu;
  uint32_t i;

  xlnx_qpmap_store_ctx_t *store = calloc (1, sizeof (xlnx_qpmap_store_ctx_t));
  if (!store) {
    return NULL;
  }
  store->numQPMaps = cfg->la_depth + 1;
  maps =
      (xlnx_tp_qpmap_t *) calloc (store->numQPMaps, sizeof (xlnx_tp_qpmap_t));
  if (!maps) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to allocate memory");
    free (store);
    return NULL;
  }
  //@TODO +1 recomemded???????
  numL1Lcu = cfg->num_mb + 1;

  for (i = 0; i < store->numQPMaps; i++) {
    xlnx_aq_buf_t *qpmap;

    tp_qpmap = &maps[i];
    qpmap = &tp_qpmap->qpmap;
    qpmap->ptr = (uint8_t *) calloc (1, sizeof (uint32_t) * numL1Lcu);
    qpmap->size = cfg->qpmap_size;
    tp_qpmap->inUse = 0;
    tp_qpmap->frame_num = 0;
  }
  LOG_MESSAGE (LOG_LEVEL_DEBUG, "width=%u height=%u lookahead=%u",
      cfg->outWidth, cfg->outHeight, cfg->la_depth);
  store->maps = maps;
  return (xlnx_qpmap_store_t) store;
}

static xlnx_tp_qpmap_t *
get_qp_map_at (xlnx_qpmap_store_t handle, uint32_t idx)
{
  xlnx_qpmap_store_ctx_t *store = (xlnx_qpmap_store_ctx_t *) handle;
  if (idx >= store->numQPMaps) {
    return NULL;
  }
  return &store->maps[idx];
}

static void
destroy_qp_map_store (xlnx_qpmap_store_t handle)
{
  xlnx_tp_qpmap_t *xlnx_tp_qpmap_t;
  xlnx_qpmap_store_ctx_t *store = (xlnx_qpmap_store_ctx_t *) handle;
  uint32_t i;

  for (i = 0; i < store->numQPMaps; i++) {
    xlnx_tp_qpmap_t = &store->maps[i];
    free (xlnx_tp_qpmap_t->qpmap.ptr);
  }
  free (store->maps);
  store->maps = NULL;
  free (store);
}

static xlnx_aq_dump_t
create_aq_dump_handle (xlnx_aq_dump_cfg * dumpCfg, aq_config_t * cfg)
{
  xlnx_ap_dump_ctx_t *ctx = calloc (1, sizeof (xlnx_ap_dump_ctx_t));
  if (!ctx) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to allocate memory");
    return NULL;
  }
  ctx->cfg = *cfg;
  ctx->dumpCfg = *dumpCfg;
  if (dumpCfg->outPath && dumpCfg->dumpDeltaQpMapHex) {
    char strbuf[512];

    if (system ("mkdir output")) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "Failed to create output dir");
    }
#if MULTI_CHANNEL_DUMP
    sprintf (strbuf, "mkdir -p output/%u_%u_%p/",
        cfg->outWidth * cfg->blockWidth, cfg->outHeight * cfg->blockHeight,
        ctx);
#else
    sprintf (strbuf, "mkdir -p output/%u_%u/", cfg->outWidth * cfg->blockWidth,
        cfg->outHeight * cfg->blockHeight);
#endif
    if (system (strbuf)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "Failed to execute %s", strbuf);
    }
  }
  return (xlnx_aq_dump_t) ctx;
}

static int32_t
destroy_aq_dump_handle (xlnx_aq_dump_t handle)
{
  xlnx_ap_dump_ctx_t *ctx = (xlnx_ap_dump_ctx_t *) handle;
  if (ctx) {
    free (ctx);
  }
  return 0;
}

#if DUMP_FRAME_BLOCK_SAD
static int32_t
dump_frame_block_sad (xlnx_aq_dump_t handle,
    uint64_t frame_num, const uint16_t * frameSAD, size_t size)
{
  xlnx_ap_dump_ctx_t *ctx = (xlnx_ap_dump_ctx_t *) handle;
  xlnx_aq_dump_cfg *dumpCfg = &ctx->dumpCfg;
  if (dumpCfg->dumpBlockSAD) {
    char strbuf[512];
    sprintf (strbuf, "output/BlockSAD_%04ld.bin", frame_num);
    const char *fileName = strbuf;
    FILE *frameSAD_f = fopen (fileName, "wb");
    if (NULL == frameSAD_f) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "Failed to open file %s", fileName);
      return (-1);
    }
    fwrite (frameSAD, sizeof (uint16_t), size, frameSAD_f);
    fclose (frameSAD_f);
  }
  return 0;
}
#endif //DUMP_FRAME_BLOCK_SAD

static int32_t
dump_frame_delta_qp_map (xlnx_aq_dump_t handle,
    uint8_t * deltaQpMap, uint32_t dump_length, uint64_t frame_num,
    uint8_t isLA)
{
  //printf("dump_frame_delta_qp_map frame_num=%lu isLA=%d\n", frame_num, isLA);
  FILE *f_DeltaQpMap = NULL;
  FILE *f_DeltaQpMapHex = NULL;
  xlnx_ap_dump_ctx_t *ctx = (xlnx_ap_dump_ctx_t *) handle;
  xlnx_aq_dump_cfg *dumpCfg = &ctx->dumpCfg;
  aq_config_t *cfg = &ctx->cfg;
  uint32_t idx;
  char strbuf[512];
  const char *fileName;

  /*printf("dump_frame_delta_qp_map frame_num=%lu isLA=%d dump_length=%u\n",
     frame_num, isLA, dump_length); */
  if (dumpCfg->dumpDeltaQpMap) {
    if (isLA) {
      sprintf (strbuf, "output/%s_LA-delta_QP_map_frame%ld.csv",
          dumpCfg->outPath, frame_num);
    } else {
      sprintf (strbuf, "output/%s_deltaQp_map_frame%ld.csv", dumpCfg->outPath,
          frame_num);
    }
    fileName = strbuf;
    f_DeltaQpMap = fopen (fileName, "wb");
    if (NULL == f_DeltaQpMap) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "Failed to open file %s", fileName);
      return (-1);
    }
  }
  if (dumpCfg->dumpDeltaQpMapHex) {
#if MULTI_CHANNEL_DUMP
    sprintf (strbuf, "output/%u_%u_%p/QP_%ld.hex",
        cfg->outWidth * cfg->blockWidth, cfg->outHeight * cfg->blockHeight, ctx,
        frame_num);
#else
    sprintf (strbuf, "output/%u_%u/QP_%ld.hex", cfg->outWidth * cfg->blockWidth,
        cfg->outHeight * cfg->blockHeight, frame_num);
#endif
    fileName = strbuf;
    f_DeltaQpMapHex = fopen (fileName, "wb");
    if (NULL == f_DeltaQpMapHex) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "Failed to open file %s", fileName);

      if (f_DeltaQpMap) {
        fclose (f_DeltaQpMap);
      }
      return (-1);
    }
  }
  for (idx = 0; idx < dump_length; idx++) {
    if (dumpCfg->dumpDeltaQpMap) {
      fprintf (f_DeltaQpMap, ",%d", deltaQpMap[idx]);
    }
    if (dumpCfg->dumpDeltaQpMapHex) {
      fprintf (f_DeltaQpMapHex, "%02X\n", deltaQpMap[idx]);
    }
    if (dumpCfg->dumpDeltaQpMap) {
      fprintf (f_DeltaQpMap, "\n");
    }
  }
  if (f_DeltaQpMap) {
    fclose (f_DeltaQpMap);
  }
  if (f_DeltaQpMapHex) {
    fclose (f_DeltaQpMapHex);
  }
  return 0;
}

static void
init_aq_modes (xlnx_aq_core_ctx_t * ctx, aq_config_t * cfg)
{
  int32_t i = 0;

  for (i = 0; i < (XLNX_DEFAULT_LA_DEPTH + 1); i++) {
    ctx->spatial_aq_mode[i] = cfg->spatial_aq_mode;
    ctx->temporal_aq_mode[i] = cfg->temporal_aq_mode;
  }
  ctx->in_frame_num = 0;
  ctx->out_frame_num = 0;
  return;
}

void
update_num_b_frames (xlnx_aq_core_t handle, aq_config_t * cfg)
{
  xlnx_aq_core_ctx_t *ctx = (xlnx_aq_core_ctx_t *) handle;
  ctx->num_b_frames[ctx->in_frame_num % (XLNX_DEFAULT_LA_DEPTH + 1)] =
      cfg->num_b_frames;
  return;
}

void
update_aq_modes (xlnx_aq_core_t handle, aq_config_t * cfg)
{
  xlnx_aq_core_ctx_t *ctx = (xlnx_aq_core_ctx_t *) handle;
  ctx->spatial_aq_mode[ctx->in_frame_num % (XLNX_DEFAULT_LA_DEPTH + 1)] =
      cfg->spatial_aq_mode;
  ctx->temporal_aq_mode[ctx->in_frame_num % (XLNX_DEFAULT_LA_DEPTH + 1)] =
      cfg->temporal_aq_mode;
  update_num_b_frames (handle, cfg);

  update_aq_gain (ctx->sp_h, cfg);
  return;
}

xlnx_aq_core_t
create_aq_core (aq_config_t * cfg, xlnx_aq_dump_cfg * dumpCfg)
{
  uint32_t numL1Lcu;
  int32_t i;
  xlnx_aq_core_ctx_t *ctx = calloc (1, sizeof (xlnx_aq_core_ctx_t));
  if (!ctx) {
    return NULL;
  }

  LOG_MESSAGE (LOG_LEVEL_DEBUG, "config : w = %u, h = %u, ow = %u, oh = %u, "
      "actual_mb_w = %u, actual_mb_h = %u, padded_mb_w = %u, padded_mb_h = %u"
      "blockWidth = %u, blockHeight = %u, intraPeriod = %u, la_depth = %u, spatial_aq_mode = %u, spatial_aq_gain = %u, "
      "temporal_aq_mode = %u, rate_control_mode = %u, num_mb = %u, qpmap_size = %u, num_b_frames = %u, codec_type = %u",
      cfg->width, cfg->height, cfg->outWidth, cfg->outHeight, cfg->actual_mb_w,
      cfg->actual_mb_h, cfg->padded_mb_w, cfg->padded_mb_h, cfg->blockWidth,
      cfg->blockHeight, cfg->intraPeriod, cfg->la_depth, cfg->spatial_aq_mode,
      cfg->spatial_aq_gain, cfg->temporal_aq_mode, cfg->rate_control_mode,
      cfg->num_mb, cfg->qpmap_size, cfg->num_b_frames, cfg->codec_type);

  ctx->cfg = cfg;
  ctx->num_mb = cfg->num_mb;
  numL1Lcu = ctx->num_mb + 1;
  ctx->isDeltaQpMapLAPending = 1;
  init_aq_modes (ctx, cfg);
  ctx->rate_control_mode = cfg->rate_control_mode;
  ctx->codec_type = cfg->codec_type;
  ctx->qpmaps_enabled = 1;

  for (i = 0; i < (XLNX_DEFAULT_LA_DEPTH + 1); i++) {
    ctx->num_b_frames[i] = cfg->num_b_frames;
  }
  ctx->init_b_frames = cfg->num_b_frames;
  ctx->prev_p = 0;
  ctx->rc_h = NULL;
  ctx->sp_h = NULL;
  ctx->qpStore = NULL;
  ctx->tmp_hevc_map = NULL;
  ctx->collocatedSadLA = NULL;

  if (ctx->rate_control_mode > 0) {
    ctx->rc_h = xlnx_algo_rc_create (cfg->la_depth);
    if (ctx->rc_h == NULL) {
      destroy_aq_core (ctx);
      LOG_MESSAGE (LOG_LEVEL_ERROR, "xlnx_algo_rc_create Failed");
      return NULL;
    }
  }
  ctx->collocatedSadLA = malloc (sizeof (uint32_t) * numL1Lcu);
  if (!ctx->collocatedSadLA) {
    destroy_aq_core (ctx);
    LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to allocate memory");
    return NULL;
  }
  memset (ctx->collocatedSadLA, 0, sizeof (uint32_t) * numL1Lcu);
  ctx->qpStore = create_qp_map_store (cfg);
  if (!ctx->qpStore) {
    destroy_aq_core (ctx);
    LOG_MESSAGE (LOG_LEVEL_ERROR, "failed create qpmap store");
    return NULL;
  }
  if ((ctx->codec_type == EXlnxHevc) && (ctx->tmp_hevc_map == NULL)) {
    ctx->tmp_hevc_map = calloc (1, sizeof (int32_t) * cfg->qpmap_size);
    if (ctx->tmp_hevc_map == NULL) {
      destroy_aq_core (ctx);
      LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to allocate memory");
      return NULL;
    }
  }
  ctx->read_idx = 1;
  ctx->write_idx = 1;
  ctx->laMapCtx = get_qp_map_at (ctx->qpStore, 0);

  if (dumpCfg) {
    ctx->dump_handle = create_aq_dump_handle (dumpCfg, cfg);
  } else {
    ctx->dump_handle = NULL;
  }
  ctx->sp_h = xlnx_spatial_create (cfg);
  if (ctx->sp_h == NULL) {
    destroy_aq_core (ctx);
    LOG_MESSAGE (LOG_LEVEL_ERROR, "xlnx_spatial_create Failed");
    return NULL;
  }
  if ((ctx->codec_type == EXlnxHevc) && (ctx->tmp_hevc_map == NULL)) {
    ctx->tmp_hevc_map = calloc (1, sizeof (int32_t) * cfg->qpmap_size);
    if (ctx->tmp_hevc_map == NULL) {
      destroy_aq_core (ctx);
      LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to allocate memory");
      return NULL;
    }
  }

  return (xlnx_aq_core_t) ctx;
}

void
destroy_aq_core (xlnx_aq_core_t handle)
{
  xlnx_aq_core_ctx_t *ctx = (xlnx_aq_core_ctx_t *) handle;
  if (ctx->collocatedSadLA) {
    free (ctx->collocatedSadLA);
    ctx->collocatedSadLA = NULL;
  }
  if (ctx->dump_handle) {
    destroy_aq_dump_handle (ctx->dump_handle);
    ctx->dump_handle = NULL;
  }
  if (ctx->qpStore) {
    destroy_qp_map_store (ctx->qpStore);
    ctx->qpStore = NULL;
  }
  if (ctx->rc_h) {
    xlnx_algo_rc_destroy (ctx->rc_h);
    ctx->rc_h = NULL;
  }
  if (ctx->sp_h) {
    xlnx_spatial_destroy (ctx->sp_h);
    ctx->sp_h = NULL;
  }
  if (ctx->tmp_hevc_map) {
    free (ctx->tmp_hevc_map);
    ctx->tmp_hevc_map = NULL;
  }
  free (ctx);
}

static inline uint8_t
getQpHexByte (int32_t qp_value_32)
{
  uint8_t qp_value_8;
  if (qp_value_32 < 0) {
    qp_value_8 = (uint8_t) (64 + qp_value_32);
  } else {
    qp_value_8 = (uint8_t) qp_value_32;
  }
  return qp_value_8;
}

static inline int
getFrameSad (xlnx_aq_core_ctx_t * ctx, const uint16_t * sadIn,
    uint32_t * frameSAD)
{
  const uint16_t *sad = sadIn;
  uint32_t frame_sad = 0;
  aq_config_t *cfg = ctx->cfg;
  uint32_t blockWidth = cfg->blockWidth;
  uint32_t blockHeight = cfg->blockHeight;
  int32_t outWidth = cfg->outWidth;
  int32_t outHeight = cfg->outHeight;
  int x, y;

  *frameSAD = 0;

  if (!sad) {
    return -1;
  }

  for (y = 0; y < outHeight; y += blockHeight) {
    for (x = 0; x < outWidth; x += blockWidth) {
      frame_sad += *sad;
      sad++;
    }
  }
  LOG_MESSAGE (LOG_LEVEL_DEBUG, "calculated framesad=%u", frame_sad);
  *frameSAD = frame_sad;
  return 0;
}

static int32_t
xlnx_temporal_gen_qpmap (xlnx_aq_core_ctx_t * ctx,
    const uint16_t * sadIn, uint32_t dynamic_gop,
    uint64_t frame_num, uint32_t * frameSAD, xlnx_tp_qpmap_t * outMapCtx)
{
  int32_t avgBlockSad;
  int32_t minBlockSad, maxBlockSad, maxBlockDistance, absMinBlockDistance;
  int32_t minBlockDistance;
  int32_t x, y = 0;
  int32_t colIn = 0;
  int32_t tmp_qp = 0;
  int32_t *deltaQpMap = NULL;
  aq_config_t *cfg = ctx->cfg;
  uint32_t blockWidth = cfg->blockWidth;
  uint32_t blockHeight = cfg->blockHeight;
  int32_t outWidth = cfg->outWidth;
  int32_t outHeight = cfg->outHeight;
  //uint32_t intraPeriod = cfg->intraPeriod;
  xlnx_tp_qpmap_t *xlnx_tp_qpmap_t = outMapCtx;
  uint32_t numL1Lcu = ctx->num_mb + 1;

  const uint16_t *sad = sadIn;
  uint32_t minQp, maxQp;
  uint32_t dndx = 0;
  int32_t diffSad;
  int32_t hgt, wth;
  uint32_t num_b_frames;
  uint32_t prev_b_frames;

  if (frame_num == 0) {
    *frameSAD = 0;
    return 0;
  }

  minBlockSad = 0xFFFF;
  maxBlockSad = 0;
  *frameSAD = 0;

  LOG_MESSAGE (LOG_LEVEL_DEBUG, "frame_num=%lu ctx->isDeltaQpMapLAPending=%u",
      frame_num, ctx->isDeltaQpMapLAPending);

  for (y = 0; y < outHeight; y += blockHeight) {
    for (x = 0; x < outWidth; x += blockWidth) {
      ctx->collocatedSadLA[colIn++] += *sad;
      *frameSAD += *sad;
      minBlockSad = (minBlockSad < *sad) ? minBlockSad : *sad;
      maxBlockSad = (maxBlockSad > *sad) ? maxBlockSad : *sad;
      sad++;
    }
  }

  avgBlockSad = *frameSAD / numL1Lcu;
  LOG_MESSAGE (LOG_LEVEL_DEBUG,
      "frameSad[%lu]= %u; minBlockSad=%d maxBlockSad=%d assumed blocks=%u actual blocks=%u",
      frame_num, *frameSAD, minBlockSad, maxBlockSad, numL1Lcu, colIn);
  minBlockDistance = abs (minBlockSad - avgBlockSad);
  absMinBlockDistance = minBlockDistance;
  maxBlockDistance = maxBlockSad - avgBlockSad;

  if (minBlockDistance < maxBlockDistance) {
    maxBlockDistance = minBlockDistance;
    minBlockDistance = (int32_t) (0 - minBlockDistance);
  } else {
    minBlockDistance = (int32_t) (0 - maxBlockDistance);
  }

  absMinBlockDistance = (absMinBlockDistance == 0) ? 1 : absMinBlockDistance;
  maxBlockDistance = (maxBlockDistance == 0) ? 1 : maxBlockDistance;


  num_b_frames = ctx->num_b_frames[frame_num % (XLNX_DEFAULT_LA_DEPTH + 1)];
  prev_b_frames =
      ctx->num_b_frames[(frame_num - 1) % (XLNX_DEFAULT_LA_DEPTH + 1)];

  /* initialize min and max qp for dynamic gop case */
  minQp = 2;
  maxQp = 2;

  if (!dynamic_gop) {           /* When dynamic gop is enabled there will be no difference for P and B frames (they will have the same delta qp range) */
    /* If number of B frames changes at run time, need to adjust the previous
       P frame accordingly for correct P and B frame prediction. VCU encoder
       always buffers initial B frames and when number of B frame changes, it
       applies from the immediate sub GOP instead of the sub GOP that follows
       the frame number at which the user changes B frames. */

    /* Example: Initial B frames is 4, user changes to 1 B frame at frame 100.
       When 100th frame is given to the encoder, it is yet to encode 96th frame.
       Since the sub GOP ends at 95th frame(P frame), it applies new B frame
       changes from 96th frame encoding 96 as B, 97 as P and so on. */

    if (prev_b_frames != num_b_frames) {
      uint32_t enc_gop_frame = frame_num - (ctx->init_b_frames + 1);

      if (enc_gop_frame != ctx->prev_p) {
        /* The below loop updates the previous P frame according to the
           encoder current state */
        while ((ctx->prev_p - (prev_b_frames + 1)) >= enc_gop_frame) {
          ctx->prev_p -= (prev_b_frames + 1);
        }
      }

      /* Adjust the prev_p closest to the frame number based on new sub GOP */
      while ((ctx->prev_p + (num_b_frames + 1)) < frame_num) {
        ctx->prev_p += (num_b_frames + 1);
      }
    }

    /* Adjust the previous P for new GOP structure */
    if ((frame_num == 0) || ((frame_num - ctx->prev_p) == (num_b_frames + 1)) ||
        (frame_num == ctx->nextIntraFrame)) {
      minQp = 6;
      maxQp = 3;
      ctx->prev_p = frame_num;
    } else {
      minQp = 2;
      maxQp = 2;
    }
  }

  deltaQpMap = (int32_t *) xlnx_tp_qpmap_t->qpmap.ptr;
  sad = sadIn;
  for (hgt = 0; hgt < outHeight; hgt += blockHeight) {
    for (wth = 0; wth < outWidth; wth += blockWidth) {
      diffSad = (int32_t) (*sad - avgBlockSad);

      if ((int32_t) diffSad <= 0) {
        tmp_qp = 0 - (int32_t) (minQp * XLNX_MIN (avgBlockSad - *sad,
                absMinBlockDistance) / absMinBlockDistance);
      } else {
        tmp_qp = (int32_t) (maxQp * XLNX_MIN (diffSad,
                maxBlockDistance) / maxBlockDistance);
      }
      deltaQpMap[dndx++] = tmp_qp;
      sad++;
    }
  }

  if (frame_num != ctx->nextIntraFrame) {
    xlnx_tp_qpmap_t->inUse = 1;
    xlnx_tp_qpmap_t->frame_num = frame_num;
  }
#if DUMP_FRAME_BLOCK_SAD
  if (ctx->dump_handle && sadIn) {
    dump_frame_block_sad (ctx->dump_handle, frame_num, sadIn, numL1Lcu - 1);
  }
#endif //DUMP_FRAME_BLOCK_SAD
  return 0;
}

static int32_t
xlnx_temporal_gen_la_qpmap (xlnx_aq_core_ctx_t * ctx,
    xlnx_tp_qpmap_t * outMapCtx)
{
  uint32_t minQp, maxQp;
  int32_t colIn = 0;
  uint64_t accLaSad = 0;
  int32_t avgLaSad = 0;
  int32_t tmp_qp = 0;
  uint32_t minLaBlockSad, maxLaBlockSad;
  int32_t minLaDistance, maxLaDistance;
  aq_config_t *cfg = ctx->cfg;
  uint32_t blockWidth = cfg->blockWidth;
  uint32_t blockHeight = cfg->blockHeight;
  int32_t outWidth = cfg->outWidth;
  int32_t outHeight = cfg->outHeight;
  int32_t *deltaQpMapLA = NULL;
  xlnx_tp_qpmap_t *laMapCtx = outMapCtx;
  uint32_t numL1Lcu = ctx->num_mb + 1;
  uint32_t absMinLaDistance;
  uint32_t dnx = 0;
  int32_t diffSadLa;
  int32_t hgt, wth;
  accLaSad = 0;
  minLaBlockSad = 0xFFFF;
  maxLaBlockSad = 0;

  for (hgt = 0; hgt < outHeight; hgt += blockHeight) {
    for (wth = 0; wth < outWidth; wth += blockWidth) {
      accLaSad += ctx->collocatedSadLA[colIn];
      minLaBlockSad =
          (minLaBlockSad <
          ctx->
          collocatedSadLA[colIn]) ? minLaBlockSad : ctx->collocatedSadLA[colIn];
      maxLaBlockSad =
          (maxLaBlockSad >
          ctx->
          collocatedSadLA[colIn]) ? maxLaBlockSad : ctx->collocatedSadLA[colIn];
      colIn++;
    }
  }
  avgLaSad = accLaSad / numL1Lcu;
  //printf("avgLaSad %d\t minLaBlockSad %d\t maxLaBlockSad %d\n", avgLaSad, minLaBlockSad, maxLaBlockSad);

  minLaDistance = abs (minLaBlockSad - avgLaSad);
  maxLaDistance = maxLaBlockSad - avgLaSad;

  //printf("minLaDistance %d\t maxLaDistance %d\n", minLaDistance, maxLaDistance);

  if (minLaDistance < maxLaDistance) {
    maxLaDistance = minLaDistance;
    absMinLaDistance = minLaDistance;
    minLaDistance = (int32_t) (0 - minLaDistance);
  } else {
    absMinLaDistance = maxLaDistance;
    minLaDistance = (int32_t) (0 - maxLaDistance);
  }

  absMinLaDistance = (absMinLaDistance == 0) ? 1 : absMinLaDistance;
  maxLaDistance = (maxLaDistance == 0) ? 1 : maxLaDistance;

  // Adjust AQ weights here
  minQp = XLNX_MIN_QP;
  maxQp = XLNX_MAX_QP;          // For intra frames do not increase the QP

  colIn = 0;

  if (laMapCtx->inUse != 0) {
    LOG_MESSAGE (LOG_LEVEL_ERROR,
        "Intra qpmap buf(frame_num=%lu) already occupied", laMapCtx->frame_num);
    return -1;
  }
  deltaQpMapLA = (int32_t *) laMapCtx->qpmap.ptr;
  for (hgt = 0; hgt < outHeight; hgt += blockHeight) {
    for (wth = 0; wth < outWidth; wth += blockWidth) {
      diffSadLa = (int32_t) (ctx->collocatedSadLA[colIn] - avgLaSad);

      if ((int32_t) diffSadLa < 0) {
        tmp_qp = 0 - (int32_t) (minQp * XLNX_MIN (avgLaSad -
                ctx->collocatedSadLA[colIn],
                absMinLaDistance) / absMinLaDistance);
      } else {
        tmp_qp = (int32_t) (maxQp * XLNX_MIN (diffSadLa,
                maxLaDistance) / maxLaDistance);
      }
      deltaQpMapLA[dnx++] = tmp_qp;
      colIn++;
    }
  }
  ctx->accumulatedSadFrames = 0;
  memset (ctx->collocatedSadLA, 0, sizeof (uint32_t) * numL1Lcu);
  return 0;
}

static void
merge_qp_maps (xlnx_aq_core_ctx_t * ctx, int32_t * temporal_qpmap,
    float *spatial_qpmap, uint32_t num_mb, uint8_t * out_map)
{
  aq_config_t *cfg = ctx->cfg;
  uint32_t padded_w = cfg->padded_mb_w;
  uint32_t actual_w = cfg->actual_mb_w;
  int32_t temporal = 0;
  float spatial = 0;
  int32_t last_deltaQp = 0;
  uint8_t out_byte = 0;
  uint8_t remove_padding = 0;
  uint32_t w_size = 0;
  uint32_t i;

  if (padded_w != actual_w) {
    remove_padding = 1;
    //printf("padded_w = %u actual_w=%u\n", padded_w, actual_w);
  }
  for (i = 0; i < num_mb; i++) {
    if (temporal_qpmap) {
      temporal = temporal_qpmap[i];
    }
    if (spatial_qpmap) {
      int32_t finaldeltaQP;

      spatial = spatial_qpmap[i];
      finaldeltaQP = temporal + (spatial + 0.5);
      if (i != 0) {
        finaldeltaQP = abs (finaldeltaQP - last_deltaQp) == 1 ? last_deltaQp :
            finaldeltaQP;
      }

      last_deltaQp = finaldeltaQP;
      if (remove_padding) {
        if ((i % padded_w) >= actual_w) {
          continue;
        }
      }
      out_byte = getQpHexByte (finaldeltaQP);
    } else {
      out_byte = getQpHexByte (temporal);
      if (remove_padding) {
        if (((i + 1) % padded_w) == actual_w) {
          //printf("skip @ i=%u by %u \n", i, padded_w - actual_w);
          i = i + padded_w - actual_w;

        }
      }
    }
    out_map[w_size] = out_byte;
    w_size++;
    if (w_size == cfg->qpmap_size) {
      //printf("break at i=%u num_mb = %u map_size =%u %u \n",i, num_mb, w_size, cfg->qpmap_size);
      break;
    }
  }
}

static void
merge_qp_maps_hevc (xlnx_aq_core_ctx_t * ctx, int32_t * temporal_qpmap,
    float *spatial_qpmap, uint32_t num_mb, uint8_t * out_map)
{
  aq_config_t *cfg = ctx->cfg;
  uint32_t padded_w = cfg->padded_mb_w;
  uint32_t actual_w = cfg->actual_mb_w;
  uint32_t actual_h = cfg->actual_mb_h;
  int32_t temporal = 0;
  float spatial = 0;
  int32_t last_deltaQp = 0;
  uint8_t remove_padding = 0;
  uint32_t w_size = 0;
  int32_t *hevc_map = ctx->tmp_hevc_map;
  int32_t out_qp = 0;
  int32_t out_idx = 0;
  uint32_t i, w, h;

  if (padded_w != actual_w) {
    remove_padding = 1;
    //printf("padded_w = %u actual_w=%u\n", padded_w, actual_w);
  }
  for (i = 0; i < num_mb; i++) {
    if (temporal_qpmap) {
      temporal = temporal_qpmap[i];
    }
    if (spatial_qpmap) {
      int32_t finaldeltaQP;

      spatial = spatial_qpmap[i];
      finaldeltaQP = temporal + (spatial + 0.5);
      if (i != 0) {
        finaldeltaQP = abs (finaldeltaQP - last_deltaQp) == 1 ? last_deltaQp :
            finaldeltaQP;
      }

      last_deltaQp = finaldeltaQP;
      if (remove_padding) {
        if ((i % padded_w) >= actual_w) {
          continue;
        }
      }
      out_qp = finaldeltaQP;
    } else {
      out_qp = temporal;
      if (remove_padding) {
        if (((i + 1) % padded_w) == actual_w) {
          //printf("skip @ i=%u by %u \n", i, padded_w - actual_w);
          i = i + padded_w - actual_w;

        }
      }
    }
    hevc_map[w_size] = out_qp;
    w_size++;
    if (w_size == cfg->qpmap_size) {
      //printf("break at i=%u num_mb = %u map_size =%u %u \n",i, num_mb, w_size, cfg->qpmap_size);
      break;
    }
  }

  // average values to derive final values
  for (h = 0; h < actual_h; h += 2) {
    for (w = 0; w < actual_w; w += 2) {
      int avgdeltaQP = hevc_map[(h * actual_w) + w];
      int countMbs = 1;

      if ((w + 1) < actual_w) {
        avgdeltaQP += hevc_map[(h * actual_w) + w + 1];
        countMbs++;
      }
      if ((h + 1) < actual_h) {
        avgdeltaQP += hevc_map[((h + 1) * actual_w) + w];
        countMbs++;
      }

      if (((w + 1) < actual_w) && ((h + 1) < actual_h)) {
        avgdeltaQP += hevc_map[((h + 1) * actual_w) + w + 1];
        countMbs++;
      }

      avgdeltaQP = avgdeltaQP / countMbs;
      out_map[out_idx++] = getQpHexByte (avgdeltaQP);
    }
  }
}

/* This function calculates the histogram of motion vectors coming from the
   lookahead. Using the histograme, amount of motion in the frame (low,
   medium and high) is also computed.*/
xlnx_status
generate_mv_histogram (xlnx_aq_core_t handle, uint64_t frame_num,
    const uint32_t * mvIn, uint32_t isLastFrame,
    int32_t * frame_complexity, int32_t is_idr)
{
  xlnx_aq_core_ctx_t *ctx = (xlnx_aq_core_ctx_t *) handle;
  aq_config_t *cfg = ctx->cfg;

  const uint32_t *mv = mvIn;

  uint32_t x, y;
  int16_t mv_x, mv_y;
  uint16_t mv_index;
  uint32_t sum_hist, thres_hist, seven_eighth_hist, three_forth_hist;
  uint32_t sum_2Bins;

  uint32_t blockWidth = cfg->blockWidth;
  uint32_t blockHeight = cfg->blockHeight;
  int32_t outWidth = cfg->outWidth;
  int32_t outHeight = cfg->outHeight;
  *frame_complexity = 0;

  /* reset mv_hist buffer */
  for (x = 0; x < MV_HIST_BINS; x++)
    ctx->mv_histogram[x] = 0;


  /* unpacking the mv_x and mv_y info from the 32 bit and compute histogram */
  for (y = 0; y < outHeight; y += blockHeight) {
    for (x = 0; x < outWidth; x += blockWidth) {
      mv_x = (int16_t) ((*mv) & 0x0000FFFF);
      mv_y = (int16_t) (((*mv) >> 16) & 0x0000FFFF);
      mv_index =
          MIN ((uint16_t) (MV_HIST_BINS - 1), (uint16_t) (MAX (abs (mv_x),
                  abs (mv_y)) >> 1));
      ctx->mv_histogram[mv_index]++;

      mv++;
    }
  }

  /* Determine frame complexity */
  sum_hist = 0;
  for (x = 0; x < MV_HIST_BINS; x++)
    sum_hist += ctx->mv_histogram[x];

  three_forth_hist = (sum_hist * 3) >> 2;
  seven_eighth_hist = (sum_hist * 7) >> 3;
  thres_hist = sum_hist >> 1;

  if (ctx->codec_type == EXlnxAvc) {
    sum_hist = ctx->mv_histogram[0] + ctx->mv_histogram[1];

    if (sum_hist > seven_eighth_hist) {
      /* low motion */
      *frame_complexity = LOW_MOTION;
    } else if (sum_hist < thres_hist) {
      /* high motion */
      *frame_complexity = HIGH_MOTION;
    } else {
      /* medium motion */
      *frame_complexity = MEDIUM_MOTION;
    }
  } else {                      /* codec_type = EXlnxHEVC */
    sum_2Bins = ctx->mv_histogram[0] + ctx->mv_histogram[1];
    sum_hist =
        ctx->mv_histogram[0] + ctx->mv_histogram[1] + ctx->mv_histogram[2] +
        ctx->mv_histogram[3];

    if (sum_2Bins > three_forth_hist) {
      /* low motion */
      *frame_complexity = LOW_MOTION;
    } else if (sum_hist < thres_hist) {
      /* high motion */
      *frame_complexity = HIGH_MOTION;
    } else {
      /* medium motion */
      *frame_complexity = MEDIUM_MOTION;
    }
  }

  return EXlnxSuccess;
}

static xlnx_status
generateQPMap (xlnx_aq_core_t handle, uint32_t dynamic_gop, uint64_t frame_num,
    const uint16_t * sadIn,
    const uint32_t * var_energy_map, const uint16_t * act_energy_map,
    uint32_t isLastFrame,
    uint32_t * frame_activity, uint32_t * frame_sad, int32_t is_idr)
{
  xlnx_status ret_status = EXlnxSuccess;

  xlnx_aq_core_ctx_t *ctx = (xlnx_aq_core_ctx_t *) handle;
  aq_config_t *cfg = ctx->cfg;
  xlnx_tp_qpmap_t *laMapCtx = ctx->laMapCtx;
  uint32_t numL1Lcu = ctx->num_mb + 1;
  xlnx_tp_qpmap_t *xlnx_tp_qpmap_t = NULL;
  uint8_t isIntraFrame = 0;

  LOG_MESSAGE (LOG_LEVEL_DEBUG,
      "IN frame_num=%lu sadIn=%p var_energy_map=%p act_energy_map=%p isLastFrame=%u",
      frame_num, sadIn, var_energy_map, act_energy_map, isLastFrame);

  if (frame_num == ctx->nextIntraFrame) {
    isIntraFrame = 1;
  }

  if (ctx->spatial_aq_mode[ctx->in_frame_num % (XLNX_DEFAULT_LA_DEPTH + 1)]) {
    ret_status =
        xlnx_spatial_gen_qpmap (ctx->sp_h, var_energy_map, act_energy_map,
        frame_num, frame_activity);
    if (ret_status != EXlnxSuccess) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "Spatial AQ failed");
      return ret_status;
    }
  }

  /* Dynamic IDR insertion check. Make sure not to redo the QP map 
     calculation when GOP period and IDR insertion occurs at same frame */
  if (sadIn && !isIntraFrame && is_idr) {
    ctx->nextIntraFrame = frame_num;
    if ((frame_num - ctx->lastIntraFrame) < cfg->la_depth) {
      LOG_MESSAGE (LOG_LEVEL_ERROR,
          "%s: Multiple Intra/IDR frames within LA depth is not supported",
          __FUNCTION__);
      return EXlnxError;
    }
    /* Memset collocated SAD buffer to 0 to start accumulating SAD for 
       IDR frames */
    if (((frame_num - ctx->lastIntraFrame) + 1) > cfg->la_depth) {
      memset (ctx->collocatedSadLA, 0, sizeof (uint32_t) * numL1Lcu);
      laMapCtx->frame_num = frame_num;
      ctx->isDeltaQpMapLAPending = 1;
    }
  }

  if (sadIn) {
    xlnx_tp_qpmap_t = get_qp_map_at (ctx->qpStore, ctx->write_idx);
    if (xlnx_tp_qpmap_t->inUse == 1) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "Input SAD Q is full");
      return EXlnxTryAgain;
    }

    if (isIntraFrame) {
      ctx->isDeltaQpMapLAPending = 1;
      assert (laMapCtx->inUse == 0);
      laMapCtx->frame_num = frame_num;
      memset (ctx->collocatedSadLA, 0, sizeof (uint32_t) * numL1Lcu);
    }
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "frame_num=%lu ctx->isDeltaQpMapLAPending=%u",
        frame_num, ctx->isDeltaQpMapLAPending);

    if (xlnx_temporal_gen_qpmap (ctx, sadIn, dynamic_gop, frame_num, frame_sad,
            xlnx_tp_qpmap_t)) {
      return EXlnxTryAgain;
    }
    /* Update previous and next intra frame for every GOP period */
    if (isIntraFrame || is_idr) {
      ctx->nextIntraFrame = frame_num + cfg->intraPeriod;
      ctx->lastIntraFrame = frame_num;
    } else {
      ctx->write_idx++;
      if (ctx->write_idx >= (cfg->la_depth + 1)) {
        ctx->write_idx = 1;
      }
    }
    ctx->accumulatedSadFrames += *frame_sad;
  }
  LOG_MESSAGE (LOG_LEVEL_DEBUG,
      "frame_num=%lu isDeltaQpMapLAPending=%u distance=%lu isLastFrame=%u",
      frame_num, ctx->isDeltaQpMapLAPending, (frame_num - ctx->lastIntraFrame),
      isLastFrame);

  if ((frame_num && (((frame_num - ctx->lastIntraFrame) + 1) == cfg->la_depth))
      || ((cfg->la_depth == 1) && (frame_num == 1))
      || isLastFrame || (sadIn == NULL)) {
    if (ctx->isDeltaQpMapLAPending) {
      if (laMapCtx->frame_num != ctx->lastIntraFrame) {
        LOG_MESSAGE (LOG_LEVEL_ERROR,
            "Expected Intra map = %lu does not match =%u", laMapCtx->frame_num,
            ctx->lastIntraFrame);
        return EXlnxError;
      }
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "Dump I %lu", laMapCtx->frame_num);
      xlnx_temporal_gen_la_qpmap (ctx, laMapCtx);
      laMapCtx->inUse = 1;
      ctx->isDeltaQpMapLAPending = 0;
    }
  }

  return EXlnxSuccess;
}

xlnx_status
send_frame_stats (xlnx_aq_core_t handle, uint32_t dynamic_gop,
    uint64_t frame_num, xlnx_frame_stats * stats, uint32_t isLastFrame,
    int32_t is_idr)
{
  xlnx_aq_core_ctx_t *ctx = (xlnx_aq_core_ctx_t *) handle;
  const uint16_t *sadIn = NULL;
  const uint32_t *varIn = NULL;
  const uint16_t *actIn = NULL;
  uint32_t frame_activity = 0;
  uint32_t frame_sad = 0;
  xlnx_status ret = EXlnxSuccess;

  if (stats != NULL) {
    sadIn = stats->sad;
    varIn = stats->var;
    actIn = stats->act;
  }

  ret =
      generateQPMap (handle, dynamic_gop, frame_num, sadIn, varIn, actIn,
      isLastFrame, &frame_activity, &frame_sad, is_idr);
  if (ret != EXlnxSuccess && stats && !isLastFrame) {
    return ret;
  }

  if (ctx->rc_h) {
    if (sadIn && actIn) {
      xlnx_rc_fsfa_t fsfa;
      if (ctx->temporal_aq_mode[ctx->in_frame_num % (XLNX_DEFAULT_LA_DEPTH +
                  1)] == XLNX_AQ_TEMPORAL_LINEAR) {
        fsfa.fs = frame_sad;
      } else {
        if (getFrameSad (ctx, sadIn, &fsfa.fs)) {
          return ret;
        }
      }
      if (ctx->spatial_aq_mode[ctx->in_frame_num % (XLNX_DEFAULT_LA_DEPTH +
                  1)] == XLNX_AQ_SPATIAL_ACTIVITY) {
        fsfa.fa = frame_activity;
      } else {
        if (xlnx_spatial_frame_activity (ctx->cfg, actIn, &fsfa.fa)) {
          return ret;
        }
      }
      ret = xlnx_algo_rc_write_fsfa (ctx->rc_h, &fsfa);
      if (isLastFrame) {
        LOG_MESSAGE (LOG_LEVEL_DEBUG, "EOS sent from xlnx algos");
        //printf("EOS sent from xlnx algos\n");
        xlnx_algo_rc_write_fsfa (ctx->rc_h, NULL);
      }
    } else {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "EOS sent from xlnx algos");
      ret = xlnx_algo_rc_write_fsfa (ctx->rc_h, NULL);
    }
  }
  ctx->in_frame_num++;

  return ret;
}

static int32_t
is_qp_map_pending (xlnx_aq_core_ctx_t * ctx, QpMapType type,
    uint64_t * frame_num, uint32_t * is_available)
{
  xlnx_tp_qpmap_t *xlnx_tp_qpmap_t = NULL;

  *frame_num = 0;
  *is_available = 0;
  if (type == EIType) {
    xlnx_tp_qpmap_t = ctx->laMapCtx;
    if (ctx->isDeltaQpMapLAPending || xlnx_tp_qpmap_t->inUse) {
      if (xlnx_tp_qpmap_t->inUse) {
        *is_available = 1;
      }
      *frame_num = xlnx_tp_qpmap_t->frame_num;
      return 1;
    }
  } else if (type == EPType) {
    xlnx_tp_qpmap_t = get_qp_map_at (ctx->qpStore, ctx->read_idx);
    if (xlnx_tp_qpmap_t->inUse) {
      *is_available = 1;
      *frame_num = xlnx_tp_qpmap_t->frame_num;
      return 1;
    }
  }
  return 0;
}

static xlnx_status
copy_qpmaps (xlnx_aq_core_ctx_t * ctx,
    xlnx_aq_info_t * dstVQInfo,
    xlnx_tp_qpmap_t * t_qpmap, spatial_qpmap_t * s_qpmap, uint32_t num_mb)
{
  int32_t *temporal_qpmap = NULL;
  float *spatial_qpmap = NULL;
  xlnx_aq_buf_t *dst_qpmap = NULL;
  uint64_t frame_num = 0;

  if (t_qpmap && !dstVQInfo) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, "VQ out buffer invalid!!");
    return EXlnxError;
  }
  if (t_qpmap && t_qpmap->qpmap.ptr && s_qpmap && s_qpmap->fPtr &&
      (t_qpmap->frame_num != s_qpmap->frame_num)) {
    LOG_MESSAGE (LOG_LEVEL_WARNING,
        "Temporal frame number(%lu) != Spatial qpmap number(%lu)",
        t_qpmap->frame_num, s_qpmap->frame_num);
  }
  dst_qpmap = &dstVQInfo->qpmap;
  if (dst_qpmap->ptr == NULL) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, "VQ out buffer invalid!!");
    return EXlnxError;
  }
  if (t_qpmap && t_qpmap->qpmap.ptr) {
    temporal_qpmap = (int32_t *) t_qpmap->qpmap.ptr;
    frame_num = t_qpmap->frame_num;
  }
  if (s_qpmap && s_qpmap->fPtr) {
    spatial_qpmap = s_qpmap->fPtr;
    frame_num = s_qpmap->frame_num;
  }
  if (ctx->tmp_hevc_map) {
    merge_qp_maps_hevc (ctx, temporal_qpmap, spatial_qpmap, num_mb,
        dst_qpmap->ptr);
  } else {
    merge_qp_maps (ctx, temporal_qpmap, spatial_qpmap, num_mb, dst_qpmap->ptr);
  }
  dstVQInfo->frame_num = frame_num;
  return EXlnxSuccess;
}

static xlnx_status
copy_fsfa (xlnx_rc_aq_t rc, xlnx_aq_info_t * dstVQInfo)
{
  if (rc && dstVQInfo) {
    uint64_t rc_frame_num = 0;
    xlnx_aq_buf_t *dst_fsfa = &dstVQInfo->fsfa;

    if (!dst_fsfa->ptr) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "VQ out buffer invalid!!");
      return EXlnxError;
    }
    if (EXlnxSuccess != xlnx_algo_rc_read_fsfa (rc, dst_fsfa, &rc_frame_num)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "RC algo failed");
      return EXlnxError;
    }
    dstVQInfo->frame_num = rc_frame_num;
  }
  return EXlnxSuccess;
}

static int
xlnx_temporal_is_ready (xlnx_aq_core_ctx_t * ctx)
{
  uint64_t frameNumI, frameNumP;
  uint32_t is_available;
  int32_t iPending, pPending;
  int ret_available = 0;
  iPending = is_qp_map_pending (ctx, EIType, &frameNumI, &is_available);
  if (iPending && is_available) {
    ret_available = 1;
  } else {
    pPending = is_qp_map_pending (ctx, EPType, &frameNumP, &is_available);
    if (pPending && is_available) {
      if ((frameNumP < frameNumI) || !iPending) {
        ret_available = 1;
      }
    }
  }
  return ret_available;
}

static int
all_qpmaps_available (xlnx_aq_core_ctx_t * ctx)
{
  int ret = 1;
  int sp_ready = 1;
  int tp_ready = 1;

  if (ctx->spatial_aq_mode[ctx->out_frame_num % (XLNX_DEFAULT_LA_DEPTH + 1)]) {
    sp_ready = xlnx_spatial_is_ready (ctx->sp_h);
  }

  tp_ready = xlnx_temporal_is_ready (ctx);
  if (!sp_ready || !tp_ready) {
    ret = 0;
  }
  return ret;
}

xlnx_status
recv_frame_aq_info (xlnx_aq_core_t handle, xlnx_aq_info_t * vqInfo,
    uint64_t frame_num, int32_t is_idr)
{
  xlnx_aq_core_ctx_t *ctx = (xlnx_aq_core_ctx_t *) handle;
  aq_config_t *cfg = ctx->cfg;
  uint64_t frameNumI, frameNumP;
  uint32_t is_available;
  int32_t iPending;
  int32_t pPending;
  //uint32_t numL1Lcu = ctx->num_mb+1;
  uint8_t iframe_sent = 0;
  xlnx_status qp_status = EXlnxSuccess;
  spatial_qpmap_t s_qpmap;
  xlnx_tp_qpmap_t *t_qpmap = NULL;

  if (ctx->rc_h) {
    if (xlnx_algo_rc_fsfa_available (ctx->rc_h) == 0) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "rc fsfa not available...try again");
      return EXlnxTryAgain;
    }
  }

  if (all_qpmaps_available (ctx) == 0) {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "qpmaps not available...try again");
    return EXlnxTryAgain;
  }

  s_qpmap.fPtr = NULL;
  if (ctx->spatial_aq_mode[ctx->out_frame_num % (XLNX_DEFAULT_LA_DEPTH + 1)]) {
    qp_status = xlnx_spatial_recv_qpmap (ctx->sp_h, &s_qpmap);
    if (qp_status != EXlnxSuccess) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "Spatial qpmap not available");
    }
  }

  iPending = is_qp_map_pending (ctx, EIType, &frameNumI, &is_available);
  if (iPending && is_available) {
    t_qpmap = ctx->laMapCtx;
    pPending = is_qp_map_pending (ctx, EPType, &frameNumP, &is_available);
    if (pPending && is_available) {
      if (frameNumP < frameNumI) {
        t_qpmap = get_qp_map_at (ctx->qpStore, ctx->read_idx);
        ctx->read_idx++;
        if (ctx->read_idx >= (cfg->la_depth + 1)) {
          ctx->read_idx = 1;
        }
        LOG_MESSAGE (LOG_LEVEL_DEBUG, "OUT P available");
      } else
        iframe_sent = 1;
    } else
      iframe_sent = 1;
  } else {
    t_qpmap = get_qp_map_at (ctx->qpStore, ctx->read_idx);
    ctx->read_idx++;
    if (ctx->read_idx >= (cfg->la_depth + 1)) {
      ctx->read_idx = 1;
    }
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "OUT P available");
  }

  if (!ctx->temporal_aq_mode[ctx->out_frame_num % (XLNX_DEFAULT_LA_DEPTH + 1)]) {
    t_qpmap->inUse = 0;
    t_qpmap = NULL;
  }

  if (ctx->spatial_aq_mode[ctx->out_frame_num % (XLNX_DEFAULT_LA_DEPTH + 1)] ||
      ctx->temporal_aq_mode[ctx->out_frame_num % (XLNX_DEFAULT_LA_DEPTH + 1)]) {
    qp_status = copy_qpmaps (ctx, vqInfo, t_qpmap, &s_qpmap, ctx->num_mb);
    if (ctx->dump_handle) {
      dump_frame_delta_qp_map (ctx->dump_handle, vqInfo->qpmap.ptr,
          vqInfo->qpmap.size, vqInfo->frame_num, 0);
    }
  } else {
    memset (vqInfo->qpmap.ptr, 0, vqInfo->qpmap.size);
  }

  if (t_qpmap) {
    t_qpmap->inUse = 0;
  }
  if (ctx->spatial_aq_mode[ctx->out_frame_num % (XLNX_DEFAULT_LA_DEPTH + 1)]) {
    xlnx_spatial_release_qpmap (ctx->sp_h, &s_qpmap);
  }
  if (qp_status != EXlnxSuccess) {
    LOG_MESSAGE (LOG_LEVEL_ERROR, "qpmap generation status = %d", qp_status);
    return qp_status;
  }

  /* Updating next intra frame number for LA map. This has been introduced 
     to handle dynamic IDR insertions */
  if (iframe_sent) {
    ctx->laMapCtx->frame_num = ctx->lastIntraFrame;
  }
  ctx->out_frame_num++;
  return copy_fsfa (ctx->rc_h, vqInfo);
}
