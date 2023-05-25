.. _custom_plugins_label:

#################
Custom Plug-ins
#################
There are specific functions, like video decoder, encoder, meta-affixer etc. where the requirements are different and are difficult to implement in an optimized way using highly simplified and generic infrastructure plug-ins framework. Hence, these functions are implemented using custom GStreamer plug-ins. This section covers details about the custom plug-ins.

Infrastructure plug-ins are generic plug-ins that interact with the acceleration kernel through a set of APIs exposed by an acceleration software library corresponding to that kernel. Infrastructure plug-ins abstract the core/common functionality of the GStreamer framework (for example: caps negotiation and buffer management).

Acceleration software libraries control the acceleration kernel, like register programming, or any other core logic required to implement the functions. Acceleration software libraries expose a simplified interface that is called by the GStreamer infrastructure plug-ins to interact with the acceleration kernel.These libraries are used with one of the infrastructure plug-ins to use the functionality a GStreamer-based application. Example pipelines with GStreamer infrastructure plug-ins and acceleration software libraries are covered later in this section.

GStreamer infrastructure plug-ins are available in the ``vvas-gst-plugins`` folder in the vvas source code tree. The following section describes each infrastructure plug-in.


.. toctree::
   :maxdepth: 1

   vvas_xmetaaffixer <./plugin_vvas_xmetaaffixer>

.. toctree::
   :maxdepth: 1

   vvas_xabrscaler <./plugin_vvas_xabrscaler>

.. toctree::
   :maxdepth: 1

   vvas_xinfer <./plugin_vvas_xinfer>

.. toctree::
   :maxdepth: 1

   vvas_xoptflow <./plugin_vvas_xoptflow>

.. toctree::
   :maxdepth: 1

   vvas_xoverlay <./plugin_vvas_xoverlay>

.. toctree::
   :maxdepth: 1

   vvas_xtracker <./plugin_vvas_xtracker>

.. toctree::
   :maxdepth: 1

   vvas_xskipframe <./plugin_vvas_xskipframe>

.. toctree::
   :maxdepth: 1

   vvas_xreorderframe <./plugin_vvas_xreorderframe>

.. toctree::
   :maxdepth: 1

   vvas_xmetaconvert <./plugin_vvas_xmetaconvert>

.. toctree::
   :maxdepth: 1

   vvas_xfunnel <./plugin_vvas_xfunnel>

.. toctree::
   :maxdepth: 1

   vvas_xdefunnel <./plugin_vvas_xdefunnel>

.. toctree::
   :maxdepth: 1

   vvas_xmulticrop <./plugin_vvas_xmulticrop>

.. toctree::
   :maxdepth: 1

   vvas_xcompositor <./plugin_vvas_xcompositor>

.. toctree::
   :maxdepth: 1

   VVAS Bufferpool & Allocator <./vvas_bufferpool_and_allocator>

