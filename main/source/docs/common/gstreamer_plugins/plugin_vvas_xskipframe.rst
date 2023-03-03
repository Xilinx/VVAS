.. _vvas_xskipframe:

vvas_xskipframe
==================

vvas_xskipframe plugin is used in video analytics pipelines in conjunction with :ref:`vvas_xreorderframe` plugin to control inference rate.
It achieves this by using the user configured infer-interval property and determines which frames to be served for inference and which frames to be skipped for inference.
For example, if the infer-interval is 1, all frames are sent for inference,
if infer-interval is 2, then every alternate frame is sent for inference.

This element has one sink pad and two source pads. It is multi-stream aware and its sink pad can connect with :ref:`vvas_xfunnel` element to accept frames from multiple streams.
One source pad is connected to :ref:`vvas_xinfer` element to send the frames that are marked for inference, another source pad is connected to :ref:`vvas_xreorderframe` 
to send the frames marked skipped.

vvas_xskipframe plugin adds frame id to the GstVvasSrcIDMeta metadata so that :ref:`vvas_xreorderframe` plugin can identify the order of the frames.


Input and Output
--------------------

vvas_xskipframe plugin is format agnostic and can accept any format on input and output pads.


Control Parameters and Plugin Properties
---------------------------------------------

The following table lists the GStreamer plugin properties supported by the vvas_xskipframe plugin.

Table 9: vvas_xskipframe plugin properties


+-----------------------+-------------+---------------+-----------------------+-----------------------+
|                       |             |               |                       |                       |
|  **Property Name**    |   **Type**  | **Range**     |    **Default**        |    **Description**    |
|                       |             |               |                       |                       |
+=======================+=============+===============+=======================+=======================+
|    infer-interval     |   UINT      |   1 to 7      |          1            | Every n-th frame will |
|                       |             |               |                       | be pushed to inference|
|                       |             |               |                       | source pad. Here, 'n' |
|                       |             |               |                       | represents inference  |
|                       |             |               |                       | interval.             |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
