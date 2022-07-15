########################################################################
 # Copyright 2020 - 2022 Xilinx, Inc.
 #
 # Licensed under the Apache License, Version 2.0 (the "License");
 # you may not use this file except in compliance with the License.
 # You may obtain a copy of the License at
 #
 #     http://www.apache.org/licenses/LICENSE-2.0
 #
 # Unless required by applicable law or agreed to in writing, software
 # distributed under the License is distributed on an "AS IS" BASIS,
 # WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 # See the License for the specific language governing permissions and
 # limitations under the License.
#########################################################################

#!/bin/bash

## This file will accept in mp4 1080p60 file with h264 elementary stream in it. It will calculates the total and individual plugins latency of the ABR ladder pipeline and displays the latency using gst-stats-1.0 tool

if [ $# -ne 1 ];
then
echo "Follow below help:"
echo "$0 <Input MP4 file with H.264 1080p60>"
exit
fi

container=`gst-discoverer-1.0 $1 | grep "container:" |  cut -d":" -f2`
codec_type=`gst-discoverer-1.0 $1 | grep "video:" | cut -d":" -f2 | awk '{print $1;}'`

if [ "$container" != " Quicktime" ] || [ "$codec_type" != "H.264" ]; then
        echo "Input file must be MP4 with H.264 video";
        exit -1 
fi

num_instance=1
export GST_DEBUG="GST_TRACER:7" GST_TRACERS="latency(flags=pipeline+element)" GST_DEBUG_FILE=log.txt
for (( c = 1; c <= $num_instance; c++ ))
do
        parse_name="parse_"$c
        dec_name="dec_"$c
        sc_name="sc_"$c
        qu_name="q_sc_$c"
        tee_name="tee_"$c
        enc_720p60="enc_720p60_"$c
        enc_720p30="enc_720p30_"$c
        enc_480p30="enc_480p30_"$c
        enc_360p30="enc_360p30_"$c
        enc_160p30="enc_160p30_"$c
        sink_720p60="sink_720p60_"$c
        sink_720p30="sink_720p30_"$c
        sink_480p30="sink_480p30_"$c
        sink_360p30="sink_360p30_"$c
        sink_160p30="sink_160p30_"$c
pipe+=" filesrc location=$1 ! qtdemux name=demux demux.  \
! h264parse name=$parse_name ! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$dec_name splitbuff-mode=true low-latency=true avoid-output-copy=true dev-idx=0  \
! vvas_xabrscaler xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=0 scale-mode=2 name=$sc_name $sc_name.src_0 \
! queue name=$qu_name max-size-buffers=1 ! video/x-raw, width=1280, height=720, format=NV12 \
! tee name=$tee_name $tee_name. \
! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_720p60 control-rate=low-latency b-frames=0 gop-mode=low-delay-p target-bitrate=4000 dev-idx=0 max-bitrate=4000 \
! h264parse ! video/x-h264, profile=high, level=(string)4.2 \
! fakesink name=$sink_720p60  async=false sync=false $tee_name. \
! queue max-size-buffers=1 \
! videorate drop-only=true ! video/x-raw, width=1280, height=720, framerate=30/1 \
! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_720p30 control-rate=low-latency b-frames=0 gop-mode=low-delay-p target-bitrate=3000 dev-idx=0 max-bitrate=3000 \
! h264parse ! video/x-h264, profile=high, level=(string)4.2 \
! fakesink name=$sink_720p30  async=false sync=false $sc_name.src_1 \
! queue max-size-buffers=1 ! video/x-raw, width=848, height=480, format=NV12 \
! videorate drop-only=true ! video/x-raw, framerate=30/1 \
! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_480p30 control-rate=low-latency b-frames=0 gop-mode=low-delay-p target-bitrate=2500 dev-idx=0 max-bitrate=2500 \
! h264parse ! video/x-h264, profile=high, level=(string)4.2 \
! fakesink name=$sink_480p30  async=false sync=false $sc_name.src_2 \
! queue max-size-buffers=1 ! video/x-raw, width=640, height=360, format=NV12 \
! videorate drop-only=true ! video/x-raw, framerate=30/1 \
! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_360p30 control-rate=low-latency b-frames=0 gop-mode=low-delay-p target-bitrate=1250 dev-idx=0 max-bitrate=1250 \
! h264parse ! video/x-h264, profile=high, level=(string)4.2 \
! fakesink name=$sink_360p30  async=false sync=false $sc_name.src_3 \
! queue max-size-buffers=1 ! video/x-raw, width=288, height=160, format=NV12 \
! videorate drop-only=true ! video/x-raw, framerate=30/1 \
! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=$enc_160p30 control-rate=low-latency b-frames=0 gop-mode=low-delay-p target-bitrate=625 dev-idx=0 max-bitrate=625 \
! h264parse ! video/x-h264, profile=high, level=(string)4.2 "

pipe+="! fakesink name=$sink_160p30  async=false sync=false "

test=`gst-discoverer-1.0  $1 | grep "audio:" | wc -l`
for ((i=0; i<$test; i++))
do
pipe+=" demux. ! queue ! fakesink "
done

pipe="gst-launch-1.0 "$pipe

$pipe
done
sync
cp log.txt temp.txt
gst-stats-1.0 temp.txt > stat.txt
printf "\n\n\n"

printf "Average end to end latency (720p60 leg) in milli seconds :  "
cat stat.txt   | grep "sink_720p60_1"  | awk -F ":" '{print $2}' | awk -F "=" '{print $2}' | awk -F " " '{print $1/1000000}'

printf "Average decoder latency in milli seconds :  "
cat stat.txt   | grep "dec_1"  | awk -F ":" '{print $2}' | awk -F "=" '{print $2}' | awk -F " " '{print $1/1000000}'

printf "Average ecnoder latency (720p60 leg) in milli seconds :  "
cat stat.txt   | grep "enc_720p30_1"  | awk -F ":" '{print $2}' | awk -F "=" '{print $2}' | awk -F " " '{print $1/1000000}'

printf "Average scaler latency (720p60 leg) in milli seconds :  "
cat stat.txt   | grep "sc_1.src_0"  | awk -F ":" '{print $2}' | awk -F "=" '{print $2}' | awk -F " " '{print $1/1000000}'

rm -f log.txt  stat.txt
