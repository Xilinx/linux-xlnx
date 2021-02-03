// SPDX-License-Identifier: GPL-2.0-only
/*
 * AI engine sysfs
 */

#include "ai-engine-internal.h"

/**
 * running_freq_show() - show the running frequency through sysfs
 *
 * @dev: device of AI engine partition
 * @attr: device attributes, not used
 * @buf: buffer for output
 * @return: 0 for success, negative value for failure
 */
static ssize_t running_freq_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	u64 freq;
	int ret;
	struct aie_partition *apart = dev_to_aiepart(dev);

	(void)attr;

	ret = aie_part_get_running_freq(apart, &freq);
	if (ret) {
		dev_err(&apart->dev, "failed to get running frequency.\n");
		return ret;
	}

	return sprintf(buf, "%llu\n", freq);
}
static DEVICE_ATTR_RO(running_freq);

static struct attribute *aie_freq_attrs[] = {
	&dev_attr_running_freq.attr,
	NULL
};

ATTRIBUTE_GROUPS(aie_freq);

/**
 * aie_part_sysfs_init() - initialize AI engine partition sysfs entries
 *
 * @apart: AI engine partition
 * @return: 0 for success, negative value for failure
 */
int aie_part_sysfs_init(struct aie_partition *apart)
{
	int ret;

	ret = devm_device_add_groups(&apart->dev, aie_freq_groups);
	if (ret)
		dev_err(&apart->dev,
			"Failed to add groups to device, error %d\n", ret);
	return ret;
}
