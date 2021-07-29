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

######################################################################
VVAS GStreamer Plug-ins for Embedded Platforms
######################################################################



The following section describes GStreamer plugins for Embedded platforms. The plug-ins source code is available in the ivas-gst-plugins repository/folder of the VVAS sources tree. The following table lists the GStreamer plug-ins.

Table 1: GStreamer Plug-ins

+----------------------------------+----------------------------------+
|    **Plug-in Name**              |    **Functionality**             |
+==================================+==================================+
|    omxh264dnc/omxh265dec         | GStreamer plug-in to perform     |
|                                  | hardware accelerated h264/h265   |
|                                  | video decoding.                  |
+----------------------------------+----------------------------------+
|    omxh264dnc/omxh265dec         | GStreamer plug-in to perform     |
|                                  | hardware accelerated h264/h265   |
|                                  | video encoding.                  |
+----------------------------------+----------------------------------+
|    ivas_xroigen                  | Plug-in to generate region of    |
|                                  | interest metadata.               |
+----------------------------------+----------------------------------+

=========================================
OMX Encoder/Decoder Plug-in
=========================================

“omxh264decoder/omxh265decoder” are GStreamer Plugins for hardware accelerated H264/H265 Video decoding using VCU IP. Similarly “omxh264encoder/omxh265encoder” are GStreamer Plugins for hardware accelerated H264/H265 Video encoding using VCU IP. 
For more details about these plugins, refer to the `VCU PG252 <https://www.xilinx.com/support/documentation/ip_documentation/vcu/v1_2/pg252-vcu.pdf>`_  Chapter 23: Encoder and Decoder Software Features. 

.. _roi-plugin:

===========================
Region of Interest Plug-in
===========================

The region of interest (ROI) GStreamer ivas_xroigen plug-in generates GstVideoRegionOfInterestMeta metadata information, which is expected by GStreamer OMX encoder plug-ins to encode raw frames with the desired quality parameters (QP) values/ level for specified ROIs. The ivas_xroigen plug-in prepares the GstVideoRegionOfInterestMeta metadata by parsing an IVAS supported list of metadata objects (GstInferenceMeta).

.. tip::  *This plug-in is only useful with embedded platforms because its main job is to generate metadata required for the GStreamer OMX encoder plug-in that exists on embedded platforms.*

GStreamer ROI metadata information is attached with each outgoing buffer based on the following plug-in properties.

------------------
Input and Output
------------------

Accepts buffers with any of the GStreamer supported video formats on input and output GstPads.

-------------------------------------------
Control Parameters and Plug-in Properties
-------------------------------------------

The following table lists the GObject properties exposed by ivas_xroigen plug-in.

Table 4: ivas_xroigen Plug-in Properties

+--------------------------+----------------+-------------+-------------+------------------------+
|  **Property Name**       |   **Type**     |  **Range**  | **Default** | **Description**        |
+==========================+================+=============+=============+========================+
| class-filters            | GstValueArray  |    None     |    None     |   Array of desired     |
|                          |                |             |             |   inference classes.   |
|                          |                |             |             |   ivas_xroigen         |
|                          |                |             |             |   prepares ROI metadata|
|                          |                |             |             |   only for             |
|                          |                |             |             |   class-filters        |
|                          |                |             |             |   array of strings     |
|                          |                |             |             |   out of classes       |
|                          |                |             |             |   present in IVAS      |
|                          |                |             |             |   supported metadata   |
|                          |                |             |             |   objects.             |
|                          |                |             |             |                        |
|                          |                |             |             |   Default behavior:    |
|                          |                |             |             |   Plug-in allows all   |
|                          |                |             |             |   classes when this    |
|                          |                |             |             |   property is not set. |
|                          |                |             |             |   e.g.,                |
|                          |                |             |             |   class-filters        |
|                          |                |             |             |   =<"person", "car">   |
+--------------------------+----------------+-------------+-------------+------------------------+
| insert-roi-sei           | Boolean        | true, false | false       | When 'true', it sends  |
|                          |                |             |             | custom events to OMX   |
|                          |                |             |             | encoder to insert ROI  |
|                          |                |             |             | information as SEI     |
|                          |                |             |             | packet.                |
+--------------------------+----------------+-------------+-------------+------------------------+
| resolution-range         | GstValueArray  |    None     |    None     | Array of               |
|                          |                |             |             | integers               |
|                          |                |             |             | that                   |
|                          |                |             |             | represent              |
|                          |                |             |             | the minimum            |
|                          |                |             |             | and maximum            |
|                          |                |             |             | range of               |
|                          |                |             |             | ROI                    |
|                          |                |             |             | resolutions.           |
|                          |                |             |             | Plug-in                |
|                          |                |             |             | uses this              |
|                          |                |             |             | resolution             |
|                          |                |             |             | range to               |
|                          |                |             |             | filter out             |
|                          |                |             |             | ROIs.                  |
|                          |                |             |             |                        |
|                          |                |             |             | Set this               |
|                          |                |             |             | property:              |
|                          |                |             |             | <min-width,            |
|                          |                |             |             | min-                   |
|                          |                |             |             | height,                |
|                          |                |             |             | max-width,             |
|                          |                |             |             | max-height>)           |
|                          |                |             |             | Default                |
|                          |                |             |             | behavior:              |
|                          |                |             |             | Plug-in                |
|                          |                |             |             | allows all             |
|                          |                |             |             | ROIs. e.g.,            |
|                          |                |             |             | resoluti               |
|                          |                |             |             | on-range=<0            |
|                          |                |             |             | ,0,320,240>            |
+--------------------------+----------------+-------------+-------------+------------------------+
| roi-max-num              |    Unsigned    |  0 to       |             | Maximum                |
|                          |    integer     |             |  4294967295 | number of              |
|                          |                |  4294967295 |             | ROI                    |
|                          |                |             |             | metadata to            |
|                          |                |             |             | be attached            |
|                          |                |             |             | per buffer             |
+--------------------------+----------------+-------------+-------------+------------------------+
| roi-qp-delta             |    Integer     |    –32 to   |    0        | QP delta to            |
|                          |                |    31       |             | be used for            |
|                          |                |             |             | ROI in                 |
|                          |                |             |             | encoder                |
+--------------------------+----------------+-------------+-------------+------------------------+
| roi-qp-level             |    Enum        |    0, 1, 2, |    0        | QP level to            |
|                          |                |    3, 4     |             | be used for            |
|                          |                |             |             | ROI in                 |
|                          |                |             |             | encoder 0:             |
|                          |                |             |             | high (delta            |
|                          |                |             |             | QP of –5)              |
|                          |                |             |             |                        |
|                          |                |             |             | 1: medium              |
|                          |                |             |             | (delta QP              |
|                          |                |             |             | of 0)                  |
|                          |                |             |             |                        |
|                          |                |             |             | 2: low                 |
|                          |                |             |             | (delta QP              |
|                          |                |             |             | of +5)                 |
|                          |                |             |             |                        |
|                          |                |             |             | 3:                     |
|                          |                |             |             | don't-care             |
|                          |                |             |             | (maximum               | 
|                          |                |             |             | delta QP               |
|                          |                |             |             | value)                 |
|                          |                |             |             |                        |
|                          |                |             |             | 4: intra               |
|                          |                |             |             | (region all            |
|                          |                |             |             | LCU encoded            |
|                          |                |             |             | with intra             |
|                          |                |             |             | prediction             |
|                          |                |             |             | mode)                  |
+--------------------------+----------------+-------------+-------------+------------------------+
| roi-type                 |    Enum        |    0, 1, 2  |    0        | Type to be             |
|                          |                |             |             | used to                |
|                          |                |             |             | generate               |
|                          |                |             |             | ROI                    |
|                          |                |             |             | metadata               |
|                          |                |             |             |                        |
|                          |                |             |             | 0: default             |
|                          |                |             |             | (ROI                   |
|                          |                |             |             | without                |
|                          |                |             |             | encoder QP             |
|                          |                |             |             | i                      |
|                          |                |             |             | nformation)            |
|                          |                |             |             |                        |
|                          |                |             |             | 1: qp_level            |
|                          |                |             |             | (ROI                   | 
|                          |                |             |             | encoder QP             |
|                          |                |             |             | level) 2:              |
|                          |                |             |             | qp_delta               |
|                          |                |             |             | (ROI                   |
|                          |                |             |             | encoder QP             |
|                          |                |             |             | delta)                 |
+--------------------------+----------------+-------------+-------------+------------------------+


~~~~~~~~~~~~~~~~~
Example Pipelines
~~~~~~~~~~~~~~~~~                 

The following pipeline takes input video in an MP4 container from the filesrc plug-in. The ``qtdemux`` extracts the ``H.264`` elementary stream from the MP4 container and pass it to the ``omxh264dec`` plug-in for decoding. The output of the decoder goes to the ``ivas_xmultisrc`` plug-in for resizing to a resolution of 640 x 360 and color-space conversion to ``BGR`` format. If your design has other kernels, like ``multiscaler``, for re-size and colorspace conversin, then you may use it along with ``ivas_xabrscaler`` plug-in. The output from ivas_xmultisrc goes to the ivas_xfilter plug-in for object detection using the densebox model. The inference operation generates the metadata and bounding box, for each detected object. The ivas_xroigen plug-in creates the ROI metadata from the incoming bounding box information and passes this metadata to omxh264enc for ROI based encoding. This is an example pipeline to demonstrate ROI feature. This pipeline uses omxh264enc hence your design must have ``VCU`` encoder.

.. code-block::

      gst-launch-1.0 -v filesrc location=<filename>.mp4 \
      ! qtdemux \
      ! h264parse \
      ! omxh264dec internal-entropy-buffers=3 \
      ! queue \
      ! ivas_xmultisrc kconfig="/home/root/jsons/kernel_resize_bgr.json"
      name=m2m_0 \
      ! video/x-raw, width=640, height=360, format=BGR \
      ! queue \
      ! ivas_xfilter kernels-config="/home/root/jsons/
      kernel_densebox_640_360.json" \
      ! queue \
      ! ivas_xroigen roi-type=1 roi-qp-delta=-10 roi-max-num=10 resolution-
      range="<0,0,500,300>" \
      ! queue \
      ! omxh264enc prefetch-buffer=true qp-mode=1
      ! queue
      ! fakesink
