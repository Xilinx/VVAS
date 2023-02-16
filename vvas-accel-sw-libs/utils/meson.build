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

vvas_accel_utils = library('vvas_accel_utils',
  'vvas_accel_utils.c',
  c_args : [vvas_accel_sw_libs_args],
  include_directories : [configinc],
  dependencies : [vvasutils_dep, vvascore_dep, xrt_dep],
  install : true,
)

#VVAS Accel Util Headers to install
vvas_accel_uril_headers = ['vvas_accel_utils.h']

install_headers(vvas_accel_uril_headers)

vvas_accel_utils_dep = declare_dependency(link_with : [vvas_accel_utils], dependencies : [vvasutils_dep, vvascore_dep])
