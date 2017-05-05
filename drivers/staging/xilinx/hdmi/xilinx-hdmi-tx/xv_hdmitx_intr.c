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
* @file xv_hdmitx_intr.c
*
* This file contains interrupt related functions for Xilinx HDMI TX core.
* Please see xv_hdmitx.h for more details of the driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00         10/07/15 Initial release.
* 1.1   YH     18/08/16 squash unused variable compiler warning
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xv_hdmitx.h"

/************************** Constant Definitions *****************************/


/***************** Macros (Inline Functions) Definitions *********************/


/**************************** Type Definitions *******************************/


/************************** Function Prototypes ******************************/

void HdmiTx_PioIntrHandler(XV_HdmiTx *InstancePtr);
void HdmiTx_DdcIntrHandler(XV_HdmiTx *InstancePtr);

/************************** Variable Definitions *****************************/


/************************** Function Definitions *****************************/

/*****************************************************************************/
/**
*
* This function is the interrupt handler for the HDMI TX driver.
*
* This handler reads the pending interrupt from PIO and DDC peripheral,
* determines the source of the interrupts, clears the interrupts and calls
* callbacks accordingly.
*
* The application is responsible for connecting this function to the interrupt
* system. Application beyond this driver is also responsible for providing
* callbacks to handle interrupts and installing the callbacks using
* XV_HdmiTx_SetCallback() during initialization phase. An example delivered
* with this driver demonstrates how this could be done.
*
* @param    InstancePtr is a pointer to the XV_HdmiTx instance that just
*       interrupted.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
void XV_HdmiTx_IntrHandler(void *InstancePtr)
{
    u32 Data;
    XV_HdmiTx *HdmiTxPtr = (XV_HdmiTx *)InstancePtr;

    /* Verify arguments */
    Xil_AssertVoid(HdmiTxPtr != NULL);
    Xil_AssertVoid(HdmiTxPtr->IsReady == XIL_COMPONENT_IS_READY);

    /* PIO */
    Data = XV_HdmiTx_ReadReg(HdmiTxPtr->Config.BaseAddress,
                            (XV_HDMITX_PIO_STA_OFFSET)) &
                            (XV_HDMITX_PIO_STA_IRQ_MASK);

    /* Check for IRQ flag set */
    if (Data) {
        /* Jump to PIO handler */
        HdmiTx_PioIntrHandler(HdmiTxPtr);
    }

    /* DDC */
    Data = XV_HdmiTx_ReadReg(HdmiTxPtr->Config.BaseAddress,
                            (XV_HDMITX_DDC_STA_OFFSET)) &
                            (XV_HDMITX_DDC_STA_IRQ_MASK);

    /* Check for IRQ flag set */
    if (Data) {
        /* Jump to DDC handler */
        HdmiTx_DdcIntrHandler(HdmiTxPtr);
    }
}

/*****************************************************************************/
/**
*
* This function installs an asynchronous callback function for the given
* HandlerType:
*
* <pre>
* HandlerType              Callback Function Type
* -----------------------  --------------------------------------------------
* (XV_HDMITX_HANDLER_HPD)   HpdCallback
* (XV_HDMITX_HANDLER_VS)    VsCallback
* </pre>
*
* @param    InstancePtr is a pointer to the HDMI TX core instance.
* @param    HandlerType specifies the type of handler.
* @param    CallbackFunc is the address of the callback function.
* @param    CallbackRef is a user data item that will be passed to the
*       callback function when it is invoked.
*
* @return
*       - XST_SUCCESS if callback function installed successfully.
*       - XST_INVALID_PARAM when HandlerType is invalid.
*
* @note     Invoking this function for a handler that already has been
*       installed replaces it with the new handler.
*
******************************************************************************/
int XV_HdmiTx_SetCallback(XV_HdmiTx *InstancePtr,
                        u32 HandlerType,
                        void *CallbackFunc,
                        void *CallbackRef)
{
    u32 Status;

    /* Verify arguments. */
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(HandlerType >= (XV_HDMITX_HANDLER_CONNECT));
    Xil_AssertNonvoid(CallbackFunc != NULL);
    Xil_AssertNonvoid(CallbackRef != NULL);

    /* Check for handler type */
    switch (HandlerType) {
        case (XV_HDMITX_HANDLER_CONNECT):
            InstancePtr->ConnectCallback = (XV_HdmiTx_Callback)CallbackFunc;
            InstancePtr->ConnectRef = CallbackRef;
            InstancePtr->IsConnectCallbackSet = (TRUE);
            Status = (XST_SUCCESS);
            break;

        case (XV_HDMITX_HANDLER_TOGGLE):
            InstancePtr->ToggleCallback = (XV_HdmiTx_Callback)CallbackFunc;
            InstancePtr->ToggleRef = CallbackRef;
            InstancePtr->IsToggleCallbackSet = (TRUE);
            Status = (XST_SUCCESS);
            break;

        case (XV_HDMITX_HANDLER_VS):
            InstancePtr->VsCallback = (XV_HdmiTx_Callback)CallbackFunc;
            InstancePtr->VsRef = CallbackRef;
            InstancePtr->IsVsCallbackSet = (TRUE);
            Status = (XST_SUCCESS);
            break;

        // Stream down
        case (XV_HDMITX_HANDLER_STREAM_DOWN):
            InstancePtr->StreamDownCallback = (XV_HdmiTx_Callback)CallbackFunc;
            InstancePtr->StreamDownRef = CallbackRef;
            InstancePtr->IsStreamDownCallbackSet = (TRUE);
            Status = (XST_SUCCESS);
            break;

        // Stream up
        case (XV_HDMITX_HANDLER_STREAM_UP):
            InstancePtr->StreamUpCallback = (XV_HdmiTx_Callback)CallbackFunc;
            InstancePtr->StreamUpRef = CallbackRef;
            InstancePtr->IsStreamUpCallbackSet = (TRUE);
            Status = (XST_SUCCESS);
            break;

        default:
            Status = (XST_INVALID_PARAM);
            break;
    }

    return Status;
}

/*****************************************************************************/
/**
*
* This function is the HDMI TX PIO peripheral interrupt handler.
*
* This handler reads corresponding event interrupt from the PIO_IN_EVT
* register. It determines the source of the interrupts and calls according
* callbacks.
*
* @param    InstancePtr is a pointer to the HDMI TX core instance.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
void HdmiTx_PioIntrHandler(XV_HdmiTx *InstancePtr)
{
    u32 Event;
    u32 Data;

    /* Read PIO IN Event register.*/
    Event = XV_HdmiTx_ReadReg(InstancePtr->Config.BaseAddress,
                            (XV_HDMITX_PIO_IN_EVT_OFFSET));

    /* Clear event flags */
    XV_HdmiTx_WriteReg(InstancePtr->Config.BaseAddress,
                    (XV_HDMITX_PIO_IN_EVT_OFFSET),
                    (Event));

    /* Read data */
    Data = XV_HdmiTx_ReadReg(InstancePtr->Config.BaseAddress,
                            (XV_HDMITX_PIO_IN_OFFSET));

    /* HPD event has occurred */
    if ((Event) & (XV_HDMITX_PIO_IN_HPD_TOGGLE_MASK)) {

        // Check if user callback has been registered
        if (InstancePtr->IsToggleCallbackSet) {
            InstancePtr->ToggleCallback(InstancePtr->ToggleRef);
        }
    }

    /* HPD event has occurred */
    if ((Event) & (XV_HDMITX_PIO_IN_HPD_MASK)) {

        // Check the HPD status
        if ((Data) & (XV_HDMITX_PIO_IN_HPD_MASK))
            InstancePtr->Stream.IsConnected = (TRUE);   // Set connected flag
        else
            InstancePtr->Stream.IsConnected = (FALSE);  // Clear connected flag

        // Check if user callback has been registered
        if (InstancePtr->IsConnectCallbackSet) {
            InstancePtr->ConnectCallback(InstancePtr->ConnectRef);
        }
    }

    /* Vsync event has occurred */
    if ((Event) & (XV_HDMITX_PIO_IN_VS_MASK)) {

        // Check if user callback has been registered
        if (InstancePtr->IsVsCallbackSet) {
            InstancePtr->VsCallback(InstancePtr->VsRef);
        }
    }

    /* Link ready event has occurred */
    if ((Event) & (XV_HDMITX_PIO_IN_LNK_RDY_MASK)) {

        // Check the link status
        if ((Data) & (XV_HDMITX_PIO_IN_LNK_RDY_MASK)) {
            // Set stream status to up
            InstancePtr->Stream.State = XV_HDMITX_STATE_STREAM_UP;

            /* Enable the AUX peripheral */
            XV_HdmiTx_AuxEnable(InstancePtr);

            /* Enable the AUX peripheral interrupt */
            XV_HdmiTx_AuxIntrEnable(InstancePtr);

            /* Enable audio */
            //XV_HdmiTx_AudioEnable(InstancePtr);

            // Check if user callback has been registered
            if (InstancePtr->IsStreamUpCallbackSet) {
                InstancePtr->StreamUpCallback(InstancePtr->StreamUpRef);
            }
        }

        // Link down
        else {
            // Set stream status to down
            InstancePtr->Stream.State = XV_HDMITX_STATE_STREAM_DOWN;

            /* Disable Audio */
            XV_HdmiTx_AudioDisable(InstancePtr);

            /* Disable AUX */
            XV_HdmiTx_AuxDisable(InstancePtr);

            // Check if user callback has been registered
            if (InstancePtr->IsStreamDownCallbackSet) {
                InstancePtr->StreamDownCallback(InstancePtr->StreamDownRef);
            }
        }
    }
}

/*****************************************************************************/
/**
*
* This function is the HDMI TX DDC peripheral interrupt handler.
*
* This handler reads DDC Status register and determines the timeout. It also
* determines the state and based on that performs required operation.
*
*
* @param    InstancePtr is a pointer to the HDMI TX core instance.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
void HdmiTx_DdcIntrHandler(XV_HdmiTx *InstancePtr)
{
    u32 Data;

    /* Read DDC Status register */
    Data = XV_HdmiTx_ReadReg(InstancePtr->Config.BaseAddress,
                            (XV_HDMITX_DDC_STA_OFFSET));
    Data = Data; //squash unused variable compiler warning
}
