/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Xilinx AI Engine driver internal header
 *
 * Copyright (C) 2020 - 2021 Xilinx, Inc.
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
#include <linux/fpga/fpga-bridge.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <uapi/linux/xlnx-ai-engine.h>

/*
 * Macros for AI engine tile type bitmasks
 */
enum aie_tile_type {
	AIE_TILE_TYPE_TILE,
	AIE_TILE_TYPE_SHIMPL,
	AIE_TILE_TYPE_SHIMNOC,
	AIE_TILE_TYPE_MAX
};

#define AIE_TILE_TYPE_MASK_TILE		BIT(AIE_TILE_TYPE_TILE)
#define AIE_TILE_TYPE_MASK_SHIMPL	BIT(AIE_TILE_TYPE_SHIMPL)
/* SHIM NOC tile includes SHIM PL and SHIM NOC modules */
#define AIE_TILE_TYPE_MASK_SHIMNOC	BIT(AIE_TILE_TYPE_SHIMNOC)

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

#define AIE_PART_STATUS_BRIDGE_DISABLED	0x1U

/* Silicon Engineering Sample(ES) revision ID */
#define VERSAL_ES1_REV_ID		0x0
#define VERSAL_ES2_REV_ID		0x1

#define AIE_NPI_ERROR_ID		BIT(1)

/* Macros relevant to interrupts */
#define AIE_INTR_L2_CTRL_MASK_WIDTH	32

/* Max number of modules per tile */
#define AIE_MAX_MODS_PER_TILE		2U

/* AIE core registers step size */
#define AIE_CORE_REGS_STEP		0x10

/*
 * Macros of AI engine module type index of a tile type
 * e.g.
 * id 0 of CORE tile is memory module, and 1 is core module
 * id 0 of SHIM tile is pl module, and 1 is noc module
 */
#define AIE_TILE_MOD_START		AIE_MEM_MOD
#define AIE_MOD_ID(T, M)		((M) - AIE_##T ## _MOD_START)
#define AIE_TILE_MEM_MOD_ID		AIE_MOD_ID(TILE, AIE_MEM_MOD)
#define AIE_TILE_CORE_MOD_ID		AIE_MOD_ID(TILE, AIE_CORE_MOD)
#define AIE_SHIMPL_MOD_START		AIE_PL_MOD
#define AIE_SHIMNOC_MOD_START		AIE_PL_MOD
#define AIE_SHIM_PL_MOD_ID		AIE_MOD_ID(SHIMPL, AIE_PL_MOD)
#define AIE_SHIM_NOC_MOD_ID		AIE_MOD_ID(SHIMNOC, AIE_NOC_MOD)

/* String delimiter to format sysfs data */
#define DELIMITER_LEVEL0 "|"
#define DELIMITER_LEVEL1 ", "
#define DELIMITER_LEVEL2 "; "

/* Macros to define size of temporary string buffers */
#define AIE_SYSFS_CORE_STS_SIZE		100U
#define AIE_SYSFS_CHAN_STS_SIZE		150U
#define AIE_SYSFS_QUEUE_SIZE_SIZE	40U
#define AIE_SYSFS_QUEUE_STS_SIZE	60U
#define AIE_SYSFS_BD_SIZE		40U
#define AIE_SYSFS_ERROR_SIZE		300U
#define AIE_SYSFS_ERROR_CATEGORY_SIZE	500U
#define AIE_SYSFS_LOCK_STS_SIZE		400U
#define AIE_SYSFS_EVENT_STS_SIZE	550U

/* Helper macros to dynamically create sysfs device attribute */
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

/*
 * enum aie_shim_switch_type - identifies different switches in shim tile.
 */
enum aie_shim_switch_type {
	AIE_SHIM_SWITCH_A,
	AIE_SHIM_SWITCH_B
};

/**
 * struct aie_tile_regs - contiguous range of AI engine register
 *			  within an AI engine tile
 * @soff: start offset of the range
 * @eoff: end offset of the range
 * @attribute: registers attribute. It uses AIE_REGS_ATTR_* macros defined
 *	       above.
 */
struct aie_tile_regs {
	size_t soff;
	size_t eoff;
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
 * struct aie_dma_attr - AI engine DMA attributes structure
 * @laddr: low address field attributes
 * @haddr: high address field attributes
 * @buflen: buffer length field attributes
 * @sts: channel status field attributes
 * @stall: queue stall status field attributes
 * @qsize: queue size field attributes
 * @curbd: current buffer descriptor field attributes
 * @qsts: queue status field attributes
 * @bd_regoff: SHIM DMA buffer descriptors register offset
 * @mm2s_sts_regoff: MM2S status register offset
 * @s2mm_sts_regoff: S2MM status register offset
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
	struct aie_single_reg_field stall;
	struct aie_single_reg_field qsize;
	struct aie_single_reg_field curbd;
	struct aie_single_reg_field qsts;
	u32 bd_regoff;
	u32 mm2s_sts_regoff;
	u32 s2mm_sts_regoff;
	u32 num_mm2s_chan;
	u32 num_s2mm_chan;
	u32 num_bds;
	u32 bd_len;
};

/**
 * struct aie_core_regs_attr - AI engine core register attributes structure
 * @core_regs: core registers
 * @width: number of 32 bit words
 */
struct aie_core_regs_attr {
	const struct aie_tile_regs *core_regs;
	u32 width;
};

/**
 * struct aie_tile_operations - AI engine device operations
 * @get_tile_type: get type of tile based on tile operation
 * @get_mem_info: get different types of memories information
 * @get_core_status: get the status of AIE core.
 * @reset_shim: reset shim, it will assert and then release SHIM reset
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
 *
 * Different AI engine device version has its own device
 * operation.
 */
struct aie_tile_operations {
	u32 (*get_tile_type)(struct aie_location *loc);
	unsigned int (*get_mem_info)(struct aie_range *range,
				     struct aie_part_mem *pmem);
	u32 (*get_core_status)(struct aie_partition *apart,
			       struct aie_location *loc);
	int (*reset_shim)(struct aie_device *adev, struct aie_range *range);
	int (*init_part_clk_state)(struct aie_partition *apart);
	int (*scan_part_clocks)(struct aie_partition *apart);
	int (*set_part_clocks)(struct aie_partition *apart);
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

/**
 * struct aie_event_attr - AI Engine event attributes structure.
 * @bc_event: broadcast event attribute to capture event mask value and
 *	      register offset from @bc_regoff.
 * @group_error: group error attribute to capture error group mask value and
 *		 register offset value from @group_regoff.
 * @bc_regoff: base broadcast register offset.
 * @status_regoff: base status register offset.
 * @group_regoff: base group error register offset.
 * @base_error_event: event ID of first error event in a group error.
 * @num_broadcasts: total number of broadcast events.
 * @base_bc_event: broadcast 0 vent ID
 * @num_events: total number of events.
 */
struct aie_event_attr {
	struct aie_single_reg_field bc_event;
	struct aie_single_reg_field group_error;
	u32 bc_regoff;
	u32 status_regoff;
	u32 group_regoff;
	u32 base_error_event;
	u32 num_broadcasts;
	u32 base_bc_event;
	u32 num_events;
};

/**
 * struct aie_l1_intr_ctrl_attr - AI engine level 1 interrupt controller
 *				  attributes structure.
 * @mask: level 1 interrupt controller mask attribute.
 * @swa_status: switch A level 1 interrupt controller status attribute.
 * @swb_status: switch A level 1 interrupt controller status attribute.
 * @swa_event: switch A level 1 interrupt controller event attribute.
 * @swb_event: switch A level 1 interrupt controller event attribute.
 * @regoff: base level 1 interrupt controller register offset.
 * @event_lsb: lsb of IRQ event within IRQ event switch register.
 * @num_broadcasts: total number of broadcast signals to level 1 interrupt
 *		    controller.
 */
struct aie_l1_intr_ctrl_attr {
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
 * @regoff: level 2 interrupt controller register offset.
 * @num_broadcasts: total number of broadcast signals to level 2 interrupt
 *		    controller.
 */
struct aie_l2_intr_ctrl_attr {
	struct aie_single_reg_field mask;
	struct aie_single_reg_field enable;
	struct aie_single_reg_field disable;
	struct aie_single_reg_field status;
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
 */
struct aie_lock_attr {
	struct aie_single_reg_field sts;
	u32 sts_regoff;
	u32 num_locks;
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
 */
struct aie_tile {
	struct aie_location loc;
	struct aie_partition *apart;
	struct device dev;
	struct attribute_group *attr_grp;
};

/**
 * struct aie_device - AI engine device structure
 * @partitions: list of partitions requested
 * @cdev: cdev for the AI engine
 * @dev: device for the AI engine device
 * @mlock: protection for AI engine device operations
 * @base: AI engine device base virtual address
 * @clk: AI enigne device clock
 * @res: memory resource of AI engine device
 * @kernel_regs: array of kernel only registers
 * @core_regs: array of core registers
 * @ops: tile operations
 * @col_rst: column reset attribute
 * @col_clkbuf: column clock buffer attribute
 * @shim_dma: SHIM DMA attribute
 * @tile_dma: tile DMA attribute
 * @pl_events: pl module event attribute
 * @mem_events: memory module event attribute
 * @core_events: core module event attribute
 * @l1_ctrl: level 1 interrupt controller attribute
 * @l2_ctrl: level 2 interrupt controller attribute
 * @core_errors: core module error attribute
 * @mem_errors: memory module error attribute
 * @shim_errors: shim tile error attribute
 * @size: size of the AI engine address space
 * @array_shift: array address shift
 * @col_shift: column address shift
 * @row_shift: row address shift
 * @cols_res: AI engine columns resources to indicate
 *	      while columns are occupied by partitions.
 * @num_kernel_regs: number of kernel only registers range
 * @num_core_regs: number of core registers range
 * @irq: Linux IRQ number
 * @backtrack: workqueue to backtrack interrupt
 * @version: AI engine device version
 * @pm_node_id: AI Engine platform management node ID
 * @clock_id: AI Engine clock ID
 * @ttype_attr: tile type attributes
 * @part_sysfs_attr: partition level sysfs attributes
 * @tile_sysfs_attr: tile level sysfs attributes
 * @core_status_str: core status in string format
 * @core_pc: program counter attribute
 * @core_lr: link register attribute
 * @core_sp: stack pointer attribute
 * @dma_status_str: DMA channel status in string format
 * @queue_status_str: DMA queue status in string format
 * @pl_lock: PL module lock attribute
 * @mem_lock: memory module lock attribute
 * @lock_status_str: lock status in string format
 */
struct aie_device {
	struct list_head partitions;
	struct cdev cdev;
	struct device dev;
	struct mutex mlock; /* protection for AI engine partitions */
	void __iomem *base;
	struct clk *clk;
	struct resource *res;
	const struct aie_tile_regs *kernel_regs;
	const struct aie_core_regs_attr *core_regs;
	const struct aie_tile_operations *ops;
	const struct aie_single_reg_field *col_rst;
	const struct aie_single_reg_field *col_clkbuf;
	const struct aie_dma_attr *shim_dma;
	const struct aie_dma_attr *tile_dma;
	const struct aie_event_attr *pl_events;
	const struct aie_event_attr *mem_events;
	const struct aie_event_attr *core_events;
	const struct aie_l1_intr_ctrl_attr *l1_ctrl;
	const struct aie_l2_intr_ctrl_attr *l2_ctrl;
	const struct aie_error_attr *core_errors;
	const struct aie_error_attr *mem_errors;
	const struct aie_error_attr *shim_errors;
	size_t size;
	struct aie_resource cols_res;
	u32 array_shift;
	u32 col_shift;
	u32 row_shift;
	u32 num_kernel_regs;
	u32 num_core_regs;
	int irq;
	struct work_struct backtrack;
	int version;
	u32 pm_node_id;
	u32 clock_id;
	struct aie_tile_attr ttype_attr[AIE_TILE_TYPE_MAX];
	const struct aie_sysfs_attr *part_sysfs_attr;
	const struct aie_sysfs_attr *tile_sysfs_attr;
	char **core_status_str;
	const struct aie_single_reg_field *core_pc;
	const struct aie_single_reg_field *core_lr;
	const struct aie_single_reg_field *core_sp;
	char **dma_status_str;
	char **queue_status_str;
	const struct aie_lock_attr *pl_lock;
	const struct aie_lock_attr *mem_lock;
	char **lock_status_str;
};

/**
 * struct aie_part_bridge - AI engine FPGA bridge
 * @name: name of the FPGA bridge
 * @br: pointer to FPGA bridge
 */
struct aie_part_bridge {
	char name[32];
	struct fpga_bridge *br;
};

/**
 * struct aie_partition - AI engine partition structure
 * @node: list node
 * @dbufs: dmabufs list
 * @adev: pointer to AI device instance
 * @filep: pointer to file for refcount on the users of the partition
 * @pmems: pointer to partition memories types
 * @trscs: resources bitmaps for each tile
 * @freq_req: required frequency
 * @br: AI engine FPGA bridge
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
 * @l2_mask: level 2 interrupt controller mask bitmap
 * @partition_id: partition id. Partition ID is the identifier
 *		  of the AI engine partition in the system.
 * @status: indicate if the partition is in use
 * @cntrflag: partition control flag. e.g. whether to reset columns when
 *	      the partition is released
 * @error_to_report: indicates if there are errors pending to be reported to
 *		     the application. This value is set to true if errors are
 *		     found during backtracking, and error interrupt was
 *		     received when partition was not requested yet.
 */
struct aie_partition {
	struct list_head node;
	struct list_head dbufs;
	struct aie_part_bridge br;
	struct aie_device *adev;
	struct file *filep;
	struct aie_part_mem *pmems;
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
	struct aie_resource l2_mask;
	struct attribute_group *attr_grp;
	u32 partition_id;
	u32 status;
	u32 cntrflag;
	u8 error_to_report;
};

/**
 * struct aie_part_pinned_region - AI engine user space pinned region
 * @user_addr: user space address
 * @len: length of the user space buffer in bytes
 * @npages: number of pages of the user space buffer
 * @pages: array to receive pointers to the pages pinned.
 *	   should be at least npages long
 */
struct aie_part_pinned_region {
	u64 user_addr;
	u64 len;
	struct page **pages;
	int npages;
};

extern struct class *aie_class;
extern const struct file_operations aie_part_fops;

#define cdev_to_aiedev(i_cdev) container_of((i_cdev), struct aie_device, cdev)
#define dev_to_aiedev(_dev) container_of((_dev), struct aie_device, dev)
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
 * aie_validate_location() - validate tile location within an AI engine
 *			     partition
 * @apart: AI engine partition
 * @loc: AI engine tile location
 * @return: return 0 if it is valid, negative value for errors.
 *
 * This function checks if the AI engine location is within the AI engine
 * partition.
 */
static inline int aie_validate_location(struct aie_partition *apart,
					struct aie_location loc)
{
	if (loc.col < apart->range.start.col ||
	    loc.col >= apart->range.start.col + apart->range.size.col ||
	    loc.row < apart->range.start.row ||
	    loc.row >= apart->range.start.row + apart->range.size.row)
		return -EINVAL;

	return 0;
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
struct aie_partition *of_aie_part_probe(struct aie_device *adev,
					struct device_node *nc);
void aie_part_remove(struct aie_partition *apart);
int aie_part_clean(struct aie_partition *apart);

int aie_fpga_create_bridge(struct aie_partition *apart);
void aie_fpga_free_bridge(struct aie_partition *apart);

int aie_mem_get_info(struct aie_partition *apart, unsigned long arg);

long aie_part_attach_dmabuf_req(struct aie_partition *apart,
				void __user *user_args);
long aie_part_detach_dmabuf_req(struct aie_partition *apart,
				void __user *user_args);
long aie_part_set_bd(struct aie_partition *apart, void __user *user_args);
long aie_part_set_dmabuf_bd(struct aie_partition *apart,
			    void __user *user_args);
void aie_part_release_dmabufs(struct aie_partition *apart);

int aie_part_scan_clk_state(struct aie_partition *apart);
bool aie_part_check_clk_enable_loc(struct aie_partition *apart,
				   struct aie_location *loc);
int aie_part_set_freq(struct aie_partition *apart, u64 freq);
int aie_part_get_running_freq(struct aie_partition *apart, u64 *freq);

int aie_part_request_tiles_from_user(struct aie_partition *apart,
				     void __user *user_args);
int aie_part_release_tiles_from_user(struct aie_partition *apart,
				     void __user *user_args);
int aie_device_init(struct aie_device *adev);

void aie_array_backtrack(struct work_struct *work);
irqreturn_t aie_interrupt(int irq, void *data);
void aie_part_clear_cached_events(struct aie_partition *apart);
int aie_part_set_intr_rscs(struct aie_partition *apart);

bool aie_part_has_mem_mmapped(struct aie_partition *apart);
bool aie_part_has_regs_mmapped(struct aie_partition *apart);

int aie_part_get_tile_rows(struct aie_partition *apart,
			   enum aie_tile_type ttype);

int aie_part_reset(struct aie_partition *apart);
int aie_part_post_reinit(struct aie_partition *apart);

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

int aie_part_sysfs_create_entries(struct aie_partition *apart);
void aie_part_sysfs_remove_entries(struct aie_partition *apart);
int aie_tile_sysfs_create_entries(struct aie_tile *atile);
void aie_tile_sysfs_remove_entries(struct aie_tile *atile);
ssize_t aie_sysfs_read_handler(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *attr, char *buf,
			       loff_t offset, size_t max_size);

ssize_t aie_sysfs_get_core_status(struct aie_partition *apart,
				  struct aie_location *loc, char *buffer,
				  ssize_t size);
ssize_t aie_tile_show_core(struct device *dev, struct device_attribute *attr,
			   char *buffer);
ssize_t aie_part_read_cb_core(struct kobject *kobj, char *buffer, ssize_t size);
ssize_t aie_sysfs_get_dma_status(struct aie_partition *apart,
				 struct aie_location *loc, char *buffer,
				 ssize_t size);
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
bool aie_check_error_bitmap(struct aie_partition *apart,
			    struct aie_location loc,
			    enum aie_module_type module, u8 event);
u32 aie_get_error_count(struct aie_partition *apart);
ssize_t aie_sysfs_get_errors(struct aie_partition *apart,
			     struct aie_location *loc, char *buffer,
			     ssize_t size);
ssize_t aie_tile_show_error(struct device *dev, struct device_attribute *attr,
			    char *buffer);
ssize_t aie_part_show_error_stat(struct device *dev,
				 struct device_attribute *attr, char *buffer);
ssize_t aie_part_read_cb_error(struct kobject *kobj, char *buffer,
			       ssize_t size);
ssize_t aie_tile_show_event(struct device *dev, struct device_attribute *attr,
			    char *buffer);
void aie_read_event_status(struct aie_partition *apart,
			   struct aie_location *loc,
			   enum aie_module_type module, u32 *reg);
ssize_t aie_part_read_cb_status(struct kobject *kobj, char *buffer,
				ssize_t size);
long aie_part_rscmgr_get_statistics(struct aie_partition *apart,
				    void __user *user_args);

#endif /* AIE_INTERNAL_H */
