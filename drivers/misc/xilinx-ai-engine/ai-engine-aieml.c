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

#define NUM_MODS_CORE_TILE	2U
#define NUM_MODS_MEM_TILE	1U
#define NUM_MODS_SHIMPL_TILE	1U

/*
 * Number of resources per module
 */
#define AIEML_NUM_PERF_TILE_CORE_MOD		4U
#define AIEML_NUM_USEREVENT_TILE_CORE_MOD	4U
#define AIEML_NUM_TRACECONTROL_TILE_CORE_MOD	1U
#define AIEML_NUM_PCEVENT_TILE_CORE_MOD		4U
#define AIEML_NUM_SSSELECT_TILE_CORE_MOD	8U
#define AIEML_NUM_BROADCAST_TILE_CORE_MOD	16U
#define AIEML_NUM_COMBOEVENT_TILE_CORE_MOD	4U
#define AIEML_NUM_GROUPEVENTS_TILE_CORE_MOD	9U

#define AIEML_NUM_PERF_TILE_MEM_MOD		2U
#define AIEML_NUM_USEREVENT_TILE_MEM_MOD	4U
#define AIEML_NUM_TRACECONTROL_TILE_MEM_MOD	1U
#define AIEML_NUM_PCEVENT_TILE_MEM_MOD		0U
#define AIEML_NUM_SSSELECT_TILE_MEM_MOD		0U
#define AIEML_NUM_BROADCAST_TILE_MEM_MOD	16U
#define AIEML_NUM_COMBOEVENT_TILE_MEM_MOD	4U
#define AIEML_NUM_GROUPEVENTS_TILE_MEM_MOD	8U

#define AIEML_NUM_PERF_MEM_MOD			4U
#define AIEML_NUM_USEREVENT_MEM_MOD		2U
#define AIEML_NUM_TRACECONTROL_MEM_MOD		1U
#define AIEML_NUM_PCEVENT_MEM_MOD		0U
#define AIEML_NUM_SSSELECT_MEM_MOD		8U
#define AIEML_NUM_BROADCAST_MEM_MOD		16U
#define AIEML_NUM_COMBOEVENT_MEM_MOD		4U
#define AIEML_NUM_GROUPEVENTS_MEM_MOD		9U

#define AIEML_NUM_PERF_PL_MOD			2U
#define AIEML_NUM_USEREVENT_PL_MOD		2U
#define AIEML_NUM_TRACECONTROL_PL_MOD		1U
#define AIEML_NUM_PCEVENT_PL_MOD		0U
#define AIEML_NUM_SSSELECT_PL_MOD		8U
#define AIEML_NUM_BROADCAST_PL_MOD		16U
#define AIEML_NUM_COMBOEVENT_PL_MOD		4U
#define AIEML_NUM_GROUPEVENTS_PL_MOD		6U

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
#define AIEML_SHIMPL_EVENT_BC0_REGOFF			0x00034010U
#define AIEML_SHIMPL_EVENT_STATUS0_REGOFF		0x00034200U
#define AIEML_SHIMPL_GROUP0_REGOFF			0x00034500U
#define AIEML_SHIMPL_GROUPERROR_REGOFF			0x0003450cU
#define AIEML_SHIMPL_L1INTR_MASK_A_REGOFF		0x00035000U
#define AIEML_SHIMPL_L1INTR_BLOCK_NORTH_B_REGOFF	0x00035050U
#define AIEML_SHIMPL_TILECTRL_REGOFF			0x00036030U
#define AIEML_SHIMPL_MODCLOCK_CTRL_0_REGOFF		0x000fff00U
#define AIEML_SHIMPL_MODCLOCK_CTRL_1_REGOFF		0x000fff04U
#define AIEML_SHIMPL_MODRESET_CTRL_0_REGOFF		0x000fff10U
#define AIEML_SHIMPL_MODRESET_CTRL_1_REGOFF		0x000fff14U
#define AIEML_MEMORY_GROUP0_REGOFF			0x00094500U
#define AIEML_MEMORY_GROUPERROR_REGOFF			0x00094518U
#define AIEML_MEMORY_TILECTRL_REGOFF			0x00096030U
#define AIEML_MEMORY_EVENT_BC0_REGOFF			0x00094010U
#define AIEML_MEMORY_EVENT_STATUS0_REGOFF		0x00094200U
#define AIEML_MEMORY_MEMCTRL_REGOFF			0x00096048U
#define AIEML_MEMORY_MODCLOCKCTRL_REGOFF		0x000fff00U
#define AIEML_MEMORY_MODRESETCTRL_REGOFF		0x000fff10U
#define AIEML_TILE_COREMOD_AMLL0_PART1_REGOFF		0x00030000U
#define AIEML_TILE_COREMOD_AMHH8_PART2_REGOFF		0x00030470U
#define AIEML_TILE_COREMOD_GROUPERROR_REGOFF		0x00034510U
#define AIEML_TILE_COREMOD_TILECTRL_REGOFF		0x00036030U
#define AIEML_TILE_COREMOD_GROUP0_REGOFF		0x00034500U
#define AIEML_TILE_COREMOD_EVENT_BC0_REGOFF		0x00034010U
#define AIEML_TILE_COREMOD_EVENT_STATUS0_REGOFF		0x00034200U
#define AIEML_TILE_COREMOD_MEMCTRL_REGOFF		0x00036070U
#define AIEML_TILE_COREMOD_MODCLOCKCTRL_REGOFF		0x00060000U
#define AIEML_TILE_COREMOD_MODRESETCTRL_REGOFF		0x00060010U
#define AIEML_TILE_COREMOD_WL0_PART1_REGOFF		0x00030800U
#define AIEML_TILE_COREMOD_WH11_PART2_REGOFF		0x00030af0U
#define AIEML_TILE_COREMOD_R0_REGOFF			0x00030c00U
#define AIEML_TILE_COREMOD_R31_REGOFF			0x00030df0U
#define AIEML_TILE_COREMOD_CORE_STATUS_REGOFF		0x00032004U
#define AIEML_TILE_COREMOD_CORE_PC_REGOFF		0x00031100U
#define AIEML_TILE_COREMOD_CORE_SP_REGOFF		0x00031120U
#define AIEML_TILE_COREMOD_CORE_LR_REGOFF		0x00031130U
#define AIEML_TILE_MEMMOD_GROUPERROR_REGOFF		0x00014514U
#define AIEML_TILE_MEMMOD_GROUP0_REGOFF			0x00014500U
#define AIEML_TILE_MEMMOD_EVENT_BC0_REGOFF		0x00014010U
#define AIEML_TILE_MEMMOD_EVENT_STATUS0_REGOFF		0x00014200U
#define AIEML_TILE_MEMMOD_MEMCTRL_REGOFF		0x00016010U

/*
 * Register masks
 */
#define AIEML_SHIMPL_COLRESET_CTRL_MASK			GENMASK(1, 0)
#define AIEML_SHIMPL_COLCLOCK_CTRL_MASK			GENMASK(1, 0)

/* Macros to define size of a sysfs binary attribute */
#define AIEML_PART_SYSFS_CORE_BINA_SIZE		0x4000		/* 16KB */

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
	/* SHIM tile control */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_SHIMPL_TILECTRL_REGOFF,
	 .eoff = AIEML_SHIMPL_TILECTRL_REGOFF,
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
	/* MEMORY mem tile control */
	{.attribute = AIE_TILE_TYPE_MASK_MEMORY << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_MEMORY_TILECTRL_REGOFF,
	 .eoff = AIEML_MEMORY_TILECTRL_REGOFF,
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
	/* TILE tile control */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIEML_TILE_COREMOD_TILECTRL_REGOFF,
	 .eoff = AIEML_TILE_COREMOD_TILECTRL_REGOFF,
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

/* resource attributes for core tile type */
static const
struct aie_tile_rsc_attr aieml_core_tile_rscs_attr[AIE_RSCTYPE_MAX] = {
	{
		/* perf counter */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_PERF_TILE_MEM_MOD,},
			{.num_rscs = AIEML_NUM_PERF_TILE_CORE_MOD,},
		},
	},
	{
		/* user event */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_USEREVENT_TILE_MEM_MOD,},
			{.num_rscs = AIEML_NUM_USEREVENT_TILE_CORE_MOD,},
		},
	},
	{
		/* trace control */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_TRACECONTROL_TILE_MEM_MOD,},
			{.num_rscs = AIEML_NUM_TRACECONTROL_TILE_CORE_MOD,},
		},
	},
	{
		/* pc event */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_PCEVENT_TILE_MEM_MOD,},
			{.num_rscs = AIEML_NUM_PCEVENT_TILE_CORE_MOD,},
		},
	},
	{
		/* stream switch port select */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_SSSELECT_TILE_MEM_MOD,},
			{.num_rscs = AIEML_NUM_SSSELECT_TILE_CORE_MOD,},
		},
	},
	{
		/* broadcast */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_BROADCAST_TILE_MEM_MOD,},
			{.num_rscs = AIEML_NUM_BROADCAST_TILE_CORE_MOD,},
		},
	},
	{
		/* combo events */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_COMBOEVENT_TILE_MEM_MOD,},
			{.num_rscs = AIEML_NUM_COMBOEVENT_TILE_CORE_MOD,},
		},
	},
	{
		/* group events */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_GROUPEVENTS_TILE_MEM_MOD,},
			{.num_rscs = AIEML_NUM_GROUPEVENTS_TILE_CORE_MOD,},
		},
	},
};

/* resource attributes for mem tile type */
static const
struct aie_tile_rsc_attr aieml_mem_tile_rscs_attr[AIE_RSCTYPE_MAX] = {
	{
		/* perf counter */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_PERF_MEM_MOD,},
		},
	},
	{
		/* user event */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_USEREVENT_MEM_MOD,},
		},
	},
	{
		/* trace control */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_TRACECONTROL_MEM_MOD,},
		},
	},
	{
		/* pc event */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_PCEVENT_MEM_MOD,},
		},
	},
	{
		/* stream switch port select */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_SSSELECT_MEM_MOD,},
		},
	},
	{
		/* broadcast */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_BROADCAST_MEM_MOD,},
		},
	},
	{
		/* combo events */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_COMBOEVENT_MEM_MOD,},
		},
	},
	{
		/* group events */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_GROUPEVENTS_MEM_MOD,},
		},
	},
};

/* resource attributes for shim tile type */
static const
struct aie_tile_rsc_attr aieml_shimpl_tile_rscs_attr[AIE_RSCTYPE_MAX] = {
	{
		/* perf counter */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_PERF_PL_MOD,},
		},
	},
	{
		/* user event */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_USEREVENT_PL_MOD,},
		},
	},
	{
		/* trace control */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_TRACECONTROL_PL_MOD,},
		},
	},
	{
		/* pc event */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_PCEVENT_PL_MOD,},
		},
	},
	{
		/* stream switch port select */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_SSSELECT_PL_MOD,},
		},
	},
	{
		/* broadcast */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_BROADCAST_PL_MOD,},
		},
	},
	{
		/* combo events */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_COMBOEVENT_PL_MOD,},
		},
	},
	{
		/* group events */
		.mod_attr = {
			{.num_rscs = AIEML_NUM_GROUPEVENTS_PL_MOD,},
		},
	},
};

/* modules types array of CORE tile */
static const
enum aie_module_type aieml_core_tile_module_types[NUM_MODS_CORE_TILE] = {
	AIE_MEM_MOD,
	AIE_CORE_MOD,
};

/* modules types array of MEM tile */
static const
enum aie_module_type aieml_mem_tile_module_types[NUM_MODS_MEM_TILE] = {
	AIE_MEM_MOD,
};

/* modules types array of SHIM PL tile */
static const
enum aie_module_type aieml_shimpl_tile_module_types[NUM_MODS_SHIMPL_TILE] = {
	AIE_PL_MOD,
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

static const struct aie_single_reg_field aieml_core_sts = {
	.mask = GENMASK(21, 0),
	.regoff = AIEML_TILE_COREMOD_CORE_STATUS_REGOFF,
};

static const struct aie_single_reg_field aieml_core_pc = {
	.mask = GENMASK(19, 0),
	.regoff = AIEML_TILE_COREMOD_CORE_PC_REGOFF,
};

static const struct aie_single_reg_field aieml_core_lr = {
	.mask = GENMASK(19, 0),
	.regoff = AIEML_TILE_COREMOD_CORE_LR_REGOFF,
};

static const struct aie_single_reg_field aieml_core_sp = {
	.mask = GENMASK(19, 0),
	.regoff = AIEML_TILE_COREMOD_CORE_SP_REGOFF,
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

static const struct aie_event_attr aieml_pl_event = {
	.bc_event = {
		.mask = GENMASK(6, 0),
		.regoff = 0U,
	},
	.group_error = {
		.mask = GENMASK(11, 0),
		.regoff = 0xcU,
	},
	.bc_regoff = AIEML_SHIMPL_EVENT_BC0_REGOFF,
	.status_regoff = AIEML_SHIMPL_EVENT_STATUS0_REGOFF,
	.group_regoff = AIEML_SHIMPL_GROUP0_REGOFF,
	.base_error_event = 64U,
	.num_broadcasts = 16U,
	.base_bc_event = 110U,
	.num_events = 128U,
};

static const struct aie_event_attr aieml_memtile_event = {
	.bc_event = {
		.mask = GENMASK(7, 0),
		.regoff = 0U,
	},
	.group_error = {
		.mask = GENMASK(11, 0),
		.regoff = 0x18U,
	},
	.bc_regoff = AIEML_MEMORY_EVENT_BC0_REGOFF,
	.status_regoff = AIEML_MEMORY_EVENT_STATUS0_REGOFF,
	.group_regoff = AIEML_MEMORY_GROUP0_REGOFF,
	.base_error_event = 129U,
	.num_broadcasts = 16U,
	.base_bc_event = 142U,
	.num_events = 192U,
};

static const struct aie_event_attr aieml_mem_event = {
	.bc_event = {
		.mask = GENMASK(6, 0),
		.regoff = 0U,
	},
	.group_error = {
		.mask = GENMASK(15, 0),
		.regoff = 0x14U,
	},
	.bc_regoff = AIEML_TILE_MEMMOD_EVENT_BC0_REGOFF,
	.status_regoff = AIEML_TILE_MEMMOD_EVENT_STATUS0_REGOFF,
	.group_regoff = AIEML_TILE_MEMMOD_GROUP0_REGOFF,
	.base_error_event = 87U,
	.num_broadcasts = 16U,
	.base_bc_event = 107U,
	.num_events = 128U,
};

static const struct aie_event_attr aieml_core_event = {
	.bc_event = {
		.mask = GENMASK(6, 0),
		.regoff = 0U,
	},
	.group_error = {
		.mask = GENMASK(24, 0),
		.regoff = 0x10U,
	},
	.bc_regoff = AIEML_TILE_COREMOD_EVENT_BC0_REGOFF,
	.status_regoff = AIEML_TILE_COREMOD_EVENT_STATUS0_REGOFF,
	.group_regoff = AIEML_TILE_COREMOD_GROUP0_REGOFF,
	.base_error_event = 48U,
	.num_broadcasts = 16U,
	.base_bc_event = 107U,
	.num_events = 128U,
};

static char *aieml_core_status_str[] = {
	"enable",
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
	"",
	"stream_stall_ms0",
	"",
	"cascade_stall_scd",
	"cascade_stall_mcd",
	"debug_halt",
	"ecc_error_stall",
	"ecc_scrubbing_stall",
	"error_halt",
	"core_done",
	"core_processor_bus_stall",
};

static const struct aie_dev_attr aieml_tile_dev_attr[] = {
	AIE_TILE_DEV_ATTR_RO(core, AIE_TILE_TYPE_MASK_TILE),
	AIE_TILE_DEV_ATTR_RO(event, AIE_TILE_TYPE_MASK_TILE |
			     AIE_TILE_TYPE_MASK_MEMORY |
			     AIE_TILE_TYPE_MASK_SHIMNOC |
			     AIE_TILE_TYPE_MASK_SHIMPL),
};

static const struct aie_dev_attr aieml_part_dev_attr[] = {
	AIE_PART_DEV_ATTR_RO(current_freq),
};

static const struct aie_bin_attr aieml_part_bin_attr[] = {
	AIE_PART_BIN_ATTR_RO(core, AIEML_PART_SYSFS_CORE_BINA_SIZE),
};

static const struct aie_sysfs_attr aieml_part_sysfs_attr = {
	.dev_attr = aieml_part_dev_attr,
	.bin_attr = aieml_part_bin_attr,
	.num_dev_attrs = ARRAY_SIZE(aieml_part_dev_attr),
	.num_bin_attrs = ARRAY_SIZE(aieml_part_bin_attr),
};

static const struct aie_sysfs_attr aieml_tile_sysfs_attr = {
	.dev_attr = aieml_tile_dev_attr,
	.bin_attr = NULL,
	.num_dev_attrs = ARRAY_SIZE(aieml_tile_dev_attr),
	.num_bin_attrs = 0U,
};

static u32 aieml_get_core_status(struct aie_partition *apart,
				 struct aie_location *loc)
{
	u32 regoff, regvalue;

	regoff = aie_cal_regoff(apart->adev, *loc, aieml_core_sts.regoff);
	regvalue = ioread32(apart->aperture->base + regoff);

	return aie_get_reg_field(&aieml_core_sts, regvalue);
}

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

static int aieml_init_part_clk_state(struct aie_partition *apart)
{
	int ret, num_tiles;

	num_tiles = apart->range.size.col * (apart->range.size.row - 1);

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
	if (ret < 0) {
		dev_err(&apart->dev, "failed to enable clocks for partition\n");
		return ret;
	}

	return aie_resource_set(&apart->cores_clk_state, 0,
				apart->cores_clk_state.total);
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

/**
 * aieml_set_tile_isolation() - Set isolation boundary of AI engile tile
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
static int aieml_set_tile_isolation(struct aie_partition *apart,
				    struct aie_location *loc, u8 dir)
{
	struct aie_device *adev = apart->adev;
	struct aie_aperture *aperture = apart->aperture;
	void __iomem *va;
	u32 ttype, val;

	/* For AIEML device, dir input will match register mask */
	val = (u32)dir;
	ttype = aieml_get_tile_type(adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE) {
		va = aperture->base +
		     aie_cal_regoff(adev, *loc,
				    AIEML_TILE_COREMOD_TILECTRL_REGOFF);
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		va = aperture->base +
		     aie_cal_regoff(adev, *loc, AIEML_MEMORY_TILECTRL_REGOFF);
	} else {
		va = aperture->base +
		     aie_cal_regoff(adev, *loc, AIEML_SHIMPL_TILECTRL_REGOFF);
	}
	iowrite32(val, va);
	return 0;
}

static const struct aie_tile_operations aieml_ops = {
	.get_tile_type = aieml_get_tile_type,
	.get_mem_info = aieml_get_mem_info,
	.get_core_status = aieml_get_core_status,
	.init_part_clk_state = aieml_init_part_clk_state,
	.scan_part_clocks = aieml_scan_part_clocks,
	.set_part_clocks = aieml_set_part_clocks,
	.set_tile_isolation = aieml_set_tile_isolation,
	.mem_clear = aieml_part_clear_mems,
};

/**
 * aieml_device_init_rscs_attr() - initialize AI engine device resources
 *				   attributes
 * @adev: AI engine device
 */
static void aieml_device_init_rscs_attr(struct aie_device *adev)
{
	struct aie_tile_attr *tattr;

	tattr = &adev->ttype_attr[AIE_TILE_TYPE_TILE];
	tattr->num_mods = NUM_MODS_CORE_TILE;
	tattr->rscs_attr = aieml_core_tile_rscs_attr;
	tattr->mods = aieml_core_tile_module_types;

	tattr = &adev->ttype_attr[AIE_TILE_TYPE_MEMORY];
	tattr->num_mods = NUM_MODS_MEM_TILE;
	tattr->rscs_attr = aieml_mem_tile_rscs_attr;
	tattr->mods = aieml_mem_tile_module_types;

	tattr = &adev->ttype_attr[AIE_TILE_TYPE_SHIMPL];
	tattr->num_mods = NUM_MODS_SHIMPL_TILE;
	tattr->rscs_attr = aieml_shimpl_tile_rscs_attr;
	tattr->mods = aieml_shimpl_tile_module_types;

	/*
	 * For now, SHIMNOC is the same as SHIMPL as there is
	 * no SHIMNOC specific resources managed by kernel
	 * driver yet.
	 */
	tattr = &adev->ttype_attr[AIE_TILE_TYPE_SHIMNOC];
	tattr->num_mods = NUM_MODS_SHIMPL_TILE;
	tattr->rscs_attr = aieml_shimpl_tile_rscs_attr;
	tattr->mods = aieml_shimpl_tile_module_types;
}

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
	adev->part_sysfs_attr = &aieml_part_sysfs_attr;
	adev->tile_sysfs_attr = &aieml_tile_sysfs_attr;
	adev->core_status_str = aieml_core_status_str;
	adev->core_pc = &aieml_core_pc;
	adev->core_lr = &aieml_core_lr;
	adev->core_sp = &aieml_core_sp;
	adev->pl_events = &aieml_pl_event;
	adev->memtile_events = &aieml_memtile_event;
	adev->mem_events = &aieml_mem_event;
	adev->core_events = &aieml_core_event;

	aieml_device_init_rscs_attr(adev);

	return 0;
}
