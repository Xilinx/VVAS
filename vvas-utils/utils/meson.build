########################################################################
 # Copyright 2020 - 2022 Xilinx, Inc.
 # Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
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

vvasutil_sources = ['vvas_kernel_utils.c']

vvasutil = library('vvasutil-' + utils_version,
  vvasutil_sources,
  c_args : vvas_utils_args,
  include_directories : [configinc],
  install : true,
  dependencies : [xrt_dep, jansson_dep, vvascore_dep],
)

#VVAS Common Headers to install
vvas_common_headers = ['vvas/vvas_kernel.h', 'vvas/vvasmeta.h', 'vvas/vvaslogs.h', 'vvas/vvas_structure.h']
install_headers(vvas_common_headers, subdir : 'vvas/')

vvasutils_dep = declare_dependency(link_with : vvasutil,
  include_directories : [utilsinc],
  dependencies : [vvascore_dep])

vvasstructure_sources = ['vvas_structure.c']

vvasstructure = library('vvasstructure-' + utils_version,
  vvasstructure_sources,
  cpp_args : vvas_utils_args,
  include_directories : [configinc],
  install : true,
  dependencies : [glib_deps],
)

vvasstructure_dep = declare_dependency(link_with : vvasstructure,
  include_directories : [utilsinc],
  dependencies : [glib_deps])
