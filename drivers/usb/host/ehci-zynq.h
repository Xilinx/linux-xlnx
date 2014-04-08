/*
 * Xilinx Zynq USB Host Controller Driver Header file.
 *
 * Copyright (C) 2011 - 2014 Xilinx, Inc.
 *
 * This file is based on ehci-fsl.h file with few minor modifications
 * to support Xilinx Zynq USB controller.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#ifndef _EHCI_ZYNQ_H
#define _EHCI_ZYNQ_H

#include <linux/usb/zynq_otg.h>

/* offsets for the non-ehci registers in the ZYNQ SOC USB controller */
#define ZYNQ_SOC_USB_ULPIVP	0x170
#define ZYNQ_SOC_USB_PORTSC1	0x184
#define PORT_PTS_MSK		(3<<30)
#define PORT_PTS_UTMI		(0<<30)
#define PORT_PTS_ULPI		(2<<30)
#define PORT_PTS_SERIAL		(3<<30)
#define PORT_PTS_PTW		(1<<28)
#define ZYNQ_SOC_USB_PORTSC2	0x188

#endif /* _EHCI_ZYNQ_H */
