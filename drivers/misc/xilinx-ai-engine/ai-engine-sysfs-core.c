// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver.
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */
#include "ai-engine-internal.h"

#define AIE_CORE_STS_ENABLE_MASK	0x3U

/**
 * aie_get_core_pc() - reads the AI engine core program counter value.
 * @apart: AI engine partition.
 * @loc: location of AI engine core.
 * @return: 32-bit register value.
 */
static u32 aie_get_core_pc(struct aie_partition *apart,
			   struct aie_location *loc)
{
	u32 regoff;

	regoff = aie_cal_regoff(apart->adev, *loc,
				apart->adev->core_pc->regoff);
	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie_get_core_lr() - reads the AI engine core link register value.
 * @apart: AI engine partition.
 * @loc: location of AI engine core.
 * @return: 32-bit register value.
 */
static u32 aie_get_core_lr(struct aie_partition *apart,
			   struct aie_location *loc)
{
	u32 regoff;

	regoff = aie_cal_regoff(apart->adev, *loc,
				apart->adev->core_lr->regoff);
	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie_get_core_sp() - reads the AI engine core stack pointer value.
 * @apart: AI engine partition.
 * @loc: location of AI engine core.
 * @return: 32-bit register value.
 */
static u32 aie_get_core_sp(struct aie_partition *apart,
			   struct aie_location *loc)
{
	u32 regoff;

	regoff = aie_cal_regoff(apart->adev, *loc,
				apart->adev->core_sp->regoff);
	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie_sysfs_get_core_status() - returns the status of core in string format
 *				 with each status value separated by a '|'
 *				 symbol.
 * @apart: AI engine partition.
 * @loc: location of AI engine core.
 * @buffer: location to return core status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie_sysfs_get_core_status(struct aie_partition *apart,
				  struct aie_location *loc, char *buffer,
				  ssize_t size)
{
	ssize_t len = 0;
	u32 ttype, n;
	unsigned long status;
	bool is_delimit_req = false;
	char **str = apart->adev->core_status_str;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		return len;

	if (!aie_part_check_clk_enable_loc(apart, loc)) {
		len += scnprintf(buffer, max(0L, size), "clock_gated");
		return len;
	}

	/*
	 * core is in disabled state when neither the enable nor reset bit is
	 * high
	 */
	status = apart->adev->ops->get_core_status(apart, loc);
	if (!(status & AIE_CORE_STS_ENABLE_MASK)) {
		len += scnprintf(&buffer[len], max(0L, size - len), "disabled");
		is_delimit_req = true;
	}

	for_each_set_bit(n, &status, 32) {
		if (is_delimit_req) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL0);
		}
		len += scnprintf(&buffer[len], max(0L, size - len), str[n]);
		is_delimit_req = true;
	}

	return len;
}

/**
 * aie_tile_show_core() - exports AI engine core status, value of program
 *			  counter, stack pointer, and link register to a tile
 *			  level sysfs node.
 * @dev: AI engine tile device.
 * @attr: sysfs device attribute.
 * @buffer: export buffer.
 * @return: length of string copied to buffer.
 */
ssize_t aie_tile_show_core(struct device *dev, struct device_attribute *attr,
			   char *buffer)
{
	struct aie_tile *atile = container_of(dev, struct aie_tile, dev);
	struct aie_partition *apart = atile->apart;
	ssize_t len = 0, size = PAGE_SIZE;
	u32 pc = 0, lr = 0, sp = 0;
	char sts_buf[AIE_SYSFS_CORE_STS_SIZE];

	if (mutex_lock_interruptible(&apart->mlock)) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return len;
	}

	if (!aie_part_check_clk_enable_loc(apart, &atile->loc)) {
		scnprintf(sts_buf, AIE_SYSFS_CORE_STS_SIZE, "clock_gated");
		goto out;
	}

	aie_sysfs_get_core_status(apart, &atile->loc, sts_buf,
				  AIE_SYSFS_CORE_STS_SIZE);
	pc = aie_get_core_pc(apart, &atile->loc);
	lr = aie_get_core_lr(apart, &atile->loc);
	sp = aie_get_core_sp(apart, &atile->loc);

out:
	mutex_unlock(&apart->mlock);

	len += scnprintf(&buffer[len], max(0L, size - len), "status: %s\n",
			 sts_buf);
	len += scnprintf(&buffer[len], max(0L, size - len), "pc: %#.8x\n", pc);
	len += scnprintf(&buffer[len], max(0L, size - len), "lr: %#.8x\n", lr);
	len += scnprintf(&buffer[len], max(0L, size - len), "sp: %#.8x\n", sp);
	return len;
}

/**
 * aie_part_read_cb_core() - exports status of all cores within a given
 *			     partition to partition level node.
 * @kobj: kobject used to create sysfs node.
 * @buffer: export buffer.
 * @size: length of export buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie_part_read_cb_core(struct kobject *kobj, char *buffer, ssize_t size)
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
		u32 ttype = apart->adev->ops->get_tile_type(apart->adev,
							    &atile->loc);

		if (ttype != AIE_TILE_TYPE_TILE)
			continue;

		len += scnprintf(&buffer[len], max(0L, size - len), "%d_%d: ",
				 atile->loc.col, atile->loc.row);
		len += aie_sysfs_get_core_status(apart, &atile->loc,
						 &buffer[len], size - len);
		len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	}

	mutex_unlock(&apart->mlock);
	return len;
}
