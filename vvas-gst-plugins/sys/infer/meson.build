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

dpucore_path = vvascore_dep.get_variable(pkgconfig : 'libdir')
dpucore_version = vvascore_dep.version()
dpuinfer_libdep = cc.find_library('vvascore_dpuinfer-' + dpucore_version, dirs : [dpucore_path], required : false)

if not dpuinfer_libdep.found()
  if host_machine.cpu_family() == 'x86_64'
    message('vvas core dpu library is not available. Skipping infer plugin build.')
    subdir_done()
  else
    error('Failed to find vvas core dpu library')
  endif
endif

gstvvas_xinfer = library('gstvvas_xinfer', 'gstvvas_xinfer.c',
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep, gst_dep, gstvvaspool_dep, gstvvasalloc_dep, dl_dep, jansson_dep, gstallocators_dep, uuid_dep, vvascore_dep, gstvvasutils_dep, gstvvasinfermeta_dep, math_dep, gstvvascoreutils_dep, vvasstructure_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstvvas_xinfer, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstvvas_xinfer]
