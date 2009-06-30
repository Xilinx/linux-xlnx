/* $Id: xsysace.h,v 1.1 2006/02/17 21:52:36 moleres Exp $ */
/******************************************************************************
*
*       XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
*       AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
*       SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
*       OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
*       APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
*       THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
*       AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
*       FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
*       WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
*       IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
*       REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
*       INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*       FOR A PARTICULAR PURPOSE.
*
*       (c) Copyright 2002-2005 Xilinx Inc.
*       All rights reserved.
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xsysace.h
*
* The Xilinx System ACE driver. This driver supports the Xilinx System Advanced
* Configuration Environment (ACE) controller. It currently supports only the
* CompactFlash solution. The driver makes use of the Microprocessor (MPU)
* interface to communicate with the device.
*
* The driver provides a user the ability to access the CompactFlash through
* the System ACE device.  The user can read and write CompactFlash sectors,
* identify the flash device, and reset the flash device.  Also, the driver
* provides a user the ability to configure FPGA devices by selecting a
* configuration file (.ace file) resident on the CompactFlash, or directly
* configuring the FPGA devices via the MPU port and the configuration JTAG
* port of the controller.
*
* <b>Initialization & Configuration</b>
*
* The XSysAce_Config structure is used by the driver to configure itself. This
* configuration structure is typically created by the tool-chain based on HW
* build properties.
*
* To support multiple runtime loading and initialization strategies employed
* by various operating systems, the driver instance can be initialized in one
* of the following ways:
*
*   - XSysAce_Initialize(InstancePtr, DeviceId) - The driver looks up its own
*     configuration structure created by the tool-chain based on an ID provided
*     by the tool-chain.
*
*   - XSysAce_CfgInitialize(InstancePtr, CfgPtr, EffectiveAddr) - Uses a
*     configuration structure provided by the caller. If running in a system
*     with address translation, the provided virtual memory base address
*     replaces the physical address present in the configuration structure.
*
* <b>Bus Mode</b>
*
* The System ACE device supports both 8-bit and 16-bit access to its registers.
* The driver defaults to 8-bit access, but can be changed to use 16-bit access
* at compile-time.  The compile-time constant XPAR_XSYSACE_MEM_WIDTH must be
* defined equal to 16 to make the driver use 16-bit access. This constant is
* typically defined in xparameters.h.
*
* <b>Endianness</b>
*
* The System ACE device is little-endian. If being accessed by a big-endian
* processor, the endian conversion will be done by the device driver. The
* endian conversion is encapsulated inside the XSysAce_RegRead/Write functions
* so that it can be removed if the endian conversion is moved to hardware.
*
* <b>Hardware Access</b>
*
* The device driver expects the System ACE controller to be a memory-mapped
* device. Access to the System ACE controller is typically achieved through
* the External Memory Controller (EMC) IP core. The EMC is simply a pass-through
* device that allows access to the off-chip System ACE device. There is no
* software-based setup or configuration necessary for the EMC.
*
* The System ACE registers are expected to be byte-addressable. If for some
* reason this is not possible, the register offsets defined in xsysace_l.h must
* be changed accordingly.
*
* <b>Reading or Writing CompactFlash</b>
*
* The smallest unit that can be read from or written to CompactFlash is one
* sector. A sector is 512 bytes.  The functions provided by this driver allow
* the user to specify a starting sector ID and the number of sectors to be read
* or written. At most 256 sectors can be read or written in one operation. The
* user must ensure that the buffer passed to the functions is big enough to
* hold (512 * NumSectors), where NumSectors is the number of sectors specified.
*
* <b>Interrupt Mode</b>
*
* By default, the device and driver are in polled mode. The user is required to
* enable interrupts using XSysAce_EnableInterrupt(). In order to use interrupts,
* it is necessary for the user to connect the driver's interrupt handler,
* XSysAce_InterruptHandler(), to the interrupt system of the application. This
* function does not save and restore the processor context. An event handler
* must also be set by the user, using XSysAce_SetEventHandler(), for the driver
* such that the handler is called when interrupt events occur. The handler is
* called from interrupt context and allows application-specific processing to
* be performed.
*
* In interrupt mode, the only available interrupt is data buffer ready, so
* the size of a data transfer between interrupts is 32 bytes (the size of the
* data buffer).
*
* <b>Polled Mode</b>
*
* The sector read and write functions are blocking when in polled mode. This
* choice was made over non-blocking since sector transfer rates are high
* (>20Mbps) and the user can limit the number of sectors transferred in a single
* operation to 1 when in polled mode, plus the API for non-blocking polled
* functions was a bit awkward. Below is some more information on the sector
* transfer rates given the current state of technology (year 2002). Although
* the seek times for CompactFlash cards is high, this average hit needs to be
* taken every time a new read/write operation is invoked by the user. So the
* additional few microseconds to transfer an entire sector along with seeking
* is miniscule.
*
* - Microdrives are slower than CompactFlash cards by a significant factor,
*   especially if the MD is asleep.
*     - Microdrive:
*           - Power-up/wake-up time is approx. 150 to 1000 ms.
*           - Average seek time is approx. 15 to 20 ms.
*     - CompactFlash:
*           - Power-up/reset time is approx. 50 to 400 ms and wake-up time is
*             approx. 3 ms.
*           - "Seek time" here means how long it takes the internal controller
*             to process the command until the sector data is ready for transfer
*             by the ACE controller.  This time is approx. 2 ms per sector.
*
*  - Once the sector data is ready in the CF device buffer (i.e., "seek time" is
*    over) the ACE controller can read 2 bytes from the MD/CF device every 11
*    clock cycles, assuming no wait cycles happen.  For instance, if the clock
*    is 33 MHz, then then the max. rate that the ACE controller can transfer is
*    6 MB/sec.  However, due to other overhead (e.g., time for data buffer
*    transfers over MPU port, etc.), a better estimate is 3-5 MB/sec.
*
* <b>Mutual Exclusion</b>
*
* This driver is not thread-safe. The System ACE device has a single data
* buffer and therefore only one operation can be active at a time. The device
* driver does not prevent the user from starting an operation while a previous
* operation is still in progress. It is up to the user to provide this mutual
* exclusion.
*
* <b>Errors</b>
*
* Error causes are defined in xsysace_l.h using the prefix XSA_ER_*. The
* user can use XSysAce_GetErrors() to retrieve all outstanding errors.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a rpm  06/17/02 work in progress
* 1.01a jvb  12/14/05 I separated dependency on the static config table and
*                     xparameters.h from the driver initialization by moving
*                     _Initialize and _LookupConfig to _sinit.c. I also added
*                     the new _CfgInitialize routine. (The dependency on
*                     XPAR_XSYSACE_MEM_WIDTH still remains.)
* </pre>
*
******************************************************************************/

#ifndef XSYSACE_H		/* prevent circular inclusions */
#define XSYSACE_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif


/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xstatus.h"
#include "xsysace_l.h"

/************************** Constant Definitions *****************************/

/** @name Asynchronous Events
 *
 * Asynchronous events passed to the event handler when in interrupt mode.
 *
 * Note that when an error event occurs, the only way to clear this condition
 * is to reset the CompactFlash or the System ACE configuration controller,
 * depending on where the error occurred. The driver does not reset either
 * and leaves this task to the user.
 * @{
 */
#define XSA_EVENT_CFG_DONE  1  /**< Configuration of JTAG chain is done */
#define XSA_EVENT_DATA_DONE 2  /**< Data transfer to/from CompactFlash is done */
#define XSA_EVENT_ERROR     3  /**< An error occurred. Use XSysAce_GetErrors()
                                *   to determine the cause of the error(s).
                                */
/*@}*/


/**************************** Type Definitions *******************************/

/**
 * Typedef for CompactFlash identify drive parameters. Use XSysAce_IdentifyCF()
 * to retrieve this information from the CompactFlash storage device.
 */
typedef struct {
	u16 Signature;	    /**< CompactFlash signature is 0x848a */
	u16 NumCylinders;   /**< Default number of cylinders */
	u16 Reserved;
	u16 NumHeads;	    /**< Default number of heads */
	u16 NumBytesPerTrack;
			    /**< Number of unformatted bytes per track */
	u16 NumBytesPerSector;
			    /**< Number of unformatted bytes per sector */
	u16 NumSectorsPerTrack;
			    /**< Default number of sectors per track */
	u32 NumSectorsPerCard;
			    /**< Default number of sectors per card */
	u16 VendorUnique;   /**< Vendor unique */
	u8 SerialNo[20];    /**< ASCII serial number */
	u16 BufferType;	    /**< Buffer type */
	u16 BufferSize;	    /**< Buffer size in 512-byte increments */
	u16 NumEccBytes;    /**< Number of ECC bytes on R/W Long cmds */
	u8 FwVersion[8];    /**< ASCII firmware version */
	u8 ModelNo[40];	    /**< ASCII model number */
	u16 MaxSectors;	    /**< Max sectors on R/W Multiple cmds */
	u16 DblWord;	    /**< Double Word not supported */
	u16 Capabilities;   /**< Device capabilities */
	u16 Reserved2;
	u16 PioMode;	    /**< PIO data transfer cycle timing mode */
	u16 DmaMode;	    /**< DMA data transfer cycle timing mode */
	u16 TranslationValid;
			    /**< Translation parameters are valid */
	u16 CurNumCylinders;/**< Current number of cylinders */
	u16 CurNumHeads;    /**< Current number of heads */
	u16 CurSectorsPerTrack;
			    /**< Current number of sectors per track */
	u32 CurSectorsPerCard;
			    /**< Current capacity in sectors */
	u16 MultipleSectors;/**< Multiple sector setting */
	u32 LbaSectors;	    /**< Number of addressable sectors in LBA mode */
	u8 Reserved3[132];
	u16 SecurityStatus; /**< Security status */
	u8 VendorUniqueBytes[62];
			      /**< Vendor unique bytes */
	u16 PowerDesc;	    /**< Power requirement description */
	u8 Reserved4[190];

} XSysAce_CFParameters;


/**
 * Callback when an asynchronous event occurs during interrupt mode.
 *
 * @param CallBackRef is a callback reference passed in by the upper layer
 *        when setting the callback functions, and passed back to the upper
 *        layer when the callback is invoked.
 * @param Event is the event that occurred.  See xsysace.h and the event
 *        identifiers prefixed with XSA_EVENT_* for a description of possible
 *        events.
 */
typedef void (*XSysAce_EventHandler) (void *CallBackRef, int Event);

/**
 * This typedef contains configuration information for the device.
 */
typedef struct {
	u16 DeviceId;	/**< Unique ID  of device */
	u32 BaseAddress;/**< Register base address */

} XSysAce_Config;

/**
 * The XSysAce driver instance data. The user is required to allocate a
 * variable of this type for every System ACE device in the system. A
 * pointer to a variable of this type is then passed to the driver API
 * functions.
 */
typedef struct {
	u32 BaseAddress;	/* Base address of ACE device */
	u32 IsReady;		/* Device is initialized and ready */

	/* interrupt-related data */
	int NumRequested;	/* Number of bytes to read/write */
	int NumRemaining;	/* Number of bytes left to read/write */
	u8 *BufferPtr;		/* Buffer being read/written */
	XSysAce_EventHandler EventHandler;	/* Callback for asynchronous events */
	void *EventRef;		/* Callback reference */

} XSysAce;


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/

/*
 * Initialization functions in xsysace_sinit.c
 */
int XSysAce_Initialize(XSysAce * InstancePtr, u16 DeviceId);
XSysAce_Config *XSysAce_LookupConfig(u16 DeviceId);

/*
 * Required functions in xsysace.c
 */
int XSysAce_CfgInitialize(XSysAce * InstancePtr, XSysAce_Config * Config,
			  u32 EffectiveAddr);
int XSysAce_Lock(XSysAce * InstancePtr, u32 Force);
void XSysAce_Unlock(XSysAce * InstancePtr);
u32 XSysAce_GetErrors(XSysAce * InstancePtr);

/*
 * CompactFlash access functions in xsysace_compactflash.c
 */
int XSysAce_ResetCF(XSysAce * InstancePtr);
int XSysAce_AbortCF(XSysAce * InstancePtr);
int XSysAce_IdentifyCF(XSysAce * InstancePtr, XSysAce_CFParameters * ParamPtr);
u32 XSysAce_IsCFReady(XSysAce * InstancePtr);
int XSysAce_SectorRead(XSysAce * InstancePtr, u32 StartSector,
		       int NumSectors, u8 *BufferPtr);
int XSysAce_SectorWrite(XSysAce * InstancePtr, u32 StartSector,
			int NumSectors, u8 *BufferPtr);
u16 XSysAce_GetFatStatus(XSysAce * InstancePtr);

/*
 * JTAG configuration interface functions in xsysace_jtagcfg.c
 */
void XSysAce_ResetCfg(XSysAce * InstancePtr);
void XSysAce_SetCfgAddr(XSysAce * InstancePtr, unsigned int Address);
void XSysAce_SetStartMode(XSysAce * InstancePtr, u32 ImmedOnReset,
			  u32 SetStart);
u32 XSysAce_IsCfgDone(XSysAce * InstancePtr);
u32 XSysAce_GetCfgSector(XSysAce * InstancePtr);
int XSysAce_ProgramChain(XSysAce * InstancePtr, u8 *BufferPtr, int NumBytes);

/*
 * General interrupt-related functions in xsysace_intr.c
 */
void XSysAce_EnableInterrupt(XSysAce * InstancePtr);
void XSysAce_DisableInterrupt(XSysAce * InstancePtr);
void XSysAce_SetEventHandler(XSysAce * InstancePtr,
			     XSysAce_EventHandler FuncPtr, void *CallBackRef);
void XSysAce_InterruptHandler(void *InstancePtr);	/* interrupt handler */

/*
 * Diagnostic functions in xsysace_selftest.c
 */
int XSysAce_SelfTest(XSysAce * InstancePtr);
u16 XSysAce_GetVersion(XSysAce * InstancePtr);



#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */
