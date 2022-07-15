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

# This script accepts 4x 8-bit, YUV420, pre-encoded h264 files of equal dimensions, and will create an output 2x2 composite video which will be an encoded h.264 output saved to /tmp/xil_2x2_composite.mp4 at a rate of 8Mbps. The output resolution will be equal to the original input.


if [ $# -ne 5 ]
  then
    echo "Incorrect arguments supplied"
    echo "Usage: ./04_multi_comp.sh <dev-idx> <h264 clip> <h264 clip> <h264 clip> <h264 clip>"
    exit 1
fi

di=$1
TOPLEFT=$2
TOPRIGHT=$3
BOTLEFT=$4
BOTRIGHT=$5
Width=`gst-discoverer-1.0 -v $2 | grep "Width" | cut -d":" -f2 | awk '{print $1;}'`
Height=`gst-discoverer-1.0 -v $2 | grep "Height" | cut -d":" -f2 | awk '{print $1;}'`



#check if the input stream is compatiable with this script
container1=`gst-discoverer-1.0 $2 | grep "container:" |  cut -d":" -f2`
container2=`gst-discoverer-1.0 $3 | grep "container:" |  cut -d":" -f2`
container3=`gst-discoverer-1.0 $4 | grep "container:" |  cut -d":" -f2`
container4=`gst-discoverer-1.0 $5 | grep "container:" |  cut -d":" -f2`
codec_type1=`gst-discoverer-1.0 $2 | grep "video:" | cut -d":" -f2 | awk '{print $1;}'`
codec_type2=`gst-discoverer-1.0 $3 | grep "video:" | cut -d":" -f2 | awk '{print $1;}'`
codec_type3=`gst-discoverer-1.0 $4 | grep "video:" | cut -d":" -f2 | awk '{print $1;}'`
codec_type4=`gst-discoverer-1.0 $5 | grep "video:" | cut -d":" -f2 | awk '{print $1;}'`
echo $container
echo $codec_type
if [ -z "$container1" ] && [ -z "$codec_type1" ]; then
    echo "Input file is of unknown/unsupported type"
    exit -1
elif [ -z "$container1" ] && [ -z "$container2" ] && [ -z "$container3" ] && [ -z "$container4" ] && \
	[ "$codec_type1" = "H.264" ] && [ "$codec_type2" = "H.264" ] && [ "$codec_type3" = "H.264" ] && [ "$codec_type4" = "H.264" ]; then
        echo "Input file is of type H.264, it is an valid input";
else
    echo "Either of the four  Input files is of unknown/unsupported type"
    exit -1
fi

pipe="gst-launch-1.0 -v 
filesrc location=$TOPLEFT
! h264parse
! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin   dev-idx=$di ! videoscale
! video/x-raw, height=$(( $Height / 2)) , width=$(($Width / 2 ))
! comp_$c.

filesrc location=$TOPRIGHT
! h264parse
! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di
! videoscale
! video/x-raw, height=$(( $Height / 2)) , width=$(($Width / 2 ))
! comp_$c.

filesrc location=$BOTLEFT
! h264parse
! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin   dev-idx=$di ! videoscale
! video/x-raw, height=$(( $Height / 2)) , width=$(($Width / 2 ))
! comp_$c.

filesrc location=$BOTRIGHT
! h264parse
! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di ! videoscale
! video/x-raw, height=$(( $Height / 2)) , width=$(($Width / 2 ))
! comp_$c.


compositor name=comp_$c
sink_0::zorder=1 sink_0::xpos=0 sink_0::ypos=0
sink_1::zorder=2 sink_1::xpos=$(($Width / 2 )) sink_1::ypos=0
sink_2::zorder=3 sink_2::xpos=0 sink_2::ypos=$(( $Height / 2))
sink_3::zorder=4 sink_3::xpos=$(($Width / 2 )) sink_3::ypos=$(( $Height / 2))

! video/x-raw , width=$Width , height=$Height , format=(string)NV12
! queue
! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin  dev-idx=$di ! h264parse
! qtmux
! fpsdisplaysink video-sink=\"filesink location=/tmp/xil_multi_comp.mp4\" text-overlay=false sync=false
"

$pipe
