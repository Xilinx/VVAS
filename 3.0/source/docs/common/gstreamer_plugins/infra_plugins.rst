.. _infra_plugins_label:

############################################################
Infrastructure Plug-ins and Acceleration Software Libraries
############################################################

GStreamer Infrastructure plug-ins in VVAS serve as generic plugins that interface with an acceleration kernel through a set of APIs exposed by the corresponding acceleration software library. They abstract the core functionality of the GStreamer framework, such as buffer management and caps negotiation.

Acceleration software libraries, on the other hand, control the acceleration kernel by handling tasks such as register programming and implementing necessary core logic to execute specific functions. These libraries expose a simplified interface, which is utilized by the GStreamer infrastructure plugins to interact with the acceleration kernel. When used together, they enable a GStreamer-based application to leverage accelerated functionality.

Example pipelines utilizing GStreamer infrastructure plugins and acceleration software libraries will be covered later in this section. The vvas-gst-plugins folder within the vvas source code tree contains the available GStreamer infrastructure plugins, and each plugin will be described in detail in the following section.


.. toctree::
   :maxdepth: 1

   vvas_xfilter <./plugin_vvas_xfilter>

.. toctree::
   :maxdepth: 1
   
   vvas_xmultisrc <./plugin_vvas_xmultisrc>

.. toctree::
   :maxdepth: 1
   
   JSON Schema <./json_file_schema>

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

