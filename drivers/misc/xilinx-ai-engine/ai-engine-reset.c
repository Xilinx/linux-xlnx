// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver resets implementation
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include <linux/bitfield.h>
#include <linux/io.h>

#include "ai-engine-internal.h"

/**
 * aie_part_set_col_reset() - set AI engine column reset
 * @apart: AI engine partition
 * @col: column to reset
 * @reset: true to assert reset, false to release reset
 */
static void aie_part_set_col_reset(struct aie_partition *apart, u32 col,
				   bool reset)
{
	struct aie_device *adev = apart->adev;
	const struct aie_single_reg_field *col_rst = adev->col_rst;
	struct aie_location loc;
	u32 regoff, val;

	loc.row = 0;
	loc.col = col;

	val = aie_get_field_val(col_rst, (reset ? 1 : 0));
	regoff = aie_cal_regoff(adev, loc, col_rst->regoff);
	iowrite32(val, adev->base + regoff);
}

/**
 * aie_part_set_col_clkbuf() - set AI engine column clock buffer
 * @apart: AI engine partition
 * @col: column to reset
 * @enable: true to enable, false to disable
 */
static void aie_part_set_col_clkbuf(struct aie_partition *apart, u32 col,
				    bool enable)
{
	struct aie_device *adev = apart->adev;
	const struct aie_single_reg_field *col_clkbuf = adev->col_clkbuf;
	struct aie_location loc;
	u32 regoff, val;

	loc.row = 0;
	loc.col = col;

	val = aie_get_field_val(col_clkbuf, (enable ? 1 : 0));
	regoff = aie_cal_regoff(adev, loc, col_clkbuf->regoff);
	iowrite32(val, adev->base + regoff);
}

/**
 * aie_part_set_cols_reset() - set column reset of every column in a partition
 * @apart: AI engine partition
 * @reset: bool to assert reset, false to release reset
 */
static void aie_part_set_cols_reset(struct aie_partition *apart, bool reset)
{
	struct aie_range *range = &apart->range;
	u32 c;

	for (c = range->start.col; c < range->start.col + range->size.col;
	     c++)
		aie_part_set_col_reset(apart, c, reset);
}

/**
 * aie_part_set_cols_clkbuf() - set column clock buffer of every column in a
 *				partition
 * @apart: AI engine partition
 * @enable: true to enable, false to disable
 */
static void aie_part_set_cols_clkbuf(struct aie_partition *apart, bool enable)
{
	struct aie_range *range = &apart->range;
	u32 c;

	for (c = range->start.col; c < range->start.col + range->size.col;
	     c++)
		aie_part_set_col_clkbuf(apart, c, enable);
}

/**
 * aie_part_clear_mems() - clear memories of every tile in a partition
 * @apart: AI engine partition
 */
static void aie_part_clear_mems(struct aie_partition *apart)
{
	struct aie_device *adev = apart->adev;
	struct aie_part_mem *pmems = apart->pmems;
	u32 i, num_mems;

	/* Get the number of different types of memories */
	num_mems = adev->ops->get_mem_info(&apart->range, NULL);
	if (!num_mems)
		return;

	/* Clear each type of memories in the partition */
	for (i = 0; i < num_mems; i++) {
		struct aie_mem *mem = &pmems[i].mem;
		struct aie_range *range = &mem->range;
		u32 c, r;

		for (c = range->start.col;
		     c < range->start.col + range->size.col; c++) {
			for (r = range->start.row;
			     r < range->start.row + range->size.row; r++) {
				struct aie_location loc;
				u32 memoff;

				loc.col = c;
				loc.row = r;
				memoff = aie_cal_regoff(adev, loc, mem->offset);
				memset_io(adev->base + memoff, 0, mem->size);
			}
		}
	}
}

/**
 * aie_part_clean() - reset and clear AI engine partition
 * @apart: AI engine partition
 * @return: 0 for success and negative value for failure
 *
 * This function will:
 *  * reset AI engine partition columns
 *  * reset AI engine shims
 *  * clear the memories
 *  * gate all the tiles in a partition.
 *
 * This function will not validate the partition, the caller will need to
 * provide a valid AI engine partition.
 */
int aie_part_clean(struct aie_partition *apart)
{
	struct aie_device *adev = apart->adev;
	int ret;

	aie_part_set_cols_reset(apart, true);

	ret = apart->adev->ops->reset_shim(adev, &apart->range);
	if (ret < 0)
		return ret;

	aie_part_clear_mems(apart);
	aie_part_set_cols_clkbuf(apart, false);

	return 0;
}
