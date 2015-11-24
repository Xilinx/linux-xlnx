/*
 * Based on m25p80.c, by Mike Lavender (mike@steroidmicros.com), with
 * influence from lart.c (Abraham Van Der Merwe) and mtd_dataflash.c
 *
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/math64.h>

#include <linux/mtd/cfi.h>
#include <linux/mtd/mtd.h>
#include <linux/of_platform.h>
#include <linux/spi/flash.h>
#include <linux/mtd/spi-nor.h>
#include <linux/spi/spi.h>

/* Define max times to check status register before we give up. */
#define	MAX_READY_WAIT_JIFFIES	(40 * HZ) /* M25P16 specs 40s max chip erase */

#define SPI_NOR_MAX_ID_LEN	6

struct flash_info {
	/*
	 * This array stores the ID bytes.
	 * The first three bytes are the JEDIC ID.
	 * JEDEC ID zero means "no ID" (mostly older chips).
	 */
	u8		id[SPI_NOR_MAX_ID_LEN];
	u8		id_len;

	/* The size listed here is what works with SPINOR_OP_SE, which isn't
	 * necessarily called a "sector" by the vendor.
	 */
	unsigned	sector_size;
	u16		n_sectors;

	u16		page_size;
	u16		addr_width;

	u16		flags;
#define	SECT_4K			0x01	/* SPINOR_OP_BE_4K works uniformly */
#define	SPI_NOR_NO_ERASE	0x02	/* No erase command needed */
#define	SST_WRITE		0x04	/* use SST byte programming */
#define	SPI_NOR_NO_FR		0x08	/* Can't do fastread */
#define	SECT_4K_PMC		0x10	/* SPINOR_OP_BE_4K_PMC works uniformly */
#define	SPI_NOR_DUAL_READ	0x20    /* Flash supports Dual Read */
#define	SPI_NOR_QUAD_READ	0x40    /* Flash supports Quad Read */
#define	USE_FSR			0x80	/* use flag status register */
#define	SPI_NOR_FLASH_LOCK	0x100	/* Flash protection support */
#define	SPI_NOR_QUAD_IO_READ	0x200	/* Flash supports Quad IO read */
/* Unlock the Global protection for sst flashes */
#define	SST_GLOBAL_PROT_UNLK	0x400
};

#define JEDEC_MFR(info)	((info)->id[0])

static const struct spi_device_id *spi_nor_match_id(const char *name);

/*
 * Read the status register, returning its value in the location
 * Return the status register value.
 * Returns negative if error occurred.
 */
static int read_sr(struct spi_nor *nor)
{
	int ret;
	u8 val[2];

	if (nor->isparallel) {
		ret = nor->read_reg(nor, SPINOR_OP_RDSR, &val[0], 2);
		if (ret < 0) {
			pr_err("error %d reading SR\n", (int) ret);
			return ret;
		}
		val[0] |= val[1];
	} else {
		ret = nor->read_reg(nor, SPINOR_OP_RDSR, &val[0], 1);
		if (ret < 0) {
			pr_err("error %d reading SR\n", (int) ret);
			return ret;
		}
	}

	return val[0];
}

/*
 * Read the flag status register, returning its value in the location
 * Return the status register value.
 * Returns negative if error occurred.
 */
static int read_fsr(struct spi_nor *nor)
{
	int ret;
	u8 val[2];

	if (nor->isparallel) {
		ret = nor->read_reg(nor, SPINOR_OP_RDFSR, &val[0], 2);
		if (ret < 0) {
			pr_err("error %d reading FSR\n", ret);
			return ret;
		}
		val[0] &= val[1];
	} else {
		ret = nor->read_reg(nor, SPINOR_OP_RDFSR, &val[0], 1);
		if (ret < 0) {
			pr_err("error %d reading FSR\n", ret);
			return ret;
		}
	}

	return val[0];
}

/*
 * Read configuration register, returning its value in the
 * location. Return the configuration register value.
 * Returns negative if error occured.
 */
static int read_cr(struct spi_nor *nor)
{
	int ret;
	u8 val;

	ret = nor->read_reg(nor, SPINOR_OP_RDCR, &val, 1);
	if (ret < 0) {
		dev_err(nor->dev, "error %d reading CR\n", ret);
		return ret;
	}

	return val;
}

/*
 * Dummy Cycle calculation for different type of read.
 * It can be used to support more commands with
 * different dummy cycle requirements.
 */
static inline int spi_nor_read_dummy_cycles(struct spi_nor *nor)
{
	switch (nor->flash_read) {
	case SPI_NOR_FAST:
	case SPI_NOR_DUAL:
	case SPI_NOR_QUAD:
		return 8;
	case SPI_NOR_QUAD_IO:
		return 40;
	case SPI_NOR_NORMAL:
		return 0;
	}
	return 0;
}

/*
 * Write status register 1 byte
 * Returns negative if error occurred.
 */
static inline int write_sr(struct spi_nor *nor, u8 val)
{
	nor->cmd_buf[0] = val;
	return nor->write_reg(nor, SPINOR_OP_WRSR, nor->cmd_buf, 1, 0);
}

/*
 * Write status Register and configuration register with 2 bytes
 * The first byte will be written to the status register, while the
 * second byte will be written to the configuration register.
 * Return negative if error occured.
 */
static int write_sr_cr(struct spi_nor *nor, u16 val)
{
	nor->cmd_buf[0] = val & 0xff;
	nor->cmd_buf[1] = (val >> 8);

	return nor->write_reg(nor, SPINOR_OP_WRSR, nor->cmd_buf, 2, 0);
}

/*
 * Set write enable latch with Write Enable command.
 * Returns negative if error occurred.
 */
static inline int write_enable(struct spi_nor *nor)
{
	return nor->write_reg(nor, SPINOR_OP_WREN, NULL, 0, 0);
}

/*
 * Send write disble instruction to the chip.
 */
static inline int write_disable(struct spi_nor *nor)
{
	return nor->write_reg(nor, SPINOR_OP_WRDI, NULL, 0, 0);
}

static inline struct spi_nor *mtd_to_spi_nor(struct mtd_info *mtd)
{
	return mtd->priv;
}

/* Enable/disable 4-byte addressing mode. */
static inline int set_4byte(struct spi_nor *nor, struct flash_info *info,
			    int enable)
{
	int status;
	bool need_wren = false;
	u8 cmd;

	switch (JEDEC_MFR(info)) {
	case CFI_MFR_ST: /* Micron, actually */
		/* Some Micron need WREN command; all will accept it */
		need_wren = true;
	case CFI_MFR_MACRONIX:
	case 0xEF /* winbond */:
		if (need_wren)
			write_enable(nor);

		cmd = enable ? SPINOR_OP_EN4B : SPINOR_OP_EX4B;
		status = nor->write_reg(nor, cmd, NULL, 0, 0);
		if (need_wren)
			write_disable(nor);

		return status;
	default:
		/* Spansion style */
		nor->cmd_buf[0] = enable << 7;
		return nor->write_reg(nor, SPINOR_OP_BRWR, nor->cmd_buf, 1, 0);
	}
}

/**
 * read_ear - Get the extended/bank address register value
 * @nor:	Pointer to the flash control structure
 *
 * This routine reads the Extended/bank address register value
 *
 * Return:	Negative if error occured.
 */
static int read_ear(struct spi_nor *nor, struct flash_info *info)
{
	int ret;
	u8 val;
	u8 code;

	/* This is actually Spansion */
	if (JEDEC_MFR(info) == CFI_MFR_AMD)
		code = SPINOR_OP_BRRD;
	/* This is actually Micron */
	else if (JEDEC_MFR(info) == CFI_MFR_ST)
		code = SPINOR_OP_RDEAR;
	else
		return -EINVAL;

	ret = nor->read_reg(nor, code, &val, 1);
	if (ret < 0)
		return ret;

	return val;
}

static inline int spi_nor_sr_ready(struct spi_nor *nor)
{
	int sr = read_sr(nor);
	if (sr < 0)
		return sr;
	else
		return !(sr & SR_WIP);
}

static inline int spi_nor_fsr_ready(struct spi_nor *nor)
{
	int fsr = read_fsr(nor);
	if (fsr < 0)
		return fsr;
	else
		return fsr & FSR_READY;
}

static int spi_nor_ready(struct spi_nor *nor)
{
	int sr, fsr;
	sr = spi_nor_sr_ready(nor);
	if (sr < 0)
		return sr;
	fsr = nor->flags & SNOR_F_USE_FSR ? spi_nor_fsr_ready(nor) : 1;
	if (fsr < 0)
		return fsr;
	return sr && fsr;
}

/*
 * Service routine to read status register until ready, or timeout occurs.
 * Returns non-zero if error.
 */
static int spi_nor_wait_till_ready(struct spi_nor *nor)
{
	unsigned long deadline;
	int timeout = 0, ret;

	deadline = jiffies + MAX_READY_WAIT_JIFFIES;

	while (!timeout) {
		if (time_after_eq(jiffies, deadline))
			timeout = 1;

		ret = spi_nor_ready(nor);
		if (ret < 0)
			return ret;
		if (ret)
			return 0;

		cond_resched();
	}

	dev_err(nor->dev, "flash operation timed out\n");

	return -ETIMEDOUT;
}

/*
 * Update Extended Address/bank selection Register.
 * Call with flash->lock locked.
 */
static int write_ear(struct spi_nor *nor, u32 addr)
{
	u8 code;
	u8 ear;
	int ret;

	/* Wait until finished previous write command. */
	if (spi_nor_wait_till_ready(nor))
		return 1;

	if (nor->mtd->size <= (0x1000000) << nor->shift)
		return 0;

	addr = addr % (u32) nor->mtd->size;
	ear = addr >> 24;

	if ((!nor->isstacked) && (ear == nor->curbank))
		return 0;

	if (nor->isstacked && (nor->mtd->size <= 0x2000000))
		return 0;

	if (nor->jedec_id == CFI_MFR_AMD)
		code = SPINOR_OP_BRWR;
	if (nor->jedec_id == CFI_MFR_ST) {
		write_enable(nor);
		code = SPINOR_OP_WREAR;
	}
	nor->cmd_buf[0] = ear;

	ret = nor->write_reg(nor, code, nor->cmd_buf, 1, 0);
	if (ret < 0)
		return ret;

	nor->curbank = ear;

	return 0;
}

/*
 * Erase the whole flash memory
 *
 * Returns 0 if successful, non-zero otherwise.
 */
static int erase_chip(struct spi_nor *nor)
{
	int ret;

	dev_dbg(nor->dev, " %lldKiB\n", (long long)(nor->mtd->size >> 10));

	/* Wait until finished previous write command. */
	ret = spi_nor_wait_till_ready(nor);
	if (ret)
		return ret;

	if (nor->isstacked)
		nor->spi->master->flags &= ~SPI_MASTER_U_PAGE;

	/* Send write enable, then erase commands. */
	write_enable(nor);

	ret = nor->write_reg(nor, SPINOR_OP_CHIP_ERASE, NULL, 0, 0);
	if (ret)
		return ret;

	if (nor->isstacked) {
		/* Wait until finished previous write command. */
		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			return ret;

		nor->spi->master->flags |= SPI_MASTER_U_PAGE;

		/* Send write enable, then erase commands. */
		write_enable(nor);

		ret = nor->write_reg(nor, SPINOR_OP_CHIP_ERASE, NULL, 0, 0);
	}

	return ret;
}

static int spi_nor_lock_and_prep(struct spi_nor *nor, enum spi_nor_ops ops)
{
	int ret = 0;

	mutex_lock(&nor->lock);

	if (nor->prepare) {
		ret = nor->prepare(nor, ops);
		if (ret) {
			dev_err(nor->dev, "failed in the preparation.\n");
			mutex_unlock(&nor->lock);
			return ret;
		}
	}
	return ret;
}

static void spi_nor_unlock_and_unprep(struct spi_nor *nor, enum spi_nor_ops ops)
{
	if (nor->unprepare)
		nor->unprepare(nor, ops);
	mutex_unlock(&nor->lock);
}

/*
 * Erase an address range on the nor chip.  The address range may extend
 * one or more erase sectors.  Return an error is there is a problem erasing.
 */
static int spi_nor_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	u32 addr, len, offset;
	uint32_t rem;
	int ret;

	dev_dbg(nor->dev, "at 0x%llx, len %lld\n", (long long)instr->addr,
			(long long)instr->len);

	div_u64_rem(instr->len, mtd->erasesize, &rem);
	if (rem)
		return -EINVAL;

	addr = instr->addr;
	len = instr->len;

	ret = spi_nor_lock_and_prep(nor, SPI_NOR_OPS_ERASE);
	if (ret)
		return ret;

	if (nor->isparallel)
		nor->spi->master->flags |= SPI_DATA_STRIPE;
	/* whole-chip erase? */
	if (len == mtd->size) {
		write_enable(nor);

		if (erase_chip(nor)) {
			ret = -EIO;
			goto erase_err;
		}

		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			goto erase_err;

	/* REVISIT in some cases we could speed up erasing large regions
	 * by using SPINOR_OP_SE instead of SPINOR_OP_BE_4K.  We may have set up
	 * to use "small sector erase", but that's not always optimal.
	 */

	/* "sector"-at-a-time erase */
	} else {
		while (len) {
			offset = addr;
			if (nor->isparallel == 1)
				offset /= 2;
			if (nor->isstacked == 1) {
				if (offset >= (mtd->size / 2)) {
					offset = offset - (mtd->size / 2);
					nor->spi->master->flags |=
							SPI_MASTER_U_PAGE;
				} else
					nor->spi->master->flags &=
							~SPI_MASTER_U_PAGE;
			}

			/* Wait until finished previous write command. */
			ret = spi_nor_wait_till_ready(nor);
			if (ret)
				goto erase_err;

			if (nor->addr_width == 3) {
				/* Update Extended Address Register */
				ret = write_ear(nor, offset);
				if (ret)
					goto erase_err;
			}

			ret = spi_nor_wait_till_ready(nor);
			if (ret)
				goto erase_err;

			write_enable(nor);

			if (nor->erase(nor, offset)) {
				ret = -EIO;
				goto erase_err;
			}

			addr += mtd->erasesize;
			len -= mtd->erasesize;

			ret = spi_nor_wait_till_ready(nor);
			if (ret)
				goto erase_err;
		}
	}

	write_disable(nor);

	spi_nor_unlock_and_unprep(nor, SPI_NOR_OPS_ERASE);

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	if (nor->isparallel)
		nor->spi->master->flags &= ~SPI_DATA_STRIPE;
	return ret;

erase_err:
	spi_nor_unlock_and_unprep(nor, SPI_NOR_OPS_ERASE);
	instr->state = MTD_ERASE_FAILED;
	if (nor->isparallel)
		nor->spi->master->flags &= ~SPI_DATA_STRIPE;
	return ret;
}

static inline uint16_t min_lockable_sectors(struct spi_nor *nor,
					    uint16_t n_sectors)
{
	uint16_t lock_granularity;

	/*
	 * Revisit - SST (not used by us) has the same JEDEC ID as micron but
	 * protected area table is similar to that of spansion.
	 */
	if (nor->jedec_id == CFI_MFR_ST)	/* Micron */
		lock_granularity = 1;
	else
		lock_granularity = max(1, n_sectors/M25P_MAX_LOCKABLE_SECTORS);

	return lock_granularity;
}

static inline uint32_t get_protected_area_start(struct spi_nor *nor,
						uint8_t lock_bits)
{
	u16 n_sectors;
	u32 sector_size;
	uint64_t mtd_size;

	n_sectors = nor->n_sectors;
	sector_size = nor->sector_size;
	mtd_size = nor->mtd->size;

	if (nor->isparallel) {
		sector_size = (nor->sector_size >> 1);
		mtd_size = (nor->mtd->size >> 1);
	}
	if (nor->isstacked) {
		n_sectors = (nor->n_sectors >> 1);
		mtd_size = (nor->mtd->size >> 1);
	}

	return mtd_size - (1<<(lock_bits-1)) *
		min_lockable_sectors(nor, n_sectors) * sector_size;
}

static uint8_t min_protected_area_including_offset(struct spi_nor *nor,
						   uint32_t offset)
{
	uint8_t lock_bits, lockbits_limit;

	/*
	 * Revisit - SST (not used by us) has the same JEDEC ID as micron but
	 * protected area table is similar to that of spansion.
	 * Micron has 4 block protect bits.
	 */
	lockbits_limit = 7;
	if (nor->jedec_id == CFI_MFR_ST)	/* Micron */
		lockbits_limit = 15;

	for (lock_bits = 1; lock_bits < lockbits_limit; lock_bits++) {
		if (offset >= get_protected_area_start(nor, lock_bits))
			break;
	}
	return lock_bits;
}

static int write_sr_modify_protection(struct spi_nor *nor, uint8_t status,
				      uint8_t lock_bits)
{
	uint8_t status_new, bp_mask;
	u16 val;

	status_new = status & ~SR_BP_BIT_MASK;
	bp_mask = (lock_bits << SR_BP_BIT_OFFSET) & SR_BP_BIT_MASK;

	/* Micron */
	if (nor->jedec_id == CFI_MFR_ST) {
		/* To support chips with more than 896 sectors (56MB) */
		status_new &= ~SR_BP3;

		/* Protected area starts from top */
		status_new &= ~SR_BP_TB;

		if (lock_bits > 7)
			bp_mask |= SR_BP3;
	}

	status_new |= bp_mask;

	write_enable(nor);

	/* For spansion flashes */
	if (nor->jedec_id == CFI_MFR_AMD) {
		val = read_cr(nor) << 8;
		val |= status_new;
		if (write_sr_cr(nor, val) < 0)
			return 1;
	} else {
		if (write_sr(nor, status_new) < 0)
			return 1;
	}
	return 0;
}

static uint8_t bp_bits_from_sr(struct spi_nor *nor, uint8_t status)
{
	uint8_t ret;

	ret = (((status) & SR_BP_BIT_MASK) >> SR_BP_BIT_OFFSET);
	if (nor->jedec_id == 0x20)
		ret |= ((status & SR_BP3) >> (SR_BP_BIT_OFFSET + 1));

	return ret;
}

static int spi_nor_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	uint32_t offset = ofs;
	uint8_t status;
	uint8_t lock_bits;
	int ret = 0;

	ret = spi_nor_lock_and_prep(nor, SPI_NOR_OPS_LOCK);
	if (ret)
		return ret;

	if (nor->isparallel == 1)
		offset /= 2;

	if (nor->isstacked == 1) {
		if (offset >= (nor->mtd->size / 2)) {
			offset = offset - (nor->mtd->size / 2);
			nor->spi->master->flags |= SPI_MASTER_U_PAGE;
		} else
			nor->spi->master->flags &= ~SPI_MASTER_U_PAGE;
	}

	/* Wait until finished previous command */
	ret = spi_nor_wait_till_ready(nor);
	if (ret)
		goto err;

	status = read_sr(nor);

	lock_bits = min_protected_area_including_offset(nor, offset);

	/* Only modify protection if it will not unlock other areas */
	if (lock_bits > bp_bits_from_sr(nor, status))
		ret = write_sr_modify_protection(nor, status, lock_bits);
	else
		dev_err(nor->dev, "trying to unlock already locked area\n");

err:
	spi_nor_unlock_and_unprep(nor, SPI_NOR_OPS_LOCK);
	return ret;
}

static int spi_nor_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	uint32_t offset = ofs;
	uint8_t status;
	uint8_t lock_bits;
	int ret = 0;

	ret = spi_nor_lock_and_prep(nor, SPI_NOR_OPS_UNLOCK);
	if (ret)
		return ret;

	if (nor->isparallel == 1)
		offset /= 2;

	if (nor->isstacked == 1) {
		if (offset >= (nor->mtd->size / 2)) {
			offset = offset - (nor->mtd->size / 2);
			nor->spi->master->flags |= SPI_MASTER_U_PAGE;
		} else
			nor->spi->master->flags &= ~SPI_MASTER_U_PAGE;
	}

	/* Wait until finished previous command */
	ret = spi_nor_wait_till_ready(nor);
	if (ret)
		goto err;

	status = read_sr(nor);

	lock_bits = min_protected_area_including_offset(nor, offset+len) - 1;

	/* Only modify protection if it will not lock other areas */
	if (lock_bits < bp_bits_from_sr(nor, status))
		ret = write_sr_modify_protection(nor, status, lock_bits);
	else
		dev_err(nor->dev, "trying to lock already unlocked area\n");

err:
	spi_nor_unlock_and_unprep(nor, SPI_NOR_OPS_UNLOCK);
	return ret;
}

static int spi_nor_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	uint32_t offset = ofs;
	uint32_t protected_area_start;
	uint8_t status;
	int ret;

	ret = spi_nor_lock_and_prep(nor, SPI_NOR_OPS_UNLOCK);
	if (ret)
		return ret;
	/* Wait until finished previous command */
	ret = spi_nor_wait_till_ready(nor);
	if (ret)
		goto err;
	status = read_sr(nor);

	protected_area_start = get_protected_area_start(nor,
						bp_bits_from_sr(nor, status));
	if (offset >= protected_area_start)
		ret = MTD_IS_LOCKED;
	else if (offset+len < protected_area_start)
		ret = MTD_IS_UNLOCKED;
	else
		ret = MTD_IS_PARTIALLY_LOCKED;

err:
	spi_nor_unlock_and_unprep(nor, SPI_NOR_OPS_UNLOCK);
	return ret;
}

/* Used when the "_ext_id" is two bytes at most */
#define INFO(_jedec_id, _ext_id, _sector_size, _n_sectors, _flags)	\
	((kernel_ulong_t)&(struct flash_info) {				\
		.id = {							\
			((_jedec_id) >> 16) & 0xff,			\
			((_jedec_id) >> 8) & 0xff,			\
			(_jedec_id) & 0xff,				\
			((_ext_id) >> 8) & 0xff,			\
			(_ext_id) & 0xff,				\
			},						\
		.id_len = (!(_jedec_id) ? 0 : (3 + ((_ext_id) ? 2 : 0))),	\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = 256,					\
		.flags = (_flags),					\
	})

#define INFO6(_jedec_id, _ext_id, _sector_size, _n_sectors, _flags)	\
	((kernel_ulong_t)&(struct flash_info) {				\
		.id = {							\
			((_jedec_id) >> 16) & 0xff,			\
			((_jedec_id) >> 8) & 0xff,			\
			(_jedec_id) & 0xff,				\
			((_ext_id) >> 16) & 0xff,			\
			((_ext_id) >> 8) & 0xff,			\
			(_ext_id) & 0xff,				\
			},						\
		.id_len = 6,						\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = 256,					\
		.flags = (_flags),					\
	})

#define CAT25_INFO(_sector_size, _n_sectors, _page_size, _addr_width, _flags)	\
	((kernel_ulong_t)&(struct flash_info) {				\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = (_page_size),				\
		.addr_width = (_addr_width),				\
		.flags = (_flags),					\
	})

/* NOTE: double check command sets and memory organization when you add
 * more nor chips.  This current list focusses on newer chips, which
 * have been converging on command sets which including JEDEC ID.
 */
static const struct spi_device_id spi_nor_ids[] = {
	/* Atmel -- some are (confusingly) marketed as "DataFlash" */
	{ "at25fs010",  INFO(0x1f6601, 0, 32 * 1024,   4, SECT_4K) },
	{ "at25fs040",  INFO(0x1f6604, 0, 64 * 1024,   8, SECT_4K) },

	{ "at25df041a", INFO(0x1f4401, 0, 64 * 1024,   8, SECT_4K) },
	{ "at25df321a", INFO(0x1f4701, 0, 64 * 1024,  64, SECT_4K) },
	{ "at25df641",  INFO(0x1f4800, 0, 64 * 1024, 128, SECT_4K) },

	{ "at26f004",   INFO(0x1f0400, 0, 64 * 1024,  8, SECT_4K) },
	{ "at26df081a", INFO(0x1f4501, 0, 64 * 1024, 16, SECT_4K) },
	{ "at26df161a", INFO(0x1f4601, 0, 64 * 1024, 32, SECT_4K) },
	{ "at26df321",  INFO(0x1f4700, 0, 64 * 1024, 64, SECT_4K) },

	{ "at45db081d", INFO(0x1f2500, 0, 64 * 1024, 16, SECT_4K) },

	/* EON -- en25xxx */
	{ "en25f32",    INFO(0x1c3116, 0, 64 * 1024,   64, SECT_4K) },
	{ "en25p32",    INFO(0x1c2016, 0, 64 * 1024,   64, 0) },
	{ "en25q32b",   INFO(0x1c3016, 0, 64 * 1024,   64, 0) },
	{ "en25p64",    INFO(0x1c2017, 0, 64 * 1024,  128, 0) },
	{ "en25q64",    INFO(0x1c3017, 0, 64 * 1024,  128, SECT_4K) },
	{ "en25qh128",  INFO(0x1c7018, 0, 64 * 1024,  256, 0) },
	{ "en25qh256",  INFO(0x1c7019, 0, 64 * 1024,  512, 0) },

	/* ESMT */
	{ "f25l32pa", INFO(0x8c2016, 0, 64 * 1024, 64, SECT_4K) },

	/* Everspin */
	{ "mr25h256", CAT25_INFO( 32 * 1024, 1, 256, 2, SPI_NOR_NO_ERASE | SPI_NOR_NO_FR) },
	{ "mr25h10",  CAT25_INFO(128 * 1024, 1, 256, 3, SPI_NOR_NO_ERASE | SPI_NOR_NO_FR) },

	/* Fujitsu */
	{ "mb85rs1mt", INFO(0x047f27, 0, 128 * 1024, 1, SPI_NOR_NO_ERASE) },

	/* GigaDevice */
	{ "gd25q32", INFO(0xc84016, 0, 64 * 1024,  64, SECT_4K) },
	{ "gd25q64", INFO(0xc84017, 0, 64 * 1024, 128, SECT_4K) },
	{ "gd25q128", INFO(0xc84018, 0, 64 * 1024, 256, SECT_4K) },

	/* Intel/Numonyx -- xxxs33b */
	{ "160s33b",  INFO(0x898911, 0, 64 * 1024,  32, 0) },
	{ "320s33b",  INFO(0x898912, 0, 64 * 1024,  64, 0) },
	{ "640s33b",  INFO(0x898913, 0, 64 * 1024, 128, 0) },

	/* Macronix */
	{ "mx25l2005a",  INFO(0xc22012, 0, 64 * 1024,   4, SECT_4K) },
	{ "mx25l4005a",  INFO(0xc22013, 0, 64 * 1024,   8, SECT_4K) },
	{ "mx25l8005",   INFO(0xc22014, 0, 64 * 1024,  16, 0) },
	{ "mx25l1606e",  INFO(0xc22015, 0, 64 * 1024,  32, SECT_4K) },
	{ "mx25l3205d",  INFO(0xc22016, 0, 64 * 1024,  64, 0) },
	{ "mx25l3255e",  INFO(0xc29e16, 0, 64 * 1024,  64, SECT_4K) },
	{ "mx25l6405d",  INFO(0xc22017, 0, 64 * 1024, 128, 0) },
	{ "mx25l12805d", INFO(0xc22018, 0, 64 * 1024, 256, 0) },
	{ "mx25l12855e", INFO(0xc22618, 0, 64 * 1024, 256, 0) },
	{ "mx25l25635e", INFO(0xc22019, 0, 64 * 1024, 512, 0) },
	{ "mx25l25655e", INFO(0xc22619, 0, 64 * 1024, 512, 0) },
	{ "mx66l51235l", INFO(0xc2201a, 0, 64 * 1024, 1024, SPI_NOR_QUAD_READ) },
	{ "mx66l1g55g",  INFO(0xc2261b, 0, 64 * 1024, 2048, SPI_NOR_QUAD_READ) },

	/* Micron */
	{ "n25q032",	 INFO(0x20ba16, 0, 64 * 1024,   64, SPI_NOR_QUAD_READ) },
	{ "n25q064",     INFO(0x20ba17, 0, 64 * 1024,  128, SPI_NOR_QUAD_READ) },
	{ "n25q128a11",  INFO(0x20bb18, 0, 64 * 1024,  256, SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | SPI_NOR_FLASH_LOCK) },
	{ "n25q128a13",  INFO(0x20ba18, 0, 64 * 1024,  256, SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | SPI_NOR_FLASH_LOCK) },
	{ "n25q256a",    INFO(0x20bb19, 0, 64 * 1024,  512, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | USE_FSR | SPI_NOR_FLASH_LOCK) },
	{ "n25q256a13",  INFO(0x20ba19, 0, 64 * 1024,  512, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | USE_FSR | SPI_NOR_FLASH_LOCK) },
	{ "n25q512a",    INFO(0x20bb20, 0, 64 * 1024, 1024, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | USE_FSR | SPI_NOR_FLASH_LOCK) },
	{ "n25q512a13",  INFO(0x20ba20, 0, 64 * 1024, 1024, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | USE_FSR | SPI_NOR_FLASH_LOCK) },
	{ "n25q512ax3",  INFO(0x20ba20, 0, 64 * 1024, 1024, USE_FSR | SPI_NOR_FLASH_LOCK) },
	{ "n25q00",      INFO(0x20ba21, 0, 64 * 1024, 2048, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | USE_FSR | SPI_NOR_FLASH_LOCK) },

	/* PMC */
	{ "pm25lv512",   INFO(0,        0, 32 * 1024,    2, SECT_4K_PMC) },
	{ "pm25lv010",   INFO(0,        0, 32 * 1024,    4, SECT_4K_PMC) },
	{ "pm25lq032",   INFO(0x7f9d46, 0, 64 * 1024,   64, SECT_4K) },

	/* Spansion -- single (large) sector size only, at least
	 * for the chips listed here (without boot sectors).
	 */
	{ "s25sl032p",  INFO(0x010215, 0x4d00,  64 * 1024,  64, SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s25sl064p",  INFO(0x010216, 0x4d00,  64 * 1024, 128, 0) },
	{ "s25fl256s0", INFO(0x010219, 0x4d00, 256 * 1024, 128, SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | SPI_NOR_FLASH_LOCK) },
	{ "s25fl256s1", INFO(0x010219, 0x4d01,  64 * 1024, 512, SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s25fl512s",  INFO(0x010220, 0x4d00, 256 * 1024, 256, SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s70fl01gs",  INFO(0x010221, 0x4d00, 256 * 1024, 256, SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "s25sl12800", INFO(0x012018, 0x0300, 256 * 1024,  64, SPI_NOR_FLASH_LOCK) },
	{ "s25sl12801", INFO(0x012018, 0x0301,  64 * 1024, 256, SPI_NOR_FLASH_LOCK) },
	{ "s25fl128s",	INFO6(0x012018, 0x4d0180, 64 * 1024, 256, SPI_NOR_QUAD_READ) },
	{ "s25fl129p0", INFO(0x012018, 0x4d00, 256 * 1024,  64, SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | SPI_NOR_FLASH_LOCK) },
	{ "s25fl129p1", INFO(0x012018, 0x4d01,  64 * 1024, 256, SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | SPI_NOR_FLASH_LOCK) },
	{ "s25sl004a",  INFO(0x010212,      0,  64 * 1024,   8, 0) },
	{ "s25sl008a",  INFO(0x010213,      0,  64 * 1024,  16, 0) },
	{ "s25sl016a",  INFO(0x010214,      0,  64 * 1024,  32, 0) },
	{ "s25sl032a",  INFO(0x010215,      0,  64 * 1024,  64, 0) },
	{ "s25sl064a",  INFO(0x010216,      0,  64 * 1024, 128, 0) },
	{ "s25fl008k",  INFO(0xef4014,      0,  64 * 1024,  16, SECT_4K) },
	{ "s25fl016k",  INFO(0xef4015,      0,  64 * 1024,  32, SECT_4K) },
	{ "s25fl064k",  INFO(0xef4017,      0,  64 * 1024, 128, SECT_4K) },
	{ "s25fl132k",  INFO(0x014016,      0,  64 * 1024,  64, 0) },

	/* SST -- large erase sizes are "overlays", "sectors" are 4K */
	{ "sst25vf040b", INFO(0xbf258d, 0, 64 * 1024,  8, SECT_4K | SST_WRITE) },
	{ "sst25vf080b", INFO(0xbf258e, 0, 64 * 1024, 16, SECT_4K | SST_WRITE) },
	{ "sst25vf016b", INFO(0xbf2541, 0, 64 * 1024, 32, SECT_4K | SST_WRITE) },
	{ "sst25vf032b", INFO(0xbf254a, 0, 64 * 1024, 64, SECT_4K | SST_WRITE) },
	{ "sst25vf064c", INFO(0xbf254b, 0, 64 * 1024, 128, SECT_4K) },
	{ "sst25wf512",  INFO(0xbf2501, 0, 64 * 1024,  1, SECT_4K | SST_WRITE) },
	{ "sst25wf010",  INFO(0xbf2502, 0, 64 * 1024,  2, SECT_4K | SST_WRITE) },
	{ "sst25wf020",  INFO(0xbf2503, 0, 64 * 1024,  4, SECT_4K | SST_WRITE) },
	{ "sst25wf040",  INFO(0xbf2504, 0, 64 * 1024,  8, SECT_4K | SST_WRITE) },
	{ "sst25wf080",  INFO(0xbf2505, 0, 64 * 1024, 16, SECT_4K | SST_WRITE) },
	{ "sst26wf016B", INFO(0xbf2651, 0, 64 * 1024, 32, SECT_4K |
							SST_GLOBAL_PROT_UNLK) },

	/* ST Microelectronics -- newer production may have feature updates */
	{ "m25p05",  INFO(0x202010,  0,  32 * 1024,   2, 0) },
	{ "m25p10",  INFO(0x202011,  0,  32 * 1024,   4, 0) },
	{ "m25p20",  INFO(0x202012,  0,  64 * 1024,   4, 0) },
	{ "m25p40",  INFO(0x202013,  0,  64 * 1024,   8, 0) },
	{ "m25p80",  INFO(0x202014,  0,  64 * 1024,  16, 0) },
	{ "m25p16",  INFO(0x202015,  0,  64 * 1024,  32, 0) },
	{ "m25p32",  INFO(0x202016,  0,  64 * 1024,  64, 0) },
	{ "m25p64",  INFO(0x202017,  0,  64 * 1024, 128, 0) },
	{ "m25p128", INFO(0x202018,  0, 256 * 1024,  64, 0) },

	{ "m25p05-nonjedec",  INFO(0, 0,  32 * 1024,   2, 0) },
	{ "m25p10-nonjedec",  INFO(0, 0,  32 * 1024,   4, 0) },
	{ "m25p20-nonjedec",  INFO(0, 0,  64 * 1024,   4, 0) },
	{ "m25p40-nonjedec",  INFO(0, 0,  64 * 1024,   8, 0) },
	{ "m25p80-nonjedec",  INFO(0, 0,  64 * 1024,  16, 0) },
	{ "m25p16-nonjedec",  INFO(0, 0,  64 * 1024,  32, 0) },
	{ "m25p32-nonjedec",  INFO(0, 0,  64 * 1024,  64, 0) },
	{ "m25p64-nonjedec",  INFO(0, 0,  64 * 1024, 128, 0) },
	{ "m25p128-nonjedec", INFO(0, 0, 256 * 1024,  64, 0) },

	{ "m45pe10", INFO(0x204011,  0, 64 * 1024,    2, 0) },
	{ "m45pe80", INFO(0x204014,  0, 64 * 1024,   16, 0) },
	{ "m45pe16", INFO(0x204015,  0, 64 * 1024,   32, 0) },

	{ "m25pe20", INFO(0x208012,  0, 64 * 1024,  4,       0) },
	{ "m25pe80", INFO(0x208014,  0, 64 * 1024, 16,       0) },
	{ "m25pe16", INFO(0x208015,  0, 64 * 1024, 32, SECT_4K) },

	{ "m25px16",    INFO(0x207115,  0, 64 * 1024, 32, SECT_4K) },
	{ "m25px32",    INFO(0x207116,  0, 64 * 1024, 64, SECT_4K) },
	{ "m25px32-s0", INFO(0x207316,  0, 64 * 1024, 64, SECT_4K) },
	{ "m25px32-s1", INFO(0x206316,  0, 64 * 1024, 64, SECT_4K) },
	{ "m25px64",    INFO(0x207117,  0, 64 * 1024, 128, 0) },
	{ "m25px80",    INFO(0x207114,  0, 64 * 1024, 16, 0) },

	/* Winbond -- w25x "blocks" are 64K, "sectors" are 4KiB */
	{ "w25x10", INFO(0xef3011, 0, 64 * 1024,  2,  SECT_4K) },
	{ "w25x20", INFO(0xef3012, 0, 64 * 1024,  4,  SECT_4K) },
	{ "w25x40", INFO(0xef3013, 0, 64 * 1024,  8,  SECT_4K) },
	{ "w25x80", INFO(0xef3014, 0, 64 * 1024,  16, SECT_4K) },
	{ "w25x16", INFO(0xef3015, 0, 64 * 1024,  32, SECT_4K) },
	{ "w25x32", INFO(0xef3016, 0, 64 * 1024,  64, SECT_4K) },
	{ "w25q32", INFO(0xef4016, 0, 64 * 1024,  64, SECT_4K) },
	{ "w25q32dw", INFO(0xef6016, 0, 64 * 1024,  64, SECT_4K) },
	{ "w25x64", INFO(0xef3017, 0, 64 * 1024, 128, SECT_4K) },
	{ "w25q64", INFO(0xef4017, 0, 64 * 1024, 128, SECT_4K) },
	{ "w25q80", INFO(0xef5014, 0, 64 * 1024,  16, SECT_4K) },
	{ "w25q80bl", INFO(0xef4014, 0, 64 * 1024,  16, SECT_4K) },
	{ "w25q128", INFO(0xef4018, 0, 64 * 1024, 256, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "w25q256", INFO(0xef4019, 0, 64 * 1024, 512, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },

	/* Catalyst / On Semiconductor -- non-JEDEC */
	{ "cat25c11", CAT25_INFO(  16, 8, 16, 1, SPI_NOR_NO_ERASE | SPI_NOR_NO_FR) },
	{ "cat25c03", CAT25_INFO(  32, 8, 16, 2, SPI_NOR_NO_ERASE | SPI_NOR_NO_FR) },
	{ "cat25c09", CAT25_INFO( 128, 8, 32, 2, SPI_NOR_NO_ERASE | SPI_NOR_NO_FR) },
	{ "cat25c17", CAT25_INFO( 256, 8, 32, 2, SPI_NOR_NO_ERASE | SPI_NOR_NO_FR) },
	{ "cat25128", CAT25_INFO(2048, 8, 64, 2, SPI_NOR_NO_ERASE | SPI_NOR_NO_FR) },
		/* ISSI flash */
	{ "is25lp032", INFO(0x9d6016, 0, 64 * 1024, 64,
				SECT_4K | SPI_NOR_QUAD_IO_READ) },
	{ "is25lp064", INFO(0x9d6017, 0, 64 * 1024, 128,
				SECT_4K | SPI_NOR_QUAD_IO_READ) },
	{ "is25lp128", INFO(0x9D6018, 0, 64 * 1024, 256,
				SECT_4K | SPI_NOR_QUAD_IO_READ) },
	{ },
};

static const struct spi_device_id *spi_nor_read_id(struct spi_nor *nor)
{
	int			tmp;
	u8			id[SPI_NOR_MAX_ID_LEN];
	struct flash_info	*info;
	nor->spi->master->flags &= ~SPI_BOTH_FLASH;

	/* If more than one flash are present,need to read id of second flash */
	tmp = nor->read_reg(nor, SPINOR_OP_RDID, id, SPI_NOR_MAX_ID_LEN);
	if (tmp < 0) {
		dev_dbg(nor->dev, " error %d reading JEDEC ID\n", tmp);
		return ERR_PTR(tmp);
	}

	for (tmp = 0; tmp < ARRAY_SIZE(spi_nor_ids) - 1; tmp++) {
		info = (void *)spi_nor_ids[tmp].driver_data;
		if (info->id_len) {
			if (!memcmp(info->id, id, info->id_len))
				return &spi_nor_ids[tmp];
		}
	}
	dev_err(nor->dev, "unrecognized JEDEC id bytes: %02x, %2x, %2x\n",
		id[0], id[1], id[2]);
	return ERR_PTR(-ENODEV);
}

static int spi_nor_read(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	ret = nor->read(nor, from, len, retlen, buf);

	return ret;
}

static int spi_nor_read_ext(struct mtd_info *mtd, loff_t from, size_t len,
			    size_t *retlen, u_char *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	u32 addr = from;
	u32 offset = from;
	u32 read_len = 0;
	u32 actual_len = 0;
	u32 read_count = 0;
	u32 rem_bank_len = 0;
	u8 bank = 0;
	u8 stack_shift = 0;
	int ret;

#define OFFSET_16_MB 0x1000000

	dev_dbg(nor->dev, "from 0x%08x, len %zd\n", (u32)from, len);

	ret = spi_nor_lock_and_prep(nor, SPI_NOR_OPS_READ);
	if (ret)
		return ret;
	if (nor->isparallel)
		nor->spi->master->flags |= SPI_DATA_STRIPE;

	while (len) {
		if (nor->addr_width == 3) {
			bank = addr / (OFFSET_16_MB << nor->shift);
			rem_bank_len = ((OFFSET_16_MB << nor->shift) *
							(bank + 1)) - addr;
		}
		offset = addr;
		if (nor->isparallel == 1)
			offset /= 2;
		if (nor->isstacked == 1) {
			stack_shift = 1;
			if (offset >= (nor->mtd->size / 2)) {
				offset = offset - (nor->mtd->size / 2);
				nor->spi->master->flags |= SPI_MASTER_U_PAGE;
			} else {
				nor->spi->master->flags &= ~SPI_MASTER_U_PAGE;
			}
		}
		/* Die cross over issue is not handled */
		if (nor->addr_width == 4) {
			rem_bank_len = (nor->mtd->size >> stack_shift) -
					(offset << nor->shift);
		}
		if (nor->addr_width == 3)
			write_ear(nor, offset);
		if (len < rem_bank_len)
			read_len = len;
		else
			read_len = rem_bank_len;

		/* Wait till previous write/erase is done. */
		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			goto read_err;

		ret = spi_nor_read(mtd, offset, read_len, &actual_len, buf);
		if (ret)
			return ret;

		addr += actual_len;
		len -= actual_len;
		buf += actual_len;
		read_count += actual_len;
	}

	*retlen = read_count;

read_err:
	if (nor->isparallel)
		nor->spi->master->flags &= ~SPI_DATA_STRIPE;
	spi_nor_unlock_and_unprep(nor, SPI_NOR_OPS_READ);
	return ret;
}

static int sst_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u_char *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	size_t actual;
	int ret;

	dev_dbg(nor->dev, "to 0x%08x, len %zd\n", (u32)to, len);

	ret = spi_nor_lock_and_prep(nor, SPI_NOR_OPS_WRITE);
	if (ret)
		return ret;

	write_enable(nor);

	nor->sst_write_second = false;

	actual = to % 2;
	/* Start write from odd address. */
	if (actual) {
		nor->program_opcode = SPINOR_OP_BP;

		/* write one byte. */
		nor->write(nor, to, 1, retlen, buf);
		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			goto time_out;
	}
	to += actual;

	/* Write out most of the data here. */
	for (; actual < len - 1; actual += 2) {
		nor->program_opcode = SPINOR_OP_AAI_WP;

		/* write two bytes. */
		nor->write(nor, to, 2, retlen, buf + actual);
		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			goto time_out;
		to += 2;
		nor->sst_write_second = true;
	}
	nor->sst_write_second = false;

	write_disable(nor);
	ret = spi_nor_wait_till_ready(nor);
	if (ret)
		goto time_out;

	/* Write out trailing byte if it exists. */
	if (actual != len) {
		write_enable(nor);

		nor->program_opcode = SPINOR_OP_BP;
		nor->write(nor, to, 1, retlen, buf + actual);

		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			goto time_out;
		write_disable(nor);
	}
time_out:
	spi_nor_unlock_and_unprep(nor, SPI_NOR_OPS_WRITE);
	return ret;
}

/*
 * Write an address range to the nor chip.  Data must be written in
 * FLASH_PAGESIZE chunks.  The address range may be any size provided
 * it is within the physical boundaries.
 */
static int spi_nor_write(struct mtd_info *mtd, loff_t to, size_t len,
			 size_t *retlen, const u_char *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	u32 page_offset, page_size, i;
	int ret;

	dev_dbg(nor->dev, "to 0x%08x, len %zd\n", (u32)to, len);
	/* Wait until finished previous write command. */
	ret = spi_nor_wait_till_ready(nor);
	if (ret)
		return ret;

	write_enable(nor);

	page_offset = to & (nor->page_size - 1);

	/* do all the bytes fit onto one page? */
	if (page_offset + len <= nor->page_size) {
		nor->write(nor, to >> nor->shift, len, retlen, buf);
	} else {
		/* the size of data remaining on the first page */
		page_size = nor->page_size - page_offset;
		nor->write(nor, to >> nor->shift, page_size, retlen, buf);

		/* write everything in nor->page_size chunks */
		for (i = page_size; i < len; i += page_size) {
			page_size = len - i;
			if (page_size > nor->page_size)
				page_size = nor->page_size;

			ret = spi_nor_wait_till_ready(nor);
			if (ret)
				return ret;
			write_enable(nor);

			nor->write(nor, (to + i) >> nor->shift, page_size,
				   retlen, buf + i);
		}
	}

	return 0;
}

static int spi_nor_write_ext(struct mtd_info *mtd, loff_t to, size_t len,
			     size_t *retlen, const u_char *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	u32 addr = to;
	u32 offset = to;
	u32 write_len = 0;
	u32 actual_len = 0;
	u32 write_count = 0;
	u32 rem_bank_len = 0;
	u8 bank = 0;
	u8 stack_shift = 0;
	int ret;

#define OFFSET_16_MB 0x1000000

	dev_dbg(nor->dev, "to 0x%08x, len %zd\n", (u32)to, len);

	ret = spi_nor_lock_and_prep(nor, SPI_NOR_OPS_WRITE);
	if (ret)
		return ret;
	if (nor->isparallel)
		nor->spi->master->flags |= SPI_DATA_STRIPE;

	while (len) {
		actual_len = 0;
		if (nor->addr_width == 3) {
			bank = addr / (OFFSET_16_MB << nor->shift);
			rem_bank_len = ((OFFSET_16_MB << nor->shift) *
							(bank + 1)) - addr;
		}
		offset = addr;

		if (nor->isstacked == 1) {
			stack_shift = 1;
			if (offset >= (nor->mtd->size / 2)) {
				offset = offset - (nor->mtd->size / 2);
				nor->spi->master->flags |= SPI_MASTER_U_PAGE;
			} else {
				nor->spi->master->flags &= ~SPI_MASTER_U_PAGE;
			}
		}
		/* Die cross over issue is not handled */
		if (nor->addr_width == 4)
			rem_bank_len = (nor->mtd->size >> stack_shift) - offset;
		if (nor->addr_width == 3)
			write_ear(nor, (offset >> nor->shift));
		if (len < rem_bank_len)
			write_len = len;
		else
			write_len = rem_bank_len;

		ret = spi_nor_write(mtd, offset, write_len, &actual_len, buf);
		if (ret)
			goto write_err;

		addr += actual_len;
		len -= actual_len;
		buf += actual_len;
		write_count += actual_len;
	}

	*retlen = write_count;

write_err:
	if (nor->isparallel)
		nor->spi->master->flags &= ~SPI_DATA_STRIPE;
	spi_nor_unlock_and_unprep(nor, SPI_NOR_OPS_WRITE);
	return ret;
}

static int macronix_quad_enable(struct spi_nor *nor)
{
	int ret, val;

	val = read_sr(nor);
	write_enable(nor);

	nor->cmd_buf[0] = val | SR_QUAD_EN_MX;
	nor->write_reg(nor, SPINOR_OP_WRSR, nor->cmd_buf, 1, 0);

	if (spi_nor_wait_till_ready(nor))
		return 1;

	ret = read_sr(nor);
	if (!(ret > 0 && (ret & SR_QUAD_EN_MX))) {
		dev_err(nor->dev, "Macronix Quad bit not set\n");
		return -EINVAL;
	}

	return 0;
}

static int __maybe_unused spansion_quad_enable(struct spi_nor *nor)
{
	int ret;
	int quad_en = CR_QUAD_EN_SPAN << 8;

	if (nor->isparallel)
		nor->spi->master->flags |= SPI_DATA_STRIPE;

	quad_en |= read_sr(nor);
	quad_en |= (read_cr(nor) << 8);

	if (nor->isparallel)
		nor->spi->master->flags &= ~SPI_DATA_STRIPE;

	write_enable(nor);

	ret = write_sr_cr(nor, quad_en);
	if (ret < 0) {
		dev_err(nor->dev,
			"error while writing configuration register\n");
		return -EINVAL;
	}

	if (nor->isparallel)
		nor->spi->master->flags |= SPI_DATA_STRIPE;
	/* read back and check it */
	ret = read_cr(nor);
	if (!(ret > 0 && (ret & CR_QUAD_EN_SPAN))) {
		dev_err(nor->dev, "Spansion Quad bit not set\n");
		if (nor->isparallel)
			nor->spi->master->flags &= ~SPI_DATA_STRIPE;
		return -EINVAL;
	}

	if (nor->isparallel)
		nor->spi->master->flags &= ~SPI_DATA_STRIPE;

	return 0;
}

static int micron_quad_enable(struct spi_nor *nor)
{
	int ret;
	u8 val;

	ret = nor->read_reg(nor, SPINOR_OP_RD_EVCR, &val, 1);
	if (ret < 0) {
		dev_err(nor->dev, "error %d reading EVCR\n", ret);
		return ret;
	}

	write_enable(nor);

	/* set EVCR, enable quad I/O */
	nor->cmd_buf[0] = val & ~EVCR_QUAD_EN_MICRON;
	ret = nor->write_reg(nor, SPINOR_OP_WD_EVCR, nor->cmd_buf, 1, 0);
	if (ret < 0) {
		dev_err(nor->dev, "error while writing EVCR register\n");
		return ret;
	}

	ret = spi_nor_wait_till_ready(nor);
	if (ret)
		return ret;

	/* read EVCR and check it */
	ret = nor->read_reg(nor, SPINOR_OP_RD_EVCR, &val, 1);
	if (ret < 0) {
		dev_err(nor->dev, "error %d reading EVCR\n", ret);
		return ret;
	}
	if (val & EVCR_QUAD_EN_MICRON) {
		dev_err(nor->dev, "Micron EVCR Quad bit not clear\n");
		return -EINVAL;
	}

	return 0;
}

static int set_quad_mode(struct spi_nor *nor, struct flash_info *info)
{
	int status;

	switch (JEDEC_MFR(info)) {
	case CFI_MFR_ISSI:
	case CFI_MFR_MACRONIX:
		status = macronix_quad_enable(nor);
		if (status) {
			dev_err(nor->dev, "Macronix quad-read not enabled\n");
			return -EINVAL;
		}
		return status;
	case CFI_MFR_ST:
		if (!(nor->spi->mode & SPI_TX_QUAD)) {
			dev_info(nor->dev, "Controller not in SPI_TX_QUAD mode, just use extended SPI mode\n");
			return 0;
		}
		status = micron_quad_enable(nor);
		if (status) {
			dev_err(nor->dev, "Micron quad-read not enabled\n");
			return -EINVAL;
		}
		return status;
	case CFI_MFR_AMD:
		status = spansion_quad_enable(nor);
		if (status) {
			dev_err(nor->dev, "Spansion quad-read not enabled\n");
			return -EINVAL;
		}
		return status;
	default:
		return 0;
	}
}

static int spi_nor_check(struct spi_nor *nor)
{
	if (!nor->dev || !nor->read || !nor->write ||
		!nor->read_reg || !nor->write_reg || !nor->erase) {
		pr_err("spi-nor: please fill all the necessary fields!\n");
		return -EINVAL;
	}

	return 0;
}

int spi_nor_scan(struct spi_nor *nor, const char *name, enum read_mode mode)
{
	const struct spi_device_id	*id = NULL;
	struct flash_info		*info;
	struct device *dev = nor->dev;
	struct mtd_info *mtd = nor->mtd;
	struct device_node *np = dev->of_node;
	struct device_node *np_spi;
	uint64_t actual_size;
	int ret;
	int i;

	ret = spi_nor_check(nor);
	if (ret)
		return ret;

	/* Try to auto-detect if chip name wasn't specified */
	if (!name)
		id = spi_nor_read_id(nor);
	else
		id = spi_nor_match_id(name);
	if (IS_ERR_OR_NULL(id))
		return -ENOENT;

	info = (void *)id->driver_data;

	/*
	 * If caller has specified name of flash model that can normally be
	 * detected using JEDEC, let's verify it.
	 */
	if (name && info->id_len) {
		const struct spi_device_id *jid;

		jid = spi_nor_read_id(nor);
		if (IS_ERR(jid)) {
			return PTR_ERR(jid);
		} else if (jid != id) {
			/*
			 * JEDEC knows better, so overwrite platform ID. We
			 * can't trust partitions any longer, but we'll let
			 * mtd apply them anyway, since some partitions may be
			 * marked read-only, and we don't want to lose that
			 * information, even if it's not 100% accurate.
			 */
			dev_warn(dev, "found %s, expected %s\n",
				 jid->name, id->name);
			id = jid;
			info = (void *)jid->driver_data;
		}
	}

	mutex_init(&nor->lock);

	/*
	 * Atmel, SST and Intel/Numonyx serial nor tend to power
	 * up with the software protection bits set
	 */

	if (JEDEC_MFR(info) == CFI_MFR_ATMEL ||
	    JEDEC_MFR(info) == CFI_MFR_INTEL ||
	    JEDEC_MFR(info) == CFI_MFR_SST) {
		write_enable(nor);
		write_sr(nor, 0);

		if (info->flags & SST_GLOBAL_PROT_UNLK) {
			write_enable(nor);
			/* Unlock global write protection bits */
			nor->write_reg(nor, GLOBAL_BLKPROT_UNLK, NULL, 0, 0);
		}
	}

	if (!mtd->name)
		mtd->name = dev_name(dev);
	mtd->type = MTD_NORFLASH;
	mtd->writesize = 1;
	mtd->flags = MTD_CAP_NORFLASH;
	mtd->size = info->sector_size * info->n_sectors;
	mtd->_erase = spi_nor_erase;
	mtd->_read = spi_nor_read_ext;
	actual_size = mtd->size;

	{
#ifdef CONFIG_OF
		u32 is_dual;

		np_spi = of_get_next_parent(np);
		if ((of_property_match_string(np_spi, "compatible",
		    "xlnx,zynq-qspi-1.0") >= 0) ||
			(of_property_match_string(np_spi, "compatible",
					"xlnx,zynqmp-qspi-1.0") >= 0)) {
			if (of_property_read_u32(np_spi, "is-dual",
						 &is_dual) < 0) {
				/* Default to single if prop not defined */
				nor->shift = 0;
				nor->isstacked = 0;
				nor->isparallel = 0;
			} else {
				if (is_dual == 1) {
					/* dual parallel */
					nor->shift = 1;
					info->sector_size <<= nor->shift;
					info->page_size <<= nor->shift;
					mtd->size <<= nor->shift;
					nor->isparallel = 1;
					nor->isstacked = 0;
					nor->spi->master->flags |=
							SPI_BOTH_FLASH;
				} else {
#ifdef CONFIG_SPI_ZYNQ_QSPI_DUAL_STACKED
					/* dual stacked */
					nor->shift = 0;
					mtd->size <<= 1;
					info->n_sectors <<= 1;
					nor->isstacked = 1;
					nor->isparallel = 0;
#else
					u32 is_stacked;
					if (of_property_read_u32(np_spi,
							"is-stacked",
							&is_stacked) < 0) {
						is_stacked = 0;
					}
					if (is_stacked) {
						/* dual stacked */
						nor->shift = 0;
						mtd->size <<= 1;
						info->n_sectors <<= 1;
						nor->isstacked = 1;
						nor->isparallel = 0;
					} else {
						/* single */
						nor->shift = 0;
						nor->isstacked = 0;
						nor->isparallel = 0;
					}
#endif
				}
			}
		}
#else
		/* Default to single */
		nor->shift = 0;
		nor->isstacked = 0;
		nor->isparallel = 0;
#endif
	}

	nor->n_sectors = info->n_sectors;
	nor->sector_size = info->sector_size;
	/* nor protection support for STmicro chips */
	if (info->flags & SPI_NOR_FLASH_LOCK) {
		mtd->_lock = spi_nor_lock;
		mtd->_unlock = spi_nor_unlock;
		mtd->_is_locked = spi_nor_is_locked;
	}

	/* sst nor chips use AAI word program */
	if (info->flags & SST_WRITE)
		mtd->_write = sst_write;
	else
		mtd->_write = spi_nor_write_ext;

	if (info->flags & USE_FSR)
		nor->flags |= SNOR_F_USE_FSR;

#ifdef CONFIG_MTD_SPI_NOR_USE_4K_SECTORS
	/* prefer "small sector" erase if possible */
	if (info->flags & SECT_4K) {
		nor->erase_opcode = SPINOR_OP_BE_4K;
		mtd->erasesize = 4096 << nor->shift;
	} else if (info->flags & SECT_4K_PMC) {
		nor->erase_opcode = SPINOR_OP_BE_4K_PMC;
		mtd->erasesize = 4096;
	} else
#endif
	{
		nor->erase_opcode = SPINOR_OP_SE;
		mtd->erasesize = info->sector_size;
	}

	if (info->flags & SPI_NOR_NO_ERASE)
		mtd->flags |= MTD_NO_ERASE;

	nor->jedec_id = info->id[0];
	mtd->dev.parent = dev;
	nor->page_size = info->page_size;
	mtd->writebufsize = nor->page_size;

	if (np) {
		/* If we were instantiated by DT, use it */
		if (of_property_read_bool(np, "m25p,fast-read"))
			nor->flash_read = SPI_NOR_FAST;
		else
			nor->flash_read = SPI_NOR_NORMAL;
	} else {
		/* If we weren't instantiated by DT, default to fast-read */
		nor->flash_read = SPI_NOR_FAST;
	}

	/* Some devices cannot do fast-read, no matter what DT tells us */
	if (info->flags & SPI_NOR_NO_FR)
		nor->flash_read = SPI_NOR_NORMAL;

	/* Quad/Dual-read mode takes precedence over fast/normal */
	if (mode == SPI_NOR_QUAD && info->flags & SPI_NOR_QUAD_READ) {
		ret = set_quad_mode(nor, info);
		if (ret) {
			dev_err(dev, "quad mode not supported\n");
			return ret;
		}
		nor->flash_read = SPI_NOR_QUAD;
	} else if (mode == SPI_NOR_QUAD &&
		   info->flags & SPI_NOR_QUAD_IO_READ) {
		ret = set_quad_mode(nor, info);
		if (ret) {
			dev_err(dev, "quad IO mode not supported\n");
			return ret;
		}
		nor->flash_read = SPI_NOR_QUAD_IO;
	} else if (mode == SPI_NOR_DUAL && info->flags & SPI_NOR_DUAL_READ) {
		nor->flash_read = SPI_NOR_DUAL;
	}

	/* Default commands */
	switch (nor->flash_read) {
	case SPI_NOR_QUAD_IO:
		nor->read_opcode = SPINOR_OP_READ_1_4_4;
		break;
	case SPI_NOR_QUAD:
		nor->read_opcode = SPINOR_OP_READ_1_1_4;
		break;
	case SPI_NOR_DUAL:
		nor->read_opcode = SPINOR_OP_READ_1_1_2;
		break;
	case SPI_NOR_FAST:
		nor->read_opcode = SPINOR_OP_READ_FAST;
		break;
	case SPI_NOR_NORMAL:
		nor->read_opcode = SPINOR_OP_READ;
		break;
	default:
		dev_err(dev, "No Read opcode defined\n");
		return -EINVAL;
	}

	nor->program_opcode = SPINOR_OP_PP;

	if (info->addr_width)
		nor->addr_width = info->addr_width;
	else if (actual_size > 0x1000000) {
#ifdef CONFIG_OF
		np_spi = of_get_next_parent(np);
		if (of_property_match_string(np_spi, "compatible",
					     "xlnx,zynq-qspi-1.0") >= 0) {
			int status;

			nor->addr_width = 3;
			set_4byte(nor, info, 0);
			status = read_ear(nor, info);
			if (status < 0)
				dev_warn(dev, "failed to read ear reg\n");
			else
				nor->curbank = status & EAR_SEGMENT_MASK;
		} else {
#endif
		/* enable 4-byte addressing if the device exceeds 16MiB */
		nor->addr_width = 4;
		if (JEDEC_MFR(info) == CFI_MFR_AMD) {
			/* Dedicated 4-byte command set */
			switch (nor->flash_read) {
			case SPI_NOR_QUAD_IO:
				nor->read_opcode = SPINOR_OP_READ4_1_4_4;
				break;
			case SPI_NOR_QUAD:
				nor->read_opcode = SPINOR_OP_READ4_1_1_4;
				break;
			case SPI_NOR_DUAL:
				nor->read_opcode = SPINOR_OP_READ4_1_1_2;
				break;
			case SPI_NOR_FAST:
				nor->read_opcode = SPINOR_OP_READ4_FAST;
				break;
			case SPI_NOR_NORMAL:
				nor->read_opcode = SPINOR_OP_READ4;
				break;
			}
			nor->program_opcode = SPINOR_OP_PP_4B;
			/* No small sector erase for 4-byte command set */
			nor->erase_opcode = SPINOR_OP_SE_4B;
			mtd->erasesize = info->sector_size;
		} else
			set_4byte(nor, info, 1);
			if (nor->isstacked) {
				nor->spi->master->flags |= SPI_MASTER_U_PAGE;
				set_4byte(nor, info, 1);
				nor->spi->master->flags &= ~SPI_MASTER_U_PAGE;
			}
#ifdef CONFIG_OF
		}
#endif
	} else {
		nor->addr_width = 3;
	}

	nor->read_dummy = spi_nor_read_dummy_cycles(nor);

	dev_info(dev, "%s (%lld Kbytes)\n", id->name,
			(long long)mtd->size >> 10);

	dev_dbg(dev,
		"mtd .name = %s, .size = 0x%llx (%lldMiB), "
		".erasesize = 0x%.8x (%uKiB) .numeraseregions = %d\n",
		mtd->name, (long long)mtd->size, (long long)(mtd->size >> 20),
		mtd->erasesize, mtd->erasesize / 1024, mtd->numeraseregions);

	if (mtd->numeraseregions)
		for (i = 0; i < mtd->numeraseregions; i++)
			dev_dbg(dev,
				"mtd.eraseregions[%d] = { .offset = 0x%llx, "
				".erasesize = 0x%.8x (%uKiB), "
				".numblocks = %d }\n",
				i, (long long)mtd->eraseregions[i].offset,
				mtd->eraseregions[i].erasesize,
				mtd->eraseregions[i].erasesize / 1024,
				mtd->eraseregions[i].numblocks);
	return 0;
}
EXPORT_SYMBOL_GPL(spi_nor_scan);

static const struct spi_device_id *spi_nor_match_id(const char *name)
{
	const struct spi_device_id *id = spi_nor_ids;

	while (id->name[0]) {
		if (!strcmp(name, id->name))
			return id;
		id++;
	}
	return NULL;
}

void spi_nor_shutdown(struct spi_nor *nor)
{
	if (nor->addr_width == 3 &&
		(nor->mtd->size >> nor->shift) > 0x1000000)
		write_ear(nor, 0);
}
EXPORT_SYMBOL_GPL(spi_nor_shutdown);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huang Shijie <shijie8@gmail.com>");
MODULE_AUTHOR("Mike Lavender");
MODULE_DESCRIPTION("framework for SPI NOR");
