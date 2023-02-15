..
   Copyright 2022 Xilinx, Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

#######################################################
VVAS GStreamer Plug-ins for PCIe/Data Center Platforms
#######################################################

VVAS 3.0 Release supports below mentioned plug-ins that are needed for PCIe/Data Center platforms for supporting Machine Learning solutions:

1.  Video Decoder (:ref:`vvas_xvideodec`)

2.  ABR Scaler (:ref:`vvas_xabrscaler`)

Following sections details about each plugin.

.. _vvas_xvideodec:

vvas_xvideodec
-------------------

In Versal devices, there is an IP, VDU, for video decoder. This IP supports decoding of H264/HEVC formats. Please refer to `VDU PG414 <https://www.xilinx.com/content/dam/xilinx/support/documents/ip_documentation/vdu/v1_0/pg414-vdu.pdf>`_ for more information on the Xilinx VDU block. This guide provides details about this IP and how to configure it.

vvas_xvideodec plugin is provided to control the VDU hardened IP for Video Decoding jobs. This plug-in is used only in PCIe/DC platform only. For Embedded platforms, there is a separate plug-in that is covered in
:ref:`omx_encoder_decoder`. For implementation details, refer to `vvas_xvideodec source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/sys/vdudec>`_


.. figure:: ../images/video_decoder.png

Input and Output
================

This plugin supports H264 and HEVC encoded streams of various profile, level values as input. For complete list of supported profiles and levels, please refer to `VDU PG414 <https://www.xilinx.com/content/dam/xilinx/support/documents/ip_documentation/vdu/v1_0/pg414-vdu.pdf>`_
The native output of this plugin is NV12 format. For any other desired output format, it should be software-converted and will affect performance.

Control Parameters and Plug-in Properties
==========================================

The following table lists the GStreamer plug-in properties supported by the vvas_xvideodec plug-in.

Table 1: vvas_xvideodec Plug-in Properties

+------------------------+-------------+---------------+------------------------+------------------+
|                        |             |               |                        |                  |
|  **Property Name**     |   **Type**  | **Range**     | **Default**            | **Description**  |
|                        |             |               |                        |                  |
+========================+=============+===============+========================+==================+
| avoid-output-copy      |   Boolean   | true/false    | False                  | Avoid output     |
|                        |             |               |                        | frames copy      |
|                        |             |               |                        | even when        |
|                        |             |               |                        | downstream does  |
|                        |             |               |                        | not support      |
|                        |             |               |                        | GstVideoMeta     |
|                        |             |               |                        | metadata         |
+------------------------+-------------+---------------+------------------------+------------------+
| avoid-dynamic-alloc    |   Boolean   |  true/false   | True                   | Avoid dynamic    |
|                        |             |               |                        | allocation of    |
|                        |             |               |                        | output buffers   |
+------------------------+-------------+---------------+------------------------+------------------+
|    dev-idx             |    Integer  | 0 to 31       |    0                   | Device index     |
|                        |             |               |                        | This is valid    |
|                        |             |               |                        | only in PCIe/    |
|                        |             |               |                        | Data Center      |
|                        |             |               |                        | platforms.       |
+------------------------+-------------+---------------+------------------------+------------------+
| i-frame-only           |    Boolean  |  true/false   |    false               | Whether to       |
|                        |             |               |                        | decode 'I' frames|
|                        |             |               |                        | only or not from |
|                        |             |               |                        | the given input  |
|                        |             |               |                        | encoded stream   |
+------------------------+-------------+---------------+------------------------+------------------+
| instance-id            |    Integer  | 0 to 1        |    0                   | Select one of    |
|                        |             |               |                        | the VDU hardware |
|                        |             |               |                        | IP instances     |
+------------------------+-------------+---------------+------------------------+------------------+
| disable-hdr10-sei      |    Boolean  |  true/false   |    false               | Whether to       |
|                        |             |               |                        | passthrough      |
|                        |             |               |                        | HDR10/10+        |
|                        |             |               |                        | SEI messages or  |
|                        |             |               |                        | not              |
+------------------------+-------------+---------------+------------------------+------------------+
| interpolate-timestamps |    Boolean  |  true/false   |    false               | Whether to       |
|                        |             |               |                        | interpolate      |
|                        |             |               |                        | timestamps       |
|                        |             |               |                        | or not           |
+------------------------+-------------+---------------+------------------------+------------------+
|                        |    String   |    N/A        | decoder:{decoder_1}    | Kernel name      |
| kernel-name            |             |               |                        | and              |
|                        |             |               |                        | instance         |
|                        |             |               |                        | separated        |
|                        |             |               |                        | by a colon.      |
+------------------------+-------------+---------------+------------------------+------------------+
|   low-latency          |    Boolean  | true/false    |    false               | Whether to enable|
|                        |             |               |                        | low latency      |
|                        |             |               |                        | or not           |
+------------------------+-------------+---------------+------------------------+------------------+
|  num-entropy-buf       |  Unsigned   | 2 - 10        |    2                   | Number of        |
|                        |  Integer    |               |                        | entropy buffers  |
+------------------------+-------------+---------------+------------------------+------------------+
|  splitbuff-mode        |  Boolean    | true/false    |    false               | Whether to enable|
|                        |             |               |                        | splitbuff mode or|
|                        |             |               |                        | not              |
+------------------------+-------------+---------------+------------------------+------------------+
|                        |    String   |    N/A        | null                   | The              |
| xclbin-location        |             |               |                        | location of      |
|                        |             |               |                        | xclbin.          |
+------------------------+-------------+---------------+------------------------+------------------+

Example Pipelines
-----------------

The following pipeline takes MP4 file with H264 codec as an input and provides raw decoded output in NV12 format. Please refer above table for other parameters and values.

`dev-idx` represents the zero based index of the device on which this decoder to be launched. Please note that 'dev-idx' parameter range can vary from 0 to N-1 where N can be obtained with below command:

.. code-block:: shell

    N=xbutil  examine | grep xilinx_v70 | wc -l

.. code-block:: shell

        gst-launch-1.0 -v filesrc location=<input compressed H264 stream in MP4 container> \
        ! qtdemux ! h264parse \
        ! vvas_xvideodec kernel-name=kernel_vdu_decoder:{kernel_vdu_decoder_0} dev-idx=0 instance-id=0 avoid-output-copy=false xclbin-location=<xclbin file path> \
        ! fpsdisplaysink name=fpssink video-sink=fakesink text-overlay=false sync=false

kernel name is mandatory and the name represents both the name of the decoder kernel as well as its instance. In above example, "kernel-name=kernel_vdu_decoder:{kernel_vdu_decoder_0}" represents decoder kernel name as "kernel_vdu_decoder" and instance "0". Instance Id range can be known from the below command:

.. code-block:: shell

       xbutil examine -d <PCI bdf>

Currently, 2 instances of video decoder IP (VDU) is supported and can be selected with the plugin parameter 'instance-id'. Above example uses instance id = 0.

vvas_xabrscaler 
----------------

In several usecases, an input frame needs to be resized/scaled to different resolutions, to be encoded at different bitrates. VVAS has provided hardware accelerated IP, multiscaler, that can resize the input frame into several different resolutions and formats. `vvas_xabrscaler` plugin controls this IP. This takes one raw input stream as input and produces one or more scaled/resized raw streams.
This is a common plugin for both DC and Embedded platforms. So,the details are captured in common plugins section.
For more details on using this plugin, please refer to :ref:`vvas_xabrscaler`

For more information, contact `vvas_discuss@amd.com`.
