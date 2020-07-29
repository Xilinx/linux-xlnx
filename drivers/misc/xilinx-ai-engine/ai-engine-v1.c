// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine driver v1 specific implementation
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include <linux/bitfield.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "ai-engine-internal.h"

#define KBYTES(n)	((n) * 1024)

#define AIE_ARRAY_SHIFT		30U
#define AIE_COL_SHIFT		23U
#define AIE_ROW_SHIFT		18U

#define NUM_MEMS_PER_TILE	2U

/*
 * Registers offsets
 */
#define AIE_SHIMNOC_L2INTR_MASK_REGOFF		0x00015000U
#define AIE_SHIMNOC_L2INTR_INTR_REGOFF		0x00015010U
#define AIE_SHIMNOC_DMA_BD0_ADDRLOW_REGOFF	0x0001d000U
#define AIE_SHIMNOC_DMA_BD15_PACKET_REGOFF	0x0001d13cU
#define AIE_SHIMNOC_AXIMM_REGOFF		0x0001e020U
#define AIE_SHIMPL_L1INTR_MASK_A_REGOFF		0x00035000U
#define AIE_SHIMPL_L1INTR_BLOCK_NORTH_B_REGOFF	0x00035050U
#define AIE_SHIMPL_CLKCNTR_REGOFF		0x00036040U
#define AIE_SHIMPL_RESET_REGOFF			0x0003604cU
#define AIE_TILE_CORE_CLKCNTR_REGOFF		0x00036040U

/*
 * Register masks
 */
#define AIE_SHIMPL_SHIMRST_MASK			0x1U

/*
 * AI engine SHIM reset ID.
 * TODO: it should follow the Linux reset framework. The ID should be in the
 * device tree. However, as versal resets is not ready, we hardcode it in the
 * driver.
 */
#define VERSAL_PM_RST_AIE_SHIM_ID			0xc10405fU

static const struct aie_tile_regs aiev1_kernel_regs[] = {
	/* SHIM AXI MM Config */
	{.attribute = AIE_TILE_TYPE_SHIMNOC << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMNOC_AXIMM_REGOFF,
	 .eoff = AIE_SHIMNOC_AXIMM_REGOFF,
	},
	/* SHIM DMA ADDRESS range */
	{.attribute = AIE_TILE_TYPE_SHIMNOC << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMNOC_DMA_BD0_ADDRLOW_REGOFF,
	 .eoff = AIE_SHIMNOC_DMA_BD15_PACKET_REGOFF,
	},
	/* SHIM 2nd level interrupt controller */
	{.attribute = AIE_TILE_TYPE_SHIMNOC << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMNOC_L2INTR_MASK_REGOFF,
	 .eoff = AIE_SHIMNOC_L2INTR_INTR_REGOFF,
	},
	/* SHIM 1st level interrupt controller */
	{.attribute = AIE_TILE_TYPE_SHIMPL << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_L1INTR_MASK_A_REGOFF,
	 .eoff = AIE_SHIMPL_L1INTR_BLOCK_NORTH_B_REGOFF,
	},
	/* SHIM reset Enable */
	{.attribute = AIE_TILE_TYPE_SHIMPL << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_RESET_REGOFF,
	 .eoff = AIE_SHIMPL_RESET_REGOFF,
	},
	/* SHIM clock control */
	{.attribute = AIE_TILE_TYPE_SHIMPL << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_CLKCNTR_REGOFF,
	 .eoff = AIE_SHIMPL_CLKCNTR_REGOFF,
	},
	/* Tile clock control */
	{.attribute = AIE_TILE_TYPE_SHIMPL << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_TILE_CORE_CLKCNTR_REGOFF,
	 .eoff = AIE_TILE_CORE_CLKCNTR_REGOFF,
	},
};

static const struct zynqmp_eemi_ops *eemi_ops;

static u32 aiev1_get_tile_type(struct aie_location *loc)
{
	if (loc->row)
		return AIE_TILE_TYPE_TILE;
	/* SHIM row */
	if ((loc->col % 4) < 2)
		return AIE_TILE_TYPE_SHIMPL;

	return AIE_TILE_TYPE_SHIMNOC;
}

static unsigned int aiev1_get_mem_info(struct aie_range *range,
				       struct aie_part_mem *pmem)
{
	unsigned int i;

	if (range->start.row + range->size.row <= 1) {
		/* SHIM row only, no memories in this range */
		return 0;
	}
	if (!pmem)
		return NUM_MEMS_PER_TILE;

	for (i = 0; i < NUM_MEMS_PER_TILE; i++) {
		struct aie_mem *mem = &pmem[i].mem;

		memcpy(&mem->range, range, sizeof(*range));
		if (!mem->range.start.row) {
			mem->range.start.row = 1;
			mem->range.size.row--;
		}
	}
	/* Setup tile data memory information */
	pmem[0].mem.offset = 0;
	pmem[0].mem.size = KBYTES(32);
	/* Setup program memory information */
	pmem[1].mem.offset = 0x20000;
	pmem[1].mem.size = KBYTES(16);

	return NUM_MEMS_PER_TILE;
}

/**
 * aiev1_set_shim_reset() - Set AI engine SHIM reset
 * @adev: AI engine device
 * @range: range of AI engine tiles
 * @assert: true to set reset, false to unset reset
 */
static void aiev1_set_shim_reset(struct aie_device *adev,
				 struct aie_range *range, bool assert)
{
	u32 c;
	u32 val;
	struct aie_location loc;

	val = FIELD_PREP(AIE_SHIMPL_SHIMRST_MASK, (assert ? 1 : 0));
	loc.row = 0;
	for (c = range->start.col; c < range->start.col + range->size.col;
	     c++) {
		u32 regoff;

		loc.col = c;
		regoff = aie_cal_regoff(adev, loc, AIE_SHIMPL_RESET_REGOFF);
		iowrite32(val, adev->base + regoff);
	}
}

static int aiev1_reset_shim(struct aie_device *adev, struct aie_range *range)
{
	int ret;

	/* Enable shim reset of each column */
	aiev1_set_shim_reset(adev, range, true);

	/* Assert shim reset of AI engine array */
	ret = eemi_ops->reset_assert(VERSAL_PM_RST_AIE_SHIM_ID,
				     PM_RESET_ACTION_ASSERT);
	if (ret < 0) {
		dev_err(&adev->dev, "failed to assert SHIM reset.\n");
		return ret;
	}

	/* Release shim reset of AI engine array */
	ret = eemi_ops->reset_assert(VERSAL_PM_RST_AIE_SHIM_ID,
				     PM_RESET_ACTION_RELEASE);
	if (ret < 0) {
		dev_err(&adev->dev, "failed to release SHIM reset.\n");
		return ret;
	}

	/* Disable shim reset of each column */
	aiev1_set_shim_reset(adev, range, false);

	return 0;
}

static const struct aie_tile_operations aiev1_ops = {
	.get_tile_type = aiev1_get_tile_type,
	.get_mem_info = aiev1_get_mem_info,
	.reset_shim = aiev1_reset_shim,
};

/**
 * aiev1_device_init() - Initialize AI engine device struct v1 specific
 * @adev: AI engine device
 * @return: 0 for success, negative value for failure.
 *
 * This function initialize the AI engine device structure device version
 * specific elements such as register addressing related array shift,
 * column shift, and row shift; v1 specific device operations, device
 * columns resource.
 */
int aiev1_device_init(struct aie_device *adev)
{
	int ret;

	adev->array_shift = AIE_ARRAY_SHIFT;
	adev->col_shift = AIE_COL_SHIFT;
	adev->row_shift = AIE_ROW_SHIFT;
	adev->ops = &aiev1_ops;
	adev->num_kernel_regs = ARRAY_SIZE(aiev1_kernel_regs);
	adev->kernel_regs = aiev1_kernel_regs;

	eemi_ops = zynqmp_pm_get_eemi_ops();
	if (IS_ERR(eemi_ops) || !eemi_ops->reset_assert) {
		dev_err(&adev->dev, "failed to get eemi ops.\n");
		return PTR_ERR(eemi_ops);
	}

	/* Get the columns resource */
	/* Get number of columns from AI engine memory resource */
	ret = aie_resource_initialize(&adev->cols_res, 50);
	if (ret)
		dev_err(&adev->dev, "failed to initialize columns resource.\n");

	return ret;
}
