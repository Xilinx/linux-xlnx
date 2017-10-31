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
* @file xhdcp22_rx.h
* @addtogroup hdcp22_rx_v2_0
* @{
* @details
*
* This is the main header file for the Xilinx HDCP 2.2 Receiver device
* driver. The HDCP 2.2 Receiver driver implements the authentication
* state machine.
* It consists of:
* - A state machine handling the states as specified in the HDCP revision 2.2
*   specification.
* - Message handling from/to the HDCP 2.2 transmitter.
* - Logging functionality including time stamps.
*
* <b>Software Initialization and Configuration</b>
*
* The application needs to do the following steps to run the Receiver.
* - Call XHdcp22Rx_LookupConfig using the device ID to find the
*   core configuration instance.
* - Call XHdcp22Rx_CfgInitialize to intitialize the device instance.
* - Call XHdcp22Rx_SetCallback to set the pointers to the callback
*   functions defined by the enumerated type XHdcp22_Rx_HandlerType.
* - Call XHdcp22Rx_LoadPublicCert to load the DCP public certificate.
* - Call XHdcp22Rx_LoadPrivateKey to load the RSA private key.
* - Call XHdcp22Rx_LoadLc128 to load the DCP global constant.
* - Call XHdcp22Rx_LogReset to reset the log buffer.
* - The following functions should be called in the interfacing
*   protocol driver (i.e. HDMI) to set event flags:
*   - XHdcp22Rx_SetLinkError
*   - XHdcp22Rx_SetDdcError
*   - XHdcp22Rx_SetWriteMessageAvailable
*   - XHdcp22Rx_SetReadMessageComplete
* - Call XHdcp22Rx_Enable to enable the state machine.
* - Call XHdcp22Rx_Poll to run the Receiver state machine. The
*   call to this function is non-blocking and should be called
*   repeatedly in a spin loop as long as the receiver is active.
*
* <b>Interrupts</b>
*
* None.
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
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00  MH   10/30/15 First Release
* 1.01  MH   01/15/16 Added XHdcp22Rx_SetDdcReauthReq to function prototypes.
*                     Replaced function XHdcp22Rx_SetDdcHandles with
*                     XHdcp22Rx_SetCallback.
* 1.02  MH   03/02/16 Updated to change NPrimeP and NPrimeQ from pointer
*                     to array. Added function XHDCP22Rx_GetVersion.
* 2.00  MH   04/14/16 Updated for repeater upstream support.
* 2.01  MH   02/28/17 Fixed compiler warnings.
*</pre>
*
*****************************************************************************/

#ifndef XHDCP22_RX_H		/* prevent circular inclusions */
#define XHDCP22_RX_H		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files ********************************/
#include "xil_types.h"
#include "xil_assert.h"
#include "xstatus.h"
#include "xdebug.h"
#include "xtmrctr.h"
#include "xhdcp22_rng.h"
#include "xhdcp22_mmult.h"
#include "xhdcp22_cipher.h"

/************************** Constant Definitions ****************************/
#define XHDCP22_RX_MAX_MESSAGE_SIZE           534  /**< Maximum message size */
#define XHDCP22_RX_LOG_BUFFER_SIZE            256  /**< The size of the log buffer */
#define XHDCP22_RX_ENCRYPTION_STATUS_INTERVAL 1000 /**< 1sec encryption status check interval */
#define XHDCP22_RX_REPEATERAUTH_ACK_INTERVAL  2000 /**< 2sec RepeaterAuth Ack interval */

/************************** Variable Declaration ****************************/

/**************************** Type Definitions ******************************/
/** Type for pointer to state function */
typedef void *(*XHdcp22_Rx_StateFunc)(void *InstancePtr);
/** Type for pointer to single input function */
typedef void (*XHdcp22_Rx_RunHandler)(void *HandlerRef);
/** Type for pointer to two input function */
typedef void (*XHdcp22_Rx_SetHandler)(void *HandlerRef, u32 Data);
/** Type for pointer to single input function with a return value */
typedef u32  (*XHdcp22_Rx_GetHandler)(void *HandlerRef);

/**
 * These constants are used to define the protocol.
 */
typedef enum
{
	XHDCP22_RX_HDMI, /**< HDCP22 over HDMI */
	XHDCP22_RX_DP    /**< HDCP22 over DP, Not yet supported */
} XHdcp22_Rx_Protocol;

/**
 * These constants are used to define the mode.
 */
typedef enum
{
	XHDCP22_RX_RECEIVER,  /**< HDCP22 receiver */
	XHDCP22_RX_REPEATER,  /**< HDCP22 repeater upstream interface */
	XHDCP22_RX_CONVERTER  /**< HDCP22 converter upstream interface */
} XHdcp22_Rx_Mode;

/**
 * These constants are used to identify callback functions.
 */
typedef enum
{
	XHDCP22_RX_HANDLER_UNDEFINED,             /**< Undefined */
	XHDCP22_RX_HANDLER_DDC_SETREGADDR,        /**< Set the DDC register address */
	XHDCP22_RX_HANDLER_DDC_SETREGDATA,        /**< Set the DDC register data */
	XHDCP22_RX_HANDLER_DDC_GETREGDATA,        /**< Get the DDC register data */
	XHDCP22_RX_HANDLER_DDC_GETWBUFSIZE,       /**< Get the DDC the write buffer size */
	XHDCP22_RX_HANDLER_DDC_GETRBUFSIZE,       /**< Get the DDC the read buffer size */
	XHDCP22_RX_HANDLER_DDC_ISWBUFEMPTY,       /**< Check if the DDC write buffer is empty */
	XHDCP22_RX_HANDLER_DDC_ISRBUFEMPTY,       /**< Check if the DDC read buffer is empty */
	XHDCP22_RX_HANDLER_DDC_CLEARRBUF,         /**< Clear the DDC read buffer */
	XHDCP22_RX_HANDLER_DDC_CLEARWBUF,         /**< Clear the DDC write buffer */
	XHDCP22_RX_HANDLER_AUTHENTICATED,         /**< Executed when state machine transitions to authenticated */
	XHDCP22_RX_HANDLER_UNAUTHENTICATED,       /**< Executed when state machine transitions to unauthenticated */
	XHDCP22_RX_HANDLER_AUTHENTICATION_REQUEST,/**< Executed when authentication request is received */
	XHDCP22_RX_HANDLER_TOPOLOGY_UPDATE,       /**< Executed when state machine is ready for the topology update. */
	XHDCP22_RX_HANDLER_STREAM_MANAGE_REQUEST, /**< Executed when stream management request is received. */
	XHDCP22_RX_HANDLER_ENCRYPTION_UPDATE,     /**< Executed when encryption status update. */
	XHDCP22_RX_HANDLER_INVALID                /**< Invalid */
} XHdcp22_Rx_HandlerType;

/**
 * These constants are the authentication and key exchange states.
 */
typedef enum
{
	XHDCP22_RX_STATE_UNDEFINED                    = 0x000,  /**< Undefined */
	XHDCP22_RX_STATE_B0_WAIT_AKEINIT              = 0xB00,  /**< Unauthenticated */
	XHDCP22_RX_STATE_B1_SEND_AKESENDCERT          = 0xB10,  /**< Compute Km: Send AKE_Send_Cert */
	XHDCP22_RX_STATE_B1_WAIT_AKEKM                = 0xB11,  /**< Compute Km: Wait for AKE_No_Stored_km or AKE_Stored_km */
	XHDCP22_RX_STATE_B1_SEND_AKESENDHPRIME        = 0xB12,  /**< Compute Km: Send AKE_Send_H_prime */
	XHDCP22_RX_STATE_B1_SEND_AKESENDPAIRINGINFO   = 0xB13,  /**< Compute Km: Send AKE_Send_Pairing_Info */
	XHDCP22_RX_STATE_B1_WAIT_LCINIT               = 0xB14,  /**< Compute Km: Wait for LCInit */
	XHDCP22_RX_STATE_B2_SEND_LCSENDLPRIME         = 0xB20,  /**< Compute L': Send LC_Send_L_prime */
	XHDCP22_RX_STATE_B2_WAIT_SKESENDEKS           = 0xB21,  /**< Compute L': Wait for SKE_Send_Eks */
	XHDCP22_RX_STATE_B3_COMPUTE_KS                = 0xB30,  /**< Compute Ks */
	XHDCP22_RX_STATE_B4_AUTHENTICATED             = 0xB40,  /**< Receiver Authenticated */
	XHDCP22_RX_STATE_C4_WAIT_FOR_DOWNSTREAM       = 0xC40,  /**< Wait for Downstream */
	XHDCP22_RX_STATE_C5_SEND_RECEIVERIDLIST       = 0xC50,  /**< Assemble Receiver ID List: Send RepeaterAuth_Send_ReceiverID_List */
	XHDCP22_RX_STATE_C5_SEND_RECEIVERIDLIST_DONE  = 0xC51,  /**< Assemble Receiver ID List: Send RepeaterAuth_Send_ReceiverID_List Done */
	XHDCP22_RX_STATE_C6_VERIFY_RECEIVERIDLISTACK  = 0xC60,  /**< Verify Receiver ID List Acknowledgement*/
	XHDCP22_RX_STATE_C7_WAIT_STREAM_MANAGEMENT    = 0xC70,  /**< Content Stream Management: Wait for RepeaterAuth_Stream_Manage */
	XHDCP22_RX_STATE_C7_SEND_STREAM_READY         = 0xC71,  /**< Content Stream Management: Send RepeaterAuth_Stream_Ready */
	XHDCP22_RX_STATE_C7_SEND_STREAM_READY_DONE    = 0xC72,  /**< Content Stream Management: Send RepeaterAuth_Stream_Ready Done */
	XHDCP22_RX_STATE_C8_AUTHENTICATED             = 0xC80,  /**< Repeater Authenticated */
	XHDCP22_RX_STATE_INVALID                                /**< Invalid */
} XHdcp22_Rx_StateType;

/**
 * These constants define the authentication status.
 */
typedef enum
{
	XHDCP22_RX_UNAUTHENTICATED,         /**< Unauthenticated */
	XHDCP22_RX_AUTHENTICATION_BUSY,     /**< Authentication Busy */
	XHDCP22_RX_AUTHENTICATED,           /**< Authenticated */
	XHDCP22_RX_REAUTHENTICATE_REQUESTED /**< Reauthentication requested */
} XHdcp22_Rx_AuthenticationType;

/**
* These constants are the general logging events.
*/
typedef enum {
	XHDCP22_RX_LOG_EVT_NONE,          /** Log Event None */
	XHDCP22_RX_LOG_EVT_INFO,          /** Log General Info Event */
	XHDCP22_RX_LOG_EVT_INFO_STATE,    /** Log State Info Event */
	XHDCP22_RX_LOG_EVT_INFO_MESSAGE,  /** Log Messsage Info Event */
	XHDCP22_RX_LOG_EVT_DEBUG,         /** Log Debug Event */
	XHDCP22_RX_LOG_EVT_ERROR,         /** Log Error Event */
	XHDCP22_RX_LOG_EVT_USER,          /** User logging */
	XHDCP22_RX_LOG_EVT_INVALID        /** Last value the list, only used for checking */
} XHdcp22_Rx_LogEvt;

/**
* These constants are used to identify fields inside the topology structure
*/
typedef enum {
	XHDCP22_RX_TOPOLOGY_DEPTH,
	XHDCP22_RX_TOPOLOGY_DEVICECNT,
	XHDCP22_RX_TOPOLOGY_MAXDEVSEXCEEDED,
	XHDCP22_RX_TOPOLOGY_MAXCASCADEEXCEEDED,
	XHDCP22_RX_TOPOLOGY_HDCP20REPEATERDOWNSTREAM,
	XHDCP22_RX_TOPOLOGY_HDCP1DEVICEDOWNSTREAM,
	XHDCP22_RX_TOPOLOGY_INVALID
} XHdcp22_Rx_TopologyField;

/**
 * This typedef is the test DDC register definition.
 */
typedef struct
{
	u8 Address;
	u8 Name[20];
	int Access;
	u8 Value;
} XHdcp22_Rx_TestDdcReg;

/**
 * This typedef is the test structure used for standalone driver testing.
 */
typedef struct
{
	int                    TestFlag;
	int                    TestMode;
	int                    TestReceiver;
	int                    State;
	int                    *NextStateVector;
	int                    TestReturnCode;
	u32                    NextStateOffset;
	u32                    NextStateSize;
	u32                    NextStateStatus;
	XHdcp22_Rx_TestDdcReg  DdcRegisterMap[5];
	u32                    DdcRegisterMapAddress;
	u32                    DdcRegisterAddress;
	u8                     WriteMessageBuffer[XHDCP22_RX_MAX_MESSAGE_SIZE];
	u32                    WriteMessageSize;
	u32                    WriteMessageOffset;
	u8                     ReadMessageBuffer[XHDCP22_RX_MAX_MESSAGE_SIZE];
	u32                    ReadMessageSize;
	u32                    ReadMessageOffset;
	u32                    WaitCounter;
	u8                     Rrx[8];
	u8                     RxCaps[3];
	u8                     Verbose;
} XHdcp22_Rx_Test;

/**
 * This typedef is used to store handles to function pointers
 */
typedef struct
{
	/** Function pointer used to set the DDC register address */
	XHdcp22_Rx_SetHandler DdcSetAddressCallback;
	/** To be passed to callback function */
	void                  *DdcSetAddressCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsDdcSetAddressCallbackSet;

	/** Function pointer used to set the DDC register data */
	XHdcp22_Rx_SetHandler DdcSetDataCallback;
	/** To be passed to callback function */
	void                  *DdcSetDataCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsDdcSetDataCallbackSet;

	/** Function pointer used to get the DDC register data */
	XHdcp22_Rx_GetHandler DdcGetDataCallback;
	/** To be passed to callback function */
	void                  *DdcGetDataCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsDdcGetDataCallbackSet;

	/** Function pointer used to get the DDC write buffer size */
	XHdcp22_Rx_GetHandler DdcGetWriteBufferSizeCallback;
	/** To be passed to callback function */
	void                  *DdcGetWriteBufferSizeCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsDdcGetWriteBufferSizeCallbackSet;

	/** Function pointer used to get the DDC read buffer size */
	XHdcp22_Rx_GetHandler DdcGetReadBufferSizeCallback;
	/** To be passed to callback function */
	void                  *DdcGetReadBufferSizeCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsDdcGetReadBufferSizeCallbackRefSet;

	/** Function pointer used to check if the DDC write buffer is empty */
	XHdcp22_Rx_GetHandler DdcIsWriteBufferEmptyCallback;
	/** To be passed to callback function */
	void                  *DdcIsWriteBufferEmptyCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsDdcIsWriteBufferEmptyCallbackSet;

	/** Function pointer used to check if the DDC read buffer is empty */
	XHdcp22_Rx_GetHandler DdcIsReadBufferEmptyCallback;
	/** To be passed to callback function */
	void                  *DdcIsReadBufferEmptyCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsDdcIsReadBufferEmptyCallbackSet;

	/** Function pointer used to clear the DDC read buffer */
	XHdcp22_Rx_RunHandler DdcClearReadBufferCallback;
	/** To be passed to callback function */
	void                  *DdcClearReadBufferCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsDdcClearReadBufferCallbackSet;

	/** Function pointer used to clear the DDC write buffer */
	XHdcp22_Rx_RunHandler DdcClearWriteBufferCallback;
	/** To be passed to callback function */
	void                  *DdcClearWriteBufferCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsDdcClearWriteBufferCallbackSet;

	/** Function pointer used to set auto clear on the DDC RxStatus READY bit*/
	XHdcp22_Rx_RunHandler DdcClearRepeaterReadyCallback;
	/** To be passed to callback function */
	void                  *DdcClearRepeaterReadyCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsDdcClearRepeaterReadyCallbackSet;

	/** This flag is set true when all the DDC callbacks have been registered */
	u8                    IsDdcAllCallbacksSet;

	/** Function pointer called when statemachine transitions to authenticated */
	XHdcp22_Rx_RunHandler AuthenticatedCallback;
	/** To be passed to callback function */
	void                  *AuthenticatedCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsAuthenticatedCallbackSet;

	/** Function pointer called when statemachine transitions to unauthenticated */
	XHdcp22_Rx_RunHandler UnauthenticatedCallback;
	/** To be passed to callback function */
	void                  *UnauthenticatedCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsUnauthenticatedCallbackSet;

	/** Function pointer called when authentication request message is received */
	XHdcp22_Rx_RunHandler AuthenticationRequestCallback;
	/** To be passed to callback function */
	void                  *AuthenticationRequestCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsAuthenticationRequestCallbackSet;

	/** Function pointer called when stream management message is received */
	XHdcp22_Rx_RunHandler StreamManageRequestCallback;
	/** To be passed to callback function */
	void                  *StreamManageRequestCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsStreamManageRequestCallbackSet;

	/** Function pointer called when repeater is ready to for a topology update */
	XHdcp22_Rx_RunHandler TopologyUpdateCallback;
	/** To be passed to callback function */
	void                  *TopologyUpdateCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsTopologyUpdateCallbackSet;

	/** Function pointer called when encryption status has changed */
	XHdcp22_Rx_RunHandler EncryptionStatusCallback;
	/** To be passed to callback function */
	void                  *EncryptionStatusCallbackRef;
	/** This flag is set true when the callback has been registered */
	u8                    IsEncryptionStatusCallbackSet;
} XHdcp22_Rx_Handles;

/**
 * This typedef is used to store temporary parameters for computations
 */
typedef struct
{
	u8 Rtx[8];
	u8 TxCaps[3];
	u8 Rrx[8];
	u8 RxCaps[3];
	u8 Km[16];
	u8 Rn[8];
	u8 HPrime[32];
	u8 LPrime[32];
	u8 EKh[16];
	u8 Riv[8];
	u8 Ks[16];
	u8 VPrime[32];
	u8 SeqNumM[3];
	u8 StreamIdType[2];
	u8 MPrime[32];
} XHdcp22_Rx_Parameters;

/**
 * This typedef is used to store logging events.
 */
typedef struct {
	/** Event that has been triggered */
	u16 LogEvent;
	/** Optional data */
	u16 Data;
	/** Timestamp on when event occured. Only used for time critical events */
	u32 TimeStamp;
} XHdcp22_Rx_LogItem;

/**
* This typedef contains the HDCP22 log list.
*/
typedef struct {
	/** Data */
	XHdcp22_Rx_LogItem LogItems[XHDCP22_RX_LOG_BUFFER_SIZE];
	/** Tail pointer */
	u8 Tail;
	/** Head pointer */
	u8 Head;
	/** Logging is extended with debug events. */
	u8 Verbose;
} XHdcp22_Rx_Log;

/**
 * This typedef provides information about status of HDCP-RX authentication.
 */
typedef struct
{
	/** Flag indicates that device is enabled */
	u8  IsEnabled;
	/** Flag indicates that AKE_No_Stored_Km message was received */
	u8  IsNoStoredKm;
	/** Number of LC_Init attempts */
	u16 LCInitAttempts;
	/** Reauthentication request flag */
	u8  ReauthReq;
	/** Topology ready flag */
	u8  TopologyReady;
	/** Flag to capture error events that require service */
	u32 ErrorFlag;
	/** Flag to capture all error conditions persistently */
	u32 ErrorFlagSticky;
	/** Flag to capture DDC events */
	u32 DdcFlag;
	/** Expiration flag set when the hardware timer has interrupted. */
	u8  TimerExpired;
	/** Keep track of why the timer was started (message or status checking). */
	u8  TimerReasonId;
	/** Keep track of the start value of the timer. */
	u32 TimerInitialTicks;
	/** Counter for repeater topology upstream propogation */
	u32 SeqNumV;
	/** Flag indicated that content stream management info messsage has been processed */
	u8  HasStreamManagementInfo;
	/** State machine current state */
	XHdcp22_Rx_StateType CurrentState;
	/** State machine next state */
	XHdcp22_Rx_StateType NextState;
	/** State machine return state. Used for parallel execution of content stream
	    management and topology information propagation */
	XHdcp22_Rx_StateType ReturnState;
	/** Authentication status */
	XHdcp22_Rx_AuthenticationType AuthenticationStatus;
	/** Skip read, indicates that message has been read but not yet processed */
	u8 SkipRead;
	/** Flag used to indicate topology change. When set to one, indicates
	    that the topology table has been updated and is available for
	    upstream propagation. */
	u8 IsTopologyValid;
	/** Flag indicates encryption is enabled */
	u8 IsEncrypted;
	/** Authentication request count */
	u32 AuthRequestCnt;
	/** Re-authentication request count */
	u32 ReauthRequestCnt;
	/** Link error count */
	u32 LinkErrorCnt;
	/** Ddc error count */
	u32 DdcErrorCnt;
} XHdcp22_Rx_Info;

/**
 * This typedef is the repeater topology table used to communicate topology
 * information to the receiver upstream interface.
 */
typedef struct
{
	/** Receiver ID list of all downstream devices. The list consists of
	    a contiguous set of bytes stored in big-endian order, with each
	    Receiver ID occuping five bytes and a total of 31 Receiver IDs. */
	u8 ReceiverIdList[31][5];
	/** Repeater cascade depth. This value gives the number of attached
	    levels through the connection topology with maximum depth of 4. */
	u8 Depth;
	/** Total number of connected downstream devices. Always zero for
	    HDCP Receivers. This count does not include the HDCP Repeater
	    itself, but only devices downstream from the HDCP Repeater. */
	u8 DeviceCnt;
	/** Flag used to indicate topology error. When set to one, more
	    than 31 downstream devices are attached. */
	u8 MaxDevsExceeded;
	/** Flag used to indicate topology error. When set to one, more
	    than four levels of repeaters have been casaded together. */
	u8 MaxCascadeExceeded;
	/** Flag used to indicate topology information. When set to one,
	    indicates presence of an HDCP2.0-compliant Repeater in the topology. */
	u8 Hdcp20RepeaterDownstream;
	/** Flag used to indicate topology information. When set to one,
	    indicates precense of an HDCP1.x-compliant device in the topology. */
	u8 Hdcp1DeviceDownstream;
} XHdcp22_Rx_Topology;

/**
 * This typedef contains configuration information for the device.
 */
typedef struct
{
	/** Unique ID of device instance */
	u16 DeviceId;
	/** Base address of subsystem */
	UINTPTR BaseAddress;
	/** HDCP22 over specified protocol (i.e. hdmi) */
	int Protocol;
	/** HDCP22 mode (i.e. receiver, repeater, or converter) */
	int Mode;
	/** Timer device instance ID */
	u32 TimerDeviceId;
	/** Cipher device instance ID */
	u32 CipherDeviceId;
	/** Mongomery multiplier device instance ID */
	u32 MontMultDeviceId;
	/** Random number generator device instance ID */
	u32 RngDeviceId;
} XHdcp22_Rx_Config;

/**
 * The XHdcp driver instance data. The user is required to
 * allocate a variable of this type for every HDCP-RX device in the
 * system. A pointer to a variable of this type is then passed to the driver
 * API functions.
 */
typedef struct
{
	/** HDCP-RX config structure */
	XHdcp22_Rx_Config Config;
	/** Indicates device is initialized and ready */
	u32 IsReady;
	/** RxCaps set during initialization */
	u8 RxCaps[3];
	/** DCP public certificate pointer */
	const u8 *PublicCertPtr;
	/** RSA private key pointer */
	const u8 *PrivateKeyPtr;
	/** Montgomery NPrimeP array */
	u8 NPrimeP[64];
	/** Montgomery NPrimeQ array */
	u8 NPrimeQ[64];
	/** HDCP-RX authentication and key exchange info */
	XHdcp22_Rx_Info Info;
	/** HDCP-RX authentication and key exchange parameters */
	XHdcp22_Rx_Parameters Params;
	/** State function pointer */
	XHdcp22_Rx_StateFunc StateFunc;
	/** Message handles */
	XHdcp22_Rx_Handles Handles;
	/** Log instance */
	XHdcp22_Rx_Log Log;
	/** Montgomery multiplier instance */
	XHdcp22_mmult MmultInst;
	/** Timer instance */
	XTmrCtr TimerInst;
	/** Random number generator instance */
	XHdcp22_Rng RngInst;
	/** Cipher instance */
	XHdcp22_Cipher CipherInst;
	/** Message structure */
	u8 MessageBuffer[XHDCP22_RX_MAX_MESSAGE_SIZE];
	/** Message size */
	int MessageSize;
	/** Repeater topology instance */
	XHdcp22_Rx_Topology Topology;
#ifdef _XHDCP22_RX_TEST_
	/** Test instance */
	XHdcp22_Rx_Test Test;
#endif
} XHdcp22_Rx;

/***************** Macros (Inline Functions) Definitions ********************/

/************************** Function Prototypes *****************************/
/* Functions for initializing and running driver */
XHdcp22_Rx_Config *XHdcp22Rx_LookupConfig(u16 DeviceId);
int  XHdcp22Rx_CfgInitialize(XHdcp22_Rx *InstancePtr, XHdcp22_Rx_Config *ConfigPtr,
       UINTPTR EffectiveAddr);
int  XHdcp22Rx_Enable(XHdcp22_Rx *InstancePtr);
int  XHdcp22Rx_Disable(XHdcp22_Rx *InstancePtr);
int  XHdcp22Rx_Reset(XHdcp22_Rx *InstancePtr);
int  XHdcp22Rx_Poll(XHdcp22_Rx *InstancePtr);
int  XHdcp22Rx_SetCallback(XHdcp22_Rx *InstancePtr,
       XHdcp22_Rx_HandlerType HandlerType, void *CallbackFunc, void *CallbackRef);
u32  XHdcp22Rx_GetVersion(XHdcp22_Rx *InstancePtr);
XTmrCtr* XHdcp22Rx_GetTimer(XHdcp22_Rx *InstancePtr);

/* Functions for checking status */
u8   XHdcp22Rx_IsEnabled(XHdcp22_Rx *InstancePtr);
u8   XHdcp22Rx_IsEncryptionEnabled(XHdcp22_Rx *InstancePtr);
u8   XHdcp22Rx_IsInProgress(XHdcp22_Rx *InstancePtr);
u8   XHdcp22Rx_IsAuthenticated(XHdcp22_Rx *InstancePtr);
u8   XHdcp22Rx_IsError(XHdcp22_Rx *InstancePtr);
void XHdcp22Rx_Info(XHdcp22_Rx *InstancePtr);

/* Functions used in callback routines */
void XHdcp22Rx_SetLinkError(XHdcp22_Rx *InstancePtr);
void XHdcp22Rx_SetDdcError(XHdcp22_Rx *InstancePtr);
void XHdcp22Rx_SetWriteMessageAvailable(XHdcp22_Rx *InstancePtr);
void XHdcp22Rx_SetReadMessageComplete(XHdcp22_Rx *InstancePtr);

/* Functions for loading authentication constants */
void XHdcp22Rx_LoadLc128(XHdcp22_Rx *InstancePtr, const u8 *Lc128Ptr);
void XHdcp22Rx_LoadPublicCert(XHdcp22_Rx *InstancePtr, const u8 *PublicCertPtr);
int  XHdcp22Rx_LoadPrivateKey(XHdcp22_Rx *InstancePtr, const u8 *PrivateKeyPtr);

/* Functions for logging */
void XHdcp22Rx_LogReset(XHdcp22_Rx *InstancePtr, u8 Verbose);
u32  XHdcp22Rx_LogGetTimeUSecs(XHdcp22_Rx *InstancePtr);
void XHdcp22Rx_LogWr(XHdcp22_Rx *InstancePtr, u16 Evt, u16 Data);
XHdcp22_Rx_LogItem* XHdcp22Rx_LogRd(XHdcp22_Rx *InstancePtr);
int XHdcp22Rx_LogShow(XHdcp22_Rx *InstancePtr, char *buff, int buff_size);

/* Functions for repeater upstream interface */
void XHdcp22Rx_SetTopology(XHdcp22_Rx *InstancePtr, const XHdcp22_Rx_Topology *TopologyPtr);
void XHdcp22Rx_SetTopologyReceiverIdList(XHdcp22_Rx *InstancePtr, const u8 *ListPtr, u32 ListSize);
void XHdcp22Rx_SetTopologyField(XHdcp22_Rx *InstancePtr, XHdcp22_Rx_TopologyField Field, u8 Value);
void XHdcp22Rx_SetTopologyUpdate(XHdcp22_Rx *InstancePtr);
void XHdcp22Rx_SetRepeater(XHdcp22_Rx *InstancePtr, u8 Set);
u8   XHdcp22Rx_IsRepeater(XHdcp22_Rx *InstancePtr);
u8   XHdcp22Rx_GetContentStreamType(XHdcp22_Rx *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif /* XHDCP22_RX_H_ */

/** @} */
