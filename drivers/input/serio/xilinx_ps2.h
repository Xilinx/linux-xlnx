/*****************************************************************************
 *
 *     Author: Xilinx, Inc.
 *
 *     This program is free software; you can redistribute it and/or modify it
 *     under the terms of the GNU General Public License as published by the
 *     Free Software Foundation; either version 2 of the License, or (at your
 *     option) any later version.
 *
 *     XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
 *     AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
 *     SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
 *     OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
 *     APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
 *     THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
 *     AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
 *     FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
 *     WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
 *     IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
 *     REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
 *     INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE.
 *
 *     Xilinx products are not intended for use in life support appliances,
 *     devices, or systems. Use in such applications is expressly prohibited.
 *
 *     (c) Copyright 2008 Xilinx Inc.
 *     All rights reserved.
 *
 *     You should have received a copy of the GNU General Public License along
 *     with this program; if not, write to the Free Software Foundation, Inc.,
 *     675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

#ifndef XILINX_PS2_H_	/* prevent circular inclusions */
#define XILINX_PS2_H_	/* by using protection macros */

/* Register offsets for the xps2 device */
#define XPS2_SRST_OFFSET	0x00000000 /* Software Reset register */
#define XPS2_STATUS_OFFSET	0x00000004 /* Status register */
#define XPS2_RX_DATA_OFFSET	0x00000008 /* Receive Data register */
#define XPS2_TX_DATA_OFFSET	0x0000000C /* Transmit Data register */
#define XPS2_GIER_OFFSET	0x0000002C /* Global Interrupt Enable reg */
#define XPS2_IPISR_OFFSET	0x00000030 /* Interrupt Status register */
#define XPS2_IPIER_OFFSET	0x00000038 /* Interrupt Enable register */

/* Reset Register Bit Definitions */
#define XPS2_SRST_RESET		0x0000000A /* Software Reset  */

/* Status Register Bit Positions */
#define XPS2_STATUS_RX_FULL	0x00000001 /* Receive Full  */
#define XPS2_STATUS_TX_FULL	0x00000002 /* Transmit Full  */

/* Bit definitions for ISR/IER registers. Both the registers have the same bit
 * definitions and are only defined once. */
#define XPS2_IPIXR_WDT_TOUT	0x00000001 /* Watchdog Timeout Interrupt */
#define XPS2_IPIXR_TX_NOACK	0x00000002 /* Transmit No ACK Interrupt */
#define XPS2_IPIXR_TX_ACK	0x00000004 /* Transmit ACK (Data) Interrupt */
#define XPS2_IPIXR_RX_OVF	0x00000008 /* Receive Overflow Interrupt */
#define XPS2_IPIXR_RX_ERR	0x00000010 /* Receive Error Interrupt */
#define XPS2_IPIXR_RX_FULL	0x00000020 /* Receive Data Interrupt */

/* Mask for all the Transmit Interrupts */
#define XPS2_IPIXR_TX_ALL	(XPS2_IPIXR_TX_NOACK | XPS2_IPIXR_TX_ACK)

/* Mask for all the Receive Interrupts */
#define XPS2_IPIXR_RX_ALL	(XPS2_IPIXR_RX_OVF | XPS2_IPIXR_RX_ERR |  \
					XPS2_IPIXR_RX_FULL)

/* Mask for all the Interrupts */
#define XPS2_IPIXR_ALL		(XPS2_IPIXR_TX_ALL | XPS2_IPIXR_RX_ALL |  \
					XPS2_IPIXR_WDT_TOUT)

/* Global Interrupt Enable mask */
#define XPS2_GIER_GIE_MASK	0x80000000

struct xps2data {
	int irq;
	u32 phys_addr;
	u32 remap_size;
	spinlock_t lock;
	u8 rxb;				/* Rx buffer */
	void __iomem *base_address;	/* virt. address of control registers */
	unsigned long tx_end;
	unsigned int dfl;
	struct serio serio;		/* serio */
};

static u8 xps2_send(struct xps2data *drvdata, u8 *buffer_ptr);
static u8 xps2_recv(struct xps2data *drvdata, u8 *buffer_ptr);
static int xps2_initialize(struct xps2data *drvdata);
static int xps2_setup(struct device *dev, int id, struct resource *regs_res,
		      struct resource *irq_res);

#endif					/* end of protection macro */

