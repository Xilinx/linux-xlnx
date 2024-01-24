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

#define NUM_UTIL_EVENTS		4U

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
#define AIEML_SHIMNOC_LOCK_REGOFF			0x00014000U
#define AIEML_SHIMNOC_LOCK_OVERFLOW_REGOFF		0x00014120U
#define AIEML_SHIMNOC_LOCK_UNDERFLOW_REGOFF		0x00014128U
#define AIEML_SHIMNOC_DMA_S2MM_STATUS_REGOFF		0x0001D220U
#define AIEML_SHIMNOC_DMA_MM2S_STATUS_REGOFF		0x0001D228U

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

#define AIEML_MEMORY_BD0_0_REGOFF			0x000A0000U
#define AIEML_MEMORY_GROUP0_REGOFF			0x00094500U
#define AIEML_MEMORY_GROUPERROR_REGOFF			0x00094518U
#define AIEML_MEMORY_TILECTRL_REGOFF			0x00096030U
#define AIEML_MEMORY_EVENT_BC0_REGOFF			0x00094010U
#define AIEML_MEMORY_EVENT_STATUS0_REGOFF		0x00094200U
#define AIEML_MEMORY_MEMCTRL_REGOFF			0x00096048U
#define AIEML_MEMORY_MODCLOCKCTRL_REGOFF		0x000fff00U
#define AIEML_MEMORY_MODRESETCTRL_REGOFF		0x000fff10U
#define AIEML_MEMORY_LOCK_REGOFF			0x000C0000U
#define AIEML_MEMORY_LOCK_OVERFLOW_REGOFF		0x000C0420U
#define AIEML_MEMORY_LOCK_UNDERFLOW_REGOFF		0x000C0428U
#define AIEML_MEMORY_DMA_S2MM_STATUS_REGOFF		0x000A0660U
#define AIEML_MEMORY_DMA_MM2S_STATUS_REGOFF		0x000A0680U

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
#define AIEML_TILE_MEMMOD_BD0_0_REGOFF			0x0001D000U
#define AIEML_TILE_MEMMOD_GROUPERROR_REGOFF		0x00014514U
#define AIEML_TILE_MEMMOD_GROUP0_REGOFF			0x00014500U
#define AIEML_TILE_MEMMOD_EVENT_BC0_REGOFF		0x00014010U
#define AIEML_TILE_MEMMOD_EVENT_STATUS0_REGOFF		0x00014200U
#define AIEML_TILE_MEMMOD_MEMCTRL_REGOFF		0x00016010U
#define AIEML_TILE_MEMMOD_LOCK_REGOFF			0x0001F000U
#define AIEML_TILE_MEMMOD_LOCK_OVERFLOW_REGOFF		0x0001F120U
#define AIEML_TILE_MEMMOD_LOCK_UNDERFLOW_REGOFF		0x0001F128U
#define AIEML_TILE_MEMMOD_DMA_S2MM_STATUS_REGOFF	0x0001DF00U
#define AIEML_TILE_MEMMOD_DMA_MM2S_STATUS_REGOFF	0x0001DF10U
#define AIEML_TILE_COREMOD_PERFCTRL_REGOFF		0x00031500U
#define AIEML_TILE_COREMOD_PERFCTRL_RESET_REGOFF	0x00031508U
#define AIEML_TILE_COREMOD_PERFCNT0_REGOFF		0x00031520U
#define AIEML_TILE_CORE_EVNTGEN_REGOFF			0x00034008U

/*
 * Register masks
 */
#define AIEML_SHIMPL_COLRESET_CTRL_MASK			GENMASK(1, 0)
#define AIEML_SHIMPL_COLCLOCK_CTRL_MASK			GENMASK(1, 0)
#define AIEML_TILE_PERFCTRL_CNT0_MASK			0x7F7FU
#define AIEML_TILE_PERFCTRL_RESET_MASK			0x7FU
#define AIEML_TILE_CORE_PERFCNT0_MASK			0xFFFFFFFFU
#define AIEML_TILE_CORE_EVNTGEN_MASK			0x7F

/* Macros to define size of a sysfs binary attribute */
#define AIEML_PART_SYSFS_CORE_BINA_SIZE		0x4000		/* 16KB */
#define AIEML_PART_SYSFS_LOCK_BINA_SIZE		0x28000		/* 160KB */
#define AIEML_PART_SYSFS_ERROR_BINA_SIZE	0x4000		/* 16KB */
#define AIEML_PART_SYSFS_DMA_BINA_SIZE		0xC800		/* 50KB */
#define AIEML_PART_SYSFS_STATUS_BINA_SIZE	0x3c000		/* 240KB */

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

/* Events needed for core tile utilization */
static const
enum aie_events aieml_core_util_events[NUM_UTIL_EVENTS] = {
		[AIE_EVENT_CORE_ACTIVE] = 28,
		[AIE_EVENT_CORE_DISABLED] = 29,
		[AIE_EVENT_CORE_USER_EVNT_0] = 124,
		[AIE_EVENT_CORE_USER_EVNT_1] = 125,
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

static const struct aie_event_prop aieml_core_stream_error_prop[] = {
	{
		.event = 71U,
		.event_str = "stream_switch_port_parity_error",
	},
	{
		.event = 57U,
		.event_str = "control_pkt_error",
	},
	{
		.event = 56U,
		.event_str = "stream_pkt_parity_error",
	},
};

static const struct aie_event_prop aieml_core_inst_error_prop[] = {
	{
		.event = 59U,
		.event_str = "instruction_decompression_error",
	},
	{
		.event = 70U,
		.event_str = "decompression_underflow",
	},
};

static const struct aie_event_prop aieml_core_ecc_error_prop[] = {
	{
		.event = 64U,
		.event_str = "pm_ecc_error_2-bit",
	},
	{
		.event = 62U,
		.event_str = "pm_ecc_error_scrub_2-bit",
	},
};

static const struct aie_event_prop aieml_core_access_error_prop[] = {
	{
		.event = 55U,
		.event_str = "pm_reg_access_failure",
	},
	{
		.event = 60U,
		.event_str = "dm_address_out_of_range",
	},
	{
		.event = 65U,
		.event_str = "pm_address_out_of_range",
	},
	{
		.event = 66U,
		.event_str = "dm_access_to_unavailable",
	},
};

static const struct aie_event_prop aieml_core_lock_error_prop[] = {
	{
		.event = 67U,
		.event_str = "lock_access_to_unavailable",
	},
	{
		.event = 72U,
		.event_str = "processor_bus_error",
	},
};

static const struct aie_event_prop aieml_core_bus_error_prop[] = {
	{
		.event = 58U,
		.event_str = "axi_mm_slave_error",
	},
};

static const struct aie_event_prop aieml_mem_ecc_error_prop[] = {
	{
		.event = 88U,
		.event_str = "dm_ecc_error_scrub_2-bit",
	},
	{
		.event = 90U,
		.event_str = "dm_ecc_error_2-bit",
	},
};

static const struct aie_event_prop aieml_mem_parity_error_prop[] = {
	{
		.event = 96U,
		.event_str = "dm_parity_error_bank_7",
	},
	{
		.event = 95U,
		.event_str = "dm_parity_error_bank_6",
	},
	{
		.event = 94U,
		.event_str = "dm_parity_error_bank_5",
	},
	{
		.event = 93U,
		.event_str = "dm_parity_error_bank_4",
	},
	{
		.event = 92U,
		.event_str = "dm_parity_error_bank_3",
	},
	{
		.event = 91U,
		.event_str = "dm_parity_error_bank_2",
	},
};

static const struct aie_event_prop aieml_mem_dma_error_prop[] = {
	{
		.event = 100U,
		.event_str = "dma_mm2s_1_error",
	},
	{
		.event = 99U,
		.event_str = "dma_mm2s_0_error",
	},
	{
		.event = 98U,
		.event_str = "dma_s2mm_1_error",
	},
	{
		.event = 97U,
		.event_str = "dma_s2mm_0_error",
	},
};

static const struct aie_event_prop aieml_memtile_ecc_error_prop[] = {
	{
		.event = 132U,
		.event_str = "dm_ecc_error_2-bit",
	},
	{
		.event = 130U,
		.event_str = "dm_ecc_error_scrub_2-bit",
	},
};

static const struct aie_event_prop aieml_memtile_dma_error_prop[] = {
	{
		.event = 134U,
		.event_str = "dma_mm2s_error",
	},
	{
		.event = 133U,
		.event_str = "dma_s2mm_error",
	},
};

static const struct aie_event_prop aieml_memtile_stream_error_prop[] = {
	{
		.event = 137U,
		.event_str = "control_pkt_error",
	},
	{
		.event = 136U,
		.event_str = "stream_pkt_parity_error",
	},
	{
		.event = 135U,
		.event_str = "stream_switch_port_parity_error",
	},
};

static const struct aie_event_prop aieml_memtile_lock_error_prop[] = {
	{
		.event = 139U,
		.event_str = "lock_error",
	},
};

static const struct aie_event_prop aieml_memtile_bus_error_prop[] = {
	{
		.event = 58U,
		.event_str = "axi_mm_slave_error",
	},
};

static const struct aie_event_prop aieml_shim_bus_error_prop[] = {
	{
		.event = 71U,
		.event_str = "axi_mm_byte_strobe_error",
	},
	{
		.event = 70U,
		.event_str = "axi_mm_unsecure_access_in_secure_mode",
	},
	{
		.event = 69U,
		.event_str = "axi_mm_unsupported_traffic",
	},
	{
		.event = 68U,
		.event_str = "axi_mm_slave_nsu_error",
	},
	{
		.event = 67U,
		.event_str = "axi_mm_decode_nsu_error",
	},
	{
		.event = 64U,
		.event_str = "axi_mm_slave_tile_error",
	},
};

static const struct aie_event_prop aieml_shim_stream_error_prop[] = {
	{
		.event = 66U,
		.event_str = "stream_switch_port_parity_error",
	},
	{
		.event = 65U,
		.event_str = "control_pkt_error",
	},
};

static const struct aie_event_prop aieml_shim_dma_error_prop[] = {
	{
		.event = 73U,
		.event_str = "dma_mm2s_error",
	},
	{
		.event = 72U,
		.event_str = "dma_s2mm_error",
	},
};

static const struct aie_err_category aieml_core_err_category[] = {
	{
		/* AIE_ERROR_CATEGORY_STREAM */
		.err_category = AIE_ERROR_CATEGORY_STREAM,
		.num_events = ARRAY_SIZE(aieml_core_stream_error_prop),
		.prop = aieml_core_stream_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_ACCESS */
		.err_category = AIE_ERROR_CATEGORY_ACCESS,
		.num_events = ARRAY_SIZE(aieml_core_access_error_prop),
		.prop = aieml_core_access_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_BUS */
		.err_category = AIE_ERROR_CATEGORY_BUS,
		.num_events = ARRAY_SIZE(aieml_core_bus_error_prop),
		.prop = aieml_core_bus_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_INSTRUCTION */
		.err_category = AIE_ERROR_CATEGORY_INSTRUCTION,
		.num_events = ARRAY_SIZE(aieml_core_inst_error_prop),
		.prop = aieml_core_inst_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_ECC */
		.err_category = AIE_ERROR_CATEGORY_ECC,
		.num_events = ARRAY_SIZE(aieml_core_ecc_error_prop),
		.prop = aieml_core_ecc_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_LOCK */
		.err_category = AIE_ERROR_CATEGORY_LOCK,
		.num_events = ARRAY_SIZE(aieml_core_lock_error_prop),
		.prop = aieml_core_lock_error_prop,
	},
};

static const struct aie_err_category aieml_mem_err_category[] = {
	{
		/* AIE_ERROR_CATEGORY_ECC */
		.err_category = AIE_ERROR_CATEGORY_ECC,
		.num_events = ARRAY_SIZE(aieml_mem_ecc_error_prop),
		.prop = aieml_mem_ecc_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_MEM_PARITY */
		.err_category = AIE_ERROR_CATEGORY_MEM_PARITY,
		.num_events = ARRAY_SIZE(aieml_mem_parity_error_prop),
		.prop = aieml_mem_parity_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_DMA */
		.err_category = AIE_ERROR_CATEGORY_DMA,
		.num_events = ARRAY_SIZE(aieml_mem_dma_error_prop),
		.prop = aieml_mem_dma_error_prop,
	},
};

static const struct aie_err_category aieml_memtile_err_category[] = {
	{
		/* AIE_ERROR_CATEGORY_ECC */
		.err_category = AIE_ERROR_CATEGORY_ECC,
		.num_events = ARRAY_SIZE(aieml_memtile_ecc_error_prop),
		.prop = aieml_memtile_ecc_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_STREAM */
		.err_category = AIE_ERROR_CATEGORY_STREAM,
		.num_events = ARRAY_SIZE(aieml_memtile_stream_error_prop),
		.prop = aieml_memtile_stream_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_DMA */
		.err_category = AIE_ERROR_CATEGORY_DMA,
		.num_events = ARRAY_SIZE(aieml_memtile_dma_error_prop),
		.prop = aieml_memtile_dma_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_BUS */
		.err_category = AIE_ERROR_CATEGORY_BUS,
		.num_events = ARRAY_SIZE(aieml_memtile_bus_error_prop),
		.prop = aieml_memtile_bus_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_LOCK */
		.err_category = AIE_ERROR_CATEGORY_LOCK,
		.num_events = ARRAY_SIZE(aieml_memtile_lock_error_prop),
		.prop = aieml_memtile_lock_error_prop,
	},
};

static const struct aie_err_category aieml_shim_err_category[] = {
	{
		/* AIE_ERROR_CATEGORY_BUS */
		.err_category = AIE_ERROR_CATEGORY_BUS,
		.num_events = ARRAY_SIZE(aieml_shim_bus_error_prop),
		.prop = aieml_shim_bus_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_STREAM */
		.err_category = AIE_ERROR_CATEGORY_STREAM,
		.num_events = ARRAY_SIZE(aieml_shim_stream_error_prop),
		.prop = aieml_shim_stream_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_DMA */
		.err_category = AIE_ERROR_CATEGORY_DMA,
		.num_events = ARRAY_SIZE(aieml_shim_dma_error_prop),
		.prop = aieml_shim_dma_error_prop,
	},
};

static const struct aie_error_attr aieml_core_error = {
	.num_err_categories = ARRAY_SIZE(aieml_core_err_category),
	.err_category = aieml_core_err_category,
};

static const struct aie_error_attr aieml_mem_error = {
	.num_err_categories = ARRAY_SIZE(aieml_mem_err_category),
	.err_category = aieml_mem_err_category,
};

static const struct aie_error_attr aieml_memtile_error = {
	.num_err_categories = ARRAY_SIZE(aieml_memtile_err_category),
	.err_category = aieml_memtile_err_category,
};

static const struct aie_error_attr aieml_shim_error = {
	.num_err_categories = ARRAY_SIZE(aieml_shim_err_category),
	.err_category = aieml_shim_err_category,
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

static const struct aie_single_reg_field aieml_core_perfctrl = {
	.mask = AIEML_TILE_PERFCTRL_CNT0_MASK,
	.regoff = AIEML_TILE_COREMOD_PERFCTRL_REGOFF,
};

static const struct aie_single_reg_field aieml_core_perfctrl_reset = {
	.mask = AIEML_TILE_PERFCTRL_RESET_MASK,
	.regoff = AIEML_TILE_COREMOD_PERFCTRL_RESET_REGOFF,
};

static const struct aie_single_reg_field aieml_core_perfcnt = {
	.mask = AIEML_TILE_CORE_PERFCNT0_MASK,
	.regoff = AIEML_TILE_COREMOD_PERFCNT0_REGOFF,
};

static const struct aie_single_reg_field aieml_core_evntgen = {
	.mask = AIEML_TILE_CORE_EVNTGEN_MASK,
	.regoff = AIEML_TILE_CORE_EVNTGEN_REGOFF,
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

static const struct aie_bd_attr aieml_tilebd = {
	.valid_bd = {
		.mask = BIT(25),
		.regoff = 0x14U,
	},
	.next_bd = {
		.mask = GENMASK(30, 27),
		.regoff = 0x14U,
	},
	.use_next = {
		.mask = BIT(26),
		.regoff = 0x14U,
	},
	.addr = {
		.addr = {
			.mask = GENMASK(27, 14),
			.regoff = 0x0U,
		},
		.length = {
			.mask = GENMASK(13, 0),
			.regoff = 0x0U,
		},
	},
	.compression_en = {
		.mask = BIT(31),
		.regoff = 0x4U,
	},
	.out_of_order_id = {
		.mask = GENMASK(29, 24),
		.regoff = 0x4U,
	},
	.tlast_suppress = {
		.mask = BIT(31),
		.regoff = 0x14U,
	},
	.lock = {
		.lock_acq_id = {
			.mask = GENMASK(3, 0),
			.regoff = 0x14U,
		},
		.lock_acq_val = {
			.mask = GENMASK(11, 5),
			.regoff = 0x14U,
		},
		.lock_acq_en = {
			.mask = BIT(12),
			.regoff = 0x14U,
		},
		.lock_rel_id = {
			.mask = GENMASK(16, 13),
			.regoff = 0x14U,
		},
		.lock_rel_val = {
			.mask = GENMASK(24, 18),
			.regoff = 0x14U,
		},
	},
	.packet = {
		.pkt_en = {
			.mask = BIT(30),
			.regoff = 0x4U,
		},
		.pkt_type = {
			.mask = GENMASK(18, 16),
			.regoff = 0x4U,
		},
		.pkt_id = {
			.mask = GENMASK(23, 19),
			.regoff = 0x4U,
		},
	},
	.aieml_dim = {
		.iter_curr = {
			.mask = GENMASK(24, 19),
			.regoff = 0x10U,
		},
		.iter = {
			.wrap = {
				.mask = GENMASK(18, 13),
				.regoff = 0x10U,
			},
			.step_size = {
				.mask = GENMASK(12, 0),
				.regoff = 0x10U,
			},
		},
		.dims = {
			/* Dim 0 */
			{
				.wrap = {
					.mask = GENMASK(20, 13),
					.regoff = 0xCU,
				},
				.step_size = {
					.mask = GENMASK(12, 0),
					.regoff = 0x8U,
				},
			},
			/* Dim 1 */
			{
				.wrap = {
					.mask = GENMASK(28, 21),
					.regoff = 0xCU,
				},
				.step_size = {
					.mask = GENMASK(25, 13),
					.regoff = 0x8U,
				},
			},
			/* Dim 2 */
			{
				.step_size = {
					.mask = GENMASK(12, 0),
					.regoff = 0xCU,
				},
			},
		},
	},
	.num_dims = 3,
	.bd_idx_off = 0x20U,
};

static const struct aie_bd_attr aieml_memtilebd = {
	.valid_bd = {
		.mask = BIT(31),
		.regoff = 0x1CU,
	},
	.next_bd = {
		.mask = GENMASK(25, 20),
		.regoff = 0x4U,
	},
	.use_next = {
		.mask = BIT(19),
		.regoff = 0x4U,
	},
	.addr = {
		.addr = {
			.mask = GENMASK(18, 0),
			.regoff = 0x4U,
		},
		.length = {
			.mask = GENMASK(16, 0),
			.regoff = 0x0U,
		},
	},
	.compression_en = {
		.mask = BIT(31),
		.regoff = 0x10U,
	},
	.out_of_order_id = {
		.mask = GENMASK(22, 17),
		.regoff = 0x0U,
	},
	.tlast_suppress = {
		.mask = BIT(31),
		.regoff = 0x8U,
	},
	.lock = {
		.lock_acq_id = {
			.mask = GENMASK(7, 0),
			.regoff = 0x1CU,
		},
		.lock_acq_val = {
			.mask = GENMASK(14, 8),
			.regoff = 0x1CU,
		},
		.lock_acq_en = {
			.mask = BIT(15),
			.regoff = 0x1CU,
		},
		.lock_rel_id = {
			.mask = GENMASK(23, 16),
			.regoff = 0x1CU,
		},
		.lock_rel_val = {
			.mask = GENMASK(30, 24),
			.regoff = 0x1CU,
		},
	},
	.packet = {
		.pkt_en = {
			.mask = BIT(31),
			.regoff = 0x0U,
		},
		.pkt_type = {
			.mask = GENMASK(30, 28),
			.regoff = 0x0U,
		},
		.pkt_id = {
			.mask = GENMASK(27, 23),
			.regoff = 0x0U,
		},
	},
	.aieml_dim = {
		.iter_curr = {
			.mask = GENMASK(28, 23),
			.regoff = 0x18U,
		},
		.iter = {
			.wrap = {
				.mask = GENMASK(22, 17),
				.regoff = 0x18U,
			},
			.step_size = {
				.mask = GENMASK(16, 0),
				.regoff = 0x18U,
			},
		},
		.dims = {
			/* Dim 0 */
			{
				.wrap = {
					.mask = GENMASK(26, 17),
					.regoff = 0x8U,
				},
				.step_size = {
					.mask = GENMASK(16, 0),
					.regoff = 0x8U,
				},
			},
			/* Dim 1 */
			{
				.wrap = {
					.mask = GENMASK(26, 17),
					.regoff = 0xCU,
				},
				.step_size = {
					.mask = GENMASK(16, 0),
					.regoff = 0xCU,
				},
			},
			/* Dim 2 */
			{
				.wrap = {
					.mask = GENMASK(26, 17),
					.regoff = 0x10U,
				},
				.step_size = {
					.mask = GENMASK(16, 0),
					.regoff = 0x10U,
				},
			},
			/* Dim 3 */
			{
				.step_size = {
					.mask = GENMASK(16, 0),
					.regoff = 0x14U,
				},
			},
		},
		.pads = {
			/* Dim 0 */
			{
				.before = {
					.mask = GENMASK(31, 26),
					.regoff = 0x4U,
				},
				.after = {
					.mask = GENMASK(22, 17),
					.regoff = 0x14U,
				},
			},
			/* Dim 1 */
			{
				.before = {
					.mask = GENMASK(31, 27),
					.regoff = 0xCU,
				},
				.after = {
					.mask = GENMASK(27, 23),
					.regoff = 0x14U,
				},
			},
			/* Dim 2 */
			{
				.before = {
					.mask = GENMASK(30, 27),
					.regoff = 0x10U,
				},
				.after = {
					.mask = GENMASK(31, 28),
					.regoff = 0x14U,
				},
			},
		},
	},
	.num_dims = 4,
	.bd_idx_off = 0x20U,
};

static const struct aie_bd_attr aieml_shimbd = {
	.valid_bd = {
		.mask = BIT(25),
		.regoff = 0x1CU,
	},
	.next_bd = {
		.mask = GENMASK(30, 27),
		.regoff = 0x1CU,
	},
	.use_next = {
		.mask = BIT(26),
		.regoff = 0x1CU,
	},
	.addr = {
		.addr = {
			.mask = GENMASK(31, 0),
			.regoff = 0x4U,
		},
		.length = {
			.mask = GENMASK(31, 0),
			.regoff = 0x0U,
		},
	},
	.addr_2 = {
		.addr = {
			.mask = GENMASK(15, 0),
			.regoff = 0x8U,
		},
	},
	.compression_en = {
		.mask = BIT(31),
		.regoff = 0x10U,
	},
	.out_of_order_id = {
		.mask = GENMASK(29, 24),
		.regoff = 0x8U,
	},
	.tlast_suppress = {
		.mask = BIT(31),
		.regoff = 0x1CU,
	},
	.lock = {
		.lock_acq_id = {
			.mask = GENMASK(3, 0),
			.regoff = 0x1CU,
		},
		.lock_acq_val = {
			.mask = GENMASK(11, 5),
			.regoff = 0x1CU,
		},
		.lock_acq_en = {
			.mask = BIT(12),
			.regoff = 0x1CU,
		},
		.lock_rel_id = {
			.mask = GENMASK(16, 13),
			.regoff = 0x1CU,
		},
		.lock_rel_val = {
			.mask = GENMASK(24, 18),
			.regoff = 0x1CU,
		},
	},
	.packet = {
		.pkt_en = {
			.mask = BIT(30),
			.regoff = 0x8U,
		},
		.pkt_type = {
			.mask = GENMASK(18, 16),
			.regoff = 0x8U,
		},
		.pkt_id = {
			.mask = GENMASK(23, 19),
			.regoff = 0x8U,
		},
	},
	.axi = {
		.smid = {
			.mask = GENMASK(31, 28),
			.regoff = 0x14U,
		},
		.cache = {
			.mask = GENMASK(27, 24),
			.regoff = 0x14U,
		},
		.qos = {
			.mask = GENMASK(23, 20),
			.regoff = 0x14U,
		},
		.secure_en = {
			.mask = BIT(30),
			.regoff = 0xCU,
		},
		.burst_len = {
			.mask = GENMASK(31, 30),
			.regoff = 0x10U,
		},
	},
	.aieml_dim = {
		.iter_curr = {
			.mask = GENMASK(31, 26),
			.regoff = 0x18U,
		},
		.iter = {
			.wrap = {
				.mask = GENMASK(25, 20),
				.regoff = 0x18U,
			},
			.step_size = {
				.mask = GENMASK(19, 0),
				.regoff = 0x18U,
			},
		},
		.dims = {
			/* Dim 0 */
			{
				.wrap = {
					.mask = GENMASK(29, 20),
					.regoff = 0xCU,
				},
				.step_size = {
					.mask = GENMASK(19, 0),
					.regoff = 0xCU,
				},
			},
			/* Dim 1 */
			{
				.wrap = {
					.mask = GENMASK(29, 20),
					.regoff = 0x10U,
				},
				.step_size = {
					.mask = GENMASK(19, 0),
					.regoff = 0x10U,
				},
			},
			/* Dim 2 */
			{
				.step_size = {
					.mask = GENMASK(19, 0),
					.regoff = 0x14U,
				},
			},
		},
	},
	.num_dims = 3,
	.bd_idx_off = 0x20U,
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
	.chansts = {
		.mask = BIT(19),
		.regoff = 0x4,
	},
	.qsize = {
		.mask = GENMASK(22, 20),
		.regoff = 0x0,
	},
	.qsts = {
		.mask = BIT(18),
		.regoff = 0x0,
	},
	.curbd = {
		.mask = GENMASK(27, 24),
		.regoff = 0x0,
	},
	.bd_regoff = AIEML_SHIMNOC_BD0_0_REGOFF,
	.num_bds = 16,
	.bd_len = 0x20U,
	.num_mm2s_chan = 2U,
	.num_s2mm_chan = 2U,
	.mm2s_sts_regoff = AIEML_SHIMNOC_DMA_MM2S_STATUS_REGOFF,
	.s2mm_sts_regoff = AIEML_SHIMNOC_DMA_S2MM_STATUS_REGOFF,
};

static const struct aie_dma_attr aieml_tiledma = {
	.chansts = {
		.mask = BIT(19),
		.regoff = 0x4,
	},
	.qsize = {
		.mask = GENMASK(22, 20),
		.regoff = 0x0,
	},
	.qsts = {
		.mask = BIT(18),
		.regoff = 0x0,
	},
	.curbd = {
		.mask = GENMASK(27, 24),
		.regoff = 0x0,
	},
	.bd_regoff = AIEML_TILE_MEMMOD_BD0_0_REGOFF,
	.num_bds = 16,
	.bd_len = 0x18U,
	.num_mm2s_chan = 2U,
	.num_s2mm_chan = 2U,
	.mm2s_sts_regoff = AIEML_TILE_MEMMOD_DMA_MM2S_STATUS_REGOFF,
	.s2mm_sts_regoff = AIEML_TILE_MEMMOD_DMA_S2MM_STATUS_REGOFF,
};

static const struct aie_dma_attr aieml_memtiledma = {
	.chansts = {
		.mask = BIT(19),
		.regoff = 0x4,
	},
	.qsize = {
		.mask = GENMASK(22, 20),
		.regoff = 0x0,
	},
	.qsts = {
		.mask = BIT(18),
		.regoff = 0x0,
	},
	.curbd = {
		.mask = GENMASK(29, 24),
		.regoff = 0x0,
	},
	.bd_regoff = AIEML_MEMORY_BD0_0_REGOFF,
	.num_bds = 48,
	.bd_len = 0x20U,
	.num_mm2s_chan = 6U,
	.num_s2mm_chan = 6U,
	.mm2s_sts_regoff = AIEML_MEMORY_DMA_MM2S_STATUS_REGOFF,
	.s2mm_sts_regoff = AIEML_MEMORY_DMA_S2MM_STATUS_REGOFF,
};

static const struct aie_lock_attr aieml_pl_lock = {
	.sts = {
		.mask = GENMASK(5, 0),
		.regoff = 0x10,
	},
	.sts_regoff = AIEML_SHIMNOC_LOCK_REGOFF,
	.num_locks = 16U,
	.overflow = {
		.mask = GENMASK(15, 0),
		.regoff = 0x4,
	},
	.overflow_regoff = AIEML_SHIMNOC_LOCK_OVERFLOW_REGOFF,
	.underflow = {
		.mask = GENMASK(15, 0),
		.regoff = 0x4,
	},
	.underflow_regoff = AIEML_SHIMNOC_LOCK_UNDERFLOW_REGOFF,
};

static const struct aie_lock_attr aieml_mem_lock = {
	.sts = {
		.mask = GENMASK(5, 0),
		.regoff = 0x10,
	},
	.sts_regoff = AIEML_TILE_MEMMOD_LOCK_REGOFF,
	.num_locks = 16U,
	.overflow = {
		.mask = GENMASK(15, 0),
		.regoff = 0x4,
	},
	.overflow_regoff = AIEML_TILE_MEMMOD_LOCK_OVERFLOW_REGOFF,
	.underflow = {
		.mask = GENMASK(15, 0),
		.regoff = 0x4,
	},
	.underflow_regoff = AIEML_TILE_MEMMOD_LOCK_UNDERFLOW_REGOFF,
};

static const struct aie_lock_attr aieml_memtile_lock = {
	.sts = {
		.mask = GENMASK(5, 0),
		.regoff = 0x10,
	},
	.sts_regoff = AIEML_MEMORY_LOCK_REGOFF,
	.num_locks = 64U,
	.overflow = {
		.mask = GENMASK(31, 0),
		.regoff = 0x4,
	},
	.overflow_regoff = AIEML_MEMORY_LOCK_OVERFLOW_REGOFF,
	.underflow = {
		.mask = GENMASK(31, 0),
		.regoff = 0x4,
	},
	.underflow_regoff = AIEML_MEMORY_LOCK_UNDERFLOW_REGOFF,
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

static const struct aie_l1_intr_ctrl_attr aieml_l1_intr_ctrl = {
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

static const struct aie_l2_intr_ctrl_attr aieml_l2_intr_ctrl = {
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

static char *aieml_dma_chan_status_str[] = {
	"idle",
	"running",
};

static char *aieml_dma_qsts_str[] = {
	"okay",
	"overflow",
};

static const struct aie_dev_attr aieml_aperture_dev_attr[] = {
	AIE_APERTURE_ATTR_RO(hardware_info),
};

static const struct aie_dev_attr aieml_tile_dev_attr[] = {
	AIE_TILE_DEV_ATTR_RO(bd, AIE_TILE_TYPE_MASK_TILE |
			     AIE_TILE_TYPE_MASK_MEMORY |
			     AIE_TILE_TYPE_MASK_SHIMNOC),
	AIE_TILE_DEV_ATTR_RO(core, AIE_TILE_TYPE_MASK_TILE),
	AIE_TILE_DEV_ATTR_RO(dma, AIE_TILE_TYPE_MASK_TILE |
			     AIE_TILE_TYPE_MASK_MEMORY |
			     AIE_TILE_TYPE_MASK_SHIMNOC),
	AIE_TILE_DEV_ATTR_RO(error, AIE_TILE_TYPE_MASK_TILE |
			     AIE_TILE_TYPE_MASK_MEMORY |
			     AIE_TILE_TYPE_MASK_SHIMNOC |
			     AIE_TILE_TYPE_MASK_SHIMPL),
	AIE_TILE_DEV_ATTR_RO(event, AIE_TILE_TYPE_MASK_TILE |
			     AIE_TILE_TYPE_MASK_MEMORY |
			     AIE_TILE_TYPE_MASK_SHIMNOC |
			     AIE_TILE_TYPE_MASK_SHIMPL),
	AIE_TILE_DEV_ATTR_RO(lock, AIE_TILE_TYPE_MASK_TILE |
			     AIE_TILE_TYPE_MASK_MEMORY |
			     AIE_TILE_TYPE_MASK_SHIMNOC),
};

static const struct aie_dev_attr aieml_part_dev_attr[] = {
	AIE_PART_DEV_ATTR_RO(current_freq),
	AIE_PART_DEV_ATTR_RO(error_stat),
};

static const struct aie_bin_attr aieml_part_bin_attr[] = {
	AIE_PART_BIN_ATTR_RO(core, AIEML_PART_SYSFS_CORE_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(lock, AIEML_PART_SYSFS_LOCK_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(dma, AIEML_PART_SYSFS_DMA_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(error, AIEML_PART_SYSFS_ERROR_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(status, AIEML_PART_SYSFS_STATUS_BINA_SIZE),
};

static const struct aie_sysfs_attr aieml_aperture_sysfs_attr = {
	.dev_attr = aieml_aperture_dev_attr,
	.bin_attr = NULL,
	.num_dev_attrs = ARRAY_SIZE(aieml_aperture_dev_attr),
	.num_bin_attrs = 0U,
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

static u32 aieml_get_lock_status(struct aie_partition *apart,
				 struct aie_location *loc, u8 lock)
{
	const struct aie_lock_attr *attr;
	u32 ttype, stsoff, regoff, value;

	ttype = aieml_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		attr = &aieml_mem_lock;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		attr = &aieml_memtile_lock;
	else
		attr = &aieml_pl_lock;

	stsoff = attr->sts.regoff * lock + attr->sts_regoff;
	regoff = aie_cal_regoff(apart->adev, *loc, stsoff);
	value = ioread32(apart->aperture->base + regoff);

	return aie_get_reg_field(&attr->sts, value);
}

static u64 aieml_get_lock_overflow_status(struct aie_partition *apart,
					  struct aie_location *loc)
{
	const struct aie_lock_attr *attr;
	u32 ttype, stsoff, regoff;
	u64 value = 0;

	ttype = aieml_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		attr = &aieml_mem_lock;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		attr = &aieml_memtile_lock;
	else
		attr = &aieml_pl_lock;

	if (ttype != AIE_TILE_TYPE_MEMORY) {
		stsoff = attr->overflow_regoff;
		regoff = aie_cal_regoff(apart->adev, *loc, stsoff);
		value = ioread32(apart->aperture->base + regoff);
		value = aie_get_reg_field(&attr->overflow, value);
	} else {
		stsoff = attr->overflow_regoff;
		regoff = aie_cal_regoff(apart->adev, *loc, stsoff);
		value = ioread32(apart->aperture->base + regoff);

		stsoff = attr->overflow.regoff * 1U + attr->overflow_regoff;
		regoff = aie_cal_regoff(apart->adev, *loc, stsoff);
		value |= ((u64)ioread32(apart->aperture->base + regoff)) << 32;
	}
	return value;
}

static u64 aieml_get_lock_underflow_status(struct aie_partition *apart,
					   struct aie_location *loc)
{
	const struct aie_lock_attr *attr;
	u32 ttype, stsoff, regoff;
	u64 value = 0;

	ttype = aieml_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		attr = &aieml_mem_lock;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		attr = &aieml_memtile_lock;
	else
		attr = &aieml_pl_lock;

	if (ttype != AIE_TILE_TYPE_MEMORY) {
		stsoff = attr->underflow_regoff;
		regoff = aie_cal_regoff(apart->adev, *loc, stsoff);
		value = ioread32(apart->aperture->base + regoff);
		value = aie_get_reg_field(&attr->underflow, value);
	} else {
		stsoff = attr->underflow_regoff;
		regoff = aie_cal_regoff(apart->adev, *loc, stsoff);
		value = ioread32(apart->aperture->base + regoff);

		stsoff = attr->underflow.regoff * 1U + attr->underflow_regoff;
		regoff = aie_cal_regoff(apart->adev, *loc, stsoff);
		value |= ((u64)ioread32(apart->aperture->base + regoff)) << 32;
	}
	return value;
}

static ssize_t aieml_get_tile_sysfs_lock_status(struct aie_partition *apart,
						struct aie_location *loc,
						char *buffer, ssize_t size)
{
	unsigned long overflow, underflow;
	u32 i, ttype, num_locks;
	ssize_t len = 0;

	ttype = aieml_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_SHIMPL)
		return len;

	if (ttype == AIE_TILE_TYPE_TILE)
		num_locks = aieml_mem_lock.num_locks;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		num_locks = aieml_memtile_lock.num_locks;
	else
		num_locks = aieml_pl_lock.num_locks;

	if (!aie_part_check_clk_enable_loc(apart, loc)) {
		for (i = 0; i < num_locks; i++) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%d: clock_gated\n", i);
		}
		return len;
	}

	overflow = aieml_get_lock_overflow_status(apart, loc);

	underflow = aieml_get_lock_underflow_status(apart, loc);

	for (i = 0; i < num_locks; i++) {
		len += scnprintf(&buffer[len], max(0L, size - len), "%d: %d",
				 i, aieml_get_lock_status(apart, loc, i));

		if (test_bit(num_locks, &overflow))
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "|overflow");

		if (test_bit(num_locks, &underflow))
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "|underflow");

		len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	}

	return len;
}

static ssize_t aieml_get_part_sysfs_lock_status(struct aie_partition *apart,
						struct aie_location *loc,
						char *buffer, ssize_t size)
{
	u32 i, ttype, num_locks;
	ssize_t len = 0;

	if (!aie_part_check_clk_enable_loc(apart, loc)) {
		len += scnprintf(&buffer[len], max(0L, size - len),
				 "clock_gated");
		return len;
	}

	ttype = aieml_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		num_locks = aieml_mem_lock.num_locks;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		num_locks = aieml_memtile_lock.num_locks;
	else
		num_locks = aieml_pl_lock.num_locks;

	for (i = 0; i < num_locks; i++) {
		len += scnprintf(&buffer[len], max(0L, size - len), "%d",
				 aieml_get_lock_status(apart, loc, i));

		if (i < num_locks - 1)
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL0);
	}

	return len;
}

/*
 * aieml_get_tile_bd_attr() - gets tile bd attribute for AIEML
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @attr: pointer of attribute to assign
 */
static void aieml_get_tile_bd_attr(struct aie_partition *apart,
				   struct aie_location *loc,
				   const struct aie_bd_attr **attr)
{
	u32 ttype;

	ttype = aieml_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		*attr = &aieml_tilebd;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		*attr = &aieml_memtilebd;
	else
		*attr = &aieml_shimbd;
}

/*
 * aieml_get_tile_dma_attr() - gets tile dma attribute for AIEML
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @attr: pointer of attribute to assign
 */
static void aieml_get_tile_dma_attr(struct aie_partition *apart,
				    struct aie_location *loc,
				    const struct aie_dma_attr **attr)
{
	u32 ttype;

	ttype = aieml_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		*attr = &aieml_tiledma;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		*attr = &aieml_memtiledma;
	else
		*attr = &aieml_shimdma;
}

/**
 * aieml_get_dma_s2mm_status() - reads the DMA stream to memory map status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @chanid: DMA channel ID.
 * @return: 32-bit register value.
 */
static u32 aieml_get_dma_s2mm_status(struct aie_partition *apart,
				     struct aie_location *loc, u8 chanid)
{
	const struct aie_dma_attr *attr;
	u32 stsoff, regoff;

	aieml_get_tile_dma_attr(apart, loc, &attr);

	stsoff = attr->s2mm_sts_regoff + chanid * attr->chansts.regoff;
	regoff = aie_cal_regoff(apart->adev, *loc, stsoff);

	return ioread32(apart->aperture->base + regoff);
}

/**
 * aieml_get_dma_mm2s_status() - reads the DMA memory map to stream status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @chanid: DMA channel ID.
 * @return: 32-bit register value.
 */
static u32 aieml_get_dma_mm2s_status(struct aie_partition *apart,
				     struct aie_location *loc, u8 chanid)
{
	const struct aie_dma_attr *attr;
	u32 stsoff, regoff;

	aieml_get_tile_dma_attr(apart, loc, &attr);

	stsoff = attr->mm2s_sts_regoff + chanid * attr->chansts.regoff;
	regoff = aie_cal_regoff(apart->adev, *loc, stsoff);

	return ioread32(apart->aperture->base + regoff);
}

/**
 * aieml_get_chan_status() - reads the DMA channel status from DMA status value.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @return: 8-bit status value.
 */
static u8 aieml_get_chan_status(struct aie_partition *apart,
				struct aie_location *loc, u32 status)
{
	const struct aie_dma_attr *attr;

	aieml_get_tile_dma_attr(apart, loc, &attr);

	return aie_get_reg_field(&attr->chansts, status);
}

/**
 * aieml_get_queue_size() - reads the DMA queue size from DMA status value.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @return: 8-bit value.
 */
static u8 aieml_get_queue_size(struct aie_partition *apart,
			       struct aie_location *loc, u32 status)
{
	const struct aie_dma_attr *attr;

	aieml_get_tile_dma_attr(apart, loc, &attr);

	return aie_get_reg_field(&attr->qsize, status);
}

/**
 * aieml_get_queue_status() - reads the DMA queue status from DMA status value.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @return: 8-bit status value.
 */
static u8 aieml_get_queue_status(struct aie_partition *apart,
				 struct aie_location *loc, u32 status)
{
	const struct aie_dma_attr *attr;

	aieml_get_tile_dma_attr(apart, loc, &attr);

	return aie_get_reg_field(&attr->qsts, status);
}

/**
 * aieml_get_current_bd() - reads the current buffer descriptor being processed
 *			    by DMA channel from DMA status value.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @return: 8-bit buffer descriptor value.
 */
static u8 aieml_get_current_bd(struct aie_partition *apart,
			       struct aie_location *loc, u32 status)
{
	const struct aie_dma_attr *attr;

	aieml_get_tile_dma_attr(apart, loc, &attr);

	return aie_get_reg_field(&attr->curbd, status);
}

/**
 * aieml_get_part_sysfs_dma_status() - returns the status of DMA in string
 *				       format with MM2S and S2MM type channel
 *				       separated by a ',' symbol. Channels with
 *				       a given type are separated by a '|'
 *				       symbol.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @buffer: location to return DMA status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
static ssize_t aieml_get_part_sysfs_dma_status(struct aie_partition *apart,
					       struct aie_location *loc,
					       char *buffer, ssize_t size)
{
	u32 i, ttype, num_s2mm_chan, num_mm2s_chan;
	bool is_delimit_req = false;
	ssize_t len = 0;

	if (!aie_part_check_clk_enable_loc(apart, loc)) {
		len += scnprintf(buffer, max(0L, size - len),
				 "mm2s: clock_gated%ss2mm: clock_gated",
				 DELIMITER_LEVEL1);
		return len;
	}

	ttype = aieml_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE) {
		num_mm2s_chan = aieml_tiledma.num_mm2s_chan;
		num_s2mm_chan = aieml_tiledma.num_s2mm_chan;
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		num_mm2s_chan = aieml_memtiledma.num_mm2s_chan;
		num_s2mm_chan = aieml_memtiledma.num_s2mm_chan;
	} else {
		num_mm2s_chan = aieml_shimdma.num_mm2s_chan;
		num_s2mm_chan = aieml_shimdma.num_s2mm_chan;
	}

	/* MM2S */
	len += scnprintf(&buffer[len], max(0L, size - len), "mm2s: ");
	for (i = 0; i < num_mm2s_chan; i++) {
		u32 status = aieml_get_dma_mm2s_status(apart, loc, i);
		u32 value = aieml_get_chan_status(apart, loc, status);

		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len),
				 aieml_dma_chan_status_str[value]);
		is_delimit_req = true;
	}

	/* S2MM */
	is_delimit_req = false;
	len += scnprintf(&buffer[len], max(0L, size - len), "%ss2mm: ",
			 DELIMITER_LEVEL1);
	for (i = 0; i < num_s2mm_chan; i++) {
		u32 status = aieml_get_dma_s2mm_status(apart, loc, i);
		u32 value = aieml_get_chan_status(apart, loc, status);

		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len),
				 aieml_dma_chan_status_str[value]);
		is_delimit_req = true;
	}

	return len;
}

/**
 * aieml_get_tile_sysfs_dma_status() - exports AI engine DMA channel status,
 *				       queue size, queue status, and current
 *				       buffer descriptor ID being processed by
 *				       DMA channel to a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @buffer: location to return DMA status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
static ssize_t aieml_get_tile_sysfs_dma_status(struct aie_partition *apart,
					       struct aie_location *loc,
					       char *buffer, ssize_t size)
{
	u32 i, ttype, chan, mm2s[AIE_MAX_MM2S_CH], s2mm[AIE_MAX_S2MM_CH],
	    num_s2mm_chan, num_mm2s_chan;
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
		return len;
	}

	ttype = aieml_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE) {
		num_mm2s_chan = aieml_tiledma.num_mm2s_chan;
		num_s2mm_chan = aieml_tiledma.num_s2mm_chan;
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		num_mm2s_chan = aieml_memtiledma.num_mm2s_chan;
		num_s2mm_chan = aieml_memtiledma.num_s2mm_chan;
	} else {
		num_mm2s_chan = aieml_shimdma.num_mm2s_chan;
		num_s2mm_chan = aieml_shimdma.num_s2mm_chan;
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "channel_status: ");
	len += aieml_get_part_sysfs_dma_status(apart, loc, &buffer[len],
					       max(0L, size - len));

	for (i = 0; i < num_mm2s_chan; i++)
		mm2s[i] = aieml_get_dma_mm2s_status(apart, loc, i);

	for (i = 0; i < num_s2mm_chan; i++)
		s2mm[i] = aieml_get_dma_s2mm_status(apart, loc, i);

	/* Queue size */
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "\nqueue_size: mm2s: ");

	for (chan = 0; chan < num_mm2s_chan; chan++) {
		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len), "%d",
				 aieml_get_queue_size(apart, loc, mm2s[chan]));
		is_delimit_req = true;
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "%ss2mm: ",
			 DELIMITER_LEVEL1);

	is_delimit_req = false;
	for (chan = 0; chan < num_s2mm_chan; chan++) {
		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len), "%d",
				 aieml_get_queue_size(apart, loc, s2mm[chan]));
		is_delimit_req = true;
	}

	/* Queue status */
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "\nqueue_status: mm2s: ");

	is_delimit_req = false;
	for (chan = 0; chan < num_mm2s_chan; chan++) {
		u32 value = aieml_get_queue_status(apart, loc, mm2s[chan]);

		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len),
				 aieml_dma_qsts_str[value]);
		is_delimit_req = true;
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "%ss2mm: ",
			 DELIMITER_LEVEL1);

	is_delimit_req = false;
	for (chan = 0; chan < num_s2mm_chan; chan++) {
		u32 value = aieml_get_queue_status(apart, loc, s2mm[chan]);

		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len),
				 aieml_dma_qsts_str[value]);
		is_delimit_req = true;
	}

	/* Current BD */
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "\ncurrent_bd: mm2s: ");

	is_delimit_req = false;
	for (chan = 0; chan < num_mm2s_chan; chan++) {
		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len), "%d",
				 aieml_get_current_bd(apart, loc, mm2s[chan]));
		is_delimit_req = true;
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "%ss2mm: ",
			 DELIMITER_LEVEL1);

	is_delimit_req = false;
	for (chan = 0; chan < num_s2mm_chan; chan++) {
		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len), "%d",
				 aieml_get_current_bd(apart, loc, s2mm[chan]));
		is_delimit_req = true;
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	return len;
}

/**
 * aieml_get_tile_sysfs_bd_metadata() - exports AI engine DMA buffer descriptor
 *					metadata for all buffer descriptors to
 *					a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA buffer descriptors.
 * @buffer: location to return DMA buffer descriptor metadata string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
static ssize_t aieml_get_tile_sysfs_bd_metadata(struct aie_partition *apart,
						struct aie_location *loc,
						char *buffer, ssize_t size)
{
	const struct aie_dma_attr *dma_attr;
	const struct aie_bd_attr *bd_attr;
	u32 enabled, ttype;
	ssize_t len = 0;

	aieml_get_tile_dma_attr(apart, loc, &dma_attr);
	aieml_get_tile_bd_attr(apart, loc, &bd_attr);

	ttype = aieml_get_tile_type(apart->adev, loc);
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
		index = bd_attr->addr.addr.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->addr.addr, bd_data[index]);
		if (ttype == AIE_TILE_TYPE_SHIMNOC) {
			u32 h_addr;

			/* add high address */
			h_addr = bd_data[bd_attr->addr_2.addr.regoff / sizeof(u32)];
			h_addr = aie_get_reg_field(&bd_attr->addr_2.addr, h_addr);
			value |= (u64)h_addr << 32;
		}
		len += scnprintf(&buffer[len], max(0L, size - len), "%llx%s",
				 value, DELIMITER_LEVEL0);

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
		value = aie_get_reg_field(&bd_attr->lock.lock_rel_id,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->lock.lock_rel_val,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);

		/* packet */
		index = bd_attr->packet.pkt_en.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->packet.pkt_en,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->packet.pkt_id,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
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
		index = bd_attr->tlast_suppress.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->tlast_suppress,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		index = bd_attr->out_of_order_id.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->out_of_order_id,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		if (ttype != AIE_TILE_TYPE_SHIMNOC) {
			index = bd_attr->compression_en.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->compression_en,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
		}

		/* Dimensions */
		index = bd_attr->aieml_dim.iter_curr.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->aieml_dim.iter_curr,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->aieml_dim.iter.step_size,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->aieml_dim.iter.wrap,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);

		for (i = 0; i < bd_attr->num_dims - 1; i++) {
			index = bd_attr->aieml_dim.dims[i].step_size.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->aieml_dim.dims[i].step_size,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			index = bd_attr->aieml_dim.dims[i].wrap.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->aieml_dim.dims[i].wrap,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			/* padding */
			if (ttype == AIE_TILE_TYPE_MEMORY) {
				index = bd_attr->aieml_dim.pads[i].before.regoff / sizeof(u32);
				value = aie_get_reg_field(&bd_attr->aieml_dim.pads[i].before,
							  bd_data[index]);
				len += scnprintf(&buffer[len], max(0L, size - len),
						 "%lld%s", value, DELIMITER_LEVEL0);
				index = bd_attr->aieml_dim.pads[i].after.regoff / sizeof(u32);
				value = aie_get_reg_field(&bd_attr->aieml_dim.pads[i].after,
							  bd_data[index]);
				len += scnprintf(&buffer[len], max(0L, size - len),
						 "%lld%s", value, DELIMITER_LEVEL0);
			}
		}
		index = bd_attr->aieml_dim.dims[i].step_size.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->aieml_dim.dims[i].step_size,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld", value);

		/* axi settings */
		if (ttype == AIE_TILE_TYPE_SHIMNOC) {
			index = bd_attr->axi.smid.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->axi.smid,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%s%lld%s", DELIMITER_LEVEL0, value,
					 DELIMITER_LEVEL0);
			value = aie_get_reg_field(&bd_attr->axi.cache,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			value = aie_get_reg_field(&bd_attr->axi.qos,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			index = bd_attr->axi.secure_en.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->axi.secure_en,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			index = bd_attr->axi.burst_len.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->axi.burst_len,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld", value);
		}

		len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	}

	return len;
}

static u32 aieml_get_core_status(struct aie_partition *apart,
				 struct aie_location *loc)
{
	u32 regoff, regvalue;

	regoff = aie_cal_regoff(apart->adev, *loc, aieml_core_sts.regoff);
	regvalue = ioread32(apart->aperture->base + regoff);

	return aie_get_reg_field(&aieml_core_sts, regvalue);
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
	struct aie_location loc;
	int ret, status;

	for (loc.col = range->start.col;
	     loc.col < range->start.col + range->size.col;
	     loc.col++) {
		u32 startbit, col_inuse = 0;

		startbit = loc.col * (range->size.row - 1);

		for (loc.row = range->start.row + 1;
		     loc.row < range->start.row + range->size.row;
		     loc.row++) {
			u32 nbitpos = startbit + loc.row - 1;

			if (aie_resource_testbit(&apart->tiles_inuse, nbitpos)) {
				col_inuse = 1;
				break;
			}
		}

		if (col_inuse) {
			ret = zynqmp_pm_aie_operation(node_id, loc.col,
						      1,
						      XILINX_AIE_OPS_ENB_COL_CLK_BUFF);
			if (ret < 0) {
				dev_err(&apart->dev,
					"failed to enable clock for column: %d\n",
					loc.col);
				return ret;
			}

			status = aie_resource_set(&apart->tiles_inuse,
						  startbit, apart->range.size.row - 1);
			status = aie_resource_set(&apart->cores_clk_state,
						  startbit, apart->range.size.row - 1);
		} else {
			ret = zynqmp_pm_aie_operation(node_id, loc.col,
						      1,
						      XILINX_AIE_OPS_DIS_COL_CLK_BUFF);
			if (ret < 0) {
				dev_err(&apart->dev,
					"failed to disable clock for column: %d\n",
					loc.col);
				return ret;
			}

			status = aie_resource_clear(&apart->tiles_inuse,
						    startbit, apart->range.size.row - 1);
			status = aie_resource_clear(&apart->cores_clk_state,
						    startbit, apart->range.size.row - 1);
		}
	}

	return status;
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
	.get_part_sysfs_lock_status = aieml_get_part_sysfs_lock_status,
	.get_tile_sysfs_lock_status = aieml_get_tile_sysfs_lock_status,
	.get_part_sysfs_dma_status = aieml_get_part_sysfs_dma_status,
	.get_tile_sysfs_dma_status = aieml_get_tile_sysfs_dma_status,
	.get_tile_sysfs_bd_metadata = aieml_get_tile_sysfs_bd_metadata,
	.init_part_clk_state = aieml_init_part_clk_state,
	.scan_part_clocks = aieml_scan_part_clocks,
	.set_part_clocks = aieml_set_part_clocks,
	.set_tile_isolation = aieml_set_tile_isolation,
	.mem_clear = aieml_part_clear_mems,
	.get_dma_s2mm_status = aieml_get_dma_s2mm_status,
	.get_dma_mm2s_status = aieml_get_dma_mm2s_status,
	.get_chan_status = aieml_get_chan_status,
	.get_lock_status = aieml_get_lock_status,
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
	adev->tile_bd = &aieml_tilebd;
	adev->shim_bd = &aieml_shimbd;
	adev->memtile_bd = &aieml_memtilebd;
	adev->tile_dma = &aieml_tiledma;
	adev->shim_dma = &aieml_shimdma;
	adev->memtile_dma = &aieml_memtiledma;
	adev->aperture_sysfs_attr = &aieml_aperture_sysfs_attr;
	adev->part_sysfs_attr = &aieml_part_sysfs_attr;
	adev->tile_sysfs_attr = &aieml_tile_sysfs_attr;
	adev->core_status_str = aieml_core_status_str;
	adev->core_pc = &aieml_core_pc;
	adev->core_lr = &aieml_core_lr;
	adev->core_sp = &aieml_core_sp;
	adev->pl_events = &aieml_pl_event;
	adev->memtile_events = &aieml_memtile_event;
	adev->mem_events = &aieml_mem_event;
	adev->mem_lock = &aieml_mem_lock;
	adev->pl_lock = &aieml_pl_lock;
	adev->memtile_lock = &aieml_memtile_lock;
	adev->core_events = &aieml_core_event;
	adev->core_errors = &aieml_core_error;
	adev->mem_errors = &aieml_mem_error;
	adev->memtile_errors = &aieml_memtile_error;
	adev->shim_errors = &aieml_shim_error;
	adev->l1_ctrl = &aieml_l1_intr_ctrl;
	adev->l2_ctrl = &aieml_l2_intr_ctrl;
	adev->core_perfctrl = &aieml_core_perfctrl;
	adev->core_perfctrl_reset = &aieml_core_perfctrl_reset;
	adev->core_perfcnt = &aieml_core_perfcnt;
	adev->core_evntgen = &aieml_core_evntgen;
	adev->core_util_events = aieml_core_util_events;

	aieml_device_init_rscs_attr(adev);

	return 0;
}
