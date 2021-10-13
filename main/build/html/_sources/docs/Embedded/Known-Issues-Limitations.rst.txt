##############################################
Known Issues and Limitations
##############################################

************************************************
Embedded Platform
************************************************

* Item 1. When a GStreamer tee element placed between a source and downstream element that has different stride requirements, the tee element drops the GstVideoMeta API from the allocation query which causes a copy of the buffers, leading to performance degradation.

* Item 2. v4l2src with io-mode=4, does not honor stride. Because of this issue, the display is not as expected.
