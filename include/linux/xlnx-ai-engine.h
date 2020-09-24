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

/*
 * Macro to classify errors into categories to provide higher-level error
 * event abstraction.
 */
#define AIE_ERROR_CATEGORY_SATURATION		0U
#define AIE_ERROR_CATEGORY_FP			1U
#define AIE_ERROR_CATEGORY_STREAM		2U
#define AIE_ERROR_CATEGORY_ACCESS		3U
#define AIE_ERROR_CATEGORY_BUS			4U
#define AIE_ERROR_CATEGORY_INSTRUCTION		5U
#define AIE_ERROR_CATEGORY_ECC			6U
#define AIE_ERROR_CATEGORY_LOCK			7U
#define AIE_ERROR_CATEGORY_DMA			8U
#define AIE_ERROR_CATEGORY_MEM_PARITY		9U

/* AIE error category bit mask */
#define AIE_ERROR_CATMASK(c)			BIT(AIE_ERROR_CATEGORY_##c)
#define AIE_ERROR_CATEGORY_MASK_SATURATION	AIE_ERROR_CATMASK(SATURATION)
#define AIE_ERROR_CATEGORY_MASK_FP		AIE_ERROR_CATMASK(FP)
#define AIE_ERROR_CATEGORY_MASK_STREAM		AIE_ERROR_CATMASK(STREAM)
#define AIE_ERROR_CATEGORY_MASK_ACCESS		AIE_ERROR_CATMASK(ACCESS)
#define AIE_ERROR_CATEGORY_MASK_BUS		AIE_ERROR_CATMASK(BUS)
#define AIE_ERROR_CATEGORY_MASK_INSTRUCTION	AIE_ERROR_CATMASK(INSTRUCTION)
#define AIE_ERROR_CATEGORY_MASK_ECC		AIE_ERROR_CATMASK(ECC)
#define AIE_ERROR_CATEGORY_MASK_LOCK		AIE_ERROR_CATMASK(LOCK)
#define AIE_ERROR_CATEGORY_MASK_DMA		AIE_ERROR_CATMASK(DMA)
#define AIE_ERROR_CATEGORY_MASK_MEM_PARITY	AIE_ERROR_CATMASK(MEM_PARITY)

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

/**
 * struct aie_error - AI engine error
 * @loc: AI engine tile location of which the error is from
 * @module: AI engine module type of which the error is from
 * @error_id: AI engine hardware event ID
 * @category: AI engine error category of the error
 */
struct aie_error {
	struct aie_location loc;
	enum aie_module_type module;
	u32 error_id;
	u32 category;
};

struct aie_errors {
	struct device *dev;
	struct aie_error *errors;
	u32 num_err;
};

#if IS_ENABLED(CONFIG_XILINX_AIE)
bool aie_partition_is_available(struct aie_partition_req *req);
struct device *aie_partition_request(struct aie_partition_req *req);
int aie_partition_get_fd(struct device *dev);
void aie_partition_release(struct device *dev);
int aie_partition_reset(struct device *dev);
int aie_partition_post_reinit(struct device *dev);

int aie_register_error_notification(struct device *dev,
				    void (*cb)(void *priv), void *priv);
int aie_unregister_error_notification(struct device *dev);
struct aie_errors *aie_get_errors(struct device *dev);
u32 aie_get_error_categories(struct aie_errors *aie_errs);
const char *aie_get_error_string(struct aie_errors *aie_errs,
				 struct aie_error *aie_err);
int aie_flush_errors(struct device *dev);
void aie_free_errors(struct aie_errors *aie_errs);

/**
 * aie_get_error_category() - Get the category of an AIE error
 * @err: AI engine hardware error
 * @return: category of the error
 */
static inline u32 aie_get_error_category(struct aie_error *err)
{
	return err->category;
}

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

static inline int aie_partition_reset(struct device *dev)
{
	return -EINVAL;
}

static inline int aie_partition_post_reinit(struct device *dev)
{
	return -EINVAL;
}

static inline int
aie_register_error_notification(struct device *dev, void (*cb)(void *priv),
				void *priv)
{
	return -EINVAL;
}

static inline int aie_unregister_error_notification(struct device *dev)
{
	return -EINVAL;
}

static inline struct aie_errors *aie_get_errors(struct device *dev)
{
	return NULL;
}

static inline u32 aie_get_error_categories(struct aie_errors *aie_errs)
{
	return 0;
}

static inline const char *aie_get_error_string(struct aie_errors *aie_errs,
					       struct aie_error *aie_err)
{
	return NULL;
}

static inline int aie_flush_errors(struct device *dev)
{
	return -EINVAL;
}

static inline void aie_free_errors(struct aie_errors *aie_errs) {}

static inline u32 aie_get_error_category(struct aie_error *err)
{
	return 0;
}
#endif /* CONFIG_XILINX_AIE */
#endif
