#####################
Development Guide
#####################

For advanceded developers who wants to develop their Kernel or VVAS acceleration s/w library for their Kernel, this section covers detailed description and steps to achieve that.  Acceleration s/w library implements the logic to control the kernel. Each acceleration s/w lib must implement four APIs that are then called by VVAS Infrastructure plugins to interact with the kernel.

********************************
Acceleration s/w lib development
********************************
This section covers the intefaces exposed by VVAS framework to develop the aceleration s/w library. It also covers various types of Kernels supported and how to develop acceleration s/w lib for each type of kernels. Refer to the :doc:`Acceleration s/w Library Developement Guide <6-common-Acceleration-Software-Library-Development>` to know more details.

***********************************
Acceleration h/w Kernel development
***********************************
This section covers the H/W (HLS) kernels supported by the VVAS release. It explains how to change the kernel configuration parameters, how to build these kernels. Ready to use workspace is already created to build the kernel. Refer to :doc:`Acceleration H/W Kernels <Acceleration-Hardware>`.

