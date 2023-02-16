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

# This script automates the build and deployment of images to the target/Host. If your
# environment is different you may need to make minor changes.
#set -o xtrace
usage()  
{  
  echo "Usage: e.g. $0 TARGET=<PCIe/Edge> [PLATFORM=U30/V70] [ENABLE_XRM=<0/1> ENABLE_PPE=<0/1> USE_SIMD=<0/1>]"
  exit 1
}

INSTALL_ACCEL_SW=true

for ARGUMENT in "$@"
do
  KEY=$(echo $ARGUMENT | cut -f1 -d=)

  KEY_LENGTH=${#KEY}
  VALUE="${ARGUMENT:$KEY_LENGTH+1}"

  export "$KEY"="$VALUE"
done

if [ ! "$TARGET" ]; then
  echo "TARGET is must for compilation"
  usage
fi

if [ ! "$ENABLE_XRM" ]; then
  ENABLE_XRM=0
  echo "Default: XRM is disabled"
fi

if [ ! "$USE_SIMD" ]; then
  USE_SIMD=0
  echo "Default: Use of SIMD library disabled"
fi

if [ ! "$ENABLE_PPE" ]; then
  ENABLE_PPE=1
  echo "Default: Use of PPE is enabled"
fi

#For GST Application use glib based  vvas_core/utils
VVAS_CORE_UTILS='GLIB'

if [ -f "/opt/xilinx/vvas/include/vvas/config.h" ]; then
  echo -e "\nINFO: Deleting previous build and its configuration\n"
  #This gets called only for PCIe
  ./clean_vvas.sh PCIe
fi

# Get the current meson version and update the command
# "meson <builddir>" command should be used as "meson setup <builddir>" since 0.64.0
MesonCurrV=`meson --version`
MesonExpecV="0.64.0"

if [ $(echo -e "${MesonCurrV}\n${MesonExpecV}"|sort -rV |head -1) == "${MesonCurrV}" ];
then
MESON="meson setup"
else
MESON="meson"
fi

# Update the version in meson.build files if required to be updated
vvas_version=$(cat VERSION)

root_meson_build_files=( \
  "./vvas-accel-sw-libs/meson.build" \
  "./vvas-gst-plugins/meson.build"   \
  "./vvas-utils/meson.build"         \
  "./vvas-examples/DC/meson.build"
)

for file in ${root_meson_build_files[@]}; do
  if [ -f "$file" ]; then
    sed -i 's!  version :.*!  version : \x27'"$vvas_version"'\x27,!' $file
  fi
done

if [[ ("$TARGET" = "Edge") || ("$TARGET" = "EDGE") || ("$TARGET" = "edge") ]]; then
  echo "Building for Edge"
  TARGET="EDGE"
  PREFIX="/usr"
  INSTALL_ACCEL_SW=true
elif [[ ("$TARGET" = "Pcie") || ("$TARGET" = "PCIE") || ("$TARGET" = "pcie") || ("$TARGET" = "PCIe") ]]; then
  echo "Building for PCIe HOST"
  TARGET="PCIE"
  LIB="lib"
  PREFIX="/opt/xilinx/vvas/"
else
  echo "TARGET is not supported"
  usage
fi

echo INSTALL_ACCEL_SW = $INSTALL_ACCEL_SW
echo TARGET = $TARGET
echo ENABLE_XRM = $ENABLE_XRM
echo USE_SIMD = $USE_SIMD
echo ENABLE_PPE = $ENABLE_PPE
echo VVAS_CORE_UTILS = $VVAS_CORE_UTILS

set -e

if [[ "$TARGET" == "EDGE" ]]; then #TARGET == EDGE
  if [ -z "$CC" ]; then
  echo "Cross compilation not set - source environment setup first"
  exit
  fi

  if [ $INSTALL_ACCEL_SW = true ]; then
    if [ ! -e "$SDKTARGETSYSROOT/usr/lib/libvart-util.so.3.0.0" ]; then
      echo "Vitis AI 3.0 is not installed in the target sysroot"
      echo "Please install same and come back"
      exit 1
    fi
  fi # close [ $INSTALL_ACCEL_SW = true ]

  rm -rf install

  # Work-arround the dependancy on meson.native in sysroot
  if [ ! -f $OECORE_NATIVE_SYSROOT/usr/share/meson/meson.native ]; then
    touch $OECORE_NATIVE_SYSROOT/usr/share/meson/meson.native;
  fi

  BASEDIR=$PWD
  cd vvas-core
  ./build_install.sh TARGET=$TARGET ENABLE_PPE=$ENABLE_PPE USE_SIMD=$USE_SIMD 

  cd ../vvas-utils
  sed -E 's@<SYSROOT>@'"$SDKTARGETSYSROOT"'@g; s@<NATIVESYSROOT>@'"$OECORE_NATIVE_SYSROOT"'@g' meson.cross.template > meson.cross
  cp meson.cross $OECORE_NATIVE_SYSROOT/usr/share/meson/aarch64-xilinx-linux-meson.cross
  $MESON build --prefix $PREFIX --cross-file $PWD/meson.cross
  cd build
  ninja
  DESTDIR=$SDKTARGETSYSROOT ninja install
  DESTDIR=../../install ninja install

  cd ../../vvas-gst-plugins
  sed -E 's@<SYSROOT>@'"$SDKTARGETSYSROOT"'@g; s@<NATIVESYSROOT>@'"$OECORE_NATIVE_SYSROOT"'@g' meson.cross.template > meson.cross
  cp meson.cross $OECORE_NATIVE_SYSROOT/usr/share/meson/aarch64-xilinx-linux-meson.cross
  $MESON --prefix /usr build --cross-file meson.cross -Denable_ppe=$ENABLE_PPE -Dvvas_core_utils=$VVAS_CORE_UTILS

  cd build
  ninja
  DESTDIR=$SDKTARGETSYSROOT ninja install
  DESTDIR=../../install ninja install

  if [ $INSTALL_ACCEL_SW = true ]; then
    cd ../../vvas-accel-sw-libs
    sed -E 's@<SYSROOT>@'"$SDKTARGETSYSROOT"'@g; s@<NATIVESYSROOT>@'"$OECORE_NATIVE_SYSROOT"'@g' meson.cross.template > meson.cross
    cp meson.cross $OECORE_NATIVE_SYSROOT/usr/share/meson/aarch64-xilinx-linux-meson.cross
    $MESON build --cross-file meson.cross --prefix /usr -Dvvas_core_utils=$VVAS_CORE_UTILS
    cd build
    ninja
    DESTDIR=$SDKTARGETSYSROOT ninja install
    DESTDIR=../../install ninja install
  fi # close [ $INSTALL_ACCEL_SW = true ]

  cd ../../install
  cp -r $BASEDIR/vvas-core/install/usr/ .
  tar -pczvf vvas_installer.tar.gz usr
  cd ..

else # TARGET == PCIE

  if [[ (! "$PLATFORM") || ("$PLATFORM" == "V70") || ("$PLATFORM" == "v70") ]]; then
    PCI_PLATFORM="V70"
  else if [[ ("$PLATFORM" == "U30") || ("$PLATFORM" == "u30") ]]; then
    PCI_PLATFORM="U30"
  else
    PCI_PLATFORM="V70"
  fi
  fi

  BASEDIR=$PWD
  source /opt/xilinx/vvas/setup.sh

  # export inside this file will not affect the terminal environment
  # make sure before running the pipe run “source /opt/xilinx/vvas/setup.sh”
  export LD_LIBRARY_PATH=$PWD/install/opt/xilinx/vvas/lib:$LD_LIBRARY_PATH
  export PKG_CONFIG_PATH=$PWD/install/opt/xilinx/vvas/lib/pkgconfig:$PKG_CONFIG_PATH

  cd $BASEDIR/vvas-core
  ./build_install.sh PLATFORM=$PCI_PLATFORM TARGET=$TARGET ENABLE_PPE=$ENABLE_PPE USE_SIMD=$USE_SIMD

  cd $BASEDIR/vvas-utils
  $MESON build --prefix $PREFIX --libdir $LIB
  cd build
  ninja
  sudo ninja install

  cd $BASEDIR/vvas-gst-plugins
  $MESON build --prefix $PREFIX --libdir $LIB -Denable_xrm=$ENABLE_XRM -Denable_ppe=$ENABLE_PPE -Dpci_platform=$PCI_PLATFORM -Dvvas_core_utils=$VVAS_CORE_UTILS
  cd build
  ninja
  sudo ninja install

  if [ $INSTALL_ACCEL_SW = true ]; then
    cd $BASEDIR/vvas-accel-sw-libs
    $MESON build --prefix $PREFIX --libdir $LIB
    cd build
    ninja
    sudo ninja install
  fi # close $INSTALL_ACCEL_SW = true

fi # close TARGET == PCIE
