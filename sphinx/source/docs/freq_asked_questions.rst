..
   Copyright 2021 Xilinx, Inc.

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
   <summary><a>Is VVAS open source?</a></summary>

Yes.

.. raw:: html

   </details>
   </br>











.. raw:: html

   <details>
   <summary><a>What type of licensing options are available for VVAS source code (package)?</a></summary>

VVAS Release has been covered by below mentioned licenses:

* Apache License
* Version 2.0
* 3-Clause BSD License
* GNU GENERAL PUBLIC LICENSE
* The MIT License

.. raw:: html

   </details>
   </br>








.. raw:: html

   <details>
   <summary><a>What platform and OS are compatible with VVAS?</a></summary>

VVAS is tested on PetaLinux for embedded platforms. For more information about supported platforms, refer to :doc:`Platforms And Applications <Embedded/platforms_and_applications>`.


.. raw:: html

   </details>
   </br>



.. raw:: html

   </details>
   </br>




.. raw:: html

   <details>
   <summary><a>Which AI models are supported with VVAS?</a></summary>

Below mentioned 16 models are supported.

* resnet50
* resnet18
* mobilenet_v2
* inception_v1
* ssd_adas_pruned_0_95
* ssd_traffic_pruned_0_9
* ssd_mobilenet_v2
* ssd_pedestrian_pruned_0_97
* tiny_yolov3_vmss
* yolov3_voc_tf
* yolov3_adas_pruned_0_9
* refinedet_pruned_0_96
* yolov2_voc
* yolov2_voc_pruned_0_77
* densebox_320_320
* densebox_640_360

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary><a>How do I enable models that are not officially supported?</a></summary>

Does this mean models not supported by Vitis AI? If the model is not in DPU deployable format, then it first needs to be converted into DPU deployable state. For this refer to `Vitis AI 1.4 documentation <https://www.xilinx.com/html_docs/vitis_ai/1_4/zmw1606771874842.html>`_.


.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary><a>What is the version of Vitis AI tool used for VVAS?</a></summary>

This VVAS release supports Vitis AI V1.4.


.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary><a>Is VVAS compatible with lower versions of Vitis AI tools, such as VAI 1.3?</a></summary>

No, it has dependencies on Vitis AI 1.4.


.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary><a>How can I change the model in the pipeline?</a></summary>

The model name to be used for inferencing has to be provided in the JSON file for dpuinfer. For more details, see :ref:`DPU Infer <json-ivas-dpuinfer>`.


.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary><a>Can the model be changed dynamically?</a></summary>

while a pipeline is running, the model details cannot change. To change the model's details, stop the running pipeline, and then update the JSON file. Re-start the pipeline.


.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary><a>What types of input streams are supported?</a></summary>

* H.264, H.265 encoded video streams
* Raw video frames in NV12, BGR/RGB formats


.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary><a>Is receiving RTSP stream supported?</a></summary>

Receiving RTSP stream is supported by an open source plugin. 


.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary><a>Is multi-stream processing supported (such as muletiple decode and detections)?</a></summary>

Yes, VVAS suports simultaneous execution of multiple instances of plugins to realize multistream decode and ML operations.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary><a>How do I develop kernel libraries</a></summary>

Refer to :doc:`Acceleration s/w development guide <common/6-common-Acceleration-Software-Library-Development>`.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary><a>Do I need FPGA design experience to develop video analytics applications with VVAS?</a></summary>

No. Using a platform that supports the required hardware/software components for the video analytics applications, you can directly use VVAS to realize your video analytics application with several reference solutions. Refer :doc:`Platforms And Applications <Embedded/platforms_and_applications>`.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary><a>Is ROI-based encoding supported?</a></summary>

Yes. The :ref:`ROI Plug-in <roi-plugin>` that generates ROI data required for encoders.

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary><a>Can I generate multiple outputs for a single input?</a></summary>

Yes. The ``ivas_xabrscaler`` plug-in controls the ``multiscaler`` kernel to generate up to 8 different resolutions for one input frame. This plugin, along with resize, can also do colorspace conversion.

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary><a>Is audio analytics supported?</a></summary>

No.

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary><a>Are there sample accelerated applications developed using VVAS?</a></summary>

Yes. There are sample accelerated platforms and applications provided that you can execute by following a few steps. Start at :doc:`Platforms And Applications <Embedded/platforms_and_applications>`.

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary><a>Is there support for multi-stage (cascading) network?</a></summary>

One can connect multipe instances of ``ivas_xdpuinfer`` one after another to implement multi-stage cascading network and each ML instance will generate its own inference data separately. This is already supported in this release. However accumulation of inference data from several ML instances in a pipeline into a single meta data structure is not yet supported by plug-ins and this has to be done by the application.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary><a>How to debug VVAS application if there are any issues?</a></summary>

VVAS is based on GStreamer framework. It relies of debugging tools supported by GStreamer framework. For more details, you may refer to `GStreamer Debugging Tools <https://gstreamer.freedesktop.org/documentation/tutorials/basic/debugging-tools.html?gi-language=c>`_.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary><a>How do I check the throughput of VVAS application/pipeline?</a></summary>

Using GStreamer's native fps display mechanism.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary><a>How do I compile and prune the model to be used?</a></summary>

Refer to `Vitis AI 1.4 documentation <https://www.xilinx.com/html_docs/vitis_ai/1_4/zmw1606771874842.html>`_.

.. raw:: html

   </details>
   </br>


.. raw:: html

   <details>
   <summary><a>How do I build plugins?</a></summary>

Refer to :ref:`Building VVAS Plugins and Libraries <build_vvas_plugins_and_libs>`.

.. raw:: html

   </details>
   </br>



.. raw:: html

   <details>
   <summary><a>What if I cannot find the information that i am looking for?</a></summary>

Contact support.

.. raw:: html

   </details>
   </br>
