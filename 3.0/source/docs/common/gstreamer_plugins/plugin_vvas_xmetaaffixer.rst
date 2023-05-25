.. _vvas_xmetaaffixer:

vvas_xmetaaffixer
==================

The metaaffixer plug-in, ``vvas_xmetaaffixer``, is used to scale the incoming metadata information for the different resolutions. A machine learning (ML) operation can be performed on a different frame resolution and color format than the original frame, but the metadata might be associated with the full resolution, original frame. The vvas_metaaffixer has two types of pads, master pads and slave pads. Input pads are pads on request; the number of input pads can be created based on the number of input sources. There is one mandatory master input pad (sink pad) that receives the original/reference metadata. Other input pads are referred to as slave pads. Metadata received on the master sink pad is scaled in relation to the resolution of each of the slave sink pads. The scaled metadata is attached to the buffer going out of the output (source) slave pads. There can be up to 16 slave pads created as required. For implementation details, refer to `vvas_xmetaaffixer source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/gst/metaaffixer>`_

.. figure:: ../../images/metaaffixer_pipeline.png


Input and Output
--------------------

This plug-in is format agnostic and can accept any input format. It operates only on the metadata. The vvas_xmetaaffixer plug-in supports the GstInferenceMeta data structure. For details about this structure, refer to the :doc:`VVAS Inference Metadata <../meta_data/vvas_meta_data_structures>` section.


Control Parameters and Plug-in Properties
--------------------------------------------------------

Table 2: vvas_xmetaaffixer Plug-in Properties

+--------------------+-------------+------------------+-------------+--------------------------------------------------------+
|                    |             |                  |             |                                                        |
| **Property Name**  |   **Type**  |  **Range**       | **Default** |                   **Description**                      |
|                    |             |                  |             |                                                        |
+====================+=============+==================+=============+========================================================+
|    sync            |    Boolean  |  True/False      |    True     | This property is to enable the synchronization         |
|                    |             |                  |             | between master and slave pads buffers.                 |
|                    |             |                  |             | If sync=true is set, then the metadata is scaled       |
|                    |             |                  |             | and attached to buffers on slave pads that have        |
|                    |             |                  |             | matching PTS or PTS falls within frame duration of the |
|                    |             |                  |             | buffer on the master sink pad.                         |
|                    |             |                  |             | If sync=false is set on the element, then the          |
|                    |             |                  |             | metadata is scaled and attached to all the             |
|                    |             |                  |             | buffers on the slave pads. If this option is used,     |
|                    |             |                  |             | there is possibility that the metadata is not          |  
|                    |             |                  |             | suitable for the frames/buffers that are not           |
|                    |             |                  |             | corresponding to the frames/buffers on the master      |
|                    |             |                  |             | pad.                                                   |
+--------------------+-------------+------------------+-------------+--------------------------------------------------------+
|    timeout         |    Int64    |  -1 to           |    2000     | Timeout in millisec. Plug-in will wait for "timeout"   |
|                    |             |  9223372036854   |             | duration for a buffer on other pads before pushing     |
|                    |             |                  |             | buffer on output pad. This is to avoid hang in case    |
|                    |             |                  |             | buffers are arriving at different frame rates and      |
|                    |             |                  |             | slave pad buffer is waiting for another buffer at      |
|                    |             |                  |             | master pad (which may not arrive as there is no more   |
|                    |             |                  |             | buffers available).                                    |
+--------------------+-------------+------------------+-------------+--------------------------------------------------------+


Pad naming syntax
---------------------------

The pad naming syntax is listed, and the following image shows the syntax:

* MetaAffixer master input pad should be named sink_master.

* MetaAffixer master output pad should be named src_master.

* MetaAffixer slave input pad should be named sink_slave_0, sink_slave_1.

* MetaAffixer slave output pad should be named src_slave_0, src_slave_1, src_slave_2.

.. figure:: ../../images/metaaffixer_plug-in.png 


Example Pipelines
-----------------------------

This section covers the example pipelines using the metaaffixer plug-in. 

.. code-block::

        gst-launch-1.0 videotestsrc num-buffers=1 \
        ! video/x-raw, width=1920, height=1080, format=NV12 \
        ! queue \
        ! videoconvert \
        ! queue \
        ! ima.sink_master vvas_xmetaaffixer name=ima ima.src_master \
        ! queue \
        ! fakesink videotestsrc num-buffers=1 \
        ! video/x-raw, width=1920, height=1080, format=NV12 \
        ! queue \
        ! videoconvert \
        ! video/x-raw, width=1920, height=1080, format=YUY2 \
        ! ima.sink_slave_0 ima.src_slave_0 \
        ! queue \
        ! fakesink -v.. _vvas_xmetaaffixer:

