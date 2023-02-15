..
   Copyright 2021 Xilinx, Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

**********************
Acceleration Kernels
**********************

This section covers details about how to build the HLS/RTL acceleration kernels. The kernels are HLS code that can be built using Vitis HLS and the compiled kernels are provided as ``.xo`` files. These compiled kernels then can be used with any Vitis platform.

==================
Building Kernels
==================

Follow the steps mentioned below to build the kernel. These are mostly applicable for building any HLS kernel.

#. Setup the Vitis HLS 2022.1 software. Refer to `Vitis Software Development Platform 2022.1 <https://www.xilinx.com/html_docs/xilinx2022.1/vitis_doc/gnq1597858079367.html>`_.

#. Edit the makefile to point the PLATFORM_FILE to any Vitis 2022.1 platform.

#. Edit the options in <kernel_folder>/kernel_config.h.

#. Make <kernel name>.

.. _image-processing-kernel:

=======================
Image Processing Kernel
=======================

Image-processing Kernel is capable of hardware accelerated resizing, color space conversion and cropping of region of interest in a frame. VVAS releases full HLS source code of the Image-processing 2.0 IP/Kernel.


Kernel Configuration
---------------------                    

Image-processing IP is highly configurable and supports several features. All applications may not need all the features. Keeping all the features enabled results in lots of FPGA resources being consumed. Hence it is recommended to enable only those features that are required for your applications. The image-processing kernel configuration can be edited by changing the `<VVAS_SOURCES>/vvas-accel-hw/image_processing/image_processing_config.h` file. The parameters in the following table can be changed as required.

Table 14: Image-Processing Kernel Configuration

+----------------------+----------------------+----------------------+
| **Parameter Macro**  | **Possible Values**  |    **Description**   |
|                      |                      |                      |
+======================+======================+======================+
| H                    |    1, 2, 4           | Pixels per clock     |
| SC_SAMPLES_PER_CLOCK |                      |                      |
+----------------------+----------------------+----------------------+
| HSC_MAX_WIDTH        |    64 to 7680        | Maximum width of the |
|                      |                      | resolution supported |
+----------------------+----------------------+----------------------+
| HSC_MAX_HEIGHT       |    64 to 4320        | Maximum height of    |
|                      |                      | the resolution       |
|                      |                      | supported            |
+----------------------+----------------------+----------------------+
| HS                   |    8, 10             | Bits per component   |
| C_BITS_PER_COMPONENT |                      |                      |
+----------------------+----------------------+----------------------+
| HSC_SCALE_MODE       |    0: Bilinear       | Scaling algorithm    |
|                      |                      |                      |
|                      |    1: Bicubic        |                      |
|                      |                      |                      |
|                      |    2: Polyphase      |                      |
+----------------------+----------------------+----------------------+
| HAS_RGBX8_YUVX8      |    0: Disable        | RGBX8 and YUVX8      |
|                      |                      | color format support |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| HAS_YUYV8            |    0: Disable        | YUYV8 color format   |
|                      |                      | support              |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| HAS_RGBA8_YUVA8      |    0: Disable        | RGBA8 and YUVA8      |
|                      |                      | color format support |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| HAS_RGBX10_YUVX10    |    0: Disable        | RGBX10 and YUVX10    |
|                      |                      | color format support |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| HAS_Y_UV8_Y_UV8_420  |    0: Disable        | Y_UV8 and Y_UV8_420  |
|                      |                      | color format support |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| HAS_RGB8_YUV8        |    0: Disable        | RGB8 and YUV8 color  |
|                      |                      | format support       |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| H                    |    0: Disable        | Y_UV10 and           |
| AS_Y_UV10_Y_UV10_420 |                      | Y_UV10_420 color     |
|                      |    1: Enable         | format support       |
+----------------------+----------------------+----------------------+
| HAS_Y8               |    0: Disable        | Y8 color format      |
|                      |                      | support              |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| HAS_Y10              |    0: Disable        | Y10 color format     |
|                      |                      | support              |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| HAS_BGRA8            |    0: Disable        | BGRA8 color format   |
|                      |                      | support              |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| HAS_BGRX8            |    0: Disable        | BGRX8 color format   |
|                      |                      | support              |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| HAS_UYVY8            |    0: Disable        | UYVY8 color format   |
|                      |                      | support              |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| HAS_BGR8             |    0: Disable        | BGR8 color format    |
|                      |                      | support              |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| HAS_R_G_B8           |    0: Disable        | R_G_B8 color format  |
|                      |                      | support              |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+
| HAS_Y_U_V8_420       |    0: Disable        | Y_U_V8_420 color     |
|                      |                      | format support       |
|                      |    1: Enable         |                      |
+----------------------+----------------------+----------------------+


Steps to Build Kernel
----------------------

#. Source the Vitis HLS 2022.2 software.

#. Edit the makefile to point the PLATFORM_FILE, `(.xpfm)` file, to any Vitis 2022.2 platform (tested using the ZCU104 base platform).

#. Edit the options in the image_processing/image_processing_config.h.

#. Make image_processing.xo. The generated output will be in the xo folder as xo/ image_processing.xo.
