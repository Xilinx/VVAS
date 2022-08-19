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

xrtutil_sources = ['xrt_utils.cpp']

xrtutils = library('xrtutil-' + utils_version,
  xrtutil_sources,
  cpp_args : vvas_utils_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [xrt_dep, uuid_dep],
)

xrtutils_dep = declare_dependency(link_with : xrtutils, 
  include_directories : [utilsinc],
  dependencies : [xrt_dep])

vvasutil_sources = ['vvas_kernel_utils.c']

vvasutil = library('vvasutil-' + utils_version,
  vvasutil_sources,
  c_args : vvas_utils_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [xrt_dep, jansson_dep, xrtutils_dep],
)

#VVAS Common Headers to install
#vvas_common_headers = ['vvas/vvas_kernel.h', 'vvas/vvasmeta.h']
vvas_common_headers = ['vvas/vvas_kernel.h', 'vvas/vvasmeta.h', 'xrt_utils.h', 'vvas/vvaslogs.h']
install_headers(vvas_common_headers, subdir : 'vvas/')

vvasutils_dep = declare_dependency(link_with : vvasutil,
  include_directories : [utilsinc],
  dependencies : [xrt_dep])
