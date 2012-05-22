/* $Id: */
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
*       (c) Copyright 2005-2006 Xilinx Inc.
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
 * @file xtemac.h
 *
 * The Xilinx Tri-Mode Ethernet driver component. This driver supports the
 * Virtex-4(TM) 10/100/1000 MAC (TEMAC).
 *
 * For a full description of TEMAC features, please see the HW spec. This driver
 * supports the following features:
 *   - Memory mapped access to host interface registers
 *   - API for polled frame transfers (FIFO direct HW configuration only)
 *   - API for interrupt driven frame transfers for HW configured with FIFO
 *     direct, or Scatter Gather DMA
 *   - Virtual memory support
 *   - Unicast, broadcast, and multicast receive address filtering
 *   - Full duplex operation (half duplex not supported)
 *   - Automatic source address insertion or overwrite (programmable)
 *   - Automatic PAD & FCS insertion and stripping (programmable)
 *   - Flow control
 *   - VLAN frame support
 *   - Pause frame support
 *   - Jumbo frame support
 *   - Data Realignment Engine (DRE)
 *   - Checksum offload
 *
 * <b>Driver Description</b>
 *
 * The device driver enables higher layer software (e.g., an application) to
 * communicate to the TEMAC. The driver handles transmission and reception of
 * Ethernet frames, as well as configuration and control. No pre or post
 * processing of frame data is performed. The driver does not validate the
 * contents of an incoming frame in addition to what has already occurred in HW.
 * A single device driver can support multiple devices even when those devices
 * have significantly different configurations.
 *
 * <b>Initialization & Configuration</b>
 *
 * The XTemac_Config structure is used by the driver to configure itself. This
 * configuration structure is typically created by the tool-chain based on HW
 * build properties.
 *
 * To support multiple runtime loading and initialization strategies employed
 * by various operating systems, the driver instance can be initialized in one
 * of the following ways:
 *
 *   - XTemac_Initialize(InstancePtr, DeviceId): The driver looks up its own
 *     configuration structure created by the tool-chain based on an ID provided
 *     by the tool-chain.
 *
 *   - XTemac_VmInitialize(InstancePtr, DeviceId, VirtualAddress): Operates
 *     like XTemac_Initialize() except the physical base address found in the
 *     configuration structure is replaced with the provided virtual address
 *
 *   - XTemac_CfgInitialize(InstancePtr, CfgPtr, VirtualAddress):  Uses a
 *     configuration structure provided by the caller. If running in a system
 *     with address translation, the provided virtual memory base address
 *     replaces the physical address present in the configuration structure.
 *
 * The device can be configured for 2 major modes of operation: FIFO direct,
 * or scatter gather DMA (SGDMA). Each of these modes are independent of one
 * another and have their own frame transfer API. This driver can manage an
 * arbitrary number of devices each with its own operating mode and supporting
 * features and options.
 *
 * The driver tries to use the features built into the device as described
 * by the configuration structure. So if the hardware is configured with
 * SGDMA, the driver expects to start the SGDMA channels and expects that
 * the user has set up the buffer descriptor lists.
 *
 * <b>Interrupts and Asynchronous Callbacks</b>
 *
 * The driver has no dependencies on the interrupt controller. It provides
 * one interrupt handler per mode of operation (FIFO, SGDMA) that can be
 * connected to the system interrupt controller by BSP/OS specific means.
 *
 * When an interrupt occurs, the handler will perform a small amount of
 * housekeeping work, determine the source of the interrupt, and call the
 * appropriate callback function. All callbacks are registered by the user
 * level application.
 *
 * SGDMA implements interrupt coalescing features that reduce the frequency
 * of interrupts. A more complete discussion of this feature occurs in the API
 * section below.
 *
 * <b>Device Reset</b>
 *
 * Some errors that can occur require a device reset. These errors are listed
 * in the XTemac_ErrorHandler() function typedef header. The user's error
 * callback handler is responsible for resetting and re-configuring the device.
 * When a device reset is required, XTemac_Reset() should be utilized.
 *
 * <b>Virtual Memory</b>
 *
 * This driver may be used in systems with virtual memory support by using one
 * of the initialization functions that supply the virtual memory address of
 * the device.
 *
 * All virtual to physical memory mappings must occur prior to accessing the
 * driver API. The driver does not support multiple virtual memory translations
 * that map to the same physical address.
 *
 * For DMA transactions, user buffers supplied to the driver must be in terms
 * of their physical address.
 *
 * <b>Transfer Mode APIs</b>
 *
 * Using the proper API depends on how the HW has been configured. There are
 * two interrupt driven modes (FIFO Direct, and SGDMA). FIFO Direct also
 * supports a polled mode of operation.
 *
 * It is the user's responsibilty to use the API that matches the device
 * configuration. Most API functions do not perform runtime checks to verify
 * proper configuration. If an API function is called in error on a device
 * instance, then that function may attempt to access registers that are not
 * present resulting in bus errors and/or corrupted data. Macros are defined
 * that help the user determine which API can be used.
 *
 * All API functions are prototyped in xtemac.h and are implemented in various
 * xtemac_*.c files by feature.
 *
 * The following sections discuss in more detail each of the available APIs.
 *
 * <b>FIFO Direct API</b>
 *
 * This device mode utilizes the processor to transfer data between user buffers
 * and the packet FIFOs. HW configured in this way uses the least amount of FPGA
 * resources but provides the lowest data throughput.
 *
 * This API allows user independent access to the data packet, packet length,
 * and event FIFOs. While more sophisticated device modes keep these FIFOs
 * in sync automatically, the user has the primary responsibility in FIFO
 * direct mode.
 *
 * The packet FIFOs contain the frame data while the length/status FIFOs contain
 * receive lengths, transmit lengths, and transmit statuses. When these FIFOs
 * go out of sync, then packet data will become corrupted.
 *
 * On the transmit side, the transmit packet FIFO may contain more than one
 * Ethernet packet placed there by XTemac_FifoWrite(). The number of packets it
 * may contain depends on its depth which is controlled at HW build time. For
 * each packet in the FIFO, the user must initiate a transmit by writing into
 * the transmit length FIFO (see XTemac_FifoSend()). The number of bytes
 * specified to transmit must match exactly the lengths of packets in the
 * packet FIFO. For example, if a 76 byte packet was written followed by a
 * 124 byte packet, then the transmit length FIFO must be written with 76
 * followed by 124. At the completion of the transmission, the transmit status
 * FIFO must be read to obtain the outcome of the operation. The first status
 * will be for the 76 byte packet followed by the 124 byte packet.
 *
 * If there is not enough data in the packet FIFO to complete a transmit
 * operation, an underrun condition will be reported. The frame that gets
 * transmitted in this case is forced to a corrupted state so that it
 * will flagged as invalid by other receivers.
 *
 * On the receive side, it is a little easier to keep things in sync because
 * the HW writes to the receive packet FIFO. Just like the transmit packet FIFO,
 * the receive packet FIFO can contain more than one received Ethernet frame.
 * Each time a length is extracted from the receive length FIFO (see
 * XTemac_FifoRecv()), then that many bytes must be read from the receive
 * packet FIFO by XTemac_FifoRead().
 *
 * The easiest way to keep these FIFOs in sync is to process a single frame at
 * a time. But when performance is an issue, it may be desirable to process
 * multiple or even partial frames from non-contiguous memory regions. The
 * examples that accompany this driver illustrate how these advanced frame
 * processing methods can be implemented.
 *
 * In interrupt driven mode, user callbacks are invoked by the interrupt handler
 * to signal that frames have arrived, frames have been transmitted, or an
 * error has occurred. When the XTE_POLLED_OPTION is set, the user must use
 * send and receive query status functions to determine when these events
 * occur.
 *
 * <b>SGDMA API</b>
 *
 * This API utilizes scatter-gather DMA (SGDMA) channels to transfer frame data
 * between user buffers and the packet FIFOs.
 *
 * The SGDMA engine uses buffer descriptors (BDs) to describe Ethernet frames.
 * These BDs are typically chained together into a list the HW follows when
 * transferring data in and out of the packet FIFOs. Each BD describes a memory
 * region containing either a full or partial Ethernet packet.
 *
 * The frequency of interrupts can be controlled with the interrupt coalescing
 * features of the SG DMA engine. These features can be used to optimize
 * interrupt latency and throughput for the user's network traffic conditions.
 * The packet threshold count will delay processor interrupts until a
 * programmable number of packets have arrived or have been transmitted. The
 * packet wait bound timer can be used to cause a processor interrupt even though
 * the packet threshold has not been reached. The timer begins counting after the
 * last packet is processed. If no other packet is processed as the timer
 * expires, then an interrupt will be generated.
 *
 * Another form of interrupt control is provided with the XTE_SGEND_INT_OPTION
 * option. When enabled, an interrupt will occur when SGDMA engine completes the
 * last BD to be processed and transitions to an idle state. This feature may be
 * useful when a set of BDs have been queued up and the user only wants to be
 * notified when they have all been processed by the HW. To use this feature
 * effectively, interrupt coalescing should be disabled (packet threshold = 0,
 * wait bound timer = 0), or the packet threshold should be set to a number
 * larger than the number of packets queued up.
 *
 * By default, the driver will set the packet threshold = 1, wait bound timer =
 * 0, and disable the XTE_SGEND_INT_OPTION. These settings will cause one
 * interrupt per packet.
 *
 * This API requires the user to understand the how the SGDMA driver operates.
 * The following paragraphs provide some explanation, but the user is encouraged
 * to read documentation in xdmav3.h and xdmabdv3.h as well as study example code
 * that accompanies this driver.
 *
 * The API is designed to get BDs to and from the SGDMA engine in the most
 * efficient means possible. The first step is to establish a  memory region to
 * contain all BDs for a specific channel. This is done with XTemac_SgSetSpace()
 * and assumes the memory region is non-cached. This function sets up a BD ring
 * that HW will follow as BDs are processed. The ring will consist of a user
 * defined number of BDs which will all be partially initialized. For example on
 * the transmit channel, the driver will initialize all BDs' so that they are
 * configured for transmit. The more fields that can be permanently setup at
 * initialization, then the fewer accesses will be needed to each BD while the
 * SGDMA engine is in operation resulting in better throughput and CPU
 * utilization. The best case initialization would require the user to set only
 * a frame buffer address and length prior to submitting the BD to the engine.
 *
 * BDs move through the engine with the help of functions XTemac_SgAlloc(),
 * XTemac_SgCommit(), XTemac_SgGetProcessed(), and XTemac_SgFree(). All these
 * functions handle BDs that are in place. That is, there are no copies of BDs
 * kept anywhere and any BD the user interacts with is an actual BD from the
 * same ring HW accesses. Changing fields within BDs is done through an API
 * defined in xdmabdv3.h as well as checksum offloading macros defined in
 * xtemac.h.
 *
 * BDs in the ring go through a series of states as follows:
 *   1. Idle. The driver controls BDs in this state.
 *   2. The user has data to transfer. XTemac_SgAlloc() is called to reserve
 *      BD(s). Once allocated, the user may setup the BD(s) with frame buffer
 *      address, length, and other attributes. The user controls BDs in this
 *      state.
 *   3. The user submits BDs to the SGDMA engine with XTemac_SgCommit. BDs in
 *      this state are either waiting to be processed by HW, are in process, or
 *      have been processed. The SGDMA engine controls BDs in this state.
 *   4. Processed BDs are retrieved with XTemac_SgGetProcessed() by the
 *      user. Once retrieved, the user can examine each BD for the outcome of
 *      the DMA transfer. The user controls BDs in this state. After examining
 *      the BDs the user calls XTemac_SgFree() which places the BDs back into
 *      state 1.
 *
 * Each of the four BD accessor functions operate on a set of BDs. A set is
 * defined as a segment of the BD ring consisting of one or more BDs.  The user
 * views the set as a pointer to the first BD along with the number of BDs for
 * that set. The set can be navigated by using macros XTemac_mSgRecvBdNext() or
 * XTemac_mSgSendBdNext(). The user must exercise extreme caution when changing
 * BDs in a set as there is nothing to prevent doing a mSgRecvBdNext past the
 * end of the set and modifying a BD out of bounds.
 *
 * XTemac_SgAlloc() + XTemac_SgCommit(), as well as XTemac_SgGetProcessed() +
 * XTemac_SgFree() are designed to be used in tandem. The same BD set retrieved
 * with SgAlloc should be the same one provided to HW with SgCommit. Same goes
 * with SgGetProcessed and SgFree.
 *
 * <b>SG DMA Troubleshooting</b>
 *
 * To verify internal structures of BDs and the BD ring, the function
 * XTemac_SgCheck() is provided. This function should be used as a debugging
 * or diagnostic tool. If it returns a failure, the user must perform more
 * in depth debugging to find the root cause.
 *
 * To avoid problems, do not use the following BD macros for transmit channel
 * BDs (XTE_SEND):
 *
 *   - XDmaBdV3_mClear()
 *   - XDmaBdV3_mSetRxDir()
 *
 * and for receive channel BDs (XTE_RECV):
 *
 *   - XDmaBdV3_mClear()
 *   - XDmaBdV3_mSetTxDir()
 *
 * <b>Alignment & Data Cache Restrictions</b>
 *
 * FIFO Direct:
 *
 *   - No frame buffer alignment restrictions for Tx or Rx
 *   - Buffers not aligned on a 4-byte boundary will take longer to process
 *     as the driver uses a small transfer buffer to realign them prior to
 *     packet FIFO access
 *   - Frame buffers may be in cached memory
 *
 * SGDMA Tx with DRE:
 *
 *   - No frame buffer alignment restrictions
 *   - If frame buffers exist in cached memory, then they must be flushed prior
 *     to committing them to HW
 *   - Descriptors must be 4-byte aligned
 *   - Descriptors must be in non-cached memory
 *
 * SGDMA Tx without DRE:
 *
 *   - Frame buffers must be 8-byte aligned
 *   - If frame buffers exist in cached memory, then they must be flushed prior
 *     to committing them to HW
 *   - Descriptors must be 4-byte aligned
 *   - Descriptors must be in non-cached memory
 *
 * SGDMA Rx with DRE:
 *
 *   - No frame buffer alignment restrictions
 *   - If frame buffers exist in cached memory, then the cache must be
 *     invalidated for the memory region containing the frame prior to data
 *     access
 *   - Descriptors must be 4-byte aligned
 *   - Descriptors must be in non-cached memory
 *
 * SGDMA Rx without DRE:
 *
 *   - Frame buffers must be 8-byte aligned
 *   - If frame buffers exist in cached memory, then the cache must be
 *     invalidated for the memory region containing the frame prior to data
 *     access
 *   - Descriptors must be 4-byte aligned
 *   - Descriptors must be in non-cached memory
 *
 * <b>Buffer Copying</b>
 *
 * The driver is designed for a zero-copy buffer scheme. That is, the driver will
 * not copy buffers. This avoids potential throughput bottlenecks within the
 * driver.
 *
 * The only exception to this is when buffers are passed to XTemac_FifoRead() and
 * XTemac_FifoWrite() on 1, 2, or 3 byte alignments. These buffers will be byte
 * copied into a small holding area on their way to or from the packet FIFOs.
 * For PLB TEMAC this holding area is 8 bytes each way. If byte copying is
 * required, then the transfer will take longer to complete.
 *
 * <b>Checksum Offloading</b>
 *
 * If configured, the device can compute a 16-bit checksum from frame data. In
 * most circumstances this can lead to a substantial gain in throughput.
 *
 * For Tx, the SW can specify where in the frame the checksum calculation is
 * to start, where it should be inserted, and a seed value. The checksum is
 * calculated from the start point through the end of frame. For Rx, the 15th
 * byte to end of frame is checksummed. This is the entire Ethernet payload
 * for non-VLAN frames.
 *
 * Setting up and accessing checksum data is done with XTemac API macro calls
 * on buffer descriptors on a per-frame basis.
 *
 * Since this HW implementation is general purpose in nature system SW must
 * perform pre and post frame processing to obtain the desired results for the
 * types of packets being transferred. Most of the time this will be TCP/IP
 * traffic.
 *
 * TCP/IP and UDP/IP frames contain separate checksums for the IP header and
 * UDP/TCP header+data. With this HW implementation, the IP header checksum
 * cannot be offloaded. Many stacks that support offloading will compute the IP
 * header if required and use HW to compute the UDP/TCP header+data checksum.
 * There are other complications concerning the IP pseudo header that must be
 * taken into consideration. Readers should consult a TCP/IP design reference
 * for more details.
 *
 * There are certain device options that will affect the checksum calculation
 * performed by HW for Tx:
 *
 *   - FCS insertion disabled (XTE_FCS_INSERT_OPTION): SW is required to
 *     calculate and insert the FCS value at the end of the frame, but the
 *     checksum must be known ahead of time prior to calculating the FCS.
 *     Therefore checksum offloading cannot be used in this situation.
 *
 * And for Rx:
 *
 *   - FCS/PAD stripping disabled (XTE_FCS_STRIP_OPTION): The 4 byte FCS at the
 *     end of frame will be included in the HW calculated checksum. SW must
 *     subtract out this data.
 *
 *   - FCS/PAD stripping disabled (XTE_FCS_STRIP_OPTION): For frames smaller
 *     than 64 bytes, padding will be included in the HW calculated checksum.
 *     SW must subtract out this data. It may be better to allow the TCP/IP
 *     stack verify checksums for this type of packet.
 *
 *   - VLAN enabled (XTE_VLAN_OPTION): The 4 extra bytes in the Ethernet header
 *     affect the HW calculated checksum. SW must subtract out the 1st two
 *     16-bit words starting at the 15th byte.
 *
 * <b>PHY Communication</b>
 *
 * Prior to PHY access, the MDIO clock must be setup. This driver will set a
 * safe default that should work with PLB bus speeds of up to 150 MHz and keep
 * the MDIO clock below 2.5 MHz. If the user wishes faster access to the PHY
 * then the clock divisor can be set to a different value (see
 * XTemac_PhySetMdioDivisor()).
 *
 * MII register access is performed through the functions XTemac_PhyRead() and
 * XTemac_PhyWrite().
 *
 * <b>Link Sync</b>
 *
 * When the device is used in a multispeed environment, the link speed must be
 * explicitly set using XTemac_SetOperatingSpeed() and must match the speed the
 * PHY has negotiated. If the speeds are mismatched, then the MAC will not pass
 * traffic.
 *
 * Using the XTE_ANEG_OPTION and the provided callback handler, SW can be
 * notified when the PHY has completed auto-negotiation.
 *
 * <b>Asserts</b>
 *
 * Asserts are used within all Xilinx drivers to enforce constraints on argument
 * values. Asserts can be turned off on a system-wide basis by defining, at
 * compile time, the NDEBUG identifier. By default, asserts are turned on and it
 * is recommended that users leave asserts on during development. For deployment
 * use -DNDEBUG compiler switch to remove assert code.
 *
 * <b>Driver Errata</b>
 *
 *   - A dropped receive frame indication may be reported by the driver after
 *     calling XTemac_Stop() followed by XTemac_Start(). This can occur if a
 *     frame is arriving when stop is called.
 *   - On Rx with checksum offloading enabled and FCS/PAD stripping disabled,
 *     FCS and PAD data will be included in the checksum result.
 *   - On Tx with checksum offloading enabled and auto FCS insertion disabled,
 *     the user calculated FCS will be included in the checksum result.
 *
 * @note
 *
 * Xilinx drivers are typically composed of two components, one is the driver
 * and the other is the adapter.  The driver is independent of OS and processor
 * and is intended to be highly portable.  The adapter is OS-specific and
 * facilitates communication between the driver and an OS.
 * <br><br>
 * This driver is intended to be RTOS and processor independent. Any needs for
 * dynamic memory management, threads or thread mutual exclusion, or cache
 * control must be satisfied by the layer above this driver.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -------------------------------------------------------
 * 1.00a rmm  06/01/05 First release
 * 1.00b rmm  09/23/05 Replaced XTemac_GetPhysicalInterface() with macro
 *                     XTemac_mGetPhysicalInterface(). Implemented
 *                     XTemac_PhyRead/Write() functions. Redesigned MII/RGMII/
 *                     SGMII status functions. Renamed most of the host
 *                     registers to reflect latest changes in HW spec, added
 *                     XST_FIFO_ERROR return code to polled FIFO query
 *                     functions.
 * 2.00a rmm  11/21/05 Switched to local link DMA driver, removed simple-DMA
 *                     mode, added auto-negotiation callback, added checksum
 *                     offload access macros, removed XST_SEND_ERROR error
 *                     class completely since TSR bits went away, removed
 *                     XST_FAILURE return code for XTemac_FifoQuerySendStatus(),
 *                     added static init feature, changed XTE_FCS_STRIP_OPTION
 *                     to default to set.
 * </pre>
 *
 *****************************************************************************/

#ifndef XTEMAC_H		/* prevent circular inclusions */
#define XTEMAC_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include <asm/delay.h>
#include "xbasic_types.h"
#include "xstatus.h"
#include "xparameters.h"
#include "xipif_v1_23_b.h"
#include "xpacket_fifo_v2_00_a.h"
#include "xdmav3.h"
#include "xtemac_l.h"

/************************** Constant Definitions *****************************/

/*
 * Device information
 */
#define XTE_DEVICE_NAME     "xtemac"
#define XTE_DEVICE_DESC     "Xilinx Tri-speed 10/100/1000 MAC"


/** @name Configuration options
 *
 * Device configuration options. See the XTemac_SetOptions(),
 * XTemac_ClearOptions() and XTemac_GetOptions() for information on how to use
 * options.
 *
 * The default state of the options are noted and are what the device and driver
 * will be set to after calling XTemac_Reset() or XTemac_Initialize().
 *
 * @{
 */

#define XTE_PROMISC_OPTION               0x00000001
/**< Accept all incoming packets.
 *   This option defaults to disabled (cleared) */

#define XTE_JUMBO_OPTION                 0x00000002
/**< Jumbo frame support for Tx & Rx.
 *   This option defaults to disabled (cleared) */

#define XTE_VLAN_OPTION                  0x00000004
/**< VLAN Rx & Tx frame support.
 *   This option defaults to disabled (cleared) */

#define XTE_FLOW_CONTROL_OPTION          0x00000010
/**< Enable recognition of flow control frames on Rx
 *   This option defaults to enabled (set) */

#define XTE_FCS_STRIP_OPTION             0x00000020
/**< Strip FCS and PAD from incoming frames. Note: PAD from VLAN frames is not
 *   stripped.
 *   This option defaults to disabled (set) */

#define XTE_FCS_INSERT_OPTION            0x00000040
/**< Generate FCS field and add PAD automatically for outgoing frames.
 *   This option defaults to enabled (set) */

#define XTE_LENTYPE_ERR_OPTION           0x00000080
/**< Enable Length/Type error checking for incoming frames. When this option is
 *   set, the MAC will filter frames that have a mismatched type/length field
 *   and if XTE_REPORT_RXERR_OPTION is set, the user is notified when these
 *   types of frames are encountered. When this option is cleared, the MAC will
 *   allow these types of frames to be received.
 *
 *   This option defaults to enabled (set) */

#define XTE_SGEND_INT_OPTION             0x00000100
/**< Enable the SGEND interrupt with SG DMA. When enabled, an interrupt will
 *   be triggered when the end of the buffer descriptor list is reached. The
 *   interrupt will occur despite interrupt coalescing settings.
 *   This option defaults to disabled (cleared) */

#define XTE_POLLED_OPTION                0x00000200
/**< Polled mode communications. Enables use of XTemac_FifoQuerySendStatus()
 *   and XTemac_FifoQueryRecvStatus(). Users may enter/exit polled mode
 *   from any interrupt driven mode.
 *   This option defaults to disabled (cleared) */

#define XTE_REPORT_RXERR_OPTION          0x00000400
/**< Enable reporting of dropped receive packets due to errors
 *   This option defaults to enabled (set) */

#define XTE_TRANSMITTER_ENABLE_OPTION    0x00000800
/**< Enable the transmitter.
 *   This option defaults to enabled (set) */

#define XTE_RECEIVER_ENABLE_OPTION       0x00001000
/**< Enable the receiver
 *   This option defaults to enabled (set) */

#define XTE_BROADCAST_OPTION             0x00002000
/**< Allow reception of the broadcast address
 *   This option defaults to enabled (set) */

#define XTE_MULTICAST_CAM_OPTION         0x00004000
/**< Allows reception of multicast addresses programmed into CAM
 *   This option defaults to disabled (clear) */

#define XTE_REPORT_TXSTATUS_OVERRUN_OPTION 0x00008000
/**< Enable reporting the overrun of the Transmit status FIFO. This type of
 *   error is latched by HW and can be cleared only by a reset. SGDMA systems,
 *   this option should be enabled since the DMA engine is responsible for
 *   keeping this from occurring. For FIFO direct systems, this error may be
 *   a nuisance because a SW system may be able to transmit frames faster
 *   than the interrupt handler can handle retrieving statuses.
 *   This option defaults to enabled (set) */

#define XTE_ANEG_OPTION                  0x00010000
/**< Enable autonegotiation interrupt
     This option defaults to disabled (clear) */

#define XTE_DEFAULT_OPTIONS                     \
    (XTE_FLOW_CONTROL_OPTION |                  \
     XTE_BROADCAST_OPTION |                     \
     XTE_FCS_INSERT_OPTION |                    \
     XTE_FCS_STRIP_OPTION |                     \
     XTE_LENTYPE_ERR_OPTION |                   \
     XTE_TRANSMITTER_ENABLE_OPTION |            \
     XTE_REPORT_RXERR_OPTION |                  \
     XTE_REPORT_TXSTATUS_OVERRUN_OPTION |       \
     XTE_RECEIVER_ENABLE_OPTION)
/**< Default options set when device is initialized or reset */

/*@}*/

/** @name Direction identifiers
 *
 *  These are used by several functions and callbacks that need
 *  to specify whether an operation specifies a send or receive channel.
 * @{
 */
#define XTE_SEND    1
#define XTE_RECV    2
/*@}*/

/** @name Reset parameters
 *
 *  These are used by function XTemac_Reset().
 * @{
 */
#define XTE_RESET_HARD    1
#define XTE_NORESET_HARD  0
/*@}*/

/** @name XTemac_FifoWrite/Read() function arguments
 *
 *  These are used by XTemac_FifoWrite/Read() End Of Packet (Eop)
 *  parameter.
 * @{
 */
#define XTE_END_OF_PACKET   1	/**< The data written is the last for the
                                  *  current packet */
#define XTE_PARTIAL_PACKET  0	/**< There is more data to come for the
                                  *  current packet */
/*@}*/

/** @name Callback identifiers
 *
 * These constants are used as parameters to XTemac_SetHandler()
 * @{
 */
#define XTE_HANDLER_FIFOSEND     1
#define XTE_HANDLER_FIFORECV     2
#define XTE_HANDLER_SGSEND       5
#define XTE_HANDLER_SGRECV       6
#define XTE_HANDLER_ERROR        7
#define XTE_HANDLER_ANEG         8
/*@}*/


/* Constants to determine the configuration of the hardware device. They are
 * used to allow the driver to verify it can operate with the hardware.
 */
#define XTE_CFG_NO_DMA              1	/* No DMA */
#define XTE_CFG_SIMPLE_DMA          2	/* Simple DMA (not supported) */
#define XTE_CFG_DMA_SG              3	/* DMA scatter gather */

#define XTE_MULTI_CAM_ENTRIES       4	/* Number of storable addresses in
					   the CAM */

#define XTE_MDIO_DIV_DFT            29	/* Default MDIO clock divisor */

/* Some default values for interrupt coalescing within the scatter-gather
 * DMA engine.
 */
#define XTE_SGDMA_DFT_THRESHOLD     1	/* Default pkt threshold */
#define XTE_SGDMA_MAX_THRESHOLD     1023	/* Maximum pkt theshold */
#define XTE_SGDMA_DFT_WAITBOUND     0	/* Default pkt wait bound (msec) */
#define XTE_SGDMA_MAX_WAITBOUND     1023	/* Maximum pkt wait bound (msec) */

/* The next few constants help upper layers determine the size of memory
 * pools used for Ethernet buffers and descriptor lists.
 */
#define XTE_MAC_ADDR_SIZE   6	/* six-byte MAC address */
#define XTE_MTU             1500	/* max MTU size of Ethernet frame */
#define XTE_JUMBO_MTU       8982	/* max MTU size of jumbo Ethernet frame */
#define XTE_HDR_SIZE        14	/* size of Ethernet header */
#define XTE_HDR_VLAN_SIZE   18	/* size of Ethernet header with VLAN */
#define XTE_TRL_SIZE        4	/* size of Ethernet trailer (FCS) */
#define XTE_MAX_FRAME_SIZE       (XTE_MTU + XTE_HDR_SIZE + XTE_TRL_SIZE)
#define XTE_MAX_VLAN_FRAME_SIZE  (XTE_MTU + XTE_HDR_VLAN_SIZE + XTE_TRL_SIZE)
#define XTE_MAX_JUMBO_FRAME_SIZE (XTE_JUMBO_MTU + XTE_HDR_SIZE + XTE_TRL_SIZE)

/* Constant values returned by XTemac_mGetPhysicalInterface(). Note that these
 * values match design parameters from the PLB_TEMAC spec
 */
#define XTE_PHY_TYPE_MII         0
#define XTE_PHY_TYPE_GMII        1
#define XTE_PHY_TYPE_RGMII_1_3   2
#define XTE_PHY_TYPE_RGMII_2_0   3
#define XTE_PHY_TYPE_SGMII       4
#define XTE_PHY_TYPE_1000BASE_X  5

/**************************** Type Definitions *******************************/


/** @name Typedefs for callback functions
 *
 * These callbacks are invoked in interrupt context.
 * @{
 */

/**
 * Callback invoked when frame(s) have been sent in interrupt driven FIFO
 * direct mode. To set this callback, invoke XTemac_SetHander() with
 * XTE_HANDLER_FIFOSEND in the HandlerType parameter.
 *
 * @param CallBackRef is user data assigned when the callback was set.
 * @param StatusCnt is the number of statuses read from the device indicating
 *        a successful frame transmit.
 *
 */
typedef void (*XTemac_FifoSendHandler) (void *CallBackRef, unsigned StatusCnt);

/**
 * Callback invoked when frame(s) have been received in interrupt driven FIFO
 * direct mode. To set this callback, invoke XTemac_SetHander() with
 * XTE_HANDLER_FIFORECV in the HandlerType parameter.
 *
 * @param CallBackRef is user data assigned when the callback was set.
 *
 */
typedef void (*XTemac_FifoRecvHandler) (void *CallBackRef);

/**
 * Callback invoked when frame(s) have been sent or received in interrupt
 * driven SGDMA mode. To set the send callback, invoke XTemac_SetHandler()
 * with XTE_HANDLER_SGSEND in the HandlerType parameter. For the receive
 * callback use XTE_HANDLER_SGRECV.
 *
 * @param CallBackRef is user data assigned when the callback was set.
 */
typedef void (*XTemac_SgHandler) (void *CallBackRef);

/**
 * Callback invoked when auto-negotiation interrupt is asserted
 * To set this callback, invoke XTemac_SetHandler() with XTE_HANDLER_ANEG in
 * the HandlerType parameter.
 *
 * @param CallBackRef is user data assigned when the callback was set.
 */
typedef void (*XTemac_AnegHandler) (void *CallBackRef);

/**
 * Callback when an asynchronous error occurs. To set this callback, invoke
 * XTemac_SetHandler() with XTE_HANDLER_ERROR in the HandlerType paramter.
 *
 * @param CallBackRef is user data assigned when the callback was set.
 * @param ErrorClass defines what class of error is being reported
 * @param ErrorWord1 definition varies with ErrorClass
 * @param ErrorWord2 definition varies with ErrorClass
 *
 * The following information lists what each ErrorClass is, the source of the
 * ErrorWords, what they mean, and if the device should be reset should it be
 * reported
 *
 * <b>ErrorClass == XST_FIFO_ERROR</b>
 *
 * This error class means there was a fatal error with one of the device FIFOs.
 * This type of error cannot be cleared. The user should initiate a device reset.
 *
 * ErrorWord1 is defined as a bit mask from XTE_IPXR_FIFO_FATAL_ERROR_MASK
 * that originates from the device's IPISR register.
 *
 * ErrorWord2 is reserved.
 *
 *
 * <b>ErrorClass == XST_PFIFO_DEADLOCK</b>
 *
 * This error class indicates that one of the packet FIFOs is reporting a
 * deadlock condition. This means the FIFO is reporting that it is empty and
 * full at the same time. This condition will occur when data being written
 * exceeds the capacity of the packet FIFO. The device should be reset if this
 * error is reported.
 *
 * Note that this error is reported only if the device is configured for FIFO
 * direct mode. For SGDMA, this error is reported in ErrorClass XST_FIFO_ERROR.
 *
 * If ErrorWord1 = XTE_RECV, then the deadlock occurred in the receive channel.
 * If ErrorWord1 = XTE_SEND, then the deadlock occurred in the send channel.
 *
 * ErrorWord2 is reserved.
 *
 *
 * <b>ErrorClass == XST_IPIF_ERROR</b>
 *
 * This error means that a register read or write caused a bus error within the
 * TEMAC's IPIF. This condition is fatal. The user should initiate a device
 * reset.
 *
 * ErrorWord1 is defined as the contents XTE_DISR_OFFSET register where these
 * errors are reported. Bits XTE_DXR_DPTO_MASK and XTE_DXR_TERR_MASK are
 * relevent in this context.
 *
 * ErrorWord2 is reserved.
 *
 *
 * <b>ErrorClass == XST_DMA_ERROR</b>
 *
 * This error class means there was a problem during a DMA transfer.
 *
 * ErrorWord1 defines which channel caused the error XTE_RECV or XTE_SEND.
 *
 * ErrorWord2 is set to the DMA status register XDMAV3_DMASR_OFFSET.
 * The relevent bits to test are XDMAV3_DMASR_DBE_MASK and XDMAV3_DMASR_DBT_MASK.
 * If either of these bits are set, a reset is recommended.
 *
 *
 * <b>ErrorClass == XST_RECV_ERROR</b>
 *
 * This error class means a packet was dropped.
 *
 * ErrorWord1 is defined as the contents of the device's XTE_IPISR_OFFSET
 * relating to receive errors. If any bit is set in the
 * XTE_IPXR_RECV_DROPPED_MASK then a packet was rejected. Refer to xtemac_l.h
 * for more information on what each bit in this mask means.
 *
 * ErrorWord2 is reserved.
 *
 * No action is typically required when this error occurs.
 *
 * Reporting of this error class can be disabled by clearing the
 * XTE_REPORT_RXERR_OPTION.
 *
 * @note
 * See xtemac_l.h for bitmasks definitions and the device hardware spec for
 * further information on their meaning.
 *
 */
typedef void (*XTemac_ErrorHandler) (void *CallBackRef, int ErrorClass,
				     u32 ErrorWord1, u32 ErrorWord2);
/*@}*/


/**
 * Statistics maintained by the driver
 */
typedef struct {
	u32 TxDmaErrors; /**< Number of Tx DMA errors detected */
	u32 TxPktFifoErrors;
			 /**< Number of Tx packet FIFO errors detected */
	u32 TxStatusErrors;
			 /**< Number of Tx errors derived from XTE_TSR_OFFSET
                                  register */
	u32 RxRejectErrors;
			 /**< Number of frames discarded due to errors */
	u32 RxDmaErrors; /**< Number of Rx DMA errors detected */
	u32 RxPktFifoErrors;
			 /**< Number of Rx packet FIFO errors detected */

	u32 FifoErrors;	 /**< Number of length/status FIFO errors detected */
	u32 IpifErrors;	 /**< Number of IPIF transaction and data phase errors
                                  detected */
	u32 Interrupts;	 /**< Number of interrupts serviced */
} XTemac_SoftStats;


/**
 * This typedef contains configuration information for a device.
 */
typedef struct {
	u16 DeviceId;	/**< Unique ID  of device */
	u32 BaseAddress;/**< Physical base address of IPIF registers */
	u32 RxPktFifoDepth;
			/**< Depth of receive packet FIFO in bits */
	u32 TxPktFifoDepth;
			/**< Depth of transmit packet FIFO in bits */
	u16 MacFifoDepth;
			/**< Depth of the status/length FIFOs in entries */
	u8 IpIfDmaConfig;
			/**< IPIF/DMA hardware configuration */
	u8 TxDre;	/**< Has data realignment engine on Tx channel */
	u8 RxDre;	/**< Has data realignment engine on Rx channel */
	u8 TxCsum;	/**< Has checksum offload on Tx channel */
	u8 RxCsum;	/**< Has checksum offload on Tx channel */
	u8 PhyType;	/**< Which type of PHY interface is used (MII,
                                 GMII, RGMII, ect. */
} XTemac_Config;


/* This type encapsulates a packet FIFO channel and support attributes to
 * allow unaligned data transfers.
 */
typedef struct XTemac_PacketFifo {
	u32 Hold[2];		/* Holding register */
	unsigned ByteIndex;	/* Holding register index */
	unsigned Width;		/* Width of packet FIFO's keyhole data port in
				   bytes */
	XPacketFifoV200a Fifo;	/* Packet FIFO channel */
	/* Function used to transfer data between
	   FIFO and a buffer */
	int (*XferFn) (struct XTemac_PacketFifo *Fptr, void *BufPtr,
		       u32 ByteCount, int Eop);
} XTemac_PacketFifo;


/**
 * The XTemac driver instance data. The user is required to allocate a
 * structure of this type for every TEMAC device in the system. A pointer
 * to a structure of this type is then passed to the driver API functions.
 */
typedef struct XTemac {
	u32 BaseAddress;	/* Base address of IPIF register set */
	u32 IsStarted;		/* Device is currently started */
	u32 IsReady;		/* Device is initialized and ready */
	u32 Options;		/* Current options word */
	u32 Flags;		/* Internal driver flags */
	XTemac_Config Config;	/* HW configuration */

	/* Packet FIFO channels */
	XTemac_PacketFifo RecvFifo;	/* Receive channel */
	XTemac_PacketFifo SendFifo;	/* Transmit channel */

	/* DMA channels */
	XDmaV3 RecvDma;		/* Receive channel */
	XDmaV3 SendDma;		/* Transmit channel */

	/* Callbacks for FIFO direct modes */
	XTemac_FifoRecvHandler FifoRecvHandler;
	XTemac_FifoSendHandler FifoSendHandler;
	void *FifoRecvRef;
	void *FifoSendRef;

	/* Callbacks for SG DMA mode */
	XTemac_SgHandler SgRecvHandler;
	XTemac_SgHandler SgSendHandler;
	void *SgRecvRef;
	void *SgSendRef;

	/* Auto negotiation callback */
	XTemac_AnegHandler AnegHandler;
	void *AnegRef;

	/* Error callback */
	XTemac_ErrorHandler ErrorHandler;
	void *ErrorRef;

	/* Driver maintained statistics */
	XTemac_SoftStats Stats;

} XTemac;


/***************** Macros (Inline Functions) Definitions *********************/

/*****************************************************************************/
/**
*
* This macro can be used to determine if the device is in the started or
* stopped state. To be in the started state, the user must have made a
* successful call to XTemac_Start(). To be in the stopped state, XTemac_Stop()
* or one of the XTemac initialize functions must have been called.
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device has been started, FALSE otherwise
*
* @note
*
* Signature: u32 XTemac_mIsStarted(XTemac *InstancePtr)
*
******************************************************************************/
#define XTemac_mIsStarted(InstancePtr) \
    (((InstancePtr)->IsStarted == XCOMPONENT_IS_STARTED) ? TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device thinks it has received a frame. This
* function is useful if the device is operating in FIFO direct interrupt driven
* mode. For polled mode, use XTemac_FifoQueryRecvStatus().
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device interrupt status register reports that a frame
* status and length is available. FALSE otherwise.
*
* @note
*
* Signature: u32 XTemac_mIsRecvFrame(XTemac *InstancePtr)
*
******************************************************************************/
#define XTemac_mIsRecvFrame(InstancePtr)                            \
    ((XTemac_mReadReg((InstancePtr)->BaseAddress, XTE_IPISR_OFFSET) \
      & XTE_IPXR_RECV_DONE_MASK) ? TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device thinks it has dropped a receive frame.
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device interrupt status register reports that a frame
* has been dropped. FALSE otherwise.
*
* @note
*
* Signature: u32 XTemac_mIsRecvFrameDropped(XTemac *InstancePtr)
*
******************************************************************************/
#define XTemac_mIsRecvFrameDropped(InstancePtr)                     \
    ((XTemac_mReadReg((InstancePtr)->BaseAddress, XTE_IPISR_OFFSET) \
      & XTE_IPXR_RECV_REJECT_MASK) ? TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device is currently configured for
* FIFO direct mode
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured for FIFO direct, or FALSE
* if it is not.
*
* @note
*
* Signature: u32 XTemac_mIsFifo(XTemac *InstancePtr)
*
******************************************************************************/
#define XTemac_mIsFifo(InstancePtr) \
    (((InstancePtr)->Config.IpIfDmaConfig == XTE_CFG_NO_DMA) ? TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device is currently configured for
* scatter-gather DMA.
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured for scatter-gather DMA, or FALSE
* if it is not.
*
* @note
*
* Signature: u32 XTemac_mIsSgDma(XTemac *InstancePtr)
*
******************************************************************************/
#define XTemac_mIsSgDma(InstancePtr) \
    (((InstancePtr)->Config.IpIfDmaConfig == XTE_CFG_DMA_SG) ? TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device is configured with the Data Realignment
* Engine (DRE) on the receive channel
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured with DRE, or FALSE otherwise.
*
* @note
*
* Signature: u32 XTemac_mIsRxDre(XTemac *InstancePtr)
*
******************************************************************************/
#define XTemac_mIsRxDre(InstancePtr) (((InstancePtr)->Config.RxDre) ?  \
                                      TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device is configured with the Data Realignment
* Engine (DRE) on the transmit channel
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured with DRE, or FALSE otherwise.
*
* @note
*
* Signature: u32 XTemac_mIsTxDre(XTemac *InstancePtr)
*
******************************************************************************/
#define XTemac_mIsTxDre(InstancePtr) (((InstancePtr)->Config.TxDre) ?  \
                                      TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device is configured with checksum offloading
* on the receive channel
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured with checksum offloading, or
* FALSE otherwise.
*
* @note
*
* Signature: u32 XTemac_mIsRxCsum(XTemac *InstancePtr)
*
******************************************************************************/
#define XTemac_mIsRxCsum(InstancePtr) (((InstancePtr)->Config.RxCsum) ?  \
                                       TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro determines if the device is configured with checksum offloading
* on the transmit channel
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
*
* @return
*
* Boolean TRUE if the device is configured with checksum offloading, or
* FALSE otherwise.
*
* @note
*
* Signature: u32 XTemac_mIsTxCsum(XTemac *InstancePtr)
*
******************************************************************************/
#define XTemac_mIsTxCsum(InstancePtr) (((InstancePtr)->Config.TxCsum) ?  \
                                       TRUE : FALSE)

/*****************************************************************************/
/**
*
* This macro returns the type of PHY interface being used by the given
* instance.
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
*
* @return
*
* One of XTE_PHY_TYPE_<x> where <x> is MII, GMII, RGMII_1_3, RGMII_2_0,
* SGMII, or 1000BASE_X.
*
* @note
*
* Signature: int XTemac_mGetPhysicalInterface(XTemac *InstancePtr)
*
******************************************************************************/
#define XTemac_mGetPhysicalInterface(InstancePtr)       \
    ((InstancePtr)->Config.PhyType)

/*****************************************************************************/
/**
*
* Return the next buffer descriptor in the list on the send channel.
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
* @param BdPtr is the source descriptor
*
* @return Next descriptor in the SGDMA transmit ring (i.e. BdPtr->Next)
*
* @note
*
* Signature: XDmaBdV3 XTemac_mSgSendBdNext(XTemac *InstancePtr,
*                                          XDmaBdV3 *BdPtr)
*
******************************************************************************/
#define XTemac_mSgSendBdNext(InstancePtr, BdPtr)        \
    XDmaV3_mSgBdNext(&(InstancePtr)->SendDma, (BdPtr))

/*****************************************************************************/
/**
*
* Return the previous buffer descriptor in the list on the send channel.
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
* @param BdPtr is the source descriptor
*
* @return Previous descriptor in the SGDMA transmit ring (i.e. BdPtr->Prev)
*
* @note
*
* Signature: XDmaBdV3 XTemac_mSgSendBdPrev(XTemac *InstancePtr,
*                                          XDmaBdV3 *BdPtr)
*
******************************************************************************/
#define XTemac_mSgSendBdPrev(InstancePtr, BdPtr)        \
    XDmaV3_mSgBdPrev(&(InstancePtr)->SendDma, (BdPtr))

/*****************************************************************************/
/**
*
* Return the next buffer descriptor in the list on the receive channel.
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
* @param BdPtr is the source descriptor
*
* @return Next descriptor in the SGDMA receive ring (i.e. BdPtr->Next)
*
* @note
*
* Signature: XDmaBdV3 XTemac_mSgRecvBdNext(XTemac *InstancePtr,
*                                          XDmaBdV3 *BdPtr)
*
******************************************************************************/
#define XTemac_mSgRecvBdNext(InstancePtr, BdPtr)        \
    XDmaV3_mSgBdNext(&(InstancePtr)->RecvDma, (BdPtr))

/*****************************************************************************/
/**
*
* Return the previous buffer descriptor in the list on the receive channel.
*
* @param InstancePtr is a pointer to the XTemac instance to be worked on.
* @param BdPtr is the source descriptor
*
* @return Previous descriptor in the SGDMA receive ring (i.e. BdPtr->Prev)
*
* @note
*
* Signature: XDmaBdV3 XTemac_mSgRecvBdPrev(XTemac *InstancePtr,
*                                          XDmaBdV3 *BdPtr)
*
******************************************************************************/
#define XTemac_mSgRecvBdPrev(InstancePtr, BdPtr)        \
    XDmaV3_mSgBdNext(&(InstancePtr)->RecvDma, (BdPtr))

/*****************************************************************************/
/**
*
* Retrieve the received frame checksum as calculated by HW
*
* @param BdPtr is the source descriptor
*
* @return 16-bit checksum value

* @note
*
* Signature: u16 XTemac_mSgRecvBdCsumGet(XDmaBdV3 *BdPtr)
*
******************************************************************************/
#define XTemac_mSgRecvBdCsumGet(BdPtr)                          \
    (*(u16*)((u32)(BdPtr) + XTE_BD_RX_CSRAW_OFFSET))

/*****************************************************************************/
/**
*
* Enable transmit side checksum calculation for the given descriptor.
*
* @param BdPtr is the source descriptor
*
* @note
*
* Signature: void XTemac_mSgSendBdCsumEnable(XDmaBdV3 *BdPtr)
*
******************************************************************************/
#define XTemac_mSgSendBdCsumEnable(BdPtr)                       \
    *(u16*)((u32)(BdPtr) + XTE_BD_TX_CSCNTRL_OFFSET) =  \
        XTE_BD_TX_CSCNTRL_CALC_MASK

/*****************************************************************************/
/**
*
* Disable transmit side checksum calculation for the given descriptor.
*
* @param BdPtr is the source descriptor
*
* @note
*
* Signature: void XTemac_mSgSendBdCsumDisable(XDmaBdV3 *BdPtr)
*
******************************************************************************/
#define XTemac_mSgSendBdCsumDisable(BdPtr)                              \
    *(u16*)((u32)(BdPtr) + XTE_BD_TX_CSCNTRL_OFFSET) = 0

/*****************************************************************************/
/**
*
* Setup checksum attributes for a transmit frame. If a seed value is required
* XTemac_mSgSendBdCsumSeed() can be used
*
* @param BdPtr is the source descriptor
* @param StartOffset is the byte offset where HW will begin checksumming data
* @param InsertOffset is the byte offset where HW will insert the calculated
*        checksum value
*
* @note
*
* Signature: void XTemac_mSgSendBdCsumSetup(XDmaBdV3 *BdPtr,
*                                           u16 StartOffset,
*                                           u16 InsertOffset)
*
******************************************************************************/
#define XTemac_mSgSendBdCsumSetup(BdPtr, StartOffset, InsertOffset)     \
    *(u32*)((u32)(BdPtr) + XTE_BD_TX_CSBEGIN_OFFSET) =          \
        ((StartOffset) << 16) | (InsertOffset)

/*****************************************************************************/
/**
*
* Set the initial checksum seed for a transmit frame. HW will add this value
* to the calculated frame checksum. If not required then the seed should be
* set to 0.
*
* @param BdPtr is the source descriptor
* @param Seed is added to the calculated checksum
*
* @note
*
* Signature: void XTemac_mSgSendBdCsumSeed(XDmaBdV3 *BdPtr, u16 Seed)
*
******************************************************************************/
#define XTemac_mSgSendBdCsumSeed(BdPtr, Seed)                           \
    *(u16*)((u32)(BdPtr) + XTE_BD_TX_CSINIT_OFFSET) = (Seed)


/************************** Function Prototypes ******************************/

/*
 * Initialization functions in xtemac.c
 */
int XTemac_CfgInitialize(XTemac *InstancePtr, XTemac_Config *CfgPtr,
			 u32 VirtualAddress);
int XTemac_Start(XTemac *InstancePtr);
void XTemac_Stop(XTemac *InstancePtr);
void XTemac_Reset(XTemac *InstancePtr, int HardCoreAction);

/*
 * Initialization functions in xtemac_sinit.c
 */
int XTemac_Initialize(XTemac *InstancePtr, u16 DeviceId);
int XTemac_VmInitialize(XTemac *InstancePtr, u16 DeviceId, u32 VirtualAddress);
XTemac_Config *XTemac_LookupConfig(u16 DeviceId);

/*
 * General interrupt-related functions in xtemac_intr.c
 */
int XTemac_SetHandler(XTemac *InstancePtr, u32 HandlerType,
		      void *CallbackFunc, void *CallbackRef);

/*
 * Fifo direct mode functions implemented in xtemac_fifo.c
 */
int XTemac_FifoWrite(XTemac *InstancePtr, void *BufPtr, u32 ByteCount, int Eop);
int XTemac_FifoSend(XTemac *InstancePtr, u32 TxByteCount);

int XTemac_FifoRecv(XTemac *InstancePtr, u32 *ByteCountPtr);
int XTemac_FifoRead(XTemac *InstancePtr, void *BufPtr, u32 ByteCount, int Eop);
u32 XTemac_FifoGetFreeBytes(XTemac *InstancePtr, u32 Direction);

int XTemac_FifoQuerySendStatus(XTemac *InstancePtr, u32 *SendStatusPtr);
int XTemac_FifoQueryRecvStatus(XTemac *InstancePtr);

/*
 * Interrupt management functions for FIFO direct mode implemented in
 * xtemac_intr_fifo.c.
 */
void XTemac_IntrFifoEnable(XTemac *InstancePtr, u32 Direction);
void XTemac_IntrFifoDisable(XTemac *InstancePtr, u32 Direction);
extern void XTemac_IntrFifoHandler(void *InstancePtr);

/*
 * SG DMA mode functions implemented in xtemac_sgdma.c
 */
int XTemac_SgAlloc(XTemac *InstancePtr, u32 Direction,
		   unsigned NumBd, XDmaBdV3 ** BdPtr);
int XTemac_SgUnAlloc(XTemac *InstancePtr, u32 Direction,
		     unsigned NumBd, XDmaBdV3 * BdPtr);
int XTemac_SgCommit(XTemac *InstancePtr, u32 Direction,
		    unsigned NumBd, XDmaBdV3 * BdPtr);
unsigned XTemac_SgGetProcessed(XTemac *InstancePtr, u32 Direction,
			       unsigned NumBd, XDmaBdV3 ** BdPtr);
int XTemac_SgFree(XTemac *InstancePtr, u32 Direction,
		  unsigned NumBd, XDmaBdV3 * BdPtr);

int XTemac_SgCheck(XTemac *InstancePtr, u32 Direction);

int XTemac_SgSetSpace(XTemac *InstancePtr, u32 Direction,
		      u32 PhysicalAddr, u32 VirtualAddr,
		      u32 Alignment, unsigned BdCount, XDmaBdV3 * BdTemplate);

/*
 * Interrupt management functions for SG DMA mode implemented in
 * xtemac_intr_sgdma.c
 */
void XTemac_IntrSgEnable(XTemac *InstancePtr, u32 Direction);
void XTemac_IntrSgDisable(XTemac *InstancePtr, u32 Direction);
int XTemac_IntrSgCoalSet(XTemac *InstancePtr, u32 Direction,
			 u16 Threshold, u16 Timer);
int XTemac_IntrSgCoalGet(XTemac *InstancePtr, u32 Direction,
			 u16 *ThresholdPtr, u16 *TimerPtr);

extern void XTemac_IntrSgHandler(void *InstancePtr);

/*
 * MAC configuration/control functions in xtemac_control.c
 */
int XTemac_SetOptions(XTemac *InstancePtr, u32 Options);
int XTemac_ClearOptions(XTemac *InstancePtr, u32 Options);
u32 XTemac_GetOptions(XTemac *InstancePtr);

int XTemac_SetMacAddress(XTemac *InstancePtr, void *AddressPtr);
void XTemac_GetMacAddress(XTemac *InstancePtr, void *AddressPtr);

int XTemac_SetMacPauseAddress(XTemac *InstancePtr, void *AddressPtr);
void XTemac_GetMacPauseAddress(XTemac *InstancePtr, void *AddressPtr);
int XTemac_SendPausePacket(XTemac *InstancePtr, u16 PauseValue);

int XTemac_GetSgmiiStatus(XTemac *InstancePtr, u16 *SpeedPtr);
int XTemac_GetRgmiiStatus(XTemac *InstancePtr, u16 *SpeedPtr,
			  u32 *IsFullDuplexPtr, u32 *IsLinkUpPtr);
u16 XTemac_GetOperatingSpeed(XTemac *InstancePtr);
void XTemac_SetOperatingSpeed(XTemac *InstancePtr, u16 Speed);

void XTemac_PhySetMdioDivisor(XTemac *InstancePtr, u8 Divisor);
int XTemac_PhyRead(XTemac *InstancePtr, u32 PhyAddress,
		   u32 RegisterNum, u16 *PhyDataPtr);
int XTemac_PhyWrite(XTemac *InstancePtr, u32 PhyAddress,
		    u32 RegisterNum, u16 PhyData);
int XTemac_MulticastAdd(XTemac *InstancePtr, void *AddressPtr, int Entry);
void XTemac_MulticastGet(XTemac *InstancePtr, void *AddressPtr, int Entry);
int XTemac_MulticastClear(XTemac *InstancePtr, int Entry);

/*
 * Statistics in xtemac_stats.c
 */
void XTemac_GetSoftStats(XTemac *InstancePtr, XTemac_SoftStats *StatsPtr);
void XTemac_ClearSoftStats(XTemac *InstancePtr);

/*
 * Diagnostic functions in xtemac_selftest.c
 */
int XTemac_SelfTest(XTemac *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */
