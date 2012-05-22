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
*     FROM ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE FOR OBTAINING
*     ANY THIRD PARTY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION.
*     XILINX EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO
*     THE ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY
*     WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM
*     CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND
*     FITNESS FOR A PARTICULAR PURPOSE.
*
*
*     Xilinx hardware products are not intended for use in life support
*     appliances, devices, or systems. Use in such applications is
*     expressly prohibited.
*
*
*     (c) Copyright 2002-2004 Xilinx Inc.
*     All rights reserved.
*
*
*     You should have received a copy of the GNU General Public License along
*     with this program; if not, write to the Free Software Foundation, Inc.,
*     675 Mass Ave, Cambridge, MA 02139, USA.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xemac.h
*
* The Xilinx Ethernet driver component.  This component supports the Xilinx
* Ethernet 10/100 MAC (EMAC).
*
* The Xilinx Ethernet 10/100 MAC supports the following features:
*   - Simple and scatter-gather DMA operations, as well as simple memory
*     mapped direct I/O interface (FIFOs)
*   - Media Independent Interface (MII) for connection to external
*     10/100 Mbps PHY transceivers
*   - MII management control reads and writes with MII PHYs
*   - Independent internal transmit and receive FIFOs
*   - CSMA/CD compliant operations for half-duplex modes
*   - Programmable PHY reset signal
*   - Unicast, broadcast, multicast, and promiscuous address filtering
*   - Reception of any address that matches a CAM entry.
*   - Internal loopback
*   - Automatic source address insertion or overwrite (programmable)
*   - Automatic FCS insertion and stripping (programmable)
*   - Automatic pad insertion and stripping (programmable)
*   - Pause frame (flow control) detection in full-duplex mode
*   - Programmable interframe gap
*   - VLAN frame support
*   - Pause frame support
*   - Jumbo frame support
*   - Dynamic Re-alignment Engine (DRE) support handled automatically
*
* The device driver supports all the features listed above.
*
* <b>Driver Description</b>
*
* The device driver enables higher layer software (e.g., an application) to
* communicate to the EMAC. The driver handles transmission and reception of
* Ethernet frames, as well as configuration of the controller. It does not
* handle protocol stack functionality such as Link Layer Control (LLC) or the
* Address Resolution Protocol (ARP). The protocol stack that makes use of the
* driver handles this functionality. This implies that the driver is simply a
* pass-through mechanism between a protocol stack and the EMAC. A single device
* driver can support multiple EMACs.
*
* The driver is designed for a zero-copy buffer scheme. That is, the driver will
* not copy buffers. This avoids potential throughput bottlenecks within the
* driver.
*
* Since the driver is a simple pass-through mechanism between a protocol stack
* and the EMAC, no assembly or disassembly of Ethernet frames is done at the
* driver-level. This assumes that the protocol stack passes a correctly
* formatted Ethernet frame to the driver for transmission, and that the driver
* does not validate the contents of an incoming frame
*
* <b>Buffer Alignment</b>
*
* It is important to note that when using direct FIFO communication (either
* polled or interrupt-driven), packet buffers must be 32-bit aligned. When
* using DMA without DRE and the OPB 10/100 Ethernet core, packet buffers
* must be 32-bit aligned. When using DMA without DRE and the PLB 10/100
* Ethernet core, packet buffers must be 64-bit aligned.
*
* When using scatter-gather DMA, the buffer descriptors must be 32-bit
* aligned (for either the OPB or the PLB core). The driver may not enforce
* this alignment so it is up to the user to guarantee the proper alignment.
*
* When DRE is available in the DMA engine, only the buffer descriptors must
* be aligned, the actual buffers do not need to be aligned to any particular
* addressing convention, the DRE takes care of that in hardware.
*
* <b>Receive Address Filtering</b>
*
* The device can be set to accept frames whose destination MAC address:
*
*   - Match the station MAC address (see XEmac_SetMacAddress())
*   - Match the broadcast MAC address (see XEM_BROADCAST_OPTION)
*   - Match any multicast MAC address (see XEM_MULTICAST_OPTION)
*   - Match any one of the 64 possible CAM addresses (see XEmac_MulticastAdd()
*     and XEM_MULTICAST_CAM_OPTION). The CAM is optional.
*   - Match any MAC address (see XEM_PROMISC_OPTION)
*
* <b>PHY Communication</b>
*
* The driver provides rudimentary read and write functions to allow the higher
* layer software to access the PHY. The EMAC provides MII registers for the
* driver to access. This management interface can be parameterized away in the
* FPGA implementation process. If this is the case, the PHY read and write
* functions of the driver return XST_NO_FEATURE.
*
* External loopback is usually supported at the PHY. It is up to the user to
* turn external loopback on or off at the PHY. The driver simply provides pass-
* through functions for configuring the PHY. The driver does not read, write,
* or reset the PHY on its own. All control of the PHY must be done by the user.
*
* <b>Asynchronous Callbacks</b>
*
* The driver services interrupts and passes Ethernet frames to the higher layer
* software through asynchronous callback functions. When using the driver
* directly (i.e., not with the RTOS protocol stack), the higher layer
* software must register its callback functions during initialization. The
* driver requires callback functions for received frames, for confirmation of
* transmitted frames, and for asynchronous errors.
*
* <b>Interrupts</b>
*
* The driver has no dependencies on the interrupt controller. The driver
* provides two interrupt handlers.  XEmac_IntrHandlerDma() handles interrupts
* when the EMAC is configured with scatter-gather DMA.  XEmac_IntrHandlerFifo()
* handles interrupts when the EMAC is configured for direct FIFO I/O or simple
* DMA.  Either of these routines can be connected to the system interrupt
* controller by the user.
*
* <b>Interrupt Frequency</b>
*
* When the EMAC is configured with scatter-gather DMA, the frequency of
* interrupts can be controlled with the interrupt coalescing features of the
* scatter-gather DMA engine. The frequency of interrupts can be adjusted using
* the driver API functions for setting the packet count threshold and the packet
* wait bound values.
*
* The scatter-gather DMA engine only interrupts when the packet count threshold
* is reached, instead of interrupting for each packet. A packet is a generic
* term used by the scatter-gather DMA engine, and is equivalent to an Ethernet
* frame in our case.
*
* The packet wait bound is a timer value used during interrupt coalescing to
* trigger an interrupt when not enough packets have been received to reach the
* packet count threshold.
*
* These values can be tuned by the user to meet their needs. If there appear to
* be interrupt latency problems or delays in packet arrival that are longer than
* might be expected, the user should verify that the packet count threshold is
* set low enough to receive interrupts before the wait bound timer goes off.
*
* <b>Device Reset</b>
*
* Some errors that can occur in the device require a device reset. These errors
* are listed in the XEmac_SetErrorHandler() function header. The user's error
* handler is responsible for resetting the device and re-configuring it based on
* its needs (the driver does not save the current configuration). When
* integrating into an RTOS, these reset and re-configure obligations are
* taken care of by the Xilinx adapter software if it exists for that RTOS.
*
* <b>Device Configuration</b>
*
* The device can be configured in various ways during the FPGA implementation
* process.  Configuration parameters are stored in the xemac_g.c files.
* A table is defined where each entry contains configuration information
* for an EMAC device.  This information includes such things as the base address
* of the memory-mapped device, the base addresses of IPIF, DMA, and FIFO modules
* within the device, and whether the device has DMA, counter registers,
* multicast support, MII support, and flow control.
*
* The driver tries to use the features built into the device. So if, for
* example, the hardware is configured with scatter-gather DMA, the driver
* expects to start the scatter-gather channels and expects that the user has set
* up the buffer descriptor lists already. If the user expects to use the driver
* in a mode different than how the hardware is configured, the user should
* modify the configuration table to reflect the mode to be used. Modifying the
* configuration table is a workaround for now until we get some experience with
* how users are intending to use the hardware in its different configurations.
* For example, if the hardware is built with scatter-gather DMA but the user is
* intending to use only simple DMA, the user either needs to modify the config
* table as a workaround or rebuild the hardware with only simple DMA. The
* recommendation at this point is to build the hardware with the features you
* intend to use. If you're inclined to modify the table, do so before the call
* to XEmac_Initialize().  Here is a snippet of code that changes a device to
* simple DMA (the hardware needs to have DMA for this to work of course):
* <pre>
*        XEmac_Config *ConfigPtr;
*
*        ConfigPtr = XEmac_LookupConfig(DeviceId);
*        ConfigPtr->IpIfDmaConfig = XEM_CFG_SIMPLE_DMA;
* </pre>
*
* <b>Simple DMA</b>
*
* Simple DMA is supported through the FIFO functions, FifoSend and FifoRecv, of
* the driver (i.e., there is no separate interface for it). The driver makes use
* of the DMA engine for a simple DMA transfer if the device is configured with
* DMA, otherwise it uses the FIFOs directly. While the simple DMA interface is
* therefore transparent to the user, the caching of network buffers is not.
* If the device is configured with DMA and the FIFO interface is used, the user
* must ensure that the network buffers are not cached or are cache coherent,
* since DMA will be used to transfer to and from the Emac device. If the device
* is configured with DMA and the user really wants to use the FIFOs directly,
* the user should rebuild the hardware without DMA. If unable to do this, there
* is a workaround (described above in Device Configuration) to modify the
* configuration table of the driver to fake the driver into thinking the device
* has no DMA. A code snippet follows:
* <pre>
*        XEmac_Config *ConfigPtr;
*
*        ConfigPtr = XEmac_LookupConfig(DeviceId);
*        ConfigPtr->IpIfDmaConfig = XEM_CFG_NO_DMA;
* </pre>
*
* <b>Asserts</b>
*
* Asserts are used within all Xilinx drivers to enforce constraints on argument
* values. Asserts can be turned off on a system-wide basis by defining, at
* compile time, the NDEBUG identifier. By default, asserts are turned on and it
* is recommended that users leave asserts on during development.
*
* <b>Building the driver</b>
*
* The XEmac driver is composed of several source files. Why so many?  This
* allows the user to build and link only those parts of the driver that are
* necessary. Since the EMAC hardware can be configured in various ways (e.g.,
* with or without DMA), the driver too can be built with varying features.
* For the most part, this means that besides always linking in xemac.c, you
* link in only the driver functionality you want. Some of the choices you have
* are polled vs. interrupt, interrupt with FIFOs only vs. interrupt with DMA,
* self-test diagnostics, and driver statistics. Note that currently the DMA code
* must be linked in, even if you don't have DMA in the device.
*
* @note
*
* Xilinx drivers are typically composed of two components, one is the driver
* and the other is the adapter.  The driver is independent of OS and processor
* and is intended to be highly portable.  The adapter is OS-specific and
* facilitates communication between the driver and an OS.
* <br><br>
* This driver is intended to be RTOS and processor independent.  It works
* with physical addresses only.  Any needs for dynamic memory management,
* threads or thread mutual exclusion, virtual memory, or cache control must
* be satisfied by the layer above this driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00a rpm  07/31/01 First release
* 1.00b rpm  02/20/02 Repartitioned files and functions
* 1.00b rpm  10/08/02 Replaced HasSgDma boolean with IpifDmaConfig enumerated
*                     configuration parameter
* 1.00c rpm  12/05/02 New version includes support for simple DMA and the delay
*                     argument to SgSend
* 1.00c rpm  02/03/03 The XST_DMA_SG_COUNT_EXCEEDED return code was removed
*                     from SetPktThreshold in the internal DMA driver. Also
*                     avoided compiler warnings by initializing Result in the
*                     DMA interrupt service routines.
* 1.00d rpm  09/26/03 New version includes support PLB Ethernet and v2.00a of
*                     the packet fifo driver. Also supports multicast option.
* 1.00e rmm  04/06/04 SGEND option added, Zero instance memory on init. Changed
*                     SG DMA callback invokation from once per packet to once
*                     for all packets received for an interrupt event. Added
*                     XEmac_GetSgRecvFreeDesc() and GetSgSendFreeDesc()
*                     functions. Moved some IFG and PHY constants to xemac_l.h.
* 1.00f rmm  10/19/04 Added programmable CAM address filtering. Added jumbo
*                     frame support. Added XEmac_PhyReset() function.
* 1.01a ecm  09/01/05 Added DRE support through Control words in instance which
*                     are set at initialization.
*                     Added the config structure items to support separate
*                     Tx and Rx capabilities..
* 1.01a wgr  09/14/06 Ported to Linux 2.6
* 1.11a wgr  03/22/07 Converted to new coding style.
* </pre>
*
******************************************************************************/

#ifndef XEMAC_H			/* prevent circular inclusions */
#define XEMAC_H			/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xstatus.h"
#include "xpacket_fifo_v2_00_a.h"	/* Uses v2.00a of Packet Fifo */
#include "xdma_channel.h"

/************************** Constant Definitions *****************************/

/*
 * Device information
 */
#define XEM_DEVICE_NAME     "xemac"
#define XEM_DEVICE_DESC     "Xilinx Ethernet 10/100 MAC"

/** @name Configuration options
 *
 * Device configuration options (see the XEmac_SetOptions() and
 * XEmac_GetOptions() for information on how to use these options)
 * @{
 */
#define XEM_UNICAST_OPTION        0x00000001UL /**< Unicast addressing
                                                    (defaults on) */
#define XEM_BROADCAST_OPTION      0x00000002UL /**< Broadcast addressing
                                                    (defaults on) */
#define XEM_PROMISC_OPTION        0x00000004UL /**< Promiscuous addressing
                                                    (defaults off) */
#define XEM_FDUPLEX_OPTION        0x00000008UL /**< Full duplex mode
                                                    (defaults off) */
#define XEM_POLLED_OPTION         0x00000010UL /**< Polled mode (defaults off) */
#define XEM_LOOPBACK_OPTION       0x00000020UL /**< Internal loopback mode
                                                    (defaults off) */
#define XEM_MULTICAST_OPTION      0x00000040UL /**< Multicast address reception
                                                    (defaults off) */
#define XEM_FLOW_CONTROL_OPTION   0x00000080UL /**< Interpret pause frames in
                                                    full duplex mode (defaults
                                                    off) */
#define XEM_INSERT_PAD_OPTION     0x00000100UL /**< Pad short frames on transmit
                                                    (defaults on) */
#define XEM_INSERT_FCS_OPTION     0x00000200UL /**< Insert FCS (CRC) on transmit
                                                    (defaults on) */
#define XEM_INSERT_ADDR_OPTION    0x00000400UL /**< Insert source address on
                                                    transmit (defaults on) */
#define XEM_OVWRT_ADDR_OPTION     0x00000800UL /**< Overwrite source address on
                                                    transmit. This is only used
                                                    only used if source address
                                                    insertion is on (defaults on) */
#define XEM_NO_SGEND_INT_OPTION   0x00001000UL /**< Disables the SGEND interrupt
                                                    with SG DMA. Setting this
                                                    option to ON may help bulk
                                                    data transfer performance
                                                    when utilizing higher packet
                                                    threshold counts on slower
                                                    systems (default is off) */
#define XEM_STRIP_PAD_FCS_OPTION  0x00002000UL /**< Strip FCS and padding from
                                                    received frames (defaults off) */
#define XEM_JUMBO_OPTION          0x00004000UL /**< Allow reception of Jumbo frames,
                                                    transmission of Jumbo frames is
                                                    always enabled.
                                                    (default is off) */
#define XEM_MULTICAST_CAM_OPTION  0x00008000UL /**< Allow Rx address filtering
                                                    for multicast CAM entries
                                                    (default is off) */
/*@}*/

/*
 * Some default values for interrupt coalescing within the scatter-gather
 * DMA engine.
 */
#define XEM_SGDMA_DFT_THRESHOLD     1	/* Default pkt threshold */
#define XEM_SGDMA_MAX_THRESHOLD     255	/* Maximum pkt theshold */
#define XEM_SGDMA_DFT_WAITBOUND     5	/* Default pkt wait bound (msec) */
#define XEM_SGDMA_MAX_WAITBOUND     1023	/* Maximum pkt wait bound (msec) */

/*
 * Direction identifiers. These are used for setting values like packet
 * thresholds and wait bound for specific channels
 */
#define XEM_SEND    1
#define XEM_RECV    2

/*
 * Arguments to SgSend function to indicate whether to hold off starting
 * the scatter-gather engine.
 */
#define XEM_SGDMA_NODELAY     0	/* start SG DMA immediately */
#define XEM_SGDMA_DELAY       1	/* do not start SG DMA */

/*
 * Constants to determine the configuration of the hardware device. They are
 * used to allow the driver to verify it can operate with the hardware.
 */
#define XEM_CFG_NO_IPIF             0	/* Not supported by the driver */
#define XEM_CFG_NO_DMA              1	/* No DMA */
#define XEM_CFG_SIMPLE_DMA          2	/* Simple DMA */
#define XEM_CFG_DMA_SG              3	/* DMA scatter gather */

#define XEM_MULTI_CAM_ENTRIES       64	/* Number of storable addresses in
					   the CAM */

/*
 * The next few constants help upper layers determine the size of memory
 * pools used for Ethernet buffers and descriptor lists.
 */
#define XEM_MAC_ADDR_SIZE   6	/* six-byte MAC address */
#define XEM_MTU             1500	/* max size of Ethernet frame */
#define XEM_JUMBO_MTU       8982	/* max payload size of jumbo frame */
#define XEM_HDR_SIZE        14	/* size of Ethernet header */
#define XEM_HDR_VLAN_SIZE   18	/* size of Ethernet header with VLAN */
#define XEM_TRL_SIZE        4	/* size of Ethernet trailer (FCS) */
#define XEM_MAX_FRAME_SIZE  (XEM_MTU + XEM_HDR_SIZE + XEM_TRL_SIZE)
#define XEM_MAX_VLAN_FRAME_SIZE  (XEM_MTU + XEM_HDR_VLAN_SIZE + XEM_TRL_SIZE)
#define XEM_MAX_JUMBO_FRAME_SIZE (XEM_JUMBO_MTU + XEM_HDR_SIZE + XEM_TRL_SIZE)

/*
 * Define a default number of send and receive buffers
 */
#define XEM_MIN_RECV_BUFS   32	/* minimum # of recv buffers */
#define XEM_DFT_RECV_BUFS   64	/* default # of recv buffers */

#define XEM_MIN_SEND_BUFS   16	/* minimum # of send buffers */
#define XEM_DFT_SEND_BUFS   32	/* default # of send buffers */

#define XEM_MIN_BUFFERS     (XEM_MIN_RECV_BUFS + XEM_MIN_SEND_BUFS)
#define XEM_DFT_BUFFERS     (XEM_DFT_RECV_BUFS + XEM_DFT_SEND_BUFS)

/*
 * Define the number of send and receive buffer descriptors, used for
 * scatter-gather DMA
 */
#define XEM_MIN_RECV_DESC   16	/* minimum # of recv descriptors */
#define XEM_DFT_RECV_DESC   32	/* default # of recv descriptors */

#define XEM_MIN_SEND_DESC   8	/* minimum # of send descriptors */
#define XEM_DFT_SEND_DESC   16	/* default # of send descriptors */


/**************************** Type Definitions *******************************/

/**
 * Ethernet statistics (see XEmac_GetStats() and XEmac_ClearStats())
 */
typedef struct {
	u32 XmitFrames;		 /**< Number of frames transmitted */
	u32 XmitBytes;		 /**< Number of bytes transmitted */
	u32 XmitLateCollisionErrors;
				 /**< Number of transmission failures
                                          due to late collisions */
	u32 XmitExcessDeferral;	 /**< Number of transmission failures
                                          due o excess collision deferrals */
	u32 XmitOverrunErrors;	 /**< Number of transmit overrun errors */
	u32 XmitUnderrunErrors;	 /**< Number of transmit underrun errors */
	u32 RecvFrames;		 /**< Number of frames received */
	u32 RecvBytes;		 /**< Number of bytes received */
	u32 RecvFcsErrors;	 /**< Number of frames discarded due
                                          to FCS errors */
	u32 RecvAlignmentErrors; /**< Number of frames received with
                                          alignment errors */
	u32 RecvOverrunErrors;	 /**< Number of frames discarded due
                                          to overrun errors */
	u32 RecvUnderrunErrors;	 /**< Number of recv underrun errors */
	u32 RecvMissedFrameErrors;
				 /**< Number of frames missed by MAC */
	u32 RecvCollisionErrors; /**< Number of frames discarded due
                                          to collisions */
	u32 RecvLengthFieldErrors;
				 /**< Number of frames discarded with
                                          invalid length field */
	u32 RecvShortErrors;	 /**< Number of short frames discarded */
	u32 RecvLongErrors;	 /**< Number of long frames discarded */
	u32 DmaErrors;		 /**< Number of DMA errors since init */
	u32 FifoErrors;		 /**< Number of FIFO errors since init */
	u32 RecvInterrupts;	 /**< Number of receive interrupts */
	u32 XmitInterrupts;	 /**< Number of transmit interrupts */
	u32 EmacInterrupts;	 /**< Number of MAC (device) interrupts */
	u32 TotalIntrs;		 /**< Total interrupts */
} XEmac_Stats;

/**
 * This typedef contains configuration information for a device.
 */
typedef struct {
	u16 DeviceId;	    /**< Unique ID  of device */
	u32 BaseAddress;    /**< Register base address */
	u32 HasCounters;   /**< Does device have counters? */
	u8 IpIfDmaConfig;   /**< IPIF/DMA hardware configuration */
	u32 HasMii;	   /**< Does device support MII? */
	u32 HasCam;	   /**< Does device have multicast CAM */
	u32 HasJumbo;	   /**< Can device transfer jumbo frames */
	u32 TxDre;	   /**< Has data realignment engine on TX channel */
	u32 RxDre;	   /**< Has data realignment engine on RX channel */
	u32 TxHwCsum;	   /**< Has checksum offload on TX channel */
	u32 RxHwCsum;	   /**< Has checksum offload on RX channel */
} XEmac_Config;


/** @name Typedefs for callbacks
 * Callback functions.
 * @{
 */
/**
 * Callback when data is sent or received with scatter-gather DMA.
 *
 * @param CallBackRef is a callback reference passed in by the upper layer
 *        when setting the callback functions, and passed back to the upper
 *        layer when the callback is invoked.
 * @param BdPtr is a pointer to the first buffer descriptor in a list of
 *        buffer descriptors.
 * @param NumBds is the number of buffer descriptors in the list pointed
 *        to by BdPtr.
 */
typedef void (*XEmac_SgHandler) (void *CallBackRef, XBufDescriptor * BdPtr,
				 u32 NumBds);

/**
 * Callback when data is sent or received with direct FIFO communication or
 * simple DMA. The user typically defines two callacks, one for send and one
 * for receive.
 *
 * @param CallBackRef is a callback reference passed in by the upper layer
 *        when setting the callback functions, and passed back to the upper
 *        layer when the callback is invoked.
 */
typedef void (*XEmac_FifoHandler) (void *CallBackRef);

/**
 * Callback when an asynchronous error occurs.
 *
 * @param CallBackRef is a callback reference passed in by the upper layer
 *        when setting the callback functions, and passed back to the upper
 *        layer when the callback is invoked.
 * @param ErrorCode is a Xilinx error code defined in xstatus.h.  Also see
 *        XEmac_SetErrorHandler() for a description of possible errors.
 */
typedef void (*XEmac_ErrorHandler) (void *CallBackRef, int ErrorCode);

/*@}*/

/**
 * The XEmac driver instance data. The user is required to allocate a
 * variable of this type for every EMAC device in the system. A pointer
 * to a variable of this type is then passed to the driver API functions.
 */
typedef struct {
	XEmac_Config Config;	/* Configuration table entry */

	u32 BaseAddress;	/* Base address (of IPIF) */
	u32 PhysAddress;	/* Base address, physical  (of IPIF) */
	u32 IsStarted;		/* Device is currently started */
	u32 IsReady;		/* Device is initialized and ready */
	u32 TxDmaControlWord;	/* TX SGDMA channel control word */
	u32 RxDmaControlWord;	/* RX SGDMA channel control word */
	u32 IsPolled;		/* Device is in polled mode */
	u32 HasMulticastHash;	/* Does device support multicast hash table? */

	XEmac_Stats Stats;
	XPacketFifoV200a RecvFifo;	/* FIFO used to receive frames */
	XPacketFifoV200a SendFifo;	/* FIFO used to send frames */

	/*
	 * Callbacks
	 */
	XEmac_FifoHandler FifoRecvHandler;	/* for non-DMA/simple DMA interrupts */
	void *FifoRecvRef;
	XEmac_FifoHandler FifoSendHandler;	/* for non-DMA/simple DMA interrupts */
	void *FifoSendRef;
	XEmac_ErrorHandler ErrorHandler;	/* for asynchronous errors */
	void *ErrorRef;

	XDmaChannel RecvChannel;	/* DMA receive channel driver */
	XDmaChannel SendChannel;	/* DMA send channel driver */
	u32 IsSgEndDisable;	/* Does SG DMA enable SGEND interrupt */

	XEmac_SgHandler SgRecvHandler;	/* callback for scatter-gather DMA */
	void *SgRecvRef;
	XEmac_SgHandler SgSendHandler;	/* callback for scatter-gather DMA */
	void *SgSendRef;
} XEmac;


/***************** Macros (Inline Functions) Definitions *********************/
/*****************************************************************************/
/**
*
* This macro determines if the device is currently configured for
* scatter-gather DMA.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured for scatter-gather DMA, or FALSE
* if it is not.
*
* @note
*
* Signature: u32 XEmac_mIsSgDma(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mIsSgDma(InstancePtr) \
            ((InstancePtr)->Config.IpIfDmaConfig == XEM_CFG_DMA_SG)

/*****************************************************************************/
/**
*
* This macro determines if the device is currently configured for simple DMA.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured for simple DMA, or FALSE otherwise
*
* @note
*
* Signature: u32 XEmac_mIsSimpleDma(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mIsSimpleDma(InstancePtr) \
            ((InstancePtr)->Config.IpIfDmaConfig == XEM_CFG_SIMPLE_DMA)

/*****************************************************************************/
/**
*
* This macro determines if the device is currently configured with DMA (either
* simple DMA or scatter-gather DMA)
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured with DMA, or FALSE otherwise
*
* @note
*
* Signature: u32 XEmac_mIsDma(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mIsDma(InstancePtr) \
            (XEmac_mIsSimpleDma(InstancePtr) || XEmac_mIsSgDma(InstancePtr))

/*****************************************************************************/
/**
*
* This macro determines if the device has CAM option for storing additional
* receive filters for multicast or unicast addresses.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured with the CAM, or FALSE otherwise
*
* @note
*
* Signature: u32 XEmac_mHasCam(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mHasCam(InstancePtr) \
    (((InstancePtr)->ConfigPtr->HasCam == 1) ? TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device has the MII option for communications
* with a PHY.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured with MII, or FALSE otherwise
*
* @note
*
* Signature: u32 XEmac_mHasMii(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mHasMii(InstancePtr) \
    (((InstancePtr)->Config.HasMii == 1) ? TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device has the option to transfer jumbo sized
* frames.
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured with jubmo frame capability, or
* FALSE otherwise
*
* @note
*
* Signature: u32 XEmac_mHasJumbo(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mHasJumbo(InstancePtr) \
    (((InstancePtr)->ConfigPtr->HasJumbo == 1) ? TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device is configured with the Data Realignment
* Engine (DRE)
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured with TX DRE, or FALSE otherwise.
* Note that earlier versions do not have DRE capability so this macro always
* returns FALSE.
*
* @note
*
* Signature: u32 XEmac_mIsTxDre(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mIsTxDre(InstancePtr)                                         \
    (((InstancePtr)->Config.TxDre != 0) ? TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device is configured with the Data Realignment
* Engine (DRE)
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured with RX DRE, or FALSE otherwise.
* Note that earlier versions do not have DRE capability so this macro always
* returns FALSE.
*
* @note
*
* Signature: u32 XEmac_mIsRxDre(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mIsRxDre(InstancePtr)                                         \
    (((InstancePtr)->Config.RxDre != 0) ? TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device is configured with the Checksum offload
* functionality
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured with TX CSum, or FALSE otherwise.
* Note that earlier versions do not have CSum capability so this macro always
* returns FALSE.
*
* @note
*
* Signature: u32 XEmac_mIsTxHwCsum(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mIsTxHwCsum(InstancePtr)                                         \
    (((InstancePtr)->Config.TxHwCsum == 1) ? TRUE : FALSE)
/*****************************************************************************/
/**
*
* This macro determines if the device is configured with the Checksum offload
* functionality
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured with RX CSum, or FALSE otherwise.
* Note that earlier versions do not have CSum capability so this macro always
* returns FALSE.
*
* @note
*
* Signature: u32 XEmac_mIsRxHwCsum(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mIsRxHwCsum(InstancePtr)                                         \
    (((InstancePtr)->Config.RxHwCsum == 1) ? TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro enables the TxHwCsum for the EMAC
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* none
*
* @note
*
* Signature: void XEmac_mEnableTxHwCsum(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mEnableTxHwCsum(InstancePtr)                                    \
        ((InstancePtr)->TxDmaControlWord |= XDC_DMACR_CS_OFFLOAD_MASK);

/*****************************************************************************/
/**
*
* This macro disables the TxHwCsum for the EMAC
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* none
*
* @note
*
* Signature: void XEmac_mDisableTxHwCsum(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mDisableTxHwCsum(InstancePtr)                                    \
        ((InstancePtr)->TxDmaControlWord &= ~XDC_DMACR_CS_OFFLOAD_MASK);

/*****************************************************************************/
/**
*
* This macro disables the Global interrupt for the EMAC
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* none
*
* @note
*
* Signature: void XEmac_mDisableGIE(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mDisableGIE(InstancePtr)                                           \
    XIIF_V123B_GINTR_DISABLE((InstancePtr)->BaseAddress);

/*****************************************************************************/
/**
*
* This macro enables the Global interrupt for the EMAC
*
* @param InstancePtr is a pointer to the XEmac instance to be worked on.
*
* @return
*
* none
*
* @note
*
* Signature: void XEmac_mEnableGIE(XEmac *InstancePtr)
*
******************************************************************************/
#define XEmac_mEnableGIE(InstancePtr)                                           \
    XIIF_V123B_GINTR_ENABLE((InstancePtr)->BaseAddress);

/************************** Function Prototypes ******************************/

int XEmac_CfgInitialize(XEmac * InstancePtr, XEmac_Config * CfgPtr,
			u32 VirtualAddress);
int XEmac_Start(XEmac * InstancePtr);
int XEmac_Stop(XEmac * InstancePtr);
void XEmac_Reset(XEmac * InstancePtr);

/*
 * Diagnostic functions in xemac_selftest.c
 */
int XEmac_SelfTest(XEmac * InstancePtr);

/*
 * Polled functions in xemac_polled.c
 */
int XEmac_PollSend(XEmac * InstancePtr, u8 *BufPtr, u32 ByteCount);
int XEmac_PollRecv(XEmac * InstancePtr, u8 *BufPtr, u32 *ByteCountPtr);

/*
 * Interrupts with scatter-gather DMA functions in xemac_intr_dma.c
 */
int XEmac_SgSend(XEmac * InstancePtr, XBufDescriptor * BdPtr, int Delay);
int XEmac_SgRecv(XEmac * InstancePtr, XBufDescriptor * BdPtr);
int XEmac_SetPktThreshold(XEmac * InstancePtr, u32 Direction, u8 Threshold);
int XEmac_GetPktThreshold(XEmac * InstancePtr, u32 Direction, u8 *ThreshPtr);
int XEmac_SetPktWaitBound(XEmac * InstancePtr, u32 Direction, u32 TimerValue);
int XEmac_GetPktWaitBound(XEmac * InstancePtr, u32 Direction, u32 *WaitPtr);
int XEmac_SetSgRecvSpace(XEmac * InstancePtr, u32 *MemoryPtr,
			 u32 ByteCount, void *PhyPtr);
int XEmac_SetSgSendSpace(XEmac * InstancePtr, u32 *MemoryPtr,
			 u32 ByteCount, void *PhyPtr);
void XEmac_SetSgRecvHandler(XEmac * InstancePtr, void *CallBackRef,
			    XEmac_SgHandler FuncPtr);
void XEmac_SetSgSendHandler(XEmac * InstancePtr, void *CallBackRef,
			    XEmac_SgHandler FuncPtr);
unsigned XEmac_GetSgSendFreeDesc(XEmac * InstancePtr);
unsigned XEmac_GetSgRecvFreeDesc(XEmac * InstancePtr);

void XEmac_IntrHandlerDma(void *InstancePtr);	/* interrupt handler */

/*
 * Interrupts with direct FIFO functions in xemac_intr_fifo.c. Also used
 * for simple DMA.
 */
int XEmac_FifoSend(XEmac * InstancePtr, u8 *BufPtr, u32 ByteCount);
int XEmac_FifoRecv(XEmac * InstancePtr, u8 *BufPtr, u32 *ByteCountPtr);
void XEmac_SetFifoRecvHandler(XEmac * InstancePtr, void *CallBackRef,
			      XEmac_FifoHandler FuncPtr);
void XEmac_SetFifoSendHandler(XEmac * InstancePtr, void *CallBackRef,
			      XEmac_FifoHandler FuncPtr);

void XEmac_IntrHandlerFifo(void *InstancePtr);	/* interrupt handler */

/*
 * General interrupt-related functions in xemac_intr.c
 */
void XEmac_SetErrorHandler(XEmac * InstancePtr, void *CallBackRef,
			   XEmac_ErrorHandler FuncPtr);

/*
 * MAC configuration in xemac_options.c
 */
int XEmac_SetOptions(XEmac * InstancePtr, u32 OptionFlag);
u32 XEmac_GetOptions(XEmac * InstancePtr);
int XEmac_SetMacAddress(XEmac * InstancePtr, u8 *AddressPtr);
void XEmac_GetMacAddress(XEmac * InstancePtr, u8 *BufferPtr);
int XEmac_SetInterframeGap(XEmac * InstancePtr, u8 Part1, u8 Part2);
void XEmac_GetInterframeGap(XEmac * InstancePtr, u8 *Part1Ptr, u8 *Part2Ptr);

/*
 * Multicast functions in xemac_multicast.c
 */
int XEmac_MulticastAdd(XEmac * InstancePtr, u8 *AddressPtr, int Entry);
int XEmac_MulticastClear(XEmac * InstancePtr, int Entry);

/*
 * PHY configuration in xemac_phy.c
 */
void XEmac_PhyReset(XEmac * InstancePtr);
int XEmac_PhyRead(XEmac * InstancePtr, u32 PhyAddress,
		  u32 RegisterNum, u16 *PhyDataPtr);
int XEmac_PhyWrite(XEmac * InstancePtr, u32 PhyAddress,
		   u32 RegisterNum, u16 PhyData);

/*
 * Statistics in xemac_stats.c
 */
void XEmac_GetStats(XEmac * InstancePtr, XEmac_Stats * StatsPtr);
void XEmac_ClearStats(XEmac * InstancePtr);


#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */
