gst-launch-1.0 -v --no-position \
filesrc location=/home/root/videos/PLATEDETECT.mp4   \
! qtdemux ! h264parse ! omxh264dec internal-entropy-buffers=2 ! queue  \
! vvas_xinfer preprocess-config=kernel_pp.json infer-config=kernel_yolov3_voc.json name=infer1 ! queue  \
! vvas_xmetaconvert config-location="metaconvert_config.json" ! vvas_xoverlay ! queue \
! kmssink plane-id=34 bus-id="a0130000.v_mix" sync=false
