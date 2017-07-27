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

#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/string.h>
#include <linux/soc/xilinx/zynqmp/firmware.h>

/* Constant Definitions */
#define IXR_FPGA_DONE_MASK	0X00000008U
#define IXR_FPGA_AUTHENTICATIN	0x00000004U
#define IXR_FPGA_ENCRYPTION_USRKEY_EN	0x00000008U
#define IXR_FPGA_ENCRYPTION_DEVKEY_EN	0x00000010U

struct zynqmp_fpga_priv {
	struct device *dev;
	u32 flags;
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
	size_t dma_size = size;
	dma_addr_t dma_addr;
	u32 transfer_length;
	int ret;

	priv = mgr->priv;

	if (mgr->flags & IXR_FPGA_AUTHENTICATIN)
		dma_size = dma_size + SIGNATURE_LEN + PUBLIC_KEY_LEN;
	if (mgr->flags & IXR_FPGA_ENCRYPTION_DEVKEY_EN)
		dma_size = dma_size + ENCRYPTED_IV_LEN;
	else if (mgr->flags & IXR_FPGA_ENCRYPTION_USRKEY_EN)
		dma_size = dma_size + ENCRYPTED_KEY_LEN + ENCRYPTED_IV_LEN;

	kbuf = dma_alloc_coherent(priv->dev, dma_size, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	memcpy(kbuf, buf, size);

	if (mgr->flags & IXR_FPGA_AUTHENTICATIN) {
		memcpy(kbuf + size, mgr->signature, SIGNATURE_LEN);
		memcpy(kbuf + size + SIGNATURE_LEN, mgr->pubkey,
						PUBLIC_KEY_LEN);
	}
	if (mgr->flags & IXR_FPGA_ENCRYPTION_DEVKEY_EN)
		memcpy(kbuf + size, mgr->iv, ENCRYPTED_IV_LEN);
	else if (mgr->flags & IXR_FPGA_ENCRYPTION_USRKEY_EN) {
		memcpy(kbuf + size, mgr->key, ENCRYPTED_KEY_LEN);
		memcpy(kbuf + size + ENCRYPTED_KEY_LEN, mgr->iv,
						ENCRYPTED_IV_LEN);
	}

	__flush_cache_user_range((unsigned long)kbuf,
				 (unsigned long)kbuf + dma_size);

	/**
	 * Translate size from bytes to number of 32bit words that
	 * the DMA should write to the PCAP interface
	 */
	if (size & 3)
		transfer_length = (size >> 2) + 1;
	else
		transfer_length = size >> 2;

	ret = zynqmp_pm_fpga_load(dma_addr, transfer_length, mgr->flags);

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

	zynqmp_pm_fpga_get_status(&status);
	if (status & IXR_FPGA_DONE_MASK)
		return FPGA_MGR_STATE_OPERATING;

	return FPGA_MGR_STATE_UNKNOWN;
}

static const struct fpga_manager_ops zynqmp_fpga_ops = {
	.state = zynqmp_fpga_ops_state,
	.write_init = zynqmp_fpga_ops_write_init,
	.write = zynqmp_fpga_ops_write,
	.write_complete = zynqmp_fpga_ops_write_complete,
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
