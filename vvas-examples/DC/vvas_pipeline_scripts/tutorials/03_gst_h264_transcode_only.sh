#
# Copyright 2020 - 2022 Xilinx, Inc.
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
# This script accepts an 8-bit and 10-bit, YUV420, pre-encoded h264 file and will send the encoded h.264 output to /tmp/xil_xcode.mp4 at a rate of 8Mbps.
if [ $# -lt 3 ]; then
     echo -e "\e[01;31m"Note : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <device index> <Input H264 file> <Number of transcode instances, 1 to 8>  <Number of buffers> <fakesink 0/1>"
     echo ""
     echo "As per VCU procceing capacity, below are the input limitations."
     echo "Maximum 8 instances if its 1080p30"
     echo "Ex: $0 0 Test_1080p30.h264 8 -1 1"
     echo "Maximum 4 instances if its 1080p60"
     echo "Ex: $0 0 Test_1080p60.h264 4 -1 0"
     echo "Use Separate script for 4K stream"
     exit 1
fi

#check if the input stream is compatiable with this script
container=`gst-discoverer-1.0 $2 | grep "container:" |  cut -d":" -f2`
codec_type=`gst-discoverer-1.0 $2 | grep "video:" | cut -d":" -f2 | awk '{print $1;}'`
echo $container
echo $codec_type
if [ -z "$container" ] && [ -z "$codec_type" ]; then
    echo "Input file is of unknown/unsupported type"
    exit -1
elif [ -z "$container" ] && [ "$codec_type" = "H.264" ]; then
        echo "Input file is of type H.264";
        demuxer="";
elif [ -z "$container" ] && [ "$codec_type" = "H.265" ]; then
        echo "Input file is of type H.265. Invalid for this script";
        exit -1
elif [ $container = "Quicktime" ] && [ "$codec_type" = "H.264" ]; then
        echo "Input file is of type Quicktime H.264";
        demuxer="! qtdemux ! queue"
elif [ $container = "Quicktime" ] && [ "$codec_type" = "H.265" ]; then
        echo "Input file is of type Quicktime H.265. Invalid for this script";
        exit -1
else
    echo "Input file is of unknown/unsupported type"
    exit -1
fi

num_dev=$(xbutil examine  | grep xilinx_u30 | wc -l)

if [ $1 -ge $num_dev ]; then
	echo "Device index is not correct, exiting, possible values are 0 to $((num_dev-1))"
    exit
else
    di=$1
fi

#By default this script generates decoded filesink outputs. Giving 4th argument as 1, script runs for fakesink
if [ -z "$5" ]
then
      echo "No fakesink argument is supplied"
      fakesink=0
else
      echo "fakesink argument is supplied"
      fakesink=$5
fi

if [ -z "$4" ]; then
    echo "num-buffers not passed, setting to 2000"
    num=2000
else
    echo "num-buffers defined"
    num=$4
fi


d=$3

#Check if number of instances is given a value of at least <1> 
if [ $3 -lt 1 ]; then
    echo "Num instances are not correct. It should be 1 at least. Run $0 for help"
    exit
fi
sink="fakesink"
pipe=" -v"
for (( c=0; c<$d; c++ ))
do
echo $c
echo "fakesink value is $fakesink"
sinkname="fpsdisplaysink_${c}"
if [ $fakesink == "0" ]
then
	sink="\"filesink location=/tmp/xil_xcode_out_$c.mp4 \""
	muxer="! qtmux"
        echo "Script is running for filesink ............"
else
	echo "Script is running for fakesink ............"
	muxer=
fi
pipe="gst-launch-1.0 -v filesrc num-buffers=$num location=$2
	${demuxer}
	! h264parse 
	! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di
	! queue 
	! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=$di  target-bitrate=8000 max-bitrate=8000
	! h264parse
       ${muxer}
	! fpsdisplaysink name=$sinkname video-sink=$sink text-overlay=false sync=false "
$pipe &
done
wait
