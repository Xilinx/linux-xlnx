// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver.
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */
#include "ai-engine-internal.h"
#include "linux/xlnx-ai-engine.h"

static char *aie_error_category_str[] = {
	"saturation",
	"floating_point",
	"stream_switch",
	"access",
	"bus",
	"instruction",
	"ecc",
	"lock",
	"dma",
	"memory_parity",
};

/**
 * aie_get_errors_str() - returns errors in string format. Errors of the same
 *			  category are separated by a '|' symbol with the error
 *			  category sting as a label prefix.
 * @apart: AI engine partition.
 * @loc: location of AI engine tile.
 * @module: module type.
 * @err_attr: error attribute for given module type.
 * @buffer: location to return error string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
static ssize_t aie_get_errors_str(struct aie_partition *apart,
				  struct aie_location loc,
				  enum aie_module_type module,
				  const struct aie_error_attr *err_attr,
				  char *buffer, ssize_t size)
{
	ssize_t len = 0;
	u32 i, j;
	char *mod;

	if (module == AIE_CORE_MOD)
		mod = "core";
	else if (module == AIE_MEM_MOD)
		mod = "memory";
	else
		mod = "pl";

	for (i = 0; i < err_attr->num_err_categories; i++) {
		const struct aie_err_category *category;
		char errstr[AIE_SYSFS_ERROR_SIZE];
		bool is_delimit_req = false;
		bool err = false;
		ssize_t l = 0;
		u8 index;

		category = &err_attr->err_category[i];
		index = category->err_category;
		for (j = 0; j < category->num_events; j++) {
			u8 event = category->prop[j].event;
			char *str = category->prop[j].event_str;

			if (!aie_check_error_bitmap(apart, loc, module, event))
				continue;

			if (is_delimit_req) {
				l += scnprintf(&errstr[l],
					       max(0L, AIE_SYSFS_ERROR_SIZE - l),
					       DELIMITER_LEVEL0);
			}

			l += scnprintf(&errstr[l],
				       max(0L, AIE_SYSFS_ERROR_SIZE - l), str);
			err = true;
			is_delimit_req = true;
		}

		if (err) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%s: %s: %s\n", mod,
					 aie_error_category_str[index], errstr);
		}
	}
	return len;
}

/**
 * aie_get_error_category_str() - returns error categories in string format.
 *				  Errors categories are separated by a '|'
 *				  symbol.
 * @apart: AI engine partition.
 * @loc: location of AI engine tile.
 * @module: module type.
 * @err_attr: error attribute for given module type.
 * @buffer: location to return error string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
static ssize_t aie_get_error_category_str(struct aie_partition *apart,
					  struct aie_location loc,
					  enum aie_module_type module,
					  const struct aie_error_attr *err_attr,
					  char *buffer, ssize_t size)
{
	ssize_t len = 0;
	u32 i, j;
	bool is_delimit_req = false;

	for (i = 0; i < err_attr->num_err_categories; i++) {
		const struct aie_err_category *category;

		category = &err_attr->err_category[i];
		for (j = 0; j < category->num_events; j++) {
			u8 event = category->prop[j].event;
			u8 index = category->err_category;

			if (!aie_check_error_bitmap(apart, loc, module, event))
				continue;

			if (is_delimit_req) {
				len += scnprintf(&buffer[len], max(0L, size - len),
						 DELIMITER_LEVEL0);
			}

			len += scnprintf(&buffer[len], max(0L, size - len),
					aie_error_category_str[index]);
			is_delimit_req = true;
			break;
		}
	}
	return len;
}

/**
 * aie_tile_show_error() - exports detailed error information to a tile level
 *			   sysfs node.
 * @dev: AI engine tile device.
 * @attr: sysfs device attribute.
 * @buffer: export buffer.
 * @return: length of string copied to buffer.
 */
ssize_t aie_tile_show_error(struct device *dev, struct device_attribute *attr,
			    char *buffer)
{
	struct aie_tile *atile = container_of(dev, struct aie_tile, dev);
	struct aie_partition *apart = atile->apart;
	const struct aie_error_attr *core_attr, *mem_attr, *pl_attr;
	ssize_t len = 0, size = PAGE_SIZE;
	u32 ttype, core_count = 0, mem_count = 0, pl_count = 0;

	if (mutex_lock_interruptible(&apart->mlock)) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return len;
	}

	ttype = apart->adev->ops->get_tile_type(&atile->loc);
	if (ttype == AIE_TILE_TYPE_TILE) {
		core_attr = apart->adev->core_errors;
		mem_attr = apart->adev->mem_errors;
		core_count = aie_get_module_error_count(apart, atile->loc,
							AIE_CORE_MOD,
							core_attr);
		mem_count = aie_get_module_error_count(apart, atile->loc,
						       AIE_MEM_MOD, mem_attr);
	} else {
		pl_attr = apart->adev->shim_errors;
		pl_count = aie_get_module_error_count(apart, atile->loc,
						      AIE_PL_MOD, pl_attr);
	}

	if (!(core_count || mem_count || pl_count)) {
		mutex_unlock(&apart->mlock);
		return len;
	}

	if (core_count) {
		len += aie_get_errors_str(apart, atile->loc, AIE_CORE_MOD,
					  core_attr, &buffer[len], size - len);
	}

	if (mem_count) {
		len += aie_get_errors_str(apart, atile->loc, AIE_MEM_MOD,
					  mem_attr, &buffer[len], size - len);
	}

	if (pl_count) {
		len += aie_get_errors_str(apart, atile->loc, AIE_PL_MOD,
					  pl_attr, &buffer[len], size - len);
	}

	mutex_unlock(&apart->mlock);
	return len;
}

/**
 * aie_part_show_error_stat() - exports error count in a partition to a
 *				partition level sysfs node.
 * @dev: AI engine tile device.
 * @attr: sysfs device attribute.
 * @buffer: export buffer.
 * @return: length of string copied to buffer.
 */
ssize_t aie_part_show_error_stat(struct device *dev,
				 struct device_attribute *attr, char *buffer)
{
	struct aie_partition *apart = dev_to_aiepart(dev);
	struct aie_tile *atile = apart->atiles;
	const struct aie_error_attr *core_attr, *mem_attr, *pl_attr;
	ssize_t len = 0, size = PAGE_SIZE;
	u32 index, core = 0, mem = 0, pl = 0;

	if (mutex_lock_interruptible(&apart->mlock)) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return len;
	}

	for (index = 0; index < apart->range.size.col * apart->range.size.row;
	     index++, atile++) {
		u32 ttype = apart->adev->ops->get_tile_type(&atile->loc);

		if (ttype == AIE_TILE_TYPE_TILE) {
			core_attr = apart->adev->core_errors;
			mem_attr = apart->adev->mem_errors;
			core += aie_get_module_error_count(apart, atile->loc,
							   AIE_CORE_MOD,
							   core_attr);
			mem += aie_get_module_error_count(apart, atile->loc,
							  AIE_MEM_MOD,
							  mem_attr);
		} else {
			pl_attr = apart->adev->shim_errors;
			pl += aie_get_module_error_count(apart, atile->loc,
							 AIE_PL_MOD, pl_attr);
		}
	}

	mutex_unlock(&apart->mlock);

	len += scnprintf(&buffer[len], max(0L, size - len), "core: %d\n", core);
	len += scnprintf(&buffer[len], max(0L, size - len), "memory: %d\n",
			 mem);
	len += scnprintf(&buffer[len], max(0L, size - len), "pl: %d\n", pl);
	return len;
}

/**
 * aie_sysfs_get_errors() - returns all asserted error categories in string
 *			    format. Error categories within module label,
 *			    are separated by a '|' symbol.
 * @apart: AI engine partition.
 * @loc: location of AI engine tile.
 * @buffer: location to return error string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie_sysfs_get_errors(struct aie_partition *apart,
			     struct aie_location *loc, char *buffer,
			     ssize_t size)
{
	u32 ttype, core_count = 0, mem_count = 0, pl_count = 0;
	const struct aie_error_attr *core_attr, *mem_attr, *pl_attr;
	ssize_t len = 0;

	ttype = apart->adev->ops->get_tile_type(loc);
	if (ttype == AIE_TILE_TYPE_TILE) {
		core_attr = apart->adev->core_errors;
		mem_attr = apart->adev->mem_errors;
		core_count = aie_get_module_error_count(apart, *loc,
							AIE_CORE_MOD,
							core_attr);
		mem_count  = aie_get_module_error_count(apart, *loc,
							AIE_MEM_MOD, mem_attr);
	} else {
		pl_attr = apart->adev->shim_errors;
		pl_count = aie_get_module_error_count(apart, *loc, AIE_PL_MOD,
						      pl_attr);
	}

	if (!(core_count || mem_count || pl_count))
		return len;

	len += scnprintf(&buffer[len], max(0L, size - len), "%d_%d: ", loc->col,
			 loc->row);

	if (core_count) {
		len += scnprintf(&buffer[len], max(0L, size - len), "core: ");
		len += aie_get_error_category_str(apart, *loc, AIE_CORE_MOD,
						  core_attr, &buffer[len],
						  size - len);
	}

	if (mem_count) {
		len += scnprintf(&buffer[len], max(0L, size - len), "%smemory: ",
				 core_count ? DELIMITER_LEVEL1 : "");
		len += aie_get_error_category_str(apart, *loc, AIE_MEM_MOD,
						  mem_attr, &buffer[len],
						  size - len);
	}

	if (pl_count) {
		len += scnprintf(&buffer[len], max(0L, size - len), "pl: ");
		len += aie_get_error_category_str(apart, *loc, AIE_PL_MOD,
						  pl_attr, &buffer[len],
						  size - len);
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	return len;
}

/**
 * aie_part_read_cb_error() - exports errors with a given partition to
 *			      partition level node.
 * @kobj: kobject used to create sysfs node.
 * @buffer: export buffer.
 * @size: length of export buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie_part_read_cb_error(struct kobject *kobj, char *buffer, ssize_t size)
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

	if (!(aie_get_error_count(apart))) {
		mutex_unlock(&apart->mlock);
		return len;
	}

	for (index = 0; index < apart->range.size.col * apart->range.size.row;
	     index++, atile++) {
		len += aie_sysfs_get_errors(apart, &atile->loc, &buffer[len],
					    size - len);
	}

	mutex_unlock(&apart->mlock);
	return len;
}
