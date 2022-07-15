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

vvas_xboundingbox = library('vvas_xboundingbox',
  'src/vvas_xboundingbox.cpp',
  cpp_args : [vvas_accel_sw_libs_args, '-std=c++17'],
  include_directories : [configinc],
  dependencies : [gst_dep, jansson_dep, vvasutils_dep, gstvvas_dep, opencv_dep],
  install : true,
)
