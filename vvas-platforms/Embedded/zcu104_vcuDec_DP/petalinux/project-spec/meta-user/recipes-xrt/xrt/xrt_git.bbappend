REPO ?= "git://github.com/Xilinx/XRT.git;protocol=https"
BRANCHARG = "${@['nobranch=1', 'branch=${BRANCH}'][d.getVar('BRANCH', True) != '']}"
SRC_URI = "${REPO};${BRANCHARG}"

BRANCH= "2022.2"
SRCREV= "d6130b0b762eddb7ac32a24f09c28e5719d4bb1e"
PV = "202220.2.14.0"
