SUMMARY = "VVAS accel sw libs"
DESCRIPTION = "VVAS accelerator libraries"
SECTION = "multimedia"
LICENSE = "Apache-2.0"

include vvas.inc

DEPENDS = "glib-2.0 glib-2.0-native xrt libcap libxml2 bison-native flex-native jansson vvas-utils vvas-gst opencv vitis-ai-library vart"

inherit meson pkgconfig gettext

S = "${WORKDIR}/git/vvas-accel-sw-libs"

GIR_MESON_ENABLE_FLAG = "enabled"
GIR_MESON_DISABLE_FLAG = "disabled"

FILES:${PN} += "${libdir}/vvas/*.so ${libdir}/*.so"
FILES:${PN}-dev = "${includedir}"
