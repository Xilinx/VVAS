/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
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
//   * The name of Intel Corporation may not be used to endorse or promote products
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


/* Part of this file source code is taken from 
 * https://github.com/opencv/opencv/blob/master/modules/core/src/dxt.cpp 
 */


#include "fft.hpp"

static unsigned char bitrevTab[] =
{
  0x00,0x80,0x40,0xc0,0x20,0xa0,0x60,0xe0,0x10,0x90,0x50,0xd0,0x30,0xb0,0x70,0xf0,
  0x08,0x88,0x48,0xc8,0x28,0xa8,0x68,0xe8,0x18,0x98,0x58,0xd8,0x38,0xb8,0x78,0xf8,
  0x04,0x84,0x44,0xc4,0x24,0xa4,0x64,0xe4,0x14,0x94,0x54,0xd4,0x34,0xb4,0x74,0xf4,
  0x0c,0x8c,0x4c,0xcc,0x2c,0xac,0x6c,0xec,0x1c,0x9c,0x5c,0xdc,0x3c,0xbc,0x7c,0xfc,
  0x02,0x82,0x42,0xc2,0x22,0xa2,0x62,0xe2,0x12,0x92,0x52,0xd2,0x32,0xb2,0x72,0xf2,
  0x0a,0x8a,0x4a,0xca,0x2a,0xaa,0x6a,0xea,0x1a,0x9a,0x5a,0xda,0x3a,0xba,0x7a,0xfa,
  0x06,0x86,0x46,0xc6,0x26,0xa6,0x66,0xe6,0x16,0x96,0x56,0xd6,0x36,0xb6,0x76,0xf6,
  0x0e,0x8e,0x4e,0xce,0x2e,0xae,0x6e,0xee,0x1e,0x9e,0x5e,0xde,0x3e,0xbe,0x7e,0xfe,
  0x01,0x81,0x41,0xc1,0x21,0xa1,0x61,0xe1,0x11,0x91,0x51,0xd1,0x31,0xb1,0x71,0xf1,
  0x09,0x89,0x49,0xc9,0x29,0xa9,0x69,0xe9,0x19,0x99,0x59,0xd9,0x39,0xb9,0x79,0xf9,
  0x05,0x85,0x45,0xc5,0x25,0xa5,0x65,0xe5,0x15,0x95,0x55,0xd5,0x35,0xb5,0x75,0xf5,
  0x0d,0x8d,0x4d,0xcd,0x2d,0xad,0x6d,0xed,0x1d,0x9d,0x5d,0xdd,0x3d,0xbd,0x7d,0xfd,
  0x03,0x83,0x43,0xc3,0x23,0xa3,0x63,0xe3,0x13,0x93,0x53,0xd3,0x33,0xb3,0x73,0xf3,
  0x0b,0x8b,0x4b,0xcb,0x2b,0xab,0x6b,0xeb,0x1b,0x9b,0x5b,0xdb,0x3b,0xbb,0x7b,0xfb,
  0x07,0x87,0x47,0xc7,0x27,0xa7,0x67,0xe7,0x17,0x97,0x57,0xd7,0x37,0xb7,0x77,0xf7,
  0x0f,0x8f,0x4f,0xcf,0x2f,0xaf,0x6f,0xef,0x1f,0x9f,0x5f,0xdf,0x3f,0xbf,0x7f,0xff
};

static const double DFTTab[][2] =
{
{ 1.00000000000000000, 0.00000000000000000 },
{-1.00000000000000000, 0.00000000000000000 },
{ 0.00000000000000000, 1.00000000000000000 },
{ 0.70710678118654757, 0.70710678118654746 },
{ 0.92387953251128674, 0.38268343236508978 },
{ 0.98078528040323043, 0.19509032201612825 },
{ 0.99518472667219693, 0.09801714032956060 },
{ 0.99879545620517241, 0.04906767432741802 },
{ 0.99969881869620425, 0.02454122852291229 },
{ 0.99992470183914450, 0.01227153828571993 },
{ 0.99998117528260111, 0.00613588464915448 },
{ 0.99999529380957619, 0.00306795676296598 },
{ 0.99999882345170188, 0.00153398018628477 },
{ 0.99999970586288223, 0.00076699031874270 },
{ 0.99999992646571789, 0.00038349518757140 },
{ 0.99999998161642933, 0.00019174759731070 },
{ 0.99999999540410733, 0.00009587379909598 },
{ 0.99999999885102686, 0.00004793689960307 },
{ 0.99999999971275666, 0.00002396844980842 },
{ 0.99999999992818922, 0.00001198422490507 },
{ 0.99999999998204725, 0.00000599211245264 },
{ 0.99999999999551181, 0.00000299605622633 },
{ 0.99999999999887801, 0.00000149802811317 },
{ 0.99999999999971945, 0.00000074901405658 },
{ 0.99999999999992983, 0.00000037450702829 },
{ 0.99999999999998246, 0.00000018725351415 },
{ 0.99999999999999567, 0.00000009362675707 },
{ 0.99999999999999889, 0.00000004681337854 },
{ 0.99999999999999978, 0.00000002340668927 },
{ 0.99999999999999989, 0.00000001170334463 },
{ 1.00000000000000000, 0.00000000585167232 },
{ 1.00000000000000000, 0.00000000292583616 }
};

static int
dft_factorize(int n, int* factors)
{
  int nf = 0, f, i, j;

  if (n <= 5)
  {
	factors[0] = n;
	return 1;
  }

  f = (((n - 1) ^ n) + 1) >> 1;
  if (f > 1)
  {
	factors[nf++] = f;
	n = f == n ? 1 : n / f;
  }

  for (f = 3; n > 1; )
  {
	int d = n / f;
	if (d*f == n)
	{
	  factors[nf++] = f;
	  n = d;
	}
	else
	{
	  f += 2;
	  if (f*f > n)
		break;
	}
  }

  if (n > 1)
	factors[nf++] = n;

  f = (factors[0] & 1) == 0;
  for (i = f; i < (nf + f) / 2; i++)
	TRK_SWAP(factors[i], factors[nf - i - 1 + f], j);

  return nf;
}

static void
dft_init(int n0, int nf, const int* factors, int* itab, int elem_size, void* _wave, int inv_itab)
{
  int digits[34], radix[34];
  int n = factors[0], m = 0;
  int* itab0 = itab;
  int i, j, k;
  Complex<double> w, w1;
  double t;

  if (n0 <= 5)
  {
	itab[0] = 0;
	itab[n0 - 1] = n0 - 1;

	if (n0 != 4)
	{
	  for (i = 1; i < n0 - 1; i++)
		itab[i] = i;
	}
	else
	{
	  itab[1] = 2;
	  itab[2] = 1;
	}
	if (n0 == 5)
	{
	  if (elem_size == sizeof(Complex<double>))
		((Complex<double>*)_wave)[0] = Complex<double>(1., 0.);
	  else
		((Complex<float>*)_wave)[0] = Complex<float>(1.f, 0.f);
	}
	if (n0 != 4)
	  return;
	m = 2;
  }
  else
  {
	// radix[] is initialized from index 'nf' down to zero
	//assert(nf < 34);
	if (nf >= 34)
	  return;
	radix[nf] = 1;
	digits[nf] = 0;
	for (i = 0; i < nf; i++)
	{
	  digits[i] = 0;
	  radix[nf - i - 1] = radix[nf - i] * factors[nf - i - 1];
	}

	if (inv_itab && factors[0] != factors[nf - 1])
	  itab = (int*)_wave;

	if ((n & 1) == 0)
	{
	  int a = radix[1], na2 = n * a >> 1, na4 = na2 >> 1;
	  for (m = 0; (unsigned)(1 << m) < (unsigned)n; m++);
	  if (n <= 2)
	  {
		itab[0] = 0;
		itab[1] = na2;
	  }
	  else if (n <= 256)
	  {
		int shift = 10 - m;
		for (i = 0; i <= n - 4; i += 4)
		{
		  j = (bitrevTab[i >> 2] >> shift)*a;
		  itab[i] = j;
		  itab[i + 1] = j + na2;
		  itab[i + 2] = j + na4;
		  itab[i + 3] = j + na2 + na4;
		}
	  }

	  digits[1]++;

	  if (nf >= 2)
	  {
		for (i = n, j = radix[2]; i < n0; )
		{
		  for (k = 0; k < n; k++)
			itab[i + k] = itab[k] + j;
		  if ((i += n) >= n0)
			break;
		  j += radix[2];
		  for (k = 1; ++digits[k] >= factors[k]; k++)
		  {
			digits[k] = 0;
			j += radix[k + 2] - radix[k];
		  }
		}
	  }
	}
	else
	{
	  for (i = 0, j = 0;; )
	  {
		itab[i] = j;
		if (++i >= n0)
		  break;
		j += radix[1];
		for (k = 0; ++digits[k] >= factors[k]; k++)
		{
		  digits[k] = 0;
		  j += radix[k + 2] - radix[k];
		}
	  }
	}

	if (itab != itab0)
	{
	  itab0[0] = 0;
	  for (i = n0 & 1; i < n0; i += 2)
	  {
		int k0 = itab[i];
		int k1 = itab[i + 1];
		itab0[k0] = i;
		itab0[k1] = i + 1;
	  }
	}
  }

  if ((n0 & (n0 - 1)) == 0)
  {
	w.re = w1.re = DFTTab[m][0];
	w.im = w1.im = -DFTTab[m][1];
  }
  else
  {
	t = -PI * 2 / n0;
	w.im = w1.im = sin(t);
	w.re = w1.re = std::sqrt(1. - w1.im*w1.im);
  }
  n = (n0 + 1) / 2;

  if (elem_size == sizeof(Complex<double>))
  {
	Complex<double>* wave = (Complex<double>*)_wave;

	wave[0].re = 1.;
	wave[0].im = 0.;

	if ((n0 & 1) == 0)
	{
	  wave[n].re = -1.;
	  wave[n].im = 0;
	}

	for (i = 1; i < n; i++)
	{
	  wave[i] = w;
	  wave[n0 - i].re = w.re;
	  wave[n0 - i].im = -w.im;

	  t = w.re*w1.re - w.im*w1.im;
	  w.im = w.re*w1.im + w.im*w1.re;
	  w.re = t;
	}
  }
  else
  {
	Complex<float>* wave = (Complex<float>*)_wave;
	//assert(elem_size == sizeof(Complex<float>));

	wave[0].re = 1.f;
	wave[0].im = 0.f;

	if ((n0 & 1) == 0)
	{
	  wave[n].re = -1.f;
	  wave[n].im = 0.f;
	}

	for (i = 1; i < n; i++)
	{
	  wave[i].re = (float)w.re;
	  wave[i].im = (float)w.im;
	  wave[n0 - i].re = (float)w.re;
	  wave[n0 - i].im = (float)-w.im;

	  t = w.re*w1.re - w.im*w1.im;
	  w.im = w.re*w1.im + w.im*w1.re;
	  w.re = t;
	}
  }
}

// mixed-radix complex discrete Fourier transform: double-precision version
template<typename T> static void
dft_complex(const dft_options & c, const Complex<T>* src, Complex<T>* dst)
{
  static const T sin_120 = (T)0.86602540378443864676372317075294;
  static const T fft5_2 = (T)0.559016994374947424102293417182819;
  static const T fft5_3 = (T)-0.951056516295153572116439333379382;
  static const T fft5_4 = (T)-1.538841768587626701285145288018455;
  static const T fft5_5 = (T)0.363271264002680442947733378740309;

  const Complex<T>* wave = (Complex<T>*)c.wave;
  const int * itab = c.itab;

  int n = c.n;
  int f_idx, nx;
  int inv = c.isInverse;
  int dw0 = c.tab_size, dw;
  int i, j;
  Complex<T> t;
  T scale = (T)c.scale;

  int tab_step = c.tab_size == n ? 1 : c.tab_size == n * 2 ? 2 : c.tab_size / n;

  // 0. shuffle data
  if (dst != src)
  {
	//assert(!c.noPermute);
	if (!inv)
	{
	  for (i = 0; i <= n - 2; i += 2, itab += 2 * tab_step)
	  {
		int k0 = itab[0], k1 = itab[tab_step];
		dst[i] = src[k0]; dst[i + 1] = src[k1];
	  }

	  if (i < n)
		dst[n - 1] = src[n - 1];
	}
	else
	{
	  for (i = 0; i <= n - 2; i += 2, itab += 2 * tab_step)
	  {
		int k0 = itab[0], k1 = itab[tab_step];
		t.re = src[k0].re; t.im = -src[k0].im;
		dst[i] = t;
		t.re = src[k1].re; t.im = -src[k1].im;
		dst[i + 1] = t;
	  }

	  if (i < n)
	  {
		t.re = src[n - 1].re; t.im = -src[n - 1].im;
		dst[i] = t;
	  }
	}
  }
  else
  {
	if (!c.noPermute)
	{
	  if (c.nf == 1)
	  {
		if ((n & 3) == 0)
		{
		  int n2 = n / 2;
		  Complex<T>* dsth = dst + n2;

		  for (i = 0; i < n2; i += 2, itab += tab_step * 2)
		  {
			j = itab[0];

			TRK_SWAP(dst[i + 1], dsth[j], t);
			if (j > i)
			{
			  TRK_SWAP(dst[i], dst[j], t);
			  TRK_SWAP(dsth[i + 1], dsth[j + 1], t);
			}
		  }
		}
		// else do nothing
	  }
	  else
	  {
		for (i = 0; i < n; i++, itab += tab_step)
		{
		  j = itab[0];

		  if (j > i)
			TRK_SWAP(dst[i], dst[j], t);
		}
	  }
	}

	if (inv)
	{
	  for (i = 0; i <= n - 2; i += 2)
	  {
		T t0 = -dst[i].im;
		T t1 = -dst[i + 1].im;
		dst[i].im = t0; dst[i + 1].im = t1;
	  }

	  if (i < n)
		dst[n - 1].im = -dst[n - 1].im;
	}
  }

  n = 1;
  // 1. power-2 transforms
  if ((c.factors[0] & 1) == 0)
  {
	if (c.factors[0] >= 4 && c.haveSSE3)
	{
	  dft_vecr4<T> vr4;
	  n = vr4(dst, c.factors[0], c.n, dw0, wave);
	}

	// radix-4 transform
	for (; n * 4 <= c.factors[0]; )
	{
	  nx = n;
	  n *= 4;
	  dw0 /= 4;

	  for (i = 0; i < c.n; i += n)
	  {
		Complex<T> *v0, *v1;
		T r0, i0, r1, i1, r2, i2, r3, i3, r4, i4;

		v0 = dst + i;
		v1 = v0 + nx * 2;

		r0 = v1[0].re; i0 = v1[0].im;
		r4 = v1[nx].re; i4 = v1[nx].im;

		r1 = r0 + r4; i1 = i0 + i4;
		r3 = i0 - i4; i3 = r4 - r0;

		r2 = v0[0].re; i2 = v0[0].im;
		r4 = v0[nx].re; i4 = v0[nx].im;

		r0 = r2 + r4; i0 = i2 + i4;
		r2 -= r4; i2 -= i4;

		v0[0].re = r0 + r1; v0[0].im = i0 + i1;
		v1[0].re = r0 - r1; v1[0].im = i0 - i1;
		v0[nx].re = r2 + r3; v0[nx].im = i2 + i3;
		v1[nx].re = r2 - r3; v1[nx].im = i2 - i3;

		for (j = 1, dw = dw0; j < nx; j++, dw += dw0)
		{
		  v0 = dst + i + j;
		  v1 = v0 + nx * 2;

		  r2 = v0[nx].re*wave[dw * 2].re - v0[nx].im*wave[dw * 2].im;
		  i2 = v0[nx].re*wave[dw * 2].im + v0[nx].im*wave[dw * 2].re;
		  r0 = v1[0].re*wave[dw].im + v1[0].im*wave[dw].re;
		  i0 = v1[0].re*wave[dw].re - v1[0].im*wave[dw].im;
		  r3 = v1[nx].re*wave[dw * 3].im + v1[nx].im*wave[dw * 3].re;
		  i3 = v1[nx].re*wave[dw * 3].re - v1[nx].im*wave[dw * 3].im;

		  r1 = i0 + i3; i1 = r0 + r3;
		  r3 = r0 - r3; i3 = i3 - i0;
		  r4 = v0[0].re; i4 = v0[0].im;

		  r0 = r4 + r2; i0 = i4 + i2;
		  r2 = r4 - r2; i2 = i4 - i2;

		  v0[0].re = r0 + r1; v0[0].im = i0 + i1;
		  v1[0].re = r0 - r1; v1[0].im = i0 - i1;
		  v0[nx].re = r2 + r3; v0[nx].im = i2 + i3;
		  v1[nx].re = r2 - r3; v1[nx].im = i2 - i3;
		}
	  }
	}

	for (; n < c.factors[0]; )
	{
	  // do the remaining radix-2 transform
	  nx = n;
	  n *= 2;
	  dw0 /= 2;

	  for (i = 0; i < c.n; i += n)
	  {
		Complex<T>* v = dst + i;
		T r0 = v[0].re + v[nx].re;
		T i0 = v[0].im + v[nx].im;
		T r1 = v[0].re - v[nx].re;
		T i1 = v[0].im - v[nx].im;
		v[0].re = r0; v[0].im = i0;
		v[nx].re = r1; v[nx].im = i1;

		for (j = 1, dw = dw0; j < nx; j++, dw += dw0)
		{
		  v = dst + i + j;
		  r1 = v[nx].re*wave[dw].re - v[nx].im*wave[dw].im;
		  i1 = v[nx].im*wave[dw].re + v[nx].re*wave[dw].im;
		  r0 = v[0].re; i0 = v[0].im;

		  v[0].re = r0 + r1; v[0].im = i0 + i1;
		  v[nx].re = r0 - r1; v[nx].im = i0 - i1;
		}
	  }
	}
  }

  // 2. all the other transforms
  for (f_idx = (c.factors[0] & 1) ? 0 : 1; f_idx < c.nf; f_idx++)
  {
	int factor = c.factors[f_idx];
	nx = n;
	n *= factor;
	dw0 /= factor;

	if (factor == 3)
	{
	  // radix-3
	  for (i = 0; i < c.n; i += n)
	  {
		Complex<T>* v = dst + i;

		T r1 = v[nx].re + v[nx * 2].re;
		T i1 = v[nx].im + v[nx * 2].im;
		T r0 = v[0].re;
		T i0 = v[0].im;
		T r2 = sin_120 * (v[nx].im - v[nx * 2].im);
		T i2 = sin_120 * (v[nx * 2].re - v[nx].re);
		v[0].re = r0 + r1; v[0].im = i0 + i1;
		r0 -= (T)0.5*r1; i0 -= (T)0.5*i1;
		v[nx].re = r0 + r2; v[nx].im = i0 + i2;
		v[nx * 2].re = r0 - r2; v[nx * 2].im = i0 - i2;

		for (j = 1, dw = dw0; j < nx; j++, dw += dw0)
		{
		  v = dst + i + j;
		  r0 = v[nx].re*wave[dw].re - v[nx].im*wave[dw].im;
		  i0 = v[nx].re*wave[dw].im + v[nx].im*wave[dw].re;
		  i2 = v[nx * 2].re*wave[dw * 2].re - v[nx * 2].im*wave[dw * 2].im;
		  r2 = v[nx * 2].re*wave[dw * 2].im + v[nx * 2].im*wave[dw * 2].re;
		  r1 = r0 + i2; i1 = i0 + r2;

		  r2 = sin_120 * (i0 - r2); i2 = sin_120 * (i2 - r0);
		  r0 = v[0].re; i0 = v[0].im;
		  v[0].re = r0 + r1; v[0].im = i0 + i1;
		  r0 -= (T)0.5*r1; i0 -= (T)0.5*i1;
		  v[nx].re = r0 + r2; v[nx].im = i0 + i2;
		  v[nx * 2].re = r0 - r2; v[nx * 2].im = i0 - i2;
		}
	  }
	}
	else if (factor == 5)
	{
	  // radix-5
	  for (i = 0; i < c.n; i += n)
	  {
		for (j = 0, dw = 0; j < nx; j++, dw += dw0)
		{
		  Complex<T>* v0 = dst + i + j;
		  Complex<T>* v1 = v0 + nx * 2;
		  Complex<T>* v2 = v1 + nx * 2;

		  T r0, i0, r1, i1, r2, i2, r3, i3, r4, i4, r5, i5;

		  r3 = v0[nx].re*wave[dw].re - v0[nx].im*wave[dw].im;
		  i3 = v0[nx].re*wave[dw].im + v0[nx].im*wave[dw].re;
		  r2 = v2[0].re*wave[dw * 4].re - v2[0].im*wave[dw * 4].im;
		  i2 = v2[0].re*wave[dw * 4].im + v2[0].im*wave[dw * 4].re;

		  r1 = r3 + r2; i1 = i3 + i2;
		  r3 -= r2; i3 -= i2;

		  r4 = v1[nx].re*wave[dw * 3].re - v1[nx].im*wave[dw * 3].im;
		  i4 = v1[nx].re*wave[dw * 3].im + v1[nx].im*wave[dw * 3].re;
		  r0 = v1[0].re*wave[dw * 2].re - v1[0].im*wave[dw * 2].im;
		  i0 = v1[0].re*wave[dw * 2].im + v1[0].im*wave[dw * 2].re;

		  r2 = r4 + r0; i2 = i4 + i0;
		  r4 -= r0; i4 -= i0;

		  r0 = v0[0].re; i0 = v0[0].im;
		  r5 = r1 + r2; i5 = i1 + i2;

		  v0[0].re = r0 + r5; v0[0].im = i0 + i5;

		  r0 -= (T)0.25*r5; i0 -= (T)0.25*i5;
		  r1 = fft5_2 * (r1 - r2); i1 = fft5_2 * (i1 - i2);
		  r2 = -fft5_3 * (i3 + i4); i2 = fft5_3 * (r3 + r4);

		  i3 *= -fft5_5; r3 *= fft5_5;
		  i4 *= -fft5_4; r4 *= fft5_4;

		  r5 = r2 + i3; i5 = i2 + r3;
		  r2 -= i4; i2 -= r4;

		  r3 = r0 + r1; i3 = i0 + i1;
		  r0 -= r1; i0 -= i1;

		  v0[nx].re = r3 + r2; v0[nx].im = i3 + i2;
		  v2[0].re = r3 - r2; v2[0].im = i3 - i2;

		  v1[0].re = r0 + r5; v1[0].im = i0 + i5;
		  v1[nx].re = r0 - r5; v1[nx].im = i0 - i5;
		}
	  }
	}
  }

  if (scale != 1)
  {
	T re_scale = scale, im_scale = scale;
	if (inv)
	  im_scale = -im_scale;

	for (i = 0; i < c.n; i++)
	{
	  T t0 = dst[i].re*re_scale;
	  T t1 = dst[i].im*im_scale;
	  dst[i].re = t0;
	  dst[i].im = t1;
	}
  }
  else if (inv)
  {
	for (i = 0; i <= c.n - 2; i += 2)
	{
	  T t0 = -dst[i].im;
	  T t1 = -dst[i + 1].im;
	  dst[i].im = t0;
	  dst[i + 1].im = t1;
	}

	if (i < c.n)
	  dst[c.n - 1].im = -dst[c.n - 1].im;
  }
}

template<typename T> static void
dft_real(const dft_options & c, const T* src, T* dst)
{
  int n = c.n;
  int complex_output = c.isComplex;
  T scale = (T)c.scale;
  int j;
  dst += complex_output;

  //assert(c.tab_size == n);

  if (n == 1)
  {
	dst[0] = src[0] * scale;
  }
  else if (n == 2)
  {
	T t = (src[0] + src[1])*scale;
	dst[1] = (src[0] - src[1])*scale;
	dst[0] = t;
  }
  else if (n & 1)
  {
	dst -= complex_output;
	Complex<T>* _dst = (Complex<T>*)dst;
	_dst[0].re = src[0] * scale;
	_dst[0].im = 0;
	for (j = 1; j < n; j += 2)
	{
	  T t0 = src[c.itab[j]] * scale;
	  T t1 = src[c.itab[j + 1]] * scale;
	  _dst[j].re = t0;
	  _dst[j].im = 0;
	  _dst[j + 1].re = t1;
	  _dst[j + 1].im = 0;
	}
	dft_options sub_c = c;
	sub_c.isComplex = false;
	sub_c.isInverse = false;
	sub_c.noPermute = true;
	sub_c.scale = 1.;
	dft_complex(sub_c, _dst, _dst);
	if (!complex_output)
	  dst[1] = dst[0];
  }
  else
  {
	T t0, t;
	T h1_re, h1_im, h2_re, h2_im;
	T scale2 = scale * (T)0.5;
	int n2 = n >> 1;

	c.factors[0] >>= 1;

	dft_options sub_c = c;
	sub_c.factors += (c.factors[0] == 1);
	sub_c.nf -= (c.factors[0] == 1);
	sub_c.isComplex = false;
	sub_c.isInverse = false;
	sub_c.noPermute = false;
	sub_c.scale = 1.;
	sub_c.n = n2;

	dft_complex(sub_c, (Complex<T>*)src, (Complex<T>*)dst);

	c.factors[0] <<= 1;

	t = dst[0] - dst[1];
	dst[0] = (dst[0] + dst[1])*scale;
	dst[1] = t * scale;

	t0 = dst[n2];
	t = dst[n - 1];
	dst[n - 1] = dst[1];

	const Complex<T> *wave = (const Complex<T>*)c.wave;

	for (j = 2, wave++; j < n2; j += 2, wave++)
	{
	  /* calc odd */
	  h2_re = scale2 * (dst[j + 1] + t);
	  h2_im = scale2 * (dst[n - j] - dst[j]);

	  /* calc even */
	  h1_re = scale2 * (dst[j] + dst[n - j]);
	  h1_im = scale2 * (dst[j + 1] - t);

	  /* rotate */
	  t = h2_re * wave->re - h2_im * wave->im;
	  h2_im = h2_re * wave->im + h2_im * wave->re;
	  h2_re = t;
	  t = dst[n - j - 1];

	  dst[j - 1] = h1_re + h2_re;
	  dst[n - j - 1] = h1_re - h2_re;
	  dst[j] = h1_im + h2_im;
	  dst[n - j] = h2_im - h1_im;
	}

	if (j <= n2)
	{
	  dst[n2 - 1] = t0 * scale;
	  dst[n2] = -t * scale;
	}
  }

  if (complex_output && ((n & 1) == 0 || n == 1))
  {
	dst[-1] = dst[0];
	dst[0] = 0;
	if (n > 1)
	  dst[n] = 0;
  }
}

static void
copy_column(const uchar* _src, size_t src_step,
  uchar* _dst, size_t dst_step,
  int len, size_t elem_size)
{
  int i, t0, t1;
  const int* src = (const int*)_src;
  int* dst = (int*)_dst;
  src_step /= sizeof(src[0]);
  dst_step /= sizeof(dst[0]);

  if (elem_size == sizeof(int))
  {
	for (i = 0; i < len; i++, src += src_step, dst += dst_step)
	  dst[0] = src[0];
  }
  else if (elem_size == sizeof(int) * 2)
  {
	for (i = 0; i < len; i++, src += src_step, dst += dst_step)
	{
	  t0 = src[0]; t1 = src[1];
	  dst[0] = t0; dst[1] = t1;
	}
  }
  else if (elem_size == sizeof(int) * 4)
  {
	for (i = 0; i < len; i++, src += src_step, dst += dst_step)
	{
	  t0 = src[0]; t1 = src[1];
	  dst[0] = t0; dst[1] = t1;
	  t0 = src[2]; t1 = src[3];
	  dst[2] = t0; dst[3] = t1;
	}
  }
}

static void
copy_from_column(const uchar* _src, size_t src_step,
  uchar* _dst0, uchar* _dst1,
  int len, size_t elem_size)
{
  int i, t0, t1;
  const int* src = (const int*)_src;
  int* dst0 = (int*)_dst0;
  int* dst1 = (int*)_dst1;
  src_step /= sizeof(src[0]);

  if (elem_size == sizeof(int))
  {
	for (i = 0; i < len; i++, src += src_step)
	{
	  t0 = src[0]; t1 = src[1];
	  dst0[i] = t0; dst1[i] = t1;
	}
  }
  else if (elem_size == sizeof(int) * 2)
  {
	for (i = 0; i < len * 2; i += 2, src += src_step)
	{
	  t0 = src[0]; t1 = src[1];
	  dst0[i] = t0; dst0[i + 1] = t1;
	  t0 = src[2]; t1 = src[3];
	  dst1[i] = t0; dst1[i + 1] = t1;
	}
  }
  else if (elem_size == sizeof(int) * 4)
  {
	for (i = 0; i < len * 4; i += 4, src += src_step)
	{
	  t0 = src[0]; t1 = src[1];
	  dst0[i] = t0; dst0[i + 1] = t1;
	  t0 = src[2]; t1 = src[3];
	  dst0[i + 2] = t0; dst0[i + 3] = t1;
	  t0 = src[4]; t1 = src[5];
	  dst1[i] = t0; dst1[i + 1] = t1;
	  t0 = src[6]; t1 = src[7];
	  dst1[i + 2] = t0; dst1[i + 3] = t1;
	}
  }
}

static void
copy_to_column(const uchar* _src0, const uchar* _src1,
  uchar* _dst, size_t dst_step,
  int len, size_t elem_size)
{
  int i, t0, t1;
  const int* src0 = (const int*)_src0;
  const int* src1 = (const int*)_src1;
  int* dst = (int*)_dst;
  dst_step /= sizeof(dst[0]);

  if (elem_size == sizeof(int))
  {
	for (i = 0; i < len; i++, dst += dst_step)
	{
	  t0 = src0[i]; t1 = src1[i];
	  dst[0] = t0; dst[1] = t1;
	}
  }
  else if (elem_size == sizeof(int) * 2)
  {
	for (i = 0; i < len * 2; i += 2, dst += dst_step)
	{
	  t0 = src0[i]; t1 = src0[i + 1];
	  dst[0] = t0; dst[1] = t1;
	  t0 = src1[i]; t1 = src1[i + 1];
	  dst[2] = t0; dst[3] = t1;
	}
  }
  else if (elem_size == sizeof(int) * 4)
  {
	for (i = 0; i < len * 4; i += 4, dst += dst_step)
	{
	  t0 = src0[i]; t1 = src0[i + 1];
	  dst[0] = t0; dst[1] = t1;
	  t0 = src0[i + 2]; t1 = src0[i + 3];
	  dst[2] = t0; dst[3] = t1;
	  t0 = src1[i]; t1 = src1[i + 1];
	  dst[4] = t0; dst[5] = t1;
	  t0 = src1[i + 2]; t1 = src1[i + 3];
	  dst[6] = t0; dst[7] = t1;
	}
  }
}

static void DFT_32f(const dft_options & c, const Complexf* src, Complexf* dst)
{
  dft_complex(c, src, dst);
}

static void RealDFT_32f(const dft_options & c, const float* src, float* dst)
{
  dft_real(c, src, dst);
}


template <typename T>
static void complement_complex(T * ptr, size_t step, int n, int len, int dft_dims)
{
  T* p0 = (T*)ptr;
  size_t dstep = step / sizeof(p0[0]);
  for (int i = 0; i < len; i++)
  {
	T* p = p0 + dstep * i;
	T* q = dft_dims == 1 || i == 0 || i * 2 == len ? p : p0 + dstep * (len - i);

	for (int j = 1; j < (n + 1) / 2; j++)
	{
	  p[(n - j) * 2] = q[j * 2];
	  p[(n - j) * 2 + 1] = -q[j * 2 + 1];
	}
  }
}

void dft_basic_impl::init(int len, int count, bool flags, int stage, bool *needBuffer)
{
  int prev_len = opt.n;

  int complex_elem_size = sizeof(Complex<float>);
  opt.isInverse = flags;
  bool real_transform = !flags;
  opt.isComplex = (stage == 0) && (!flags);
  bool needAnotherStage = (stage == 0);

  opt.scale = 1;
  opt.tab_size = len;
  opt.n = len;

  if (len != prev_len)
  {
	opt.nf = dft_factorize(opt.n, opt.factors);
  }
  bool inplace_transform = opt.factors[0] == opt.factors[opt.nf - 1];
  if (len != prev_len || (!inplace_transform && opt.isInverse && real_transform))
  {
	//wave_buf.allocate(opt.n*complex_elem_size);
	opt.wave = wave_buf;
	//itab_buf.allocate(opt.n);
	opt.itab = itab_buf;
	dft_init(opt.n, opt.nf, opt.factors, opt.itab, complex_elem_size,
	  opt.wave, stage == 0 && opt.isInverse && real_transform);
  }
  // otherwise reuse the tables calculated on the previous stage
  if (needBuffer)
  {
	if ((stage == 0 && ((*needBuffer && !inplace_transform) || (real_transform && (len & 1)))) ||
	  (stage == 1 && !inplace_transform))
	{
	  *needBuffer = true;
	}
  }

  {
	static dft_funcs dft_tbl[6] =
	{
	  (dft_funcs)DFT_32f,
	  (dft_funcs)RealDFT_32f,
	};
	int idx = 0;
	if (stage == 0)
	{
	  if (real_transform)
	  {
		if (!opt.isInverse)
		  idx = 1;
		else
		  idx = 2;
	  }
	}

	opt.dft_func = dft_tbl[idx];
  }

  if (!needAnotherStage && flags)
	opt.scale = 1. / (len * count);
}

void dft_basic_impl::apply(const uchar *src, uchar *dst)
{
  opt.dft_func(opt, src, dst);
}

void dft_impl::init(int _width, int _height, int _src_channels, int _dst_channels, bool flags)
{
  width = _width;
  height = _height;
  src_channels = _src_channels;
  dst_channels = _dst_channels;

  inv = flags;

  elem_size = sizeof(float);
  complex_elem_size = elem_size * 2;
  if (inv)
	elem_size = complex_elem_size;

  contextA->init(width, height, inv, 0, &needBufferA);
  contextB->init(height, width, inv, 1, &needBufferB);
}

void dft_impl::deinit() {
  delete contextA;
  delete contextB;
}

void dft_impl::apply(const uchar * src, size_t src_step, uchar * dst, size_t dst_step)
{
  memset(dst, 0, width * height * 2 * sizeof(float));
  int stage_src_channels = src_channels;
  int stage_dst_channels = dst_channels;

  bool isComplex = stage_src_channels != stage_dst_channels;

  row_dft(src, src_step, dst, dst_step, isComplex, false);

  src = dst;
  src_step = dst_step;
  stage_src_channels = stage_dst_channels;
  col_dft(src, src_step, dst, dst_step, stage_src_channels, stage_dst_channels, true);
}

void dft_impl::row_dft(const uchar* src_data, size_t src_step, uchar* dst_data, size_t dst_step, bool isComplex, bool isLastStage)
{
  int len, count;

  len = width;
  count = height;

  int dptr_offset = 0;
  int dst_full_len = len * elem_size;

  if (!inv && isComplex)
	dst_full_len += (len & 1) ? elem_size : complex_elem_size;

  int i;
  for (i = 0; i < count; i++)
  {
	const uchar* sptr = src_data + src_step * i;
	uchar* dptr0 = dst_data + dst_step * i;
	uchar* dptr = dptr0;

	if (needBufferA)
	  dptr = tmp_bufA;

	contextA->apply(sptr, dptr);

	if (needBufferA)
	  memcpy(dptr0, dptr + dptr_offset, dst_full_len);
  }

  for (; i < count; i++)
  {
	uchar* dptr0 = dst_data + dst_step * i;
	memset(dptr0, 0, dst_full_len);
  }
  if (isLastStage && !inv)
	complement_complex((float *)dst_data, dst_step, len, count, 1);
}

void dft_impl::col_dft(const uchar* src_data, size_t src_step, uchar* dst_data, size_t dst_step, int stage_src_channels, int stage_dst_channels, bool isLastStage)
{
  int len = height;
  int count = width;
  int a = 0, b = count;
  uchar *dbuf0, *dbuf1;
  const uchar* sptr0 = src_data;
  uchar* dptr0 = dst_data;

  dbuf0 = buf0, dbuf1 = buf1;

  if (needBufferB)
  {
	dbuf1 = tmp_bufB;
	dbuf0 = buf1;
  }

  if (!inv)
  {
	int even;
	a = 1;
	even = (count & 1) == 0;
	b = (count + 1) / 2;
	if (!inv)
	{
	  memset(buf0, 0, len*complex_elem_size);
	  copy_column(sptr0, src_step, buf0, complex_elem_size, len, elem_size);
	  sptr0 += stage_dst_channels * elem_size;
	  if (even)
	  {
		memset(buf1, 0, len*complex_elem_size);
		copy_column(sptr0 + (count - 2)*elem_size, src_step,
		  buf1, complex_elem_size, len, elem_size);
	  }
	}
	else
	{
	  copy_column(sptr0, src_step, buf0, complex_elem_size, len, complex_elem_size);
	  if (even)
	  {
		copy_column(sptr0 + b * complex_elem_size, src_step,
		  buf1, complex_elem_size, len, complex_elem_size);
	  }
	  sptr0 += complex_elem_size;
	}

	if (even)
	  contextB->apply(buf1, dbuf1);
	contextB->apply(buf0, dbuf0);

	if (stage_dst_channels == 1)
	{
	  if (!inv)
	  {
		// copy the half of output vector to the first/last column.
		// before doing that, defgragment the vector
		memcpy(dbuf0 + elem_size, dbuf0, elem_size);
		copy_column(dbuf0 + elem_size, elem_size, dptr0,
		  dst_step, len, elem_size);
		if (even)
		{
		  memcpy(dbuf1 + elem_size, dbuf1, elem_size);
		  copy_column(dbuf1 + elem_size, elem_size,
			dptr0 + (count - 1)*elem_size,
			dst_step, len, elem_size);
		}
		dptr0 += elem_size;
	  }
	  else
	  {
		// copy the real part of the complex vector to the first/last column
		copy_column(dbuf0, complex_elem_size, dptr0, dst_step, len, elem_size);
		if (even)
		  copy_column(dbuf1, complex_elem_size, dptr0 + (count - 1)*elem_size,
			dst_step, len, elem_size);
		dptr0 += elem_size;
	  }
	}
	else
	{
	  //assert(!inv);
	  copy_column(dbuf0, complex_elem_size, dptr0,
		dst_step, len, complex_elem_size);
	  if (even)
		copy_column(dbuf1, complex_elem_size,
		  dptr0 + b * complex_elem_size,
		  dst_step, len, complex_elem_size);
	  dptr0 += complex_elem_size;
	}
  }

  for (int i = a; i < b; i += 2)
  {
	if (i + 1 < b)
	{
	  copy_from_column(sptr0, src_step, buf0, buf1, len, complex_elem_size);
	  contextB->apply(buf1, dbuf1);
	}
	else
	  copy_column(sptr0, src_step, buf0, complex_elem_size, len, complex_elem_size);

	contextB->apply(buf0, dbuf0);

	if (i + 1 < b)
	  copy_to_column(dbuf0, dbuf1, dptr0, dst_step, len, complex_elem_size);
	else
	  copy_column(dbuf0, complex_elem_size, dptr0, dst_step, len, complex_elem_size);
	sptr0 += 2 * complex_elem_size;
	dptr0 += 2 * complex_elem_size;
  }
  if (isLastStage && !inv)
	complement_complex((float *)dst_data, dst_step, count, len, 2);
}

#ifdef XLNX_EMBEDDED_PLATFORM
void dft_impl::apply_neon(const uchar * src, size_t src_step, uchar * dst, size_t dst_step, ne10_fft_cfg_float32_t cfg)
{
  memset(dst, 0, width * height * 2 * sizeof(float));
  int stage_src_channels = src_channels;
  int stage_dst_channels = dst_channels;

  bool isComplex = stage_src_channels != stage_dst_channels;

  row_dft(src, src_step, dst, dst_step, isComplex, false);

  src = dst;
  src_step = dst_step;
  stage_src_channels = stage_dst_channels;
  if (height > 15)
	col_dft_neon(src, src_step, dst, dst_step, stage_src_channels, stage_dst_channels, true, cfg);
  else
	col_dft(src, src_step, dst, dst_step, stage_src_channels, stage_dst_channels, true);
}

void dft_impl::col_dft_neon(const uchar* src_data, size_t src_step, uchar* dst_data, size_t dst_step, int stage_src_channels, int stage_dst_channels, bool isLastStage, ne10_fft_cfg_float32_t cfg)
{
  int len = height;
  int count = width;
  int a = 0, b = count;
  uchar *dbuf0, *dbuf1;
  const uchar* sptr0 = src_data;
  uchar* dptr0 = dst_data;

  dbuf0 = buf0, dbuf1 = buf1;

  if (needBufferB)
  {
	dbuf1 = tmp_bufB;
	dbuf0 = buf1;
  }

  if (!inv)
  {
	int even;
	a = 1;
	even = (count & 1) == 0;
	b = (count + 1) / 2;
	if (!inv)
	{
	  memset(buf0, 0, len*complex_elem_size);
	  copy_column(sptr0, src_step, buf0, complex_elem_size, len, elem_size);
	  sptr0 += stage_dst_channels * elem_size;
	  if (even)
	  {
		memset(buf1, 0, len*complex_elem_size);
		copy_column(sptr0 + (count - 2)*elem_size, src_step,
		  buf1, complex_elem_size, len, elem_size);
	  }
	}
	else
	{
	  copy_column(sptr0, src_step, buf0, complex_elem_size, len, complex_elem_size);
	  if (even)
	  {
		copy_column(sptr0 + b * complex_elem_size, src_step,
		  buf1, complex_elem_size, len, complex_elem_size);
	  }
	  sptr0 += complex_elem_size;
	}

	if (even) {
	  //contextB->apply(buf1.data(), dbuf1);
	  ne10_fft_c2c_1d_float32_neon((ne10_fft_cpx_float32_t *)dbuf1, (ne10_fft_cpx_float32_t *)buf1, cfg, 0);
	}

	//contextB->apply(buf0.data(), dbuf0);
	ne10_fft_c2c_1d_float32_neon((ne10_fft_cpx_float32_t *)dbuf0, (ne10_fft_cpx_float32_t *)buf0, cfg, 0);

	if (stage_dst_channels == 1)
	{
	  if (!inv)
	  {
		// copy the half of output vector to the first/last column.
		// before doing that, defgragment the vector
		memcpy(dbuf0 + elem_size, dbuf0, elem_size);
		copy_column(dbuf0 + elem_size, elem_size, dptr0,
		  dst_step, len, elem_size);
		if (even)
		{
		  memcpy(dbuf1 + elem_size, dbuf1, elem_size);
		  copy_column(dbuf1 + elem_size, elem_size,
			dptr0 + (count - 1)*elem_size,
			dst_step, len, elem_size);
		}
		dptr0 += elem_size;
	  }
	  else
	  {
		// copy the real part of the complex vector to the first/last column
		copy_column(dbuf0, complex_elem_size, dptr0, dst_step, len, elem_size);
		if (even)
		  copy_column(dbuf1, complex_elem_size, dptr0 + (count - 1)*elem_size,
			dst_step, len, elem_size);
		dptr0 += elem_size;
	  }
	}
	else
	{
	  //assert(!inv);
	  copy_column(dbuf0, complex_elem_size, dptr0,
		dst_step, len, complex_elem_size);
	  if (even)
		copy_column(dbuf1, complex_elem_size,
		  dptr0 + b * complex_elem_size,
		  dst_step, len, complex_elem_size);
	  dptr0 += complex_elem_size;
	}
  }

  for (int i = a; i < b; i += 2)
  {
	if (i + 1 < b)
	{
	  copy_from_column(sptr0, src_step, buf0, buf1, len, complex_elem_size);
	  //contextB->apply(buf1.data(), dbuf1);
	  ne10_fft_c2c_1d_float32_neon((ne10_fft_cpx_float32_t *)dbuf1, (ne10_fft_cpx_float32_t *)buf1, cfg, 0);
	}
	else
	  copy_column(sptr0, src_step, buf0, complex_elem_size, len, complex_elem_size);

	//contextB->apply(buf0.data(), dbuf0);
	ne10_fft_c2c_1d_float32_neon((ne10_fft_cpx_float32_t *)dbuf0, (ne10_fft_cpx_float32_t *)buf0, cfg, 0);

	if (i + 1 < b)
	  copy_to_column(dbuf0, dbuf1, dptr0, dst_step, len, complex_elem_size);
	else
	  copy_column(dbuf0, complex_elem_size, dptr0, dst_step, len, complex_elem_size);
	sptr0 += 2 * complex_elem_size;
	dptr0 += 2 * complex_elem_size;
  }
  if (isLastStage && !inv)
	complement_complex((float*)dst_data, dst_step, count, len, 2);
}
#endif

void dft(fMat _src0, int src_ch, fMat _dst, int dst_ch, bool flags)
{
  int src_step = _src0.width * sizeof(float);
  int dst_step = _dst.width * sizeof(float);
  int width = _src0.width / src_ch;

  dft_impl *impl = new dft_impl();
  impl->init(width, _src0.height, src_ch, dst_ch, flags);

  impl->apply((uchar*)_src0.data, src_step, (uchar*)_dst.data, dst_step);
  impl->deinit();
  delete impl;
}

#define VAL(buf, elem) (((T*)((char*)data ## buf + (step ## buf * (elem))))[0])
#define MUL_SPECTRUMS_COL(A, B, C) \
    VAL(C, 0) = VAL(A, 0) * VAL(B, 0); \
    for (size_t j = 1; j <= rows - 2; j += 2) \
    { \
        double a_re = VAL(A, j), a_im = VAL(A, j + 1); \
        double b_re = VAL(B, j), b_im = VAL(B, j + 1); \
        if (conjB) b_im = -b_im; \
        double c_re = a_re * b_re - a_im * b_im; \
        double c_im = a_re * b_im + a_im * b_re; \
        VAL(C, j) = (T)c_re; VAL(C, j + 1) = (T)c_im; \
    } \
    if ((rows & 1) == 0) \
        VAL(C, rows-1) = VAL(A, rows-1) * VAL(B, rows-1)

template <typename T, bool conjB> static inline
void mulSpectrums_processCol_noinplace(const T* dataA, const T* dataB, T* dataC, size_t stepA, size_t stepB, size_t stepC, size_t rows)
{
  MUL_SPECTRUMS_COL(A, B, C);
}

template <typename T, bool conjB> static inline
void mulSpectrums_processCol_inplaceA(const T* dataB, T* dataAC, size_t stepB, size_t stepAC, size_t rows)
{
  MUL_SPECTRUMS_COL(AC, B, AC);
}

template <typename T, bool conjB, bool inplaceA> static inline
void mulSpectrums_processCol(const T* dataA, const T* dataB, T* dataC, size_t stepA, size_t stepB, size_t stepC, size_t rows)
{
  if (inplaceA)
	mulSpectrums_processCol_inplaceA<T, conjB>(dataB, dataC, stepB, stepC, rows);
  else
	mulSpectrums_processCol_noinplace<T, conjB>(dataA, dataB, dataC, stepA, stepB, stepC, rows);
}
#undef MUL_SPECTRUMS_COL
#undef VAL

template <typename T, bool conjB, bool inplaceA> static inline
void mulSpectrums_processCols(const T* dataA, const T* dataB, T* dataC, size_t stepA, size_t stepB, size_t stepC, size_t rows, size_t cols)
{
  mulSpectrums_processCol<T, conjB, inplaceA>(dataA, dataB, dataC, stepA, stepB, stepC, rows);
  if ((cols & 1) == 0)
  {
	mulSpectrums_processCol<T, conjB, inplaceA>(dataA + cols - 1, dataB + cols - 1, dataC + cols - 1, stepA, stepB, stepC, rows);
  }
}

#define VAL(buf, elem) (data ## buf[(elem)])
#define MUL_SPECTRUMS_ROW(A, B, C) \
    for (size_t j = j0; j < j1; j += 2) \
    { \
        double a_re = VAL(A, j), a_im = VAL(A, j + 1); \
        double b_re = VAL(B, j), b_im = VAL(B, j + 1); \
        if (conjB) b_im = -b_im; \
        double c_re = a_re * b_re - a_im * b_im; \
        double c_im = a_re * b_im + a_im * b_re; \
        VAL(C, j) = (T)c_re; VAL(C, j + 1) = (T)c_im; \
    }

template <typename T, bool conjB> static inline
void mulSpectrums_processRow_noinplace(const T* dataA, const T* dataB, T* dataC, size_t j0, size_t j1)
{
  MUL_SPECTRUMS_ROW(A, B, C);
}

template <typename T, bool conjB> static inline
void mulSpectrums_processRow_inplaceA(const T* dataB, T* dataAC, size_t j0, size_t j1)
{
  MUL_SPECTRUMS_ROW(AC, B, AC);
}

template <typename T, bool conjB, bool inplaceA> static inline
void mulSpectrums_processRow(const T* dataA, const T* dataB, T* dataC, size_t j0, size_t j1)
{
  if (inplaceA)
	mulSpectrums_processRow_inplaceA<T, conjB>(dataB, dataC, j0, j1);
  else
	mulSpectrums_processRow_noinplace<T, conjB>(dataA, dataB, dataC, j0, j1);
}
#undef MUL_SPECTRUMS_ROW
#undef VAL

template <typename T, bool conjB, bool inplaceA> static inline
void mulSpectrums_processRows(const T* dataA, const T* dataB, T* dataC, size_t stepA, size_t stepB, size_t stepC, size_t rows, size_t cols, size_t j0, size_t j1, bool is_1d_CN1)
{
  while (rows-- > 0)
  {
	if (is_1d_CN1)
	  dataC[0] = dataA[0] * dataB[0];
	mulSpectrums_processRow<T, conjB, inplaceA>(dataA, dataB, dataC, j0, j1);
	if (is_1d_CN1 && (cols & 1) == 0)
	  dataC[j1] = dataA[j1] * dataB[j1];

	dataA = (const T*)(((char*)dataA) + stepA);
	dataB = (const T*)(((char*)dataB) + stepB);
	dataC = (T*)(((char*)dataC) + stepC);
  }
}


template <typename T, bool conjB, bool inplaceA> static inline
void mulSpectrums_Impl_(const T* dataA, const T* dataB, T* dataC, size_t stepA, size_t stepB, size_t stepC, size_t rows, size_t cols, size_t j0, size_t j1, bool is_1d, bool isCN1)
{
  if (!is_1d && isCN1)
  {
	mulSpectrums_processCols<T, conjB, inplaceA>(dataA, dataB, dataC, stepA, stepB, stepC, rows, cols);
  }
  mulSpectrums_processRows<T, conjB, inplaceA>(dataA, dataB, dataC, stepA, stepB, stepC, rows, cols, j0, j1, is_1d && isCN1);
}

template <typename T, bool conjB> static inline
void mulSpectrums_Impl(const T* dataA, const T* dataB, T* dataC, size_t stepA, size_t stepB, size_t stepC, size_t rows, size_t cols, size_t j0, size_t j1, bool is_1d, bool isCN1)
{
  if (dataA == dataC)
	mulSpectrums_Impl_<T, conjB, true>(dataA, dataB, dataC, stepA, stepB, stepC, rows, cols, j0, j1, is_1d, isCN1);
  else
	mulSpectrums_Impl_<T, conjB, false>(dataA, dataB, dataC, stepA, stepB, stepC, rows, cols, j0, j1, is_1d, isCN1);
}

void mulSpectrums(fMat _srcA, fMat _srcB, int src_ch, fMat _dst, int flags, bool conjB)
{
  int cn = 2;
  size_t rows = _srcA.height, cols = _srcA.width / src_ch;
  bool isCN1 = cn == 1;
  size_t j0 = isCN1 ? 1 : 0;
  size_t j1 = cols * cn - (((cols & 1) == 0 && cn == 1) ? 1 : 0);
  int src_step = _srcA.width * sizeof(float);
  int dst_step = _dst.width * sizeof(float);

  const float* dataA = _srcA.data;
  const float* dataB = _srcB.data;
  float* dataC = _dst.data;
  if (!conjB)
	mulSpectrums_Impl<float, false>(dataA, dataB, dataC, src_step, src_step, dst_step, rows, cols, j0, j1, false, isCN1);
  else
	mulSpectrums_Impl<float, true>(dataA, dataB, dataC, src_step, src_step, dst_step, rows, cols, j0, j1, false, isCN1);
}

void magnitude(fMat img, fMat *mag)
{
  float *rptr, *iptr, *fptr;

  rptr = img.data;
  iptr = img.data + 1;
  fptr = mag->data;

  for (short i = 0;i < (img.width >> 1) * img.height; i++) {
	*fptr++ = sqrt(*rptr * *rptr + *iptr * *iptr);
	rptr += 2;
	iptr += 2;
  }
}

#ifdef XLNX_EMBEDDED_PLATFORM
void dft_neon(fMat _src0, int src_ch, fMat _dst, int dst_ch, bool flags, ne10_fft_cfg_float32_t cfg)
{
  int src_step = _src0.width * sizeof(float);
  int dst_step = _dst.width * sizeof(float);
  int width = _src0.width / src_ch;

  dft_impl *impl = new dft_impl();
  impl->init(width, _src0.height, src_ch, dst_ch, flags);

  impl->apply_neon((uchar*)_src0.data, src_step, (uchar*)_dst.data, dst_step, cfg);
  impl->deinit();
  delete impl;
}
#endif

void complexMultiplication(fMat a, fMat b, fMat res)
{
  float *rptr1, *iptr1;
  float *rptr2, *iptr2;
  float *fptr1, *fptr2;
  short width = res.width >> 1;

  rptr1 = a.data;
  iptr1 = a.data + 1;
  rptr2 = b.data;
  iptr2 = b.data + 1;
  fptr1 = res.data;
  fptr2 = res.data + 1;
  for (short i = 0;i < width * res.height; i++) {
	*fptr1 = *rptr1 * *rptr2 - *iptr1 * *iptr2;
	*fptr2 = *rptr1 * *iptr2 + *iptr1 * *rptr2;
	rptr1 += 2; iptr1 += 2;
	rptr2 += 2; iptr2 += 2;
	fptr1 += 2; fptr2 += 2;
  }
}

void complexDivision(fMat a, fMat b, fMat *res)
{
  float *rptr1, *iptr1;
  float *rptr2, *iptr2;
  float *fptr1, *fptr2;
  short width = res->width >> 1;
  float divisor;

  rptr1 = a.data;
  iptr1 = a.data + 1;
  rptr2 = b.data;
  iptr2 = b.data + 1;
  fptr1 = res->data;
  fptr2 = res->data + 1;
  for (short i = 0;i < width * res->height; i++) {
	divisor = *rptr2 * *rptr2 + *iptr2 * *iptr2;
	if (divisor)
	  divisor = 1.0 / divisor;
	else
	  divisor = 0;

	*fptr1 = (*rptr1 * *rptr2 + *iptr1 * *iptr2) * divisor;
	*fptr2 = (*rptr1 * *iptr2 + *iptr1 * *rptr2) * divisor;
	rptr1 += 2; iptr1 += 2;
	rptr2 += 2; iptr2 += 2;
	fptr1 += 2; fptr2 += 2;
  }
}

void rearrange(fMat *img)
{
  short width = img->width;
  short height = img->height;
  short cx = width / 2;
  short cy = height / 2;
  float tmp, *fptr0, *fptr1, *fptr2, *fptr3;

  fptr0 = img->data;
  fptr1 = img->data + cx;
  fptr2 = img->data + cy * width;
  fptr3 = img->data + cy * width + cx;

  for (short i = 0; i < cy; i++) {
	for (short j = 0; j < cx; j++) {
	  tmp = *fptr0;
	  *fptr0 = *fptr3;
	  *fptr3 = tmp;

	  tmp = *fptr1;
	  *fptr1 = *fptr2;
	  *fptr2 = tmp;

	  fptr0++; fptr1++; fptr2++; fptr3++;
	}
	fptr0 += cx;
	fptr1 += cx;
	fptr2 += cx;
	fptr3 += cx;
  }

}

void rearrange_comp(fMat *img)
{
  short width = img->width >> 1;
  short height = img->height;
  short cx = width / 2;
  short cy = height / 2;
  float tmp, *fptr0, *fptr1, *fptr2, *fptr3;
  float *rptr0, *rptr1, *rptr2, *rptr3;

  fptr0 = img->data;
  fptr1 = img->data + cx;
  fptr2 = img->data + cy * width;
  fptr3 = img->data + cy * width + cx;

  rptr0 = img->data + width * height;
  rptr1 = rptr0 + cx;
  rptr2 = rptr0 + cy * width;
  rptr3 = rptr0 + cy * width + cx;

  for (short i = 0; i < cy; i++) {
	for (short j = 0; j < cx; j++) {
	  tmp = *fptr0;
	  *fptr0 = *fptr3;
	  *fptr3 = tmp;

	  tmp = *fptr1;
	  *fptr1 = *fptr2;
	  *fptr2 = tmp;

	  fptr0++; fptr1++; fptr2++; fptr3++;

	  tmp = *rptr0;
	  *rptr0 = *rptr3;
	  *rptr3 = tmp;

	  tmp = *rptr1;
	  *rptr1 = *rptr2;
	  *rptr2 = tmp;

	  rptr0++; rptr1++; rptr2++; rptr3++;
	}
	fptr0 += cx;
	fptr1 += cx;
	fptr2 += cx;
	fptr3 += cx;

	rptr0 += cx;
	rptr1 += cx;
	rptr2 += cx;
	rptr3 += cx;
  }

}

void normalizedLogTransform(fMat *img)
{
  for (short i = 0; i < img->width * img->height; i++)
	img->data[i] = log(1 + img->data[i]);
}

void mulSpectrums_conjugate(fMat src1, fMat src2, fMat *dst) {
  float *rptr1, *iptr1;
  float *rptr2, *iptr2;
  float *fptr1, *fptr2;
  short width = src1.width >> 1;

  rptr1 = src1.data;
  iptr1 = src1.data + 1;
  rptr2 = src2.data;
  iptr2 = src2.data + 1;
  fptr1 = dst->data;
  fptr2 = dst->data + 1;

  for (short i = 0; i < width * src1.height; i++) {
	*fptr1 = *rptr1 * *rptr2 + *iptr1 * *iptr2;
	*fptr2 = *iptr1 * *rptr2 - *rptr1 * *iptr2;
	rptr1 += 2; iptr1 += 2;
	rptr2 += 2; iptr2 += 2;
	fptr1 += 2; fptr2 += 2;
  }
}

#ifdef XLNX_EMBEDDED_PLATFORM
void arm_cmplx_conj_f32(const float32_t * pSrc, float32_t * pDst, uint32_t numSamples)
{
  uint32_t blkCnt;

  float32x4_t zero;
  float32x4x2_t vec;

  zero = vdupq_n_f32(0.0f);

  blkCnt = numSamples >> 2U;

  while (blkCnt > 0U)
  {
	vec = vld2q_f32(pSrc);
	vec.val[1] = vsubq_f32(zero, vec.val[1]);
	vst2q_f32(pDst, vec);

	pSrc += 8;
	pDst += 8;

	blkCnt--;
  }

  blkCnt = numSamples & 0x3;

  while (blkCnt > 0U)
  {
	*pDst++ = *pSrc++;
	*pDst++ = *pSrc++;
	blkCnt--;
  }

}


void arm_cmplx_mult_cmplx_f32(const float32_t * pSrcA, const float32_t * pSrcB, float32_t * pDst, uint32_t numSamples)
{
  uint32_t blkCnt;
  float32_t a, b, c, d;

  float32x4x2_t va, vb;
  float32x4x2_t outCplx;

  blkCnt = numSamples >> 2U;

  while (blkCnt > 0U)
  {
	va = vld2q_f32(pSrcA);
	vb = vld2q_f32(pSrcB);

	pSrcA += 8;
	pSrcB += 8;

	outCplx.val[0] = vmulq_f32(va.val[0], vb.val[0]);
	outCplx.val[0] = vmlsq_f32(outCplx.val[0], va.val[1], vb.val[1]);

	outCplx.val[1] = vmulq_f32(va.val[0], vb.val[1]);
	outCplx.val[1] = vmlaq_f32(outCplx.val[1], va.val[1], vb.val[0]);

	vst2q_f32(pDst, outCplx);

	pDst += 8;

	blkCnt--;
  }

  blkCnt = numSamples & 3;
  while (blkCnt > 0U) {

	a = *pSrcA++;
	b = *pSrcA++;
	c = *pSrcB++;
	d = *pSrcB++;

	*pDst++ = (a * c) - (b * d);
	*pDst++ = (a * d) + (b * c);

	blkCnt--;
  }

}

void arm_mult_f32(const float32_t * pSrcA, const float32_t * pSrcB, float32_t * pDst, uint32_t blockSize)
{
  uint32_t blkCnt;

  float32x4_t vec1;
  float32x4_t vec2;
  float32x4_t res;

  blkCnt = blockSize >> 2U;
  while (blkCnt > 0U)
  {
	vec1 = vld1q_f32(pSrcA);
	vec2 = vld1q_f32(pSrcB);
	res = vmulq_f32(vec1, vec2);
	vst1q_f32(pDst, res);

	pSrcA += 4;
	pSrcB += 4;
	pDst += 4;

	blkCnt--;
  }

  blkCnt = blockSize & 0x3;
  if (blkCnt > 0U)
  {
	*pDst = *pSrcA * *pSrcB;
	pSrcA++;
	pSrcB++;
	pDst++;
	blkCnt--;
  }
}

void arm_mult_sm_f32(fMat *pMatA, const fMat pMatB)
{
  uint32_t blkCnt, t_blkCnt;
  uint32_t rm_blkCnt, t_rm_blkCnt;
  float32_t *pSrcA = pMatA->data;
  float32_t *pSrcB = pMatB.data;

  float32x4_t vec1;
  float32x4_t vec2;

  blkCnt = pMatA->width >> 2U;
  rm_blkCnt = pMatA->width & 0x3;

  for (uint32_t i = 0; i < (unsigned int)pMatA->height; i++) {
	t_blkCnt = blkCnt;
	while (t_blkCnt > 0U)
	{
	  vec1 = vld1q_f32(pSrcA);
	  vec2 = vld1q_f32(pSrcB);
	  vec1 = vmulq_f32(vec1, vec2);
	  vst1q_f32(pSrcA, vec1);

	  pSrcA += 4;
	  pSrcB += 4;

	  t_blkCnt--;
	}

	t_rm_blkCnt = rm_blkCnt;
	if (t_rm_blkCnt > 0U)
	{
	  *pSrcA *= *pSrcB;
	  pSrcA++;
	  pSrcB++;
	  t_rm_blkCnt--;
	}
  }
}

void arm_cmplx_mult_cmplx_flt_resp_f32(const fMat pMatF, const fMat pMatG,
  fMat *pMatA, fMat *pMatB, fMat *pMatH)
{
  uint32_t blkCnt, t_blkCnt;
  uint32_t rm_blkCnt, t_rm_blkCnt;
  float32_t inv_val;
  float32_t *pSrcF = pMatF.data;
  float32_t *pSrcG = pMatG.data;
  float32_t *pSrcA = pMatA->data;
  float32_t *pSrcB = pMatB->data;
  float32_t *pDstH = pMatH->data;
  float32_t F_rptr, F_iptr;
  float32_t G_rptr, G_iptr;
  float32_t A_rptr, A_iptr, B_rptr;
  float32x4_t _eps = vdupq_n_f32(0.0001f);

  float32x4x2_t vf, vg;
  float32x4_t out_den, recipro;
  float32x4x2_t outCplx, flt_resp;

  blkCnt = pMatA->width >> 2U;
  rm_blkCnt = pMatA->width & 0x3;

  for (uint32_t i = 0; i < (unsigned int)pMatA->height; i++) {
	t_blkCnt = blkCnt;
	while (t_blkCnt > 0U)
	{
	  vf = vld2q_f32(pSrcF);
	  vg = vld2q_f32(pSrcG);

	  outCplx.val[0] = vmulq_f32(vf.val[0], vg.val[0]);
	  outCplx.val[0] = vmlaq_f32(outCplx.val[0], vf.val[1], vg.val[1]);

	  outCplx.val[1] = vmulq_f32(vf.val[0], vg.val[1]);
	  outCplx.val[1] = vmlsq_f32(outCplx.val[1], vf.val[1], vg.val[0]);

	  vst2q_f32(pSrcA, outCplx);

	  //fprintf (stdout,"%f %f %f %f %f %f %f %f\n", *pSrcA, *(pSrcA+1), *(pSrcA+2), *(pSrcA+3), *(pSrcA+4), *(pSrcA+5), *(pSrcA+6), *(pSrcA+7));

	  out_den = vmulq_f32(vf.val[0], vf.val[0]);
	  out_den = vmlaq_f32(out_den, vf.val[1], vf.val[1]);

	  vst1q_f32(pSrcB, out_den);

	  out_den = vaddq_f32(out_den, _eps);

	  recipro = vrecpeq_f32(out_den);

	  flt_resp.val[0] = vmulq_f32(outCplx.val[0], recipro);
	  flt_resp.val[1] = vmulq_f32(outCplx.val[1], recipro);
	  vst2q_f32(pDstH, flt_resp);

	  pSrcF += 8;
	  pSrcG += 8;
	  pSrcA += 8;
	  pSrcB += 4;
	  pDstH += 8;

	  t_blkCnt--;
	}

	t_rm_blkCnt = rm_blkCnt;
	while (t_rm_blkCnt > 0U) {
	  F_rptr = *pSrcF++;
	  F_iptr = *pSrcF++;
	  G_rptr = *pSrcG++;
	  G_iptr = *pSrcG++;

	  A_rptr = F_rptr * G_rptr + F_iptr * G_iptr;
	  A_iptr = F_rptr * G_iptr - F_iptr * G_rptr;

	  B_rptr = F_rptr * F_rptr + F_iptr * F_iptr;

	  //if (*B_rptr)
	  inv_val = 1.0 / (B_rptr + 0.0001f);
	  //else
		//inv_val = 1.0;

	  *pDstH++ = A_rptr * inv_val;
	  *pDstH++ = A_iptr * inv_val;
	  *pSrcA++ = A_rptr;
	  *pSrcA++ = A_iptr;
	  *pSrcB++ = B_rptr;

	  t_rm_blkCnt--;
	}
  }
}

void arm_cmplx_mult_cmplx_flt_resp_upate_f32(const fMat pMatF, const fMat pMatG,
  fMat *pMatA, fMat *pMatB, fMat *pMatH, float32_t lrn_rate)
{
  uint32_t blkCnt, t_blkCnt;
  uint32_t rm_blkCnt, t_rm_blkCnt;
  float32_t inv_val;
  float32_t F_rptr, F_iptr;
  float32_t G_rptr, G_iptr;
  float32_t A_rptr, A_iptr, B_rptr;
  float32_t *pSrcF = pMatF.data;
  float32_t *pSrcG = pMatG.data;
  float32_t *pSrcA = pMatA->data;
  float32_t *pSrcB = pMatB->data;
  float32_t *pDstH = pMatH->data;
  float32_t inv_lrn_rate = 1.0 - lrn_rate;
  float32x4_t _lrn_rate = vdupq_n_f32(lrn_rate);
  float32x4_t _inv_lrn_rate = vdupq_n_f32(inv_lrn_rate);
  float32x4_t _eps = vdupq_n_f32(0.0001f);

  float32x4x2_t vf, vg, va;
  float32x4_t vb, out_den, recipro;
  float32x4x2_t outCplx, out_num, flt_resp;

  blkCnt = pMatA->width >> 2U;
  rm_blkCnt = pMatA->width & 0x3;

  for (uint32_t i = 0; i < (unsigned int)pMatA->height; i++) {
	t_blkCnt = blkCnt;
	while (t_blkCnt > 0U)
	{
	  vf = vld2q_f32(pSrcF);
	  vg = vld2q_f32(pSrcG);
	  va = vld2q_f32(pSrcA);
	  vb = vld1q_f32(pSrcB);

	  outCplx.val[0] = vmulq_f32(vf.val[0], vg.val[0]);
	  outCplx.val[0] = vmulq_f32(vmlaq_f32(outCplx.val[0], vf.val[1], vg.val[1]), _lrn_rate);
	  out_num.val[0] = vaddq_f32(outCplx.val[0], vmulq_f32(va.val[0], _inv_lrn_rate));

	  outCplx.val[1] = vmulq_f32(vf.val[0], vg.val[1]);
	  outCplx.val[1] = vmulq_f32(vmlsq_f32(outCplx.val[1], vf.val[1], vg.val[0]), _lrn_rate);
	  out_num.val[1] = vaddq_f32(outCplx.val[1], vmulq_f32(va.val[1], _inv_lrn_rate));

	  vst2q_f32(pSrcA, out_num);

	  out_den = vmulq_f32(vf.val[0], vf.val[0]);
	  out_den = vmulq_f32(vmlaq_f32(out_den, vf.val[1], vf.val[1]), _lrn_rate);
	  out_den = vaddq_f32(out_den, vmulq_f32(vb, _inv_lrn_rate));

	  vst1q_f32(pSrcB, out_den);

	  out_den = vaddq_f32(out_den, _eps);

	  recipro = vrecpeq_f32(out_den);

	  flt_resp.val[0] = vmulq_f32(out_num.val[0], recipro);
	  flt_resp.val[1] = vmulq_f32(out_num.val[1], recipro);
	  vst2q_f32(pDstH, flt_resp);

	  pSrcF += 8;
	  pSrcG += 8;
	  pSrcA += 8;
	  pSrcB += 4;
	  pDstH += 8;

	  t_blkCnt--;
	}

	t_rm_blkCnt = rm_blkCnt;
	while (t_rm_blkCnt > 0U) {
	  F_rptr = *pSrcF++;
	  F_iptr = *pSrcF++;
	  G_rptr = *pSrcG++;
	  G_iptr = *pSrcG++;
	  A_rptr = *pSrcA;
	  A_iptr = *(pSrcA + 1);
	  B_rptr = *pSrcB;


	  A_rptr = (F_rptr * G_rptr + F_iptr * G_iptr) * lrn_rate + A_rptr * inv_lrn_rate;
	  A_iptr = (F_rptr * G_iptr - F_iptr * G_rptr) * lrn_rate + A_iptr * inv_lrn_rate;

	  B_rptr = (F_rptr * F_rptr + F_iptr * F_iptr) * lrn_rate + B_rptr * inv_lrn_rate;

	  inv_val = 1.0 / (B_rptr + 0.0001f);

	  *pDstH++ = A_rptr * inv_val;
	  *pDstH++ = A_iptr * inv_val;
	  *pSrcA++ = A_rptr;
	  *pSrcA++ = A_iptr;
	  *pSrcB++ = B_rptr;

	  t_rm_blkCnt--;
	}
  }
}

void arm_cmplx_mag_f32(const float32_t * pSrc, float32_t * pDst, uint32_t numSamples)
{
  uint32_t blkCnt;
  float32_t real, imag;


  float32x4x2_t vecA;
  float32x4_t vRealA;
  float32x4_t vImagA;
  float32x4_t vMagSqA;

  float32x4x2_t vecB;
  float32x4_t vRealB;
  float32x4_t vImagB;
  float32x4_t vMagSqB;

  blkCnt = numSamples >> 3;

  while (blkCnt > 0U)
  {
	vecA = vld2q_f32(pSrc);
	pSrc += 8;

	vecB = vld2q_f32(pSrc);
	pSrc += 8;

	vRealA = vmulq_f32(vecA.val[0], vecA.val[0]);
	vImagA = vmulq_f32(vecA.val[1], vecA.val[1]);
	vMagSqA = vaddq_f32(vRealA, vImagA);

	vRealB = vmulq_f32(vecB.val[0], vecB.val[0]);
	vImagB = vmulq_f32(vecB.val[1], vecB.val[1]);
	vMagSqB = vaddq_f32(vRealB, vImagB);

	vst1q_f32(pDst, vsqrtq_f32(vMagSqA));
	pDst += 4;

	vst1q_f32(pDst, vsqrtq_f32(vMagSqB));
	pDst += 4;

	blkCnt--;
  }

  blkCnt = numSamples & 7;
  while (blkCnt > 0U)
  {
	real = *pSrc++;
	imag = *pSrc++;

	*pDst = sqrt((real * real) + (imag * imag));
	pDst++;

	blkCnt--;
  }

}
#endif

void get_subwindow(Mat_img frame, Rect extracted_roi, Mat_img *z) {
  short i, j;
  Rect img_window;
  short stx, sty;
  unsigned char *img_ptr;
  unsigned char *reg_ptr;

  memset(z->data, 0, z->width * z->height);

  if (extracted_roi.x < 0) {
	img_window.x = 0;
	stx = -extracted_roi.x;
	img_window.width = extracted_roi.width - stx;
  }
  else {
	img_window.x = extracted_roi.x;
	stx = 0;
	img_window.width = extracted_roi.width;
  }

  if ((img_window.x + img_window.width) > frame.width)
	img_window.width = frame.width - img_window.x;

  if (extracted_roi.y < 0) {
	img_window.y = 0;
	sty = -extracted_roi.y;
	img_window.height = extracted_roi.height - sty;
  }
  else {
	img_window.y = extracted_roi.y;
	sty = 0;
	img_window.height = extracted_roi.height;
  }

  if ((img_window.y + img_window.height) > frame.height)
	img_window.height = frame.height - img_window.y;

  reg_ptr = z->data + sty * z->width + stx;

  if (frame.channels == 1) {
	img_ptr = frame.data + img_window.y * frame.width + img_window.x;
	for (i = 0; i < img_window.height; i++) {
	  memcpy(reg_ptr, img_ptr, img_window.width);
	  img_ptr += frame.width;
	  reg_ptr += z->width;
	}
  }
  else if (frame.channels == 3) {
	img_ptr = frame.data + (img_window.y * frame.width + img_window.x) * 3;
	for (i = 0; i < img_window.height; i++) {
	  for (j = 0; j < img_window.width; j++) {
		reg_ptr[j] = (img_ptr[j * 3] + img_ptr[j * 3 + 1] + img_ptr[j * 3 + 2]) / 3;
	  }
	  img_ptr += (frame.width * 3);
	  reg_ptr += z->width;
	}
  }
}