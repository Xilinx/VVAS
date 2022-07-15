## Copyright and license statement
Copyright 2020-2022 Xilinx Inc.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0).

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

# VVAS compositor plugin
vvas_xcompositor is plugin that combines two or more video streams into a single frame using hardware mutiscaler kernel.

### Input & output
It can accept NV12 video streams. For each of the requested sink pads it will compare the incoming geometry and framerate to define the output parameters. Indeed output video frames will have the geometry of the biggest incoming video stream and the framerate of the fastest incoming one.

vvas_xompositor will do colorspace conversion.

### Individual parameters for each input stream can be configured on the GstCompositorPad:

- "xpos": The x-coordinate position of the top-left corner of the picture (#gint)
- "ypos": The y-coordinate position of the top-left corner of the picture (#gint)
- "width": The width of the picture; the input will be scaled if necessary (#gint)
- "height": The height of the picture; the input will be scaled if necessary (#gint)
- "zorder": The z-order position of the picture in the composition (#guint)

![This is an image](optflow_plugin.jpg)




### Plugin properties

| Property Name | Description | Type | Range | Default |
| --- | --- | --- | --- | --- |
| avoid-output-copy | Avoid output frames copy on all source pads even when downstream does not support GstVideoMeta metadata | Boolean | true or false | false |
| best-fit | downscale/upscale the input video to best-fit in each window | Boolean | true or false | false |
| dev-idx | Device index | Integer | -1 to 31 | -1 |
| kernel-name | String defining the kernel name and instance as mentioned in xclbin | String | NA | v_multi_scaler:v_multi_scaler_1 |
| xclbin-location | Location of the xclbin to program devices | String | NA | NULL |




