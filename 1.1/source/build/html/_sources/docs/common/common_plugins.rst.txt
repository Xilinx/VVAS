###################
Plug-ins
###################

VVAS is based on the GStreamer framework. The two types of VVAS GStreamer plug-ins are custom plug-ins and infrastructure plug-ins. This section describes the VVAS GStreamer plug-ins, their input, outputs, and control parameters. The plug-ins source code is available in the ``vvas-gst-plugins`` folder of the VVAS source tree. This section covers the plug-in that are common for Edge as well as cloud solutions. There are few plug-ins that are specific to Edge/Embedded platforms and are covered in :doc:`Plugins for Embedded platforms <../Embedded/6-embedded-plugins>`. Similarly there are few plug-ins that are specific to Cloud/Data Center platforms and these are covered in :doc:`Plugins for Data Center Platform <../DC/6-DC-plugins>`. The following table lists the VVAS GStreamer plug-ins.

Table 1: GStreamer Plug-ins

.. list-table:: 
   :widths: 20 80
   :header-rows: 1
   
   * - Plug-in Name
     - Functionality
	 
   * - :ref:`vvas_xmetaaffixer`
     - Plug-in to scale and attach metadata to the frame of different resolutions.

   * - :ref:`vvas_xabrscaler`
     - Hardware accelerated scaling and color space conversion.

   * - :ref:`vvas_xmultisrc`
     - A generic infrastructure plug-in: 1 input, N output, supporting transform processing.

   * - :ref:`vvas_xfilter`
     - A generic infrastructure plug-in: 1 input, 1 output, supporting pass-through, in-place, and transform processing.

   * - :ref:`vvas_xinfer`
     - An inference plug-in using vvas_xdpuinfer acceleration software library and attaches inference output as GstInferenceMeta to input buffer. Also, this plug-in does optinal pre-processing required for inference using vvas_xpreprocess acceleration software library.

.. _custom_plugins_label:

**************************
Custom Plug-ins
**************************

There are specific functions, like video decoder, encoder, and meta affixer where the requirements are difficult to implement in an optimized way using highly simplified and generic infrastructure plug-ins framework. Hence, these functions are implemented using custom GStreamer plug-ins. This section covers details about the custom plug-ins.

.. _vvas_xmetaaffixer:


vvas_xmetaaffixer
==================

The metaaffixer plug-in, ``vvas_xmetaaffixer``, is used to scale the incoming metadata information for the different resolutions. A machine learning (ML) operation can be performed on a different frame resolution and color format than the original frame, but the metadata might be associated with the full resolution, original frame. The vvas_metaaffixer has two types of pads, master pads and slave pads. Input pads are pads on request; the number of input pads can be created based on the number of input sources. There is one mandatory master input pad (sink pad) that receives the original/reference metadata. Other input pads are referred to as slave pads. Metadata received on the master sink pad is scaled in relation to the resolution of each of the slave sink pads. The scaled metadata is attached to the buffer going out of the output (source) slave pads. There can be up to 16 slave pads created as required. For implementation details, refer to `vvas_xmetaaffixer source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/gst/metaaffixer>`_

.. figure:: ../images/image5.png


Input and Output
--------------------

This plug-in is format agnostic and can accept any input format. It operates only on the metadata. The vvas_xmetaaffixer plug-in supports the GstInferenceMeta data structure. For details about this structure, refer to the :doc:`VVAS Inference Metadata <A-VVAS-Inference-Metadata>` section.


Control Parameters and Plug-in Properties
--------------------------------------------------------

Table 2: vvas_xmetaaffixer Plug-in Properties

+--------------------+-------------+------------------+-------------+--------------------------------------------------------+
|                    |             |                  |             |                                                        |
| **Property Name**  |   **Type**  |  **Range**       | **Default** |                   **Description**                      |
|                    |             |                  |             |                                                        |
+====================+=============+==================+=============+========================================================+
|    sync            |    Boolean  |  True/False      |    True     | This property is to enable the synchronization         |
|                    |             |                  |             | between master and slave pads buffers.                 |
|                    |             |                  |             | If sync=true is set, then the metadata is scaled       |
|                    |             |                  |             | and attached to buffers on slave pads that have        |
|                    |             |                  |             | matching PTS or PTS falls within frame duration of the |
|                    |             |                  |             | buffer on the master sink pad.                         |
|                    |             |                  |             | If sync=false is set on the element, then the          |
|                    |             |                  |             | metadata is scaled and attached to all the             |
|                    |             |                  |             | buffers on the slave pads. If this option is used,     |
|                    |             |                  |             | there is possibility that the metadata is not          |  
|                    |             |                  |             | suitable for the frames/buffers that are not           |
|                    |             |                  |             | corresponding to the frames/buffers on the master      |
|                    |             |                  |             | pad.                                                   |
+--------------------+-------------+------------------+-------------+--------------------------------------------------------+
|    timeout         |    Int64    |  -1 to           |    2000     | Timeout in millisec.                                   |
|                    |             |  9223372036854   |             |                                                        |
+--------------------+-------------+------------------+-------------+--------------------------------------------------------+


Pad naming syntax
---------------------------

The pad naming syntax is listed, and the following image shows the syntax:

* MetaAffixer master input pad should be named sink_master.

* MetaAffixer master output pad should be named src_master.

* MetaAffixer slave input pad should be named sink_slave_0, sink_slave_1.

* MetaAffixer slave output pad should be named src_slave_0, src_slave_1, src_slave_2.

.. figure:: ../images/image6.png 


Example Pipelines
-----------------------------

This section covers the example pipelines using the metaaffixer plug-in. 

.. code-block::

        gst-launch-1.0 videotestsrc num-buffers=1 \
        ! video/x-raw, width=1920, height=1080, format=NV12 \
        ! queue \
        ! videoconvert \
        ! queue \
        ! ima.sink_master vvas_xmetaaffixer name=ima ima.src_master \
        ! queue \
        ! fakesink videotestsrc num-buffers=1 \
        ! video/x-raw, width=1920, height=1080, format=NV12 \
        ! queue \
        ! videoconvert \
        ! video/x-raw, width=1920, height=1080, format=YUY2 \
        ! ima.sink_slave_0 ima.src_slave_0 \
        ! queue \
        ! fakesink -v


.. _vvas_xabrscaler:


vvas_xabrscaler
================

In adaptive bit rate (ABR) use cases, one video is encoded at different bit rates so that it can be streamed in different network bandwidth conditions without any artifacts. To achieve this, input frame is decoded, resized to different resolutions and then re-encoded. vvas_xabrscaler is a plug-in that takes one input frame and can produce several outputs frames having different resolutions and color formats. The vvas_xabrscaler is a GStreamer plug- in developed to accelerate the resize and color space conversion functionality. For more implementation details, refer to `vvas_xabrscaler source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/sys/abrscaler>`_.

This plug-in supports:

* Single input, multiple output pads

* Color space conversion

* Resize

* Each output pad has independent resolution and color space conversion capability.

.. important:: The `vvas_xabrscaler` plug-in controls the multiscaler kernel. If your application uses this plug-in, then make sure that multi-scaler kernel is included in your hardware design.

.. important:: Make sure that the multi-scaler hardware kernel supports maximum resolution required by your application. 

As a reference, maximum resolution supported by multi-scaler kernel in ``Smart Model Select`` example design can be found in  `multi-scaler kernel config <https://github.com/Xilinx/VVAS/blob/master/vvas-examples/Embedded/smart_model_select/v_multi_scaler_user_config.h#L33>`_

Prerequisite
----------------

This plug-in requires the multiscaler kernel to be available in the hardware design. See :ref:`Multiscaler Kernel <multiscaler-kernel>`

Input and Output
------------------------

This plug-in accepts buffers with the following color format standards:

* RGBx
* YUY2
* r210
* Y410
* NV16
* NV12
* RGB
* v308
* I422_10LE
* GRAY8
* NV12_10LE32
* BGRx
* GRAY10_LE32
* BGRx
* UYVY
* BGR
* RGBA
* BGRA
* I420

.. important:: Make sure that the color formats needed for your application are supported by the multi-scaler hardware kernel. 


As a reference, multi-scaler configuration for ``smart model select`` example design can be found in `multi-scaler configuration <https://github.com/Xilinx/VVAS/blob/master/vvas-examples/Embedded/smart_model_select/v_multi_scaler_user_config.h>`_


Control Parameters and Plug-in Properties
------------------------------------------------

The following table lists the GStreamer plug-in properties supported by the vvas_xabrscaler plug-in.

Table 3: vvas_xabrscaler Plug-in Properties

+--------------------+-------------+---------------+------------------------+------------------+
|                    |             |               |                        |                  |
|  **Property Name** |   **Type**  | **Range**     | **Default**            | **Description**  |
|                    |             |               |                        |                  |
+====================+=============+===============+========================+==================+
| avoid-output-copy  |   Boolean   | true/false    | False                  | Avoid output     |
|                    |             |               |                        | frames copy on   |
|                    |             |               |                        | all source pads  |
|                    |             |               |                        | even when        |
|                    |             |               |                        | downstream does  |
|                    |             |               |                        | not support      |
|                    |             |               |                        | GstVideoMeta     |
|                    |             |               |                        | metadata         |
+--------------------+-------------+---------------+------------------------+------------------+
| enable-pipeline    |    Boolean  |  true/false   | false                  | Enable buffer    |
|                    |             |               |                        | pipelining to    |
|                    |             |               |                        | improve          |
|                    |             |               |                        | performance in   |
|                    |             |               |                        | non zero-copy    |
|                    |             |               |                        | use cases        |
+--------------------+-------------+---------------+------------------------+------------------+
| in-mem-bank        | Unsigned int|  0 - 65535    | 0                      | VVAS input memory|
|                    |             |               |                        | bank to allocate |
|                    |             |               |                        | memory           |
+--------------------+-------------+---------------+------------------------+------------------+
| out-mem-bank       | Unsigned int|  0 - 65535    | 0                      | VVAS o/p memory  |
|                    |             |               |                        | bank to allocate |
|                    |             |               |                        | memory           |
+--------------------+-------------+---------------+------------------------+------------------+
|                    |    string   |    N/A        | ./binary_container_1   | The              |
|  xclbin-location   |             |               | xclbin                 | location of      |
|                    |             |               |                        | xclbin.          |
+--------------------+-------------+---------------+------------------------+------------------+
|                    |    string   |    N/A        |                        | Kernel name      |
| kernel-name        |             |               | v_multi_scaler:        | and              |
|                    |             |               | multi_scaler_1         | instance         |
|                    |             |               |                        | separated        |
|                    |             |               |                        | by a colon.      |
+--------------------+-------------+---------------+------------------------+------------------+
|    dev-idx         |    integer  | 0 to 31       |    0                   | Device index     |
|                    |             |               |                        | This is valid    |
|                    |             |               |                        | only in PCIe/    |
|                    |             |               |                        | Data Center      |
|                    |             |               |                        | platforms.       |
+--------------------+-------------+---------------+------------------------+------------------+
|    ppc             |    integer  | 1, 2, 4       |    2                   | Pixel per        |
|                    |             |               |                        | clock            |
|                    |             |               |                        | supported        |
|                    |             |               |                        | by a multi-      |
|                    |             |               |                        | scaler           |
|                    |             |               |                        | kernel           |
+--------------------+-------------+---------------+------------------------+------------------+
|   scale-mode       |    integer  | 0, 1, 2       |    0                   | Scale algorithm  |
|                    |             |               |                        | to use:          |
|                    |             |               |                        | 0:BILINEAR       |
|                    |             |               |                        | 1:BICUBIC        |
|                    |             |               |                        | 2:POLYPHASE      |
+--------------------+-------------+---------------+------------------------+------------------+
|    coef-load-type  |  integer    | 0 => Fixed    |    1                   | Type of filter   |
|                    |             | 1 => Auto     |                        | Coefficients to  |
|                    |             |               |                        | be used: Fixed   |
|                    |             |               |                        | or Auto          |
|                    |             |               |                        | generated        |
+--------------------+-------------+---------------+------------------------+------------------+
|    num-taps        |  integer    | 6=>6 taps     |    1                   | Number of filter |
|                    |             | 8=>8 taps     |                        | taps to be used  |
|                    |             | 10=>10 taps   |                        | for scaling      |
|                    |             | 12=>12 taps   |                        |                  |
+--------------------+-------------+---------------+------------------------+------------------+
|    alpha-b         |  float      | 0 to 128      |    0                   | Mean subreaction |
|                    |             |               |                        | for blue channel |
+--------------------+-------------+---------------+------------------------+------------------+
|    alpha-g         |  float      | 0 to 128      |    0                   | Mean subreaction |
|                    |             |               |                        | for green channel|
+--------------------+-------------+---------------+------------------------+------------------+
|    alpha-r         |  float      | 0 to 128      |    0                   | Mean subreaction |
|                    |             |               |                        | for red  channel |
+--------------------+-------------+---------------+------------------------+------------------+
|    beta-b          |  float      | 0 to 1        |    1                   | Scaling for blue |
|                    |             |               |                        | channel          |
+--------------------+-------------+---------------+------------------------+------------------+
|    beta-g          |  float      | 0 to 1        |    1                   | scaling for green|
|                    |             |               |                        | channel          |
+--------------------+-------------+---------------+------------------------+------------------+
|    beta-r          |  float      | 0 to 1        |    1                   | scaling for red  |
|                    |             |               |                        | channel          |
+--------------------+-------------+---------------+------------------------+------------------+


Example Pipelines
-------------------------


One input one output
^^^^^^^^^^^^^^^^^^^^^^^^^^

The following example configures vvas_xabrscaler in one input and one output mode. The input to the scaler is 1280 x 720, NV12 frames that are resized to 640 x 360 resolution, and the color format is changed from NV12 to BGR.

.. code-block::

      gst-launch-1.0 videotestsrc num-buffers=100 \
      ! "video/x-raw, width=1280, height=720, format=NV12" \
      ! vvas_xabrscaler xclbin-location="/usr/lib/dpu.xclbin" kernel-name=v_multi_scaler:v_multi_scaler_1 \
      ! "video/x-raw, width=640, height=360, format=BGR" ! fakesink -v


One input multiple output
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following example configures vvas_xabrscaler for one input and three outputs. The input is 1920 x 1080 resolution in NV12 format. There are three output formats:

* 1280 x 720 in BGR format

* 300 x 300 in RGB format

* 640 x 480 in NV12 format

.. code-block::

        gst-launch-1.0 videotestsrc num-buffers=100 \
        ! "video/x-raw, width=1920, height=1080, format=NV12, framerate=60/1" \
        ! vvas_xabrscaler xclbin-location="/usr/lib/dpu.xclbin" kernel-name=v_multi_scaler:v_multi_scaler_1 name=sc sc.src_0 \
        ! queue \
        ! "video/x-raw, width=1280, height=720, format=BGR" \
        ! fakesink sc.src_1 \
        ! queue \
        ! "video/x-raw, width=300, height=300, format=RGB" \
        ! fakesink sc.src_2 \
        ! queue \
        ! "video/x-raw, width=640, height=480, format=NV12" \
        ! fakesink -v

.. _vvas_xinfer:

vvas_xinfer
================

GStreamer inference vvas_xinfer plugin performs inferencing on video frames with the help of Vitis AI library and prepares tree like metadata in GstInferenceMeta object and attaches the same to input GstBuffer. This plugin triggers optional preprocessing (scale/crop & etc.) operations with the help of VVAS scaler kernel library (which is on top of Xilinx's multiscaler IP) on incoming video frames before calling VVAS inference kernel library. vvas_xinfer plugin's input capabilities are influenced by preprocessing library input capabilities and Vitis AI library capabilities (Vitis-AI does software scaling). This plugin disables preprocessing whenever the input resolution & color format is same as inference engine's resolution & color format. If preprocessing is enabled and vvas_xinfer plugin is receiving non-CMA memory frames, then data copy will be made to ensure CMA frames goes to preprocessing engine. The main advantage of this plugin is users/customers can realize inference cascading use cases with ease.

.. figure:: ../images/vvas_xinfer_blockdiagram.png
   :align: center
   :scale: 80


Input and Output
--------------------
  * Accepts buffers with GRAY8/ NV12/ BGR/ RGB/ YUY2/ r210/ v308/ GRAY10_LE32/ ABGR/ ARGB color formats on input GstPad & output GstPad.
  * Attaches GstInferenceMeta metadata to output GstBuffer

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
   * - dynamic-config
     - String
     - N/A
     - Null
     - String contains dynamic json configuration of inference accelration library
   * - infer-config
     - String
     - N/A
     - Null
     - location of the inference kernel library configuration file in json format
   * - preprocess-config
     - String
     - N/A
     - Null
     - location of the kernels config file in json format

infer-config json members
-------------------------

+---------------------+--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Json key            | Item               | Item description                                                                                                                                                           |
+=====================+====================+============================================================================================================================================================================+
|                     | Description        | VVAS libraries repository path to look for kernel libraries by VVAS GStreamer plugin                                                                                       |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Value type         | String                                                                                                                                                                     |
| vvas-library-repo   +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Mandatory/Optional | Optional                                                                                                                                                                   |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Default value      | /usr/lib in Embedded platforms                                                                                                                                             |
|                     |                    | /opt/xilinx/vvas/lib in PCIe platforms                                                                                                                                     |
+---------------------+--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Description        | Inference level in cascaded inference use case. e.g. Object detection ML (level-1) followed by object classification (level-2) on detected objects                         |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Value type         | Integer                                                                                                                                                                    |
| inference-level     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Mandatory/Optional | Optional                                                                                                                                                                   |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Default value      | 1                                                                                                                                                                          |
+---------------------+--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Description        | Parameter to enable/disable low-latency mode in vvas_xinfer and it is useful only when inference-level > 1.                                                                |
|                     |                    | If enabled, then vvas_xinfer plugin will not wait for batch-size frames to be accumulated to reduce latency. If disabled, inference engine can work at maximum throughput. |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| low-latency         | Value type         | Boolean                                                                                                                                                                    |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Mandatory/Optional | Optional                                                                                                                                                                   |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Default value      | false                                                                                                                                                                      |
+---------------------+--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Description        | Maximum number of input frames those can be queued inside the plugin.                                                                                                      |
|                     |                    | When low-latency is disabled, vvas_xinfer plugin will wait for inference-max-queue buffers until batch-size is accumulated                                                 |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| inference-max-queue | Value type         | Integer                                                                                                                                                                    |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Mandatory/Optional | Optional                                                                                                                                                                   |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Default value      | batch-size                                                                                                                                                                 |
+---------------------+--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Description        | Attaches output of preprocessing library to GstInferenceMeta to avoid redoing of the preprocessing if required.                                                            |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Value type         | Boolean                                                                                                                                                                    |
| attach-ppe-outbuf   +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Mandatory/Optional | Optional                                                                                                                                                                   |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Default value      | False                                                                                                                                                                      |
+---------------------+--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Description        | Kernel object provides information about an VVAS kernel library configuration and kernel library name                                                                      |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Value type         | JSON Object                                                                                                                                                                |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| kernel              | Mandatory/Optional | Mandatory                                                                                                                                                                  |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Default value      | None                                                                                                                                                                       |
|                     +--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
|                     | Object Members     | members of kernel JSON object are mentioned below                                                                                                                          |
+---------------------+--------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+

infer-config::kernel json members
---------------------------------

+--------------+--------------------+---------------------------------------------------------------------------------------------------------------------+
| JSON key     | Item               | Description                                                                                                         |
+==============+====================+=====================================================================================================================+
|              | Description        | Name of inference kernel library to be loaded for inferencing                                                       |
|              +--------------------+---------------------------------------------------------------------------------------------------------------------+
|              | Value type         | String                                                                                                              |
| library-name +--------------------+---------------------------------------------------------------------------------------------------------------------+
|              | Mandatory/Optional | Mandatory                                                                                                           |
|              +--------------------+---------------------------------------------------------------------------------------------------------------------+
|              | Default value      | NULL                                                                                                                |
+--------------+--------------------+---------------------------------------------------------------------------------------------------------------------+
|              | Description        | Inference kernel specific configuration                                                                             |
|              +--------------------+---------------------------------------------------------------------------------------------------------------------+
|              | Value type         | JSON object                                                                                                         |
|              +--------------------+---------------------------------------------------------------------------------------------------------------------+
| config       | Mandatory/Optional | Mandatory                                                                                                           |
|              +--------------------+---------------------------------------------------------------------------------------------------------------------+
|              | Default value      | None                                                                                                                |
|              +--------------------+---------------------------------------------------------------------------------------------------------------------+
|              | Object members     | Contains members specific to inference kernel library. See vvas_xdpuinfer acceleration library for more information |
+--------------+--------------------+---------------------------------------------------------------------------------------------------------------------+

preprocess-config json members
------------------------------

Table 5 preprocess-config json members

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
|                   | Description        | VVAS libraries repository path to look for kernel libraries by VVAS GStreamer plugin                  |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Value type         | String                                                                                                |
| vvas-library-repo +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Mandatory/Optional | Optional                                                                                              |
|                   +--------------------+-------------------------------------------------------------------------------------------------------+
|                   | Default value      | /usr/lib in Embedded platforms                                                                        |
|                   |                    | /opt/xilinx/vvas/lib in PCIe platforms                                                                |
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
|                   | Description        | Kernel object provides information about an VVAS kernel library configuration and kernel library name |
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

Table 6: preprocess-config::kernel json members


+--------------+--------------------+----------------------------------------------------------------------------+
| JSON key     | Item               | Description                                                                |
+==============+====================+============================================================================+
|              | Description        | Name of kernel library to be loaded for inferencing                        |
|              +--------------------+----------------------------------------------------------------------------+
|              | Value type         | String                                                                     |
| library-name +--------------------+----------------------------------------------------------------------------+
|              | Mandatory/Optional | Mandatory                                                                  |
|              +--------------------+----------------------------------------------------------------------------+
|              | Default value      | NULL                                                                       |
+--------------+--------------------+----------------------------------------------------------------------------+
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
|              | Object members     | Contains members specific to preprocess kernel library                     |
+--------------+--------------------+----------------------------------------------------------------------------+



* Example infer-json file:


.. code-block::

   {
      "vvas-library-repo": "/usr/lib/",
      "inference-level":1,
      "inference-max-queue":30,
      "attach-ppe-outbuf": false,
      "low-latency":false,
      "kernel" : {
          "library-name":"libvvas_xdpuinfer.so",
          "config": {
              "model-name" : "yolov3_voc_tf",
              "model-class" : "YOLOV3",
              "model-format" : "RGB",
              "batch-size":1,
              "model-path" : "/usr/share/vitis_ai_library/models/",
              "run_time_model" : false,
              "need_preprocess" : false,
              "performance_test" : false,
              "debug_level" : 0
          }
      }
   }


* Example preprocess-config json file:


.. code-block::

   {
      "xclbin-location":"/usr/lib/dpu.xclbin",
      "vvas-library-repo": "/usr/lib",
      "device-index": 0,
      "kernels" :[
          {
              "kernel-name": "v_multi_scaler:v_multi_scaler_1",
              "library-name": "libvvas_xpreprocessor.so",
              "config": {
	         "alpha_r" : 0.0,
	         "alpha_g" : 0.0,
	         "alpha_b" : 0.0,
	         "beta_r" : 0.25,
	         "beta_g" : 0.25,
	         "beta_b" : 0.25,
	         "cascade" : 1,
	         "debug_level" : 0
              }
          }
      ]


.. note::

   When user wants to perform hardware accelerated pre-processing on input frame using ``vvas_xinfer`` plug-in, then the pre-processing parameters provided in preprocess-config json file must be checked for correctness. The pre-processing parameters for the models are provided in the prototxt file for the models. For few models these parameters needs to be modified from what it is in the prototxt file. User must provide these modified values in the preprocess-config json file.
   There are few steps mentioned below to know which models need changes.

To determine the pre-processing parameters for a model, follow the steps mentioned below:

* Get the algorithmic scale vector, i.e. "scale" fields for each channel R,G,B mentioned in the model prototxt file. If the model expects input image in RGB format, then the first field will corresponds to Channel R, next will corresponds to Channel G, and the last one will be for Channel B. Example prototxt file contents are mentioned below:


.. code-block::

   model {
           name: "yolov3_voc_416"
           kernel {
               name: "yolov3_voc_416"
               mean: 0.0
               mean: 0.0
               mean: 0.0
               scale: 0.00390625
               scale: 0.00390625
               scale: 0.00390625
   }


* Calculate the ``inner scale`` value from the "fixpos" field of the "input_tensor" by executing the command at command prompt on the target board in case of Embedded platform.

.. code-block::

   xdputil xmodel -l path-to-xmodel.xmodel

   you can get something like:

   {
    "subgraphs":[
        {
            "name":"subgraph_data",
            "device":"USER"
        },
        {
           "name":"subgraph_conv1_1",
            "device":"DPU",
            "fingerprint":"0x1000020f6014407",
            "DPU Arch":"DPUCZDX8G_ISA0_B4096_MAX_BG2",
            "input_tensor":[
                {
                    "name":"data_fixed",
                    "shape":"[1, 360, 480, 3]",
                    "fixpos":-1
                }
            ],


    inner scale = 2^fixpos
                = 2^-1
                = 0.5


* Multiply the "scale" vector values from prototxt file with the inner scale calculated above. Provide these values in the preprocess-config json file. User need to be careful while assigning the value to the correct channel field.

.. code-block::
   If the model expects input image in RGB format, then the first scale value in prototxt file will corresponds to channel R. So after multiplying with the "inner scale" value, assign this first value to "beta_r" in  preprocess-config json file. Repeat the same for other channels.


* Example Simple inference (YOLOv3) pipeline which takes NV12 YUV file (test.nv12) as input is described below:

.. code-block::
  
  gst-launch-1.0 filesrc location=<test.nv12> ! videoparse width=1920 height=1080 format=nv12 ! \
  vvas_xinfer preprocess-config=yolo_preproc.json infer-config=yolov3_voc.json ! fakesink -v

* Example cascade inference (YOLOv3+Resnet18) pipeline which takes NV12 YUV file (test.nv12) as input is described below:

.. code-block::

  gst-launch-1.0 filesrc location=<test.nv12> ! videoparse width=1920 height=1080 format=nv12 ! \
  vvas_xinfer preprocess-config=yolo_preproc.json infer-config=yolov3_voc.json ! queue ! \
  vvas_xinfer preprocess-config=resnet_preproc.json infer-config=resnet18.json ! fakesink -v

.. _infra_plugins_label:

**********************************************************************
Infrastructure Plug-ins and Acceleration Software Libraries
**********************************************************************

Infrastructure plug-ins are generic plug-ins that interact with the acceleration kernel through a set of APIs exposed by an acceleration software library corresponding to that kernel. Infrastructure plug-ins abstract the core/common functionality of the GStreamer framework (for example: caps negotiation and buffer management).

Table 5: Infrastructure Plug-ins

+----------------------------------------+----------------------------------+
|  **Infrastructure Plug-ins**           |          **Function**            |
|                                        |                                  |
+========================================+==================================+
|    vvas_xfilter                        | Plug-in has one input, one output|
|                                        | Support Transform, passthrough   |
|                                        | and inplace transform operations |
+----------------------------------------+----------------------------------+
|    vvas_xmultisrc                      | Plug-in support one input and    |
|                                        | multiple output pads.            |
|                                        | Support transform operation      |
+----------------------------------------+----------------------------------+

.. note::

        Though one input and one output kernel can be integrated using any of the two infrastructure plug-ins, we recommend using vvas_xfilter plugin for one input and one output kernels.


Acceleration software libraries control the acceleration kernel, like register programming, or any other core logic required to implement the functions. Acceleration software libraries expose a simplified interface that is called by the GStreamer infrastructure plug-ins to interact with the acceleration kernel. The following table lists the acceleration software libraries developed to implement specific functionality. These libraries are used with one of the infrastructure plug-ins to use the functionality a GStreamer-based application. Example pipelines with GStreamer infrastructure plug-ins and acceleration software libraries are covered later in this section.

Table 6: Acceleration Software Libraries

+----------------------------------------+----------------------------------+
|  **Acceleration Software Library**     |          **Function**            |
|                                        |                                  |
+========================================+==================================+
|    vvas_xdpuinfer                      |    Library based on Vitis AI to  |
|                                        |    control DPU kernels for       |
|                                        |    machine learning.             |
+----------------------------------------+----------------------------------+
|    vvas_xboundingbox                   |    Library to draw a bounding    |
|                                        |    box and labels on the frame   |
|                                        |    using OpenCV.                 |
+----------------------------------------+----------------------------------+


The GStreamer infrastructure plug-ins are available in the vvas-gst-plugins repository/ folder. The following section describes each infrastructure plug-in.

.. _vvas_xfilter:


vvas_xfilter
==========================

The GStreamer vvas_xfilter is an infrastructure plug-in that is derived from GstBaseTransform. It supports one input pad and one output pad. The vvas_xfilter efficiently works with hard-kernel/soft-kernel/software (user-space) acceleration software library types as shown in the following figure.

.. figure:: ../images/image8.png 

This plug-in can operate in the following three modes.
  
* **Passthrough:** Useful when the acceleration software library does not need to alter the input buffer.

* **In-place:** Useful when the acceleration software library needs to alter the input buffer.

* **Transform:** In this mode, for each input buffer, a new output buffer is produced.

You must set the mode using the JSON file. Refer to :doc:`JSON File Schema <B-JSON-File-Schema>` for information related to the kernels-config property.

.. figure:: ../images/image9.png 

The vvas_xfilter plug-in takes configuration file as one of the input properties, kernels- config. This configuration file is in JSON format and contains information required by the kernel. During initialization, the vvas_xfilter parses the JSON file and performs the following tasks:

* Finds the VVAS acceleration software library in the path and loads the shared library.

* Understands the acceleration software library type and prepares the acceleration software library handle (VVASKernel) to be passed to the core APIs.



Input and Output
-------------------

The vvas_xfilter accepts the buffers with the following color formats on input GstPad and output GstPad.

* GRAY8
* NV12
* BGR
* RGB
* YUY2
* r210
* v308
* GRAY10_LE32
* ABGR
* ARGB

The formats listed are the Xilinx IP supported color formats. To add other color formats, update the vvas_kernel.h and vvas_xfilter plug-ins.



Control Parameters and Plug-in Properties
--------------------------------------------

The following table lists the GObject properties exposed by the vvas_xfilter. Most of them are only available in PCIe supported platforms.

Table 6: GObject Properties

+---------------------+----------------------------+----------+-----------+-------------+-----------------------------------------------+
|                     |                            |          |           |             |                                               |
|  **Property Name**  |   **Platforms Supported**  | **Type** | **Range** | **Default** |                **Description**                |
|                     |                            |          |           |             |                                               |
|                     |                            |          |           |             |                                               |
+=====================+============================+==========+===========+=============+===============================================+
|                     |                            |          |           |             |                                               |
|  dynamic-config     |    PCIe and Embedded       |   String |    N/A    |     Null    |  JSON formatted string contains kernel        |
|                     |                            |          |           |             |  specific configuration for run time changes  |
|                     |                            |          |           |             |                                               |
+---------------------+----------------------------+----------+-----------+-------------+-----------------------------------------------+
|                     |                            |          |           |             |                                               |
|  dev-idx            |    PCIe only               |  Integer |  0 to 31  |      0      |  Device used for kernel processing,           |
|                     |                            |          |           |             |  xclbin download.                             |
|                     |                            |          |           |             |                                               |
+---------------------+----------------------------+----------+-----------+-------------+-----------------------------------------------+
|                     |                            |          |           |             |                                               |
|  kernels-config     |  PCIe and Embedded         |   String |    N/A    |    NULL     | JSON configuration file path based on VVAS    |
|                     |                            |          |           |             | acceleration software library requirements.   |
|                     |                            |          |           |             | Refer to the :doc:`B-JSON-File-Schema`        |
|                     |                            |          |           |             |                                               |
+---------------------+----------------------------+----------+-----------+-------------+-----------------------------------------------+
|                     |                            |          |           |             |                                               |
| sk-cur-idx          |  PCIe only                 |  Integer | 0 to 31   |    0        | Softkernel current index to be used for       |
|                     |                            |          |           |             | executing job on device.                      |
+---------------------+----------------------------+----------+-----------+-------------+-----------------------------------------------+
| reservation-id      |  PCIe only                 |  Integer | 0 to 1024 |    0        | Reservation ID provided by the Xilinx         |
|                     |                            |          |           |             | resource manager (XRM).                       | 
+---------------------+----------------------------+----------+-----------+-------------+-----------------------------------------------+



JSON Format for vvas_xfilter Plug-in
---------------------------------------

The following table provides the JSON keys accepted by the GStreamer vvas_xfilter plug-in.

Table 7: Root JSON Object Members

+----------------------+----------------------+-----------------------------------+
|    **JSON Key**      |    **Item**          |    **Item Description**           |
|                      |                      |                                   |
+======================+======================+===================================+
|    xclbin-location   |    Description       |    The location of                |
|                      |                      |    the xclbin that                |
|                      |                      |    is used to                     |
|                      |                      |    program the FPGA               |
|                      |                      |    device.                        |
+----------------------+----------------------+-----------------------------------+
|                      |    Value type        |    String                         |
+----------------------+----------------------+-----------------------------------+
|                      |    Mandatory or      | Conditionally                     |
|                      |    optional          | mandatory:                        |
|                      |                      |                                   |
|                      |                      | -  If the VVAS                    |
|                      |                      |    acceleration                   |
|                      |                      |    software                       |
|                      |                      |    library is                     |
|                      |                      |    developed for                  |
|                      |                      |    hard-kernel IP                 |
|                      |                      |    and                            |
|                      |                      |    soft-kernel,                   |
|                      |                      |    then the                       |
|                      |                      |    xclbin-location                |
|                      |                      |    is mandatory.                  |
|                      |                      |                                   |
|                      |                      | -  If the VVAS                    |
|                      |                      |    acceleration                   |
|                      |                      |    software                       |
|                      |                      |    library is                     |
|                      |                      |    developed for                  |
|                      |                      |    a software                     |
|                      |                      |    library (e.g.,                 |
|                      |                      |    OpenCV), then                  |
|                      |                      |    the xclbin-location            |
|                      |                      |    is not required                |
+----------------------+----------------------+-----------------------------------+
|                      | Default value        | NULL                              |
+----------------------+----------------------+-----------------------------------+
| vvas-library-repo    | Description          | This is the VVAS                  |
|                      |                      | libraries repository              |
|                      |                      | path to look for                  |
|                      |                      | acceleration                      |
|                      |                      | software libraries                |
|                      |                      | by the VVAS                       |
|                      |                      | GStreamer plug-in.                |
+----------------------+----------------------+-----------------------------------+
|                      | Value type           | String                            |
+----------------------+----------------------+-----------------------------------+
|                      | Mandatory or         | Optional                          |
|                      | optional             |                                   |
+----------------------+----------------------+-----------------------------------+
|                      | Default value        | /usr/lib                          |
+----------------------+----------------------+-----------------------------------+
| element-mode         | Description          | GStreamer element                 |
|                      |                      | mode to operate.                  |
|                      |                      | Based on your                     |
|                      |                      | requirement, choose               |
|                      |                      | one of the following              |
|                      |                      | modes:                            |
|                      |                      |                                   |
|                      |                      | 1. Passthrough: In                |
|                      |                      |    this mode,                     |
|                      |                      |    element does                   |
|                      |                      |    not want to                    |
|                      |                      |    alter the                      |
|                      |                      |    input buffer.                  |
|                      |                      |                                   |
|                      |                      | 2. Inplace: In this               |
|                      |                      |    mode, element                  |
|                      |                      |    wants to alter                 |
|                      |                      |    the input                      |
|                      |                      |    buffer itself                  |
|                      |                      |    instead of                     |
|                      |                      |    producing new                  |
|                      |                      |    output                         |
|                      |                      |    buffers.                       |
|                      |                      |                                   |
|                      |                      | 3. Transform: In                  |
|                      |                      |    this mode,                     |
|                      |                      |    element                        |
|                      |                      |    produces a                     |
|                      |                      |    output buffer                  |
|                      |                      |    for each input                 |
|                      |                      |    buffer.                        |
+----------------------+----------------------+-----------------------------------+
|                      | Value type           | Enum                              |
+----------------------+----------------------+-----------------------------------+
|                      | Mandatory or         | Mandatory                         |
|                      | optional             |                                   |
+----------------------+----------------------+-----------------------------------+
| kernels              | Description          | This is the array of              |
|                      |                      | kernel objects. Each              |
|                      |                      | kernel object                     |
|                      |                      | provides information              |
|                      |                      | about an VVAS                     |
|                      |                      | acceleration                      |
|                      |                      | software library                  |
|                      |                      | configuration. The                |
|                      |                      | vvas_xfilter only                 |
|                      |                      | takes the first                   |
|                      |                      | kernel object in the              |
|                      |                      | kernel array.                     |
+----------------------+----------------------+-----------------------------------+
|                      | Value type           | Array of objects                  |
+----------------------+----------------------+-----------------------------------+
|                      | Mandatory or         | Mandatory                         |
|                      | optional             |                                   |
+----------------------+----------------------+-----------------------------------+
|                      | Default value        | None                              |
+----------------------+----------------------+-----------------------------------+
|                      | Minimum value        | 1                                 |
+----------------------+----------------------+-----------------------------------+
|                      | Maximum value        | 10                                |
+----------------------+----------------------+-----------------------------------+
|                      | Object members       | Refer to :ref:`Kernel JSON Object |
|                      |                      | <kernel-json-object>`             |
+----------------------+----------------------+-----------------------------------+

The information in the following table is specific to the kernel chosen.

.. _kernel-json-object:

Table 8: Kernel JSON Object Members

+----------------------+----------------------+--------------------------+
|    **JSON Key**      |    **Item**          |    **Item Description**  |
|                      |                      |                          |
+======================+======================+==========================+
| library-name         | Description          | The name of the VVAS     |
|                      |                      | acceleration             |
|                      |                      | software library to      |
|                      |                      | be loaded by the         |
|                      |                      | VVAS GStreamer           |
|                      |                      | plug-ins. The            |
|                      |                      | absolute path of the     |
|                      |                      | kernel library is        |
|                      |                      | formed by the            |
|                      |                      | pre-pending              |
|                      |                      | vvas-library-repo        |
|                      |                      | path.                    |
+----------------------+----------------------+--------------------------+
|                      | Value type           | String                   |
+----------------------+----------------------+--------------------------+
|                      | Mandatory or         | Mandatory                |
|                      | optional             |                          |
+----------------------+----------------------+--------------------------+
|                      | Default value        | None                     |
+----------------------+----------------------+--------------------------+
| kernel-name          | Description          | The name of the          |
|                      |                      | IP/kernel in the         |
|                      |                      | form of <kernel          |
|                      |                      | name>:<instance          |
|                      |                      | name>                    |
+----------------------+----------------------+--------------------------+
|                      | Value type           | String                   |
+----------------------+----------------------+--------------------------+
|                      | Mandatory or         | Optional                 |
|                      | optional             |                          |
+----------------------+----------------------+--------------------------+
|                      | Default value        | None                     |
+----------------------+----------------------+--------------------------+
| config               | Description          | Holds the                |
|                      |                      | configuration            |
|                      |                      | specific to the VVAS     |
|                      |                      | acceleration             |
|                      |                      | software library.        |
|                      |                      | VVAS GStreamer           |
|                      |                      | plug-ins do not          |
|                      |                      | parse this JSON          |
|                      |                      | object, instead it       |
|                      |                      | is sent to the           |
|                      |                      | acceleration             |
|                      |                      | software library.        |
+----------------------+----------------------+--------------------------+
|                      | Value type           | Object                   |
+----------------------+----------------------+--------------------------+
|                      | Mandatory or         | Optional                 |
|                      | optional             |                          |
+----------------------+----------------------+--------------------------+
|                      | Default value        | None                     |
+----------------------+----------------------+--------------------------+
| softkernel           | Description          | Contains the             |
|                      |                      | information specific     |
|                      |                      | to the soft-kernel.      |
|                      |                      | This JSON object is      |
|                      |                      | valid only for the       |
|                      |                      | PCIe based               |
|                      |                      | platforms.               |
+----------------------+----------------------+--------------------------+
|                      | Value type           | Object                   |
+----------------------+----------------------+--------------------------+
|                      | Mandatory or         | Mandatory if kernel      |
|                      | optional             | library is written       |
|                      |                      | for soft-kernel.         |
+----------------------+----------------------+--------------------------+
|                      | Default value        | None                     |
+----------------------+----------------------+--------------------------+
|                      | Members              | Not required for         |
|                      |                      | embedded platforms.      |
+----------------------+----------------------+--------------------------+

For ``vvas_xfilter`` implementation details, refer to `vvas_xfilter source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/sys/filter>`_


Example JSON Configuration Files
-----------------------------------


JSON File for Vitis AI API-based VVAS Acceleration Software Library
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following JSON file is for pure software-based acceleration, it does not involve any kernel for acceleration. However, the Vitis AI API based DPU is a special case, where the DPU hardware kernel is controlled by Vitis AI. The acceleration software library calls the Vitis AI API calls and it is implemented as a pure software acceleration software library. There is no need to provide the path of the xclbin in the JSON file.

.. code-block::

         {
            "vvas-library-repo": "/usr/lib/",
            "element-mode":"inplace",
            "kernels" :[
               {
                  "library-name":"libvvas_xdpuinfer.so",
                  "config": {
                     "model-name" : "densebox_320_320",
                     "model-class" : "FACEDETECT",
                     "model-format": ""BGR",
                     "model-path" : "/usr/share/vitis_ai_library/models/",
                     "run_time_model" : false,
                     "need_preprocess" : true,
                     "performance_test" : true,
                     "max_num" : -1,
                     "prob_cutoff" : 0.0,
                     "debug_level" : 1
                  }
               }
            ]
         }


JSON File for a Hard Kernel
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following JSON file uses vvas_xfilter to control multi-scaler IP (hard-kernel). The acceleration software library accessing the register map is libvvas_xcrop.so.

.. code-block::

      {
         "xclbin-location":"/usr/lib/dpu.xclbin",
         "vvas-library-repo": "/usr/lib/",
         "element-mode":"passthrough",
         "kernels" :[
             {
                "kernel-name":"v_multi_scaler:v_multi_scaler_1",
                "library-name":"libvvas_xcrop.so",
                "config": {
                }
             }
         ]
      }


Example Pipelines
--------------------------

This section covers the GStreamer example pipelines using the ``vvas_xfilter`` infrastructure plug-in and several acceleration software libraries. This section covers the bounding box functionality and the machine learning functions using the vvas_xfilter plug-in.

* The bounding box functionality is implemented in the ``vvas_xboundingbox`` acceleration software library that is controlled by the ``vvas_xfilter`` plug-in.

* Machine learning using the DPU is implemented by the ``vvas_xdpuinfer`` acceleration software library that is called by the ``vvas_xfilter`` plug-in.

.. _vvas_xboundingbox:

Bounding Box Example
============================

This section describes how to draw a bounding box and label information using the VVAS infrastructure plug-in ``vvas_xfilter`` and the ``vvas_xboundingbox`` acceleration software library. The vvas_xboundingbox interprets machine learning inference results from the vvas_xdpuinfer acceleration software library and uses an OpenCV library to draw the bounding box and label on the identified objects.

For ``vvas_xboundingbox`` implementation details, refer to `vvas_xboundingbox source code <https://github.com/Xilinx/VVAS/tree/master/vvas-accel-sw-libs/vvas_xboundingbox>`_

.. figure:: ../images/X24998-vvas-xboundingbox.png


Prerequisites
-----------------------------------------

There are a few requirements before start running bounding box examples. Make sure these prerequisites are met.

* Installation of OpenCV: The vvas_xboundingbox uses OpenCV for the graphics back-end library to draw the boxes and labels. Install the libopencv-core-dev package (the preferred version is 3.2.0 or later).

.. _json-vvas_xboundingbox:


JSON File for vvas_xboundingbox
-------------------------------------------

This section describes the JSON file format and configuration parameters for the bounding box acceleration software library. The GStreamer vvas_xfilter plug-in used in the inplace mode. Bounding box and labels are drawn on identified objects on the incoming frame. Bounding box functionality is implemented in the libvvas_xboundingbox.so acceleration software library.

The following example is of a JSON file to pass to the vvas_xfilter.

.. code-block::

      {
         "xclbin-location":"/usr/lib/dpu.xclbin",
         "vvas-library-repo": "/usr/local/lib/vvas",
         "element-mode":"inplace",
         "kernels" :[
            {
               "library-name":"libvvas_xboundingbox.so",
               "config": {
                  "font_size" : 0.5,
                  "font" : 3,
                  "thickness" : 2,
                  "debug_level" : 2,
                  "label_color" : { "blue" : 0, "green" : 0, "red" : 0 },
                  "label_filter" : [ "class", "probability" ],
                  "classes" : [
                     {
                        "name" : "car",
                        "blue" : 255,
                        "green" : 0,
                        "red" : 0
                     },
                     {
                        "name" : "person",
                        "blue" : 0,
                        "green" : 255,
                        "red" : 0
                     },
                     {  
                        "name" : "bicycle",
                        "blue" : 0,
                        "green" : 0,
                        "red" : 255
                     }
                  ]
               }  
            }
         ]
      }

Various configuration parameters of the bounding box acceleration software library are described in the following table.

Table 9: vvas_xboundingbox Parameters

+----------------------+----------------------+----------------------+
|    **Parameter**     | **Expected Values**  |    **Description**   |
|                      |                      |                      |
+======================+======================+======================+
|    debug_level       |    0:                |    Enables the log   |
|                      |    LOG_LEVEL_ERROR   |    levels. There are |
|                      |                      |    four log levels   |
|                      |    1:                |    listed in the     |
|                      |    LOG_LEVEL_WARNING |    expected values   |
|                      |                      |    column.           |
|                      |    2: LOG_LEVEL_INFO |                      |
|                      |                      |                      |
|                      |    3:                |                      |
|                      |    LOG_LEVEL_DEBUG   |                      |
+----------------------+----------------------+----------------------+
|    font              |    0 to 7            |    Font for the      |
|                      |                      |    label text. 0:    |
|                      |                      |    Hershey Simplex   |
|                      |                      |                      |
|                      |                      |    1: Hershey Plain  |
|                      |                      |                      |
|                      |                      |    2: Hershey Duplex |
|                      |                      |                      |
|                      |                      |    3: Hershey        |
|                      |                      |    Complex           |
|                      |                      |                      |
|                      |                      |    4: Hershey        |
|                      |                      |    Triplex           |
|                      |                      |                      |
|                      |                      |    5: Hershey        |
|                      |                      |    Complex Small 6:  |
|                      |                      |    Hershey Script    |
|                      |                      |    Simplex 7:        |
|                      |                      |    Hershey Script    |
|                      |                      |    Complex           |
+----------------------+----------------------+----------------------+
|    font_size         |    0.5 to 1          |    Font fraction     |
|                      |                      |    scale factor that |
|                      |                      |    is multiplied by  |
|                      |                      |    the font-specific |
|                      |                      |    base size.        |
+----------------------+----------------------+----------------------+
| thickness            |    Integer 1 to 3    | The thickness of the |
|                      |                      | line that makes up   |
|                      |                      | the rectangle.       |
|                      |                      | Negative values,     |
|                      |                      | like -1, signify     |
|                      |                      | that the function    |
|                      |                      | draws a filled       |
|                      |                      | rectangle.           |
|                      |                      |                      |
|                      |                      | The recommended      |
|                      |                      | value is between 1   |
|                      |                      | and 3.               |
+----------------------+----------------------+----------------------+
| label_color          |    { "blue" : 0,     | The color of the     |
|                      |    "green" : 0,      | label is specified.  |
|                      |    "red" : 0 }       |                      |
+----------------------+----------------------+----------------------+
| label_filter         |    [ "class",        | This field indicates |
|                      |    "probability" ]   | that all information |
|                      |                      | printed is the label |
|                      |                      | string. Using        |
|                      |                      | "class" alone adds   |
|                      |                      | the ML               |
|                      |                      | classification name. |
|                      |                      | For example, car,    |
|                      |                      | person, etc.         |
|                      |                      |                      |
|                      |                      | The addition of      |
|                      |                      | "probability" in the |
|                      |                      | array adds the       |
|                      |                      | probability of a     |
|                      |                      | positive object      |
|                      |                      | identification.      |
+----------------------+----------------------+----------------------+
| classes              |    { "name" : "car", | This is a filtering  |
|                      |                      | option when using    |
|                      |    "blue" : 255,     | the                  |
|                      |    "green" :         | vvas_xboundingbox.   |
|                      |                      | The bounding box is  |
|                      |    0, "red" : 0 }    | only drawn for the   |
|                      |                      | classes that are     |
|                      |                      | listed in this       |
|                      |                      | configuration. Other |
|                      |                      | classes are ignored. |
|                      |                      | For instance, if     |
|                      |                      | "car", "person",     |
|                      |                      | "bicycle" is         |
|                      |                      | entered under        |
|                      |                      | "classes", then the  |
|                      |                      | bounding box is only |
|                      |                      | drawn for these      |
|                      |                      | three classes, and   |
|                      |                      | other classes like   |
|                      |                      | horse, motorbike,    |
|                      |                      | etc. are ignored.    |
|                      |                      |                      |
|                      |                      | The expected value   |
|                      |                      | columns show an      |
|                      |                      | example of how each  |
|                      |                      | class should be      |
|                      |                      | described. All       |
|                      |                      | objects in this      |
|                      |                      | example, by class,   |
|                      |                      | are drawn using the  |
|                      |                      | color combination    |
|                      |                      | listed.              |
|                      |                      |                      |
|                      |                      | The class names in   |
|                      |                      | this list matches the|
|                      |                      | class names assigned |
|                      |                      | by the               |
|                      |                      | vvas_xdpuinfer.      |
|                      |                      | Otherwise, the       |
|                      |                      | bounding box is not  |
|                      |                      | drawn.               |
|                      |                      |                      |
|                      |                      | For face detect,     |
|                      |                      | keep the "classes"   |
|                      |                      | array empty.         |
+----------------------+----------------------+----------------------+
| display_level        |  Integer 0 to N      | display bounding box |
|                      |  0 => all levels     | of one particular    |
|                      |  N => specific level | level or all levels  |
+----------------------+----------------------+----------------------+

An example of using a bounding box along with the machine learning plug-in is shown in the :doc:`Multi Channel ML <../Embedded/Tutorials/MultiChannelML>` Tutorial.


.. _vvas_xdpuinfer:

Machine Learning Example
===================================

This section discusses how machine learning solutions can be implemented using the VVAS infrastructure ``vvas_xfilter`` plug-in and the ``vvas_xdpuinfer`` acceleration software library.

.. figure:: ../images/image11.png

The vvas_xdpuinfer is the acceleration software library that controls the DPU through the Vitis AI interface. The vvas_xdpuinfer does not modify the contents of the input buffer. The input buffer is passed to the Vitis AI model library that generates the inference data. This inference data is then mapped into the VVAS meta data structure and attached to the input buffer. The same input buffer is then pushed to the downstream plug-in.

For ``vvas_xdpuinfer`` implementation details, refer to `vvas_xdpuinfer source code <https://github.com/Xilinx/VVAS/tree/master/vvas-accel-sw-libs/vvas_xdpuinfer>`_


Prerequisites
---------------------------------------------

There are a few requirements to be met before you can start running the machine learning examples.


Model Information
---------------------------------------------

The model directory name must match with the ELF and prototxt files inside the model directory. The model directory should contain:

* model-name.elf/model-name.xmodel and model-name.prototxt file.

* label.json file containing the label information, if the models generate labels.

The following is an example of the model directory (yolov2_voc), which contains the yolov2_voc.xmodel and yolov2_voc.prototxt files along with the label.json file.

.. figure:: ../images/model-directory.png

 
xclbin Location
---------------------------------------------
   
By default, the Vitis AI interface expects xclbin to be located at /usr/lib/ and the xclbin is called dpu.xclbin. Another option is to use the environment variable XLNX_VART_FIRMWARE to change the path and the corresponding path can be updated in the JSON file. That is, export XLNX_VART_FIRMWARE=/where/your/dpu.xclbin.


Input Image
---------------------------------------------

The vvas_xdpuinfer works with raw BGR and RGB images as required by the model. Make sure you have specified correct color format in model-format field in json file. The exact resolution of the image to vvas_xdpuinfer must be provided, it is expected by the model. There is a performance loss if a different resolution of the BGR image is provided to the vvas_xdpuinfer, because resizing is done on the CPU inside the Vitis AI library.

.. _json-vvas-dpuinfer:


JSON File for vvas_xdpuinfer
---------------------------------------------

The following table shows the JSON file format and configuration parameters for vvas_xdpuinfer.

Table 10: JSON File for vvas_xdpuinfer

+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Parameter         | Type    | Expected Values                         | Default      | Description                                                                                                                                                                                                                                                                                                                                                                                                  |
+===================+=========+=========================================+==============+==============================================================================================================================================================================================================================================================================================================================================================================================================+
| xclbin-location   | string  | This field is not needed for dpuinfer   | NULL         | By default, Vitis AI expects xclbin to be located at /usr/lib/ and xclbin is called "dpu.xclbin".                                                                                                                                                                                                                                                                                                            |
|                   |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         |                                         |              | The environment variable XLNX_VART_FIRMWARE could also be used to change the path. For example, export XLNX_VART_FIRMWARE=/where/your/dpu.xclbin. Alternatively, you may also provide the xclbin path in ``/etc/vart.conf``. In this release, ``/etc/vart.conf`` has "/media/sd-mmcblk0p1/dpu.xclbin"                                                                                                        |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| vvas-library-repo | string  | ``/usr/local/lib/vvas/``                | ``usr/lib/`` | This is the path where the vvas_xfilter will search the acceleration software library. The kernel name is specified in the library-name parameter of the JSON file.                                                                                                                                                                                                                                          |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| element-mode      | string  | inplace                                 | None         | Because the input buffer is not modified by the ML operation, but the metadata generated out of an inference operation needs to be added/appended to the input buffer, the GstBuffer is writable. The vvas_xfilter is configured in inplace mode                                                                                                                                                             |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| kernels           | N/A     | N/A                                     | N/A          | The JSON tag for starting the kernel specific configurations.                                                                                                                                                                                                                                                                                                                                                |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| kernel-name       | string  | N/A                                     | NULL         | The name and instance of a kernel separated by “:”                                                                                                                                                                                                                                                                                                                                                           |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| library-name      | string  | N/A                                     | NULL         | Acceleration software library name for the kernel. It is appended to the vvas-l ibrary-repo for an absolute path.                                                                                                                                                                                                                                                                                            |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| config            | N/A     | N/A                                     | N/A          | The JSON tag for kernel-specific configurations depending on the acceleration software library.                                                                                                                                                                                                                                                                                                              |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| model-name        | string  | resnet50                                | N/A          | Name string of the machine learning model to be executed. The name string should be same as the name of the directory available in model -path parameter file. If the name of the model ELF file is resnet50.elf, then the model-name is resnet50 in the JSON file. The ELF file present in the specified path model-path of the JSON file.                                                                  |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| model-class       | string  | YOLOV3                                  | N/A          | Class of some model corresponding to model. Some examples are shown below:                                                                                                                                                                                                                                                                                                                                   |
|                   |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         | FACEDETECT                              |              | * **YOLOV3**: yolov3_adas_pruned_0_9, yolov3_voc, yolov3_voc_tf                                                                                                                                                                                                                                                                                                                                              |
|                   |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         | CLASSIFICATION                          |              | * **FACEDETECT**: densebox_320_320, densebox_640_360                                                                                                                                                                                                                                                                                                                                                         |
|                   |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         | SSD                                     |              | * **CLASSIFICATION**: resnet18, resnet50, resnet_v1_50_tf                                                                                                                                                                                                                                                                                                                                                    |
|                   |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         | REFINEDET                               |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         | TFSSD                                   |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         | YOLOV2                                  |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| model-format      | string  | RGB/BGR                                 | N/A          | Image color format required by model.                                                                                                                                                                                                                                                                                                                                                                        |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| model-path        | string  | ``/usr/share/vitis_ai_library/models/`` | N/A          | Path of the folder where the model to be executed is stored.                                                                                                                                                                                                                                                                                                                                                 |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| run_time_model    | Boolean | True/False                              | False        | If there is a requirement to change the ML model at the frame level, then set this flag to true. If this parameter is set to true then vvas_xdpuinfer will read the model name and class from the incoming input metadata and execute the same model found in the path specified in the model-path. The model-name and model-class parameter of the JSON file are not required when enabling this parameter. |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| need_preprocess   | Boolean | True/False                              | True         | If need_preprocess = true: Normalize with mean/scale through the Vitis AI Library If need_preprocess = false: Normalize with mean/scale is performed before feeding the frame to vvas_xdpuinfer. The Vitis AI library does not perform these operations.                                                                                                                                                     |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| performance_test  | Boolean | True/False                              | False        | Enable performance test and corresponding flops per second (f/s) display logs. Calculates and displays the f/s of the standalone DPU after every second.                                                                                                                                                                                                                                                     |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| debug_level       | integer | 0 to 3                                  | 1            | Used to enable log levels.                                                                                                                                                                                                                                                                                                                                                                                   |
|                   |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         |                                         |              | There are four log levels for a message sent by the kernel library code, starting from level 0 and decreasing in severity till level 3 the lowest log-level identifier. When a log level is set, it acts as a filter, where only messages with a log-level lower than it, (therefore messages with an higher severity) are displayed.                                                                        |
|                   |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         |                                         |              | 0: This is the highest level in order of severity: it is used for messages about critical errors, both hardware and software related.                                                                                                                                                                                                                                                                        |
|                   |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         |                                         |              | 1: This level is used in situations where you attention is immediately required.                                                                                                                                                                                                                                                                                                                             |
|                   |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         |                                         |              | 2: This is the log level used for information messages about the action performed by the kernel and output of model.                                                                                                                                                                                                                                                                                         |
|                   |         |                                         |              |                                                                                                                                                                                                                                                                                                                                                                                                              |
|                   |         |                                         |              | 3: This level is used for debugging.                                                                                                                                                                                                                                                                                                                                                                         |
+-------------------+---------+-----------------------------------------+--------------+--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+



Model Parameters
---------------------------------------------

The Vitis AI library provides a way to read model parameters by reading the configuration file. It facilitates uniform configuration management of model parameters. The configuration file is located in the model-path of the JSON file with [model_name].prototxt. These parameters are model specific. For more information on model parameters, refer to the Vitis AI Library User Guide (`UG1354 <http://www.xilinx.com/cgi-bin/docs/rdoc?t=vitis_ai%3Bv%3Dlatest%3Bd%3Dug1354-xilinx-ai-sdk.pdf>`__).


Example GStreamer Pipelines
---------------------------------------------

This section describes a few example GStreamer pipelines.


Classification Example Using Resnet50
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following pipeline performs ML using a Resnet50 model. DPU configuration uses kernels- config = ./json_files/kernel_resnet50.json for the vvas_xdpuinfer. The output of the vvas_xfilter is passed to fakesink along with the metadata.

.. figure:: ../images/example-using-resnet50-model.png 

The GStreamer command for the example pipeline:

.. code-block::

      gst-launch-1.0 filesrc location="<PATH>/001.bgr" blocksize=150528 numbuffers=1 
      ! videoparse width=224 height=224 framerate=30/1 format=16 
      ! vvas_xfilter name="kernel1" kernels-config="<PATH>/kernel_resnet50.json" 
      ! fakesink
  
The JSON file for the vvas_xdpuinfer to execute ``resnet50`` model based classification pipeline is described below.

.. code-block::

        {
           "vvas-library-repo": "/usr/local/lib/vvas/",
           "element-mode":"inplace",
           "kernels" :[
              {
                 "library-name":"libvvas_xdpuinfer.so",
                 "config": {
                    "model-name" : "resnet50",
                    "model-class" : "CLASSIFICATION",
                    "model-format : "BGR"
                    "model-path" : "/usr/share/vitis_ai_library/models/",
                    "run_time_model" : false,
                    "need_preprocess" : true,
                    "performance_test" : true,
                    "debug_level" : 2
                 }
              }
           ]
        }


.. note::
        If "need_preprocess" = false, then pre-processing operations like, Normalization, scaling must be
        performed on the frame before feeding to vvas_xfilter/vvas_xdpuinfer otherwise results may not be as expected.


.. _vvas_xmultisrc:

vvas_xmultisrc
==============================

The GStreamer vvas_xmultisrc plug-in can have one input pad and multiple-output pads. This single plug-in supports multiple acceleration kernels, each controlled by a separate acceleration software library.

For ``vvas_xmultisrc`` implementation details, refer to `vvas_xmultisrc source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/sys/multisrc>`_

.. figure:: ../images/vvas_xmultisrc.png


Input and Output
--------------------------------

Input and output accept buffers with the following color formats on input GstPad and output GstPad.

* GRAY8

* NV12

* BGR

* RGB

* YUY2

* r210

* v308

* GRAY10_LE32

* ABGR

* ARGB

The formats listed are the Xilinx IP supported color formats. To add other color formats, update the vvas_kernel.h and vvas_xfilter plug-ins.


Control Parameters and Plug-in Properties
----------------------------------------------------

Table 11: Plug-in Properties

+--------------------+-------------+-------------+-------------+-------------------------------------------+
|                    |             |             |             |                                           |
|  **Property Name** |  **Type**   |  **Range**  | **Default** |         **Description**                   |
|                    |             |             |             |                                           |
+====================+=============+=============+=============+===========================================+
| kconfig            |    String   |    N/A      |    NULL     | Path of the JSON configuration file based |
|                    |             |             |             | on the VVAS acceleration software library |
|                    |             |             |             | requirements. For further information,    |
|                    |             |             |             | refer to :doc:`B-JSON-File-Schema`        |
|                    |             |             |             |                                           |
+--------------------+-------------+-------------+-------------+-------------------------------------------+
| dynamic-config     |  String     |    N/A      |    NULL     |                                           |
+--------------------+-------------+-------------+-------------+-------------------------------------------+


JSON File for vvas_xmultisrc
---------------------------------------

This section covers the format of JSON file/string to be passed to the vvas_xmultisrc plug-in.


Example JSON File
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following example file describes how to specify two kernels that are being controlled by the single instance of the vvas_xmultisrc plug-in. Modify this json file as per your kernels and acceleration software library. The next section describes each field in this example file.

.. code-block::

      {
         "xclbin-location":"/usr/lib/binary_1.xclbin",
         "vvas-library-repo": "/usr/lib/",
         "kernels" :[
            {
               "kernel-name":"resize:resize_1", <------------------ kernel 1
               "library-name":"libvvas_xresize.so",
               "config": {
                  x : 4,
                  y : 7
               }
            }
            {
               "kernel-name":"cvt_rgb:cvt_rgb_1", <-------------- kernel 2
               "library-name":"libcvt_bgr.so",
               "config": {
                  name = "xilinx",
                  value = 98.34
               }
            }
         ]
      }

Table 12: JSON Properties

+--------------------+-------------+-------------+-------------+------------------------------------------+
|                    |             |             |             |                                          |
|  **Property Name** |  **Type**   |  **Range**  | **Default** |         **Description**                  |
|                    |             |             |             |                                          |
+====================+=============+=============+=============+==========================================+
| xclbin-location    |    String   |    N/A      |    NULL     | The path of xclbin including the xclbin  |
|                    |             |             |             | name. The plug-in downloads this xclbin  |
|                    |             |             |             | and creates an XRT handle for memory     |
|                    |             |             |             | allocation and programming kernels.      |
|                    |             |             |             |                                          |
+--------------------+-------------+-------------+-------------+------------------------------------------+
| vvas-library-repo  |    String   |    N/A      |   /usr/lib  | The library path for the VVAS repository |
|                    |             |             |             | for all the acceleration software        |
|                    |             |             |             | libraries.                               |
+--------------------+-------------+-------------+-------------+------------------------------------------+
| kernels            |    N/A      |    N/A      |    N/A      | The JSON tag for starting the kernel     |
|                    |             |             |             | specific configurations.                 |
+--------------------+-------------+-------------+-------------+------------------------------------------+
| kernel-name        |    String   |    N/A      |    NULL     | Name and instance of a kernel separated  |
|                    |             |             |             | by ":" as mentioned in xclbin.           |
+--------------------+-------------+-------------+-------------+------------------------------------------+
| library-name       |    String   |    N/A      |    NULL     | The acceleration software library name   |
|                    |             |             |             | for the kernel. This is appended to      |
|                    |             |             |             | vvas-library-repo for an absolute path.  |
+--------------------+-------------+-------------+-------------+------------------------------------------+
| config             |    N/A      |    N/A      |    N/A      | The JSON tag for kernel specific         |
|                    |             |             |             | configurations that depends on the       |
|                    |             |             |             | acceleration software library.           |
+--------------------+-------------+-------------+-------------+------------------------------------------+


Source Pad Naming Syntax
^^^^^^^^^^^^^^^^^^^^^^^^^^^                  

For single output pad naming is optional. For multiple pads, the source pads names shall be as mentioned below, assuming the name of the plug-in as `sc`.

sc.src_0, sc.src_1 .....sc.src_n


Example Pipelines
---------------------


Single Output Pad
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following example demonstrates the vvas_xmultisrc plug-in configured for one input and one output. A test video pattern is generated by the videotestrc plug-in and passed to the vvas_xmultisrc plug-in. Depending on the kernel being used, vvas_xmultisrc uses kernel.json to configure the kernel for processing the input frame before passing it to the fakesink.

.. code-block::

      gst-launch-1.0 videotestsrc 
      ! "video/x-raw, width=1280, height=720, format=BGR" 
      ! vvas_xmultisrc kconfig="/root/jsons/<kernel.json>" 
      ! "video/x-raw, width=640, height=360, format=BGR" 
      ! fakesink -v

The following is an example kernel.json file having `mean_value` and `use_mean` as kernel configuration parameters. Modify this as per your kernel requirements.

.. code-block::

      {
         "xclbin-location": "/usr/lib/dpu.xclbin",
         "vvas-library-repo": "/usr/lib/vvas",
         "kernels": [
            {
               "kernel-name": "<kernel-name>",
               "library-name": "libvvas_xresize_bgr.so",
               "config": {
               "use_mean": 1,
               "mean_value": 128
               }
            }
         ]
      }

GstVvasBufferPool
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The GStreamer VVAS buffer pool holds the pool of video frames allocated using the GStreamer allocator object. It is derived from the GStreamer base video buffer pool object (GstVideoBufferPool).

The VVAS buffer pool:

* Allocates buffers with stride and height alignment requirements. (e.g., the video codec unit (VCU) requires the stride to be aligned with 256 bytes and the height aligned with 64 bytes)

* Provides a callback to the GStreamer plug-in when the buffer comes back to the pool after it is used.

The following API is used to create the buffer pool.

.. code-block::

      GstBufferPool *gst_vvas_buffer_pool_new (guint stride_align, guint
      elevation_align)

      Parameters:
         stride_align - stride alignment required
         elevation_align - height alignment required

      Return:
         GstBufferPool object handle

Plug-ins can use the following API to set the callback function on the VVAS buffer pool and the callback function is called when the buffer arrives back to the pool after it is used.

.. code-block::

      Buffer release callback function pointer declaration:
      typedef void (*ReleaseBufferCallback)(GstBuffer *buf, gpointer user_data);

      void gst_vvas_buffer_pool_set_release_buffer_cb (GstVvasBufferPool *xpool,
      ReleaseBufferCallback release_buf_cb, gpointer user_data)

      Parameters:
         xpool - VVAS buffer pool created using gst_vvas_buffer_pool_new
         release_buf_cb - function pointer of callback
         user_data - user provided handle to be sent as function argument while
      calling release_buf_cb()

      Return:
          None


GstVvasAllocator
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The GStreamer VVAS allocator object is derived from an open source GstAllocator object designed for allocating memory using XRT APIs. The VVAS allocator is the backbone to the VVAS framework achieving zero-copy (wherever possible).


Allocator APIs
---------------------------------

GStreamer plug-in developers can use the following APIs to interact with the VVAS allocator. To allocate memory using XRT, the GStreamer plug-ins and buffer pools require the GstAllocator object provided by the following API.

.. code-block::

      GstAllocator* gst_vvas_allocator_new (guint dev_idx, gboolean need_dma)

      Parameters:
         dev_idx - FPGA Device index on which memory is going to allocated
         need_dma - will decide memory allocated is dmabuf or not

      Return:
         GstAllocator handle on success or NULL on failure

..note:: In PCIe platforms, the DMA buffer allocation support is not available. This means that the need_dma argument to gst_vvas_allocator_new() API must be false.

Use the following API to check if a particular GstMemory object is allocated using GstVvasAlloctor.

.. code-block::

      gboolean gst_is_vvas_memory (GstMemory *mem)

      Parameters:
         mem - memory to be validated

      Return:
         true if memory is allocated using VVAS Allocator or false


When GStreamer plug-ins are interacting with hard-kernel IP or soft-kernel, the plug-ins need physical memory addresses on an FPGA using the following API.

.. code-block::

      guint64 gst_vvas_allocator_get_paddr (GstMemory *mem)

      Parameters:
         mem - memory to get physical address

      Return:
         valid physical address on FPGA device or 0 on failure

      Use the following API when plug-ins need an XRT buffer object (BO) corresponding to an VVAS memory object.

.. code-block::

      guint gst_vvas_allocator_get_bo (GstMemory *mem)

      Parameters:
         mem - memory to get XRT BO

      Return:
         valid XRT BO on success or 0 on failure



JSON Schema
==========================================

This section covers the JSON schema for the configuration files used by the infrastructure plug-ins. For more details, refer to :doc:`JSON Schema <B-JSON-File-Schema>`


VVAS Inference Meta Data
=====================================

This section covers details about inference meta data structure used to store meta data. For more details, refer to :doc:`VVAS Inference Meta Data <A-VVAS-Inference-Metadata>`
