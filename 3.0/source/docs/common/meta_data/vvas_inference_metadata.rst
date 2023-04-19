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


.. _vvas_inference_metadata:


#########################
VVAS Inference Metadata
#########################

The ``GstInferenceMeta`` object, also known as the VVAS inference metadata, serves as a repository for information related to metadata produced by software libraries that accelerate machine learning (ML) inference. It collects data generated from various levels of ML operations and organizes them hierarchically within a single structure. Essentially, this data structure consolidates and stores metadata from detection and classification models.

The GStreamer plug-ins can set and get inference metadata from the GstBuffer by using the `gst_buffer_add_meta ()` API and `gst_buffer_get_meta ()` API, respectively.

.. toctree::
   :maxdepth: 1

   GStreamer Inference Metadata <./vvas_gstinference_metadata>

.. toctree::
   :maxdepth: 1

   VVAS Core Inference Metadata <./vvas_core_inference_metadata>
