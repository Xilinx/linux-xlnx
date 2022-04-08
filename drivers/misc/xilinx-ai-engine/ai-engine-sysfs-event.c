// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver.
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */
#include "ai-engine-internal.h"

/**
 * aie_tile_print_event() - formats events strings from each module into a
 *			    single buffer.
 * @atile: AI engine tile.
 * @buffer: export buffer.
 * @core: core module event string
 * @mem: memory module event string
 * @pl: pl module event string
 * @return: length of string copied to buffer.
 */
static ssize_t aie_tile_print_event(struct aie_tile *atile, char *buffer,
				    char *core, char *mem, char *pl)
{
	ssize_t len = 0, size = PAGE_SIZE;
	u32 ttype;

	ttype = atile->apart->adev->ops->get_tile_type(atile->apart->adev,
						       &atile->loc);
	if (ttype == AIE_TILE_TYPE_TILE) {
		len += scnprintf(&buffer[len], max(0L, size - len),
				 "core: %s\n", core);
		len += scnprintf(&buffer[len], max(0L, size - len),
				 "memory: %s\n", mem);
	} else {
		len += scnprintf(&buffer[len], max(0L, size - len), "pl: %s\n",
				 pl);
	}
	return len;
}

/**
 * aie_tile_show_event() - exports all active events in a given tile to a
 *			   tile level sysfs node.
 * @dev: AI engine tile device.
 * @attr: sysfs device attribute.
 * @buffer: export buffer.
 * @return: length of string copied to buffer.
 */
ssize_t aie_tile_show_event(struct device *dev, struct device_attribute *attr,
			    char *buffer)
{
	struct aie_tile *atile = container_of(dev, struct aie_tile, dev);
	struct aie_partition *apart = atile->apart;
	ssize_t l = 0;
	unsigned long cs[4] = {0}, ms[4] = {0}, ps[4] = {0};
	u32 ttype, n;
	char core_buf[AIE_SYSFS_EVENT_STS_SIZE],
	     mem_buf[AIE_SYSFS_EVENT_STS_SIZE],
	     pl_buf[AIE_SYSFS_EVENT_STS_SIZE];
	bool is_delimit_req = false;

	/*
	 * Initialize local buffers to avoid garbage data being returned to the
	 * export buffer.
	 */
	core_buf[0] = '\0';
	mem_buf[0] = '\0';
	pl_buf[0] = '\0';

	if (mutex_lock_interruptible(&apart->mlock)) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return 0;
	}

	ttype = apart->adev->ops->get_tile_type(apart->adev, &atile->loc);

	if (!aie_part_check_clk_enable_loc(apart, &atile->loc)) {
		mutex_unlock(&apart->mlock);
		return aie_tile_print_event(atile, buffer, "clock_gated",
					    "clock_gated", "clock_gated");
	}

	if (ttype == AIE_TILE_TYPE_TILE) {
		aie_read_event_status(apart, &atile->loc, AIE_CORE_MOD,
				      (u32 *)cs);
		aie_read_event_status(apart, &atile->loc, AIE_MEM_MOD,
				      (u32 *)ms);
	} else {
		aie_read_event_status(apart, &atile->loc, AIE_PL_MOD,
				      (u32 *)ps);
	}

	for_each_set_bit(n, cs, 128) {
		if (is_delimit_req) {
			l += scnprintf(&core_buf[l],
				       max(0L, AIE_SYSFS_EVENT_STS_SIZE - l),
				       DELIMITER_LEVEL0);
		}

		l += scnprintf(&core_buf[l],
			       max(0L, AIE_SYSFS_EVENT_STS_SIZE - l), "%d", n);
		is_delimit_req = true;
	}

	l = 0;
	is_delimit_req = false;
	for_each_set_bit(n, ms, 128) {
		if (is_delimit_req) {
			l += scnprintf(&mem_buf[l],
				       max(0L, AIE_SYSFS_EVENT_STS_SIZE - l),
				       DELIMITER_LEVEL0);
		}

		l += scnprintf(&mem_buf[l],
			       max(0L, AIE_SYSFS_EVENT_STS_SIZE - l), "%d", n);
		is_delimit_req = true;
	}

	l = 0;
	is_delimit_req = false;
	for_each_set_bit(n, ps, 128) {
		if (is_delimit_req) {
			l += scnprintf(&pl_buf[l],
				       max(0L, AIE_SYSFS_EVENT_STS_SIZE - l),
				       DELIMITER_LEVEL0);
		}

		l += scnprintf(&pl_buf[l],
			       max(0L, AIE_SYSFS_EVENT_STS_SIZE - l), "%d", n);
		is_delimit_req = true;
	}

	mutex_unlock(&apart->mlock);
	return aie_tile_print_event(atile, buffer, core_buf, mem_buf, pl_buf);
}
