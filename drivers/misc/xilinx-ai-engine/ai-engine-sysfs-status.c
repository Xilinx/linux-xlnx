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
	u32 index;

	if (mutex_lock_interruptible(&apart->mlock)) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return len;
	}

	for (index = 0; index < apart->range.size.col * apart->range.size.row;
	     index++, atile++) {
		struct aie_location loc = atile->loc;
		bool preamble = true;
		u32 ttype;

		ttype = apart->adev->ops->get_tile_type(apart->adev, &loc);

		if (ttype == AIE_TILE_TYPE_TILE) {
			if (preamble) {
				len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "%d_%d: cs: ", loc.col,
						 loc.row);
				preamble = false;
			}

			len += aie_sysfs_get_core_status(apart, &loc,
							 &buffer[len], size);
		}

		if (ttype == AIE_TILE_TYPE_TILE ||
		    ttype == AIE_TILE_TYPE_SHIMNOC) {
			if (preamble) {
				len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "%d_%d: ds: ", loc.col,
						 loc.row);
				preamble = false;
			} else {
				len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "%sds: ", DELIMITER_LEVEL2);
			}

			len += aie_sysfs_get_dma_status(apart, &loc,
							&buffer[len], size);

			if (preamble) {
				len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "%d_%d: ls: ", loc.col,
						 loc.row);
				preamble = false;
			} else {
				len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "%sls: ", DELIMITER_LEVEL2);
			}

			len += aie_sysfs_get_lock_status(apart, &loc,
							 &buffer[len], size);
		}

		if (aie_check_tile_error(apart, loc)) {
			if (preamble) {
				len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "%d_%d: es: ", loc.col,
						 loc.row);
				preamble = false;
			} else {
				len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "%ses: ", DELIMITER_LEVEL2);
			}

			len += aie_sysfs_get_errors(apart, &loc, &buffer[len],
						    size);
		}

		if (!preamble) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "\n");
		}
	}

	mutex_unlock(&apart->mlock);
	return len;
}
