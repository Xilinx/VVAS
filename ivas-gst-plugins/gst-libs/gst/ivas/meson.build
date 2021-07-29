# IVAS metadata
meta_sources = ['gstivaslameta.c']

gstivaslameta = library('gstivaslameta-' + api_version,
  meta_sources,
  c_args : gst_plugins_ivas_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
)
gstivaslameta_dep = declare_dependency(link_with : [gstivaslameta], dependencies : [gst_dep, gstbase_dep, gstvideo_dep])

# Extended GstInferenceMeta for IVAS
infermeta_sources = ['gstinferencemeta.c', 'gstinferenceclassification.c', 'gstinferenceprediction.c']

gstivasinfermeta = library('gstivasinfermeta-' + api_version,
  infermeta_sources,
  c_args : gst_plugins_ivas_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, ivasutils_dep],
)
gstivasinfermeta_dep = declare_dependency(link_with : [gstivasinfermeta], dependencies : [gst_dep, gstbase_dep, ivasutils_dep])

#IVAS allocator using XRT
alloc_sources = ['gstivasallocator.c']

gstivasalloc = library('gstivasallocator-' + api_version,
  alloc_sources,
  c_args : gst_plugins_ivas_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, xrt_dep, gstallocators_dep],
)
gstivasalloc_dep = declare_dependency(link_with : [gstivasalloc], dependencies : [gst_dep, gstbase_dep, gstvideo_dep, xrt_dep, gstallocators_dep])

#IVAS bufferpool with stride and elevation
ivaspool_sources = ['gstivasbufferpool.c']

gstivaspool = library('gstivasbufferpool-' + api_version,
  ivaspool_sources,
  c_args : gst_plugins_ivas_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, gstivasalloc_dep],
)
gstivaspool_dep = declare_dependency(link_with : [gstivaspool], dependencies : [gst_dep, gstvideo_dep, gstbase_dep, gstivasalloc_dep])

# IVAS Input Inference metadata
inpinfermeta_sources = ['gstivasinpinfer.c']

gstivasinpinfermeta = library('gstivasinpinfermeta-' + api_version,
  inpinfermeta_sources,
  c_args : gst_plugins_ivas_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
)
gstivasinpinfermeta_dep = declare_dependency(link_with : [gstivasinpinfermeta], dependencies : [gst_dep, gstbase_dep])

gstivasutils = library('gstivasutils', 'gstivasutils.c',
  c_args : gst_plugins_ivas_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
  install : true,
)
gstivasutils_dep = declare_dependency(link_with : [gstivasutils], dependencies : [gst_dep, gstbase_dep, gstvideo_dep])

#IVAS GST Headers to install
ivas_gst_headers = ['gstivaslameta.h',
                    'gstivasallocator.h',
                    'gstivasbufferpool.h',
                    'gstinferencemeta.h',
                    'gstinferenceprediction.h',
                    'gstinferenceclassification.h',
                    'gstivasinpinfer.h',
                    'gstivasutils.h',
                    'gstivascommon.h']

install_headers(ivas_gst_headers, subdir : 'gstreamer-1.0/gst/ivas/')

gstivaspool_dep = declare_dependency(link_with : [gstivaspool], dependencies : [gst_dep, gstbase_dep])
