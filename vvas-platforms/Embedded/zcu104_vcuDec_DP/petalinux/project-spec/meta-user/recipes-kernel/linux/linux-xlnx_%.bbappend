FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " file://bsp.cfg \
                   file://0001-misc-xlnx_dpu-support-softmax-40bit-addressing-from-.patch \
"
KERNEL_FEATURES:append = " bsp.cfg"
