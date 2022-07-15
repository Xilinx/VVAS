gst-launch-1.0 -v --no-position \
 filesrc location=/home/root/videos/FACEDETECT.mp4 \
  ! qtdemux ! h264parse ! omxh264dec internal-entropy-buffers=2 ! queue \
  ! vvas_xinfer preprocess-config=kernel_pp_facedectect.json infer-config=kernel_densebox_320_320.json name=infer1 ! queue \
  ! vvas_xfilter kernels-config="kernel_boundingbox.json" ! queue \
  ! fpsdisplaysink video-sink="kmssink plane-id=34 bus-id=a0130000.v_mix render-rectangle=<0,0,1920,1080>" text-overlay=false sync=false \
filesrc location=/home/root/videos/YOLOV3.mp4 \
  ! qtdemux ! h264parse ! omxh264dec internal-entropy-buffers=2 ! queue \
  ! vvas_xinfer preprocess-config=kernel_pp_yolov3.json infer-config=kernel_yolov3_adas_pruned_0_9.json name=infer2 ! queue \
  ! vvas_xfilter kernels-config="kernel_boundingbox.json" ! queue \
  ! fpsdisplaysink video-sink="kmssink plane-id=35 bus-id=a0130000.v_mix render-rectangle=<1920,0,1920,1080>" text-overlay=false sync=false \
filesrc location=/home/root/videos/CLASSIFICATION.mp4 \
  ! qtdemux ! h264parse ! omxh264dec internal-entropy-buffers=2 ! queue \
  ! vvas_xinfer preprocess-config=kernel_pp_resnet50.json infer-config=kernel_resnet50.json name=infer3 ! queue \
  ! vvas_xfilter kernels-config="kernel_boundingbox.json" ! queue \
  ! fpsdisplaysink video-sink="kmssink plane-id=36 bus-id=a0130000.v_mix render-rectangle=<0,1080,1920,1080>" text-overlay=false sync=false \
filesrc location=/home/root/videos/REFINEDET.mp4 \
  ! qtdemux ! h264parse ! omxh264dec internal-entropy-buffers=2 ! queue \
  ! vvas_xinfer preprocess-config=kernel_pp_refinedet.json infer-config=kernel_refinedet_pruned_0_96.json name=infer4 ! queue \
  ! vvas_xfilter kernels-config="kernel_boundingbox.json" ! queue \
  ! fpsdisplaysink video-sink="kmssink plane-id=37 bus-id=a0130000.v_mix render-rectangle=<1920,1080,1920,1080>" text-overlay=false sync=false \
