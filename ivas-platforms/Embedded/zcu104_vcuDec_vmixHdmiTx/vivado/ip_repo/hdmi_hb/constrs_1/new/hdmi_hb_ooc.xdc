## Copyright 2019 Xilinx Inc.## Licensed under the Apache License, Version 2.0 (the "License");# you may not use this file except in compliance with the License.# You may obtain a copy of the License at##     http://www.apache.org/licenses/LICENSE-2.0## Unless required by applicable law or agreed to in writing, software# distributed under the License is distributed on an "AS IS" BASIS,# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.# See the License for the specific language governing permissions and# limitations under the License.#create_clock -period 10.000 -name status_sb_aclk [get_ports status_sb_aclk]
#set_property HD.CLK_SRC BUFGCTRL_X0Y0 [get_ports status_sb_aclk]

create_clock -period 3.333 -name link_clk [get_ports link_clk]
#set_property HD.CLK_SRC BUFGCTRL_X0Y1 [get_ports link_clk]

