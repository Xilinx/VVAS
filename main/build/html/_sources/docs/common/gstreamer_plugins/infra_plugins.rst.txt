.. _infra_plugins_label:

############################################################
Infrastructure Plug-ins and Acceleration Software Libraries
############################################################

Infrastructure plug-ins are generic plug-ins that interact with the acceleration kernel through a set of APIs exposed by an acceleration software library corresponding to that kernel. Infrastructure plug-ins abstract the core/common functionality of the GStreamer framework (for example: caps negotiation and buffer management).

Acceleration software libraries control the acceleration kernel, like register programming, or any other core logic required to implement the functions. Acceleration software libraries expose a simplified interface that is called by the GStreamer infrastructure plug-ins to interact with the acceleration kernel.These libraries are used with one of the infrastructure plug-ins to use the functionality a GStreamer-based application. Example pipelines with GStreamer infrastructure plug-ins and acceleration software libraries are covered later in this section.

GStreamer infrastructure plug-ins are available in the ``vvas-gst-plugins`` folder in the vvas source code tree. The following section describes each infrastructure plug-in.

.. toctree::
   :maxdepth: 1

   vvas_xfilter <./plugin_vvas_xfilter>

.. toctree::
   :maxdepth: 1
   
   vvas_xmultisrc <./plugin_vvas_xmultisrc>

.. toctree::
   :maxdepth: 1
   
   JSON Schema <./json_file_schema>
