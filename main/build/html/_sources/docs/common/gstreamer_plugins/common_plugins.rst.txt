###################
GStreamer Plug-ins
###################

VVAS is based on the GStreamer framework. This section describes the VVAS GStreamer plug-ins, their input, outputs, and control parameters. The plug-ins source code is available in the ``vvas-gst-plugins`` folder of the VVAS source tree. The two types of VVAS GStreamer plug-ins are custom plug-ins and infrastructure plug-ins. Infrastructure plug-ins are developed to enable developers to integrate their kernel into GStreamer based applications without having understanding about GStreamer framework. Infrastructure plug-ins encapsulates most of the basic GStreamer plug-in requirements/features, like memory management, kernel configuration, caps negotiation etc. Same Infrastructure plug-ins can be used for integrating different kernels to realize different functionalities. Custom plug-ins implement a specific functionality, like encode, decode, overlay etc. that can't be implemented using Infrastructure plug-ins in an optimized way. 

This section covers the plug-in that are common for Edge (Embedded) as well as cloud (PCI basede) solutions. There are few plug-ins that are specific to Edge/Embedded platforms and are covered in :doc:`Plugins for Embedded platforms <../../Embedded/embedded-plugins>`. The following table lists the VVAS GStreamer plug-ins.


There are specific functions, like video decoder, encoder, meta-affixer etc. where the requirements are different and are difficult to implement in an optimized way using highly simplified and generic infrastructure plug-ins framework. Hence, these functions are implemented using custom GStreamer plug-ins. This section covers details about the custom plug-ins.

.. toctree::
   :maxdepth: 1

   Custom Plugins <./custom_plugins>

.. toctree::
   :maxdepth: 1

   Infrastructure Plugins <./infra_plugins>
