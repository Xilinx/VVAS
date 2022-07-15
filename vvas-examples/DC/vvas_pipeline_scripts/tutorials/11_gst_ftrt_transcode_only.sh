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
# This script accepts an 8-bit, YUV420, pre-encoded h264 file and will send the encoded h.264 output to /tmp/xil_ftrt_xcode.mp4 at a rate of 8Mbps.
# This use case is optimized for "faster than realtime"; that is, processing a whole clip faster than the human eye would watch it.

# You may edit this to enable other output bitrates (target-bitrate and max-bitrate).

# You may change the target codecs to HEVC by changing:
#     the first  instance of (-c:v mpsoc_vcu_h264) to (-c:v mpsoc_vcu_hevc) for decoding from HEVC
#     the second instance of (-c:v mpsoc_vcu_h264) to (-c:v mpsoc_vcu_hevc) for encoding into HEVC

# The -slices flag has implications on visual quality, while the -cores flag does not.
# Refer to the online documentation and to the visual quality examples for more details.

if [ $# -lt 1 ]; then
     echo -e "\e[01;31m"Note : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <Input H264 file>"
     echo "" 
     echo "As per VCU procceing capacity, below are the input limitations."
     echo "Ex: $0 Test_1080p30.h264"
     exit 1
fi

#check if the input stream is compatiable with this script
container=`gst-discoverer-1.0 $1 | grep "container:" |  cut -d":" -f2`
codec_type=`gst-discoverer-1.0 $1 | grep "video:" | cut -d":" -f2 | awk '{print $1;}'`
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

if [ $num_dev -lt 1 ]; then
	echo "Device index is not correct, exiting, possible values are 0 to $((num_dev-1))"
    exit
fi

pipe=" -v"
echo "Script is running for filesink ............"
pipe="filesrc location=$1 
	${demuxer}
	! h264parse 
	! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=0
	! queue 
	! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=0 num-cores=4 num-slices=4 target-bitrate=8000 max-bitrate=8000
	! h264parse 
	! video/x-h264 
	! qtmux 
	! fpsdisplaysink video-sink=\"filesink location=/tmp/xil_ftrt_xcode.mp4 \" text-overlay=false sync=false "$pipe
pipe="gst-launch-1.0 "$pipe
$pipe
