.. _vvas_xabrscaler:

vvas_xabrscaler
=================

There are certain situations where the available frame resolution and color format may not be suitable for the next component's consumption. For instance, in machine learning applications, inputs can be sourced from different resolutions and color formats, but the ML models require fixed resolution and specific color formats. Thus, it becomes necessary to resize the input image to a different resolution and convert its color space, for example, to BGR.

In some adaptive bit rate (ABR) use cases, a single video is encoded at different bit rates to allow streaming under different network bandwidth conditions without artifacts. This requires the input frame to be decoded, resized to different resolutions, and then re-encoded. To accelerate these processes, ``vvas_xabrscaler`` is a GStreamer plug-in that can take one input frame and produce multiple output frames with different resolutions and color formats. This plug-in can also perform the ``pre-processing`` operations like mean subtraction, normalization, and cropping. For implementation details, please refer to `vvas_xabrscaler source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/sys/abrscaler>`_.

This plug-in supports:

* Single input, multiple output pads

* Color space conversion

* Resize

* Each output pad has independent resolution and color space conversion capability.

* Crop of input buffer

Prerequisite
----------------

To use this plug-in, your hardware design must have the ``image_processing`` kernel available and it must be configured to support the required color formats and maximum resolution for your application. You can find the configuration details for the ``image_processing`` kernel, including the supported maximum resolution and color formats, in `image-processing kernel config <https://github.com/Xilinx/VVAS/blob/master/vvas-accel-hw/image_processing/image_processing_config.h>`_

Input and Output
------------------------

This plug-in accepts buffers with the following color format standards:

* RGBx
* YUY2
* r210
* Y410
* NV16
* NV12
* RGB
* v308
* I422_10LE
* GRAY8
* NV12_10LE32
* BGRx
* GRAY10_LE32
* BGRx
* UYVY
* BGR
* RGBA
* BGRA
* I420
* GBR

.. important:: Make sure that the color formats needed for your application are supported by the image-processing hardware kernel. 

``image_processing`` kernel configuration, like maximum resolution supported, color formats etc. can be found in `image-processing kernel config <https://github.com/Xilinx/VVAS/blob/master/vvas-accel-hw/image_processing/image_processing_config.h>`_


Control Parameters and Plug-in Properties
------------------------------------------------

The following table lists the GStreamer plug-in properties supported by the vvas_xabrscaler plug-in.

Table 3: vvas_xabrscaler Plug-in Properties

+--------------------+-------------+---------------+------------------------+------------------+
|                    |             |               |                        |                  |
|  **Property Name** |   **Type**  | **Range**     | **Default**            | **Description**  |
|                    |             |               |                        |                  |
+====================+=============+===============+========================+==================+
| avoid-output-copy  |   Boolean   | true/false    | False                  | Avoid output     |
|                    |             |               |                        | frames copy on   |
|                    |             |               |                        | all source pads  |
|                    |             |               |                        | even when        |
|                    |             |               |                        | downstream does  |
|                    |             |               |                        | not support      |
|                    |             |               |                        | GstVideoMeta     |
|                    |             |               |                        | metadata         |
+--------------------+-------------+---------------+------------------------+------------------+
| enable-pipeline    |    Boolean  |  true/false   | false                  | Enable buffer    |
|                    |             |               |                        | pipelining to    |
|                    |             |               |                        | improve          |
|                    |             |               |                        | performance in   |
|                    |             |               |                        | non zero-copy    |
|                    |             |               |                        | use cases        |
+--------------------+-------------+---------------+------------------------+------------------+
| in-mem-bank        | Unsigned int|  0 - 65535    | 0                      | VVAS input memory|
|                    |             |               |                        | bank to allocate |
|                    |             |               |                        | memory           |
+--------------------+-------------+---------------+------------------------+------------------+
| out-mem-bank       | Unsigned int|  0 - 65535    | 0                      | VVAS o/p memory  |
|                    |             |               |                        | bank to allocate |
|                    |             |               |                        | memory           |
+--------------------+-------------+---------------+------------------------+------------------+
|                    |    string   |    N/A        | ./binary_container_1   | The              |
|  xclbin-location   |             |               | xclbin                 | location of      |
|                    |             |               |                        | xclbin.          |
+--------------------+-------------+---------------+------------------------+------------------+
|                    |    string   |    N/A        |                        | Kernel name      |
| kernel-name        |             |               | image_processing:      | and              |
|                    |             |               | {image_processing_1}   | instance         |
|                    |             |               |                        | separated        |
|                    |             |               |                        | by a colon.      |
+--------------------+-------------+---------------+------------------------+------------------+
|    dev-idx         |    integer  | 0 to 31       |    0                   | Device index     |
|                    |             |               |                        | This is valid    |
|                    |             |               |                        | only in PCIe/    |
|                    |             |               |                        | Data Center      |
|                    |             |               |                        | platforms.       |
+--------------------+-------------+---------------+------------------------+------------------+
|    ppc             |    integer  | 1, 2, 4       |    2                   | Pixel per        |
|                    |             |               |                        | clock            |
|                    |             |               |                        | supported        |
|                    |             |               |                        | by a multi-      |
|                    |             |               |                        | scaler           |
|                    |             |               |                        | kernel           |
+--------------------+-------------+---------------+------------------------+------------------+
|   scale-mode       |    integer  | 0, 1, 2       |    0                   | Scale algorithm  |
|                    |             |               |                        | to use:          |
|                    |             |               |                        | 0:BILINEAR       |
|                    |             |               |                        | 1:BICUBIC        |
|                    |             |               |                        | 2:POLYPHASE      |
+--------------------+-------------+---------------+------------------------+------------------+
|    coef-load-type  |  integer    | 0 => Fixed    |    1                   | Type of filter   |
|                    |             | 1 => Auto     |                        | Coefficients to  |
|                    |             |               |                        | be used: Fixed   |
|                    |             |               |                        | or Auto          |
|                    |             |               |                        | generated        |
+--------------------+-------------+---------------+------------------------+------------------+
|    num-taps        |  integer    | 6=>6 taps     |    1                   | Number of filter |
|                    |             | 8=>8 taps     |                        | taps to be used  |
|                    |             | 10=>10 taps   |                        | for scaling      |
|                    |             | 12=>12 taps   |                        |                  |
+--------------------+-------------+---------------+------------------------+------------------+
|    alpha-b         |  float      | 0 to 128      |    0                   | Mean subtraction |
|                    |             |               |                        | for blue channel |
|                    |             |               |                        | , needed for PPE |
+--------------------+-------------+---------------+------------------------+------------------+
|    alpha-g         |  float      | 0 to 128      |    0                   | Mean subtraction |
|                    |             |               |                        | for green channel|
|                    |             |               |                        | , needed for PPE |
+--------------------+-------------+---------------+------------------------+------------------+
|    alpha-r         |  float      | 0 to 128      |    0                   | Mean subtraction |
|                    |             |               |                        | for red  channel |
|                    |             |               |                        | , needed for PPE |
+--------------------+-------------+---------------+------------------------+------------------+
|    beta-b          |  float      | 0 to 1        |    1                   | Scaling for blue |
|                    |             |               |                        | channel, needed  |
|                    |             |               |                        | for PPE          |
+--------------------+-------------+---------------+------------------------+------------------+
|    beta-g          |  float      | 0 to 1        |    1                   | scaling for green|
|                    |             |               |                        | channel, needed  |
|                    |             |               |                        | for PPE          |
+--------------------+-------------+---------------+------------------------+------------------+
|    beta-r          |  float      | 0 to 1        |    1                   | scaling for red  |
|                    |             |               |                        | channel, needed  |
|                    |             |               |                        | for PPE          |
+--------------------+-------------+---------------+------------------------+------------------+
|    crop-x          |  unsigned   | 0 to          |    0                   | Crop X           |
|                    |  integer    | 4294967295    |                        | coordinate       |
+--------------------+-------------+---------------+------------------------+------------------+
|    crop-y          |  unsigned   | 0 to          |    0                   | Crop Y           |
|                    |  integer    | 4294967295    |                        | coordinate       |
+--------------------+-------------+---------------+------------------------+------------------+
|    crop-width      |  unsigned   | 0 to          |    0                   | Crop width (     |
|                    |  integer    | 4294967295    |                        | minimum: 64), if |
|                    |             |               |                        | this is 0 or not |
|                    |             |               |                        | set, it will be  |
|                    |             |               |                        | calculated as    |
|                    |             |               |                        | input width -    |
|                    |             |               |                        | `crop-x`         |
|                    |             |               |                        |                  |
+--------------------+-------------+---------------+------------------------+------------------+
|    crop-height     |  unsigned   | 0 to          |    0                   | Crop height (    |
|                    |  integer    | 4294967295    |                        | minimum: 64), if |
|                    |             |               |                        | this is 0 or not |
|                    |             |               |                        | set, it will be  |
|                    |             |               |                        | calculated as    |
|                    |             |               |                        | input height -   |
|                    |             |               |                        | `crop-y`         |
+--------------------+-------------+---------------+------------------------+------------------+
| software-scaling   |    Boolean  |  true/false   | false                  | Enable software  |
|                    |             |               |                        | scaling instead  |
|                    |             |               |                        | of accelerated   |
|                    |             |               |                        | scaling.         |
+--------------------+-------------+---------------+------------------------+------------------+


.. note::

       Image-processing IP has some alignment requirement, hence user given parameters for crop are aligned as per the IP requirement, alignment ensures that it covers the region of crop specified by user, hence final cropped image may have extra pixels cropped.


Example Pipelines
-------------------------


One input one output
^^^^^^^^^^^^^^^^^^^^^^^^^^

The following example configures vvas_xabrscaler in one input and one output mode. The input to the scaler is 1280 x 720, NV12 frames that are resized to 640 x 360 resolution, and the color format is changed from NV12 to BGR.

.. code-block::

      gst-launch-1.0 videotestsrc num-buffers=100 \
      ! "video/x-raw, width=1280, height=720, format=NV12" \
      ! vvas_xabrscaler xclbin-location="/run/media/mmcblk0p1/dpu.xclbin" kernel-name=image_processing:{image_processing_1} \
      ! "video/x-raw, width=640, height=360, format=BGR" ! fakesink -v


One input multiple output
^^^^^^^^^^^^^^^^^^^^^^^^^^

The following example configures vvas_xabrscaler for one input and three outputs. The input is 1920 x 1080 resolution in NV12 format. There are three output formats:

* 1280 x 720 in BGR format

* 300 x 300 in RGB format

* 640 x 480 in NV12 format


.. code-block::

        gst-launch-1.0 videotestsrc num-buffers=100 \
        ! "video/x-raw, width=1920, height=1080, format=NV12, framerate=60/1" \
        ! vvas_xabrscaler xclbin-location="/run/media/mmcblk0p1/dpu.xclbin" kernel-name=image_processing:{image_processing_1} name=sc sc.src_0 \
        ! queue \
        ! "video/x-raw, width=1280, height=720, format=BGR" \
        ! fakesink sc.src_1 \
        ! queue \
        ! "video/x-raw, width=300, height=300, format=RGB" \
        ! fakesink sc.src_2 \
        ! queue \
        ! "video/x-raw, width=640, height=480, format=NV12" \
        ! fakesink -v


Crop with multiple output:
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following example configures ``vvas_xabrscaler`` for one input and three outputs. The input is 1920 x 1080 resolution in NV12 format.
This input is cropped at X:140, Y:300, Width:640, Height:480.

Cropped input is scaled and converted to below format:

* 640 * 480 in RGB format

* 320 * 240 in RGB format

* 256 * 256 in NV12 format

.. code-block::

       gst-launch-1.0 -v \
       videotestsrc num-buffers=10 ! video/x-raw,format=NV12,width=1920,height=1080 \
       ! vvas_xabrscaler xclbin-location="/run/media/mmcblk0p1/dpu.xclbin" kernel-name=image_processing:{image_processing_1} crop-x=140 crop-y=300 crop-width=640 crop-height=480 name=sc \
       sc.src_0 ! queue ! video/x-raw,format=RGB,width=640,height=480 ! filesink location=480p.yuv \
       sc.src_1 ! queue ! video/x-raw,format=RGB,width=320,height=240 ! filesink location=240p.yuv \
       sc.src_2 ! queue ! video/x-raw,format=NV12,width=256,height=256 ! filesink location=256p.yuv -v

vvas_xabrscaler with software scaling kernel
------------------------------------------------

VVAS plugin "vvas_xabrscaler" can also work with software implementation of the IP. The same plugin can be used to invoke the software scaling functionality.
User needs to set few properties on "vvas_xabrscaler" plugin to invoke the software scaling, please refer the example pipeline mentioned below. The current release version
supports only fixed and 12 tap filter coefficients. Below are the formats supported by the current release version.

* NV12
* RGB
* GRAY8
* BGR
* I420

Note: For GRAY8, only scaling is supported, cross format conversion is not supported.

Example pipeline:
^^^^^^^^^^^^^^^^^^^

.. code-block::

		gst-launch-1.0  videotestsrc num-buffers=10  \
		! video/x-raw, width=1920, height=1080, format=NV12  \
		! vvas_xabrscaler kernel-name="image_processing_sw:{image_processing_sw_1}" software-scaling=true coef-load-type=0 num-taps=12 \
		! video/x-raw, width=1280, height=720, format=NV12 !  filesink location=output_sw_scale.nv12 -v

..
  ------------
  
  © Copyright 2023, Advanced Micro Devices, Inc.
  
   MIT License

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:
   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

