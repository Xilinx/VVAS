gstivas_xabrscaler = library('gstivas_xabrscaler', 'gstivas_xabrscaler.c',
  c_args : gst_plugins_ivas_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep, gst_dep, gstivasalloc_dep, gstivaspool_dep, xrt_dep, dl_dep, gstallocators_dep, uuid_dep, gstivasinfermeta_dep, xrm_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstivas_xabrscaler, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstivas_xabrscaler]
