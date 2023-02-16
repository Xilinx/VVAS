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

#! /bin/bash

#####################################
###### Setup VVAS Environment #######
#####################################
sudo rm -rf /opt/xilinx/vvas
sudo mkdir -p /opt/xilinx/vvas/lib/pkgconfig
sudo mkdir -p /opt/xilinx/vvas/bin
sudo cp -rf setup.sh /opt/xilinx/vvas/

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

cpu_count=`cat /proc/cpuinfo | grep processor | wc -l`

echo CPU = $cpu_count

retval=$?
if [ $retval -ne 0 ]; then
	echo "Unable to uninstall package(s) ($retval)"
	return 1
fi

os_distr=`lsb_release -a | grep "Distributor ID:"`
os_version=`lsb_release -a | grep "Release:"`

echo $os_distr
echo $os_version

if [[ $os_distr == *Ubuntu* ]]; then
	OS_TYPE="UBUNTU"
        if [[ $os_version =~ .*18.04.* ]]; then
		OS_VERSION="18_04"
        elif [[ $os_version =~ .*20.04.* ]]; then
		OS_VERSION="20_04"
        else
                echo "Unsupported OS version"
                return 1
        fi
elif [[ $os_distr == *RedHatEnterpriseServer* ]]; then
	OS_TYPE="RHEL"
        if [[ $os_version == *7.8* ]]; then
		OS_VERSION="7_8"
                echo "OS version : $os_version"
        else
                echo "Unsupported OS version"
                return 1
        fi
elif [[ $os_distr == *Amazon* ]]; then
	OS_TYPE="AMAZON"
        if [[ $os_version == *2* ]]; then
		OS_VERSION="2"
                echo "OS version : $os_version"
        else
                echo "Unsupported OS version"
                return 1
        fi
elif [[ $os_distr == *CentOS* ]]; then
	OS_TYPE="CENTOS"
        if [[ $os_version == *7* ]]; then
		OS_VERSION="7"
                echo "OS version : $os_version"
        else
                echo "Unsupported OS version"
                return 1
        fi
else
        echo "Unsupported OS type"
        return 1
fi

if [[ $OS_TYPE == "UBUNTU" ]]; then
	sudo apt-get update
	sudo apt-get install -y build-essential git autoconf autopoint libtool bison flex yasm \
		 libssl-dev python3 python3-pip python3-setuptools python3-wheel \
		 ninja-build cmake libxext-dev libpango1.0-dev libgdk-pixbuf2.0-dev libavfilter-dev
	sudo apt-get purge -y libjansson-dev
	cd /tmp/ && git clone https://github.com/akheron/jansson.git && cd jansson
	git checkout tags/v2.14 -b 2.14
	mkdir build && cd build
	cmake -DJANSSON_BUILD_SHARED_LIBS=1 -DCMAKE_INSTALL_PREFIX:PATH=/opt/xilinx/vvas ..
	make && sudo make install
	cd $BASEDIR
	rm -rf /tmp/jansson*
	if [[ $os_version =~ .*18.04.* ]]; then
                OS_VERSION="18_04"
                sudo apt-get install -y libpangocairo-1.0-0
        elif [[ $os_version =~ .*20.04.* ]]; then
                OS_VERSION="20_04"
                sudo apt-get install -y libpangocairo-1.0-0 librust-pangocairo-dev
        else
                echo "Unsupported OS version"
                return 1
        fi

	retval=$?
	if [ $retval -ne 0 ]; then
		echo "Unable to install package(s) ($retval)"
		return 1
	fi
elif [[ $OS_TYPE == "RHEL" ]]; then
	sudo yum install -y http://repo.okay.com.mx/centos/7/x86_64/release/okay-release-1-1.noarch.rpm
	sudo yum install -y automake glib glib2-devel openssl-devel openssl-devel xorg-x11-server-devel \
		libssl-dev openssl openssl-devel yasm-devel  python3 python3-pip python3-setuptools python3-wheel jansson-devel ninja-build \
		pango pango-devel cairo-devel gdk-pixbuf2-devel
elif [[ $OS_TYPE == "AMAZON" ]]; then
	sudo yum install -y http://repo.okay.com.mx/centos/7/x86_64/release/okay-release-1-1.noarch.rpm
	sudo yum-config-manager --save --setopt=okay.skip_if_unavailable=true
	
	cd /tmp/ && wget http://ftp.gnu.org/gnu/automake/automake-1.14.tar.gz --no-check-certificate && \
	tar xvzf automake-1.14.tar.gz && cd automake-1.14 && \
	./configure
	make && sudo make install
	cd $BASEDIR
	rm -rf /tmp/automake*
	
	sudo yum install -y automake glib glib2-devel openssl-devel openssl-devel xorg-x11-server-devel \
		libssl-dev openssl openssl-devel yasm-devel  python3 python3-pip python3-setuptools python3-wheel jansson-devel ninja-build \
		pango pango-devel cairo-devel gdk-pixbuf2-devel gettext-devel flex bison
elif [[ $OS_TYPE == "CENTOS" ]]; then
	sudo yum install -y http://repo.okay.com.mx/centos/7/x86_64/release/okay-release-1-1.noarch.rpm
	sudo yum-config-manager --save --setopt=okay.skip_if_unavailable=true
	
	cd /tmp/ && wget http://ftp.gnu.org/gnu/automake/automake-1.14.tar.gz --no-check-certificate && \
	tar xvzf automake-1.14.tar.gz && cd automake-1.14 && \
	./configure
	make && sudo make install
	cd $BASEDIR
	rm -rf /tmp/automake*
	
	sudo yum install -y automake glib glib2-devel openssl-devel openssl-devel xorg-x11-server-devel \
		libssl-dev openssl openssl-devel yasm-devel  python3 python3-pip python3-setuptools python3-wheel jansson-devel ninja-build \
		pango pango-devel cairo-devel gdk-pixbuf2-devel gettext-devel flex bison
else
	echo "Unsupported OS type $OS_TYPE"
	return 1
fi

######## Install Meson ###########
if [[ $OS_TYPE == "UBUNTU" ]]; then
	#pip3 install meson
	#export PATH=~/.local/bin:$PATH
	#elif [[ $OS_TYPE == "RHEL" ]]; then
    pip3 list | grep meson
    if [ $? != 0 ]; then
	    sudo pip3 install meson
    fi
elif [[ $OS_TYPE == "RHEL" ]]; then
    pip3 list | grep meson
    if [ $? != 0 ]; then
		sudo pip3 install meson
		if [ ! -f /usr/bin/ninja ]; then
			sudo ln -s /usr/local/bin/ninja /usr/bin/ninja
		fi
    fi
elif [[ $OS_TYPE == "CENTOS" ]]; then
	#pip3 install meson --user
	#pip3 install ninja --user
	#export PATH=~/.local/bin:$PATH
    pip3 list | grep meson
    if [ $? != 0 ]; then
	    sudo pip3 install meson
    fi
    pip3 list | grep ninja 
    if [ $? != 0 ]; then
	    sudo pip3 install ninja
    fi
	export PATH=/usr/local/bin:$PATH
elif [[ $OS_TYPE == "AMAZON" ]]; then
	#pip3 install meson --user
	#pip3 install ninja --user
	#export PATH=~/.local/bin:$PATH
    pip3 list | grep meson
    if [ $? != 0 ]; then
	    sudo pip3 install meson
    fi
    pip3 list | grep ninja 
	if [ $? != 0 ]; then
		sudo pip3 install ninja
		if [ ! -f /usr/bin/ninja ]; then
			sudo ln -s /usr/local/bin/ninja /usr/bin/ninja
		fi
    fi
	export PATH=/usr/local/bin:$PATH
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

cd $BASEDIR

# GStreamer core package installation
cd /tmp/ && git clone https://github.com/Xilinx/gstreamer.git -b xlnx-rebase-v1.18.5
cd gstreamer
$MESON --prefix=/opt/xilinx/vvas --libdir=lib build && cd build
ninja && sudo ninja install
retval=$?
if [ $retval -ne 0 ]; then
	echo "Unable to install gstreamer core package ($retval)"
	cd $BASEDIR
	return 1
fi

cd $BASEDIR
rm -rf /tmp/gstreamer*

# GStreamer base package installation with patch
cd /tmp && git clone https://github.com/Xilinx/gst-plugins-base.git -b xlnx-rebase-v1.18.5
cd gst-plugins-base
CFLAGS='-std=gnu99' $MESON --prefix=/opt/xilinx/vvas --libdir=lib build && cd build
ninja && sudo ninja install
retval=$?
if [ $retval -ne 0 ]; then
	echo "Unable to install base gstreamer plugins ($retval)"
	cd $BASEDIR
	return 1
fi
cd $BASEDIR
rm -rf /tmp/gst-plugins-base*

# GStreamer good package installation
cd /tmp && git clone https://github.com/Xilinx/gst-plugins-good.git -b xlnx-rebase-v1.18.5
cd gst-plugins-good
CFLAGS='-std=gnu99' $MESON --prefix=/opt/xilinx/vvas --libdir=lib build && cd build
ninja && sudo ninja install
retval=$?
if [ $retval -ne 0 ]; then
	echo "Unable to install good gstreamer plugins ($retval)"
        cd $BASEDIR
	return 1
fi
cd $BASEDIR
rm -rf /tmp/gst-plugins-good*

# GStreamer bad package installation
cd /tmp && git clone https://github.com/Xilinx/gst-plugins-bad.git -b xlnx-rebase-v1.18.5
cd gst-plugins-bad
CFLAGS='-std=gnu99' $MESON --prefix=/opt/xilinx/vvas --libdir=lib -Dmediasrcbin=disabled -Dmpegpsmux=disabled build && cd build
ninja && sudo ninja install
retval=$?
if [ $retval -ne 0 ]; then
	echo "Unable to install bad gstreamer plugins ($retval)"
        cd $BASEDIR
	return 1
fi
cd $BASEDIR
rm -rf /tmp/gst-plugins-bad*

# GStreamer libav package installation
cd /tmp && git clone https://github.com/GStreamer/gst-libav.git
cd gst-libav
git checkout  tags/1.18.5 -b 1.18.5
CFLAGS='-std=gnu99' $MESON --prefix=/opt/xilinx/vvas --libdir=lib build && cd build
ninja && sudo ninja install
retval=$?
if [ $retval -ne 0 ]; then
	echo "Unable to install base gstreamer libav ($retval)"
	cd $BASEDIR
	return 1
fi
cd $BASEDIR
rm -rf /tmp/gst-libav


#Remove GStreamer plugin cache
rm -rf ~/.cache/gstreamer-1.0/

echo "#######################################################################"
echo "########         GStreamer setup completed successful          ########"
echo "#######################################################################"
