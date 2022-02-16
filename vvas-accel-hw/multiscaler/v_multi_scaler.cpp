#ifndef __SYNTHESIS__
#include <stdio.h>
#endif
#include <stdio.h>
#include <assert.h>
#include "v_multi_scaler.h"

//#define DBG_PRINT
const U8 rgb = 0;
const U8 yuv444 = 1;
const U8 yuv422 = 2;
const U8 yuv420 = 3;

enum ops { mean_sub, scale_n_bias_mean_sub };

static void v_scaler_top(
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM srcImgBuf0,
#else
		HSC_AXI_STREAM_IN& s_axis_vid,
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM dstImgBuf0,
#else
		HSC_AXI_STREAM_OUT& m_axis_vid,
#endif
#if ((MAX_NR_PLANES == 2) || (MAX_NR_PLANES == 3))
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM srcImgBuf1,
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM dstImgBuf1,
#endif
#if (MAX_NR_PLANES == 3)
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM srcImgBuf2,
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM dstImgBuf2,
#endif
#endif
#endif
		U16 HeightIn, U16 HeightOut, U16 WidthIn, U16 WidthOut,
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
		U16 StrideIn,
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
		U16 StrideOut,
#endif
		U8 InPixelFmt, U8 OutPixelFmt, U32 PixelRate, U32 LineRate,
#if (HSC_SCALE_MODE == HSC_POLYPHASE)
		I16 hfltCoeff[HSC_PHASES][HSC_TAPS][HSC_SAMPLES_PER_CLOCK],
#endif
#if (VSC_SCALE_MODE == VSC_POLYPHASE)
	I16 vfltCoeff[VSC_PHASES][VSC_TAPS],
#endif
#if (NORMALIZATION == 1)
	int params[2 * 3],
#endif
		HSC_PHASE_CTRL blkmm_phasesH[HSC_MAX_WIDTH / HSC_SAMPLES_PER_CLOCK]);

void preProcessKernel(HSC_STREAM_MULTIPIX& srcStrm,
                        HSC_STREAM_MULTIPIX& dstStrm,
                        int alpha_reg[3],
                        int beta_reg[3],
                        int loop_count, int HeightOut, int WidthOut, int ColorModeOut) {

I16 yOffset;
YUV_MULTI_PIXEL in_pix, out_pix;
yOffset = 0;
I16 out_y, out_x;
I16 y,x,k;
I16 bPassThru = 0;
//Do pre-processing when the color mode is BGR/RGB
if(ColorModeOut == rgb)
{
	bPassThru = 0;
} else {
	bPassThru = 1;
}

 for(y=0; y<HeightOut; ++y)
    {

        for(x=0; x< WidthOut / HSC_SAMPLES_PER_CLOCK; ++x)
        {

#pragma HLS LOOP_FLATTEN OFF
#pragma HLS PIPELINE II=1
            srcStrm >> in_pix;
        	for(int i = 0; i < HSC_SAMPLES_PER_CLOCK; i++) {
        		for (int j = 0; j < HSC_NR_COMPONENTS; j++) {
        			ap_uint<8> x = in_pix.val[i * HSC_NR_COMPONENTS + j];

        			int a = alpha_reg[j];
        			int b = beta_reg[j];
        			int out;

        			switch (OPMODE) {
                     	 case mean_sub: {
                     		 out = x - a;
                                 out = out >> 16;
                     	 } break;

                     	 case scale_n_bias_mean_sub: {
                     		 int prod3 = (x - a) * b;

                     		 out = prod3;
                                 out = out >> 16;
                     	 } break;
        			}

        			ap_uint<HSC_BITS_PER_COMPONENT>* out_val;

        			out_val = (ap_uint<HSC_BITS_PER_COMPONENT>*)&out;

        			out_pix.val[i * HSC_NR_COMPONENTS + j] = *out_val;
        		}
        	}
            dstStrm << (bPassThru ? in_pix : out_pix);
        }
   }
}
static void calc_phaseH(U16 WidthIn, U16 WidthOut, U32 PixelRate, HSC_PHASE_CTRL *blkmm_phasesH,
		ap_uint<1> done_flag)
{
	unsigned int loopWidth = (MAX(WidthIn, WidthOut) + (HSC_SAMPLES_PER_CLOCK - 1))
			/ HSC_SAMPLES_PER_CLOCK;
	int offset = 0;
	unsigned int xWritePos = 0;
	ap_uint<1> OutputWriteEn;
	ap_uint<1> GetNewPix;
	ap_uint<6> PhaseH;
	ap_uint<3> arrayIdx;

	arrayIdx = 0;
	for (int x = 0; x < loopWidth; x++)
	{
#pragma HLS loop_flatten off
		blkmm_phasesH[x] = 0;
		for (int s = 0; s < HSC_SAMPLES_PER_CLOCK; s++)
		{
#pragma HLS PIPELINE off
			PhaseH = (offset >> (STEP_PRECISION_SHIFT - HSC_PHASE_SHIFT)) & (HSC_PHASES - 1);
			GetNewPix = 0;
			OutputWriteEn = 0;
			if ((offset >> STEP_PRECISION_SHIFT) != 0)
			{
				// read a new input sample
				GetNewPix = 1;
				offset = offset - (1 << STEP_PRECISION_SHIFT);
				OutputWriteEn = 0;
				arrayIdx++;
			}
			if (((offset >> STEP_PRECISION_SHIFT) == 0) && (xWritePos < (U32) WidthOut))
			{
				// produce a new output sample
				offset += PixelRate;
				OutputWriteEn = 1;
				xWritePos++;
			}
			blkmm_phasesH[x](HSC_PHASE_CTRL_PHASE_MSB + s * HSC_PHASE_CTRL_BITS,
			HSC_PHASE_CTRL_PHASE_LSB + s * HSC_PHASE_CTRL_BITS) = PhaseH;
			blkmm_phasesH[x](HSC_PHASE_CTRL_INDEX_MSB + s * HSC_PHASE_CTRL_BITS,
			HSC_PHASE_CTRL_INDEX_LSB + s * HSC_PHASE_CTRL_BITS) = arrayIdx;
			blkmm_phasesH[x][HSC_PHASE_CTRL_ENABLE_LSB + s * HSC_PHASE_CTRL_BITS] = OutputWriteEn;
		}

		done_flag = 1;
		if (arrayIdx >= HSC_SAMPLES_PER_CLOCK)
			arrayIdx &= (HSC_SAMPLES_PER_CLOCK - 1);
	}
}

#if DEBUG
static void write_debug_variables(V_SCALER_TOP_STRUCT Multi_Sc, U64 start_addr, AXIMM maxi_src)
{
	ap_uint<AXIMM_DATA_WIDTH> aximm_debug;
	U32 debug_arr_size, i, j, k;
	debug_arr_size = (AXIMM_DATA_WIDTH8 / 4)*READ_LENGTH_DESC;
	U64 readOffset;
	readOffset = start_addr / AXIMM_DATA_WIDTH8;
	k = 0;
	for (i = READ_LENGTH_DESC - DEBUG_VARS/(2*HSC_SAMPLES_PER_CLOCK); i < READ_LENGTH_DESC; i++)
	{
#pragma HLS loop_flatten off
		for (j = 0; j < (AXIMM_DATA_WIDTH8 / 4); j++)
		{
#pragma HLS PIPELINE off
			aximm_debug(j * 32 + 31, j * 32) = Multi_Sc.debug_var[k + j];
		}
		k = k + 2*HSC_SAMPLES_PER_CLOCK;
		maxi_src[readOffset + i] = aximm_debug;
	}
}
#endif

static void GetMultiScAndCoeff(HSC_HW_STRUCT_REG &HwReg, V_SCALER_TOP_STRUCT &Multi_Sc
#if (HSC_SCALE_MODE == HSC_POLYPHASE)
		, I16 hfltCoeff[HSC_PHASES][HSC_TAPS][HSC_SAMPLES_PER_CLOCK],
		I16 vfltCoeff[VSC_PHASES][VSC_TAPS]
#endif
#if (NORMALIZATION == 1)
	, int params[2 * 3]
#endif
		)
{
#pragma HLS inline
#define U32_VALUE_FROM_AXIMM_ARRAY(x)	u32TempArray[x]
#define U64_VALUE_FROM_AXIMM_ARRAY(x)	((U64)u32TempArray[x] + ((U64)u32TempArray[x+1]<<32))
#define READ_LENGTH_DESCRIPTOR 	((sizeof(V_SCALER_TOP_STRUCT) + AXIMM_DATA_WIDTH8 - 1) / AXIMM_DATA_WIDTH8)
#define READ_LENGTH_COEFF_H		(HSC_PHASES*HSC_TAPS*16/AXIMM_DATA_WIDTH)
#define READ_LENGTH_COEFF_V		(VSC_PHASES*VSC_TAPS*16/AXIMM_DATA_WIDTH)

	U32 i, j, k, u32TempArray[(AXIMM_DATA_WIDTH8 / 4) * READ_LENGTH_DESCRIPTOR], xOffset, yOffset;
	U64 readOffset, readOffset2;
	ap_uint<AXIMM_DATA_WIDTH> aximmTemp, aximmTemp2;

	readOffset = HwReg.start_addr / AXIMM_DATA_WIDTH8;
	for (i = 0; i < READ_LENGTH_DESCRIPTOR; i++)
	{
#pragma HLS loop_flatten off
		aximmTemp = HwReg.ms_maxi_srcbuf[readOffset + i];
		for (j = 0; j < (AXIMM_DATA_WIDTH8 / 4); j++)
		{
#pragma HLS PIPELINE off
			u32TempArray[(i * AXIMM_DATA_WIDTH8) / 4 + j] = aximmTemp(j * 32 + 31, j * 32);
		}
	}

	Multi_Sc.msc_widthIn = U32_VALUE_FROM_AXIMM_ARRAY(0);
	Multi_Sc.msc_widthOut = U32_VALUE_FROM_AXIMM_ARRAY(1);
	Multi_Sc.msc_heightIn = U32_VALUE_FROM_AXIMM_ARRAY(2);
	Multi_Sc.msc_heightOut = U32_VALUE_FROM_AXIMM_ARRAY(3);
	Multi_Sc.msc_lineRate = U32_VALUE_FROM_AXIMM_ARRAY(4);
	Multi_Sc.msc_pixelRate = U32_VALUE_FROM_AXIMM_ARRAY(5);
	Multi_Sc.msc_inPixelFmt = U32_VALUE_FROM_AXIMM_ARRAY(6);
	Multi_Sc.msc_outPixelFmt = U32_VALUE_FROM_AXIMM_ARRAY(7);
	Multi_Sc.msc_strideIn = U32_VALUE_FROM_AXIMM_ARRAY(8);
	Multi_Sc.msc_strideOut = U32_VALUE_FROM_AXIMM_ARRAY(9);
	Multi_Sc.msc_srcImgBuf0 = U64_VALUE_FROM_AXIMM_ARRAY(10);
	Multi_Sc.msc_srcImgBuf1 = U64_VALUE_FROM_AXIMM_ARRAY(12);
	Multi_Sc.msc_srcImgBuf2 = U64_VALUE_FROM_AXIMM_ARRAY(14);
	Multi_Sc.msc_dstImgBuf0 = U64_VALUE_FROM_AXIMM_ARRAY(16);
	Multi_Sc.msc_dstImgBuf1 = U64_VALUE_FROM_AXIMM_ARRAY(18);
	Multi_Sc.msc_dstImgBuf2 = U64_VALUE_FROM_AXIMM_ARRAY(20);
	Multi_Sc.hfltCoeffOffset = U64_VALUE_FROM_AXIMM_ARRAY(22);
	Multi_Sc.vfltCoeffOffset = U64_VALUE_FROM_AXIMM_ARRAY(24);
	Multi_Sc.params_alpha_0 = U32_VALUE_FROM_AXIMM_ARRAY(26);
	Multi_Sc.params_alpha_1 = U32_VALUE_FROM_AXIMM_ARRAY(27);
	Multi_Sc.params_alpha_2 = U32_VALUE_FROM_AXIMM_ARRAY(28);
	Multi_Sc.params_beta_0 = U32_VALUE_FROM_AXIMM_ARRAY(29);
	Multi_Sc.params_beta_1 = U32_VALUE_FROM_AXIMM_ARRAY(30);
	Multi_Sc.params_beta_2 = U32_VALUE_FROM_AXIMM_ARRAY(31);
	Multi_Sc.msc_nxtaddr = U64_VALUE_FROM_AXIMM_ARRAY(32);

  	params[0] = Multi_Sc.params_alpha_0;
  	params[1] = Multi_Sc.params_alpha_1;
  	params[2] = Multi_Sc.params_alpha_2;
  	params[3] = Multi_Sc.params_beta_0;
  	params[4] = Multi_Sc.params_beta_1;
  	params[5] = Multi_Sc.params_beta_2;

#if (HSC_SCALE_MODE == HSC_POLYPHASE)
	/* hfiltCoff read */
	xOffset = yOffset = 0;
	readOffset = Multi_Sc.hfltCoeffOffset / AXIMM_DATA_WIDTH8;
	for (i = 0; i < READ_LENGTH_COEFF_H; i++)
	{
#pragma HLS loop_flatten off
		aximmTemp = HwReg.ms_maxi_srcbuf[readOffset + i];
		for (j = 0; j < AXIMM_DATA_WIDTH8 / 2; j++)
		{
#pragma HLS loop_flatten off
			for (k = 0; k < HSC_SAMPLES_PER_CLOCK; k++)
			{
#pragma HLS loop_flatten off
				hfltCoeff[yOffset][xOffset][k] = aximmTemp(j * 16 + 15, j * 16);
			}
			yOffset += (xOffset == (HSC_TAPS - 1)) ? 1 : 0;
			xOffset = (xOffset == (HSC_TAPS - 1)) ? 0 : (xOffset + 1);
		}
	}
	/* vfiltCoff read */
	xOffset = yOffset = 0;
	readOffset = Multi_Sc.vfltCoeffOffset / AXIMM_DATA_WIDTH8;
	for (i = 0; i < READ_LENGTH_COEFF_V; i++)
	{
#pragma HLS loop_flatten off
		aximmTemp = HwReg.ms_maxi_srcbuf[readOffset + i];
		for (j = 0; j < AXIMM_DATA_WIDTH8 / 2; j++)
		{
#pragma HLS loop_flatten off
			vfltCoeff[yOffset][xOffset] = aximmTemp(j * 16 + 15, j * 16);
			yOffset += (xOffset == (VSC_TAPS - 1)) ? 1 : 0;
			xOffset = (xOffset == (VSC_TAPS - 1)) ? 0 : (xOffset + 1);
		}
	}
#endif
}

/*********************************************************************************
 * Function:    hscale_top
 * Parameters:  Stream of input/output pixels, image resolution, type of scaling etc
 * Return:
 * Description: Top level function to perform horizontal image resizing
 * submodules - AXIvideo2MultiPixStream
 *              hscale_core
 *              MultiPixStream2AXIvideo
 **********************************************************************************/
void v_multi_scaler(U8 num_outs, U64 start_addr, AXIMM ms_maxi_srcbuf,
#if(INPUT_INTERFACE == AXI_STREAM_INTERFACE)
		HSC_AXI_STREAM_IN& s_axis_video,
#endif
#if (OUTPUT_INTERFACE == AXI_STREAM_INTERFACE)
		HSC_AXI_STREAM_OUT& m_axis_video,
#else
		AXIMM ms_maxi_dstbuf,
#endif
		U8 ms_status)
{
	__xilinx_ip_top(0);

	const int NUM_OUTSTANDING = AXIMM_NUM_OUTSTANDING;
	const int BURST_LENGTH = AXIMM_BURST_LENGTH;

#if(INPUT_INTERFACE == AXI_STREAM_INTERFACE)
#pragma HLS INTERFACE axis port=&s_axis_video register
#endif

#pragma HLS INTERFACE s_axilite port=num_outs bundle=CTRL offset=0x0020
#pragma HLS INTERFACE ap_none port=num_outs

#pragma HLS INTERFACE s_axilite port=start_addr bundle=CTRL offset=0x0030
#pragma HLS INTERFACE ap_none port=start_addr

#pragma HLS INTERFACE m_axi port=ms_maxi_srcbuf offset=slave bundle=mm_video latency=100 depth=2*128*128 num_write_outstanding=NUM_OUTSTANDING num_read_outstanding=NUM_OUTSTANDING max_write_burst_length=BURST_LENGTH max_read_burst_length=BURST_LENGTH
#pragma HLS INTERFACE s_axilite port=ms_maxi_srcbuf bundle=CTRL offset=0x0040

#if (OUTPUT_INTERFACE == AXI_STREAM_INTERFACE)
#pragma HLS INTERFACE axis port=&m_axis_video register
#else
#pragma HLS INTERFACE m_axi port=ms_maxi_dstbuf offset=slave bundle=mm_video latency=100 depth=2*256*256 num_write_outstanding=NUM_OUTSTANDING num_read_outstanding=NUM_OUTSTANDING max_write_burst_length=BURST_LENGTH max_read_burst_length=BURST_LENGTH
#pragma HLS INTERFACE s_axilite port=ms_maxi_dstbuf bundle=CTRL offset=0x0050
#endif

#pragma HLS INTERFACE s_axilite port=ms_status bundle=CTRL offset=0x0060
#pragma HLS INTERFACE ap_none port=ms_status

#pragma HLS INTERFACE s_axilite port=return bundle=CTRL

#pragma HLS ALLOCATION function instances=v_scaler_top   limit=1
#pragma HLS ALLOCATION function instances=calc_phaseH    limit=1

	HSC_HW_STRUCT_REG HwReg;
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
	HwReg.ms_maxi_dstbuf = ms_maxi_dstbuf;
#endif
	HwReg.ms_maxi_srcbuf = ms_maxi_srcbuf;
	HwReg.ms_status = ms_status;
	HwReg.num_outs = num_outs;
	HwReg.start_addr = start_addr;
	V_SCALER_TOP_STRUCT Multi_Sc;

#if DEBUG
	if((HwReg.start_addr % AXIMM_DATA_WIDTH8) != 0)
	{
		Multi_Sc.debug_var[0] = DEBUG_INVALID_START_ADDR;
		write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		assert(1);
	}
	else
	{
		Multi_Sc.debug_var[0] = DEBUG_PASS;
		write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
	}
#endif

	I16 hfltCoeff[HSC_PHASES][HSC_TAPS][HSC_SAMPLES_PER_CLOCK];
		int params[2 * 3];

#pragma HLS ARRAY_PARTITION variable=hfltCoeff     complete dim=2
#pragma HLS ARRAY_PARTITION variable=hfltCoeff     complete dim=3
#pragma HLS bind_storage variable=hfltCoeff type=RAM_1P impl=BRAM
#pragma HLS bind_storage variable=params type=RAM_1P impl=BRAM


	I16 vfltCoeff[VSC_PHASES][VSC_TAPS];
#pragma HLS bind_storage variable=vfltCoeff type=RAM_1P impl=BRAM
#pragma HLS ARRAY_PARTITION variable=vfltCoeff complete dim=2

	HSC_PHASE_CTRL blkmm_phasesH[HSC_MAX_WIDTH / HSC_SAMPLES_PER_CLOCK];
	ap_uint<1> done_flag;
	U8 stats = 0;
	U8 dummy = 0;
	ap_uint<1> RdnxtDesc = 1;

	while (RdnxtDesc != 0)
	{
		GetMultiScAndCoeff(HwReg, Multi_Sc
#if (HSC_SCALE_MODE == HSC_POLYPHASE)
				, hfltCoeff, vfltCoeff
#endif
#if (NORMALIZATION == 1)
	, params
#endif
				);

#if DEBUG
		if( Multi_Sc.msc_srcImgBuf0 % AXIMM_DATA_WIDTH8 != 0 )
		{
			Multi_Sc.debug_var[1] = DEBUG_INVALID_SRC_BUFF0_ADDR;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[1] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}
		if( Multi_Sc.msc_dstImgBuf0 % AXIMM_DATA_WIDTH8 != 0 )
		{
			Multi_Sc.debug_var[2] = DEBUG_INVALID_DST_BUFF0_ADDR;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}
		else
		{
			Multi_Sc.debug_var[2] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}
#if (MAX_NR_PLANES > 1)
		if( Multi_Sc.msc_srcImgBuf1 % AXIMM_DATA_WIDTH8 != 0 )
		{
			Multi_Sc.debug_var[3] = DEBUG_INVALID_SRC_BUFF1_ADDR;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[3] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}
		if( Multi_Sc.msc_dstImgBuf1 % AXIMM_DATA_WIDTH8 != 0 )
		{
			Multi_Sc.debug_var[4] = DEBUG_INVALID_DST_BUFF1_ADDR;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[4] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}
#endif

#if (MAX_NR_PLANES > 2)
		if( Multi_Sc.msc_srcImgBuf2 % AXIMM_DATA_WIDTH8 != 0 )
		{
			Multi_Sc.debug_var[5] = DEBUG_INVALID_SRC_BUFF2_ADDR;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[5] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}
		if( Multi_Sc.msc_dstImgBuf2 % AXIMM_DATA_WIDTH8 != 0 )
		{
			Multi_Sc.debug_var[6] = DEBUG_INVALID_DST_BUFF2_ADDR;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[6] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}

#endif

#if( (HSC_SCALE_MODE == HSC_POLYPHASE) && (VSC_SCALE_MODE == HSC_POLYPHASE))
		if( Multi_Sc.hfltCoeffOffset % AXIMM_DATA_WIDTH8 != 0 )
		{
			Multi_Sc.debug_var[7] = DEBUG_INVALID_HCOEF_BUFF_ADDR;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[7] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}

		if( Multi_Sc.vfltCoeffOffset % AXIMM_DATA_WIDTH8 != 0 )
		{
			Multi_Sc.debug_var[8] = DEBUG_INVALID_VCOEF_BUFF_ADDR;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[8] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}
#endif
		if( Multi_Sc.msc_nxtaddr % AXIMM_DATA_WIDTH8 != 0 )
		{
			Multi_Sc.debug_var[9] = DEBUG_INVALID_NEXT_ADDR;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[9] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}

		if( ( Multi_Sc.msc_heightIn < 64 ) || ( Multi_Sc.msc_heightIn > HSC_MAX_HEIGHT ) )
		{
			Multi_Sc.debug_var[10] = DEBUG_INVALID_HEIGHT_IN;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[10] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}

		if( ( Multi_Sc.msc_heightOut < 64 ) || ( Multi_Sc.msc_heightOut > HSC_MAX_HEIGHT ))
		{
			Multi_Sc.debug_var[11] = DEBUG_INVALID_HEIGHT_OUT;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[11] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}

		if(( Multi_Sc.msc_widthIn < 64 ) || ( Multi_Sc.msc_widthIn >  HSC_MAX_WIDTH))
		{
			Multi_Sc.debug_var[12] = DEBUG_INVALID_WIDTH_IN;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[12] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}

		if(( Multi_Sc.msc_widthOut < 64  ) || ( Multi_Sc.msc_widthOut > HSC_MAX_WIDTH ))
		{
			Multi_Sc.debug_var[13] = DEBUG_INVALID_WIDTH_OUT;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[13] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}

		if(( Multi_Sc.msc_inPixelFmt < 0 ) || ( Multi_Sc.msc_inPixelFmt > 41 ))
		{
			Multi_Sc.debug_var[14] = DEBUG_INVALID_IN_PIXEL_FORMAT;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[14] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}

		if( ( Multi_Sc.msc_outPixelFmt < 0 ) || ( Multi_Sc.msc_outPixelFmt > 41 ))
		{
			Multi_Sc.debug_var[15] = DEBUG_INVALID_OUT_PIXEL_FORMAT;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
			assert(1);
		}
		else
		{
			Multi_Sc.debug_var[15] = DEBUG_PASS;
			write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
		}
#endif

#if DEBUG
		Multi_Sc.debug_var[16] = DEBUG_PHASE_CALC_FUNC_NOT_EXECUTED;
		write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
#endif
		calc_phaseH(Multi_Sc.msc_widthIn, Multi_Sc.msc_widthOut, Multi_Sc.msc_pixelRate,
				blkmm_phasesH, done_flag);
#if DEBUG
		Multi_Sc.debug_var[16] = DEBUG_PHASE_CALC_FUNC_EXECUTED;
		write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
#endif

#if (INPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM src0 = (AXIMM) &HwReg.ms_maxi_srcbuf[Multi_Sc.msc_srcImgBuf0 / AXIMM_DATA_WIDTH8];
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM dst0 = (AXIMM) &HwReg.ms_maxi_dstbuf[Multi_Sc.msc_dstImgBuf0 / AXIMM_DATA_WIDTH8];
#endif
#if ((MAX_NR_PLANES == 2) || (MAX_NR_PLANES == 3))
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM src1 = (AXIMM) &HwReg.ms_maxi_srcbuf[Multi_Sc.msc_srcImgBuf1 / AXIMM_DATA_WIDTH8];
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM dst1 = (AXIMM) &HwReg.ms_maxi_dstbuf[Multi_Sc.msc_dstImgBuf1 / AXIMM_DATA_WIDTH8];
#endif
#if (MAX_NR_PLANES == 3)
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM src2 = (AXIMM) &HwReg.ms_maxi_srcbuf[Multi_Sc.msc_srcImgBuf2 / AXIMM_DATA_WIDTH8];
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM dst2 = (AXIMM) &HwReg.ms_maxi_dstbuf[Multi_Sc.msc_dstImgBuf2 / AXIMM_DATA_WIDTH8];
#endif
#endif	/* end of (MAX_NR_PLANES == 3) */
#endif	/* end of ((MAX_NR_PLANES == 2) || (MAX_NR_PLANES == 3)) */

#if DEBUG
		Multi_Sc.debug_var[17] = DEBUG_INSIDE_DATAFLOW;
		write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
#endif

		v_scaler_top(
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
				src0,
#else
				s_axis_video,
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
				dst0,
#else
				m_axis_video,
#endif
#if ((MAX_NR_PLANES == 2) || (MAX_NR_PLANES == 3))
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
				src1,
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
				dst1,
#endif
#if (MAX_NR_PLANES == 3)
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
				src2,
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
				dst2,
#endif
#endif
#endif
				(U16) Multi_Sc.msc_heightIn, (U16) Multi_Sc.msc_heightOut,
				(U16) Multi_Sc.msc_widthIn, (U16) Multi_Sc.msc_widthOut,
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
				(U16) Multi_Sc.msc_strideIn,
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
				(U16) Multi_Sc.msc_strideOut,
#endif
				(U8) Multi_Sc.msc_inPixelFmt, (U8) Multi_Sc.msc_outPixelFmt,
				(U32) Multi_Sc.msc_pixelRate, (U32) Multi_Sc.msc_lineRate,
#if (HSC_SCALE_MODE == HSC_POLYPHASE)
				hfltCoeff,
#endif
#if (VSC_SCALE_MODE == VSC_POLYPHASE)
				vfltCoeff,
#endif
#if(NORMALIZATION == 1)
			params,
#endif
				blkmm_phasesH);
#if DEBUG
		Multi_Sc.debug_var[17] = DEBUG_OUTSIDE_DATAFLOW
				//unused or disabled debug vars
				for(int d = 18; d < DEBUG_VARS; d++)
					Multi_Sc.debug_var[d] = DEBUG_UNUSED;
		write_debug_variables(Multi_Sc, HwReg.start_addr, HwReg.ms_maxi_srcbuf);
#endif

		stats = stats + 1;
		HwReg.ms_status = stats;
		HwReg.start_addr = Multi_Sc.msc_nxtaddr;
		if ((HwReg.start_addr == 0) || (stats == HwReg.num_outs))
			RdnxtDesc = 0;
	}
}

static void v_scaler_top(
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM srcImgBuf0,
#else
		HSC_AXI_STREAM_IN& s_axis_vid,
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM dstImgBuf0,
#else
		HSC_AXI_STREAM_OUT& m_axis_vid,
#endif
#if ((MAX_NR_PLANES == 2) || (MAX_NR_PLANES == 3))
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM srcImgBuf1,
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM dstImgBuf1,
#endif
#if (MAX_NR_PLANES == 3)
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM srcImgBuf2,
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
		AXIMM dstImgBuf2,
#endif
#endif
#endif
		U16 HeightIn, U16 HeightOut, U16 WidthIn, U16 WidthOut,
#if (INPUT_INTERFACE == AXIMM_INTERFACE)
		U16 StrideIn,
#endif
#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
		U16 StrideOut,
#endif
		U8 InPixelFmt, U8 OutPixelFmt, U32 PixelRate, U32 LineRate,
#if (HSC_SCALE_MODE == HSC_POLYPHASE)
		I16 hfltCoeff[HSC_PHASES][HSC_TAPS][HSC_SAMPLES_PER_CLOCK],
#endif
#if (VSC_SCALE_MODE == VSC_POLYPHASE)
		I16 vfltCoeff[VSC_PHASES][VSC_TAPS],
#endif
#if (NORMALIZATION == 1)
	int params[2 * 3],
#endif
		HSC_PHASE_CTRL blkmm_phasesH[HSC_MAX_WIDTH / HSC_SAMPLES_PER_CLOCK])
{
	U8 ColorModeIn = MEMORY2LIVE[InPixelFmt];
	U8 ColorModeOut = MEMORY2LIVE[OutPixelFmt];
	bool bPassThruVcrUp = (ColorModeIn == yuv420) ? false : true;
	bool bPassThruHcrUp = (ColorModeIn == yuv422 || ColorModeIn == yuv420) ? false : true;
	bool bPassThruHcrDown = (ColorModeOut == yuv422 || ColorModeOut == yuv420) ? false : true;
	bool bPassThruVcrDown = (ColorModeOut == yuv420) ? false : true;
	bool bPassThruCsc =
			((ColorModeIn == rgb && ColorModeOut != rgb)
					|| (ColorModeIn != rgb && ColorModeOut == rgb)) ? false : true;
	bool bPassThruHsc = (WidthIn != WidthOut) ? false : true;
	bool bPassThruVsc = (HeightIn != HeightOut) ? false : true;

	if ((ColorModeIn == ColorModeOut) && (WidthIn == WidthOut) && (HeightIn == HeightOut))
	{
		bPassThruHsc = true;
		bPassThruVsc = true;
		bPassThruVcrUp = true;
		bPassThruHcrUp = true;
		bPassThruHcrDown = true;
		bPassThruVcrDown = true;
		bPassThruCsc = true;
	}

	int WidthInBytes;
	int WidthOutBytes;

	const int PLANE_STREAM_DEPTH0 = 2 * PLANE0_STREAM_DEPTH;
	STREAM_BYTES srcPlane0, dstPlane0;
#pragma HLS BIND_STORAGE variable=srcPlane0 type=fifo impl=lutram
#pragma HLS STREAM variable=srcPlane0 depth=PLANE_STREAM_DEPTH0
#pragma HLS BIND_STORAGE variable=dstPlane0 type=fifo impl=lutram
#pragma HLS STREAM variable=dstPlane0 depth=PLANE_STREAM_DEPTH0

#if ((MAX_NR_PLANES==2) || (MAX_NR_PLANES==3))
	STREAM_BYTES srcPlane1, dstPlane1;
#pragma HLS BIND_STORAGE variable=srcPlane1 type=fifo impl=lutram
#pragma HLS STREAM variable=srcPlane1 depth=PLANE_STREAM_DEPTH0
#pragma HLS BIND_STORAGE variable=dstPlane1 type=fifo impl=lutram
#pragma HLS STREAM variable=dstPlane1 depth=PLANE_STREAM_DEPTH0
#endif

#if (MAX_NR_PLANES==3)
	STREAM_BYTES srcPlane2, dstPlane2;
#pragma HLS BIND_STORAGE variable=srcPlane2 type=fifo impl=lutram
#pragma HLS STREAM variable=srcPlane2 depth=PLANE_STREAM_DEPTH0
#pragma HLS BIND_STORAGE variable=dstPlane2 type=fifo impl=lutram
#pragma HLS STREAM variable=dstPlane2 depth=PLANE_STREAM_DEPTH0
#endif

	HSC_STREAM_MULTIPIX stream_in;
	HSC_STREAM_MULTIPIX stream_1;
	HSC_STREAM_MULTIPIX stream_2;
	HSC_STREAM_MULTIPIX stream_3;
	HSC_STREAM_MULTIPIX stream_4;
	HSC_STREAM_MULTIPIX stream_4_csc;
	HSC_STREAM_MULTIPIX stream_5;
	HSC_STREAM_MULTIPIX stream_out;
	HSC_STREAM_MULTIPIX dstStrm;

#pragma HLS DATAFLOW

#pragma HLS stream depth=16 variable=stream_in
#pragma HLS stream depth=16 variable=stream_1
#pragma HLS stream depth=16 variable=stream_2
#pragma HLS stream depth=4096 variable=stream_3
#pragma HLS stream depth=16 variable=stream_4
#pragma HLS stream depth=16 variable=stream_4_csc
#pragma HLS stream depth=16 variable=stream_5
#pragma HLS stream depth=16 variable=stream_out
#pragma HLS stream depth=16 variable=dstStrm

const int strm_width_in = CPW * HSC_SAMPLES_PER_CLOCK * HSC_BITS_PER_COMPONENT;
const int strm_width_out = CPW * HSC_SAMPLES_PER_CLOCK * HSC_BITS_PER_COMPONENT;

hls::stream<ap_uint<strm_width_in> > srcStrmIn;
hls::stream<ap_uint<strm_width_out> > dstStrmOut;

int alpha_reg[3];
int beta_reg[3];

// clang-format off
#pragma HLS ARRAY_PARTITION variable=alpha_reg dim=0 complete
#pragma HLS ARRAY_PARTITION variable=beta_reg dim=0 complete

  //params[0] = params[1] = params[2] = 128;
  //params[3] = params[4] = params[5] = 1;
  //params[6] = params[7] = params[8] = 0;

for (int i = 0; i < 2 * 3; i++) {
// clang-format off
#pragma HLS LOOP_TRIPCOUNT min=1 max=12
#pragma HLS PIPELINE II=1
    // clang-format on
    int temp = params[i];
    if (i < 3)
        alpha_reg[i] = temp;
    else
        beta_reg[i - 3] = temp;
}

int loop_count = (HeightOut * WidthOut) / (HSC_SAMPLES_PER_CLOCK);
	if (InPixelFmt == Y_UV10 || InPixelFmt == Y_UV10_420 || InPixelFmt == Y10)
	{
		//4 bytes per 3 pixels
		WidthInBytes = (WidthIn * 4) / 3;
	}
	else
	{
		WidthInBytes = WidthIn * BYTES_PER_PIXEL[InPixelFmt];
	}

	if (OutPixelFmt == Y_UV10 || OutPixelFmt == Y_UV10_420 || OutPixelFmt == Y10)
	{
		//4 bytes per 3 pixels
		WidthOutBytes = (WidthOut * 4) / 3;
	}
	else
	{
		WidthOutBytes = WidthOut * BYTES_PER_PIXEL[OutPixelFmt];
	}

#if (INPUT_INTERFACE == AXIMM_INTERFACE)

	AXIMMvideo2Bytes(srcImgBuf0, srcPlane0,
#if ((MAX_NR_PLANES==2) || (MAX_NR_PLANES==3))
			srcImgBuf1, srcPlane1,
#if (MAX_NR_PLANES == 3)
			srcImgBuf2, srcPlane2,
#endif
#endif
			HeightIn, WidthIn, WidthInBytes, StrideIn, InPixelFmt);

	Bytes2MultiPixStream(srcPlane0,
#if ((MAX_NR_PLANES==2) || (MAX_NR_PLANES==3))
			srcPlane1,
#if (MAX_NR_PLANES == 3)
			srcPlane2,
#endif
#endif
			stream_in, HeightIn, WidthIn, WidthInBytes, StrideIn, InPixelFmt);
#else
	AXIvideo2MultiPixStream(s_axis_vid, stream_in, HeightIn, WidthIn, InPixelFmt);

#endif

	v_vcresampler(stream_in, HeightIn, WidthIn, yuv420, bPassThruVcrUp, stream_1);

	v_hcresampler(stream_1, HeightIn, WidthIn, yuv422, bPassThruHcrUp, stream_2);

	v_vscaler(stream_2, HeightIn, WidthIn, HeightOut, LineRate, bPassThruVsc,
#if  (VSC_SCALE_MODE == VSC_POLYPHASE)
			vfltCoeff,
#endif
			stream_3);

	v_hscaler(stream_3, HeightOut, WidthIn, WidthOut, PixelRate, ColorModeIn, bPassThruHsc,
#if (HSC_SCALE_MODE == HSC_POLYPHASE)
			hfltCoeff,
#endif
			blkmm_phasesH, stream_4);

	v_csc(stream_4, HeightOut, WidthOut, ColorModeIn, bPassThruCsc, stream_4_csc);

	v_hcresampler(stream_4_csc, HeightOut, WidthOut, yuv444, bPassThruHcrDown, stream_5);

	v_vcresampler(stream_5, HeightOut, WidthOut, yuv422, bPassThruVcrDown, stream_out);

#if (OUTPUT_INTERFACE == AXIMM_INTERFACE)
	preProcessKernel(stream_out, dstStrm, alpha_reg, beta_reg, loop_count, HeightOut, WidthOut, ColorModeOut);
	MultiPixStream2Bytes(dstStrm, dstPlane0,
#if ((MAX_NR_PLANES==2) || (MAX_NR_PLANES==3))
			dstPlane1,
#endif
#if (MAX_NR_PLANES == 3)
			dstPlane2,
#endif
			HeightOut, WidthOut, WidthOutBytes, StrideOut, OutPixelFmt);

	Bytes2AXIMMvideo(dstPlane0, dstImgBuf0,
#if ((MAX_NR_PLANES == 2) || (MAX_NR_PLANES == 3))
			dstPlane1, dstImgBuf1,
#if (MAX_NR_PLANES == 3)
			dstPlane2, dstImgBuf2,
#endif
#endif
			HeightOut, WidthOut, WidthOutBytes, StrideOut, OutPixelFmt);
#else
	MultiPixStream2AXIvideo(stream_out, m_axis_vid, HeightOut, WidthOut, OutPixelFmt);
#endif
}
