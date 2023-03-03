..
   Copyright 2021-2022 Xilinx, Inc.

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

+-------------+---------------+---------------------------+---------------------+-----------------------+
|             |               |                           |                     |                       |
| **JSONKey** | **ValueType** | **Mandatory or Optional** |   **Default Value** |   **Description**     |
|             |               |                           |                     |                       |
|             |               |                           |                     |                       |
|             |               |                           |                     |                       |
+=============+===============+===========================+=====================+=======================+
| xclbin-     |    String     |    Optional               |    NULL             | Location of           |
| location    |               |                           |                     | the xclbin            |
|             |               |                           |                     | to program            |
|             |               |                           |                     | the FPGA.             |
|             |               |                           |                     | In PCIe               |
|             |               |                           |                     | platforms,            |
|             |               |                           |                     | the XRM               |
|             |               |                           |                     | chooses the           |
|             |               |                           |                     | device to             |
|             |               |                           |                     | download              |
|             |               |                           |                     | the xclbin            |
|             |               |                           |                     | based on              |
|             |               |                           |                     | processing            |
|             |               |                           |                     | load.                 |
+-------------+---------------+---------------------------+---------------------+-----------------------+
| vvas-       |    String     |    Optional               |    /usr/lib         | The VVAS              |
| library-    |               |                           |                     | library               |
| repo        |               |                           |                     | repository            |
|             |               |                           |                     | path that             |
|             |               |                           |                     | looks for             |
|             |               |                           |                     | acceleration          |
|             |               |                           |                     | software              |
|             |               |                           |                     | libraries             |
|             |               |                           |                     | using the             |
|             |               |                           |                     | VVAS                  |
|             |               |                           |                     | GStreamer             |
|             |               |                           |                     | plug-in.              |
+-------------+---------------+---------------------------+---------------------+-----------------------+
| element-    |    String     |                           |                     | The                   |
| mode        |               |   Mandatory               |                     | GStreamer             |
|             |               |                           |                     | uses the              |
|             |               |                           |                     | element               |
|             |               |                           |                     | mode to               |
|             |               |                           |                     | operate.              |
|             |               |                           |                     | Based on              |
|             |               |                           |                     | requirements          |
|             |               |                           |                     | you must              |
|             |               |                           |                     | choose one            |
|             |               |                           |                     | of the                |
|             |               |                           |                     | following             |
|             |               |                           |                     | modes:                |
|             |               |                           |                     |                       |
|             |               |                           |                     | -  Passthrough:       |
|             |               |                           |                     |     In this           |
|             |               |                           |                     |     mode, the         |
|             |               |                           |                     |     element           |
|             |               |                           |                     |     does not          |
|             |               |                           |                     |     alter the         |
|             |               |                           |                     |     input             |
|             |               |                           |                     |     buffer.           |
|             |               |                           |                     |                       |
|             |               |                           |                     | -  Inplace:           |
|             |               |                           |                     |     In this           |
|             |               |                           |                     |     mode, the         |
|             |               |                           |                     |     element           |
|             |               |                           |                     |     alters            |
|             |               |                           |                     |     the input         |
|             |               |                           |                     |     buffer            |
|             |               |                           |                     |     instead           |
|             |               |                           |                     |     of producing      |
|             |               |                           |                     |     new output        |
|             |               |                           |                     |     buffers.          |
|             |               |                           |                     |                       |
|             |               |                           |                     | -  Transform:         |
|             |               |                           |                     |     In this           |
|             |               |                           |                     |     mode, the         |
|             |               |                           |                     |     element           |
|             |               |                           |                     |     produces          |
|             |               |                           |                     |     an ouput          |
|             |               |                           |                     |     buffer fo         |
|             |               |                           |                     |     each input        |
|             |               |                           |                     |     buffer.           |
+-------------+---------------+---------------------------+---------------------+-----------------------+
|    kernel   |  Kernel       |                           |    None             | A Kernel JSON         |
|             |  JSON         |   Mandatory               |                     | object                |
|             |  Object       |                           |                     | provides              |
|             |               |                           |                     | information           |
|             |               |                           |                     | about an              |
|             |               |                           |                     | VVAS video            |
|             |               |                           |                     | library               |
|             |               |                           |                     | configuration.        |
|             |               |                           |                     |                       |
|             |               |                           |                     | For                   |
|             |               |                           |                     | information           |
|             |               |                           |                     | on object             |
|             |               |                           |                     | members,              |
|             |               |                           |                     | see :ref:`Kernel      |
|             |               |                           |                     | JSON Object           |
|             |               |                           |                     | Members               |
|             |               |                           |                     | <kernel-json-object>` |
+-------------+---------------+---------------------------+---------------------+-----------------------+

.. _kernel-json-object:

Table 16: Kernel JSON Object Members

+-------------+---------------+---------------------------+-------------------+-----------------+
|             |               |                           |                   |                 |
| **JSON Key**| **Value Type**| **Mandatory or Optional** | **Default Value** | **Description** |
|             |               |                           |                   |                 |
|             |               |                           |                   |                 |
|             |               |                           |                   |                 |
+=============+===============+===========================+===================+=================+
| library-    |    String     |                           |    None           | The name of     |
| name        |               |   Mandatory               |                   | the VVAS        |
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
| kernel-     |   String      |    Optional               |  shared           | Mode in which   |
| access-     |               |                           |                   | IP/Kernel is    |
| mode        |               |                           |                   | to be accessed. |
|             |               |                           |                   | "shared" or     |
|             |               |                           |                   | "exclusive"     |
+-------------+---------------+---------------------------+-------------------+-----------------+
|    config   |    Object     |    Optional               |    None           | Holds the       |
|             |               |                           |                   | configuration   |
|             |               |                           |                   | specific        |
|             |               |                           |                   | to the VVAS     |
|             |               |                           |                   | video           |
|             |               |                           |                   | library.        |
|             |               |                           |                   |                 |
|             |               |                           |                   | The VVAS        |
|             |               |                           |                   | GStreamer       |
|             |               |                           |                   | plug-ins        |
|             |               |                           |                   | do not          |
|             |               |                           |                   | parse           |
|             |               |                           |                   | this            |
|             |               |                           |                   | JSON            |
|             |               |                           |                   | object,         |
|             |               |                           |                   | instead         |
|             |               |                           |                   | it is           |
|             |               |                           |                   | sent to         |
|             |               |                           |                   | the             |
|             |               |                           |                   | video           |
|             |               |                           |                   | library.        |
+-------------+---------------+---------------------------+-------------------+-----------------+
| soft-kernel |    Object     |                           |    None           | Contains        |
|             |               |   Mandatory               |                   | soft-           |
|             |               |   ( if the kernel         |                   | kernel          |
|             |               |   library is              |                   | specific        |
|             |               |   written for             |                   | information.    |
|             |               |   the soft kernel. )      |                   |                 |
|             |               |                           |                   | This            |
|             |               |                           |                   | JSON            |
|             |               |                           |                   | object          |
|             |               |                           |                   | is only         |
|             |               |                           |                   | valid           |
|             |               |                           |                   | for PCIe        |
|             |               |                           |                   | based           |
|             |               |                           |                   | platforms.      |
+-------------+---------------+---------------------------+-------------------+-----------------+
