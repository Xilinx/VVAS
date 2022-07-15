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

#include "mosse_filter.hpp"
#include "fft.hpp"
#include <array>

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#ifdef XLNX_EMBEDDED_PLATFORM
#include <arm_neon.h>
//#include "NE10.h"
//#include "fft_neon.hpp"
#endif

//Gaussian weigt map is created based on size of object.
void mosse_filter::est_gaussian_wts(int rows, int cols, fMat *dst)
{
  float *fptr;
  int j_pos;
  dst->width = cols;
  dst->height = rows;

  int cy = (rows) / 2;
  int cx = (cols) / 2;

  float sigma = std::sqrt((float)cols * rows) / padding_scale * MOSSE_GUS_SIGMA;
  float mult = -0.5 / (sigma * sigma);
  fptr = dst->data;
  
#ifdef XLNX_EMBEDDED_PLATFORM
  int32_t blkCnt;
  float32x4_t _ipos, _jpos, _mult;
  float32x4_t _isqr, _jsqr, _coeff;
  int32_t j;
  float32_t idx[4], ipos;
  
  blkCnt = (cols >> 2U) << 2U;
  
  _mult = vdupq_n_f32(mult);
  
  for (int i = 0; i < rows; i++){
	ipos = i - cy;
	j=0;
	_ipos = vdupq_n_f32(ipos);
	_isqr = vmulq_f32(_ipos, _ipos);
	  
	while(j<blkCnt)
	{
	  idx[0] = j-cx;j++;
	  idx[1] = j-cx;j++;
	  idx[2] = j-cx;j++;
	  idx[3] = j-cx;j++;
		  
	  _jpos = vld1q_f32(idx);
		  
	  _jsqr = vmulq_f32(_jpos, _jpos);
		  
	  _coeff = vmulq_f32(_mult, vaddq_f32(_isqr, _jsqr));
		  
	  *fptr++ = std::exp(vgetq_lane_f32(_coeff, 0));
	  *fptr++ = std::exp(vgetq_lane_f32(_coeff, 1));
	  *fptr++ = std::exp(vgetq_lane_f32(_coeff, 2));
	  *fptr++ = std::exp(vgetq_lane_f32(_coeff, 3));
	}
	  
	while (j<cols) {
	  j_pos = j++ - cx;
	  *fptr++ = std::exp(mult * (float)(ipos * ipos + j_pos * j_pos));
	}  
  }
#endif

#ifdef XLNX_PCIe_PLATFORM
  int i_pos;
  for (int i = 0; i < rows; i++) {
	for (int j = 0; j < cols; j++) {
	  i_pos = i - cy;
	  j_pos = j - cx;
	  *fptr = std::exp(mult * (float)(i_pos * i_pos + j_pos * j_pos));
	  fptr++;
	}
  }
#endif
}

// create Hanning mat for data smoothing. Created first time for each object
void mosse_filter::hanning_weights(int rows, int cols, fMat *hann_mat) {
  float w_x[MOSSE_TMPL_SZ], w_y[MOSSE_TMPL_SZ];
  float *fptr;

  hann_mat->width = cols;
  hann_mat->height = rows;
  fptr = hann_mat->data;
  
  for (int i = 0; i < cols; i++)
	w_x[i] = 0.5 * (1 - std::cos(2 * PI * i / (cols - 1)));
  for (int i = 0; i < rows; i++)
	w_y[i] = 0.5 * (1 - std::cos(2 * PI * i / (rows - 1)));

#ifdef XLNX_EMBEDDED_PLATFORM
  uint32_t blkCnt, t_blkCnt;
  uint32_t rm_blkCnt, t_rm_blkCnt;
  float32x4_t _w_y, _w_x, _mult;
  float32_t *pwx;
  
  blkCnt = cols >> 2U;
  rm_blkCnt = cols & 0x3;
  
  for (int i = 0; i < rows; i++){
	_w_y = vdupq_n_f32(w_y[i]);
	t_blkCnt = blkCnt;
	pwx = w_x;
	while (t_blkCnt > 0U)
	{
	  _w_x = vld1q_f32(pwx);
	  _mult = vmulq_f32(_w_y, _w_x);
	  vst1q_f32(fptr, _mult);
		  
	  fptr += 4;
	  pwx += 4;
	  t_blkCnt--;
	}
	t_rm_blkCnt = rm_blkCnt;
	while (t_rm_blkCnt > 0U) {
	  *fptr = w_y[i] * *pwx;
	  fptr++;
	  pwx++;
	  t_rm_blkCnt--;
	} 
  }
#endif

#ifdef XLNX_PCIe_PLATFORM
  
  for (int i = 0;i < rows;i++) {
	for (int j = 0;j < cols;j++) {
	  *fptr = w_y[i] * w_x[j];
	  fptr++;
	}
  }
#endif
}

#define FACTOR      2048
#define SHIFT       11

void resize(Mat_img *src, fMat *dst) {
  const int xs = (int)((double)FACTOR * src->width / dst->width + 0.5);
  const int ys = (int)((double)FACTOR * src->height / dst->height + 0.5);
  unsigned char *source = src->data;
  float *target = dst->data;

  for (int y = 0; y < dst->height; y++)
  {
	const int sy = y * ys;
	const int y0 = sy >> SHIFT;
	const int fracy = sy - (y0 << SHIFT);

	for (int x = 0; x < dst->width; x++)
	{
	  const int sx = x * xs;
	  const int x0 = sx >> SHIFT;
	  const int fracx = sx - (x0 << SHIFT);

	  if (x0 >= src->width - 1 || y0 >= src->height - 1)
	  {
		// insert special handling here
		continue;
	  }

	  const int offset = y0 * src->width + x0;
	  target[y * dst->width + x] = 	((source[offset] * (FACTOR - fracx) * (FACTOR - fracy) +
		  source[offset + 1] * fracx * (FACTOR - fracy) +
		  source[offset + src->width] * (FACTOR - fracx) * fracy +
		  source[offset + src->width + 1] * fracx * fracy +
		  (FACTOR * FACTOR / 2)) >> (2 * SHIFT));
	}
  }
}

void mosse_filter::extract_object(Mat_img frame, bool first, float scale, fMat *obj)
{
  Rect new_roi;

  float cx = bbox.x + bbox.width / 2;
  float cy = bbox.y + bbox.height / 2;

  if (first) {
	int new_width = bbox.width * padding_scale;
	int new_height = bbox.height * padding_scale;
	
	if (tmpl_sz > 1) {
	  if (new_width >= new_height) {
		scale_factor_x = new_width / (float)tmpl_sz;
		obj_size.width = tmpl_sz;

		obj_size.height = new_height / scale_factor_x;

		if (obj_size.height < 8) {
		  obj_size.height = 8;
		}
		else {
		  obj_size.height = (obj_size.height / 4) * 4;

		  if (obj_size.height > 80)
			obj_size.height = tmpl_sz;
		  else if (obj_size.height % 7 == 0)
			obj_size.height = (obj_size.height / 7) * 8;
		  else if (obj_size.height % 11 == 0)
			obj_size.height = 40;//(obj_size.height / 11) * 10;
		  else if (obj_size.height % 13 == 0)
			obj_size.height = 48;//obj_size.height / 13) * 12;
		  else if (obj_size.height % 17 == 0)
			obj_size.height = 64;//obj_size.height / 17) * 16;
		  else if (obj_size.height % 19 == 0)
			obj_size.height = 72;//obj_size.height / 19) * 18;
		}

		scale_factor_y = new_height / (float)obj_size.height;
	  }
	  else {
		scale_factor_y = new_height / (float)tmpl_sz;
		obj_size.height = tmpl_sz;

		obj_size.width = new_width / scale_factor_y;

		if (obj_size.width < 8) {
		  obj_size.width = 8;
		}
		else {
		  obj_size.width = (obj_size.width / 4) * 4;

		  if (obj_size.width > 80)
			obj_size.width = tmpl_sz;
		  else if (obj_size.width % 7 == 0)
			obj_size.width = (obj_size.width / 7) * 8;
		  else if (obj_size.width % 11 == 0)
			obj_size.width = 40;//(obj_size.height / 11) * 10;
		  else if (obj_size.width % 13 == 0)
			obj_size.width = 48;//obj_size.height / 13) * 12;
		  else if (obj_size.width % 17 == 0)
			obj_size.width = 64;//obj_size.height / 17) * 16;
		  else if (obj_size.width % 19 == 0)
			obj_size.width = 72;//obj_size.height / 19) * 18;
		}

		scale_factor_x = new_width / (float)obj_size.width;
	  }
	}
	else {
	  obj_size.width = new_width;
	  obj_size.height = new_width;
	  scale_factor_x = 1;
	  scale_factor_y = 1;
	}
  }

  new_roi.width = scale * padding_scale * bbox.width;
  new_roi.height = scale * padding_scale * bbox.height;
  // center of roi with new size
  new_roi.x = cx - new_roi.width / 2;
  new_roi.y = cy - new_roi.height / 2;

  z.width = new_roi.width;
  z.height = new_roi.height;
  get_subwindow(frame, new_roi, &z);

  if (z.width != obj_size.width || z.height != obj_size.height) {
	obj->width = obj_size.width;
	obj->height = obj_size.height;

	//ResizeBilinear(z.data, z.width, z.height, z.width, rsz_z.data, rsz_z.width, rsz_z.height, rsz_z.width, 1);
	resize(&z, obj);
  }
  else {
	obj->width = obj_size.width;
	obj->height = obj_size.height;

	for (int i = 0; i < obj->width*obj->height; i++)
	  obj->data[i] = (float)z.data[i];
	//rsz_z = z;
  }
}

void mosse_filter::filter_response() {
  short width = in_F.width;
  short height = in_F.height;
  
  A.width = width;
  A.height = height;
  filter_H.width = width;
  filter_H.height = height;

  width >>= 1;
  B.width = width;
  B.height = height;
  
#ifdef XLNX_EMBEDDED_PLATFORM
  arm_cmplx_mult_cmplx_flt_resp_f32(in_F, gau_wt, &A, &B, &filter_H);
#endif

#ifdef XLNX_PCIe_PLATFORM
  float *F_rptr, *F_iptr;
  float *G_rptr, *G_iptr;
  float *A_rptr, *A_iptr;
  float *H_rptr, *H_iptr;
  float *B_rptr, inv_val;
  
  F_rptr = in_F.data;
  F_iptr = in_F.data + 1;
  G_rptr = gau_wt.data;
  G_iptr = gau_wt.data + 1;
  A_rptr = A.data;
  A_iptr = A.data + 1;
  B_rptr = B.data;
  H_rptr = filter_H.data;
  H_iptr = filter_H.data + 1;

  for (short i = 0; i < width * height; i++) {
	*A_rptr = *F_rptr * *G_rptr + *F_iptr * *G_iptr;
	*A_iptr = *F_rptr * *G_iptr - *F_iptr * *G_rptr;

	*B_rptr = *F_rptr * *F_rptr + *F_iptr * *F_iptr;

	//if (*B_rptr)
	  inv_val = 1.0 / (*B_rptr + 0.0001f);
	//else
	  //inv_val = 1.0;

	*H_rptr = *A_rptr * inv_val;
	*H_iptr = *A_iptr * inv_val;

	F_rptr += 2; F_iptr += 2;
	G_rptr += 2; G_iptr += 2;
	A_rptr += 2; A_iptr += 2;
	B_rptr++;
	H_rptr += 2; H_iptr += 2;
  }
#endif
}

void mosse_filter::filter_response_update() {
#ifdef XLNX_EMBEDDED_PLATFORM
  arm_cmplx_mult_cmplx_flt_resp_upate_f32(in_F, gau_wt, &A, &B, &filter_H, lrn_rate);
#endif

#ifdef XLNX_PCIe_PLATFORM
  float *F_rptr, *F_iptr;
  float *G_rptr, *G_iptr;
  float *A_rptr, *A_iptr;
  float *H_rptr, *H_iptr;
  float *B_rptr, inv_val;
  short width = in_F.width >> 1;

  F_rptr = in_F.data;
  F_iptr = in_F.data + 1;
  G_rptr = gau_wt.data;
  G_iptr = gau_wt.data + 1;
  A_rptr = A.data;
  A_iptr = A.data + 1;
  B_rptr = B.data;
  H_rptr = filter_H.data;
  H_iptr = filter_H.data + 1;

  for (short i = 0; i < width * in_F.height; i++) {
	*A_rptr = (*F_rptr * *G_rptr + *F_iptr * *G_iptr) * lrn_rate + *A_rptr * inv_lrn_rate;
	*A_iptr = (*F_rptr * *G_iptr - *F_iptr * *G_rptr) * lrn_rate + *A_iptr * inv_lrn_rate;

	*B_rptr = (*F_rptr * *F_rptr + *F_iptr * *F_iptr) * lrn_rate + *B_rptr * inv_lrn_rate;

	//if (*B_rptr)
	inv_val = 1.0 / (*B_rptr + 0.0001f);
	//else
	  //inv_val = 1.0;

	*H_rptr = *A_rptr * inv_val;
	*H_iptr = *A_iptr * inv_val;

	F_rptr += 2; F_iptr += 2;
	G_rptr += 2; G_iptr += 2;
	A_rptr += 2; A_iptr += 2;
	B_rptr++;
	H_rptr += 2; H_iptr += 2;
  }
#endif
}

// finding sub_pixel accuracy
float mosse_filter::sub_pix_estimation(float pre, float cur, float nxt) {
  float den = 2 * cur - nxt - pre;

  if (den)
	return 0.5 * (nxt - pre) / den;
  else
	return 0;
}

void mosse_filter::estimate_fourier(fMat src, fMat dst, bool fft_type) {
#ifdef XLNX_EMBEDDED_PLATFORM
  ne10_fft_cfg_float32_t cfg;
  cfg = ne10_fft_alloc_c2c_float32_neon(src.height);
  dft_neon(src, 1, dst, 2, fft_type, cfg);
  free(cfg);
#endif

#ifdef XLNX_PCIe_PLATFORM
  dft(src, 1, dst, 2, fft_type);
#endif
}

void mosse_filter::apply_Hanning_window(fMat hann_win, fMat *inout) {
#ifdef XLNX_EMBEDDED_PLATFORM
  arm_mult_sm_f32(inout, hann_win);
#endif

#ifdef XLNX_PCIe_PLATFORM
  float *fptr = inout->data;
  float *fptr1 = hann_win.data;
  for (int i = 0; i < hann_win.height; i++) {
	for (int j = 0; j < hann_win.width; j++) {
	  *fptr *= *fptr1;
	  fptr++;
	  fptr1++;
	}
  }
#endif
}

// tracker initalization 
void mosse_filter::init_cf(track_config tconfig, Rectf bbox_new, Mat_img frame)
{
  int size;
  int roi_size;
  
  padding_scale = tconfig.padding;

  lrn_rate = MOSSE_LRN_RATE;
  inv_lrn_rate = 1.0 - lrn_rate;

  if (tconfig.multiscale) { // multiscale
	scale_type = tconfig.multiscale;
	tmpl_sz = MOSSE_TMPL_SZ;
	scaling = MOSSE_SCALING_FACT;
	if (!tconfig.fixed_window) {
	  tconfig.fixed_window = true;
	}
  }
  else if (tconfig.fixed_window) {
	tmpl_sz = MOSSE_TMPL_SZ;
	scaling = 1;
  }
  else {
	tmpl_sz = 1;
	scaling = 1;
  }

  //Assuming template is of 96 
  //roi_size + resize + hann_wt + 6 * 104 * 104 * 2 (complex)
  //roi_size + 104*104 * 4 + 104 * 104  * 4 + 6 * (104*104) * 2 * 4;
  roi_size = (frame.width * frame.height) >> 0;
  size = roi_size + 605696;

  head_ptr = calloc(size, 1);
  z.data = (unsigned char *)head_ptr;
  rsz_z.data = (float *)(z.data + roi_size);
  hann_wts.data = rsz_z.data + 10816;//104*104
  gau_wt.data = hann_wts.data + 10816;//104*104
  in_F.data = gau_wt.data + 21632;//104*104*4*2
  filter_H.data = in_F.data + 21632;
  resp_G.data = filter_H.data + 21632;
  A.data = resp_G.data + 21632;
  B.data = A.data + 21632;
  
  bbox = bbox_new;

  extract_object(frame, 1, 1.0, &rsz_z);
  
  hanning_weights(obj_size.height, obj_size.width, &hann_wts);

  apply_Hanning_window(hann_wts, &rsz_z);
  
  est_gaussian_wts(obj_size.height, obj_size.width, &resp_G);
  
  gau_wt.width = resp_G.width << 1;
  gau_wt.height = resp_G.height;
  estimate_fourier(resp_G, gau_wt, false);
  
  in_F.width = rsz_z.width << 1;
  in_F.height = rsz_z.height;
  estimate_fourier(rsz_z, in_F, false);

  filter_response();

  prev_obj_size.width = obj_size.width;
  prev_obj_size.height = obj_size.height;
}

// Initialize tracker 
void mosse_filter::deinit_cf() {
  if (head_ptr != NULL) {
	free(head_ptr);
	head_ptr = NULL;
  }
}

// Update position based on the new frame
void mosse_filter::detect_update(Rectf bbox_new, Mat_img image) {
  bbox = bbox_new;

  extract_object(image, 1, 1.0, &rsz_z);

  if (prev_obj_size.width != obj_size.width || prev_obj_size.height != obj_size.height) {
	hanning_weights(obj_size.height, obj_size.width, &hann_wts);

	est_gaussian_wts(obj_size.height, obj_size.width, &resp_G);

	gau_wt.width = resp_G.width << 1;
	gau_wt.height = resp_G.height;
	estimate_fourier(resp_G, gau_wt, false);

	prev_obj_size.width = obj_size.width;
	prev_obj_size.height = obj_size.height;
  }

  apply_Hanning_window(hann_wts, &rsz_z);

  in_F.width = rsz_z.width << 1;
  in_F.height = rsz_z.height;
  estimate_fourier(rsz_z, in_F, false);

  filter_response();
}

void mosse_filter::train_update() {
  apply_Hanning_window(hann_wts, &rsz_z);

  in_F.width = rsz_z.width << 1;
  in_F.height = rsz_z.height;
  estimate_fourier(rsz_z, in_F, false);

  filter_response_update();
}

// Detect object in the current frame.
Point2f mosse_filter::locate_new_position(float &peak_value) {
  int width = rsz_z.width;

  apply_Hanning_window(hann_wts, &rsz_z);

  in_F.width = rsz_z.width << 1;
  in_F.height = rsz_z.height;
  estimate_fourier(rsz_z, in_F, false);

  resp_G.width = rsz_z.width << 1;
  resp_G.height = rsz_z.height;
  complexMultiplication(in_F, filter_H, resp_G);

  dft(resp_G, 2, in_F, 2, true);

  int pix, piy;
  float pival;
  Point2f p;
  pival = in_F.data[0];
  pix = 0;

  for (int i = 2; i < in_F.height*in_F.width; i += 2)
	if (in_F.data[i] > pival) {
	  pival = in_F.data[i];
	  pix = i;
	}
  pix >>= 1;
  peak_value = pival;
  piy = pix / width;
  pix = pix % width;

  p.x = (float)pix;
  p.y = (float)piy;

  /*if (pix > 0 && pix < width - 1) {
	p.x += sub_pix_estimation(in_F.data[(piy * width + pix - 1) << 0], peak_value, in_F.data[(piy*width + pix + 1) << 0]);
  }

  if (piy > 0 && piy < in_F.height - 1) {
	p.y += sub_pix_estimation(in_F.data[((piy - 1)*width + pix) << 0], peak_value, in_F.data[((piy + 1)*width + pix) << 0]);
  }*/

  p.x -= (width) / 2;
  p.y -= (in_F.height) / 2;

  return p;
}

// Update position based on the new frame
float mosse_filter::update_position(Mat_img image, Rectf *bbox_out, float conf_score)
{
  if (bbox.x + bbox.width <= 0) bbox.x = -bbox.width + 1;
  if (bbox.y + bbox.height <= 0) bbox.y = -bbox.height + 1;
  if (bbox.x >= image.width - 1) bbox.x = image.width - 2;
  if (bbox.y >= image.height - 1) bbox.y = image.height - 2;

  float cx = bbox.x + bbox.width / 2.0f;
  float cy = bbox.y + bbox.height / 2.0f;
  float peak_value;

  extract_object(image, 0, 1.0, &rsz_z);

  Point2f res = locate_new_position(peak_value);

  /*if (scaling != 1) {
	// locating at lower scale factor
	float new_peak_value;
	if (scale_type == 1 || scale_type == 3) {
	  extract_object(image, 0, 1.0f / scaling, &rsz_z);

	  Point2f new_res = locate_new_position(new_peak_value);

	  if (SCALE_WT * new_peak_value > peak_value) {
		res = new_res;
		peak_value = new_peak_value;
		scale_factor_x /= scaling;
		scale_factor_y /= scaling;
		bbox.width /= scaling;
		bbox.height /= scaling;
	  }
	}

	// locating at higher scale factor
	if (scale_type == 1 || scale_type == 2) {
	  extract_object(image, 0, scaling, &rsz_z);

	  Point2f new_res = locate_new_position(new_peak_value);

	  if (SCALE_WT * new_peak_value > peak_value) {
		res = new_res;
		peak_value = new_peak_value;
		scale_factor_x *= scaling;
		scale_factor_y *= scaling;
		bbox.width *= scaling;
		bbox.height *= scaling;
	  }
	}
  }*/

  if (peak_value >= conf_score) {
	bbox.x = cx - bbox.width / 2.0f + ((float)res.x * scale_factor_x);
	bbox.y = cy - bbox.height / 2.0f + ((float)res.y * scale_factor_y);

	if (bbox.x >= image.width - 1) bbox.x = image.width - 1;
	if (bbox.y >= image.height - 1) bbox.y = image.height - 1;
	if (bbox.x + bbox.width <= 0) bbox.x = -bbox.width + 2;
	if (bbox.y + bbox.height <= 0) bbox.y = -bbox.height + 2;

	extract_object(image, 0, 1.0, &rsz_z);

	train_update();

	bbox_out->x = bbox.x;
	bbox_out->y = bbox.y;
	bbox_out->width = bbox.width;
	bbox_out->height = bbox.height;
  }

  return peak_value;
}


