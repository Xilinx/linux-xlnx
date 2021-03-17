// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 - 2021 Xilinx, Inc.
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/firmware/xlnx-zynqmp.h>

#define SILICON_REVISION_MASK 0xF
#define P_USER_0_64_UPPER_MASK	0x5FFF0000
#define P_USER_127_LOWER_4_BIT_MASK 0xF
#define WORD_INBYTES		(4)
#define SOC_VER_SIZE		(0x4)
#define EFUSE_MEMORY_SIZE	(0x177)
#define UNUSED_SPACE		(0x8)
#define ZYNQMP_NVMEM_SIZE	(SOC_VER_SIZE + UNUSED_SPACE + \
				 EFUSE_MEMORY_SIZE)
#define SOC_VERSION_OFFSET	(0x0)
#define EFUSE_START_OFFSET	(0xC)
#define EFUSE_END_OFFSET	(0xFC)
#define EFUSE_PUF_START_OFFSET	(0x100)
#define EFUSE_PUF_MID_OFFSET	(0x140)
#define EFUSE_PUF_END_OFFSET	(0x17F)
#define EFUSE_NOT_ENABLED	(29)
#define EFUSE_READ		(0)
#define EFUSE_WRITE		(1)

/**
 * struct xilinx_efuse - the basic structure
 * @src:	address of the buffer to store the data to be write/read
 * @size:	no of words to be read/write
 * @offset:	offset to be read/write`
 * @flag:	0 - represents efuse read and 1- represents efuse write
 * @pufuserfuse:0 - represents non-puf efuses, offset is used for read/write
 *		1 - represents puf user fuse row number.
 *
 * this structure stores all the required details to
 * read/write efuse memory.
 */
struct xilinx_efuse {
	u64 src;
	u32 size;
	u32 offset;
	u32 flag;
	u32 pufuserfuse;
};

static int zynqmp_efuse_access(void *context, unsigned int offset,
			       void *val, size_t bytes, unsigned int flag,
			       unsigned int pufflag)
{
	size_t words = bytes / WORD_INBYTES;
	struct device *dev = context;
	dma_addr_t dma_addr, dma_buf;
	struct xilinx_efuse *efuse;
	char *data;
	int ret, value;

	if (bytes % WORD_INBYTES != 0) {
		dev_err(dev, "Bytes requested should be word aligned\n");
		return -EOPNOTSUPP;
	}

	if (pufflag == 0 && offset % WORD_INBYTES) {
		dev_err(dev, "Offset requested should be word aligned\n");
		return -EOPNOTSUPP;
	}

	if (pufflag == 1 && flag == EFUSE_WRITE) {
		memcpy(&value, val, bytes);
		if ((offset == EFUSE_PUF_START_OFFSET ||
		     offset == EFUSE_PUF_MID_OFFSET) &&
		    value & P_USER_0_64_UPPER_MASK) {
			dev_err(dev, "Only lower 4 bytes are allowed to be programmed in P_USER_0 & P_USER_64\n");
			return -EOPNOTSUPP;
		}

		if (offset == EFUSE_PUF_END_OFFSET &&
		    (value & P_USER_127_LOWER_4_BIT_MASK)) {
			dev_err(dev, "Only MSB 28 bits are allowed to be programmed for P_USER_127\n");
			return -EOPNOTSUPP;
		}
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
	efuse->pufuserfuse = pufflag;

	zynqmp_pm_efuse_access(dma_addr, (u32 *)&ret);
	if (ret != 0) {
		if (ret == EFUSE_NOT_ENABLED) {
			dev_err(dev, "efuse access is not enabled\n");
			ret = -EOPNOTSUPP;
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

static int zynqmp_nvmem_read(void *context, unsigned int offset, void *val, size_t bytes)
{
	int ret, pufflag = 0;
	int idcode, version;

	if (offset >= EFUSE_PUF_START_OFFSET && offset <= EFUSE_PUF_END_OFFSET)
		pufflag = 1;

	switch (offset) {
	/* Soc version offset is zero */
	case SOC_VERSION_OFFSET:
		if (bytes != SOC_VER_SIZE)
			return -EOPNOTSUPP;

		ret = zynqmp_pm_get_chipid((u32 *)&idcode, (u32 *)&version);
		if (ret < 0)
			return ret;

		pr_debug("Read chipid val %x %x\n", idcode, version);
		*(int *)val = version & SILICON_REVISION_MASK;
		break;
	/* Efuse offset starts from 0xc */
	case EFUSE_START_OFFSET ... EFUSE_END_OFFSET:
	case EFUSE_PUF_START_OFFSET ... EFUSE_PUF_END_OFFSET:
		ret = zynqmp_efuse_access(context, offset, val,
					  bytes, EFUSE_READ, pufflag);
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
	int pufflag = 0;

	if (offset < EFUSE_START_OFFSET || offset > EFUSE_PUF_END_OFFSET)
		return -EOPNOTSUPP;

	if (offset >= EFUSE_PUF_START_OFFSET && offset <= EFUSE_PUF_END_OFFSET)
		pufflag = 1;

	return zynqmp_efuse_access(context, offset,
				   val, bytes, EFUSE_WRITE, pufflag);
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
