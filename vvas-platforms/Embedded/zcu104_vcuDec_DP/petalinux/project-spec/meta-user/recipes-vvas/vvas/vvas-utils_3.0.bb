SUMMARY = "VVAS util"
DESCRIPTION = "VVAS utils for VVAS SDK"
SECTION = "multimedia"
LICENSE = "Apache-2.0"

include vvas.inc

DEPENDS = "glib-2.0 glib-2.0-native xrt libcap libxml2 bison-native flex-native gstreamer1.0-plugins-base jansson vvas-core"

inherit meson pkgconfig gettext

TARGET_CPPFLAGS:append = " -I=/usr/include/xrt"

S = "${WORKDIR}/git/vvas-utils"

GIR_MESON_ENABLE_FLAG = "enabled"
GIR_MESON_DISABLE_FLAG = "disabled"

FILES:${PN} += " ${libdir}/libvvasutil-*.so ${libdir}/libvvasstructure-*.so"
FILES:${PN}-dev = "${includedir} ${libdir}/pkgconfig/*"

#CVE_PRODUCT = "gstreamer"
