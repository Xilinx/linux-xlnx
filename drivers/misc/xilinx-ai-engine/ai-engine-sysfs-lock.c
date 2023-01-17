// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver.
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */
#include "ai-engine-internal.h"

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

	if (mutex_lock_interruptible(&apart->mlock)) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return len;
	}

	len = apart->adev->ops->get_tile_sysfs_lock_status(apart, &atile->loc,
							   &buffer[len],
							   size - len);

	mutex_unlock(&apart->mlock);
	return len;
}

/**
 * aie_part_read_cb_lock() - exports status of all lock modules within a given
 *			     partition to partition level node.
 * @kobj: kobject used to create sysfs node.
 * @buffer: export buffer.
 * @size: length of export buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie_part_read_cb_lock(struct kobject *kobj, char *buffer, ssize_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct aie_partition *apart = dev_to_aiepart(dev);
	struct aie_tile *atile = apart->atiles;
	const struct aie_tile_operations *ops = apart->adev->ops;
	ssize_t len = 0;
	u32 index;

	if (mutex_lock_interruptible(&apart->mlock)) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return len;
	}

	for (index = 0; index < apart->range.size.col * apart->range.size.row;
	     index++, atile++) {
		u32 ttype = ops->get_tile_type(apart->adev, &atile->loc);

		if (ttype == AIE_TILE_TYPE_SHIMPL)
			continue;

		len += scnprintf(&buffer[len], max(0L, size - len), "%d_%d: ",
				 atile->loc.col, atile->loc.row);
		len += ops->get_part_sysfs_lock_status(apart, &atile->loc,
						       &buffer[len],
						       size - len);
		len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	}

	mutex_unlock(&apart->mlock);
	return len;
}
