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

#opencv dependency
opencv_dep = dependency('opencv4', version : '>=4.2.0', required: false)

if not opencv_dep.found()
  opencv_dep = dependency('opencv', version : '>=3.2.0', required: true)
endif

gstvvas_xoverlay = library('gstvvas_xoverlay', 'gstvvas_xoverlay.cpp',
  cpp_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep, gst_dep, gstvvasalloc_dep, gstvvaspool_dep, dl_dep, gstallocators_dep, uuid_dep, opencv_dep, gstvvasoverlaymeta_dep, vvasutils_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstvvas_xoverlay, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstvvas_xoverlay]
