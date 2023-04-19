.. _vvas_xdefunnel:

vvas_xdefunnel
================

The Gstreamer plug-in, ``vvas_xdefunnel``, is responsible for de-serializing data received from the ``vvas_xfunnel`` plug-in. This plug-in dynamically creates and removes source pads based on events received from the ``vvas_xfunnel`` plug-in.

For ``vvas_xdefunnel`` implementation details, refer to `vvas_xdefunnel source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/gst/defunnel>`_

This plug-in supports:

* Single sink pad, multiple source pads.

* Source pads are sometimes pad.

* Source pads will be added and removed dynamically.

``vvas_xdefunnel`` supports all caps.

Control Parameters and Plug-in Signals
------------------------------------------------

The following table lists the GStreamer plug-in signals supported by the vvas_xdefunnel plug-in.

Table 11: vvas_xdefunnel Plug-in signals

+--------------------+--------------------------+------------------------------------------+
|                    |                          |                                          |
|  **Signal Name**   |       **Description**    |                 **callback**             |
|                    |                          |                                          |
+====================+==========================+==========================================+
|     pad-added      | This signal will be      | void user_function (GstElement* object,  |
|                    | emitted when new source  | GstPad* arg0, gpointer user_data);       |
|                    | pad is created.          |                                          |
+--------------------+--------------------------+------------------------------------------+
|    pad-removed     | This signal will be      | void user_function (GstElement* object,  |
|                    | emitted when a source    | GstPad* arg0, gpointer user_data);       |
|                    | pad is removed.          |                                          |
+--------------------+--------------------------+------------------------------------------+

The following table lists the GStreamer plug-in properties supported by the vvas_xdefunnel plug-in.

Table 12: vvas_xdefunnel Plug-in Properties

+--------------------+--------------+--------------+-----------------------------+
|                    |              |              |                             |
|  **Property Name** |   **Type**   | **Default**  |   **Description**           |
|                    |              |              |                             |
+====================+==============+==============+=============================+
|    active-pad      | GST_TYPE_PAD |   Read Only  | Source pad to which current |
|                    |              |   property   | buffer is being pushed to   |
+--------------------+--------------+--------------+-----------------------------+

Example Pipelines
-------------------------

.. code-block::

	gst-launch-1.0 -v \
	videotestsrc num-buffers=50 pattern=0  ! video/x-raw,format=NV12,width=640,height=480 ! funnel.sink_0 \
	videotestsrc num-buffers=80 pattern=19 ! video/x-raw,format=NV12,width=640,height=480 ! funnel.sink_1 \
	vvas_xfunnel name=funnel ! identity ! vvas_xdefunnel name=defunnel \
	defunnel.src_0 ! queue ! filesink location=sink_0.yuv \
	defunnel.src_1 ! queue ! filesink location=sink_1.yuv

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

