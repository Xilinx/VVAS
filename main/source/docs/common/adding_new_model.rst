##########################
Adding new model in VVAS
##########################

This section covers details about various possibilities of executing a ML model using VVAS.

If the model corresponds to one of the classes already supported by ``vvas_xinfer`` plug-in then there is no change needed anywhere except specifying correct class name in the infer json file used by ``vvas_xinfer``.

If the class corresponding to the new model is not supported by ``vvas_xinfer`` but that is supported by ``Vitis AI`` library then follow the below mentioned steps to add a new model class in ``vvas_xinfer``.

* Add the model class type in file `vvas_dpucommon.h <https://github.com/Xilinx/vvas-core/blob/master/common/vvas_core/vvas_dpucommon.h#L52>`_
* Update `vvas_dpuinfer.cpp <https://github.com/Xilinx/vvas-core/blob/master/dpuinfer/vvas_dpuinfer.cpp>`_ with the header file for your model class. You may refer to the implementation for other class to know thw changes needed.
* Update the `meson.build <https://github.com/Xilinx/vvas-core/blob/master/dpuinfer/meson.build>`_ for the new class and corresponding ``Vitis AI`` library name.
* Add new files corresponding to the new class implementation.
  -  You may refer to the other class implementation.
* Update `meson.build <https://github.com/Xilinx/vvas-core/blob/master/meson.build#L139>`_
* In case the new model results are not supported by the existing fields in the :ref:`vvas_inference_metadata` structure, then this may also needs to be modified to support the new meta data. Modifications are needed in `vvas_infer_prediction.h <https://github.com/Xilinx/vvas-core/blob/master/common/vvas_core/vvas_infer_prediction.h>`_

If the model is not supported by any of the classes supported by ``Vitis AI`` library or if the model is not in DPU (Deep Learning Processing Unit) deployable format, then it first needs to be converted into DPU deployable state. For this refer to `Deploying a NN Model with Vitis AI <https://xilinx.github.io/Vitis-AI/docs/workflow-model-development.html#>`_. For user guide, refer to `Vitis AI 3.0 documentation <https://docs.xilinx.com/access/sources/ud/document?Doc_Version=3.0%20English&url=ug1431-vitis-ai-documentation>`_ 

Once ``xmodel`` is availablel for the user model, refer to :ref:`raw-tensor-example-label` to know more about how to use this model with VVAS.
