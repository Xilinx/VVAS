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

#ifndef __SYNTHESIS__
#include <stdio.h>
#endif
#include <assert.h>
#include "v_multi_scaler.h"

#define MAX_DATA_WIDTH 		HSC_BITS_PER_COMPONENT

#if (INPUT_INTERFACE == AXIMM_INTERFACE)
void AXIMMvideo2Bytes(AXIMM srcImg, STREAM_BYTES &srcPlane0,
#if ((MAX_NR_PLANES==2) || (MAX_NR_PLANES==3))
		AXIMM srcImg1, STREAM_BYTES &srcPlane1,
#if (MAX_NR_PLANES==3)
		AXIMM srcImg2, STREAM_BYTES &srcPlane2,
#endif
#endif
		const U16 Height, const U16 WidthIn, const U16 WidthInBytes, const U16 StrideInBytes,
		const U8 VideoFormat)
{
	int loopwidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;

	if (VideoFormat == Y_U_V8_420)
	{
#if (HAS_Y_U_V8_420==1)
		int offsetY = 0;
		int offsetUv = 0;
		ap_uint<AXIMM_DATA_WIDTH> fb_pix;
loop_AXIMMvideo2BytesYuv_3planes:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopwidth; x++)
			{
#pragma HLS pipeline II=1
				fb_pix = srcImg[offsetY + x];
				srcPlane0 << fb_pix;
			}
			offsetY += StrideInBytes / (AXIMM_DATA_WIDTH8);

			if (!(y & 1))
			{
				for (int x = 0; x < loopwidth / 2; x++)
				{
#pragma HLS PIPELINE II=1
					fb_pix = srcImg1[offsetUv + x];
					srcPlane1 << fb_pix;
				}
				for (int x = 0; x < loopwidth / 2; x++)
				{
#pragma HLS PIPELINE II=1
					fb_pix = srcImg2[offsetUv + x];
					srcPlane2 << fb_pix;
				}
				offsetUv += StrideInBytes / (2 * AXIMM_DATA_WIDTH8);
			}
		}
#endif
	}
	else if (VideoFormat == R_G_B8)
	{
#if (HAS_R_G_B8==1)
		int offset = 0;
		ap_uint<AXIMM_DATA_WIDTH> fb_pix;
loop_AXIMMvideo2BytesRgb_3planes:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopwidth; x++)
			{
#pragma HLS pipeline II=1
				fb_pix = srcImg[offset + x];
				srcPlane0 << fb_pix;
			}
			for (int x = 0; x < loopwidth; x++)
			{
#pragma HLS pipeline II=1
				fb_pix = srcImg1[offset + x];
				srcPlane1 << fb_pix;
			}
			for (int x = 0; x < loopwidth; x++)
			{
#pragma HLS pipeline II=1
				fb_pix = srcImg2[offset + x];
				srcPlane2 << fb_pix;
			}
			offset += StrideInBytes / (AXIMM_DATA_WIDTH8);
		}
#endif
	}
	else
	{
		int offsetY = 0;
		int offsetUv = 0;
loop_AXIMMvideo2Bytes_2Or3Planes:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopwidth; x++)
			{
#pragma HLS pipeline II=1
				ap_uint<AXIMM_DATA_WIDTH> fb_pix = srcImg[offsetY + x];
				srcPlane0 << fb_pix;
			}
			offsetY += StrideInBytes / AXIMM_DATA_WIDTH8;

#if ((MAX_NR_PLANES==2) || (MAX_NR_PLANES==3))
			if (NR_PLANES(VideoFormat) == 2 && (!(y & 1) || !IS_420(VideoFormat)))
			{
				for (int x = 0; x < loopwidth; x++)
				{
#pragma HLS pipeline II=1
					ap_uint<AXIMM_DATA_WIDTH> fb_pix = srcImg1[offsetUv + x];
					srcPlane1 << fb_pix;
				}
				offsetUv += StrideInBytes / AXIMM_DATA_WIDTH8;
			}
#endif
		}
	}
}

void Bytes2MultiPixStream(STREAM_BYTES &srcPlane0,
#if ((MAX_NR_PLANES==2) || (MAX_NR_PLANES==3))
		STREAM_BYTES &srcPlane1,
#endif
#if (MAX_NR_PLANES==3)
		STREAM_BYTES &srcPlane2,
#endif
		HSC_STREAM_MULTIPIX &img, const U16 Height, const U16 Width, const U16 WidthInBytes,
		const U16 StrideInBytes, const U8 VideoFormat)
{
	if (VideoFormat == R_G_B8)
	{
#if (HAS_R_G_B8==1)
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes;
		int remainPix = widthInPix % AXIMM_DATA_WIDTH8;
		remainPix = (remainPix == 0) ? AXIMM_DATA_WIDTH8 : remainPix;
loop_Bytes2MulPxStrm_R_G_B8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
			YUV_MULTI_PIXEL pix;
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=8
				ap_uint<AXIMM_DATA_WIDTH> rd_r, rd_g, rd_b;
				srcPlane0 >> rd_r;
				srcPlane1 >> rd_g;
				srcPlane2 >> rd_b;
				for (int i = 0; i < AXIMM_DATA_WIDTH8 / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = (rd_r(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8) << (MAX_DATA_WIDTH - 8));
						pix.val[1 + k * HSC_NR_COMPONENTS] = (rd_g(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8) << (MAX_DATA_WIDTH - 8));
						pix.val[2 + k * HSC_NR_COMPONENTS] = (rd_b(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8) << (MAX_DATA_WIDTH - 8));
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
					{
						img << pix;
					}
				}
			}
		}

#endif
	}
	else if (VideoFormat == Y_U_V8_420)
	{
#if (HAS_Y_U_V8_420 == 1)
		ap_uint<AXIMM_DATA_WIDTH_420> y_fb_pix;
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes;
		int remainPix = widthInPix % (4 * (AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? (4 * (AXIMM_DATA_WIDTH / 32)) : remainPix;

#if (HSC_SAMPLES_PER_CLOCK==1)
		ap_uint<AXIMM_DATA_WIDTH_420> u_fb_pix, v_fb_pix;
#else
		ap_uint<AXIMM_DATA_WIDTH_420 / 2> u_fb_pix, v_fb_pix;
#endif
loop_Bytes2MulPxStrm_Y_U_V8_420:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
			YUV_MULTI_PIXEL pix;
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
				/* In this loop HSC_SAMPLES_PER_CLOCK pixels will be processed. So the loop must have to be width/Samples per clock */
#pragma HLS pipeline II=8
				ap_uint<AXIMM_DATA_WIDTH> pixY, pixU, pixV;
				srcPlane0 >> pixY;
				if (!(y & 1) && !(x & 1))
				{
					srcPlane1 >> pixU;
					srcPlane2 >> pixV;
				}
				else if (y & 1)
				{
					pixU = 0;
					pixV = 0;
				}
				for (unsigned int i = 0; i < AXIMM_DATA_WIDTH8 / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = (pixY(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8) << (MAX_DATA_WIDTH - 8));
					}
#if (HSC_SAMPLES_PER_CLOCK==1)
					if (!(i & 1))
					{
						pix.val[1] = (pixU(((x & 1) * 4 + (i >> 1)) * HSC_SAMPLES_PER_CLOCK * 8 + 7,
								((x & 1) * 4 + (i >> 1)) * HSC_SAMPLES_PER_CLOCK * 8)
								<< (MAX_DATA_WIDTH - 8));
					}
					else
					{
						pix.val[1] = (pixV(((x & 1) * 4 + HSC_SAMPLES_PER_CLOCK * (i >> 1)) * 8 + 7,
								((x & 1) * 4 + HSC_SAMPLES_PER_CLOCK * (i >> 1)) * 8)
								<< (MAX_DATA_WIDTH - 8));
					}
#elif (HSC_SAMPLES_PER_CLOCK==2)
					pix.val[1] = (pixU(
							((x & 1) * 4 + (i >> 1)) * HSC_SAMPLES_PER_CLOCK * 8 + 7,
							((x & 1) * 4 + (i >> 1)) * HSC_SAMPLES_PER_CLOCK * 8)
							<< (MAX_DATA_WIDTH - 8));
					pix.val[1 + 1 * HSC_NR_COMPONENTS] = (pixV(
							((x & 1) * 4 + (i >> 1)) * HSC_SAMPLES_PER_CLOCK * 8 + 7,
							((x & 1) * 4 + (i >> 1)) * HSC_SAMPLES_PER_CLOCK * 8)
							<< (MAX_DATA_WIDTH - 8));
#elif (HSC_SAMPLES_PER_CLOCK==4)
					pix.val[1] = (pixU(((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8 + 7,
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8)
							<< (MAX_DATA_WIDTH - 8));
					pix.val[1 + 1 * HSC_NR_COMPONENTS] = (pixV(
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8 + 7,
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8)
							<< (MAX_DATA_WIDTH - 8));

					pix.val[1 + 2 * HSC_NR_COMPONENTS] = (pixU(
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8 + 15,
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8 + 8)
							<< (MAX_DATA_WIDTH - 8));
					pix.val[1 + 3 * HSC_NR_COMPONENTS] = (pixV(
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8 + 15,
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8 + 8)
							<< (MAX_DATA_WIDTH - 8));
#endif

					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
					{
						img << pix;
					}
				}
			}
		}
#endif
	}
	else if (VideoFormat == Y_UV8 || VideoFormat == Y_UV8_420)
	{
#if (HAS_Y_UV8_Y_UV8_420==1)
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes;
		int remainPix = widthInPix % (4 * (AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? (4 * (AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_Y_UV8_Y_UV8_420:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
			YUV_MULTI_PIXEL pix;
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=8
				ap_uint<AXIMM_DATA_WIDTH> rd;
				srcPlane0 >> rd;
				ap_uint<AXIMM_DATA_WIDTH> rdUv;
				if (!(y & 1) || !IS_420(VideoFormat))
					srcPlane1 >> rdUv;

				for (int i = 0; i < 4 * (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8) << (MAX_DATA_WIDTH - 8));
						pix.val[1 + k * HSC_NR_COMPONENTS] = (rdUv(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8) << (MAX_DATA_WIDTH - 8));
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
	else if (VideoFormat == Y_UV10 || VideoFormat == Y_UV10_420)
	{
#if (HAS_Y_UV10_Y_UV10_420==1)
		// Optimized Y_UV10, Y_UV10_420 implementation
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = Width;
		int remainPix = widthInPix % (3 * (AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? (3 * (AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_Y_UV10_Y_UV10_420:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320

			YUV_MULTI_PIXEL pix;

#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=6
				ap_uint<AXIMM_DATA_WIDTH> rd_raw;
				ap_uint<AXIMM_DATA_WIDTH> rd;
				srcPlane0 >> rd_raw;
				for (int j = 0; j < (AXIMM_DATA_WIDTH / 32); j++)
				{
					rd((30 * j) + 29, 30 * j) = rd_raw((32 * j) + 29, 32 * j);
				}

				ap_uint<AXIMM_DATA_WIDTH> rdUv_raw;
				ap_uint<AXIMM_DATA_WIDTH> rdUv;
				if (!(y & 1) || !IS_420(VideoFormat))
				{
					srcPlane1 >> rdUv_raw;
					for (int j = 0; j < (AXIMM_DATA_WIDTH / 32); j++)
					{
						rdUv((30 * j) + 29, 30 * j) = rdUv_raw((32 * j) + 29, 32 * j);
					}
				}

				for (int i = 0; i < 3 * (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 10 + 9,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 10);
						pix.val[1 + k * HSC_NR_COMPONENTS] = rdUv(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 10 + 9,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 10);
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
	else if (VideoFormat == YUYV8)
	{
#if (HAS_YUYV8==1)
		// Optimized YUYV8 implementation
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes / 2;
		int remainPix = widthInPix % (2 * (AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? (2 * (AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_YUYV8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320

			YUV_MULTI_PIXEL pix;

#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=4
				ap_uint<AXIMM_DATA_WIDTH> rd;
				srcPlane0 >> rd;

				for (int i = 0; i < 2 * (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 16 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 16) << (MAX_DATA_WIDTH - 8));
						pix.val[1 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 16 + 15,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 16 + 8) << (MAX_DATA_WIDTH - 8));
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
	else if (VideoFormat == UYVY8)
	{
#if (HAS_UYVY8==1)
		// Optimized UYVY8 implementation
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes / 2;
		int remainPix = widthInPix % (2 * (AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? (2 * (AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_UYVY8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
			YUV_MULTI_PIXEL pix;

#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=4
				ap_uint<AXIMM_DATA_WIDTH> rd;
				srcPlane0 >> rd;

				for (int i = 0; i < 2 * (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[1 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 16 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 16) << (MAX_DATA_WIDTH - 8));
						pix.val[0 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 16 + 15,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 16 + 8) << (MAX_DATA_WIDTH - 8));
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
	else if (VideoFormat == RGB8 || VideoFormat == YUV8)
	{
#if (HAS_RGB8_YUV8==1)
		// Optimized RGB8, YUV8 implementation
		int loopWidth1 = ((WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8);
		int loopWidth = ((loopWidth1 + 2) / 3);
		int remainTrx = loopWidth1 % 3;
		int widthInPix = Width;
		int remainPix = widthInPix % (4 * (AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? (4 * (AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_RGB8_YUV8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
			ap_uint<AXIMM_DATA_WIDTH> rd0, rd1, rd2;
			YUV_MULTI_PIXEL pix;

#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=8
				ap_uint<AXIMM_DATA_WIDTH * 3> rd;
				srcPlane0 >> rd0;
				rd(AXIMM_DATA_WIDTH - 1, 0) = rd0;
				if (x < loopWidth - 1 || remainTrx != 1)
					srcPlane0 >> rd1;
				rd(2 * AXIMM_DATA_WIDTH - 1, AXIMM_DATA_WIDTH) = rd1;
				if (x < loopWidth - 1 || remainTrx == 0)
					srcPlane0 >> rd2;
				rd(3 * AXIMM_DATA_WIDTH - 1, 2 * AXIMM_DATA_WIDTH) = rd2;

				for (int i = 0; i < 4 * (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 24 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 24));
						pix.val[1 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 24 + 15,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 24 + 8));
						pix.val[2 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 24 + 23,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 24 + 16));
						pix.val[0 + k * HSC_NR_COMPONENTS] = pix.val[0 + k * HSC_NR_COMPONENTS]
								<< (MAX_DATA_WIDTH - 8);
						pix.val[1 + k * HSC_NR_COMPONENTS] = pix.val[1 + k * HSC_NR_COMPONENTS]
								<< (MAX_DATA_WIDTH - 8);
						pix.val[2 + k * HSC_NR_COMPONENTS] = pix.val[2 + k * HSC_NR_COMPONENTS]
								<< (MAX_DATA_WIDTH - 8);
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
	else if (VideoFormat == BGR8)
	{
#if (HAS_BGR8==1)
		// Optimized BGR8 implementation
		int loopWidth1 = ((WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8);
		int loopWidth = ((loopWidth1 + 2) / 3);
		int remainTrx = loopWidth1 % 3;
		int widthInPix = Width;
		int remainPix = widthInPix % (4 * (AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? (4 * (AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_BGR8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320

			ap_uint<AXIMM_DATA_WIDTH> rd0, rd1, rd2;
			YUV_MULTI_PIXEL pix;

#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=8
				ap_uint<AXIMM_DATA_WIDTH * 3> rd;
				srcPlane0 >> rd0;
				rd(AXIMM_DATA_WIDTH - 1, 0) = rd0;
				if (x < loopWidth - 1 || remainTrx != 1)
					srcPlane0 >> rd1;
				rd(2 * AXIMM_DATA_WIDTH - 1, AXIMM_DATA_WIDTH) = rd1;
				if (x < loopWidth - 1 || remainTrx == 0)
					srcPlane0 >> rd2;
				rd(3 * AXIMM_DATA_WIDTH - 1, 2 * AXIMM_DATA_WIDTH) = rd2;

				for (int i = 0; i < 4 * (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[2 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 24 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 24));
						pix.val[1 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 24 + 15,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 24 + 8));
						pix.val[0 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 24 + 23,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 24 + 16));
						pix.val[0 + k * HSC_NR_COMPONENTS] = pix.val[0 + k * HSC_NR_COMPONENTS]
								<< (MAX_DATA_WIDTH - 8);
						pix.val[1 + k * HSC_NR_COMPONENTS] = pix.val[1 + k * HSC_NR_COMPONENTS]
								<< (MAX_DATA_WIDTH - 8);
						pix.val[2 + k * HSC_NR_COMPONENTS] = pix.val[2 + k * HSC_NR_COMPONENTS]
								<< (MAX_DATA_WIDTH - 8);
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
	else if (VideoFormat == RGBX8 || VideoFormat == YUVX8)
	{
#if (HAS_RGBX8_YUVX8==1)
		// Optimized RGBX8, YUVX8 implementation
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes / 4;
		int remainPix = widthInPix % ((AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? ((AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_RGBX8_YUVX8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
			YUV_MULTI_PIXEL pix;

#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=2
				ap_uint<AXIMM_DATA_WIDTH> rd;
				srcPlane0 >> rd;
				for (int i = 0; i < (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32) << (MAX_DATA_WIDTH - 8));
						pix.val[1 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 15,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 8) << (MAX_DATA_WIDTH - 8));
						pix.val[2 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 23,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 16) << (MAX_DATA_WIDTH - 8));
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
	else if (VideoFormat == RGBX10 || VideoFormat == YUVX10)
	{
#if (HAS_RGBX10_YUVX10==1)
		// Optimized RGBX10, YUVX10 implementation
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes / 4;
		int remainPix = widthInPix % ((AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? ((AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_RGBX10_YUVX10:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320

			YUV_MULTI_PIXEL pix;

#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=2
				ap_uint<AXIMM_DATA_WIDTH> rd;
				srcPlane0 >> rd;
				for (int i = 0; i < (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 9,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32);
						pix.val[1 + k * HSC_NR_COMPONENTS] = rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 19,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 10);
						pix.val[2 + k * HSC_NR_COMPONENTS] = rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 29,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 20);
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
	else if (VideoFormat == Y8)
	{
#if (HAS_Y8==1)
		// Optimized Y8 implementation
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes;
		int remainPix = widthInPix % (4 * (AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? (4 * (AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_Y8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320

			YUV_MULTI_PIXEL pix;

#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=8
				ap_uint<AXIMM_DATA_WIDTH> rd;
				srcPlane0 >> rd;

				for (int i = 0; i < 4 * (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 8) << (MAX_DATA_WIDTH - 8));
						pix.val[1 + k * HSC_NR_COMPONENTS] = (128 << (MAX_DATA_WIDTH - 8));
						pix.val[2 + k * HSC_NR_COMPONENTS] = (128 << (MAX_DATA_WIDTH - 8));
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
	else if (VideoFormat == Y10)
	{
#if (HAS_Y10==1)
		// Optimized Y10 implementation
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = Width;
		int remainPix = widthInPix % (3 * (AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? (3 * (AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_Y10:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320

			YUV_MULTI_PIXEL pix;

#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=6
				ap_uint<AXIMM_DATA_WIDTH> rd_raw;
				ap_uint<AXIMM_DATA_WIDTH> rd;
				srcPlane0 >> rd_raw;
				for (int j = 0; j < (AXIMM_DATA_WIDTH / 32); j++)
				{
					rd((30 * j) + 29, 30 * j) = rd_raw((32 * j) + 29, 32 * j);
				}

				for (int i = 0; i < 3 * (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 10 + 9,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 10);
						pix.val[1 + k * HSC_NR_COMPONENTS] = 512;
						pix.val[2 + k * HSC_NR_COMPONENTS] = 512;
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
	else if (VideoFormat == RGBA8 || VideoFormat == YUVA8)
	{
#if (HAS_RGBA8_YUVA8==1)
		// Optimized RGBA8, YUVA8 implementation
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes / 4;
		int remainPix = widthInPix % ((AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? ((AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_RGBA8_YUVA8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
			YUV_MULTI_PIXEL pix;
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=2
				ap_uint<AXIMM_DATA_WIDTH> rd;
				srcPlane0 >> rd;
				for (int i = 0; i < (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32) << (MAX_DATA_WIDTH - 8));
						pix.val[1 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 15,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 8) << (MAX_DATA_WIDTH - 8));
						pix.val[2 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 23,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 16) << (MAX_DATA_WIDTH - 8));
						pix.val[3 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 31,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 24) << (MAX_DATA_WIDTH - 8));
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
	else if (VideoFormat == BGRA8)
	{
#if (HAS_BGRA8==1)
		// Optimized BGRA8 implementation
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes / 4;
		int remainPix = widthInPix % ((AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? ((AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_BGRA8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320

			YUV_MULTI_PIXEL pix;
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=2
				ap_uint<AXIMM_DATA_WIDTH> rd;
				srcPlane0 >> rd;
				for (int i = 0; i < (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 23,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 16) << (MAX_DATA_WIDTH - 8));
						pix.val[1 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 15,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 8) << (MAX_DATA_WIDTH - 8));
						pix.val[2 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32) << (MAX_DATA_WIDTH - 8));
						pix.val[3 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 31,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 24) << (MAX_DATA_WIDTH - 8));
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
	else if (VideoFormat == BGRX8)
	{
#if (HAS_BGRX8==1)
		// Optimized BGRX8 implementation
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes / 4;
		int remainPix = widthInPix % ((AXIMM_DATA_WIDTH / 32));
		remainPix = (remainPix == 0) ? ((AXIMM_DATA_WIDTH / 32)) : remainPix;

loop_BGRX8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
			YUV_MULTI_PIXEL pix;

#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS PIPELINE II=2
				ap_uint<AXIMM_DATA_WIDTH> rd;
				srcPlane0 >> rd;
				for (int i = 0; i < (AXIMM_DATA_WIDTH / 32) / HSC_SAMPLES_PER_CLOCK; i++)
				{
					for (int k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
					{
						pix.val[0 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 23,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 16) << (MAX_DATA_WIDTH - 8));
						pix.val[1 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 15,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 8) << (MAX_DATA_WIDTH - 8));
						pix.val[2 + k * HSC_NR_COMPONENTS] = (rd(
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32 + 7,
								(HSC_SAMPLES_PER_CLOCK * i + k) * 32) << (MAX_DATA_WIDTH - 8));
					}
					if (x < loopWidth - 1 || i < remainPix / HSC_SAMPLES_PER_CLOCK)
						img << pix;
				}
			}
		}
#endif
	}
}
#endif /* end of if (INPUT_INTERFACE == AXIMM_INTERFACE) */

#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
void MultiPixStream2Bytes(HSC_STREAM_MULTIPIX &StrmMPix, STREAM_BYTES &dstPlane0,
#if ((MAX_NR_PLANES==2) || (MAX_NR_PLANES==3))
		STREAM_BYTES &dstPlane1,
#endif
#if (MAX_NR_PLANES == 3)
		STREAM_BYTES &dstPlane2,
#endif
		U16 Height, U16 WidthInPix, U16 WidthInBytes, U16 StrideInBytes, U8 VideoFormat)
{
	if (VideoFormat == R_G_B8)
	{
#if (HAS_R_G_B8==1)
		YUV_MULTI_PIXEL pix;
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes;
		int remainPix = widthInPix % AXIMM_DATA_WIDTH8;
		remainPix = (remainPix == 0) ? 8 : (remainPix / HSC_SAMPLES_PER_CLOCK);
		ap_uint<AXIMM_DATA_WIDTH8> in_pix_r, in_pix_g, in_pix_b = 0;
		ap_uint<AXIMM_DATA_WIDTH> out_pix_r, out_pix_g, out_pix_b;

loop_MultPixStrm2Bytes_R_G_B8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=8
				for (int i = 0; i < AXIMM_DATA_WIDTH8 / HSC_SAMPLES_PER_CLOCK; i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						in_pix_r(8 * (l + 1) - 1, 8 * l) = pix.val[0 + l * HSC_NR_COMPONENTS]
								>> (MAX_DATA_WIDTH - 8);
						in_pix_g(8 * (l + 1) - 1, 8 * l) = pix.val[1 + l * HSC_NR_COMPONENTS]
								>> (MAX_DATA_WIDTH - 8);
						in_pix_b(8 * (l + 1) - 1, 8 * l) = pix.val[2 + l * HSC_NR_COMPONENTS]
								>> (MAX_DATA_WIDTH - 8);
					}
					out_pix_r((AXIMM_DATA_WIDTH8 * (i + 1)) - 1, (AXIMM_DATA_WIDTH8 * i)) =
							in_pix_r;
					out_pix_g((AXIMM_DATA_WIDTH8 * (i + 1)) - 1, (AXIMM_DATA_WIDTH8 * i)) =
							in_pix_g;
					out_pix_b((AXIMM_DATA_WIDTH8 * (i + 1)) - 1, (AXIMM_DATA_WIDTH8 * i)) =
							in_pix_b;
				}
				dstPlane0 << out_pix_r;
				dstPlane1 << out_pix_g;
				dstPlane2 << out_pix_b;
			}
		}
#endif
	}
	else if (VideoFormat == Y_U_V8_420)
	{
#if (HAS_Y_U_V8_420 ==1)
		YUV_MULTI_PIXEL pix;
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes;
		int remainPix = widthInPix % AXIMM_DATA_WIDTH8;
		remainPix =
				(remainPix == 0) ?
						(AXIMM_DATA_WIDTH / (8 * HSC_SAMPLES_PER_CLOCK)) :
						(remainPix / HSC_SAMPLES_PER_CLOCK);
		ap_uint<8 * HSC_SAMPLES_PER_CLOCK> in_pix_Y = 0;
		ap_uint<AXIMM_DATA_WIDTH> pixY = 0, pixU = 0, pixV = 0;

loop_MultPixStrm2Bytes_Y_U_V8_420:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=8
				for (int i = 0; i < AXIMM_DATA_WIDTH8 / HSC_SAMPLES_PER_CLOCK; i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						in_pix_Y(8 * (l + 1) - 1, 8 * l) = pix.val[0 + l * HSC_NR_COMPONENTS]
								>> (MAX_DATA_WIDTH - 8);
					}
#if (HSC_SAMPLES_PER_CLOCK==1)
					if (!(i & 1))
					{
						pixU(((x & 1) * 4 + (i >> 1)) * HSC_SAMPLES_PER_CLOCK * 8 + 7,
								((x & 1) * 4 + (i >> 1)) * HSC_SAMPLES_PER_CLOCK * 8) = (pix.val[1]
								>> (MAX_DATA_WIDTH - 8));
					}
					else
					{
						pixV(((x & 1) * 4 + (i >> 1)) * HSC_SAMPLES_PER_CLOCK * 8 + 7,
								((x & 1) * 4 + (i >> 1)) * HSC_SAMPLES_PER_CLOCK * 8) = (pix.val[1]
								>> (MAX_DATA_WIDTH - 8));
					}
#elif (HSC_SAMPLES_PER_CLOCK==2)
					pixU(((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + i) * 8 + 7,
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + i) * 8) = (pix.val[1]
							>> (MAX_DATA_WIDTH - 8));
					pixV(((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + i) * 8 + 7,
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + i) * 8) = (pix.val[1
							+ 1 * HSC_NR_COMPONENTS] >> (MAX_DATA_WIDTH - 8));
#elif (HSC_SAMPLES_PER_CLOCK==4)
					pixU(((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8 + 7,
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8) = (pix.val[1]
							>> (MAX_DATA_WIDTH - 8));
					pixV(((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8 + 7,
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8) = (pix.val[1
							+ 1 * HSC_NR_COMPONENTS] >> (MAX_DATA_WIDTH - 8));

					pixU(((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8 + 15,
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8 + 8) = (pix.val[1
							+ 2 * HSC_NR_COMPONENTS] >> (MAX_DATA_WIDTH - 8));
					pixV(((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8 + 15,
							((x & 1) * 4 * HSC_SAMPLES_PER_CLOCK + (i << 1)) * 8 + 8) = (pix.val[1
							+ 3 * HSC_NR_COMPONENTS] >> (MAX_DATA_WIDTH - 8));
#endif

					pixY((8 * HSC_SAMPLES_PER_CLOCK * (i + 1)) - 1, (8 * HSC_SAMPLES_PER_CLOCK * i)) =
							in_pix_Y;
				}
				dstPlane0 << pixY;
				if (!(y & 1) && (x & 1))
				{
					dstPlane1 << pixU;
					dstPlane2 << pixV;
				}
			}
		}
#endif
	}
	else if (VideoFormat == RGBX8 || VideoFormat == YUVX8)
	{
#if (HAS_RGBX8_YUVX8==1)
		YUV_MULTI_PIXEL pix;
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes / 4;
		int remainPix = widthInPix % (AXIMM_DATA_WIDTH / 32);
		remainPix =
				(remainPix == 0) ?
						(AXIMM_DATA_WIDTH / (32 * HSC_SAMPLES_PER_CLOCK)) :
						(remainPix / HSC_SAMPLES_PER_CLOCK);

		ap_uint<24> in_pix = 0;
		ap_uint<AXIMM_DATA_WIDTH> out_pix = 0;

loop_RGBX8_YUVX8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=4
				for (int i = 0; i < (AXIMM_DATA_WIDTH / (32 * HSC_SAMPLES_PER_CLOCK)); i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						for (int k = 0; k < HSC_NR_COMPONENTS; k++)
						{
							in_pix((k + 1) * 8 - 1, k * 8) = pix.val[k + l * HSC_NR_COMPONENTS]
									>> (MAX_DATA_WIDTH - 8);
						}
						out_pix((32 * HSC_SAMPLES_PER_CLOCK * i) + (l * 32) + 23,
								(32 * HSC_SAMPLES_PER_CLOCK * i) + (l * 32)) = in_pix;
					}
				}
				dstPlane0 << out_pix;
			}
		}
#endif
	}
	else if (VideoFormat == BGRX8)
	{
#if (HAS_BGRX8==1)
		YUV_MULTI_PIXEL pix;
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes / 4;
		int remainPix = widthInPix % (AXIMM_DATA_WIDTH / 32);
		remainPix =
				(remainPix == 0) ?
						(AXIMM_DATA_WIDTH / (32 * HSC_SAMPLES_PER_CLOCK)) :
						(remainPix / HSC_SAMPLES_PER_CLOCK);

		ap_uint<24> in_pix = 0;
		ap_uint<AXIMM_DATA_WIDTH> out_pix = 0;

loop_BGRX8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=4
				for (int i = 0; i < (AXIMM_DATA_WIDTH / (32 * HSC_SAMPLES_PER_CLOCK)); i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						in_pix(7, 0) = pix.val[2 + l * HSC_NR_COMPONENTS] >> (MAX_DATA_WIDTH - 8);
						in_pix(15, 8) = pix.val[1 + l * HSC_NR_COMPONENTS] >> (MAX_DATA_WIDTH - 8);
						in_pix(23, 16) = pix.val[0 + l * HSC_NR_COMPONENTS] >> (MAX_DATA_WIDTH - 8);
						out_pix((32 * HSC_SAMPLES_PER_CLOCK * i) + (l * 32) + 23,
								(32 * HSC_SAMPLES_PER_CLOCK * i) + (l * 32)) = in_pix;
					}
				}
				dstPlane0 << out_pix;
			}
		}
#endif
	}
	else if (VideoFormat == RGBX10 || VideoFormat == YUVX10)
	{
#if (HAS_RGBX10_YUVX10==1)
		YUV_MULTI_PIXEL pix;
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes / 4;
		int remainPix = widthInPix % (AXIMM_DATA_WIDTH / 32);
		remainPix =
				(remainPix == 0) ?
						(AXIMM_DATA_WIDTH / (32 * HSC_SAMPLES_PER_CLOCK)) :
						(remainPix / HSC_SAMPLES_PER_CLOCK);

		ap_uint<30> in_pix = 0;
		ap_uint<AXIMM_DATA_WIDTH> out_pix = 0;

loop_RGBX10_YUVX10:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=4
				for (int i = 0; i < (AXIMM_DATA_WIDTH / (32 * HSC_SAMPLES_PER_CLOCK)); i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						for (int k = 0; k < HSC_NR_COMPONENTS; k++)
						{
							in_pix((k + 1) * 10 - 1, k * 10) = pix.val[k + l * HSC_NR_COMPONENTS];
						}
						out_pix((32 * HSC_SAMPLES_PER_CLOCK * i) + (l * 32) + 29,
								(32 * HSC_SAMPLES_PER_CLOCK * i) + (l * 32)) = in_pix;
					}
				}
				dstPlane0 << out_pix;
			}
		}
#endif
	}
	else if (VideoFormat == YUYV8)
	{
#if (HAS_YUYV8==1)
		YUV_MULTI_PIXEL pix;
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes / 2;
		int remainPix = widthInPix % (AXIMM_DATA_WIDTH / 16);
		remainPix =
				(remainPix == 0) ?
						(AXIMM_DATA_WIDTH / (16 * HSC_SAMPLES_PER_CLOCK)) :
						(remainPix / HSC_SAMPLES_PER_CLOCK);

		ap_uint<16 * HSC_SAMPLES_PER_CLOCK> in_pix = 0;
		ap_uint<AXIMM_DATA_WIDTH> out_pix = 0;

loop_YUYV8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=4
				for (int i = 0; i < (AXIMM_DATA_WIDTH / (16 * HSC_SAMPLES_PER_CLOCK)); i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						for (int k = 0; k < 2; k++)
						{
							in_pix(16 * l + (k + 1) * 8 - 1, 16 * l + k * 8) = pix.val[k
									+ l * HSC_NR_COMPONENTS] >> (MAX_DATA_WIDTH - 8);
						}
					}
					out_pix((16 * HSC_SAMPLES_PER_CLOCK * (i + 1)) - 1,
							(16 * HSC_SAMPLES_PER_CLOCK * i)) = in_pix;
				}
				dstPlane0 << out_pix;
			}
		}
#endif
	}
	else if (VideoFormat == UYVY8)
	{
#if (HAS_UYVY8==1)
		YUV_MULTI_PIXEL pix;
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes / 2;
		int remainPix = widthInPix % (AXIMM_DATA_WIDTH / 16);
		remainPix =
				(remainPix == 0) ?
						(AXIMM_DATA_WIDTH / (16 * HSC_SAMPLES_PER_CLOCK)) :
						(remainPix / HSC_SAMPLES_PER_CLOCK);

		ap_uint<16 * HSC_SAMPLES_PER_CLOCK> in_pix = 0;
		ap_uint<AXIMM_DATA_WIDTH> out_pix = 0;

loop_UYVY8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=4
				for (int i = 0; i < (AXIMM_DATA_WIDTH / (16 * HSC_SAMPLES_PER_CLOCK)); i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						in_pix(16 * l + 7, 16 * l) = pix.val[1 + l * HSC_NR_COMPONENTS]
								>> (MAX_DATA_WIDTH - 8);
						in_pix(16 * l + 15, 16 * l + 8) = pix.val[0 + l * HSC_NR_COMPONENTS]
								>> (MAX_DATA_WIDTH - 8);
					}
					out_pix((16 * HSC_SAMPLES_PER_CLOCK * (i + 1)) - 1,
							(16 * HSC_SAMPLES_PER_CLOCK * i)) = in_pix;
				}
				dstPlane0 << out_pix;
			}
		}
#endif
	}
	else if (VideoFormat == Y_UV8 || VideoFormat == Y_UV8_420)
	{
#if (HAS_Y_UV8_Y_UV8_420==1)
		YUV_MULTI_PIXEL pix;
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes;
		int remainPix = widthInPix % AXIMM_DATA_WIDTH8;
		remainPix =
				(remainPix == 0) ?
						(AXIMM_DATA_WIDTH / (8 * HSC_SAMPLES_PER_CLOCK)) :
						(remainPix / HSC_SAMPLES_PER_CLOCK);

		ap_uint<8 * HSC_SAMPLES_PER_CLOCK> in_pix_Y = 0, in_pix_UV = 0;
		ap_uint<AXIMM_DATA_WIDTH> pixY = 0, pixUv = 0;

loop_Y_UV8_Y_UV8_420:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=8
				for (int i = 0; i < (AXIMM_DATA_WIDTH / (8 * HSC_SAMPLES_PER_CLOCK)); i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						in_pix_Y(8 * (l + 1) - 1, 8 * l) = pix.val[0 + l * HSC_NR_COMPONENTS]
								>> (MAX_DATA_WIDTH - 8);
						in_pix_UV(8 * (l + 1) - 1, 8 * l) = pix.val[1 + l * HSC_NR_COMPONENTS]
								>> (MAX_DATA_WIDTH - 8);
					}
					pixY((8 * HSC_SAMPLES_PER_CLOCK * (i + 1)) - 1, (8 * HSC_SAMPLES_PER_CLOCK * i)) =
							in_pix_Y;
					pixUv((8 * HSC_SAMPLES_PER_CLOCK * (i + 1)) - 1,
							(8 * HSC_SAMPLES_PER_CLOCK * i)) = in_pix_UV;
				}
				dstPlane0 << pixY;
				if (!(y & 1) || !IS_420(VideoFormat))
					dstPlane1 << pixUv;
			}
		}
#endif
	}
	else if (VideoFormat == Y_UV10 || VideoFormat == Y_UV10_420)
	{
#if (HAS_Y_UV10_Y_UV10_420==1)
		YUV_MULTI_PIXEL pix;
		//assumes AXIMM_DATA_WIDTH contains a multiple of HSC_SAMPLES_PER_CLOCK pixels (for example, 2 pix/clk needs at least 64 bits)
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInPix;
		int remainPix = widthInPix % ((AXIMM_DATA_WIDTH * 3) / 32);
		remainPix =
				(remainPix == 0) ?
						((AXIMM_DATA_WIDTH * 3) / (32 * HSC_SAMPLES_PER_CLOCK)) :
						(remainPix / HSC_SAMPLES_PER_CLOCK);

		ap_uint<10 * HSC_SAMPLES_PER_CLOCK> in_pix_Y = 0, in_pix_UV = 0;
		ap_uint<AXIMM_DATA_WIDTH> raw_pix_Y = 0, raw_pix_UV = 0;
		ap_uint<AXIMM_DATA_WIDTH> pixY = 0, pixUv = 0;

loop_Y_UV10_Y_UV10_420:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=6
				for (int i = 0; i < ((AXIMM_DATA_WIDTH * 3) / (32 * HSC_SAMPLES_PER_CLOCK)); i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						in_pix_Y(10 * (l + 1) - 1, 10 * l) = pix.val[0 + l * HSC_NR_COMPONENTS];
						in_pix_UV(10 * (l + 1) - 1, 10 * l) = pix.val[1 + l * HSC_NR_COMPONENTS];
					}
					raw_pix_Y((10 * HSC_SAMPLES_PER_CLOCK * (i + 1)) - 1,
							(10 * HSC_SAMPLES_PER_CLOCK * i)) = in_pix_Y;
					raw_pix_UV((10 * HSC_SAMPLES_PER_CLOCK * (i + 1)) - 1,
							(10 * HSC_SAMPLES_PER_CLOCK * i)) = in_pix_UV;
				}
				for (int j = 0; j < (AXIMM_DATA_WIDTH / 32); j++)
				{
					pixY((32 * j) + 29, 32 * j) = raw_pix_Y((30 * j) + 29, 30 * j);
					pixUv((32 * j) + 29, 32 * j) = raw_pix_UV((30 * j) + 29, 30 * j);
				}
				dstPlane0 << pixY;
				if (!(y & 1) || !IS_420(VideoFormat))
					dstPlane1 << pixUv;
			}
		}
#endif
	}
	else if (VideoFormat == RGB8 || VideoFormat == YUV8)
	{
#if (HAS_RGB8_YUV8==1)
		YUV_MULTI_PIXEL pix;
		//int loopWidth = ((WidthInBytes+((AXIMM_DATA_WIDTH*3)/8)-1)/((AXIMM_DATA_WIDTH*3)/8));
		int loopWidth1 = ((WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8);
		int loopWidth = ((loopWidth1 + 2) / 3);
		int widthInPix = WidthInPix;    //WidthInBytes/3
		int remPix = widthInPix % ((3 * AXIMM_DATA_WIDTH) / 24);
		int remainPix =
				(remPix == 0) ?
						((AXIMM_DATA_WIDTH * 3) / (24 * HSC_SAMPLES_PER_CLOCK)) :
						(remPix / HSC_SAMPLES_PER_CLOCK);
		int remainTrx =
				(remPix == 0) ? 3 : ((remPix * 24) + (AXIMM_DATA_WIDTH - 1)) / AXIMM_DATA_WIDTH;

		ap_uint<24 * HSC_SAMPLES_PER_CLOCK> in_pix = 0;
		ap_uint<AXIMM_DATA_WIDTH * 3> out_pix = 0;

loop_RGB8_YUV8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=8
				for (int i = 0; i < ((AXIMM_DATA_WIDTH * 3) / (24 * HSC_SAMPLES_PER_CLOCK)); i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						for (int k = 0; k < HSC_NR_COMPONENTS; k++)
						{
							in_pix(24 * l + (k + 1) * 8 - 1, 24 * l + k * 8) = pix.val[k
									+ l * HSC_NR_COMPONENTS] >> (MAX_DATA_WIDTH - 8);
						}
					}
					out_pix((24 * HSC_SAMPLES_PER_CLOCK * (i + 1)) - 1,
							(24 * HSC_SAMPLES_PER_CLOCK * i)) = in_pix;
				}
				for (int j = 0; j < 3; j++)
				{
					if (x < loopWidth - 1 || j < remainTrx)
						dstPlane0 << out_pix(AXIMM_DATA_WIDTH * (j + 1) - 1, AXIMM_DATA_WIDTH * j);
				}
			}
		}
#endif
	}
	else if (VideoFormat == BGR8)
	{
#if (HAS_BGR8==1)
		YUV_MULTI_PIXEL pix;
		int loopWidth1 = ((WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8);
		int loopWidth = ((loopWidth1 + 2) / 3);
		int widthInPix = WidthInPix;    //WidthInBytes/3
		int remPix = widthInPix % ((3 * AXIMM_DATA_WIDTH) / 24);
		int remainPix =
				(remPix == 0) ?
						((AXIMM_DATA_WIDTH * 3) / (24 * HSC_SAMPLES_PER_CLOCK)) :
						(remPix / HSC_SAMPLES_PER_CLOCK);
		int remainTrx =
				(remPix == 0) ? 3 : ((remPix * 24) + (AXIMM_DATA_WIDTH - 1)) / AXIMM_DATA_WIDTH;

		ap_uint<24 * HSC_SAMPLES_PER_CLOCK> in_pix = 0;
		ap_uint<AXIMM_DATA_WIDTH * 3> out_pix = 0;

loop_BGR8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=8
				for (int i = 0; i < ((AXIMM_DATA_WIDTH * 3) / (24 * HSC_SAMPLES_PER_CLOCK)); i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						for (int k = 0; k < HSC_NR_COMPONENTS; k++)
						{
							in_pix(24 * l + (k + 1) * 8 - 1, 24 * l + k * 8) =
									pix.val[(HSC_NR_COMPONENTS - 1 - k) + l * HSC_NR_COMPONENTS]
											>> (MAX_DATA_WIDTH - 8);
						}
					}
					out_pix((24 * HSC_SAMPLES_PER_CLOCK * (i + 1)) - 1,
							(24 * HSC_SAMPLES_PER_CLOCK * i)) = in_pix;
				}
				for (int j = 0; j < 3; j++)
				{
					if (x < loopWidth - 1 || j < remainTrx)
						dstPlane0 << out_pix(AXIMM_DATA_WIDTH * (j + 1) - 1, AXIMM_DATA_WIDTH * j);
				}
			}
		}
#endif
	}
	else if (VideoFormat == Y8)
	{
#if (HAS_Y8==1)
		YUV_MULTI_PIXEL pix;
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInBytes;
		int remainPix = widthInPix % AXIMM_DATA_WIDTH8;
		remainPix =
				(remainPix == 0) ?
						(AXIMM_DATA_WIDTH / (8 * HSC_SAMPLES_PER_CLOCK)) :
						(remainPix / HSC_SAMPLES_PER_CLOCK);

		ap_uint<8 * HSC_SAMPLES_PER_CLOCK> in_pix = 0;
		ap_uint<AXIMM_DATA_WIDTH> out_pix;

loop_Y8:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=8
				for (int i = 0; i < (AXIMM_DATA_WIDTH / (8 * HSC_SAMPLES_PER_CLOCK)); i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						in_pix(8 * (l + 1) - 1, 8 * l) = pix.val[0 + l * HSC_NR_COMPONENTS]
								>> (MAX_DATA_WIDTH - 8);
					}
					out_pix((8 * HSC_SAMPLES_PER_CLOCK * (i + 1)) - 1,
							(8 * HSC_SAMPLES_PER_CLOCK * i)) = in_pix;
				}
				dstPlane0 << out_pix;
			}
		}
#endif
	}
	else if (VideoFormat == Y10)
	{
#if (HAS_Y10==1)
		YUV_MULTI_PIXEL pix;
		//assumes AXIMM_DATA_WIDTH contains a multiple of HSC_SAMPLES_PER_CLOCK pixels (for example, 2 pix/clk needs at least 64 bits)
		int loopWidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;
		int widthInPix = WidthInPix;
		int remainPix = widthInPix % ((AXIMM_DATA_WIDTH * 3) / 32);
		remainPix =
				(remainPix == 0) ?
						((AXIMM_DATA_WIDTH * 3) / (32 * HSC_SAMPLES_PER_CLOCK)) :
						(remainPix / HSC_SAMPLES_PER_CLOCK);

		ap_uint<10 * HSC_SAMPLES_PER_CLOCK> in_pix = 0;
		ap_uint<AXIMM_DATA_WIDTH> raw_pix = 0;
		ap_uint<AXIMM_DATA_WIDTH> out_pix = 0;

loop_Y10:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopWidth; x++)
			{
#pragma HLS pipeline II=6
				for (int i = 0; i < ((AXIMM_DATA_WIDTH * 3) / (32 * HSC_SAMPLES_PER_CLOCK)); i++)
				{
					if (x < loopWidth - 1 || i < remainPix)
						StrmMPix >> pix;
					for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
					{
						in_pix(10 * (l + 1) - 1, 10 * l) = pix.val[0 + l * HSC_NR_COMPONENTS];
					}
					raw_pix((10 * HSC_SAMPLES_PER_CLOCK * (i + 1)) - 1,
							(10 * HSC_SAMPLES_PER_CLOCK * i)) = in_pix;
				}
				for (int j = 0; j < (AXIMM_DATA_WIDTH / 32); j++)
				{
					out_pix((32 * j) + 29, 32 * j) = raw_pix((30 * j) + 29, 30 * j);
				}
				dstPlane0 << out_pix;
			}
		}
#endif
	}
}

void Bytes2AXIMMvideo(STREAM_BYTES &dstPlane0, AXIMM dstImg,
#if ((MAX_NR_PLANES == 2) || (MAX_NR_PLANES == 3))
		STREAM_BYTES &dstPlane1, AXIMM dstImg1,
#if(MAX_NR_PLANES == 3)
		STREAM_BYTES &dstPlane2, AXIMM dstImg2,
#endif
#endif
		U16 Height, U16 WidthOut, U16 WidthInBytes, U16 StrideInBytes, U8 VideoFormat)
{
	int loopwidth = (WidthInBytes + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8;

	if (VideoFormat == R_G_B8)
	{
#if (HAS_R_G_B8 == 1)
		int offset = 0;
		ap_uint<AXIMM_DATA_WIDTH> fb_pix;

loop_Bytes2AXIMMvideo_R_G_B_planes:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopwidth; x++)
			{
#pragma HLS pipeline II=1
				dstPlane0 >> fb_pix;
				dstImg[offset + x] = fb_pix;
			}
			for (int x = 0; x < loopwidth; x++)
			{
#pragma HLS pipeline II=1
				dstPlane1 >> fb_pix;
				dstImg1[offset + x] = fb_pix;
			}
			for (int x = 0; x < loopwidth; x++)
			{
#pragma HLS pipeline II=1
				dstPlane2 >> fb_pix;
				dstImg2[offset + x] = fb_pix;
			}
			offset += StrideInBytes / AXIMM_DATA_WIDTH8;
		}
#endif
	}
	else if (VideoFormat == Y_U_V8_420)
	{
#if (HAS_Y_U_V8_420 == 1)
		int offsetY = 0;
		int offsetUv = 0;
		ap_uint<AXIMM_DATA_WIDTH> fb_pix;

loop_Bytes2AXIMMvideo_Y_U_V8_420_planes:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopwidth; x++)
			{
#pragma HLS pipeline II=1
				dstPlane0 >> fb_pix;
				dstImg[offsetY + x] = fb_pix;
			}
			offsetY += StrideInBytes / AXIMM_DATA_WIDTH8;

			if (!(y & 1))
			{
				for (int x = 0; x < loopwidth / 2; x++)
				{
#pragma HLS pipeline II=1
					dstPlane1 >> fb_pix;
					dstImg1[offsetUv + x] = fb_pix;
				}
				for (int x = 0; x < loopwidth / 2; x++)
				{
#pragma HLS pipeline II=1
					dstPlane2 >> fb_pix;
					dstImg2[offsetUv + x] = fb_pix;
				}
				offsetUv += StrideInBytes / (2 * AXIMM_DATA_WIDTH8);
			}
		}
#endif
	}
	else
	{
		int offsetY = 0;
		int offsetUv = 0;
		ap_uint<AXIMM_DATA_WIDTH> fb_pix;

loop_Bytes2AXIMMvideo_3planes:
		for (int y = 0; y < Height; y++)
		{
#pragma HLS loop_tripcount max=4320
#pragma HLS loop_flatten off
			for (int x = 0; x < loopwidth; x++)
			{
#pragma HLS pipeline II=1
				dstPlane0 >> fb_pix;
				dstImg[offsetY + x] = fb_pix;
			}
			offsetY += StrideInBytes / AXIMM_DATA_WIDTH8;

#if ((MAX_NR_PLANES==2) || (MAX_NR_PLANES==3))
			if (NR_PLANES(VideoFormat) == 2 && (!(y & 1) || !IS_420(VideoFormat)))
			{
				for (int x = 0; x < loopwidth; x++)
				{
#pragma HLS pipeline II=1
					dstPlane1 >> fb_pix;
					dstImg1[offsetUv + x] = fb_pix;
				}
				offsetUv += StrideInBytes / AXIMM_DATA_WIDTH8;
			}
#endif
		}
	}
}
#endif /* end of if (OUTPUT_INTERFACE == AXIMM_INTERFACE) */

#if (INPUT_INTERFACE == AXI_STREAM_INTERFACE)
/************************************************************************************
* Function:    AXIvideo2MultiPixStream
* Parameters:  Multiple Pixel AXI Stream, User Stream, Image Resolution
* Return:      None
* Description: Read data from multiple pixel/clk AXI stream into user defined
*              stream
**************************************************************************************/
int AXIvideo2MultiPixStream(HSC_AXI_STREAM_IN& AXI_video_strm,
	HSC_STREAM_MULTIPIX& img,
	U16 Height,
	U16 WidthIn,
	U8 colorMode
)
{
	int res = 0;
	ap_axiu<(HSC_BITS_PER_CLOCK), 1, 1, 1> axi;
	YUV_MULTI_PIXEL pix;

	int rows = Height;
	int cols = WidthIn;
	assert(rows <= HSC_MAX_HEIGHT);
	assert(cols <= HSC_MAX_WIDTH);
	assert(rows >= MIN_PIXELS);
	assert(cols >= MIN_PIXELS);

	assert(cols % HSC_SAMPLES_PER_CLOCK == 0);

	int depth = HSC_BITS_PER_COMPONENT; //BitsPerCol;

	bool sof = 0;
	//#pragma HLS array_partition complete dim=0 variable=&pix.p
loop_wait_for_start:
	while (!sof) {
#pragma HLS pipeline II=1
#pragma HLS loop_tripcount avg=0 max=0
		AXI_video_strm >> axi;
		sof = axi.user.to_int();
	}
loop_height:
	for (int i = 0; i < rows; i++) {

		bool eol = 0;
	loop_width:
		for (int j = 0; j < cols / HSC_SAMPLES_PER_CLOCK; j++)
		{
#pragma HLS loop_flatten off
#pragma HLS pipeline II=1
			if (sof || eol)
			{
				sof = 0;
			}
			else
			{
				AXI_video_strm >> axi;
			}
			eol = axi.last.to_int();
			if (eol && (j != cols / HSC_SAMPLES_PER_CLOCK - 1))
			{
				// will work only for integral values of image width to samplesperclk
				res |= ERROR_IO_EOL_EARLY;
			}
			for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
			{
				for (HLS_CHANNEL_T k = 0; k < HSC_NR_COMPONENTS; k++)
				{
					ap_uint<HSC_BITS_PER_COMPONENT> pix_rgb, pix_444, pix_422;
					const int map[3] = { 2, 0, 1 };
#pragma HLS ARRAY_PARTITION variable=&map complete dim=0
					switch (colorMode)
					{
					case 0x0:
						hls::AXIGetBitFields(axi, (map[k] + l * 3) * depth, depth, pix_rgb);
						pix.val[k + l * HSC_NR_COMPONENTS] = pix_rgb;
						break;
					case 0x1:
						hls::AXIGetBitFields(axi, (k + l * 3) * depth, depth, pix_444);
						pix.val[k + l * HSC_NR_COMPONENTS] = pix_444;
						break;
					default:
						hls::AXIGetBitFields(axi, (k + l * 2) * depth, depth, pix_422);
						pix.val[k + l * HSC_NR_COMPONENTS] = pix_422;
						break;
					}
				}
			}
			img << pix;
		}
	loop_wait_for_eol:
		while (!eol)
		{
#pragma HLS pipeline II=1
#pragma HLS loop_tripcount avg=0 max=0
			// Keep reading until we get to EOL
			AXI_video_strm >> axi;
			eol = axi.last.to_int();
			res |= ERROR_IO_EOL_LATE;
		}
	}
	return res;
}
#endif /* end of if (INPUT_INTERFACE == AXI_STREAM_INTERFACE) */

#if(OUTPUT_INTERFACE == AXI_STREAM_INTERFACE)
/*********************************************************************************
* Function:    MultiPixStream2AXIvideo
* Parameters:  Multi Pixel Stream, AXI Video Stream, Image Resolution
* Return:      None
* Description: Convert a m pixel/clk stream to AXI Video
*              (temporary function until official hls:video library is updated
*               to provide the required functionality)
**********************************************************************************/
int MultiPixStream2AXIvideo(HSC_STREAM_MULTIPIX& StrmMPix,
	HSC_AXI_STREAM_OUT& AXI_video_strm,
	U16 Height, U16 WidthOut, U8 ColorMode)
{
	int res = 0;
	YUV_MULTI_PIXEL pix;

	ap_axiu<(HSC_BITS_PER_CLOCK), 1, 1, 1> axi;
	int depth = HSC_BITS_PER_COMPONENT; //BitsPerCol;

	int rows = Height;
	int cols = WidthOut;
	assert(rows <= HSC_MAX_HEIGHT);
	assert(cols <= HSC_MAX_WIDTH);
	assert(rows >= MIN_PIXELS);
	assert(cols >= MIN_PIXELS);
	assert(cols % HSC_SAMPLES_PER_CLOCK == 0);

#if (HSC_SAMPLES_PER_CLOCK == 1)
	const ap_uint<5> mapComp[4][3] = {
										{1,  2,  0},     //RGB
										{0,  1,  2},     //4:4:4
										{0,  1,  2},     //4:2:2
										{0,  1,  2}      //4:2:0
	};
#endif
#if (HSC_SAMPLES_PER_CLOCK == 2)
	const ap_uint<5> mapComp[4][6] = {
										{1,  2,  0,  4,  5,  3},     //RGB
										{0,  1,  2,  3,  4,  5},     //4:4:4
										{0,  1,  3,  4,  5,  2},     //4:2:2
										{0,  1,  3,  4,  5,  2}      //4:2:0
	};
#endif
#if (HSC_SAMPLES_PER_CLOCK == 4)
	const ap_uint<5> mapComp[4][12] = {
										{1,  2,  0,  4,  5,  3,  7,  8,  6, 10, 11,  9},     //RGB
										{0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11},     //4:4:4
										{0,  1,  3,  4,  6,  7,  9, 10, 11,  8,  5,  2},     //4:2:2
										{0,  1,  3,  4,  6,  7,  9, 10, 11,  8,  5,  2}      //4:2:0
	};
#endif

#if (HSC_SAMPLES_PER_CLOCK == 8)
	const ap_uint<5> mapComp[4][24] = {
										{1,  2,  0,  4,  5,  3,  7,  8,  6, 10, 11,  9, 13, 14, 12, 16, 17, 15, 19, 20, 18, 22, 23, 21},     //RGB
										{0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23},     //4:4:4
										{0,  1,  3,  4,  6,  7,  9, 10, 12, 13, 15, 16, 18, 19, 21, 22, 23, 20, 17, 14, 11,  8,  5,  2},     //4:2:2
										{0,  1,  3,  4,  6,  7,  9, 10, 12, 13, 15, 16, 18, 19, 21, 22, 23, 20, 17, 14, 11,  8,  5,  2}     //4:2:0
	};
#endif

	ap_uint<5> map[HSC_NR_COMPONENTS*HSC_SAMPLES_PER_CLOCK];
#pragma HLS ARRAY_PARTITION variable=&map complete dim=0
	for (int i = 0; i < (HSC_NR_COMPONENTS*HSC_SAMPLES_PER_CLOCK); i++)
	{
		map[i] = mapComp[ColorMode][i];
	}

	bool sof = 1;
	for (int i = 0; i < rows; i++)
	{
		//#pragma HLS loop_tripcount max=2160
		for (int j = 0; j < cols / HSC_SAMPLES_PER_CLOCK; j++)
		{
			//#pragma HLS loop_tripcount max=3840 //+SmplsPerClk
#pragma HLS loop_flatten off
#pragma HLS pipeline II=1
			if (sof)
			{
				axi.user = 1;
				sof = 0;
			}
			else
			{
				axi.user = 0;
			}
			if (j == (cols / HSC_SAMPLES_PER_CLOCK - 1))
			{
				axi.last = 1;
			}
			else
			{
				axi.last = 0;
			}
			StrmMPix >> pix;
			axi.data = -1;


			for (int l = 0; l < HSC_SAMPLES_PER_CLOCK; l++)
			{
				for (int k = 0; k < HSC_NR_COMPONENTS; k++)
				{
					ap_uint<5> kMap = map[k + l * HSC_NR_COMPONENTS];
					int start = (k + l * HSC_NR_COMPONENTS) * depth;
					axi.data(start + depth - 1, start) = pix.val[kMap];
				}
			}
			axi.keep = -1;

			AXI_video_strm << axi;
		}
	}
	return res;
}
#endif /* if(OUTPUT_INTERFACE == AXI_STREAM_INTERFACE) */
