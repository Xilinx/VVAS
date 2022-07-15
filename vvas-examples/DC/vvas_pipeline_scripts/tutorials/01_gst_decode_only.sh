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
# This script accepts an 8-bit or 10-bit, YUV420, pre-encoded h264 file and will send a decoded output
# to /tmp/xil_dec_out*.nv12 or /tmp/xil_dec_out*.nv12_10le32 at a rate of 8Mbps using filesink option (i.e. fakesink=0)
# This script also has the option to display the max possible fps values if fakesink=1 as argument. Default value is 0
# This script also supports running multiple instances of same pipeline based on resolution of input stream

# You may change the target codec to HEVC by changing:
#     the h264parse plugin to h265parse plugin for decoding from HEVC

if [ $# -lt 3 ]; then
     echo -e "\e[01;31m"Note : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <device index> <Input H264 file> <Number of decoder instances, 1 to 8>  <Number of buffers> <fakesink 0/1>"
     echo ""
     echo "As per VCU procceing capacity, below are the input limitations."
     echo "Maximum 8 instances if its 1080p30"
     echo "Ex: $0 0 Test_1080p30.h264 8 -1 1"
     echo "Maximum 4 instances if its 1080p60"
     echo "Ex: $0 0 Test_1080p60.h264 4 2000 1"
     echo "Maximux 1 instance if its 4k 60fps"
     echo "Ex: $0 0 Test_4k_60fps.h264 -1 1"
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


#Check if the device index is correct
num_dev=$(xbutil examine  | grep xilinx_u30 | wc -l)
if [ $1 -ge $num_dev ]; then
    echo "Device index is not correct, exiting. possible values are 0 to $((num_dev-1))"
    exit
else
    di=$1
fi
#checking input file is 8 bit or 10 bit 
Width=`gst-discoverer-1.0 -v $2 | grep "Width:" | grep -o '[0-9]*' | cut -d":" -f2 | head `
Height=`gst-discoverer-1.0 -v  $2 | grep "Height:" | grep -o '[0-9]*' | cut -d":" -f2 | head `
bitdep=`gst-discoverer-1.0 -v $2 | grep "Depth" | grep -o '[0-9]*' | cut -d":" -f2 | tail -1 `
extn="nv12"
echo $bitdep
Depth=$(( $bitdep / 3 ))
if [ $Depth == "8" ]
then
    extn="nv12"
else
    extn="nv12_10le32"
fi
echo $Width
echo $Height
echo $Depth


#By default this script generates decoded filesink outputs. Giving 4th argument as 1, script runs for fakesink
if [ -z "$5" ]
then
      echo "No fakesink argument is supplied"
      fakesink=0
else
      echo "fakesink argument is supplied"
      fakesink=$5
fi


#Check if number of instances is given a value of at least <1>
if [ $3 -lt 1 ]; then
    echo "Num instances are not correct. It should be 1 at least. Run $0 for help"
    exit
fi

if [ -z "$4" ]; then
    echo "num-buffers not passed, setting to 2000"
    num=2000
else
    echo "num-buffers defined"
    num=$4
fi



sink=fakesink
d=$3
pipe=" -v"
for (( c=0; c<$d; c++ ))
do
echo "fakesink value is $fakesink"
sinkname="fpsdisplaysink_${c}"
if [ $fakesink == "0" ]
then
	sink="\"filesink location=/tmp/xil_dec_out_${Width}x${Height}_${Depth}_${c}.${extn} \""
    	echo "Script is running for filesink ............"
else
	echo "Script is running for fakesink ............"
fi
pipe="gst-launch-1.0 -v filesrc num-buffers=$num location=$2
	${demuxer}
	! h264parse
	! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di
	! fpsdisplaysink name=$sinkname video-sink=$sink text-overlay=false sync=false "
$pipe &
done
wait
