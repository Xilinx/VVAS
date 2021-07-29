echo "sourcing Init script..."
SCRIPTPATH4=$(dirname $BASH_SOURCE)
echo ${SCRIPTPATH4}
cp ${SCRIPTPATH4}/platform_desc.txt /etc/xocl.txt
export XILINX_XRT=/usr
cp /mnt/sd-mmcblk0p1/ivas_pack/libs/lib/* /usr/lib/
cp /mnt/sd-mmcblk0p1/ivas_pack/libs/gstreamer/* /usr/lib/gstreamer-1.0/
cp /mnt/sd-mmcblk0p1/dpu.xclbin /usr/lib/

#cp /mnt/sd-mmcblk0p1/ivas_pack/models/dpu_densebox_320_320.elf /usr/share/vitis_ai_library/models/densebox_320_320/densebox_320_320.elf
#cp /mnt/sd-mmcblk0p1/ivas_pack/models/dpu_densebox_360_640.elf /usr/share/vitis_ai_library/models/densebox_640_360/densebox_640_360.elf
#cp /mnt/sd-mmcblk0p1/ivas_pack/models/densebox_320_320.prototxt /usr/share/vitis_ai_library/models/densebox_320_320/densebox_320_320.prototxt
#cp /mnt/sd-mmcblk0p1/ivas_pack/models/densebox_640_360.prototxt /usr/share/vitis_ai_library/models/densebox_640_360/densebox_640_360.prototxt
#cp /media/sd-mmcblk0p1/ivas_pack/models/dpu_refinedet_pruned_0_96_0.elf /usr/share/vitis_ai_library/models/refinedet_pruned_0_96/refinedet_pruned_0_96.elf
#cp /mnt/sd-mmcblk0p1/ivas_pack/models/dpu_resnet50_0.elf /usr/share/vitis_ai_library/models/resnet50/resnet50.elf
cp /mnt/sd-mmcblk0p1/ivas_pack/models/dpu_yolov3_voc.elf /usr/share/vitis_ai_library/models/yolov3_voc/yolov3_voc.elf
cp /mnt/sd-mmcblk0p1/ivas_pack/models/label.json /usr/share/vitis_ai_library/models/yolov3_voc/


cd /media/sd-mmcblk0p1/dpu_sw_optimize/zynqmp/
./zynqmp_dpu_optimize.sh

cd /media/sd-mmcblk0p1/

cp dpu.xclbin /usr/lib/

/sbin/devmem 0xfd360004 32 0x7
/sbin/devmem 0xfd360008 32 0x0
/sbin/devmem 0xfd360018 32 0x7
/sbin/devmem 0xfd36001c 32 0x0
/sbin/devmem 0xfd370004 32 0x7
/sbin/devmem 0xfd370008 32 0x0
/sbin/devmem 0xfd370018 32 0x7
/sbin/devmem 0xfd37001c 32 0x0
/sbin/devmem 0xfd380004 32 0x7
/sbin/devmem 0xfd380008 32 0x0
/sbin/devmem 0xfd380018 32 0x7
/sbin/devmem 0xfd38001c 32 0x0
/sbin/devmem 0xfd390004 32 0x7
/sbin/devmem 0xfd390008 32 0x0
/sbin/devmem 0xfd390018 32 0x7
/sbin/devmem 0xfd39001c 32 0x0
/sbin/devmem 0xfd3a0004 32 0x7
/sbin/devmem 0xfd3a0008 32 0x0
/sbin/devmem 0xfd3a0018 32 0x7
/sbin/devmem 0xfd3a001c 32 0x0
/sbin/devmem 0xfd3b0004 32 0x7
/sbin/devmem 0xfd3b0008 32 0x0
/sbin/devmem 0xfd3b0018 32 0x7
/sbin/devmem 0xfd3b001c 32 0x0
########################################################################
 # Copyright 2020 Xilinx, Inc.
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
##########################################################################
