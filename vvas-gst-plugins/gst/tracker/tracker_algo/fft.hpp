/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                          License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000-2008, Intel Corporation, all rights reserved.
// Copyright (C) 2009, Willow Garage Inc., all rights reserved.
// Copyright (C) 2013, OpenCV Foundation, all rights reserved.
// Copyright (C) 2021 Xilinx, Inc.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

/* 
 * Part of this file source code is taken from 
 * https://github.com/opencv/opencv/blob/master/modules/core/include/opencv2/core/types.hpp 
 */

#ifndef _FFT_HPP_
#define _FFT_HPP_

#include <string.h>
#include "define.hpp"

#ifdef XLNX_EMBEDDED_PLATFORM
#include "NE10.h"
//#include "fft_neon.hpp"
#include<arm_neon.h>
#endif

typedef unsigned char uchar;
typedef unsigned uint;

#define TRK_SWAP(a,b,t) ((t) = (a), (a) = (b), (b) = (t))

#ifndef MIN
#  define MIN(a,b)  ((a) > (b) ? (b) : (a))
#endif

template<typename _Tp> class Complex
{
public:

  //! default constructor
  Complex();
  Complex(_Tp _re, _Tp _im = 0);

  //! conversion to another data type
  template<typename T2> operator Complex<T2>() const;
  //! conjugation
  //Complex conj() const;

  _Tp re, im; //< the real and the imaginary parts
};

typedef Complex<float> Complexf;
typedef Complex<double> Complexd;

template<typename _Tp> inline
Complex<_Tp>::Complex()
  : re(0), im(0) {}

template<typename _Tp> inline
Complex<_Tp>::Complex(_Tp _re, _Tp _im)
  : re(_re), im(_im) {}

template<typename _Tp> template<typename T2> inline
Complex<_Tp>::operator Complex<T2>() const
{
  //return Complex<T2>(saturate_cast<T2>(re), saturate_cast<T2>(im));
  return Complex<T2>(re, im);
}

template<typename T> struct dft_vecr4
{
  int operator()(Complex<T>*, int, int, int&, const Complex<T>*) const { return 1; }
};

struct dft_options;

typedef void(*dft_funcs)(const dft_options & c, const void* src, void* dst);

struct dft_options {
  int nf;
  int *factors;
  double scale;

  int* itab;
  void* wave;
  int tab_size;
  int n;

  bool isInverse;
  bool noPermute;
  bool isComplex;

  bool haveSSE3;

  dft_funcs dft_func;

  dft_options()
  {
	nf = 0;
	factors = 0;
	scale = 0;
	itab = 0;
	wave = 0;
	tab_size = 0;
	n = 0;
	isInverse = false;
	noPermute = false;
	isComplex = false;

	dft_func = 0;
  }
};

class dft_basic_impl
{
public:
  dft_options opt;
  int _factors[34];
  uchar wave_buf[1024];
  int itab_buf[256];

  dft_basic_impl() {
	opt.factors = _factors;
  }

  void init(int len, int count, bool flags, int stage, bool *needBuffer);
  void apply(const uchar *src, uchar *dst);
  void free() {}
};


class dft_impl
{
protected:
  dft_basic_impl *contextA = new dft_basic_impl();
  dft_basic_impl *contextB = new dft_basic_impl();
  bool needBufferA;
  bool needBufferB;
  bool inv;
  int width;
  int height;
  int elem_size;
  int complex_elem_size;
  int src_channels;
  int dst_channels;

  uchar tmp_bufA[1024];
  uchar tmp_bufB[1024];
  uchar buf0[1024];
  uchar buf1[1024];

public:
  dft_impl()
  {
	needBufferA = false;
	needBufferB = false;
	inv = false;
	width = 0;
	height = 0;
	elem_size = 0;
	complex_elem_size = 0;
	src_channels = 0;
	dst_channels = 0;
  }

  void init(int _width, int _height, int _src_channels, int _dst_channels, bool flags);
  void deinit();
  void apply(const uchar * src, size_t src_step, uchar * dst, size_t dst_step);
  void row_dft(const uchar* src_data, size_t src_step, uchar* dst_data, size_t dst_step, bool isComplex, bool isLastStage);
  void col_dft(const uchar* src_data, size_t src_step, uchar* dst_data, size_t dst_step, int stage_src_channels, int stage_dst_channels, bool isLastStage);
#ifdef XLNX_EMBEDDED_PLATFORM
  void apply_neon(const uchar * src, size_t src_step, uchar * dst, size_t dst_step, ne10_fft_cfg_float32_t cfg);
  void col_dft_neon(const uchar* src_data, size_t src_step, uchar* dst_data, size_t dst_step, int stage_src_channels, int stage_dst_channels, bool isLastStage, ne10_fft_cfg_float32_t cfg);
#endif
};

void dft(fMat _src0, int src_ch, fMat _dst, int dst_ch, bool flags);
void magnitude(fMat img, fMat *mag);
void complexMultiplication(fMat a, fMat b, fMat res);
void complexDivision(fMat a, fMat b, fMat *res);
void rearrange(fMat *img);
void normalizedLogTransform(fMat *img);
void mulSpectrums(fMat _srcA, fMat _srcB, int src_ch, fMat _dst, int flags, bool conjB);

#ifdef XLNX_EMBEDDED_PLATFORM
void dft_neon(fMat _src0, int src_ch, fMat _dst, int dst_ch, bool flags, ne10_fft_cfg_float32_t cfg);
void arm_cmplx_conj_f32(const float32_t * pSrc, float32_t * pDst, uint32_t numSamples);
void arm_cmplx_mult_cmplx_f32(const float32_t * pSrcA, const float32_t * pSrcB, float32_t * pDst, uint32_t numSamples);
void arm_mult_f32(const float32_t * pSrcA, const float32_t * pSrcB, float32_t * pDst, uint32_t blockSize);
void arm_mult_sm_f32(fMat *pMatA, const fMat pMatB);
void arm_cmplx_mult_cmplx_flt_resp_f32(const fMat pMatF, const fMat pMatG,
  fMat *pMatA, fMat *pMatB, fMat *pMatH);
void arm_cmplx_mult_cmplx_flt_resp_upate_f32(const fMat pMatF, const fMat pMatG,
  fMat *pMatA, fMat *pMatB, fMat *pMatH, float32_t lrn_rate);
void arm_cmplx_mag_f32(const float32_t * pSrc, float32_t * pDst, uint32_t numSamples);
#endif

void get_subwindow(Mat_img frame, Rect extracted_roi, Mat_img *z);

#endif /* _FFT_HPP_ */