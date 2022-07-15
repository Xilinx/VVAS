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
# This script expects 3 H.264 files and will transcode them to HEVC, sending the outputs to /tmp/xil_xcode_{n}.mp4.
# The three transcodes are run in parallel in individual xterms.
# The first job is run on device #0 and the two others jobs are run on device #1.
# After the jobs are launched, a JSON system load report is generated.

# You may edit this script to run the job on a different combination of devices (-xlnx_hwdev).
# You may saving the resulting videos by setting /dev/null as the output (-y).

if ! [ -x "$(command -v xterm)" ]
then
    echo "This example requires the 'xterm' program which could not be found on this system. Aborting."
    exit 1
fi

if [ $# -ne 3 ]; then
     echo -e "\e[01;31m"Note : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <input_h264_1_mp4> <input_h264_2_mp4> <input_h264_3_mp4> "
     echo ""
     echo "Ex: $0 bbb_sunflower_1080p_60fps_normal.mp4 bbb_sunflower_1080p_60fps_normal.mp4 bbb_sunflower_1080p_60fps_normal.mp4 "
     exit 1
fi
#check if the input stream is compatiable with this script
for input_file in "$@";
do

container=`gst-discoverer-1.0 $input_file | grep "container:" |  cut -d":" -f2`
codec_type=`gst-discoverer-1.0 $input_file | grep "video:" | cut -d":" -f2 | awk '{print $1;}'`
echo $container
echo $codec_type
if [ -z "$container" ] && [ -z "$codec_type" ]; then
    echo "Input file is of unknown/unsupported type"
    exit -1
elif [ -z "$container" ] && [ "$codec_type" = "H.264" ]; then
        echo "Input file is of type H.264 and not in mp4 format";
	exit -1
elif [ -z "$container" ] && [ "$codec_type" = "H.265" ]; then
        echo "Input file is of type H.265. Invalid for this script";
        exit -1
elif [ $container = "Quicktime" ] && [ "$codec_type" = "H.264" ]; then
        echo "Input file is of type Quicktime H.264";
elif [ $container = "Quicktime" ] && [ "$codec_type" = "H.265" ]; then
        echo "Input file is of type Quicktime H.265. Invalid for this script";
        exit -1
else
    echo "Input file is of unknown/unsupported type"
    exit -1
fi
done
num_dev=$(xbutil examine  | grep xilinx_u30 | wc -l)

if [ $num_dev -lt 2 ]; then
    echo "This script needs at least two devices to be present, hence exiting.. )"
    exit
fi

echo "Triggering 3 xterm windows that runs transcoding........"
pipe1="gst-launch-1.0 filesrc location=$1
				! qtdemux
				! queue
				! h264parse 
				! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=0
				! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=0 target-bitrate=2000
				! h265parse ! video/x-h265 
				! qtmux 
				! fpsdisplaysink video-sink=\"filesink location=/tmp/xil_xcode_1.mp4\" text-overlay=false sync=false -v"
xterm -fa mono:size=9 -hold -e $pipe1 &
pipe2="gst-launch-1.0 filesrc location=$2
				! qtdemux
				! queue
				! h264parse 
				! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=1
				! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=1 target-bitrate=2000
				! h265parse ! video/x-h265 
				! qtmux 
				! fpsdisplaysink video-sink=\"filesink location=/tmp/xil_xcode_2.mp4\" text-overlay=false sync=false -v"
xterm -fa mono:size=9 -hold -e $pipe2 &
pipe3="gst-launch-1.0 filesrc location=$2
				! qtdemux
				! queue
				! h264parse 
				! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=1
				! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=1 target-bitrate=2000
				! h265parse 
				! video/x-h265 
				! qtmux 
				! fpsdisplaysink video-sink=\"filesink location=/tmp/xil_xcode_3.mp4\" text-overlay=false sync=false -v"
xterm -fa mono:size=9 -hold -e $pipe3 &

# Wait until the jobs are started to generate a system load report
sleep 2s
xrmadm /opt/xilinx/xrm/test/list_cmd.json &

