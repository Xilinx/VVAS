.. _vvas_xoverlay:

vvas_xoverlay
=============

The Overlay plug-in is utilized to draw shapes like bounding boxes, lines, arrows, circles, and polygons on a given frame or image. Users can display a clock on any part of the frame by using the display-clock property. This plug-in employs the ``vvas_overlay`` library to produce different shapes, text, and clocks.

The overlay metadata structure, ``GstVvasOverlayMeta``, must be used to provide the information about the objects like, bounding boxes, lines, and arrows to be drawn. This metadata must be attached to the input buffer. For more details about this overlay metadata structure, please refer :ref:`vvas_overlay_metadata` section.

To convert metadata generated from an upstream plug-in such as ``vvas_xinfer``, ``vvas_xoptflow``, or segmentation into ``GstVvasOverlayMeta``, an intermediate plug-in called :ref:`vvas_xmetaconvert` is provided and this can be used before the ``vvas_xoverlay`` plug-in in gstreamer pipeline.

For implementation details, refer to `vvas_xoverlay source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/sys/overlay>`_

.. figure:: ../../images/vvas_overlay_blockdiagram.png
   :align: center
   :scale: 80


Supported features
-------------------

+--------------+-----------------------------------------------------------------------------------------------+
|              |                                                                                               |
|  **Feature** |                                   **Description**                                             |
|              |                                                                                               |
+==============+===============================================================================================+
|   boxes      | Drawing bounding boxes with and without background color                                      |
|              |                                                                                               |
+--------------+-----------------------------------------------------------------------------------------------+
|   text       | Overlaying of text with and without background color. Supports text fonts available in opencv |
|              |                                                                                               |
+--------------+-----------------------------------------------------------------------------------------------+
|   lines      | For drawing lines with different thickness and color                                          |
|              |                                                                                               |
+--------------+-----------------------------------------------------------------------------------------------+
|   arrows     | Drawing arrows on either side of line or both the sides                                       |
|              |                                                                                               |
+--------------+-----------------------------------------------------------------------------------------------+
|   circle     | For drawing circles of different radius and thickness without fill                            |
|              |                                                                                               |
+--------------+-----------------------------------------------------------------------------------------------+
|   polygons   | For drawing closed polygons without fill                                                      |
|              |                                                                                               |
+--------------+-----------------------------------------------------------------------------------------------+


Input and Output
--------------------

Supported input buffer formats are RGB/BGR, NV12 and Grayscale. Required metadata in gstvvasoverlaymeta type for drawing.


Control Parameters and Plug-in Properties
---------------------------------------------

The following table lists the GStreamer plug-in properties supported by the vvas_xoverlay plug-in.

Table 7: vvas_xoverlay Plug-in Properties


+--------------------+--------------+---------------+-----------------------+----------------------+
|                    |              |               |                       |                      |
|  **Property Name** |   **Type**   | **Range**     |    **Default**        |    **Description**   |
|                    |              |               |                       |                      |
+====================+==============+===============+=======================+======================+
| xclbin-location    |   String     |      NA       | ./binary_container_1  | location of xcllbin  |
|                    |              |               | .xclbin               |                      |
+--------------------+--------------+---------------+-----------------------+----------------------+
| dev-idx            |   Integer    |    0 to 31    |           0           | device index         |
+--------------------+--------------+---------------+-----------------------+----------------------+
| in-mem-bank        | Unsigned int |  0 - 65535    |           0           | VVAS input memory    |
|                    |              |               |                       | bank to allocate     |
|                    |              |               |                       | memory               |
+--------------------+--------------+---------------+-----------------------+----------------------+
| display-clock      |   Boolean    |    0 or 1     |           0           | flag for indicating  |
|                    |              |               |                       | displaying clock     |
+--------------------+--------------+---------------+-----------------------+----------------------+
| clock-fontname     |   Integer    |    0 or 7     |           0           | font number for clock|
|                    |              |               |                       | based on opencv      |
+--------------------+--------------+---------------+-----------------------+----------------------+
| clock-fontscale    |   float      |  0.1 to 1.0   |           0.5         | font scale of        |
|                    |              |               |                       | display clock        |
+--------------------+--------------+---------------+-----------------------+----------------------+
| clock-fontcolor    |   Integer    | 0 to          |        0xff00         | RGB color of display |
|                    |              | 4,294,967,295 |                       | clock as 0xRGB       |
+--------------------+--------------+---------------+-----------------------+----------------------+
| clock-xoffset      |   Integer    | 0 to          |        100            | x starting position  |
|                    |              | frame width   |                       |                      |
+--------------------+--------------+---------------+-----------------------+----------------------+
| clock-yoffset      |   Integer    | 0 to          |        50             | y starting position  |
|                    |              | frame height  |                       |                      |
+--------------------+--------------+---------------+-----------------------+----------------------+


Example Pipelines
---------------------

The following example demonstrates use of ``vvas_xoverlay`` plug-in with ``vvas_xinfer`` and ``vvas_xmetaconvert`` plug-ins for drawing bounding boxes. ``vvas_xinfer`` plug-in produces inference result and stores in ``GstInferenceMeta`` structure. This metadata is parsed and translated into ``GstVvasOverlayMeta`` structure that is understood by ``vvas_xmetaconvert`` plug-in to draw the bounding box. This translation of meta data is done by ``vvas_xmetaconvert`` plug-in.

.. code-block::

     gst-launch-1.0 filesrc location="<PATH>/001.bgr" blocksize=150528 numbuffers=1
     ! videoparse width=224 height=224 framerate=30/1 format=16
     ! vvas_xfilter name="kernel1" kernels-config="<PATH>/kernel_resnet50.json"
     ! vvas_xmetaconvert config-location="<PATH>/metaconvert.json"
     ! vvas_xoverlay ! filesink location=output.bgr

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

