REPO = "git://github.com/Xilinx/XRT.git;protocol=https"
BRANCHARG = "${@['nobranch=1', 'branch=${BRANCH}'][d.getVar('BRANCH', True) != '']}"
SRC_URI = "${REPO};${BRANCHARG}"

BRANCH= "2021.2"
SRCREV= "9006f1204a910a3bd308e085fc651316576a7c84"
PV = "202120.2.12.0"
