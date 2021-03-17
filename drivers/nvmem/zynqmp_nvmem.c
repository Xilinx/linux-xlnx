// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 - 2019 Xilinx, Inc.
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/firmware/xlnx-zynqmp.h>

#define SILICON_REVISION_MASK 0xF
#define WORD_INBYTES		(4)
#define SOC_VER_SIZE		(0x4)
#define EFUSE_MEMORY_SIZE	(0xF4)
#define UNUSED_SPACE		(0x8)
#define ZYNQMP_NVMEM_SIZE	(SOC_VER_SIZE + UNUSED_SPACE + \
				 EFUSE_MEMORY_SIZE)
#define SOC_VERSION_OFFSET	(0x0)
#define EFUSE_START_OFFSET	(0xC)
#define EFUSE_END_OFFSET	(0xFC)
#define EFUSE_NOT_ENABLED	(29)
#define EFUSE_READ		(0)
#define EFUSE_WRITE		(1)

/**
 * struct xilinx_efuse - the basic structure
 * @src:	address of the buffer to store the data to be write/read
 * @size:	no of words to be read/write
 * @offset:	offset to be read/write`
 * @flag:	0 - represents efuse read and 1- represents efuse write
 *
 * this structure stores all the required details to
 * read/write efuse memory.
 */
struct xilinx_efuse {
	u64 src;
	u32 size;
	u32 offset;
	u32 flag;
	u32 fullmap;
};

static int zynqmp_efuse_access(void *context, unsigned int offset,
			       void *val, size_t bytes, unsigned int flag)
{
	size_t words = bytes / WORD_INBYTES;
	struct device *dev = context;
	dma_addr_t dma_addr, dma_buf;
	struct xilinx_efuse *efuse;
	char *data;
	int ret;

	if (bytes % WORD_INBYTES != 0) {
		dev_err(dev, "Bytes requested should be word aligned\n");
		return -ENOTSUPP;
	}
	if (offset % WORD_INBYTES != 0) {
		dev_err(dev, "Offset requested should be word aligned\n");
		return -ENOTSUPP;
	}

	efuse = dma_alloc_coherent(dev, sizeof(struct xilinx_efuse),
				   &dma_addr, GFP_KERNEL);
	if (!efuse)
		return -ENOMEM;

	data = dma_alloc_coherent(dev, sizeof(bytes),
				  &dma_buf, GFP_KERNEL);
	if (!data) {
		dma_free_coherent(dev, sizeof(struct xilinx_efuse),
				  efuse, dma_addr);
		return -ENOMEM;
	}

	if (flag == EFUSE_WRITE) {
		memcpy(data, val, bytes);
		efuse->flag = EFUSE_WRITE;
	} else {
		efuse->flag = EFUSE_READ;
	}

	efuse->src = dma_buf;
	efuse->size = words;
	efuse->offset = offset;

	zynqmp_pm_efuse_access(dma_addr, &ret);
	if (ret != 0) {
		if (ret == EFUSE_NOT_ENABLED) {
			dev_err(dev, "efuse access is not enabled\n");
			ret = -ENOTSUPP;
			goto END;
		}
		dev_err(dev, "Error in efuse read %x\n", ret);
		ret = -EPERM;
		goto END;
	}

	if (flag == EFUSE_READ)
		memcpy(val, data, bytes);
END:

	dma_free_coherent(dev, sizeof(struct xilinx_efuse),
			  efuse, dma_addr);
	dma_free_coherent(dev, sizeof(bytes),
			  data, dma_buf);

	return ret;
}

static int zynqmp_nvmem_read(void *context, unsigned int offset,
					void *val, size_t bytes)
{
	int ret;
	int idcode, version;

	switch (offset) {
	/* Soc version offset is zero */
	case SOC_VERSION_OFFSET:
		if (bytes != SOC_VER_SIZE)
			return -ENOTSUPP;

		ret = zynqmp_pm_get_chipid(&idcode, &version);
		if (ret < 0)
			return ret;

		pr_debug("Read chipid val %x %x\n", idcode, version);
		*(int *)val = version & SILICON_REVISION_MASK;
		break;
	/* Efuse offset starts from 0xc */
	case EFUSE_START_OFFSET ... EFUSE_END_OFFSET:
		ret = zynqmp_efuse_access(context, offset, val,
					  bytes, EFUSE_READ);
		break;
	default:
		*(u32 *)val = 0xDEADBEEF;
		ret = 0;
		break;
	}

	return ret;
}

static int zynqmp_nvmem_write(void *context,
			      unsigned int offset, void *val, size_t bytes)
{
	/* Efuse offset starts from 0xc */
	if (offset < EFUSE_START_OFFSET)
		return -ENOTSUPP;

	return(zynqmp_efuse_access(context, offset,
				   val, bytes, EFUSE_WRITE));
}

static struct nvmem_config econfig = {
	.name = "zynqmp-nvmem",
	.owner = THIS_MODULE,
	.word_size = 1,
	.size = ZYNQMP_NVMEM_SIZE,
};

static const struct of_device_id zynqmp_nvmem_match[] = {
	{ .compatible = "xlnx,zynqmp-nvmem-fw", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, zynqmp_nvmem_match);

static int zynqmp_nvmem_probe(struct platform_device *pdev)
{
	struct nvmem_device *nvmem;

	econfig.dev = &pdev->dev;
	econfig.priv = &pdev->dev;
	econfig.reg_read = zynqmp_nvmem_read;
	econfig.reg_write = zynqmp_nvmem_write;

	nvmem = nvmem_register(&econfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

static struct platform_driver zynqmp_nvmem_driver = {
	.probe = zynqmp_nvmem_probe,
	.driver = {
		.name = "zynqmp-nvmem",
		.of_match_table = zynqmp_nvmem_match,
	},
};

module_platform_driver(zynqmp_nvmem_driver);

MODULE_AUTHOR("Michal Simek <michal.simek@xilinx.com>, Nava kishore Manne <navam@xilinx.com>");
MODULE_DESCRIPTION("ZynqMP NVMEM driver");
MODULE_LICENSE("GPL");
