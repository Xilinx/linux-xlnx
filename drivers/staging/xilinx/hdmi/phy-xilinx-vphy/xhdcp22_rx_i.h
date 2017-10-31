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
* @file xhdcp22_rx_i.h
* @addtogroup hdcp22_rx_v2_0
* @{
* @details
*
* This header file contains internal data types and functions declarations
* for the Xilinx HDCP 2.2 Receiver.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00  MH   10/30/15 First Release
* 1.01  MH   03/02/16 Moved prototype of XHdcp22Rx_CalcMontNPrime to
*                     to internal functions.
* 1.02  MH   04/14/16 Updated for repeater upstream support.
*</pre>
*
*****************************************************************************/

#ifndef XHDCP22_RX_I_H_		/* prevent circular inclusions */
#define XHDCP22_RX_I_H_		/* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files ********************************/
#include "xhdcp22_rx.h"
#include "xhdcp22_mmult.h"

/************************** Constant Definitions ****************************/

#define XHDCP22_RX_HASH_SIZE              32            /** Hash size in bytes */
#define XHDCP22_RX_N_SIZE                 128           /** Modulus size in bytes */
#define XHDCP22_RX_P_SIZE                 64            /** RSA private parameter size in bytes */
#define XHDCP22_RX_KM_SIZE                16            /** Km size in bytes */
#define XHDCP22_RX_EKH_SIZE               16            /** Ekh size size in bytes */
#define XHDCP22_RX_KD_SIZE                32            /** Kd size size in bytes */
#define XHDCP22_RX_HPRIME_SIZE            32            /** HPrime size size in bytes */
#define XHDCP22_RX_LPRIME_SIZE            32            /** LPrime size size in bytes */
#define XHDCP22_RX_RN_SIZE                8             /** Rn size size in bytes */
#define XHDCP22_RX_RIV_SIZE               8             /** Riv size size in bytes */
#define XHDCP22_RX_KS_SIZE                16            /** Ks size size in bytes */
#define XHDCP22_RX_AES_SIZE               16            /** AES size size in bytes */
#define XHDCP22_RX_RTX_SIZE               8             /** Rtx size size in bytes */
#define XHDCP22_RX_RRX_SIZE               8             /** Rrx size size in bytes */
#define XHDCP22_RX_TXCAPS_SIZE            3             /** TxCaps size size in bytes */
#define XHDCP22_RX_RXCAPS_SIZE            3             /** RxCaps size size in bytes */
#define XHDCP22_RX_CERT_SIZE              522           /** DCP certificate size in bytes */
#define XHDCP22_RX_PRIVATEKEY_SIZE        320           /** RSA private key size (64*5) in bytes */
#define XHDCP22_RX_LC128_SIZE             16            /** Lc128 global constant size in bytes */

#define XHDCP22_RX_RCVID_SIZE             5             /** Repeater ReceiverID size in bytes */
#define XHDCP22_RX_SEQNUMV_SIZE           3             /** Repeater seq_num_V size in bytes */
#define XHDCP22_RX_RXINFO_SIZE            2             /** Repeater RxInfo size in bytes */
#define XHDCP22_RX_VPRIME_SIZE            32            /** Repeater VPrime size in bytes */
#define XHDCP22_RX_SEQNUMM_SIZE           3             /** Repeater seq_num_M size in bytes */
#define XHDCP22_RX_MPRIME_SIZE            32            /** Repeater MPrime size in bytes */
#define XHDCP22_RX_STREAMID_SIZE          2             /** Repeater MPrime size in bytes */

#define XHDCP22_RX_MAX_SEQNUMV            ((1<<24)-1)   /** Repeater maximum seq_num_V count */
#define XHDCP22_RX_MAX_LCINIT             1024          /** Maximum LC_Init attempts */
#define XHDCP22_RX_MAX_DEVICE_COUNT       31            /** Repeater maximum devices */
#define XHDCP22_RX_MAX_DEPTH              4             /** Repeater maximum depth */

#define XHDCP22_RX_DDC_VERSION_REG        0x50          /** Address of DDC version regiser */
#define XHDCP22_RX_DDC_WRITE_REG          0x60          /** Address of DDC write message regiser */
#define XHDCP22_RX_DDC_RXSTATUS0_REG      0x70          /** Address of first DDC RxStatus register */
#define XHDCP22_RX_DDC_RXSTATUS1_REG      0x71          /** Address of second DDC RxStatus register */
#define XHDCP22_RX_DDC_READ_REG           0x80          /** Address of DDC read message regiser */
#define XHDCP22_RX_TMR_CTR_0              0             /** First timer counter, used for log timestamps */
#define XHDCP22_RX_TMR_CTR_1              1             /** Second timer counter, used for protocol timeout*/

#define XHDCP22_RX_TEST_DDC_REGMAP_SIZE   5             /** Size of ddc register map for testing */

/**************************** Type Definitions ******************************/

/**
 * These constants are the message identification codes.
 */
typedef enum
{
	XHDCP22_RX_MSG_ID_AKEINIT                   = 2,   /**< AKE_Init message ID */
	XHDCP22_RX_MSG_ID_AKESENDCERT               = 3,   /**< AKE_Send_Cert message ID */
	XHDCP22_RX_MSG_ID_AKENOSTOREDKM             = 4,   /**< AKE_No_Stored_km message ID */
	XHDCP22_RX_MSG_ID_AKESTOREDKM               = 5,   /**< AKE_Stored_km message ID */
	XHDCP22_RX_MSG_ID_AKESENDHPRIME             = 7,   /**< AKE_Send_H_prime message ID */
	XHDCP22_RX_MSG_ID_AKESENDPAIRINGINFO        = 8,   /**< AKE_Send_Pairing_Info message ID */
	XHDCP22_RX_MSG_ID_LCINIT                    = 9,   /**< LC_Init message ID */
	XHDCP22_RX_MSG_ID_LCSENDLPRIME              = 10,  /**< LC_Send_L_prime message ID */
	XHDCP22_RX_MSG_ID_SKESENDEKS                = 11,  /**< SKE_Send_Eks message ID */
	XHDCP22_RX_MSG_ID_REPEATERAUTHSENDRXIDLIST  = 12,  /**< RepeaterAuth_Send_ReceiverID_List message ID */
	XHDCP22_RX_MSG_ID_REPEATERAUTHSENDACK       = 15,  /**< RepeaterAuth_Send_Ack message ID */
	XHDCP22_RX_MSG_ID_REPEATERAUTHSTREAMMANAGE  = 16,  /**< RepeaterAuth_Stream_Manage message ID */
	XHDCP22_RX_MSG_ID_REPEATERAUTHSTREAMREADY   = 17   /**< RepeaterAuth_Stream_Ready message ID */
} XHdcp22_Rx_MessageIds;

/**
 * These constants define the error conditions encountered during authentication and key exchange.
 */
typedef enum
{
	XHDCP22_RX_ERROR_FLAG_NONE                                 = 0,     /**< No errors */
	XHDCP22_RX_ERROR_FLAG_MESSAGE_SIZE                         = 1,     /**< Message size error */
	XHDCP22_RX_ERROR_FLAG_FORCE_RESET                          = 2,     /**< Force reset after error */
	XHDCP22_RX_ERROR_FLAG_PROCESSING_AKEINIT                   = 4,     /**< AKE_Init message processing error */
	XHDCP22_RX_ERROR_FLAG_PROCESSING_AKENOSTOREDKM             = 8,     /**< AKE_No_Stored_km message processing error */
	XHDCP22_RX_ERROR_FLAG_PROCESSING_AKESTOREDKM               = 16,    /**< AKE_Stored_km message processing error */
	XHDCP22_RX_ERROR_FLAG_PROCESSING_LCINIT                    = 32,    /**< LC_Init message processing error */
	XHDCP22_RX_ERROR_FLAG_PROCESSING_SKESENDEKS                = 64,    /**< SKE_Send_Eks message processing error */
	XHDCP22_RX_ERROR_FLAG_PROCESSING_REPEATERAUTHSENDACK       = 128,   /**< RepeaterAuthSendAck message processing error */
	XHDCP22_RX_ERROR_FLAG_PROCESSING_REPEATERAUTHSTREAMMANAGE  = 256,   /**< RepeaterAuthStreamManage message processing error */
	XHDCP22_RX_ERROR_FLAG_LINK_INTEGRITY                       = 512,   /**< Link integrity check error */
	XHDCP22_RX_ERROR_FLAG_DDC_BURST                            = 1024,  /**< DDC message burst read/write error */
	XHDCP22_RX_ERROR_FLAG_MAX_LCINIT_ATTEMPTS                  = 2048,  /**< Maximum LC_Init attempts error */
	XHDCP22_RX_ERROR_FLAG_MAX_REPEATER_TOPOLOGY                = 4096,  /**< Maximum LC_Init attempts error */
	XHDCP22_RX_ERROR_FLAG_EMPTY_REPEATER_TOPOLOGY              = 8192   /**< Maximum LC_Init attempts error */
} XHdcp22_Rx_ErrorFlag;

/**
 * These constants defines the DDC flags used to determine when messages are available
 * in the write message buffer or when a message has been read out of the read message
 * buffer.
 */
typedef enum
{
	XHDCP22_RX_DDC_FLAG_NONE                = 0, /**< Clear DDC flag */
	XHDCP22_RX_DDC_FLAG_WRITE_MESSAGE_READY = 1, /**< Write message buffer ready to read */
	XHDCP22_RX_DDC_FLAG_READ_MESSAGE_READY  = 2  /**< Read message buffer ready to write */
} XHdcp22_Rx_DdcFlag;

/**
 * These constants are the detailed logging events.
 */
typedef enum
{
	XHDCP22_RX_LOG_INFO_RESET,                     /**< Reset event */
	XHDCP22_RX_LOG_INFO_ENABLE,                    /**< Enable event */
	XHDCP22_RX_LOG_INFO_DISABLE,                   /**< Disable event */
	XHDCP22_RX_LOG_INFO_REQAUTH_REQ,               /**< Reauthentication request */
	XHDCP22_RX_LOG_INFO_ENCRYPTION_ENABLE,         /**< Encryption enabled */
	XHDCP22_RX_LOG_INFO_TOPOLOGY_UPDATE,           /**< Topology update triggered */
	XHDCP22_RX_LOG_DEBUG_WRITE_MESSAGE_AVAILABLE,  /**< Write message available */
	XHDCP22_RX_LOG_DEBUG_READ_MESSAGE_COMPLETE,    /**< Read message complete */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_RSA,              /**< RSA decryption of Km computation start */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_RSA_DONE,         /**< RSA decryption of Km computation done */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_KM,               /**< Authentication Km computation start */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_KM_DONE,          /**< Authentication Km computation done */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_HPRIME,           /**< Authentication HPrime computation start */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_HPRIME_DONE,      /**< Authentication HPrime computation done */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_EKH,              /**< Pairing EKh computation start */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_EKH_DONE,         /**< Pairing Ekh computation done */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_LPRIME,           /**< Locality check LPrime computation start */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_LPRIME_DONE,      /**< Locality check LPrime computation done */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_KS,               /**< Session key exchange Ks computation start */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_KS_DONE,          /**< Session key exchange Ks computation done */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_VPRIME,           /**< Locality check VPrime computation start */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_VPRIME_DONE,      /**< Locality check VPrime computation start */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_MPRIME,           /**< Locality check MPrime computation start */
	XHDCP22_RX_LOG_DEBUG_COMPUTE_MPRIME_DONE,      /**< Locality check MPrime computation start */
	XHDCP22_RX_LOG_DEBUG_TIMER_START,              /**< Start protocol timer */
	XHDCP22_RX_LOG_DEBUG_TIMER_EXPIRED             /**< Timer expired */
} XHdcp22_Rx_LogData;

/**
 * These constants are used for setting up the desired unit test for standalong testing.
 */
typedef enum {
	XHDCP22_RX_TEST_FLAG_NONE,                           /**< No directed test */
	XHDCP22_RX_TEST_FLAG_NOSTOREDKM_WITH_RECEIVER,       /**< Directed test flag [No_Stored_km with Receiver] */
	XHDCP22_RX_TEST_FLAG_STOREDKM_WITH_RECEIVER,         /**< Directed test flag [Stored_km with Receiver]*/
	XHDCP22_RX_TEST_FLAG_NOSTOREDKM_WITH_REPEATER,       /**< Directed test flag [No_Stored_km with Repeater], Sequence: [List, ListAck, StreamManage, StreamReady] */
	XHDCP22_RX_TEST_FLAG_STOREDKM_WITH_REPEATER,         /**< Directed test flag [Stored_km with Repeater], Sequence: [List, ListAck, StreamManage, StreamReady] */
	XHDCP22_RX_TEST_FLAG_REPEATER_MISORDERED_SEQUENCE_1, /**< Directed test flag [Repeater Misordered Sequence 1], Sequence: [StreamManage, StreamReady, List, ListAck] */
	XHDCP22_RX_TEST_FLAG_REPEATER_MISORDERED_SEQUENCE_2, /**< Directed test flag [Repeater Misordered Sequence 2], Sequence [List, StreamManage, StreamReady, ListAck] */
	XHDCP22_RX_TEST_FLAG_REPEATER_MISORDERED_SEQUENCE_3, /**< Directed test flag [Repeater Misordered Sequence 3], Sequence [List, StreamManage, ListAck, StreamReady] */
	XHDCP22_RX_TEST_FLAG_REPEATER_TOPOLOGY_CHANGE,       /**< Directed test flag [Repeater Topology Change] */
	XHDCP22_RX_TEST_FLAG_REPEATER_TOPOLOGY_TIMEOUT,      /**< Directed test flag [Repeater Topology Timeout] */
	XHDCP22_RX_TEST_FLAG_INVALID                         /**< Last value the list, only used for checking */
} XHdcp22_Rx_TestFlags;

/**
 * These constants are used to set the cores test mode.
 */
typedef enum {
	XHDCP22_RX_TESTMODE_DISABLED,  /**< Test mode disabled */
	XHDCP22_RX_TESTMODE_NO_TX,     /**< Test mode to emulate transmitter internally used for unit testing */
	XHDCP22_RX_TESTMODE_SW_TX,     /**< Test mode to emulate transmitter externally used for loopback testing */
	XHDCP22_RX_TESTMODE_INVALID    /**< Last value the list, only used for checking */
} XHdcp22_Rx_TestMode;

/**
 * These constants define the test ddc access types for standalone self testing.
 */
typedef enum
{
	XHDCP22_RX_TEST_DDC_ACCESS_WO,  /**< Write-Only */
	XHDCP22_RX_TEST_DDC_ACCESS_RO,  /**< Read-Only */
	XHDCP22_RX_TEST_DDC_ACCESS_RW   /**< Read-Write */
} XHdcp22_Rx_TestDdcAccess;

/**
 * These constants are the discrete event states for standalone self testing.
 */
typedef enum
{
	XHDCP22_RX_TEST_STATE_UNAUTHENTICATED          = 0xB00,
	XHDCP22_RX_TEST_STATE_SEND_AKEINIT             = 0xB10,
	XHDCP22_RX_TEST_STATE_WAIT_AKESENDCERT         = 0xB11,
	XHDCP22_RX_TEST_STATE_SEND_AKENOSTOREDKM       = 0xB12,
	XHDCP22_RX_TEST_STATE_SEND_AKESTOREDKM         = 0xB13,
	XHDCP22_RX_TEST_STATE_WAIT_AKESENDHPRIME       = 0xB14,
	XHDCP22_RX_TEST_STATE_WAIT_AKESENDPAIRING      = 0xB15,
	XHDCP22_RX_TEST_STATE_SEND_LCINIT              = 0xB20,
	XHDCP22_RX_TEST_STATE_WAIT_LCSENDLPRIME        = 0xB21,
	XHDCP22_RX_TEST_STATE_SEND_SKESENDEKS          = 0xB30,
	XHDCP22_RX_TEST_STATE_WAIT_AUTHENTICATED       = 0xB40,
	XHDCP22_RX_TEST_STATE_UPDATE_TOPOLOGY          = 0xC40,
	XHDCP22_RX_TEST_STATE_WAIT_RECEIVERIDLIST      = 0xC50,
	XHDCP22_RX_TEST_STATE_SEND_RECEIVERIDLISTACK   = 0xC60,
	XHDCP22_RX_TEST_STATE_SEND_STREAMMANAGEMENT    = 0xC70,
	XHDCP22_RX_TEST_STATE_WAIT_STREAMREADY         = 0xC80,
	XHDCP22_RX_TEST_STATE_WAIT_REAUTHREQ,
	XHDCP22_RX_TEST_STATE_WAIT_REPEATERREADY
} XHdcp22_Rx_TestState;

/**
 * This typedef is the RSA private key quintuple definition.
 */
typedef struct
{
	u8 p[64];
	u8 q[64];
	u8 dp[64];
	u8 dq[64];
	u8 qinv[64];
} XHdcp22_Rx_KprivRx;

/**
 * This typedef is the RSA public key definition.
 */
typedef struct
{
	u8 N[128];
	u8 e[3];
} XHdcp22_Rx_KpubRx;

/**
 * This typedef is the DCP public certificate definition.
 */
typedef struct
{
	u8 ReceiverId[5];
	u8 KpubRx[131];
	u8 Reserved[2];
	u8 Signature[384];
} XHdcp22_Rx_CertRx;

/**
 * This typedef is the AKE_Init message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 Rtx[8];
	u8 TxCaps[3];
} XHdcp22_Rx_AKEInit;

/**
 * This typedef is the AKE_Send_Cert message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 CertRx[522];
	u8 Rrx[8];
	u8 RxCaps[3];
} XHdcp22_Rx_AKESendCert;

/**
 * This typedef is the AKE_No_Stored_km message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 EKpubKm[128];
} XHdcp22_Rx_AKENoStoredKm;

/**
 * This typedef is the AKE_Stored_km message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 EKhKm[16];
	u8 M[16];
} XHdcp22_Rx_AKEStoredKm;

/**
 * This typedef is the AKE_Send_H_prime message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 HPrime[32];
} XHdcp22_Rx_AKESendHPrime;

/**
 * This typedef is the AKE_Send_Pairing_Info message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 EKhKm[16];
} XHdcp22_Rx_AKESendPairingInfo;

/**
 * This typedef is the LC_Init message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 Rn[8];
} XHdcp22_Rx_LCInit;

/**
 * This typdef is the LC_Send_L_prime message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 LPrime[32];
} XHdcp22_Rx_LCSendLPrime;

/**
 * This typedef is the SKE_Send_Eks message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 EDkeyKs[16];
	u8 Riv[8];
} XHdcp22_Rx_SKESendEks;

/**
 * This typedef is the RepeaterAuth_Send_RceiverID_List message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 RxInfo[2];
	u8 SeqNumV[3];
	u8 VPrime[16];
	u8 ReceiverIdList[5*31];
} XHdcp22_Rx_RepeaterAuthSendRxIdList;

/**
 * This typedef is the RepeaterAuth_Send_Ack message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 V[16];
} XHdcp22_Rx_RepeaterAuthSendAck;

/**
 * This typedef is the RepeaterAuth_Stream_Manage message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 SeqNumM[3];
	u8 K[2];
	u8 StreamIdType[2];
} XHdcp22_Rx_RepeaterAuthStreamManage;

/**
 * This typedef is the RepeaterAuth_Stream_Ready message definition.
 */
typedef struct
{
	u8 MsgId;
	u8 MPrime[32];
} XHdcp22_Rx_RepeaterAuthStreamReady;

/**
 * This typedef is the union of all the message types.
 */
typedef union
{
	u8 MsgId;
	XHdcp22_Rx_AKEInit            		AKEInit;
	XHdcp22_Rx_AKESendCert        		AKESendCert;
	XHdcp22_Rx_AKENoStoredKm      		AKENoStoredKm;
	XHdcp22_Rx_AKEStoredKm        		AKEStoredKm;
	XHdcp22_Rx_AKESendHPrime      		AKESendHPrime;
	XHdcp22_Rx_AKESendPairingInfo 		AKESendPairingInfo;
	XHdcp22_Rx_LCInit					LCInit;
	XHdcp22_Rx_LCSendLPrime       		LCSendLPrime;
	XHdcp22_Rx_SKESendEks         		SKESendEks;
	XHdcp22_Rx_RepeaterAuthSendRxIdList	RepeaterAuthSendRxIdList;
	XHdcp22_Rx_RepeaterAuthSendAck		RepeaterAuthSendAck;
	XHdcp22_Rx_RepeaterAuthStreamManage	RepeaterAuthStreamManage;
	XHdcp22_Rx_RepeaterAuthStreamReady	RepeaterAuthStreamReady;
} XHdcp22_Rx_Message;

/***************** Macros (Inline Functions) Definitions ********************/

/************************** Function Prototypes *****************************/

/* Crypto Functions */
int  XHdcp22Rx_CalcMontNPrime(u8 *NPrime, const u8 *N, int NDigits);
void XHdcp22Rx_GenerateRandom(XHdcp22_Rx *InstancePtr, int NumOctets, u8* RandomNumberPtr);
int  XHdcp22Rx_RsaesOaepEncrypt(const XHdcp22_Rx_KpubRx *KpubRx, const u8 *Message,
			const u32 MessageLen, const u8 *MaskingSeed, u8 *EncryptedMessage);
int  XHdcp22Rx_RsaesOaepDecrypt(XHdcp22_Rx *InstancePtr, const XHdcp22_Rx_KprivRx *KprivRx,
			 u8 *EncryptedMessage, u8 *Message, int *MessageLen);
void XHdcp22Rx_ComputeHPrime(const u8* Rrx, const u8 *RxCaps, const u8* Rtx,
	     const u8 *TxCaps, const u8 *Km, u8 *HPrime);
void XHdcp22Rx_ComputeEkh(const u8 *KprivRx, const u8 *Km, const u8 *M, u8 *Ekh);
void XHdcp22Rx_ComputeLPrime(const u8 *Rn, const u8 *Km, const u8 *Rrx, const u8 *Rtx, u8 *LPrime);
void XHdcp22Rx_ComputeKs(const u8* Rrx, const u8* Rtx, const u8 *Km, const u8 *Rn,
			 const u8 *Eks, u8 * Ks);
void XHdcp22Rx_ComputeVPrime(const u8 *ReceiverIdList, u32 ReceiverIdListSize,
       const u8 *RxInfo, const u8 *SeqNumV, const u8 *Km, const u8 *Rrx,
       const u8 *Rtx, u8 *VPrime);
void XHdcp22Rx_ComputeMPrime(const u8 *StreamIdType, const u8 *SeqNumM,
       const u8 *Km, const u8 *Rrx, const u8 *Rtx, u8 *MPrime);

#ifdef _XHDCP22_RX_TEST_
/* External functions used for self-testing */
int  XHdcp22Rx_TestSetMode(XHdcp22_Rx *InstancePtr, XHdcp22_Rx_TestMode TestMode,
			 XHdcp22_Rx_TestFlags TestVectorFlag);
int  XHdcp22Rx_TestRun(XHdcp22_Rx *InstancePtr);
u8   XHdcp22Rx_TestIsFinished(XHdcp22_Rx *InstancePtr);
u8   XHdcp22Rx_TestIsPassed(XHdcp22_Rx *InstancePtr);
int  XHdcp22Rx_TestLoadKeys(XHdcp22_Rx *InstancePtr);
void XHdcp22Rx_TestSetVerbose(XHdcp22_Rx *InstancePtr, u8 Verbose);

/* Internal functions used for self-testing */
int  XHdcp22Rx_TestDdcWriteReg(XHdcp22_Rx *InstancePtr, u8 DeviceAddress, int Size, u8 *Data, u8 Stop);
int  XHdcp22Rx_TestDdcReadReg(XHdcp22_Rx *InstancePtr, u8 DeviceAddress, int Size, u8 *Data, u8 Stop);
void XHdcp22Rx_TestGenerateRrx(XHdcp22_Rx *InstancePtr, u8* RrxPtr);
#endif

#ifdef __cplusplus
}
#endif

#endif /* XHDCP22_RX_I_H_ */

/** @} */
