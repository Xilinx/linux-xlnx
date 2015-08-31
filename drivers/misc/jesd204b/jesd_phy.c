/*
 * Jesd204b phy support
 *
 * Copyright (C) 2014 - 2015 Xilinx, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include "jesd_phy.h"
#include "gtx7s_cpll_bands.h"
#include "gtx7s_qpll_bands.h"

#define PLATFORM_JESD204_PHY_ADDR 0x41E10000
#define JESD_PHY_LOOP_OFF	0
#define JESD_PHY_LOOP_PCS	1
#define JESD_PHY_LOOP_PMA	2
#define JESD_PHY_LOOP_MAX	2

static inline void jesd204b_phy_write(struct jesd204b_phy_state *st,
				      unsigned reg, unsigned val)
{
	iowrite32(val, st->phy + reg);
}

static inline unsigned int jesd204b_phy_read(struct jesd204b_phy_state *st,
					     unsigned reg)
{
	return ioread32(st->phy + reg);
}

#define NUM_GT_CHANNELS	8

#define QPLL	0x3 /* QPLL (7 series) QPLL1 (UltraScale) */
#define QPLL0	0x2 /* (UltraScale Only) */
#define CPLL	0x0

#define DRPREAD  BIT(30)
#define DRPWRITE BIT(31)

#define NR_COMMON_DRP_INTERFACES 0x008
#define NR_TRANS_DRP_INTERFACES 0x00C

#define CHANNEL_DRP_BASE	0x200
#define CHANNEL_DRP_ADDR	0x204
#define CHANNEL_DRP_DREAD	0x20C
#define CHANNEL_DRP_DWRITE	0x208
#define CHANNEL_DRP_STAT	0x214

#define CHANNEL_XCVR_SEL	0x400
#define CHANNEL_XCVR_TXPLL	0x40C
#define CHANNEL_XCVR_RXPLL	0x410
#define CHANNEL_XCVR_LOOPB	0x41C

static u32 read_channel_drp_reg(struct jesd204b_phy_state *st, u32 addr)
{
	u32 temp;

	jesd204b_phy_write(st, CHANNEL_DRP_ADDR, (DRPREAD | addr));
	temp = jesd204b_phy_read(st, CHANNEL_DRP_DREAD);
	return temp;
}

static void write_channel_drp_reg(struct jesd204b_phy_state *st, u32 addr,
				  u32 data)
{
	u32 loop = 10;

	jesd204b_phy_write(st, CHANNEL_DRP_DWRITE, data);
	jesd204b_phy_write(st, CHANNEL_DRP_ADDR, (DRPWRITE | addr));

	do {
		if (!jesd204b_phy_read(st, CHANNEL_DRP_STAT))
			break;
		msleep(1);
	} while (loop--);

	if (!loop)
		dev_err(st->dev, "DRP wait timeout\n");
}

static void read_plls(struct jesd204b_phy_state *st)
{
	int i;
	int pll = st->pll;
	u32 no_of_common_drp_interfaces = 1;

	if (st->pll == CPLL)
		no_of_common_drp_interfaces = jesd204b_phy_read(
						st, NR_TRANS_DRP_INTERFACES);
	else
		no_of_common_drp_interfaces = jesd204b_phy_read(
						st, NR_COMMON_DRP_INTERFACES);

	for (i = 0; i < no_of_common_drp_interfaces; i++) {
		jesd204b_phy_write(st, CHANNEL_XCVR_SEL, i);
		pll = jesd204b_phy_read(st, CHANNEL_XCVR_TXPLL);
		pll = jesd204b_phy_read(st, CHANNEL_XCVR_RXPLL);
	}
}

static void configure_plls(struct jesd204b_phy_state *st, u32 pll)
{
	int i;
	u32 no_of_common_drp_interfaces;

	if (pll == CPLL)
		no_of_common_drp_interfaces = jesd204b_phy_read(
						st, NR_TRANS_DRP_INTERFACES);
	else
		no_of_common_drp_interfaces = jesd204b_phy_read(
						st, NR_COMMON_DRP_INTERFACES);

	for (i = 0; i < no_of_common_drp_interfaces; i++) {
		jesd204b_phy_write(st, CHANNEL_XCVR_SEL, i);
		jesd204b_phy_write(st, CHANNEL_XCVR_TXPLL, pll);
		jesd204b_phy_write(st, CHANNEL_XCVR_RXPLL, pll);
	}
}

static void configure_channel_drp(struct jesd204b_phy_state *st, u32 setting)
{
	u32 i, j, addr, temp, no_of_common_drp_interfaces;
	u32 no_channel_drp_reg = GTX7S_QPLL_NUM_CHANNEL_DRP_REGS;

	no_of_common_drp_interfaces = jesd204b_phy_read(
					st, NR_TRANS_DRP_INTERFACES);

	if (st->pll == CPLL)
		no_channel_drp_reg = GTX7S_CPLL_NUM_CHANNEL_DRP_REGS;
	for (i = 0; i < no_of_common_drp_interfaces; i++) {
		jesd204b_phy_write(st, CHANNEL_DRP_BASE, i);
		for (j = 0; j < no_channel_drp_reg; j++) {
			/* Get the register address */
			if (st->pll == QPLL) {
				addr = get_gtx7s_qpll_address_lut(j);

				/* Read the register */
				temp = read_channel_drp_reg(st, addr);

				temp &= (0xFFFF ^ (get_gtx7s_qpll_mask_lut(j)));
				temp |= ((get_gtx7s_qpll_param_lut(j, setting)
						<< get_gtx7s_qpll_offset_lut(j))
						& get_gtx7s_qpll_mask_lut(j));
			} else {
				addr = get_gtx7s_cpll_address_lut(j);

				temp = read_channel_drp_reg(st, addr);

				temp &= (0xFFFF ^ (get_gtx7s_cpll_mask_lut(j)));
				temp |= ((get_gtx7s_cpll_param_lut(j, setting)
						<< get_gtx7s_cpll_offset_lut(j))
						& get_gtx7s_cpll_mask_lut(j));
			}
			write_channel_drp_reg(st, addr, temp);
		}
	}
}

void jesd204_phy_set_speed(struct jesd204b_phy_state *st, u32 band)
{
	/* make sure we have the correct PLL's selected. */
	configure_channel_drp(st, band);
}

static void jesd204_phy_init(struct jesd204b_phy_state *st, int line_rate)
{
	jesd204_phy_set_speed(st, line_rate);
}

int jesd204_phy_set_loop(struct jesd204b_phy_state *st, u32 loopval)
{
	int i;
	u32 no_of_channels;

	no_of_channels = jesd204b_phy_read(st, NR_COMMON_DRP_INTERFACES);

	if (loopval > JESD_PHY_LOOP_MAX)
		return -EINVAL;

	for (i = 0; i < no_of_channels ; i++) {
		jesd204b_phy_write(st, CHANNEL_XCVR_SEL, i);
		jesd204b_phy_write(st, CHANNEL_XCVR_LOOPB, loopval);
	}
	return 0;
}

static ssize_t jesd204b_pll_read(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct jesd204b_phy_state *st = dev_get_drvdata(dev);

	read_plls(st);
	if (st->pll == CPLL)
		return sprintf(buf, "cpll\n");
	return sprintf(buf, "qpll\n");
}

static ssize_t jesd204b_configure_pll(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct jesd204b_phy_state *st = dev_get_drvdata(dev);
	unsigned val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (!ret)
		return 0;

	if (val > QPLL) {
		dev_err(dev, "Setting the pll to %d valid values\n"
			      "00 = CPLL\n"
			      "10 = QPLL0 (UltraScale Only)\n"
			      "11 = QPLL (7 series) QPLL1 (UltraScale)\n", val);
		return 0;
	}
	st->pll = val;
	configure_plls(st, val);

	return count;
}

static DEVICE_ATTR(configure_pll, S_IWUSR | S_IRUSR, jesd204b_pll_read,
			jesd204b_configure_pll);

static ssize_t jesd204b_linerate_read(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct jesd204b_phy_state *st = dev_get_drvdata(dev);

	return sprintf(buf, "0x%X\n", st->band);
}

static ssize_t jesd204b_linerate_write(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct jesd204b_phy_state *st = dev_get_drvdata(dev);
	int ret;
	/* Low frequencies are not supported by qpll */

	ret = kstrtouint(buf, 0, &st->band);
	if (ret)
		return ret;

	dev_info(dev, "Setting the line rate to band to %d\n", st->band);
	/* QPLL - freq options in phy
	 * 62.5
	 * 78.125
	 * 94.697
	 * 97.656
	 * 125.000
	 * 156.25
	 * 187.5
	 * 189.394
	 * 195.313
	 * 234.375
	 * 250.000
	 * 284.091
	 * 292.969
	 */
	if (st->band == 2)
		clk_set_rate(st->clk,  62500000); /* 2.5G */
	else if (st->band == 4)
		clk_set_rate(st->clk,  97656000); /* 3.9G */
	else if (st->band == 6)
		clk_set_rate(st->clk, 125000000); /* 5G */
	else if (st->band == 7)
		clk_set_rate(st->clk, 156250000); /* 6.25G */
	else if (st->band == 8)
		clk_set_rate(st->clk, 195313000); /* 7.812G */
	else if (st->band == 9)
		clk_set_rate(st->clk, 250000000);/* 10G */

	jesd204_phy_init(st, st->band);

	return count;
}

static DEVICE_ATTR(line_rate_band, S_IWUSR | S_IRUSR, jesd204b_linerate_read,
		   jesd204b_linerate_write);

/* Match table for of_platform binding */
static const struct of_device_id jesd204b_phy_of_match[] = {
	{ .compatible = "xlnx,jesd204-phy-2.0", },
	{ /* end of list */ },
};

static int jesd204b_phy_probe(struct platform_device *pdev)
{
	struct jesd204b_phy_state *st;
	struct resource *mem; /* IO mem resources */
	int ret;
	u32 ref_clk;

	st = devm_kzalloc(&pdev->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(st->clk))
		return -EPROBE_DEFER;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	st->phy = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(st->phy)) {
		dev_err(&pdev->dev, "Failed ioremap\n");
		return PTR_ERR(st->phy);
	}
	st->dev = &pdev->dev;
	platform_set_drvdata(pdev, st);

	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,lanes",
				   &st->lanes);
	if (ret) {
		dev_err(&pdev->dev, "Failed to read required dt property\n");
		return ret;
	}
	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,pll-selection",
				   &st->pll);
	if (ret) {
		dev_err(&pdev->dev, "Failed to read required dt property\n");
		return ret;
	}
	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,gt-refclk-freq",
				   &ref_clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to read required dt property\n");
		return ret;
	}

	clk_set_rate(st->clk, (unsigned long)ref_clk);
	device_create_file(&pdev->dev, &dev_attr_configure_pll);
	device_create_file(&pdev->dev, &dev_attr_line_rate_band);

	ret = clk_prepare_enable(st->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable clock.\n");
		return ret;
	}

	return 0;
}

static int jesd204b_phy_remove(struct platform_device *pdev)
{
	struct jesd204b_phy_state *st = platform_get_drvdata(pdev);

	clk_disable_unprepare(st->clk);
	clk_put(st->clk);
	device_remove_file(&pdev->dev, &dev_attr_configure_pll);
	device_remove_file(&pdev->dev, &dev_attr_line_rate_band);
	return 0;
}

static struct platform_driver jesd204b_driver = {
	.driver = {
		.name = "jesd204b_phy",
		.of_match_table = jesd204b_phy_of_match,
	},
	.probe		= jesd204b_phy_probe,
	.remove		= jesd204b_phy_remove,
};

module_platform_driver(jesd204b_driver);

MODULE_AUTHOR("Shubhrajyoti Datta <shubhraj@xilinx.com>");
MODULE_DESCRIPTION("AXI-JESD204B Phy Interface Module");
MODULE_LICENSE("GPL");
