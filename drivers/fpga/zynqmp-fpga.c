// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 - 2019 Xilinx, Inc.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/firmware/xlnx-zynqmp.h>

/* Constant Definitions */
#define IXR_FPGA_DONE_MASK	0X00000008U
#define IXR_FPGA_ENCRYPTION_EN	0x00000008U

#define READ_DMA_SIZE		0x200
#define DUMMY_FRAMES_SIZE	0x64
#define PCAP_READ_CLKFREQ	25000000

static bool readback_type;
module_param(readback_type, bool, 0644);
MODULE_PARM_DESC(readback_type,
		 "readback_type 0-configuration register read "
		 "1- configuration data read (default: 0)");

static const struct zynqmp_eemi_ops *eemi_ops;

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
 * @lock:	Mutex lock for device
 * @clk:	Clock resource for pcap controller
 * @flags:	flags which is used to identify the bitfile type
 * @size:	Size of the Bitstream used for readback
 */
struct zynqmp_fpga_priv {
	struct device *dev;
	struct mutex lock;
	struct clk *clk;
	char *key;
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
	priv->key = info->key;

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

	if (!eemi_ops->fpga_load)
		return -ENXIO;

	priv = mgr->priv;
	priv->size = size;

	if (!mutex_trylock(&priv->lock))
		return -EBUSY;

	ret = clk_enable(priv->clk);
	if (ret)
		goto err_unlock;

	if (priv->flags & IXR_FPGA_ENCRYPTION_EN)
		dma_size = size + ENCRYPTED_KEY_LEN;
	else
		dma_size = size;

	kbuf = dma_alloc_coherent(priv->dev, dma_size, &dma_addr, GFP_KERNEL);
	if (!kbuf) {
		ret = -ENOMEM;
		goto disable_clk;
	}

	memcpy(kbuf, buf, size);

	if (priv->flags & IXR_FPGA_ENCRYPTION_EN)
		memcpy(kbuf + size, priv->key, ENCRYPTED_KEY_LEN);

	wmb(); /* ensure all writes are done before initiate FW call */

	if (priv->flags & IXR_FPGA_ENCRYPTION_EN)
		ret = eemi_ops->fpga_load(dma_addr, dma_addr + size,
					  priv->flags);
	else
		ret = eemi_ops->fpga_load(dma_addr, size, priv->flags);

	dma_free_coherent(priv->dev, dma_size, kbuf, dma_addr);
disable_clk:
	clk_disable(priv->clk);
err_unlock:
	mutex_unlock(&priv->lock);
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

	if (!eemi_ops->fpga_get_status)
		return FPGA_MGR_STATE_UNKNOWN;

	eemi_ops->fpga_get_status(&status);
	if (status & IXR_FPGA_DONE_MASK)
		return FPGA_MGR_STATE_OPERATING;

	return FPGA_MGR_STATE_UNKNOWN;
}

static int zynqmp_fpga_read_cfgreg(struct fpga_manager *mgr,
				   struct seq_file *s)
{
	struct zynqmp_fpga_priv *priv = mgr->priv;
	int ret, val;
	unsigned int *buf;
	dma_addr_t dma_addr;
	struct zynqmp_configreg *p = cfgreg;

	ret = clk_enable(priv->clk);
	if (ret)
		return ret;

	buf = dma_zalloc_coherent(mgr->dev.parent, READ_DMA_SIZE,
				  &dma_addr, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto disable_clk;
	}

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
disable_clk:
	clk_disable(priv->clk);

	return ret;
}

static int zynqmp_fpga_read_cfgdata(struct fpga_manager *mgr,
				    struct seq_file *s)
{
	struct zynqmp_fpga_priv *priv;
	int ret, data_offset;
	unsigned int *buf;
	dma_addr_t dma_addr;
	size_t size;
	int clk_rate;

	priv = mgr->priv;
	size = priv->size + READ_DMA_SIZE + DUMMY_FRAMES_SIZE;

	/*
	 * There is no h/w flow control for pcap read
	 * to prevent the FIFO from over flowing, reduce
	 * the PCAP operating frequency.
	 */
	clk_rate = clk_get_rate(priv->clk);
	clk_unprepare(priv->clk);
	ret = clk_set_rate(priv->clk, PCAP_READ_CLKFREQ);
	if (ret) {
		dev_err(&mgr->dev, "Unable to reduce the PCAP freq %d\n", ret);
		goto prepare_clk;
	}
	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(&mgr->dev, "Cannot enable clock.\n");
		goto restore_pcap_clk;
	}

	buf = dma_zalloc_coherent(mgr->dev.parent, size, &dma_addr,
				  GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto disable_clk;
	}

	seq_puts(s, "zynqMP FPGA Configuration data contents are\n");
	ret = eemi_ops->fpga_read((priv->size + DUMMY_FRAMES_SIZE) / 4,
				  dma_addr, readback_type, &data_offset);
	if (ret)
		goto free_dmabuf;

	seq_write(s, &buf[data_offset], priv->size);

free_dmabuf:
	dma_free_coherent(mgr->dev.parent, size, buf, dma_addr);
disable_clk:
	clk_disable_unprepare(priv->clk);
restore_pcap_clk:
	clk_set_rate(priv->clk, clk_rate);
prepare_clk:
	clk_prepare(priv->clk);

	return ret;
}

static int zynqmp_fpga_ops_read(struct fpga_manager *mgr, struct seq_file *s)
{
	struct zynqmp_fpga_priv *priv = mgr->priv;
	int ret;

	if (!eemi_ops->fpga_read)
		return -ENXIO;

	if (!mutex_trylock(&priv->lock))
		return -EBUSY;

	if (readback_type)
		ret = zynqmp_fpga_read_cfgdata(mgr, s);
	else
		ret = zynqmp_fpga_read_cfgreg(mgr, s);

	mutex_unlock(&priv->lock);
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
	struct fpga_manager *mgr;

	eemi_ops = zynqmp_pm_get_eemi_ops();
	if (IS_ERR(eemi_ops))
		return PTR_ERR(eemi_ops);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	mutex_init(&priv->lock);
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(44));
	if (ret < 0)
		dev_err(dev, "no usable DMA configuration");

	mgr = fpga_mgr_create(dev, "Xilinx ZynqMP FPGA Manager",
			      &zynqmp_fpga_ops, priv);
	if (!mgr)
		return -ENOMEM;

	platform_set_drvdata(pdev, mgr);

	priv->clk = devm_clk_get(dev, "ref_clk");
	if (IS_ERR(priv->clk)) {
		ret = PTR_ERR(priv->clk);
		dev_err(dev, "failed to to get pcp ref_clk (%d)\n", ret);
		return ret;
	}

	ret = clk_prepare(priv->clk);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		return ret;
	}

	err = fpga_mgr_register(mgr);
	if (err) {
		dev_err(dev, "unable to register FPGA manager");
		fpga_mgr_free(mgr);
		clk_unprepare(priv->clk);
		return err;
	}

	return 0;
}

static int zynqmp_fpga_remove(struct platform_device *pdev)
{
	struct zynqmp_fpga_priv *priv;
	struct fpga_manager *mgr;

	mgr = platform_get_drvdata(pdev);
	priv = mgr->priv;

	fpga_mgr_unregister(mgr);
	clk_unprepare(priv->clk);

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
