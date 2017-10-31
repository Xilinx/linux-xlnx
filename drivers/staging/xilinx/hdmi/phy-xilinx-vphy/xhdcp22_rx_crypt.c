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
* @file xhdcp22_rx_crypt.c
* @addtogroup hdcp22_rx_v2_0
* @{
* @details
*
* This file contains the implementation of the PKCS #1 Public Key Cryptography
* Standard and other cryptographic functions used during HDCP 2.2 receiver
* authentication and key exchange.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00  MH   10/30/15 First Release
* 2.00  MH   04/14/16 Updated for repeater upstream support.
* 2.20  MH   06/21/17 Updated for 64 bit support.
*</pre>
*
*****************************************************************************/

/***************************** Include Files ********************************/
//#include "stdio.h"
//#include "stdlib.h"
#include <linux/string.h>
#include "xil_assert.h"
#include "xstatus.h"
#include "xil_printf.h"
#include "xhdcp22_rx_i.h"
#include "xhdcp22_common.h"

/************************** Constant Definitions ****************************/

/**************************** Type Definitions ******************************/

/***************** Macros (Inline Functions) Definitions ********************/
#define XHdcp22Rx_MpSizeof(A) (sizeof(A)/sizeof(u32))

/************************** Variable Definitions ****************************/

/************************** Function Prototypes *****************************/
/* Functions for implementing PCKS1 */
static int  XHdcp22Rx_Pkcs1Rsaep(const XHdcp22_Rx_KpubRx *KpubRx, u8 *Message,
	            u8 *EncryptedMessage);
static int  XHdcp22Rx_Pkcs1Rsadp(XHdcp22_Rx *InstancePtr, const XHdcp22_Rx_KprivRx *KprivRx,
	            u8 *EncryptedMessage, u8 *Message);
static int  XHdcp22Rx_Pkcs1Mgf1(const u8 *seed, const u32 seedlen, u8  *mask, u32 masklen);
static int  XHdcp22Rx_Pkcs1EmeOaepEncode(const u8 *Message, const u32 MessageLen,
	            const u8 *MaskingSeed, u8 *EncodedMessage);
static int  XHdcp22Rx_Pkcs1EmeOaepDecode(u8 *EncodedMessage, u8 *Message, int *MessageLen);
static void XHdcp22Rx_Pkcs1MontMultFiosInit(XHdcp22_Rx *InstancePtr, u32 *N,
	            const u32 *NPrime, int NDigits);
#ifndef _XHDCP22_RX_SW_MMULT_
static void XHdcp22Rx_Pkcs1MontMultFios(XHdcp22_Rx *InstancePtr, u32 *U, u32 *A,
	            u32 *B, int NDigits);
#else
static void XHdcp22Rx_Pkcs1MontMultFiosStub(u32 *U, u32 *A, u32 *B, u32 *N,
	            const u32 *NPrime, int NDigits);
static void XHdcp22Rx_Pkcs1MontMultAdd(u32 *A, u32 C, int SDigit, int NDigits);
#endif
static int  XHdcp22Rx_Pkcs1MontExp(XHdcp22_Rx *InstancePtr, u32 *C, u32 *A, u32 *E,
	            u32 *N, const u32 *NPrime, int NDigits);

/* Functions for implementing other cryptographic tasks */
static void XHdcp22Rx_ComputeDKey(const u8* Rrx, const u8* Rtx, const u8 *Km,
	            const u8 *Rn, u8 *Ctr, u8 *DKey);
static void XHdcp22Rx_Xor(u8 *Cout, const u8 *Ain, const u8 *Bin, u32 Len);

/*****************************************************************************/
/**
* This function is used to calculate the Montgomery NPrime. NPrime is
* calculated from the following equation R*Rinv - N*NPrime == 1.
* For the HDCP2.2 receiver the modulus N has a fixed size
* of k = 512bits. Given k, the value R = 2^(k), and Rinv is the modular
* inverse of R.
*
* Reference:
* Analyzing and Comparing Montgomery Multiplication Algorithms
* IEEE Micro, 16(3):26-33,June 1996
* By: Cetin Koc, Tolga Acar, and Burton Kaliski
*
* @param	NPrime is the calculated value and 2*NDigits in Size.
* @param	N is modulus
* @param	NDigits is the integer precision of arguments (N, NPrime), which
* 			should always be 16 for the HDCP2.2 receiver.
*
* @return	XST_SUCCESS or FAILURE.
*
* @note		None.
******************************************************************************/
int XHdcp22Rx_CalcMontNPrime(u8 *NPrime, const u8 *N, int NDigits)
{
	/* Verify arguments */
	Xil_AssertNonvoid(NPrime != NULL);
	Xil_AssertNonvoid(N != NULL);
	Xil_AssertNonvoid(NDigits == 16);

	u32 N_i[XHDCP22_RX_N_SIZE/4];
	u32 NPrime_i[XHDCP22_RX_N_SIZE/4];
	u32 R[XHDCP22_RX_N_SIZE/4];
	u32 RInv[XHDCP22_RX_N_SIZE/4];
	u32 T1[XHDCP22_RX_N_SIZE/2];
	u32 T2[XHDCP22_RX_N_SIZE/2];

	/* Clear variables */
	memset(N_i, 0, sizeof(N_i));
	memset(NPrime_i, 0, sizeof(NPrime_i));
	memset(R, 0, sizeof(R));
	memset(RInv, 0, sizeof(RInv));
	memset(T1, 0, sizeof(T1));
	memset(T2, 0, sizeof(T2));

	/* Convert from octet string */
	mpConvFromOctets(N_i, XHdcp22Rx_MpSizeof(N_i), N, 4*NDigits);

	/* Step 1: R = 2^(NDigits*32) */
	R[0] = 1;
	mpShiftLeft(R, R, 32*NDigits, XHdcp22Rx_MpSizeof(R));

	/* Step 2: Rinv = R^(-1)*mod(N) */
	T1[0] = 0;
	memcpy(T1, N_i, sizeof(N_i)); // Increase precision of N
	if(mpModInv(RInv, R, T1, XHdcp22Rx_MpSizeof(RInv)))
	{
		xil_printf("ERROR: Failed Rinv Calculation\n\r");
		return XST_FAILURE;
	}

	/* Step 3: NPrime = (R*Rinv-1)/N */
	mpMultiply(T1, R, RInv, 2*NDigits);
	memset(T2, 0, sizeof(T2));
	T2[0] = 1;
	mpSubtract(T1, T1, T2, XHdcp22Rx_MpSizeof(T1));
	mpDivide(NPrime_i, T2, T1, XHdcp22Rx_MpSizeof(NPrime_i), (u32 *)N_i, NDigits);

	/* Step 4: Sanity Check, R*Rinv - N*NPrime == 1 */
	mpMultiply(T1, R, RInv, 2*NDigits);
	mpMultiply(T2, (u32 *)N_i, NPrime_i, XHdcp22Rx_MpSizeof(NPrime_i));
	mpSubtract(T1, T1, T2, XHdcp22Rx_MpSizeof(T1));
	memset(T2, 0, sizeof(T2));
	T2[0] = 1;
	if(!mpEqual(T1, T2, XHdcp22Rx_MpSizeof(T1)))
	{
		xil_printf("ERROR: Failed NPrime Calculation\n\r");
		return XST_FAILURE;
	}

	/* Convert to octet string */
	mpConvToOctets(NPrime_i, NDigits, NPrime, 4*NDigits);

	return XST_SUCCESS;
}

/****************************************************************************/
/**
* This function implements the RSAES-OAEP-Encrypt operation. The message
* is encoded using EME-OAEP and then encrypted with the public key
* using RSAEP.
*
* Reference: PKCS#1 v2.1, Section 7.1.1
*
* @param	KpubRx is the RSA public key structure containing the 1024 bit
* 			modulus and 24 bit public exponent.
* @param	Message is the octet string to be encrypted.
* @param	MessageLen is the length of octet string to be encrypted and
* 			must be less than or equal to (nLen - 2*hLen - 2)
* @param	MaskingSeed is the random octet string seed of length hLen
* 			used by EME-OAEP encoding function.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
*****************************************************************************/
int XHdcp22Rx_RsaesOaepEncrypt(const XHdcp22_Rx_KpubRx *KpubRx, const u8 *Message,
	const u32 MessageLen, const u8 *MaskingSeed, u8 *EncryptedMessage)
{
	/* Verify arguments */
	Xil_AssertNonvoid(KpubRx != NULL);
	Xil_AssertNonvoid(Message != NULL);
	Xil_AssertNonvoid(MessageLen > 0);
	Xil_AssertNonvoid(MaskingSeed != NULL);
	Xil_AssertNonvoid(EncryptedMessage != NULL);

	u8 em[XHDCP22_RX_N_SIZE];
	int Status;

	/* Step 1: Length checking */
	if(MessageLen > (XHDCP22_RX_N_SIZE - 2*XHDCP22_RX_HASH_SIZE - 2))
	{
		return XST_FAILURE;
	}

	/* Step 2: EME-OAEP Encoding */
	Status = XHdcp22Rx_Pkcs1EmeOaepEncode(Message, MessageLen, MaskingSeed, em);
	if(Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* Step 3: RSA encryption */
	Status = XHdcp22Rx_Pkcs1Rsaep(KpubRx, em, EncryptedMessage);
	if(Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

/****************************************************************************/
/**
* This function implements the RSAES-OAEP-Decrypt operation. The message is
* 	decrypted using RSADP and then decoded using EME-OAEP.
*
* Reference: PKCS#1 v2.1, Section 7.1.2
*
* @param	InstancePtr is a pointer to the MMULT instance.
* @param	KprivRx is the RSA private key structure containing the quintuple.
* @param	EncryptedMessage is the 128 byte octet string to be decrypted.
* @param	Message is the octet string after decryption.
* @param	MessageLen is the length of the message octet string after decryption.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
*****************************************************************************/
int  XHdcp22Rx_RsaesOaepDecrypt(XHdcp22_Rx *InstancePtr, const XHdcp22_Rx_KprivRx *KprivRx,
	u8 *EncryptedMessage, u8 *Message, int *MessageLen)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(KprivRx != NULL);
	Xil_AssertNonvoid(EncryptedMessage != NULL);
	Xil_AssertNonvoid(Message != NULL);

	u8  em[XHDCP22_RX_N_SIZE];
	u32 Status;

	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_RSA);

	/* Step 1: Length checking, Skip */

	/* Step 2: RSA decryption */
	Status = XHdcp22Rx_Pkcs1Rsadp(InstancePtr, KprivRx, EncryptedMessage, em);
	if(Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* Step 3: EME-OAEP decoding */
	Status = XHdcp22Rx_Pkcs1EmeOaepDecode(em, Message, MessageLen);
	if(Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	XHdcp22Rx_LogWr(InstancePtr, XHDCP22_RX_LOG_EVT_DEBUG, XHDCP22_RX_LOG_DEBUG_COMPUTE_RSA_DONE);

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function generates random octets.
*
* @param  NumOctets is the number of octets in the random number.
* @param  RandomNumberPtr is a pointer to the random number.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XHdcp22Rx_GenerateRandom(XHdcp22_Rx *InstancePtr, int NumOctets,
                              u8* RandomNumberPtr)
{
	/* Use hardware generator */
	XHdcp22Rng_GetRandom(&InstancePtr->RngInst, RandomNumberPtr, NumOctets, NumOctets);
}

/****************************************************************************/
/**
* This function implements the RSAEP primitive.
*
* Reference: PKCS#1 v2.1, Section 5.1.1
*
* @param	KpubRx is the RSA public key structure containing the 1024 bit
* 			modulus and 24 bit public exponent.
* @param	Message is the 128 byte octet string to be encrypted.
* @param	EncryptedMessage is the 128 byte octet string after encryption.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
*****************************************************************************/
static int XHdcp22Rx_Pkcs1Rsaep(const XHdcp22_Rx_KpubRx *KpubRx, u8 *Message, u8 *EncryptedMessage)
{
	/* Verify arguments */
	Xil_AssertNonvoid(KpubRx != NULL);
	Xil_AssertNonvoid(Message != NULL);
	Xil_AssertNonvoid(EncryptedMessage != NULL);

	u32 N[XHDCP22_RX_N_SIZE/4];
	u32 E[XHDCP22_RX_N_SIZE/4];
	u32 M[XHDCP22_RX_N_SIZE/4];
	u32 C[XHDCP22_RX_N_SIZE/4];

	/* Convert octet string to integer */
	mpConvFromOctets(N, XHDCP22_RX_N_SIZE/4, KpubRx->N, XHDCP22_RX_N_SIZE);
	mpConvFromOctets(E, XHDCP22_RX_N_SIZE/4, KpubRx->e, 3);
	mpConvFromOctets(M, XHDCP22_RX_N_SIZE/4, Message, XHDCP22_RX_N_SIZE);

	/* Generate cipher text, c = m^e*mod(n) */
	mpModExp(C, M, E, N, XHDCP22_RX_N_SIZE/4);

	/* Convert integer to octet string */
	mpConvToOctets(C, XHDCP22_RX_N_SIZE/4, EncryptedMessage, XHDCP22_RX_N_SIZE);

	return XST_SUCCESS;
}

/****************************************************************************/
/**
* This function implements the RSADP primitive using the Chinese Remainder
* Theorem (CRT).
*
* Reference: PKCS#1 v2.1, Section 5.1.2
*
* @param	InstancePtr is a pointer to the MMULT instance.
* @param	KprivRx is the RSA private key structure containing the quintuple.
* @param	EncryptedMessage is the 128 byte octet string to be decrypted.
* @param	Message is the 128 byte decrypted octet string.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
*****************************************************************************/
static int XHdcp22Rx_Pkcs1Rsadp(XHdcp22_Rx *InstancePtr, const XHdcp22_Rx_KprivRx *KprivRx,
	u8 *EncryptedMessage, u8 *Message)
{
	/* Verify arguments */
	Xil_AssertNonvoid(InstancePtr != NULL);
	Xil_AssertNonvoid(KprivRx != NULL);
	Xil_AssertNonvoid(EncryptedMessage != NULL);
	Xil_AssertNonvoid(Message != NULL);

	u32 A[XHDCP22_RX_N_SIZE/4];
	u32 B[XHDCP22_RX_N_SIZE/4];
	u32 C[XHDCP22_RX_N_SIZE/4];
	u32 D[XHDCP22_RX_N_SIZE/4];
	u32 M1[XHDCP22_RX_N_SIZE/4];
	u32 M2[XHDCP22_RX_N_SIZE/4];
	u32 Status;

	/* Clear variables */
	memset(A, 0, sizeof(A));
	memset(B, 0, sizeof(B));
	memset(C, 0, sizeof(C));
	memset(D, 0, sizeof(D));
	memset(M1, 0, sizeof(M1));
	memset(M2, 0, sizeof(M2));

	/* Step 2b part I: Generate m1 = c^dP * mod(p) */
	mpConvFromOctets(A, XHdcp22Rx_MpSizeof(A), KprivRx->p, XHDCP22_RX_P_SIZE);
	mpConvFromOctets(B, XHdcp22Rx_MpSizeof(B), KprivRx->dp, XHDCP22_RX_P_SIZE);
	mpConvFromOctets(C, XHdcp22Rx_MpSizeof(C), EncryptedMessage, XHDCP22_RX_N_SIZE);
	mpConvFromOctets(D, XHdcp22Rx_MpSizeof(D), InstancePtr->NPrimeP, XHDCP22_RX_P_SIZE);
	//Status = mpModExp(M1, C, B, A, XHDCP22_RX_N_SIZE/4);
	Status = XHdcp22Rx_Pkcs1MontExp(InstancePtr, M1, C, B, A, D, 16);

	/* Step 2b part I: Generate m2 = c^dQ * mod(q) */
	mpConvFromOctets(A, XHdcp22Rx_MpSizeof(A), KprivRx->q, XHDCP22_RX_P_SIZE);
	mpConvFromOctets(B, XHdcp22Rx_MpSizeof(B), KprivRx->dq, XHDCP22_RX_P_SIZE);
	mpConvFromOctets(D, XHdcp22Rx_MpSizeof(D), InstancePtr->NPrimeQ, XHDCP22_RX_P_SIZE);
	//Status = mpModExp(M2, C, D, B, XHDCP22_RX_N_SIZE/4);
	Status = XHdcp22Rx_Pkcs1MontExp(InstancePtr, M2, C, B, A, D, 16);

	/* Step 2b part II: Skip since u=2 */

	/* Step 2b part III: Generate h = (m1 - m2) * qInv * mod(p) */
	mpConvFromOctets(A, XHdcp22Rx_MpSizeof(A), KprivRx->p, XHDCP22_RX_P_SIZE);
	Status = mpSubtract(D, M1, M2, XHdcp22Rx_MpSizeof(D)); // mdiff = m1 -m2
	if(Status != XST_SUCCESS)
	{
		mpAdd(M1, M1, A, XHdcp22Rx_MpSizeof(M1));
		mpSubtract(D, M1, M2, XHdcp22Rx_MpSizeof(D));
	}
	mpConvFromOctets(C, XHdcp22Rx_MpSizeof(C), KprivRx->qinv, XHDCP22_RX_P_SIZE);
	Status = mpModMult(C, D, C, A, XHDCP22_RX_N_SIZE/4); // h = mdiff * qInv * mod(p)

	/* Step 2b part IV: Generate m = m2 + q * h */
	mpConvFromOctets(A, XHdcp22Rx_MpSizeof(A), KprivRx->q, XHDCP22_RX_P_SIZE);
	Status = mpMultiply(D, A, C, XHDCP22_RX_P_SIZE/4); // qh = q * h
	Status = mpAdd(C, M2, D, XHDCP22_RX_N_SIZE/4); // m = m2 + qh

	/* Convert integer to octet string */
	mpConvToOctets(C, XHdcp22Rx_MpSizeof(C), Message, XHDCP22_RX_N_SIZE);

	return XST_SUCCESS;
}

/****************************************************************************/
/**
* This function implements the Mask Generation Function MGF1.
* The underlying hash function used is SHA256.
*
* Reference: PKCS#1 v2.1, Section B.2.1
*
* @param	Seed is the octet string from which the mask is generated.
* @param	SeedLen is the length of the seed in octet string.
* @param	Mask is the output octet string after MGF1.
* @param	MaskLen is the desired length of the mask with a length
* 			less than or equal to 128 bytes.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
*****************************************************************************/
static int XHdcp22Rx_Pkcs1Mgf1(const u8 *Seed, const u32 SeedLen, u8  *Mask, u32 MaskLen)
{
	/* Verify arguments */
	Xil_AssertNonvoid(Seed != NULL);
	Xil_AssertNonvoid(SeedLen > 0);
	Xil_AssertNonvoid(Mask != NULL);
	Xil_AssertNonvoid(MaskLen > 0);

	u8  Hash[XHDCP22_RX_HASH_SIZE];
	u8  HashData[XHDCP22_RX_N_SIZE];	// SeedLen+4
	u32 C;
	u8  Cbig[4];
	u8  T[XHDCP22_RX_N_SIZE]; 		// MaskLen + XHDCP22_RX_HASH_SIZE

	/* Step 2: Clear T */
	memset(T, 0x00, sizeof(T));

	/* Step 3: T = T || SHA256(mgfSeed || C) */
	memcpy(HashData, Seed, SeedLen);

	for(C=0; (C*XHDCP22_RX_HASH_SIZE) < MaskLen; C++)
	{
		/* Convert counter value to big endian */
		Cbig[0] = ((C >> 24) & 0xFF);
		Cbig[1] = ((C >> 16) & 0xFF);
		Cbig[2] = ((C >> 8) & 0xFF);
		Cbig[3] = (C & 0xFF);

		/* Constructing Hash Input */
		memcpy(HashData+SeedLen, &Cbig, 4);

		/* Computing Hash */
		XHdcp22Cmn_Sha256Hash(HashData, SeedLen+4, Hash);

		/* Appending Hash to T */
		memcpy(T+C*XHDCP22_RX_HASH_SIZE, Hash, XHDCP22_RX_HASH_SIZE);
	}

	/* Step 4: Output leading maskLen octets of T */
	memcpy(Mask, T, MaskLen);

	return XST_SUCCESS;
}

/****************************************************************************/
/**
* This function implements EME-OAEP encoding. The label L is the empty
* string and the underlying hash function is SHA256.
*
* Reference: PKCS#1 v2.1, Section 7.1.1, Part 2
*
* @param	Message is the octet string to be encoded
* @param	MessageLen is the length of the octet string to be encoded.
* @param	MaskingSeed is the random octet string seed of length hLen.
* @param	EncodedMessage is the 128 byte encoded octet string.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
*****************************************************************************/
static int XHdcp22Rx_Pkcs1EmeOaepEncode(const u8 *Message, const u32 MessageLen,
	const u8 *MaskingSeed, u8 *EncodedMessage)
{
	/* Verify arguments */
	Xil_AssertNonvoid(Message != NULL);
	Xil_AssertNonvoid(MessageLen > 0);
	Xil_AssertNonvoid(MaskingSeed != NULL);
	Xil_AssertNonvoid(EncodedMessage != NULL);

	u8  lHash[XHDCP22_RX_HASH_SIZE];
	u8  seed[XHDCP22_RX_HASH_SIZE];
	u8  dbMask[XHDCP22_RX_N_SIZE-XHDCP22_RX_HASH_SIZE-1];
	u8  DB[XHDCP22_RX_N_SIZE-XHDCP22_RX_HASH_SIZE-1];
	u8  seedMask[XHDCP22_RX_HASH_SIZE];
	u32 Status;

	/* Step 2a: L is the empty string */
	XHdcp22Cmn_Sha256Hash(NULL,0,lHash);

	/* Step 2b: Generate PS by initializing DB to zeros */
	memset(DB, 0x00, sizeof(DB));

	/* Step 2c: Generate DB = lHash || PS || 0x01 || M */
	memcpy(DB, lHash, XHDCP22_RX_HASH_SIZE);
	DB[XHDCP22_RX_N_SIZE-MessageLen-XHDCP22_RX_HASH_SIZE-2] = 0x01;
	memcpy(DB+XHDCP22_RX_N_SIZE-MessageLen-XHDCP22_RX_HASH_SIZE-1, Message, MessageLen);

	/* Step 2d: Generate random seed of length hLen
	 *   The random seed is passed in as an argument to this function.
	 */

	/* Step 2e: Generate dbMask = MGF1(seed, length(DB)) */
	Status = XHdcp22Rx_Pkcs1Mgf1(MaskingSeed, XHDCP22_RX_HASH_SIZE, dbMask,
						XHDCP22_RX_N_SIZE-XHDCP22_RX_HASH_SIZE-1);
	if(Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* Step 2f: Generate maskedDB = DB xor dbMask */
	XHdcp22Rx_Xor(DB, DB, dbMask, XHDCP22_RX_N_SIZE-XHDCP22_RX_HASH_SIZE-1);

	/* Step 2g: Generate seedMask = MGF(maskedDB, length(seed)) */
	Status = XHdcp22Rx_Pkcs1Mgf1(DB, XHDCP22_RX_N_SIZE-XHDCP22_RX_HASH_SIZE-1,
						seedMask, XHDCP22_RX_HASH_SIZE);
	if(Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* Step 2h: Generate maskedSeed = seed xor seedMask */
	XHdcp22Rx_Xor(seed, MaskingSeed, seedMask, XHDCP22_RX_HASH_SIZE);

	/* Step 2i: Form encoded message EM = 0x00 || maskedSeed || maskedDB */
	memset(EncodedMessage, 0x00, XHDCP22_RX_N_SIZE);
	memcpy(EncodedMessage+1, seed, XHDCP22_RX_HASH_SIZE);
	memcpy(EncodedMessage+1+XHDCP22_RX_HASH_SIZE, DB, XHDCP22_RX_N_SIZE-XHDCP22_RX_HASH_SIZE-1);

	return XST_SUCCESS;
}

/****************************************************************************/
/**
* This function implements EME-OAEP decoding. The label L is the empty string
* and the underlying hash function is SHA256.
*
* Reference: PKCS#1 v2.1, Section 7.1.2, Part 3
*
* @param	EncodedMessage is the 128 byte encoded octet string.
* @param	Message is decoded octet string.
* @param	MessageLen is the length of the decoded octet string.
*
* @return	XST_SUCCESS or XST_FAILURE.
*
* @note		None.
*****************************************************************************/
static int XHdcp22Rx_Pkcs1EmeOaepDecode(u8 *EncodedMessage, u8 *Message, int *MessageLen)
{
	/* Verify arguments */
	Xil_AssertNonvoid(EncodedMessage != NULL);
	Xil_AssertNonvoid(Message != NULL);
	Xil_AssertNonvoid(MessageLen != NULL);

	u8  lHash[XHDCP22_RX_HASH_SIZE];
	u8  *maskedSeed;
	u8  seed[XHDCP22_RX_HASH_SIZE];
	u8  *maskedDB;
	u8  DB[XHDCP22_RX_N_SIZE-XHDCP22_RX_HASH_SIZE-1];
	u32 Offset = XHDCP22_RX_HASH_SIZE;
	u32 Status;

	/* Step 3a: L is the empty string */
	XHdcp22Cmn_Sha256Hash(NULL,0,lHash);

	/* Step 3b: Separate EM = Y || maskedSeed || maskedDB */
	maskedSeed = EncodedMessage+1;
	maskedDB = EncodedMessage+1+XHDCP22_RX_HASH_SIZE;

	/* Step 3c: Generate seedMask = MGF(maskedDB, hLen) */
	Status = XHdcp22Rx_Pkcs1Mgf1(maskedDB, XHDCP22_RX_N_SIZE-XHDCP22_RX_HASH_SIZE-1,
						seed, XHDCP22_RX_HASH_SIZE);
	if(Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* Step 3d: Generate seed = maskedSeed xor seedMask */
	XHdcp22Rx_Xor(seed, maskedSeed, seed, XHDCP22_RX_HASH_SIZE);

	/* Step 3e: Generate dbMask = MGF(seed, k-hLen-1) */
	Status = XHdcp22Rx_Pkcs1Mgf1(seed, XHDCP22_RX_HASH_SIZE, DB,
						XHDCP22_RX_N_SIZE-XHDCP22_RX_HASH_SIZE-1);
	if(Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* Step 3f: Generate DB = maskedDB xor dbMask */
	XHdcp22Rx_Xor(DB, maskedDB, DB, XHDCP22_RX_N_SIZE-XHDCP22_RX_HASH_SIZE-1);

	/*
	 * Step 3g: Separate DB = lHash' || PS || 0x01 || M
	 *
	 * - Note: To avoid side channel attacks from now on run all
	 * code even in the error case. This avoids possible timing
	 * attacks as described by Manger.
	 */
	Status = XST_SUCCESS;

	/* Compare Y */
	if(*(EncodedMessage) != 0x00)
	{
		Status = XST_FAILURE;
	}

	/* Compare lHash' */
	if(memcmp(DB, lHash, XHDCP22_RX_HASH_SIZE) != 0)
	{
		Status = XST_FAILURE;
	}

	/* Compare PS */
	for(Offset = XHDCP22_RX_HASH_SIZE; Offset < (XHDCP22_RX_N_SIZE-XHDCP22_RX_HASH_SIZE-1); Offset++)
	{
		if(DB[Offset] == 0x01)
		{
			break;
		}
		else if(DB[Offset] != 0x00)
		{
			Status = XST_FAILURE;
		}
	}

	/* Return Error */
	if(Status != XST_SUCCESS)
	{
		return XST_FAILURE;
	}

	/* Extract M */
	*MessageLen = (XHDCP22_RX_N_SIZE-XHDCP22_RX_HASH_SIZE-1) - (Offset + 1);
	memcpy(Message, DB+Offset+1, *MessageLen);

	return XST_SUCCESS;
}

#ifdef _XHDCP22_RX_SW_MMULT_
/****************************************************************************/
/**
* This function performs a carry propagation adding C to the input
* array A of size NDigits, given by the first argument starting from
* the first element SDigit, and propagates it until no further carry
* is generated.
*
* ADD(A[i],C)
*
* Reference:
* Analyzing and Comparing Montgomery Multiplication Algorithms
* IEEE Micro, 16(3):26-33,June 1996
* By: Cetin Koc, Tolga Acar, and Burton Kaliski
*
* @param	A is an input array of size NDigits
* @param	C is the value being added to the input A
* @param	SDigit is the start digit
* @param	NDigits is the integer precision of the arguments (A)
*
* @return	None.
*
* @note		None.
*****************************************************************************/
static void XHdcp22Rx_Pkcs1MontMultAdd(u32 *A, u32 C, int SDigit, int NDigits)
{
	/* Verify arguments */
	Xil_AssertVoid(A != NULL);
	Xil_AssertVoid(SDigit <= NDigits);

	int i;

	for(i=SDigit; i<NDigits; i++)
	{
		C = mpAdd(A+i, A+i, &C, 1);

		if(C == 0)
		{
			return;
		}
	}
}
#endif

#ifdef _XHDCP22_RX_SW_MMULT_
/****************************************************************************/
/**
* This function implements the Montgomery Modular Multiplication (MMM)
* Finely Integrated Operand Scanning (FIOS) algorithm. The FIOS method
* interleaves multiplication and reduction operations. Requires NDigits+3
* words of temporary storage.
*
* U = MontMult(A,B,N)
*
* Reference:
* Analyzing and Comparing Montgomery Multiplication Algorithms
* IEEE Micro, 16(3):26-33,June 1996
* By: Cetin Koc, Tolga Acar, and Burton Kaliski
*
* @param	U is the MMM result
* @param	A is the n-residue input, A' = A*R mod N
* @param	B is the n-residue input, B' = B*R mod N
* @param	N is the modulus
* @param	NPrime is a pre-computed constant, NPrime = (1-R*Rbar)/N
* @param	NDigits is the integer precision of the arguments (C,A,B,N,NPrime)
*
* @return	None.
*
* @note		None.
*****************************************************************************/
static void XHdcp22Rx_Pkcs1MontMultFiosStub(u32 *U, u32 *A, u32 *B,
	u32 *N, const u32 *NPrime, int NDigits)
{
	/* Verify arguments */
	Xil_AssertVoid(U != NULL);
	Xil_AssertVoid(A != NULL);
	Xil_AssertVoid(B != NULL);
	Xil_AssertVoid(N != NULL);
	Xil_AssertVoid(NPrime != NULL);
	Xil_AssertVoid(NDigits == 16);

	int i, j;
	u32 S, C, C1, C2, M[2], X[2];
	u32 T[XHDCP22_RX_N_SIZE];

	memset(T, 0, 4*(NDigits+3));

	for(i=0; i<NDigits; i++)
	{
		// (C,S) = t[0] + a[0]*b[i], worst case 2 words
		spMultiply(X, A[0], B[i]);	// X[Upper,Lower] = a[0]*b[i]
		C = mpAdd(&S, T+0, X+0, 1);	// [C,S] = t[0] + X[Lower]
		mpAdd(&C, &C, X+1, 1);		// [~,C] = C + X[Upper], No carry

		// ADD(t[1],C)
		XHdcp22Rx_Pkcs1MontMultAdd(T, C, 1, NDigits+3);

		// m = S*n'[0] mod W, where W=2^32
		// Note: X[Upper,Lower] = S*n'[0], m=X[Lower]
		spMultiply(M, S, NPrime[0]);

		// (C,S) = S + m*n[0], worst case 2 words
		spMultiply(X, M[0], N[0]);	// X[Upper,Lower] = m*n[0]
		C = mpAdd(&S, &S, X+0, 1);	// [C,S] = S + X[Lower]
		mpAdd(&C, &C, X+1, 1);		// [~,C] = C + X[Upper]

		for(j=1; j<NDigits; j++)
		{
			// (C,S) = t[j] + a[j]*b[i] + C, worst case 2 words
			spMultiply(X, A[j], B[i]);	 	// X[Upper,Lower] = a[j]*b[i], double precision
			C1 = mpAdd(&S, T+j, &C, 1);		// (C1,S) = t[j] + C
			C2 = mpAdd(&S, &S, X+0, 1); 	// (C2,S) = S + X[Lower]
			mpAdd(&C, &C1, X+1, 1);			// (~,C)  = C1 + X[Upper], doesn't produce carry
			mpAdd(&C, &C, &C2, 1); 			// (~,C)  = C + C2, doesn't produce carry

			// ADD(t[j+1],C)
			XHdcp22Rx_Pkcs1MontMultAdd(T, C, j+1, NDigits+3);

			// (C,S) = S + m*n[j]
			spMultiply(X, M[0], N[j]);	// X[Upper,Lower] = m*n[j]
			C = mpAdd(&S, &S, X+0, 1);	// [C,S] = S + X[Lower]
			mpAdd(&C, &C, X+1, 1);		// [~,C] = C + X[Upper]

			// t[j-1] = S
			T[j-1] = S;
		}

		// (C,S) = t[s] + C
		C = mpAdd(&S, T+NDigits, &C, 1);
		// t[s-1] = S
		T[NDigits-1] = S;
		// t[s] = t[s+1] + C
		mpAdd(T+NDigits, T+NDigits+1, &C, 1);
		// t[s+1] = 0
		T[NDigits+1] = 0;
	}

	/* Step 3: if(u>=n) return u-n else return u */
	if(mpCompare(T, N, NDigits+3) >= 0)
	{
		mpSubtract(T, T, N, NDigits+3);
	}

	memcpy(U, T, 4*NDigits);
}
#endif

/****************************************************************************/
/**
* This function initializes the Montgomery Multiplier (MMULT) hardware
* by writing the N and NPrime registers.
*
* U = MontMult(A,B,N)
*
* Reference:
* Analyzing and Comparing Montgomery Multiplication Algorithms
* IEEE Micro, 16(3):26-33,June 1996
* By: Cetin Koc, Tolga Acar, and Burton Kaliski
*
* @param	InstancePtr is a pointer to the MMULT instance.
* @param	N is the modulus
* @param	NPrime is a pre-computed constant, NPrime = (1-R*Rbar)/N
* @param	NDigits is the integer precision of the arguments (C,A,B,N,NPrime)
*
* @return	None.
*
* @note		None.
*****************************************************************************/
static void XHdcp22Rx_Pkcs1MontMultFiosInit(XHdcp22_Rx *InstancePtr, u32 *N,
	const u32 *NPrime, int NDigits)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(N != NULL);
	Xil_AssertVoid(NPrime != NULL);
	Xil_AssertVoid(NDigits == 16);

	/* Check Ready */
	while(XHdcp22_mmult_IsReady(&InstancePtr->MmultInst) == 0);

	/* Write Register N */
	XHdcp22_mmult_Write_N_Words(&InstancePtr->MmultInst, 0, (int *)N, NDigits);

	/* Write Register NPrime */
	XHdcp22_mmult_Write_NPrime_Words(&InstancePtr->MmultInst, 0, (int *)NPrime, NDigits);
}

#ifndef _XHDCP22_RX_SW_MMULT_
/****************************************************************************/
/**
* This function runs the Montgomery Multiplier (MMULT) hardware to perform
* the modular multiplication operation required by RSA decryption.
*
* U = MontMult(A,B,N)
*
* @param	InstancePtr is a pointer to the MMULT instance.
* @param	U is the MMM result
* @param	A is the n-residue input, A' = A*R mod N
* @param	B is the n-residue input, B' = B*R mod N
* @param	NDigits is the integer precision of the arguments (C,A,B,N,NPrime)
*
* @return	None.
*
* @note		None.
*****************************************************************************/
static void XHdcp22Rx_Pkcs1MontMultFios(XHdcp22_Rx *InstancePtr, u32 *U,
	u32 *A, u32 *B, int NDigits)
{
	/* Verify arguments */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(U != NULL);
	Xil_AssertVoid(A != NULL);
	Xil_AssertVoid(B != NULL);
	Xil_AssertVoid(NDigits == 16);

	/* Check Ready */
	while(XHdcp22_mmult_IsReady(&InstancePtr->MmultInst) == 0);

	/* Write Register A */
	XHdcp22_mmult_Write_A_Words(&InstancePtr->MmultInst, 0, (int *)A, NDigits);

	/* Write Register B */
	XHdcp22_mmult_Write_B_Words(&InstancePtr->MmultInst, 0, (int *)B, NDigits);

	/* Run MontMult */
	XHdcp22_mmult_Start(&InstancePtr->MmultInst);

	/* Poll Result */
	while(XHdcp22_mmult_IsDone(&InstancePtr->MmultInst) == 0);

	/* Read Register U */
	XHdcp22_mmult_Read_U_Words(&InstancePtr->MmultInst, 0, (int *)U, NDigits);
}
#endif

/****************************************************************************/
/**
* This function performs the modular exponentation operation using the
* binary square and multiply method.
*
* C = ModExp(A, E, N) = A^E*mod(N)
*
* @param	C is result of the modular exponentiation
* @param	A is the base
* @param	E is the exponent
* @param	N is the modulus
* @param	NPrime is a constant
* @param	NDigits is the integer precision of the arguments (C,A,B,N,NPrime).
* 			Maximum integer precision is 16.
*
* @return	None.
*
* @note		None.
*****************************************************************************/
static int XHdcp22Rx_Pkcs1MontExp(XHdcp22_Rx *InstancePtr, u32 *C, u32 *A,
	u32 *E, u32 *N, const u32 *NPrime, int NDigits)
{
	int Offset;
	u32 R[XHDCP22_RX_N_SIZE/4];
	u32 Abar[XHDCP22_RX_N_SIZE/4];
	u32 Xbar[XHDCP22_RX_N_SIZE/4];

	memset(R, 0, sizeof(R));
	memset(Abar, 0, sizeof(Abar));
	memset(Xbar, 0, sizeof(Xbar));

#ifndef _XHDCP22_RX_SW_MMULT_
	XHdcp22Rx_Pkcs1MontMultFiosInit(InstancePtr, N, NPrime, NDigits);
#endif

	/* Step 0: R = 2^(NDigits*32) */
	R[0] = 1;
	mpShiftLeft(R, R, NDigits*32, XHDCP22_RX_N_SIZE/4);

	/* Step 1: Xbar = 1*R*mod(N) */
	mpModulo(Xbar, R, XHDCP22_RX_N_SIZE/4, N, NDigits); // Optimization

	/* Step 2: Abar = A*R*mod(N) */
	mpModMult(Abar, A, Xbar, N, 2*NDigits);

	/* Step 3: Binary square and multiply */
	for(Offset=32*NDigits-1; Offset>=0; Offset--)
	{
#ifndef _XHDCP22_RX_SW_MMULT_
		XHdcp22Rx_Pkcs1MontMultFios(InstancePtr, Xbar, Xbar, Xbar, NDigits);
#else
		XHdcp22Rx_Pkcs1MontMultFiosStub(Xbar, Xbar, Xbar, N, NPrime, NDigits);
#endif

		if(mpGetBit(E, NDigits, Offset) == TRUE)
		{
#ifndef _XHDCP22_RX_SW_MMULT_
			XHdcp22Rx_Pkcs1MontMultFios(InstancePtr, Xbar, Xbar, Abar, NDigits);
#else
			XHdcp22Rx_Pkcs1MontMultFiosStub(Xbar, Xbar, Abar, N, NPrime, NDigits);
#endif
		}
	}

	/* Step 4: C=MonPro(Xbar,1) */
	memset(R, 0, sizeof(R));
	R[0] = 1;

#ifndef _XHDCP22_RX_SW_MMULT_
	XHdcp22Rx_Pkcs1MontMultFios(InstancePtr, C, Xbar, R, NDigits);
#else
	XHdcp22Rx_Pkcs1MontMultFiosStub(C, Xbar, R, N, NPrime, NDigits);
#endif

	return XST_SUCCESS;
}

/****************************************************************************/
/**
* This function calculates XOR for any array.
*
* @param	Cout is the XOR of the input arrays.
* @param    Ain is the input byte array.
* @param    Bin is the input byte array.
* @param    Len is the length of the arrays in bytes.
*
* @return	None.
*
* @note		None.
*****************************************************************************/
static void XHdcp22Rx_Xor(u8 *Cout, const u8 *Ain, const u8 *Bin, u32 Len)
{
	while(Len--)
	{
		Cout[Len] = Ain[Len] ^ Bin[Len];
	}
}

/*****************************************************************************/
/**
* This function computes the derived keys used during HDCP 2.2 authentication
* and key exchange.
*
* Reference: HDCP v2.2, section 2.7
*
* @param	Rrx is the Rx random generated value.
* @param	Rtx is the Tx random generated value.
* @param	Km is the master key generated by tx.
* @param	Rn is the 64-bit psuedo-random nonce generated by the transmitter.
* @param	Ctr is the 64-bit AES counter value.
* @param	DKey is the 128-bit derived key.
*
* @return	None.
*
* @note		None.
******************************************************************************/
static void XHdcp22Rx_ComputeDKey(const u8* Rrx, const u8* Rtx, const u8 *Km,
	const u8 *Rn, u8 *Ctr, u8 *DKey)
{
	u8 Aes_Iv[XHDCP22_RX_AES_SIZE];
	u8 Aes_Key[XHDCP22_RX_AES_SIZE];

	/* Verify arguments */
	Xil_AssertVoid(Rrx != NULL);
	Xil_AssertVoid(Rtx != NULL);
	Xil_AssertVoid(Km != NULL);
	Xil_AssertVoid(DKey != NULL);

	/* AES Key = Km xor Rn */
	memcpy(Aes_Key, Km, XHDCP22_RX_AES_SIZE);
	if(Rn != NULL)
	{
		XHdcp22Rx_Xor(Aes_Key+XHDCP22_RX_RN_SIZE, Km+XHDCP22_RX_RN_SIZE, Rn, XHDCP22_RX_RN_SIZE);
	}

	/* AES Input = Rtx || (Rrx xor Ctr) */
	memcpy(Aes_Iv, Rtx, XHDCP22_RX_RTX_SIZE);
	if(Ctr == NULL)
	{
		memcpy(&Aes_Iv[XHDCP22_RX_RRX_SIZE], Rrx, XHDCP22_RX_RRX_SIZE);
	}
	else
	{
		XHdcp22Rx_Xor(Aes_Iv+XHDCP22_RX_RTX_SIZE, Rrx, Ctr, XHDCP22_RX_RRX_SIZE);
	}

	XHdcp22Cmn_Aes128Encrypt(Aes_Iv, Aes_Key, DKey);
}

/*****************************************************************************/
/**
* This function computes HPrime used during HDCP 2.2 authentication and key
* exchange.
*
* Reference: HDCP v2.2, section 2.2
*
* @param	Rrx is the Rx random generated value.
* @param	RxCaps are the capabilities of the receiver.
* @param	Rtx is the Tx random generated value.
* @param	TxCaps are the capabilities of the receiver.
* @param	Km is the master key generated by tx.
* @param	HPrime is a pointer to the HPrime hash from the HDCP2.2 receiver.
*
* @return	None.
*
* @note		None.
******************************************************************************/
void XHdcp22Rx_ComputeHPrime(const u8* Rrx, const u8 *RxCaps, const u8* Rtx,
	const u8 *TxCaps, const u8 *Km, u8 *HPrime)
{
	u8 HashInput[XHDCP22_RX_RTX_SIZE + XHDCP22_RX_RXCAPS_SIZE + XHDCP22_RX_TXCAPS_SIZE];
	int Idx = 0;
	u8 Ctr[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
	u8 Kd[2 * XHDCP22_RX_AES_SIZE]; /* dkey0 || dkey 1 */


	/* Verify arguments */
	Xil_AssertVoid(Rrx != NULL);
	Xil_AssertVoid(RxCaps != NULL);
	Xil_AssertVoid(Rtx != NULL);
	Xil_AssertVoid(TxCaps != NULL);
	Xil_AssertVoid(Km != NULL);
	Xil_AssertVoid(HPrime != NULL);

	/* Generate derived keys dkey0 and dkey1
	   HashKey Kd = dkey0 || dkey1 */
	XHdcp22Rx_ComputeDKey(Rrx, Rtx, Km, NULL, NULL, Kd);
	XHdcp22Rx_ComputeDKey(Rrx, Rtx, Km, NULL, Ctr, Kd+XHDCP22_RX_AES_SIZE);

	/* HashInput = Rtx || RxCaps || TxCaps */
	memcpy(HashInput, Rtx, XHDCP22_RX_RTX_SIZE);
	Idx += XHDCP22_RX_RTX_SIZE;
	memcpy(&HashInput[Idx], RxCaps, XHDCP22_RX_RXCAPS_SIZE);
	Idx += XHDCP22_RX_RXCAPS_SIZE;
	memcpy(&HashInput[Idx], TxCaps, XHDCP22_RX_TXCAPS_SIZE);

	/* Compute H' = HMAC-SHA256(HashInput, Kd) */
	XHdcp22Cmn_HmacSha256Hash(HashInput, sizeof(HashInput), Kd, XHDCP22_RX_KD_SIZE, HPrime);
}

/*****************************************************************************/
/**
* This function computes Ekh used during HDCP 2.2 authentication and key
* exchange for pairing with receiver.
*
* Reference: HDCP v2.2, section 2.2.1
*
* @param	KprivRx is the RSA private key structure containing the quintuple.
* @param	Km is the master key generated by tx.
* @param	M is constructed by concatenating Rtx || Rrx.
* @param	Ekh is the encrypted Km used for pairing.
*
* @return	None.
*
* @note		None.
******************************************************************************/
void XHdcp22Rx_ComputeEkh(const u8 *KprivRx, const u8 *Km, const u8 *M, u8 *Ekh)
{
	u8 Kh[XHDCP22_RX_HASH_SIZE];

	/* Verify arguments */
	Xil_AssertVoid(KprivRx != NULL);
	Xil_AssertVoid(Km != NULL);
	Xil_AssertVoid(M != NULL);
	Xil_AssertVoid(Ekh != NULL);

	/* Generate Kh = SHA256(p || q || dP || dQ || qInv)[127:0] */
	XHdcp22Cmn_Sha256Hash(KprivRx, sizeof(XHdcp22_Rx_KprivRx), Kh);

	/* Compute Ekh = AES128(Kh, (Rtx || Rrx)) xor Km */
	XHdcp22Cmn_Aes128Encrypt(M, Kh+XHDCP22_RX_EKH_SIZE, Ekh);
	XHdcp22Rx_Xor(Ekh, Ekh, Km, XHDCP22_RX_EKH_SIZE);
}

/*****************************************************************************/
/**
* This function computes LPrime used during HDCP 2.2 locality check.
*
* Reference: HDCP v2.2, section 2.3
*
* @param	Rn is the 64-bit psuedo-random nonce generated by the transmitter.
* @param	Km is the 128-bit master key generated by tx.
* @param	Rrx is the 64-bit pseudo-random number generated by the receiver.
* @param	Rtx is the 64-bit pseudo-random number generated by the transmitter.
* @param	LPrime is the 256-bit value generated for locality check.
*
* @return	None.
*
* @note		None.
******************************************************************************/
void XHdcp22Rx_ComputeLPrime(const u8 *Rn, const u8 *Km, const u8 *Rrx, const u8 *Rtx, u8 *LPrime)
{
	u8 HashKey[XHDCP22_RX_KD_SIZE];
	u8 Ctr[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
	u8 Kd[2 * XHDCP22_RX_AES_SIZE]; /* dkey0 || dkey 1 */

	/* Verify arguments */
	Xil_AssertVoid(Rn != NULL);
	Xil_AssertVoid(Km != NULL);
	Xil_AssertVoid(Rrx != NULL);
	Xil_AssertVoid(Rtx != NULL);
	Xil_AssertVoid(LPrime != NULL);

	/* Generate derived keys dkey0 and dkey1
	   HashKey Kd = dkey0 || dkey1 */
	XHdcp22Rx_ComputeDKey(Rrx, Rtx, Km, NULL, NULL, Kd);
	XHdcp22Rx_ComputeDKey(Rrx, Rtx, Km, NULL, Ctr, Kd+XHDCP22_RX_AES_SIZE);

	/* HashKey = Kd[256:64] || (Kd[63:0] xor Rrx) */
	memcpy(HashKey, Kd, XHDCP22_RX_KD_SIZE);
	XHdcp22Rx_Xor(HashKey+(XHDCP22_RX_KD_SIZE-XHDCP22_RX_RRX_SIZE),
		Kd+(XHDCP22_RX_KD_SIZE-XHDCP22_RX_RRX_SIZE), Rrx, XHDCP22_RX_RRX_SIZE);

	/* LPrime = HMAC-SHA256(Rn, HashKey) */
	XHdcp22Cmn_HmacSha256Hash(Rn, XHDCP22_RX_RN_SIZE, HashKey, XHDCP22_RX_KD_SIZE, LPrime);
}

/*****************************************************************************/
/**
* This function computes the Ks used during HDCP 2.2 session key exchange.
*
* Reference: HDCP v2.2, section 2.4
*
* @param	Rrx is the Rx random generated value.
* @param	Rtx is the Tx random generated value.
* @param	Km is the master key generated by the transmitter.
* @param	Rn is the 64-bit psuedo-random nonce generated by the transmitter.
* @param	Eks is the encrypted session generated by the transmitter.
* @param	Ks is the decrypted session key.
*
* @return	None.
*
* @note		None.
******************************************************************************/
void XHdcp22Rx_ComputeKs(const u8* Rrx, const u8* Rtx, const u8 *Km, const u8 *Rn,
	     const u8 *Eks, u8 *Ks)
{
	u8 Dkey2[XHDCP22_RX_KS_SIZE];
	u8 Ctr[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};

	/* Verify arguments */
	Xil_AssertVoid(Rrx != NULL);
	Xil_AssertVoid(Rtx != NULL);
	Xil_AssertVoid(Km != NULL);
	Xil_AssertVoid(Rn != NULL);
	Xil_AssertVoid(Eks != NULL);
	Xil_AssertVoid(Ks != NULL);

	/* Generate derived key dkey2 */
	XHdcp22Rx_ComputeDKey(Rrx, Rtx, Km, Rn, Ctr, Dkey2);

	/* Compute Ks = EKs xor (Dkey2 xor Rrx) */
	memcpy(Ks, Dkey2, XHDCP22_RX_KS_SIZE);
	XHdcp22Rx_Xor(Ks+XHDCP22_RX_RRX_SIZE, Ks+XHDCP22_RX_RRX_SIZE, Rrx, XHDCP22_RX_RRX_SIZE);
	XHdcp22Rx_Xor(Ks, Ks, Eks, XHDCP22_RX_KS_SIZE);
}

/*****************************************************************************/
/**
* This function computes VPrime used during HDCP 2.2 repeater
* authentication.
*
* Reference: HDCP v2.2, section 2.3
*
* @param  ReceiverIdList is a list of downstream receivers IDs in big-endian
*         order. Each receiver ID is 5 Bytes.
* @param  ReceiverIdListSize is the number of receiver Ids in ReceiverIdList.
*         There can be between 1 and 31 devices in the list.
* @param  RxInfo is the 16-bit field in the RepeaterAuth_Send_ReceiverID_List
*         message.
* @param  Km is the 128-bit master key generated by tx.
* @param  Rrx is the 64-bit pseudo-random number generated by the receiver.
* @param  Rtx is the 64-bit pseudo-random number generated by the transmitter.
* @param  VPrime is the 256-bit value generated for repeater authentication.
*
* @return None.
*
* @note   None.
******************************************************************************/
void XHdcp22Rx_ComputeVPrime(const u8 *ReceiverIdList, u32 ReceiverIdListSize,
       const u8 *RxInfo, const u8 *SeqNumV, const u8 *Km, const u8 *Rrx,
       const u8 *Rtx, u8 *VPrime)
{
	int Idx = 0;
	u8 HashInput[XHDCP22_RX_SEQNUMV_SIZE +
					XHDCP22_RX_RXINFO_SIZE +
					(XHDCP22_RX_MAX_DEVICE_COUNT*XHDCP22_RX_RCVID_SIZE)];
	int HashInputSize = (ReceiverIdListSize*XHDCP22_RX_RCVID_SIZE) +
					XHDCP22_RX_SEQNUMV_SIZE + XHDCP22_RX_RXINFO_SIZE;
	u8 Ctr[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
	u8 Kd[2 * XHDCP22_RX_AES_SIZE]; /* dkey0 || dkey 1 */

	/* Verify arguments */
	Xil_AssertVoid(ReceiverIdList != NULL);
	Xil_AssertVoid(ReceiverIdListSize > 0);
	Xil_AssertVoid(RxInfo != NULL);
	Xil_AssertVoid(SeqNumV != NULL);
	Xil_AssertVoid(Km != NULL);
	Xil_AssertVoid(Rrx != NULL);
	Xil_AssertVoid(Rtx != NULL);
	Xil_AssertVoid(VPrime != NULL);

	/* Generate derived keys dkey0 and dkey1
	   HashKey Kd = dkey0 || dkey1 */
	XHdcp22Rx_ComputeDKey(Rrx, Rtx, Km, NULL, NULL, Kd);
	XHdcp22Rx_ComputeDKey(Rrx, Rtx, Km, NULL, Ctr, Kd+XHDCP22_RX_AES_SIZE);

	/* HashInput = ReceiverIdList || RxInfo || SeqNumV */
	memcpy(HashInput, ReceiverIdList, ReceiverIdListSize*XHDCP22_RX_RCVID_SIZE);
	Idx += ReceiverIdListSize*XHDCP22_RX_RCVID_SIZE;
	memcpy(&HashInput[Idx], RxInfo, XHDCP22_RX_RXINFO_SIZE);
	Idx += XHDCP22_RX_RXINFO_SIZE;
	memcpy(&HashInput[Idx], SeqNumV, XHDCP22_RX_SEQNUMV_SIZE);

	/* VPrime = HMAC-SHA256(HashInput, Kd) */
	XHdcp22Cmn_HmacSha256Hash(HashInput, HashInputSize, Kd, XHDCP22_RX_KD_SIZE, VPrime);
}

/*****************************************************************************/
/**
* This function computes VPrime used during HDCP 2.2 repeater
* authentication.
*
* Reference: HDCP v2.2, section 2.3
*
* @param  StreamIdType is the 16-bit field in the RepeaterAuth_Send_ReceiverID_List
* 			  message.
* @param  SeqNumM is the 24-bit field in the RepeaterAuth_Stream_Manage
* 			  message.
* @param  Km is the 128-bit master key generated by tx.
* @param  Rrx is the 64-bit pseudo-random number generated by the receiver.
* @param  Rtx is the 64-bit pseudo-random number generated by the transmitter.
* @param  MPrime is the 256-bit value generated for repeater stream
*         management ready.
*
* @return None.
*
* @note	  None.
******************************************************************************/
void XHdcp22Rx_ComputeMPrime(const u8 *StreamIdType, const u8 *SeqNumM,
       const u8 *Km, const u8 *Rrx, const u8 *Rtx, u8 *MPrime)
{
	int Idx = 0;
	u8 HashInput[XHDCP22_RX_STREAMID_SIZE + XHDCP22_RX_SEQNUMM_SIZE];
	u8 HashKey[XHDCP22_RX_HASH_SIZE];
	u8 Ctr[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
	u8 Kd[2 * XHDCP22_RX_AES_SIZE]; /* dkey0 || dkey 1 */

	/* Verify arguments */
	Xil_AssertVoid(StreamIdType != NULL);
	Xil_AssertVoid(SeqNumM != NULL);
	Xil_AssertVoid(Km != NULL);
	Xil_AssertVoid(Rrx != NULL);
	Xil_AssertVoid(Rtx != NULL);
	Xil_AssertVoid(MPrime != NULL);

	/* HashInput = StreamIdType || SeqNumM */
	memcpy(HashInput, StreamIdType, XHDCP22_RX_STREAMID_SIZE);
	Idx += XHDCP22_RX_STREAMID_SIZE;
	memcpy(&HashInput[Idx], SeqNumM, XHDCP22_RX_SEQNUMM_SIZE);

	/* Generate derived keys dkey0 and dkey1
	   HashKey Kd = dkey0 || dkey1 */
	XHdcp22Rx_ComputeDKey(Rrx, Rtx, Km, NULL, NULL, Kd);
	XHdcp22Rx_ComputeDKey(Rrx, Rtx, Km, NULL, Ctr, Kd+XHDCP22_RX_AES_SIZE);

	/* Hashkey = SHA256(Kd) */
	XHdcp22Cmn_Sha256Hash(Kd, XHDCP22_RX_KD_SIZE, HashKey);

	/* VPrime = HMAC-SHA256(HashInput, Kd) */
	XHdcp22Cmn_HmacSha256Hash(HashInput, sizeof(HashInput), HashKey, XHDCP22_RX_HASH_SIZE, MPrime);
}

/** @} */
