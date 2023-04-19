vvas_xmultisrc
===============

The GStreamer ``vvas_xmultisrc`` plug-in can have one input pad and multiple-output pads. 

For ``vvas_xmultisrc`` implementation details, refer to `vvas_xmultisrc source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/sys/multisrc>`_

.. figure:: ../../images/vvas_xmultisrc.png


Input and Output
--------------------------------

Input and output accept buffers with the following color formats on input GstPad and output GstPad.

* GRAY8

* NV12

* NV16

* BGR

* BGRx

* RGB

* RGBx

* YUY2

* UYVY

* BGRA

* RGBA

The formats listed are the AMD IP supported color formats. To add other color formats, update the vvas_kernel.h and vvas_xmultisrc plug-ins pad template. Also ensure that the kernel support the formats that you are configuring.


Control Parameters and Plug-in Properties
------------------------------------------

Table 23: Plug-in Properties

+--------------------+-------------+-------------+-------------+-------------------------------------------+
|                    |             |             |             |                                           |
|  **Property Name** |  **Type**   |  **Range**  | **Default** |         **Description**                   |
|                    |             |             |             |                                           |
+====================+=============+=============+=============+===========================================+
| kconfig            |    String   |    N/A      |    NULL     | Path of the JSON configuration file based |
|                    |             |             |             | on the VVAS acceleration software library |
|                    |             |             |             | requirements. For further information,    |
|                    |             |             |             | refer to :doc:`json_file_schema`          |
|                    |             |             |             |                                           |
+--------------------+-------------+-------------+-------------+-------------------------------------------+
| dynamic-config     |  String     |    N/A      |    NULL     |                                           |
+--------------------+-------------+-------------+-------------+-------------------------------------------+


JSON format for vvas_xmultisrc
--------------------------------

This section covers the format of JSON file/string to be passed to the vvas_xmultisrc plug-in.


Example JSON File
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following example file describes how to specify a kernel that is being controlled by the instance of the vvas_xmultisrc plug-in. Modify this json file as per your kernels and acceleration software library. The next section describes each field in this example file.

.. code-block::

      {
         "xclbin-location":"/usr/lib/binary_1.xclbin",
         "vvas-library-repo": "/usr/lib/",
         "kernel" : {
            "kernel-name":"resize:resize_1", <------------------ kernel name
            "library-name":"libvvas_xresize.so",
            "config": {
               x : 4,
               y : 7
            }
         }
      }

Table 24: JSON Properties

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
| kernel             |    N/A      |    N/A      |    N/A      | The JSON tag for starting the kernel     |
|                    |             |             |             | specific configurations.                 |
+--------------------+-------------+-------------+-------------+------------------------------------------+
| kernel-name        |    String   |    N/A      |    NULL     | Name and instance of a kernel separated  |
|                    |             |             |             | by ":" as mentioned in xclbin.           |
+--------------------+-------------+-------------+-------------+------------------------------------------+
| library-name       |    String   |    N/A      |    NULL     | The acceleration software library name   |
|                    |             |             |             | for the kernel. This is appended to      |
|                    |             |             |             | vvas-library-repo for an absolute path.  |
+--------------------+-------------+-------------+-------------+------------------------------------------+
| kernel-access-mode |    String   |    N/A      |  "shared"   | Mode in which IP/Kernel is to be         |
|                    |             |             |             | accessed.                                |
|                    |             |             |             | "shared or "exclusive"                   |
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
         "xclbin-location": "/run/media/mmcblk0p1/dpu.xclbin",
         "vvas-library-repo": "/usr/lib/vvas",
         "kernel": { 
            "kernel-name": "<kernel-name>",
            "library-name": "libvvas_xresize_bgr.so",
            "config": {
            "use_mean": 1,
            "mean_value": 128
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

