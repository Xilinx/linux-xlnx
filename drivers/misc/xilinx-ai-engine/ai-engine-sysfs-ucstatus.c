// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine driver AIE-2PS device specific implementation
 *
 * Copyright (C) 2023 AMD, Inc.
 */

#include <linux/slab.h>

#include "ai-engine-internal.h"

/**
 * aie_part_read_cb_ucstatus() - exports status of cores, DMAs, errors, and locks
 *                             within a partition at a partition level node.
 *                             this node serves as a single access point to
 *                             query the status of a partition by a
 *                             script/tool. For a given tile location, core
 *                             status, DMAs, etc are separated by a ';' symbol.
 *                             Core status information is captured under 'cs'
 *                             label, DMA under 'ds', errors under 'es', and
 *                             lock status under 'ls'.
 * @kobj: kobject used to create sysfs node.
 * @buffer: export buffer.
 * @size: length of export buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie_part_read_cb_ucstatus(struct kobject *kobj, char *buffer,
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
		const struct aie_tile_operations *ops = apart->adev->ops;
		bool preamble = true;
		u32 ttype;

		ttype = ops->get_tile_type(apart->adev, &loc);

		if (ttype == AIE_TILE_TYPE_SHIMNOC) {
			if (preamble) {
				len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "%d_%d: cs: ", loc.col,
						 loc.row);
				preamble = false;
			}

			len += aie2ps_sysfs_get_uc_core_status(apart, &loc,
							 &buffer[len],
							 size - len);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL2);

			len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "cis: ");
			len += aie2ps_sysfs_get_uc_core_intr(apart, &loc,
							 &buffer[len],
							 size - len);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL2);

			len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "dcs: ");
			len += aie2ps_sysfs_get_uc_mdm_dbg_sts(apart, &loc,
							 &buffer[len],
							 size - len);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL2);

			len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "dm2mm: ");
			len += aie2ps_sysfs_get_uc_dma_dm2mm_sts(apart, &loc,
							 &buffer[len],
							 size - len);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL2);

			len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "mm2dm: ");
			len += aie2ps_sysfs_get_uc_dma_mm2dm_sts(apart, &loc,
							 &buffer[len],
							 size - len);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL2);

			len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "axmm: ");
			len += aie2ps_sysfs_get_uc_mod_aximm(apart, &loc,
							 &buffer[len],
							 size - len);

			len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 "axmm_ot: ");
			len += aie2ps_sysfs_get_uc_mod_aximm_out_trans(apart, &loc,
							 &buffer[len],
							 size - len);
		}
	}
	mutex_unlock(&apart->mlock);
	return len;
}
