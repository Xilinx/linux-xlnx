/* arch/arm/mach-zynq/include/mach/irqs.h
 *
 *  Copyright (C) 2011 Xilinx
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

/* AXI PCIe MSI support */
#if defined(CONFIG_XILINX_AXIPCIE) && defined(CONFIG_PCI_MSI)
#define IRQ_XILINX_MSI_0	128
#define NR_XILINX_IRQS		(IRQ_XILINX_MSI_0 + 128)
#else
#define NR_XILINX_IRQS		128
#endif

#define NR_IRQS			NR_XILINX_IRQS

#endif
