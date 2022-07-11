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
Tutorials for PCIe/Data Center platforms
#########################################

This section covers how to build GStreamer based Transcoding pipelines for PCIe base platforms that are used in Data Center. Transcoding may be from a Live (capture/streaming) source or from a storage (offline transcoding). First tutorial will cover basic transcoding, how to build and execute single instance of transcode pipeline. Second tutorial talks about complex and multi-instance Transcode pipelines. This tutorial covers how to define multiple instances and launch these on several Alveo U30 cards.

:doc:`Basic Transcoding Pipelines <./transcoding>` tutorial covers basic transcoding, how to build and execute single instance of different transcode pipelines.

:doc:`Multi-instance High Density Transcoding Pipelines <./multi_instance_launch_utilities>` tutorial covers how to define multiple instances and launch these on several Alveo U30 cards installed on a server.


.. toctree::
   :maxdepth: 3
   :caption: Basic Transcoding
   :hidden:

   Basic Transcoding <./transcoding>

.. toctree::
   :maxdepth: 3
   :caption: Multi-instance Transcoding
   :hidden:

   Multi-instance Transcoding <./multi_instance_launch_utilities>

.. toctree::
   :maxdepth: 3
   :caption: ABR Ladder Application
   :hidden:

   ABR Ladder Application <./xabrladder>
