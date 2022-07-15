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

# The U30 Video SDK solution supports real-time decoding and encoding of 4k streams with the following notes:
# - The U30 video pipeline is optimized for live-streaming use cases. For 4k streams with bitrates significantly higher than the ones typically used for live streaming, it may not be possible to sustain real-time performance.
# - When decoding 4k streams with a high bitrate, increasing the number of entropy buffers using the num-entropy-buf option can help improve performance
# - When encoding raw video to 4k, set the width and height options to 3840 and 2160 respectively to specify the desired resolution.
# - When encoding 4k streams to H.264, the num-slices option is required to sustain real-time performance. A value of 4 is recommended. 

# This script accepts an 8-bit, pre-encoded HEVC file and will send the encoded H.264 output to /tmp/xil_4k_xcode.mp4 at a rate of 20Mbps.

# You may edit this to enable other output bitrates (target-bitrate and max-bitrate).

# You may change the target codec to HEVC by changing:
#     the h264parse plugin to h265parse plugin for encoding to HEVC

if [ $# -lt 3 ]; then
     echo -e "\e[01;31m"Note : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <Device Index> <Input 4K H265/HEVC file> <Number of transcode instances, 1>  <Number of buffers> <fakesink 0/1>"
     echo "Ex: $0 0 ~/videos/Test_4k_60fps.h265 1 2000 1"
     exit 1
fi
#Handling hevc extension as gst-discoverer-1.0 application APIs are not working fine with hevc extension
inputfile=$2
EXTENSION=`echo "$inputfile" | cut -d'.' -f2`
echo $EXTENSION
if [ $EXTENSION = "hevc" ];then
echo "gst-discoverer-1.0 application APIs are not working fine with hevc extension. So renaming the input file with h265 extension temporarily as an another file"
cp $inputfile /tmp/input.h265
inputfile=/tmp/input.h265
fi


#check if the input stream is compatiable with this script
container=`gst-discoverer-1.0 $inputfile | grep "container:" |  cut -d":" -f2`
codec_type=`gst-discoverer-1.0 $inputfile | grep "video:" | cut -d":" -f2 | awk '{print $1;}'`
width=`gst-discoverer-1.0 -v $inputfile | grep "Width" | cut -d":" -f2 | awk '{print $1;}'`
echo $container
echo $codec_type
echo $width
if [ -z "$container" ] && [ -z "$codec_type" ]; then
    echo "Input file is of unknown/unsupported type"
    exit -1
elif [ -z "$container" ] && [ "$codec_type" = "H.264" ] && [ $width -eq 3840 ]; then
#elif [ -z "$container" ] && [ "$codec_type" = "H.264" ]; then
        echo "Input file is of type H.264. Invalid for this script";
        exit -1
elif [ -z "$container" ] && [ "$codec_type" = "H.265" ]; then
	demuxer=""
        echo "Input file is of type H.265 with 4K";
elif [ $container = "Quicktime" ] && [ "$codec_type" = "H.264" ]; then
        echo "Input file is of type Quicktime H.264. Invalid for this script";
        exit -1
elif [ $container = "Quicktime" ] && [ "$codec_type" = "H.265" ]; then
	demuxer="! qtdemux ! queue"
        echo "Input file is of type Quicktime H.265";
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
if [ $fakesink == "0" ]
then
        sink="\"filesink location=/tmp/xil_xcode_4k_$c.mp4 \""
	muxer="! qtmux"
        echo "Script is running for filesink ............"
else
        echo "Script is running for fakesink ............"
	muxer=
fi

pipe="filesrc location=$inputfile num-buffers=$num
	${demuxer}
	! h265parse
	! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin num-entropy-buf=3  dev-idx=$di 	! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=$di  b-frames=2 target-bitrate=20000 max-bitrate=20000 prefetch-buffer=true num-slices=4 gop-mode=low-delay-p control-rate=2
	! h264parse
	${muxer}
	! fpsdisplaysink video-sink=$sink text-overlay=false sync=false "$pipe

done
pipe="gst-launch-1.0 "$pipe
$pipe

#Removing temporarily created h265 file
if [ $EXTENSION = "hevc" ];then
rm /tmp/input.h265
fi
