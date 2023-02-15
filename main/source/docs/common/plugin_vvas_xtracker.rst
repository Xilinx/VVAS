vvas_xtracker
===============

The tracker plug-in tracks detected objects in a sequence of video frames. Because tracking is easier than detection, tracking algorithms can use fewer computational resources 
than running an object detector on every frame and hence finds many applications.

Each tracker element in the frame is given a unique ID and is initialized with the coordinates of the object detected by ML block. 
The object is snipped from the frame and taken as a template. Afterwards, the tracker element updates the filter every time that there is an object hit, 
adapting itself to the possible changes which an object might have during the video. 

The tracker plug-in supports three types of object tracking algorithms, namely **Intersection-Over-Union (IOU), Kernelized Correlation Filters (KCF) and 
Minimum Output Sum of Squared Error (MOSSE)**.  IOU algorithm is used when detection output is available for every frame.  
Kernelized Correlation Filters (KCF) algorithm or Minimum Output Sum of Squared Error (MOSSE) filter algorithm implemented completely in software 
using optimizations for the host processor.

Tracker plugin supports tracking up to 16 objects. Tracker Id starts from 0 and supported till MAX_INT32, -1 will be used when Id is not aaigned yet by tracker.


.. figure:: ../images/tracker_embedded_pipeline.PNG
   :align: center
   :scale: 80



This pipeline depicts the typical multi-stream usecase where, multiple streams are muxed by funnel element to form a batch for inference.
With tracker in the pipeline, not all the frames are required to be fed for inference. :ref:`vvas_xskipframe` plugin controls the rate at which the frames needs to be sent for inference.
:ref:`vvas_xreorderframe` plugin receives the frames that are inferred and that are skipped and arranges them in the correct order of presentation before forwarding frames downstream.

Optionally, if less CPU is available on the host, the frames can be scaled to lower resolution (can be scaled to model resolution or nearest resolution that maintains aspect ratio) before submitting to Tracker so as to avoid CPU load of Tracker algorithm that requires to work on larger areas if not scaled down.

The frames in the correct order are presented to :ref:`vvas_xtracker` plugin to track the objects that were detected by inference.
Tracker waits for object to be found in multiple frames before assigning Tracker Id to it and attaches the tracked bounding box information in the GstBuffer metadata.

If video resolution is scaled down for tracker, metaaffixer plugin should be added to the pipeline to scale the bounding box co-ordinates received from tracker to original video resolution. 
Metaaffixer scales and updates the Inference metadata to original video resolution and pushes the frames downstream.

Downstream plugins read the bounding box information of the objects from inference metadata attached to the GstBuffer and does further processing.


The :ref:`vvas_xskipframe`, :ref:`vvas_xreorderframe` and :ref:`vvas_xtracker` are multi-stream aware and supports buffers of multiple streams on their sink and source GstPads.


Input and Output
--------------------

Accepts buffers with NV12 color formats on sink and source pads.

Control Parameters and Plugin Properties
---------------------------------------------

The following table lists the GStreamer plugin properties supported by the vvas_xtracker plugin.

Table 8: vvas_xtracker plugin properties


+-----------------------+-------------+---------------+-----------------------+-----------------------+
|                       |             |               |                       |                       |
|  **Property Name**    |   **Type**  | **Range**     |    **Default**        |    **Description**    |
|                       |             |               |                       |                       |
+=======================+=============+===============+=======================+=======================+
| tracker-algo          |   Enum      |   0, 1, 2     |          0            | Tracker algorithm to  |
|                       |             |               |                       | use                   |
|                       |             |               |                       |                       |
|                       |             |               |                       | 0 - Tracker IOU       |
|                       |             |               |                       | algorithm             |
|                       |             |               |                       |                       |
|                       |             |               |                       | 1 - Tracker MOSSE     |
|                       |             |               |                       | algorithm             |
|                       |             |               |                       |                       |
|                       |             |               |                       | 2 - Tracker KCF       |
|                       |             |               |                       | algorithm             |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| IOU-with-color        |   Boolean   |    0 or 1     |           0           | To use color          |
|                       |             |               |                       | information during    |
|                       |             |               |                       | matching when tracker |
|                       |             |               |                       | algo is IOU           |
|                       |             |               |                       |                       |
|                       |             |               |                       | 0 - No color          |
|                       |             |               |                       | information for       |
|                       |             |               |                       | matching              |
|                       |             |               |                       |                       |
|                       |             |               |                       | 1 - Use color         |
|                       |             |               |                       | information for       |
|                       |             |               |                       | matching              |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| obj-match-color-space |   Enum      |   0, 1        |          1            | Color space to use    |
|                       |             |               |                       | for objects matching  |
|                       |             |               |                       |                       |
|                       |             |               |                       | 0 - RGB color space   |
|                       |             |               |                       | for object match      |
|                       |             |               |                       |                       |
|                       |             |               |                       | 1 - HSV color space   |
|                       |             |               |                       | for object match      |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| feature-length        |   Enum      |  22, 31       |         31            | Object feature length |
|                       |             |               |                       | (required only for    |
|                       |             |               |                       | KCF algorithm)        |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| search-scale          |   Enum      | 0, 1, 2       |         0             | Scales to verify for  |
|                       |             |               |                       | object localization   |
|                       |             |               |                       |                       |
|                       |             |               |                       | 0 - Search all scales |
|                       |             |               |                       | (up, down and same    |
|                       |             |               |                       | scale)                |
|                       |             |               |                       |                       |
|                       |             |               |                       | 1 - Search up and     |
|                       |             |               |                       | same scale            |
|                       |             |               |                       |                       |
|                       |             |               |                       | 2 - Search down and   |
|                       |             |               |                       | same scale            |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| inactive-wait-        | Integer     |  0 to         |        200            | Number of frames      |
| interval              |             |  4294967295   |                       | to wait before        |
|                       |             |               |                       | stopping tracking of  |
|                       |             |               |                       | inactive objects      |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| min-object-width      | Integer     |  0 to         |        20             | Minimum object width  |
|                       |             |  4294967295   |                       | in pixels to consider |
|                       |             |               |                       | for tracking          |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| min-object-height     | Integer     |  0 to         |        60             | Minimum object height |
|                       |             |  4294967295   |                       | in pixels to consider |
|                       |             |               |                       | for tracking          |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| max-object-width      | Integer     |  0 to         |        200            | Objects width above   |
|                       |             |  4294967295   |                       | maximum width are not |
|                       |             |               |                       | considered for        |
|                       |             |               |                       | tracking              |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| max-object-height     | Integer     |  0 to         |        360            | Objects height above  |
|                       |             |  4294967295   |                       | maximum height are    |
|                       |             |               |                       | not considered for    |
|                       |             |               |                       | tracking              |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| num-frames-confidence | Integer     |  0 to         |        3              | Number of times       |
|                       |             |  4294967295   |                       | object need to be     |
|                       |             |               |                       | detected continuously |
|                       |             |               |                       | to consider for       |
|                       |             |               |                       | tracking              |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| match-search-region   | Float       |  1.0 to 2.0   |        1.5            | Object search region  |
|                       |             |               |                       | during detection for  |
|                       |             |               |                       | IOU based matching    |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| relative-search-      | Float       |  1.0 to 2.5   |        1.5            | Object search region  |
| region                |             |               |                       | with respect to       |
|                       |             |               |                       | detection coordinates |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| correlation-threshold | Float       |  0.1 to 1.0   |        0.7            | Object correlation    |
|                       |             |               |                       | threshold value for   |
|                       |             |               |                       | matching              |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| correlation-weight    | Float       |  0.0 to 1.0   |       0.7             | Weightage for         |
|                       |             |               |                       | correlation value     |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| overlap-threshold     | Float       | 0.0 to 1.0    |        0              | Object overlap        |
|                       |             |               |                       | threshold to consider |
|                       |             |               |                       | for matching          |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| overlap-weight        | Float       |  0.0 to 1.0   |       0.2             | Weightage for         |
|                       |             |               |                       | overlap value         |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| scale-change-         | Float       |  0.001 to 1.0 |        0.7            | Maximum object scale  |
| threshold             |             |               |                       | change threshold to   |
|                       |             |               |                       | consider for matching.|
|                       |             |               |                       | Value of 1 means      |
|                       |             |               |                       | double the scale      |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| scale-change-weight   | Float       |  0.0 to 1.0   |       0.1             | Weightage for         |
|                       |             |               |                       | change in scale       |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| occlusion-threshold   | Float       |  0.0 to 1.0   |       0.4             | Threshold for         |
|                       |             |               |                       | considering object as |
|                       |             |               |                       | occluded              |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| confidence-score-     | Float       |  0.0 to 1.0   |       0.25            | Confidence score of   |
| threshold             |             |               |                       | tracker to be         |
|                       |             |               |                       | consider for tracking |
|                       |             |               |                       | objects               |
+-----------------------+-------------+---------------+-----------------------+-----------------------+
| skip-inactive-objs    | Boolean     |  0 or 1       |       0               | Whether to mark the   |
|                       |             |               |                       | inactive tracker      |
|                       |             |               |                       | objects to be skipped |
|                       |             |               |                       | by downstream plugins |
|                       |             |               |                       | or not.               |
|                       |             |               |                       |                       |
|                       |             |               |                       | 0 - Inactive objects  |
|                       |             |               |                       | are processed by      |
|                       |             |               |                       | downstream plugins    |
|                       |             |               |                       |                       |
|                       |             |               |                       | 1 - Inactive objects  |
|                       |             |               |                       | marked not to be      |
|                       |             |               |                       | processed by          |
|                       |             |               |                       | downstream plugins    |
+-----------------------+-------------+---------------+-----------------------+-----------------------+


Example Pipeline
-----------------

The following is an example pipeline to demonstrate usage of vvas_xtracker plugin with inference performed every 6th frame.

.. code-block::

   gst-launch-1.0 filesrc location=cars_1.264 ! \
   h264parse ! queue ! \
   omxh264dec internal-entropy-buffers=2 ! \
   queue ! \
   vvas_xfunnel name=xfunnel_0_0 ! \
   vvas_xskipframe name=skip_0_0 infer-interval=6 ! \
   vvas_xinfer preprocess-config=kernel_preprocessor_dev0_yolov3_voc_tf.json infer-config=kernel_yolov3_voc_tf.json name=infer ! \
   vvas_xreorderframe name=reorder_0_0 ! \
   tee name=t_0_0 ! \
   queue max-size-bytes=0 max-size-buffers=1 max-size-time=0 ! \
   vvas_xabrscaler xclbin-location="/run/media/mmcblk0p1/dpu.xclbin" kernel-name=image_processing:{image_processing_1} avoid-output-copy=true ! \
   video/x-raw, width=416, height=416, format=NV12 ! \
   vvas_xtracker skip-inactive-objs=true min-object-width=20 max-object-width=1900 min-object-height=20 max-object-height=1000 match-search-region=1 num-frames-confidence=2 ! \
   ma_1_0.sink_master vvas_xmetaaffixer name=ma_1_0 sync=false ma_1_0.src_master ! \
   fakesink ma_1_0.src_slave_0 ! \
   queue max-size-bytes=0 max-size-buffers=1 max-size-time=0 ! \
   vvas_xdefunnel name=xdefunnel_0_0 \
   skip_0_0. ! reorder_0_0. \
   t_0_0. ! \
   queue max-size-bytes=0 max-size-buffers=1 max-size-time=0 ! \
   ma_1_0.sink_slave_0 xdefunnel_0_0. ! \
   queue min-threshold-buffers=4 ! \
   vvas_xmetaconvert config-location=metaconvert_config.json ! \
   vvas_xoverlay ! \
   queue ! \
   fpsdisplaysink name="fpssink0" video-sink=\"fakesink async=false sync=false\" text-overlay=false sync=false -v
