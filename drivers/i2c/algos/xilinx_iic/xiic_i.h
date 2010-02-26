/* $Id: xiic_i.h,v 1.1 2007/12/03 15:44:58 meinelte Exp $ */
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
*       (c) Copyright 2002-07 Xilinx Inc.
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
* @file xiic_i.h
*
* This header file contains internal identifiers, which are those shared
* between XIic components.  The identifiers in this file are not intended for
* use external to the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.01a rfp  10/19/01 release
* 1.01c ecm  12/05/02 new rev
* 1.13a wgr  03/22/07 Converted to new coding style.
* </pre>
*
******************************************************************************/

#ifndef XIIC_I_H		/* prevent circular inclusions */
#define XIIC_I_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/

#include "xbasic_types.h"
#include "xstatus.h"
#include "xiic.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/******************************************************************************
*
* This macro sends the first byte of the address for a 10 bit address during
* both read and write operations. It takes care of the details to format the
* address correctly.
*
* address = 1111_0xxD   xx = address MSBits
*                        D = Tx direction = 0 = write
*
* @param	SlaveAddress contains the address of the slave to send to.
* @param	Operation indicates XIIC_READ_OPERATION or XIIC_WRITE_OPERATION
*
* @return	None.
*
* @note		Signature:
*		void XIic_mSend10BitAddrByte1(u16 SlaveAddress, u8 Operation);
*
******************************************************************************/
#define XIic_mSend10BitAddrByte1(SlaveAddress, Operation)                  \
{                                                                            \
    u8 LocalAddr = (u8)((SlaveAddress) >> 7);                        \
    LocalAddr = (LocalAddr & 0xF6) | 0xF0 | (Operation);                     \
    XIo_Out8(InstancePtr->BaseAddress + XIIC_DTR_REG_OFFSET, LocalAddr);  \
}

/******************************************************************************
*
* This macro sends the second byte of the address for a 10 bit address during
* both read and write operations. It takes care of the details to format the
* address correctly.
*
* @param	SlaveAddress contains the address of the slave to send to.
*
* @return	None.
*
* @note		Signature: void XIic_mSend10BitAddrByte2(u16 SlaveAddress,
*                                            u8 Operation);
*
******************************************************************************/
#define XIic_mSend10BitAddrByte2(SlaveAddress)                        \
    XIo_Out8(InstancePtr->BaseAddress + XIIC_DTR_REG_OFFSET,         \
             (u8)(SlaveAddress));

/******************************************************************************
*
* This macro sends the address for a 7 bit address during both read and write
* operations. It takes care of the details to format the address correctly.
*
* @param	SlaveAddress contains the address of the slave to send to.
* @param	Operation indicates XIIC_READ_OPERATION or XIIC_WRITE_OPERATION
*
* @return	None.
*
* @note		Signature:
*		void XIic_mSend7BitAddr(u16 SlaveAddress, u8 Operation);
*
******************************************************************************/
#define XIic_mSend7BitAddr(SlaveAddress, Operation)                          \
{                                                                            \
    u8 LocalAddr = (u8)(SlaveAddress << 1);                          \
    LocalAddr = (LocalAddr & 0xFE) | (Operation);                            \
    XIo_Out8(InstancePtr->BaseAddress + XIIC_DTR_REG_OFFSET, LocalAddr);  \
}

/******************************************************************************
*
* This macro disables the specified interrupts in the Interrupt enable
* register.  It is non-destructive in that the register is read and only the
* interrupts specified is changed.
*
* @param	BaseAddress is the base address of the IIC device.
* @param	InterruptMask contains the interrupts to be disabled
*
* @return	None.
*
* @note		Signature:
*		void XIic_mDisableIntr(u32 BaseAddress, u32 InterruptMask);
*
******************************************************************************/
#define XIic_mDisableIntr(BaseAddress, InterruptMask)           \
    XIIC_WRITE_IIER((BaseAddress),                        \
        XIIC_READ_IIER(BaseAddress) & ~(InterruptMask))

/******************************************************************************
*
* This macro enables the specified interrupts in the Interrupt enable
* register.  It is non-destructive in that the register is read and only the
* interrupts specified is changed.
*
* @param	BaseAddress is the base address of the IIC device.
* @param	InterruptMask contains the interrupts to be disabled
*
* @return	None.
*
* @note		Signature:
*		void XIic_mEnableIntr(u32 BaseAddress, u32 InterruptMask);
*
******************************************************************************/
#define XIic_mEnableIntr(BaseAddress, InterruptMask)           \
    XIIC_WRITE_IIER((BaseAddress),                       \
        XIIC_READ_IIER(BaseAddress) | (InterruptMask))

/******************************************************************************
*
* This macro clears the specified interrupt in the Interrupt status
* register.  It is non-destructive in that the register is read and only the
* interrupt specified is cleared.  Clearing an interrupt acknowledges it.
*
* @param	BaseAddress is the base address of the IIC device.
* @param	InterruptMask contains the interrupts to be disabled
*
* @return	None.
*
* @note		Signature:
*		void XIic_mClearIntr(u32 BaseAddress, u32 InterruptMask);
*
******************************************************************************/
#define XIic_mClearIntr(BaseAddress, InterruptMask)                 \
    XIIC_WRITE_IISR((BaseAddress),                            \
        XIIC_READ_IISR(BaseAddress) & (InterruptMask))

/******************************************************************************
*
* This macro clears and enables the specified interrupt in the Interrupt
* status and enable registers.  It is non-destructive in that the registers are
* read and only the interrupt specified is modified.
* Clearing an interrupt acknowledges it.
*
* @param	BaseAddress is the base address of the IIC device.
* @param	InterruptMask contains the interrupts to be cleared and enabled
*
* @return	None.
*
* @note		Signature:
*		void XIic_mClearEnableIntr(u32 BaseAddress, u32 InterruptMask);
*
******************************************************************************/
#define XIic_mClearEnableIntr(BaseAddress, InterruptMask)          \
{                                                                       \
    XIIC_WRITE_IISR(BaseAddress,                              \
        (XIIC_READ_IISR(BaseAddress) & (InterruptMask)));     \
                                                                        \
    XIIC_WRITE_IIER(BaseAddress,                              \
        (XIIC_READ_IIER(BaseAddress) | (InterruptMask)));     \
}

/******************************************************************************
*
* This macro flushes the receive FIFO such that all bytes contained within it
* are discarded.
*
* @param	InstancePtr is a pointer to the IIC instance containing the FIFO
*		to be flushed.
*
* @return	None.
*
* @note		Signature:
*		void XIic_mFlushRxFifo(XIic *InstancePtr);
*
******************************************************************************/
#define XIic_mFlushRxFifo(InstancePtr)                                     \
{                                                                           \
    int LoopCnt;                                                            \
    u8 Temp;                                                            \
    u8 BytesToRead = XIo_In8(InstancePtr->BaseAddress +              \
                                 XIIC_RFO_REG_OFFSET) + 1;                  \
    for(LoopCnt = 0; LoopCnt < BytesToRead; LoopCnt++)                      \
    {                                                                       \
        Temp = XIo_In8(InstancePtr->BaseAddress + XIIC_DRR_REG_OFFSET);  \
    }                                                                       \
}

/******************************************************************************
*
* This macro flushes the transmit FIFO such that all bytes contained within it
* are discarded.
*
* @param	InstancePtr is a pointer to the IIC instance containing the FIFO
*		to be flushed.
*
* @return	None.
*
* @note		Signature:
*		void XIic_mFlushTxFifo(XIic *InstancePtr);
*
******************************************************************************/
#define XIic_mFlushTxFifo(InstancePtr);                                    \
{                                                                           \
    u8 CntlReg = XIo_In8(InstancePtr->BaseAddress +                  \
                             XIIC_CR_REG_OFFSET);                           \
    XIo_Out8(InstancePtr->BaseAddress + XIIC_CR_REG_OFFSET,              \
             CntlReg | XIIC_CR_TX_FIFO_RESET_MASK);                         \
    XIo_Out8(InstancePtr->BaseAddress + XIIC_CR_REG_OFFSET, CntlReg);    \
}

/******************************************************************************
*
* This macro reads the next available received byte from the receive FIFO
* and updates all the data structures to reflect it.
*
* @param	InstancePtr is a pointer to the IIC instance to be operated on.
*
* @return	None.
*
* @note		Signature:
*		void XIic_mReadRecvByte(XIic *InstancePtr);
*
******************************************************************************/
#define XIic_mReadRecvByte(InstancePtr)                                    \
{                                                                           \
    *InstancePtr->RecvBufferPtr++ =                                         \
        XIo_In8(InstancePtr->BaseAddress + XIIC_DRR_REG_OFFSET);         \
    InstancePtr->RecvByteCount--;                                           \
    InstancePtr->Stats.RecvBytes++;                                         \
}

/******************************************************************************
*
* This macro writes the next byte to be sent to the transmit FIFO
* and updates all the data structures to reflect it.
*
* @param	InstancePtr is a pointer to the IIC instance to be operated on.
*
* @return	None.
*
* @note		Signature:
*		void XIic_mWriteSendByte(XIic *InstancePtr);
*
******************************************************************************/
#define XIic_mWriteSendByte(InstancePtr)                                   \
{                                                                           \
    XIo_Out8(InstancePtr->BaseAddress + XIIC_DTR_REG_OFFSET,             \
        *InstancePtr->SendBufferPtr++);                                     \
    InstancePtr->SendByteCount--;                                           \
    InstancePtr->Stats.SendBytes++;                                         \
}

/******************************************************************************
*
* This macro sets up the control register for a master receive operation.
* A write is necessary if a 10 bit operation is being performed.
*
* @param	InstancePtr is a pointer to the IIC instance to be operated on.
* @param	ControlRegister contains the contents of the IIC device control
*		register
* @param	ByteCount contains the number of bytes to be received for the
*		master receive operation
*
* @return	None.
*
* @note		Signature:
*		void XIic_mSetControlRegister(XIic *InstancePtr,
*						u8 ControlRegister,
*						int ByteCount);
*
******************************************************************************/
#define XIic_mSetControlRegister(InstancePtr, ControlRegister, ByteCount)  \
{                                                                           \
    (ControlRegister) &= ~(XIIC_CR_NO_ACK_MASK | XIIC_CR_DIR_IS_TX_MASK);   \
    if (InstancePtr->Options & XII_SEND_10_BIT_OPTION)                      \
    {                                                                       \
        (ControlRegister) |= XIIC_CR_DIR_IS_TX_MASK;                        \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        if ((ByteCount) == 1)                                               \
        {                                                                   \
            (ControlRegister) |= XIIC_CR_NO_ACK_MASK;                       \
        }                                                                   \
    }                                                                       \
}

/******************************************************************************
*
* This macro enters a critical region by disabling the global interrupt bit
* in the Global interrupt register.
*
* @param	BaseAddress is the base address of the IIC device.
*
* @return	None.
*
* @note		Signature:
*		void XIic_mEnterCriticalRegion(u32 BaseAddress)
*
******************************************************************************/
#define XIic_mEnterCriticalRegion(BaseAddress)  \
	XIIC_GINTR_DISABLE(BaseAddress)

/******************************************************************************
*
* This macro exits a critical region by enabling the global interrupt bit
* in the Global interrupt register.
*
* @param	BaseAddress is the base address of the IIC device.
*
* @return	None.
*
* @note		Signature:
*		void XIic_mExitCriticalRegion(u32 BaseAddress)
*
******************************************************************************/
#define XIic_mExitCriticalRegion(BaseAddress)  \
	XIIC_GINTR_ENABLE(BaseAddress)

/******************************************************************************
*
* This macro clears the statistics of an instance such that it can be common
* such that some parts of the driver may be optional.
*
* @param	InstancePtr is a pointer to the IIC instance to be operated on.
*
* @return	None.
*
* @note		Signature:
*		void XIIC_CLEAR_STATS(XIic *InstancePtr)
*
******************************************************************************/
#define XIIC_CLEAR_STATS(InstancePtr)                                   \
{                                                                       \
    u8 NumBytes;                                                    \
    u8 *DestPtr;                                                    \
                                                                        \
    DestPtr = (u8 *)&InstancePtr->Stats;                            \
    for (NumBytes = 0; NumBytes < sizeof(XIicStats); NumBytes++)        \
    {                                                                   \
        *DestPtr++ = 0;                                                 \
    }                                                                   \
}

/************************** Function Prototypes ******************************/

extern XIic_Config XIic_ConfigTable[];

/* The following variables are shared across files of the driver and
 * are function pointers that are necessary to break dependencies allowing
 * optional parts of the driver to be used without condition compilation
 */
extern void (*XIic_AddrAsSlaveFuncPtr) (XIic * InstancePtr);
extern void (*XIic_NotAddrAsSlaveFuncPtr) (XIic * InstancePtr);
extern void (*XIic_RecvSlaveFuncPtr) (XIic * InstancePtr);
extern void (*XIic_SendSlaveFuncPtr) (XIic * InstancePtr);
extern void (*XIic_RecvMasterFuncPtr) (XIic * InstancePtr);
extern void (*XIic_SendMasterFuncPtr) (XIic * InstancePtr);
extern void (*XIic_ArbLostFuncPtr) (XIic * InstancePtr);
extern void (*XIic_BusNotBusyFuncPtr) (XIic * InstancePtr);

void XIic_TransmitFifoFill(XIic * InstancePtr, int Role);

#ifdef __cplusplus
}
#endif

#endif /* end of protection macro */
