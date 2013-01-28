/* arch/arm/mach-xilinx/include/mach/pl330.h
 *
 *  Copyright (C) 2009 Xilinx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_PL330_H__
#define __ASM_ARCH_PL330_H__

#include <linux/types.h>

/**
 * pl330_bus_des - This is the struct to characterize the AXI bus transaction.
 * @burst_size:	The data size for the burst when
 * 	reading or writing a device. It is in
 * 	terms of bytes, must be power of two and less
 * 	than the bus word size.
 * @burst_len: The number of transfers for each burst.
 * @prot_ctrl: Protection against illegal transactions, value is 0 .. 7
 * @cache_ctrl: System level cache, value is 0 .. 15
 */
struct pl330_bus_des {
	unsigned int burst_size;
	unsigned int burst_len;
	unsigned int prot_ctrl;
	unsigned int cache_ctrl;
};

/**
 * pl330_client_data - This is the struct for a DMA cleint device
 * @dev_addr: It's the bus address for the client device
 * @dev_bus_des: It's the characterization of the bus transaction for
 *	the device.
 * @mem_bus_des: It's the characterization of the bus transaction for
 *	the memory.
 * @endian_swap_size: It defines whether data can be swapped little-endian (LE)
 *	and byte-invariant big-endia (BE-8) format. Here are the acceptable
 *	values:
 *	b000: No swap, 8-bit data
 *	b001: Swap bytes within 16-bit data
 *	b010: Swap bytes within 32-bit data
 *	b011: Swap bytes within 64-bit data
 *	b100: Swap bytes within 128-bit data
 *	b101: Reserved
 *	b110: Reserved
 *	b111: Reserved
 */
struct pl330_client_data {
	dma_addr_t dev_addr;
	struct pl330_bus_des dev_bus_des;
	struct pl330_bus_des mem_bus_des;
	unsigned int endian_swap_size;
};


typedef void (*pl330_done_callback_t) (unsigned int channel, void *data);
typedef void (*pl330_fault_callback_t) (unsigned int channel,
					unsigned int fault_type,
					unsigned int fault_address,
					void *data);

extern int set_pl330_client_data(unsigned int channel,
				 struct pl330_client_data *dev_data);

extern int set_pl330_done_callback(unsigned int channel,
				   pl330_done_callback_t done_callback,
				   void *data);

extern int set_pl330_fault_callback(unsigned int channel,
				    pl330_fault_callback_t fault_callback,
				    void *data);

extern int set_pl330_dma_prog_addr(unsigned int channel, u32 start_address);

extern int set_pl330_incr_dev_addr(unsigned int channel, unsigned int flag);

extern char *get_pl330_dma_program(unsigned int channel, unsigned int *bytes);

extern u32 get_pl330_sa_reg(unsigned int channel);

extern u32 get_pl330_da_reg(unsigned int channel);

#endif
