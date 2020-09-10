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

/**
 * enum aie_module_type - identifies different hardware modules within a
 *			  tile type. AIE tile may have memory and core
 *			  module. While a PL or shim tile may have PL module.
 * @AIE_MEM_MOD: comprises of the following sub-modules,
 *			* data memory.
 *			* tile DMA.
 *			* lock module.
 *			* events, event broadcast and event actions.
 *			* tracing and profiling.
 * @AIE_CORE_MOD: comprises of the following sub-modules,
 *			* AIE core.
 *			* program Memory.
 *			* events, event broadcast and event actions.
 *			* tracing and profiling.
 *			* AXI-MM and AXI-S tile interconnects.
 * @AIE_PL_MOD: comprises of the following sub-modules,
 *			* PL interface.
 *			* AXI-MM and AXI-S tile interconnects.
 *			* Level 1 interrupt controllers.
 *			* events, event broadcast and event actions.
 *			* tracing and profiling.
 * @AIE_NOC_MOD: comprises of the following sub-modules,
 *			* interface from NoC Slave Unit (NSU)
 *			  (bridge to AXI-MM switch)
 *			* interfaces to NoC NoC Master Unit (NMU)
 *				* shim DMA & locks
 *				* NoC stream interface
 */
enum aie_module_type {
	AIE_MEM_MOD,
	AIE_CORE_MOD,
	AIE_PL_MOD,
	AIE_NOC_MOD,
};

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
