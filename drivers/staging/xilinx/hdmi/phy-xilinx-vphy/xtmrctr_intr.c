/******************************************************************************
*
 *
 * Copyright (C) 2015, 2016, 2017 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xtmrctr_intr.c
* @addtogroup tmrctr_v4_0
* @{
*
* Contains interrupt-related functions for the XTmrCtr component.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00b jhl  02/06/02 First release
* 1.10b mta  03/21/07 Updated to new coding style
* 2.00a ktn  10/30/09 Updated to use HAL API's. _m is removed from all the macro
*		      definitions.
* 2.03a rvo  11/30/10 Added check to see if interrupt is enabled before further
*		      processing for CR 584557.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xtmrctr.h"
#include "xil_printf.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


/************************** Variable Definitions *****************************/


/*****************************************************************************/
/**
*
* Sets the timer callback function, which the driver calls when the specified
* timer times out.
*
* @param	InstancePtr is a pointer to the XTmrCtr instance .
* @param	CallBackRef is the upper layer callback reference passed back
*		when the callback function is invoked.
* @param	FuncPtr is the pointer to the callback function.
*
* @return	None.
*
* @note
*
* The handler is called within interrupt context so the function that is
* called should either be short or pass the more extensive processing off
* to another task to allow the interrupt to return and normal processing
* to continue.
*
******************************************************************************/
void XTmrCtr_SetHandler(XTmrCtr * InstancePtr, XTmrCtr_Handler FuncPtr,
			void *CallBackRef)
{
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(FuncPtr != NULL);
	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

	InstancePtr->Handler = FuncPtr;
	InstancePtr->CallBackRef = CallBackRef;
}

/*****************************************************************************/
/**
*
* Interrupt Service Routine (ISR) for the driver.  This function only performs
* processing for the device and does not save and restore the interrupt context.
*
* @param	InstancePtr contains a pointer to the timer/counter instance for
*		the interrupt.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XTmrCtr_InterruptHandler(void *InstancePtr)
{
	XTmrCtr *TmrCtrPtr = NULL;
	u8 TmrCtrNumber;
	u32 ControlStatusReg;

	/*
	 * Verify that each of the inputs are valid.
	 */
	Xil_AssertVoid(InstancePtr != NULL);

	/*
	 * Convert the non-typed pointer to an timer/counter instance pointer
	 * such that there is access to the timer/counter
	 */
	TmrCtrPtr = (XTmrCtr *) InstancePtr;

	/*
	 * Loop thru each timer counter in the device and call the callback
	 * function for each timer which has caused an interrupt
	 */
	for (TmrCtrNumber = 0;
		TmrCtrNumber < XTC_DEVICE_TIMER_COUNT; TmrCtrNumber++) {

		ControlStatusReg = XTmrCtr_ReadReg(TmrCtrPtr->BaseAddress,
						   TmrCtrNumber,
						   XTC_TCSR_OFFSET);
		/*
		 * Linux - 	Do not check if interrupt is enabled as top half will 
					mask the interrupt
		 */
//		if (ControlStatusReg & XTC_CSR_ENABLE_INT_MASK) {

			/*
			 * Check if timer expired and interrupt occured
			 */
			if (ControlStatusReg & XTC_CSR_INT_OCCURED_MASK) {
				/*
				 * Increment statistics for the number of
				 * interrupts and call the callback to handle
				 * any application specific processing
				 */
				TmrCtrPtr->Stats.Interrupts++;
				TmrCtrPtr->Handler(TmrCtrPtr->CallBackRef,
						   TmrCtrNumber);
						   
				/*
				 * Read the new Control/Status Register content.
				 */
				ControlStatusReg =
					XTmrCtr_ReadReg(TmrCtrPtr->BaseAddress,
								TmrCtrNumber,
								XTC_TCSR_OFFSET);
				/*
				 * If in compare mode and a single shot rather
				 * than auto reload mode then disable the timer
				 * and reset it such so that the interrupt can
				 * be acknowledged, this should be only temporary
				 * till the hardware is fixed
				 */
				if (((ControlStatusReg &
					XTC_CSR_AUTO_RELOAD_MASK) == 0) &&
					((ControlStatusReg &
					  XTC_CSR_CAPTURE_MODE_MASK)== 0)) {
						/*
						 * Disable the timer counter and
						 * reset it such that the timer
						 * counter is loaded with the
						 * reset value allowing the
						 * interrupt to be acknowledged
						 */
						ControlStatusReg &=
							~XTC_CSR_ENABLE_TMR_MASK;

						XTmrCtr_WriteReg(
							TmrCtrPtr->BaseAddress,
							TmrCtrNumber,
							XTC_TCSR_OFFSET,
							ControlStatusReg |
							XTC_CSR_LOAD_MASK);

						/*
						 * Clear the reset condition,
						 * the reset bit must be
						 * manually cleared by a 2nd write
						 * to the register
						 */
						XTmrCtr_WriteReg(
							TmrCtrPtr->BaseAddress,
							TmrCtrNumber,
							XTC_TCSR_OFFSET,
							ControlStatusReg);
				}

				/*
				 * Acknowledge the interrupt by clearing the
				 * interrupt bit in the timer control status
				 * register, this is done after calling the
				 * handler so the application could call
				 * IsExpired, the interrupt is cleared by
				 * writing a 1 to the interrupt bit of the
				 * register without changing any of the other
				 * bits
				 */
				XTmrCtr_WriteReg(TmrCtrPtr->BaseAddress,
						 TmrCtrNumber,
						 XTC_TCSR_OFFSET,
						 ControlStatusReg |
						 XTC_CSR_INT_OCCURED_MASK);
			}
//		}
	} //for
}
/** @} */
