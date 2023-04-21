/*
 * Copyright 2020 - 2022 Xilinx, Inc.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <vvas_core/vvas_device.h>
#include "gstvvasallocator.h"
#include <sys/mman.h>
#include <string.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/gstpoll.h>

/** @def GST_CAT_DEFAULT
 *  @brief Setting vvasallocator_debug as default debug category for logging
 */
#define GST_CAT_DEFAULT vvasallocator_debug

/**
 *  @brief Defines a static GstDebugCategory global variable "vvasallocator_debug"
 */
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/** @def GST_VVAS_MEMORY_TYPE
 *  @brief Assiging a name to VVASMemory to validate memory received is of specific type
 */
#define GST_VVAS_MEMORY_TYPE "VVASMemory"

/** @def DEFAULT_DEVICE_INDEX
 *  @brief Taking default device index as zero.
 */
#define DEFAULT_DEVICE_INDEX 0

/** @def DEFAULT_NEED_DMA
 *  @brief By default vvasallocator does not allocate DMA fd corresponding to XRT BO
 */
#define DEFAULT_NEED_DMA FALSE

/** @def DEFAULT_INIT_VALUE
 *  @brief By default vvasallocator initialize buffers with zero.
 */
#define DEFAULT_INIT_VALUE 0

/**  @brief  Contains properties related to VVAS allocator
 */
enum
{
  PROP_0,
  /** Device index prorperty ID */
  PROP_DEVICE_INDEX,
  /** Need DMA fd property ID */
  PROP_NEED_DMA,
  /** Memory bank index property ID */
  PROP_MEM_BANK,
  /** Initial buffer value property ID*/
  PROP_INIT_VALUE
};

/** @brief  Contains signals those will be emitted by VVAS allocator
 */
enum
{
  VVAS_MEM_RELEASED,
  LAST_SIGNAL
};

/** @struct _GstVvasAllocatorPrivate
 *  @brief  Holds private members related VVAS allocator instance
 */
struct _GstVvasAllocatorPrivate
{
  /** Index of the device on which memory is going to allocated */
  gint dev_idx;
  /** TRUE if one needs DMA fd corresponding to XRT BO */
  gboolean need_dma;
  /** Memory bank index (on a device with \p dev_idx) which is used to
   * allocate memory */
  guint mem_bank;
  /** Handle to FPGA device with index \p dev_idx */
  vvasDeviceHandle handle;
  /** Handle to DMA allocator to allocate GstFdMemory */
  GstAllocator *dmabuf_alloc;
  /** Holds the state of the VVAS allocator */
  gboolean active;
  /** Queue to hold free GstVvasMemory objects */
  GstAtomicQueue *free_queue;
  GstPoll *poll;
  /** Number of GstVvasMemory objects currently active in current instance */
  guint cur_mem;
  /** Minimum number of GstVvasMemory objects to allocated on _start () */
  guint min_mem;
  /** Allowed maximum number of GstVvasMemory objects to allocated using this
   * GstVvasAllocator instance */
  guint max_mem;
  /** Initial Buffer value */
  gint init_value;
};

/** @struct GstVvasMemory
 *  @brief  Holds members related VVAS Memory object
 */
typedef struct _GstVvasMemory
{
  GstMemory parent;
  /** Holds virtual address corresponding XRT BO in non-dma mode */
  gpointer data;
  /** Handle to XRT BO */
  vvasBOHandle bo;
  /** Allocated memory size */
  gsize size;
  /** Pointer to allocator instance by which memory is allocated */
  GstVvasAllocator *alloc;
  /** Flags used in mapping XRT BO to user space */
  GstMapFlags mmapping_flags;
  /** Count of current mapping requests */
  gint mmap_count;
  GMutex lock;
  /** If TRUE memory will freed. Else, memory object (GstVvasMemory) will be queued
   * _GstVvasAllocatorPrivate::free_queue to avoid memory fragmentation */
  gboolean do_free;
  /** Sync flags to device whether data need to synced (DMA transfer) between FPGA device and Host */
  VvasSyncFlags sync_flags;
} GstVvasMemory;

static guint gst_vvas_allocator_signals[LAST_SIGNAL] = { 0 };

#define parent_class gst_vvas_allocator_parent_class

/** @brief  Glib's convenience macro for GstVvasAllocator type implementation.
 *  @details This macro does below tasks:\n
 *             - Declares a class initialization function with prefix gst_vvas_allocator \n
 *             - Declares an instance initialization function\n
 *             - A static variable named gst_vvas_allocator_parent_class pointing to the parent class\n
 *             - Defines a gst_vvas_allocator_get_type() function with below tasks\n
 *                 - Initializes GTypeInfo function pointers\n
 *                 - Registers GstVvasAllocatorPrivate as private structure to GstVvasAllocator type\n
 *                 - Initialize new debug category vvasallocator for logging\n
 */
G_DEFINE_TYPE_WITH_CODE (GstVvasAllocator, gst_vvas_allocator,
    GST_TYPE_ALLOCATOR, G_ADD_PRIVATE (GstVvasAllocator);
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vvasallocator", 0,
        "VVAS allocator"));

/**
 *  @brief Defines a static GstDebugCategory global variable with name GST_CAT_PERFORMANCE for
 *  performance logging purpose
 */
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

/**
 *  @fn static GstVvasMemory *get_vvas_mem (GstMemory * mem)
 *  @param [in] mem - Handle to GstMemory
 *  @return GstVvasMemory pointer on success\n  NULL on failure
 *  @brief  Gets GstVvasMemory object from GstMemory object
 */
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

/**
 *  @fn static gboolean vvas_allocator_memory_dispose (GstMiniObject * obj)
 *  @param [in] obj - Handle to GstMiniObject which will be typecasted to GstVvasMemory.
 *                    Here, GstMiniObject is grand-parent of GstVvasMemory
 *  @return TRUE if GstVvasMemory object need to be freed\n
 *          FALSE if GstVvasMemory to be queued in GstVvasAllocator's queue.
 *  @brief  This API decides whether GstVvasMemory object need to be freed or
 *          queued in GstVvasAllocator to reuse the GstVvasMemory object. If this API returns TRUE,
 *          GStreamer framework will call gst_vvas_allocator_free() to free up current memory object
 */
static gboolean
vvas_allocator_memory_dispose (GstMiniObject * obj)
{
  GstMemory *mem = GST_MEMORY_CAST (obj);
  GstVvasMemory *vvasmem = (GstVvasMemory *) mem;
  GstVvasAllocator *vvas_alloc = (GstVvasAllocator *) mem->allocator;
  GstVvasAllocatorPrivate *priv = vvas_alloc->priv;

  if (priv->free_queue && !vvasmem->do_free) {
    GST_DEBUG_OBJECT (vvas_alloc, "pushing back memory %p to free queue", mem);
    /* push current memory object to free queue and wakes up polling to pop
     * from queue */
    gst_atomic_queue_push (priv->free_queue, gst_memory_ref (mem));
    gst_poll_write_control (priv->poll);
    g_signal_emit (vvas_alloc, gst_vvas_allocator_signals[VVAS_MEM_RELEASED], 0,
        mem);
    return FALSE;
  }

  /* returning TRUE to free current memory object */
  return TRUE;
}

/**
 *  @fn static GstMemory *gst_vvas_allocator_alloc (GstAllocator * allocator,
 *                                                  gsize size,
 *                                                  GstAllocationParams * params)
 *  @param [in] allocator - Pointer allocator object
 *  @param [in] size - Size of the memory to be allocated
 *  @param [in] params - Holds parameters related to memory allocation
 *  @return GstMemory pointer on success\n NULL on failure
 *  @brief  Allocates memory of request size or pop from free memory queue
 *  @details If gst_vvas_allocator_start is called by user, then this API gets memory objects from free queue or else,
 *           memory will be allocated based on \p size and allocation parameters \p params
 */
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
  void *data = NULL;

  /* take buffers from free_queue as we have preallocated memory objects */
  if (priv->free_queue && g_atomic_int_get (&priv->active)) {
    // TODO: peek memory and check size of popped mem is sufficient or not

  pop_now:
    mem = gst_atomic_queue_pop (priv->free_queue);
    if (G_LIKELY (mem)) {
      while (!gst_poll_read_control (priv->poll)) {
        if (errno == EWOULDBLOCK) {
          /* We put the buffer into the queue but did not finish writing
           * control yet, let's wait a bit and retry */
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
        if (params->flags & GST_VVAS_ALLOCATOR_FLAG_DONTWAIT) {
          GST_DEBUG_OBJECT (vvas_alloc, "don't wait for memory, return NULL");
          return NULL;
        }
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
        goto pop_now;           // now try popping memory as wait is completed
      }
    }
  }

  if (priv->handle == NULL) {
    GST_ERROR_OBJECT (vvas_alloc, "failed get handle from VVAS");
    return NULL;
  }

  /* Start creating memory object of requested size */
  vvasmem = g_slice_new0 (GstVvasMemory);
  gst_memory_init (GST_MEMORY_CAST (vvasmem), params->flags,
      GST_ALLOCATOR_CAST (vvas_alloc), NULL, size, params->align,
      params->prefix, size);

  if (priv->free_queue) {
    /* override dispose function so that allocator will get a callback when
     * memory is about to freed */
    vvasmem->parent.mini_object.dispose = (GstMiniObjectDisposeFunction)
        vvas_allocator_memory_dispose;
  }

  GST_DEBUG_OBJECT (vvas_alloc, "memory allocated %p with size %lu, flags %d, "
      "align %lu, prefix %lu", vvasmem, size, params->flags, params->align,
      params->prefix);

  vvasmem->alloc = vvas_alloc;
  g_mutex_init (&vvasmem->lock);

  vvasmem->sync_flags = VVAS_SYNC_NONE;

  /* allocate XRT buffer on device and host */
  vvasmem->bo = vvas_xrt_alloc_bo (priv->handle, size, VVAS_BO_FLAGS_NONE,
      priv->mem_bank);
  if (vvasmem->bo == NULL) {
    GST_ERROR_OBJECT (vvas_alloc, "failed to allocate Device BO. reason %s(%d)",
        strerror (errno), errno);
    return NULL;
  }
  vvasmem->size = size;
  vvasmem->do_free = FALSE;

  /* Based on caller request, we need to reset data to 0 to
   * avoid garbage data in padded video frames.*/
  if (params->flags & GST_VVAS_ALLOCATOR_FLAG_MEM_INIT) {
    GST_LOG_OBJECT (vvas_alloc, "Doing memset for created buffer");
    /* Get user space address corresponding host buffer */
    data = vvas_xrt_map_bo (vvasmem->bo, true);
    if (data) {
      memset (data, priv->init_value, size);
      /* Once data is set to zero, sync the data from host to device via
       * DMA transfer */
      iret = vvas_xrt_sync_bo (vvasmem->bo, VVAS_BO_SYNC_BO_TO_DEVICE, size, 0);
      if (iret != 0) {
        GST_ERROR_OBJECT (vvas_alloc,
            "failed to sync output buffer. reason : %d, %s",
            iret, strerror (errno));
        //vvas_xrt_unmap_bo(priv->handle, vvasmem->bo, data);
        return NULL;
      }
      //vvas_xrt_unmap_bo(priv->handle, vvasmem->bo, data);
    }
  }

  /* Creates DMA fd corresponding to XRT BO.
   * Currently DMA fd is supported only in Embedded platforms only */
  if (priv->need_dma) {
    prime_fd = vvas_xrt_export_bo (vvasmem->bo);
    if (prime_fd < 0) {
      GST_ERROR_OBJECT (vvas_alloc, "failed to get dmafd...");
      vvas_xrt_free_bo (vvasmem->bo);
      vvasmem->bo = 0;
      return NULL;
    }

    GST_DEBUG_OBJECT (vvas_alloc,
        "exported xrt bo %p as dmafd %d", vvasmem->bo, prime_fd);

    mem =
        gst_dmabuf_allocator_alloc_with_flags (priv->dmabuf_alloc, prime_fd,
        size, GST_FD_MEMORY_FLAG_DONT_CLOSE);

    gst_mini_object_set_qdata (GST_MINI_OBJECT (mem),
        g_quark_from_static_string ("vvasmem"), vvasmem,
        (GDestroyNotify) gst_memory_unref);
  } else {
    mem = GST_MEMORY_CAST (vvasmem);
  }

  priv->cur_mem++;
  return mem;
}

/**
 *  @fn static gpointer gst_vvas_mem_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
 *  @param [in] mem - Pointer to memory object
 *  @param [in] maxsize - Size of the memory to be mapped [unused as XRT maps entire memory]
 *  @param [in] flags - Flags needed while mapping memory
 *  @return User space address on success\n  NULL on failure
 *  @brief Maps memory to user space address for reading and/or writing
 *  @details Based on \p flags and GstVvasMemory::sync_flags, this API does DMA transfer between host and device
 *           before mapping memory to host side
 */
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
  /* Before mapping, synchronize the data between host and
   * device if required */
  if (!gst_vvas_memory_sync_with_flags (mem, flags)) {
    g_mutex_unlock (&vvasmem->lock);
    return NULL;
  }

  /* Mapping memory to user space on host */
  vvasmem->data = vvas_xrt_map_bo (vvasmem->bo, flags & GST_MAP_WRITE);
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

/**
 *  @fn static GstMemory *gst_vvas_mem_share (GstMemory * mem, gssize offset, gssize size)
 *  @param [in] mem - Pointer to memory object
 *  @param [in] offset - Offset in \p mem object.[unused as entire memory is shared]
 *  @param [in] size - Size of the memory to be shared.[unused as entire memory is shared]
 *  @return GstMemory pointer
 *  @brief Creates memory object in readonly mode by sharing \p mem object
 */
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

  /* shared memory is always readonly and make vvasmem object as parent
   * sub object */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, vvasmem->parent.allocator, parent,
      vvasmem->parent.maxsize, vvasmem->parent.align,
      vvasmem->parent.offset + offset, size);

  g_mutex_init (&sub->lock);
  sub->bo = vvasmem->bo;
  sub->alloc = vvasmem->alloc;
  sub->size = vvasmem->size;

  sub->sync_flags = vvasmem->sync_flags;

  GST_DEBUG ("%p: share mem created", sub);

  return GST_MEMORY_CAST (sub);
}

/**
 *  @fn static void gst_vvas_mem_unmap (GstMemory * mem)
 *  @param [in] mem - Pointer to memory object
 *  @return None
 *  @brief Unmaps the memory from user space
 */
static void
gst_vvas_mem_unmap (GstMemory * mem)
{
  GstVvasMemory *vvasmem = (GstVvasMemory *) mem;
  GstVvasAllocator *alloc = vvasmem->alloc;
  int ret = 0;

  /* unmaps parent if its sub memory object created in _share API */
  if (mem->parent)
    return gst_vvas_mem_unmap (mem->parent);

  g_mutex_lock (&vvasmem->lock);
  /* unmap memory only if there is no references to it */
  if (vvasmem->data && !(--vvasmem->mmap_count)) {
    /* unmap memory using XRT API */
    ret = vvas_xrt_unmap_bo (vvasmem->bo, vvasmem->data);
    if (ret) {
      GST_ERROR ("failed to unmap %p", vvasmem->data);
    }
    vvasmem->data = NULL;
    vvasmem->mmapping_flags = 0;
    GST_DEBUG_OBJECT (alloc, "%p: bo %p unmapped", vvasmem, vvasmem->bo);
  }
  g_mutex_unlock (&vvasmem->lock);
}

/**
 *  @fn static void gst_vvas_allocator_free (GstAllocator * allocator, GstMemory * mem)
 *  @param [in] allocator - Pointer to allocator instance
 *  @param [in] mem - Memory pointer to be freed
 *  @return None
 *  @brief Frees the memory associated with object \p mem
 */
static void
gst_vvas_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstVvasMemory *vvasmem = (GstVvasMemory *) mem;
  GstVvasAllocator *alloc = GST_VVAS_ALLOCATOR (allocator);

  /* free memory using XRT API if its not a sub buffer */
  if (vvasmem->bo != NULL && mem->parent == NULL)
    vvas_xrt_free_bo (vvasmem->bo);

  GST_DEBUG ("freeing mem: %p", mem);

  g_mutex_clear (&vvasmem->lock);
  g_slice_free (GstVvasMemory, vvasmem);

  GST_OBJECT_LOCK (alloc);
  alloc->priv->cur_mem--;
  GST_OBJECT_UNLOCK (alloc);

}

/**
 *  @fn static void gst_vvas_allocator_finalize (GObject * obj)
 *  @param [in] obj - Handle to GstVvasAllocator typecasted to GObject
 *  @return None
 *  @brief This API will be called during GstVvasAllocator object's destruction phase.
 *              Close references to devices and free memories if any
 *  @note After this API GstVvasAllocator object \p obj will be destroyed completely.
 *              So free all internal memories held by current object
 */
static void
gst_vvas_allocator_finalize (GObject * obj)
{
  GstVvasAllocator *alloc = GST_VVAS_ALLOCATOR (obj);

  if (alloc->priv->dmabuf_alloc)
    gst_object_unref (alloc->priv->dmabuf_alloc);

  /* release handle to device */
  vvas_xrt_close_device (alloc->priv->handle);

  /* call GstVvasAllocator's parent object finalize API, so that parent can also do its cleanup */
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/**
 *  @fn static void gst_vvas_allocator_set_property (GObject * object,
 *                                                   guint prop_id,
 *                                                   const GValue * value,
 *                                                   GParamSpec * pspec)
 *  @param [in] object - Handle to GstVvasAllocator typecasted to GObject
 *  @param [in] prop_id - Property ID value
 *  @param [in] value - GValue which holds property value set by user
 *  @param [in] pspec - Handle to metadata of a property with property ID \p prop_id
 *  @return None
 *  @brief This API stores values sent from the user in GstVvasAllocator object members.
 *  @details This API is registered with GObjectClass by overriding GObjectClass::set_property function
 *           pointer and this will be invoked when developer sets properties on GstVvasAllocator object.
 *           Based on property value type, corresponding g_value_get_xxx API will be called to get
 *           property value from GValue handle.
 */
static void
gst_vvas_allocator_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVvasAllocator *alloc = GST_VVAS_ALLOCATOR (object);

  switch (prop_id) {
    case PROP_DEVICE_INDEX:
      alloc->priv->dev_idx = g_value_get_int (value);
      if (!vvas_xrt_open_device (alloc->priv->dev_idx, &alloc->priv->handle)) {
        GST_ERROR_OBJECT (alloc, "failed to open device");
      }
      break;
    case PROP_NEED_DMA:
      alloc->priv->need_dma = g_value_get_boolean (value);
      break;
    case PROP_MEM_BANK:
      alloc->priv->mem_bank = g_value_get_int (value);
      break;
    case PROP_INIT_VALUE:
      alloc->priv->init_value = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 *  @fn static void gst_vvas_allocator_class_init (GstVvasAllocatorClass * klass)
 *  @param [in]klass  - Handle to GstVvasAllocatorClass
 *  @return None
 *  @brief Add properties and signals of GstVvasAllocator to parent GObjectClass and ovverrides
 *         function pointers present in itself and/or its parent class structures
 *  @details This function publishes properties those can be set/get from application on
 *                  GstVvasAllocator object. And, while publishing a property it also declares type, range of
 *                  acceptable values, default value, readability/writability and in which GStreamer state a
 *                  property can be changed.
 */
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

  g_object_class_install_property (gobject_class, PROP_INIT_VALUE,
      g_param_spec_int ("init-value", "Initial Buffer value",
          "Allocator will set as a initial value for buffer memory", 0,
          G_MAXINT, DEFAULT_INIT_VALUE,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  gst_vvas_allocator_signals[VVAS_MEM_RELEASED] =
      g_signal_new ("vvas-mem-released", G_TYPE_FROM_CLASS (gobject_class),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_MEMORY);

  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
}

/**
 *  @fn static void gst_vvas_allocator_init (GstVvasAllocator * allocator)
 *  @param [in] allocator - Handle to GstVvasAllocator instance
 *  @return None
 *  @brief  Initilizes GstVvasAllocator member variables to default and does one time object/memory allocations
 *          in object's lifecycle
 *  @details Overrides GstAllocator object's function pointers so that operations performance on GstMemory will invoke
 *           GstVvasAllocator APIs. For example, gst_memory_map invokes gst_vvas_mem_map() to map an XRT buffer to
 *           user space
 */
static void
gst_vvas_allocator_init (GstVvasAllocator * allocator)
{
  GstAllocator *alloc;

  alloc = GST_ALLOCATOR_CAST (allocator);
  /* get instance's private structure and store for quick referencing */
  allocator->priv = (GstVvasAllocatorPrivate *)
      gst_vvas_allocator_get_instance_private (allocator);

  /* overrides function pointers with APIs those need to be triggered
   * when GstMemory's member functions are called */
  alloc->mem_type = GST_VVAS_MEMORY_TYPE;
  alloc->mem_map = gst_vvas_mem_map;
  alloc->mem_unmap = gst_vvas_mem_unmap;
  alloc->mem_share = gst_vvas_mem_share;

  allocator->priv->dmabuf_alloc = gst_dmabuf_allocator_new ();
  allocator->priv->active = FALSE;
  allocator->priv->free_queue = NULL;
  allocator->priv->init_value = DEFAULT_INIT_VALUE;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

/**
 *  @fn GstAllocator* gst_vvas_allocator_new (guint dev_idx,
 *                                            gboolean need_dma,
 *                                            guint mem_bank)
 *  @param [in] dev_idx - Index of the FPGA device on which memory need to allocated. Developer can get device indexes
 *                        by calling XRT utility applications
 *  @param [in] need_dma - To decide whether allocator needs to allocate DMA fd corresponding to XRT BO or not
 *  @param [in] mem_bank - Memory bank index on a device with an index \p dev_idx
 *  @return GstAllocator pointer on success\n NULL on failure
 *  @brief  Allocates GstVvasAllocator object using parameters passed to this API.
 */
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

/**
 *  @fn GstAllocator* gst_vvas_allocator_new_and_set (guint dev_idx,
 *                                                    gboolean need_dma,
 *                                                    guint mem_bank,
 *                                                    gint init_value)
 *  @param [in] dev_idx - Index of the FPGA device on which memory need to allocated. Developer can get device indexes
 *                        by calling XRT utility applications
 *  @param [in] need_dma - To decide whether allocator needs to allocate DMA fd corresponding to XRT BO or not
 *  @param [in] mem_bank - Memory bank index on a device with an index \p dev_idx
 *  @param [in] init_value - Initial value for allocated buffers
 *  @return GstAllocator pointer on success\n NULL on failure
 *  @brief  Allocates GstVvasAllocator object using parameters passed to this API.
 */
GstAllocator *
gst_vvas_allocator_new_and_set (guint dev_idx, gboolean need_dma,
    guint mem_bank, gint init_value)
{
  GstAllocator *alloc = NULL;

  alloc = (GstAllocator *) g_object_new (GST_TYPE_VVAS_ALLOCATOR,
      "device-index", dev_idx, "need-dma", need_dma,
      "mem-bank", mem_bank, "init-value", init_value, NULL);
  gst_object_ref_sink (alloc);
  return alloc;
}

/**
 *  @fn gboolean gst_vvas_allocator_start (GstVvasAllocator * vvas_alloc,
 *                                         guint min_mem,
 *                                         guint max_mem,
 *                                         gsize size,
 *                                         GstAllocationParams * params)
 *  @param [in] vvas_alloc - Pointer to GstVvasAllocator object
 *  @param [in] min_mem - Minimum number of memories to be preallocated
 *  @param [in] max_mem - Maximum number of memories allowed to be allocated using \p vvas_alloc. If max_mem is zero,
 *                        memories can be allocated until device memory exhausts
 *  @param [in] size - Size of the memory which will be held GstMemory object
 *  @param [in] params - Pointer to GstAllocationParams used to allocate memory
 *  @return TRUE on success.\n FALSE on failure.
 *  @brief Allocates \p min_mem memories and holds them in a queue. Developers can call this API to avoid memory
 *         fragmentation.
 */
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

  /* allocating queue of initialize 16, but queue will be expanded when we add
   *  objects */
  priv->free_queue = gst_atomic_queue_new (16);
  priv->poll = gst_poll_new_timer ();

  GST_INFO_OBJECT (vvas_alloc,
      "going to store %u memories with size %lu in queue %p", min_mem, size,
      priv->free_queue);

  /* preallocate minimum number of memories and queue them */
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

  /* activate memory free queue */
  g_atomic_int_set (&priv->active, TRUE);

  return TRUE;
}

/**
 *  @fn gboolean gst_vvas_allocator_stop (GstVvasAllocator * vvas_alloc)
 *  @param [in] vvas_alloc - Pointer to GstVvasAllocator object
 *  @return TRUE on success\n FALSE on failure
 *  @brief Frees all memories holded in queue and destorys the queue
 */
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

  /* check all allocated memories came back to queue */
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
  /* deactivate memory free queue */
  g_atomic_int_set (&priv->active, FALSE);
  gst_poll_free (priv->poll);

  return TRUE;

error:
  return FALSE;
}

/**
 *  @fn gboolean gst_is_vvas_memory (GstMemory * mem)
 *  @param [in] mem - Pointer to GstMemory object
 *  @return TRUE if memory object is a GstVvasMemory type, else return FALSE
 *  @brief Validates whether a GstMemory object is GstVvasMemory type or not
 */
gboolean
gst_is_vvas_memory (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, FALSE);

  if (get_vvas_mem (mem))
    return TRUE;
  return FALSE;
}

/**
 *  @fn guint64 gst_vvas_allocator_get_paddr (GstMemory * mem)
 *  @param [in] mem - Pointer to GstMemory object
 *  @return Physical address corresponding to data on success.\n 0 on failure.
 *  @brief Gets physical address corresponding to GstVvasMemory \p mem
 */
guint64
gst_vvas_allocator_get_paddr (GstMemory * mem)
{
  GstVvasMemory *vvasmem;

  g_return_val_if_fail (mem != NULL, FALSE);

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return 0;
  }

  return vvas_xrt_get_bo_phy_addres (vvasmem->bo);
}

/**
 *  @fn void* gst_vvas_allocator_get_bo (GstMemory * mem)
 *  @param [in] mem - Pointer to GstMemory object
 *  @return Pointer to XRT BO handle on success.\n NULL on failure
 *  @brief Gets XRT BO corresponding to GstVvasMemory \p mem
 */
void *
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

/**
 *  @fn gboolean gst_vvas_memory_can_avoid_copy (GstMemory * mem,
 *                                               guint cur_devid,
 *                                               guint req_mem_bank)
 *  @param [in] mem - Pointer to GstMemory object
 *  @param [in] cur_devid - Index device which is going to be compared with device index on which memory allocated
 *  @param [in] req_mem_bank - Memory bank index which will be compared against memory's bank index
 *  @return TRUE if zero copy possible\n FALSE if copy is needed
 *  @brief This API returns TRUE if requested device index and memory bank index are same as \p mem device index &
 *         memory bank index, else returns false
 */
gboolean
gst_vvas_memory_can_avoid_copy (GstMemory * mem, guint cur_devid,
    guint req_mem_bank)
{
  GstVvasAllocator *alloc;
  GstVvasMemory *vvasmem;

  /* Check whether memory is of GstVvasMemory type */
  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return FALSE;
  }

  alloc = vvasmem->alloc;
  return (alloc->priv->dev_idx == cur_devid &&
      alloc->priv->mem_bank == req_mem_bank);
}

/**
 *  @fn guint gst_vvas_allocator_get_device_idx (GstAllocator * allocator)
 *  @param [in] allocator - Pointer to GstVvasAllocator object
 *  @return Device index of the \p allocator object on success. (guint)-1 on failure
 *  @brief This API returns device index which is set while creating GstVvasAllocator object using
 *         gst_vvas_allocator_new
 */
guint
gst_vvas_allocator_get_device_idx (GstAllocator * allocator)
{
  GstVvasAllocator *alloc;
  g_return_val_if_fail (GST_IS_VVAS_ALLOCATOR (allocator), -1);
  alloc = GST_VVAS_ALLOCATOR (allocator);
  return alloc->priv->dev_idx;
}

/**
 *  @fn void gst_vvas_memory_set_sync_flag (GstMemory * mem, VvasSyncFlags flag)
 *  @param [in]  mem - Pointer to GstMemory object
 *  @param [in] flag - Flags to be set on \p mem object
 *  @return None
 *  @brief API to set data synchronization flags on GstVvasMemory object
 */
void
gst_vvas_memory_set_sync_flag (GstMemory * mem, VvasSyncFlags flag)
{
  GstVvasMemory *vvasmem;
  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    return;
  }
  vvasmem->sync_flags |= flag;
}

/**
 *  @fn void gst_vvas_memory_get_sync_flag (GstMemory *mem)
 *  @param [in] mem - Pointer to GstMemory object
 *  @return VvasSyncFlags
 *  @brief API to get VvasSyncFlags on a GstVvasMemory object
 */
VvasSyncFlags
gst_vvas_memory_get_sync_flag (GstMemory * mem)
{
  GstVvasMemory *vvasmem;
  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return VVAS_SYNC_NONE;
  }
  return vvasmem->sync_flags;
}

/**
 *  @fn gboolean gst_vvas_memory_sync_with_flags (GstMemory * mem, GstMapFlags flags)
 *  @param [in] mem - Pointer to GstMemory object
 *  @param [in] flags - GStreamer mapping flags which decides whether data need to synchronized between host and device
 *  @return TRUE on success.\n FALSE on failure
 *  @brief Synchronize the data vai DMA transfer between host memory and device memory corresponding to a XRT BO based
 *         on GstMapFlags
 */
gboolean
gst_vvas_memory_sync_with_flags (GstMemory * mem, GstMapFlags flags)
{
  GstVvasMemory *vvasmem;
  GstVvasAllocator *alloc;

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    /* Check whether it is a dma memory */
    if (gst_is_dmabuf_memory (mem)) {
      GST_DEBUG ("%p is dma non-vvas memory, don't sync", mem);
      return TRUE;
    } else {
      GST_ERROR ("failed to get vvas memory");
      return FALSE;
    }
  }

  alloc = vvasmem->alloc;

  /* synchronize data from device to host when application would like to read
   *  from host virtual address */
  if ((flags & GST_MAP_READ) && (vvasmem->sync_flags & VVAS_SYNC_FROM_DEVICE)) {
    int iret = 0;

    GST_LOG_OBJECT (alloc,
        "sync from device %p : bo = %p, size = %lu, handle = %p", vvasmem,
        vvasmem->bo, vvasmem->size, alloc->priv->handle);

    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, alloc,
        "slow copy data from device");

    /* DMA transfer data from device to host */
    iret = vvas_xrt_sync_bo (vvasmem->bo,
        VVAS_BO_SYNC_BO_FROM_DEVICE, vvasmem->size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (alloc, "failed to sync output buffer. reason : %d, %s",
          iret, strerror (errno));
      return FALSE;
    }
    /* disable the sync flag after sync operation is completed */
    vvasmem->sync_flags &= ~VVAS_SYNC_FROM_DEVICE;
  }

  /* When user is mapping memory in WRITE mode, it is assumed that data need
   * to synced to device */
  if (flags & GST_MAP_WRITE) {
    vvasmem->sync_flags |= VVAS_SYNC_TO_DEVICE;
    /* vvas plugins does VVAS_BO_SYNC_BO_TO_DEVICE to update data, for others
     * dont care */
    GST_LOG_OBJECT (alloc, "enabling sync to device flag for %p", vvasmem);
  }
  return TRUE;
}

/**
 *  @fn gboolean gst_vvas_memory_sync_bo (GstMemory * mem)
 *  @param [in] mem - Pointer to GstMemory object
 *  @return TRUE on success.\n FALSE on failure
 *  @brief Synchronize data from host to device based on GstVvasMemory::sync_flags. Data from device to host
 *         automatically happens when memory is mapped in READ mode. However, when data need to be synchronized from
 *         host to device this API need to called.
 */
gboolean
gst_vvas_memory_sync_bo (GstMemory * mem)
{
  GstVvasMemory *vvasmem;
  GstVvasAllocator *alloc;
  int iret = 0;

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    /* Check whether memory is of GstVvasMemory type */
    if (gst_is_dmabuf_memory (mem)) {
      GST_DEBUG ("%p is dma non-vvas memory, don't sync", mem);
      return TRUE;
    } else {
      GST_ERROR ("failed to get vvas memory");
      return FALSE;
    }
  }

  alloc = vvasmem->alloc;

  GST_LOG_OBJECT (alloc, "sync bo %p of size %lu, handle = %p, flags %d",
      vvasmem->bo, vvasmem->size, alloc->priv->handle, vvasmem->sync_flags);

  if ((vvasmem->sync_flags & VVAS_SYNC_TO_DEVICE)
      && (vvasmem->sync_flags & VVAS_SYNC_FROM_DEVICE)) {
    GST_ERROR_OBJECT (alloc, "should not reach here.. both sync flags enabled");
    return FALSE;
  }

  if (vvasmem->sync_flags & VVAS_SYNC_TO_DEVICE) {
    GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, alloc, "slow copy data to device");

    /* sync data using DMA transfer */
    iret = vvas_xrt_sync_bo (vvasmem->bo,
        VVAS_BO_SYNC_BO_TO_DEVICE, vvasmem->size, 0);
    if (iret != 0) {
      GST_ERROR_OBJECT (alloc,
          "failed to sync output buffer to device. reason : %d, %s", iret,
          strerror (errno));
      return FALSE;
    }
    /* unset flag after successful transfer */
    vvasmem->sync_flags &= ~VVAS_SYNC_TO_DEVICE;
  }

  return TRUE;
}

/**
 *  @fn void gst_vvas_memory_set_flag (GstMemory *mem, VvasSyncFlags flag)
 *  @param [in] mem - Pointer to GstMemory object
 *  @param [in] flag - Flag to be enabled on \p mem
 *  @return None
 *  @brief API to enable VvasSyncFlags on a GstVvasMemory object
 */
void
gst_vvas_memory_set_flag (GstMemory * mem, VvasSyncFlags flag)
{
  GstVvasMemory *vvasmem;

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return;
  }

  vvasmem->sync_flags = vvasmem->sync_flags | flag;
}

/**
 *  @fn void gst_vvas_memory_unset_flag (GstMemory *mem, VvasSyncFlags flag)
 *  @param [in] mem - Pointer to GstMemory object
 *  @param [in] flag - Flag to be disabled on \p mem
 *  @return None
 *  @brief API to disable VvasSyncFlags on a GstVvasMemory object
 */
void
gst_vvas_memory_unset_flag (GstMemory * mem, VvasSyncFlags flag)
{
  GstVvasMemory *vvasmem;

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return;
  }

  vvasmem->sync_flags = vvasmem->sync_flags & ~flag;
}

/**
 *  @fn void gst_vvas_memory_reset_sync_flag (GstMemory * mem)
 *  @param [in] mem - Pointer to GstMemory object
 *  @return None
 *  @brief API to reset all VvasSyncFlags on a GstVvasMemory object
 */
void
gst_vvas_memory_reset_sync_flag (GstMemory * mem)
{
  GstVvasMemory *vvasmem;

  vvasmem = get_vvas_mem (mem);
  if (vvasmem == NULL) {
    GST_ERROR ("failed to get vvas memory");
    return;
  }

  vvasmem->sync_flags = VVAS_SYNC_NONE;
}
