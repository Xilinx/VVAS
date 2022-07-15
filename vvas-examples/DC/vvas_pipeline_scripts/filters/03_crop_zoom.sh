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

#This script accepts an 8-bit, YUV420, pre-encoded h264 file and will crop a 300x200 sized section of the original video. The section's top left corner begins at 20 pixels to the right, and 10 pixels down from the top-left corner of the original video. The output video is encoded in 8Mbps, and is saved to /tmp/xil_crop_zoom.mp4

if [ $# -ne 2 ]
  then
    echo "Incorrect arguments supplied"
    echo "Usage: ./03_crop_zoom.sh <dev-idx> <h264 clip>"
    exit 1
fi
VIDEO_WIDTH=`gst-discoverer-1.0 -v $2 | grep "Width" | cut -d":" -f2 | awk '{print $1;}'`
VIDEO_HEIGHT=`gst-discoverer-1.0 -v $2 | grep "Height" | cut -d":" -f2 | awk '{print $1;}'`
di=$1
INPUT=$2
XPOSITION=20 
YPOSITION=10
CROP_HEIGHT=200
CROP_WIDTH=300
BOTTOM=`expr $VIDEO_HEIGHT - $YPOSITION - $CROP_HEIGHT `
RIGHT=`expr $VIDEO_WIDTH - $XPOSITION - $CROP_WIDTH `
container=`gst-discoverer-1.0 $2 | grep "container:" |  cut -d":" -f2`
codec_type=`gst-discoverer-1.0 $2 | grep "video:" | cut -d":" -f2 | awk '{print $1;}'`
echo $container
echo $codec_type
if [ -z "$container" ] && [ -z "$codec_type" ]; then
    echo "Input file is of unknown/unsupported type"
    exit -1
elif [ -z "$container" ] && [ "$codec_type" = "H.264" ]; then
        echo "Input file is of type H.264";
elif [ -z "$container" ] && [ "$codec_type" = "H.265" ]; then
        echo "Input file is of type H.265. Invalid for this script";
        exit -1
elif [ $container = "Quicktime" ] && [ "$codec_type" = "H.264" ]; then
        echo "Input file is of type Quicktime H.264. Invalid for this script";
        exit -1
elif [ $container = "Quicktime" ] && [ "$codec_type" = "H.265" ]; then
        echo "Input file is of type Quicktime H.265. Invalid for this script";
        exit -1
else
    echo "Input file is of unknown/unsupported type"
    exit -1
fi


pipe="gst-launch-1.0 -v filesrc location=$INPUT ! h264parse ! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin   dev-idx=$di ! videocrop top=$YPOSITION bottom=$BOTTOM left=$XPOSITION right=$RIGHT ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di target-bitrate=8000 ! h264parse ! qtmux ! fpsdisplaysink video-sink=\"filesink location=/tmp/xil_crop_zoom.mp4\" sync=false text-overlay=false"

$pipe

