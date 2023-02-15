vvas_xabrscaler
======================

There are several use cases where the available frame resolution and color formats may not be suitable for the consumption by the next component. For example, in case of Machine Learning applications, input can be from different sources, resolutions, but ML models work on a fixed resolution. In such cases, the input image needs to be re-sized to a different resolution. Also, the input image color format may be YUV/NV12, but the ML models require image to be in BGR format. In this case we need to do the color space conversion as well. ML model may also require some pre-processing, like Mean Subtraction, Normalization etc. on the input image. 

In adaptive bit rate (ABR) use cases, one video is encoded at different bit rates so that it can be streamed in different network bandwidth conditions without any artifacts. To achieve this, input frame is decoded, resized to different resolutions and then re-encoded. vvas_xabrscaler is a plug-in that takes one input frame and can produce several outputs frames having different resolutions and color formats. The ``vvas_xabrscaler`` is a GStreamer plug-in developed to accelerate the resize and color space conversion, Mean Subtraction, Normalization, and cropping. For more implementation details, refer to `vvas_xabrscaler source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/sys/abrscaler>`_.

This plug-in supports:

* Single input, multiple output pads

* Color space conversion

* Resize

* Each output pad has independent resolution and color space conversion capability.

* Crop of input buffer

.. important:: The `vvas_xabrscaler` plug-in controls the image_processing kernel. If your application uses this plug-in, then make sure that image_processing kernel is included in your hardware design.

.. important:: Make sure that the image_processing hardware kernel supports maximum resolution required by your application.

As a reference, maximum resolution supported by image_processing kernel in ``Smart Model Select`` example design can be found in `image-processing kernel config <TBD>`_

Prerequisite
----------------

This plug-in requires the image_processing kernel to be available in the hardware design and the required color formats are enabled. See :ref:`Image Processing Kernel <image-processing-kernel>`

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


As a reference, image-processing configuration for ``smart model select`` example design can be found in `image-processing configuration <TBD>`_


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
