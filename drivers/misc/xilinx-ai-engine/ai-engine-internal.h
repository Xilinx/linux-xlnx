/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Xilinx AI Engine driver internal header
 *
 * Copyright (C) 2020 - 2021 Xilinx, Inc.
 * Copyright (C) 2024 - 2025 Advanced Micro Devices, Inc.
 */

#ifndef AIE_INTERNAL_H
#define AIE_INTERNAL_H

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bits.h>
#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/file.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <uapi/linux/xlnx-ai-engine.h>

#define AIE_DEVICE_GEN_AIE	1U
#define AIE_DEVICE_GEN_AIEML	2U
#define AIE_DEVICE_GEN_AIE2PS	5U

/*
 * Macros for AI engine tile type bitmasks
 */
enum aie_tile_type {
	AIE_TILE_TYPE_TILE,
	AIE_TILE_TYPE_SHIMPL,
	AIE_TILE_TYPE_SHIMNOC,
	AIE_TILE_TYPE_MEMORY,
	AIE_TILE_TYPE_MAX
};

#define AIE_TILE_TYPE_MASK_TILE		BIT(AIE_TILE_TYPE_TILE)
#define AIE_TILE_TYPE_MASK_SHIMPL	BIT(AIE_TILE_TYPE_SHIMPL)
/* SHIM NOC tile includes SHIM PL and SHIM NOC modules */
#define AIE_TILE_TYPE_MASK_SHIMNOC	BIT(AIE_TILE_TYPE_SHIMNOC)
#define AIE_TILE_TYPE_MASK_MEMORY	BIT(AIE_TILE_TYPE_MEMORY)

#define AIE_ISOLATE_EAST_MASK		BIT(3)
#define AIE_ISOLATE_NORTH_MASK		BIT(2)
#define AIE_ISOLATE_WEST_MASK		BIT(1)
#define AIE_ISOLATE_SOUTH_MASK		BIT(0)
#define AIE_ISOLATE_ALL_MASK		GENMASK(3, 0)

/*
 * Macros for attribute property of AI engine registers accessed by kernel
 * 0 - 7 bits: tile type bits
 * 8 - 15 bits: permission bits. If it is 1, it allows write from userspace
 */
#define AIE_REGS_ATTR_TILE_TYPE_SHIFT	0U
#define AIE_REGS_ATTR_PERM_SHIFT	8U
#define AIE_REGS_ATTR_TILE_TYPE_MASK	GENMASK(AIE_REGS_ATTR_PERM_SHIFT - 1, \
						AIE_REGS_ATTR_TILE_TYPE_SHIFT)
#define AIE_REGS_ATTR_PERM_MASK		GENMASK(15, \
						AIE_REGS_ATTR_PERM_SHIFT)

#define KBYTES(n)	((n) * 1024)

#define AIE_NPI_ERROR_ID		BIT(1)

/* Macros relevant to interrupts */
#define AIE_INTR_L2_CTRL_MASK_WIDTH	32U

/* Max number of modules per tile */
#define AIE_MAX_MODS_PER_TILE		2U

/* AIE core registers step size */
#define AIE_CORE_REGS_STEP		0x10

/* Number of event status registers */
#define AIE_NUM_EVENT_STS_CORETILE	4U
#define AIE_NUM_EVENT_STS_MEMTILE	6U
#define AIE_NUM_EVENT_STS_SHIMTILE	8U

/* Number of DMA channels */
#define AIE_MAX_MM2S_CH		6U
#define AIE_MAX_S2MM_CH		6U

/* Max size of DMA buffer descriptors */
#define AIE_MAX_BD_SIZE		8U

/* Program memory offset and size index */
#define AIE_PM_MEM_OFFSET_IDX	1U

/*
 * Macros of AI engine module type index of a tile type
 * e.g.
 * id 0 of CORE tile is memory module, and 1 is core module
 * id 0 of MEM tile is memory module
 * id 0 of SHIM tile is pl module, and 1 is noc module
 */
#define AIE_TILE_MOD_START		AIE_MEM_MOD
#define AIE_MOD_ID(T, M)		((M) - AIE_##T ## _MOD_START)
#define AIE_TILE_MEM_MOD_ID		AIE_MOD_ID(TILE, AIE_MEM_MOD)
#define AIE_TILE_CORE_MOD_ID		AIE_MOD_ID(TILE, AIE_CORE_MOD)
#define AIE_MEMORY_MOD_START		AIE_MEM_MOD
#define AIE_MEMORY_MEM_MOD_ID		AIE_MOD_ID(MEMORY, AIE_MEM_MOD)
#define AIE_SHIMPL_MOD_START		AIE_PL_MOD
#define AIE_SHIMNOC_MOD_START		AIE_PL_MOD
#define AIE_SHIM_PL_MOD_ID		AIE_MOD_ID(SHIMPL, AIE_PL_MOD)
#define AIE_SHIM_NOC_MOD_ID		AIE_MOD_ID(SHIMNOC, AIE_NOC_MOD)

/* String delimiter to format sysfs data */
#define DELIMITER_LEVEL0 "|"
#define DELIMITER_LEVEL1 ", "
#define DELIMITER_LEVEL2 "; "

/* Helper macros to dynamically create sysfs device attribute */
#define AIE_APERTURE_ATTR_RO(_name) {				\
	.name		= __stringify(_name),			\
	.mode		= 0444,					\
	.show		= aie_aperture_show_##_name,		\
}

#define AIE_PART_DEV_ATTR_RO(_name) {				\
	.name		= __stringify(_name),			\
	.mode		= 0444,					\
	.show		= aie_part_show_##_name,		\
}

#define AIE_PART_DEV_ATTR_WO(_name) {				\
	.name		= __stringify(_name),			\
	.mode		= 0200,					\
	.store		= aie_part_store_##_name,		\
}

#define AIE_PART_DEV_ATTR_RW(_name) {				\
	.name		= __stringify(_name),			\
	.mode		= 0644,					\
	.show		= aie_part_show_##_name,		\
	.store		= aie_part_store_##_name,		\
}

#define AIE_TILE_DEV_ATTR_RO(_name, _ttype) {			\
	.name		= __stringify(_name),			\
	.mode		= 0444,					\
	.tile_type	= _ttype,				\
	.show		= aie_tile_show_##_name,		\
}

#define AIE_TILE_DEV_ATTR_WO(_name, _ttype) {			\
	.name		= __stringify(_name),			\
	.mode		= 0200,					\
	.tile_type	= _ttype,				\
	.store		= aie_tile_store_##_name,		\
}

#define AIE_TILE_DEV_ATTR_RW(_name, _ttype) {			\
	.name		= __stringify(_name),			\
	.mode		= 0644,					\
	.tile_type	= _ttype,				\
	.show		= aie_tile_show_##_name,		\
	.store		= aie_tile_store_##_name,		\
}

#define AIE_PART_BIN_ATTR_RO(_name, _size) {			\
	.name		= __stringify(_name),			\
	.mode		= 0444,					\
	.size		= _size,				\
	.read		= aie_sysfs_read_handler,		\
	.read_callback	= aie_part_read_cb_##_name,		\
}

#define AIE_PART_BIN_ATTR_WO(_name, _size) {			\
	.name		= __stringify(_name),			\
	.mode		= 0200,					\
	.size		= _size,				\
	.write		= aie_part_write_handler,		\
	.write_callback	= aie_part_write_cb_##_name,		\
}

#define AIE_PART_BIN_ATTR_RW(_name, _size) {			\
	.name		= __stringify(_name),			\
	.mode		= 0644					\
	.size		= _size,				\
	.read		= aie_sysfs_read_handler,		\
	.write		= aie_part_write_handler,		\
	.read_callback	= aie_part_read_cb_##_name,		\
	.write_callback	= aie_part_write_cb_##_name,		\
}

#define AIE_TILE_BIN_ATTR_RO(_name, _size, _ttype) {		\
	.name		= __stringify(_name),			\
	.mode		= 0444,					\
	.size		= _size,				\
	.tile_type	= _ttype,				\
	.read		= aie_sysfs_read_handler,		\
	.read_callback	= aie_tile_read_cb_##_name,		\
}

#define AIE_TILE_BIN_ATTR_WO(_name, _size, _ttype) {		\
	.name		= __stringify(_name),			\
	.mode		= 0200,					\
	.size		= _size,				\
	.tile_type	= _ttype,				\
	.write		= aie_tile_write_handler,		\
	.write_callback	= aie_tile_write_cb_##_name,		\
}

#define AIE_TILE_BIN_ATTR_RW(_name, _size, _ttype) {		\
	.name		= __stringify(_name),			\
	.mode		= 0644,					\
	.size		= _size,				\
	.tile_type	= _ttype,				\
	.read		= aie_sysfs_read_handler,		\
	.write		= aie_tile_write_handler,		\
	.read_callback	= aie_tile_read_cb_##_name,		\
	.write_callback	= aie_tile_write_cb_##_name,		\
}

#define AIE_NPI_NUM_IRQS		3U
#define AIE_USER_EVENT1_NUM_IRQ		(AIE_NPI_NUM_IRQS - 1)

/*
 * enum aie_uc_mem_type - identifies the type of UC memory
 */
enum aie_uc_mem_type {
	AIE_UC_PROGRAM_MEM,
	AIE_UC_PRIVATE_DATA_MEM,
	AIE_UC_SHARED_DATA_MEM,
	AIE_UC_MEM_MAX
};

/*
 * enum aie_shim_switch_type - identifies different switches in shim tile.
 */
enum aie_shim_switch_type {
	AIE_SHIM_SWITCH_A,
	AIE_SHIM_SWITCH_B
};

#define AIE_EVENT_BROADCAST_SOUTH	BIT(0)
#define AIE_EVENT_BROADCAST_WEST	BIT(1)
#define AIE_EVENT_BROADCAST_NORTH	BIT(2)
#define AIE_EVENT_BROADCAST_EAST	BIT(3)
#define AIE_EVENT_BROADCAST_ALL		(AIE_EVENT_BROADCAST_SOUTH	| \
					 AIE_EVENT_BROADCAST_WEST	| \
					 AIE_EVENT_BROADCAST_NORTH	| \
					 AIE_EVENT_BROADCAST_EAST)

/*
 * enum for SSIT devices
 */
enum aie_device_type {
	AIE_DEV_GENERIC_DEVICE,
	AIE_DEV_GEN_S100 = 100,
	AIE_DEV_GEN_S200 = 200
};

/**
 * struct aie_tile_regs - contiguous range of AI engine register
 *			  within an AI engine tile
 * @soff: start offset of the range
 * @eoff: end offset of the range
 * @width: length of each register in bytes
 * @step: offset between registers in bytes
 *        When step == width, no gaps/holes between registers.
 * @attribute: registers attribute. It uses AIE_REGS_ATTR_* macros defined
 *	       above.
 */
struct aie_tile_regs {
	size_t soff;
	size_t eoff;
	u16 width;
	u16 step;
	u32 attribute;
};

/**
 * struct aie_single_reg_field - AI engine single field register attribute
 * @mask: field mask
 * @regoff: register offset of the field
 */
struct aie_single_reg_field {
	u32 mask;
	u32 regoff;
};

struct aie_device;
struct aie_partition;

/**
 * struct aie_part_mem - AI engine partition memory information structure
 * @apart: AI engine partition
 * @dbuf: dmabuf pointer associated with the memory
 * @mem: memory information of a type of memory
 * @size: size of the total memories in the partition
 *
 * This structure is to keep the information of a type of memory in a
 * partition. The memory information will be stored in @mem property.
 * The following information will be keep:
 *  * memory start address offset within a tile
 *  * memory size
 *  * what tiles contain this type of memory
 */
struct aie_part_mem {
	struct aie_partition *apart;
	struct dma_buf *dbuf;
	struct aie_mem mem;
	size_t size;
};

/**
 * struct aie_dma_mem - AI engine dma memory information structure
 * @pmem: memory info allocated for dma transactions
 * @dma_addr: dma address
 * @node: list node
 *
 * This structure holds the virtual memory and dma address returned by
 * dma_alloc_coherent.
 */
struct aie_dma_mem {
	struct aie_part_mem pmem;
	dma_addr_t dma_addr;
	struct list_head node;
};

/**
 * struct aie_uc_corectrl_attr - AI engine uc module core control attribute
 * @wakeup: wakeup field attribute.
 * @sleep: sleep field attribute.
 */
struct aie_uc_corectrl_attr {
	struct aie_single_reg_field wakeup;
	struct aie_single_reg_field sleep;
};

/**
 * struct aie_bd_addr_attr - AI engine buffer descriptor address attributes
 * @addr: address field attributes
 * @length: length field attributes
 */
struct aie_bd_addr_attr {
	struct aie_single_reg_field addr;
	struct aie_single_reg_field length;
};

/**
 * struct aie_bd_lock_attr - AI engine buffer descriptor lock attributes
 * @lock_acq_id: lock acquire id field attributes
 * @lock_acq_val: lock acquire value field attributes
 * @lock_acq_en: lock acquire enable field attributes
 * @lock_acq_val_en: lock acquire value enable field attributes
 * @lock_rel_id: lock release id field attributes
 * @lock_rel_val: lock release value field attributes
 * @lock_rel_en: lock release enable field attributes
 * @lock_rel_val_en: lock release value enable field attributes
 */
struct aie_bd_lock_attr {
	struct aie_single_reg_field lock_acq_id;
	struct aie_single_reg_field lock_acq_val;
	struct aie_single_reg_field lock_acq_en;
	struct aie_single_reg_field lock_acq_val_en;
	struct aie_single_reg_field lock_rel_id;
	struct aie_single_reg_field lock_rel_val;
	struct aie_single_reg_field lock_rel_en;
	struct aie_single_reg_field lock_rel_val_en;
};

/**
 * struct aie_bd_pkt_attr - AI engine buffer descriptor packet attributes
 * @pkt_en: packet enable field attributes
 * @pkt_type: packet type field attributes
 * @pkt_id: packet id field attributes
 */
struct aie_bd_pkt_attr {
	struct aie_single_reg_field pkt_en;
	struct aie_single_reg_field pkt_type;
	struct aie_single_reg_field pkt_id;
};

/**
 * struct aie_bd_axi_attr - AI engine buffer descriptor AXI attributes
 * @smid: smid field attributes
 * @cache: AxCache field attributes
 * @qos: Axi QoS field attributes
 * @secure_en: Axi Secure access field attributes
 * @burst_len: Axi bursth length field attributes
 */
struct aie_bd_axi_attr {
	struct aie_single_reg_field smid;
	struct aie_single_reg_field cache;
	struct aie_single_reg_field qos;
	struct aie_single_reg_field secure_en;
	struct aie_single_reg_field burst_len;
};

/**
 * struct aie_bd_aie_dim_attr - AI engine buffer descriptor dimension
 *				attributes for aie
 * @x_incr: x increment field attributes
 * @x_wrap: x wrap field attributes
 * @x_off: x offset field attributes
 * @y_incr: y increment field attributes
 * @y_wrap: y wrap field attributes
 * @y_off: y offset field attributes
 */
struct aie_bd_aie_dim_attr {
	struct aie_single_reg_field x_incr;
	struct aie_single_reg_field x_wrap;
	struct aie_single_reg_field x_off;
	struct aie_single_reg_field y_incr;
	struct aie_single_reg_field y_wrap;
	struct aie_single_reg_field y_off;
};

/**
 * struct aie_bd_multi_dim_attr - AI engine buffer descriptor dimension
 *				  attributes
 * @wrap: wrap field attributes
 * @step_size: step size field attributes
 */
struct aie_bd_multi_dim_attr {
	struct aie_single_reg_field wrap;
	struct aie_single_reg_field step_size;
};

/**
 * struct aie_bd_pad_attr - AI engine buffer descriptor padding attributes
 * @before: before padding attributes
 * @after: after padding attributes
 */
struct aie_bd_pad_attr {
	struct aie_single_reg_field before;
	struct aie_single_reg_field after;
};

/**
 * struct aie_bd_aieml_dim_attr - AI engine buffer descriptor dimension
 *				  attributes for aieml
 * @iter_curr: iteration current field attributes
 * @iter: iteration field attributes
 * @dims: dimension field attributes, supports up to 4 dimensions
 * @pads: padding field attributes
 */
struct aie_bd_aieml_dim_attr {
	struct aie_single_reg_field iter_curr;
	struct aie_bd_multi_dim_attr iter;
	struct aie_bd_multi_dim_attr dims[4U];
	struct aie_bd_pad_attr pads[3U];
};

/**
 * struct aie_bd_attr - AI engine DMA attributes structure
 * @valid_bd: buffer descriptor valid bd field attributes
 * @next_bd: buffer descriptor next bd field attributes
 * @use_next: buffer descriptor use next bd field attributes
 * @addr: buffer descriptor address attributes
 * @addr_2: buffer descriptor address attributes of second address
 * @lock: buffer descriptor lock attributes
 * @lock_2: buffer descriptor lock attributes of second lock
 * @packet: buffer descriptor packet attributes
 * @axi: buffer descriptor AXI attributes
 * @aie_dim: buffer descriptor dimension attributes for aie dma
 * @aieml_dim: buffer descriptor dimension attributes for aieml dma
 * @aie2ps_dim: buffer descriptor dimension attributes for aie2ps dma
 * @buf_sel: buffer descriptor buffer selection field attributes
 * @curr_ptr: buffer descriptor current pointer field attributes
 * @interleave_en: buffer descriptor interleave enable field attributes
 * @interleave_cnt: buffer descriptor interleave count field attributes
 * @double_buff_en: buffer descriptor double buffer enable field attributes
 * @fifo_mode: buffer descriptor fifo mode field attributes
 * @compression_en: buffer descriptor compression enable field attributes
 * @out_of_order_id: buffer descriptor out of order bd id field attributes
 * @tlast_suppress: buffer descriptor tlast suppress field attributes
 * @num_dims: number of dimensions for tile buffer descriptor
 * @bd_idx_off: buffer descriptor index offset in bytes
 */
struct aie_bd_attr {
	struct aie_single_reg_field valid_bd;
	struct aie_single_reg_field next_bd;
	struct aie_single_reg_field use_next;
	struct aie_bd_addr_attr addr;
	struct aie_bd_addr_attr addr_2;
	struct aie_bd_lock_attr lock;
	struct aie_bd_lock_attr lock_2;
	struct aie_bd_pkt_attr packet;
	struct aie_bd_axi_attr axi;
	union {
		struct aie_bd_aie_dim_attr aie_dim;
		struct aie_bd_aieml_dim_attr aieml_dim;
		struct aie_bd_aieml_dim_attr aie2ps_dim;
	};
	struct aie_single_reg_field buf_sel;
	struct aie_single_reg_field curr_ptr;
	struct aie_single_reg_field interleave_en;
	struct aie_single_reg_field interleave_cnt;
	struct aie_single_reg_field double_buff_en;
	struct aie_single_reg_field fifo_mode;
	struct aie_single_reg_field compression_en;
	struct aie_single_reg_field out_of_order_id;
	struct aie_single_reg_field tlast_suppress;
	u32 num_dims;
	u32 bd_idx_off;
};

/**
 * struct aie_dma_attr - AI engine DMA attributes structure
 * @laddr: low address field attributes
 * @haddr: high address field attributes
 * @buflen: buffer length field attributes
 * @sts: FSM status field attributes
 * @chansts: channel status field attributes
 * @stall: queue stall status field attributes
 * @qsize: queue size field attributes
 * @curbd: current buffer descriptor field attributes
 * @qsts: queue status field attributes
 * @fifo_cnt: FIFO counter field attributes
 * @bd_regoff: SHIM DMA buffer descriptors register offset
 * @mm2s_sts_regoff: MM2S status register offset
 * @s2mm_sts_regoff: S2MM status register offset
 * @fifo_cnt_regoff: FIFO counter register offset
 * @num_mm2s_chan: number of MM2S channels
 * @num_s2mm_chan: number of S2MM channels
 * @num_bds: number of buffer descriptors
 * @bd_len: length of a buffer descriptor in bytes
 */
struct aie_dma_attr {
	struct aie_single_reg_field laddr;
	struct aie_single_reg_field haddr;
	struct aie_single_reg_field buflen;
	struct aie_single_reg_field sts;
	struct aie_single_reg_field chansts;
	struct aie_single_reg_field stall;
	struct aie_single_reg_field qsize;
	struct aie_single_reg_field curbd;
	struct aie_single_reg_field qsts;
	struct aie_single_reg_field fifo_cnt;
	u32 bd_regoff;
	u32 mm2s_sts_regoff;
	u32 s2mm_sts_regoff;
	u32 fifo_cnt_regoff;
	u32 num_mm2s_chan;
	u32 num_s2mm_chan;
	u32 num_bds;
	u32 bd_len;
};

struct aie_aperture;
/**
 * struct aie_tile_operations - AI engine device operations
 * @get_tile_type: get type of tile based on tile operation
 * @get_mem_info: get different types of memories information
 * @get_core_status: get the status of AIE core.
 * @get_part_sysfs_lock_status: get partition lock status for sysfs.
 * @get_tile_sysfs_lock_status: get tile lock status for sysfs.
 * @get_part_sysfs_dma_status: get partition dma status for sysfs.
 * @get_tile_sysfs_dma_status: get tile dma status for sysfs.
 * @get_tile_sysfs_bd_metadata: get tile bd metadata for sysfs.
 * @init_part_clk_state: initialize clock states software structure which is a
 *			 bitmap for the AI engine partition. The clock states
 *			 structure is the structure used to keep track of if
 *			 the modules in the AI engine partition are gated.
 * @scan_part_clocks: scan partition modules to check whether the modules are
 *		      clock gated or not, and update the soft clock states
 *		      structure. It is required to be called when the partition
 *		      is requested so that the driver knows which modules are
 *		      clock gated when the partition is requested. This function
 *		      expects the caller to apply partition lock before calling
 *		      this function.
 * @set_part_clocks: set partition modules clocks gate registers based on the
 *		     partition clock states bitmap. This function expects the
 *		     caller to apply partition lock before calling this
 *		     function. The caller function will need to set the bitmap
 *		     on which tiles are required to be clocked on.
 * @set_column_clock: Enable or disable column clock.
 * @set_tile_isolation: set tile isolation boundary for input direction.
 * @mem_clear: clear data memory banks of the partition.
 * @get_dma_s2mm_status: get dma s2mm status
 * @get_dma_mm2s_status: get dma mm2s status
 * @get_chan_status: get dma channel status
 * @get_lock_status: get tile, shimdma and memtile lock status
 * @wake_tile_uc_core_up: wakes shile tile uc core up
 * @get_uc_core_sts: Retrieve the status of a uc core
 * @get_uc_core_intr: Retrieve the status of a uc core interrupt
 * @get_uc_mdm_dbg_sts: Retrieve the status of a uc core mdm debug
 * @get_uc_dma_dm2mm_sts: Retrieve the status of a uc core dm2mm
 * @get_uc_dma_mm2dm_sts: Retrieve the status of a uc core mm2dm
 * @get_uc_mod_aximm: Retrieve the status of a uc core aximm
 * @get_uc_mod_aximm_out_trans: Retrieve the status of a uc core aximm out transactions
 * @map_uc_mem: Get uc offset to AI array offset map for mems
 * @part_init: partition initialize for tiles.
 * @part_teardown: partition teardown.
 * @part_clear_context: partition clear context, subset of partition init.
 * @part_clean: partition clean for tiles.
 * @part_reset: reset partition.
 *
 * Different AI engine device version has its own device
 * operation.
 */
struct aie_tile_operations {
	u32 (*get_tile_type)(struct aie_device *adev, struct aie_location *loc);
	unsigned int (*get_mem_info)(struct aie_device *adev,
				     struct aie_range *range,
				     struct aie_part_mem *pmem);
	u32 (*get_core_status)(struct aie_partition *apart,
			       struct aie_location *loc);
	ssize_t (*get_part_sysfs_lock_status)(struct aie_partition *apart,
					      struct aie_location *loc,
					      char *buffer, ssize_t size);
	ssize_t (*get_tile_sysfs_lock_status)(struct aie_partition *apart,
					      struct aie_location *loc,
					      char *buffer, ssize_t size);
	ssize_t (*get_part_sysfs_dma_status)(struct aie_partition *apart,
					     struct aie_location *loc,
					     char *buffer, ssize_t size);
	ssize_t (*get_tile_sysfs_dma_status)(struct aie_partition *apart,
					     struct aie_location *loc,
					     char *buffer, ssize_t size);
	ssize_t (*get_tile_sysfs_bd_metadata)(struct aie_partition *apart,
					      struct aie_location *loc,
					      char *buffer, ssize_t size);
	int (*init_part_clk_state)(struct aie_partition *apart);
	int (*scan_part_clocks)(struct aie_partition *apart);
	int (*set_part_clocks)(struct aie_partition *apart);
	int (*set_column_clock)(struct aie_partition *apart, struct aie_column_args *args);
	int (*set_tile_isolation)(struct aie_partition *apart,
				  struct aie_location *loc, u8 dir);
	int (*mem_clear)(struct aie_partition *apart);
	u32 (*get_dma_s2mm_status)(struct aie_partition *apart,
				   struct aie_location *loc,
				   u8 chanid);
	u32 (*get_dma_mm2s_status)(struct aie_partition *apart,
				   struct aie_location *loc,
				   u8 chanid);
	u8 (*get_chan_status)(struct aie_partition *apart,
			      struct aie_location *loc,
			      u32 status);
	u32 (*get_lock_status)(struct aie_partition *apart,
			       struct aie_location *loc,
			       u8 lock);
	int (*wake_tile_uc_core_up)(struct aie_partition *apart,
				    struct aie_location *loc);
	u32 (*get_uc_core_sts)(struct aie_partition *apart,
			       struct aie_location *loc);
	u32 (*get_uc_core_intr)(struct aie_partition *apart,
				struct aie_location *loc);
	u32 (*get_uc_mdm_dbg_sts)(struct aie_partition *apart,
				  struct aie_location *loc);
	u32 (*get_uc_dma_dm2mm_sts)(struct aie_partition *apart,
				    struct aie_location *loc);
	u32 (*get_uc_dma_mm2dm_sts)(struct aie_partition *apart,
				    struct aie_location *loc);
	u32 (*get_uc_mod_aximm)(struct aie_partition *apart,
				struct aie_location *loc);
	u32 (*get_uc_mod_aximm_out_trans)(struct aie_partition *apart,
					  struct aie_location *loc);
	int (*map_uc_mem)(struct aie_partition *apart, u64 addr, struct aie_part_mem *pmem);
	int (*part_init)(struct aie_partition *apart, struct aie_partition_init_args *args);
	int (*part_teardown)(struct aie_partition *apart);
	int (*part_clear_context)(struct aie_partition *apart);
	int (*part_clean)(struct aie_partition *apart);
	int (*part_reset)(struct aie_partition *apart);
};

/**
 * struct aie_resource - AI engine resource structure
 * @bitmap: resource bitmap
 * @total: total number of resource
 */
struct aie_resource {
	unsigned long *bitmap;
	u32 total;
};

struct aie_event_bc_block {
	u32 south_set;
	u32 south_clr;
	u32 south_value;
	u32 reserved0;
	u32 west_set;
	u32 west_clr;
	u32 west_value;
	u32 reserved1;
	u32 north_set;
	u32 north_clr;
	u32 north_value;
	u32 reserved2;
	u32 east_set;
	u32 east_clr;
	u32 east_value;
};

/**
 * struct aie_event_attr - AI Engine event attributes structure.
 * @bc_event: broadcast event attribute to capture event mask value and
 *	      register offset from @bc_regoff.
 * @group_error: group error attribute to capture error group mask value and
 *		 register offset value from @group_regoff.
 * @bc_block_a: broadcast block a south-set register offset.
 * @bc_block_b: broadcast block b south-set register offset.
 * @error_halt_event: error halt event register offset.
 * @error_halt_event_group: default value of error halt event group.
 * @event_group_error0_enable: group_error0 register offset.
 * @event_group_error0_enable_default: default event0 group enabled mask.
 * @bc_regoff: base broadcast register offset.
 * @status_regoff: base status register offset.
 * @group_regoff: base group error register offset.
 * @base_error_event: event ID of first error event in a group error.
 * @num_broadcasts: total number of broadcast events.
 * @base_bc_event: broadcast 0 vent ID
 * @base_error_group: First event ID of @base_bc_event event group.
 * @user_event1: USER_EVENT1 event id.
 * @uc_error_group: event ID of Uc error group.
 * @num_events: total number of events.
 */
struct aie_event_attr {
	struct aie_single_reg_field bc_event;
	struct aie_single_reg_field group_error;
	struct aie_single_reg_field bc_block_a;
	struct aie_single_reg_field bc_block_b;
	struct aie_single_reg_field error_halt_event;
	u32 error_halt_event_group;
	struct aie_single_reg_field event_group_error0_enable;
	u32 event_group_error0_enable_default;
	u32 bc_regoff;
	u32 status_regoff;
	u32 group_regoff;
	u32 base_error_event;
	u32 num_broadcasts;
	u32 base_bc_event;
	u32 base_error_group;
	u32 user_event1;
	u32 uc_error_group;
	u32 num_events;
};

/**
 * struct aie_l1_intr_ctrl_attr - AI engine level 1 interrupt controller
 *				  attributes structure.
 * @mask_a: switch A level 1 interrupt controller mask attribute.
 * @enable_a: switch A level 1 interrupt controller enable attribute.
 * @disable_a: switch A level 1 interrupt controller disable attribute.
 * @irq_no_a: switch A level 1 interrupt controller irq_no attribute.
 * @irq_event_a: switch A level 1 interrupt controller irq_event attribute.
 * @block_north_a_set: switch A level 1 interrupt controller block_north set attribute.
 * @block_north_a_clear: switch A level 1 interrupt controller block north clear attribute.
 * @block_north_a_value: switch A level 1 interrupt controller block north value attribute.
 * @mask_b: switch B level 1 interrupt controller mask attribute.
 * @enable_b: switch B level 1 interrupt controller enable attribute.
 * @disable_b: switch B level 1 interrupt controller disable attribute.
 * @irq_no_b: switch B level 1 interrupt controller irq_no attribute.
 * @irq_event_b: switch B level 1 interrupt controller irq_event attribute.
 * @block_north_b_set: switch B level 1 interrupt controller block_north set attribute.
 * @block_north_b_clear: switch B level 1 interrupt controller block north clear attribute.
 * @block_north_b_value: switch B level 1 interrupt controller block north value attribute.
 * @swa_status: switch A level 1 interrupt controller status attribute.
 * @swb_status: switch B level 1 interrupt controller status attribute.
 * @swa_event: switch A level 1 interrupt controller event attribute.
 * @swb_event: switch B level 1 interrupt controller event attribute.
 * @regoff: base level 1 interrupt controller register offset.
 * @event_lsb: lsb of IRQ event within IRQ event switch register.
 * @num_broadcasts: total number of broadcast signals to level 1 interrupt
 *		    controller.
 */
struct aie_l1_intr_ctrl_attr {
	struct aie_single_reg_field mask_a;
	struct aie_single_reg_field enable_a;
	struct aie_single_reg_field disable_a;
	struct aie_single_reg_field irq_no_a;
	struct aie_single_reg_field irq_event_a;
	struct aie_single_reg_field block_north_a_set;
	struct aie_single_reg_field block_north_a_clear;
	struct aie_single_reg_field block_north_a_value;

	struct aie_single_reg_field mask_b;
	struct aie_single_reg_field enable_b;
	struct aie_single_reg_field disable_b;
	struct aie_single_reg_field irq_no_b;
	struct aie_single_reg_field irq_event_b;
	struct aie_single_reg_field block_north_b_set;
	struct aie_single_reg_field block_north_b_clear;
	struct aie_single_reg_field block_north_b_value;

	struct aie_single_reg_field swa_status;
	struct aie_single_reg_field swb_status;
	struct aie_single_reg_field swa_event;
	struct aie_single_reg_field swb_event;
	u32 regoff;
	u32 event_lsb;
	u32 num_broadcasts;
};

/**
 * struct aie_l2_intr_ctrl_attr - AI engine level 2 interrupt controller
 *				  attributes structure.
 * @mask: level 2 interrupt controller mask attribute.
 * @enable: level 2 interrupt controller enable attribute.
 * @disable: level 2 interrupt controller disable attribute.
 * @status: level 2 interrupt controller status attribute.
 * @intr: level 2 interrupt controller interrupt.
 * @regoff: level 2 interrupt controller register offset.
 * @num_broadcasts: total number of broadcast signals to level 2 interrupt
 *		    controller.
 */
struct aie_l2_intr_ctrl_attr {
	struct aie_single_reg_field mask;
	struct aie_single_reg_field enable;
	struct aie_single_reg_field disable;
	struct aie_single_reg_field status;
	struct aie_single_reg_field intr;
	u32 regoff;
	u32 num_broadcasts;
};

/**
 * struct aie_error_cb - AI engine error callback struct.
 * @cb: pointer to callback function.
 * @priv: data to be passed to the callback function.
 */
struct aie_error_cb {
	void (*cb)(void *priv);
	void *priv;
};

/**
 * struct aie_event_prop - AI engine event property.
 * @event: error event ID.
 * @event_str: error string.
 */
struct aie_event_prop {
	u32 event;
	char *event_str;
};

/**
 * struct aie_err_category - AI engine errors category.
 * @err_category: category of error.
 * @num_events: number of event IDs in a category.
 * @prop: pointer to an array event properties.
 */
struct aie_err_category {
	u32 err_category;
	u32 num_events;
	const struct aie_event_prop *prop;
};

/**
 * struct aie_error_attr - AI engine error attribute.
 * @num_err_categories: number of possible error categories valid for a given
 *			module.
 * @err_category: pointer to an array of error categories.
 */
struct aie_error_attr {
	u32 num_err_categories;
	const struct aie_err_category *err_category;
};

/**
 * struct aie_rsc_stat - AI engine hardware resource status bitmap of a
 *			 resource of a module type of a tile type of an AI
 *			 engine partition
 * @rbits: runtime allocated resource bitmap
 * @sbits: static resource bitmap for resources allocated at compilation
 *	   time
 */
struct aie_rsc_stat {
	struct aie_resource rbits;
	struct aie_resource sbits;
};

/**
 * struct aie_mod_rscs - AI engine hardware resource status bitmaps of
 *			 a module type of a tile type of an AI engine
 *			 partition.
 * @rscs_stat: resource status bitmaps
 */
struct aie_mod_rscs {
	struct aie_rsc_stat *rscs_stat;
};

/**
 * struct aie_tile_rscs - AI engine hardware resource status bitmaps of all
 *			  resources of a tile type of a partition.
 * @mod_rscs: array of pointers of AI engine resources. Each element is an
 *	      array of hardware resources of different modules of a particular
 *	      resource type of a tile type.
 *	      e.g. if the tile type is TILE. The rscs are an arrary of
 *	      resources bitmap of all the defined AI engine resources types of
 *	      TILE type. e.g. the element of AIE_RSCTYPE_PERF. It is an array
 *	      of perfcounter resources bitmaps for both core module and memory
 *	      module of TILE type of an AI engine partition.
 */
struct aie_tile_rscs {
	struct aie_mod_rscs *mod_rscs[AIE_RSCTYPE_MAX];
};

/**
 * struct aie_mod_rsc_attr - AI engine resource attribute of a module
 * @num_rscs: number of resource
 */
struct aie_mod_rsc_attr {
	u8 num_rscs;
};

/**
 * struct aie_tile_rsc_attr - AI engine resource attributes
 * @mod_attr: array of resource attribute different modules of a tile type of
 *	      a particular resource type.
 */
struct aie_tile_rsc_attr {
	struct aie_mod_rsc_attr mod_attr[AIE_MAX_MODS_PER_TILE];
};

/**
 * struct aie_lock_attr - AI engine lock attributes
 * @sts: lock status field attributes
 * @sts_regoff: lock status register offset
 * @num_locks: number of locks
 * @overflow: overflow status field attributes
 * @overflow_regoff: overflow status register offset
 * @underflow: underflow status field attributes
 * @underflow_regoff: underflowstatus register offset
 */
struct aie_lock_attr {
	struct aie_single_reg_field sts;
	u32 sts_regoff;
	u32 num_locks;
	struct aie_single_reg_field overflow;
	u32 overflow_regoff;
	struct aie_single_reg_field underflow;
	u32 underflow_regoff;
};

/**
 * struct aie_tile_attr - AI engine device tile type attributes
 * @start_row: start row
 * @num_rows: number of rows
 * @num_mods: number of modules of this tile type
 * @mods: array of module types of this tile type
 * @rscs_attr: resources attributes array. Each element is an array of
 *	       attributes of a resource type of a tile type.
 */
struct aie_tile_attr {
	u8 start_row;
	u8 num_rows;
	u8 num_mods;
	const enum aie_module_type *mods;
	const struct aie_tile_rsc_attr *rscs_attr;
};

/**
 * struct aie_dev_attr - device attribute properties for AI Engine sysfs nodes.
 * @name: name of the device attribute
 * @mode: permissions associated
 * @tile_type: tile type(s) attribute is valid for. use AIE_TILE_TYPE_MASK_*.
 * @show: read function handler
 * @store: write function handler
 */
struct aie_dev_attr {
	const char *name;
	umode_t mode;
	u32 tile_type;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr,
			char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count);
};

/**
 * struct aie_sysfs_prop - private data passed to the sysfs read/write handler.
 * @data: buffer to export sysfs data
 * @size: size of data exported
 * @max_size: max size of data that could be exported
 * @read_callback: callback to fetch data from on read
 * @write_callback: callback to send data to on write
 */
struct aie_sysfs_prop {
	char *data;
	ssize_t size;
	ssize_t max_size;
	ssize_t (*read_callback)(struct kobject *kobj, char *buffer,
				 ssize_t size);
	ssize_t (*write_callback)(struct kobject *kobj, char *buffer,
				  ssize_t size);
};

/**
 * struct aie_bin_attr - binary attribute properties for AI Engine sysfs nodes
 * @name: name of the binary attribute
 * @mode: permissions associated
 * @size: size of the buffer to be allocated
 * @tile_type: tile type(s) attribute is valid for. use AIE_TILE_TYPE_MASK_*.
 * @read: read handler
 * @write: write handler
 * @read_callback: callback to fetch data from on read
 * @write_callback:  callback to send data to on write
 */
struct aie_bin_attr {
	const char *name;
	umode_t mode;
	ssize_t size;
	u32 tile_type;
	ssize_t (*read)(struct file *filp, struct kobject *kobj,
			struct bin_attribute *attr, char *buf, loff_t offset,
			size_t max_size);
	ssize_t (*write)(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *attr, char *buf, loff_t offset,
			 size_t max_size);
	ssize_t (*read_callback)(struct kobject *kobj, char *buffer,
				 ssize_t size);
	ssize_t (*write_callback)(struct kobject *kobj, char *buffer,
				  ssize_t size);
};

/**
 * struct aie_sysfs_attr - captures all sysfs attributes defined at
 *			   partition or tile level.
 * @dev_attr: pointer to array of device attributes
 * @bin_attr: pointer to array of binary attributes
 * @num_dev_attrs: number of device attributes
 * @num_bin_attrs: number of binary attributes
 */
struct aie_sysfs_attr {
	const struct aie_dev_attr *dev_attr;
	const struct aie_bin_attr *bin_attr;
	u32 num_dev_attrs;
	u32 num_bin_attrs;
};

/**
 * struct aie_tile - AI engine tile structure
 * @loc: tile co-ordinates
 * @apart: parent partition the tile belongs to
 * @dev: device for the AI engine tile device
 * @attr_grp: attribute group
 */
struct aie_tile {
	struct aie_location loc;
	struct aie_partition *apart;
	struct device dev;
	struct attribute_group *attr_grp;
};

/**
 * struct aie_dma_addrlen - Stores the address and length of the transaction
 * @dma_addr: dma address array.
 * @len: length of the data to be transferred.
 */
struct aie_dma_addrlen {
	dma_addr_t dma_addr;
	size_t len;
};

/**
 * struct aie_addrlen - Stores the address and length of the transaction
 * @addr: load cert src address.
 * @len: length of the data to be transferred.
 */
struct aie_addrlen {
	void *addr;
	size_t len;
};

/**
 * struct aie_device - AI engine device structure
 * @apertures: list of apertures
 * @cdev: cdev for the AI engine
 * @dev: device for the AI engine device
 * @mlock: protection for AI engine device operations
 * @clk: AI enigne device clock
 * @kernel_regs: array of kernel only registers
 * @core_regs_clr: array of core registers to be cleared
 * @ops: tile operations
 * @col_rst: column reset attribute
 * @col_clkbuf: column clock buffer attribute
 * @noc_outstanding_aximm: register for outstanding noc aximm
 * @uc_outstanding_aximm: register for outstanding uc aximm
 * @shimnoc_uc_corectrl: UC core control attribute
 * @shim_bd: SHIM DMA buffer descriptor attribute
 * @tile_bd: tile DMA buffer descriptor attribute
 * @memtile_bd: MEM tile DMA buffer descriptor attribute
 * @shim_dma: SHIM DMA attribute
 * @tile_dma: tile DMA attribute
 * @memtile_dma: MEM tile DMA attribute
 * @pl_events: pl module event attribute
 * @memtile_events: memory tile event attribute
 * @mem_events: memory module event attribute
 * @core_events: core module event attribute
 * @mem_lock: mem lock attribute
 * @pl_lock: Shim tile lock attribute
 * @memtile_lock: Mem Tile lock attribute
 * @l1_ctrl: level 1 interrupt controller attribute
 * @l2_ctrl: level 2 interrupt controller attribute
 * @core_errors: core module error attribute
 * @mem_errors: memory module error attribute
 * @memtile_errors: memory tile error attribute
 * @shim_errors: shim tile error attribute
 * @array_shift: array address shift
 * @col_shift: column address shift
 * @row_shift: row address shift
 * @dev_gen: aie hardware device generation
 * @num_kernel_regs: number of kernel only registers range
 * @num_core_regs_clr: number of core registers to clear
 * @clock_id: AI Engine clock ID
 * @device_name: identify ssit device id
 * @ttype_attr: tile type attributes
 * @aperture_sysfs_attr: aperture level sysfs attributes
 * @part_sysfs_attr: partition level sysfs attributes
 * @tile_sysfs_attr: tile level sysfs attributes
 * @core_status_str: core status in string format
 * @core_pc: program counter attribute
 * @core_lr: link register attribute
 * @core_sp: stack pointer attribute
 * @hw_err_status: hw error status register attribute
 */
struct aie_device {
	struct list_head apertures;
	struct cdev cdev;
	struct device dev;
	struct mutex mlock; /* protection for AI engine apertures */
	struct clk *clk;
	const struct aie_tile_regs *kernel_regs;
	const struct aie_tile_regs *core_regs_clr;
	const struct aie_tile_operations *ops;
	const struct aie_single_reg_field *col_rst;
	const struct aie_single_reg_field *col_clkbuf;
	const struct aie_single_reg_field *noc_outstanding_aximm;
	const struct aie_single_reg_field *uc_outstanding_aximm;
	const struct aie_uc_corectrl_attr *shimnoc_uc_corectrl;
	const struct aie_bd_attr *shim_bd;
	const struct aie_bd_attr *tile_bd;
	const struct aie_bd_attr *memtile_bd;
	const struct aie_dma_attr *shim_dma;
	const struct aie_dma_attr *tile_dma;
	const struct aie_dma_attr *memtile_dma;
	const struct aie_event_attr *pl_events;
	const struct aie_event_attr *memtile_events;
	const struct aie_event_attr *mem_events;
	const struct aie_event_attr *core_events;
	const struct aie_lock_attr *mem_lock;
	const struct aie_lock_attr *memtile_lock;
	const struct aie_lock_attr *pl_lock;
	const struct aie_l1_intr_ctrl_attr *l1_ctrl;
	const struct aie_l2_intr_ctrl_attr *l2_ctrl;
	const struct aie_error_attr *core_errors;
	const struct aie_error_attr *mem_errors;
	const struct aie_error_attr *memtile_errors;
	const struct aie_error_attr *shim_errors;
	u32 array_shift;
	u32 col_shift;
	u32 row_shift;
	u32 dev_gen;
	u32 num_kernel_regs;
	u32 num_core_regs_clr;
	u32 clock_id;
	u32 device_name;
	struct aie_tile_attr ttype_attr[AIE_TILE_TYPE_MAX];
	const struct aie_sysfs_attr *aperture_sysfs_attr;
	const struct aie_sysfs_attr *part_sysfs_attr;
	const struct aie_sysfs_attr *tile_sysfs_attr;
	char **core_status_str;
	const struct aie_single_reg_field *core_pc;
	const struct aie_single_reg_field *core_lr;
	const struct aie_single_reg_field *core_sp;
	const struct aie_single_reg_field *hw_err_status;
};

struct aie_l2_mask {
	u32 *val;
	int count;
};

/**
 * struct aie_aperture - AI engine aperture structure
 * @node: list node
 * @partitions: list of partitions of this aperture
 * @adev: pointer to AI device instance
 * @mlock: protection for AI engine aperture operations
 * @base: AI engine aperture base virtual address
 * @res: memory resource of AI engine aperture
 * @dev: device of aperture
 * @cols_res: AI engine columns resources to indicate
 *	      while columns are occupied by partitions.
 * @node_id: AI engine aperture node id which is to identify
 *	     the aperture in the system in firmware
 * @npi_irq: Linux IRQ numbers
 * @range: range of aperture
 * @backtrack: workqueue to backtrack interrupt
 * @l2_mask: level 2 interrupt controller mask bitmap
 * @attr_grp: attribute group for sysfs
 */
struct aie_aperture {
	struct list_head node;
	struct list_head partitions;
	struct aie_device *adev;
	struct mutex mlock; /* protection for AI engine aperture operations */
	void __iomem *base;
	struct resource res;
	struct device dev;
	struct aie_resource cols_res;
	u32 node_id;
	int npi_irq[AIE_NPI_NUM_IRQS];
	struct aie_range range;
	struct work_struct backtrack;
	struct aie_l2_mask l2_mask;
	struct attribute_group *attr_grp;
};

struct aie_op_start_num_col {
	u16 type;         /* Operation Type */
	u16 len;          /* Operation struct length */
	u16 start_col;    /* Start Column */
	u16 num_col;      /* Number of Columns */
} __aligned(4);

/**
 * struct aie_op_l2_ctrl_irq - L2 Control IRQ ops
 * @type: operation type.
 * @len: length of the op
 * @irq: irq value to be written to L2start_col: start column
 */
struct  aie_op_l2_ctrl_irq {
	u16 type;         /* Operation Type */
	u16 len;          /* Operation struct length */
	u16 irq;          /* Value to be written to the L2 interrupt controller register. */
} __aligned(4);

struct aie_op_type_len {
	u16 type;        /* Operation Type */
	u16 len;         /* Operation struct length */
} __aligned(4);

struct aie_op_hw_err {
	u16 type;         /* Operation Type */
	u16 len;          /* Operation struct length */
	u16 val;          /* Depends on the operation. Refer the table below for more information.*/
} __aligned(4);

struct aie_op_uc_zeroisation {
	u16 type;         /* Operation Type */
	u16 len;          /* Operation struct length*/
	u16 flag;         /* Value to be written to the uc zeroization register */
} __aligned(4);

struct aie_op_handshake_data {
	void *addr;
	size_t size;
};

struct aie_op_handshake {
	u16 type;         /* Operation Type */
	u16 len;          /* Operation struct length*/
	u32 high_addr; /* physical address of the buffer that has handshake data */
	u32 low_addr;
} __aligned(4);

struct aie_op_nmu_switch {
	u16 type;      /* Operation Type */
	u16 len;       /* Operation struct length */
	u16 c0_route;  /* Value to be written to column 0 nmu switch register */
	u16 c1_route;  /* Value to be written to column 1 nmu switch register */
} __aligned(4);

struct aie_op_aximm_isolation {
	u16 type;      /* Operation Type */
	u16 len;       /* Operation struct length */
	u16 traffic;   /* Value to be written to the aximm isolation register */
} __aligned(4);

struct aie_op_ecc_scrub_period {
	u16 type; /* Operation Type */
	u16 len; /* Operation struct length */
	u16 scrub_period; /* Value to be written to the ecc scrub period register */
} __aligned(4);

/**
 * struct aie_pm_ops - AI engine plm calls struct.
 * @pkt_va: pm ops data virtual address.
 * @pkt_dma: pm ops data dma address.
 * @size: size of pkt_va.
 * @offset: offset within pkt_va;
 * @op_range: pointer to pkt_va for latest range. All op headers added at offset will be for this
 *	      range.
 */
struct aie_pm_ops {
	void *pkt_va;
	dma_addr_t pkt_dma;
	size_t size;
	size_t offset;
	struct aie_op_start_num_col *op_range;
};

/**
 * struct aie_partition - AI engine partition structure
 * @node: list node
 * @dbufs: dmabufs list
 * @aperture: pointer to AI engine aperture
 * @adev: pointer to AI device instance
 * @filep: pointer to file for refcount on the users of the partition
 * @pmems: pointer to partition memories types
 * @dma_mem: dma memory list
 * @dbufs_cache: memory management object for preallocated dmabuf descriptors
 * @trscs: resources bitmaps for each tile
 * @freq_req: required frequency
 * @range: range of partition
 * @mlock: protection for AI engine partition operations
 * @dev: device for the AI engine partition
 * @atiles: pointer to an array of AIE tile structure.
 * @cores_clk_state: bitmap to indicate the power state of core modules
 * @tiles_inuse: bitmap to indicate if a tile is in use
 * @error_cb: error callback
 * @core_event_status: core module event bitmap
 * @mem_event_status: memory module event bitmap
 * @pl_event_status: pl module event bitmap
 * @attr_grp: attribute group
 * @pm_ops: pm ops pkt and metadata for zynq plm calls
 * @partition_id: partition id. Partition ID is the identifier
 *		  of the AI engine partition in the system.
 * @status: indicate if the partition is in use
 * @cntrflag: partition control flag. e.g. whether to reset columns when
 *	      the partition is released
 * @user_event1_complete: call back function for inference complete
 * @user_event1_priv: priv data for user_event1_complete cb
 * @error_to_report: indicates if there are errors pending to be reported to
 *		     the application. This value is set to true if errors are
 *		     found during backtracking, and error interrupt was
 *		     received when partition was not requested yet.
 */
struct aie_partition {
	struct list_head node;
	struct list_head dbufs;
	struct aie_aperture *aperture;
	struct aie_device *adev;
	struct file *filep;
	struct aie_part_mem *pmems;
	struct list_head dma_mem;
	struct kmem_cache *dbufs_cache;
	struct aie_tile_rscs trscs[AIE_TILE_TYPE_MAX];
	u64 freq_req;
	struct aie_range range;
	struct mutex mlock; /* protection for AI engine partition operations */
	struct device dev;
	struct aie_tile *atiles;
	struct aie_resource cores_clk_state;
	struct aie_resource tiles_inuse;
	struct aie_error_cb error_cb;
	struct aie_resource core_event_status;
	struct aie_resource mem_event_status;
	struct aie_resource pl_event_status;
	struct attribute_group *attr_grp;
	struct aie_pm_ops pm_ops;
	u32 partition_id;
	u32 status;
	u32 cntrflag;
	void (*user_event1_complete)(__u32 partition_id, void *user_event1_priv);
	void *user_event1_priv;
	u8 error_to_report;
};

/**
 * struct aie_part_pinned_region - AI engine user space pinned region
 * @user_addr: user space address
 * @len: length of the user space buffer in bytes
 * @npages: number of pages of the user space buffer
 * @pages: array to receive pointers to the pages pinned.
 *	   should be at least npages long
 * @aie_dma_handle: DMA physical address handle for AIE.
 */
struct aie_part_pinned_region {
	u64 user_addr;
	u64 len;
	struct page **pages;
	int npages;
	dma_addr_t aie_dma_handle;
};

extern struct class *aie_class;
extern const struct file_operations aie_part_fops;

#define cdev_to_aiedev(i_cdev) container_of((i_cdev), struct aie_device, cdev)
#define dev_to_aiedev(_dev) container_of((_dev), struct aie_device, dev)
#define dev_to_aieaperture(_dev) container_of((_dev), struct aie_aperture, dev)
#define dev_to_aiepart(_dev) container_of((_dev), struct aie_partition, dev)
#define dev_to_aietile(_dev) container_of((_dev), struct aie_tile, dev)

#define aie_col_mask(adev) ({ \
	struct aie_device *_adev = (adev); \
	GENMASK_ULL(_adev->array_shift - 1, _adev->col_shift);  \
	})

#define aie_row_mask(adev) ({ \
	struct aie_device *_adev = (adev); \
	GENMASK_ULL(_adev->col_shift - 1, _adev->row_shift);  \
	})

#define aie_tile_reg_mask(adev) ({ \
	struct aie_device *_adev = (adev); \
	GENMASK_ULL(_adev->row_shift - 1, 0);  \
	})

/*
 * Need to define field get, as AI engine shift mask is not constant.
 * Cannot use FIELD_GET()
 */
#define aie_tile_reg_field_get(mask, shift, regoff) ( \
	((regoff) & (mask)) >> (shift))

#define aie_cal_tile_reg(adev, regoff) ( \
	aie_tile_reg_field_get(aie_tile_reg_mask(adev), 0, regoff))

/**
 * aie_get_field_val() - calculate value of an AI engine register field
 * @field: a field in a register
 * @val: value of the field
 * @return: value of a register field
 */
static inline u32 aie_get_field_val(const struct aie_single_reg_field *field,
				    u32 val)
{
	long long mask = (long long)field->mask & 0x00000000ffffffff;

	return (val << __bf_shf(mask)) & field->mask;
}

/**
 * aie_get_reg_field() - get value from a field from a register valuer
 * @field: a field in a register
 * @regval: register value
 * @return: value of a register field
 */
static inline u32 aie_get_reg_field(const struct aie_single_reg_field *field,
				    u32 regval)
{
	long long mask64 = (long long)field->mask & 0x00000000ffffffff;

	return (regval & field->mask) >> __bf_shf(mask64);
}

/**
 * aie_cal_regoff() - calculate register offset to the whole AI engine
 *		      device start address
 * @adev: AI engine device
 * @loc: AI engine tile location
 * @regoff_intile: register offset within a tile
 * @return: register offset to the whole AI engine device start address
 */
static inline u32 aie_cal_regoff(struct aie_device *adev,
				 struct aie_location loc, u32 regoff_intile)
{
	return regoff_intile + (loc.col << adev->col_shift) +
	       (loc.row << adev->row_shift);
}

/**
 * aie_aperture_cal_regoff() - calculate register offset to the whole AI engine
 *                             device start address
 * @aperture: AI aperture
 * @loc: AI engine tile location
 * @regoff_intile: register offset within a tile
 * @return: register offset to the whole AI engine device start address
 */
static inline u32 aie_aperture_cal_regoff(struct aie_aperture *aperture,
					  struct aie_location loc,
					  u32 regoff_intile)
{
	struct aie_device *adev = aperture->adev;

	return regoff_intile + ((loc.col - aperture->range.start.col) <<
				adev->col_shift) + (loc.row << adev->row_shift);
}

/**
 * aie_validate_location() - validate tile location within an AI engine
 *			     partition
 * @apart: AI engine partition
 * @loc: AI engine tile location relative in partition
 * @return: return 0 if it is valid, negative value for errors.
 *
 * This function checks if the AI engine location is within the AI engine
 * partition.
 */
static inline int aie_validate_location(struct aie_partition *apart,
					struct aie_location loc)
{
	if (loc.col >= apart->range.size.col ||
	    loc.row >= apart->range.size.row)
		return -EINVAL;

	return 0;
}

static inline int aie_get_tile_status_size(struct aie_partition *apart,
					   struct aie_location *loc)
{
	switch (apart->adev->ops->get_tile_type(apart->adev, loc)) {
	case AIE_TILE_TYPE_MEMORY:
		return AIE_NUM_EVENT_STS_MEMTILE;
	case AIE_TILE_TYPE_TILE:
		return AIE_NUM_EVENT_STS_CORETILE;
	case AIE_TILE_TYPE_SHIMPL:
	case AIE_TILE_TYPE_SHIMNOC:
		return AIE_NUM_EVENT_STS_SHIMTILE;
	default:
		return 1;
	}
}

/**
 * aie_resource_or_get_valueul() - get unsigned long value of specified
 *				   number of bits starting from specified
 *				   start bit of a resource bitmap
 *
 * @res: pointer to AI engine resource
 * @sbit: start bit for OR operation
 * @nbits: number of bits to OR
 * @return: or result of @nbits of two bitmaps starting from @sbit
 *
 * OR @nbits of two resource bitmaps starting from @sbit
 */
static inline
unsigned long aie_resource_or_get_valueul(struct aie_resource *res,
					  u32 sbit, u32 nbits)
{
	const size_t i = BIT_WORD(sbit);
	unsigned long bits;

	bits = res->bitmap[i];
	bits >>= (sbit % BITS_PER_LONG);
	bits |= BITMAP_FIRST_WORD_MASK(nbits);

	return bits;
}

int aie_resource_initialize(struct aie_resource *res, int count);
void aie_resource_uninitialize(struct aie_resource *res);
int aie_resource_check_region(struct aie_resource *res, u32 start,
			      u32 count);
int aie_resource_get_region(struct aie_resource *res, u32 start,
			    u32 count);
void aie_resource_put_region(struct aie_resource *res, int start, u32 count);
int aie_resource_set(struct aie_resource *res, u32 start, u32 count);
int aie_resource_cpy_from_arr32(struct aie_resource *res, u32 start,
				const u32 *src, u32 nbits);
int aie_resource_cpy_to_arr32(struct aie_resource *res, u32 start, u32 *dst,
			      u32 nbits);
int aie_resource_clear(struct aie_resource *res, u32 start, u32 count);
int aie_resource_clear_all(struct aie_resource *res);
bool aie_resource_testbit(struct aie_resource *res, u32 bit);
int aie_resource_check_common_avail(struct aie_resource *res0,
				    struct aie_resource *res1,
				    u32 sbit, u32 nbits);
int aie_resource_get_common_avail(struct aie_resource *res0,
				  struct aie_resource *res1,
				  u32 sbit, u32 nbits, u32 total,
				  struct aie_rsc *rscs);
int aie_resource_check_pattern_region(struct aie_resource *res,
				      u32 start, u32 end, u32 count);
int aie_resource_check_common_pattern_region(struct aie_resource *res0,
					     struct aie_resource *res1,
					     u32 sbit, u32 nbits, u32 total);
int aie_resource_get_common_pattern_region(struct aie_resource *res0,
					   struct aie_resource *res1,
					   u32 sbit, u32 nbits, u32 total,
					   struct aie_rsc *rscs);

const struct file_operations *aie_part_get_fops(void);
u8 aie_part_in_use(struct aie_partition *apart);
struct aie_partition *aie_get_partition_from_id(struct aie_device *adev,
						u32 partition_id);
void of_xilinx_ai_engine_aperture_probe(struct aie_device *adev);
struct aie_device *of_ai_engine_class_find(struct device_node *np);
int xilinx_ai_engine_add_dev(struct aie_device *adev,
			     struct platform_device *pdev);
int xilinx_ai_engine_probe_v1(struct platform_device *pdev);

void aie_part_remove(struct aie_partition *apart);
int aie_part_clear_context(struct aie_partition *apart);
int aie_part_clean(struct aie_partition *apart);
int aie_part_open(struct aie_partition *apart, void *rsc_metadata);
int aie_part_initialize(struct aie_partition *apart, struct aie_partition_init_args *args);
int aie_part_teardown(struct aie_partition *apart);

int aie_mem_get_info(struct aie_partition *apart, unsigned long arg);

long aie_part_attach_dmabuf_req(struct aie_partition *apart,
				void __user *user_args);
long aie_part_detach_dmabuf_req(struct aie_partition *apart,
				void __user *user_args);
long aie_part_set_bd_from_user(struct aie_partition *apart,
					void __user *user_args);
long aie_part_set_bd(struct aie_partition *apart,
					struct aie_dma_bd_args *args);
long aie_part_set_dmabuf_bd_from_user(struct aie_partition *apart,
			    void __user *user_args);
long aie_part_update_dmabuf_bd_from_user(struct aie_partition *apart,
					 void __user *user_args);
long aie_part_set_dmabuf_bd(struct aie_partition *apart,
					struct aie_dmabuf_bd_args *args);
void aie_part_release_dmabufs(struct aie_partition *apart);
int aie_part_prealloc_dbufs_cache(struct aie_partition *apart);

int aie_part_scan_clk_state(struct aie_partition *apart);
bool aie_part_check_clk_enable_loc(struct aie_partition *apart,
				   struct aie_location *loc);
int aie_part_set_freq(struct aie_partition *apart, u64 freq);
int aie_part_get_freq(struct aie_partition *apart, u64 *freq);

int aie_part_request_tiles(struct aie_partition *apart, int num_tiles,
			   struct aie_location *locs);
int aie_part_release_tiles(struct aie_partition *apart, int num_tiles,
			   struct aie_location *locs);
int aie_part_request_tiles_from_user(struct aie_partition *apart,
				     void __user *user_args);
int aie_part_release_tiles_from_user(struct aie_partition *apart,
				     void __user *user_args);
int aie_device_init(struct aie_device *adev);
int aieml_device_init(struct aie_device *adev);
int aie2ps_device_init(struct aie_device *adev);

bool aie_part_has_mem_mmapped(struct aie_partition *apart);
bool aie_part_has_regs_mmapped(struct aie_partition *apart);

int aie_part_get_tile_rows(struct aie_partition *apart,
			   enum aie_tile_type ttype);

int aie_part_reset(struct aie_partition *apart);
int aie_part_post_reinit(struct aie_partition *apart);
int aie_part_init_isolation(struct aie_partition *apart);
struct aie_partition *aie_create_partition(struct aie_aperture *aperture,
					   u32 partition_id);

void aie_aperture_backtrack(struct work_struct *work);
irqreturn_t aie_interrupt(int irq, void *data);
irqreturn_t aie2ps_interrupt_fn(int irq, void *data);
irqreturn_t aie2ps_interrupt_user_event1(int irq, void *data);
void aie_interrupt_callback(const u32 *payload, void *data);
int aie_aperture_create_l2_mask(struct aie_aperture *aperture);
bool aie_part_has_error(struct aie_partition *apart);
void aie_part_clear_cached_events(struct aie_partition *apart);
int aie_part_set_intr_rscs(struct aie_partition *apart);

struct aie_aperture *
of_aie_aperture_probe(struct aie_device *adev, struct device_node *nc);
int aie_aperture_remove(struct aie_aperture *aperture);
int aie_aperture_check_part_avail(struct aie_aperture *aperture,
				  struct aie_partition_req *req);
struct aie_partition *
aie_aperture_request_part_from_id(struct aie_aperture *aperture,
				  u32 partition_id);
int aie_aperture_enquire_parts(struct aie_aperture *aperture,
			       unsigned int num_queries,
			       struct aie_range_args  *queries,
			       int *num_parts_left, bool to_user);
unsigned int aie_aperture_get_num_parts(struct aie_aperture *aperture);
int aie_aperture_add_dev(struct aie_aperture *aperture,
			 struct device_node *nc);

int aie_part_rscmgr_init(struct aie_partition *apart);
void aie_part_rscmgr_finish(struct aie_partition *apart);
void aie_part_rscmgr_reset(struct aie_partition *apart);
long aie_part_rscmgr_rsc_req(struct aie_partition *apart,
			     void __user *user_args);
long aie_part_rscmgr_rsc_release(struct aie_partition *apart,
				 void __user *user_args);
long aie_part_rscmgr_rsc_free(struct aie_partition *apart,
			      void __user *user_args);
long aie_part_rscmgr_rsc_req_specific(struct aie_partition *apart,
				      void __user *user_args);
long aie_part_rscmgr_rsc_check_avail(struct aie_partition *apart,
				     void __user *user_args);
long aie_part_rscmgr_get_broadcast(struct aie_partition *apart,
				   void __user *user_args);
int aie_part_rscmgr_set_static(struct aie_partition *apart, void *meta);
int aie_part_rscmgr_set_tile_broadcast(struct aie_partition *apart,
				       struct aie_location loc,
				       enum aie_module_type mod, uint32_t id);

int aie_aperture_sysfs_create_entries(struct aie_aperture *aperture);
void aie_aperture_sysfs_remove_entries(struct aie_aperture *aperture);
int aie_part_sysfs_create_entries(struct aie_partition *apart);
void aie_part_sysfs_remove_entries(struct aie_partition *apart);
int aie_tile_sysfs_create_entries(struct aie_tile *atile);
void aie_tile_sysfs_remove_entries(struct aie_tile *atile);
ssize_t aie_sysfs_read_handler(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *attr, char *buf,
			       loff_t offset, size_t max_size);

ssize_t aie2ps_sysfs_get_uc_core_status(struct aie_partition *apart,
					struct aie_location *loc, char *buffer,
					ssize_t size);
ssize_t aie2ps_sysfs_get_uc_core_intr(struct aie_partition *apart,
				      struct aie_location *loc, char *buffer,
				      ssize_t size);
ssize_t aie2ps_sysfs_get_uc_mdm_dbg_sts(struct aie_partition *apart,
					struct aie_location *loc, char *buffer,
					ssize_t size);
ssize_t aie2ps_sysfs_get_uc_dma_dm2mm_sts(struct aie_partition *apart,
					  struct aie_location *loc, char *buffer,
					  ssize_t size);
ssize_t aie2ps_sysfs_get_uc_dma_mm2dm_sts(struct aie_partition *apart,
					  struct aie_location *loc, char *buffer,
					  ssize_t size);
ssize_t aie2ps_sysfs_get_uc_mod_aximm(struct aie_partition *apart,
				      struct aie_location *loc, char *buffer,
				      ssize_t size);
ssize_t aie2ps_sysfs_get_uc_mod_aximm_out_trans(struct aie_partition *apart,
						struct aie_location *loc, char *buffer,
						ssize_t size);
ssize_t aie_sysfs_get_core_status(struct aie_partition *apart,
				  struct aie_location *loc, char *buffer,
				  ssize_t size);
ssize_t aie_tile_show_core(struct device *dev, struct device_attribute *attr,
			   char *buffer);
ssize_t aie_part_read_cb_core(struct kobject *kobj, char *buffer, ssize_t size);
ssize_t aie_sysfs_get_dma_status(struct aie_partition *apart,
				 struct aie_location *loc, char *buffer,
				 ssize_t size);
ssize_t aie_tile_show_bd(struct device *dev, struct device_attribute *attr,
			 char *buffer);
ssize_t aie_tile_show_dma(struct device *dev, struct device_attribute *attr,
			  char *buffer);
ssize_t aie_part_read_cb_dma(struct kobject *kobj, char *buffer, ssize_t size);
ssize_t aie_tile_show_lock(struct device *dev, struct device_attribute *attr,
			   char *buffer);
ssize_t aie_part_read_cb_lock(struct kobject *kobj, char *buffer, ssize_t size);
ssize_t aie_sysfs_get_lock_status(struct aie_partition *apart,
				  struct aie_location *loc, char *buffer,
				  ssize_t size);
u32 aie_get_module_error_count(struct aie_partition *apart,
			       struct aie_location loc,
			       enum aie_module_type module,
			       const struct aie_error_attr *err_attr);
bool aie_check_tile_error(struct aie_partition *apart, struct aie_location loc);
bool aie_check_error_bitmap(struct aie_partition *apart,
			    struct aie_location loc,
			    enum aie_module_type module, u8 event);
u32 aie_get_error_count(struct aie_partition *apart);
ssize_t aie_sysfs_get_errors(struct aie_partition *apart,
			     struct aie_location *loc, char *buffer,
			     ssize_t size);
ssize_t aie_tile_show_error(struct device *dev, struct device_attribute *attr,
			    char *buffer);
ssize_t aie_aperture_show_hardware_info(struct device *dev,
					struct device_attribute *attr,
					char *buffer);
ssize_t aie_part_show_error_stat(struct device *dev,
				 struct device_attribute *attr, char *buffer);
ssize_t aie_part_show_current_freq(struct device *dev,
				   struct device_attribute *attr, char *buffer);
ssize_t aie_part_read_cb_error(struct kobject *kobj, char *buffer,
			       ssize_t size);
ssize_t aie_tile_show_event(struct device *dev, struct device_attribute *attr,
			    char *buffer);
void aie_read_event_status(struct aie_partition *apart,
			   struct aie_location *loc,
			   enum aie_module_type module, u32 *reg);
ssize_t aie_part_read_cb_ucstatus(struct kobject *kobj, char *buffer,
				  ssize_t size);
ssize_t aie_part_read_cb_status(struct kobject *kobj, char *buffer,
				ssize_t size);
long aie_part_rscmgr_get_statistics(struct aie_partition *apart,
				    void __user *user_args);
int aie_part_set_column_clock_from_user(struct aie_partition *apart, struct aie_column_args *args);
int aie2ps_part_set_column_clock_from_user(struct aie_partition *apart,
					   struct aie_column_args *args);

int aie_overlay_register_notifier(void);
void aie_overlay_unregister_notifier(void);
u32 aie_get_core_pc(struct aie_partition *apart,
		    struct aie_location *loc);
u32 aie_get_core_lr(struct aie_partition *apart,
		    struct aie_location *loc);
u32 aie_get_core_sp(struct aie_partition *apart,
		    struct aie_location *loc);
int aie_dma_mem_alloc(struct aie_partition *apart, __kernel_size_t size);
int aie_dma_mem_free(int fd);
int aie_dma_begin_cpu_access(struct dma_buf *dmabuf,
			     enum dma_data_direction direction);
int aie_dma_end_cpu_access(struct dma_buf *dmabuf,
			   enum dma_data_direction direction);
int aie_part_pm_ops_create(struct aie_partition *apart);

int aie_part_pm_ops(struct aie_partition *apart, void *data, u32 type, struct aie_range range,
		    bool flush);
int aie_part_pm_ops_flush(struct aie_partition *apart);
int aie2ps_part_initialize(struct aie_partition *apart, struct aie_partition_init_args *args);
int aie2ps_part_teardown(struct aie_partition *apart);
int aie2ps_part_clear_context(struct aie_partition *apart);
int aie2ps_part_clean(struct aie_partition *apart);
int aie2ps_part_reset(struct aie_partition *apart);
int aie_part_maskpoll_register(struct aie_partition *apart, u32 offset, u32 data, u32 mask,
			       u32 timeout);
int aie_partition_uc_zeroize_mem(struct device *dev, struct aie_location *loc, u32 regval);
int aie_error_handling_init(struct aie_partition *apart);

#endif /* AIE_INTERNAL_H */
