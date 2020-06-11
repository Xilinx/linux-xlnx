// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver sysfs for clock.
 *
 * Copyright (C) 2022 Xilinx, Inc.
 */
#include "ai-engine-internal.h"

/**
 * aie_aperture_show_hardware_info() - exports AI engine hardware information
 *
 * @dev: AI engine device.
 * @attr: sysfs device attribute.
 * @buffer: export buffer.
 * @return: length of string copied to buffer.
 */
ssize_t aie_aperture_show_hardware_info(struct device *dev,
					struct device_attribute *attr,
					char *buffer)
{
	struct aie_aperture *aperture = dev_to_aieaperture(dev);
	ssize_t len = 0, size = PAGE_SIZE;
	u32 num_rows[AIE_TILE_TYPE_MAX], start_rows[AIE_TILE_TYPE_MAX];
	u32 total_rows = 0, total_cols = 0;
	u32 version, ttype;

	if (mutex_lock_interruptible(&aperture->mlock))
		return 0;

	version = aperture->adev->dev_gen;
	len += scnprintf(&buffer[len], max(0L, size - len), "generation: ");
	switch (version) {
	case AIE_DEVICE_GEN_AIE:
		len += scnprintf(&buffer[len], max(0L, size - len), "aie\n");
		break;
	case AIE_DEVICE_GEN_AIEML:
		len += scnprintf(&buffer[len], max(0L, size - len), "aieml\n");
		break;
	default:
		len += scnprintf(&buffer[len], max(0L, size - len),
				 "invalid\n");
		memset(num_rows, 0, sizeof(num_rows));
		memset(start_rows, 0, sizeof(start_rows));
		goto exit;
	}

	total_cols = aperture->range.size.col;
	for (ttype = 0; ttype < AIE_TILE_TYPE_MAX; ttype++) {
		if (ttype == AIE_TILE_TYPE_SHIMNOC)
			continue;
		num_rows[ttype] = aperture->adev->ttype_attr[ttype].num_rows;
		start_rows[ttype] = aperture->adev->ttype_attr[ttype].start_row;
		total_rows += num_rows[ttype];
	}

exit:
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "total_cols: %d\n", total_cols);
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "total_rows: %d\n", total_rows);

	len += scnprintf(&buffer[len], max(0L, size - len),
			 "shim_tile: start row: %d%snum_rows: %d\n",
			 start_rows[AIE_TILE_TYPE_SHIMPL], DELIMITER_LEVEL1,
			 num_rows[AIE_TILE_TYPE_SHIMPL]);

	len += scnprintf(&buffer[len], max(0L, size - len),
			 "memory_tile: start row: %d%snum_rows: %d\n",
			 start_rows[AIE_TILE_TYPE_MEMORY], DELIMITER_LEVEL1,
			 num_rows[AIE_TILE_TYPE_MEMORY]);

	len += scnprintf(&buffer[len], max(0L, size - len),
			 "aie_tile: start row: %d%snum_rows: %d\n",
			 start_rows[AIE_TILE_TYPE_TILE], DELIMITER_LEVEL1,
			 num_rows[AIE_TILE_TYPE_TILE]);

	mutex_unlock(&aperture->mlock);
	return len;
}
