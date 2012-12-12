/*
 * Xilinx PL330 DMAC driver
 *
 * 2009 (c) Xilinx, Inc.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any
 * later version.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 * 02139, USA.
 */

/*
 * The AXI PL330 DMA API is modeled on the ISA DMA API and performs DMA
 * transfers between a device and memory, i.e. a fixed address and a memory
 * region.
 *
 * The AXI bus related configurations, such as burst size, burst length,
 * protection control etc., are passes through some functions provided
 * in this driver. The driver will construct PL330 DMA program and let
 * the PL330 execute the program.
 *
 */

/*
 * Usage of this driver:
 *
 * There are a few things that the ISA DMA API does not cover.
 *
 * You need to set up the AXI bus transaction configurations for
 * both device side and the memory side. You also need to pass the device
 * address to the driver. You can use struct pl330_client_data and
 * the function set_pl330_client_data to pass the above settings.
 *
 * The driver has interrupt service routines for DMA done interrupt and DMA
 * abort interrupt. You can pass your own callbacks for DMA done and DMA fault
 * to the driver using the set_pl330_done_callback function and the
 * set_pl330_fault_callback function.
 *
 * In general, the driver generates a DMA program on the fly for the PL330
 * to execute. If you want PL330 to execute your own DMA program, you can call
 * function set_pl330_dma_prog_addr.
 *
 * Here's an example of starting a DMA transaction:
 *
 *	struct pl330_client_data client_data = {
 *		.dev_addr = my_device_addr,
 *		.dev_bus_des = {
 *			.burst_size = 4,
 *			.burst_len = 4,
 *		},
 *		.mem_bus_des = {
 *			.burst_size = 4,
 *			.burst_len = 4,
 *		},
 *	};
 *
 *	status = request_dma(channel, DRIVER_NAME);
 *
 *	if (status != 0)
 *		goto failed;
 *
 *	set_dma_mode(channel, DMA_MODE_READ);
 *
 *	set_dma_addr(channel, buf_bus_addr);
 *
 *	set_dma_count(channel, num_of_bytes);
 *
 *	set_pl330_client_data(channel, &client_data);
 *
 *	set_pl330_done_callback(channel, my_done_callback, my_dev);
 *
 *	set_pl330_fault_callback(channel, my_fault_callback2, my_dev);
 *
 *	enable_dma(channel);
 *
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/spinlock_types.h>

#include <asm/atomic.h>
#include <linux/io.h>
#include <asm/dma.h>
#include <asm/mach/dma.h>
#include <mach/pl330.h>

#define DRIVER_NAME         "pl330"

#define PL330_OPTIMIZE_ICACHE
#define PL330_DEFAULT_BURST_SIZE 4

#ifndef PL330_MAX_WAIT
#define PL330_MAX_WAIT 40000
#endif


static dma_t dma_chan[MAX_DMA_CHANNELS];

/**
 * struct pl330_device_data - Stores information related to the device.
 * @base: Device base address
 * @channels: Number of channels
 * @starting_channel: Starting channel number in the driver API
 * @starting_irq: Starting interrupt number in the driver API of 1st half
 * @ending_irq: Last interrupt number in the driver API of 1st section
 * @starting_irq1: Starting interrupt number in the driver API of 2nd half
 * @ending_irq1: Last interrupt number in the driver API of 2nd section
 * @dev_id: Device id. The id starts from zero for the first PL330 device. The
 *	last id is MAX_DMA_DEVICES - 1.
 * @dev: Pointer to the device struct.
 * @lock: Spin lock for the device. This is needed when accessing DMA debug
 *	register and disabling and enabling interrupts.
 * @fault_irq: Fault interrupt number
 * @default_burst_size: Default burst size
 * @i_cache_len: PL330 instruction cache line length. This information is read
 *	from config register 1.
 */
struct pl330_device_data {
	void __iomem *base;
	unsigned int channels;
	unsigned int starting_channel;
	unsigned int starting_irq;
	unsigned int ending_irq;
	unsigned int starting_irq1;
	unsigned int ending_irq1;
	unsigned int dev_id;
	struct device *dev;
	spinlock_t lock;
	unsigned int fault_irq;
	unsigned int  default_burst_size;
	unsigned int i_cache_len;
};

/**
 * struct pl330_channel_static_data - Static information related to a channel.
 * @dev_id: Device id
 * @channel: Channel number of the DMA driver. This is the number that passed
 *	to the DMA API. The number starts at 0. For example, if a system
 *	has two DMA devices, each DMA device has 4 channels, then the channel
 *	number is from 0 to 7. The channel number for the first DMA device is
 *	from 0 to 3. The channel number for the second DMA device is 4 to 7.
 * @dev_chan: Channel number of the device. This number is relative to the
 *	corresponding device. The number starts at 0. For example, if a system
 *	system has two DMA devices, each DMA device has 4 channels. The
 *	dev_chan for the first device is 0 to 3. The dev_chan for the second
 *	second device is also 0 to 3.
 * @irq: Interrupt number assigned to the channel
 */
struct pl330_channel_static_data {
	unsigned int dev_id;
	unsigned int channel;
	unsigned int dev_chan;
	unsigned int irq;
};

/**
 * struct pl330_channel_data - Channel related information
 * @dma_program: Starting address of DMA program for the channel set by users
 * @dma_prog_buf: Buffer for DMA program if there is a need to construct the
 *	program. This is a virtual address.
 * @dma_prog_phy: Buffer physical address for DMA program. This is needed when
 *	the buffer is released by dma_free_coherent.
 * @dma_prog_len: Length of constructed DMA program.
 * @client_data: Instance of pl330_client_data passed from a client of the
 *	driver.
 * @done_callback: Done callback function.
 * @done_callback_data: Done callback data.
 * @fault_callback: Fault callback function.
 * @fault_callback_data: Fault callback data.
 * @incr_dev_addr: A flag for whether incrementing the device address
 * @default_client_data: Default client data setting
 */
struct pl330_channel_data {
	u32 dma_program;
	void *dma_prog_buf;
	dma_addr_t dma_prog_phy;
	unsigned int dma_prog_len;
	struct pl330_client_data *client_data;
	pl330_done_callback_t done_callback;
	void *done_callback_data;
	pl330_fault_callback_t fault_callback;
	void *fault_callback_data;
	unsigned int incr_dev_addr;
	struct pl330_client_data default_client_data;
};

/**
 * struct pl330_driver_data - Top level struct for the driver data.
 * @dma_chan: Pointer to the dma_struct array.
 * @device_data: Array of pl330_device_data.
 * @channel_data: Array of pl330_channel_data.
 * @channel_static_data: Array of pl330_channel_static_data.
 */
struct pl330_driver_data {
	struct dma_struct *dma_chan;
	struct pl330_device_data device_data[MAX_DMA_DEVICES];
	struct pl330_channel_data channel_data[MAX_DMA_CHANNELS];
	struct pl330_channel_static_data channel_static_data[MAX_DMA_CHANNELS];
};


/*
 * driver_data Root instance of the pl330_driver_data.
 */
static struct pl330_driver_data driver_data;

/*
 * read and write macros for register IO.
 */

#define pl330_readreg(base, offset) __raw_readl(base + offset)
#define pl330_writereg(data, base, offset) __raw_writel(data, base + offset)


/*
 * Register offset for PL330
 */
#define PL330_DS_OFFSET		0x000 /* DMA Status Register */
#define PL330_DPC_OFFSET	0x004 /* DMA Program Counter Rregister */
#define PL330_INTEN_OFFSET	0X020 /* DMA Interrupt Enable Register */
#define PL330_ES_OFFSET		0x024 /* DMA Event Status Register */
#define PL330_INTSTATUS_OFFSET	0x028 /* DMA Interrupt Status Register */
#define PL330_INTCLR_OFFSET	0x02c /* DMA Interrupt Clear Register */
#define PL330_FSM_OFFSET	0x030 /* DMA Fault Status DMA Manager
				       * Register
				       */
#define PL330_FSC_OFFSET	0x034 /* DMA Fault Status DMA Chanel Register
				       */
#define PL330_FTM_OFFSET	0x038 /* DMA Fault Type DMA Manager Register */

#define PL330_FTC0_OFFSET	0x040 /* DMA Fault Type for DMA Channel 0 */
/*
 * The offset for the rest of the FTC registers is calculated as
 * FTC0 + dev_chan_num * 4
 */
#define PL330_FTCn_OFFSET(ch)	(PL330_FTC0_OFFSET + (ch) * 4)

#define PL330_CS0_OFFSET	0x100 /* Channel Status for DMA Channel 0 */
/*
 * The offset for the rest of the CS registers is calculated as
 * CS0 + * dev_chan_num * 0x08
 */
#define PL330_CSn_OFFSET(ch)	(PL330_CS0_OFFSET + (ch) * 8)

#define PL330_CPC0_OFFSET	0x104 /* Channel Program Counter for DMA
				       * Channel 0
				       */
/*
 * The offset for the rest of the CPC registers is calculated as
 * CPC0 + dev_chan_num * 0x08
 */
#define PL330_CPCn_OFFSET(ch)	(PL330_CPC0_OFFSET + (ch) * 8)

#define PL330_SA_0_OFFSET	0x400 /* Source Address Register for DMA
				       * Channel 0
				       */
/* The offset for the rest of the SA registers is calculated as
 * SA_0 + dev_chan_num * 0x20
 */
#define PL330_SA_n_OFFSET(ch)	(PL330_SA_0_OFFSET + (ch) * 0x20)

#define PL330_DA_0_OFFSET	0x404 /* Destination Address Register for
				       * DMA Channel 0
				       */
/* The offset for the rest of the DA registers is calculated as
 * DA_0 + dev_chan_num * 0x20
 */
#define PL330_DA_n_OFFSET(ch)	(PL330_DA_0_OFFSET + (ch) * 0x20)

#define PL330_CC_0_OFFSET	0x408 /* Channel Control Register for
				       * DMA Channel 0
				       */
/*
 * The offset for the rest of the CC registers is calculated as
 * CC_0 + dev_chan_num * 0x20
 */
#define PL330_CC_n_OFFSET(ch)	(PL330_CC_0_OFFSET + (ch) * 0x20)

#define PL330_LC0_0_OFFSET	0x40C /* Loop Counter 0 for DMA Channel 0 */
/*
 * The offset for the rest of the LC0 registers is calculated as
 * LC_0 + dev_chan_num * 0x20
 */
#define PL330_LC0_n_OFFSET(ch)	(PL330_LC0_0_OFFSET + (ch) * 0x20)
#define PL330_LC1_0_OFFSET	0x410 /* Loop Counter 1 for DMA Channel 0 */
/*
 * The offset for the rest of the LC1 registers is calculated as
 * LC_0 + dev_chan_num * 0x20
 */
#define PL330_LC1_n_OFFSET(ch)	(PL330_LC1_0_OFFSET + (ch) * 0x20)

#define PL330_DBGSTATUS_OFFSET	0xD00 /* Debug Status Register */
#define PL330_DBGCMD_OFFSET	0xD04 /* Debug Command Register */
#define PL330_DBGINST0_OFFSET	0xD08 /* Debug Instruction 0 Register */
#define PL330_DBGINST1_OFFSET	0xD0C /* Debug Instruction 1 Register */

#define PL330_CR0_OFFSET	0xE00 /* Configuration Register 0 */
#define PL330_CR1_OFFSET	0xE04 /* Configuration Register 1 */
#define PL330_CR2_OFFSET	0xE08 /* Configuration Register 2 */
#define PL330_CR3_OFFSET	0xE0C /* Configuration Register 3 */
#define PL330_CR4_OFFSET	0xE10 /* Configuration Register 4 */
#define PL330_CRDN_OFFSET	0xE14 /* Configuration Register Dn */

#define PL330_PERIPH_ID_0_OFFSET	0xFE0 /* Peripheral Identification
					       * Register 0
					       */
#define PL330_PERIPH_ID_1_OFFSET	0xFE4 /* Peripheral Identification
					       * Register 1
					       */
#define PL330_PERIPH_ID_2_OFFSET	0xFE8 /* Peripheral Identification
					       * Register 2
					       */
#define PL330_PERIPH_ID_3_OFFSET	0xFEC /* Peripheral Identification
					       * Register 3
					       */
#define PL330_PCELL_ID_0_OFFSET	0xFF0 /* PrimeCell Identification
				       * Register 0
				       */
#define PL330_PCELL_ID_1_OFFSET	0xFF4 /* PrimeCell Identification
				       * Register 1
				       */
#define PL330_PCELL_ID_2_OFFSET	0xFF8 /* PrimeCell Identification
				       * Register 2
				       */
#define PL330_PCELL_ID_3_OFFSET	0xFFC /* PrimeCell Identification
				       * Register 3
				       */

/*
 * Some useful register masks
 */
#define PL330_DS_DMA_STATUS		0x0F /* DMA status mask */
#define PL330_DS_DMA_STATUS_STOPPED	0x00 /* debug status busy mask */

#define PL330_DBGSTATUS_BUSY		0x01 /* debug status busy mask */

#define PL330_CS_ACTIVE_MASK		0x07 /* channel status active mask,
					      * llast 3 bits of CS register
					      */

#define PL330_CR1_I_CACHE_LEN_MASK	0x07 /* i_cache_len mask */


/*
 * PL330_DBGINST0 - constructs the word for the Debug Instruction-0 Register.
 * @b1: Instruction byte 1
 * @b0: Instruction byte 0
 * @ch: Channel number
 * @dbg_th: Debug thread encoding: 0 = DMA manager thread, 1 = DMA channel
 */
#define PL330_DBGINST0(b1, b0, ch, dbg_th) \
	(((b1) << 24) | ((b0) << 16) | (((ch) & 0x7) << 8) | ((dbg_th & 0x1)))


/**
 * pl330_instr_dmaend - Construction function for DMAEND instruction. This
 *	function fills the program buffer with the constructed instruction.
 * @dma_prog: the DMA program buffer, it's the starting address for the
 *	instruction being constructed
 *
 * Returns the number of bytes for this instruction which is 1.
 */
inline int pl330_instr_dmaend(char *dma_prog)
{
	/*
	 * DMAEND encoding:
	 * 7 6 5 4 3 2 1 0
	 * 0 0 0 0 0 0 0 0
	 */
	*dma_prog = 0x0;

	return 1;
}

/**
 * pl330_instr_dmago - Construction function for DMAGO instruction. This
 *	function fills the program buffer with the constructed instruction.
 * @dma_prog: the DMA program buffer, it's the starting address for the
 *	instruction being constructed
 * @cn: Channel number, 0 - 7
 * @imm: 32-bit immediate number written to the Channel Program Counter.
 * @ns: Non-secure flag. If ns is 1, the DMA channel operates in the
 *	Non-secure state. If ns is 0, the execution depends on the security
 *	state of the DMA manager:
 *		DMA manager is in the Secure state, DMA channel operates in the
 *			Secure state.
 *		DMA manager is in the Non-secure state,
 *			DMAC aborts.
 *
 * Returns the number of bytes for this instruction which is 6.
 */
inline int pl330_instr_dmago(char *dma_prog, unsigned int cn,
			     u32 imm, unsigned int ns)
{
	pr_debug("entering pl330_instru_dmago(%#x, %d, %#x, %d)\n",
			(unsigned int)dma_prog, cn, imm, ns);
	/*
	 * DMAGO encoding:
	 * 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
	 *  0  0  0  0  0 |cn[2:0]| 1  0  1  0  0  0 ns  0
	 *
	 * 47 ... 16
	 *  imm[32:0]
	 */
	*dma_prog = 0xA0 | ((ns << 1) & 0x02);

	*(dma_prog + 1) = (u8)(cn & 0x07);

	*((u32 *)(dma_prog + 2)) = imm;

	/* success */
	return 6;
}

/**
 * pl330_instr_dmald - Construction function for DMALD instruction. This
 *	function fills the program buffer with the constructed instruction.
 * @dma_prog: the DMA program buffer, it's the starting address for the
 *	instruction being constructed
 *
 * Returns the number of bytes for this instruction which is 1.
 */
inline int pl330_instr_dmald(char *dma_prog)
{
	/*
	 * DMALD encoding
	 * 7 6 5 4 3 2 1  0
	 * 0 0 0 0 0 1 bs x
	 *
	 * Note: this driver doesn't support conditional load or store,
	 * so the bs bit is 0 and x bit is 0.
	 */
	*dma_prog = 0x04;
	return 1;
}

/**
 * pl330_instr_dmalp - Construction function for DMALP instruction. This
 *	function fills the program buffer with the constructed instruction.
 * @dma_prog: the DMA program buffer, it's the starting address for the
 *	instruction being constructed
 * @lc:	Loop counter register, can either be 0 or 1.
 * @loop_iterations: the number of interations, loop_interations - 1 will
 *	be encoded in the DMALP instruction.
 *
 * Returns the number of bytes for this instruction which is 2.
 */
inline int pl330_instr_dmalp(char *dma_prog, unsigned lc,
			     unsigned loop_iterations)
{
	/*
	 * DMALP encoding
	 * 15   ...   8 7 6 5 4 3 2 1  0
	 * | iter[7:0] |0 0 1 0 0 0 lc 0
	 */
	*dma_prog = (u8)(0x20 | ((lc & 1) << 1));
	*(dma_prog + 1) = (u8)(loop_iterations - 1);
	return 2;
}

/**
 * pl330_instr_dmalpend - Construction function for DMALPEND instruction. This
 *	function fills the program buffer with the constructed instruction.
 * @dma_prog: the DMA program buffer, it's the starting address for the
 *	instruction being constructed
 * @body_start: the starting address of the loop body. It is used to calculate
 *	the bytes of backward jump.
 * @lc:	Loop counter register, can either be 0 or 1.
 *
 * Returns the number of bytes for this instruction which is 2.
 */
inline int pl330_instr_dmalpend(char *dma_prog, char *body_start, unsigned lc)
{
	/*
	 * DMALPEND encoding
	 * 15       ...        8 7 6 5 4  3 2  1  0
	 * | backward_jump[7:0] |0 0 1 nf 1 lc bs x
	 *
	 * lc: loop counter
	 * nf is for loop forever. The driver does not support loop forever,
	 * so nf is 1.
	 * The driver does not support conditional LPEND, so bs is 0, x is 0.
	 */
	*dma_prog = 0x38 | ((lc & 1) << 2);
	*(dma_prog + 1) = (u8)(dma_prog - body_start);

	return 2;
}

/*
 * Register number for the DMAMOV instruction
 */
#define PL330_MOV_SAR 0x0
#define PL330_MOV_CCR 0x1
#define PL330_MOV_DAR 0x2

/**
 * pl330_instr_dmamov - Construction function for DMAMOV instruction. This
 *	function fills the program buffer with the constructed instruction.
 * @dma_prog: the DMA program buffer, it's the starting address for the
 *	instruction being constructed
 * @rd:	register id, 0 for SAR, 1 for CCR, and 2 for DAR
 * @imm: the 32-bit immediate number
 *
 * Returns the number of bytes for this instruction which is 6.
 */
inline int pl330_instr_dmamov(char *dma_prog, unsigned rd, u32 imm)
{
	/*
	 * DMAMOV encoding
	 * 15 4 3 2 1 10 ... 8 7 6 5 4 3 2 1 0
	 *  0 0 0 0 0 |rd[2:0]|1 0 1 1 1 1 0 0
	 *
	 * 47 ... 16
	 *  imm[32:0]
	 *
	 * rd: b000 for SAR, b001 CCR, b010 DAR
	 */
	*dma_prog = 0xBC;
	*(dma_prog + 1) = rd & 0x7;
	*((u32 *)(dma_prog + 2)) = imm;

	return 6;
}

/**
 * pl330_instr_dmanop - Construction function for DMANOP instruction. This
 *	function fills the program buffer with the constructed instruction.
 * @dma_prog: the DMA program buffer, it's the starting address for the
 *	instruction being constructed
 *
 * Returns the number of bytes for this instruction which is 1.
 */
inline int pl330_instr_dmanop(char *dma_prog)
{
	/*
	 * DMANOP encoding
	 * 7 6 5 4 3 2 1 0
	 * 0 0 0 1 1 0 0 0
	 */
	*dma_prog = 0x18;
	return 1;
}

/**
 * pl330_instr_dmarmb - Construction function for DMARMB instruction. This
 *	function fills the program buffer with the constructed instruction.
 * @dma_prog: the DMA program buffer, it's the starting address for the
 *	instruction being constructed
 *
 * Returns the number of bytes for this instruction which is 1.
 */
inline int pl330_instr_dmarmb(char *dma_prog)
{
	/*
	 * DMARMB encoding
	 * 7 6 5 4 3 2 1 0
	 * 0 0 0 1 0 0 1 0
	 */
	*dma_prog = 0x12;
	return 1;
}

/**
 * pl330_instr_dmasev - Construction function for DMASEV instruction. This
 *	function fills the program buffer with the constructed instruction.
 * @dma_prog: the DMA program buffer, it's the starting address for the
 *		instruction being constructed
 * @event_number:	Event number to signal.
 *
 * Returns the number of bytes for this instruction which is 2.
 */
inline int pl330_instr_dmasev(char *dma_prog, unsigned event_number)
{
	/*
	 * DMASEV encoding
	 * 15 4 3 2 1  10 9 8 7 6 5 4 3 2 1 0
	 * |event[4:0]| 0 0 0 0 0 1 1 0 1 0 0
	 */
	*dma_prog = 0x34;
	*(dma_prog + 1) = (u8)(event_number << 3);

	return 2;
}


/**
 * pl330_instr_dmast - Construction function for DMAST instruction. This
 *	function fills the program buffer with the constructed instruction.
 * @dma_prog: the DMA program buffer, it's the starting address for the
 *	instruction being constructed
 *
 * Returns the number of bytes for this instruction which is 1.
 */
inline int pl330_instr_dmast(char *dma_prog)
{
	/*
	 * DMAST encoding
	 * 7 6 5 4 3 2 1  0
	 * 0 0 0 0 1 0 bs x
	 *
	 * Note: this driver doesn't support conditional load or store,
	 * so the bs bit is 0 and x bit is 0.
	 */
	*dma_prog = 0x08;
	return 1;
}

/**
 * pl330_instr_dmawmb - Construction function for DMAWMB instruction. This
 *	function fills the program buffer with the constructed instruction.
 * @dma_prog: the DMA program buffer, it's the starting address for the
 *	instruction being constructed
 *
 * Returns the number of bytes for this instruction which is 1.
 */
inline int pl330_instr_dmawmb(char *dma_prog)
{
	/*
	 * DMAWMB encoding
	 * 7 6 5 4 3 2 1 0
	 * 0 0 0 1 0 0 1 0
	 */
	*dma_prog = 0x13;
	return 1;
}

/**
 * pl330_to_endian_swap_size_bits - conversion function from the endian
 *	swap size to the bit encoding of the CCR
 * @endian_swap_size: The endian swap size, in terms of bits, it could be
 *	8, 16, 32, 64, or 128. (We are using DMA assembly syntax.)
 *
 * Returns the endian swap size bit encoding for the CCR.
 */
inline unsigned pl330_to_endian_swap_size_bits(unsigned endian_swap_size)
{
	switch (endian_swap_size) {
	case 0:
	case 8:
		return 0;
	case 16:
		return 1;
	case 32:
		return 2;
	case 64:
		return 3;
	case 128:
		return 4;
	default:
		return 0;
	}

}

/**
 * pl330_to_burst_size_bits - conversion function from the burst
 *	size to the bit encoding of the CCR
 * @burst_size: The burst size. It's the data width. In terms of bytes,
 *	it could be 1, 2, 4, 8, 16, 32, 64, or 128. It must be no larger
 *	than the bus width. (We are using DMA assembly syntax.)
 *
 * Returns the burst size bit encoding for the CCR.
 */
inline unsigned pl330_to_burst_size_bits(unsigned burst_size)
{
	if (burst_size == 1)
		return 0;
	else if (burst_size == 2)
		return 1;
	else if (burst_size == 4)
		return 2;
	else if (burst_size == 8)
		return 3;
	else if (burst_size == 16)
		return 4;
	else if (burst_size == 32)
		return 5;
	else if (burst_size == 64)
		return 6;
	else if (burst_size == 128)
		return 7;
	else
		return 0;
}

/**
 * pl330_to_ccr_value - conversion function from PL330 bus transfer descriptors
 *	to CCR value. All the values passed to the functions are in terms
 *	of assembly languages, not in terms of the register bit encoding.
 * @src_bus_des: The source AXI bus descriptor.
 * @src_inc: Whether the source address should be increment.
 * @dst_bus_des: The destination AXI bus descriptor.
 * @dst_inc: Whether the destination address should be increment.
 * @endian_swap_size: Endian swap szie
 *
 * Returns the 32-bit CCR value.
 */
static u32 pl330_to_ccr_value(struct pl330_bus_des *src_bus_des,
		       unsigned src_inc,
		       struct pl330_bus_des *dst_bus_des,
		       unsigned dst_inc,
		       unsigned endian_swap_size)
{
	/*
	 * Channel Control Register encoding
	 * [31:28] - endian_swap_size
	 * [27:25] - dst_cache_ctrl
	 * [24:22] - dst_prot_ctrl
	 * [21:18] - dst_burst_len
	 * [17:15] - dst_burst_size
	 * [14]    - dst_inc
	 * [13:11] - src_cache_ctrl
	 * [10:8] - src_prot_ctrl
	 * [7:4]  - src_burst_len
	 * [3:1]  - src_burst_size
	 * [0]     - src_inc
	 */

	unsigned es = pl330_to_endian_swap_size_bits(endian_swap_size);

	unsigned dst_burst_size =
		pl330_to_burst_size_bits(dst_bus_des->burst_size);
	unsigned dst_burst_len = (dst_bus_des->burst_len - 1) & 0x0F;
	unsigned dst_cache_ctrl = (dst_bus_des->cache_ctrl & 0x03)
		| ((dst_bus_des->cache_ctrl & 0x08) >> 1);
	unsigned dst_prot_ctrl = dst_bus_des->prot_ctrl & 0x07;
	unsigned dst_inc_bit = dst_inc & 1;

	unsigned src_burst_size =
		pl330_to_burst_size_bits(src_bus_des->burst_size);
	unsigned src_burst_len = (src_bus_des->burst_len - 1) & 0x0F;
	unsigned src_cache_ctrl = (src_bus_des->cache_ctrl & 0x03)
		| ((src_bus_des->cache_ctrl & 0x08) >> 1);
	unsigned src_prot_ctrl = src_bus_des->prot_ctrl & 0x07;
	unsigned src_inc_bit = src_inc & 1;

	u32 ccr_value = (es << 28)
		| (dst_cache_ctrl << 25)
		| (dst_prot_ctrl << 22)
		| (dst_burst_len << 18)
		| (dst_burst_size << 15)
		| (dst_inc_bit << 14)
		| (src_cache_ctrl << 11)
		| (src_prot_ctrl << 8)
		| (src_burst_len << 4)
		| (src_burst_size << 1)
		| (src_inc_bit);

	pr_debug("CCR: es %x\n", es);
	pr_debug("CCR: dca %x, dpr %x, dbl %x, dbs %x, di %x\n",
			dst_cache_ctrl, dst_prot_ctrl, dst_burst_len,
			dst_burst_size, dst_inc_bit);
	pr_debug("CCR: sca %x, spr %x, sbl %x, sbs %x, si %x\n", src_cache_ctrl,
			src_prot_ctrl, src_burst_len,
			src_burst_size, src_inc_bit);

	return ccr_value;
}

/**
 * pl330_construct_single_loop - Construct a loop with only DMALD and DMAST
 *	as the body using loop counter 0. The function also makes sure the
 *	loop body and the lpend is in the same cache line.
 * @dma_prog_start: The very start address of the DMA program. This is used
 *	to calculate whether the loop is in a cache line.
 * @cache_length: The icache line length, in terms of bytes. If it's zero, the
 *	performance enhancement feature will be turned off.
 * @dma_prog_loop_start: The starting address of the loop (DMALP).
 * @loop_count: The inner loop count. Loop count - 1 will be used to initialize
 *	the loop counter.
 * Returns the number of bytes the loop has.
 */
static int pl330_construct_single_loop(char *dma_prog_start,
				int cache_length,
				char *dma_prog_loop_start,
				int loop_count)
{
	int cache_start_offset;
	int cache_end_offset;
	int num_nops;
	char *dma_prog_buf = dma_prog_loop_start;

	pr_debug("Contructing single loop: loop count %d\n", loop_count);

	dma_prog_buf += pl330_instr_dmalp(dma_prog_buf, 0, loop_count);

	if (cache_length > 0) {
		/*
		 * the cache_length > 0 switch is ued to turn on/off nop
		 * insertion
		 */
		cache_start_offset = dma_prog_buf - dma_prog_start;
		cache_end_offset = cache_start_offset + 3;

		/*
		 * check whether the body and lpend fit in one cache line
		 */
		if (cache_start_offset / cache_length
		    != cache_end_offset / cache_length) {
			/* insert the nops */
			num_nops = cache_length
				- cache_start_offset % cache_length;
			while (num_nops--) {
				dma_prog_buf +=
					pl330_instr_dmanop(dma_prog_buf);
			}
		}
	}

	dma_prog_buf += pl330_instr_dmald(dma_prog_buf);
	dma_prog_buf += pl330_instr_dmast(dma_prog_buf);
	dma_prog_buf += pl330_instr_dmalpend(dma_prog_buf,
					     dma_prog_buf - 2, 0);

	return dma_prog_buf - dma_prog_loop_start;
}

/**
 * pl330_construct_nested_loop - Construct a nested loop with only
 *	DMALD and DMAST in the inner loop body. It uses loop counter 1 for
 *	the outer loop and loop counter 0 for the inner loop.
 * @dma_prog_start: The very start address of the DMA program. This is used
 *	to caculate whether the loop is in a cache line.
 * @cache_length: The icache line length, in terms of bytes. If it's zero, the
 *	performance enhancement feture will be turned off.
 * @dma_prog_loop_start: The starting address of the loop (DMALP).
 * @loop_count_outer: The outer loop count. Loop count - 1 will be used to
 *	initialize the loop counter.
 * @loop_count_inner: The inner loop count. Loop count - 1 will be used to
 *	initialize the loop counter.
 */
static int pl330_construct_nested_loop(char *dma_prog_start,
				int cache_length,
				char *dma_prog_loop_start,
				unsigned int loop_count_outer,
				unsigned int loop_count_inner)
{
	int cache_start_offset;
	int cache_end_offset;
	int num_nops;
	char *inner_loop_start;
	char *dma_prog_buf = dma_prog_loop_start;

	pr_debug("Contructing nested loop outer %d, inner %d\n",
			loop_count_outer, loop_count_inner);

	dma_prog_buf += pl330_instr_dmalp(dma_prog_buf, 1, loop_count_outer);
	inner_loop_start = dma_prog_buf;

	if (cache_length > 0) {
		/*
		 * the cache_length > 0 switch is ued to turn on/off nop
		 * insertion
		 */
		if (cache_length < 8) {
			/*
			 * if the cache line is too small to fit both loops
			 * just align the inner loop
			 */
			dma_prog_buf +=
				pl330_construct_single_loop(dma_prog_start,
							    cache_length,
							    dma_prog_buf,
							    loop_count_inner);
			/* outer loop end */
			dma_prog_buf +=
				pl330_instr_dmalpend(dma_prog_buf,
						     inner_loop_start,
						     1);

			/*
			 * the nested loop is constructed for
			 * smaller cache line
			 */
			return dma_prog_buf - dma_prog_loop_start;
		}

		/*
		 * Now let's handle the case where a cache line can
		 * fit the nested loops.
		 */
		cache_start_offset = dma_prog_buf - dma_prog_start;
		cache_end_offset = cache_start_offset + 7;

		/*
		 * check whether the body and lpend fit in one cache line
		 */
		if (cache_start_offset / cache_length
		    != cache_end_offset / cache_length) {
			/* insert the nops */
			num_nops = cache_length
				- cache_start_offset % cache_length;
			while (num_nops--) {
				dma_prog_buf +=
					pl330_instr_dmanop(dma_prog_buf);
			}
		}
	}

	/* insert the inner DMALP */
	dma_prog_buf += pl330_instr_dmalp(dma_prog_buf, 0, loop_count_inner);

	/* DMALD and DMAST instructions */
	dma_prog_buf += pl330_instr_dmald(dma_prog_buf);
	dma_prog_buf += pl330_instr_dmast(dma_prog_buf);

	/* inner DMALPEND */
	dma_prog_buf += pl330_instr_dmalpend(dma_prog_buf,
					     dma_prog_buf - 2, 0);
	/* outer DMALPEND */
	dma_prog_buf += pl330_instr_dmalpend(dma_prog_buf,
					     inner_loop_start, 1);

	/* return the number of bytes */
	return dma_prog_buf - dma_prog_loop_start;
}

/*
 * prog_build_args - Stores arguments to build a DMA program.
 * @dma_prog_buf: DMA program buffer. It points to the starting address.
 * @dev_chan: Channel number of that device.
 * @dma_count: Number of bytes for the DMA request.
 * @src_addr: 32-bit srouce address
 * @src_bus_des: Source bus transaction descriptor, which includes the
 *	burst size, burst length, protection control, and cache control.
 * @src_inc: Source address is incremental.
 * @dst_addr:  32-bit destination address.
 * @dst_bus_des: Destination bus transation descriptor, whcih includes the
 *	burst size, burst length, protection control, and cache control.
 * @dst_inc: Destination address is incremental.
 * @src_is_mem: Source is memory buffer and the address is incremental.
 * @endian_swap_size: endian swap size, in bits, 8, 16, 32, 64, or 128.
 * @cache_length: The DMA instruction cache line length, in bytes. This
 *	is used to make sure the loop is in one cache line. If this argument
 *	is zero, the performance enhancement will be turned off.
 */
struct prog_build_args {
	unsigned int channel;
	char *dma_prog_buf;
	unsigned dev_chan;
	unsigned long dma_count;
	u32 src_addr;
	struct pl330_bus_des *src_bus_des;
	unsigned src_inc;
	u32 dst_addr;
	struct pl330_bus_des *dst_bus_des;
	unsigned dst_inc;
	unsigned int src_is_mem;
	unsigned endian_swap_size;
	unsigned cache_length;
};

/*
int pl330_build_dma_prog(unsigned int channel,
			 char *dma_prog_buf,
			 unsigned dev_chan,
			 unsigned long dma_count,
			 u32 src_addr,
			 struct pl330_bus_des *src_bus_des,
			 unsigned src_inc,
			 u32 dst_addr,
			 struct pl330_bus_des *dst_bus_des,
			 unsigned dst_inc,
			 unsigned int src_is_mem,
			 unsigned endian_swap_size,
			 unsigned cache_length)
*/
/**
 * pl330_build_dma_prog - Construct the DMA program based on the descriptions
 *	of the DMA transfer. The function handles memory to device and device
 *	to memory DMA transfers. It also handles unalgined head and small
 *	amount of residue tail.
 * @build_args: Instance of program build arguments. See the above struct
 *	definition for details.
 * Returns the number of bytes for the program.
 */
static int pl330_build_dma_prog(struct prog_build_args *build_args)
{
	/*
	 * unpack arguments
	 */
	unsigned int channel = build_args->channel;
	char *dma_prog_buf = build_args->dma_prog_buf;
	unsigned dev_chan = build_args->dev_chan;
	unsigned long dma_count = build_args->dma_count;
	u32 src_addr = build_args->src_addr;
	struct pl330_bus_des *src_bus_des = build_args->src_bus_des;
	unsigned src_inc = build_args->src_inc;
	u32 dst_addr = build_args->dst_addr;
	struct pl330_bus_des *dst_bus_des = build_args->dst_bus_des;
	unsigned dst_inc = build_args->dst_inc;
	unsigned int src_is_mem = build_args->src_is_mem;
	unsigned endian_swap_size = build_args->endian_swap_size;
	unsigned cache_length = build_args->cache_length;

	char *dma_prog_start = dma_prog_buf;
	unsigned int burst_bytes;
	unsigned int loop_count;
	unsigned int loop_count1 = 0;
	unsigned int loop_residue = 0;
	unsigned int tail_bytes;
	unsigned int tail_words;
	int dma_prog_bytes;
	u32 ccr_value;
	unsigned int unaligned;
	unsigned int unaligned_count;
	u32 mem_addr;
	int i;

	struct pl330_bus_des *mem_bus_des;

	/* for head and tail we just transfer in bytes */
	struct pl330_bus_des single_bus_des = {
		.burst_size = 1,
		.burst_len = 1,
	};

	struct pl330_bus_des single_transfer_des;


	/* insert DMAMOV for SAR and DAR */
	dma_prog_buf += pl330_instr_dmamov(dma_prog_buf,
					   PL330_MOV_SAR,
					   0);
	dma_prog_buf += pl330_instr_dmamov(dma_prog_buf,
					   PL330_MOV_DAR,
					   0);

	/* insert DMAMOV for SAR and DAR */
	dma_prog_buf += pl330_instr_dmamov(dma_prog_buf,
					   PL330_MOV_SAR,
					   src_addr);
	dma_prog_buf += pl330_instr_dmamov(dma_prog_buf,
					   PL330_MOV_DAR,
					   dst_addr);

	mem_bus_des = src_is_mem ? src_bus_des : dst_bus_des;
	mem_addr = src_is_mem ? src_addr : dst_addr;

	/* check whether the head is aligned or not */
	unaligned = mem_addr % mem_bus_des->burst_size;

	if (unaligned) {
		/* if head is unaligned, transfer head in bytes */
		unaligned_count = mem_bus_des->burst_size - unaligned;
		ccr_value = pl330_to_ccr_value(&single_bus_des, src_inc,
					       &single_bus_des, dst_inc,
					       endian_swap_size);
		dma_prog_buf += pl330_instr_dmamov(dma_prog_buf,
						   PL330_MOV_CCR,
						   ccr_value);

		pr_debug("unaligned head count %d\n", unaligned_count);
		for (i = 0; i < unaligned_count; i++) {
			dma_prog_buf += pl330_instr_dmald(dma_prog_buf);
			dma_prog_buf += pl330_instr_dmast(dma_prog_buf);
		}

		dma_count -= unaligned_count;
	}

	/* now the burst transfer part */
	ccr_value = pl330_to_ccr_value(src_bus_des,
				       src_inc,
				       dst_bus_des,
				       dst_inc,
				       endian_swap_size);
	dma_prog_buf += pl330_instr_dmamov(dma_prog_buf,
					   PL330_MOV_CCR,
					   ccr_value);

	burst_bytes = src_bus_des->burst_size * src_bus_des->burst_len;
	loop_count = dma_count / burst_bytes;
	tail_bytes = dma_count % burst_bytes;

	/*
	 * the loop count register is 8-bit wide, so if we need
	 * a larger loop, we need to have nested loops
	 */
	if (loop_count > 256) {
		loop_count1 = loop_count / 256;
		if (loop_count1 > 256) {
			pr_err("DMA operation cannot fit in a 2-level loop ");
			pr_cont("for channel %d, please reduce the ", channel);
			pr_cont("DMA length or increase the burst size or ");
			pr_cont("length");
			BUG();
			return 0;
		}
		loop_residue = loop_count % 256;

		pr_debug("loop count %d is greater than 256\n", loop_count);
		if (loop_count1 > 1)
			dma_prog_buf +=
				pl330_construct_nested_loop(dma_prog_start,
							    cache_length,
							    dma_prog_buf,
							    loop_count1,
							    256);
		else
			dma_prog_buf +=
				pl330_construct_single_loop(dma_prog_start,
							    cache_length,
							    dma_prog_buf,
							    256);

		/* there will be some that cannot be covered by
		 * nested loops
		 */
		loop_count = loop_residue;
	}

	if (loop_count > 0) {
		pr_debug("now loop count is %d\n", loop_count);
		dma_prog_buf += pl330_construct_single_loop(dma_prog_start,
							    cache_length,
							    dma_prog_buf,
							    loop_count);
	}

	if (tail_bytes) {
		/* handle the tail */
		tail_words = tail_bytes / mem_bus_des->burst_size;
		tail_bytes = tail_bytes % mem_bus_des->burst_size;

		if (tail_words) {
			pr_debug("tail words is %d\n", tail_words);
			/*
			 * if we can transfer the tail in words, we will
			 * transfer words as much as possible
			 */
			single_transfer_des.burst_size =
				mem_bus_des->burst_size;
			single_transfer_des.burst_len = 1;
			single_transfer_des.prot_ctrl =
				mem_bus_des->prot_ctrl;
			single_transfer_des.cache_ctrl =
				mem_bus_des->cache_ctrl;

			/*
			 * the burst length is 1
			 */
			ccr_value =
				pl330_to_ccr_value(&single_transfer_des,
						   src_inc,
						   &single_transfer_des,
						   dst_inc,
						   endian_swap_size);

			dma_prog_buf +=
				pl330_instr_dmamov(dma_prog_buf,
						   PL330_MOV_CCR,
						   ccr_value);
			dma_prog_buf +=
				pl330_construct_single_loop(dma_prog_start,
							    cache_length,
							    dma_prog_buf,
							    tail_words);

		}

		if (tail_bytes) {
			/*
			 * for the rest, we'll tranfer in bytes
			 */
			/*
			 * todo: so far just to be safe, the tail bytes
			 * are transfered in a loop. We can optimize a little
			 * to perform a burst.
			 */
			ccr_value =
				pl330_to_ccr_value(&single_bus_des, src_inc,
						   &single_bus_des, dst_inc,
						   endian_swap_size);
			dma_prog_buf +=
				pl330_instr_dmamov(dma_prog_buf,
						   PL330_MOV_CCR,
						   ccr_value);

			pr_debug("tail bytes is %d\n", tail_bytes);
			dma_prog_buf +=
				pl330_construct_single_loop(dma_prog_start,
							    cache_length,
							    dma_prog_buf,
							    tail_bytes);

		}
	}

	dma_prog_buf += pl330_instr_dmasev(dma_prog_buf, dev_chan);
	dma_prog_buf += pl330_instr_dmaend(dma_prog_buf);

	dma_prog_bytes = dma_prog_buf - dma_prog_start;

	return dma_prog_bytes;

}

/**
 * pl330_exec_dmakill - Use the debug registers to kill the DMA thread.
 * @dev_id: PL330 device ID indicating which PL330, the ID starts at 0.
 * @base: DMA device base address.
 * @dev_chan: DMA channel of the device.
 * @thread: Debug thread encoding. 0: DMA manager thread, 1: DMA channel.
 *
 * Returns 0 on success, -1 on time out
 */
static int pl330_exec_dmakill(unsigned int dev_id,
		       void __iomem *base,
		       unsigned int dev_chan,
		       unsigned int thread)
{
	u32 dbginst0;
	int wait_count;

	dbginst0 = PL330_DBGINST0(0, 0x01, dev_chan, thread);

	/* wait while debug status is busy */
	wait_count = 0;
	while (pl330_readreg(base, PL330_DBGSTATUS_OFFSET)
	       & PL330_DBGSTATUS_BUSY
	       && wait_count < PL330_MAX_WAIT)
		wait_count++;

	if (wait_count >= PL330_MAX_WAIT) {
		/* wait time out */
		pr_err("PL330 device %d debug status busy time out\n",
				dev_id);

		return -1;
	}

	/* write debug instruction 0 */
	pl330_writereg(dbginst0, base, PL330_DBGINST0_OFFSET);


	/* run the command in dbginst0 and dbginst1 */
	pl330_writereg(0, base, PL330_DBGCMD_OFFSET);

	return 0;
}

/**
 * pl330_init_channel_static_data - Initialize the pl330_channel_static_data
 *	struct.
 * @pdev_id: Device id.
 */
static void pl330_init_channel_static_data(unsigned int pdev_id)
{
	unsigned int i;
	struct pl330_device_data *dev_data = driver_data.device_data + pdev_id;
	struct pl330_channel_static_data *channel_static_data =
		driver_data.channel_static_data;

	for (i = dev_data->starting_channel;
	     i < dev_data->starting_channel + dev_data->channels;
	     i++) {
		channel_static_data[i].dev_id = pdev_id;
		channel_static_data[i].dev_chan =
			i - dev_data->starting_channel;
		channel_static_data[i].channel = i;
	}
}


/**
 * pl330_don_isr - Done interrup handler. One handler per channel.
 * @irq: Irq number
 * @dev: Pointer to the pl330_channel_static_data
 *
 * Returns IRQHANDLED
 */
static irqreturn_t pl330_done_isr(int irq, void *dev)
{

	struct pl330_channel_static_data *channel_static_data =
		(struct pl330_channel_static_data *)dev;
	unsigned int dev_chan = channel_static_data->dev_chan;
	unsigned int dev_id = channel_static_data->dev_id;
	unsigned int channel = channel_static_data->channel;

	struct pl330_device_data *device_data =
		driver_data.device_data + dev_id;
	struct dma_struct *dma_chan = driver_data.dma_chan + channel;
	struct pl330_channel_data *channel_data =
		driver_data.channel_data + channel;

	pr_debug("Entering PL330 Done irq on channel %d\n",
			channel_static_data->channel);
	/*
	 * clear channel interrupt status
	 */
	pl330_writereg(0x1 << dev_chan,
		       device_data->base,
		       PL330_INTCLR_OFFSET);

	/*
	 * Clear the count and active flag, and invoke the done callback.
	 */

	dma_chan->count = 0;

	dma_chan->active = 0;

	if (dma_chan->lock && channel_data->done_callback) {
		channel_data->done_callback(channel,
					    channel_data->done_callback_data);
	}

	pr_debug("Handled PL330 Done irq on channel %d\n",
			channel_static_data->channel);

	return IRQ_HANDLED;
}

/**
 * pl330_fault_isr - Done interrup handler. One handler per device.
 * @irq: Irq number
 * @dev: Pointer to the pl330_device_data struct of the device
 *
 * Returns IRQHANDLED
 */
static irqreturn_t pl330_fault_isr(int irq, void *dev)
{
	struct pl330_device_data *device_data =
		(struct pl330_device_data *)dev;
	void __iomem *base = device_data->base;
	struct pl330_channel_data *channel_data;
	struct dma_struct *dma_chan;

	unsigned int dev_id = device_data->dev_id;
	unsigned int dev_chan;
	unsigned int channel;

	u32 fsm; /* Fault status DMA manager register value */
	u32 fsc; /* Fault status DMA channel register value */
	u32 fault_type; /* Fault type DMA manager register value */

	u32 pc; /* DMA PC or channel PC */
	void *data; /* call back data */

	unsigned long spin_flags;

	pr_debug("Handling PL330 Fault irq on device %d\n", dev_id);

	fsm = pl330_readreg(base, PL330_FSM_OFFSET) & 0x01;
	fsc = pl330_readreg(base, PL330_FSC_OFFSET) & 0xFF;


	if (fsm) {
		/*
		 * if DMA manager is fault
		 */
		fault_type = pl330_readreg(base, PL330_FTM_OFFSET);
		pc = pl330_readreg(base, PL330_DPC_OFFSET);

		pr_err("PL330 device %d fault with type: %x at PC %x\n",
				device_data->dev_id, fault_type, pc);

		/* kill the DMA manager thread */
		spin_lock_irqsave(&device_data->lock, spin_flags);
		pl330_exec_dmakill(dev_id, base, 0, 0);
		spin_unlock_irqrestore(&device_data->lock, spin_flags);
	}

	/*
	 * check which channel faults and kill the channel thread
	 */
	for (dev_chan = 0; dev_chan < device_data->channels; dev_chan++) {
		if (fsc & (0x01 << dev_chan)) {
			pr_debug("pl330_fault_isr: channel %d device %d\n",
					dev_chan, device_data->dev_id);
			fault_type =
				pl330_readreg(base,
					      PL330_FTCn_OFFSET(dev_chan));
			pc = pl330_readreg(base, PL330_CPCn_OFFSET(dev_chan));
			pr_debug("pl330_fault_isr: fault type %#x pc %#x\n",
					fault_type, pc);

			/* kill the channel thread */
			pr_debug("pl330_fault_isr: killing channel ch:%d id:%d",
				dev_chan, device_data->dev_id);
			spin_lock_irqsave(&device_data->lock, spin_flags);
			pl330_exec_dmakill(dev_id, base, dev_chan, 1);
			spin_unlock_irqrestore(&device_data->lock, spin_flags);

			/*
			 * get the fault type and fault pc and invoke the
			 * fault callback.
			 */
			channel = device_data->starting_channel + dev_chan;
			dma_chan = driver_data.dma_chan + channel;
			channel_data = driver_data.channel_data + channel;

			dma_chan->active = 0;

			data = channel_data->fault_callback_data;
			if (dma_chan->lock && channel_data->fault_callback)
				channel_data->fault_callback(channel,
							     fault_type,
							     pc,
							     data);

		}
	}

	return IRQ_HANDLED;
}

/**
 * pl330_request_irq - set up the interrupt handler for the corresponding
 *	device. It sets up all the interrupt for all the channels of that
 *	device. It also sets the the fault interrupt handler for the device.
 * @dev_id: device id.
 *
 * Returns 0 on success, otherwise on failure
 */
static int pl330_request_irq(unsigned int dev_id)
{
	unsigned int irq;
	unsigned int irq2;

	struct pl330_channel_static_data *channel_static_data;
	struct pl330_device_data *device_data =
		driver_data.device_data + dev_id;

	int status;

	pr_debug("PL330 requesting irq for device %d\n", dev_id);

	channel_static_data = driver_data.channel_static_data
		+ device_data->starting_channel;

	irq = device_data->fault_irq;

	/* set up the fault irq */
	status = request_irq(irq, pl330_fault_isr,
			     IRQF_DISABLED, DRIVER_NAME, device_data);

	if (status) {
		pr_err("PL330 request fault irq %d failed %d\n",
				irq, status);
		return -1;
	} else {
		pr_debug("PL330 request fault irq %d successful\n", irq);
	}


	for (irq = device_data->starting_irq;
	     irq != 0 && irq <= device_data->ending_irq; irq++) {

		/* set up the done irq */
		status = request_irq(irq, pl330_done_isr,
				     IRQF_DISABLED, DRIVER_NAME,
				     channel_static_data);

		if (status) {
			pr_err("PL330 request done irq %d failed %d\n",
					irq, status);
			goto req_done_irq_failed;
		} else {
			channel_static_data->irq = irq;

			pr_debug("PL330 request done irq %d successful\n", irq);
		}

		channel_static_data++;
	}

	for (irq = device_data->starting_irq1;
	     irq != 0 && irq <= device_data->ending_irq1; irq++) {

		/* set up the done irq */
		status = request_irq(irq, pl330_done_isr,
				     IRQF_DISABLED, DRIVER_NAME,
				     channel_static_data);

		if (status) {
			pr_err("PL330 request done irq %d failed %d\n",
					irq, status);
			goto req_done_irq1_failed;
		} else {
			channel_static_data->irq = irq;

			pr_debug("PL330 request done irq %d successful\n", irq);
		}

		channel_static_data++;
	}

	return 0;

 req_done_irq1_failed:
	for (irq2 = device_data->starting_irq1;
	     irq2 < irq; irq2++)
		free_irq(irq2, channel_static_data);

	irq = device_data->ending_irq + 1;

 req_done_irq_failed:
	for (irq2 = device_data->starting_irq;
	     irq2 < irq; irq2++)
		free_irq(irq2, channel_static_data);

	free_irq(device_data->fault_irq, channel_static_data);

	return -1;
}

/**
 * pl330_free_irq - Free the requested interrupt for the device
 * @dev_id: device id.
 */
static void pl330_free_irq(unsigned int dev_id)
{
	unsigned int irq;
	int i;

	struct pl330_channel_static_data *channel_static_data;
	struct pl330_device_data *device_data =
		driver_data.device_data + dev_id;

	pr_debug("PL330 freeing irq for device %d\n", dev_id);

	channel_static_data = driver_data.channel_static_data
		+ device_data->starting_channel;

	for (i = 0; i < device_data->channels; i++) {

		irq = channel_static_data->irq;

		/* free the done irq */
		free_irq(irq, channel_static_data);

		channel_static_data++;
	}

	irq = device_data->fault_irq;

	/* free the fault irq */
	free_irq(irq, device_data);

	return;
}

/**
 * pl330_init_device_data - Initialize pl330_device_data struct instance.
 * @dev_id: Device id
 * @pdev: Instance of platform_device struct.
 */
static void pl330_init_device_data(unsigned int dev_id,
			    struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct pl330_platform_config *pl330_config;

	u32 cfg_reg;
	u32 value;

	u32 pid;
	u32 cid;

	int i;

	struct pl330_device_data *device_data =
		driver_data.device_data + dev_id;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			    "get_resource for MEM resource for dev %d failed\n",
			    dev_id);
		return;
	} else {
		pr_debug("pl330 device %d actual base is %x\n",
				dev_id, (unsigned int)res->start);
	}

	if (!request_mem_region(res->start, 0x1000, "pl330")) {
		dev_err(&pdev->dev, "memory request failue for base %x\n",
		       (unsigned int)res->start);
		return;
	}

	spin_lock_init(&device_data->lock);

	device_data->base = ioremap(res->start, SZ_4K);
	pr_debug("pl330 dev %d ioremap to %#x\n", dev_id,
			(unsigned int)device_data->base);
	if (!device_data->base) {
		dev_err(&pdev->dev, "ioremap failure for base %#x\n",
				(unsigned int)res->start);
		release_mem_region(res->start, SZ_4K);
		return;
	}
	pr_debug("virt_to_bus(base) is %#08x\n",
			(u32)virt_to_bus(device_data->base));
	pr_debug("page_to_phys(base) is %#08x\n",
			(u32)page_to_phys(virt_to_page(device_data->base)));

	for (pid = 0, i = 0; i < 4; i++)
		pid |= (pl330_readreg(device_data->base, 0xFE0 + i * 4) & 0xFF)
			<< (i * 8);
	pr_debug("Periperal ID is %#08x\n", pid);

	for (cid = 0, i = 0; i < 4; i++)
		cid |= (pl330_readreg(device_data->base, 0xFF0 + i * 4) & 0xFF)
			<< (i * 8);
	pr_debug("PrimeCell ID is %#08x\n", cid);

	/* store the PL330 id. The device id starts from zero.
	 * The last one is MAX_DMA_DEVICES - 1
	 */
	device_data->dev_id = dev_id;

	/* store the device instance */
	device_data->dev = dev;

	/* now set up the channel configurations */
	pl330_config = (struct pl330_platform_config *)dev->platform_data;
	device_data->channels = pl330_config->channels;
	device_data->starting_channel = pl330_config->starting_channel;
	pr_debug("pl330 device %d starting channel %d, channels %d\n", dev_id,
			device_data->starting_channel, device_data->channels);

	/* now get the irq configurations */

	/* The 1st IRQ resource is for fault irq */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res)
		dev_err(&pdev->dev,
			    "get_resource for IRQ resource for dev %d failed\n",
			    dev_id);

	if (res->start != res->end)
		dev_err(&pdev->dev, "the first IRQ resource for dev %d should "
		       "be a single IRQ for FAULT\n", dev_id);
	device_data->fault_irq = res->start;

	/* The 2nd IRQ resource is for 1st half of channel IRQ */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (!res) {
		dev_err(&pdev->dev,
			 "get_resource for IRQ resource %d for dev %d failed\n",
			 1, dev_id);

		device_data->starting_irq = 0;
		device_data->ending_irq = 0;
	} else {
		device_data->starting_irq = res->start;
		device_data->ending_irq = res->end;
	}

	pr_debug("pl330 device %d 1st half starting irq %d, ending irq %d\n",
			dev_id, device_data->starting_irq,
			device_data->ending_irq);

	/* The 3rd IRQ resource is for 2nd half of channel IRQ */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 2);
	if (!res) {
		dev_err(&pdev->dev,
			 "get_resource for IRQ resource %d for dev %d failed\n",
			 2, dev_id);
		device_data->starting_irq1 = 0;
		device_data->ending_irq1 = 0;
	} else {
		device_data->starting_irq1 = res->start;
		device_data->ending_irq1 = res->end;
	}

	pr_debug("pl330 device %d 2nd half starting irq %d, ending irq %d\n",
			dev_id, device_data->starting_irq1,
			device_data->ending_irq1);

#ifdef PL330_OPTIMIZE_ICACHE
	/*
	 * This option optimizes the DMA program based on the PL330 icache
	 * line size. When generating the DMA program, the loop body should
	 * be in on cache line to have a better cache performance.
	 */
	cfg_reg = pl330_readreg(device_data->base, PL330_CR1_OFFSET);
	value = cfg_reg & PL330_CR1_I_CACHE_LEN_MASK;
	if (value < 2 || value > 5)
		value = 0;
	else
		value = 1 << value;

	device_data->i_cache_len = value;
#else
	device_data->i_cache_len = 0;
#endif
}


/**
 * pl330_setspeed_dma - Implementation of set_dma_speed. The function actually
 *	doesn't do anything.
 * @channel: DMA channel number, from 0 to MAX_DMA_CHANNELS - 1
 * @indexed_dma_chan: Instance of dma_struct for the channel.
 * @cycle_ns: DMA speed.
 *
 * Returns 0 on success.
 */
static int pl330_setspeed_dma(unsigned int channel,
			      struct dma_struct *indexed_dma_chan,
			      int cycle_ns)
{
	pr_debug("PL330::pl330_setspeed_dma(), doing nothing\n");
	return 0;
}

/**
 * pl330_get_residue_dma - Impementation of get_dma_residue.
 * @channel: DMA channel number, from 0 to MAX_DMA_CHANNELS - 1
 * @indexed_dma_chan: Instance of dma_struct for the channel.
 *
 * Returns: full count or 0. No partial values in this DMAC;
 */
static int pl330_get_residue_dma(unsigned int channel,
				 struct dma_struct *indexed_dma_chan)
{
	return indexed_dma_chan->count;
}

/**
 * pl330_request_dma - Implementation of request_dma.
 *	Is this channel one of those allowed for the requesting device
 *	- platform data defines which channels are for which devices
 *	Requesting device name from driver stored in indexed_channel
 *	We want the hardware bus id to match to a channel and the mode
 *	to be correct.
 * @channel: DMA channel number, from 0 to MAX_DMA_CHANNELS - 1
 * @indexed_dma_chan: Instance of dma_struct for the channel.
 *
 * Returns 0 on success
 */
static int pl330_request_dma(unsigned int channel, dma_t *indexed_dma_chan)
{
	/*
	 * the skeleton request_dma keeps track of which channel is busy.
	 * so in here we only need to some sanity check to see the client data
	 * is freed or not
	 */

	/*
	 * we still need to clear the channel data
	 */
	struct pl330_channel_data *channel_data =
		driver_data.channel_data + channel;

	pr_debug("PL330::pl330_request_dma() ...\n");

	memset(channel_data, 0, sizeof(struct pl330_channel_data));

	return 0;
}

/**
 * pl330_free_dma - Implementation of free_dma.
 * @channel: DMA channel number, from 0 to MAX_DMA_CHANNELS - 1
 * @indexed_dma_chan: Instance of dma_struct for the channel.
 *
 */
static void pl330_free_dma(unsigned int channel, dma_t *indexed_dma_chan)
{
	/*
	 * we need to free client data
	 */
	struct pl330_channel_data *channel_data =
		driver_data.channel_data + channel;
	unsigned dev_id = driver_data.channel_static_data[channel].dev_id;
	struct pl330_device_data *device_data =
		driver_data.device_data + dev_id;

	channel_data->client_data = NULL;

	if (channel_data->dma_prog_buf) {
		/* release the program buffer */
		dma_free_coherent(device_data->dev, 0x1000,
				  channel_data->dma_prog_buf,
				  channel_data->dma_prog_phy);

		channel_data->dma_prog_buf = NULL;
		channel_data->dma_prog_phy = 0;
	}

	return;
}

/**
 * print_pl330_bus_des - Debugging utility to print a pl330_bus_des struct
 * @bus_des: Pointer to the pl330_bus_des struct.
 */
#ifdef PL330_DEBUG
static void print_pl330_bus_des(struct pl330_bus_des *bus_des)
{

	if (!bus_des) {
		pr_debug("NULL\n");
		return;
	}

	pr_debug("  .burst_size = %d\n", bus_des->burst_size);
	pr_debug("  .burst_len = %d\n", bus_des->burst_len);
	pr_debug("  .prot_ctrl = %d\n", bus_des->prot_ctrl);
	pr_debug("  .cache_ctrl = %d\n", bus_des->cache_ctrl);

	return;
}
#else
#	define print_pl330_bus_des(bus_des)
#endif

/**
 * pl33_exec_dmago - Execute the DMAGO to start a channel.
 * @dev_id: PL330 device ID indicating which PL330, the ID starts at 0.
 * @base: PL330 device base address
 * @dev_chan: Channel number for the device
 * @dma_prog: DMA program starting address, this should be DMA address
 *
 * Returns 0 on success, -1 on time out
 */
static int pl330_exec_dmago(unsigned int dev_id,
		      void __iomem *base,
		      unsigned int dev_chan,
		      u32 dma_prog)
{
	char dma_go_prog[8];
	u32 dbginst0;
	u32 dbginst1;

	int wait_count;

	pr_debug("pl330_exec_dmago: entering\n");

	pl330_instr_dmago(dma_go_prog, dev_chan, dma_prog, 0);

	dbginst0 = PL330_DBGINST0(*(dma_go_prog + 1), *dma_go_prog, 0, 0);
	dbginst1 = (u32)dma_prog;

	pr_debug("inside pl330_exec_dmago: base %x, dev_chan %d, dma_prog %x\n",
			(u32)base, dev_chan, dma_prog);

	/* wait while debug status is busy */
	wait_count = 0;
	while (pl330_readreg(base, PL330_DBGSTATUS_OFFSET)
	       & PL330_DBGSTATUS_BUSY
	       && wait_count < PL330_MAX_WAIT) {
		pr_debug("dbgstatus %x\n",
				pl330_readreg(base, PL330_DBGSTATUS_OFFSET));

		wait_count++;
	}

	if (wait_count >= PL330_MAX_WAIT) {
		pr_err("PL330 device %d debug status busy time out\n", dev_id);
		return -1;
	}

	pr_debug("dbgstatus idle\n");

	/* write debug instruction 0 */
	pl330_writereg(dbginst0, base, PL330_DBGINST0_OFFSET);
	/* write debug instruction 1 */
	pl330_writereg(dbginst1, base, PL330_DBGINST1_OFFSET);


	/* wait while the DMA Manager is busy */
	wait_count = 0;
	while ((pl330_readreg(base, PL330_DS_OFFSET) & PL330_DS_DMA_STATUS)
	       != PL330_DS_DMA_STATUS_STOPPED
	       && wait_count <= PL330_MAX_WAIT) {
		pr_debug("ds %x\n", pl330_readreg(base, PL330_DS_OFFSET));
		wait_count++;
	}

	if (wait_count >= PL330_MAX_WAIT) {
		pr_err("PL330 device %d debug status busy time out\n", dev_id);
		return -1;
	}

	/* run the command in dbginst0 and dbginst1 */
	pl330_writereg(0, base, PL330_DBGCMD_OFFSET);
	pr_debug("pl330_exec_dmago done\n");

	return 0;
}

/**
 * pl330_enable_dma - Implementation of enable_dma. It translates the
 *	DMA parameters to a DMA program if the DMA program is not provided,
 *	then starts the DMA program on a channel thread.
 * @channel: DMA channel number
 * @indexed_dma_chan: Instance of the dma_struct.
 */
static void pl330_enable_dma(unsigned int channel,
			     struct dma_struct *indexed_dma_chan)
{
	struct dma_struct *dma = indexed_dma_chan;
	struct pl330_channel_data *channel_data;
	struct pl330_channel_static_data *channel_static_data;
	struct pl330_client_data *client_data;
	struct pl330_device_data *device_data;

	unsigned int dev_chan;

	struct pl330_bus_des *src_bus_des = NULL;
	struct pl330_bus_des *dst_bus_des = NULL;

	struct default_src_bus_des;
	struct default_dst_bus_des;

	unsigned src_inc = 1;
	unsigned dst_inc = 1;

	u32 src_addr;
	u32 dst_addr;

	u32 dma_prog;
	char *dma_prog_buf;

	int dma_prog_bytes;

	u32 inten;

	unsigned long spin_flags;

	struct prog_build_args build_args;

	channel_static_data = driver_data.channel_static_data + channel;
	device_data = driver_data.device_data + channel_static_data->dev_id;
	channel_data = driver_data.channel_data + channel;
	client_data = driver_data.channel_data[channel].client_data;

	if (!client_data) {
		pr_err("client data is not set for DMA channel %d\n", channel);
		BUG();
		return;
	}

	/*
	 * find out which one is source which one is destination
	 */
	if (dma->dma_mode == DMA_MODE_READ) {
		pr_debug("dma_mode is DMA_MODE_READ\n");

		src_bus_des = &client_data->dev_bus_des;
		dst_bus_des = &client_data->mem_bus_des;

		src_addr = (u32)client_data->dev_addr;
		dst_addr = (u32)virt_to_bus(dma->addr);

		src_inc = channel_data->incr_dev_addr;
		dst_inc = 1;
	} else if (dma->dma_mode == DMA_MODE_WRITE) {
		pr_debug("dma_mode is DMA_MODE_WRITE\n");

		src_bus_des = &client_data->mem_bus_des;
		dst_bus_des = &client_data->dev_bus_des;

		src_addr = (u32)virt_to_bus(dma->addr);
		dst_addr = (u32)client_data->dev_addr;
		src_inc = 1;
		dst_inc = channel_data->incr_dev_addr;
	} else {
		pr_err("Error: mode %x is not supported\n", dma->dma_mode);

		return;
	}

	if (dma->count == 0) {
		pr_err("Error: DMA count for channel %d is zero", channel);
		return;
	}

	/* print some debugging messages */
	pr_debug("count is %ld\n", dma->count);

	pr_debug("dev_addr = %x\n", (unsigned int)client_data->dev_addr);

	pr_debug("dev_bus_des = {\n");
	print_pl330_bus_des(&client_data->dev_bus_des);
	pr_debug("}\n");

	pr_debug("mem_bus_des = {\n");
	print_pl330_bus_des(&client_data->mem_bus_des);
	pr_debug("}\n");

	pr_debug("endian_swap_size = %d\n", client_data->endian_swap_size);
	pr_debug("incr_dev_addr = %d\n", channel_data->incr_dev_addr);

	dma_prog = channel_data->dma_program;

	dev_chan = channel_static_data->dev_chan;

	if (dma_prog == 0) {
		/*
		 * if the DMA program is not set by a user,
		 * construct the dma program
		 */
		pr_debug("constructing DMA program\n");
		if (!channel_data->dma_prog_buf) {
			/* allocate the dma prog buffer */
			channel_data->dma_prog_buf =
				dma_alloc_coherent(device_data->dev,
						   0x1000,
						   &channel_data->dma_prog_phy,
						   GFP_KERNEL);
		}
		pr_debug("channel %d DMA program: vir %#08x, phy %#08x\n",
				channel, (u32)channel_data->dma_prog_buf,
				(u32)channel_data->dma_prog_phy);

		dma_prog_buf = (char *)channel_data->dma_prog_buf;

		/*
		 * setup the arguments
		 */
		build_args.channel = channel;
		build_args.dma_prog_buf = dma_prog_buf;
		build_args.dev_chan = dev_chan;
		build_args.dma_count = dma->count;
		build_args.src_addr = src_addr;
		build_args.src_bus_des = src_bus_des;
		build_args.src_inc = src_inc;
		build_args.dst_addr = dst_addr;
		build_args.dst_bus_des = dst_bus_des;
		build_args.dst_inc = dst_inc;
		build_args.src_is_mem = dma->dma_mode == DMA_MODE_WRITE;
		build_args.endian_swap_size = client_data->endian_swap_size;
		build_args.cache_length = device_data->i_cache_len;

		dma_prog_bytes = pl330_build_dma_prog(&build_args);

		/*
		 * using physical address for DMA prog
		 */
		dma_prog = channel_data->dma_prog_phy;

		channel_data->dma_prog_len = dma_prog_bytes;

		pr_debug("DMA program constructed\n");
	} else {
		pr_debug("channel %d user defined DMA program %#08x\n", channel,
				(u32)dma_prog);
	}

	pr_debug("enable_dma: spin_lock_irqsave\n");
	spin_lock_irqsave(&device_data->lock, spin_flags);

	/* enable the interrupt */
	pr_debug("enable_dma: enabling interrupt\n");
	inten = pl330_readreg(device_data->base, PL330_INTEN_OFFSET);
	inten |= 0x01 << dev_chan; /* set the correpsonding bit */
	pl330_writereg(inten, device_data->base, PL330_INTEN_OFFSET);
	pr_debug("pl330 interrupt enabled for channel %d\n", channel);

	pl330_exec_dmago(device_data->dev_id,
			 device_data->base,
			 dev_chan,
			 dma_prog);

	spin_unlock_irqrestore(&device_data->lock, spin_flags);

	return;
}

/**
 * pl330_disable_dma - Implementation of disable_dma. If the channel is active,
 *	kill the DMA channel thread.
 * @channel: DMA channel number
 * @indexed_dma_chan: Instance of the dma_struct.
 */
static void pl330_disable_dma(unsigned int channel,
			      struct dma_struct *indexed_dma_chan)
{
	struct dma_struct *dma = indexed_dma_chan;

	struct pl330_channel_static_data *channel_static_data =
		driver_data.channel_static_data + channel;

	struct pl330_device_data *device_data =
		driver_data.device_data + channel_static_data->dev_id;

	void __iomem *base = device_data->base;

	unsigned int dev_chan = channel_static_data->dev_chan;
	unsigned int dev_id = channel_static_data->dev_id;

	u32 inten;

	unsigned long spin_flags;

	spin_lock_irqsave(&device_data->lock, spin_flags);

	if (pl330_readreg(base, PL330_CS0_OFFSET + dev_chan * 0x08)
	    & PL330_CS_ACTIVE_MASK) {
		/* channel is not stopped */
		pl330_exec_dmakill(dev_id, base, dev_chan, 1);
	}

	/* disable the interrupt */
	inten = pl330_readreg(device_data->base, PL330_INTEN_OFFSET);
	inten &= ~(0x01 << dev_chan); /* clear the correpsonding bit */
	pl330_writereg(inten, device_data->base, PL330_INTEN_OFFSET);

	spin_unlock_irqrestore(&device_data->lock, spin_flags);

	dma->count = 0;

	return;
}

/*
 * Platform bus binding
 */
static struct dma_ops pl330_ops = {
	.request     = pl330_request_dma,
	.free        = pl330_free_dma,
	.enable      = pl330_enable_dma,
	.disable     = pl330_disable_dma,
	.setspeed    = pl330_setspeed_dma,
	.residue     = pl330_get_residue_dma,
	.type        = "PL330",
};

static void pl330_set_default_burst_size(unsigned int dev_id)
{
#ifndef PL330_DEFAULT_BURST_SIZE
	u32 crdn = pl330_readreg(driver_data.device_data[dev_id].base,
				 PL330_CRDN_OFFSET);
	unsigned int default_burst_size;
	switch (crdn & 0x03) {
	case 2:
		/* 4 bytes 32-bit */
		default_burst_size = 4;
		break;
	case 3:
		/* 8 bytes 64-bit */
		default_burst_size = 8;
		break;
	case 4:
		/* 16 bytes 128-bit */
		default_burst_size = 16;
		break;
	default:
		/* 4 bytes 32-bit */
		default_burst_size = 4;
	}
	driver_data.device_data[dev_id].default_burst_size =
		default_burst_size;
#else
	driver_data.device_data[dev_id].default_burst_size =
		PL330_DEFAULT_BURST_SIZE;
#endif
}

/*
 * pl330_release_io - iounmap the base and release the memory region
 * @pdev: Pointer to the platform device structure
 * @dev_id: device id, starting 0
 */
static void pl330_release_io(struct platform_device *pdev, int dev_id)
{
	struct resource *res;

	struct pl330_device_data *device_data;

	device_data = driver_data.device_data + dev_id;
	if (device_data->base)
		iounmap(device_data->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		dev_err(&pdev->dev,
			    "get_resource for MEM resource for dev %d failed\n",
			    dev_id);

	if (res)
		release_mem_region(res->start, SZ_4K);

}

/**
 * pl330_platform_probe - Platform driver probe
 * @pdev: Pointer to the platform device structure
 *
 * Returns 0 on success, negative error otherwise
 */
static int __devinit pl330_platform_probe(struct platform_device *pdev)
{
	int pdev_id;

	if (!pdev) {
		dev_err(&pdev->dev, "pl330 probe called with NULL param.\n");
		return -ENODEV;
	}

	pr_debug("pl330 driver probing dev_id %d\n", pdev->id);

	pdev_id = 0;
	if (pdev->id < 0) {
		pdev_id = 0;
	} else if (pdev->id < MAX_DMA_DEVICES) {
		pdev_id = pdev->id;
	} else {
		dev_err(&pdev->dev,
			"pl330 device id exceeds the supported number.\n");
		return -ENODEV;
	}

	pl330_init_device_data(pdev_id, pdev);

	/* assume the init_device_data is invoked before this point */
	pl330_init_channel_static_data(pdev_id);

	/* setup the default burst size */
	pl330_set_default_burst_size(pdev_id);

	/* request irq */
	if (pl330_request_irq(pdev_id)) {
		pl330_release_io(pdev, pdev_id);
		return -1;
	}

	dev_info(&pdev->dev, "pl330 dev %d probe success\n", pdev->id);

	return 0;
}


/**
 * pl330_platform_remove - called when the platform driver is unregistered
 * @pdev: Pointer to the platform device structure
 *
 * Returns 0 on success, negative error otherwise
 */
static int pl330_platform_remove(struct platform_device *pdev)
{
	int pdev_id;

	if (!pdev) {
		dev_err(&pdev->dev, "pl330 remove called with NULL param.\n");
		return -ENODEV;
	}

	pr_debug("pl330 driver removing %d\n", pdev->id);

	pdev_id = 0;
	if (pdev->id < 0) {
		pdev_id = 0;
	} else if (pdev->id < MAX_DMA_DEVICES) {
		pdev_id = pdev->id;
	} else {
		dev_err(&pdev->dev,
			"pl330 device id exceeds the supported number.\n");
		return -ENODEV;
	}


	pl330_free_irq(pdev_id);

	pl330_release_io(pdev, pdev_id);

	return 0;
}


static struct platform_driver pl330_platform_driver = {
	.probe = pl330_platform_probe,
	.remove = pl330_platform_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

/**
 * pl330_driver_init - Initialize the dma_struct array and store the pointer
 *	to array
 */
static void pl330_driver_init(void)
{
	unsigned int i;

	pr_debug("inside pl330_driver_init, dma_chan is %x\n",
	       (unsigned int)dma_chan);

	driver_data.dma_chan = dma_chan;

	memset(dma_chan, 0, sizeof(dma_chan[MAX_DMA_CHANNELS]));

	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		dma_chan[i].d_ops = &pl330_ops;
		isa_dma_add(i, dma_chan + i);
	}

}

/*
 * setup_default_bus_des - Setup default bus descriptor. User may only
 *	set certain fields of a bus descriptor. This function sets the rest
 *	to be the default values.
 * @default_burst_size: Default burst size
 * @user_bus_des: User bus descriptor
 * @default_bus_des: Default bus decriptor, this is the returned value
 */
static void setup_default_bus_des(unsigned int default_burst_size,
			   struct pl330_bus_des *user_bus_des,
			   struct pl330_bus_des *default_bus_des)
{
	if (user_bus_des->burst_size == 0)
		default_bus_des->burst_size = default_burst_size;
	else
		default_bus_des->burst_size = user_bus_des->burst_size;

	if (user_bus_des->burst_len == 0)
		default_bus_des->burst_len = 1;
	else
		default_bus_des->burst_len = user_bus_des->burst_len;

	default_bus_des->prot_ctrl = user_bus_des->prot_ctrl;
	default_bus_des->cache_ctrl = user_bus_des->cache_ctrl;
}

/**
 * set_pl330_client_data - Associate an instance of struct pl330_client_data
 *	with a DMA channel.
 * @channel: DMA channel number.
 * @client_data: instance of the struct pl330_client_data.
 * Returns 0 on success, -EINVAL if the channel number is out of range,
 *	-ACCESS if the channel has not been allocated.
 */

int set_pl330_client_data(unsigned int channel,
			  struct pl330_client_data *client_data)
{
	struct dma_struct *dma = driver_data.dma_chan + channel;
	struct pl330_bus_des *mem_bus_des;
	struct pl330_bus_des *dev_bus_des;
	struct pl330_device_data *device_data;
	struct pl330_channel_data *channel_data;
	struct pl330_client_data *default_client_data;

	if (channel >= MAX_DMA_CHANNELS)
		return -EINVAL;

	if (!dma->lock) {
		pr_err("trying to set pl330_client_data on a free channel %d\n",
				channel);
		return -EINVAL;
	}

	if (dma->active) {
		pr_err("trying to set pl330_client_data on an active channel ");
		pr_cont("%d\n", channel);
		return -EBUSY;
	}

	channel_data = driver_data.channel_data + channel;

	if (client_data->mem_bus_des.burst_size == 0
	    || client_data->mem_bus_des.burst_len == 0
	    || client_data->dev_bus_des.burst_size == 0
	    || client_data->dev_bus_des.burst_len == 0) {
		device_data = driver_data.device_data +
			driver_data.channel_static_data[channel].dev_id;
		default_client_data = &channel_data->default_client_data;

		setup_default_bus_des(device_data->default_burst_size,
				      &client_data->mem_bus_des,
				      &default_client_data->mem_bus_des);
		setup_default_bus_des(device_data->default_burst_size,
				      &client_data->dev_bus_des,
				      &default_client_data->dev_bus_des);

		default_client_data->dev_addr = client_data->dev_addr;
		default_client_data->endian_swap_size =
			client_data->endian_swap_size;


		client_data = default_client_data;
	}

	mem_bus_des = &client_data->mem_bus_des;
	dev_bus_des = &client_data->dev_bus_des;

	if (dev_bus_des->burst_size * dev_bus_des->burst_len
	    != mem_bus_des->burst_size * mem_bus_des->burst_len) {
		pr_err("DMA channel %d has unmatched burst for ", channel);
		pr_cont("device and memory, device burst %d bytes, ",
			      dev_bus_des->burst_size * dev_bus_des->burst_len);
		pr_cont("memory burst %d bytes\n",
			      mem_bus_des->burst_size * mem_bus_des->burst_len);
		return -EINVAL;
	}

	driver_data.channel_data[channel].client_data = client_data;

	return 0;

}
EXPORT_SYMBOL(set_pl330_client_data);


/**
 * set_pl330_dma_prog_addr - Associate a DMA program with a DMA channel.
 * @channel: DMA channel number.
 * @start_address: DMA program starting address
 * Returns 0 on success, -EINVAL if the channel number is out of range,
 *	-ACCESS if the channel has not been allocated.
 */
int set_pl330_dma_prog_addr(unsigned int channel,
			    u32 start_address)
{
	struct dma_struct *dma = driver_data.dma_chan + channel;

	if (channel >= MAX_DMA_CHANNELS)
		return -EINVAL;

	if (!dma->lock) {
		pr_err("trying to set pl330_dma_program on a free channel %d\n",
				channel);
		return -EINVAL;
	}

	if (dma->active) {
		pr_err("trying to set pl330_dma_program on an active channel ");
		pr_cont("%d\n", channel);
		return -EBUSY;
	}

	driver_data.channel_data[channel].dma_program = start_address;


	return 0;
}
EXPORT_SYMBOL(set_pl330_dma_prog_addr);

/**
 * get_pl330_dma_program - Get the constructed DMA program.
 * @channel: DMA channel number.
 * @bytes: the number of bytes is stored in the location this argument
 *	points to.
 *
 * Returns the starting address of the DMA program the channel uses.
 */
char *get_pl330_dma_program(unsigned int channel,
			    unsigned int *bytes)
{
	struct dma_struct *dma = driver_data.dma_chan + channel;

	if (channel >= MAX_DMA_CHANNELS)
		return NULL;

	if (!dma->lock) {
		pr_err("trying to set pl330_dma_program on a free channel %d\n",
				channel);
		return NULL;
	}

	*bytes = driver_data.channel_data[channel].dma_prog_len;

	if (driver_data.channel_data[channel].dma_program)
		return (char *)driver_data.channel_data[channel].dma_program;
	else
		return (char *)driver_data.channel_data[channel].dma_prog_buf;
}
EXPORT_SYMBOL(get_pl330_dma_program);

/**
 * set_pl330_done_callback - Associate a DMA done callback with a DMA channel.
 * @channel: DMA channel number.
 * @done_callback: Channel done callback.
 * @data: The callback reference data, usually the instance of the driver data
 *
 * Returns 0 on success, -EINVAL if the channel number is out of range,
 *	-ACCESS if the channel has not been allocated.
 */

int set_pl330_done_callback(unsigned int channel,
			    pl330_done_callback_t done_callback,
			    void *data)
{
	struct dma_struct *dma = driver_data.dma_chan + channel;

	if (channel >= MAX_DMA_CHANNELS)
		return -EINVAL;

	if (!dma->lock) {
		pr_err("Trying to pl330_done_callback on a free channel (%d)\n",
				channel);
		return -EINVAL;
	}

	if (dma->active) {
		pr_err("Trying to set pl330_done_callback on an active ");
		pr_cont("channel (%d)\n", channel);
		return -EBUSY;
	}

	driver_data.channel_data[channel].done_callback = done_callback;
	driver_data.channel_data[channel].done_callback_data = data;

	return 0;


}
EXPORT_SYMBOL(set_pl330_done_callback);



/**
 * set_pl330_fault_callback - Associate a DMA fault callback with a DMA
 *	channel.
 * @channel: The DMA channel number.
 * @fault_callback: Channel fault callback.
 * @data: The callback data
 * Returns 0 on success, -EINVAL if the channel number is out of range,
 *	-ACCESS if the channel has not been allocated.
 */

int set_pl330_fault_callback(unsigned int channel,
			     pl330_fault_callback_t fault_callback,
			     void *data)
{
	struct dma_struct *dma = driver_data.dma_chan + channel;

	if (channel >= MAX_DMA_CHANNELS)
		return -EINVAL;

	if (!dma->lock) {
		pr_err("trying to set pl330_fault_callback on a free channel ");
		pr_cont("%d\n", channel);
		return -EINVAL;
	}

	if (dma->active) {
		pr_err("trying to set pl330_fault_callback on an active ");
		pr_cont("channel %d\n", channel);
		return -EBUSY;
	}

	driver_data.channel_data[channel].fault_callback = fault_callback;
	driver_data.channel_data[channel].fault_callback_data = data;

	return 0;
}
EXPORT_SYMBOL(set_pl330_fault_callback);


/**
 * set_pl330_incr_dev_addr - Sets the device address increment flag. This
 *	allows users to test a driver without a device being available.
 *	Setting this flag to be 1 can make the PL330 perform memory to memory
 *	transactions.
 * @channel: DMA channel number.
 * @flag: If it's 1 the device address will be increment, 0, the address
 *	will be fixed.
 * Returns 0 on success, -EINVAL if the channel number is out of range,
 *	-ACCESS if the channel has not been allocated.
 */
int set_pl330_incr_dev_addr(unsigned int channel,
			    unsigned int flag)
{
	struct dma_struct *dma = driver_data.dma_chan + channel;

	if (channel >= MAX_DMA_CHANNELS)
		return -EINVAL;

	if (!dma->lock) {
		pr_err("trying to set pl330_fault_callback on a free channel ");
		pr_cont("%d\n", channel);
		return -EINVAL;
	}

	if (dma->active) {
		pr_err("trying to set pl330_fault_callback on an active ");
		pr_cont("channel %d\n", channel);
		return -EBUSY;
	}

	driver_data.channel_data[channel].incr_dev_addr = flag;

	return 0;
}
EXPORT_SYMBOL(set_pl330_incr_dev_addr);

/**
 * get_pl330_sa_reg - Gets the PL330 source address register. This is
 *	mainly for testing and debugging.
 * @channel: DMA channel number.
 *
 * Returns the PL330 DMAC source address value, or 0xFFFFFFFF if the channel
 *	number is out of range
 */
u32 get_pl330_sa_reg(unsigned int channel)
{
	struct pl330_channel_static_data *channel_static_data =
		driver_data.channel_static_data + channel;
	struct pl330_device_data *device_data =
		driver_data.device_data + channel_static_data->dev_id;

	unsigned int dev_chan = channel_static_data->dev_chan;

	if (channel >= MAX_DMA_CHANNELS)
		return 0xFFFFFFFF;

	return pl330_readreg(device_data->base, PL330_SA_n_OFFSET(dev_chan));
}
EXPORT_SYMBOL(get_pl330_sa_reg);

/**
 * get_pl330_da_reg - Gets the PL330 destination address register. This is
 *	mainly for testing and debugging.
 * @channel: DMA channel number.
 *
 * Returns the PL330 DMAC destination address value, or 0xFFFFFFFF if
 *	the channel number is out of range
 */
u32 get_pl330_da_reg(unsigned int channel)
{
	struct pl330_channel_static_data *channel_static_data =
		driver_data.channel_static_data + channel;
	struct pl330_device_data *device_data =
		driver_data.device_data + channel_static_data->dev_id;

	unsigned int dev_chan = channel_static_data->dev_chan;

	if (channel >= MAX_DMA_CHANNELS)
		return 0xFFFFFFFF;

	return pl330_readreg(device_data->base, PL330_DA_n_OFFSET(dev_chan));
}
EXPORT_SYMBOL(get_pl330_da_reg);


/**
 * pl330_init - module init function
 *
 * Returns 0 on success.
 */
static int __init pl330_init(void)
{
	int status;

	pl330_driver_init();

	status = platform_driver_register(&pl330_platform_driver);
	pr_debug("platform_driver_register: %d\n", status);
	return status;
}
module_init(pl330_init);

/**
 * pl330_init - module exit function
 *
 */
static void __exit pl330_exit(void)
{
	/*
	 * unregister dma_driver_ops first
	 */
	platform_driver_unregister(&pl330_platform_driver);
	pr_debug("platform_driver_unregister\n");
}
module_exit(pl330_exit);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("pl330 driver");
MODULE_AUTHOR("Xilinx, Inc.");
MODULE_VERSION("1.00a");
