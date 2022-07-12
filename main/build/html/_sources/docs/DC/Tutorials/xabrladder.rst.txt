
.. _gst_abrladder:

#############################################
GStreamer ABR Ladder Application
#############################################

.. highlight:: none

.. contents:: Table of Contents
    :local:
    :depth: 1
.. .. section-numbering::
 

*********************************************
Introduction
*********************************************
The :program:`vvas_xabrladder` application is a command line utility which implements the GStreamer video transcoding pipeline described in the diagram below. This application expects an input video file (mp4 with H.264/H.265 or H.264/H.265 elementary stream) and produces 5 different H.264/H.265 elementary streams. The resolution of each output stream is configured using a JSON file (by default: :file:`/opt/xilinx/vvas/share/vvas-examples/abrladder.json`). The output streams produced by this application are saved in the :file:`/tmp/ladder_outputs/` folder.

.. image:: ../../images/gst_xabrladder.png
    :width: 1024
    :alt: GStreamer pipeline of the vvas_xabrladder application
    :align: center


.. note::

  This example supports only input files of type mp4 with H.264/H.265 elementary stream in it. No other formats are supported


*********************************************
Host System Requirement
*********************************************

* GStreamer 1.18.5
* gst-plugins-good
* gst-plugins-base
* gst-plugins-bad
* gst-libav
* gstpbutils 

* Opensource GStreamer plugins to be verified after installation of above packages:
  
  - h264parse
  - h265parse
  - qtdemux
  - tee
  - filesink
  - filesrc

*********************************************
Usage
*********************************************

Below are the input parameters required to run the application.

================================ ===============  ==============
Parameter                         Short Form       Description
================================ ===============  ==============
.. option:: --devidx             .. option:: -i   | device index
                                                  | Type:    int 
                                                  | Range:   0 to 31 
                                                  | Default: NA  
                                                  | Option:  Mandatory 
.. option:: --json               .. option:: -j   | JSON file used to describe the configuration of the ABR ladder
                                                  | Type:    string
                                                  | Range:   NA
                                                  | Default: :file:`/opt/xilinx/vvas/share/vvas-examples/abrladder.json` 
                                                  | Option:  Optional
.. option:: --lookahead_enable   .. option:: -l   | Enables or disables lookahead functionality
                                                  | Type:    int
                                                  | Range:   0 (disable), 1 (enable)
                                                  | Default: 1 
                                                  | Option:  Optional
.. option:: --codectype          .. option:: -c   | Output codec type
                                                  | Type:    int
                                                  | Range:   0 (H264), 1 (H265)  
                                                  | Default: NA 
                                                  | Option:  Mandatory
.. option:: --file               .. option:: -f   | Input file path name (mp4/elementary-stream)
                                                  | Type:    string
                                                  | Range:   NA  
                                                  | Default: NA  
                                                  | Option:  Mandatory 
.. option:: --forcekeyframe      .. option:: -k   | Keyframe (IDR frame) insertion frequency in number of frames
                                                  | Type:    int
                                                  | Range:   NA  
                                                  | Default: 0  
                                                  | Option:  Optional 
================================ ===============  ==============

JSON Usage
----------
The json file provided with ``--json`` option allows user to specify the ladder configuration with init time and run time (dynamic) property setting for each ladder.
The default json file that showcase basic ladder configuration (without run time property) is shown below.

::

  {
    "log-level": 2,
    "ladder": {
        "sink":"filesink",
        "outputs" :[
         {
           "height" : 720,
           "width" : 1280,
           "framerate" : 60
         },
         {
           "height" : 720,
           "width" : 1280,
           "framerate" : 30
         },
         {
           "height" : 480,
           "width" : 848,
           "framerate" : 30
         },
         {
           "height" : 360,
           "width" : 640,
           "framerate" : 30
         },
         {
           "height" : 160,
           "width" : 288,
           "framerate" : 30
         }
        ]
      }
  }


.. _dynamic_params_json:

An example json file that showcase run time property (``dynamic_params``) change is shown below.

::

  {
    "log-level": 2,
    "ladder": {
        "sink":"filesink",
        "outputs" :[
         {
           "height" : 720,
           "width" : 1280,
           "framerate" : 60,
           "b-frames" : 4,
           "dynamic_params" :[
           {
             "frame" : 600,
             "b-frames" : 2,
             "bitrate" : 6000
           },
           {
              "frame" : 1500,
              "b-frames" : 0,
              "bitrate" : 3000
           }
          ]
         },
         {
           "height" : 720,
           "width" : 1280,
           "framerate" : 30,
           "b-frames" : 4,
           "spatial-aq" : false,
           "temporal-aq" : false,
           "spatial-aq-gain" : 50,
           "dynamic_params" :[
           {
             "frame" : 500,
             "spatial-aq" : true,
             "temporal-aq" : false,
             "spatial-aq-gain" : 50
           },
           {
             "frame" : 1500,
             "spatial-aq" : true,
             "temporal-aq" : true,
             "spatial-aq-gain" : 50
           }
          ]
         },
         {
           "height" : 480,
           "width" : 848,
           "framerate" : 30,
           "b-frames" : 4,
           "dynamic_params" :[
           {
             "frame" : 1000,
             "b-frames" : 2,
             "spatial-aq" : false,
             "temporal-aq" : true,
             "spatial-aq-gain" : 50
           },
           {
              "frame" : 1500,
              "b-frames" : 4,
              "bitrate" : 3000,
              "spatial-aq" : true,
              "temporal-aq" : true,
              "spatial-aq-gain" : 75
           }
          ]
         },
         {
           "height" : 360,
           "width" : 640,
           "framerate" : 30,
           "b-frames" : 4
         },
         {
           "height" : 160,
           "width" : 288,
           "framerate" : 30,
           "b-frames" : 4
         }
        ]
      }
  }


The JSON entries are explained in table below.

================== ==============
JSON Key            Description
================== ==============
ladder             | Indicates configuration of output ladder
sink               | Sink plugin to use for the GStreamer pipeline
outputs            | Array containing each ladder's init time and run time (dynamic) configuration.
                   | Supported init time configuration for each ladder are:
                   |   height
                   |   width
                   |   framerate
                   |   :option:`b-frames`
                   |   :option:`target-bitrate`
                   |   :option:`gop-length`
                   |   :option:`lookahead-depth`
                   |   :option:`rc-mode`
                   |   :option:`spatial-aq`
                   |   :option:`temporal-aq`
                   |   :option:`spatial-aq-gain`
                   |   :option:`max-bitrate`
                   |   h264-profile
                   |   h264-level
                   |   h265-profile
                   |   h265-level
                   |
                   | The default values of these parameters can be referred from the application source file.
dynamic_params     | Array entries inside ``outputs`` JSON key containing ladder parameters that can be changed at run time at specified ``frame`` number.
                   | Application expects entries in each ladder to be in ascending order of ``frame`` number.
                   | Supported run time parameters are :
                   |   :option:`b-frames`
                   |   :option:`bitrate<target-bitrate>`
                   |   :option:`spatial-aq`
                   |   :option:`temporal-aq`
                   |   :option:`spatial-aq-gain`
================== ==============


*********************************************
Examples
*********************************************
Below are example commands for the ABR ladder use case. The output files are stored in :file:`/tmp/ladder_outputs/` folder. Ensure that enough space is availabe in this folder.

1. Running one ABR ladder on one device with lookahead (enabled by default)::

	vvas_xabrladder  --devidx 0 --codectype 0 --file <path to file> 

The above command takes the input video file (mp4 with H.264/H.265 or H.264/H.265 elementary stream) and produces 5 different H.264/H.265 elementary streams based on the codec type provided  (0 for H.264 and 1 for H.265) with the following resolutions: 720p60, 720p30, 480p30, 360p30 and 160p30.

2. Running one ABR ladder on one device without lookahead::

	vvas_xabrladder  --devidx 0 --lookahead_enable 0 --codectype 0 --file <path to file>

The above command takes the input video file (mp4 with H.264/H.265 or H.264/H.265 elementary stream) and produces 5 different H.264/H.265 elementary streams based on the codec type provided  (0 for H.264 and 1 for H.265) with the following resolutions: 720p60, 720p30, 480p30, 360p30 and 160p30.

3. Running one ABR ladder on one device with lookahead enabled, and using the short-form options::

	vvas_xabrladder -i 0 -l 1 -c 0 -f <path to file>

4. Running two ABR ladders, mapping each ladder to a specific device using the devidx option::

	vvas_xabrladder --devidx 0 --lookahead_enable 0 --codectype 1 --file <path to file>
	vvas_xabrladder --devidx 1 --lookahead_enable 0 --codectype 1 --file <path to file>

5. Running four ABR ladders on one device, leveraging the devidx option to optimally leverage resources:: 

	vvas_xabrladder --devidx 0 --lookahead_enable 0 --codectype 1 --file <path to file>
	vvas_xabrladder --devidx 0 --lookahead_enable 0 --codectype 1 --file <path to file>
	vvas_xabrladder --devidx 0 --lookahead_enable 0 --codectype 1 --file <path to file>
	vvas_xabrladder --devidx 0 --lookahead_enable 0 --codectype 1 --file <path to file>

6. The four ABR ladders above can also be run using the :file:`examples/gstreamer/tutorials/14_gst_app_transcode_plus_scale.sh` script::

	<path to script>/14_gst_app_transcode_plus_scale.sh 0 <path to file>

7. Running one ABR ladder to insert Key (IDR) frames every 30 frames::

	vvas_xabrladder --devidx 0 --lookahead_enable 0 --codectype 1 --forcekeyframe 30 --file <path to input file>


.. _dynamic_params_test_example:

8. Running one ABR ladder to dynamically change encoder and lookahead parameters::

	vvas_xabrladder --json <path to json file with dynamic parameters configuration> --devidx 0 --codectype 1 --file <path to input file>


..
