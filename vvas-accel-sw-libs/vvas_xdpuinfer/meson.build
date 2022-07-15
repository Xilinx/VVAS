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

sources = [
  'src/vvas_xdpuinfer.cpp'
]

# ADD YOLOV3
if get_option('YOLOV3') != '0'
  add_project_arguments('-DENABLE_YOLOV3', language : 'c')
  add_project_arguments('-DENABLE_YOLOV3', language : 'cpp')
  yolov3_dep = cc.find_library('vitis_ai_library-yolov3')
  sources += [
    'src/vvas_xyolov3.cpp',
  ]
else
  yolov3_dep = []
endif

# ADD CLASSIFICATION
if get_option('CLASSIFICATION') != '0'
  add_project_arguments('-DENABLE_CLASSIFICATION', language : 'c')
  add_project_arguments('-DENABLE_CLASSIFICATION', language : 'cpp')
  classi_dep = cc.find_library('vitis_ai_library-classification')
  sources += [
    'src/vvas_xclassification.cpp',
  ]
else
  classi_dep = []
endif

# ADD FACEDETECT
if get_option('FACEDETECT') != '0'
  add_project_arguments('-DENABLE_FACEDETECT', language : 'c')
  add_project_arguments('-DENABLE_FACEDETECT', language : 'cpp')
  facedetect_dep = cc.find_library('vitis_ai_library-facedetect')
  sources += [
    'src/vvas_xfacedetect.cpp',
  ]
else
  facedetect_dep = []
endif

#ADD SSD
if get_option('SSD') != '0'
  add_project_arguments('-DENABLE_SSD', language : 'c')
  add_project_arguments('-DENABLE_SSD', language : 'cpp')
  ssd_dep = cc.find_library('vitis_ai_library-ssd')
  sources += [
    'src/vvas_xssd.cpp',
  ]
else
  ssd_dep = []
endif

#ADD REFINEDET
if get_option('REFINEDET') != '0'
  add_project_arguments('-DENABLE_REFINEDET', language : 'c')
  add_project_arguments('-DENABLE_REFINEDET', language : 'cpp')
  refinedet_dep = cc.find_library('vitis_ai_library-refinedet')
  sources += [
    'src/vvas_xrefinedet.cpp',
  ]
else
  refinedet_dep = []
endif

#ADD TFSSD
if get_option('TFSSD') != '0'
  add_project_arguments('-DENABLE_TFSSD', language : 'c')
  add_project_arguments('-DENABLE_TFSSD', language : 'cpp')
  tfssd_dep = cc.find_library('vitis_ai_library-tfssd')
  sources += [
    'src/vvas_xtfssd.cpp',
  ]
else
  tfssd_dep = []
endif

#ADD YOLOV2
if get_option('YOLOV2') != '0'
  add_project_arguments('-DENABLE_YOLOV2', language : 'c')
  add_project_arguments('-DENABLE_YOLOV2', language : 'cpp')
  yolov2_dep = cc.find_library('vitis_ai_library-yolov2')
  sources += [
    'src/vvas_xyolov2.cpp',
  ]
else
  yolov2_dep = []
endif

#ADD SEGMENTATION
if get_option('SEGMENTATION') != '0'
  add_project_arguments('-DENABLE_SEGMENTATION', language : 'c')
  add_project_arguments('-DENABLE_SEGMENTATION', language : 'cpp')
  segmentation_dep = cc.find_library('vitis_ai_library-segmentation')
  sources += [
    'src/vvas_xsegmentation.cpp',
  ]
else
  segmentation_dep = []
endif

# ADD PLATEDETECT
if get_option('PLATEDETECT') != '0'
  add_project_arguments('-DENABLE_PLATEDETECT', language : 'c')
  add_project_arguments('-DENABLE_PLATEDETECT', language : 'cpp')
  platedetect_dep = cc.find_library('vitis_ai_library-platedetect')
  sources += [
    'src/vvas_xplatedetect.cpp',
  ]
else
  platedetect_dep = []
endif

# ADD PLATENUM
if get_option('PLATENUM') != '0'
  add_project_arguments('-DENABLE_PLATENUM', language : 'c')
  add_project_arguments('-DENABLE_PLATENUM', language : 'cpp')
  platenum_dep = cc.find_library('vitis_ai_library-platenum')
  sources += [
    'src/vvas_xplatenum.cpp',
  ]
else
  platenum_dep = []
endif

vartutil_dep = cc.find_library('vart-util')
xnnpp_dep = cc.find_library('vitis_ai_library-xnnpp')
vitisconfig_dep = cc.find_library('vitis_ai_library-model_config')
dputask_dep = cc.find_library('vitis_ai_library-dpu_task')
opencvcore_dep = cc.find_library('opencv_core')

#vitisinc_dir = include_directories('/proj/ipeng3/saurabhs/nobkup/2020_1_sysroot/sysroots/aarch64-xilinx-linux/usr/include/vitis')

vvas_xdpuinfer = library('vvas_xdpuinfer',
  sources,
  cpp_args : [vvas_accel_sw_libs_args, '-std=c++17'],
  include_directories : [configinc],
  dependencies : [gstvideo_dep, gst_dep, xrt_dep, jansson_dep, vvasutils_dep, gstvvas_dep, opencv_dep, opencvcore_dep, vartutil_dep, xnnpp_dep, vitisconfig_dep, dputask_dep, classi_dep, yolov3_dep, facedetect_dep, ssd_dep, tfssd_dep, yolov2_dep, segmentation_dep, refinedet_dep, platedetect_dep, platenum_dep],
  install : true,
)

