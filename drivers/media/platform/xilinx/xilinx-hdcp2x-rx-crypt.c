// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDCP2X Cryptography driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Kunal Vasant Rane <kunal.rane@amd.com>
 *
 * This driver provides Xilinx HDCP 2X receiver cryptographic
 * functionality.
 *
 * References:
 *
 * http://www.citi.umich.edu/projects/nfsv4/rfc/pkcs-1v2-1.pdf
 * https://www.cryptrec.go.jp/cryptrec_03_spec_cypherlist_files/PDF/pkcs-1v2-12.pdf
 * https://www.digital-cp.com/sites/default/files/HDCP%20on%20DisplayPort%20Specification%20Rev2_3.pdf
 */

#include <crypto/aes.h>
#include <crypto/sha2.h>
#include <linux/delay.h>
#include <linux/xlnx/xlnx_hdcp_common.h>
#include <linux/xlnx/xlnx_hdcp_rng.h>
#include <linux/xlnx/xlnx_hdcp2x_cipher.h>
#include <linux/xlnx/xlnx_timer.h>
#include "xilinx-hdcp2x-rx.h"

#define XHDCP2X_SHA256_SIZE		(256 / 8)
#define XHDCP2X_RX_MP_SIZE_OF(a) (sizeof(a) / sizeof(u32))
#define XHDCP2X_RX_SHA_SIZE		256
#define XHDCP2X_RX_SHA_KEY_LENGTH	64
#define XHDCP2X_RX_INNER_PADDING_BYTE	0x36
#define XHDCP2X_RX_OUTER_PADDING_BYTE	0x5C
#define XHDCP2X_NDIGITS			16
#define XHDCP2X_NDIGITS_MULT		4

/**
 * xlnx_hdcp2x_rx_aes128_encrypt() - encrypts 16 bytes data with key of size 16 bytes
 * @data: Input is the 16 byte plaintext
 * @key: User supplied key
 * @output: 16 byte cipher text
 */
static void xlnx_hdcp2x_rx_aes128_encrypt(const u8 *data, const u8 *key, u8 *output)
{
	struct crypto_aes_ctx ctx;

	aes_expandkey(&ctx, key, 16);
	aes_encrypt(&ctx, output, data);
	memzero_explicit(&ctx, sizeof(ctx));
}

/**
 * xhdcp2x_rx_calc_mont_nprime() - calculate Montgomery NPrime
 * @ref: void pointer for passing dummy structure pointer.
 * @nprime: pointer for nprimep.
 * @n: pointer for privatekey.
 * @ndigits: pointer for key size.
 *
 * The modulus N has a fixed size of k = 512bits and give k,
 * r = 2^(k), and rinv is the modular inverse of r.
 * reference:
 * Analyzing and Comparing Montgomery Multiplication Algorithms
 * IEEE Micro, 16(3):26-33,June 1996
 * By: Cetin Koc, Tolga Acar, and Burton Kaliski
 *
 * Return: 0 on success, otherwise EINVAL.
 */
int xhdcp2x_rx_calc_mont_nprime(void *ref, u8 *nprime, const u8 *n, int ndigits)
{
	u32 *n_i, *nprime_i, *r, *rinv, *t1, *t2;
	int ret = 0;

	struct xlnx_hdcp2x_config *xhdcp2x_rx = (struct xlnx_hdcp2x_config *)ref;

	n_i = kzalloc(4 * XHDCP2X_RX_HASH_SIZE * sizeof(u32), GFP_KERNEL);
	if (!n_i)
		return -ENOMEM;

	nprime_i = &n_i[XHDCP2X_RX_HASH_SIZE];
	r = &n_i[2 * XHDCP2X_RX_HASH_SIZE];
	rinv = &n_i[3 * XHDCP2X_RX_HASH_SIZE];

	t1 = kzalloc(sizeof(u32) * XHDCP2X_RX_P_SIZE, GFP_KERNEL);
	if (!t1) {
		kfree(n_i);
		return -ENOMEM;
	}

	t2 = kzalloc(sizeof(u32) * XHDCP2X_RX_N_SIZE, GFP_KERNEL);
	if (!t2) {
		kfree(t1);
		kfree(n_i);
		return -ENOMEM;
	}

	mp_conv_from_octets(n_i, XHDCP2X_RX_HASH_SIZE, n, XHDCP2X_NDIGITS_MULT * ndigits);

	/* Step 1: R = 2^(NDigits*32) */
	r[0] = 1;
	mp_shift_left(r, r, 32 * ndigits, XHDCP2X_RX_HASH_SIZE);

	/* Step 2: rinv = r^(-1) * mod(N) */
	t1[0] = 0;
	memcpy(t1, n_i, XHDCP2X_RX_HASH_SIZE * sizeof(u32));

	if (mp_mod_inv(rinv, r, t1, XHDCP2X_RX_HASH_SIZE)) {
		dev_err(xhdcp2x_rx->dev, "Error: Failed rinv calculation");
		ret = -EINVAL;
		goto free_buf;
	}

	/* Step 3: NPrime = (r*rinv-1)/N */
	mp_multiply(t1, r, rinv, 2 * ndigits);
	t2[0] = 1;
	mp_subtract(t1, t1, t2, XHDCP2X_RX_P_SIZE);
	mp_divide(nprime_i, t2, t1, XHDCP2X_RX_HASH_SIZE, (u32 *)n_i, ndigits);

	/* Step 4: Sanity Check, R*Rinv - N*NPrime == 1 */
	mp_multiply(t1, r, rinv, 2 * ndigits);
	mp_multiply(t2, (u32 *)n_i, nprime_i, XHDCP2X_RX_HASH_SIZE);
	mp_subtract(t1, t1, t2, XHDCP2X_RX_P_SIZE);
	memset(t2, 0, XHDCP2X_RX_N_SIZE * sizeof(u32));

	t2[0] = 1;
	if (!mp_equal(t1, t2, XHDCP2X_RX_P_SIZE)) {
		dev_err(xhdcp2x_rx->dev, "Error: Failed NPrime calculation");
		ret = -EINVAL;
		goto free_buf;
	}

	mp_conv_to_octets(nprime_i, ndigits, nprime, XHDCP2X_NDIGITS_MULT * ndigits);

free_buf:	kfree(t2);
		kfree(t1);
		kfree(n_i);

	return ret;
}

static void xhdcp2x_rx_xor(u8 *cout, const u8 *ain, const u8 *bin, u32 len)
{
	while (len--)
		cout[len] = ain[len] ^ bin[len];
}

/**
 * xhdcp2x_rx_pkcs1_mgf1()- calculate mgf1.
 * @seed: pointer for input seed.
 * @seedlen: input seedlen.
 * @mask: input mask.
 * @masklen: input masklen.
 *
 * Reference:
 * https://www.cryptrec.go.jp/cryptrec_03_spec_cypherlist_files
 * /PDF/pkcs-1v2-12.pdf
 * Section: B.2.1
 */
static void xhdcp2x_rx_pkcs1_mgf1(const u8 *seed, const u32 seedlen, u8 *mask, u32 masklen)
{
	u8 hash[XHDCP2X_RX_HASH_SIZE] = {0};
	u8 hash_data[XHDCP2X_RX_N_SIZE] = {0};
	u8 t[XHDCP2X_RX_N_SIZE] = {0};
	u32 c;

	memcpy(hash_data, seed, seedlen);

	for (c = 0; (c * XHDCP2X_RX_HASH_SIZE) < masklen; c++) {
		u32 counter;

		counter = ntohl(c);
		memcpy(hash_data + seedlen, &counter, XHDCP2X_NDIGITS_MULT);
		sha256(hash_data, seedlen + XHDCP2X_NDIGITS_MULT, hash);
		memcpy(t + c * XHDCP2X_RX_HASH_SIZE, hash, XHDCP2X_RX_HASH_SIZE);
	}
	memcpy(mask, t, masklen);
}

/**
 * xhdcp2x_rx_pkcs1_mont_mult_fios_init()- Initialize Montgomery core
 * functions.
 * @xhdcp2x_rx: structure pointer for HDCP2x.
 * @n: pointer for exponentiation input.
 * @nprime: pointer for nprime number.
 * @ndigits: pointer for ndigits.
 *
 * Reference:
 * Analyzing and Comparing Montgomery Multiplication Algorithms
 * IEEE Micro, 16(3):26-33,June 1996
 * By: Cetin Koc, Tolga Acar, and Burton Kaliski
 *
 * Return: 0 on success, otherwise ETIME
 */
static int xhdcp2x_rx_pkcs1_mont_mult_fios_init(struct xlnx_hdcp2x_config *xhdcp2x_rx,
						u32 *n, const u32 *nprime, int ndigits)
{
	u32 timeout = 1000; /* us */

	while (xlnx_hdcp2x_mmult_is_ready(&xhdcp2x_rx->xhdcp2x_hw.mmult_inst) == 0) {
		if (timeout == 0) {
			dev_err(xhdcp2x_rx->dev, "Error: MMULT core is not ready");
			return -ETIME;
		}

		timeout--;
		udelay(1);
	}

	xlnx_hdcp2x_mmult_write_type(&xhdcp2x_rx->xhdcp2x_hw.mmult_inst, 0, (int *)n,
				     ndigits, XHDCP2X_MMULT_N);
	xlnx_hdcp2x_mmult_write_type(&xhdcp2x_rx->xhdcp2x_hw.mmult_inst, 0, (int *)nprime,
				     ndigits, XHDCP2X_MMULT_NPRIME);

	return 0;
}

/**
 * xhdcp2x_rx_pkcs1_mont_mult_fios()- calculate pkcs1 mont mult fios.
 * @xhdcp2x_rx: HDCP structure pointer.
 * @u: pointer for modulas multiplier.
 * @a: pointer for modulas multiplier.
 * @b: pointer for modulas multiplier.
 * @ndigits: input for mmult register write.
 *
 * This function offers Modular multiplication operation required
 * by RSA decryption.
 *
 * Return: 0 on success, otherwise return ETIME.
 */
static int xhdcp2x_rx_pkcs1_mont_mult_fios(struct xlnx_hdcp2x_config *xhdcp2x_rx, u32 *u,
					   u32 *a, u32 *b, int ndigits)
{
	u32 timeout = 1000; /* us */

	while (xlnx_hdcp2x_mmult_is_ready(&xhdcp2x_rx->xhdcp2x_hw.mmult_inst) == 0) {
		if (timeout == 0) {
			dev_err(xhdcp2x_rx->dev, "Error: MMULT core is not ready");
			return -ETIME;
		}

		timeout--;
		udelay(1);
	}

	xlnx_hdcp2x_mmult_write_type(&xhdcp2x_rx->xhdcp2x_hw.mmult_inst, 0, (int *)a,
				     ndigits, XHDCP2X_MMULT_A);
	xlnx_hdcp2x_mmult_write_type(&xhdcp2x_rx->xhdcp2x_hw.mmult_inst, 0, (int *)b,
				     ndigits, XHDCP2X_MMULT_B);
	xlnx_hdcp2x_mmult_enable(&xhdcp2x_rx->xhdcp2x_hw.mmult_inst);

	while (xlnx_hdcp2x_mmult_is_done(&xhdcp2x_rx->xhdcp2x_hw.mmult_inst) == 0) {
		if (timeout == 0) {
			dev_err(xhdcp2x_rx->dev, "Error: MMULT core is not done");
			return -ETIME;
		}

		timeout--;
		udelay(1);
	}

	xlnx_hdcp2x_mmult_read_u_words(&xhdcp2x_rx->xhdcp2x_hw.mmult_inst, 0, (int *)u, ndigits);

	return 0;
}

/*
 * Modular exponentiation operation using the
 * binary square and multiply method.
 */
static int xhdcp2x_rx_pkcs1_mont_exp(struct xlnx_hdcp2x_config *xhdcp2x_rx, u32 *c, u32 *a,
				     u32 *e, u32 *n, const u32 *nprime, int ndigits)
{
	int offset;
	u32 r[XHDCP2X_RX_HASH_SIZE] = {0};
	u32 abar[XHDCP2X_RX_HASH_SIZE] = {0};
	u32 xbar[XHDCP2X_RX_HASH_SIZE] = {0};

	xhdcp2x_rx_pkcs1_mont_mult_fios_init(xhdcp2x_rx, n, nprime, ndigits);

	r[0] = 1;
	mp_shift_left(r, r, ndigits * XHDCP2X_RX_HASH_SIZE, XHDCP2X_RX_HASH_SIZE);
	mp_modulo(xbar, r, XHDCP2X_RX_HASH_SIZE, n, ndigits);
	mp_mod_mult(abar, a, xbar, n, 2 * ndigits);

	for (offset = 32 * ndigits - 1; offset >= 0; offset--) {
		xhdcp2x_rx_pkcs1_mont_mult_fios(xhdcp2x_rx, xbar, xbar, xbar, ndigits);
		if (mp_get_bit(e, ndigits, offset) == 1)
			xhdcp2x_rx_pkcs1_mont_mult_fios(xhdcp2x_rx, xbar,
							xbar, abar, ndigits);
	}

	memset(r, 0, sizeof(r));
	r[0] = 1;

	xhdcp2x_rx_pkcs1_mont_mult_fios(xhdcp2x_rx, c, xbar, r, ndigits);

	return 0;
}

/**
 * xhdcp2x_rx_pkcs1_rsa_dp()- to calculate rsa.
 * @xhdcp2x_rx: structure pointer for HDCP.
 * @kpriv_rx: structure pointer for krpiv.
 * @encrypted_message: pointer for encrypter_message.
 * @message: pointer for encrypted message.
 *
 * RSADP implemented using the chinese remainder theorem
 * reference: PKCS#1 v2.1, section 5.1.2
 *
 * Return: 0 on success, otherwise integer value.
 */
static int xhdcp2x_rx_pkcs1_rsa_dp(struct xlnx_hdcp2x_config *xhdcp2x_rx,
				   const struct xhdcp2x_rx_kpriv_rx *kpriv_rx,
				   u8 *encrypted_message,
				   u8 *message)
{
	u32 a[XHDCP2X_RX_HASH_SIZE] = {0};
	u32 b[XHDCP2X_RX_HASH_SIZE] = {0};
	u32 c[XHDCP2X_RX_HASH_SIZE] = {0};
	u32 d[XHDCP2X_RX_HASH_SIZE] = {0};
	u32 m1[XHDCP2X_RX_HASH_SIZE] = {0};
	u32 m2[XHDCP2X_RX_HASH_SIZE] = {0};
	u32 status = 0;

	mp_conv_from_octets(a, XHDCP2X_RX_MP_SIZE_OF(a), kpriv_rx->p, XHDCP2X_RX_P_SIZE);
	mp_conv_from_octets(b, XHDCP2X_RX_MP_SIZE_OF(b), kpriv_rx->dp, XHDCP2X_RX_P_SIZE);
	mp_conv_from_octets(c, XHDCP2X_RX_MP_SIZE_OF(c), encrypted_message, XHDCP2X_RX_N_SIZE);
	mp_conv_from_octets(d, XHDCP2X_RX_MP_SIZE_OF(d), xhdcp2x_rx->nprimep, XHDCP2X_RX_P_SIZE);
	status = xhdcp2x_rx_pkcs1_mont_exp(xhdcp2x_rx, m1, c, b, a, d, XHDCP2X_NDIGITS);

	mp_conv_from_octets(a, XHDCP2X_RX_MP_SIZE_OF(a), kpriv_rx->q, XHDCP2X_RX_P_SIZE);
	mp_conv_from_octets(b, XHDCP2X_RX_MP_SIZE_OF(b), kpriv_rx->dq, XHDCP2X_RX_P_SIZE);
	mp_conv_from_octets(d, XHDCP2X_RX_MP_SIZE_OF(d), xhdcp2x_rx->nprimeq, XHDCP2X_RX_P_SIZE);
	status = xhdcp2x_rx_pkcs1_mont_exp(xhdcp2x_rx, m2, c, b, a, d, XHDCP2X_NDIGITS);
	mp_conv_from_octets(a, XHDCP2X_RX_MP_SIZE_OF(a), kpriv_rx->p, XHDCP2X_RX_P_SIZE);
	status = mp_subtract(d, m1, m2, XHDCP2X_RX_MP_SIZE_OF(d));
	if (status) {
		mp_add(m1, m1, a, XHDCP2X_RX_MP_SIZE_OF(m1));
		mp_subtract(d, m1, m2, XHDCP2X_RX_MP_SIZE_OF(d));
	}
	mp_conv_from_octets(c, XHDCP2X_RX_MP_SIZE_OF(c), kpriv_rx->qinv, XHDCP2X_RX_P_SIZE);
	status = mp_mod_mult(c, d, c, a, XHDCP2X_RX_HASH_SIZE);

	mp_conv_from_octets(a, XHDCP2X_RX_MP_SIZE_OF(a), kpriv_rx->q, XHDCP2X_RX_P_SIZE);
	status = mp_multiply(d, a, c, XHDCP2X_RX_P_SIZE / XHDCP2X_NDIGITS_MULT);
	status = mp_add(c, m2, d, XHDCP2X_RX_HASH_SIZE);

	mp_conv_to_octets(c, XHDCP2X_RX_MP_SIZE_OF(c), message, XHDCP2X_RX_N_SIZE);

	return status;
}

/**
 * xhdcp2x_rx_pkcs1_eme_oaep_decode()- function for oaep decode.
 * @encoded_message: pointer for encoded message.
 * @message: pointer for messagge.
 * @message_len: pointer for message len.
 *
 * Eme oaep decoding. the label l is the empty stringg and underlying hash function is
 * SHA256.
 * reference: PKCS#1 v2.1 Section 7.1.2, part 3.
 *
 * Return: 0 on success, otherwise EINVAL.
 */
static int xhdcp2x_rx_pkcs1_eme_oaep_decode(u8 *encoded_message, u8 *message, int *message_len)
{
	u8 *masked_seed = NULL;
	u8 l_hash[XHDCP2X_RX_HASH_SIZE] = {0};
	u8 seed[XHDCP2X_RX_HASH_SIZE] = {0};
	u8 *masked_db = NULL;
	u8 db[XHDCP2X_RX_N_SIZE - XHDCP2X_RX_HASH_SIZE - 1] = {0};
	u32 offset = XHDCP2X_RX_HASH_SIZE;
	u32 status = 0;

	/* l_hash is the empty string */
	sha256(NULL, 0, l_hash);

	/* Separate EM = Y || maskedSeed || maskedDB */
	masked_seed = encoded_message + 1;
	masked_db = encoded_message + 1 + XHDCP2X_RX_HASH_SIZE;

	/* Generate seedMask = MGF(maskedDB, hLen) */
	xhdcp2x_rx_pkcs1_mgf1(masked_db, XHDCP2X_RX_N_SIZE - XHDCP2X_RX_HASH_SIZE - 1,
			      seed, XHDCP2X_RX_HASH_SIZE);

	/* Generate seed = maskedSeed xor seedMask */
	xhdcp2x_rx_xor(seed, masked_seed, seed, XHDCP2X_RX_HASH_SIZE);

	/* Generate dbMask = MGF(seed, k-hLen-1) */
	xhdcp2x_rx_pkcs1_mgf1(seed, XHDCP2X_RX_HASH_SIZE, db,
			      XHDCP2X_RX_N_SIZE - XHDCP2X_RX_HASH_SIZE - 1);

	/* Generate DB = maskedDB xor dbMask */
	xhdcp2x_rx_xor(db, masked_db, db, XHDCP2X_RX_N_SIZE - XHDCP2X_RX_HASH_SIZE - 1);

	if (*encoded_message)
		status = 1;

	if (memcmp(db, l_hash, XHDCP2X_RX_HASH_SIZE))
		status = 1;

	for (offset = XHDCP2X_RX_HASH_SIZE;
		offset < (XHDCP2X_RX_N_SIZE - XHDCP2X_RX_HASH_SIZE - 1); offset++) {
		if (db[offset] == 0x01)
			break;
		else if (db[offset])
			status = 1;
	}

	if (status)
		return -EINVAL;

	*message_len = (XHDCP2X_RX_N_SIZE - XHDCP2X_RX_HASH_SIZE - 1) - (offset + 1);

	memcpy(message, db + offset + 1, *message_len);

	return 0;
}

/**
 * xhdcp2x_rx_rsaes_oaep_decrypt()- function for oaep decrypt.
 * @xhdcp2x_rx: structure pointer for HDCP.
 * @kpriv_rx: structure pointer for kpriv_rx
 * @encrypted_message: pointer for encrypted message.
 * @message: pointer for message.
 * @message_len: pointer for message length.
 *
 * RSAES-OAEP decrypt operation, decrypted using RSADP
 * and decode using EME-OAEP.
 *
 * Return: 0 on success, otherwise EINVAL.
 */
int  xhdcp2x_rx_rsaes_oaep_decrypt(struct xlnx_hdcp2x_config *xhdcp2x_rx,
				   struct xhdcp2x_rx_kpriv_rx *kpriv_rx,
				   u8 *encrypted_message, u8 *message, int *message_len)
{
	u8 em[XHDCP2X_RX_N_SIZE] = {0};
	u32 status;

	status = xhdcp2x_rx_pkcs1_rsa_dp(xhdcp2x_rx, kpriv_rx, encrypted_message,
					 em);
	if (status)
		return -EINVAL;

	status = xhdcp2x_rx_pkcs1_eme_oaep_decode(em, message, message_len);
	if (status)
		return -EINVAL;

	return 0;
}

/*
 * Computes the derived keys used during the HDCP2.2 authentication and
 * key exchange.
 * reference: HDCP2.2 section 2.7
 */
static void xhdcp2x_rx_compute_dkey(const u8 *rrx, const u8 *rtx, const u8 *km,
				    const u8 *rn, u8 *ctr, u8 *dkey)
{
	u8 aes_iv[XHDCP2X_RX_AES_SIZE] = {0};
	u8 aes_key[XHDCP2X_RX_AES_SIZE] = {0};

	memcpy(aes_key, km, XHDCP2X_RX_AES_SIZE);

	if (rn)
		xhdcp2x_rx_xor(aes_key + XHDCP2X_RX_RN_SIZE, km + XHDCP2X_RX_RN_SIZE,
			       rn, XHDCP2X_RX_RN_SIZE);

	memcpy(aes_iv, rtx, XHDCP2X_RX_RTX_SIZE);

	if (!ctr)
		memcpy(&aes_iv[XHDCP2X_RX_RRX_SIZE], rrx, XHDCP2X_RX_RRX_SIZE);
	else
		xhdcp2x_rx_xor(aes_iv + XHDCP2X_RX_RTX_SIZE, rrx, ctr,
			       XHDCP2X_RX_RRX_SIZE);

	xlnx_hdcp2x_rx_aes128_encrypt(aes_iv, aes_key, dkey);
}

/**
 * xlnx_hdcp2x_cmn_hmac_sha256_hash()- transform to HMAC sha256.
 * @data: pointer for input data.
 * @data_size: input data size.
 * @key: pointer for key.
 * @key_size: input key size.
 * @hashed_data: pointer for hashed data.
 *
 * HMAC_SHA256 transform using SHA256 function
 *
 * Return: 0 on success, otherwise EINVAL.
 */

static int xlnx_hdcp2x_cmn_hmac_sha256_hash(const u8 *data, int data_size, const u8 *key,
					    int key_size, u8  *hashed_data)
{
	u8 buffer_in[XHDCP2X_RX_SHA_SIZE] = {0};
	u8 buffer_out[XHDCP2X_RX_SHA_SIZE] = {0};
	u8 ktemp[XHDCP2X_SHA256_SIZE] = {0};
	u8 ktemp2[XHDCP2X_SHA256_SIZE] = {0};
	u8 ipad[XHDCP2X_RX_SHA_KEY_LENGTH + 1] = {0};
	u8 opad[XHDCP2X_RX_SHA_KEY_LENGTH + 1] = {0};
	int i;

	if (data_size + XHDCP2X_RX_SHA_KEY_LENGTH > XHDCP2X_RX_SHA_SIZE)
		return -EINVAL;

	if (key_size > XHDCP2X_RX_SHA_KEY_LENGTH) {
		sha256(key, key_size, ktemp);
		key = ktemp;
		key_size = XHDCP2X_SHA256_SIZE;
	}

	memcpy(ipad, key, key_size);
	memcpy(opad, key, key_size);

	for (i = 0; i < XHDCP2X_RX_SHA_KEY_LENGTH; i++) {
		ipad[i] ^= XHDCP2X_RX_INNER_PADDING_BYTE;
		opad[i] ^= XHDCP2X_RX_OUTER_PADDING_BYTE;
	}

	memcpy(buffer_in, ipad, XHDCP2X_RX_SHA_KEY_LENGTH);
	memcpy(buffer_in + XHDCP2X_RX_SHA_KEY_LENGTH, data, data_size);
	sha256(buffer_in, XHDCP2X_RX_SHA_KEY_LENGTH + data_size, ktemp2);

	memcpy(buffer_out, opad, XHDCP2X_RX_SHA_KEY_LENGTH);
	memcpy(buffer_out + XHDCP2X_RX_SHA_KEY_LENGTH, ktemp2, XHDCP2X_SHA256_SIZE);
	sha256(buffer_out, XHDCP2X_RX_SHA_KEY_LENGTH + XHDCP2X_SHA256_SIZE, (u8 *)hashed_data);

	return 0;
}

/**
 * xhdcp2x_rx_compute_hprime() - compute hprime.
 * @rrx: pointer for rrx.
 * @rxcaps: pointer for rxcaps.
 * @rtx: pointer for rtx.
 * @txcaps: pointer for txcaps.dd
 * @km: pointer for km.
 * @hprime: pointer for hprime.
 *
 * Computes hprime used during HDCP2.2 authentication and key exchange.
 * reference: HDCP v2.2, section 2.2.
 */
void xhdcp2x_rx_compute_hprime(const u8 *rrx, const u8 *rxcaps, const u8 *rtx
		, const u8 *txcaps, const u8 *km, u8 *hprime)
{
	u8 hash_input[XHDCP2X_RX_RTX_SIZE + XHDCP2X_RX_RXCAPS_SIZE + XHDCP2X_RX_TXCAPS_SIZE];
	int idx = 0;
	u8 ctr[] = {[0 ... 6] = 0x00, [7] = 0x01};
	u8 kd[2 * XHDCP2X_RX_AES_SIZE];

	xhdcp2x_rx_compute_dkey(rrx, rtx, km, NULL, NULL, kd);
	xhdcp2x_rx_compute_dkey(rrx, rtx, km, NULL, ctr, kd + XHDCP2X_RX_AES_SIZE);

	memcpy(hash_input, rtx, XHDCP2X_RX_RTX_SIZE);
	idx += XHDCP2X_RX_RTX_SIZE;
	memcpy(&hash_input[idx], rxcaps, XHDCP2X_RX_RXCAPS_SIZE);
	idx += XHDCP2X_RX_RXCAPS_SIZE;
	memcpy(&hash_input[idx], txcaps, XHDCP2X_RX_TXCAPS_SIZE);

	xlnx_hdcp2x_cmn_hmac_sha256_hash(hash_input
			, sizeof(hash_input), kd, XHDCP2X_RX_KD_SIZE, hprime);
}

/**
 * xhdcp2x_rx_compute_ekh()- compute ekh.
 * @kpriv_rx: pointer for kpriv_rx.
 * @km: pointer for km.
 * @m: pointer for m.
 * @ekh: pointer for ekh.
 *
 * Computes ekh used during HDCP2.2 authentication and key exchanges.
 * reference: HDCP v2.2, section 2.2.1
 */
void xhdcp2x_rx_compute_ekh(const u8 *kpriv_rx, const u8 *km, const u8 *m, u8 *ekh)
{
	u8 kh[XHDCP2X_RX_HASH_SIZE];

	sha256(kpriv_rx, sizeof(struct xhdcp2x_rx_kpriv_rx), kh);

	xlnx_hdcp2x_rx_aes128_encrypt(m, kh + XHDCP2X_RX_EKH_SIZE, ekh);
	xhdcp2x_rx_xor(ekh, ekh, km, XHDCP2X_RX_EKH_SIZE);
}

/**
 * xhdcp2x_rx_compute_lprime()- compute lprime.
 * @rn: pointer for rn.
 * @km: pointer for km.
 * @rrx: pointer for rrx.
 * @rtx: pointer rtx.
 * @lprime: pointer lprime.
 *
 * Computes lprime used during locality check.
 * reference: HDCPv2.2, section 2.3
 */
void xhdcp2x_rx_compute_lprime(const u8 *rn, const u8 *km, const u8 *rrx,
			       const u8 *rtx, u8 *lprime)
{
	u8 hash_key[XHDCP2X_RX_KD_SIZE] = {0};
	u8 ctr[] = {[0 ... 6] = 0x00, [7] = 0x01};
	u8 kd[2 * XHDCP2X_RX_AES_SIZE] = {0};

	xhdcp2x_rx_compute_dkey(rrx, rtx, km, NULL, NULL, kd);
	xhdcp2x_rx_compute_dkey(rrx, rtx, km, NULL, ctr, kd + XHDCP2X_RX_AES_SIZE);

	memcpy(hash_key, kd, XHDCP2X_RX_KD_SIZE);

	xhdcp2x_rx_xor(hash_key + (XHDCP2X_RX_KD_SIZE - XHDCP2X_RX_RRX_SIZE),
		       kd + (XHDCP2X_RX_KD_SIZE - XHDCP2X_RX_RRX_SIZE),
		       rrx, XHDCP2X_RX_RRX_SIZE);

	xlnx_hdcp2x_cmn_hmac_sha256_hash(rn, XHDCP2X_RX_RN_SIZE,
					 hash_key, XHDCP2X_RX_KD_SIZE, lprime);
}

/**
 * xhdcp2x_rx_compute_ks()- compute ks.
 * @rrx: pointer for rrx.
 * @rtx: pointer for rtx.
 * @km: pointer for km.
 * @rn: pointer rn.
 * @eks: pointer eks.
 * @ks: pointer for ks.
 *
 * Computes Ks used during session key exchange
 * reference: HDCP v2.2, section 2.4
 */
void xhdcp2x_rx_compute_ks(const u8 *rrx, const u8 *rtx, const u8 *km, const u8 *rn,
			   const u8 *eks, u8 *ks)
{
	u8 dkey2[XHDCP2X_RX_KS_SIZE] = {0};
	u8 ctr[] = {[0 ... 6] = 0x00, [7] = 0x02};

	xhdcp2x_rx_compute_dkey(rrx, rtx, km, rn, ctr, dkey2);
	memcpy(ks, dkey2, XHDCP2X_RX_KS_SIZE);
	xhdcp2x_rx_xor(ks + XHDCP2X_RX_RRX_SIZE, ks + XHDCP2X_RX_RRX_SIZE,
		       rrx, XHDCP2X_RX_RRX_SIZE);
	xhdcp2x_rx_xor(ks, ks, eks, XHDCP2X_RX_KS_SIZE);
}

void xhdcp2x_rx_generate_random(struct xlnx_hdcp2x_config *xhdcp2x_rx,
				int num_octets, u8 *random_number_ptr)
{
	xlnx_hdcp2x_rng_get_random_number(&xhdcp2x_rx->xhdcp2x_hw.rng_inst,
					  random_number_ptr, num_octets, num_octets);
}
