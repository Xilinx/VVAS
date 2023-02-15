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

##########################
VVAS Debugging Support
##########################

This section covers various debugging options available in VVAS.

GStreamer logs
===============

VVAS relies on debugging tools supported by GStreamer framework. For more details, you may refer to `GStreamer Debugging Tools <https://gstreamer.freedesktop.org/documentation/tutorials/basic/debugging-tools.html?gi-language=c>`_

vvas-core library logs
=======================

VVAS GStreamer plug-ins are using different vvas-core libraries for different functionalities. Logs specific to each of these vvas-core libraries can be enabled/disabled. How to rout these to syslog, to a file or to the console is covered in the section below.
vvas-core library logs will be routed to one of the options mentioned below.

To syslog
----------
This is the default destination for the logs. Logs will be routed to either ``/var/log/syslog`` or ``/var/logs/messages``.

.. note::

        Make sure systemd or other logging service is running

To a File
----------
If environment variable ``VVAS_CORE_LOG_FILE_PATH`` is set to file path then vvas-core logs will be routed to file provided.
if file path is invalid or unable to create file in the provided path due to permissions then logs will be routed to SYSLOG.

.. code-block::

        Example: export VVAS_CORE_LOG_FILE_PATH=$PWD/log.txt

To Consol
----------
If environment variable ``VVAS_CORE_LOG_FILE_PATH`` is set to “CONSOLE” then vvas-core logs will be routed to console.

.. code-block::

        Example: export VVAS_CORE_LOG_FILE_PATH=CONSOLE

Setting VVAS_CORE Log Level
============================
To set vvas_core log level for GStreamer based applications, GST_EXPORT should be set accordingly. Mapping between GStreamer log level and vvas-core library log level is as mentioned below.

.. list-table::
   :widths: 20 80
   :header-rows: 1

   * - GStreamer Log level
     - vvas-core log level

   * - GST_LEVEL_NONE, GST_LEVEL_ERROR
     - LOG_LEVEL_ERROR

   * - GST_LEVEL_WARNING, GST_LEVEL_FIXME
     - LOG_LEVEL_WARNING

   * - GST_LEVEL_INFO
     - LOG_LEVEL_INFO

   * - Default
     - LOG_LEVEL_DEBUG
