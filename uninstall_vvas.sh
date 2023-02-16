########################################################################
# Copyright 2020-2022 Xilinx, Inc.
# Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#########################################################################

#! /bin/bash

##################################################
# This script uninstalls the gstreamer and vvas
# packages if installed from debian package and
# removes the /opt/xilinx/vvas directory
##################################################

VVAS_PKG="xilinx-u30-vvas"
GSTREAMER_PKG="xilinx-u30-gstreamer-1.16.2"
VVAS_INSTALL_DIR="/opt/xilinx/vvas"

os_distr=`lsb_release -a | grep "Distributor ID:"`
is_package_found=0

is_package_installed()
{
  CNT=0
  if [[ $os_distr == *Ubuntu* ]]; then
    CNT=`dpkg -l | grep -i xilinx | grep -c $@`
  else
    CNT=`rpm -qa --qf '%{NAME} %{VENDOR}\n' | grep -i xilinx | grep -c $@`
  fi
  is_package_found=$CNT
}

remove_package()
{
  echo "Removing $@"
  if [[ $os_distr == *Ubuntu* ]]; then
    sudo apt-get purge -y $@
  else
    sudo yum -y remove $@
  fi
}

remove_package_if_installed()
{
  is_package_installed $@
  if [ $is_package_found -ne 0 ]; then
    remove_package $@
  else
    echo "$@ package is not installed"
  fi
}

remove_package_if_installed $VVAS_PKG
remove_package_if_installed $GSTREAMER_PKG

if [ -d $VVAS_INSTALL_DIR ]; then
  echo "Removing $VVAS_INSTALL_DIR"
  sudo rm -rf $VVAS_INSTALL_DIR
fi
