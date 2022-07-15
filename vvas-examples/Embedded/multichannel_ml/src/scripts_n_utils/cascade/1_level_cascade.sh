gst-launch-1.0 -v --no-position \
filesrc location=/home/root/videos/platedetect_sample.mp4   \
! qtdemux ! h264parse ! omxh264dec internal-entropy-buffers=2 ! queue  \
! vvas_xinfer preprocess-config=kernel_pp_yolov3.json infer-config=kernel_yolov3_voc.json name=infer1 ! queue  \
! vvas_xfilter kernels-config="kernel_boundingbox.json" ! queue  \
! kmssink plane-id=34 bus-id="a0130000.v_mix"
