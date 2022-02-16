gstvvas_xroigen = library('gstvvas_xroigen', 'gstvvas_xroigen.c',
  c_args : gst_plugins_vvas_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep, gst_dep, gstvvasinfermeta_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstvvas_xroigen, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstvvas_xroigen]
