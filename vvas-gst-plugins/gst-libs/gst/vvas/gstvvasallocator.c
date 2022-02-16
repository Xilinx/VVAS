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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvvasallocator.h"
#include <vvas/xrt_utils.h>
#include <sys/mman.h>
#include <string.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/gstpoll.h>

#define GST_CAT_DEFAULT vvasallocator_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_VVAS_MEMORY_TYPE "VVASMemory"
#define DEFAULT_DEVICE_INDEX 0
#define DEFAULT_NEED_DMA FALSE

enum
{
  PROP_0,
  PROP_DEVICE_INDEX,
  PROP_NEED_DMA,
  PROP_MEM_BANK,
};

enum
{
  VVAS_MEM_RELEASED,
  LAST_SIGNAL
};

struct _GstVvasAllocatorPrivate
{
  gint dev_idx;
  gboolean need_dma;
  guint mem_bank;
  vvasDeviceHandle handle;
  GstAllocator *dmabuf_alloc;
  gboolean active;
  GstAtomicQueue *free_queue;
  GstPoll *poll;
  guint cur_mem;
  guint min_mem;
  guint max_mem;
};

typedef struct _GstVvasMemory
{
  GstMemory parent;
  gpointer data;                /* used in non-dma mode */
  unsigned int bo;
  gsize size;
  GstVvasAllocator *alloc;
  GstMapFlags mmapping_flags;
  gint mmap_count;
  GMutex lock;
  gboolean do_free;
#ifdef XLNX_PCIe_PLATFORM
  VvasSyncFlags sync_flags;
#endif
} GstVvasMemory;

static guint gst_vvas_allocator_signals[LAST_SIGNAL] = { 0 };

#define parent_class gst_vvas_allocator_parent_class
G_DEFINE_TYPE_WITH_CODE (GstVvasAllocator, gst_vvas_allocator,
    GST_TYPE_ALLOCATOR, G_ADD_PRIVATE (GstVvasAllocator);
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vvasallocator", 0,
        "VVAS allocator"));
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

static GstVvasMemory *
get_vvas_mem (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, NULL);

  if (GST_IS_VVAS_ALLOCATOR (mem->allocator)) {
    return (GstVvasMemory *) mem;
  } else if (gst_is_dmabuf_memory (mem)) {
    return (GstVvasMemory *) gst_mini_object_get_qdata (GST_MINI_OBJECT (mem),
        g_quark_from_static_string ("vvasmem"));
  } else {
    return NULL;
  }
}

static gboolean
vvas_allocator_memory_dispose (GstMiniObject * obj)
{
  GstMemory *mem = GST_MEMORY_CAST (obj);
  GstVvasMemory *vvasmem = (GstVvasMemory *) mem;
  GstVvasAllocator *vvas_alloc = (GstVvasAllocator *) mem->allocator;
  GstVvasAllocatorPrivate *priv = vvas_alloc->priv;

  if (priv->free_queue && !vvasmem->do_free) {
    GST_DEBUG_OBJECT (vvas_alloc, "pushing back memory %p to free queue", mem);
    gst_atomic_queue_push (priv->free_queue, gst_memory_ref (mem));
    gst_poll_write_control (priv->poll);
    g_signal_emit (vvas_alloc, gst_vvas_allocator_signals[VVAS_MEM_RELEASED], 0, mem);
    return FALSE;
  }

  return TRUE;
}

static GstMemory *
gst_vvas_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstVvasAllocator *vvas_alloc = GST_VVAS_ALLOCATOR (allocator);
  GstVvasAllocatorPrivate *priv = vvas_alloc->priv;
  GstVvasMemory *vvasmem;
  GstMemory *mem;
  gint prime_fd = 0;
  int iret = 0;
  void* data = NULL;

  if (priv->free_queue && g_atomic_int_get (&priv->active)) {
    // TODO: peek memory and check size of popped mem is sufficient or not

pop_now:
    mem = gst_atomic_queue_pop (priv->free_queue);
    if (G_LIKELY (mem)) {
      while (!gst_poll_read_control (priv->poll)) {
        if (errno == EWOULDBLOCK) {
          /* We put the buffer into the queue but did not finish writing control
           * yet, let's wait a bit and retry */
          g_thread_yield ();
          continue;
        } else {
          /* Critical error but GstPoll already complained */
          break;
        }
      }
      GST_LOG_OBJECT (vvas_alloc, "popped preallocated memory %p", mem);
      return mem;
    } else {
      /* check we reached maximum buffers */
      if (priv->max_mem && priv->cur_mem >= priv->max_mem) {
        if (!gst_poll_read_control (priv->poll)) {
          if (errno == EWOULDBLOCK) {
            GST_LOG_OBJECT (vvas_alloc, "waiting for free memory");
            gst_poll_wait (priv->poll, GST_CLOCK_TIME_NONE);
          } else {
            GST_ERROR_OBJECT (vvas_alloc, "critical error");
            return NULL;
          }
        } else {
          GST_LOG_OBJECT (vvas_alloc, "waiting for free memory");
          gst_poll_wait (priv->poll, GST_CLOCK_TIME_NONE);
          gst_poll_write_control (priv->poll);
        }
        goto pop_now; // now try popping memory as wait is completed
      }
    }
  }

  if (priv->handle == NULL) {
    GST_ERROR_OBJECT (vvas_alloc, "failed get handle from VVAS");
    return NULL;
  }

  vvasmem = g_slice_new0 (GstVvasMemory);
  gst_memory_init (GST_MEMORY_CAST (vvasmem), params->flags,
      GST_ALLOCATOR_CAST (vvas_alloc), NULL, size, params->align,
      params->prefix, size);

  if (priv->free_queue) {
    vvasmem->parent.mini_object.dispose = (GstMiniObjectDisposeFunction)
        vvas_allocator_memory_dispose;
  }

  GST_DEBUG_OBJECT (vvas_alloc, "memory allocated %p with size %lu, flags %d, "
      "align %lu, prefix %lu", vvasmem, size, params->flags, params->align,
      params->prefix);

  vvasmem->alloc = vvas_alloc;
  g_mutex_init (&vvasmem->lock);

#ifdef XLNX_PCIe_PLATFORM
  vvasmem->sync_flags = VVAS_SYNC_NONE;
#endif

  vvasmem->bo = vvas_xrt_alloc_bo (priv->handle, size,
		                   VVAS_BO_DEVICE_RAM, priv->mem_bank);
  if (vvasmem->bo == NULLBO) {
    GST_ERROR_OBJECT (vvas_alloc, "failed to allocate Device BO. reason %s(%d)",
        strerror (errno), errno);
    return NULL;
  }
  vvasmem->size = size;
  vvasmem->do_free = FALSE;

  if (params->flags & GST_VVAS_ALLOCATOR_FLAG_MEM_INIT) {
    GST_LOG_OBJECT (vvas_alloc, "Doing memset for created buffer");
    data = vvas_xrt_map_bo (priv->handle, vvasmem->bo, true);
    if (data) {
      memset(data, 0, size);
      iret = vvas_xrt_sync_bo (priv->handle, vvasmem->bo,
                               VVAS_BO_SYNC_BO_TO_DEVICE, size, 0);
      if (iret != 0) {
        GST_ERROR_OBJECT (vvas_alloc,
                          "failed to sync output buffer. reason : %d, %s",
                          iret, strerror (errno));
        vvas_xrt_unmap_bo(priv->handle, vvasmem->bo, data);
        return NULL;
      }
      vvas_xrt_unmap_bo(priv->handle, vvasmem->bo, data);
    }
  }

  if (priv->need_dma) {
    prime_fd = vvas_xrt_export_bo (priv->handle, vvasmem->bo);
    if (prime_fd < 0) {
      GST_ERROR_OBJECT (vvas_alloc, "failed to get dmafd...");
      vvas_xrt_free_bo (priv->handle, vvasmem->bo);
      vvasmem->bo = 0;
      return NULL;
    }

    GST_DEBUG_OBJECT (vvas_alloc,
        "exported xrt bo %u as dmafd %d", vvasmem->bo, prime_fd);

    mem = gst_dmabuf_allocator_alloc (priv->dmabuf_alloc, prime_fd, size);

    gst_mini_object_set_qdata (GST_MINI_OBJECT (mem),
        g_quark_from_static_string ("vvasmem"), vvasmem,
        (GDestroyNotify) gst_memory_unref);
  } else {
    mem = GST_MEMORY_CAST (vvasmem);
  }

  priv->cur_mem++;
  return mem;
}

static gpointer
gst_vvas_mem_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstVvasMemory *vvasmem = (GstVvasMemory *) mem;
  GstVvasAllocator *alloc = vvasmem->alloc;
  gpointer ret = NULL;

  g_mutex_lock (&vvasmem->lock);

  if (vvasmem->data) {
    /* only return address if mapping flags are a subset
     * of the previous flags */
    if ((vvasmem->mmapping_flags & flags) == flags) {
      ret = vvasmem->data;
      vvasmem->mmap_count++;
    }
    goto out;
  }
#ifdef XLNX_PCIe_PLATFORM
  if (!gst_vvas_memory_sync_with_flags (mem, flags)) {
    g_mutex_unlock (&vvasmem->lock);
    return NULL;
  }
#endif

  vvasmem->data =
      vvas_xrt_map_bo (alloc->priv->handle, vvasmem->bo, flags & GST_MAP_WRITE);
  GST_DEBUG_OBJECT (alloc, "mapped pointer %p with size %lu do_write %d",
      vvasmem->data, maxsize, flags & GST_MAP_WRITE);

  if (vvasmem->data) {
    vvasmem->mmapping_flags = flags;
    vvasmem->mmap_count++;
    ret = vvasmem->data;
  }

out:
  g_mutex_unlock (&vvasmem->lock);
  return ret;
}

static GstMemory *
gst_vvas_mem_share (GstMemory * mem, gssize offset, gssize size)
{
  GstVvasMemory *vvasmem = (GstVvasMemory *) mem;
  GstMemory *parent;
  GstVvasMemory *sub;

  GST_DEBUG ("%p: share %" G_GSSIZE_FORMAT " %" G_GSIZE_FORMAT, mem, offset,
      size);

  /* find the real parent */
  if ((parent = vvasmem->parent.parent) == NULL)
    parent = GST_MEMORY_CAST (vvasmem);

  if (size == -1)
    size = mem->maxsize - offset;

  sub = g_slice_new0 (GstVvasMemory);

  /* shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, vvasmem->parent.allocator, parent,
      vvasmem->parent.maxsize, vvasmem->parent.align,
      vvasmem->parent.offset + offset, size);

  g_mutex_init (&sub->lock);
  sub->bo = vvasmem->bo;
  sub->alloc = vvasmem->alloc;
  sub->size = vvasmem->size;

#ifdef XLNX_PCIe_PLATFORM
  sub->sync_flags = vvasmem->sync_flags;
#endif

  GST_DEBUG ("%p: share mem created", sub);

  return GST_MEMORY_CAST (sub);
}

static void
gst_vvas_mem_unmap (GstMemory * mem)
{
  GstVvasMemory *vvasmem = (GstVvasMemory *) mem;
  GstVvasAllocator *alloc = vvasmem->alloc;
  int ret = 0;

  if (mem->parent)
    return gst_vvas_mem_unmap (mem->parent);

  g_mutex_lock (&vvasmem->lock);
  if (vvasmem->data && !(--vvasmem->mmap_count)) {
    //ret = vvas_xrt_unmap_bo (alloc->priv->handle, vvasmem->bo, vvasmem->data);
    ret = munmap (vvasmem->data, vvasmem->size);
    if (ret) {
      GST_ERROR ("failed to unmap %p", vvasmem->data);
    }
    vvasmem->data = NULL;
    vvasmem->mmapping_flags = 0;
    GST_DEBUG_OBJECT (alloc, "%p: bo %d unmapped", vvasmem, vvasmem->bo);
  }
  g_mutex_unlock (&vvasmem->lock);
}

static void
gst_vvas_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstVvasMemory *vvasmem = (GstVvasMemory *) mem;
  GstVvasAllocator *alloc = GST_VVAS_ALLOCATOR (allocator);

  if (vvasmem->bo != NULLBO && mem->parent == NULL)
    vvas_xrt_free_bo (alloc->priv->handle, vvasmem->bo);

  g_mutex_clear (&vvasmem->lock);
  g_slice_free (GstVvasMemory, vvasmem);

  GST_OBJECT_LOCK (alloc);
  alloc->priv->cur_mem--;
  GST_OBJECT_UNLOCK (alloc);

  GST_DEBUG ("%p: freed", mem);
}

static void
gst_vvas_allocator_finalize (GObject * obj)
{
  GstVvasAllocator *alloc = GST_VVAS_ALLOCATOR (obj);

  if (alloc->priv->dmabuf_alloc)
    gst_object_unref (alloc->priv->dmabuf_alloc);

  vvas_xrt_close_device (alloc->priv->handle);
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_vvas_allocator_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvasAllocator *alloc = GST_VVAS_ALLOCATOR (object);

  switch (prop_id) {
    case PROP_DEVICE_INDEX:
      alloc->priv->dev_idx = g_value_get_int (value);
      vvas_xrt_open_device (alloc->priv->dev_idx, &alloc->priv->handle);
      break;
    case PROP_NEED_DMA:
      alloc->priv->need_dma = g_value_get_boolean (value);
      break;
    case PROP_MEM_BANK:
      alloc->priv->mem_bank = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vvas_allocator_class_init (GstVvasAllocatorClass * klass)
{
  GObjectClass *gobject_class;
  GstAllocatorClass *allocator_class;

  gobject_class = G_OBJECT_CLASS (klass);
  allocator_class = GST_ALLOCATOR_CLASS (klass);

  gobject_class->finalize = gst_vvas_allocator_finalize;
  gobject_class->set_property = gst_vvas_allocator_set_property;

  allocator_class->free = gst_vvas_allocator_free;
  allocator_class->alloc = gst_vvas_allocator_alloc;

  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("device-index", "VVAS device index",
          "VVAS device index to allocate memory", 0, G_MAXINT,
          DEFAULT_DEVICE_INDEX, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NEED_DMA,
      g_param_spec_boolean ("need-dma", "Expose as DMABuf Allocator",
          "Expose as DMABuf Allocator", DEFAULT_NEED_DMA,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MEM_BANK,
      g_param_spec_int ("mem-bank", "DDR Memory Bank",
          "DDR Memory bank to allocate memory", 0, G_MAXINT,
          DEFAULT_MEM_BANK, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gst_vvas_allocator_signals[VVAS_MEM_RELEASED] = g_signal_new ("vvas-mem-released",
      G_TYPE_FROM_CLASS (gobject_class), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_MEMORY);

  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
}

static void
gst_vvas_allocator_init (GstVvasAllocator * allocator)
{
  GstAllocator *alloc;

  alloc = GST_ALLOCATOR_CAST (allocator);
  allocator->priv = (GstVvasAllocatorPrivate *)
      gst_vvas_allocator_get_instance_private (allocator);

  alloc->mem_type = GST_VVAS_MEMORY_TYPE;
  alloc->mem_map = gst_vvas_mem_map;
  alloc->mem_unmap = gst_vvas_mem_unmap;
  alloc->mem_share = gst_vvas_mem_share;

  allocator->priv->dmabuf_alloc = gst_dmabuf_allocator_new ();
  allocator->priv->active = FALSE;
  allocator->priv->free_queue = NULL;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

GstAllocator *
gst_vvas_allocator_new (guint dev_idx, gboolean need_dma, guint mem_bank)
{
  GstAllocator *alloc = NULL;

  alloc = (GstAllocator *) g_object_new (GST_TYPE_VVAS_ALLOCATOR,
      "device-index", dev_idx, "need-dma", need_dma,
      "mem-bank", mem_bank, NULL);
  gst_object_ref_sink (alloc);
  return alloc;
}

gboolean
gst_vvas_allocator_start (GstVvasAllocator * vvas_alloc, guint min_mem,
    guint max_mem, gsize size, GstAllocationParams * params)
{
  GstVvasAllocatorPrivate *priv = vvas_alloc->priv;
  GstMemory *mem = NULL;
  guint i;

  g_return_val_if_fail (min_mem != 0, FALSE);

  if (g_atomic_int_get (&priv->active)) {
    GST_ERROR_OBJECT (vvas_alloc, "allocator is already active");
    return TRUE;
  }

  priv->min_mem = min_mem;
  priv->max_mem = max_mem;

  priv->free_queue = gst_atomic_queue_new (16);
  priv->poll = gst_poll_new_timer ();

  GST_INFO_OBJECT (vvas_alloc,
      "going to store %u memories with size %lu in queue %p", min_mem, size,
      priv->free_queue);

  for (i = 0; i < min_mem; i++) {
    mem = gst_vvas_allocator_alloc (GST_ALLOCATOR (vvas_alloc), size, params);
    if (!mem) {
      GST_ERROR_OBJECT (vvas_alloc, "failed allocate memory of size %lu", size);
      return FALSE;
    }
    GST_DEBUG_OBJECT (vvas_alloc,
        "pushing memory %p to free memory queue at index %u", mem, i);
    gst_atomic_queue_push (priv->free_queue, mem);
    gst_poll_write_control (priv->poll);
  }

  gst_poll_write_control (priv->poll);

  g_atomic_int_set (&priv->active, TRUE);

  return TRUE;
}

gboolean
gst_vvas_allocator_stop (GstVvasAllocator * vvas_alloc)
{
  GstVvasAllocatorPrivate *priv = vvas_alloc->priv;
  GstMemory *mem = NULL;
  GstVvasMemory *vvasmem = NULL;

  GST_DEBUG_OBJECT (vvas_alloc, "stop allocator");

  if (!g_atomic_int_get (&priv->active)) {
    GST_WARNING_OBJECT (vvas_alloc, "allocator is not active");
    goto error;
  }

  if (gst_atomic_queue_length (priv->free_queue) != priv->cur_mem) {
    GST_WARNING_OBJECT (vvas_alloc, "some buffers are still outstanding");
    goto error;
  }

  /* clear the pool */
  while ((mem = gst_atomic_queue_pop (priv->free_queue))) {
    while (!gst_poll_read_control (priv->poll)) {
      if (errno == EWOULDBLOCK) {
        /* We put the buffer into the queue but did not finish writing control
         * yet, let's wait a bit and retry */
        g_thread_yield ();
        continue;
      } else {
        /* Critical error but GstPoll already complained */
        break;
      }
    }
    GST_LOG_OBJECT (vvas_alloc, "freeing memory %p (%u left)", mem,
        priv->cur_mem);
    vvasmem = (GstVvasMemory *) mem;
    /* avoid feeding back to free queue */
    vvasmem->do_free = TRUE;
    gst_memory_unref (mem);
  }

  priv->cur_mem = 0;
  gst_atomic_queue_unref (priv->free_queue);
  g_atomic_int_set (&priv->active, FALSE);
  gst_poll_free (priv->poll);

  return TRUE;

error:
  return FALSE;
}

gboolean
gst_is_vvas_memory (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, FALSE);

  if (get_vvas_mem (mem))
    return TRUE;
  return FALSE;
}

guint64
gst_vvas_allocator_get_paddr (GstMemory * mem)
{
  GstVvasMemory *vvasmem;
  GstVvasAllocator *alloc;
  struct xclBOProperties p;

  g_return_val_if_fail (mem != NULL, FALSE);

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return 0;
  }

  alloc = vvasmem->alloc;

  if (!vvas_xrt_get_bo_properties (alloc->priv->handle, vvasmem->bo, &p)) {
    return p.paddr;
  } else {
    GST_ERROR_OBJECT (alloc, "failed to get physical address...");
    return 0;
  }
}

guint
gst_vvas_allocator_get_bo (GstMemory * mem)
{
  GstVvasMemory *vvasmem;

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return 0;
  }
  return vvasmem->bo;
}

gboolean
gst_vvas_memory_can_avoid_copy (GstMemory * mem, guint cur_devid,
				guint req_mem_bank)
{
  GstVvasAllocator *alloc;
  GstVvasMemory *vvasmem;

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return FALSE;
  }

  alloc = vvasmem->alloc;
  return (alloc->priv->dev_idx == cur_devid &&
	  alloc->priv->mem_bank == req_mem_bank);
}

guint
gst_vvas_allocator_get_device_idx (GstAllocator * allocator)
{
  GstVvasAllocator *alloc;
  g_return_val_if_fail (GST_IS_VVAS_ALLOCATOR (allocator), -1);
  alloc = GST_VVAS_ALLOCATOR (allocator);
  return alloc->priv->dev_idx;
}

#ifdef XLNX_PCIe_PLATFORM
void
gst_vvas_memory_set_sync_flag (GstMemory * mem, VvasSyncFlags flag)
{
  GstVvasMemory *vvasmem;
  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return;
  }
  vvasmem->sync_flags |= flag;
}

gboolean
gst_vvas_memory_sync_with_flags (GstMemory * mem, GstMapFlags flags)
{
  GstVvasMemory *vvasmem;
  GstVvasAllocator *alloc;

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return FALSE;
  }

  alloc = vvasmem->alloc;

  if ((flags & GST_MAP_READ) && (vvasmem->sync_flags & VVAS_SYNC_FROM_DEVICE)) {
    int iret = 0;

    GST_LOG_OBJECT (alloc,
        "sync from device %p : bo = %u, size = %lu, handle = %p", vvasmem,
        vvasmem->bo, vvasmem->size, alloc->priv->handle);

    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, alloc,
        "slow copy data from device");

    iret = vvas_xrt_sync_bo (alloc->priv->handle, vvasmem->bo,
                             VVAS_BO_SYNC_BO_FROM_DEVICE,
                             vvasmem->size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (alloc, "failed to sync output buffer. reason : %d, %s",
          iret, strerror (errno));
      return FALSE;
    }
    vvasmem->sync_flags &= ~VVAS_SYNC_FROM_DEVICE;
  }

  if (flags & GST_MAP_WRITE) {
    vvasmem->sync_flags |= VVAS_SYNC_TO_DEVICE;
    /* vvas plugins does VVAS_BO_SYNC_BO_TO_DEVICE to update data, for others dont care */
    GST_LOG_OBJECT (alloc, "enabling sync to device flag for %p", vvasmem);
  }
  return TRUE;
}

gboolean
gst_vvas_memory_sync_bo (GstMemory * mem)
{
  GstVvasMemory *vvasmem;
  GstVvasAllocator *alloc;
  int iret = 0;

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return 0;
  }

  alloc = vvasmem->alloc;

  GST_LOG_OBJECT (alloc, "sync bo %u of size %lu, handle = %p, flags %d",
      vvasmem->bo, vvasmem->size, alloc->priv->handle, vvasmem->sync_flags);

  if ((vvasmem->sync_flags & VVAS_SYNC_TO_DEVICE)
      && (vvasmem->sync_flags & VVAS_SYNC_FROM_DEVICE)) {
    GST_ERROR_OBJECT (alloc, "should not reach here.. both sync flags enabled");
    return FALSE;
  }

  if (vvasmem->sync_flags & VVAS_SYNC_TO_DEVICE) {
    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, alloc, "slow copy data to device");

    iret = vvas_xrt_sync_bo (alloc->priv->handle, vvasmem->bo,
                             VVAS_BO_SYNC_BO_TO_DEVICE,
                             vvasmem->size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (alloc,
          "failed to sync output buffer to device. reason : %d, %s", iret,
          strerror (errno));
      return FALSE;
    }
    vvasmem->sync_flags &= ~VVAS_SYNC_TO_DEVICE;
  }

  return TRUE;
}

void gst_vvas_memory_set_flag (GstMemory *mem, VvasSyncFlags flag)
{
  GstVvasMemory *vvasmem;

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return;
  }

  vvasmem->sync_flags = vvasmem->sync_flags & flag;
}

void gst_vvas_memory_unset_flag (GstMemory *mem, VvasSyncFlags flag)
{
  GstVvasMemory *vvasmem;

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return;
  }

  vvasmem->sync_flags = vvasmem->sync_flags & ~flag;
}
#endif
