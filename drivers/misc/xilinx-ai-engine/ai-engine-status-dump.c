// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine Status Dump.
 *
 * Copyright (C) 2023 AMD, Inc.
 */
#include "ai-engine-internal.h"
#include "linux/xlnx-ai-engine.h"

/*
 * Version Number to maintain Tile and Column Structure change
 * between Linux Driver and Application
 */
#define MAJOR_VERSION 1
#define MINOR_VERSION 1

/**
 * aie_tile_core_status() - Stores AI engine core status, value of program
 *                        counter, stack pointer, and link register to a tile
 *                        column structure.
 * @apart: AI engine Partition.
 * @status: Pointer to Structure which store status value of
 *		Tile core registers
 * @loc: Location of AI Engine Tile
 * @return: 0 for success, negative value for failure
 */
static int aie_tile_core_status(struct aie_partition *apart,
				struct aie_col_status *status,
				struct aie_location *loc)
{
	u8 tile_st;
	u32 ttype;
	int ret;

	if (apart->adev->dev_gen != AIE_DEVICE_GEN_AIEML) {
		dev_warn(&apart->dev, "Skipping Tile core status For Non AIEML Devices\n");
		return 0;
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dev_err(&apart->dev,
			"Failed to acquire lock Process was interrupted by fatal signals\n");
		return ret;
	}

	tile_st = apart->adev->ttype_attr[AIE_TILE_TYPE_TILE].start_row;
	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);

	if (ttype == AIE_TILE_TYPE_TILE) {
		if (aie_part_check_clk_enable_loc(apart, loc)) {
			status[loc->col].core_tile[loc->row - tile_st].prg_cntr =
				aie_get_core_pc(apart, loc);
			status[loc->col].core_tile[loc->row - tile_st].link_reg =
				aie_get_core_lr(apart, loc);
			status[loc->col].core_tile[loc->row - tile_st].stack_ptr =
				aie_get_core_sp(apart, loc);
			status[loc->col].core_tile[loc->row - tile_st].core_status =
				apart->adev->ops->get_core_status(apart, loc);
		}
	}

	mutex_unlock(&apart->mlock);

	return 0;
}

/**
 * aie_dma_status() - Stores AI engine DMA status values to a tile
 *                        column structure.
 * @apart: AI engine Partition.
 * @status: Pointer to Structure which store status value of Tile DMA registers
 * @loc: Location of AI Engine Tile
 * @return: 0 for success, negative value for failure
 */
static int aie_dma_status(struct aie_partition *apart, struct aie_col_status *status,
			  struct aie_location *loc)
{
	u32 ttype, i, num_s2mm_chan, num_mm2s_chan, status_val, value;
	struct aie_core_tile_status *sts_coretile;
	struct aie_shim_tile_status *sts_shimtile;
	struct aie_mem_tile_status *sts_memtile;
	u8 tile_st, memtile_st, index;
	int ret;

	if (apart->adev->dev_gen != AIE_DEVICE_GEN_AIEML) {
		dev_warn(&apart->dev, "Skipping DMA Status For Non AIEML Devices\n");
		return 0;
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dev_err(&apart->dev,
			"Failed to acquire lock Process was interrupted by fatal signals\n");
		return ret;
	}

	tile_st = apart->adev->ttype_attr[AIE_TILE_TYPE_TILE].start_row;
	memtile_st = apart->adev->ttype_attr[AIE_TILE_TYPE_MEMORY].start_row;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE) {
		num_mm2s_chan = apart->adev->tile_dma->num_mm2s_chan;
		num_s2mm_chan = apart->adev->tile_dma->num_s2mm_chan;
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		num_mm2s_chan = apart->adev->memtile_dma->num_mm2s_chan;
		num_s2mm_chan = apart->adev->memtile_dma->num_s2mm_chan;
	} else {
		num_mm2s_chan = apart->adev->shim_dma->num_mm2s_chan;
		num_s2mm_chan = apart->adev->shim_dma->num_s2mm_chan;
	}

	if (aie_part_check_clk_enable_loc(apart, loc)) {
		const struct aie_tile_operations *ops;

		ops = apart->adev->ops;
		sts_coretile = status[loc->col].core_tile;
		sts_memtile = status[loc->col].mem_tile;
		sts_shimtile = status[loc->col].shim_tile;

		for (i = 0; i < num_s2mm_chan; i++) {
			status_val = ops->get_dma_s2mm_status(apart, loc, i);
			value = ops->get_chan_status(apart, loc, status_val);

			if (ttype == AIE_TILE_TYPE_TILE) {
				index = loc->row - tile_st;
				sts_coretile[index].dma[i].s2mm_sts = value;
			} else if (ttype == AIE_TILE_TYPE_MEMORY) {
				index = loc->row - memtile_st;
				sts_memtile[index].dma[i].s2mm_sts = value;
			} else {
				sts_shimtile[loc->row].dma[i].s2mm_sts = value;
			}
		}

		for (i = 0; i < num_mm2s_chan; i++) {
			status_val = ops->get_dma_mm2s_status(apart, loc, i);
			value = ops->get_chan_status(apart, loc, status_val);

			if (ttype == AIE_TILE_TYPE_TILE) {
				index = loc->row - tile_st;
				sts_coretile[index].dma[i].mm2s_sts = value;
			} else if (ttype == AIE_TILE_TYPE_MEMORY) {
				index = loc->row - memtile_st;
				sts_memtile[index].dma[i].mm2s_sts = value;
			} else {
				sts_shimtile[loc->row].dma[i].mm2s_sts = value;
			}
		}
	}
	mutex_unlock(&apart->mlock);

	return 0;
}

/**
 * aie_lock_status() - Stores AI engine Tile Lock status values to a tile
 *                        column structure.
 * @apart: AI engine Partition.
 * @status: Pointer to Structure which store status value of Tile Lock registers
 * @loc: Location of AI Engine Tile
 * @return: 0 for success, negative value for failure
 */
static int aie_lock_status(struct aie_partition *apart, struct aie_col_status *status,
			   struct aie_location *loc)
{
	struct aie_core_tile_status *sts_coretile;
	struct aie_shim_tile_status *sts_shimtile;
	struct aie_mem_tile_status *sts_memtile;
	u8 i, tile_st, memtile_st, index;
	u32 ttype, num_locks, lock_val;
	int ret;

	if (apart->adev->dev_gen != AIE_DEVICE_GEN_AIEML) {
		dev_warn(&apart->dev, "Skipping Lock Status For Non AIEML Devices\n");
		return 0;
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dev_err(&apart->dev, "Failed to acquire lock Process was interrupted by fatal signals\n");
		return ret;
	}

	tile_st = apart->adev->ttype_attr[AIE_TILE_TYPE_TILE].start_row;
	memtile_st = apart->adev->ttype_attr[AIE_TILE_TYPE_MEMORY].start_row;

	loc->row = 0;
	loc->col = 0;
	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);

	if (aie_part_check_clk_enable_loc(apart, loc)) {
		const struct aie_tile_operations *ops;

		ops = apart->adev->ops;
		sts_coretile = status[loc->col].core_tile;
		sts_memtile = status[loc->col].mem_tile;
		sts_shimtile = status[loc->col].shim_tile;

		if (ttype == AIE_TILE_TYPE_TILE)
			num_locks = apart->adev->mem_lock->num_locks;
		else if (ttype == AIE_TILE_TYPE_MEMORY)
			num_locks = apart->adev->memtile_lock->num_locks;
		else
			num_locks = apart->adev->pl_lock->num_locks;

		for (i = 0; i < num_locks; i++) {
			lock_val = ops->get_lock_status(apart, loc, i);

			if (ttype == AIE_TILE_TYPE_TILE) {
				index = loc->row - tile_st;
				sts_coretile[index].lock_value[i] = lock_val;
			} else if (ttype == AIE_TILE_TYPE_MEMORY) {
				index = loc->row - memtile_st;
				sts_memtile[index].lock_value[i] = lock_val;
			} else {
				sts_shimtile[loc->row].lock_value[i] = lock_val;
			}
		}
	}
	mutex_unlock(&apart->mlock);

	return 0;
}

/**
 * aie_event_status() - Stores AI engine Tile Event status values to a tile
 *                        column structure.
 * @apart: AI engine partition.
 * @status: Pointer to Structure which store status value of Tile Lock registers
 * @loc: Location of AI Engine Tile
 * @return: 0 for success, negative value for failure
 */
static int aie_event_status(struct aie_partition *apart, struct aie_col_status *status,
			    struct aie_location *loc)
{
	struct aie_core_tile_status *sts_coretile;
	struct aie_shim_tile_status *sts_shimtile;
	struct aie_mem_tile_status *sts_memtile;
	u8 tile_st, memtile_st, index;
	u32 ttype;
	int ret;

	if (apart->adev->dev_gen != AIE_DEVICE_GEN_AIEML) {
		dev_warn(&apart->dev, "Skipping Event Status For Non AIEML Devices\n");
		return 0;
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dev_err(&apart->dev, "Failed to acquire lock Process was interrupted by fatal signals\n");
		return ret;
	}

	tile_st = apart->adev->ttype_attr[AIE_TILE_TYPE_TILE].start_row;
	memtile_st = apart->adev->ttype_attr[AIE_TILE_TYPE_MEMORY].start_row;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);
	if (aie_part_check_clk_enable_loc(apart, loc)) {
		sts_coretile = status[loc->col].core_tile;
		sts_memtile = status[loc->col].mem_tile;
		sts_shimtile = status[loc->col].shim_tile;

		if (ttype == AIE_TILE_TYPE_TILE) {
			index = loc->row - tile_st;
			aie_read_event_status(apart, loc, AIE_CORE_MOD,
					      sts_coretile[index].core_mode_event_sts);
			aie_read_event_status(apart, loc, AIE_MEM_MOD,
					      sts_coretile[index].mem_mode_event_sts);
		} else if (ttype == AIE_TILE_TYPE_MEMORY) {
			index = loc->row - memtile_st;
			aie_read_event_status(apart, loc, AIE_MEM_MOD,
					      sts_memtile[index].event_sts);
		} else {
			aie_read_event_status(apart, loc, AIE_PL_MOD,
					      sts_shimtile[loc->row].event_sts);
		}
	}
	mutex_unlock(&apart->mlock);

	return 0;
}

/**
 * aie_get_status_dump() - exports AI engine core status, value of program
 *                        counter, stack pointer, and link register to a tile
 *                        level sysfs node.
 * @dev: AI engine tile device.
 * @status: Pointer to Structure which stores status value of
 * Tile Core regisers and Tile DMA registers
 * @return: 0 for success, negative value for failure
 */
int aie_get_status_dump(struct device *dev, struct aie_col_status *status)
{
	struct aie_partition *apart = dev_to_aiepart(dev);
	struct aie_location loc;
	u32 row, col, ttype;
	int ret;

	if (!status)
		return -EFAULT;

	for (col = apart->range.start.col; col < apart->range.size.col; col++) {
		for (row = apart->range.start.row; row < apart->range.size.row; row++) {
			loc.row = row;
			loc.col = col;

			ttype = apart->adev->ops->get_tile_type(apart->adev, &loc);
			if (ttype == AIE_TILE_TYPE_SHIMPL)
				continue;
			/* Get Status of Core Tile and DMA */
			ret = aie_tile_core_status(apart, status, &loc);
			if (ret) {
				dev_err(dev, "aie_tile_core_status API Failed\n");
				return ret;
			}

			ret = aie_dma_status(apart, status, &loc);
			if (ret) {
				dev_err(dev, "aie_dma_status API Failed\n");
				return ret;
			}

			ret = aie_lock_status(apart, status, &loc);
			if (ret) {
				dev_err(dev, "aie_lock_status API Failed\n");
				return ret;
			}

			ret = aie_event_status(apart, status, &loc);
			if (ret) {
				dev_err(dev, "aie_event_status API Failed\n");
				return ret;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(aie_get_status_dump);

/**
 * aie_get_tile_info() - exports AI engine tile information
 * @dev: AI engine tile device.
 * @tile_info: Pointer to Structure which stores Tile information
 * @return: 0 for success, negative value for failure
 */
int aie_get_tile_info(struct device *dev, struct aie_tile_info *tile_info)
{
	struct aie_partition *apart = dev_to_aiepart(dev);

	if (!tile_info)
		return -EFAULT;

	tile_info->minor = MINOR_VERSION;
	tile_info->major = MAJOR_VERSION;
	tile_info->cols = apart->range.size.col;
	tile_info->rows = apart->range.size.row;
	tile_info->core_rows = apart->adev->ttype_attr[AIE_TILE_TYPE_TILE].num_rows;
	tile_info->mem_rows = apart->adev->ttype_attr[AIE_TILE_TYPE_MEMORY].num_rows;
	tile_info->shim_rows = apart->adev->ttype_attr[AIE_TILE_TYPE_SHIMPL].num_rows;
	tile_info->core_row_start = apart->adev->ttype_attr[AIE_TILE_TYPE_TILE].start_row;
	tile_info->mem_row_start = apart->adev->ttype_attr[AIE_TILE_TYPE_MEMORY].start_row;
	tile_info->shim_row_start = apart->adev->ttype_attr[AIE_TILE_TYPE_SHIMPL].start_row;
	tile_info->core_dma_channels = apart->adev->tile_dma->num_s2mm_chan;
	tile_info->shim_dma_channels = apart->adev->shim_dma->num_s2mm_chan;
	tile_info->mem_dma_channels = apart->adev->memtile_dma->num_mm2s_chan;
	tile_info->core_locks = apart->adev->mem_lock->num_locks;
	tile_info->mem_locks = apart->adev->memtile_lock->num_locks;
	tile_info->shim_locks = apart->adev->pl_lock->num_locks;
	tile_info->core_events = (apart->adev->core_events->num_events) / 32;
	tile_info->mem_events = (apart->adev->memtile_events->num_events) / 32;
	tile_info->shim_events = (apart->adev->pl_events->num_events) / 32;

	return 0;
}
EXPORT_SYMBOL_GPL(aie_get_tile_info);
