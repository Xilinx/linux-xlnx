// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Xilinx, Inc.
 */

#include <linux/dma-mapping.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#define AES_KEY_STRING_256_BYTES	64
#define AES_KEY_STRING_128_BYTES	32
#define AES_KEY_SIZE_256_BYTES		32
#define AES_KEY_SIZE_128_BYTES		16

#define BBRAM_ZEROIZE_OFFSET		0x4
#define BBRAM_KEY_OFFSET		0x10
#define BBRAM_USER_DATA_OFFSET		0x30
#define BBRAM_LOCK_DATA_OFFSET		0x48
#define AES_USER_KEY_0_OFFSET		0x110
#define AES_USER_KEY_1_OFFSET		0x130
#define AES_USER_KEY_2_OFFSET		0x150
#define AES_USER_KEY_3_OFFSET		0x170
#define AES_USER_KEY_4_OFFSET		0x190
#define AES_USER_KEY_5_OFFSET		0x1B0
#define AES_USER_KEY_6_OFFSET		0x1D0
#define AES_USER_KEY_7_OFFSET		0x1F0

#define BBRAM_USER_DATA_SIZE		0x4
#define BBRAM_LOCK_DATA_SIZE		0x4
#define BBRAM_ZEROIZE_SIZE		0x4

#define BBRAM_LOCK_DATA_VALUE		0x12345678
#define BBRAM_ZEROIZE_VALUE		0x87654321

#define NVMEM_SIZE			0x230

enum aes_keysrc {
	AES_USER_KEY_0 = 12,
	AES_USER_KEY_1 = 13,
	AES_USER_KEY_2 = 14,
	AES_USER_KEY_3 = 15,
	AES_USER_KEY_4 = 16,
	AES_USER_KEY_5 = 17,
	AES_USER_KEY_6 = 18,
	AES_USER_KEY_7 = 19
};

enum aes_keysize {
	AES_KEY_SIZE_128 = 0,
	AES_KEY_SIZE_256 = 2
};

static u_int32_t convert_char_to_nibble(char in_char, unsigned char *num)
{
	if ((in_char >= '0') && (in_char <= '9'))
		*num = in_char - '0';
	else if ((in_char >= 'a') && (in_char <= 'f'))
		*num = in_char - 'a' + 10;
	else if ((in_char >= 'A') && (in_char <= 'F'))
		*num = in_char - 'A' + 10;
	else
		return -EINVAL;

	return 0;
}

static u_int32_t convert_string_to_hex_be(const char *str, unsigned char *buf, u_int32_t len)
{
	u32 converted_len;
	unsigned char lower_nibble = 0;
	unsigned char upper_nibble = 0;
	u32 str_length = strnlen(str, 64);

	if (!str || !buf)
		return -EINVAL;

	if (len == 0)
		return -EINVAL;

	if ((str_length / 2) > len)
		return -EINVAL;

	converted_len = 0;
	while (converted_len < str_length) {
		if (convert_char_to_nibble(str[converted_len], &upper_nibble) == 0) {
			if (convert_char_to_nibble(str[converted_len + 1], &lower_nibble) == 0)
				buf[converted_len / 2] = (upper_nibble << 4) | lower_nibble;
			else
				return -EINVAL;
		} else {
			return -EINVAL;
		}

		converted_len += 2;
	}

	return 0;
}

static u_int32_t convert_string_to_hex_le(const char *str, unsigned char *buf, u_int32_t len)
{
	u32 converted_len;
	unsigned char lower_nibble = 0;
	unsigned char upper_nibble = 0;
	u32 str_index;
	u32 str_length = strnlen(str, 64);

	if (!str || !buf)
		return -EINVAL;

	if (len == 0)
		return -EINVAL;

	if (str_length != len)
		return -EINVAL;

	str_index = len / 2 - 1;
	converted_len = 0;

	while (converted_len < (len)) {
		if (convert_char_to_nibble(str[converted_len], &upper_nibble) == 0) {
			if (convert_char_to_nibble(str[converted_len + 1], &lower_nibble) == 0) {
				buf[str_index] = (upper_nibble << 4) | lower_nibble;
				str_index = str_index - 1;
			} else {
				return -EINVAL;
			}
		} else {
			return -EINVAL;
		}

		converted_len += 2;
	}

	return 0;
}

static int sec_cfg_write(void *context, unsigned int offset, void *val, size_t bytes)
{
	int ret;
	struct device *dev = context;
	dma_addr_t dma_addr;
	u8 *data;

	data = dma_alloc_coherent(dev, bytes, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	switch (offset) {
	case AES_USER_KEY_0_OFFSET:
	case AES_USER_KEY_1_OFFSET:
	case AES_USER_KEY_2_OFFSET:
	case AES_USER_KEY_3_OFFSET:
	case AES_USER_KEY_4_OFFSET:
	case AES_USER_KEY_5_OFFSET:
	case AES_USER_KEY_6_OFFSET:
	case AES_USER_KEY_7_OFFSET:
		if (bytes != AES_KEY_STRING_128_BYTES && bytes != AES_KEY_STRING_256_BYTES) {
			ret = -EINVAL;
		} else {
			ret = convert_string_to_hex_be((const char *)val, data, bytes);
			if (!ret) {
				u32 keylen, keysrc;

				keylen = (bytes == AES_KEY_STRING_128_BYTES) ? AES_KEY_SIZE_128 : AES_KEY_SIZE_256;
				keysrc = ((offset - AES_USER_KEY_0_OFFSET) / 0x20) + 12;
				ret = zynqmp_pm_write_aes_key(keylen, keysrc, dma_addr);
			}
		}
		break;
	case BBRAM_KEY_OFFSET:
		if (bytes != AES_KEY_STRING_256_BYTES) {
			ret = -EINVAL;
		} else {
			ret = convert_string_to_hex_le((const char *)val, data, bytes);
			if (!ret)
				ret = zynqmp_pm_bbram_write_aeskey(bytes / 2, dma_addr);
		}
		break;
	case BBRAM_USER_DATA_OFFSET:
		if (bytes != BBRAM_USER_DATA_SIZE)
			ret = -EINVAL;
		else
			ret = zynqmp_pm_bbram_write_usrdata(*(u32 *)val);
		break;
	case BBRAM_LOCK_DATA_OFFSET:
		if (bytes != BBRAM_USER_DATA_SIZE || *(u32 *)val != BBRAM_LOCK_DATA_VALUE)
			ret = -EINVAL;
		else
			ret = zynqmp_pm_bbram_lock_userdata();
		break;
	case BBRAM_ZEROIZE_OFFSET:
		if (bytes != BBRAM_ZEROIZE_SIZE || *(u32 *)val != BBRAM_ZEROIZE_VALUE)
			ret = -EINVAL;
		else
			ret = zynqmp_pm_bbram_zeroize();
		break;
	default:
		ret = -EINVAL;
		break;
	}

	dma_free_coherent(dev, bytes, data, dma_addr);

	return ret;
}

static int sec_cfg_read(void *context, unsigned int offset, void *val, size_t bytes)
{
	struct device *dev = context;
	dma_addr_t dma_addr;
	int *data;
	int ret = -EINVAL;

	if (offset != BBRAM_USER_DATA_OFFSET && bytes != BBRAM_USER_DATA_SIZE)
		return -EOPNOTSUPP;

	data = dma_alloc_coherent(dev, bytes, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = zynqmp_pm_bbram_read_usrdata(dma_addr);
	if (!ret)
		memcpy(val, data, bytes);

	dma_free_coherent(dev, bytes, data, dma_addr);

	return ret;
}

static struct nvmem_config secureconfig = {
	.name = "xilinx-secure-config",
	.owner = THIS_MODULE,
	.word_size = 1,
	.size = NVMEM_SIZE,
};

static const struct of_device_id sec_cfg_match[] = {
	{ .compatible = "xlnx,versal-sec-cfg", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sec_cfg_match);

static int sec_cfg_probe(struct platform_device *pdev)
{
	struct nvmem_device *nvmem;

	secureconfig.dev = &pdev->dev;
	secureconfig.priv = &pdev->dev;
	secureconfig.reg_read = sec_cfg_read;
	secureconfig.reg_write = sec_cfg_write;

	nvmem = nvmem_register(&secureconfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	dev_dbg(&pdev->dev, "Successfully registered driver to nvmem framework");

	return 0;
}

static struct platform_driver secure_config_driver = {
	.probe = sec_cfg_probe,
	.driver = {
		.name = "xilinx-secure-config",
		.of_match_table = sec_cfg_match,
	},
};

module_platform_driver(secure_config_driver);

MODULE_AUTHOR("Harsha <harsha.harsha@xilinx.com>");
MODULE_DESCRIPTION("Versal Secure Configuration driver");
MODULE_LICENSE("GPL v2");
