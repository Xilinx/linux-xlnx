// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDCP2X Cryptography driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 *
 * This driver provides Xilinx HDCP 2X transmitter cryptographic functionality.
 *
 * References:
 *
 * http://www.citi.umich.edu/projects/nfsv4/rfc/pkcs-1v2-1.pdf
 * https://www.cryptrec.go.jp/cryptrec_03_spec_cypherlist_files/PDF/pkcs-1v2-12.pdf
 * https://www.digital-cp.com/sites/default/files/HDCP%20on%20DisplayPort%20Specification%20Rev2_3.pdf
 */

#include <crypto/aes.h>
#include <crypto/sha2.h>
#include <linux/xlnx/xlnx_hdcp_common.h>
#include "xlnx_hdcp2x_tx.h"

#define BD_MAX_MOD_SIZE  (HDCP2X_TX_CERT_RSA_PARAMETER_SIZE / sizeof(u32))

#define XHDCP2X_TX_SHA256_SIZE		(256 / 8)
#define XHDCP2X_TX_INNER_PADDING_BYTE	0x36
#define XHDCP2X_TX_OUTER_PADDING_BYTE	0x5C

/*
 * DER encoding T of the Digestinfo value is equal to this hash values
 * Reference: http://www.citi.umich.edu/projects/nfsv4/rfc/pkcs-1v2-1.pdf
 * Section 8.2.2 and 9.2.
 */
static u8 ti_identifier[] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
			     0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
			     0x00, 0x04, 0x20};

/* RSA OAEP masking function */
static void xlnx_hdcp2x_tx_mg_f1(const u8 *seed, unsigned int seedlen,
				 u8 *mask, unsigned int mask_len)
{
	u8  hash_data[HDCP2X_TX_CERT_PUB_KEY_N_SIZE] = {0};
	u8  tx_cert_key[HDCP2X_TX_CERT_PUB_KEY_N_SIZE] = {0};
	u8  hash[HDCP2X_TX_SHA256_HASH_SIZE] = {0};
	u32 i;

	memcpy(hash_data, seed, seedlen);

	/*
	 * Reference: https://www.cryptrec.go.jp/cryptrec_03_spec_cypherlist_files/PDF/pkcs-1v2-12.pdf
	 * Section: 7.1
	 */
	for (i = 0; (i * HDCP2X_TX_SHA256_HASH_SIZE) < mask_len; i++) {
		u32 counter;

		counter = ntohl(i);
		memcpy(hash_data + seedlen, &counter, HDCP2X_TX_CERT_PUBLIC_EXPONENT_E);
		sha256(hash_data, seedlen + HDCP2X_TX_CERT_PUBLIC_EXPONENT_E, hash);
		memcpy(tx_cert_key + (i * HDCP2X_TX_SHA256_HASH_SIZE), hash,
		       HDCP2X_TX_SHA256_HASH_SIZE);
	}
	memcpy(mask, tx_cert_key, mask_len);
}

static void xlnx_hdcp2x_tx_memxor(u8 *out, const u8 *inputparam1,
				  const u8 *inputparam2, u32 size)
{
	u32 i;

	for (i = 0; i < size; i++)
		out[i] = inputparam1[i] ^ inputparam2[i];
}

/* Reference: PKCS#1 v2.1, Section 7.1.1, Part 2 */
static void xlnx_hdcp2x_tx_pkcs1_eme_oaep_encode(const u8 *message, const u32 message_length,
						 const u8 *masking_seed, u8 *encoded_msg)
{
	u8  db_mask[HDCP2X_TX_CERT_PUB_KEY_N_SIZE - HDCP2X_TX_SHA256_HASH_SIZE - 1] = {0};
	u8  db[HDCP2X_TX_CERT_PUB_KEY_N_SIZE - HDCP2X_TX_SHA256_HASH_SIZE - 1] = {0};
	u8  seed_mask[HDCP2X_TX_SHA256_HASH_SIZE] = {0};
	u8  l_hash[HDCP2X_TX_SHA256_HASH_SIZE] = {0};
	u8  seed[HDCP2X_TX_SHA256_HASH_SIZE] = {0};

	/* Step 2a: l_hash is the empty string */
	sha256(NULL, 0, l_hash);

	/* Step 2b: Generate PS by initializing DB to zeros */
	memcpy(db, l_hash, HDCP2X_TX_SHA256_HASH_SIZE);

	/* Step 2c: Generate DB = lHash || PS || 0x01 || M */
	db[HDCP2X_TX_CERT_PUB_KEY_N_SIZE - message_length -
	   HDCP2X_TX_SHA256_HASH_SIZE - 2] = 0x01;

	/*
	 * Step 2d: Generate random seed of length hLen
	 * The random seed is passed in as an argument to this function.
	 */
	memcpy(db + HDCP2X_TX_CERT_PUB_KEY_N_SIZE - message_length -
	       HDCP2X_TX_SHA256_HASH_SIZE - 1,
	       message, message_length);

	/* Step 2e: Generate dbMask = MGF1(seed, length(DB)) */
	xlnx_hdcp2x_tx_mg_f1(masking_seed,
			     HDCP2X_TX_SHA256_HASH_SIZE, db_mask,
			     HDCP2X_TX_CERT_PUB_KEY_N_SIZE - HDCP2X_TX_SHA256_HASH_SIZE
			     - 1);

	/* Step 2f: Generate maskedDB = DB xor dbMask */
	xlnx_hdcp2x_tx_memxor(db, db, db_mask,
			      HDCP2X_TX_CERT_PUB_KEY_N_SIZE - HDCP2X_TX_SHA256_HASH_SIZE - 1);

	/* Step 2g: Generate seedMask = MGF(maskedDB, length(seed)) */
	xlnx_hdcp2x_tx_mg_f1(db,
			     HDCP2X_TX_CERT_PUB_KEY_N_SIZE - HDCP2X_TX_SHA256_HASH_SIZE - 1,
			     seed_mask, HDCP2X_TX_SHA256_HASH_SIZE);

	/* Step 2h: Generate maskedSeed = seed xor seedMask */
	xlnx_hdcp2x_tx_memxor(seed, masking_seed, seed_mask,
			      HDCP2X_TX_SHA256_HASH_SIZE);

	/* Step 2i: Form encoded message EM = 0x00 || maskedSeed || maskedDB */
	memset(encoded_msg, 0, HDCP2X_TX_CERT_PUB_KEY_N_SIZE);
	memcpy(encoded_msg + 1, seed, HDCP2X_TX_SHA256_HASH_SIZE);
	memcpy(encoded_msg + 1 + HDCP2X_TX_SHA256_HASH_SIZE, db,
	       HDCP2X_TX_CERT_PUB_KEY_N_SIZE - HDCP2X_TX_SHA256_HASH_SIZE - 1);
}

static int xlnx_hdcp2x_tx_rsa_encrypt(const u8 *rsa_public_key, int public_key_size,
				      const u8 *exponent_key, int exponent_key_size,
				      const u8 *msg, int msg_size, u8 *encrypted_msg)
{
	unsigned int *n, *e, *m, *s;
	unsigned int mod_size = public_key_size / sizeof(unsigned int);

	if (msg_size != public_key_size)
		return -EINVAL;

	n = kzalloc(4 * BD_MAX_MOD_SIZE * sizeof(u32), GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	/*
	 * Divide the mem allocated to n into 4 parts such that
	 * e follows n, m follows e and s follows m.
	 */
	e = &n[BD_MAX_MOD_SIZE];
	m = &e[BD_MAX_MOD_SIZE];
	s = &m[BD_MAX_MOD_SIZE];

	mp_conv_from_octets(n, mod_size, rsa_public_key, public_key_size);
	mp_conv_from_octets(e, mod_size, exponent_key, exponent_key_size);

	mp_conv_from_octets(m, mod_size, msg, msg_size);
	mp_mod_exp(s, m, e, n, mod_size);
	mp_conv_to_octets(s, mod_size, encrypted_msg, msg_size);

	kfree(n);

	return 0;
}

/* Reference: PKCS#1 v2.1, Section 7.1. */
static int xlnx_hdcp2x_tx_rsa_oae_encrypt(const u8 *rsa_public_key,
					  int public_key_size,
					  const u8 *exponent_key, int exponent_key_size,
					  const u8 *message, const u32 message_length,
					  const u8 *masking_seed, u8 *encrypted_msg)
{
	int status;
	u8 encrypt_msg[HDCP2X_TX_CERT_PUB_KEY_N_SIZE];

	/* Step 1: Length checking */
	if (message_length > (HDCP2X_TX_CERT_PUB_KEY_N_SIZE -
			2 * HDCP2X_TX_SHA256_HASH_SIZE - 2))
		return -EINVAL;

	/* Step 2: EME-OAEP Encoding */
	xlnx_hdcp2x_tx_pkcs1_eme_oaep_encode(message, message_length,
					     masking_seed, encrypt_msg);

	/* Step 3: RSA encryption */
	status = xlnx_hdcp2x_tx_rsa_encrypt(rsa_public_key, public_key_size,
					    exponent_key, exponent_key_size,
					    encrypt_msg, public_key_size,
					    encrypted_msg);
	if (status)
		return -EINVAL;

	return 0;
}

/* Reference: PKCS#1 v2.1, Section 8.2.2 and Section 9.2 */
static int xlnx_hdcp2x_tx_rsa_signature_verify(const u8 *msg_ptr, int msg_size,
					       const u8 *signature,
					       const u8 *dcp_cert_nvalue, int dcp_cert_nsize,
					       const u8 *dcp_cert_evalue, int dcp_cert_esize)
{
	u8 encrypted_msg[HDCP2X_TX_CERT_SIGNATURE_SIZE];
	u8 t_hash[HDCP2X_TX_SHA256_HASH_SIZE];
	u8 *encrypted_msg_ptr = NULL;
	int i;
	int result = 0;

	sha256(msg_ptr, msg_size, t_hash);

	result = xlnx_hdcp2x_tx_rsa_encrypt(dcp_cert_nvalue, dcp_cert_nsize,
					    dcp_cert_evalue, dcp_cert_esize,
					    signature, HDCP2X_TX_CERT_SIGNATURE_SIZE,
					    encrypted_msg);
	if (result)
		return -EINVAL;

	if (encrypted_msg[0] != 0 || encrypted_msg[1] != 1)
		return -EFAULT;

	encrypted_msg_ptr = &encrypted_msg[HDCP2X_TX_CERT_RSVD_SIZE];
	for (i = 0; i < HDCP2X_TX_CERT_PADDING_BYTES; i++) {
		if (encrypted_msg_ptr[i] != GENMASK(7, 0))
			return -EFAULT;
	}

	encrypted_msg_ptr = &encrypted_msg[HDCP2X_TX_CERT_PADDING_END_DELIMITER];
	if (encrypted_msg_ptr[0])
		return -EFAULT;

	encrypted_msg_ptr = &encrypted_msg[HDCP2X_TX_CERT_PADDING_TI_IDENTIFIER];
	if (memcmp(ti_identifier, encrypted_msg_ptr, HDCP2X_TX_CERT_TI_IDENTIFIER_SIZE))
		return -EFAULT;

	encrypted_msg_ptr = &encrypted_msg[HDCP2X_TX_CERT_PADDING_T_HASH];
	if (memcmp(t_hash, encrypted_msg_ptr, HDCP2X_TX_CERT_T_HASH_SIZE))
		return -EFAULT;

	return result;
}

int xlnx_hdcp2x_tx_verify_certificate(const struct hdcp2x_tx_cert_rx *rx_certificate,
				      const u8 *dcp_cert_nvalue, int dcp_cert_nsize,
				      const u8 *dcp_cert_evalue, int dcp_cert_esize)
{
	return xlnx_hdcp2x_tx_rsa_signature_verify((u8 *)rx_certificate,
						  (sizeof(struct hdcp2x_tx_cert_rx) -
						  sizeof(rx_certificate->signature)),
						  rx_certificate->signature,
						  dcp_cert_nvalue, dcp_cert_nsize,
						  dcp_cert_evalue, dcp_cert_esize);
}

int xlnx_hdcp2x_verify_srm(const u8 *srm, int srm_size,
			   const u8 *dcp_cert_nvalue, int dcp_cert_nsize,
			   const u8 *dcp_cert_evalue, int dcp_cert_esize)
{
	return xlnx_hdcp2x_tx_rsa_signature_verify((u8 *)srm,
						  srm_size - HDCP2X_TX_SRM_SIGNATURE_SIZE,
						  srm + (srm_size - HDCP2X_TX_SRM_SIGNATURE_SIZE),
						  dcp_cert_nvalue, dcp_cert_nsize,
						  dcp_cert_evalue, dcp_cert_esize);
}

static void xlnx_hdcp2x_tx_aes128_encrypt(const u8 *data, const u8 *key, u8 *output)
{
	struct crypto_aes_ctx ctx;

	aes_expandkey(&ctx, key, HDCP2X_TX_AES128_SIZE);
	aes_encrypt(&ctx, output, data);
	memzero_explicit(&ctx, sizeof(ctx));
}

/*
 * This function implements the HMAC Hash message for Authentication.
 * Reference: http://www.citi.umich.edu/projects/nfsv4/rfc/pkcs-1v2-1.pdf
 */
static int xlnx_hdcp2x_cmn_hmac_sha256_hash(const u8 *data, int data_size, const u8 *key,
					    int key_size, u8  *hashed_data)
{
	u8 buffer_in[XHDCP2X_TX_SHA_SIZE] = {0};
	u8 buffer_out[XHDCP2X_TX_SHA_SIZE] = {0};
	u8 ktemp[XHDCP2X_TX_SHA256_SIZE] = {0};
	u8 ktemp2[XHDCP2X_TX_SHA256_SIZE] = {0};
	u8 ipad[XHDCP2X_TX_SHA_KEY_LENGTH + 1] = {0};
	u8 opad[XHDCP2X_TX_SHA_KEY_LENGTH + 1] = {0};
	int i;

	if (data_size + XHDCP2X_TX_SHA_KEY_LENGTH >  XHDCP2X_TX_SHA_SIZE)
		return -EINVAL;

	if (key_size > XHDCP2X_TX_SHA_KEY_LENGTH) {
		sha256(key, key_size, ktemp);
		key     = ktemp;
		key_size = XHDCP2X_TX_SHA256_SIZE;
	}

	memcpy(ipad, key, key_size);
	memcpy(opad, key, key_size);

	for (i = 0; i < XHDCP2X_TX_SHA_KEY_LENGTH; i++) {
		ipad[i] ^= XHDCP2X_TX_INNER_PADDING_BYTE;
		opad[i] ^= XHDCP2X_TX_OUTER_PADDING_BYTE;
	}

	memcpy(buffer_in, ipad, XHDCP2X_TX_SHA_KEY_LENGTH);
	memcpy(buffer_in + XHDCP2X_TX_SHA_KEY_LENGTH, data, data_size);
	sha256(buffer_in, XHDCP2X_TX_SHA_KEY_LENGTH + data_size, ktemp2);

	memcpy(buffer_out, opad, XHDCP2X_TX_SHA_KEY_LENGTH);
	memcpy(buffer_out + XHDCP2X_TX_SHA_KEY_LENGTH, ktemp2, XHDCP2X_TX_SHA256_SIZE);
	sha256(buffer_out, XHDCP2X_TX_SHA_KEY_LENGTH + XHDCP2X_TX_SHA256_SIZE, (u8 *)hashed_data);

	return 0;
}

void xlnx_hdcp2x_tx_compute_hprime(const u8 *r_rx, const u8 *rxcaps,
				   const u8 *r_tx, const u8 *txcaps,
				   const u8 *km, u8 *hprime)
{
	u8 kd[HDCP2X_TX_DKEY_SIZE * HDCP2X_TX_AES128_SIZE] = {0};
	u8 aes_iv[HDCP2X_TX_AES128_SIZE] = {0};
	u8 aes_key[HDCP2X_TX_AES128_SIZE] = {0};
	u8 hash_input[HDCP_2_2_RTX_LEN + HDCP_2_2_RXCAPS_LEN +
				HDCP2X_TX_TXCAPS_SIZE] = {0};
	int idx = 0;

	memcpy(aes_key, km, HDCP2X_TX_KM_SIZE);
	memcpy(aes_iv, r_tx, HDCP_2_2_RTX_LEN);
	memcpy(&aes_iv[HDCP_2_2_RTX_LEN], r_rx, HDCP_2_2_RRX_LEN);

	xlnx_hdcp2x_tx_aes128_encrypt(aes_iv, aes_key, kd);

	/* Determine dkey1, counter is 1: Rrx | 0x01. */

	/*
	 * Reference: Section 2.7.1: Key derivation
	 * https://www.digital-cp.com/sites/default/files/HDCP%20on%20DisplayPort%20Specification%20Rev2_3.pdf
	 */
	aes_iv[HDCP2X_TX_DKEY] ^= HDCP2X_TX_DKEY_CTR1;

	xlnx_hdcp2x_tx_aes128_encrypt(aes_iv, aes_key, &kd[HDCP2X_TX_KM_SIZE]);
	memcpy(hash_input, r_tx, HDCP_2_2_RTX_LEN);
	idx += HDCP_2_2_RTX_LEN;
	memcpy(&hash_input[idx], rxcaps, HDCP_2_2_RXCAPS_LEN);
	idx += HDCP_2_2_RXCAPS_LEN;
	memcpy(&hash_input[idx], txcaps, HDCP2X_TX_TXCAPS_SIZE);

	xlnx_hdcp2x_cmn_hmac_sha256_hash(hash_input, sizeof(hash_input), kd,
					 sizeof(kd), hprime);
}

void xlnx_hdcp2x_tx_compute_edkey_ks(const u8 *rn, const u8 *km, const u8 *ks,
				     const u8 *r_rx, const u8 *r_tx,
				     u8 *encrypted_ks)
{
	u8 aes_iv[HDCP2X_TX_AES128_SIZE] = {0};
	u8 aes_key[HDCP2X_TX_AES128_SIZE] = {0};
	u8 dkey2[HDCP2X_TX_AES128_SIZE] = {0};

	memcpy(&aes_key[HDCP_2_2_RN_LEN], rn, HDCP_2_2_RN_LEN);

	xlnx_hdcp2x_tx_memxor(aes_key, aes_key, km, HDCP2X_TX_KM_SIZE);

	/* Determine dkey2. */
	/* Add m = Rtx || Rrx. */
	memcpy(aes_iv, r_tx, HDCP_2_2_RTX_LEN);
	memcpy(&aes_iv[HDCP_2_2_RTX_LEN], r_rx, HDCP_2_2_RRX_LEN);

	aes_iv[HDCP2X_TX_DKEY] ^= HDCP2X_TX_DKEY_CTR2;

	xlnx_hdcp2x_tx_aes128_encrypt(aes_iv, aes_key, dkey2);

	/* EdkeyKs = Ks XOR (Dkey2 XOR Rrx). */
	/* Rrx XOR Dkey2. */
	memset(encrypted_ks, 0, HDCP_2_2_E_DKEY_KS_LEN);

	memcpy(&encrypted_ks[HDCP_2_2_E_DKEY_KS_LEN - HDCP_2_2_RRX_LEN], r_rx,
	       HDCP_2_2_RRX_LEN);

	xlnx_hdcp2x_tx_memxor(encrypted_ks, encrypted_ks, dkey2, HDCP2X_TX_AES128_SIZE);
	xlnx_hdcp2x_tx_memxor(encrypted_ks, encrypted_ks, ks, HDCP2X_TX_KS_SIZE);
}

void xlnx_hdcp2x_tx_compute_lprime(const u8 *rn, const u8 *km,
				   const u8 *r_rx, const u8 *r_tx,
				   u8 *lprime)
{
	u8 hash_key[HDCP2X_TX_SHA256_HASH_SIZE] = {0};
	u8 aes_iv[HDCP2X_TX_AES128_SIZE] = {0};
	u8 aes_key[HDCP2X_TX_AES128_SIZE] = {0};
	u8 kd[HDCP2X_TX_DKEY_SIZE * HDCP2X_TX_AES128_SIZE] = {0};

	memcpy(aes_key, km, HDCP2X_TX_KM_SIZE);
	memcpy(aes_iv, r_tx, HDCP_2_2_RTX_LEN);
	memcpy(&aes_iv[HDCP_2_2_RTX_LEN], r_rx, HDCP_2_2_RRX_LEN);

	/* Compute Dkey0. */
	/* Add m = Rtx || Rrx. */
	xlnx_hdcp2x_tx_aes128_encrypt(aes_iv, aes_key, kd);

	/* Compute Dkey , counter is 1: Rrx | 0x01. */
	aes_iv[HDCP2X_TX_DKEY] ^= HDCP2X_TX_DKEY_CTR1;
	xlnx_hdcp2x_tx_aes128_encrypt(aes_iv, aes_key, &kd[HDCP2X_TX_KM_SIZE]);

	/* Create hash with HMAC-SHA256. */
	/* Key:	Kd XOR Rrx (least sign. 64 bits). */
	memcpy(&hash_key[HDCP2X_TX_SHA256_HASH_SIZE - HDCP_2_2_RRX_LEN], r_rx,
	       HDCP_2_2_RRX_LEN);

	xlnx_hdcp2x_tx_memxor(hash_key, hash_key, kd, HDCP2X_TX_SHA256_HASH_SIZE);
	xlnx_hdcp2x_cmn_hmac_sha256_hash(rn, HDCP_2_2_RN_LEN, hash_key,
					 HDCP2X_TX_SHA256_HASH_SIZE, lprime);
}

void xlnx_hdcp2x_tx_compute_v(const u8 *rn, const u8 *r_rx, const u8 *rx_info,
			      const u8 *r_tx, const u8 *rcvid_list, const u8 rcvid_count,
			      const u8 *seq_num_v, const u8 *km, u8 *hash_v)
{
	u8 kd[HDCP2X_TX_DKEY_SIZE * HDCP2X_TX_AES128_SIZE] = {0};
	u8 aes_iv[HDCP2X_TX_AES128_SIZE] = {0};
	u8 aes_key[HDCP2X_TX_AES128_SIZE] = {0};
	u8 hash_input[(HDCP_2_2_MAX_DEVICE_COUNT * HDCP_2_2_RECEIVER_ID_LEN) +
				  HDCP_2_2_RXINFO_LEN + HDCP_2_2_SEQ_NUM_LEN];
	int idx = 0;

	memcpy(aes_key, km, HDCP2X_TX_KM_SIZE);

	/* Determine Dkey. */
	/* Add m = Rtx || Rrx. */
	memcpy(aes_iv, r_tx, HDCP_2_2_RTX_LEN);
	memcpy(&aes_iv[HDCP_2_2_RTX_LEN], r_rx, HDCP_2_2_RRX_LEN);

	xlnx_hdcp2x_tx_aes128_encrypt(aes_iv, aes_key, kd);

	aes_iv[HDCP2X_TX_DKEY] ^= HDCP2X_TX_DKEY_CTR1;
	xlnx_hdcp2x_tx_aes128_encrypt(aes_iv, aes_key, &kd[HDCP2X_TX_KM_SIZE]);

	/* Create hash with HMAC-SHA256. */
	/* Input: ReceiverID list || RxInfo || seq_num_V. */
	memcpy(hash_input, rcvid_list, (rcvid_count * HDCP_2_2_RECEIVER_ID_LEN));
	idx += (rcvid_count * HDCP_2_2_RECEIVER_ID_LEN);
	memcpy(&hash_input[idx], rx_info, HDCP_2_2_RXINFO_LEN);
	idx += HDCP_2_2_RXINFO_LEN;
	memcpy(&hash_input[idx], seq_num_v, HDCP_2_2_SEQ_NUM_LEN);
	idx += HDCP_2_2_SEQ_NUM_LEN;

	xlnx_hdcp2x_cmn_hmac_sha256_hash(hash_input, idx, kd, sizeof(kd), hash_v);
}

void xlnx_hdcp2x_tx_compute_m(const u8 *rn, const u8 *r_rx, const u8 *r_tx,
			      const u8 *stream_id_type, const u8 *k,
			      const u8 *seq_num_m, const u8 *km, u8 *m_hash)
{
	u8 aes_iv[HDCP2X_TX_AES128_SIZE];
	u8 aes_key[HDCP2X_TX_AES128_SIZE];
	u8 kd[HDCP2X_TX_DKEY_SIZE * HDCP2X_TX_AES128_SIZE];
	u8 sha256_kd[HDCP2X_TX_SHA256_HASH_SIZE];
	u8 hash_input[(HDCP_2_2_MAX_DEVICE_COUNT * HDCP_2_2_RECEIVER_ID_LEN) +
				  HDCP_2_2_RXINFO_LEN + HDCP_2_2_SEQ_NUM_LEN];
	u16 stream_id_count;
	int idx = 0;

	stream_id_count  = k[0] << BITS_PER_BYTE;
	stream_id_count |= k[1];

	memcpy(aes_key, km, HDCP2X_TX_KM_SIZE);
	memcpy(aes_iv, r_tx, HDCP_2_2_RTX_LEN);
	memcpy(&aes_iv[HDCP_2_2_RTX_LEN], r_rx, HDCP_2_2_RRX_LEN);

	xlnx_hdcp2x_tx_aes128_encrypt(aes_iv, aes_key, kd);

	/* Determine Dkey0. */
	/* Add m = Rtx || Rrx. */
	aes_iv[HDCP2X_TX_DKEY] ^= HDCP2X_TX_DKEY_CTR1;

	xlnx_hdcp2x_tx_aes128_encrypt(aes_iv, aes_key, &kd[HDCP2X_TX_KM_SIZE]);

	sha256(kd, sizeof(kd), sha256_kd);

	/* Create hash with HMAC-SHA256. */
	/* Input: StreamID_Type list || seq_num_M. */
	memcpy(hash_input, stream_id_type, (stream_id_count * HDCP2X_TX_STREAMID_TYPE_SIZE));
	idx += (stream_id_count * HDCP2X_TX_STREAMID_TYPE_SIZE);
	memcpy(&hash_input[idx], seq_num_m, HDCP_2_2_SEQ_NUM_LEN);
	idx += HDCP_2_2_SEQ_NUM_LEN;

	/* HashKey:	SHA256(Kd) */
	xlnx_hdcp2x_cmn_hmac_sha256_hash(hash_input, idx, sha256_kd, sizeof(sha256_kd), m_hash);
}

int xlnx_hdcp2x_tx_encryptedkm(const struct hdcp2x_tx_cert_rx *rx_certificate,
			       const u8 *km_ptr, u8 *masking_seed, u8 *encrypted_km)
{
	return xlnx_hdcp2x_tx_rsa_oae_encrypt(rx_certificate->N, HDCP2X_TX_CERT_PUB_KEY_N_SIZE,
					      rx_certificate->e, HDCP2X_TX_CERT_PUB_KEY_E_SIZE,
					      km_ptr, HDCP2X_TX_KM_SIZE,
					      masking_seed, encrypted_km);
}
