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

# pre-requisite to run this script: source /opt/xilinx/vvas/setup.sh
# This script assumes an 8-bit, YUV420, pre-encoded 60FPS h.264/MP4 file input. It will scale this input into multiple renditions of various sizes,
# and sends them to the encoder targeting various bitrates (as defined by the target-bitrate property of encoder).
# This application is equivalent functionally with 06_gst_transcode_plus_scale.sh script 

# The 1080p60 input is scaled down to the following resolutions, framerates, and bitrates (respectively):
# 720p60 4.0   Mbps
# 720p30 3.0   Mbps
# 848p30 2.5   Mbps
# 360p30 1.25  Mbps
# 288p30 0.625 Mbps
# This application reads the json file from /opt/xilinx/vvas/share/vvas-examples/abrladder.json
# Advanced users can update this for required resolutiosn and test fakesink by replacing filesink for max possible fps numbers
# As the script is writing output files into /tmp area, the displayed fps may not be the true fps with filesink
# The source code for the vvas_xabrladder application is present in the vvas repo: vvas-examples/DC/vvas_xabrladder

if [ $# -lt 1 ]; then
     echo -e "\e[01;31m"WARNING : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <device index> <Input 1080p60 MP4 file with H264 content>"
     echo -e "\e[01;31m"e.g $0 0 bbb_sunflower_1080p_60fps_normal.mp4"\e[0m"
     echo ""
     echo "NOTE: Running this script, generates 4 transcoding ladders with each ladder outputting 4 resolutions and writes encoded files into /tmp/ladder_outputs folder and display average fps values on terminal for all instances using notation <deviceid>_<ladder>_<resolution>_<fps>"
     echo "e.g. Frame info devid/ladder[0/0] output_1280x720p60:        last-message = rendered: 31, dropped: 0, current: 61.79, average: 61.79"
     echo "This message shows that 72p60 ladder for device 0 and ladder 0 is running at 61.79 fps"
     echo "Ensure that the input MP4 stream resolution given as input argument is not beyond 1080p60"
     echo ""
     exit 1
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

handle_chld() {
	local tmp=()
	for((i=0;i<${#pids[@]};++i)); do
		if [ ! -d /proc/${pids[i]} ]; then
			wait ${pids[i]}
			echo "Stopped ${pids[i]}; exit code: $?"
		else tmp+=(${pids[i]})
		fi
	done
	pids=(${tmp[@]})
}
# Start background processes
vvas_xabrladder  --devidx $1  --lookahead_enable 0 --codectype 0 --file $2 &
pids+=($!)
vvas_xabrladder  --devidx $1  --lookahead_enable 0 --codectype 0 --file $2 &
pids+=($!)
vvas_xabrladder  --devidx $1  --lookahead_enable 0 --codectype 0 --file $2 &
pids+=($!)
vvas_xabrladder  --devidx $1  --lookahead_enable 0 --codectype 0 --file $2 &
pids+=($!)
# Wait until all background processes are stopped
while [ ${#pids[@]} -gt 0 ]; 
do 
	#echo "WAITING FOR: ${pids[@]}";
	sleep 1;
	handle_chld; 
done
echo STOPPED
