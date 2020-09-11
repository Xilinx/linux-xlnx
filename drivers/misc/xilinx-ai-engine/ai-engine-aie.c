// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine driver AIE device specific implementation
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include <linux/bitfield.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/xlnx-ai-engine.h>

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
#define AIE_SHIMPL_COLRESET_REGOFF		0x00036048U
#define AIE_SHIMPL_RESET_REGOFF			0x0003604cU
#define AIE_SHIMPL_GROUP_ERROR_REGOFF		0x0003450cU
#define AIE_TILE_CORE_CLKCNTR_REGOFF		0x00036040U
#define AIE_TILE_CORE_GROUP_ERROR_REGOFF	0x00034510U
#define AIE_TILE_MEM_GROUP_ERROR_REGOFF		0x00014514U

/*
 * Register masks
 */
#define AIE_SHIMPL_SHIMRST_MASK			0x1U
#define AIE_SHIMPL_COLRST_MASK			0x1U
#define AIE_SHIMPL_CLKCNTR_COLBUF_MASK		0x1U
#define AIE_SHIMPL_CLKCNTR_NEXTCLK_MASK		BIT(1)
#define AIE_TILE_CLKCNTR_COLBUF_MASK		BIT(0)
#define AIE_TILE_CLKCNTR_NEXTCLK_MASK		BIT(1)

/*
 * AI engine SHIM reset ID.
 * TODO: it should follow the Linux reset framework. The ID should be in the
 * device tree. However, as versal resets is not ready, we hardcode it in the
 * driver.
 */
#define VERSAL_PM_RST_AIE_SHIM_ID			0xc10405fU

static const struct aie_tile_regs aie_kernel_regs[] = {
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
	{.attribute = (AIE_TILE_TYPE_SHIMPL | AIE_TILE_TYPE_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_L1INTR_MASK_A_REGOFF,
	 .eoff = AIE_SHIMPL_L1INTR_BLOCK_NORTH_B_REGOFF,
	},
	/* SHIM column reset */
	{.attribute = (AIE_TILE_TYPE_SHIMPL | AIE_TILE_TYPE_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_COLRESET_REGOFF,
	 .eoff = AIE_SHIMPL_COLRESET_REGOFF,
	},
	/* SHIM reset Enable */
	{.attribute = (AIE_TILE_TYPE_SHIMPL | AIE_TILE_TYPE_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_RESET_REGOFF,
	 .eoff = AIE_SHIMPL_RESET_REGOFF,
	},
	/* SHIM clock control */
	{.attribute = (AIE_TILE_TYPE_SHIMPL | AIE_TILE_TYPE_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_CLKCNTR_REGOFF,
	 .eoff = AIE_SHIMPL_CLKCNTR_REGOFF,
	},
	/* SHIM group error enable */
	{.attribute = (AIE_TILE_TYPE_SHIMPL | AIE_TILE_TYPE_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_GROUP_ERROR_REGOFF,
	 .eoff = AIE_SHIMPL_GROUP_ERROR_REGOFF,
	},
	/* Tile clock control */
	{.attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_TILE_CORE_CLKCNTR_REGOFF,
	 .eoff = AIE_TILE_CORE_CLKCNTR_REGOFF,
	},
	/* Tile group error for core module */
	{.attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_TILE_CORE_GROUP_ERROR_REGOFF,
	 .eoff = AIE_TILE_CORE_GROUP_ERROR_REGOFF,
	},
	/* Tile group error for memory module */
	{.attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_TILE_MEM_GROUP_ERROR_REGOFF,
	 .eoff = AIE_TILE_MEM_GROUP_ERROR_REGOFF,
	},
};

static const struct aie_single_reg_field aie_col_rst = {
	.mask = AIE_SHIMPL_COLRST_MASK,
	.regoff = AIE_SHIMPL_COLRESET_REGOFF,
};

static const struct aie_single_reg_field aie_col_clkbuf = {
	.mask = AIE_SHIMPL_CLKCNTR_COLBUF_MASK,
	.regoff = AIE_SHIMPL_CLKCNTR_REGOFF,
};

static const struct aie_dma_attr aie_shimdma = {
	.laddr = {
		.mask = 0xffffffffU,
		.regoff = 0U,
	},
	.haddr = {
		.mask = 0xffff0000U,
		.regoff = 0x8U,
	},
	.buflen = {
		.mask = 0xffffffffU,
		.regoff = 0x4U,
	},
	.bd_regoff = 0x0001d000U,
	.num_bds = 16,
	.bd_len = 0x14U,
};

static const struct aie_event_attr aie_pl_event = {
	.bc_event = {
		.mask = GENMASK(6, 0),
		.regoff = 0x0U,
	},
	.group_error = {
		.mask = GENMASK(10, 0),
		.regoff = 0xcU,
	},
	.bc_regoff = 0x34010U,
	.status_regoff = 0x34200U,
	.group_regoff = 0x34500U,
	.base_error_event = 62U,
	.num_broadcasts = 16U,
	.base_bc_event = 107U,
	.num_events = 128U,
};

static const struct aie_event_attr aie_mem_event = {
	.bc_event = {
		.mask = GENMASK(6, 0),
		.regoff = 0x0U,
	},
	.group_error = {
		.mask = GENMASK(13, 0),
		.regoff = 0x14U,
	},
	.bc_regoff = 0x14010U,
	.status_regoff = 0x14200U,
	.group_regoff = 0x14500U,
	.base_error_event = 87U,
	.num_broadcasts = 16U,
	.base_bc_event = 107U,
	.num_events = 128U,
};

static const struct aie_event_attr aie_core_event = {
	.bc_event = {
		.mask = GENMASK(6, 0),
		.regoff = 0x0U,
	},
	.group_error = {
		.mask = GENMASK(21, 0),
		.regoff = 0x10U,
	},
	.bc_regoff = 0x34010U,
	.status_regoff = 0x34200U,
	.group_regoff = 0x34500U,
	.base_error_event = 48U,
	.num_broadcasts = 16U,
	.base_bc_event = 107U,
	.num_events = 128U,
};

static const struct aie_l1_intr_ctrl_attr aie_l1_intr_ctrl = {
	.swa_status = {
		.mask = GENMASK(19, 0),
		.regoff = 0xcU,
	},
	.swb_status = {
		.mask = GENMASK(19, 0),
		.regoff = 0x3cU,
	},
	.swa_event = {
		.mask = GENMASK(6, 0),
		.regoff = 0x14U,
	},
	.swb_event = {
		.mask = GENMASK(6, 0),
		.regoff = 0x44U,
	},
	.regoff = 0x35000U,
	.event_lsb = 8,
	.num_broadcasts = 0x14U,
};

static const struct aie_l2_intr_ctrl_attr aie_l2_intr_ctrl = {
	.mask = {
		.mask = GENMASK(15, 0),
		.regoff = 0x0U,
	},
	.enable = {
		.mask = GENMASK(15, 0),
		.regoff = 0x4U,
	},
	.disable = {
		.mask = GENMASK(15, 0),
		.regoff = 0x8U,
	},
	.status = {
		.mask = GENMASK(15, 0),
		.regoff = 0xcU,
	},
	.regoff = 0x15000U,
	.num_broadcasts = 0x10U,
};

static const struct aie_event_prop aie_core_stream_error_prop[] = {
	{
		.event = 54U,
		.event_str = "CORE: TLAST in WSS words 0-2",
	},
	{
		.event = 57U,
		.event_str = "CORE: Control packet error",
	},
	{
		.event = 56U,
		.event_str = "CORE: Stream packet parity error",
	},
};

static const struct aie_event_prop aie_core_inst_error_prop[] = {
	{
		.event = 59U,
		.event_str = "CORE: Instruction decompression error",
	},
};

static const struct aie_event_prop aie_core_ecc_error_prop[] = {
	{
		.event = 64U,
		.event_str = "CORE: PM ECC error 2-bit",
	},
	{
		.event = 62U,
		.event_str = "CORE: PM ECC error scrub 2-bit",
	},
};

static const struct aie_event_prop aie_core_access_error_prop[] = {
	{
		.event = 55U,
		.event_str = "CORE: PM reg access failure",
	},
	{
		.event = 66U,
		.event_str = "CORE: DM access to unavailable",
	},
	{
		.event = 65U,
		.event_str = "CORE: PM address out of range",
	},
	{
		.event = 60U,
		.event_str = "CORE: DM address out of range",
	},
};

static const struct aie_event_prop aie_core_lock_error_prop[] = {
	{
		.event = 67U,
		.event_str = "CORE: Lock access to unavailable",
	},
};

static const struct aie_event_prop aie_core_bus_error_prop[] = {
	{
		.event = 58U,
		.event_str = "CORE: AXI-MM slave error",
	},
};

static const struct aie_event_prop aie_mem_ecc_error_prop[] = {
	{
		.event = 88U,
		.event_str = "MEM: DM ECC error scrub 2-bit",
	},
	{
		.event = 90U,
		.event_str = "MEM: DM ECC error 2-bit",
	},
};

static const struct aie_event_prop aie_mem_parity_error_prop[] = {
	{
		.event = 91U,
		.event_str = " MEM: DM parity error bank 2",
	},
	{
		.event = 92U,
		.event_str = " MEM: DM parity error bank 3",
	},
	{
		.event = 93U,
		.event_str = " MEM: DM parity error bank 4",
	},
	{
		.event = 94U,
		.event_str = " MEM: DM parity error bank 5",
	},
	{
		.event = 95U,
		.event_str = " MEM: DM parity error bank 6",
	},
	{
		.event = 96U,
		.event_str = " MEM: DM parity error bank 7",
	},
};

static const struct aie_event_prop aie_mem_dma_error_prop[] = {
	{
		.event = 97U,
		.event_str = " MEM: DMA S2MM 0 error",
	},
	{
		.event = 98U,
		.event_str = " MEM: DMA S2MM 1 error",
	},
	{
		.event = 99U,
		.event_str = " MEM: DMA MM2S 0 error",
	},
	{
		.event = 100U,
		.event_str = " MEM: DMA MM2S 1 error",
	},
};

static const struct aie_event_prop aie_shim_bus_error_prop[] = {
	{
		.event = 62U,
		.event_str = "SHIM: AXI-MM slave tile error",
	},
};

static const struct aie_event_prop aie_shim_stream_error_prop[] = {
	{
		.event = 63U,
		.event_str = "SHIM: Control packet error",
	},
	{
		.event = 64U,
		.event_str = "SHIM: AXI-MM decode NSU error",
	},
	{
		.event = 65U,
		.event_str = "SHIM: AXI-MM slave NSU error",
	},
	{
		.event = 66U,
		.event_str = "SHIM: AXI-MM unsupported traffic",
	},
	{
		.event = 67U,
		.event_str = "SHIM: AXI-MM unsecure access in secure mode",
	},
	{
		.event = 68U,
		.event_str = "SHIM: AXI-MM byte strobe error",
	},
};

static const struct aie_event_prop aie_shim_dma_error_prop[] = {
	{
		.event = 69U,
		.event_str = "SHIM: DMA S2MM 0 error",
	},
	{
		.event = 70U,
		.event_str = "SHIM: DMA S2MM 1 error",
	},
	{
		.event = 71U,
		.event_str = "SHIM: DMA MM2S 0 error",
	},
	{
		.event = 72U,
		.event_str = "SHIM: DMA MM2S 1 error",
	},
};

static const struct aie_err_category aie_core_err_category[] = {
	{
		/* AIE_ERROR_CATEGORY_STREAM */
		.err_category = AIE_ERROR_CATEGORY_STREAM,
		.num_events = ARRAY_SIZE(aie_core_stream_error_prop),
		.prop = aie_core_stream_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_ACCESS */
		.err_category = AIE_ERROR_CATEGORY_ACCESS,
		.num_events = ARRAY_SIZE(aie_core_access_error_prop),
		.prop = aie_core_access_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_BUS */
		.err_category = AIE_ERROR_CATEGORY_BUS,
		.num_events = ARRAY_SIZE(aie_core_bus_error_prop),
		.prop = aie_core_bus_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_INSTRUCTION */
		.err_category = AIE_ERROR_CATEGORY_INSTRUCTION,
		.num_events = ARRAY_SIZE(aie_core_inst_error_prop),
		.prop = aie_core_inst_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_ECC */
		.err_category = AIE_ERROR_CATEGORY_ECC,
		.num_events = ARRAY_SIZE(aie_core_ecc_error_prop),
		.prop = aie_core_ecc_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_LOCK */
		.err_category = AIE_ERROR_CATEGORY_LOCK,
		.num_events = ARRAY_SIZE(aie_core_lock_error_prop),
		.prop = aie_core_lock_error_prop,
	},
};

static const struct aie_err_category aie_mem_err_category[] = {
	{
		/* AIE_ERROR_CATEGORY_ECC */
		.err_category = AIE_ERROR_CATEGORY_ECC,
		.num_events = ARRAY_SIZE(aie_mem_ecc_error_prop),
		.prop = aie_mem_ecc_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_MEM_PARITY */
		.err_category = AIE_ERROR_CATEGORY_MEM_PARITY,
		.num_events = ARRAY_SIZE(aie_mem_parity_error_prop),
		.prop = aie_mem_parity_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_DMA */
		.err_category = AIE_ERROR_CATEGORY_DMA,
		.num_events = ARRAY_SIZE(aie_mem_dma_error_prop),
		.prop = aie_mem_dma_error_prop,
	},
};

static const struct aie_err_category aie_shim_err_category[] = {
	{
		/* AIE_ERROR_CATEGORY_BUS */
		.err_category = AIE_ERROR_CATEGORY_BUS,
		.num_events = ARRAY_SIZE(aie_shim_bus_error_prop),
		.prop = aie_shim_bus_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_STREAM */
		.err_category = AIE_ERROR_CATEGORY_STREAM,
		.num_events = ARRAY_SIZE(aie_shim_stream_error_prop),
		.prop = aie_shim_stream_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_DMA */
		.err_category = AIE_ERROR_CATEGORY_DMA,
		.num_events = ARRAY_SIZE(aie_shim_dma_error_prop),
		.prop = aie_shim_dma_error_prop,
	},
};

static const struct aie_error_attr aie_core_error = {
	.num_err_categories = ARRAY_SIZE(aie_core_err_category),
	.err_category = aie_core_err_category,
};

static const struct aie_error_attr aie_mem_error = {
	.num_err_categories = ARRAY_SIZE(aie_mem_err_category),
	.err_category = aie_mem_err_category,
};

static const struct aie_error_attr aie_shim_error = {
	.num_err_categories = ARRAY_SIZE(aie_shim_err_category),
	.err_category = aie_shim_err_category,
};

static u32 aie_get_tile_type(struct aie_location *loc)
{
	if (loc->row)
		return AIE_TILE_TYPE_TILE;
	/* SHIM row */
	if ((loc->col % 4) < 2)
		return AIE_TILE_TYPE_SHIMPL;

	return AIE_TILE_TYPE_SHIMNOC;
}

static unsigned int aie_get_mem_info(struct aie_range *range,
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
 * aie_set_shim_reset() - Set AI engine SHIM reset
 * @adev: AI engine device
 * @range: range of AI engine tiles
 * @assert: true to set reset, false to unset reset
 */
static void aie_set_shim_reset(struct aie_device *adev,
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

static int aie_reset_shim(struct aie_device *adev, struct aie_range *range)
{
	int ret;

	/* Enable shim reset of each column */
	aie_set_shim_reset(adev, range, true);

	/* Assert shim reset of AI engine array */
	ret = adev->eemi_ops->reset_assert(VERSAL_PM_RST_AIE_SHIM_ID,
					   PM_RESET_ACTION_ASSERT);
	if (ret < 0) {
		dev_err(&adev->dev, "failed to assert SHIM reset.\n");
		return ret;
	}

	/* Release shim reset of AI engine array */
	ret = adev->eemi_ops->reset_assert(VERSAL_PM_RST_AIE_SHIM_ID,
					   PM_RESET_ACTION_RELEASE);
	if (ret < 0) {
		dev_err(&adev->dev, "failed to release SHIM reset.\n");
		return ret;
	}

	/* Disable shim reset of each column */
	aie_set_shim_reset(adev, range, false);

	return 0;
}

static int aie_init_part_clk_state(struct aie_partition *apart)
{
	int ret, num_tiles;

	num_tiles = apart->range.size.col * (apart->range.size.row - 1);

	ret = aie_resource_initialize(&apart->cores_clk_state, num_tiles);
	if (ret) {
		dev_err(&apart->dev,
			"failed to initialize cores clock state resource.\n");
		return ret;
	}

	ret = aie_resource_initialize(&apart->tiles_inuse, num_tiles);
	if (ret) {
		dev_err(&apart->dev,
			"failed to initialize tiles in use resource.\n");
		return ret;
	}

	return 0;
}

static int aie_scan_part_clocks(struct aie_partition *apart)
{
	struct aie_device *adev = apart->adev;
	struct aie_range *range = &apart->range;
	struct aie_location loc;

	/* Clear the bitmap of cores and memories clock state */
	aie_resource_put_region(&apart->cores_clk_state, 0,
				apart->cores_clk_state.total);

	for (loc.col = range->start.col;
	     loc.col < range->start.col + range->size.col;
	     loc.col++) {
		for (loc.row = range->start.row;
		     loc.row < range->start.row + range->size.row - 1;
		     loc.row++) {
			void __iomem *va;
			u32 val, nbitpos;

			/*
			 * Reading registers of the current tile to see the next
			 * tile is clock gated.
			 */
			nbitpos = loc.col * (range->size.row - 1) + loc.row;

			if (aie_get_tile_type(&loc) != AIE_TILE_TYPE_TILE) {
				/* Checks shim tile for next core tile */
				va = adev->base +
				     aie_cal_regoff(adev, loc,
						    AIE_SHIMPL_CLKCNTR_REGOFF);
				val = ioread32(va);

				/*
				 * check if the clock buffer and the next clock
				 * tile is set, if one of them is not set, the
				 * tiles of the column are clock gated.
				 */
				if (!(val & AIE_SHIMPL_CLKCNTR_COLBUF_MASK) ||
				    !(val & AIE_SHIMPL_CLKCNTR_NEXTCLK_MASK))
					break;

				/* Set next tile in the row clock state on */
				aie_resource_set(&apart->cores_clk_state,
						 nbitpos, 1);
				continue;
			}

			/* Checks core tile for next tile */
			va = adev->base +
			     aie_cal_regoff(adev, loc,
					    AIE_TILE_CORE_CLKCNTR_REGOFF);
			val = ioread32(va);

			/*
			 * If the next tile is gated, skip the rest of the
			 * column.
			 */
			if (!(val & AIE_TILE_CLKCNTR_NEXTCLK_MASK))
				break;

			aie_resource_set(&apart->cores_clk_state, nbitpos, 1);
		}
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

/* aie_set_col_clocks() - set clocks of a range of tiles of a column
 * @apart: AI engine partition
 * @range: range of tiles of a column
 * @enable: true to enable the clock, false to disable
 * @return: 0 for success, negative value of errors.
 */
static int aie_set_col_clocks(struct aie_partition *apart,
			      struct aie_range *range, bool enable)
{
	struct aie_location ploc;
	u32 startbit;

	/*
	 * check if the range is of single colum. only single column is allowed.
	 * check if the start row is tile row, only tile rows are allowed.
	 */
	if (range->size.col != 1 || range->start.row < 1)
		return -EINVAL;

	ploc.col = range->start.col;
	for (ploc.row = range->start.row - 1;
	     ploc.row < range->start.row + range->size.row - 1;
	     ploc.row++) {
		struct aie_device *adev = apart->adev;

		if (!ploc.row) {
			void __iomem *va;
			u32 val = 0;

			/*
			 * Configure SHIM clock registers to gate or
			 * ungate next tile.
			 */
			if (enable)
				val = AIE_SHIMPL_CLKCNTR_COLBUF_MASK |
				      AIE_SHIMPL_CLKCNTR_NEXTCLK_MASK;
			va = adev->base +
			     aie_cal_regoff(adev, ploc,
					    AIE_SHIMPL_CLKCNTR_REGOFF);
			iowrite32(val, va);
		} else {
			void __iomem *va;
			u32 val = 0;

			/*
			 * Configure core tile clock registers to gate
			 * or ungate next tile.
			 */
			if (enable)
				val = AIE_TILE_CLKCNTR_COLBUF_MASK |
				      AIE_TILE_CLKCNTR_NEXTCLK_MASK;
			va = adev->base +
			     aie_cal_regoff(adev, ploc,
					    AIE_TILE_CORE_CLKCNTR_REGOFF);
			iowrite32(val, va);
		}

		/* If the tile clock is not on, jump to next column */
		if (!enable)
			break;
	}

	/* Update clock state bitmap */
	startbit = range->start.col * (apart->range.size.row - 1) +
		   range->start.row - 1;
	if (enable)
		aie_resource_set(&apart->cores_clk_state, startbit,
				 range->size.row);
	else
		aie_resource_clear(&apart->cores_clk_state, startbit,
				   range->size.row);

	return 0;
}

static int aie_set_part_clocks(struct aie_partition *apart)
{
	struct aie_range *range = &apart->range, lrange;
	struct aie_location loc;

	/*
	 * The tiles below the highest tile whose clock is on, need to have the
	 * clock on. The first for loop is to scan the clock states bitmap to
	 * see which tiles are required to be clocked on, and update the bitmap
	 * to make sure the tiles below are also required to be clocked on.
	 */
	for (loc.col = range->start.col;
	     loc.col < range->start.col + range->size.col;
	     loc.col++) {
		u32 startbit, inuse_toprow = 0, clk_toprow = 0;

		startbit = loc.col * (range->size.row - 1);

		for (loc.row = range->start.row + 1;
		     loc.row < range->start.row + range->size.row;
		     loc.row++) {
			u32 bit = startbit + loc.row - 1;

			if (aie_resource_testbit(&apart->tiles_inuse, bit))
				inuse_toprow = loc.row;
			if (aie_resource_testbit(&apart->cores_clk_state, bit))
				clk_toprow = loc.row;
		}

		/* Update clock states of a column */
		lrange.start.col = loc.col;
		lrange.size.col = 1;
		if (inuse_toprow < clk_toprow) {
			lrange.start.row = inuse_toprow + 1;
			lrange.size.row = clk_toprow - inuse_toprow;
			aie_set_col_clocks(apart, &lrange, false);
		} else  if (inuse_toprow > clk_toprow) {
			lrange.start.row = clk_toprow + 1;
			lrange.size.row = inuse_toprow - clk_toprow;
			aie_set_col_clocks(apart, &lrange, true);
		}
	}

	return 0;
}

static const struct aie_tile_operations aie_ops = {
	.get_tile_type = aie_get_tile_type,
	.get_mem_info = aie_get_mem_info,
	.reset_shim = aie_reset_shim,
	.init_part_clk_state = aie_init_part_clk_state,
	.scan_part_clocks = aie_scan_part_clocks,
	.set_part_clocks = aie_set_part_clocks,
};

/**
 * aie_device_init() - Initialize AI engine device struct AIE specific
 * @adev: AI engine device
 * @return: 0 for success, negative value for failure.
 *
 * This function initialize the AI engine device structure device version
 * specific elements such as register addressing related array shift,
 * column shift, and row shift; AIE device specific device operations, device
 * columns resource.
 */
int aie_device_init(struct aie_device *adev)
{
	int ret;

	adev->array_shift = AIE_ARRAY_SHIFT;
	adev->col_shift = AIE_COL_SHIFT;
	adev->row_shift = AIE_ROW_SHIFT;
	adev->ops = &aie_ops;
	adev->num_kernel_regs = ARRAY_SIZE(aie_kernel_regs);
	adev->kernel_regs = aie_kernel_regs;
	adev->col_rst = &aie_col_rst;
	adev->col_clkbuf = &aie_col_clkbuf;
	adev->shim_dma = &aie_shimdma;
	adev->pl_events = &aie_pl_event;
	adev->mem_events = &aie_mem_event;
	adev->core_events = &aie_core_event;
	adev->l1_ctrl = &aie_l1_intr_ctrl;
	adev->l2_ctrl = &aie_l2_intr_ctrl;
	adev->core_errors = &aie_core_error;
	adev->mem_errors = &aie_mem_error;
	adev->shim_errors = &aie_shim_error;

	/* Get the columns resource */
	/* Get number of columns from AI engine memory resource */
	ret = aie_resource_initialize(&adev->cols_res, 50);
	if (ret)
		dev_err(&adev->dev, "failed to initialize columns resource.\n");

	return ret;
}
