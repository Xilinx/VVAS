.. _vvas_xfunnel:

vvas_xfunnel
=============

The ``vvas_xfunnel`` VVAS Gstreamer plug-in serializes data on its sink pads by iterating through all of them in a round-robin fashion. If a sink pad has a buffer available, it sends the data on the source pad. If a sink pad does not have data available, the plug-in will wait for a user-configurable amount of time before moving on to the next sink pad.

When a new sink pad is added or removed from the ``vvas_xfunnel`` plug-in, it sends custom events to notify the ``vvas_xdefunnel`` plug-in to create or destroy source pads. Each buffer includes metadata that enables the ``vvas_xdefunnel`` plug-in to determine which source pad the buffer should be sent to.

The vvas_xfunnel plug-in can support a maximum of 256 sink pads connected to it.

``vvas_xfunnel`` supports all caps.

Refer ``vvas_xdefunnel`` plug-in which de-serializes these serialized data and pushes them to the output pads.

For ``vvas_xfunnel`` implementation details, refer to `vvas_xfunnel source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/gst/funnel>`_

This plug-in supports:

* Multiple sink pads, single source pad.

* Sink pads are request pads and they can be added or removed dynamically.

* Serialization of parallel data using round robin algorithm.

.. important:: Caps of all the sink pads must be same, this is to avoid caps renegotiation always when sending buffer from the sink pad which has different caps.
.. important:: There are several custom events sent from this plugin for ``vvas_xdefunnel`` plug-in, intermediate plug-ins must pass these events.

Control Parameters and Plug-in Properties
------------------------------------------------

The following table lists the GStreamer plug-in properties supported by the vvas_xfunnel plug-in.

Table 10: vvas_xfunnel Plug-in Properties

+--------------------+-------------+---------------+--------------+----------------------+
|                    |             |               |              |                      |
|  **Property Name** |   **Type**  |  **Range**    | **Default**  |   **Description**    |
|                    |             |               |              |                      |
+====================+=============+===============+==============+======================+
|    queue-size      |   unsigned  |    1 - 100    |     2        | Buffer queue size    |
|                    |   integer   |               |              | for each sink pad    |
+--------------------+-------------+---------------+--------------+----------------------+
| sink-wait-timeout  |   unsigned  |    1 - 100    |     33       | Time to wait before  |
|                    |   integer   |               |              | switching to next    |
|                    |             |               |              | sink pad in mili     |
|                    |             |               |              | seconds, default     |
|                    |             |               |              | will be calculated   |
|                    |             |               |              | as 1/FPS             |
+--------------------+-------------+---------------+--------------+----------------------+

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

