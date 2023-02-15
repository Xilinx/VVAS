..
   Copyright 2021 Xilinx, Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

****************
JSON File Schema
****************
Configuration file used in VVAS Infrastructure plugins has to be defined in json format. This section defines the schema for the configuration files.

Table 15: Root JSON Object Members

+-------------+---------------+---------------------------+---------------------+--------------------+
|             |               |                           |                     |                    |
| **JSONKey** | **ValueType** | **Mandatory or Optional** |   **Default Value** |   **Description**  |
|             |               |                           |                     |                    |
|             |               |                           |                     |                    |
|             |               |                           |                     |                    |
+=============+===============+===========================+=====================+====================+
|    xclb     |    String     |    Optional               |    NULL             | Location of        |
| in-location |               |                           |                     | the xclbin         |
|             |               |                           |                     | to program         |
|             |               |                           |                     | the FPGA.          |
|             |               |                           |                     | In PCIe            |
|             |               |                           |                     | platforms,         |
|             |               |                           |                     | the XRM            |
|             |               |                           |                     | chooses the        |
|             |               |                           |                     | device to          |
|             |               |                           |                     | download           |
|             |               |                           |                     | the xclbin         |
|             |               |                           |                     | based on           |
|             |               |                           |                     | processing         |
|             |               |                           |                     | load.              |
+-------------+---------------+---------------------------+---------------------+--------------------+
|    vvas-l   |    String     |    Optional               |    /usr/lib         | The VVAS           |
| ibrary-repo |               |                           |                     | library            |
|             |               |                           |                     | repository         |
|             |               |                           |                     | path that          |
|             |               |                           |                     | looks for          |
|             |               |                           |                     | a                  |
|             |               |                           |                     | cceleration        |
|             |               |                           |                     | software           |
|             |               |                           |                     | libraries          |
|             |               |                           |                     | using the          |
|             |               |                           |                     | VVAS               |
|             |               |                           |                     | GStreamer          |
|             |               |                           |                     | plug-in.           |
+-------------+---------------+---------------------------+---------------------+--------------------+
|    e        |    Enum       |                           |                     | The                |
| lement-mode |               |   Mandatory               |                     | GStreamer          |
|             |               |                           |                     | uses the           |
|             |               |                           |                     | element            |
|             |               |                           |                     | mode to            |
|             |               |                           |                     | operate.           |
|             |               |                           |                     | Based on           |
|             |               |                           |                     | re                 |
|             |               |                           |                     | quirements,        |
|             |               |                           |                     | you must           |
|             |               |                           |                     | choose one         |
|             |               |                           |                     | of the             |
|             |               |                           |                     | following          |
|             |               |                           |                     | modes:             |
|             |               |                           |                     |                    |
|             |               |                           |                     | -  P               |
|             |               |                           |                     | assthrough:        |
|             |               |                           |                     |       In           |
|             |               |                           |                     |       this         |
|             |               |                           |                     |       mode,        |
|             |               |                           |                     |       the          |
|             |               |                           |                     |                    |
|             |               |                           |                     |     element        |
|             |               |                           |                     |       does         |
|             |               |                           |                     |       not          |
|             |               |                           |                     |       alter        |
|             |               |                           |                     |       the          |
|             |               |                           |                     |       input        |
|             |               |                           |                     |                    |
|             |               |                           |                     |     buffer.        |
|             |               |                           |                     |                    |
|             |               |                           |                     | -  Inplace:        |
|             |               |                           |                     |       In           |
|             |               |                           |                     |       this         |
|             |               |                           |                     |       mode,        |
|             |               |                           |                     |       the          |
|             |               |                           |                     |                    |
|             |               |                           |                     |     element        |
|             |               |                           |                     |                    |
|             |               |                           |                     |      alters        |
|             |               |                           |                     |       the          |
|             |               |                           |                     |       input        |
|             |               |                           |                     |                    |
|             |               |                           |                     |      buffer        |
|             |               |                           |                     |                    |
|             |               |                           |                     |     instead        |
|             |               |                           |                     |       of           |
|             |               |                           |                     |                    |
|             |               |                           |                     |   producing        |
|             |               |                           |                     |       new          |
|             |               |                           |                     |                    |
|             |               |                           |                     |      output        |
|             |               |                           |                     |                    |
|             |               |                           |                     |    buffers.        |
|             |               |                           |                     |                    |
|             |               |                           |                     | -                  |
|             |               |                           |                     |  Transform:        |
|             |               |                           |                     |       In           |
|             |               |                           |                     |       this         |
|             |               |                           |                     |       mode,        |
|             |               |                           |                     |       the          |
|             |               |                           |                     |                    |
|             |               |                           |                     |     element        |
|             |               |                           |                     |                    |
|             |               |                           |                     |    produces        |
|             |               |                           |                     |       an           |
|             |               |                           |                     |                    |
|             |               |                           |                     |      output        |
|             |               |                           |                     |                    |
|             |               |                           |                     |      buffer        |
|             |               |                           |                     |       for          |
|             |               |                           |                     |       each         |
|             |               |                           |                     |       input        |
|             |               |                           |                     |                    |
|             |               |                           |                     |     buffer.        |
+-------------+---------------+---------------------------+---------------------+--------------------+
|    kernels  |    Array of   |                           |    None             | Array of           |
|             |    Objects    |   Mandatory               |                     | kernel             |
|             |               |                           |                     | objects.           |
|             |               |                           |                     | Each kernel        |
|             |               |                           |                     | object             |
|             |               |                           |                     | provides           |
|             |               |                           |                     | information        |
|             |               |                           |                     | about an           |
|             |               |                           |                     | VVAS video         |
|             |               |                           |                     | library            |
|             |               |                           |                     | con                |
|             |               |                           |                     | figuration.        |
|             |               |                           |                     |                    |
|             |               |                           |                     | Minimum            |
|             |               |                           |                     | value: 1           |
|             |               |                           |                     |                    |
|             |               |                           |                     | Maximum            |
|             |               |                           |                     | value: 2           |
|             |               |                           |                     |                    |
|             |               |                           |                     | For                |
|             |               |                           |                     | information        |
|             |               |                           |                     | on object          |
|             |               |                           |                     | members,           |
|             |               |                           |                     | see `Table         |
|             |               |                           |                     | 16: Kernel         |
|             |               |                           |                     | JSON Object        |
|             |               |                           |                     | Memb               |
|             |               |                           |                     | ers <#_book        |
|             |               |                           |                     | mark25>`__.        |
+-------------+-------------+-----------------------------+---------------------+--------------------+

Table 16: Kernel JSON Object Members

+-------------+---------------+---------------------------+-------------------+-----------------+
|             |               |                           |                   |                 |
| **JSON Key**| **Value Type**| **Mandatory or Optional** | **Default Value** | **Description** |
|             |               |                           |                   |                 |
|             |               |                           |                   |                 |
|             |               |                           |                   |                 |
+=============+===============+===========================+===================+=================+
| l           |    String     |                           |    None           | The name of     |
| ibrary-name |               |   Mandatory               |                   | the VVAS        |
|             |               |                           |                   | video           |
|             |               |                           |                   | library         |
|             |               |                           |                   | loaded by       |
|             |               |                           |                   | the VVAS        |
|             |               |                           |                   | GStreamer       |
|             |               |                           |                   | plug-ins.       |
|             |               |                           |                   | The             |
|             |               |                           |                   | absolute        |
|             |               |                           |                   | path of the     |
|             |               |                           |                   | video           |
|             |               |                           |                   | library is      |
|             |               |                           |                   | formed by       |
|             |               |                           |                   | pre-pending     |
|             |               |                           |                   | the vvas-       |
|             |               |                           |                   | l               |
|             |               |                           |                   | ibrary-repo     |
|             |               |                           |                   | path.           |
+-------------+---------------+---------------------------+-------------------+-----------------+
| kernel-name |    String     |    Optional               |    None           | The name of     |
|             |               |                           |                   | the IP or       |
|             |               |                           |                   | kernel in       |
|             |               |                           |                   | the form of     |
|             |               |                           |                   |                 |
|             |               |                           |                   | <kernel         |
|             |               |                           |                   | name            |
|             |               |                           |                   | >:<instance     |
|             |               |                           |                   | name>           |
+-------------+---------------+---------------------------+-------------------+-----------------+
|    config   |    Object     |    Optional               |    None           |    Holds        |
|             |               |                           |                   |    the          |
|             |               |                           |                   |    co           |
|             |               |                           |                   | nfiguration     |
|             |               |                           |                   |    specific     |
|             |               |                           |                   |    to the       |
|             |               |                           |                   |    VVAS         |
|             |               |                           |                   |    video        |
|             |               |                           |                   |    library.     |
|             |               |                           |                   |    The VVAS     |
|             |               |                           |                   |                 |
|             |               |                           |                   |   GStreamer     |
|             |               |                           |                   |    plug-ins     |
|             |               |                           |                   |    do not       |
|             |               |                           |                   |    parse        |
|             |               |                           |                   |    this         |
|             |               |                           |                   |    JSON         |
|             |               |                           |                   |    object,      |
|             |               |                           |                   |    instead      |
|             |               |                           |                   |    it is        |
|             |               |                           |                   |    sent to      |
|             |               |                           |                   |    the          |
|             |               |                           |                   |    video        |
|             |               |                           |                   |    library.     |
+-------------+---------------+---------------------------+-------------------+-----------------+
|             |    Object     |                           |    None           |    Contains     |
| soft-kernel |               |   Mandatory               |                   |                 |
|             |               |    if the                 |                   | soft-kernel     |
|             |               |    kernel                 |                   |    specific     |
|             |               |    library                |                   |    i            |
|             |               |    is                     |                   | nformation.     |
|             |               |    written                |                   |    This         |
|             |               |    for the                |                   |    JSON         |
|             |               |    soft                   |                   |    object       |
|             |               |    kernel.                |                   |    is only      |
|             |               |                           |                   |    valid        |
|             |               |                           |                   |    for PCIe     |
|             |               |                           |                   |    based        |
|             |               |                           |                   |                 |
|             |               |                           |                   |  platforms.     |
+-------------+---------------+---------------------------+-------------------+-----------------+
