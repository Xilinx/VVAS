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

################################
Application Development Guide
################################

********
Overview
********


`XRM Library`
 The XRM library is used to manage the hardware accelerators available in the system. XRM keeps track of total system capacity for each of the compute units such as the decoder, scaler, and encoder. The XRM library makes it possible to perform actions such as reserving, allocating and releasing resources; calculating resource load and max capacity.
Details about XRM can be found in general `XRM documentation <https://xilinx.github.io/XRM/index.html>`_.


General application development on Data Center platforms is organised into below mentioned logicle steps.

1. Initialization
2. Resource Reservation
3. Session Creation
4. Runtime Processing
5. Cleanup

===============
Initialization
===============

Applications using the U30 plugins must first create a XRM context using the `xrmCreateContext()` function in order to establish a connection with the XRM daemon.
Further information about these APIs can be found in the online `XRT` and `XRM` documentation.

===================
Resource Allocation
===================
After the initialization step, the application must determine on which device to run and reserve the necessary hardware resources (CUs) on that device. This is done using the `XRM` APIs, as described in detail in the XRM API Reference Guide below.

======================
Session Creation
======================
Once the resources have been allocated, the application must create dedicated plugin sessions for each of the hardware accelerators that need to be used (decoder, scaler, encoder, lookahead).
To create a session, the application must first initialize all the required properties and parameters of the particular plugin. It must then call the corresponding session creation function. A complete reference for all the plugins is provided below.

===================
Runtime Processing
===================
The U30 plugins provide functions to send data from the host and receive data from the device. It is also possible to do zero-copy operations where frames are passed from one hardware accelerator to the next without being copied back to the host. The send and receive functions are specific to each plugin and the return code should be used to determine the next suitable action.

========
Cleanup
========
When the application finishes, it should destroy each plugin session using the corresponding destroy function. Doing so will free the U30 resources for other jobs and ensure that everything is released and cleaned-up properly.
The application should also use the ``xrmDestroyContext()`` function to destroy the XRM session, stop the connection to the daemon and ensure all resources are properly released.

****************************
Device ID Selection with XRM
****************************
In order to run the Gstreamer pipelines, the ID of the device on which the job will run must be known.
• If the user provides the device ID, the application can directly perform initialization based on this information.
• If the user does not specify a device ID, the application needs to determine a valid one. It can do so by using the XRM API to calculate the load of the specific job which needs to be run, reserve resources based on the load, and retrieve the ID of the device on which the resources have been reserved. More specifically:
1. Calculate the channel load based on the job properties (extracted from command line arguments and/or header of the video stream)
2. Using the ``xrmCheckCuPoolAvailableNum()`` function, query XRM for the number of resources available based on the channel load. XRM checks all the devices available on the server and returns how many such channels can be accommodated.
3. Using the ``xrmCuPoolReserve()`` function, reserve the resources required for this channel load. XRM returns a reservation index.
4. Using the ``xrmReservationQuery()`` function, obtain the ID of the device on which the resource has been allocated and the name of the xclbin.
5. Initialize Gstreamer using the device ID and the xclbin information.

============================
Resource Allocation with XRM
============================
In order to create an Gstreamer plugin session (encoder/decoder/scaler/lookahead), the necessary compute unit (CU) resources must first be successfully reserved and allocated.
• If the user provides device ID, the application should perform CU allocation on that particular device. If there are not enough resources available to support the specific channel load, the application should error out and exit.
• If the user does not specify a device ID, the application should perform CU allocation using the reservation index it received during XRM setup. 

============================
Reserving Multiple Job Slots
============================
Another example of XMR API usage can be found in the source code of the job slot reservation application. This example shows how to reserve as many job slots as possible given an input job description. This example works as follows:
1. Calculate the channel load based on a JSON job description
2. Using the ``xrmCheckCuPoolAvailableNum()`` function, query XRM for the number of resources available based on the channel load. XRM checks all the devices available on the server and returns how many such channels can be accommodated.
3. Call the ``xrmCuPoolReserve()`` function as many times as there are available resources to reserve all of them.
4. Wait for the user to press Enter to relinquish the reserved resources using ``xrmCuPoolRelinquish()``.
This example can be used as a starting point for developing custom orchestration layers.

