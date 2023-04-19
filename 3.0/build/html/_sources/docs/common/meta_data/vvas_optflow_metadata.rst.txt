.. _vvas_optflow_metadata:

*************************
VVAS Opticalflow Metadata
*************************

VVAS optical flow metadata structure hold the information of motion of frame in x and y direction and object motion information. VVAS optical flow plugin set the optical flow meta data of frame. This metadata structure also supports storing of motion information in object level for further analysis by downstream plugins.  The GStreamer plug-ins can set and get optical flow metadata from the GstBuffer by using the gst_buffer_add_meta () API and gst_buffer_get_meta () API respectively.

================
 GstOptflowMeta
================

GstOptflowMeta stores the information of optical flow of frames and object motion information.

.. code-block::


      struct _vvas_obj_motinfo
      {
        float mean_x_displ;
        float mean_y_displ;
        float angle;
        float dist;
        char dirc_name[DIR_NAME_SZ];
        BoundingBox bbox;
      };


      /**
      * GstVvasOverlayMeta:
      * @num_objs: number of objects with motion information
      * @obj_mot_infos: list of objects
      * @x_displ: pointer to motion data of frame in x-direction
      * @y_displ: pointer to motion data of frame in y-directiont
      */
      struct _GstVvasOFMeta
      {
        GstMeta meta;

        guint num_objs;
        GList *obj_mot_infos;

        GstBuffer *x_displ;
        GstBuffer *y_displ;

      };

