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
# This script accepts an 8-bit, YUV420, pre-encoded h264 file and will send the encoded h.264 output in a set of HLS streams located at /var/www/html/xil_xcode_stream_scale_*.m3u8

# The 1080p60 input is scaled down to the following resolutions, framerates, and bitrates (respectively):
# 720p60 4.0   Mbps
# 720p30 3.0   Mbps
# 848p30 2.5   Mbps
# 360p30 1.25  Mbps
# 288p30 0.625 Mbps

# Most webservers host their "accessible locations" from /var/www/html/. This will not exist without a webserver (e.g. Apache2) installed. Please ensure one is installed and the directory is writable; otherwise this script will fail.

# You may edit this to enable other output bitrates (target-bitrate)
# You may change the output framerate via the framerate property

# You may change the target codecs to HEVC by changing:
#     the h264parse plugin to h265parse plugin for decoding from HEVC
# Removing qtdemux script supports the input h.264 files instead of MP4 with h.264 content

# Stream settings may be changed to accomodate different needs:
# "target-duration 4"
#       Sets the segment length to 4 seconds

# "playlist-length 5"
#       Sets the number of 4-second segments to 5 to be saved in the playlist/manifest

# "max-files delete_segments"
#       Deletes segments after reaching the specificed list size (in this case 5)


if [ $# -lt 2 ]; then
     echo ""
     echo -e "\e[01;31m"WARNING : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <device index> <Input 1080p60 H264 file> <[Number of buffers]>"
     echo -e "\e[01;31m"e.g $0 0 bbb_sunflower_1080p_60fps_normal.mp4 2000"\e[0m"
     echo "<Number of buffers> - Input number of buffers to be processed from input file (each buffer is of size 4K), -1 value runs complete stream" 
     echo "NOTE: Running this script will create a “manifest” file .m3u8 for streaming along with several .ts files with the actual playback data"
     echo "Ensure that the input MP4 stream resolution given as input argument is 1080p60 only"
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

if [ -z "$3" ]; then
    echo "num-buffers not passed, setting to 2000"
    num=2000 
else
    echo "num-buffers defined"
    num=$3
fi

#pipe=" -v -e"
count=0
d=$di

c=0 #single instance
sc_name="sc_"$c$d
tee_name="tee_"$c$d
enc_720p60="enc_720p60_dev"$d"_"$c
enc_720p30="enc_720p30_dev"$d"_"$c
enc_480p30="enc_480p30_dev"$d"_"$c
enc_360p30="enc_360p30_dev"$d"_"$c
enc_160p30="enc_160p30_dev"$d"_"$c
sink_720p60="sink_xcode_scale_720p60_dev"$d"_"$c
sink_720p30="sink_xcode_scale_720p30_dev"$d"_"$c
sink_480p30="sink_xcode_scale_480p30_dev"$d"_"$c
sink_360p30="sink_xcode_scale_360p30_dev"$d"_"$c
sink_160p30="sink_xcode_scale_160p30_dev"$d"_"$c

pipe="gst-launch-1.0 filesrc num-buffers=$num location=$input_file
	${demuxer}
	! queue
	! h264parse
	! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di 	! queue
	! vvas_xabrscaler  xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di ppc=4 scale-mode=2 name=$sc_name avoid-output-copy=true
	$sc_name.src_0
	    ! queue
	    ! video/x-raw, width=1280, height=720, format=NV12
	    ! tee name=$tee_name 
	    $tee_name.
		! queue
		! videorate
		! video/x-raw, framerate=60/1
		! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_720p60 dev-idx=$di  target-bitrate=4000
		! h264parse
		! video/x-h264
		! hlssink2 target-duration=4 playlist-length=5 max-files=5 location=/var/www/html/segment1_%05d.ts playlist-location=/var/www/html/xil_xcode_stream_scale_720p60.m3u8
	    $tee_name.
		! queue
		! videorate
		! video/x-raw, framerate=30/1
		! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_720p30 dev-idx=$di  target-bitrate=3000
		! h264parse
		! video/x-h264
		! hlssink2 target-duration=4 playlist-length=5 max-files=5 location=/var/www/html/segment2_%05d.ts playlist-location=/var/www/html/xil_xcode_stream_scale_720p30.m3u8
	$sc_name.src_1
	    ! queue
	    ! video/x-raw, width=848, height=480, format=NV12
	    ! videorate
	    ! video/x-raw, framerate=30/1
	    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_480p30 dev-idx=$di  target-bitrate=2500
	    ! h264parse
	    ! video/x-h264
	    ! hlssink2 target-duration=4 playlist-length=5 max-files=5 location=/var/www/html/segment3_%05d.ts playlist-location=/var/www/html/xil_xcode_stream_scale_480p30.m3u8
	$sc_name.src_2
	    ! queue
	    ! video/x-raw, width=640, height=360, format=NV12
	    ! videorate
	    ! video/x-raw, framerate=30/1
	    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_360p30 dev-idx=$di  target-bitrate=1250
	    ! h264parse
	    ! video/x-h264
	    ! hlssink2 target-duration=4 playlist-length=5 max-files=5 location=/var/www/html/segment4_%05d.ts playlist-location=/var/www/html/xil_xcode_stream_scale_360p30.m3u8
	$sc_name.src_3
	    ! queue
	    ! video/x-raw, width=288, height=160, format=NV12
	    ! videorate
	    ! video/x-raw, framerate=30/1
	    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_160p30 dev-idx=$di  target-bitrate=625
	    ! h264parse
	    ! video/x-h264
	    ! hlssink2 target-duration=4 playlist-length=5 max-files=5 location=/var/www/html/segment5_%05d.ts playlist-location=/var/www/html/xil_xcode_stream_scale_160p30.m3u8 -v"
$pipe &
wait
