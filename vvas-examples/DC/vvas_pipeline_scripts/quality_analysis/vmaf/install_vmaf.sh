########################################################################
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
#########################################################################

#! /bin/bash

#####################################
###### Setup VVAS Environment #######
#####################################

if [[ $PATH == /opt/xilinx/vvas/bin* ]] && \
   [[ $LD_LIBRARY_PATH == /opt/xilinx/vvas/lib* ]] && \
   [[ $PKG_CONFIG_PATH == /opt/xilinx/vvas/lib/pkgconfig* ]] && \
   [[ $GST_PLUGIN_PATH == /opt/xilinx/vvas/lib/gstreamer-1.0* ]]
then
	echo "Already has VVAS environment variables set correctly"
else
	echo "Does not have VVAS environment paths. Setting using /opt/xilinx/vvas/setup.sh"
	source /opt/xilinx/vvas/setup.sh
fi


BASEDIR=$PWD

os_distr=`lsb_release -a | grep "Distributor ID:"`
os_version=`lsb_release -a | grep "Release:"`

echo $os_distr
echo $os_version

cpu_count=`cat /proc/cpuinfo | grep processor | wc -l`

echo CPU = $cpu_count

cp ./patches/* /tmp/

# GStreamer bad package installation
cd /tmp && git clone https://github.com/Xilinx/gst-plugins-bad.git -b xlnx-rebase-v1.18.5
cd gst-plugins-bad
patch -p1 < /tmp/0001-VMAF-integration-in-gst-plugins-bad-1.18.5.patch
mkdir subprojects && cd subprojects && git clone https://github.com/werti/vmaf.git -b v1.3.14-gstreamer && \
cd vmaf &&  patch -p1 < /tmp/0001-Building-the-vmaf-as-dynamic-library.patch
cd /tmp/gst-plugins-bad
CFLAGS='-std=gnu99' meson --prefix=/opt/xilinx/vvas --libdir=lib -Dmediasrcbin=disabled -Dmpegpsmux=disabled build && cd build
ninja && sudo ninja install
retval=$?
if [ $retval -ne 0 ]; then
	echo "Unable to install bad gstreamer plugins ($retval)"
        cd $BASEDIR
	return 1
fi
cd $BASEDIR
rm -rf /tmp/gst-plugins-bad*

#Remove GStreamer plugin cache
rm -rf ~/.cache/gstreamer-1.0/

echo "#######################################################################"
echo "########         VMAF plugin installed successfully          ########"
echo "#######################################################################"
