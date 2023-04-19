.. _vvas_xinfer:


vvas_xinfer
============

The ``vvas_xinfer`` GStreamer plug-in is capable of performing inferencing on video frames/images and generating output in the form of a ``GstInferenceMeta`` object, which is a tree-like structure containing inference results. This metadata is attached to the input GstBuffer. Additionally, this plug-in can perform hardware-accelerated preprocessing operations, such as resize/crop/normalization, on incoming video frames/images before conducting inferencing. The ``vvas_xinfer`` plug-in relies on the ``Vitis-AI`` library for inferencing.

The use of hardware-accelerated preprocessing with the ``vvas_xinfer`` plug-in is optional and requires the presence of the ``image_processing`` kernel in the hardware design. Users may also opt to use software-based preprocessing, which internally uses the ``Vitis-AI`` library.

If hardware-accelerated preprocessing is enabled and the ``vvas_xinfer`` plug-in is not receiving physically contiguous memory, there may be an overhead of copying data into physically contiguous memory before sending it to the preprocessing engine.

Another useful feature of the ``vvas_xinfer`` plug-in is its ability to consolidate the inferencing results of different stages of cascaded inferencing use cases. The plug-in can update/append the new metadata information generated at each stage into the metadata of the previous stages.

For implementation details, please refer to `vvas_xinfer source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/sys/infer>`_

.. figure:: ../../images/vvas_xinfer_blockdiagram.png
   :align: center
   :scale: 80

.. note::

        To ensure smooth operation of multithreaded applications, it is important to create the ML pipeline in such a way that vvas_xinfer instances are sequentially created instead of concurrently by multiple threads.

Input and Output
--------------------

* ML hardware engine supports images in BGR/RGB formats only (depending on model). Though this plug-in can accepts buffers with GRAY8, NV12, BGR, RGB, YUY2, r210, v308, GRAY10_LE32, ABGR, ARGB color formats on input GstPad & output GstPad, actual formats supported may vary depending on the hardware design, and the color formats enabled in the hardware design.

  - In case there is ``image_processing`` kernel in the design, then user may choose to use hardware accelerated pre-processing and/or color space conversion. Make sure ``image_processing`` kernel supports the required color format.
  - In case ``image_processing`` kernel in not there in the design then the input image to ``vvas_xinfer`` plug-in must be in BGR/RGB format (depending on the model requirements) otherwise the results are unexpected.

* ``vvas_xinfer`` attaches the ``GstInferenceMeta`` metadata to the output GstBuffer.. For details about meta data, refer to :ref:`vvas_inference_metadata`

Control Parameters
--------------------

.. list-table:: Control Parameters
   :widths: 20 10 10 10 50
   :header-rows: 1

   * - Property Name
     - Type
     - Range
     - Default
     - Description
   * - infer-config
     - String
     - N/A
     - Null
     - Complete path, including file name, of the inference configuration JSON file
   * - preprocess-config
     - String
     - N/A
     - Null
     - Complete path, including file name, of the pre-processing kernels config JSON file
   * - attach-empty-metadata
     - Boolean
     - True/False
     - True
     - Flag to decide attaching empty metadata strucutre when there is no inference results available for the current image
   * - batch-timeout
     - Unsigned Integer
     - 0 - UINT_MAX
     - 0 (No timeout)
     - time (in milliseconds) to wait when batch is not full, before pushing batch of frames for inference

infer-config json members
-------------------------

+---------------------+--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
| Json key            | Item               | Item description                                                                                                                                     |
+=====================+====================+======================================================================================================================================================+
|                     | Description        | Inference level in cascaded inference use case. e.g. Object detection ML (level-1) followed by object classification (level-2) on detected objects   |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Value type         | Integer                                                                                                                                              |
| inference-level     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Mandatory/Optional | Optional                                                                                                                                             |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Default value      | 1                                                                                                                                                    |
+---------------------+--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Description        | Parameter to enable/disable low-latency mode in vvas_xinfer and it is useful only when inference-level > 1.                                          |
|                     |                    | If enabled, then vvas_xinfer plug-in will not wait for batch-size frames to be accumulated to reduce latency.                                        |
|                     |                    | If disabled, inference engine can work at maximum throughput.                                                                                        |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
| low-latency         | Value type         | Boolean                                                                                                                                              |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Mandatory/Optional | Optional                                                                                                                                             |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Default value      | false                                                                                                                                                |
+---------------------+--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Description        | Maximum number of input frames those can be queued inside the plug-in.                                                                               |
|                     |                    | When low-latency is disabled, vvas_xinfer plug-in will wait for inference-max-queue buffers until batch-size is accumulated                          |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
| inference-max-queue | Value type         | Integer                                                                                                                                              |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Mandatory/Optional | Optional                                                                                                                                             |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Default value      | batch-size                                                                                                                                           |
+---------------------+--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Description        | Attaches output of preprocessing library to GstInferenceMeta to avoid redoing of the preprocessing if required.                                      |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Value type         | Boolean                                                                                                                                              |
| attach-ppe-outbuf   +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Mandatory/Optional | Optional                                                                                                                                             |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Default value      | False                                                                                                                                                |
+---------------------+--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Description        | Kernel object provides information about an VVAS kernel library configuration and kernel library name                                                |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Value type         | JSON Object                                                                                                                                          |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
| kernel              | Mandatory/Optional | Mandatory                                                                                                                                            |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Default value      | None                                                                                                                                                 |
|                     +--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Object Members     | members of kernel JSON object are mentioned below                                                                                                    |
+---------------------+--------------------+------------------------------------------------------------------------------------------------------------------------------------------------------+


infer-config::kernel json members
---------------------------------


+--------------+--------------------+---------------------------------------------------------------------------------------------------------------------+
| JSON key     | Item               |  Description                                                                                                        |
+==============+====================+=====================================================================================================================+
|              | Description        | Inference kernel specific configuration                                                                             |
|              +--------------------+---------------------------------------------------------------------------------------------------------------------+
|              | Value type         | JSON object                                                                                                         |
|              +--------------------+---------------------------------------------------------------------------------------------------------------------+
| config       | Mandatory/Optional | Mandatory                                                                                                           |
|              +--------------------+---------------------------------------------------------------------------------------------------------------------+
|              | Default value      | None                                                                                                                |
|              +--------------------+---------------------------------------------------------------------------------------------------------------------+
|              | Object members     | Contains members specific to inference library. See vvas_xdpuinfer library for more information                     |
+--------------+--------------------+---------------------------------------------------------------------------------------------------------------------+


infer-config::config json members
---------------------------------


+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Parameter           | Type    | Expected Values                         | Default      | Description                                                                                                                                                                                                                                                                                                                                                                                                  |
+=====================+=========+=========================================+==============+==============================================================================================================================================================================================================================================================================================================================================================================================================+
| model-name          | string  | resnet50                                | N/A          | Name string of the machine learning model to be executed. The name string should be same as the name of the directory available in model -path parameter file. If the name of the model ELF file is resnet50.elf, then the model-name is resnet50 in the JSON file. The ELF file present in the specified path model-path of the JSON file.                                                                  |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| model-class         | string  | YOLOV3                                  | N/A          | Class of some model corresponding to model. Some examples are shown below:                                                                                                                                                                                                                                                                                                                                   |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | FACEDETECT                              |              | * **YOLOV3**: yolov3_adas_pruned_0_9, yolov3_voc, yolov3_voc_tf                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | CLASSIFICATION                          |              | * **FACEDETECT**: densebox_320_320, densebox_640_360                                                                                                                                                                                                                                                                                                                                                         |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | SSD                                     |              | * **CLASSIFICATION**: resnet18, resnet50, resnet_v1_50_tf                                                                                                                                                                                                                                                                                                                                                    |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | REFINEDET                               |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | TFSSD                                   |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | YOLOV2                                  |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | VEHICLECLASSIFICATION                   |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | REID                                    |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | SEGMENTATION                            |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | PLATEDETECT                             |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | PLATENUM                                |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | POSEDETECT                              |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | BCC                                     |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | EFFICIENTDETD2                          |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | FACEFEATURE                             |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | FACELANDMARK                            |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | ROADLINE                                |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | ULTRAFAST                               |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         | RAWTENSOR                               |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| model-format        | string  | RGB/BGR                                 | N/A          | Image color format required by model.                                                                                                                                                                                                                                                                                                                                                                        |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| model-path          | string  | ``/usr/share/vitis_ai_library/models/`` | N/A          | Path of the folder where the model to be executed is stored.                                                                                                                                                                                                                                                                                                                                                 |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| vitis-ai-preprocess | Boolean | True/False                              | True         | If vitis-ai-preprocess = true: Normalize with mean/scale through the Vitis AI Library If vitis-ai-preprocess = false: Normalize with mean/scale is performed before calling the vvas_xdpuinfer API's. The Vitis AI library does not perform these operations.                                                                                                                                                |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| batch-size          | Integer | 0 to UINT_MAX                           | N/A          | Number of frames to be processed in a single batch. If not set or is greater than the batch-size supported by model, it is adjusted to the maximum batch-size supported by the model.                                                                                                                                                                                                                        |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| float-feature       | Boolean | True/False                              | False        | This is used for FACEFEATURE class. If float-feature = true: Features are provided as float numbers. If float-feature = false: Features are provided as integers.                                                                                                                                                                                                                                            |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| max-objects         | Integer | 0 to UINT_MAX                           | UINT_MAX     | Maximum number of objects to be detected.                                                                                                                                                                                                                                                                                                                                                                    |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| segoutfactor        | Integer | 0 to UINT_MAX                           | 1            | Multiplication factor for Y8 output to look bright.                                                                                                                                                                                                                                                                                                                                                          |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| seg-out-format      | string  | BGR/GRAY8                               | N/A          | Output color format of segmentation.                                                                                                                                                                                                                                                                                                                                                                         |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| filter-labels       | Array   |                                         | N/A          | Array of comma separated strings to filter objects with certain labels only.                                                                                                                                                                                                                                                                                                                                 |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| performance-test    | Boolean | True/False                              | False        | Enable performance test and corresponding flops per second (f/s) display logs. Calculates and displays the f/s of the standalone DPU after every second.                                                                                                                                                                                                                                                     |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| postprocess-lib-path| string  | /usr/lib/libvvascore_postprocessor.so   | N/A          | Library to post-process tensors. Absolute path of the library has to be given                                                                                                                                                                                                                                                                                                                                |
|                     |         |                                         |              | Embedded: /usr/lib/libvvascore_postprocessor.so                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              | PCIe: /opt/xilinx/vvas/lib/libvvascore_postprocessor.so                                                                                                                                                                                                                                                                                                                                                      |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| debug-level         | Integer | 0 to 3                                  | 1            | Used to enable log levels.                                                                                                                                                                                                                                                                                                                                                                                   |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              | There are four log levels for a message sent by the kernel library code, starting from level 0 and decreasing in severity till level 3 the lowest log-level identifier. When a log level is set, it acts as a filter, where only messages with a log-level lower than it, (therefore messages with an higher severity) are displayed.                                                                        |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              | 0: This is the highest level in order of severity: it is used for messages about critical errors, both hardware and software related.                                                                                                                                                                                                                                                                        |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              | 1: This level is used in situations where you attention is immediately required.                                                                                                                                                                                                                                                                                                                             |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              | 2: This is the log level used for information messages about the action performed by the kernel and output of model.                                                                                                                                                                                                                                                                                         |
|                     |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                     |         |                                         |              | 3: This level is used for debugging.                                                                                                                                                                                                                                                                                                                                                                         |
+---------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+

.. note::
        In case of class type RAWTENSOR, it is mandatory to provide the post processing function name in the json file.


preprocess-config json members
------------------------------

Table 4 preprocess-config json members

+-------------------+--------------------+-------------------------------------------------------------------------------------------------------+
| Json key          | Item               | Item description                                                                                      |
+===================+====================+=======================================================================================================+
|                   | Description        | Location of xclbin which contains scaler IP to program FPGA device based on device-index property     |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Value type         | String                                                                                                |
| xclbin-location   +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Mandatory/Optional | Mandatory                                                                                             |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Default value      | NULL                                                                                                  |
+-------------------+--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Description        | Device index on which scaler IP is present                                                            |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Value type         | Integer                                                                                               |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
| device-index      | Mandatory/Optional | Mandatory in PCIe platforms                                                                           |
|                   |                    | In embedded platforms, device-index is not an applicable option as it is always zero                  |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Default value      | -1 in PCIe platforms                                                                                  |
|                   |                    | 0 in Embedded platforms                                                                               |
+-------------------+--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Description        | Use software/hardware pre-processing.                                                                 |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Value type         | Boolean                                                                                               |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
| software-ppe      | Mandatory/Optional | Optional                                                                                              |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Default value      | FALSE                                                                                                 |
+-------------------+--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Description        | Kernel object provides information about an VVAS library configuration.                               |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Value type         | JSON Object                                                                                           |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
| kernel            | Mandatory/Optional | Mandatory                                                                                             |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Default value      | None                                                                                                  |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Object Members     | members of kernel JSON object are mentioned below                                                     |
+-------------------+--------------------+-------------------------------------------------------------------------------------------------------+

preprocess-config::kernel json members
---------------------------------------

Table 5: preprocess-config::kernel json members


+--------------+--------------------+----------------------------------------------------------------------------+
| JSON key     | Item               | Description                                                                |
+==============+====================+============================================================================+
|              | Description        | Name of the preprocessing kernel. Syntax : "<kernel_name>:<instance_name>" |
|              +--------------------+----------------------------------------------------------------------------+
|              | Value type         | String                                                                     |
| kernel-name  +--------------------+----------------------------------------------------------------------------+
|              | Mandatory/Optional | Mandatory                                                                  |
|              +--------------------+----------------------------------------------------------------------------+
|              | Default value      | NULL                                                                       |
+--------------+--------------------+----------------------------------------------------------------------------+
|              | Description        | preprocess kernel specific configuration                                   |
|              +--------------------+----------------------------------------------------------------------------+
|              | Value type         | JSON object                                                                |
|              +--------------------+----------------------------------------------------------------------------+
| config       | Mandatory/Optional | Mandatory                                                                  |
|              +--------------------+----------------------------------------------------------------------------+
|              | Default value      | None                                                                       |
|              +--------------------+----------------------------------------------------------------------------+
|              | Object members     | Contains members specific to preprocess library                            |
+--------------+--------------------+----------------------------------------------------------------------------+


preprocess-config::config json members
---------------------------------------


+---------------------+---------+-----------------------------------------+--------------+---------------------------------------------------------------------------------------------------------------------------+
| Parameter           | Type    | Expected Values                         | Default      | Description                                                                                                               |
+=====================+=========+=========================================+==============+===========================================================================================================================+
| ppc                 | Integer | 2/4                                     | PCIe : 4     | Pixel per clock supported by a multi- scaler kernel                                                                       |
|                     |         |                                         | Embedded : 2 |                                                                                                                           |
+---------------------+---------+-----------------------------------------+--------------+---------------------------------------------------------------------------------------------------------------------------+
| scaler-type         | string  | letterbox/envelope_cropped/NA           | default      | Type of scaling to be used for resize operation. Some models require resize to be done with aspect-ratio preserved.       |
|                     |         |                                         | resize       | If not set, default res-sizing will be done.                                                                              |
|                     |         |                                         |              | letterbox: letterbox cropping                                                                                             |
|                     |         |                                         |              | envelope_cropped: envelope cropping                                                                                       |
+---------------------+---------+-----------------------------------------+--------------+---------------------------------------------------------------------------------------------------------------------------+
| scaler-horz-align   | string  | left/right/center                       | left         | Used when "scaler-type" = letterbox.                                                                                      |
|                     |         |                                         |              | left: Image will be at the left i.e, padding will be added at the right end of the image.                                 |
|                     |         |                                         |              | right: Image will be at the left i.e, padding will be added at the right end of the image.                                |
|                     |         |                                         |              | center: Image will be at the center i.e, padding will be added at both right and left ends of the image.                  |
+---------------------+---------+-----------------------------------------+--------------+---------------------------------------------------------------------------------------------------------------------------+
| scaler-vert-align   | string  | top/bottom/center                       | top          | Used when "scaler-type" = letterbox.                                                                                      |
|                     |         |                                         |              | top: Image will be at the top i.e, padding will be added at the bottom end of the image.                                  |
|                     |         |                                         |              | bottom: Image will be at the bottom i.e, padding will be added at the top end of the image.                               |
|                     |         |                                         |              | center: Image will be at the center i.e, padding will be added at both top and botoom ends of the image.                  |
+---------------------+---------+-----------------------------------------+--------------+---------------------------------------------------------------------------------------------------------------------------+
| scaler-pad-value    | Integer | 0 - UINT_MAX                            | 0            | pixel value of the padded region in letterbox cropping.                                                                   |
+---------------------+---------+-----------------------------------------+--------------+---------------------------------------------------------------------------------------------------------------------------+
| in-mem-bank         | Integer | 0 - 65535                               | 0            | VVAS input memory bank to allocate memory.                                                                                |
+---------------------+---------+-----------------------------------------+--------------+---------------------------------------------------------------------------------------------------------------------------+
| out-mem-bank        | Integer | 0 - 65535                               | 0            | VVAS output memory bank to allocate memory.                                                                               |
+---------------------+---------+-----------------------------------------+--------------+---------------------------------------------------------------------------------------------------------------------------+

Scaler Types
--------------------

Letterbox
--------------------

The letterbox scaling technique is used to maintain the aspect ratio of an image while resizing it to a specific resolution. This method involves determining the target aspect ratio and scaling the image down to fit within that ratio while preserving its original aspect ratio. The resulting image will have bars (either on the top and bottom or left and right) to fill in the remaining space, allowing the entire image to be visible without cutting off important parts.

For instance, consider an input image of 1920x1080 which needs to be resized to a resolution of 416x234 while preserving the aspect ratio. After resizing, the letterbox method is applied by adding black bars horizontally to the image, resulting in a final resolution of 416x416 pixels.

.. figure:: ../../images/infer_letter_box.png
   :align: center
   :scale: 80

Envelope Cropped
--------------------

Envelope cropped scaling is a digital image processing technique that resizes an image to fit a specific resolution while maintaining its aspect ratio. The algorithm involves several steps:

First, the target aspect ratio is determined by comparing the aspect ratio of the original image to that of the target resolution. Next, the image is scaled down by a factor that preserves its original aspect ratio while ensuring that the smallest side of the image fits within the target resolution. Finally, the image is cropped by removing equal parts from both sides of the image, thereby retaining the central part of the image.

This technique ensures that the input image is resized while preserving its aspect ratio and fitting the target resolution by scaling the image down to ensure that the smallest side fits within the target resolution. However, it may result in cutting off important parts of the image, so the potential impact on the model's performance must be carefully considered.

For example, consider an input image of size 1920x1080 being scaled down to a resolution of 455x256 using the smallest side factor of 256 pixels, which preserves the original image's aspect ratio. Following this, a center crop of 224x224 pixels is taken from the scaled image to achieve a final resolution of 224x224 pixels.

.. figure:: ../../images/infer_example_envelop_crop.png
   :align: center
   :scale: 80

.. note::
        * Not all models require the use of the scaler-type parameter. Some models have specific requirements for image resizing to achieve better inference results. Therefore, it is recommended to use the scaler-type parameter only when necessary, and leave it unset otherwise.
        * bcc uses letterbox scaler-type for re-sizing.
        * efficientnetd2 models use envelope_cropped scaler-type for re-sizing.

.. note::
        * Vitis-AI-Preprocess does not support color format conversion. Therefore, if "vitis-ai-preprocess" is set to true, it is the user's responsibility to provide the frame in the format required by the model.
        * If "vitis-ai-preprocess" is set to false and no preprocess-config is provided, it is necessary to perform pre-processing operations such as normalization and scaling on the frame prior to feeding it to vvas_xinfer. Failure to do so may result in unexpected outcomes.
        * When "vitis-ai-preprocess" is set to true in the infer-config json and a preprocess-config json is also provided, VVAS performs pre-processing using hardware acceleration for improved performance.

.. note::
        Set "device-index" = -1 and "kernel-name" = image_processing_sw:{image_processing_sw_1} when using software-ppe from VVAS.

.. note::
        * If tensors are needed instead of post-processed results, user can set "model-class" = "RAWTENSOR" in the infer-config json file.
        * Users have the option to implement their own post-processing to handle the tensors. For instance, the vvascore_postprocessor library serves as a demonstration of how to create a post-processing library. It should be noted that this is simply an example library for reference purposes, and is not optimized.
        * The ``vvascore_postprocessor`` library only supports yolov3_voc, yolov3_voc_tf, plate_num, densebox_320_320, resnet_v1_50_tf models.

Example Pipelines and Jsons
----------------------------

Single stage inference example
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Below is an example of a simple inference pipeline using YOLOv3. The input for this pipeline is an NV12 YUV file (test.nv12):

The pipeline employs the yolov3_voc_tf model for ML inference. First, a 1920x1080 NV12 frame is fed into the vvas_xinfer plugin. The pre-processor then resizes the frame and converts the color format to RGB, which is required by the model. In addition, mean value subtraction and normalization operations are performed on the frame. The resized and pre-processed frame is then passed to the inference library, which generates the inference predictions. These predictions are then upscaled to the original resolution (1920x1080) and attached to the output buffer.

.. code-block::
  
  gst-launch-1.0 filesrc location=<test.nv12> ! videoparse width=1920 height=1080 format=nv12 ! \
  vvas_xinfer preprocess-config=yolov_preproc.json infer-config=yolov3_voc_tf.json ! fakesink -v

.. code-block::

  {
      "inference-level":1,
      "inference-max-queue":30,
      "attach-ppe-outbuf": false,
      "low-latency":false,
      "kernel" : {
         "config": {
            "batch-size":13,
            "model-name" : "yolov3_voc_tf",
            "model-class" : "YOLOV3",
            "model-format" : "RGB",
            "model-path" : "/usr/share/vitis_ai_library/models/",
            "vitis-ai-preprocess" : false,
            "performance-test" : false,
            "debug-level" : 0
         }
      }
   }

.. code-block::

   {
      "xclbin-location":"/run/media/mmcblk0p1/dpu.xclbin",
      "software-ppe": false,
      "device-index": 0,
      "kernel" : {
         "kernel-name": "image_processing:{image_processing_1}",
         "config": {
            "ppc": 4
         }
      }
   }


2-level inference example
^^^^^^^^^^^^^^^^^^^^^^^^^^^

An example cascade inference (YOLOv3+Resnet18) pipeline which takes NV12 YUV file (test.nv12) as input is described below:

Here the objects detected in level-1 are cropped using ``vvas_xabrscaler`` and fed to ``vascore_dpuinfer`` for further inference.
Refer to jsons in above example for level-1. jsons files for level-2 are provided below.

.. code-block::

  gst-launch-1.0 filesrc location=<test.nv12> ! videoparse width=1920 height=1080 format=nv12 ! \
  vvas_xinfer preprocess-config=yolo_preproc.json infer-config=yolov3_voc_tf.json ! queue ! \
  vvas_xinfer preprocess-config=resnet_preproc.json infer-config=resnet18.json ! fakesink -v

.. code-block::

  {
      "inference-level":2,
      "inference-max-queue":30,
      "attach-ppe-outbuf": false,
      "low-latency":false,
      "kernel" : {
         "config": {
            "batch-size":13,
            "model-name" : "resnet50",
            "model-class" : "CLASSIFICATION",
            "model-format" : "RGB",
            "model-path" : "/usr/share/vitis_ai_library/models/",
            "vitis-ai-preprocess" : false,
            "performance-test" : false,
            "debug-level" : 0
         }
      }
   }

.. code-block::

   {
      "xclbin-location":"/run/media/mmcblk0p1/dpu.xclbin",
      "software-ppe": false,
      "device-index": 0,
      "kernel" : {
         "kernel-name": "image_processing:{image_processing_1}",
         "config": {
            "ppc": 4
         }
      }
   }

.. _raw-tensor-example-label:

Rawtensor example
^^^^^^^^^^^^^^^^^^

An example inference pipeline to get tensors is described below:

The below pipeline performs inference using yolov3_voc_tf model. In the infer-json ``model-class: RAWTENSOR`` indicates that tensors are required by the user instead of post-processed inference results.

.. code-block::

  gst-launch-1.0 filesrc location=<test.nv12> ! videoparse width=1920 height=1080 format=nv12 ! \
  vvas_xinfer preprocess-config=yolov_preproc.json infer-config=yolov3_voc_tf.json ! fakesink -v

.. code-block::

  {
      "inference-level":1,
      "inference-max-queue":30,
      "attach-ppe-outbuf": false,
      "low-latency":false,
      "kernel" : {
         "config": {
            "batch-size":13,
            "model-name" : "yolov3_voc_tf",
            "model-class" : "RAWTENSOR",
            "model-format" : "RGB",
            "model-path" : "/usr/share/vitis_ai_library/models/",
            "vitis-ai-preprocess" : false,
            "performance-test" : false,
            "debug-level" : 0
         }
      }
   }

.. code-block::

   {
      "xclbin-location":"/run/media/mmcblk0p1/dpu.xclbin",
      "software-ppe": false,
      "device-index": 0,
      "kernel" : {
         "kernel-name": "image_processing:{image_processing_1}",
         "config": {
            "ppc": 4
         }
      }
   }


Using the same pipeline described above, if post-processing has to be performed on the tensors, ``postprocess-lib-path`` is added in the infer-config json. Note that the post-processing library used here is only a refernce library and does not support all models.

.. code-block::

  {
      "inference-level":1,
      "inference-max-queue":30,
      "attach-ppe-outbuf": false,
      "low-latency":false,
      "kernel" : {
         "config": {
            "batch-size":13,
            "model-name" : "yolov3_voc_tf",
            "model-class" : "RAWTENSOR",
            "postprocess-lib-path" : "/opt/xilinx/vvas/lib/libvvascore_postprocessor.so",
            "model-format" : "RGB",
            "model-path" : "/usr/share/vitis_ai_library/models/",
            "vitis-ai-preprocess" : false,
            "performance-test" : false,
            "debug-level" : 0
         }
      }
   }

..
  ------------
  
  © Copyright 2023, Advanced Micro Devices, Inc.
  
   MIT License

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:
   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

