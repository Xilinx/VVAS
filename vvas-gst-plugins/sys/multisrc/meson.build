vvas_xmultisrc = library('gstvvas_xmultisrc', 'gstvvas_xmultisrc.cpp',
  cpp_args : [gst_plugins_vvas_args, '-std=c++11'],
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep, gst_dep, dl_dep, jansson_dep, gstallocators_dep, gstvvasalloc_dep, uuid_dep, vvasutils_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(vvas_xmultisrc, install_dir : plugins_pkgconfig_install_dir)
plugins += [vvas_xmultisrc]
