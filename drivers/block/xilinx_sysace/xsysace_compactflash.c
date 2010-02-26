/* $Id: xsysace_compactflash.c,v 1.1 2006/02/17 21:52:36 moleres Exp $ */
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
*       (c) Copyright 2002 Xilinx Inc.
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
* @file xsysace_compactflash.c
*
* Contains functions to reset, read, and write the CompactFlash device via
* the System ACE controller.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a rpm  06/17/02 work in progress
* 1.00a ecm  09/17/04 Fixed the endianism issue with the string copies.
*                     Replaced the ByteCopy with WordCopySwap which
*                     copies the bytes and swaps to correct the endianism.
*                     CR 194182
*
* 1.00a ecm  09/27/04 Fixed the lack of reset during read and write in
*                     L1 functions.
*                     CR 194423
*
* 1.00a ecm  12/09/04 Removed the above fix, breaks MVL.
*                     CR 200015
*
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xsysace.h"
#include "xsysace_l.h"

/************************** Constant Definitions *****************************/

/*
 * Indices into the parameter information from the CompactFlash.  When the
 * user calls XSysAce_IdentifyCF(), the parameter information is read into a
 * byte buffer.  The byte buffer is then mapped to a XSysAce_CFParameters
 * structure using these indices into the byte buffer.
 */
#define XSA_CFPARM_SIGNATURE    0
#define XSA_CFPARM_NUMCYLS      2
#define XSA_CFPARM_RESERVED1    4
#define XSA_CFPARM_NUMHEADS     6
#define XSA_CFPARM_BYTES_TRACK  8
#define XSA_CFPARM_BYTES_SECT   10
#define XSA_CFPARM_SECTS_TRK    12
#define XSA_CFPARM_SECTS_HI     14
#define XSA_CFPARM_SECTS_LO     16
#define XSA_CFPARM_VENDOR1      18
#define XSA_CFPARM_SERIAL_NO    20
#define XSA_CFPARM_BUFFER_TYPE  40
#define XSA_CFPARM_BUFFER_SIZE  42
#define XSA_CFPARM_ECC_BYTES    44
#define XSA_CFPARM_FW_VERSION   46
#define XSA_CFPARM_MODEL_NO     54
#define XSA_CFPARM_MAX_SECTORS  94
#define XSA_CFPARM_DBL_WORD     96
#define XSA_CFPARM_CAPS         98
#define XSA_CFPARM_RESERVED2    100
#define XSA_CFPARM_PIO_MODE     102
#define XSA_CFPARM_DMA_MODE     104
#define XSA_CFPARM_TRANSLATE    106
#define XSA_CFPARM_CURCYLS      108
#define XSA_CFPARM_CURHEADS     110
#define XSA_CFPARM_CURSECTS_TRK 112
#define XSA_CFPARM_CURSECTS     114
#define XSA_CFPARM_MULTIPLE     118
#define XSA_CFPARM_LBA_SECTS    120
#define XSA_CFPARM_RESERVED3    124
#define XSA_CFPARM_SECURITY     256
#define XSA_CFPARM_VENDOR2      258
#define XSA_CFPARM_POWER        320
#define XSA_CFPARM_RESERVED4    322

/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/

static void WordCopySwap(u8 *SourcePtr, u8 *DestPtr, int NumBytes);
static void FillParam(XSysAce_CFParameters * ParamPtr, u8 *BufPtr);

/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
*
* Reset the CompactFlash device. This function does not reset the System ACE
* controller.  An ATA soft-reset of the CompactFlash is performed.
*
* An MPU lock, obtained using XSysAce_Lock(), must be granted before calling
* this function. If a lock has not been granted, no action is taken and an
* error is returned.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
*
* @return
*
* - XST_SUCCESS if the reset was done successfully
* - XST_SYSACE_NO_LOCK if no MPU lock has yet been granted
* - XST_DEVICE_BUSY if the CompactFlash is not ready for a command
*
* @note
*
* None.
*
******************************************************************************/
int XSysAce_ResetCF(XSysAce * InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* If a lock has not been granted, return an error */
	if (!XSysAce_mIsMpuLocked(InstancePtr->BaseAddress)) {
		return XST_SYSACE_NO_LOCK;
	}

	/* See if the CF is ready for a command */
	if (!XSysAce_mIsReadyForCmd(InstancePtr->BaseAddress)) {
		return XST_DEVICE_BUSY;
	}

	/*
	 * If interrupts are enabled, enable the error interrupt. A reset clears
	 * the error status, so we're going to re-enable the interrupt here so any
	 * new errors will be caught.
	 */
	if (XSysAce_mIsIntrEnabled(InstancePtr->BaseAddress)) {
		XSysAce_mOrControlReg(InstancePtr->BaseAddress,
				      XSA_CR_ERRORIRQ_MASK);
	}

	/*
	 * Send the reset command
	 */
	XSysAce_RegWrite16(InstancePtr->BaseAddress + XSA_SCCR_OFFSET,
			   XSA_SCCR_RESET_MASK);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Abort the CompactFlash operation currently in progress.
*
* An MPU lock, obtained using XSysAce_Lock(), must be granted before calling
* this function. If a lock has not been granted, no action is taken and an
* error is returned.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
*
* @return
*
* - XST_SUCCESS if the abort was done successfully
* - XST_SYSACE_NO_LOCK if no MPU lock has yet been granted
* - XST_DEVICE_BUSY if the CompactFlash is not ready for a command
*
* @note
*
* According to the ASIC designer, the abort command has not been well tested.
*
******************************************************************************/
int XSysAce_AbortCF(XSysAce * InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* If a lock has not been granted, return an error */
	if (!XSysAce_mIsMpuLocked(InstancePtr->BaseAddress)) {
		return XST_SYSACE_NO_LOCK;
	}

	/*
	 * See if the CF is ready for a command
	 *
	 * TODO: make sure this check works, or possibly the abort can be done
	 * if it is not ready for a command (e.g., that's what we're aborting)?
	 */
	if (!XSysAce_mIsReadyForCmd(InstancePtr->BaseAddress)) {
		return XST_DEVICE_BUSY;
	}

	/*
	 * Send the abort command
	 */
	XSysAce_RegWrite16(InstancePtr->BaseAddress + XSA_SCCR_OFFSET,
			   XSA_SCCR_ABORT_MASK);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Identify the CompactFlash device. Retrieves the parameters for the
* CompactFlash storage device. Note that this is a polled read of one sector
* of data. The data is read from the CompactFlash into a byte buffer, which
* is then copied into the XSysAce_CFParameters structure passed in by the
* user.  The copy is necessary since we don't know how the compiler packs
* the XSysAce_CFParameters structure.
*
* An MPU lock, obtained using XSysAce_Lock(), must be granted before calling
* this function. If a lock has not been granted, no action is taken and an
* error is returned.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
* @param ParamPtr is a pointer to a XSysAce_CFParameters structure where the
*        information for the CompactFlash device will be stored. See xsysace.h
*        for details on the XSysAce_CFParameters structure.
*
* @return
*
* - XST_SUCCESS if the identify was done successfully
* - XST_FAILURE if an error occurs. Use XSysAce_GetErrors() to determine cause.
* - XST_SYSACE_NO_LOCK if no MPU lock has yet been granted
* - XST_DEVICE_BUSY if the CompactFlash is not ready for a command
*
* @note
*
* None.
*
* @internal
*
* The identify command has the same protocol as the read sector command
* according to the CompactFlash specification.  However, there is a discepency
* in that same specification on the size of the parameter structure. The word
* addresses defined in the spec indicate the parameter information is a full
* 512 bytes, the same size as a sector. The total bytes defined in the spec,
* however, indicate that the parameter information is only 500 bytes. We
* defined the parameter structure in xsysace.h assuming the parameters are the
* full 512 bytes since that makes sense, and therefore ignored the "Total
* Bytes" column in the spec.
*
* The SectorData variable was made static to avoid putting 512 bytes on the
* stack every time this function is called.
*
******************************************************************************/
int XSysAce_IdentifyCF(XSysAce * InstancePtr, XSysAce_CFParameters * ParamPtr)
{
	int NumRead;
	u32 InterruptsOn;
	static u8 SectorData[XSA_CF_SECTOR_SIZE];

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(ParamPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* If a lock has not been granted, return an error */
	if (!XSysAce_mIsMpuLocked(InstancePtr->BaseAddress)) {
		return XST_SYSACE_NO_LOCK;
	}

	/* See if the CF is ready for a command */
	if (!XSysAce_mIsReadyForCmd(InstancePtr->BaseAddress)) {
		return XST_DEVICE_BUSY;
	}

	/*
	 * If interrupts are enabled, we disable them because we want to do this
	 * identify in polled mode - due to the buffer endian conversion and copy
	 * that takes place.
	 */
	InterruptsOn = XSysAce_mIsIntrEnabled(InstancePtr->BaseAddress);
	if (InterruptsOn) {
		XSysAce_DisableInterrupt(InstancePtr);
	}

	/*
	 * Send the identify command
	 */
	XSysAce_RegWrite16(InstancePtr->BaseAddress + XSA_SCCR_OFFSET,
			   XSA_SCCR_IDENTIFY_MASK);

	/* Reset configuration controller (be sure to keep the lock) */
	/* This breaks mvl, beware! */
	/* XSysAce_mOrControlReg(InstancePtr->BaseAddress, XSA_CR_CFGRESET_MASK); */

	/*
	 * Read a sector of data from the data buffer. The parameter info is
	 * the same size as a sector.
	 */
	NumRead = XSysAce_ReadDataBuffer(InstancePtr->BaseAddress, SectorData,
					 XSA_CF_SECTOR_SIZE);

	/* Clear reset of configuration controller */
	/* This breaks mvl, beware! */
	/*XSysAce_mAndControlReg(InstancePtr->BaseAddress, ~(XSA_CR_CFGRESET_MASK)); */

	/* If interrupts were on, re-enable interrupts (regardless of error) */
	if (InterruptsOn) {
		XSysAce_EnableInterrupt(InstancePtr);
	}

	if (NumRead == 0) {
		/* an error occurred */
		return XST_FAILURE;
	}

	/*
	 * Copy the byte buffer to the parameter structure
	 */
	FillParam(ParamPtr, SectorData);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Check to see if the CompactFlash is ready for a command. The CompactFlash
* may delay after one operation before it is ready for the next. This function
* helps the user determine when it is ready before invoking a CompactFlash
* operation such as XSysAce_SectorRead() or XSysAce_SectorWrite();
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
*
* @return
*
* TRUE if the CompactFlash is ready for a command, and FALSE otherwise.
*
* @note
*
* None.
*
******************************************************************************/
u32 XSysAce_IsCFReady(XSysAce * InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	return XSysAce_mIsReadyForCmd(InstancePtr->BaseAddress);
}

/*****************************************************************************/
/**
*
* Read at least one sector of data from the CompactFlash. The user specifies
* the starting sector ID and the number of sectors to be read. The minimum unit
* that can be read from the CompactFlash is a sector, which is 512 bytes.
*
* In polled mode, this read is blocking. If there are other tasks in the system
* that must run, it is best to keep the number of sectors to be read to a
* minimum (e.g., 1). In interrupt mode, this read is non-blocking and an event,
* XSA_EVENT_DATA_DONE, is returned to the user in the asynchronous event
* handler when the read is complete. The user must call
* XSysAce_EnableInterrupt() to put the driver/device into interrupt mode.
*
* An MPU lock, obtained using XSysAce_Lock(), must be granted before calling
* this function. If a lock has not been granted, no action is taken and an
* error is returned.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
* @param StartSector is the starting sector ID from where data will be read.
*        Sector IDs range from 0 (first sector) to 0x10000000.
* @param NumSectors is the number of sectors to read. The range can be from
*        1 to 256.
* @param BufferPtr is a pointer to a buffer where the data will be stored.
*        The user must ensure it is big enough to hold (512 * NumSectors) bytes.
*
* @return
*
* - XST_SUCCESS if the read was successful. In interrupt mode, this does not
*   mean the read is complete, only that it has begun. An event is returned
*   to the user when the read is complete.
* - XST_SYSACE_NO_LOCK if no MPU lock has yet been granted
* - XST_DEVICE_BUSY if the ACE controller is not ready for a command
* - XST_FAILURE if an error occurred during the read. The user should call
*   XSysAce_GetErrors() to determine the cause of the error.
*
* @note
*
* None.
*
* @internal
*
* Polled mode is blocking under the assumption that a single sector can be
* transferred at a very fast rate (>20 Mbps).  So, the user can choose to
* transfer only single sectors when in polled mode, thus allowing time for
* other work to be done. The biggest issue is that although data transfer
* rates are high, seek time for CompactFlash cards is slow (5-20 ms on
* average, depending on the type of device). We could move to a non-blocking
* solution that transfers 32 bytes at a time (the entire data buffer) and
* then returns. The user would then need to increment its buffer pointer
* appropriately and call the read/write again. The driver would need some way
* to know not to issue a new command to the CompactFlash, but instead continue
* with the previous command.  This can be done either with a NumSectors argument
* of zero to indicate that there is already an operation in progress, or by
* having the driver keep state to know there is an operation in progress. The
* interface for either seems a bit awkward. Also, the hit for seek time needs
* to be taken regardless of the blocking or non-blocking nature of the call, so
* the additional few microseconds to transfer a sector of data seems acceptable.
*
******************************************************************************/
int XSysAce_SectorRead(XSysAce * InstancePtr, u32 StartSector,
		       int NumSectors, u8 *BufferPtr)
{
	u16 SectorCmd;
	int BytesToRecv;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(NumSectors > 0 &&
			NumSectors <= (XSA_SCCR_COUNT_MASK + 1));
	XASSERT_NONVOID(BufferPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* If a lock has not been granted, return an error */
	if (!XSysAce_mIsMpuLocked(InstancePtr->BaseAddress)) {
		return XST_SYSACE_NO_LOCK;
	}

	/* See if the CF is ready for a command */
	if (!XSysAce_mIsReadyForCmd(InstancePtr->BaseAddress)) {
		return XST_DEVICE_BUSY;
	}

	BytesToRecv = XSA_CF_SECTOR_SIZE * NumSectors;

	/*
	 * If in interrupt mode, set up the state variables and enable the
	 * data-buffer-ready interrupt. This needs to be done before the command
	 * is sent to the ACE, which will cause the interrupt to occur.
	 */
	if (XSysAce_mIsIntrEnabled(InstancePtr->BaseAddress)) {
		InstancePtr->NumRequested = BytesToRecv;
		InstancePtr->NumRemaining = BytesToRecv;
		InstancePtr->BufferPtr = BufferPtr;

		XSysAce_mOrControlReg(InstancePtr->BaseAddress,
				      XSA_CR_DATARDYIRQ_MASK);
	}

	/* Write the sector ID (LBA) */
	XSysAce_RegWrite32(InstancePtr->BaseAddress + XSA_MLR_OFFSET,
			   StartSector);

	/*
	 * Send the read command for the number of sectors specified
	 */
	SectorCmd = (NumSectors & XSA_SCCR_COUNT_MASK) | XSA_SCCR_READDATA_MASK;
	XSysAce_RegWrite16(InstancePtr->BaseAddress + XSA_SCCR_OFFSET,
			   SectorCmd);

	/*
	 * If in polled mode, receive the entire amount requested
	 */
	if (!XSysAce_mIsIntrEnabled(InstancePtr->BaseAddress)) {
		int NumRead;

		/* Reset configuration controller (be sure to keep the lock) */
		/* This breaks mvl, beware! */
		/*XSysAce_mOrControlReg(InstancePtr->BaseAddress, XSA_CR_CFGRESET_MASK); */

		NumRead =
			XSysAce_ReadDataBuffer(InstancePtr->BaseAddress,
					       BufferPtr, BytesToRecv);
		/* Clear reset of configuration controller */
		/* This breaks mvl, beware! */
		/*XSysAce_mAndControlReg(InstancePtr->BaseAddress, ~(XSA_CR_CFGRESET_MASK)); */

		if (NumRead != BytesToRecv) {
			/* an error occurred, report this to the user */
			return XST_FAILURE;
		}
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* Write data to the CompactFlash. The user specifies the starting sector ID
* and the number of sectors to be written. The minimum unit that can be written
* to the CompactFlash is a sector, which is 512 bytes.
*
* In polled mode, this write is blocking. If there are other tasks in the
* system that must run, it is best to keep the number of sectors to be written
* to a minimum (e.g., 1). In interrupt mode, this write is non-blocking and an
* event, XSA_EVENT_DATA_DONE, is returned to the user in the asynchronous
* event handler when the write is complete. The user must call
* XSysAce_EnableInterrupt() to put the driver/device into interrupt mode.
*
* An MPU lock, obtained using XSysAce_Lock(), must be granted before calling
* this function. If a lock has not been granted, no action is taken and an
* error is returned.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
* @param StartSector is the starting sector ID from where data will be written.
*        Sector IDs range from 0 (first sector) to 0x10000000.
* @param NumSectors is the number of sectors to write. The range can be from
*        1 to 256.
* @param BufferPtr is a pointer to the data buffer to be written. This buffer
*        must have at least (512 * NumSectors) bytes.
*
* @return
*
* - XST_SUCCESS if the write was successful. In interrupt mode, this does not
*   mean the write is complete, only that it has begun. An event is returned
*   to the user when the write is complete.
* - XST_SYSACE_NO_LOCK if no MPU lock has yet been granted
* - XST_DEVICE_BUSY if the ACE controller is not ready for a command
* - XST_FAILURE if an error occurred during the write. The user should call
*   XSysAce_GetErrors() to determine the cause of the error.
*
* @note
*
* None.
*
* @internal
*
* Polled mode is blocking under the assumption that a single sector can be
* transferred at a very fast rate (>20 Mbps).  So, the user can choose to
* transfer only single sectors when in polled mode, thus allowing time for
* other work to be done. The biggest issue is that although data transfer
* rates are high, seek time for CompactFlash cards is slow (5-20 ms on
* average, depending on the type of device). We could move to a non-blocking
* solution that transfers 32 bytes at a time (the entire data buffer) and
* then returns. The user would then need to increment its buffer pointer
* appropriately and call the read/write again. The driver would need some way
* to know not to issue a new command to the CompactFlash, but instead continue
* with the previous command.  This can be done either with a NumSectors argument
* of zero to indicate that there is already an operation in progress, or by
* having the driver keep state to know there is an operation in progress. The
* interface for either seems a bit awkward. Also, the hit for seek time needs
* to be taken regardless of the blocking or non-blocking nature of the call, so
* the additional few microseconds to transfer a sector of data seems acceptable.
*
******************************************************************************/
int XSysAce_SectorWrite(XSysAce * InstancePtr, u32 StartSector,
			int NumSectors, u8 *BufferPtr)
{
	u16 SectorCmd;
	int NumSent;
	int BytesToSend;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(NumSectors > 0 &&
			NumSectors <= (XSA_SCCR_COUNT_MASK + 1));
	XASSERT_NONVOID(BufferPtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	/* If a lock has not been granted, return an error */
	if (!XSysAce_mIsMpuLocked(InstancePtr->BaseAddress)) {
		return XST_SYSACE_NO_LOCK;
	}

	/* See if the CF is ready for a command */
	if (!XSysAce_mIsReadyForCmd(InstancePtr->BaseAddress)) {
		return XST_DEVICE_BUSY;
	}

	/* Write the sector ID (LBA) */
	XSysAce_RegWrite32(InstancePtr->BaseAddress + XSA_MLR_OFFSET,
			   StartSector);

	/*
	 * Send the write command for the number of sectors specified
	 */
	SectorCmd =
		(NumSectors & XSA_SCCR_COUNT_MASK) | XSA_SCCR_WRITEDATA_MASK;
	XSysAce_RegWrite16(InstancePtr->BaseAddress + XSA_SCCR_OFFSET,
			   SectorCmd);

	BytesToSend = XSA_CF_SECTOR_SIZE * NumSectors;

	/*
	 * If in interrupt mode, set up the state variables and enable the
	 * data-buffer-ready interrupt. We do this after the write command above
	 * is done in order to guarantee that the interrupt occurs only after the
	 * first data buffer write is done below (an interrupt may or may not occur
	 * after the write command is issued)
	 */
	if (XSysAce_mIsIntrEnabled(InstancePtr->BaseAddress)) {
		/*
		 * Set the state variables. We're going to send one data buffer here in
		 * this routine, so adjust the buffer pointer and number remaining to
		 * reflect this.
		 */
		InstancePtr->NumRequested = BytesToSend;
		InstancePtr->NumRemaining = BytesToSend - XSA_DATA_BUFFER_SIZE;
		InstancePtr->BufferPtr = BufferPtr + XSA_DATA_BUFFER_SIZE;

		/* Send only one data buffer in interrupt mode */
		BytesToSend = XSA_DATA_BUFFER_SIZE;

		XSysAce_mOrControlReg(InstancePtr->BaseAddress,
				      XSA_CR_DATARDYIRQ_MASK);
	}

	NumSent = XSysAce_WriteDataBuffer(InstancePtr->BaseAddress, BufferPtr,
					  BytesToSend);
	if (NumSent != BytesToSend) {
		/* an error occurred, report this to the user */
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}


/*****************************************************************************/
/**
*
* Get the status of the FAT filesystem on the first valid partition of the
* CompactFlash device such as the boot record and FAT types found.
*
* @param InstancePtr is a pointer to the XSysAce instance to be worked on.
*
* @return
*
* A 16-bit mask of status values. These values are defined in xsysace_l.h
* with the prefix XSA_FAT_*.
*
* @note
*
* None.
*
******************************************************************************/
u16 XSysAce_GetFatStatus(XSysAce * InstancePtr)
{
	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	return XSysAce_RegRead16(InstancePtr->BaseAddress + XSA_FSR_OFFSET);
}

/*****************************************************************************/
/**
*
* This bit of ugliness allows us to present a structure to the user. The
* byte buffer which was read from the CompactFlash is converted into the
* XSysAce_CFParameters structure. The byte buffer is accessed by the indices
* of the fields as defined at the top of this file. We do not read from
* CompactFlash directly into the CF Parameter structure because of structure
* packing problems.
*
* Note that we also need to perform endian conversion here since the System
* ACE device gives us little endian data and we're (possibly) on a big endian
* processor.
*
* @param ParamPtr is the structure to fill
* @param BufPtr is the byte buffer containing the CF parameter data
*
* @return
*
* None.
*
* @note
*
* None.
*
******************************************************************************/
static void FillParam(XSysAce_CFParameters * ParamPtr, u8 *BufPtr)
{
	u16 HiWord;
	u16 LoWord;

	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_SIGNATURE]),
			       &ParamPtr->Signature);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_NUMCYLS]),
			       &ParamPtr->NumCylinders);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_RESERVED1]),
			       &ParamPtr->Reserved);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_NUMHEADS]),
			       &ParamPtr->NumHeads);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_BYTES_TRACK]),
			       &ParamPtr->NumBytesPerTrack);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_BYTES_SECT]),
			       &ParamPtr->NumBytesPerSector);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_SECTS_TRK]),
			       &ParamPtr->NumSectorsPerTrack);

	/* NumSectorsPerCard is stored as two half-words, MSW first */
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_SECTS_HI]),
			       &HiWord);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_SECTS_LO]),
			       &LoWord);
	ParamPtr->NumSectorsPerCard = ((u32) HiWord << 16) | (u32) LoWord;

	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_VENDOR1]),
			       &ParamPtr->VendorUnique);

	WordCopySwap(&BufPtr[XSA_CFPARM_SERIAL_NO], ParamPtr->SerialNo, 20);

	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_BUFFER_TYPE]),
			       &ParamPtr->BufferType);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_BUFFER_SIZE]),
			       &ParamPtr->BufferSize);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_ECC_BYTES]),
			       &ParamPtr->NumEccBytes);

	WordCopySwap(&BufPtr[XSA_CFPARM_FW_VERSION], ParamPtr->FwVersion, 8);
	WordCopySwap(&BufPtr[XSA_CFPARM_MODEL_NO], ParamPtr->ModelNo, 40);

	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_MAX_SECTORS]),
			       &ParamPtr->MaxSectors);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_DBL_WORD]),
			       &ParamPtr->DblWord);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_CAPS]),
			       &ParamPtr->Capabilities);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_RESERVED2]),
			       &ParamPtr->Reserved2);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_PIO_MODE]),
			       &ParamPtr->PioMode);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_DMA_MODE]),
			       &ParamPtr->DmaMode);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_TRANSLATE]),
			       &ParamPtr->TranslationValid);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_CURCYLS]),
			       &ParamPtr->CurNumCylinders);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_CURHEADS]),
			       &ParamPtr->CurNumHeads);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_CURSECTS_TRK]),
			       &ParamPtr->CurSectorsPerTrack);
	XIo_FromLittleEndian32(*((u32 *) &BufPtr[XSA_CFPARM_CURSECTS]),
			       &ParamPtr->CurSectorsPerCard);
	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_MULTIPLE]),
			       &ParamPtr->MultipleSectors);
	XIo_FromLittleEndian32(*((u32 *) &BufPtr[XSA_CFPARM_LBA_SECTS]),
			       &ParamPtr->LbaSectors);

	WordCopySwap(&BufPtr[XSA_CFPARM_RESERVED3], ParamPtr->Reserved3, 132);

	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_SECURITY]),
			       &ParamPtr->SecurityStatus);

	WordCopySwap(&BufPtr[XSA_CFPARM_VENDOR2], ParamPtr->VendorUniqueBytes,
		     62);

	XIo_FromLittleEndian16(*((u16 *) &BufPtr[XSA_CFPARM_POWER]),
			       &ParamPtr->PowerDesc);

	WordCopySwap(&BufPtr[XSA_CFPARM_RESERVED4], ParamPtr->Reserved4, 190);

}

/*****************************************************************************/
/**
*
* Utility to copy words and swap the endianism on the fly.
*
* @param SourcePtr is a pointer to the source byte buffer
* @param DestPtr is a pointer to the destination byte buffer
* @param NumBytes is the number of bytes to copy
*
* @return
*
* None.
*
* @note
*
* NumBytes should be even but if it isn't, the function increases by 1
* to correct.
*
******************************************************************************/
static void WordCopySwap(u8 *SourcePtr, u8 *DestPtr, int NumBytes)
{
	int i;

	/* make sure the requested length is even, if not, increase by 1 */

	if ((NumBytes & 0x00000001) != 0) {
		NumBytes += 1;
	}

	for (i = 0; i < NumBytes; i += 2) {
		DestPtr[i + 1] = SourcePtr[i];
		DestPtr[i] = SourcePtr[i + 1];
	}
}
