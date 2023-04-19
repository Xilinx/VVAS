.. _vvas_xreorderframe:

vvas_xreorderframe
==================

The ``vvas_xreorderframe`` plug-in is utilized in video analytics pipelines to perform inference in conjunction with :ref:`vvas_xskipframe` plug-in, which regulates the inference rate. This plug-in comprises two sink pads and one source pad. One of the sink pads is linked with the :ref:`vvas_xskipframe` plug-in, while the other sink pad is connected to the :ref:`vvas_xinfer` plug-in. It has the ability to receive frames in arbitrary order from both ``vvas_xinfer`` and ``vvas_xskipframe`` plug-ins, and it rearranges them into the correct order of presentation before sending them downstream. Additionally, ``vvas_xreorderframe`` is capable of handling frames/images from multiple sources, and it employs the ``GstVvasSrcIDMeta`` metadata attached to GstBuffer to obtain the stream ID and frame ID and arrange the frames of various sources in the appropriate order.

Input and Output
--------------------

vvas_xreorderframe plugin is format agnostic and can accept any format on input and output pads.

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

