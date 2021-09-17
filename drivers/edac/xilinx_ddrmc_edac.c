// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/firmware/xlnx-error-events.h>
#include <linux/firmware/xlnx-event-manager.h>

#include "edac_module.h"

/* Granularity of reported error in bytes */
#define XDDR_EDAC_ERR_GRAIN			1

#define XDDR_EDAC_MSG_SIZE			256

#define XDDR_PCSR_OFFSET			0xC
#define XDDR_ISR_OFFSET				0x14
#define XDDR_IRQ_EN_OFFSET			0x20
#define XDDR_IRQ1_EN_OFFSET			0x2C
#define XDDR_IRQ_DIS_OFFSET			0x24
#define XDDR_IRQ_CE_MASK			GENMASK(18, 15)
#define XDDR_IRQ_UE_MASK			GENMASK(14, 11)

#define XDDR_REG_CONFIG0_OFFSET			0x258
#define XDDR_REG_CONFIG0_BUS_WIDTH_MASK		GENMASK(19, 18)
#define XDDR_REG_CONFIG0_BUS_WIDTH_SHIFT	18
#define XDDR_REG_CONFIG0_NUM_CHANS_MASK		BIT(17)
#define XDDR_REG_CONFIG0_NUM_CHANS_SHIFT	17
#define XDDR_REG_CONFIG0_NUM_RANKS_MASK		GENMASK(15, 14)
#define XDDR_REG_CONFIG0_NUM_RANKS_SHIFT	14
#define XDDR_REG_CONFIG0_SIZE_MASK		GENMASK(10, 8)
#define XDDR_REG_CONFIG0_SIZE_SHIFT		8

#define XDDR_REG_PINOUT_OFFSET			0x25C
#define XDDR_REG_PINOUT_ECC_EN_MASK		GENMASK(7, 5)

#define ECCW0_FLIP_CTRL				0x109C
#define ECCW0_FLIP0_OFFSET			0x10A0
#define ECCW1_FLIP_CTRL				0x10AC
#define ECCW1_FLIP0_OFFSET			0x10B0
#define ECCR0_CERR_STAT_OFFSET			0x10BC
#define ECCR0_CE_ADDR_LO_OFFSET			0x10C0
#define ECCR0_CE_ADDR_LO_OFFSET			0x10C0
#define ECCR0_CE_ADDR_HI_OFFSET			0x10C4
#define ECCR0_CE_DATA_LO_OFFSET			0x10C8
#define ECCR0_CE_DATA_HI_OFFSET			0x10CC
#define ECCR0_CE_DATA_PAR_OFFSET		0x10D0

#define ECCR0_UERR_STAT_OFFSET			0x10D4
#define ECCR0_UE_ADDR_LO_OFFSET			0x10D8
#define ECCR0_UE_ADDR_HI_OFFSET			0x10DC
#define ECCR0_UE_DATA_LO_OFFSET			0x10E0
#define ECCR0_UE_DATA_HI_OFFSET			0x10E4
#define ECCR0_UE_DATA_PAR_OFFSET		0x10E8

#define ECCR1_CERR_STAT_OFFSET			0x10F4
#define ECCR1_CE_ADDR_LO_OFFSET			0x10F8
#define ECCR1_CE_ADDR_HI_OFFSET			0x10FC
#define ECCR1_CE_DATA_LO_OFFSET			0x1100
#define ECCR1_CE_DATA_HI_OFFSET			0x110C
#define ECCR1_CE_DATA_PAR_OFFSET		0x1108

#define ECCR1_UERR_STAT_OFFSET			0x110C
#define ECCR1_UE_ADDR_LO_OFFSET			0x1110
#define ECCR1_UE_ADDR_HI_OFFSET			0x1114
#define ECCR1_UE_DATA_LO_OFFSET			0x1118
#define ECCR1_UE_DATA_HI_OFFSET			0x111C
#define ECCR1_UE_DATA_PAR_OFFSET		0x1120

#define XDDR_NOC_REG_ADEC4_OFFSET		0x44
#define RANK_0_MASK				GENMASK(5, 0)
#define RANK_1_MASK				GENMASK(11, 6)
#define RANK_1_SHIFT				6
#define LRANK_0_MASK				GENMASK(17, 12)
#define LRANK_0_SHIFT				12
#define LRANK_1_MASK				GENMASK(23, 18)
#define LRANK_1_SHIFT				18
#define LRANK_2_MASK				GENMASK(29, 24)
#define LRANK_2_SHIFT				24

#define XDDR_NOC_REG_ADEC5_OFFSET		0x48
#define ROW_0_MASK				GENMASK(5, 0)
#define ROW_1_MASK				GENMASK(11, 6)
#define ROW_1_SHIFT				6
#define ROW_2_MASK				GENMASK(17, 12)
#define ROW_2_SHIFT				12
#define ROW_3_MASK				GENMASK(23, 18)
#define ROW_3_SHIFT				18
#define ROW_4_MASK				GENMASK(29, 24)
#define ROW_4_SHIFT				24

#define XDDR_NOC_REG_ADEC6_OFFSET		0x4C
#define ROW_5_MASK				GENMASK(5, 0)
#define ROW_6_MASK				GENMASK(11, 6)
#define ROW_6_SHIFT				6
#define ROW_7_MASK				GENMASK(17, 12)
#define ROW_7_SHIFT				12
#define ROW_8_MASK				GENMASK(23, 18)
#define ROW_8_SHIFT				18
#define ROW_9_MASK				GENMASK(29, 24)
#define ROW_9_SHIFT				24

#define XDDR_NOC_REG_ADEC7_OFFSET		0x50
#define ROW_10_MASK				GENMASK(5, 0)
#define ROW_11_MASK				GENMASK(11, 6)
#define ROW_11_SHIFT				6
#define ROW_12_MASK				GENMASK(17, 12)
#define ROW_12_SHIFT				12
#define ROW_13_MASK				GENMASK(23, 18)
#define ROW_13_SHIFT				18
#define ROW_14_MASK				GENMASK(29, 24)
#define ROW_14_SHIFT				24

#define XDDR_NOC_REG_ADEC8_OFFSET		0x54
#define ROW_15_MASK				GENMASK(5, 0)
#define ROW_16_MASK				GENMASK(11, 6)
#define ROW_16_SHIFT				6
#define ROW_17_MASK				GENMASK(17, 12)
#define ROW_17_SHIFT				12
#define ROW_18_MASK				GENMASK(23, 18)
#define ROW_18_SHIFT				18
#define COL_0_MASK				GENMASK(29, 24)
#define COL_0_SHIFT				24

#define XDDR_NOC_REG_ADEC9_OFFSET		0x58
#define COL_1_MASK				GENMASK(5, 0)
#define COL_2_MASK				GENMASK(11, 6)
#define COL_2_SHIFT				6
#define COL_3_MASK				GENMASK(17, 12)
#define COL_3_SHIFT				12
#define COL_4_MASK				GENMASK(23, 18)
#define COL_4_SHIFT				18
#define COL_5_MASK				GENMASK(29, 24)
#define COL_5_SHIFT				24

#define XDDR_NOC_REG_ADEC10_OFFSET		0x5C
#define COL_6_MASK				GENMASK(5, 0)
#define COL_7_MASK				GENMASK(11, 6)
#define COL_7_SHIFT				6
#define COL_8_MASK				GENMASK(17, 12)
#define COL_8_SHIFT				12
#define COL_9_MASK				GENMASK(23, 18)
#define COL_9_SHIFT				18
#define BANK_0_MASK				GENMASK(29, 24)
#define BANK_0_SHIFT				24

#define XDDR_NOC_REG_ADEC11_OFFSET		0x60
#define BANK_1_MASK				GENMASK(5, 0)
#define GRP_0_MASK				GENMASK(11, 6)
#define GRP_0_SHIFT				6
#define GRP_1_MASK				GENMASK(17, 12)
#define GRP_1_SHIFT				12
#define CH_0_MASK				GENMASK(23, 18)
#define CH_0_SHIFT				18

#define XDDR_NOC_REG_ADEC12_OFFSET		0x71C
#define XDDR_NOC_REG_ADEC13_OFFSET		0x720

#define XDDR_NOC_REG_ADEC14_OFFSET		0x724
#define XDDR_NOC_ROW_MATCH_MASK			GENMASK(17, 0)
#define XDDR_NOC_COL_MATCH_MASK			GENMASK(27, 18)
#define XDDR_NOC_COL_MATCH_SHIFT		18
#define XDDR_NOC_BANK_MATCH_MASK		GENMASK(29, 28)
#define XDDR_NOC_BANK_MATCH_SHIFT		28
#define XDDR_NOC_GRP_MATCH_MASK			GENMASK(31, 30)
#define XDDR_NOC_GRP_MATCH_SHIFT		30

#define XDDR_NOC_REG_ADEC15_OFFSET		0x728
#define XDDR_NOC_RANK_MATCH_MASK		GENMASK(1, 0)
#define XDDR_NOC_LRANK_MATCH_MASK		GENMASK(4, 2)
#define XDDR_NOC_LRANK_MATCH_SHIFT		2
#define XDDR_NOC_CH_MATCH_MASK			BIT(5)
#define XDDR_NOC_CH_MATCH_SHIFT			5
#define XDDR_NOC_MOD_SEL_MASK			BIT(6)
#define XDDR_NOC_MATCH_EN_MASK			BIT(8)

#define ECCR_UE_CE_ADDR_LO_BP_MASK		GENMASK(2, 0)
#define ECCR_UE_CE_ADDR_LO_LRANK_MASK		GENMASK(5, 3)
#define ECCR_UE_CE_ADDR_LO_LRANK_SHIFT		3
#define ECCR_UE_CE_ADDR_LO_RANK_MASK		GENMASK(7, 6)
#define ECCR_UE_CE_ADDR_LO_RANK_SHIFT		6
#define ECCR_UE_CE_ADDR_LO_GRP_MASK		GENMASK(9, 8)
#define ECCR_UE_CE_ADDR_LO_GRP_SHIFT		8
#define ECCR_UE_CE_ADDR_LO_BANK_MASK		GENMASK(11, 10)
#define ECCR_UE_CE_ADDR_LO_BANK_SHIFT		10
#define ECCR_UE_CE_ADDR_LO_COL_MASK		GENMASK(21, 12)
#define ECCR_UE_CE_ADDR_LO_COL_SHIFT		12
#define ECCR_UE_CE_ADDR_LO_ROW_MASK		GENMASK(31, 22)
#define ECCR_UE_CE_ADDR_LO_ROW_SHIFT		22
#define ECCR_UE_CE_ADDR_HI_ROW_MASK		GENMASK(7, 0)
#define ECCR_UE_CE_ADDR_HI_ROW_SHIFT		10

#define XDDR_EDAC_NR_CSROWS			1
#define XDDR_EDAC_NR_CHANS			1

#define XDDR_BUS_WIDTH_64			0
#define XDDR_BUS_WIDTH_32			1
#define XDDR_BUS_WIDTH_16			2

#define ECC_CEPOISON_MASK			0x1
#define ECC_UEPOISON_MASK			0x3

#define XDDR_MAX_ROW_CNT			18
#define XDDR_MAX_COL_CNT			10
#define XDDR_MAX_RANK_CNT			2
#define XDDR_MAX_LRANK_CNT			3
#define XDDR_MAX_BANK_CNT			2
#define XDDR_MAX_GRP_CNT			2

#define PCSR_UNLOCK_VAL				0xF9E8D7C6
#define XDDR_ERR_TYPE_CE			0
#define XDDR_ERR_TYPE_UE			1

#define XILINX_DRAM_SIZE_4G			0
#define XILINX_DRAM_SIZE_6G			1
#define XILINX_DRAM_SIZE_8G			2
#define XILINX_DRAM_SIZE_12G			3
#define XILINX_DRAM_SIZE_16G			4
#define XILINX_DRAM_SIZE_32G			5

/**
 * struct xddr_ecc_error_info - ECC error log information.
 * @rank:		Rank number.
 * @lrank:		Logical Rank number.
 * @row:		Row number.
 * @col:		Column number.
 * @bank:		Bank number.
 * @group:		Group number.
 * @burstpos:		Burst position.
 */
struct xddr_ecc_error_info {
	u32 rank;
	u32 lrank;
	u32 row;
	u32 col;
	u32 bank;
	u32 group;
	u32 burstpos;
};

/**
 * struct xddr_ecc_status - ECC status information to report.
 * @ceinfo:	Correctable error log information.
 * @ueinfo:	Uncorrectable error log information.
 * @channel:	Channel number.
 * @error_type:	Error type information.
 */
struct xddr_ecc_status {
	struct xddr_ecc_error_info ceinfo[2];
	struct xddr_ecc_error_info ueinfo[2];
	u32 channel;
	u8 error_type;
};

/**
 * struct xddr_edac_priv - DDR memory controller private instance data.
 * @ddrmc_baseaddr:	Base address of the DDR controller.
 * @ddrmc_noc_baseaddr:	Base address of the DDRMC NOC.
 * @message:		Buffer for framing the event specific info.
 * @mc_id:		Memory controller ID.
 * @ce_cnt:		Correctable error count.
 * @ue_cnt:		UnCorrectable error count.
 * @stat:		ECC status information.
 * @lrank_bit:		Bit shifts for lrank bit.
 * @rank_bit:		Bit shifts for rank bit.
 * @row_bit:		Bit shifts for row bit.
 * @col_bit:		Bit shifts for column bit.
 * @bank_bit:		Bit shifts for bank bit.
 * @grp_bit:		Bit shifts for group bit.
 * @ch_bit:		Bit shifts for channel bit.
 * @err_inject_addr:	Data poison address.
 */
struct xddr_edac_priv {
	void __iomem *ddrmc_baseaddr;
	void __iomem *ddrmc_noc_baseaddr;
	char message[XDDR_EDAC_MSG_SIZE];
	u32 mc_id;
	u32 ce_cnt;
	u32 ue_cnt;
	struct xddr_ecc_status stat;
	u32 lrank_bit[3];
	u32 rank_bit[2];
	u32 row_bit[18];
	u32 col_bit[10];
	u32 bank_bit[2];
	u32 grp_bit[2];
	u32 ch_bit;
#ifdef CONFIG_EDAC_DEBUG
	u64 err_inject_addr;
#endif
};

/**
 * xddr_get_error_info - Get the current ECC error info.
 * @priv:	DDR memory controller private instance data.
 *
 * Return: one if there is no error otherwise returns zero.
 */
static int xddr_get_error_info(struct xddr_edac_priv *priv)
{
	struct xddr_ecc_status *p;
	u32 eccr0_ceval, eccr1_ceval, eccr0_ueval, eccr1_ueval, regval;
	void __iomem *ddrmc_base;

	ddrmc_base = priv->ddrmc_baseaddr;
	p = &priv->stat;

	eccr0_ceval = readl(ddrmc_base + ECCR0_CERR_STAT_OFFSET);
	eccr1_ceval = readl(ddrmc_base + ECCR1_CERR_STAT_OFFSET);
	eccr0_ueval = readl(ddrmc_base + ECCR0_UERR_STAT_OFFSET);
	eccr1_ueval = readl(ddrmc_base + ECCR1_UERR_STAT_OFFSET);

	if (!eccr0_ceval && !eccr1_ceval && !eccr0_ueval && !eccr1_ueval)
		return 1;
	else if (!eccr0_ceval && !eccr1_ceval)
		goto ue_err;
	else if (!eccr0_ceval)
		p->channel = 1;
	else
		p->channel = 0;

	p->error_type = XDDR_ERR_TYPE_CE;
	regval = readl(ddrmc_base + ECCR0_CE_ADDR_LO_OFFSET);
	p->ceinfo[0].burstpos = (regval & ECCR_UE_CE_ADDR_LO_BP_MASK);
	p->ceinfo[0].lrank = (regval & ECCR_UE_CE_ADDR_LO_LRANK_MASK) >>
					ECCR_UE_CE_ADDR_LO_LRANK_SHIFT;
	p->ceinfo[0].rank = (regval & ECCR_UE_CE_ADDR_LO_RANK_MASK) >>
					ECCR_UE_CE_ADDR_LO_RANK_SHIFT;
	p->ceinfo[0].group = (regval & ECCR_UE_CE_ADDR_LO_GRP_MASK) >>
					ECCR_UE_CE_ADDR_LO_GRP_SHIFT;
	p->ceinfo[0].bank = (regval & ECCR_UE_CE_ADDR_LO_BANK_MASK) >>
					ECCR_UE_CE_ADDR_LO_BANK_SHIFT;
	p->ceinfo[0].col = (regval & ECCR_UE_CE_ADDR_LO_COL_MASK) >>
					ECCR_UE_CE_ADDR_LO_COL_SHIFT;
	p->ceinfo[0].row = (regval & ECCR_UE_CE_ADDR_LO_ROW_MASK) >>
					ECCR_UE_CE_ADDR_LO_ROW_SHIFT;
	regval = readl(ddrmc_base + ECCR0_CE_ADDR_HI_OFFSET);
	p->ceinfo[0].row |= ((regval & ECCR_UE_CE_ADDR_HI_ROW_MASK) <<
					ECCR_UE_CE_ADDR_HI_ROW_SHIFT);

	edac_dbg(2, "ERR DATA LOW: 0x%08X ERR DATA HIGH: 0x%08X ERR DATA PARITY: 0x%08X\n",
		 readl(ddrmc_base + ECCR0_CE_DATA_LO_OFFSET),
		 readl(ddrmc_base + ECCR0_CE_DATA_HI_OFFSET),
		 readl(ddrmc_base + ECCR0_CE_DATA_PAR_OFFSET));

	regval = readl(ddrmc_base + ECCR1_CE_ADDR_LO_OFFSET);
	p->ceinfo[1].burstpos = (regval & ECCR_UE_CE_ADDR_LO_BP_MASK);
	p->ceinfo[1].lrank = (regval & ECCR_UE_CE_ADDR_LO_LRANK_MASK) >>
					ECCR_UE_CE_ADDR_LO_LRANK_SHIFT;
	p->ceinfo[1].rank = (regval & ECCR_UE_CE_ADDR_LO_RANK_MASK) >>
					ECCR_UE_CE_ADDR_LO_RANK_SHIFT;
	p->ceinfo[1].group = (regval & ECCR_UE_CE_ADDR_LO_GRP_MASK) >>
					ECCR_UE_CE_ADDR_LO_GRP_SHIFT;
	p->ceinfo[1].bank = (regval & ECCR_UE_CE_ADDR_LO_BANK_MASK) >>
					ECCR_UE_CE_ADDR_LO_BANK_SHIFT;
	p->ceinfo[1].col = (regval & ECCR_UE_CE_ADDR_LO_COL_MASK) >>
					ECCR_UE_CE_ADDR_LO_COL_SHIFT;
	p->ceinfo[1].row = (regval & ECCR_UE_CE_ADDR_LO_ROW_MASK) >>
					ECCR_UE_CE_ADDR_LO_ROW_SHIFT;
	regval = readl(ddrmc_base + ECCR1_CE_ADDR_HI_OFFSET);
	p->ceinfo[1].row |= ((regval & ECCR_UE_CE_ADDR_HI_ROW_MASK) <<
					ECCR_UE_CE_ADDR_HI_ROW_SHIFT);

	edac_dbg(2, "ERR DATA LOW: 0x%08X ERR DATA HIGH: 0x%08X ERR DATA PARITY: 0x%08X\n",
		 readl(ddrmc_base + ECCR1_CE_DATA_LO_OFFSET),
		 readl(ddrmc_base + ECCR1_CE_DATA_HI_OFFSET),
		 readl(ddrmc_base + ECCR1_CE_DATA_PAR_OFFSET));
ue_err:
	if (!eccr0_ueval && !eccr1_ueval)
		goto out;
	else if (!eccr0_ueval)
		p->channel = 1;
	else
		p->channel = 0;

	p->error_type = XDDR_ERR_TYPE_UE;
	regval = readl(ddrmc_base + ECCR0_UE_ADDR_LO_OFFSET);
	p->ueinfo[0].burstpos = (regval & ECCR_UE_CE_ADDR_LO_BP_MASK);
	p->ueinfo[0].lrank = (regval & ECCR_UE_CE_ADDR_LO_LRANK_MASK) >>
					ECCR_UE_CE_ADDR_LO_LRANK_SHIFT;
	p->ueinfo[0].rank = (regval & ECCR_UE_CE_ADDR_LO_RANK_MASK) >>
					ECCR_UE_CE_ADDR_LO_RANK_SHIFT;
	p->ueinfo[0].group = (regval & ECCR_UE_CE_ADDR_LO_GRP_MASK) >>
					ECCR_UE_CE_ADDR_LO_GRP_SHIFT;
	p->ueinfo[0].bank = (regval & ECCR_UE_CE_ADDR_LO_BANK_MASK) >>
					ECCR_UE_CE_ADDR_LO_BANK_SHIFT;
	p->ueinfo[0].col = (regval & ECCR_UE_CE_ADDR_LO_COL_MASK) >>
					ECCR_UE_CE_ADDR_LO_COL_SHIFT;
	p->ueinfo[0].row = (regval & ECCR_UE_CE_ADDR_LO_ROW_MASK) >>
					ECCR_UE_CE_ADDR_LO_ROW_SHIFT;
	regval = readl(ddrmc_base + ECCR0_UE_ADDR_HI_OFFSET);
	p->ueinfo[0].row |= ((regval & ECCR_UE_CE_ADDR_HI_ROW_MASK) <<
					ECCR_UE_CE_ADDR_HI_ROW_SHIFT);

	edac_dbg(2, "ERR DATA LOW: 0x%08X ERR DATA HIGH: 0x%08X ERR DATA PARITY: 0x%08X\n",
		 readl(ddrmc_base + ECCR0_UE_DATA_LO_OFFSET),
		 readl(ddrmc_base + ECCR0_UE_DATA_HI_OFFSET),
		 readl(ddrmc_base + ECCR0_UE_DATA_PAR_OFFSET));

	regval = readl(ddrmc_base + ECCR1_UE_ADDR_LO_OFFSET);
	p->ueinfo[1].burstpos = (regval & ECCR_UE_CE_ADDR_LO_BP_MASK);
	p->ueinfo[1].lrank = (regval & ECCR_UE_CE_ADDR_LO_LRANK_MASK) >>
					ECCR_UE_CE_ADDR_LO_LRANK_SHIFT;
	p->ueinfo[1].rank = (regval & ECCR_UE_CE_ADDR_LO_RANK_MASK) >>
					ECCR_UE_CE_ADDR_LO_RANK_SHIFT;
	p->ueinfo[1].group = (regval & ECCR_UE_CE_ADDR_LO_GRP_MASK) >>
					ECCR_UE_CE_ADDR_LO_GRP_SHIFT;
	p->ueinfo[1].bank = (regval & ECCR_UE_CE_ADDR_LO_BANK_MASK) >>
					ECCR_UE_CE_ADDR_LO_BANK_SHIFT;
	p->ueinfo[1].col = (regval & ECCR_UE_CE_ADDR_LO_COL_MASK) >>
					ECCR_UE_CE_ADDR_LO_COL_SHIFT;
	p->ueinfo[1].row = (regval & ECCR_UE_CE_ADDR_LO_ROW_MASK) >>
					ECCR_UE_CE_ADDR_LO_ROW_SHIFT;
	regval = readl(ddrmc_base + ECCR1_UE_ADDR_HI_OFFSET);
	p->ueinfo[1].row |= ((regval & ECCR_UE_CE_ADDR_HI_ROW_MASK) <<
					ECCR_UE_CE_ADDR_HI_ROW_SHIFT);

	edac_dbg(2, "ERR DATA LOW: 0x%08X ERR DATA HIGH: 0x%08X ERR DATA PARITY: 0x%08X\n",
		 readl(ddrmc_base + ECCR1_UE_DATA_LO_OFFSET),
		 readl(ddrmc_base + ECCR1_UE_DATA_HI_OFFSET),
		 readl(ddrmc_base + ECCR1_UE_DATA_PAR_OFFSET));

out:
	/* Unlock the PCSR registers */
	writel(PCSR_UNLOCK_VAL, ddrmc_base + XDDR_PCSR_OFFSET);

	writel(0, ddrmc_base + ECCR0_CERR_STAT_OFFSET);
	writel(0, ddrmc_base + ECCR1_CERR_STAT_OFFSET);
	writel(0, ddrmc_base + ECCR0_UERR_STAT_OFFSET);
	writel(0, ddrmc_base + ECCR1_UERR_STAT_OFFSET);

	/* Lock the PCSR registers */
	writel(1, ddrmc_base + XDDR_PCSR_OFFSET);

	return 0;
}

/**
 * xddr_convert_to_physical - Convert to physical address.
 * @priv:	DDR memory controller private instance data.
 * @pinf:	ECC error info structure.
 *
 * Return: Physical address of the DDR memory.
 */
static ulong xddr_convert_to_physical(struct xddr_edac_priv *priv,
				      struct xddr_ecc_error_info pinf)
{
	ulong err_addr = 0;
	u32 index;

	for (index = 0; index < XDDR_MAX_ROW_CNT; index++) {
		err_addr |= (pinf.row & BIT(0)) << priv->row_bit[index];
		pinf.row >>= 1;
	}

	for (index = 0; index < XDDR_MAX_COL_CNT; index++) {
		err_addr |= (pinf.col & BIT(0)) << priv->col_bit[index];
		pinf.col >>= 1;
	}

	for (index = 0; index < XDDR_MAX_BANK_CNT; index++) {
		err_addr |= (pinf.bank & BIT(0)) << priv->bank_bit[index];
		pinf.bank >>= 1;
	}

	for (index = 0; index < XDDR_MAX_GRP_CNT; index++) {
		err_addr |= (pinf.group & BIT(0)) << priv->grp_bit[index];
		pinf.group >>= 1;
	}

	for (index = 0; index < XDDR_MAX_RANK_CNT; index++) {
		err_addr |= (pinf.rank & BIT(0)) << priv->rank_bit[index];
		pinf.rank >>= 1;
	}

	for (index = 0; index < XDDR_MAX_LRANK_CNT; index++) {
		err_addr |= (pinf.lrank & BIT(0)) << priv->lrank_bit[index];
		pinf.lrank >>= 1;
	}

	err_addr |= (priv->stat.channel & BIT(0)) << priv->ch_bit;

	return err_addr;
}

/**
 * xddr_handle_error - Handle Correctable and Uncorrectable errors.
 * @mci:	EDAC memory controller instance.
 * @stat:	ECC status structure.
 *
 * Handles ECC correctable and uncorrectable errors.
 */
static void xddr_handle_error(struct mem_ctl_info *mci,
			      struct xddr_ecc_status *stat)
{
	struct xddr_edac_priv *priv = mci->pvt_info;
	struct xddr_ecc_error_info pinf;

	if (stat->error_type == XDDR_ERR_TYPE_CE) {
		priv->ce_cnt++;
		pinf = stat->ceinfo[stat->channel];
		snprintf(priv->message, XDDR_EDAC_MSG_SIZE,
			 "Error type:%s MC ID: %d Addr at %lx Burst Pos: %d\n",
			 "CE", priv->mc_id,
			 xddr_convert_to_physical(priv, pinf), pinf.burstpos);

		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci,
				     priv->ce_cnt, 0, 0, 0, 0, 0, -1,
				     priv->message, "");
	}

	if (stat->error_type == XDDR_ERR_TYPE_UE) {
		priv->ue_cnt++;
		pinf = stat->ueinfo[stat->channel];
		snprintf(priv->message, XDDR_EDAC_MSG_SIZE,
			 "Error type:%s MC ID: %d Addr at %lx Burst Pos: %d\n",
			 "UE", priv->mc_id,
			 xddr_convert_to_physical(priv, pinf), pinf.burstpos);

		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci,
				     priv->ue_cnt, 0, 0, 0, 0, 0, -1,
				     priv->message, "");
	}

	memset(stat, 0, sizeof(*stat));
}

/**
 * xddr_intr_handler - Interrupt Handler for ECC interrupts.
 * @irq:	IRQ number
 * @dev_id:	Device ID
 *
 * Return: IRQ_NONE, if interrupt not set or IRQ_HANDLED otherwise.
 */
static irqreturn_t xddr_intr_handler(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;
	struct xddr_edac_priv *priv;
	int status, regval;

	priv = mci->pvt_info;
	regval = readl(priv->ddrmc_baseaddr + XDDR_ISR_OFFSET);
	regval &= (XDDR_IRQ_CE_MASK | XDDR_IRQ_UE_MASK);
	if (!regval)
		return IRQ_NONE;

	/* Unlock the PCSR registers */
	writel(PCSR_UNLOCK_VAL, priv->ddrmc_baseaddr + XDDR_PCSR_OFFSET);

	/* Clear the ISR */
	writel(regval, priv->ddrmc_baseaddr + XDDR_ISR_OFFSET);

	/* Lock the PCSR registers */
	writel(1, priv->ddrmc_baseaddr + XDDR_PCSR_OFFSET);

	status = xddr_get_error_info(priv);
	if (status)
		return IRQ_NONE;

	xddr_handle_error(mci, &priv->stat);

	edac_dbg(3, "Total error count CE %d UE %d\n",
		 priv->ce_cnt, priv->ue_cnt);

	return IRQ_HANDLED;
}

/**
 * xddr_err_callback - Handle Correctable and Uncorrectable errors.
 * @payload:	payload data.
 * @data:	mci controller data.
 *
 * Handles ECC correctable and uncorrectable errors.
 */
static void xddr_err_callback(const u32 *payload, void *data)
{
	struct mem_ctl_info *mci = (struct mem_ctl_info *)data;
	struct xddr_edac_priv *priv;
	int status, regval;
	struct xddr_ecc_status *p;

	priv = mci->pvt_info;
	p = &priv->stat;

	regval = readl(priv->ddrmc_baseaddr + XDDR_ISR_OFFSET);
	regval &= (XDDR_IRQ_CE_MASK | XDDR_IRQ_UE_MASK);
	if (!regval)
		return;

	/* Unlock the PCSR registers */
	writel(PCSR_UNLOCK_VAL, priv->ddrmc_baseaddr + XDDR_PCSR_OFFSET);

	/* Clear the ISR */
	writel(regval, priv->ddrmc_baseaddr + XDDR_ISR_OFFSET);
	/* Lock the PCSR registers */

	writel(1, priv->ddrmc_baseaddr + XDDR_PCSR_OFFSET);
	if (payload[2] == XPM_EVENT_ERROR_MASK_DDRMC_CR)
		p->error_type = XDDR_ERR_TYPE_CE;
	if (payload[2] == XPM_EVENT_ERROR_MASK_DDRMC_NCR)
		p->error_type = XDDR_ERR_TYPE_UE;

	status = xddr_get_error_info(priv);
	if (status)
		return;

	xddr_handle_error(mci, &priv->stat);

	edac_dbg(3, "Total error count CE %d UE %d\n",
		 priv->ce_cnt, priv->ue_cnt);
}

/**
 * xddr_get_dtype - Return the controller memory width.
 * @base:	DDR memory controller base address.
 *
 * Get the EDAC device type width appropriate for the controller
 * configuration.
 *
 * Return: a device type width enumeration.
 */
static enum dev_type xddr_get_dwidth(const void __iomem *base)
{
	enum dev_type dt;
	u32 regval;

	regval = readl(base + XDDR_REG_CONFIG0_OFFSET);
	regval = (regval & XDDR_REG_CONFIG0_BUS_WIDTH_MASK) >>
				XDDR_REG_CONFIG0_BUS_WIDTH_SHIFT;
	switch (regval) {
	case XDDR_BUS_WIDTH_16:
		dt = DEV_X2;
		break;
	case XDDR_BUS_WIDTH_32:
		dt = DEV_X4;
		break;
	case XDDR_BUS_WIDTH_64:
		dt = DEV_X8;
		break;
	default:
		dt = DEV_UNKNOWN;
	}

	return dt;
}

/**
 * xddr_get_ecc_state - Return the controller ECC enable/disable status.
 * @base:	DDR memory controller base address.
 *
 * Get the ECC enable/disable status for the controller.
 *
 * Return: a ECC status boolean i.e true/false - enabled/disabled.
 */
static bool xddr_get_ecc_state(void __iomem *base)
{
	enum dev_type dt;
	u32 ecctype;

	dt = xddr_get_dwidth(base);
	if (dt == DEV_UNKNOWN)
		return false;

	ecctype = readl(base + XDDR_REG_PINOUT_OFFSET);
	ecctype &= XDDR_REG_PINOUT_ECC_EN_MASK;
	if (ecctype)
		return true;

	return false;
}

/**
 * xddr_get_memsize - Get the size of the attached memory device.
 * @priv:	DDR memory controller private instance data.
 *
 * Return: the memory size in bytes.
 */
static u64 xddr_get_memsize(struct xddr_edac_priv *priv)
{
	u64 size;
	u32 regval;

	regval = readl(priv->ddrmc_baseaddr + XDDR_REG_CONFIG0_OFFSET) &
				XDDR_REG_CONFIG0_SIZE_MASK;
	regval >>= XDDR_REG_CONFIG0_SIZE_SHIFT;
	switch (regval) {
	case XILINX_DRAM_SIZE_4G:
		size = (4U * SZ_1G);
		break;
	case XILINX_DRAM_SIZE_6G:
		size = (6U * SZ_1G);
		break;
	case XILINX_DRAM_SIZE_8G:
		size = (8U * SZ_1G);
		break;
	case XILINX_DRAM_SIZE_12G:
		size = (12U * SZ_1G);
		break;
	case XILINX_DRAM_SIZE_16G:
		size = (16U * SZ_1G);
		break;
	case XILINX_DRAM_SIZE_32G:
		size = (32U * SZ_1G);
		break;
	default:
		/* Invalid configuration */
		size = 0;
		break;
	}

	return size;
}

/**
 * xddr_init_csrows - Initialize the csrow data.
 * @mci:	EDAC memory controller instance.
 *
 * Initialize the chip select rows associated with the EDAC memory
 * controller instance.
 */
static void xddr_init_csrows(struct mem_ctl_info *mci)
{
	struct xddr_edac_priv *priv = mci->pvt_info;
	struct csrow_info *csi;
	struct dimm_info *dimm;
	u32 row;
	int ch;
	unsigned long size;

	size = xddr_get_memsize(priv);
	for (row = 0; row < mci->nr_csrows; row++) {
		csi = mci->csrows[row];
		for (ch = 0; ch < csi->nr_channels; ch++) {
			dimm = csi->channels[ch]->dimm;
			dimm->edac_mode	= EDAC_SECDED;
			dimm->mtype = MEM_DDR4;
			dimm->nr_pages = (size >> PAGE_SHIFT) /
						csi->nr_channels;
			dimm->grain = XDDR_EDAC_ERR_GRAIN;
			dimm->dtype = xddr_get_dwidth(priv->ddrmc_baseaddr);
		}
	}
}

/**
 * xddr_mc_init - Initialize one driver instance.
 * @mci:	EDAC memory controller instance.
 * @pdev:	platform device.
 *
 * Perform initialization of the EDAC memory controller instance and
 * related driver-private data associated with the memory controller the
 * instance is bound to.
 */
static void xddr_mc_init(struct mem_ctl_info *mci, struct platform_device *pdev)
{
	mci->pdev = &pdev->dev;
	platform_set_drvdata(pdev, mci);

	/* Initialize controller capabilities and configuration */
	mci->mtype_cap = MEM_FLAG_DDR4;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->scrub_cap = SCRUB_HW_SRC;
	mci->scrub_mode = SCRUB_NONE;

	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->ctl_name = "xlnx_ddr_controller";
	mci->dev_name = dev_name(&pdev->dev);
	mci->mod_name = "xlnx_edac";

	edac_op_state = EDAC_OPSTATE_INT;

	xddr_init_csrows(mci);
}

static void xddr_enable_intr(struct xddr_edac_priv *priv)
{
	/* Unlock the PCSR registers */
	writel(PCSR_UNLOCK_VAL, priv->ddrmc_baseaddr + XDDR_PCSR_OFFSET);

	/* Enable UE and CE Interrupts to support the interrupt case */
	writel(XDDR_IRQ_CE_MASK | XDDR_IRQ_UE_MASK,
	       priv->ddrmc_baseaddr + XDDR_IRQ_EN_OFFSET);

	writel(XDDR_IRQ_UE_MASK,
	       priv->ddrmc_baseaddr + XDDR_IRQ1_EN_OFFSET);
	/* Lock the PCSR registers */
	writel(1, priv->ddrmc_baseaddr + XDDR_PCSR_OFFSET);
}

static void xddr_disable_intr(struct xddr_edac_priv *priv)
{
	/* Unlock the PCSR registers */
	writel(PCSR_UNLOCK_VAL, priv->ddrmc_baseaddr + XDDR_PCSR_OFFSET);

	/* Disable UE/CE Interrupts */
	writel(XDDR_IRQ_CE_MASK | XDDR_IRQ_UE_MASK,
	       priv->ddrmc_baseaddr + XDDR_IRQ_DIS_OFFSET);

	/* Lock the PCSR registers */
	writel(1, priv->ddrmc_baseaddr + XDDR_PCSR_OFFSET);
}

static int xddr_setup_irq(struct mem_ctl_info *mci,
			  struct platform_device *pdev)
{
	int ret, irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "No IRQ %d in DT\n", irq);
		return irq;
	}

	ret = devm_request_irq(&pdev->dev, irq, xddr_intr_handler,
			       IRQF_SHARED, dev_name(&pdev->dev), mci);
	if (ret < 0) {
		edac_printk(KERN_ERR, EDAC_MC, "Failed to request IRQ\n");
		return ret;
	}

	return 0;
}

#ifdef CONFIG_EDAC_DEBUG
#define to_mci(k) container_of(k, struct mem_ctl_info, dev)

/**
 * ddr_poison_setup - Update poison registers.
 * @priv:	DDR memory controller private instance data.
 *
 * Update poison registers as per DDR mapping.
 * Return: none.
 */
static void xddr_poison_setup(struct xddr_edac_priv *priv)
{
	u32 col = 0, row = 0, bank = 0, grp = 0, rank = 0, lrank = 0, ch = 0;
	u32 index, regval;

	for (index = 0; index < XDDR_MAX_ROW_CNT; index++) {
		row |= (((priv->err_inject_addr >> priv->row_bit[index]) &
						BIT(0)) << index);
	}

	for (index = 0; index < XDDR_MAX_COL_CNT; index++) {
		col |= (((priv->err_inject_addr >> priv->col_bit[index]) &
						BIT(0)) << index);
	}

	for (index = 0; index < XDDR_MAX_BANK_CNT; index++) {
		bank |= (((priv->err_inject_addr >> priv->bank_bit[index]) &
						BIT(0)) << index);
	}

	for (index = 0; index < XDDR_MAX_GRP_CNT; index++) {
		grp |= (((priv->err_inject_addr >> priv->grp_bit[index]) &
						BIT(0)) << index);
	}

	for (index = 0; index < XDDR_MAX_RANK_CNT; index++) {
		rank |= (((priv->err_inject_addr >> priv->rank_bit[index]) &
						BIT(0)) << index);
	}

	for (index = 0; index < XDDR_MAX_LRANK_CNT; index++) {
		lrank |= (((priv->err_inject_addr >> priv->lrank_bit[index]) &
						BIT(0)) << index);
	}

	ch = (priv->err_inject_addr >> priv->ch_bit) & BIT(0);
	if (ch)
		writel(0xFF, priv->ddrmc_baseaddr + ECCW1_FLIP_CTRL);
	else
		writel(0xFF, priv->ddrmc_baseaddr + ECCW0_FLIP_CTRL);

	writel(0, priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC12_OFFSET);
	writel(0, priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC13_OFFSET);

	regval = (row & XDDR_NOC_ROW_MATCH_MASK);
	regval |= (col << XDDR_NOC_COL_MATCH_SHIFT) & XDDR_NOC_COL_MATCH_MASK;
	regval |= (bank << XDDR_NOC_BANK_MATCH_SHIFT) &
			XDDR_NOC_BANK_MATCH_MASK;
	regval |= (grp << XDDR_NOC_GRP_MATCH_SHIFT) & XDDR_NOC_GRP_MATCH_MASK;
	writel(regval, priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC14_OFFSET);

	regval = (rank & XDDR_NOC_RANK_MATCH_MASK);
	regval |= (lrank << XDDR_NOC_LRANK_MATCH_SHIFT) &
			XDDR_NOC_LRANK_MATCH_MASK;
	regval |= (ch << XDDR_NOC_CH_MATCH_SHIFT) & XDDR_NOC_CH_MATCH_MASK;
	regval |= (XDDR_NOC_MOD_SEL_MASK | XDDR_NOC_MATCH_EN_MASK);
	writel(regval, priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC15_OFFSET);
}

static ssize_t inject_data_error_show(struct device *dev,
				      struct device_attribute *mattr,
				      char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct xddr_edac_priv *priv = mci->pvt_info;

	return sprintf(data, "Error injection Address: 0x%llx\n\r",
			priv->err_inject_addr);
}

static ssize_t inject_data_error_store(struct device *dev,
				       struct device_attribute *mattr,
				       const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct xddr_edac_priv *priv = mci->pvt_info;

	if (kstrtoull(data, 0, &priv->err_inject_addr))
		return -EINVAL;

	/* Unlock the PCSR registers */
	writel(PCSR_UNLOCK_VAL, priv->ddrmc_baseaddr + XDDR_PCSR_OFFSET);
	writel(PCSR_UNLOCK_VAL, priv->ddrmc_noc_baseaddr + XDDR_PCSR_OFFSET);

	xddr_poison_setup(priv);

	/* Lock the PCSR registers */
	writel(1, priv->ddrmc_baseaddr + XDDR_PCSR_OFFSET);
	writel(1, priv->ddrmc_noc_baseaddr + XDDR_PCSR_OFFSET);

	return count;
}

static ssize_t inject_data_poison_show(struct device *dev,
				       struct device_attribute *mattr,
				       char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct xddr_edac_priv *priv = mci->pvt_info;
	u32 regval;

	regval = readl(priv->ddrmc_baseaddr + ECCW0_FLIP0_OFFSET);
	return sprintf(data, "Data Poisoning: %s\n\r",
			((regval & 0x3) == 1)
			? ("Correctable Error") : ("UnCorrectable Error"));
}

static ssize_t inject_data_poison_store(struct device *dev,
					struct device_attribute *mattr,
					const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct xddr_edac_priv *priv = mci->pvt_info;

	/* Unlock the PCSR registers */
	writel(PCSR_UNLOCK_VAL, priv->ddrmc_baseaddr + XDDR_PCSR_OFFSET);

	writel(0, priv->ddrmc_baseaddr + ECCW0_FLIP0_OFFSET);
	writel(0, priv->ddrmc_baseaddr + ECCW1_FLIP0_OFFSET);
	if (strncmp(data, "CE", 2) == 0) {
		writel(ECC_CEPOISON_MASK, priv->ddrmc_baseaddr +
		       ECCW0_FLIP0_OFFSET);
		writel(ECC_CEPOISON_MASK, priv->ddrmc_baseaddr +
		       ECCW1_FLIP0_OFFSET);
	} else {
		writel(ECC_UEPOISON_MASK, priv->ddrmc_baseaddr +
		       ECCW0_FLIP0_OFFSET);
		writel(ECC_UEPOISON_MASK, priv->ddrmc_baseaddr +
		       ECCW1_FLIP0_OFFSET);
	}

	/* Lock the PCSR registers */
	writel(1, priv->ddrmc_baseaddr + XDDR_PCSR_OFFSET);

	return count;
}

static DEVICE_ATTR_RW(inject_data_error);
static DEVICE_ATTR_RW(inject_data_poison);

static int edac_create_sysfs_attributes(struct mem_ctl_info *mci)
{
	int rc;

	rc = device_create_file(&mci->dev, &dev_attr_inject_data_error);
	if (rc < 0)
		return rc;

	rc = device_create_file(&mci->dev, &dev_attr_inject_data_poison);
	if (rc < 0)
		return rc;

	return 0;
}

static void edac_remove_sysfs_attributes(struct mem_ctl_info *mci)
{
	device_remove_file(&mci->dev, &dev_attr_inject_data_error);
	device_remove_file(&mci->dev, &dev_attr_inject_data_poison);
}

static void xddr_setup_row_address_map(struct xddr_edac_priv *priv)
{
	u32 regval;

	regval = readl(priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC5_OFFSET);
	priv->row_bit[0] = regval & ROW_0_MASK;
	priv->row_bit[1] = (regval & ROW_1_MASK) >> ROW_1_SHIFT;
	priv->row_bit[2] = (regval & ROW_2_MASK) >> ROW_2_SHIFT;
	priv->row_bit[3] = (regval & ROW_3_MASK) >> ROW_3_SHIFT;
	priv->row_bit[4] = (regval & ROW_4_MASK) >> ROW_4_SHIFT;

	regval = readl(priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC6_OFFSET);
	priv->row_bit[5] = regval & ROW_5_MASK;
	priv->row_bit[6] = (regval & ROW_6_MASK) >> ROW_6_SHIFT;
	priv->row_bit[7] = (regval & ROW_7_MASK) >> ROW_7_SHIFT;
	priv->row_bit[8] = (regval & ROW_8_MASK) >> ROW_8_SHIFT;
	priv->row_bit[9] = (regval & ROW_9_MASK) >> ROW_9_SHIFT;

	regval = readl(priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC7_OFFSET);
	priv->row_bit[10] = regval & ROW_10_MASK;
	priv->row_bit[11] = (regval & ROW_11_MASK) >> ROW_11_SHIFT;
	priv->row_bit[12] = (regval & ROW_12_MASK) >> ROW_12_SHIFT;
	priv->row_bit[13] = (regval & ROW_13_MASK) >> ROW_13_SHIFT;
	priv->row_bit[14] = (regval & ROW_14_MASK) >> ROW_14_SHIFT;

	regval = readl(priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC8_OFFSET);

	priv->row_bit[15] = regval & ROW_15_MASK;
	priv->row_bit[16] = (regval & ROW_16_MASK) >> ROW_16_SHIFT;
	priv->row_bit[17] = (regval & ROW_17_MASK) >> ROW_17_SHIFT;
}

static void xddr_setup_column_address_map(struct xddr_edac_priv *priv)
{
	u32 regval;

	regval = readl(priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC8_OFFSET);
	priv->col_bit[0] = (regval & COL_0_MASK) >> COL_0_SHIFT;

	regval = readl(priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC9_OFFSET);
	priv->col_bit[1] = (regval & COL_1_MASK);
	priv->col_bit[2] = (regval & COL_2_MASK) >> COL_2_SHIFT;
	priv->col_bit[3] = (regval & COL_3_MASK) >> COL_3_SHIFT;
	priv->col_bit[4] = (regval & COL_4_MASK) >> COL_4_SHIFT;
	priv->col_bit[5] = (regval & COL_5_MASK) >> COL_5_SHIFT;

	regval = readl(priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC10_OFFSET);
	priv->col_bit[6] = (regval & COL_6_MASK);
	priv->col_bit[7] = (regval & COL_7_MASK) >> COL_7_SHIFT;
	priv->col_bit[8] = (regval & COL_8_MASK) >> COL_8_SHIFT;
	priv->col_bit[9] = (regval & COL_9_MASK) >> COL_9_SHIFT;
}

static void xddr_setup_bank_grp_ch_address_map(struct xddr_edac_priv *priv)
{
	u32 regval;

	regval = readl(priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC10_OFFSET);
	priv->bank_bit[0] = (regval & BANK_0_MASK) >> BANK_0_SHIFT;

	regval = readl(priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC11_OFFSET);
	priv->bank_bit[1] = (regval & BANK_1_MASK);
	priv->grp_bit[0] = (regval & GRP_0_MASK) >> GRP_0_SHIFT;
	priv->grp_bit[1] = (regval & GRP_1_MASK) >> GRP_1_SHIFT;
	priv->ch_bit = (regval & CH_0_MASK) >> CH_0_SHIFT;
}

static void xddr_setup_rank_lrank_address_map(struct xddr_edac_priv *priv)
{
	u32 regval;

	regval = readl(priv->ddrmc_noc_baseaddr + XDDR_NOC_REG_ADEC4_OFFSET);
	priv->rank_bit[0] = (regval & RANK_0_MASK);
	priv->rank_bit[1] = (regval & RANK_1_MASK) >> RANK_1_SHIFT;
	priv->lrank_bit[0] = (regval & LRANK_0_MASK) >> LRANK_0_SHIFT;
	priv->lrank_bit[1] = (regval & LRANK_1_MASK) >> LRANK_1_SHIFT;
	priv->lrank_bit[2] = (regval & LRANK_2_MASK) >> LRANK_2_SHIFT;
}

/**
 * xddr_setup_address_map - Set Address Map by querying ADDRMAP registers.
 * @priv:	DDR memory controller private instance data.
 *
 * Set Address Map by querying ADDRMAP registers.
 *
 * Return: none.
 */
static void xddr_setup_address_map(struct xddr_edac_priv *priv)
{
	xddr_setup_row_address_map(priv);

	xddr_setup_column_address_map(priv);

	xddr_setup_bank_grp_ch_address_map(priv);

	xddr_setup_rank_lrank_address_map(priv);
}
#endif /* CONFIG_EDAC_DEBUG */

/**
 * xddr_mc_probe - Check controller and bind driver.
 * @pdev:	platform device.
 *
 * Probe a specific controller instance for binding with the driver.
 *
 * Return: 0 if the controller instance was successfully bound to the
 * driver; otherwise, < 0 on error.
 */
static int xddr_mc_probe(struct platform_device *pdev)
{
	struct edac_mc_layer layers[2];
	struct xddr_edac_priv *priv;
	struct mem_ctl_info *mci;
	void __iomem *ddrmc_baseaddr, *ddrmc_noc_baseaddr;
	struct resource *res;
	int rc;
	u8 num_chans, num_csrows;
	u32 edac_mc_id, regval;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ddrmc_base");
	ddrmc_baseaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ddrmc_baseaddr))
		return PTR_ERR(ddrmc_baseaddr);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "ddrmc_noc_base");
	ddrmc_noc_baseaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ddrmc_noc_baseaddr))
		return PTR_ERR(ddrmc_noc_baseaddr);

	if (!xddr_get_ecc_state(ddrmc_baseaddr))
		return -ENXIO;

	rc = of_property_read_u32(pdev->dev.of_node, "xlnx,mc-id",
				  &edac_mc_id);
	if (rc)
		return rc;

	regval = readl(ddrmc_baseaddr + XDDR_REG_CONFIG0_OFFSET);
	num_chans = (regval & XDDR_REG_CONFIG0_NUM_CHANS_MASK) >>
			XDDR_REG_CONFIG0_NUM_CHANS_SHIFT;
	num_chans++;

	num_csrows = (regval & XDDR_REG_CONFIG0_NUM_RANKS_MASK) >>
			XDDR_REG_CONFIG0_NUM_RANKS_SHIFT;
	num_csrows *= 2;
	if (!num_csrows)
		num_csrows = 1;

	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = num_csrows;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = num_chans;
	layers[1].is_virt_csrow = false;

	mci = edac_mc_alloc(edac_mc_id, ARRAY_SIZE(layers), layers,
			    sizeof(struct xddr_edac_priv));
	if (!mci) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Failed memory allocation for mc instance\n");
		return -ENOMEM;
	}

	priv = mci->pvt_info;
	priv->ddrmc_baseaddr = ddrmc_baseaddr;
	priv->ddrmc_noc_baseaddr = ddrmc_noc_baseaddr;
	priv->ce_cnt = 0;
	priv->ue_cnt = 0;

	xddr_mc_init(mci, pdev);

	rc = edac_mc_add_mc(mci);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Failed to register with EDAC core\n");
		goto free_edac_mc;
	}

#ifdef CONFIG_EDAC_DEBUG
	if (edac_create_sysfs_attributes(mci)) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Failed to create sysfs entries\n");
		goto del_edac_mc;
	}

	xddr_setup_address_map(priv);
#endif

	rc = xlnx_register_event(PM_NOTIFY_CB, XPM_NODETYPE_EVENT_ERROR_PMC_ERR1,
				 XPM_EVENT_ERROR_MASK_DDRMC_CR | XPM_EVENT_ERROR_MASK_DDRMC_NCR,
				 false, xddr_err_callback, mci);
	if (rc == -ENODEV) {
		rc = xddr_setup_irq(mci, pdev);
		if (rc)
			goto del_edac_mc;
	}
	if (rc) {
		if (rc == -EACCES)
			rc = -EPROBE_DEFER;

		goto del_edac_mc;
	}

	xddr_enable_intr(priv);

	return rc;

del_edac_mc:
	edac_mc_del_mc(&pdev->dev);
free_edac_mc:
	edac_mc_free(mci);

	return rc;
}

/**
 * xddr_mc_remove - Unbind driver from controller.
 * @pdev:	Platform device.
 *
 * Return: Unconditionally 0
 */
static int xddr_mc_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);
	struct xddr_edac_priv *priv = mci->pvt_info;

	xddr_disable_intr(priv);

#ifdef CONFIG_EDAC_DEBUG
	edac_remove_sysfs_attributes(mci);
#endif

	xlnx_unregister_event(PM_NOTIFY_CB, XPM_NODETYPE_EVENT_ERROR_PMC_ERR1,
			      XPM_EVENT_ERROR_MASK_DDRMC_CR | XPM_EVENT_ERROR_MASK_DDRMC_NCR,
			      xddr_err_callback);
	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);

	return 0;
}

static const struct of_device_id xlnx_edac_match[] = {
	{ .compatible = "xlnx,versal-ddrmc-edac", },
	{
		/* end of table */
	}
};

MODULE_DEVICE_TABLE(of, xlnx_edac_match);

static struct platform_driver xilinx_ddr_edac_mc_driver = {
	.driver = {
		.name = "xilinx-ddrmc-edac",
		.of_match_table = xlnx_edac_match,
	},
	.probe = xddr_mc_probe,
	.remove = xddr_mc_remove,
};

module_platform_driver(xilinx_ddr_edac_mc_driver);

MODULE_AUTHOR("Xilinx Inc");
MODULE_DESCRIPTION("Xilinx DDRMC ECC driver");
MODULE_LICENSE("GPL");
