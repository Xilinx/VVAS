# Vitis™ Video Analytics SDK

## Copyright and license statement
Copyright 2022 Xilinx Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

## View the [Documentation](https://xilinx.github.io/VVAS/)

## Folder Structure

- **vvas-utils** core contains infrastructure libraries for VVAS gstreamer plugins and kernel libraries
- **vvas-gst-plugins** contains VVAS specific gstreamer plugins
- **vvas-accel-sw-libs** contains VVAS specific kernel libs which are supported by VVAS gstreamer infrastructure plugins
- **vvas-accel-hw** contains VVAS HW kernels which can be build with vitis
- **vvas-examples** contains examples for using VVAS stack
- **vvas-platform** contains sample VVAS platforms
- **vvas-xcdr** contains PCIe/Data center platform specific commands/scripts
- **vvas-xrm-plugins contains PCIe/Data Center platform specific plug-ins for load calculations

## Build and install VVAS essentials for embedded solutions:

A helper script, **./build_install_vvas.sh**, is provided in root of this repo to build and install VVAS components.

Step 1 : Source sysroot path if not done already
```
	source <sysroot path>/environment-setup-aarch64-xilinx-linux
```
Step 2 : Build
```
	./build_install_vvas.sh Edge
```
Step 3 : copy VVAS installer to embedded board
```
	scp install/vvas_installer.tar.gz <board ip>:/
```
Step 4 : Install VVAS on embedded board
```
	cd /
	tar -xvf vvas_installer.tar.gz
```
