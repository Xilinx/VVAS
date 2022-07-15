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

project('vvas-xrm-plugins', 'c', 'cpp',
  version : '2.0.0',
  meson_version : '>= 0.54.1',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

xrt_req = '>= 2.6' #XRT 2022.1
xrm_req = '>= 1.0.0' #XRM 2022.1

configinc = include_directories('.')

plugins_install_dir = '/opt/xilinx/xrm/plugin'
plugins = []

cdata = configuration_data()
cc = meson.get_compiler('cpp')

# supporting 64-bit x86 family & ARM 64-bit compilers only

if host_machine.cpu_family() == 'x86_64'
  message ('compiling for PCIe platform')
  cdata.set('XLNX_PCIe_PLATFORM', 1)
else
  error('not supporting host machine families other than x86_64')
endif

warning_flags = [
  '-Wmissing-declarations',
  '-Wredundant-decls',
  '-Wwrite-strings',
  '-Winit-self',
  '-Wno-multichar',
  '-Wvla',
  '-Wpointer-arith',
]

warning_c_flags = [
  '-std=c++11',
  '-Wall',
  '-Wextra',
  '-O1',
  '-g',
  '-DDSA64',
  '-fpermissive',
]

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

foreach extra_arg : warning_c_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'cpp')
  endif
endforeach


# XRT dependency
xrt_dep = dependency('xrt', version : xrt_req, required : true)

# External dependency
jansson_dep = dependency('jansson', version : '>= 2.8', required: true)

xrm_dep = [dependency('libxrm', version : xrm_req, required : true), jansson_dep]

plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif

pkgconfig = import('pkgconfig')

subdir('codec-decoder-xrm-plg-u30')
subdir('codec-encoder-xrm-plg-u30')
subdir('filter-scaler-xrm-plg-u30')
