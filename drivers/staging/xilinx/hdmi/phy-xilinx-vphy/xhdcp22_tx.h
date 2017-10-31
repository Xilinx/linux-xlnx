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
* @file xhdcp22_tx.h
* @addtogroup hdcp22_tx_v2_0
* @{
* @details
*
* This is the main header file for Xilinx HDCP 2.2 Transmiter device driver.
* The HDCP 2.2 Transmitter driver implements the authentication state machine.
* It consists of:
* - A state machine handling the states as specified in the HDCP revision 2.2
*   specification.
* - Functionality for checking if the HDCP 2.2 Receiver sink does respond within
*   specified times.
* - Message handling from/to the HDCP 2.2 receiver sink.
* - Logging functionality including time stamps.

* <b>Interrupts </b>
*
* The driver uses a hardware timer interrupt. The interrupt controller that should
* be used, must be passed with the #XHdcp22Tx_SetInterruptController function.
*
* Application developer needs to register interrupt handler with the processor,
* within their examples. Whenever processor calls registered application's
* interrupt handler associated with interrupt id, application's interrupt
* handler needs to call appropriate peripheral interrupt handler reading
* peripheral's Status register.
*
* <b> Threads </b>
*
* This driver is not thread safe. Any needs for threads or thread mutual
* exclusion must be satisfied by the layer above this driver.
*
* <b> Asserts </b>
*
* Asserts are used within all Xilinx drivers to enforce constraints on argument
* values. Asserts can be turned off on a system-wide basis by defining at
* compile time, the NDEBUG identifier. By default, asserts are turned on and it
* is recommended that users leave asserts on during development.
*
* <b> Building the driver </b>
*
* The HDCP TX driver is composed of several source files. This allows the user
* to build and link only those parts of the driver that are necessary.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  JO     06/24/15 Initial release.
* 1.01  MG     02/25/16 Added authenticated callback and GetVersion.
* 2.00  MH     06/28/16 Updated for repeater downstream support.
* 2.01  MH     02/28/17 Fixed compiler warnings.
* 2.20  MH     04/12/17 Added function XHdcp22Tx_IsDwnstrmCapable.
* 2.30  MH     07/06/17 Changed default polling value to 10 ms.
* </pre>
*
******************************************************************************/
#ifndef XHDCP22_TX_H
/**  prevent circular inclusions by using protection macros */
#define XHDCP22_TX_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
//#include <stdio.h>
#include <linux/string.h>


#include "xil_assert.h"
#include "xstatus.h"
#include "xdebug.h"
#include "xtmrctr.h"
#include "xhdcp22_cipher.h"
#include "xhdcp22_rng.h"

/************************** Constant Definitions *****************************/

/**
* This value is the default polling interval defined in millseconds.
* The polling interval defines the amount of time to wait between
* successive reads of the RxStatus register.
*/
#define XHDCP22_TX_DEFAULT_RX_STATUS_POLLVALUE  10

/**
* Needed storage to transmit/receive messages to the HDCP2.2 receiver
* This includes 1 extra byte for the DDC address used.
*/
#define  XHDCP22_TX_MAX_MESSAGE_SIZE    1+534

/**
* Needed storage for the Device IDs in the revocation list.
*/
#define XHDCP22_TX_REVOCATION_LIST_MAX_DEVICES 944

/**
* The list of maximum pairing info items to store.
*/
#define XHDCP22_TX_MAX_STORED_PAIRINGINFO  2

/**
* The size of the log buffer.
*/
#define XHDCP22_TX_LOG_BUFFER_SIZE 256

/**************************** Type Definitions *******************************/

/**
 * These constants are used to identify callback functions.
 */
typedef enum
{
	XHDCP22_TX_HANDLER_UNDEFINED,
	XHDCP22_TX_HANDLER_DDC_WRITE,
	XHDCP22_TX_HANDLER_DDC_READ,
	XHDCP22_TX_HANDLER_AUTHENTICATED,
	XHDCP22_TX_HANDLER_UNAUTHENTICATED,
	XHDCP22_TX_HANDLER_DOWNSTREAM_TOPOLOGY_AVAILABLE,
	XHDCP22_TX_HANDLER_INVALID
} XHdcp22_Tx_HandlerType;

/**
* These constants specify the different states in the internal state machine.
*/
typedef enum
{
	XHDCP22_TX_STATE_H0,      /**< No Rx Attached. */
	XHDCP22_TX_STATE_H1,      /**< Transmit Low value content. */
	XHDCP22_TX_STATE_A0,      /**< Known HDCP2 Capable Rx. */
	XHDCP22_TX_STATE_A1,      /**< Exchange Km. */
	XHDCP22_TX_STATE_A1_1,    /**< substate of A1: wait for AKE_SEND_CERT. */
	XHDCP22_TX_STATE_A1_NSK0, /**< No stored Km substate of A1: wait for AKE_SEND_H_PRIME. */
	XHDCP22_TX_STATE_A1_NSK1, /**< No stored Km substate of A1, wait for AKE_SEND_PAIRING_INFO. */
	XHDCP22_TX_STATE_A1_SK0,  /**< Stored substate of A1, wait for AKE_SEND_PAIRING_INFO. */
	XHDCP22_TX_STATE_A2,      /**< Locality Check. */
	XHDCP22_TX_STATE_A2_1,    /**< Locality Check. Receive and verify L_Prime. */
	XHDCP22_TX_STATE_A3,      /**< Exchange Ks. */
	XHDCP22_TX_STATE_A4,      /**< Test for repeater. */
	XHDCP22_TX_STATE_A5,      /**< Authenticated. */
	XHDCP22_TX_STATE_A6_A7_A8,/**< Wait for receiver ID list, verify and send acknowledgment */
	XHDCP22_TX_STATE_A6,      /**< Wait for receiver ID list. */
	XHDCP22_TX_STATE_A7,      /**< Verify Receiver ID List. */
	XHDCP22_TX_STATE_A8,      /**< Send Receiver ID List acknowledgment. */
	XHDCP22_TX_STATE_A9,      /**< Content Stream Management. */
	XHDCP22_TX_STATE_A9_1,    /**< Content Stream Management. Receive and verify M_Prime. */
	XHDCP22_TX_NUM_STATES     /**< Number of states in the state machine. */
}XHdcp22_Tx_StateType;

/**
* These constants specify return values on polling with #XHdcp22Tx_Poll.
* Depending on this return value, encryption of the HDMI signal should be
* set to encrypted or un-encrypted. See also #XHdcp22Tx_EnableEncryption and
* #XHdcp22Tx_DisableEncryption
*/
typedef enum
{
	XHDCP22_TX_INCOMPATIBLE_RX,          /**< A HDCP2 compatible receiver is not found. */
	XHDCP22_TX_AUTHENTICATION_BUSY,      /**< Authentication is busy. */
	XHDCP22_TX_AUTHENTICATED,            /**< Authentication is completed successfully. */
	XHDCP22_TX_UNAUTHENTICATED,          /**< Authentication failed. */
	XHDCP22_TX_REAUTHENTICATE_REQUESTED, /**< ReAuthentication requested.*/
	XHDCP22_TX_DEVICE_IS_REVOKED,        /**< A device in the HDCP chain is revoked. */
	XHDCP22_TX_NO_SRM_LOADED             /**< No valid SRM is loaded. */
} XHdcp22_Tx_AuthenticationType;

/**
* These constants are used to define the content stream type.
*/
typedef enum {
	XHDCP22_STREAMTYPE_0, /**< Type 0 Content Stream.
	                        * Stream may be transmitted to all HDCP devices. */
	XHDCP22_STREAMTYPE_1, /**< Type 1 Content Stream.
	                        * Stream must not be transmitted to HDCP1.x devices
	                        * and HDCP2.0 Repeaters. */
} XHdcp22_Tx_ContentStreamType;

/**
* These constants are events as stored in the logging list.
*/
typedef enum {
	XHDCP22_TX_LOG_EVT_NONE,            /**< Log Event None. */
	XHDCP22_TX_LOG_EVT_STATE,           /**< State of the state machine. */
	XHDCP22_TX_LOG_EVT_POLL_RESULT,     /**< Authentication result of polling. */
	XHDCP22_TX_LOG_EVT_ENABLED,         /**< HDCP2.2 core is enabled or disabled. */
	XHDCP22_TX_LOG_EVT_RESET,           /**< HDCP2.2 core is reset. */
	XHDCP22_TX_LOG_EVT_ENCR_ENABLED,    /**< HDCP2.2 stream is encrypted or not. */
	XHDCP22_TX_LOG_EVT_TEST_ERROR,      /**< An error was detected in one of the test modes. */
	XHDCP22_TX_LOG_EVT_DBG,             /**< Log event for debugging. */
	XHDCP22_TX_LOG_EVT_LCCHK_COUNT,     /**< Number of times Locality check has been done. */
	XHDCP22_TX_LOG_EVT_STRMMNGCHK_COUNT,/**< Number of times Content Stream Management check has been done. */
	XHDCP22_TX_LOG_EVT_USER,            /**< User logging. */
	XHDCP22_TX_LOG_INVALID              /**< Last value the list, only used for checking. */
} XHdcp22_Tx_LogEvt;

/**
 * These constants are used to define the used protocol.
 */
typedef enum {
	XHDCP22_TX_HDMI,                    /**< HDMI protocol. */
	XHDCP22_TX_DP,                      /**< Display Port protocol. */
} XHdcp22_Tx_Protocol;

/**
 * These constants are used to define the used mode.
 */
typedef enum {
	XHDCP22_TX_TRANSMITTER,             /**< Module acts as a HDCP 2.2 transmitter. */
	XHDCP22_TX_REPEATER,                /**< Module acts as a HDCP 2.2 repeater.  */
	XHDCP22_TX_CONVERTER                /**< Module acts as a HDCP 2.2 converter.  */
} XHdcp22_Tx_Mode;

/**
* These constants are used to identify fields inside the topology structure
*/
typedef enum {
	XHDCP22_TX_TOPOLOGY_DEPTH,
	XHDCP22_TX_TOPOLOGY_DEVICECNT,
	XHDCP22_TX_TOPOLOGY_MAXDEVSEXCEEDED,
	XHDCP22_TX_TOPOLOGY_MAXCASCADEEXCEEDED,
	XHDCP22_TX_TOPOLOGY_HDCP20REPEATERDOWNSTREAM,
	XHDCP22_TX_TOPOLOGY_HDCP1DEVICEDOWNSTREAM,
	XHDCP22_TX_TOPOLOGY_INVALID
} XHdcp22_Tx_TopologyField;

/**
* Callback type for status.
*
* @param  CallbackRef is a callback reference passed in by the upper
*         layer when setting the callback functions, and passed back to
*         the upper layer when the callback is invoked.
*
* @return None.
*
* @note   None.
*
*/
typedef void (*XHdcp22_Tx_Callback)(void *CallbackRef);

/**
* This typedef contains configuration information for the device.
*/
typedef struct
{
	/** DeviceId is the unique ID of the device. */
	u16 DeviceId;
	/** Base Address is the physical base address of the device's registers. */
	UINTPTR BaseAddress;
	/** HDMI or DP (Always HDCP22_TX_HDMI: Currently DP is not supported). */
	int Protocol;
	/** Future expansion. */
	int Mode;
	/** DeviceId of the internal used timer. */
	u16 TimerDeviceId;
	/** DeviceId of the used cipher. */
	u16 CipherId;
	/** Device Id of the random generator. */
	u16 RngId;
} XHdcp22_Tx_Config;

/**
 * The current state and data for internal used timer.
 */
typedef struct
{
	/** Expiration flag set when the hardware timer has interrupted. */
	u8 TimerExpired;
	/** Keep track of why the timer was started (message or status checking). */
	u8 ReasonId;
	/** Keep track of the start value of the timer. */
	u32 InitialTicks;
	/** The hardware timer instance.*/
	XTmrCtr TmrCtr;
} XHdcp22_Tx_Timer;

/**
* This typedef contains the the used keys used for authentication with stored Km.
*/
typedef struct {
	u8 ReceiverId[5];    /**< Unique receiver Id. */
	u8 RxCaps[3];        /**< Capabilities of the receiver. */
	u8 Rtx[8];           /**< Random nonce for tx. */
	u8 Rrx[8];           /**< Random nonce for Rx (m: Rtx || Rrx). */
	u8 Km[16];           /**< Km. */
	u8 Ekh_Km[16];       /**< Ekh(Km). */
     u8 Ready;            /**< Indicates a valid entry */
} XHdcp22_Tx_PairingInfo;
/**
* This typedef contains information about the HDCP22 transmitter.
*/
typedef struct
{
	XHdcp22_Tx_Protocol Protocol;       /**< Copy of configuration setting Protocol.*/

	/* state handling. */
	XHdcp22_Tx_StateType CurrentState;  /**< Current state of the internal state machine. */
	XHdcp22_Tx_StateType PrvState;      /**< Previous state of the internal state machine. */
	u8 Rtx[8];                          /**< Internal used Rtx. */
	u8 Rrx[8];                          /**< Internal used Rrx. */
	u8 Rn[8];                           /**< Internal used Rn. */
	void *StateContext;                 /**< Context used internally by the state machine. */
	u16  LocalityCheckCounter;          /**< Locality may attempt 1024 times. */
	u8 MsgAvailable;                    /**< Message is available for reading. */

	XHdcp22_Tx_PairingInfo PairingInfo[XHDCP22_TX_MAX_STORED_PAIRINGINFO];
	/** The result after a call to #XHdcp22Tx_Poll. */
	XHdcp22_Tx_AuthenticationType AuthenticationStatus;

	/** Content stream type used with Content Stream Management */
	XHdcp22_Tx_ContentStreamType ContentStreamType;

	/** Sequence number M used with Content Stream Management */
	u32 SeqNum_M;

	/** Indicates if the first seq_num_M value is sent */
	u8 SentFirstSeqNum_M;

	/** Calculated M value */
	u8 M[32];

	/** Is topology info available */
	u8 IsTopologyAvailable;

	/** Content stream type is sent */
	u8 IsContentStreamTypeSent;

	/** Content stream type is set */
	u8 IsContentStreamTypeSet;

	/** Keeps track of the number of Content Stream Management checks performed */
	u16 ContentStreamManageCheckCounter;

	/** Content stream management failed */
	u8 ContentStreamManageFailed;

	/** Indicates if the first seq_num_V value is received */
	u8 ReceivedFirstSeqNum_V;

	/** Is re-authentication requested by HDCP 2.2 RX. */
	u8 ReAuthenticationRequested;

	/** HDCP RX status read on timer interrupt. */
	u16 RxStatus;

	/** Is HDCP TX enabled (state machine is active). */
	u8 IsEnabled;

	/** Is the receiver a HDCP 2.2 type. */
	u8 IsReceiverHDCP2Capable;

	/** Is the receiver a HDCP repeater */
	u8 IsReceiverRepeater;

	/** Is revocation list valid */
	u8 IsRevocationListValid;

	/** Is a device listed in the revocation list */
	u8 IsDeviceRevoked;

	/** The currently used polling value see also #XHDCP22_TX_DEFAULT_RX_STATUS_POLLVALUE. */
	u32 PollingValue;

	/** Authentication request count */
	u32 AuthRequestCnt;

	/** Re-authentication request count */
	u32 ReauthRequestCnt;
}XHdcp22_Tx_Info;

/**
 * This typedef is used to store logging events.
 */
typedef struct {
	u8  LogEvent;       /**< Event that has been triggered. */
	u16  Data;          /**< Optional data. */
	u32 TimeStamp;      /**< Time stamp on when event occurred. Only used for time critical events. */
} XHdcp22_Tx_LogItem;

/**
* This typedef contains the HDCP22 log list.
*/
typedef struct {
	XHdcp22_Tx_LogItem LogItems[XHDCP22_TX_LOG_BUFFER_SIZE]; /**< Data. */
	u16 Tail;                         /**< Tail pointer. */
	u16 Head;                         /**< Head pointer. */
	u8 Verbose;                       /**< Logging is extended with debug events. */
} XHdcp22_Tx_Log;

/**
* This typedef contains the HDCP22 test parameters and settings.
*/
typedef struct {
	u32 TestMode;                 /**< Current used test mode. */
	u32 TestFlags;                /**< Current used test flags. */
	u8 CurrentDdcAddress;         /**< Current DDC address by the testing framework. */
} XHdcp22_Tx_Test;

/**
* This structure contains the HDCP2 Revocation information.
*/
typedef struct
{
	u32 NumDevices;
	u8  ReceiverId[XHDCP22_TX_REVOCATION_LIST_MAX_DEVICES][5];
} XHdcp22_Tx_RevocationList;

/**
* This structure contains the HDCP topology information.
*/
typedef struct
{
	/** Receiver ID list of all downstream devices. The list consists of
	a contiguous set of bytes stored in big-endian order. */
	u8  ReceiverId[32][5];

	/** Repeater cascade depth. This value gives the number of attached
	    levels through the connection topology. */
	u8  Depth;

	/** Total number of connected downstream devices. */
	u8  DeviceCnt;

	/** Flag used to indicate topology error. When set to one, more
	    than 31 devices are attached to a downstream repeater. */
	u8  MaxDevsExceeded;

	/** Flag used to indicate topology error. When set to one, more
	    than four levels of repeaters have been cascaded together. */
	u8  MaxCascadeExceeded;

	/** Flag used to indicate topology information. When set to one,
	    indicates presence of an HDCP2.0-compliant Repeater in the topology. */
	u8  Hdcp20RepeaterDownstream;

	/** Flag used to indicate topology information. When set to one,
	    indicates presence of an HDCP1.x-compliant device in the topology. */
	u8  Hdcp1DeviceDownstream;
} XHdcp22_Tx_Topology;

/**
* Callback type used for calling DDC read and write functions.
*
* @param  DeviceAddress is the (i2c) device address of the HDCP port.
* @param  ByteCount is the amount of data bytes in the buffer to read or write.
* @param  BufferPtr is a pointer to a buffer that is used for reading or writing.
* @param  Stop is a flag to control if a stop token is set or not.
* @param  RefPtr is a callback reference passed in by the upper layer when setting
*         the DDC reading and writing functions, and passed back to the upper layer when
*         the callback is invoked.
*
* @return
*         - XST_SUCCESS The read action was successful.
*         - XST_FAILURE The read action failed.
*
* @note   None.
*
*/
typedef int (*XHdcp22_Tx_DdcHandler)(u8 DeviceAddress, u16 ByteCount, u8* BufferPtr,
             u8 Stop, void *RefPtr);

/** Callback type used for pointer to single input function */
typedef void (*XHdcp22_Tx_Callback)(void *HandlerRef);


/**
* The XHdcpTx driver instance data. An instance must be allocated for each
* HDCP TX core in use.
*/
typedef struct
{
	/** Config */
	XHdcp22_Tx_Config Config;
	/** Is the component ready for usage. */
	u32 IsReady;

	/** Is the attached receiver HDCP2 capable. */
	u8 IsReceiverHDCP2Capable;

	/** Function pointer for reading DDC (Rx HDCP DeviceAddress: 0x74)
	    using the XHdcp22_Tx_Ddc stucture as parameter. */
	XHdcp22_Tx_DdcHandler DdcRead;
	/** Set if DdcRead handler is defined. */
	u8 IsDdcReadSet;

	/** Function pointer for writing DDC (Rx HDCP DeviceAddress: 0x74)
	    using the XHdcp22_Tx_Ddc stucture as parameter. */
	XHdcp22_Tx_DdcHandler DdcWrite;
	/** Set if DdcWrite handler is defined. */
	u8 IsDdcWriteSet;

	/** Reference pointer set with #XHdcp22Tx_SetCallback function. */
	void *DdcHandlerRef;

	/** Function pointer called after successful authentication */
	XHdcp22_Tx_Callback AuthenticatedCallback;
	/** Set if AuthenticatedCallback handler is defined. */
	u8 IsAuthenticatedCallbackSet;
	void *AuthenticatedCallbackRef;

	/** Function pointer called after authentication failure */
	XHdcp22_Tx_Callback UnauthenticatedCallback;
	/** Set if UnauthenticatedCallback handler is defined. */
	u8 IsUnauthenticatedCallbackSet;
	void *UnauthenticatedCallbackRef;

	/** Function pointer called after the downstream topology is available */
	XHdcp22_Tx_Callback DownstreamTopologyAvailableCallback;
	/** Set if DownstreamTopologyAvailableCallback handler is defined. */
	u8 IsDownstreamTopologyAvailableCallbackSet;
	void *DownstreamTopologyAvailableCallbackRef;

	/** Internal used timer. */
	XHdcp22_Tx_Timer Timer;

	/** Internal used hardware random number generator. */
	XHdcp22_Rng Rng;

	/** Internal used cipher. */
	XHdcp22_Cipher Cipher;

	/** Info. */
	XHdcp22_Tx_Info Info;

	/** Logging. */
	XHdcp22_Tx_Log Log;

	/** Message buffer for messages that are sent/received. */
	u8 MessageBuffer[XHDCP22_TX_MAX_MESSAGE_SIZE];

	/** Revocation List. */
	XHdcp22_Tx_RevocationList RevocationList;

	/** Topology info. */
	XHdcp22_Tx_Topology Topology;

#ifdef _XHDCP22_TX_TEST_
	/** Testing. */
	XHdcp22_Tx_Test Test;
#endif
}XHdcp22_Tx;


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/

/* Initialization function in xhdcp22_tx_sinit.c */
XHdcp22_Tx_Config  *XHdcp22Tx_LookupConfig (u16 DeviceId);

/* Initialization and control functions in xhdcp_tx.c */
int XHdcp22Tx_CfgInitialize(XHdcp22_Tx *InstancePtr, XHdcp22_Tx_Config *CfgPtr,
                            UINTPTR EffectiveAddr);
int XHdcp22Tx_Reset(XHdcp22_Tx *InstancePtr);
int XHdcp22Tx_ClearPairingInfo(XHdcp22_Tx *InstancePtr);
int XHdcp22Tx_Authenticate (XHdcp22_Tx *InstancePtr);
int XHdcp22Tx_Poll(XHdcp22_Tx *InstancePtr);
int XHdcp22Tx_Enable (XHdcp22_Tx *InstancePtr);
int XHdcp22Tx_Disable (XHdcp22_Tx *InstancePtr);
int XHdcp22Tx_EnableEncryption (XHdcp22_Tx *InstancePtr);
int XHdcp22Tx_DisableEncryption (XHdcp22_Tx *InstancePtr);
void XHdcp22Tx_EnableBlank (XHdcp22_Tx *InstancePtr);
void XHdcp22Tx_DisableBlank (XHdcp22_Tx *InstancePtr);
u8  XHdcp22Tx_IsEnabled (XHdcp22_Tx *InstancePtr);
u8  XHdcp22Tx_IsEncryptionEnabled (XHdcp22_Tx *InstancePtr);
u8  XHdcp22Tx_IsInProgress (XHdcp22_Tx *InstancePtr);
u8  XHdcp22Tx_IsAuthenticated (XHdcp22_Tx *InstancePtr);
u8  XHdcp22Tx_IsDwnstrmCapable (XHdcp22_Tx *InstancePtr);
u32 XHdcp22Tx_GetVersion(XHdcp22_Tx *InstancePtr);

/* Set DDC handler function pointers. */
int XHdcp22Tx_SetCallback(XHdcp22_Tx *InstancePtr,
                          XHdcp22_Tx_HandlerType HandlerType,
                          void *CallbackFunc, void *CallbackRef);

/* Return the internal timer instance. */
XTmrCtr* XHdcp22Tx_GetTimer(XHdcp22_Tx *InstancePtr);

/* Logging and testing */
void XHdcp22Tx_LogReset(XHdcp22_Tx *InstancePtr, u8 Verbose);
void XHdcp22Tx_LogWr(XHdcp22_Tx *InstancePtr, XHdcp22_Tx_LogEvt Evt, u16 Data);
XHdcp22_Tx_LogItem* XHdcp22Tx_LogRd(XHdcp22_Tx *InstancePtr);
int XHdcp22Tx_LogShow(XHdcp22_Tx *InstancePtr, char *buff, int buff_size);
u32  XHdcp22Tx_LogGetTimeUSecs(XHdcp22_Tx *InstancePtr);
void XHdcp22Tx_SetMessagePollingValue(XHdcp22_Tx *InstancePtr, u32 PollingValue);
void XHdcp22Tx_Info(XHdcp22_Tx *InstancePtr);

/* Functions for loading authentication constants */
void XHdcp22Tx_LoadLc128(XHdcp22_Tx *InstancePtr, const u8 *Lc128Ptr);

/* SRM and revocation */
int XHdcp22Tx_LoadRevocationTable(XHdcp22_Tx *InstancePtr, const u8 *SrmPtr);
XHdcp22_Tx_RevocationList* XHdcp22Tx_GetRevocationReceiverIdList(XHdcp22_Tx *InstancePtr);
u8 XHdcp22Tx_IsDeviceRevoked(XHdcp22_Tx *InstancePtr, u8 *RecvIdPtr);

/* Functions for repeater downstream interface */
XHdcp22_Tx_Topology *XHdcp22Tx_GetTopology(XHdcp22_Tx *InstancePtr);
u8 *XHdcp22Tx_GetTopologyReceiverIdList(XHdcp22_Tx *InstancePtr);
u32 XHdcp22Tx_GetTopologyField(XHdcp22_Tx *InstancePtr, XHdcp22_Tx_TopologyField Field);
u8 XHdcp22Tx_IsRepeater(XHdcp22_Tx *InstancePtr);
void XHdcp22Tx_SetRepeater(XHdcp22_Tx *InstancePtr, u8 Set);
void XHdcp22Tx_SetContentStreamType(XHdcp22_Tx *InstancePtr, XHdcp22_Tx_ContentStreamType StreamType);

/************************** Variable Declarations ****************************/

#ifdef __cplusplus
}
#endif

#endif /* End of protection macro */

/** @} */
