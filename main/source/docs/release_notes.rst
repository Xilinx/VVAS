#######################################
Vitis Video Analytics SDK Release V3.0
#######################################

****************
Release Summary
****************

* Based on 2022.2 Tools
* Based on Vitis AI 3.0 for Machine Learning
* Based on GStreamer 1.18.5
* Embedded Platforms
   - ``Zynq UltraScale+ MPSoC`` as well as ``Versal`` based platforms
* PCIe/Data Center Platform
   - ``Alveo V70``

*****************************
What is new in this release?
*****************************

* Added support for below mentioned models
   - Pose detection
   - Lane detection
   - Crowd detection
* Added software resize and colorspace conversion support in ``vvas_xabrscaler`` plug-in
* Added support for xmodel where user can use any model supported by Vitis AI and ``vvas_xinfer`` plug-in will perform the ML operation and returns the output tensor. User can perform his/her post-processing on the output tensor data. For more details, refer to  :ref:`vvas_xinfer <vvas_xinfer>`
* In Embedded platform example designs Autologin has been disabled and user need to enter login and password for each boot. “root” is the default user name and users are required to set the new password at the time of fist login.
* VVAS Tracker implementation uses `Simd <https://github.com/ermig1979/Simd>` library for high performance image processing operations. User has option to build VVAS with Simd library or not.


*******************************************
Other changes compared to previous release
*******************************************

* ``vvas_xboundingbox`` accel-sw-lib has been discontinued and removed from this release. ``vvas_xmetaconvert`` and ``vvas_xoverlay``  plug-ins can be used to implement the features supported by ``vvas_xboundingbox`` and many more new features.
* ``vvas_xdpuinfer`` accel-sw-lib has been discontinued and removed from this release. ``vvas_xinfer`` plug-in shall be used for the inference requirements.
* JSON file schema and fields have been updated. Refer to the relavant section in this document to know details about the modifications. In case you were already using previous version of VVAS release, then please adapt your applications as per changes in this release.
* ``vvas_xpreprocessor`` accel-sw-lib has been discontinued and removed from this release. Pre-processing functionality can be achieved through ``vvas_xabrscaler`` plug-in. Preprocessing needed by Machine Learning can also be done by ``vvas_xinfer`` plug-in.
* ``vvas_xvcudec`` plug-in has been discontinued and removed from this release and instead ``vvas_xvideodec`` plug-in has been introduced for video decoding on PCIe platform. This plug-in will not work on Alveo U30 platform. 
* ``vvas_xvcuenc`` plug-in is not supported this release.
* ``vvas_xlookahead`` plug-in is not supported in this release.
* ``v_multiscaler`` kernel has been renamed to ``image_processing`` kernel to align with the features it supports.
* Support for Alveo U30 card has been discontinued and removed from this release
* Encoding and transcoding support has been removed in PCIe platform.
* GstInference metadata structure has been re-organized and hence it is not backward compatible with previous VVAS releases. In case you were already using previous version of VVAS release and GstInference metadata, then please adapt your applications as per changes in this release.

*********
Features
*********

* Supports different input sources like Camera, RTP/RTSP streaming, file source etc.
* Hardware accelerated H264/H265 Video Encoding/Decoding.
* Vitis AI based inferencing for detection and classification
* Supported ML Models

  - resnet50
  - resnet18
  - mobilenet_v2
  - inception_v1
  - ssd_adas_pruned_0_95
  - ssd_traffic_pruned_0_9
  - ssd_mobilenet_v2
  - ssd_pedestrian_pruned_0_97
  - plate_detect
  - plat_num
  - yolov3_voc_tf
  - yolov3_adas_pruned_0_9
  - refinedet_pruned_0_96
  - yolov2_voc
  - yolov2_voc_pruned_0_77
  - densebox_320_320
  - densebox_640_360
  - bcc_pt
  - efficientdet_d2_tf
  - efficientnet-b0_tf2
  - face_mask_detection_pt
  - facerec_resnet20
  - refinedet_pruned_0_96
  - sp_net
  - ultrafast_pt
  - vpgnet_pruned_0_99

* Hardware accelerated Resize and color space conversion
* On-screen displaying bounding box around objects, text, circle. arrows, polygon etc.
* HDMI Tx and Display Port interface for displaying the contents

******************
Known Limitations
******************

* Image_processing kernel require minimum cropping width and height to be 16. In case the cropping width or height is less than 16, then cropping is not performed. 
* Due to the above limitation, during cascaded ML inferencing, in case the first stage ML inference generates objects with region of interest having width and/height less than 16, then the object is not cropped and the ML inference for that object in next stages will not be correct.
* In case ``Image Processing`` kernel is being used for resize, scaling or cropping, then output resolution width must be multiple of 8*PPC (Pixel Per Clock). ``Image_processing`` kernel can be configured to process 1, 2 or 4 Pixels Per Clock. Higher the PPC value, higher will be throughput. The kernel will always write the data in chunks of 8*PPC. In case the output width is not multiple of 8*PPC, then there may be some garbage data in the right side of the image, depending on the difference between the actual width and the 8*PPC aligned width.   
* All inputs to the ``vvas_xcompositor`` shall have same frame rate otherwise the processing will be controlled by the slowest frame rate input stream.

For platform/example specific known limitations, refer to the example/application design.

*************
Known Issues
*************

Embedded Platforms
-------------------

* On zcu104 boards, Cascaded pipelines OR several ML instances running simultaneously OR few models need more processing power when running in performance mode are sending board into bad state and needs reboot to recover from it. The default value of IOUT_OC_FAULT_LIMIT on PMIC chip irps5401 is too low and that is causing the temperature fault limit getting crossed. Workaround is to increase this limit. But there is risk of board getting damaged if running for long time.
