Xilinx Radio over Ethernet Framer driver
=========================================

About the RoE Framer

The "Radio Over Ethernet Framer" IP (roe_framer) ingests/generates Ethernet
packet data, (de-)multiplexes packets based on protocol into/from various
Radio Antenna data streams.

It has 2 main, independent, data paths

- Downlink, from the BaseBand to the Phone, Ethernet to Antenna,
we call this the De-Framer path, or defm on all related IP signals.

- Uplink, from the Phone to the BaseBand, Antenna to Ethernet,
we call this the Framer path, or fram on all related IP signals.

Key points:

- Apart from the AXI4-Lite configuration port and a handful of strobe/control
signals all data interfaces are AXI Stream(AXIS).
- The IP does not contain an Ethernet MAC IP, rather it routes, or creates
packets based on the direction through the roe_framer.
- Currently designed to work with
	- 1, 2 or 4 10G Ethernet AXIS stream ports to/from 1, 2, 4, 8, 16,
	or 32 antenna ports
		Note: each Ethernet port is 64 bit data @ 156.25MHz
	- 1 or 2 25G Ethernet AXIS stream ports to/from 1, 2, 4, 8, 16,
	or 32 antenna ports
		Note: each Ethernet port is 64 bit data @ 390.25MHz
- Contains a filter so that all non-protocol packets, or non-hardware-IP
processed packets can be forwarded to another block for processing. In general
this in a Microprocessor, specifically the Zynq ARM in our case. This filter
function can move into the optional switch when TSN is used.

About the Linux Driver

The RoE Framer Linux Driver provides sysfs access to the framer controls. The
loading of the driver to the hardware is possible using Device Tree binding
(see "dt-binding.txt" for more information). When the driver is loaded, the
general controls (such as framing mode, enable, restart etc) are exposed
under /sys/kernel/xroe. Furthermore, specific controls can be found under
/sys/kernel/xroe/framer. These include protocol-specific settings, for
IPv4, IPv6 & UDP.

There is also the option of accessing the framer's register map using
ioctl calls for both reading and writing (where permitted) directly.
