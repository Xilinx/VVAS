#########################################
Adaptive Bit Rate Application
#########################################

This script calls four processes of ``ivas_xabrladder`` application simultaneously. ivas_xabrladder is a command line utility that implements the GStreamer video transcoding pipeline. This application expects an input video file (mp4 with H.264/H.265 or H.264/H.265 elementary stream) and produces 5 different H.264/H.265 elementary streams based on codec type provided. The output files are stored at /tmp/ladder_outputs directory. More documentation on ivas_xabrladder can be found at aws-ivas-installer/doc/ivas_xabrladder.docx

Command::

   ./14_gst_app_transcode_plus_scale.sh  0 ~/videos/bbb_sunflower_1080p_60fps_normal.mp4

The format of the script is::

   ./14_gst_app_transcode_plus_scale.sh <device index> <Input MP4 file with H264 content>


