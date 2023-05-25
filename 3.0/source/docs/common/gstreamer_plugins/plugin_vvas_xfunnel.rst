.. _vvas_xfunnel:

vvas_xfunnel
=============

vvas_xfunnel gstreamer plug-in serializes data on its sink pads, it iterates all the sink pads in round robin order, if sink pad has buffer available; it sends it on the source pad, else the plug-in will wait for preset (user configurable) time, by that time also if data is not available the plug-in will skip that sink pad and it will probe next sink pad.

Whenever new sink pad is added to or removed from this plug-in, it sends custom events to notify ``vvas_xdefunnel`` to create or destroy source pads.
Metadata on each buffer is attached to enable ``vvas_xdefunnel`` plug-in to decide the source pad to which that buffer has to be sent.

At max ``vvas_xfunnel`` can have 256 number of sink pads connected to it.

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
