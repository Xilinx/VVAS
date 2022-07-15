##########################################################################
 # Copyright 2020 - 2022 Xilinx, Inc.
 #
 # Licensed under the Apache License, Version 2.0 (the "License");
 # you may not use this file except in compliance with the License.
 # You may obtain a copy of the License at
 #
 #     http://www.apache.org/licenses/LICENSE-2.0
 #
 # Unless required by applicable law or agreed to in writing, software
 # distributed under the License is distributed on an "AS IS" BASIS,
 # WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 # See the License for the specific language governing permissions and
 # limitations under the License.
###########################################################################

#PLATFORM_FILE := /proj/xbuilds/2022.1_released/internal_platforms/xilinx_zcu104_base_202210_1/xilinx_zcu104_base_202210_1.xpfm

TARGET := hw

XOCCFLAGS := --platform $(PLATFORM_FILE) -t $(TARGET) -s -g 

MULTISCALER_FLAGS := --kernel v_multi_scaler -I. -I./src/hls

.PHONY: clean

all: v_multi_scaler.xo

v_multi_scaler.xo: src/v_multi_scaler.cpp src/v_hresampler.cpp src/v_hscaler.cpp src/v_dma.cpp src/v_csc.cpp src/v_vresampler.cpp src/v_vscaler.cpp
	v++ $(XOCCFLAGS) $(MULTISCALER_FLAGS) -c -o xo/$@ $^

clean:
	$(RM) -r xo/* *_x .Xil sd_card* *.xclbin *.ltx *.log *.info packaged_kernel* tmp_kernel* vivado* pfm_sw dpu_conf.vh
	$(RM) *summary* *.str *.hwh
