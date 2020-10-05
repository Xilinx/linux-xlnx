// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Xilinx, Inc.
 */

#include <linux/dma-mapping.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/string.h>
#include <linux/firmware/xlnx-zynqmp.h>

/* Constant Definitions */
#define PDI_SOURCE_TYPE	0xF

/**
 * struct versal_fpga_priv - Private data structure
 * @dev:	Device data structure
 * @flags:	flags which is used to identify the PL Image type
 */
struct versal_fpga_priv {
	struct device *dev;
	u32 flags;
};

static int versal_fpga_ops_write_init(struct fpga_manager *mgr,
				      struct fpga_image_info *info,
				      const char *buf, size_t size)
{
	struct versal_fpga_priv *priv;

	priv = mgr->priv;
	priv->flags = info->flags;

	return 0;
}

static int versal_fpga_ops_write_sg(struct fpga_manager *mgr,
				    struct sg_table *sgt)
{
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();
	dma_addr_t dma_addr;
	int ret;

	if (IS_ERR_OR_NULL(eemi_ops) || !eemi_ops->fpga_load)
		return -ENXIO;

	dma_addr = sg_dma_address(sgt->sgl);
	ret = eemi_ops->pdi_load(PDI_SOURCE_TYPE, dma_addr);

	return ret;
}

static int versal_fpga_ops_write(struct fpga_manager *mgr,
				 const char *buf, size_t size)
{
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();
	struct versal_fpga_priv *priv;
	dma_addr_t dma_addr = 0;
	char *kbuf;
	int ret;

	if (IS_ERR(eemi_ops) || !eemi_ops->pdi_load)
		return -ENXIO;

	priv = mgr->priv;

	kbuf = dma_alloc_coherent(priv->dev, size, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	memcpy(kbuf, buf, size);

	wmb(); /* ensure all writes are done before initiate FW call */

	ret = eemi_ops->pdi_load(PDI_SOURCE_TYPE, dma_addr);

	dma_free_coherent(priv->dev, size, kbuf, dma_addr);

	return ret;
}

static int versal_fpga_ops_write_complete(struct fpga_manager *mgr,
					  struct fpga_image_info *info)
{
	return 0;
}

static enum fpga_mgr_states versal_fpga_ops_state(struct fpga_manager *mgr)
{
	return FPGA_MGR_STATE_OPERATING;
}

static const struct fpga_manager_ops versal_fpga_ops = {
	.state = versal_fpga_ops_state,
	.write_init = versal_fpga_ops_write_init,
	.write = versal_fpga_ops_write,
	.write_sg = versal_fpga_ops_write_sg,
	.write_complete = versal_fpga_ops_write_complete,
};

static int versal_fpga_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct versal_fpga_priv *priv;
	struct fpga_manager *mgr;
	int err, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret < 0) {
		dev_err(dev, "no usable DMA configuration");
		return ret;
	}

	mgr = devm_fpga_mgr_create(dev, "Xilinx Versal FPGA Manager",
				   &versal_fpga_ops, priv);
	if (!mgr)
		return -ENOMEM;

	platform_set_drvdata(pdev, mgr);

	err = fpga_mgr_register(mgr);
	if (err) {
		dev_err(dev, "unable to register FPGA manager");
		fpga_mgr_free(mgr);
		return err;
	}

	return 0;
}

static int versal_fpga_remove(struct platform_device *pdev)
{
	struct fpga_manager *mgr = platform_get_drvdata(pdev);

	fpga_mgr_unregister(mgr);
	fpga_mgr_free(mgr);

	return 0;
}

static const struct of_device_id versal_fpga_of_match[] = {
	{ .compatible = "xlnx,versal-fpga", },
	{},
};

MODULE_DEVICE_TABLE(of, versal_fpga_of_match);

static struct platform_driver versal_fpga_driver = {
	.probe = versal_fpga_probe,
	.remove = versal_fpga_remove,
	.driver = {
		.name = "versal_fpga_manager",
		.of_match_table = of_match_ptr(versal_fpga_of_match),
	},
};

module_platform_driver(versal_fpga_driver);

MODULE_AUTHOR("Nava kishore Manne <nava.manne@xilinx.com>");
MODULE_AUTHOR("Appana Durga Kedareswara rao <appanad.durga.rao@xilinx.com>");
MODULE_DESCRIPTION("Xilinx Versal FPGA Manager");
MODULE_LICENSE("GPL");
