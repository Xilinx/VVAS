#!/bin/bash

# This script automates the build and deployment of images to the target/Host. If your
# environment is different you may need to make minor changes.
#set -o xtrace
usage()  
{  
  echo "Usage: $0 <PCIe/Edge> <enable xrm or not>"
  echo "Usage: e.g. $0 PCIe 1"
  exit 1
}

INSTALL_ACCEL_SW=false
#INSTALL_ACCEL_SW=true

if [ "$#" -lt 1 ]; then
  usage
  exit 1
fi

if [[ ("$1" = "Edge") || ("$1" = "EDGE") || ("$1" = "edge") ]]; then
  echo "Building for Edge"
  TARGET="EDGE"
  PREFIX="/usr"
  INSTALL_ACCEL_SW=true
  ENABLE_PPE=1
  echo "Enabling PPE...."
elif [[ ("$1" = "Pcie") || ("$1" = "PCIE") || ("$1" = "pcie") || ("$1" = "PCIe") ]]; then
  echo "Building for PCIe HOST"
  TARGET="PCIE"
  LIB="lib"
  PREFIX="/opt/xilinx/vvas/"
  ENABLE_XRM=1
  ENABLE_PPE=0
  if [[ ("$2" = 0) ]]; then
    ENABLE_XRM=0
  fi
else
  usage
fi

echo INSTALL_ACCEL_SW = $INSTALL_ACCEL_SW
set -e

if [[ "$TARGET" == "EDGE" ]]; then #TARGET == EDGE
  if [ -z "$CC" ]; then
  echo "Cross compilation not set - source environment setup first"
  exit
  fi

  if [ $INSTALL_ACCEL_SW = true ]; then
    if [ ! -e "$SDKTARGETSYSROOT/usr/lib/libvart-util.so.2.0.0" ]; then
      echo "Vitis AI 2.0 is not installed in the target sysroot"
      echo "Please install same and come back"
      exit 1
    fi
  fi # close [ $INSTALL_ACCEL_SW = true ]

  rm -rf install

  cd vvas-utils
  sed -E 's@<SYSROOT>@'"$SDKTARGETSYSROOT"'@g; s@<NATIVESYSROOT>@'"$OECORE_NATIVE_SYSROOT"'@g' meson.cross.template > meson.cross
  cp meson.cross $OECORE_NATIVE_SYSROOT/usr/share/meson/aarch64-xilinx-linux-meson.cross
  meson build --prefix $PREFIX --cross-file $PWD/meson.cross
  cd build
  ninja
  DESTDIR=$SDKTARGETSYSROOT ninja install
  DESTDIR=../../install ninja install

  cd ../../vvas-gst-plugins
  sed -E 's@<SYSROOT>@'"$SDKTARGETSYSROOT"'@g; s@<NATIVESYSROOT>@'"$OECORE_NATIVE_SYSROOT"'@g' meson.cross.template > meson.cross
  cp meson.cross $OECORE_NATIVE_SYSROOT/usr/share/meson/aarch64-xilinx-linux-meson.cross
  meson --prefix /usr build --cross-file meson.cross -Denable_ppe=$ENABLE_PPE
  cd build
  ninja
  DESTDIR=$SDKTARGETSYSROOT ninja install
  DESTDIR=../../install ninja install

  if [ $INSTALL_ACCEL_SW = true ]; then
    cd ../../vvas-accel-sw-libs
    sed -E 's@<SYSROOT>@'"$SDKTARGETSYSROOT"'@g; s@<NATIVESYSROOT>@'"$OECORE_NATIVE_SYSROOT"'@g' meson.cross.template > meson.cross
    cp meson.cross $OECORE_NATIVE_SYSROOT/usr/share/meson/aarch64-xilinx-linux-meson.cross
    meson build --cross-file meson.cross --prefix /usr
    cd build
    ninja
    DESTDIR=$SDKTARGETSYSROOT ninja install
    DESTDIR=../../install ninja install
  fi # close [ $INSTALL_ACCEL_SW = true ]

  cd ../../install
  tar -pczvf vvas_installer.tar.gz usr
  cd ..

else # TARGET == PCIE

  BASEDIR=$PWD
  source /opt/xilinx/vvas/setup.sh

  # export inside this file will not affect the terminal environment
  # make sure before running the pipe run “source /opt/xilinx/vvas/setup.sh”
  export LD_LIBRARY_PATH=$PWD/install/opt/xilinx/vvas/lib:$LD_LIBRARY_PATH
  export PKG_CONFIG_PATH=$PWD/install/opt/xilinx/vvas/lib/pkgconfig:$PKG_CONFIG_PATH

  cd $BASEDIR/vvas-utils
  meson build --prefix $PREFIX --libdir $LIB
  cd build
  ninja
  sudo ninja install

  cd $BASEDIR/vvas-gst-plugins
  meson build --prefix $PREFIX --libdir $LIB -Denable_xrm=$ENABLE_XRM
  cd build
  ninja
  sudo ninja install

  cd $BASEDIR/vvas-examples/DC
  meson build --prefix $PREFIX --libdir $LIB -Denable_xrm=$ENABLE_XRM
  cd build
  ninja
  sudo ninja install

  if [ $INSTALL_ACCEL_SW = true ]; then
    cd $BASEDIR/vvas-accel-sw-libs
    meson build --prefix $PREFIX --libdir $LIB
    cd build
    ninja
    sudo ninja install
  fi # close $INSTALL_ACCEL_SW = true

fi # close TARGET == PCIE

########################################################################
 # Copyright 2020 - 2021 Xilinx, Inc.
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
