SUMMARY = "VVAS Core"
DESCRIPTION = "VVAS Core C-library"
SECTION = "multimedia"
LICENSE = "MIT"

include vvas.inc

PV = "1.0+git${SRCPV}"

DEPENDS = "glib-2.0 glib-2.0-native xrt opencv vitis-ai-library vart protobuf jansson ne10 simd"

inherit meson pkgconfig gettext

TARGET_CPPFLAGS:append = " -I=/usr/include/xrt"

S = "${WORKDIR}/git/vvas-core"
B = "${S}/build"

EXTRA_OEMESON += " \
    -Denable_ppe=1 \
    -Dtracker_use_simd=1 \
    -Dvvas_core_utils='GLIB' \
"

GIR_MESON_ENABLE_FLAG = "enabled"
GIR_MESON_DISABLE_FLAG = "disabled"

SOLIBS = ".so"
FILES_SOLIBSDEV = ""
FILES:${PN} += " ${libdir}/*.so ${libdir}/vvas_core/*.so"
FILES:${PN}-dev += " ${includedir}/vvas_core/* ${includedir}/vvas_utils/* ${libdir}/pkgconfig/*.pc"
