# VVAS utility for VVAS Gstreamer plugins and VVAS Kernel libs

***We dont't suggest to modify ivas-util code.***

# Native compilation steps:

```
meson --prefix=/opt/xilinx/ivas --libdir=lib build;
cd build;
ninja;
sudo ninja install;
```

# Steps for Cross Compilation:
1. Copy sdk.sh file to <sdk.sh_folder> on build machine

2. Prepare SYSROOT and set environment variables
```
cd <sdk.sh_folder>
./sdk.sh -d `pwd` -y
```

***Note: Following packages need to be available in sysroot :***
```
- jansson >= 2.7
```
3. Edit ivas-utils/meson.cross to point to SYSROOT path

4. Build & Compile VVAS utility
```
meson build --cross-file meson.cross
cd build;
ninja;
```
5. For installing user to copy .so to target in respective locations
