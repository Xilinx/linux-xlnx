/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Xilinx AI Engine driver internal header
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#ifndef AIE_INTERNAL_H
#define AIE_INTERNAL_H

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/fpga/fpga-bridge.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <uapi/linux/xlnx-ai-engine.h>

/*
 * Macros for AI engine tile type bitmasks
 */
#define AIE_TILE_TYPE_TILE	BIT(0)
#define AIE_TILE_TYPE_SHIMPL	BIT(1)
/* SHIM NOC tile includes SHIM PL and SHIM NOC modules */
#define AIE_TILE_TYPE_SHIMNOC	BIT(2)

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
	struct aie_mem mem;
	size_t size;
};

/**
 * struct aie_dma_attr - AI engine DMA attributes structure
 * @laddr: low address field attributes
 * @haddr: high address field attributes
 * @buflen: buffer length field attributes
 * @bd_regoff: SHIM DMA buffer descriptors register offset
 * @num_bds: number of buffer descriptors
 * @bd_len: length of a buffer descriptor in bytes
 */
struct aie_dma_attr {
	struct aie_single_reg_field laddr;
	struct aie_single_reg_field haddr;
	struct aie_single_reg_field buflen;
	u32 bd_regoff;
	u32 num_bds;
	u32 bd_len;
};

/**
 * struct aie_tile_operations - AI engine device operations
 * @get_tile_type: get type of tile based on tile operation
 * @get_mem_info: get different types of memories information
 * @reset_shim: reset shim, it will assert and then release SHIM reset
 *
 * Different AI engine device version has its own device
 * operation.
 */
struct aie_tile_operations {
	u32 (*get_tile_type)(struct aie_location *loc);
	unsigned int (*get_mem_info)(struct aie_range *range,
				     struct aie_part_mem *pmem);
	int (*reset_shim)(struct aie_device *adev, struct aie_range *range);
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
 * struct aie_device - AI engine device structure
 * @partitions: list of partitions requested
 * @cdev: cdev for the AI engine
 * @dev: device for the AI engine device
 * @mlock: protection for AI engine device operations
 * @base: AI engine device base virtual address
 * @res: memory resource of AI engine device
 * @kernel_regs: array of kernel only registers
 * @ops: tile operations
 * @col_rst: column reset attribute
 * @col_clkbuf: column clock buffer attribute
 * @shim_dma: SHIM DMA attribute
 * @size: size of the AI engine address space
 * @array_shift: array address shift
 * @col_shift: column address shift
 * @row_shift: row address shift
 * @cols_res: AI engine columns resources to indicate
 *	      while columns are occupied by partitions.
 * @num_kernel_regs: number of kernel only registers range
 * @version: AI engine device version
 */
struct aie_device {
	struct list_head partitions;
	struct cdev cdev;
	struct device dev;
	struct mutex mlock; /* protection for AI engine partitions */
	void __iomem *base;
	struct resource *res;
	const struct aie_tile_regs *kernel_regs;
	const struct aie_tile_operations *ops;
	const struct aie_single_reg_field *col_rst;
	const struct aie_single_reg_field *col_clkbuf;
	const struct aie_dma_attr *shim_dma;
	size_t size;
	struct aie_resource cols_res;
	u32 array_shift;
	u32 col_shift;
	u32 row_shift;
	u32 num_kernel_regs;
	int version;
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
 * @br: AI engine FPGA bridge
 * @range: range of partition
 * @mlock: protection for AI engine partition operations
 * @dev: device for the AI engine partition
 * @partition_id: partition id. Partition ID is the identifier
 *		  of the AI engine partition in the system.
 * @status: indicate if the partition is in use
 * @cntrflag: partition control flag. e.g. whether to reset columns when
 *	      the partition is released
 */
struct aie_partition {
	struct list_head node;
	struct list_head dbufs;
	struct aie_part_bridge br;
	struct aie_device *adev;
	struct file *filep;
	struct aie_part_mem *pmems;
	struct aie_range range;
	struct mutex mlock; /* protection for AI engine partition operations */
	struct device dev;
	u32 partition_id;
	u32 status;
	u32 cntrflag;
};

extern struct class *aie_class;
extern const struct file_operations aie_part_fops;

#define cdev_to_aiedev(i_cdev) container_of((i_cdev), struct aie_device, cdev)
#define dev_to_aiedev(_dev) container_of((_dev), struct aie_device, dev)
#define dev_to_aiepart(_dev) container_of((_dev), struct aie_partition, dev)

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
 * @return: return 0 if it it is valid, negative value for errors.
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

int aie_resource_initialize(struct aie_resource *res, int count);
void aie_resource_uninitialize(struct aie_resource *res);
int aie_resource_check_region(struct aie_resource *res, u32 start,
			      u32 count);
int aie_resource_get_region(struct aie_resource *res, u32 start,
			    u32 count);
void aie_resource_put_region(struct aie_resource *res, int start, u32 count);

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
void aie_part_release_dmabufs(struct aie_partition *apart);

int aiev1_device_init(struct aie_device *adev);
#endif /* AIE_INTERNAL_H */
