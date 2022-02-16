# VVAS metadata

# Extended GstInferenceMeta for VVAS
infermeta_sources = ['gstinferencemeta.c', 'gstinferenceclassification.c', 'gstinferenceprediction.c']

gstvvasinfermeta = library('gstvvasinfermeta-' + api_version,
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

gstvvasalloc = library('gstvvasallocator-' + api_version,
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

gstvvaspool = library('gstvvasbufferpool-' + api_version,
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

gstvvasinpinfermeta = library('gstvvasinpinfermeta-' + api_version,
  inpinfermeta_sources,
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
)
gstvvasinpinfermeta_dep = declare_dependency(link_with : [gstvvasinpinfermeta], dependencies : [gst_dep, gstbase_dep])

# VVAS HDR metadata
hdrmeta_sources = ['gstvvashdrmeta.c']

gstvvashdrmeta = library('gstvvashdrmeta-' + api_version,
  hdrmeta_sources,
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
)
gstvvashdrmeta_dep = declare_dependency(link_with : [gstvvashdrmeta], dependencies : [gst_dep, gstbase_dep])

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
vvas_gst_headers = ['gstvvasallocator.h',
                    'gstvvasbufferpool.h',
                    'gstinferencemeta.h',
                    'gstinferenceprediction.h',
                    'gstinferenceclassification.h',
                    'gstvvasinpinfer.h',
		    'gstvvashdrmeta.h',
                    'gstvvasutils.h',
                    'gstvvascommon.h']

install_headers(vvas_gst_headers, subdir : 'gstreamer-1.0/gst/vvas/')

gstvvaspool_dep = declare_dependency(link_with : [gstvvaspool], dependencies : [gst_dep, gstbase_dep])
