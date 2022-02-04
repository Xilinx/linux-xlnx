// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include "ai-engine-internal.h"
#include <linux/export.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/xlnx-ai-engine.h>

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
	if (apart->adev->ops->get_tile_type(apart->adev, loc) !=
			AIE_TILE_TYPE_TILE)
		return -EINVAL;

	return (loc->col - apart->range.start.col) *
	       (apart->range.size.row - 1) + loc->row - 1;
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

	if (apart->adev->ops->get_tile_type(apart->adev, loc) !=
			AIE_TILE_TYPE_TILE)
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

/**
 * aie_aperture_get_freq_req() - get current required frequency of aperture
 * @aperture: AI engine aperture
 * @return: required clock frequency of the aperture which is the largest
 *	    required clock frequency of all partitions of the aperture. If
 *	    return value is 0, it means no partition has specific frequency
 *	    requirement.
 */
static unsigned long aie_aperture_get_freq_req(struct aie_aperture *aperture)
{
	struct aie_partition *apart;
	unsigned long freq_req = 0;
	int ret;

	ret = mutex_lock_interruptible(&aperture->mlock);
	if (ret)
		return freq_req;

	list_for_each_entry(apart, &aperture->partitions, node) {
		if (apart->freq_req > freq_req)
			freq_req = apart->freq_req;
	}

	mutex_unlock(&aperture->mlock);

	return freq_req;
}

/**
 * aie_part_set_freq() - set frequency requirement of an AI engine partition
 *
 * @apart: AI engine partition
 * @freq: required frequency
 * @return: 0 for success, negative value for failure
 *
 * This function sets frequency requirement for the partition.
 * It will call aie_dev_set_freq() to check the frequency requirements
 * of all partitions. it will send QoS EEMI request to request the max
 * frequency of all the partitions.
 */
int aie_part_set_freq(struct aie_partition *apart, u64 freq)
{
	struct aie_device *adev = apart->adev;
	struct aie_aperture *aperture = apart->aperture;
	unsigned long clk_rate;
	u32 boot_qos, current_qos, target_qos;
	int ret;

	clk_rate = clk_get_rate(adev->clk);
	if (freq > (u64)clk_rate) {
		dev_err(&apart->dev,
			"Invalid frequency to set, larger than full frequency(%lu).\n",
			clk_rate);
		return -EINVAL;
	}

	apart->freq_req = freq;

	freq = aie_aperture_get_freq_req(aperture);
	if (!freq)
		return 0;

	ret = zynqmp_pm_get_qos(aperture->node_id, &boot_qos, &current_qos);
	if (ret < 0) {
		dev_err(&apart->dev, "Failed to get clock divider value.\n");
		return -EINVAL;
	}

	target_qos = (boot_qos * freq) / clk_rate;
	ret = zynqmp_pm_set_requirement(aperture->node_id,
					ZYNQMP_PM_CAPABILITY_ACCESS, target_qos,
					ZYNQMP_PM_REQUEST_ACK_BLOCKING);
	if (ret < 0)
		dev_err(&apart->dev, "Failed to set frequency requirement.\n");

	return ret;
}

/**
 * aie_partition_set_freq_req() - set partition frequency requirement
 *
 * @dev: AI engine partition device
 * @freq: required frequency
 * @return: 0 for success, negative value for failure
 *
 * This function sets the minimum required frequency for the AI engine
 * partition. If there are other partitions requiring a higher frequency in the
 * system, AI engine device will be clocked at that value to satisfy frequency
 * requirements of all partitions.
 */
int aie_partition_set_freq_req(struct device *dev, u64 freq)
{
	struct aie_partition *apart;

	if (!dev)
		return -EINVAL;

	apart = dev_to_aiepart(dev);
	return aie_part_set_freq(apart, freq);
}
EXPORT_SYMBOL_GPL(aie_partition_set_freq_req);

/**
 * aie_part_get_freq() - get running frequency of AI engine device.
 *
 * @apart: AI engine partition
 * @freq: return running frequency
 * @return: 0 for success, negative value for failure
 *
 * This function gets clock divider value with EEMI requests, and it gets the
 * full clock frequency from common clock framework. And then it divides the
 * full clock frequency by the divider value and returns the result.
 */
int aie_part_get_freq(struct aie_partition *apart, u64 *freq)
{
	unsigned long clk_rate;
	struct aie_device *adev = apart->adev;
	u32 boot_qos, current_qos;
	int ret;

	if (!freq)
		return -EINVAL;

	clk_rate = clk_get_rate(adev->clk);
	ret = zynqmp_pm_get_qos(apart->aperture->node_id, &boot_qos,
				&current_qos);
	if (ret < 0) {
		dev_err(&apart->dev, "Failed to get clock divider value.\n");
		return ret;
	}

	*freq = (clk_rate * current_qos) / boot_qos;
	return 0;
}

/**
 * aie_partition_get_freq() - get partition running frequency
 *
 * @dev: AI engine partition device
 * @freq: return running frequency
 * @return: 0 for success, negative value for failure
 */
int aie_partition_get_freq(struct device *dev, u64 *freq)
{
	struct aie_partition *apart;

	if (!dev)
		return -EINVAL;

	apart = dev_to_aiepart(dev);
	return aie_part_get_freq(apart, freq);
}
EXPORT_SYMBOL_GPL(aie_partition_get_freq);

/**
 * aie_partition_get_freq_req() - get partition required frequency
 *
 * @dev: AI engine partition device
 * @freq: return partition required frequency. 0 means partition doesn't
 *	  have frequency requirement.
 * @return: 0 for success, negative value for failure
 */
int aie_partition_get_freq_req(struct device *dev, u64 *freq)
{
	struct aie_partition *apart;

	if (!dev)
		return -EINVAL;

	apart = dev_to_aiepart(dev);
	*freq = apart->freq_req;

	return 0;
}
EXPORT_SYMBOL_GPL(aie_partition_get_freq_req);
