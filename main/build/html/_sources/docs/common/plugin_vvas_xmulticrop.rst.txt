vvas_xmulticrop
================

In ML applications we often need to crop the video frame for processing objects and number of objects per frame may differ. For ML we may also need preprocessing.


`vvas_xmulticrop` is a GStreamer plug-in to do scaling, color space conversion, preprocessing and to crop single or multiple objects.
It takes one input frame and can crop one or more objects from it. Scaling, color space conversion and preprocessing can be applied to all the cropped objects/buffers.
If user wants to get cropped buffer directly on the source pad, they can use static cropping, but with static cropping user can crop only one object.
If user wants to crop more than one objects, they can use dynamic cropping, dynamically cropped objects/buffers are not sent onto the source pad, they are attached as metadata into the output buffer.
Both static and dynamic cropping can be performed simultaneously.

For more implementation details, refer to `vvas_xmulticrop source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/sys/multicrop>`_.

This plug-in supports:

* Single sink pad, single source pad

* Color space conversion

* Resize

* Pre-processing

* Static and Dynamic cropping

vvas_xmulticrop plug-in is similar to ``vvas_xabrscaler`` excepts below features:

* It has only one source pad

* It supports dynamic cropping.


Static Cropping: For cropping only one object. The cropped object/buffer is sent to the source pad. `s-crop-x`, `s-crop-y`, `s-crop-width`, `s-crop-height` are the properties to set the crop parameters.


Dynamic Cropping: For cropping more than one objects from the input buffer. To use dynamic crop feature user must send crop coordinates attached to the buffer in GstVideoRegionOfInterestMeta metadata and GstVideoRegionOfInterestMeta->roi_type must be set to "roi-crop-meta". One instance of GstVideoRegionOfInterestMeta in buffer represents one object for dynamic crop. `vvas_xmulticrop` will dynamically crop the object and attach the cropped objects/buffers to output buffer in GstVideoRegionOfInterestMeta->params. GstVideoRegionOfInterestMeta->params is a GList * of GstStructure. Dynamically cropped buffer is attached into this field, The name of GstStructure is "roi-buffer", and this GstStructure has only one field "sub-buffer" of type GST_TYPE_BUFFER. User should extract this cropped buffer use it and unref it.
User can choose to resize these cropped buffers to some width and height by setting `d-height` and `d-width` properties. If these properties are not set, then cropped buffers will not be resized.

If user wants cropped buffers to be of different format than the input format, they can specify this by setting `d-format` gstreamer property. If this property is not set all cropped buffers will have format same as input buffer.

User specified pre-processing is applied to dynamically cropped buffers only, if user wants it to be applied on output buffer/static crop buffers also, they can set `ppe-on-main buffer` gstreamer property.

Static and dynamic cropping both is possible simultaneously. As explained statically cropped buffer is sent on the source pad and dynamically cropped buffers are attached into that output buffer.

`vvas_xmulticrop` supports at max 39 dynamic crops.
Memory for dynamically cropped buffers is allocated from a GStreamer buffer pool, there is no upper limit on this buffer pool. So, if buffers are not freed, new buffers will be allocated which may lead to more memory consumption.

.. important:: The `vvas_xmulticrop` plug-in controls the image-processing kernel. If your application uses this plug-in, then make sure that image-processing kernel is included in your hardware design.

.. important:: Make sure that the image-processing hardware kernel supports maximum resolution required by your application.

As a reference, maximum resolution supported by image-processing kernel in ``Smart Model Select`` example design can be found in  `image-processing kernel config <https://github.com/Xilinx/VVAS/blob/master/vvas-examples/Embedded/smart_model_select/v_multi_scaler_user_config.h#L33>`_

Prerequisite
----------------

This plug-in requires the image_processing kernel to be available in the hardware design. See :ref:`Image Processing Kernel <image-processing-kernel>`

Input and Output
------------------------

This plug-in accepts buffers with the following color format standards:

* RGBx
* YUY2
* r210
* Y410
* NV16
* NV12
* RGB
* v308
* I422_10LE
* GRAY8
* NV12_10LE32
* BGRx
* GRAY10_LE32
* BGRx
* UYVY
* BGR
* RGBA
* BGRA
* I420
* GBR

.. important:: Make sure that the color formats needed for your application are supported by the image-processing hardware kernel.

As a reference, image-processing configuration for ``smart model select`` example design can be found in `image-processing configuration <https://github.com/Xilinx/VVAS/blob/master/vvas-examples/Embedded/smart_model_select/v_multi_scaler_user_config.h>`_


Control Parameters and Plug-in Properties
------------------------------------------------

The following table lists the GStreamer plug-in properties supported by the vvas_xmulticrop plug-in.

Table 13: vvas_xmulticrop Plug-in Properties

+--------------------+--------------+---------------+------------------------+-------------------+
|                    |              |               |                        |                   |
|  **Property Name** |   **Type**   | **Range**     | **Default**            | **Description**   |
|                    |              |               |                        |                   |
+====================+==============+===============+========================+===================+
| avoid-output-copy  |   Boolean    | true/false    | False                  | Avoid output      |
|                    |              |               |                        | frames copy even  |
|                    |              |               |                        | when downstream   |
|                    |              |               |                        | does not support  |
|                    |              |               |                        | GstVideoMeta      |
|                    |              |               |                        | metadata          |
+--------------------+--------------+---------------+------------------------+-------------------+
| enable-pipeline    |    Boolean   |  true/false   | false                  | Enable buffer     |
|                    |              |               |                        | pipelining to     |
|                    |              |               |                        | improve           |
|                    |              |               |                        | performance in    |
|                    |              |               |                        | non zero-copy     |
|                    |              |               |                        | use cases         |
+--------------------+--------------+---------------+------------------------+-------------------+
| in-mem-bank        | Unsigned int |  0 - 65535    | 0                      | VVAS input memory |
|                    |              |               |                        | bank to allocate  |
|                    |              |               |                        | memory            |
+--------------------+--------------+---------------+------------------------+-------------------+
| out-mem-bank       | Unsigned int |  0 - 65535    | 0                      | VVAS o/p memory   |
|                    |              |               |                        | bank to allocate  |
|                    |              |               |                        | memory            |
+--------------------+--------------+---------------+------------------------+-------------------+
|                    |    string    |    N/A        |         NULL           | The               |
| xclbin-location    |              |               |                        | location of       |
|                    |              |               |                        | xclbin.           |
+--------------------+--------------+---------------+------------------------+-------------------+
|                    |    string    |    N/A        |                        | Kernel name       |
| kernel-name        |              |               | image_processing:      | and               |
|                    |              |               | {image_processing_1}   | instance          |
|                    |              |               |                        | separated         |
|                    |              |               |                        | by a colon.       |
+--------------------+--------------+---------------+------------------------+-------------------+
|    dev-idx         |    integer   | 0 to 31       |    -1                  | Device index      |
|                    |              |               |                        | This is valid     |
|                    |              |               |                        | only in PCIe/     |
|                    |              |               |                        | Data Center       |
|                    |              |               |                        | platforms.        |
+--------------------+--------------+---------------+------------------------+-------------------+
|    ppc             |    integer   | 1, 2, 4       |    2                   | Pixel per         |
|                    |              |               |                        | clock             |
|                    |              |               |                        | supported         |
|                    |              |               |                        | by a image-       |
|                    |              |               |                        | processing        |
|                    |              |               |                        | kernel. Default   |
|                    |              |               |                        | value 2 for Edge, |
|                    |              |               |                        | 4 for PCIe        |
|                    |              |               |                        | platform.         |
+--------------------+--------------+---------------+------------------------+-------------------+
|   scale-mode       |    integer   | 0, 1, 2       |    0                   | Scale algorithm   |
|                    |              |               |                        | to use:           |
|                    |              |               |                        | 0:BILINEAR        |
|                    |              |               |                        | 1:BICUBIC         |
|                    |              |               |                        | 2:POLYPHASE       |
|                    |              |               |                        | Default value     |
|                    |              |               |                        | 0 for Edge,       |
|                    |              |               |                        | 2 for PCIe        |
|                    |              |               |                        | platform.         |
+--------------------+--------------+---------------+------------------------+-------------------+
|    coef-load-type  |  integer     | 0 => Fixed    |    0                   | Type of filter    |
|                    |              | 1 => Auto     |                        | Coefficients to   |
|                    |              |               |                        | be used: Fixed    |
|                    |              |               |                        | or Auto           |
|                    |              |               |                        | generated.        |
|                    |              |               |                        | Default value     |
|                    |              |               |                        | 0 for Edge,       |
|                    |              |               |                        | 1 for PCIe        |
|                    |              |               |                        | platform.         |
+--------------------+--------------+---------------+------------------------+-------------------+
|    num-taps        |  integer     | 6=>6 taps     |    6                   | Number of filter  |
|                    |              | 8=>8 taps     |                        | taps to be used   |
|                    |              | 10=>10 taps   |                        | for scaling.      |
|                    |              | 12=>12 taps   |                        | Default value     |
|                    |              |               |                        | 6 for Edge,       |
|                    |              |               |                        | 12 for PCIe       |
|                    |              |               |                        | platform.         |
+--------------------+--------------+---------------+------------------------+-------------------+
|    alpha-b         |  float       | 0 to 128      |    0                   | Mean subtraction  |
|                    |              |               |                        | for blue channel  |
|                    |              |               |                        | , needed only if  |
|                    |              |               |                        | PPE is required   |
+--------------------+--------------+---------------+------------------------+-------------------+
|    alpha-g         |  float       | 0 to 128      |    0                   | Mean subtraction  |
|                    |              |               |                        | for green channel |
|                    |              |               |                        | , needed only if  |
|                    |              |               |                        | PPE is required   |
+--------------------+--------------+---------------+------------------------+-------------------+
|    alpha-r         |  float       | 0 to 128      |    0                   | Mean subtraction  |
|                    |              |               |                        | for red  channel  |
|                    |              |               |                        | , needed only if  |
|                    |              |               |                        | PPE is required   |
+--------------------+--------------+---------------+------------------------+-------------------+
|    beta-b          |  float       | 0 to 1        |    1                   | Scaling for blue  |
|                    |              |               |                        | channel, needed   |
|                    |              |               |                        | only if PPE is    |
|                    |              |               |                        | required          |
+--------------------+--------------+---------------+------------------------+-------------------+
|    beta-g          |  float       | 0 to 1        |    1                   | scaling for green |
|                    |              |               |                        | channel, needed   |
|                    |              |               |                        | only if PPE is    |
|                    |              |               |                        | required          |
+--------------------+--------------+---------------+------------------------+-------------------+
|    beta-r          |  float       | 0 to 1        |    1                   | scaling for red   |
|                    |              |               |                        | channel, needed   |
|                    |              |               |                        | only if PPE is    |
|                    |              |               |                        | required          |
+--------------------+--------------+---------------+------------------------+-------------------+
|    s-crop-x        | unsigned int | 0 to          |    0                   | Crop X coordinate |
|                    |              | 4294967925    |                        | for static        |
|                    |              |               |                        | cropping          |
+--------------------+--------------+---------------+------------------------+-------------------+
|    s-crop-y        | unsigned int | 0 to          |    0                   | Crop Y coordinate |
|                    |              | 4294967925    |                        | for static        |
|                    |              |               |                        | cropping          |
+--------------------+--------------+---------------+------------------------+-------------------+
|   s-crop-width     | unsigned int | 0 to          |    0                   | Crop width for    |
|                    |              | 4294967925    |                        | static cropping   |
|                    |              |               |                        | (minimum: 16),    |
|                    |              |               |                        | when this is 0    |
|                    |              |               |                        | or not set, it    |
|                    |              |               |                        | will be           |
|                    |              |               |                        | calculated as     |
|                    |              |               |                        | input width -     |
|                    |              |               |                        | `s-crop-x`        |
+--------------------+--------------+---------------+------------------------+-------------------+
|  s-crop-height     | unsigned int | 0 to          |    0                   | Crop height for   |
|                    |              | 4294967925    |                        | static cropping   |
|                    |              |               |                        | (minimum: 16),    |
|                    |              |               |                        | when this is 0    |
|                    |              |               |                        | or not set, it    |
|                    |              |               |                        | will be           |
|                    |              |               |                        | calculated as     |
|                    |              |               |                        | input height -    |
|                    |              |               |                        | `s-crop-y`        |
+--------------------+--------------+---------------+------------------------+-------------------+
|     d-width        | unsigned int | 0 to          |    0                   | Width of          |
|                    |              | 4294967925    |                        | dynamically       |
|                    |              |               |                        | cropped buffers   |
+--------------------+--------------+---------------+------------------------+-------------------+
|     d-height       | unsigned int | 0 to          |    0                   | Height of         |
|                    |              | 4294967925    |                        | dynamically       |
|                    |              |               |                        | cropped buffers   |
+--------------------+--------------+---------------+------------------------+-------------------+
|     d-format       |   integer    | 0,2,4,5,7,8,  |    0                   | Format of         |
|                    |              | 11,12,15,16,  |                        | dynamically       |
|                    |              | 23,25,41,45,  |                        | cropped buffers   |
|                    |              | 48,51,78,79,  |                        |                   |
|                    |              | 83            |                        |                   |
+--------------------+--------------+---------------+------------------------+-------------------+
| ppe-on-main-buffer |   boolean    |   true/false  |    0                   | Apply pre-        |
|                    |              |               |                        | processing on     |
|                    |              |               |                        | main buffer also  |
+--------------------+--------------+---------------+------------------------+-------------------+
| software-scaling   |    Boolean   |  true/false   | false                  | Enable software   |
|                    |              |               |                        | scaling instead   |
|                    |              |               |                        | of accelerated    |
|                    |              |               |                        | scaling.          |
+--------------------+--------------+---------------+------------------------+-------------------+

.. note::

       Image-processing IP has some alignment requirement, hence user given parameters for crop are aligned as per the IP requirement, alignment ensures that it covers the region of crop specified by user, hence final cropped image may have extra pixels cropped. Crop width and height must be at least 16.

Example Pipelines
--------------------

The pipeline mentioned below is for PCIe/Data Center platform. In case you want to execute this pipeline on Embedded platform, then remove **dev-idx** property in the pipelines mentioned below.

* Below pipeline converts NV12 to RGB and performs scaling from 1920x1080 to 640x480. The pipeline mentioned below is for PCIe/Data Center platform.


.. code-block::

        gst-launch-1.0 -v \
        videotestsrc num-buffers=10 ! video/x-raw,format=NV12,width=1920,height=1080 \
        ! vvas_xmulticrop dev-idx=0 xclbin-location=<xclbin path> \
        ! video/x-raw,format=RGB,width=640,height=480 ! filesink location=out.yuv

* Below pipeline performs pre-processing along with color space conversion and scaling on output buffers

.. code-block::

        gst-launch-1.0 -v \
        videotestsrc num-buffers=10 ! video/x-raw,format=NV12,width=1920,height=1080 \
        ! vvas_xmulticrop dev-idx=0 ppe-on-main-buffer=true alpha-r=124 alpha-g=116 alpha-b=104 beta-r=0.547 beta-g=0.56 beta-b=0.557 xclbin-location=<xclbin path> \
        ! video/x-raw,format=RGB,width=640,height=480 ! filesink location=out.yuv

* Below pipeline performs static cropping at (x,y) = (100,80) and (width,height)= (1280,720), this cropped buffers gets scaled to 640x480 and converted to RGB.

.. code-block::

        gst-launch-1.0 -v \
        videotestsrc num-buffers=10 ! video/x-raw,format=NV12,width=1920,height=1080 \
        ! vvas_xmulticrop dev-idx=0 s-crop-x=100 s-crop-y=80 s-crop-width=1280 s-crop-height=720 xclbin-location=<xclbin path> \
        ! video/x-raw,format=RGB,width=640,height=480 ! filesink location=out.yuv

* Below code shows how to add GstVideoRegionOfInterestMeta for dynamic cropping.

.. code-block::

    GstVideoRegionOfInterestMeta *meta;
    meta = gst_buffer_add_video_region_of_interest_meta (buffer, "roi-crop-meta", 0, 0, 0, 0);
    if (meta) {
      meta->id = id;
      meta->parent_id = p_id;
      meta->x =  x;
      meta->y =  y;
      meta->w = w;
      meta->h = h;
      printf("meta: x:%u y:%u, w:%u h:%u", meta->x, meta->y, meta->w, meta->h);
    }

* Below code shows how to read GstVideoRegionOfInterestMeta and how to extract dynamically cropped buffer/object

.. code-block::

     read_crop_meta (GstBuffer *buf) {
       gpointer state = NULL;
       GstMeta *_meta;
       while ((_meta = gst_buffer_iterate_meta_filtered (buf, &state,
                   GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE))) {
         GstStructure *s = NULL;
         GstVideoRegionOfInterestMeta *roi_meta =
                               (GstVideoRegionOfInterestMeta  *) _meta;
         if (g_strcmp0("roi-crop-meta", g_quark_to_string (roi_meta->roi_type))) {
           //This is not the metadata we are looking for
           continue;
         }
         //Got the ROI crop metadata, prepare output buffer
         //Extract dynamically cropped buffer from this meta
         s = gst_video_region_of_interest_meta_get_param (roi_meta, "roi-buffer");
         if (s) {
           GstBuffer *sub_buffer = NULL;
           gst_structure_get (s, "sub-buffer", GST_TYPE_BUFFER, &sub_buffer, NULL);
           if (sub_buffer) {
             //use sub_buffer and unref it
             dump_dynamically_cropped_buffer (sub_buffer);
           } else {
             printf("couldn't get sub buffer");
           }
         } else {
           printf("couldn't get expected struct");
         }
       }
       return TRUE;
     }

* Below code shows how to read/dump dynamically cropped buffers

.. code-block::

     dump_dynamically_cropped_buffer (GstBuffer *sub_buffer) {
       /* Read GstVideoMeta from the buffer, dump the buffer to file */
       GstVideoMeta *vmeta = NULL;
       FILE *fp;
       GstMapInfo map = {0};
       gchar name[256] = {0};
       GstBuffer *new_outbuf;
       GstVideoFrame new_frame = { 0 }, out_frame = { 0 };
       GstVideoInfo *vinfo;

       if (!sub_buffer) {
         return FALSE;
       }

       vmeta = gst_buffer_get_video_meta (sub_buffer);
       if (!vmeta) {
         printf"couldn't get video meta");
       }

       sprintf (name, "dynbuf_%ux%u_%s.yuv", vmeta->width, vmeta->height,
                             gst_video_format_to_string (vmeta->format));
       fp = fopen (name, "wb");
       if (!fp) {
         return FALSE;
       }

       vinfo = gst_video_info_new ();
       gst_video_info_set_format (vinfo, vmeta->format, vmeta->width, vmeta->height);

       new_outbuf = gst_buffer_new_and_alloc (GST_VIDEO_INFO_SIZE (vinfo));
       if (!new_outbuf) {
         printf("couldn't allocate buffer");
         gst_video_info_free (vinfo);
         fclose (fp);
         return FALSE;
       }

       gst_video_frame_map (&out_frame, vinfo, sub_buffer, GST_MAP_READ);
       gst_video_frame_map (&new_frame, vinfo, new_outbuf, GST_MAP_WRITE);
       gst_video_frame_copy (&new_frame, &out_frame);
       gst_video_frame_unmap (&out_frame);
       gst_video_frame_unmap (&new_frame);
       gst_video_info_free (vinfo);

       if (gst_buffer_map ( new_outbuf, &map, GST_MAP_READ)) {
         fwrite (map.data, map.size, 1, fp);
       }
       gst_buffer_unmap (new_outbuf, &map);
       gst_buffer_unref (new_outbuf);
       fclose (fp);
       gst_buffer_unref (sub_buffer);
       return TRUE;
     }

vvas_xmulticrop with software scaling kernel
------------------------------------------------

VVAS plugin "vvas_xmulticrop" can also work with software implementation of the IP. User has to set "software-scaling" property to "true", set the "kernel-name" to "image_processing_sw:{image_processing_sw}",
also set "coef-load-type" to "fixed" type and set "num-taps" to 12. Below are the formats supported by the current implementation.

* NV12
* RGB
* GRAY8
* BGR
* I420

Note: For GRAY8, only scaling is supported, cross format conversion is not supported.

Example pipeline:
^^^^^^^^^^^^^^^^^^^

.. code-block::

        gst-launch-1.0 -v \
        videotestsrc num-buffers=10 ! video/x-raw,format=NV12,width=1920,height=1080 \
        ! vvas_xmulticrop kernel-name="image_processing_sw:{image_processing_sw_1}" software-scaling=true coef-load-type=0 num-taps=12 \
        ! video/x-raw,format=RGB,width=640,height=480 ! filesink location=out.yuv
