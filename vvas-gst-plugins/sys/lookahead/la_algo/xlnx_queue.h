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
#ifndef XLNX_QUEUE_H
#define XLNX_QUEUE_H

typedef void *XItem;
typedef void *xlnx_queue;

xlnx_queue createQueue (size_t capacity, size_t itemSize);
void destroyQueue (xlnx_queue q);
int isFull (xlnx_queue q);
int isEmpty (xlnx_queue q);
size_t getSize (xlnx_queue q);
int enqueue (xlnx_queue q, XItem item);
int dequeue (xlnx_queue q, XItem item);
int front (xlnx_queue q, XItem item);
int rear (xlnx_queue q, XItem item);

int PushQ (xlnx_queue q, XItem buff);
int PopQ (xlnx_queue q, XItem buff);

#endif // XLNX_QUEUE_H
