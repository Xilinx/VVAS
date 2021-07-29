############################################################
Vitis Video Analytics SDK Overview
############################################################

The Vitis Video Analytics SDK **(VVAS)** is a framework to build transcoding and AI-powered solutions on Xilinx platforms. It takes input data as - from USB/CSI camera, video from file or streams over RTSP, uses Vitis AI to generate insights from pixels for various usecases. VVAS SDK can be the foundation layer for a number of video analytic solutions like understanding traffic and pedestrians in smart city, health and safety monitoring in hospitals, self-checkout and analytics in retail, detecting component defects at a manufacturing facility and others. VVAS can also be used to build Adaptive Bitrate Transcoding solutions that may require re-encoding the incoming video at different bitrates, resolution and encoding format. 

The core SDK consists of several hardware accelerator plugins that use various accelerators such as Video Encoder, Decoder, multiscaler (for resize and color space conversion), Deep learning Processing Unit (DPU) for Machine Learning etc.. By performing all the compute heavy operations in a dedicated accelerator, VVAS can achieve highest performance for video analytics, transcoding and several other application areas. 

For the developer community, VVAS also provides a framework in the form of generic Infrastructure plugins, software acceleration libraries and a simplified interface to develop their own acceleration library to control a custom hardware accelerator. Using this framework, user can easily integrate their custom accelerators/kernels into Gstreamer framework. VVAS builds on top of Xilinx Run Time (XRT) and Vitis AI and abstracts these complex interface, making it easy for developers to build video analytics and transcoding pipelines without having to learn the complexities of XRT, Vitis AI.

Using VVAS SDK,  applications can be deployed on an embedded edge device running on Zynq MPSoc platform or can be deployed on larger edge or datacenter platforms like Alveo U30. 

.. figure:: /docs/images/VVA_TopLevel_Overview.png
   :width: 1300

   VVAS Block Diagram

**********************************************************
VVAS Graph Architecture
**********************************************************

VVAS is an optimized graph architecture built using the open source GStreamer framework. The graph below shows a typical video analytic application starting from input video to outputting insights. All the individual blocks are various plugins that are used. At the bottom are the different hardware engines that are utilized throughout the application. Optimum memory management with zero-memory copy between plugins and the use of various accelerators ensure the highest performance.

.. figure:: /docs/images/VVA_Graph.png
   :width: 1100

* Streaming data can come over the network through RTSP or from a local file system or from a camera directly. The captured frames are sent for decoding using the hardware accelerated video decoder. ``ivas_xvcudec``, ``omxh264dec`` and ``omxh265dec`` are the plugin for decoding. 
* After decoding, there is an optional image pre-processing step where the input image can be pre-processed before inference. The pre-processing can be resizing the image or color space conversion. ivas_xabrscaler plugin can perform hardware accelerated resize as well as color format conversion on the frame.
* After pre-processing, frame is sent for inference. Inference is performed using ivas infrastructure plugin, ivas_xfilter and ivas_xdpuinfer acceleration library. ivas_xdpuinfer is built on top of Vitis AI Development Kit to accelerate the AI Inference on Xilinx hardware platforms. 
* To overlay the inference results such as bounding boxes, labels etc., there is a plugin called ivas_xboundingbox. This plugin draws bounding box and level information on the frame.
* Finally to output the results, VVAS presents various options, like render the output with the bounding boxes on the screen, save the output to the local disk, stream out over RTSP.



.. toctree::
   :maxdepth: 3
   :caption: Release Notes
   :hidden:

   Release Notes <docs/release_notes>

.. toctree::
   :maxdepth: 3
   :caption: Base Infrastructure
   :hidden:

   Plugins <docs/common/common_plugins>
   For Advanced Developers <docs/common/for_developers>

.. toctree::
   :maxdepth: 3
   :caption: Embedded
   :hidden:

   Platforms & Applications <docs/Embedded/platforms_and_applications>
   Plugins <docs/Embedded/6-embedded-plugins>
   Tutorials <docs/Embedded/Tutorials/Tutorials>

.. toctree::
   :maxdepth: 3
   :caption: Data Center
   :hidden:

   Plattforms & Applications <docs/DC/platforms_and_applications>
   Plugins <docs/DC/6-DC-plugins>

.. toctree::
   :maxdepth: 3
   :caption: White Papers
   :hidden:

   TBD <docs/common/6-common-Acceleration-Software-Library-Development>

.. toctree::
   :maxdepth: 3
   :caption: Training
   :hidden:

   TBD <docs/common/6-common-Acceleration-Software-Library-Development>

.. toctree::
   :maxdepth: 3
   :caption: FAQ
   :hidden:

   Frequently Asked Questions <docs/freq_asked_questions>


Why VVAS?
============

Application developers can build seamless streaming pipelines for AI-based video and image analytics, complex Adaptive Bitrate Transcoding pipelines and several other solutions using VVAS without having any understanding about FPGA or other development environmentcomplexities. VVAS ships with several hardware accelerators for various functionalities, highly optimized GStreamer plugins meeting most of the requirements of the Video Analytics and transcoding solutions.
For advanced developers, VVAS provides an easy to use framework to integrate their own hardware accelerators/kernels in to Gstreamer framework based applications.  
VVAS provide AI model support for popular object detection and classification models such as  SSD, YOLO etc.
All these infratructure gives the flexibility for rapid prototyping to full production level solutions by significantly reducing time to market for the solutions on Xilinx platforms. 



VVAS Core Components
====================

This section gives more deeper insight into VVAS. VVAS comprises following core components.

.. figure:: /docs/images/vvas_core_comp.png
   :width: 400


VVAS Custom Plug-ins
-------------------------------------------------------

These are highly optimized GStreamer plug-ins developed  to provide very specific functionality using optimized Kernels and IPs on Xilinx Platform. Refer to :ref:`VVAS Custom Plug-ins <custom_plugins_label>` for more details about how to use these plug-ins.

VVAS Infrastructure Plug-ins
------------------------------------------------

These are generic infrastructure GStreamer plug-ins being developed to help users to directly use these plug-ins to integrate their Kernels into GStreamer framework. User need not have in-depth understanding of the GStreamer framework. Refer to :ref:`VVAS Infrastructure Plug-ins <infra_plugins_label>` for more details about how to use these plug-ins. These plug-ins are part of “ivas-gst-plugins” repository.

VVAS Acceleration S/W Libs
-----------------------------------

These are optimized Acceleration s/w libs developed to manage the state machine of the acceleration Kernels/IPs and expose the interface so that these Acceleration s/w libs can be hooked into VVAS Generic Infrastructure Plug-ins. These can be used as reference to develop a new Acceleration s/w lib based on VVAS framework. Details about the vvas acceleration s/w libs and how can these be used with infrastructure plugins are explained in :ref:`VVAS Infrastructure Plug-ins and Acceleration s/w Libraries <infra_plugins_label>` section.

VVAS Acceleration H/W
------------------------------------------------

These are highly optimized Kernels being developed by Xilinx. Details of these are captured in this section. Refer to VVAS Acceleration H/W :doc:`VVAS Acceleration H/W  <docs/common/Acceleration-Hardware>` for more details.

VVAS Reference Platforms
----------------------------------

There are different requirements of different applicatios. VVAS provides several reference platforms catering to different applications/solutions needs. Embedded platforms and application details can be found in :doc:`Platforms And Applications <docs/Embedded/platforms_and_applications>` section.
