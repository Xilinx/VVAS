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
#include "xlnx_queue.h"

typedef struct my_queue
{
  int front, rear;
  size_t size, capacity, itemSize;
  XItem pItem;
} my_queue;

xlnx_queue
createQueue (size_t capacity, size_t itemSize)
{
  my_queue *queue = NULL;

  if ((capacity == 0) || (itemSize == 0)) {
    printf ("Error: createQueue Invalid capacity=%lu itemSize=%lu ! %s\n",
        capacity, itemSize, __FILE__);
  }
  queue = (my_queue *) malloc (sizeof (my_queue));
  if (NULL == queue) {
    printf ("Error: %s OOM!\n", __FUNCTION__);
    return queue;
  }
  queue->capacity = capacity;
  queue->front = queue->size = 0;
  queue->rear = -1;
  queue->itemSize = itemSize;
  queue->pItem = (XItem) calloc (queue->capacity, itemSize);
  if (NULL == queue->pItem) {
    printf ("Error: %s OOM!\n", __FUNCTION__);
    free (queue);
    return NULL;
  }
  return (xlnx_queue) queue;
}

void
destroyQueue (xlnx_queue q)
{
  my_queue *queue = (my_queue *) q;
  if (NULL == queue) {
    return;
  }
  free (queue->pItem);
  free (queue);
  return;
}

int
isFull (xlnx_queue q)
{
  my_queue *queue = (my_queue *) q;
  assert (queue != NULL);
  return (queue->size == queue->capacity);
}

int
isEmpty (xlnx_queue q)
{
  my_queue *queue = (my_queue *) q;
  assert (queue != NULL);
  return (queue->size == 0);
}

size_t
getSize (xlnx_queue q)
{
  my_queue *queue = (my_queue *) q;
  assert (queue != NULL);
  return queue->size;
}

int
enqueue (xlnx_queue q, XItem item)
{
  my_queue *queue = (my_queue *) q;
  char *dst = NULL;

  if ((queue == NULL) || (item == NULL)) {
    printf ("Error: xlnx_queue enqueue Invalid queue=%p item=%p \n", queue,
        item);
  }
  if (isFull (queue)) {
    printf ("Error: %p xlnx_queue enqueue FULL\n", queue);
    return -1;
  }
  queue->rear = (queue->rear + 1) % queue->capacity;
  //queue->pItem[queue->rear] = item;
  dst = (char *) queue->pItem + queue->rear * queue->itemSize;
  memcpy (dst, item, queue->itemSize);
  queue->size++;
  return 0;
}

int
dequeue (xlnx_queue q, XItem item)
{
  my_queue *queue = (my_queue *) q;
  char *src = NULL;

  if ((queue == NULL) || (item == NULL)) {
    printf ("Error: dequeue Invalid queue=%p item=%p \n", queue, item);
    return -1;
  }
  if (isEmpty (queue)) {
    //printf("Error: dequeue empty\n");
    return -1;
  }
  src = (char *) queue->pItem + queue->front * queue->itemSize;
  memcpy (item, src, queue->itemSize);
  //item = queue->pItem[queue->front];
  //queue->pItem[queue->front] = 0;
  queue->front = (queue->front + 1) % queue->capacity;
  queue->size = queue->size - 1;
  return 0;
}

int
front (xlnx_queue q, XItem item)
{
  my_queue *queue = (my_queue *) q;
  char *src = NULL;

  if ((queue == NULL) || (item == NULL)) {
    printf ("Error: front Invalid queue=%p item=%p \n", queue, item);
    return -1;
  }
  if (isEmpty (queue)) {
    //printf("Error: front empty\n");
    return -1;
  }
  src = (char *) queue->pItem + queue->front * queue->itemSize;
  memcpy (item, src, queue->itemSize);
  return 0;
}

int
rear (xlnx_queue q, XItem item)
{
  my_queue *queue = (my_queue *) q;
  char *src = NULL;

  if ((queue == NULL) || (item == NULL)) {
    printf ("Error: front Invalid queue=%p item=%p \n", queue, item);
    return -1;
  }
  if (isEmpty (queue)) {
    //printf("Error: rear empty\n");
    return -1;
  }
  src = (char *) queue->pItem + queue->rear * queue->itemSize;
  memcpy (item, src, queue->itemSize);
  return 0;
}

int
PushQ (xlnx_queue q, XItem buff)
{
  return enqueue (q, buff);
}

int
PopQ (xlnx_queue q, XItem buff)
{
  int empty = 0;
  if (dequeue (q, buff) < 0) {
    empty = 1;
  }
  return empty;
}
