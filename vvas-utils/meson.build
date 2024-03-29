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

project('vvas-utils', 'c', 'cpp',
  version : '3.0.0',
  meson_version : '>= 0.54.1',
  default_options : [ 'warning_level=1', 'cpp_std=c++14',
                      'buildtype=debugoptimized' ])

project_version = meson.project_version()
version_arr = project_version.split('.')
utils_version_major = version_arr[0].to_int()
utils_version_minor = version_arr[1].to_int()

utils_version = '@0@.@1@'.format(utils_version_major, utils_version_minor)

xrt_req = '>= 2.6' #XRT 2020.1

vvas_utils_args = ['-DHAVE_CONFIG_H']

configinc = include_directories('.')
utilsinc = include_directories('utils')

soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
curversion = utils_version_major * 100 + utils_version_minor
libversion = '@0@.@1@.0'.format(soversion, curversion)

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins = []

cdata = configuration_data()
cc = meson.get_compiler('c')

cdata.set_quoted('VERSION', utils_version)
cdata.set_quoted('PACKAGE', 'vvas-utils')
cdata.set_quoted('PACKAGE_VERSION', utils_version)
cdata.set_quoted('PACKAGE_NAME', 'VVAS Utility package')
cdata.set_quoted('GETTEXT_PACKAGE', 'vvas-utils-' + utils_version)

#TODO: currently proprietary until release
cdata.set_quoted('LIBDIR', join_paths(get_option('prefix'), get_option('libdir')))
cdata.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))

# supporting 64-bit x86 family & ARM 64-bit compilers only

if host_machine.cpu_family() == 'x86_64'
  message ('compiling for PCIe platform')
  cdata.set('XLNX_PCIe_PLATFORM', 1)
elif host_machine.cpu_family() == 'aarch64'
  message ('compiling for Embedded platform')
  cdata.set('XLNX_EMBEDDED_PLATFORM ', 1)
else
  error('not supporting host machine families other than x86_64 & aarch64')
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
  '-Wmissing-prototypes',
  '-Wdeclaration-after-statement',
  '-Wold-style-definition',
  '-Waggregate-return',
]

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

foreach extra_arg : warning_c_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

check_headers = [
  ['HAVE_STDIO_H', 'stdio.h'],
  ['HAVE_DLFCN_H', 'dlfcn.h'],
]

foreach h : check_headers
  if cc.has_header(h.get(1))
    cdata.set(h.get(0), 1)
  endif
endforeach

# XRT dependency
xrt_dep = dependency('xrt', version : xrt_req, required : true)

# External dependency
dl_dep = cc.find_library('dl', required : true)
uuid_dep = cc.find_library('uuid', required : true)
jansson_dep = dependency('jansson', version : '>= 2.7', required: true)
vvascore_dep = dependency('vvas-core', version : '>= 1.0', required: true)

glib_req = '>= 2.44.0'
glib_deps = [dependency('glib-2.0', version : glib_req, fallback: ['glib', 'libglib_dep']),
             dependency('gobject-2.0', fallback: ['glib', 'libgobject_dep'])]

plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif

python3 = import('python').find_installation()
pkgconfig = import('pkgconfig')

configure_file(output : 'config.h', configuration : cdata)

#include directories
subdir('utils')
subdir('pkgconfig')

