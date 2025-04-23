// SPDX-License-Identifier: GPL-2.0
/*
 * Firmware layer for XilSecure APIs.
 *
 * Copyright (C), 2025 Advanced Micro Devices, Inc.
 */

#include <linux/firmware/xlnx-zynqmp.h>

int zynqmp_pm_secure_load(const u64 src_addr, u64 key_addr, u64 *dst)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret_value;

	if (!dst)
		return -EINVAL;

	ret_value = zynqmp_pm_invoke_fn(PM_SECURE_IMAGE, ret_payload, 4,
					lower_32_bits(src_addr),
					upper_32_bits(src_addr),
					lower_32_bits(key_addr),
					upper_32_bits(key_addr));
	*dst = ((u64)ret_payload[1] << 32) | ret_payload[2];

	return ret_value;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_secure_load);

/**
 * zynqmp_pm_sha_hash - Access the SHA engine to calculate the hash
 * @address:	Address of the data/ Address of output buffer where
 *		hash should be stored.
 * @size:	Size of the data.
 * @flags:
 *	BIT(0) - for initializing csudma driver and SHA3(Here address
 *		 and size inputs can be NULL).
 *	BIT(1) - to call Sha3_Update API which can be called multiple
 *		 times when data is not contiguous.
 *	BIT(2) - to get final hash of the whole updated data.
 *		 Hash will be overwritten at provided address with
 *		 48 bytes.
 *
 * Return:	Returns status, either success or error code.
 */
int zynqmp_pm_sha_hash(const u64 address, const u32 size, const u32 flags)
{
	u32 lower_addr = lower_32_bits(address);
	u32 upper_addr = upper_32_bits(address);

	return zynqmp_pm_invoke_fn(PM_SECURE_SHA, NULL, 4, upper_addr, lower_addr, size, flags);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_sha_hash);

int versal_pm_puf_registration(const u64 in_addr)
{
	return zynqmp_pm_invoke_fn(XPUF_API_PUF_REGISTRATION, NULL,
				   2, lower_32_bits(in_addr),
				   upper_32_bits(in_addr));
}
EXPORT_SYMBOL_GPL(versal_pm_puf_registration);

int versal_pm_puf_clear_id(void)
{
	return zynqmp_pm_invoke_fn(XPUF_API_PUF_CLEAR_PUF_ID, NULL,
				   2, NULL, NULL);
}
EXPORT_SYMBOL_GPL(versal_pm_puf_clear_id);

int versal_pm_puf_regeneration(const u64 in_addr)
{
	return zynqmp_pm_invoke_fn(XPUF_API_PUF_REGENERATION, NULL,
				   2, lower_32_bits(in_addr),
				   upper_32_bits(in_addr));
}
EXPORT_SYMBOL_GPL(versal_pm_puf_regeneration);

/**
 * zynqmp_pm_efuse_access - Provides access to efuse memory.
 * @address:	Address of the efuse params structure
 * @out:		Returned output value
 *
 * Return:	Returns status, either success or error code.
 */
int zynqmp_pm_efuse_access(const u64 address, u32 *out)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!out)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_EFUSE_ACCESS, ret_payload, 2,
				  upper_32_bits(address),
				  lower_32_bits(address));
	*out = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_efuse_access);

/**
 * versal_pm_efuse_read - Reads efuse.
 * @address: Address of the payload
 * @offset: Efuse offset
 * @size: Size of data to be read
 *
 * This function provides support to read data from eFuse.
 *
 * Return: status, either success or error code.
 */
int versal_pm_efuse_read(const u64 address, u32 offset, u32 size)
{
	return zynqmp_pm_invoke_fn(PM_EFUSE_READ_VERSAL, NULL, 4, offset,
				   lower_32_bits(address),
				   upper_32_bits(address), size);
}
EXPORT_SYMBOL_GPL(versal_pm_efuse_read);

/**
 * versal_pm_efuse_write - Write efuse
 * @address: Address of the payload
 * @operationid: operationid which includes module and API id
 * @envdis: Environment disable variable
 *
 * This function provides support to write data into eFuse.
 *
 * Return: status, either success or error+reason
 */
int versal_pm_efuse_write(const u64 address, const u32 operationid,
			  const u8 envdis)
{
	return zynqmp_pm_invoke_fn(operationid, NULL, 3, lower_32_bits(address),
				   upper_32_bits(address), envdis);
}
EXPORT_SYMBOL_GPL(versal_pm_efuse_write);

/**
 * versal_pm_sha_hash - Access the SHA engine to calculate the hash
 * @src:	Address of the data
 * @dst:	Address of the output buffer
 * @size:	Size of the data.
 *
 * Return:	Returns status, either success or error code.
 */
int versal_pm_sha_hash(const u64 src, const u64 dst, const u32 size)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_SHA3_UPDATE, NULL, 5,
				   lower_32_bits(src), upper_32_bits(src),
				   size,
				   lower_32_bits(dst), upper_32_bits(dst));
}
EXPORT_SYMBOL_GPL(versal_pm_sha_hash);

/**
 * versal_pm_rsa_encrypt - Access RSA hardware to encrypt the data with RSA.
 * @in_params:	Address of the input parameter
 * @in_addr:	Address of input buffer
 *
 * Return:	Returns status, either success or error code.
 */
int versal_pm_rsa_encrypt(const u64 in_params, const u64 in_addr)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_RSA_PUBLIC_ENCRYPT, NULL, 4,
				   lower_32_bits(in_params),
				   upper_32_bits(in_params),
				   lower_32_bits(in_addr),
				   upper_32_bits(in_addr));
}
EXPORT_SYMBOL_GPL(versal_pm_rsa_encrypt);

/**
 * versal_pm_rsa_decrypt - Access RSA hardware to decrypt the data with RSA.
 * @in_params:	Address of the input parameter
 * @in_addr:	Address of input buffer
 *
 * Return:	Returns status, either success or error code.
 */
int versal_pm_rsa_decrypt(const u64 in_params, const u64 in_addr)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_RSA_PRIVATE_DECRYPT, NULL, 4,
				   lower_32_bits(in_params),
				   upper_32_bits(in_params),
				   lower_32_bits(in_addr),
				   upper_32_bits(in_addr));
}
EXPORT_SYMBOL_GPL(versal_pm_rsa_decrypt);

/**
 * versal_pm_ecdsa_validate_key - Access ECDSA hardware to validate key
 * @key_addr:	Address of the key
 * @curve_id:	Type of ECC curve
 *
 * Return:	Returns status, either success or error code.
 */
int versal_pm_ecdsa_validate_key(const u64 key_addr, const u32 curve_id)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_ELLIPTIC_VALIDATE_KEY,
				   NULL, 3, curve_id,
				   lower_32_bits(key_addr),
				   upper_32_bits(key_addr));
}
EXPORT_SYMBOL_GPL(versal_pm_ecdsa_validate_key);

/**
 * versal_pm_ecdsa_verify_sign - Access ECDSA hardware to verify sign
 * @sign_param_addr:	Address of the sign params
 *
 * Return:	Returns status, either success or error code.
 */
int versal_pm_ecdsa_verify_sign(const u64 sign_param_addr)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_ELLIPTIC_VERIFY_SIGN,
				   NULL, 2, lower_32_bits(sign_param_addr),
				   upper_32_bits(sign_param_addr));
}
EXPORT_SYMBOL_GPL(versal_pm_ecdsa_verify_sign);

/**
 * versal_pm_aes_key_write - Write AES key registers
 * @keylen:	Size of the input key to be written
 * @keysrc:	Key Source to be selected to which provided
 *			key should be updated
 * @keyaddr:	Address of a buffer which should contain the key
 *			to be written
 *
 * This function provides support to write AES volatile user keys.
 *
 * Return: Returns status, either success or error+reason
 */
int versal_pm_aes_key_write(const u32 keylen,
			    const u32 keysrc, const u64 keyaddr)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_AES_WRITE_KEY, NULL, 4,
				   keylen, keysrc,
				   lower_32_bits(keyaddr),
				   upper_32_bits(keyaddr));
}
EXPORT_SYMBOL_GPL(versal_pm_aes_key_write);

/**
 * versal_pm_aes_key_zero - Zeroise AES User key registers
 * @keysrc:	Key Source to be selected to which provided
 *		key should be updated
 *
 * This function provides support to zeroise AES volatile user keys.
 *
 * Return: Returns status, either success or error+reason
 */
int versal_pm_aes_key_zero(const u32 keysrc)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_AES_KEY_ZERO, NULL, 1, keysrc);
}
EXPORT_SYMBOL_GPL(versal_pm_aes_key_zero);

/**
 * versal_pm_aes_op_init - Init AES operation
 * @hw_req:	AES op init structure address
 *
 * This function provides support to init AES operation.
 *
 * Return: Returns status, either success or error+reason
 */
int versal_pm_aes_op_init(const u64 hw_req)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_AES_OP_INIT, NULL, 2,
				   lower_32_bits(hw_req),
				   upper_32_bits(hw_req));
}
EXPORT_SYMBOL_GPL(versal_pm_aes_op_init);

/**
 * versal_pm_aes_update_aad - AES update aad
 * @aad_addr:	AES aad address
 * @aad_len:	AES aad data length
 *
 * This function provides support to update AAD data.
 *
 * Return: Returns status, either success or error+reason
 */
int versal_pm_aes_update_aad(const u64 aad_addr, const u32 aad_len)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_AES_UPDATE_AAD, NULL, 3,
				   lower_32_bits(aad_addr),
				   upper_32_bits(aad_addr),
				   aad_len);
}
EXPORT_SYMBOL_GPL(versal_pm_aes_update_aad);

/**
 * versal_pm_aes_enc_update - Access AES hardware to encrypt the data using
 * AES-GCM core.
 * @in_params:	Address of the AesParams structure
 * @in_addr:	Address of input buffer
 *
 * Return:	Returns status, either success or error code.
 */
int versal_pm_aes_enc_update(const u64 in_params, const u64 in_addr)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_AES_ENCRYPT_UPDATE, NULL, 4,
				   lower_32_bits(in_params),
				   upper_32_bits(in_params),
				   lower_32_bits(in_addr),
				   upper_32_bits(in_addr));
}
EXPORT_SYMBOL_GPL(versal_pm_aes_enc_update);

/**
 * versal_pm_aes_enc_final - Access AES hardware to store the GCM tag
 * @gcm_addr:	Address of the gcm tag
 *
 * Return:	Returns status, either success or error code.
 */
int versal_pm_aes_enc_final(const u64 gcm_addr)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_AES_ENCRYPT_FINAL, NULL, 2,
				   lower_32_bits(gcm_addr),
				   upper_32_bits(gcm_addr));
}
EXPORT_SYMBOL_GPL(versal_pm_aes_enc_final);

/**
 * versal_pm_aes_dec_update - Access AES hardware to decrypt the data using
 * AES-GCM core.
 * @in_params:	Address of the AesParams structure
 * @in_addr:	Address of input buffer
 *
 * Return:	Returns status, either success or error code.
 */
int versal_pm_aes_dec_update(const u64 in_params, const u64 in_addr)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_AES_DECRYPT_UPDATE, NULL, 4,
				   lower_32_bits(in_params),
				   upper_32_bits(in_params),
				   lower_32_bits(in_addr),
				   upper_32_bits(in_addr));
}
EXPORT_SYMBOL_GPL(versal_pm_aes_dec_update);

/**
 * versal_pm_aes_dec_final - Access AES hardware to get the GCM tag
 * @gcm_addr:	Address of the gcm tag
 *
 * Return:	Returns status, either success or error code.
 */
int versal_pm_aes_dec_final(const u64 gcm_addr)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_AES_DECRYPT_FINAL, NULL, 2,
				   lower_32_bits(gcm_addr),
				   upper_32_bits(gcm_addr));
}
EXPORT_SYMBOL_GPL(versal_pm_aes_dec_final);

/**
 * versal_pm_aes_init - Init AES block
 *
 * This function initialise AES block.
 *
 * Return: Returns status, either success or error+reason
 */
int versal_pm_aes_init(void)
{
	return zynqmp_pm_invoke_fn(XSECURE_API_AES_INIT, NULL, 0);
}
EXPORT_SYMBOL_GPL(versal_pm_aes_init);

/**
 * zynqmp_pm_aes_engine - Access AES hardware to encrypt/decrypt the data using
 * AES-GCM core.
 * @address:	Address of the AesParams structure.
 * @out:	Returned output value
 *
 * Return:	Returns status, either success or error code.
 */
int zynqmp_pm_aes_engine(const u64 address, u32 *out)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!out)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_SECURE_AES, ret_payload, 2, upper_32_bits(address),
				  lower_32_bits(address));
	*out = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_aes_engine);

/**
 * xlnx_get_crypto_dev_data() - Get crypto dev data of platform
 * @feature_map:	List of available feature map of all platform
 *
 * Return: Returns crypto dev data, either address crypto dev or ERR PTR
 */
void *xlnx_get_crypto_dev_data(struct xlnx_feature *feature_map)
{
	struct xlnx_feature *feature;
	u32 v;
	u32 pm_family_code;
	u32 pm_sub_family_code;
	int ret;

	ret = zynqmp_pm_get_api_version(&v);
	if (ret)
		return ERR_PTR(ret);

	/* Get the Family code and sub family code of platform */
	ret = zynqmp_pm_get_family_info(&pm_family_code, &pm_sub_family_code);
	if (ret < 0)
		return ERR_PTR(ret);

	feature = feature_map;
	for (; feature->family; feature++) {
		if (feature->family == pm_family_code &&
		    (feature->subfamily == ALL_SUB_FAMILY_CODE ||
		     pm_sub_family_code <= VERSAL_SUB_FAMILY_CODE_MAX)) {
			if (feature->family == ZYNQMP_FAMILY_CODE ||
			    feature->family == VERSAL_FAMILY_CODE) {
				ret = zynqmp_pm_feature(feature->feature_id);
				if (ret < 0)
					return ERR_PTR(ret);
			} else {
				return ERR_PTR(-ENODEV);
			}

			return feature->data;
		}
	}
	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(xlnx_get_crypto_dev_data);
