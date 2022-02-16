# VVAS utility for VVAS Gstreamer plugins and VVAS Kernel libs

## Copyright and license statement
Copyright 2022 Xilinx Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.


***Note: We do not suggest to modify vvas-util code.***


# Steps for Cross Compilation for Embedded platforms:
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
3. Edit vvas-utils/meson.cross to point to SYSROOT path

4. Build & Compile VVAS utility
```
meson build --cross-file meson.cross
cd build;
ninja;
```
5. For installing user to copy .so to target in respective locations
