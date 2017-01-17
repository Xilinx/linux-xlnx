// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Xilinx, Inc.
 *
 * Michal Simek <michal.simek@xilinx.com>
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
