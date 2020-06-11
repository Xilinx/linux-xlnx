// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver.
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */
#include "ai-engine-internal.h"

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
	ssize_t len = 0, size = PAGE_SIZE;
	unsigned long cs[AIE_NUM_EVENT_STS_CORETILE] = {0},
		      ms[AIE_NUM_EVENT_STS_MEMTILE] = {0},
		      ps[AIE_NUM_EVENT_STS_SHIMTILE] = {0};
	u32 ttype, n;
	bool is_delimit_req = false;

	if (mutex_lock_interruptible(&apart->mlock)) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return 0;
	}

	ttype = apart->adev->ops->get_tile_type(apart->adev, &atile->loc);

	if (ttype == AIE_TILE_TYPE_TILE) {
		if (!aie_part_check_clk_enable_loc(apart, &atile->loc)) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "core: clock_gated\n");
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "memory: clock_gated\n");
			goto exit;
		}

		aie_read_event_status(apart, &atile->loc, AIE_CORE_MOD,
				      (u32 *)cs);
		aie_read_event_status(apart, &atile->loc, AIE_MEM_MOD,
				      (u32 *)ms);

		len += scnprintf(&buffer[len], max(0L, size - len), "core: ");

		for_each_set_bit(n, cs, apart->adev->core_events->num_events) {
			if (is_delimit_req) {
				len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 DELIMITER_LEVEL0);
			}

			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%d", n);
			is_delimit_req = true;
		}

		len += scnprintf(&buffer[len], max(0L, size - len),
				 "\nmemory: ");

		is_delimit_req = false;
		for_each_set_bit(n, ms, apart->adev->mem_events->num_events) {
			if (is_delimit_req) {
				len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 DELIMITER_LEVEL0);
			}

			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%d", n);
			is_delimit_req = true;
		}

		len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		if (!aie_part_check_clk_enable_loc(apart, &atile->loc)) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "memory: clock_gated\n");
			goto exit;
		}

		aie_read_event_status(apart, &atile->loc, AIE_MEM_MOD,
				      (u32 *)ms);

		len += scnprintf(&buffer[len], max(0L, size - len),
				 "memory_tile: ");

		is_delimit_req = false;
		for_each_set_bit(n, ms,
				 apart->adev->memtile_events->num_events) {
			if (is_delimit_req) {
				len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 DELIMITER_LEVEL0);
			}

			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%d", n);
			is_delimit_req = true;
		}

		len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	} else {
		aie_read_event_status(apart, &atile->loc, AIE_PL_MOD,
				      (u32 *)ps);

		len += scnprintf(&buffer[len], max(0L, size - len), "pl: ");

		for_each_set_bit(n, ps, apart->adev->pl_events->num_events) {
			if (is_delimit_req) {
				len += scnprintf(&buffer[len],
						 max(0L, size - len),
						 DELIMITER_LEVEL0);
			}

			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%d", n);
			is_delimit_req = true;
		}

		len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	}

exit:
	mutex_unlock(&apart->mlock);
	return len;
}
