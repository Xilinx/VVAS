gstvvas_xinfer = library('gstvvas_xinfer', 'gstvvas_xinfer.c',
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep, gst_dep, gstvvaspool_dep, gstvvasalloc_dep, dl_dep, jansson_dep, gstallocators_dep, uuid_dep, vvasutils_dep, gstvvasutils_dep, gstvvasinfermeta_dep, math_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstvvas_xinfer, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstvvas_xinfer]
