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
*       (c) Copyright 2005-2007 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
 *
 * @file xlltemac.h
 *
 * The Xilinx Tri-Mode Ethernet driver component. This driver supports the
 * Virtex-5(TM) and Virtex-4(TM) 10/100/1000 MAC (TEMAC).
 *
 * For a full description of TEMAC features, please see the hardware spec. This driver
 * supports the following features:
 *   - Memory mapped access to host interface registers
 *   - Virtual memory support
 *   - Unicast, broadcast, and multicast receive address filtering
 *   - Full duplex operation (half duplex not supported)
 *   - Automatic source address insertion or overwrite (programmable)
 *   - Automatic PAD & FCS insertion and stripping (programmable)
 *   - Flow control
 *   - VLAN frame support
 *   - Pause frame support
 *   - Jumbo frame support
 *   - Checksum offload
 *
 * <h2>Driver Description</h2>
 *
 * The device driver enables higher layer software (e.g., an application) to
 * configure a TEMAC channel. It is intended that this driver be used in
 * cooperation with another driver (FIFO or DMA) for data communication. This
 * device driver can support multiple devices even when those devices have
 * significantly different configurations.
 *
 * <h2>Initialization & Configuration</h2>
 *
 * The XLlTemac_Config structure can be used by the driver to configure itself.
 * This configuration structure is typically created by the tool-chain based on
 * hardware build properties, although, other methods are allowed and currently
 * used in some systems.
 *
 * To support multiple runtime loading and initialization strategies employed
 * by various operating systems, the driver instance can be initialized using
 * the XLlTemac_CfgInitialze() routine.
 *
 * <h2>Interrupts and Asynchronous Callbacks</h2>
 *
 * The driver has no dependencies on the interrupt controller. It provides
 * no interrupt handlers. The application/OS software should set up its own
 * interrupt handlers if required.
 *
 * <h2>Device Reset</h2>
 *
 * When a TEMAC channel is connected up to a FIFO or DMA core in hardware,
 * errors may be reported on one of those cores (FIFO or DMA) such that it can
 * be determined that the TEMAC channel needs to be reset. If a reset is
 * performed, the calling code should also reconfigure and reapply the proper
 * settings in the TEMAC channel.
 *
 * When a TEMAC channel reset is required, XLlTemac_Reset() should be utilized.
 *
 * <h2>Virtual Memory</h2>
 *
 * This driver may be used in systems with virtual memory support by passing
 * the appropriate value for the <i>EffectiveAddress</i> parameter to the
 * XLlTemac_CfgInitialize() routine.
 *
 * <h2>Transfering Data</h2>
 *
 * The TEMAC core by itself is not cabable of transmitting or receiving data in
 * any meaninful way. Instead one or both TEMAC channels need to be connected
 * to a FIFO or DMA core in hardware.
 *
 * This TEMAC driver is modeled in a similar fashion where the application code
 * or O/S adapter driver needs to make use of a separte FIFO or DMA driver in
 * connection with this driver to establish meaningful communication over
 * ethernet.
 *
 * <h2>Checksum Offloading</h2>
 *
 * If configured, the device can compute a 16-bit checksum from frame data. In
 * most circumstances this can lead to a substantial gain in throughput.
 *
 * The checksum offload settings for each frame sent or recieved are
 * transmitted through the LocalLink interface in hardware. What this means is
 * that the checksum offload feature is indirectly controlled in the TEMAC
 * channel through the driver for the FIFO or DMA core connected to the TEMAC
 * channel.
 *
 * Refer to the documentation for the FIFO or DMA driver used for data
 * communication on how to set the values for the relevant LocalLink header
 * words.
 *
 * Since this hardware implementation is general purpose in nature system software must
 * perform pre and post frame processing to obtain the desired results for the
 * types of packets being transferred. Most of the time this will be TCP/IP
 * traffic.
 *
 * TCP/IP and UDP/IP frames contain separate checksums for the IP header and
 * UDP/TCP header+data. With this hardware implementation, the IP header checksum
 * cannot be offloaded. Many stacks that support offloading will compute the IP
 * header if required and use hardware to compute the UDP/TCP header+data checksum.
 * There are other complications concerning the IP pseudo header that must be
 * taken into consideration. Readers should consult a TCP/IP design reference
 * for more details.
 *
 * There are certain device options that will affect the checksum calculation
 * performed by hardware for Tx:
 *
 *   - FCS insertion disabled (XTE_FCS_INSERT_OPTION): software is required to
 *     calculate and insert the FCS value at the end of the frame, but the
 *     checksum must be known ahead of time prior to calculating the FCS.
 *     Therefore checksum offloading cannot be used in this situation.
 *
 * And for Rx:
 *
 *   - FCS/PAD stripping disabled (XTE_FCS_STRIP_OPTION): The 4 byte FCS at the
 *     end of frame will be included in the hardware calculated checksum. software must
 *     subtract out this data.
 *
 *   - FCS/PAD stripping disabled (XTE_FCS_STRIP_OPTION): For frames smaller
 *     than 64 bytes, padding will be included in the hardware calculated checksum.
 *     software must subtract out this data. It may be better to allow the TCP/IP
 *     stack verify checksums for this type of packet.
 *
 *   - VLAN enabled (XTE_VLAN_OPTION): The 4 extra bytes in the Ethernet header
 *     affect the hardware calculated checksum. software must subtract out the 1st two
 *     16-bit words starting at the 15th byte.
 *
 * <h3>Transmit Checksum Offloading</h3>
 *
 * For transmit, the software can specify where in the frame the checksum
 * calculation is to start, where the result should be inserted, and a seed
 * value. The checksum is calculated from the start point through the end of
 * frame.
 *
 * The checsum offloading settings are sent in the transmit LocalLink header
 * words. The relevant LocalLink header words are described in brief below.
 * Refer to the XPS_LL_TEMAC v1.00a hardware specification for more details.
 *
 *   <h4>LocalLink header word 3:</h4>
 *   <pre>
 *   Bits    31 (MSB): Transmit Checksum Enable: 1 - enabled, 0 - disabled
 *   Bits  0-30 (LSB): Reserved
 *   </pre>
 *
 *   <h4>LocalLink header word 4:</h4>
 *   <pre>
 *   Bits 16-31 (MSB): Transmit Checksum Insertion Point: Frame offset where the
 *                     computed checksum value is stored, which should be in the
 *                     TCP or UDP header
 *   Bits  0-15 (LSB): Transmit Checksum Calculation Starting Point: Offset
 *                     in the frame where checksum calculation should begin
 *   </pre>
 *
 *   <h4>LocalLink header word 5:</h4>
 *   <pre>
 *   Bits 16-31 (MSB): Transmit Checksum Calculation Initial Value: Checksum
 *                     seed value
 *   Bits  0-15 (LSB): Reserved
 *   </pre>
 *
 * <h3>Receive Checksum Offloading</h3>
 *
 * For Receive, the 15th byte to end of frame is checksummed. This range of
 * bytes is the entire Ethernet payload (for non-VLAN frames).
 *
 * The checsum offloading information is sent in the receive LocalLink header
 * words. The relevant LocalLink header words are described in brief below.
 * Refer to the XPS_LL_TEMAC v1.00a hardware specification for more details.
 *
 *   <h4>LocalLink header word 6:</h4>
 *   <pre>
 *   Bits 16-31 (MSB): Receive Raw Checksum: Computed checksum value
 *   Bits  0-15 (LSB): Reserved
 *   </pre>
 *
 * <h2>PHY Communication</h2>
 *
 * Prior to PHY access, the MDIO clock must be setup. This driver will set a
 * safe default that should work with PLB bus speeds of up to 150 MHz and keep
 * the MDIO clock below 2.5 MHz. If the user wishes faster access to the PHY
 * then the clock divisor can be set to a different value (see
 * XLlTemac_PhySetMdioDivisor()).
 *
 * MII register access is performed through the functions XLlTemac_PhyRead() and
 * XLlTemac_PhyWrite().
 *
 * <h2>Link Sync</h2>
 *
 * When the device is used in a multispeed environment, the link speed must be
 * explicitly set using XLlTemac_SetOperatingSpeed() and must match the speed the
 * PHY has negotiated. If the speeds are mismatched, then the MAC will not pass
 * traffic.
 *
 * The application/OS software may use the AutoNegotiation interrupt to be
 * notified when the PHY has completed auto-negotiation.
 *
 * <h2>Asserts</h2>
 *
 * Asserts are used within all Xilinx drivers to enforce constraints on argument
 * values. Asserts can be turned off on a system-wide basis by defining, at
 * compile time, the NDEBUG identifier. By default, asserts are turned on and it
 * is recommended that users leave asserts on during development. For deployment
 * use -DNDEBUG compiler switch to remove assert code.
 *
 * <h2>Driver Errata</h2>
 *
 *   - A dropped receive frame indication may be reported by the driver after
 *     calling XLlTemac_Stop() followed by XLlTemac_Start(). This can occur if a
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
 * 1.00a jvb  11/10/06 First release
 * 1.00a rpm  06/08/07 Added interrupt IDs to config structure for convenience
 * </pre>
 *
 *****************************************************************************/

#ifndef XTEMAC_H		/* prevent circular inclusions */
#define XTEMAC_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xenv.h"
#include "xbasic_types.h"
#include "xstatus.h"
#include "xlltemac_hw.h"

/************************** Constant Definitions *****************************/

/*
 * Device information
 */
#define XTE_DEVICE_NAME     "xlltemac"
#define XTE_DEVICE_DESC     "Xilinx Tri-speed 10/100/1000 MAC"

/* LocalLink TYPE Enumerations */
#define XPAR_LL_FIFO    1
#define XPAR_LL_DMA     2

/** @name Configuration options
 *
 * The following are device configuration options. See the
 * <i>XLlTemac_SetOptions</i>, <i>XLlTemac_ClearOptions</i> and
 * <i>XLlTemac_GetOptions</i> routines for information on how to use options.
 *
 * The default state of the options are also noted below.
 *
 * @{
 */

#define XTE_PROMISC_OPTION               0x00000001
/**< XTE_PROMISC_OPTION specifies the TEMAC channel to accept all incoming
 *   packets.
 *   This driver sets this option to disabled (cleared) by default. */

#define XTE_JUMBO_OPTION                 0x00000002
/**< XTE_JUMBO_OPTION specifies the TEMAC channel to accept jumbo frames
 *   for transmit and receive.
 *   This driver sets this option to disabled (cleared) by default. */

#define XTE_VLAN_OPTION                  0x00000004
/**< XTE_VLAN_OPTION specifies the TEMAC channel to enable VLAN support for
 *   transmit and receive.
 *   This driver sets this option to disabled (cleared) by default. */

#define XTE_FLOW_CONTROL_OPTION          0x00000008
/**< XTE_FLOW_CONTROL_OPTION specifies the TEMAC channel to recognize
 *   received flow control frames.
 *   This driver sets this option to enabled (set) by default. */

#define XTE_FCS_STRIP_OPTION             0x00000010
/**< XTE_FCS_STRIP_OPTION specifies the TEMAC channel to strip FCS and PAD
 *   from received frames. Note that PAD from VLAN frames is not stripped.
 *   This driver sets this option to enabled (set) by default. */

#define XTE_FCS_INSERT_OPTION            0x00000020
/**< XTE_FCS_INSERT_OPTION specifies the TEMAC channel to generate the FCS
 *   field and add PAD automatically for outgoing frames.
 *   This driver sets this option to enabled (set) by default. */

#define XTE_LENTYPE_ERR_OPTION           0x00000040
/**< XTE_LENTYPE_ERR_OPTION specifies the TEMAC channel to enable
 *   Length/Type error checking (mismatched type/length field) for received
 *   frames.
 *   This driver sets this option to enabled (set) by default. */

#define XTE_TRANSMITTER_ENABLE_OPTION    0x00000080
/**< XTE_TRANSMITTER_ENABLE_OPTION specifies the TEMAC channel transmitter
 *   to be enabled.
 *   This driver sets this option to enabled (set) by default. */

#define XTE_RECEIVER_ENABLE_OPTION       0x00000100
/**< XTE_RECEIVER_ENABLE_OPTION specifies the TEMAC channel receiver to be
 *   enabled.
 *   This driver sets this option to enabled (set) by default. */

#define XTE_BROADCAST_OPTION             0x00000200
/**< XTE_BROADCAST_OPTION specifies the TEMAC channel to receive frames
 *   sent to the broadcast Ethernet address.
 *   This driver sets this option to enabled (set) by default. */

#define XTE_MULTICAST_OPTION         0x00000400
/**< XTE_MULTICAST_OPTION specifies the TEMAC channel to receive frames
 *   sent to Ethernet addresses that are programmed into the Multicast Address
 *   Table (MAT).
 *   This driver sets this option to disabled (cleared) by default. */

#define XTE_DEFAULT_OPTIONS                     \
    (XTE_FLOW_CONTROL_OPTION |                  \
     XTE_BROADCAST_OPTION |                     \
     XTE_FCS_INSERT_OPTION |                    \
     XTE_FCS_STRIP_OPTION |                     \
     XTE_LENTYPE_ERR_OPTION |                   \
     XTE_TRANSMITTER_ENABLE_OPTION |            \
     XTE_RECEIVER_ENABLE_OPTION)
/**< XTE_DEFAULT_OPTIONS specify the options set in XLlTemac_Reset() and
 *   XLlTemac_CfgInitialize() */

/*@}*/

/** @name Reset parameters
 *
 *  These are used by function XLlTemac_Reset().
 * @{
 */
#define XTE_RESET_HARD    1
#define XTE_NORESET_HARD  0
/*@}*/

#define XTE_MULTI_MAT_ENTRIES       4	/* Number of storable addresses in
					   the Multicast Address Table */

#define XTE_MDIO_DIV_DFT            29	/* Default MDIO clock divisor */

/* The next few constants help upper layers determine the size of memory
 * pools used for Ethernet buffers and descriptor lists.
 */
#define XTE_MAC_ADDR_SIZE   6		/* MAC addresses are 6 bytes */
#define XTE_MTU             1500	/* max MTU size of an Ethernet frame */
#define XTE_JUMBO_MTU       8982	/* max MTU size of a jumbo Ethernet frame */
#define XTE_HDR_SIZE        14	/* size of an Ethernet header */
#define XTE_HDR_VLAN_SIZE   18	/* size of an Ethernet header with VLAN */
#define XTE_TRL_SIZE        4	/* size of an Ethernet trailer (FCS) */
#define XTE_MAX_FRAME_SIZE       (XTE_MTU + XTE_HDR_SIZE + XTE_TRL_SIZE)
#define XTE_MAX_VLAN_FRAME_SIZE  (XTE_MTU + XTE_HDR_VLAN_SIZE + XTE_TRL_SIZE)
#define XTE_MAX_JUMBO_FRAME_SIZE (XTE_JUMBO_MTU + XTE_HDR_SIZE + XTE_TRL_SIZE)

/* Constant values returned by XLlTemac_mGetPhysicalInterface(). Note that these
 * values match design parameters from the PLB_TEMAC spec
 */
#define XTE_PHY_TYPE_MII         0
#define XTE_PHY_TYPE_GMII        1
#define XTE_PHY_TYPE_RGMII_1_3   2
#define XTE_PHY_TYPE_RGMII_2_0   3
#define XTE_PHY_TYPE_SGMII       4
#define XTE_PHY_TYPE_1000BASE_X  5

/**************************** Type Definitions *******************************/


/**
 * This typedef contains configuration information for a TEMAC channel.
 * Each channel is treated as a separate device from the point of view of this
 * driver.
 */
typedef struct {
        /** u16 DeviceId;	< DeviceId is the unique ID  of the device */
	u32 BaseAddress;/**< BaseAddress is the physical base address of the
                          *  channel's registers
                          */
	u8 TxCsum;	/**< TxCsum indicates that the channel has checksum
	                  *  offload on the Tx channel or not.
	                  */
	u8 RxCsum;	/**< RxCsum indicates that the channel has checksum
	                  *  offload on the Rx channel or not.
	                  */
	u8 PhyType;	/**< PhyType indicates which type of PHY interface is
	                  *  used (MII, GMII, RGMII, ect.
	                  */
	u8 TemacIntr;	/**< TEMAC interrupt ID */

	int LLDevType;	/**< LLDevType is the type of device attached to the
			 *   temac's local link interface.
			 */
	u32 LLDevBaseAddress; /**< LLDevBaseAddress is the base address of then
			       *   device attached to the temac's local link
			       *   interface.
			       */
	u8 LLFifoIntr;	/**< LL FIFO interrupt ID (unused if DMA) */
	u8 LLDmaRxIntr;	/**< LL DMA RX interrupt ID (unused if FIFO) */
	u8 LLDmaTxIntr;	/**< LL DMA TX interrupt ID (unused if FIFO) */

} XLlTemac_Config;


/**
 * struct XLlTemac is the type for TEMAC driver instance data. The calling code
 * is required to use a unique instance of this structure for every TEMAC
 * channel used in the system. Each channel is treated as a separate device
 * from the point of view of this driver. A reference to a structure of this
 * type is then passed to the driver API functions.
 */
typedef struct XLlTemac {
	XLlTemac_Config Config;	/* hardware configuration */
	u32 IsStarted;		/* Device is currently started */
	u32 IsReady;		/* Device is initialized and ready */
	u32 Options;		/* Current options word */
	u32 Flags;		/* Internal driver flags */
} XLlTemac;


/***************** Macros (Inline Functions) Definitions *********************/

/*****************************************************************************/
/**
 *
 * XLlTemac_IsStarted reports if the device is in the started or stopped state. To
 * be in the started state, the calling code must have made a successful call to
 * <i>XLlTemac_Start</i>. To be in the stopped state, <i>XLlTemac_Stop</i> or
 * <i>XLlTemac_CfgInitialize</i> function must have been called.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 *
 * @return XLlTemac_IsStarted returns TRUE if the device has been started.
 *         Otherwise, XLlTemac_IsStarted returns FALSE.
 *
 * @note
 *
 * Signature: u32 XLlTemac_IsStarted(XLlTemac *InstancePtr)
 *
 ******************************************************************************/
#define XLlTemac_IsStarted(InstancePtr) \
	(((InstancePtr)->IsStarted == XCOMPONENT_IS_STARTED) ? TRUE : FALSE)

/*****************************************************************************/
/**
*
* XLlTemac_IsDma reports if the device is currently connected to DMA.
*
* @param InstancePtr references the TEMAC channel on which to operate.
*
* @return XLlTemac_IsDma returns TRUE if the device is connected DMA. Otherwise,
*         XLlTemac_IsDma returns FALSE.
*
* @note
*
* Signature: u32 XLlTemac_IsDma(XLlTemac *InstancePtr)
*
******************************************************************************/
#define XLlTemac_IsDma(InstancePtr) \
	(((InstancePtr)->Config.LLDevType == XPAR_LL_DMA) ? TRUE: FALSE)

/*****************************************************************************/
/**
*
* XLlTemac_IsFifo reports if the device is currently connected to a fifo core.
*
* @param InstancePtr references the TEMAC channel on which to operate.
*
* @return XLlTemac_IsFifo returns TRUE if the device is connected to a fifo core.
*         Otherwise, XLlTemac_IsFifo returns FALSE.
*
* @note
*
* Signature: u32 XLlTemac_IsFifo(XLlTemac *InstancePtr)
*
******************************************************************************/
#define XLlTemac_IsFifo(InstancePtr) \
	(((InstancePtr)->Config.LLDevType == XPAR_LL_FIFO) ? TRUE: FALSE)

/*****************************************************************************/
/**
*
* XLlTemac_LlDevBaseAddress reports the base address of the core connected to
* the TEMAC's local link interface.
*
* @param InstancePtr references the TEMAC channel on which to operate.
*
* @return XLlTemac_IsFifo returns the base address of the core connected to
* the TEMAC's local link interface.
*
* @note
*
* Signature: u32 XLlTemac_LlDevBaseAddress(XLlTemac *InstancePtr)
*
******************************************************************************/
#define XLlTemac_LlDevBaseAddress(InstancePtr) \
	((InstancePtr)->Config.LLDevBaseAddress)

/*****************************************************************************/
/**
 *
 * XLlTemac_IsRecvFrameDropped determines if the device thinks it has dropped a
 * receive frame.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 *
 * @return XLlTemac_IsRecvFrameDropped returns TRUE if the device interrupt
 * status register reports that a frame has been dropped. Otherwise,
 * XLlTemac_IsRecvFrameDropped returns FALSE.
 *
 * @note
 *
 * Signature: u32 XLlTemac_IsRecvFrameDropped(XLlTemac *InstancePtr)
 *
 ******************************************************************************/
#define XLlTemac_IsRecvFrameDropped(InstancePtr)                     \
	((XLlTemac_ReadReg((InstancePtr)->Config.BaseAddress, XTE_IS_OFFSET) \
	& XTE_INT_RXRJECT_MASK) ? TRUE : FALSE)

/*****************************************************************************/
/**
 *
 * XLlTemac_IsRxCsum determines if the device is configured with checksum
 * offloading on the receive channel.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 *
 * @return XLlTemac_IsRxCsum returns TRUE if the device is configured with
 *         checksum offloading on the receive channel. Otherwise,
 *         XLlTemac_IsRxCsum returns FALSE.
 *
 * @note
 *
 * Signature: u32 XLlTemac_IsRxCsum(XLlTemac *InstancePtr)
 *
 ******************************************************************************/
#define XLlTemac_IsRxCsum(InstancePtr) (((InstancePtr)->Config.RxCsum) ?  \
                                       TRUE : FALSE)

/*****************************************************************************/
/**
 *
 * XLlTemac_IsTxCsum determines if the device is configured with checksum
 * offloading on the transmit channel.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 *
 * @return XLlTemac_IsTxCsum returns TRUE if the device is configured with
 *         checksum offloading on the transmit channel. Otherwise,
 *         XLlTemac_IsTxCsum returns FALSE.
 *
 * @note
 *
 * Signature: u32 XLlTemac_IsTxCsum(XLlTemac *InstancePtr)
 *
 ******************************************************************************/
#define XLlTemac_IsTxCsum(InstancePtr) (((InstancePtr)->Config.TxCsum) ?  \
                                       TRUE : FALSE)

/*****************************************************************************/
/**
 *
 * XLlTemac_GetPhysicalInterface returns the type of PHY interface being used by
 * the given instance, specified by <i>InstancePtr</i>.
 *
 * @param InstancePtr references the TEMAC channel on which to operate.
 *
 * @return XLlTemac_GetPhysicalInterface returns one of XTE_PHY_TYPE_<x> where
 * <x> is MII, GMII, RGMII_1_3, RGMII_2_0, SGMII, or 1000BASE_X (defined in
 * xlltemac.h).
 *
 * @note
 *
 * Signature: int XLlTemac_GetPhysicalInterface(XLlTemac *InstancePtr)
 *
 ******************************************************************************/
#define XLlTemac_GetPhysicalInterface(InstancePtr)       \
	((InstancePtr)->Config.PhyType)

/****************************************************************************/
/**
*
* XLlTemac_Status returns a bit mask of the interrupt status register (ISR).
* XLlTemac_Status can be used to query the status without having to have
* interrupts enabled.
*
* @param    InstancePtr references the TEMAC channel on which to operate.
*
* @return   XLlTemac_IntStatus returns a bit mask of the status conditions.
*           The mask will be a set of bitwise or'd values from the
*           <code>XTE_INT_*_MASK</code> preprocessor symbols.
*
* @note
* C-style signature:
*    u32 XLlTemac_IntStatus(XLlTemac *InstancePtr)
*
*****************************************************************************/
#define XLlTemac_Status(InstancePtr) \
	 XLlTemac_ReadReg((InstancePtr)->Config.BaseAddress, XTE_IS_OFFSET)

/****************************************************************************/
/**
*
* XLlTemac_IntEnable enables the interrupts specified in <i>Mask</i>. The
* corresponding interrupt for each bit set to 1 in <i>Mask</i>, will be
* enabled.
*
* @param    InstancePtr references the TEMAC channel on which to operate.
*
* @param    Mask contains a bit mask of the interrupts to enable. The mask
*           can be formed using a set of bitwise or'd values from the
*           <code>XTE_INT_*_MASK</code> preprocessor symbols.
*
* @return   N/A
*
* @note
* C-style signature:
*    void XLlTemac_IntEnable(XLlTemac *InstancePtr, u32 Mask)
*
*****************************************************************************/
#define XLlTemac_IntEnable(InstancePtr, Mask) \
	XLlTemac_WriteReg((InstancePtr)->Config.BaseAddress, XTE_IE_OFFSET, \
		XLlTemac_ReadReg((InstancePtr)->Config.BaseAddress, \
				XTE_IE_OFFSET) | ((Mask) & XTE_INT_ALL_MASK)); \

/****************************************************************************/
/**
*
* XLlTemac_IntDisable disables the interrupts specified in <i>Mask</i>. The
* corresponding interrupt for each bit set to 1 in <i>Mask</i>, will be
* disabled. In other words, XLlTemac_IntDisable uses the "set a bit to clear it"
* scheme.
*
* @param    InstancePtr references the TEMAC channel on which to operate.
*
* @param    Mask contains a bit mask of the interrupts to disable. The mask
*           can be formed using a set of bitwise or'd values from the
*           <code>XTE_INT_*_MASK</code> preprocessor symbols.
*
* @return   N/A
*
* @note
* C-style signature:
*    void XLlTemac_IntDisable(XLlTemac *InstancePtr, u32 Mask)
*
*****************************************************************************/
#define XLlTemac_IntDisable(InstancePtr, Mask) \
	XLlTemac_WriteReg((InstancePtr)->Config.BaseAddress, XTE_IE_OFFSET, \
		XLlTemac_ReadReg((InstancePtr)->Config.BaseAddress, \
				XTE_IE_OFFSET) & ~((Mask) & XTE_INT_ALL_MASK)); \

/****************************************************************************/
/**
*
* XLlTemac_IntPending returns a bit mask of the pending interrupts. Each bit
* set to 1 in the return value represents a pending interrupt.
*
* @param    InstancePtr references the TEMAC channel on which to operate.
*
* @return   XLlTemac_IntPending returns a bit mask of the interrupts that are
*           pending. The mask will be a set of bitwise or'd values from the
*           <code>XTE_INT_*_MASK</code> preprocessor symbols.
*
* @note
* C-style signature:
*    u32 XLlTemac_IntPending(XLlTemac *InstancePtr)
*
*****************************************************************************/
#define XLlTemac_IntPending(InstancePtr) \
	XLlTemac_ReadReg((InstancePtr)->Config.BaseAddress, XTE_IP_OFFSET)

/****************************************************************************/
/**
*
* XLlTemac_IntClear clears pending interrupts specified in <i>Mask</i>.
* The corresponding pending interrupt for each bit set to 1 in <i>Mask</i>,
* will be cleared. In other words, XLlTemac_IntClear uses the "set a bit to
* clear it" scheme.
*
* @param    InstancePtr references the TEMAC channel on which to operate.
*
* @param    Mask contains a bit mask of the pending interrupts to clear. The
*           mask can be formed using a set of bitwise or'd values from the
*           <code>XTE_INT_*_MASK</code> preprocessor symbols.
*
* @note
* C-style signature:
*    void XLlTemac_IntClear(XLlTemac *InstancePtr, u32 Mask)
*
*****************************************************************************/
#define XLlTemac_IntClear(InstancePtr, Mask) \
	XLlTemac_WriteReg((InstancePtr)->Config.BaseAddress, XTE_IS_OFFSET, \
			((Mask) & XTE_INT_ALL_MASK))

/************************** Function Prototypes ******************************/

/*
 * Initialization functions in xlltemac.c
 */
int XLlTemac_CfgInitialize(XLlTemac *InstancePtr, XLlTemac_Config *CfgPtr,
			   u32 VirtualAddress);
void XLlTemac_Start(XLlTemac *InstancePtr);
void XLlTemac_Stop(XLlTemac *InstancePtr);
void XLlTemac_Reset(XLlTemac *InstancePtr, int HardCoreAction);

/*
 * Initialization functions in xlltemac_sinit.c
 */
XLlTemac_Config *XLlTemac_LookupConfig(u16 DeviceId);

/*
 * MAC configuration/control functions in xlltemac_control.c
 */
int XLlTemac_SetOptions(XLlTemac *InstancePtr, u32 Options);
int XLlTemac_ClearOptions(XLlTemac *InstancePtr, u32 Options);
u32 XLlTemac_GetOptions(XLlTemac *InstancePtr);

int XLlTemac_SetMacAddress(XLlTemac *InstancePtr, void *AddressPtr);
void XLlTemac_GetMacAddress(XLlTemac *InstancePtr, void *AddressPtr);

int XLlTemac_SetMacPauseAddress(XLlTemac *InstancePtr, void *AddressPtr);
void XLlTemac_GetMacPauseAddress(XLlTemac *InstancePtr, void *AddressPtr);
int XLlTemac_SendPausePacket(XLlTemac *InstancePtr, u16 PauseValue);

int XLlTemac_GetSgmiiStatus(XLlTemac *InstancePtr, u16 *SpeedPtr);
int XLlTemac_GetRgmiiStatus(XLlTemac *InstancePtr, u16 *SpeedPtr,
			    int *IsFullDuplexPtr, int *IsLinkUpPtr);
u16 XLlTemac_GetOperatingSpeed(XLlTemac *InstancePtr);
void XLlTemac_SetOperatingSpeed(XLlTemac *InstancePtr, u16 Speed);

void XLlTemac_PhySetMdioDivisor(XLlTemac *InstancePtr, u8 Divisor);
void XLlTemac_PhyRead(XLlTemac *InstancePtr, u32 PhyAddress, u32 RegisterNum,
		      u16 *PhyDataPtr);
void XLlTemac_PhyWrite(XLlTemac *InstancePtr, u32 PhyAddress, u32 RegisterNum,
		       u16 PhyData);
int XLlTemac_MulticastAdd(XLlTemac *InstancePtr, void *AddressPtr, int Entry);
void XLlTemac_MulticastGet(XLlTemac *InstancePtr, void *AddressPtr, int Entry);
int XLlTemac_MulticastClear(XLlTemac *InstancePtr, int Entry);

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */
