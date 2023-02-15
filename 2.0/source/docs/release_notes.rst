#######################################
Vitis Video Analytics SDK Release V2.0
#######################################

****************
Release Summary
****************

* Based on Vivado 2022.1, Vitis 2022.1, Petalinux 2022.1
* Based on Vitis AI 2.5 for Machine Learning
* Embedded Platforms
   - Zynq UltraScale+ MPSoC based ``ZCU104`` Development Board 
   - ``KV260`` KRIA SOM Boards
   - ``vck190`` board, a ``Versal`` based platform
* PCIe/Data Center Platform
   - Zynq UltraScale+ MPSoC based Alveo U30 Cards
* Based on GStreamer 1.18

*****************************
What is new in this release?
*****************************

* Introducing support for PCIe based platforms

  - Releasing example design for Transcoding solution on Alveo U30 based platform
* Added new plug-ins

  - **vvas_xcompositor**: Hardware accelerated composition of frames from up to 16 input sources.
  - **vvas_xmulticrop**: Hardware accelerate cropping of multiple ROI from a frame.
  - **vvas_xopticleflow**: To detect motion using optical flow algorithm.
  - **vvas_xoverlay**: To draw text, rectangle, polygon, arrows, circles on a frame
  - **vvas_xmetaconvert**: plug-in to translate inference meta data in to overlay meta data
  - **vvas_xtracker**: Implements tracking algorithms to track objects in multiple frames
  - **vvas_xfunnel**: Multiplex streams on multiple input pads and output onto a single pad
  - **vvas_xdefunnel**: Send buffers received on input pad on different output pads corresponding to the stream id information associated with the buffer.
* Migrated to XRT native APIs
  - Development of acceleration software libraries simplified with this change. This is not backward compatible.
* Tutorial for cascaded Machine Learning
* On-screen displaying bounding box around objects, text, circle. arrows, polygon etc.

*********
Features
*********

* Supports different input sources like Camera, RTP/RTSP streaming, file source etc.
* Hardware accelerated H264/H265 Video Encoding/Decoding
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

* Hardware accelerated Resize and color space conversion
* Region Of Interest based encoding
* On-screen displaying bounding box around objects, text, circle. arrows, polygon etc.
* HDMI Tx and Display Port interface for displaying the contents

******************
Known Limitations
******************

* Multiscaler kernel require minimum cropping width and height to be 64. In case the cropping width or height is less than 64, then cropping is not performed. 
* Due to the above limitation, during cascaded ML inferencing, in case the first stage ML inference generates- objects with region of interest having width and/height less than 64, then the object is not cropped and the ML inference for that object in next stages will not be correct.  
* All inputs to the ``vvas_xcompositor`` shall have same frame rate otherwise the processing will be controlled by the slowest frame rate input stream.

For platform/example specific known limitations, refer to the example/application design.

*************
Known Issues
*************

PCIe/Data Center
-----------------

* High density transcoding performance is less than real time as XRT is being configured in polling mode instead of interrupt mode because of a known issue in XRT on U30 platform.

For known issues, refer to the example/application design for specific issues, if any.

Embedded Platforms
-------------------

* On zcu104 boards, Cascaded pipelines OR several ML instances running simultaneously are sending board into bad state and needs reboot to recover from it. The default value of IOUT_OC_FAULT_LIMIT on PMIC chip irps5401 is too low and that is causing the temperature fault limit getting crossed. Workaround is to increase this limit. But there is risk of board getting damaged if running for long time.
