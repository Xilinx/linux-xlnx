/*
 * Copyright (C) 2017 Xilinx, Inc.
 *
 * Michal Simek <michal.simek@xilinx.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __SOC_ZYNQMP_FW_H__
#define __SOC_ZYNQMP_FW_H__

#include <linux/nvmem-consumer.h>

enum {
	ZYNQMP_SILICON_V1 = 0,
	ZYNQMP_SILICON_V2,
	ZYNQMP_SILICON_V3,
	ZYNQMP_SILICON_V4,
};

static inline char *zynqmp_nvmem_get_silicon_version(struct device *dev,
						     const char *cname)
{
	struct nvmem_cell *cell;
	ssize_t data;
	char *ret;

	cell = nvmem_cell_get(dev, cname);
	if (IS_ERR(cell))
		return ERR_CAST(cell);

	ret = nvmem_cell_read(cell, &data);
	nvmem_cell_put(cell);

	return ret;
}

#endif /* __SOC_ZYNQMP_FW_H__ */
