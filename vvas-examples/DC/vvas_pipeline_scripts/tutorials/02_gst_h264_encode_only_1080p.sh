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
# This script accepts an 8-bit and 10bit, YUV420, 1080p60 RAW nv12 file and will send the encoded h.264 output to 
# /tmp/xil_enc_out*.mp4 at a rate of 8Mbps using filesink option
# This script also has the option to display the max possible fps values if fakesink=1 as argument. Default value is 0
# This script also supports running multiple instances of same pipeline based on resolution of input stream

#You may edit this to accept other resolution (width, height), other framerate , other output bitrates (target-bitrate).
#You may also use /dev/null inplace of file output path.

# You may change the target codec to HEVC by changing:
#     the h264parse plugin to h265parse plugin, video/x-raw property for encoding into HEVC

if [ $# -lt 3 ]; then
     echo -e "\e[01;31m"Note : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <device index> <Input 1080p60 NV12 file> <Number of encoder instances, 1 to 4> <10bit-input 0/1> <fakesink 0/1>"
     echo ""
     echo "Maximum 4 instances if each output is 1080p60"
     echo "Ex: $0 0 Test_1920x1080p60.nv12 4 0 0"
     echo "NOTE: For input raw strem resolutions other than 1080p, change width, height, blocksize and framerate params in the script accordingly"
     echo "NOTE: blocksize is the size of one frame in bytes"
     exit 1
fi

#Check if the device index is correct
num_dev=$(xbutil examine  | grep xilinx_u30 | wc -l)

if [ $1 -ge $num_dev ]; then
    echo "Device index is not correct, exiting, possible values are 0 to $((num_dev-1))"
    exit
else
    di=$1
fi

input_file=$2
if [[ $input_file = *.nv12* || $input_file = *.NV12* ]] || [[ $input_file = *.yuv* || $input_file = *.YUV* ]]
then
    echo ""
else
    echo "Please provide input raw files with 8 bit or 10 bit in it. Run $0 for help"
    exit
fi

#By default this script generates encoded filesink outputs. Giving 5th argument as 1, script runs for fakesink
if [ -z "$5" ]
then
      echo "No fakesink argument is supplied"
      fakesink=0
else
      echo "fakesink argument is supplied"
      fakesink=$5
fi



d=$3

#Check if number of instances is given a value of at least <1>
if [ $3 -lt 1 ]; then
    echo "Num instances are not correct. It should be 1 at least. Run $0 for help"
    exit
fi
###10 bit input or not
if [ -z "$4" ]
then
      echo "no 10bit-input arg supplied. Hence assuming that 8-bit raw input video stream"
      format_name="nv12"
else
      if [ $4 -eq 1 ]
          then
              echo "10 bit input argument is supplied"
              format_name="nv12-10le32"
	      blocksize=4147200
          else
              echo "8 bit input argument is supplied"
              format_name="nv12"
	      blocksize=3110400
      fi
fi

echo $format_name


sink="fakesink"
pipe=" -v"
for (( c=0; c<$d; c++ ))
do
echo "instance number $c"
echo "fakesink value is $fakesink"
sinkname="fpsdisplaysink_${c}"
if [ $fakesink == "0" ]
then
	sink="\"filesink location=/tmp/xil_enc_out_$c.mp4 \""
	muxer="! qtmux"
        echo "Script is running for filesink ............"
else
	echo "Script is running for fakesink ............"
	muxer=
fi

pipe="gst-launch-1.0 -v filesrc location=$2 blocksize=$blocksize
	! queue 
	! rawvideoparse format=$format_name width=1920 height=1080 framerate=60/1 
	! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=$di  target-bitrate=8000 max-bitrate=8000 enable-pipeline=true
	! h264parse 
	${muxer}
	! fpsdisplaysink name=$sinkname video-sink=$sink text-overlay=false sync=false "

$pipe &
done
wait
