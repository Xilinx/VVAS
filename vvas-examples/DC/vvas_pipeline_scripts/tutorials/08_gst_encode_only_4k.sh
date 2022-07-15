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

# The U30 Video SDK solution supports real-time decoding and encoding of 4k streams with the following notes:
# - The U30 video pipeline is optimized for live-streaming use cases. For 4k streams with bitrates significantly higher than the ones typically used for live streaming, it may not be possible to sustain real-time performance.
# - When decoding 4k streams with a high bitrate, increasing the number of entropy buffers using the "num-entropy-buf" option can help improve performance
# - When encoding raw video to 4k, set the width option to 3840x2160 to specify the desired resolution.
# - When encoding 4k streams to H.264, the num-slices option is required to sustain real-time performance. A value of 4 is recommended.


# This script accepts an 8-bit, YUV420, 2160p60 RAW file and will send the encoded H.264 output to /tmp/xil_4k_enc_out.mp4 at a rate of 20Mbps.

# You may edit this to accept other sizes (width, height), other framerates (framerate), other output bitrates (target-bitrate, max-bitrate).


# You may change the target codec to H265 by changing:
#     the h264parse plugin to h265parse plugin for encoding to H265

if [ $# -lt 3 ]; then
     echo -e "\e[01;31m"Note : Script did not run. Check the below help on how to run the script and arguments to be given"\e[0m"
     echo "$0 <Device Index> <raw 4K nv12 file> <Number of encode instances, 1> <10bitinput 0/1> <fakesink 0/1>"
     echo "Ex: $0 0 ~/videos/test4K.nv12 1 0 1"
     echo "NOTE: For input raw strem resolutions other than 1080p, change width, height, blocksize and framerate params in the script accordingly"
     echo "NOTE: blocksize is the size of one frame in bytes"
     exit 1
fi

#By default this script generates decoded filesink outputs. Giving 4th argument as 1, script runs for fakesink
if [ -z "$5" ]
then
      echo "No fakesink argument is supplied"
      fakesink=0
else
      echo "fakesink argument is supplied"
      fakesink=$5
fi
#checking rawfile extension or not
input_file=$2
if [[ $input_file = *.nv12* || $input_file = *.NV12* ]] || [[ $input_file = *.yuv* || $input_file = *.YUV* ]] 
then
    echo ""
else
    echo "Please provide input raw files with 8 bit or 10 bit in it. Run $0 for help."
    exit 1
fi
###10 bit input or not
#By default this script generates decoded filesink outputs. Giving 4th argument as 1, script runs for fakesink
if [ -z "$4" ]
then
      echo "no 10bit arg supplied. Hence assuming that 8-bit raw input video stream"
      format_name="nv12"
else
      if [ $4 -eq 1 ]
          then
              echo "10-bit raw input argument is supplied"
              format_name="nv12-10le32"
	      blocksize=16588800
          else
              echo "8-bit raw input argument is supplied"
              format_name="nv12"
	      blocksize=12441600
      fi
fi

echo $format_name

num_dev=$(xbutil examine  | grep xilinx_u30 | wc -l)

if [ $1 -ge $num_dev ]; then
        echo "Device index is not correct, exiting, possible values are 0 to $((num_dev-1))"
    exit
else
    di=$1
fi

d=$3

#Check if number of instances is given a value of at least <1>
if [ $3 -lt 1 ]; then
    echo "Num instances are not correct. It should be 1 at least. Run $0 for help"
    exit 1
fi
sink="fakesink"
pipe=" -v"
for (( c=0; c<$d; c++ ))
do
echo "fakesink value is $fakesink"
if [ $fakesink == "0" ]
then
        sink="\"filesink location=/tmp/xil_4k_enc_out_$c.mp4 \""
	muxer="! qtmux"
        echo "Script is running for filesink ............"
else
        echo "Script is running for fakesink ............"
	muxer=
fi

pipe="filesrc location=$2 blocksize=$blocksize
	! queue
	! rawvideoparse format=$format_name width=3840 height=2160 framerate=60/1
	! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=$di target-bitrate=20000 max-bitrate=20000 num-slices=4 enable-pipeline=true
	! h264parse
	${muxer}
	! fpsdisplaysink video-sink=$sink text-overlay=false sync=false "$pipe
done
pipe="gst-launch-1.0 "$pipe
$pipe


