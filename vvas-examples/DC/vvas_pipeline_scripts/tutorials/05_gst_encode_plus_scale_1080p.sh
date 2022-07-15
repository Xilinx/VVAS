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
# This script accepts an 8-bit (N12) or 10-bit (NV12_10LE32), 1080p60 RAW file and will send it to the scaler, which outputs various renditions of various sizes,
# and sends them to the encoder targeting various bitrates (as defined by the target-bitrate property of encoder).
# All outputs will be stored at /tmp/sink_scale_enc_*.mp4
# Giving fakesink=1 as argument, will not dump output files to /tmp and displays the max. possible fps values
# You may edit this to accept other resolutions, other framerates, other output bitrates.

# The 1080p60 input is scaled down to the following resolutions, framerates, and bitrates (respectively):
# 720p60 4.0   Mbps
# 720p30 3.0   Mbps
# 848p30 2.5   Mbps
# 360p30 1.25  Mbps
# 288p30 0.625 Mbps

# You may edit this to enable other output bitrates (target-bitrate)
# You may change the output framerate via the framerate property

# You may change the target codecs to HEVC by changing:
#     the h264parse plugin to h265parse plugin for encoding to HEVC

if [ $# -lt 3 ]; then
     echo -e "\e[01;31m"WARNING : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <device index> <1080p60 nv12 RAW file> <num instances, 1 to 4> <10bitinput 0/1> <fakesink 0/1>"
     echo -e "\e[01;31m"e.g $0 0 Test_1920x1080.nv12 1 0 0"\e[0m"
     echo ""
     echo "NOTE: Running this script writes encoded output files into /tmp directory and displays the average fps values on terminal for all instances using notation <reoslution>_<deviceid>_<instance_number>"
     echo "e.g. sink_720p60_0_2: last-message = rendered: 4443, dropped: 0, current: 93.77, average: 61.89."
     echo "This shows 72p60 ladder for device 0 and instance 2 is running at 61.89 fps"
     echo "NOTE: For input raw strem resolutions other than 1080p, change width, height, blocksize and framerate params in the script accordingly"
     echo "NOTE: blocksize is the size of one frame in bytes"
     echo ""
     exit 1
fi

#number of supported devices
num_dev=$(xbutil examine  | grep xilinx_u30 | wc -l)

#check if argument1 is an integer
re='^[0-9]+$'
if ! [[ $1 =~ $re ]] ; then
   echo "error: Give a number for argument 1 with in the range of 0 to $num_dev. Run $0 for help"; exit 1
fi

#Check if valid device index is given as argument 1
if [ $1 -ge $num_dev ]; then
    echo "Device index is not correct, exiting Run $0 for help"
    exit 1
else
    di=$1
fi

#check if input_file extension is rawfile or not
input_file=$2

if [[ $input_file = *.nv12* || $input_file = *.NV12* ]] || [[ $input_file = *.yuv* || $input_file = *.YUV* ]]
then
    echo ""
else
    echo "Please provide input raw files with 8 bit or 10 bit in it. Run $0 for help"
    exit 1

fi

if [ $3 -gt 4 ]; then
    echo "you gave $3. Num instances can not be more than 4, exiting Run $0 for help"
    exit 
fi

#checking width and height from file name for rawfiles 
rawfileinput_name=`basename $2`
height_val=`echo $rawfileinput_name | cut -d"x" -f2 | grep -o '[0-9]*' | head -1`
width_val=`echo $rawfileinput_name | cut -d"x" -f1 | grep -o '[0-9]*' | head -1`

echo $width_val
echo $height_val

if [[ $width_val = 1920 || $height_val = 1080 ]]
then
    echo ""
else
    echo "Please provide input with width:1920 and height:1080 raw file. Run $0 for help"
    exit 1
fi
###10 bit input or not
if [ -z "$4" ]
then
      echo "no 10-bit arg supplied. Hence assuming that 8-bit raw input video stream"
      rawvideo_format_name="nv12"
else
      if [ $4 -eq 1 ]
          then
              echo "10-bit raw input argument is supplied"
              rawvideo_format_name="nv12-10le32"
	      blocksize=4147200
          else
              echo "8-bit raw input argument is supplied"
              rawvideo_format_name="nv12"
	      blocksize=3110400
      fi
fi

#pipe=" -v -e"
count=0
d=0
num_instance=$3

#Check if number of instances is given a value of at least <1>
if [ $3 -lt 1 ]; then
    echo "Num instances are not correct. It should be 1 at least. Run $0 for help"
    exit 1
fi


for (( c = 0; c < $num_instance; c++ ))
do
	sc_name="sc_"$c$d
        tee_name="tee_"$c$d
        enc_720p60="enc_720p60_dev"$d"_"$c
        enc_720p30="enc_720p30_dev"$d"_"$c
        enc_480p30="enc_480p30_dev"$d"_"$c
        enc_360p30="enc_360p30_dev"$d"_"$c
        enc_160p30="enc_160p30_dev"$d"_"$c
        fps_sink_720p60="sink_scale_enc_720p60_dev"$d"_"$c
        fps_sink_720p30="sink_scale_enc_720p30_dev"$d"_"$c
        fps_sink_480p30="sink_scale_enc_480p30_dev"$d"_"$c
        fps_sink_360p30="sink_scale_enc_360p30_dev"$d"_"$c
        fps_sink_160p30="sink_scale_enc_160p30_dev"$d"_"$c

if [[ $5  -eq 0 ]]; then
        echo "Script is running for filesink...."
	muxer="! qtmux"

        sink_720p60="\"filesink location=/\tmp/\xil_scale_enc_720p60_dev_${Depth}_${d}_${c}.mp4\""
        sink_720p30="\"filesink location=/\tmp/\xil_scale_enc_720p30_dev_${Depth}_${d}_${c}.mp4\""
        sink_480p30="\"filesink location=/\tmp/\xil_scale_enc_480p30_dev_${Depth}_${d}_${c}.mp4\""
        sink_360p30="\"filesink location=/\tmp/\xil_scale_enc_360p30_dev_${Depth}_${d}_${c}.mp4\""
        sink_160p30="\"filesink location=/\tmp/\xil_scale_enc_160p30_dev_${Depth}_${d}_${c}.mp4\""
else
        echo "Script is running for fakesink...."
	muxer=
        sink_720p60="fakesink"
        sink_720p30="fakesink"
        sink_480p30="fakesink"
        sink_360p30="fakesink"
        sink_160p30="fakesink"
fi


pipe="gst-launch-1.0 filesrc location=$2 blocksize=$blocksize
	! queue 
	! rawvideoparse format=$rawvideo_format_name width=1920 height=1080 framerate=60/1
	! queue
	! vvas_xabrscaler  xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di ppc=4 scale-mode=2 name=$sc_name avoid-output-copy=true enable-pipeline=true
	$sc_name.src_0
	    ! queue
	    ! video/x-raw, width=1280, height=720
	    ! queue
	    ! tee name=$tee_name 
	    $tee_name.
		! queue
		! videorate
		! video/x-raw, framerate=60/1
		! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_720p60 dev-idx=$di  target-bitrate=4000 max-bitrate=4000
		! h264parse
               ${muxer}
		! fpsdisplaysink name=$fps_sink_720p60 video-sink=$sink_720p60 text-overlay=false sync=false
	    $tee_name.
		! queue
		! videorate
		! video/x-raw, framerate=30/1
		! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_720p30 dev-idx=$di  target-bitrate=3000 max-bitrate=3000
		! h264parse
               ${muxer}
		! fpsdisplaysink name=$fps_sink_720p30 video-sink=$sink_720p30 text-overlay=false sync=false
	$sc_name.src_1
	    ! queue
	    ! video/x-raw, width=848, height=480
	    ! videorate
	    ! video/x-raw, framerate=30/1
	    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_480p30 dev-idx=$di  target-bitrate=2500 max-bitrate=2500
	    ! h264parse
           ${muxer}
	    ! fpsdisplaysink name=$fps_sink_480p30 video-sink=$sink_480p30 text-overlay=false sync=false
	$sc_name.src_2
	    ! queue
	    ! video/x-raw, width=640, height=360
	    ! videorate
	    ! video/x-raw, framerate=30/1
	    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_360p30 dev-idx=$di  target-bitrate=1250 max-bitrate=1250
	    ! h264parse
           ${muxer}
	    ! fpsdisplaysink name=$fps_sink_360p30 video-sink=$sink_360p30 text-overlay=false sync=false
	$sc_name.src_3
	    ! queue
	    ! video/x-raw, width=288, height=160
	    ! videorate
	    ! video/x-raw, framerate=30/1
	    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_160p30 dev-idx=$di  target-bitrate=625 max-bitrate=625
	    ! h264parse
           ${muxer}
	    ! fpsdisplaysink name=$fps_sink_160p30 video-sink=$sink_160p30 text-overlay=false sync=false -v"
#pipe="gst-launch-1.0 "$pipe
$pipe &
done
wait
