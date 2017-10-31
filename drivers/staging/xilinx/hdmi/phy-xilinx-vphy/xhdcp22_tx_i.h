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
* @file xhdcp22_tx_i.h
* @addtogroup hdcp22_tx_v2_0
* @{
* @details
*
* This file contains data which is shared between files and internal to the
* XIntc component. It is intended for internal use only.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ------ -------- --------------------------------------------------
* 1.00  JO     06/24/15 Initial release.
* 1.01  MH     03/14/15 Changed maximum locality check count
*                       from 1024 to 128.
* 2.00  MH     06/28/16 Updated for repeater downstream support.
* 2.01  MH     02/13/17 1. Updated maximum locality check count
*                       from 128 to 8 to avoid delays in re-auth.
*                       2. Added log events for failures.
* </pre>
*
******************************************************************************/
#ifndef XHDCP22_TX_I_H
/** prevent circular inclusions by using protection macros. */
#define XHDCP22_TX_I_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#include "xhdcp22_tx.h"

/************************** Constant Definitions *****************************/
/** Maximum allowed re-checking locality, prescribed by LLC. */
#define XHDCP22_TX_MAX_ALLOWED_LOCALITY_CHECKS  8
/** Maximum allowed re-checking content stream management */
#define XHDCP22_TX_MAX_ALLOWED_STREAM_MANAGE_CHECKS 128

#define XHDCP22_TX_LC128_SIZE                   16  /**< Lc128 global constant size */

#define XHDCP22_TX_RCVID_SIZE                   5   /**< Unique receiver Id size in bytes */

#define XHDCP22_TX_REPEATER_MAX_DEVICE_COUNT    31  /**< Max number of downstream devices allowed */
#define XHDCP22_TX_REPEATER_MAX_CASCADE_DEPTH   4   /**< Max cascade depth */

/* Message Ids. */
#define XHDCP22_TX_MSG_UNDEFINED                0   /**< Undefined. */
#define XHDCP22_TX_AKE_INIT                     2   /**< AKE Init message. */
#define XHDCP22_TX_AKE_INIT_SIZE                12  /**< AKE Init message size. */
#define XHDCP22_TX_AKE_SEND_CERT                3   /**< AKE Send Certificate message. */
#define XHDCP22_TX_AKE_SEND_CERT_SIZE           534 /**< AKE Send Certificate message size. */
#define XHDCP22_TX_AKE_NO_STORED_KM             4   /**< AKE No Stored Km message. */
#define XHDCP22_TX_AKE_NO_STORED_KM_SIZE        129 /**< AKE No Stored Km message size. */
#define XHDCP22_TX_AKE_STORED_KM                5   /**< AKE Stored Km message size. */
#define XHDCP22_TX_AKE_STORED_KM_SIZE           33  /**< AKE Stored Km message size. */
#define XHDCP22_TX_AKE_SEND_H_PRIME             7   /**< AKE H' message. */
#define XHDCP22_TX_AKE_SEND_H_PRIME_SIZE        33  /**< AKE H' message size. */
#define XHDCP22_TX_AKE_SEND_PAIRING_INFO        8   /**< AKE Pairing info message.*/
#define XHDCP22_TX_AKE_SEND_PAIRING_INFO_SIZE   17  /**< AKE Pairing info message size. */
#define XHDCP22_TX_LC_INIT                      9   /**< LC Init message. */
#define XHDCP22_TX_LC_INIT_SIZE                 9   /**< LC Init message size. */
#define XHDCP22_TX_LC_SEND_L_PRIME              10  /**< Send L' message. */
#define XHDCP22_TX_LC_SEND_L_PRIME_SIZE         33  /**< Send L' message size. */
#define XHDCP22_TX_SKE_SEND_EKS                 11  /**< Send Eks message. */
#define XHDCP22_TX_SKE_SEND_EKS_SIZE            25  /**< Send Eks message size.*/
#define XHDCP22_TX_REPEATAUTH_SEND_RECVID_LIST  12  /**< Repeater Auth send receiver ID list message. */
#define XHDCP22_TX_REPEATAUTH_SEND_RECVID_LIST_SIZE 177 /**< Repeater Auth send receiver ID list maximum message size. */
#define XHDCP22_TX_REPEATAUTH_SEND_ACK          15  /**< RepeaterAuth send ack message. */
#define XHDCP22_TX_REPEATAUTH_SEND_ACK_SIZE     17  /**< RepeaterAuth send ack message size in bytes. */
#define XHDCP22_TX_REPEATAUTH_STREAM_MANAGE     16  /**< RepeaterAuth stream manage message. */
#define XHDCP22_TX_REPEATAUTH_STREAM_MANAGE_SIZE 8  /**< RepeaterAuth stream manage message size in bytes. */
#define XHDCP22_TX_REPEATAUTH_STREAM_READY      17  /**< RepeaterAuth stream ready message. */
#define XHDCP22_TX_REPEATAUTH_STREAM_READY_SIZE 33  /**< RepeaterAuth stream ready message size in bytes. */

/** Reason why the timer was started: Undefined. */
#define XHDCP22_TX_TS_UNDEFINED                 XHDCP22_TX_MSG_UNDEFINED

/** Reason why the timer was started:
* Waiting for Content Stream Type to be set when in repeater mode.
* @note: The message ids also double as a reason identifier.
*        Thus, the value of this define should NOT overlap a message Id.*/
#define XHDCP22_TX_TS_WAIT_FOR_STREAM_TYPE      0xFD
/** Reason why the timer was started:
* Mandatory wait of 200 ms before the cipher may be activated.
* Authenticated flag is only set after this period has expired.
* @note: The message ids also double as a reason identifier.
*        Thus, the value of this define should NOT overlap a message Id.*/
#define XHDCP22_TX_TS_WAIT_FOR_CIPHER           0xFE
/** Reason why the timer was started: Status checking.
* @note: The message ids also double as a reason identifier.
*        Thus, the value of this define should NOT overlap a message Id.*/
#define XHDCP22_TX_TS_RX_REAUTH_CHECK           0xFF

/** Internal used timer counter for timeout checking. */
#define XHDCP22_TX_TIMER_CNTR_0                 0
/** Internal used timer counter for logging. */
#define XHDCP22_TX_TIMER_CNTR_1                 1

#define XHDCP22_TX_HDCPPORT_VERSION_OFFSET      0x50 /**< DDC version offset. */
#define XHDCP22_TX_HDCPPORT_WRITE_MSG_OFFSET    0x60 /**< DDC write message buffer offset. */
#define XHDCP22_TX_HDCPPORT_RXSTATUS_OFFSET     0x70 /**< DDC RX status offset. */
#define XHDCP22_TX_HDCPPORT_READ_MSG_OFFSET     0x80 /**< DDC read message buffer offset. */

/** HDCP Port DDC Rx status register masks. */
#define XHDCP22_TX_RXSTATUS_REAUTH_REQ_MASK     (1<<11) /**< RX status REAUTHENTICATION bit. */
#define XHDCP22_TX_RXSTATUS_READY_MASK          (1<<10) /**< RX status READY bit. */
#define XHDCP22_TX_RXSTATUS_AVAIL_BYTES_MASK    (0x3FF) /**< RX status available bytes in read message buffer. */

/** RX certificate and Tx public key sizes in bytes. */
#define XHDCP22_TX_CERT_RCVID_SIZE              5   /**< Unique receiver Id size in the RX certificate. */
#define XHDCP22_TX_CERT_PUB_KEY_N_SIZE          128 /**< Public key-N size in the RX certificate. */
#define XHDCP22_TX_CERT_PUB_KEY_E_SIZE          3   /**< Public key-E size in the RX certificate. */
#define XHDCP22_TX_CERT_RSVD_SIZE               2   /**< Reserved size in the RX certificate. */
#define XHDCP22_TX_CERT_SIGNATURE_SIZE          384 /**< Signature size in the RX certificate. */
/** Total size of the RX certificate. */
#define XHDCP22_TX_CERT_SIZE                     \
       ( XHDCP22_TX_CERT_RCVID_SIZE +            \
         XHDCP22_TX_CERT_PUB_KEY_N_SIZE +        \
         XHDCP22_TX_CERT_PUB_KEY_E_SIZE +        \
         XHDCP22_TX_CERT_RSVD_SIZE +             \
         XHDCP22_TX_CERT_SIGNATURE_SIZE )
#define XHDCP22_TX_RXCAPS_SIZE                  3   /**< RX capabilities size. */
#define XHDCP22_TX_TXCAPS_SIZE                  3   /**< TX capabilities size. */
#define XHDCP22_TX_KPUB_DCP_LLC_N_SIZE          384 /**< LLC public key-N size. */
#define XHDCP22_TX_KPUB_DCP_LLC_E_SIZE          1   /**< LLC public key-E size. */

/* defines for de/encryption. */
#define XHDCP22_TX_SHA256_HASH_SIZE             32 /**< SHA256 hash size in bytes. */
#define XHDCP22_TX_AES128_SIZE                  16 /**< AES128 keys in bytes. */

/* Sizes of keys in bytes. */
#define XHDCP22_TX_RTX_SIZE                     8     /**< 64 bits. */
#define XHDCP22_TX_RRX_SIZE                     8     /**< 64 bits. */
#define XHDCP22_TX_KM_SIZE                      XHDCP22_TX_AES128_SIZE
#define XHDCP22_TX_E_KPUB_KM_SIZE               128   /**< 1024 bits. */
#define XHDCP22_TX_H_PRIME_SIZE                 32    /**< 256 bits. */
#define XHDCP22_TX_EKH_KM_SIZE                  16    /**< 128 bits. */
#define XHDCP22_TX_KM_MSK_SEED_SIZE             XHDCP22_TX_SHA256_HASH_SIZE
#define XHDCP22_TX_RN_SIZE                      8     /**< 64-bits. */
#define XHDCP22_TX_RIV_SIZE                     8     /**< 64-bits. */
#define XHDCP22_TX_L_PRIME_SIZE                 32    /**< 256 bits. */
#define XHDCP22_TX_KS_SIZE                      16    /**< 128 bits. */
#define XHDCP22_TX_EDKEY_KS_SIZE                16    /**< 128 bits. */

/* Sizes of SRM Fields in bytes. */
#define XHDCP22_TX_SRM_RCVID_SIZE       XHDCP22_TX_RCVID_SIZE   /**< Receiver Id size in the SRM block. */
#define XHDCP22_TX_SRM_SIGNATURE_SIZE   384                     /**< Signature size in the SRM block. */

/* Defines for Repeater Authentication messages */
#define XHDCP22_TX_RXINFO_SIZE                  2     /**< RxInfo size in bytes. */
#define XHDCP22_TX_SEQ_NUM_V_SIZE               3     /**< Seq_num_v size in bytes. */
#define XHDCP22_TX_V_SIZE                       32    /**< V size in bytes. */
#define XHDCP22_TX_V_PRIME_SIZE                 16    /**< VPrime size in bytes. */
#define XHDCP22_TX_SEQ_NUM_M_SIZE               3     /**< Seq_num_m size in bytes. */
#define XHDCP22_TX_K_SIZE                       2     /**< K size in bytes. */
#define XHDCP22_TX_STREAMID_TYPE_SIZE           2     /**< Stream ID and Type size in bytes. */
#define XHDCP22_TX_M_PRIME_SIZE                 32    /**< MPrime size in bytes. */

/* Test flags to trigger errors for unit tests. */

/** Use a certificate test vector. */
#define XHDCP22_TX_TEST_CERT_RX         0x00000001
/** Use a H_Prime test vector.*/
#define XHDCP22_TX_TEST_H1              0x00000002
/** Use a L_Prime test vector.*/
#define XHDCP22_TX_TEST_L1              0x00000004
/** Use a pairing info Ekh(Km) test vector, use i.c.w #XHDCP22_TX_TEST_RCV_TIMEOUT*/
#define XHDCP22_TX_TEST_EKH_KM          0x00000008
/** Invalidate a value */
#define XHDCP22_TX_TEST_INVALID_VALUE   0x00000010
/** Timeout on a received message. */
#define XHDCP22_TX_TEST_RCV_TIMEOUT     0x00000020
/** Use a V_Prime test vector. */
#define XHDCP22_TX_TEST_V1              0x00000040
/** Use a M_Prime test vector. */
#define XHDCP22_TX_TEST_M1              0x00000080
/** AKE is forced using a stored Km scenarion. Pairing info is pre-loaded
* with test vectors that forces Stored Km scenario.*/
#define XHDCP22_TX_TEST_STORED_KM       0x00000100
/** Disable timeout checking. this is mainly used to check the HDCP RX core in
* loopback mode in case it cannot meet timing requirements (no offloading to
* hardware) and reponsetimes need to be logged.*/
#define XHDCP22_TX_TEST_NO_TIMEOUT      0x00000200
/** Pairing info is cleared, to force a non-stored Km scenario */
#define XHDCP22_TX_TEST_CLR_PAIRINGINFO 0x00000400
/** Use testvectors for receiver R1 */
#define XHDCP22_TX_TEST_USE_TEST_VECTOR_R1 0x80000000

/** DDC base address (0x74 >> 1) */
#define XHDCP22_TX_DDC_BASE_ADDRESS     0x3A

/**************************** Type Definitions *******************************/
/**
* These constants are used to set the core into testing mode with #XHdcp22Tx_TestSetMode.
*/
typedef enum {
	XHDCP22_TX_TESTMODE_DISABLED,       /**< Testmode is disabled. */
	XHDCP22_TX_TESTMODE_SW_RX,          /**< Actual HDCP2.2 RX component is connected. */
	XHDCP22_TX_TESTMODE_NO_RX,          /**< HDCP2.2 RX software component is not available and will be emulated. */
	XHDCP22_TX_TESTMODE_UNIT,           /**< HDCP2.2 RX is emulated, #XHdcp22Tx_LogDisplay shows source code.*/
	XHDCP22_TX_TESTMODE_USE_TESTKEYS,   /**< Use test keys as defined in Errata to HDCP on HDMI Specification
	                                         Revision 2.2, February 09, 2015. */
	XHDCP22_TX_TESTMODE_INVALID         /**< Last value the list, only used for checking. */
} XHdcp22_Tx_TestMode;

/**
* Value definitions for debugging.
* These values are used as parameter for the #XHDCP22_TX_LOG_EVT_DBG
* logging event.
*/
typedef enum
{
	XHDCP22_TX_LOG_DBG_STARTIMER,
	XHDCP22_TX_LOG_DBG_MSGAVAILABLE,
	XHDCP22_TX_LOG_DBG_TX_AKEINIT,
	XHDCP22_TX_LOG_DBG_RX_CERT,
	XHDCP22_TX_LOG_DBG_VERIFY_SIGNATURE,
	XHDCP22_TX_LOG_DBG_VERIFY_SIGNATURE_PASS,
	XHDCP22_TX_LOG_DBG_VERIFY_SIGNATURE_FAIL,
	XHDCP22_TX_LOG_DBG_DEVICE_IS_REVOKED,
	XHDCP22_TX_LOG_DBG_ENCRYPT_KM,
	XHDCP22_TX_LOG_DBG_ENCRYPT_KM_DONE,
	XHDCP22_TX_LOG_DBG_TX_NOSTOREDKM,
	XHDCP22_TX_LOG_DBG_TX_STOREDKM,
	XHDCP22_TX_LOG_DBG_RX_H1,
	XHDCP22_TX_LOG_DBG_RX_EKHKM,
	XHDCP22_TX_LOG_DBG_COMPUTE_H,
	XHDCP22_TX_LOG_DBG_COMPUTE_H_DONE,
	XHDCP22_TX_LOG_DBG_COMPARE_H_FAIL,
	XHDCP22_TX_LOG_DBG_TX_LCINIT,
	XHDCP22_TX_LOG_DBG_RX_L1,
	XHDCP22_TX_LOG_DBG_COMPUTE_L,
	XHDCP22_TX_LOG_DBG_COMPUTE_L_DONE,
	XHDCP22_TX_LOG_DBG_COMPARE_L_FAIL,
	XHDCP22_TX_LOG_DBG_TX_EKS,
	XHDCP22_TX_LOG_DBG_COMPUTE_EDKEYKS,
	XHDCP22_TX_LOG_DBG_COMPUTE_EDKEYKS_DONE,
	XHDCP22_TX_LOG_DBG_RX_RCVIDLIST,
	XHDCP22_TX_LOG_DBG_COMPUTE_V,
	XHDCP22_TX_LOG_DBG_COMPUTE_V_DONE,
	XHDCP22_TX_LOG_DBG_COMPARE_V_FAIL,
	XHDCP22_TX_LOG_DBG_RX_M1,
	XHDCP22_TX_LOG_DBG_COMPUTE_M,
	XHDCP22_TX_LOG_DBG_COMPUTE_M_DONE,
	XHDCP22_TX_LOG_DBG_CHECK_REAUTH,
	XHDCP22_TX_LOG_DBG_TIMEOUT,
	XHDCP22_TX_LOG_DBG_TIMESTAMP,
	XHDCP22_TX_LOG_DBG_AES128ENC,
	XHDCP22_TX_LOG_DBG_AES128ENC_DONE,
	XHDCP22_TX_LOG_DBG_SHA256HASH,
	XHDCP22_TX_LOG_DBG_SHA256HASH_DONE,
	XHDCP22_TX_LOG_DBG_OEAPENC,
	XHDCP22_TX_LOG_DBG_OEAPENC_DONE,
	XHDCP22_TX_LOG_DBG_RSAENC,
	XHDCP22_TX_LOG_DBG_RSAENC_DONE,
	XHDCP22_TX_LOG_DBG_MSG_WRITE_FAIL,
	XHDCP22_TX_LOG_DBG_MSG_READ_FAIL
} XHdcp22_Tx_LogDebugValue;

/**

* This typedef contains the public key certificate of Receiver that is received
* with AKE_Send_Cert.
*/
typedef	struct {
	u8 ReceiverId[XHDCP22_TX_CERT_RCVID_SIZE];
	u8 N[XHDCP22_TX_CERT_PUB_KEY_N_SIZE];
	u8 e[XHDCP22_TX_CERT_PUB_KEY_E_SIZE];
	u8 Reserved[XHDCP22_TX_CERT_RSVD_SIZE];
	u8 Signature[XHDCP22_TX_CERT_SIGNATURE_SIZE];
} XHdcp22_Tx_CertRx;

/**
* This typedef contains the received AKE_Send_Cert message definition.
*/
typedef struct
{
	u8 MsgId;

	XHdcp22_Tx_CertRx CertRx;
	u8 Rrx[XHDCP22_TX_RRX_SIZE];
	u8 RxCaps[XHDCP22_TX_RXCAPS_SIZE];
} XHdcp22_Tx_AKESendCert;

/**
* This typedef contains the received AKE_Send_H_prime message definition.
*/
typedef struct
{
	u8 MsgId;

	u8 HPrime[XHDCP22_TX_H_PRIME_SIZE];
} XHdcp22_Tx_AKESendHPrime;

/**
* This typedef contains the received AKE_Send_Pairing_Info message definition.
*/
typedef struct
{
	u8 MsgId;

	u8 EKhKm[XHDCP22_TX_EKH_KM_SIZE];
} XHdcp22_Tx_AKESendPairingInfo;

/**
* This typedef contains the received AKE_Send_L_prime message definition.
*/
typedef struct
{
	u8 MsgId;

	u8 LPrime[XHDCP22_TX_L_PRIME_SIZE];
} XHdcp22_Tx_LCSendLPrime;

/**
* This typedef contains the transmitted AKE_Init message definition.
*/
typedef struct
{
	u8 MsgId;

	u8 Rtx[XHDCP22_TX_RTX_SIZE];
	u8 TxCaps[XHDCP22_TX_TXCAPS_SIZE];
} XHdcp22_Tx_AKEInit;

/**
* This typedef contains the transmitted AKE_No_Stored_km message definition.
*/
typedef struct
{
	u8 MsgId;

	u8 EKpubKm[XHDCP22_TX_E_KPUB_KM_SIZE];
} XHdcp22_Tx_AKENoStoredKm;


/**
* This typedef contains the transmitted AKE_Stored_km message definition.
*/
typedef struct
{
	u8 MsgId;

	u8 EKhKm[XHDCP22_TX_EKH_KM_SIZE];
	u8 Rtx[XHDCP22_TX_RTX_SIZE];	// In the protocol defined as M=Rtx || Rrx
	u8 Rrx[XHDCP22_TX_RRX_SIZE];
} XHdcp22_Tx_AKEStoredKm;

/**
* This typedef contains the transmitted LC_Init message definition.
*/
typedef struct
{
	u8 MsgId;

	u8 Rn[XHDCP22_TX_RN_SIZE];
} XHdcp22_Tx_LCInit;

/**
* This typedef contains the transmitted SKE_Send_Eks message definition.
*/
typedef struct
{
	u8 MsgId;

	u8 EDkeyKs[XHDCP22_TX_EDKEY_KS_SIZE];
	u8 Riv[XHDCP22_TX_RIV_SIZE];
} XHdcp22_Tx_SKESendEks;

/**
* This typedef contains the RepeaterAuth_Send_ReceiverID_List message definition.
*/
typedef struct
{
	u8 MsgId;

	u8 RxInfo[XHDCP22_TX_RXINFO_SIZE];
	u8 SeqNum_V[XHDCP22_TX_SEQ_NUM_V_SIZE];
	u8 VPrime[XHDCP22_TX_V_PRIME_SIZE];
	u8 ReceiverIDs[XHDCP22_TX_REPEATER_MAX_DEVICE_COUNT][XHDCP22_TX_RCVID_SIZE];
} XHdcp22_Tx_RepeatAuthSendRecvIDList;

/**
* This typedef contains the RepeaterAuth_Send_Ack message definition.
*/
typedef struct
{
	u8 MsgId;

	u8 V[XHDCP22_TX_V_PRIME_SIZE];
} XHdcp22_Tx_RepeatAuthSendAck;

/**
* This typedef contains the RepeaterAuth_Stream_Manage message definition.
*/
typedef struct
{
	u8 MsgId;

	u8 SeqNum_M[XHDCP22_TX_SEQ_NUM_M_SIZE];
	u8 K[XHDCP22_TX_K_SIZE];
	u8 StreamID_Type[XHDCP22_TX_STREAMID_TYPE_SIZE];
} XHdcp22_Tx_RepeatAuthStreamManage;

/**
* This typedef contains the RepeaterAuth_Stream_Ready message definition.
*/
typedef struct
{
	u8 MsgId;

	u8 MPrime[XHDCP22_TX_M_PRIME_SIZE];
} XHdcp22_Tx_RepeatAuthStreamReady;

/**
* Message buffer structure.
*/
typedef union
{
	/* Message Id. */
	u8 MsgId;

	/* Received messages. */
	XHdcp22_Tx_AKESendCert        AKESendCert;
	XHdcp22_Tx_AKESendHPrime      AKESendHPrime;
	XHdcp22_Tx_AKESendPairingInfo AKESendPairingInfo;
	XHdcp22_Tx_LCSendLPrime       LCSendLPrime;
	XHdcp22_Tx_RepeatAuthSendRecvIDList RepeatAuthSendRecvIDList;
	XHdcp22_Tx_RepeatAuthStreamReady RepeatAuthStreamReady;

	/* Transmitted messages. */
	XHdcp22_Tx_AKEInit            AKEInit;
	XHdcp22_Tx_AKENoStoredKm      AKENoStoredKm;
	XHdcp22_Tx_AKEStoredKm        AKEStoredKm;
	XHdcp22_Tx_LCInit             LCInit;
	XHdcp22_Tx_SKESendEks         SKESendEks;
	XHdcp22_Tx_RepeatAuthSendAck  RepeatAuthSendAck;
	XHdcp22_Tx_RepeatAuthStreamManage RepeatAuthStreamManage;
} XHdcp22_Tx_Message;

/**
* Message including the DDC Address.
*/
typedef struct
{
	u8 DdcAddress;
	XHdcp22_Tx_Message Message;
} XHdcp22_Tx_DDCMessage;


/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

/* Crypto functions */
void XHdcp22Tx_MemXor(u8 *Output, const u8 *InputA, const u8 *InputB,
                      unsigned int Size);
int XHdcp22Tx_VerifyCertificate(const XHdcp22_Tx_CertRx *CertificatePtr,
                                const u8* KpubDcpNPtr, int KpubDcpNSize,
                                const u8* KpubDcpEPtr, int KpubDcpESize);
int XHdcp22Tx_VerifySRM(const u8* SrmPtr, int SrmSize,
                        const u8* KpubDcpNPtr, int KpubDcpNSize,
                        const u8* KpubDcpEPtr, int KpubDcpESize);
void XHdcp22Tx_ComputeHPrime(const u8 *Rrx, const u8 *RxCaps,
                             const u8* Rtx,  const u8 *TxCaps,
                             const u8 *Km, u8 *HPrime);
void XHdcp22Tx_ComputeLPrime(const u8* Rn, const u8 *Km,
                             const u8 *Rrx, const u8 *Rtx,
                             u8 *LPrime);
void XHdcp22Tx_ComputeV(const u8* Rn, const u8* Rrx, const u8* RxInfo,
	                    const u8* Rtx, const u8* RecvIDList, const u8 RecvIDCount,
	                    const u8* SeqNum_V, const u8* Km, u8* V);
void XHdcp22Tx_ComputeM(const u8* Rn, const u8* Rrx, const u8* Rtx,
                        const u8* StreamIDType, const u8* k,
                        const u8* SeqNum_M, const u8* Km, u8* M);
void XHdcp22Tx_ComputeEdkeyKs(const u8* Rn, const u8* Km,
                              const u8 *Ks, const u8 *Rrx,
                              const u8 *Rtx,  u8 *EdkeyKs);
int XHdcp22Tx_EncryptKm(const XHdcp22_Tx_CertRx* CertificatePtr,
                        const u8* KmPtr, u8 *MaskingSeedPtr,
                        u8* EncryptedKmPtr);
void XHdcp22Tx_GenerateRandom(XHdcp22_Tx *InstancePtr, int NumOctets,
                              u8* RandomNumberPtr);

/* Functions for logging */
void XHdcp22Tx_Dump(const char *string, const u8 *m, u32 mlen);
void XHdcp22Tx_LogWrNoInst(XHdcp22_Tx_LogEvt Evt, u16 Data);

#ifdef _XHDCP22_TX_TEST_
/* External functions used for self-testing */
void XHdcp22Tx_TestSetMode(XHdcp22_Tx *InstancePtr, XHdcp22_Tx_TestMode Mode,
                           u32 TestFlags);
u8 XHdcp22Tx_TestCheckResults(XHdcp22_Tx *InstancePtr,
                              XHdcp22_Tx_LogItem *Expected, u32 nExpected);

/* Internal functions used for self-testing */
u8   XHdcp22Tx_TestSimulateTimeout(XHdcp22_Tx *InstancePtr);
void XHdcp22Tx_TestGenerateRtx(XHdcp22_Tx *InstancePtr, u8* RtxPtr);
void XHdcp22Tx_TestGenerateKm(XHdcp22_Tx *InstancePtr, u8* KmPtr);
void XHdcp22Tx_TestGenerateKmMaskingSeed(XHdcp22_Tx *InstancePtr, u8* SeedPtr);
void XHdcp22Tx_TestGenerateRn(XHdcp22_Tx *InstancePtr, u8* RnPtr);
void XHdcp22Tx_TestGenerateRiv(XHdcp22_Tx *InstancePtr, u8* RivPtr);
void XHdcp22Tx_TestGenerateKs(XHdcp22_Tx *InstancePtr, u8* KsPtr);
const u8* XHdcp22Tx_TestGetKPubDpc(XHdcp22_Tx *InstancePtr);
const u8* XHdcp22Tx_TestGetSrm(XHdcp22_Tx *InstancePtr, u8 Select);
void XHdcp22Tx_LogDisplayUnitTest(XHdcp22_Tx *InstancePtr);
#endif


/************************** Variable Definitions *****************************/
#ifdef __cplusplus
}
#endif

#endif /* End of protection macro. */

/** @} */
