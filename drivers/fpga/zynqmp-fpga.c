/*
 * Copyright (C) 2016 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/dma-mapping.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/string.h>
#include <linux/firmware/xilinx/zynqmp/firmware.h>

/* Constant Definitions */
#define IXR_FPGA_DONE_MASK	0X00000008U
#define IXR_FPGA_ENCRYPTION_EN	0x00000008U

#define READ_DMA_SIZE		0x200
#define DUMMY_FRAMES_SIZE	0x64

static bool readback_type;
module_param(readback_type, bool, 0644);
MODULE_PARM_DESC(readback_type,
		 "readback_type 0-configuration register read "
		 "1- configuration data read (default: 0)");

/**
 * struct zynqmp_configreg - Configuration register offsets
 * @reg:	Name of the configuration register.
 * @offset:	Register offset.
 */
struct zynqmp_configreg {
	char *reg;
	u32 offset;
};

static struct zynqmp_configreg cfgreg[] = {
	{.reg = "CRC",		.offset = 0},
	{.reg = "FAR",		.offset = 1},
	{.reg = "FDRI",		.offset = 2},
	{.reg = "FDRO",		.offset = 3},
	{.reg = "CMD",		.offset = 4},
	{.reg = "CTRL0",	.offset = 5},
	{.reg = "MASK",		.offset = 6},
	{.reg = "STAT",		.offset = 7},
	{.reg = "LOUT",		.offset = 8},
	{.reg = "COR0",		.offset = 9},
	{.reg = "MFWR",		.offset = 10},
	{.reg = "CBC",		.offset = 11},
	{.reg = "IDCODE",	.offset = 12},
	{.reg = "AXSS",		.offset = 13},
	{.reg = "COR1",		.offset = 14},
	{.reg = "WBSTR",	.offset = 16},
	{.reg = "TIMER",	.offset = 17},
	{.reg = "BOOTSTS",	.offset = 22},
	{.reg = "CTRL1",	.offset = 24},
	{}
};

/**
 * struct zynqmp_fpga_priv - Private data structure
 * @dev:	Device data structure
 * @flags:	flags which is used to identify the bitfile type
 * @size:	Size of the Bit-stream used for readback
 */
struct zynqmp_fpga_priv {
	struct device *dev;
	u32 flags;
	u32 size;
};

static int zynqmp_fpga_ops_write_init(struct fpga_manager *mgr,
				      struct fpga_image_info *info,
				      const char *buf, size_t size)
{
	struct zynqmp_fpga_priv *priv;

	priv = mgr->priv;
	priv->flags = info->flags;

	return 0;
}

static int zynqmp_fpga_ops_write(struct fpga_manager *mgr,
					const char *buf, size_t size)
{
	struct zynqmp_fpga_priv *priv;
	char *kbuf;
	size_t dma_size;
	dma_addr_t dma_addr;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->fpga_load)
		return -ENXIO;

	priv = mgr->priv;
	priv->size = size;

	if (mgr->flags & IXR_FPGA_ENCRYPTION_EN)
		dma_size = size + ENCRYPTED_KEY_LEN;
	else
		dma_size = size;

	kbuf = dma_alloc_coherent(priv->dev, dma_size, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	memcpy(kbuf, buf, size);

	if (mgr->flags & IXR_FPGA_ENCRYPTION_EN)
		memcpy(kbuf + size, mgr->key, ENCRYPTED_KEY_LEN);

	wmb(); /* ensure all writes are done before initiate FW call */

	if (mgr->flags & IXR_FPGA_ENCRYPTION_EN)
		ret = eemi_ops->fpga_load(dma_addr, dma_addr + size,
							mgr->flags);
	else
		ret = eemi_ops->fpga_load(dma_addr, size,
						mgr->flags);

	dma_free_coherent(priv->dev, dma_size, kbuf, dma_addr);

	return ret;
}

static int zynqmp_fpga_ops_write_complete(struct fpga_manager *mgr,
					  struct fpga_image_info *info)
{
	return 0;
}

static enum fpga_mgr_states zynqmp_fpga_ops_state(struct fpga_manager *mgr)
{
	u32 status;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	if (!eemi_ops || !eemi_ops->fpga_get_status)
		return FPGA_MGR_STATE_UNKNOWN;

	eemi_ops->fpga_get_status(&status);
	if (status & IXR_FPGA_DONE_MASK)
		return FPGA_MGR_STATE_OPERATING;

	return FPGA_MGR_STATE_UNKNOWN;
}

static int zynqmp_fpga_read_cfgreg(struct fpga_manager *mgr,
				   struct seq_file *s)
{
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();
	int ret, val;
	unsigned int *buf;
	dma_addr_t dma_addr;
	struct zynqmp_configreg *p = cfgreg;

	buf = dma_zalloc_coherent(mgr->dev.parent, READ_DMA_SIZE,
				  &dma_addr, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	seq_puts(s, "zynqMP FPGA Configuration register contents are\n");

	while (p->reg) {
		ret = eemi_ops->fpga_read(p->offset, dma_addr, readback_type,
					  &val);
		if (ret)
			goto free_dmabuf;
		seq_printf(s, "%s --> \t %x \t\r\n", p->reg, val);
		p++;
	}

free_dmabuf:
	dma_free_coherent(mgr->dev.parent, READ_DMA_SIZE, buf,
			  dma_addr);

	return ret;
}

static int zynqmp_fpga_read_cfgdata(struct fpga_manager *mgr,
				    struct seq_file *s)
{
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();
	struct zynqmp_fpga_priv *priv;
	int ret, data_offset;
	unsigned int *buf;
	dma_addr_t dma_addr;
	size_t size;

	priv = mgr->priv;
	size = priv->size + READ_DMA_SIZE + DUMMY_FRAMES_SIZE;

	buf = dma_zalloc_coherent(mgr->dev.parent, size, &dma_addr,
				  GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	seq_puts(s, "zynqMP FPGA Configuration data contents are\n");
	ret = eemi_ops->fpga_read((priv->size + DUMMY_FRAMES_SIZE) / 4,
				  dma_addr, readback_type, &data_offset);
	if (ret)
		goto free_dmabuf;

	seq_write(s, &buf[data_offset], priv->size);

free_dmabuf:
	dma_free_coherent(mgr->dev.parent, size, buf, dma_addr);

	return ret;
}

static int zynqmp_fpga_ops_read(struct fpga_manager *mgr, struct seq_file *s)
{
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();
	int ret;

	if (!eemi_ops || !eemi_ops->fpga_read)
		return -ENXIO;

	if (readback_type)
		ret = zynqmp_fpga_read_cfgdata(mgr, s);
	else
		ret = zynqmp_fpga_read_cfgreg(mgr, s);

	return ret;
}

static const struct fpga_manager_ops zynqmp_fpga_ops = {
	.state = zynqmp_fpga_ops_state,
	.write_init = zynqmp_fpga_ops_write_init,
	.write = zynqmp_fpga_ops_write,
	.write_complete = zynqmp_fpga_ops_write_complete,
	.read = zynqmp_fpga_ops_read,
};

static int zynqmp_fpga_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zynqmp_fpga_priv *priv;
	int err, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(44));
	if (ret < 0)
		dev_err(dev, "no usable DMA configuration");

	err = fpga_mgr_register(dev, "Xilinx ZynqMP FPGA Manager",
				&zynqmp_fpga_ops, priv);
	if (err) {
		dev_err(dev, "unable to register FPGA manager");
		return err;
	}

	return 0;
}

static int zynqmp_fpga_remove(struct platform_device *pdev)
{
	fpga_mgr_unregister(&pdev->dev);

	return 0;
}

static const struct of_device_id zynqmp_fpga_of_match[] = {
	{ .compatible = "xlnx,zynqmp-pcap-fpga", },
	{},
};

MODULE_DEVICE_TABLE(of, zynqmp_fpga_of_match);

static struct platform_driver zynqmp_fpga_driver = {
	.probe = zynqmp_fpga_probe,
	.remove = zynqmp_fpga_remove,
	.driver = {
		.name = "zynqmp_fpga_manager",
		.of_match_table = of_match_ptr(zynqmp_fpga_of_match),
	},
};

module_platform_driver(zynqmp_fpga_driver);

MODULE_AUTHOR("Nava kishore Manne <navam@xilinx.com>");
MODULE_DESCRIPTION("Xilinx ZynqMp FPGA Manager");
MODULE_LICENSE("GPL");
