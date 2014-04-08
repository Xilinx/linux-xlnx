/*
 * Definitions for any platform device related flags or structures for
 * Xilinx EDK IPs
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002-2005 (c) MontaVista Software, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#ifdef __KERNEL__
#ifndef _XILINX_DEVICE_H_
#define _XILINX_DEVICE_H_

#include <linux/types.h>
#include <linux/version.h>
#include <linux/platform_device.h>

/*- PS USB Controller IP -*/
enum zynq_usb2_operating_modes {
	ZYNQ_USB2_MPH_HOST,
	ZYNQ_USB2_DR_HOST,
	ZYNQ_USB2_DR_DEVICE,
	ZYNQ_USB2_DR_OTG,
};

enum zynq_usb2_phy_modes {
	ZYNQ_USB2_PHY_NONE,
	ZYNQ_USB2_PHY_ULPI,
	ZYNQ_USB2_PHY_UTMI,
	ZYNQ_USB2_PHY_UTMI_WIDE,
	ZYNQ_USB2_PHY_SERIAL,
};

struct clk;
struct platform_device;

struct zynq_usb2_platform_data {
	/* board specific information */
	enum zynq_usb2_operating_modes	operating_mode;
	enum zynq_usb2_phy_modes	phy_mode;
	unsigned int			port_enables;
	unsigned int			workaround;

	int		(*init)(struct platform_device *);
	void		(*exit)(struct platform_device *);
	void __iomem	*regs;		/* ioremap'd register base */
	struct usb_phy	*otg;
	struct usb_phy	*ulpi;
	int		irq;
	struct clk	*clk;
	unsigned	big_endian_mmio:1;
	unsigned	big_endian_desc:1;
	unsigned	es:1;		/* need USBMODE:ES */
	unsigned	le_setup_buf:1;
	unsigned	have_sysif_regs:1;
	unsigned	invert_drvvbus:1;
	unsigned	invert_pwr_fault:1;
};

#endif /* _XILINX_DEVICE_H_ */
#endif /* __KERNEL__ */
