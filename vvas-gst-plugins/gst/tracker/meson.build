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

gstvvas_xtracker = library('gstvvas_xtracker', 'gstvvas_xtracker.cpp',
  c_args : gst_plugins_vvas_args,
  cpp_args : [gst_plugins_vvas_args, '-std=c++17'],
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep, gst_dep, gstvvasalloc_dep, xrt_dep, dl_dep, gstallocators_dep, uuid_dep, vvasutils_dep, gstvvasutils_dep, xrm_dep, gstvvasinfermeta_dep, vvascore_dep, gstvvascoreutils_dep, gstvvassrcidmeta_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstvvas_xtracker, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstvvas_xtracker]
