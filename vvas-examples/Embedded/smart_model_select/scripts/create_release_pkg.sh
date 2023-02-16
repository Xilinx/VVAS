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
PVERSION=$(grep PETALINUX_VER= ../../../vvas-platforms/Embedded/zcu104_vcuDec_DP/petalinux/.petalinux/metadata | cut -d'=' -f2)
FOLDERNAME=vvas_${APPNAME}_${PVERSION}_zcu104

models=( \
  "resnet50"  \
  "resnet18"  \
  "mobilenet_v2"  \
  "inception_v1"  \
  "ssd_adas_pruned_0_95"  \
  "ssd_traffic_pruned_0_9"  \
  "ssd_mobilenet_v2"  \
  "ssd_pedestrian_pruned_0_97"  \
  "plate_detect"  \
  "yolov3_voc_tf"  \
  "yolov3_adas_pruned_0_9"  \
  "refinedet_pruned_0_96"  \
  "yolov2_voc"  \
  "yolov2_voc_pruned_0_77"  \
  "densebox_320_320"  \
  "densebox_640_360"  \
  # Following models added as requested in CR-1148755
  "bcc_pt"  \
  "efficientdet_d2_tf"  \
  "efficientnet-b0_tf2"  \
  "face_landmark"  \
  "face_mask_detection_pt"  \
  "facerec_resnet20"  \
  "plate_num"  \
  "sp_net"  \
  "ultrafast_pt"  \
  "vpgnet_pruned_0_99"  \
  # Following model needed for raw-tensor test
  "resnet_v1_50_tf" \
  # Following modles added as requested in CR-1150531
  "chen_color_resnet18_pt" \
  "vehicle_make_resnet18_pt" \
  "vehicle_type_resnet18_pt"
)

# Check if MODEL_DIR is set correctly
if [ ! -d ${MODEL_DIR}/${models[0]} ]; then
  echo ERROR: Please check if MODEL_DIR is set correctly !
  echo Export the MODEL_DIR before executing this script as follows
  echo
  echo "  export MODEL_DIR=<leading_path>xilinx_model_zoo_zcu102_zcu104_kv260_all-2.0.0-Linux/usr/share/vitis_ai_library/models/"
  exit 0;
fi

# Create dir
if [ ! -d $FOLDERNAME ]; then
  mkdir -p ${FOLDERNAME}/app/jsons ${FOLDERNAME}/models
fi

# Install kernel json files
cp src/jsons/kernel*.json ${FOLDERNAME}/app/jsons/
cp src/jsons/metaconvert_config.json ${FOLDERNAME}/app/jsons/

# Install setup.sh
cp src/setup.sh ${FOLDERNAME}/app/

# Install app
cp src/smart_model_select ${FOLDERNAME}/app/

# Install app templates
cp -r src/templates ${FOLDERNAME}/app/

# Install models
for model in ${models[@]}; do
  if [ ! -d ${MODEL_DIR}/${model} ]; then
    echo "ERROR: ${model} not found";
  fi

  cp -r ${MODEL_DIR}/${model} ${FOLDERNAME}/models
  if [ -f src/jsons/label_${model}.json ]; then
    cp src/jsons/label_${model}.json ${FOLDERNAME}/models/${model}/label.json
  fi
done

# Fix the top_k in classification models to work-arround the bbox issue.
for ((i=0; i<4; i++)) do
  sed -i 's!top_k : 5!top_k : 1!' ${FOLDERNAME}/models/${models[$i]}/*.prototxt
done

# Fix the top_k in models added as requested in CR-1150531
for ((i=27; i<30; i++)) do
  sed -i 's!top_k : 5!top_k : 1!' ${FOLDERNAME}/models/${models[$i]}/*.prototxt
done

# Install arch.json
cp binary_container_1/sd_card/arch.json ${FOLDERNAME}/

# Install sdk.sh
cp `pwd`/../../../vvas-platforms/Embedded/zcu104_vcuDec_DP/platform_repo/tmp/sw_components/sdk.sh ${FOLDERNAME}/

# Install sd_card.img
cp binary_container_1/sd_card.img ${FOLDERNAME}/


# Create a Zip archive of the release package
zip -r ${FOLDERNAME}.zip ${FOLDERNAME}
