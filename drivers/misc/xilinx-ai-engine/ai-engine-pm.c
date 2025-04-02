// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine driver AIE-2PS device specific implementation
 *
 * Copyright (C) 2023 - 2025 Advanced Micro Devices, Inc.
 */

#include "ai-engine-internal.h"

#include <linux/dma-mapping.h>

#define AIE_PM_OPS_PKT_SIZE	200 /* bytes */

int aie_part_pm_ops_create(struct aie_partition *apart)
{
	struct aie_pm_ops *pm_ops = &apart->pm_ops;

	if (apart->adev->dev_gen != AIE_DEVICE_GEN_AIE2PS)
		return 0;

	pm_ops->pkt_va = dmam_alloc_coherent(&apart->dev, AIE_PM_OPS_PKT_SIZE, &pm_ops->pkt_dma,
					     GFP_KERNEL);
	if (!pm_ops->pkt_va)
		return -ENOMEM;
	memset(pm_ops->pkt_va, 0, AIE_PM_OPS_PKT_SIZE);

	pm_ops->size = AIE_PM_OPS_PKT_SIZE;
	pm_ops->offset = 0;

	return 0;
}

void aie_part_pm_ops_free(struct aie_partition *apart)
{
	if (!apart->pm_ops.pkt_va)
		return;
	dmam_free_coherent(&apart->dev, apart->pm_ops.size, apart->pm_ops.pkt_va,
			   apart->pm_ops.pkt_dma);
}
