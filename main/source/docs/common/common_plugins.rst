###################
GStreamer Plug-ins
###################

VVAS is based on the GStreamer framework. This section describes the VVAS GStreamer plug-ins, their input, outputs, and control parameters. The plug-ins source code is available in the ``vvas-gst-plugins`` folder of the VVAS source tree. The two types of VVAS GStreamer plug-ins are custom plug-ins and infrastructure plug-ins. Infrastructure plug-ins are developed to enable developers to integrate their kernel into GStreamer based applications without having understanding about GStreamer framework. Infrastructure plug-ins encapsulates most of the basic GStreamer plug-in requirements/features, like memory management, kernel configuration, caps negotiation etc. Same Infrastructure plug-ins can be used for integrating different kernels to realize different functionalities. Custom plug-ins implement a specific functionality, like encode, decode, overlay etc. that can't be implemented using Infrastructure plug-ins in an optimized way. 

This section covers the plug-in that are common for Edge (Embedded) as well as cloud (PCI basede) solutions. There are few plug-ins that are specific to Edge/Embedded platforms and are covered in :doc:`Plugins for Embedded platforms <../Embedded/embedded-plugins>`. The following table lists the VVAS GStreamer plug-ins.

Table 1: GStreamer Plug-ins

.. list-table:: 
   :widths: 20 80
   :header-rows: 1
   
   * - Plug-in Name
     - Functionality
	 
   * - :ref:`vvas_xmetaaffixer`
     - Plug-in to scale and attach metadata to the frame of different resolutions.

   * - :ref:`vvas_xabrscaler`
     - Hardware accelerated scaling and color space conversion.

   * - :ref:`vvas_xmultisrc`
     - A generic infrastructure plug-in: 1 input, N output, supporting transform processing.

   * - :ref:`vvas_xfilter`
     - A generic infrastructure plug-in: 1 input, 1 output, supporting pass-through, in-place, and transform processing.

   * - :ref:`vvas_xinfer`
     - An inference plug-in using vvas_xdpuinfer software library and attaches inference output as GstInferenceMeta to input buffer. Also, this plug-in does optional, hardware accelerated  pre-processing required for inference.

   * - :ref:`vvas_xoptflow`
     - Plug-in to estimate optical flow using previous and current frame.

   * - :ref:`vvas_xoverlay`
     - Plug-in to draw text, boxes, lines, arrows, circles, polygons, and time stamp on frames. It uses vvas_xoverlay library for drawing on frames.

   * - :ref:`vvas_xtracker`
     - Plug-in to track objects of interest detected during infer.  This plug-in tracks the previously detected objects during the time interval when infer information is not available.

   * - :ref:`vvas_xskipframe`
     - Plugin to control the rate of inference. This plugin skips the frames for inference based on user configured infer-interval property.

   * - :ref:`vvas_xreorderframe`
     - Plugin to arrange the frames received from :ref:`vvas_xinfer` and :ref:`vvas_xskipframe` plugins in correct order of presentation before pushing them downstream.

   * - :ref:`vvas_xmetaconvert`
     - Plugin to convert VVAS ML inference metadata (GstInferenceMeta) into overlay metadata

   * - :ref:`vvas_xfunnel`
     - Plug-in to serialize data on its sink pads in round robin order

   * - :ref:`vvas_xdefunnel`
     - Plug-in to de-serialize data serialized using `vvas_xfunnel` plug-in

   * - :ref:`vvas_xmulticrop`
     - Hardware accelerated plug-in for doing static and dynamic cropping, pre-processing, scaling, and color space conversion.

   * - :ref:`vvas_xcompositor`
     - Hardware accelerated N input, 1 output plug-in that combines two or more video frames into a single frame.


.. _custom_plugins_label:

*****************
Custom Plug-ins
*****************

There are specific functions, like video decoder, encoder, meta-affixer etc. where the requirements are different and are difficult to implement in an optimized way using highly simplified and generic infrastructure plug-ins framework. Hence, these functions are implemented using custom GStreamer plug-ins. This section covers details about the custom plug-ins.

.. _vvas_xmetaaffixer:

.. include:: ./plugin_vvas_xmetaaffixer.rst

.. _vvas_xabrscaler:

.. include:: ./plugin_vvas_xabrscaler.rst

.. _vvas_xinfer:

.. include:: ./plugin_vvas_xinfer.rst

.. _vvas_xoptflow:

.. include:: ./plugin_vvas_xoptflow.rst

.. _vvas_xoverlay:

.. include:: ./plugin_vvas_xoverlay.rst

.. _vvas_xtracker:

.. include:: ./plugin_vvas_xtracker.rst

.. _vvas_xskipframe:

.. include:: ./plugin_vvas_xskipframe.rst

.. _vvas_xreorderframe:

.. include:: ./plugin_vvas_xreorderframe.rst

.. _vvas_xmetaconvert:

.. include:: ./plugin_vvas_xmetaconvert.rst

.. _vvas_xfunnel:

.. include:: ./plugin_vvas_xfunnel.rst

.. _vvas_xdefunnel:

.. include:: ./plugin_vvas_xdefunnel.rst

.. _vvas_xmulticrop:

.. include:: ./plugin_vvas_xmulticrop.rst

.. _vvas_xcompositor:

.. include:: ./plugin_vvas_xcompositor.rst

.. _infra_plugins_label:

**********************************************************************
Infrastructure Plug-ins and Acceleration Software Libraries
**********************************************************************

Infrastructure plug-ins are generic plug-ins that interact with the acceleration kernel through a set of APIs exposed by an acceleration software library corresponding to that kernel. Infrastructure plug-ins abstract the core/common functionality of the GStreamer framework (for example: caps negotiation and buffer management).

Table 16: Infrastructure Plug-ins

+----------------------------------------+----------------------------------+
|  **Infrastructure Plug-ins**           |          **Function**            |
|                                        |                                  |
+========================================+==================================+
|    vvas_xfilter                        | Plug-in has one input, one output|
|                                        | Support Transform, passthrough   |
|                                        | and inplace transform operations |
+----------------------------------------+----------------------------------+
|    vvas_xmultisrc                      | Plug-in support one input and    |
|                                        | multiple output pads.            |
|                                        | Support transform operation      |
+----------------------------------------+----------------------------------+

.. note::

        Though one input and one output kernel can be integrated using any of the two infrastructure plug-ins, we recommend using vvas_xfilter plugin for one input and one output kernels.

Acceleration software libraries control the acceleration kernel, like register programming, or any other core logic required to implement the functions. Acceleration software libraries expose a simplified interface that is called by the GStreamer infrastructure plug-ins to interact with the acceleration kernel.These libraries are used with one of the infrastructure plug-ins to use the functionality a GStreamer-based application. Example pipelines with GStreamer infrastructure plug-ins and acceleration software libraries are covered later in this section.

GStreamer infrastructure plug-ins are available in the ``vvas-gst-plugins`` folder in the vvas source code tree. The following section describes each infrastructure plug-in.

.. _vvas_xfilter:

.. include:: ./plugin_vvas_xfilter.rst

.. _vvas_xmultisrc:

.. include:: ./plugin_vvas_xmultisrc.rst

GstVvasBufferPool
=================

The GStreamer VVAS buffer pool holds the pool of video frames allocated using the GStreamer allocator object. It is derived from the GStreamer base video buffer pool object (GstVideoBufferPool).

The VVAS buffer pool:

* Allocates buffers with stride and height alignment requirements. (e.g., the video codec unit (VCU) requires the stride to be aligned with 256 bytes and the height aligned with 64 bytes)

* Provides a callback to the GStreamer plug-in when the buffer comes back to the pool after it is used.

The following API is used to create the buffer pool.

.. code-block::

      GstBufferPool *gst_vvas_buffer_pool_new (guint stride_align, guint
      elevation_align)

      Parameters:
         stride_align - stride alignment required
         elevation_align - height alignment required

      Return:
         GstBufferPool object handle

Plug-ins can use the following API to set the callback function on the VVAS buffer pool and the callback function is called when the buffer arrives back to the pool after it is used.

.. code-block::

      Buffer release callback function pointer declaration:
      typedef void (*ReleaseBufferCallback)(GstBuffer *buf, gpointer user_data);

      void gst_vvas_buffer_pool_set_release_buffer_cb (GstVvasBufferPool *xpool,
      ReleaseBufferCallback release_buf_cb, gpointer user_data)

      Parameters:
         xpool - VVAS buffer pool created using gst_vvas_buffer_pool_new
         release_buf_cb - function pointer of callback
         user_data - user provided handle to be sent as function argument while
      calling release_buf_cb()

      Return:
          None



GstVvasAllocator
==================

The GStreamer VVAS allocator object is derived from an open source GstAllocator object designed for allocating memory using XRT APIs. The VVAS allocator is the backbone to the VVAS framework achieving zero-copy (wherever possible).


Allocator APIs
---------------------------------

GStreamer plug-in developers can use the following APIs to interact with the VVAS allocator. To allocate memory using XRT, the GStreamer plug-ins and buffer pools require the GstAllocator object provided by the following API.

.. code-block::

      GstAllocator* gst_vvas_allocator_new (guint dev_idx, gboolean need_dma)

      Parameters:
         dev_idx - FPGA Device index on which memory is going to allocated
         need_dma - will decide memory allocated is dmabuf or not

      Return:
         GstAllocator handle on success or NULL on failure

.. note:: 

        In PCIe platforms, the DMA buffer allocation support is not available. This means that the need_dma argument to gst_vvas_allocator_new() API must be false.

Use the following API to check if a particular GstMemory object is allocated using GstVvasAlloctor.

.. code-block::

      gboolean gst_is_vvas_memory (GstMemory *mem)

      Parameters:
         mem - memory to be validated

      Return:
         true if memory is allocated using VVAS Allocator or false


When GStreamer plug-ins are interacting with hard-kernel/IP or soft-kernel, the plug-ins need physical memory addresses on an FPGA using the following API.

.. code-block::

      guint64 gst_vvas_allocator_get_paddr (GstMemory *mem)

      Parameters:
         mem - memory to get physical address

      Return:
         valid physical address on FPGA device or 0 on failure

      Use the following API when plug-ins need an XRT buffer object (BO) corresponding to an VVAS memory object.

.. code-block::

      guint gst_vvas_allocator_get_bo (GstMemory *mem)

      Parameters:
         mem - memory to get XRT BO

      Return:
         valid XRT BO on success or 0 on failure



JSON Schema
=============

This section covers the JSON schema for the configuration files used by the infrastructure plug-ins. For more details, refer to :doc:`JSON Schema <JSON-File-Schema>`


VVAS Inference Meta Data
=====================================

This section covers details about inference meta data structure used to store meta data. For more details, refer to :ref:`vvas_inference_metadata`

Overlay Meta Data
==================

vvas_xoverlay plug-in understand GstOverlayMetaData structure. Details about this structure can be referred at :ref:`vvas_overlay_metadata`.
