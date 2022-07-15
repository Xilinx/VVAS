DESCRIPTION = "Open source image processing and machine learning library"
HOMEPAGE = "http://ermig1979.github.io/Simd/"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=2b3b6a5b8f5050f7edbf74b44d59497a"
#SECTION = "libs"

SRC_URI = "git://github.com/ermig1979/Simd.git \
           file://0001-Add-support-for-cell-size-4.patch \
           file://0002-CMakeLists.txt-Remove-REGEX.patch \
          "
SRCREV = "82e824d849bb1b66937cecca7036f6f6e09b2118"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

S = "${WORKDIR}/git"

inherit cmake

do_configure() {
	cd ${S}/prj/cmake
  cmake . -DTOOLCHAIN="aarch64-xilinx-linux-g++" -DCMAKE_CXX_FLAGS="-O2 -pipe -g -feliminate-unused-debug-types" -DTARGET="aarch64" -DCMAKE_BUILD_TYPE="Release" -DLIBRARY="SHARED"
}

do_compile() {
  cd ${S}/prj/cmake
  make Simd
}

do_install() {
    install -d ${D}${libdir}
    install -d ${D}${includedir}/Simd
    install -m 0644 ${WORKDIR}/git/src/Simd/*.h ${D}${includedir}/Simd/
    install -m 0644 ${WORKDIR}/git/src/Simd/*.hpp ${D}${includedir}/Simd/
    install -m 0644 ${S}/prj/cmake/libSimd.so ${D}${libdir}/
}

SOLIBS = ".so"
FILES_SOLIBSDEV = ""
