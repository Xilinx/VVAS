######################################
Vitis Video Analytics SDK Release V1.0
######################################

*********
Summary
*********

* Based on Vivado 2021.1, Vitis 2021.1, Petalinux 2021.1
* Based on Vitis AI 1.4 for Machine Learning
* Supports Zynq Ultrascale+ MPSoc based devices like ``ZCU104`` Development Board and ``KV260`` SOM Embedded Platforms


****************
Features
****************

* Supports different input sources like Camera, RTP/RTSP streaming, file source etc.
* H264/H265 Video Encoding/Decoding
* Vitis AI based inferencing for detection and classification
* Supporting 16 ML Models
   - resnet50
   - resnet18
   - mobilenet_v2
   - inception_v1
   - ssd_adas_pruned_0_95
   - ssd_traffic_pruned_0_9
   - ssd_mobilenet_v2
   - ssd_pedestrian_pruned_0_97
   - tiny_yolov3_vmss
   - yolov3_voc_tf
   - yolov3_adas_pruned_0_9
   - refinedet_pruned_0_96
   - yolov2_voc
   - yolov2_voc_pruned_0_77
   - densebox_320_320
   - densebox_640_360
* Hardware accelerated Resize and colorspace conversion
* Region Of Interest based encoding
* On-screen displaying bounding box around objects and text overly
* HDMI Tx and Display Port interface for displaying the contents

******************
Known Limitations
******************
For known limitations, refer to the example/application design for specific limitations, if any.

*************
Known Issues
*************
For known issues, refer to the example/application design for specific issues, if any.
