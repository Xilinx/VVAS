# VVAS Accelerator Software libraries

## Copyright and license statement
Copyright 2022 Xilinx Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

## Steps for Cross Compilation for Embedded Platforms
:
1. Copy sdk.sh file to <sdk.sh_folder> on build machine

2. Prepare SYSROOT and set environment variables
```
cd <sdk.sh_folder>
./sdk.sh -d `pwd` -y
```
3. Edit vvas-accel-sw-libs/meson.cross to point to SYSROOT path

4. Build & Compile VVAS accelerator libs
```
meson build --cross-file meson.cross
cd build;
ninja;
```
5. For installing user to copy .so to target in respective locations

***Note:***<br />
Following packages need to be available in sysroot :
```
- jansson >= 2.7
- Vitis AI 2.0 (https://www.xilinx.com/bin/public/openDownload?filename=vitis_ai_2021.2-r2.0.0.tar.gz)
- vvas-utils and vvas-gst-plugins
```

***Note:***<br />

***Enable/disable accelerator sw libraries/features***
By default, all libraries are enabled in build process. User can selectively enable/disable libraries in build using build option “-D”.<br />
Below is example to disable vvas_xboundingbox library in build process 
```
meson -Dvvas_xboundingbox=disabled --libdir=/usr/local/lib/vvas/ build;  
```
Also, you can include different model class in build process. To enable/disable the model class use the “-D” option with 0/1: 
meson -DMODELCLASS=0 --libdir=/usr/local/lib/vvas/ build;
where MODELCLASS supported by VVAS are 
```
YOLOV3
SSD
CLASSIFICATION
FACEDETECT 
REFINEDET
TFSSD
YOLOV2
```

***Example to enable all models and accel sw libraries in meson build is***

```
meson -DYOLOV3=1 -DSSD=1 -DREID=0 -DCLASSIFICATION=1 -DFACEDETECT=1 -DREFINEDET=1 -DTFSSD=1 -DYOLOV2=1 -Dvvas_xboundingbox=enabled --libdir=/usr/local/lib/vvas/ build;
cd build;
ninja;
sudo ninja install;
```


## Test Setup
---
```
cd ../vvas_xdpuinfer/
Copy resnet model to /usr/share/vitis_ai_library/models/
./cmd_resnet50.sh
```
