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
# This script assumes an 8-bit, YUV420, pre-encoded 60FPS mp4 with h.264 container file.
# It will scale this input into multiple renditions of various sizes,
# and send them to the encoder targeting various bitrates (as defined by the target-bitrate property).

# This is a LOW-LATENCY version: b-frames are removed, as well as a reduced lookahead. This means 2 things:
# 1) The decoder does not accept B-Frames (out of order decoding). If you provide a clip with B-Frames, you will receive an invalid output
# If your input has B-Frames, simply remove `b-frames=0`
# 2) The encoder will not produce B-Frames in its output

# The 1080p60 input is scaled down to the following resolutions, framerates, and bitrates (respectively):
# 720p60 4.0   Mbps
# 720p30 3.0   Mbps
# 848p30 2.5   Mbps
# 360p30 1.25  Mbps
# 288p30 0.625 Mbps

# You may change the target codec to HEVC by changing:
#     the h264parse plugin to h265parse plugin for decoding from HEVC

if [ $# -lt 3 ]; then
     echo -e "\e[01;31m"WARNING : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <device index> <Input 1080p60 H264 File> <num instances, 1 to 4> <Number of buffers> <fakesink 0/1>"
     echo -e "\e[01;31m"e.g $0 0 bbb_sunflower_1080p_60fps_normal.mp4 1 2000 1"\e[0m"
     echo "<Number of buffers> - Input number of buffers to be processed from input file (each buffer is of size 4K)" 
     echo ""
     echo "NOTE: Running this script with fakesink=0 writes outputs to /tmp/sink_ll_*.mp4 and displays the average fps values on terminal for all instances using notation <reoslution>_<deviceid>_<instance_number>"
     echo "e.g. sink_720p60_0_2: last-message = rendered: 4443, dropped: 0, current: 93.77, average: 61.89."
     echo "This shows 72p60 ladder for device 0 and instance 2 is running at 61.89 fps. Run script with fakesink=1 for full fps"
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

if [ -z "$4" ]; then
    echo "num-buffers not passed, setting to 2000"
    num=2000 
else
    echo "num-buffers defined"
    num=$4
fi

let "frm_rate = 240 / $3"
echo $frm_rate

count=0
d=$di
num_instance=$3

for (( c = 0; c < $num_instance; c++ ))
do
	sc_name="sc_"$c$d
        tee_name="tee_"$c$d
        enc_720p60="enc_720p60_dev"$d"_"$c
        enc_720p30="enc_720p30_dev"$d"_"$c
        enc_480p30="enc_480p30_dev"$d"_"$c
        enc_360p30="enc_360p30_dev"$d"_"$c
        enc_160p30="enc_160p30_dev"$d"_"$c
        fps_sink_720p60="sink_ll_xcode_scale_720p60_dev"$d"_"$c
        fps_sink_720p30="sink_ll_xcode_scale_720p30_dev"$d"_"$c
        fps_sink_480p30="sink_ll_xcode_scale_480p30_dev"$d"_"$c
        fps_sink_360p30="sink_ll_xcode_scale_360p30_dev"$d"_"$c
        fps_sink_160p30="sink_ll_xcode_scale_160p30_dev"$d"_"$c

if [[ $5  -eq 0 ]]; then
        echo "Script is running for filesink...."
	muxer="! qtmux"
        sink_720p60="\"filesink location=/\tmp/\xil_ll_xcode_scale_720p60_dev_${Depth}_${d}_${c}.mp4\""
        sink_720p30="\"filesink location=/\tmp/\xil_ll_xcode_scale_720p30_dev_${Depth}_${d}_${c}.mp4\""
        sink_480p30="\"filesink location=/\tmp/\xil_ll_xcode_scale_480p30_dev_${Depth}_${d}_${c}.mp4\""
        sink_360p30="\"filesink location=/\tmp/\xil_ll_xcode_scale_360p30_dev_${Depth}_${d}_${c}.mp4\""
        sink_160p30="\"filesink location=/\tmp/\xil_ll_xcode_scale_160p30_dev_${Depth}_${d}_${c}.mp4\""
else
        echo "Script is running for fakesink...."
	muxer=
        sink_720p60="fakesink"
        sink_720p30="fakesink"
        sink_480p30="fakesink"
        sink_360p30="fakesink"
        sink_160p30="fakesink"
fi

pipe="gst-launch-1.0 filesrc num-buffers=$num location=$input_file
	${demuxer}
	! h264parse
	! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di low-latency=1
	! queue
	! vvas_xabrscaler  xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di ppc=4 scale-mode=2 avoid-output-copy=true name=$sc_name
	$sc_name.src_0
	    ! queue
	    ! video/x-raw, width=1280, height=720
	    ! tee name=$tee_name 
	    $tee_name.
		! queue
		! videorate
		! video/x-raw, framerate=60/1
		! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_720p60 dev-idx=$di  target-bitrate=4000 b-frames=0 scaling-list=0
		! h264parse
		${muxer}
		! fpsdisplaysink name=$fps_sink_720p60 video-sink=$sink_720p60 text-overlay=false sync=false
	    $tee_name.
		! queue
		! videorate
		! video/x-raw, framerate=30/1
		! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_720p30 dev-idx=$di  target-bitrate=3000 b-frames=0 scaling-list=0
		! h264parse
		${muxer}
		! fpsdisplaysink name=$fps_sink_720p30 video-sink=$sink_720p30 text-overlay=false sync=false
	$sc_name.src_1
	    ! queue
	    ! video/x-raw, width=848, height=480
	    ! videorate
	    ! video/x-raw, framerate=30/1
	    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_480p30 dev-idx=$di  target-bitrate=2500 b-frames=0 scaling-list=0
	    ! h264parse
	    ${muxer}
	    ! fpsdisplaysink name=$fps_sink_480p30 video-sink=$sink_480p30 text-overlay=false sync=false
	$sc_name.src_2
	    ! queue
	    ! video/x-raw, width=640, height=360
	    ! videorate
	    ! video/x-raw, framerate=30/1
	    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_360p30 dev-idx=$di  target-bitrate=1250 b-frames=0 scaling-list=0
	    ! h264parse
	    ${muxer}
	    ! fpsdisplaysink name=$fps_sink_360p30 video-sink=$sink_360p30 text-overlay=false sync=false
	$sc_name.src_3
	    ! queue
	    ! video/x-raw, width=288, height=160
	    ! videorate
	    ! video/x-raw, framerate=30/1
	    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_160p30 dev-idx=$di  target-bitrate=625 b-frames=0 scaling-list=0
	    ! h264parse
	    ${muxer}
	    ! fpsdisplaysink name=$fps_sink_160p30 video-sink=$sink_160p30 text-overlay=false sync=false -v"
#pipe="gst-launch-1.0 "$pipe
$pipe &
done
wait
