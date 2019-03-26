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

/**
 * struct versal_fpga_priv - Private data structure
 * @dev:	Device data structure
 * @source:	Source of the PDI Image DDR, OCM etc...
 * @flags:	flags which is used to identify the PL Image type
 * @source_attr: source sysfs attribute
 */
struct versal_fpga_priv {
	struct device *dev;
	u32 source;
	u32 flags;
	struct device_attribute *source_attr;
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

static int versal_fpga_ops_write(struct fpga_manager *mgr,
				 const char *buf, size_t size)
{
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();
	struct versal_fpga_priv *priv;
	dma_addr_t dma_addr;
	char *kbuf;
	int ret;

	if (!eemi_ops || !eemi_ops->pdi_load)
		return -ENXIO;

	priv = mgr->priv;

	kbuf = dma_alloc_coherent(priv->dev, size, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	memcpy(kbuf, buf, size);

	wmb(); /* ensure all writes are done before initiate FW call */

	ret = eemi_ops->pdi_load(priv->source, dma_addr);

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
	.write_complete = versal_fpga_ops_write_complete,
};

static ssize_t source_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);
	struct versal_fpga_priv *priv = mgr->priv;
	int ret;

	ret = kstrtou32(buf, 16, &priv->source);
	if (ret)
		return ret;

	return count;
}

static struct device_attribute *
versal_fpga_create_sysfs_entry(struct device *dev, char *name, int mode)
{
	struct device_attribute *attrs;
	char *name_copy;

	attrs = devm_kmalloc(dev, sizeof(struct device_attribute), GFP_KERNEL);
	if (!attrs)
		return NULL;

	name_copy = devm_kstrdup(dev, name, GFP_KERNEL);
	if (!name_copy)
		return NULL;

	attrs->attr.name = name_copy;
	attrs->attr.mode = mode;
	attrs->store = source_store;
	sysfs_attr_init(&attrs->attr);

	return attrs;
}

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
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(44));
	if (ret < 0) {
		dev_err(dev, "no usable DMA configuration");
		return ret;
	}

	mgr = fpga_mgr_create(dev, "Xilinx Versal FPGA Manager",
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

	priv->source_attr = versal_fpga_create_sysfs_entry(&mgr->dev, "source",
							   0200);
	if (!priv->source_attr) {
		dev_err(dev, "unable to create source sysfs attribute");
		fpga_mgr_unregister(mgr);
		fpga_mgr_free(mgr);
		return -ENOMEM;
	}

	return device_create_file(&mgr->dev, priv->source_attr);
}

static int versal_fpga_remove(struct platform_device *pdev)
{
	struct fpga_manager *mgr = platform_get_drvdata(pdev);
	struct versal_fpga_priv *priv = mgr->priv;

	device_remove_file(&mgr->dev, priv->source_attr);
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
