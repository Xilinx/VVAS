
vvas_xpreprocessor = library('vvas_xpreprocessor',
  'src/vvas_xpreprocessor.c',
  c_args : [gst_plugins_vvas_args],
  include_directories : [configinc],
  dependencies : [gstvideo_dep, gst_dep, xrt_dep, jansson_dep, vvasutils_dep, gstvvasinfermeta_dep, opencv_dep],
  install : true,
)

pkgconfig.generate(vvas_xpreprocessor, install_dir : plugins_pkgconfig_install_dir)
