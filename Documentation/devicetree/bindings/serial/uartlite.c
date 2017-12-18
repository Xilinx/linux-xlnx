Xilinx Axi Uartlite controller Device Tree Bindings
---------------------------------------------------------

Required properties:
- compatible		: Can be either of
				"xlnx,xps-uartlite-1.00.a"
				"xlnx,opb-uartlite-1.00.b"
- reg			: Physical base address and size of the Axi Uartlite
			  registers map.
- interrupts		: Property with a value describing the interrupt
			  number.
- interrupt-parent	: Must be core interrupt controller.

Optional properties:
- port-number		: Set Uart port number

Example:
serial@800C0000 {
	compatible = "xlnx,xps-uartlite-1.00.a";
	reg = <0x0 0x800c0000 0x10000>;
	interrupt-parent = <&gic>;
	interrupts = <0x0 0x6e 0x1>;
	port-number = <0>;
};
