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

VVAS 2.0 Release supports below mentioned plug-ins that are needed for PCIe/Data Center platforms for Transcoding solutions:

1.  VCU Decoder (:ref:`vvas_xvcudec`)
2.  VCU Encoder (:ref:`vvas_xvcuenc`)
3.  Lookahead (:ref:`vvas_xlookahead`)
4.  ABR Scaler (:ref:`vvas_xabrscaler`)

Following sections details about each plugin.

.. _vvas_xvcudec:

vvas_xvcudec
-------------------

In Zynq UltraScale+ MPSoC devices, there is an IP, VCU, for video decoder and encoder. This IP supports encoding and decoding of H264/HEVC formats. Please refer to `VCU PG252 <https://www.xilinx.com/support/documentation/ip_documentation/vcu/v1_2/pg252-vcu.pdf>`_ for more information on the Xilinx VCU block. This guide provides details about this IP and how to configure it.

vvas_xvcudec plugin is provided to control the VCU hardened IP for Video Decoding jobs. This plug-in is used only in PCIe/DC platform only. For Embedded platforms, there is a separate plug-in that is covered in 
:ref:`omx_encoder_decoder`. For implementation details, refer to `vvas_xvcudec source code <https://gitenterprise.xilinx.com/IPS-SSW/vvas/tree/master/vvas-gst-plugins/sys/vcudec>`_


.. figure:: ../images/vcu_decoder.png

Input and Output
================

This plugin supports H264 and HEVC encoded streams of various profile, level values as input. For complete list of supported profiles and levels, please refer to `VCU PG252 <https://www.xilinx.com/support/documentation/ip_documentation/vcu/v1_2/pg252-vcu.pdf>`_
The native output of this plugin is NV12 format. For any other desired output format, it should be software-converted and will affect performance.

Control Parameters and Plug-in Properties
==========================================

The following table lists the GStreamer plug-in properties supported by the vvas_xvcudec plug-in.

Table 1: vvas_xvcudec Plug-in Properties

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
| disable-hdr10-sei      |    Boolean  |  true/false   |    false               | Whether to       |
|                        |             |               |                        | passthrough      |
|                        |             |               |                        | HDR10/10+        |
|                        |             |               |                        | SEI messages or  |
|                        |             |               |                        | not              |
+------------------------+-------------+---------------+------------------------+------------------+
| in-mem-bank            | Unsigned    |  0 - 65535    | 0                      | VVAS input memory|
|                        | Integer     |               |                        | bank to allocate |
|                        |             |               |                        | memory           |
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
| out-mem-bank           | Unsigned int|  0 - 65535    | 0                      | VVAS o/p memory  |
|                        |             |               |                        | bank to allocate |
|                        |             |               |                        | memory           |
+------------------------+-------------+---------------+------------------------+------------------+
|  reservation-id        |  Unsigned   | 0-            |    0                   | Resource Pool    |
|                        |  Integer64  | 184467440     |                        | Reservation id   |
|                        |             | 73709551615   |                        |                  |
|                        |             | 12=>12 taps   |                        |                  |
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

`dev-idx` represents the zero based index of the device on which this decoder to be launched. Please note that 'dev-idx' parameter range can vary from 0 to N where N can be obtained with below command:

.. code-block:: shell

    N=xbutil  examine | grep xilinx_u30 | wc -l

.. code-block:: shell

        gst-launch-1.0 -v filesrc location=<input compressed H264 stream in MP4 container> \
        ! qtdemux ! h264parse \
        ! vvas_xvcudec dev-idx=3  avoid-output-copy=false xclbin-location=<xclbin file path> \
        ! fpsdisplaysink name=fpssink video-sink=fakesink text-overlay=false sync=false

.. _vvas_xvcuenc:

vvas_xvcuenc
-------------

In Zynq UltraScale+ MPSoC devices, there is an IP, VCU, for video decoder and encoder. This IP supports encoding and decoding of H264/HEVC formats. Please refer to `VCU PG252 <https://www.xilinx.com/support/documentation/ip_documentation/vcu/v1_2/pg252-vcu.pdf>`_ for more information on the Xilinx VCU block. This guide provides details about this IP and how to configure it.

vvas_xvcuenc plugin is provided to control the VCU hardened IP for Video Encoding jobs. This plug-in is used only in PCIe/DC platform only. For Embedded platforms, there is a separate plug-in that is covered in :ref:`omx_encoder_decoder`

.. figure:: ../images/vcu_encoder.png

For implementation details, refer to `vvas_xvcuenc source code <https://gitenterprise.xilinx.com/IPS-SSW/vvas/tree/master/vvas-gst-plugins/sys/vcuenc>`_

Input and Output
=================

This plug-in takes 8/10 bit raw input in NV12 format and generates H264/H265 Encoded streams. There are several parameters that affect the encoded stream. Details of each parameter are covered in the table below. These parameters are configured using plug-in properties.

Control Parameters and Plug-in Properties
==========================================

The following table lists the GStreamer plug-in properties supported by the vvas_xvcuenc plug-in.

Table 2: vvas_xvcuenc Plug-in Properties

+--------------------+-------------+---------------+------------------------+----------------------------------------+
|                    |             |               |                        |                                        |
|  **Property Name** |   **Type**  | **Range**     | **Default**            | **Description**                        |
|                    |             |               |                        |                                        |
+====================+=============+===============+========================+========================================+
| aspect-ratio       |   Enum      | 0: auto       |  0 (auto)              |  Display aspect ratio of the video     |
|                    |             |               |                        |  sequence to be written in SPS/VUI     |
|                    |             | 1: 4-3        |                        |                                        |
|                    |             |               |                        |                                        |
|                    |             | 2: 16-9       |                        |                                        |
|                    |             |               |                        |                                        |
|                    |             | 3: none       |                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| avc-lowlat         |   Boolean   | true/false    |  false                 |  Enable AVC low latency flag for H264  |
|                    |             |               |                        |  to run multiple cores in              |
|                    |             |               |                        |  ultra-low-latency mode.               |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| b-frames           |  Integer    |  -1 -         | -1                     | Number of B-frames between two         |
|                    |             |  4294967295   |                        | consecutive P-frames. Internally set   |
|                    |             |               |                        | to 0 in case of ultra-low-latency mode,|
|                    |             |               |                        | 2 otherwise if not-configured or       |
|                    |             |               |                        | configured with -1.                    |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| constrained-intra- |  Boolean    |  true/false   | false                  | If enabled, prediction only uses       |
| prediction         |             |               |                        | residual data and decoded samples      |
|                    |             |               |                        | from neighbouring coding blocks coded  |
|                    |             |               |                        | using intra prediction modes.          |
|                    |             |               |                        | This property is experimental.         |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| control-rate       |  Enum       | 0 - disable   | 1 (constant)           | Bitrate control method                 |
|                    |             |               |                        |                                        |
|                    |             | 1 - constant  |                        |                                        |
|                    |             |               |                        |                                        |
|                    |             | 2 - variable  |                        |                                        |
|                    |             |               |                        |                                        |
|                    |             | 3 -low-latency|                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| cpb-size           |  Unsigned   |  0 -          | 2000                   | Coded Picture Buffer as specified      |
|                    |  Integer    |  4294967295   |                        | when control-rate=disable. This        |
|                    |             |               |                        | property is experimental.              |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| dependent-slice    |  Boolean    |  true/false   | false                  | Specifies whether the additional       |
|                    |             |               |                        | slices are dependent another slice     |
|                    |             |               |                        | segments or regular slices in multiple |
|                    |             |               |                        | slicesencoding sessions. Used in H.265 |
|                    |             |               |                        | (HEVC) encoding only.                  |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| dev-idx            |    Integer  | 0 to 31       |    0                   | Device index. This is valid only in    |
|                    |             |               |                        | PCIe/ Data Center platforms.           |
|                    |             |               |                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| enable-pipeline    |  Boolean    |  true/false   | false                  | Enable buffer pipelining to improve    |
|                    |             |               |                        | performance in non zero-copy use cases |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| entropy-mode       |  Enum       |  0 : CAVLC    | 1 (CABAC)              | Entropy mode for encoding process      |
|                    |             |               |                        | (only in H264). This property is       |
|                    |             |  1 : CABAC    |                        | experimental                           |
|                    |             |               |                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| filler-data        |  Boolean    |  true/false   | false                  | Enable/Disable Filler Data NAL         |
|                    |             |               |                        | (only in H264). This property is       |
|                    |             |               |                        | experimental                           |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| gdr-mode           |  Enum       | 0 : disable   | 0 (disable)            | Entropy mode for encoding process      |
|                    |             |               |                        | (only in H264). This property is       |
|                    |             | 1 : vertical  |                        | experimental                           |
|                    |             |               |                        |                                        |
|                    |             | 2 : horizontal|                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| gop-length         |  Unsigned   | 0 - 100       | 120                    | Number of all frames in 1 GOP, Must be |
|                    |  Integer    |               |                        | in multiple of (b-frames+1), Distance  |
|                    |             |               |                        | between two consecutive I frames       |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| gop-mode           |  Enum       | 0: basic      | 0 (basic)              | Group Of Pictures mode.                |
|                    |             |               |                        | This property is experimental.         |
|                    |             | 1: pyramidal  |                        |                                        |
|                    |             |               |                        |                                        |
|                    |             | 2: low-delay-p|                        |                                        |
|                    |             |               |                        |                                        |
|                    |             | 3: low-delay-b|                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| initial-delay      |  Unsigned   | 0 -           | 1000                   | The initial removal delay as specified,|
|                    |  Integer    | 4294967295    |                        | in the HRD model in msec. Not used when|
|                    |             |               |                        | control-rate=disable. This property is |
|                    |             |               |                        | experimental.                          |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| in-mem-bank        | Unsigned    |  0 - 65535    | 0                      | VVAS input memory bank to allocate     |
|                    | Integer     |               |                        | memory.                                |
|                    |             |               |                        |                                        |
+------------------------+-------------+---------------+------------------------+------------------------------------+
| ip-delta           |  Integer    | -1 - 51       | -1                     | IP Delta                               |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| kernel-name        |    String   |    N/A        | encoder:encoder_1      | Kernel name and instance separated by  |
|                    |             |               |                        | colon.                                 |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| loop-filter-beta-  |  Integer    |  -6 - 6       | -1                     | loop filter beta offset                |
| offset             |             |               |                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| loop-filter-mode   |  Enum       | 0: enable     | 0 (enable)             | Enable or disable the deblocking       |
|                    |             |               |                        | filter. This property is experimental. |
|                    |             | 1: disable    |                        |                                        |
|                    |             |               |                        |                                        |
|                    |             | 2: low-delay-p|                        |                                        |
|                    |             |               |                        |                                        |
|                    |             | 3: disable-   |                        |                                        |
|                    |             |               |                        |                                        |
|                    |             | slice-boundary|                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| loop-filter-tc-    |  Integer    |  -6 - 6       | -1                     | loop filter tc offset                  |
| offset             |             |               |                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| max-bitrate        |  Unsigned   | 0 -           | 5000                   | Max bitrate in Kbps, only, used if     |
|                    |  Integer    | 35000000      |                        | control-rate=variable                  |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| max-qp             |  Unsigned   | 0 - 51        | 51                     | Maximum QP value allowed,for the rate  |
|                    |  Integer    |               |                        | control                                |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| min-qp             |  Unsigned   | 0 - 51        | 0                      | Minimum QP value allowed,for the rate  |
|                    |  Integer    |               |                        | control                                |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| num-cores          |  Unsigned   | 0 - 4         | 0                      | Number of Encoder Cores to be used for |
|                    |  Integer    |               |                        | current Stream. There are 4 Encoder    |
|                    |             |               |                        | cores. Value  0 => AUTO, VCU Encoder   |
|                    |             |               |                        | will automatically decide the number   |
|                    |             |               |                        | of cores for the current stream. Value |
|                    |             |               |                        | 1 to 4 => number of cores to be used   |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| num-slices         |  Unsigned   | 0 - 68        | 1                      | Number of slices produced for each     |
|                    |  Integer    |               |                        | frame. Each slice contains one or more |
|                    |             |               |                        | complete macroblock/CTU row(s). Slices |
|                    |             |               |                        | are distributed over the frame as      |
|                    |             |               |                        | possible. If slice-size is defined as  |
|                    |             |               |                        | well more slices may be produced to    |
|                    |             |               |                        | fit the slice-size requirement. In     |
|                    |             |               |                        | low-latency mode  H.264(AVC): 32,H.265 |
|                    |             |               |                        | (HEVC): 22 and In normal latency-mode  |
|                    |             |               |                        | H.264(AVC): picture_height/16,         |
|                    |             |               |                        | H.265(HEVC): minimum of                |
|                    |             |               |                        | picture_height/32                      |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| out-mem-bank       | Unsigned    |  0 - 65535    | 0                      | VVAS output memory bank to allocate    |
|                    | Integer     |               |                        | memory.                                |
|                    |             |               |                        |                                        |
+------------------------+-------------+---------------+------------------------+------------------------------------+
| pb-delta           |  Integer    |  -1 - 51      | -1                     | PB Delta                               |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| periodicity-idr    |  Unsigned   | 0 -           | 4294967295             | Periodicity of IDR frames              |
|                    |  Integer    | 4294967295    |                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| prefetch-buffer    |  Boolean    |  true/false   | true                   | Enable/Disable L2Cache buffer in       |
|                    |             |               |                        | encoding process. This property is     |
|                    |             |               |                        | experimental                           |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| qos                |  Boolean    |  true/false   | false                  | Handle Quality-of-Service events       |
|                    |             |               |                        | from downstream                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| qp-mode            |  Enum       | 0: uniform    | 1 (auto)               | QP control mode used by the VCU        |
|                    |             |               |                        | encoder                                |
|                    |             | 1: auto       |                        |                                        |
|                    |             |               |                        |                                        |
|                    |             | 2: roi        |                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| rc-mode            |  Boolean    |  true/false   | true                   | VCU Custom rate control mode           |
|                    |             |               |                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| scaling-list       |  Enum       | 0: flat       | 1 (default)            | Scaling list mode                      |
|                    |             |               |                        |                                        |
|                    |             | 1: default    |                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| slice-qp           |  Integer    |  -1 - 51      | -1                     | When RateCtrlMode = CONST_QP the       |
|                    |             |               |                        | specified QP is applied to all slices. |
|                    |             |               |                        | When RateCtrlMode = CBR the specified  |
|                    |             |               |                        | QP is used as initial QP               |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| slice-size         |  Unsigned   |  0 - 65535    | 0                      | Target slice size (in bytes) that the  |
|                    |  Integer    |               |                        | encoder uses to automatically split    |
|                    |             |               |                        | the bitstream into approximately       |
|                    |             |               |                        | equally-sized slices. This property is |
|                    |             |               |                        | experimental.                          |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| target-bitrate     |  Unsigned   |  0 -          | 5000                   | Target bitrate in Kbps                 |
|                    |  Integer    |  4294967295   |                        | (5000 Kbps = component default)        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| tune-metrics       |  Boolean    |  true/false   | false                  | Tunes Encoder's video quality          |
|                    |             |               |                        | for objective metrics                  |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| ultra-low-latency  |  Boolean    |  true/false   | false                  | Serializes encoding when b-frames=0    |
|                    |             |               |                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+
| xclbin-location    |    String   |    N/A        | null                   | The location of xclbin.                |
|                    |             |               |                        |                                        |
+--------------------+-------------+---------------+------------------------+----------------------------------------+

Example Pipelines
-----------------

`dev-idx` represents the zero based index of the device on which this decoder to be launched. Please note that 'dev-idx' parameter range can vary from 0 to N where N can be obtained with below command:

.. code-block:: shell

    N=xbutil  examine | grep xilinx_u30 | wc -l

1.  The following example pipeline encodes a raw data in NV12 data generated by 'videotestsrc' plugin to a h264 stream and displays the performance in frames/sec (fps).

.. code-block:: shell

    gst-launch-1.0 -v videotestsrc num-buffers=1000 ! queue \
    ! vvas_xvcuenc xclbin-location=<xclbin file path> dev-idx=3 target-bitrate=600 \
    ! h264parse ! fpsdisplaysink video-sink=fakesink text-overlay=false sync=false

2.  The following pipeline reads raw/uncompressed data, the properties of which should be known to user. These are provided in the command line such width(1920), height(height), format(NV12), framerate(30). The configurable properties are for encoder plugin (vvas_xvcuenc) such as b-frames (0), num-cores(4) and num-slices(4). When no parameter values are mentioned, default values are considered.

Please refer above table for other parameters and corresponding default/range values/ for encoder plugin.

.. code-block:: shell

    Note: BLOCKSIZE = width * height * 1.5  as only NV12 input format is specified.

    gst-launch-1.0 filesrc location=<input file> blocksize=$BLOCKSIZE ! \
    queue ! \
    rawvideoparse width=1920 height=1080 format=nv12 framerate=30 ! \
    vvas_xvcuenc enable-pipeline=0 dev-idx=0 b-frames=0 num-cores=4 num-slices=4 xclbin-location=<xclbin ! \
    h264parse ! \
    fpsdisplaysink video-sink="filesink location=<output file>" text-overlay=false sync=false

.. _vvas_xlookahead:

vvas_xlookahead
----------------

For video quality improvements, Xilinx have developped ``Lookahead`` IP. This IP performs analysis on several frames and provides few parameters for the encoder to improve the video quality. ``vvas_xlookahead`` plugin controles this IP. This is an optional plug-in in transcoding pipeline.
For implementation details, refer to `vvas_xlookahead source code <https://gitenterprise.xilinx.com/IPS-SSW/vvas/tree/master/vvas-gst-plugins/sys/lookahead>`_

Input and Output
================

This plugin takes raw video data as input and generates 'QP' values which are used during encoding.

Control Parameters and Plug-in Properties
=========================================

The following table lists the GStreamer plug-in properties supported by the vvas_xlookahead plug-in.

Table 3: vvas_xlookahead Plug-in Properties

+--------------------+-------------+---------------+------------------------+------------------------+
|                    |             |               |                        |                        |
|  **Property Name** |   **Type**  | **Range**     | **Default**            | **Description**        |
|                    |             |               |                        |                        |
+====================+=============+===============+========================+========================+
| b-frames           |  Integer    |  -1 -         | -1                     | Number of B-frames     |
|                    |             |  4294967295   |                        | between two consecutive|
|                    |             |  4294967295   |                        | P-frames. Internally   |
|                    |             |               |                        | set to 0 in case of    |
|                    |             |               |                        | ultra-low-latency mode,|
|                    |             |               |                        | 2 otherwise if         |
|                    |             |               |                        | not-configured or      |
|                    |             |               |                        | configured with -1.    |
+--------------------+-------------+---------------+------------------------+------------------------+
| codec-type         |   Enum      |  -1 : none    | -1                     | Codec type -           |
|                    |             |  0  : H264    |                        | H264/H265              |
|                    |             |  1  : H265    |                        |                        |
+--------------------+-------------+---------------+------------------------+------------------------+
|    dev-idx         |    Integer  | 0 to 31       |    0                   | Device index           |
|                    |             |               |                        | This is valid          |
|                    |             |               |                        | only in PCIe/          |
|                    |             |               |                        | Data Center            |
|                    |             |               |                        | platforms.             |
+--------------------+-------------+---------------+------------------------+------------------------+
| dynamic-gop        |  Boolean    |  true/false   | false                  | Automatically          |
|                    |             |               |                        | change b-frame         |
|                    |             |               |                        | structure based        |
|                    |             |               |                        | on motion vectors      |
+--------------------+-------------+---------------+------------------------+------------------------+
| enable-pipeline    |    Boolean  |  true/false   |    false               | Enable buffer          |
|                    |             |               |                        | pipelining to          |
|                    |             |               |                        | improve                |
|                    |             |               |                        | performance            |
|                    |             |               |                        | in non zero-copy       |
|                    |             |               |                        | use cases              |
+--------------------+-------------+---------------+------------------------+------------------------+
| in-mem-bank        | Unsigned    |  0 - 65535    | 0                      | VVAS input memory      |
|                    | Integer     |               |                        | bank to allocate       |
|                    |             |               |                        | memory                 |
+--------------------+-------------+---------------+------------------------+------------------------+
|                    |    String   |    N/A        | v_mot_est:{v_mot_est_1}| Kernel name            |
| kernel-name        |             |               |                        | and                    |
|                    |             |               |                        | instance               |
|                    |             |               |                        | separated              |
|                    |             |               |                        | by a colon.            |
+--------------------+-------------+---------------+------------------------+------------------------+
| lookahead-depth    |  Unsigned   | 1 - 20        |    8                   | Lookahead depth        |
|                    |  Integer    |               |                        |                        |
|                    |             |               |                        |                        |
+--------------------+-------------+---------------+------------------------+------------------------+
|  reservation-id    |  Unsigned   | 0-            |    0                   | Resource Pool          |
|                    |  Integer64  | 184467440     |                        | Reservation id         |
|                    |             | 73709551615   |                        |                        |
|                    |             | 12=>12 taps   |                        |                        |
+--------------------+-------------+---------------+------------------------+------------------------+
|  spatial-aq        |  Boolean    | true/false    |   true                 | Enable/Disable         |
|                    |             |               |                        | Spatial AQ             |
|                    |             |               |                        | activity               |
+--------------------+-------------+---------------+------------------------+------------------------+
| spatial-aq-gain    | Unsigned    |  0 - 100      |   50                   | Percentage of          |
|                    | Integer     |               |                        | Spatial AQ gain,       |
|                    |             |               |                        | applied when           |
|                    |             |               |                        | spatial-aq             |
|                    |             |               |                        | is true                |
+--------------------+-------------+---------------+------------------------+------------------------+
|  temporal-aq       |  Boolean    | true/false    |   true                 | Enable/Disable         |
|                    |             |               |                        | Temporal AQ            |
|                    |             |               |                        | linear                 |
+--------------------+-------------+---------------+------------------------+------------------------+
|                    |    String   |    N/A        | null                   | The                    |
| xclbin-location    |             |               |                        | location of            |
|                    |             |               |                        | xclbin.                |
+--------------------+-------------+---------------+------------------------+------------------------+

Example Pipelines
-----------------

Please note that 'dev-idx' parameter range can vary from 0 to N where N can be obtained with below command:

.. code-block:: shell

    N=xbutil  examine | grep xilinx_u30 | wc -l

The following pipeline transcodes a stream with improved quality. User can tweak the lookahead parameters - "spatial-aq,
temporal-aq, lookahead-depth" to have varied degree of quality. Please refer above Table-3 for more information.

.. code-block:: shell

    gst-launch-1.0 -v filesrc location=<input h264 file> \
    ! queue \
    ! h264parse \
    ! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=3 \
    ! vvas_xabrscaler xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=3 ppc=4 scale-mode=0 name=sc_00 sc_00.src_0 \
    ! queue \
    ! video/x-raw, width=848, height=480, format=NV12 \
    ! queue \
    ! vvas_xlookahead xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin codec-type=0 spatial-aq=1 temporal-aq=1 lookahead-depth=8 dev-idx=3 \
    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=enc_720p60_dev0_0 dev-idx=3 target-bitrate=600 \
    ! h264parse \
    ! fpsdisplaysink name=sink_xcode_scale_720p60_dev0_0 video-sink=fakesink text-overlay=false sync=false

vvas_xabrscaler 
----------------

In several usecases, an input frame needs to be resized/scaled to different resolutions, to be encoded at different bitrates. VVAS has provided hardware accelerated IP, multiscaler, that can resize the input frame in to several different resolutions and formats. `vvas_xabrscaler` plugin controls this IP. This takes one raw input stream as input and produces one or more scaled/resized raw streams.
This is a common plugin for both DC and Embedded platforms. So,the details are captured in common plugins section.
For more details on using this plugin, please refer to :ref:`vvas_xabrscaler`

For more information, contact `vvas_discuss@xilinx.com`.
