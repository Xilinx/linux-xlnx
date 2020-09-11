// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include "ai-engine-internal.h"
#include <linux/slab.h>
#include <linux/uaccess.h>

/**
 * aie_part_get_clk_state_bit() - return bit position of the clock state of a
 *				  tile
 * @apart: AI engine partition
 * @loc: AI engine tile location
 * @return: bit position for success, negative value for failure
 */
static int aie_part_get_clk_state_bit(struct aie_partition *apart,
				      struct aie_location *loc)
{
	if (apart->adev->ops->get_tile_type(loc) != AIE_TILE_TYPE_TILE)
		return -EINVAL;

	return loc->col * (apart->range.size.row - 1) + loc->row - 1;
}

/**
 * aie_part_scan_clk_state() - scan the clock states of tiles of the AI engine
 *			       partition
 * @apart: AI engine partition
 * @return: 0 for success, negative value for failure.
 *
 * This function will scan the clock status of both the memory and core
 * modules.
 */
int aie_part_scan_clk_state(struct aie_partition *apart)
{
	return apart->adev->ops->scan_part_clocks(apart);
}

/**
 * aie_part_check_clk_enable_loc() - return if clock of a tile is enabled
 * @apart: AI engine partition
 * @loc: AI engine tile location
 * @return: true for enabled, false for disabled
 */
bool aie_part_check_clk_enable_loc(struct aie_partition *apart,
				   struct aie_location *loc)
{
	int bit;

	if (apart->adev->ops->get_tile_type(loc) != AIE_TILE_TYPE_TILE)
		return true;

	bit = aie_part_get_clk_state_bit(apart, loc);
	return aie_resource_testbit(&apart->cores_clk_state, bit);
}

/**
 * aie_part_request_tiles() - request tiles from an AI engine partition.
 * @apart: AI engine partition
 * @num_tiles: number of tiles to request. If it is 0, it means all tiles
 * @locs: the AI engine tiles locations array which will be requested
 * @return: 0 for success, negative value for failure.
 *
 * This function will enable clocks of the specified tiles.
 */
static int aie_part_request_tiles(struct aie_partition *apart, int num_tiles,
				  struct aie_location *locs)
{
	if (num_tiles == 0) {
		aie_resource_set(&apart->tiles_inuse, 0,
				 apart->tiles_inuse.total);
	} else {
		u32 n;

		if (!locs)
			return -EINVAL;

		for (n = 0; n < num_tiles; n++) {
			int bit = aie_part_get_clk_state_bit(apart, &locs[n]);

			if (bit >= 0)
				aie_resource_set(&apart->tiles_inuse, bit, 1);
		}
	}

	return apart->adev->ops->set_part_clocks(apart);
}

/**
 * aie_part_release_tiles() - release tiles from an AI engine partition.
 * @apart: AI engine partition
 * @num_tiles: number of tiles to release. If it is 0, it means all tiles
 * @locs: the AI engine tiles locations array which will be released
 * @return: 0 for success, negative value for failure.
 *
 * This function will disable clocks of the specified tiles.
 */
static int aie_part_release_tiles(struct aie_partition *apart, int num_tiles,
				  struct aie_location *locs)
{
	if (num_tiles == 0) {
		aie_resource_clear(&apart->tiles_inuse, 0,
				   apart->tiles_inuse.total);
	} else {
		u32 n;

		if (!locs)
			return -EINVAL;

		for (n = 0; n < num_tiles; n++) {
			int bit = aie_part_get_clk_state_bit(apart, &locs[n]);

			if (bit >= 0)
				aie_resource_clear(&apart->tiles_inuse, bit, 1);
		}
	}

	return apart->adev->ops->set_part_clocks(apart);
}

/**
 * aie_part_request_tiles_from_user() - request tiles from an AI engine
 *					partition from user
 * @apart: AI engine partition
 * @user_args: user AI engine request tiles argument
 * @return: 0 for success, negative value for failure.
 *
 * This function will request tiles from user request.
 */
int aie_part_request_tiles_from_user(struct aie_partition *apart,
				     void __user *user_args)
{
	struct aie_tiles_array args;
	struct aie_location *locs = NULL;
	int ret;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	if (args.num_tiles) {
		u32 i;

		locs = kmalloc_array(args.num_tiles, sizeof(*locs),
				     GFP_KERNEL);
		if (!locs)
			return -ENOMEM;

		if (copy_from_user(locs, (void __user *)args.locs,
				   args.num_tiles * sizeof(*locs))) {
			kfree(locs);
			return -EFAULT;
		}

		/* update the location to absolute location */
		for (i = 0; i < args.num_tiles; i++) {
			if (locs[i].col > apart->range.size.col ||
			    locs[i].row > apart->range.size.row) {
				dev_err(&apart->dev,
					"failed to request tiles, invalid tile(%u,%u).\n",
					locs[i].col, locs[i].row);
				kfree(locs);
				return -EINVAL;
			}
			locs[i].col += apart->range.start.col;
			locs[i].row += apart->range.start.row;
		}
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		kfree(locs);
		return ret;
	}

	ret = aie_part_request_tiles(apart, args.num_tiles, locs);
	mutex_unlock(&apart->mlock);

	kfree(locs);
	return ret;
}

/**
 * aie_part_release_tiles_from_user() - release tiles from an AI engine
 *					partition from user
 * @apart: AI engine partition
 * @user_args: user AI engine request tiles argument
 * @return: 0 for success, negative value for failure.
 *
 * This function will release tiles from user request.
 */
int aie_part_release_tiles_from_user(struct aie_partition *apart,
				     void __user *user_args)
{
	struct aie_tiles_array args;
	struct aie_location *locs = NULL;
	int ret;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	if (args.num_tiles) {
		int i;

		locs = kmalloc_array(args.num_tiles, sizeof(*locs),
				     GFP_KERNEL);
		if (!locs)
			return -ENOMEM;

		if (copy_from_user(locs, (void __user *)args.locs,
				   args.num_tiles * sizeof(*locs))) {
			kfree(locs);
			return -EFAULT;
		}

		/* update the location to absolute location */
		for (i = 0; i < args.num_tiles; i++) {
			if (locs[i].col > apart->range.size.col ||
			    locs[i].row > apart->range.size.row) {
				dev_err(&apart->dev,
					"failed to release tiles, invalid tile(%u,%u).\n",
					locs[i].col, locs[i].row);
				kfree(locs);
				return -EINVAL;
			}
			locs[i].col += apart->range.start.col;
			locs[i].row += apart->range.start.row;
		}
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		kfree(locs);
		return ret;
	}

	ret = aie_part_release_tiles(apart, args.num_tiles, locs);
	mutex_unlock(&apart->mlock);

	kfree(locs);
	return ret;
}
