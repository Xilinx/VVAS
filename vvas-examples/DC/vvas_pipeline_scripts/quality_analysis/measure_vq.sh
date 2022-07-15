#
# Copyright 2020-2022 Xilinx, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); 
# you may not use this file except in compliance with the License. 
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software 
# distributed under the License is distributed on an "AS IS" BASIS, 
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
# See the License for the specific language governing permissions and 
# limitations under the License.
#

#!/bin/bash

#This file will accept 2 files, a master clip, and an encoded clip. It will calculate PSNR, SSIM, MS-SSIM, VMAF and display 
# the above values on terminal for each frame.

#The only variable in this script you should edit is the $MODEL, which is described on the GitHub

if [ $# -le 4 ]
  then
    echo "Incorrect arguments supplied"
    echo "Usage: ./measure_vq.sh <Encoded Clip> <Resolution ('W'x'H')> <Framerate> <Master YUV clip> [MODEL path for 1080p]"
    echo "Example: ./measure_vq.sh u30_3mpbs_clip.mp4 1920x1080 60 original_clip.yuv vmaf/model/vmaf_v0.6.1.pkl"
    exit 1
fi


DISTORTED=$1
RESOLUTION=$2
FRAMERATE=$3
MASTER=$4
MODEL_PATH=$5

HEIGHT=`echo $RESOLUTION | cut -d x -f2 `
WIDTH=`echo $RESOLUTION | cut -d x -f1`

pipe="gst-launch-1.0 -m filesrc location=$MASTER ! queue ! rawvideoparse  width=$WIDTH height=$HEIGHT framerate=$FRAMERATE/1 ! queue ! vmaf.sink_0  iqa-vmaf  psnr=1 ms-ssim=1 ssim=1  model-filename=$MODEL_PATH name=vmaf ! video/x-raw, format=I420, height=$HEIGHT, width=$WIDTH ! fakesink filesrc location=$DISTORTED ! queue ! qtdemux ! h264parse ! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=0 ! videoconvert ! video/x-raw, format=I420, height=$HEIGHT, width=$WIDTH, framerate=$FRAMERATE/1 ! queue !  vmaf.sink_1 "

$pipe
