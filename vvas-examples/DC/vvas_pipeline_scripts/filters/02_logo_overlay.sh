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

# This script accepts an 8-bit, YUV420, pre-encoded h264 file and an image file ("logo"). It will scale the logo to 500x100, place it 16 pixels right and 16 pixels down from the top-left corner of the output video file, which will be an encoded h.264 output saved to /tmp/xil_logo_overlay.mp4 at a rate of 8Mbps.

# The GitHub example will provide the Xilinx Logo in the main directory, "xilinx_logo.png".

if [ $# -ne 3 ]
  then
    echo "Incorrect arguments supplied"
    echo "Usage: ./02_logo_overlay.sh <dev-idx> <h264 clip> <Logo.png>"
    exit 1
fi
di=$1
INPUT=$2
LOGO=$3


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


pipe="gst-launch-1.0 -v filesrc location=$INPUT  ! h264parse ! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin   dev-idx=$di ! gdkpixbufoverlay location=$LOGO overlay-height=100 overlay-width=500 offset-x=16 offset-y=16 ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di target-bitrate=8000 ! h264parse ! qtmux ! fpsdisplaysink video-sink=\"filesink location=/tmp/xil_logo_overlay.mp4\" sync=false text-overlay=false"

$pipe
