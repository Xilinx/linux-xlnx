// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver.
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */
#include "ai-engine-internal.h"

/**
 * aie_get_lock_status() - reads the lock status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @return: 32-bit register value.
 */
static u32 aie_get_lock_status(struct aie_partition *apart,
			       struct aie_location *loc)
{
	u32 ttype, stsoff, regoff;

	ttype = apart->adev->ops->get_tile_type(loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		stsoff = apart->adev->pl_lock->sts_regoff;
	else
		stsoff = apart->adev->mem_lock->sts_regoff;
	regoff = aie_cal_regoff(apart->adev, *loc, stsoff);
	return ioread32(apart->adev->base + regoff);
}

/**
 * aie_get_lock_status_str() - returns the string value corresponding to
 *			       lock status value.
 * @apart: AI engine partition.
 * @loc: location of AI engine lock.
 * @status: status value of lock.
 * @lock: lock ID.
 * @buffer: location to return lock status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
static ssize_t aie_get_lock_status_str(struct aie_partition *apart,
				       struct aie_location *loc, u32 status,
				       u32 lock, char *buffer, ssize_t size)
{
	char **str = apart->adev->lock_status_str;
	u32 ttype, mask;
	u8 value, shift;

	ttype = apart->adev->ops->get_tile_type(loc);
	if (ttype != AIE_TILE_TYPE_TILE) {
		shift = lock * apart->adev->pl_lock->sts.regoff;
		mask = (apart->adev->pl_lock->sts.mask) << shift;
	} else {
		shift = lock * apart->adev->mem_lock->sts.regoff;
		mask = (apart->adev->mem_lock->sts.mask) << shift;
	}

	value = (status & mask) >> shift;
	return scnprintf(buffer, max(0L, size), str[value]);
}

/**
 * aie_tile_show_lock() - exports AI engine lock status to a tile level sysfs
 *			  node.
 * @dev: AI engine tile device.
 * @attr: sysfs device attribute.
 * @buffer: export buffer.
 * @return: length of string copied to buffer.
 */
ssize_t aie_tile_show_lock(struct device *dev, struct device_attribute *attr,
			   char *buffer)
{
	struct aie_tile *atile = container_of(dev, struct aie_tile, dev);
	struct aie_partition *apart = atile->apart;
	ssize_t len = 0, size = PAGE_SIZE;
	unsigned long status;
	u32 ttype, i, num_locks;

	if (mutex_lock_interruptible(&apart->mlock)) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return len;
	}

	ttype = apart->adev->ops->get_tile_type(&atile->loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		num_locks = apart->adev->pl_lock->num_locks;
	else
		num_locks = apart->adev->mem_lock->num_locks;

	if (!aie_part_check_clk_enable_loc(apart, &atile->loc)) {
		for (i = 0; i < num_locks; i++) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%d: clock_gated\n", i);
		}
		mutex_unlock(&apart->mlock);
		return len;
	}

	status = aie_get_lock_status(apart, &atile->loc);
	for (i = 0; i < num_locks; i++) {
		len += scnprintf(&buffer[len], max(0L, size - len), "%d: ", i);
		len += aie_get_lock_status_str(apart, &atile->loc, status, i,
					       &buffer[len], size - len);
		len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	}
	mutex_unlock(&apart->mlock);
	return len;
}
