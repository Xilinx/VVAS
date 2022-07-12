########################################
Multi-instance High density Transcoding 
########################################

Each ``Zynq UltraScale+ MPSoc`` device has a decoder and an encoder with max capacity of encoding/decoding @4Kp60. So there can be more number of ecoder/encoder instances running simultaneously in case the the total required capacity is less than 4Kp60. For example, we can launch 4x transcode pipelines of capacity 1080p60 resolution.  

There are two ``Zynq UltraScale+ MPSoC`` devices inside ``Alveo U30`` card and each is capable of transcoding @4Kp60. If there are more such cards connected to the server, then this capacity is increased and we can launch more transcode pipelines in parallel.

Pre-requisite
---------------

Before start executing the Transcoding usecases, make sure the setup is ready:

 * Required Alveo U30 cards are installed in the PCIe slots of the server machine.
 * VVAS 2.0 Package has been installed and the Alveo U30 cards are flashed with the required image. If not, follow the section :doc:`Alveo U30 setup for VVAS 2.0 <../u30_platform/u30_setup>`

If your setup is re-booted, or you have opened a fresh terminal, perform the following steps.

 .. code-block:: shell

    /opt/xilinx/xcdr/setup.sh

Resource Management
-------------------

When there are several devices/cards, connected to the server, then it is important to select correct device to launch each new instance of the transcode pipeline. Selection of device depends on how much capacity is left on that device and how much load new instance needs. This has been simplified by ``XRM`` (Xilinc Resource Manager). User need not know details about how xrm works. XRM keeps on checking the available capacity on each device and launches the transcode pipeline on a device only when it has capacity. If current device doesn't have the required capacity, then it goes to next device and launch the new pipeline instance if it has the capacity. User need not know details about how xrm works. There are two applications provided that will interact with XRM and manage the resource allocation.

Following XRM based applications are provided with varied ease-of-use to support the same:

1.  jobSlotReservation
2.  launcher

jobSlotReservation
^^^^^^^^^^^^^^^^^^

Each pipeline, be it simple decode, or simple transcode or and ABR ladder, is a JOB for jobSlotReservation application. Each compute unit and its load capacity in such pipeline is described in a json file and this json file is known as job descriptor. Sample job description files are located at : /opt/xilinx/launcher/scripts/describe_job

Based on the input resolution and type of use-case, the load requirement of compute units(CUs) within a JOB varies which inturn determines how many instances of each JOB can be launched on the available devices at a time.


Steps to use:

1.  Once the device and host boots up, setup the environment:

.. code-block:: shell

    source /opt/xilinx/xcdr/setup.sh

2.  run the command :

.. code-block:: shell

    jobSlotReservation  <job description file name>
    Ex: jobSlotReservation /opt/xilinx/launcher/scripts/describe_job/describe_job_h264_transcodeOnly_1080p60.json

.. code-block:: shell

    jobSlotReservation /opt/xilinx/launcher/scripts/describe_job/describe_job_h264_transcodeOnly_1080p60.json
    [0]: decoder plugin function =0 success to run the function, output_load:250000
    [0]: scaler plugin function =0 success to run the function, output_load:0
    [0]: encoder plugin function =0 success to run the function, output_load:250000 enc_num=1 la_load=250000


    For /opt/xilinx/launcher/scripts/describe_job/describe_job_h264_transcodeOnly_1080p60.json, Possible number of job slots available = 14

    The Job_slot_reservations are alive as long as this Application is alive!
    (press Enter to end)


.. Note::

   This application, jobSlotReservation, must be alive a long as your use-case is executing. So do not press 'Enter' as long as use cases is executing.

As a result of above command, file "/var/tmp/xilinx/xrm_jobReservation.sh" will be created. This file will have a unique reservation id for each JOB. Open this file and note down the "XRM_RESERVE_ID_#N" entries.

If the last such entry is XRM_RESERVE_ID_14, it means user launch 14 instances of this JOB in parallel. 

.. Note::
   Each JOB must be launched in a separate shell. So as per example above, open 14 different shells and launch 14 instances as mentioned below.

For example, to start 1st instance of use-case, open a new shell and execute the commands mentioned below:

.. code-block:: shell

    source /opt/xilinx/xcdr/setup.sh
    source /var/tmp/xilinx/xrm_jobReservation.sh
    export XRM_RESERVE_ID=${XRM_RESERVE_ID_1}

Then, launch the use case (GStreamer command) which matches the job description. Example GStreamer command is mentioned below:

.. code-block:: shell

    gst-launch-1.0 filesrc location=~/Videos/bbb_sunflower_1080p_60fps_normal.mp4 ! qtdemux \
    ! h264parse \
    ! vvas_xvcudec num-entropy-buf=2 xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin \
    ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin target-bitrate=600 \
    ! h264parse \
    ! fpsdisplaysink name=fpssink video-sink="fakesink" text-overlay=false sync=false -v

User can repeat the same for 13 more such instances in each separate shell without worrying about 'dev-idx' parameter
on which command needs to be run.

Launcher
^^^^^^^^^

'Launcher' is another application which makes it much more easier to run maximum multiple instances of a use-case without the knowledge of
number of devices available and how many are free to use. This is achieved with the help of XRM based 'launcher' application.

Steps to use:

1.  After the device and host boots up, setup the environment:

.. code-block:: shell

    source /opt/xilinx/xcdr/setup.sh

2.  Create a text file with the absolute path of the input stream needed for a use-case, including file name. There shall be one entry for each instance. In case you want to provide same file as input for all instances, then add same path and file name in each entry in the file. Otherwise provide the required filename with complete path for each instance.

If, say, there are 'N' entries in the text file, and depending on the devices available and the load requirement of one instance/JOB, 'Y' instances can be launched simultaneously, then
If 'N' <= 'Y' all 'N' instances will be launched in one go.
If 'N' > 'Y', then first 'Y' instances will be launched quickly in one go. After that as soon as any of the instances is finished running, a new instance will be launched. This will continue as long as all 'N' instances are launched.

Example of such text file, source.txt is:

.. code-block:: shell

    /tmp/Videos/bbb_sunflower_1080p_60fps_normal.mp4
    /tmp/Videos/bbb_sunflower_1080p_60fps_normal.mp4
    /tmp/Videos/bbb_sunflower_1080p_60fps_normal.mp4
    /tmp/Videos/bbb_sunflower_1080p_60fps_normal.mp4

3.  Create a text file with the command to be run. Example of such file, say run.txt is as below. Make sure to use matching
job description json file and the command. Examples of such run param files are located at: /opt/xilinx/launcher/scripts/vvas_run_params/

.. code-block:: shell

    job_description = /opt/xilinx/launcher/scripts/describe_job/describe_job_h264_transcodeOnly_1080p60.json

    cmdline = gst-launch-1.0 filesrc \
            ! qtdemux \
            ! h264parse \
            ! vvas_xvcudec num-entropy-buf=2 xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin \
            ! queue max-size-buffers=1 \
            ! vvas_xvcuenc xclbin-location=/opt/xilinx/xcdr/xclbins/transcode.xclbin \
            ! video/x-h264 \
            ! fpsdisplaysink name=sink_xcode_maxresolution fps-update-interval=5000 video-sink=fakesink text-overlay=false sync=false -v

4.  Launch multiple instances

.. code-block:: shell

    launcher source.txt run.txt
