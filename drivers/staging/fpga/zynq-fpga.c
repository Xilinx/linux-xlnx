/*
 * Copyright (c) 2011-2015 Xilinx Inc.
 * Copyright (c) 2015, National Instruments Corp.
 *
 * FPGA Manager Driver for Xilinx Zynq, heavily based on xdevcfg driver
 * in their vendor tree.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/string.h>

/* Offsets into SLCR regmap */
#define SLCR_FPGA_RST_CTRL_OFFSET	0x240 /* FPGA Software Reset Control */
#define SLCR_LVL_SHFTR_EN_OFFSET	0x900 /* Level Shifters Enable */

/* Constant Definitions */
#define CTRL_OFFSET		0x00 /* Control Register */
#define LOCK_OFFSET		0x04 /* Lock Register */
#define INT_STS_OFFSET		0x0c /* Interrupt Status Register */
#define INT_MASK_OFFSET		0x10 /* Interrupt Mask Register */
#define STATUS_OFFSET		0x14 /* Status Register */
#define DMA_SRC_ADDR_OFFSET	0x18 /* DMA Source Address Register */
#define DMA_DEST_ADDR_OFFSET	0x1c /* DMA Destination Address Reg */
#define DMA_SRC_LEN_OFFSET	0x20 /* DMA Source Transfer Length */
#define DMA_DEST_LEN_OFFSET	0x24 /* DMA Destination Transfer */
#define UNLOCK_OFFSET		0x34 /* Unlock Register */
#define MCTRL_OFFSET		0x80 /* Misc. Control Register */

/* Control Register Bit definitions */
#define CTRL_PCFG_PROG_B_MASK	BIT(30) /* Program signal to reset FPGA */
#define CTRL_PCAP_PR_MASK	BIT(27) /* Enable PCAP for PR */
#define CTRL_PCAP_MODE_MASK	BIT(26) /* Enable PCAP */

/* Miscellaneous Control Register bit definitions */
#define MCTRL_PCAP_LPBK_MASK	BIT(4) /* Internal PCAP loopback */

/* Status register bit definitions */
#define STATUS_PCFG_INIT_MASK	BIT(4) /* FPGA init status */

/* Interrupt Status/Mask Register Bit definitions */
#define IXR_DMA_DONE_MASK	BIT(13) /* DMA command done */
#define IXR_D_P_DONE_MASK	BIT(12) /* DMA and PCAP cmd done */
#define IXR_PCFG_DONE_MASK	BIT(2)  /* FPGA programmed */
#define IXR_ERROR_FLAGS_MASK	0x00F0F860
#define IXR_ALL_MASK		0xF8F7F87F

/* Miscellaneous constant values */
#define DMA_INVALID_ADDRESS	GENMASK(31, 0)  /* Invalid DMA address */
#define UNLOCK_MASK		0x757bdf0d /* Used to unlock the device */

/* Masks for controlling stuff in SLCR */
#define LVL_SHFTR_DISABLE_ALL_MASK	0x0 /* Disable all Level shifters */
#define LVL_SHFTR_ENABLE_PS_TO_PL	0xa /* Enable all Level shifters */
#define LVL_SHFTR_ENABLE_PL_TO_PS	0xf /* Enable all Level shifters */
#define FPGA_RST_ALL_MASK		0xf /* Enable global resets */
#define FPGA_RST_NONE_MASK		0x0 /* Disable global resets */

struct zynq_fpga_priv {
	struct device *dev;
	int irq;
	struct clk *clk;

	void __iomem *io_base;
	struct regmap *slcr;

	/* this protects the error flag */
	spinlock_t lock;
	bool error;

	struct completion dma_done;
};

static inline void zynq_fpga_write(struct zynq_fpga_priv *priv, u32 offset,
				   u32 val)
{
	writel(val, priv->io_base + offset);
}

static inline u32 zynq_fpga_read(const struct zynq_fpga_priv *priv,
				 u32 offset)
{
	return readl(priv->io_base + offset);
}

static void zynq_fpga_mask_irqs(struct zynq_fpga_priv *priv)
{
	u32 intr_mask;

	intr_mask = zynq_fpga_read(priv, INT_MASK_OFFSET);
	zynq_fpga_write(priv, INT_MASK_OFFSET,
			intr_mask | IXR_DMA_DONE_MASK | IXR_ERROR_FLAGS_MASK);
}

static void zynq_fpga_unmask_irqs(struct zynq_fpga_priv *priv)
{
	u32 intr_mask;

	intr_mask = zynq_fpga_read(priv, INT_MASK_OFFSET);
	zynq_fpga_write(priv, INT_MASK_OFFSET,
			intr_mask
			& ~(IXR_D_P_DONE_MASK | IXR_ERROR_FLAGS_MASK));
}

static irqreturn_t zynq_fpga_isr(int irq, void *data)
{
	u32 intr_status;
	struct zynq_fpga_priv *priv = data;

	spin_lock(&priv->lock);
	intr_status = zynq_fpga_read(priv, INT_STS_OFFSET);

	if (!intr_status) {
		spin_unlock(&priv->lock);
		return IRQ_NONE;
	}

	zynq_fpga_write(priv, INT_STS_OFFSET, intr_status);

	if ((intr_status & IXR_D_P_DONE_MASK) == IXR_D_P_DONE_MASK)
		complete(&priv->dma_done);

	if ((intr_status & IXR_ERROR_FLAGS_MASK) ==
			IXR_ERROR_FLAGS_MASK) {
		priv->error = true;
		dev_err(priv->dev, "%s dma error\n", __func__);
	}
	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
}

static int zynq_fpga_ops_reset(struct fpga_manager *mgr)
{
	struct zynq_fpga_priv *priv;
	int err;

	priv = mgr->priv;

	err = clk_enable(priv->clk);
	if (err)
		return err;

	/* assert fpga top level output resets */
	regmap_write(priv->slcr, SLCR_FPGA_RST_CTRL_OFFSET, FPGA_RST_ALL_MASK);

	/* disable all level shifters */
	regmap_write(priv->slcr, SLCR_LVL_SHFTR_EN_OFFSET,
		     LVL_SHFTR_DISABLE_ALL_MASK);

	/* enable output level shifters */
	regmap_write(priv->slcr, SLCR_LVL_SHFTR_EN_OFFSET,
		     LVL_SHFTR_ENABLE_PS_TO_PL);

	clk_disable(priv->clk);

	return 0;
}

static int zynq_fpga_ops_write_init(struct fpga_manager *mgr)
{
	struct zynq_fpga_priv *priv;
	u32 ctrl;
	int err;

	priv = mgr->priv;

	err = clk_enable(priv->clk);
	if (err)
		return err;

	/* create a rising edge on PCFG_INIT. PCFG_INIT follows PCFG_PROG_B,
	 * so we need to poll it after setting PCFG_PROG_B to make sure
	 * the rising edge actually happens */

	ctrl = zynq_fpga_read(priv, CTRL_OFFSET);
	ctrl |= CTRL_PCFG_PROG_B_MASK;

	zynq_fpga_write(priv, CTRL_OFFSET, ctrl);

	while (!(zynq_fpga_read(priv, STATUS_OFFSET) & STATUS_PCFG_INIT_MASK))
		;

	ctrl = zynq_fpga_read(priv, CTRL_OFFSET);
	ctrl &= ~CTRL_PCFG_PROG_B_MASK;

	zynq_fpga_write(priv, CTRL_OFFSET, ctrl);

	while ((zynq_fpga_read(priv, STATUS_OFFSET) & STATUS_PCFG_INIT_MASK))
		;

	ctrl = zynq_fpga_read(priv, CTRL_OFFSET);
	ctrl |= CTRL_PCFG_PROG_B_MASK;

	zynq_fpga_write(priv, CTRL_OFFSET, ctrl);

	while (!(zynq_fpga_read(priv, STATUS_OFFSET) & STATUS_PCFG_INIT_MASK))
		;

	/* TODO: Needed ? */
	zynq_fpga_write(priv, INT_STS_OFFSET, IXR_PCFG_DONE_MASK);

	clk_disable(priv->clk);

	return 0;
}

static int zynq_fpga_ops_write(struct fpga_manager *mgr,
			       const char *buf, size_t count)
{
	struct zynq_fpga_priv *priv;
	int err;
	char *kbuf;
	size_t i, in_count;
	dma_addr_t dma_addr;
	u32 transfer_length = 0;
	bool endian_swap = false;

	in_count = count;
	priv = mgr->priv;

	kbuf = dma_alloc_coherent(priv->dev, count, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	memcpy(kbuf, buf, count);

	/* look for the sync word */
	for (i = 0; i < count - 4; i++) {
		if (memcmp(kbuf + i, "\x66\x55\x99\xAA", 4) == 0) {
			dev_dbg(priv->dev, "Found normal sync word\n");
			endian_swap = false;
			break;
		}
		if (memcmp(kbuf + i, "\xAA\x99\x55\x66", 4) == 0) {
			dev_dbg(priv->dev, "Found swapped sync word\n");
			endian_swap = true;
			break;
		}
	}

	/* remove the header, align the data on word boundary */
	if (i != count - 4) {
		count -= i;
		memmove(kbuf, kbuf + i, count);
	}

	/* fixup endianness of the data */
	if (endian_swap) {
		for (i = 0; i < count; i += 4) {
			u32 *p = (u32 *)&kbuf[i];
			*p = swab32(*p);
		}
	}

	/* enable clock */
	err = clk_enable(priv->clk);
	if (err)
		goto out_free;

	zynq_fpga_write(priv, INT_STS_OFFSET, IXR_ALL_MASK);

	/* enable DMA and error IRQs */
	zynq_fpga_unmask_irqs(priv);

	priv->error = false;

	/* the +1 in the src addr is used to hold off on DMA_DONE IRQ */
	/* until both AXI and PCAP are done */
	if (count < PAGE_SIZE)
		zynq_fpga_write(priv, DMA_SRC_ADDR_OFFSET, (u32)(dma_addr + 1));
	else
		zynq_fpga_write(priv, DMA_SRC_ADDR_OFFSET, (u32)(dma_addr));

	zynq_fpga_write(priv, DMA_DEST_ADDR_OFFSET, (u32)DMA_INVALID_ADDRESS);

	/* convert #bytes to #words */
	transfer_length = (count + 3) / 4;

	zynq_fpga_write(priv, DMA_SRC_LEN_OFFSET, transfer_length);
	zynq_fpga_write(priv, DMA_DEST_LEN_OFFSET, 0);

	wait_for_completion_interruptible(&priv->dma_done);
	if (priv->error)
		dev_err(priv->dev, "Error configuring FPGA.\n");

	/* disable DMA and error IRQs */
	zynq_fpga_mask_irqs(priv);

	if (priv->error)
		err = -EFAULT;
	else
		err = 0;

	clk_disable(priv->clk);

out_free:
	dma_free_coherent(priv->dev, in_count, kbuf, dma_addr);

	return err;
}

static int zynq_fpga_ops_write_complete(struct fpga_manager *mgr)
{
	struct zynq_fpga_priv *priv = mgr->priv;

	/* enable all level shifters */
	regmap_write(priv->slcr, SLCR_LVL_SHFTR_EN_OFFSET,
		     LVL_SHFTR_ENABLE_PL_TO_PS);

	/* deassert AXI interface resets */
	regmap_write(priv->slcr, SLCR_FPGA_RST_CTRL_OFFSET, FPGA_RST_NONE_MASK);

	return 0;
}

static enum fpga_mgr_states zynq_fpga_ops_state(struct fpga_manager *mgr)
{
	int err;
	u32 intr_status;
	struct zynq_fpga_priv *priv;

	priv = mgr->priv;

	err = clk_enable(priv->clk);
	if (err)
		return FPGA_MGR_STATE_UNKNOWN;

	intr_status = zynq_fpga_read(priv, INT_STS_OFFSET);
	clk_disable(priv->clk);

	if (intr_status & IXR_PCFG_DONE_MASK)
		return FPGA_MGR_STATE_OPERATING;

	return FPGA_MGR_STATE_UNKNOWN;
}

static int zynq_fpga_suspend(struct fpga_manager *mgr)
{
	return 0;
}

static int zynq_fpga_resume(struct fpga_manager *mgr)
{
	return 0;
}

static const struct fpga_manager_ops zynq_fpga_ops = {
	.reset = zynq_fpga_ops_reset,
	.state = zynq_fpga_ops_state,
	.write_init = zynq_fpga_ops_write_init,
	.write = zynq_fpga_ops_write,
	.write_complete = zynq_fpga_ops_write_complete,
	.suspend = zynq_fpga_suspend,
	.resume = zynq_fpga_resume,
};

static int zynq_fpga_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zynq_fpga_priv *priv;
	struct resource *res;
	u32 ctrl_reg;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	priv->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->io_base))
		return PTR_ERR(priv->io_base);

	priv->slcr = syscon_regmap_lookup_by_phandle(dev->of_node,
		"syscon");
	if (IS_ERR(priv->slcr)) {
		dev_err(dev, "unable to get zynq-slcr regmap");
		return PTR_ERR(priv->slcr);
	}

	init_completion(&priv->dma_done);

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		dev_err(dev, "No IRQ available");
		return priv->irq;
	}

	ret = devm_request_irq(dev, priv->irq, zynq_fpga_isr, 0,
			       dev_name(dev), priv);
	if (IS_ERR_VALUE(ret))
		return ret;

	priv->clk = devm_clk_get(dev, "ref_clk");
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "input clock not found\n");
		return PTR_ERR(priv->clk);
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "unable to enable clock\n");
		return ret;
	}

	/* unlock the device */
	zynq_fpga_write(priv, UNLOCK_OFFSET, UNLOCK_MASK);

	/* set configuration register with following options:
	* - reset FPGA
	* - enable PCAP interface for partial reconfig
	* - set throughput for maximum speed
	* - set CPU in user mode
	*/
	ctrl_reg = zynq_fpga_read(priv, CTRL_OFFSET);
	zynq_fpga_write(priv, CTRL_OFFSET, (CTRL_PCFG_PROG_B_MASK |
		CTRL_PCAP_PR_MASK | CTRL_PCAP_MODE_MASK | ctrl_reg));

	/* ensure internal PCAP loopback is disabled */
	ctrl_reg = zynq_fpga_read(priv, MCTRL_OFFSET);
	zynq_fpga_write(priv, MCTRL_OFFSET, (~MCTRL_PCAP_LPBK_MASK & ctrl_reg));

	ret = fpga_mgr_register(dev, "Xilinx Zynq FPGA Manager",
				&zynq_fpga_ops, priv);
	if (ret) {
		dev_err(dev, "unable to register FPGA manager");
		clk_disable_unprepare(priv->clk);
		return ret;
	}
	return 0;
}

static int zynq_fpga_remove(struct platform_device *pdev)
{
	fpga_mgr_remove(pdev);

	return 0;
}

static const struct of_device_id zynq_fpga_of_match[] = {
	{ .compatible = "xlnx,zynq-devcfg-1.0", },
	{},
};

MODULE_DEVICE_TABLE(of, zynq_fpga_of_match);

static struct platform_driver zynq_fpga_driver = {
	.probe = zynq_fpga_probe,
	.remove = zynq_fpga_remove,
	.driver = {
		.name = "zynq_fpga_manager",
		.of_match_table = of_match_ptr(zynq_fpga_of_match),
	},
};

module_platform_driver(zynq_fpga_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Moritz Fischer <moritz.fischer@ettus.com>");
MODULE_AUTHOR("Michal Simek <michal.simek@xilinx.com>");
MODULE_DESCRIPTION("Xilinx Zynq FPGA Manager");
MODULE_ALIAS("fpga:zynq");
