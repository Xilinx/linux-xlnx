// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ZynqMP SecureFw Driver.
 * Copyright (C) 2018 - 2022 Xilinx Inc.
 * Copyright (C) 2022 - 2025 Advanced Micro Devices, Inc.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#define ZYNQMP_AES_KEY_SIZE	64
#define ZYNQMP_SECURE_DATA_FILE_NAME	"xlnx_secure_data.bin"

struct secure_driver_data {
	u8 key[ZYNQMP_AES_KEY_SIZE];
	bool has_key;
};

static ssize_t secure_load_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct secure_driver_data *drv_data = dev_get_drvdata(dev);
	const struct firmware *fw;
	unsigned int trigger;
	dma_addr_t dma_addr;
	size_t dma_size;
	char *kbuf;
	int ret;
	u64 dst;

	if (!drv_data)
		return -ENODEV;

	ret = kstrtouint(buf, 10, &trigger);
	if (ret)
		return -EINVAL;

	if (trigger != 1)
		return -EINVAL;

	ret = request_firmware(&fw, ZYNQMP_SECURE_DATA_FILE_NAME, dev);
	if (ret) {
		dev_err(dev, "Error requesting firmware %s\n",
			ZYNQMP_SECURE_DATA_FILE_NAME);
		return ret;
	}
	dma_size = fw->size;

	if (drv_data->has_key)
		dma_size = fw->size + ZYNQMP_AES_KEY_SIZE;

	kbuf = dma_alloc_coherent(dev, dma_size,
				  &dma_addr, GFP_KERNEL);
	if (!kbuf) {
		release_firmware(fw);
		return -ENOMEM;
	}

	memcpy(kbuf, fw->data, fw->size);

	if (drv_data->has_key) {
		memcpy(kbuf + fw->size, drv_data->key, ZYNQMP_AES_KEY_SIZE);

		ret = zynqmp_pm_secure_load(dma_addr, dma_addr + fw->size,
					    &dst);
		drv_data->has_key = false;
		memzero_explicit(drv_data->key, ZYNQMP_AES_KEY_SIZE);
		memzero_explicit(kbuf + fw->size, ZYNQMP_AES_KEY_SIZE);
	} else {
		ret = zynqmp_pm_secure_load(dma_addr, 0, &dst);
	}

	release_firmware(fw);

	dma_free_coherent(dev, dma_size, kbuf, dma_addr);

	if (ret) {
		dev_err(dev, "Failed to load secure image\n");
		return ret;
	}
	dev_info(dev, "Secure image loaded successfully\n");

	return count;
}

static ssize_t key_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct secure_driver_data *drv_data = dev_get_drvdata(dev);

	if (!drv_data)
		return -ENODEV;

	if (!count || count > ZYNQMP_AES_KEY_SIZE)
		return -EINVAL;

	memcpy(drv_data->key, buf, count);
	if (count < ZYNQMP_AES_KEY_SIZE)
		memzero_explicit(drv_data->key + count, ZYNQMP_AES_KEY_SIZE - count);
	drv_data->has_key = true;
	return count;
}

static DEVICE_ATTR_WO(key);
static DEVICE_ATTR_WO(secure_load);

static struct attribute *securefw_attrs[] = {
	&dev_attr_secure_load.attr,
	&dev_attr_key.attr,
	NULL,
};

ATTRIBUTE_GROUPS(securefw);

static int securefw_probe(struct platform_device *pdev)
{
	struct platform_device *securefw_pdev;
	struct secure_driver_data *drv_data;
	int ret;

	securefw_pdev = pdev;
	drv_data = devm_kzalloc(&pdev->dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	platform_set_drvdata(securefw_pdev, drv_data);

	ret = of_dma_configure(&securefw_pdev->dev, NULL, true);
	if (ret < 0) {
		dev_err(&securefw_pdev->dev, "Cannot setup DMA ops\n");
		return ret;
	}

	ret = dma_set_mask_and_coherent(&securefw_pdev->dev, DMA_BIT_MASK(32));
	if (ret < 0) {
		dev_err(&securefw_pdev->dev, "No usable DMA configuration\n");
		return ret;
	}

	ret = sysfs_create_groups(&securefw_pdev->dev.kobj, securefw_groups);
	if (ret)
		return ret;

	dev_info(&securefw_pdev->dev, "securefw probed\n");
	return ret;
}

static void securefw_remove(struct platform_device *pdev)
{
	struct secure_driver_data *drv_data = platform_get_drvdata(pdev);

	sysfs_remove_groups(&pdev->dev.kobj, securefw_groups);

	if (!drv_data)
		return;

	if (drv_data->has_key)
		memzero_explicit(drv_data->key, ZYNQMP_AES_KEY_SIZE);
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

MODULE_DESCRIPTION("Xilinx ZynqMP Secure Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kalyani Akula <kalyani.akula@amd.com>");
