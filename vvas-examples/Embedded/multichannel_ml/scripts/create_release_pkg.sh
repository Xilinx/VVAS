##########################################################################
 # Copyright 2020 - 2022 Xilinx, Inc.
 # Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
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

#!/bin/bash

########################### Usage Note ########################################
# This script should be called from the parent directory i.e.
# smart_model_select as follows.
#     ./script/create_release_pkg.sh
#
# Before envocation the MODEL_PATH should be set the  to the directory which
# has folder corresponding to each of the model as listed below in the models
# bash array.
###############################################################################

APPNAME=${PWD##*/}
PVERSION=$(grep PETALINUX_VER= ../../../vvas-platforms/Embedded/zcu104_vcuDec_vmixHdmiTx/petalinux/.petalinux/metadata | cut -d'=' -f2)
FOLDERNAME=vvas_${APPNAME}_${PVERSION}_zcu104

models=( \
  "densebox_320_320"  \
  "plate_detect"  \
  "plate_num" \
  "refinedet_pruned_0_96" \
  "resnet50"  \
  "yolov3_adas_pruned_0_9" \
  "yolov3_voc" \
  # Following models added as requested in CR-1148755
  "bcc_pt" \
  "efficientdet_d2_tf" \
  "efficientnet-b0_tf2" \
  "face_landmark" \
  "face_mask_detection_pt" \
  "facerec_resnet20" \
  "sp_net" \
  "ultrafast_pt" \
  "vpgnet_pruned_0_99" \
  "densebox_640_360" \
  "yolov2_voc" \
  "yolov2_voc_pruned_0_77" \
  "mobilenet_v2" \
  "inception_v1" \
  "resnet18" \
  "yolov3_voc_tf" \
  "ssd_adas_pruned_0_95" \
  "ssd_mobilenet_v2" \
  "ssd_traffic_pruned_0_9" \
  "ssd_pedestrian_pruned_0_97" \
  # Following model needed for raw-tensor test
  "resnet_v1_50_tf" \
  # Following modles add as requested in CR-1150531
  "chen_color_resnet18_pt" \
  "vehicle_make_resnet18_pt" \
  "vehicle_type_resnet18_pt"
)

# Check if MODEL_DIR is set correctly
if [ ! -d ${MODEL_DIR}/${models[0]} ]; then
  echo ERROR: Please check if MODEL_DIR is set correctly !
  echo Export the MODEL_DIR before executing this script as follows
  echo
  echo "  export MODEL_DIR=<leading_path>xilinx_model_zoo_DPUCZDX8G_ISA1_B3136_release-dev-Linux/usr/share/vitis_ai_library/models/"
  exit 0;
fi

# Create dir
if [ ! -d $FOLDERNAME ]; then
  mkdir -p ${FOLDERNAME}/models
fi

# Install scripts_n_utils
cp -r src/scripts_n_utils ${FOLDERNAME}/

# Install models
for model in ${models[@]}; do
  if [ ! -d ${MODEL_DIR}/${model} ]; then
    echo "ERROR: ${model} not found";
  fi

  cp -r ${MODEL_DIR}/${model} ${FOLDERNAME}/models
  if [ -f src/labels/label_${model}.json ]; then
    cp src/labels/label_${model}.json ${FOLDERNAME}/models/${model}/label.json
  fi
done

# Workarrounds 
#1 Modify the top_k
sed -i 's!top_k : 5!top_k : 1!' ${FOLDERNAME}/models/resnet50/resnet50.prototxt

# CR-1150440 : Modify top_k to 1 for mobilenet_v2, inception_v1 and resnet18
for ((i=19; i<22; i++)) do
  sed -i 's!top_k : 5!top_k : 1!' ${FOLDERNAME}/models/${models[$i]}/*.prototxt
done

# Fix the top_k in models added as requested in CR-1150531
for ((i=28; i<31; i++)) do
  sed -i 's!top_k : 5!top_k : 1!' ${FOLDERNAME}/models/${models[$i]}/*.prototxt
done
# End - Workarrounds

# Install arch.json
cp binary_container_1/sd_card/arch.json ${FOLDERNAME}/

# Install sdk.sh
cp `pwd`/../../../vvas-platforms/Embedded/zcu104_vcuDec_vmixHdmiTx/platform_repo/tmp/sw_components/sdk.sh ${FOLDERNAME}/

# Install sd_card.img
cp binary_container_1/sd_card.img ${FOLDERNAME}/


# Create a Zip archive of the release package
zip -r ${FOLDERNAME}.zip ${FOLDERNAME}
