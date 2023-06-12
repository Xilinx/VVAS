.. _vvas_xoverlay:

vvas_xoverlay
=============

Overlay plug-in is used for drawing bounding boxes, text, lines, arrows, circles, and polygons on frames. Using display-clock property user can display clock on any part of the frame.  This plug-in internally uses ``vvas_xoverlay`` library for drawing different shapes, text and clock

For drawing objects like bounding box, lines, arrows etc., the information about these objects must be provided through overlay metadata structure attached to the buffer. For more information about this overlay meta data structure, refer to :ref:`vvas_overlay_metadata`. 

For converting metadata generated from upstream plug-in like infer, opticalflow, segmentation etc., to gstvvasoverlaymeta an intermediate plug-in, :ref:`vvas_xmetaconvert` is to be used before ``vvas_xoverlay`` plug-in.

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
|                    |              |               |                       |                      |
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

The following example demonstrates use of ``vvas_xoverlay`` plug-in with ``vvas_xinfer`` and ``vvas_xmetaconvert`` plug-ins for drawing bounding boxes. ``vvas_xinfer`` plug-in produces inference result and stores in VVAS_GstInference data structure. This metadata is parsed and translated into a different metadata structure that understood by ``vvas_xmetaconvert`` plug-in to draw the bounding box. This translation of meta data is done by ``vvas_xmetaconvert`` plug-in.

.. code-block::

     gst-launch-1.0 filesrc location="<PATH>/001.bgr" blocksize=150528 numbuffers=1
     ! videoparse width=224 height=224 framerate=30/1 format=16
     ! vvas_xfilter name="kernel1" kernels-config="<PATH>/kernel_resnet50.json"
     ! vvas_xmetaconvert config-location="<PATH>/metaconvert.json"
     ! vvas_xoverlay ! filesink location=output.bgr
