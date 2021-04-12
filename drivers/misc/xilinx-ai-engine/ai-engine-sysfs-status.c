// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine driver AIE device specific implementation
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */
#include <linux/slab.h>

#include "ai-engine-internal.h"

/**
 * aie_part_read_cb_status() - exports status of cores, DMAs, errors, and locks
 *			       within a partition at a partition level node.
 *			       this node serves as a single access point to
 *			       query the status of a partition by a
 *			       script/tool. For a given tile location, core
 *			       status, DMAs, etc are separated by a ';' symbol.
 *			       Core status information is captured under 'cs'
 *			       label, DMA under 'ds', errors under 'es', and
 *			       lock status under 'ls'.
 * @kobj: kobject used to create sysfs node.
 * @buffer: export buffer.
 * @size: length of export buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie_part_read_cb_status(struct kobject *kobj, char *buffer,
				ssize_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct aie_partition *apart = dev_to_aiepart(dev);
	struct aie_tile *atile = apart->atiles;
	ssize_t len = 0;
	size_t temp_size = AIE_SYSFS_CORE_STS_SIZE + AIE_SYSFS_CHAN_STS_SIZE +
			   AIE_SYSFS_ERROR_CATEGORY_SIZE +
			   AIE_SYSFS_LOCK_STS_SIZE;
	char *cs_buf, *ds_buf, *es_buf, *ls_buf;
	u32 index;

	if (mutex_lock_interruptible(&apart->mlock)) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return len;
	}

	cs_buf = kmalloc(temp_size, GFP_KERNEL);
	if (!cs_buf) {
		mutex_unlock(&apart->mlock);
		return len;
	}

	ds_buf = cs_buf + AIE_SYSFS_CORE_STS_SIZE;
	es_buf = ds_buf + AIE_SYSFS_CHAN_STS_SIZE;
	ls_buf = es_buf + AIE_SYSFS_ERROR_CATEGORY_SIZE;

	for (index = 0; index < apart->range.size.col * apart->range.size.row;
	     index++, atile++) {
		u32 cs = 0, ds = 0, es = 0, ls = 0;

		cs = aie_sysfs_get_core_status(apart, &atile->loc, cs_buf,
					       AIE_SYSFS_CORE_STS_SIZE);
		ds = aie_sysfs_get_dma_status(apart, &atile->loc, ds_buf,
					      AIE_SYSFS_CHAN_STS_SIZE);
		es = aie_sysfs_get_errors(apart, &atile->loc, es_buf,
					  AIE_SYSFS_ERROR_CATEGORY_SIZE);
		ls = aie_sysfs_get_lock_status(apart, &atile->loc, ls_buf,
					       AIE_SYSFS_LOCK_STS_SIZE);

		if (!(cs || ds || es || ls))
			continue;

		len += scnprintf(&buffer[len], max(0L, size - len), "%d_%d: ",
				 atile->loc.col, atile->loc.row);

		if (cs) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "cs: %s", cs_buf);
		}

		if (ds) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%sds: %s", cs ?
					 DELIMITER_LEVEL2 : "", ds_buf);
		}

		if (es) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%ses: %s", (cs || ds) ?
					 DELIMITER_LEVEL2 : "", es_buf);
		}

		if (ls) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%sls: %s", (cs || ds || es) ?
					 DELIMITER_LEVEL2 : "", ls_buf);
		}

		len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	}

	mutex_unlock(&apart->mlock);
	kfree(cs_buf);
	return len;
}
