/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for Xilinx Ethernet Offload Engine.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 */

#ifndef XILINX_AXIENET_EOE_H
#define XILINX_AXIENET_EOE_H

#include "xilinx_axienet.h"

#ifdef CONFIG_XILINX_AXI_EOE
int axienet_eoe_probe(struct platform_device *pdev);
#else
static inline int axienet_eoe_probe(struct platform_device *pdev)
{
	return -ENODEV;
}
#endif

#endif /* XILINX_AXIENET_EOE_H */
