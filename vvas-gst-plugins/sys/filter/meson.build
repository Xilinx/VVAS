gstvvas_xfilter = library('gstvvas_xfilter', 'gstvvas_xfilter.c',
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep, gst_dep, gstvvasalloc_dep, gstvvaspool_dep, dl_dep, jansson_dep, gstallocators_dep, uuid_dep, vvasutils_dep, gstvvasutils_dep, xrm_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstvvas_xfilter, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstvvas_xfilter]
