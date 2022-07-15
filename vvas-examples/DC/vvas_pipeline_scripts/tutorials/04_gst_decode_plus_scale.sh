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

# This script accepts an 8-bit and 10-bit, NV12, pre-encoded 60FPS MP4 file with H.264 content or H.264 elementary stream. It will scale this input into multiple renditions of various sizes, and send them back to disk in /tmp/

# The 1080p60 input is scaled down to the following resolutions and framerates (respectively):
# 720p60
# 720p30
# 480p30
# 360p30
# 288p30

# You may change the target codec to HEVC by changing:
#     the h264parse plugin to h265parse plugin for decoding from HEVC

if [ $# -lt 3 ]; then
     echo -e "\e[01;31m"WARNING : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <device index> <Input 1080p60 H264 file> <num instances, 1 to 4> <Number of buffers> <fakesink 0/1>"
     echo -e "\e[01;31m"e.g $0 0 bbb_sunflower_1080p_60fps_normal.mp4 2 2000 1"\e[0m"
     echo ""
     echo "<Number of buffers> - Input number of buffers to be processed from input file (each buffer is of size 4K)" 
     echo "Giving -1 value as number of buffers, it will run for all of buffers of the video stream" 
     echo "NOTE: Running this script with fakesink=0, writes outputs to files in /tmp and displays the average fps values on terminal for all instances using notation <reoslution>_<deviceid>_<instance_number>"
     echo "e.g. sink_720p60_0_2: last-message = rendered: 4443, dropped: 0, current: 93.77, average: 61.89."
     echo "This shows 72p60 ladder for device 0 and instance 2 is running at 61.89 fps"
     echo "Ensure that the input MP4 stream resolution given as input argument is not beyond 1080p60"
     echo ""
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

#number of supported devices
num_dev=$(xbutil examine  | grep xilinx_u30 | wc -l)

#check if argument1 is an integer
re='^[0-9]+$'
if ! [[ $1 =~ $re ]] ; then
   echo "error: Give a number for argument 1 with in the range of 0 to $num_dev"; exit 1
fi

#Check if valid device index is given as argument 1
if [ $1 -ge $num_dev ]; then
    echo "Device index is not correct, exiting"
    exit 
else
    di=$1
fi

#check if input_file extension is mp4
input_file=$2

if [ $3 -gt 4 ]; then
    echo "you gave $3. Num instances can not be more than 4, exiting"
    exit 
fi

#By default this script generates decoded filesink outputs. Giving 4th argument as 1, script runs for fakesink
if [ -z "$4" ]; then
    echo "num-buffers not passed, setting to 2000"
    num=2000 
else
    echo "num-buffers defined"
    num=$4
fi

#pipe=" -v -e"
count=0
d=0
num_instance=$3

#Check if number of instances is given a value of at least <1>
if [ $3 -lt 1 ]; then
    echo "Num instances are not correct. It should be 1 at least. Run $0 for help"
    exit
fi

#checking input file is 8 bit or 10 bit 
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

echo $Depth
echo $extn

for (( c = 0; c < $num_instance; c++ ))
do
	sc_name="sc_"$c$d
        tee_name="tee_"$c$d
        enc_720p60="enc_720p60_dev"$d"_"$c
        enc_720p30="enc_720p30_dev"$d"_"$c
        enc_480p30="enc_480p30_dev"$d"_"$c
        enc_360p30="enc_360p30_dev"$d"_"$c
        enc_160p30="enc_160p30_dev"$d"_"$c
        fps_sink_720p60="sink_dec_scale_720p60_dev"$d"_"$c
        fps_sink_720p30="sink_dec_scale_720p30_dev"$d"_"$c
        fps_sink_480p30="sink_dec_scale_480p30_dev"$d"_"$c
        fps_sink_360p30="sink_dec_scale_360p30_dev"$d"_"$c
        fps_sink_160p30="sink_dec_scale_160p30_dev"$d"_"$c

if [[ $5  -eq 0 ]]; then
	echo "Script is running for filesink...."
        sink_720p60="\"filesink location=/\tmp/\xil_dec_scale_720p60_dev_${Depth}_${d}_${c}.${extn}\""
        sink_720p30="\"filesink location=/\tmp/\xil_dec_scale_720p30_dev_${Depth}_${d}_${c}.${extn}\""
	sink_480p30="\"filesink location=/\tmp/\xil_dec_scale_480p30_dev_${Depth}_${d}_${c}.${extn}\""
        sink_360p30="\"filesink location=/\tmp/\xil_dec_scale_360p30_dev_${Depth}_${d}_${c}.${extn}\""
        sink_160p30="\"filesink location=/\tmp/\xil_dec_scale_160p30_dev_${Depth}_${d}_${c}.${extn}\""
else
	echo "Script is running for fakesink...."
        sink_720p60="fakesink"
        sink_720p30="fakesink"
	sink_480p30="fakesink"
        sink_360p30="fakesink"
        sink_160p30="fakesink"
fi
pipe="gst-launch-1.0 filesrc num-buffers=$num location=$input_file
	${demuxer}
	! h264parse
	! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di 	! queue
	! vvas_xabrscaler  xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di ppc=4 scale-mode=2 name=$sc_name
	$sc_name.src_0
	    ! queue
	    ! video/x-raw, width=1280, height=720
	    ! tee name=$tee_name
	    $tee_name.
		! queue
		! videorate
		! video/x-raw, framerate=60/1
		! fpsdisplaysink name=$fps_sink_720p60 video-sink=$sink_720p60 text-overlay=false sync=false
	    $tee_name.
		! queue
		! videorate
		! video/x-raw, framerate=30/1
		! fpsdisplaysink name=$fps_sink_720p30 video-sink=$sink_720p30 text-overlay=false sync=false
	$sc_name.src_1
	    ! queue
	    ! video/x-raw, width=848, height=480
	    ! videorate
	    ! video/x-raw, framerate=30/1
	    ! fpsdisplaysink name=$fps_sink_480p30 video-sink=$sink_480p30 text-overlay=false sync=false
	$sc_name.src_2
	    ! queue
	    ! video/x-raw, width=640, height=360
	    ! videorate
	    ! video/x-raw, framerate=30/1
	    ! fpsdisplaysink name=$fps_sink_360p30 video-sink=$sink_360p30 text-overlay=false sync=false
	$sc_name.src_3
	    ! queue
	    ! video/x-raw, width=288, height=160
	    ! videorate
	    ! video/x-raw, framerate=30/1
	    ! fpsdisplaysink name=$fps_sink_160p30 video-sink=$sink_160p30 text-overlay=false sync=false -v"
#pipe="gst-launch-1.0 "$pipe
$pipe &

done
wait
