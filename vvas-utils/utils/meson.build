xrtutil_sources = ['xrt_utils.c']

xrtutils = library('xrtutil',
  xrtutil_sources,
  c_args : vvas_utils_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [xrt_dep, uuid_dep],
)

xrtutils_dep = declare_dependency(link_with : xrtutils, 
  include_directories : [utilsinc],
  dependencies : [xrt_dep])

vvasutil_sources = ['vvas_kernel_utils.c']

vvasutil = library('vvasutil',
  vvasutil_sources,
  c_args : vvas_utils_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [xrt_dep, jansson_dep, xrtutils_dep],
)

#VVAS Common Headers to install
#vvas_common_headers = ['vvas/vvas_kernel.h', 'vvas/vvasmeta.h']
vvas_common_headers = ['vvas/vvas_kernel.h', 'vvas/vvasmeta.h', 'xrt_utils.h', 'vvas/vvaslogs.h']
install_headers(vvas_common_headers, subdir : 'vvas/')

vvasutils_dep = declare_dependency(link_with : vvasutil,
  include_directories : [utilsinc],
  dependencies : [xrt_dep])
