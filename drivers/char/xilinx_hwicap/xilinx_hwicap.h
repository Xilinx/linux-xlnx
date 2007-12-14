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
 *     (c) Copyright 2003-2007 Xilinx Inc.
 *     All rights reserved.
 *
 *     You should have received a copy of the GNU General Public License along
 *     with this program; if not, write to the Free Software Foundation, Inc.,
 *     675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

#ifndef XILINX_HWICAP_H_	/* prevent circular inclusions */
#define XILINX_HWICAP_H_	/* by using protection macros */

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/version.h>
#include <linux/platform_device.h>

#include <asm/io.h>

struct hwicap_drvdata {
	u32 flags;
	u32 write_buffer_in_use;  /* Always in [0,3] */
	u8 write_buffer[4];
	u32 read_buffer_in_use;	  /* Always in [0,3] */
	u8 read_buffer[4];
	u32 mem_start;		  /* phys. address of the control registers */
	u32 mem_end;		  /* phys. address of the control registers */
	u32 mem_size;
	void __iomem *baseAddress;/* virt. address of the control registers */

	struct device *dev;
	struct cdev cdev;	/* Char device structure */
	dev_t devt;
};

/************** Include Files ***************/

#define virtex2 0
#define virtex4 1

#ifdef CONFIG_XILINX_VIRTEX_4_FX
#define XHI_FAMILY virtex4
#else
#define XHI_FAMILY virtex2
#endif

/************ Constant Definitions *************/

#define XHI_PAD_FRAMES              0x1

/* Mask for calculating configuration packet headers */
#define XHI_WORD_COUNT_MASK_TYPE_1  0x7FFUL
#define XHI_WORD_COUNT_MASK_TYPE_2  0x1FFFFFUL
#define XHI_TYPE_MASK               0x7
#define XHI_REGISTER_MASK           0xF
#define XHI_OP_MASK                 0x3

#define XHI_TYPE_SHIFT              29
#define XHI_REGISTER_SHIFT          13
#define XHI_OP_SHIFT                27

#define XHI_TYPE_1                  1
#define XHI_TYPE_2                  2
#define XHI_OP_WRITE                2
#define XHI_OP_READ                 1

/* Address Block Types */
#define XHI_FAR_CLB_BLOCK           0
#define XHI_FAR_BRAM_BLOCK          1
#define XHI_FAR_BRAM_INT_BLOCK      2

/* Addresses of the Configuration Registers */
#define XHI_CRC                     0
#define XHI_FAR                     1
#define XHI_FDRI                    2
#define XHI_FDRO                    3
#define XHI_CMD                     4
#define XHI_CTL                     5
#define XHI_MASK                    6
#define XHI_STAT                    7
#define XHI_LOUT                    8
#define XHI_COR                     9
#define XHI_MFWR                    10

#if XHI_FAMILY == virtex4

#define XHI_CBC                     11
#define XHI_IDCODE                  12
#define XHI_AXSS                    13
#define XHI_NUM_REGISTERS           14

#else

#define XHI_FLR                     11
#define XHI_KEY                     12
#define XHI_CBC                     13
#define XHI_IDCODE                  14
#define XHI_NUM_REGISTERS           15

#endif

/* Configuration Commands */
#define XHI_CMD_NULL                0
#define XHI_CMD_WCFG                1
#define XHI_CMD_MFW                 2
#define XHI_CMD_DGHIGH              3
#define XHI_CMD_RCFG                4
#define XHI_CMD_START               5
#define XHI_CMD_RCAP                6
#define XHI_CMD_RCRC                7
#define XHI_CMD_AGHIGH              8
#define XHI_CMD_SWITCH              9
#define XHI_CMD_GRESTORE            10
#define XHI_CMD_SHUTDOWN            11
#define XHI_CMD_GCAPTURE            12
#define XHI_CMD_DESYNCH             13

/* Packet constants */
#define XHI_SYNC_PACKET             0xAA995566UL
#define XHI_DUMMY_PACKET            0xFFFFFFFFUL
#define XHI_NOOP_PACKET             (XHI_TYPE_1 << XHI_TYPE_SHIFT)
#define XHI_TYPE_2_READ ((XHI_TYPE_2 << XHI_TYPE_SHIFT) | \
			(XHI_OP_READ << XHI_OP_SHIFT))

#define XHI_TYPE_2_WRITE ((XHI_TYPE_2 << XHI_TYPE_SHIFT) | \
			(XHI_OP_WRITE << XHI_OP_SHIFT))

#define XHI_TYPE2_CNT_MASK          0x07FFFFFF

#define XHI_TYPE_1_PACKET_MAX_WORDS 2047UL
#define XHI_TYPE_1_HEADER_BYTES     4
#define XHI_TYPE_2_HEADER_BYTES     8

/* Indicates how many bytes will fit in a buffer. (1 BRAM) */
#define XHI_MAX_BUFFER_BYTES        2048
#define XHI_MAX_BUFFER_INTS         (XHI_MAX_BUFFER_BYTES >> 2)

/* File access and error constants */
#define XHI_DEVICE_READ_ERROR       -1
#define XHI_DEVICE_WRITE_ERROR      -2
#define XHI_BUFFER_OVERFLOW_ERROR   -3

#define XHI_DEVICE_READ             0x1
#define XHI_DEVICE_WRITE            0x0

/* Constants for checking transfer status */
#define XHI_CYCLE_DONE              0
#define XHI_CYCLE_EXECUTING         1

/* Constant to use for CRC check when CRC has been disabled */
#define XHI_DISABLED_AUTO_CRC       0x0000DEFCUL

/* Number of times to poll the done regsiter */
#define XHI_MAX_RETRIES     1000

/************ Constant Definitions *************/

/* XHwIcap register offsets */

/* Size of transfer, read & write */
#define XHI_SIZE_REG_OFFSET        0x800L
/* offset into bram, read & write */
#define XHI_BRAM_OFFSET_REG_OFFSET 0x804L
/* Read not Configure, direction of transfer.  Write only */
#define XHI_RNC_REG_OFFSET         0x808L
/* Indicates transfer complete. Read only */
#define XHI_STATUS_REG_OFFSET      0x80CL

/* Constants for setting the RNC register */
#define XHI_CONFIGURE              0x0UL
#define XHI_READBACK               0x1UL

/* Constants for the Done register */
#define XHI_NOT_FINISHED           0x0UL
#define XHI_FINISHED               0x1UL

/**
 * XHwIcap_mGetSizeReg: Get the contents of the size register.
 * @parameter base_address: is the base address of the device
 *
 * The size register holds the number of 32 bit words to transfer between
 * bram and the icap (or icap to bram).
 **/
static inline u32 XHwIcap_mGetSizeReg(void __iomem *base_address) {
    return in_be32((base_address + XHI_SIZE_REG_OFFSET));
}

/**
 * XHwIcap_mGetoffsetReg: Get the contents of the bram offset register.
 * @parameter base_address: is the base address of the device
 *
 * The bram offset register holds the starting bram address to transfer
 * data from during configuration or write data to during readback.
 **/
static inline u32 XHwIcap_mGetoffsetReg(void __iomem *base_address) {
    return in_be32(((base_address + XHI_BRAM_OFFSET_REG_OFFSET)));
}

/**
 *
 * XHwIcap_mGetStatusReg: Get the contents of the status register.
 * @parameter base_address: is the base address of the device
 *
 * The status register contains the ICAP status and the done bit.
 *
 * D8 - cfgerr
 * D7 - dalign
 * D6 - rip
 * D5 - in_abort_l
 * D4 - Always 1
 * D3 - Always 1
 * D2 - Always 1
 * D1 - Always 1
 * D0 - Done bit
 **/
static inline u32 XHwIcap_mGetStatusReg(void __iomem *base_address) {
    return in_be32(((base_address + XHI_STATUS_REG_OFFSET)));
}

/**
 * XHwIcap_mGetBram: Reads data from the storage buffer bram.
 * @parameter base_address: contains the base address of the component.
 * @parameter offset: The offset into which the data should be read.
 *
 * A bram is used as a configuration memory cache.  One frame of data can
 * be stored in this "storage buffer".
 **/
static inline u32 XHwIcap_mGetBram(void __iomem *base_address, u32 offset) {
    return in_be32(((base_address+(offset<<2))));
}

/**
 * hwicap_busy: Return true if the icap device is busy
 * @parameter base_address: is the base address of the device
 *
 * The queries the low order bit of the status register, which
 * indicates whether the current configuration or readback operation
 * has completed.
 **/
static inline bool hwicap_busy(void __iomem *base_address) {
    return (XHwIcap_mGetStatusReg(base_address) & 1) == XHI_NOT_FINISHED;
}

/**
 * hwicap_busy: Return true if the icap device is not busy
 * @parameter base_address: is the base address of the device
 *
 * The queries the low order bit of the status register, which
 * indicates whether the current configuration or readback operation
 * has completed.
 **/
static inline bool hwicap_done(void __iomem *base_address) {
    return (XHwIcap_mGetStatusReg(base_address) & 1) == XHI_FINISHED;
}

/**
 * XHwIcap_mSetSizeReg: Set the size register.
 * @parameter base_address: is the base address of the device
 * @parameter Data: The size in bytes.
 *
 * The size register holds the number of 8 bit bytes to transfer between
 * bram and the icap (or icap to bram).
 **/
static inline void XHwIcap_mSetSizeReg(void __iomem *base_address, u32 Data) {
    out_be32((base_address + XHI_SIZE_REG_OFFSET), Data);
}

/**
 * XHwIcap_mSetoffsetReg: Set the bram offset register.
 * @parameter base_address: contains the base address of the device.
 * @parameter Data: is the value to be written to the data register.
 *
 * The bram offset register holds the starting bram address to transfer
 * data from during configuration or write data to during readback.
 **/
static inline void XHwIcap_mSetOffsetReg(void __iomem *base_address, u32 Data) {
    out_be32((base_address + XHI_BRAM_OFFSET_REG_OFFSET), Data);
}

/**
 * XHwIcap_mSetRncReg: Set the RNC (Readback not Configure) register.
 * @parameter base_address: contains the base address of the device.
 * @parameter Data: is the value to be written to the data register.
 *
 * The RNC register determines the direction of the data transfer.  It
 * controls whether a configuration or readback take place.  Writing to
 * this register initiates the transfer.  A value of 1 initiates a
 * readback while writing a value of 0 initiates a configuration.
 **/
static inline void XHwIcap_mSetRncReg(void __iomem *base_address, u32 Data) {
    out_be32((base_address + XHI_RNC_REG_OFFSET), Data);
}

/**
 * XHwIcap_mSetBram: Write data to the storage buffer bram.
 * @parameter base_address: contains the base address of the component.
 * @parameter offset: The offset into which the data should be written.
 * @parameter Data: The value to be written to the bram offset.
 *
 * A bram is used as a configuration memory cache.  One frame of data can
 * be stored in this "storage buffer".
 **/
static inline void XHwIcap_mSetBram(void __iomem *base_address, u32 offset, u32 Data) {
    out_be32(((base_address+(offset<<2))), (Data));
}

/**
 * XHwIcap_mReset: Reset the logic of the icap device.
 * @parameter base_address: contains the base address of the component.
 *
 * Writing to the status register resets the ICAP logic.
 **/
static inline void XHwIcap_mReset(void __iomem *base_address) {
    out_be32(((base_address + XHI_STATUS_REG_OFFSET)), 0xFEFE);
}

/**
 * XHwIcap_Type1Read: Generates a Type 1 read packet header.
 * @parameter: Register is the address of the register to be read back.
 *
 * Generates a Type 1 read packet header, which is used to indirectly
 * read registers in the configuration logic.  This packet must then
 * be sent through the icap device, and a return packet received with
 * the information.
 **/
static inline u32 XHwIcap_Type1Read(u32 Register) {
    return (XHI_TYPE_1 << XHI_TYPE_SHIFT) | 
        (Register << XHI_REGISTER_SHIFT) |
        (XHI_OP_READ << XHI_OP_SHIFT);
}

/**
 * XHwIcap_Type1Write: Generates a Type 1 write packet header
 * @parameter: Register is the address of the register to be read back.
 **/
static inline u32 XHwIcap_Type1Write(u32 Register) {
    return (XHI_TYPE_1 << XHI_TYPE_SHIFT) |
        (Register << XHI_REGISTER_SHIFT) |
        (XHI_OP_WRITE << XHI_OP_SHIFT);
}

/******** Function Prototypes **********/

/* These functions are the ones defined in the lower level
 * Self-Reconfiguration Platform (SRP) API.
 */

/* Initializes a XHwIcap instance.. */
int XHwIcap_Initialize(struct hwicap_drvdata *drvdata, u16 device_id,
		       u32 device_idcode);

/* Reads integers from the device into the storage buffer. */
int XHwIcap_DeviceRead(struct hwicap_drvdata *drvdata, u32 offset,
		       u32 NumInts);

/* Writes integers to the device from the storage buffer. */
int XHwIcap_DeviceWrite(struct hwicap_drvdata *drvdata, u32 offset,
			u32 NumInts);

/* Writes word to the storage buffer. */
void XHwIcap_StorageBufferWrite(struct hwicap_drvdata *drvdata,
				u32 address, u32 Data);

/* Reads word from the storage buffer. */
u32 XHwIcap_StorageBufferRead(struct hwicap_drvdata *drvdata, u32 address);

/* Loads a partial bitstream from system memory. */
int XHwIcap_SetConfiguration(struct hwicap_drvdata *drvdata, u32 *Data,
			     u32 Size);

/* Loads a partial bitstream from system memory. */
int XHwIcap_GetConfiguration(struct hwicap_drvdata *drvdata, u32 *Data,
			     u32 Size);

/* Sends a DESYNC command to the ICAP */
int XHwIcap_CommandDesync(struct hwicap_drvdata *drvdata);

/* Sends a CAPTURE command to the ICAP */
int XHwIcap_CommandCapture(struct hwicap_drvdata *drvdata);

/* Returns the value of the specified configuration register */
u32 XHwIcap_GetConfigReg(struct hwicap_drvdata *drvdata, u32 ConfigReg);

#endif
