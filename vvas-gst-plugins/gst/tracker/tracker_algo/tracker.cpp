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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tracker_int.hpp"
#include "correlation_filter.hpp"
#include "Hungarian.h"

void est_histogram_rgb(Mat_img frame, Rectf roi, float *_hist, int size) {
  int i, j;
  unsigned char *img_ptr, *img_ptr_uv;
  int y, u = 0, v = 0;
  int r, g, b;
  int diff_width;
  float *hist, *hist_g, *hist_b;
  float inv_roi_size;

  hist = _hist;
  hist_g = hist + 64;
  hist_b = hist_g + 64;

  memset(hist, 0, sizeof(float)*size);

  if (roi.x < 0)
	roi.x = 0;
  if (roi.x + roi.width >= frame.width)
	roi.width = frame.width - roi.x - 1;

  if (roi.y < 0)
	roi.y = 0;
  if (roi.y + roi.height >= frame.height)
	roi.height = frame.height - roi.y - 1;

  inv_roi_size = 1.0 / (roi.width * roi.height);

  img_ptr = frame.data + ((int)(frame.width * roi.y + roi.x));
  diff_width = frame.width - roi.width;

  if ((int)roi.x % 2)
	img_ptr_uv = frame.data + ((int)(frame.width * frame.height + frame.width * (((int)roi.y) / 2) + roi.x));
  else
	img_ptr_uv = frame.data + ((int)(frame.width * frame.height + frame.width * (((int)roi.y) / 2) + roi.x - 1));

  for (i = 0; i < roi.height; i++) {
	for (j = 0; j < roi.width; j++) {
	  y = *img_ptr++;
	  if (j % 2 == 0) {
		u = img_ptr_uv[j];
		v = img_ptr_uv[j + 1];

		u -= 128;
		v -= 128;
	  }

	  y -= 16;
	  r = 1.164 * y + 1.596 *v;
	  g = 1.164 * y - 0.392 * u - 0.813 * v;
	  b = 1.164 * y + 2.017 * u;

	  r = r < 0 ? 0 : r > 255 ? 255 : r;
	  g = g < 0 ? 0 : g > 255 ? 255 : g;
	  b = b < 0 ? 0 : b > 255 ? 255 : b;
	  hist[r >> HIST_BIN]++;
	  hist_g[g >> HIST_BIN]++;
	  hist_b[b >> HIST_BIN]++;
	}

	img_ptr += diff_width;
	if (i % 2)
	  img_ptr_uv += frame.width;
  }

  for (i = 0; i < 64; i++) {
	hist[i] *= inv_roi_size;
	hist_g[i] *= inv_roi_size;
	hist_b[i] *= inv_roi_size;
  }
}

void est_histogram_hsv(Mat_img frame, Rectf roi, float *_hist, int size) {
  int i, j;
  unsigned char *img_ptr, *img_ptr_uv;
  int y, u = 0, v = 0;
  int r, g, b;
  int diff_width;
  float *hist, *hist_g, *hist_b;
  float inv_roi_size;
  int rgb_min, rgb_max;
  int h, s, l;

  hist = _hist;
  hist_g = hist + 64;
  hist_b = hist_g + 64;
  
  memset(hist, 0, sizeof(float)*size);

  if (roi.x < 0)
	roi.x = 0;
  if (roi.x + roi.width >= frame.width)
	roi.width = frame.width - roi.x - 1;

  if (roi.y < 0)
	roi.y = 0;
  if (roi.y + roi.height >= frame.height)
	roi.height = frame.height - roi.y - 1;

  inv_roi_size = 1.0 / (roi.width * roi.height);

  img_ptr = frame.data + ((int)(frame.width * roi.y + roi.x));
  diff_width = frame.width - roi.width;

  if ((int)roi.x % 2)
	img_ptr_uv = frame.data + ((int)(frame.width * frame.height + frame.width * (((int)roi.y) / 2) + roi.x));
  else
	img_ptr_uv = frame.data + ((int)(frame.width * frame.height + frame.width * (((int)roi.y) / 2) + roi.x - 1));

  for (i = 0; i < roi.height; i++) {
	for (j = 0; j < roi.width; j++) {
	  y = *img_ptr++;
	  if (j % 2 == 0) {
		u = img_ptr_uv[j];
		v = img_ptr_uv[j + 1];

		u -= 128;
		v -= 128;
	  }

	  y -= 16;
	  r = 1.164 * y + 1.596 *v;
	  g = 1.164 * y - 0.392 * u - 0.813 * v;
	  b = 1.164 * y + 2.017 * u;

	  r = r < 0 ? 0 : r > 255 ? 255 : r;
	  g = g < 0 ? 0 : g > 255 ? 255 : g;
	  b = b < 0 ? 0 : b > 255 ? 255 : b;

	  rgb_min = std::min(r, std::min(g, b));
	  rgb_max = std::max(r, std::max(g, b));
	  l = rgb_max;
	  if (l == 0) {
		h = 0;
		s = 0;
	  }
	  else {
		s = 255 * ((long)(rgb_max - rgb_min)) / l;
		if (s == 0) {
		  h = 0;
		}
		else {
		  if (rgb_max == r)
			h = 0 + (43 * (g - b)) / (rgb_max - rgb_min);
		  else if (rgb_max == g)
			h = 85 + (43 * (b - r)) / (rgb_max - rgb_min);
		  else
			h = 171 + (43 * (r - g)) / (rgb_max - rgb_min);
		}
	  }

	  if (h < 0)
		h += 256;

	  hist[h >> HIST_BIN]++;
	  hist_g[s >> HIST_BIN]++;
	  hist_b[l >> HIST_BIN]++;
	}

	img_ptr += diff_width;
	if (i % 2)
	  img_ptr_uv += frame.width;
  }

  for (i = 0; i < 64; i++) {
	hist[i] *= inv_roi_size;
	hist_g[i] *= inv_roi_size;
	hist_b[i] *= inv_roi_size;
  }
}

float hist_dist_correlation(float *hist1, float *hist2, int size) {
  float s1 = 0, s2 = 0, s12 = 0, s11 = 0, s22 = 0;
  float num, denum;
  float norm = 1 / size;
  float corr;
  for (int i = 0; i < size; i++) {
	s1 += hist1[i];
	s2 += hist2[i];
	s11 += hist1[i] * hist1[i];
	s22 += hist2[i] * hist2[i];
	s12 += hist1[i] * hist2[i];
  }
  num = s12 - s1 * s2 * norm;
  denum = (s11 - s1 * s1 * norm) * (s22 - s2 * s2 * norm);
  if (denum)
	corr = num / sqrt(denum);
  else
	corr = 1.0;
  return corr;
}

float region_overlap(Rectf b1, Rectf b2) {
  float iou = 0;

  float b1x2 = b1.x + b1.width;
  float b1y2 = b1.y + b1.height;
  float b2x2 = b2.x + b2.width;
  float b2y2 = b2.y + b2.height;
  float b1_area = b1.width * b1.height;
  float b2_area = b2.width * b2.height;

  float x_max = max(b1.x, b2.x);
  float y_max = max(b1.y, b2.y);
  float x_min = min(b1x2, b2x2);
  float y_min = min(b1y2, b2y2);

  float tot_width = max(x_min - x_max, 0.0f);
  float tot_height = max(y_min - y_max, 0.0f);

  float num = tot_width * tot_height;
  float den = b1_area + b2_area - num;

  iou = num / den;
  return iou;
}

float percentage_occlusion(Rectf b1, Rectf b2) {
  float iou = 0;
  float b1x2 = b1.x + b1.width;
  float b1y2 = b1.y + b1.height;
  float b2x2 = b2.x + b2.width;
  float b2y2 = b2.y + b2.height;
  float b1_area = b1.width * b1.height;
  float x_max = max(b1.x, b2.x);
  float y_max = max(b1.y, b2.y);
  float x_min = min(b1x2, b2x2);
  float y_min = min(b1y2, b2y2);
  float tot_width = max(x_min - x_max, 0.0f);
  float tot_height = max(y_min - y_max, 0.0f);
  float num = tot_width * tot_height;
  iou = num / b1_area;
  return iou;
}

void estimate_bbox_info(MAE_info maInfo, Rectf prev_bbox, short duration, Rectf *bbox_new) {
  bbox_info bbox_avg;
  int cnt, st_pt, ed_pt;
  float cx, cy;

  if (maInfo.complete) {
	if (maInfo.idx - 1 < 0) {
	  st_pt = 0;
	  ed_pt = MAE_FRM - 1;
	}
	else {
	  st_pt = maInfo.idx;
	  ed_pt = maInfo.idx - 1;
	}
	cnt = MAE_FRM;
  }
  else {
	st_pt = 0;
	ed_pt = maInfo.idx - 1;
	cnt = maInfo.idx;
  }
  if(cnt) {
	//direction of motion
	float x_dir = maInfo.bbox_data[ed_pt].cx - maInfo.bbox_data[st_pt].cx;
	float y_dir = maInfo.bbox_data[ed_pt].cy - maInfo.bbox_data[st_pt].cy;

	//check object is really moved
	if ((fabs(x_dir) / prev_bbox.width > 0.1) || (fabs(y_dir) / prev_bbox.height > 0.1)) {
	  bbox_avg.cx = x_dir / cnt;
	  bbox_avg.cy = y_dir / cnt;
	  bbox_avg.w = maInfo.bbox_sum.w / cnt;
	  bbox_avg.h = maInfo.bbox_sum.h / cnt;
	  bbox_avg.dcx = maInfo.bbox_sum.dcx / cnt;
	  bbox_avg.dcy = maInfo.bbox_sum.dcy / cnt;
	  bbox_avg.dw = maInfo.bbox_sum.dw / cnt;
	  bbox_avg.dh = maInfo.bbox_sum.dh / cnt;
	  bbox_avg.d2cx = maInfo.bbox_sum.d2cx / cnt;
	  bbox_avg.d2cy = maInfo.bbox_sum.d2cy / cnt;
	  bbox_avg.d2w = maInfo.bbox_sum.d2w / cnt;
	  bbox_avg.d2h = maInfo.bbox_sum.d2h / cnt;
	  bbox_avg.vel = maInfo.bbox_sum.vel / cnt;

	  cx = prev_bbox.x;
	  cy = prev_bbox.y;
	  if (bbox_avg.dw < 0)
		bbox_new->width = prev_bbox.width - fabs(bbox_avg.d2w) * duration;
	  else
		bbox_new->width = prev_bbox.width + fabs(bbox_avg.d2w) * duration;

	  if (bbox_avg.dh < 0)
		bbox_new->height = prev_bbox.height - fabs(bbox_avg.d2h) * duration;
	  else
		bbox_new->height = prev_bbox.height + fabs(bbox_avg.d2h) * duration;

	  if (bbox_avg.cx < 0)
		bbox_new->x = (cx + (bbox_avg.cx * duration) - (fabs(bbox_avg.d2cx) * duration ));
	  else
		bbox_new->x = (cx + (bbox_avg.cx * duration) + (fabs(bbox_avg.d2cx) * duration ));

	  if (bbox_avg.cy < 0)
		bbox_new->y = (cy + (bbox_avg.cy * duration) - (fabs(bbox_avg.d2cy) * duration ));
	  else
		bbox_new->y = (cy + (bbox_avg.cy * duration) + (fabs(bbox_avg.d2cy) * duration ));
	}
	else {
	  bbox_new->width = prev_bbox.width;
	  bbox_new->height = prev_bbox.height;
	  bbox_new->x = prev_bbox.x;
	  bbox_new->y = prev_bbox.y;
	}
  }
  else {
	bbox_new->width = prev_bbox.width;
	bbox_new->height = prev_bbox.height;
	bbox_new->x = prev_bbox.x;
	bbox_new->y = prev_bbox.y;
  }
}

void ema_update_bbox(Rectf prev_bbox, Rectf *cur_bbox) {
  cur_bbox->x = prev_bbox.x * (1.0 - LRN_POS) + LRN_POS * cur_bbox->x;
  cur_bbox->y = prev_bbox.y * (1.0 - LRN_POS) + LRN_POS * cur_bbox->y;
  cur_bbox->width = prev_bbox.width * (1.0 - LRN_SCALE) + LRN_SCALE * cur_bbox->width;
  cur_bbox->height = prev_bbox.height * (1.0 - LRN_SCALE) + LRN_SCALE * cur_bbox->height;
}

void update_movingAverageInfo(MAE_info *maInfo, Rectf prev_bbox, Rectf cur_bbox) {
  float prev_cx, prev_cy;
  float cur_cx, cur_cy;
  int idx = maInfo->idx;
  prev_cx = prev_bbox.x;
  prev_cy = prev_bbox.y;

  cur_cx = cur_bbox.x;
  cur_cy = cur_bbox.y;

  int prev_idx;
  float cur_val;
  if (maInfo->complete) {
	prev_idx = idx - 1;
	if (prev_idx < 0)
	  prev_idx = MAE_FRM - 1;
	maInfo->bbox_sum.cx -= maInfo->bbox_data[idx].cx;
	maInfo->bbox_data[idx].cx = cur_cx;
	maInfo->bbox_sum.cx += maInfo->bbox_data[idx].cx;
	maInfo->bbox_sum.cy -= maInfo->bbox_data[idx].cy;
	maInfo->bbox_data[idx].cy = cur_cy;
	maInfo->bbox_sum.cy += maInfo->bbox_data[idx].cy;
	maInfo->bbox_sum.w -= maInfo->bbox_data[idx].w;
	maInfo->bbox_data[idx].w = cur_bbox.width;
	maInfo->bbox_sum.w += maInfo->bbox_data[idx].w;
	maInfo->bbox_sum.h -= maInfo->bbox_data[idx].h;
	maInfo->bbox_data[idx].h = cur_bbox.height;
	maInfo->bbox_sum.h += maInfo->bbox_data[idx].h;

	cur_val = cur_cx - prev_cx;
	maInfo->bbox_sum.dcx -= maInfo->bbox_data[idx].dcx;
	maInfo->bbox_data[idx].dcx = maInfo->bbox_data[prev_idx].dcx * (1.0 - LRN_DIFF_POS) + LRN_DIFF_POS * cur_val;
	maInfo->bbox_sum.dcx += maInfo->bbox_data[idx].dcx;
	cur_val = cur_cy - prev_cy;
	maInfo->bbox_sum.dcy -= maInfo->bbox_data[idx].dcy;
	maInfo->bbox_data[idx].dcy = maInfo->bbox_data[prev_idx].dcy * (1.0 - LRN_DIFF_POS) + LRN_DIFF_POS * cur_val;
	maInfo->bbox_sum.dcy += maInfo->bbox_data[idx].dcy;
	cur_val = cur_bbox.width - prev_bbox.width;
	maInfo->bbox_sum.dw -= maInfo->bbox_data[idx].dw;
	maInfo->bbox_data[idx].dw = maInfo->bbox_data[prev_idx].dw * (1.0 - LRN_DIFF_SCALE) + LRN_DIFF_SCALE * cur_val;
	maInfo->bbox_sum.dw += maInfo->bbox_data[idx].dw;
	cur_val = cur_bbox.height - prev_bbox.height;
	maInfo->bbox_sum.dh -= maInfo->bbox_data[idx].dh;
	maInfo->bbox_data[idx].dh = maInfo->bbox_data[prev_idx].dh * (1.0 - LRN_DIFF_SCALE) + LRN_DIFF_SCALE * cur_val;
	maInfo->bbox_sum.dh += maInfo->bbox_data[idx].dh;

	cur_val = maInfo->bbox_data[idx].dcx - maInfo->bbox_data[prev_idx].dcx;
	maInfo->bbox_sum.d2cx -= maInfo->bbox_data[idx].d2cx;
	maInfo->bbox_data[idx].d2cx = maInfo->bbox_data[prev_idx].d2cx * (1.0 - LRN_DIFF_POS) + LRN_DIFF_POS * cur_val;
	maInfo->bbox_sum.d2cx += maInfo->bbox_data[idx].d2cx;
	cur_val = maInfo->bbox_data[idx].dcy - maInfo->bbox_data[prev_idx].dcy;
	maInfo->bbox_sum.d2cy -= maInfo->bbox_data[idx].d2cy;
	maInfo->bbox_data[idx].d2cy = maInfo->bbox_data[prev_idx].d2cy * (1.0 - LRN_DIFF_POS) + LRN_DIFF_POS * cur_val;
	maInfo->bbox_sum.d2cy += maInfo->bbox_data[idx].d2cy;
	cur_val = maInfo->bbox_data[idx].dw - maInfo->bbox_data[prev_idx].dw;
	maInfo->bbox_sum.d2w -= maInfo->bbox_data[idx].d2w;
	maInfo->bbox_data[idx].d2w = maInfo->bbox_data[prev_idx].d2w * (1.0 - LRN_DIFF_SCALE) + LRN_DIFF_SCALE * cur_val;
	maInfo->bbox_sum.d2w += maInfo->bbox_data[idx].d2w;
	cur_val = maInfo->bbox_data[idx].dh - maInfo->bbox_data[prev_idx].dh;
	maInfo->bbox_sum.d2h -= maInfo->bbox_data[idx].d2h;
	maInfo->bbox_data[idx].d2h = maInfo->bbox_data[prev_idx].d2h * (1.0 - LRN_DIFF_SCALE) + LRN_DIFF_SCALE * cur_val;
	maInfo->bbox_sum.d2h += maInfo->bbox_data[idx].d2h;
	cur_val = sqrt(maInfo->bbox_data[idx].dcx * maInfo->bbox_data[idx].dcx + maInfo->bbox_data[idx].dcy * maInfo->bbox_data[idx].dcy);
	maInfo->bbox_data[idx].vel = maInfo->bbox_data[prev_idx].vel * (1.0 - LRN_VEL) + LRN_VEL * cur_val;
  }
  else if (idx) {
	prev_idx = idx - 1;
	
	maInfo->bbox_data[idx].cx = cur_cx;
	maInfo->bbox_sum.cx += maInfo->bbox_data[idx].cx;
	maInfo->bbox_data[idx].cy = cur_cy;
	maInfo->bbox_sum.cy += maInfo->bbox_data[idx].cy;
	maInfo->bbox_data[idx].w = cur_bbox.width;
	maInfo->bbox_sum.w += maInfo->bbox_data[idx].w;
	maInfo->bbox_data[idx].h = cur_bbox.height;
	maInfo->bbox_sum.h += maInfo->bbox_data[idx].h;

	cur_val = cur_cx - prev_cx;
	maInfo->bbox_data[idx].dcx = maInfo->bbox_data[prev_idx].dcx * (1.0 - LRN_DIFF_POS) + LRN_DIFF_POS * cur_val;
	maInfo->bbox_sum.dcx += maInfo->bbox_data[idx].dcx;
	cur_val = cur_cy - prev_cy;
	maInfo->bbox_data[idx].dcy = maInfo->bbox_data[prev_idx].dcy * (1.0 - LRN_DIFF_POS) + LRN_DIFF_POS * cur_val;
	maInfo->bbox_sum.dcy += maInfo->bbox_data[idx].dcy;
	cur_val = cur_bbox.width - prev_bbox.width;
	maInfo->bbox_data[idx].dw = maInfo->bbox_data[prev_idx].dw * (1.0 - LRN_DIFF_SCALE) + LRN_DIFF_SCALE * cur_val;
	maInfo->bbox_sum.dw += maInfo->bbox_data[idx].dw;
	cur_val = cur_bbox.height - prev_bbox.height;
	maInfo->bbox_data[idx].dh = maInfo->bbox_data[prev_idx].dh * (1.0 - LRN_DIFF_SCALE) + LRN_DIFF_SCALE * cur_val;
	maInfo->bbox_sum.dh += maInfo->bbox_data[idx].dh;

	cur_val = maInfo->bbox_data[idx].dcx - maInfo->bbox_data[prev_idx].dcx;
	maInfo->bbox_data[idx].d2cx = maInfo->bbox_data[prev_idx].d2cx * (1.0 - LRN_DIFF_POS) + LRN_DIFF_POS * cur_val;
	maInfo->bbox_sum.d2cx += maInfo->bbox_data[idx].d2cx;
	cur_val = maInfo->bbox_data[idx].dcy - maInfo->bbox_data[prev_idx].dcy;
	maInfo->bbox_data[idx].d2cy = maInfo->bbox_data[prev_idx].d2cy * (1.0 - LRN_DIFF_POS) + LRN_DIFF_POS * cur_val;
	maInfo->bbox_sum.d2cy += maInfo->bbox_data[idx].d2cy;
	cur_val = maInfo->bbox_data[idx].dw - maInfo->bbox_data[prev_idx].dw;
	maInfo->bbox_data[idx].d2w = maInfo->bbox_data[prev_idx].d2w * (1.0 - LRN_DIFF_SCALE) + LRN_DIFF_SCALE * cur_val;
	maInfo->bbox_sum.d2w += maInfo->bbox_data[idx].d2w;
	cur_val = maInfo->bbox_data[idx].dh - maInfo->bbox_data[prev_idx].dh;
	maInfo->bbox_data[idx].d2h = maInfo->bbox_data[prev_idx].d2h * (1.0 - LRN_DIFF_SCALE) + LRN_DIFF_SCALE * cur_val;
	maInfo->bbox_sum.d2h += maInfo->bbox_data[idx].d2h;
	cur_val = sqrt(maInfo->bbox_data[idx].dcx * maInfo->bbox_data[idx].dcx + maInfo->bbox_data[idx].dcy * maInfo->bbox_data[idx].dcy);
	maInfo->bbox_data[idx].vel = maInfo->bbox_data[prev_idx].vel * (1.0 - LRN_VEL) + LRN_VEL * cur_val;
  }
  else {
	maInfo->bbox_data[idx].cx = cur_cx;
	maInfo->bbox_sum.cx = maInfo->bbox_data[idx].cx;
	maInfo->bbox_data[idx].cy = cur_cy;
	maInfo->bbox_sum.cy = maInfo->bbox_data[idx].cy;
	maInfo->bbox_data[idx].w = cur_bbox.width;
	maInfo->bbox_sum.w = maInfo->bbox_data[idx].w;
	maInfo->bbox_data[idx].h = cur_bbox.height;
	maInfo->bbox_sum.h = maInfo->bbox_data[idx].h;

	maInfo->bbox_data[idx].dcx = cur_cx - prev_cx;
	maInfo->bbox_sum.dcx = maInfo->bbox_data[idx].dcx;
	maInfo->bbox_data[idx].dcy = cur_cy - prev_cy;
	maInfo->bbox_sum.dcy = maInfo->bbox_data[idx].dcy;
	maInfo->bbox_data[idx].dw = cur_bbox.width - prev_bbox.width;
	maInfo->bbox_sum.dw = maInfo->bbox_data[idx].dw;
	maInfo->bbox_data[idx].dh = cur_bbox.height - prev_bbox.height;
	maInfo->bbox_sum.dh = maInfo->bbox_data[idx].dh;

	maInfo->bbox_data[idx].d2cx = 0;
	maInfo->bbox_sum.d2cx = maInfo->bbox_data[idx].d2cx;
	maInfo->bbox_data[idx].d2cy = 0;
	maInfo->bbox_sum.d2cy = maInfo->bbox_data[idx].d2cy;
	maInfo->bbox_data[idx].d2w = 0;
	maInfo->bbox_sum.d2w = maInfo->bbox_data[idx].d2w;
	maInfo->bbox_data[idx].d2h = 0;
	maInfo->bbox_sum.d2h = maInfo->bbox_data[idx].d2h;
	maInfo->bbox_data[idx].vel = sqrt(maInfo->bbox_data[idx].dcx * maInfo->bbox_data[idx].dcx + maInfo->bbox_data[idx].dcy * maInfo->bbox_data[idx].dcy);
  }

  idx++;
  if (idx == MAE_FRM) {
	idx = 0;
	maInfo->complete = 1;
  }
  maInfo->idx = idx;
}

float match_distance(MAE_info mae_data, Rectf prev_bbox, float *prev_hist,
  Rectf cur_bbox, float *cur_hist, int duration, track_config tconfig) {
  float dist = 1000.0; //Higher value
  float corr = 0;
  Rectf est_bbox;
  float sz_change;

  if (tconfig.tracker_type != ALGO_IOU || tconfig.iou_use_color)
	corr = hist_dist_correlation(prev_hist, cur_hist, CORR_HIST_SIZE);
  
  estimate_bbox_info(mae_data, prev_bbox, duration, &est_bbox);
  float iou = region_overlap(cur_bbox, est_bbox);
  float cur_area = cur_bbox.width * cur_bbox.height;
  float est_area = est_bbox.width * est_bbox.height;

  if (cur_area > est_area)
	sz_change = fabs(cur_area - est_area) / cur_area;
  else
	sz_change = fabs(est_area - cur_area) / est_area;

  if (tconfig.tracker_type != ALGO_IOU || tconfig.iou_use_color) {
	if (corr < tconfig.dist_correlation_threshold ||
	  iou <= tconfig.dist_overlap_threshold ||
	  sz_change > tconfig.dist_scale_change_threshold)
	  return dist;

	dist = (1.0 - corr) * tconfig.dist_correlation_weight +
	  (1.0 - iou) * tconfig.dist_overlap_weight +
	  sz_change * tconfig.dist_scale_change_weight;
  }
  else {
	if (iou <= tconfig.dist_overlap_threshold ||
	  sz_change > tconfig.dist_scale_change_threshold)
	  return dist;

	dist = (1.0 - iou) * tconfig.dist_overlap_weight +
	  sz_change * tconfig.dist_scale_change_weight;
  }
  return dist;
}

float match_distance_long(MAE_info mae_data, Rectf prev_bbox, float *prev_hist,
  float *prev_hist_st, Rectf cur_bbox, float *cur_hist, int duration, track_config tconfig) {
  float dist = 1000.0; //Higher value
  Rectf est_bbox;
  Rectf srh_bbox;
  float corr = 0;
  float percent_inc = tconfig.obj_match_search_region;
  float sz_change;
  float cur_area, est_area;

  if (tconfig.tracker_type != ALGO_IOU || tconfig.iou_use_color) {
	corr = hist_dist_correlation(prev_hist, cur_hist, CORR_HIST_SIZE);
    
	if ((corr < tconfig.dist_correlation_threshold) && (mae_data.idx >= 3 || mae_data.complete)) {
	  corr = hist_dist_correlation(prev_hist_st, cur_hist, CORR_HIST_SIZE);
	}
  }

  /* First check for objects in same location by increasing
     search region around th object.  This is required as
	 prediction failing for nearby objects for noisy detection
	 and for objects changing the path */

  srh_bbox.x = prev_bbox.x - percent_inc * prev_bbox.width;
  if (srh_bbox.x < 0) srh_bbox.x = 0;

  srh_bbox.y = prev_bbox.y - percent_inc * prev_bbox.height;
  if (srh_bbox.y < 0) srh_bbox.y = 0;
  srh_bbox.width = prev_bbox.width + 2 * percent_inc * prev_bbox.width;
  srh_bbox.height = prev_bbox.height + 2 * percent_inc * prev_bbox.height;

  float iou = region_overlap(cur_bbox, srh_bbox);

  if (iou != 0) {
	cur_area = cur_bbox.width * cur_bbox.height;
	est_area = prev_bbox.width * prev_bbox.height;
	//sz_change = fabs(cur_area - est_area) / cur_area;
  }
  else {
	estimate_bbox_info(mae_data, prev_bbox, duration, &est_bbox);

	iou = region_overlap(cur_bbox, est_bbox);
	cur_area = cur_bbox.width * cur_bbox.height;
	est_area = est_bbox.width * est_bbox.height;
	//sz_change = fabs(cur_area - est_area) / cur_area;
  }

  if (cur_area > est_area)
	sz_change = fabs(cur_area - est_area) / cur_area;
  else
	sz_change = fabs(est_area - cur_area) / est_area;

  if (tconfig.tracker_type != ALGO_IOU || tconfig.iou_use_color) {
	if (corr < tconfig.dist_correlation_threshold ||
	  iou <= tconfig.dist_overlap_threshold ||
	  sz_change > tconfig.dist_scale_change_threshold)
	  return dist;

	dist = (1.0 - corr) * tconfig.dist_correlation_weight +
	 (1.0 - iou) * tconfig.dist_overlap_weight +
	  sz_change * tconfig.dist_scale_change_weight;
  }
  else {
	if (iou <= tconfig.dist_overlap_threshold ||
	  sz_change > tconfig.dist_scale_change_threshold)
	  return dist;

	dist = (1.0 - iou) * tconfig.dist_overlap_weight +
	  sz_change * tconfig.dist_scale_change_weight;
  }

  return dist;
}

int find_empty_tracker(vvas_tracker **tracker) {
  int idx = -1;
  int j, max_cnt, max_idx;

  //1. Tracker not in use
  for (j = 0; j < MAX_OBJ_TRACK; j++) {
	if (tracker[j]->status == -3 || tracker[j]->status == -2)
	  break;
  }
  if (j != MAX_OBJ_TRACK)
	return j;

  //2. Tracker with high inactive count
  max_idx = -1;
  max_cnt = 0;
  for (j = 0; j < MAX_OBJ_TRACK; j++) {
	if (tracker[j]->status == -1)
	  if (tracker[j]->cnt_inactive > max_cnt) {
		max_idx = j;
		max_cnt = tracker[j]->cnt_inactive;
	  }
  }

  if (max_idx != -1)
	return max_idx;

  return idx;
}


void track_by_detection(vvas_tracker **tracker, objs_data new_objs, int *ids, Mat_img img, track_config tconfig) {
  int i, j, k, l, min_idx;
  float iou;
  int found_det[MAX_OBJ_TRACK], found_trk[MAX_OBJ_TRACK];
  float dist_match[MAX_OBJ_TRACK * MAX_OBJ_TRACK];
  int assignment[MAX_OBJ_TRACK], assign_idx[MAX_OBJ_TRACK];
  float dist, min_dist;
  float obj_hist[MAX_OBJ_TRACK][CORR_HIST_SIZE];
  Rectf bbox1, bbox2;
  vector<vector<double>> dist_v;
  
  vector<int> assignment_v;

  HungarianAlgorithm HungAlgo;

  for (i = 0; i < MAX_OBJ_TRACK; i++) {
	found_det[i] = 0;
	found_trk[i] = 0;
  }

  int obj = min(new_objs.num_objs, MAX_OBJ_TRACK);
  
  //Check removes noisy objects
  for (i = 0; i < obj; i++) {
	if ((unsigned int)new_objs.objs[i].height < tconfig.min_height ||
	  (unsigned int)new_objs.objs[i].width < tconfig.min_width)
	  found_det[i] = -2; //Noisy object
	else if ((unsigned int)new_objs.objs[i].height > tconfig.max_height ||
	  (unsigned int)new_objs.objs[i].width > tconfig.max_width)
	  found_det[i] = -2; //Noisy object	
  }

  //Check removes overlapped objects
  for (i = 0; i < obj; i++) {
	bbox1.x = new_objs.objs[i].x;
	bbox1.y = new_objs.objs[i].y;
	bbox1.width = new_objs.objs[i].width;
	bbox1.height = new_objs.objs[i].height;

	if (found_det[i] != -2) {
	  for (j = 0; j < obj; j++) {
		if ((i != j) && (found_det[j] != -2)) {
		  bbox2.x = new_objs.objs[j].x;
		  bbox2.y = new_objs.objs[j].y;
		  bbox2.width = new_objs.objs[j].width;
		  bbox2.height = new_objs.objs[j].height;
		  iou = percentage_occlusion(bbox1, bbox2);
		  if (iou > tconfig.occlusion_threshold) {
			found_det[i] = -1;
		  }
		}
	  }
	}

	if ((tconfig.tracker_type != ALGO_IOU || tconfig.iou_use_color)
	  && (!found_det[i])) {
	  if (tconfig.obj_match_color == USE_HSV)
	    est_histogram_hsv(img, bbox1, obj_hist[i], CORR_HIST_SIZE);
	  else
		est_histogram_rgb(img, bbox1, obj_hist[i], CORR_HIST_SIZE);
	}
  }

  int det_num = 0, trk_num = 0;
  //Newly detected objects after removing noisy and heavely occluded objects
  for (i = 0; i < obj; i++) {
	if (!found_det[i])
	  det_num++;
  }

  //Active and internal tracking objects 
  for (j = 0; j < MAX_OBJ_TRACK; j++) {
	if (!found_trk[j] && tracker[j]->status >= 0) {
	  assign_idx[trk_num] = j;
	  trk_num++;
	}
  }

  //Associate active trackers and detected objects
  l = 0; i = 0;
  if (det_num && trk_num) {
	i = 0;
	for (k = 0; k < obj; k++) {
	  if (!found_det[k]) {
		l = 0;
		for (j = 0; j < MAX_OBJ_TRACK; j++) {
		  if (!found_trk[j] && tracker[j]->status >= 0) {
			bbox1.x = new_objs.objs[k].x;
			bbox1.y = new_objs.objs[k].y;
			bbox1.width = new_objs.objs[k].width;
			bbox1.height = new_objs.objs[k].height;
			dist = match_distance(tracker[j]->movingAvg_info, tracker[j]->prev_pos,
			  tracker[j]->hist_map, bbox1, obj_hist[k], 1, tconfig); //dist_mod
			dist_match[l*det_num + i] = dist;
			l++;
		  }
		}
		i++;
	  }
	}

	for (j = 0; j < l; j++) {
	  vector<double> v1;
	  for (k = 0; k < i; k++) {
		v1.push_back(dist_match[j*det_num + k]);
	  }
	  dist_v.push_back(v1);
	}

	HungAlgo.Solve(dist_v, assignment_v);

	for (j = 0; j < i; j++)
	  assignment[j] = -1;

	for (j = 0; j < l; j++) {
	  if (assignment_v[j] >= 0)
	    assignment[assignment_v[j]] = j;
	}

	i = 0;
	for (k = 0; k < obj; k++) {
	  if (!found_det[k]) {
		if (assignment[i] >= 0) {
		  if (dist_match[assignment[i] * det_num + i] != 1000.0) {
			int trk_id = assign_idx[assignment[i]];
			bbox1.x = new_objs.objs[k].x;
			bbox1.y = new_objs.objs[k].y;
			bbox1.width = new_objs.objs[k].width;
			bbox1.height = new_objs.objs[k].height;
			bbox1.map_id = new_objs.objs[k].map_id;
			tracker[trk_id]->detect_update(bbox1, img, obj_hist[k]);
			if (tracker[trk_id]->cnt_confidence >= tconfig.num_frames_confidence && tracker[trk_id]->status == 0) {
			  //Compare with all inactive trackers and find best match
			  min_dist = 1000.0;
			  min_idx = MAX_OBJ_TRACK;
			  for (j = 0; j < MAX_OBJ_TRACK; j++) {
				if (!found_trk[j] && tracker[j]->status == -1 && trk_id != j) {
				  dist = match_distance_long(tracker[j]->movingAvg_info, tracker[j]->prev_pos,
					tracker[j]->hist_map, tracker[j]->hist_map_st, tracker[trk_id]->obj_rect, tracker[trk_id]->hist_map,
					tracker[j]->cnt_inactive, tconfig);//dist_mod
				 
				  if (dist < min_dist) {
					min_dist = dist;
					min_idx = j;
				  }
				}
			  }

			  if (min_idx == MAX_OBJ_TRACK) { //No matching inactive tracker found assign new id
				tracker[trk_id]->status = 1;
				tracker[trk_id]->id = *ids;
				(*ids)++;
				if (*ids == MAX_OBJ_TRACK)
				  *ids = (*ids) % MAX_OBJ_TRACK;
				}
			  else {
				tracker[trk_id]->id = tracker[min_idx]->id;
				tracker[trk_id]->status = 1;
				tracker[min_idx]->status = -2;
				found_trk[min_idx] = 1;
			  }
			}
			found_det[k] = 1;
			found_trk[trk_id] = 1;
		  }
		}
		i++;
	  }
	}
  }

  //Handling remaining tracking objects
  for (j = 0; j < MAX_OBJ_TRACK; j++) {
	if (!found_trk[j]) {
	  if (tracker[j]->status == -1) {
		if (!(tracker[j]->cnt_update % UPDATE_INTRVL)) {
		  tracker[j]->cnt_inactive++;
		  tracker[j]->cnt_update = 1;
		}
		else
		  tracker[j]->cnt_update++;

		if (tracker[j]->cnt_inactive >= tconfig.num_inactive_frames)
		  tracker[j]->status = -2;
	  }
	  else if (tracker[j]->status == 1) {
		tracker[j]->status = -1;
		tracker[j]->cnt_inactive++;
		if (!(tracker[j]->cnt_update % UPDATE_INTRVL)) {
		  tracker[j]->cnt_update = 1;
		}
		else
		  tracker[j]->cnt_update++;
	  }
	  else if (tracker[j]->status == 0) {
		tracker[j]->status = -2;
	  }
	}
  }

  //Create new tracker for left over detected objects
  for (i = 0; i < obj; i++) {
	if (!found_det[i]) {

	  //Find empty tracker
	  j = find_empty_tracker(tracker);

	  tracker[j]->deinit_tracker();
	  bbox1.x = new_objs.objs[i].x;
	  bbox1.y = new_objs.objs[i].y;
	  bbox1.width = new_objs.objs[i].width;
	  bbox1.height = new_objs.objs[i].height;
	  bbox1.map_id = new_objs.objs[i].map_id;
	  tracker[j]->init_tracker(tconfig, bbox1, img, obj_hist[i]);
	  found_det[i] = 1;
	  found_trk[j] = 1;
	}
  }
}

void vvas_tracker::init_tracker(track_config tconfig, Rectf bbox, Mat_img img, float *hist_new) {
  if(tracker_type == ALGO_MOSSE)
	mosse_tracker.init_cf(tconfig, bbox, img);
  else if (tracker_type == ALGO_KCF)
	corr_tracker.init_cf(tconfig, bbox, img);

  id = -1;
  conf_score = 1;
  status = 0;
  cnt_inactive = 0;
  cnt_confidence = 1;
  cnt_update = 1;
  movingAvg_info.bbox_sum.cx = 0;
  movingAvg_info.bbox_sum.cy = 0;
  movingAvg_info.bbox_sum.w = 0;
  movingAvg_info.bbox_sum.h = 0;
  movingAvg_info.bbox_sum.dcx = 0;
  movingAvg_info.bbox_sum.dcy = 0;
  movingAvg_info.bbox_sum.dw = 0;
  movingAvg_info.bbox_sum.dh = 0;
  movingAvg_info.bbox_sum.vel = 0;
  movingAvg_info.idx = 0;
  movingAvg_info.complete = 0;
  obj_rect = bbox;
  prev_pos = bbox;
  memcpy(hist_map, hist_new, sizeof(float)*CORR_HIST_SIZE);
}

void vvas_tracker::deinit_tracker() {
  if (status != -3) {
	if (tracker_type == ALGO_MOSSE)
	  mosse_tracker.deinit_cf();
	else if (tracker_type == ALGO_KCF)
	  corr_tracker.deinit_cf();
  }
}

void vvas_tracker::detect_update(Rectf bbox, Mat_img img, float *hist_new) {
  ema_update_bbox(prev_pos, &bbox);

  if (!(cnt_update % UPDATE_INTRVL)) {
	update_movingAverageInfo(&movingAvg_info, prev_pos, bbox);
	cnt_update = 1;
  }
  else
	cnt_update++;

  if (tracker_type == ALGO_MOSSE)
	mosse_tracker.detect_update(bbox, img);
  else if (tracker_type == ALGO_KCF)
	corr_tracker.detect_update(bbox, img);

  conf_score = 1;
  cnt_inactive = 0;
  if (status == 0)
	cnt_confidence++;
  
  obj_rect = bbox;
  prev_pos = bbox;
  memcpy(hist_map, hist_new, sizeof(float)*CORR_HIST_SIZE);

  //Instead of storing histogram in first frame storing after
  //few time intervals (assuming that object might came out of
  //partial occlusion
  if(movingAvg_info.idx == 3 && !movingAvg_info.complete)
	memcpy(hist_map_st, hist_new, sizeof(float)*CORR_HIST_SIZE);
}

void vvas_tracker::track_update(Mat_img img, track_config tconfig) {
  float conf;
  Rectf bbox;
  
  if (tracker_type == ALGO_MOSSE)
	conf = mosse_tracker.update_position(img, &bbox, tconfig.confidence_score);
  else if (tracker_type == ALGO_KCF)
	conf = corr_tracker.update_position(img, &bbox, tconfig.confidence_score);
  else
	return;

  //map prediction id
  bbox.map_id = prev_pos.map_id;
  if (conf < tconfig.confidence_score) {
    if (status == 1)
      status = -1;
    else
      status = -2;
    cnt_inactive++;
  }
  else {
	conf_score = conf;
	ema_update_bbox(prev_pos, &bbox);

	if (!(cnt_update % UPDATE_INTRVL)) {
	  update_movingAverageInfo(&movingAvg_info, prev_pos, bbox);
	  cnt_update = 1;
	}
	else
	  cnt_update++;
	
	obj_rect = bbox;
  }
}

int init_tracker(tracker_handle *tracker_data, track_config *config) {
  vvas_tracker **trackers;
  
  trackers = (vvas_tracker **)malloc(sizeof(vvas_tracker *) * MAX_OBJ_TRACK);

  for (int i = 0; i < MAX_OBJ_TRACK; i++) {
	trackers[i] = (vvas_tracker *)malloc(sizeof(vvas_tracker) * MAX_OBJ_TRACK);
	trackers[i]->status = -3;
	if (config->tracker_type == ALGO_IOU)
	  trackers[i]->tracker_type = 0;
	else if (config->tracker_type == ALGO_MOSSE)
	  trackers[i]->tracker_type = 1;
	else if (config->tracker_type == ALGO_KCF)
	  trackers[i]->tracker_type = 2;
	else 
	  trackers[i]->tracker_type = -1;

  }

  tracker_data->tracker_info = (char *)trackers;
  return 0;
}

int deinit_tracker(tracker_handle *tracker_data) {
  vvas_tracker **trackers;
  trackers = (vvas_tracker **)tracker_data->tracker_info;
  for (int i = 0; i < MAX_OBJ_TRACK; i++) {
	trackers[i]->deinit_tracker();
	free(trackers[i]);
  }

  free(trackers);
  tracker_data->tracker_info = NULL;
  return 0;
}

void out_object_tracker_info(vvas_tracker **tracker, track_config tconfig, objs_data *trk_objs) {
  int obj_cnt = 0;
  for (int i = 0; i < MAX_OBJ_TRACK; i++) {
    if (tracker[i]->status >= -1) {
      trk_objs->objs[obj_cnt].x = tracker[i]->obj_rect.x;
      trk_objs->objs[obj_cnt].y = tracker[i]->obj_rect.y;
      trk_objs->objs[obj_cnt].width = tracker[i]->obj_rect.width;
      trk_objs->objs[obj_cnt].height = tracker[i]->obj_rect.height;
      trk_objs->objs[obj_cnt].map_id = tracker[i]->obj_rect.map_id;
      trk_objs->objs[obj_cnt].status = tracker[i]->status;
      if (tracker[i]->status == 1) {
        trk_objs->objs[obj_cnt].trk_id = tracker[i]->id;
      }
      obj_cnt++;
    }
  }
  trk_objs->num_objs = obj_cnt;
}

int objects_track_update(vvas_tracker **tracker, Mat_img img, track_config tconfig) {
  for (int i = 0; i < MAX_OBJ_TRACK; i++) {
	if (tracker[i]->status >= 0)
	  tracker[i]->track_update(img, tconfig);
	else if (tracker[i]->status == -1) {
	  if (!(tracker[i]->cnt_update % UPDATE_INTRVL)) {
		tracker[i]->cnt_inactive++;
		tracker[i]->cnt_update = 1;
	  }
	  else
		tracker[i]->cnt_update++;
	}
  }

  return 0;
}

int objects_detect_update(vvas_tracker **tracker, objs_data new_objs, int *ids, Mat_img img, track_config tconfig) {
  track_by_detection(tracker, new_objs, ids, img, tconfig);
  
  return 0;
}

int run_tracker(Mat_img img, track_config tconfig, tracker_handle *tracker_data, bool detect_flag) {
  vvas_tracker **trackers;
 
  trackers = (vvas_tracker **)tracker_data->tracker_info;
  
  if (detect_flag)
	objects_detect_update(trackers, tracker_data->new_objs, &tracker_data->ids, img, tconfig);
  else
	objects_track_update(trackers, img, tconfig);

  out_object_tracker_info(trackers, tconfig, &tracker_data->trk_objs);

  return 0;
}
