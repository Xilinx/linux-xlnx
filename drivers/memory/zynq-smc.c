/*
 * Xilinx Zynq SMC Driver
 *
 * Copyright (C) 2012 - 2014 Xilinx, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Currently only a single SMC instance is supported.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/memory/zynq-smc.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Register definitions */
#define ZYNQ_SMC_MEMC_STATUS_OFFS	0	/* Controller status reg, RO */
#define ZYNQ_SMC_CFG_CLR_OFFS		0xC	/* Clear config reg, WO */
#define ZYNQ_SMC_DIRECT_CMD_OFFS	0x10	/* Direct command reg, WO */
#define ZYNQ_SMC_SET_CYCLES_OFFS	0x14	/* Set cycles register, WO */
#define ZYNQ_SMC_SET_OPMODE_OFFS	0x18	/* Set opmode register, WO */
#define ZYNQ_SMC_ECC_STATUS_OFFS	0x400	/* ECC status register */
#define ZYNQ_SMC_ECC_MEMCFG_OFFS	0x404	/* ECC mem config reg */
#define ZYNQ_SMC_ECC_MEMCMD1_OFFS	0x408	/* ECC mem cmd1 reg */
#define ZYNQ_SMC_ECC_MEMCMD2_OFFS	0x40C	/* ECC mem cmd2 reg */
#define ZYNQ_SMC_ECC_VALUE0_OFFS	0x418	/* ECC value 0 reg */

#define ZYNQ_SMC_CFG_CLR_INT_1	0x10
#define ZYNQ_SMC_ECC_STATUS_BUSY	(1 << 6)
#define ZYNQ_SMC_DC_UPT_NAND_REGS	((4 << 23) |	/* CS: NAND chip */ \
				 (2 << 21))	/* UpdateRegs operation */

#define ZYNQ_NAND_ECC_CMD1	((0x80)       |	/* Write command */ \
				 (0 << 8)     |	/* Read command */ \
				 (0x30 << 16) |	/* Read End command */ \
				 (1 << 24))	/* Read End command calid */

#define ZYNQ_NAND_ECC_CMD2	((0x85)	      |	/* Write col change cmd */ \
				 (5 << 8)     |	/* Read col change cmd */ \
				 (0xE0 << 16) |	/* Read col change end cmd */ \
				 (1 << 24)) /* Read col change end cmd valid */
/**
 * struct zynq_smc_data - Private smc driver structure
 * @devclk:		Pointer to the peripheral clock
 * @aperclk:		Pointer to the APER clock
 * @clk_rate_change_nb:	Notifier block for clock frequency change callback
 */
struct zynq_smc_data {
	struct clk		*devclk;
	struct clk		*aperclk;
	struct notifier_block	clk_rate_change_nb;
};

/* SMC virtual register base */
static void __iomem *zynq_smc_base;
static DEFINE_SPINLOCK(zynq_smc_lock);

/**
 * zynq_smc_set_buswidth - Set memory buswidth
 * @bw:	Memory buswidth (8 | 16)
 * Return: 0 on success or negative errno.
 *
 * Must be called with zynq_smc_lock held.
 */
static int zynq_smc_set_buswidth(unsigned int bw)
{
	u32 reg;

	if (bw != 8 && bw != 16)
		return -EINVAL;

	reg = readl(zynq_smc_base + ZYNQ_SMC_SET_OPMODE_OFFS);
	reg &= ~3;
	if (bw == 16)
		reg |= 1;
	writel(reg, zynq_smc_base + ZYNQ_SMC_SET_OPMODE_OFFS);

	return 0;
}

/**
 * zynq_smc_set_cycles - Set memory timing parameters
 * @t0:	t_rc		read cycle time
 * @t1:	t_wc		write cycle time
 * @t2:	t_rea/t_ceoe	output enable assertion delay
 * @t3:	t_wp		write enable deassertion delay
 * @t4:	t_clr/t_pc	page cycle time
 * @t5:	t_ar/t_ta	ID read time/turnaround time
 * @t6:	t_rr		busy to RE timing
 *
 * Sets NAND chip specific timing parameters.
 *
 * Must be called with zynq_smc_lock held.
 */
static void zynq_smc_set_cycles(u32 t0, u32 t1, u32 t2, u32 t3, u32
			      t4, u32 t5, u32 t6)
{
	t0 &= 0xf;
	t1 = (t1 & 0xf) << 4;
	t2 = (t2 & 7) << 8;
	t3 = (t3 & 7) << 11;
	t4 = (t4 & 7) << 14;
	t5 = (t5 & 7) << 17;
	t6 = (t6 & 0xf) << 20;

	t0 |= t1 | t2 | t3 | t4 | t5 | t6;

	writel(t0, zynq_smc_base + ZYNQ_SMC_SET_CYCLES_OFFS);
}

/**
 * zynq_smc_ecc_is_busy_noirq - Read ecc busy flag
 * Return: the ecc_status bit from the ecc_status register. 1 = busy, 0 = idle
 *
 * Must be called with zynq_smc_lock held.
 */
static int zynq_smc_ecc_is_busy_noirq(void)
{
	return !!(readl(zynq_smc_base + ZYNQ_SMC_ECC_STATUS_OFFS) &
		  ZYNQ_SMC_ECC_STATUS_BUSY);
}

/**
 * zynq_smc_ecc_is_busy - Read ecc busy flag
 * Return: the ecc_status bit from the ecc_status register. 1 = busy, 0 = idle
 */
int zynq_smc_ecc_is_busy(void)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&zynq_smc_lock, flags);

	ret = zynq_smc_ecc_is_busy_noirq();

	spin_unlock_irqrestore(&zynq_smc_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(zynq_smc_ecc_is_busy);

/**
 * zynq_smc_get_ecc_val - Read ecc_valueN registers
 * @ecc_reg:	Index of the ecc_value reg (0..3)
 * Return: the content of the requested ecc_value register.
 *
 * There are four valid ecc_value registers. The argument is truncated to stay
 * within this valid boundary.
 */
u32 zynq_smc_get_ecc_val(int ecc_reg)
{
	u32 addr, reg;
	unsigned long flags;

	ecc_reg &= 3;
	addr = ZYNQ_SMC_ECC_VALUE0_OFFS + (ecc_reg << 2);

	spin_lock_irqsave(&zynq_smc_lock, flags);

	reg = readl(zynq_smc_base + addr);

	spin_unlock_irqrestore(&zynq_smc_lock, flags);

	return reg;
}
EXPORT_SYMBOL_GPL(zynq_smc_get_ecc_val);

/**
 * zynq_smc_get_nand_int_status_raw - Get NAND interrupt status bit
 * Return: the raw_int_status1 bit from the memc_status register
 */
int zynq_smc_get_nand_int_status_raw(void)
{
	u32 reg;
	unsigned long flags;

	spin_lock_irqsave(&zynq_smc_lock, flags);

	reg = readl(zynq_smc_base + ZYNQ_SMC_MEMC_STATUS_OFFS);

	spin_unlock_irqrestore(&zynq_smc_lock, flags);

	reg >>= 6;
	reg &= 1;

	return reg;
}
EXPORT_SYMBOL_GPL(zynq_smc_get_nand_int_status_raw);

/**
 * zynq_smc_clr_nand_int - Clear NAND interrupt
 */
void zynq_smc_clr_nand_int(void)
{
	unsigned long flags;

	spin_lock_irqsave(&zynq_smc_lock, flags);

	writel(ZYNQ_SMC_CFG_CLR_INT_1, zynq_smc_base + ZYNQ_SMC_CFG_CLR_OFFS);

	spin_unlock_irqrestore(&zynq_smc_lock, flags);
}
EXPORT_SYMBOL_GPL(zynq_smc_clr_nand_int);

/**
 * zynq_smc_set_ecc_mode - Set SMC ECC mode
 * @mode:	ECC mode (BYPASS, APB, MEM)
 * Return: 0 on success or negative errno.
 */
int zynq_smc_set_ecc_mode(enum zynq_smc_ecc_mode mode)
{
	u32 reg;
	unsigned long flags;
	int ret = 0;

	switch (mode) {
	case ZYNQ_SMC_ECCMODE_BYPASS:
	case ZYNQ_SMC_ECCMODE_APB:
	case ZYNQ_SMC_ECCMODE_MEM:
		spin_lock_irqsave(&zynq_smc_lock, flags);

		reg = readl(zynq_smc_base + ZYNQ_SMC_ECC_MEMCFG_OFFS);
		reg &= ~0xc;
		reg |= mode << 2;
		writel(reg, zynq_smc_base + ZYNQ_SMC_ECC_MEMCFG_OFFS);

		spin_unlock_irqrestore(&zynq_smc_lock, flags);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(zynq_smc_set_ecc_mode);

/**
 * zynq_smc_set_ecc_pg_size - Set SMC ECC page size
 * @pg_sz:	ECC page size
 * Return: 0 on success or negative errno.
 */
int zynq_smc_set_ecc_pg_size(unsigned int pg_sz)
{
	u32 reg, sz;
	unsigned long flags;

	switch (pg_sz) {
	case 0:
		sz = 0;
		break;
	case 512:
		sz = 1;
		break;
	case 1024:
		sz = 2;
		break;
	case 2048:
		sz = 3;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&zynq_smc_lock, flags);

	reg = readl(zynq_smc_base + ZYNQ_SMC_ECC_MEMCFG_OFFS);
	reg &= ~3;
	reg |= sz;
	writel(reg, zynq_smc_base + ZYNQ_SMC_ECC_MEMCFG_OFFS);

	spin_unlock_irqrestore(&zynq_smc_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(zynq_smc_set_ecc_pg_size);

static int zynq_smc_clk_notifier_cb(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	switch (event) {
	case PRE_RATE_CHANGE:
		/*
		 * if a rate change is announced we need to check whether we can
		 * run under the changed conditions
		 */
		/* fall through */
	case POST_RATE_CHANGE:
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

#ifdef CONFIG_PM_SLEEP
static int zynq_smc_suspend(struct device *dev)
{
	struct zynq_smc_data *zynq_smc = dev_get_drvdata(dev);

	clk_disable(zynq_smc->devclk);
	clk_disable(zynq_smc->aperclk);

	return 0;
}

static int zynq_smc_resume(struct device *dev)
{
	int ret;
	struct zynq_smc_data *zynq_smc = dev_get_drvdata(dev);

	ret = clk_enable(zynq_smc->aperclk);
	if (ret) {
		dev_err(dev, "Cannot enable APER clock.\n");
		return ret;
	}

	ret = clk_enable(zynq_smc->devclk);
	if (ret) {
		dev_err(dev, "Cannot enable device clock.\n");
		clk_disable(zynq_smc->aperclk);
		return ret;
	}
	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(zynq_smc_dev_pm_ops, zynq_smc_suspend,
			 zynq_smc_resume);

/**
 * zynq_smc_init_nand_interface - Initialize the NAND interface
 * @pdev:	Pointer to the platform_device struct
 * @nand_node:	Pointer to the zynq_nand device_node struct
 */
static void zynq_smc_init_nand_interface(struct platform_device *pdev,
				       struct device_node *nand_node)
{
	u32 t_rc, t_wc, t_rea, t_wp, t_clr, t_ar, t_rr;
	unsigned int bw;
	int err;
	unsigned long flags;

	err = of_property_read_u32(nand_node, "xlnx,nand-width", &bw);
	if (err) {
		dev_warn(&pdev->dev,
			 "xlnx,nand-width not in device tree, using 8");
		bw = 8;
	}
	/* nand-cycle-<X> property is refer to the NAND flash timing
	 * mapping between dts and the NAND flash AC timing
	 *  X  : AC timing name
	 *  t0 : t_rc
	 *  t1 : t_wc
	 *  t2 : t_rea
	 *  t3 : t_wp
	 *  t4 : t_clr
	 *  t5 : t_ar
	 *  t6 : t_rr
	 */
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t0", &t_rc);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t0 not in device tree");
		goto default_nand_timing;
	}
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t1", &t_wc);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t1 not in device tree");
		goto default_nand_timing;
	}
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t2", &t_rea);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t2 not in device tree");
		goto default_nand_timing;
	}
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t3", &t_wp);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t3 not in device tree");
		goto default_nand_timing;
	}
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t4", &t_clr);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t4 not in device tree");
		goto default_nand_timing;
	}
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t5", &t_ar);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t5 not in device tree");
		goto default_nand_timing;
	}
	err = of_property_read_u32(nand_node, "xlnx,nand-cycle-t6", &t_rr);
	if (err) {
		dev_warn(&pdev->dev, "xlnx,nand-cycle-t6 not in device tree");
		goto default_nand_timing;
	}

default_nand_timing:
	if (err) {
		/* set default NAND flash timing property */
		dev_warn(&pdev->dev, "Using default timing for");
		dev_warn(&pdev->dev, "2Gb Numonyx MT29F2G08ABAEAWP NAND flash");
		dev_warn(&pdev->dev, "t_wp, t_clr, t_ar are set to 4");
		dev_warn(&pdev->dev, "t_rc, t_wc, t_rr are set to 2");
		dev_warn(&pdev->dev, "t_rea is set to 1");
		t_rc = t_wc = t_rr = 4;
		t_rea = 1;
		t_wp = t_clr = t_ar = 2;
	}

	spin_lock_irqsave(&zynq_smc_lock, flags);

	if (zynq_smc_set_buswidth(bw)) {
		dev_warn(&pdev->dev, "xlnx,nand-width not valid, using 8");
		zynq_smc_set_buswidth(8);
	}

	/*
	 * Default assume 50MHz clock (20ns cycle time) and 3V operation
	 * The SET_CYCLES_REG register value depends on the flash device.
	 * Look in to the device datasheet and change its value, This value
	 * is for 2Gb Numonyx flash.
	 */
	zynq_smc_set_cycles(t_rc, t_wc, t_rea, t_wp, t_clr, t_ar, t_rr);
	writel(ZYNQ_SMC_CFG_CLR_INT_1, zynq_smc_base + ZYNQ_SMC_CFG_CLR_OFFS);
	writel(ZYNQ_SMC_DC_UPT_NAND_REGS, zynq_smc_base +
	       ZYNQ_SMC_DIRECT_CMD_OFFS);
	/* Wait till the ECC operation is complete */
	while (zynq_smc_ecc_is_busy_noirq())
		cpu_relax();
	/* Set the command1 and command2 register */
	writel(ZYNQ_NAND_ECC_CMD1, zynq_smc_base + ZYNQ_SMC_ECC_MEMCMD1_OFFS);
	writel(ZYNQ_NAND_ECC_CMD2, zynq_smc_base + ZYNQ_SMC_ECC_MEMCMD2_OFFS);

	spin_unlock_irqrestore(&zynq_smc_lock, flags);
}

static const struct of_device_id matches_nor[] = {
	{ .compatible = "cfi-flash" },
	{}
};

static const struct of_device_id matches_nand[] = {
	{ .compatible = "xlnx,zynq-nand-1.00.a" },
	{}
};

static int zynq_smc_probe(struct platform_device *pdev)
{
	struct zynq_smc_data *zynq_smc;
	struct device_node *child;
	struct resource *res;
	unsigned long flags;
	int err;
	struct device_node *of_node = pdev->dev.of_node;
	const struct of_device_id *matches = NULL;

	zynq_smc = devm_kzalloc(&pdev->dev, sizeof(*zynq_smc), GFP_KERNEL);
	if (!zynq_smc)
		return -ENOMEM;

	/* Get the NAND controller virtual address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	zynq_smc_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(zynq_smc_base))
		return PTR_ERR(zynq_smc_base);

	zynq_smc->aperclk = devm_clk_get(&pdev->dev, "aper_clk");
	if (IS_ERR(zynq_smc->aperclk)) {
		dev_err(&pdev->dev, "aper_clk clock not found.\n");
		return PTR_ERR(zynq_smc->aperclk);
	}

	zynq_smc->devclk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(zynq_smc->devclk)) {
		dev_err(&pdev->dev, "ref_clk clock not found.\n");
		return PTR_ERR(zynq_smc->devclk);
	}

	err = clk_prepare_enable(zynq_smc->aperclk);
	if (err) {
		dev_err(&pdev->dev, "Unable to enable APER clock.\n");
		return err;
	}

	err = clk_prepare_enable(zynq_smc->devclk);
	if (err) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto out_clk_dis_aper;
	}

	platform_set_drvdata(pdev, zynq_smc);

	zynq_smc->clk_rate_change_nb.notifier_call = zynq_smc_clk_notifier_cb;
	if (clk_notifier_register(zynq_smc->devclk,
				  &zynq_smc->clk_rate_change_nb))
		dev_warn(&pdev->dev, "Unable to register clock notifier.\n");

	/* clear interrupts */
	spin_lock_irqsave(&zynq_smc_lock, flags);

	writel(0x52, zynq_smc_base + ZYNQ_SMC_CFG_CLR_OFFS);

	spin_unlock_irqrestore(&zynq_smc_lock, flags);

	/* Find compatible children. Only a single child is supported */
	for_each_available_child_of_node(of_node, child) {
		if (of_match_node(matches_nand, child)) {
			zynq_smc_init_nand_interface(pdev, child);
			if (!matches) {
				matches = matches_nand;
			} else {
				dev_err(&pdev->dev,
					"incompatible configuration\n");
				goto out_clk_disable;
			}
		}

		if (of_match_node(matches_nor, child)) {
			static int counts;
			if (!matches) {
				matches = matches_nor;
			} else {
				if (matches != matches_nor || counts > 1) {
					dev_err(&pdev->dev,
						"incompatible configuration\n");
					goto out_clk_disable;
				}
			}
			counts++;
		}
	}

	if (matches)
		of_platform_populate(of_node, matches, NULL, &pdev->dev);

	return 0;

out_clk_disable:
	clk_disable_unprepare(zynq_smc->devclk);
out_clk_dis_aper:
	clk_disable_unprepare(zynq_smc->aperclk);

	return err;
}

static int zynq_smc_remove(struct platform_device *pdev)
{
	struct zynq_smc_data *zynq_smc = platform_get_drvdata(pdev);

	clk_notifier_unregister(zynq_smc->devclk,
				&zynq_smc->clk_rate_change_nb);
	clk_disable_unprepare(zynq_smc->devclk);
	clk_disable_unprepare(zynq_smc->aperclk);

	return 0;
}

/* Match table for device tree binding */
static const struct of_device_id zynq_smc_of_match[] = {
	{ .compatible = "xlnx,zynq-smc-1.00.a" },
	{ },
};
MODULE_DEVICE_TABLE(of, zynq_smc_of_match);

static struct platform_driver zynq_smc_driver = {
	.probe		= zynq_smc_probe,
	.remove		= zynq_smc_remove,
	.driver		= {
		.name	= "zynq-smc",
		.owner	= THIS_MODULE,
		.pm	= &zynq_smc_dev_pm_ops,
		.of_match_table = zynq_smc_of_match,
	},
};

module_platform_driver(zynq_smc_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx Zynq SMC Driver");
MODULE_LICENSE("GPL");
