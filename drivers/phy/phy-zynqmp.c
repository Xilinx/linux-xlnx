/*
 * phy-zynqmp.c - PHY driver for Xilinx ZynqMP GT.
 *
 * Copyright (C) 2015 - 2016 Xilinx Inc.
 *
 * Author: Subbaraya Sundeep <sbhatta@xilinx.com>
 * Author: Anurag Kumar Vulisha <anuragku@xilinx.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver is tested for USB and SATA currently.
 * Other controllers PCIe, Display Port and SGMII should also
 * work but that is experimental as of now.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <dt-bindings/phy/phy.h>
#include <linux/list.h>

#define MAX_LANES			4

#define RST_LPD				0x023C
#define RST_FPD				0x0100
#define RST_ULPI			0x0250
#define RST_ULPI_HI			0x202
#define RST_ULPI_LOW			0x02

#define RST_ULPI_TIMEOUT		10

#define SATA_RESET			BIT(1)
#define DP_RESET			BIT(16)
#define USB0_RESET			(BIT(6) | BIT(8) | BIT(10))
#define USB1_RESET			(BIT(7) | BIT(9) | BIT(11))

#define ICM_CFG0			0x10010
#define ICM_CFG1			0x10014
#define ICM_CFG0_L0_MASK		0x07
#define ICM_CFG0_L1_MASK		0x70
#define ICM_CFG1_L2_MASK		0x07
#define ICM_CFG2_L3_MASK		0x70

#define TM_CMN_RST			0x10018
#define TM_CMN_RST_MASK			0x3
#define TM_CMN_RST_EN			0x1
#define TM_CMN_RST_SET			0x2

#define ICM_PROTOCOL_PD			0x0
#define ICM_PROTOCOL_PCIE		0x1
#define ICM_PROTOCOL_SATA		0x2
#define ICM_PROTOCOL_USB		0x3
#define ICM_PROTOCOL_DP			0x4
#define ICM_PROTOCOL_SGMII		0x5

#define PLL_REF_SEL0			0x10000
#define PLL_REF_OFFSET			0x4
#define PLL_FREQ_MASK			0x1F

#define L0_L0_REF_CLK_SEL		0x2860

#define L0_PLL_STATUS_READ_1		0x23E4
#define PLL_STATUS_READ_OFFSET		0x4000
#define PLL_STATUS_LOCKED		0x10

#define L0_PLL_SS_STEP_SIZE_0_LSB	0x2370
#define L0_PLL_SS_STEP_SIZE_1		0x2374
#define L0_PLL_SS_STEP_SIZE_2		0x2378
#define L0_PLL_SS_STEP_SIZE_3_MSB	0x237C
#define STEP_SIZE_OFFSET		0x4000
#define STEP_SIZE_0_MASK		0xFF
#define STEP_SIZE_1_MASK		0xFF
#define STEP_SIZE_2_MASK		0xFF
#define STEP_SIZE_3_MASK		0x3
#define FORCE_STEP_SIZE			0x10
#define FORCE_STEPS			0x20

#define L0_PLL_SS_STEPS_0_LSB		0x2368
#define L0_PLL_SS_STEPS_1_MSB		0x236C
#define STEPS_OFFSET			0x4000
#define STEPS_0_MASK			0xFF
#define STEPS_1_MASK			0x07

#define BGCAL_REF_SEL			0x10028
#define BGCAL_REF_VALUE			0x0C

#define L3_TM_CALIB_DIG19		0xEC4C
#define L3_TM_CALIB_DIG19_NSW		0x07

#define TM_OVERRIDE_NSW_CODE            0x20

#define L3_CALIB_DONE_STATUS		0xEF14
#define CALIB_DONE			0x02

#define L0_TXPMA_ST_3			0x0B0C
#define DN_CALIB_CODE			0x3F
#define DN_CALIB_SHIFT			3

#define L3_TM_CALIB_DIG18		0xEC48
#define L3_TM_CALIB_DIG18_NSW		0xE0
#define NSW_SHIFT			5
#define NSW_PIPE_SHIFT			4

#define L0_TM_PLL_DIG_37		0x2094
#define TM_PLL_DIG_37_OFFSET		0x4000
#define TM_COARSE_CODE_LIMIT		0x10

#define L0_TM_DIG_6			0x106C
#define TM_DIG_6_OFFSET			0x4000
#define TM_DISABLE_DESCRAMBLE_DECODER	0x0F

#define L0_TX_DIG_61			0x00F4
#define TX_DIG_61_OFFSET		0x4000
#define TM_DISABLE_SCRAMBLE_ENCODER	0x0F

#define L0_TX_ANA_TM_18			0x0048
#define TX_ANA_TM_18_OFFSET		0x4000

#define L0_TXPMD_TM_48			0x0CC0
#define TXPMD_TM_48_OFFSET		0x4000

#define LANE_CLK_SHARE_MASK		0x8F

#define SATA_CONTROL_OFFSET		0x0100

#define CONTROLLERS_PER_LANE		5

#define XPSGTR_TYPE_USB0	0 /* USB controller 0 */
#define XPSGTR_TYPE_USB1	1 /* USB controller 1 */
#define XPSGTR_TYPE_SATA_0	2 /* SATA controller lane 0 */
#define XPSGTR_TYPE_SATA_1	3 /* SATA controller lane 1 */
#define XPSGTR_TYPE_PCIE_0	4 /* PCIe controller lane 0 */
#define XPSGTR_TYPE_PCIE_1	5 /* PCIe controller lane 1 */
#define XPSGTR_TYPE_PCIE_2	6 /* PCIe controller lane 2 */
#define XPSGTR_TYPE_PCIE_3	7 /* PCIe controller lane 3 */
#define XPSGTR_TYPE_DP_0	8 /* Display Port controller lane 0 */
#define XPSGTR_TYPE_DP_1	9 /* Display Port controller lane 1 */
#define XPSGTR_TYPE_SGMII0	10 /* Ethernet SGMII controller 0 */
#define XPSGTR_TYPE_SGMII1	11 /* Ethernet SGMII controller 1 */
#define XPSGTR_TYPE_SGMII2	12 /* Ethernet SGMII controller 2 */
#define XPSGTR_TYPE_SGMII3	13 /* Ethernet SGMII controller 3 */

/*
 * This table holds the valid combinations of controllers and
 * lanes(Interconnect Matrix).
 */
static unsigned int icm_matrix[][CONTROLLERS_PER_LANE] = {
	{ XPSGTR_TYPE_PCIE_0, XPSGTR_TYPE_SATA_0, XPSGTR_TYPE_USB0,
		XPSGTR_TYPE_DP_1, XPSGTR_TYPE_SGMII0 },
	{ XPSGTR_TYPE_PCIE_1, XPSGTR_TYPE_SATA_1, XPSGTR_TYPE_USB0,
		XPSGTR_TYPE_DP_0, XPSGTR_TYPE_SGMII1 },
	{ XPSGTR_TYPE_PCIE_2, XPSGTR_TYPE_SATA_0, XPSGTR_TYPE_USB0,
		XPSGTR_TYPE_DP_1, XPSGTR_TYPE_SGMII2 },
	{ XPSGTR_TYPE_PCIE_3, XPSGTR_TYPE_SATA_1, XPSGTR_TYPE_USB1,
		XPSGTR_TYPE_DP_0, XPSGTR_TYPE_SGMII3 }
};

/* Allowed PLL reference clock frequencies */
enum pll_frequencies {
	REF_19_2M = 0,
	REF_20M,
	REF_24M,
	REF_26M,
	REF_27M,
	REF_38_4M,
	REF_40M,
	REF_52M,
	REF_100M,
	REF_108M,
	REF_125M,
	REF_135M,
	REF_150M,
};

/**
 * struct xpsgtr_phy - representation of a lane
 * @phy: pointer to the kernel PHY device
 * @type: controller which uses this lane
 * @lane: lane number
 * @protocol: protocol in which the lane operates
 * @ref_clk: enum of allowed ref clock rates for this lane PLL
 * @pll_lock: PLL status
 * @data: pointer to hold private data
 * @refclk_rate: PLL reference clock frequency
 * @share_laneclk: lane number of the clock to be shared
 */
struct xpsgtr_phy {
	struct phy *phy;
	u8 type;
	u8 lane;
	u8 protocol;
	enum pll_frequencies ref_clk;
	bool pll_lock;
	void *data;
	u32 refclk_rate;
	u32 share_laneclk;
};

/**
 * struct xpsgtr_ssc - structure to hold SSC settings for a lane
 * @refclk_rate: PLL reference clock frequency
 * @pll_ref_clk: value to be written to register for corresponding ref clk rate
 * @steps: number of steps of SSC (Spread Spectrum Clock)
 * @step_size: step size of each step
 */
struct xpsgtr_ssc {
	u32 refclk_rate;
	u8  pll_ref_clk;
	u32 steps;
	u32 step_size;
};

/* lookup table to hold all settings needed for a ref clock frequency */
static struct xpsgtr_ssc ssc_lookup[] = {
	{19200000, 0x05, 608, 264020},
	{20000000, 0x06, 634, 243454},
	{24000000, 0x07, 760, 168973},
	{26000000, 0x08, 824, 143860},
	{27000000, 0x09, 856, 86551},
	{38400000, 0x0A, 1218, 65896},
	{40000000, 0x0B, 634, 243454},
	{52000000, 0x0C, 824, 143860},
	{100000000, 0x0D, 1058, 87533},
	{108000000, 0x0E, 856, 86551},
	{125000000, 0x0F, 992, 119497},
	{135000000, 0x10, 1070, 55393},
	{150000000, 0x11, 792, 187091}
};

/**
 * struct xpsgtr_dev - representation of a ZynMP GT device
 * @dev: pointer to device
 * @serdes: serdes base address
 * @siou: siou base address
 * @gtr_mutex: mutex for locking
 * @phys: pointer to all the lanes
 * @fpd: base address for full power domain devices reset control
 * @lpd: base address for low power domain devices reset control
 * @tx_term_fix: fix for GT issue
 */
struct xpsgtr_dev {
	struct device *dev;
	void __iomem *serdes;
	void __iomem *siou;
	struct mutex gtr_mutex;
	struct xpsgtr_phy **phys;
	void __iomem *fpd;
	void __iomem *lpd;
	bool tx_term_fix;
};

int xpsgtr_override_deemph(struct phy *phy, u8 plvl, u8 vlvl)
{
	struct xpsgtr_phy *gtr_phy = phy_get_drvdata(phy);
	struct xpsgtr_dev *gtr_dev = gtr_phy->data;
	static u8 vs[4][4] = { { 0x2a, 0x27, 0x24, 0x20 },
			       { 0x27, 0x23, 0x20, 0xff },
			       { 0x24, 0x20, 0xff, 0xff },
			       { 0xff, 0xff, 0xff, 0xff } };

	writel(vs[plvl][vlvl],
	       gtr_dev->serdes + gtr_phy->lane * TX_ANA_TM_18_OFFSET +
	       L0_TX_ANA_TM_18);

	return 0;
}
EXPORT_SYMBOL_GPL(xpsgtr_override_deemph);

int xpsgtr_margining_factor(struct phy *phy, u8 plvl, u8 vlvl)
{
	struct xpsgtr_phy *gtr_phy = phy_get_drvdata(phy);
	struct xpsgtr_dev *gtr_dev = gtr_phy->data;
	static u8 pe[4][4] = { { 0x2, 0x2, 0x2, 0x2 },
			       { 0x1, 0x1, 0x1, 0xff },
			       { 0x0, 0x0, 0xff, 0xff },
			       { 0xff, 0xff, 0xff, 0xff } };

	writel(pe[plvl][vlvl],
	       gtr_dev->serdes + gtr_phy->lane * TXPMD_TM_48_OFFSET +
	       L0_TXPMD_TM_48);

	return 0;
}
EXPORT_SYMBOL_GPL(xpsgtr_margining_factor);

/**
 * xpsgtr_configure_pll - configures SSC settings for a lane
 * @gtr_phy: pointer to lane
 */
static void xpsgtr_configure_pll(struct xpsgtr_phy *gtr_phy)
{
	struct xpsgtr_dev *gtr_dev = gtr_phy->data;
	u32 reg;
	u32 offset;
	u32 steps;
	u32 size;
	u8 pll_ref_clk;

	steps = ssc_lookup[gtr_phy->ref_clk].steps;
	size = ssc_lookup[gtr_phy->ref_clk].step_size;
	pll_ref_clk = ssc_lookup[gtr_phy->ref_clk].pll_ref_clk;

	offset = gtr_phy->lane * PLL_REF_OFFSET + PLL_REF_SEL0;
	reg = readl(gtr_dev->serdes + offset);
	reg = (reg & ~PLL_FREQ_MASK) | pll_ref_clk;
	writel(reg, gtr_dev->serdes + offset);

	/* Enable lane clock sharing, if required */
	if (gtr_phy->share_laneclk != gtr_phy->lane) {
		/* Lane3 Ref Clock Selection Register */
		offset = gtr_phy->lane * PLL_REF_OFFSET + L0_L0_REF_CLK_SEL;
		reg = readl(gtr_dev->serdes + offset);
		reg = (reg & ~LANE_CLK_SHARE_MASK) |
				(1 << gtr_phy->share_laneclk);
		writel(reg, gtr_dev->serdes + offset);
	}

	/* SSC step size [7:0] */
	offset = gtr_phy->lane * STEP_SIZE_OFFSET + L0_PLL_SS_STEP_SIZE_0_LSB;
	reg = readl(gtr_dev->serdes + offset);
	reg = (reg & ~STEP_SIZE_0_MASK) |
		(size & STEP_SIZE_0_MASK);
	writel(reg, gtr_dev->serdes + offset);

	/* SSC step size [15:8] */
	size = size >> 8;
	offset = gtr_phy->lane * STEP_SIZE_OFFSET + L0_PLL_SS_STEP_SIZE_1;
	reg = readl(gtr_dev->serdes + offset);
	reg = (reg & ~STEP_SIZE_1_MASK) |
		(size & STEP_SIZE_1_MASK);
	writel(reg, gtr_dev->serdes + offset);

	/* SSC step size [23:16] */
	size = size >> 8;
	offset = gtr_phy->lane * STEP_SIZE_OFFSET + L0_PLL_SS_STEP_SIZE_2;
	reg = readl(gtr_dev->serdes + offset);
	reg = (reg & ~STEP_SIZE_2_MASK) |
		(size & STEP_SIZE_2_MASK);
	writel(reg, gtr_dev->serdes + offset);

	/* SSC steps [7:0] */
	offset = gtr_phy->lane * STEPS_OFFSET + L0_PLL_SS_STEPS_0_LSB;
	reg = readl(gtr_dev->serdes + offset);
	reg = (reg & ~STEPS_0_MASK) |
		(steps & STEPS_0_MASK);
	writel(reg, gtr_dev->serdes + offset);

	/* SSC steps [10:8] */
	steps = steps >> 8;
	offset = gtr_phy->lane * STEPS_OFFSET + L0_PLL_SS_STEPS_1_MSB;
	reg = readl(gtr_dev->serdes + offset);
	reg = (reg & ~STEPS_1_MASK) |
		(steps & STEPS_1_MASK);
	writel(reg, gtr_dev->serdes + offset);

	/* SSC step size [24:25] */
	size = size >> 8;
	offset = gtr_phy->lane * STEP_SIZE_OFFSET + L0_PLL_SS_STEP_SIZE_3_MSB;
	reg = readl(gtr_dev->serdes + offset);
	reg = (reg & ~STEP_SIZE_3_MASK) |
		(size & STEP_SIZE_3_MASK);
	reg |= FORCE_STEP_SIZE | FORCE_STEPS;
	writel(reg, gtr_dev->serdes + offset);
}

/**
 * xpsgtr_lane_setprotocol - sets required protocol in ICM registers
 * @gtr_phy: pointer to lane
 */
static void xpsgtr_lane_setprotocol(struct xpsgtr_phy *gtr_phy)
{
	struct xpsgtr_dev *gtr_dev = gtr_phy->data;
	u32 reg;
	u8 protocol = gtr_phy->protocol;

	switch (gtr_phy->lane) {
	case 0:
		reg = readl(gtr_dev->serdes + ICM_CFG0);
		reg = (reg & ~ICM_CFG0_L0_MASK) | protocol;
		writel(reg, gtr_dev->serdes + ICM_CFG0);
		break;
	case 1:
		reg = readl(gtr_dev->serdes + ICM_CFG0);
		reg = (reg & ~ICM_CFG0_L1_MASK) | (protocol << 4);
		writel(reg, gtr_dev->serdes + ICM_CFG0);
		break;
	case 2:
		reg = readl(gtr_dev->serdes + ICM_CFG1);
		reg = (reg & ~ICM_CFG0_L0_MASK) | protocol;
		writel(reg, gtr_dev->serdes + ICM_CFG1);
		break;
	case 3:
		reg = readl(gtr_dev->serdes + ICM_CFG1);
		reg = (reg & ~ICM_CFG0_L1_MASK) | (protocol << 4);
		writel(reg, gtr_dev->serdes + ICM_CFG1);
		break;
	default:
		/* We already checked 0 <= lane <= 3 */
		break;
	}
}

/**
 * xpsgtr_get_ssc - gets the required ssc settings based on clk rate
 * @gtr_phy: pointer to lane
 *
 * Return: 0 on success or error on failure
 */
static int xpsgtr_get_ssc(struct xpsgtr_phy *gtr_phy)
{
	u32 i;

	/*
	 * Assign the required spread spectrum(SSC) settings
	 * from lane refernce clk rate
	 */
	for (i = 0 ; i < ARRAY_SIZE(ssc_lookup); i++) {
		if (gtr_phy->refclk_rate == ssc_lookup[i].refclk_rate) {
			gtr_phy->ref_clk = i;
			return 0;
		}
	}

	/* Did not get valid ssc settings*/
	return -EINVAL;
}

/**
 * xpsgtr_configure_lane - configures SSC settings for a lane
 * @gtr_phy: pointer to lane
 *
 * Return: 0 on success or error on failure
 */
static int xpsgtr_configure_lane(struct xpsgtr_phy *gtr_phy)
{

	switch (gtr_phy->type) {
	case XPSGTR_TYPE_USB0:
	case XPSGTR_TYPE_USB1:
		gtr_phy->protocol = ICM_PROTOCOL_USB;
		break;
	case XPSGTR_TYPE_SATA_0:
	case XPSGTR_TYPE_SATA_1:
		gtr_phy->protocol = ICM_PROTOCOL_SATA;
		break;
	case XPSGTR_TYPE_DP_0:
	case XPSGTR_TYPE_DP_1:
		gtr_phy->protocol = ICM_PROTOCOL_DP;
		break;
	case XPSGTR_TYPE_PCIE_0:
	case XPSGTR_TYPE_PCIE_1:
	case XPSGTR_TYPE_PCIE_2:
	case XPSGTR_TYPE_PCIE_3:
		gtr_phy->protocol = ICM_PROTOCOL_PCIE;
		break;
	case XPSGTR_TYPE_SGMII0:
	case XPSGTR_TYPE_SGMII1:
	case XPSGTR_TYPE_SGMII2:
	case XPSGTR_TYPE_SGMII3:
		gtr_phy->protocol = ICM_PROTOCOL_SGMII;
		break;
	default:
		gtr_phy->protocol = ICM_PROTOCOL_PD;
		break;
	}

	/* Get SSC settinsg for refernce clk rate */
	if (xpsgtr_get_ssc(gtr_phy) < 0)
		return -EINVAL;

	return 0;
}

/**
 * xpsgtr_controller_reset - puts controller in reset
 * @gtr_phy: pointer to lane
 */
static void xpsgtr_controller_reset(struct xpsgtr_phy *gtr_phy)
{
	struct xpsgtr_dev *gtr_dev = gtr_phy->data;
	u32 reg;

	switch (gtr_phy->type) {
	case XPSGTR_TYPE_USB0:
		reg = readl(gtr_dev->lpd + RST_LPD);
		reg |= USB0_RESET;
		writel(reg, gtr_dev->lpd + RST_LPD);
		break;
	case XPSGTR_TYPE_USB1:
		reg = readl(gtr_dev->lpd + RST_LPD);
		reg |= USB1_RESET;
		writel(reg, gtr_dev->lpd + RST_LPD);
		break;
	case XPSGTR_TYPE_SATA_0:
	case XPSGTR_TYPE_SATA_1:
		reg = readl(gtr_dev->fpd + RST_FPD);
		reg |= SATA_RESET;
		writel(reg, gtr_dev->fpd + RST_FPD);
		break;
	case XPSGTR_TYPE_DP_0:
	case XPSGTR_TYPE_DP_1:
		reg = readl(gtr_dev->fpd + RST_FPD);
		reg |= DP_RESET;
		writel(reg, gtr_dev->fpd + RST_FPD);
		break;
	default:
		break;
	}
}

/**
 * xpsgtr_controller_release_reset - releases controller from reset
 * @gtr_phy: pointer to lane
 */
static void xpsgtr_controller_release_reset(struct xpsgtr_phy *gtr_phy)
{
	struct xpsgtr_dev *gtr_dev = gtr_phy->data;
	u32 reg;

	switch (gtr_phy->type) {
	case XPSGTR_TYPE_USB0:
		reg = readl(gtr_dev->lpd + RST_LPD);
		reg &= ~USB0_RESET;
		writel(reg, gtr_dev->lpd + RST_LPD);
		break;
	case XPSGTR_TYPE_USB1:
		reg = readl(gtr_dev->lpd + RST_LPD);
		reg &= ~USB1_RESET;
		writel(reg, gtr_dev->lpd + RST_LPD);
		break;
	case XPSGTR_TYPE_SATA_0:
	case XPSGTR_TYPE_SATA_1:
		reg = readl(gtr_dev->fpd + RST_FPD);
		reg &= ~SATA_RESET;
		writel(reg, gtr_dev->fpd + RST_FPD);
		break;
	case XPSGTR_TYPE_DP_0:
	case XPSGTR_TYPE_DP_1:
		reg = readl(gtr_dev->fpd + RST_FPD);
		reg &= ~DP_RESET;
		writel(reg, gtr_dev->fpd + RST_FPD);
		break;
	default:
		break;
	}
}

int xpsgtr_wait_pll_lock(struct phy *phy)
{
	struct xpsgtr_phy *gtr_phy = phy_get_drvdata(phy);
	struct xpsgtr_dev *gtr_dev = gtr_phy->data;
	u32 offset, reg;
	u32 timeout = 1000;
	int ret = 0;

	/* Check pll is locked */
	offset = gtr_phy->lane * PLL_STATUS_READ_OFFSET + L0_PLL_STATUS_READ_1;
	dev_dbg(gtr_dev->dev, "Waiting for PLL lock...\n");

	do {
		reg = readl(gtr_dev->serdes + offset);
		if ((reg & PLL_STATUS_LOCKED) == PLL_STATUS_LOCKED)
			break;

		if (!--timeout) {
			dev_err(gtr_dev->dev, "PLL lock time out\n");
			ret = -ETIMEDOUT;
			break;
		}
		udelay(1);
	} while (1);

	if (ret == 0)
		gtr_phy->pll_lock = true;

	dev_info(gtr_dev->dev, "Lane:%d type:%d protocol:%d pll_locked:%s\n",
			gtr_phy->lane, gtr_phy->type, gtr_phy->protocol,
			gtr_phy->pll_lock ? "yes" : "no");
	return ret;
}
EXPORT_SYMBOL_GPL(xpsgtr_wait_pll_lock);

/**
 * xpsgtr_misc_sata - miscellaneous settings for SATA
 * @gtr_phy: pointer to lane
 */
static void xpsgtr_misc_sata(struct xpsgtr_phy *gtr_phy)
{
	u32 offset;
	struct xpsgtr_dev *gtr_dev = gtr_phy->data;

	/* bypass Descrambler and 8b/10b decoder */
	offset = gtr_phy->lane * TM_DIG_6_OFFSET + L0_TM_DIG_6;
	writel(TM_DISABLE_DESCRAMBLE_DECODER, gtr_dev->serdes + offset);

	/* bypass Scrambler and 8b/10b Encoder */
	offset = gtr_phy->lane * TX_DIG_61_OFFSET + L0_TX_DIG_61;
	writel(TM_DISABLE_SCRAMBLE_ENCODER, gtr_dev->serdes + offset);

	writel(gtr_phy->lane, gtr_dev->siou + SATA_CONTROL_OFFSET);
}


/**
 * xpsgtr_ulpi_reset - This function does ULPI reset.
 * @gtr_phy: pointer to lane
 */
static void xpsgtr_ulpi_reset(struct xpsgtr_phy *gtr_phy)
{
	struct xpsgtr_dev *gtr_dev = gtr_phy->data;
	unsigned long loop_time = msecs_to_jiffies(RST_ULPI_TIMEOUT);
	unsigned long timeout;

	writel(RST_ULPI_HI, gtr_dev->lpd + RST_ULPI);

	/* wait for some time */
	timeout = jiffies + loop_time;
	do {
		cpu_relax();
	} while (!time_after_eq(jiffies, timeout));

	writel(RST_ULPI_LOW, gtr_dev->lpd + RST_ULPI);

	/* wait for some time */
	timeout = jiffies + loop_time;
	do {
		cpu_relax();
	} while (!time_after_eq(jiffies, timeout));

	writel(RST_ULPI_HI, gtr_dev->lpd + RST_ULPI);

}

/**
 * xpsgtr_phy_init - initializes a lane
 * @phy: pointer to kernel PHY device
 *
 * Return: 0 on success or error on failure
 */
static int xpsgtr_phy_init(struct phy *phy)
{
	struct xpsgtr_phy *gtr_phy = phy_get_drvdata(phy);
	struct xpsgtr_dev *gtr_dev = gtr_phy->data;
	int ret = 0;
	u32 offset;
	u32 reg;
	u32 nsw;
	u32 timeout = 500;

	mutex_lock(&gtr_dev->gtr_mutex);

	/* Put controller in reset */
	xpsgtr_controller_reset(gtr_phy);

	/*
	 * There is a functional issue in the GT. The TX termination resistance
	 * can be out of spec due to a bug in the calibration logic. Below is
	 * the workaround to fix it.
	 */
	if (gtr_dev->tx_term_fix) {

		/* Enabling Test Mode control for CMN Rest */
		reg = readl(gtr_dev->serdes + TM_CMN_RST);
		reg = (reg & ~TM_CMN_RST_MASK) | TM_CMN_RST_SET;
		writel(reg, gtr_dev->serdes + TM_CMN_RST);

		/* Set Test Mode reset */
		reg = readl(gtr_dev->serdes + TM_CMN_RST);
		reg = (reg & ~TM_CMN_RST_MASK) | TM_CMN_RST_EN;
		writel(reg, gtr_dev->serdes + TM_CMN_RST);

		writel(0x00, gtr_dev->serdes + L3_TM_CALIB_DIG18);
		writel(TM_OVERRIDE_NSW_CODE, gtr_dev->serdes +
				L3_TM_CALIB_DIG19);

		/* Writing to ICM_CFG0 restes the serdes */
		writel(1, gtr_dev->serdes + ICM_CFG0);

		/* Clear Test Mode reset */
		reg = readl(gtr_dev->serdes + TM_CMN_RST);
		reg = (reg & ~TM_CMN_RST_MASK) | TM_CMN_RST_SET;
		writel(reg, gtr_dev->serdes + TM_CMN_RST);

		dev_dbg(gtr_dev->dev, "calibrating...\n");

		do {
			reg = readl(gtr_dev->serdes + L3_CALIB_DONE_STATUS);
			if ((reg & CALIB_DONE) == CALIB_DONE)
				break;

			if (!--timeout) {
				dev_err(gtr_dev->dev, "calibration time out\n");
				ret = -ETIMEDOUT;
				goto out;
			}
			udelay(1);
		} while (1);

		dev_dbg(gtr_dev->dev, "calibration done\n");

		/* Reading NMOS Register Code */
		nsw = readl(gtr_dev->serdes + L0_TXPMA_ST_3);

		/* Set Test Mode reset */
		reg = readl(gtr_dev->serdes + TM_CMN_RST);
		reg = (reg & ~TM_CMN_RST_MASK) | TM_CMN_RST_EN;
		writel(reg, gtr_dev->serdes + TM_CMN_RST);

		nsw = nsw & DN_CALIB_CODE;

		/* Writing NMOS register values back [5:3] */
		reg = nsw >> DN_CALIB_SHIFT;
		writel(reg, gtr_dev->serdes + L3_TM_CALIB_DIG19);

		/* Writing NMOS register value [2:0] */
		reg = ((nsw & 0x7) << NSW_SHIFT) | (1 << NSW_PIPE_SHIFT);
		writel(reg, gtr_dev->serdes + L3_TM_CALIB_DIG18);

		/* Clear Test Mode reset */
		reg = readl(gtr_dev->serdes + TM_CMN_RST);
		reg = (reg & ~TM_CMN_RST_MASK) | TM_CMN_RST_SET;
		writel(reg, gtr_dev->serdes + TM_CMN_RST);

		gtr_dev->tx_term_fix = false;
	}

	/* Enable coarse code saturation limiting logic */
	offset = gtr_phy->lane * TM_PLL_DIG_37_OFFSET + L0_TM_PLL_DIG_37;
	writel(TM_COARSE_CODE_LIMIT, gtr_dev->serdes + offset);

	xpsgtr_configure_pll(gtr_phy);
	xpsgtr_lane_setprotocol(gtr_phy);

	if (gtr_phy->protocol == ICM_PROTOCOL_SATA)
		xpsgtr_misc_sata(gtr_phy);

	/* Bring controller out of reset */
	xpsgtr_controller_release_reset(gtr_phy);

	/* Wait till pll is locked for all protocols except DP. For DP
	 * pll locking function will be called from driver.
	 */
	if (gtr_phy->protocol != ICM_PROTOCOL_DP) {
		ret = xpsgtr_wait_pll_lock(phy);
		if (ret != 0)
			goto out;
	}

	/* Do ULPI reset for usb */
	if (gtr_phy->protocol == ICM_PROTOCOL_USB)
		xpsgtr_ulpi_reset(gtr_phy);

out:
	mutex_unlock(&gtr_dev->gtr_mutex);
	return ret;
}

/**
 * xpsgtr_set_lanetype - derives lane type from dts arguments
 * @gtr_phy: pointer to lane
 * @controller: type of controller
 * @instance_num: instance number of the controller in case multilane controller
 *
 * Return: 0 on success or error on failure
 */
static int xpsgtr_set_lanetype(struct xpsgtr_phy *gtr_phy, u8 controller,
				u8 instance_num)
{
	switch (controller) {
	case PHY_TYPE_SATA:
		if (instance_num == 0)
			gtr_phy->type = XPSGTR_TYPE_SATA_0;
		else if (instance_num == 1)
			gtr_phy->type = XPSGTR_TYPE_SATA_1;
		else
			return -EINVAL;
		break;
	case PHY_TYPE_USB3:
		if (instance_num == 0)
			gtr_phy->type = XPSGTR_TYPE_USB0;
		else if (instance_num == 1)
			gtr_phy->type = XPSGTR_TYPE_USB1;
		else
			return -EINVAL;
		break;
	case PHY_TYPE_DP:
		if (instance_num == 0)
			gtr_phy->type = XPSGTR_TYPE_DP_0;
		else if (instance_num == 1)
			gtr_phy->type = XPSGTR_TYPE_DP_1;
		else
			return -EINVAL;
		break;
	case PHY_TYPE_PCIE:
		if (instance_num == 0)
			gtr_phy->type = XPSGTR_TYPE_PCIE_0;
		else if (instance_num == 1)
			gtr_phy->type = XPSGTR_TYPE_PCIE_1;
		else if (instance_num == 2)
			gtr_phy->type = XPSGTR_TYPE_PCIE_2;
		else if (instance_num == 3)
			gtr_phy->type = XPSGTR_TYPE_PCIE_3;
		else
			return -EINVAL;
		break;
	case PHY_TYPE_SGMII:
		if (instance_num == 0)
			gtr_phy->type = XPSGTR_TYPE_SGMII0;
		else if (instance_num == 1)
			gtr_phy->type = XPSGTR_TYPE_SGMII1;
		else if (instance_num == 2)
			gtr_phy->type = XPSGTR_TYPE_SGMII2;
		else if (instance_num == 3)
			gtr_phy->type = XPSGTR_TYPE_SGMII3;
		else
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * xpsgtr_xlate - provides a PHY specific to a controller
 * @dev: pointer to device
 * @args: arguments from dts
 *
 * Return: pointer to kernel PHY device or error on failure
 */
static struct phy *xpsgtr_xlate(struct device *dev,
				   struct of_phandle_args *args)
{
	struct xpsgtr_dev *gtr_dev = dev_get_drvdata(dev);
	struct xpsgtr_phy *gtr_phy = NULL;
	struct device_node *phynode = args->np;
	int index;
	int i;
	u8 controller;
	u8 instance_num;

	if (args->args_count != 4) {
		dev_err(dev, "Invalid number of cells in 'phy' property\n");
		return ERR_PTR(-EINVAL);
	}
	if (!of_device_is_available(phynode)) {
		dev_warn(dev, "requested PHY is disabled\n");
		return ERR_PTR(-ENODEV);
	}
	for (index = 0; index < of_get_child_count(dev->of_node); index++) {
		if (phynode == gtr_dev->phys[index]->phy->dev.of_node) {
			gtr_phy = gtr_dev->phys[index];
			break;
		}
	}
	if (!gtr_phy) {
		dev_err(dev, "failed to find appropriate phy\n");
		return ERR_PTR(-EINVAL);
	}

	/* get type of controller from phys */
	controller = args->args[0];

	/* get controller instance number */
	instance_num = args->args[1];

	/* Check if lane sharing is required */
	gtr_phy->share_laneclk = args->args[2];

	/* get the required clk rate for controller from phys */
	gtr_phy->refclk_rate = args->args[3];

	/* derive lane type */
	if (xpsgtr_set_lanetype(gtr_phy, controller, instance_num) < 0) {
		dev_err(gtr_dev->dev, "Invalid lane type\n");
		return ERR_PTR(-EINVAL);
	}

	/* configures SSC settings for a lane */
	if (xpsgtr_configure_lane(gtr_phy) < 0) {
		dev_err(gtr_dev->dev, "Invalid clock rate: %d\n",
						gtr_phy->refclk_rate);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * Check Interconnect Matrix is obeyed i.e, given lane type
	 * is allowed to operate on the lane.
	 */
	for (i = 0; i < CONTROLLERS_PER_LANE; i++) {
		if (icm_matrix[index][i] == gtr_phy->type)
			return gtr_phy->phy;
	}

	/* Should not reach here */
	return ERR_PTR(-EINVAL);
}

static struct phy_ops xpsgtr_phyops = {
	.init		= xpsgtr_phy_init,
	.owner		= THIS_MODULE,
};

/**
 * xpsgtr_probe - The device probe function for driver initialization.
 * @pdev: pointer to the platform device structure.
 *
 * Return: 0 for success and error value on failure
 */
static int xpsgtr_probe(struct platform_device *pdev)
{
	struct device_node *child, *np = pdev->dev.of_node;
	struct xpsgtr_dev *gtr_dev;
	struct phy_provider *provider;
	struct phy *phy;
	struct resource *res;
	int lanecount, port = 0, index = 0;

	gtr_dev = devm_kzalloc(&pdev->dev, sizeof(*gtr_dev), GFP_KERNEL);
	if (!gtr_dev)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "serdes");
	gtr_dev->serdes = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gtr_dev->serdes))
		return PTR_ERR(gtr_dev->serdes);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "siou");
	gtr_dev->siou = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gtr_dev->siou))
		return PTR_ERR(gtr_dev->siou);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lpd");
	gtr_dev->lpd = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gtr_dev->lpd))
		return PTR_ERR(gtr_dev->lpd);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "fpd");
	gtr_dev->fpd = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gtr_dev->fpd))
		return PTR_ERR(gtr_dev->fpd);

	lanecount = of_get_child_count(np);
	if (lanecount > MAX_LANES || lanecount == 0)
		return -EINVAL;

	gtr_dev->phys = devm_kzalloc(&pdev->dev, sizeof(phy) * lanecount,
				       GFP_KERNEL);
	if (!gtr_dev->phys)
		return -ENOMEM;

	gtr_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, gtr_dev);
	mutex_init(&gtr_dev->gtr_mutex);
	gtr_dev->tx_term_fix = of_property_read_bool(np,
					"xlnx,tx_termination_fix");

	for_each_child_of_node(np, child) {
		struct xpsgtr_phy *gtr_phy;

		gtr_phy = devm_kzalloc(&pdev->dev, sizeof(*gtr_phy),
					 GFP_KERNEL);
		if (!gtr_phy)
			return -ENOMEM;

		/* Assign lane number to gtr_phy instance */
		gtr_phy->lane = index;

		/* Disable lane sharing as default */
		gtr_phy->share_laneclk = -1;

		gtr_dev->phys[port] = gtr_phy;
		phy = devm_phy_create(&pdev->dev, child, &xpsgtr_phyops);
		if (IS_ERR(phy)) {
			dev_err(&pdev->dev, "failed to create PHY\n");
			return PTR_ERR(phy);
		}
		gtr_dev->phys[port]->phy = phy;
		phy_set_drvdata(phy, gtr_dev->phys[port]);
		gtr_phy->data = gtr_dev;
		port++;
		index++;
	}
	provider = devm_of_phy_provider_register(&pdev->dev, xpsgtr_xlate);
	if (IS_ERR(provider)) {
		dev_err(&pdev->dev, "registering provider failed\n");
			return PTR_ERR(provider);
	}

	return 0;
}

/* Match table for of_platform binding */
static const struct of_device_id xpsgtr_of_match[] = {
	{ .compatible = "xlnx,zynqmp-psgtr", },
	{},
};
MODULE_DEVICE_TABLE(of, xpsgtr_of_match);

static struct platform_driver xpsgtr_driver = {
	.probe = xpsgtr_probe,
	.driver = {
		.name = "xilinx-psgtr",
		.of_match_table	= xpsgtr_of_match,
	},
};

module_platform_driver(xpsgtr_driver);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Xilinx ZynqMP High speed Gigabit Transceiver");
