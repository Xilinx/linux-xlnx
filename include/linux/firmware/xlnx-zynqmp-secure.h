/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Firmware layer for XilSECURE APIs.
 *
 * Copyright (C), 2025 Advanced Micro Devices, Inc.
 */

#ifndef __FIRMWARE_ZYNQMP_SECURE_H__
#define __FIRMWARE_ZYNQMP_SECURE_H__

/* xilSecure API commands  module id + api id */
#define XSECURE_API_RSA_SIGN_VERIFY	0x501
#define XSECURE_API_RSA_PUBLIC_ENCRYPT	0x502
#define XSECURE_API_RSA_PRIVATE_DECRYPT	0x503
#define XSECURE_API_SHA3_UPDATE		0x504
#define XSECURE_API_ELLIPTIC_VALIDATE_KEY	0x507
#define XSECURE_API_ELLIPTIC_VERIFY_SIGN	0x508
#define XSECURE_API_AES_INIT		0x509
#define XSECURE_API_AES_OP_INIT		0x50a
#define XSECURE_API_AES_UPDATE_AAD	0x50b
#define XSECURE_API_AES_ENCRYPT_UPDATE	0x50c
#define XSECURE_API_AES_ENCRYPT_FINAL	0x50d
#define XSECURE_API_AES_DECRYPT_UPDATE	0x50e
#define XSECURE_API_AES_DECRYPT_FINAL	0x50f
#define XSECURE_API_AES_KEY_ZERO	0x510
#define XSECURE_API_AES_WRITE_KEY	0x511

/* XilPuf API commands module id + api id */
#define XPUF_API_PUF_REGISTRATION	0xc01
#define XPUF_API_PUF_REGENERATION	0xc02
#define XPUF_API_PUF_CLEAR_PUF_ID	0xc03

/**
 * struct xlnx_feature - Feature data
 * @family:	Family code of platform
 * @subfamily:	Subfamily code of platform
 * @feature_id:	Feature id of module
 * @data:	Collection of all supported platform data
 */
struct xlnx_feature {
	u32 family;
	u32 subfamily;
	u32 feature_id;
	void *data;
};

enum xsecure_aeskeysize {
	XSECURE_AES_KEY_SIZE_128 = 16,
	XSECURE_AES_KEY_SIZE_256 = 32,
};

#if IS_REACHABLE(CONFIG_ZYNQMP_FIRMWARE)
void *xlnx_get_crypto_dev_data(struct xlnx_feature *feature_map);
int zynqmp_pm_secure_load(const u64 src_addr, u64 key_addr, u64 *dst);
int zynqmp_pm_sha_hash(const u64 address, const u32 size, const u32 flags);
int versal_pm_puf_registration(const u64 in_addr);
int versal_pm_puf_regeneration(const u64 in_addr);
int versal_pm_puf_clear_id(void);
int zynqmp_pm_efuse_access(const u64 address, u32 *out);
int versal_pm_efuse_read(const u64 address, u32 offset, u32 size);
int versal_pm_efuse_write(const u64 address, const u32 operationid, const u8 envdis);
int versal_pm_sha_hash(const u64 src, const u64 dst, const u32 size);
int versal_pm_rsa_encrypt(const u64 in_params, const u64 in_addr);
int versal_pm_rsa_decrypt(const u64 in_params, const u64 in_addr);
int versal_pm_ecdsa_validate_key(const u64 key_addr, const u32 curveid);
int versal_pm_ecdsa_verify_sign(const u64 sign_param_addr);
int zynqmp_pm_aes_engine(const u64 address, u32 *out);
int versal_pm_aes_key_write(const u32 keylen,
			    const u32 keysrc, const u64 keyaddr);
int versal_pm_aes_key_zero(const u32 keysrc);
int versal_pm_aes_op_init(const u64 hw_req);
int versal_pm_aes_update_aad(const u64 aad_addr, const u32 aad_len);
int versal_pm_aes_enc_update(const u64 in_params, const u64 in_addr);
int versal_pm_aes_dec_update(const u64 in_params, const u64 in_addr);
int versal_pm_aes_dec_final(const u64 gcm_addr);
int versal_pm_aes_enc_final(const u64 gcm_addr);
int versal_pm_aes_init(void);
#else
static inline void *xlnx_get_crypto_dev_data(struct xlnx_feature *feature_map)
{
	return ERR_PTR(-ENODEV);
}

static inline int zynqmp_pm_secure_load(const u64 src_addr, u64 key_addr, u64 *dst)
{
	return -ENODEV;
}

static inline int zynqmp_pm_sha_hash(const u64 address, const u32 size,
				     const u32 flags)
{
	return -ENODEV;
}

static inline int versal_pm_puf_registration(const u64 in_addr)
{
	return -ENODEV;
}

static inline int versal_pm_puf_regeneration(const u64 in_addr)
{
	return -ENODEV;
}

static inline int versal_pm_puf_clear_id(void)
{
	return -ENODEV;
}

static inline int versal_pm_efuse_read(const u64 address, u32 offset, u32 size)
{
	return -ENODEV;
}

static inline int versal_pm_efuse_write(const u64 address, const u32 operationid, const u8 envdis)
{
	return -ENODEV;
}

static inline int zynqmp_pm_efuse_access(const u64 address, u32 *out)
{
	return -ENODEV;
}

static inline int versal_pm_sha_hash(const u64 src, const u64 dst, const u32 size)
{
	return -ENODEV;
}

static inline int versal_pm_rsa_encrypt(const u64 in_params,
					const u64 in_addr)
{
	return -ENODEV;
}

static inline int versal_pm_rsa_decrypt(const u64 in_params,
					const u64 in_addr)
{
	return -ENODEV;
}

static inline int versal_pm_ecdsa_validate_key(const u64 key_addr,
					       const u32 curveid)
{
	return -ENODEV;
}

static inline int versal_pm_ecdsa_verify_sign(const u64 sign_param_addr)
{
	return -ENODEV;
}

static inline int zynqmp_pm_aes_engine(const u64 address, u32 *out)
{
	return -ENODEV;
}

static inline int versal_pm_aes_key_write(const u32 keylen,
					  const u32 keysrc, const u64 keyaddr)
{
	return -ENODEV;
}

static inline int versal_pm_aes_key_zero(const u32 keysrc)
{
	return -ENODEV;
}

static inline int versal_pm_aes_op_init(const u64 hw_req)
{
	return -ENODEV;
}

static inline int versal_pm_aes_update_aad(const u64 aad_addr,
					   const u32 aad_len)
{
	return -ENODEV;
}

static inline int versal_pm_aes_enc_update(const u64 in_params,
					   const u64 in_addr)
{
	return -ENODEV;
}

static inline int versal_pm_aes_dec_update(const u64 in_params,
					   const u64 in_addr)
{
	return -ENODEV;
}

static inline int versal_pm_aes_enc_final(const u64 gcm_addr)
{
	return -ENODEV;
}

static inline int versal_pm_aes_dec_final(const u64 gcm_addr)
{
	return -ENODEV;
}

static inline int versal_pm_aes_init(void)
{
	return -ENODEV;
}
#endif

#endif /* __FIRMWARE_ZYNQMP_SECURE_H__ */
