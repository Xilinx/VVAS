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

#project('app', 'c')
src = ['src/vvas_xcompositor.c']
exe = executable('vvas_xcompositor', src,
c_args : gst_plugins_vvas_args,
include_directories : [configinc],
dependencies : [gstvideo_dep, gst_dep, xrt_dep, jansson_dep, vvasutils_dep, gstpbutils_dep],
install : true,
)
