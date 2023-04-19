#####################
Development Guide
#####################

For advanced developers who wants to develop their Kernel or VVAS acceleration s/w library for their Kernel, this section covers detailed description and steps to achieve that.  Acceleration s/w library implements the logic to control the kernel. Each acceleration s/w lib must implement four APIs that are then called by VVAS Infrastructure plugins to interact with the kernel.



.. toctree::
   :maxdepth: 3
   :caption: For Advanced Developers
   :hidden:

   Acceleration s/w Library Developement Guide <Acceleration-Software-Library-Development>
   Acceleration H/W Kernels <Acceleration-Hardware>
   Add support for new models in VVAS <adding_new_model>


.. list-table:: 
   :widths: 20 80
   :header-rows: 1
   
   * - Title
     - Description
	 
   * - :doc:`Acceleration s/w Library Developement Guide <Acceleration-Software-Library-Development>`
     - This section covers the intefaces exposed by VVAS framework to develop the aceleration s/w library. It also covers various types of Kernels supported and how to develop acceleration s/w lib for each type of kernels.

   * - :doc:`Acceleration H/W Kernels <Acceleration-Hardware>`
     - This section covers the H/W (HLS) kernels supported by the VVAS release. It explains how to change the kernel configuration parameters, how to build these kernels. Ready to use workspace is already created to build the kernel.

   * - :doc:`Adding new model support in VVAS  <adding_new_model>`
     - This section covers the details about how to add a new model support into VVAS.
