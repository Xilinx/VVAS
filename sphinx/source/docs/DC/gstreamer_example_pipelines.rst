################################
GStreamer Pipelines
################################

This section covers various example pipelines using ``gst-launch``.

.. note::
         Running each script without any argument gives the example on how to run that script and the required arguments. Go to scripts folder inside release package folder and run the below scripts. By default, all scripts use filesink plugin and writes the output files into /tmp directory.

There is a chance that due to the massive bandwidth required for operating on these RAW files, you will notice a drop in FPS; this is not due to the U30 card, but the disk speeds. We recommend reading/writing from /dev/shm which is a RAM disk. Most of the scripts also have provision to use fakesink plugin which displays only performance numbers and will not write output to files. This is done by giving last argument “fakesink” to 1. Each script also has checks along with the Gstreamer pipeline command to help users avoid giving incorrect arguments.

Set VVAS environment variables using below command (this must be done in every current terminal session):
::

        source /opt/xilinx/xcdr/setup.sh
        source /opt/xilinx/ivas/setup.sh

        Missing to source the above setup script in current terminal, it gives the error “xbutil: command not found”


********************************************************
Decode only Pipeline
********************************************************

This example accepts a clip that is already encoded in h.264 and will decode it using ivas_xvcudec plugin file into a RAW NV12 format and save it to disk at /tmp/xil_dec_out_*.nv12. To get more understanding of the ivas_xvcudec plugin properties, refer to the section VVAS VCU Decoder Plug-in.
::
        ./01_gst_decode_only.sh 0 ~/videos/Test_1080p60.h264 1 0

The format of the script is:
::

        ./01_gst_decode_only.sh <device index> <Input H264 file> <Number of decoder instances, 1 to 8> <fakesink 0/1>

        All the scripts work with h.264 input streams. To use h.265 input stream, update the scripts to use “h265parse” 
        plugin instead of “h264parse”. Similarly, the encoder profile must be changed from “high” to “main” for h.265 
        streams and the levels accordingly.

********************************************************
Encode only Pipeline
********************************************************

This example accepts a RAW 1080p60 clip in NV12 format. It will pass the clip to the encoder using ``ivas_xvcuenc`` plugin  to produce an h.264 encoded outputs with a target bitrate of 8Mbps and save it to disk at /tmp/xil_enc_out_*.h264. To get more understanding of the ivas_xvcuenc plugin properties, refer to the section VVAS VCU Encoder Plug-in.
::
        ./02_gst_h264_encode_only_1080p.sh 0 ~/videos/Test_1080p.nv12 1 1

The format of the script is:
::
        ./02_gst_h264_encode_only_1080p.sh <device index> <Input 1080p NV12 file> <Number of encoder instances, 1 to 8> <fakesink 0/1>

********************************************************
Basic Transcode
********************************************************


This example demonstrates how to achieve simple transcoding, i.e. encoding format change without resolution change. Input is H.264 elementary stream, 1080p60, from file source, decoded using hardware decoder and then re-encoded using vcu hardware encoder to H.264 format. To get more understanding of the ``ivas_xvcudec`` plugin properties, refer to the section VVAS VCU Decoder Plug-in. To get more understanding of the ``ivas_xvcuenc`` plugin properties, refer to the section VVAS VCU Encoder Plug-in.
The output files are stored at /tmp/xil_xcode_out_*.h264
::
        ./03_gst_h264_transcode_only.sh 0 ~/videos/Test_1080p60.h264 1 0

The format of the script is:
::
        ./03_gst_h264_transcode_only.sh <device index> <Input H264 file> <Number of transcode instances, 1 to 8> <fakesink 0/1>

********************************************************
Decode only into Multiple-Resolution outputs
********************************************************


This example will decode an 8-bit, YUV420, pre-encoded 60FPS MP4 file with h.264 content as input and  scales this decoded output into multiple renditions of various sizes and send them back to disk in /tmp/sink_dec_scal*.nv12. 
The 1080p60 input is scaled down to the following resolutions and framerates (respectively):
720p60, 720p30, 480p30, 360p30, 288p30
::
        ./04_gst_decode_plus_scale.sh 0 ~/videos/bbb_sunflower_1080p_60fps_normal.mp4 1 -1 0

The format of the script is:
::
        ./04_gst_decode_plus_scale.sh <device index> <Input MP4 file with H264> <num instances, 1 to 4> <Number of buffers> <fakesink 0/1>
        Num of buffers as -1 will run the complete video stream. For any positive value, it will decode only those many buffers

********************************************************
Encode only into Multiple-Resolution outputs
********************************************************

This example will take a 1080p60 RAW NV12 file and scale it and encode it into the resolutions as defined below and save them to disk under **/tmp/sink_scale_enc*.h264**
720p60, 720p30, 480p30, 360p30, 288p30
::
        ./05_gst_encode_plus_scale_1080p.sh 0 ~/videos/Test_1080p.nv12 1 -1 0

The format of the script is:
::
        ./05_gst_encode_plus_scale_1080p.sh <device index> <8-bit, YUV420, 1080p60 RAW file> <num instances, 1 to 4> <Number of buffers><fakesink 0/1>

********************************************************
Transcode with Multiple-Resolution outputs
********************************************************


This example will do a full transcode pipeline on an 1080p60 h.264 input, scale it into the resolutions below, and re-encode them, saving them in /tmp/sink_xcode_scale_*.h264

The 1080p60 input is scaled down and encoded back to the following resolutions and framerates (respectively):
720p60, 720p30, 480p30, 360p30, 288p30
::
./06_gst_transcode_plus_scale.sh 0 ~/videos/bbb_sunflower_1080p_60fps_normal.mp4 1 -1 0

The format of the script is:
::
        ./06_gst_transcode_plus_scale.sh <device index> <Input MP4 file with H264 content> <num instances, 1 to 4> <Number of buffers> <fakesink 0/1>

********************************************************
Lower latency Transcode with Multiple-Resolution outputs
********************************************************

This example is the same as full transcode pipeline (decode, scale, encode), saving the scaled outputs into the files /tmp/sink_ll_*.h264. This differs in that it is a ``low latency`` version, which doesn't encode ``B-frames``, and reduces the lookahead depth. This, in short, decreases the latency at the cost of visual quality.
This example will output corrupt data if you provide an input file that contains B-Frames.
::
        ./07_gst_transcode_plus_scale_lowlatency.sh 0 ~/videos/bbb_sunflower_1080p_60fps_normal.mp4 1 -1 0

The format of the script is:
::
        ./07_gst_transcode_plus_scale_lowlatency.sh <device index> <Input MP4 file with H264 content> <num instances, 1 to 4> <Number of buffers><fakesink 0/1>

********************************************************
4K Considerations
********************************************************


4k videos are supported with two notes below; otherwise, no changes to the commands/examples above are required, except when processing RAW input files where width x height is changed from 1920x1080 to 3840x2160.

.. note::
        Scaling Restrictions

        In this current release (0.96.0), 4k is only supported without a scaler in the pipeline. You may:
        - Decode Only
        - Encode Only
        - Transcode (Decode -> Encode)


Encoding
========================

4K H.264 Live-Encode
 While most use-cases will not involve live-streaming 4k sized H.264/HEVC files, should you wish to do this, an example command line follows and the output file is stored at **/tmp/xil_4k_enc_out.h265**
::
        ./08_gst_encode_only_4k.sh 0 ~/videos/test4K.nv12 1 0

The format of the script is:
::
        ./08_gst_encode_only_4k.sh <Device Index> <raw 4K nv12 file> <Number of encode instances, 1 to 2> <fakesink 0/1>

4K H.264 Live-Transcode
 The output is stored at `/tmp/xil_4k_xcode.h264`. Command:
::
 ./09_gst_transcode_only_4k.sh 0 ~/videos/xil_4k.h264 1 0


The format of the script is:
::
./09_gst_transcode_only_4k.sh <Device Index> <Input 4K H264 file> <Number of transcode instances, 1 to 2><fakesink 0/1>

**************************************************
Faster Than Real Time
**************************************************


The U30 is optimized for ``low latency`` ``realtime`` applications. It provides deterministic low latency transcoding, while operating at the FPS the human eye would normally process/watch it. This is ideal for ingesting a live video stream where there is minimal buffering.
Faster Than Real Time (FTRT) is almost the contrary: you have the entire file/clip saved and can therefore “divide and conquer”. There are two main flags to consider when processing in this flow: cores and slices. The outputs are stored in `/tmp/xil_xcode_ftrt_out_*.h264`. This script can be best demonstrated only if the number of instances are less than what a device is capable of, e.g. 1 instance as shown below:
::
        ./11_gst_ftrt_transcode_only.sh 0 ~/videos/Test_1080p60.h264 1 1

The format of the script is:
::
        ./11_gst_ftrt_transcode_only.sh <device index> <Input H264 file> <Number of transcode instances, 1 to 8> <fakesink 0/1>

