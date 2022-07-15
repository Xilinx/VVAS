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

gstvvas_xlookahead = library('gstvvas_xlookahead', ['gstvvas_xlookahead.c', 'la_algo/xlnx_aq_core.c', 'la_algo/xlnx_rc_aq.c', 'la_algo/xlnx_spatial_aq.c', 'la_algo/xlnx_queue.c'],
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep, gst_dep, gstvvasalloc_dep, gstallocators_dep, uuid_dep, vvasutils_dep, gstvvaslameta_dep, xrm_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstvvas_xlookahead, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstvvas_xlookahead]
