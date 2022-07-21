############################
Basic Transcoding pipelines
############################

This section covers how to build and launch basic transcoding pipelines using VVAS. There are different types of transcoding use cases and each one is covered in detail. VVAS plug-ins supports H264 and H265 (HEVC) encoding formats only. For improving quality, VVAS also supports ``Lookahead`` feature through ``vvas_xlookahead`` plug-in and kernel on U30 platform.

Pre-requisite
---------------

Before start executing the Transcoding usecases, make sure the setup is ready:

* Required Alveo U30 cards are installed in the PCIe slots of the server machine.
* VVAS 2.0 Package has been installed and the Alveo U30 cards are flashed with the required image. If not, follow the section :doc:`Alveo U30 setup for VVAS 2.0 <../u30_platform/u30_setup>`

If your setup is re-booted, or you have opened a fresh terminal, perform the following steps.

.. code-block:: shell

    /opt/xilinx/xcdr/setup.sh

 
Basic transcode pipeline
-------------------------

In this use-case, there is only change of the encoding format. Frame resolution remains same. vvas_xvcudec is decoder plug-in and vvas_xvcuenc is encoder plugin. The pipeline mentioned below is not using many parameters and relying on default plugin properties. You may provide different configuration parameters in thi pipeline to see the difference in the encoded output.
For detailed information about the available configuration parameters, refer to plug-in description :ref:`vvas_xvcudec` and :ref:`vvas_xvcuenc`

Please note that 'dev-idx' parameter range can vary from 0 to N where N can be obtained with below command:

.. code-block:: shell

    N=xbutil  examine | grep xilinx_u30 | wc -l

.. code-block:: shell

    gst-launch-1.0 filesrc location=<mp4 file> \
    ! qtdemux \
    ! h264parse \
    ! vvas_xvcudec dev-idx=3 xclbin-location=<xclbin file path> \
    ! vvas_xvcuenc dev-idx=3 xclbin-location=<xclbin file path> \
    ! h265parse \
    ! fpsdisplaysink name=fpssink video-sink="fakesink" text-overlay=false sync=false -v

The above pipeline transcodes a given mp4 file encoded with H264 codec to H265 codec and displays the rate, frames per second, at which it is transcoding the input. Currently output is not stored into any file. In case you want to store the transcoded output, change the 'fakesink' to 'filesink'

Transcode pipeline with scaling (resize)
-----------------------------------------

In earlier example, we have transcoded an input stream while retaining original frame resolution. In some usecases, we may have to resize the input frame to a different resolution and then encode. This resize operation is achieved using :ref:`vvas_xabrscaler` plug-in. This is hardware accelerated resize operation.

Please note that 'dev-idx' parameter range can vary from 0 to N where N can be obtained with below command:

.. code-block:: shell

    N=xbutil  examine | grep xilinx_u30 | wc -l

In below example, 1080p stream is decoded, scaled down to 720p and then encoded to H265 stream of 720p.

.. code-block:: shell

    gst-launch-1.0 filesrc location=<input mp4 file> \
    ! qtdemux \
    ! h264parse \
    ! vvas_xvcudec dev-idx=3 xclbin-location=<xclbin file path> \
    ! vvas_xabrscaler dev-idx=3 xclbin-location=<xclbin file path> scale-mode=2 \
    ! video/x-raw, width=1280, height=720 \
    ! queue max-size-buffers=1 \
    ! vvas_xvcuenc dev-idx=3 xclbin-location=<xclbin file path> \
    ! h265parse \
    ! fpsdisplaysink name=fpssink video-sink="fakesink" text-overlay=false sync=false -v

ABR ladder pipeline
-------------------

ABR (Adaptive Bitrate) Encoding is required in scenarios where one needs to transmit the streams at different bitrate and resolutions. In this case one input stream is transcoded with different resolutions and encoding parameters as needed. This is very useful use case.

Input stream is decoded using 'vvas_xvcudec' plug-in. 'vvas_xabrscaler' plug-in is used to resize the input frame into several output resolutions. Each output resolution from vvas_xabrscaler will be encoded into a separate output stream. Each encoding session can be of different configurations, as captured in the example below:

.. figure:: ../images/abr_ladder.png

.. code-block:: shell

    gst-launch-1.0 filesrc num-buffers=2000 location=<input mp4 file> \
    ! qtdemux \
    ! queue \
    ! h264parse \
    ! vvas_xvcudec dev-idx=3 xclbin-location=<xclbin file path> \
    ! queue \
    ! vvas_xabrscaler xclbin-location=<xclbin file path> dev-idx=3 ppc=4 scale-mode=2 avoid-output-copy=true name=sc_03 sc_03.src_0 \
    ! queue \
    ! video/x-raw, width=1280, height=720 \
    ! tee name=tee_03 tee_03. \
    ! queue \
    ! videorate \
    ! video/x-raw, framerate=60/1 \
    ! vvas_xvcuenc xclbin-location=<xclbin file path> name=enc_720p60_dev3_0 dev-idx=3 target-bitrate=4000 \
    ! h264parse \
    ! fpsdisplaysink name=sink_xcode_scale_720p60_dev3_0 video-sink=fakesink text-overlay=false sync=false tee_03. \
    ! queue \
    ! videorate \
    ! video/x-raw, framerate=30/1 ! vvas_xvcuenc xclbin-location=<xclbin file path> name=enc_720p30_dev3_0 dev-idx=3 target-bitrate=3000 \
    ! h264parse ! fpsdisplaysink name=sink_xcode_scale_720p30_dev3_0 video-sink=fakesink text-overlay=false sync=false sc_03.src_1 \
    ! queue \
    ! video/x-raw, width=848, height=480 \
    ! videorate \
    ! video/x-raw, framerate=30/1 \
    ! vvas_xvcuenc name=enc_480p30_dev3_0 dev-idx=3 target-bitrate=2500 xclbin-location=<xclbin file path> \
    ! h264parse \
    ! fpsdisplaysink name=sink_xcode_scale_480p30_dev3_0 video-sink=fakesink text-overlay=false sync=false sc_03.src_2 \
    ! queue \
    ! video/x-raw, width=640, height=360 \
    ! videorate \
    ! video/x-raw, framerate=30/1 \
    ! vvas_xvcuenc name=enc_360p30_dev3_0 dev-idx=3 target-bitrate=1250 xclbin-location=<xclbin file path> \
    ! h264parse \
    ! fpsdisplaysink name=sink_xcode_scale_360p30_dev3_0 video-sink=fakesink text-overlay=false sync=false sc_03.src_3 \
    ! queue \
    ! video/x-raw, width=288, height=160 \
    ! videorate \
    ! video/x-raw, framerate=30/1 \
    ! vvas_xvcuenc name=enc_160p30_dev3_0 dev-idx=3 target-bitrate=625 xclbin-location=<xclbin file path> \
    ! h264parse \
    ! fpsdisplaysink name=sink_xcode_scale_160p30_dev3_0 video-sink=fakesink text-overlay=false sync=false -v

In the above example, one 1080p@60fps stream is decoded to NV12 raw data (by default) and is given as input to vvas_xabrscaler plugin. This plug-in resizes the input frame into multiple resolutions. In this case, it is 720p, 480p, 360p, 160p resolutions. It should be noted that, 720p@60 stream is passed to encoder via
``videorate`` plug-in which changes framerate from 60 to 30 and the same 720p@60 is also passed to encoder to generate 720p@60 encoded stream. This is achieved with the help of ``tee`` plug-in.
Similarly, with the help of 'videorate' plug-in, all the remaining scaler output streams are encoded at 30fps.

Transcoding with lookahead
---------------------------

AMD has developed an IP, Lookahead, for visual quality improvements. Example of pipeline with lookahead is mentioned below:

.. code-block:: shell

    gst-launch-1.0 -v filesrc location=<input h264 file> \
    ! queue \
    ! h264parse \
    ! vvas_xvcudec xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=0 \
    ! vvas_xabrscaler xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin dev-idx=0 ppc=4 scale-mode=2 name=sc_00 sc_00.src_0 \
    ! queue \
    ! video/x-raw, width=1280, height=720, format=NV12 \
    ! tee name=tee_00 tee_00. \
    ! queue \
    ! videorate \
    ! video/x-raw, framerate=60/1 \
    ! queue \
    ! vvas_xlookahead xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin codec-type=0 spatial-aq=1 temporal-aq=1 lookahead-depth=8 dev-idx=0 \
    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=enc_720p60_dev0_0 dev-idx=0 target-bitrate=4000 \
    ! h264parse \
    ! fpsdisplaysink name=sink_xcode_scale_720p60_dev0_0 video-sink=fakesink text-overlay=false sync=false tee_00. \
    ! queue \
    ! videorate \
    ! video/x-raw, framerate=30/1 \
    ! queue \
    ! vvas_xlookahead xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin codec-type=0 spatial-aq=1 temporal-aq=1 lookahead-depth=8 dev-idx=0 \
    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=enc_720p30_dev0_0 dev-idx=0 target-bitrate=3000 \
    ! h264parse \
    ! fpsdisplaysink name=sink_xcode_scale_720p30_dev0_0 video-sink=fakesink text-overlay=false sync=false sc_00.src_1 \
    ! queue \
    ! video/x-raw, width=848, height=480, format=NV12 \
    ! videorate \
    ! video/x-raw, framerate=30/1 \
    ! queue \
    ! vvas_xlookahead xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin codec-type=0 spatial-aq=1 temporal-aq=1 lookahead-depth=8 dev-idx=0 \
    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=enc_480p30_dev0_0 dev-idx=0 target-bitrate=2500 \
    ! h264parse \
    ! fpsdisplaysink name=sink_xcode_scale_480p30_dev0_0 video-sink=fakesink text-overlay=false sync=false sc_00.src_2 \
    ! queue \
    ! video/x-raw, width=640, height=360, format=NV12 \
    ! videorate \
    ! video/x-raw, framerate=30/1 \
    ! queue \
    ! vvas_xlookahead xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin codec-type=0 spatial-aq=1 temporal-aq=1 lookahead-depth=8 dev-idx=0 \
    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=enc_360p30_dev0_0 dev-idx=0 target-bitrate=1250 \
    ! h264parse \
    ! fpsdisplaysink name=sink_xcode_scale_360p30_dev0_0 video-sink=fakesink text-overlay=false sync=false sc_00.src_3 \
    ! queue \
    ! video/x-raw, width=288, height=160, format=NV12 \
    ! videorate \
    ! video/x-raw, framerate=30/1 \
    ! queue \
    ! vvas_xlookahead xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin codec-type=0 spatial-aq=1 temporal-aq=1 lookahead-depth=8 dev-idx=0 \
    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=enc_160p30_dev0_0 dev-idx=0 target-bitrate=625 \
    ! h264parse \
    ! fpsdisplaysink name=sink_xcode_scale_160p30_dev0_0 video-sink=fakesink text-overlay=false sync=false sc_00.src_4 \
    ! queue ! video/x-raw, width=1920, height=1080, format=NV12 \
    ! videorate \
    ! video/x-raw, framerate=60/1 \
    ! queue \
    ! vvas_xlookahead xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin codec-type=0 spatial-aq=1 temporal-aq=1 lookahead-depth=8 dev-idx=0 \
    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin name=enc_1080p60_dev0_0 dev-idx=0 target-bitrate=5000 \
    ! h264parse \
    ! fpsdisplaysink name=sink_xcode_maxresolution_dev0_0 video-sink=fakesink text-overlay=false sync=false

or more information on lookahead plugin options, refer to :doc:`DC Plugins <../DC_plugins>`
