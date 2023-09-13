/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  xlnx_hdcp_sha1.h
 *
 *  Description:
 *      This is the header file for code which implements the Secure
 *      Hashing Algorithm 1 as defined in FIPS PUB 180-1 published
 *      April 17, 1995.
 *
 *      Many of the variable names in this code, especially the
 *      single character names, were used because those were the names
 *      used in the publication.
 *
 *      Please read the file xlnx_hdcp_sha1.c for more information.
 *
 */

#ifndef _XLNX_HDCP_SHA1_H_
#define _XLNX_HDCP_SHA1_H_

#include <linux/device.h>
#include <linux/io.h>

enum {
	XLNX_SHA_SUCCESS = 0,
	XLNX_SHA_NULL = 1,  /* Null pointer parameter */
	XLNX_SHA_INPUT_TOO_LONG = 2,  /* input data too long */
	XLNX_SHA_STATE_ERROR = 3     /* called Input after Result */
};

#define SHA1_HASH_SIZE 20
#define MESSAGE_BLOCK_SIZE 64
#define SHA_INTERMEDIATE_HASH_H3 3
#define SHA_MAX_HASH_OPERATIONS 80
#define SHA_BITS_TO_ROTATE 30
#define SHA_BITS_TO_ROTATE_ROUND2 40
#define SHA_BITS_TO_ROTATE_ROUND3 60

#define K1	0x5a827999
#define K2	0x6ed9eba1
#define K3	0x8f1bbcdc
#define K4	0xca62c1d6

/**
 * struct xlnx_sha1_context - This structure holds the context
 * information for the SHA-1 hashing operation.
 * @intermediate_hash: Message digest
 * @length_low: Message length in bits
 * @length_high: Message length in bits
 * @msg_block_index: Index into message block array
 * @message_block: 512-bit message block array
 * @computed: Indicates the message digest is computed
 * @corrupted: Indicates the message digest is corrupted
 */
struct xlnx_sha1_context {
	unsigned int intermediate_hash[SHA1_HASH_SIZE / 4];
	unsigned int length_low;
	unsigned int length_high;
	u16 msg_block_index;
	unsigned char message_block[MESSAGE_BLOCK_SIZE];
	int computed;
	int corrupted;
};

int xlnx_sha1_reset(struct xlnx_sha1_context *context);
int xlnx_sha1_input(struct xlnx_sha1_context *context, const unsigned char *msg,
		    unsigned int length);
int xlnx_sha1_result(struct xlnx_sha1_context *context,
		     unsigned char message_digest[SHA1_HASH_SIZE]);
void xlnx_sha1_pad_message(struct xlnx_sha1_context *context);
void xlnx_sha1_process_message_block(struct xlnx_sha1_context *context);

#endif
