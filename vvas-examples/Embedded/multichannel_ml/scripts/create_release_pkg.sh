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
  cp -r ${MODEL_DIR}/${model} ${FOLDERNAME}/models
  if [ -f src/labels/label_${model}.json ]; then
    cp src/labels/label_${model}.json ${FOLDERNAME}/models/${model}/label.json
  fi
done

# Workarrounds 
#1 Modify the top_k
sed -i 's!top_k : 5!top_k : 1!' ${FOLDERNAME}/models/resnet50/resnet50.prototxt
# End - Workarrounds

# Install arch.json
cp $(find -name arch.json -print -quit) ${FOLDERNAME}/

# Install sdk.sh
cp `pwd`/../../../vvas-platforms/Embedded/zcu104_vcuDec_vmixHdmiTx/platform_repo/tmp/sw_components/sdk.sh ${FOLDERNAME}/
# Install arch.json
cp $(find -name arch.json -print -quit) ${FOLDERNAME}/

# Install sd_card.img
cp binary_container_1/sd_card.img ${FOLDERNAME}/


# Create a Zip archive of the release package
#zip -r ${FOLDERNAME}.zip ${FOLDERNAME}
