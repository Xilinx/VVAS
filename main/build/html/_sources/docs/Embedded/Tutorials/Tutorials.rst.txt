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

#########################################
Tutorials
#########################################

This section covers tutorials that will explain step by step approach to build GStreamer pipelines for different usecases  using VVAS framework. Each tutorial explains what is the purpose of each plugin in the pipeline and how to configure, set properties and the corresponding json files whereever applicable.

********************
Multi Channel ML
********************
This tutorial explains how to build Machine Learning pipelines using VVAS. This tutorial first covers single stage Machine Learning Inference pipeline where only one ML operation is required on input image. Complex Real life applications may require more than one stages of Machine Learning operations to get the required information. When more than once Machine Learning Inference operations are performed, then this is commonly known as Cascaded Machine Learning. This tutorial also covers how to build multi stage Cascaded Machine Learning Pipelines using VVAS. For more details, refer to the link :doc:`MultiChannelML <MultiChannelML>` .

.. toctree::
   :maxdepth: 3
   :caption: Tutorials
   :hidden:


   Multichannel ML <./MultiChannelML>


