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
# pre-requisite to run this script: source /opt/xilinx/vvas/setup.sh
#This file is the most recommended set of flags to produce a low latency encoded HEVC output that will look best visually
#Please provide a RAW NV12 for the script; you can change the input resolution and framerate with the target-bitrate and framerate, respectively

if [ $# -lt 3 ]; then
     echo -e "\e[01;31m"Note : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <device index> <Input 1080p60 NV12 file> <target-bitrate in kbps>"
     echo ""
     echo "Ex: $0 0 Test_1080p.nv12 10000"
     exit 1
fi

#Check if the device index is correct
num_dev=$(xbutil examine  | grep xilinx_u30 | wc -l)

if [ $1 -ge $num_dev ]; then
    echo "Device index is not correct, exiting, possible values are 0 to $((num_dev-1))"
    exit
else
    di=$1
fi

if [[ $2 == *.nv12 ]] ||  [[ $2 == *.NV12 ]]
then
      echo "correct raw input file"
else
      echo "Incorrect input file, exiting.."
      exit -1
fi


#By default this script generates encoded filesink outputs. Giving 4th argument as 1, script runs for fakesink
if [ -z "$3" ]
then
      echo "No bitrate is specified"
      bitrate=10000
else
      echo "bitrate is specified"
      bitrate=$3
fi


#Check if number of instances is given a value of at least <1>
if [ $3 -lt 1 ]; then
    echo "bitrate should be > 1. Run $0 for help"
    exit
fi

pipe=" -v"
echo "Script is running for filesink ............"
pipe="filesrc location=$2 blocksize=3110400 ! video/x-raw, width=1920, height=1080, format=NV12, framerate=60/1 ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=$di  b-frames=0 gop-length=120 periodicity-idr=120 qp-mode=uniform scaling-list=flat target-bitrate=$3 max-bitrate=$3 ! video/x-h265 ! fpsdisplaysink video-sink=\"filesink location=/tmp/xil_enc_${bitrate}_ll_objective.h265 \" text-overlay=false sync=false "$pipe
pipe="gst-launch-1.0 "$pipe
$pipe
