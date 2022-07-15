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

project('vvas-xcdr', 'c', 'cpp',
  version : '2.0.0',
  meson_version : '>= 0.54.1',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

install_subdir('scripts', install_dir : '/opt/xilinx/xcdr')
install_subdir('xclbins', install_dir : '/opt/xilinx/xcdr')
install_data('setup.sh', install_dir : '/opt/xilinx/xcdr')
install_data('xrmd_start.bash', install_dir : '/opt/xilinx/xcdr')
