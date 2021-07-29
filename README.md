# VVAS

To clone this repo :

git clone https://github.com/Xilinx/VVAS.git

## Folder Structure

- **ivas-utis** core contains infrastructure libraries for VVAS gstreamer plugins and kernel libraries
- **ivas-gst-plugins** contains VVAS specific gstreamer plugins
- **ivas-accel-sw-libs** contains VVAS specific kernel libs which are supported by VVAS gstreamer infrastructure plugins
- **ivas-accel-hw** contains VVAS HW kernels which can be build with vitis
- **ivas-examples** contains examples for using VVAS stack
- **ivas-platform** contains sample VVAS platforms

***Note:*** Compile and installation of VVAS software libraries need to follow below sequence:
```
	First  : ivas-utils
	Second : ivas-gst-plugins
	Third  : ivas-accel-sw-libs
```

## Build and install VVAS essentials for embedded:
Following are the three VVAS folders which are essential to build the VVAS software stack for any platform:
```
	ivas-utils, ivas-gst-plugins and ivas-accel-sw-libs
```
A helper script is provided in root of this repo to build and install VVAS essentials for embedded devices.

Step 1 : Source sysroot
```
	source <sysroot path>/environment-setup-aarch64-xilinx-linux
```
Step 2 : Build
```
	./build-ivas-essential.sh Edge
```
Step 3 : copy VVAS installer to embedded board
```
	scp install/ivas_installer.tar.gz <board ip>:/
```
Step 4 : Install VVAS on embedded board
```
	cd /
	tar -xvf ivas_installer.tar.gz
```
## Steps to install meson:
VVAS build system uses meson to build, following are the steps to install meson locally:

```
sudo apt-get install python3 python3-pip python3-setuptools python3-wheel ninja-build
pip3 install meson 
pip3 install ninja
Above steps installs meson in  /home/<username>/.local/bin/meson, so change PATH env to point using
export PATH= /home/<username>/.local/bin/meson:$PATH
```
***Note:*** In case pip3 is not installed in system run the following command
```
python3 -m pip install --user --upgrade pip==20.2.2
```
