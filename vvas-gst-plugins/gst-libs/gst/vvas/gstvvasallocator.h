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

/** @def GST_TYPE_VVAS_ALLOCATOR
 *  @brief Macro to get GstVvasAllocator object type
 */
#define GST_TYPE_VVAS_ALLOCATOR  \
   (gst_vvas_allocator_get_type())

/** @def GST_IS_VVAS_ALLOCATOR
 *  @brief Macro to validate whether object is of GstVvasAllocator type
 */
#define GST_IS_VVAS_ALLOCATOR(obj)       \
   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VVAS_ALLOCATOR))

/** @def GST_IS_VVAS_ALLOCATOR_CLASS
 *  @brief Macro to validate whether object class  is of GstVvasAllocatorClass type
 */
#define GST_IS_VVAS_ALLOCATOR_CLASS(klass)     \
   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VVAS_ALLOCATOR))

/** @def GST_VVAS_ALLOCATOR_GET_CLASS
 *  @brief Macro to get object GstVvasAllocatorClass object from GstVvasAllocator object
 */
#define GST_VVAS_ALLOCATOR_GET_CLASS(obj)      \
   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VVAS_ALLOCATOR, GstVvasAllocatorClass))

/** @def GST_VVAS_ALLOCATOR
 *  @brief Macro to typecast parent object to GstVvasAllocator object
 */
#define GST_VVAS_ALLOCATOR(obj)        \
   (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VVAS_ALLOCATOR, GstVvasAllocator))

/** @def GST_VVAS_ALLOCATOR_CLASS
 *  @brief Macro to typecast parent class object to GstVvasAllocatorClass object
 */
#define GST_VVAS_ALLOCATOR_CLASS(klass)      \
   (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VVAS_ALLOCATOR, GstVvasAllocatorClass))

typedef struct _GstVvasAllocator GstVvasAllocator;
typedef struct _GstVvasAllocatorClass GstVvasAllocatorClass;
typedef struct _GstVvasAllocatorPrivate GstVvasAllocatorPrivate;

enum _GstVvasAllocatorFlags
{
  /** To reset memory to specific value */
  GST_VVAS_ALLOCATOR_FLAG_MEM_INIT = (GST_ALLOCATOR_FLAG_LAST << 0),
  /** Return from alloc function without wait, when number of memory objects limit is reached &
   * memory objects are not present in free queue */
  GST_VVAS_ALLOCATOR_FLAG_DONTWAIT = (GST_ALLOCATOR_FLAG_LAST << 1),
};

struct _GstVvasAllocator
{
  /** parent of GstVvasAllocator object */
  GstAllocator parent;
  /** Pointer instance's private structure for each of access */
  GstVvasAllocatorPrivate *priv;
};

struct _GstVvasAllocatorClass {
  /** parent class */
  GstAllocatorClass parent_class;
};

GST_EXPORT
GType gst_vvas_allocator_get_type (void) G_GNUC_CONST;

/**
 *  @fn GstAllocator* gst_vvas_allocator_new (guint dev_idx,
 *                                            gboolean need_dma,
 *                                            guint mem_bank)
 *  @param [in] dev_idx Index of the FPGA device on which memory need to allocated.
 *                      Developer can get device indexes by calling XRT utility applications
 *  @param [in] need_dma To decide whether allocator needs to allocate DMA fd corresponding to XRT BO or not
 *  @param [in] mem_bank Memory bank index on a device with an index \p dev_idx
 *  @return GstAllocator pointer on success\n NULL on failure
 *  @brief  Allocates GstVvasAllocator object using parameters passed to this API.
 */
GST_EXPORT
GstAllocator* gst_vvas_allocator_new (guint dev_idx, gboolean need_dma,
                                 guint mem_bank);

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
GST_EXPORT
GstAllocator * gst_vvas_allocator_new_and_set (guint dev_idx,
    gboolean need_dma, guint mem_bank, gint init_value);

/**
 *  @fn gboolean gst_vvas_allocator_start (GstVvasAllocator * vvas_alloc,
 *                                         guint min_mem,
 *                                         guint max_mem,
 *                                         gsize size,
 *                                         GstAllocationParams * params)
 *  @param [in] vvas_alloc Pointer to GstVvasAllocator object
 *  @param [in] min_mem Minimum number of memories to be preallocated
 *  @param [in] max_mem Maximum number of memories allowed to be allocated using @vvas_alloc.
 *                      If max_mem is zero, memories can be allocated until device memory exhausts
 *  @param [in] size Size of the memory which will be held GstMemory object
 *  @param [in] params Pointer to GstAllocationParams used to allocate memory
 *  @return TRUE on success\n FALSE on failure
 *  @brief Allocates @min_mem memories and holds them in a queue. Developers can call this API to
 *         avoid memory fragmentation.
 */
GST_EXPORT
gboolean gst_vvas_allocator_start (GstVvasAllocator * allocator, guint min_mem,
    guint max_mem, gsize size, GstAllocationParams * params);

/**
 *  @fn gboolean gst_vvas_allocator_stop (GstVvasAllocator * vvas_alloc)
 *  @param [in] vvas_alloc Pointer to GstVvasAllocator object
 *  @return TRUE on success\n FALSE on failure
 *  @brief Frees all memories holded in queue and destorys the queue
 */
GST_EXPORT
gboolean gst_vvas_allocator_stop (GstVvasAllocator * allocator);

/**
 *  @fn gboolean gst_is_vvas_memory (GstMemory * mem)
 *  @param [in] mem Pointers to GstMemory object
 *  @return TRUE if memory object is a GstVvasMemory type, else return FALSE
 *  @brief Validates whether a GstMemory object is GstVvasMemory type or not
 */
GST_EXPORT
gboolean gst_is_vvas_memory (GstMemory *mem);

/**
 *  @fn guint64 gst_vvas_allocator_get_paddr (GstMemory * mem)
 *  @param [in] mem Pointer to GstMemory object
 *  @return Physical address corresponding to data on success.\n 0 on failure.
 *  @brief Gets physical address corresponding to GstVvasMemory \p mem
 */
GST_EXPORT
guint64  gst_vvas_allocator_get_paddr (GstMemory *mem);

/**
 *  @fn void* gst_vvas_allocator_get_bo (GstMemory * mem)
 *  @param [in] mem Pointer to GstMemory object
 *  @return Pointer to XRT BO handle on success\n NULL on failure
 *  @brief Gets XRT BO corresponding to GstVvasMemory \p mem
 */
GST_EXPORT
void*  gst_vvas_allocator_get_bo (GstMemory *mem);

/**
 *  @fn gboolean gst_vvas_memory_can_avoid_copy (GstMemory * mem,
 *                                               guint cur_devid,
 *                                               guint req_mem_bank)
 *  @param [in] mem Pointer to GstMemory object
 *  @param [in] cur_devid Index device which is going to be compared with device index on which memory allocated
 *  @param [in] req_mem_bank Memory bank index which will be compared against memory's bank index
 *  @return TRUE if zero copy possible\n FALSE if copy is needed
 *  @brief This API returns TRUE if requested device index and memory bank index are same as @mem device index
 *         & memory bank index, else returns false
 */
GST_EXPORT
gboolean gst_vvas_memory_can_avoid_copy (GstMemory *mem, guint cur_devid, guint req_mem_bank);

/**
 *  @fn guint gst_vvas_allocator_get_device_idx (GstAllocator * allocator)
 *  @param [in] Pointer to GstVvasAllocator object
 *  @return Device index of the \p allocator object on success. (guint)-1 on failure
 *  @brief This API returns device index which is set while creating GstVvasAllocator object using
 *         gst_vvas_allocator_new
 */
GST_EXPORT
guint gst_vvas_allocator_get_device_idx (GstAllocator * allocator);

typedef enum {
  /** No sync */
  VVAS_SYNC_NONE = 0,
  /** sync data to device using DMA transfer */
  VVAS_SYNC_TO_DEVICE = 1 << 0,
  /** sync data to device using DMA transfer */
  VVAS_SYNC_FROM_DEVICE = 1 << 1,
} VvasSyncFlags;

/**
 *  @fn void gst_vvas_memory_set_sync_flag (GstMemory * mem, VvasSyncFlags flag)
 *  @param [in]  mem Pointer to GstMemory object
 *  @param [in] flag Flags to be set on \p mem object
 *  @return None
 *  @brief API to set data synchronization flags on GstVvasMemory object
 */
GST_EXPORT
void gst_vvas_memory_set_sync_flag (GstMemory *mem, VvasSyncFlags flag);

/**
 *  @fn gboolean gst_vvas_memory_sync_bo (GstMemory * mem)
 *  @param [in] mem Pointer to GstMemory object
 *  @return TRUE on success\n FALSE on failure
 *  @brief Synchronize data from host to device based on GstVvasMemory::sync_flags. Data from device to host
 *         automatically happens when memory is mapped in READ mode. However, when data need to be synchronized from
 *         host to device this API need to called.
 */
GST_EXPORT
gboolean gst_vvas_memory_sync_bo (GstMemory *mem);

/**
 *  @fn gboolean gst_vvas_memory_sync_with_flags (GstMemory * mem, GstMapFlags flags)
 *  @param [in] mem Pointer to GstMemory object
 *  @param [in] flags GStreamer mapping flags which decides whether data need to synchronized between host and device
 *  @return TRUE on success\n FALSE on failure
 *  @brief Synchronize the data vai DMA transfer between host memory and device memory
 *         corresponding to a XRT BO based on GstMapFlags
 */
GST_EXPORT
gboolean gst_vvas_memory_sync_with_flags (GstMemory *mem, GstMapFlags flags);

/**
 *  @fn void gst_vvas_memory_set_flag (GstMemory *mem, VvasSyncFlags flag)
 *  @param [in] mem Pointer to GstMemory object
 *  @param [in] flag Flag to be enabled on @mem
 *  @return None
 *  @brief API to enable VvasSyncFlags on a GstVvasMemory object
 */
GST_EXPORT
void gst_vvas_memory_set_flag (GstMemory *mem, VvasSyncFlags flag);

/**
 *  @fn void gst_vvas_memory_unset_flag (GstMemory *mem, VvasSyncFlags flag)
 *  @param [in] mem Pointer to GstMemory object
 *  @param [in] flag Flag to be disabled on @mem
 *  @return None
 *  @brief API to disable VvasSyncFlags on a GstVvasMemory object
 */
GST_EXPORT
void gst_vvas_memory_unset_flag (GstMemory *mem, VvasSyncFlags flag);

/**
 *  @fn void gst_vvas_memory_get_sync_flag (GstMemory *mem)
 *  @param [in] mem Pointer to GstMemory object
 *  @return VvasSyncFlags
 *  @brief API to get VvasSyncFlags on a GstVvasMemory object
 */
GST_EXPORT
VvasSyncFlags gst_vvas_memory_get_sync_flag (GstMemory * mem);

/**
 *  @fn void gst_vvas_memory_reset_flag (GstMemory * mem)
 *  @param [in] mem - Pointer to GstMemory object
 *  @return None
 *  @brief API to reset all VvasSyncFlags on a GstVvasMemory object
 */
GST_EXPORT
void gst_vvas_memory_reset_sync_flag (GstMemory * mem);

G_END_DECLS

#endif /* __GST_VVAS_ALLOCATOR_H__ */
