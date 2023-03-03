.. _vvas_xoptflow:

vvas_xoptflow
==============

Optical flow is one of the key functions used in many image processing and computer vision applications like object tracking, motion based segmentation, depth estimation, stitching and video frame rate conversion etc. Optical flow is estimated using previous and current frame pixel information.

For optical flow estimation this plug-in uses hardware accelerator of xfopencv non-pyramid optical flow. This non-pyramid optical flow function takes current and previous frame as input and generates two floating point buffers of x and y direction displacements. Optical flow plug-in attaches these displacement buffers as **gstvvasofmeta**.
For implementation details, refer to `vvas_xoptflow source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/sys/optflow>`_

.. figure:: ../../images/vvas_optflow_blockdiagram.png
   :align: center
   :scale: 80


Prerequisite
--------------

This plug-in uses **dense_non_pyr_of_accel** kernel. Make sure your xclbin has this kernel.

Input and Output
--------------------

Accepts buffer of NV12 format and generates two metadata buffers of type float and each size equal to the size of frame.
For details about the meta data structure, refer to :ref:`vvas_optflow_metadata`

Plug-in Properties
-------------------

Table 6: vvas_xoptflow Plug-in Properties

+--------------------+-------------+---------------+----------------------+------------------+
|                    |             |               |                      |                  |
|  **Property Name** |   **Type**  | **Range**     |     **Default**      | **Description**  |
|                    |             |               |                      |                  |
+====================+=============+===============+======================+==================+
| xclbin-location    |   String    |      NA       | ./binary_container_1 | location of      |
|                    |             |               | .xclbin              | xclbin           |
+--------------------+-------------+---------------+----------------------+------------------+
| dev-idx            |   Integer   |    0 to 31    |           0          | device index     |
+--------------------+-------------+---------------+----------------------+------------------+

example Pipelines
---------------------

The following example demonstrates use of vvas_xoptflow plug-in.


 
.. code-block::

      gst-launch-1.0 filesrc location=$1 ! \
      h264parse ! \
      queue ! \
      omxh264dec internal-entropy-buffers=2 ! \
      videorate ! video/x-raw, framerate=10/1 ! \
      queue ! \
      vvas_xinfer preprocess-config=kernel_preprocessor_dev0_yolov3_voc_tf.json infer-config=kernel_yolov3_voc_tf.json name=infer ! \
      vvas_xoptflow xclbin-location="/run/media/mmcblk0p1/dpu.xclbin" ! \
      fakesink -v
