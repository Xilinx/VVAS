# VVAS Accelerator Software libraries

## Native compilation steps:
---
```
meson --libdir=/usr/lib/ build;
cd build;
ninja;
sudo ninja install;
```

## Steps for Cross Compilation:
1. Copy sdk.sh file to <sdk.sh_folder> on build machine

2. Prepare SYSROOT and set environment variables
```
cd <sdk.sh_folder>
./sdk.sh -d `pwd` -y
```
3. Edit ivas-accel-sw-libs/meson.cross to point to SYSROOT path

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
- Vitis AI 1.4 (https://www.xilinx.com/bin/public/openDownload?filename=vitis_ai_2021.1-r1.4.0.tar.gz)
- ivas-utils and ivas-gst-plugins
```

***Note:***<br />

***Enable/disable accelerator sw libraries/features***
By default, all libraries are enabled in build process. User can selectively enable/disable libraries in build using build option “-D”.<br />
Below is example to disable ivas_xboundingbox library in build process 
```
meson -Divas_xboundingbox=disabled --libdir=/usr/local/lib/ivas/ build;  
```
Also, you can include different model class in build process. To enable/disable the model class use the “-D” option with 0/1: 
meson -DMODELCLASS=0 --libdir=/usr/local/lib/ivas/ build;
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
meson -DYOLOV3=1 -DSSD=1 -DREID=0 -DCLASSIFICATION=1 -DFACEDETECT=1 -DREFINEDET=1 -DTFSSD=1 -DYOLOV2=1 -Divas_xboundingbox=enabled --libdir=/usr/local/lib/ivas/ build;
cd build;
ninja;
sudo ninja install;
```


## Test Setup
---
```
cd ../ivas_xdpuinfer/
Copy resnet model to /usr/share/vitis_ai_library/models/
./cmd_resnet50.sh
```
