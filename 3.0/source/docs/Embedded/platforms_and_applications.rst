########################################
Platforms And Applications
########################################


This VVAS release supports ``Zynq UltraScale+ MPSoc`` as well as ``Versal`` based Embedded platforms. This release has been validated on ``zcu104`` based platform for different usecases. Various supported example applications using VVAS on ``zcu104`` and ``KV260 SOM`` based platforms are listed below. For creating example designs for ``Versal`` based platforms, you may refer to the ``zcu104`` based example designs provided with this release as reference for patches needed for other platforms. 

*******************
SOM Examples
*******************

There are three Application specific SOM platforms supported. Click on the link below to know more about these.

.. note::
        Currently KV260 SOM example designs mentioned below are based on Previous VVAS release (i.e. VVAS 2.0 and Vitis AI 2.5 release).

Smart Camera
   Smart Camera Application with face detection and display functionality. For more details refer to `Smart Camera <https://www.xilinx.com/products/app-store/kria/smart-camera.html>`_

AIBox-ReiD
   Application demonstrating multistream tracking and Re-Identification. For more details, refer to `AIBox-ReID <https://www.xilinx.com/products/app-store/kria/ai-box-for-pedestrian-tracking.html>`_

Defect Detection
   Application to detect defects in mangoes. For more details, refer to `Defect Detect <https://www.xilinx.com/products/app-store/kria/defect-detection.html>`_


.. toctree::
   :maxdepth: 3
   :caption: Smart Camera
   :hidden:

   Smart Camera <https://www.xilinx.com/products/app-store/kria/smart-camera.html>

.. toctree::
   :maxdepth: 3
   :caption: AIBox-ReiD
   :hidden:

   AIBox-ReID <https://www.xilinx.com/products/app-store/kria/ai-box-for-pedestrian-tracking.html>

.. toctree::
   :maxdepth: 3
   :caption: Defect Detect
   :hidden:

   Defect Detect <https://www.xilinx.com/products/app-store/kria/defect-detection.html>


*******************
ZCU104 Examples
*******************

Smart Model Select
   This application allows selection of ML model from 16 supported models, input source and output using command line and then it constructs and executes the pipeline. For more details, refer to :doc:`Smart ML Model Select <smart_model_select>`.

Multi Channel ML
   This application demonstrates multi channel multi model Machine Learning capability using VVAS. :doc:`Multi Channel ML Tutorial <Tutorials/MultiChannelML>` tutorial for beginners explains step by step approach to build this application/pipeline using VVAS.

.. toctree::
   :maxdepth: 3
   :caption: Smart Model Select
   :hidden:

   Smart Model Select <./smart_model_select>

.. toctree::
   :maxdepth: 3
   :caption: Multi Channel ML
   :hidden:

   Multi Channel ML <./Tutorials/MultiChannelML>

