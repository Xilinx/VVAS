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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#include "xlnx_rc_aq.h"

#define XLNX_ALGO_RC_MASTER_BUF_SIZE 4096
#define PRINT_FSFA 0

typedef struct xlnx_rc
{
  int front, rear;
  size_t size, capacity, fsfa_size;
  size_t r_size;
  uint32_t la_depth;
  uint8_t *pBuf;
  size_t buf_size;
  uint64_t in_frame_num;
  uint64_t out_frame_num;
  uint8_t eos_notified;
} xlnx_rc_t;

xlnx_rc_aq_t
xlnx_algo_rc_create (uint32_t la_depth)
{
  xlnx_rc_t *rc;

  if (0 == la_depth) {
    //printf("Error: RC la_depth=%u\n", la_depth);
    return NULL;
  }
  rc = (xlnx_rc_t *) calloc (1, sizeof (xlnx_rc_t));
  if (NULL == rc) {
    //printf("Error: Out of Memory! %s at LINE %d\n", __FILE__, __LINE__);
    return rc;
  }
  rc->la_depth = la_depth;
  rc->fsfa_size = sizeof (xlnx_rc_fsfa_t);
  rc->r_size = la_depth * rc->fsfa_size;
  rc->buf_size = XLNX_ALGO_RC_MASTER_BUF_SIZE - (XLNX_ALGO_RC_MASTER_BUF_SIZE %
      sizeof (xlnx_rc_fsfa_t));
  rc->capacity = rc->buf_size / rc->fsfa_size;
  rc->front = rc->size = 0;
  rc->rear = -1;
  rc->pBuf = (uint8_t *) calloc (1, rc->buf_size);
  if (NULL == rc->pBuf) {
    //printf("Error: Out of Memory! %s at LINE %d\n", __FILE__, __LINE__);
    free (rc);
    return NULL;
  }
  rc->in_frame_num = 0;
  rc->out_frame_num = 0;
  rc->eos_notified = 0;
  return (xlnx_rc_aq_t) rc;
}

void
xlnx_algo_rc_destroy (xlnx_rc_aq_t rc_handle)
{
  xlnx_rc_t *rc = (xlnx_rc_t *) rc_handle;
  if (NULL == rc) {
    return;
  }
  free (rc->pBuf);
  free (rc);
  return;
}

static inline int
is_buf_full (xlnx_rc_t * rc)
{
  return (rc->size == rc->capacity);
}

static inline int
is_read_possible (xlnx_rc_t * rc)
{
  return ((rc->size >= rc->la_depth) || ((rc->eos_notified == 1) &&
          (rc->size > 0)));
}

int
xlnx_algo_rc_fsfa_available (xlnx_rc_aq_t rc_handle)
{
  xlnx_rc_t *rc = (xlnx_rc_t *) rc_handle;
  return is_read_possible (rc);
}

static inline size_t
getSize (xlnx_rc_t * rc)
{
  return rc->size;
}

xlnx_status
xlnx_algo_rc_write_fsfa (xlnx_rc_aq_t rc_handle, xlnx_rc_fsfa_t * fsfa)
{
  xlnx_rc_t *rc = (xlnx_rc_t *) rc_handle;
  uint8_t *dst = NULL;

  if (NULL == rc) {
    /*printf("Error: xlnx_algo_rc_write_fsfa : Rate control handle=%p fsfa=%p\n", rc,
       fsfa); */
    return EXlnxError;
  }
  if (fsfa == NULL) {
    rc->eos_notified = 1;
    //printf("xlnx_algo_rc_write_fsfa EOS received\n");
    return EXlnxSuccess;
  }

  if (is_buf_full (rc)) {
    //printf("WARNING: RC buf is full\n");
    return EXlnxTryAgain;
  }
  rc->rear = (rc->rear + 1) % rc->capacity;
  dst = rc->pBuf + rc->rear * rc->fsfa_size;
  memcpy (dst, (uint8_t *) fsfa, rc->fsfa_size);
  rc->size++;

  /*printf("xlnx_algo_rc_write_fsfa [%lu] write sad=%u activity=%u\n",
     rc->in_frame_num, fsfa->fs, fsfa->fa); */
  rc->in_frame_num++;
  return EXlnxSuccess;
}

xlnx_status
xlnx_algo_rc_read_fsfa (xlnx_rc_aq_t rc_handle,
    xlnx_aq_buf_t * fsfa_buf, uint64_t * frame_num)
{
  xlnx_rc_t *rc = (xlnx_rc_t *) rc_handle;
  if ((NULL == rc) || (fsfa_buf == NULL)) {
    //printf("Error: xlnx_algo_rc_read_fsfa : RC handle=%p fsfa=%p\n", rc, fsfa_buf);
    return EXlnxError;
  }
  if (fsfa_buf->size < rc->r_size) {
    //printf("Error: xlnx_algo_rc_read_fsfa : fsfa size=%lu\n", fsfa_buf->size);
    return EXlnxError;
  }
  if (is_read_possible (rc)) {
    uint8_t *src = rc->pBuf + rc->front * rc->fsfa_size;
    if ((rc->size <= rc->la_depth) && rc->eos_notified) {
      xlnx_rc_fsfa_t *pad;
      int i;
      if ((rc->capacity - rc->front) >= rc->size) {
        memcpy (fsfa_buf->ptr, src, rc->size * rc->fsfa_size);
      } else {
        size_t r1 = (rc->capacity - rc->front) * rc->fsfa_size;
        size_t r2;

        memcpy (fsfa_buf->ptr, src, r1);
        r2 = (rc->size * rc->fsfa_size) - r1;
        memcpy (fsfa_buf->ptr + r1, rc->pBuf, r2);
      }
      pad = (xlnx_rc_fsfa_t *) (fsfa_buf->ptr + rc->size * rc->fsfa_size);
      for (i = 0; i < (rc->la_depth - rc->size); i++) {
        pad[i].fs = UINT_MAX;
        pad[i].fa = UINT_MAX;
      }
    } else if ((rc->capacity - rc->front) >= rc->la_depth) {
      memcpy (fsfa_buf->ptr, src, rc->r_size);
    } else {
      size_t r1 = (rc->capacity - rc->front) * rc->fsfa_size;
      memcpy (fsfa_buf->ptr, src, r1);
      memcpy (fsfa_buf->ptr + r1, rc->pBuf, (rc->r_size - r1));
    }
    rc->front = (rc->front + 1) % rc->capacity;
    rc->size = rc->size - 1;
    *frame_num = rc->out_frame_num;

#if PRINT_FSFA
    xlnx_rc_fsfa_t *pfsfa = src;
    /*printf("activity[%lu]= %u;\t frameSad[%lu]=%u;\n", rc->out_frame_num,
       pfsfa->fa, rc->out_frame_num, pfsfa->fs); */
#endif //PRINT_FSFA
    /*printf("xlnx_algo_rc_read_fsfa : fsfa size=%lu out=%lu \n", rc->size,
       rc->out_frame_num); */
    rc->out_frame_num++;
    return EXlnxSuccess;
  }
  /*printf("xlnx_algo_rc_read_fsfa : fsfa size=%lu la_depth=%u \n", rc->size,
     rc->la_depth); */
  return EXlnxTryAgain;
}
