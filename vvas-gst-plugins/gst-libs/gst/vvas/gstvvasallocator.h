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

#ifndef __GST_VVAS_ALLOCATOR_H__
#define __GST_VVAS_ALLOCATOR_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#ifdef XLNX_PCIe_PLATFORM
#include <xrt.h>
#else
#include <xrt/xrt.h>
#endif

G_BEGIN_DECLS

#define GST_TYPE_VVAS_ALLOCATOR  \
   (gst_vvas_allocator_get_type())
#define GST_IS_VVAS_ALLOCATOR(obj)       \
   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VVAS_ALLOCATOR))
#define GST_IS_VVAS_ALLOCATOR_CLASS(klass)     \
   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VVAS_ALLOCATOR))
#define GST_VVAS_ALLOCATOR_GET_CLASS(obj)      \
   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VVAS_ALLOCATOR, GstVvasAllocatorClass))
#define GST_VVAS_ALLOCATOR(obj)        \
   (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VVAS_ALLOCATOR, GstVvasAllocator))
#define GST_VVAS_ALLOCATOR_CLASS(klass)      \
   (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VVAS_ALLOCATOR, GstVvasAllocatorClass))

typedef struct _GstVvasAllocator GstVvasAllocator;
typedef struct _GstVvasAllocatorClass GstVvasAllocatorClass;
typedef struct _GstVvasAllocatorPrivate GstVvasAllocatorPrivate;

enum _GstVvasAllocatorFlags
{
  GST_VVAS_ALLOCATOR_FLAG_MEM_INIT = (GST_ALLOCATOR_FLAG_LAST << 0)
};

struct _GstVvasAllocator
{
  GstAllocator parent;
  GstVvasAllocatorPrivate *priv;
};

struct _GstVvasAllocatorClass {
  GstAllocatorClass parent_class;
};

GST_EXPORT
GType gst_vvas_allocator_get_type (void) G_GNUC_CONST;
GST_EXPORT
GstAllocator* gst_vvas_allocator_new (guint dev_idx, gboolean need_dma,
                                 guint mem_bank, void* kern_handle);
GST_EXPORT
gboolean gst_vvas_allocator_start (GstVvasAllocator * allocator, guint min_mem,
    guint max_mem, gsize size, GstAllocationParams * params);
GST_EXPORT
gboolean gst_vvas_allocator_stop (GstVvasAllocator * allocator);
GST_EXPORT
gboolean gst_is_vvas_memory (GstMemory *mem);
GST_EXPORT
guint64  gst_vvas_allocator_get_paddr (GstMemory *mem);
GST_EXPORT
void*  gst_vvas_allocator_get_bo (GstMemory *mem);
GST_EXPORT
gboolean gst_vvas_memory_can_avoid_copy (GstMemory *mem, guint cur_devid, guint req_mem_bank);
GST_EXPORT
guint gst_vvas_allocator_get_device_idx (GstAllocator * allocator);

#ifdef XLNX_PCIe_PLATFORM
typedef enum {
  VVAS_SYNC_NONE = 0,
  VVAS_SYNC_TO_DEVICE = 1 << 0, /* sync data to device using DMA transfer */
  VVAS_SYNC_FROM_DEVICE = 1 << 1, /* sync data to device using DMA transfer */
} VvasSyncFlags;

GST_EXPORT
void gst_vvas_memory_set_sync_flag (GstMemory *mem, VvasSyncFlags flag);
GST_EXPORT
gboolean gst_vvas_memory_sync_bo (GstMemory *mem);
GST_EXPORT
gboolean gst_vvas_memory_sync_with_flags (GstMemory *mem, GstMapFlags flags);
GST_EXPORT
void gst_vvas_memory_set_flag (GstMemory *mem, VvasSyncFlags flag);
GST_EXPORT
void gst_vvas_memory_unset_flag (GstMemory *mem, VvasSyncFlags flag);
#endif

G_END_DECLS

#endif /* __GST_VVAS_ALLOCATOR_H__ */
