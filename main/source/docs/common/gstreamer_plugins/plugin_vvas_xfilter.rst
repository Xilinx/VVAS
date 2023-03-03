.. _vvas_xfilter:

vvas_xfilter
==============

The GStreamer vvas_xfilter is an infrastructure plug-in that is derived from GstBaseTransform. It supports one input pad and one output pad. The vvas_xfilter efficiently works with hard-kernel/soft-kernel/software (user-space) acceleration software library types as shown in the following figure.

.. figure:: ../../images/xfilter_plugin_arch.png 

This plug-in can operate in the following three modes.
  
* **Passthrough:** Useful when the acceleration software library does not need to alter the input buffer.

* **In-place:** Useful when the acceleration software library needs to alter the input buffer.

* **Transform:** In this mode, for each input buffer, a new output buffer is produced.

You must set the mode using the JSON file. Refer to :doc:`JSON File Schema <json_file_schema>` for information related to the kernels-config property.

.. figure:: ../../images/xfilter_plugin.png 

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
-----------------------------------------

The following table lists the GObject properties exposed by the vvas_xfilter. Most of them are only available in PCIe based platforms.

Table 18: GObject Properties

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
|                     |                            |          |           |             | Refer to the :doc:`json_file_schema`          |
|                     |                            |          |           |             |                                               |
+---------------------+----------------------------+----------+-----------+-------------+-----------------------------------------------+
|                     |                            |          |           |             |                                               |
| sk-cur-idx          |  PCIe only                 |  Integer | 0 to 31   |    0        | Softkernel current index to be used for       |
|                     |                            |          |           |             | executing job on device.                      |
+---------------------+----------------------------+----------+-----------+-------------+-----------------------------------------------+



JSON Format for vvas_xfilter Plug-in
------------------------------------

The following table provides the JSON keys accepted by the GStreamer vvas_xfilter plug-in.

Table 19: Root JSON Object Members

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
|                      | Value type           | String                            |
+----------------------+----------------------+-----------------------------------+
|                      | Mandatory or         | Mandatory                         |
|                      | optional             |                                   |
+----------------------+----------------------+-----------------------------------+
| kernel               | Description          | A Kernel JSON object              |
|                      |                      | provides information              |
|                      |                      | about an VVAS                     |
|                      |                      | acceleration                      |
|                      |                      | software library                  |
|                      |                      | configuration.                    |
+----------------------+----------------------+-----------------------------------+
|                      | Value type           | Kernel JSON Object                |
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

Table 20: Kernel JSON Object Members

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
| kernel-access-mode   | Description          | Mode in which IP/Kernel  |
|                      |                      | is to be accessed.       |
|                      |                      | "shared" or "exclusive"  |
+----------------------+----------------------+--------------------------+
|                      | Value type           | String                   |
+----------------------+----------------------+--------------------------+
|                      | Mandatory or         | Optional                 |
|                      | optional             |                          |
+----------------------+----------------------+--------------------------+
|                      | Default value        | "shared"                 |
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
----------------------------------


JSON File for a Hard Kernel
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following JSON file uses vvas_xfilter to control multi-scaler IP (hard-kernel). The acceleration software library accessing the register map is libvvas_xcrop.so.

.. code-block::

 {
  "xclbin-location": "/run/media/mmcblk0p1/dpu.xclbin",
  "vvas-library-repo": "/usr/lib/",
  "element-mode": "passthrough",
  "kernel": {
    "kernel-name": "v_multi_scaler:{v_multi_scaler_1}",
    "library-name": "libvvas_xcrop.so",
    "config": {}
  }
 }
