/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CRYPTO_ECDSA_H
#define _CRYPTO_ECDSA_H

struct ecdsa_signature_ctx {
	const struct ecc_curve *curve;
	u64 r[ECC_MAX_DIGITS];
	u64 s[ECC_MAX_DIGITS];
};

int ecdsa_get_signature_rs(u64 *dest, size_t hdrlen, unsigned char tag,
			   const void *value, size_t vlen, unsigned int ndigits);

#endif /* _CRYPTO_ECDSA_H */
