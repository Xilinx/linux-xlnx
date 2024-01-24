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

#define AIE_ARRAY_SHIFT		30U
#define AIE_COL_SHIFT		23U
#define AIE_ROW_SHIFT		18U

#define NUM_MEMS_PER_TILE	2U

#define NUM_MODS_CORE_TILE	2U
#define NUM_MODS_SHIMPL_TILE	1U

#define NUM_UTIL_EVENTS		4U

/*
 * Number of resources per module
 */
#define AIE_NUM_PERF_CORE_MOD		4U
#define AIE_NUM_USEREVENT_CORE_MOD	4U
#define AIE_NUM_TRACECONTROL_CORE_MOD	1U
#define AIE_NUM_PCEVENT_CORE_MOD	4U
#define AIE_NUM_SSSELECT_CORE_MOD	8U
#define AIE_NUM_BROADCAST_CORE_MOD	16U
#define AIE_NUM_COMBOEVENT_CORE_MOD	4U
#define AIE_NUM_GROUPEVENTS_CORE_MOD	9U

#define AIE_NUM_PERF_MEM_MOD		2U
#define AIE_NUM_USEREVENT_MEM_MOD	4U
#define AIE_NUM_TRACECONTROL_MEM_MOD	1U
#define AIE_NUM_PCEVENT_MEM_MOD		0U
#define AIE_NUM_SSSELECT_MEM_MOD	0U
#define AIE_NUM_BROADCAST_MEM_MOD	16U
#define AIE_NUM_COMBOEVENT_MEM_MOD	4U
#define AIE_NUM_GROUPEVENTS_MEM_MOD	8U

#define AIE_NUM_PERF_PL_MOD		2U
#define AIE_NUM_USEREVENT_PL_MOD	4U
#define AIE_NUM_TRACECONTROL_PL_MOD	1U
#define AIE_NUM_PCEVENT_PL_MOD		0U
#define AIE_NUM_SSSELECT_PL_MOD		8U
#define AIE_NUM_BROADCAST_PL_MOD	16U
#define AIE_NUM_COMBOEVENT_PL_MOD	4U
#define AIE_NUM_GROUPEVENTS_PL_MOD	7U

/*
 * Registers offsets
 */
#define AIE_SHIMNOC_L2INTR_MASK_REGOFF		0x00015000U
#define AIE_SHIMNOC_L2INTR_INTR_REGOFF		0x00015010U
#define AIE_SHIMNOC_DMA_BD0_ADDRLOW_REGOFF	0x0001d000U
#define AIE_SHIMNOC_DMA_BD15_PACKET_REGOFF	0x0001d13cU
#define AIE_SHIMNOC_AXIMM_REGOFF		0x0001e020U
#define AIE_SHIMPL_BISR_CACHE_CTRL_REGOFF	0x00036000U
#define AIE_SHIMPL_L1INTR_MASK_A_REGOFF		0x00035000U
#define AIE_SHIMPL_L1INTR_BLOCK_NORTH_B_REGOFF	0x00035050U
#define AIE_SHIMPL_TILECTRL_REGOFF		0x00036030U
#define AIE_SHIMPL_CLKCNTR_REGOFF		0x00036040U
#define AIE_SHIMPL_COLRESET_REGOFF		0x00036048U
#define AIE_SHIMPL_RESET_REGOFF			0x0003604cU
#define AIE_SHIMPL_GROUP_ERROR_REGOFF		0x0003450cU
#define AIE_TILE_MEM_DMA_BD0_ADDR_A		0x0001D000U
#define AIE_TILE_CORE_TILECTRL_REGOFF		0x00036030U
#define AIE_TILE_CORE_CLKCNTR_REGOFF		0x00036040U
#define AIE_TILE_CORE_GROUP_ERROR_REGOFF	0x00034510U
#define AIE_TILE_MEM_GROUP_ERROR_REGOFF		0x00014514U
#define AIE_TILE_CORE_R0_REGOFF			0x00030000U
#define AIE_TILE_CORE_LC_REGOFF			0x00030520U
#define AIE_TILE_CORE_VRL0_REGOFF		0x00030530U
#define AIE_TILE_CORE_AMH3_PART3_REGOFF		0x000307a0U
#define AIE_TILE_CORE_PERFCTRL_REGOFF		0x00031000U
#define AIE_TILE_CORE_PERFCTRL_RESET_REGOFF	0x00031008U
#define AIE_TILE_CORE_PERFCNT0_REGOFF		0x00031020U
#define AIE_TILE_CORE_EVNTGEN_REGOFF		0x00034008U

/*
 * Register masks
 */
#define AIE_SHIMPL_SHIMRST_MASK			0x1U
#define AIE_SHIMPL_COLRST_MASK			0x1U
#define AIE_SHIMPL_CLKCNTR_COLBUF_MASK		0x1U
#define AIE_SHIMPL_CLKCNTR_NEXTCLK_MASK		BIT(1)
#define AIE_TILE_CLKCNTR_COLBUF_MASK		BIT(0)
#define AIE_TILE_CLKCNTR_NEXTCLK_MASK		BIT(1)
#define AIE_TILE_PERFCTRL_CNT0_MASK		0x7F7FU
#define AIE_TILE_PERFCTRL_RESET_MASK		0x7FU
#define AIE_TILE_CORE_PERFCNT0_MASK		0xFFFFFFFFU
#define AIE_TILE_CORE_EVNTGEN_MASK		0x7F

/*
 * AI engine SHIM reset ID.
 * TODO: it should follow the Linux reset framework. The ID should be in the
 * device tree. However, as versal resets is not ready, we hardcode it in the
 * driver.
 */
#define VERSAL_PM_RST_AIE_SHIM_ID			0xc10405fU

/* Macros to define size of a sysfs binary attribute */
#define AIE_PART_SYSFS_CORE_BINA_SIZE		0x4000		/* 16KB */
#define AIE_PART_SYSFS_DMA_BINA_SIZE		0xC800		/* 50KB */
#define AIE_PART_SYSFS_LOCK_BINA_SIZE		0x28000		/* 160KB */
#define AIE_PART_SYSFS_ERROR_BINA_SIZE		0x4000		/* 16KB */
#define AIE_PART_SYSFS_STATUS_BINA_SIZE		0x3c000		/* 240KB */

static const struct aie_tile_regs aie_kernel_regs[] = {
	/* SHIM AXI MM Config */
	{.attribute = AIE_TILE_TYPE_MASK_SHIMNOC << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMNOC_AXIMM_REGOFF,
	 .eoff = AIE_SHIMNOC_AXIMM_REGOFF,
	},
	/* SHIM DMA ADDRESS range */
	{.attribute = AIE_TILE_TYPE_MASK_SHIMNOC << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMNOC_DMA_BD0_ADDRLOW_REGOFF,
	 .eoff = AIE_SHIMNOC_DMA_BD15_PACKET_REGOFF,
	},
	/* SHIM 2nd level interrupt controller */
	{.attribute = AIE_TILE_TYPE_MASK_SHIMNOC << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMNOC_L2INTR_MASK_REGOFF,
	 .eoff = AIE_SHIMNOC_L2INTR_INTR_REGOFF,
	},
	/* SHIM 1st level interrupt controller */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_L1INTR_MASK_A_REGOFF,
	 .eoff = AIE_SHIMPL_L1INTR_BLOCK_NORTH_B_REGOFF,
	},
	/* SHIM column reset */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_COLRESET_REGOFF,
	 .eoff = AIE_SHIMPL_COLRESET_REGOFF,
	},
	/* SHIM reset Enable */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_RESET_REGOFF,
	 .eoff = AIE_SHIMPL_RESET_REGOFF,
	},
	/* SHIM tile control */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_TILECTRL_REGOFF,
	 .eoff = AIE_SHIMPL_TILECTRL_REGOFF,
	},
	/* SHIM clock control */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_CLKCNTR_REGOFF,
	 .eoff = AIE_SHIMPL_CLKCNTR_REGOFF,
	},
	/* SHIM BISR cache control */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_BISR_CACHE_CTRL_REGOFF,
	 .eoff = AIE_SHIMPL_BISR_CACHE_CTRL_REGOFF,
	},
	/* SHIM group error enable */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_SHIMPL_GROUP_ERROR_REGOFF,
	 .eoff = AIE_SHIMPL_GROUP_ERROR_REGOFF,
	},
	/* Core tile control */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_TILE_CORE_TILECTRL_REGOFF,
	 .eoff = AIE_TILE_CORE_TILECTRL_REGOFF,
	},
	/* Tile clock control */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_TILE_CORE_CLKCNTR_REGOFF,
	 .eoff = AIE_TILE_CORE_CLKCNTR_REGOFF,
	},
	/* Tile group error for core module */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_TILE_CORE_GROUP_ERROR_REGOFF,
	 .eoff = AIE_TILE_CORE_GROUP_ERROR_REGOFF,
	},
	/* Tile group error for memory module */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE_TILE_MEM_GROUP_ERROR_REGOFF,
	 .eoff = AIE_TILE_MEM_GROUP_ERROR_REGOFF,
	},
};

static const struct aie_tile_regs aie_core_32bit_regs = {
	.attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	.soff = AIE_TILE_CORE_R0_REGOFF,
	.eoff = AIE_TILE_CORE_LC_REGOFF,
};

static const struct aie_tile_regs aie_core_128bit_regs = {
	.attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	.soff = AIE_TILE_CORE_VRL0_REGOFF,
	.eoff = AIE_TILE_CORE_AMH3_PART3_REGOFF,
};

static const struct aie_core_regs_attr aie_core_regs[] = {
	{.core_regs = &aie_core_32bit_regs,
	 .width = 1,
	},
	{.core_regs = &aie_core_128bit_regs,
	 .width = 4,
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

static const struct aie_bd_attr aie_tilebd = {
	.valid_bd = {
		.mask = BIT(31),
		.regoff = 0x18U,
	},
	.next_bd = {
		.mask = GENMASK(16, 13),
		.regoff = 0x18U,
	},
	.use_next = {
		.mask = BIT(17),
		.regoff = 0x18U,
	},
	.addr = {
		.addr = {
			.mask = GENMASK(12, 0),
			.regoff = 0x0U,
		},
		.length = {
			.mask = GENMASK(12, 0),
			.regoff = 0x18U,
		},
	},
	.addr_2 = {
		.addr = {
			.mask = GENMASK(12, 0),
			.regoff = 0x4U,
		},
		.length = {
			.mask = GENMASK(12, 0),
			.regoff = 0x18U,
		},
	},
	.lock = {
		.lock_acq_id = {
			.mask = GENMASK(25, 22),
			.regoff = 0x0U,
		},
		.lock_acq_val = {
			.mask = BIT(17),
			.regoff = 0x0U,
		},
		.lock_acq_en = {
			.mask = BIT(18),
			.regoff = 0x0U,
		},
		.lock_acq_val_en = {
			.mask = BIT(16),
			.regoff = 0x0U,
		},
		.lock_rel_id = {
			.mask = GENMASK(25, 22),
			.regoff = 0x0U,
		},
		.lock_rel_val = {
			.mask = BIT(20),
			.regoff = 0x0U,
		},
		.lock_rel_en = {
			.mask = BIT(21),
			.regoff = 0x0U,
		},
		.lock_rel_val_en = {
			.mask = BIT(19),
			.regoff = 0x0U,
		},
	},
	.lock_2 = {
		.lock_acq_id = {
			.mask = GENMASK(25, 22),
			.regoff = 0x4U,
		},
		.lock_acq_val = {
			.mask = BIT(17),
			.regoff = 0x4U,
		},
		.lock_acq_en = {
			.mask = BIT(18),
			.regoff = 0x4U,
		},
		.lock_acq_val_en = {
			.mask = BIT(16),
			.regoff = 0x4U,
		},
		.lock_rel_id = {
			.mask = GENMASK(25, 22),
			.regoff = 0x4U,
		},
		.lock_rel_val = {
			.mask = BIT(20),
			.regoff = 0x4U,
		},
		.lock_rel_en = {
			.mask = BIT(21),
			.regoff = 0x4U,
		},
		.lock_rel_val_en = {
			.mask = BIT(19),
			.regoff = 0x4U,
		},
	},
	.packet = {
		.pkt_en = {
			.mask = BIT(27),
			.regoff = 0x18U,
		},
		.pkt_type = {
			.mask = GENMASK(14, 12),
			.regoff = 0x10U,
		},
		.pkt_id = {
			.mask = GENMASK(4, 0),
			.regoff = 0x10U,
		},
	},
	.aie_dim = {
		.x_incr = {
			.mask = GENMASK(31, 24),
			.regoff = 0x8U,
		},
		.x_wrap = {
			.mask = GENMASK(23, 16),
			.regoff = 0x8U,
		},
		.x_off = {
			.mask = GENMASK(12, 0),
			.regoff = 0x8U,
		},
		.y_incr = {
			.mask = GENMASK(31, 24),
			.regoff = 0xCU,
		},
		.y_wrap = {
			.mask = GENMASK(23, 16),
			.regoff = 0xCU,
		},
		.y_off = {
			.mask = GENMASK(12, 0),
			.regoff = 0xCU,
		},
	},
	.buf_sel = {
		.mask = BIT(16),
		.regoff = 0x14U,
	},
	.curr_ptr = {
		.mask = GENMASK(12, 0),
		.regoff = 0x14U,
	},
	.interleave_en = {
		.mask = BIT(26),
		.regoff = 0x18U,
	},
	.interleave_cnt = {
		.mask = GENMASK(25, 18),
		.regoff = 0x18U,
	},
	.double_buff_en = {
		.mask = BIT(30),
		.regoff = 0x18U,
	},
	.fifo_mode = {
		.mask = GENMASK(29, 28),
		.regoff = 0x18U,
	},
	.bd_idx_off = 0x20U,
};

static const struct aie_bd_attr aie_shimbd = {
	.valid_bd = {
		.mask = BIT(0),
		.regoff = 0x8U,
	},
	.next_bd = {
		.mask = GENMASK(14, 11),
		.regoff = 0x8U,
	},
	.use_next = {
		.mask = BIT(15),
		.regoff = 0x8U,
	},
	.addr = {
		.addr = {
			.mask = GENMASK(31, 0),
			.regoff = 0x0U,
		},
		.length = {
			.mask = GENMASK(31, 0),
			.regoff = 0x4U,
		},
	},
	.addr_2 = {
		.addr = {
			.mask = GENMASK(31, 16),
			.regoff = 0x8U,
		},
		.length = {
			.mask = GENMASK(31, 0),
			.regoff = 0x4U,
		},
	},
	.lock = {
		.lock_acq_id = {
			.mask = GENMASK(10, 7),
			.regoff = 0x8U,
		},
		.lock_acq_val = {
			.mask = BIT(2),
			.regoff = 0x8U,
		},
		.lock_acq_en = {
			.mask = BIT(3),
			.regoff = 0x8U,
		},
		.lock_acq_val_en = {
			.mask = BIT(1),
			.regoff = 0x8U,
		},
		.lock_rel_id = {
			.mask = GENMASK(10, 7),
			.regoff = 0x8U,
		},
		.lock_rel_val = {
			.mask = BIT(5),
			.regoff = 0x8U,
		},
		.lock_rel_en = {
			.mask = BIT(6),
			.regoff = 0x8U,
		},
		.lock_rel_val_en = {
			.mask = BIT(4),
			.regoff = 0x8U,
		},
	},
	.packet = {
		.pkt_en = {
			.mask = BIT(31),
			.regoff = 0x10U,
		},
		.pkt_type = {
			.mask = GENMASK(14, 12),
			.regoff = 0x10U,
		},
		.pkt_id = {
			.mask = GENMASK(4, 0),
			.regoff = 0x10U,
		},
	},
	.axi = {
		.smid = {
			.mask = GENMASK(31, 28),
			.regoff = 0xCU,
		},
		.cache = {
			.mask = GENMASK(3, 0),
			.regoff = 0xCU,
		},
		.qos = {
			.mask = GENMASK(8, 5),
			.regoff = 0xCU,
		},
		.secure_en = {
			.mask = BIT(4),
			.regoff = 0xCU,
		},
		.burst_len = {
			.mask = GENMASK(10, 9),
			.regoff = 0xCU,
		},
	},
	.bd_idx_off = 0x14U,
};

static const struct aie_single_reg_field aie_core_perfctrl = {
	.mask = AIE_TILE_PERFCTRL_CNT0_MASK,
	.regoff = AIE_TILE_CORE_PERFCTRL_REGOFF,
};

static const struct aie_single_reg_field aie_core_perfctrl_reset = {
	.mask = AIE_TILE_PERFCTRL_RESET_MASK,
	.regoff = AIE_TILE_CORE_PERFCTRL_RESET_REGOFF,
};

static const struct aie_single_reg_field aie_core_perfcnt = {
	.mask = AIE_TILE_CORE_PERFCNT0_MASK,
	.regoff = AIE_TILE_CORE_PERFCNT0_REGOFF,
};

static const struct aie_single_reg_field aie_core_evntgen = {
	.mask = AIE_TILE_CORE_EVNTGEN_MASK,
	.regoff = AIE_TILE_CORE_EVNTGEN_REGOFF,
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
	.sts = {
		.mask = GENMASK(1, 0),
		.regoff = 2U,
	},
	.stall = {
		.mask = BIT(4),
		.regoff = 1U,
	},
	.qsize = {
		.mask = GENMASK(8, 6),
		.regoff = 3U,
	},
	.curbd = {
		.mask = GENMASK(19, 16),
		.regoff = 4U,
	},
	.qsts = {
		.mask = BIT(28),
		.regoff = 1U,
	},
	.fifo_cnt = {
		.mask = GENMASK(12, 0),
		.regoff = 16U,
	},
	.bd_regoff = AIE_SHIMNOC_DMA_BD0_ADDRLOW_REGOFF,
	.mm2s_sts_regoff = 0x1d164U,
	.s2mm_sts_regoff = 0x1d160U,
	.fifo_cnt_regoff = 0x1DF20U,
	.num_bds = 16,
	.num_mm2s_chan = 2U,
	.num_s2mm_chan = 2U,
	.bd_len = 0x14U,
};

static const struct aie_dma_attr aie_tiledma = {
	.sts = {
		.mask = GENMASK(1, 0),
		.regoff = 2U,
	},
	.stall = {
		.mask = BIT(4),
		.regoff = 1U,
	},
	.qsize = {
		.mask = GENMASK(8, 6),
		.regoff = 3U,
	},
	.curbd = {
		.mask = GENMASK(19, 16),
		.regoff = 4U,
	},
	.qsts = {
		.mask = BIT(28),
		.regoff = 1U,
	},
	.bd_regoff = AIE_TILE_MEM_DMA_BD0_ADDR_A,
	.mm2s_sts_regoff = 0x1df10U,
	.s2mm_sts_regoff = 0x1df00U,
	.num_bds = 16,
	.num_mm2s_chan = 2U,
	.num_s2mm_chan = 2U,
	.bd_len = 0x1CU,
};

static char *aie_dma_status_str[] = {
	"idle",
	"starting",
	"running",
	"stalled_on_requesting_lock",
	"invalid_status",
};

static char *aie_queue_status_str[] = {
	"okay",
	"overflow",
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
		.event_str = "tlast_in_wss_words_0-2",
	},
	{
		.event = 57U,
		.event_str = "control_packet_error",
	},
	{
		.event = 56U,
		.event_str = "stream_packet_parity_error",
	},
};

static const struct aie_event_prop aie_core_inst_error_prop[] = {
	{
		.event = 59U,
		.event_str = "instruction_decompression_error",
	},
};

static const struct aie_event_prop aie_core_ecc_error_prop[] = {
	{
		.event = 64U,
		.event_str = "pm_ecc_error_2-bit",
	},
	{
		.event = 62U,
		.event_str = "pm_ecc_error_scrub_2-bit",
	},
};

static const struct aie_event_prop aie_core_access_error_prop[] = {
	{
		.event = 55U,
		.event_str = "pm_reg_access_failure",
	},
	{
		.event = 66U,
		.event_str = "dm_access_to_unavailable",
	},
	{
		.event = 65U,
		.event_str = "pm_address_out_of_range",
	},
	{
		.event = 60U,
		.event_str = "dm_address_out_of_range",
	},
};

static const struct aie_event_prop aie_core_lock_error_prop[] = {
	{
		.event = 67U,
		.event_str = "lock_access_to_unavailable",
	},
};

static const struct aie_event_prop aie_core_bus_error_prop[] = {
	{
		.event = 58U,
		.event_str = "axi-mm_slave_error",
	},
};

static const struct aie_event_prop aie_mem_ecc_error_prop[] = {
	{
		.event = 88U,
		.event_str = "dm_ecc_error_scrub_2-bit",
	},
	{
		.event = 90U,
		.event_str = "dm_ecc_error_2-bit",
	},
};

static const struct aie_event_prop aie_mem_parity_error_prop[] = {
	{
		.event = 91U,
		.event_str = "dm_parity_error_bank_2",
	},
	{
		.event = 92U,
		.event_str = "dm_parity_error_bank_3",
	},
	{
		.event = 93U,
		.event_str = "dm_parity_error_bank_4",
	},
	{
		.event = 94U,
		.event_str = "dm_parity_error_bank_5",
	},
	{
		.event = 95U,
		.event_str = "dm_parity_error_bank_6",
	},
	{
		.event = 96U,
		.event_str = "dm_parity_error_bank_7",
	},
};

static const struct aie_event_prop aie_mem_dma_error_prop[] = {
	{
		.event = 97U,
		.event_str = "dma_s2mm_0_error",
	},
	{
		.event = 98U,
		.event_str = "dma_s2mm_1_error",
	},
	{
		.event = 99U,
		.event_str = "dma_mm2s_0_error",
	},
	{
		.event = 100U,
		.event_str = "dma_mm2s_1_error",
	},
};

static const struct aie_event_prop aie_shim_bus_error_prop[] = {
	{
		.event = 62U,
		.event_str = "axi-mm_slave_tile_error",
	},
};

static const struct aie_event_prop aie_shim_stream_error_prop[] = {
	{
		.event = 63U,
		.event_str = "control_packet_error",
	},
	{
		.event = 64U,
		.event_str = "axi-mm_decode_nsu_error",
	},
	{
		.event = 65U,
		.event_str = "axi-mm_slave_nsu_error",
	},
	{
		.event = 66U,
		.event_str = "axi-mm_unsupported_traffic",
	},
	{
		.event = 67U,
		.event_str = "axi-mm_unsecure_access_in_secure_mode",
	},
	{
		.event = 68U,
		.event_str = "axi-mm_byte_strobe_error",
	},
};

static const struct aie_event_prop aie_shim_dma_error_prop[] = {
	{
		.event = 69U,
		.event_str = "dma_s2mm_0_error",
	},
	{
		.event = 70U,
		.event_str = "dma_s2mm_1_error",
	},
	{
		.event = 71U,
		.event_str = "dma_mm2s_0_error",
	},
	{
		.event = 72U,
		.event_str = "dma_mm2s_1_error",
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

/* resource attributes for core tile type */
static const
struct aie_tile_rsc_attr aie_core_tile_rscs_attr[AIE_RSCTYPE_MAX] =  {
	{
		/* perf counter */
		.mod_attr = {
			{.num_rscs = AIE_NUM_PERF_MEM_MOD,},
			{.num_rscs = AIE_NUM_PERF_CORE_MOD,},
		},
	},
	{
		/* user event */
		.mod_attr = {
			{.num_rscs = AIE_NUM_USEREVENT_MEM_MOD,},
			{.num_rscs = AIE_NUM_USEREVENT_CORE_MOD,},
		},
	},
	{
		/* trace control */
		.mod_attr = {
			{.num_rscs = AIE_NUM_TRACECONTROL_MEM_MOD,},
			{.num_rscs = AIE_NUM_TRACECONTROL_CORE_MOD,},
		},
	},
	{
		/* pc event */
		.mod_attr = {
			{.num_rscs = AIE_NUM_PCEVENT_MEM_MOD,},
			{.num_rscs = AIE_NUM_PCEVENT_CORE_MOD,},
		},
	},
	{
		/* stream switch port select */
		.mod_attr = {
			{.num_rscs = AIE_NUM_SSSELECT_MEM_MOD,},
			{.num_rscs = AIE_NUM_SSSELECT_CORE_MOD,},
		},
	},
	{
		/* broadcast */
		.mod_attr = {
			{.num_rscs = AIE_NUM_BROADCAST_MEM_MOD,},
			{.num_rscs = AIE_NUM_BROADCAST_CORE_MOD,},
		},
	},
	{
		/* combo events */
		.mod_attr = {
			{.num_rscs = AIE_NUM_COMBOEVENT_MEM_MOD,},
			{.num_rscs = AIE_NUM_COMBOEVENT_CORE_MOD,},
		},
	},
	{
		/* group events */
		.mod_attr = {
			{.num_rscs = AIE_NUM_GROUPEVENTS_MEM_MOD,},
			{.num_rscs = AIE_NUM_GROUPEVENTS_CORE_MOD,},
		},
	},
};

/* resource attributes for SHIM PL tile type */
static const
struct aie_tile_rsc_attr aie_shimpl_tile_rscs_attr[AIE_RSCTYPE_MAX] =  {
	{
		/* perf counter */
		.mod_attr = {
			{.num_rscs = AIE_NUM_PERF_PL_MOD,},
		},
	},
	{
		/* user event */
		.mod_attr = {
			{.num_rscs = AIE_NUM_USEREVENT_PL_MOD,},
		},
	},
	{
		/* trace control */
		.mod_attr = {
			{.num_rscs = AIE_NUM_TRACECONTROL_PL_MOD},
		},
	},
	{
		/* pc event */
		.mod_attr = {
			{.num_rscs = AIE_NUM_PCEVENT_PL_MOD},
		},
	},
	{
		/* stream switch port select */
		.mod_attr = {
			{.num_rscs = AIE_NUM_SSSELECT_PL_MOD},
		},
	},
	{
		/* broadcast */
		.mod_attr = {
			{.num_rscs = AIE_NUM_BROADCAST_PL_MOD},
		},
	},
	{
		/* combo events */
		.mod_attr = {
			{.num_rscs = AIE_NUM_COMBOEVENT_PL_MOD},
		},
	},
	{
		/* group events */
		.mod_attr = {
			{.num_rscs = AIE_NUM_GROUPEVENTS_PL_MOD},
		},
	},
};

/* Events needed for core tile utilization */
static const
enum aie_events aie_core_util_events[NUM_UTIL_EVENTS] = {
		[AIE_EVENT_CORE_ACTIVE] = 28,
		[AIE_EVENT_CORE_DISABLED] = 29,
		[AIE_EVENT_CORE_USER_EVNT_0] = 124,
		[AIE_EVENT_CORE_USER_EVNT_1] = 125,
};

/* modules types array of CORE tile */
static const
enum aie_module_type aie_core_tile_module_types[NUM_MODS_CORE_TILE] = {
	AIE_MEM_MOD,
	AIE_CORE_MOD,
};

/* modules types array of SHIM PL tile */
static const
enum aie_module_type aie_shimpl_tile_module_types[NUM_MODS_SHIMPL_TILE] = {
	AIE_PL_MOD,
};

static const struct aie_single_reg_field aie_core_sts = {
	.mask = GENMASK(20, 0),
	.regoff = 0x32004U,
};

static const struct aie_single_reg_field aie_core_done = {
	.mask = BIT(20),
	.regoff = 0x32004U,
};

static const struct aie_single_reg_field aie_core_disable_event_sts = {
	.mask = BIT(15),
	.regoff = 0x32008U,
};

static const struct aie_single_reg_field aie_core_pc = {
	.mask = GENMASK(19, 0),
	.regoff = 0x30280U,
};

static const struct aie_single_reg_field aie_core_lr = {
	.mask = GENMASK(19, 0),
	.regoff = 0x302B0U,
};

static const struct aie_single_reg_field aie_core_sp = {
	.mask = GENMASK(19, 0),
	.regoff = 0x302A0U,
};

static char *aie_core_status_str[] = {
	"enabled",
	"reset",
	"south_memory_stall",
	"west_memory_stall",
	"north_memory_stall",
	"east_memory_stall",
	"south_lock_stall",
	"west_lock_stall",
	"north_lock_stall",
	"east_lock_stall",
	"stream_stall_ss0",
	"stream_stall_ss1",
	"stream_stall_ms0",
	"stream_stall_ms1",
	"cascade_stall_scd",
	"cascade_stall_mcd",
	"debug_halt",
	"ecc_error_stall",
	"ecc_scrubbing_stall",
	"error_halt",
	"core_done",
};

static const struct aie_lock_attr aie_pl_lock = {
	.sts = {
		.mask = GENMASK(1, 0),
		.regoff = 2U,
	},
	.sts_regoff = 0x14F00,
	.num_locks = 16U,
};

static const struct aie_lock_attr aie_mem_lock = {
	.sts = {
		.mask = GENMASK(1, 0),
		.regoff = 2U,
	},
	.sts_regoff = 0x1EF00,
	.num_locks = 16U,
};

static char *aie_lock_status_str[] = {
	"released_for_write",
	"acquired_for_write",
	"released_for_read",
	"acquired_for_read",
};

static const struct aie_dev_attr aie_tile_dev_attr[] = {
	AIE_TILE_DEV_ATTR_RO(bd, AIE_TILE_TYPE_MASK_TILE |
			     AIE_TILE_TYPE_MASK_SHIMNOC),
	AIE_TILE_DEV_ATTR_RO(core, AIE_TILE_TYPE_MASK_TILE),
	AIE_TILE_DEV_ATTR_RO(dma, AIE_TILE_TYPE_MASK_TILE |
			     AIE_TILE_TYPE_MASK_SHIMNOC),
	AIE_TILE_DEV_ATTR_RO(error, AIE_TILE_TYPE_MASK_TILE |
			     AIE_TILE_TYPE_MASK_SHIMNOC |
			     AIE_TILE_TYPE_MASK_SHIMPL),
	AIE_TILE_DEV_ATTR_RO(event, AIE_TILE_TYPE_MASK_TILE |
			     AIE_TILE_TYPE_MASK_SHIMNOC |
			     AIE_TILE_TYPE_MASK_SHIMPL),
	AIE_TILE_DEV_ATTR_RO(lock, AIE_TILE_TYPE_MASK_TILE |
			     AIE_TILE_TYPE_MASK_SHIMNOC),
};

static const struct aie_dev_attr aie_aperture_dev_attr[] = {
	AIE_APERTURE_ATTR_RO(hardware_info),
};

static const struct aie_dev_attr aie_part_dev_attr[] = {
	AIE_PART_DEV_ATTR_RO(error_stat),
	AIE_PART_DEV_ATTR_RO(current_freq),
};

static const struct aie_bin_attr aie_part_bin_attr[] = {
	AIE_PART_BIN_ATTR_RO(core, AIE_PART_SYSFS_CORE_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(dma, AIE_PART_SYSFS_DMA_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(error, AIE_PART_SYSFS_ERROR_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(lock, AIE_PART_SYSFS_LOCK_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(status, AIE_PART_SYSFS_STATUS_BINA_SIZE),
};

static const struct aie_sysfs_attr aie_aperture_sysfs_attr = {
	.dev_attr = aie_aperture_dev_attr,
	.bin_attr = NULL,
	.num_dev_attrs = ARRAY_SIZE(aie_aperture_dev_attr),
	.num_bin_attrs = 0U,
};

static const struct aie_sysfs_attr aie_part_sysfs_attr = {
	.dev_attr = aie_part_dev_attr,
	.bin_attr = aie_part_bin_attr,
	.num_dev_attrs = ARRAY_SIZE(aie_part_dev_attr),
	.num_bin_attrs = ARRAY_SIZE(aie_part_bin_attr),
};

static const struct aie_sysfs_attr aie_tile_sysfs_attr = {
	.dev_attr = aie_tile_dev_attr,
	.bin_attr = NULL,
	.num_dev_attrs = ARRAY_SIZE(aie_tile_dev_attr),
	.num_bin_attrs = 0U,
};

static u32 aie_get_tile_type(struct aie_device *adev, struct aie_location *loc)
{
	if (loc->row)
		return AIE_TILE_TYPE_TILE;
	/* SHIM row */
	if ((loc->col % 4) < 2)
		return AIE_TILE_TYPE_SHIMPL;

	if (adev->device_name == AIE_DEV_GEN_S100 ||
	    adev->device_name == AIE_DEV_GEN_S200) {
		if (loc->col == 58)
			return AIE_TILE_TYPE_SHIMPL;
	}

	return AIE_TILE_TYPE_SHIMNOC;
}

static unsigned int aie_get_mem_info(struct aie_device *adev,
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
		return NUM_MEMS_PER_TILE;

	for (i = 0; i < NUM_MEMS_PER_TILE; i++) {
		struct aie_mem *mem = &pmem[i].mem;

		memcpy(&mem->range, range, sizeof(*range));
	}

	start_row = adev->ttype_attr[AIE_TILE_TYPE_TILE].start_row;
	num_rows = adev->ttype_attr[AIE_TILE_TYPE_TILE].num_rows;
	/* Setup tile data memory information */
	pmem[0].mem.offset = 0;
	pmem[0].mem.size = KBYTES(32);
	pmem[0].mem.range.start.row = start_row;
	pmem[0].mem.range.size.row = num_rows;
	/* Setup program memory information */
	pmem[1].mem.offset = 0x20000;
	pmem[1].mem.size = KBYTES(16);
	pmem[1].mem.range.start.row = start_row;
	pmem[1].mem.range.size.row = num_rows;

	return NUM_MEMS_PER_TILE;
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
	struct aie_aperture *aperture = apart->aperture;
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

			if (aie_get_tile_type(adev, &loc) !=
					AIE_TILE_TYPE_TILE) {
				/* Checks shim tile for next core tile */
				va = aperture->base +
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
			va = aperture->base +
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
		struct aie_aperture *aperture = apart->aperture;

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
			va = aperture->base +
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
			va = aperture->base +
			     aie_cal_regoff(adev, ploc,
					    AIE_TILE_CORE_CLKCNTR_REGOFF);
			iowrite32(val, va);
		}

		/* If the tile clock is not on, jump to next column */
		if (!enable)
			break;
	}

	/* Update clock state bitmap */
	startbit = (range->start.col - apart->range.start.col) *
		   (apart->range.size.row - 1) + range->start.row - 1;
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
	struct aie_location rloc;

	/*
	 * The tiles below the highest tile whose clock is on, need to have the
	 * clock on. The first for loop is to scan the clock states bitmap to
	 * see which tiles are required to be clocked on, and update the bitmap
	 * to make sure the tiles below are also required to be clocked on.
	 */
	for (rloc.col = 0; rloc.col < range->size.col; rloc.col++) {
		u32 startbit, inuse_toprow = 0, clk_toprow = 0;

		startbit = rloc.col * (range->size.row - 1);

		for (rloc.row = range->start.row + 1;
		     rloc.row < range->start.row + range->size.row;
		     rloc.row++) {
			u32 bit = startbit + rloc.row - 1;

			if (aie_resource_testbit(&apart->tiles_inuse, bit))
				inuse_toprow = rloc.row;
			if (aie_resource_testbit(&apart->cores_clk_state, bit))
				clk_toprow = rloc.row;
		}

		/* Update clock states of a column */
		lrange.start.col = rloc.col + range->start.col;
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

/**
 * aie_get_core_status() - read the AI engine core status register.
 * @apart: AI engine partition.
 * @loc: location of AI engine core.
 * @return: 32-bit register value.
 */
static u32 aie_get_core_status(struct aie_partition *apart,
			       struct aie_location *loc)
{
	u32 regoff, regvalue, eventval;

	regoff = aie_cal_regoff(apart->adev, *loc, aie_core_sts.regoff);
	regvalue = ioread32(apart->aperture->base + regoff);

	/* Apply core done workaround */
	if (!FIELD_GET(aie_core_done.mask, regvalue)) {
		regoff = aie_cal_regoff(apart->adev, *loc,
					aie_core_disable_event_sts.regoff);
		eventval = ioread32(apart->aperture->base + regoff);

		if (FIELD_GET(aie_core_disable_event_sts.mask, eventval))
			regvalue |= aie_core_done.mask;
	}
	return regvalue;
}

/**
 * aie_part_clear_mems() - clear memories of every tile in a partition
 * @apart: AI engine partition
 * @return: return 0 always.
 */
static int aie_part_clear_mems(struct aie_partition *apart)
{
	struct aie_device *adev = apart->adev;
	struct aie_part_mem *pmems = apart->pmems;
	u32 i, num_mems;

	/* Get the number of different types of memories */
	num_mems = adev->ops->get_mem_info(adev, &apart->range, NULL);
	if (!num_mems)
		return 0;

	/* Clear each type of memories in the partition */
	for (i = 0; i < num_mems; i++) {
		struct aie_mem *mem = &pmems[i].mem;
		struct aie_range *range = &mem->range;
		u32 c, r;

		for (c = range->start.col;
		     c < range->start.col + range->size.col; c++) {
			for (r = range->start.row;
			     r < range->start.row + range->size.row; r++) {
				struct aie_location loc;
				u32 memoff;

				loc.col = c;
				loc.row = r;
				memoff = aie_cal_regoff(adev, loc, mem->offset);
				memset_io(apart->aperture->base + memoff, 0,
					  mem->size);
			}
		}
	}

	return 0;
}

/**
 * aie_set_tile_isolation() - Set isolation boundary of AI engine tile
 * @apart: AI engine partition
 * @loc: Location of tile
 * @dir: Direction to block
 * @return: return 0 if success negative value for failure.
 *
 * Possible direction values are:
 *	- AIE_ISOLATE_EAST_MASK
 *	- AIE_ISOLATE_NORTH_MASK
 *	- AIE_ISOLATE_WEST_MASK
 *	- AIE_ISOLATE_SOUTH_MASK
 *	- AIE_ISOLATE_ALL_MASK
 *	- or "OR" of multiple values
 */
static int aie_set_tile_isolation(struct aie_partition *apart,
				  struct aie_location *loc, u8 dir)
{
	struct aie_device *adev = apart->adev;
	struct aie_aperture *aperture = apart->aperture;
	void __iomem *va;
	u32 ttype, val;

	/* For AIE device, dir input will match register masks */
	val = (u32)dir;
	ttype = aie_get_tile_type(adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE) {
		va = aperture->base +
		     aie_cal_regoff(adev, *loc, AIE_TILE_CORE_TILECTRL_REGOFF);
	} else {
		va = aperture->base +
		     aie_cal_regoff(adev, *loc, AIE_SHIMPL_TILECTRL_REGOFF);
	}
	iowrite32(val, va);

	return 0;
}

/**
 * aie_get_lock_status() - reads the lock status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @return: 32-bit register value.
 */
static u32 aie_get_lock_status(struct aie_partition *apart,
			       struct aie_location *loc)
{
	u32 ttype, stsoff, regoff;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		stsoff = aie_pl_lock.sts_regoff;
	else
		stsoff = aie_mem_lock.sts_regoff;

	regoff = aie_cal_regoff(apart->adev, *loc, stsoff);

	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie_get_lock_status_str() - returns the string value corresponding to
 *			       lock status value.
 * @apart: AI engine partition.
 * @loc: location of AI engine lock.
 * @status: status value of lock.
 * @lock: lock ID.
 * @buffer: location to return lock status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
static ssize_t aie_get_lock_status_str(struct aie_partition *apart,
				       struct aie_location *loc, u32 status,
				       u32 lock, char *buffer, ssize_t size)
{
	char **str = aie_lock_status_str;
	u32 ttype, mask;
	u8 value, shift;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE) {
		shift = lock * aie_pl_lock.sts.regoff;
		mask = aie_pl_lock.sts.mask << shift;
	} else {
		shift = lock * aie_mem_lock.sts.regoff;
		mask = aie_mem_lock.sts.mask << shift;
	}

	value = (status & mask) >> shift;

	return scnprintf(buffer, max(0L, size), str[value]);
}

static ssize_t aie_get_tile_sysfs_lock_status(struct aie_partition *apart,
					      struct aie_location *loc,
					      char *buffer, ssize_t size)
{
	u32 i, ttype, num_locks;
	unsigned long status;
	ssize_t len = 0;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_SHIMPL)
		return len;

	if (ttype == AIE_TILE_TYPE_TILE)
		num_locks = aie_mem_lock.num_locks;
	else
		num_locks = aie_pl_lock.num_locks;

	if (!aie_part_check_clk_enable_loc(apart, loc)) {
		for (i = 0; i < num_locks; i++) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%d: clock_gated\n", i);
		}
		return len;
	}

	status = aie_get_lock_status(apart, loc);
	for (i = 0; i < num_locks; i++) {
		len += scnprintf(&buffer[len], max(0L, size - len), "%d: ", i);
		len += aie_get_lock_status_str(apart, loc, status, i,
					       &buffer[len], size - len);
		len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	}

	return len;
}

static ssize_t aie_get_part_sysfs_lock_status(struct aie_partition *apart,
					      struct aie_location *loc,
					      char *buffer, ssize_t size)
{
	u32 i, ttype, num_locks;
	unsigned long status;
	ssize_t len = 0;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_SHIMPL)
		return len;

	if (!aie_part_check_clk_enable_loc(apart, loc)) {
		len += scnprintf(&buffer[len], max(0L, size - len),
				 "clock_gated");
		return len;
	}

	if (ttype == AIE_TILE_TYPE_TILE)
		num_locks = aie_mem_lock.num_locks;
	else
		num_locks = aie_pl_lock.num_locks;

	status = aie_get_lock_status(apart, loc);
	for (i = 0; i < num_locks; i++) {
		len += aie_get_lock_status_str(apart, loc, status, i,
					       &buffer[len], size - len);
		if (i < num_locks - 1) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL0);
		}
	}
	return len;
}

/*
 * aie_get_tile_bd_attr() - gets tile bd attribute for AIE
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @attr: pointer of attribute to assign
 */
static void aie_get_tile_bd_attr(struct aie_partition *apart,
				 struct aie_location *loc,
				 const struct aie_bd_attr **attr)
{
	u32 ttype;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		*attr = &aie_tilebd;
	else
		*attr = &aie_shimbd;
}

/*
 * aie_get_tile_dma_attr() - gets tile dma attribute for AIE
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @attr: pointer of attribute to assign
 */
static void aie_get_tile_dma_attr(struct aie_partition *apart,
				  struct aie_location *loc,
				  const struct aie_dma_attr **attr)
{
	u32 ttype;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		*attr = &aie_tiledma;
	else
		*attr = &aie_shimdma;
}

/**
 * aie_get_dma_s2mm_status() - reads the DMA stream to memory map status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @return: 32-bit register value.
 */
static u32 aie_get_dma_s2mm_status(struct aie_partition *apart,
				   struct aie_location *loc)
{
	u32 stsoff, regoff, ttype;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		stsoff = aie_shimdma.s2mm_sts_regoff;
	else
		stsoff = aie_tiledma.s2mm_sts_regoff;

	regoff = aie_cal_regoff(apart->adev, *loc, stsoff);

	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie_get_dma_mm2s_status() - reads the DMA memory map to stream status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @return: 32-bit register value.
 */
static u32 aie_get_dma_mm2s_status(struct aie_partition *apart,
				   struct aie_location *loc)
{
	u32 stsoff, regoff, ttype;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		stsoff = aie_shimdma.mm2s_sts_regoff;
	else
		stsoff = aie_tiledma.mm2s_sts_regoff;

	regoff = aie_cal_regoff(apart->adev, *loc, stsoff);

	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie_get_chan_status() - reads the DMA channel status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @chanid: DMA channel ID.
 * @return: 8-bit status value.
 */
static u8 aie_get_chan_status(struct aie_partition *apart,
			      struct aie_location *loc, u32 status, u8 chanid)
{
	const struct aie_single_reg_field *sts, *stall;
	u32 mask, chan_shift, shift, value, ttype;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE) {
		sts = &aie_shimdma.sts;
		stall = &aie_shimdma.stall;
	} else {
		sts = &aie_tiledma.sts;
		stall = &aie_tiledma.stall;
	}

	/* Calculate channel status bit */
	chan_shift = sts->regoff;
	mask = sts->mask << (chan_shift * chanid);
	shift = ffs(mask) - 1;
	value = (status & mask) >> shift;

	/* Calculate stall status bit */
	chan_shift = stall->regoff;
	mask = stall->mask << (chan_shift * chanid);
	shift = ffs(mask) - 1;
	value |= (status & mask) >> shift;

	/* If invalid value set to "invalid_status" */
	if (value >= ARRAY_SIZE(aie_dma_status_str))
		value = ARRAY_SIZE(aie_dma_status_str) - 1;

	return value;
}

/**
 * aie_get_queue_size() - reads the DMA queue size.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @chanid: DMA channel ID.
 * @return: 8-bit value.
 */
static u8 aie_get_queue_size(struct aie_partition *apart,
			     struct aie_location *loc, u32 status, u8 chanid)
{
	const struct aie_single_reg_field *qsize;
	u32 mask, chan_shift, shift, ttype;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		qsize = &aie_shimdma.qsize;
	else
		qsize = &aie_tiledma.qsize;

	/* Calculate queue size bit */
	chan_shift = qsize->regoff;
	mask = qsize->mask << (chan_shift * chanid);
	shift = ffs(mask) - 1;

	return (status & mask) >> shift;
}

/**
 * aie_get_queue_status() - reads the DMA queue status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @chanid: DMA channel ID.
 * @return: 8-bit status value.
 */
static u8 aie_get_queue_status(struct aie_partition *apart,
			       struct aie_location *loc, u32 status, u8 chanid)
{
	const struct aie_single_reg_field *qsts;
	u32 mask, chan_shift, shift, ttype;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		qsts = &aie_shimdma.qsts;
	else
		qsts = &aie_tiledma.qsts;

	/* Get queue status bit */
	chan_shift = qsts->regoff;
	mask = qsts->mask << (chan_shift * chanid);
	shift = ffs(mask) - 1;

	return (status & mask) >> shift;
}

/**
 * aie_get_current_bd() - reads the current buffer descriptor being processed
 *			  by DMA channel.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @chanid: DMA channel ID.
 * @return: 8-bit buffer descriptor value.
 */
static u8 aie_get_current_bd(struct aie_partition *apart,
			     struct aie_location *loc, u32 status, u8 chanid)
{
	const struct aie_single_reg_field *curbd;
	u32 mask, chan_shift, shift, ttype;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		curbd = &aie_shimdma.curbd;
	else
		curbd = &aie_tiledma.curbd;

	/* Get current buffer descriptor */
	chan_shift = curbd->regoff;
	mask = curbd->mask << (chan_shift * chanid);
	shift = ffs(mask) - 1;

	return (status & mask) >> shift;
}

/**
 * aie_get_fifo_status() - reads the current value of DMA FIFO counters.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @return: concatenated value of counters for AIE tiles and 0 for shim tiles.
 */
static u32 aie_get_fifo_status(struct aie_partition *apart,
			       struct aie_location *loc)
{
	u32 fifo_off, regoff, ttype;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE)
		return 0U;

	fifo_off = aie_tiledma.fifo_cnt_regoff;

	regoff = aie_cal_regoff(apart->adev, *loc, fifo_off);

	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie_get_fifo_count() - returns the value of a DMA FIFO counter from its
 *			  concatenated register value.
 * @apart: AI engine partition.
 * @status: register value of DMA FIFO counter.
 * @counterid: counter ID.
 * @return: DMA FIFO count.
 */
static u32 aie_get_fifo_count(struct aie_partition *apart, u32 status,
			      u8 counterid)
{
	status >>= (aie_tiledma.fifo_cnt.regoff * counterid);

	return (status & aie_tiledma.fifo_cnt.mask);
}

/**
 * aie_get_part_sysfs_dma_status() - returns the status of DMA in string format
 *				     with MM2S and S2MM type channel separated
 *				     by a ',' symbol. Channels with a given
 *				     type are separated by a '|' symbol.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @buffer: location to return DMA status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
static ssize_t aie_get_part_sysfs_dma_status(struct aie_partition *apart,
					     struct aie_location *loc,
					     char *buffer, ssize_t size)
{
	u32 i, ttype, num_s2mm_chan, num_mm2s_chan;
	bool is_delimit_req = false;
	unsigned long status;
	ssize_t len = 0;

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_SHIMPL)
		return len;

	if (!aie_part_check_clk_enable_loc(apart, loc)) {
		len += scnprintf(buffer, max(0L, size - len),
				 "mm2s: clock_gated%ss2mm: clock_gated",
				 DELIMITER_LEVEL1);
		return len;
	}

	if (ttype != AIE_TILE_TYPE_TILE) {
		num_mm2s_chan = aie_shimdma.num_mm2s_chan;
		num_s2mm_chan = aie_shimdma.num_s2mm_chan;
	} else {
		num_mm2s_chan = aie_tiledma.num_mm2s_chan;
		num_s2mm_chan = aie_tiledma.num_s2mm_chan;
	}

	/* MM2S */
	len += scnprintf(&buffer[len], max(0L, size - len), "mm2s: ");
	status = aie_get_dma_mm2s_status(apart, loc);

	for (i = 0; i < num_mm2s_chan; i++) {
		u32 value = aie_get_chan_status(apart, loc, status, i);

		if (is_delimit_req) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL0);
		}
		len += scnprintf(&buffer[len], max(0L, size - len),
				 aie_dma_status_str[value]);
		is_delimit_req = true;
	}

	/* S2MM */
	is_delimit_req = false;
	len += scnprintf(&buffer[len], max(0L, size - len), "%ss2mm: ",
			 DELIMITER_LEVEL1);
	status = aie_get_dma_s2mm_status(apart, loc);

	for (i = 0; i < num_s2mm_chan; i++) {
		u32 value = aie_get_chan_status(apart, loc, status, i);

		if (is_delimit_req) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL0);
		}
		len += scnprintf(&buffer[len], max(0L, size - len),
				 aie_dma_status_str[value]);
		is_delimit_req = true;
	}
	return len;
}

/*
 * aie_get_tile_sysfs_dma_status() - exports AI engine DMA channel status,
 *				     queue size, queue status, and current
 *				     buffer descriptor ID being processed by
 *				     DMA channel to a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @buffer: location to return DMA status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
static ssize_t aie_get_tile_sysfs_dma_status(struct aie_partition *apart,
					     struct aie_location *loc,
					     char *buffer, ssize_t size)
{
	u32 ttype, chan, mm2s, s2mm, num_s2mm_chan, num_mm2s_chan, fifo;
	char **qsts_str = aie_queue_status_str;
	bool is_delimit_req = false;
	ssize_t len = 0;

	if (!aie_part_check_clk_enable_loc(apart, loc)) {
		len += scnprintf(&buffer[len], max(0L, size - len),
				 "channel_status: mm2s: clock_gated%ss2mm: clock_gated\n",
				 DELIMITER_LEVEL1);
		len += scnprintf(&buffer[len], max(0L, size - len),
				 "queue_size: mm2s: clock_gated%ss2mm: clock_gated\n",
				 DELIMITER_LEVEL1);
		len += scnprintf(&buffer[len], max(0L, size - len),
				 "queue_status: mm2s: clock_gated%ss2mm: clock_gated\n",
				 DELIMITER_LEVEL1);
		len += scnprintf(&buffer[len], max(0L, size - len),
				 "current_bd: mm2s: clock_gated%ss2mm: clock_gated\n",
				 DELIMITER_LEVEL1);
		len += scnprintf(&buffer[len], max(0L, size - len),
				 "fifo_len: clock_gated\n");
		return len;
	}

	ttype = aie_get_tile_type(apart->adev, loc);
	if (ttype != AIE_TILE_TYPE_TILE) {
		num_mm2s_chan = aie_shimdma.num_mm2s_chan;
		num_s2mm_chan = aie_shimdma.num_s2mm_chan;
	} else {
		num_mm2s_chan = aie_tiledma.num_mm2s_chan;
		num_s2mm_chan = aie_tiledma.num_s2mm_chan;
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "channel_status: ");
	len += aie_get_part_sysfs_dma_status(apart, loc, &buffer[len],
					     max(0L, size - len));

	mm2s = aie_get_dma_mm2s_status(apart, loc);
	s2mm = aie_get_dma_s2mm_status(apart, loc);

	/* Queue size */
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "\nqueue_size: mm2s: ");

	for (chan = 0; chan < num_mm2s_chan; chan++) {
		if (is_delimit_req) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);
		}

		len += scnprintf(&buffer[len], max(0L, size - len), "%d",
				 aie_get_queue_size(apart, loc, mm2s, chan));
		is_delimit_req = true;
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "%ss2mm: ",
			 DELIMITER_LEVEL1);

	is_delimit_req = false;
	for (chan = 0; chan < num_s2mm_chan; chan++) {
		if (is_delimit_req) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);
		}

		len += scnprintf(&buffer[len], max(0L, size - len), "%d",
				 aie_get_queue_size(apart, loc, s2mm, chan));
		is_delimit_req = true;
	}

	/* Queue status */
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "\nqueue_status: mm2s: ");

	is_delimit_req = false;
	for (chan = 0; chan < num_mm2s_chan; chan++) {
		if (is_delimit_req) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);
		}

		len += scnprintf(&buffer[len], max(0L, size - len),
				 qsts_str[aie_get_queue_status(apart, loc, mm2s,
					  chan)]);
		is_delimit_req = true;
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "%ss2mm: ",
			 DELIMITER_LEVEL1);

	is_delimit_req = false;
	for (chan = 0; chan < num_s2mm_chan; chan++) {
		if (is_delimit_req) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);
		}

		len += scnprintf(&buffer[len], max(0L, size - len),
				 qsts_str[aie_get_queue_status(apart, loc, s2mm,
					  chan)]);
		is_delimit_req = true;
	}

	/* Current BD */
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "\ncurrent_bd: mm2s: ");

	is_delimit_req = false;
	for (chan = 0; chan < num_mm2s_chan; chan++) {
		if (is_delimit_req) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);
		}

		len += scnprintf(&buffer[len], max(0L, size - len), "%d",
				 aie_get_current_bd(apart, loc, mm2s, chan));
		is_delimit_req = true;
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "%ss2mm: ",
			 DELIMITER_LEVEL1);

	is_delimit_req = false;
	for (chan = 0; chan < num_s2mm_chan; chan++) {
		if (is_delimit_req) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);
		}

		len += scnprintf(&buffer[len], max(0L, size - len), "%d",
				 aie_get_current_bd(apart, loc, s2mm, chan));
		is_delimit_req = true;
	}

	/* FIFO length */
	len += scnprintf(&buffer[len], max(0L, size - len), "\nfifo_len: ");

	fifo = aie_get_fifo_status(apart, loc);
	len += scnprintf(&buffer[len], max(0L, size - len), "%d%s%d\n",
			 aie_get_fifo_count(apart, fifo, 0), DELIMITER_LEVEL0,
			 aie_get_fifo_count(apart, fifo, 1));
	return len;
}

/**
 * aie_get_tile_sysfs_bd_metadata() - exports AI engine DMA buffer descriptor
 *				      metadata for all buffer descriptors to
 *				      a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA buffer descriptors.
 * @buffer: location to return DMA buffer descriptor metadata string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
static ssize_t aie_get_tile_sysfs_bd_metadata(struct aie_partition *apart,
					      struct aie_location *loc,
					      char *buffer, ssize_t size)
{
	const struct aie_dma_attr *dma_attr;
	const struct aie_bd_attr *bd_attr;
	u32 enabled, ttype;
	ssize_t len = 0;

	aie_get_tile_dma_attr(apart, loc, &dma_attr);
	aie_get_tile_bd_attr(apart, loc, &bd_attr);

	ttype = aie_get_tile_type(apart->adev, loc);
	enabled = aie_part_check_clk_enable_loc(apart, loc);
	for (u32 bd = 0; bd < dma_attr->num_bds; bd++) {
		u32 bd_data[AIE_MAX_BD_SIZE];
		u32 i, index, base_bdoff;
		u64 value;

		len += scnprintf(&buffer[len], max(0L, size - len),
				 "%d: ", bd);
		if (!enabled) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "clock_gated\n");
			continue;
		}

		base_bdoff = dma_attr->bd_regoff + (bd_attr->bd_idx_off * bd);
		memset(bd_data, 0, sizeof(bd_data));
		for (i = 0; i < dma_attr->bd_len / sizeof(u32); i++) {
			u32 regoff;

			regoff = aie_cal_regoff(apart->adev, *loc,
						base_bdoff + (i * 4U));
			bd_data[i] = ioread32(apart->aperture->base + regoff);
		}

		/* address and length */
		if (ttype == AIE_TILE_TYPE_TILE) {
			index = bd_attr->addr.addr.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->addr.addr,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%llx%s", value, DELIMITER_LEVEL0);
			index = bd_attr->addr_2.addr.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->addr_2.addr,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%llx%s", value, DELIMITER_LEVEL0);
		} else {
			u32 h_addr;

			index = bd_attr->addr.addr.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->addr.addr,
						  bd_data[index]);
			h_addr = bd_data[bd_attr->addr_2.addr.regoff / sizeof(u32)];
			h_addr = aie_get_reg_field(&bd_attr->addr_2.addr, h_addr);
			value |= (u64)h_addr << 32;
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%llx%s", value, DELIMITER_LEVEL0);
		}

		index = bd_attr->addr.length.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->addr.length, bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);

		/* locks */
		index = bd_attr->lock.lock_acq_id.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->lock.lock_acq_id,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->lock.lock_acq_val,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->lock.lock_acq_en,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->lock.lock_acq_val_en,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->lock.lock_rel_val,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->lock.lock_rel_en,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->lock.lock_rel_val_en,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);

		if (ttype == AIE_TILE_TYPE_TILE) {
			index = bd_attr->lock_2.lock_acq_id.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->lock_2.lock_acq_id,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			value = aie_get_reg_field(&bd_attr->lock_2.lock_acq_val,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			value = aie_get_reg_field(&bd_attr->lock_2.lock_acq_en,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			value = aie_get_reg_field(&bd_attr->lock_2.lock_acq_val_en,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			value = aie_get_reg_field(&bd_attr->lock_2.lock_rel_val,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			value = aie_get_reg_field(&bd_attr->lock_2.lock_rel_en,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			value = aie_get_reg_field(&bd_attr->lock_2.lock_rel_val_en,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
		}

		/* packet */
		index = bd_attr->packet.pkt_en.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->packet.pkt_en,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		index = bd_attr->packet.pkt_id.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->packet.pkt_id,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		index = bd_attr->packet.pkt_type.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->packet.pkt_type,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);

		/* control */
		index = bd_attr->valid_bd.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->valid_bd, bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		index = bd_attr->use_next.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->use_next, bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		index = bd_attr->next_bd.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->next_bd, bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);

		/* axi settings */
		if (ttype == AIE_TILE_TYPE_SHIMNOC) {
			index = bd_attr->axi.smid.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->axi.smid,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			value = aie_get_reg_field(&bd_attr->axi.cache,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			value = aie_get_reg_field(&bd_attr->axi.qos,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			value = aie_get_reg_field(&bd_attr->axi.secure_en,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			value = aie_get_reg_field(&bd_attr->axi.burst_len,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld\n", value);
			continue;
		}

		index = bd_attr->buf_sel.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->buf_sel, bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		index = bd_attr->curr_ptr.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->curr_ptr, bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		index = bd_attr->double_buff_en.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->double_buff_en,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		index = bd_attr->interleave_en.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->interleave_en,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		index = bd_attr->interleave_cnt.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->interleave_cnt,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		index = bd_attr->fifo_mode.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->fifo_mode, bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);

		/* dimensions */
		index = bd_attr->aie_dim.x_incr.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->aie_dim.x_incr,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->aie_dim.x_wrap,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->aie_dim.x_off,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		index = bd_attr->aie_dim.y_incr.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->aie_dim.y_incr,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->aie_dim.y_wrap,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->aie_dim.y_off,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld\n",
				 value);
	}

	return len;
}

static const struct aie_tile_operations aie_ops = {
	.get_tile_type = aie_get_tile_type,
	.get_mem_info = aie_get_mem_info,
	.get_core_status = aie_get_core_status,
	.get_part_sysfs_lock_status = aie_get_part_sysfs_lock_status,
	.get_tile_sysfs_lock_status = aie_get_tile_sysfs_lock_status,
	.get_part_sysfs_dma_status = aie_get_part_sysfs_dma_status,
	.get_tile_sysfs_dma_status = aie_get_tile_sysfs_dma_status,
	.get_tile_sysfs_bd_metadata = aie_get_tile_sysfs_bd_metadata,
	.init_part_clk_state = aie_init_part_clk_state,
	.scan_part_clocks = aie_scan_part_clocks,
	.set_part_clocks = aie_set_part_clocks,
	.set_tile_isolation = aie_set_tile_isolation,
	.mem_clear = aie_part_clear_mems,
};

/**
 * aie_device_init_rscs_attr() - initialize AI engine device resources
 *				 attributes
 * @adev: AI engine device
 */
static void aie_device_init_rscs_attr(struct aie_device *adev)
{
	struct aie_tile_attr *tattr;

	tattr = &adev->ttype_attr[AIE_TILE_TYPE_TILE];
	tattr->num_mods = NUM_MODS_CORE_TILE;
	tattr->rscs_attr = aie_core_tile_rscs_attr;
	tattr->mods = aie_core_tile_module_types;

	tattr = &adev->ttype_attr[AIE_TILE_TYPE_SHIMPL];
	tattr->num_mods = NUM_MODS_SHIMPL_TILE;
	tattr->rscs_attr = aie_shimpl_tile_rscs_attr;
	tattr->mods = aie_shimpl_tile_module_types;

	/*
	 * For now, SHIMNOC is the same as SHIMPL as there is
	 * no SHIMNOC specific resources managed by kernel
	 * driver yet.
	 */
	tattr = &adev->ttype_attr[AIE_TILE_TYPE_SHIMNOC];
	tattr->num_mods = NUM_MODS_SHIMPL_TILE;
	tattr->rscs_attr = aie_shimpl_tile_rscs_attr;
	tattr->mods = aie_shimpl_tile_module_types;
}

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
	adev->array_shift = AIE_ARRAY_SHIFT;
	adev->col_shift = AIE_COL_SHIFT;
	adev->row_shift = AIE_ROW_SHIFT;
	adev->ops = &aie_ops;
	adev->num_kernel_regs = ARRAY_SIZE(aie_kernel_regs);
	adev->kernel_regs = aie_kernel_regs;
	adev->num_core_regs = ARRAY_SIZE(aie_core_regs);
	adev->core_regs = aie_core_regs;
	adev->col_rst = &aie_col_rst;
	adev->col_clkbuf = &aie_col_clkbuf;
	adev->shim_bd = &aie_shimbd;
	adev->tile_bd = &aie_tilebd;
	adev->shim_dma = &aie_shimdma;
	adev->tile_dma = &aie_tiledma;
	adev->pl_events = &aie_pl_event;
	adev->mem_events = &aie_mem_event;
	adev->core_events = &aie_core_event;
	adev->l1_ctrl = &aie_l1_intr_ctrl;
	adev->l2_ctrl = &aie_l2_intr_ctrl;
	adev->core_errors = &aie_core_error;
	adev->mem_errors = &aie_mem_error;
	adev->shim_errors = &aie_shim_error;
	adev->aperture_sysfs_attr = &aie_aperture_sysfs_attr;
	adev->part_sysfs_attr = &aie_part_sysfs_attr;
	adev->tile_sysfs_attr = &aie_tile_sysfs_attr;
	adev->core_status_str = aie_core_status_str;
	adev->core_pc = &aie_core_pc;
	adev->core_lr = &aie_core_lr;
	adev->core_sp = &aie_core_sp;
	adev->core_perfctrl = &aie_core_perfctrl;
	adev->core_perfctrl_reset = &aie_core_perfctrl_reset;
	adev->core_perfcnt = &aie_core_perfcnt;
	adev->core_evntgen = &aie_core_evntgen;
	adev->core_util_events = aie_core_util_events;

	aie_device_init_rscs_attr(adev);

	return 0;
}
