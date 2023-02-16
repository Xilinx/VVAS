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

project('vvas-accel-sw-libs', 'c', 'cpp',
  version : '3.0.0',
  meson_version : '>= 0.54.1',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

gst_version = '1.18.5'
version_arr = gst_version.split('.')
gst_version_major = version_arr[0].to_int()
gst_version_minor = version_arr[1].to_int()
gst_version_micro = version_arr[2].to_int()
 if version_arr.length() == 4
  gst_version_nano = version_arr[3].to_int()
else
  gst_version_nano = 0
endif
gst_version_is_dev = gst_version_minor % 2 == 1 and gst_version_micro < 90

glib_req = '>= 2.44.0'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)
xrt_req = '>= 2.6' #XRT 2020.1

glib_deps = [dependency('glib-2.0', version : glib_req, fallback: ['glib', 'libglib_dep']),
             dependency('gobject-2.0', fallback: ['glib', 'libgobject_dep'])]

vvas_accel_sw_libs_args = ['-DHAVE_CONFIG_H']

configinc = include_directories('.')
vvas_accel_util_header = include_directories('utils')

soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
curversion = gst_version_minor * 100 + gst_version_micro
libversion = '@0@.@1@.0'.format(soversion, curversion)

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins = []

cdata = configuration_data()
cc = meson.get_compiler('c')

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

# Mandatory GST deps
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])

# XRT dependency
xrt_dep = dependency('xrt', version : xrt_req, required : true)

#VVAS Core dependency
vvascore_dep = dependency('vvas-core', version : '>= 1.0', required: true)

#vvasutils dependency
vvasutils_dep = dependency('vvas-utils', version : '>= 1.0', required: true)

#vvasstructure dependency
vvasstructure_dep = dependency('vvas-structure', version : '>= 1.0', required: true)

#gstvvas dependency
gstvvas_dep = dependency('vvas-gst-plugins', version : '>= 1.0', required : true)

#gstvvasmeta dependency
gstvvasinfermeta_dep = dependency('vvas-gst-plugins', version : '>= 1.0', required: true)

# External dependency
jansson_dep = dependency('jansson', version : '>= 2.8', required: true)

if host_machine.cpu_family() == 'aarch64'
  opencv_dep = dependency('opencv4', version : '>=4.4.0', required: true)
endif
if host_machine.cpu_family() == 'x86_64'
  opencv_dep = dependency('opencv4', version : '>=4.2.0', required: false)
  if not opencv_dep.found()
    opencv_dep = dependency('opencv', version : '>=3.2.0', required: true)
  endif
endif

vvas_videolib_install_path = ['/usr/lib/vvas/']

plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif

python3 = import('python').find_installation()
pkgconfig = import('pkgconfig')

if host_machine.cpu_family() == 'x86_64'
  # common kernel libraries for PCIe platforms
  foreach kernel : ['utils']
    if not get_option(kernel).disabled()
      subdir(kernel)
    endif
  endforeach
elif host_machine.cpu_family() == 'aarch64'
  # common kernel libraries for embedded platforms
  foreach kernel : ['utils']
    if not get_option(kernel).disabled()
      subdir(kernel)
    endif
  endforeach
endif


configure_file(output : 'config.h', configuration : cdata)

