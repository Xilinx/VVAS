####################################
Vitis Video Analytics SDK Overview
####################################

The Vitis Video Analytics SDK (VVAS) is a framework designed for AMD platforms that allows creation of various AI and transcoding solutions. It supports input data from USB/CSI camera, video from file or streams over RTSP, and uses Vitis AI to generate insights from pixels for various use cases. VVAS SDK can be the foundation layer for multiple video analytic solutions like understanding traffic and pedestrians in smart city, health and safety monitoring in hospitals, self-checkout, and analytics in retail, detecting component defects at a manufacturing facility and others. VVAS can also be used to build Adaptive Bitrate Transcoding solutions that may require re-encoding the incoming video at different bitrates, resolution and encoding format. 

The core SDK comprises of optimized VVAS Core C-APIs, as well as GStreamer plug-ins that employ various accelerators such as video encoders, decoders, image processing kernels for resizing and color space conversion, Deep learning Processing Units (DPUs) for machine learning, etc.

By running computationally heavy operations on specialized accelerators, VVAS facilitates the creation of high-performance video analytics, transcoding pipelines and other applications.

For developers, VVAS provides a framework in the form of Infrastructure GStreamer plug-ins and a simplified interface to create custom acceleration libraries to control a custom hardware accelerator. This framework enables users to easily integrate their custom accelerators/kernels into the GStreamer framework. VVAS builds on top of Xilinx Run Time (XRT) and Vitis AI, abstracting these complex interfaces, so developers can create video analytics and transcoding pipelines without having to learn the intricacies of XRT or Vitis AI.

VVAS SDK supports deployment on embedded edge devices, as well as larger edge or data center platforms like Alveo V70.

.. figure:: /docs/images/VVA_TopLevel_Overview.png
   :width: 1300

   VVAS Block Diagram

************************
VVAS Graph Architecture
************************

VVAS is a streamlined graph structure constructed with the help of the GStreamer framework. The diagram below demonstrates a common video analytic pipeline that begins with the input of video data and concludes with the presentation of valuable insights. Each of the constituent blocks is composed of distinct plugins. The application employs several hardware engines, which are depicted at the bottom. By implementing efficient memory management techniques, avoiding unnecessary memory duplication between plugins, and utilizing diverse accelerators, the system attains optimal performance.

.. figure:: /docs/images/VVA_Graph.png
   :width: 1100

*  Data that is being streamed can originate from multiple sources such as the network through RTSP, the local file system, or directly from a camera. The received frames are then sent for decoding using a hardware-accelerated video decoder. The decoding process is carried out using plugins such as ``vvas_xvideodec``, ``omxh264dec``, and ``omxh265dec``.
* Once decoding is complete, there is an optional image pre-processing phase where the input image can undergo certain adjustments prior to inference. Pre-processing can entail resizing the image, converting its color space, or performing mean subtraction, among other operations. The ``vvas_xabrscaler`` plugin can facilitate hardware-accelerated resizing, color format conversion and pre-processing operations like mean subtraction, normalization etc. on the frame.
* Following pre-processing, the frame is directed to undergo inference, which is carried out through the use of the ``vvas_xinfer`` plugin. This plugin not only performs pre-processing but also facilitates ML inferencing in batch mode, thereby enhancing overall performance.
* In order to overlay inference results such as bounding boxes, labels, and arrows into the video output, two plugins are utilized: ``vvas_xmetaconvert`` and ``vvas_xoverlay``. The former plugin comprehends the ``GstInferenceMeta`` meta-data structure and carries out parsing operations to create new metadata for lines, arrows, text, and other such elements, which can be understood by the ``vvas_xoverlay`` plugin. The ``vvas_xoverlay`` plugin is responsible for drawing various components such as bounding boxes, arrows, circles, and text based on the received information.
* Lastly, VVAS offers a range of options for presenting the results, including displaying the output with bounding boxes on the screen, saving the output to the local disk, or streaming it out over RTSP.



.. toctree::
   :maxdepth: 3
   :caption: Release Notes
   :hidden:

   Release Notes <docs/release_notes>

.. toctree::
   :maxdepth: 3
   :caption: Base Infrastructure
   :hidden:

   VVAS GStreamer Plugins <docs/common/gstreamer_plugins/common_plugins>
   VVAS Meta Data <docs/common/meta_data/vvas_meta_data_structures>
   VVAS For Advanced Developers <docs/common/for_developers>
   VVAS Debug Support <docs/common/debug_support>

.. toctree::
   :maxdepth: 3
   :caption: Embedded
   :hidden:

   Platforms & Applications <docs/Embedded/platforms_and_applications>
   VVAS GStreamer Plugins <docs/Embedded/embedded-plugins>
   Tutorials <docs/Embedded/Tutorials/Tutorials>

.. toctree::
   :maxdepth: 3
   :caption: Data Center
   :hidden:

   Platforms & Applications <docs/DC/platforms_and_applications>

.. toctree::
   :maxdepth: 3
   :caption: FAQ
   :hidden:

   Frequently Asked Questions <docs/freq_asked_questions>

Why VVAS?
============

VVAS enables application developers to construct seamless streaming pipelines for AI-based video and image analytics, complex adaptive bitrate transcoding pipelines, and various other solutions without requiring any comprehension of FPGA or other complex development environments. VVAS comes equipped with a multitude of hardware accelerators for various functionalities, along with highly optimized GStreamer plugins that satisfy most requirements for video analytics and transcoding solutions.

For advanced developers, VVAS offers a user-friendly framework that allows for integration of their own hardware accelerators/kernels into GStreamer-based applications. VVAS also provides support for popular object detection and classification models such as SSD, YOLO, and others.

All of this infrastructure grants the flexibility for swift prototyping and progression to full-fledged production-level solutions, thereby significantly reducing the time-to-market for AMD platform-based solutions.


VVAS Core Components
====================

This section gives more deeper insight into VVAS. VVAS comprises following core components.

.. figure:: /docs/images/vvas_core_comp.png
   :width: 400


Custom GStreamer Plug-ins
-------------------------------------------------------

These are highly optimized GStreamer plug-ins developed to provide very specific functionality using optimized Kernels and IPs on AMD Platform. Refer to :ref:`VVAS Custom Plug-ins <custom_plugins_label>` for more details about how to use these plug-ins.

Infrastructure GStreamer Plug-ins
------------------------------------------------

These GStreamer plugins are designed as generic infrastructure to assist users in directly integrating their kernels into the GStreamer framework without requiring an in-depth understanding of the framework itself. Refer to :ref:`VVAS Infrastructure Plug-ins <infra_plugins_label>` for more details about how to use these plug-ins.

Acceleration S/W Libs
-----------------------------------

These optimized software libraries for acceleration have been developed to manage the state machine of acceleration kernels/IPs and provide an interface for integration with VVAS generic infrastructure plugins. They can serve as a reference for developing new acceleration software libraries based on the VVAS framework. Information on VVAS acceleration software libraries and their integration with infrastructure plugins is elaborated upon in the following details. :ref:`VVAS Infrastructure Plug-ins and Acceleration s/w Libraries <infra_plugins_label>` section.

Acceleration H/W (Kernels/IPs)
------------------------------------------------

These are highly optimized Kernels being developed by AMD. Details of these are captured in this section. Refer to VVAS Acceleration H/W :doc:`VVAS Acceleration H/W  <docs/common/Acceleration-Hardware>` for more details.

Reference Platforms and Applications
------------------------------------------

Various applications have distinct requirements, and VVAS addresses these by offering several reference platforms tailored to different needs. Examples of embedded platform designs and details on sample applications can be found in :doc:`Platforms And Applications <docs/Embedded/platforms_and_applications>` section. Similarly Platforms and application details for PCIe/Data center are covered in :doc:`PCIe/Data Center Platforms and Applications <docs/DC/platforms_and_applications>`
