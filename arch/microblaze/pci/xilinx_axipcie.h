/*
 * Header file for Xilinx AXI PCIe IP driver.
 *
 * Copyright (c) 2010-2011 Xilinx, Inc.
 *
 * This program has adopted some work from PCI/PCIE support for AMCC
 * PowerPC boards written by Benjamin Herrenschmidt.
 * Copyright 2007 Ben. Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef XILINX_AXIPCIE_H_
#define XILINX_AXIPCIE_H_

/* Register definitions */
#define PCIE_CFG_CMD			0x00000004
#define PCIE_CFG_CLS			0x00000008
#define PCIE_CFG_HDR			0x0000000C
#define PCIE_CFG_AD1			0x00000010
#define PCIE_CFG_AD2			0x00000014
#define PCIE_CFG_BUS			0x00000018
#define PCIE_CFG_IO			0x0000001C
#define PCIE_CFG_MEM			0x00000020
#define PCIE_CFG_PREF_MEM		0x00000024
#define PCIE_CFG_PREF_BASE_UPPER	0x00000028
#define PCIE_CFG_PREF_LIMIT_UPPER	0x0000002c
#define PCIE_CFG_IO_UPPER		0x00000030

#define AXIPCIE_REG_VSECC		0x00000128
#define AXIPCIE_REG_VSECH		0x0000012c
#define AXIPCIE_REG_BIR			0x00000130
#define AXIPCIE_REG_BSCR		0x00000134
#define AXIPCIE_REG_IDR			0x00000138
#define AXIPCIE_REG_IMR			0x0000013c
#define AXIPCIE_REG_BLR			0x00000140
#define AXIPCIE_REG_PSCR		0x00000144
#define AXIPCIE_REG_RPSC		0x00000148
#define AXIPCIE_REG_MSIBASE1		0x0000014c
#define AXIPCIE_REG_MSIBASE2		0x00000150
#define AXIPCIE_REG_RPEFR		0x00000154
#define AXIPCIE_REG_RPIFR1		0x00000158
#define AXIPCIE_REG_RPIFR2		0x0000015c
#define AXIPCIE_REG_VSECC2		0x00000200
#define AXIPCIE_REG_VSECH2		0x00000204

/* Interrupt register defines */
#define AXIPCIE_INTR_LINK_DOWN		(1 << 0)
#define AXIPCIE_INTR_ECRC_ERR		(1 << 1)
#define AXIPCIE_INTR_STR_ERR		(1 << 2)
#define AXIPCIE_INTR_HOT_RESET		(1 << 3)
#define AXIPCIE_INTR_CFG_COMPL		(7 << 5)
#define AXIPCIE_INTR_CFG_TIMEOUT	(1 << 8)
#define AXIPCIE_INTR_CORRECTABLE	(1 << 9)
#define AXIPCIE_INTR_NONFATAL		(1 << 10)
#define AXIPCIE_INTR_FATAL		(1 << 11)
#define AXIPCIE_INTR_INTX		(1 << 16)
#define AXIPCIE_INTR_MSI		(1 << 17)
#define AXIPCIE_INTR_SLV_UNSUPP		(1 << 20)
#define AXIPCIE_INTR_SLV_UNEXP		(1 << 21)
#define AXIPCIE_INTR_SLV_COMPL		(1 << 22)
#define AXIPCIE_INTR_SLV_ERRP		(1 << 23)
#define AXIPCIE_INTR_SLV_CMPABT		(1 << 24)
#define AXIPCIE_INTR_SLV_ILLBUR		(1 << 25)
#define AXIPCIE_INTR_MST_DECERR		(1 << 26)
#define AXIPCIE_INTR_MST_SLVERR		(1 << 27)
#define AXIPCIE_INTR_MST_ERRP		(1 << 28)

#define BUS_LOC_SHIFT			20
#define DEV_LOC_SHIFT			12
#define PRIMARY_BUS			1
#define PORT_REG_SIZE			0x1000
#define PORT_HEADER_SIZE		0x128

#define AXIPCIE_LOCAL_CNFG_BASE		0x00000000
#define AXIPCIE_REG_BASE		0x00000128
#define AXIPCIE_REG_PSCR_LNKUP		0x00000800
#define AXIPCIE_REG_IMR_MASKALL		0x1FF30FED
#define AXIPCIE_REG_IDR_MASKALL		0xFFFFFFFF
#define AXIPCIE_REG_RPSC_BEN		0x00000001
#define BUS_MASTER_ENABLE		0x00000004

/* debug */
//#define XILINX_AXIPCIE_DEBUG
#ifdef XILINX_AXIPCIE_DEBUG
#define DBG(x...) ((void)printk(x))
#else
#define DBG(x...)	\
	do {		\
	} while(0)
#endif

/* Xilinx CR# 657412 */
/* Byte swapping */
#define xpcie_out_be32(a, v) __raw_writel(__cpu_to_be32(v), (a))
#define xpcie_out_be16(a, v) __raw_writew(__cpu_to_be16(v), (a))

#define xpcie_in_be32(a) __be32_to_cpu(__raw_readl(a))
#define xpcie_in_be16(a) __be16_to_cpu(__raw_readw(a))

#ifdef CONFIG_PCI_MSI
extern unsigned long msg_addr;
#endif

struct xilinx_axipcie_node {
	u32 number_of_instances;
	u32 device_id;
	u32 device_type;
	u32 ecam_base;
	u32 ecam_high;
	u32 baseaddr;
	u32 highaddr;
	u32 bars_num;
	u32 irq_num;
	u32 reg_base;
	u32 reg_len;
	u32 pcie2axibar_0;
	u32 pcie2axibar_1;
};

struct xilinx_axipcie_port {
	struct pci_controller	*hose;
	struct device_node	*node;
	u32			reg_base;
	u32			reg_len;
	u32			ecam_base;
	u32			ecam_high;
	u32			baseaddr;
	u32			highaddr;
	u32			header_addr;
	u8			index;
	u8			type;
	u8			link;
	u8			bars_num;
	u32			irq_num;
	unsigned int __iomem	*base_addr_remap;
	unsigned int __iomem	*header_remap;
	unsigned int __iomem	*ecam_remap;
	u32 pcie2axibar_0;
	u32 pcie2axibar_1;
};

#endif /* XILINX_AXIPCIE_H_ */
