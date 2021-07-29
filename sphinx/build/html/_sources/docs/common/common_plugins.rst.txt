###################
Plug-ins
###################

VVAS is based on the GStreamer framework. TThe two types of VVAS GStreamer plug-ins are custom plug-ins and infrastructure plug-ins.This section describes the VVAS GStreamer plug-ins, their input, outputs, and control parameters. The plug-ins source code is available in the ``ivas-gst-plugins`` folder of the VVAS source tree. This section covers the plug-is that are common for Edge as well as cloud solutions. There are few plug-ins that are pecific to Edge/Embedded platforms and are covered in :doc:`Plugins for Embedded platforms <../Embedded/6-embedded-plugins>`. Similarly there are few plug-ins that are specific to Cloud/Data Center platforms and these are covered in :doc:`Plugins for Data Center Platform <../DC/6-DC-plugins>`. The following table lists the GStreamer plug-ins.

Table 1: GStreamer Plug-ins

.. list-table:: 
   :widths: 20 80
   :header-rows: 1
   
   * - Plug-in Name
     - Functionality
	 
   * - :ref:`ivas_xmetaaffixer`
     - Plug-in to scale and attach metadata to the frame of different resolutions.

   * - :ref:`ivas_xabrscaler`
     - Hardware accelerated scaling and color space conversion.

   * - :ref:`ivas_xmultisrc`
     - A generic infrastructure plug-in: 1 input, N output, supporting transform processing.

   * - :ref:`ivas_xfilter`
     - A generic infrastructure plug-in: 1 input, 1 output, supporting pass-through, in-place, and transform processing.


.. _custom_plugins_label:

**************************
Custom Plug-ins
**************************

There are specific functions, like video decoder, encoder, and meta affixer where the requirements are difficult to implement in an optimized way using highly simplified and generic infrastructure plug-ins framework. Hence, these functions are implemented using custom GStreamer plug-ins. This section covers details about the custom plug-ins.

.. _ivas_xmetaaffixer:


MetaAffixer
==========================

The metaaffixer plug-in is used to scale the incoming metadata information for the different resolutions. A machine learning (ML) operation can be performed on a different frame resolution and color format than the original frame, but the metadata might be associated with the full resolution, original frame. The ivas_metaaffixer has two types of pads, master pads and slave pads. Input pads are pads on request, the number of input pads can be created based on the number of input sources. There is one mandatory master input pad (sink pad) that receives the original/reference metadata. Other input pads are referred to as slave pads. Metadata received on the master sink pad is scaled in relation to the resolution of each of the slave sink pads. The scaled metadata is attached to the buffer going out of the output (source) slave pads. There can be up to 16 slave pads created as required.

.. figure:: ../images/image5.png


Input and Output
--------------------

This plug-in is format agnostic and can accept any input format. It operates only on the metadata. The ivas_xmetaaffixer plug-in supports the GstInferenceMeta data structure. For details about this structure, refer to the :doc:`VVAS Inference Metadata <A-IVAS-Inference-Metadata>` section.


Control Parameters and Plug-in Properties
--------------------------------------------------------

Table 2: ivas_xmetaaffixer Plug-in Properties

+--------------------+-------------+-------------+-------------+--------------------------------------------------------+
|                    |             |             |             |                                                        |
| **Property Name**  |   **Type**  |  **Range**  | **Default** |                   **Description**                      |
|                    |             |             |             |                                                        |
+====================+=============+=============+=============+========================================================+
|    sync            |    Boolean  |  True/False |    True     | This property is to enable the synchronization         |
|                    |             |             |             | between master and slave pads buffers.                 |
|                    |             |             |             | If sync=true is set, then the metadata is scaled       |
|                    |             |             |             | and attached to only those buffers on slave pads       |
|                    |             |             |             | that have matching PTS with the buffer on the          |
|                    |             |             |             | master sink pad.                                       |
|                    |             |             |             | If sync=false is set on the element, then the          |
|                    |             |             |             | metadata is scaled and attached to all the             |
|                    |             |             |             | buffers on the slave pads. If this option is used,     |
|                    |             |             |             | there is possibility that the metadata is not          |  
|                    |             |             |             | suitable for the frames/buffers that are not           |
|                    |             |             |             | corresponding to the frames/buffers on the master      |
|                    |             |             |             | pad.                                                   |
+--------------------+-------------+-------------+-------------+--------------------------------------------------------+


Pad naming syntax
---------------------------
The pad naming syntax is listed and the following image shows the syntax:

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
        ! ima.sink_master ivas_xmetaaffixer name=ima ima.src_master \
        ! queue \
        ! fakesink videotestsrc num-buffers=1 \
        ! video/x-raw, width=1920, height=1080, format=NV12 \
        ! queue \
        ! videoconvert \
        ! video/x-raw, width=1920, height=1080, format=YUY2 \
        ! ima.sink_slave_0 ima.src_slave_0 \
        ! queue \
        ! fakesink -v


.. _ivas_xabrscaler:


ivas_xabrscaler
================

In adaptive bit rate (ABR) use cases, one video is encoded at different bit rates so that it can be streamed in different network bandwidth conditions without any artifacts. To achieve this, input frame is decoded, resized to different resolutions and then re-encoded. ivas_xabrscaler is a plug-in that takes one input frame and can produce several outputs frames having different resolutions and color formats. The ivas_xabrscaler is a GStreamer plug- in developed to accelerate the resize and color space conversion functionality. This plug-in supports:

* Single input, multiple output pads

* Color space conversion

* Resize

* Each output pad has independent resolution and color space conversion capability.

.. important:: *The ivas_xabrscaler plug-in controls the multiscaler kernel, it must be included in your hardware design. See the `Multiscaler Kernel <#multiscaler-kernel>`__section*.


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

.. important:: *Make sure that the color formats provided to this plug-in are supported by the multi-scaler hardware kernel.*


Control Parameters and Plug-in Properties
------------------------------------------------

The following table lists the GStreamer plug-in properties supported by the ivas_xabrscaler plug-in.

Table 3: ivas_xabrscaler Plug-in Properties

+--------------------+-------------+-----------+-------------+------------------+
|                    |             |           |             |                  |
|  **Property Name** |   **Type**  | **Range** | **Default** | **Description**  |
|                    |             |           |             |                  |
+====================+=============+===========+=============+==================+
|                    |    string   |    N/A    | ./binary    | The              |
|  xclbin-location   |             |           | _contain    | location of      |
|                    |             |           | er_1.xclbin | xclbin.          |
+--------------------+-------------+-----------+-------------+------------------+
|                    |    string   |    N/A    |             | Kernel name      |
| kernel-name        |             |           | v_multi_    | and              |
|                    |             |           | scaler:     | instance         |
|                    |             |           | multi_      | separated        |
|                    |             |           | scaler_1    | by a colon.      |
+--------------------+-------------+-----------+-------------+------------------+
|    dev-idx         |    integer  | 0 to 31   |    0        | Device index     |
|                    |             |           |             | This is valid    |
|                    |             |           |             | only in PCIe/    |
|                    |             |           |             | Data Center      |
|                    |             |           |             | platforms.       |
+--------------------+-------------+-----------+-------------+------------------+
|    ppc             |    integer  | 1, 2, 4   |    2        | Pixel per        |
|                    |             |           |             | clock            |
|                    |             |           |             | supported        |
|                    |             |           |             | by a multi-      |
|                    |             |           |             | scaler           |
|                    |             |           |             | kernel           |
+--------------------+-------------+-----------+-------------+------------------+
|   scale-mode       |    integer  | 0, 1, 2   |    0        | Scale algorithm  |
|                    |             |           |             | to use:          |
|                    |             |           |             | 0:BILINEAR       |
|                    |             |           |             | 1:BICUBIC        |
|                    |             |           |             | 2:POLYPHASE      |
+--------------------+-------------+-----------+-------------+------------------+
|    coef-load-type  |  integer    | 0 => Fixed|    1        | Type of filter   |
|                    |             | 1 => Auto |             | Coefficients to  |
|                    |             |           |             | be used: Fixed   |
|                    |             |           |             | or Auto          |
|                    |             |           |             | generated        |
+--------------------+-------------+-----------+-------------+------------------+
|    num-taps        |  integer    | 6=>6 taps |    1        | Number of filter |
|                    |             | 8=>8 taps |             | taps to be ussed |
|                    |             |10=>10 taps|             | for scaling      |
|                    |             |12=>12 taps|             |                  |
+--------------------+-------------+-----------+-------------+------------------+


Example Pipelines
-------------------------


One input one output
^^^^^^^^^^^^^^^^^^^^^^^^^^

The following example configures ivas_xabrscaler in one input and one output mode. The input to the scaler is 1280 x 720, NV12 frames that are resized to 640 x 360 resolution, and the color format is changed
from NV12 to BGR.

.. code-block::

      gst-launch-1.0 videotestsrc num-buffers=100 \
      ! "video/x-raw, width=1280, height=720, format=NV12" \
      ! ivas_xabrscaler xclbin-location="/usr/lib/dpu.xclbin" kernel-name=v_multi_scaler:v_multi_scaler_1 \
      ! "video/x-raw, width=640, height=360, format=BGR" ! fakesink -v


One input multiple output
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following example configures ivas_xabrscaler for one input and three outputs. The input is 1920 x 1080 resolution in NV12 format. There are three output formats:

* 1280 x 720 in BGR format

* 300 x 300 in RGB format

* 640 x 480 in NV12 format

.. code-block::

        gst-launch-1.0 videotestsrc num-buffers=100 \
        ! "video/x-raw, width=1920, height=1080, format=NV12, framerate=60/1" \
        ! ivas_xabrscaler xclbin-location="/usr/lib/dpu.xclbin" kernel-name=v_multi_scaler:v_multi_scaler_1 name=sc sc.src_0 \
        ! queue \
        ! "video/x-raw, width=1280, height=720, format=BGR" \
        ! fakesink sc.src_1 \
        ! queue \
        ! "video/x-raw, width=300, height=300, format=RGB" \
        ! fakesink sc.src_2 \
        ! queue \
        ! "video/x-raw, width=640, height=480, format=NV12" \
        ! fakesink -v


.. _infra_plugins_label:

**********************************************************************
Infrastructure Plug-ins and Acceleration Software Libraries
**********************************************************************

Infrastructure plug-ins are generic plug-ins that interact with the acceleration kernel through a set of APIs exposed by an acceleration software library corresponding to that kernel. Infrastructure plug-ins abstract the core/common functionality of the GStreamer framework (for example: caps negotiation and buffer management).

Table 5: Acceleration Software Libraries

+----------------------------------------+----------------------------------+
|  **Infrastructure Plug-ins**           |          **Function**            |
|                                        |                                  |
+========================================+==================================+
|    ivas_xfilter                        | Plug-in has one input, one output|
|                                        | Support Transform, passthrough   |
|                                        | and inplace transform operations |
+----------------------------------------+----------------------------------+
|    ivas_xmultisrc                      | Plug-in support one input and    |
|                                        | multiple output pads.            |
|                                        | Support transform operation      |
+----------------------------------------+----------------------------------+

Acceleration software libraries control the acceleration kernel, like register programming, or any other core logic required to implement the functions. Acceleration software libraries expose a simplified interface that is called by the GStreamer infrastructure plug-ins to interact with the acceleration kernel. The following table lists the acceleration software libraries developed to implement specific functionality. These libraries are used with one of the infrastructure plug-ins to use the functionality a GStreamer-based application. Example pipelines with GStreamer infrastructure plug-ins and acceleration software libraries are covered later in this chapter.

Table 6: Acceleration Software Libraries

+----------------------------------------+----------------------------------+
|  **Acceleration SoftwareLibrary**      |          **Function**            |
|                                        |                                  |
+========================================+==================================+
|    ivas_xdpuinfer                      |    Library based on Vitis AI to  |
|                                        |    control DPU kernels for       |
|                                        |    machine learning.             |
+----------------------------------------+----------------------------------+
|    ivas_xboundingbox                   |    Library to draw a bounding    |
|                                        |    box and labels on the frame   |
|                                        |    using OpenCV.                 |
+----------------------------------------+----------------------------------+


The GStreamer infrastructure plug-ins are available in the ivas-gst-plugins repository/ folder. The following section describes each infrastructure plug-in.

.. _ivas_xfilter:


ivas_xfilter
==========================

The GStreamer ivas_xfilter is an infrastructure plug-in that is derived from GstBaseTransform. It supports one input pad and one output pad. The ivas_xfilter efficiently works with hard-kernel/soft-kernel/software (user-space) acceleration software library types as shown in the following figure.

.. figure:: ../images/image8.png 

This plug-in can operate in the following three modes.
  
* **Passthrough:** Useful when the acceleration software library does not need to alter the input buffer.

* **In-place:** Useful when the acceleration software library needs to alter the input buffer.

* **Transform:** In this mode, for each input buffer, a new output buffer is produced.

You must set the mode using the JSON file. Refer to :doc:`JSON File Schema <B-JSON-File-Schema>` for information related to the kernels-config property.

.. figure:: ../images/image9.png 

The ivas_xfilter plug-in takes configuration file as one of the input properties, kernels- config. This configuration file is in JSON format and contains information required by the kernel. During initialization, the ivas_xfilter parses the JSON file and performs the following tasks:

* Finds the VVAS acceleration software library in the path and loads the shared library.

* Understands the acceleration software library type and prepares the acceleration software library handle (IVASKernel) to be passed to the core APIs.



Input and Output
-------------------

The ivas_xfilter accepts the buffers with the following color formats on input GstPad and output GstPad.

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

The formats listed are the Xilinx IP supported color formats. To add other color formats, update the ivas_kernel.h and ivas_xfilter plug-ins.



Control Parameters and Plug-in Properties
--------------------------------------------

The following table lists the GObject properties exposed by the ivas_xfilter. Most of them are only available in PCIe supported platforms.

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



JSON Format for ivas_xfilter Plug-in
---------------------------------------

The following table provides the JSON keys accepted by the GStreamer ivas_xfilter plug-in.

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
| ivas-library-repo    | Description          | This is the VVAS                  |
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
|                      |                      | ivas_xfilter only                 |
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
|                      |                      | ivas-library-repo        |
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



Example JSON Configuration Files
-----------------------------------


JSON File for Vitis AI API-based VVAS Acceleration Software Library
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following JSON file is for pure software-based acceleration, it does not involve any kernel for acceleration. However, the Vitis AI API based DPU is a special case, where the DPU hardware kernel is controlled by Vitis AI. The acceleration software library calls the Vitis AI API calls and it is implemented as a pure software acceleration software library. There is no need to provide the path of the xclbin in the JSON file.

.. code-block::

         {
         "ivas-library-repo": "/usr/lib/",
         "element-mode":"inplace",
         "kernels" :[
            {
               "library-name":"libivas_xdpuinfer.so",
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

The following JSON file uses ivas_xfilter to control multi-scaler IP (hard-kernel). The acceleration software library accessing the register map is libivas_xcrop.so.

.. code-block::

      {
         "xclbin-location":"/usr/lib/dpu.xclbin",
         "ivas-library-repo": "/usr/lib/",
         "element-mode":"passthrough",
         "kernels" :[
         {
            "kernel-name":"v_multi_scaler:v_multi_scaler_1",
            "library-name":"libivas_xcrop.so",
            "config": {
            }
         }
         ]
      }


Example Pipelines
--------------------------

This section covers the GStreamer example pipelines using the ivas_xfilter infrastructure plug-in and several acceleration software libraries. This section covers the bounding box functionality and the machine learning functions using the ivas_xfilter plug-in.

* The bounding box functionality is implemented in the ivas_xboundingbox acceleration software library that is controlled by the ivas_xfilter plug-in.

* Machine learning using the DPU is implemented by the ivas_xdpuinfer acceleration software library that is called by the ivas_xfilter plug-in.

.. _ivas_xboundingbox:

Bounding Box Using the ivas_xboundingbox Acceleration Software Library
================================================================================

This section describes how to draw a bounding box and label information using the VVAS infrastructure plug-in ivas_xfilter and the ivas_xboundingbox acceleration software library. The ivas_xboundingbox interprets machine learning inference results from the ivas_xdpuinfer acceleration software library and uses an OpenCV library to draw the bounding box and label on the identified objects.

.. figure:: ../images/X24998-ivas-xboundingbox.png


Prerequisites
-----------------------------------------

There are a few requirements before start running bounding box examples. Make sure these prerequisites are met.

* Installation of OpenCV: The ivas_xboundingbox uses OpenCV for the graphics back-end library to draw the boxes and labels. Install the libopencv-core-dev package (the preferred version is 3.2.0 or later).

.. _json-ivas_xboundingbox:


JSON File for ivas_xboundingbox
-------------------------------------------

This section describes the JSON file format and configuration parameters for the bounding box acceleration software library. The GStreamer ivas_xfilter plug-in used in the inplace mode. Bounding box and labels are drawn on identified objects on the incoming frame. Bounding box functionality is implemented in the libivas_xboundingbox.so acceleration software library.

The following example is of a JSON file to pass to the ivas_xfilter.

.. code-block::

      {
      "xclbin-location":"/usr/lib/dpu.xclbin",
      "ivas-library-repo": "/usr/local/lib/ivas",
      "element-mode":"inplace",
      "kernels" :[
         {
            "library-name":"libivas_xboundingbox.so",
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

Table 9: ivas_xboundingbox Parameters

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
|                      |                      | For example car,     |
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
|                      |    "green" :         | ivas_xboundingbox.   |
|                      |                      | The bounding box is  |
|                      |    0, "red" : 0 }    | only drawn for the   |
|                      |                      | classes that are     |
|                      |                      | listed in this       |
|                      |                      | configuration. Other |
|                      |                      | classes are ignored. |
|                      |                      | For instance, if     |
|                      |                      | "car", "person",     |
|                      |                      | "bicycle" are        |
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
|                      |                      | columns shows an     |
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
|                      |                      | this list match the  |
|                      |                      | class names assigned |
|                      |                      | by the               |
|                      |                      | ivas_xdpuinfer.      |
|                      |                      | Otherwise, the       |
|                      |                      | bounding box is not  |
|                      |                      | drawn.               |
|                      |                      |                      |
|                      |                      | For face detect,     |
|                      |                      | keep the "classes"   |
|                      |                      | array empty.         |
+----------------------+----------------------+----------------------+

An example of using a bounding box along with the machine learnin plug-in is shown in the :doc:`Multi Channel ML <../Embedded/Tutorials/MultiChannelML>` Tutoial.


.. _ivas_xdpuinfer:

Machine Learning Using the ivas_xdpuinfer Plug-in
===============================================================================

This section discusses how machine learning can be implemented using the VVAS infrastructure ivas_xfilter plug-in and the ivas_xdpuinfer acceleration software library. Details about the ivas_xfilter plug-in and ivas_xdpuinfer acceleration software library are beyond the scope of this section.

.. figure:: ../images/image11.png

The ivas_xdpuinfer is the acceleration software library that controls the DPU through the Vitis AI interface. The ivas_xdpuinfer does not modify the contents of the input buffer. The input buffer is passed to the Vitis AI model library that generates the inference data. This inference data is then mapped into the VVAS metameta structure and attached to the input buffer. The same input buffer is then pushed to the downstream plug-in.


Prerequisites
---------------------------------------------

There are a few requirements to be met before you can start running the machine learning examples.


Model Information
---------------------------------------------

The model directory name must match with the ELF and prototxt files inside the model directory. The model directory should contain:

* model-name.elf/model-name.xmodel and model-name.prototxt file.

* label.json file containing the label information, if the models generate labels.

The following is an example of the model directory (yolov3_voc_tf), which contains the yolov3_voc_tf.elf/yolov3_voc_tf.xmodel and yolov3_voc_tf.prototxt files along with the label.json file.

.. figure:: ../images/model-directory.png

 
xclbin Location
---------------------------------------------
   
By default, the Vitis AI interface expects xclbin to be located at /usr/lib/ and the xclbin is called dpu.xclbin. Another option is to use the environment variable XLNX_VART_FIRMWARE to change the path and the corresponding path can be updated in the JSON file. That is, export XLNX_VART_FIRMWARE=/where/your/dpu.xclbin.


Input Image
---------------------------------------------

The ivas_xdpuinfer works with raw BGR and RGB images as required by the model. Make sure you have specified correct color format in model-format field in json file. The exact resolution of the image to ivas_xdpuinfer must be provided, it is expected by the model. There is a performance loss if a different resolution of the BGR image is provided to the ivas_xdpuinfer, because resizing is done on the CPU inside the Vitis AI library.

.. _json-ivas-dpuinfer:


JSON File for ivas_xdpuinfer
---------------------------------------------

The following table shows the JSON file format and configuration parameters for ivas_xdpuinfer.

Table 10: JSON File for ivas_xdpuinfer

+----------------+-------------+--------------------+-------------+-----------------------------------------------+
|                |             |                    |             |                                               |
| **Parameter**  |  **Type**   | **Expected Values**| **Default** |          **Description**                      |
|                |             |                    |             |                                               |
+================+=============+====================+=============+===============================================+
| xclb           |    string   |    /usr/lib        |    NULL     | By default, Vitis AI expects xclbin           |
| in-location    |             | /dpu.xclbin        |             | to be located at /usr/lib/ and xclbin         |
|                |             |                    |             | is called dpu.xclbin.                         |
|                |             |                    |             | The environment variable XLNX_VART_FIRMWARE   |
|                |             |                    |             | could also be used to change the path and     |
|                |             |                    |             | the corresponding path can be updated in the  |
|                |             |                    |             | JSON file.                                    |
|                |             |                    |             | e.g., export XLNX_VART_FIRMWARE=/where/your/  |
|                |             |                    |             | dpu.xclbin                                    |
|+---------------+-------------+--------------------+-------------+-----------------------------------------------+
| ivas-l         |    string   |                    |             | This is the path where the ivas_xfilter will  |
| ibrary-repo    |             |   /usr/loca        |   /usr/lib/ | search the acceleration software library.     |
|                |             | l/lib/ivas/        |             | The kernel name is specified in the           |
|                |             |                    |             | library-name parameter of the JSON file.      |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
| element-mode   |    string   |    inplace         |    None     | Because the input buffer is not modified by   |
|                |             |                    |             | the ML operation, but the metadata generated  |
|                |             |                    |             | out of an inference operation needs to be     |
|                |             |                    |             | added/appended to the input buffer, the       |
|                |             |                    |             | GstBuffer is writable. The ivas_xfilter is    |
|                |             |                    |             | configured in inplace mode                    |
|                |             |                    |             |                                               |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
| kernels        |    N/A      |    N/A             |    N/A      | The JSON tag for starting the kernel specific |
|                |             |                    |             | configurations.                               |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
| kernel-name    |    string   |    N/A             |    NULL     | The name and instance of a kernel separated   |
|                |             |                    |             | by ":"                                        |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
| library-name   |    string   |    N/A             |    NULL     | Acceleration software library name for the    |
|                |             |                    |             | kernel. It is appended to the ivas-l          |
|                |             |                    |             | ibrary-repo for an absolute path.             |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
| config         |    N/A      |    N/A             |    N/A      | The JSON tag for kernel-specific              |
|                |             |                    |             | configurations depending on the acceleration  |
|                |             |                    |             | software library.                             |
|                |             |                    |             |                                               |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
| model-name     |    string   |    resnet50        |    N/A      | Name string of the machine learning model to  |
|                |             |                    |             | be executed. The name string should be same as|
|                |             |                    |             | the name of the directory available in model  |
|                |             |                    |             | -path parameter file. If the name of the model|
|                |             |                    |             | ELF file is resnet50.elf, then the model-name |
|                |             |                    |             | is resnet50 in the JSON file. The ELF file    |
|                |             |                    |             | present in the specified path model-path of   |
|                |             |                    |             | the JSON file.                                |
|                |             |                    |             |                                               |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
| model-class    |    string   |    YOLOV3          |    N/A      | Class of model corresponding to model. Some of|
|                |             |                    |             | the examples are shown here:                  |
|                |             |  FACEDETECT        |             |                                               |
|                |             |    CLA             |             | * YOLOV3: yolov3_adas_pruned_0_9,             |
|                |             | SSIFICATION        |             |           yolov3_voc, yolov3_voc_tf           |
|                |             |    SSD             |             | * FACEDETECT: densebox_320_320,               |
|                |             |  REFINEDET         |             |               densebox_640_360                |                                         
|                |             |    TFSSD           |             | * CLASSIFICATION: resnet18, resnet50,         |
|                |             |    YOLOV2          |             |                   resnet_v1_50_tf             |
|                |             |                    |             |                                               | 
|                |             |                    |             |                                               |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
| model-format   |   string    |  RGB/BGR           |    N/A      | Image color format required by model.         |
|                |             |                    |             |                                               | 
|                |             |                    |             |                                               |
|                |             |                    |             |                                               |
|                |             |                    |             |                                               | 
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
|                |    string   |                    |    N/A      | Path of the folder where the model to be      |
| model-path     |             | /usr/share/        |             | executed is stored.                           |
|                |             |    vitis_          |             |                                               |
|                |             | ai_library/        |             |                                               |
|                |             |    models/         |             |                                               |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
| run_time_model |    Boolean  |  True/False        |    False    | If there is a requirement to change the ML    |
|                |             |                    |             | model at the frame level, then set this flag  |
|                |             |                    |             | to true. If this parameter is set to true then|
|                |             |                    |             | ivas_xdpuinfer will read the model name and   |
|                |             |                    |             | class from the incoming input metadata and    |
|                |             |                    |             | execute the same model found in the path      |
|                |             |                    |             | specified in the model-path. The model-name   |
|                |             |                    |             | and model-class parameter of the JSON file are|
|                |             |                    |             | not required when enabling this parameter.    |
|                |             |                    |             |                                               |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
| need_preprocess|    Boolean  |                    |    True     | If need_preprocess = true: Normalize with     |
|                |             |                    |             | mean/scale through the Vitis AI Library       |
|                |             |                    |             | If need_preprocess = false: Normalize with    |
|                |             |                    |             | mean/scale is performed before feeding the    |
|                |             |                    |             | frame to ivas_xdpuinfer. The Vitis AI library |
|                |             |                    |             | does not perform these operations.            |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
|    perfo       |    Boolean  |                    |    False    | Enable performance test and corresponding     |
| rmance_test    |             |  True/False        |             | flops per second (f/s) display logs.          |
|                |             |                    |             | Calculates and displays the f/s of the        |
|                |             |                    |             | standalone DPU after every second.            |
|                |             |                    |             |                                               |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+
| debug_level    |    integer  |    0 to 3          |    1        | Used to enable log levels.                    |
|                |             |                    |             |                                               |
|                |             |                    |             |                                               |
|                |             |                    |             | There are basically four log levels for a     |
|                |             |                    |             | message sent by the kernel library code,      |
|                |             |                    |             | starting from level 0 and decreasing in       |
|                |             |                    |             | severity till level 3 the lowest log-level    |
|                |             |                    |             | identifier. When a log level is set, it acts  |
|                |             |                    |             | as a filter, where only messages with a       |
|                |             |                    |             | log-level lower than it, (therefore messages  |
|                |             |                    |             | with an higher severity) are displayed.       |
|                |             |                    |             |                                               |
|                |             |                    |             | 0: This is the highest level in order of      |
|                |             |                    |             | severity: It is used for messages about       |
|                |             |                    |             | critical errors, both hardware or software    |
|                |             |                    |             | related.                                      |
|                |             |                    |             |                                               |
|                |             |                    |             | 1: This level is used in situations where     |
|                |             |                    |             | your attention is immediately required.       |
|                |             |                    |             |                                               |
|                |             |                    |             | 2: This is the log level used for             |
|                |             |                    |             | informational messages about the action       |
|                |             |                    |             | performed by the kernel and output of model   |
|                |             |                    |             |                                               |
|                |             |                    |             | 3: This level is used for debugging.          |
|                |             |                    |             | level is                                      |
+----------------+-------------+--------------------+-------------+-----------------------------------------------+


Model Parameters
---------------------------------------------

The Vitis AI library provides a way to read model parameters by reading the configuration file. It facilitates uniform configuration management of model parameters. The configuration file is located in the model-path of the JSON file with [model_name].prototxt. These parameters are model specific. For more information on model parameters, refer to the Vitis AI Library User Guide (`UG1354 <http://www.xilinx.com/cgi-bin/docs/rdoc?t=vitis_ai%3Bv%3Dlatest%3Bd%3Dug1354-xilinx-ai-sdk.pdf>`__).


Example GStreamer Pipelines
---------------------------------------------

This section describes a few example GStreamer pipelines.


Classification Example Using Resnet50
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following pipeline performs ML using a Resnet50 model. DPU configuration uses kernels- config = ./json_files/kernel_resnet50.json for the ivas_xdpuinfer. The output of the ivas_xfilter is passed to fakesink along with the metadata.

.. figure:: ../images/example-using-resnet50-model.png 

The GStreamer command for the example pipeline:

.. code-block::

      gst-launch-1.0 filesrc location="<PATH>/001.bgr" blocksize=150528 numbuffers=1 
      ! videoparse width=224 height=224 framerate=30/1 format=16 
      ! ivas_xfilter name="kernel1" kernels-config="<PATH>/kernel_resnet50.json" 
      ! fakesink
  
The JSON file for the ivas_xdpuinfer to execute ``resnet50`` model based classification pipeline is described below.

.. code-block::

        {
      "ivas-library-repo": "/usr/local/lib/ivas/",
      "element-mode":"inplace",
      "kernels" :[
        {
         "library-name":"libivas_xdpuinfer.so",
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
        performed on the frame before feeding to ivas_xfilter/ivas_xdpuinfer othereise results may not be as expected.


.. _ivas_xmultisrc:

ivas_xmultisrc
==============================

The GStreamer ivas_xmultisrc plug-in can have one input pad and multiple-output pads. This single plug-in supports multiple acceleration kernels, each controlled by a separate acceleration software library.

.. figure:: ../images/ivas_xmultisrc.png


Input and Output
--------------------------------

Input and output accepts buffers with the following color formats on input GstPad and output GstPad.

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

The formats listed are the Xilinx IP supported color formats. To add other color formats, update the ivas_kernel.h and ivas_xfilter plug-ins.


Control Parameters and Plug-in Properties
----------------------------------------------------

Table 11: Plug-in Properties

+--------------------+-------------+-------------+-------------+-------------------------------------------+
|                    |             |             |             |                                           |
|  **Property Name** |  **Type**   |  **Range**  | **Default** |         **Description**                   |
|                    |             |             |             |                                           |
+====================+=============+=============+=============+===========================================+
| Kconfig            |    String   |    N/A      |    NULL     | Path of the JSON configuration file based |
|                    |             |             |             | on the VVAS cceleration software library  |
|                    |             |             |             | requirements. For further information,    |
|                    |             |             |             | refer to :doc:`B-JSON-File-Schema`        |
|                    |             |             |             |                                           |
+--------------------+-------------+-------------+-------------+-------------------------------------------+


JSON File for ivas_xmultisrc
---------------------------------------

This section covers the format of JSON file/string to be passed to the ivas_xmultisrc plug-in.


Example JSON File
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following example file describes how to specify two kernels that are being controlled by the single instance of the ivas_xmultisrc plug-in. The next section describes each field in this example file.

.. code-block::

      {
         "xclbin-location":"/usr/lib/binary_1.xclbin",
         "ivas-library-repo": "/usr/lib/",
         "kernels" :[
         {
            "kernel-name":"resize:resize_1", <------------------ kernel 1
            "library-name":"libivas_xresize.so",
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
| ivas-library-repo  |    String   |    N/A      |   /usr/lib  | The library path for the VVAS repository |
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
|                    |             |             |             | ivas-library-repo for an absolute path.  |
+--------------------+-------------+-------------+-------------+------------------------------------------+
| config             |    N/A      |    N/A      |    N/A      | The JSON tag for kernel specific         |
|                    |             |             |             | configurations that depends on the       |
|                    |             |             |             | acceleration software library.           |
+--------------------+-------------+-------------+-------------+------------------------------------------+


Src Pad Naming Syntax
^^^^^^^^^^^^^^^^^^^^^^^^^^^                  

For single output pad naming is optional. For multiple pads, the source pads names shall be as mentioned below, assuming the name of the plug-in as `sc`.

sc.src_0, sc.src_1 .....sc.src_n


Example Pipelines
---------------------


Single Output Pad
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following example demonstrates the ivas_xmultisrc plug-in configured for one input and one output. A test video pattern is generated by the videotestrc plug-in and passed to the ivas_xmultisrc plug-in. Depending on the kernel being used, ivas_xmultisrc uses kernel.json to configure the kernel for processing the input frame before passing it to the fakesink.

.. code-block::

      gst-launch-1.0 videotestsrc 
      ! "video/x-raw, width=1280, height=720, format=BGR" 
      ! ivas_xmultisrc kconfig="/root/jsons/<kernel.json>" 
      ! "video/x-raw, width=640, height=360, format=BGR" 
      ! fakesink -v

The following is an example kernel.json file having `mean_value` and `use_mean` as kernel configuration parameters:

.. code-block::

      {
         "xclbin-location": "/usr/lib/dpu.xclbin",
         "ivas-library-repo": "/usr/lib/ivas",
         "kernels": [
            {
               "kernel-name": "<kernel-name>",
               "library-name": "libivas_xresize_bgr.so",
               "config": {
               "use_mean": 1,
               "mean_value": 128
               }
            }
         ]
         }
      }



GstIvasBufferPool
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The GStreamer VVAS buffer pool holds the pool of video frames allocated using the GStreamer allocator object. It is derived from the GStreamer base video buffer pool object (GstVideoBufferPool).

The VVAS buffer pool:

* Allocates buffers with stride and height alignment requirements. (e.g., the video codec unit (VCU) requires the stride to be aligned with 256 bytes and the height aligned with 64 bytes)

* Provides a callback to the GStreamer plug-in when the buffer comes back to the pool after it is used.

The following API is used to create the buffer pool.

.. code-block::

      GstBufferPool *gst_ivas_buffer_pool_new (guint stride_align, guint
      elevation_align)

      Parameters:
         stride_align - stride alignment required
         elevation_align - height alignment required

      Return:
         GstBufferPool object handle

Plug-ins can use the following API to set the callback function on the IVAS buffer pool and the callback function is called when the buffer arrives back to the pool after it is used.

.. code-block::

      Buffer release callback function pointer declaration:
      typedef void (*ReleaseBufferCallback)(GstBuffer *buf, gpointer user_data);

      void gst_ivas_buffer_pool_set_release_buffer_cb (GstIvasBufferPool *xpool,
      ReleaseBufferCallback release_buf_cb, gpointer user_data)

      Parameters:
         xpool - IVAS buffer pool created using gst_ivas_buffer_pool_new
         release_buf_cb - function pointer of callback
         user_data - user provided handle to be sent as function argument while
      calling release_buf_cb()

      Return:
          None


GstIvasAllocator
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The GStreamer IVAS allocator object is derived from an open source GstAllocator object designed for allocating memory using XRT APIs. The IVAS allocator is the backbone to the VVAS framework achieving zero-copy (wherever possible).


Allocator APIs
---------------------------------

GStreamer plug-in developers can use the following APIs to interact with the IVAS allocator. To allocate memory using XRT, the GStreamer plug-ins and buffer pools require the GstAllocator object provided by the following API.

.. code-block::

      GstAllocator* gst_ivas_allocator_new (guint dev_idx, gboolean need_dma)

      Parameters:
         dev_idx - FPGA Device index on which memory is going to allocated
         need_dma - will decide memory allocated is dmabuf or not

      Return:
         GstAllocator handle on success or NULL on failure

..note:: In PCIe platforms, the DMA buffer allocation support is not available. This means that the need_dma argument to gst_ivas_allocator_new() API must be false.

Use the following API to check if a particular GstMemory object is allocated using GstIvasAlloctor.

.. code-block::

      gboolean gst_is_ivas_memory (GstMemory *mem)

      Parameters:
         mem - memory to be validated

      Return:
         true if memory is allocated using IVAS Allocator or false


When GStreamer plug-ins are interacting with hard-kernel IP or soft-kernel, the plug-ins need physical memory addresses on an FPGA using the following API.

.. code-block::

      guint64 gst_ivas_allocator_get_paddr (GstMemory *mem)

      Parameters:
         mem - memory to get physical address

      Return:
         valid physical address on FPGA device or 0 on failure

      Use the following API when plug-ins need an XRT buffer object (BO) corresponding to an VVAS memory object.

.. code-block::

      guint gst_ivas_allocator_get_bo (GstMemory *mem)

      Parameters:
         mem - memory to get XRT BO

      Return:
         valid XRT BO on success or 0 on failure



JSON Schema
==========================================

This section covers the JSON schema for the configuration files used by the infrastructure plug-ins. For more details, refer to :doc:`JSON Schema <B-JSON-File-Schema>`


VVAS Inference Meta Data
=====================================

This section covers details about inference meta data structure used to store meta data. For more details, refer to :doc:`VVAS Inference Meta Data <A-IVAS-Inference-Metadata>`