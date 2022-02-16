#
# Copyright 2022 Xilinx Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#      http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

PLATFORM ?= xilinx_zcu104_vcuDec_vmixHdmiTx_202120_1
CWD=$(shell pwd)
XSA_DIR ?= $(CWD)/vivado
OUTPUT_PATH ?= $(CWD)/platform_repo
XSCT = $(XILINX_VITIS)/bin/xsct
CPU_ARCH=a53
export COMMON_RFS_KRNL_SYSROOT = FALSE

.phony: all

$(PLATOFORM): all
all: $(PLATFORM)

$(PLATFORM): xsa petalinux_proj peta_sysroot pfm
	@echo 'Platform build complete'

xsa:
	$(MAKE) -C vivado PLATFORM=$(PLATFORM)

petalinux_proj: ./vivado/$(PLATFORM).xsa
	$(MAKE) -C petalinux all XSA_DIR=$(XSA_DIR) PLATFORM=$(PLATFORM)

peta_sysroot:
	$(MAKE) -C petalinux sysroot

ifeq ($(COMMON_RFS_KRNL_SYSROOT), TRUE)
pfm:
	$(XSCT) -nodisp -sdx scripts/${PLATFORM}_pfm.tcl ${PLATFORM} ${XSA_DIR} ${OUTPUT_PATH}
	mkdir platform_repo/${PLATFORM}/export/${PLATFORM}/sw/${PLATFORM}/pre-built/
	cp -rf platform_repo/tmp/sw_components/src/${CPU_ARCH}/BOOT.BIN platform_repo/${PLATFORM}/export/${PLATFORM}/sw/${PLATFORM}/pre-built/
else
pfm:
	$(XSCT) -nodisp -sdx scripts/${PLATFORM}_pfm.tcl ${PLATFORM} ${XSA_DIR} ${OUTPUT_PATH}
	mkdir platform_repo/${PLATFORM}/export/${PLATFORM}/sw/${PLATFORM}/pre-built/
	cp -rf platform_repo/tmp/sw_components/src/${CPU_ARCH}/BOOT.BIN platform_repo/${PLATFORM}/export/${PLATFORM}/sw/${PLATFORM}/pre-built/
	cp -rf platform_repo/tmp/sw_components/src/${CPU_ARCH}/xrt/filesystem platform_repo/${PLATFORM}/export/${PLATFORM}/sw/${PLATFORM}/
endif

clean:
	${RM} -r platform_repo
	$(MAKE) -C vivado clean PLATFORM=$(PLATFORM)
	$(MAKE) -C petalinux ultraclean PLATFORM=$(PLATFORM)
