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

decoder_xrm_plugin = library('xrmU30Decoder', 'codec-decoder-xrm-plg-u30.cpp',
  include_directories : [configinc],
  dependencies : [xrm_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(decoder_xrm_plugin, install_dir : plugins_pkgconfig_install_dir)
plugins += [decoder_xrm_plugin]
