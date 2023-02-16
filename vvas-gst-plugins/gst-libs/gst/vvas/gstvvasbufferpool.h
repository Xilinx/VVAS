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

#ifndef __GST_VVAS_BUFFER_POOL_H__
#define __GST_VVAS_BUFFER_POOL_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstVvasBufferPool GstVvasBufferPool;
typedef struct _GstVvasBufferPoolClass GstVvasBufferPoolClass;
typedef struct _GstVvasBufferPoolPrivate GstVvasBufferPoolPrivate;

/** @def GST_TYPE_VVAS_BUFFER_POOL
 *  @brief Macro to get GstVvasBufferPool object type
 */
#define GST_TYPE_VVAS_BUFFER_POOL \
  (gst_vvas_buffer_pool_get_type())

/** @def GST_IS_VVAS_BUFFER_POOL
 *  @brief Macro to validate whether object is of GstVvasBufferPool type
 */
#define GST_IS_VVAS_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VVAS_BUFFER_POOL))

/** @def GST_VVAS_BUFFER_POOL
 *  @brief Macro to typecast parent object to GstVvasBufferPool object
 */
#define GST_VVAS_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VVAS_BUFFER_POOL, GstVvasBufferPool))

/** @def GST_VVAS_BUFFER_POOL_CAST
 *  @brief Macro to typecast object to GstVvasBufferPool object
 */
#define GST_VVAS_BUFFER_POOL_CAST(obj) \
  ((GstVvasBufferPool*)(obj))

/**
 *  @typedef  typedef void (*PreReleaseBufferCallback)(GstBuffer *buf, gpointer user_data)
 *  @param [in] buf Buffer which is going to be freed
 *  @param [in] user_data User data pointer while setting this callback function
 *  @return None
 *  @brief Sets callback function to be called before releasing buffer
 */
typedef void (*PreReleaseBufferCallback)(GstBuffer *buf, gpointer user_data);

/**
 *  @typedef  typedef void (*PostReleaseBufferCallback)(GstBuffer *buf, gpointer user_data)
 *  @param [in] buf Buffer which is going to be freed
 *  @param [in] user_data User data pointer while setting this callback function
 *  @return None
 *  @brief Sets callback function to be called after releasing buffer
 */
typedef void (*PostReleaseBufferCallback)(GstBuffer *buf, gpointer user_data);

struct _GstVvasBufferPool
{
  /** parent of GstVvasBufferPool object */
  GstVideoBufferPool parent;
  /** Pointer instance's private structure for each of access */
  GstVvasBufferPoolPrivate *priv;
  /** pre release callback function pointer */
  PreReleaseBufferCallback pre_release_cb;
  /** post release callback function pointer */
  PostReleaseBufferCallback post_release_cb;
  /** pre release callback user data */
  gpointer pre_cb_user_data;
  /** post release callback user data */
  gpointer post_cb_user_data;
};

struct _GstVvasBufferPoolClass
{
  /** parent class */
  GstVideoBufferPoolClass parent_class;
};

GST_EXPORT
GType gst_vvas_buffer_pool_get_type (void) G_GNUC_CONST;

/**
 *  @fn GstBufferPool *gst_vvas_buffer_pool_new (guint stride_align, guint elevation_align)
 *  @param [in] stride_align Stride alignment value to be used while allocating video frames
 *  @param [in] elevation_align Height alignment value to be used while allocating video frames
 *  @return GstBufferPool pointer on success\n  NULL on failure
 *  @brief  Allocates GstVvasBufferPool object using parameters passed to this API.
 */
GST_EXPORT
GstBufferPool *gst_vvas_buffer_pool_new (guint stride_align, guint elevation_align);

/**
 *  @fn void gst_vvas_buffer_pool_set_pre_release_buffer_cb (GstVvasBufferPool * xpool,
 *                                                           PreReleaseBufferCallback release_buf_cb,
 *                                                           gpointer user_data)
 *  @param [in] xpool VVAS buffer pool instance handle
 *  @param [in] release_buf_cb Pre-release buffer back function
 *  @param [in] user_data User data to be passed while calling @release_buf_cb
 *  @return None
 *  @brief Sets callback function to be called before releasing buffer
 */
GST_EXPORT
void gst_vvas_buffer_pool_set_pre_release_buffer_cb (GstVvasBufferPool *xpool, PreReleaseBufferCallback pre_release_cb, gpointer user_data);

/**
 *  @fn void gst_vvas_buffer_pool_set_post_release_buffer_cb (GstVvasBufferPool * xpool,
 *                                                            PostReleaseBufferCallback release_buf_cb,
 *                                                            gpointer user_data)
 *  @param [in] xpool VVAS buffer pool instance handle
 *  @param [in] release_buf_cb Post-release buffer back function
 *  @param [in] user_data User data to be passed while calling @release_buf_cb
 *  @return None
 *  @brief Sets callback function to be called after releasing buffer
 */
GST_EXPORT
void gst_vvas_buffer_pool_set_post_release_buffer_cb (GstVvasBufferPool *xpool, PostReleaseBufferCallback post_release_cb, gpointer user_data);

G_END_DECLS

#endif /* __GST_VVAS_BUFFER_POOL_H__ */
