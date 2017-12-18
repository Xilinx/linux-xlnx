/*
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/string.h>
#include <crypto/dh.h>
#include <crypto/kpp.h>

#define DH_KPP_SECRET_MIN_SIZE (sizeof(struct kpp_secret) + 3 * sizeof(int))

static inline u8 *dh_pack_data(void *dst, const void *src, size_t size)
{
	memcpy(dst, src, size);
	return dst + size;
}

static inline const u8 *dh_unpack_data(void *dst, const void *src, size_t size)
{
	memcpy(dst, src, size);
	return src + size;
}

static inline int dh_data_size(const struct dh *p)
{
	return p->key_size + p->p_size + p->g_size;
}

int crypto_dh_key_len(const struct dh *p)
{
	return DH_KPP_SECRET_MIN_SIZE + dh_data_size(p);
}
EXPORT_SYMBOL_GPL(crypto_dh_key_len);

int crypto_dh_encode_key(char *buf, unsigned int len, const struct dh *params)
{
	u8 *ptr = buf;
	struct kpp_secret secret = {
		.type = CRYPTO_KPP_SECRET_TYPE_DH,
		.len = len
	};

	if (unlikely(!buf))
		return -EINVAL;

	if (len != crypto_dh_key_len(params))
		return -EINVAL;

	ptr = dh_pack_data(ptr, &secret, sizeof(secret));
	ptr = dh_pack_data(ptr, &params->key_size, sizeof(params->key_size));
	ptr = dh_pack_data(ptr, &params->p_size, sizeof(params->p_size));
	ptr = dh_pack_data(ptr, &params->g_size, sizeof(params->g_size));
	ptr = dh_pack_data(ptr, params->key, params->key_size);
	ptr = dh_pack_data(ptr, params->p, params->p_size);
	dh_pack_data(ptr, params->g, params->g_size);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_dh_encode_key);

int crypto_dh_decode_key(const char *buf, unsigned int len, struct dh *params)
{
	const u8 *ptr = buf;
	struct kpp_secret secret;

	if (unlikely(!buf || len < DH_KPP_SECRET_MIN_SIZE))
		return -EINVAL;

	ptr = dh_unpack_data(&secret, ptr, sizeof(secret));
	if (secret.type != CRYPTO_KPP_SECRET_TYPE_DH)
		return -EINVAL;

	ptr = dh_unpack_data(&params->key_size, ptr, sizeof(params->key_size));
	ptr = dh_unpack_data(&params->p_size, ptr, sizeof(params->p_size));
	ptr = dh_unpack_data(&params->g_size, ptr, sizeof(params->g_size));
	if (secret.len != crypto_dh_key_len(params))
		return -EINVAL;

	/* Don't allocate memory. Set pointers to data within
	 * the given buffer
	 */
	params->key = (void *)ptr;
	params->p = (void *)(ptr + params->key_size);
	params->g = (void *)(ptr + params->key_size + params->p_size);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_dh_decode_key);
