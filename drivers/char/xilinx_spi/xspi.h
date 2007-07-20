/* $Id: xspi.h,v 1.1 2007/04/04 18:31:38 wre Exp $ */
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
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xspi.h
*
* This component contains the implementation of the XSpi component. It is the
* driver for an SPI master or slave device. User documentation for the driver
* functions is contained in this file in the form of comment blocks at the
* front of each function.
*
* SPI is a 4-wire serial interface. It is a full-duplex, synchronous bus that
* facilitates communication between one master and one slave. The device is
* always full-duplex, which means that for every byte sent, one is received, and
* vice-versa. The master controls the clock, so it can regulate when it wants
* to send or receive data. The slave is under control of the master, it must
* respond quickly since it has no control of the clock and must send/receive
* data as fast or as slow as the master does.
*
* The application software between master and slave must implement a higher
* layer protocol so that slaves know what to transmit to the master and when.
*
* <b>Initialization & Configuration</b>
*
* The XSpi_Config structure is used by the driver to configure itself. This
* configuration structure is typically created by the tool-chain based on HW
* build properties.
*
* To support multiple runtime loading and initialization strategies employed
* by various operating systems, the driver instance can be initialized in one
* of the following ways:
*
*   - XSpi_Initialize(InstancePtr, DeviceId) - The driver looks up its own
*     configuration structure created by the tool-chain based on an ID provided
*     by the tool-chain.
*
*   - XSpi_CfgInitialize(InstancePtr, CfgPtr, EffectiveAddr) - Uses a
*     configuration structure provided by the caller. If running in a system
*     with address translation, the provided virtual memory base address
*     replaces the physical address present in the configuration structure.
*
* <b>Multiple Masters</b>
*
* More than one master can exist, but arbitration is the responsibility of the
* higher layer software. The device driver does not perform any type of
* arbitration.
*
* <b>Multiple Slaves</b>
*
* Multiple slaves are supported by adding additional slave select (SS) signals
* to each device, one for each slave on the bus. The driver ensures that only
* one slave can be selected at any one time.
*
* <b>FIFOs</b>
*
* The SPI hardware is parameterized such that it can be built with or without
* FIFOs. When using FIFOs, both send and receive must have FIFOs. The driver
* will not function correctly if one direction has a FIFO but the other
* direction does not. The frequency of the interrupts which occur is
* proportional to the data rate such that high data rates without the FIFOs
* could cause the software to consume large amounts of processing time. The
* driver is designed to work with or without the FIFOs.
*
* <b>Interrupts</b>
*
* The user must connect the interrupt handler of the driver,
* XSpi_InterruptHandler to an interrupt system such that it will be called when
* an interrupt occurs. This function does not save and restore the processor
* context such that the user must provide this processing.
*
* The driver handles the following interrupts:
* - Data Transmit Register/FIFO Empty
* - Data Transmit Register/FIFO Underrun
* - Data Receive Register/FIFO Overrun
* - Mode Fault Error
* - Slave Mode Fault Error
*
* The Data Transmit Register/FIFO Empty interrupt indicates that the SPI device
* has transmitted all the data available to transmit, and now its data register
* (or FIFO) is empty. The driver uses this interrupt to indicate progress while
* sending data.  The driver may have more data to send, in which case the data
* transmit register (or FIFO) is filled for subsequent transmission. When this
* interrupt arrives and all the data has been sent, the driver invokes the status
* callback with a value of XST_SPI_TRANSFER_DONE to inform the upper layer
* software that all data has been sent.
*
* The Data Transmit Register/FIFO Underrun interrupt indicates that, as slave,
* the SPI device was required to transmit but there was no data available to
* transmit in the transmit register (or FIFO). This may not be an error if the
* master is not expecting data, but in the case where the master is expecting
* data this serves as a notification of such a condition. The driver reports
* this condition to the upper layer software through the status handler.
*
* The Data Receive Register/FIFO Overrun interrupt indicates that the SPI device
* received data and subsequently dropped the data because the data receive
* register (or FIFO) was full. The interrupt applies to both master and slave
* operation. The driver reports this condition to the upper layer software
* through the status handler. This likely indicates a problem with the higher
* layer protocol, or a problem with the slave performance.
*
* The Mode Fault Error interrupt indicates that while configured as a master,
* the device was selected as a slave by another master. This can be used by the
* application for arbitration in a multimaster environment or to indicate a
* problem with arbitration. When this interrupt occurs, the driver invokes the
* status callback with a status value of XST_SPI_MODE_FAULT. It is up to the
* application to resolve the conflict.
*
* The Slave Mode Fault Error interrupt indicates that a slave device was
* selected as a slave by a master, but the slave device was disabled.  This can
* be used during system debugging or by the slave application to learn when the
* slave application has not prepared for a master operation in a timely fashion.
* This likely indicates a problem with the higher layer protocol, or a problem
* with the slave performance.
*
* This driver is currently dependent on the interrupt registers and will not
* function without them.
*
* <b>Polled Operation</b>
*
* There is no support for polled mode of operation.
*
* <b>Device Busy</b>
*
* Some operations are disallowed when the device is busy. The driver tracks
* whether a device is busy. The device is considered busy when a data transfer
* request is outstanding, and is considered not busy only when that transfer
* completes (or is aborted with a mode fault error). This applies to both
* master and slave devices.
*
* <b>Device Configuration</b>
*
* The device can be configured in various ways during the FPGA implementation
* process. Configuration parameters are stored in the xspi_g.c file or passed
* in via _CfgInitialize(). A table is defined where each entry contains
* configuration information for an SPI device. This information includes such
* things as the base address of the memory-mapped device, the base address of
* the IPIF module within the device, the number of slave select bits in the
* device, and whether the device has FIFOs and is configured as slave-only.
*
* <b>RTOS Independence</b>
*
* This driver is intended to be RTOS and processor independent.  It works
* with physical addresses only.  Any needs for dynamic memory management,
* threads or thread mutual exclusion, virtual memory, or cache control must
* be satisfied by the layer above this driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a rpm  10/11/01 First release
* 1.00b jhl  03/14/02 Repartitioned driver for smaller files.
* 1.01a jvb  12/14/05 I separated dependency on the static config table and
*                     xparameters.h from the driver initialization by moving
*                     _Initialize and _LookupConfig to _sinit.c. I also added
*                     the new _CfgInitialize routine.
*
* 1.11a wgr  03/22/07 Converted to new coding style.
* </pre>
*
******************************************************************************/

#ifndef XSPI_H			/* prevent circular inclusions */
#define XSPI_H			/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xstatus.h"

/************************** Constant Definitions *****************************/

/** @name Configuration options
 *
 * The following options may be specified or retrieved for the device and
 * enable/disable additional features of the SPI.  Each of the options
 * are bit fields, so more than one may be specified.
 *
 * @{
 */
/**
 * <pre>
 * The Master option configures the SPI device as a master. By default, the
 * device is a slave.
 *
 * The Active Low Clock option configures the device's clock polarity. Setting
 * this option means the clock is active low and the SCK signal idles high. By
 * default, the clock is active high and SCK idles low.
 *
 * The Clock Phase option configures the SPI device for one of two transfer
 * formats.  A clock phase of 0, the default, means data if valid on the first
 * SCK edge (rising or falling) after the slave select (SS) signal has been
 * asserted. A clock phase of 1 means data is valid on the second SCK edge
 * (rising or falling) after SS has been asserted.
 *
 * The Loopback option configures the SPI device for loopback mode.  Data is
 * looped back from the transmitter to the receiver.
 *
 * The Manual Slave Select option, which is default, causes the device not
 * to automatically drive the slave select.  The driver selects the device
 * at the start of a transfer and deselects it at the end of a transfer.
 * If this option is off, then the device automatically toggles the slave
 * select signal between bytes in a transfer.
 * </pre>
 */
#define XSP_MASTER_OPTION           0x1
#define XSP_CLK_ACTIVE_LOW_OPTION   0x2
#define XSP_CLK_PHASE_1_OPTION      0x4
#define XSP_LOOPBACK_OPTION         0x8
#define XSP_MANUAL_SSELECT_OPTION   0x10
/*@}*/

/**************************** Type Definitions *******************************/

/**
 * The handler data type allows the user to define a callback function to
 * handle the asynchronous processing of the SPI driver.  The application using
 * this driver is expected to define a handler of this type to support interrupt
 * driven mode.  The handler executes in an interrupt context such that minimal
 * processing should be performed.
 *
 *  @param CallBackRef   A callback reference passed in by the upper layer when
 *                       setting the callback functions, and passed back to the
 *                       upper layer when the callback is invoked. Its type is
 *                       unimportant to the driver component, so it is a void
 *                       pointer.
 *  @param StatusEvent   Indicates one or more status events that occurred. See
 *                       the XSpi_SetStatusHandler() for details on the status
 *                       events that can be passed in the callback.
 *  @param ByteCount     Indicates how many bytes of data were successfully
 *                       transferred.  This may be less than the number of bytes
 *                       requested if the status event indicates an error.
 */
typedef void (*XSpi_StatusHandler) (void *CallBackRef, u32 StatusEvent,
				    unsigned int ByteCount);

/**
 * XSpi statistics
 */
typedef struct {
	u32 ModeFaults;	    /**< Number of mode fault errors */
	u32 XmitUnderruns;  /**< Number of transmit underruns */
	u32 RecvOverruns;   /**< Number of receive overruns */
	u32 SlaveModeFaults;/**< Number of selects as a slave while disabled */
	u32 BytesTransferred;
			    /**< Number of bytes transferred */
	u32 NumInterrupts;  /**< Number of transmit/receive interrupts */
} XSpi_Stats;

/**
 * This typedef contains configuration information for the device.
 */
typedef struct {
	u16 DeviceId;	/**< Unique ID  of device */
	u32 BaseAddress;/**< Base address of the device */

	/* Device capabilities */
	int HasFifos;  /**< Does device have FIFOs? */
	int SlaveOnly; /**< Is the device slave only? */
	u8 NumSlaveBits;/**< Number of slave select bits on the device */
} XSpi_Config;

/**
 * The XSpi driver instance data. The user is required to allocate a
 * variable of this type for every SPI device in the system. A pointer
 * to a variable of this type is then passed to the driver API functions.
 */
typedef struct {
	XSpi_Stats Stats;	/* Statistics */
	u32 BaseAddr;		/* Base address of device (IPIF) */
	int IsReady;		/* Device is initialized and ready */
	int IsStarted;		/* Device has been started */
	int HasFifos;		/* Device is configured with FIFOs or not */
	int SlaveOnly;		/* Device is configured to be slave only */
	u8 NumSlaveBits;	/* Number of slave selects for this device */
	u32 SlaveSelectMask;	/* Mask that matches the number of SS bits */
	u32 SlaveSelectReg;	/* Slave select register */

	u8 *SendBufferPtr;	/* Buffer to send (state) */
	u8 *RecvBufferPtr;	/* Buffer to receive (state) */
	unsigned int RequestedBytes;	/* Number of bytes to transfer (state) */
	unsigned int RemainingBytes;	/* Number of bytes left to transfer (state) */
	int IsBusy;		/* A transfer is in progress (state) */

	XSpi_StatusHandler StatusHandler;
	void *StatusRef;	/* Callback reference for status handler */

} XSpi;

/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/

/*
 * Initialization functions in xspi_sinit.c
 */
int XSpi_Initialize(XSpi * InstancePtr, u16 DeviceId);
XSpi_Config *XSpi_LookupConfig(u16 DeviceId);

/*
 * required functions, in xspi.c
 */
int XSpi_CfgInitialize(XSpi * InstancePtr, XSpi_Config * Config,
		       u32 EffectiveAddr);

int XSpi_Start(XSpi * InstancePtr);
int XSpi_Stop(XSpi * InstancePtr);

void XSpi_Reset(XSpi * InstancePtr);

int XSpi_SetSlaveSelect(XSpi * InstancePtr, u32 SlaveMask);
u32 XSpi_GetSlaveSelect(XSpi * InstancePtr);

int XSpi_Transfer(XSpi * InstancePtr, u8 *SendBufPtr, u8 *RecvBufPtr,
		  unsigned int ByteCount);

void XSpi_SetStatusHandler(XSpi * InstancePtr, void *CallBackRef,
			   XSpi_StatusHandler FuncPtr);
void XSpi_InterruptHandler(void *InstancePtr);


/*
 * functions for selftest, in xspi_selftest.c
 */
int XSpi_SelfTest(XSpi * InstancePtr);

/*
 * functions for statistics, in xspi_stats.c
 */
void XSpi_GetStats(XSpi * InstancePtr, XSpi_Stats * StatsPtr);
void XSpi_ClearStats(XSpi * InstancePtr);

/*
 * functions for options, in xspi_options.c
 */
int XSpi_SetOptions(XSpi * InstancePtr, u32 Options);
u32 XSpi_GetOptions(XSpi * InstancePtr);

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */
