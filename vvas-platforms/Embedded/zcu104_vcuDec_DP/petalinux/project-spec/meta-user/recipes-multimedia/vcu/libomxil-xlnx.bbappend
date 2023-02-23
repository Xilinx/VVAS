FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = " \
  file://0001-fix-2022.2-Store-initial-GST-display-resolution.patch \
  file://0001-fix-4444-Round-up-display-parameters-before-calling-.patch \
"
