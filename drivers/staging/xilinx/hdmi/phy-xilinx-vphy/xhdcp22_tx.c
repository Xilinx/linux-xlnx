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
* @file xhdcp22_tx.c
* @addtogroup hdcp22_tx_v2_0
* @{
* @details
*
* This file contains the main implementation of the Xilinx HDCP 2.2 Transmitter
* device driver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- -------------------------------------------------------
* 1.00  JO     06/24/15 Initial release.
* 1.01  MH     01/15/16 Replaced function XHdcp22Tx_SetDdcHandles with
*                       XHdcp22Tx_SetCallback. Removed test directives.
* 1.02  MG     02/25/16 Added authenticated callback and GetVersion.
* 1.03  MG     02/29/16 Added XHdcp22Cipher_Disable in function XHdcp22Tx_Reset.
* 1.04  MH     03/14/16 Updated StateA5 to check re-authentication request
*                       before enabling the cipher.
* 2.00  MH     06/28/16 Updated for repeater downstream support.
* 2.01  MH     02/13/17 1. Fixed function XHdcp22Tx_IsInProgress to correctly
*                          return TRUE while state machine is executing.
*                       2. Fixed checking of seq_num_V for topology propagation.
*                       3. Updated state A0 to set 100ms timer before sending
*                          AKE_Init message to ensure encryption is disabled.
*                       4. Fixed function XHdcp22Tx_UpdatePairingInfo to check
*                          for empty slots before overriding entry.
*                       5. Fixed problem with pairing table update that was
*                          causing corrupted entries.
*                       6. Update to check return status of DDC read/write.
*                       7. Fixed compiler warnings.
* 2.20  MH     04/12/17 Added function XHdcp22Tx_IsDwnstrmCapable.
* 2.30  MH     07/06/17 1. Updated for 64 bit ARM support.
*                       2. Added HDCP2Capable check for re-authentication
*                       3. Update cipher enablement
*                       4. Fix in XHdcp22Tx_WaitForReceiver to poll RxStatus
*                          based on fixed interval.
*                       5. Fix in XHdcp22Tx_WaitForReceiver to wait for READY
*                          and non-zero Message_Size before reading message
*                          buffer.
*                       6. Check return status of DDC write/read when polling
*                          RxStatus register.
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xhdcp22_tx.h"
#include "xhdcp22_tx_i.h"
#include "xil_printf.h"

/************************** Constant Definitions *****************************/

/** RxStatus value used to force re-authentication */
#define XHDCP22_TX_INVALID_RXSTATUS 0xFFFF

/***************** Macros (Inline Functions) Definitions *********************/

/** Case replacement to copy a case Id to a string with a pre-lead */
#define XHDCP22_TX_CASE_TO_STR_PRE(pre, arg) \
case pre ## arg : strcpy(str, #arg); break;

/**************************** Type Definitions *******************************/
/**
* Pointer to the state handling functions
*
* @param  InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return The next state that should be executed.
*
* @note   None.
*
*/
typedef XHdcp22_Tx_StateType XHdcp22Tx_StateFuncType(XHdcp22_Tx *InstancePtr);

/**
* Pointer to the transition functions going from one state to the next.
*
* @param  InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return None.
*
* @note   None.
*
*/
typedef void XHdcp22_Tx_TransitionFuncType(XHdcp22_Tx *InstancePtr);

/************************** Function Prototypes ******************************/

/* Initialization of peripherals */
static int XHdcp22Tx_InitializeTimer(XHdcp22_Tx *InstancePtr);
static int XHdcp22Tx_InitializeCipher(XHdcp22_Tx *InstancePtr);
static int XHdcp22Tx_InitializeRng(XHdcp22_Tx *InstancePtr);
static int XHdcp22Tx_ComputeBaseAddress(UINTPTR BaseAddress, UINTPTR SubcoreOffset, UINTPTR *SubcoreAddressPtr);

/* Stubs for callbacks */
static int XHdcp22Tx_StubDdc(u8 DeviceAddress, u16 ByteCount, u8* BufferPtr,
                             u8 Stop, void *RefPtr);
static void XHdcp22Tx_StubCallback(void* RefPtr);

/* state handling  functions */
static XHdcp22_Tx_StateType XHdcp22Tx_StateH0(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateH1(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA0(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA1(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType Xhdcp22Tx_StateA1_1(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType Xhdcp22Tx_StateA1_Nsk0(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType Xhdcp22Tx_StateA1_Nsk1(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType Xhdcp22Tx_StateA1_Sk0(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA2(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA2_1(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA3(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA4(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA5(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA6_A7_A8(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA6(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA7(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA8(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA9(XHdcp22_Tx *InstancePtr);
static XHdcp22_Tx_StateType XHdcp22Tx_StateA9_1(XHdcp22_Tx *InstancePtr);

/* State transition handling functions are used to do initial code before
* entering a new state. This is used in cases that a state is executed
* repeatedly due to the polling mechanism. A state transisition ensures in
* this case that the code is executed only once during authentication.
*/
static void XHdcp22Tx_A1A0(XHdcp22_Tx *InstancePtr);
static void XHdcp22Tx_A1A2(XHdcp22_Tx *InstancePtr);
static void XHdcp22Tx_A2A0(XHdcp22_Tx *InstancePtr);
static void XHdcp22Tx_A3A0(XHdcp22_Tx *InstancePtr);
static void XHdcp22Tx_A3A4(XHdcp22_Tx *InstancePtr);
static void XHdcp22Tx_A4A5(XHdcp22_Tx *InstancePtr);
static void XHdcp22Tx_A6A7A0(XHdcp22_Tx *InstancePtr);
static void XHdcp22Tx_A9A0(XHdcp22_Tx *InstancePtr);

/* Protocol specific functions */
static int XHdcp22Tx_WriteAKEInit(XHdcp22_Tx *InstancePtr);
static int XHdcp22Tx_WriteAKENoStoredKm(XHdcp22_Tx *InstancePtr,
                    const XHdcp22_Tx_PairingInfo *PairingInfoPtr,
                    const XHdcp22_Tx_CertRx *CertificatePtr);
static int XHdcp22Tx_WriteAKEStoredKm(XHdcp22_Tx *InstancePtr,
                    const XHdcp22_Tx_PairingInfo *PairingInfoPtr);
static int XHdcp22Tx_WriteLcInit(XHdcp22_Tx *InstancePtr, const u8 *Rn);
static int XHdcp22Tx_WriteSkeSendEks(XHdcp22_Tx *InstancePtr,
                    const u8 *EdkeyKsPtr, const u8 *RivPtr);
static int XHdcp22Tx_WriteRepeaterAuth_Send_Ack(XHdcp22_Tx *InstancePtr, const u8 *V);
static int XHdcp22Tx_WriteRepeaterAuth_Stream_Manage(XHdcp22_Tx *InstancePtr);
static int XHdcp22Tx_ReceiveMsg(XHdcp22_Tx *InstancePtr, u8 MessageId,
                    u32 MessageSize);

/* Generators and functions that return a generated value or a testvector */
static void XHdcp22Tx_GenerateRtx(XHdcp22_Tx *InstancePtr, u8* RtxPtr);
static void XHdcp22Tx_GenerateKm(XHdcp22_Tx *InstancePtr, u8* KmPtr);
static void XHdcp22Tx_GenerateKmMaskingSeed(XHdcp22_Tx *InstancePtr, u8* SeedPtr);
static void XHdcp22Tx_GenerateRn(XHdcp22_Tx *InstancePtr, u8* RnPtr);
static void XHdcp22Tx_GenerateRiv(XHdcp22_Tx *InstancePtr, u8* RivPtr);
static void XHdcp22Tx_GenerateKs(XHdcp22_Tx *InstancePtr, u8* KsPtr);
static const u8* XHdcp22Tx_GetKPubDpc(XHdcp22_Tx *InstancePtr);

/* Cryptographic functions */
static XHdcp22_Tx_PairingInfo *XHdcp22Tx_GetPairingInfo(XHdcp22_Tx *InstancePtr,
                                                        const u8 *ReceiverId);
static void XHdcp22Tx_InvalidatePairingInfo(XHdcp22_Tx *InstancePtr,
                                             const u8* ReceiverId);
static XHdcp22_Tx_PairingInfo *XHdcp22Tx_UpdatePairingInfo(XHdcp22_Tx *InstancePtr,
                              const XHdcp22_Tx_PairingInfo *PairingInfo, u8 Ready);

/* Timer functions */
static void XHdcp22Tx_TimerHandler(void *CallbackRef, u8 TmrCntNumber);
static int XHdcp22Tx_StartTimer(XHdcp22_Tx *InstancePtr, u32 TimeOut_mSec,
                                u8 ReasonId);
static int XHdcp22Tx_WaitForReceiver(XHdcp22_Tx *InstancePtr, int ExpectedSize, u8 ReadyBit);
static u32 XHdcp22Tx_GetTimerCount(XHdcp22_Tx *InstancePtr);

/* RxStatus handling */
static void XHdcp22Tx_ReadRxStatus(XHdcp22_Tx *InstancePtr);

/* Status handling */
static void XHdcp22Tx_HandleAuthenticationFailed(XHdcp22_Tx *InstancePtr);
static void XHdcp22Tx_HandleReauthenticationRequest(XHdcp22_Tx * InstancePtr);

/* Function for setting fields inside the topology structure */
static u32 XHdcp22Tx_GetTopologyDepth(XHdcp22_Tx *InstancePtr);
static u32 XHdcp22Tx_GetTopologyDeviceCnt(XHdcp22_Tx *InstancePtr);
static u32 XHdcp22Tx_GetTopologyMaxDevsExceeded(XHdcp22_Tx *InstancePtr);
static u32 XHdcp22Tx_GetTopologyMaxCascadeExceeded(XHdcp22_Tx *InstancePtr);
static u32 XHdcp22Tx_GetTopologyHdcp20RepeaterDownstream(XHdcp22_Tx *InstancePtr);
static u32 XHdcp22Tx_GetTopologyHdcp1DeviceDownstream(XHdcp22_Tx *InstancePtr);

/************************** Variable Definitions *****************************/

/**
* This table contains the function pointers for all possible states.
* The order of elements must match the #XHdcp22_Tx_StateType enumerator definitions.
*/
XHdcp22Tx_StateFuncType* const XHdcp22_Tx_StateTable[XHDCP22_TX_NUM_STATES] =
{
	XHdcp22Tx_StateH0, XHdcp22Tx_StateH1, XHdcp22Tx_StateA0,
	XHdcp22Tx_StateA1, Xhdcp22Tx_StateA1_1,
	Xhdcp22Tx_StateA1_Nsk0, Xhdcp22Tx_StateA1_Nsk1, Xhdcp22Tx_StateA1_Sk0,
	XHdcp22Tx_StateA2, XHdcp22Tx_StateA2_1,
	XHdcp22Tx_StateA3, XHdcp22Tx_StateA4, XHdcp22Tx_StateA5,
	XHdcp22Tx_StateA6_A7_A8,
	XHdcp22Tx_StateA6, XHdcp22Tx_StateA7, XHdcp22Tx_StateA8, XHdcp22Tx_StateA9, XHdcp22Tx_StateA9_1
};

/**
* This table is a matrix that contains the function pointers for functions to execute on
* state transition.
* It is filled dynamically.
* A NULL value is used if no transition is required.
*/
static XHdcp22_Tx_TransitionFuncType * transition_table[XHDCP22_TX_NUM_STATES][XHDCP22_TX_NUM_STATES];

/* public transmitter DCP LLC key - n=384 bytes, e=1 byte */
static const u8 XHdcp22_Tx_KpubDcp[XHDCP22_TX_KPUB_DCP_LLC_N_SIZE + XHDCP22_TX_KPUB_DCP_LLC_E_SIZE] = {
	0xB0, 0xE9, 0xAA, 0x45, 0xF1, 0x29, 0xBA, 0x0A, 0x1C, 0xBE, 0x17, 0x57, 0x28, 0xEB, 0x2B, 0x4E,
	0x8F, 0xD0, 0xC0, 0x6A, 0xAD, 0x79, 0x98, 0x0F, 0x8D, 0x43, 0x8D, 0x47, 0x04, 0xB8, 0x2B, 0xF4,
	0x15, 0x21, 0x56, 0x19, 0x01, 0x40, 0x01, 0x3B, 0xD0, 0x91, 0x90, 0x62, 0x9E, 0x89, 0xC2, 0x27,
	0x8E, 0xCF, 0xB6, 0xDB, 0xCE, 0x3F, 0x72, 0x10, 0x50, 0x93, 0x8C, 0x23, 0x29, 0x83, 0x7B, 0x80,
	0x64, 0xA7, 0x59, 0xE8, 0x61, 0x67, 0x4C, 0xBC, 0xD8, 0x58, 0xB8, 0xF1, 0xD4, 0xF8, 0x2C, 0x37,
	0x98, 0x16, 0x26, 0x0E, 0x4E, 0xF9, 0x4E, 0xEE, 0x24, 0xDE, 0xCC, 0xD1, 0x4B, 0x4B, 0xC5, 0x06,
	0x7A, 0xFB, 0x49, 0x65, 0xE6, 0xC0, 0x00, 0x83, 0x48, 0x1E, 0x8E, 0x42, 0x2A, 0x53, 0xA0, 0xF5,
	0x37, 0x29, 0x2B, 0x5A, 0xF9, 0x73, 0xC5, 0x9A, 0xA1, 0xB5, 0xB5, 0x74, 0x7C, 0x06, 0xDC, 0x7B,
	0x7C, 0xDC, 0x6C, 0x6E, 0x82, 0x6B, 0x49, 0x88, 0xD4, 0x1B, 0x25, 0xE0, 0xEE, 0xD1, 0x79, 0xBD,
	0x39, 0x85, 0xFA, 0x4F, 0x25, 0xEC, 0x70, 0x19, 0x23, 0xC1, 0xB9, 0xA6, 0xD9, 0x7E, 0x3E, 0xDA,
	0x48, 0xA9, 0x58, 0xE3, 0x18, 0x14, 0x1E, 0x9F, 0x30, 0x7F, 0x4C, 0xA8, 0xAE, 0x53, 0x22, 0x66,
	0x2B, 0xBE, 0x24, 0xCB, 0x47, 0x66, 0xFC, 0x83, 0xCF, 0x5C, 0x2D, 0x1E, 0x3A, 0xAB, 0xAB, 0x06,
	0xBE, 0x05, 0xAA, 0x1A, 0x9B, 0x2D, 0xB7, 0xA6, 0x54, 0xF3, 0x63, 0x2B, 0x97, 0xBF, 0x93, 0xBE,
	0xC1, 0xAF, 0x21, 0x39, 0x49, 0x0C, 0xE9, 0x31, 0x90, 0xCC, 0xC2, 0xBB, 0x3C, 0x02, 0xC4, 0xE2,
	0xBD, 0xBD, 0x2F, 0x84, 0x63, 0x9B, 0xD2, 0xDD, 0x78, 0x3E, 0x90, 0xC6, 0xC5, 0xAC, 0x16, 0x77,
	0x2E, 0x69, 0x6C, 0x77, 0xFD, 0xED, 0x8A, 0x4D, 0x6A, 0x8C, 0xA3, 0xA9, 0x25, 0x6C, 0x21, 0xFD,
	0xB2, 0x94, 0x0C, 0x84, 0xAA, 0x07, 0x29, 0x26, 0x46, 0xF7, 0x9B, 0x3A, 0x19, 0x87, 0xE0, 0x9F,
	0xEB, 0x30, 0xA8, 0xF5, 0x64, 0xEB, 0x07, 0xF1, 0xE9, 0xDB, 0xF9, 0xAF, 0x2C, 0x8B, 0x69, 0x7E,
	0x2E, 0x67, 0x39, 0x3F, 0xF3, 0xA6, 0xE5, 0xCD, 0xDA, 0x24, 0x9B, 0xA2, 0x78, 0x72, 0xF0, 0xA2,
	0x27, 0xC3, 0xE0, 0x25, 0xB4, 0xA1, 0x04, 0x6A, 0x59, 0x80, 0x27, 0xB5, 0xDA, 0xB4, 0xB4, 0x53,
	0x97, 0x3B, 0x28, 0x99, 0xAC, 0xF4, 0x96, 0x27, 0x0F, 0x7F, 0x30, 0x0C, 0x4A, 0xAF, 0xCB, 0x9E,
	0xD8, 0x71, 0x28, 0x24, 0x3E, 0xBC, 0x35, 0x15, 0xBE, 0x13, 0xEB, 0xAF, 0x43, 0x01, 0xBD, 0x61,
	0x24, 0x54, 0x34, 0x9F, 0x73, 0x3E, 0xB5, 0x10, 0x9F, 0xC9, 0xFC, 0x80, 0xE8, 0x4D, 0xE3, 0x32,
	0x96, 0x8F, 0x88, 0x10, 0x23, 0x25, 0xF3, 0xD3, 0x3E, 0x6E, 0x6D, 0xBB, 0xDC, 0x29, 0x66, 0xEB,
	0x03
};


/**
* This array contains the capabilities of the HDCP22 TX core, and is transmitted during
* authentication as part of the AKE_Init message.
*/
static const u8 XHdcp22_Tx_TxCaps[] = { 0x02, 0x00, 0x00 };


/************************** Function Definitions *****************************/
/*****************************************************************************/
/**
*
* This function initializes the HDCP22 TX core. This function must be called
* prior to using the HDCP TX core. Initialization of the HDCP TX includes
* setting up the instance data and ensuring the hardware is in a quiescent
* state.
*
* @param InstancePtr is a pointer to the XHdcp22_Tx core instance.
* @param CfgPtr points to the configuration structure associated with
*        the HDCP TX core.
* @param EffectiveAddr is the base address of the device. If address
*        translation is being used, then this parameter must reflect the
*        virtual base address. Otherwise, the physical address should be
*        used.
*
* @return
*    - XST_SUCCESS Initialization was successful.
*    - XST_FAILURE Initialization of the internal timer failed or there was a
*               HDCP TX PIO ID mismatch.
* @note   None.
*
******************************************************************************/
int XHdcp22Tx_CfgInitialize(XHdcp22_Tx *InstancePtr, XHdcp22_Tx_Config *CfgPtr,
                            UINTPTR EffectiveAddr)
{
	int Result = XST_SUCCESS;

	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(CfgPtr != NULL);
	Xil_AssertNonvoid(EffectiveAddr != (UINTPTR)NULL);

	/* Setup the instance */
	(void)memset((void *)InstancePtr, 0, sizeof(XHdcp22_Tx));

	/* copy configuration settings */
	(void)memcpy((void *)&(InstancePtr->Config), (const void *)CfgPtr,
				 sizeof(XHdcp22_Tx_Config));

	InstancePtr->Config.BaseAddress = EffectiveAddr;

	/* Set all handlers to stub values, let user configure this data later */
	InstancePtr->DdcRead = XHdcp22Tx_StubDdc;
	InstancePtr->IsDdcReadSet = (FALSE);
	InstancePtr->DdcWrite = XHdcp22Tx_StubDdc;
	InstancePtr->IsDdcWriteSet = (FALSE);
	InstancePtr->AuthenticatedCallback = XHdcp22Tx_StubCallback;
	InstancePtr->IsAuthenticatedCallbackSet = (FALSE);
	InstancePtr->UnauthenticatedCallback = XHdcp22Tx_StubCallback;
	InstancePtr->IsUnauthenticatedCallbackSet = (FALSE);
	InstancePtr->DownstreamTopologyAvailableCallback = XHdcp22Tx_StubCallback;
	InstancePtr->IsDownstreamTopologyAvailableCallbackSet = (FALSE);

	InstancePtr->Info.Protocol = XHDCP22_TX_HDMI;

	/* Initialize global parameters */
	InstancePtr->Info.IsReceiverHDCP2Capable = (FALSE);

	/* Initialize statemachine, but do not run it */
	/* Dynamically setup the transition table */
	memset(transition_table, 0x00, sizeof(transition_table));
	transition_table[XHDCP22_TX_STATE_A1][XHDCP22_TX_STATE_A0] = XHdcp22Tx_A1A0;
	transition_table[XHDCP22_TX_STATE_A1_1][XHDCP22_TX_STATE_A0] = XHdcp22Tx_A1A0;
	transition_table[XHDCP22_TX_STATE_A1_NSK0][XHDCP22_TX_STATE_A0] = XHdcp22Tx_A1A0;
	transition_table[XHDCP22_TX_STATE_A1_NSK1][XHDCP22_TX_STATE_A0] = XHdcp22Tx_A1A0;
	transition_table[XHDCP22_TX_STATE_A1_SK0][XHDCP22_TX_STATE_A0] = XHdcp22Tx_A1A0;

	transition_table[XHDCP22_TX_STATE_A1_NSK1][XHDCP22_TX_STATE_A2] = XHdcp22Tx_A1A2;
	transition_table[XHDCP22_TX_STATE_A1_SK0][XHDCP22_TX_STATE_A2] = XHdcp22Tx_A1A2;

	transition_table[XHDCP22_TX_STATE_A2][XHDCP22_TX_STATE_A0] = XHdcp22Tx_A2A0;
	transition_table[XHDCP22_TX_STATE_A2_1][XHDCP22_TX_STATE_A0] = XHdcp22Tx_A2A0;
	transition_table[XHDCP22_TX_STATE_A3][XHDCP22_TX_STATE_A0] = XHdcp22Tx_A3A0;
	transition_table[XHDCP22_TX_STATE_A4][XHDCP22_TX_STATE_A5] = XHdcp22Tx_A4A5;
	transition_table[XHDCP22_TX_STATE_A3][XHDCP22_TX_STATE_A4] = XHdcp22Tx_A3A4;
	transition_table[XHDCP22_TX_STATE_A6_A7_A8][XHDCP22_TX_STATE_A0] = XHdcp22Tx_A6A7A0;
	transition_table[XHDCP22_TX_STATE_A9][XHDCP22_TX_STATE_A0] = XHdcp22Tx_A9A0;

	InstancePtr->Info.AuthenticationStatus = XHDCP22_TX_UNAUTHENTICATED;
	InstancePtr->Info.CurrentState = XHDCP22_TX_STATE_H0;
	InstancePtr->Info.PrvState = XHDCP22_TX_STATE_H0;
	InstancePtr->Info.IsEnabled = (FALSE);
	InstancePtr->Info.StateContext = NULL;
	InstancePtr->Info.MsgAvailable = (FALSE);
	InstancePtr->Info.PollingValue = XHDCP22_TX_DEFAULT_RX_STATUS_POLLVALUE;

	/* Topology info */
	InstancePtr->Info.IsTopologyAvailable = (FALSE);

	/* Timer configuration */
	InstancePtr->Timer.TimerExpired = (TRUE);
	InstancePtr->Timer.ReasonId = XHDCP22_TX_TS_UNDEFINED;
	InstancePtr->Timer.InitialTicks = 0;

	/* Receiver ID list */
	InstancePtr->Info.ReceivedFirstSeqNum_V = (FALSE);

	/* Revocation List */
	InstancePtr->Info.IsRevocationListValid = (FALSE);
	InstancePtr->RevocationList.NumDevices = 0;

	/* Content Stream Management */
	InstancePtr->Info.ContentStreamType = XHDCP22_STREAMTYPE_0; // Default
	InstancePtr->Info.IsContentStreamTypeSet = (TRUE);

	/* Clear pairing info */
	XHdcp22Tx_ClearPairingInfo(InstancePtr);

	/* Initialize hardware timer */
	Result = XHdcp22Tx_InitializeTimer(InstancePtr);
	if (Result != XST_SUCCESS) {
		return Result;
	}

	/* Initialize random number generator */
	Result = XHdcp22Tx_InitializeRng(InstancePtr);
	if (Result != XST_SUCCESS) {
		return Result;
	}

	/* Initialize cipher */
	Result = XHdcp22Tx_InitializeCipher(InstancePtr);
	if (Result != XST_SUCCESS) {
		return Result;
	}

	/* Indicate the instance is now ready to use, initialized without error */
	InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

	XHdcp22Tx_LogReset(InstancePtr, FALSE);

	return (XST_SUCCESS);
}

/*****************************************************************************/
/**
*
* This function is a called to initialize the hardware timer.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return
*    - XST_SUCCESS if authenticated is started successfully.
*    - XST_FAIL if the state machine is disabled.
*
* @note  None.
*
******************************************************************************/
static int XHdcp22Tx_InitializeTimer(XHdcp22_Tx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	UINTPTR SubcoreBaseAddr;

	int Result = XST_SUCCESS;

	XTmrCtr_Config *TimerConfigPtr;

	TimerConfigPtr = XTmrCtr_LookupConfig(InstancePtr->Config.TimerDeviceId);
	if(TimerConfigPtr == NULL) {
		return XST_FAILURE;
	}

	Result = XHdcp22Tx_ComputeBaseAddress(InstancePtr->Config.BaseAddress, TimerConfigPtr->BaseAddress, &SubcoreBaseAddr);
	XTmrCtr_CfgInitialize(&InstancePtr->Timer.TmrCtr, TimerConfigPtr, SubcoreBaseAddr);
	if (Result != XST_SUCCESS) {
		return Result;
	}

	XTmrCtr_SetOptions(&InstancePtr->Timer.TmrCtr, XHDCP22_TX_TIMER_CNTR_0,
	                   XTC_INT_MODE_OPTION | XTC_DOWN_COUNT_OPTION);
	XTmrCtr_SetOptions(&InstancePtr->Timer.TmrCtr, XHDCP22_TX_TIMER_CNTR_1,
	                   XTC_AUTO_RELOAD_OPTION);
	XTmrCtr_SetHandler(&InstancePtr->Timer.TmrCtr, XHdcp22Tx_TimerHandler,
	                   InstancePtr);
	return Result;
}

/*****************************************************************************/
/**
*
* This function is a called to initialize the cipher.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return
*    - XST_SUCCESS if authenticated is started successfully.
*    - XST_FAIL if the state machine is disabled.
*
* @note  None.
*
******************************************************************************/
static int XHdcp22Tx_InitializeCipher(XHdcp22_Tx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Result = XST_SUCCESS;

	XHdcp22_Cipher_Config *ConfigPtr = NULL;
	UINTPTR SubcoreBaseAddr;

	ConfigPtr = XHdcp22Cipher_LookupConfig(InstancePtr->Config.CipherId);
	if (ConfigPtr == NULL) {
		return XST_DEVICE_NOT_FOUND;
	}

	Result = XHdcp22Tx_ComputeBaseAddress(InstancePtr->Config.BaseAddress, ConfigPtr->BaseAddress, &SubcoreBaseAddr);
	Result |= XHdcp22Cipher_CfgInitialize(&InstancePtr->Cipher, ConfigPtr, SubcoreBaseAddr);
	if (Result != XST_SUCCESS) {
		return Result;
	}

	/* Set cipher to TX mode */
	XHdcp22Cipher_SetTxMode(&InstancePtr->Cipher);

	/* Disable encryption */
	XHdcp22Tx_DisableEncryption(InstancePtr);

	/* Disable cipher */
	XHdcp22Cipher_Disable(&InstancePtr->Cipher);

	return Result;
}

/*****************************************************************************/
/**
*
* This function is a called to initialize the hardware random generator.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return
*    - XST_SUCCESS if authenticated is started successfully.
*    - XST_FAIL if the state machine is disabled.
*
* @note  None.
*
******************************************************************************/
static int XHdcp22Tx_InitializeRng(XHdcp22_Tx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	int Result = XST_SUCCESS;
	UINTPTR SubcoreBaseAddr;

	XHdcp22_Rng_Config *ConfigPtr = NULL;

	ConfigPtr = XHdcp22Rng_LookupConfig(InstancePtr->Config.RngId);
	if (ConfigPtr == NULL) {
		return XST_DEVICE_NOT_FOUND;
	}

	Result = XHdcp22Tx_ComputeBaseAddress(InstancePtr->Config.BaseAddress, ConfigPtr->BaseAddress, &SubcoreBaseAddr);
	Result |= XHdcp22Rng_CfgInitialize(&InstancePtr->Rng, ConfigPtr, SubcoreBaseAddr);
	if (Result != XST_SUCCESS) {
		return Result;
	}
	XHdcp22Rng_Enable(&InstancePtr->Rng);
	return Result;
}

/*****************************************************************************/
/**
*
* This function is used to load the Lc128 value by copying the contents
* of the array referenced by Lc128Ptr into the cipher.
*
* @param	InstancePtr is a pointer to the XHdcp22_Tx core instance.
* @param	Lc128Ptr is a pointer to an array.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdcp22Tx_LoadLc128(XHdcp22_Tx *InstancePtr, const u8 *Lc128Ptr)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Lc128Ptr != NULL);

	XHdcp22Cipher_SetLc128(&InstancePtr->Cipher, Lc128Ptr,
	                       XHDCP22_TX_LC128_SIZE);
}

/*****************************************************************************/
/**
*
* This function is used to load the system renewability messages (SRMs)
* which carries the Receiver ID revocation list.
*
* @param	InstancePtr is a pointer to the XHdcp22_Tx core instance.
* @param	SrmPtr is a pointer to an array.
*
* @return
*    - XST_SUCCESS if loaded SRM successfully.
*    - XST_FAILURE if SRM signature verification failed.
*
* @note		None.
*
******************************************************************************/
int XHdcp22Tx_LoadRevocationTable(XHdcp22_Tx *InstancePtr, const u8 *SrmPtr)
{
	int Result;
	const u8* KPubDpcPtr = NULL;
	const u8* SrmBlockPtr = NULL;
	u8 SrmId;
	u16 SrmVersion;
	u8 SrmGenNr;
	u32 BlockSize;
	u32 LengthField;
	u16 NumDevices;
	const u8* ReceiverIdPtr;
	XHdcp22_Tx_RevocationList* RevocationListPtr = NULL;
	int i, j;

	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(SrmPtr != NULL);

	SrmBlockPtr = SrmPtr;

	/* byte 1 contains the SRM ID and HDCP2 Indicator field */
	SrmId = SrmBlockPtr[0];
	if (SrmId != 0x91) {
		/* Unknown SRM ID so ignore the SRM */
		return XST_FAILURE;
	}

	/* byte 2 is reserved */

	/* byte 3-4 contain the SRM Version.
	 * Value is in big endian format, Microblaze is little endian */
	SrmVersion  = SrmBlockPtr[2] << 8; // MSB
	SrmVersion |= SrmBlockPtr[3];      // LSB

	/* byte 5 contains the SRM Generation Number */
	SrmGenNr = SrmBlockPtr[4];

	/* byte 6,7,8 contain the length of the first-generation SRM in bytes.
	 * Value is in big endian format, Microblaze is little endian */
	LengthField  = SrmBlockPtr[5] << 16; // MSB
	LengthField |= SrmBlockPtr[6] << 8;
	LengthField |= SrmBlockPtr[7];       // LSB

	/* The size of the first-generation SRM block */
	BlockSize = LengthField + 5;

	KPubDpcPtr = XHdcp22Tx_GetKPubDpc(InstancePtr);

	/* Verify the first-generation SRM block */
	Result = XHdcp22Tx_VerifySRM(SrmBlockPtr,
	                             BlockSize,
	                             KPubDpcPtr, // N
	                             XHDCP22_TX_KPUB_DCP_LLC_N_SIZE,
	                             &KPubDpcPtr[XHDCP22_TX_KPUB_DCP_LLC_N_SIZE], // e
	                             XHDCP22_TX_KPUB_DCP_LLC_E_SIZE);
	if (Result != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* Update the SRM Block pointer */
	SrmBlockPtr += BlockSize;

	for (i = 1; i < SrmGenNr; i++) {
		/* byte 1-2 contain the length of the next-generation SRM in bytes.
		 * Value is in big endian format, Microblaze is little endian */
		LengthField  = SrmBlockPtr[0] << 8; // MSB
		LengthField |= SrmBlockPtr[1];      // LSB

		/* The size of the next-generation SRM block */
		BlockSize = LengthField;

		/* Verify the next-generation SRM block */
		Result = XHdcp22Tx_VerifySRM(SrmBlockPtr,
		                             BlockSize,
		                             KPubDpcPtr, // N
		                             XHDCP22_TX_KPUB_DCP_LLC_N_SIZE,
		                             &KPubDpcPtr[XHDCP22_TX_KPUB_DCP_LLC_N_SIZE], // e
		                             XHDCP22_TX_KPUB_DCP_LLC_E_SIZE);
		if (Result != XST_SUCCESS) {
			return XST_FAILURE;
		}

		/* Update the SRM Block pointer */
		SrmBlockPtr += BlockSize;
	}

	/* SRM has been verified to be correct.
	 * Now extract the revocation information */
	SrmBlockPtr = SrmPtr;

	/* byte 6,7,8 contain the length of the first-generation SRM in bytes.
	 * Value is in big endian format, Microblaze is little endian */
	LengthField  = SrmBlockPtr[5] << 16; // MSB
	LengthField |= SrmBlockPtr[6] << 8;
	LengthField |= SrmBlockPtr[7];       // LSB

	/* The size of the first-generation SRM block */
	BlockSize = LengthField + 5;

	/* byte 9,10 contain the number of devices of the firs-generation SRM block
	 * Value is in big endian format, Microblaze is little endian */
	NumDevices  = SrmBlockPtr[8] << 2; // MSB
	NumDevices |= SrmBlockPtr[9] >> 6; // LSB

	RevocationListPtr = XHdcp22Tx_GetRevocationReceiverIdList(InstancePtr);
	RevocationListPtr->NumDevices = 0;

	/* byte 12 will contain the first byte of the first receiver ID */
	ReceiverIdPtr = &SrmBlockPtr[12];

	for (i = 0; i < NumDevices; i++) {
		/* Is the revocation list full? */
		if (RevocationListPtr->NumDevices == XHDCP22_TX_REVOCATION_LIST_MAX_DEVICES) {
			return XST_FAILURE;
		}

		memcpy(RevocationListPtr->ReceiverId[RevocationListPtr->NumDevices], ReceiverIdPtr, XHDCP22_TX_SRM_RCVID_SIZE);
		RevocationListPtr->NumDevices++;
		ReceiverIdPtr += XHDCP22_TX_SRM_RCVID_SIZE;
	}

	/* Update the SRM Block pointer */
	SrmBlockPtr += BlockSize;

	for (j = 1; j < SrmGenNr; j++) {
		/* byte 1-2 contain the length of the next-generation SRM in bytes.
		 * Value is in big endian format, Microblaze is little endian */
		LengthField  = SrmBlockPtr[0] << 8; // MSB
		LengthField |= SrmBlockPtr[1];      // LSB

		/* The size of the next-generation SRM block */
		BlockSize = LengthField;

		/* byte 3,4 contain the number of devices of the next-generation SRM block
		 * Value is in big endian format, Microblaze is little endian */
		NumDevices  = (SrmBlockPtr[2] & 0x3) << 8; // MSB
		NumDevices |=  SrmBlockPtr[3]; // LSB

		/* byte 5 will contain the first byte of the first receiver ID */
		ReceiverIdPtr = &SrmBlockPtr[4];

		for (i = 0; i < NumDevices; i++) {
			/* Is the revocation list full? */
			if (RevocationListPtr->NumDevices == XHDCP22_TX_REVOCATION_LIST_MAX_DEVICES) {
				return XST_FAILURE;
			}

			memcpy(RevocationListPtr->ReceiverId[RevocationListPtr->NumDevices], ReceiverIdPtr, XHDCP22_TX_SRM_RCVID_SIZE);
			RevocationListPtr->NumDevices++;
			ReceiverIdPtr += XHDCP22_TX_SRM_RCVID_SIZE;
		}

		/* Update the SRM Block pointer */
		SrmBlockPtr += BlockSize;
	}

	InstancePtr->Info.IsRevocationListValid = (TRUE);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function searches for the specified ReceiverID in the revocation list.
*
* @param    InstancePtr is a pointer to the XHdcp22_Tx core instance.
* @param    RecvIdPtr is a pointer to the ReceiverID to lookup
*
* @return
*    - TRUE if Receiver ID was found in the revocation list
*    - FALSE if Receiver ID was not found in the revocation list
*
* @note     None.
*
******************************************************************************/
u8 XHdcp22Tx_IsDeviceRevoked(XHdcp22_Tx *InstancePtr, u8 *RecvIdPtr)
{
	int Result;
	u32 i;
	XHdcp22_Tx_RevocationList* RevocationListPtr = NULL;

	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(RecvIdPtr != NULL);

	RevocationListPtr = XHdcp22Tx_GetRevocationReceiverIdList(InstancePtr);

	/* Loop over the revocation list */
	for (i = 0; i < RevocationListPtr->NumDevices; i++) {
		Result = memcmp(RecvIdPtr, RevocationListPtr->ReceiverId[i], XHDCP22_TX_SRM_RCVID_SIZE);
		if (Result == 0)
			return TRUE;
	}
	return FALSE;
}

/*****************************************************************************/
/**
*
* This function returns a pointer to the Revocation Receiver ID list.
*
* @param  InstancePtr is a pointer to the XHdcp22_Tx instance.
*
* @return Pointer to the XHdcp22_Tx_RevocationList instance.
*
******************************************************************************/
XHdcp22_Tx_RevocationList* XHdcp22Tx_GetRevocationReceiverIdList(XHdcp22_Tx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return &InstancePtr->RevocationList;
}

/*****************************************************************************/
/**
*
* This function returns a pointer to the downstream topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return   A pointer to the XHdcp22_Tx_Topology structure or NULL when
*           the topology info is invalid.
*
* @note     None.
*
******************************************************************************/
XHdcp22_Tx_Topology *XHdcp22Tx_GetTopology(XHdcp22_Tx *InstancePtr)
{
	if (InstancePtr->Info.IsTopologyAvailable)
		return &InstancePtr->Topology;
	else
		return NULL;
}

/*****************************************************************************/
/**
*
* This function returns a pointer to the ReceiverID list in the repeater
* topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return   A pointer to the ReceiverID list in the topology structure.
*
* @note     None.
*
******************************************************************************/
u8 *XHdcp22Tx_GetTopologyReceiverIdList(XHdcp22_Tx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Topology.ReceiverId[0]);
}

/*****************************************************************************/
/**
* This function is used to get various fields inside the topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp22_Tx core instance.
* @param    Field indicates what field of the topology structure to get.
*
* @return   Value associated to field inside the topology structure.
*
* @note     None.
******************************************************************************/
u32 XHdcp22Tx_GetTopologyField(XHdcp22_Tx *InstancePtr, XHdcp22_Tx_TopologyField Field)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(Field < XHDCP22_TX_TOPOLOGY_INVALID);

	switch(Field)
	{
	case XHDCP22_TX_TOPOLOGY_DEPTH :
		return XHdcp22Tx_GetTopologyDepth(InstancePtr);
	case XHDCP22_TX_TOPOLOGY_DEVICECNT :
		return XHdcp22Tx_GetTopologyDeviceCnt(InstancePtr);
	case XHDCP22_TX_TOPOLOGY_MAXDEVSEXCEEDED :
		return XHdcp22Tx_GetTopologyMaxDevsExceeded(InstancePtr);
	case XHDCP22_TX_TOPOLOGY_MAXCASCADEEXCEEDED :
		return XHdcp22Tx_GetTopologyMaxCascadeExceeded(InstancePtr);
	case XHDCP22_TX_TOPOLOGY_HDCP20REPEATERDOWNSTREAM :
		return XHdcp22Tx_GetTopologyHdcp20RepeaterDownstream(InstancePtr);
	case XHDCP22_TX_TOPOLOGY_HDCP1DEVICEDOWNSTREAM :
		return XHdcp22Tx_GetTopologyHdcp1DeviceDownstream(InstancePtr);
	default:
		return 0;
	}
}

/*****************************************************************************/
/**
*
* This function sets the Content Stream Type.
*
* @param  InstancePtr is a pointer to the XHdcp22_Tx instance.
* @param  StreamType specifies the content stream type.
*
* @return None
*
******************************************************************************/
void XHdcp22Tx_SetContentStreamType(XHdcp22_Tx *InstancePtr, XHdcp22_Tx_ContentStreamType StreamType)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);

	InstancePtr->Info.ContentStreamType = StreamType;
	InstancePtr->Info.IsContentStreamTypeSet = (TRUE);
}

/*****************************************************************************/
/**
*
* This function reads the version.
*
* @param    InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return   Returns the version register of the cipher.
*
* @note     None.
*
******************************************************************************/
u32 XHdcp22Tx_GetVersion(XHdcp22_Tx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	return XHdcp22Cipher_GetVersion(&InstancePtr->Cipher);
}

/*****************************************************************************/
/**
*
* This function computes the subcore absolute address on axi-lite interface
* Subsystem is mapped at an absolute address and all included sub-cores are
* at pre-defined offset from the subsystem base address. To access the subcore
* register map from host CPU an absolute address is required.
*
* @param  BaseAddress is the base address of the subsystem instance
* @param  SubcoreOffset is the offset of the the subcore instance
* @param  SubcoreAddressPtr is the computed base address of the subcore instance
*
* @return XST_SUCCESS if base address computation is successful and within
*         subsystem address range else XST_FAILURE
*
******************************************************************************/
static int XHdcp22Tx_ComputeBaseAddress(UINTPTR BaseAddress, UINTPTR SubcoreOffset, UINTPTR *SubcoreAddressPtr)
{
	int Status;
	UINTPTR Address;

	Address = BaseAddress | SubcoreOffset;
	if((Address >= BaseAddress))
	{
		*SubcoreAddressPtr = Address;
		Status = XST_SUCCESS;
	}
	else
	{
		*SubcoreAddressPtr = 0;
		Status = XST_FAILURE;
	}

	return (Status);
}

/*****************************************************************************/
/**
*
* This function is a called to start authentication.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return
*    - XST_SUCCESS if authenticated is started successfully.
*    - XST_FAIL if the state machine is disabled.
*
* @note  None.
*
******************************************************************************/
int XHdcp22Tx_Authenticate(XHdcp22_Tx *InstancePtr)
{
	Xil_AssertNonvoid(InstancePtr);

	/* return a failure if not enabled */
	if (InstancePtr->Info.IsEnabled == (FALSE))
		return XST_FAILURE;

	/* state H1 checks this one */
	InstancePtr->Info.IsReceiverHDCP2Capable = (FALSE);

	/* initialize statemachine, and set to busy status */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_TX_AUTHENTICATION_BUSY;
	InstancePtr->Info.CurrentState = XHDCP22_TX_STATE_H0;
	InstancePtr->Info.PrvState = XHDCP22_TX_STATE_H0;

	/* Clear Topology Available flag */
	InstancePtr->Info.IsTopologyAvailable = (FALSE);

	/* If in repeater mode, clear the content stream type is set flag */
	if (XHdcp22Tx_IsRepeater(InstancePtr)) {
		InstancePtr->Info.IsContentStreamTypeSet = (FALSE);
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function is a executed every time to trigger the state machine.
* The user of HDCP22 TX is responsible to call this function on a regular base.
*
* @param   InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return  Enumerated authentication status defined in
*          #XHdcp22_Tx_AuthenticationType.
*
* @note    None.
*
******************************************************************************/
int XHdcp22Tx_Poll(XHdcp22_Tx *InstancePtr)
{
	XHdcp22_Tx_StateType NewState;
	XHdcp22_Tx_TransitionFuncType *Transition;
	XHdcp22_Tx_AuthenticationType PrvAuthenticationStatus;

	Xil_AssertNonvoid(InstancePtr != NULL);

	/* return immediately if not enabled */
	if (InstancePtr->Info.IsEnabled == (FALSE))
		return (int)(InstancePtr->Info.AuthenticationStatus);

	/* Store the authentication status before executing the next state */
	PrvAuthenticationStatus = InstancePtr->Info.AuthenticationStatus;

	/* continue executing the statemachine */
	NewState = XHdcp22_Tx_StateTable[InstancePtr->Info.CurrentState](InstancePtr);
	Transition = transition_table[InstancePtr->Info.CurrentState][NewState];

	if (Transition != NULL)
		Transition(InstancePtr);

	InstancePtr->Info.PrvState = InstancePtr->Info.CurrentState;
	InstancePtr->Info.CurrentState = NewState;

	/* Log only if the AuthenticationStatus status changes and do not log
	 * XHDCP22_TX_AUTHENTICATION_BUSY to avoid polluting the log buffer
	 */
	if(PrvAuthenticationStatus != InstancePtr->Info.AuthenticationStatus &&
	   InstancePtr->Info.AuthenticationStatus != XHDCP22_TX_AUTHENTICATION_BUSY) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_POLL_RESULT,
		                (u8)InstancePtr->Info.AuthenticationStatus);
	}

	return (int)(InstancePtr->Info.AuthenticationStatus);
}

/*****************************************************************************/
/**
*
* This function resets the state machine.
*
* @param   InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return  XST_SUCCESS
*
* @note    None.
*
******************************************************************************/
int XHdcp22Tx_Reset(XHdcp22_Tx *InstancePtr)
{
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* If in a authenticated state, execute the unauthenticated callback */
	if (InstancePtr->Info.AuthenticationStatus == XHDCP22_TX_AUTHENTICATED) {
		if (InstancePtr->IsUnauthenticatedCallbackSet)
			InstancePtr->UnauthenticatedCallback(InstancePtr->UnauthenticatedCallbackRef);
	}

	/* Initialize statemachine, but do not run it */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_TX_UNAUTHENTICATED;
	InstancePtr->Info.CurrentState = XHDCP22_TX_STATE_H0;
	InstancePtr->Info.PrvState = XHDCP22_TX_STATE_H0;

	/* Clear statistics counters */
	InstancePtr->Info.AuthRequestCnt = 0;
	InstancePtr->Info.ReauthRequestCnt = 0;

	/* Stop the timer if it's still running */
	XTmrCtr_Stop(&InstancePtr->Timer.TmrCtr, XHDCP22_TX_TIMER_CNTR_0);

	/* Clear Topology Available flag */
	InstancePtr->Info.IsTopologyAvailable = (FALSE);

	/* Disable encryption */
	XHdcp22Tx_DisableEncryption(InstancePtr);

     /* Reset Cipher */
	XHdcp22Cipher_Disable(&InstancePtr->Cipher);

	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_RESET, 0);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function enables the state machine and acts as a resume.
*
* @param   InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return  XST_SUCCESS
*
* @note    None.
*
******************************************************************************/
int XHdcp22Tx_Enable(XHdcp22_Tx *InstancePtr)
{
	Xil_AssertNonvoid(InstancePtr != NULL);

	InstancePtr->Info.IsEnabled = (TRUE);
	XHdcp22Cipher_Enable(&InstancePtr->Cipher);
	XTmrCtr_Stop(&InstancePtr->Timer.TmrCtr, XHDCP22_TX_TIMER_CNTR_0);
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_ENABLED, 1);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function disables the state machine and acts as a pause.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return XST_SUCCESS.
*
* @note   None.
*
******************************************************************************/
int XHdcp22Tx_Disable(XHdcp22_Tx *InstancePtr)
{
	Xil_AssertNonvoid(InstancePtr != NULL);

	InstancePtr->Info.IsEnabled = (FALSE);
	XHdcp22Cipher_Disable(&InstancePtr->Cipher);
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_ENABLED, 0);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function enables HDMI stream encryption by enabling the cipher.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return - XST_SUCCESS if authenticated is started successfully.
*         - XST_FAIL if the state machine is disabled.
*
* @note   None.
*
******************************************************************************/
int XHdcp22Tx_EnableEncryption(XHdcp22_Tx *InstancePtr)
{

	Xil_AssertNonvoid(InstancePtr != NULL);

	if (XHdcp22Tx_IsAuthenticated(InstancePtr)) {
		XHdcp22Cipher_EnableTxEncryption(&InstancePtr->Cipher);
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_ENCR_ENABLED, 1);
		return XST_SUCCESS;
  }

  return XST_FAILURE;
}

/*****************************************************************************/
/**
*
* This function disables HDMI stream encryption by disabling the cipher.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return XST_SUCCESS.
*
* @note   None.
*
******************************************************************************/
int XHdcp22Tx_DisableEncryption(XHdcp22_Tx *InstancePtr)
{
	Xil_AssertNonvoid(InstancePtr != NULL);
	XHdcp22Cipher_DisableTxEncryption(&InstancePtr->Cipher);
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_ENCR_ENABLED, 0);
	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function enables the blank output for the cipher.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return - XST_SUCCESS if authenticated is started successfully.
*         - XST_FAIL if the state machine is disabled.
*
* @note   None.
*
******************************************************************************/
void XHdcp22Tx_EnableBlank(XHdcp22_Tx *InstancePtr)
{
	Xil_AssertVoid(InstancePtr != NULL);

	XHdcp22Cipher_Blank(&InstancePtr->Cipher, TRUE);
}

/*****************************************************************************/
/**
*
* This function disables the blank output for the cipher.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return - XST_SUCCESS if authenticated is started successfully.
*         - XST_FAIL if the state machine is disabled.
*
* @note   None.
*
******************************************************************************/
void XHdcp22Tx_DisableBlank(XHdcp22_Tx *InstancePtr)
{
	Xil_AssertVoid(InstancePtr != NULL);

	XHdcp22Cipher_Blank(&InstancePtr->Cipher, FALSE);
}

/*****************************************************************************/
/**
*
* This function returns the current repeater mode status.
*
* @param  InstancePtr is a pointer to the XHdcp22_Tx instance.
*
* @return
*   - TRUE if the HDCP 2.2 instance is part of the downstream port of a repeater
*   - FALSE if the HDCP 2.2 instance is a transmitter
*
******************************************************************************/
u8 XHdcp22Tx_IsRepeater(XHdcp22_Tx *InstancePtr)
{
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Config.Mode != XHDCP22_TX_TRANSMITTER)?TRUE:FALSE;
}

/*****************************************************************************/
/**
*
* This function sets the repeater mode status.
*
* @param  InstancePtr is a pointer to the XHdcp22_Tx instance.
* @param  Set is TRUE to enable the repeater mode and FALSE to disable.
*
* @return None.
*
******************************************************************************/
void XHdcp22Tx_SetRepeater(XHdcp22_Tx *InstancePtr, u8 Set)
{
	Xil_AssertVoid(InstancePtr != NULL);

	if (Set)
		InstancePtr->Config.Mode = XHDCP22_TX_REPEATER;
	else
		InstancePtr->Config.Mode = XHDCP22_TX_TRANSMITTER;
}

/*****************************************************************************/
/**
*
* This function returns the current enabled state.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return TRUE if enabled otherwise FALSE.
*
* @note   None.
*
******************************************************************************/
u8 XHdcp22Tx_IsEnabled (XHdcp22_Tx *InstancePtr)
{
	return InstancePtr->Info.IsEnabled;
}

/*****************************************************************************/
/**
*
* This function returns the current encryption enabled state.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return TRUE if encryption is enabled otherwise FALSE.
*
* @note   None.
*
******************************************************************************/
u8 XHdcp22Tx_IsEncryptionEnabled (XHdcp22_Tx *InstancePtr)
{
	u32 Status;

	Status = XHdcp22Cipher_IsEncrypted(&InstancePtr->Cipher);

	return (Status) ? TRUE : FALSE;
}

/*****************************************************************************/
/**
*
* This function returns the current progress state.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return TRUE if authentication is in progress otherwise FALSE.
*
* @note   None.
*
******************************************************************************/
u8 XHdcp22Tx_IsInProgress (XHdcp22_Tx *InstancePtr)
{
	return (InstancePtr->Info.AuthenticationStatus !=
	       XHDCP22_TX_UNAUTHENTICATED) ? (TRUE) : (FALSE);
}

/*****************************************************************************/
/**
*
* This function returns the current authenticated state.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return TRUE if authenticated otherwise FALSE.
*
* @note   None.
*
******************************************************************************/
u8 XHdcp22Tx_IsAuthenticated (XHdcp22_Tx *InstancePtr)
{
	return (InstancePtr->Info.AuthenticationStatus ==
	                    XHDCP22_TX_AUTHENTICATED) ? (TRUE) : (FALSE);
}

/*****************************************************************************/
/**
*
* This function checks if the downstream device HDCP2Version register is set.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return - TRUE if downstream device is HDCP 2.2 capable.
*         - FALSE if downstream device is not HDCP 2.2 capable.
*
* @note   None.
*
******************************************************************************/
u8 XHdcp22Tx_IsDwnstrmCapable (XHdcp22_Tx *InstancePtr)
{
	u8 DdcBuf[1];
	int Status = XST_FAILURE;

	if (InstancePtr->DdcHandlerRef) {
		/* Read HDCP2Version register */
		InstancePtr->IsReceiverHDCP2Capable = (FALSE);
		DdcBuf[0] = XHDCP22_TX_HDCPPORT_VERSION_OFFSET;
		Status = InstancePtr->DdcWrite(XHDCP22_TX_DDC_BASE_ADDRESS, 1, DdcBuf,
		                              (FALSE), InstancePtr->DdcHandlerRef);
		if (Status == (XST_SUCCESS)) {
			Status = InstancePtr->DdcRead(XHDCP22_TX_DDC_BASE_ADDRESS,
			                              sizeof(DdcBuf), DdcBuf, (TRUE),
			                              InstancePtr->DdcHandlerRef);
		}
	}

	/* Check expected value */
	if (Status == (XST_SUCCESS)) {
		if (DdcBuf[0]  == 0x04) {
			return TRUE;
		}
		else {
			return FALSE;
          }
	}
	else {
		return FALSE;
	}
}

/*****************************************************************************/
/**
*
* This function installs callback functions for the given
* HandlerType:
*
* <pre>
* HandlerType                                        Callback Function Type
* -------------------------                          ---------------------------
* (XHDCP22_TX_HANDLER_DDC_WRITE)                     DdcWrite
* (XHDCP22_TX_HANDLER_DDC_READ)                      DdcRead
* (XHDCP22_TX_HANDLER_AUTHENTICATED)                 AuthenticatedCallback
* (XHDCP22_TX_HANDLER_UNAUTHENTICATED)               UnauthenticatedCallback
* (XHDCP22_TX_HANDLER_DOWNSTREAM_TOPOLOGY_AVAILABLE) DownstreamTopologyAvailableCallback
* </pre>
*
* @param	InstancePtr is a pointer to the HDMI RX core instance.
* @param	HandlerType specifies the type of handler.
* @param	CallbackFunc is the address of the callback function.
* @param	CallbackRef is a user data item that will be passed to the
*			callback function when it is invoked.
*
* @return
*			- XST_SUCCESS if callback function installed successfully.
*			- XST_INVALID_PARAM when HandlerType is invalid.
*
* @note		Invoking this function for a handler that already has been
*			installed replaces it with the new handler.
*
******************************************************************************/
int XHdcp22Tx_SetCallback(XHdcp22_Tx *InstancePtr, XHdcp22_Tx_HandlerType HandlerType, void *CallbackFunc, void *CallbackRef)
{
	/* Verify arguments. */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(HandlerType > (XHDCP22_TX_HANDLER_UNDEFINED));
	Xil_AssertNonvoid(HandlerType < (XHDCP22_TX_HANDLER_INVALID));
	Xil_AssertNonvoid(CallbackFunc != NULL);
	Xil_AssertNonvoid(CallbackRef != NULL);

	u32 Status;

	/* Check for handler type */
	switch (HandlerType)
	{
		// DDC write
		case (XHDCP22_TX_HANDLER_DDC_WRITE):
			InstancePtr->DdcWrite = (XHdcp22_Tx_DdcHandler)CallbackFunc;
			InstancePtr->DdcHandlerRef = CallbackRef;
			InstancePtr->IsDdcWriteSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// DDC read
		case (XHDCP22_TX_HANDLER_DDC_READ):
			InstancePtr->DdcRead = (XHdcp22_Tx_DdcHandler)CallbackFunc;
			InstancePtr->DdcHandlerRef = CallbackRef;
			InstancePtr->IsDdcReadSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// Authenticated
		case (XHDCP22_TX_HANDLER_AUTHENTICATED):
			InstancePtr->AuthenticatedCallback = (XHdcp22_Tx_Callback)CallbackFunc;
			InstancePtr->AuthenticatedCallbackRef = CallbackRef;
			InstancePtr->IsAuthenticatedCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// Authentication failed
		case (XHDCP22_TX_HANDLER_UNAUTHENTICATED):
			InstancePtr->UnauthenticatedCallback = (XHdcp22_Tx_Callback)CallbackFunc;
			InstancePtr->UnauthenticatedCallbackRef = CallbackRef;
			InstancePtr->IsUnauthenticatedCallbackSet = (TRUE);
			Status = (XST_SUCCESS);
			break;

		// Downstream topology is available
		case (XHDCP22_TX_HANDLER_DOWNSTREAM_TOPOLOGY_AVAILABLE) :
			InstancePtr->DownstreamTopologyAvailableCallback = (XHdcp22_Tx_Callback)CallbackFunc;
			InstancePtr->DownstreamTopologyAvailableCallbackRef = CallbackRef;
			InstancePtr->IsDownstreamTopologyAvailableCallbackSet = (TRUE);
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
* This function returns the pointer to the internal timer control instance
* needed for connecting the timer interrupt to an interrupt controller.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return A pointer to the internal timer control instance.
*
* @note   None.
*
******************************************************************************/
XTmrCtr* XHdcp22Tx_GetTimer(XHdcp22_Tx *InstancePtr)
{
	Xil_AssertNonvoid(InstancePtr != NULL);
	return &InstancePtr->Timer.TmrCtr;
}

/*****************************************************************************/
/**
*
* This function is the implementation of the HDCP transmit H0 state.
* According protocol this is the reset state that is entered initially.
* As soon as hot plug is detected and Rx is available, a transisition to state h1 is
* performed. Since hotplug detect is controlled by the user,
* the next state is always state H1.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return XHDCP22_TX_STATE_H1
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateH0(XHdcp22_Tx *InstancePtr)
{
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE, (u16)XHDCP22_TX_STATE_H0);

	return XHDCP22_TX_STATE_H1;
}

/*****************************************************************************/
/**
*
* This function is the implementation of the HDCP transmit H1 state.
* As soon as hot plug is detected and Rx is available, a transisition to
* this state is performed. Hotplug detection if controlled by the user of this core.
* This means that this state is practically the entry state.
*
* @param  InstancePtr is a pointer to the XHDCP22 TX core instance.
*
* @return
*         - XHDCP22_TX_STATE_A0 if the attached receiver is HDCP2 capable.
*         - XHDCP22_TX_STATE_H1 if the attached receiver is NOT HDCP2 capable.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateH1(XHdcp22_Tx *InstancePtr)
{
	/* Avoid polluting */
	if (InstancePtr->Info.PrvState != InstancePtr->Info.CurrentState) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE, (u16)XHDCP22_TX_STATE_H1);
	}

	/* Stay in this state until XHdcp22Tx_Authenticate is called and the status
	 * is set to XHDCP22_TX_AUTHENTICATION_BUSY */
	if (InstancePtr->Info.AuthenticationStatus != XHDCP22_TX_AUTHENTICATION_BUSY) {
		return XHDCP22_TX_STATE_H1;
	}

	/* HDCP2Version */
	InstancePtr->IsReceiverHDCP2Capable = XHdcp22Tx_IsDwnstrmCapable(InstancePtr);

	if (InstancePtr->IsReceiverHDCP2Capable) {
		return XHDCP22_TX_STATE_A0;
	}

	InstancePtr->Info.AuthenticationStatus = XHDCP22_TX_INCOMPATIBLE_RX;
	return XHDCP22_TX_STATE_H1;
}

/*****************************************************************************/
/**
*
* This function is the implementation of the HDCP transmit A0 state.
* If content protection not desired, return to H1 state.
*
* @param  InstancePtr is a pointer to the XHDCP22 TX core instance.
*
* @return
*         - XHDCP22_TX_STATE_H1 if state machine is set to disabled (content protection
*           NOT desired).
*         - XHDCP22_TX_STATE_A1 if authentication must start.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA0(XHdcp22_Tx *InstancePtr)
{
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE, (u16)XHDCP22_TX_STATE_A0);

	/* Check if HDCP2Capable flag is true */
	if (!InstancePtr->IsReceiverHDCP2Capable) {
		InstancePtr->Info.AuthenticationStatus = XHDCP22_TX_INCOMPATIBLE_RX;
		return XHDCP22_TX_STATE_H1;
	}

	/* Content protection not desired; go back to H1 state */
	if (InstancePtr->Info.IsEnabled == (FALSE))
		return XHDCP22_TX_STATE_H1;

	/* Authentication starts, set status as BUSY */
	InstancePtr->Info.AuthenticationStatus = XHDCP22_TX_AUTHENTICATION_BUSY;

	/* Disable encryption */
	XHdcp22Tx_DisableEncryption(InstancePtr);

	/* Start the timer for authentication.
        This is required to ensure that encryption is disabled before
        authentication is requested */
	XHdcp22Tx_StartTimer(InstancePtr, 100, XHDCP22_TX_AKE_INIT);

	return XHDCP22_TX_STATE_A1;
}

/*****************************************************************************/
/**
*
* This function executes the first part of the A1 state of the
* HDCP2.2 TX protocol.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return XHDCP22_TX_STATE_A1_1
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA1(XHdcp22_Tx *InstancePtr)
{
	int Result = XST_SUCCESS;

#ifndef _XHDCP22_TX_DISABLE_TIMEOUT_CHECKING_
     /* Wait for 100ms timer to expire
        This timeout ensures that encryption is disabled
        before authentication is requested */
	if (InstancePtr->Timer.TimerExpired == (FALSE)) {
		return XHDCP22_TX_STATE_A1;
	}
#endif

	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE, (u16)XHDCP22_TX_STATE_A1);

	/* Write AKE_Init message */
	Result = XHdcp22Tx_WriteAKEInit(InstancePtr);

	if (Result != XST_SUCCESS) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_MSG_WRITE_FAIL);
		return XHDCP22_TX_STATE_A0;
	}

	/* Start the timer for receiving XHDCP22_TX_AKE_SEND_CERT */
	XHdcp22Tx_StartTimer(InstancePtr, 100, XHDCP22_TX_AKE_SEND_CERT);

	/* Reset some variables */
	InstancePtr->Topology.DeviceCnt = 0;
	InstancePtr->Topology.Depth = 0;
	InstancePtr->Topology.MaxDevsExceeded = FALSE;
	InstancePtr->Topology.MaxCascadeExceeded = FALSE;
	InstancePtr->Topology.Hdcp20RepeaterDownstream = FALSE;
	InstancePtr->Topology.Hdcp1DeviceDownstream = FALSE;
	InstancePtr->Info.ReceivedFirstSeqNum_V = FALSE;
	InstancePtr->Info.SentFirstSeqNum_M = FALSE;
	InstancePtr->Info.IsContentStreamTypeSent = FALSE;
	InstancePtr->Info.SeqNum_M = 0;
	InstancePtr->Info.ContentStreamManageCheckCounter = 0;

	/* Goto the waiting state for AKE_SEND_CERT */
	return XHDCP22_TX_STATE_A1_1;
}

/******************************************************************************/
/**
*
* This function executes a part of the A1 state functionality of the HDCP2.2
* TX protocol.
* It receives the certificate and decides if it should continue to a
* 'no stored km' or 'stored Km'intermediate state.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return
*         - XHDCP22_TX_STATE_A0 on errors.
*         - XHDCP22_TX_STATE_A1_1 if busy waiting for the
*           XHDCP22_TX_AKE_SEND_CERT message.
*         - XHDCP22_TX_STATE_A1_SK0 for a 'Stored Km' scenario.
*         - XHDCP22_TX_STATE_A1_NSK0 for a 'Non Stored Km' scenario.
*
* @note   None.
*
******************************************************************************/
XHdcp22_Tx_StateType Xhdcp22Tx_StateA1_1(XHdcp22_Tx *InstancePtr)
{
	int Result = XST_SUCCESS;
	XHdcp22_Tx_DDCMessage *MsgPtr = (XHdcp22_Tx_DDCMessage *)InstancePtr->MessageBuffer;
	XHdcp22_Tx_PairingInfo *PairingInfoPtr = NULL;
	const u8* KPubDpcPtr = NULL;
	XHdcp22_Tx_PairingInfo NewPairingInfo;

	/* receive AKE Send message, wait for 100 ms */
	Result = XHdcp22Tx_WaitForReceiver(InstancePtr, sizeof(XHdcp22_Tx_AKESendCert), FALSE);
	if (Result != XST_SUCCESS) {
		return XHDCP22_TX_STATE_A0;
	}
	if (InstancePtr->Info.MsgAvailable == (FALSE)) {
		return XHDCP22_TX_STATE_A1_1;
	}

	/* Log after waiting */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE, (u16)XHDCP22_TX_STATE_A1_1);

	/* Receive the RX certificate */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_RX_CERT);
	Result = XHdcp22Tx_ReceiveMsg(InstancePtr, XHDCP22_TX_AKE_SEND_CERT,
									sizeof(XHdcp22_Tx_AKESendCert));
	if (Result != XST_SUCCESS) {
		return XHDCP22_TX_STATE_A0;
	}

	/* Verify the signature */
	KPubDpcPtr = XHdcp22Tx_GetKPubDpc(InstancePtr);
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	                XHDCP22_TX_LOG_DBG_VERIFY_SIGNATURE);
	Result = XHdcp22Tx_VerifyCertificate(&MsgPtr->Message.AKESendCert.CertRx,
	                   KPubDpcPtr, /* N */
	                   XHDCP22_TX_KPUB_DCP_LLC_N_SIZE,
	                   &KPubDpcPtr[XHDCP22_TX_KPUB_DCP_LLC_N_SIZE], /* e */
	                   XHDCP22_TX_KPUB_DCP_LLC_E_SIZE);

	if (Result != XST_SUCCESS) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	                XHDCP22_TX_LOG_DBG_VERIFY_SIGNATURE_FAIL);
		return XHDCP22_TX_STATE_A0;
	}
	else {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	                XHDCP22_TX_LOG_DBG_VERIFY_SIGNATURE_PASS);
	}

	/* SRM and revocation check are only performed by the top-level HDCP transmitter */
	if (InstancePtr->Config.Mode == XHDCP22_TX_TRANSMITTER) {
		/* Check to see wether there is a valid SRM loaded */
		if (InstancePtr->Info.IsRevocationListValid == FALSE) {
			/* No valid revocation list loaded. According to
			 * the HDCP spec, authentication has to be aborted */
			InstancePtr->Info.AuthenticationStatus = XHDCP22_TX_NO_SRM_LOADED;
			return XHDCP22_TX_STATE_A0;
		}

		/* Check to see wether the Receiver ID is in the revocation list */
		if (XHdcp22Tx_IsDeviceRevoked(InstancePtr, MsgPtr->Message.AKESendCert.CertRx.ReceiverId)) {
			XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
				XHDCP22_TX_LOG_DBG_DEVICE_IS_REVOKED);
			InstancePtr->Info.IsDeviceRevoked = TRUE;
			InstancePtr->Info.AuthenticationStatus = XHDCP22_TX_DEVICE_IS_REVOKED;
			return XHDCP22_TX_STATE_A0;
		}
		else {
			InstancePtr->Info.IsDeviceRevoked = FALSE;
		}
	}

	/* Add receiver ID to the topology info */
	memcpy(&InstancePtr->Topology.ReceiverId[0],
		MsgPtr->Message.AKESendCert.CertRx.ReceiverId,
		XHDCP22_TX_RCVID_SIZE);
	InstancePtr->Topology.DeviceCnt = 1;

	/* Check to see wether the Receiver is an HDCP Repeater */
	if (MsgPtr->Message.AKESendCert.RxCaps[2] & 0x1) { /* big endian! */
		InstancePtr->Info.IsReceiverRepeater = TRUE;
	}
	else {
		InstancePtr->Info.IsReceiverRepeater = FALSE;
	}

	/* Store received Rrx for calculations in other states */
	memcpy(InstancePtr->Info.Rrx,  MsgPtr->Message.AKESendCert.Rrx,
			 sizeof(InstancePtr->Info.Rrx));

	/* Get pairing info pointer for the connected receiver */
	PairingInfoPtr = XHdcp22Tx_GetPairingInfo(InstancePtr, MsgPtr->Message.AKESendCert.CertRx.ReceiverId);

	/********************* Handle Stored Km **********************************/
	/* If already existing handle Stored Km sequence: Write AKE_Stored_Km
	 * and wait for H Prime */
	if (PairingInfoPtr != NULL) {
		if (PairingInfoPtr->Ready == TRUE) {
			/* Update RxCaps in pairing info */
			memcpy(PairingInfoPtr->RxCaps, MsgPtr->Message.AKESendCert.RxCaps,
				sizeof(PairingInfoPtr->RxCaps));

			/* Write encrypted Km */
			Result = XHdcp22Tx_WriteAKEStoredKm(InstancePtr, PairingInfoPtr);

			if (Result != XST_SUCCESS) {
				XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
					XHDCP22_TX_LOG_DBG_MSG_WRITE_FAIL);
				return XHDCP22_TX_STATE_A0;
			}

			InstancePtr->Info.StateContext = PairingInfoPtr;

			/* Start the timer for receiving XHDCP22_TX_AKE_SEND_HPRIME */
			XHdcp22Tx_StartTimer(InstancePtr, 200, XHDCP22_TX_AKE_SEND_H_PRIME);
			return XHDCP22_TX_STATE_A1_SK0;
		}
	}

	/********************* Handle No Stored Km *******************************/
	/* Update pairing info */
	memcpy(NewPairingInfo.Rrx, InstancePtr->Info.Rrx,
	       sizeof(NewPairingInfo.Rrx));
	memcpy(NewPairingInfo.Rtx, InstancePtr->Info.Rtx,
	       sizeof(NewPairingInfo.Rtx));
	memcpy(NewPairingInfo.RxCaps, MsgPtr->Message.AKESendCert.RxCaps,
	       sizeof(NewPairingInfo.RxCaps));
	memcpy(NewPairingInfo.ReceiverId, MsgPtr->Message.AKESendCert.CertRx.ReceiverId,
	       sizeof(NewPairingInfo.ReceiverId));

	/* Generate the hashed Km */
	XHdcp22Tx_GenerateKm(InstancePtr, NewPairingInfo.Km);

	/* Done with first step, update pairing info and goto the next
	 * step in the No Stored Km sequence: waiting for H Prime*/
	PairingInfoPtr = XHdcp22Tx_UpdatePairingInfo(InstancePtr, &NewPairingInfo, FALSE);
	if (PairingInfoPtr == NULL) {
		return XHDCP22_TX_STATE_A0;
	}

	InstancePtr->Info.StateContext = (void *)PairingInfoPtr;

	/* Write encrypted Km  */
	Result = XHdcp22Tx_WriteAKENoStoredKm(InstancePtr, &NewPairingInfo,
	                             &MsgPtr->Message.AKESendCert.CertRx);

	if (Result != XST_SUCCESS) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_MSG_WRITE_FAIL);
		return XHDCP22_TX_STATE_A0;
	}

	/* Start the timer for receiving XHDCP22_TX_AKE_SEND_HPRIME */
	XHdcp22Tx_StartTimer(InstancePtr, 1000, XHDCP22_TX_AKE_SEND_H_PRIME);

	return XHDCP22_TX_STATE_A1_NSK0;
}

/******************************************************************************/
/**
*
* This function executes a part of the A1 state functionality of the HDCP2.2
* TX protocol.
* It handles a part of the 'No Stored Km' scenario and receives and verifies H'
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return
*         - XHDCP22_TX_STATE_A0 on errors.
*         - XHDCP22_TX_STATE_A1_NSK0 if busy waiting for
*           the XHDCP22_TX_AKE_SEND_H_PRIME message.
*         - XHDCP22_TX_STATE_A1_NSK1 for the next part of the 'Stored Km' scenario.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType Xhdcp22Tx_StateA1_Nsk0(XHdcp22_Tx *InstancePtr)
{
	int Result = XST_SUCCESS;
	u8 HPrime[XHDCP22_TX_H_PRIME_SIZE];

	XHdcp22_Tx_PairingInfo *PairingInfoPtr =
	                       (XHdcp22_Tx_PairingInfo *)InstancePtr->Info.StateContext;
	XHdcp22_Tx_DDCMessage *MsgPtr = (XHdcp22_Tx_DDCMessage *)InstancePtr->MessageBuffer;

	/* Wait for the receiver to respond within 1 second.
	 * If the receiver has timed out we go to state A0.
	 * If the receiver is busy, we stay in this state (return from polling).
	 * If the receiver has finished, but the message was not handled yet,
	 * we handle the message.*/
	Result = XHdcp22Tx_WaitForReceiver(InstancePtr, sizeof(XHdcp22_Tx_AKESendHPrime), FALSE);
	if (Result != XST_SUCCESS) {
		XHdcp22Tx_InvalidatePairingInfo(InstancePtr, PairingInfoPtr->ReceiverId);
		return XHDCP22_TX_STATE_A0;
	}
	if (InstancePtr->Info.MsgAvailable == (FALSE)) {
		return XHDCP22_TX_STATE_A1_NSK0;
	}

	/* Log after waiting */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE, (u16)XHDCP22_TX_STATE_A1_NSK0);

	/* Receive H' */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_RX_H1);
	Result = XHdcp22Tx_ReceiveMsg(InstancePtr, XHDCP22_TX_AKE_SEND_H_PRIME,
	                              sizeof(XHdcp22_Tx_AKESendHPrime));
	if (Result != XST_SUCCESS) {
		XHdcp22Tx_InvalidatePairingInfo(InstancePtr, PairingInfoPtr->ReceiverId);
		return XHDCP22_TX_STATE_A0;
	}

	/* Verify the received H' */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
                  XHDCP22_TX_LOG_DBG_COMPUTE_H);
	XHdcp22Tx_ComputeHPrime(PairingInfoPtr->Rrx, PairingInfoPtr->RxCaps,
	                        PairingInfoPtr->Rtx, XHdcp22_Tx_TxCaps,
	                        PairingInfoPtr->Km, HPrime);
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	                XHDCP22_TX_LOG_DBG_COMPUTE_H_DONE);

	if(memcmp(MsgPtr->Message.AKESendHPrime.HPrime, HPrime, sizeof(HPrime)) != 0) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
		                XHDCP22_TX_LOG_DBG_COMPARE_H_FAIL);
		XHdcp22Tx_InvalidatePairingInfo(InstancePtr, PairingInfoPtr->ReceiverId);
		return XHDCP22_TX_STATE_A0;
	}

	/* Start the timer for receiving XHDCP22_TX_AKE_SEND_PAIRING_INFO */
	XHdcp22Tx_StartTimer(InstancePtr, 200, XHDCP22_TX_AKE_SEND_PAIRING_INFO);
	return XHDCP22_TX_STATE_A1_NSK1;
}

/******************************************************************************/
/**
*
* This function executes a part of the A1 state functionality of the HDCP2.2
* TX protocol.
* It handles a part of the  'No Stored Km' scenario and receives and
* stores pairing info.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return
*         - XHDCP22_TX_STATE_A0 on errors.
*         - XHDCP22_TX_STATE_A1_NSK1 if busy waiting for
*           the XHDCP22_TX_AKE_SEND_PAIRING_INFO message.
*         - XHDCP22_TX_STATE_A2 if authentication is done.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType Xhdcp22Tx_StateA1_Nsk1(XHdcp22_Tx *InstancePtr)
{
	int Result = XST_SUCCESS;

	XHdcp22_Tx_PairingInfo *PairingInfoPtr =
	                       (XHdcp22_Tx_PairingInfo *)InstancePtr->Info.StateContext;
	XHdcp22_Tx_DDCMessage *MsgPtr =
	                       (XHdcp22_Tx_DDCMessage *)InstancePtr->MessageBuffer;

	/* Wait for the receiver to send AKE_Send_Pairing_Info */
	Result = XHdcp22Tx_WaitForReceiver(InstancePtr, sizeof(XHdcp22_Tx_AKESendPairingInfo), FALSE);
	if (Result != XST_SUCCESS) {
		XHdcp22Tx_InvalidatePairingInfo(InstancePtr, PairingInfoPtr->ReceiverId);
		return XHDCP22_TX_STATE_A0;
	}
	if (InstancePtr->Info.MsgAvailable == (FALSE)) {
		return XHDCP22_TX_STATE_A1_NSK1;
	}

	/* Log after waiting */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE,
	                (u16)XHDCP22_TX_STATE_A1_NSK1);

	/* Receive the expected message */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_RX_EKHKM);
	Result = XHdcp22Tx_ReceiveMsg(InstancePtr, XHDCP22_TX_AKE_SEND_PAIRING_INFO,
	                              sizeof(XHdcp22_Tx_AKESendPairingInfo));
	if (Result != XST_SUCCESS) {
		XHdcp22Tx_InvalidatePairingInfo(InstancePtr, PairingInfoPtr->ReceiverId);
		return XHDCP22_TX_STATE_A0;
	}

	/* Store the pairing info with the received Ekh(Km) */
	memcpy(PairingInfoPtr->Ekh_Km, MsgPtr->Message.AKESendPairingInfo.EKhKm,
	       sizeof(PairingInfoPtr->Ekh_Km));

	XHdcp22Tx_UpdatePairingInfo(InstancePtr, PairingInfoPtr, TRUE);

	/* Authentication done, goto the next state (exchange Ks) */
	return XHDCP22_TX_STATE_A2;
}

/******************************************************************************/
/**
*
* This function executes a part of the A1 state functionality of the HDCP2.2
* TX protocol.
* It handles a part of the  'Stored Km' scenario and receives and verifies
* H'.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return
*         - XHDCP22_TX_STATE_A0 on errors.
*         - XHDCP22_TX_STATE_A1_SK0 if busy waiting for the
*         - XHDCP22_TX_AKE_SEND_H_PRIME message.
*         - XHDCP22_TX_STATE_A2 if authentication is done.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType Xhdcp22Tx_StateA1_Sk0(XHdcp22_Tx *InstancePtr)
{
	int Result = XST_SUCCESS;
	u8 HPrime[XHDCP22_TX_H_PRIME_SIZE];

	XHdcp22_Tx_PairingInfo *PairingInfoPtr =
						(XHdcp22_Tx_PairingInfo *)InstancePtr->Info.StateContext;
	XHdcp22_Tx_DDCMessage *MsgPtr = (XHdcp22_Tx_DDCMessage *)InstancePtr->MessageBuffer;

	/* Wait for the receiver to respond within 1 second.
	 * If the receiver has timed out we go to state A0.
	 * If the receiver is busy, we stay in this state (return from polling).
	 * If the receiver has finished, but the message was not handled yet,
	 * we handle the message.*/
	Result = XHdcp22Tx_WaitForReceiver(InstancePtr, sizeof(XHdcp22_Tx_AKESendHPrime), FALSE);
	if (Result != XST_SUCCESS) {
		return XHDCP22_TX_STATE_A0;
	}
	if (InstancePtr->Info.MsgAvailable == (FALSE)) {
		return XHDCP22_TX_STATE_A1_SK0;
	}

	/* Log after waiting */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE,
	(u16)XHDCP22_TX_STATE_A1_SK0);

	/* Receive the expected message */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_RX_H1);
	Result = XHdcp22Tx_ReceiveMsg(InstancePtr, XHDCP22_TX_AKE_SEND_H_PRIME,
									sizeof(XHdcp22_Tx_AKESendHPrime));
	if (Result != XST_SUCCESS) {
		return XHDCP22_TX_STATE_A0;
	}

	/* Verify the received H' */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
								 XHDCP22_TX_LOG_DBG_COMPUTE_H);
	XHdcp22Tx_ComputeHPrime(InstancePtr->Info.Rrx, PairingInfoPtr->RxCaps,
							InstancePtr->Info.Rtx, XHdcp22_Tx_TxCaps,
							PairingInfoPtr->Km, HPrime);
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
								 XHDCP22_TX_LOG_DBG_COMPUTE_H_DONE);

	if(memcmp(MsgPtr->Message.AKESendHPrime.HPrime, HPrime, sizeof(HPrime)) != 0) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
		                XHDCP22_TX_LOG_DBG_COMPARE_H_FAIL);
		XHdcp22Tx_InvalidatePairingInfo(InstancePtr, PairingInfoPtr->ReceiverId);
		return XHDCP22_TX_STATE_A0;
	}
	return XHDCP22_TX_STATE_A2;
}

/*****************************************************************************/
/**
*
* This function executes the A2 state of the HDCP2.2 TX protocol.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return The next state to execute.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA2(XHdcp22_Tx *InstancePtr)
{
	int Result;

	/* Log but don't clutter our log buffer, check on counter */
	if (InstancePtr->Info.LocalityCheckCounter == 0) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE,
		                (u16)XHDCP22_TX_STATE_A2);
	}

	/* It is allowed to retry locality check until it has been
	 * checked for 1024 times */
	InstancePtr->Info.LocalityCheckCounter++;

	if (InstancePtr->Info.LocalityCheckCounter > XHDCP22_TX_MAX_ALLOWED_LOCALITY_CHECKS) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_LCCHK_COUNT,
		                (u16)InstancePtr->Info.LocalityCheckCounter-1);
		return XHDCP22_TX_STATE_A0;
	}

	/* Generate Rn */
	XHdcp22Tx_GenerateRn(InstancePtr, InstancePtr->Info.Rn);

	/* Send LC Init message */
	Result = XHdcp22Tx_WriteLcInit(InstancePtr, InstancePtr->Info.Rn);

	if (Result != XST_SUCCESS) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_MSG_WRITE_FAIL);
		return XHDCP22_TX_STATE_A0;
	}

	/* Start the timer for receiving XHDCP22_TX_AKE_SEND_PAIRING_INFO */
	XHdcp22Tx_StartTimer(InstancePtr, 20, XHDCP22_TX_LC_SEND_L_PRIME);

	return XHDCP22_TX_STATE_A2_1;
}


/*****************************************************************************/
/**
*
* This function executes a part of the A2 state functionality of the HDCP2.2
* TX protocol.
* It receives and verifies L' (locality check).
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return
*         - XHDCP22_TX_STATE_A2_1 if busy waiting for the
*           XHDCP22_TX_AKE_SEND_L_PRIME message.
*         - XHDCP22_TX_STATE_A3 if authentication is done.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA2_1(XHdcp22_Tx *InstancePtr)
{
	int Result = XST_SUCCESS;
	u8 LPrime[XHDCP22_TX_H_PRIME_SIZE];

	XHdcp22_Tx_PairingInfo *PairingInfoPtr =
	                       (XHdcp22_Tx_PairingInfo *)InstancePtr->Info.StateContext;
	XHdcp22_Tx_DDCMessage *MsgPtr =
	                      (XHdcp22_Tx_DDCMessage *)InstancePtr->MessageBuffer;

	/* Wait for the receiver to respond within 20 msecs.
	 * If the receiver has timed out we go to state A2 for a retry.
	 * If the receiver is busy, we stay in this state (return from polling).
	 * If the receiver has finished, but the message was not handled yet,
	 * we handle the message.*/
	Result = XHdcp22Tx_WaitForReceiver(InstancePtr, sizeof(XHdcp22_Tx_LCSendLPrime), FALSE);
	if (Result != XST_SUCCESS) {
		/* Retry state state A2 */
		return XHDCP22_TX_STATE_A2;
	}
	if (InstancePtr->Info.MsgAvailable == (FALSE)) {
		return XHDCP22_TX_STATE_A2_1;
	}

	/* Log after waiting (don't clutter our log buffer, check on counter) */
	if (InstancePtr->Info.LocalityCheckCounter == 1) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE,
		                (u16)XHDCP22_TX_STATE_A2_1);
	}

	/* Receive the expected message */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_RX_L1);
	Result = XHdcp22Tx_ReceiveMsg(InstancePtr, XHDCP22_TX_LC_SEND_L_PRIME,
	                              sizeof(XHdcp22_Tx_LCSendLPrime));
	if (Result != XST_SUCCESS) {
		/* Retry state state A2 */
		return XHDCP22_TX_STATE_A2;
	}

	/* Verify the received L' */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	XHDCP22_TX_LOG_DBG_COMPUTE_L);
	XHdcp22Tx_ComputeLPrime(InstancePtr->Info.Rn, PairingInfoPtr->Km,
	                        InstancePtr->Info.Rrx, InstancePtr->Info.Rtx,
	                        LPrime);
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
		XHDCP22_TX_LOG_DBG_COMPUTE_L_DONE);

	if(memcmp(MsgPtr->Message.LCSendLPrime.LPrime, LPrime, sizeof(LPrime)) != 0) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
			XHDCP22_TX_LOG_DBG_COMPARE_L_FAIL);
		/* Retry state A2 */
		return XHDCP22_TX_STATE_A2;
	}

	/* Log how many times the locality check was repeated (*/
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_LCCHK_COUNT,
	                (u16)InstancePtr->Info.LocalityCheckCounter);
	return XHDCP22_TX_STATE_A3;
}

/*****************************************************************************/
/**
*
* This function executes the A3 state of the HDCP2.2 TX protocol.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return The next state to execute.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA3(XHdcp22_Tx *InstancePtr)
{
	int Result;
	u8 Riv[XHDCP22_TX_RIV_SIZE];
	u8 Ks[XHDCP22_TX_KS_SIZE];
	u8 EdkeyKs[XHDCP22_TX_EDKEY_KS_SIZE];
	XHdcp22_Tx_PairingInfo *PairingInfoPtr =
	                       (XHdcp22_Tx_PairingInfo *)InstancePtr->Info.StateContext;

	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE,
	                (u16)XHDCP22_TX_STATE_A3);


	XHdcp22Tx_GenerateRiv(InstancePtr, Riv);

	/* Set Riv in the cipher */
	XHdcp22Cipher_SetRiv(&InstancePtr->Cipher, Riv, XHDCP22_TX_RIV_SIZE);
	XHdcp22Tx_GenerateKs(InstancePtr, Ks);

	/* Set Ks in the cipher */
	XHdcp22Cipher_SetKs(&InstancePtr->Cipher, Ks, XHDCP22_TX_KS_SIZE);
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	                XHDCP22_TX_LOG_DBG_COMPUTE_EDKEYKS);
	XHdcp22Tx_ComputeEdkeyKs(InstancePtr->Info.Rn, PairingInfoPtr->Km, Ks,
	                         InstancePtr->Info.Rrx, InstancePtr->Info.Rtx, EdkeyKs);
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	                XHDCP22_TX_LOG_DBG_COMPUTE_EDKEYKS_DONE);

	/* Write encrypted sesssion key */
	Result = XHdcp22Tx_WriteSkeSendEks(InstancePtr, EdkeyKs, Riv);

	if (Result != XST_SUCCESS) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_MSG_WRITE_FAIL);
		return XHDCP22_TX_STATE_A0;
	}

	return XHDCP22_TX_STATE_A4;
}

/*****************************************************************************/
/**
*
* This function executes the A4 state of the HDCP2.2 TX protocol.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return The next state to execute.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA4(XHdcp22_Tx *InstancePtr)
{
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE,
	                (u16)XHDCP22_TX_STATE_A4);

	if (InstancePtr->Info.IsReceiverRepeater) {

		return XHDCP22_TX_STATE_A6_A7_A8;
	}
	else {
		/* Start the mandatory 200 ms timer before authentication can be granted
		 * and the cipher may be enabled */
		XHdcp22Tx_StartTimer(InstancePtr, 200, XHDCP22_TX_TS_WAIT_FOR_CIPHER);

		/* The downstream topology is definitive  */
		InstancePtr->Info.IsTopologyAvailable = (TRUE);
		if (InstancePtr->IsDownstreamTopologyAvailableCallbackSet)
			InstancePtr->DownstreamTopologyAvailableCallback(InstancePtr->DownstreamTopologyAvailableCallbackRef);

		return XHDCP22_TX_STATE_A5;
	}
}

/*****************************************************************************/
/**
*
* This function executes the A5 state of the HDCP2.2 TX protocol.
* Before authentication is granted, a 200 ms mandatory wait is added, so the
* integrating application does not start the cipher premature.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return The next state to execute.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA5(XHdcp22_Tx *InstancePtr)
{

#ifndef _XHDCP22_TX_DISABLE_TIMEOUT_CHECKING_
	/* wait for a timer to expire, either it is the 200 ms mandatory time
	 * before cipher enabling, or the re-authentication check timer */
	if (InstancePtr->Timer.TimerExpired == (FALSE)) {
		return XHDCP22_TX_STATE_A5;
	}
#endif

	/* Do not pollute the logging on polling log authenticated only once */
	if (InstancePtr->Info.AuthenticationStatus != XHDCP22_TX_AUTHENTICATED) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE,
		                (u16)XHDCP22_TX_STATE_A5);
	}

	/* Timer has expired, handle it */

	/* Handle mandatory 200 ms cipher timeout */
	if (InstancePtr->Timer.ReasonId == XHDCP22_TX_TS_WAIT_FOR_CIPHER) {
		/* Check re-authentication before enabling cipher */
		if ((InstancePtr->Info.RxStatus&XHDCP22_TX_RXSTATUS_REAUTH_REQ_MASK) ==
		    XHDCP22_TX_RXSTATUS_REAUTH_REQ_MASK)
		{
			XHdcp22Tx_HandleReauthenticationRequest(InstancePtr);
			return XHDCP22_TX_STATE_A0;
		}
		else
		{
			/* Authenticated ! */
			InstancePtr->Info.AuthenticationStatus = XHDCP22_TX_AUTHENTICATED;
			InstancePtr->Info.ReAuthenticationRequested = (FALSE);

			/* Authenticated callback */
			if (InstancePtr->IsAuthenticatedCallbackSet) {
				InstancePtr->AuthenticatedCallback(InstancePtr->AuthenticatedCallbackRef);
			}

			/* Start the re-authentication check timer */
			XHdcp22Tx_StartTimer(InstancePtr, 1000, XHDCP22_TX_TS_RX_REAUTH_CHECK);
			return XHDCP22_TX_STATE_A5;
		}
	}

	/* Handle the re-authentication check timer */
	if (InstancePtr->Timer.ReasonId == XHDCP22_TX_TS_RX_REAUTH_CHECK) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
		                XHDCP22_TX_LOG_DBG_CHECK_REAUTH);

		if ((InstancePtr->Info.RxStatus&XHDCP22_TX_RXSTATUS_REAUTH_REQ_MASK) ==
		    XHDCP22_TX_RXSTATUS_REAUTH_REQ_MASK)
		{
			XHdcp22Tx_HandleReauthenticationRequest(InstancePtr);
			return XHDCP22_TX_STATE_A0;
		}
		/* Has the repeater built an updated downstream receiver ID list? */
		if ((InstancePtr->Info.RxStatus&XHDCP22_TX_RXSTATUS_READY_MASK) ==
			XHDCP22_TX_RXSTATUS_READY_MASK) {

			/* The downstream topology has changed */
			return XHDCP22_TX_STATE_A6_A7_A8;
		}

		/* Re-Start the timer for the next status check */
		XHdcp22Tx_StartTimer(InstancePtr, 1000, XHDCP22_TX_TS_RX_REAUTH_CHECK);
	}
	return XHDCP22_TX_STATE_A5;
}

/*****************************************************************************/
/**
*
* This function executes the states A6, A7 and A8 of the HDCP2.2 TX protocol.
* Within these states the HDCP2.2 TX receives and verifies the receiver ID list.
* The HDCP2.2 TX is required to send a respons within 2 seconds after the
* HDCP2.2 repeater has made the receiver ID list available.
* These states have been grouped in order to guarantee that the HDCP2.2 TX
* responds as fast as it can.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return The next state to execute.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA6_A7_A8(XHdcp22_Tx *InstancePtr)
{
	int Result;
	u8 DeviceCount;
	u8 V[XHDCP22_TX_V_SIZE];
	u32 SeqNum_V;
	int i;

	/* When we (re)-enter this state the topology info is not available
	 * so clear the topology available flag */
	InstancePtr->Info.IsTopologyAvailable = (FALSE);

	XHdcp22_Tx_PairingInfo *PairingInfoPtr =
		(XHdcp22_Tx_PairingInfo *)InstancePtr->Info.StateContext;
	XHdcp22_Tx_DDCMessage *MsgPtr =
		(XHdcp22_Tx_DDCMessage *)InstancePtr->MessageBuffer;
	/* Wait for the receiver to respond within 3 secs.
	* If the receiver has timed out we go to state A0.
	* If the receiver is busy, we stay in this state (return from polling).
	* If the receiver has finished, but the message was not handled yet,
	* we handle the message.*/
	Result = XHdcp22Tx_WaitForReceiver(InstancePtr, 0, TRUE);
	if (Result != XST_SUCCESS) {
		return XHDCP22_TX_STATE_A0;
	}
	if (InstancePtr->Info.MsgAvailable == (FALSE)) {
		return XHDCP22_TX_STATE_A6_A7_A8;
	}

	/* Log after waiting */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE, (u16)XHDCP22_TX_STATE_A6_A7_A8);

	/* Receive the expected message */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_RX_RCVIDLIST);
	Result = XHdcp22Tx_ReceiveMsg(InstancePtr, XHDCP22_TX_REPEATAUTH_SEND_RECVID_LIST,
		InstancePtr->Info.RxStatus & XHDCP22_TX_RXSTATUS_AVAIL_BYTES_MASK);
	if (Result != XST_SUCCESS) {
		/* Received message is invalid. Go to state A0 */
		return XHDCP22_TX_STATE_A0;
	}

	/* Extract the RxInfo and set the topology info.
	 * RxInfo is in big endian format */
	DeviceCount = (MsgPtr->Message.RepeatAuthSendRecvIDList.RxInfo[0] & 0x1) << 4;  // MSB
	DeviceCount |= (MsgPtr->Message.RepeatAuthSendRecvIDList.RxInfo[1] >> 4) & 0xF; // LSB

	/* The device count extracted from RxInfo does not include
	 * the HDCP repeater itself, hence the +1 */
	InstancePtr->Topology.DeviceCnt = DeviceCount + 1;
	InstancePtr->Topology.Depth =
		(MsgPtr->Message.RepeatAuthSendRecvIDList.RxInfo[0] >> 1) & 0x7;
	InstancePtr->Topology.MaxDevsExceeded =
		(MsgPtr->Message.RepeatAuthSendRecvIDList.RxInfo[1] & 0x8) ? TRUE : FALSE;
	InstancePtr->Topology.MaxCascadeExceeded =
		(MsgPtr->Message.RepeatAuthSendRecvIDList.RxInfo[1] & 0x4) ? TRUE : FALSE;
	InstancePtr->Topology.Hdcp20RepeaterDownstream =
		(MsgPtr->Message.RepeatAuthSendRecvIDList.RxInfo[1] & 0x2) ? TRUE : FALSE;
	InstancePtr->Topology.Hdcp1DeviceDownstream =
		(MsgPtr->Message.RepeatAuthSendRecvIDList.RxInfo[1] & 0x1) ? TRUE : FALSE;

	/* State A7: Verify Receiver ID list */

	/* Check the topology */
	if (InstancePtr->Topology.MaxDevsExceeded == TRUE ||
	    InstancePtr->Topology.MaxCascadeExceeded == TRUE) {
		InstancePtr->Info.IsTopologyAvailable = (TRUE);
		if (InstancePtr->IsDownstreamTopologyAvailableCallbackSet)
			InstancePtr->DownstreamTopologyAvailableCallback(InstancePtr->DownstreamTopologyAvailableCallbackRef);
		/* Topology error. Go to state A0 */
		return XHDCP22_TX_STATE_A0;
	}

	/* Verify the received VPrime */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_COMPUTE_V);
	XHdcp22Tx_ComputeV(InstancePtr->Info.Rn, InstancePtr->Info.Rrx,
		MsgPtr->Message.RepeatAuthSendRecvIDList.RxInfo,
		InstancePtr->Info.Rtx,
		(u8 *)MsgPtr->Message.RepeatAuthSendRecvIDList.ReceiverIDs,
		DeviceCount,
		MsgPtr->Message.RepeatAuthSendRecvIDList.SeqNum_V,
		PairingInfoPtr->Km,
		V);
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_COMPUTE_V_DONE);

	/* Compare VPrime with the most significant 128-bits of V */
	if (memcmp(MsgPtr->Message.RepeatAuthSendRecvIDList.VPrime, V, XHDCP22_TX_V_PRIME_SIZE) != 0) {
		/* Missmatch V MSB and VPrime. Go to state A0 */
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_COMPARE_V_FAIL);
		return XHDCP22_TX_STATE_A0;
	}

	InstancePtr->Info.IsDeviceRevoked = FALSE;
	for (i = 0; i < DeviceCount; i++) {
		/* Add receiver ID to topology info. */
		memcpy(&InstancePtr->Topology.ReceiverId[i + 1],
			&MsgPtr->Message.RepeatAuthSendRecvIDList.ReceiverIDs[i],
			XHDCP22_TX_RCVID_SIZE);

		/* SRM and revocation check are only performed by the top-level HDCP transmitter */
		if (InstancePtr->Config.Mode == XHDCP22_TX_TRANSMITTER) {
			/* Check to see wether the receiver ID is revoked */
			if (XHdcp22Tx_IsDeviceRevoked(InstancePtr,
				MsgPtr->Message.RepeatAuthSendRecvIDList.ReceiverIDs[i])) {
				InstancePtr->Info.IsDeviceRevoked = TRUE;
				InstancePtr->Info.AuthenticationStatus = XHDCP22_TX_DEVICE_IS_REVOKED;
				/* Device is revoked. Go to state A0 */
				return XHDCP22_TX_STATE_A0;
			}
		}
	}

	/* Get the seq_num_V. Value is in big endian format */
	SeqNum_V  = MsgPtr->Message.RepeatAuthSendRecvIDList.SeqNum_V[0] << 16; // MSB
	SeqNum_V |= MsgPtr->Message.RepeatAuthSendRecvIDList.SeqNum_V[1] << 8;
	SeqNum_V |= MsgPtr->Message.RepeatAuthSendRecvIDList.SeqNum_V[2];       // LSB

	/* Verify the seq_num_V value */
	if (InstancePtr->Info.ReceivedFirstSeqNum_V == FALSE) {
		if (SeqNum_V != 0) {
			/* First value should be 0. Go to state A0 */
			return XHDCP22_TX_STATE_A0;
		}
		InstancePtr->Info.ReceivedFirstSeqNum_V = TRUE;
	}
	else {
		/* Check for roll-over of seq_num_V */
		if (SeqNum_V == 0) {
			/* Roll-over of seq_num_V. Go to state A0 */
			return XHDCP22_TX_STATE_A0;
		}
	}

	/* State A8: Send Receiver ID list acknowledgement */
	Result = XHdcp22Tx_WriteRepeaterAuth_Send_Ack(InstancePtr, &V[XHDCP22_TX_V_PRIME_SIZE]);

	if (Result != XST_SUCCESS) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_MSG_WRITE_FAIL);
		return XHDCP22_TX_STATE_A0;
	}

	/* The downstream topology is definitive  */
	InstancePtr->Info.IsTopologyAvailable = (TRUE);
	if (InstancePtr->IsDownstreamTopologyAvailableCallbackSet)
		InstancePtr->DownstreamTopologyAvailableCallback(InstancePtr->DownstreamTopologyAvailableCallbackRef);

	/* Have we already sent the Content Stream Type? */
	if (InstancePtr->Info.IsContentStreamTypeSent == TRUE) {
		/* Start the re-authentication check timer */
		XHdcp22Tx_StartTimer(InstancePtr, 1000, XHDCP22_TX_TS_RX_REAUTH_CHECK);

		/* Go to state A5 */
		return XHDCP22_TX_STATE_A5;
	}
	else {
		/* Go to state A9 */
		return XHDCP22_TX_STATE_A9;
	}
}


/*****************************************************************************/
/**
*
* This function executes the A6 state of the HDCP2.2 TX protocol.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return The next state to execute.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA6(XHdcp22_Tx *InstancePtr)
{
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE,
		(u16)XHDCP22_TX_STATE_A6);
	return XHDCP22_TX_STATE_A0;
}

/*****************************************************************************/
/**
*
* This function executes the A7 state of the HDCP2.2 TX protocol.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return The next state to execute.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA7(XHdcp22_Tx *InstancePtr)
{
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE,
	                (u16)XHDCP22_TX_STATE_A7);
	return XHDCP22_TX_STATE_A0;
}

/*****************************************************************************/
/**
*
* This function executes the A8 state of the HDCP2.2 TX protocol.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return The next state to execute.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA8(XHdcp22_Tx *InstancePtr)
{
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE,
	                (u16)XHDCP22_TX_STATE_A8);
	return XHDCP22_TX_STATE_A0;
}

/*****************************************************************************/
/**
*
* This function executes the first portion of the A9 state of the HDCP2.2 TX protocol.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return The next state to execute.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA9(XHdcp22_Tx *InstancePtr)
{
	int Result;

#ifndef _XHDCP22_TX_DISABLE_TIMEOUT_CHECKING_
	/* Wait for the stream manage timer to expire */
	if (InstancePtr->Timer.TimerExpired == (FALSE)) {
		return XHDCP22_TX_STATE_A9;
	}
#endif

	/* Timer has expired, handle it */

	/* Check re-authentication */
	XHdcp22Tx_ReadRxStatus(InstancePtr);
	if ((InstancePtr->Info.RxStatus&XHDCP22_TX_RXSTATUS_REAUTH_REQ_MASK) ==
		XHDCP22_TX_RXSTATUS_REAUTH_REQ_MASK) {
		XHdcp22Tx_HandleReauthenticationRequest(InstancePtr);
		return XHDCP22_TX_STATE_A0;
	}

	/* Check if the Content Stream Type is available.
	 * If not set, we stay in this state (return from polling) and set a timer */
	if (InstancePtr->Info.IsContentStreamTypeSet == FALSE) {
		/* Start the wait for stream type timer */
		XHdcp22Tx_StartTimer(InstancePtr, 50, XHDCP22_TX_TS_WAIT_FOR_STREAM_TYPE);
		return XHDCP22_TX_STATE_A9;
	}

	/* Log but don't clutter our log buffer, check on counter */
	if (InstancePtr->Info.ContentStreamManageCheckCounter == 0) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE,
		                (u16)XHDCP22_TX_STATE_A9);
	}

	if (InstancePtr->Info.ContentStreamManageCheckCounter >= XHDCP22_TX_MAX_ALLOWED_STREAM_MANAGE_CHECKS) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STRMMNGCHK_COUNT,
		                (u16)InstancePtr->Info.ContentStreamManageCheckCounter);
		return XHDCP22_TX_STATE_A0;
	}

	/* Check for roll-over of seq_num_M */
	if ((InstancePtr->Info.SentFirstSeqNum_M == TRUE) &&
		(InstancePtr->Info.SeqNum_M == 0)) {
		/* Roll-over detected. According to the state-diagram
		 * we should go to state H1 and restart authentication.
		 * Instead of doing that we restart authentication by going to state A0 */
		return XHDCP22_TX_STATE_A0;
	}

	/* Send the Content Stream Manage message */
	Result = XHdcp22Tx_WriteRepeaterAuth_Stream_Manage(InstancePtr);

	if (Result != XST_SUCCESS) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_MSG_WRITE_FAIL);
		return XHDCP22_TX_STATE_A0;
	}

	/* Start the timer for receiving XHDCP22_TX_REPEATAUTH_STREAM_READY */
	XHdcp22Tx_StartTimer(InstancePtr, 100, XHDCP22_TX_REPEATAUTH_STREAM_READY);

	InstancePtr->Info.SentFirstSeqNum_M = TRUE;
	InstancePtr->Info.ContentStreamManageCheckCounter++;

	return XHDCP22_TX_STATE_A9_1;
}

/*****************************************************************************/
/**
*
* This function executes the second portion of the A9 state of the HDCP2.2 TX protocol.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return The next state to execute.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_StateType XHdcp22Tx_StateA9_1(XHdcp22_Tx *InstancePtr)
{
	int Result;

	XHdcp22_Tx_DDCMessage *MsgPtr =
		(XHdcp22_Tx_DDCMessage *)InstancePtr->MessageBuffer;

	/* Wait for the receiver to respond within 100 msecs.
	* If the receiver has timed out we go to state A9 for a retry.
	* If the receiver is busy, we stay in this state (return from polling).
	* If the receiver has finished, but the message was not handled yet,
	* we handle the message.*/
	Result = XHdcp22Tx_WaitForReceiver(InstancePtr, XHDCP22_TX_REPEATAUTH_STREAM_READY_SIZE, FALSE);
	if (Result != XST_SUCCESS) {
		/* Timeout. Go to state A9 for a retry */
		InstancePtr->Info.ContentStreamManageFailed = TRUE;
		return XHDCP22_TX_STATE_A9;
	}
	if (InstancePtr->Info.MsgAvailable == (FALSE)) {
		return XHDCP22_TX_STATE_A9_1;
	}

	/* Log after waiting (don't clutter our log buffer, check on counter) */
	if (InstancePtr->Info.ContentStreamManageCheckCounter == 1) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_STATE,
		                (u16)XHDCP22_TX_STATE_A9_1);
	}

	/* Receive the expected message */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_RX_M1);
	Result = XHdcp22Tx_ReceiveMsg(InstancePtr, XHDCP22_TX_REPEATAUTH_STREAM_READY,
		                                            XHDCP22_TX_REPEATAUTH_STREAM_READY_SIZE);
	if (Result != XST_SUCCESS) {
		/* Received message is invalid. Go to state A9 for a retry */
		InstancePtr->Info.ContentStreamManageFailed = TRUE;
		return XHDCP22_TX_STATE_A9;
	}

	/* Verify the received MPrime */
	/* We've allready calculated and stored the expected value in the
	 * XHdcp22Tx_WriteRepeaterAuth_Stream_Manage function */
	/* Compare MPrime with M */
	if (memcmp(MsgPtr->Message.RepeatAuthStreamReady.MPrime, InstancePtr->Info.M,
		XHDCP22_TX_M_PRIME_SIZE) != 0) {
		/* Missmatch M and MPrime. Go to state A9 for a retry */
		InstancePtr->Info.ContentStreamManageFailed = TRUE;
		return XHDCP22_TX_STATE_A9;
	}

	/* According to the spec, the HDCP transmitter must write the
	 * RepeaterAuth_Stream_Manage message at least 100ms before the
	 * transmission of the corresponding Content Stream. Just to make
	 * sure the wait time is met, wait for 100ms. */
	XHdcp22Tx_StartTimer(InstancePtr, 100, XHDCP22_TX_TS_WAIT_FOR_CIPHER);

	InstancePtr->Info.ContentStreamManageFailed = FALSE;
	InstancePtr->Info.IsContentStreamTypeSent = TRUE;
	return XHDCP22_TX_STATE_A5;
}

/*****************************************************************************/
/**
*
* This function executes on transition from state A1 or one of its sub states
* to A0, which means that an error in authentication has occured.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_A1A0(XHdcp22_Tx *InstancePtr)
{
	XHdcp22Tx_HandleAuthenticationFailed(InstancePtr);
}

/*****************************************************************************/
/**
*
* This function executes on transition from state A1_SK0 or A1_NSK1 to A2,
* which means that state A2 is executed for the first time.
* The locality check counter is initialized.
* Subsequent locality checks are then executed for an extra 1023 times.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_A1A2(XHdcp22_Tx *InstancePtr)
{
	InstancePtr->Info.LocalityCheckCounter = 0;
}

/*****************************************************************************/
/**
*
* This function executes on transition from state A2 or one of its substates
* to A0, which means that an error in authentication has occured.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_A2A0(XHdcp22_Tx *InstancePtr)
{
	XHdcp22Tx_HandleAuthenticationFailed(InstancePtr);
}

/*****************************************************************************/
/**
*
* This function executes on transition from state A3 to A0, which means that
* an error in authetication has occured.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_A3A0(XHdcp22_Tx *InstancePtr)
{
	XHdcp22Tx_HandleAuthenticationFailed(InstancePtr);
}

/*****************************************************************************/
/**
*
* This function executes on transition from state A3 to A4, which means that
* the Session Key Exchange (SKE) has completed.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_A3A4(XHdcp22_Tx *InstancePtr)
{
	/* Check to see wether the Receiver is an HDCP Repeater */
	if (InstancePtr->Info.IsReceiverRepeater) {
		/* Start timer for receiving the Receiver ID list */
		XHdcp22Tx_StartTimer(InstancePtr, 3000, XHDCP22_TX_REPEATAUTH_SEND_RECVID_LIST);
	}
}

/*****************************************************************************/
/**
*
* This function executes on transition from state A4 to A5, which means that
* an authetication has succeeded and state A5 is executed the first time.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_A4A5(XHdcp22_Tx *InstancePtr)
{
	Xil_AssertVoid(InstancePtr != NULL);
}

/*****************************************************************************/
/**
*
* This function executes on transition from state A6 or A7 to A0, which means that
* an error in authetication has occured.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_A6A7A0(XHdcp22_Tx *InstancePtr)
{
	XHdcp22Tx_HandleAuthenticationFailed(InstancePtr);
}

/*****************************************************************************/
/**
*
* This function executes on transition from state A9 to A0, which means that
* an error in the content stream management has occured.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_A9A0(XHdcp22_Tx *InstancePtr)
{
	XHdcp22Tx_HandleAuthenticationFailed(InstancePtr);
}

/*****************************************************************************/
/**
*
* This function is a dummy called as long as there is no valid ddc handler set.
* It always returns an error.
*
* @param  DeviceAddress is the DDC (i2c) device address.
* @param  ByteCount is the number of bytes in the buffer.
* @param  BufferPtr is the buffer that stores the data to be written or read.
* @param  Stop is a flag to signal i2c stop condition.
* @param  RefPtr is the reference pointer for the handler.
*
* @return XST_FAILURE
*
* @note   None.
*
******************************************************************************/
static int XHdcp22Tx_StubDdc(u8 DeviceAddress, u16 ByteCount, u8* BufferPtr,
                             u8 Stop, void* RefPtr)
{
	Xil_AssertNonvoidAlways();
	Xil_AssertNonvoid(DeviceAddress);
	Xil_AssertNonvoid(ByteCount);
	Xil_AssertNonvoid(BufferPtr != NULL);
	Xil_AssertNonvoid(Stop);
	Xil_AssertNonvoid(RefPtr != NULL);

	return XST_FAILURE;
}

/*****************************************************************************/
/**
*
* This function is a dummy called as long as there is no valid
* authenticated handler set. It always returns an error.
*
* @param  RefPtr is the reference pointer for the handler.
*
* @return XST_FAILURE
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_StubCallback(void* RefPtr)
{
	Xil_AssertVoidAlways();
	Xil_AssertVoid(RefPtr != NULL);
}

/*****************************************************************************/
/**
*
* This function returns a pseudo random 64-bit value for Rtx as part of the
* HDCP22 TX initiation message. If appropriate it is overwritten with a test
* vector.
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  RtxPtr is a 64-bit random value;
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_GenerateRtx(XHdcp22_Tx *InstancePtr, u8* RtxPtr)
{
	Xil_AssertVoid(InstancePtr != NULL);

	/* get a 64 bits random number */
	XHdcp22Tx_GenerateRandom(InstancePtr, XHDCP22_TX_RTX_SIZE, RtxPtr);

#ifdef _XHDCP22_TX_TEST_
	/* Let the test module decide if it should be overwritten with a test vector */
	XHdcp22Tx_TestGenerateRtx(InstancePtr, RtxPtr);
#endif
}

/*****************************************************************************/
/**
*
* This function returns a pseudo random 128-bit value for Km as part of the HDCP22 TX
* initiation message. If appropriate it is overwritten with a test vector.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  KmPtr is a pointer to a 128 bits random value.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_GenerateKm(XHdcp22_Tx *InstancePtr, u8* KmPtr)
{
	Xil_AssertVoid(InstancePtr != NULL);

	/* get a 128 bits random number */
	XHdcp22Tx_GenerateRandom(InstancePtr, XHDCP22_TX_KM_SIZE, KmPtr);

#ifdef _XHDCP22_TX_TEST_
	/* Let the test module decide if it should be overwritten with a test vector */
	XHdcp22Tx_TestGenerateKm(InstancePtr, KmPtr);
#endif
}

/*****************************************************************************/
/**
*
* This function returns a pseudo random 64-bit value for Rn as part
* of the locality check.
* If appropriate it is overwritten with a test vector.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  KmPtr is a pointer to a 256 bits random seed value.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_GenerateKmMaskingSeed(XHdcp22_Tx *InstancePtr,
                                             u8* SeedPtr)
{
	Xil_AssertVoid(InstancePtr != NULL);

	/* get a 128 bits random number */
	XHdcp22Tx_GenerateRandom(InstancePtr, XHDCP22_TX_KM_MSK_SEED_SIZE, SeedPtr);

#ifdef _XHDCP22_TX_TEST_
	/* Let the test module decide if it should be overwritten with a test vector */
	XHdcp22Tx_TestGenerateKmMaskingSeed(InstancePtr, SeedPtr);
#endif
}

/*****************************************************************************/
/**
*
* This function returns a pseudo random 64-bit value for Rn as part of the
* HDCP22 TX locality check. If appropriate it is overwritten with a test vector.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  RnPtr is a pointer to a 64 bits random value.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_GenerateRn(XHdcp22_Tx *InstancePtr, u8* RnPtr)
{
	Xil_AssertVoid(InstancePtr != NULL);

	/* get a 128 bits random number */
	XHdcp22Tx_GenerateRandom(InstancePtr, XHDCP22_TX_RN_SIZE, RnPtr);

#ifdef _XHDCP22_TX_TEST_
	/* Let the test module decide if it should be overwritten with a test vector */
	XHdcp22Tx_TestGenerateRn(InstancePtr, RnPtr);
#endif
}

/*****************************************************************************/
/**
*
* This function returns a pseudo random 128-bit value for Ks as part of the HDCP22 TX
* session key exchange message. If appropriate it is overwritten with a test vector.
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  KsPtr is a 128-bit random value;
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_GenerateKs(XHdcp22_Tx *InstancePtr, u8* KsPtr)
{
	Xil_AssertVoid(InstancePtr != NULL);

	/* get a 64 bits random number */
	XHdcp22Tx_GenerateRandom(InstancePtr, XHDCP22_TX_KS_SIZE, KsPtr);

#ifdef _XHDCP22_TX_TEST_
	/* Let the test module decide if it should be overwritten with a test vector */
	XHdcp22Tx_TestGenerateKs(InstancePtr, KsPtr);
#endif
}

/*****************************************************************************/
/**
*
* This function returns a pseudo random 64-bit value for Riv as part of the HDCP22 TX
* session key exchange message. If appropriate it is overwritten with a test vector.
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  RivPtr is a 64-bit random value;
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_GenerateRiv(XHdcp22_Tx *InstancePtr, u8* RivPtr)
{
	Xil_AssertVoid(InstancePtr != NULL);

	/* get a 64 bits random number */
	XHdcp22Tx_GenerateRandom(InstancePtr, XHDCP22_TX_RIV_SIZE, RivPtr);

#ifdef _XHDCP22_TX_TEST_
	/* Let the test module decide if it should be overwritten with a test vector */
	XHdcp22Tx_TestGenerateRiv(InstancePtr, RivPtr);
#endif
}

/*****************************************************************************/
/**
*
* This function returns the DCP LLC public key
* If appropriate it returns a test vector.
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  RivPtr is a 64-bit random value;
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static const u8* XHdcp22Tx_GetKPubDpc(XHdcp22_Tx *InstancePtr)
{
	const u8* KPubDpcPtr = NULL;

	Xil_AssertNonvoid(InstancePtr != NULL);

#ifdef _XHDCP22_TX_TEST_
	/* Let the test module decide if it should be overwritten with a test vector */
	KPubDpcPtr = XHdcp22Tx_TestGetKPubDpc(InstancePtr);
#endif

	if (KPubDpcPtr == NULL) {
		KPubDpcPtr = XHdcp22_Tx_KpubDcp;
	}

	return KPubDpcPtr;
}

/*****************************************************************************/
/**
*
* This function starts the timer needed for checking the HDCP22 Receive status
* register. In case the timer is started to receive a message,
* the MsgAvailable flag is reset.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  TimeOut_mSec is the timeout for the next status update.
*
* @return XST_SUCCESS or XST_FAILURE.
*
* @note   None.
*
******************************************************************************/
static int XHdcp22Tx_StartTimer(XHdcp22_Tx *InstancePtr, u32 TimeOut_mSec,
                                u8 ReasonId)
{
	u32 Ticks = (u32)(InstancePtr->Timer.TmrCtr.Config.SysClockFreqHz / 1000000) * TimeOut_mSec * 1000;

	Xil_AssertNonvoid(InstancePtr != NULL);

	InstancePtr->Timer.TimerExpired = (FALSE);
	InstancePtr->Timer.ReasonId = ReasonId;
	InstancePtr->Timer.InitialTicks = Ticks;

	/* If the timer was started for receiving a message,
	 * the message available flag must be reset */
	if (ReasonId != XHDCP22_TX_TS_UNDEFINED &&
	    ReasonId != XHDCP22_TX_TS_RX_REAUTH_CHECK &&
	    ReasonId != XHDCP22_TX_TS_WAIT_FOR_CIPHER) {
		InstancePtr->Info.MsgAvailable = (FALSE);
	}

#ifndef _XHDCP22_TX_DISABLE_TIMEOUT_CHECKING_
#ifdef _XHDCP22_TX_TEST_
	if (InstancePtr->Test.TestMode == XHDCP22_TX_TESTMODE_UNIT) {
		XHdcp22Tx_TimerHandler(InstancePtr, XHDCP22_TX_TIMER_CNTR_0);
	}
#else
	if (InstancePtr->Timer.TmrCtr.IsReady == (FALSE)) {
		return XST_FAILURE;
	}

	XTmrCtr_SetResetValue(&InstancePtr->Timer.TmrCtr, XHDCP22_TX_TIMER_CNTR_0, Ticks);
	XTmrCtr_Start(&InstancePtr->Timer.TmrCtr, XHDCP22_TX_TIMER_CNTR_0);

	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	                XHDCP22_TX_LOG_DBG_STARTIMER);
#endif /* _XHDCP22_TX_TEST_ */
#endif /* _XHDCP22_TX_DISABLE_TIMEOUT_CHECKING_ */

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function returns the current timer count value of the expiration timer.
* register.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  TimeOut_mSec is the timeout for the next status update.
*
* @return
*         - XST_SUCCESS if the timer count could be acquired.
*         - XST_FAILURE if not.
*
* @note None.
*
******************************************************************************/
static u32 XHdcp22Tx_GetTimerCount(XHdcp22_Tx *InstancePtr)
{
	return XTmrCtr_GetValue(&InstancePtr->Timer.TmrCtr, XHDCP22_TX_TIMER_CNTR_0);
}

/*****************************************************************************/
/**
*
* This function handles timer interrupts
*
* @param  CallbackRef refers to a XHdcp22_Tx structure
* @param  TmrCntNumber the number of used timer.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_TimerHandler(void *CallbackRef, u8 TmrCntNumber)
{
	XHdcp22_Tx *InstancePtr = (XHdcp22_Tx *)CallbackRef;

	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

	if (TmrCntNumber == XHDCP22_TX_TIMER_CNTR_1) {
		return;
	}

	/* Set timer expired signaling flag */
	InstancePtr->Timer.TimerExpired = (TRUE);

	if (InstancePtr->Info.IsEnabled)
		XHdcp22Tx_ReadRxStatus(InstancePtr);
}


/*****************************************************************************/
/**
*
* This function can be used to change the polling value. The polling value
* is the amount of time in milliseconds to wait between successive reads
* of the RxStatus register. The RxStatus register is polled to determine
* when a message is available for reading during authentication or during
* the link integrity check phase to determine when to issue re-authentication.
* The polling value needs to be at most 20ms to account for the locality
* check.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  PollingValue is the polling interval defined in milliseconds
*         - 0 : Always Poll
*         - 1 : Poll after 1ms
*         - 2 : Poll after 2ms
*         - 3 : Poll after 3ms etc...
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XHdcp22Tx_SetMessagePollingValue(XHdcp22_Tx *InstancePtr, u32 PollingValue)
{
	InstancePtr->Info.PollingValue = PollingValue;
}

/*****************************************************************************/
/**
*
* This function waits for receiving expected messages from the receiver.
* If the timer is not started, it will be started.
* This has to be used to avoid blocking waits, and allows polling to return
* to allow the main thread to continue handling other requests.
* Some receivers require to read status as soon as possible otherwise the
* receiver may request for a re-authentication so we must poll!
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  ExpectedSize indicates the expected message size in bytes.
* @param  ReadyBit indicates if the repeater ready bit from RxStatus register
*         should be used or the specified ExpectedSize.
*         - TRUE Ready bit from RxStatus is used. ExpectedSize will be ignored.
*         - FALSE ExpectedSize is used. Ready bit from RxStatus will be ignored.
*
* @return
*         - XST_SUCCESS if no problems
*         - XST_FAILURE if the receiver has timed out
*
* @note   None.
*
******************************************************************************/
static int XHdcp22Tx_WaitForReceiver(XHdcp22_Tx *InstancePtr, int ExpectedSize, u8 ReadyBit)
{
	/*
	 * Timer is counting down.
	 * The interval count is the number of clock ticks for a polling interval.
	 * The RxStatus register is read each time the difference between the
	 * previous poll count and the current timer count is greater than
	 * or equal to the interval count.
	 */
	u32 TimerCount = 0;
	u32 IntervalCount = InstancePtr->Info.PollingValue *
		((u32)InstancePtr->Timer.TmrCtr.Config.SysClockFreqHz / 1000);

#ifdef _XHDCP22_TX_TEST_
	if (XHdcp22Tx_TestSimulateTimeout(InstancePtr) == TRUE) {
		return XST_FAILURE;
	}
#endif

#ifdef _XHDCP22_TX_TEST_
	/* If the timeout flag is disabled, we disable the timer and keep on polling */
	if ((InstancePtr->Test.TestFlags & XHDCP22_TX_TEST_NO_TIMEOUT) == XHDCP22_TX_TEST_NO_TIMEOUT) {
		if (InstancePtr->Timer.TmrCtr.IsStartedTmrCtr0) {
			XTmrCtr_Stop(&InstancePtr->Timer.TmrCtr, XHDCP22_TX_TIMER_CNTR_0);
		}
		XHdcp22Tx_ReadRxStatus(InstancePtr);
		InstancePtr->Timer.TimerExpired = (FALSE);
		if (((ReadyBit == FALSE) && ((InstancePtr->Info.RxStatus & XHDCP22_TX_RXSTATUS_AVAIL_BYTES_MASK) == ExpectedSize)) ||
			((ReadyBit == TRUE)  &&  (InstancePtr->Info.RxStatus & XHDCP22_TX_RXSTATUS_READY_MASK))){
			/* Set timer expired flag to signal we've finished waiting */
			XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_MSGAVAILABLE);
			InstancePtr->Timer.TimerExpired = (TRUE);
			InstancePtr->Info.MsgAvailable = (TRUE);
		}

		return XST_SUCCESS;
	}
#endif

	/* busy waiting...*/
	if (InstancePtr->Timer.TimerExpired == (FALSE)) {

		/* Poll if requested */
		/* Read current timer count */
		TimerCount = XHdcp22Tx_GetTimerCount(InstancePtr);

		/* Apply polling value: 0=poll always, 1=poll after 1ms, etc... */
		if ((InstancePtr->Info.PollingValue == 0) ||
			((InstancePtr->Timer.InitialTicks - TimerCount) >= IntervalCount))
		{
			/* Update InitialTicks to the current counter value */
			InstancePtr->Timer.InitialTicks = TimerCount;

			/* Read Rx status. */
			XHdcp22Tx_ReadRxStatus(InstancePtr);

			if (((ReadyBit == FALSE) && ((InstancePtr->Info.RxStatus & XHDCP22_TX_RXSTATUS_AVAIL_BYTES_MASK) == ExpectedSize)) ||
				(((ReadyBit == TRUE) && (InstancePtr->Info.RxStatus & XHDCP22_TX_RXSTATUS_READY_MASK)) &&
				((InstancePtr->Info.RxStatus & XHDCP22_TX_RXSTATUS_AVAIL_BYTES_MASK) > 0))) {

				/* Stop the hardware timer */
				XTmrCtr_Stop(&InstancePtr->Timer.TmrCtr, XHDCP22_TX_TIMER_CNTR_0);

				/* Set timer expired flag and MsgAvailable flag to signal we've finished waiting */
				XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_MSGAVAILABLE);
				InstancePtr->Timer.TimerExpired = (TRUE);
				InstancePtr->Info.MsgAvailable = (TRUE);
			}
		}
		return XST_SUCCESS;
	}

	/* timer expired: waiting done...check size in the status */
	if (((ReadyBit == FALSE) && ((InstancePtr->Info.RxStatus & XHDCP22_TX_RXSTATUS_AVAIL_BYTES_MASK) == ExpectedSize)) ||
		((ReadyBit == TRUE) && (InstancePtr->Info.RxStatus & XHDCP22_TX_RXSTATUS_READY_MASK))) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_MSGAVAILABLE);
		InstancePtr->Info.MsgAvailable = (TRUE);
		return XST_SUCCESS;
	}

	/* The receiver has timed out...and the data size does not match
	 * the expected size! */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_TIMEOUT);
	return XST_FAILURE;
}


/*****************************************************************************/
/**
*
* This function reads RxStatus from the DDC channel. If the read
* is not successful it will default to an RxStatus value of 0xFFFF
* to initiate re-authentication.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  MsgBufferPtr the buffer to use for messaging.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_ReadRxStatus(XHdcp22_Tx *InstancePtr)
{
	u8 DdcBuf[2];
	int Status = XST_FAILURE;

	/* Set the RxStatus register address */
	DdcBuf[0] = XHDCP22_TX_HDCPPORT_RXSTATUS_OFFSET;

	Status = InstancePtr->DdcWrite(XHDCP22_TX_DDC_BASE_ADDRESS, 1, DdcBuf, (FALSE),
	                      InstancePtr->DdcHandlerRef);

	/* If write fails, request re-authentication */
	if (Status != XST_SUCCESS) {
		InstancePtr->Info.RxStatus = XHDCP22_TX_INVALID_RXSTATUS;
		return;
	}

	Status = InstancePtr->DdcRead(XHDCP22_TX_DDC_BASE_ADDRESS, sizeof(DdcBuf),
					DdcBuf, (TRUE), InstancePtr->DdcHandlerRef);

	/* If read fails, request re-authentication */
	if (Status != XST_SUCCESS) {
		InstancePtr->Info.RxStatus = XHDCP22_TX_INVALID_RXSTATUS;
		return;
	}

	InstancePtr->Info.RxStatus = DdcBuf[0] | (DdcBuf[1] << 8);
}

/*****************************************************************************/
/**
*
* This function handles authentication failures.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_HandleAuthenticationFailed(XHdcp22_Tx * InstancePtr)
{
	InstancePtr->Info.AuthenticationStatus = XHDCP22_TX_AUTHENTICATION_BUSY;

     /* Run user callback */
	if (InstancePtr->IsUnauthenticatedCallbackSet)
		InstancePtr->UnauthenticatedCallback(InstancePtr->UnauthenticatedCallbackRef);

	/* HDCP2Version */
	InstancePtr->IsReceiverHDCP2Capable = XHdcp22Tx_IsDwnstrmCapable(InstancePtr);
}

/*****************************************************************************/
/**
*
* This function handles reauthentication requests.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_HandleReauthenticationRequest(XHdcp22_Tx * InstancePtr)
{
	InstancePtr->Info.ReAuthenticationRequested = (TRUE);
	InstancePtr->Info.AuthenticationStatus = XHDCP22_TX_REAUTHENTICATE_REQUESTED;

	/* Reset cipher */
	XHdcp22Tx_DisableEncryption(InstancePtr);
	XHdcp22Cipher_Disable(&InstancePtr->Cipher);
	XHdcp22Cipher_Enable(&InstancePtr->Cipher);

	/* Increment re-authentication request count */
	InstancePtr->Info.ReauthRequestCnt++;

	/* Clear Topology Available flag */
	InstancePtr->Info.IsTopologyAvailable = (FALSE);

	if (InstancePtr->IsUnauthenticatedCallbackSet)
		InstancePtr->UnauthenticatedCallback(InstancePtr->UnauthenticatedCallbackRef);
}

/*****************************************************************************/
/**
*
* This function issues a AKE_Init message
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  MsgBufferPtr the buffer to use for messaging.
*
* @return
*         - XST_SUCCESS if writing succeeded
*         - XST_FAILURE if failed to write.
*
* @note   None.
*
******************************************************************************/
static int XHdcp22Tx_WriteAKEInit(XHdcp22_Tx *InstancePtr)
{
	XHdcp22_Tx_DDCMessage* MsgPtr = (XHdcp22_Tx_DDCMessage*)InstancePtr->MessageBuffer;

	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_TX_AKEINIT);

	/* Increment authentication request count */
	InstancePtr->Info.AuthRequestCnt++;

	MsgPtr->DdcAddress = XHDCP22_TX_HDCPPORT_WRITE_MSG_OFFSET;
	MsgPtr->Message.MsgId = XHDCP22_TX_AKE_INIT;

	/* Generate Rtx and add to the buffer*/
	XHdcp22Tx_GenerateRtx(InstancePtr, InstancePtr->Info.Rtx);

	memcpy(&MsgPtr->Message.AKEInit.Rtx, InstancePtr->Info.Rtx,
	       sizeof(MsgPtr->Message.AKEInit.Rtx));
	/* Add TxCaps to the buffer*/
	memcpy(&MsgPtr->Message.AKEInit.TxCaps, XHdcp22_Tx_TxCaps,
	       sizeof(MsgPtr->Message.AKEInit.TxCaps));

	/* Execute write */
	return InstancePtr->DdcWrite(XHDCP22_TX_DDC_BASE_ADDRESS,
	                             sizeof(XHdcp22_Tx_AKEInit)+1,
	                             (u8 *)MsgPtr, (TRUE),
	                             InstancePtr->DdcHandlerRef);
}

/*****************************************************************************/
/**
*
* This function issues a AKE_No_Stored_km message
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  MsgBufferPtr the buffer containing the certificate and re-used
*         for sending the message.
*
* @return
*         - XST_SUCCESS if writing succeeded
*         - XST_FAILURE if failed to write
*
* @note   None.
*
******************************************************************************/
static int XHdcp22Tx_WriteAKENoStoredKm(XHdcp22_Tx *InstancePtr,
                                         const XHdcp22_Tx_PairingInfo *PairingInfoPtr,
                                         const XHdcp22_Tx_CertRx *CertificatePtr)
{
	XHdcp22_Tx_DDCMessage* MsgPtr = (XHdcp22_Tx_DDCMessage*)InstancePtr->MessageBuffer;
	u8 MaskingSeed[XHDCP22_TX_KM_MSK_SEED_SIZE];
	u8 EKpubKm[XHDCP22_TX_E_KPUB_KM_SIZE];

	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	                XHDCP22_TX_LOG_DBG_ENCRYPT_KM_DONE);

	MsgPtr->DdcAddress = XHDCP22_TX_HDCPPORT_WRITE_MSG_OFFSET;
	MsgPtr->Message.MsgId = XHDCP22_TX_AKE_NO_STORED_KM;

	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	                XHDCP22_TX_LOG_DBG_ENCRYPT_KM);

	/* Get the seed for the masking RSA-OEAP masking function */
	XHdcp22Tx_GenerateKmMaskingSeed(InstancePtr, MaskingSeed);

	/* Encrypt, pass certificate (1st value is messageId) */
	XHdcp22Tx_EncryptKm(CertificatePtr, PairingInfoPtr->Km, MaskingSeed, EKpubKm);
	memcpy(MsgPtr->Message.AKENoStoredKm.EKpubKm, EKpubKm,
	       sizeof(MsgPtr->Message.AKENoStoredKm.EKpubKm));

	/* Execute write */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	                XHDCP22_TX_LOG_DBG_TX_NOSTOREDKM);

	return InstancePtr->DdcWrite(XHDCP22_TX_DDC_BASE_ADDRESS,
	                      sizeof(XHdcp22_Tx_AKENoStoredKm)+1, (u8 *)MsgPtr,
	                      (TRUE), InstancePtr->DdcHandlerRef);
}

/*****************************************************************************/
/**
*
* This function issues a AKE_Stored_km message
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  MsgBufferPtr the buffer to use for messaging.
*
* @return
*         - XST_SUCCESS if writing succeeded
*         - XST_FAILURE if failed to write
*
* @note   None.
*
******************************************************************************/
static int XHdcp22Tx_WriteAKEStoredKm(XHdcp22_Tx *InstancePtr,
                                       const XHdcp22_Tx_PairingInfo *PairingInfoPtr)
{
	XHdcp22_Tx_DDCMessage* MsgPtr =
	                       (XHdcp22_Tx_DDCMessage*)InstancePtr->MessageBuffer;

	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	                XHDCP22_TX_LOG_DBG_TX_STOREDKM);

	MsgPtr->DdcAddress = XHDCP22_TX_HDCPPORT_WRITE_MSG_OFFSET;
	MsgPtr->Message.MsgId = XHDCP22_TX_AKE_STORED_KM;

	memcpy(MsgPtr->Message.AKEStoredKm.EKhKm, PairingInfoPtr->Ekh_Km,
	       sizeof(MsgPtr->Message.AKEStoredKm.EKhKm));
	memcpy(MsgPtr->Message.AKEStoredKm.Rtx, PairingInfoPtr->Rtx,
	       sizeof(MsgPtr->Message.AKEStoredKm.Rtx));
	memcpy(MsgPtr->Message.AKEStoredKm.Rrx, PairingInfoPtr->Rrx,
	       sizeof(MsgPtr->Message.AKEStoredKm.Rrx));

	/* Execute write */
	return InstancePtr->DdcWrite(XHDCP22_TX_DDC_BASE_ADDRESS,
	                      sizeof(XHdcp22_Tx_AKEStoredKm)+1, (u8 *)MsgPtr,
	                      (TRUE), InstancePtr->DdcHandlerRef);
}

/*****************************************************************************/
/**
*
* This function write a locality check has value.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  RnPtr is a pointer to the XHdcp22Tx core instance.
*
* @return
*         - XST_SUCCESS if writing succeeded
*         - XST_FAILURE if failed to write
*
* @note   None.
*
******************************************************************************/
static int XHdcp22Tx_WriteLcInit(XHdcp22_Tx *InstancePtr, const u8* RnPtr)
{
	XHdcp22_Tx_DDCMessage* MsgPtr =
	                      (XHdcp22_Tx_DDCMessage*)InstancePtr->MessageBuffer;

	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG,
	                XHDCP22_TX_LOG_DBG_TX_LCINIT);

	MsgPtr->DdcAddress = XHDCP22_TX_HDCPPORT_WRITE_MSG_OFFSET;
	MsgPtr->Message.MsgId = XHDCP22_TX_LC_INIT;

	memcpy(MsgPtr->Message.LCInit.Rn, RnPtr, sizeof(MsgPtr->Message.LCInit.Rn));

	/* Execute write */
	return InstancePtr->DdcWrite(XHDCP22_TX_DDC_BASE_ADDRESS,
	                      sizeof(XHdcp22_Tx_LCInit)+1, (u8 *)MsgPtr,
	                      (TRUE), InstancePtr->DdcHandlerRef);
}

/******************************************************************************/
/**
*
* This function sends the session key to the receiver.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  EdkeyKsPtr is a pointer to the encrypted session key.
* @param  RivPtr is a pointer to the random IV pair.
*
* @return
*         - XST_SUCCESS if writing succeeded
*         - XST_FAILURE if failed to write
*
* @note   None.
*
******************************************************************************/
static int XHdcp22Tx_WriteSkeSendEks(XHdcp22_Tx *InstancePtr,
                                      const u8 *EdkeyKsPtr, const u8 *RivPtr)
{
	XHdcp22_Tx_DDCMessage* MsgPtr =
	                      (XHdcp22_Tx_DDCMessage*)InstancePtr->MessageBuffer;

	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_TX_EKS);

	MsgPtr->DdcAddress = XHDCP22_TX_HDCPPORT_WRITE_MSG_OFFSET;
	MsgPtr->Message.MsgId = XHDCP22_TX_SKE_SEND_EKS;

	memcpy(MsgPtr->Message.SKESendEks.EDkeyKs, EdkeyKsPtr,
	       sizeof(MsgPtr->Message.SKESendEks.EDkeyKs));

	memcpy(MsgPtr->Message.SKESendEks.Riv, RivPtr,
	       sizeof(MsgPtr->Message.SKESendEks.Riv));

	/* Execute write */
	return InstancePtr->DdcWrite(XHDCP22_TX_DDC_BASE_ADDRESS,
	                      sizeof(XHdcp22_Tx_SKESendEks)+1, (u8 *)MsgPtr,
	                      (TRUE), InstancePtr->DdcHandlerRef);
}

/******************************************************************************/
/**
*
* This function sends the Receiver ID List acknowledgement to the repeater.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  VPtr is a pointer to the least significant 128-bits of V.
*
* @return
*         - XST_SUCCESS if writing succeeded
*         - XST_FAILURE if failed to write
*
* @note   None.
*
******************************************************************************/
static int XHdcp22Tx_WriteRepeaterAuth_Send_Ack(XHdcp22_Tx *InstancePtr, const u8 *VPtr)
{
	XHdcp22_Tx_DDCMessage* MsgPtr =
		(XHdcp22_Tx_DDCMessage*)InstancePtr->MessageBuffer;

	MsgPtr->DdcAddress = XHDCP22_TX_HDCPPORT_WRITE_MSG_OFFSET;
	MsgPtr->Message.MsgId = XHDCP22_TX_REPEATAUTH_SEND_ACK;

	memcpy(MsgPtr->Message.RepeatAuthSendAck.V, VPtr,
		sizeof(MsgPtr->Message.RepeatAuthSendAck.V));

	/* Execute write */
	return InstancePtr->DdcWrite(XHDCP22_TX_DDC_BASE_ADDRESS,
	                      sizeof(XHdcp22_Tx_RepeatAuthSendAck) + 1, (u8 *)MsgPtr,
	                      (TRUE), InstancePtr->DdcHandlerRef);
}

/******************************************************************************/
/**
*
* This function sends the Content Stream Type to the repeater.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return
*         - XST_SUCCESS if writing succeeded
*         - XST_FAILURE if failed to write
*
* @note   None.
*
******************************************************************************/
static int XHdcp22Tx_WriteRepeaterAuth_Stream_Manage(XHdcp22_Tx *InstancePtr)
{
	XHdcp22_Tx_PairingInfo *PairingInfoPtr =
		(XHdcp22_Tx_PairingInfo *)InstancePtr->Info.StateContext;
	XHdcp22_Tx_DDCMessage* MsgPtr =
		(XHdcp22_Tx_DDCMessage*)InstancePtr->MessageBuffer;

	MsgPtr->DdcAddress = XHDCP22_TX_HDCPPORT_WRITE_MSG_OFFSET;
	MsgPtr->Message.MsgId = XHDCP22_TX_REPEATAUTH_STREAM_MANAGE;

	/* Value is sent in big endian format */
	MsgPtr->Message.RepeatAuthStreamManage.SeqNum_M[0] = (InstancePtr->Info.SeqNum_M >> 16) & 0xFF; // MSB
	MsgPtr->Message.RepeatAuthStreamManage.SeqNum_M[1] = (InstancePtr->Info.SeqNum_M >> 8)  & 0xFF;
	MsgPtr->Message.RepeatAuthStreamManage.SeqNum_M[2] =  InstancePtr->Info.SeqNum_M        & 0xFF; // LSB

	/* The parameter K is always set to 0x1 by the HDCP transmitter */
	/* Value is sent in big endian format */
	MsgPtr->Message.RepeatAuthStreamManage.K[0] = 0x0; // MSB
	MsgPtr->Message.RepeatAuthStreamManage.K[1] = 0x1; // LSB

	/* StreamID_Type = STREAM_ID || Type */
	/* Value is sent in big endian format */
	MsgPtr->Message.RepeatAuthStreamManage.StreamID_Type[0] = 0x0; // STREAM_ID: must always be 0x0
	MsgPtr->Message.RepeatAuthStreamManage.StreamID_Type[1] = (u8)InstancePtr->Info.ContentStreamType; // Stream Type

	/* To make verifying the MPrime from the repeater easier,
	 * the M is calculated and stored before executing the write */
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_COMPUTE_M);
	XHdcp22Tx_ComputeM(InstancePtr->Info.Rn,
		InstancePtr->Info.Rrx,
		InstancePtr->Info.Rtx,
		MsgPtr->Message.RepeatAuthStreamManage.StreamID_Type,
		MsgPtr->Message.RepeatAuthStreamManage.K,
		MsgPtr->Message.RepeatAuthStreamManage.SeqNum_M,
		PairingInfoPtr->Km,
		InstancePtr->Info.M);
	XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_COMPUTE_M_DONE);

	/* Increment the M */
	InstancePtr->Info.SeqNum_M += 1;
	/* Seq_num_M is 3 bytes, the variable is a u32.
	 * Mask out the most-significant byte.
	 */
	InstancePtr->Info.SeqNum_M &= 0xFFF;

	/* Execute write */
	return InstancePtr->DdcWrite(XHDCP22_TX_DDC_BASE_ADDRESS,
		XHDCP22_TX_REPEATAUTH_STREAM_MANAGE_SIZE + 1, (u8 *)MsgPtr,
		(TRUE), InstancePtr->DdcHandlerRef);
}

/*****************************************************************************/
/**
*
* This function receives a message send by HDCP22 RX.
*
* @param  InstancePtr is a pointer to the XHdcp22Tx core instance.
* @param  MessageId the message Id of the expected message.
* @param  MessageSize the size of the expected message.
*
* @return
*         - XST_SUCCESS if the message size and id are as expected.
*         - XST_FAILURE if if the message size and id are incorrect.
* @note   None.
*
******************************************************************************/
static int XHdcp22Tx_ReceiveMsg(XHdcp22_Tx *InstancePtr, u8 MessageId,
                                u32 MessageSize)
{
	int Result = XST_SUCCESS;
	XHdcp22_Tx_DDCMessage* MsgPtr =
	                       (XHdcp22_Tx_DDCMessage*)InstancePtr->MessageBuffer;

	u32 ReceivedSize = InstancePtr->Info.RxStatus &
	                   XHDCP22_TX_RXSTATUS_AVAIL_BYTES_MASK;

	/* Check if the received size matches the expected size. */
	if (ReceivedSize != MessageSize) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_MSG_READ_FAIL);
		return XST_FAILURE;
	}

	MsgPtr->DdcAddress = XHDCP22_TX_HDCPPORT_READ_MSG_OFFSET;
	/* Set the Expected msg ID in the buffer for testing purposes */
	MsgPtr->Message.MsgId = MessageId;

	Result = InstancePtr->DdcWrite(XHDCP22_TX_DDC_BASE_ADDRESS, 1,
	                               &MsgPtr->DdcAddress, (FALSE),
	                               InstancePtr->DdcHandlerRef);
	if (Result != XST_SUCCESS) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_MSG_READ_FAIL);
		return Result;
	}

	/* Reading starts on message id */
	Result = InstancePtr->DdcRead(XHDCP22_TX_DDC_BASE_ADDRESS, ReceivedSize,
	                              (u8 *)&MsgPtr->Message.MsgId,
	                              (TRUE), InstancePtr->DdcHandlerRef);
	if (Result != XST_SUCCESS) {
		XHdcp22Tx_LogWr(InstancePtr, XHDCP22_TX_LOG_EVT_DBG, XHDCP22_TX_LOG_DBG_MSG_READ_FAIL);
		return Result;
	}

	/* Check if the received message id matches the expected message id */
	if (MsgPtr->Message.MsgId != MessageId)
		return XST_FAILURE;

	return XST_SUCCESS;
}


/*****************************************************************************/
/**
*
* This function clear the global pairing info structure, so every
* HDCP2.2 receiver will have to go through the 'no stored km' sequence
* to authenticate.
*
* @param   InstancePtr is a pointer to the XHdcp22Tx core instance.
*
* @return  XST_SUCCESS
*
* @note    None.
*
******************************************************************************/
int XHdcp22Tx_ClearPairingInfo(XHdcp22_Tx *InstancePtr)
{
	Xil_AssertNonvoid(InstancePtr != NULL);

	memset(InstancePtr->Info.PairingInfo, 0x00,
	       sizeof(InstancePtr->Info.PairingInfo));

	return XST_SUCCESS;
}
/*****************************************************************************/
/**
*
* This function gets a stored pairing info entry from the storage.
*
* @param  ReceiverId is a pointer to a 5-byte receiver Id.
*
* @return A pointer to the found ReceiverId or NULL if the pairing info wasn't
*         stored yet.
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_PairingInfo *XHdcp22Tx_GetPairingInfo(XHdcp22_Tx *InstancePtr,
	                                                     const u8 *ReceiverId)
{
	int i = 0;
	u8 IllegalRecvID[] = {0x0, 0x0, 0x0, 0x0, 0x0};
	XHdcp22_Tx_PairingInfo * PairingInfoPtr = NULL;

	/* Check for illegal Receiver ID */
	if (memcmp(ReceiverId, IllegalRecvID, XHDCP22_TX_CERT_RCVID_SIZE) == 0) {
		return NULL;
	}

	for (i=0; i<XHDCP22_TX_MAX_STORED_PAIRINGINFO; i++) {
		PairingInfoPtr = &InstancePtr->Info.PairingInfo[i];
		if (memcmp(ReceiverId, PairingInfoPtr->ReceiverId,
		           XHDCP22_TX_CERT_RCVID_SIZE) == 0) {
			return PairingInfoPtr;
		}
	}
	return NULL;
}

/*****************************************************************************/
/**
*
* This function updates a pairing info entry in the storage.
*
* @param  PairingInfo is a pointer to a pairing info structure.
*
* @return
*         - XST_SUCCESS if there was enough room
*         - XST_FAILURE if there was no room for this receiver
*
* @note   None.
*
******************************************************************************/
static XHdcp22_Tx_PairingInfo *XHdcp22Tx_UpdatePairingInfo(
	                          XHdcp22_Tx *InstancePtr,
                              const XHdcp22_Tx_PairingInfo *PairingInfo,
                              u8 Ready)
{
	int i = 0;
	int i_match = 0;
	u8 Match = (FALSE);
	XHdcp22_Tx_PairingInfo * PairingInfoPtr = NULL;

	/* Find slot */
	for (i=0; i<XHDCP22_TX_MAX_STORED_PAIRINGINFO; i++) {

		PairingInfoPtr = &InstancePtr->Info.PairingInfo[i];

		/* Look for empty slot */
		if ((PairingInfoPtr->Ready == FALSE) && (Match == FALSE)) {
			i_match = i;
			Match = (TRUE);
		}

		/* Look for match, match overrides empty slot */
		if (memcmp(PairingInfo->ReceiverId, PairingInfoPtr->ReceiverId,
		           XHDCP22_TX_CERT_RCVID_SIZE) == 0) {
			i_match = i;
			break;
		}
	}

	PairingInfoPtr = &InstancePtr->Info.PairingInfo[i_match];

	/* Copy pairing info*/
	memcpy(PairingInfoPtr, PairingInfo, sizeof(XHdcp22_Tx_PairingInfo));

	/* Set table ready */
	PairingInfoPtr->Ready = Ready;

	return PairingInfoPtr;
}

/*****************************************************************************/
/**
*
* This function invalidates a pairing info structure
*
* @param  ReceiverId is a pointer to a 5-byte receiver Id.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void XHdcp22Tx_InvalidatePairingInfo(XHdcp22_Tx *InstancePtr,
	                                        const u8* ReceiverId)
{
	XHdcp22_Tx_PairingInfo *InfoPtr = XHdcp22Tx_GetPairingInfo(InstancePtr,
		                                                       ReceiverId);

	/* do nothing if the id was not found */
	if (InfoPtr == NULL) {
		return;
	}
	/* clear the found structure */
	memset(InfoPtr, 0x00, sizeof(XHdcp22_Tx_PairingInfo));
}

/*****************************************************************************/
/**
*
* This function returns the DEPTH in the repeater topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return   Repeater topology DEPTH.
*
* @note     None.
*
******************************************************************************/
static u32 XHdcp22Tx_GetTopologyDepth(XHdcp22_Tx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Topology.Depth);
}

/*****************************************************************************/
/**
*
* This function returns the DEVICE_COUNT in the repeater topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return   DEVICE_COUNT in the repeater topology structure.
*
* @note     None.
*
******************************************************************************/
static u32 XHdcp22Tx_GetTopologyDeviceCnt(XHdcp22_Tx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Topology.DeviceCnt);
}

/*****************************************************************************/
/**
*
* This function returns the MAX_DEVICS_EXCEEDED flag in the repeater
* topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return   MAX_DEVICS_EXCEEDED flag in the repeater structure.
*
* @note     None.
*
******************************************************************************/
static u32 XHdcp22Tx_GetTopologyMaxDevsExceeded(XHdcp22_Tx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Topology.MaxDevsExceeded) ? TRUE : FALSE;
}

/*****************************************************************************/
/**
*
* This function returns the MAX_CASCADE_EXCEEDED flag in the repeater
* topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return   MAX_CASCADE_EXCEEDED flag in the repeater structure.
*
* @note     None.
*
******************************************************************************/
static u32 XHdcp22Tx_GetTopologyMaxCascadeExceeded(XHdcp22_Tx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Topology.MaxCascadeExceeded) ? TRUE : FALSE;
}

/*****************************************************************************/
/**
*
* This function returns the HDCP2_0_REPEATER_DOWNSTREAM flag in the repeater
* topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return   HDCP2_0_REPEATER_DOWNSTREAM flag in the repeater structure.
*
* @note     None.
*
******************************************************************************/
static u32 XHdcp22Tx_GetTopologyHdcp20RepeaterDownstream(XHdcp22_Tx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Topology.Hdcp20RepeaterDownstream) ? TRUE : FALSE;
}

/*****************************************************************************/
/**
*
* This function returns the HDCP1_DEVICE_DOWNSTREAM flag in the repeater
* topology structure.
*
* @param    InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return   HDCP1_DEVICE_DOWNSTREAM flag in the repeater structure.
*
* @note     None.
*
******************************************************************************/
static u32 XHdcp22Tx_GetTopologyHdcp1DeviceDownstream(XHdcp22_Tx *InstancePtr)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);

	return (InstancePtr->Topology.Hdcp1DeviceDownstream) ? TRUE : FALSE;
}

/*****************************************************************************/
/**
*
* This function clears the log pointers
*
* @param  InstancePtr is a pointer to the XHdcp22_Tx core instance.
* @param  Verbose allows to add debug logging.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XHdcp22Tx_LogReset(XHdcp22_Tx *InstancePtr, u8 Verbose)
{
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);

	InstancePtr->Log.Head = 0;
	InstancePtr->Log.Tail = 0;
	InstancePtr->Log.Verbose = Verbose;
	/* Reset and start the logging timer. */
	/* Note: This timer increments continuously and will wrap at 42 second (100 Mhz clock) */
	if (InstancePtr->Timer.TmrCtr.IsReady == XIL_COMPONENT_IS_READY) {
	   XTmrCtr_SetResetValue(&InstancePtr->Timer.TmrCtr, XHDCP22_TX_TIMER_CNTR_1, 0);
	   XTmrCtr_Start(&InstancePtr->Timer.TmrCtr, XHDCP22_TX_TIMER_CNTR_1);
	}
}

/*****************************************************************************/
/**
*
* This function returns the time expired since a log reset was called
*
* @param  InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return The expired logging time in useconds.
*
* @note   None.
*
******************************************************************************/
u32 XHdcp22Tx_LogGetTimeUSecs(XHdcp22_Tx *InstancePtr)
{
	if (InstancePtr->Timer.TmrCtr.IsReady != XIL_COMPONENT_IS_READY)
		return 0;

	u32 PeriodUsec = (u32)InstancePtr->Timer.TmrCtr.Config.SysClockFreqHz / 1000000;
	return 	 ( XTmrCtr_GetValue(&InstancePtr->Timer.TmrCtr,
			   XHDCP22_TX_TIMER_CNTR_1) / PeriodUsec);
}

/*****************************************************************************/
/**
*
* This function writes HDCP TX logs into buffer.
*
* @param  InstancePtr is a pointer to the XHdcp22_Tx core instance.
* @param  Evt specifies an action to be carried out. Please refer
*         #XHdcp22_Tx_LogEvt enum in xhdcp22_tx.h.
* @param  Data specifies the information that gets written into log
*         buffer.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XHdcp22Tx_LogWr(XHdcp22_Tx *InstancePtr, XHdcp22_Tx_LogEvt Evt, u16 Data)
{
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Evt < (XHDCP22_TX_LOG_INVALID));

	if (InstancePtr->Log.Verbose == FALSE && Evt == XHDCP22_TX_LOG_EVT_DBG) {
		return;
	}

	/* Write data and event into log buffer */
	InstancePtr->Log.LogItems[InstancePtr->Log.Head].Data = Data;
	InstancePtr->Log.LogItems[InstancePtr->Log.Head].LogEvent = Evt;
	InstancePtr->Log.LogItems[InstancePtr->Log.Head].TimeStamp =
                            XHdcp22Tx_LogGetTimeUSecs(InstancePtr);

	/* Update head pointer if reached to end of the buffer */
	if (InstancePtr->Log.Head == XHDCP22_TX_LOG_BUFFER_SIZE - 1) {
		/* Clear pointer */
		InstancePtr->Log.Head = 0;
	} else {
		/* Increment pointer */
		InstancePtr->Log.Head++;
	}

	/* Check tail pointer. When the two pointer are equal, then the buffer
	 * is full.In this case then increment the tail pointer as well to
	 * remove the oldest entry from the buffer.
	 */
	if (InstancePtr->Log.Tail == InstancePtr->Log.Head) {
		if (InstancePtr->Log.Tail == XHDCP22_TX_LOG_BUFFER_SIZE - 1) {
			InstancePtr->Log.Tail = 0;
		} else {
			InstancePtr->Log.Tail++;
		}
	}
}

/*****************************************************************************/
/**
*
* This function provides the log information from the log buffer.
*
* @param  InstancePtr is a pointer to the XHdcp22_Tx core instance.
*
* @return
*         - Content of log buffer if log pointers are not equal.
*         - Otherwise Zero.
*
* @note   None.
*
******************************************************************************/
XHdcp22_Tx_LogItem* XHdcp22Tx_LogRd(XHdcp22_Tx *InstancePtr)
{
	XHdcp22_Tx_LogItem* LogPtr;
	u16 Tail = 0;
	u16 Head = 0;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	Tail = InstancePtr->Log.Tail;
	Head = InstancePtr->Log.Head;

	/* Check if there is any data in the log and return a NONE defined log item */
	if (Tail == Head) {
		LogPtr = &InstancePtr->Log.LogItems[Tail];
		LogPtr->Data = 0;
		LogPtr->LogEvent = XHDCP22_TX_LOG_EVT_NONE;
		LogPtr->TimeStamp = 0;
		return LogPtr;
	}

	LogPtr = &InstancePtr->Log.LogItems[Tail];

	/* Increment tail pointer */
	if (Tail == XHDCP22_TX_LOG_BUFFER_SIZE - 1) {
		InstancePtr->Log.Tail = 0;
	}
	else {
		InstancePtr->Log.Tail++;
	}
	return LogPtr;
}

/*****************************************************************************/
/**
*
* This function prints the content of log buffer.
*
* @param  InstancePtr is a pointer to the HDCP22 TX core instance.
* @param  buff is a pointer to the buffer to write to.
* @param  buff_size is the size of the passed buffer
*
* @return Number of characters printed to the buffer.
*
* @note   None.
*
******************************************************************************/
int XHdcp22Tx_LogShow(XHdcp22_Tx *InstancePtr, char *buff, int buff_size)
{
	int strSize = 0;
	XHdcp22_Tx_LogItem* LogPtr;
	char str[255];
	u32 TimeStampPrev = 0;

	/* Verify argument. */
	Xil_AssertVoid(InstancePtr != NULL);

#ifdef _XHDCP22_TX_TEST_
	if (InstancePtr->Test.TestMode == XHDCP22_TX_TESTMODE_UNIT) {
		XHdcp22Tx_LogDisplayUnitTest(InstancePtr);
		return;
	}
#endif

	strSize += scnprintf(buff+strSize, buff_size-strSize,
			"\r\n-------HDCP22 TX log start-------\r\n");
	strSize += scnprintf(buff+strSize, buff_size-strSize,
			"[Time(us):Delta(us)] <Event>\n\r");
	strcpy(str, "UNDEFINED");
	do {
		/* Read log data */
		LogPtr = XHdcp22Tx_LogRd(InstancePtr);

		/* Print timestamp */
		if(LogPtr->LogEvent != XHDCP22_TX_LOG_EVT_NONE)
		{
			if(LogPtr->TimeStamp < TimeStampPrev) TimeStampPrev = 0;
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"[%8u:", LogPtr->TimeStamp);
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"%8u] ", (LogPtr->TimeStamp - TimeStampPrev));
			TimeStampPrev = LogPtr->TimeStamp;
		}

		/* Print log event */
		switch (LogPtr->LogEvent) {
		case (XHDCP22_TX_LOG_EVT_NONE):
		strSize += scnprintf(buff+strSize, buff_size-strSize,
				"-------HDCP22 TX log end-------\r\n\r\n");
			break;
		case XHDCP22_TX_LOG_EVT_STATE:
			switch(LogPtr->Data)
			{
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, H0)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, H1)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A0)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A1)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A1_1)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A1_NSK0)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A1_NSK1)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A1_SK0)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A2)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A2_1)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A3)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A4)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A5)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A6_A7_A8)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A6)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A7)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A8)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A9)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_STATE_, A9_1)
				default: break;
			};
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Current state [%s]\r\n", str);
			break;
		case XHDCP22_TX_LOG_EVT_POLL_RESULT:
			switch(LogPtr->Data)
			{
			case XHDCP22_TX_INCOMPATIBLE_RX: strcpy(str, "INCOMPATIBLE RX"); break;
			case XHDCP22_TX_AUTHENTICATION_BUSY: strcpy(str, "AUTHENTICATION BUSY"); break;
			case XHDCP22_TX_AUTHENTICATED: strcpy(str, "AUTHENTICATED"); break;
			case XHDCP22_TX_UNAUTHENTICATED: strcpy(str, "UN-AUTHENTICATED"); break;
			case XHDCP22_TX_REAUTHENTICATE_REQUESTED: strcpy(str, "RE-AUTHENTICATION REQUESTED"); break;
			default: break;
			}
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Poll result [%s]\r\n", str);
			break;
		case XHDCP22_TX_LOG_EVT_ENABLED:
			if (LogPtr->Data == (FALSE)) {
				strcpy(str, "DISABLED");
			} else {
				strcpy(str, "ENABLED");
			}
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"State machine [%s]\r\n", str);
			break;
		case XHDCP22_TX_LOG_EVT_RESET:
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Asserted [RESET]\r\n");
			break;
		case XHDCP22_TX_LOG_EVT_ENCR_ENABLED:
			if (LogPtr->Data == (FALSE)) {
				strcpy(str, "DISABLED");
			} else {
				strcpy(str, "ENABLED");
			}
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Encryption [%s]\r\n", str);
			break;
		case XHDCP22_TX_LOG_EVT_TEST_ERROR:
			switch(LogPtr->Data)
			{
			case XHDCP22_TX_AKE_NO_STORED_KM:
			      strcpy(str, "EkpubKm does not match the calculated value.");
			      break;
			case XHDCP22_TX_SKE_SEND_EKS:
			      strcpy(str, "EdkeyKs does not match the calculated value.");
			      break;
			case XHDCP22_TX_MSG_UNDEFINED:
			      strcpy(str, "Trying to write an unexpected message.");
			      break;
			default: break;
			};
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: Test error [%s]\r\n", str);
			break;
		case XHDCP22_TX_LOG_EVT_LCCHK_COUNT:
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Locality check count [%d]\r\n", LogPtr->Data);
			break;
		case XHDCP22_TX_LOG_EVT_STRMMNGCHK_COUNT:
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Content Stream Management check count [%d]\r\n", LogPtr->Data);
			break;
		case XHDCP22_TX_LOG_EVT_DBG:
			switch(LogPtr->Data)
			{
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, STARTIMER)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, MSGAVAILABLE)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, TX_AKEINIT)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, RX_CERT)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, VERIFY_SIGNATURE)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, VERIFY_SIGNATURE_PASS)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, VERIFY_SIGNATURE_FAIL)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, DEVICE_IS_REVOKED)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, ENCRYPT_KM)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, ENCRYPT_KM_DONE)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, TX_NOSTOREDKM)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, TX_STOREDKM)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, RX_H1)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, RX_EKHKM)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPUTE_H)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPUTE_H_DONE)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPARE_H_FAIL)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, TX_LCINIT)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, RX_L1)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPUTE_L)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPUTE_L_DONE)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPARE_L_FAIL)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPUTE_EDKEYKS)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPUTE_EDKEYKS_DONE)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, TX_EKS)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, RX_RCVIDLIST)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPUTE_V)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPUTE_V_DONE)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPARE_V_FAIL)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, RX_M1)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPUTE_M)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, COMPUTE_M_DONE)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, CHECK_REAUTH)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, TIMEOUT)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, TIMESTAMP)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, AES128ENC)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, AES128ENC_DONE)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, SHA256HASH)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, SHA256HASH_DONE)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, OEAPENC)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, OEAPENC_DONE)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, RSAENC)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, RSAENC_DONE)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, MSG_WRITE_FAIL)
				XHDCP22_TX_CASE_TO_STR_PRE(XHDCP22_TX_LOG_DBG_, MSG_READ_FAIL)
				default: break;
			};
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Debug: Event [%s]\r\n", str);
			break;
		case XHDCP22_TX_LOG_EVT_USER:
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"User: %d\r\n", LogPtr->Data);
			break;
		default:
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: Unknown log event\r\n");
			break;
		}
	} while (LogPtr->LogEvent != XHDCP22_TX_LOG_EVT_NONE);

	return strSize;
}

/*****************************************************************************/
/**
*
* This function prints the state machine information.
*
* @param  InstancePtr is a pointer to the HDCP22 TX core instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XHdcp22Tx_Info(XHdcp22_Tx *InstancePtr)
{
	XDEBUG_PRINTF("Status : ");
	if (XHdcp22Tx_IsEnabled(InstancePtr)) {
		switch (InstancePtr->Info.AuthenticationStatus) {
			case XHDCP22_TX_INCOMPATIBLE_RX :
				XDEBUG_PRINTF("RX is incompatible.\n\r");
			break;

			case XHDCP22_TX_AUTHENTICATION_BUSY :
				XDEBUG_PRINTF("Busy Authentication.\n\r");
			break;

			case XHDCP22_TX_REAUTHENTICATE_REQUESTED :
				XDEBUG_PRINTF("Re-authentication Requested.\n\r");
			break;

			case XHDCP22_TX_UNAUTHENTICATED :
				XDEBUG_PRINTF("Not Authenticated.\n\r");
			break;

			case XHDCP22_TX_AUTHENTICATED :
				XDEBUG_PRINTF("Authenticated.\n\r");
			break;

            case XHDCP22_TX_DEVICE_IS_REVOKED :
            	XDEBUG_PRINTF("Device Revoked.\n\r");

            case XHDCP22_TX_NO_SRM_LOADED :
            	XDEBUG_PRINTF("No SRM Loaded.\n\r");

			default :
				XDEBUG_PRINTF("Unknown state.\n\r");
			break;
		}
	} else {
		XDEBUG_PRINTF("Core is disabled.\n\r");
	}

	XDEBUG_PRINTF("Encryption : ");
	if (XHdcp22Tx_IsEncryptionEnabled(InstancePtr)) {
		XDEBUG_PRINTF("Enabled.\n\r");
	} else {
		XDEBUG_PRINTF("Disabled.\n\r");
	}

	XDEBUG_PRINTF("Repeater: ");
	if (XHdcp22Tx_IsRepeater(InstancePtr)) {
		if (InstancePtr->Topology.MaxDevsExceeded)
			XDEBUG_PRINTF("MaxDevsExceeded, ");

		if (InstancePtr->Topology.MaxCascadeExceeded)
			XDEBUG_PRINTF("MaxCascadeExceeded, ");

		if (InstancePtr->Topology.Hdcp20RepeaterDownstream)
			XDEBUG_PRINTF("Hdcp20RepeaterDownstream, ");

		if (InstancePtr->Topology.Hdcp1DeviceDownstream)
			XDEBUG_PRINTF("Hdcp1DeviceDownstream, ");

		XDEBUG_PRINTF("Depth=%d, ", InstancePtr->Topology.Depth);
		XDEBUG_PRINTF("DeviceCnt=%d, ", InstancePtr->Topology.DeviceCnt);
		XDEBUG_PRINTF("StreamType=%d\n\r", InstancePtr->Info.ContentStreamType);
	} else {
		XDEBUG_PRINTF("Disabled.\n\r");
	}

	XDEBUG_PRINTF("Auth Requests: %d\n\r", InstancePtr->Info.AuthRequestCnt);
	XDEBUG_PRINTF("Reauth Requests: %d\n\r", InstancePtr->Info.ReauthRequestCnt);
	XDEBUG_PRINTF("Polling Interval: %d ms\n\r", InstancePtr->Info.PollingValue);
}

/** @} */
