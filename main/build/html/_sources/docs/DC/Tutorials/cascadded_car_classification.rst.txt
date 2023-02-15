#################################################
Developing an application using VVAS Core APIs
#################################################

This document explains steps for using the VVAS Core APIs. This document can be used as a user guide to develop any application using VVAS Core APIs. An application to do 1st level of inferencing is considered as an example to explain this.

Below is the block diagram.

.. figure:: ../../images/App_Development.png

Below is the algorithm for the above application.

* (A) Create Parser, Decoder, Scaler, Infer, and MetaConvert instance.
* (B)	Read encoded data from the file and parse it using Parser, if EOS go to step K.
* (C)	Feed decoder parsed data and get the decoded data.
* (D)	Do scaling and pre-processing on decoded buffer using Scaler and keep the decoded buffer as it is.
* (E)	Do inferencing on scaled and pre-processed buffer using DPU.
* (F)	Free scaled and pre-processed buffer and upscale the inference bounding box to the resolution of decoded buffer.
* (G)	Convert DPU inference data to Overlay data using MetaConvert.
* (H)	Draw inference data on decoded buffer.
* (I)	Consume this buffer.
* (J)	Go to step B.
* (K)	Destroy all instances of VVAS core modules.
* (L)	Exit

Before starting this application let’s have a quick overview of APIs for all above VVAS Core modules.

*************
Parser APIs
*************
* Creating Parser instance

.. code-block::

	VvasParser* vvas_parser_create (VvasContext* vvas_ctx, VvasCodecType codec_type, VvasLogLevel log_level)

* Get Stream Parameters (Decoder input configuration) and Access Unit frame

.. code-block::

	VvasReturnType vvas_parser_get_au (VvasParser *handle, VvasMemory *inbuf, int32_t valid_insize, VvasMemory **outbuf, int32_t *offset, VvasDecoderInCfg **dec_cfg, bool islast)

* Destroy Parser instance

.. code-block::

	VvasReturnType vvas_parser_destroy (VvasParser *handle)

For complete details of all the Parser APIs please refer to the <link to vvas-core parser APIs>

**************
Decoder APIs
**************

* Create Decoder instance

.. code-block:: C

	VvasDecoder* vvas_decoder_create (VvasContext *vvas_ctx, uint8_t *dec_name, VvasCodecType dec_type, uint8_t hw_instance_id, VvasLogLevel log_level)

* Configure Decoder and Get Decoder's output configuration

.. code-block::

	VvasReturnType vvas_decoder_config (VvasDecoder* dec_handle, VvasDecoderInCfg *input_cfg, VvasDecoderOutCfg *output_cfg)

* Submit Parsed Frame and List of output video frames to the decoder

.. code-block::

	VVvasReturnType vvas_decoder_submit_frames(VvasDecoder* dec_handle, VvasMemory *nalu, VvasList *loutframes)

* Get Decoded frame from the Decoder

.. code-block::

	VvasReturnType vvas_decoder_get_decoded_frame(VvasDecoder* dec_handle, VvasVideoFrame **output)

* Destroy Decoder instance

.. code-block::

	VvasReturnType vvas_decoder_destroy (VvasDecoder* dec_handle)

For complete details of all the Decoder APIs please refer to the <link to vvas-core decoder APIs>

************
Scaler APIs
************

* Create Scaler instance

.. code-block::

	VvasScaler * vvas_scaler_create (VvasContext * ctx, const char * kernel_name, VvasLogLevel log_level)

* Configuring Scaler

.. code-block::

	VvasReturnType vvas_scaler_prop_set (VvasScaler * hndl, VvasScalerProp * prop)

* Adding processing channel into Scaler

.. code-block::

	VvasReturnType vvas_scaler_channel_add (VvasScaler * hndl, VvasScalerRect * src_rect, VvasScalerRect * dst_rect, VvasScalerPpe * ppe, VvasScalerParam * param)

* Processing all the added channels

.. code-block::

	VvasReturnType vvas_scaler_process_frame (VvasScaler * hndl)

* Destroy Scaler instance

.. code-block::

	VvasReturnType vvas_scaler_destroy (VvasScaler * hndl)

For complete details of all the Scaler APIs please refer to the <link to vvas-core scaler APIs>

************
DPU APIs
************

* Create DPU instance

.. code-block::

	VvasDpuInfer * vvas_dpuinfer_create (VvasDpuInferConf * dpu_conf, VvasLogLevel log_level)

* Get DPU configuration

.. code-block::

	VvasReturnType vvas_dpuinfer_get_config (VvasDpuInfer * dpu_handle, VvasModelConf *model_conf)

* Do inferencing

.. code-block::

	VvasReturnType vvas_dpuinfer_process_frames (VvasDpuInfer * dpu_handle, VvasVideoFrame *inputs[MAX_NUM_OBJECT], VvasInferPrediction *predictions[MAX_NUM_OBJECT], int batch_size)

* Destroy DPU instance

.. code-block::

	VvasReturnType vvas_dpuinfer_destroy (VvasDpuInfer * dpu_handle)


For complete details of all the DPU APIs please refer to the <link to vvas-core DPU APIs>

*****************
MetaConvert APIs
*****************

* Create MetaConvert instance

.. code-block::

	VvasMetaConvert *vvas_metaconvert_create (VvasContext *vvas_ctx, VvasMetaConvertConfig *cfg,VvasLogLevel log_level, VvasReturnType *ret)

* Convert DPU detected data to Overlay data format

.. code-block::

	VvasReturnType vvas_metaconvert_prepare_overlay_metadata (VvasMetaConvert *meta_convert, VvasTreeNode *parent, VvasOverlayShapeInfo *shape_info)

* Destroy MetaConvert instance

.. code-block::

	void vvas_metaconvert_destroy (VvasMetaConvert *meta_convert)


For complete details of all the MetaConvert APIs please refer to the <link to vvas-core MetaConvert APIs>

*****************
Overlay APIs
*****************

* Draw Infer data onto the Video

.. code-block::

	VvasReturnType vvas_overlay_process_frame (VvasOverlayFrameInfo *pFrameInfo)

For complete details of all the Overlay APIs please refer to the <link to vvas-core overlay APIs>


.. note::

	* VVAS Core APIs are not thread safe.
	* User should create their own buffer pool and manage the buffers allocation.
	* VVAS Core Parser can parse only H264 and H265 elementary streams.

As we have now an overview of all the APIs of several VVAS Core modules, let’s use them to create an application.

The very first step to use VVAS Core APIs of different modules is to create the VVAS Context.

*********************
Creating VVAS Context
*********************

VVAS Context is needed by almost every VVAS Core module, VVAS Context can be created using below API.

.. code-block::

	VvasContext* vvas_context_create (int32_t dev_idx, char * xclbin_loc, VvasLogLevel log_level, VvasReturnType *vret)

XCL bin is needed for Scaler and Decoder only in our use case, as they are the only modules using Hardware IP.

It is recommended to create VVAS context for each component separately if each module will be running in different thread, for creating VVAS context for Parser, MetaConvert and Overlay XCL bin is not needed, hence XCL bin can passed as NULL and dev_idx as -1.


**********************
Creating VVAS Memory
**********************

VVAS Memory is used to store elementary streams and can be created using below API.

.. code-block::

	VvasMemory* vvas_memory_alloc (VvasContext *vvas_ctx, VvasAllocationType mem_type, VvasAllocationFlags mem_flags, uint8_t mbank_idx, size_t size, VvasReturnType *ret)

For reading/writing into VvasMemeory, user need to map it is read/write mode using below API.

.. code-block::

	VvasReturnType vvas_memory_map (VvasMemory* vvas_mem, VvasDataMapFlags flags, VvasMemoryMapInfo *info)

User can get the virtual pointer and size of the data from VvasMemoryMapInfo after mapping it.

Once done with read/write on VvasMemory it must be unmapped using below API.

.. code-block::

	VvasReturnType vvas_memory_unmap (VvasMemory* vvas_mem, VvasMemoryMapInfo *info)

When done with VvasMemory, it must be freed using below API.

.. code-block::

	void vvas_memory_free (VvasMemory* vvas_mem)


****************************
Creating VVAS Video Frame
****************************

VvasVideoFrame is used to store raw video data and can be created using below API.

.. code-block::

	VvasVideoFrame* vvas_video_frame_alloc (VvasContext *vvas_ctx, VvasAllocationType alloc_type, VvasAllocationFlags alloc_flags, uint8_t mbank_idx, VvasVideoInfo *vinfo, VvasReturnType *ret)

For reading or writing into VvasVideoFrame user must map it in READ or WRITE mode respectively using below API.

.. code-block::

	VvasVideoFrame* vvas_video_frame_alloc (VvasContext *vvas_ctx, VvasAllocationType alloc_type, VvasAllocationFlags alloc_flags, uint8_t mbank_idx, VvasVideoInfo *vinfo, VvasReturnType *ret)

After read/write, VvasVideoFrame must be unmapped using below API.

.. code-block::

	VvasReturnType vvas_video_frame_unmap (VvasVideoFrame* vvas_vframe, VvasVideoFrameMapInfo *info)

When done with VvasVideoFrame , free it using below API.

.. code-block::

	void vvas_video_frame_free (VvasVideoFrame* vvas_vframe)

VvasAllocationType and VvasAllocationFlags for VVAS Core’s Decoder and Scaler buffers must be VVAS_ALLOC_TYPE_CMA and VVAS_ALLOC_FLAG_NONE respectively.


*******************************
Parsing H.264/H.265 streams
*******************************

Once VVAS context is created, create the Parser context using below API.

.. code-block::

	VvasParser* vvas_parser_create (VvasContext* vvas_ctx, VvasCodecType codec_type, VvasLogLevel log_level):

To feed parser with encoded buffer user need to allocate VvasMemory and copy encoded data into it and feed this VvasMemory to the parser to get the parsed buffer and the decoder’s input configuration (Encoded stream information).

.. code-block::

	VvasReturnType vvas_parser_get_au (VvasParser *handle, VvasMemory *inbuf, int32_t valid_insize, VvasMemory **outbuf, int32_t *offset, VvasDecoderInCfg **dec_cfg, bool islast)

If return value from above API is VVAS_RET_NEED_MOREDATA, it means that the encoded buffer was not sufficient for the parser and it need more data.

While feeding above API user need to be careful of @offset value, this is both in and out parameter for the API, As input it should be pointing to the offset in input encoded buffer, and when this API returns it will contain the offset till which parser consumed the encoded buffer, hence while feeding this API again user should feed the remaining data if parser was not able to parse the complete data given to it.

Above API will also return the stream parameters into @dec_cfg, this configuration is generated whenever parser finds any change in the stream parameter or if it is the very first encoded frame.

This dec_cfg will be used to configure the decoder, user must free it after its use.
On VVAS_RET_SUCCESS from the API would get the parsed access unit frame in outbuf. This outbuf can be now fed to the decoder. This outbuf is allocated inside parser module, hence must be freed by the user after its use.


*********************************
Decoding H.264/H.265 streams
*********************************

Once VVAS context is created, decoder instance can be created using the below API.

.. code-block::

	VvasDecoder* vvas_decoder_create (VvasContext *vvas_ctx, uint8_t *dec_name, VvasCodecType dec_type, uint8_t hw_instance_id, VvasLogLevel log_level)

Decoder name for v70 is kernel_vdu_decoder:{kernel_vdu_decoder_xx} where xx can be from 0 – 16, each index represents one unique instance of the decoder.

Hw_instance_is is the Hardware instance id, In V70 for decoder_name  kernel_vdu_decoder:{kernel_vdu_decoder_0} to kernel_vdu_decoder:{kernel_vdu_decoder_7} it would be 0 and for kernel_vdu_decoder:{kernel_vdu_decoder_8} to kernel_vdu_decoder:{kernel_vdu_decoder_15} it should be 1.

Once decoder instance is created, it needs to be configured first using the below API.

.. code-block::

	VvasReturnType vvas_decoder_config (VvasDecoder* dec_handle, VvasDecoderInCfg *icfg, VvasDecoderOutCfg *ocfg)

The icfg can be get from the parser or if using external parser then this needs to be filled with correct values.
Ocfg is the output configuration which decoder will return.

Ocfg gives info on how many minimum buffers decoder needs, along with this information it will also give the VvasVideoInfo of the output buffer and memory bank index where these buffers must be allocated.

Once minimum number of buffers are allocated, put them in one VvasList, and then submit the parsed buffer along with this list of free/minimum buffers to the decoder using below API.

.. code-block::

	VvasReturnType vvas_decoder_submit_frames (VvasDecoder* dec_handle, VvasMemory *au_frame, VvasList *loutframes)

If above API returns VVAS_RET_SEND_AGAIN, this means decoder didn’t consume the current Access Unit frame and need to feed it again. Once possible reason for this return value could be that there is no room for decoded buffer.

If above API returns VVAS_RET_SUCCESS, this means decoder successfully consumed access unit frame, au_frame can be freed now.

Au_frame = NULL means this is the last buffer to be decoded, and this is a notification for the decoder to start flushing.

Even if above API returns VVAS_RET_SEND_AGAIN, user need to query the decoded buffer from the decoder using below API.

.. code-block::

	VvasReturnType vvas_decoder_get_decoded_frame (VvasDecoder* dec_handle, VvasVideoFrame **output)

If above API returns VVAS_RET_NEED_MOREDATA, this means decoder doesn’t have any decoded buffer yet, need to feed more data  using submit_frames API and call this API again.

If above API returns VVAS_RET_EOS, this means that there are no more decoded frames from the decoder.

If above API returns VVAS_RET_SUCCESS, this means that decoder has returned a decoded buffer into output, note that this output buffer is not allocated by the decoder, it is one of the buffer which was fed to the decoder using submit_frames API.

Below is the algorithm for decoding the frame.

* (A)	Create Decoder instance
* (B)	Get Parsed buffer and the decoder’s input configuration
* (C)	Configure decoder and get the decoder’s output configuration.
* (D)	Allocate minimum number of output buffers and prepare the free buffer list containing all these allocated buffers.
* (E)	Submit the decoded buffer and the list of free output buffers
* (F)	If submit frame was successful, free the parsed buffer
* (G)	If submit frame returned send again, then we need to send this buffer again, don’t free the parsed buffer.
* (H)	Clear the free buffer list as list of free buffers are given to the decoder.
* (I)	Get decoded buffer
* (J)	If get decoded_frame returned EOS, goto N.
* (K)	If get decoded_frame returned SUCCESS, then consume the decoded buffer and after consumption put it in list of free output buffers.
* (L)	If submit_frame has returned send again, goto E.
* (M)	Get new parsed buffer and goto E.
* (N)	Destroy the decoder
* (O)	Exit


**************************************************************
Scaling/Cropping/Pre-Processing Decoded data Using Scaler
**************************************************************
As VVVAS context is created scaler instance can be created using below API.

.. code-block::

	VvasScaler * vvas_scaler_create (VvasContext * ctx, const char * kernel_name, VvasLogLevel log_level)

Kernel name for V70 is image_processing:{image_processing_1} or image_processing:{image_processing_2}

Configure the scaler using below API.

.. code-block::

	VvasReturnType vvas_scaler_prop_set (VvasScaler * hndl, VvasScalerProp * prop)

For processing any data using scaler user need to add them as processing channel using below API.

.. code-block::

	VvasReturnType vvas_scaler_channel_add (VvasScaler * hndl, VvasScalerRect * src_rect, VvasScalerRect * dst_rect, VvasScalerPpe * ppe, VvasScalerParam * param)

User need to always feed the src_rect and dst_rect information to the scaler, for just scaling; x and y of src_rect must be zero, and width and height must be the width and height of the frame.

For doing crop, set x and y and width and height of src_rect as per the crop requirement.
For doing pre-processing, set ppe otherwise set it to NULL.
Different type of scaling can be done by providing VvasScalerParam

Examples Let’s scale 1920x1080 to 640x480.
Src_rect.x = 0;
Src_rext.y = 0;
Src_rect.width = 1920;
Src_rect.height = 1080;
Src_rect.frame = input_frame;
Dst_rect.x = 0;
Dst_rect.y = 0;
Dst_rect.width = 640;
Dst_rect.height = 480;
Dst_rect.frame = output_frame;

Let’s crop input frame from x,y = (300,350) width = 278, height = 590 and scale this cropped frame to 224x224.
Src_rect.x = 300;
Src_rect.y = 350;
Src_rect.width = 278;
Src_rect.height = 590;
Src_rect.frame = input_frame;
Dst_rect.x = 0;
Dst_rect.y = 0;
Dst_rect.width = 224;
Dst_rect.height = 224;
Dst_rect.frame = output_frame;

Once channels are added into the scaler, process all of them in one go using below API.

.. code-block::

	VvasReturnType vvas_scaler_process_frame (VvasScaler * hndl)

Once done with scaler; destroy it using below API.

.. code-block::

	VvasReturnType vvas_scaler_destroy (VvasScaler * hndl)



*************************
Doing inferencing
*************************

For doing inferencing create the DPU instance using below API.

.. code-block::

	VvasDpuInfer * vvas_dpuinfer_create (VvasDpuInferConf * dpu_conf, VvasLogLevel log_level)

Every DPU model has its own pre-processing, input format, width and input height requirement, this can be queried from the DPU using below API.

.. code-block::

	VvasReturnType vvas_dpuinfer_get_config (VvasDpuInfer * dpu_handle, VvasModelConf *model_conf)

DPU will do software-based scaling if input frame being submitted is not of the same resolution as DPU is expecting. This will have an impact on performance.

DPU can also do software-based pre-processing if VvasDpuInferConf.need_preprocess is true. This will have an impact on performance.
User can avoid these software-based operation by using Hardware accelerated VVAS Core Scaler for doing pre-processing and scaling in one operation.

Inferencing can be done on input frame(s) using below API.

.. code-block::

	VvasReturnType vvas_dpuinfer_process_frames (VvasDpuInfer * dpu_handle, VvasVideoFrame *inputs[MAX_NUM_OBJECT], VvasInferPrediction *predictions[MAX_NUM_OBJECT], int batch_size)

DPU supports batching mode, the number of frames that DPU can process in one batch (batch size) can be queried using vvas_dpuinfer_get_config API.

It is recommended to form the batch of input frames and then call vvas_dpuinfer_process_frames API for better performance.

In above API if predictions[x] is NULL, then DPU will create a tree structure of VvasInferPrediction nodes with the root node indicating the image resolution and predictions attached as children to this root node and return it when there is any detection/classification, if this is not NULL then VvasInferPrediction is appended as children to the passed VvasInferPrediction node.

It is user responsibility to free this VvasInferPrediction node after use.

It is to be noted that the bounding box information given in VvasInferPrediction are in respect with the input frame’s width and height.

Once done with inferencing of all frames, destroy the DPU instance using below API.

.. code-block::

	VvasReturnType vvas_dpuinfer_destroy (VvasDpuInfer * dpu_handle)


******************************************************************
Drawing Inference Information on Video Frame
******************************************************************

For drawing bounding box/classification data onto the video frame VVAS Core’s Overlay module can be used, but this Overlay module doesn’t accept data generated using VVAS Core’s DPU module directly. User need to convert Inference data generated using DPU to the format which Overlay module accepts using VVAS Core’s MetaConvert module.

MetaConvert module’s instance can be created using below API.

.. code-block::

	VvasMetaConvert *vvas_metaconvert_create (VvasContext *vvas_ctx, VvasMetaConvertConfig *cfg,VvasLogLevel log_level, VvasReturnType *ret)

Convert VvasInferPrediction to VvasOverlayShapeInfo using below API.

.. code-block::

	VvasReturnType vvas_metaconvert_prepare_overlay_metadata (VvasMetaConvert *meta_convert, VvasTreeNode *parent, VvasOverlayShapeInfo *shape_info)

Here parent is nothing but VvasInferPrediction.node.

Now as VvasInferPrediction is converted to VvasOverlayShapeInfo use Overlay module to draw the inference data using below API.

.. code-block::

	VvasReturnType vvas_overlay_process_frame (VvasOverlayFrameInfo *pFrameInfo)

As VvasInferPrediction is consumed, free using below API.

.. code-block::

	void vvas_inferprediction_free(VvasInferPrediction *meta)

Once done with MetaConvert instance, destroy it using below API.

.. code-block::

	void vvas_metaconvert_destroy (VvasMetaConvert *meta_convert)


************
Sink
************

Now as inference data is rendered over original decoded data, consume this buffer; either dump it into file or display it or do whatever you want to do with it.
Once this buffer is consumed, re-fed it to the decoder for reusing it.


**************************************
Compiling VVAS Core Application
**************************************
VVAS Core installs its libraries and header files into /opt/xilinx/vvas/ directory.
It also installs the pkg-cfg files for user to easily get the compiler flags and libraries to link VVAS core libraries.

VVAS Core installs below package configuration file.
    * VVAS Core Utils:

	.. code-block::

		pkg-config --cflags --libs vvas-utils

    * VVAS Core Libs:

	.. code-block::

		pkg-config --cflags --libs vvas-core

VVAS Core libraries are dependent on XRT, compiler flags and libraries for XRT libraries can be found using below command.

.. code-block::

	pkg-config --cflags --libs xrt

First do source /opt/xilinx/vvas/setup.sh, then above pkg-cfg command can be used to get the compiler flags and libraries for VVAS utils and core libraries.

Along with these compiler flags, user need to enable few macros also while compiling the test application.
    * VVAS_GLIB_UTILS : This macro is must as of now, as VVAS Core Utils is based on Glib implementation.
    * XLNX_PCIe_PLATFORM: Set it if compiling the application for PCIe platform.
    * XLNX_EMBEDDED_PLATFORM: Set it if compiling the application for Edge platform.
    * XLNX_V70_PLATFORM: Set it if compiling the application for V70 platform.

Let’s compile test_cascade_yolov3_3xresnet.cpp test application developed for V70 platform located at vvas-core/test/app/test_cascade_yolov3_3xresnet.cpp using below Makefile commands

.. code-block::

    all: test_video_ml
    XRT_PKG_CFG=`pkg-config --cflags --libs xrt`
    VVAS_UTILS_PKG_CFG=`pkg-config --cflags --libs vvas-utils`
    VVAS_CORE_PKG_CFG=`pkg-config --cflags --libs vvas-core`
    VVAS_CORE_MACROS=-DVVAS_GLIB_UTILS -DXLNX_PCIe_PLATFORM -DXLNX_V70_PLATFORM

    test_video_ml:
        g++ -Wall -g test_cascade_yolov3_3xresnet.cpp $(XRT_PKG_CFG) $(VVAS_UTILS_PKG_CFG) $(VVAS_CORE_PKG_CFG) $(VVAS_CORE_MACROS) -o test_video_ml

    clean:
        rm test_video_ml
