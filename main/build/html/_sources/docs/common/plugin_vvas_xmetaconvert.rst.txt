vvas_xmetaconvert
=================

GStreamer vvas_xmetaconvert plug-in produces overlay metadata(GstVvasOverlayMeta) structure needed by :ref:`vvas_xoverlay` plug-in from VVAS ML inference metadata (i.e.GstInferenceMeta) which is attached input buffer. Static configuration parameters like font type, font size and etc need to be provided in JSON format via **config-location** property of this plugin.
For implementation details, refer to `vvas_xmetaconvert source code <https://github.com/Xilinx/VVAS/tree/master/vvas-gst-plugins/gst/metaconvert>`_

Input and Output
--------------------

This plug-in is format agnostic and can accept any input format as it operates only on metadata attached to input buffer and same will be pushed out of source pad after attaching overlay metadata.

Control Parameters and Plug-in Properties
------------------------------------------------

The following table lists the GStreamer plug-in properties supported by the vvas_xmetaconvert plug-in.

Table 3: vvas_xmetaconvert Plug-in Properties

+--------------------+-------------+---------------+------------------------+----------------------------------------------------------------+
|                    |             |               |                        |                                                                |
|  **Property Name** |   **Type**  | **Range**     | **Default**            | **Description**                                                |
|                    |             |               |                        |                                                                |
+====================+=============+===============+========================+================================================================+
| config-location    | String      | NA            | NULL                   | Location of the metaconvert configuration in JSON format which |
|                    |             |               |                        | will be used to convert inference metadata to overlay metadata |
+--------------------+-------------+---------------+------------------------+----------------------------------------------------------------+


JSON File format to be provided **config-location** property
--------------------------------------------------------------

This section describes the JSON file format and configuration parameters for the vvas_xmetaconvert plug-in. 

The following example is of a JSON file to pass to the vvas_xmetaconvert using **config-location** property.

.. code-block::

      {
        "config": {
          "display-level": 0,
          "font-size" : 0.5,
          "font" : 3,
          "thickness" : 2,
          "radius": 5,
          "mask-level" : 0,
          "y-offset" : 0,
          "label-filter" : [ "class", "probability" ],
          "classes" : [
            {
              "name" : "car",
              "blue" : 255,
              "green" : 0,
              "red"  : 0,
              "masking"  : 0
            },
            {
              "name" : "person",
              "blue" : 0,
              "green" : 255,
              "red"  : 0,
              "masking"  : 0
            },
            {
              "name" : "bus",
              "blue" : 0,
              "green" : 0,
              "red"  : 255,
              "masking"  : 0
            },
            {
              "name" : "bicycle",
              "blue" : 0,
              "green" : 0,
              "red"  : 255,
              "masking"  : 0
            }
          ]
        }
      }


Various configuration parameters passed to vvas_xmetaconvert via json file are described in the following table.

Table 9: vvas_xmetaconvert Parameters

+----------------------+----------------------+-----------------------------------------------------------------+
|    **Parameter**     | **Expected Values**  |    **Description**                                              |
|                      |                      |                                                                 |
+======================+======================+=================================================================+
| font                 |    0 to 7            |Below is the list of text font values and its description.       |
|                      |                      | - 0: Hershey Simplex (default)                                  |
|                      |                      | - 1: Hershey Plain                                              |
|                      |                      | - 2: Hershey Duplex                                             |
|                      |                      | - 3: Hershey Complex                                            |
|                      |                      | - 4: Hershey Triplex                                            |
|                      |                      | - 5: Hershey Complex Small                                      |
|                      |                      | - 6: Hershey Script Simplex                                     |
|                      |                      | - 7: Hershey Script Complex                                     |
+----------------------+----------------------+-----------------------------------------------------------------+
| font-size            |    0.5 to 1          |Font fraction scale factor that is multiplied by the             |
|                      |                      |font-specific base size. Default value is 0.5                    |
+----------------------+----------------------+-----------------------------------------------------------------+
| thickness            |    Integer 1 to 3    |The thickness of the line that makes up the rectangle. Negative  |
|                      |                      |values like -1, signify that the function draws a filled         |
|                      |                      |rectangle. The recommended value is between 1 and 3.             |
|                      |                      |Default line thickness value is 1.                               |
+----------------------+----------------------+-----------------------------------------------------------------+
| mask-level           |    Integer           |In case of cascaded ML pipeline, user can use this field to mask |
|                      |                      |out the results of a particular level in inference results tree. |
|                      |                      |All bounding boxes in thatlevel will be masked with black color. |
|                      |                      |                                                                 |
|                      |                      |When set to 0(default), none of the levels are masked.           |
+----------------------+----------------------+-----------------------------------------------------------------+
| label-color          |{"blue":0,            |The color of the text to be used to display.                     |
|                      |"green":0,            |                                                                 |
|                      |"red":0}              |                                                                 |
+----------------------+----------------------+-----------------------------------------------------------------+
| label-filter         |["class",             |This field indicates that all information printed is the label   |
|                      |"probability",        |string. Using "class" alone adds the ML classification name.     |
|                      |"tracker-id"]         |For example, car, person, etc.                                   |
|                      |                      |                                                                 |
|                      |                      |The addition of "probability" in the array adds the probability  |
|                      |                      |of a positive object identification.                             |
+----------------------+----------------------+-----------------------------------------------------------------+
| y-offset             |Integer 0 to height   |'y' offset to be  added along height for label in case of        |
|                      |                      |classification model                                             |
+----------------------+----------------------+-----------------------------------------------------------------+
| classes              |{"name":"car",        |This is a filtering option when using the vvas_xoverlay. The     |
|                      |"blue":255,           |bounding box is only drawn for the classes that are listed in    |
|                      |"green":0,            |this configuration and other classes are ignored. For instance,  |
|                      |"red" : 0,            |if "car", "person", "bicycle" is entered under "classes", then   |
|                      |"masking" : 0}        |the bounding box is only drawn for these three classes, and other|
|                      |                      |classes like horse, motorbike, etc. are ignored.                 |
|                      |                      |                                                                 |
|                      |                      |The expected value columns show an example of how each class     |
|                      |                      |should be described. All objects in this example, by class, are  |
|                      |                      |using the color combination listed.                              |
|                      |                      |                                                                 |
|                      |                      |The class names in this list matches the class names assigned    |
|                      |                      |by the vvas_xdpuinfer. Otherwise, the bounding box is not drawn. |
|                      |                      |                                                                 |
|                      |                      |"masking" flag can be used to mask all objects of a class Set    |
|                      |                      |it to 1 for enabling masking. This flag will override            |
|                      |                      |"mask_level" field.                                              |
|                      |                      |                                                                 |
|                      |                      |For instance, if "mask_level" is set 1 and "masking" for "car"   |
|                      |                      |is set 1 and if level 1 has cars and busses, then only cars are  |
|                      |                      |masked not the entire level.                                     |
|                      |                      |For face detect, keep the "classes" array empty.                 |
+----------------------+----------------------+-----------------------------------------------------------------+
| display-level        |  Integer 0 to N      |Display bounding box of one particular level or all levels     	|
|                      |  0 => all levels     |                                                                 |
|                      |  N => specific level |                                                                 |
+----------------------+----------------------+-----------------------------------------------------------------+

Example Pipelines
---------------------

The following example demonstrates use of vvas_xmetaconvert with :ref:`vvas_xoverlay` plug-in for drawing bounding boxes.
 
.. code-block::

    gst-launch-1.0 filesrc location="<PATH>/001.bgr" blocksize=150528 numbuffers=1
    ! videoparse width=224 height=224 framerate=30/1 format=16
    ! vvas_xinfer infer-config="<PATH>/kernel_resnet50.json"
    ! vvas_xmetaconvert config-location="<PATH>/metaconvert.json"
    ! vvas_xoverlay ! filesink location=output.bgr
