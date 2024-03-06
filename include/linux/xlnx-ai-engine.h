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

struct device;

/* Data structure to capture the Tile Information */
struct aie_tile_info {
	u32 col_size;
	u16 major;
	u16 minor;
	u16 cols;
	u16 rows;
	u16 core_rows;
	u16 mem_rows;
	u16 shim_rows;
	u16 core_row_start;
	u16 mem_row_start;
	u16 shim_row_start;
	u16 core_dma_channels;
	u16 mem_dma_channels;
	u16 shim_dma_channels;
	u16 core_locks;
	u16 mem_locks;
	u16 shim_locks;
	u16 core_events;
	u16 mem_events;
	u16 shim_events;
	u16 padding;
};

/* Data structure to capture the dma status */
struct aie_dma_status {
	u32 s2mm_sts;
	u32 mm2s_sts;
};

/* Data structure to capture the core tile status */
struct aie_core_tile_status {
	struct aie_dma_status *dma;
	u32 *core_mode_event_sts;
	u32 *mem_mode_event_sts;
	u32 core_status;
	u32 prg_cntr;
	u32 stack_ptr;
	u32 link_reg;
	u8 *lock_value;
};

/* Data structure to capture the mem tile status */
struct aie_mem_tile_status {
	struct aie_dma_status *dma;
	u32 *event_sts;
	u8 *lock_value;
};

/* Data structure to capture the shim tile status */
struct aie_shim_tile_status {
	struct aie_dma_status *dma;
	u32 *event_sts;
	u8 *lock_value;
};

/* Data structure to capture column status */
struct aie_col_status {
	struct aie_core_tile_status *core_tile;
	struct aie_mem_tile_status *mem_tile;
	struct aie_shim_tile_status *shim_tile;
};

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

int aie_partition_set_freq_req(struct device *dev, u64 freq);
int aie_partition_get_freq(struct device *dev, u64 *freq);
int aie_partition_get_freq_req(struct device *dev, u64 *freq);
int aie_part_rscmgr_set_static_range(struct device *dev,
				     u8 start_col, u8 num_col, void *meta);

int aie_get_status_dump(struct device *dev, struct aie_col_status *status);
int aie_get_tile_info(struct device *dev, struct aie_tile_info *tile_info);
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

static inline int aie_partition_set_freq_req(struct device *dev, u64 freq)
{
	return -EINVAL;
}

static inline int aie_partition_get_freq(struct device *dev, u64 *freq)
{
	return -EINVAL;
}

static inline int aie_partition_get_freq_req(struct device *dev, u64 *freq)
{
	return -EINVAL;
}

static inline int aie_get_status_dump(struct device *dev, struct aie_col_status *status)
{
	return -EINVAL;
}

static inline int aie_get_tile_info(struct device *dev, struct aie_tile_info *tile_info)
{
	return -EINVAL;
}

static inline int aie_part_rscmgr_set_static_range(struct device *dev,
						   u8 start_col, u8 num_col, void *meta)
{
	return -EINVAL;
}

#endif /* CONFIG_XILINX_AIE */
#endif
