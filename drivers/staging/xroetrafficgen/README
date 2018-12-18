Xilinx Radio over Ethernet Traffic Generator driver
===================================================

About the RoE Framer Traffic Generator

The Traffic Generator is used for in testing of other RoE IP Blocks (currenty
the XRoE Framer) and simulates an radio antenna interface. It generates rolling
rampdata for eCPRI antenna paths. Each path is tagged with the antenna number.
The sink locks to this ramp data, then checks the next value is as expected.


About the Linux Driver

The RoE Traffic Generator Linux Driver provides sysfs access to control a
simulated radio antenna interface.
The loading of the driver to the hardware is possible using Device Tree binding
(see "dt-binding.txt" for more information). When the driver is loaded, the
general controls (such as sink lock, enable, loopback etc) are exposed
under /sys/kernel/xroetrafficgen.
