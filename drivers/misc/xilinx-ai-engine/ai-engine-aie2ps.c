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

#define UC_PROG_MEM		3U
#define UC_PROG_MEM_NUM		1U
#define UC_DATA_MEM		4U
#define UC_DATA_MEM_NUM		2U

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
#define AIE2PS_SHIMNOC_AXI_OUTSTANDING_TX_REGOFF	0x00002120U
#define AIE2PS_UCMOD_AXI_OUTSTANDING_TX_REGOFF		0x000C0024U
#define AIE2PS_SHIMNOC_UCMOD_MEM_PRIV_REGOFF		0x000C0034U
#define AIE2PS_SHIMNOC_UCMOD_MEM_DM_ECC_ERR_GEN		0x000C003CU
#define AIE2PS_SHIMNOC_UCMOD_UCVIEW_PM_OFFSET		0x00000000U
#define AIE2PS_SHIMNOC_UCMOD_UCVIEW_PRIV_DM_OFFSET	0x00008000U
#define AIE2PS_SHIMNOC_UCMOD_UCVIEW_SHARED_DM_OFFSET	0x00020000U

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
#define AIE2PS_SHIMPL_HW_ERROR_STATUS_REGOFF		0x0007ff54U

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
#define AIE2PS_CORE_STATUS_REGOFF			0x000C0000U
#define AIE2PS_CORE_INTR_REGOFF				0x000C0008U
#define AIE2PS_MDM_DBG_CTRL_STATUS_REGOFF		0x000B0010U
#define AIE2PS_DMA_DM2MM_STATUS_REGOFF			0x000C0100U
#define AIE2PS_DMA_MM2DM_STATUS_REGOFF			0x000C0110U
#define AIE2PS_MOD_AXIMM_REGOFF				0x000C0020U
#define AIE2PS_MOD_AXIMM_OUTSTNDG_TRANS_REGOFF		0x000C0024U

/*
 * Register masks
 */
#define AIE2PS_SHIMPL_COLCLOCK_CTRL_MASK		GENMASK(1, 0)
#define AIE2PS_UCCORE_STS_MASK0				0x1U
#define AIE2PS_UCCORE_STS_MASK1				0x2U
#define AIE2PS_MASK_RUNNING				0x00000001U
#define AIE2PS_MASK_ERR_BD_INVLD			0x00000002U
#define AIE2PS_MASK_ERR_LOCAL_ADDR_OUT_OF_RANGE		0x00000004U
#define AIE2PS_MASK_AXI_MM_SLVERR			0x00000008U
#define AIE2PS_MASK_AXI_MM_DECERR			0x00000010U
#define AIE2PS_MASK_ERROR_ECC_DED			0x00000020U
#define AIE2PS_MASK_TASK_QUEUE_OVERFLOW			0x00000040U
#define AIE2PS_MASK_TASK_QUEUE_SIZE			0x00001F00U
#define AIE2PS_MASK_RESPONSE_QUEUE_SIZE			0x001F0000U

/* Macros to define size of a sysfs binary attribute */
#define AIE2PS_PART_SYSFS_CORE_BINA_SIZE	0x4000		/* 16KB */
#define AIE2PS_PART_SYSFS_LOCK_BINA_SIZE	0x28000		/* 160KB */
#define AIE2PS_PART_SYSFS_ERROR_BINA_SIZE	0x4000		/* 16KB */
#define AIE2PS_PART_SYSFS_DMA_BINA_SIZE		0xC800		/* 50KB */
#define AIE2PS_PART_SYSFS_STATUS_BINA_SIZE	0x3c000		/* 240KB */
#define AIE2PS_PART_SYSFS_UCSTATUS_BINA_SIZE	0x3c000		/* 240KB */

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

static const struct aie_dma_attr aie2ps_tiledma = {
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
	.bd_regoff = AIE2PS_TILE_MEMMOD_BD0_0_REGOFF,
	.num_bds = 16,
	.bd_len = 0x18U,
	.num_mm2s_chan = 2U,
	.num_s2mm_chan = 2U,
	.mm2s_sts_regoff = AIE2PS_TILE_MEMMOD_DMA_MM2S_STATUS_REGOFF,
	.s2mm_sts_regoff = AIE2PS_TILE_MEMMOD_DMA_S2MM_STATUS_REGOFF,
};

static const struct aie_dma_attr aie2ps_memtiledma = {
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
	.bd_regoff = AIE2PS_MEMORY_BD0_0_REGOFF,
	.num_bds = 48,
	.bd_len = 0x20U,
	.num_mm2s_chan = 6U,
	.num_s2mm_chan = 6U,
	.mm2s_sts_regoff = AIE2PS_MEMORY_DMA_MM2S_STATUS_REGOFF,
	.s2mm_sts_regoff = AIE2PS_MEMORY_DMA_S2MM_STATUS_REGOFF,
};

static const struct aie_dma_attr aie2ps_shimdma = {
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
	.bd_regoff = AIE2PS_SHIMNOC_BD0_0_REGOFF,
	.num_bds = 16,
	.bd_len = 0x30U,
	.num_mm2s_chan = 2U,
	.num_s2mm_chan = 2U,
	.mm2s_sts_regoff = AIE2PS_SHIMNOC_DMA_MM2S_STATUS_REGOFF,
	.s2mm_sts_regoff = AIE2PS_SHIMNOC_DMA_S2MM_STATUS_REGOFF,
};

static char *aie2ps_dma_chan_status_str[] = {
	"idle",
	"running",
};

static char *aie2ps_dma_qsts_str[] = {
	"okay",
	"overflow",
};

static const struct aie_bd_lock_attr aie2ps_tile_lockbd = {
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
};

static const struct aie_bd_pkt_attr aie2ps_tile_pktbd = {
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
};

static const struct aie_bd_aieml_dim_attr aie2ps_tile_dimbd = {
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
};

static const struct aie_bd_attr aie2ps_tilebd = {
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
	.lock = aie2ps_tile_lockbd,
	.packet = aie2ps_tile_pktbd,
	.aie2ps_dim = aie2ps_tile_dimbd,
	.num_dims = 3,
	.bd_idx_off = 0x20U,
};

static const struct aie_bd_lock_attr aie2ps_memtile_lockbd = {
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
};

static const struct aie_bd_pkt_attr aie2ps_memtile_pktbd = {
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
};

static const struct aie_bd_aieml_dim_attr aie2ps_memtile_dimbd = {
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
};

static const struct aie_bd_attr aie2ps_memtilebd = {
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
	.lock = aie2ps_memtile_lockbd,
	.packet = aie2ps_memtile_pktbd,
	.aie2ps_dim = aie2ps_memtile_dimbd,
	.num_dims = 4,
	.bd_idx_off = 0x20U,
};

static const struct aie_bd_lock_attr aie2ps_shim_lockbd = {
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
};

static const struct aie_bd_pkt_attr aie2ps_shim_pktbd = {
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
};

static const struct aie_bd_axi_attr aie2ps_shim_axibd = {
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
};

static const struct aie_bd_aieml_dim_attr aie2ps_shim_dimbd = {
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
};

static const struct aie_bd_attr aie2ps_shimbd = {
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
	.lock = aie2ps_shim_lockbd,
	.packet = aie2ps_shim_pktbd,
	.axi = aie2ps_shim_axibd,
	.aie2ps_dim = aie2ps_shim_dimbd,
	.num_dims = 3,
	.bd_idx_off = 0x20U,
};

static char *aie2ps_core_status_str[] = {
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

static const struct aie_dev_attr aie2ps_aperture_dev_attr[] = {
	AIE_APERTURE_ATTR_RO(hardware_info),
};

static const struct aie_dev_attr aie2ps_tile_dev_attr[] = {
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

static const struct aie_dev_attr aie2ps_part_dev_attr[] = {
	AIE_PART_DEV_ATTR_RO(current_freq),
	AIE_PART_DEV_ATTR_RO(error_stat),
};

static const struct aie_bin_attr aie2ps_part_bin_attr[] = {
	AIE_PART_BIN_ATTR_RO(core, AIE2PS_PART_SYSFS_CORE_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(lock, AIE2PS_PART_SYSFS_LOCK_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(dma, AIE2PS_PART_SYSFS_DMA_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(error, AIE2PS_PART_SYSFS_ERROR_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(status, AIE2PS_PART_SYSFS_STATUS_BINA_SIZE),
	AIE_PART_BIN_ATTR_RO(ucstatus, AIE2PS_PART_SYSFS_UCSTATUS_BINA_SIZE),
};

static const struct aie_single_reg_field aie2ps_uc_core_sts = {
	.mask = GENMASK(1, 0),
	.regoff = AIE2PS_CORE_STATUS_REGOFF,
};

static const struct aie_single_reg_field aie2ps_uc_core_intr = {
	.mask = GENMASK(1, 0),
	.regoff = AIE2PS_CORE_INTR_REGOFF,
};

static const struct aie_single_reg_field aie2ps_uc_mdm_dbg_sts = {
	.mask = GENMASK(19, 0),
	.regoff = AIE2PS_MDM_DBG_CTRL_STATUS_REGOFF,
};

static const struct aie_single_reg_field aie2ps_uc_dma_dm2mm_sts = {
	.mask = GENMASK(20, 0),
	.regoff = AIE2PS_DMA_DM2MM_STATUS_REGOFF,
};

static const struct aie_single_reg_field aie2ps_uc_dma_mm2dm_sts = {
	.mask = GENMASK(20, 0),
	.regoff = AIE2PS_DMA_MM2DM_STATUS_REGOFF,
};

static const struct aie_single_reg_field aie2ps_uc_mod_aximm = {
	.mask = GENMASK(31, 0),
	.regoff = AIE2PS_MOD_AXIMM_REGOFF,
};

static const struct aie_single_reg_field aie2ps_uc_mod_aximm_out_trans = {
	.mask = GENMASK(1, 0),
	.regoff = AIE2PS_MOD_AXIMM_OUTSTNDG_TRANS_REGOFF,
};

static const struct aie_uc_corectrl_attr aie2ps_shimnoc_uc_core_ctrl = {
	.wakeup = {
		.mask = BIT(0),
		.regoff = AIE2PS_SHIMNOC_UCMOD_CORE_CTRL_REGOFF,
	},
	.sleep = {
		.mask = BIT(1),
		.regoff = AIE2PS_SHIMNOC_UCMOD_CORE_CTRL_REGOFF,
	},
};

static const struct aie_sysfs_attr aie2ps_aperture_sysfs_attr = {
	.dev_attr = aie2ps_aperture_dev_attr,
	.bin_attr = NULL,
	.num_dev_attrs = ARRAY_SIZE(aie2ps_aperture_dev_attr),
	.num_bin_attrs = 0U,
};

static const struct aie_sysfs_attr aie2ps_part_sysfs_attr = {
	.dev_attr = aie2ps_part_dev_attr,
	.bin_attr = aie2ps_part_bin_attr,
	.num_dev_attrs = ARRAY_SIZE(aie2ps_part_dev_attr),
	.num_bin_attrs = ARRAY_SIZE(aie2ps_part_bin_attr),
};

static const struct aie_sysfs_attr aie2ps_tile_sysfs_attr = {
	.dev_attr = aie2ps_tile_dev_attr,
	.bin_attr = NULL,
	.num_dev_attrs = ARRAY_SIZE(aie2ps_tile_dev_attr),
	.num_bin_attrs = 0U,
};

static const struct aie_single_reg_field aie2ps_core_pc = {
	.mask = GENMASK(19, 0),
	.regoff = AIE2PS_TILE_COREMOD_CORE_PC_REGOFF,
};

static const struct aie_single_reg_field aie2ps_core_lr = {
	.mask = GENMASK(19, 0),
	.regoff = AIE2PS_TILE_COREMOD_CORE_LR_REGOFF,
};

static const struct aie_single_reg_field aie2ps_core_sp = {
	.mask = GENMASK(19, 0),
	.regoff = AIE2PS_TILE_COREMOD_CORE_SP_REGOFF,
};

static const struct aie_single_reg_field aie2ps_noc_outstanding_aximm = {
	.mask = BIT(0),
	.regoff = AIE2PS_SHIMNOC_AXI_OUTSTANDING_TX_REGOFF,
};

static const struct aie_single_reg_field aie2ps_uc_outstanding_aximm = {
	.mask = GENMASK(1, 0),
	.regoff = AIE2PS_UCMOD_AXI_OUTSTANDING_TX_REGOFF,
};

static const struct aie_single_reg_field aie2ps_hw_err_status = {
	.mask = GENMASK(2, 0),
	.regoff = AIE2PS_SHIMPL_HW_ERROR_STATUS_REGOFF,
};

static const struct aie_event_attr aie2ps_pl_event = {
	.bc_event = {
		.mask = GENMASK(6, 0),
		.regoff = 0U,
	},
	.group_error = {
		.mask = GENMASK(11, 0),
		.regoff = 0xcU,
	},
	.bc_block_a = {
		.mask = GENMASK(15, 0),
		.regoff = AIE2PS_SHIMPL_EVENT_BC_A_BLOCK_SOUTH_SET,
	},
	.bc_block_b = {
		.mask = GENMASK(15, 0),
		.regoff = AIE2PS_SHIMPL_EVENT_BC_B_BLOCK_SOUTH_SET,
	},
	.event_group_error0_enable = {
		.mask = GENMASK(9, 0),
		.regoff = AIE2PS_SHIMPL_GROUP0_REGOFF,
	},
	.event_group_error0_enable_default = 0x3FFU,
	.bc_regoff = AIE2PS_SHIMPL_EVENT_BC0_REGOFF,
	.status_regoff = AIE2PS_SHIMPL_EVENT_STATUS0_REGOFF,
	.group_regoff = AIE2PS_SHIMPL_GROUP0_REGOFF,
	.base_error_event = 114U,
	.base_error_group = 113U,
	.num_broadcasts = 16U,
	.base_bc_event = 166U,
	.user_event1 = 183U,
	.uc_error_group = 195U,
	.num_events = 256U,
};

static const struct aie_event_attr aie2ps_memtile_event = {
	.bc_event = {
		.mask = GENMASK(7, 0),
		.regoff = 0U,
	},
	.group_error = {
		.mask = GENMASK(11, 0),
		.regoff = 0x18U,
	},
	.bc_block_a = {
		.mask = GENMASK(15, 0),
		.regoff = AIE2PS_MEMORY_EVENT_BC_A_BLOCK_SOUTH_SET,
	},
	.bc_block_b = {
		.mask = GENMASK(15, 0),
		.regoff = AIE2PS_MEMORY_EVENT_BC_B_BLOCK_SOUTH_SET,
	},
	.event_group_error0_enable = {
		.mask = GENMASK(11, 0),
		.regoff = AIE2PS_MEMORY_GROUPERROR_REGOFF,
	},
	.event_group_error0_enable_default = 0x7FAU,

	.bc_regoff = AIE2PS_MEMORY_EVENT_BC0_REGOFF,
	.status_regoff = AIE2PS_MEMORY_EVENT_STATUS0_REGOFF,
	.group_regoff = AIE2PS_MEMORY_GROUP0_REGOFF,
	.base_error_event = 129U,
	.base_error_group = 128U,
	.num_broadcasts = 16U,
	.base_bc_event = 142U,
	.num_events = 192U,
};

static const struct aie_event_attr aie2ps_mem_event = {
	.bc_event = {
		.mask = GENMASK(6, 0),
		.regoff = 0U,
	},
	.group_error = {
		.mask = GENMASK(15, 0),
		.regoff = 0x14U,
	},
	.bc_block_b = {
		.mask = GENMASK(15, 0),
		.regoff = AIE2PS_TILE_MEMMOD_EVENT_BC_B_BLOCK_SOUTH_SET,
	},
	.event_group_error0_enable = {
		.mask = GENMASK(15, 0),
		.regoff = AIE2PS_TILE_MEMMOD_GROUPERROR_REGOFF,
	},
	.event_group_error0_enable_default = 0x7FFAU,
	.bc_regoff = AIE2PS_TILE_MEMMOD_EVENT_BC0_REGOFF,
	.status_regoff = AIE2PS_TILE_MEMMOD_EVENT_STATUS0_REGOFF,
	.group_regoff = AIE2PS_TILE_MEMMOD_GROUP0_REGOFF,
	.base_error_event = 87U,
	.base_error_group = 86U,
	.num_broadcasts = 16U,
	.base_bc_event = 107U,
	.num_events = 128U,
};

static const struct aie_event_attr aie2ps_core_event = {
	.bc_event = {
		.mask = GENMASK(6, 0),
		.regoff = 0U,
	},
	.group_error = {
		.mask = GENMASK(24, 0),
		.regoff = 0x10U,
	},
	.bc_block_a = {
		.mask = GENMASK(15, 0),
		.regoff = AIE2PS_TILE_COREMOD_EVENT_BC_A_BLOCK_SOUTH_SET,
	},
	.error_halt_event = {
		.mask = GENMASK(6, 0),
		.regoff = AIE2PS_TILE_COREMOD_ERROR_HALT_EVENT_REGOFF,
	},
	.error_halt_event_group = 46U,
	.event_group_error0_enable = {
		.mask = GENMASK(24, 0),
		.regoff = AIE2PS_TILE_COREMOD_GROUPERROR_REGOFF,
	},
	.event_group_error0_enable_default = 0x1CF5F80U,
	.bc_regoff = AIE2PS_TILE_COREMOD_EVENT_BC0_REGOFF,
	.status_regoff = AIE2PS_TILE_COREMOD_EVENT_STATUS0_REGOFF,
	.group_regoff = AIE2PS_TILE_COREMOD_GROUP0_REGOFF,
	.base_error_event = 48U,
	.base_error_group = 46U,
	.num_broadcasts = 16U,
	.base_bc_event = 107U,
	.num_events = 128U,
};

static const struct aie_l1_intr_ctrl_attr aie2ps_l1_intr_ctrl = {
	.mask_a = {
		.mask = GENMASK(19, 0),
		.regoff = 0,
	},
	.enable_a = {
		.mask = GENMASK(19, 0),
		.regoff = 0x4U,
	},
	.disable_a = {
		.mask = GENMASK(19, 0),
		.regoff = 0x8U,
	},
	.irq_no_a = {
		.mask = GENMASK(3, 0),
		.regoff = 0x10U,
	},
	.irq_event_a = {
		.mask = GENMASK(31, 0),
		.regoff = 0x14U,
	},
	.block_north_a_set = {
		.mask = GENMASK(15, 0),
		.regoff = 0x18U,
	},
	.block_north_a_clear = {
		.mask = GENMASK(15, 0),
		.regoff = 0x1CU,
	},
	.block_north_a_value = {
		.mask = GENMASK(15, 0),
		.regoff = 0x20U,
	},

	.mask_b = {
		.mask = GENMASK(19, 0),
		.regoff = 0x30U,
	},
	.enable_b = {
		.mask = GENMASK(19, 0),
		.regoff = 0x34U,
	},
	.disable_b = {
		.mask = GENMASK(19, 0),
		.regoff = 0x38U,
	},
	.irq_no_b = {
		.mask = GENMASK(3, 0),
		.regoff = 0x40U,
	},
	.irq_event_b = {
		.mask = GENMASK(31, 0),
		.regoff = 0x44U,
	},
	.block_north_b_set = {
		.mask = GENMASK(15, 0),
		.regoff = 0x48U,
	},
	.block_north_b_clear = {
		.mask = GENMASK(15, 0),
		.regoff = 0x4CU,
	},
	.block_north_b_value = {
		.mask = GENMASK(15, 0),
		.regoff = 0x50U,
	},

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

static const struct aie_l2_intr_ctrl_attr aie2ps_l2_intr_ctrl = {
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
	.intr = {
		.mask = GENMASK(1, 0),
		.regoff = 0x10U,
	},
	.regoff = 0x1000U,
	.num_broadcasts = 0x10U,
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

/**
 * aie2ps_get_uc_core_status() - Retrieve the status of a uc core
 * @apart: AI engine partition.
 * @loc: Location of the AI engine uc core.
 * @return: A 32-bit value representing the core's status.
 */
static u32 aie2ps_get_uc_core_status(struct aie_partition *apart,
				     struct aie_location *loc)
{
	u32 regoff, regvalue;

	regoff = aie_cal_regoff(apart->adev, *loc, aie2ps_uc_core_sts.regoff);
	regvalue = ioread32(apart->aperture->base + regoff);

	return aie_get_reg_field(&aie2ps_uc_core_sts, regvalue);
}

/**
 * aie2ps_get_uc_core_intr() - Retrieve the status of a uc core interrupt
 * @apart: AI engine partition.
 * @loc: Location of the AI engine uc core.
 * @return: A 32-bit value representing the core interrupt status.
 */
static u32 aie2ps_get_uc_core_intr(struct aie_partition *apart,
				   struct aie_location *loc)
{
	u32 regoff, regvalue;

	regoff = aie_cal_regoff(apart->adev, *loc, aie2ps_uc_core_intr.regoff);
	regvalue = ioread32(apart->aperture->base + regoff);

	return aie_get_reg_field(&aie2ps_uc_core_intr, regvalue);
}

/**
 * aie2ps_get_uc_mdm_dbg_sts() - Retrieve the status of a uc core mdm debug
 * @apart: AI engine partition.
 * @loc: Location of the AI engine uc core.
 * @return: A 32-bit value representing the core's mdm dbg status.
 */
static u32 aie2ps_get_uc_mdm_dbg_sts(struct aie_partition *apart,
				     struct aie_location *loc)
{
	u32 regoff, regvalue;

	regoff = aie_cal_regoff(apart->adev, *loc, aie2ps_uc_mdm_dbg_sts.regoff);
	regvalue = ioread32(apart->aperture->base + regoff);

	return aie_get_reg_field(&aie2ps_uc_mdm_dbg_sts, regvalue);
}

/**
 * aie2ps_get_uc_dma_dm2mm_sts() - Retrieve the status of a uc core dm2mm
 * @apart: AI engine partition.
 * @loc: Location of the AI engine uc core.
 * @return: A 32-bit value representing the core's dm2mm status.
 */
static u32 aie2ps_get_uc_dma_dm2mm_sts(struct aie_partition *apart,
				       struct aie_location *loc)
{
	u32 regoff, regvalue;

	regoff = aie_cal_regoff(apart->adev, *loc, aie2ps_uc_dma_dm2mm_sts.regoff);
	regvalue = ioread32(apart->aperture->base + regoff);

	return aie_get_reg_field(&aie2ps_uc_dma_dm2mm_sts, regvalue);
}

/**
 * aie2ps_get_uc_dma_mm2dm_sts() - Retrieve the status of a uc core mm2dm
 * @apart: AI engine partition.
 * @loc: Location of the AI engine uc core.
 * @return: A 32-bit value representing the core's mm2dm status.
 */
static u32 aie2ps_get_uc_dma_mm2dm_sts(struct aie_partition *apart,
				       struct aie_location *loc)
{
	u32 regoff, regvalue;

	regoff = aie_cal_regoff(apart->adev, *loc, aie2ps_uc_dma_mm2dm_sts.regoff);
	regvalue = ioread32(apart->aperture->base + regoff);

	return aie_get_reg_field(&aie2ps_uc_dma_mm2dm_sts, regvalue);
}

/**
 * aie2ps_get_uc_mod_aximm() - Retrieve the status of a uc core aximm
 * @apart: AI engine partition.
 * @loc: Location of the AI engine uc core.
 * @return: A 32-bit value representing the core's aximm status.
 */
static u32 aie2ps_get_uc_mod_aximm(struct aie_partition *apart,
				   struct aie_location *loc)
{
	u32 regoff, regvalue;

	regoff = aie_cal_regoff(apart->adev, *loc, aie2ps_uc_mod_aximm.regoff);
	regvalue = ioread32(apart->aperture->base + regoff);

	return aie_get_reg_field(&aie2ps_uc_mod_aximm, regvalue);
}

/**
 * aie2ps_get_uc_mod_aximm_out_trans() - Retrieve the status of a uc core aximm
 *                                       out transactions
 * @apart: AI engine partition.
 * @loc: Location of the AI engine uc core.
 * @return: A 32-bit value representing the core's aximm out transactions status.
 */
static u32 aie2ps_get_uc_mod_aximm_out_trans(struct aie_partition *apart,
					     struct aie_location *loc)
{
	u32 regoff, regvalue;

	regoff = aie_cal_regoff(apart->adev, *loc, aie2ps_uc_mod_aximm_out_trans.regoff);
	regvalue = ioread32(apart->aperture->base + regoff);

	return aie_get_reg_field(&aie2ps_uc_mod_aximm_out_trans, regvalue);
}

/**
 * aie2ps_sysfs_get_uc_core_status() - exports AI engine uc core status,
 *                                     sleep and pending interrupt status
 *                                     to a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine Tile.
 * @buffer: location to return uc core  status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie2ps_sysfs_get_uc_core_status(struct aie_partition *apart,
					struct aie_location *loc, char *buffer,
					ssize_t size)
{
	unsigned long status;
	ssize_t len = 0;
	bool is_delimit_req = false;

	status = apart->adev->ops->get_uc_core_sts(apart, loc);
	if (status & AIE2PS_UCCORE_STS_MASK0) {
		len += scnprintf(&buffer[len], max(0L, size - len), "sleep");
		is_delimit_req = true;
	}

	if (status & AIE2PS_UCCORE_STS_MASK1) {
		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len), DELIMITER_LEVEL1);
		len += scnprintf(&buffer[len], max(0L, size - len), "interrupt");
	}

	return len;
}

/**
 * aie2ps_sysfs_get_uc_core_intr() - exports AI engine uc core interrupt status,
 *                                   event action and go to sleep interrupt,
 *                                   to a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine Tile.
 * @buffer: location to return uc core  status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie2ps_sysfs_get_uc_core_intr(struct aie_partition *apart,
				      struct aie_location *loc, char *buffer,
				      ssize_t size)
{
	unsigned long status;
	ssize_t len = 0;
	bool is_delimit_req = false;

	status = apart->adev->ops->get_uc_core_intr(apart, loc);
	if (status & AIE2PS_UCCORE_STS_MASK0) {
		len += scnprintf(&buffer[len], max(0L, size - len), "go_to_sleep");
		is_delimit_req = true;
	}

	if (status & AIE2PS_UCCORE_STS_MASK1) {
		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len), DELIMITER_LEVEL1);
		len += scnprintf(&buffer[len], max(0L, size - len), "event_action");
	}

	return len;
}

/**
 * aie2ps_sysfs_get_uc_mdm_dbg_sts() - exports AI engine mdm debug lock status
 *                                     to a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine Tile.
 * @buffer: location to return uc core  status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie2ps_sysfs_get_uc_mdm_dbg_sts(struct aie_partition *apart,
					struct aie_location *loc, char *buffer,
					ssize_t size)
{
	unsigned long status;
	ssize_t len = 0;

	status = apart->adev->ops->get_uc_mdm_dbg_sts(apart, loc);
	if (status & AIE2PS_UCCORE_STS_MASK0)
		len += scnprintf(&buffer[len], max(0L, size - len), "lock_acquired\n");

	return len;
}

/**
 * aie2ps_sysfs_get_uc_dma_dm2mm_sts() - exports AI engine uc DMA channel status,
 *                                       response queue size, task queue size,
 *                                       error ecc ded, aximm decode error,
 *                                       aximm slave error, error on address out
 *                                       of range, issue loading BD, channel
 *                                       running status to a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine Tile.
 * @buffer: location to return uc core  status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie2ps_sysfs_get_uc_dma_dm2mm_sts(struct aie_partition *apart,
					  struct aie_location *loc, char *buffer,
					  ssize_t size)
{
	unsigned long status;
	unsigned int running, error_bd_invalid, err_local_addr_out_of_range;
	unsigned int aximm_slv_err, aximm_dec_err;
	unsigned int err_ecc_ded, task_queue_overflow, task_queue_size, response_queue_size;
	ssize_t len = 0;

	status = apart->adev->ops->get_uc_dma_dm2mm_sts(apart, loc);

	running = status & AIE2PS_MASK_RUNNING;
	if (running)
		len += scnprintf(&buffer[len], max(0L, size - len), "Running%s", DELIMITER_LEVEL1);
	else
		len += scnprintf(&buffer[len], max(0L, size - len), "Idle%s", DELIMITER_LEVEL1);

	error_bd_invalid = (status & AIE2PS_MASK_ERR_BD_INVLD) >> 1;
	len += scnprintf(&buffer[len], max(0L, size - len), "EBDI - %u%s",
			 error_bd_invalid, DELIMITER_LEVEL1);

	err_local_addr_out_of_range = (status & AIE2PS_MASK_ERR_LOCAL_ADDR_OUT_OF_RANGE) >> 2;
	len += scnprintf(&buffer[len], max(0L, size - len), "ELAOR - %u%s",
			 err_local_addr_out_of_range, DELIMITER_LEVEL1);

	aximm_slv_err = (status & AIE2PS_MASK_AXI_MM_SLVERR) >> 3;
	len += scnprintf(&buffer[len], max(0L, size - len), "AMS - %u%s",
			 aximm_slv_err, DELIMITER_LEVEL1);

	aximm_dec_err = (status & AIE2PS_MASK_AXI_MM_DECERR) >> 4;
	len += scnprintf(&buffer[len], max(0L, size - len), "AMD - %u%s",
			 aximm_dec_err, DELIMITER_LEVEL1);

	err_ecc_ded = (status & AIE2PS_MASK_ERROR_ECC_DED) >> 5;
	len += scnprintf(&buffer[len], max(0L, size - len), "EED - %u%s",
			 err_ecc_ded, DELIMITER_LEVEL1);

	task_queue_overflow = (status & AIE2PS_MASK_TASK_QUEUE_OVERFLOW) >> 6;
	len += scnprintf(&buffer[len], max(0L, size - len), "TQO - %u%s",
			 task_queue_overflow, DELIMITER_LEVEL1);

	task_queue_size = (status & AIE2PS_MASK_TASK_QUEUE_SIZE) >> 8;
	len += scnprintf(&buffer[len], max(0L, size - len), "TQS - %u%s",
			 task_queue_size, DELIMITER_LEVEL1);

	response_queue_size = (status & AIE2PS_MASK_RESPONSE_QUEUE_SIZE) >> 16;
	len += scnprintf(&buffer[len], max(0L, size - len), "RQS - %u", response_queue_size);

	return len;
}

/**
 * aie2ps_sysfs_get_uc_dma_mm2dm_sts() - exports AI engine uc DMA channel status,
 *                                       response queue size, task queue size,
 *                                       error ecc ded, aximm decode error,
 *                                       aximm slave error, error on address out
 *                                       of range, issue loading BD, channel
 *                                       running status to a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine Tile.
 * @buffer: location to return uc core  status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie2ps_sysfs_get_uc_dma_mm2dm_sts(struct aie_partition *apart,
					  struct aie_location *loc, char *buffer,
					  ssize_t size)
{
	unsigned long status;
	unsigned int running, error_bd_invalid, err_local_addr_out_of_range;
	unsigned int aximm_slv_err, aximm_dec_err;
	unsigned int err_ecc_ded, task_queue_overflow, task_queue_size, response_queue_size;
	ssize_t len = 0;

	status = apart->adev->ops->get_uc_dma_mm2dm_sts(apart, loc);

	running = status & AIE2PS_MASK_RUNNING;
	if (running)
		len += scnprintf(&buffer[len], max(0L, size - len), "Running%s", DELIMITER_LEVEL1);
	else
		len += scnprintf(&buffer[len], max(0L, size - len), "Idle%s", DELIMITER_LEVEL1);

	error_bd_invalid = (status & AIE2PS_MASK_ERR_BD_INVLD) >> 1;
	len += scnprintf(&buffer[len], max(0L, size - len), "EBDI - %u%s",
			 error_bd_invalid, DELIMITER_LEVEL1);

	err_local_addr_out_of_range = (status & AIE2PS_MASK_ERR_LOCAL_ADDR_OUT_OF_RANGE) >> 2;
	len += scnprintf(&buffer[len], max(0L, size - len), "ELAOR - %u%s",
			 err_local_addr_out_of_range, DELIMITER_LEVEL1);

	aximm_slv_err = (status & AIE2PS_MASK_AXI_MM_SLVERR) >> 3;
	len += scnprintf(&buffer[len], max(0L, size - len), "AMS - %u%s",
			 aximm_slv_err, DELIMITER_LEVEL1);

	aximm_dec_err = (status & AIE2PS_MASK_AXI_MM_DECERR) >> 4;
	len += scnprintf(&buffer[len], max(0L, size - len), "AMD - %u%s",
			 aximm_dec_err, DELIMITER_LEVEL1);

	err_ecc_ded = (status & AIE2PS_MASK_ERROR_ECC_DED) >> 5;
	len += scnprintf(&buffer[len], max(0L, size - len), "EED - %u%s",
			 err_ecc_ded, DELIMITER_LEVEL1);

	task_queue_overflow = (status & AIE2PS_MASK_TASK_QUEUE_OVERFLOW) >> 6;
	len += scnprintf(&buffer[len], max(0L, size - len), "TQO - %u%s",
			 task_queue_overflow, DELIMITER_LEVEL1);

	task_queue_size = (status & AIE2PS_MASK_TASK_QUEUE_SIZE) >> 8;
	len += scnprintf(&buffer[len], max(0L, size - len), "TQS - %u%s",
			 task_queue_size, DELIMITER_LEVEL1);

	response_queue_size = (status & AIE2PS_MASK_RESPONSE_QUEUE_SIZE) >> 16;
	len += scnprintf(&buffer[len], max(0L, size - len), "RQS - %u", response_queue_size);

	return len;
}

/**
 * aie2ps_sysfs_get_uc_mod_aximm() - exports AI engine uc aximm offset status
 *                                   to a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine Tile.
 * @buffer: location to return uc core  status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie2ps_sysfs_get_uc_mod_aximm(struct aie_partition *apart,
				      struct aie_location *loc, char *buffer,
				      ssize_t size)
{
	unsigned long status;
	ssize_t len = 0;

	status = apart->adev->ops->get_uc_mod_aximm(apart, loc);
	len += scnprintf(&buffer[len], max(0L, size - len), "aximm_offset - 0x%lX%s",
			 status, DELIMITER_LEVEL2);

	return len;
}

/**
 * aie2ps_sysfs_get_uc_mod_aximm_out_trans() - exports AI engine uc aximm outstanding transactions
 *                                             status of module to array and DMA
 *                                             to NMU to a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine Tile.
 * @buffer: location to return uc core  status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
ssize_t aie2ps_sysfs_get_uc_mod_aximm_out_trans(struct aie_partition *apart,
						struct aie_location *loc, char *buffer,
						ssize_t size)
{
	unsigned long status;
	unsigned int module_to_array, dma_to_nmu;
	ssize_t len = 0;

	status = apart->adev->ops->get_uc_mod_aximm_out_trans(apart, loc);

	dma_to_nmu = status & AIE2PS_UCCORE_STS_MASK0;
	len += scnprintf(&buffer[len], max(0L, size - len), "d2n - %u%s",
			 dma_to_nmu, DELIMITER_LEVEL1);

	module_to_array = status & AIE2PS_UCCORE_STS_MASK1;
	len += scnprintf(&buffer[len], max(0L, size - len), "m2a - %u%s\n",
			 module_to_array, DELIMITER_LEVEL2);

	return len;
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

/**
 * aie2ps_get_tile_dma_attr() - gets tile dma attribute for AIEML
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @attr: pointer of attribute to assign
 */
static void aie2ps_get_tile_dma_attr(struct aie_partition *apart,
				     struct aie_location *loc,
				     const struct aie_dma_attr **attr)
{
	u32 ttype;

	ttype = aie2ps_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		*attr = &aie2ps_tiledma;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		*attr = &aie2ps_memtiledma;
	else
		*attr = &aie2ps_shimdma;
}

/**
 * aie2ps_get_dma_mm2s_status() - reads the DMA memory map to stream status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @chanid: DMA channel ID.
 * @return: 32-bit register value.
 */
static u32 aie2ps_get_dma_mm2s_status(struct aie_partition *apart,
				      struct aie_location *loc, u8 chanid)
{
	const struct aie_dma_attr *attr;
	u32 stsoff, regoff;

	aie2ps_get_tile_dma_attr(apart, loc, &attr);

	stsoff = attr->mm2s_sts_regoff + chanid * attr->chansts.regoff;
	regoff = aie_cal_regoff(apart->adev, *loc, stsoff);

	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie2ps_get_chan_status() - reads the DMA channel status from DMA status value.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @return: 8-bit status value.
 */
static u8 aie2ps_get_chan_status(struct aie_partition *apart,
				 struct aie_location *loc, u32 status)
{
	const struct aie_dma_attr *attr;

	aie2ps_get_tile_dma_attr(apart, loc, &attr);

	return aie_get_reg_field(&attr->chansts, status);
}

/**
 * aie2ps_get_dma_s2mm_status() - reads the DMA stream to memory map status.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @chanid: DMA channel ID.
 * @return: 32-bit register value.
 */
static u32 aie2ps_get_dma_s2mm_status(struct aie_partition *apart,
				      struct aie_location *loc, u8 chanid)
{
	const struct aie_dma_attr *attr;
	u32 stsoff, regoff;

	aie2ps_get_tile_dma_attr(apart, loc, &attr);

	stsoff = attr->s2mm_sts_regoff + chanid * attr->chansts.regoff;
	regoff = aie_cal_regoff(apart->adev, *loc, stsoff);

	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie2ps_get_part_sysfs_dma_status() - returns the status of DMA in string
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
static ssize_t aie2ps_get_part_sysfs_dma_status(struct aie_partition *apart,
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

	ttype = aie2ps_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE) {
		num_mm2s_chan = aie2ps_tiledma.num_mm2s_chan;
		num_s2mm_chan = aie2ps_tiledma.num_s2mm_chan;
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		num_mm2s_chan = aie2ps_memtiledma.num_mm2s_chan;
		num_s2mm_chan = aie2ps_memtiledma.num_s2mm_chan;
	} else {
		num_mm2s_chan = aie2ps_shimdma.num_mm2s_chan;
		num_s2mm_chan = aie2ps_shimdma.num_s2mm_chan;
	}

	/* MM2S */
	len += scnprintf(&buffer[len], max(0L, size - len), "mm2s: ");
	for (i = 0; i < num_mm2s_chan; i++) {
		u32 status = aie2ps_get_dma_mm2s_status(apart, loc, i);
		u32 value = aie2ps_get_chan_status(apart, loc, status);

		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len),
				 aie2ps_dma_chan_status_str[value]);
		is_delimit_req = true;
	}

	/* S2MM */
	is_delimit_req = false;
	len += scnprintf(&buffer[len], max(0L, size - len), "%ss2mm: ",
			 DELIMITER_LEVEL1);
	for (i = 0; i < num_s2mm_chan; i++) {
		u32 status = aie2ps_get_dma_s2mm_status(apart, loc, i);
		u32 value = aie2ps_get_chan_status(apart, loc, status);

		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					 DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len),
				 aie2ps_dma_chan_status_str[value]);
		is_delimit_req = true;
	}

	return len;
}

/**
 * aie2ps_get_queue_size() - reads the DMA queue size from DMA status value.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @return: 8-bit value.
 */
static u8 aie2ps_get_queue_size(struct aie_partition *apart,
				struct aie_location *loc, u32 status)
{
	const struct aie_dma_attr *attr;

	aie2ps_get_tile_dma_attr(apart, loc, &attr);

	return aie_get_reg_field(&attr->qsize, status);
}

/**
 * aie2ps_get_queue_status() - reads the DMA queue status from DMA status value.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @return: 8-bit status value.
 */
static u8 aie2ps_get_queue_status(struct aie_partition *apart,
				  struct aie_location *loc, u32 status)
{
	const struct aie_dma_attr *attr;

	aie2ps_get_tile_dma_attr(apart, loc, &attr);

	return aie_get_reg_field(&attr->qsts, status);
}

/**
 * aie2ps_get_current_bd() - reads the current buffer descriptor being processed
 *			    by DMA channel from DMA status value.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @status: status value of DMA.
 * @return: 8-bit buffer descriptor value.
 */
static u8 aie2ps_get_current_bd(struct aie_partition *apart,
				struct aie_location *loc, u32 status)
{
	const struct aie_dma_attr *attr;

	aie2ps_get_tile_dma_attr(apart, loc, &attr);

	return aie_get_reg_field(&attr->curbd, status);
}

/**
 * aie2ps_get_tile_sysfs_dma_status() - exports AI engine DMA channel status,
 *				       queue size, queue status, and current
 *				       buffer descriptor ID being processed by
 *				       DMA channel to a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @buffer: location to return DMA status string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
static ssize_t aie2ps_get_tile_sysfs_dma_status(struct aie_partition *apart,
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

	ttype = aie2ps_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE) {
		num_mm2s_chan = aie2ps_tiledma.num_mm2s_chan;
		num_s2mm_chan = aie2ps_tiledma.num_s2mm_chan;
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		num_mm2s_chan = aie2ps_memtiledma.num_mm2s_chan;
		num_s2mm_chan = aie2ps_memtiledma.num_s2mm_chan;
	} else {
		num_mm2s_chan = aie2ps_shimdma.num_mm2s_chan;
		num_s2mm_chan = aie2ps_shimdma.num_s2mm_chan;
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "channel_status: ");
	len += aie2ps_get_part_sysfs_dma_status(apart, loc, &buffer[len],
					       max(0L, size - len));

	for (i = 0; i < num_mm2s_chan; i++)
		mm2s[i] = aie2ps_get_dma_mm2s_status(apart, loc, i);

	for (i = 0; i < num_s2mm_chan; i++)
		s2mm[i] = aie2ps_get_dma_s2mm_status(apart, loc, i);

	/* Queue size */
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "\nqueue_size: mm2s: ");

	for (chan = 0; chan < num_mm2s_chan; chan++) {
		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len), "%d",
				 aie2ps_get_queue_size(apart, loc, mm2s[chan]));
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
				 aie2ps_get_queue_size(apart, loc, s2mm[chan]));
		is_delimit_req = true;
	}

	/* Queue status */
	len += scnprintf(&buffer[len], max(0L, size - len),
			 "\nqueue_status: mm2s: ");

	is_delimit_req = false;
	for (chan = 0; chan < num_mm2s_chan; chan++) {
		u32 value = aie2ps_get_queue_status(apart, loc, mm2s[chan]);

		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len),
				 aie2ps_dma_qsts_str[value]);
		is_delimit_req = true;
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "%ss2mm: ",
			 DELIMITER_LEVEL1);

	is_delimit_req = false;
	for (chan = 0; chan < num_s2mm_chan; chan++) {
		u32 value = aie2ps_get_queue_status(apart, loc, s2mm[chan]);

		if (is_delimit_req)
			len += scnprintf(&buffer[len], max(0L, size - len),
					DELIMITER_LEVEL0);

		len += scnprintf(&buffer[len], max(0L, size - len),
				 aie2ps_dma_qsts_str[value]);
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
				 aie2ps_get_current_bd(apart, loc, mm2s[chan]));
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
				 aie2ps_get_current_bd(apart, loc, s2mm[chan]));
		is_delimit_req = true;
	}

	len += scnprintf(&buffer[len], max(0L, size - len), "\n");
	return len;
}

/**
 * aie2ps_get_tile_bd_attr() - gets tile bd attribute for AIEML
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA.
 * @attr: pointer of attribute to assign
 */
static void aie2ps_get_tile_bd_attr(struct aie_partition *apart,
				    struct aie_location *loc,
				    const struct aie_bd_attr **attr)
{
	u32 ttype;

	ttype = aie2ps_get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE)
		*attr = &aie2ps_tilebd;
	else if (ttype == AIE_TILE_TYPE_MEMORY)
		*attr = &aie2ps_memtilebd;
	else
		*attr = &aie2ps_shimbd;
}

/**
 * aie2ps_get_tile_sysfs_bd_metadata() - exports AI engine DMA buffer descriptor
 *					metadata for all buffer descriptors to
 *					a tile level sysfs node.
 * @apart: AI engine partition.
 * @loc: location of AI engine DMA buffer descriptors.
 * @buffer: location to return DMA buffer descriptor metadata string.
 * @size: total size of buffer available.
 * @return: length of string copied to buffer.
 */
static ssize_t aie2ps_get_tile_sysfs_bd_metadata(struct aie_partition *apart,
						 struct aie_location *loc,
						 char *buffer, ssize_t size)
{
	const struct aie_dma_attr *dma_attr;
	const struct aie_bd_attr *bd_attr;
	u32 enabled, ttype;
	ssize_t len = 0;

	aie2ps_get_tile_dma_attr(apart, loc, &dma_attr);
	aie2ps_get_tile_bd_attr(apart, loc, &bd_attr);

	ttype = aie2ps_get_tile_type(apart->adev, loc);
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
		index = bd_attr->aie2ps_dim.iter_curr.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->aie2ps_dim.iter_curr,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->aie2ps_dim.iter.step_size,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);
		value = aie_get_reg_field(&bd_attr->aie2ps_dim.iter.wrap,
					  bd_data[index]);
		len += scnprintf(&buffer[len], max(0L, size - len), "%lld%s",
				 value, DELIMITER_LEVEL0);

		for (i = 0; i < bd_attr->num_dims - 1; i++) {
			index = bd_attr->aie2ps_dim.dims[i].step_size.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->aie2ps_dim.dims[i].step_size,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			index = bd_attr->aie2ps_dim.dims[i].wrap.regoff / sizeof(u32);
			value = aie_get_reg_field(&bd_attr->aie2ps_dim.dims[i].wrap,
						  bd_data[index]);
			len += scnprintf(&buffer[len], max(0L, size - len),
					 "%lld%s", value, DELIMITER_LEVEL0);
			/* padding */
			if (ttype == AIE_TILE_TYPE_MEMORY) {
				index = bd_attr->aie2ps_dim.pads[i].before.regoff / sizeof(u32);
				value = aie_get_reg_field(&bd_attr->aie2ps_dim.pads[i].before,
							  bd_data[index]);
				len += scnprintf(&buffer[len], max(0L, size - len),
						 "%lld%s", value, DELIMITER_LEVEL0);
				index = bd_attr->aie2ps_dim.pads[i].after.regoff / sizeof(u32);
				value = aie_get_reg_field(&bd_attr->aie2ps_dim.pads[i].after,
							  bd_data[index]);
				len += scnprintf(&buffer[len], max(0L, size - len),
						 "%lld%s", value, DELIMITER_LEVEL0);
			}
		}
		index = bd_attr->aie2ps_dim.dims[i].step_size.regoff / sizeof(u32);
		value = aie_get_reg_field(&bd_attr->aie2ps_dim.dims[i].step_size,
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

static int aie2ps_init_part_clk_state(struct aie_partition *apart)
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

static int aie2ps_set_part_clocks(struct aie_partition *apart)
{
	struct aie_range *range = &apart->range;
	struct aie_location loc;
	struct aie_range prev_range = {};
	u32 prev_ops = 0;
	int ret;

	for (loc.col = 0; loc.col < range->size.col; loc.col++) {
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
			struct aie_range op_range = {
				.start.col = loc.col + range->start.col,
				.size.col = 1,
			};

			ret = aie_resource_set(&apart->tiles_inuse, startbit,
					       apart->range.size.row - 1);
			if (ret)
				return ret;
			ret = aie_resource_set(&apart->cores_clk_state, startbit,
					       apart->range.size.row - 1);
			if (ret)
				return ret;

			if (!prev_ops) {
				prev_range = op_range;
				prev_ops = AIE_PART_INIT_OPT_ENB_COLCLK_BUFF;
				continue;
			}
			if (prev_ops == AIE_PART_INIT_OPT_ENB_COLCLK_BUFF &&
			    ((prev_range.start.col + prev_range.size.col) == op_range.start.col)) {
				prev_range.size.col += op_range.size.col;
				continue;
			}
			ret = aie_part_pm_ops(apart, NULL, prev_ops, prev_range, 0);
			if (ret)
				return ret;
			prev_ops = AIE_PART_INIT_OPT_ENB_COLCLK_BUFF;
			prev_range = op_range;
		} else {
			struct aie_range op_range = {
				.start.col = loc.col + range->start.col,
				.size.col = 1,
			};

			ret = aie_resource_clear(&apart->tiles_inuse, startbit,
						 apart->range.size.row - 1);
			if (ret)
				return ret;
			ret = aie_resource_clear(&apart->cores_clk_state, startbit,
						 apart->range.size.row - 1);
			if (ret)
				return ret;
			if (!prev_ops) {
				prev_range = op_range;
				prev_ops = AIE_PART_INIT_OPT_DIS_COLCLK_BUFF;
				continue;
			}
			if (prev_ops == AIE_PART_INIT_OPT_DIS_COLCLK_BUFF &&
			    ((prev_range.start.col + prev_range.size.col) == op_range.start.col)) {
				prev_range.size.col += op_range.size.col;
				continue;
			}
			ret = aie_part_pm_ops(apart, NULL, prev_ops, prev_range, 0);
			if (ret)
				return ret;
			prev_ops = AIE_PART_INIT_OPT_DIS_COLCLK_BUFF;
			prev_range = op_range;
		}
	}
	if (!prev_ops)
		ret = aie_part_pm_ops(apart, NULL, prev_ops, prev_range, 0);
	if (ret)
		return ret;
	return aie_part_pm_ops_flush(apart);
}

static int aie2ps_scan_part_clocks(struct aie_partition *apart)
{
	struct aie_device *adev = apart->adev;
	const struct aie_aperture *aperture = apart->aperture;
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

		nbitpos = (loc.col - range->start.col) * (range->size.row - 1) + loc.row;

		va = aperture->base +
		     aie_cal_regoff(adev, loc,
				    AIE2PS_SHIMPL_COLCLOCK_CTRL_REGOFF);
		val = ioread32(va);

		if (!(val & AIE2PS_SHIMPL_COLCLOCK_CTRL_MASK))
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

/**
 * aie2ps_set_tile_isolation() - Set isolation boundary of AI engile tile
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
static int aie2ps_set_tile_isolation(struct aie_partition *apart,
				     struct aie_location *loc, u8 dir)
{
	struct aie_device *adev = apart->adev;
	const struct aie_aperture *aperture = apart->aperture;
	void __iomem *va;
	u32 ttype, val;

	/* For AIEML device, dir input will match register mask */
	val = (u32)dir;
	ttype = aie2ps_get_tile_type(adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE) {
		va = aperture->base +
		     aie_cal_regoff(adev, *loc,
				    AIE2PS_TILE_COREMOD_TILECTRL_REGOFF);
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		va = aperture->base +
		     aie_cal_regoff(adev, *loc, AIE2PS_MEMORY_TILECTRL_REGOFF);
	} else {
		va = aperture->base +
		     aie_cal_regoff(adev, *loc, AIE2PS_SHIMPL_TILECTRL_REGOFF);
	}
	iowrite32(val, va);
	return 0;
}

static int aie2ps_part_clear_mems(struct aie_partition *apart)
{
	u32 opts = AIE_PART_INIT_OPT_ZEROIZEMEM | AIE_PART_INIT_OPT_UC_ZEROIZATION;
	u16 data = 0x6;
	int ret;

	ret = aie_part_pm_ops(apart, &data, opts, apart->range, 1);
	if (ret)
		dev_err(&apart->dev, "failed to clear memory for partition\n");

	return ret;
}

/**
 * aie2ps_wake_tile_uc_core_up() - wake uc core up
 * @apart: AI engine partition
 * @loc: Relative location of the tile
 * @return: return 0 if success negative value for failure.
 */
static int aie2ps_wake_tile_uc_core_up(struct aie_partition *apart,
				       struct aie_location *loc)
{
	struct aie_aperture *aperture = apart->aperture;
	struct aie_device *adev = apart->adev;
	struct aie_location loc_adjust = {0};
	void __iomem *va;
	u32 ttype, val;

	ttype = aie2ps_get_tile_type(adev, loc);
	if (ttype != AIE_TILE_TYPE_SHIMNOC) {
		dev_err(&apart->dev, "invalid tile type.\n");
		return -EINVAL;
	}

	if (aie_validate_location(apart, *loc)) {
		dev_err(&apart->dev,
			"Invalid (%d,%d) out of part(%d,%d)\n",
			loc->col, loc->row,
			apart->range.size.col, apart->range.size.row);
		return -EINVAL;
	}

	loc_adjust.col = loc->col + apart->range.start.col;

	va = aperture->base +
	     aie_aperture_cal_regoff(aperture, loc_adjust, AIE2PS_SHIMNOC_UCMOD_CORE_CTRL_REGOFF);
	val = aie_get_field_val(&adev->shimnoc_uc_corectrl->wakeup, 0x1);
	iowrite32(val, va);
	return 0;
}

/**
 * aie2ps_map_uc_mem() - maps uc core view address to host view
 * @apart: AI engine partition
 * @addr: address to be mapped
 * @pmem: partition memory
 * @return: return 0 if success negative value for failure.
 *
 */
static int aie2ps_map_uc_mem(struct aie_partition *apart, u64 addr,
			     struct aie_part_mem *pmem)
{
	struct aie_part_mem *pmems = apart->pmems;
	u32 i, mem_type;

	for (i = UC_PROG_MEM; i < NUM_TYPES_OF_MEM; i++) {
		mem_type = i % (UC_PROG_MEM_NUM + UC_DATA_MEM_NUM);
		if (pmem)
			pmem->mem = pmems[i].mem;

		if (mem_type == AIE_UC_PROGRAM_MEM && addr >=
		    AIE2PS_SHIMNOC_UCMOD_UCVIEW_PM_OFFSET && addr <
		    (AIE2PS_SHIMNOC_UCMOD_UCVIEW_PM_OFFSET +
		     pmems[i].mem.size)) {
			return AIE_UC_PROGRAM_MEM;
		} else if (mem_type == AIE_UC_PRIVATE_DATA_MEM && addr >=
			   AIE2PS_SHIMNOC_UCMOD_UCVIEW_PRIV_DM_OFFSET &&
			   addr < (AIE2PS_SHIMNOC_UCMOD_UCVIEW_PRIV_DM_OFFSET +
			   pmems[i].mem.size)) {
			return AIE_UC_PRIVATE_DATA_MEM;
		} else if (mem_type == AIE_UC_SHARED_DATA_MEM && addr >=
			   AIE2PS_SHIMNOC_UCMOD_UCVIEW_SHARED_DM_OFFSET &&
			   addr < (AIE2PS_SHIMNOC_UCMOD_UCVIEW_SHARED_DM_OFFSET
			   + pmems[i].mem.size)) {
			return AIE_UC_SHARED_DATA_MEM;
		}
	}

	pmem = NULL;
	return AIE_UC_MEM_MAX;
}

static const struct aie_tile_operations aie2ps_ops = {
	.get_tile_type = aie2ps_get_tile_type,
	.get_mem_info = aie2ps_get_mem_info,
	.get_core_status = aie2ps_get_core_status,
	.get_part_sysfs_lock_status = aie2ps_get_part_sysfs_lock_status,
	.get_tile_sysfs_lock_status = aie2ps_get_tile_sysfs_lock_status,
	.get_part_sysfs_dma_status = aie2ps_get_part_sysfs_dma_status,
	.get_tile_sysfs_dma_status = aie2ps_get_tile_sysfs_dma_status,
	.get_tile_sysfs_bd_metadata = aie2ps_get_tile_sysfs_bd_metadata,
	.init_part_clk_state = aie2ps_init_part_clk_state,
	.scan_part_clocks = aie2ps_scan_part_clocks,
	.set_part_clocks = aie2ps_set_part_clocks,
	.set_column_clock = aie2ps_part_set_column_clock_from_user,
	.set_tile_isolation = aie2ps_set_tile_isolation,
	.mem_clear = aie2ps_part_clear_mems,
	.get_dma_s2mm_status = aie2ps_get_dma_s2mm_status,
	.get_dma_mm2s_status = aie2ps_get_dma_mm2s_status,
	.get_chan_status = aie2ps_get_chan_status,
	.get_lock_status = aie2ps_get_lock_status,
	.wake_tile_uc_core_up = aie2ps_wake_tile_uc_core_up,
	.get_uc_core_sts = aie2ps_get_uc_core_status,
	.get_uc_core_intr = aie2ps_get_uc_core_intr,
	.get_uc_mdm_dbg_sts = aie2ps_get_uc_mdm_dbg_sts,
	.get_uc_dma_dm2mm_sts = aie2ps_get_uc_dma_dm2mm_sts,
	.get_uc_dma_mm2dm_sts = aie2ps_get_uc_dma_mm2dm_sts,
	.get_uc_mod_aximm = aie2ps_get_uc_mod_aximm,
	.get_uc_mod_aximm_out_trans = aie2ps_get_uc_mod_aximm_out_trans,
	.map_uc_mem = aie2ps_map_uc_mem,
	.part_init = aie2ps_part_initialize,
	.part_teardown = aie2ps_part_teardown,
	.part_clear_context = aie2ps_part_clear_context,
	.part_clean = aie2ps_part_clean,
	.part_reset = aie2ps_part_reset,
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
	adev->array_shift = AIE2PS_ARRAY_SHIFT;
	adev->col_shift = AIE2PS_COL_SHIFT;
	adev->row_shift = AIE2PS_ROW_SHIFT;
	adev->ops = &aie2ps_ops;
	adev->num_kernel_regs = ARRAY_SIZE(aie2ps_kernel_regs);
	adev->kernel_regs = aie2ps_kernel_regs;
	adev->core_regs_clr = aie2ps_core_regs_clr;
	adev->num_core_regs_clr = ARRAY_SIZE(aie2ps_core_regs_clr);
	adev->core_errors = &aie2ps_core_error;
	adev->mem_errors = &aie2ps_mem_error;
	adev->memtile_errors = &aie2ps_memtile_error;
	adev->noc_outstanding_aximm = &aie2ps_noc_outstanding_aximm;
	adev->uc_outstanding_aximm = &aie2ps_uc_outstanding_aximm;
	adev->shim_errors = &aie2ps_shim_error;
	adev->tile_dma = &aie2ps_tiledma;
	adev->shim_dma = &aie2ps_shimdma;
	adev->memtile_dma = &aie2ps_memtiledma;
	adev->shimnoc_uc_corectrl = &aie2ps_shimnoc_uc_core_ctrl;
	adev->aperture_sysfs_attr = &aie2ps_aperture_sysfs_attr;
	adev->part_sysfs_attr = &aie2ps_part_sysfs_attr;
	adev->tile_sysfs_attr = &aie2ps_tile_sysfs_attr;
	adev->core_status_str = aie2ps_core_status_str;
	adev->core_pc = &aie2ps_core_pc;
	adev->core_lr = &aie2ps_core_lr;
	adev->core_sp = &aie2ps_core_sp;
	adev->pl_events = &aie2ps_pl_event;
	adev->memtile_events = &aie2ps_memtile_event;
	adev->mem_events = &aie2ps_mem_event;
	adev->mem_lock = &aie2ps_mem_lock;
	adev->pl_lock = &aie2ps_pl_lock;
	adev->memtile_lock = &aie2ps_memtile_lock;
	adev->core_events = &aie2ps_core_event;
	adev->l1_ctrl = &aie2ps_l1_intr_ctrl;
	adev->l2_ctrl = &aie2ps_l2_intr_ctrl;
	adev->hw_err_status = &aie2ps_hw_err_status;
	aie2ps_device_init_rscs_attr(adev);

	return 0;
}
