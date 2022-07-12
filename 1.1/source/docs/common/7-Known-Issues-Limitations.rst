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

****************************
Known Issues and Limitations
****************************

=================
Embedded Platform
=================                 

* Item 1. When a GStreamer tee element placed between a source and downstream element that has different stride requirements, the tee element drops the GstVideoMeta API from the allocation query which causes a copy of the buffers, leading to performance degradation.

* Item 2. v4l2src with io-mode=4, does not honor stride. Because of this issue, the display is not as expected.
