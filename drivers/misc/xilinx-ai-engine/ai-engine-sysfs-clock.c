// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver sysfs for clock.
 *
 * Copyright (C) 2022 Xilinx, Inc.
 */
#include "ai-engine-internal.h"

/**
 * aie_part_show_current_freq() - exports AI engine partition current frequency
 *
 * @dev: AI engine tile device.
 * @attr: sysfs device attribute.
 * @buffer: export buffer.
 * @return: length of string copied to buffer.
 */
ssize_t aie_part_show_current_freq(struct device *dev,
				   struct device_attribute *attr, char *buffer)
{
	struct aie_partition *apart = dev_to_aiepart(dev);
	int ret = 0;
	u64 freq;

	if (mutex_lock_interruptible(&apart->mlock))
		return ret;

	ret = aie_part_get_freq(apart, &freq);
	if (ret) {
		dev_err(dev, "Failed to get partition frequency.\n");
		mutex_unlock(&apart->mlock);
		return ret;
	}

	mutex_unlock(&apart->mlock);

	return scnprintf(buffer, PAGE_SIZE, "%llu\n", freq);
}
