// SPDX-License-Identifier: GPL-2.0
/*
 * SPI NOR Software Write Protection logic.
 *
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */
#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-nor.h>

#include "core.h"

static u8 spi_nor_get_sr_bp_mask(struct spi_nor *nor)
{
	u8 mask = SR_BP2 | SR_BP1 | SR_BP0;

	if (nor->flags & SNOR_F_HAS_SR_BP3_BIT6)
		return mask | SR_BP3_BIT6;
	else if (nor->flags & SNOR_F_HAS_SR_BP3_BIT5)
		return mask | SR_BP3_BIT5;

	if (nor->flags & SNOR_F_HAS_4BIT_BP)
		return mask | SR_BP3;

	return mask;
}

static u8 spi_nor_get_sr_tb_mask(struct spi_nor *nor)
{
	if (nor->flags & SNOR_F_HAS_SR_TB_BIT6)
		return SR_TB_BIT6;
	else
		return SR_TB_BIT5;
}

static u64 spi_nor_get_min_prot_length_sr(struct spi_nor *nor)
{
	unsigned int bp_slots, bp_slots_needed;
	u8 mask = spi_nor_get_sr_bp_mask(nor);
	u32 n_sectors = nor->info->n_sectors;
	u32 sector_size = nor->info->sector_size;

	if (nor->isstacked)
		n_sectors <<= 1;

	if (nor->isparallel)
		sector_size <<= 1;

	/* Reserved one for "protect none" and one for "protect all". */
	bp_slots = (1 << hweight8(mask)) - 2;
	bp_slots_needed = ilog2(n_sectors);

	if (bp_slots_needed > bp_slots)
		return sector_size <<
			(bp_slots_needed - bp_slots);
	else
		return sector_size;
}

static void spi_nor_get_locked_range_sr(struct spi_nor *nor, u8 sr, loff_t *ofs,
					uint64_t *len)
{
	struct mtd_info *mtd = &nor->mtd;
	u64 min_prot_len;
	u8 mask = spi_nor_get_sr_bp_mask(nor);
	u8 tb_mask = spi_nor_get_sr_tb_mask(nor);
	u8 bp, val = sr & mask;

	if (nor->flags & SNOR_F_HAS_SR_BP3_BIT6 && val & SR_BP3_BIT6)
		val = (val & ~SR_BP3_BIT6) | SR_BP3;

	bp = val >> SR_BP_SHIFT;

	if (!bp) {
		/* No protection */
		*ofs = 0;
		*len = 0;
		return;
	}

	min_prot_len = spi_nor_get_min_prot_length_sr(nor);
	*len = min_prot_len << (bp - 1);

	if (*len > mtd->size)
		*len = mtd->size;

	if (nor->flags & SNOR_F_HAS_SR_TB && sr & tb_mask)
		*ofs = 0;
	else
		*ofs = mtd->size - *len;
}

/*
 * Return true if the entire region is locked (if @locked is true) or unlocked
 * (if @locked is false); false otherwise.
 */
static bool spi_nor_check_lock_status_sr(struct spi_nor *nor, loff_t ofs,
					 uint64_t len, u8 sr, bool locked)
{
	loff_t lock_offs, lock_offs_max, offs_max;
	uint64_t lock_len;

	if (!len)
		return true;

	spi_nor_get_locked_range_sr(nor, sr, &lock_offs, &lock_len);

	lock_offs_max = lock_offs + lock_len;
	offs_max = ofs + len;

	if (locked)
		/* Requested range is a sub-range of locked range */
		return (offs_max <= lock_offs_max) && (ofs >= lock_offs);
	else
		/* Requested range does not overlap with locked range */
		return (ofs >= lock_offs_max) || (offs_max <= lock_offs);
}

static bool spi_nor_is_locked_sr(struct spi_nor *nor, loff_t ofs, uint64_t len,
				 u8 sr)
{
	return spi_nor_check_lock_status_sr(nor, ofs, len, sr, true);
}

static bool spi_nor_is_unlocked_sr(struct spi_nor *nor, loff_t ofs,
				   uint64_t len, u8 sr)
{
	return spi_nor_check_lock_status_sr(nor, ofs, len, sr, false);
}

/*
 * Lock a region of the flash. Compatible with ST Micro and similar flash.
 * Supports the block protection bits BP{0,1,2}/BP{0,1,2,3} in the status
 * register
 * (SR). Does not support these features found in newer SR bitfields:
 *   - SEC: sector/block protect - only handle SEC=0 (block protect)
 *   - CMP: complement protect - only support CMP=0 (range is not complemented)
 *
 * Support for the following is provided conditionally for some flash:
 *   - TB: top/bottom protect
 *
 * Sample table portion for 8MB flash (Winbond w25q64fw):
 *
 *   SEC  |  TB   |  BP2  |  BP1  |  BP0  |  Prot Length  | Protected Portion
 *  --------------------------------------------------------------------------
 *    X   |   X   |   0   |   0   |   0   |  NONE         | NONE
 *    0   |   0   |   0   |   0   |   1   |  128 KB       | Upper 1/64
 *    0   |   0   |   0   |   1   |   0   |  256 KB       | Upper 1/32
 *    0   |   0   |   0   |   1   |   1   |  512 KB       | Upper 1/16
 *    0   |   0   |   1   |   0   |   0   |  1 MB         | Upper 1/8
 *    0   |   0   |   1   |   0   |   1   |  2 MB         | Upper 1/4
 *    0   |   0   |   1   |   1   |   0   |  4 MB         | Upper 1/2
 *    X   |   X   |   1   |   1   |   1   |  8 MB         | ALL
 *  ------|-------|-------|-------|-------|---------------|-------------------
 *    0   |   1   |   0   |   0   |   1   |  128 KB       | Lower 1/64
 *    0   |   1   |   0   |   1   |   0   |  256 KB       | Lower 1/32
 *    0   |   1   |   0   |   1   |   1   |  512 KB       | Lower 1/16
 *    0   |   1   |   1   |   0   |   0   |  1 MB         | Lower 1/8
 *    0   |   1   |   1   |   0   |   1   |  2 MB         | Lower 1/4
 *    0   |   1   |   1   |   1   |   0   |  4 MB         | Lower 1/2
 *
 * Returns negative on errors, 0 on success.
 */
static int spi_nor_sr_lock(struct spi_nor *nor, loff_t ofs, uint64_t len)
{
	struct mtd_info *mtd = &nor->mtd;
	u64 min_prot_len;
	int ret, status_old, status_new;
	u8 mask = spi_nor_get_sr_bp_mask(nor);
	u8 tb_mask = spi_nor_get_sr_tb_mask(nor);
	u8 pow, val;
	loff_t lock_len;
	bool can_be_top = true, can_be_bottom = nor->flags & SNOR_F_HAS_SR_TB;
	bool use_top;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	status_old = nor->bouncebuf[0];

	/* If nothing in our range is unlocked, we don't need to do anything */
	if (spi_nor_is_locked_sr(nor, ofs, len, status_old))
		return 0;

	/* If anything below us is unlocked, we can't use 'bottom' protection */
	if (!spi_nor_is_locked_sr(nor, 0, ofs, status_old))
		can_be_bottom = false;

	/* If anything above us is unlocked, we can't use 'top' protection */
	if (!spi_nor_is_locked_sr(nor, ofs + len, mtd->size - (ofs + len),
				  status_old))
		can_be_top = false;

	if (!can_be_bottom && !can_be_top)
		return -EINVAL;

	/* Prefer top, if both are valid */
	use_top = can_be_top;

	/* lock_len: length of region that should end up locked */
	if (use_top)
		lock_len = mtd->size - ofs;
	else
		lock_len = ofs + len;

	if (lock_len == mtd->size) {
		val = mask;
	} else {
		min_prot_len = spi_nor_get_min_prot_length_sr(nor);
		pow = ilog2(lock_len) - ilog2(min_prot_len) + 1;
		val = pow << SR_BP_SHIFT;

		if (nor->flags & SNOR_F_HAS_SR_BP3_BIT6 && val & SR_BP3)
			val = (val & ~SR_BP3) | SR_BP3_BIT6;
		else if (nor->flags & SNOR_F_HAS_SR_BP3_BIT5 &&
			 val & SR_BP3_BIT5)
			val |= SR_BP3_BIT5;

		if (val & ~mask)
			return -EINVAL;

		/* Don't "lock" with no region! */
		if (!(val & mask))
			return -EINVAL;
	}

	status_new = (status_old & ~mask & ~tb_mask) | val;

	/* Disallow further writes if WP pin is asserted */
	status_new |= SR_SRWD;

	if (!use_top)
		status_new |= tb_mask;

	/* Don't bother if they're the same */
	if (status_new == status_old)
		return 0;

	/* Only modify protection if it will not unlock other areas */
	if ((status_new & mask) < (status_old & mask))
		return -EINVAL;

	return spi_nor_write_sr_and_check(nor, status_new);
}

static bool spi_nor_is_lower_area(struct spi_nor *nor, loff_t ofs, uint64_t len)
{
	struct mtd_info *mtd = &nor->mtd;

	if (nor->flags & SNOR_F_HAS_SR_TB)
		return ((ofs + len) <= (mtd->size >> 1));
	else
		return false;
}

static bool spi_nor_is_upper_area(struct spi_nor *nor, loff_t ofs, uint64_t len)
{
	struct mtd_info *mtd = &nor->mtd;

	if ((nor->flags & SNOR_F_HAS_SR_TB))
		return (ofs >= (mtd->size >> 1));
	else
		return true;
}

/*
 * Unlock a region of the flash. See spi_nor_sr_lock() for more info
 *
 * Returns negative on errors, 0 on success.
 */
static int spi_nor_sr_unlock(struct spi_nor *nor, loff_t ofs, uint64_t len)
{
	struct mtd_info *mtd = &nor->mtd;
	u64 min_prot_len;
	int ret, status_old, status_new;
	u8 mask = spi_nor_get_sr_bp_mask(nor);
	u8 tb_mask = spi_nor_get_sr_tb_mask(nor);
	u8 pow, val;
	loff_t lock_len;
	bool can_be_top = true, can_be_bottom = nor->flags & SNOR_F_HAS_SR_TB;
	bool use_top;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	status_old = nor->bouncebuf[0];

	/* If nothing in our range is locked, we don't need to do anything */
	if (spi_nor_is_unlocked_sr(nor, ofs, len, status_old))
		return 0;

	/* If anything below us is locked, we can't use 'top' protection */
	if ((!spi_nor_is_unlocked_sr(nor, 0, ofs, status_old)) ||
	    spi_nor_is_lower_area(nor, ofs, len))
		can_be_top = false;

	/* If anything above us is locked, we can't use 'bottom' protection */
	if (!spi_nor_is_unlocked_sr(nor, ofs + len, mtd->size - (ofs + len),
				    status_old) || spi_nor_is_upper_area(nor, ofs, len))
		can_be_bottom = false;

	if (!can_be_bottom && !can_be_top)
		return -EINVAL;

	/* Prefer top, if both are valid */
	use_top = can_be_top;

	/* lock_len: length of region that should remain locked */
	if (use_top)
		lock_len = mtd->size - (ofs + len);
	else
		lock_len = ofs;

	if (lock_len == 0) {
		val = 0; /* fully unlocked */
	} else {
		min_prot_len = spi_nor_get_min_prot_length_sr(nor);
		pow = ilog2(mtd->size) - ilog2(min_prot_len) + 1;
		val = pow << SR_BP_SHIFT;

		if (nor->flags & SNOR_F_HAS_SR_BP3_BIT6 && val & SR_BP3)
			val = (val & ~SR_BP3) | SR_BP3_BIT6;
		else if (nor->flags & SNOR_F_HAS_SR_BP3_BIT5 &&
			 val & SR_BP3_BIT5)
			val |= SR_BP3_BIT5;

		/* Some power-of-two sizes are not supported */
		if (val & ~mask)
			return -EINVAL;
	}

	status_new = (status_old & ~mask & ~tb_mask) | val;

	/* Don't protect status register if we're fully unlocked */
	if (lock_len == 0)
		status_new &= ~SR_SRWD;

	if (!use_top)
		status_new |= tb_mask;

	/* Don't bother if they're the same */
	if (status_new == status_old)
		return 0;

	/* Only modify protection if it will not lock other areas */
	if ((status_new & mask) > (status_old & mask))
		return -EINVAL;

	return spi_nor_write_sr_and_check(nor, status_new);
}

/*
 * Check if a region of the flash is (completely) locked. See spi_nor_sr_lock()
 * for more info.
 *
 * Returns 1 if entire region is locked, 0 if any portion is unlocked, and
 * negative on errors.
 */
static int spi_nor_sr_is_locked(struct spi_nor *nor, loff_t ofs, uint64_t len)
{
	int ret;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	return spi_nor_is_locked_sr(nor, ofs, len, nor->bouncebuf[0]);
}

static const struct spi_nor_locking_ops spi_nor_sr_locking_ops = {
	.lock = spi_nor_sr_lock,
	.unlock = spi_nor_sr_unlock,
	.is_locked = spi_nor_sr_is_locked,
};

void spi_nor_init_default_locking_ops(struct spi_nor *nor)
{
	nor->params->locking_ops = &spi_nor_sr_locking_ops;
}

static int write_sr_modify_protection(struct spi_nor *nor, u8 status,
				      u8 lock_bits)
{
	u8 status_new, bp_mask;

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
	} else if (nor->jedec_id == CFI_MFR_WINBND) { /* Winbond */
		status_new &= ~SR_BP3_BIT5;

		/* Protected area starts from top */
		status_new &= ~SR_BP_TB;

		if (lock_bits > 7)
			bp_mask |= SR_BP3_BIT5;
	/* ISSI */
	} else if (nor->jedec_id == CFI_MFR_PMC) {
		status_new &= ~SR_BP3_BIT5;

		if (lock_bits > 7)
			bp_mask |= SR_BP3_BIT5;
	}

	if (nor->is_lock)
		status_new |= bp_mask;

	/* For spansion flashes */
	if (nor->jedec_id == CFI_MFR_AMD) {
		spi_nor_read_cr(nor, &nor->bouncebuf[1]);
		nor->bouncebuf[0] = status_new;
		if (spi_nor_write_sr(nor, nor->bouncebuf, 2) < 0)
			return 1;
	} else {
		nor->bouncebuf[0] = status_new;
		if (spi_nor_write_sr(nor, &nor->bouncebuf[0], 1) < 0)
			return 1;
	}
	return 0;
}

static u8 bp_bits_from_sr(struct spi_nor *nor, u8 status)
{
	u8 ret;

	ret = (((status) & SR_BP_BIT_MASK) >> SR_BP_BIT_OFFSET);
	if (nor->jedec_id == 0x20)
		ret |= ((status & SR_BP3) >> (SR_BP_BIT_OFFSET + 1));
	else if ((nor->jedec_id == CFI_MFR_WINBND) &&
		 (nor->flags & SNOR_F_HAS_4BIT_BP))
		ret |= ((status & SR_BP3_BIT5) >> SR_BP_BIT_OFFSET);
	else if (nor->jedec_id == CFI_MFR_PMC)	/* ISSI */
		ret |= ((status & SR_BP3_BIT5) >> SR_BP_BIT_OFFSET);

	return ret;
}

static inline u16 min_lockable_sectors(struct spi_nor *nor,
				       u16 n_sectors)
{
	u16 lock_granularity;

	/*
	 * Revisit - SST (not used by us) has the same JEDEC ID as micron but
	 * protected area table is similar to that of spansion.
	 */
	lock_granularity = max(1, n_sectors / M25P_MAX_LOCKABLE_SECTORS);
	if (nor->jedec_id == CFI_MFR_ST ||	/* Micron */
	    nor->jedec_id == CFI_MFR_PMC)	/* ISSI */
		lock_granularity = 1;

	return lock_granularity;
}

static inline uint32_t get_protected_area_start(struct spi_nor *nor,
						u8 lock_bits)
{
	u16 n_sectors;
	u32 sector_size;
	u64 mtd_size;
	struct mtd_info *mtd = &nor->mtd;

	n_sectors = nor->n_sectors;
	sector_size = nor->sector_size;
	mtd_size = mtd->size;

	if (nor->isparallel) {
		sector_size = (nor->sector_size >> 1);
		mtd_size = (mtd->size >> 1);
	}
	if (nor->isstacked) {
		n_sectors = (nor->n_sectors >> 1);
		mtd_size = (mtd->size >> 1);
	}

	return mtd_size - (1 << (lock_bits - 1)) *
		min_lockable_sectors(nor, n_sectors) * sector_size;
}

static u8 min_protected_area_including_offset(struct spi_nor *nor,
					      uint32_t offset)
{
	u8 lock_bits, lockbits_limit;

	/*
	 * Revisit - SST (not used by us) has the same JEDEC ID as micron but
	 * protected area table is similar to that of spansion.
	 * Mircon has 4 block protect bits.
	 */
	lockbits_limit = 7;
	if (nor->jedec_id == CFI_MFR_ST ||	/* Micron */
	    nor->jedec_id == CFI_MFR_PMC)	/* ISSI */
		lockbits_limit = 15;

	for (lock_bits = 1; lock_bits < lockbits_limit; lock_bits++) {
		if (offset >= get_protected_area_start(nor, lock_bits))
			break;
	}
	return lock_bits;
}

static int spi_nor_lock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;
	u8 lock_bits;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	if (nor->isparallel == 1)
		ofs = ofs >> nor->shift;

	if (nor->isstacked == 1) {
		if (ofs >= (mtd->size / 2)) {
			ofs = ofs - (mtd->size / 2);
			nor->spimem->spi->master->flags |= SPI_MASTER_U_PAGE;
		} else {
			nor->spimem->spi->master->flags &= ~SPI_MASTER_U_PAGE;
		}
	}
	ret = nor->params->locking_ops->lock(nor, ofs, len);
	/* Wait until finished previous command */
	ret = spi_nor_wait_till_ready(nor);
	if (ret)
		goto err;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);

	lock_bits = min_protected_area_including_offset(nor, ofs);

	/* Only modify protection if it will not unlock other areas */
	if (lock_bits > bp_bits_from_sr(nor, nor->bouncebuf[0])) {
		nor->is_lock = 1;
		ret = write_sr_modify_protection(nor, nor->bouncebuf[0], lock_bits);
	} else {
		dev_err(nor->dev, "trying to unlock already locked area\n");
	}
err:
	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int spi_nor_unlock(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;
	u8 lock_bits;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	if (nor->isparallel == 1)
		ofs = ofs >> nor->shift;

	if (nor->isstacked == 1) {
		if (ofs >= (mtd->size / 2)) {
			ofs = ofs - (mtd->size / 2);
			nor->spimem->spi->master->flags |= SPI_MASTER_U_PAGE;
		} else {
			nor->spimem->spi->master->flags &= ~SPI_MASTER_U_PAGE;
		}
	}
	ret = nor->params->locking_ops->unlock(nor, ofs, len);
	/* Wait until finished previous command */
	ret = spi_nor_wait_till_ready(nor);
	if (ret)
		goto err;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);

	lock_bits = min_protected_area_including_offset(nor, ofs + len) - 1;
	/* Only modify protection if it will not lock other areas */
	if (lock_bits < bp_bits_from_sr(nor, nor->bouncebuf[0])) {
		nor->is_lock = 0;
		ret = write_sr_modify_protection(nor, nor->bouncebuf[0], lock_bits);
	} else {
		dev_err(nor->dev, "trying to lock already unlocked area\n");
	}
err:
	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int spi_nor_is_locked(struct mtd_info *mtd, loff_t ofs, uint64_t len)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	ret = nor->params->locking_ops->is_locked(nor, ofs, len);

	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static void spi_nor_prot_unlock(struct spi_nor *nor)
{
	if (nor->info->flags & SST_GLOBAL_PROT_UNLK) {
		spi_nor_write_enable(nor);
		if (nor->spimem) {
			struct spi_mem_op op =
				SPI_MEM_OP(SPI_MEM_OP_CMD(GLOBAL_BLKPROT_UNLK, 1),
					   SPI_MEM_OP_NO_ADDR,
					   SPI_MEM_OP_NO_DUMMY,
					   SPI_MEM_OP_NO_DATA);

			spi_mem_exec_op(nor->spimem, &op);
		} else {
			/* Unlock global write protection bits */
			nor->controller_ops->write_reg(nor, GLOBAL_BLKPROT_UNLK, NULL, 0);
		}
	}
	spi_nor_wait_till_ready(nor);
}

/**
 * spi_nor_try_unlock_all() - Tries to unlock the entire flash memory array.
 * @nor:	pointer to a 'struct spi_nor'.
 *
 * Some SPI NOR flashes are write protected by default after a power-on reset
 * cycle, in order to avoid inadvertent writes during power-up. Backward
 * compatibility imposes to unlock the entire flash memory array at power-up
 * by default.
 *
 * Unprotecting the entire flash array will fail for boards which are hardware
 * write-protected. Thus any errors are ignored.
 */
void spi_nor_try_unlock_all(struct spi_nor *nor)
{
	int ret;
	const struct flash_info *info = nor->info;

	if (nor->jedec_id == CFI_MFR_ATMEL ||
	    nor->jedec_id == CFI_MFR_INTEL ||
	    nor->jedec_id == CFI_MFR_SST ||
	    nor->flags & SNOR_F_HAS_LOCK) {
		if (info->flags & SST_GLOBAL_PROT_UNLK) {
			spi_nor_prot_unlock(nor);
		} else {
			ret = spi_nor_unlock(&nor->mtd, 0, nor->params->size);
			if (ret)
				dev_dbg(nor->dev, "Failed to unlock the entire flash memory array\n");
		}
	}
}

void spi_nor_register_locking_ops(struct spi_nor *nor)
{
	struct mtd_info *mtd = &nor->mtd;

	if (!nor->params->locking_ops)
		return;

	mtd->_lock = spi_nor_lock;
	mtd->_unlock = spi_nor_unlock;
	mtd->_is_locked = spi_nor_is_locked;
}
