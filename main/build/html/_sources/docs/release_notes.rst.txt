######################################
Vitis Video Analytics SDK Release V1.1
######################################

*********
Summary
*********

- Based on Vivado 2021.2, Vitis 2021.2, Petalinux 2021.2
- Based on Vitis AI 2.0 for Machine Learning
- Supports:
  
  - Zynq Ultrascale+ MPSoc based devices like ``ZCU104`` Development Board and ``KV260`` SOM Embedded Platforms
  - ``Versal`` based platforms like ``vck190``

****************************
What is new in this release?
****************************

- Added support for ``Versal`` device based platforms
- Support for cascaded Machine Learning
- Introducing a new plug-in called ``vvas_xinfer``, which supports:

  - pre-processing (Resize, colorspace conversion, Normalization
  - batch mode for increased DPU throughput
  - Consolidating the inference information generated from previous levels into a single data structure.

- Added support for two new models
  - Semantic Segmentation
  - Numberplate detection









*********
Features
*********

- Supports different input sources like Camera, RTP/RTSP streaming, file source etc.
- H264/H265 Video Encoding/Decoding
- Vitis AI based inferencing for detection and classification
- Supported ML Models

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

- Hardware accelerated Resize and color space conversion
- Region Of Interest based encoding
- On-screen displaying bounding box around objects and text overly
- HDMI Tx and Display Port interface for displaying the contents

******************
Known Limitations
******************

For known limitations, refer to the example/application design for specific limitations, if any.

*************
Known Issues
*************

For known issues, refer to the example/application design for specific issues, if any.

**************
Patches
**************
1. We need one patch in XRT to fix an issue related to memory bank handling. This issue was seen in Versal based platform when plug-in tries to write into a buffer from memory Bank 3. Refer to `XRT Patch <https://github.com/Xilinx/VVAS/tree/master/vvas-platforms/Embedded/zcu104_vcuDec_DP/petalinux/project-spec/meta-user/recipes-xrt>`_ for more details.
