/*
 * nand.h - xilinx nand details
 *
 * Copyright (c) 2010 Xilinx Inc.
 *
 * This file is released under the GPLv2
 *
 */

#ifndef __ASM_ARCH_NAND_H_
#define __ASM_ARCH_NAND_H_

#include <linux/mtd/partitions.h>

struct xnand_platform_data {
        unsigned int            options;
        struct mtd_partition    *parts;
	int			nr_parts;
};

#endif
