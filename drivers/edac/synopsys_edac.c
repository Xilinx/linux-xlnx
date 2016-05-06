/*
 * Synopsys DDR ECC Driver
 * This driver is based on ppc4xx_edac.c drivers
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
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details
 */

#include <linux/edac.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of.h>

#include "edac_core.h"

/* Number of cs_rows needed per memory controller */
#define SYNPS_EDAC_NR_CSROWS	1

/* Number of channels per memory controller */
#define SYNPS_EDAC_NR_CHANS	1

/* Granularity of reported error in bytes */
#define SYNPS_EDAC_ERR_GRAIN	1

#define SYNPS_EDAC_MSG_SIZE	256

#define SYNPS_EDAC_MOD_STRING	"synps_edac"
#define SYNPS_EDAC_MOD_VER	"1"

/* Synopsys DDR memory controller registers that are relevant to ECC */
#define CTRL_OFST		0x0
#define T_ZQ_OFST		0xA4

/* ECC control register */
#define ECC_CTRL_OFST		0xC4
/* ECC log register */
#define CE_LOG_OFST		0xC8
/* ECC address register */
#define CE_ADDR_OFST		0xCC
/* ECC data[31:0] register */
#define CE_DATA_31_0_OFST	0xD0

/* Uncorrectable error info registers */
#define UE_LOG_OFST		0xDC
#define UE_ADDR_OFST		0xE0
#define UE_DATA_31_0_OFST	0xE4

#define STAT_OFST		0xF0
#define SCRUB_OFST		0xF4

/* Control register bit field definitions */
#define CTRL_BW_MASK		0xC
#define CTRL_BW_SHIFT		2

#define DDRCTL_WDTH_16		1
#define DDRCTL_WDTH_32		0

/* ZQ register bit field definitions */
#define T_ZQ_DDRMODE_MASK	0x2

/* ECC control register bit field definitions */
#define ECC_CTRL_CLR_CE_ERR	0x2
#define ECC_CTRL_CLR_UE_ERR	0x1

/* ECC correctable/uncorrectable error log register definitions */
#define LOG_VALID		0x1
#define CE_LOG_BITPOS_MASK	0xFE
#define CE_LOG_BITPOS_SHIFT	1

/* ECC correctable/uncorrectable error address register definitions */
#define ADDR_COL_MASK		0xFFF
#define ADDR_ROW_MASK		0xFFFF000
#define ADDR_ROW_SHIFT		12
#define ADDR_BANK_MASK		0x70000000
#define ADDR_BANK_SHIFT		28

/* ECC statistic register definitions */
#define STAT_UECNT_MASK		0xFF
#define STAT_CECNT_MASK		0xFF00
#define STAT_CECNT_SHIFT	8

/* ECC scrub register definitions */
#define SCRUB_MODE_MASK		0x7
#define SCRUB_MODE_SECDED	0x4

/* DDR ECC Quirks */
#define DDR_ECC_INTR_SUPPORT    BIT(0)
#define DDR_ECC_DATA_POISON_SUPPORT BIT(1)

/* ZynqMP Enhanced DDR memory controller registers that are relevant to ECC */
/* ECC Configuration Registers */
#define ECC_CFG0_OFST	0x70
#define ECC_CFG1_OFST	0x74

/* ECC Status Register */
#define ECC_STAT_OFST	0x78

/* ECC Clear Register */
#define ECC_CLR_OFST	0x7C

/* ECC Error count Register */
#define ECC_ERRCNT_OFST	0x80

/* ECC Corrected Error Address Register */
#define ECC_CEADDR0_OFST	0x84
#define ECC_CEADDR1_OFST	0x88

/* ECC Syndrome Registers */
#define ECC_CSYND0_OFST	0x8C
#define ECC_CSYND1_OFST	0x90
#define ECC_CSYND2_OFST	0x94

/* ECC Bit Mask0 Address Register */
#define ECC_BITMASK0_OFST	0x98
#define ECC_BITMASK1_OFST	0x9C
#define ECC_BITMASK2_OFST	0xA0

/* ECC UnCorrected Error Address Register */
#define ECC_UEADDR0_OFST	0xA4
#define ECC_UEADDR1_OFST	0xA8

/* ECC Syndrome Registers */
#define ECC_UESYND0_OFST	0xAC
#define ECC_UESYND1_OFST	0xB0
#define ECC_UESYND2_OFST	0xB4

/* ECC Poison Address Reg */
#define ECC_POISON0_OFST	0xB8
#define ECC_POISON1_OFST	0xBC

/* Control regsiter bitfield definitions */
#define ECC_CTRL_BUSWIDTH_MASK	0x3000
#define ECC_CTRL_BUSWIDTH_SHIFT	12
#define ECC_CTRL_CLR_CE_ERRCNT	BIT(2)
#define ECC_CTRL_CLR_UE_ERRCNT	BIT(3)

/* DDR Control Register width definitions  */
#define DDRCTL_EWDTH_16		2
#define DDRCTL_EWDTH_32		1
#define DDRCTL_EWDTH_64		0

/* ECC status regsiter definitions */
#define ECC_STAT_UECNT_MASK	0xF0000
#define ECC_STAT_UECNT_SHIFT	16
#define ECC_STAT_CECNT_MASK	0xF00
#define ECC_STAT_CECNT_SHIFT	8
#define ECC_STAT_BITNUM_MASK	0x7F

/* DDR QOS Interrupt regsiter definitions */
#define DDR_QOS_IRQ_STAT_OFST	0x20200
#define DDR_QOSUE_MASK		0x4
#define	DDR_QOSCE_MASK		0x2
#define	ECC_CE_UE_INTR_MASK	0x6
#define DDR_QOS_IRQ_EN_OFST     0x20208
#define DDR_QOS_IRQ_DB_OFST	0x2020C

/* ECC Corrected Error Register Mask and Shifts*/
#define ECC_CEADDR0_RW_MASK	0x3FFFF
#define ECC_CEADDR0_RNK_MASK	BIT(24)
#define ECC_CEADDR1_BNKGRP_MASK	0x3000000
#define ECC_CEADDR1_BNKNR_MASK	0x70000
#define ECC_CEADDR1_BLKNR_MASK	0xFFF
#define ECC_CEADDR1_BNKGRP_SHIFT	24
#define ECC_CEADDR1_BNKNR_SHIFT	16

/* ECC Poison register shifts */
#define ECC_POISON0_RANK_SHIFT 24
#define ECC_POISON1_BANKGRP_SHIFT 28
#define ECC_POISON1_BANKNR_SHIFT 24

/* DDR Memory type defines */
#define MEM_TYPE_DDR3 0x1
#define MEM_TYPE_LPDDR3 0x1
#define MEM_TYPE_DDR2 0x4
#define MEM_TYPE_DDR4 0x10
#define MEM_TYPE_LPDDR4 0x10

/* DDRC Software control register */
#define DDRC_SWCTL 0x320

/* DDRC ECC CE & UE poison mask */
#define ECC_CEPOISON_MASK 0x3
#define ECC_UEPOISON_MASK 0x1

/* DDRC Device config masks */
#define DDRC_MSTR_DEV_CONFIG_MASK 0xC0000000
#define DDRC_MSTR_DEV_CONFIG_SHIFT	30
#define DDRC_MSTR_DEV_CONFIG_X4_MASK	0
#define DDRC_MSTR_DEV_CONFIG_X8_MASK	1
#define DDRC_MSTR_DEV_CONFIG_X16_MASK	0x10
#define DDRC_MSTR_DEV_CONFIG_X32_MASK	0X11

/* DDR4 and DDR3 device Row,Column,Bank Mapping */
#define DDR4_COL_SHIFT		3
#define DDR4_BANKGRP_SHIFT	13
#define DDR4_BANK_SHIFT	15
#define DDR4_ROW_SHIFT		17
#define DDR4_COL_MASK		0x3FF
#define DDR4_BANKGRP_MASK	0x3
#define DDR4_BANK_MASK		0x3
#define DDR4_ROW_MASK		0x7FFF

#define DDR3_COL_SHIFT	3
#define DDR3_BANK_SHIFT 13
#define DDR3_ROW_SHIFT	16
#define DDR3_COL_MASK	0x3FF
#define DDR3_BANK_MASK	0x7
#define DDR3_ROW_MASK	0x3FFF

/**
 * struct ecc_error_info - ECC error log information
 * @row:	Row number
 * @col:	Column number
 * @bank:	Bank number
 * @bitpos:	Bit position
 * @data:	Data causing the error
 * @bankgrpnr:	Bank group number
 * @blknr:	Block number
 */
struct ecc_error_info {
	u32 row;
	u32 col;
	u32 bank;
	u32 bitpos;
	u32 data;
	u32 bankgrpnr;
	u32 blknr;
};

/**
 * struct synps_ecc_status - ECC status information to report
 * @ce_cnt:	Correctable error count
 * @ue_cnt:	Uncorrectable error count
 * @ceinfo:	Correctable error log information
 * @ueinfo:	Uncorrectable error log information
 */
struct synps_ecc_status {
	u32 ce_cnt;
	u32 ue_cnt;
	struct ecc_error_info ceinfo;
	struct ecc_error_info ueinfo;
};

/**
 * struct synps_edac_priv - DDR memory controller private instance data
 * @baseaddr:	Base address of the DDR controller
 * @message:	Buffer for framing the event specific info
 * @stat:	ECC status information
 * @p_data:	Pointer to platform data
 * @ce_cnt:	Correctable Error count
 * @ue_cnt:	Uncorrectable Error count
 * @poison_addr:Data poison address
 */
struct synps_edac_priv {
	void __iomem *baseaddr;
	char message[SYNPS_EDAC_MSG_SIZE];
	struct synps_ecc_status stat;
	const struct synps_platform_data *p_data;
	u32 ce_cnt;
	u32 ue_cnt;
	ulong poison_addr;
};

/**
 * struct synps_platform_data -  synps platform data structure
 * @synps_edac_geterror_info:	function pointer to synps edac error info
 * @synps_edac_get_mtype:	function pointer to synps edac mtype
 * @synps_edac_get_dtype:	function pointer to synps edac dtype
 * @synps_edac_get_eccstate:	function pointer to synps edac eccstate
 * @quirks:			to differentiate IPs
 */
struct synps_platform_data {
	int (*synps_edac_geterror_info)(void __iomem *base,
					 struct synps_ecc_status *p);
	enum mem_type (*synps_edac_get_mtype)(const void __iomem *base);
	enum dev_type (*synps_edac_get_dtype)(const void __iomem *base);
	bool (*synps_edac_get_eccstate)(void __iomem *base);
	int quirks;
};

/**
 * synps_edac_geterror_info - Get the current ecc error info
 * @base:	Pointer to the base address of the ddr memory controller
 * @p:		Pointer to the synopsys ecc status structure
 *
 * Determines there is any ecc error or not
 *
 * Return: 1 if there is no error otherwise returns 0
 */
static int synps_edac_geterror_info(void __iomem *base,
				    struct synps_ecc_status *p)
{
	u32 regval, clearval = 0;

	regval = readl(base + STAT_OFST);
	if (!regval)
		return 1;

	p->ce_cnt = (regval & STAT_CECNT_MASK) >> STAT_CECNT_SHIFT;
	p->ue_cnt = regval & STAT_UECNT_MASK;

	regval = readl(base + CE_LOG_OFST);
	if (!(p->ce_cnt && (regval & LOG_VALID)))
		goto ue_err;

	p->ceinfo.bitpos = (regval & CE_LOG_BITPOS_MASK) >> CE_LOG_BITPOS_SHIFT;
	regval = readl(base + CE_ADDR_OFST);
	p->ceinfo.row = (regval & ADDR_ROW_MASK) >> ADDR_ROW_SHIFT;
	p->ceinfo.col = regval & ADDR_COL_MASK;
	p->ceinfo.bank = (regval & ADDR_BANK_MASK) >> ADDR_BANK_SHIFT;
	p->ceinfo.data = readl(base + CE_DATA_31_0_OFST);
	edac_dbg(3, "ce bit position: %d data: %d\n", p->ceinfo.bitpos,
		 p->ceinfo.data);
	clearval = ECC_CTRL_CLR_CE_ERR;

ue_err:
	regval = readl(base + UE_LOG_OFST);
	if (!(p->ue_cnt && (regval & LOG_VALID)))
		goto out;

	regval = readl(base + UE_ADDR_OFST);
	p->ueinfo.row = (regval & ADDR_ROW_MASK) >> ADDR_ROW_SHIFT;
	p->ueinfo.col = regval & ADDR_COL_MASK;
	p->ueinfo.bank = (regval & ADDR_BANK_MASK) >> ADDR_BANK_SHIFT;
	p->ueinfo.data = readl(base + UE_DATA_31_0_OFST);
	clearval |= ECC_CTRL_CLR_UE_ERR;

out:
	writel(clearval, base + ECC_CTRL_OFST);
	writel(0x0, base + ECC_CTRL_OFST);

	return 0;
}

/**
 * synps_enh_edac_geterror_info - Get the current ecc error info
 * @base:	Pointer to the base address of the ddr memory controller
 * @p:		Pointer to the synopsys ecc status structure
 *
 * Determines there is any ecc error or not
 *
 * Return: one if there is no error otherwise returns zero
 */
static int synps_enh_edac_geterror_info(void __iomem *base,
					struct synps_ecc_status *p)
{
	u32 regval, clearval = 0;

	regval = readl(base + ECC_STAT_OFST);
	if (!regval)
		return 1;

	p->ce_cnt = (regval & ECC_STAT_CECNT_MASK) >> ECC_STAT_CECNT_SHIFT;
	p->ue_cnt = (regval & ECC_STAT_UECNT_MASK) >> ECC_STAT_UECNT_SHIFT;
	p->ceinfo.bitpos = (regval & ECC_STAT_BITNUM_MASK);

	regval = readl(base + ECC_CEADDR0_OFST);
	if (!(p->ce_cnt))
		goto ue_err;

	p->ceinfo.row = (regval & ECC_CEADDR0_RW_MASK);
	regval = readl(base + ECC_CEADDR1_OFST);
	p->ceinfo.bank = (regval & ECC_CEADDR1_BNKNR_MASK) >>
					ECC_CEADDR1_BNKNR_SHIFT;
	p->ceinfo.bankgrpnr = (regval &	ECC_CEADDR1_BNKGRP_MASK) >>
					ECC_CEADDR1_BNKGRP_SHIFT;
	p->ceinfo.blknr = (regval & ECC_CEADDR1_BLKNR_MASK);
	p->ceinfo.data = readl(base + ECC_CSYND0_OFST);
	edac_dbg(3, "ce bit position: %d data: %d\n", p->ceinfo.bitpos,
		 p->ceinfo.data);

ue_err:
	regval = readl(base + ECC_UEADDR0_OFST);
	if (!(p->ue_cnt))
		goto out;

	p->ueinfo.row = (regval & ECC_CEADDR0_RW_MASK);
	regval = readl(base + ECC_UEADDR1_OFST);
	p->ueinfo.bankgrpnr = (regval & ECC_CEADDR1_BNKGRP_MASK) >>
					ECC_CEADDR1_BNKGRP_SHIFT;
	p->ueinfo.bank = (regval & ECC_CEADDR1_BNKNR_MASK) >>
					ECC_CEADDR1_BNKNR_SHIFT;
	p->ueinfo.blknr = (regval & ECC_CEADDR1_BLKNR_MASK);
	p->ueinfo.data = readl(base + ECC_UESYND0_OFST);
out:
	clearval = ECC_CTRL_CLR_CE_ERR | ECC_CTRL_CLR_CE_ERRCNT;
	clearval |= ECC_CTRL_CLR_UE_ERR | ECC_CTRL_CLR_UE_ERRCNT;
	writel(clearval, base + ECC_CLR_OFST);
	writel(0x0, base + ECC_CLR_OFST);

	return 0;
}

/**
 * synps_edac_handle_error - Handle controller error types CE and UE
 * @mci:	Pointer to the edac memory controller instance
 * @p:		Pointer to the synopsys ecc status structure
 *
 * Handles the controller ECC correctable and un correctable error.
 */
static void synps_edac_handle_error(struct mem_ctl_info *mci,
				    struct synps_ecc_status *p)
{
	struct synps_edac_priv *priv = mci->pvt_info;
	struct ecc_error_info *pinf;

	if (p->ce_cnt) {
		pinf = &p->ceinfo;
		if (priv->p_data->quirks == 0)
			snprintf(priv->message, SYNPS_EDAC_MSG_SIZE,
				 "DDR ECC error type :%s Row %d Bank %d Col %d ",
				 "CE", pinf->row, pinf->bank, pinf->col);
		else
			snprintf(priv->message, SYNPS_EDAC_MSG_SIZE,
				 "DDR ECC error type :%s Row %d Bank %d Col %d "
				 "BankGroup Number %d Block Number %d",
				 "CE", pinf->row, pinf->bank, pinf->col,
				 pinf->bankgrpnr, pinf->blknr);

		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci,
				     p->ce_cnt, 0, 0, 0, 0, 0, -1,
				     priv->message, "");
	}

	if (p->ue_cnt) {
		pinf = &p->ueinfo;
		if (priv->p_data->quirks == 0)
			snprintf(priv->message, SYNPS_EDAC_MSG_SIZE,
				 "DDR ECC error type :%s Row %d Bank %d Col %d ",
				"UE", pinf->row, pinf->bank, pinf->col);
		else
			snprintf(priv->message, SYNPS_EDAC_MSG_SIZE,
				 "DDR ECC error type :%s Row %d Bank %d Col %d "
				 "BankGroup Number %d Block Number %d",
				 "UE", pinf->row, pinf->bank, pinf->col,
				 pinf->bankgrpnr, pinf->blknr);
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci,
				     p->ue_cnt, 0, 0, 0, 0, 0, -1,
				     priv->message, "");
	}

	memset(p, 0, sizeof(*p));
}

/**
 * synps_edac_intr_handler - synps edac isr
 * @irq:        irq number
 * @dev_id:     device id poniter
 *
 * This is the Isr routine called by edac core interrupt thread.
 * Used to check and post ECC errors.
 *
 * Return: IRQ_NONE, if interrupt not set or IRQ_HANDLED otherwise
 */
static irqreturn_t synps_edac_intr_handler(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;
	struct synps_edac_priv *priv = mci->pvt_info;
	int status, regval;

	regval = readl(priv->baseaddr + DDR_QOS_IRQ_STAT_OFST) &
			(DDR_QOSCE_MASK | DDR_QOSUE_MASK);
	if (!(regval & ECC_CE_UE_INTR_MASK))
		return IRQ_NONE;
	status = priv->p_data->synps_edac_geterror_info(priv->baseaddr,
				&priv->stat);
	if (status)
		return IRQ_NONE;

	priv->ce_cnt += priv->stat.ce_cnt;
	priv->ue_cnt += priv->stat.ue_cnt;
	synps_edac_handle_error(mci, &priv->stat);

	edac_dbg(3, "Total error count ce %d ue %d\n",
		 priv->ce_cnt, priv->ue_cnt);
	writel(regval, priv->baseaddr + DDR_QOS_IRQ_STAT_OFST);
	return IRQ_HANDLED;
}

/**
 * synps_edac_check - Check controller for ECC errors
 * @mci:	Pointer to the edac memory controller instance
 *
 * Used to check and post ECC errors. Called by the polling thread
 */
static void synps_edac_check(struct mem_ctl_info *mci)
{
	struct synps_edac_priv *priv = mci->pvt_info;
	int status;

	status = priv->p_data->synps_edac_geterror_info(priv->baseaddr,
							&priv->stat);
	if (status)
		return;

	priv->ce_cnt += priv->stat.ce_cnt;
	priv->ue_cnt += priv->stat.ue_cnt;
	synps_edac_handle_error(mci, &priv->stat);

	edac_dbg(3, "Total error count ce %d ue %d\n",
		 priv->ce_cnt, priv->ue_cnt);
}

/**
 * synps_edac_get_dtype - Return the controller memory width
 * @base:	Pointer to the ddr memory controller base address
 *
 * Get the EDAC device type width appropriate for the current controller
 * configuration.
 *
 * Return: a device type width enumeration.
 */
static enum dev_type synps_edac_get_dtype(const void __iomem *base)
{
	enum dev_type dt;
	u32 width;

	width = readl(base + CTRL_OFST);
	width = (width & CTRL_BW_MASK) >> CTRL_BW_SHIFT;

	switch (width) {
	case DDRCTL_WDTH_16:
		dt = DEV_X2;
		break;
	case DDRCTL_WDTH_32:
		dt = DEV_X4;
		break;
	default:
		dt = DEV_UNKNOWN;
	}

	return dt;
}

/**
 * synps_enh_edac_get_dtype - Return the controller memory width
 * @base:	Pointer to the ddr memory controller base address
 *
 * Get the EDAC device type width appropriate for the current controller
 * configuration.
 *
 * Return: a device type width enumeration.
 */
static enum dev_type synps_enh_edac_get_dtype(const void __iomem *base)
{
	enum dev_type dt;
	u32 width;

	width = readl(base + CTRL_OFST);
	width = (width & ECC_CTRL_BUSWIDTH_MASK) >>
		ECC_CTRL_BUSWIDTH_SHIFT;
	switch (width) {
	case DDRCTL_EWDTH_16:
		dt = DEV_X2;
		break;
	case DDRCTL_EWDTH_32:
		dt = DEV_X4;
		break;
	case DDRCTL_EWDTH_64:
		dt = DEV_X8;
		break;
	default:
		dt = DEV_UNKNOWN;
	}

	return dt;
}

/**
 * synps_edac_get_eccstate - Return the controller ecc enable/disable status
 * @base:	Pointer to the ddr memory controller base address
 *
 * Get the ECC enable/disable status for the controller
 *
 * Return: a ecc status boolean i.e true/false - enabled/disabled.
 */
static bool synps_edac_get_eccstate(void __iomem *base)
{
	enum dev_type dt;
	u32 ecctype;
	bool state = false;

	dt = synps_edac_get_dtype(base);
	if (dt == DEV_UNKNOWN)
		return state;

	ecctype = readl(base + SCRUB_OFST) & SCRUB_MODE_MASK;
	if ((ecctype == SCRUB_MODE_SECDED) && (dt == DEV_X2))
		state = true;

	return state;
}

/**
 * synps_enh_edac_get_eccstate - Return the controller ecc enable/disable status
 * @base:	Pointer to the ddr memory controller base address
 *
 * Get the ECC enable/disable status for the controller
 *
 * Return: a ecc status boolean i.e true/false - enabled/disabled.
 */
static bool synps_enh_edac_get_eccstate(void __iomem *base)
{
	enum dev_type dt;
	u32 ecctype;
	bool state = false;

	dt = synps_enh_edac_get_dtype(base);
	if (dt == DEV_UNKNOWN)
		return state;

	ecctype = readl(base + ECC_CFG0_OFST) & SCRUB_MODE_MASK;
	if ((ecctype == SCRUB_MODE_SECDED) &&
	    ((dt == DEV_X2) || (dt == DEV_X4) || (dt == DEV_X8)))
		state = true;

	return state;
}

/**
 * synps_edac_get_memsize - reads the size of the attached memory device
 *
 * Return: the memory size in bytes
 */
static u32 synps_edac_get_memsize(void)
{
	struct sysinfo inf;

	si_meminfo(&inf);

	return inf.totalram * inf.mem_unit;
}

/**
 * synps_edac_get_mtype - Returns controller memory type
 * @base:	pointer to the synopsys ecc status structure
 *
 * Get the EDAC memory type appropriate for the current controller
 * configuration.
 *
 * Return: a memory type enumeration.
 */
static enum mem_type synps_edac_get_mtype(const void __iomem *base)
{
	enum mem_type mt;
	u32 memtype;

	memtype = readl(base + T_ZQ_OFST);

	if (memtype & T_ZQ_DDRMODE_MASK)
		mt = MEM_DDR3;
	else
		mt = MEM_DDR2;

	return mt;
}

/**
 * synps_enh_edac_get_mtype - Returns controller memory type
 * @base:	pointer to the synopsys ecc status structure
 *
 * Get the EDAC memory type appropriate for the current controller
 * configuration.
 *
 * Return: a memory type enumeration.
 */
static enum mem_type synps_enh_edac_get_mtype(const void __iomem *base)
{
	enum mem_type mt;
	u32 memtype;

	memtype = readl(base + CTRL_OFST);

	mt = MEM_UNKNOWN;
	if ((memtype & MEM_TYPE_DDR3) || (memtype & MEM_TYPE_LPDDR3))
		mt = MEM_DDR3;
	else if (memtype & MEM_TYPE_DDR2)
		mt = MEM_RDDR2;
	else if ((memtype & MEM_TYPE_LPDDR4) || (memtype & MEM_TYPE_DDR4))
		mt = MEM_DDR4;

	return mt;
}

/**
 * synps_edac_init_csrows - Initialize the cs row data
 * @mci:	Pointer to the edac memory controller instance
 *
 * Initializes the chip select rows associated with the EDAC memory
 * controller instance
 *
 * Return: Unconditionally 0.
 */
static int synps_edac_init_csrows(struct mem_ctl_info *mci)
{
	struct csrow_info *csi;
	struct dimm_info *dimm;
	struct synps_edac_priv *priv = mci->pvt_info;
	u32 size;
	int row, j;

	for (row = 0; row < mci->nr_csrows; row++) {
		csi = mci->csrows[row];
		size = synps_edac_get_memsize();

		for (j = 0; j < csi->nr_channels; j++) {
			dimm            = csi->channels[j]->dimm;
			dimm->edac_mode = EDAC_FLAG_SECDED;
			dimm->mtype     = priv->p_data->synps_edac_get_mtype(
						priv->baseaddr);
			dimm->nr_pages  = (size >> PAGE_SHIFT) / csi->nr_channels;
			dimm->grain     = SYNPS_EDAC_ERR_GRAIN;
			dimm->dtype     = priv->p_data->synps_edac_get_dtype(
						priv->baseaddr);
		}
	}

	return 0;
}

/**
 * synps_edac_mc_init - Initialize driver instance
 * @mci:	Pointer to the edac memory controller instance
 * @pdev:	Pointer to the platform_device struct
 *
 * Performs initialization of the EDAC memory controller instance and
 * related driver-private data associated with the memory controller the
 * instance is bound to.
 *
 * Return: Always zero.
 */
static int synps_edac_mc_init(struct mem_ctl_info *mci,
				 struct platform_device *pdev)
{
	int status;
	struct synps_edac_priv *priv;

	mci->pdev = &pdev->dev;
	priv = mci->pvt_info;
	platform_set_drvdata(pdev, mci);

	/* Initialize controller capabilities and configuration */
	mci->mtype_cap = MEM_FLAG_DDR3 | MEM_FLAG_DDR2;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->scrub_cap = SCRUB_HW_SRC;
	mci->scrub_mode = SCRUB_NONE;

	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->ctl_name = "synps_ddr_controller";
	mci->dev_name = SYNPS_EDAC_MOD_STRING;
	mci->mod_name = SYNPS_EDAC_MOD_VER;
	mci->mod_ver = "1";
	if (priv->p_data->quirks & DDR_ECC_INTR_SUPPORT) {
		edac_op_state = EDAC_OPSTATE_INT;
	} else {
		edac_op_state = EDAC_OPSTATE_POLL;
		mci->edac_check = synps_edac_check;
	}
	mci->ctl_page_to_phys = NULL;

	status = synps_edac_init_csrows(mci);

	return status;
}

static const struct synps_platform_data zynq_edac_def = {
	.synps_edac_geterror_info	= synps_edac_geterror_info,
	.synps_edac_get_mtype		= synps_edac_get_mtype,
	.synps_edac_get_dtype		= synps_edac_get_dtype,
	.synps_edac_get_eccstate	= synps_edac_get_eccstate,
	.quirks				= 0,
};

static const struct synps_platform_data zynqmp_enh_edac_def = {
	.synps_edac_geterror_info	= synps_enh_edac_geterror_info,
	.synps_edac_get_mtype		= synps_enh_edac_get_mtype,
	.synps_edac_get_dtype		= synps_enh_edac_get_dtype,
	.synps_edac_get_eccstate	= synps_enh_edac_get_eccstate,
	.quirks				= (DDR_ECC_INTR_SUPPORT |
					   DDR_ECC_DATA_POISON_SUPPORT),
};

static const struct of_device_id synps_edac_match[] = {
	{ .compatible = "xlnx,zynq-ddrc-a05", .data = (void *)&zynq_edac_def },
	{ .compatible = "xlnx,zynqmp-ddrc-2.40a",
				.data = (void *)&zynqmp_enh_edac_def},
	{ /* end of table */ }
};

MODULE_DEVICE_TABLE(of, synps_edac_match);

#define to_mci(k) container_of(k, struct mem_ctl_info, dev)

/**
 * ddr4_poison_setup - update poison registers
 * @dttype:		Device structure variable
 * @device_config:	Device configuration
 * @priv:		Pointer to synps_edac_priv struct
 *
 * Update poison registers as per ddr4 mapping
 * Return: none.
 */
static void ddr4_poison_setup(enum dev_type dttype, int device_config,
				struct synps_edac_priv *priv)
{
	int col, row, bank, bankgrp, regval, shift_val = 0, col_shift;

	/* Check the Configuration of the device */
	if (device_config & DDRC_MSTR_DEV_CONFIG_X8_MASK) {
		/* For Full Dq bus */
		if (dttype == DEV_X8)
			shift_val = 0;
		/* For Half Dq bus */
		else if (dttype == DEV_X4)
			shift_val = 1;
		col_shift = 0;
	} else if (device_config & DDRC_MSTR_DEV_CONFIG_X16_MASK) {
		if (dttype == DEV_X8)
			shift_val = 1;
		else if (dttype == DEV_X4)
			shift_val = 2;
		col_shift = 1;
	}

	col = (priv->poison_addr >> (DDR4_COL_SHIFT -
				(shift_val - col_shift))) &
				DDR4_COL_MASK;
	row = priv->poison_addr >> (DDR4_ROW_SHIFT - shift_val);
	row &= DDR4_ROW_MASK;
	bank = priv->poison_addr >> (DDR4_BANK_SHIFT - shift_val);
	bank &= DDR4_BANK_MASK;
	bankgrp = (priv->poison_addr >> (DDR4_BANKGRP_SHIFT -
				(shift_val - col_shift))) &
				DDR4_BANKGRP_MASK;

	writel(col, priv->baseaddr + ECC_POISON0_OFST);
	regval = (bankgrp << ECC_POISON1_BANKGRP_SHIFT) |
		 (bank << ECC_POISON1_BANKNR_SHIFT) | row;
	writel(regval, priv->baseaddr + ECC_POISON1_OFST);
}

/**
 * ddr3_poison_setup - update poison registers
 * @dttype:		Device structure variable
 * @device_config:	Device configuration
 * @priv:		Pointer to synps_edac_priv struct
 *
 * Update poison registers as per ddr3 mapping
 * Return: none.
 */
static void ddr3_poison_setup(enum dev_type dttype, int device_config,
				struct synps_edac_priv *priv)
{
	int col, row, bank, bankgrp, regval, shift_val = 0;

	if (dttype == DEV_X8)
		/* For Full Dq bus */
		shift_val = 0;
	else if (dttype == DEV_X4)
		/* For Half Dq bus */
		shift_val = 1;

	col = (priv->poison_addr >> (DDR3_COL_SHIFT - shift_val)) &
		DDR3_COL_MASK;
	row = priv->poison_addr >> (DDR3_ROW_SHIFT - shift_val);
	row &= DDR3_ROW_MASK;
	bank = priv->poison_addr >> (DDR3_BANK_SHIFT - shift_val);
	bank &= DDR3_BANK_MASK;
	bankgrp = 0;
	writel(col, priv->baseaddr + ECC_POISON0_OFST);
	regval = (bankgrp << ECC_POISON1_BANKGRP_SHIFT) |
			 (bank << ECC_POISON1_BANKNR_SHIFT) | row;
	writel(regval, priv->baseaddr + ECC_POISON1_OFST);
}

/**
 * synps_edac_mc_inject_data_error_show - Get Poison0 & 1 register contents
 * @dev:	Pointer to the device struct
 * @mattr:	Pointer to device attributes
 * @data:	Pointer to user data
 *
 * Get the Poison0 and Poison1 register contents
 * Return: Number of bytes copied.
 */
static ssize_t synps_edac_mc_inject_data_error_show(struct device *dev,
					      struct device_attribute *mattr,
					      char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct synps_edac_priv *priv = mci->pvt_info;

	return sprintf(data, "Poison0 Addr: 0x%08x\n\rPoison1 Addr: 0x%08x\n\r"
			"Error injection Address: 0x%lx\n\r",
			readl(priv->baseaddr + ECC_POISON0_OFST),
			readl(priv->baseaddr + ECC_POISON1_OFST),
			priv->poison_addr);
}

/**
 * synps_edac_mc_inject_data_error_store - Configure Poison0 Poison1 registers
 * @dev:	Pointer to the device struct
 * @mattr:	Pointer to device attributes
 * @data:	Pointer to user data
 * @count:	read the size bytes from buffer
 *
 * Configures the Poison0 and Poison1 register contents as per user given
 * address
 * Return: Number of bytes copied.
 */
static ssize_t synps_edac_mc_inject_data_error_store(struct device *dev,
					       struct device_attribute *mattr,
					       const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct synps_edac_priv *priv = mci->pvt_info;
	int device_config;
	enum mem_type mttype;
	enum dev_type dttype;

	mttype = priv->p_data->synps_edac_get_mtype(
						priv->baseaddr);
	dttype = priv->p_data->synps_edac_get_dtype(
						priv->baseaddr);
	if (kstrtoul(data, 0, &priv->poison_addr))
		return -EINVAL;

	device_config = readl(priv->baseaddr + CTRL_OFST);
	device_config = (device_config & DDRC_MSTR_DEV_CONFIG_MASK) >>
					DDRC_MSTR_DEV_CONFIG_SHIFT;
	if (mttype == MEM_DDR4)
		ddr4_poison_setup(dttype, device_config, priv);
	else if (mttype == MEM_DDR3)
		ddr3_poison_setup(dttype, device_config, priv);

	return count;
}

/**
 * synps_edac_mc_inject_data_poison_show - Shows type of Data poison
 * @dev:	Pointer to the device struct
 * @mattr:	Pointer to device attributes
 * @data:	Pointer to user data
 *
 * Shows the type of Error injection enabled, either UE or CE
 * Return: Number of bytes copied.
 */
static ssize_t synps_edac_mc_inject_data_poison_show(struct device *dev,
					      struct device_attribute *mattr,
					      char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct synps_edac_priv *priv = mci->pvt_info;

	return sprintf(data, "Data Poisoning: %s\n\r",
			((readl(priv->baseaddr + ECC_CFG1_OFST)) & 0x3) ?
			("Correctable Error"):("UnCorrectable Error"));
}

/**
 * synps_edac_mc_inject_data_poison_store - Enbles Data poison CE/UE
 * @dev:	Pointer to the device struct
 * @mattr:	Pointer to device attributes
 * @data:	Pointer to user data
 * @count:	read the size bytes from buffer
 *
 * Enables the CE or UE Data poison
 * Return: Number of bytes copied.
 */
static ssize_t synps_edac_mc_inject_data_poison_store(struct device *dev,
					       struct device_attribute *mattr,
					       const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct synps_edac_priv *priv = mci->pvt_info;

	writel(0, priv->baseaddr + DDRC_SWCTL);
	if (strncmp(data, "CE", 2) == 0)
		writel(ECC_CEPOISON_MASK, priv->baseaddr + ECC_CFG1_OFST);
	else
		writel(ECC_UEPOISON_MASK, priv->baseaddr + ECC_CFG1_OFST);
	writel(1, priv->baseaddr + DDRC_SWCTL);

	return count;
}

static DEVICE_ATTR(inject_data_error, S_IRUGO | S_IWUSR,
	    synps_edac_mc_inject_data_error_show,
	    synps_edac_mc_inject_data_error_store);
static DEVICE_ATTR(inject_data_poison, S_IRUGO | S_IWUSR,
	    synps_edac_mc_inject_data_poison_show,
	    synps_edac_mc_inject_data_poison_store);

/**
 * synps_edac_create_sysfs_attributes - Create sysfs entries
 * @mci:	Pointer to the edac memory controller instance
 *
 * Create sysfs attributes for injecting ECC errors using data poison.
 *
 * Return: 0 if sysfs creation was successful, else return negative error code.
 */
static int synps_edac_create_sysfs_attributes(struct mem_ctl_info *mci)
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

/**
 * synps_edac_remove_sysfs_attributes - Removes sysfs entries
 * @mci:	Pointer to the edac memory controller instance
 *
 * Removes sysfs attributes.
 *
 * Return: none.
 */
static void synps_edac_remove_sysfs_attributes(struct mem_ctl_info *mci)
{
	device_remove_file(&mci->dev, &dev_attr_inject_data_error);
	device_remove_file(&mci->dev, &dev_attr_inject_data_poison);
}

/**
 * synps_edac_mc_probe - Check controller and bind driver
 * @pdev:	Pointer to the platform_device struct
 *
 * Probes a specific controller instance for binding with the driver.
 *
 * Return: 0 if the controller instance was successfully bound to the
 * driver; otherwise, < 0 on error.
 */
static int synps_edac_mc_probe(struct platform_device *pdev)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct synps_edac_priv *priv;
	int rc, irq, status;
	struct resource *res;
	void __iomem *baseaddr;
	const struct of_device_id *match;
	const struct synps_platform_data *p_data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	baseaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(baseaddr))
		return PTR_ERR(baseaddr);

	match = of_match_node(synps_edac_match, pdev->dev.of_node);
	if (!match && !match->data) {
		dev_err(&pdev->dev, "of_match_node() failed\n");
		return -EINVAL;
	}

	p_data = (struct synps_platform_data *)match->data;
	if (!(p_data->synps_edac_get_eccstate(baseaddr))) {
		edac_printk(KERN_INFO, EDAC_MC, "ECC not enabled\n");
		return -ENXIO;
	}

	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = SYNPS_EDAC_NR_CSROWS;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = SYNPS_EDAC_NR_CHANS;
	layers[1].is_virt_csrow = false;

	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers,
			    sizeof(struct synps_edac_priv));
	if (!mci) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Failed memory allocation for mc instance\n");
		return -ENOMEM;
	}

	priv = mci->pvt_info;
	priv->baseaddr = baseaddr;
	priv->p_data = match->data;

	rc = synps_edac_mc_init(mci, pdev);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Failed to initialize instance\n");
		goto free_edac_mc;
	}

	if (priv->p_data->quirks & DDR_ECC_INTR_SUPPORT) {
		irq = platform_get_irq(pdev, 0);
		if (irq < 0) {
			edac_printk(KERN_ERR, EDAC_MC,
					"No irq %d in DT\n", irq);
			return -ENODEV;
		}

		status = devm_request_irq(&pdev->dev, irq,
			synps_edac_intr_handler,
			0, dev_name(&pdev->dev), mci);
		if (status < 0) {
			edac_printk(KERN_ERR, EDAC_MC, "Failed to request Irq\n");
			goto free_edac_mc;
		}

		/* Enable UE/CE Interrupts */
		writel((DDR_QOSUE_MASK | DDR_QOSCE_MASK),
			priv->baseaddr + DDR_QOS_IRQ_EN_OFST);
	}

	rc = edac_mc_add_mc(mci);
	if (rc) {
		edac_printk(KERN_ERR, EDAC_MC,
			    "Failed to register with EDAC core\n");
		goto free_edac_mc;
	}

	if (priv->p_data->quirks & DDR_ECC_DATA_POISON_SUPPORT) {
		if (synps_edac_create_sysfs_attributes(mci)) {
			edac_printk(KERN_ERR, EDAC_MC,
					"Failed to create sysfs entries\n");
			goto free_edac_mc;
		}
	}
	/*
	 * Start capturing the correctable and uncorrectable errors. A write of
	 * 0 starts the counters.
	 */
	if (!(priv->p_data->quirks & DDR_ECC_INTR_SUPPORT))
		writel(0x0, baseaddr + ECC_CTRL_OFST);
	return rc;

free_edac_mc:
	edac_mc_free(mci);

	return rc;
}

/**
 * synps_edac_mc_remove - Unbind driver from controller
 * @pdev:	Pointer to the platform_device struct
 *
 * Return: Unconditionally 0
 */
static int synps_edac_mc_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);
	struct synps_edac_priv *priv;

	priv = mci->pvt_info;
	if (priv->p_data->quirks & DDR_ECC_INTR_SUPPORT)
		/* Disable UE/CE Interrupts */
		writel((DDR_QOSUE_MASK | DDR_QOSCE_MASK),
			priv->baseaddr + DDR_QOS_IRQ_DB_OFST);
	edac_mc_del_mc(&pdev->dev);
	if (priv->p_data->quirks & DDR_ECC_DATA_POISON_SUPPORT)
		synps_edac_remove_sysfs_attributes(mci);
	edac_mc_free(mci);

	return 0;
}

static struct platform_driver synps_edac_mc_driver = {
	.driver = {
		   .name = "synopsys-edac",
		   .of_match_table = synps_edac_match,
		   },
	.probe = synps_edac_mc_probe,
	.remove = synps_edac_mc_remove,
};

module_platform_driver(synps_edac_mc_driver);

MODULE_AUTHOR("Xilinx Inc");
MODULE_DESCRIPTION("Synopsys DDR ECC driver");
MODULE_LICENSE("GPL v2");
