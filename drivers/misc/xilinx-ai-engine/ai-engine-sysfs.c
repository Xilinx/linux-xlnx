// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver.
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */
#include "ai-engine-internal.h"

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

	sysfs_attr_init(node);

	node->attr.name = attr->name;
	node->attr.mode = attr->mode;
	node->show = attr->show;
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
	struct attribute **dev_attrs;
	const struct aie_sysfs_attr *attr;
	int ret = 0;
	u32 index, i = 0;

	attr = atile->apart->adev->tile_sysfs_attr;

	if (attr->num_dev_attrs) {
		dev_attrs = devm_kzalloc(&atile->dev, attr->num_dev_attrs + 1,
					 GFP_KERNEL);
		if (!dev_attrs)
			return -ENOMEM;

		for (index = 0; index < attr->num_dev_attrs; index++) {
			struct device_attribute *node;
			const struct aie_tile_operations *ops;
			const struct aie_dev_attr *dev_attr;
			u32 ttype;

			ops = atile->apart->adev->ops;
			ttype = ops->get_tile_type(&atile->loc);
			dev_attr = &attr->dev_attr[index];

			if (!(BIT(ttype) & attr->dev_attr[index].tile_type))
				continue;

			node = aie_sysfs_create_dev_attr(&atile->dev, dev_attr);
			if (IS_ERR_VALUE(node))
				return PTR_ERR(node);

			dev_attrs[i++] = &node->attr;
		}
	}

	if (attr->num_dev_attrs) {
		attr_grp = devm_kzalloc(&atile->dev,
					sizeof(struct attribute_group),
					GFP_KERNEL);
		if (!attr_grp)
			return -ENOMEM;

		attr_grp->attrs = dev_attrs;

		ret = devm_device_add_group(&atile->dev, attr_grp);
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
	struct attribute **dev_attrs;
	int ret = 0;
	u32 index;

	attr = apart->adev->part_sysfs_attr;

	if (attr->num_dev_attrs) {
		dev_attrs = devm_kzalloc(&apart->dev, attr->num_dev_attrs + 1,
					 GFP_KERNEL);
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

	if (attr->num_dev_attrs) {
		attr_grp = devm_kzalloc(&apart->dev,
					sizeof(struct attribute_group),
					GFP_KERNEL);
		if (!attr_grp)
			return -ENOMEM;
		attr_grp->attrs = dev_attrs;

		ret = devm_device_add_group(&apart->dev, attr_grp);
		if (ret) {
			dev_err(&apart->dev,
				"Failed to add sysfs attributes group\n");
		}
	}
	return ret;
}

/**
 * aie_part_sysfs_init() - initializes sysfs interface by creating node at tile
 *			   and partition granularity.
 * @apart: AI engine partition.
 * @return: 0 for success, error code for failure.
 */
int aie_part_sysfs_init(struct aie_partition *apart)
{
	struct aie_tile *atile = apart->atiles;
	u32 index;
	int ret;

	ret = aie_part_sysfs_create(apart);
	if (ret < 0) {
		dev_err(&apart->dev,
			"Failed to create partition level sysfs nodes\n");
		return ret;
	}

	for (index = 0; index < apart->range.size.col * apart->range.size.row;
	     index++) {
		ret = aie_tile_sysfs_create(atile);
		if (ret < 0) {
			dev_err(&atile->dev,
				"Failed to create tile level sysfs nodes\n");
			return ret;
		}
		atile++;
	}
	return ret;
}
