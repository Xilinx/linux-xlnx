/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2020, Xilinx Inc.
 */

#ifndef _UAPI_AI_ENGINE_H_
#define _UAPI_AI_ENGINE_H_

#include <linux/ioctl.h>
#include <linux/types.h>

enum aie_reg_op {
	AIE_REG_WRITE,
};

/* AI engine partition is in use */
#define XAIE_PART_STATUS_INUSE		(1U << 0)
/* AI engine partition bridge is enabled */
#define XAIE_PART_STATUS_BRIDGE_ENABLED	(1U << 1)

/*
 * AI engine partition control flags
 */
/* Not reset when release AI engine partition */
#define XAIE_PART_NOT_RST_ON_RELEASE	0x00000001U

/**
 * struct aie_location - AIE location information
 * @col: column id
 * @row: row id
 */
struct aie_location {
	__u32 col;
	__u32 row;
};

/**
 * struct aie_range - AIE range information
 * @start: start tile location
 * @size: size of the range, number of columns and rows
 */
struct aie_range {
	struct aie_location start;
	struct aie_location size;
};

/**
 * struct aie_mem - AIE memory information
 * @range: range of tiles of the memory
 * @offset: register offset within a tile of the memory
 * @size: of a the memory in one tile
 * @fd: file descriptor of the memory
 */
struct aie_mem {
	struct aie_range range;
	size_t offset;
	size_t size;
	int fd;
};

/**
 * struct aie_mem_args - AIE memory enquiry arguments
 * @num_mems: number of "struct aie_mem" elements
 *	      e.g. two memory information elements, one for tile core memory,
 *	      and the other for tile data memory.
 * @mems: array of AI engine memory information elements
 */
struct aie_mem_args {
	unsigned int num_mems;
	struct aie_mem *mems;
};

/**
 * struct aie_reg_args - AIE access register arguments
 * @op: if this request is to read, write or poll register
 * @mask: mask for mask write, 0 for not mask write
 * @offset: offset of register in the AIE device
 * @val: value to write or get
 */
struct aie_reg_args {
	enum aie_reg_op op;
	__u32 mask;
	__u64 offset;
	__u32 val;
};

/**
 * struct aie_range_args - AIE range request arguments
 * @partition_id: partition id. It is used to identify the
 *		  AI engine partition in the system.
 * @uid: image identifier loaded on the AI engine partition
 * @range: range of AIE tiles
 * @status: indicate if the AI engine is in use.
 *	    0 means not in used, otherwise, in use.
 */
struct aie_range_args {
	__u32 partition_id;
	__u32 uid;
	struct aie_range range;
	__u32 status;
};

/**
 * struct aie_partition_query - AIE partition query arguments
 * @partition_cnt: number of defined partitions in the system
 * @partitions: buffer to store defined partitions information.
 */
struct aie_partition_query {
	struct aie_range_args *partitions;
	__u32 partition_cnt;
};

/**
 * struct aie_partition_req - AIE request partition arguments
 * @partition_id: partition node id. It is used to identify the AI engine
 *		  partition in the system.
 * @uid: image identifier loaded on the AI engine partition
 * @meta_data: meta data to indicate which resources used by application.
 * @flag: used for application to indicate particular driver requirements
 *	  application wants to have for the partition. e.g. do not clean
 *	  resource when closing the partition.
 */
struct aie_partition_req {
	__u32 partition_id;
	__u32 uid;
	__u64 meta_data;
	__u32 flag;
};

#define AIE_IOCTL_BASE 'A'

/* AI engine device IOCTL operations */
#define AIE_ENQUIRE_PART_IOCTL		_IOWR(AIE_IOCTL_BASE, 0x1, \
					      struct aie_partition_query)
#define AIE_REQUEST_PART_IOCTL		_IOR(AIE_IOCTL_BASE, 0x2, \
					     struct aie_partition_req)

/* AI engine partition IOCTL operations */
#define AIE_REG_IOCTL			_IOWR(AIE_IOCTL_BASE, 0x8, \
					      struct aie_reg_args)
/**
 * DOC: AIE_GET_MEM_IOCTL - enquire information of memories in the AI engine
 *			    partition
 * This ioctl is used to get the information of all the different types of
 * memories in the AI engine partition. Application can get the memories
 * information in two steps:
 * 1. passing 0 as @num_mems in struct aie_mem_args to enquire the number of
 *    different memories in the partition, the value will be returned in
 *    @num_mems.
 * 2. passing the number of memories in @num_mems and valid pointer as @mems of
 *    struct aie_mem_args to store the details information of different
 *    memories. The driver will create DMA buf for each type of memories, and
 *    will return the memory addressing information along with the DMA buf file
 *    descriptors in @mems.
 * After getting the memories information, user can use mmap() with the DMA buf
 * file descriptor to enable access the memories from userspace.
 */
#define AIE_GET_MEM_IOCTL		_IOWR(AIE_IOCTL_BASE, 0x9, \
					      struct aie_mem_args)
#endif
