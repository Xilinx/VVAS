########################################################################
 # Copyright 2020 - 2022 Xilinx, Inc.
 #
 # Licensed under the Apache License, Version 2.0 (the "License");
 # you may not use this file except in compliance with the License.
 # You may obtain a copy of the License at
 #
 #     http://www.apache.org/licenses/LICENSE-2.0
 #
 # Unless required by applicable law or agreed to in writing, software
 # distributed under the License is distributed on an "AS IS" BASIS,
 # WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 # See the License for the specific language governing permissions and
 # limitations under the License.
#########################################################################

source /opt/xilinx/xrt/setup.sh
source /opt/xilinx/xrm/setup.sh
export LD_LIBRARY_PATH=/opt/xilinx/vvas/lib:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=/opt/xilinx/vvas/lib/pkgconfig:$PKG_CONFIG_PATH
export PATH=/opt/xilinx/vvas/bin:$PATH
export GST_PLUGIN_PATH=/opt/xilinx/vvas/lib/gstreamer-1.0:$GST_PLUGIN_PATH
