// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine driver AIE-2PS device specific implementation
 *
 * Copyright (C) 2023 - 2025 Advanced Micro Devices, Inc.
 */

#include <linux/bitfield.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/xlnx-ai-engine.h>

#include "ai-engine-internal.h"

#define AIE2PS_ARRAY_SHIFT	32U
#define AIE2PS_COL_SHIFT	25U
#define AIE2PS_ROW_SHIFT	20U

#define NUM_TYPES_OF_MEM	6U

#define NUM_MODS_CORE_TILE	2U
#define NUM_MODS_MEM_TILE	1U
#define NUM_MODS_SHIMPL_TILE	1U

/*
 * Number of resources per module
 */
#define AIE2PS_NUM_PERF_TILE_CORE_MOD		4U
#define AIE2PS_NUM_USEREVENT_TILE_CORE_MOD	4U
#define AIE2PS_NUM_TRACECONTROL_TILE_CORE_MOD	1U
#define AIE2PS_NUM_PCEVENT_TILE_CORE_MOD	4U
#define AIE2PS_NUM_SSSELECT_TILE_CORE_MOD	8U
#define AIE2PS_NUM_BROADCAST_TILE_CORE_MOD	16U
#define AIE2PS_NUM_COMBOEVENT_TILE_CORE_MOD	4U
#define AIE2PS_NUM_GROUPEVENTS_TILE_CORE_MOD	9U

#define AIE2PS_NUM_PERF_TILE_MEM_MOD		2U
#define AIE2PS_NUM_USEREVENT_TILE_MEM_MOD	4U
#define AIE2PS_NUM_TRACECONTROL_TILE_MEM_MOD	1U
#define AIE2PS_NUM_PCEVENT_TILE_MEM_MOD		0U
#define AIE2PS_NUM_SSSELECT_TILE_MEM_MOD	0U
#define AIE2PS_NUM_BROADCAST_TILE_MEM_MOD	16U
#define AIE2PS_NUM_COMBOEVENT_TILE_MEM_MOD	4U
#define AIE2PS_NUM_GROUPEVENTS_TILE_MEM_MOD	8U

#define AIE2PS_NUM_PERF_MEM_MOD			4U
#define AIE2PS_NUM_USEREVENT_MEM_MOD		2U
#define AIE2PS_NUM_TRACECONTROL_MEM_MOD		1U
#define AIE2PS_NUM_PCEVENT_MEM_MOD		0U
#define AIE2PS_NUM_SSSELECT_MEM_MOD		8U
#define AIE2PS_NUM_BROADCAST_MEM_MOD		16U
#define AIE2PS_NUM_COMBOEVENT_MEM_MOD		4U
#define AIE2PS_NUM_GROUPEVENTS_MEM_MOD		9U

#define AIE2PS_NUM_PERF_PL_MOD			2U
#define AIE2PS_NUM_USEREVENT_PL_MOD		2U
#define AIE2PS_NUM_TRACECONTROL_PL_MOD		1U
#define AIE2PS_NUM_PCEVENT_PL_MOD		0U
#define AIE2PS_NUM_SSSELECT_PL_MOD		8U
#define AIE2PS_NUM_BROADCAST_PL_MOD		16U
#define AIE2PS_NUM_COMBOEVENT_PL_MOD		4U
#define AIE2PS_NUM_GROUPEVENTS_PL_MOD		6U

/*
 * Register offsets
 */
#define AIE2PS_SHIMNOC_BD0_0_REGOFF			0x00009000U
#define AIE2PS_SHIMNOC_BD15_7_REGOFF			0x000092ecU
#define AIE2PS_SHIMNOC_LOCK_REGOFF			0x00000000U
#define AIE2PS_SHIMNOC_LOCK_OVERFLOW_REGOFF		0x00000120U
#define AIE2PS_SHIMNOC_LOCK_UNDERFLOW_REGOFF		0x00000128U
#define AIE2PS_SHIMNOC_DMA_S2MM_STATUS_REGOFF		0x00009320U
#define AIE2PS_SHIMNOC_DMA_MM2S_STATUS_REGOFF		0x00009328U
#define AIE2PS_SHIMNOC_UCMOD_CORE_CTRL_REGOFF		0x000C0004U

#define AIE2PS_SHIMPL_BISRCACHE_CTRL_REGOFF		0x00036000U
#define AIE2PS_SHIMPL_COLCLOCK_CTRL_REGOFF		0x0007ff20U
#define AIE2PS_SHIMPL_EVENT_BC0_REGOFF			0x00034010U
#define AIE2PS_SHIMPL_EVENT_BC_A_BLOCK_SOUTH_SET	0x00034050U
#define AIE2PS_SHIMPL_EVENT_BC_B_BLOCK_SOUTH_SET	0x00034090U
#define AIE2PS_SHIMPL_EVENT_STATUS0_REGOFF		0x00034200U
#define AIE2PS_SHIMPL_GROUP0_REGOFF			0x00034500U
#define AIE2PS_SHIMPL_L1INTR_MASK_A_REGOFF		0x00035000U
#define AIE2PS_SHIMPL_L1INTR_BLOCK_NORTH_B_REGOFF	0x00035050U
#define AIE2PS_SHIMPL_TILECTRL_REGOFF			0x0007ff40U
#define AIE2PS_SHIMPL_MODRESET_CTRL_0_REGOFF		0x0007ff10U
#define AIE2PS_SHIMPL_MODRESET_CTRL_1_REGOFF		0x0007ff14U

#define AIE2PS_MEMORY_BD0_0_REGOFF			0x000A0000U
#define AIE2PS_MEMORY_GROUP0_REGOFF			0x00094500U
#define AIE2PS_MEMORY_GROUPERROR_REGOFF			0x00094518U
#define AIE2PS_MEMORY_TILECTRL_REGOFF			0x000fff20U
#define AIE2PS_MEMORY_EVENT_BC0_REGOFF			0x00094010U
#define AIE2PS_MEMORY_EVENT_BC_A_BLOCK_SOUTH_SET	0x00094050U
#define AIE2PS_MEMORY_EVENT_BC_B_BLOCK_SOUTH_SET	0x00094090U
#define AIE2PS_MEMORY_EVENT_STATUS0_REGOFF		0x00094200U
#define AIE2PS_MEMORY_MEMCTRL_REGOFF			0x00096048U
#define AIE2PS_MEMORY_LOCK_REGOFF			0x000C0000U
#define AIE2PS_MEMORY_LOCK_OVERFLOW_REGOFF		0x000C0420U
#define AIE2PS_MEMORY_LOCK_UNDERFLOW_REGOFF		0x000C0428U
#define AIE2PS_MEMORY_DMA_S2MM_STATUS_REGOFF		0x000A0660U
#define AIE2PS_MEMORY_DMA_MM2S_STATUS_REGOFF		0x000A0680U

#define AIE2PS_TILE_COREMOD_BMLL0_PART1_REGOFF		0x00030000U
#define AIE2PS_TILE_COREMOD_BMHH7_PART4_REGOFF		0x000307F0U
#define AIE2PS_TILE_COREMOD_X0_PART1_REGOFF		0x00031800U
#define AIE2PS_TILE_COREMOD_X11_PART4_REGOFF		0x00031AF0U
#define AIE2PS_TILE_COREMOD_LDFIFOL0_PART1_REGOFF	0x00032400U
#define AIE2PS_TILE_COREMOD_FIFOXTRA_PART4_REGOFF	0x000325B0U
#define AIE2PS_TILE_COREMOD_EG0_REGOFF			0x00032600U
#define AIE2PS_TILE_COREMOD_EG11_REGOFF			0x000326B0U
#define AIE2PS_TILE_COREMOD_F0_REGOFF			0x00032700U
#define AIE2PS_TILE_COREMOD_F11_REGOFF			0x000327B0U
#define AIE2PS_TILE_COREMOD_R0_REGOFF			0x00032800U
#define AIE2PS_TILE_COREMOD_S3_REGOFF			0x00032CB0U
#define AIE2PS_TILE_COREMOD_SP_REGOFF			0x00032D20U
#define AIE2PS_TILE_COREMOD_GROUPERROR_REGOFF		0x00034510U
#define AIE2PS_TILE_COREMOD_TILECTRL_REGOFF		0x00060020U
#define AIE2PS_TILE_COREMOD_GROUP0_REGOFF		0x00034500U
#define AIE2PS_TILE_COREMOD_EVENT_BC0_REGOFF		0x00034010U
#define AIE2PS_TILE_COREMOD_EVENT_BC_A_BLOCK_SOUTH_SET	0x00034050U
#define AIE2PS_TILE_COREMOD_EVENT_STATUS0_REGOFF	0x00034200U
#define AIE2PS_TILE_COREMOD_MEMCTRL_REGOFF		0x00036070U
#define AIE2PS_TILE_COREMOD_MODRESETCTRL_REGOFF		0x00060010U
#define AIE2PS_TILE_COREMOD_CORE_STATUS_REGOFF		0x00038004U
#define AIE2PS_TILE_COREMOD_ERROR_HALT_EVENT_REGOFF	0x00038034U
#define AIE2PS_TILE_COREMOD_CORE_PC_REGOFF		0x00032d00U
#define AIE2PS_TILE_COREMOD_CORE_SP_REGOFF		0x00032d20U
#define AIE2PS_TILE_COREMOD_CORE_LR_REGOFF		0x00032d30U
#define AIE2PS_TILE_MEMMOD_BD0_0_REGOFF			0x0001D000U
#define AIE2PS_TILE_MEMMOD_GROUPERROR_REGOFF		0x00014514U
#define AIE2PS_TILE_MEMMOD_GROUP0_REGOFF		0x00014500U
#define AIE2PS_TILE_MEMMOD_EVENT_BC0_REGOFF		0x00014010U
#define AIE2PS_TILE_MEMMOD_EVENT_BC_B_BLOCK_SOUTH_SET	0x00014050U
#define AIE2PS_TILE_MEMMOD_EVENT_STATUS0_REGOFF		0x00014200U
#define AIE2PS_TILE_MEMMOD_MEMCTRL_REGOFF		0x00016010U
#define AIE2PS_TILE_MEMMOD_LOCK_REGOFF			0x0001F000U
#define AIE2PS_TILE_MEMMOD_LOCK_OVERFLOW_REGOFF		0x0001F120U
#define AIE2PS_TILE_MEMMOD_LOCK_UNDERFLOW_REGOFF	0x0001F128U
#define AIE2PS_TILE_MEMMOD_DMA_S2MM_STATUS_REGOFF	0x0001DF00U
#define AIE2PS_TILE_MEMMOD_DMA_MM2S_STATUS_REGOFF	0x0001DF10U

/*
 * Register masks
 */
#define AIE2PS_SHIMPL_COLCLOCK_CTRL_MASK		GENMASK(1, 0)

/* Macros to define size of a sysfs binary attribute */
#define AIE2PS_PART_SYSFS_CORE_BINA_SIZE	0x4000		/* 16KB */
#define AIE2PS_PART_SYSFS_LOCK_BINA_SIZE	0x28000		/* 160KB */
#define AIE2PS_PART_SYSFS_ERROR_BINA_SIZE	0x4000		/* 16KB */
#define AIE2PS_PART_SYSFS_DMA_BINA_SIZE		0xC800		/* 50KB */
#define AIE2PS_PART_SYSFS_STATUS_BINA_SIZE	0x3c000		/* 240KB */

static const struct aie_tile_regs aie2ps_kernel_regs[] = {
	/* SHIM DMA buffer descriptor address range */
	{.attribute = AIE_TILE_TYPE_MASK_SHIMNOC << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_SHIMNOC_BD0_0_REGOFF,
	 .eoff = AIE2PS_SHIMNOC_BD15_7_REGOFF,
	},
	/* SHIM BISR cache control */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		      AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_SHIMPL_BISRCACHE_CTRL_REGOFF,
	 .eoff = AIE2PS_SHIMPL_BISRCACHE_CTRL_REGOFF,
	},
	/* SHIM tile control */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_SHIMPL_TILECTRL_REGOFF,
	 .eoff = AIE2PS_SHIMPL_TILECTRL_REGOFF,
	},
	/* SHIM 1st level interrupt controller */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_SHIMPL_L1INTR_MASK_A_REGOFF,
	 .eoff = AIE2PS_SHIMPL_L1INTR_BLOCK_NORTH_B_REGOFF,
	},
	/* SHIM module reset control */
	{.attribute = (AIE_TILE_TYPE_MASK_SHIMPL | AIE_TILE_TYPE_MASK_SHIMNOC) <<
		AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_SHIMPL_MODRESET_CTRL_0_REGOFF,
	 .eoff = AIE2PS_SHIMPL_MODRESET_CTRL_1_REGOFF,
	},
	/* MEMORY tile group error enable */
	{.attribute = AIE_TILE_TYPE_MASK_MEMORY << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_MEMORY_GROUPERROR_REGOFF,
	 .eoff = AIE2PS_MEMORY_GROUPERROR_REGOFF,
	},
	/* MEMORY mem tile control */
	{.attribute = AIE_TILE_TYPE_MASK_MEMORY << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_MEMORY_TILECTRL_REGOFF,
	 .eoff = AIE2PS_MEMORY_TILECTRL_REGOFF,
	},
	/* MEMORY tile mem control */
	{.attribute = AIE_TILE_TYPE_MASK_MEMORY << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_MEMORY_MEMCTRL_REGOFF,
	 .eoff = AIE2PS_MEMORY_MEMCTRL_REGOFF,
	},
	/* TILE core module group error enable */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_TILE_COREMOD_GROUPERROR_REGOFF,
	 .eoff = AIE2PS_TILE_COREMOD_GROUPERROR_REGOFF,
	},
	/* TILE tile control */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_TILE_COREMOD_TILECTRL_REGOFF,
	 .eoff = AIE2PS_TILE_COREMOD_TILECTRL_REGOFF,
	},
	/* TILE memory control */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_TILE_COREMOD_MEMCTRL_REGOFF,
	 .eoff = AIE2PS_TILE_COREMOD_MEMCTRL_REGOFF,
	},
	/* TILE module reset control */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_TILE_COREMOD_MODRESETCTRL_REGOFF,
	 .eoff = AIE2PS_TILE_COREMOD_MODRESETCTRL_REGOFF,
	},
	/* TILE memory module group error enable */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_TILE_MEMMOD_GROUPERROR_REGOFF,
	 .eoff = AIE2PS_TILE_MEMMOD_GROUPERROR_REGOFF,
	},
	/* TILE memory module mem control */
	{.attribute = AIE_TILE_TYPE_MASK_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	 .soff = AIE2PS_TILE_MEMMOD_MEMCTRL_REGOFF,
	 .eoff = AIE2PS_TILE_MEMMOD_MEMCTRL_REGOFF,
	},
};

/* resource attributes for core tile type */
static const
struct aie_tile_rsc_attr aie2ps_core_tile_rscs_attr[AIE_RSCTYPE_MAX] = {
	{
		/* perf counter */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_PERF_TILE_MEM_MOD,},
			{.num_rscs = AIE2PS_NUM_PERF_TILE_CORE_MOD,},
		},
	},
	{
		/* user event */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_USEREVENT_TILE_MEM_MOD,},
			{.num_rscs = AIE2PS_NUM_USEREVENT_TILE_CORE_MOD,},
		},
	},
	{
		/* trace control */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_TRACECONTROL_TILE_MEM_MOD,},
			{.num_rscs = AIE2PS_NUM_TRACECONTROL_TILE_CORE_MOD,},
		},
	},
	{
		/* pc event */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_PCEVENT_TILE_MEM_MOD,},
			{.num_rscs = AIE2PS_NUM_PCEVENT_TILE_CORE_MOD,},
		},
	},
	{
		/* stream switch port select */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_SSSELECT_TILE_MEM_MOD,},
			{.num_rscs = AIE2PS_NUM_SSSELECT_TILE_CORE_MOD,},
		},
	},
	{
		/* broadcast */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_BROADCAST_TILE_MEM_MOD,},
			{.num_rscs = AIE2PS_NUM_BROADCAST_TILE_CORE_MOD,},
		},
	},
	{
		/* combo events */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_COMBOEVENT_TILE_MEM_MOD,},
			{.num_rscs = AIE2PS_NUM_COMBOEVENT_TILE_CORE_MOD,},
		},
	},
	{
		/* group events */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_GROUPEVENTS_TILE_MEM_MOD,},
			{.num_rscs = AIE2PS_NUM_GROUPEVENTS_TILE_CORE_MOD,},
		},
	},
};

/* resource attributes for mem tile type */
static const
struct aie_tile_rsc_attr aie2ps_mem_tile_rscs_attr[AIE_RSCTYPE_MAX] = {
	{
		/* perf counter */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_PERF_MEM_MOD,},
		},
	},
	{
		/* user event */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_USEREVENT_MEM_MOD,},
		},
	},
	{
		/* trace control */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_TRACECONTROL_MEM_MOD,},
		},
	},
	{
		/* pc event */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_PCEVENT_MEM_MOD,},
		},
	},
	{
		/* stream switch port select */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_SSSELECT_MEM_MOD,},
		},
	},
	{
		/* broadcast */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_BROADCAST_MEM_MOD,},
		},
	},
	{
		/* combo events */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_COMBOEVENT_MEM_MOD,},
		},
	},
	{
		/* group events */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_GROUPEVENTS_MEM_MOD,},
		},
	},
};

/* resource attributes for shim tile type */
static const
struct aie_tile_rsc_attr aie2ps_shimpl_tile_rscs_attr[AIE_RSCTYPE_MAX] = {
	{
		/* perf counter */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_PERF_PL_MOD,},
		},
	},
	{
		/* user event */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_USEREVENT_PL_MOD,},
		},
	},
	{
		/* trace control */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_TRACECONTROL_PL_MOD,},
		},
	},
	{
		/* pc event */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_PCEVENT_PL_MOD,},
		},
	},
	{
		/* stream switch port select */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_SSSELECT_PL_MOD,},
		},
	},
	{
		/* broadcast */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_BROADCAST_PL_MOD,},
		},
	},
	{
		/* combo events */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_COMBOEVENT_PL_MOD,},
		},
	},
	{
		/* group events */
		.mod_attr = {
			{.num_rscs = AIE2PS_NUM_GROUPEVENTS_PL_MOD,},
		},
	},
};

/* modules types array of CORE tile */
static const
enum aie_module_type aie2ps_core_tile_module_types[NUM_MODS_CORE_TILE] = {
	AIE_MEM_MOD,
	AIE_CORE_MOD,
};

/* modules types array of MEM tile */
static const
enum aie_module_type aie2ps_mem_tile_module_types[NUM_MODS_MEM_TILE] = {
	AIE_MEM_MOD,
};

/* modules types array of SHIM PL tile */
static const
enum aie_module_type aie2ps_shimpl_tile_module_types[NUM_MODS_SHIMPL_TILE] = {
	AIE_PL_MOD,
};

static const struct aie_event_prop aie2ps_core_stream_error_prop[] = {
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

static const struct aie_event_prop aie2ps_core_inst_error_prop[] = {
	{
		.event = 59U,
		.event_str = "instruction_decompression_error",
	},
	{
		.event = 70U,
		.event_str = "decompression_underflow",
	},
};

static const struct aie_event_prop aie2ps_core_ecc_error_prop[] = {
	{
		.event = 64U,
		.event_str = "pm_ecc_error_2-bit",
	},
	{
		.event = 62U,
		.event_str = "pm_ecc_error_scrub_2-bit",
	},
};

static const struct aie_event_prop aie2ps_core_access_error_prop[] = {
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

static const struct aie_event_prop aie2ps_core_lock_error_prop[] = {
	{
		.event = 67U,
		.event_str = "lock_access_to_unavailable",
	},
	{
		.event = 72U,
		.event_str = "processor_bus_error",
	},
};

static const struct aie_event_prop aie2ps_core_bus_error_prop[] = {
	{
		.event = 58U,
		.event_str = "axi_mm_slave_error",
	},
};

static const struct aie_event_prop aie2ps_mem_ecc_error_prop[] = {
	{
		.event = 88U,
		.event_str = "dm_ecc_error_scrub_2-bit",
	},
	{
		.event = 90U,
		.event_str = "dm_ecc_error_2-bit",
	},
};

static const struct aie_event_prop aie2ps_mem_parity_error_prop[] = {
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

static const struct aie_event_prop aie2ps_mem_dma_error_prop[] = {
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

static const struct aie_event_prop aie2ps_memtile_ecc_error_prop[] = {
	{
		.event = 132U,
		.event_str = "dm_ecc_error_2-bit",
	},
	{
		.event = 130U,
		.event_str = "dm_ecc_error_scrub_2-bit",
	},
};

static const struct aie_event_prop aie2ps_memtile_dma_error_prop[] = {
	{
		.event = 134U,
		.event_str = "dma_mm2s_error",
	},
	{
		.event = 133U,
		.event_str = "dma_s2mm_error",
	},
};

static const struct aie_event_prop aie2ps_memtile_stream_error_prop[] = {
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

static const struct aie_event_prop aie2ps_memtile_lock_error_prop[] = {
	{
		.event = 139U,
		.event_str = "lock_error",
	},
};

static const struct aie_event_prop aie2ps_memtile_bus_error_prop[] = {
	{
		.event = 58U,
		.event_str = "axi_mm_slave_error",
	},
};

static const struct aie_event_prop aie2ps_shim_bus_error_prop[] = {
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

static const struct aie_event_prop aie2ps_shim_stream_error_prop[] = {
	{
		.event = 66U,
		.event_str = "stream_switch_port_parity_error",
	},
	{
		.event = 65U,
		.event_str = "control_pkt_error",
	},
};

static const struct aie_event_prop aie2ps_shim_dma_error_prop[] = {
	{
		.event = 73U,
		.event_str = "dma_mm2s_error",
	},
	{
		.event = 72U,
		.event_str = "dma_s2mm_error",
	},
};

static const struct aie_err_category aie2ps_core_err_category[] = {
	{
		/* AIE_ERROR_CATEGORY_STREAM */
		.err_category = AIE_ERROR_CATEGORY_STREAM,
		.num_events = ARRAY_SIZE(aie2ps_core_stream_error_prop),
		.prop = aie2ps_core_stream_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_ACCESS */
		.err_category = AIE_ERROR_CATEGORY_ACCESS,
		.num_events = ARRAY_SIZE(aie2ps_core_access_error_prop),
		.prop = aie2ps_core_access_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_BUS */
		.err_category = AIE_ERROR_CATEGORY_BUS,
		.num_events = ARRAY_SIZE(aie2ps_core_bus_error_prop),
		.prop = aie2ps_core_bus_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_INSTRUCTION */
		.err_category = AIE_ERROR_CATEGORY_INSTRUCTION,
		.num_events = ARRAY_SIZE(aie2ps_core_inst_error_prop),
		.prop = aie2ps_core_inst_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_ECC */
		.err_category = AIE_ERROR_CATEGORY_ECC,
		.num_events = ARRAY_SIZE(aie2ps_core_ecc_error_prop),
		.prop = aie2ps_core_ecc_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_LOCK */
		.err_category = AIE_ERROR_CATEGORY_LOCK,
		.num_events = ARRAY_SIZE(aie2ps_core_lock_error_prop),
		.prop = aie2ps_core_lock_error_prop,
	},
};

static const struct aie_err_category aie2ps_mem_err_category[] = {
	{
		/* AIE_ERROR_CATEGORY_ECC */
		.err_category = AIE_ERROR_CATEGORY_ECC,
		.num_events = ARRAY_SIZE(aie2ps_mem_ecc_error_prop),
		.prop = aie2ps_mem_ecc_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_MEM_PARITY */
		.err_category = AIE_ERROR_CATEGORY_MEM_PARITY,
		.num_events = ARRAY_SIZE(aie2ps_mem_parity_error_prop),
		.prop = aie2ps_mem_parity_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_DMA */
		.err_category = AIE_ERROR_CATEGORY_DMA,
		.num_events = ARRAY_SIZE(aie2ps_mem_dma_error_prop),
		.prop = aie2ps_mem_dma_error_prop,
	},
};

static const struct aie_err_category aie2ps_memtile_err_category[] = {
	{
		/* AIE_ERROR_CATEGORY_ECC */
		.err_category = AIE_ERROR_CATEGORY_ECC,
		.num_events = ARRAY_SIZE(aie2ps_memtile_ecc_error_prop),
		.prop = aie2ps_memtile_ecc_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_STREAM */
		.err_category = AIE_ERROR_CATEGORY_STREAM,
		.num_events = ARRAY_SIZE(aie2ps_memtile_stream_error_prop),
		.prop = aie2ps_memtile_stream_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_DMA */
		.err_category = AIE_ERROR_CATEGORY_DMA,
		.num_events = ARRAY_SIZE(aie2ps_memtile_dma_error_prop),
		.prop = aie2ps_memtile_dma_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_BUS */
		.err_category = AIE_ERROR_CATEGORY_BUS,
		.num_events = ARRAY_SIZE(aie2ps_memtile_bus_error_prop),
		.prop = aie2ps_memtile_bus_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_LOCK */
		.err_category = AIE_ERROR_CATEGORY_LOCK,
		.num_events = ARRAY_SIZE(aie2ps_memtile_lock_error_prop),
		.prop = aie2ps_memtile_lock_error_prop,
	},
};

static const struct aie_err_category aie2ps_shim_err_category[] = {
	{
		/* AIE_ERROR_CATEGORY_BUS */
		.err_category = AIE_ERROR_CATEGORY_BUS,
		.num_events = ARRAY_SIZE(aie2ps_shim_bus_error_prop),
		.prop = aie2ps_shim_bus_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_STREAM */
		.err_category = AIE_ERROR_CATEGORY_STREAM,
		.num_events = ARRAY_SIZE(aie2ps_shim_stream_error_prop),
		.prop = aie2ps_shim_stream_error_prop,
	},
	{
		/* AIE_ERROR_CATEGORY_DMA */
		.err_category = AIE_ERROR_CATEGORY_DMA,
		.num_events = ARRAY_SIZE(aie2ps_shim_dma_error_prop),
		.prop = aie2ps_shim_dma_error_prop,
	},
};

static const struct aie_error_attr aie2ps_core_error = {
	.num_err_categories = ARRAY_SIZE(aie2ps_core_err_category),
	.err_category = aie2ps_core_err_category,
};

static const struct aie_error_attr aie2ps_mem_error = {
	.num_err_categories = ARRAY_SIZE(aie2ps_mem_err_category),
	.err_category = aie2ps_mem_err_category,
};

static const struct aie_error_attr aie2ps_memtile_error = {
	.num_err_categories = ARRAY_SIZE(aie2ps_memtile_err_category),
	.err_category = aie2ps_memtile_err_category,
};

static const struct aie_error_attr aie2ps_shim_error = {
	.num_err_categories = ARRAY_SIZE(aie2ps_shim_err_category),
	.err_category = aie2ps_shim_err_category,
};

static const struct aie_tile_regs aie2ps_core_regs_clr[] = {
	{.soff = AIE2PS_TILE_COREMOD_BMLL0_PART1_REGOFF,
	 .eoff = AIE2PS_TILE_COREMOD_BMHH7_PART4_REGOFF,
	 .width = 16,	/* 128 bits */
	 .step = 16,	/* 0x10 */
	 .attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	},
	{.soff = AIE2PS_TILE_COREMOD_X0_PART1_REGOFF,
	 .eoff = AIE2PS_TILE_COREMOD_X11_PART4_REGOFF,
	 .width = 16,	/* 128 bits */
	 .step = 16,	/* 0x10 */
	 .attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	},
	{.soff = AIE2PS_TILE_COREMOD_LDFIFOL0_PART1_REGOFF,
	 .eoff = AIE2PS_TILE_COREMOD_FIFOXTRA_PART4_REGOFF,
	 .width = 16,	/* 128 bits */
	 .step = 16,	/* 0x10 */
	 .attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	},
	{.soff = AIE2PS_TILE_COREMOD_EG0_REGOFF,
	 .eoff = AIE2PS_TILE_COREMOD_EG11_REGOFF,
	 .width = 16,	/* 128 bits */
	 .step = 16,	/* 0x10 */
	 .attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	},
	{.soff = AIE2PS_TILE_COREMOD_F0_REGOFF,
	 .eoff = AIE2PS_TILE_COREMOD_F11_REGOFF,
	 .width = 16,	/* 128 bits */
	 .step = 16,	/* 0x10 */
	 .attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	},
	{.soff = AIE2PS_TILE_COREMOD_R0_REGOFF,
	 .eoff = AIE2PS_TILE_COREMOD_S3_REGOFF,
	 .width = 4,	/* 32 bits */
	 .step = 16,	/* 0x10 */
	 .attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	},
	{.soff = AIE2PS_TILE_COREMOD_SP_REGOFF,
	 .eoff = AIE2PS_TILE_COREMOD_SP_REGOFF,
	 .width = 4,	/* 32 bits */
	 .step = 4,
	 .attribute = AIE_TILE_TYPE_TILE << AIE_REGS_ATTR_TILE_TYPE_SHIFT,
	},
};

static const struct aie_single_reg_field aie2ps_core_sts = {
	.mask = GENMASK(21, 0),
	.regoff = AIE2PS_TILE_COREMOD_CORE_STATUS_REGOFF,
};

static const struct aie_lock_attr aie2ps_mem_lock = {
	.sts = {
		.mask = GENMASK(5, 0),
		.regoff = 0x10,
	},
	.sts_regoff = AIE2PS_TILE_MEMMOD_LOCK_REGOFF,
	.num_locks = 16U,
	.overflow = {
		.mask = GENMASK(15, 0),
		.regoff = 0x4,
	},
	.overflow_regoff = AIE2PS_TILE_MEMMOD_LOCK_OVERFLOW_REGOFF,
	.underflow = {
		.mask = GENMASK(15, 0),
		.regoff = 0x4,
	},
	.underflow_regoff = AIE2PS_TILE_MEMMOD_LOCK_UNDERFLOW_REGOFF,
};

static const struct aie_lock_attr aie2ps_memtile_lock = {
	.sts = {
		.mask = GENMASK(5, 0),
		.regoff = 0x10,
	},
	.sts_regoff = AIE2PS_MEMORY_LOCK_REGOFF,
	.num_locks = 64U,
	.overflow = {
		.mask = GENMASK(31, 0),
		.regoff = 0x4,
	},
	.overflow_regoff = AIE2PS_MEMORY_LOCK_OVERFLOW_REGOFF,
	.underflow = {
		.mask = GENMASK(31, 0),
		.regoff = 0x4,
	},
	.underflow_regoff = AIE2PS_MEMORY_LOCK_UNDERFLOW_REGOFF,
};

static const struct aie_lock_attr aie2ps_pl_lock = {
	.sts = {
		.mask = GENMASK(5, 0),
		.regoff = 0x10,
	},
	.sts_regoff = AIE2PS_SHIMNOC_LOCK_REGOFF,
	.num_locks = 16U,
	.overflow = {
		.mask = GENMASK(15, 0),
		.regoff = 0x4,
	},
	.overflow_regoff = AIE2PS_SHIMNOC_LOCK_OVERFLOW_REGOFF,
	.underflow = {
		.mask = GENMASK(15, 0),
		.regoff = 0x4,
	},
	.underflow_regoff = AIE2PS_SHIMNOC_LOCK_UNDERFLOW_REGOFF,
};

static u32 aie2ps_get_tile_type(struct aie_device *adev, struct aie_location *loc)
{
	u8 num_mem_rows = adev->ttype_attr[AIE_TILE_TYPE_MEMORY].num_rows;

	if (loc->row > num_mem_rows)
		return AIE_TILE_TYPE_TILE;

	if (loc->row)
		return AIE_TILE_TYPE_MEMORY;

	return AIE_TILE_TYPE_SHIMNOC;
}

static unsigned int aie2ps_get_mem_info(struct aie_device *adev, struct aie_range *range,
					struct aie_part_mem *pmem)
{
	u8 start_row, num_rows;
	unsigned int i;

	if (!pmem)
		return NUM_TYPES_OF_MEM;

	/* SHIM row only, no memories in this range */
	if (range->start.row + range->size.row <= 1)
		return 0;

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

	start_row = adev->ttype_attr[AIE_TILE_TYPE_SHIMPL].start_row;
	num_rows = adev->ttype_attr[AIE_TILE_TYPE_SHIMPL].num_rows;
	/* Setup uc program memory information */
	pmem[3].mem.offset = 0x80000;
	pmem[3].mem.size = KBYTES(32);
	pmem[3].mem.range.start.row = start_row;
	pmem[3].mem.range.size.row = num_rows;

	/* Setup uc private data memory information */
	pmem[4].mem.offset = 0x88000;
	pmem[4].mem.size = KBYTES(16);
	pmem[4].mem.range.start.row = start_row;
	pmem[4].mem.range.size.row = num_rows;

	/* Setup uc shared data memory information */
	pmem[5].mem.offset = 0xD0000;
	pmem[5].mem.size = KBYTES(32);
	pmem[5].mem.range.start.row = start_row;
	pmem[5].mem.range.size.row = num_rows;

	return NUM_TYPES_OF_MEM;
}

static u32 aie2ps_get_core_status(struct aie_partition *apart, struct aie_location *loc)
{
	u32 regoff, regvalue;

	regoff = aie_cal_regoff(apart->adev, *loc, aie2ps_core_sts.regoff);
	regvalue = ioread32(apart->aperture->base + regoff);

	return aie_get_reg_field(&aie2ps_core_sts, regvalue);
}

static u32 aie2ps_get_lock_status(struct aie_partition *apart,
				  struct aie_location *loc, u8 lock)
{
	const struct aie_lock_attr *attr;
	u32 ttype, stsoff, regoff, value;

	ttype = aie2ps_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		attr = &aie2ps_mem_lock;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		attr = &aie2ps_memtile_lock;
	else
		attr = &aie2ps_pl_lock;

	stsoff = attr->sts.regoff * lock + attr->sts_regoff;
	regoff = aie_cal_regoff(apart->adev, *loc, stsoff);
	value = ioread32(apart->aperture->base + regoff);

	return aie_get_reg_field(&attr->sts, value);
}

static ssize_t aie2ps_get_part_sysfs_lock_status(struct aie_partition *apart,
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

	ttype = aie2ps_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		num_locks = aie2ps_mem_lock.num_locks;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		num_locks = aie2ps_memtile_lock.num_locks;
	else
		num_locks = aie2ps_pl_lock.num_locks;

	for (i = 0; i < num_locks; i++) {
		len += scnprintf(&buffer[len], max(0L, size - len), "%d",
				 aie2ps_get_lock_status(apart, loc, i));

		if (i < num_locks - 1)
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL0);
	}

	return len;
}

static u64 aie2ps_get_lock_overflow_status(struct aie_partition *apart,
					   struct aie_location *loc)
{
	const struct aie_lock_attr *attr;
	u32 ttype, stsoff, regoff;
	u64 value = 0;

	ttype = aie2ps_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		attr = &aie2ps_mem_lock;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		attr = &aie2ps_memtile_lock;
	else
		attr = &aie2ps_pl_lock;

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

static u64 aie2ps_get_lock_underflow_status(struct aie_partition *apart,
					    struct aie_location *loc)
{
	const struct aie_lock_attr *attr;
	u32 ttype, stsoff, regoff;
	u64 value = 0;

	ttype = aie2ps_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		attr = &aie2ps_mem_lock;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		attr = &aie2ps_memtile_lock;
	else
		attr = &aie2ps_pl_lock;

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

static ssize_t aie2ps_get_tile_sysfs_lock_status(struct aie_partition *apart,
						 struct aie_location *loc,
						 char *buffer, ssize_t size)
{
	unsigned long overflow, underflow;
	u32 i, ttype, num_locks;
	ssize_t len = 0;

	ttype = aie2ps_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_SHIMPL)
		return len;

	if (ttype == AIE_TILE_TYPE_TILE)
		num_locks = aie2ps_mem_lock.num_locks;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		num_locks = aie2ps_memtile_lock.num_locks;
	else
		num_locks = aie2ps_pl_lock.num_locks;

	if (!aie_part_check_clk_enable_loc(apart, loc)) {
		for (i = 0; i < num_locks; i++) {
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%d: clock_gated\n", i);
		}
		return len;
	}

	overflow = aie2ps_get_lock_overflow_status(apart, loc);

	underflow = aie2ps_get_lock_underflow_status(apart, loc);

	for (i = 0; i < num_locks; i++) {
		len += scnprintf(&buffer[len], max(0L, size - len), "%d: %d",
				 i, aie2ps_get_lock_status(apart, loc, i));

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

static const struct aie_tile_operations aie2ps_ops = {
	.get_tile_type = aie2ps_get_tile_type,
	.get_mem_info = aie2ps_get_mem_info,
	.get_core_status = aie2ps_get_core_status,
	.get_part_sysfs_lock_status = aie2ps_get_part_sysfs_lock_status,
	.get_tile_sysfs_lock_status = aie2ps_get_tile_sysfs_lock_status,
	.get_lock_status = aie2ps_get_lock_status,
};

/**
 * aie2ps_device_init_rscs_attr() - initialize AI engine device resources
 *				   attributes
 * @adev: AI engine device
 */
static void aie2ps_device_init_rscs_attr(struct aie_device *adev)
{
	struct aie_tile_attr *tattr;

	tattr = &adev->ttype_attr[AIE_TILE_TYPE_TILE];
	tattr->num_mods = NUM_MODS_CORE_TILE;
	tattr->rscs_attr = aie2ps_core_tile_rscs_attr;
	tattr->mods = aie2ps_core_tile_module_types;

	tattr = &adev->ttype_attr[AIE_TILE_TYPE_MEMORY];
	tattr->num_mods = NUM_MODS_MEM_TILE;
	tattr->rscs_attr = aie2ps_mem_tile_rscs_attr;
	tattr->mods = aie2ps_mem_tile_module_types;

	tattr = &adev->ttype_attr[AIE_TILE_TYPE_SHIMPL];
	tattr->num_mods = NUM_MODS_SHIMPL_TILE;
	tattr->rscs_attr = aie2ps_shimpl_tile_rscs_attr;
	tattr->mods = aie2ps_shimpl_tile_module_types;

	/*
	 * For now, SHIMNOC is the same as SHIMPL as there is
	 * no SHIMNOC specific resources managed by kernel
	 * driver yet.
	 */
	tattr = &adev->ttype_attr[AIE_TILE_TYPE_SHIMNOC];
	tattr->num_mods = NUM_MODS_SHIMPL_TILE;
	tattr->rscs_attr = aie2ps_shimpl_tile_rscs_attr;
	tattr->mods = aie2ps_shimpl_tile_module_types;
}

int aie2ps_device_init(struct aie_device *adev)
{
	adev->ops = &aie2ps_ops;
	adev->num_kernel_regs = ARRAY_SIZE(aie2ps_kernel_regs);
	adev->kernel_regs = aie2ps_kernel_regs;
	adev->core_regs_clr = aie2ps_core_regs_clr;
	adev->num_core_regs_clr = ARRAY_SIZE(aie2ps_core_regs_clr);
	adev->core_errors = &aie2ps_core_error;
	adev->mem_errors = &aie2ps_mem_error;
	adev->memtile_errors = &aie2ps_memtile_error;
	adev->shim_errors = &aie2ps_shim_error;
	adev->mem_lock = &aie2ps_mem_lock;
	adev->pl_lock = &aie2ps_pl_lock;
	adev->memtile_lock = &aie2ps_memtile_lock;
	aie2ps_device_init_rscs_attr(adev);

	return 0;
}
