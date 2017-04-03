/*
 * Arasan NAND Flash Controller Driver
 *
 * Copyright (C) 2014 - 2017 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define DRIVER_NAME			"arasan_nand"
#define EVNT_TIMEOUT_MSEC		1000
#define ANFC_PM_TIMEOUT		1000	/* ms */

#define PKT_OFST			0x00
#define MEM_ADDR1_OFST			0x04
#define MEM_ADDR2_OFST			0x08
#define CMD_OFST			0x0C
#define PROG_OFST			0x10
#define INTR_STS_EN_OFST		0x14
#define INTR_SIG_EN_OFST		0x18
#define INTR_STS_OFST			0x1C
#define READY_STS_OFST			0x20
#define DMA_ADDR1_OFST			0x24
#define FLASH_STS_OFST			0x28
#define DATA_PORT_OFST			0x30
#define ECC_OFST			0x34
#define ECC_ERR_CNT_OFST		0x38
#define ECC_SPR_CMD_OFST		0x3C
#define ECC_ERR_CNT_1BIT_OFST		0x40
#define ECC_ERR_CNT_2BIT_OFST		0x44
#define DMA_ADDR0_OFST			0x50
#define DATA_INTERFACE_OFST		0x6C

#define PKT_CNT_SHIFT			12

#define ECC_ENABLE			BIT(31)
#define DMA_EN_MASK			GENMASK(27, 26)
#define DMA_ENABLE			0x2
#define DMA_EN_SHIFT			26
#define REG_PAGE_SIZE_SHIFT		23
#define REG_PAGE_SIZE_512		0
#define REG_PAGE_SIZE_1K		5
#define REG_PAGE_SIZE_2K		1
#define REG_PAGE_SIZE_4K		2
#define REG_PAGE_SIZE_8K		3
#define REG_PAGE_SIZE_16K		4
#define CMD2_SHIFT			8
#define ADDR_CYCLES_SHIFT		28

#define XFER_COMPLETE			BIT(2)
#define READ_READY			BIT(1)
#define WRITE_READY			BIT(0)
#define MBIT_ERROR			BIT(3)

#define PROG_PGRD			BIT(0)
#define PROG_ERASE			BIT(2)
#define PROG_STATUS			BIT(3)
#define PROG_PGPROG			BIT(4)
#define PROG_RDID			BIT(6)
#define PROG_RDPARAM			BIT(7)
#define PROG_RST			BIT(8)
#define PROG_GET_FEATURE		BIT(9)
#define PROG_SET_FEATURE		BIT(10)

#define PG_ADDR_SHIFT			16
#define BCH_MODE_SHIFT			25
#define BCH_EN_SHIFT			27
#define ECC_SIZE_SHIFT			16

#define MEM_ADDR_MASK			GENMASK(7, 0)
#define BCH_MODE_MASK			GENMASK(27, 25)

#define CS_MASK				GENMASK(31, 30)
#define CS_SHIFT			30

#define PAGE_ERR_CNT_MASK		GENMASK(16, 8)
#define PKT_ERR_CNT_MASK		GENMASK(7, 0)

#define NVDDR_MODE			BIT(9)
#define NVDDR_TIMING_MODE_SHIFT		3

#define ONFI_ID_LEN			8
#define TEMP_BUF_SIZE			1024
#define NVDDR_MODE_PACKET_SIZE		8
#define SDR_MODE_PACKET_SIZE		4

#define ONFI_DATA_INTERFACE_NVDDR      BIT(4)
#define EVENT_MASK	(XFER_COMPLETE | READ_READY | WRITE_READY | MBIT_ERROR)

#define SDR_MODE_DEFLT_FREQ		80000000
#define ONDIE_ECC_FEATURE_ADDR	0x90
#define ONFI_FEATURE_ON_DIE_ECC_EN	BIT(3)

/**
 * struct anfc_nand_chip - Defines the nand chip related information
 * @node:		used to store NAND chips into a list.
 * @chip:		NAND chip information structure.
 * @bch:		Bch / Hamming mode enable/disable.
 * @bchmode:		Bch mode.
 * @eccval:		Ecc config value.
 * @raddr_cycles:	Row address cycle information.
 * @caddr_cycles:	Column address cycle information.
 * @pktsize:		Packet size for read / write operation.
 * @csnum:		chipselect number to be used.
 * @spktsize:		Packet size in ddr mode for status operation.
 * @inftimeval:		Data interface and timing mode information
 */
struct anfc_nand_chip {
	struct list_head node;
	struct nand_chip chip;
	bool bch;
	u32 bchmode;
	u32 eccval;
	u16 raddr_cycles;
	u16 caddr_cycles;
	u32 pktsize;
	int csnum;
	u32 spktsize;
	u32 inftimeval;
};

/**
 * struct anfc - Defines the Arasan NAND flash driver instance
 * @controller:		base controller structure.
 * @chips:		list of all nand chips attached to the ctrler.
 * @dev:		Pointer to the device structure.
 * @base:		Virtual address of the NAND flash device.
 * @curr_cmd:		Current command issued.
 * @clk_sys:		Pointer to the system clock.
 * @clk_flash:		Pointer to the flash clock.
 * @dma:		Dma enable/disable.
 * @iswriteoob:		Identifies if oob write operation is required.
 * @buf:		Buffer used for read/write byte operations.
 * @irq:		irq number
 * @bufshift:		Variable used for indexing buffer operation
 * @csnum:		Chip select number currently inuse.
 * @event:		Completion event for nand status events.
 * @status:		Status of the flash device
 */
struct anfc {
	struct nand_hw_control controller;
	struct list_head chips;
	struct device *dev;
	void __iomem *base;
	int curr_cmd;
	struct clk *clk_sys;
	struct clk *clk_flash;
	bool dma;
	bool iswriteoob;
	u8 buf[TEMP_BUF_SIZE];
	int irq;
	u32 bufshift;
	int csnum;
	struct completion event;
	int status;
};

static int anfc_ooblayout_ecc(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *oobregion)
{
	struct nand_chip *nand = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oobregion->length = nand->ecc.total;
	oobregion->offset = mtd->oobsize - oobregion->length;

	return 0;
}

static int anfc_ooblayout_free(struct mtd_info *mtd, int section,
				     struct mtd_oob_region *oobregion)
{
	struct nand_chip *nand = mtd_to_nand(mtd);

	if (section)
		return -ERANGE;

	oobregion->offset = 2;
	oobregion->length = mtd->oobsize - nand->ecc.total - 2;

	return 0;
}

static const struct mtd_ooblayout_ops anfc_ooblayout_ops = {
	.ecc = anfc_ooblayout_ecc,
	.free = anfc_ooblayout_free,
};

static inline struct anfc_nand_chip *to_anfc_nand(struct nand_chip *nand)
{
	return container_of(nand, struct anfc_nand_chip, chip);
}

static inline struct anfc *to_anfc(struct nand_hw_control *ctrl)
{
	return container_of(ctrl, struct anfc, controller);
}

static u8 anfc_page(u32 pagesize)
{
	switch (pagesize) {
	case 512:
		return REG_PAGE_SIZE_512;
	case 1024:
		return REG_PAGE_SIZE_1K;
	case 2048:
		return REG_PAGE_SIZE_2K;
	case 4096:
		return REG_PAGE_SIZE_4K;
	case 8192:
		return REG_PAGE_SIZE_8K;
	case 16384:
		return REG_PAGE_SIZE_16K;
	default:
		break;
	}

	return 0;
}

static inline void anfc_enable_intrs(struct anfc *nfc, u32 val)
{
	writel(val, nfc->base + INTR_STS_EN_OFST);
	writel(val, nfc->base + INTR_SIG_EN_OFST);
}

static inline void anfc_config_ecc(struct anfc *nfc, int on)
{
	u32 val;

	val = readl(nfc->base + CMD_OFST);
	if (on)
		val |= ECC_ENABLE;
	else
		val &= ~ECC_ENABLE;
	writel(val, nfc->base + CMD_OFST);
}

static inline void anfc_config_dma(struct anfc *nfc, int on)
{
	u32 val;

	val = readl(nfc->base + CMD_OFST);
	val &= ~DMA_EN_MASK;
	if (on)
		val |= DMA_ENABLE << DMA_EN_SHIFT;
	writel(val, nfc->base + CMD_OFST);
}

static inline int anfc_wait_for_event(struct anfc *nfc)
{
	return wait_for_completion_timeout(&nfc->event,
					msecs_to_jiffies(EVNT_TIMEOUT_MSEC));
}

static inline void anfc_setpktszcnt(struct anfc *nfc, u32 pktsize,
				    u32 pktcount)
{
	writel(pktsize | (pktcount << PKT_CNT_SHIFT), nfc->base + PKT_OFST);
}

static inline void anfc_set_eccsparecmd(struct anfc *nfc,
				struct anfc_nand_chip *achip, u8 cmd1, u8 cmd2)
{
	writel(cmd1 | (cmd2 << CMD2_SHIFT) |
	       (achip->caddr_cycles << ADDR_CYCLES_SHIFT),
	       nfc->base + ECC_SPR_CMD_OFST);
}

static void anfc_setpagecoladdr(struct anfc *nfc, u32 page, u16 col)
{
	u32 val;

	writel(col | (page << PG_ADDR_SHIFT), nfc->base + MEM_ADDR1_OFST);

	val = readl(nfc->base + MEM_ADDR2_OFST);
	val = (val & ~MEM_ADDR_MASK) |
	      ((page >> PG_ADDR_SHIFT) & MEM_ADDR_MASK);
	writel(val, nfc->base + MEM_ADDR2_OFST);
}

static void anfc_prepare_cmd(struct anfc *nfc, u8 cmd1, u8 cmd2, u8 dmamode,
			     u32 pagesize, u8 addrcycles)
{
	u32 regval;

	regval = cmd1 | (cmd2 << CMD2_SHIFT);
	if (dmamode && nfc->dma)
		regval |= DMA_ENABLE << DMA_EN_SHIFT;
	regval |= addrcycles << ADDR_CYCLES_SHIFT;
	regval |= anfc_page(pagesize) << REG_PAGE_SIZE_SHIFT;
	writel(regval, nfc->base + CMD_OFST);
}

static int anfc_write_oob(struct mtd_info *mtd, struct nand_chip *chip,
			  int page)
{
	struct anfc *nfc = to_anfc(chip->controller);

	nfc->iswriteoob = true;
	chip->cmdfunc(mtd, NAND_CMD_SEQIN, mtd->writesize, page);
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);
	nfc->iswriteoob = false;

	return 0;
}

static void anfc_rw_buf_dma(struct mtd_info *mtd, uint8_t *buf, int len,
			    int operation, u32 prog)
{
	dma_addr_t paddr;
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct anfc *nfc = to_anfc(chip->controller);
	struct anfc_nand_chip *achip = to_anfc_nand(chip);
	u32 eccintr = 0, dir;
	u32 pktsize = len, pktcount = 1;

	if ((nfc->curr_cmd == NAND_CMD_READ0) ||
		((nfc->curr_cmd == NAND_CMD_SEQIN) && !nfc->iswriteoob)) {
		pktsize = achip->pktsize;
		pktcount = DIV_ROUND_UP(mtd->writesize, pktsize);
	}
	anfc_setpktszcnt(nfc, pktsize, pktcount);

	if (!achip->bch && (nfc->curr_cmd == NAND_CMD_READ0))
		eccintr = MBIT_ERROR;

	if (operation)
		dir = DMA_FROM_DEVICE;
	else
		dir = DMA_TO_DEVICE;

	paddr = dma_map_single(nfc->dev, buf, len, dir);
	if (dma_mapping_error(nfc->dev, paddr)) {
		dev_err(nfc->dev, "Read buffer mapping error");
		return;
	}
	lo_hi_writeq(paddr, nfc->base + DMA_ADDR0_OFST);
	anfc_enable_intrs(nfc, (XFER_COMPLETE | eccintr));
	writel(prog, nfc->base + PROG_OFST);
	anfc_wait_for_event(nfc);
	dma_unmap_single(nfc->dev, paddr, len, dir);
}

static void anfc_rw_buf_pio(struct mtd_info *mtd, uint8_t *buf, int len,
			    int operation, int prog)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct anfc *nfc = to_anfc(chip->controller);
	struct anfc_nand_chip *achip = to_anfc_nand(chip);
	u32 *bufptr = (u32 *)buf;
	u32 cnt = 0, intr = 0;
	u32 pktsize = len, pktcount = 1;

	anfc_config_dma(nfc, 0);

	if ((nfc->curr_cmd == NAND_CMD_READ0) ||
		((nfc->curr_cmd == NAND_CMD_SEQIN) && !nfc->iswriteoob)) {
		pktsize = achip->pktsize;
		pktcount = DIV_ROUND_UP(mtd->writesize, pktsize);
	}
	anfc_setpktszcnt(nfc, pktsize, pktcount);

	if (!achip->bch && (nfc->curr_cmd == NAND_CMD_READ0))
		intr = MBIT_ERROR;

	if (operation)
		intr |= READ_READY;
	else
		intr |= WRITE_READY;

	anfc_enable_intrs(nfc, intr);
	writel(prog, nfc->base + PROG_OFST);

	while (cnt < pktcount) {
		anfc_wait_for_event(nfc);
		cnt++;
		if (cnt == pktcount)
			anfc_enable_intrs(nfc, XFER_COMPLETE);
		if (operation)
			ioread32_rep(nfc->base + DATA_PORT_OFST, bufptr,
				     pktsize/4);
		else
			iowrite32_rep(nfc->base + DATA_PORT_OFST, bufptr,
				      pktsize/4);
		bufptr += (pktsize / 4);
		if (cnt < pktcount)
			anfc_enable_intrs(nfc, intr);
	}

	anfc_wait_for_event(nfc);
}

static void anfc_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct anfc *nfc = to_anfc(chip->controller);

	if (nfc->dma && !is_vmalloc_addr(buf))
		anfc_rw_buf_dma(mtd, buf, len, 1, PROG_PGRD);
	else
		anfc_rw_buf_pio(mtd, buf, len, 1, PROG_PGRD);
}

static void anfc_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct anfc *nfc = to_anfc(chip->controller);

	if (nfc->dma && !is_vmalloc_addr(buf))
		anfc_rw_buf_dma(mtd, (char *)buf, len, 0, PROG_PGPROG);
	else
		anfc_rw_buf_pio(mtd, (char *)buf, len, 0, PROG_PGPROG);
}

static int anfc_read_page_hwecc(struct mtd_info *mtd,
				struct nand_chip *chip, uint8_t *buf,
				int oob_required, int page)
{
	u32 val;
	struct anfc *nfc = to_anfc(chip->controller);
	struct anfc_nand_chip *achip = to_anfc_nand(chip);
	u8 *ecc_code = chip->buffers->ecccode;
	u8 *p = buf;
	int eccsize = chip->ecc.size;
	int eccbytes = chip->ecc.bytes;
	int eccsteps = chip->ecc.steps;
	int stat = 0, i;

	anfc_set_eccsparecmd(nfc, achip, NAND_CMD_RNDOUT, NAND_CMD_RNDOUTSTART);
	anfc_config_ecc(nfc, 1);

	val = readl(nfc->base + CMD_OFST);
	val = val | ECC_ENABLE;
	writel(val, nfc->base + CMD_OFST);

	chip->read_buf(mtd, buf, mtd->writesize);

	val = readl(nfc->base + ECC_ERR_CNT_OFST);
	val = ((val & PAGE_ERR_CNT_MASK) >> 8);
	if (achip->bch) {
		mtd->ecc_stats.corrected += val;
	} else {
		val = readl(nfc->base + ECC_ERR_CNT_1BIT_OFST);
		mtd->ecc_stats.corrected += val;
		val = readl(nfc->base + ECC_ERR_CNT_2BIT_OFST);
		mtd->ecc_stats.failed += val;
		/* Clear ecc error count register 1Bit, 2Bit */
		writel(0x0, nfc->base + ECC_ERR_CNT_1BIT_OFST);
		writel(0x0, nfc->base + ECC_ERR_CNT_2BIT_OFST);
	}

	if (oob_required)
		chip->ecc.read_oob(mtd, chip, page);

	if (val) {
		anfc_config_ecc(nfc, 0);
		chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, page);
		chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);
		mtd_ooblayout_get_eccbytes(mtd, ecc_code, chip->oob_poi, 0,
					   chip->ecc.total);
		for (i = 0 ; eccsteps; eccsteps--, i += eccbytes,
		     p += eccsize) {
			stat = nand_check_erased_ecc_chunk(p,
				chip->ecc.size, &ecc_code[i], eccbytes,
				NULL, 0, chip->ecc.strength);
		}
		if (stat < 0)
			stat = 0;
		else
			mtd->ecc_stats.corrected += stat;
		return stat;
	}

	return 0;
}

static int anfc_write_page_hwecc(struct mtd_info *mtd,
				 struct nand_chip *chip, const uint8_t *buf,
				 int oob_required, int page)
{
	int ret;
	struct anfc *nfc = to_anfc(chip->controller);
	struct anfc_nand_chip *achip = to_anfc_nand(chip);
	uint8_t *ecc_calc = chip->buffers->ecccalc;

	anfc_set_eccsparecmd(nfc, achip, NAND_CMD_RNDIN, 0);
	anfc_config_ecc(nfc, 1);

	chip->write_buf(mtd, buf, mtd->writesize);

	if (oob_required) {
		chip->waitfunc(mtd, chip);
		chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, page);
		chip->read_buf(mtd, ecc_calc, mtd->oobsize);
		ret = mtd_ooblayout_set_eccbytes(mtd, ecc_calc, chip->oob_poi,
						 0, chip->ecc.total);
		if (ret)
			return ret;
		chip->ecc.write_oob(mtd, chip, page);
	}

	return 0;
}

static int anfc_read_page(struct mtd_info *mtd,
			  struct nand_chip *chip, uint8_t *buf,
			  int oob_required, int page)
{
	chip->read_buf(mtd, buf, mtd->writesize);
	if (oob_required)
		chip->ecc.read_oob(mtd, chip, page);

	return 0;
}

static int anfc_write_page(struct mtd_info *mtd,
			   struct nand_chip *chip, const uint8_t *buf,
			   int oob_required, int page)
{
	chip->write_buf(mtd, buf, mtd->writesize);
	if (oob_required)
		chip->ecc.write_oob(mtd, chip, page);

	return 0;
}

static u8 anfc_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct anfc *nfc = to_anfc(chip->controller);

	if (nfc->curr_cmd == NAND_CMD_STATUS)
		return nfc->status;
	else
		return nfc->buf[nfc->bufshift++];
}

static int anfc_ecc_ooblayout_ondie64_ecc(struct mtd_info *mtd, int section,
					  struct mtd_oob_region *oobregion)
{
	if (section > 4)
		return -ERANGE;

	oobregion->offset = (section * 16) + 8;
	oobregion->length = 8;

	return 0;
}

static int anfc_ecc_ooblayout_ondie64_free(struct mtd_info *mtd, int section,
					   struct mtd_oob_region *oobregion)
{
	if (section > 4)
		return -ERANGE;

	oobregion->offset = (section * 16) + 4;
	oobregion->length = 4;

	return 0;
}

static const struct mtd_ooblayout_ops anfc_ecc_ooblayout_ondie64_ops = {
	.ecc = anfc_ecc_ooblayout_ondie64_ecc,
	.free = anfc_ecc_ooblayout_ondie64_free,
};

/* Generic flash bbt decriptors */
static u8 bbt_pattern[] = { 'B', 'b', 't', '0' };
static u8 mirror_pattern[] = { '1', 't', 'b', 'B' };

static struct nand_bbt_descr bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP |
		NAND_BBT_SCAN2NDPAGE,
	.offs = 4,
	.len = 4,
	.veroffs = 20,
	.maxblocks = 4,
	.pattern = bbt_pattern
};

static struct nand_bbt_descr bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
		| NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP |
		NAND_BBT_SCAN2NDPAGE,
	.offs = 4,
	.len = 4,
	.veroffs = 20,
	.maxblocks = 4,
	.pattern = mirror_pattern
};

static int anfc_nand_on_die_ecc_setup(struct nand_chip *chip, bool enable)
{
	u8 feature[ONFI_SUBFEATURE_PARAM_LEN] = { 0, };

	if (enable)
	feature[0] |= ONFI_FEATURE_ON_DIE_ECC_EN;

	return chip->onfi_set_features(nand_to_mtd(chip), chip,
	      ONDIE_ECC_FEATURE_ADDR, feature);
}

static int anfc_nand_detect_on_die_ecc(struct nand_chip *chip)
{
	u8 feature[ONFI_SUBFEATURE_PARAM_LEN] = { 0, };
	int ret;

	if (chip->onfi_version == 0)
		return 0;

	if (chip->bits_per_cell != 1)
		return 0;

	ret = anfc_nand_on_die_ecc_setup(chip, true);
	if (ret)
		return 0;

	chip->onfi_get_features(nand_to_mtd(chip), chip,
		ONDIE_ECC_FEATURE_ADDR, feature);
	if ((feature[0] & ONFI_FEATURE_ON_DIE_ECC_EN) == 0)
		return 0;

	return 1;
}

static int anfc_ecc_init(struct mtd_info *mtd,
			 struct nand_ecc_ctrl *ecc, int ondie_ecc_state)
{
	u32 ecc_addr;
	unsigned int bchmode, steps;
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct anfc *nfc = to_anfc(chip->controller);
	struct anfc_nand_chip *achip = to_anfc_nand(chip);

	ecc->mode = NAND_ECC_HW;
	ecc->write_oob = anfc_write_oob;

	if (ondie_ecc_state) {
		/* bypass the controller ECC block */
		anfc_config_ecc(nfc, 0);
		ecc->strength = 1;
		ecc->bytes = 0;
		ecc->size = mtd->writesize;
		ecc->read_page = anfc_read_page;
		ecc->write_page = anfc_write_page;
		mtd_set_ooblayout(mtd, &anfc_ecc_ooblayout_ondie64_ops);
		chip->bbt_td = &bbt_main_descr;
		chip->bbt_md = &bbt_mirror_descr;
	} else {
		ecc->read_page = anfc_read_page_hwecc;
		ecc->write_page = anfc_write_page_hwecc;
		mtd_set_ooblayout(mtd, &anfc_ooblayout_ops);

		steps = mtd->writesize / chip->ecc_step_ds;

		switch (chip->ecc_strength_ds) {
		case 12:
			bchmode = 0x1;
			break;
		case 8:
			bchmode = 0x2;
			break;
		case 4:
			bchmode = 0x3;
			break;
		case 24:
			bchmode = 0x4;
			break;
		default:
			bchmode = 0x0;
		}

		if (!bchmode)
			ecc->total = 3 * steps;
		else
			ecc->total =
			     DIV_ROUND_UP(fls(8 * chip->ecc_step_ds) *
				 chip->ecc_strength_ds * steps, 8);

		ecc->strength = chip->ecc_strength_ds;
		ecc->size = chip->ecc_step_ds;
		ecc->bytes = ecc->total / steps;
		ecc->steps = steps;
		achip->bchmode = bchmode;
		achip->bch = achip->bchmode;
		ecc_addr = mtd->writesize + (mtd->oobsize - ecc->total);

		achip->eccval = ecc_addr | (ecc->total << ECC_SIZE_SHIFT) |
				(achip->bch << BCH_EN_SHIFT);
	}

	if (chip->ecc_step_ds >= 1024)
		achip->pktsize = 1024;
	else
		achip->pktsize = 512;

	return 0;
}

static void anfc_cmd_function(struct mtd_info *mtd,
			      unsigned int cmd, int column, int page_addr)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct anfc_nand_chip *achip = to_anfc_nand(chip);
	struct anfc *nfc = to_anfc(chip->controller);
	bool wait = false, read = false;
	u32 addrcycles, prog;

	nfc->bufshift = 0;
	nfc->curr_cmd = cmd;

	if (page_addr == -1)
		page_addr = 0;
	if (column == -1)
		column = 0;

	switch (cmd) {
	case NAND_CMD_RESET:
		anfc_prepare_cmd(nfc, cmd, 0, 0, 0, 0);
		prog = PROG_RST;
		wait = true;
		break;
	case NAND_CMD_SEQIN:
		addrcycles = achip->raddr_cycles + achip->caddr_cycles;
		anfc_prepare_cmd(nfc, cmd, NAND_CMD_PAGEPROG, 1,
				 mtd->writesize, addrcycles);
		anfc_setpagecoladdr(nfc, page_addr, column);
		break;
	case NAND_CMD_READOOB:
		column += mtd->writesize;
	case NAND_CMD_READ0:
	case NAND_CMD_READ1:
		addrcycles = achip->raddr_cycles + achip->caddr_cycles;
		anfc_prepare_cmd(nfc, NAND_CMD_READ0, NAND_CMD_READSTART, 1,
				 mtd->writesize, addrcycles);
		anfc_setpagecoladdr(nfc, page_addr, column);
		break;
	case NAND_CMD_RNDOUT:
		anfc_prepare_cmd(nfc, cmd, NAND_CMD_RNDOUTSTART, 1,
				 mtd->writesize, 2);
		anfc_setpagecoladdr(nfc, page_addr, column);
		break;
	case NAND_CMD_PARAM:
		anfc_prepare_cmd(nfc, cmd, 0, 0, 0, 1);
		anfc_setpagecoladdr(nfc, page_addr, column);
		anfc_rw_buf_pio(mtd, nfc->buf,
				(4 * sizeof(struct nand_onfi_params)),
				1, PROG_RDPARAM);
		break;
	case NAND_CMD_READID:
		anfc_prepare_cmd(nfc, cmd, 0, 0, 0, 1);
		anfc_setpagecoladdr(nfc, page_addr, column);
		anfc_rw_buf_pio(mtd, nfc->buf, ONFI_ID_LEN, 1, PROG_RDID);
		break;
	case NAND_CMD_ERASE1:
		addrcycles = achip->raddr_cycles;
		prog = PROG_ERASE;
		anfc_prepare_cmd(nfc, cmd, NAND_CMD_ERASE2, 0, 0, addrcycles);
		column = page_addr & 0xffff;
		page_addr = (page_addr >> PG_ADDR_SHIFT) & 0xffff;
		anfc_setpagecoladdr(nfc, page_addr, column);
		wait = true;
		break;
	case NAND_CMD_STATUS:
		anfc_prepare_cmd(nfc, cmd, 0, 0, 0, 0);
		anfc_setpktszcnt(nfc, achip->spktsize/4, 1);
		anfc_setpagecoladdr(nfc, page_addr, column);
		prog = PROG_STATUS;
		wait = read = true;
		break;
	case NAND_CMD_GET_FEATURES:
		anfc_prepare_cmd(nfc, cmd, 0, 0, 0, 1);
		anfc_setpagecoladdr(nfc, page_addr, column);
		anfc_rw_buf_pio(mtd, nfc->buf, achip->spktsize, 1,
				PROG_GET_FEATURE);
		break;
	case NAND_CMD_SET_FEATURES:
		anfc_prepare_cmd(nfc, cmd, 0, 0, 0, 1);
		anfc_setpagecoladdr(nfc, page_addr, column);
		break;
	default:
		return;
	}

	if (wait) {
		anfc_enable_intrs(nfc, XFER_COMPLETE);
		writel(prog, nfc->base + PROG_OFST);
		anfc_wait_for_event(nfc);
	}
	if (read)
		nfc->status = readl(nfc->base + FLASH_STS_OFST);
}

static void anfc_select_chip(struct mtd_info *mtd, int num)
{
	u32 val;
	int ret;
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct anfc_nand_chip *achip = to_anfc_nand(chip);
	struct anfc *nfc = to_anfc(chip->controller);

	if (num == -1) {
		pm_runtime_mark_last_busy(nfc->dev);
		pm_runtime_put_autosuspend(nfc->dev);
		return;
	}

	ret = pm_runtime_get_sync(nfc->dev);
	if (ret < 0) {
		dev_err(nfc->dev, "runtime_get_sync failed\n");
		return;
	}
	val = readl(nfc->base + MEM_ADDR2_OFST);
	val &= (val & ~(CS_MASK | BCH_MODE_MASK));
	val |= (achip->csnum << CS_SHIFT) | (achip->bchmode << BCH_MODE_SHIFT);
	writel(val, nfc->base + MEM_ADDR2_OFST);
	nfc->csnum = achip->csnum;
	writel(achip->eccval, nfc->base + ECC_OFST);
	writel(achip->inftimeval, nfc->base + DATA_INTERFACE_OFST);
}

static irqreturn_t anfc_irq_handler(int irq, void *ptr)
{
	struct anfc *nfc = ptr;
	u32 status;

	status = readl(nfc->base + INTR_STS_OFST);
	if (status & EVENT_MASK) {
		complete(&nfc->event);
		writel((status & EVENT_MASK), nfc->base + INTR_STS_OFST);
		writel(0, nfc->base + INTR_STS_EN_OFST);
		writel(0, nfc->base + INTR_SIG_EN_OFST);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int anfc_onfi_set_features(struct mtd_info *mtd, struct nand_chip *chip,
				int addr, uint8_t *subfeature_param)
{
	struct anfc_nand_chip *achip = to_anfc_nand(chip);
	int status;

	if (!chip->onfi_version)
		return -EINVAL;

	if (!(le16_to_cpu(chip->onfi_params.opt_cmd) &
		ONFI_OPT_CMD_SET_GET_FEATURES))
		return -EINVAL;

	chip->cmdfunc(mtd, NAND_CMD_SET_FEATURES, addr, -1);
	anfc_rw_buf_pio(mtd, subfeature_param, achip->spktsize,
			0, PROG_SET_FEATURE);
	status = chip->waitfunc(mtd, chip);
	if (status & NAND_STATUS_FAIL)
		return -EIO;

	return 0;
}

static int anfc_init_timing_mode(struct anfc *nfc,
				 struct anfc_nand_chip *achip)
{
	int mode, err;
	unsigned int feature[2];
	u32 inftimeval;
	struct nand_chip *chip = &achip->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);
	bool change_sdr_clk = false;

	memset(feature, 0, NVDDR_MODE_PACKET_SIZE);
	/* Get nvddr timing modes */
	mode = onfi_get_sync_timing_mode(chip) & 0xff;
	if (!mode) {
		mode = fls(onfi_get_async_timing_mode(chip)) - 1;
		inftimeval = mode;
		if (mode >= 2 && mode <= 5)
			change_sdr_clk = true;
	} else {
		mode = fls(mode) - 1;
		inftimeval = NVDDR_MODE | (mode << NVDDR_TIMING_MODE_SHIFT);
		mode |= ONFI_DATA_INTERFACE_NVDDR;
	}

	feature[0] = mode;
	chip->select_chip(mtd, achip->csnum);
	err = chip->onfi_set_features(mtd, chip, ONFI_FEATURE_ADDR_TIMING_MODE,
				      (uint8_t *)feature);
	chip->select_chip(mtd, -1);
	if (err)
		return err;

	/*
	 * SDR timing modes 2-5 will not work for the arasan nand when
	 * freq > 90 MHz, so reduce the freq in SDR modes 2-5 to < 90Mhz
	 */
	if (change_sdr_clk) {
		clk_disable_unprepare(nfc->clk_sys);
		err = clk_set_rate(nfc->clk_sys, SDR_MODE_DEFLT_FREQ);
		if (err) {
			dev_err(nfc->dev, "Can't set the clock rate\n");
			return err;
		}
		err = clk_prepare_enable(nfc->clk_sys);
		if (err) {
			dev_err(nfc->dev, "Unable to enable sys clock.\n");
			clk_disable_unprepare(nfc->clk_sys);
			return err;
		}
	}
	achip->inftimeval = inftimeval;

	if (mode & ONFI_DATA_INTERFACE_NVDDR)
		achip->spktsize = NVDDR_MODE_PACKET_SIZE;

	return 0;
}

static int anfc_nand_chip_init(struct anfc *nfc,
				struct anfc_nand_chip *anand_chip,
				struct device_node *np)
{
	struct nand_chip *chip = &anand_chip->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;
	int ondie_ecc_state;

	ret = of_property_read_u32(np, "reg", &anand_chip->csnum);
	if (ret) {
		dev_err(nfc->dev, "can't get chip-select\n");
		return -ENXIO;
	}

	mtd->name = devm_kasprintf(nfc->dev, GFP_KERNEL, "arasan_nand.%d",
				   anand_chip->csnum);
	mtd->dev.parent = nfc->dev;

	chip->cmdfunc = anfc_cmd_function;
	chip->chip_delay = 30;
	chip->controller = &nfc->controller;
	chip->read_buf = anfc_read_buf;
	chip->write_buf = anfc_write_buf;
	chip->read_byte = anfc_read_byte;
	chip->options = NAND_BUSWIDTH_AUTO | NAND_NO_SUBPAGE_WRITE;
	chip->bbt_options = NAND_BBT_USE_FLASH;
	chip->select_chip = anfc_select_chip;
	chip->onfi_set_features = anfc_onfi_set_features;
	nand_set_flash_node(chip, np);

	anand_chip->spktsize = SDR_MODE_PACKET_SIZE;
	ret = nand_scan_ident(mtd, 1, NULL);
	if (ret) {
		dev_err(nfc->dev, "nand_scan_ident for NAND failed\n");
		return ret;
	}
	if (chip->onfi_version) {
		anand_chip->raddr_cycles = chip->onfi_params.addr_cycles & 0xf;
		anand_chip->caddr_cycles =
				(chip->onfi_params.addr_cycles >> 4) & 0xf;
	} else {
		/* For non-ONFI devices, configuring the address cyles as 5 */
		anand_chip->raddr_cycles = 3;
		anand_chip->caddr_cycles = 2;
	}

	ondie_ecc_state = anfc_nand_detect_on_die_ecc(chip);
	if (ondie_ecc_state)
		dev_info(nfc->dev, "On-Die ECC selected");
	else
		dev_info(nfc->dev, "HW ECC selected");
	ret = anfc_init_timing_mode(nfc, anand_chip);
	if (ret) {
		dev_err(nfc->dev, "timing mode init failed\n");
		return ret;
	}

	ret = anfc_ecc_init(mtd, &chip->ecc, ondie_ecc_state);
	if (ret)
		return ret;

	ret = nand_scan_tail(mtd);
	if (ret) {
		dev_err(nfc->dev, "nand_scan_tail for NAND failed\n");
		return ret;
	}

	return mtd_device_register(mtd, NULL, 0);
}

static int anfc_probe(struct platform_device *pdev)
{
	struct anfc *nfc;
	struct anfc_nand_chip *anand_chip;
	struct device_node *np = pdev->dev.of_node, *child;
	struct resource *res;
	int err;

	nfc = devm_kzalloc(&pdev->dev, sizeof(*nfc), GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	init_waitqueue_head(&nfc->controller.wq);
	INIT_LIST_HEAD(&nfc->chips);
	init_completion(&nfc->event);
	nfc->dev = &pdev->dev;
	platform_set_drvdata(pdev, nfc);
	nfc->csnum = -1;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nfc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(nfc->base))
		return PTR_ERR(nfc->base);
	nfc->dma = of_property_read_bool(pdev->dev.of_node,
					 "arasan,has-mdma");
	nfc->irq = platform_get_irq(pdev, 0);
	if (nfc->irq < 0) {
		dev_err(&pdev->dev, "platform_get_irq failed\n");
		return -ENXIO;
	}
	err = devm_request_irq(&pdev->dev, nfc->irq, anfc_irq_handler,
			       0, "arasannfc", nfc);
	if (err)
		return err;
	nfc->clk_sys = devm_clk_get(&pdev->dev, "clk_sys");
	if (IS_ERR(nfc->clk_sys)) {
		dev_err(&pdev->dev, "sys clock not found.\n");
		return PTR_ERR(nfc->clk_sys);
	}

	nfc->clk_flash = devm_clk_get(&pdev->dev, "clk_flash");
	if (IS_ERR(nfc->clk_flash)) {
		dev_err(&pdev->dev, "flash clock not found.\n");
		return PTR_ERR(nfc->clk_flash);
	}

	err = clk_prepare_enable(nfc->clk_sys);
	if (err) {
		dev_err(&pdev->dev, "Unable to enable sys clock.\n");
		return err;
	}

	err = clk_prepare_enable(nfc->clk_flash);
	if (err) {
		dev_err(&pdev->dev, "Unable to enable flash clock.\n");
		goto clk_dis_sys;
	}

	pm_runtime_set_autosuspend_delay(nfc->dev, ANFC_PM_TIMEOUT);
	pm_runtime_use_autosuspend(nfc->dev);
	pm_runtime_set_active(nfc->dev);
	pm_runtime_enable(nfc->dev);

	for_each_available_child_of_node(np, child) {
		anand_chip = devm_kzalloc(&pdev->dev, sizeof(*anand_chip),
					  GFP_KERNEL);
		if (!anand_chip) {
			of_node_put(child);
			err = -ENOMEM;
			goto nandchip_clean_up;
		}

		err = anfc_nand_chip_init(nfc, anand_chip, child);
		if (err) {
			devm_kfree(&pdev->dev, anand_chip);
			continue;
		}

		list_add_tail(&anand_chip->node, &nfc->chips);
	}

	pm_runtime_mark_last_busy(nfc->dev);
	pm_runtime_put_autosuspend(nfc->dev);

	return 0;

nandchip_clean_up:
	list_for_each_entry(anand_chip, &nfc->chips, node)
		nand_release(nand_to_mtd(&anand_chip->chip));
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	clk_disable_unprepare(nfc->clk_flash);
clk_dis_sys:
	clk_disable_unprepare(nfc->clk_sys);

	return err;
}

static int anfc_remove(struct platform_device *pdev)
{
	struct anfc *nfc = platform_get_drvdata(pdev);
	struct anfc_nand_chip *anand_chip;

	list_for_each_entry(anand_chip, &nfc->chips, node)
		nand_release(nand_to_mtd(&anand_chip->chip));

	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);

	clk_disable_unprepare(nfc->clk_sys);
	clk_disable_unprepare(nfc->clk_flash);

	return 0;
}

static const struct of_device_id anfc_ids[] = {
	{ .compatible = "arasan,nfc-v3p10" },
	{  }
};
MODULE_DEVICE_TABLE(of, anfc_ids);

static int anfc_suspend(struct device *dev)
{
	return pm_runtime_put_sync(dev);
}

static int anfc_resume(struct device *dev)
{
	return pm_runtime_get_sync(dev);
}

static int __maybe_unused anfc_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct anfc *nfc = platform_get_drvdata(pdev);

	clk_disable(nfc->clk_sys);
	clk_disable(nfc->clk_flash);

	return 0;
}

static int __maybe_unused anfc_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct anfc *nfc = platform_get_drvdata(pdev);
	int ret;

	ret = clk_enable(nfc->clk_sys);
	if (ret) {
		dev_err(dev, "Cannot enable sys clock.\n");
		return ret;
	}
	ret = clk_enable(nfc->clk_flash);
	if (ret) {
		dev_err(dev, "Cannot enable flash clock.\n");
		clk_disable(nfc->clk_sys);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops anfc_pm_ops = {
	.resume = anfc_resume,
	.suspend = anfc_suspend,
	.runtime_resume = anfc_runtime_resume,
	.runtime_suspend = anfc_runtime_suspend,
};

static struct platform_driver anfc_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = anfc_ids,
		.pm = &anfc_pm_ops,
	},
	.probe = anfc_probe,
	.remove = anfc_remove,
};
module_platform_driver(anfc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xilinx, Inc");
MODULE_DESCRIPTION("Arasan NAND Flash Controller Driver");
