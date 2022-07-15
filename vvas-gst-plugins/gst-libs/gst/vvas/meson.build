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

# VVAS metadata
meta_sources = ['gstvvaslameta.c']

gstvvaslameta = library('gstvvaslameta-' + vvas_version,
  meta_sources,
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
)
gstvvaslameta_dep = declare_dependency(link_with : [gstvvaslameta], dependencies : [gst_dep, gstbase_dep, gstvideo_dep])

# Extended GstInferenceMeta for VVAS
infermeta_sources = ['gstinferencemeta.c', 'gstinferenceclassification.c', 'gstinferenceprediction.c']

gstvvasinfermeta = library('gstvvasinfermeta-' + vvas_version,
  infermeta_sources,
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, vvasutils_dep, math_dep],
)
gstvvasinfermeta_dep = declare_dependency(link_with : [gstvvasinfermeta], dependencies : [gst_dep, gstbase_dep, vvasutils_dep, math_dep])

#VVAS allocator using XRT
alloc_sources = ['gstvvasallocator.c']

gstvvasalloc = library('gstvvasallocator-' + vvas_version,
  alloc_sources,
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, xrt_dep, gstallocators_dep, vvasutils_dep],
)
gstvvasalloc_dep = declare_dependency(link_with : [gstvvasalloc], dependencies : [gst_dep, gstbase_dep, gstvideo_dep, xrt_dep, gstallocators_dep])

#VVAS bufferpool with stride and elevation
vvaspool_sources = ['gstvvasbufferpool.c']

gstvvaspool = library('gstvvasbufferpool-' + vvas_version,
  vvaspool_sources,
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, gstvvasalloc_dep],
)
gstvvaspool_dep = declare_dependency(link_with : [gstvvaspool], dependencies : [gst_dep, gstvideo_dep, gstbase_dep, gstvvasalloc_dep])

# VVAS Input Inference metadata
inpinfermeta_sources = ['gstvvasinpinfer.c']

gstvvasinpinfermeta = library('gstvvasinpinfermeta-' + vvas_version,
  inpinfermeta_sources,
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
)
gstvvasinpinfermeta_dep = declare_dependency(link_with : [gstvvasinpinfermeta], dependencies : [gst_dep, gstbase_dep])

# VVAS Usr metadata
usrmeta_sources = ['gstvvasusrmeta.c']

gstvvasusrmeta = library('gstvvasusrmeta-' + vvas_version,
  usrmeta_sources,
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
)
gstvvasusrmeta_dep = declare_dependency(link_with : [gstvvasusrmeta], dependencies : [gst_dep, gstbase_dep])

# VVAS HDR metadata
hdrmeta_sources = ['gstvvashdrmeta.c']

gstvvashdrmeta = library('gstvvashdrmeta-' + vvas_version,
  hdrmeta_sources,
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
)
gstvvashdrmeta_dep = declare_dependency(link_with : [gstvvashdrmeta], dependencies : [gst_dep, gstbase_dep])

# VVAS overlay metadata
overlaymeta_sources = ['gstvvasoverlaymeta.c']

gstvvasoverlaymeta = library('gstvvasoverlaymeta-' + vvas_version,
  overlaymeta_sources,
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, gstvvasinfermeta_dep],
)
gstvvasoverlaymeta_dep = declare_dependency(link_with : [gstvvasoverlaymeta], dependencies : [gst_dep, gstbase_dep, gstvideo_dep, gstvvasinfermeta_dep])

# VVAS opticalflow metadata
ofmeta_sources = ['gstvvasofmeta.c']

gstvvasofmeta = library('gstvvasofmeta-' + vvas_version,
  ofmeta_sources,
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, gstvvasinfermeta_dep],
)
gstvvasofmeta_dep = declare_dependency(link_with : [gstvvasofmeta], dependencies : [gst_dep, gstbase_dep, vvasutils_dep, math_dep])

# VVAS SRC ID metadata
srcidmeta_sources = ['gstvvassrcidmeta.c']

gstvvassrcidmeta = library('gstvvassrcidmeta-' + vvas_version,
  srcidmeta_sources,
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
)
gstvvassrcidmeta_dep = declare_dependency(link_with : [gstvvassrcidmeta], dependencies : [gst_dep, gstbase_dep])

gstvvasutils = library('gstvvasutils', 'gstvvasutils.c',
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
  install : true,
)
gstvvasutils_dep = declare_dependency(link_with : [gstvvasutils], dependencies : [gst_dep, gstbase_dep, gstvideo_dep])

#VVAS GST Headers to install
vvas_gst_headers = ['gstvvaslameta.h',
                    'gstvvasallocator.h',
                    'gstvvasbufferpool.h',
                    'gstinferencemeta.h',
                    'gstinferenceprediction.h',
                    'gstinferenceclassification.h',
                    'gstvvasinpinfer.h',
                    'gstvvasusrmeta.h',
                    'gstvvashdrmeta.h',
                    'gstvvasoverlaymeta.h',
                    'gstvvasofmeta.h',
                    'gstvvassrcidmeta.h',
                    'gstvvasutils.h',
                    'gstvvascommon.h']

install_headers(vvas_gst_headers, subdir : 'gstreamer-1.0/gst/vvas/')

gstvvaspool_dep = declare_dependency(link_with : [gstvvaspool], dependencies : [gst_dep, gstbase_dep])
