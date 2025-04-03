// SPDX-License-Identifier: GPL-2.0
/*
 * Firmware layer for XilPDI APIs.
 *
 * Copyright (C), 2025 Advanced Micro Devices, Inc.
 */

#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/firmware.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

/* firmware required uid buff size */
#define UID_BUFF_SIZE	786
#define UID_SET_LEN	4
#define UID_LEN		4

static char image_name[NAME_MAX];

/**
 * zynqmp_pm_get_uid_info - It is used to get image Info List
 * @address:	Buffer address
 * @size:	Number of bytes required to read from the firmware.
 * @count:	Number of bytes read from the firmware.
 *
 * This function provides support to used to get image Info List
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_get_uid_info(const u64 address, const u32 size, u32 *count)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!count)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_GET_UID_INFO_LIST, ret_payload, 3,
				  upper_32_bits(address),
				  lower_32_bits(address),
				  size);

	*count = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_uid_info);

/**
 * zynqmp_pm_get_meta_header - It is used to get image meta header Info
 * @src:	PDI Image source buffer address.
 * @dst:	Meta-header destination buffer address
 * @size:	Size of the PDI image.
 * @count:	Number of bytes read from the firmware.
 *
 * This function provides a support to get the image meta header Info
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_get_meta_header(const u64 src, const u64 dst,
			      const u32 size, u32 *count)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!count)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_GET_META_HEADER_INFO_LIST, ret_payload, 5,
				  upper_32_bits(src), lower_32_bits(src),
				  upper_32_bits(dst), lower_32_bits(dst),
				  size);

	*count = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_meta_header);

/**
 * zynqmp_pm_load_pdi - Load and process PDI
 * @src:	Source device where PDI is located
 * @address:	PDI src address
 *
 * This function provides support to load PDI from linux
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_load_pdi(const u32 src, const u64 address)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;
	u64 swapped_address;

	ret = zynqmp_pm_load_pdi_word_swap(address, &swapped_address);
	if (ret)
		return ret;

	ret = zynqmp_pm_invoke_fn(PM_LOAD_PDI, ret_payload, 3, src,
				  lower_32_bits(swapped_address),
				  upper_32_bits(swapped_address));
	if (ret_payload[0])
		return ret_payload[0];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_load_pdi);

/**
 * zynqmp_pm_rsa - Access RSA hardware to encrypt/decrypt the data with RSA.
 * @address:	Address of the data
 * @size:	Size of the data.
 * @flags:
 *		BIT(0) - Encryption/Decryption
 *			 0 - RSA decryption with private key
 *			 1 - RSA encryption with public key.
 *
 * Return:	Returns status, either success or error code.
 */
int zynqmp_pm_rsa(const u64 address, const u32 size, const u32 flags)
{
	u32 lower_32_bits = lower_32_bits(address);
	u32 upper_32_bits = upper_32_bits(address);

	return zynqmp_pm_invoke_fn(PM_SECURE_RSA, NULL, 4, upper_32_bits,
				   lower_32_bits, size, flags);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_rsa);

static ssize_t firmware_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	unsigned int len;

	len = strscpy(image_name, buf, NAME_MAX);
	/* lose terminating \n */
	if (image_name[len - 1] == '\n')
		image_name[len - 1] = 0;

	return count;
}
static DEVICE_ATTR_WO(firmware);

static const struct attribute *firmware_attrs[] = {
	&dev_attr_firmware.attr,
	NULL,
};

static ssize_t firmware_uid_get_data(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *attr, char *buf,
				     loff_t off, size_t count)
{
	struct device *kdev = kobj_to_dev(kobj);
	dma_addr_t dma_addr = 0;
	char *kbuf;
	u32 size;
	int ret;

	kbuf = dma_alloc_coherent(kdev, UID_BUFF_SIZE, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	/* Read from the firmware memory */
	ret = zynqmp_pm_get_uid_info(dma_addr, UID_BUFF_SIZE, &size);
	if (ret) {
		dma_free_coherent(kdev, UID_BUFF_SIZE, kbuf, dma_addr);
		return ret;
	}

	size = size * UID_SET_LEN * UID_LEN;
	memcpy(buf, kbuf, size);
	dma_free_coherent(kdev, UID_BUFF_SIZE, kbuf, dma_addr);

	return size;
}

static const struct bin_attribute uid_attr = {
	.attr.name = "uid-read",
	.attr.mode = 00400,
	.size = 1,
	.read = firmware_uid_get_data,
};

static ssize_t firmware_meta_header_get_data(struct file *filp,
					     struct kobject *kobj,
					     struct bin_attribute *attr,
					     char *buf, loff_t off,
					     size_t count)
{
	struct device *kdev = kobj_to_dev(kobj);
	const struct firmware *fw;
	dma_addr_t dma_addr = 0;
	char *kbuf;
	u32 size;
	int ret;

	ret = request_firmware(&fw, image_name, kdev);
	if (ret) {
		dev_err(kdev, "Error requesting firmware %s\n", image_name);
		return ret;
	}

	kbuf = dma_alloc_coherent(kdev, fw->size, &dma_addr, GFP_KERNEL);
	if (!kbuf) {
		ret = -ENOMEM;
		goto free_firmware;
	}

	memcpy(kbuf, fw->data, fw->size);

	/* Read from the firmware memory */
	ret = zynqmp_pm_get_meta_header(dma_addr, dma_addr, fw->size, &size);
	if (ret)
		goto free_dma;

	memcpy(buf, kbuf, size);
	ret = size;

free_dma:
	dma_free_coherent(kdev, fw->size, kbuf, dma_addr);
free_firmware:
	release_firmware(fw);

	return ret;
}

static const struct bin_attribute meta_header_attr = {
	.attr.name = "meta-header-read",
	.attr.mode = 00400,
	.size = 1,
	.read = firmware_meta_header_get_data,
};

int zynqmp_firmware_pdi_sysfs_entry(struct platform_device *pdev)
{
	int ret;

	ret = sysfs_create_files(&pdev->dev.kobj, firmware_attrs);
	if (ret) {
		pr_err("%s() Failed to create firmware attrs, err=%d\n",
		       __func__, ret);
		return ret;
	}

	ret = sysfs_create_bin_file(&pdev->dev.kobj, &uid_attr);
	if (ret) {
		pr_err("%s() Failed to create sysfs binary file for uid-read with error%d\n",
		       __func__, ret);
		return ret;
	}

	ret = sysfs_create_bin_file(&pdev->dev.kobj, &meta_header_attr);
	if (ret)
		pr_err("%s() Failed to create sysfs binary file for meta-header-read with error%d\n",
		       __func__, ret);

	return ret;
}
