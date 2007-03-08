/* $Id: xbuf_descriptor.h,v 1.1 2006/12/13 14:21:30 imanuilov Exp $ */
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
*       (c) Copyright 2001-2004 Xilinx Inc.
*       All rights reserved.
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xbuf_descriptor.h
*
* <b>Description</b>
*
* This file contains the interface for the XBufDescriptor component.
* The XBufDescriptor component is a passive component that only maps over
* a buffer descriptor data structure shared by the scatter gather DMA hardware
* and software. The component's primary purpose is to provide encapsulation of
* the buffer descriptor processing.  See the source file xbuf_descriptor.c for
* details.
*
* @note
*
* Most of the functions of this component are implemented as macros in order
* to optimize the processing.  The names are not all uppercase such that they
* can be switched between macros and functions easily.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a xd   10/27/04 Doxygenated for inclusion in API documentation
* 1.00b ecm  10/31/05 Updated for the check sum offload changes.
* </pre>
*
******************************************************************************/

#ifndef XBUF_DESCRIPTOR_H	/* prevent circular inclusions */
#define XBUF_DESCRIPTOR_H	/* by using protection macros */

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xdma_channel_i.h"

/************************** Constant Definitions *****************************/

/** @name Buffer Descriptor fields
 *
 * @{
 */
/** This constant allows access to fields of a buffer descriptor
 * and is necessary at this level of visibility to allow macros to access
 * and modify the fields of a buffer descriptor.  It is not expected that the
 * user of a buffer descriptor would need to use this constant.
 */
#define XBD_DEVICE_STATUS_OFFSET    0
#define XBD_CONTROL_OFFSET          1
#define XBD_SOURCE_OFFSET           2
#define XBD_DESTINATION_OFFSET      3
#define XBD_LENGTH_OFFSET           4
#define XBD_STATUS_OFFSET           5
#define XBD_NEXT_PTR_OFFSET         6
#define XBD_ID_OFFSET               7
#define XBD_FLAGS_OFFSET            8
#define XBD_RQSTED_LENGTH_OFFSET    9
#define XBD_SIZE_IN_WORDS           10
/* @} */

/**
 * The following constants define the bits of the flags field of a buffer
 * descriptor
 */
#define XBD_FLAGS_LOCKED_MASK       1UL

/**************************** Type Definitions *******************************/

typedef u32 XBufDescriptor[XBD_SIZE_IN_WORDS];

/***************** Macros (Inline Functions) Definitions *********************/

/**
 * each of the following macros are named the same as functions rather than all
 * upper case in order to allow either the macros or the functions to be
 * used, see the source file xbuf_descriptor.c for documentation
 */


/*****************************************************************************/
/**
*
* This function initializes a buffer descriptor component by zeroing all of the
* fields of the buffer descriptor.  This function should be called prior to
* using a buffer descriptor.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
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
#define XBufDescriptor_Initialize(InstancePtr)                  \
{                                                               \
    (*((u32 *)InstancePtr + XBD_CONTROL_OFFSET) = 0);       \
    (*((u32 *)InstancePtr + XBD_SOURCE_OFFSET) = 0);        \
    (*((u32 *)InstancePtr + XBD_DESTINATION_OFFSET) = 0);   \
    (*((u32 *)InstancePtr + XBD_LENGTH_OFFSET) = 0);        \
    (*((u32 *)InstancePtr + XBD_STATUS_OFFSET) = 0);        \
    (*((u32 *)InstancePtr + XBD_DEVICE_STATUS_OFFSET) = 0); \
    (*((u32 *)InstancePtr + XBD_NEXT_PTR_OFFSET) = 0);      \
    (*((u32 *)InstancePtr + XBD_ID_OFFSET) = 0);            \
    (*((u32 *)InstancePtr + XBD_FLAGS_OFFSET) = 0);         \
    (*((u32 *)InstancePtr + XBD_RQSTED_LENGTH_OFFSET) = 0); \
}

/*****************************************************************************/
/**
*
* This function gets the control field of a buffer descriptor component.  The
* DMA channel hardware transfers the control field from the buffer descriptor
* into the DMA control register when a buffer descriptor is processed.  It
* controls the details of the DMA transfer.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @return
*
* The control field contents of the buffer descriptor. One or more of the
* following values may be contained the field.  Each of the values are
* unique bit masks.
*                               <br><br>
* - XDC_DMACR_SOURCE_INCR_MASK  Increment the source address
*                               <br><br>
* - XDC_DMACR_DEST_INCR_MASK    Increment the destination address
*                               <br><br>
* - XDC_DMACR_SOURCE_LOCAL_MASK Local source address
*                               <br><br>
* - XDC_DMACR_DEST_LOCAL_MASK   Local destination address
*                               <br><br>
* - XDC_DMACR_SG_ENABLE_MASK    Scatter gather enable
*                               <br><br>
* - XDC_DMACR_GEN_BD_INTR_MASK  Individual buffer descriptor interrupt
*                               <br><br>
* - XDC_DMACR_LAST_BD_MASK      Last buffer descriptor in a packet
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetControl(InstancePtr)   \
    (u32)(*((u32 *)InstancePtr + XBD_CONTROL_OFFSET))

/*****************************************************************************/
/**
*
* This function sets the control field of a buffer descriptor component.  The
* DMA channel hardware transfers the control field from the buffer descriptor
* into the DMA control register when a buffer descriptor is processed.  It
* controls the details of the DMA transfer such as
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @param
*
* Control contains the value to be written to the control field of the buffer
* descriptor. One or more of the following values may be contained the field.
* Each of the values are unique bit masks such that they may be ORed together
* to enable multiple bits or inverted and ANDed to disable multiple bits.
* - XDC_DMACR_SOURCE_INCR_MASK  Increment the source address
* - XDC_DMACR_DEST_INCR_MASK    Increment the destination address
* - XDC_DMACR_SOURCE_LOCAL_MASK Local source address
* - XDC_DMACR_DEST_LOCAL_MASK   Local destination address
* - XDC_DMACR_SG_ENABLE_MASK    Scatter gather enable
* - XDC_DMACR_GEN_BD_INTR_MASK  Individual buffer descriptor interrupt
* - XDC_DMACR_LAST_BD_MASK      Last buffer descriptor in a packet
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
#define XBufDescriptor_SetControl(InstancePtr, Control)  \
    (*((u32 *)InstancePtr + XBD_CONTROL_OFFSET) = (u32)Control)

/*****************************************************************************/
/**
*
* This function determines if this buffer descriptor is marked as being the
* last in the control field.  A packet may be broken up across multiple
* buffer descriptors such that the last buffer descriptor is the end of the
* packet.  The DMA channel hardware copies the control field from the buffer
* descriptor to the control register of the DMA channel when the buffer
* descriptor is processed.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @return
*
* TRUE if the buffer descriptor is marked as last in the control field,
* otherwise, FALSE.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_IsLastControl(InstancePtr) \
    (u32)((*((u32 *)InstancePtr + XBD_CONTROL_OFFSET) & \
               XDC_CONTROL_LAST_BD_MASK) == XDC_CONTROL_LAST_BD_MASK)

/*****************************************************************************/
/**
*
* This function marks the buffer descriptor as being last in the control
* field of the buffer descriptor.  A packet may be broken up across multiple
* buffer descriptors such that the last buffer descriptor is the end of the
* packet.  The DMA channel hardware copies the control field from the buffer
* descriptor to the control register of the DMA channel when the buffer
* descriptor is processed.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
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
#define XBufDescriptor_SetLast(InstancePtr) \
    (*((u32 *)InstancePtr + XBD_CONTROL_OFFSET) |= XDC_CONTROL_LAST_BD_MASK)

/*****************************************************************************/
/**
*
* This function gets the source address field of the buffer descriptor.
* The source address indicates the address of memory which is the
* source of a DMA scatter gather operation.  The DMA channel hardware
* copies the source address from the buffer descriptor to the source
* address register of the DMA channel when the buffer descriptor is processed.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @return
*
* The source address field of the buffer descriptor.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetSrcAddress(InstancePtr) \
    ((u32 *)(*((u32 *)InstancePtr + XBD_SOURCE_OFFSET)))

/*****************************************************************************/
/**
*
* This function sets the source address field of the buffer descriptor.
* The source address indicates the address of memory which is the
* source of a DMA scatter gather operation.  The DMA channel hardware
* copies the source address from the buffer descriptor to the source
* address register of the DMA channel when the buffer descriptor is processed.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @param
*
* SourceAddress contains the source address field for the buffer descriptor.
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
#define XBufDescriptor_SetSrcAddress(InstancePtr, Source) \
    (*((u32 *)InstancePtr + XBD_SOURCE_OFFSET) = (u32)Source)

/*****************************************************************************/
/**
*
* This function gets the destination address field of the buffer descriptor.
* The destination address indicates the address of memory which is the
* destination of a DMA scatter gather operation.  The DMA channel hardware
* copies the destination address from the buffer descriptor to the destination
* address register of the DMA channel when the buffer descriptor is processed.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @return
*
* The destination address field of the buffer descriptor.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetDestAddress(InstancePtr) \
    ((u32 *)(*((u32 *)InstancePtr + XBD_DESTINATION_OFFSET)))

/*****************************************************************************/
/**
*
* This function sets the destination address field of the buffer descriptor.
* The destination address indicates the address of memory which is the
* destination of a DMA scatter gather operation.  The DMA channel hardware
* copies the destination address from the buffer descriptor to the destination
* address register of the DMA channel when the buffer descriptor is processed.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @param
*
* DestinationAddress contains the destination address field for the buffer
* descriptor.
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
#define XBufDescriptor_SetDestAddress(InstancePtr, Destination) \
    (*((u32 *)InstancePtr + XBD_DESTINATION_OFFSET) = (u32)Destination)

/*****************************************************************************/
/**
*
* This function gets the length of the data transfer if the buffer descriptor
* has been processed by the DMA channel hardware.  If the buffer descriptor
* has not been processed, the return value will be zero indicating that no data
* has been transferred yet.  This function uses both the length and requested
* length fields of the buffer descriptor to determine the number of bytes
* transferred by the DMA operation. The length field of the buffer descriptor
* contains the number of bytes remaining from the requested length.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @return
*
* The number of bytes which have been transferred by a DMA operation on the
* buffer descriptor.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetLength(InstancePtr)                           \
    (u32)(*((u32 *)InstancePtr + XBD_RQSTED_LENGTH_OFFSET) -    \
              *((u32 *)InstancePtr + XBD_LENGTH_OFFSET))

/*****************************************************************************/
/**
*
* This function sets the length and the requested length fields of the buffer
* descriptor.  The length field indicates the number of bytes to transfer for
* the DMA operation and the requested length is written with the same value.
* The requested length is not modified by the DMA hardware while the length
* field is modified by the hardware to indicate the number of bytes remaining
* in the transfer after the transfer is complete.  The requested length allows
* the software to calculate the actual number of bytes transferred for the DMA
* operation.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @param
*
* Length contains the length to put in the length and requested length fields
* of the buffer descriptor.
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
#define XBufDescriptor_SetLength(InstancePtr, Length)                       \
{                                                                           \
    (*((u32 *)InstancePtr + XBD_LENGTH_OFFSET) = (u32)(Length));    \
    (*((u32 *)InstancePtr + XBD_RQSTED_LENGTH_OFFSET) = (u32)(Length));\
}

/*****************************************************************************/
/**
*
* This function gets the status field of a buffer descriptor component. The
* status field is written to the buffer descriptor by the DMA channel hardware
* after processing of a buffer descriptor is complete.  The status field
* indicates the status of the DMA operation.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @return
*
* The status field contents of the buffer descriptor. One or more of the
* following values may be contained the field. Each of the values are
* unique bit masks.
*                               <br><br>
* - XDC_DMASR_BUSY_MASK         The DMA channel is busy
*                               <br><br>
* - XDC_DMASR_BUS_ERROR_MASK    A bus error occurred
*                               <br><br>
* - XDC_DMASR_BUS_TIMEOUT_MASK  A bus timeout occurred
*                               <br><br>
* - XDC_DMASR_LAST_BD_MASK      The last buffer descriptor of a packet
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetStatus(InstancePtr)    \
    (u32)(*((u32 *)InstancePtr + XBD_STATUS_OFFSET))

/*****************************************************************************/
/**
*
* This function sets the status field of a buffer descriptor component.  The
* status field is written to the buffer descriptor by the DMA channel hardware
* after processing of a buffer descriptor is complete.  This function would
* typically be used during debugging of buffer descriptor processing.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @param
*
* Status contains the status field for the buffer descriptor.
* The status register contents of the DMA channel. One or more of the
* following values may be contained the register. Each of the values are
* unique bit masks.
* - XDC_DMASR_BUSY_MASK         The DMA channel is busy
* - XDC_DMASR_BUS_ERROR_MASK    A bus error occurred
* - XDC_DMASR_BUS_TIMEOUT_MASK  A bus timeout occurred
* - XDC_DMASR_LAST_BD_MASK      The last buffer descriptor of a packet
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
#define XBufDescriptor_SetStatus(InstancePtr, Status)    \
    (*((u32 *)InstancePtr + XBD_STATUS_OFFSET) = (u32)Status)

/*****************************************************************************/
/**
*
* This function determines if this buffer descriptor is marked as being the
* last in the status field.  A packet may be broken up across multiple
* buffer descriptors such that the last buffer descriptor is the end of the
* packet.  The DMA channel hardware copies the status register contents to
* the buffer descriptor of the DMA channel after processing of the buffer
* descriptor is complete.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @return
*
* TRUE if the buffer descriptor is marked as last in the status field,
* otherwise, FALSE.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_IsLastStatus(InstancePtr) \
    (u32)((*((u32 *)InstancePtr + XBD_STATUS_OFFSET) & \
               XDC_STATUS_LAST_BD_MASK) == XDC_STATUS_LAST_BD_MASK)

/*****************************************************************************/
/**
*
* This function gets the device status field of the buffer descriptor.  The
* device status is device specific such that the definition of the contents
* of this field are not defined in this function. The device is defined as the
* device which is using the DMA channel, such as an ethernet controller.  The
* DMA channel hardware copies the contents of the device status register into
* the buffer descriptor when processing of the buffer descriptor is complete.
* This value is typically used by the device driver for the device to determine
* the status of the DMA operation with respect to the device.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @return
*
* The device status field of the buffer descriptor.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetDeviceStatus(InstancePtr) \
    ((u32)(*((u32 *)InstancePtr + XBD_DEVICE_STATUS_OFFSET)))

/*****************************************************************************/
/**
*
* This function sets the device status field of the buffer descriptor.  The
* device status is device specific such that the definition of the contents
* of this field are not defined in this function. The device is defined as the
* device which is using the DMA channel, such as an ethernet controller.  This
* function is typically only used for debugging/testing.
*
* The DMA channel hardware copies the contents of the device status register
* into the buffer descriptor when processing of the buffer descriptor is
* complete.  This value is typically used by the device driver for the device
* to determine the status of the DMA operation with respect to the device.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @param
*
* Status contains the device status field for the buffer descriptor.
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
#define XBufDescriptor_SetDeviceStatus(InstancePtr, Status) \
{                                                                   \
    u32 Register;                                               \
    Register = (*((u32 *)InstancePtr + XBD_DEVICE_STATUS_OFFSET));     \
    Register &= XDC_DMASR_RX_CS_RAW_MASK;                         \
    (*((u32 *)InstancePtr + XBD_DEVICE_STATUS_OFFSET)) =               \
              Register | ((u32) (Status));               \
}

/*****************************************************************************/
/**
*
* This function gets the next pointer field of the buffer descriptor.  This
* field is used to link the buffer descriptors together such that multiple DMA
* operations can be automated for scatter gather.  It also allows a single
* packet to be broken across multiple buffer descriptors.  The DMA channel
* hardware traverses the list of buffer descriptors using the next pointer
* of each buffer descriptor.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @return
*
* The next pointer field of the buffer descriptor.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetNextPtr(InstancePtr) \
    (XBufDescriptor *)(*((u32 *)InstancePtr + XBD_NEXT_PTR_OFFSET))

/*****************************************************************************/
/**
*
* This function sets the next pointer field of the buffer descriptor.  This
* field is used to link the buffer descriptors together such that many DMA
* operations can be automated for scatter gather.  It also allows a single
* packet to be broken across multiple buffer descriptors.  The DMA channel
* hardware traverses the list of buffer descriptors using the next pointer
* of each buffer descriptor.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @param
*
* NextPtr contains the next pointer field for the buffer descriptor.
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
#define XBufDescriptor_SetNextPtr(InstancePtr, NextPtr) \
    (*((u32 *)InstancePtr + XBD_NEXT_PTR_OFFSET) = (u32)NextPtr)

/*****************************************************************************/
/**
*
* This function gets the ID field of the buffer descriptor.  The ID field is
* provided to allow a device driver to correlate the buffer descriptor to other
* data structures which may be operating system specific, such as a pointer to
* a higher level memory block. The ID field is not used by the DMA channel
* hardware and is application specific.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @return
*
* The ID field of the buffer descriptor.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetId(InstancePtr) \
    (u32)(*((u32 *)InstancePtr + XBD_ID_OFFSET))

/*****************************************************************************/
/**
*
* This function sets the ID field of the buffer descriptor.  The ID field is
* provided to allow a device driver to correlate the buffer descriptor to other
* data structures which may be operating system specific, such as a pointer to
* a higher level memory block. The ID field is not used by the DMA channel
* hardware and is application specific.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @param
*
* Id contains the ID field for the buffer descriptor.
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
#define XBufDescriptor_SetId(InstancePtr, Id) \
    (*((u32 *)InstancePtr + XBD_ID_OFFSET) = (u32)Id)

/*****************************************************************************/
/**
*
* This function gets the flags field of the buffer descriptor.  The flags
* field is not used by the DMA channel hardware and is used for software
* processing of buffer descriptors.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @return
*
* The flags field of the buffer descriptor.  The field may contain one or more
* of the following values which are bit masks.
*                               <br><br>
* - XBD_FLAGS_LOCKED_MASK       Indicates the buffer descriptor is locked
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetFlags(InstancePtr) \
    (u32)(*((u32 *)InstancePtr + XBD_FLAGS_OFFSET))

/*****************************************************************************/
/**
*
* This function sets the flags field of the buffer descriptor.  The flags
* field is not used by the DMA channel hardware and is used for software
* processing of buffer descriptors.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @param
*
* Flags contains the flags field for the buffer descriptor.  The field may
* contain one or more of the following values which are bit masks.
* - XBD_FLAGS_LOCKED_MASK       Indicates the buffer descriptor is locked
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
#define XBufDescriptor_SetFlags(InstancePtr, Flags) \
    (*((u32 *)InstancePtr + XBD_FLAGS_OFFSET) = (u32)Flags)

/*****************************************************************************/
/**
*
* This function locks the buffer descriptor. A lock is specific to the
* scatter gather processing and prevents a buffer descriptor from being
* overwritten in the scatter gather list.  This field is not used by the DMA
* channel hardware such that the hardware could still write to the buffer
* descriptor.  Locking a buffer descriptor is application specific and not
* necessary to allow the DMA channel to use the buffer descriptor, but is
* provided for flexibility in designing device drivers.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
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
#define XBufDescriptor_Lock(InstancePtr) \
    (*((u32 *)InstancePtr + XBD_FLAGS_OFFSET) |= XBD_FLAGS_LOCKED_MASK)

/*****************************************************************************/
/**
*
* This function unlocks the buffer descriptor.  A lock is specific to the
* scatter gather processing and prevents a buffer descriptor from being
* overwritten in the scatter gather list.  This field is not used by the DMA
* channel hardware such that the hardware could still write to the buffer
* descriptor.  Locking a buffer descriptor is application specific and not
* necessary to allow the DMA channel to use the buffer descriptor, but is
* provided for flex ability in designing device drivers.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
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
#define XBufDescriptor_Unlock(InstancePtr) \
    (*((u32 *)InstancePtr + XBD_FLAGS_OFFSET) &= ~XBD_FLAGS_LOCKED_MASK)

/*****************************************************************************/
/**
*
* This function determines if the buffer descriptor is locked.  The lock
* is not used by the DMA channel hardware and is used for software processing
* of buffer descriptors.
*
* @param
*
* InstancePtr points to the buffer descriptor to operate on.
*
* @return
*
* TRUE if the buffer descriptor is locked, otherwise FALSE.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_IsLocked(InstancePtr) \
    (u32) ((*((u32 *)InstancePtr + XBD_FLAGS_OFFSET) & \
        XBD_FLAGS_LOCKED_MASK) == XBD_FLAGS_LOCKED_MASK)

/*****************************************************************************/
/**
*
* This function gets the Initial value for the CS offload function.
*
* @param
*
* InstancePtr contains a pointer to the DMA channel to operate on.
*
* @return
*
* The initial value that will be used for checksum offload operation as DMA
* moves the data.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetCSInit(InstancePtr)\
(*((u32 *)InstancePtr + XBD_CONTROL_OFFSET) &= XDC_DMACR_TX_CS_INIT_MASK)

/*****************************************************************************/
/**
*
* This function Sets the Initial value for the CS offload function.
*
* @param
*
* InstancePtr contains a pointer to the DMA channel to operate on.
*
* @return
*
* None
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_SetCSInit(InstancePtr, InitialValue)            \
{                                                                   \
    u32 Register;                                               \
    Register = (*((u32 *)InstancePtr + XBD_CONTROL_OFFSET));     \
    Register &= ~XDC_DMACR_TX_CS_INIT_MASK;                         \
    (*((u32 *)InstancePtr + XBD_CONTROL_OFFSET)) =               \
              Register | ((u32) (InitialValue));               \
}
/*****************************************************************************/
/**
*
* This function gets the byte position where the CS offload function
* inserts the calculated checksum.
*
* @param
*
* InstancePtr contains a pointer to the DMA channel to operate on.
*
* @return
*
* The insert byte location value that will be used to place the results of
* the checksum offload.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetCSInsertLoc(InstancePtr)                     \
(*((u32 *)InstancePtr + XBD_DESTINATION_OFFSET) &= XDC_DAREG_CS_INSERT_MASK)

/*****************************************************************************/
/**
*
* This function sets the byte position where the CS offload function
* inserts the calculated checksum.
*
* @param
*
* InstancePtr contains a pointer to the DMA channel to operate on.
*
* @return
*
* None
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_SetCSInsertLoc(InstancePtr, InsertLocation)            \
{                                                                   \
    u32 Register;                                               \
    Register = (*((u32 *)InstancePtr + XBD_DESTINATION_OFFSET));      \
    Register &= ~XDC_DAREG_CS_INSERT_MASK;                         \
    (*((u32 *)InstancePtr + XBD_DESTINATION_OFFSET)) =                \
              Register | ((u32) (InsertLocation));             \
}

/*****************************************************************************/
/**
*
* This function gets the byte position where the CS offload function
* begins the calculation of the checksum.
*
* @param
*
* InstancePtr contains a pointer to the DMA channel to operate on.
*
* @return
*
* The insert byte location value that will be used to place the results of
* the checksum offload.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetCSBegin(InstancePtr)                      \
(u16)((*((u32 *)InstancePtr + XBD_DESTINATION_OFFSET)) >> 16)
/*****************************************************************************/
/**
*
* This function sets the byte position where the CS offload function
* begins the calculation of the checksum.
*
* @param
*
* InstancePtr contains a pointer to the DMA channel to operate on.
*
* @return
*
* None
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_SetCSBegin(InstancePtr, BeginLocation)               \
{                                                                           \
    u32 Register;                                                       \
    Register = (*((u32 *)InstancePtr + XBD_DESTINATION_OFFSET));             \
    Register &= ~XDC_DAREG_CS_BEGIN_MASK;                                 \
    (*((u32 *)InstancePtr + XBD_DESTINATION_OFFSET)) =                       \
              Register | (((u32) (BeginLocation)) << 16);               \
}
/*****************************************************************************/
/**
*
* This function gets the resulting checksum from the rx channel.
*
* @param
*
* InstancePtr contains a pointer to the DMA channel to operate on.
*
* @return
*
* The raw checksum calculation from the receive operation. It needs to
* be adjusted to remove the header and packet FCS to be correct.
*
* @note
*
* None.
*
******************************************************************************/
#define XBufDescriptor_GetCSRaw(InstancePtr)                     \
(u16)((*((u32 *)InstancePtr + XBD_DEVICE_STATUS_OFFSET)) >> 16)

/************************** Function Prototypes ******************************/

/* The following prototypes are provided to allow each of the functions to
 * be implemented as a function rather than a macro, and to provide the
 * syntax to allow users to understand how to call the macros, they are
 * commented out to prevent linker errors
 *

u32 XBufDescriptor_Initialize(XBufDescriptor* InstancePtr);

u32 XBufDescriptor_GetControl(XBufDescriptor* InstancePtr);
void XBufDescriptor_SetControl(XBufDescriptor* InstancePtr, u32 Control);

u32 XBufDescriptor_IsLastControl(XBufDescriptor* InstancePtr);
void XBufDescriptor_SetLast(XBufDescriptor* InstancePtr);

u32 XBufDescriptor_GetLength(XBufDescriptor* InstancePtr);
void XBufDescriptor_SetLength(XBufDescriptor* InstancePtr, u32 Length);

u32 XBufDescriptor_GetStatus(XBufDescriptor* InstancePtr);
void XBufDescriptor_SetStatus(XBufDescriptor* InstancePtr, u32 Status);
u32 XBufDescriptor_IsLastStatus(XBufDescriptor* InstancePtr);

u32 XBufDescriptor_GetDeviceStatus(XBufDescriptor* InstancePtr);
void XBufDescriptor_SetDeviceStatus(XBufDescriptor* InstancePtr,
                                    u32 Status);

u32 XBufDescriptor_GetSrcAddress(XBufDescriptor* InstancePtr);
void XBufDescriptor_SetSrcAddress(XBufDescriptor* InstancePtr,
                                  u32 SourceAddress);

u32 XBufDescriptor_GetDestAddress(XBufDescriptor* InstancePtr);
void XBufDescriptor_SetDestAddress(XBufDescriptor* InstancePtr,
                                   u32 DestinationAddress);

XBufDescriptor* XBufDescriptor_GetNextPtr(XBufDescriptor* InstancePtr);
void XBufDescriptor_SetNextPtr(XBufDescriptor* InstancePtr,
                               XBufDescriptor* NextPtr);

u32 XBufDescriptor_GetId(XBufDescriptor* InstancePtr);
void XBufDescriptor_SetId(XBufDescriptor* InstancePtr, u32 Id);

u32 XBufDescriptor_GetFlags(XBufDescriptor* InstancePtr);
void XBufDescriptor_SetFlags(XBufDescriptor* InstancePtr, u32 Flags);

void XBufDescriptor_Lock(XBufDescriptor* InstancePtr);
void XBufDescriptor_Unlock(XBufDescriptor* InstancePtr);
u32 XBufDescriptor_IsLocked(XBufDescriptor* InstancePtr);

u16 XBufDescriptor_GetCSInit(XBufDescriptor* InstancePtr)
void XBufDescriptor_SetCSInit(XBufDescriptor* InstancePtr, u16 InitialValue)

u16 XBufDescriptor_GetCSInsertLoc(XBufDescriptor* InstancePtr)
void XBufDescriptor_SetCSInsertLoc(XBufDescriptor* InstancePtr, u16 InsertLocation)

u16 XBufDescriptor_GetCSBegin(XBufDescriptor* InstancePtr)
void XBufDescriptor_SetCSBegin(XBufDescriptor* InstancePtr, u16 BeginLocation)

u16 XBufDescriptor_GetCSRaw(XBufDescriptor* InstancePtr)

void XBufDescriptor_Copy(XBufDescriptor* InstancePtr,
                         XBufDescriptor* DestinationPtr);

*/

#endif /* end of protection macro */
