/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Vasileios Bimpikas <vasileios.bimpikas@xilinx.com>
 */

struct xroe_traffic_gen_local {
	void __iomem *base_addr;
};

enum { XROE_SIZE_MAX = 15 };

int xroe_traffic_gen_sysfs_init(struct device *dev);
void xroe_traffic_gen_sysfs_exit(struct device *dev);
