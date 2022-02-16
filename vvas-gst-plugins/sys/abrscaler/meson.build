gstvvas_xabrscaler = library('gstvvas_xabrscaler', 'gstvvas_xabrscaler.c',
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep, gst_dep, gstvvasalloc_dep, gstvvaspool_dep, dl_dep, gstallocators_dep, uuid_dep, gstvvasinfermeta_dep, gstvvashdrmeta_dep, xrm_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstvvas_xabrscaler, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstvvas_xabrscaler]
