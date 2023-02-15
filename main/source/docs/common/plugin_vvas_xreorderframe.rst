vvas_xreorderframe
==================

vvas_xreorderframe plugin is used in video analytics pipelines for inference in conjunction with :ref:`vvas_xskipframe` plugin to control inference rate.
This element contains two sink pads and one source pad. One sink pad connects with :ref:`vvas_xskipframe` element and another sink pad connects with :ref:`vvas_xinfer` element. 
It can receive the frames from both vvas_xinfer and vvas_xskipframe plugins in random order and it rearranges the frames in correct order of presentation before pushing frames downstream.
vvas_xreorderframe is multi-stream aware and uses the GstVvasSrcIDMeta metadata attached to GstBuffer to read the stream id and frame id and arranges the frames of different sources in right order.

Input and Output
--------------------

vvas_xreorderframe plugin is format agnostic and can accept any format on input and output pads.
