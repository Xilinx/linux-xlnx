/*
 * Xilinx AXI-JESD204B Interface Module
 *
 * Copyright 2014 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 *
 * http://wiki.analog.com/resources/fpga/xilinx/
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "xilinx_jesd204b.h"

struct child_clk {
	struct clk_hw		hw;
	struct jesd204b_state	*st;
	unsigned long		rate;
	bool			enabled;
};

#define to_clk_priv(_hw) container_of(_hw, struct child_clk, hw)

static inline void jesd204b_write(struct jesd204b_state *st,
				  unsigned int reg, unsigned int val)
{
	iowrite32(val, st->regs + reg);
}

static inline unsigned int jesd204b_read(struct jesd204b_state *st,
					 unsigned int reg)
{
	return ioread32(st->regs + reg);
}

static ssize_t jesd204b_laneinfo_read(struct device *dev,
				      struct device_attribute *attr,
				      char *buf, unsigned int lane)
{
	struct jesd204b_state *st = dev_get_drvdata(dev);
	int ret;
	unsigned int val1, val2, val3;

	val1 = jesd204b_read(st, XLNX_JESD204_REG_ID_L(lane));
	val2 = jesd204b_read(st, XLNX_JESD204_REG_LANE_F(lane));
	val3 = jesd204b_read(st, XLNX_JESD204_REG_SCR_S_HD_CF(lane));
	ret = sprintf(buf,
		      "DID: %d, BID: %d, LID: %d, L: %d, SCR: %d, F: %d\n",
		      XLNX_JESD204_LANE_DID(val1),
		      XLNX_JESD204_LANE_BID(val1),
		      XLNX_JESD204_LANE_LID(val1),
		      XLNX_JESD204_LANE_L(val1),
		      XLNX_JESD204_LANE_SCR(val3),
		      XLNX_JESD204_LANE_F(val2));

	val1 = jesd204b_read(st, XLNX_JESD204_REG_LANE_K(lane));
	val2 = jesd204b_read(st, XLNX_JESD204_REG_M_N_ND_CS(lane));

	ret += sprintf(buf + ret,
		       "K: %d, M: %d, N: %d, CS: %d, S: %d, N': %d, HD: %d\n",
		       XLNX_JESD204_LANE_K(val1),
		       XLNX_JESD204_LANE_M(val2),
		       XLNX_JESD204_LANE_N(val2),
		       XLNX_JESD204_LANE_CS(val2),
		       XLNX_JESD204_LANE_S(val3),
		       XLNX_JESD204_LANE_ND(val2),
		       XLNX_JESD204_LANE_HD(val3));

	val1 = jesd204b_read(st, XLNX_JESD204_REG_FCHK(lane));
	ret += sprintf(buf + ret, "FCHK: 0x%X, CF: %d\n",
		       XLNX_JESD204_LANE_FCHK(val1),
		       XLNX_JESD204_LANE_CF(val3));

	val1 = jesd204b_read(st, XLNX_JESD204_REG_SC2_ADJ_CTRL(lane));
	val2 = jesd204b_read(st, XLNX_JESD204_REG_LANE_VERSION(lane));
	ret += sprintf(buf + ret,
		"ADJCNT: %d, PHYADJ: %d, ADJDIR: %d, JESDV: %d, SUBCLASS: %d\n",
		       XLNX_JESD204_LANE_ADJ_CNT(val1),
		       XLNX_JESD204_LANE_PHASE_ADJ_REQ(val1),
		       XLNX_JESD204_LANE_ADJ_CNT_DIR(val1),
		       XLNX_JESD204_LANE_JESDV(val2),
		       XLNX_JESD204_LANE_SUBCLASS(val2));

	ret += sprintf(buf + ret, "MFCNT : 0x%X\n",
		       jesd204b_read(st, XLNX_JESD204_REG_TM_MFC_CNT(lane)));
	ret += sprintf(buf + ret, "ILACNT: 0x%X\n",
		       jesd204b_read(st, XLNX_JESD204_REG_TM_ILA_CNT(lane)));
	ret += sprintf(buf + ret, "ERRCNT: 0x%X\n",
		       jesd204b_read(st, XLNX_JESD204_REG_TM_ERR_CNT(lane)));
	ret += sprintf(buf + ret, "BUFCNT: 0x%X\n",
		       jesd204b_read(st, XLNX_JESD204_REG_TM_BUF_ADJ(lane)));
	ret += sprintf(buf + ret, "LECNT: 0x%X\n",
		       jesd204b_read(st,
		       XLNX_JESD204_REG_TM_LINK_ERR_CNT(lane)));

	ret += sprintf(buf + ret, "FC: %lu\n", st->rate);

	return ret;
}

#define JESD_LANE(_x)							    \
static ssize_t jesd204b_lane##_x##_info_read(struct device *dev,	    \
					     struct device_attribute *attr, \
					     char *buf)			    \
{									    \
	return jesd204b_laneinfo_read(dev, attr, buf, _x);		    \
}									    \
static DEVICE_ATTR(lane##_x##_info, 0400, jesd204b_lane##_x##_info_read, \
		   NULL)

JESD_LANE(0);
JESD_LANE(1);
JESD_LANE(2);
JESD_LANE(3);
JESD_LANE(4);
JESD_LANE(5);
JESD_LANE(6);
JESD_LANE(7);

static ssize_t jesd204b_lane_syscstat_read(struct device *dev,
			struct device_attribute *attr,
			char *buf, unsigned int lane)
{
	unsigned int stat;
	struct jesd204b_state *st = dev_get_drvdata(dev);

	stat = jesd204b_read(st, XLNX_JESD204_REG_SYNC_ERR_STAT);

	return sprintf(buf,
			"NOT_IN_TAB: %d, DISPARITY: %d, UNEXPECTED_K: %d\n",
			stat & XLNX_JESD204_SYNC_ERR_NOT_IN_TAB(lane),
			stat & XLNX_JESD204_SYNC_ERR_DISPARITY(lane),
			stat & XLNX_JESD204_SYNC_ERR_UNEXPECTED_K(lane));
}

#define JESD_SYNCSTAT_LANE(_x)						       \
static ssize_t jesd204b_lane##_x##_syncstat_read(struct device *dev,	       \
						 struct device_attribute *attr,\
						 char *buf)		       \
{									       \
	return jesd204b_lane_syscstat_read(dev, attr, buf, _x);		       \
}									       \
static DEVICE_ATTR(lane##_x##_syncstat, 0400,			       \
		   jesd204b_lane##_x##_syncstat_read, NULL)

JESD_SYNCSTAT_LANE(0);
JESD_SYNCSTAT_LANE(1);
JESD_SYNCSTAT_LANE(2);
JESD_SYNCSTAT_LANE(3);
JESD_SYNCSTAT_LANE(4);
JESD_SYNCSTAT_LANE(5);
JESD_SYNCSTAT_LANE(6);
JESD_SYNCSTAT_LANE(7);

static ssize_t jesd204b_reg_write(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct jesd204b_state *st = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = sscanf(buf, "%i %i", &st->addr, &val);
	if (ret == 2)
		jesd204b_write(st, st->addr, val);

	return count;
}

static ssize_t jesd204b_reg_read(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct jesd204b_state *st = dev_get_drvdata(dev);

	return sprintf(buf, "0x%X\n", jesd204b_read(st, st->addr));
}

static DEVICE_ATTR(reg_access, 0600, jesd204b_reg_read,
		   jesd204b_reg_write);

static ssize_t jesd204b_syncreg_read(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct jesd204b_state *st = dev_get_drvdata(dev);

	return sprintf(buf, "0x%X\n", jesd204b_read(st,
					XLNX_JESD204_REG_SYNC_STATUS));
}

static DEVICE_ATTR(sync_status, 0400, jesd204b_syncreg_read, NULL);

static unsigned int long jesd204b_clk_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	return parent_rate;
}

static int jesd204b_clk_enable(struct clk_hw *hw)
{
	to_clk_priv(hw)->enabled = true;

	return 0;
}

static void jesd204b_clk_disable(struct clk_hw *hw)
{
	to_clk_priv(hw)->enabled = false;
}

static int jesd204b_clk_is_enabled(struct clk_hw *hw)
{
	return to_clk_priv(hw)->enabled;
}

static const struct clk_ops clkout_ops = {
	.recalc_rate = jesd204b_clk_recalc_rate,
	.enable = jesd204b_clk_enable,
	.disable = jesd204b_clk_disable,
	.is_enabled = jesd204b_clk_is_enabled,
};

/* Match table for of_platform binding */
static const struct of_device_id jesd204b_of_match[] = {
	{ .compatible = "xlnx,jesd204-5.1",},
	{ .compatible = "xlnx,jesd204-5.2",},
	{ .compatible = "xlnx,jesd204-6.1",},
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, jesd204b_of_match);

static int jesd204b_probe(struct platform_device *pdev)
{
	struct jesd204b_state *st;
	struct resource *mem; /* IO mem resources */
	struct clk *clk;
	struct child_clk *clk_priv;
	struct clk_init_data init;
	unsigned int val;
	int ret;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return -EPROBE_DEFER;

	st = devm_kzalloc(&pdev->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	st->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(st->regs)) {
		dev_err(&pdev->dev, "Failed ioremap\n");
		return PTR_ERR(st->regs);
	}

	st->dev = &pdev->dev;

	platform_set_drvdata(pdev, st);

	st->clk = clk;
	clk_set_rate(st->clk, 156250000);
	st->rate = clk_get_rate(clk);

	of_property_read_u32(pdev->dev.of_node, "xlnx,node-is-transmit",
			     &st->transmit);

	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,lanes",
				   &st->lanes);
	if (ret)
		st->lanes = jesd204b_read(st, XLNX_JESD204_REG_LANES) + 1;

	jesd204b_write(st, XLNX_JESD204_REG_RESET, XLNX_JESD204_RESET);
	while (!jesd204b_read(st, XLNX_JESD204_REG_RESET))
		msleep(20);

	jesd204b_write(st, XLNX_JESD204_REG_ILA_CTRL,
		       (of_property_read_bool(pdev->dev.of_node,
			"xlnx,lanesync-enable") ? XLNX_JESD204_ILA_EN : 0));

	jesd204b_write(st, XLNX_JESD204_REG_SCR_CTRL,
		       (of_property_read_bool(pdev->dev.of_node,
			"xlnx,scramble-enable") ? XLNX_JESD204_SCR_EN : 0));

	jesd204b_write(st, XLNX_JESD204_REG_SYSREF_CTRL,
		       (of_property_read_bool(pdev->dev.of_node,
			"xlnx,sysref-always-enable") ?
			XLNX_JESD204_ALWAYS_SYSREF_EN : 0));

	device_create_file(&pdev->dev, &dev_attr_reg_access);

	device_create_file(&pdev->dev, &dev_attr_sync_status);
	switch (st->lanes) {
	case 8:
		device_create_file(&pdev->dev, &dev_attr_lane4_info);
		device_create_file(&pdev->dev, &dev_attr_lane5_info);
		device_create_file(&pdev->dev, &dev_attr_lane6_info);
		device_create_file(&pdev->dev, &dev_attr_lane7_info);
		if (!st->transmit) {
			device_create_file(&pdev->dev,
					   &dev_attr_lane4_syncstat);
			device_create_file(&pdev->dev,
					   &dev_attr_lane5_syncstat);
			device_create_file(&pdev->dev,
					   &dev_attr_lane6_syncstat);
			device_create_file(&pdev->dev,
					   &dev_attr_lane7_syncstat);
		}
		/* fall through */
	case 4:
		device_create_file(&pdev->dev, &dev_attr_lane2_info);
		device_create_file(&pdev->dev, &dev_attr_lane3_info);
		if (!st->transmit) {
			device_create_file(&pdev->dev,
					   &dev_attr_lane2_syncstat);
			device_create_file(&pdev->dev,
					   &dev_attr_lane3_syncstat);
		}
		/* fall through */
	case 2:
		device_create_file(&pdev->dev, &dev_attr_lane1_info);
		if (!st->transmit)
			device_create_file(&pdev->dev,
					   &dev_attr_lane1_syncstat);
		/* fall through */
	case 1:
		device_create_file(&pdev->dev, &dev_attr_lane0_info);
		if (!st->transmit)
			device_create_file(&pdev->dev,
					   &dev_attr_lane0_syncstat);
		break;
	default:

		break;
	}

	clk_priv = devm_kzalloc(&pdev->dev, sizeof(*clk_priv), GFP_KERNEL);
	if (!clk_priv)
		return -ENOMEM;

	/* struct child_clk assignments */
	clk_priv->hw.init = &init;
	clk_priv->rate = st->rate;
	clk_priv->st = st;

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable clock.\n");
		return ret;
	}
	val = jesd204b_read(st, XLNX_JESD204_REG_VERSION);

	dev_info(&pdev->dev,
		 "AXI-JESD204B %d.%d Rev %d, at 0x%08llX mapped to 0x%p",
		 XLNX_JESD204_VERSION_MAJOR(val),
		 XLNX_JESD204_VERSION_MINOR(val),
		 XLNX_JESD204_VERSION_REV(val),
		 (unsigned long long)mem->start, st->regs);

	return 0;
}

static int jesd204b_remove(struct platform_device *pdev)
{
	struct jesd204b_state *st = platform_get_drvdata(pdev);

	clk_disable_unprepare(st->clk);
	clk_put(st->clk);

	return 0;
}

static struct platform_driver jesd204b_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = jesd204b_of_match,
	},
	.probe		= jesd204b_probe,
	.remove		= jesd204b_remove,
};

module_platform_driver(jesd204b_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AXI-JESD204B Interface Module");
MODULE_LICENSE("GPL v2");
