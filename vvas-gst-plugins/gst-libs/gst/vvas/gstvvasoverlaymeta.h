/*
 * Copyright 2022 Xilinx, Inc.
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

#ifndef __GST_VVAS_OVERLAY_META_H__
#define __GST_VVAS_OVERLAY_META_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>
#include <gst/vvas/gstvvascommon.h>
#include <gst/vvas/gstinferenceprediction.h>

G_BEGIN_DECLS

#define GST_VVAS_OVERLAY_META_API_TYPE  (gst_vvas_overlay_meta_api_get_type())
#define GST_VVAS_OVERLAY_META_INFO  (gst_vvas_overlay_meta_get_info())

typedef struct _GstVvasOverlayMeta GstVvasOverlayMeta;
typedef struct _vvas_rect_params vvas_rect_params;
typedef struct _vvas_text_params vvas_text_params;
typedef struct _vvas_line_params vvas_line_params;
typedef struct _vvas_arrow_params vvas_arrow_params;
typedef struct _vvas_circle_params vvas_circle_params;
typedef struct _vvas_polygon_params vvas_polygon_params;
typedef struct _vvas_pt vvas_pt;
typedef struct _vvas_font_params vvas_font_params;

#define VVAS_MAX_OVERLAY_DATA 16
#define VVAS_MAX_OVERLAY_POLYGON_POINTS 16
#define VVAS_MAX_FONT_TEXT_SIZE 128
#define VVAS_MAX_TEXT_SIZE 256

typedef enum
{
  AT_START,
  AT_END,
  BOTH_ENDS,
} vvas_arrow_direction;

struct _vvas_pt
{
  int x;
  int y;
};

struct _vvas_font_params
{
  int font_num;
  float font_size;
  VvasColorMetadata font_color;
};

struct _vvas_rect_params
{
  vvas_pt offset;
  int width;
  int height;
  int thickness;
  VvasColorMetadata rect_color;
  int apply_bg_color;
  VvasColorMetadata bg_color;
};

struct _vvas_text_params
{
  vvas_pt offset;
  char disp_text[VVAS_MAX_TEXT_SIZE];
  int bottom_left_origin;
  vvas_font_params text_font;
  int apply_bg_color;
  VvasColorMetadata bg_color;
};

struct _vvas_line_params
{
  vvas_pt start_pt;
  vvas_pt end_pt;
  int thickness;
  VvasColorMetadata line_color;
};

struct _vvas_arrow_params
{
  vvas_pt start_pt;
  vvas_pt end_pt;
  vvas_arrow_direction arrow_direction;
  int thickness;
  float tipLength;
  VvasColorMetadata line_color;
};

struct _vvas_circle_params
{
  vvas_pt center_pt;
  int radius;
  int thickness;
  VvasColorMetadata circle_color;
};

struct _vvas_polygon_params
{
  vvas_pt poly_pts[VVAS_MAX_OVERLAY_POLYGON_POINTS];
  int num_pts;
  int thickness;
  VvasColorMetadata poly_color;
};

struct _GstVvasOverlayMeta {
  GstMeta meta;
  int num_rects;
  int num_text;
  int num_lines;
  int num_arrows;
  int num_circles;
  int num_polys;
  
  vvas_rect_params rects[VVAS_MAX_OVERLAY_DATA];
  vvas_text_params text[VVAS_MAX_OVERLAY_DATA];
  vvas_line_params lines[VVAS_MAX_OVERLAY_DATA];
  vvas_arrow_params arrows[VVAS_MAX_OVERLAY_DATA];
  vvas_circle_params circles[VVAS_MAX_OVERLAY_DATA];
  vvas_polygon_params polygons[VVAS_MAX_OVERLAY_DATA];
};

GST_EXPORT
GType gst_vvas_overlay_meta_api_get_type (void);

GST_EXPORT
const GstMetaInfo * gst_vvas_overlay_meta_get_info (void);

#define gst_buffer_get_vvas_overlay_meta(b) ((GstVvasOverlayMeta*)gst_buffer_get_meta((b), GST_VVAS_OVERLAY_META_API_TYPE))
#define gst_buffer_add_vvas_overlay_meta(b) ((GstVvasOverlayMeta*)gst_buffer_add_meta((b), GST_VVAS_OVERLAY_META_INFO, NULL))

G_END_DECLS

#endif /* __GST_VVAS_OVERLAY_META_H__ */
