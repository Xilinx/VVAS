########################################################################
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
#########################################################################

#!/bin/bash

#set -o xtrace

if [ $# -eq 0 ]; then
    echo "Usage: e.g. $0 <PCIe/Edge>"
    exit 1
fi

BASEDIR=$PWD
INSTALL_MISC=true

rm -rf $BASEDIR/install

cd $BASEDIR/vvas-core
rm -rf build meson.cross
rm -rf install

cd $BASEDIR/vvas-gst-plugins
rm -rf build meson.cross

cd $BASEDIR/vvas-accel-sw-libs
rm -rf build meson.cross

cd $BASEDIR/vvas-utils
rm -rf build meson.cross

if [ $INSTALL_MISC = true ]; then
if [ -d "$BASEDIR/vvas-misc/vvas-accel-sw-libs" ];then
cd $BASEDIR/vvas-misc/vvas-accel-sw-libs
rm -rf build meson.cross
fi #close check vvas-accel-sw-libs directory
fi # close INSTALL_MISC = true

cd $BASEDIR

#remove installed vvas lib files from directories, 
#then remove the directories from /opt/xilinx/vvas/
if [ "$1" = "PCIe" ]; then
  for file in $(find "/opt/xilinx/vvas/" -name "*vvas*")
  do
   #echo $file
   if [ -d $file ]; then
    continue
   else
    sudo rm -r $file
   fi #close check to remove only vvas files from directory
  done 

  if [ -d "/opt/xilinx/vvas/lib/vvas_core" ]; then 
   sudo rm -rf /opt/xilinx/vvas/lib/vvas_core
  fi

  for dir in $(find "/opt/xilinx/vvas/" -name "*vvas*")
  do
   #echo $dir
   if [ "$dir" = "/opt/xilinx/vvas/" ]; then
    continue
   else
    sudo rm -r $dir
  fi #remove all the vvas directories ex: vvas_core except /opt/xilinx/vvas
  done
fi
