.. _vvas_xskipframe:

vvas_xskipframe
==================

vvas_xskipframe plugin is used in video analytics pipelines in conjunction with :ref:`vvas_xreorderframe` plugin to control inference rate.
It achieves this by using the user configured infer-interval property and determines which frames to be sent for inference and which frames to be skipped from inference.
For example, if the infer-interval is 1, all frames are sent for inference,
if infer-interval is 2, then every alternate frame is sent for inference.

This element has one sink pad and two source pads. It is multi-stream aware and its sink pad can connect with :ref:`vvas_xfunnel` element to accept frames from multiple streams.
One source pad is connected to :ref:`vvas_xinfer` element to send the frames that are marked for inference, another source pad is connected to :ref:`vvas_xreorderframe` 
to send the frames marked skipped.

:ref:`vvas_xskipframe` plug-in adds frame id to the ``GstVvasSrcIDMeta`` metadata so that :ref:`vvas_xreorderframe` plug-in can identify the order of the frames.


Input and Output
--------------------

``vvas_xskipframe`` plug-in is format agnostic and can accept any format on input and output pads.


Control Parameters and Plugin Properties
---------------------------------------------

The following table lists the GStreamer plug-in properties supported by the ``vvas_xskipframe`` plugin.

Table 9: vvas_xskipframe plug-in properties


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

