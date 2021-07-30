/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _XHDMIPHY_H_
#define _XHDMIPHY_H_

#include <linux/types.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-hdmi.h>
#include <linux/mutex.h>

/* HDMIPHY core registers: general registers. */
#define XHDMIPHY_REFCLKSEL_REG			0x010
#define XHDMIPHY_COMMON_INIT_REG		0x014
#define XHDMIPHY_PLL_LOCK_STATUS_REG		0x018
#define XHDMIPHY_TX_INIT_REG			0x01c
#define XHDMIPHY_RX_INIT_REG			0x024
#define XHDMIPHY_POWERDOWN_CONTROL_REG		0x030
#define XHDMIPHY_DRP_CONTROL_CH1_REG		0x040
#define XHDMIPHY_DRP_STATUS_CH1_REG		0x050
#define XHDMIPHY_DRP_CONTROL_COMMON_REG		0x060
#define XHDMIPHY_DRP_STATUS_COMMON_REG		0x064
#define XHDMIPHY_DRP_CONTROL_TXMMCM_REG		0x124
#define XHDMIPHY_DRP_STATUS_TXMMCM_REG		0x128
#define XHDMIPHY_DRP_CONTROL_RXMMCM_REG		0x144
#define XHDMIPHY_DRP_STATUS_RXMMCM_REG		0x148
#define XHDMIPHY_CPLL_CAL_PERIOD_REG		0x068
#define XHDMIPHY_CPLL_CAL_TOL_REG		0x06c
#define XHDMIPHY_GT_DBG_GPI_REG			0x068
#define XHDMIPHY_GT_DBG_GPO_REG			0x06c
#define XHDMIPHY_TX_BUFFER_BYPASS_REG		0x074
#define XHDMIPHY_TX_DRIVER_CH12_REG		0x07c
#define XHDMIPHY_TX_DRIVER_CH34_REG		0x080
#define XHDMIPHY_TX_DRIVER_EXT_REG		0x084
#define XHDMIPHY_TX_RATE_CH12_REG		0x08c
#define XHDMIPHY_TX_RATE_CH34_REG		0x090
#define XHDMIPHY_RX_RATE_CH12_REG		0x98
#define XHDMIPHY_RX_RATE_CH34_REG		0x9c
#define XHDMIPHY_DRP_RXCDR_CFG(n)		(0x0e + (n))
#define XHDMIPHY_RX_CONTROL_REG			0x100
#define XHDMIPHY_RX_EQ_CDR_REG			0x108
#define XHDMIPHY_INTR_EN_REG			0x110
#define XHDMIPHY_INTR_DIS_REG			0x114
#define XHDMIPHY_INTR_STS_REG			0x11c
#define XHDMIPHY_MMCM_TXUSRCLK_CTRL_REG		0x0120
#define XHDMIPHY_BUFGGT_TXUSRCLK_REG		0x0134
#define XHDMIPHY_MISC_TXUSRCLK_REG		0x0138
#define XHDMIPHY_MMCM_RXUSRCLK_CTRL_REG		0x0140
#define XHDMIPHY_BUFGGT_RXUSRCLK_REG		0x0154
#define XHDMIPHY_MISC_RXUSRCLK_REG		0x0158
#define XHDMIPHY_CLKDET_CTRL_REG		0x0200
#define XHDMIPHY_CLKDET_FREQ_TMR_TO_REG		0x0208
#define XHDMIPHY_CLKDET_FREQ_TX_REG		0x020c
#define XHDMIPHY_CLKDET_FREQ_RX_REG		0x0210
#define XHDMIPHY_CLKDET_TMR_TX_REG		0x0214
#define XHDMIPHY_CLKDET_TMR_RX_REG		0x0218
#define XHDMIPHY_CLKDET_FREQ_DRU_REG		0x021c
#define XHDMIPHY_DRU_CTRL_REG			0x0300
#define XHDMIPHY_DRU_CFREQ_L_REG(ch)		(0x0308 + (12 * ((ch) - 1)))
#define XHDMIPHY_DRU_CFREQ_H_REG(ch)		(0x030C + (12 * ((ch) - 1)))
#define XHDMIPHY_PATGEN_CTRL_REG		0x0340

#define XHDMIPHY_INTR_STS_ALL_MASK		0xffffffff
#define XHDMIPHY_INTR_ALL_MASK		(XHDMIPHY_INTR_TXRESETDONE_MASK | \
					XHDMIPHY_INTR_RXRESETDONE_MASK | \
					XHDMIPHY_INTR_CPLL_LOCK_MASK | \
					XHDMIPHY_INTR_QPLL_LOCK_MASK | \
					XHDMIPHY_INTR_TXALIGNDONE_MASK | \
					XHDMIPHY_INTR_QPLL1_LOCK_MASK | \
					XHDMIPHY_INTR_TXFREQCHANGE_MASK | \
					XHDMIPHY_INTR_RXFREQCHANGE_MASK | \
					XHDMIPHY_INTR_TXMMCMUSRCLK_LOCK_MASK | \
					XHDMIPHY_INTR_RXMMCMUSRCLK_LOCK_MASK | \
					XHDMIPHY_INTR_TXTMRTIMEOUT_MASK | \
					XHDMIPHY_INTR_RXTMRTIMEOUT_MASK)

#define XHDMIPHY_DRU_REF_CLK_HZ		100000000
#define XHDMIPHY_MAX_LANES		4
#define VPHY_DEVICE_ID_BASE		256

#define XHDMIPHY_GTHE4				5
#define XHDMIPHY_GTYE4				6
#define XHDMIPHY_GTYE5				7
#define XHDMIPHY_REFCLKSEL_MAX			5

/* linerate ranges */
#define XHDMIPHY_LRATE_3400			3400

/* pll operating ranges */
#define XHDMIPHY_QPLL0_MIN			9800000000LL
#define XHDMIPHY_QPLL0_MAX			16375000000LL
#define XHDMIPHY_QPLL1_MIN			8000000000LL
#define XHDMIPHY_QPLL1_MAX			13000000000LL
#define XHDMIPHY_CPLL_MIN			2000000000LL
#define XHDMIPHY_CPLL_MAX			6250000000LL
#define XHDMIPHY_LCPLL_MIN_REFCLK		120000000LL
#define XHDMIPHY_RPLL_MIN_REFCLK		120000000LL

/* HDMI 2.1 GT linerates */
#define XHDMIPHY_LRATE_3G			3000000000
#define XHDMIPHY_LRATE_6G			6000000000
#define XHDMIPHY_LRATE_8G			8000000000
#define XHDMIPHY_LRATE_10G			10000000000
#define XHDMIPHY_LRATE_12G			12000000000

#define XHDMIPHY_HDMI14_REFCLK_RANGE1		119990000
#define XHDMIPHY_HDMI14_REFCLK_RANGE2		204687500
#define XHDMIPHY_HDMI14_REFCLK_RANGE3		298500000
#define XHDMIPHY_HDMI20_REFCLK_RANGE1		59400000
#define XHDMIPHY_HDMI20_REFCLK_RANGE2		84570000
#define XHDMIPHY_HDMI20_REFCLK_RANGE3		99000000
#define XHDMIPHY_HDMI20_REFCLK_RANGE4		102343750
#define XHDMIPHY_HDMI20_REFCLK_RANGE5		124990000
#define XHDMIPHY_HDMI20_REFCLK_RANGE6		149500000
#define XHDMIPHY_HDMI20_REFCLK_RANGE7		340000000

#define XHDMIPHY_HDMI_GTYE5_DRU_LRATE		2500000000U
#define XHDMIPHY_HDMI_GTYE5_DRU_REFCLK		200000000LL
#define XHDMIPHY_HDMI_GTYE5_DRU_REFCLK_MIN	199990000LL
#define XHDMIPHY_HDMI_GTYE5_DRU_REFCLK_MAX	200010000LL
#define XHDMIPHY_HDMI_GTYE5_DRU_REFCLK1		125000000LL
#define XHDMIPHY_HDMI_GTYE5_DRU_REFCLK1_MIN	124990000LL
#define XHDMIPHY_HDMI_GTYE5_DRU_REFCLK1_MAX	125010000LL
#define XHDMIPHY_HDMI_GTYE5_DRU_REFCLK2		400000000LL
#define XHDMIPHY_HDMI_GTYE5_DRU_REFCLK2_MIN	399990000LL
#define XHDMIPHY_HDMI_GTYE5_DRU_REFCLK2_MAX	400010000LL
#define XHDMIPHY_HDMI_GTYE5_LCPLL_REFCLK_MIN	120000000LL
#define XHDMIPHY_HDMI_GTYE5_RPLL_REFCLK_MIN	120000000LL
#define XHDMIPHY_HDMI_GTYE5_TX_MMCM_FVCO_MIN	2160000000U
#define XHDMIPHY_HDMI_GTYE5_TX_MMCM_FVCO_MAX	4320000000U
#define XHDMIPHY_HDMI_GTYE5_RX_MMCM_FVCO_MIN	2160000000U
#define XHDMIPHY_HDMI_GTYE5_RX_MMCM_FVCO_MAX	4320000000U
#define XHDMIPHY_HDMI_GTYE5_PLL_SCALE		1000
#define XHDMIPHY_HDMI_DEFAULT_VS_VAL		0x1f
#define XHDMIPHY_HDMI_DEFAULT_PC_PE_VAL		0x4
#define XHDMIPHY_HDMI_GTYE5_RX_MMCM_SCALE	1
#define XHDMIPHY_HDMI_GTYE5_TX_MMCM_SCALE	1

#define XHDMIPHY_HDMI_GTYE4_DRU_LRATE		2500000000U
#define XHDMIPHY_HDMI_GTYE4_DRU_REFCLK		156250000LL
#define XHDMIPHY_HDMI_GTYE4_DRU_REFCLK_MIN	156240000LL
#define XHDMIPHY_HDMI_GTYE4_DRU_REFCLK_MAX	156260000LL
#define XHDMIPHY_HDMI_GTYE4_DRU_REFCLK2		400000000LL
#define XHDMIPHY_HDMI_GTYE4_DRU_REFCLK2_MIN	399990000LL
#define XHDMIPHY_HDMI_GTYE4_DRU_REFCLK2_MAX	400010000LL
#define XHDMIPHY_HDMI_GTYE4_QPLL0_REFCLK_MIN	61250000LL
#define XHDMIPHY_HDMI_GTYE4_QPLL1_REFCLK_MIN	50000000LL
#define XHDMIPHY_HDMI_GTYE4_CPLL_REFCLK_MIN	50000000LL
#define XHDMIPHY_HDMI_GTYE4_TX_MMCM_FVCO_MIN	800000000U
#define XHDMIPHY_HDMI_GTYE4_TX_MMCM_FVCO_MAX	1600000000U
#define XHDMIPHY_HDMI_GTYE4_RX_MMCM_FVCO_MIN	800000000U
#define XHDMIPHY_HDMI_GTYE4_RX_MMCM_FVCO_MAX	1600000000U
#define XHDMIPHY_HDMI_GTYE4_PLL_SCALE		1000
#define XHDMIPHY_HDMI_GTYE4_RX_MMCM_SCALE	1
#define XHDMIPHY_HDMI_GTYE4_TX_MMCM_SCALE	1

#define XHDMIPHY_HDMI_GTHE4_DRU_LRATE		2500000000U
#define XHDMIPHY_HDMI_GTHE4_DRU_REFCLK		156250000LL
#define XHDMIPHY_HDMI_GTHE4_DRU_REFCLK_MIN	156240000LL
#define XHDMIPHY_HDMI_GTHE4_DRU_REFCLK_MAX	156260000LL
#define XHDMIPHY_HDMI_GTHE4_DRU_REFCLK2		400000000LL
#define XHDMIPHY_HDMI_GTHE4_DRU_REFCLK2_MIN	399980000LL
#define XHDMIPHY_HDMI_GTHE4_DRU_REFCLK2_MAX	400020000LL
#define XHDMIPHY_HDMI_GTHE4_QPLL0_REFCLK_MIN	61250000LL
#define XHDMIPHY_HDMI_GTHE4_QPLL1_REFCLK_MIN	50000000LL
#define XHDMIPHY_HDMI_GTHE4_CPLL_REFCLK_MIN	50000000LL
#define XHDMIPHY_HDMI_GTHE4_TX_MMCM_FVCO_MIN	800000000U
#define XHDMIPHY_HDMI_GTHE4_TX_MMCM_FVCO_MAX	1600000000U
#define XHDMIPHY_HDMI_GTHE4_RX_MMCM_FVCO_MIN	800000000U
#define XHDMIPHY_HDMI_GTHE4_RX_MMCM_FVCO_MAX	1600000000U
#define XHDMIPHY_HDMI21_FRL_REFCLK		400000000U
#define XHDMIPHY_HDMI_GTHE4_DEFAULT_VS_VAL	0xb
#define XHDMIPHY_HDMI_GTHE4_PLL_SCALE		1000
#define XHDMIPHY_HDMI_GTHE4_RX_MMCM_SCALE	1
#define XHDMIPHY_HDMI_GTHE4_TX_MMCM_SCALE	1

/* 0x010: reference clock selections */
#define XHDMIPHY_REFCLKSEL_QPLL0_MASK		0x0000000f
#define XHDMIPHY_REFCLKSEL_CPLL_MASK		0x000000f0
#define XHDMIPHY_REFCLKSEL_CPLL_SHIFT		4
#define XHDMIPHY_REFCLKSEL_QPLL1_MASK		0x00000f00
#define XHDMIPHY_REFCLKSEL_QPLL1_SHIFT		8
#define XHDMIPHY_REFCLKSEL_SYSCLKSEL_MASK	0x0f000000
#define XHDMIPHY_RXSYSCLKSEL_OUT_MASK(_G) ({ \
		typeof(_G) (G) = (_G); \
		((((G) == XHDMIPHY_GTTYPE_GTHE4) || \
		 ((G) == XHDMIPHY_GTTYPE_GTYE4)) ? 0x03000000 : 0x02000000); })
#define XHDMIPHY_TXSYSCLKSEL_OUT_MASK(_G) ({ \
		typeof(_G) (G) = (_G); \
		((((G) == XHDMIPHY_GTTYPE_GTHE4) || \
		 ((G) == XHDMIPHY_GTTYPE_GTYE4)) ? 0x0C000000 : 0x08000000); })
#define XHDMIPHY_RXSYSCLKSEL_DATA_MASK(_G) ({ \
		typeof(_G) (G) = (_G); \
		((((G) == XHDMIPHY_GTTYPE_GTHE4) || \
		 ((G) == XHDMIPHY_GTTYPE_GTYE4)) ? 0x30000000 : 0x01000000); })
#define XHDMIPHY_TXSYSCLKSEL_DATA_MASK(_G) ({ \
		typeof(_G) (G) = (_G); \
		((((G) == XHDMIPHY_GTTYPE_GTHE4) || \
		 ((G) == XHDMIPHY_GTTYPE_GTYE4)) ? 0xC0000000 : 0x04000000); })
#define XHDMIPHY_RXSYSCLKSEL_OUT_SHIFT(_G) ({ \
		typeof(_G) (G) = (_G); \
		((((G) == XHDMIPHY_GTTYPE_GTHE4) || \
		 ((G) == XHDMIPHY_GTTYPE_GTYE4)) ? 24 : 25); })
#define XHDMIPHY_TXSYSCLKSEL_OUT_SHIFT(_G) ({ \
		typeof(_G) (G) = (_G); \
		((((G) == XHDMIPHY_GTTYPE_GTHE4) || \
		 ((G) == XHDMIPHY_GTTYPE_GTYE4)) ? 26 : 27); })
#define XHDMIPHY_RXSYSCLKSEL_DATA_SHIFT(_G) ({ \
		typeof(_G) (G) = (_G); \
		((((G) == XHDMIPHY_GTTYPE_GTHE4) || \
		 ((G) == XHDMIPHY_GTTYPE_GTYE4)) ? 28 : 24); })
#define XHDMIPHY_TXSYSCLKSEL_DATA_SHIFT(_G) ({ \
		typeof(_G) (G) = (_G); \
		((((G) == XHDMIPHY_GTTYPE_GTHE4) || \
		 ((G) == XHDMIPHY_GTTYPE_GTYE4)) ? 30 : 26); })

/* 0x018: pll lock status */
#define XHDMIPHY_PLL_LOCK_STATUS_CPLL_MASK(ch)		(0x01 << ((ch) - 1))
#define XHDMIPHY_PLL_LOCK_STATUS_QPLL0_MASK		0x10
#define XHDMIPHY_PLL_LOCK_STATUS_QPLL1_MASK		0x20
#define XHDMIPHY_PLL_LOCK_STATUS_CPLL_ALL_MASK \
		(XHDMIPHY_PLL_LOCK_STATUS_CPLL_MASK(XHDMIPHY_CHID_CH1) | \
		 XHDMIPHY_PLL_LOCK_STATUS_CPLL_MASK(XHDMIPHY_CHID_CH2) | \
		 XHDMIPHY_PLL_LOCK_STATUS_CPLL_MASK(XHDMIPHY_CHID_CH3) | \
		 XHDMIPHY_PLL_LOCK_STATUS_CPLL_MASK(XHDMIPHY_CHID_CH4))
#define XHDMIPHY_PLL_LOCK_STATUS_CPLL_HDMI_MASK \
		(XHDMIPHY_PLL_LOCK_STATUS_CPLL_MASK(XHDMIPHY_CHID_CH1) | \
		 XHDMIPHY_PLL_LOCK_STATUS_CPLL_MASK(XHDMIPHY_CHID_CH2) | \
		 XHDMIPHY_PLL_LOCK_STATUS_CPLL_MASK(XHDMIPHY_CHID_CH3))
#define XHDMIPHY_PLL_LOCK_STATUS_RPLL_MASK		0xc0
#define XHDMIPHY_PLL_LOCK_STATUS_LCPLL_MASK		0x300
/* 0x01C, 0x024: TX_INIT, RX_INIT */
#define XHDMIPHY_TXRX_INIT_GTRESET_MASK(ch)	(0x01 << (8 * ((ch) - 1)))
#define XHDMIPHY_TXRX_MSTRESET_MASK(ch)		(0x20 << (8 * ((ch) - 1)))
#define XHDMIPHY_TXRX_INIT_PLLGTRESET_MASK(ch)	(0x80 << (8 * ((ch) - 1)))

#define XHDMIPHY_TXRX_INIT_GTRESET_ALL_MASK \
		(XHDMIPHY_TXRX_INIT_GTRESET_MASK(XHDMIPHY_CHID_CH1) | \
		 XHDMIPHY_TXRX_INIT_GTRESET_MASK(XHDMIPHY_CHID_CH2) | \
		 XHDMIPHY_TXRX_INIT_GTRESET_MASK(XHDMIPHY_CHID_CH3) | \
		 XHDMIPHY_TXRX_INIT_GTRESET_MASK(XHDMIPHY_CHID_CH4))
#define XHDMIPHY_TXRX_INIT_PLLGTRESET_ALL_MASK \
		(XHDMIPHY_TXRX_INIT_PLLGTRESET_MASK(XHDMIPHY_CHID_CH1) | \
		 XHDMIPHY_TXRX_INIT_PLLGTRESET_MASK(XHDMIPHY_CHID_CH2) | \
		 XHDMIPHY_TXRX_INIT_PLLGTRESET_MASK(XHDMIPHY_CHID_CH3) | \
		 XHDMIPHY_TXRX_INIT_PLLGTRESET_MASK(XHDMIPHY_CHID_CH4))
#define XHDMIPHY_RXPCS_RESET_MASK			0x10101010
#define XHDMIPHY_TXPCS_RESET_MASK			0x10101010

/* 0x02C: IBUFDS_GTXX_CTRL */
#define XHDMIPHY_IBUFDS_GTXX_CTRL_GTREFCLK0_CEB_MASK	0x1
#define XHDMIPHY_IBUFDS_GTXX_CTRL_GTREFCLK1_CEB_MASK	0x2

/* 0x030: power down control */
#define XHDMIPHY_POWERDOWN_CONTROL_CPLLPD_MASK(ch)	(0x01 << (8 * ((ch) - 1)))
#define XHDMIPHY_POWERDOWN_CONTROL_QPLL0PD_MASK(ch)	(0x02 << (8 * ((ch) - 1)))
#define XHDMIPHY_POWERDOWN_CONTROL_QPLL1PD_MASK(ch)	(0x04 << (8 * ((ch) - 1)))

/* 0x040, 0x044, 0x048, 0x04C, 0x060: DRP_CONTROL_CH[1-4], DRP_CONTROL_COMMON */
#define XHDMIPHY_DRP_CONTROL_DRPADDR_MASK	0x00000ff
#define XHDMIPHY_DRP_CONTROL_DRPEN_MASK		0x00001000
#define XHDMIPHY_DRP_CONTROL_DRPWE_MASK		0x00002000
#define XHDMIPHY_DRP_CONTROL_DRPDI_MASK		0xffff0000
#define XHDMIPHY_DRP_CONTROL_DRPDI_SHIFT	16

/* 0x050, 0x054, 0x058, 0x05C, 0x064: DRP_STATUS_CH[1-4], DRP_STATUS_COMMON */
#define XHDMIPHY_DRP_STATUS_DRPO_MASK		0x0ffff
#define XHDMIPHY_DRP_STATUS_DRPRDY_MASK		0x10000
#define XHDMIPHY_DRP_STATUS_DRPBUSY_MASK	0x20000

/* 0x068: cpll cal period */
#define XHDMIPHY_CPLL_CAL_PERIOD_MASK		0x3ffff

/* 0x06C: cpll cal tolerance */
#define XHDMIPHY_CPLL_CAL_TOL_MASK		0x3ffff

/* 0x068: gpi */
#define XHDMIPHY_TX_GPI_MASK(ch)		(0x01 << ((ch) - 1))
#define XHDMIPHY_RX_GPI_MASK(ch)		(0x10 << ((ch) - 1))

/* 0x06C: gpo */
#define XHDMIPHY_TX_GPO_MASK(ch)		(0x01 << ((ch) - 1))
#define XHDMIPHY_TX_GPO_MASK_ALL(nch)		(((nch) == 3) ? 0x7 : 0xf)
#define XHDMIPHY_TX_GPO_SHIFT			0

#define XHDMIPHY_RX_GPO_MASK(ch)		(0x10 << ((ch) - 1))
#define XHDMIPHY_RX_GPO_MASK_ALL(nch)		(((nch) == 3) ? 0x70 : 0xf0)
#define XHDMIPHY_RX_GPO_SHIFT			4

/* 0x074: Tx buffer bypass */
#define XHDMIPHY_TX_BUFFER_BYPASS_TXPHDLYRESET_MASK(ch) \
						(0x01 << (8 * ((ch) - 1)))
#define XHDMIPHY_TX_BUFFER_BYPASS_TXPHALIGN_MASK(ch) \
						(0x02 << (8 * ((ch) - 1)))

/* 0x07c, 0x080: TX_DRIVER_CH12, TX_DRIVER_CH34 */
#define XHDMIPHY_TX_TXDIFFCTRL_MASK	0xf
#define XHDMIPHY_TX_DRIVER_TXDIFFCTRL_MASK(ch) \
				(0x000F << (16 * (((ch) - 1) % 2)))
#define XHDMIPHY_TX_DRIVER_TXDIFFCTRL_SHIFT(ch) \
				(16 * (((ch) - 1) % 2))
#define XHDMIPHY_TX_DRIVER_TXPOSTCURSOR_MASK(ch) \
				(0x07C0 << (16 * (((ch) - 1) % 2)))
#define XHDMIPHY_TX_DRIVER_TXPOSTCURSOR_SHIFT(ch) \
				(6 + (16 * (((ch) - 1) % 2)))
#define XHDMIPHY_TX_DRIVER_TXPRECURSOR_MASK(ch) \
				(0xF800 << (16 * (((ch) - 1) % 2)))
#define XHDMIPHY_TX_DRIVER_TXPRECURSOR_SHIFT(ch) \
				(11 + (16 * (((ch) - 1) % 2)))

/* 0x084: Tx driver exit */
#define XHDMIPHY_TX_EXT_TXDIFFCTRL_MASK	0x10
#define XHDMIPHY_TX_DRIVER_EXT_TXDIFFCTRL_MASK(ch) \
						(0x0001 << (8 * ((ch) - 1)))
#define XHDMIPHY_TX_DRIVER_EXT_TXDIFFCTRL_SHIFT(ch) \
						(8 * ((ch) - 1))

/* 0x08C, 0x090: tx rate ch12, tx rate ch34 */
#define XHDMIPHY_TX_RATE_MASK(ch)	(0x00ff << (16 * (((ch) - 1) % 2)))
#define XHDMIPHY_TX_RATE_SHIFT(ch)	(16 * (((ch) - 1) % 2))

/* 0x098, 0x09C: rx rate ch12, rx rate ch34 */
#define XHDMIPHY_RX_RATE_MASK(ch)	(0x00ff << (16 * (((ch) - 1) % 2)))
#define XHDMIPHY_RX_RATE_SHIFT(ch)	(16 * (((ch) - 1) % 2))

/* 0x104: rx eq cdr */
#define XHDMIPHY_RX_CONTROL_RXLPMEN_MASK(ch)	(0x01 << (8 * ((ch) - 1)))
#define XHDMIPHY_RX_STATUS_RXCDRHOLD_MASK(ch)	(0x02 << (8 * ((ch) - 1)))
#define XHDMIPHY_RX_STATUS_RXOSOVRDEN_MASK(ch)	(0x04 << (8 * ((ch) - 1)))
#define XHDMIPHY_RX_STATUS_RXLPMLFKLOVRDEN_MASK(ch) \
						(0x08 << (8 * ((ch) - 1)))
#define XHDMIPHY_RX_STATUS_RXLPMHFOVRDEN_MASK(ch) \
						(0x10 << (8 * ((ch) - 1)))
#define XHDMIPHY_RX_CONTROL_RXLPMEN_ALL_MASK \
		(XHDMIPHY_RX_CONTROL_RXLPMEN_MASK(XHDMIPHY_CHID_CH1) | \
		 XHDMIPHY_RX_CONTROL_RXLPMEN_MASK(XHDMIPHY_CHID_CH2) | \
		 XHDMIPHY_RX_CONTROL_RXLPMEN_MASK(XHDMIPHY_CHID_CH3) | \
		 XHDMIPHY_RX_CONTROL_RXLPMEN_MASK(XHDMIPHY_CHID_CH4))

/* 0x110, 0x114, 0x118, 0x11c: INTR_EN, INTR_DIS, INTR_MASK, INTR_STS */
#define XHDMIPHY_INTR_TXRESETDONE_MASK			BIT(0)
#define XHDMIPHY_INTR_RXRESETDONE_MASK			BIT(1)
#define XHDMIPHY_INTR_CPLL_LOCK_MASK			BIT(2)
#define XHDMIPHY_INTR_QPLL0_LOCK_MASK			BIT(3)
#define XHDMIPHY_INTR_LCPLL_LOCK_MASK			BIT(3)
#define XHDMIPHY_INTR_TXALIGNDONE_MASK			BIT(4)
#define XHDMIPHY_INTR_QPLL1_LOCK_MASK			BIT(5)
#define XHDMIPHY_INTR_RPLL_LOCK_MASK			BIT(5)
#define XHDMIPHY_INTR_TXFREQCHANGE_MASK			BIT(6)
#define XHDMIPHY_INTR_RXFREQCHANGE_MASK			BIT(7)
#define XHDMIPHY_INTR_TXMMCMUSRCLK_LOCK_MASK		BIT(9)
#define XHDMIPHY_INTR_RXMMCMUSRCLK_LOCK_MASK		BIT(10)
#define XHDMIPHY_INTR_TXGPO_RE_MASK			BIT(11)
#define XHDMIPHY_INTR_RXGPO_RE_MASK			BIT(12)
#define XHDMIPHY_INTR_TXTMRTIMEOUT_MASK			BIT(30)
#define XHDMIPHY_INTR_RXTMRTIMEOUT_MASK			BIT(31)
#define XHDMIPHY_INTR_QPLL_LOCK_MASK		XHDMIPHY_INTR_QPLL0_LOCK_MASK

/* 0x120, 0x140: MMCM_TXUSRCLK_CTRL, MMCM_RXUSRCLK_CTRL */
#define XHDMIPHY_MMCM_USRCLK_CTRL_RST_MASK		BIT(1)
#define XHDMIPHY_MMCM_USRCLK_CTRL_LOCKED_MASK		BIT(9)
#define XHDMIPHY_MMCM_USRCLK_CTRL_PWRDWN_MASK		BIT(10)
#define XHDMIPHY_MMCM_USRCLK_CTRL_LOCKED_MASK_MASK	BIT(11)
#define XHDMIPHY_MMCM_USRCLK_CTRL_CLKINSEL_MASK		BIT(12)

#define XHDMIPHY_BUFGGT_XXUSRCLK_DIV_MASK		GENMASK(3, 1)
#define XHDMIPHY_BUFGGT_XXUSRCLK_DIV_SHIFT		1

/* 0x138, 0x158: MISC_TXUSRCLK_REG, MISC_RXUSERCLK_REG */
#define XHDMIPHY_MISC_XXUSRCLK_CKOUT1_OEN_MASK		BIT(0)
#define XHDMIPHY_MISC_XXUSRCLK_REFCLK_CEB_MASK		BIT(1)

/* 0x200: clock detector control */
#define XHDMIPHY_CLKDET_CTRL_RUN_MASK			BIT(0)
#define XHDMIPHY_CLKDET_CTRL_TX_TMR_CLR_MASK		BIT(1)
#define XHDMIPHY_CLKDET_CTRL_RX_TMR_CLR_MASK		BIT(2)
#define XHDMIPHY_CLKDET_CTRL_RX_FREQ_RST_MASK		BIT(4)
#define XHDMIPHY_CLKDET_CTRL_FREQ_LOCK_THRESH_SHIFT	5
/* 0x300: dru control */
#define XHDMIPHY_DRU_CTRL_RST_MASK(ch)			(0x01 << (8 * ((ch) - 1)))
#define XHDMIPHY_DRU_CTRL_EN_MASK(ch)			(0x02 << (8 * ((ch) - 1)))
/* 0x30C, 0x318, 0x324, 0x330: DRU_CFREQ_H_CH[1-4] */
#define XHDMIPHY_DRU_CFREQ_H_MASK		0x1f

/* 0x340 TMDS PATGEN */
#define XHDMIPHY_PATGEN_CTRL_ENABLE_MASK	0x80000000
#define XHDMIPHY_PATGEN_CTRL_RATIO_MASK		0x7

#define XHDMIPHY_CH2IDX(id)		((id) - XHDMIPHY_CHID_CH1)
#define XHDMIPHY_ISTXMMCM(id)		((id) == XHDMIPHY_CHID_TXMMCM)
#define XHDMIPHY_ISRXMMCM(id)		((id) == XHDMIPHY_CHID_RXMMCM)

#define xhdmiphy_is_tx_using_cpll(inst, chid) ({ \
			(XHDMIPHY_PLL_CPLL == \
			 xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_TX, chid)); })
#define xhdmiphy_is_rx_using_cpll(inst, chid) ({ \
			(XHDMIPHY_PLL_CPLL == \
			 xhdmiphy_get_pll_type(inst, XHDMIPHY_DIR_RX, chid)); })

/* Following Data is written in to the DRP addresses.
 * To know the offsets, affected bit positions
 * and other details, please refer GT Userguide
 *
 */

/* masks and registers of GTHE4 DRP */
#define XDRP_GTHE4_CHN_REG_0028		0x0028
#define XDRP_GTHE4_CHN_REG_002A		0x002a
#define XDRP_GTHE4_CHN_REG_00CB		0x00cb
#define XDRP_GTHE4_CHN_REG_00CC		0x00cc
#define XDRP_GTHE4_CHN_REG_00BC		0x00bc
#define XDRP_GTHE4_CHN_REG_0063		0x0063
#define XDRP_GTHE4_CHN_REG_006D		0x006d
#define XDRP_GTHE4_CHN_REG_007A		0x007a
#define XDRP_GTHE4_CHN_REG_007C		0x007c
#define XDRP_GTHE4_CHN_REG_0011		0x0011
#define XDRP_GTHE4_CHN_REG_00AF		0x00af
#define XDRP_GTHE4_CHN_REG_0066		0x0066
#define XDRP_GTHE4_CHN_REG_0003		0x0003
#define XDRP_GTHE4_CHN_REG_0116		0x0116
#define XDRP_GTHE4_CHN_REG_00FB		0x00fb
#define XDRP_GTHE4_CHN_REG_009D		0x009d
#define XDRP_GTHE4_CHN_REG_0100		0x0100
#define XDRP_GTHE4_CHN_REG_003E		0x003e
#define XDRP_GTHE4_CHN_REG_0085		0x0085
#define XDRP_GTHE4_CHN_REG_0073		0x0073
#define XDRP_GTHE4_CHN_REG_00FF		0x00ff
#define XDRP_GTHE4_CHN_REG_009C		0x009c

#define XDRP_GTHE4_CHN_REG_0063_RXOUT_DIV_MASK			0x07
#define XDRP_GTHE4_CHN_REG_0063_FLD_RXOUT_DIV_MASK		0x7
#define XDRP_GTHE4_CHN_REG_007C_TXOUT_DIV_MASK			0x700
#define XDRP_GTHE4_CHN_REG_007C_FLD_TX_RXDETECT_REF_MASK	0x7
#define XDRP_GTHE4_CHN_REG_0028_FLD_CPLL_FBDIV_MASK		0xff
#define XDRP_GTHE4_CHN_REG_0028_FLD_CPLL_FBDIV_SHIFT		0x8
#define XDRP_GTHE4_CHN_REG_0028_FLD_CPLL_FBDIV_45_MASK		0x1
#define XDRP_GTHE4_CHN_REG_0028_FLD_CPLL_FBDIV_45_SHIFT		0x7
#define XDRP_GTHE4_CHN_REG_002A_FLD_A_TXDIFFCTRL_MASK		0x1f
#define XDRP_GTHE4_CHN_REG_002A_FLD_A_TXDIFFCTRL_SHIFT		0x11
#define XDRP_GTHE4_CHN_REG_0028_CPLL_FBDIV_MASK			0xff80
#define XDRP_GTHE4_CHN_REG_002A_CPLL_REFCLK_DIV_MASK		0xf800
#define XDRP_GTHE4_CHN_REG_003E_DRP_VAL1			57442
#define XDRP_GTHE4_CHN_REG_003E_DRP_VAL2			57415
#define XDRP_GTHE4_CHN_REG_0066_RX_INT_DATAWIDTH_MASK		0xf
#define XDRP_GTHE4_CHN_REG_0003_RX_DATAWIDTH_MASK		0x1e0
#define XDRP_GTHE4_CHN_REG_0003_RX_DATAWIDTH_ENC_MASK		0xf
#define XDRP_GTHE4_CHN_REG_0003_RX_DATAWIDTH_ENC_SHIFT		5
#define XDRP_GTHE4_CHN_REG_0116_CH_RX_HSPMUX_MASK		0x00ff
#define XDRP_GTHE4_CHN_REG_00FB_PREIQ_FREQ_BST_MASK		0x0030
#define XDRP_GTHE4_CHN_REG_00FB_TXPI_BIASSET_MASK		0x0006
#define XDRP_GTHE4_CHN_REG_009C_TXPI_CFG3_CFG4_MASK		0x0060
#define XDRP_GTHE4_CHN_REG_0116_CH_TX_HSPMUX_MASK		0xff00
#define XDRP_GTHE4_CHN_REG_007A_TXCLK25_MASK			0xf800
#define XDRP_GTHE4_CHN_REG_007A_TXCLK25_SHIFT			11
#define XDRP_GTHE4_CHN_REG_006D_RXCLK25_MASK			0x00f8
#define XDRP_GTHE4_CHN_REG_0066_RX_WIDEMODE_CDR_MASK_VAL	0x3
#define XDRP_GTHE4_CHN_REG_007A_TX_DATA_WIDTH_MASK		0xf
#define XDRP_GTHE4_CHN_REG_0085_TX_INT_DATAWIDTH_MASK		0x3
#define XDRP_GTHE4_CHN_REG_0085_TX_INT_DATAWIDTH_SHIFT		10
#define XDRP_GTHE4_CHN_REG_00AF_RXCDR_CGF2_GEN2_MASK		0x3ff
#define XDRP_GTHE4_CHN_REG_0011_RXCDR_CGF3_GEN2_MASK		0x3f
#define XDRP_GTHE4_CHN_REG_0011_RXCDR_CGF3_GEN2_SHIFT		10
#define XDRP_GTHE4_CHN_REG_0066_RX_WIDEMODE_CDR_MASK		0xc

#define XDRP_GTHE4_CMN_REG_0014		0x0014
#define XDRP_GTHE4_CMN_REG_0018		0x0018
#define XDRP_GTHE4_CMN_REG_0094		0x0094
#define XDRP_GTHE4_CMN_REG_0098		0x0098
#define XDRP_GTHE4_CMN_REG_008D		0x008d
#define XDRP_GTHE4_CMN_REG_0016		0x0016
#define XDRP_GTHE4_CMN_REG_000D		0x000d
#define XDRP_GTHE4_CMN_REG_0096		0x0096
#define XDRP_GTHE4_CMN_REG_0019		0x0019
#define XDRP_GTHE4_CMN_REG_0099		0x0099
#define XDRP_GTHE4_CMN_REG_0030		0x0030
#define XDRP_GTHE4_CMN_REG_00B0		0x00b0

#define XDRP_GTHE4_CMN_REG_0014_FLD_QPLL0_INIT_CFG1_MASK	0xff
#define XDRP_GTHE4_CMN_REG_0018_QPLLX_REFCLK_DIV_MASK		0xf80
#define XDRP_GTHE4_CMN_REG_0018_QPLLX_REFCLK_DIV_MASK1		0x1f
#define XDRP_GTHE4_CMN_REG_0018_QPLLX_REFCLK_DIV_SHIFT		0x6
#define XDRP_GTHE4_CMN_REG_0018_QPLLX_REFCLK_DIV_SHIFT1		0x7
#define XDRP_GTHE4_CMN_REG_000D_PPFX_CFG_MASK			0x0fc0
#define XDRP_GTHE4_CMN_REG_0019_QPLLX_LPF_MASK			0x0003
#define XDRP_GTHE4_CMN_REG_0030_QPLLX_CFG4_MASK			0x00e7

#define XHDMIPHY_DRP_CPLL_VCO_RANGE1		3000
#define XHDMIPHY_DRP_CPLL_VCO_RANGE2		4250
#define XHDMIPHY_DRP_CPLL_CFG0_VAL1		0x01fa
#define XHDMIPHY_DRP_CPLL_CFG0_VAL2		0x0ffa
#define XHDMIPHY_DRP_CPLL_CFG0_VAL3		0x03fe
#define XHDMIPHY_DRP_CPLL_CFG1_VAL1		0x0023
#define XHDMIPHY_DRP_CPLL_CFG1_VAL2		0x0021
#define XHDMIPHY_DRP_CPLL_CFG2_VAL1		0x0002
#define XHDMIPHY_DRP_CPLL_CFG2_VAL2		0x0202
#define XHDMIPHY_DRP_CPLL_CFG2_VAL3		0x0203
#define XHDMIPHY_DRP_QPLL_VCO_RANGE1		15000
#define XHDMIPHY_DRP_QPLL_VCO_RANGE2		13000
#define XHDMIPHY_DRP_QPLL_VCO_RANGE3		11000
#define XHDMIPHY_DRP_QPLL_VCO_RANGE4		7000
#define XHDMIPHY_DRP_QPLL_NFBDIV		40
#define XHDMIPHY_DRP_QPLL_CP_VAL1		0x007f
#define XHDMIPHY_DRP_QPLL_CP_VAL2		0x03ff
#define XHDMIPHY_DRP_QPLL_LPF_VAL1		0x3
#define XHDMIPHY_DRP_QPLL_LPF_VAL2		0x1
#define XHDMIPHY_DRP_QPLL_CLKOUT_RANGE1		7500
#define XHDMIPHY_DRP_QPLL_CLKOUT_RANGE2		3500
#define XHDMIPHY_DRP_QPLL_CLKOUT_RANGE3		5500
#define XHDMIPHY_DRP_PPF_MUX_CRNT_CTRL0_VAL1	0x0e00
#define XHDMIPHY_DRP_PPF_MUX_CRNT_CTRL0_VAL2	0x0800
#define XHDMIPHY_DRP_PPF_MUX_CRNT_CTRL0_VAL3	0x0600
#define XHDMIPHY_DRP_PPF_MUX_CRNT_CTRL0_VAL4	0x0400
#define XHDMIPHY_DRP_PPF_MUX_TERM_CTRL0_VAL1	0x0100
#define XHDMIPHY_DRP_PPF_MUX_TERM_CTRL0_VAL2	0x0000
#define XHDMIPHY_DRP_Q_TERM_CLK_VAL1		0x2
#define XHDMIPHY_DRP_Q_TERM_CLK_VAL2		0x0
#define XHDMIPHY_DRP_Q_TERM_CLK_VAL3		0x6
#define XHDMIPHY_DRP_Q_DCRNT_CLK_VAL1		0x5
#define XHDMIPHY_DRP_Q_DCRNT_CLK_VAL2		0x4
#define XHDMIPHY_DRP_Q_DCRNT_CLK_VAL3		0x3
#define XHDMIPHY_DRP_Q_DCRNT_CLK_SHIFT		5
#define XHDMIPHY_DRP_LINERATEKHZ_1		16400000
#define XHDMIPHY_DRP_LINERATEKHZ_2		10400000
#define XHDMIPHY_DRP_LINERATEKHZ_3		10000000
#define XHDMIPHY_DRP_LINERATEKHZ_4		20000000
#define XHDMIPHY_DRP_LINERATEKHZ_5		16375000
#define XHDMIPHY_DRP_LINERATEKHZ_6		8000000
#define XHDMIPHY_DRP_RXCDR_CFG_WORD3_VAL1	0x0010
#define XHDMIPHY_DRP_RXCDR_CFG_WORD3_VAL2	0x0018
#define XHDMIPHY_DRP_RXCDR_CFG_WORD3_VAL3	0x0012
#define XHDMIPHY_DRP_PREIQ_FREQ_BST_VAL1	3
#define XHDMIPHY_DRP_PREIQ_FREQ_BST_VAL2	2
#define XHDMIPHY_DRP_PREIQ_FREQ_BST_VAL3	1
#define XHDMIPHY_DRP_PREIQ_FREQ_BST_SHIFT	4
#define XHDMIPHY_DRP_TXOUT_OFFSET		8

#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE1		7500
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE2		3500
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE3		5500
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE4		14110
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE5		14000
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE8		7000
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE9		6500
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE10		5500
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE11		5156
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE12		4500
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE13		4000
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE14		3500
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE15		3000
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE16		2500
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE17		7500
#define XHDMIPHY_DRP_PLL_CLKOUT_RANGE18		2000

#define XHDMIPHY_DRP_RXPI_CFG0_VAL1		0x0004
#define XHDMIPHY_DRP_RXPI_CFG0_VAL2		0x0104
#define XHDMIPHY_DRP_RXPI_CFG0_VAL3		0x2004
#define XHDMIPHY_DRP_RXPI_CFG0_VAL4		0x0002
#define XHDMIPHY_DRP_RXPI_CFG0_VAL5		0x0102
#define XHDMIPHY_DRP_RXPI_CFG0_VAL6		0x2102
#define XHDMIPHY_DRP_RXPI_CFG0_VAL7		0x2202
#define XHDMIPHY_DRP_RXPI_CFG0_VAL8		0x0200
#define XHDMIPHY_DRP_RXPI_CFG0_VAL9		0x1300
#define XHDMIPHY_DRP_RXPI_CFG0_VAL10		0x3300
#define XHDMIPHY_DRP_RXPI_CFG1_VAL1		0x0000
#define XHDMIPHY_DRP_RXPI_CFG1_VAL2		0x0015
#define XHDMIPHY_DRP_RXPI_CFG1_VAL3		0x0045
#define XHDMIPHY_DRP_RXPI_CFG1_VAL4		0x00fd
#define XHDMIPHY_DRP_RXPI_CFG1_VAL5		0x00ff
#define XHDMIPHY_DRP_TXPH_CFG_VAL1		0x0723
#define XHDMIPHY_DRP_TXPH_CFG_VAL2		0x0323

#define XHDMIPHY_DRP_TX_DATAWIDTH_VAL1		40
#define XHDMIPHY_DRP_TX_DATAWIDTH_VAL2		20
#define XHDMIPHY_DRP_TX_OUTDIV_VAL1		1
#define XHDMIPHY_DRP_TX_OUTDIV_VAL2		2

#define XHDMIPHY_DRP_TXPI_CFG_VAL1		0x0000
#define XHDMIPHY_DRP_TXPI_CFG_VAL2		0x0054
#define XHDMIPHY_DRP_TXPI_CFG_VAL3		0x03df
#define XHDMIPHY_DRP_TXPI_CFG3_CFG4_VAL1	0x0
#define XHDMIPHY_DRP_TXPI_CFG3_CFG4_VAL2	0x1
#define XHDMIPHY_DRP_TXPI_CFG3_CFG4_VAL3	0x2
#define XHDMIPHY_DRP_TXPI_CFG3_CFG4_VAL4	0x3
#define XHDMIPHY_DRP_TXPI_CFG3_CFG4_SHIFT	5
#define XHDMIPHY_DRP_TXPI_BIASSET_VAL1		3
#define XHDMIPHY_DRP_TXPI_BIASSET_VAL2		2
#define XHDMIPHY_DRP_TXPI_BIASSET_VAL3		1
#define XHDMIPHY_DRP_TXPI_BIASSET_SHIFT		1
#define XHDMIPHY_DRP_CH_HSPMUX_VAL1		0x68
#define XHDMIPHY_DRP_CH_HSPMUX_VAL2		0x44
#define XHDMIPHY_DRP_CH_HSPMUX_VAL3		0x24
#define XHDMIPHY_DRP_CH_HSPMUX_VAL4		0x3c
#define XHDMIPHY_DRP_CH_HSPMUX_SHIFT		8
#define XHDMIPHY_DRP_PLL_CLKOUT_DIV_VAL1	2
#define XHDMIPHY_DRP_PLL_CLKOUT_DIV_VAL2	1
#define XHDMIPHY_DRP_PLLX_CLKOUT_VAL1		0x68
#define XHDMIPHY_DRP_PLLX_CLKOUT_VAL2		0x44
#define XHDMIPHY_DRP_PLLX_CLKOUT_VAL3		0x24
#define XHDMIPHY_DRP_PLLX_CLKOUT_VAL4		0x3c

#define XHDMIPHY_DRP_RX_DATAWIDTH_80		80
#define XHDMIPHY_DRP_RX_DATAWIDTH_64		64
#define XHDMIPHY_DRP_RX_DATAWIDTH_40		40
#define XHDMIPHY_DRP_RX_DATAWIDTH_32		32
#define XHDMIPHY_RX_WIDEMODE_CDR_ENC_VAL1	0x2
#define XHDMIPHY_RX_WIDEMODE_CDR_ENC_VAL2	0x1
#define XHDMIPHY_RX_WIDEMODE_CDR_ENC_VAL3	0x0
#define XHDMIPHY_RX_WIDEMODE_CDR_ENC_SHIFT	2
#define XHDMIPHY_RXCDR_CFG_WORD0		0x0000
#define XHDMIPHY_RXCDR_CFG_WORD1		0x0000
#define XHDMIPHY_RXCDR_CFG_WORD2		0x0262
#define XHDMIPHY_RXCDR_CFG_WORD3		0x0000
#define XHDMIPHY_RXCDR_CFG_WORD4		0x0000
#define XHDMIPHY_RXCDR_CFG_WORD2_RXDIV		0x10
#define XHDMIPHY_DRP_RXCDR_CFG_GEN3(n)		(0xa2 + (n))
#define XHDMIPHY_DRP_RXCDR_CFG_WORD3		0x11

/* masks and registers of mmcme4 DRP */
#define XHDMIPHY_MMCM4_CLKOUT0_REG1		0x08
#define XHDMIPHY_MMCM4_CLKOUT0_REG2		0x09
#define XHDMIPHY_MMCM4_CLKOUT1_REG1		0x0a
#define XHDMIPHY_MMCM4_CLKOUT1_REG2		0x0b
#define XHDMIPHY_MMCM4_CLKOUT2_REG1		0x0c
#define XHDMIPHY_MMCM4_CLKOUT2_REG2		0x0d
#define XHDMIPHY_MMCM4_CLKFBOUT_REG1		0x14
#define XHDMIPHY_MMCM4_CLKFBOUT_REG2		0x15
#define XHDMIPHY_MMCM4_DIVCLK_DIV_REG		0x16
#define XHDMIPHY_MMCM4_DRP_LOCK_REG1		0x18
#define XHDMIPHY_MMCM4_DRP_LOCK_REG2		0x19
#define XHDMIPHY_MMCM4_DRP_LOCK_REG3		0x1a
#define XHDMIPHY_MMCM4_DRP_FILTER_REG1		0x4e
#define XHDMIPHY_MMCM4_DRP_FILTER_REG2		0x4f
#define XHDMIPHY_MMCM4_PWR_REG			0x27
#define XHDMIPHY_MMCM4_WRITE_VAL		0xffff

/* registers and masks of mmcme5 DRP */
#define XHDMIPHY_MMCM5_DRP_CLKFBOUT_1_REG	0x0c
#define XHDMIPHY_MMCM5_DRP_CLKFBOUT_2_REG	0x0d
#define XHDMIPHY_MMCM5_DRP_DIVCLK_DIVIDE_REG	0x21
#define XHDMIPHY_MMCM5_DRP_DESKEW_REG		0x20
#define XHDMIPHY_MMCM5_DRP_CLKOUT0_REG1		0x0e
#define XHDMIPHY_MMCM5_DRP_CLKOUT0_REG2		0x0f
#define XHDMIPHY_MMCM5_DRP_CLKOUT1_REG1		0x10
#define XHDMIPHY_MMCM5_DRP_CLKOUT1_REG2		0x11
#define XHDMIPHY_MMCM5_DRP_CLKOUT2_REG1		0x12
#define XHDMIPHY_MMCM5_DRP_CLKOUT2_REG2		0x13
#define XHDMIPHY_MMCM5_DRP_CP_REG1		0x1e
#define XHDMIPHY_MMCM5_DRP_RES_REG1		0x2a
#define XHDMIPHY_MMCM5_DRP_LOCK_REG1		0x27
#define XHDMIPHY_MMCM5_DRP_LOCK_REG2		0x28
#define XHDMIPHY_MMCM5_WRITE_VAL		0xFFFF
#define XHDMIPHY_MMCM5_CP_RES_MASK		0xf
#define XHDMIPHY_MMCM5_RES_MASK			0x1e
#define XHDMIPHY_MMCM5_LOCK1_MASK1		0x8000
#define XHDMIPHY_MMCM5_LOCK1_MASK2		0x7fff

enum color_depth {
	XVIDC_BPC_6 = 6,
	XVIDC_BPC_8 = 8,
	XVIDC_BPC_10 = 10,
	XVIDC_BPC_12 = 12,
	XVIDC_BPC_14 = 14,
	XVIDC_BPC_16 = 16,
};

enum ppc {
	XVIDC_PPC_1 = 1,
	XVIDC_PPC_2 = 2,
	XVIDC_PPC_4 = 4,
	XVIDC_PPC_8 = 8,
};

enum color_fmt {
	/* streaming video formats */
	XVIDC_CSF_RGB = 0,
	XVIDC_CSF_YCRCB_444 = 1,
	XVIDC_CSF_YCRCB_422 = 2,
	XVIDC_CSF_YCRCB_420 = 3,
	XVIDC_CSF_YONLY = 4,
	XVIDC_CSF_RGBA = 5,
	XVIDC_CSF_YCRCBA_444 = 6,
};

enum gt_type {
	XHDMIPHY_GTTYPE_GTHE4 = 5,
	XHDMIPHY_GTTYPE_GTYE4 = 6,
	XHDMIPHY_GTTYPE_GTYE5 = 7,
};

enum prot_type {
	XHDMIPHY_PROT_HDMI = 1,
	XHDMIPHY_PROT_HDMI21 = 2,
	XHDMIPHY_PROT_NONE = 3
};

enum dir {
	XHDMIPHY_DIR_RX = 0,
	XHDMIPHY_DIR_TX = 1,
	XHDMIPHY_DIR_NONE = 2
};

enum pll_type {
	XHDMIPHY_PLL_CPLL = 1,
	XHDMIPHY_PLL_QPLL = 2,
	XHDMIPHY_PLL_QPLL0 = 3,
	XHDMIPHY_PLL_QPLL1 = 4,
	XHDMIPHY_PLL_LCPLL = 5,
	XHDMIPHY_PLL_RPLL = 6,
	XHDMIPHY_PLL_UNKNOWN = 7,
};

enum chid {
	XHDMIPHY_CHID_CH1 = 1,
	XHDMIPHY_CHID_CH2 = 2,
	XHDMIPHY_CHID_CH3 = 3,
	XHDMIPHY_CHID_CH4 = 4,
	XHDMIPHY_CHID_CMN0 = 5, /* QPLL, QPLL0, LCPLL */
	XHDMIPHY_CHID_CMN1 = 6, /* QPLL1, RPLL */
	XHDMIPHY_CHID_CHA = 7,
	XHDMIPHY_CHID_CMNA = 8,
	XHDMIPHY_CHID_TXMMCM = 9,
	XHDMIPHY_CHID_RXMMCM = 10,
	XHDMIPHY_CHID_CMN = XHDMIPHY_CHID_CMN0,
};

enum refclk_sel {
	XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0 = 1,
	XHDMIPHY_PLL_REFCLKSEL_GTREFCLK1 = 2,
	XHDMIPHY_PLL_REFCLKSEL_GTNORTHREFCLK0 = 3,
	XHDMIPHY_PLL_REFCLKSEL_GTNORTHREFCLK1 = 4,
	XHDMIPHY_PLL_REFCLKSEL_GTSOUTHREFCLK0 = 5,
	XHDMIPHY_PLL_REFCLKSEL_GTSOUTHREFCLK1 = 6,
	XHDMIPHY_PLL_REFCLKSEL_GTEASTREFCLK0 = 3,
	XHDMIPHY_PLL_REFCLKSEL_GTEASTREFCLK1 = 4,
	XHDMIPHY_PLL_REFCLKSEL_GTWESTREFCLK0 = 5,
	XHDMIPHY_PLL_REFCLKSEL_GTWESTREFCLK1 = 6,
	XHDMIPHY_PLL_REFCLKSEL_GTGREFCLK = 7,
};

enum sysclk_data_sel {
	XHDMIPHY_SYSCLKSELDATA_PLL0_OUTCLK = 0,
	XHDMIPHY_SYSCLKSELDATA_PLL1_OUTCLK = 1,
	XHDMIPHY_SYSCLKSELDATA_CPLL_OUTCLK = 0,
	XHDMIPHY_SYSCLKSELDATA_QPLL_OUTCLK = 1,
	XHDMIPHY_SYSCLKSELDATA_QPLL0_OUTCLK = 3,
	XHDMIPHY_SYSCLKSELDATA_QPLL1_OUTCLK = 2,
};

enum sysclk_outsel {
	XHDMIPHY_SYSCLKSELOUT_CPLL_REFCLK = 0,
	XHDMIPHY_SYSCLKSELOUT_QPLL_REFCLK = 1,
	XHDMIPHY_SYSCLKSELOUT_QPLL0_REFCLK = 2,
	XHDMIPHY_SYSCLKSELOUT_QPLL1_REFCLK = 3,
	XHDMIPHY_SYSCLKSELOUT_PLL0_REFCLK = 0,
	XHDMIPHY_SYSCLKSELOUT_PLL1_REFCLK = 1
};

enum outclk_sel {
	XHDMIPHY_OUTCLKSEL_TYPE_OUTCLKPCS = 1,
	XHDMIPHY_OUTCLKSEL_TYPE_OUTCLKPMA = 2,
	XHDMIPHY_OUTCLKSEL_TYPE_PLLREFCLK_DIV1 = 3,
	XHDMIPHY_OUTCLKSEL_TYPE_PLLREFCLK_DIV2 = 4,
	XHDMIPHY_OUTCLKSEL_TYPE_PROGDIVCLK = 5
};

enum gt_state {
	XHDMIPHY_GT_STATE_IDLE = 0,		/* idle state. */
	XHDMIPHY_GT_STATE_GPO_RE = 1,	/* GPO RE state. */
	XHDMIPHY_GT_STATE_LOCK = 2,		/* lock state. */
	XHDMIPHY_GT_STATE_RESET = 3,	/* reset state. */
	XHDMIPHY_GT_STATE_ALIGN = 4,	/* align state. */
	XHDMIPHY_GT_STATE_READY = 5,	/* ready state. */
};

enum mmcm_divs {
	XHDMIPHY_MMCM_CLKFBOUT_MULT_F = 0,	/* M */
	XHDMIPHY_MMCM_DIVCLK_DIVIDE = 1,	/* D */
	XHDMIPHY_MMCM_CLKOUT_DIVIDE = 2	/* On */
};

enum mmcmclk_insel {
	XHDMIPHY_MMCM_CLKINSEL_CLKIN1 = 1,
	XHDMIPHY_MMCM_CLKINSEL_CLKIN2 = 0,
};

enum tx_patgen {
	XHDMIPHY_patgen_ratio_10 = 0x1,	/* LR:clock Ratio = 10 */
	XHDMIPHY_patgen_ratio_20 = 0x2,	/* LR:clock Ratio = 20 */
	XHDMIPHY_patgen_ratio_30 = 0x3,	/* LR:clock Ratio = 30 */
	XHDMIPHY_patgen_ratio_40 = 0x4,	/* LR:clock Ratio = 40 */
	XHDMIPHY_patgen_ratio_50 = 0x5,	/* LR:clock Ratio = 50 */
};

enum prbs_pat {
	XHDMIPHY_PRBSSEL_STD_MODE = 0x0,	/* PCattern gen/mon OFF */
	XHDMIPHY_PRBSSEL_PRBS7 = 0x1,		/* PCRBS-7 */
	XHDMIPHY_PRBSSEL_PRBS9 = 0x2,		/* PCRBS-9 */
	XHDMIPHY_PRBSSEL_PRBS15 = 0x3,		/* PCRBS-15 */
	XHDMIPHY_PRBSSEL_PRBS23 = 0x4,		/* PCRBS-23 */
	XHDMIPHY_PRBSSEL_PRBS31 = 0x5,		/* PRBS-31 */
	XHDMIPHY_PRBSSEL_PCIE = 0x8,		/* PCIE compliance pattern */
	XHDMIPHY_PRBSSEL_SQUARE_2UI = 0x9,	/* square wave with 2 UI */
	XHDMIPHY_PRBSSEL_SQUARE_16UI = 0xA,	/* square wave with 16 UI */
};

struct pll_param {
	u8 m_refclk_div;
	union {
		u8 nfb_divs[2];
		u8 nfb_div;
		struct {
			u8 n1fb_div;
			u8 n2fb_div;
		};
	};
	u16 cdr[5];
	u8 is_lowerband;
};

struct channel {
	u64 linerate;
	union {
		struct pll_param qpll_param;
		struct pll_param cpll_param;
		struct pll_param pll_param;
		u16 linerate_cfg;
	};
	union {
		enum refclk_sel cpll_refclk;
		enum refclk_sel pll_refclk;
	};
	union {
		struct {
			u8 rx_outdiv;
			u8 tx_outdiv;
		};
		u8 outdiv[2];
	};
	union {
		struct {
			enum gt_state rx_state;
			enum gt_state tx_state;
		};
		enum gt_state gt_state[2];
	};
	union {
		struct {
			enum prot_type rx_protocol;
			enum prot_type tx_protocol;
		};
		enum prot_type protocol[2];
	};
	union {
		struct {
			enum sysclk_data_sel rx_data_refclk;
			enum sysclk_data_sel tx_data_refclk;
		};
		enum sysclk_data_sel data_refclk[2];
	};
	union {
		struct {
			enum sysclk_outsel rx_outrefclk;
			enum sysclk_outsel tx_outrefclk;
		};
		enum sysclk_outsel out_refclk[2];
	};
	 union {
		struct {
			enum outclk_sel rx_outlck;
			enum outclk_sel tx_outclk;
		};
		enum outclk_sel outclk_sel[2];
	};
	union {
		struct {
			u8 rx_dly_bypass;
			u8 tx_dly_bypass;
		};
		u8 dly_bypass;
	};

	u8 rx_data_width;
	u8 rx_intdata_width;
	u8 tx_data_width;
	u8 tx_intdata_width;
};

struct xhdmiphy_mmcm {
	u32 index;
	u16 clkfbout_mult;
	u16 divclk_divide;
	u16 clkout0_div;
	u16 clkout1_div;
	u16 clkout2_div;
};

struct quad {
	union {
		struct {
			struct xhdmiphy_mmcm rx_mmcm;
			struct xhdmiphy_mmcm tx_mmcm;
		};
		struct xhdmiphy_mmcm mmcm[2];
	};
	union {
		struct {
			struct channel ch1;
			struct channel ch2;
			struct channel ch3;
			struct channel ch4;
			union {
				struct channel cmn0;
				struct channel lcpll;
			};
			union {
				struct channel cmn1;
				struct channel rpll;
			};
		};
		struct channel plls[6];
	};
	union {
		struct {
			u32 gt_refclk0;
			u32 gt_refclk1;
			u32 gt_nrefclk0;
			u32 gt_nrefclk1;
			u32 gt_srefclk0;
			u32 gt_srefclk1;
			u32 gt_grefclk;
		};
	u32 refclk[7];
	};
};

struct hdmi21_cfg {
	u64 linerate;
	u8 nchannels;
	u8 is_en;
};

struct xhdmiphy_conf {
	u8 tx_channels;
	u8 rx_channels;
	enum gt_type gt_type;
	enum prot_type tx_protocol;
	enum prot_type rx_protocol;
	enum refclk_sel tx_refclk_sel;		/* tx refclk selection. */
	enum refclk_sel rx_refclk_sel;		/* rx refclk selection. */
	enum refclk_sel tx_frl_refclk_sel;	/* tx frl refclk selection. */
	enum refclk_sel rx_frl_refclk_sel;	/* rx frl refclk selection. */
	enum sysclk_data_sel tx_pllclk_sel;	/* tx sysclk selection. */
	enum sysclk_data_sel rx_pllclk_sel;	/* rx sysclk selectino. */
	u8 dru_present;
	enum refclk_sel dru_refclk_sel;		/* DRU REFCLK selection. */
	enum ppc ppc;
	u8 tx_buff_bypass;
	u8 fast_switch;
	u8 transceiver_width;
	u32 err_irq;		/* Error IRQ is enalbed in design */
	u32 axilite_freq;
	u32 drpclk_freq;
	u8 gt_as_tx_tmdsclk;	/* use 4th GT channel as tx TMDS clock */
	u8 rx_maxrate;
	u8 tx_maxrate;
};

struct xhdmiphy_dev {
	struct device *dev;
	void __iomem *phy_base;
	struct hdmiphy_callback phycb[TX_READY_CB];
	int irq;
	struct mutex hdmiphy_mutex;	/* protecting phy operations */
	struct xhdmiphy_lane *lanes[4];
	struct clk *axi_lite_clk;
	struct clk *dru_clk;
	struct clk *tmds_clk;
	struct xhdmiphy_conf conf;
	const struct gt_conf *gt_adp;
	struct hdmi21_cfg tx_hdmi21_cfg;
	struct hdmi21_cfg rx_hdmi21_cfg;
	struct quad quad;
	u32 rx_refclk_hz;
	u32 tx_refclk_hz;
	u8 bpc;
	u32 color_fmt;
	u8 rx_tmdsclock_ratio;
	u8 tx_samplerate;
	u8 rx_dru_enabled;
	u8 qpll_present;
};

void xhdmiphy_set_clr(struct xhdmiphy_dev *inst, u32 addr, u32 reg_val,
		      u32 mask_val, u8 set_clr);
void xhdmiphy_ch2ids(struct xhdmiphy_dev *inst, enum chid chid, u8 *id0,
		     u8 *id1);
u32 xhdmiphy_pll_cal(struct xhdmiphy_dev *inst, enum chid chid, enum dir dir,
		     u32 pllclk_infreq_hz);
void xhdmiphy_write_refclksel(struct xhdmiphy_dev *inst);
void xhdmiphy_pll_refclk_sel(struct xhdmiphy_dev *inst, enum chid chid,
			     enum refclk_sel refclk_sel);
void xhdmiphy_sysclk_data_sel(struct xhdmiphy_dev *inst, enum dir dir,
			      enum sysclk_data_sel sysclk_datasel);
void xhdmiphy_sysclk_out_sel(struct xhdmiphy_dev *inst, enum dir dir,
			     enum sysclk_outsel sysclk_outsel);
u32 xhdmiphy_get_quad_refclk(struct xhdmiphy_dev *inst,
			     enum refclk_sel refclk_type);
bool xhdmiphy_check_linerate_cfg(struct xhdmiphy_dev *inst, enum chid chid,
				 enum dir dir);
void xhdmiphy_set_gpi(struct xhdmiphy_dev *inst, enum chid chid, enum dir dir,
		      u8 set);
u8 xhdmiphy_get_gpo(struct xhdmiphy_dev *inst, enum chid chid, enum dir dir);
void xhdmiphy_mmcm_reset(struct xhdmiphy_dev *inst, enum dir dir, u8 hold);
void xhdmiphy_mmcm_lock_en(struct xhdmiphy_dev *inst, enum dir dir, u8 en);

void xhdmiphy_set_bufgtdiv(struct xhdmiphy_dev *inst, enum dir dir, u8 div);
void xhdmiphy_powerdown_gtpll(struct xhdmiphy_dev *inst, enum chid chid, u8 hold);

void xhdmiphy_intr_en(struct xhdmiphy_dev *inst, u32 intr);
void xhdmiphy_intr_dis(struct xhdmiphy_dev *inst, u32 intr);

u64 xhdmiphy_get_pll_vco_freq(struct xhdmiphy_dev *inst, enum chid chid,
			      enum dir dir);
bool xhdmiphy_is_hdmi(struct xhdmiphy_dev *inst, enum dir dir);
bool xhdmiphy_is_ch(enum chid chid);
bool xhdmiphy_is_cmn(enum chid chid);
bool xhdmiphy_is_using_qpll(struct xhdmiphy_dev *inst, enum chid chid,
			    enum dir dir);
u32 xhdmiphy_qpll_param(struct xhdmiphy_dev *inst, enum chid chid, enum dir dir);
u32 xhdmiphy_cpll_param(struct xhdmiphy_dev *inst, enum chid chid, enum dir dir);

void xhdmiphy_gt_handler(struct xhdmiphy_dev *inst, u32 event_ack, u32 event);
void xhdmiphy_clkdet_handler(struct xhdmiphy_dev *inst, u32 event_ack, u32 event);
void xhdmiphy_cfg_init(struct xhdmiphy_dev *inst);
void xhdmiphy_pll_init(struct xhdmiphy_dev *inst, enum chid chid,
		       enum refclk_sel qpll_refclk_sel,
		       enum refclk_sel cpll_refclk_sel,
		       enum pll_type txpll_sel, enum pll_type rxpll_sel);
void xhdmiphy_cfg_linerate(struct xhdmiphy_dev *inst, enum chid chid,
			   u64 linerate);
u32 xhdmiphy_get_pll_type(struct xhdmiphy_dev *inst, enum dir dir,
			  enum chid chid);
u64 xhdmiphy_get_linerate(struct xhdmiphy_dev *inst, enum chid chid);
void xhdmiphy_set_tx_vs(struct xhdmiphy_dev *inst, enum chid chid, u8 vs);
void xhdmiphy_set_tx_pe(struct xhdmiphy_dev *inst, enum chid chid, u8 pe);
void xhdmiphy_set_tx_pc(struct xhdmiphy_dev *inst, enum chid chid, u8 pc);
void xhdmiphy_set_rxlpm(struct xhdmiphy_dev *inst, enum chid chid,
			enum dir dir, u8 en);
u32 xhdmiphy_drpwr(struct xhdmiphy_dev *inst, enum chid chid, u16 addr,
		   u16 val);
u32 xhdmiphy_drprd(struct xhdmiphy_dev *inst, enum chid chid, u16 addr,
		   u16 *reg_val);
void xhdmiphy_mmcm_pwr_down(struct xhdmiphy_dev *inst, enum dir dir, u8 hold);
void xhdmiphy_mmcm_start(struct xhdmiphy_dev *inst, enum dir dir);
void xhdmiphy_mmcm_param(struct xhdmiphy_dev *inst, enum dir dir);
void xhdmiphy_ibufds_en(struct xhdmiphy_dev *inst, enum dir dir, u8 en);
void xhdmiphy_clkout1_obuftds_en(struct xhdmiphy_dev *inst, enum dir dir, u8 en);
u32 xhdmiphy_init_phy(struct xhdmiphy_dev *inst);
u32 xhdmiphy_set_tx_param(struct xhdmiphy_dev *inst, enum chid chid,
			  enum ppc ppc, enum color_depth bpc,
			  enum color_fmt fmt);
u32 xhdmiphy_cal_mmcm_param(struct xhdmiphy_dev *inst, enum chid chid,
			    enum dir dir, enum ppc ppc, enum color_depth bpc);
u32 xhdmiphy_get_dru_refclk(struct xhdmiphy_dev *inst);
void xhdmiphy_hdmi20_conf(struct xhdmiphy_dev *inst, enum dir dir);
u32 xhdmiphy_hdmi21_conf(struct xhdmiphy_dev *inst, enum dir dir, u64 linerate, u8 nchannels);

u32 xhdmiphy_read(struct xhdmiphy_dev *inst, u32 addr);
void xhdmiphy_write(struct xhdmiphy_dev *inst, u32 addr, u32 val);

struct gtpll_divs {
	const u8 *m;
	const u8 *n1;
	const u8 *n2;
	const u8 *d;
};

struct gt_conf {
	bool (*cfg_set_cdr)(struct xhdmiphy_dev *inst, enum chid);
	bool (*check_pll_oprange)(struct xhdmiphy_dev *inst, enum chid,
				  u64 pllclk_out_freq);
	u32 (*outdiv_ch_reconf)(struct xhdmiphy_dev *inst, enum chid, enum dir);
	u32 (*clk_ch_reconf)(struct xhdmiphy_dev *inst, enum chid);
	u32 (*clk_cmn_reconf)(struct xhdmiphy_dev *inst, enum chid);
	u32 (*rxch_reconf)(struct xhdmiphy_dev *inst, enum chid);
	u32 (*txch_reconf)(struct xhdmiphy_dev *inst, enum chid);
	struct gtpll_divs cpll_divs;
	struct gtpll_divs qpll_divs;
};

u32 xhdmiphy_outdiv_ch_reconf(struct xhdmiphy_dev *instinst, enum chid chid,
			      enum dir dir);
u32 xhdmiphy_clk_ch_reconf(struct xhdmiphy_dev *instinst, enum chid chid);
u32 xhdmiphy_clk_cmn_reconf(struct xhdmiphy_dev *instinst, enum chid chid);
u32 xhdmiphy_rxch_reconf(struct xhdmiphy_dev *instinst, enum chid chid);
u32 xhdmiphy_txch_reconf(struct xhdmiphy_dev *instinst, enum chid chid);

extern const struct gt_conf gthe4_conf;
extern const struct gt_conf gtye5_conf;
#endif /* XHDMIPHY_H_ */
/** @} */
