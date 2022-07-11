#########################
Alveo U30 Platform setup
#########################

This section covers the steps to prepare setup for Alveo U30 card based platform for PCIe based solutions like Data Center usecases. This setup consists of one or more Alveo U30 cards installed in the PCIe slots of a server.

Hardware Requirements
---------------------
- Alveo U30 card (https://www.xilinx.com/products/boards-and-kits/alveo/u30.html)
- Compatible/Recommended x86 servers

Software Requirements
----------------------
- Host system with Ubuntu 20.04 kernel 5.10

.. Note:: This platform has been tested on UBuntu 20.04, kernel 5.10 only.

Installation Steps
--------------------

Before starting software installation, make sure U30 cards are installed and detected. Execute the command mentioned below:

.. code-block:: shell

    lspci | grep Xilinx

If the messages as a result of above command on console shows "Processing accelerators: Xilinx Corporation" then devices are detected and you may continue with the installation following the steps mentioned below.

1.  On the host server, get the release package (vvas-2.0_dc.zip) from <Add release path>.

2.  Install the release packages

.. code-block:: shell

    unzip vvas-2.0_dc.zip
    cd vvas-2.0_dc
    sudo ./install

3.  After all packages are installed successfully (as seen from the last few messages of install step - "++++ ++++ Post Install Verification Successful ++++ ++++"), login as root
    and setup environment to flash the U30

.. code-block:: shell

    sudo su
    source /opt/xilinx/xrt/setup.sh

4.  Flash the card from the installed folder. Please note this step takes longer depending on the number of U30 cards
    setup in the host server.

.. code-block:: shell

    ./u30flashall.sh

5. Below is a sample message displayed on the console which shows successful flashing of one U30 card ( note : one U30 card has 2 Zynq MPSoC devices)

.. code-block:: shell

    ./u30flashall.sh
    =====================================================
    [1]: Updating flash image(s) for device: 0000:d9:00.0
    =====================================================
    xbmgmt program -d 0000:d9:00.0 -b shell --image /opt/xilinx/firmware/u30/gen3x4/base/partition.xsabin
    [PASSED] : Flash erased < 1m 48s >
    [PASSED] : Flash programmed < 1m 10s >
    [PASSED] : Flash verified < 29s >
    INFO     : Base flash image has been programmed successfully.
    ****************************************************
    Cold reboot machine to load the new image on device.
    ****************************************************

    =====================================================
    [2]: Updating flash image(s) for device: 0000:d8:00.0
    =====================================================
    xbmgmt program -d 0000:d8:00.0 -b shell --image /opt/xilinx/firmware/u30/gen3x4/base/partition.xsabin
    [PASSED] : Flash erased < 1m 45s >
    [PASSED] : Flash programmed < 1m 7s >
    [PASSED] : Flash verified < 34s >
    INFO     : Base flash image has been programmed successfully.
    ****************************************************
    Cold reboot machine to load the new image on device.
    ****************************************************

After successful flashing, reboot the host server. Once rebooted, setup is ready to run the use-cases.

For details about how to execute the test cases, refer to below mentioned Tutorials:

* Tutorial `Basic Transcoding <../Tutorials/transcoding.rst>`_
* Tutorial `Multi-instance Transcoding <../Tutorials/multi_instance_launch_utilities.rst>`_
