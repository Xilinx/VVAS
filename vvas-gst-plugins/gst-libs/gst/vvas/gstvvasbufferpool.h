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

#ifndef __GST_VVAS_BUFFER_POOL_H__
#define __GST_VVAS_BUFFER_POOL_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstVvasBufferPool GstVvasBufferPool;
typedef struct _GstVvasBufferPoolClass GstVvasBufferPoolClass;
typedef struct _GstVvasBufferPoolPrivate GstVvasBufferPoolPrivate;

#define GST_TYPE_VVAS_BUFFER_POOL \
  (gst_vvas_buffer_pool_get_type())
#define GST_IS_VVAS_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VVAS_BUFFER_POOL))
#define GST_VVAS_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VVAS_BUFFER_POOL, GstVvasBufferPool))
#define GST_VVAS_BUFFER_POOL_CAST(obj) \
  ((GstVvasBufferPool*)(obj))

typedef void (*PreReleaseBufferCallback)(GstBuffer *buf, gpointer user_data);
typedef void (*PostReleaseBufferCallback)(GstBuffer *buf, gpointer user_data);

struct _GstVvasBufferPool
{
  GstVideoBufferPool parent;
  GstVvasBufferPoolPrivate *priv;
  PreReleaseBufferCallback pre_release_cb;
  PostReleaseBufferCallback post_release_cb;
  gpointer pre_cb_user_data; /* pre release callback user data */
  gpointer post_cb_user_data; /* pre release callback user data */
};

struct _GstVvasBufferPoolClass
{
  GstVideoBufferPoolClass parent_class;
};

GST_EXPORT
GType gst_vvas_buffer_pool_get_type (void) G_GNUC_CONST;

GST_EXPORT
GstBufferPool *gst_vvas_buffer_pool_new (guint stride_align, guint elevation_align);

GST_EXPORT
void gst_vvas_buffer_pool_set_pre_release_buffer_cb (GstVvasBufferPool *xpool, PreReleaseBufferCallback pre_release_cb, gpointer user_data);

GST_EXPORT
void gst_vvas_buffer_pool_set_post_release_buffer_cb (GstVvasBufferPool *xpool, PostReleaseBufferCallback post_release_cb, gpointer user_data);

G_END_DECLS

#endif /* __GST_VVAS_BUFFER_POOL_H__ */
