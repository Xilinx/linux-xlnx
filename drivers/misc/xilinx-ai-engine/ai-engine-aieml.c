// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine driver AIE-ML device specific implementation
 *
 * Copyright (C) 2022 Xilinx, Inc.
 */

#include <linux/bitfield.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/xlnx-ai-engine.h>

#include "ai-engine-internal.h"

#define AIEML_ARRAY_SHIFT	32U
#define AIEML_COL_SHIFT		25U
#define AIEML_ROW_SHIFT		20U

#define NUM_TYPES_OF_MEM	3U

/*
 * Register offsets
 */
#define AIEML_SHIMNOC_AXIMM_REGOFF			0x0001e020U
#define AIEML_SHIMNOC_BD0_0_REGOFF			0x0001d000U
#define AIEML_SHIMNOC_BD15_7_REGOFF			0x0001d1fcU
#define AIEML_SHIMNOC_L2INTR_MASK_REGOFF		0x00015000U
#define AIEML_SHIMNOC_L2INTR_INTR_REGOFF		0x00015010U
#define AIEML_SHIMPL_BISRCACHE_CTRL_REGOFF		0x00036000U
#define AIEML_SHIMPL_COLCLOCK_CTRL_REGOFF		0x000fff20U
#define AIEML_SHIMPL_COLRESET_CTRL_REGOFF		0x000fff28U
#define AIEML_SHIMPL_GROUPERROR_REGOFF			0x0003450cU
#define AIEML_SHIMPL_L1INTR_MASK_A_REGOFF		0x00035000U
#define AIEML_SHIMPL_L1INTR_BLOCK_NORTH_B_REGOFF	0x00035050U
#define AIEML_SHIMPL_MODCLOCK_CTRL_0_REGOFF		0x000fff00U
#define AIEML_SHIMPL_MODCLOCK_CTRL_1_REGOFF		0x000fff04U
#define AIEML_SHIMPL_MODRESET_CTRL_0_REGOFF		0x000fff10U
#define AIEML_SHIMPL_MODRESET_CTRL_1_REGOFF		0x000fff14U
#define AIEML_MEMORY_GROUPERROR_REGOFF			0x00094518U
#define AIEML_MEMORY_MEMCTRL_REGOFF			0x00096048U
#define AIEML_MEMORY_MODCLOCKCTRL_REGOFF		0x000fff00U
#define AIEML_MEMORY_MODRESETCTRL_REGOFF		0x000fff10U
#define AIEML_TILE_COREMOD_AMLL0_PART1_REGOFF		0x00030000U
#define AIEML_TILE_COREMOD_AMHH8_PART2_REGOFF		0x00030470U
#define AIEML_TILE_COREMOD_GROUPERROR_REGOFF		0x00034510U
#define AIEML_TILE_COREMOD_MEMCTRL_REGOFF		0x00036070U
#define AIEML_TILE_COREMOD_MODCLOCKCTRL_REGOFF		0x00060000U
#define AIEML_TILE_COREMOD_MODRESETCTRL_REGOFF		0x00060010U
#define AIEML_TILE_COREMOD_WL0_PART1_REGOFF		0x00030800U
#define AIEML_TILE_COREMOD_WH11_PART2_REGOFF		0x00030af0U
#define AIEML_TILE_COREMOD_R0_REGOFF			0x00030c00U
#define AIEML_TILE_COREMOD_R31_REGOFF			0x00030df0U
#define AIEML_TILE_MEMMOD_GROUPERROR_REGOFF		0x00014514U
#define AIEML_TILE_MEMMOD_MEMCTRL_REGOFF		0x00016010U

/*
 * Register masks
 */
#define AIEML_SHIMPL_COLRESET_CTRL_MASK			GENMASK(1, 0)
#define AIEML_SHIMPL_COLCLOCK_CTRL_MASK			GENMASK(1, 0)

static const struct aie_tile_regs aieml_kernel_regs[] = {
	/* SHIM AXI MM Config */
	{.attribute = AIE_TILE_TYPE_MASK_SHIMNOC << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_SHIMNOC_AXIMM_REGOFF,
	 .eoff = AIEML_SHIMNOC_AXIMM_REGOFF,
	},
	/* SHIM DMA buffer descriptor address range */
	{.attribute = AIE_TILE_TYPE_MASK_SHIMNOC << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_SHIMNOC_BD0_0_REGOFF,
	 .eoff = AIEML_SHIMNOC_BD15_7_REGOFF,
	},
	/* SHIM 2nd level interrupt controller */
	{.attribute = AIE_TILE_TYPE_MASK_SHIMNOC << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_SHIMNOC_L2INTR_MASK_REGOFF,
	 .eoff = AIEML_SHIMNOC_L2INTR_INTR_REGOFF,
	},
	/* SHIM BISR cache control */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_SHIMPL_BISRCACHE_CTRL_REGOFF,
	 .eoff = AIEML_SHIMPL_BISRCACHE_CTRL_REGOFF,
	},
	/* SHIM column clock control */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_SHIMPL_COLCLOCK_CTRL_REGOFF,
	 .eoff = AIEML_SHIMPL_COLCLOCK_CTRL_REGOFF,
	},
	/* SHIM column reset control */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_SHIMPL_COLRESET_CTRL_REGOFF,
	 .eoff = AIEML_SHIMPL_COLRESET_CTRL_REGOFF,
	},
	/* SHIM group error enable */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_SHIMPL_GROUPERROR_REGOFF,
	 .eoff = AIEML_SHIMPL_GROUPERROR_REGOFF,
	},
	/* SHIM 1st level interrupt controller */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_SHIMPL_L1INTR_MASK_A_REGOFF,
	 .eoff = AIEML_SHIMPL_L1INTR_BLOCK_NORTH_B_REGOFF,
	},
	/* SHIM module clock control */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_SHIMPL_MODCLOCK_CTRL_0_REGOFF,
	 .eoff = AIEML_SHIMPL_MODCLOCK_CTRL_1_REGOFF,
	},
	/* SHIM module reset control */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_SHIMPL_MODRESET_CTRL_0_REGOFF,
	 .eoff = AIEML_SHIMPL_MODRESET_CTRL_1_REGOFF,
	},
	/* MEMORY tile group error enable */
	{.attribute = AIE_TILE_TYPE_MASK_MEMORY << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_MEMORY_GROUPERROR_REGOFF,
	 .eoff = AIEML_MEMORY_GROUPERROR_REGOFF,
	},
	/* MEMORY tile mem control */
	{.attribute = AIE_TILE_TYPE_MASK_MEMORY << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_MEMORY_MEMCTRL_REGOFF,
	 .eoff = AIEML_MEMORY_MEMCTRL_REGOFF,
	},
	/* MEMORY tile module clock control */
	{.attribute = AIE_TILE_TYPE_MASK_MEMORY << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_MEMORY_MODCLOCKCTRL_REGOFF,
	 .eoff = AIEML_MEMORY_MODCLOCKCTRL_REGOFF,
	},
	/* MEMORY tile module reset control */
	{.attribute = AIE_TILE_TYPE_MASK_MEMORY << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_MEMORY_MODRESETCTRL_REGOFF,
	 .eoff = AIEML_MEMORY_MODRESETCTRL_REGOFF,
	},
	/* TILE core module group error enable */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_TILE_COREMOD_GROUPERROR_REGOFF,
	 .eoff = AIEML_TILE_COREMOD_GROUPERROR_REGOFF,
	},
	/* TILE memory control */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_TILE_COREMOD_MEMCTRL_REGOFF,
	 .eoff = AIEML_TILE_COREMOD_MEMCTRL_REGOFF,
	},
	/* TILE module clock control */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_TILE_COREMOD_MODCLOCKCTRL_REGOFF,
	 .eoff = AIEML_TILE_COREMOD_MODCLOCKCTRL_REGOFF,
	},
	/* TILE module reset control */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_TILE_COREMOD_MODRESETCTRL_REGOFF,
	 .eoff = AIEML_TILE_COREMOD_MODRESETCTRL_REGOFF,
	},
	/* TILE memory module group error enable */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_TILE_MEMMOD_GROUPERROR_REGOFF,
	 .eoff = AIEML_TILE_MEMMOD_GROUPERROR_REGOFF,
	},
	/* TILE memory module mem control */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_TILE_MEMMOD_MEMCTRL_REGOFF,
	 .eoff = AIEML_TILE_MEMMOD_MEMCTRL_REGOFF,
	},
};

static const struct aie_tile_regs aieml_core_amxx_regs = {
	.attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	.soff = AIEML_TILE_COREMOD_AMLL0_PART1_REGOFF,
	.eoff = AIEML_TILE_COREMOD_AMHH8_PART2_REGOFF,
};

static const struct aie_tile_regs aieml_core_wx_regs = {
	.attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	.soff = AIEML_TILE_COREMOD_WL0_PART1_REGOFF,
	.eoff = AIEML_TILE_COREMOD_WH11_PART2_REGOFF,
};

static const struct aie_tile_regs aieml_core_32bit_regs = {
	.attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	.soff = AIEML_TILE_COREMOD_R0_REGOFF,
	.eoff = AIEML_TILE_COREMOD_R31_REGOFF,
};

static const struct aie_core_regs_attr aieml_core_regs[] = {
	{.core_regs = &aieml_core_amxx_regs,
	 .width = 4,
	},
	{.core_regs = &aieml_core_wx_regs,
	 .width = 4,
	},
	{.core_regs = &aieml_core_32bit_regs,
	 .width = 1,
	},
};

static const struct aie_single_reg_field aieml_col_rst = {
	.mask = AIEML_SHIMPL_COLRESET_CTRL_MASK,
	.regoff = AIEML_SHIMPL_COLRESET_CTRL_REGOFF,
};

static const struct aie_single_reg_field aieml_col_clkbuf = {
	.mask = AIEML_SHIMPL_COLCLOCK_CTRL_MASK,
	.regoff = AIEML_SHIMPL_COLCLOCK_CTRL_REGOFF,
};

static const struct aie_dma_attr aieml_shimdma = {
	.laddr = {
		.mask = 0xffffffffU,
		.regoff = 0x4U,
	},
	.haddr = {
		.mask = 0xffffU,
		.regoff = 0x8U,
	},
	.buflen = {
		.mask = 0xffffffffU,
		.regoff = 0x0U,
	},
	.bd_regoff = AIEML_SHIMNOC_BD0_0_REGOFF,
	.num_bds = 16,
	.bd_len = 0x20U,
};

static u32 aieml_get_tile_type(struct aie_device *adev,
			       struct aie_location *loc)
{
	u8 num_mem_rows = adev->ttype_attr[AIE_TILE_TYPE_MEMORY].num_rows;

	if (loc->row > num_mem_rows)
		return AIE_TILE_TYPE_TILE;
	if (loc->row && loc->row <= num_mem_rows)
		return AIE_TILE_TYPE_MEMORY;
	if (loc->row == 0)
		if ((loc->col % 4) < 2)
			return AIE_TILE_TYPE_SHIMPL;

	return AIE_TILE_TYPE_SHIMNOC;
}

static unsigned int aieml_get_mem_info(struct aie_device *adev,
				       struct aie_range *range,
				       struct aie_part_mem *pmem)
{
	unsigned int i;
	u8 start_row, num_rows;

	if (range->start.row + range->size.row <= 1) {
		/* SHIM row only, no memories in this range */
		return 0;
	}

	if (!pmem)
		return NUM_TYPES_OF_MEM;

	for (i = 0; i < NUM_TYPES_OF_MEM; i++) {
		struct aie_mem *mem = &pmem[i].mem;

		memcpy(&mem->range, range, sizeof(*range));
	}

	start_row = adev->ttype_attr[AIE_TILE_TYPE_TILE].start_row;
	num_rows = adev->ttype_attr[AIE_TILE_TYPE_TILE].num_rows;
	/* Setup tile data memory information */
	pmem[0].mem.offset = 0;
	pmem[0].mem.size = KBYTES(64);
	pmem[0].mem.range.start.row = start_row;
	pmem[0].mem.range.size.row = num_rows;

	/* Setup program memory information */
	pmem[1].mem.offset = 0x20000;
	pmem[1].mem.size = KBYTES(16);
	pmem[1].mem.range.start.row = start_row;
	pmem[1].mem.range.size.row = num_rows;

	start_row = adev->ttype_attr[AIE_TILE_TYPE_MEMORY].start_row;
	num_rows = adev->ttype_attr[AIE_TILE_TYPE_MEMORY].num_rows;
	/* Setup memory tile memory information */
	pmem[2].mem.offset = 0;
	pmem[2].mem.size = KBYTES(512);
	pmem[2].mem.range.start.row = start_row;
	pmem[2].mem.range.size.row = num_rows;

	return NUM_TYPES_OF_MEM;
}

static int aieml_reset_shim(struct aie_aperture *aperture,
			    struct aie_range *range)
{
	u32 node_id = aperture->adev->pm_node_id;
	int ret;

	ret = zynqmp_pm_aie_operation(node_id, range->start.col,
				      range->size.col,
				      XILINX_AIE_OPS_COL_RST |
				      XILINX_AIE_OPS_SHIM_RST);
	if (ret < 0)
		dev_err(&aperture->dev,
			"failed to perform shim and column reset.\n");

	return ret;
}

static int aieml_init_part_clk_state(struct aie_partition *apart)
{
	int ret, num_tiles;

	num_tiles = apart->range.size.col * apart->range.size.row - 1;

	ret = aie_resource_initialize(&apart->cores_clk_state, num_tiles);
	if (ret) {
		dev_err(&apart->dev,
			"failed to initialize tiles clock state resource.\n");
		return ret;
	}

	ret = aie_resource_initialize(&apart->tiles_inuse, num_tiles);
	if (ret)
		dev_err(&apart->dev,
			"failed to initialize tiles in use resource.\n");

	return ret;
}

static int aieml_scan_part_clocks(struct aie_partition *apart)
{
	struct aie_device *adev = apart->adev;
	struct aie_aperture *aperture = apart->aperture;
	struct aie_range *range = &apart->range;
	struct aie_location loc;

	/* Clear the bitmap of cores and memories clock state */
	aie_resource_put_region(&apart->cores_clk_state, 0,
				apart->cores_clk_state.total);

	/*
	 * In aieml if clock buffer on shim tile is enabled, the clock for all
	 * tiles in the same column is enabled.
	 */

	loc.row = 0;
	for (loc.col = range->start.col;
	     loc.col < range->start.col + range->size.col;
	     loc.col++) {
		void __iomem *va;
		u32 val, nbitpos;

		nbitpos = loc.col * (range->size.row - 1) + loc.row;

		va = aperture->base +
		     aie_cal_regoff(adev, loc,
				    AIEML_SHIMPL_COLCLOCK_CTRL_REGOFF);
		val = ioread32(va);

		if (!(val & AIEML_SHIMPL_COLCLOCK_CTRL_MASK))
			continue;

		aie_resource_set(&apart->cores_clk_state, nbitpos,
				 range->size.row - 1);
	}
	/*
	 * Set the tiles in use bitmap.
	 * In case of scanning, tiles which are powered on are considered as
	 * tiles in use.
	 */
	bitmap_copy(apart->tiles_inuse.bitmap, apart->cores_clk_state.bitmap,
		    apart->tiles_inuse.total);

	return 0;
}

static int aieml_set_part_clocks(struct aie_partition *apart)
{
	struct aie_range *range = &apart->range;
	u32 node_id = apart->adev->pm_node_id;
	int ret;

	ret = zynqmp_pm_aie_operation(node_id, range->start.col,
				      range->size.col,
				      XILINX_AIE_OPS_ENB_COL_CLK_BUFF);
	if (ret < 0)
		dev_err(&apart->dev, "failed to enable clocks for partition\n");

	return ret;
}

static int aieml_part_clear_mems(struct aie_partition *apart)
{
	struct aie_range *range = &apart->range;
	u32 node_id = apart->adev->pm_node_id;
	int ret;

	ret = zynqmp_pm_aie_operation(node_id, range->start.col,
				      range->size.col,
				      XILINX_AIE_OPS_ZEROISATION);
	if (ret < 0)
		dev_err(&apart->dev, "failed to clear memory for partition\n");

	return ret;
}

static const struct aie_tile_operations aieml_ops = {
	.get_tile_type = aieml_get_tile_type,
	.get_mem_info = aieml_get_mem_info,
	.reset_shim = aieml_reset_shim,
	.init_part_clk_state = aieml_init_part_clk_state,
	.scan_part_clocks = aieml_scan_part_clocks,
	.set_part_clocks = aieml_set_part_clocks,
	.mem_clear = aieml_part_clear_mems,
};

int aieml_device_init(struct aie_device *adev)
{
	adev->array_shift = AIEML_ARRAY_SHIFT;
	adev->col_shift = AIEML_COL_SHIFT;
	adev->row_shift = AIEML_ROW_SHIFT;
	adev->ops = &aieml_ops;
	adev->num_kernel_regs = ARRAY_SIZE(aieml_kernel_regs);
	adev->kernel_regs = aieml_kernel_regs;
	adev->num_core_regs = ARRAY_SIZE(aieml_core_regs);
	adev->core_regs = aieml_core_regs;
	adev->col_rst = &aieml_col_rst;
	adev->col_clkbuf = &aieml_col_clkbuf;
	adev->shim_dma = &aieml_shimdma;

	return 0;
}
