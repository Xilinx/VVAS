######################################
Vitis Video Analytics SDK Release V1.0
######################################

*********
Summary
*********

* Based on Vitis 2021.1
* Based on Vitis AI 1.4 for Machine Learning
* Supports Zynq Ultrascale+ MPSoc based devices like ZCU104 Development Board and SOM Embedded Platforms
* Supports Alveo U30 Card for Data Center Solution


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
* Region Of Interest
* On-screen displaying bounding box around objects and text overly
* HDMI Tx and Display Port interface for displaying the contents

******************
Known Limitations
******************

*************
Known Issues
*************
#. Multiscaler kernel in the example designs in this release supports NV12, RGB and BGR color formats only. If any other color format is configured on multiscaler, then kernel hangs. You will see "timeout.." error message. If this condition occurs, then you need to reboot the board.


