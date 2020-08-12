/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * xlnx-ai-engine.h - Xilinx AI engine external interface
 *
 * Copyright (c) 2020, Xilinx Inc.
 */

#ifndef _XLNX_AI_ENGINE_H_
#define _XLNX_AI_ENGINE_H_

#if !IS_ENABLED(CONFIG_XILINX_AIE)
#include <linux/errno.h>
#endif
#include <uapi/linux/xlnx-ai-engine.h>

struct device;

#if IS_ENABLED(CONFIG_XILINX_AIE)
bool aie_partition_is_available(struct aie_partition_req *req);
struct device *aie_partition_request(struct aie_partition_req *req);
int aie_partition_get_fd(struct device *dev);
void aie_partition_release(struct device *dev);
#else
static inline bool aie_partition_is_available(struct aie_partition_req *req)
{
	return false;
}

static inline struct device *
aie_partition_request(struct aie_partition_req *req)
{
	return NULL;
}

static inline int aie_partition_get_fd(struct device *dev)
{
	return -EINVAL;
}

static inline void aie_partition_release(struct device *dev) {}
#endif /* CONFIG_XILINX_AIE */
#endif
