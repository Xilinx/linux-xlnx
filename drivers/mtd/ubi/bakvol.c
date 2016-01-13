/* This version ported to the Linux-UBI system by Micron
 *
 * Based on: Micorn APPARATUSES AND METHODS FOR MEMORY MANAGEMENT
 *
 */

/*===========================================
  A UBI solution for MLC NAND powerloss

  This driver implements backup lower page to an UBI internal bakvol volume.

  The contents of this file are subject to the Mozilla Public
  License Version 1.1 (the "License"); You may obtain a copy of
  the License at http://www.mozilla.org/MPL/.But you can use this
  file, no matter in compliance with the License or not.

  The initial developer of the original code is Beanhuo
  <beanhuo@micorn.com>. Portions created by Micorn.

  Alternatively, the contents of this file may be used under the
  terms of the GNU General Public License version 2 (the "GPL"), in
  which case the provisions of the GPL are applicable instead of the
  above.  If you wish to allow the use of your version of this file
  only under the terms of the GPL and not to allow others to use
  your version of this file under the MPL, indicate your decision
  by deleting the provisions above and replace them with the notice
  and other provisions required by the GPL.  If you do not delete
  the provisions above, a recipient may use your version of this
  file under either the MPL or the GPL.
===========================================*/
#include <asm/div64.h>

#include <linux/crc32.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>

#include "ubi.h"

#ifdef CONFIG_MTD_UBI_MLC_NAND_BAKVOL

/* Global point for dual plane programming operation */
struct mtd_oob_ops *oob_ops_bak, *oob_ops_src;
struct mtd_oob_ops *oob_ops; /* Global read/write ops point */

int Recovery_done; /* 1: bakvol recovery already done. 0: need to do recovery */

/* Temporary variables used during scanning PEB */
struct ubi_ec_hdr *ech;
struct ubi_vid_hdr *vidh;

/* Which generation of Micron NAND device will be used */
#define MICRON_NAND_VERSION_L7X_CONFIG 0
#define MICRON_NAND_VERSION_L8X_CONFIG 1

/**
 * is_lowerpage - check page belongs to lower or uppoer page.
 *		For the detail algorithm, please refer to Micron
 *		product datasheet "Shared Pages".
 * @page_num: physical page number within PEB
 *
 *  if paeg_num is lower page, return 1
 *  if page_num is upper page, return 0
 *  if page_num is SLC page, return -1
 */
static int is_lowerpage(int page_num)
{
	int ret;
#if MICRON_NAND_VERSION_L8X_CONFIG
	/* used for Micron L8x series parallel nand */
	switch (page_num) {
	case 2:
	case 3:
	case 248:
	case 249:
	case 252:
	case 253:
	case 254:
	case 255:
		ret = -1;/* This page belongs to SLC page. */
		break;
	default:
		if (page_num % 4 < 2) {
			/* If remainder is 2,3 ,this page belongs to lower page,
			 * else if 0,1, it belongs to upper page */
			ret = 1; /* Lower page */
		} else {
			ret = 0;
		}
		break;
	}
#elif MICRON_NAND_VERSION_L7X_CONFIG
	/* Used for Micron L7X series parallel NAND */
	switch (page_num) {
	case 0:
	case 1:
		ret = 1;
		break;
	case 4:
	case 5:
	case 254:
	case 255: /* 4,5,254, 255 is upper page */
		ret = 0;
		break;
	default:
		if (page_num % 4 > 1)
			/* 2 ,3 is lower page, 0,1 is upper page */
			ret = 1; /* Lower page */
		else
			ret = 0;
		break;
	}
#endif
	return ret;
}

/**
 * get_next_lowerpage - return next lower page number.
 * @page: physical page number within PEB
 *
 * This function returns corresponding next lower page nubmer,
 * if exist lower page; return -1, if this page is already last
 * page or no lower  page after this page.
 * Note: For the detail algorithm, please refer to Micron product
 * datasheet "Shared Pages".
 */
static int get_next_lowerpage(int page)
{
	int ret = -1;
#if MICRON_NAND_VERSION_L8X_CONFIG
	if (page == 254)
		ret = -1;
	if (page >= 255)
		return -1;
	/* Used for L8x  Micron series NAND */
	if (page % 4 < 2) {
		if (page % 2 == 0)
			ret = page + 1;
		else {
			if ((page == 1) || (page == 3))
				ret = page + 1;
			else
				ret = page + 3;
		}

	} else {
		if (page % 2 == 0)
			ret = page + 2;
		else
			ret = page + 1;
	}

	/* Skip SLC page */
	switch (ret) {
	case 2:
	case 3:
		ret = 4;
		break;
	case 248:
	case 249:
	case 252:
	case 253:
	case 254:
	case 255:
		ret = -1;
		break;
	default:
		break;
	}
#elif MICRON_NAND_VERSION_L7X_CONFIG
	/* Used for Micron L7X series NAND */
	switch (page) {
	case 0:
	case 1:
	case 2:
		ret = page + 1;
		break;
	case 4:
	case 5:
		ret = page + 2;
		break;
	case 254:
	case 255:   /* 254, 255 is upper page */
		ret = -1;
		break;
	default:
		if (page % 4 > 1) {
			if (page % 2 == 0)
				ret = page + 1;
			else
				ret = page + 3;
		} else {
			if (page % 2 == 0)
				ret = page + 2;
			else
				ret = page + 1;

		}
		break;
	}
#endif
	return ret;
}

/**
 * get_oppo_plane_seq - return opposite plane number according to PEB number
 *
 *
 */
static int get_oppo_plane_seq(struct ubi_device *ubi, loff_t addr)
{
	int peb;

	peb = (int)(addr >> ubi->mtd->erasesize_shift);

	if (peb % 2)
		return 0; /* plane 0 */
	else
		return 1; /* plane 1 */
}

/**
 * check_original_data - check original data whether being corrupted.
 * @ubi: UBI device description object
 * @bak_addr: address of backup page
 *		if source block not erased, user oob data
 *		should equal to bak_addr.
 * @src_addr: source page address
 * @datbuf: point of backup data
 */
static int check_original_data(struct ubi_device *ubi, loff_t bak_addr,
					loff_t src_addr, uint8_t *datbuf)
{
	int i;
	loff_t read_addr;
	struct bakvol_oob_info *oob_info;
	void *buf = datbuf;
	int ret = -1;

	struct mtd_oob_ops *ops = kmalloc(sizeof(struct mtd_oob_ops),
						GFP_KERNEL);

	if (!ops) {
		ubi_err(ubi, "[%d]Error,kmalloc error!\n", __LINE__);
		goto out;
	}

	ops->datbuf = kmalloc(ubi->min_io_size, GFP_KERNEL);
	ops->oobbuf = kmalloc(ubi->mtd->oobsize, GFP_KERNEL);
	if (!ops->datbuf || !ops->oobbuf) {
		ubi_err(ubi, "[%d]Error,kmalloc error!\n", __LINE__);
		goto free;
	}

	ops->mode = MTD_OPS_AUTO_OOB;
	ops->ooblen = UBI_BAKVOL_OOB_SIZE;
	ops->len = ubi->min_io_size;
	ops->ooboffs = 0;
	ops->retlen = 0;
	ops->oobretlen = 0;

	dbg_gen("%s:Source page addr = 0x%llx\n", __func__, src_addr);

	ret = ubi->mtd->_read_oob(ubi->mtd, src_addr, ops);

	if (ret < 0) {
		/*
		 * Read error, means this page being corrupted,
		 * need to recover.
		 */
		ret = 1;
		goto free;
	}

	oob_info = (struct bakvol_oob_info *)ops->oobbuf;
	read_addr = be64_to_cpu(oob_info->addr);
	if (read_addr != bak_addr) {
		/*
		 * source page oobbuf != bak_addr, means this PEB already
		 * being erased or re-programmed. what's more, exsit bitflips
		 * in user oob area, anyway, current backup page data
		 * already is not corresponding source page data.
		*/
		dbg_gen("[%d] backup page address not match.\n", __LINE__);
		ret = 0;
		goto free;
	}

	for (i = 0; i < ubi->min_io_size; i += 4) {
		if (*(u32 *)ops->datbuf != *(u32 *)buf) {
			ret = 1;
			goto free;
		}
	}
	ret = 0;
	dbg_gen("Original data not being corrupted.\n");

free:
	kfree(ops->oobbuf);
	kfree(ops->datbuf);
	kfree(ops);
out:
	return ret;

}
/**
 * find_last_programmed_page - find last page being programmed.
 * @ubi: UBI device description object
 * @pnum: PEB number
 * @ret_page: return last programmed page number
 */
static int find_last_programmed_page(struct ubi_device *ubi,
					int pnum, int *ret_page)
{
	struct mtd_info *mtd = ubi->mtd;
	int page, start_page, err;

	oob_ops->mode = MTD_OPS_AUTO_OOB;
	oob_ops->ooblen = UBI_BAKVOL_OOB_SIZE;
	oob_ops->len = ubi->min_io_size;
	oob_ops->ooboffs = 0;
	oob_ops->retlen = 0;
	oob_ops->oobretlen = 0;

	page = (mtd->erasesize - 1) >> mtd->writesize_shift;
	start_page = ubi->leb_start >> mtd->writesize_shift;
	for (; page >= start_page; page--) {
		err = ubi->mtd->_read_oob(ubi->mtd,
				(pnum << mtd->erasesize_shift) |
				(page << mtd->writesize_shift), oob_ops);

		if (err < 0 && err != -EUCLEAN) {
			*ret_page = page;
			return -1;
		}

		if (!ubi_check_pattern(oob_ops->oobbuf, 0xFF,
						oob_ops->ooblen)) {
			*ret_page = page;
			return 1;
		}
	}
	return 0;
}

/**
 * get_peb_from_bakvol - get a PEB that already being opened by bakvol.
 * @ubi: UBI device description object
 * @plane: indicates this PEB should belong to which plane
 * @page_num: indicates which page should not be programmed
 */
static struct ubi_bkblk_info *get_peb_from_bakvol(struct ubi_device *ubi,
						char plane, int page_num)
{
	struct ubi_bkblk_tbl *backup_info = ubi->bkblk_tbl;
	struct list_head *head = &backup_info->head;
	struct ubi_bkblk_info *bbi, *tempbbi = NULL;
	int page;

	if (list_empty(head)) {
		dbg_gen("%s:bakvol no already opened PEB.\n", __func__);
		return NULL;
	}

	list_for_each_entry(bbi, head, node) {
		if (bbi->plane == plane) {
			page = get_next_lowerpage(bbi->pgnum);
			if (page == -1)
				continue;
			if (page == page_num) {
				/*
				 * Perfecct match
				 */
				tempbbi = bbi;
				break;
			} else if (page < page_num) {
				/*
				 * Make sure this page not be programmed.
				 */
				if (tempbbi == NULL)
					tempbbi = bbi;
				else {
					if (tempbbi->pgnum > bbi->pgnum)
						/*
						 * As far as possible choose
						 * the PEB with smallest page
						 * number.
						 */
						tempbbi = bbi;
				}
			}
		}
	}

	if (tempbbi)
		dbg_gen("Get bakvol PEB %d:%d for original lower page %d.\n",
				tempbbi->peb, tempbbi->pgnum, page_num);
	else
		dbg_gen("Cannot get free bakvol PEB for plane %d:page %d.\n",
					plane, page_num);

	return tempbbi;
}

static void prepare_bakvol_oob_info(loff_t *addr,
			struct bakvol_oob_info *bakvol_oob)
{
	uint32_t crc;

	ubi_assert(addr != NULL &&  bakvol_oob != NULL);

	bakvol_oob->addr = cpu_to_be64(*(loff_t *)addr);
	crc = crc32(UBI_CRC32_INIT, bakvol_oob, UBI_BAKVOL_OOB_SIZE_CRC);
	bakvol_oob->crc = cpu_to_be32(crc);
}

/**
 * validate_bakvol_oob_info- validate bakvol oob user info.
 * @ubi: UBI device description object
 * @oob_info: bakvol oob info
 *
 * This function returns zero if bakvol oob user info is valid,
 * and return 1 if not.
 */
static int validate_bakvol_oob_info(struct ubi_device *ubi,
			const struct bakvol_oob_info *oob_info)
{
	uint32_t crc, read_crc;
	int ret = 0;

	crc = crc32(UBI_CRC32_INIT, oob_info, UBI_BAKVOL_OOB_SIZE_CRC);
	read_crc = be32_to_cpu(oob_info->crc);
	if (read_crc != crc) {
		ubi_err(ubi, "OOB info CRC error,calculate 0x%x, read 0x%0x",
							crc, read_crc);
		ret = 1;
	}

	return ret;
}

static struct
ubi_bkblk_info *find_min_free_space_peb(struct ubi_bkblk_tbl *backup_info)
{
	struct list_head *head = &backup_info->head;
	struct ubi_bkblk_info *bbi, *retbbi;

	retbbi = list_first_entry(head, struct ubi_bkblk_info, node);

	list_for_each_entry(bbi, head, node) {
		if (bbi->pgnum > retbbi->pgnum)
			retbbi = bbi;
	}

	return retbbi;
}

static struct
ubi_bkblk_info *allo_new_block_for_bakvol(struct ubi_device *ubi, u8 plane)
{

	struct ubi_vid_hdr *vid_hdr;
	struct ubi_volume *vol;
	struct ubi_bkblk_tbl *backup_info = ubi->bkblk_tbl;
	struct ubi_bkblk_info *bbin, *newbbin;
	int bc_plane0, bc_plane1;
	int pnum;
	int err;
	int tries = 1;

	vol = ubi->volumes[vol_id2idx(ubi, UBI_BACKUP_VOLUME_ID)];

	vid_hdr = ubi_zalloc_vid_hdr(ubi, GFP_NOFS);
	if (!vid_hdr)
		return NULL;

	newbbin = kmalloc(sizeof(*newbbin), GFP_ATOMIC);
	if (!newbbin) {
		err = -ENOMEM;
		goto out;
	}

	vid_hdr->vol_type = UBI_VID_DYNAMIC;
	vid_hdr->sqnum = cpu_to_be64(ubi_next_sqnum(ubi));
	vid_hdr->vol_id = cpu_to_be32(UBI_BACKUP_VOLUME_ID);
	vid_hdr->compat = UBI_BACKUP_VOLUME_COMPAT;
	vid_hdr->data_pad = cpu_to_be32(0);
	vid_hdr->data_size = vid_hdr->used_ebs = vid_hdr->data_pad;

retry:
	pnum = ubi_wl_get_plane_peb(ubi, plane);

	if (pnum < 0) {
		err = -ENOMEM;
		goto free_mem;
	}

	if (pnum%2 != plane) {
		err = pnum;
		goto free_mem;
	}

	bc_plane0 = backup_info->bcount_of_plane[0];
	bc_plane1 = backup_info->bcount_of_plane[1];

	if (bc_plane0 + bc_plane1 >= vol->reserved_pebs) {
		/**
		 * we make sure bakvol opened PEBs cannot overflow
		 * bakvol reserved PEB.
		 */
		dbg_gen("bakvol opened PEB over reserved PEB.\n");
		dbg_gen("Will put PEB with minimum free space.\n");
		bbin = find_min_free_space_peb(backup_info);

		if (vol->eba_tbl[bbin->leb] >= 0) {
			dbg_gen("bakvol put PEB %d.\n", bbin->peb);
			err = ubi_wl_put_peb(ubi, vol->vol_id, bbin->leb,
							bbin->peb, 0);
			if (err)
				goto free_mem;
		}

		vol->eba_tbl[bbin->leb] = UBI_LEB_UNMAPPED;
		backup_info->bcount_of_plane[bbin->plane]--;
		newbbin->leb = bbin->leb;

		list_del(&bbin->node);

	} else {
		int i = 0;

		for (i = 0; i < vol->reserved_pebs; i++)
			if (vol->eba_tbl[i] == UBI_LEB_UNMAPPED) {
				newbbin->leb = i;
				break;
			}
	}

	vid_hdr->lnum = cpu_to_be32(newbbin->leb);
	if ((newbbin->leb > vol->reserved_pebs) || (newbbin->leb < 0)) {
		ubi_err(ubi, "BUG: logic block number [%d] error.\n", newbbin->leb);
		panic("BUG!");
	}

	err = ubi_io_write_vid_hdr(ubi, pnum, vid_hdr);
	if (err) {
		ubi_err(ubi, "Failed to write VID header to PEB %d.\n", pnum);
		goto write_error;
	}

	vol->eba_tbl[newbbin->leb] = pnum;

	newbbin->peb = pnum;
	newbbin->plane = plane;
	newbbin->pgnum = get_next_lowerpage(1); /* Skip page0 and page1 */

	/* Update bakvol variable */
	backup_info->bcount_of_plane[plane]++;
	list_add(&newbbin->node, &backup_info->head);

out:
	ubi_free_vid_hdr(ubi, vid_hdr);
	return newbbin;

write_error:
	/*
	 * Bad luck? This physical eraseblock is bad too?  Let's try to
	 * get another one.
	 */
	ubi_err(ubi, "Failed to write to PEB %d", pnum);
	ubi_wl_put_peb(ubi, vol->vol_id, newbbin->leb, newbbin->peb, 1);
	if (++tries > UBI_IO_RETRIES)
		goto free_mem;

	ubi_err(ubi, "Try again");
	goto retry;

free_mem:
	ubi_free_vid_hdr(ubi, vid_hdr);
	kfree(newbbin);
	ubi_err(ubi, "%s:%d %d times failed to alloc a new block!",
			__func__, __LINE__, tries);
	return NULL;
}

/**
 * is_backup_need -check if this page date need to be backup.
 * @ubi: UBI device description object
 * @addr: page address
 *
 * return 0, this page data shouldn't  backup
 * return 1, this page data should  backup
 */
int is_backup_need(struct ubi_device *ubi, loff_t addr)
{
	int page_num, ret;

	page_num = (addr & ubi->mtd->erasesize_mask) >>
						ubi->mtd->writesize_shift;
	if (page_num == 0 || page_num == 1)
		/* Currently,we don't backup EC head and VID head */
		return 0;

	ret = is_lowerpage(page_num);
	if (ret != 1) {
		dbg_gen("Page %d is %s page.\n", page_num,
				(ret == -1 ? "SLC" : "upper"));
		ret = 0;
	} else
		dbg_gen("Page %d is lower page.\n", page_num);

	return ret;
}

/**
 * ubi_check_bakvol_module - check if bakvol has been buildup
 * @ubi: UBI device description object
 *
 * returns 1 in case of  backup volume have been built,
 * 0 in case of don't find
 */
int ubi_check_bakvol_module(struct ubi_device *ubi)
{
	return (ubi->bkblk_tbl->bakvol_flag & UBI_BAKVOL_ENABLE);
}

/**
 * ubi_duplicate_data_to_bakvol - dual plane program data into bakvol area
 *			and main area respectively.
 * @ubi: UBI device description object
 * @addr: address will be programmed
 * @len: date length
 * @retlen: already programmed data length
 * @buf: data buffer, points to data that should be programmed
 */
int ubi_duplicate_data_to_bakvol(struct ubi_device *ubi, loff_t addr,
				size_t len, size_t *retlen, const void *buf)
{
	struct ubi_bkblk_tbl *backup_info = ubi->bkblk_tbl;
	struct mtd_info *mtd = ubi->mtd;
	int err = 0, nobak = 0;
	int pnum;
	u8 oppe_plane;
	loff_t lpage_addr; /* Lower page byte address */
	struct ubi_bkblk_info *pbk;
	int page_num;
	struct bakvol_oob_info *oob_bak = NULL, *oob_src = NULL;

	if (len > ubi->min_io_size) {
		ubi_err(ubi, "%d: Write data len overflow [%d]\n",
							__LINE__, len);
		return -EROFS;
	}

	if (!buf) {
		ubi_err(ubi, "%d: Write buf is NULL!\n", __LINE__);
		return -EROFS;

	}
	oppe_plane = get_oppo_plane_seq(ubi, addr);

	page_num = (int)(addr & ubi->mtd->erasesize_mask) >>
						ubi->mtd->writesize_shift;
	pnum = (int)(addr >> ubi->mtd->erasesize_shift);

	oob_bak = kzalloc(UBI_BAKVOL_OOB_SIZE, GFP_KERNEL);
	if (!oob_bak) {
		err = -ENOMEM;
		ubi_err(ubi, "%s:%d: kzalloc error.\n",
						__func__, __LINE__);
		goto free;
	}

	oob_src = kzalloc(UBI_BAKVOL_OOB_SIZE, GFP_KERNEL);
	if (!oob_src) {
		err = -ENOMEM;
		ubi_err(ubi, "%s:%d: kzalloc error.\n",
						__func__, __LINE__);
		goto free;
	}

	if (backup_info->bcount_of_plane[oppe_plane] == 0)
		pbk = NULL;
	else
		pbk = get_peb_from_bakvol(ubi, oppe_plane, page_num);

	if (pbk == NULL) {
		dbg_gen("Allocate new PEB for Bakvol.\n");
		pbk = allo_new_block_for_bakvol(ubi, oppe_plane);
		if (!pbk) {
			ubi_err(ubi, "Allocate new PEB failed.\n");
			nobak = 1;
			goto Only_source;
		}
	}

	/*
	 * Here original page address stores in user oob area of bakvol page,
	 * and corresponding its backup page address stores in user oob area of
	 * main data area page.
	*/
	lpage_addr = (pbk->peb << mtd->erasesize_shift) |
				(page_num << mtd->writesize_shift);

	/*
	 * organize bakvol page ops
	*/
	oob_ops_bak->datbuf = (uint8_t *)buf;
	oob_ops_bak->mode = MTD_OPS_AUTO_OOB;
	oob_ops_bak->ooblen = UBI_BAKVOL_OOB_SIZE;
	prepare_bakvol_oob_info(&addr, oob_bak);
	oob_ops_bak->oobbuf = (uint8_t *)oob_bak; /* Original page addr */
	oob_ops_bak->len = len;
	oob_ops_bak->ooboffs = 0;
	oob_ops_bak->retlen = 0;
	oob_ops_bak->oobretlen = 0;

Only_source:
	/*
	 * Organize main data area page ops
	*/
	oob_ops_src->datbuf = (uint8_t *)buf;
	oob_ops_src->mode = MTD_OPS_AUTO_OOB;
	oob_ops_src->ooblen = UBI_BAKVOL_OOB_SIZE;
	prepare_bakvol_oob_info(&lpage_addr, oob_src);
	oob_ops_src->oobbuf = (uint8_t *)oob_src; /* backup page addr */
	oob_ops_src->len = len;
	oob_ops_src->ooboffs = 0;
	oob_ops_src->retlen = 0;
	oob_ops_src->oobretlen = 0;

	if (!nobak)
		err = mtd_write_dual_plane_oob(ubi->mtd, lpage_addr,
						oob_ops_bak, addr, oob_ops_src);

	if ((err == -EOPNOTSUPP) || (nobak == 1)) {
		/*
		 * If dual plane program function not enable, or get
		 * bakvol peb failed, we only program original data to
		 * main data area.
		*/
		ubi_err(ubi, "Only program source data.\n");
		err = mtd_write_oob(ubi->mtd, addr, oob_ops_src);
		goto out;
	}

	pbk->pgnum = page_num; /* updata bakvol PEB programmed page info */

out:
	if (err)
		*retlen = 0;
	else
		*retlen = len;
free:
	kfree(oob_bak);
	kfree(oob_src);
	return err;
}


int ubi_bakvol_module_init(struct ubi_device *ubi)
{
	int step;

	if ((ubi->mtd->type != MTD_MLCNANDFLASH) ||
		(ubi->mtd->oobavail < UBI_BAKVOL_OOB_SIZE)) {
		ubi_err(ubi, "NAND cann't meet Bakvol module requirement.\n");
		step = 0;
		goto out_init;
	}

	ech = kzalloc(ubi->ec_hdr_alsize, GFP_KERNEL);
	if (!ech) {
		step = 1;
		goto out_init;
	}

	vidh = ubi_zalloc_vid_hdr(ubi, GFP_KERNEL);
	if (!vidh) {
		step = 2;
		goto out_ech;
	}

	ubi->bkblk_tbl = kzalloc(sizeof(struct ubi_bkblk_tbl), GFP_KERNEL);
	if (!ubi->bkblk_tbl) {
		step = 3;
		goto out_vidh;
	}
	ubi->bkblk_tbl->bcount_of_plane[0] = 0;
	ubi->bkblk_tbl->bcount_of_plane[1] = 0;
	INIT_LIST_HEAD(&ubi->bkblk_tbl->head);

	oob_ops_bak = kzalloc(sizeof(struct mtd_oob_ops), GFP_KERNEL);
	if (!oob_ops_bak) {
		step = 4;
		goto out_backup_info;
	}

	oob_ops_bak->oobbuf = kzalloc(ubi->mtd->oobsize, GFP_KERNEL);
	if (!oob_ops_bak->oobbuf) {
		step = 5;
		goto out_back_oob_ops;
	}

	oob_ops_src = kzalloc(sizeof(struct mtd_oob_ops), GFP_KERNEL);
	if (!oob_ops_src) {
		step = 6;
		goto out_obs_src;
	}

	oob_ops_src->oobbuf = kzalloc(ubi->mtd->oobsize, GFP_KERNEL);
	if (!oob_ops_src->oobbuf) {
		step = 7;
		goto out_oob_ops;
	}

	oob_ops = kmalloc(sizeof(struct mtd_oob_ops), GFP_KERNEL);
	if (!oob_ops) {
		step = 8;
		goto out0;
	}

	oob_ops->datbuf = kmalloc(ubi->min_io_size, GFP_KERNEL);
	if (!oob_ops->datbuf) {
		step = 9;
		goto out1;
	}

	oob_ops->oobbuf = kmalloc(ubi->mtd->oobsize, GFP_KERNEL);
	if (!oob_ops->oobbuf) {
		step = 10;
		goto out2;
	}

	ubi_free_vid_hdr(ubi, vidh);
	kfree(ech);

	ubi->bkblk_tbl->bakvol_flag = UBI_BAKVOL_INIT_START;
	return 0;

out2:
	kfree(oob_ops->datbuf);
out1:
	kfree(oob_ops);
out0:
	kfree(oob_ops_src->oobbuf);
out_oob_ops:
	kfree(oob_ops_src);
out_obs_src:
	kfree(oob_ops_bak->oobbuf);
out_back_oob_ops:
	kfree(oob_ops_bak);
out_backup_info:
	kfree(ubi->bkblk_tbl);
out_vidh:
	ubi_free_vid_hdr(ubi, vidh);
out_ech:
	kfree(ech);
out_init:
	ubi_err(ubi, "bakvol module init error,init step [%d].\n", step);
	ubi->bkblk_tbl->bakvol_flag = UBI_BAKVOL_REJECT;
	return -1;
}

/**
 * ubi_bakvol_peb_scan - check VID header,see if this PEB
 *				belongs to bakvol
 *
 * This function returns 1 if this PEB not belongs to bakvol
 * return -1 in case of allocate resource error
 * return 0  if scan complete, and it belongs to bakvol
 */
int ubi_bakvol_peb_scan(struct ubi_device *ubi, struct ubi_vid_hdr *vidh,
								int pnum)
{
	int page;
	int vol_id;
	struct ubi_bkblk_info *bbi;
	int lnum;
	int ret;

	page = 0;

	vol_id = be32_to_cpu(vidh->vol_id);
	if (vol_id == UBI_BACKUP_VOLUME_ID) {
		/* Found bakvol PEB */
		lnum = be32_to_cpu(vidh->lnum);
		dbg_gen("%s:Found backup PEB,pnum is [%d],lnum is[%d].\n",
							__func__, pnum, lnum);

		/* Unsupported internal volume */
		if (vidh->compat != UBI_COMPAT_REJECT) {
			ubi_err(ubi, "Backup volume compat != UBI_COMPAT_REJECT\n");
			return -1;
		}
	} else {
		return 1;
	}

	ret = find_last_programmed_page(ubi, pnum, &page);
	dbg_gen("bakvol PEB %d last programmed page [%d]\n", pnum, page);

	if (ret == 1) {
		bbi = kmalloc(sizeof(*bbi), GFP_ATOMIC);
		if (!bbi)
			goto out;

		bbi->peb = pnum;
		bbi->leb = lnum;
		bbi->pgnum = page;
		bbi->plane = pnum % 2;
		bbi->plane ? (ubi->bkblk_tbl->bcount_of_plane[1]++) :
					(ubi->bkblk_tbl->bcount_of_plane[0]++);
		list_add(&bbi->node, &ubi->bkblk_tbl->head);
	} else if (ret == 0) {
		/* This backup PEB has not been programmed. */
		bbi = kmalloc(sizeof(*bbi), GFP_ATOMIC);
		if (!bbi)
			goto out;
		bbi->peb = pnum;
		bbi->leb = lnum;
		bbi->pgnum = get_next_lowerpage(1);
		bbi->plane = pnum % 2;
		bbi->plane ? (ubi->bkblk_tbl->bcount_of_plane[1]++) :
					(ubi->bkblk_tbl->bcount_of_plane[0]++);
		list_add(&bbi->node, &ubi->bkblk_tbl->head);
	} else {
		/* Backup block is corrupted,maybe power cut happened during
		 * programming lower page. so this backup block with corrupted
		 * page can be removed from bakvol volume.
		 */
		dbg_gen("PEB %d will be removed from backup volume later.\n",
									pnum);

		bbi = kmalloc(sizeof(*bbi), GFP_ATOMIC);
		if (!bbi)
			goto out;
		bbi->peb = pnum;
		bbi->leb = lnum;
		bbi->pgnum = page;
		bbi->plane = pnum % 2;
		bbi->plane ? (ubi->bkblk_tbl->bcount_of_plane[1]++) :
				(ubi->bkblk_tbl->bcount_of_plane[0]++);
		list_add(&bbi->node, &ubi->bkblk_tbl->head);
	}

	return 0;
out:
	return -1;

}

/**
 * ubi_bakvol_module_init_tail - init backup volume.
 * @ubi: UBI device description object
 * @si: scanning information
 *
 * This function init the backup volume
 * Returns zero in case of success and a negative
 * error code in case of failure
 */
int ubi_bakvol_module_init_tail(struct ubi_device *ubi,
					struct ubi_attach_info *si)
{
	int reserved_pebs = 0;
	struct ubi_volume *vol;
	struct ubi_bkblk_info *bbi;

	if (ubi->bkblk_tbl->bakvol_flag & UBI_BAKVOL_REJECT)
		return 0;

	if (!(ubi->bkblk_tbl->bakvol_flag & UBI_BAKVOL_INIT_DONE)) {

		/* And add the layout volume */
		vol = kzalloc(sizeof(struct ubi_volume), GFP_KERNEL);
		if (!vol)
			return -ENOMEM;

		vol->reserved_pebs = UBI_BACKUP_VOLUME_EBS;
		vol->alignment = 1;
		vol->vol_type = UBI_DYNAMIC_VOLUME;
		vol->name_len = sizeof(UBI_BACKUP_VOLUME_NAME) - 1;
		vol->data_pad = 0;
		memcpy(vol->name, UBI_BACKUP_VOLUME_NAME, vol->name_len + 1);

		vol->usable_leb_size = ubi->leb_size;
		vol->used_ebs = vol->reserved_pebs;
		vol->last_eb_bytes = vol->reserved_pebs;
		vol->used_bytes =
		    (long long)vol->used_ebs * (ubi->leb_size - vol->data_pad);
		vol->vol_id = UBI_BACKUP_VOLUME_ID;
		vol->ref_count = UBI_BACKUP_VOLUME_EBS;
		ubi->volumes[vol_id2idx(ubi, vol->vol_id)] = vol;
		reserved_pebs += vol->reserved_pebs;
		ubi->vol_count += 1;
		vol->ubi = ubi;
		if (reserved_pebs > ubi->avail_pebs) {
			ubi_err(ubi, "No enough PEBs, required %d, available %d",
				reserved_pebs, ubi->avail_pebs);
			return -1;
		}
		ubi->rsvd_pebs += reserved_pebs;
		ubi->avail_pebs -= reserved_pebs;

		ubi->bkblk_tbl->bakvol_flag = UBI_BAKVOL_INIT_DONE;

		ubi_msg(ubi, "bakvol module opened PEB list:");
		list_for_each_entry(bbi, &(ubi->bkblk_tbl->head), node) {
			ubi_msg(ubi, "peb %d,pgnum %d,plane %d,;leb %d.",
				bbi->peb, bbi->pgnum, bbi->plane, bbi->leb);
		}
	}
	return 0;
}

int ubi_corrupted_data_recovery(struct ubi_volume_desc *desc)
{
	u32 base_offset, buff_offset, pnum, offset, page;
	u32 correct, uncorrect_l, uncorrect_h;
	int ret, err;
	struct ubi_device *ubi = desc->vol->ubi;
	struct mtd_info *mtd = ubi->mtd;
	struct ubi_bkblk_info *bbi;
	unsigned char *buf = ubi->peb_buf;
	struct ubi_volume_desc volume_desc;
	int i, move;
	loff_t bakpage_addr;
	struct bakvol_oob_info *oob_info;
	off_t src_addr;

	move = 0;
	correct = 0;
	uncorrect_h = 0;
	uncorrect_l = 0;

	if ((ubi->bkblk_tbl->bakvol_flag & UBI_BAKVOL_RECOVERY) ||
		!(ubi->bkblk_tbl->bakvol_flag & UBI_BAKVOL_INIT_DONE))
		return 0;

	vidh = ubi_zalloc_vid_hdr(ubi, GFP_KERNEL);
	if (!vidh)
		return -1;

	oob_ops->mode = MTD_OPS_AUTO_OOB;
	oob_ops->ooblen = UBI_BAKVOL_OOB_SIZE;
	oob_ops->len = ubi->min_io_size;

	list_for_each_entry(bbi, &(ubi->bkblk_tbl->head), node) {
		dbg_gen("Processing bakvol PEB %d,pgnum %d,plane %d.\n",
			bbi->peb, bbi->pgnum, bbi->plane);
		i = get_next_lowerpage(1);

		for ( ; (i <= bbi->pgnum) && (i != -1);
				i = get_next_lowerpage(i)) {
			/*
			 * Read backup data from bakvol PEB with OOB.
			*/
			oob_ops->ooboffs = 0;
			oob_ops->retlen = 0;
			oob_ops->oobretlen = 0;

			err = ubi->mtd->_read_oob(ubi->mtd,
					(bbi->peb << mtd->erasesize_shift) |
					(i << mtd->writesize_shift), oob_ops);

			if (err < 0 && err != -EUCLEAN) {
				dbg_gen("Read bakvol PEB %d:%d error %d.\n",
						bbi->peb, i, err);
				move = 1;
				continue;
			}

			if (ubi_check_pattern(oob_ops->oobbuf, 0xFF,
						oob_ops->ooblen)) {
				dbg_gen("Bakvol PEB %d ever skip lower page %d.\n",
						bbi->peb, i);
				continue;
			}

			oob_info = (struct bakvol_oob_info *)oob_ops->oobbuf;

			if (validate_bakvol_oob_info(ubi, oob_info)) {
				dbg_gen("Bakvol PEB %d page %d user oob area exist bitflips.\n",
						bbi->peb, i);
				continue;
			}

		src_addr = be64_to_cpu(oob_info->addr);

		/*
		 * Calculate backup page address, used for check if source block
		 * already being erased or re-mapped.
		*/
		bakpage_addr = (bbi->peb << mtd->erasesize_shift) |
					(i << mtd->writesize_shift);

		ret = check_original_data(ubi, bakpage_addr,
						src_addr, oob_ops->datbuf);
		  if (ret == 1) {
			/* The original data isn't correct anymore,
			 * already being damaged, and oobbuf store original page
			 * address.
			 */
			pnum = src_addr >> mtd->erasesize_shift;
			offset = src_addr & mtd->erasesize_mask;
			buff_offset = 0;

			/**
			 * Recover operation
			 */

			for (base_offset = ubi->leb_start;
				base_offset < ubi->peb_size;
				base_offset += mtd->writesize) {

				ret = ubi_io_read(ubi, buf + buff_offset, pnum,
						base_offset, mtd->writesize);

			if (ret == -EBADMSG || ret == -EIO) {
				if (base_offset == offset) {
				    dbg_gen("Bakvol recover offset 0x%x.....\n",
						base_offset);
				    memcpy(buf + buff_offset, oob_ops->datbuf,
							mtd->writesize);
				    correct++;
				} else {
				    page = base_offset >> mtd->writesize_shift;
					if (is_lowerpage(page)) {
						/* More low-page also
						 * be corrupted */
						uncorrect_l++;
					ubi_err(ubi, "PEB %d unrecovery lower page %d\n",
								pnum, page);
					uncorrect_l++;
					} else {
					/**
					 * Unbackup upper page also
					 * be corruptted */
					ubi_err(ubi, "PEB %d unrecovery upper page %d.\n",
								pnum, page);
					uncorrect_h++;
					}
				}
			}

			ret = ubi_check_pattern(buf + buff_offset, 0xFF,
							mtd->writesize);
			if (ret)
				/* This page is empty, so the last of pages
				 * also are empty, no need to read.*/
				break;

			buff_offset += mtd->writesize;
			}

		if ((correct == 0) || (uncorrect_h > 1) || (uncorrect_l > 0)) {
			/* Could not recover. We only accept two corrupted
			 * pages, they are paired-page. If there are two
			 * corrupted lower pages within one PEB, also give
			 * up recovery.
			 */
			dbg_gen("Can not be recoverred.\n");
			uncorrect_l = 0;
			uncorrect_h = 0;
			continue;
			}

		ret = ubi_io_read_vid_hdr(ubi, pnum, vidh, 0);

		volume_desc.vol = ubi->volumes[be32_to_cpu(vidh->vol_id)];
		volume_desc.mode = UBI_READWRITE;
		ret = ubi_leb_change(&volume_desc, be32_to_cpu(vidh->lnum),
							buf, buff_offset);
		correct = 0;
		uncorrect_h = 0;

		if (ret) {
			ubi_err(ubi, "Changing %d bytes in LEB %d failed",
						buff_offset, vidh->lnum);
			ubi_err(ubi, "Ret error:%d.", ret);
			dump_stack();
		} else
			dbg_gen(".....Done\n");

	  }

	}

		if (move == 1) {
		    bbi->pgnum = (ubi->mtd->erasesize - 1) >>
					ubi->mtd->writesize_shift;
		    move = 0;
		}
	}
	ubi->bkblk_tbl->bakvol_flag |= UBI_BAKVOL_RECOVERY;
	ubi_free_vid_hdr(ubi, vidh);
	return 0;
}
EXPORT_SYMBOL_GPL(ubi_corrupted_data_recovery);

void clear_bakvol(struct ubi_device *ubi)
{
	kfree(oob_ops->datbuf);
	kfree(oob_ops);
	kfree(oob_ops_src->oobbuf);
	kfree(oob_ops_src);
	kfree(oob_ops_bak->oobbuf);
	kfree(oob_ops_bak);
	kfree(ubi->bkblk_tbl);
	ubi->bkblk_tbl->bakvol_flag = UBI_BAKVOL_INIT_START;
}

void init_bakvol(struct ubi_volume_desc *desc, uint8_t choice)
{
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;

	if (choice) {
		if (ubi->bkblk_tbl->bakvol_flag & UBI_BAKVOL_INIT_DONE ||
			ubi->bkblk_tbl->bakvol_flag & UBI_BAKVOL_DISABLE) {
			dbg_gen("[%s][%d] Enable bakvol module successfully!\n",
						__func__, __LINE__);
			ubi->bkblk_tbl->bakvol_flag &= ~UBI_BAKVOL_DISABLE;
			ubi->bkblk_tbl->bakvol_flag |= UBI_BAKVOL_ENABLE;

		} else
			dbg_gen("[%s][%d] Enable bakvol module failed!\n",
					__func__, __LINE__);

	} else {
		ubi->bkblk_tbl->bakvol_flag &= ~UBI_BAKVOL_ENABLE;
		ubi->bkblk_tbl->bakvol_flag |= UBI_BAKVOL_DISABLE;
		dbg_gen("[%s][%d] Disable bakvol module!\n",
						__func__, __LINE__);
	}
}
EXPORT_SYMBOL_GPL(init_bakvol);
#else
int is_backup_need(struct ubi_device *ubi, loff_t addr)
{
	return 0;
}

int ubi_check_bakvol_module(struct ubi_device *ubi)
{

	return 0;
}

int ubi_duplicate_data_to_bakvol(struct ubi_device *ubi, loff_t addr,
				size_t len, size_t *retlen, const void *buf)
{
	return 0;
}
int ubi_bakvol_module_init(struct ubi_device *ubi)
{
	return 0;

}

int ubi_bakvol_peb_scan(struct ubi_device *ubi, struct ubi_vid_hdr *vidh,
								int pnum)
{
	return 0;
}

int ubi_bakvol_module_init_tail(struct ubi_device *ubi,
					struct ubi_attach_info *si)
{
	return 0;
}

int ubi_corrupted_data_recovery(struct ubi_volume_desc *desc)
{
	return 0;

}
EXPORT_SYMBOL_GPL(ubi_corrupted_data_recovery);


void clear_bakvol(struct ubi_device *ubi)
{

}

void init_bakvol(struct ubi_volume_desc *desc, uint8_t choice)
{

}
EXPORT_SYMBOL_GPL(init_bakvol);
#endif

MODULE_LICENSE("Dual MPL/GPL");
MODULE_AUTHOR("Bean Huo <beanhuo@micron.com>");
MODULE_DESCRIPTION("Support code for MLC NAND pair page powerloss protection");
