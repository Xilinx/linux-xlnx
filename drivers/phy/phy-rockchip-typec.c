/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Chris Zhong <zyw@rock-chips.com>
 *         Kever Yang <kever.yang@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * The ROCKCHIP Type-C PHY has two PLL clocks. The first PLL clock
 * is used for USB3, the second PLL clock is used for DP. This Type-C PHY has
 * 3 working modes: USB3 only mode, DP only mode, and USB3+DP mode.
 * At USB3 only mode, both PLL clocks need to be initialized, this allows the
 * PHY to switch mode between USB3 and USB3+DP, without disconnecting the USB
 * device.
 * In The DP only mode, only the DP PLL needs to be powered on, and the 4 lanes
 * are all used for DP.
 *
 * This driver gets extcon cable state and property, then decides which mode to
 * select:
 *
 * 1. USB3 only mode:
 *    EXTCON_USB or EXTCON_USB_HOST state is true, and
 *    EXTCON_PROP_USB_SS property is true.
 *    EXTCON_DISP_DP state is false.
 *
 * 2. DP only mode:
 *    EXTCON_DISP_DP state is true, and
 *    EXTCON_PROP_USB_SS property is false.
 *    If EXTCON_USB_HOST state is true, it is DP + USB2 mode, since the USB2 phy
 *    is a separate phy, so this case is still DP only mode.
 *
 * 3. USB3+DP mode:
 *    EXTCON_USB_HOST and EXTCON_DISP_DP are both true, and
 *    EXTCON_PROP_USB_SS property is true.
 *
 * This Type-C PHY driver supports normal and flip orientation. The orientation
 * is reported by the EXTCON_PROP_USB_TYPEC_POLARITY property: true is flip
 * orientation, false is normal orientation.
 *
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/extcon.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <linux/mfd/syscon.h>
#include <linux/phy/phy.h>

#define CMN_SSM_BANDGAP			(0x21 << 2)
#define CMN_SSM_BIAS			(0x22 << 2)
#define CMN_PLLSM0_PLLEN		(0x29 << 2)
#define CMN_PLLSM0_PLLPRE		(0x2a << 2)
#define CMN_PLLSM0_PLLVREF		(0x2b << 2)
#define CMN_PLLSM0_PLLLOCK		(0x2c << 2)
#define CMN_PLLSM1_PLLEN		(0x31 << 2)
#define CMN_PLLSM1_PLLPRE		(0x32 << 2)
#define CMN_PLLSM1_PLLVREF		(0x33 << 2)
#define CMN_PLLSM1_PLLLOCK		(0x34 << 2)
#define CMN_PLLSM1_USER_DEF_CTRL	(0x37 << 2)
#define CMN_ICAL_OVRD			(0xc1 << 2)
#define CMN_PLL0_VCOCAL_OVRD		(0x83 << 2)
#define CMN_PLL0_VCOCAL_INIT		(0x84 << 2)
#define CMN_PLL0_VCOCAL_ITER		(0x85 << 2)
#define CMN_PLL0_LOCK_REFCNT_START	(0x90 << 2)
#define CMN_PLL0_LOCK_PLLCNT_START	(0x92 << 2)
#define CMN_PLL0_LOCK_PLLCNT_THR	(0x93 << 2)
#define CMN_PLL0_INTDIV			(0x94 << 2)
#define CMN_PLL0_FRACDIV		(0x95 << 2)
#define CMN_PLL0_HIGH_THR		(0x96 << 2)
#define CMN_PLL0_DSM_DIAG		(0x97 << 2)
#define CMN_PLL0_SS_CTRL1		(0x98 << 2)
#define CMN_PLL0_SS_CTRL2		(0x99 << 2)
#define CMN_PLL1_VCOCAL_START		(0xa1 << 2)
#define CMN_PLL1_VCOCAL_OVRD		(0xa3 << 2)
#define CMN_PLL1_VCOCAL_INIT		(0xa4 << 2)
#define CMN_PLL1_VCOCAL_ITER		(0xa5 << 2)
#define CMN_PLL1_LOCK_REFCNT_START	(0xb0 << 2)
#define CMN_PLL1_LOCK_PLLCNT_START	(0xb2 << 2)
#define CMN_PLL1_LOCK_PLLCNT_THR	(0xb3 << 2)
#define CMN_PLL1_INTDIV			(0xb4 << 2)
#define CMN_PLL1_FRACDIV		(0xb5 << 2)
#define CMN_PLL1_HIGH_THR		(0xb6 << 2)
#define CMN_PLL1_DSM_DIAG		(0xb7 << 2)
#define CMN_PLL1_SS_CTRL1		(0xb8 << 2)
#define CMN_PLL1_SS_CTRL2		(0xb9 << 2)
#define CMN_RXCAL_OVRD			(0xd1 << 2)
#define CMN_TXPUCAL_CTRL		(0xe0 << 2)
#define CMN_TXPUCAL_OVRD		(0xe1 << 2)
#define CMN_TXPDCAL_OVRD		(0xf1 << 2)
#define CMN_DIAG_PLL0_FBH_OVRD		(0x1c0 << 2)
#define CMN_DIAG_PLL0_FBL_OVRD		(0x1c1 << 2)
#define CMN_DIAG_PLL0_OVRD		(0x1c2 << 2)
#define CMN_DIAG_PLL0_V2I_TUNE		(0x1c5 << 2)
#define CMN_DIAG_PLL0_CP_TUNE		(0x1c6 << 2)
#define CMN_DIAG_PLL0_LF_PROG		(0x1c7 << 2)
#define CMN_DIAG_PLL1_FBH_OVRD		(0x1d0 << 2)
#define CMN_DIAG_PLL1_FBL_OVRD		(0x1d1 << 2)
#define CMN_DIAG_PLL1_OVRD		(0x1d2 << 2)
#define CMN_DIAG_PLL1_V2I_TUNE		(0x1d5 << 2)
#define CMN_DIAG_PLL1_CP_TUNE		(0x1d6 << 2)
#define CMN_DIAG_PLL1_LF_PROG		(0x1d7 << 2)
#define CMN_DIAG_PLL1_PTATIS_TUNE1	(0x1d8 << 2)
#define CMN_DIAG_PLL1_PTATIS_TUNE2	(0x1d9 << 2)
#define CMN_DIAG_PLL1_INCLK_CTRL	(0x1da << 2)
#define CMN_DIAG_HSCLK_SEL		(0x1e0 << 2)

#define XCVR_PSM_RCTRL(n)		((0x4001 | ((n) << 9)) << 2)
#define XCVR_PSM_CAL_TMR(n)		((0x4002 | ((n) << 9)) << 2)
#define XCVR_PSM_A0IN_TMR(n)		((0x4003 | ((n) << 9)) << 2)
#define TX_TXCC_CAL_SCLR_MULT(n)	((0x4047 | ((n) << 9)) << 2)
#define TX_TXCC_CPOST_MULT_00(n)	((0x404c | ((n) << 9)) << 2)
#define TX_TXCC_CPOST_MULT_01(n)	((0x404d | ((n) << 9)) << 2)
#define TX_TXCC_CPOST_MULT_10(n)	((0x404e | ((n) << 9)) << 2)
#define TX_TXCC_CPOST_MULT_11(n)	((0x404f | ((n) << 9)) << 2)
#define TX_TXCC_MGNFS_MULT_000(n)	((0x4050 | ((n) << 9)) << 2)
#define TX_TXCC_MGNFS_MULT_001(n)	((0x4051 | ((n) << 9)) << 2)
#define TX_TXCC_MGNFS_MULT_010(n)	((0x4052 | ((n) << 9)) << 2)
#define TX_TXCC_MGNFS_MULT_011(n)	((0x4053 | ((n) << 9)) << 2)
#define TX_TXCC_MGNFS_MULT_100(n)	((0x4054 | ((n) << 9)) << 2)
#define TX_TXCC_MGNFS_MULT_101(n)	((0x4055 | ((n) << 9)) << 2)
#define TX_TXCC_MGNFS_MULT_110(n)	((0x4056 | ((n) << 9)) << 2)
#define TX_TXCC_MGNFS_MULT_111(n)	((0x4057 | ((n) << 9)) << 2)
#define XCVR_DIAG_PLLDRC_CTRL(n)	((0x40e0 | ((n) << 9)) << 2)
#define XCVR_DIAG_BIDI_CTRL(n)		((0x40e8 | ((n) << 9)) << 2)
#define XCVR_DIAG_LANE_FCM_EN_MGN(n)	((0x40f2 | ((n) << 9)) << 2)
#define TX_PSC_A0(n)			((0x4100 | ((n) << 9)) << 2)
#define TX_PSC_A1(n)			((0x4101 | ((n) << 9)) << 2)
#define TX_PSC_A2(n)			((0x4102 | ((n) << 9)) << 2)
#define TX_PSC_A3(n)			((0x4103 | ((n) << 9)) << 2)
#define TX_RCVDET_CTRL(n)		((0x4120 | ((n) << 9)) << 2)
#define TX_RCVDET_EN_TMR(n)		((0x4122 | ((n) << 9)) << 2)
#define TX_RCVDET_ST_TMR(n)		((0x4123 | ((n) << 9)) << 2)
#define TX_DIAG_TX_DRV(n)		((0x41e1 | ((n) << 9)) << 2)
#define TX_DIAG_BGREF_PREDRV_DELAY	(0x41e7 << 2)
#define TX_ANA_CTRL_REG_1		(0x5020 << 2)
#define TX_ANA_CTRL_REG_2		(0x5021 << 2)
#define TXDA_COEFF_CALC_CTRL		(0x5022 << 2)
#define TX_DIG_CTRL_REG_2		(0x5024 << 2)
#define TXDA_CYA_AUXDA_CYA		(0x5025 << 2)
#define TX_ANA_CTRL_REG_3		(0x5026 << 2)
#define TX_ANA_CTRL_REG_4		(0x5027 << 2)
#define TX_ANA_CTRL_REG_5		(0x5029 << 2)

#define RX_PSC_A0(n)			((0x8000 | ((n) << 9)) << 2)
#define RX_PSC_A1(n)			((0x8001 | ((n) << 9)) << 2)
#define RX_PSC_A2(n)			((0x8002 | ((n) << 9)) << 2)
#define RX_PSC_A3(n)			((0x8003 | ((n) << 9)) << 2)
#define RX_PSC_CAL(n)			((0x8006 | ((n) << 9)) << 2)
#define RX_PSC_RDY(n)			((0x8007 | ((n) << 9)) << 2)
#define RX_IQPI_ILL_CAL_OVRD		(0x8023 << 2)
#define RX_EPI_ILL_CAL_OVRD		(0x8033 << 2)
#define RX_SDCAL0_OVRD			(0x8041 << 2)
#define RX_SDCAL1_OVRD			(0x8049 << 2)
#define RX_SLC_INIT			(0x806d << 2)
#define RX_SLC_RUN			(0x806e << 2)
#define RX_CDRLF_CNFG2			(0x8081 << 2)
#define RX_SIGDET_HL_FILT_TMR(n)	((0x8090 | ((n) << 9)) << 2)
#define RX_SLC_IOP0_OVRD		(0x8101 << 2)
#define RX_SLC_IOP1_OVRD		(0x8105 << 2)
#define RX_SLC_QOP0_OVRD		(0x8109 << 2)
#define RX_SLC_QOP1_OVRD		(0x810d << 2)
#define RX_SLC_EOP0_OVRD		(0x8111 << 2)
#define RX_SLC_EOP1_OVRD		(0x8115 << 2)
#define RX_SLC_ION0_OVRD		(0x8119 << 2)
#define RX_SLC_ION1_OVRD		(0x811d << 2)
#define RX_SLC_QON0_OVRD		(0x8121 << 2)
#define RX_SLC_QON1_OVRD		(0x8125 << 2)
#define RX_SLC_EON0_OVRD		(0x8129 << 2)
#define RX_SLC_EON1_OVRD		(0x812d << 2)
#define RX_SLC_IEP0_OVRD		(0x8131 << 2)
#define RX_SLC_IEP1_OVRD		(0x8135 << 2)
#define RX_SLC_QEP0_OVRD		(0x8139 << 2)
#define RX_SLC_QEP1_OVRD		(0x813d << 2)
#define RX_SLC_EEP0_OVRD		(0x8141 << 2)
#define RX_SLC_EEP1_OVRD		(0x8145 << 2)
#define RX_SLC_IEN0_OVRD		(0x8149 << 2)
#define RX_SLC_IEN1_OVRD		(0x814d << 2)
#define RX_SLC_QEN0_OVRD		(0x8151 << 2)
#define RX_SLC_QEN1_OVRD		(0x8155 << 2)
#define RX_SLC_EEN0_OVRD		(0x8159 << 2)
#define RX_SLC_EEN1_OVRD		(0x815d << 2)
#define RX_REE_CTRL_DATA_MASK(n)	((0x81bb | ((n) << 9)) << 2)
#define RX_DIAG_SIGDET_TUNE(n)		((0x81dc | ((n) << 9)) << 2)
#define RX_DIAG_SC2C_DELAY		(0x81e1 << 2)

#define PMA_LANE_CFG			(0xc000 << 2)
#define PIPE_CMN_CTRL1			(0xc001 << 2)
#define PIPE_CMN_CTRL2			(0xc002 << 2)
#define PIPE_COM_LOCK_CFG1		(0xc003 << 2)
#define PIPE_COM_LOCK_CFG2		(0xc004 << 2)
#define PIPE_RCV_DET_INH		(0xc005 << 2)
#define DP_MODE_CTL			(0xc008 << 2)
#define DP_CLK_CTL			(0xc009 << 2)
#define STS				(0xc00F << 2)
#define PHY_ISO_CMN_CTRL		(0xc010 << 2)
#define PHY_DP_TX_CTL			(0xc408 << 2)
#define PMA_CMN_CTRL1			(0xc800 << 2)
#define PHY_PMA_ISO_CMN_CTRL		(0xc810 << 2)
#define PHY_ISOLATION_CTRL		(0xc81f << 2)
#define PHY_PMA_ISO_XCVR_CTRL(n)	((0xcc11 | ((n) << 6)) << 2)
#define PHY_PMA_ISO_LINK_MODE(n)	((0xcc12 | ((n) << 6)) << 2)
#define PHY_PMA_ISO_PWRST_CTRL(n)	((0xcc13 | ((n) << 6)) << 2)
#define PHY_PMA_ISO_TX_DATA_LO(n)	((0xcc14 | ((n) << 6)) << 2)
#define PHY_PMA_ISO_TX_DATA_HI(n)	((0xcc15 | ((n) << 6)) << 2)
#define PHY_PMA_ISO_RX_DATA_LO(n)	((0xcc16 | ((n) << 6)) << 2)
#define PHY_PMA_ISO_RX_DATA_HI(n)	((0xcc17 | ((n) << 6)) << 2)
#define TX_BIST_CTRL(n)			((0x4140 | ((n) << 9)) << 2)
#define TX_BIST_UDDWR(n)		((0x4141 | ((n) << 9)) << 2)

/*
 * Selects which PLL clock will be driven on the analog high speed
 * clock 0: PLL 0 div 1
 * clock 1: PLL 1 div 2
 */
#define CLK_PLL_CONFIG			0X30
#define CLK_PLL_MASK			0x33

#define CMN_READY			BIT(0)

#define DP_PLL_CLOCK_ENABLE		BIT(2)
#define DP_PLL_ENABLE			BIT(0)
#define DP_PLL_DATA_RATE_RBR		((2 << 12) | (4 << 8))
#define DP_PLL_DATA_RATE_HBR		((2 << 12) | (4 << 8))
#define DP_PLL_DATA_RATE_HBR2		((1 << 12) | (2 << 8))

#define DP_MODE_A0			BIT(4)
#define DP_MODE_A2			BIT(6)
#define DP_MODE_ENTER_A0		0xc101
#define DP_MODE_ENTER_A2		0xc104

#define PHY_MODE_SET_TIMEOUT		100000

#define PIN_ASSIGN_C_E			0x51d9
#define PIN_ASSIGN_D_F			0x5100

#define MODE_DISCONNECT			0
#define MODE_UFP_USB			BIT(0)
#define MODE_DFP_USB			BIT(1)
#define MODE_DFP_DP			BIT(2)

struct usb3phy_reg {
	u32 offset;
	u32 enable_bit;
	u32 write_enable;
};

struct rockchip_usb3phy_port_cfg {
	struct usb3phy_reg typec_conn_dir;
	struct usb3phy_reg usb3tousb2_en;
	struct usb3phy_reg external_psm;
	struct usb3phy_reg pipe_status;
};

struct rockchip_typec_phy {
	struct device *dev;
	void __iomem *base;
	struct extcon_dev *extcon;
	struct regmap *grf_regs;
	struct clk *clk_core;
	struct clk *clk_ref;
	struct reset_control *uphy_rst;
	struct reset_control *pipe_rst;
	struct reset_control *tcphy_rst;
	struct rockchip_usb3phy_port_cfg port_cfgs;
	/* mutex to protect access to individual PHYs */
	struct mutex lock;

	bool flip;
	u8 mode;
};

struct phy_reg {
	u16 value;
	u32 addr;
};

struct phy_reg usb3_pll_cfg[] = {
	{ 0xf0,		CMN_PLL0_VCOCAL_INIT },
	{ 0x18,		CMN_PLL0_VCOCAL_ITER },
	{ 0xd0,		CMN_PLL0_INTDIV },
	{ 0x4a4a,	CMN_PLL0_FRACDIV },
	{ 0x34,		CMN_PLL0_HIGH_THR },
	{ 0x1ee,	CMN_PLL0_SS_CTRL1 },
	{ 0x7f03,	CMN_PLL0_SS_CTRL2 },
	{ 0x20,		CMN_PLL0_DSM_DIAG },
	{ 0,		CMN_DIAG_PLL0_OVRD },
	{ 0,		CMN_DIAG_PLL0_FBH_OVRD },
	{ 0,		CMN_DIAG_PLL0_FBL_OVRD },
	{ 0x7,		CMN_DIAG_PLL0_V2I_TUNE },
	{ 0x45,		CMN_DIAG_PLL0_CP_TUNE },
	{ 0x8,		CMN_DIAG_PLL0_LF_PROG },
};

struct phy_reg dp_pll_cfg[] = {
	{ 0xf0,		CMN_PLL1_VCOCAL_INIT },
	{ 0x18,		CMN_PLL1_VCOCAL_ITER },
	{ 0x30b9,	CMN_PLL1_VCOCAL_START },
	{ 0x21c,	CMN_PLL1_INTDIV },
	{ 0,		CMN_PLL1_FRACDIV },
	{ 0x5,		CMN_PLL1_HIGH_THR },
	{ 0x35,		CMN_PLL1_SS_CTRL1 },
	{ 0x7f1e,	CMN_PLL1_SS_CTRL2 },
	{ 0x20,		CMN_PLL1_DSM_DIAG },
	{ 0,		CMN_PLLSM1_USER_DEF_CTRL },
	{ 0,		CMN_DIAG_PLL1_OVRD },
	{ 0,		CMN_DIAG_PLL1_FBH_OVRD },
	{ 0,		CMN_DIAG_PLL1_FBL_OVRD },
	{ 0x6,		CMN_DIAG_PLL1_V2I_TUNE },
	{ 0x45,		CMN_DIAG_PLL1_CP_TUNE },
	{ 0x8,		CMN_DIAG_PLL1_LF_PROG },
	{ 0x100,	CMN_DIAG_PLL1_PTATIS_TUNE1 },
	{ 0x7,		CMN_DIAG_PLL1_PTATIS_TUNE2 },
	{ 0x4,		CMN_DIAG_PLL1_INCLK_CTRL },
};

static void tcphy_cfg_24m(struct rockchip_typec_phy *tcphy)
{
	u32 i, rdata;

	/*
	 * cmn_ref_clk_sel = 3, select the 24Mhz for clk parent
	 * cmn_psm_clk_dig_div = 2, set the clk division to 2
	 */
	writel(0x830, tcphy->base + PMA_CMN_CTRL1);
	for (i = 0; i < 4; i++) {
		/*
		 * The following PHY configuration assumes a 24 MHz reference
		 * clock.
		 */
		writel(0x90, tcphy->base + XCVR_DIAG_LANE_FCM_EN_MGN(i));
		writel(0x960, tcphy->base + TX_RCVDET_EN_TMR(i));
		writel(0x30, tcphy->base + TX_RCVDET_ST_TMR(i));
	}

	rdata = readl(tcphy->base + CMN_DIAG_HSCLK_SEL);
	rdata &= ~CLK_PLL_MASK;
	rdata |= CLK_PLL_CONFIG;
	writel(rdata, tcphy->base + CMN_DIAG_HSCLK_SEL);
}

static void tcphy_cfg_usb3_pll(struct rockchip_typec_phy *tcphy)
{
	u32 i;

	/* load the configuration of PLL0 */
	for (i = 0; i < ARRAY_SIZE(usb3_pll_cfg); i++)
		writel(usb3_pll_cfg[i].value,
		       tcphy->base + usb3_pll_cfg[i].addr);
}

static void tcphy_cfg_dp_pll(struct rockchip_typec_phy *tcphy)
{
	u32 i;

	/* set the default mode to RBR */
	writel(DP_PLL_CLOCK_ENABLE | DP_PLL_ENABLE | DP_PLL_DATA_RATE_RBR,
	       tcphy->base + DP_CLK_CTL);

	/* load the configuration of PLL1 */
	for (i = 0; i < ARRAY_SIZE(dp_pll_cfg); i++)
		writel(dp_pll_cfg[i].value, tcphy->base + dp_pll_cfg[i].addr);
}

static void tcphy_tx_usb3_cfg_lane(struct rockchip_typec_phy *tcphy, u32 lane)
{
	writel(0x7799, tcphy->base + TX_PSC_A0(lane));
	writel(0x7798, tcphy->base + TX_PSC_A1(lane));
	writel(0x5098, tcphy->base + TX_PSC_A2(lane));
	writel(0x5098, tcphy->base + TX_PSC_A3(lane));
	writel(0, tcphy->base + TX_TXCC_MGNFS_MULT_000(lane));
	writel(0xbf, tcphy->base + XCVR_DIAG_BIDI_CTRL(lane));
}

static void tcphy_rx_usb3_cfg_lane(struct rockchip_typec_phy *tcphy, u32 lane)
{
	writel(0xa6fd, tcphy->base + RX_PSC_A0(lane));
	writel(0xa6fd, tcphy->base + RX_PSC_A1(lane));
	writel(0xa410, tcphy->base + RX_PSC_A2(lane));
	writel(0x2410, tcphy->base + RX_PSC_A3(lane));
	writel(0x23ff, tcphy->base + RX_PSC_CAL(lane));
	writel(0x13, tcphy->base + RX_SIGDET_HL_FILT_TMR(lane));
	writel(0x03e7, tcphy->base + RX_REE_CTRL_DATA_MASK(lane));
	writel(0x1004, tcphy->base + RX_DIAG_SIGDET_TUNE(lane));
	writel(0x2010, tcphy->base + RX_PSC_RDY(lane));
	writel(0xfb, tcphy->base + XCVR_DIAG_BIDI_CTRL(lane));
}

static void tcphy_dp_cfg_lane(struct rockchip_typec_phy *tcphy, u32 lane)
{
	u16 rdata;

	writel(0xbefc, tcphy->base + XCVR_PSM_RCTRL(lane));
	writel(0x6799, tcphy->base + TX_PSC_A0(lane));
	writel(0x6798, tcphy->base + TX_PSC_A1(lane));
	writel(0x98, tcphy->base + TX_PSC_A2(lane));
	writel(0x98, tcphy->base + TX_PSC_A3(lane));

	writel(0, tcphy->base + TX_TXCC_MGNFS_MULT_000(lane));
	writel(0, tcphy->base + TX_TXCC_MGNFS_MULT_001(lane));
	writel(0, tcphy->base + TX_TXCC_MGNFS_MULT_010(lane));
	writel(0, tcphy->base + TX_TXCC_MGNFS_MULT_011(lane));
	writel(0, tcphy->base + TX_TXCC_MGNFS_MULT_100(lane));
	writel(0, tcphy->base + TX_TXCC_MGNFS_MULT_101(lane));
	writel(0, tcphy->base + TX_TXCC_MGNFS_MULT_110(lane));
	writel(0, tcphy->base + TX_TXCC_MGNFS_MULT_111(lane));
	writel(0, tcphy->base + TX_TXCC_CPOST_MULT_10(lane));
	writel(0, tcphy->base + TX_TXCC_CPOST_MULT_01(lane));
	writel(0, tcphy->base + TX_TXCC_CPOST_MULT_00(lane));
	writel(0, tcphy->base + TX_TXCC_CPOST_MULT_11(lane));

	writel(0x128, tcphy->base + TX_TXCC_CAL_SCLR_MULT(lane));
	writel(0x400, tcphy->base + TX_DIAG_TX_DRV(lane));

	rdata = readl(tcphy->base + XCVR_DIAG_PLLDRC_CTRL(lane));
	rdata = (rdata & 0x8fff) | 0x6000;
	writel(rdata, tcphy->base + XCVR_DIAG_PLLDRC_CTRL(lane));
}

static inline int property_enable(struct rockchip_typec_phy *tcphy,
				  const struct usb3phy_reg *reg, bool en)
{
	u32 mask = 1 << reg->write_enable;
	u32 val = en << reg->enable_bit;

	return regmap_write(tcphy->grf_regs, reg->offset, val | mask);
}

static void tcphy_dp_aux_calibration(struct rockchip_typec_phy *tcphy)
{
	u16 rdata, rdata2, val;

	/* disable txda_cal_latch_en for rewrite the calibration values */
	rdata = readl(tcphy->base + TX_ANA_CTRL_REG_1);
	val = rdata & 0xdfff;
	writel(val, tcphy->base + TX_ANA_CTRL_REG_1);

	/*
	 * read a resistor calibration code from CMN_TXPUCAL_CTRL[6:0] and
	 * write it to TX_DIG_CTRL_REG_2[6:0], and delay 1ms to make sure it
	 * works.
	 */
	rdata = readl(tcphy->base + TX_DIG_CTRL_REG_2);
	rdata = rdata & 0xffc0;

	rdata2 = readl(tcphy->base + CMN_TXPUCAL_CTRL);
	rdata2 = rdata2 & 0x3f;

	val = rdata | rdata2;
	writel(val, tcphy->base + TX_DIG_CTRL_REG_2);
	usleep_range(1000, 1050);

	/*
	 * Enable signal for latch that sample and holds calibration values.
	 * Activate this signal for 1 clock cycle to sample new calibration
	 * values.
	 */
	rdata = readl(tcphy->base + TX_ANA_CTRL_REG_1);
	val = rdata | 0x2000;
	writel(val, tcphy->base + TX_ANA_CTRL_REG_1);
	usleep_range(150, 200);

	/* set TX Voltage Level and TX Deemphasis to 0 */
	writel(0, tcphy->base + PHY_DP_TX_CTL);
	/* re-enable decap */
	writel(0x100, tcphy->base + TX_ANA_CTRL_REG_2);
	writel(0x300, tcphy->base + TX_ANA_CTRL_REG_2);
	writel(0x2008, tcphy->base + TX_ANA_CTRL_REG_1);
	writel(0x2018, tcphy->base + TX_ANA_CTRL_REG_1);

	writel(0, tcphy->base + TX_ANA_CTRL_REG_5);

	/*
	 * Programs txda_drv_ldo_prog[15:0], Sets driver LDO
	 * voltage 16'h1001 for DP-AUX-TX and RX
	 */
	writel(0x1001, tcphy->base + TX_ANA_CTRL_REG_4);

	/* re-enables Bandgap reference for LDO */
	writel(0x2098, tcphy->base + TX_ANA_CTRL_REG_1);
	writel(0x2198, tcphy->base + TX_ANA_CTRL_REG_1);

	/*
	 * re-enables the transmitter pre-driver, driver data selection MUX,
	 * and receiver detect circuits.
	 */
	writel(0x301, tcphy->base + TX_ANA_CTRL_REG_2);
	writel(0x303, tcphy->base + TX_ANA_CTRL_REG_2);

	/*
	 * BIT 12: Controls auxda_polarity, which selects the polarity of the
	 * xcvr:
	 * 1, Reverses the polarity (If TYPEC, Pulls ups aux_p and pull
	 * down aux_m)
	 * 0, Normal polarity (if TYPE_C, pulls up aux_m and pulls down
	 * aux_p)
	 */
	val = 0xa078;
	if (!tcphy->flip)
		val |= BIT(12);
	writel(val, tcphy->base + TX_ANA_CTRL_REG_1);

	writel(0, tcphy->base + TX_ANA_CTRL_REG_3);
	writel(0, tcphy->base + TX_ANA_CTRL_REG_4);
	writel(0, tcphy->base + TX_ANA_CTRL_REG_5);

	/*
	 * Controls low_power_swing_en, set the voltage swing of the driver
	 * to 400mv. The values	below are peak to peak (differential) values.
	 */
	writel(4, tcphy->base + TXDA_COEFF_CALC_CTRL);
	writel(0, tcphy->base + TXDA_CYA_AUXDA_CYA);

	/* Controls tx_high_z_tm_en */
	val = readl(tcphy->base + TX_DIG_CTRL_REG_2);
	val |= BIT(15);
	writel(val, tcphy->base + TX_DIG_CTRL_REG_2);
}

static int tcphy_phy_init(struct rockchip_typec_phy *tcphy, u8 mode)
{
	struct rockchip_usb3phy_port_cfg *cfg = &tcphy->port_cfgs;
	int ret, i;
	u32 val;

	ret = clk_prepare_enable(tcphy->clk_core);
	if (ret) {
		dev_err(tcphy->dev, "Failed to prepare_enable core clock\n");
		return ret;
	}

	ret = clk_prepare_enable(tcphy->clk_ref);
	if (ret) {
		dev_err(tcphy->dev, "Failed to prepare_enable ref clock\n");
		goto err_clk_core;
	}

	reset_control_deassert(tcphy->tcphy_rst);

	property_enable(tcphy, &cfg->typec_conn_dir, tcphy->flip);

	tcphy_cfg_24m(tcphy);

	if (mode == MODE_DFP_DP) {
		tcphy_cfg_dp_pll(tcphy);
		for (i = 0; i < 4; i++)
			tcphy_dp_cfg_lane(tcphy, i);

		writel(PIN_ASSIGN_C_E, tcphy->base + PMA_LANE_CFG);
	} else {
		tcphy_cfg_usb3_pll(tcphy);
		tcphy_cfg_dp_pll(tcphy);
		if (tcphy->flip) {
			tcphy_tx_usb3_cfg_lane(tcphy, 3);
			tcphy_rx_usb3_cfg_lane(tcphy, 2);
			tcphy_dp_cfg_lane(tcphy, 0);
			tcphy_dp_cfg_lane(tcphy, 1);
		} else {
			tcphy_tx_usb3_cfg_lane(tcphy, 0);
			tcphy_rx_usb3_cfg_lane(tcphy, 1);
			tcphy_dp_cfg_lane(tcphy, 2);
			tcphy_dp_cfg_lane(tcphy, 3);
		}

		writel(PIN_ASSIGN_D_F, tcphy->base + PMA_LANE_CFG);
	}

	writel(DP_MODE_ENTER_A2, tcphy->base + DP_MODE_CTL);

	reset_control_deassert(tcphy->uphy_rst);

	ret = readx_poll_timeout(readl, tcphy->base + PMA_CMN_CTRL1,
				 val, val & CMN_READY, 10,
				 PHY_MODE_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(tcphy->dev, "wait pma ready timeout\n");
		ret = -ETIMEDOUT;
		goto err_wait_pma;
	}

	reset_control_deassert(tcphy->pipe_rst);

	return 0;

err_wait_pma:
	reset_control_assert(tcphy->uphy_rst);
	reset_control_assert(tcphy->tcphy_rst);
	clk_disable_unprepare(tcphy->clk_ref);
err_clk_core:
	clk_disable_unprepare(tcphy->clk_core);
	return ret;
}

static void tcphy_phy_deinit(struct rockchip_typec_phy *tcphy)
{
	reset_control_assert(tcphy->tcphy_rst);
	reset_control_assert(tcphy->uphy_rst);
	reset_control_assert(tcphy->pipe_rst);
	clk_disable_unprepare(tcphy->clk_core);
	clk_disable_unprepare(tcphy->clk_ref);
}

static int tcphy_get_mode(struct rockchip_typec_phy *tcphy)
{
	struct extcon_dev *edev = tcphy->extcon;
	union extcon_property_value property;
	unsigned int id;
	bool dfp, ufp, dp;
	u8 mode;
	int ret;

	ufp = extcon_get_state(edev, EXTCON_USB);
	dfp = extcon_get_state(edev, EXTCON_USB_HOST);
	dp = extcon_get_state(edev, EXTCON_DISP_DP);

	mode = MODE_DFP_USB;
	id = EXTCON_USB_HOST;

	if (ufp) {
		mode = MODE_UFP_USB;
		id = EXTCON_USB;
	} else if (dp) {
		mode = MODE_DFP_DP;
		id = EXTCON_DISP_DP;

		ret = extcon_get_property(edev, id, EXTCON_PROP_USB_SS,
					  &property);
		if (ret) {
			dev_err(tcphy->dev, "get superspeed property failed\n");
			return ret;
		}

		if (property.intval)
			mode |= MODE_DFP_USB;
	}

	ret = extcon_get_property(edev, id, EXTCON_PROP_USB_TYPEC_POLARITY,
				  &property);
	if (ret) {
		dev_err(tcphy->dev, "get polarity property failed\n");
		return ret;
	}

	tcphy->flip = property.intval ? 1 : 0;

	return mode;
}

static int rockchip_usb3_phy_power_on(struct phy *phy)
{
	struct rockchip_typec_phy *tcphy = phy_get_drvdata(phy);
	struct rockchip_usb3phy_port_cfg *cfg = &tcphy->port_cfgs;
	const struct usb3phy_reg *reg = &cfg->pipe_status;
	int timeout, new_mode, ret = 0;
	u32 val;

	mutex_lock(&tcphy->lock);

	new_mode = tcphy_get_mode(tcphy);
	if (new_mode < 0) {
		ret = new_mode;
		goto unlock_ret;
	}

	/* DP-only mode; fall back to USB2 */
	if (!(new_mode & (MODE_DFP_USB | MODE_UFP_USB)))
		goto unlock_ret;

	if (tcphy->mode == new_mode)
		goto unlock_ret;

	if (tcphy->mode == MODE_DISCONNECT)
		tcphy_phy_init(tcphy, new_mode);

	/* wait TCPHY for pipe ready */
	for (timeout = 0; timeout < 100; timeout++) {
		regmap_read(tcphy->grf_regs, reg->offset, &val);
		if (!(val & BIT(reg->enable_bit))) {
			tcphy->mode |= new_mode & (MODE_DFP_USB | MODE_UFP_USB);
			goto unlock_ret;
		}
		usleep_range(10, 20);
	}

	if (tcphy->mode == MODE_DISCONNECT)
		tcphy_phy_deinit(tcphy);

	ret = -ETIMEDOUT;

unlock_ret:
	mutex_unlock(&tcphy->lock);
	return ret;
}

static int rockchip_usb3_phy_power_off(struct phy *phy)
{
	struct rockchip_typec_phy *tcphy = phy_get_drvdata(phy);

	mutex_lock(&tcphy->lock);

	if (tcphy->mode == MODE_DISCONNECT)
		goto unlock;

	tcphy->mode &= ~(MODE_UFP_USB | MODE_DFP_USB);
	if (tcphy->mode == MODE_DISCONNECT)
		tcphy_phy_deinit(tcphy);

unlock:
	mutex_unlock(&tcphy->lock);
	return 0;
}

static const struct phy_ops rockchip_usb3_phy_ops = {
	.power_on	= rockchip_usb3_phy_power_on,
	.power_off	= rockchip_usb3_phy_power_off,
	.owner		= THIS_MODULE,
};

static int rockchip_dp_phy_power_on(struct phy *phy)
{
	struct rockchip_typec_phy *tcphy = phy_get_drvdata(phy);
	int new_mode, ret = 0;
	u32 val;

	mutex_lock(&tcphy->lock);

	new_mode = tcphy_get_mode(tcphy);
	if (new_mode < 0) {
		ret = new_mode;
		goto unlock_ret;
	}

	if (!(new_mode & MODE_DFP_DP)) {
		ret = -ENODEV;
		goto unlock_ret;
	}

	if (tcphy->mode == new_mode)
		goto unlock_ret;

	/*
	 * If the PHY has been power on, but the mode is not DP only mode,
	 * re-init the PHY for setting all of 4 lanes to DP.
	 */
	if (new_mode == MODE_DFP_DP && tcphy->mode != MODE_DISCONNECT) {
		tcphy_phy_deinit(tcphy);
		tcphy_phy_init(tcphy, new_mode);
	} else if (tcphy->mode == MODE_DISCONNECT) {
		tcphy_phy_init(tcphy, new_mode);
	}

	ret = readx_poll_timeout(readl, tcphy->base + DP_MODE_CTL,
				 val, val & DP_MODE_A2, 1000,
				 PHY_MODE_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(tcphy->dev, "failed to wait TCPHY enter A2\n");
		goto power_on_finish;
	}

	tcphy_dp_aux_calibration(tcphy);

	writel(DP_MODE_ENTER_A0, tcphy->base + DP_MODE_CTL);

	ret = readx_poll_timeout(readl, tcphy->base + DP_MODE_CTL,
				 val, val & DP_MODE_A0, 1000,
				 PHY_MODE_SET_TIMEOUT);
	if (ret < 0) {
		writel(DP_MODE_ENTER_A2, tcphy->base + DP_MODE_CTL);
		dev_err(tcphy->dev, "failed to wait TCPHY enter A0\n");
		goto power_on_finish;
	}

	tcphy->mode |= MODE_DFP_DP;

power_on_finish:
	if (tcphy->mode == MODE_DISCONNECT)
		tcphy_phy_deinit(tcphy);
unlock_ret:
	mutex_unlock(&tcphy->lock);
	return ret;
}

static int rockchip_dp_phy_power_off(struct phy *phy)
{
	struct rockchip_typec_phy *tcphy = phy_get_drvdata(phy);

	mutex_lock(&tcphy->lock);

	if (tcphy->mode == MODE_DISCONNECT)
		goto unlock;

	tcphy->mode &= ~MODE_DFP_DP;

	writel(DP_MODE_ENTER_A2, tcphy->base + DP_MODE_CTL);

	if (tcphy->mode == MODE_DISCONNECT)
		tcphy_phy_deinit(tcphy);

unlock:
	mutex_unlock(&tcphy->lock);
	return 0;
}

static const struct phy_ops rockchip_dp_phy_ops = {
	.power_on	= rockchip_dp_phy_power_on,
	.power_off	= rockchip_dp_phy_power_off,
	.owner		= THIS_MODULE,
};

static int tcphy_get_param(struct device *dev,
			   struct usb3phy_reg *reg,
			   const char *name)
{
	u32 buffer[3];
	int ret;

	ret = of_property_read_u32_array(dev->of_node, name, buffer, 3);
	if (ret) {
		dev_err(dev, "Can not parse %s\n", name);
		return ret;
	}

	reg->offset = buffer[0];
	reg->enable_bit = buffer[1];
	reg->write_enable = buffer[2];
	return 0;
}

static int tcphy_parse_dt(struct rockchip_typec_phy *tcphy,
			  struct device *dev)
{
	struct rockchip_usb3phy_port_cfg *cfg = &tcphy->port_cfgs;
	int ret;

	ret = tcphy_get_param(dev, &cfg->typec_conn_dir,
			      "rockchip,typec-conn-dir");
	if (ret)
		return ret;

	ret = tcphy_get_param(dev, &cfg->usb3tousb2_en,
			      "rockchip,usb3tousb2-en");
	if (ret)
		return ret;

	ret = tcphy_get_param(dev, &cfg->external_psm,
			      "rockchip,external-psm");
	if (ret)
		return ret;

	ret = tcphy_get_param(dev, &cfg->pipe_status,
			      "rockchip,pipe-status");
	if (ret)
		return ret;

	tcphy->grf_regs = syscon_regmap_lookup_by_phandle(dev->of_node,
							  "rockchip,grf");
	if (IS_ERR(tcphy->grf_regs)) {
		dev_err(dev, "could not find grf dt node\n");
		return PTR_ERR(tcphy->grf_regs);
	}

	tcphy->clk_core = devm_clk_get(dev, "tcpdcore");
	if (IS_ERR(tcphy->clk_core)) {
		dev_err(dev, "could not get uphy core clock\n");
		return PTR_ERR(tcphy->clk_core);
	}

	tcphy->clk_ref = devm_clk_get(dev, "tcpdphy-ref");
	if (IS_ERR(tcphy->clk_ref)) {
		dev_err(dev, "could not get uphy ref clock\n");
		return PTR_ERR(tcphy->clk_ref);
	}

	tcphy->uphy_rst = devm_reset_control_get(dev, "uphy");
	if (IS_ERR(tcphy->uphy_rst)) {
		dev_err(dev, "no uphy_rst reset control found\n");
		return PTR_ERR(tcphy->uphy_rst);
	}

	tcphy->pipe_rst = devm_reset_control_get(dev, "uphy-pipe");
	if (IS_ERR(tcphy->pipe_rst)) {
		dev_err(dev, "no pipe_rst reset control found\n");
		return PTR_ERR(tcphy->pipe_rst);
	}

	tcphy->tcphy_rst = devm_reset_control_get(dev, "uphy-tcphy");
	if (IS_ERR(tcphy->tcphy_rst)) {
		dev_err(dev, "no tcphy_rst reset control found\n");
		return PTR_ERR(tcphy->tcphy_rst);
	}

	return 0;
}

static void typec_phy_pre_init(struct rockchip_typec_phy *tcphy)
{
	struct rockchip_usb3phy_port_cfg *cfg = &tcphy->port_cfgs;

	reset_control_assert(tcphy->tcphy_rst);
	reset_control_assert(tcphy->uphy_rst);
	reset_control_assert(tcphy->pipe_rst);

	/* select external psm clock */
	property_enable(tcphy, &cfg->external_psm, 1);
	property_enable(tcphy, &cfg->usb3tousb2_en, 0);

	tcphy->mode = MODE_DISCONNECT;
}

static int rockchip_typec_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child_np;
	struct rockchip_typec_phy *tcphy;
	struct phy_provider *phy_provider;
	struct resource *res;
	int ret;

	tcphy = devm_kzalloc(dev, sizeof(*tcphy), GFP_KERNEL);
	if (!tcphy)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tcphy->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(tcphy->base))
		return PTR_ERR(tcphy->base);

	ret = tcphy_parse_dt(tcphy, dev);
	if (ret)
		return ret;

	tcphy->dev = dev;
	platform_set_drvdata(pdev, tcphy);
	mutex_init(&tcphy->lock);

	typec_phy_pre_init(tcphy);

	tcphy->extcon = extcon_get_edev_by_phandle(dev, 0);
	if (IS_ERR(tcphy->extcon)) {
		if (PTR_ERR(tcphy->extcon) != -EPROBE_DEFER)
			dev_err(dev, "Invalid or missing extcon\n");
		return PTR_ERR(tcphy->extcon);
	}

	pm_runtime_enable(dev);

	for_each_available_child_of_node(np, child_np) {
		struct phy *phy;

		if (!of_node_cmp(child_np->name, "dp-port"))
			phy = devm_phy_create(dev, child_np,
					      &rockchip_dp_phy_ops);
		else if (!of_node_cmp(child_np->name, "usb3-port"))
			phy = devm_phy_create(dev, child_np,
					      &rockchip_usb3_phy_ops);
		else
			continue;

		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create phy: %s\n",
				child_np->name);
			return PTR_ERR(phy);
		}

		phy_set_drvdata(phy, tcphy);
	}

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "Failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	return 0;
}

static int rockchip_typec_phy_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id rockchip_typec_phy_dt_ids[] = {
	{ .compatible = "rockchip,rk3399-typec-phy" },
	{}
};

MODULE_DEVICE_TABLE(of, rockchip_typec_phy_dt_ids);

static struct platform_driver rockchip_typec_phy_driver = {
	.probe		= rockchip_typec_phy_probe,
	.remove		= rockchip_typec_phy_remove,
	.driver		= {
		.name	= "rockchip-typec-phy",
		.of_match_table = rockchip_typec_phy_dt_ids,
	},
};

module_platform_driver(rockchip_typec_phy_driver);

MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Kever Yang <kever.yang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USB TYPE-C PHY driver");
MODULE_LICENSE("GPL v2");
