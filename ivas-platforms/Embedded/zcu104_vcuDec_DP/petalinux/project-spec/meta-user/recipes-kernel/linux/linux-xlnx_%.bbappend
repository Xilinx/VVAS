SRC_URI += "file://bsp.cfg"
KERNEL_FEATURES_append = " bsp.cfg"
SRC_URI += "file://0001-irps5401-fix-driver-issue-for-2021.1.patch"
FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"
