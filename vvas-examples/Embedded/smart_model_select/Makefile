# (C) Copyright 2020 - 2021 Xilinx, Inc.
# Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: Apache-2.0

ifndef DIR_PRJ
 DIR_PRJ = $(shell pwd)
endif
ifndef HW_ACCEL_PATH
 HW_ACCEL_PATH = ../..
endif
ifndef DPU_TRD_PATH
 DPU_TRD_PATH = ../..
endif

VIVADO_ROOT := $(XILINX_VIVADO)

RM = rm -f
RMDIR = rm -rf

VIVADO:=${VIVADO_ROOT}/bin/vivado
MPSOC_CXX:=aarch64-linux-gnu-g++
TARGET := hw
DEVICE := mpsoc

PLATFORM_DIR = $(shell dirname ${PLATFORM})
ROOTFS_FILE = ${PLATFORM_DIR}/sw/*/filesystem/rootfs.ext4
LINUX_IMAGE_FILE = ${PLATFORM_DIR}/sw/*/xrt/image/Image

XOCC_OPTS = -t ${TARGET} --platform ${PLATFORM} --save-temps --config ${DIR_PRJ}/prj_conf/prj_config --xp param:compiler.userPostSysLinkOverlayTcl=${DIR_PRJ}/prj_conf/strip_interconnects.tcl 
dpu_HDLSRCS= ${DIR_PRJ}/kernel_xml/dpu/kernel.xml\
	     ${DIR_PRJ}/scripts/package_dpu_kernel.tcl\
	     ${DIR_PRJ}/scripts/gen_dpu_xo.tcl\
	     ${DIR_PRJ}/dpu_conf.vh\
	     ${DPU_TRD_PATH}/dpu_ip/Vitis/dpu/hdl/DPUCZDX8G.v\
	     ${DPU_TRD_PATH}/dpu_ip/Vitis/dpu/inc/arch_def.vh\
	     ${DPU_TRD_PATH}/dpu_ip/Vitis/dpu/xdc/*.xdc\
	     ${DPU_TRD_PATH}/dpu_ip/DPUCZDX8G_*/hdl/DPUCZDX8G_*_dpu.sv\
	     ${DPU_TRD_PATH}/dpu_ip/DPUCZDX8G_*/inc/function.vh\
	     ${DPU_TRD_PATH}/dpu_ip/DPUCZDX8G_*/inc/arch_para.vh

image_processing_SRCS= ${HW_ACCEL_PATH}/image_processing/src/image_processing.cpp\
	     ${HW_ACCEL_PATH}/image_processing/src/v_hresampler.cpp\
	     ${HW_ACCEL_PATH}/image_processing/src/v_hscaler.cpp\
	     ${HW_ACCEL_PATH}/image_processing/src/v_dma.cpp\
	     ${HW_ACCEL_PATH}/image_processing/src/v_csc.cpp\
	     ${HW_ACCEL_PATH}/image_processing/src/v_vresampler.cpp\
	     ${HW_ACCEL_PATH}/image_processing/src/v_vscaler.cpp

dpu_TCL=${DIR_PRJ}/scripts/gen_dpu_xo.tcl

# Kernel name must match kernel name in kernel.xml
DPU_KERN_NAME = DPUCZDX8G
IP_KERN_NAME = image_processing

IMAGE_PROCESSING_FLAGS := --kernel $(IP_KERN_NAME) -I. -I${HW_ACCEL_PATH}/image_processing/src/hls

kernel_xo = binary_container_1/dpu.xo binary_container_1/image_processing.xo

.PHONY: all clean package

all : binary_container_1/dpu.xclbin package

binary_container_1/dpu.xo: $(dpu_HDLSRCS)
	@mkdir -p $(@D)
	-@$(RM) $@
	$(VIVADO) -mode batch -source $(dpu_TCL) -notrace -tclargs $@ $(DPU_KERN_NAME) ${TARGET} ${DEVICE}

image_processing.cfg: image_processing_config.h
	./create_imgproc_config.sh

binary_container_1/image_processing.xo: $(image_processing_SRCS) image_processing.cfg
	v++ --platform $(PLATFORM) -t $(TARGET) -s $(IMAGE_PROCESSING_FLAGS) -c -o $@ $^

binary_container_1/dpu.xclbin: $(kernel_xo)
	v++ $(XOCC_OPTS) -l --temp_dir binary_container_1 --log_dir binary_container_1/logs --remote_ip_cache binary_container_1/ip_cache -o "$@" $(+)

package: image_processing.cfg
	v++ -t ${TARGET} --platform ${PLATFORM} -p binary_container_1/dpu.xclbin  -o dpu.xclbin --package.out_dir binary_container_1 --package.rootfs ${ROOTFS_FILE} --package.sd_file ${LINUX_IMAGE_FILE} --package.sd_file image_processing.cfg 
	cp ./binary_*/link/vivado/vpl/prj/prj*/sources_1/bd/*/hw_handoff/*.hwh ./binary_*/sd_card
	cp ./binary_*/link/vivado/vpl/prj/prj.gen/sources_1/bd/*/ip/*_DPUCZDX8G_1_0/arch.json ./binary_*/sd_card

clean:
	${RM} *.o *.elf *.log *.jou sample* v++* *.xclbin *.xclbin* *.cfg
	${RMDIR} binary_container_1/ packaged_*/ tmp_*/ .Xil/ _x/

