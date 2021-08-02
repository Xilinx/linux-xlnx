// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver.
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */
#include "ai-engine-internal.h"

/**
 * aie_sysfs_read_handler() - sysfs binary attribute read handler.
 * @filp: file pointer.
 * @kobj: pointer to the kobject.
 * @attr: sysfs binary attribute.
 * @buf: buffer to copy the data to.
 * @offset: offset into the sysfs file.
 * @max_size: maximum length of data that could be copied to buf.
 * @return: length of data actually copied to buf.
 */
ssize_t aie_sysfs_read_handler(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *attr, char *buf,
			       loff_t offset, size_t max_size)
{
	ssize_t len = max_size;
	struct aie_sysfs_prop *prop;

	prop = attr->private;
	if (!prop->data)
		return 0;

	if (!offset)
		prop->size = prop->read_callback(kobj, prop->data,
						 prop->max_size);

	if (offset >= prop->size)
		return 0;

	if (offset + max_size > prop->size)
		len = prop->size - offset;

	memcpy(buf,  prop->data + offset, len);
	return len;
}

/**
 * aie_sysfs_create_dev_attr() - dynamically allocates and initialize a device
 *				 attribute
 * @dev: device to allocate attribute for.
 * @attr: AI engine device attribute.
 * @return: pointer to the allocated device attribute.
 */
static struct device_attribute *
aie_sysfs_create_dev_attr(struct device *dev, const struct aie_dev_attr *attr)
{
	struct device_attribute *node;

	node = devm_kzalloc(dev, sizeof(struct device_attribute), GFP_KERNEL);
	if (!node)
		return ERR_PTR(-ENOMEM);

	sysfs_attr_init(&node->attr);

	node->attr.name = attr->name;
	node->attr.mode = attr->mode;
	node->show = attr->show;
	return node;
}

/**
 * aie_sysfs_create_bin_attr() - dynamically allocates and initialize a binary
 *				 attribute
 * @dev: device to allocate attribute for.
 * @attr: AI engine binary attribute.
 * @return: pointer to the allocated binary attribute.
 */
static struct bin_attribute *
aie_sysfs_create_bin_attr(struct device *dev, const struct aie_bin_attr *attr)
{
	struct bin_attribute *node;
	struct aie_sysfs_prop *prop;

	node = devm_kzalloc(dev, sizeof(struct bin_attribute), GFP_KERNEL);
	if (!node)
		return ERR_PTR(-ENOMEM);

	sysfs_bin_attr_init(node);

	node->attr.name = attr->name;
	node->attr.mode = attr->mode;
	node->size = attr->size;
	node->read = attr->read;

	prop = devm_kzalloc(dev, sizeof(struct aie_sysfs_prop), GFP_KERNEL);
	if (!prop)
		return ERR_PTR(-ENOMEM);

	prop->data = devm_kzalloc(dev, node->size, GFP_KERNEL);
	if (!prop->data)
		return ERR_PTR(-ENOMEM);

	prop->max_size = node->size;
	prop->read_callback = attr->read_callback;
	node->private = prop;
	return node;
}

/**
 * aie_tile_sysfs_create() - creates sysfs nodes at the tile level.
 * @atile: AI engine tile.
 * @return: 0 for success, error code for failure.
 */
static int aie_tile_sysfs_create(struct aie_tile *atile)
{
	struct attribute_group *attr_grp;
	struct bin_attribute **bin_attrs;
	struct attribute **dev_attrs;
	const struct aie_sysfs_attr *attr;
	int ret = 0;
	u32 ttype;
	u32 index, i = 0, j = 0;

	attr = atile->apart->adev->tile_sysfs_attr;
	ttype = atile->apart->adev->ops->get_tile_type(&atile->loc);

	if (attr->num_dev_attrs) {
		dev_attrs = devm_kzalloc(&atile->dev, sizeof(*dev_attrs) *
					 (attr->num_dev_attrs + 1), GFP_KERNEL);
		if (!dev_attrs)
			return -ENOMEM;

		for (index = 0; index < attr->num_dev_attrs; index++) {
			struct device_attribute *node;
			const struct aie_dev_attr *dev_attr;

			dev_attr = &attr->dev_attr[index];

			if (!(BIT(ttype) & attr->dev_attr[index].tile_type))
				continue;

			node = aie_sysfs_create_dev_attr(&atile->dev, dev_attr);
			if (IS_ERR_VALUE(node))
				return PTR_ERR(node);

			dev_attrs[i++] = &node->attr;
		}
	}

	if (attr->num_bin_attrs) {
		bin_attrs = devm_kzalloc(&atile->dev, sizeof(*bin_attrs) *
					 (attr->num_bin_attrs + 1), GFP_KERNEL);
		if (!bin_attrs)
			return -ENOMEM;

		for (index = 0; index < attr->num_bin_attrs; index++) {
			struct bin_attribute *node;
			const struct aie_bin_attr *bin_attr;

			bin_attr = &attr->bin_attr[index];

			if (!(BIT(ttype) & attr->bin_attr[index].tile_type))
				continue;

			node = aie_sysfs_create_bin_attr(&atile->dev, bin_attr);
			if (IS_ERR_VALUE(node))
				return PTR_ERR(node);

			bin_attrs[j++] = node;
		}
	}

	if (attr->num_dev_attrs || attr->num_bin_attrs) {
		attr_grp = devm_kzalloc(&atile->dev,
					sizeof(struct attribute_group),
					GFP_KERNEL);
		if (!attr_grp)
			return -ENOMEM;

		atile->attr_grp = attr_grp;

		if (attr->num_dev_attrs)
			attr_grp->attrs = dev_attrs;

		if (attr->num_bin_attrs)
			attr_grp->bin_attrs = bin_attrs;

		/* TODO - use the non managed api to create sysfs group.
		 * This workaround solves an issue where the device_del()
		 * removes the SYSFS files before the managed create group.
		 * This results in an error where files cannot be found.
		 */
		ret = sysfs_create_group(&atile->dev.kobj, attr_grp);
		if (ret) {
			dev_err(&atile->dev,
				"Failed to add sysfs attributes group\n");
		}
	}
	return ret;
}

/**
 * aie_part_sysfs_create() - creates sysfs nodes at the partition level.
 * @apart: AI engine partition.
 * @return: 0 for success, error code for failure.
 */
static int aie_part_sysfs_create(struct aie_partition *apart)
{
	const struct aie_sysfs_attr *attr;
	struct attribute_group *attr_grp;
	struct bin_attribute **bin_attrs;
	struct attribute **dev_attrs;
	int ret = 0;
	u32 index;

	attr = apart->adev->part_sysfs_attr;

	if (attr->num_dev_attrs) {
		dev_attrs = devm_kzalloc(&apart->dev, sizeof(*dev_attrs) *
					 (attr->num_dev_attrs + 1), GFP_KERNEL);
		if (!dev_attrs)
			return -ENOMEM;

		for (index = 0; index < attr->num_dev_attrs; index++) {
			struct device_attribute *node;
			const struct aie_dev_attr *dev_attr;

			dev_attr = &attr->dev_attr[index];

			node = aie_sysfs_create_dev_attr(&apart->dev, dev_attr);
			if (IS_ERR_VALUE(node))
				return PTR_ERR(node);

			dev_attrs[index] = &node->attr;
		}
	}

	if (attr->num_bin_attrs) {
		bin_attrs = devm_kzalloc(&apart->dev, sizeof(*bin_attrs) *
					 (attr->num_bin_attrs + 1), GFP_KERNEL);
		if (!bin_attrs)
			return -ENOMEM;

		for (index = 0; index < attr->num_bin_attrs; index++) {
			struct bin_attribute *node;
			const struct aie_bin_attr *bin_attr;

			bin_attr = &attr->bin_attr[index];

			node = aie_sysfs_create_bin_attr(&apart->dev, bin_attr);
			if (IS_ERR_VALUE(node))
				return PTR_ERR(node);

			bin_attrs[index] = node;
		}
	}

	if (attr->num_dev_attrs || attr->num_bin_attrs) {
		attr_grp = devm_kzalloc(&apart->dev,
					sizeof(struct attribute_group),
					GFP_KERNEL);
		if (!attr_grp)
			return -ENOMEM;

		apart->attr_grp = attr_grp;

		if (attr->num_dev_attrs)
			attr_grp->attrs = dev_attrs;

		if (attr->num_bin_attrs)
			attr_grp->bin_attrs = bin_attrs;

		/* TODO - use the non managed api to create sysfs group.
		 * This workaround solves an issue where the device_del()
		 * removes the SYSFS files before the managed create group.
		 * This results in an error where files cannot be found.
		 */
		ret = sysfs_create_group(&apart->dev.kobj, attr_grp);
		if (ret) {
			dev_err(&apart->dev,
				"Failed to add sysfs attributes group\n");
		}
	}
	return ret;
}

/**
 * aie_part_sysfs_create() - creates sysfs group for partition device.
 * @apart: AI engine partition.
 * @return: 0 for success, error code for failure.
 */
int aie_part_sysfs_create_entries(struct aie_partition *apart)
{
	int ret;

	ret = aie_part_sysfs_create(apart);
	if (ret < 0) {
		dev_err(&apart->dev, "Failed to create sysfs partition\n");
		return ret;
	}
	return ret;
}

/**
 * aie_tile_sysfs_create() - creates sysfs group for tile device.
 * @apart: AI engine partition.
 * @return: 0 for success, error code for failure.
 */
int aie_tile_sysfs_create_entries(struct aie_tile *atile)
{
	int ret;

	ret = aie_tile_sysfs_create(atile);
	if (ret < 0) {
		dev_err(&atile->dev, "Failed to create sysfs tile\n");
		return ret;
	}
	return ret;
}

/**
 * aie_part_sysfs_remove() - removes sysfs group from partition device.
 * @apart: AI engine partition.
 */
void aie_part_sysfs_remove_entries(struct aie_partition *apart)
{
	sysfs_remove_group(&apart->dev.kobj, apart->attr_grp);
}

/**
 * aie_tile_sysfs_remove() - removes sysfs group from tile device.
 * @atile: AI engine tile.
 */
void aie_tile_sysfs_remove_entries(struct aie_tile *atile)
{
	sysfs_remove_group(&atile->dev.kobj, atile->attr_grp);
}
