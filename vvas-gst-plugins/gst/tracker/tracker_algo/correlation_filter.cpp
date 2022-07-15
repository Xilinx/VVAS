/*
Tracker based on Kernelized Correlation Filter (KCF) [1] and Circulant Structure with Kernels (CSK) [2].
CSK is implemented by using raw gray level features, since it is a single-channel filter.
KCF is implemented by using HOG features (the default), since it extends CSK to multiple channels.
[1] J. F. Henriques, R. Caseiro, P. Martins, J. Batista,
"High-Speed Tracking with Kernelized Correlation Filters", TPAMI 2015.
[2] J. F. Henriques, R. Caseiro, P. Martins, J. Batista,
"Exploiting the Circulant Structure of Tracking-by-detection with Kernels", ECCV 2012.
Authors: Joao Faro, Christian Bailer, Joao F. Henriques
Contacts: joaopfaro@gmail.com, Christian.Bailer@dfki.de, henriques@isr.uc.pt
Institute of Systems and Robotics - University of Coimbra / Department Augmented Vision DFKI
Copyright (c) 2021 Xilinx, Inc.
By downloading, copying, installing or using the software you agree to this license.
If you do not agree to this license, do not download, install,
copy or use the software.
                          License Agreement
                       (3-clause BSD License)
Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
    
  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
    
  * Neither the names of the copyright holders nor the names of the contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.
    
This software is provided by the copyright holders and contributors "as is" and
any express or implied warranties, including, but not limited to, the implied
warranties of merchantability and fitness for a particular purpose are disclaimed.
In no event shall copyright holders or contributors be liable for any direct,
indirect, incidental, special, exemplary, or consequential damages
(including, but not limited to, procurement of substitute goods or services;
loss of use, data, or profits; or business interruption) however caused
and on any theory of liability, whether in contract, strict liability,
or tort (including negligence or otherwise) arising in any way out of
the use of this software, even if advised of the possibility of such damage.
 */

/* 
 * Part of this file source code is taken from 
 * https://github.com/joaofaro/KCFcpp/blob/master/src/kcftracker.cpp
 */
 
#include "correlation_filter.hpp"
#include "fft.hpp"
#include <array>

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#ifdef XLNX_EMBEDDED_PLATFORM
#include <arm_neon.h>
#include "Simd/SimdLib.hpp"
#include "NE10.h"
//#include "fft_neon.hpp"
#endif

//Gaussian weigt map is created based on size of object.
void correlation_filter::est_gaussian_wts(int rows, int cols)
{
  float *fptr;
  int i_pos, j_pos;
  comp1.width = cols;
  comp1.height = rows;

  int cy = (rows) / 2;
  int cx = (cols) / 2;

  float sigma = std::sqrt((float)cols * rows) / padding_scale * CORR_GUS_SIGMA;
  float mult = -0.5 / (sigma * sigma);
  fptr = comp1.data;
  for (int i = 0; i < rows; i++)
	for (int j = 0; j < cols; j++) {
	  i_pos = i - cy;
	  j_pos = j - cx;
	  *fptr = std::exp(mult * (float)(i_pos * i_pos + j_pos * j_pos));
	  fptr++;
	}

  rdata.width = comp1.width * 2;
  rdata.height = comp1.height;
#ifdef XLNX_EMBEDDED_PLATFORM
  ne10_fft_cfg_float32_t cfg;
  cfg = ne10_fft_alloc_c2c_float32_neon(comp1.height);
  dft_neon(comp1, 1, gau_wt, 2, false, cfg);
  free(cfg);
#endif

#ifdef XLNX_PCIe_PLATFORM
  dft(comp1, 1, gau_wt, 2, false);
#endif

}

// create Hanning mat for data smoothing. Created first time for each object
void correlation_filter::hanning_weights() {
  float *w_x, *w_y;
  float *fptr;

  w_x = comp1.data;
  w_y = comp2.data;

  for (int i = 0; i < fet_vec_size[1]; i++)
	w_x[i] = 0.5 * (1 - std::cos(2 * PI * i / (fet_vec_size[1] - 1)));
  for (int i = 0; i < fet_vec_size[0]; i++)
	w_y[i] = 0.5 * (1 - std::cos(2 * PI * i / (fet_vec_size[0] - 1)));

  fptr = hann_wts.data;
  for (int i = 0;i < fet_vec_size[0];i++) {
	for (int j = 0;j < fet_vec_size[1];j++) {
	  *fptr = w_y[i] * w_x[j];
	  fptr++;
	}
  }
}

// For feature extraction from roi. HoG or pixel based feature are extracted
void correlation_filter::extract_feature(Mat_img frame, fMat *FeaturesMap, bool first, float scale)
{
  Rect new_roi;
  short fet_len_x, fet_len_y;

  float cx = bbox.x + bbox.width / 2;
  float cy = bbox.y + bbox.height / 2;

  if (first) {
	int new_width = bbox.width * padding_scale;
	int new_height = bbox.height * padding_scale;
	int tlen;

	if (tmpl_sz > 1) {
	  if (cell_size == 4) {
		if (new_width >= new_height) {
		  scale_factor = new_width / (float)tmpl_sz;
		  obj_size.width = tmpl_sz;

		  obj_size.height = new_height / scale_factor;
		  tlen = obj_size.height / cell_size;
		  if (tlen < 8)
			tlen = 8;
		  else {
			if (tlen & 1) {
			  tlen += 1;
			  if (tlen == 14)
				tlen = 12;
			  else if (tlen == 18)
				tlen = 16;
			  else if (tlen == 22)
				tlen = 20;
			}
			else {
			  if (tlen == 14)
				tlen = 16;
			  else if (tlen == 18)
				tlen = 16;
			  else if (tlen == 22)
				tlen = 24;
			}
		  }
		  obj_size.height = tlen * cell_size;
		}
		else {
		  scale_factor = new_height / (float)tmpl_sz;
		  obj_size.height = tmpl_sz;

		  obj_size.width = new_width / scale_factor;
		  tlen = obj_size.width / cell_size;
		  if (tlen < 8)
			tlen = 8;
		  else {
			if (tlen & 1) {
			  tlen += 1;
			  if (tlen == 14)
				tlen = 12;
			  else if (tlen == 18)
				tlen = 16;
			  else if (tlen == 22)
				tlen = 20;
			}
			else {
			  if (tlen == 14)
				tlen = 16;
			  else if (tlen == 18)
				tlen = 16;
			  else if (tlen == 22)
				tlen = 24;
			}
		  }
		  obj_size.width = tlen * cell_size;
		}
	  }

	  else if (cell_size == 8) {
		if (new_width >= new_height) {
		  scale_factor = new_width / (float)tmpl_sz;
		  obj_size.width = tmpl_sz;

		  obj_size.height = new_height / scale_factor;
		  tlen = obj_size.height / cell_size;
		  if (tlen < 8)
			tlen = 8;
		  else if (tlen & 1)
			tlen += 1;
		  obj_size.height = tlen * cell_size;
		}
		else {
		  scale_factor = new_height / (float)tmpl_sz;
		  obj_size.height = tmpl_sz;
		  obj_size.width = new_width / scale_factor;
		  tlen = obj_size.width / cell_size;
		  if (tlen < 8)
			tlen = 8;
		  else if (tlen & 1)
			tlen += 1;
		  obj_size.width = tlen * cell_size;
		}
	  }
	}

	else {
	  obj_size.width = new_width;
	  obj_size.height = new_width;
	  scale_factor = 1;
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
	rsz_z.width = obj_size.width;
	rsz_z.height = obj_size.height;

#ifdef XLNX_EMBEDDED_PLATFORM
	SimdResizeBilinear(z.data, z.width, z.height, z.width, rsz_z.data, rsz_z.width, rsz_z.height, rsz_z.width, 1);
#else
	ResizeBilinear(z.data, z.width, z.height, z.width, rsz_z.data, rsz_z.width, rsz_z.height, rsz_z.width, 1);
#endif
	//resize(&z, &rsz_z);
  }

  else
	rsz_z = z;

  if (use_hog) { // HOG features
	fet_len_x = rsz_z.width / cell_size;
	fet_len_y = rsz_z.height / cell_size;

#ifdef XLNX_EMBEDDED_PLATFORM
	SimdHogExtractFeatures(rsz_z.data, rsz_z.width, rsz_z.width, rsz_z.height, hog_feat, cell_size, fet_length);
#else
	HogExtractFeatures(rsz_z.data, rsz_z.width, rsz_z.width, rsz_z.height, hog_feat, cell_size, fet_length);
#endif

	fet_vec_size[0] = fet_len_x;
	fet_vec_size[1] = fet_len_y;
	fet_vec_size[2] = fet_length;

	FeaturesMap->width = fet_len_x * fet_len_y;
	FeaturesMap->height = fet_length;

#ifdef XLNX_EMBEDDED_PLATFORM
	float *fptr = tMat.data;
#endif

#ifdef XLNX_PCIe_PLATFORM
	float *fptr = FeaturesMap->data;
#endif
	for (int i = 0; i < FeaturesMap->height; i++)
	  for (int j = 0; j < FeaturesMap->width; j++) {
		*fptr = hog_feat[j*FeaturesMap->height + i];
		fptr++;
	  }
  }

  else {
	fet_vec_size[0] = z.height;
	fet_vec_size[1] = z.width;
	fet_vec_size[2] = 1;
  }

  if (first) {
	hanning_weights();
  }

#ifdef XLNX_EMBEDDED_PLATFORM
  float *fout = FeaturesMap->data;
  float *fptr = tMat.data;
  float *fptr1;
  for (int i = 0; i < FeaturesMap->height; i++) {
	fptr1 = hann_wts.data;
	arm_mult_f32(fptr, fptr1, fout, FeaturesMap->width);
	fptr += FeaturesMap->width;
	fout += FeaturesMap->width;
  }
#endif

#ifdef XLNX_PCIe_PLATFORM
  float *fptr = FeaturesMap->data;
  float *fptr1;
  for (int i = 0; i < FeaturesMap->height; i++) {
	fptr1 = hann_wts.data;
	for (int j = 0; j < FeaturesMap->width; j++) {
	  *fptr *= *fptr1;
	  fptr++;
	  fptr1++;
	}
  }
#endif
}

// online training of object based on extracted features
void correlation_filter::train_features(fMat x, float lrn_rate) {
  correlation_filtering(x, x, 0, &rdata, true);

  fMat kernel_corr;
  kernel_corr.width = rdata.width * 2;
  kernel_corr.height = rdata.height;
  kernel_corr.data = comp1.data;
  dft(rdata, 1, kernel_corr, 2, false);
  for (int i = 0; i < kernel_corr.width *kernel_corr.height; i += 2)
	kernel_corr.data[i] += CORR_LAMBDA;
  fMat alpha_new;
  alpha_new.width = kernel_corr.width;
  alpha_new.height = kernel_corr.height;
  alpha_new.data = comp2.data;

  complexDivision(gau_wt, kernel_corr, &alpha_new);

  for (int i = 0; i < x.width *x.height; i++)
	obj_tmpl.data[i] = (1 - lrn_rate) * obj_tmpl.data[i] + (lrn_rate)* x.data[i];

  for (int i = 0; i < alphaf.width *alphaf.height; i++)
	alphaf.data[i] = (1 - lrn_rate) * alphaf.data[i] + (lrn_rate)* alpha_new.data[i];
}

//Estimate correlation between two rois.
void correlation_filter::correlation_filtering(fMat x1, fMat x2, float sum_sqr, fMat *res, bool auto_corr) {
  fMat tmp_x1, corr, tmp_corr, acc_corr;
  fMat tx1, tx2;
  int size, size2;

  size = fet_vec_size[1] * fet_vec_size[0];
  size2 = size << 1;

  corr.width = fet_vec_size[1];
  corr.height = fet_vec_size[0];
  corr.data = res->data;

  tmp_x1.width = fet_vec_size[1];
  tmp_x1.height = fet_vec_size[0];

  tmp_corr.width = fet_vec_size[1] * 2;
  tmp_corr.height = fet_vec_size[0];
  tmp_corr.data = comp1.data;

  acc_corr.width = fet_vec_size[1] * 2;
  acc_corr.height = fet_vec_size[0];
  acc_corr.data = comp2.data;

  tx1.width = fet_vec_size[1] * 2;
  tx1.height = fet_vec_size[0];
  tx1.data = comp3.data;

  tx2.width = fet_vec_size[1] * 2;
  tx2.height = fet_vec_size[0];

  if (use_hog) { // HOG features
	memset(acc_corr.data, 0, sizeof(float) * size2);
	dft_impl *impl = new dft_impl();
	impl->init(fet_vec_size[1], fet_vec_size[0], 1, 2, false);

	int src_step = fet_vec_size[1] * sizeof(float);
	int dst_step = fet_vec_size[1] * 2 * sizeof(float);
#ifdef XLNX_EMBEDDED_PLATFORM
	ne10_fft_cfg_float32_t cfg;
	cfg = ne10_fft_alloc_c2c_float32_neon(tmp_x1.height);
#endif
	for (int i = 0; i < fet_vec_size[2]; i++) {
	  tmp_x1.data = x1.data + i * size;

#ifdef XLNX_EMBEDDED_PLATFORM
	  impl->apply_neon((uchar*)tmp_x1.data, src_step, (uchar*)tx1.data, dst_step, cfg);
#endif

#ifdef XLNX_PCIe_PLATFORM
	  impl->apply((uchar*)tmp_x1.data, src_step, (uchar*)tx1.data, dst_step);
#endif

	  //PRE_FFT
	  if (!auto_corr)
		tx2.data = x2.data + i * size2;

	  if (auto_corr) {
#ifdef XLNX_PCIe_PLATFORM
		mulSpectrums(tx1, tx1, 2, tmp_corr, 0, true);
#endif

#ifdef XLNX_EMBEDDED_PLATFORM
		arm_cmplx_conj_f32(tx1.data, comp4.data, tx1.width*tx1.height / 2);
		arm_cmplx_mult_cmplx_f32(tx1.data, comp4.data, tmp_corr.data, tx1.width * tx1.height / 2);
#endif
	  }
	  else {
#ifdef XLNX_PCIe_PLATFORM
		mulSpectrums(tx1, tx2, 2, tmp_corr, 0, true);
#endif

#ifdef XLNX_EMBEDDED_PLATFORM
		arm_cmplx_mult_cmplx_f32(tx1.data, tx2.data, tmp_corr.data, tx1.width * tx1.height / 2);
#endif
	  }

#ifdef XLNX_PCIe_PLATFORM
	  for (int j = 0; j < size2; j++)
		acc_corr.data[j] += tmp_corr.data[j];
#endif

#ifdef XLNX_EMBEDDED_PLATFORM
	  float32x4_t A;
	  float32x4_t B;
	  float32x4_t C;

	  int k = size2 % 4;

	  for (int j = 0;j < size2 / 4;j++) {
		A = vld1q_f32(acc_corr.data + (4 * j));
		B = vld1q_f32(tmp_corr.data + (4 * j));
		C = vaddq_f32(A, B);
		vst1q_f32(acc_corr.data + (4 * j), C);
	  }

	  if (k != 0) {
		for (int j = ((size2 / 4) * 4); j < size2; j++)
		  acc_corr.data[j] += tmp_corr.data[j];
	  }
#endif
	}

	impl->deinit();
	delete impl;

#ifdef XLNX_EMBEDDED_PLATFORM
	free(cfg);
#endif

	dft(acc_corr, 2, tmp_corr, 2, true);

#ifdef XLNX_PCIe_PLATFORM
	magnitude(tmp_corr, &corr);
#endif

#ifdef XLNX_EMBEDDED_PLATFORM
	arm_cmplx_mag_f32(tmp_corr.data, corr.data, tmp_corr.width * tmp_corr.height / 2);
#endif

	rearrange(&corr);
  }
  // Gray features
  else {
	dft(tmp_x1, 1, tx1, 2, false);

	//PRE_FFT
	if (!auto_corr)
	  tx2.data = x2.data;
	if (auto_corr)
	  mulSpectrums(tx1, tx1, 2, tmp_corr, 0, true);
	else
	  mulSpectrums(tx1, tx2, 2, tmp_corr, 0, true);

	dft(tmp_corr, 2, acc_corr, 2, true);
	magnitude(acc_corr, &corr);
	rearrange(&corr);
  }

  float fsum = 0;

#ifdef XLNX_PCIe_PLATFORM
  for (int i = 0; i < x1.width * x1.height; i++)
	fsum += (x1.data[i] * x1.data[i]);
#endif

#ifdef XLNX_EMBEDDED_PLATFORM
  float32x4_t A;
  float32x4_t C;
  C = vmovq_n_f32(0);

  int k = (x1.width * x1.height) >> 2U;
  float *srcA = x1.data;

  while (k--) {
	A = vld1q_f32(srcA);
	C = vmlaq_f32(C, A, A); //C = C+ (A*A)
	srcA += 4;
  }

  for (int i = 0; i < 4; i++)
	fsum += C[i];

  k = (x1.width * x1.height) & 0x3;

  while (k--) {
	fsum += (*srcA * *srcA);
	srcA++;
  }
#endif

  if (auto_corr) {
	fsum *= 2;
  }
  else {
	fsum += sum_sqr;
  }

  float val;
  res->width = corr.width;
  res->height = corr.height;
  for (int i = 0; i < corr.width * corr.height; i++) {
	val = fsum - 2.0 * corr.data[i];
	corr.data[i] = exp(-val * norm_factor);
  }
}

// Detect object in the current frame.
Point2f correlation_filter::locate_new_position(fMat z, fMat x, float sum_sqr, float &peak_value) {
  fMat kernel_corr, cong, response, tmp;

  correlation_filtering(x, z, sum_sqr, &rdata, false);

  short width = rdata.width;

  kernel_corr.width = width * 2;
  kernel_corr.height = rdata.height;
  kernel_corr.data = comp1.data;
  dft(rdata, 1, kernel_corr, 2, false);

  cong.width = kernel_corr.width;
  cong.height = kernel_corr.height;
  cong.data = comp2.data;
  tmp.width = alphaf.width;
  tmp.height = alphaf.height;
  tmp.data = alphaf.data;
  complexMultiplication(tmp, kernel_corr, cong);

  response.width = kernel_corr.width;
  response.height = kernel_corr.height;
  response.data = comp3.data;
  dft(cong, 2, response, 2, true);

  int pix, piy;
  float pival;
  Point2f p;
  pival = response.data[0];
  pix = 0;

  for (int i = 2; i < response.height*response.width; i += 2)
	if (response.data[i] > pival) {
	  pival = response.data[i];
	  pix = i;
	}
  pix >>= 1;
  peak_value = pival;
  piy = pix / width;
  pix = pix % width;

  p.x = (float)pix;
  p.y = (float)piy;

  /*if (pix > 0 && pix < width - 1) {
	p.x += sub_pix_estimation(response.data[(piy * width + pix - 1)<<1], peak_value, response.data[(piy*width + pix + 1)<<1]);
  }

  if (piy > 0 && piy < response.height - 1) {
	p.y += sub_pix_estimation(response.data[((piy - 1)*width + pix)<<1], peak_value, response.data[((piy + 1)*width + pix)<<1]);
  }*/

  p.x -= (width) / 2;
  p.y -= (response.height) / 2;

  return p;
}

// finding sub_pixel accuracy
float correlation_filter::sub_pix_estimation(float pre, float cur, float nxt) {
  float den = 2 * cur - nxt - pre;

  if (den)
	return 0.5 * (nxt - pre) / den;
  else
	return 0;
}

// tracker initalization 
void correlation_filter::init_cf(track_config tconfig, Rectf bbox_new, Mat_img frame)
{
  int size;
  int fft_size;
  int roi_size;
  int hog_size;

  padding_scale = tconfig.padding;

  if (tconfig.hog_feature) {
	lrn_rate = CORR_HOG_LRN_RATE;
	float sigma = CORR_GUS_KRNL_SIGMA;
	inv_sigma2 = 1.0 / (sigma * sigma);

	cell_size = CORR_CELL_SIZE;
	use_hog = true;
	fet_length = tconfig.fet_length;
  }
  else {
	lrn_rate = CORR_PIX_LRN_RATE;
	cell_size = 1;
	use_hog = false;
  }

  if (tconfig.multiscale) { // multiscale
	scale_type = tconfig.multiscale;
	tmpl_sz = CORR_HOG_TMPL_SZ;
	scaling = CORR_SCALING_FACT;
	if (!tconfig.fixed_window) {
	  tconfig.fixed_window = true;
	}
  }
  else if (tconfig.fixed_window) {
	tmpl_sz = CORR_HOG_TMPL_SZ;
	scaling = 1;
  }
  else {
	tmpl_sz = 1;
	scaling = 1;
  }

  //Assuming template is of 96 (104 based on cell)
  //fft size of 24x24
  //roi_size + resize + fft_size*31 + fft_size+ 6 * fft_size * 2 (complex)
  //roi_size + 104*104 + 4 * (24*24*31*2 + 6 * 24 * 48 + 24 * 24 * 2)
  roi_size = (frame.width * frame.height) >> 0;
  size = roi_size + 205504;
  //26*26*31
  hog_size = 83824;
  size += hog_size;

  //PRE_FFT
  size += 167648;//71424;

  fft_size = 1352;//26 * 26 * 2
  head_ptr = calloc(size, 1);
  z.data = (unsigned char *)head_ptr;
  rsz_z.data = z.data + roi_size;
  obj_tmpl.data = (float *)(rsz_z.data + 10816);//104*104
  cur_tmpl.data = obj_tmpl.data + 17856;//24*24*31
  gau_wt.data = cur_tmpl.data + 17856;
  alphaf.data = gau_wt.data + fft_size;//24*24*2
  comp1.data = alphaf.data + fft_size;
  comp2.data = comp1.data + fft_size;
  comp3.data = comp2.data + fft_size;
  comp4.data = comp3.data + fft_size;
  rdata.data = comp4.data + fft_size;
  hann_wts.data = rdata.data + 576;
  hog_feat = (float *)(hann_wts.data + 576);

  tMat.height = 26;
  tMat.width = 1612;
  tMat.data = (float *)malloc(tMat.width * tMat.height * sizeof(float));

  //PRE_FFT
  prev_fft.data = (float *)(hog_feat + (hog_size >> 2));

  bbox = bbox_new;

  extract_feature(frame, &obj_tmpl, 1, 1.0);
  gau_wt.width = fet_vec_size[1] * 2;
  gau_wt.height = fet_vec_size[0];
  est_gaussian_wts(fet_vec_size[0], fet_vec_size[1]);
  alphaf.width = fet_vec_size[1] * 2;
  alphaf.height = fet_vec_size[0];

  norm_factor = (1.0 / (fet_vec_size[0] * fet_vec_size[1] * fet_vec_size[2])) * inv_sigma2;

  train_features(obj_tmpl, 1.0); // train with initial frame
}

// Initialize tracker 
void correlation_filter::deinit_cf() {
  if (head_ptr != NULL) {
	free(head_ptr);
	head_ptr = NULL;
	free(tMat.data);
  }
}

// Update position based on the new frame
void correlation_filter::detect_update(Rectf bbox_new, Mat_img image) {
  bbox = bbox_new;

  extract_feature(image, &obj_tmpl, 1, 1.0);
  gau_wt.width = fet_vec_size[1] * 2;
  gau_wt.height = fet_vec_size[0];
  est_gaussian_wts(fet_vec_size[0], fet_vec_size[1]);
  alphaf.width = fet_vec_size[1] * 2;
  alphaf.height = fet_vec_size[0];

  norm_factor = (1.0 / (fet_vec_size[0] * fet_vec_size[1] * fet_vec_size[2])) * inv_sigma2;

  train_features(obj_tmpl, 1.0); // train with initial frame
}

// Update position based on the new frame
float correlation_filter::update_position(Mat_img image, Rectf *bbox_out, float conf_score)
{
  if (bbox.x + bbox.width <= 0) bbox.x = -bbox.width + 1;
  if (bbox.y + bbox.height <= 0) bbox.y = -bbox.height + 1;
  if (bbox.x >= image.width - 1) bbox.x = image.width - 2;
  if (bbox.y >= image.height - 1) bbox.y = image.height - 2;

  float cx = bbox.x + bbox.width / 2.0f;
  float cy = bbox.y + bbox.height / 2.0f;
  float peak_value;

  extract_feature(image, &cur_tmpl, 0, 1.0);

  float fsum = 0;
  for (int i = 0; i < obj_tmpl.width * obj_tmpl.height; i++)
	fsum += (obj_tmpl.data[i] * obj_tmpl.data[i]);

  //PRE_FFT
  fMat x1aux, tx1;
  prev_fft.width = obj_tmpl.width * 2;
  prev_fft.height = obj_tmpl.height;
  dft_impl *impl = new dft_impl();
  impl->init(fet_vec_size[1], fet_vec_size[0], 1, 2, false);
  int src_step = fet_vec_size[1] * sizeof(float);
  int dst_step = fet_vec_size[1] * 2 * sizeof(float);
  x1aux.width = fet_vec_size[1];
  x1aux.height = fet_vec_size[0];
  tx1.width = fet_vec_size[1] * 2;
  tx1.height = fet_vec_size[0];
#ifdef XLNX_EMBEDDED_PLATFORM
  ne10_fft_cfg_float32_t cfg;
  cfg = ne10_fft_alloc_c2c_float32_neon(x1aux.height);
#endif
  for (int i = 0; i < fet_vec_size[2]; i++) {
	x1aux.data = obj_tmpl.data + i * fet_vec_size[0] * fet_vec_size[1];
#ifdef XLNX_PCIe_PLATFORM
	tx1.data = prev_fft.data + i * fet_vec_size[0] * fet_vec_size[1] * 2;
	impl->apply((uchar*)x1aux.data, src_step, (uchar*)tx1.data, dst_step);
#endif
#ifdef XLNX_EMBEDDED_PLATFORM
	tx1.data = tMat.data + i * fet_vec_size[0] * fet_vec_size[1] * 2;
	impl->apply_neon((uchar*)x1aux.data, src_step, (uchar*)tx1.data, dst_step, cfg);
#endif
  }

  impl->deinit();
  delete impl;

#ifdef XLNX_EMBEDDED_PLATFORM
  arm_cmplx_conj_f32(tMat.data, prev_fft.data, fet_vec_size[1] * fet_vec_size[0] * fet_vec_size[2]);
  free(cfg);
#endif
  Point2f res = locate_new_position(prev_fft, cur_tmpl, fsum, peak_value);

  if (scaling != 1) {
	// locating at lower scale factor
	float new_peak_value;
	if (scale_type == 1 || scale_type == 3) {
	  extract_feature(image, &cur_tmpl, 0, 1.0f / scaling);
	  //PRE_FFT
	  Point2f new_res = locate_new_position(prev_fft, cur_tmpl, fsum, new_peak_value);

	  if (CORR_SCALE_WT * new_peak_value > peak_value) {
		res = new_res;
		peak_value = new_peak_value;
		scale_factor /= scaling;
		bbox.width /= scaling;
		bbox.height /= scaling;
	  }
	}

	// locating at higher scale factor
	if (scale_type == 1 || scale_type == 2) {
	  extract_feature(image, &cur_tmpl, 0, scaling);
	  //PRE_FFT
	  Point2f new_res = locate_new_position(prev_fft, cur_tmpl, fsum, new_peak_value);

	  if (CORR_SCALE_WT * new_peak_value > peak_value) {
		res = new_res;
		peak_value = new_peak_value;
		scale_factor *= scaling;
		bbox.width *= scaling;
		bbox.height *= scaling;
	  }
	}
  }

  if (peak_value >= conf_score) {
	bbox.x = cx - bbox.width / 2.0f + ((float)res.x * cell_size * scale_factor);
	bbox.y = cy - bbox.height / 2.0f + ((float)res.y * cell_size * scale_factor);

	if (bbox.x >= image.width - 1) bbox.x = image.width - 1;
	if (bbox.y >= image.height - 1) bbox.y = image.height - 1;
	if (bbox.x + bbox.width <= 0) bbox.x = -bbox.width + 2;
	if (bbox.y + bbox.height <= 0) bbox.y = -bbox.height + 2;

	extract_feature(image, &cur_tmpl, 0, 1.0);

	train_features(cur_tmpl, lrn_rate);

	bbox_out->x = bbox.x;
	bbox_out->y = bbox.y;
	bbox_out->width = bbox.width;
	bbox_out->height = bbox.height;
  }

  return peak_value;
}


