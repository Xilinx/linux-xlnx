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

#define AIE_IOCTL_BASE 'A'

/* AI engine device IOCTL operations */
#define AIE_ENQUIRE_PART_IOCTL		_IOWR(AIE_IOCTL_BASE, 0x1, \
					      struct aie_partition_query)
#define AIE_REQUEST_PART_IOCTL		_IOR(AIE_IOCTL_BASE, 0x2, __u32)

/* AI engine partition IOCTL operations */
#define AIE_REG_IOCTL			_IOWR(AIE_IOCTL_BASE, 0x8, \
					      struct aie_reg_args)
#endif
