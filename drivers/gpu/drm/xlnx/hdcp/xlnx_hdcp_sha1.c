// SPDX-License-Identifier: GPL-2.0
/*
 *  xlnx_hdcp_sha1.c
 *
 *  Description:
 *      This file implements the Secure Hashing Algorithm 1 as
 *      defined in FIPS PUB 180-1 published April 17, 1995.
 *
 *      The SHA-1, produces a 160-bit message digest for a given
 *      data stream.  It should take about 2**n steps to find a
 *      message with the same digest as a given message and
 *      2**(n/2) to find any two messages with the same digest,
 *      when n is the digest size in bits.  Therefore, this
 *      algorithm can serve as a means of providing a
 *      "fingerprint" for a message.
 *
 *  Caveats:
 *      SHA-1 is designed to work with messages less than 2^64 bits
 *      long.  Although SHA-1 allows a message digest to be generated
 *      for messages of any number of bits less than 2^64, this
 *      implementation only works with messages with a length that is
 *      a multiple of the size of an 8-bit character.
 *
 */

/* Some part of Code for sha calculations are modified according to Xilinx standards */

/* Reference : https://nvlpubs.nist.gov/nistpubs/Legacy/FIPS/NIST.FIPS.180.pdf */

#include <crypto/sha1.h>
#include "xlnx_hdcp_sha1.h"

/*
 *  Define the SHA1 circular left shift macro
 */
#define xlnx_sha1_circular_shift(bits, word) \
({\
	typeof(bits) (_x) = (bits); \
	typeof(word) (_y) = (word); \
	(((_y) << (_x)) | ((_y) >> (32 - (_x)))); \
})

/**
 * xlnx_sha1_reset - This function will initialize the xlnx_sha1_context in
 * preparation for computing a new SHA1 message digest
 * @context: SHA context structure
 * return: success on reset or SHA error code otherwise
 */
int xlnx_sha1_reset(struct xlnx_sha1_context *context)
{
	if (!context)
		return XLNX_SHA_NULL;

	context->length_low	= 0;
	context->length_high	= 0;
	context->msg_block_index = 0;

	memset(context->message_block, 0, MESSAGE_BLOCK_SIZE);
	context->intermediate_hash[0]   = SHA1_H0;
	context->intermediate_hash[1]   = SHA1_H1;
	context->intermediate_hash[2]   = SHA1_H2;
	context->intermediate_hash[3]   = SHA1_H3;
	context->intermediate_hash[4]   = SHA1_H4;

	context->computed   = 0;
	context->corrupted  = 0;

	return XLNX_SHA_SUCCESS;
}

/**
 * xlnx_sha1_result - This function will return the 160-bit message
 * digest into the message_digest array  provided by the caller.
 * NOTE: The first octet of hash is stored in the 0th element,
 * the last octet of hash in the 19th element
 * @context: SHA context structure
 * @message_digest: Message digest output
 * return: success when message digest is correct or
 * SHA error code otherwise
 */
int xlnx_sha1_result(struct xlnx_sha1_context *context,
		     u8 message_digest[SHA1_HASH_SIZE])
{
	int i;

	if (!context || !message_digest)
		return XLNX_SHA_NULL;
	if (context->corrupted)
		return context->corrupted;
	if (!context->computed) {
		xlnx_sha1_pad_message(context);
		for (i = 0; i < MESSAGE_BLOCK_SIZE; ++i) {
			/* message may be sensitive, clear it out */
			context->message_block[i] = 0;
		}
		context->length_low = 0; /* and clear length */
		context->length_high = 0;
		context->computed = 1;
	}
	for (i = 0; i < SHA1_HASH_SIZE; ++i) {
		message_digest[i] = context->intermediate_hash[i >> 2]
					>> BITS_PER_BYTE * (SHA_INTERMEDIATE_HASH_H3 -
					(i & SHA_INTERMEDIATE_HASH_H3));
	}

	return XLNX_SHA_SUCCESS;
}

/**
 * xlnx_sha1_process_message_block - This function will process the next
 * 512 bits of the message stored in the message_block array.
 * NOTE: Many of the variable names in this code, especially the
 * single character names, were used because those were the
 * names used in the publication.
 * @context: SHA context structure
 * return: none
 */
void xlnx_sha1_process_message_block(struct xlnx_sha1_context *context)
{
	const unsigned int K[] = {
					/* Constants defined in SHA-1   */
					K1,
					K2,
					K3,
					K4
				 };
	int t; /* Loop counter */
	unsigned int temp;   /* Temporary word value  */
	unsigned int word_seq[SHA_MAX_HASH_OPERATIONS]; /* Word sequence  */
	unsigned int A, B, C, D, E; /* Word buffers */

	/*
	 * Initialize the first 16 words in the array word_seq
	 */
	for (t = 0; t < SHA1_WORKSPACE_WORDS; t++) {
		word_seq[t] = context->message_block[t * 4] << 24;
		word_seq[t] |= context->message_block[t * 4 + 1] << 16;
		word_seq[t] |= context->message_block[t * 4 + 2] << 8;
		word_seq[t] |= context->message_block[t * 4 + 3];
	}
	for (t = SHA1_WORKSPACE_WORDS; t < SHA_MAX_HASH_OPERATIONS; t++)
		word_seq[t] = xlnx_sha1_circular_shift(1,
						       word_seq[t - 3] ^ word_seq[t - 8] ^
						       word_seq[t - 14] ^ word_seq[t - 16]);
	A = context->intermediate_hash[0];
	B = context->intermediate_hash[1];
	C = context->intermediate_hash[2];
	D = context->intermediate_hash[3];
	E = context->intermediate_hash[4];

	for (t = 0; t < SHA1_DIGEST_SIZE; t++) {
		temp =  xlnx_sha1_circular_shift(5, A) +
			((B & C) | ((~B) & D)) + E +
			word_seq[t] + K[0];
		E = D;
		D = C;
		C = xlnx_sha1_circular_shift(SHA_BITS_TO_ROTATE, B);
		B = A;
		A = temp;
	}
	for (t = SHA1_DIGEST_SIZE; t < SHA_BITS_TO_ROTATE_ROUND2; t++) {
		temp = xlnx_sha1_circular_shift(5, A) + (B ^ C ^ D) + E + word_seq[t] + K[1];
		E = D;
		D = C;
		C = xlnx_sha1_circular_shift(SHA_BITS_TO_ROTATE, B);
		B = A;
		A = temp;
	}
	for (t = SHA_BITS_TO_ROTATE_ROUND2; t < SHA_BITS_TO_ROTATE_ROUND3; t++) {
		temp = xlnx_sha1_circular_shift(5, A) +
		       ((B & C) | (B & D) | (C & D)) + E +
		       word_seq[t] + K[2];
		E = D;
		D = C;
		C = xlnx_sha1_circular_shift(SHA_BITS_TO_ROTATE, B);
		B = A;
		A = temp;
	}
	for (t = SHA_BITS_TO_ROTATE_ROUND2; t < SHA_MAX_HASH_OPERATIONS; t++) {
		temp = xlnx_sha1_circular_shift(5, A) + (B ^ C ^ D) + E + word_seq[t] + K[3];
		E = D;
		D = C;
		C = xlnx_sha1_circular_shift(SHA_BITS_TO_ROTATE, B);
		B = A;
		A = temp;
	}

	context->intermediate_hash[0] += A;
	context->intermediate_hash[1] += B;
	context->intermediate_hash[2] += C;
	context->intermediate_hash[3] += D;
	context->intermediate_hash[4] += E;

	context->msg_block_index = 0;
}

/**
 * xlnx_sha1_input - This function accepts an array of octets as the
 * next portion of the message
 * @context: SHA context structure
 * @message_array: An array of characters represetning the next
 * portion of the message
 * @length: The length of the message in message_array
 * return: success when new input is added to the SHA message or
 * SHA error code otherwise
 */
int xlnx_sha1_input(struct xlnx_sha1_context *context,
		    const unsigned char *message_array,
		    unsigned int length)
{
	if (!length)
		return XLNX_SHA_SUCCESS;
	if (!context || !message_array)
		return XLNX_SHA_NULL;
	if (context->computed) {
		context->corrupted = XLNX_SHA_STATE_ERROR;
		return XLNX_SHA_STATE_ERROR;
	}

	if (context->corrupted)
		return context->corrupted;
	while (length-- && !context->corrupted) {
		context->message_block[context->msg_block_index++] =
				(*message_array & 0xFF);
		context->length_low += 8;
		if (context->length_low == 0) {
			context->length_high++;
			if (context->length_high == 0) {
				/* Message is too long */
				context->corrupted = 1;
			}
		}
		if (context->msg_block_index == MESSAGE_BLOCK_SIZE)
			xlnx_sha1_process_message_block(context);

		message_array++;
	}

	return XLNX_SHA_SUCCESS;
}

/**
 * xlnx_sha1_pad_message - According to the standard, the message must be
 * padded to an even 512 bits.The first padding bit must be a '1'.
 * The last 64 bits represent the length of the original message.
 * All bits in between should be 0.This function will pad the message
 * according to those rules by filling the Message_Block array
 * accordingly.  It will also call the ProcessMessageBlock function
 * provided appropriately.  When it returns, it can be assumed that
 * the message digest has been computed.
 * @context: SHA context structure
 * return: None
 */
void xlnx_sha1_pad_message(struct xlnx_sha1_context *context)
{
	/*
	 * Check to see if the current message block is too small to hold
	 * the initial padding bits and length.  If so, we will pad the
	 * block, process it, and then continue padding into a second
	 * block.
	 */
	if (context->msg_block_index > 55) {
		context->message_block[context->msg_block_index++] = 0x80;
		while (context->msg_block_index < SHA1_BLOCK_SIZE)
			context->message_block[context->msg_block_index++] = 0;
		xlnx_sha1_process_message_block(context);
		while (context->msg_block_index < 56)
			context->message_block[context->msg_block_index++] = 0;
	} else {
		context->message_block[context->msg_block_index++] = 0x80;
		while (context->msg_block_index < 56)
			context->message_block[context->msg_block_index++] = 0;
	}
	/*
	 * Store the message length as the last 8 octets.
	 */
	context->message_block[56] = context->length_high >> 24;
	context->message_block[57] = context->length_high >> 16;
	context->message_block[58] = context->length_high >> 8;
	context->message_block[59] = context->length_high;
	context->message_block[60] = context->length_low >> 24;
	context->message_block[61] = context->length_low >> 16;
	context->message_block[62] = context->length_low >> 8;
	context->message_block[63] = context->length_low;

	xlnx_sha1_process_message_block(context);
}
