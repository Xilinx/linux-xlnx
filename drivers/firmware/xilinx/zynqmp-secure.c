// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ZynqMP SecureFw Driver.
 * Copyright (C) 2018 - 2022 Xilinx Inc.
 * Copyright (C) 2022 - 2025 Advanced Micro Devices, Inc.
 */

#include <asm/cacheflush.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>

#define ZYNQMP_AES_KEY_SIZE	64

static u8 key[ZYNQMP_AES_KEY_SIZE] = {0};
static dma_addr_t dma_addr;
static u8 *keyptr;
static size_t dma_size;
static char *kbuf;

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
		     feature->subfamily == pm_sub_family_code)) {
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

static ssize_t secure_load_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	const struct firmware *fw;
	char image_name[NAME_MAX];
	u64 dst, ret;
	int len;

	len = strscpy(image_name, buf, NAME_MAX - 1);
	if (len > 0) {
		if (image_name[len - 1] == '\n')
			image_name[len - 1] = 0;
	} else {
		return -E2BIG;
	}

	ret = request_firmware(&fw, image_name, dev);
	if (ret) {
		dev_err(dev, "Error requesting firmware %s\n", image_name);
		return ret;
	}
	dma_size = fw->size;

	if (keyptr)
		dma_size = fw->size + ZYNQMP_AES_KEY_SIZE;

	kbuf = dma_alloc_coherent(dev, dma_size,
				  &dma_addr, GFP_KERNEL);
	if (!kbuf) {
		release_firmware(fw);
		return -ENOMEM;
	}

	memcpy(kbuf, fw->data, fw->size);

	if (keyptr)
		memcpy(kbuf + fw->size, key, ZYNQMP_AES_KEY_SIZE);

	/* To ensure cache coherency */
	caches_clean_inval_user_pou((unsigned long)kbuf,
				    (unsigned long)kbuf + dma_size);

	if (keyptr)
		ret = zynqmp_pm_secure_load(dma_addr, dma_addr + fw->size,
					    &dst);
	else
		ret = zynqmp_pm_secure_load(dma_addr, 0, &dst);

	release_firmware(fw);

	if (ret) {
		dev_info(dev, "Failed to load secure image \r\n");
		return ret;
	}
	dev_info(dev, "Verified image at 0x%llx\n", dst);

	return count;
}

static ssize_t key_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, ZYNQMP_AES_KEY_SIZE + 1, "%s\n", key);
}

static ssize_t key_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	memcpy(key, buf, count);
	keyptr = &key[0];
	return count;
}

static ssize_t secure_load_done_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	unsigned int value;

	ret = kstrtouint(buf, 10, &value);
	if (ret)
		return ret;
	if (value)
		dma_free_coherent(dev, dma_size, kbuf, dma_addr);

	return count;
}

static DEVICE_ATTR_RW(key);
static DEVICE_ATTR_WO(secure_load);
static DEVICE_ATTR_WO(secure_load_done);

static struct attribute *securefw_attrs[] = {
	&dev_attr_secure_load_done.attr,
	&dev_attr_secure_load.attr,
	&dev_attr_key.attr,
	NULL,
};

ATTRIBUTE_GROUPS(securefw);

static int securefw_probe(struct platform_device *pdev)
{
	int ret;
	struct platform_device *securefw_pdev;

	securefw_pdev = pdev;

	securefw_pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	ret = of_dma_configure(&securefw_pdev->dev, NULL, true);
	if (ret < 0) {
		dev_info(&securefw_pdev->dev, "Cannot setup DMA ops\r\n");
		return ret;
	}

	ret = sysfs_create_groups(&securefw_pdev->dev.kobj, securefw_groups);
	if (ret)
		return ret;

	dev_info(&securefw_pdev->dev, "securefw probed\r\n");
	return ret;
}

static void securefw_remove(struct platform_device *pdev)
{
	sysfs_remove_groups(&pdev->dev.kobj, securefw_groups);
}

static struct platform_driver securefw_driver = {
	.driver = {
		.name = "securefw",
	},
	.probe = securefw_probe,
	.remove = securefw_remove,
};

static struct platform_device *securefw_dev_reg;

static int __init zynqmp_secure_init(void)
{
	int ret;

	ret = platform_driver_register(&securefw_driver);
	if (ret)
		return ret;

	securefw_dev_reg = platform_device_register_simple("securefw", -1,
							   NULL, 0);
	if (IS_ERR(securefw_dev_reg)) {
		ret = PTR_ERR(securefw_dev_reg);
		platform_driver_unregister(&securefw_driver);
		return ret;
	}
	return 0;
}

static void __exit zynqmp_secure_exit(void)
{
	platform_device_unregister(securefw_dev_reg);
	platform_driver_unregister(&securefw_driver);
}

module_init(zynqmp_secure_init);
module_exit(zynqmp_secure_exit);
