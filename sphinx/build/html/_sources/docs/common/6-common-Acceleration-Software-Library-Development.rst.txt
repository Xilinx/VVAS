##############################################################
Acceleration Software Library Development Guide
##############################################################

One of the objectives of VVAS is to define a methodology for advanced developers to develop their own kernel and integrate it into GStreamer-based applications. This section defines the steps required to develop an acceleration software library to control the kernel from the GStreamer application. The acceleration software library manages the kernel state machine as well as provide hooks to control the kernel from the GStreamer plug-ins or an application. There are three types of kernels.

.. figure:: ../images/image15.png
   :align: center

**********************************************************
Types of Kernels
**********************************************************

There are three types of kernels as mentioned below.


Hard Kernel
======================

A hard kernel is made up of HLS and RTL kernels.


Softkernel
======================

These are special kernels that executes on the application Processor of the device and are controlled over PCIe interface from the Server. These kernels are not for Embedded platforms.


Software Kernel
===============================

Software kernels are software processing modules that execute fully on the host CPU. Exposing software kernel through the acceleration software library interface allows them to integrate into the GStreamer framework without an in-depth knowledge of the GStreamer framework.

******************************************************
Interfaces for Acceleration Software Libraries
******************************************************

To be able to be controlled from an application or GStreamer plug-ins, each acceleration software library must expose three core API functions.

.. figure:: ../images/core-API-functions.png

To interact with the kernel, a set of utility APIs are provided.


ivas-utils
=================

This section covers details about the utility infrastructure required to develop the kernel libraries for a new kernel and to integrate these acceleration software libraries into the GStreamer framework.

The VVAS acceleration software library APIs are C-based APIs designed for ease of use. A developer does not need knowledge of the GStreamer plug-ins. The acceleration software libraries are developed using the following APIs and can be used by the GStreamer VVAS infrastructure plug-ins to realize plug and play functionality of various processing blocks and filters.

The utility sources are hosted in the ivas-utils repository/folder.

.. figure:: ../images/image24.png 
   :width: 400



**************************************
VVAS Data structures
**************************************

The following sections list the core structures and enumerations.


IVASKernel
======================

The IVASKernel is handle to kernel context and is created and passed to the core APIs by the GStreamer infrastructure plug-ins (for example: the ivas_xfilter).

.. code-block::

        typedef struct \_ivas_kernel IVASKernel;

        struct \_ivas_kernel {

        void \*xcl_handle; /\* XRT handle provided by GStreamer

        infrastructure plug-in \*/

        uint32_t cu_idx; /\* compute unit index of IP/soft- kernel \*/

        json_t \*kernel_config; /\* kernel specific config from app \*/ void
        \*kernel_priv; /\* to store kernel specific

        structures \*/

        xrt_buffer \*ert_cmd_buf; /\* ERT command buffer used to submit

        execution commands to XRT \*/

        size_t min_offset; size_t max_offset;

        IVASBufAllocCBFunc alloc_func; /\* callback function to allocate

        memory from GstBufferPool by GStreamer infrastructure plug-in \*/

        IVASBufFreeCBFunc free_func; /\* callback function to free memory

        allocated by alloc_func \*/ void \*cb_user_data; /\* handle to be
        passed along with

        alloc_func & free_func callback \*/

        ivaspads \*padinfo; #ifdef XLNX_PCIe_PLATFORM

        uint32_t is_softkernel; /\* true when acceleration s/w library is for

        #endif

        soft-kernel in PCIe platforms only

        \*/

        uint8_t is_multiprocess; /\* if true, ERT command buffer will

        be used to start kernel. else, direct register programming will be
        used \*/

        };


IVASVideoFormat
=================================

The IVASVideoFormat represents the video color formats supported by the VVAS framework. The GStreamer infrastructure plug-ins supports the mapping of the following formats and corresponding GStreamer color formats.

.. code-block::

        typedef enum {
        IVAS_VMFT_UNKNOWN = 0,
        IVAS_VFMT_RGBX8,
        IVAS_VFMT_YUVX8,
        IVAS_VFMT_YUYV8, // YUYV
        IVAS_VFMT_ABGR8,
        IVAS_VFMT_RGBX10,
        IVAS_VFMT_YUVX10,
        IVAS_VFMT_Y_UV8,
        IVAS_VFMT_Y_UV8_420, // NV12 
        IVAS_VFMT_RGB8,
        IVAS_VFMT_YUVA8,
        IVAS_VFMT_YUV8,
        IVAS_VFMT_Y_UV10,
        IVAS_VFMT_Y_UV10_420,
        IVAS_VFMT_Y8,
        IVAS_VFMT_Y10,
        IVAS_VFMT_ARGB8,
        IVAS_VFMT_BGRX8,
        IVAS_VFMT_UYVY8,
        IVAS_VFMT_BGR8, // BGR 
        IVAS_VFMT_RGBX12,
        IVAS_VFMT_RGB16
        }  IVASVideoFormat;


IVASFrame
=============================

The IVASFrame stores information related to a video frame. The GStreamer infrastructure plug- ins allocate the IVASFrame handle for input and output video frames and sends them to the VVAS kernel processing APIs. Also, the IVASFrame can be allocated by kernel libraries for internal memory requirements (i.e., memory for filter coefficients).


.. code-block::

        typedef struct _ivas_frame_props IVASFrameProps;
        typedef struct _ivas_frame IVASFrame;

        // frame properties hold information about video frame
        struct _ivas_frame_props {
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        IVASVideoFormat fmt;
        };
        struct _ivas_frame {
        uint32_t bo[VIDEO_MAX_PLANES]; // ignore: currently not used 
        void *vaddr[VIDEO_MAX_PLANES]; // virtual/user space address of 
                                       //video frame memory
        uint64_t paddr[VIDEO_MAX_PLANES]; // physical address of video frame
        uint32_t size[VIDEO_MAX_PLANES];
        void *meta_data;
        IVASFrameProps props; /* properties of video frame */
        /* application's private data */
        void *app_priv; /* assigned to GstBuffer by GStreamer infrastructure plugin */
        IVASMemoryType mem_t
        ype;
        /*number of planes in props.fmt */
        uint32_t n_planes; // number of planes based on color format
        };




Other VVAS Declarations
===========================================

.. code-block::

        #define MAX_NUM_OBJECT 512 /* max number of video frames/objects
        handled by VVAS */
        #define MAX_EXEC_WAIT_RETRY_CNT 10 /* retry count on xclExecWait failure */
        #define VIDEO_MAX_PLANES 4
        typedef enum {
        IVAS_UNKNOWN_MEMORY,
        IVAS_FRAME_MEMORY, /* use for input and output buffers */
        IVAS_INTERNAL_MEMORY, /* use for internal memory of IP */
        } IVASMemoryType;
        typedef struct buffer {
        unsigned int bo; /* XRT Buffer object */
        void* user_ptr; /* userspace/virtual pointer */
        uint64_t phy_addr; /* physical address */
        unsigned int size; /* size of XRT memory */
        } xrt_buffer;


VVAS acceleration software libraries APIs are broadly categorized into two API types, `Core API <#_bookmark17>`__ and `Utility API <#utility-api>`__.

********************************
Core API
********************************

Core APIs are exposed by the VVAS acceleration software library developer through a shared library. The GStreamer VVAS infrastructure plug-ins (ivas_xfilter and ivas_xmultisrc) call the APIs to perform operations on the kernel. The following is a list of core APIs.


Initialization API
=================================

The acceleration software library must perform one-time initialization tasks, such as private handles and internal memory allocations.

.. code-block::

        int32_t xlnx_kernel_init (IVASKernel * handle)
        Parameters:
            handle - VVAS kernel handle which has kernel context
        Return:
            0 on success or -1 on failure


Process API
========================================

The acceleration software library must perform per frame operations such as updating IP registers or calling the processing function of the user space library. If a acceleration software library is developed for IP (HardKernel), then this API must call the ivas_kernel_start() utility API to issue a command to process the registers using XRT.

.. code-block::

        int32_t xlnx_kernel_start (IVASKernel * handle, int start, IVASFrame *
        input[MAX_NUM_OBJECT], IVASFrame * output[MAX_NUM_OBJECT])

        Parameters:
            handle - VVAS kernel handle which has kernel context
            start – flag to indicate start of the kernel. Mainly useful in
        streaming kernel mode
            input[MAX_NUM_OBJECT] – Array of input frames populated in IVASFrame
        handle
            output[MAX_NUM_OBJECT] – Array of output frames populated in IVASFrame
        handle
        Return:
            0 on success or -1 on failure

        Note:
            1. MAX_NUM_OBJECT is 512 and same is assigned in ivas_kernel.h
            2. Input and output of array is NULL terminated to know number of input 
        & output frames received to start function

The acceleration software library can use the following API to wait for completion of the task that was started using the xlnx_kernel_start() API. In the case of a memory-memory IP acceleration software library, this API can leverage the ivas_kernel_done() API to check whether an issued command to XRT is completed.

.. code-block::

        int32_t ivas_kernel_done (IVASKernel * handle, int32_t timeout)

        Parameters:
            handle - VVAS kernel handle which has kernel context
            timeout - max. time to wait for "kernel done" notification from the
        kernel.
        Return:
            0 on success or -1 on failure


De-Initialization API
================================

The acceleration software library must perform de-initialization tasks such as freeing private handles and internal memory allocation as part of the library initialization process.

.. code-block::

        int32_t xlnx_kernel_deinit (IVASKernel * handle)

        Parameters:
            handle - VVAS kernel handle which has kernel context
        Return:
            0 on success or -1 on failure

******************************
Utility APIs
******************************

For ease of use, the utility APIs are abstracted from the XRT APIs. The following is the list of utility APIs.


Memory Management API
===========================

The following API must be used to allocate XRT memory for video frames as well as for internal memory requirements (i.e., memory for filter coefficients to be sent to IP). If video frames are requested using IVAS_FRAME_MEMORY, then the callback function invoked to the GStreamer VVAS infrastructure plug-ins, like the ivas_xfilter, will allocate frames from GstVideoBufferPool and GstIvasAllocator to avoid memory fragmentation or the memory will be allocated using direct XRT APIs.

.. code-block::

        IVASFrame* ivas_alloc_buffer (IVASKernel *handle, uint32_t size,
        IVASMemoryType mem_type, IVASFrameProps *props)

        Parameters:
            handle - VVAS kernel handle which has kernel context
            size - memory size to be allocated
            mem_type - memory can be IVAS_FRAME_MEMORY or IVAS_INTERNAL_MEMORY
            props – required when requesting IVAS_FRAME_MEMORY

        Return:
            IVASFrame handle on success or NULL on failure

The following API is to free the memory that is allocated using the ivas_alloc_buffer() API.

.. code-block::

        void ivas_free_buffer (IVASKernel * handle, IVASFrame *ivas_frame)

        Parameters:
            handle - VVAS kernel handle which has kernel context
            ivas_frame – IVASFrame handle allocated using ivas_alloc_buffer() API

        Return:
            None


********************************
Register Access APIs
********************************

The register access APIs are used to directly set or get registers of an IP or ERT command buffer that is sent to XRT. The following API is used write into the registers of an IP or to write into the ERT command buffer that is sent to XRT while starting the kernel execution.

.. code-block::

        void ivas_register_write (IVASKernel *handle, void *src, size_t size,
        size_t offset)
        Parameters:
            handle - VVAS kernel handle which has kernel context
            src – pointer to data to be written at offset in ERT command buffer or
        register at offset from base address of IP
            size – size of the data pointer src
            offset – offset at which data to be written

        Return:
            None

The following API used to read from the registers of an IP. This API is not required when is_multiprocess enabled in the IVASKernel handle.

.. code-block::

        void ivas_register_read (IVASKernel *handle, void *src, size_t size, size_t
        offset)

        Parameters:
            handle - VVAS kernel handle which has kernel context
            src – pointer to data which will be updated after read
            size – size of the data pointer src
            offset – offset from base address of an IP

        Return:
            None

********************************
Execution APIs
********************************

Execution APIs are used to start kernel execution and wait for the completion of the kernel. These APIs are only used when is_multiprocess is enabled in the IVASKernel handle. Use the following API o start IP/kernel execution.

.. code-block::

        int32_t ivas_kernel_start (IVASKernel *handle)

        Parameters:
            handle - VVAS kernel handle which has kernel context

        Return:
            0 on success -1 on failure

Use the following API to check whether the IP or kernel has finished execution. This function internally loops for MAX_EXEC_WAIT_RETRY_CNT times until a timeout before returning an error.

.. code-block::

        int32_t ivas_kernel_start (IVASKernel *handle)

        Parameters:
            handle - VVAS kernel handle which has kernel context

        Return:
            0 on success or -1 on failure
        int32_t xlnx_kernel_init (IVASKernel * handle)

        Parameters:
            handle - VVAS kernel handle which has kernel context

        Return:
            0 on success or -1 on failure


**********************************************************
Acceleration Software Library for Hard Kernels
**********************************************************

This section covers the steps to develop an acceleration software library for hard kernels.

.. note:: It is assumed that hard kernel work only on physical address. Hence Infrastructure plugins will only provide physical address for the input/output buffers. If for any reason one wants to access the input/output buffers in s/w accel lib, then need to map the buffer and get the virtual address.
Virtual address is populated by infrastructure plugins only in case of s/w accel lib for "software only" kernels.


Memory Allocation
==============================

A hard kernel works on the physically contiguous memory. Use the ivas_alloc_buffer API to allocate physically contiguous memory on the device (FPGA).


Controlling Kernel
==============================

There are two ways to control a kernel, manual mode and automatic mode.


Automatic Control Mode
---------------------------------

In this mode, VVAS is relying on the underlying XRT framework to write to the kernel registers to start the kernel. The underlying XRT framework ensures that the kernel is not accessed simultaneously by multiple users at any time. This is the recommended mode of operation for kernels that operate on memory buffers. However, there is one limitation. This mode is not suitable for streaming kernels, where kernels need to be started in auto-restart mode. For starting kernels in auto-restart mode, you must use the manual mode.

.. note:: To operate an acceleration software library in automatic mode, set the is_multiprocess flag to True in the kernel initialization API (xlnx_kernel_init).

- Programming Registers
   Use the ivas_register_write APIs to program the kernel register. In this mode, the registers are not immediately written to. The register value is updated in an internal buffer. The actual registers are updated in response to the kernel start request, described in the following section.

- Starting Kernel
   When all the register values are programed, the acceleration software library calls the ivas_kernel_start API. The kernel registers are programed and the kernel is started using the XRT command internally by the ivas_kernel_start API implementation.

- Check Kernel Done Status
   The acceleration software library calls the ivas_kernel_done API. The acceleration software library can specify the time_out value before returning from this API.


Manual Control Mode
-------------------------

The manual control mode is used when you need to start the kernel in auto-restart mode, for example, in streaming kernels. The XRT framework does not support this mode. This is achieved by directly writing into the control registers of the kernel. In this mode, you must ensure that the acceleration software library (GStreamer plug-in or application) does not allow the kernel to be accessed simultaneously by more than one thread or process at any time. It can cause unpredictable results.

- Programming Registers
   In the manual control mode, use the ivas_register_read/ivas_register_write APIs to read from or write to the kernel register. The register is accessed immediately for reading and writing.

- Starting Kernel
   Start the kernel by directly writing the appropriate value in the kernel control register.

- Check Kernel Done Status
   In this mode, the acceleration software library must either continuously poll the kernel status register using ivas_register_read, or to wait on an interrupt to know if the kernel is finished processing.


Acceleration Software Library for the Software Kernel
========================================================

Software kernels are software modules that run on the application processor. The acceleration software library for these processing modules do not interact with the XRT interface. The interface APIs that abstract the XRT interface are not needed. You must implement the core API in the acceleration software library for use in the GStreamer application through VVAS infrastructure plug-ins.

****************************************************************************************
Capability Negotiation Support in the Acceleration Software Library
****************************************************************************************

Kernel capability negotiation is an important functionality that should be accepted between the upstream element and infrastructure plug-ins to achieve an optimal solution. Because the infrastructure plug-ins are generic, the acceleration software library is responsible to populate the required kernel capability during xlnx_kernel_init(), which is negotiated between the infrastructure plug-ins and the upstream element. The infrastructure plug-in suggests a format on its sink pad and arranges the recommendation in priority order as per the kernel capability in the result from the CAPS query that is performed on the sink pad. Only the ivas_xfilter plug-in is currently supporting the kernel specific capability negotiation.

The following section explains the data structures exchange between acceleration software libraries and the infrastructure plug-ins for capability negotiation.

.. code-block::
   
        typedef struct caps
        {
        uint8_t range_height; /* true/false if height is specified in range */
        uint32_t lower_height; /* lower range of height supported,
        range_height=false then this value specified the fixed height supported
        */
        uint32_t upper_height; /* upper range of height supported */
        uint8_t range_width; /* true/false if width is specified in range */
        uint32_t lower_width; /* lower range of width supported,
        range_width=false then this value specified the fixed width supported */
        uint32_t upper_width; /* upper range of width supported */
        uint8_t num_fmt; /* number of format support by kernel */
        IVASVideoFormat *fmt; /* list of formats */
        } kernelcaps;

        typedef struct kernelpad
        {
        uint8_t nu_caps; /* number of different caps supported */
        kernelcaps **kcaps; /* lsit of caps */
        } kernelpads;

Below mentioned user friendly APIs are provided for kernel to set the above mentioned capabilities.

API to create new caps with input parameters

.. code-block::

        ivas_caps_new() - Create new caps with input parameters
        range_height
             - true  : if kernel support range of height
             - false : if kernel support fixed height
        lower_height : lower value of height supported by kernel
                       if range_height is false, this holds the fixed value
        upper_height : higher value of hight supported by kernel
                       if range_height is false, this should be 0
        range_width : same as above
        lower_width :
        upper_width :
       
                    : variable range of format supported terminated by 0
                      make sure to add 0 at end otherwise it
                      code will take format till it get 0

        kernelcaps * ivas_caps_new (uint8_t range_height,
                                    uint32_t lower_height,
                                    uint32_t upper_height, 
                                    uint8_t range_width, 
                                    uint32_t lower_width,
                                    uint32_t upper_width, ...)


API to  add new caps to sink pad. Only one pad is supported in this release.

.. code-block::

   bool ivas_caps_add_to_sink (IVASKernel * handle, kernelcaps * kcaps, int sinkpad_num)

API to add new caps to src pad. Only one pad is supported as on today.

.. code-block::

   bool ivas_caps_add_to_src (IVASKernel * handle, kernelcaps * kcaps, int sinkpad_num)

