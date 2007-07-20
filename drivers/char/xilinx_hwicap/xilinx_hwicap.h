/******************************************************************************
*
*     Author: Xilinx, Inc.
*
*
*     This program is free software; you can redistribute it and/or modify it
*     under the terms of the GNU General Public License as published by the
*     Free Software Foundation; either version 2 of the License, or (at your
*     option) any later version.
*
*
*     XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS" AS A
*     COURTESY TO YOU. BY PROVIDING THIS DESIGN, CODE, OR INFORMATION AS
*     ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE, APPLICATION OR STANDARD,
*     XILINX IS MAKING NO REPRESENTATION THAT THIS IMPLEMENTATION IS FREE
*     FROM ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE FOR
*     OBTAINING ANY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION.
*     XILINX EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO
*     THE ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY
*     WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM
*     CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND
*     FITNESS FOR A PARTICULAR PURPOSE.
*
*
*     Xilinx products are not intended for use in life support appliances,
*     devices, or systems. Use in such applications is expressly prohibited.
*
*
*     (c) Copyright 2003-2007 Xilinx Inc.
*     All rights reserved.
*
*
*     You should have received a copy of the GNU General Public License along
*     with this program; if not, write to the Free Software Foundation, Inc.,
*     675 Mass Ave, Cambridge, MA 02139, USA.
*
******************************************************************************/

#ifndef XILINX_HWICAP_H_ /* prevent circular inclusions */
#define XILINX_HWICAP_H_ /* by using protection macros */

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
#include <linux/device.h>
#else
#include <linux/platform_device.h>
#endif

#include <linux/config.h>
#include <asm/io.h>

struct xhwicap_drvdata {
        u32 flags;
        u32 write_buffer_in_use; // Always in [0,3]
        u8 write_buffer[4];
        u32 read_buffer_in_use;  // Always in [0,3]
        u8 read_buffer[4];
	u32 regs_phys;	         /* phys. address of the control registers */
        u8 *baseAddress;         /* virt. address of the control registers */
        struct device *dev;
	struct cdev cdev;	 /* Char device structure */
	dev_t devt;
};


/***************************** Include Files ********************************/

#define virtex2 0
#define virtex4 1

#ifdef CONFIG_XILINX_VIRTEX_4_FX
#define XHI_FAMILY virtex4
#else
#define XHI_FAMILY virtex2
#endif


/************************** Constant Definitions ****************************/


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
#define XHI_TYPE_2_READ ( (XHI_TYPE_2 << XHI_TYPE_SHIFT) | \
        (XHI_OP_READ << XHI_OP_SHIFT) ) 

#define XHI_TYPE_2_WRITE ( (XHI_TYPE_2 << XHI_TYPE_SHIFT) | \
        (XHI_OP_WRITE << XHI_OP_SHIFT) )
        
#define XHI_TYPE2_CNT_MASK          0x07FFFFFF

#define XHI_TYPE_1_PACKET_MAX_WORDS 2047UL
#define XHI_TYPE_1_HEADER_BYTES     4
#define XHI_TYPE_2_HEADER_BYTES     8

/* Indicates how many bytes will fit in a buffer. (1 BRAM) */
#define XHI_MAX_BUFFER_BYTES        2048
#define XHI_MAX_BUFFER_INTS         512


/* Number of frames in different tile types */
#if XHI_FAMILY == virtex4

#define XHI_GCLK_FRAMES             3
#define XHI_IOB_FRAMES              30
#define XHI_DSP_FRAMES              21
#define XHI_CLB_FRAMES              22
#define XHI_BRAM_FRAMES             64
#define XHI_BRAM_INT_FRAMES         20

#else

#define XHI_GCLK_FRAMES             4
#define XHI_IOB_FRAMES              4
#define XHI_IOI_FRAMES              22
#define XHI_CLB_FRAMES              22
#define XHI_BRAM_FRAMES             64
#define XHI_BRAM_INT_FRAMES         22

#endif

/* Device Resources */
#define CLB                         0
#define DSP                         1
#define BRAM                        2
#define BRAM_INT                    3
#define IOB                         4
#define IOI                         5
#define CLK                         6
#define MGT                         7

#define BLOCKTYPE0                  0
#define BLOCKTYPE1                  1
#define BLOCKTYPE2                  2

/* The number of words reserved for the header in the storage buffer. */ /* MAY CHANGE FOR V4*/
#define XHI_HEADER_BUFFER_WORDS     20
#define XHI_HEADER_BUFFER_BYTES     (XHI_HEADER_BUFFER_WORDS << 2)

/* CLB major frames start at 3 for the first column (since we are using 
 * column numbers that start at 1, when the column is added to this offset, 
 * that first one will be 3 as required. */
#define XHI_CLB_MAJOR_FRAME_OFFSET  2


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

/* Major Row Offset */
#define XHI_CLB_MAJOR_ROW_OFFSET 96+(32*XHI_HEADER_BUFFER_WORDS)-1

/* Number of times to poll the done regsiter */
#define XHI_MAX_RETRIES     1000

/************************** Constant Definitions ****************************/

/* XHwIcap register offsets */

#define XHI_SIZE_REG_OFFSET        0x800L  /* Size of transfer, read & write */
#define XHI_BRAM_OFFSET_REG_OFFSET 0x804L  /* Offset into bram, read & write */
#define XHI_RNC_REG_OFFSET         0x808L  /* Read not Configure, direction of
                                                transfer.  Write only */
#define XHI_STATUS_REG_OFFSET      0x80CL  /* Indicates transfer complete. Read
                                                only */

/* Constants for setting the RNC register */
#define XHI_CONFIGURE              0x0UL
#define XHI_READBACK               0x1UL

/* Constants for the Done register */
#define XHI_NOT_FINISHED           0x0UL
#define XHI_FINISHED               0x1UL

/**************************** Type Definitions ******************************/

/***************** Macros (Inline Functions) Definitions ********************/

/****************************************************************************/
/**
*
* Get the contents of the size register. 
*
* The size register holds the number of 32 bit words to transfer between
* bram and the icap (or icap to bram).
*
* @param    BaseAddress is the  base address of the device
*
* @return   A 32-bit value representing the contents of the size
* register.
*
* @note    
*
* u32 XHwIcap_mGetSizeReg(u32 BaseAddress);
*
*****************************************************************************/
#define XHwIcap_mGetSizeReg(BaseAddress) \
    ( in_be32((u32 *)((BaseAddress) + XHI_SIZE_REG_OFFSET)) )

/****************************************************************************/
/**
*
* Get the contents of the bram offset register. 
*
* The bram offset register holds the starting bram address to transfer
* data from during configuration or write data to during readback.
*
* @param    BaseAddress is the  base address of the device
*
* @return   A 32-bit value representing the contents of the bram offset
* register.
*
* @note    
*
* u32 XHwIcap_mGetOffsetReg(u32 BaseAddress);
*
*****************************************************************************/
#define XHwIcap_mGetOffsetReg(BaseAddress) \
    ( in_be32((u32 *)((BaseAddress + XHI_BRAM_OFFSET_REG_OFFSET))) )

/****************************************************************************/
/**
*
* Get the contents of the done register. 
*
* The done register is set to zero during configuration or readback.
* When the current configuration or readback completes the done register
* is set to one.
*
* @param    BaseAddress is the base address of the device
*
* @return   A 32-bit value with bit 1 representing done or not
*
* @note
*
* u32 XHwIcap_mGetDoneReg(u32 BaseAddress);
*
*****************************************************************************/

#define XHwIcap_mGetDoneReg(BaseAddress) \
    ( in_be32((u32 *)((BaseAddress + XHI_STATUS_REG_OFFSET))) & 1)

/****************************************************************************/
/**
*
* Get the contents of the status register. 
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
*
* @param    BaseAddress is the base address of the device
*
* @return   A 32-bit value representing the contents of the status register
*
* @note
*
* u32 XHwIcap_mGetStatusReg(u32 BaseAddress);
*
*****************************************************************************/

#define XHwIcap_mGetStatusReg(BaseAddress) \
    ( in_be32((u32 *)((BaseAddress + XHI_STATUS_REG_OFFSET))) )

#define XHwIcap_mReset(BaseAddress) \
    ( out_be32((u32*)((BaseAddress + XHI_STATUS_REG_OFFSET)), 0xFEFE) )

/****************************************************************************/
/**
* Reads data from the storage buffer bram.
*
* A bram is used as a configuration memory cache.  One frame of data can
* be stored in this "storage buffer".
*
* @param    BaseAddress - contains the base address of the component.
*
* @param    Offset - The offset into which the data should be read.
*
* @return   The value of the specified offset in the bram.
*
* @note
*
* u32 XHwIcap_mGetBram(u32 BaseAddress, u32 Offset);
*
*****************************************************************************/
#define XHwIcap_mGetBram(BaseAddress, Offset) \
    ( in_be32((u32 *)((BaseAddress+(Offset<<2)))) )



/****************************************************************************/
/**
* Set the size register.
*
* The size register holds the number of 8 bit bytes to transfer between
* bram and the icap (or icap to bram).
*
* @param    BaseAddress - contains the base address of the device.
*
* @param    Data - The size in bytes.
*
* @return   None.
*
* @note
*
* void XHwIcap_mSetSizeReg(u32 BaseAddress, u32 Data);
*
*****************************************************************************/
#define XHwIcap_mSetSizeReg(BaseAddress, Data) \
    ( out_be32((u32*)((BaseAddress) + XHI_SIZE_REG_OFFSET), (Data)) )

/****************************************************************************/
/**
* Set the bram offset register.
*
* The bram offset register holds the starting bram address to transfer
* data from during configuration or write data to during readback.
*
* @param    BaseAddress contains the base address of the device.
* 
* @param    Data is the value to be written to the data register.
*
* @return   None.
*
* @note 
*
* void XHwIcap_mSetOffsetReg(u32 BaseAddress, u32 Data);
*
*****************************************************************************/
#define XHwIcap_mSetOffsetReg(BaseAddress, Data) \
    ( out_be32((u32*)((BaseAddress) + XHI_BRAM_OFFSET_REG_OFFSET), (Data)) )

/****************************************************************************/
/**
* Set the RNC (Readback not Configure) register.
*
* The RNC register determines the direction of the data transfer.  It
* controls whether a configuration or readback take place.  Writing to
* this register initiates the transfer.  A value of 1 initiates a
* readback while writing a value of 0 initiates a configuration.
*
* @param    BaseAddress contains the base address of the device.
*
* @param    Data is the value to be written to the data register.
*
* @return   None.
*
* @note
*
* void XHwIcap_mSetRncReg(u32 BaseAddress, u32 Data);
*
*****************************************************************************/
#define XHwIcap_mSetRncReg(BaseAddress, Data) \
    ( out_be32((u32*)((BaseAddress) + XHI_RNC_REG_OFFSET), (Data)) )

/****************************************************************************/
/**
* Write data to the storage buffer bram.
*
* A bram is used as a configuration memory cache.  One frame of data can
* be stored in this "storage buffer".
*
* @param    BaseAddress - contains the base address of the component.
*
* @param    Offset - The offset into which the data should be written.
*
* @param    Data - The value to be written to the bram offset.
*
* @return   None.
*
* @note
*
* void XHwIcap_mSetBram(u32 BaseAddress, u32 Offset, u32 Data);
*
*****************************************************************************/
#define XHwIcap_mSetBram(BaseAddress, Offset, Data) \
    ( out_be32((u32*)((BaseAddress+(Offset<<2))), (Data)) )


/****************************************************************************/
/**
* 
* Generates a Type 1 packet header that reads back the requested configuration
* register.
*
* @param    Register is the address of the register to be read back.
*           Register constants are defined in this file.
*
* @return   Type 1 packet header to read the specified register
*
* @note     None.
*
*****************************************************************************/
#define XHwIcap_Type1Read(Register) \
    ( (XHI_TYPE_1 << XHI_TYPE_SHIFT) | (Register << XHI_REGISTER_SHIFT) | \
    (XHI_OP_READ << XHI_OP_SHIFT) )

/****************************************************************************/
/**
* 
* Generates a Type 1 packet header that writes to the requested
* configuration register.
*
* @param    Register is the address of the register to be written to.
*           Register constants are defined in this file.
*
* @return   Type 1 packet header to write the specified register
*
* @note     None.
*
*****************************************************************************/
#define XHwIcap_Type1Write(Register) \
    ( (XHI_TYPE_1 << XHI_TYPE_SHIFT) | (Register << XHI_REGISTER_SHIFT) | \
    (XHI_OP_WRITE << XHI_OP_SHIFT) )


/************************** Function Prototypes *****************************/


/* These functions are the ones defined in the lower level
 * Self-Reconfiguration Platform (SRP) API.
 */

/* Initializes a XHwIcap instance.. */
int XHwIcap_Initialize(struct xhwicap_drvdata *InstancePtr,  u16 DeviceId,
                           u32 DeviceIdCode);

/* Reads integers from the device into the storage buffer. */
int XHwIcap_DeviceRead(struct xhwicap_drvdata *InstancePtr, u32 Offset,
                           u32 NumInts);

/* Writes integers to the device from the storage buffer. */
int XHwIcap_DeviceWrite(struct xhwicap_drvdata *InstancePtr, u32 Offset,
                            u32 NumInts);

/* Writes word to the storage buffer. */
void XHwIcap_StorageBufferWrite(struct xhwicap_drvdata *InstancePtr, u32 Address,
                                u32 Data);

/* Reads word from the storage buffer. */
u32 XHwIcap_StorageBufferRead(struct xhwicap_drvdata *InstancePtr, u32 Address);

/* Reads one frame from the device and puts it in the storage buffer. */
int XHwIcap_DeviceReadFrame(struct xhwicap_drvdata *InstancePtr, s32 Block,
                               s32 MajorFrame, s32 MinorFrame);

/* Reads one frame from the device and puts it in the storage buffer. */
int XHwIcap_DeviceReadFrameV4(struct xhwicap_drvdata *InstancePtr, s32 Top,
                                s32 Block, s32 HClkRow,
                                s32 MajorFrame, s32 MinorFrame);

/* Writes one frame from the storage buffer and puts it in the device. */
int XHwIcap_DeviceWriteFrame(struct xhwicap_drvdata *InstancePtr, s32 Block,
                                s32 MajorFrame, s32 MinorFrame);

/* Writes one frame from the storage buffer and puts it in the device. */
int XHwIcap_DeviceWriteFrameV4(struct xhwicap_drvdata *InstancePtr, s32 Top,
                                s32 Block, s32 HClkRow,
                                 s32 MajorFrame, s32 MinorFrame);

/* Loads a partial bitstream from system memory. */
int XHwIcap_SetConfiguration(struct xhwicap_drvdata *InstancePtr, u32 *Data,
                                u32 Size);

/* Loads a partial bitstream from system memory. */
int XHwIcap_GetConfiguration(struct xhwicap_drvdata *InstancePtr, u32 *Data,
                                u32 Size);

/* Sends a DESYNC command to the ICAP */
int XHwIcap_CommandDesync(struct xhwicap_drvdata *InstancePtr);

/* Sends a CAPTURE command to the ICAP */
int XHwIcap_CommandCapture(struct xhwicap_drvdata *InstancePtr);

/* Returns the value of the specified configuration register */
u32 XHwIcap_GetConfigReg(struct xhwicap_drvdata *InstancePtr, u32 ConfigReg);

#endif
