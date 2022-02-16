project('vvas-utils', 'c', 'cpp',
  version : '1.0',
  meson_version : '>= 0.54.1',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

utils_version = meson.project_version()
version_arr = utils_version.split('.')
utils_version_major = version_arr[0].to_int()
utils_version_minor = version_arr[1].to_int()

xrt_req = '>= 2.6' #XRT 2020.1

vvas_utils_args = ['-DHAVE_CONFIG_H']

configinc = include_directories('.')
utilsinc = include_directories('utils')

api_version = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
curversion = utils_version_major * 100 + utils_version_minor
libversion = '@0@.@1@.0'.format(soversion, curversion)

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')
plugins = []

cdata = configuration_data()
cc = meson.get_compiler('c')

cdata.set_quoted('VERSION', utils_version)
cdata.set_quoted('PACKAGE', 'vvas-utils')
cdata.set_quoted('PACKAGE_VERSION', utils_version)
cdata.set_quoted('PACKAGE_NAME', 'VVAS Utility package')
cdata.set_quoted('GETTEXT_PACKAGE', 'vvas-utils-1.0')

#TODO: currently proprietary until release
cdata.set_quoted('LIBDIR', join_paths(get_option('prefix'), get_option('libdir')))
cdata.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))

# supporting 64-bit x86 family & ARM 64-bit compilers only

if host_machine.cpu_family() == 'x86_64'
  message ('compiling for PCIe platform')
  cdata.set('XLNX_PCIe_PLATFORM', 1)
elif host_machine.cpu_family() == 'aarch64'
  message ('compiling for Embedded platform')
  cdata.set('XLNX_EMBEDDED_PLATFORM ', 1)
else
  error('not supporting host machine families other than x86_64 & aarch64')
endif

warning_flags = [
  '-Wmissing-declarations',
  '-Wredundant-decls',
  '-Wwrite-strings',
  '-Winit-self',
  '-Wno-multichar',
  '-Wvla',
  '-Wpointer-arith',
]

warning_c_flags = [
  '-Wmissing-prototypes',
  '-Wdeclaration-after-statement',
  '-Wold-style-definition',
  '-Waggregate-return',
]

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

foreach extra_arg : warning_c_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

check_headers = [
  ['HAVE_STDIO_H', 'stdio.h'],
  ['HAVE_DLFCN_H', 'dlfcn.h'],
]

foreach h : check_headers
  if cc.has_header(h.get(1))
    cdata.set(h.get(0), 1)
  endif
endforeach

# XRT dependency
xrt_dep = dependency('xrt', version : xrt_req, required : true)

# External dependency
dl_dep = cc.find_library('dl', required : true)
uuid_dep = cc.find_library('uuid', required : true)
jansson_dep = dependency('jansson', version : '>= 2.7', required: true)

plugins_pkgconfig_install_dir = join_paths(plugins_install_dir, 'pkgconfig')
if get_option('default_library') == 'shared'
  # If we don't build static plugins there is no need to generate pc files
  plugins_pkgconfig_install_dir = disabler()
endif

python3 = import('python').find_installation()
pkgconfig = import('pkgconfig')

configure_file(output : 'config.h', configuration : cdata)

#include directories
subdir('utils')
subdir('pkgconfig')

run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
