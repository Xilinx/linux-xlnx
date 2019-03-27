// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ZynqMP SecureFw Driver.
 * Copyright (c) 2018 Xilinx Inc.
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

static const struct zynqmp_eemi_ops *eemi_ops;

static ssize_t secure_load_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	const struct firmware *fw;
	char image_name[NAME_MAX];
	u64 dst, ret;
	int len;

	if (!eemi_ops || !eemi_ops->secure_image)
		return -EFAULT;

	strncpy(image_name, buf, NAME_MAX);
	len = strlen(image_name);
	if (image_name[len - 1] == '\n')
		image_name[len - 1] = 0;

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
	if (!kbuf)
		return -ENOMEM;

	memcpy(kbuf, fw->data, fw->size);

	if (keyptr)
		memcpy(kbuf + fw->size, key, ZYNQMP_AES_KEY_SIZE);

	/* To ensure cache coherency */
	__flush_cache_user_range((unsigned long)kbuf,
				 (unsigned long)kbuf + dma_size);
	release_firmware(fw);

	if (keyptr)
		ret = eemi_ops->secure_image(dma_addr, dma_addr + fw->size,
					     &dst);
	else
		ret = eemi_ops->secure_image(dma_addr, 0, &dst);

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

	eemi_ops = zynqmp_pm_get_eemi_ops();
	if (IS_ERR(eemi_ops))
		return PTR_ERR(eemi_ops);

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

static int securefw_remove(struct platform_device *pdev)
{
	sysfs_remove_groups(&pdev->dev.kobj, securefw_groups);
	return 0;
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
