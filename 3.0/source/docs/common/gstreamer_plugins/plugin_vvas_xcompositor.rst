.. _vvas_xcompositor:

vvas_xcompositor
================

``vvas_xcompositor`` is a Gstreamer plug-in that composes multiple video frames into a single frame. The composition can be hardware accelerated using ``Image_processing`` IP or it can be performed using software implementation as well in case hardware IP is not available. This plug-in can accept frames in different resolutions, color formats on each input pad. Output resolution, color format and framerate can be set to the plugin through output caps. If capabilities are not set through the output caps, then output frame resolution will be set to the resolution of the biggest incoming video stream, output framerate will be set to the highest framerate among all incoming streams and the color format will be that of the first input stream.

If the input and output resolution and formats differ, the vvas_xcompositor plug-in will perform the color space conversion and resize operation.


Parameters related to position of input frame in output frame can be specified using the input pad properties mentioned below :
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------

* "xpos": The x-co-ordinate position of the top-left corner of the current frame in output frame.
* "ypos": The y-co-ordinate position of the top-left corner of the current frame in output frame.
* "width": The width of the current frame in the output frame; If the input width and the width in output frame are different, then resize operation will be performed.
* "height": The height of the current frame in the output frame; If the input height and the height in output frame are different, then resize operation will be performed.
* "zorder": The z-order position of the current frame in the composition.


If composition is configured in such a way that xpos+width (or) ypos+height is out of bounds of the output frame's width or height then the pixels those reside out of bounds get cropped by the plugin.

For implementation details, please refer to `vvas_xcompositor source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/sys/compositor>`_

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

.. important:: 

    Ensure that the color formats required for your application are supported by the image-processing hardware kernel.

As a reference, multi-scaler configuration for ``smart model select`` example design can be found in `image-processing configuration <https://github.com/Xilinx/VVAS/blob/master/vvas-examples/Embedded/smart_model_select/image_processing_config.h>`_

.. figure:: ../../images/compositor.png


Plugin properties
-----------------

Table 14: ``vvas_xcompositor`` Plug-in Properties

+--------------------+-------------+---------------+---------------------+----------------------+
|                    |             |               |                     |                      |
|  **Property Name** |   **Type**  |  **Range**    |     **Default**     |   **Description**    |
|                    |             |               |                     |                      |
+====================+=============+===============+=====================+======================+
| avoid-output-copy  |   Boolean   | true or false |        false        | Avoid output frames  |
|                    |             |               |                     | copy on all source   |
|                    |             |               |                     | pads even when       |
|                    |             |               |                     | downstream does not  |
|                    |             |               |                     | support GstVideoMeta |
|                    |             |               |                     | metadata             |
+--------------------+-------------+---------------+---------------------+----------------------+
|    best-fit        |   Boolean   | true or false |        false        | downscale/upscale    |
|                    |             |               |                     | the input video to   |
|                    |             |               |                     | best-fit in each     |
|                    |             |               |                     | window               |
+--------------------+-------------+---------------+---------------------+----------------------+
|     dev-idx        |   Integer   |   -1 to 31    |         -1          | Device index         |
|                    |             |               |                     | Valid only for       |
|                    |             |               |                     | PCIe/Data Center     |
|                    |             |               |                     | platforms            |
+--------------------+-------------+---------------+---------------------+----------------------+
| enable-pipeline    |   Boolean   | true or false |        false        | Enable buffer        |
|                    |             |               |                     | pipelining to        |
|                    |             |               |                     | improve performance  |
|                    |             |               |                     | in non zero-copy use |
|                    |             |               |                     | cases                |
+--------------------+-------------+---------------+---------------------+----------------------+
|   in-mem-bank      |   Unsigned  |  0 - 65535    |          0          | VVAS input memory    |
|                    |   Integer   |               |                     | bank to allocate     |
|                    |             |               |                     | memory               |
+--------------------+-------------+---------------+---------------------+----------------------+
|   out-mem-bank     |   Unsigned  |  0 - 65535    |          0          | VVAS output memory   |
|                    |   Integer   |               |                     | bank to allocate     |
|                    |             |               |                     | memory               |
+--------------------+-------------+---------------+---------------------+----------------------+
|        ppc         |   Integer   |   1, 2, 4     |          2          | Pixel per clock      |
|                    |             |               |                     | supported by the     |
|                    |             |               |                     | image-processing     |
|                    |             |               |                     | kernel               |
+--------------------+-------------+---------------+---------------------+----------------------+
|    scale-mode      |   Integer   |   0, 1, 2     |          0          | Scale algorithm      |
|                    |             |               |                     | to use:              |
|                    |             |               |                     | 0:BILINEAR           |
|                    |             |               |                     | 1:BICUBIC            |
|                    |             |               |                     | 2:POLYPHASE          |
+--------------------+-------------+---------------+---------------------+----------------------+
|   coef-load-type   |  Integer    |  0 => Fixed   |          1          | Type of filter       |
|                    |             |  1 => Auto    |                     | Coefficients to be   |
|                    |             |               |                     | used: Fixed or Auto  |
|                    |             |               |                     | generated            |
+--------------------+-------------+---------------+---------------------+----------------------+
|      num-taps      |  Integer    | 6=>6 taps     |          1          | Number of filter     |
|                    |             | 8=>8 taps     |                     | taps to be used for  |
|                    |             | 10=>10 taps   |                     | scaling              |
|                    |             | 12=>12 taps   |                     |                      |
+--------------------+-------------+---------------+---------------------+----------------------+
|    kernel-name     |   String    |      NA       | image_processing    | String defining the  |
|                    |             |               | :{image_processing  | kernel name and      |
|                    |             |               | _1}                 | instance as          |
|                    |             |               |                     | mentioned in xclbin  |
+--------------------+-------------+---------------+---------------------+----------------------+
|  xclbin-location   |   String    |      NA       |        NULL         | Location of the      |
|                    |             |               |                     | xclbin to program    |
|                    |             |               |                     | devices              |
+--------------------+-------------+---------------+---------------------+----------------------+
| software-scaling   |    Boolean  |  true/false   |        false        | Enable software      |
|                    |             |               |                     | scaling instead      |
|                    |             |               |                     | of accelerated       |
|                    |             |               |                     | scaling.             |
+--------------------+-------------+---------------+---------------------+----------------------+

vvas_xcompositor pad properties
-------------------------------

Table 15: ``vvas_xcompositor`` Pad Properties

+--------------------+-------------+---------------+---------------------+----------------------+
|                    |             |               |                     |                      |
|  **Property Name** |   **Type**  |  **Range**    |     **Default**     |   **Description**    |
|                    |             |               |                     |                      |
+====================+=============+===============+=====================+======================+
|        xpos        |  Unsigned   | 0 to          |          0          | The x-co-ordinate    | 
|                    |  Integer    | 2147483647    |                     | position of the      |
|                    |             |               |                     | top-left corner of   |
|                    |             |               |                     | the current farme in |
|                    |             |               |                     | output buffer.       |
+--------------------+-------------+---------------+---------------------+----------------------+
|        ypos        |  Unsigned   | 0 to          |          0          | The y-co-ordinate    | 
|                    |  Integer    | 2147483647    |                     | position of the      |
|                    |             |               |                     | top-left corner of   |
|                    |             |               |                     | the current farme in |
|                    |             |               |                     | output buffer.       |
+--------------------+-------------+---------------+---------------------+----------------------+
|       height       |  Integer    | -1 to         |          -1         | The height of the    |
|                    |             | 2147483647    |                     | current picture in   |
|                    |             |               |                     | the output buffer;   |
|                    |             |               |                     | If the input height  |
|                    |             |               |                     | and the height in    | 
|                    |             |               |                     | output buffer are    |
|                    |             |               |                     | different, then      | 
|                    |             |               |                     | resize operation     |
|                    |             |               |                     | will be performed.   |
|                    |             |               |                     | Setting default/-1   |
|                    |             |               |                     | treats o/p height as |
|                    |             |               |                     | input height         |
+--------------------+-------------+---------------+---------------------+----------------------+
|        width       |  Integer    | -1 to         |          -1         | The width of the     |
|                    |             | 2147483647    |                     | current picture in   |
|                    |             |               |                     | the output buffer;   |
|                    |             |               |                     | If the input width   |
|                    |             |               |                     | and the width in     | 
|                    |             |               |                     | output buffer are    |
|                    |             |               |                     | different, then      | 
|                    |             |               |                     | resize operation     |
|                    |             |               |                     | will be performed.   |
|                    |             |               |                     | Setting default/-1   |
|                    |             |               |                     | treats o/p width  as |
|                    |             |               |                     | input width          |
+--------------------+-------------+---------------+---------------------+----------------------+
|     zorder         |  Unsigned   |  -1 to 16     |        -1           | The z-order positon  |
|                    |  Integer    |               |                     | of the picture in    |
|                    |             |               |                     | the composition.     |
+--------------------+-------------+---------------+---------------------+----------------------+


The example pipeline with ``vvas_xcompositor`` plug-in is as mentioned below.


.. code-block::

        #! /bin/bash

        PAD_PROPERTIES="\
        sink_0::xpos=0 sink_0::ypos=0 \
        sink_1::xpos=1920 sink_1::ypos=0 \
        sink_2::xpos=0 sink_2::ypos=1080 \
        sink_3::xpos=1920 sink_3::ypos=1080 \
        "

        gst-launch-1.0 -v  filesrc location=$1 !\
        h264parse !\
        omxh264dec !\
        comp.sink_0 \
        vvas_xcompositor kernel-name=image_processing:{image_processing_1} \
        xclbin-location=/run/media/mmcblk0p1/dpu.xclbin $PAD_PROPERTIES name=comp !\
        video/x-raw , width=3840, height=2160 , format=NV12  !\
        queue !\
        fpsdisplaysink video-sink="kmssink  bus-id=a0130000.v_mix async=true" text-overlay=false sync=false \
        filesrc location=$2 !\
        h264parse !\
        omxh264dec name=decoder_1 !\
        queue !\
        comp.sink_1 \
        filesrc location=$3 !\
        h264parse !\
        omxh264dec !\
        queue !\
        comp.sink_2 \
        filesrc location=$4 !\
        h264parse !\
        omxh264dec !\
        queue !\
        comp.sink_3

..

vvas_xcompositor with software scaling kernel
------------------------------------------------

VVAS plugin "vvas_xcompositor" can also work with software implementation of the IP. User has to set "software-scaling" property to "true", set the "kernel-name" to "image_processing_sw:{image_processing_sw}",
also set "coef-load-type" to "fixed" type and set "num-taps" to 12. Below are the formats supported by the current implementation.

* NV12
* RGB
* GRAY8
* BGR
* I420

Note: For GRAY8, only scaling is supported, cross format conversion is not supported.

Example pipeline:
^^^^^^^^^^^^^^^^^^^

.. code-block::

        #! /bin/bash

        PAD_PROPERTIES="\
        sink_0::xpos=0 sink_0::ypos=0 \
        sink_1::xpos=640 sink_1::ypos=0 \
        sink_2::xpos=0 sink_2::ypos=360 \
        sink_3::xpos=640 sink_3::ypos=360 \
        "

        gst-launch-1.0 -v  filesrc location=$1 !\
        h264parse !\
        omxh264dec !\
        comp.sink_0 \
        vvas_xcompositor kernel-name="image_processing_sw:{image_processing_sw_1}" \
        software-scaling=true coef-load-type=0 num-taps=12  $PAD_PROPERTIES name=comp !\
        video/x-raw , width=1280, height=720 , format=NV12  !\
        queue !\
        fpsdisplaysink video-sink="kmssink  bus-id=a0130000.v_mix async=true" text-overlay=false sync=false \
        filesrc location=$2 !\
        h264parse !\
        omxh264dec name=decoder_1 !\
        queue !\
        comp.sink_1 \
        filesrc location=$3 !\
        h264parse !\
        omxh264dec !\
        queue !\
        comp.sink_2 \
        filesrc location=$4 !\
        h264parse !\
        omxh264dec !\
        queue !\
        comp.sink_3

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

