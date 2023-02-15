..
   Copyright 2022 Xilinx, Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

##########################
Frequently Asked Questions
##########################


.. raw:: html

   <details>
   <summary>Is VVAS open source?</summary>

Yes.

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary>What type of licensing options are available for VVAS source code (package)?</summary>

VVAS Release has been covered by below mentioned licenses:

* Apache License Version 2.0
* 3-Clause BSD License
* GNU GENERAL PUBLIC LICENSE
* The MIT License

.. raw:: html

   </details>
   </br>





.. raw:: html

   <details>
   <summary>What platforms and OS are compatible with VVAS?</summary>

VVAS is tested on PetaLinux for embedded platforms and UBUntu 20.04 for PCIe based platforms. For more information about supported platforms, refer to :doc:`Platforms And Applications <Embedded/platforms_and_applications>`.


.. raw:: html

   </details>
   </br>







.. raw:: html

   <details>
   <summary>Which AI models are supported with VVAS?</summary>

The following models are supported for this release:

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

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary>How do I enable models that are not officially supported?</summary>

Refer to :doc:`How to add support for new model in VVAS <common/adding_new_model>`.

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary>What is the version of Vitis AI tool used for VVAS?</summary>

This VVAS release supports Vitis AI 3.0.


.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary>Is VVAS compatible with lower versions of Vitis AI tools, such as VAI 1.3?</summary>

No, it has dependencies on Vitis AI 3.0.


.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary>How can I change the model in the pipeline?</summary>

The model name to be used for inference has to be provided in the JSON file for ``vvas_xinfer`` plug-in. For more details, see :ref:`vvas_xinfer <vvas_xinfer>`.


.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary>Can the model be changed dynamically?</summary>

while a pipeline is running, the model parameters cannot be modified. To change the model's parameters, stop the running pipeline, and then update the JSON file and then re-start the pipeline.


.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary>What types of input streams are supported?</summary>

* H.264, H.265 encoded video streams
* Raw video frames in NV12, BGR/RGB formats


.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary>Is receiving RTSP stream supported?</summary>

Receiving RTSP stream is supported by an open source plugin. 


.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary>Is multi-stream processing supported (such as muletiple decode and detections)?</summary>

Yes, VVAS suports simultaneous execution of multiple instances of plugins to realize multistream decode and ML operations.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary>How do I develop kernel libraries</summary>

Refer to :doc:`Acceleration s/w development guide <common/Acceleration-Software-Library-Development>`.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary>Do I need FPGA design experience to develop video analytics applications with VVAS?</summary>

No. VVAS SDK ships with most of the building blocks needed for video alalytics applications. These building blocks are highly optimized and ready to use. There are several example designs available with this release for video analytics applications. You may directly use these or make modifications as per your needs to build video analytics application. Refer :doc:`Platforms And Applications <Embedded/platforms_and_applications>`.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary>Is ROI-based encoding supported?</summary>

Yes. The :ref:`ROI Plug-in <roi-plugin>` that generates ROI data required for encoders.

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary>Can I generate multiple output resolutions for a single input frame?</summary>

Yes. The ``vvas_xabrscaler`` plug-in controls the ``multiscaler`` kernel to generate up to 8 different resolutions for one input frame. This plugin, along with resize, can also do colorspace conversion.

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary>Is audio analytics supported?</summary>

No.

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary>Are there sample accelerated applications developed using VVAS?</summary>

Yes. There are sample accelerated platforms and applications provided that you can execute by following a few steps. Start at :doc:`Platforms And Applications <Embedded/platforms_and_applications>`.

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary>Is there support for multi-stage (cascading) network?</summary>

One can connect multipe instances of ``vvas_xinfer`` one after another to implement multi-stage cascading network. Inference data generated by current ML operation will be appended to the Inference data generated by the previous ML stages.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary>How to debug VVAS application if there are any issues?</summary>

Refer to :doc:`VVAS Debug Support <common/debug_support>`

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary>How do I check the throughput of VVAS application/pipeline?</summary>

Using GStreamer's native fps display mechanism.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary>How do I compile and prune the model to be used?</summary>

Refer to `Vitis AI 3.0 documentation <https://docs.xilinx.com/access/sources/ud/document?Doc_Version=3.0%20English&url=ug1431-vitis-ai-documentation>`_.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary>How do I build plugins?</summary>

For Embedded platforms, refer to :ref:`Building VVAS Plugins and Libraries <build_vvas_plugins_and_libs>`.

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary>What if I cannot find the information that i am looking for?</summary>

Contact support vvas_discuss@amd.com.

.. raw:: html

   </details>
   </br>
