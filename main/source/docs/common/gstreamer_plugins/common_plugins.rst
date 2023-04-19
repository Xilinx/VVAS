.. _gstreamer_plugins:

###################
GStreamer Plug-ins
###################

The VVAS framework is built on top of GStreamer, and this section provides an overview of the VVAS GStreamer plug-ins, including their input, output, and control parameters. The source code for these plug-ins can be found in the ``vvas-gst-plugins`` directory of the VVAS source tree. There are two types of VVAS GStreamer plug-ins: custom plug-ins and infrastructure plug-ins.

Infrastructure plug-ins are designed to simplify the integration of kernels into GStreamer-based applications, allowing developers to incorporate their kernel without needing to have a deep understanding of the GStreamer framework. These plug-ins handle basic features/requirements of GStreamer plug-ins such as memory management, kernel configuration, caps negotiation, and other similar functions. The same infrastructure plug-ins can be used to integrate different kernels to achieve various functionalities.

On the other hand, custom plug-ins are developed to implement specific functionalities such as encoding, decoding, overlay, and so on, that cannot be efficiently implemented using infrastructure plug-ins.

This part of the document discusses the plug-ins that are commonly used for both Edge (Embedded) and cloud (PCI based) solutions. However, there are some plug-ins that are unique to Edge/Embedded platforms and are explained in detail in the section titled :doc:`Plugins for Embedded platforms <../../Embedded/embedded-plugins>`. Below is a list of the VVAS GStreamer plug-ins.

.. toctree::
   :maxdepth: 1

   Custom Plugins <./custom_plugins>

.. toctree::
   :maxdepth: 1

   Infrastructure Plugins <./infra_plugins>

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

