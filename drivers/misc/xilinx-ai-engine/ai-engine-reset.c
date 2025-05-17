// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver resets implementation
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include <linux/bitfield.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/io.h>
#include <linux/xlnx-ai-engine.h>

#include "ai-engine-internal.h"

#include "ai-engine-trace.h"

static void aie_part_core_regs_clr_iowrite(struct aie_partition *apart,
					   u32 addr, u32 width)
{
	const struct aie_device *adev = apart->adev;
	void __iomem *base = apart->aperture->base;
	const struct aie_range *range = &apart->range;
	struct aie_location loc;
	u32 start_col = range->start.col;
	u32 start_row = range->start.row;
	u32 num_col = range->start.col + range->size.col;
	u32 num_row = range->start.row + range->size.row;
	u32 ttype;
	u32 (*get_tile_type)(struct aie_device *adev,
			     struct aie_location *loc);

	get_tile_type = apart->adev->ops->get_tile_type;

	for (loc.row = start_row; loc.row < num_row; loc.row++) {
		u32 addr_row = loc.row << adev->row_shift;

		for (loc.col = start_col; loc.col < num_col; loc.col++) {
			u32 addr_col = loc.col << adev->col_shift;

			ttype = get_tile_type(apart->adev, &loc);
			if (ttype == AIE_TILE_TYPE_TILE &&
			    aie_part_check_clk_enable_loc(apart, &loc)) {
				void __iomem *io_addr = base + (addr | addr_col |
							addr_row);
				/* This clears a set of registers to 0 during
				 * cleanup. No need to preserve order.
				 * Use relaxed IO.
				 */
				switch (width) {
				case 1:	/* 8 bits */
				case 2: /* 16 bits */
				case 4: /* 32 bits */
					writel_relaxed(0, io_addr);
					break;
				case 8: /* 64 bits */
					writeq_relaxed(0, io_addr);
					break;
				default:
					dev_warn(&apart->dev, "[%d, %d]: Unknown width: %d",
						 loc.col, loc.row, width);
					break;
				};
			}
		}
	}
}

static void aie_part_core_regs_clr_memset_io(struct aie_partition *apart,
					     u32 addr, u32 size)
{
	const struct aie_device *adev = apart->adev;
	void __iomem *base = apart->aperture->base + addr;
	const struct aie_range *range = &apart->range;
	struct aie_location loc;
	u32 start_col = range->start.col;
	u32 start_row = range->start.row;
	u32 num_col = range->start.col + range->size.col;
	u32 num_row = range->start.row + range->size.row;
	u32 ttype;
	u32 (*get_tile_type)(struct aie_device *adev,
			     struct aie_location *loc);

	get_tile_type = apart->adev->ops->get_tile_type;

	for (loc.row = start_row; loc.row < num_row; loc.row++) {
		u32 addr_row = loc.row << adev->row_shift;

		for (loc.col = start_col; loc.col < num_col; loc.col++) {
			u32 addr_col = loc.col << adev->col_shift;

			ttype = get_tile_type(apart->adev, &loc);
			if (ttype == AIE_TILE_TYPE_TILE &&
			    aie_part_check_clk_enable_loc(apart, &loc)) {
				memset_io(base + (addr_col | addr_row), 0, size);
			}
		}
	}
}

static void aie_part_core_regs_clr(struct aie_partition *apart)
{
	int i;
	const struct aie_device *adev = apart->adev;
	const struct aie_tile_regs *reg = adev->core_regs_clr;

	for (i = 0; i < adev->num_core_regs_clr; i++) {
		if (reg[i].width == reg[i].step &&
		    reg[i].soff != reg[i].eoff) {
			u32 addr = reg[i].soff;
			u32 size = reg[i].eoff + reg[i].width - reg[i].soff;

			aie_part_core_regs_clr_memset_io(apart, addr, size);
		} else {
			u32 addr;
			u32 eoff = reg[i].eoff;
			u32 step = reg[i].step;

			for (addr = reg[i].soff; addr <= eoff; addr += step) {
				aie_part_core_regs_clr_iowrite(apart, addr,
							       reg[i].width);
			}
		}
	}
}

/**
 * aie_part_clear_data_mem() - clear data memory of every tile in a partition
 * @apart: AI engine partition
 * @return: 0 for success and negative value for failure
 */
static int aie_part_clear_data_mem(struct aie_partition *apart)
{
	struct aie_device *adev = apart->adev;
	struct aie_part_mem *pmems = apart->pmems;
	struct aie_mem *mem;
	struct aie_range *range;
	u32 num_mems, c, r;

	/* Check if memory object is present */
	num_mems = adev->ops->get_mem_info(adev, &apart->range, NULL);
	if (!num_mems)
		return -EINVAL;

	/* Clear data memory in the partition */
	mem = &pmems[0].mem;
	range = &mem->range;

	for (c = range->start.col;
		c < range->start.col + range->size.col; c++) {
		for (r = range->start.row;
			r < range->start.row + range->size.row; r++) {
			struct aie_location loc;
			u32 memoff;

			loc.col = c;
			loc.row = r;
			memoff = aie_cal_regoff(adev, loc, mem->offset);
			memset_io(apart->aperture->base + memoff, 0, mem->size);
		}
	}

	return 0;
}

/**
 * aie2ps_part_clear_context() - clear AI engine partition context
 * @apart: AI engine partition
 * @return: 0 for success and negative value for failure
 *
 * This function will:
 * - Gate all columns
 * - Reset AI engine partition columns
 * - Ungate all columns
 * - Reset shim tiles
 * - Setup axi mm to raise events
 * - Setup partition isolation
 * - Zeroize data memory
 */
int aie2ps_part_clear_context(struct aie_partition *apart)
{
	u32 opts;
	u16 data;
	int ret;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	opts = AIE_PART_INIT_OPT_COLUMN_RST |
	       AIE_PART_INIT_OPT_SHIM_RST |
	       AIE_PART_INIT_OPT_BLOCK_NOCAXIMMERR |
	       AIE_PART_INIT_OPT_ZEROIZEMEM;
	ret = aie_part_pm_ops(apart, NULL, opts, apart->range, 1);
	if (ret)
		goto out;

	data = 0x6;
	ret = aie_part_pm_ops(apart, &data, AIE_PART_INIT_OPT_UC_ZEROIZATION, apart->range, 1);
	if (ret)
		goto out;
	ret = aie_part_pm_ops_flush(apart);
	if (ret)
		goto out;

	aie_part_init_isolation(apart);
out:
	mutex_unlock(&apart->mlock);

	return ret;
}

/**
 * aie_part_clear_context() - clear AI engine partition context
 * @apart: AI engine partition
 * @return: 0 for success and negative value for failure
 *
 * This function will:
 * - Gate all columns
 * - Reset AI engine partition columns
 * - Ungate all columns
 * - Reset shim tiles
 * - Setup axi mm to raise events
 * - Setup partition isolation
 * - Zeroize data memory
 * - Setup L2 intrupt
 */
int aie_part_clear_context(struct aie_partition *apart)
{
	u32 node_id = apart->aperture->node_id;
	int ret;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_COL_RST);
	if (ret < 0)
		goto exit;

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_SHIM_RST);
	if (ret < 0)
		goto exit;

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_ENB_AXI_MM_ERR_EVENT);
	if (ret < 0)
		goto exit;

	ret = aie_part_init_isolation(apart);
	if (ret < 0)
		goto exit;

	ret = zynqmp_pm_feature(PM_IOCTL);
	if ((ret < 0) || ((ret >= 0) &&
			((ret & FIRMWARE_VERSION_MASK) < PM_API_VERSION_3))) {
		if (aie_part_clear_data_mem(apart))
			dev_warn(&apart->dev, "failed to clear data memory.\n");
	} else {
		ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
					apart->range.size.col,
					XILINX_AIE_OPS_DATA_MEM_ZEROIZATION |
					XILINX_AIE_OPS_MEM_TILE_ZEROIZATION);
		if (ret < 0)
			goto exit;
	}

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
					apart->range.size.col,
					XILINX_AIE_OPS_SET_L2_CTRL_NPI_INTR);
	aie_part_core_regs_clr(apart);

exit:
	mutex_unlock(&apart->mlock);

	return ret;
}

/**
 * aie2ps_part_clean() - reset and clear AI engine partition
 * @apart: AI engine partition
 * @return: 0 for success and negative value for failure
 *
 * This function will:
 *  * gate all the columns
 *  * reset AI engine partition columns
 *  * reset AI engine shims
 *  * clear the memories
 *  * clear core registers
 *  * gate all the tiles in a partition
 *  * update clock state bitmap
 *
 * This function will not validate the partition, the caller will need to
 * provide a valid AI engine partition.
 */
int aie2ps_part_clean(struct aie_partition *apart)
{
	u32 opts;
	int ret;

	if (apart->cntrflag & XAIE_PART_NOT_RST_ON_RELEASE)
		return 0;

	ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_DIS_COLCLK_BUFF, apart->range, 1);
	if (ret)
		goto out;

	opts = AIE_PART_INIT_OPT_COLUMN_RST | AIE_PART_INIT_OPT_SHIM_RST;
	ret = aie_part_pm_ops(apart, NULL, opts, apart->range, 1);
	if (ret)
		goto out;

	ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_ENB_COLCLK_BUFF, apart->range, 1);
	if (ret)
		goto out;

	apart->adev->ops->mem_clear(apart);
	aie_part_core_regs_clr(apart);

	ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_DIS_COLCLK_BUFF, apart->range, 1);
	if (ret)
		goto out;

	aie_resource_clear_all(&apart->cores_clk_state);

out:
	return ret;
}

/**
 * aie_part_clean() - reset and clear AI engine partition
 * @apart: AI engine partition
 * @return: 0 for success and negative value for failure
 *
 * This function will:
 *  * gate all the columns
 *  * reset AI engine partition columns
 *  * reset AI engine shims
 *  * clear the memories
 *  * clear core registers
 *  * gate all the tiles in a partition
 *  * update clock state bitmap
 *
 * This function will not validate the partition, the caller will need to
 * provide a valid AI engine partition.
 */
int aie_part_clean(struct aie_partition *apart)
{
	u32 node_id = apart->aperture->node_id;
	int ret;

	if (apart->cntrflag & XAIE_PART_NOT_RST_ON_RELEASE)
		return 0;

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_DIS_COL_CLK_BUFF);
	if (ret < 0)
		return ret;

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_COL_RST |
				      XILINX_AIE_OPS_SHIM_RST);
	if (ret < 0)
		return ret;

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_ENB_COL_CLK_BUFF);
	if (ret < 0)
		return ret;

	apart->adev->ops->mem_clear(apart);
	aie_part_core_regs_clr(apart);
	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_DIS_COL_CLK_BUFF);
	if (ret < 0)
		return ret;

	aie_resource_clear_all(&apart->cores_clk_state);

	return 0;
}

/**
 * aie2ps_part_reset() - reset AI engine partition
 * @apart: AI engine partition
 * @return: 0 for success and negative value for failure
 *
 * This function will:
 * - gate all the columns
 * - reset AI engine partition columns
 * - ungate all the columns
 * - reset AI engine shims
 * - gate all the tiles in a partition.
 *
 * This function will not validate the partition, the caller will need to
 * provide a valid AI engine partition.
 */
int aie2ps_part_reset(struct aie_partition *apart)
{
	int ret;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	/*
	 * Check if any AI engine memories or registers in the
	 * partition have been mapped. If yes, don't reset.
	 */
	if (aie_part_has_mem_mmapped(apart) ||
	    aie_part_has_regs_mmapped(apart)) {
		dev_err(&apart->dev,
			"failed to reset, there are mmapped memories or registers.\n");
		mutex_unlock(&apart->mlock);
		return -EBUSY;
	}

	/* Clear tiles in use bitmap and clock state bitmap */
	aie_resource_clear_all(&apart->tiles_inuse);
	aie_resource_clear_all(&apart->cores_clk_state);

	ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_DIS_COLCLK_BUFF, apart->range, 1);
	if (ret < 0)
		goto exit;

	ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_COLUMN_RST, apart->range, 1);
	if (ret < 0)
		goto exit;

	ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_ENB_COLCLK_BUFF, apart->range, 1);
	if (ret < 0)
		goto exit;

	ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_SHIM_RST, apart->range, 1);
	if (ret < 0)
		goto exit;

	ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_DIS_COLCLK_BUFF, apart->range, 1);
	if (ret < 0)
		goto exit;

	aie_part_clear_cached_events(apart);
	aie_part_rscmgr_reset(apart);

exit:
	mutex_unlock(&apart->mlock);

	return ret;
}

/**
 * aie_part_reset() - reset AI engine partition
 * @apart: AI engine partition
 * @return: 0 for success and negative value for failure
 *
 * This function will:
 * - gate all the columns
 * - reset AI engine partition columns
 * - ungate all the columns
 * - reset AI engine shims
 * - gate all the tiles in a partition.
 *
 * This function will not validate the partition, the caller will need to
 * provide a valid AI engine partition.
 */
int aie_part_reset(struct aie_partition *apart)
{
	u32 node_id = apart->aperture->node_id;
	int ret;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	/*
	 * Check if any AI engine memories or registers in the
	 * partition have been mapped. If yes, don't reset.
	 */
	if (aie_part_has_mem_mmapped(apart) ||
	    aie_part_has_regs_mmapped(apart)) {
		dev_err(&apart->dev,
			"failed to reset, there are mmapped memories or registers.\n");
		mutex_unlock(&apart->mlock);
		return -EBUSY;
	}

	/* Clear tiles in use bitmap and clock state bitmap */
	aie_resource_clear_all(&apart->tiles_inuse);
	aie_resource_clear_all(&apart->cores_clk_state);

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_DIS_COL_CLK_BUFF);
	if (ret < 0)
		goto exit;

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_COL_RST);
	if (ret < 0)
		goto exit;

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_ENB_COL_CLK_BUFF);
	if (ret < 0)
		goto exit;

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_SHIM_RST);
	if (ret < 0)
		goto exit;

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_DIS_COL_CLK_BUFF);
	if (ret < 0)
		goto exit;

	aie_part_clear_cached_events(apart);
	aie_part_rscmgr_reset(apart);

exit:
	mutex_unlock(&apart->mlock);

	return ret;
}

/**
 * aie_partition_uc_wakeup() - wakes the uc cores of the partition.
 * @dev: AI engine partition device
 * @loc: Location of the tile
 * @return: return 0 if success negative value for failure.
 */
int aie_partition_uc_wakeup(struct device *dev, struct aie_location *loc)
{
	struct aie_partition *apart;
	struct aie_device *adev;
	int ret;

	if (!dev || !loc)
		return -EINVAL;

	apart = dev_to_aiepart(dev);
	if (!apart)
		return -EINVAL;

	adev = apart->adev;
	if (!adev->ops->wake_tile_uc_core_up)
		return -EINVAL;
	ret = adev->ops->wake_tile_uc_core_up(apart, loc);
	if (ret < 0)
		dev_err(&apart->dev,
			"failed to wake uc core up!\n");
	return ret;
}
EXPORT_SYMBOL_GPL(aie_partition_uc_wakeup);

/**
 * aie_part_post_reinit() - AI engine partition has been re-initialized
 * @apart: AI engine partition
 * @return: 0 for success and negative value for failure
 *
 * This function will:
 * - scan which tiles are gated
 * - update memories and registers mapping
 *
 * This function will scan which tiles are gated, and update the memories and
 * registers setting. This function is called after the AI engine partition is
 * reconfigured with PDI outside the AI engine driver.
 */
int aie_part_post_reinit(struct aie_partition *apart)
{
	int ret;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	ret = aie_part_scan_clk_state(apart);
	mutex_unlock(&apart->mlock);
	if (ret) {
		dev_err(&apart->dev,
			"failed to scan clock states after reset is done.\n");
		return ret;
	}

	return 0;
}

/**
 * aie_part_init_isolation() - Set isolation boundary of AI engine partition
 * @apart: AI engine partition
 * @return: return 0 if success negative value for failure.
 */
int aie_part_init_isolation(struct aie_partition *apart)
{
	struct aie_range *range = &apart->range;
	int ret;
	u32 c, r;
	u8 dir;

	for (c = range->start.col;
	     c < range->start.col + range->size.col; c++) {
		if (c == range->start.col)
			dir = AIE_ISOLATE_WEST_MASK;
		else if (c == (range->start.col + range->size.col - 1))
			dir = AIE_ISOLATE_EAST_MASK;
		else
			dir = 0;

		for (r = range->start.row;
		     r < range->start.row + range->size.row; r++) {
			struct aie_location loc;

			loc.col = c;
			loc.row = r;
			ret = apart->adev->ops->set_tile_isolation(apart, &loc,
								   dir);
			if (ret < 0) {
				dev_err(&apart->dev,
					"failed to set partition isolation\n");
				return ret;
			}
		}
	}
	return ret;
}

int aie_partition_teardown(struct device *dev)
{
	struct aie_partition *apart;

	if (!dev)
		return -EINVAL;

	apart = dev_to_aiepart(dev);
	if (apart->adev->ops->part_teardown)
		return apart->adev->ops->part_teardown(apart);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(aie_partition_teardown);

int aie_partition_initialize(struct device *dev, struct aie_partition_init_args *args)
{
	struct aie_partition *apart;

	if (!dev || !args)
		return -EINVAL;

	apart = dev_to_aiepart(dev);

	if (apart->adev->ops->part_init)
		return apart->adev->ops->part_init(apart, args);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(aie_partition_initialize);

static int aie2ps_part_aximm_isolation(struct aie_partition *apart)
{
	struct aie_range range = {};
	int ret;
	u16 dir;

	range.start.col = apart->range.start.col;
	range.size.col = 1;
	dir = AIE_ISOLATE_WEST_MASK;
	ret = aie_part_pm_ops(apart, &dir, AIE_PART_INIT_OPT_ISOLATE, range, 0);
	if (ret)
		return ret;

	range.start.col = apart->range.size.col - 1;
	dir = AIE_ISOLATE_EAST_MASK;
	ret = aie_part_pm_ops(apart, &dir, AIE_PART_INIT_OPT_ISOLATE, range, 0);

	return ret;
}

static int aie2ps_part_set_l2_irq(struct aie_partition *apart)
{
	struct aie_range range = {};
	int ret;
	u16 irq;

	/* Partition size needs to be at least 4 for aie2ps */
	if (apart->range.size.col < 4)
		return -EINVAL;

	range.start.col = apart->range.start.col;
	range.size.col = 1;
	irq = 1;
	ret = aie_part_pm_ops(apart, &irq, AIE_PART_INIT_OPT_SET_L2_IRQ, range, 0);
	if (ret)
		return ret;

	range.start.col = apart->range.start.col + 1;
	range.size.col = 1;
	irq = (apart->partition_id % AIE_USER_EVENT1_NUM_IRQ) + 2;
	ret = aie_part_pm_ops(apart, &irq, AIE_PART_INIT_OPT_SET_L2_IRQ, range, 0);
	if (ret)
		return ret;

	range.start.col = apart->range.start.col + 2;
	range.size.col = apart->range.size.col - 2;
	irq = 1;
	ret = aie_part_pm_ops(apart, &irq, AIE_PART_INIT_OPT_SET_L2_IRQ, range, 0);
	if (ret)
		return ret;

	return ret;
}

int aie2ps_part_initialize(struct aie_partition *apart, struct aie_partition_init_args *args)
{
	u32 opts;
	int ret;
	int i;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;
	trace_aie_part_initialize(apart, args->init_opts, args->num_tiles);

	/* Clear resources */
	aie_part_clear_cached_events(apart);
	aie_part_rscmgr_reset(apart);
	aie_resource_clear_all(&apart->tiles_inuse);
	aie_resource_clear_all(&apart->cores_clk_state);

	/* First lets send non-data aie_op_type_len ops */
	opts = 0;
	/* This operation will do first 4 steps of sequence */
	opts |= (args->init_opts & AIE_PART_INIT_OPT_COLUMN_RST);
	opts |= (args->init_opts & AIE_PART_INIT_OPT_SHIM_RST);
	opts |= (args->init_opts & AIE_PART_INIT_OPT_BLOCK_NOCAXIMMERR);
	opts |= (args->init_opts & AIE_PART_INIT_OPT_ENB_COLCLK_BUFF);
	/* push opts to pm and flush, these ops needs to complete before we perform next ones*/
	ret = aie_part_pm_ops(apart, NULL, opts, apart->range, 1);
	if (ret) {
		dev_err(&apart->dev, "pm ops: 0x%x failed: %d", opts, ret);
		goto out;
	}

	if (args->init_opts & AIE_PART_INIT_OPT_ISOLATE) {
		opts |= AIE_PART_INIT_OPT_ISOLATE;
		ret = aie_part_init_isolation(apart);
		if (ret)
			goto out;
		ret = aie2ps_part_aximm_isolation(apart);
		if (ret)
			goto out;
	}

	if (args->init_opts & AIE_PART_INIT_OPT_ZEROIZEMEM) {
		opts |= AIE_PART_INIT_OPT_ZEROIZEMEM;
		ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_ZEROIZEMEM, apart->range, 1);
		if (ret)
			goto out;
	}

	if (args->init_opts & AIE_PART_INIT_OPT_UC_ZEROIZATION) {
		u16 data = 0x6;

		opts |= AIE_PART_INIT_OPT_UC_ZEROIZATION;
		ret = aie_part_pm_ops(apart, &data, AIE_PART_INIT_OPT_UC_ZEROIZATION,
				      apart->range, 1);
		if (ret)
			goto out;
	}

	if (args->init_opts & AIE_PART_INIT_OPT_SET_L2_IRQ) {
		opts |= AIE_PART_INIT_OPT_SET_L2_IRQ;

		ret = aie2ps_part_set_l2_irq(apart);
		if (ret)
			goto out;
	}

	if (args->init_opts & AIE_PART_INIT_OPT_NMU_CONFIG) {
		struct aie_range range = {0};

		opts |= ~AIE_PART_INIT_OPT_NMU_CONFIG;
		if (apart->range.start.col == 0) {
			range.size.col = 2;
			ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_NMU_CONFIG, range, 0);
			if (ret)
				goto out;
		}
	}

	if (args->init_opts & AIE_PART_INIT_OPT_HW_ERR_INT) {
		u16 data = 0;

		opts |= AIE_PART_INIT_OPT_HW_ERR_INT;
		ret = aie_part_pm_ops(apart, &data, AIE_PART_INIT_OPT_HW_ERR_INT, apart->range, 0);
		if (ret)
			goto out;
	}

	if (args->init_opts & AIE_PART_INIT_OPT_HW_ERR_MASK) {
		u16 data = 0x2;

		opts |= AIE_PART_INIT_OPT_HW_ERR_MASK;
		ret = aie_part_pm_ops(apart, &data, AIE_PART_INIT_OPT_HW_ERR_MASK, apart->range, 0);
		if (ret)
			goto out;
	}

	if (args->init_opts & AIE_PART_INIT_OPT_SET_ECC_SCRUB_PERIOD) {
		opts |= AIE_PART_INIT_OPT_SET_ECC_SCRUB_PERIOD;

		ret = aie_part_pm_ops(apart, &args->ecc_scrub,
				      AIE_PART_INIT_OPT_SET_ECC_SCRUB_PERIOD, apart->range, 0);
		if (ret)
			goto out;
	}

	if (args->init_opts & AIE_PART_INIT_OPT_UC_ENB_MEM_PRIV) {
		opts |= AIE_PART_INIT_OPT_UC_ENB_MEM_PRIV;

		ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_UC_ENB_MEM_PRIV, apart->range,
				      0);
		if (ret)
			goto out;
	}

	/* Request tile locations */
	if (args->num_tiles && trace_aie_part_initialize_tiles_enabled()) {
		for (i = 0; i < args->num_tiles; i++)
			trace_aie_part_initialize_tiles(apart, args->locs[i]);
	}

	ret = aie_part_request_tiles(apart, args->num_tiles, args->locs);
	if (ret)
		goto out;

	if (args->init_opts & AIE_PART_INIT_OPT_HANDSHAKE) {
		struct aie_op_handshake_data data = {
			.addr = args->handshake,
			.size = args->handshake_size
		};

		opts |= AIE_PART_INIT_OPT_HANDSHAKE;
		ret = aie_part_pm_ops(apart, &data, AIE_PART_INIT_OPT_HANDSHAKE, apart->range, 1);
		if (ret)
			goto out;
	}

	if (args->init_opts & AIE_PART_INIT_ERROR_HANDLING) {
		opts |= AIE_PART_INIT_ERROR_HANDLING;
		ret = aie_error_handling_init(apart);
		if (ret)
			goto out;
	}

	if (opts)
		dev_warn(&apart->dev, "Invalid init_opts: 0x%x", opts);

out:
	mutex_unlock(&apart->mlock);
	return ret;
}

/**
 * aie_part_initialize() - AI engine partition initialization
 * @apart: AI engine partition
 * @args: User initialization options
 * @return: 0 for success and negative value for failure
 *
 * This function will:
 * - gate all columns
 * - enable column reset
 * - ungate all columns
 * - disable column reset
 * - reset shim tiles
 * - setup axi mm to raise events
 * - setup partition isolation
 * - zeroize memory
 */
int aie_part_initialize(struct aie_partition *apart, struct aie_partition_init_args *args)
{
	u32 node_id = apart->aperture->node_id;
	int ret;
	int i;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;
	trace_aie_part_initialize(apart, args->init_opts, args->num_tiles);
	/* Clear resources */
	aie_part_clear_cached_events(apart);
	aie_part_rscmgr_reset(apart);
	aie_resource_clear_all(&apart->tiles_inuse);
	aie_resource_clear_all(&apart->cores_clk_state);

	/* This operation will do first 4 steps of sequence */
	if (args->init_opts & AIE_PART_INIT_OPT_COLUMN_RST) {
		ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
					      apart->range.size.col,
					      XILINX_AIE_OPS_COL_RST);
		if (ret < 0)
			goto exit;
	}

	/* Reset Shims */
	if (args->init_opts & AIE_PART_INIT_OPT_SHIM_RST) {
		ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
					      apart->range.size.col,
					      XILINX_AIE_OPS_SHIM_RST);
		if (ret < 0)
			goto exit;
	}

	/* Setup AXIMM events */
	if (args->init_opts & AIE_PART_INIT_OPT_BLOCK_NOCAXIMMERR) {
		ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
					      apart->range.size.col,
					      XILINX_AIE_OPS_ENB_AXI_MM_ERR_EVENT);
		if (ret < 0)
			goto exit;
	}

	/* Setup partition isolation */
	if (args->init_opts & AIE_PART_INIT_OPT_ISOLATE) {
		ret = aie_part_init_isolation(apart);
		if (ret < 0)
			goto exit;
	}

	/* Zeroize memory */
	if (args->init_opts & AIE_PART_INIT_OPT_ZEROIZEMEM) {
		ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
					      apart->range.size.col,
					      XILINX_AIE_OPS_ZEROISATION);
		if (ret < 0)
			goto exit;
	}

	/* Set L2 interrupt */
	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_SET_L2_CTRL_NPI_INTR);
	if (ret < 0)
		goto exit;

	/* Request tile locations */
	if (args->num_tiles && trace_aie_part_initialize_tiles_enabled()) {
		for (i = 0; i < args->num_tiles; i++)
			trace_aie_part_initialize_tiles(apart, args->locs[i]);

	}
	ret = aie_part_request_tiles(apart, args->num_tiles, args->locs);

exit:
	mutex_unlock(&apart->mlock);
	return ret;
}

static int aie_part_maskpoll_uc_outstanding_aximm_txn(struct aie_partition *apart)
{
	u32 end_col = apart->range.start.col + apart->range.size.col;
	struct aie_device *adev = apart->adev;
	struct aie_location loc = {.row = 0};
	int ret = 0;
	u32 regoff;

	if (!adev->uc_outstanding_aximm)
		return -EINVAL;
	for (loc.col = apart->range.start.col; loc.col < end_col; loc.col++) {
		regoff = aie_aperture_cal_regoff(apart->aperture, loc,
						 adev->uc_outstanding_aximm->regoff);
		ret = aie_part_maskpoll_register(apart, regoff, 0x0,
						 adev->uc_outstanding_aximm->mask, 10000);
		if (ret < 0) {
			dev_err(&apart->dev, "failed due to outstanding UC AXIMM transactions!\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int aie_part_maskpoll_noc_outstanding_aximm_txn(struct aie_partition *apart)
{
	u32 end_col = apart->range.start.col + apart->range.size.col;
	struct aie_device *adev = apart->adev;
	struct aie_location loc = {.row = 0};
	int ret = 0;
	u32 regoff;

	if (!adev->uc_outstanding_aximm)
		return -EINVAL;
	for (loc.col = apart->range.start.col; loc.col < end_col; loc.col++) {
		regoff = aie_aperture_cal_regoff(apart->aperture, loc,
						 adev->noc_outstanding_aximm->regoff);
		ret = aie_part_maskpoll_register(apart, regoff, 0x0,
						 adev->noc_outstanding_aximm->mask, 10000);
		if (ret < 0) {
			dev_err(&apart->dev, "failed due to outstanding NoC AXIMM transactions!\n");
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * aie2ps_part_teardown() - AI engine partition teardown
 * @apart: AI engine partition
 * @return: 0 for success and negative value for failure
 *
 * This function will:
 * - gate all columns
 * - enable column reset
 * - ungate all columns
 * - disable column reset
 * - reset shim tiles
 * - zeroize memory
 * - gate all columns
 */
int aie2ps_part_teardown(struct aie_partition *apart)
{
	u32 opts;
	u16 data;
	int ret;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_ENB_NOC_DMA_PAUSE, apart->range, 0);
	if (ret)
		goto out;
	ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_ENB_UC_DMA_PAUSE, apart->range, 1);
	if (ret)
		goto out;

	ret = aie_part_maskpoll_noc_outstanding_aximm_txn(apart);
	if (ret)
		goto out;

	ret = aie_part_maskpoll_uc_outstanding_aximm_txn(apart);
	if (ret)
		goto out;

	opts = AIE_PART_INIT_OPT_COLUMN_RST |
	      AIE_PART_INIT_OPT_SHIM_RST |
	      AIE_PART_INIT_OPT_ZEROIZEMEM;
	ret = aie_part_pm_ops(apart, NULL, opts, apart->range, 0);
	if (ret)
		goto out;

	/* Zeroizes the uc-DM and uc shared DM */
	data = 0x6;
	ret = aie_part_pm_ops(apart, &data, AIE_PART_INIT_OPT_UC_ZEROIZATION, apart->range, 1);
	if (ret)
		goto out;
	ret = aie_part_pm_ops(apart, NULL, AIE_PART_INIT_OPT_DIS_COLCLK_BUFF, apart->range, 0);
	if (ret)
		goto out;
	ret = aie_part_pm_ops_flush(apart);
	if (ret)
		goto out;

	/* Clear resources */
	aie_resource_clear_all(&apart->tiles_inuse);
	aie_resource_clear_all(&apart->cores_clk_state);
	aie_part_clear_cached_events(apart);
	aie_part_rscmgr_reset(apart);

out:
	mutex_unlock(&apart->mlock);

	return ret;
}

/**
 * aie_part_teardown() - AI engine partition teardown
 * @apart: AI engine partition
 * @return: 0 for success and negative value for failure
 *
 * This function will:
 * - gate all columns
 * - enable column reset
 * - ungate all columns
 * - disable column reset
 * - reset shim tiles
 * - zeroize memory
 * - gate all columns
 */
int aie_part_teardown(struct aie_partition *apart)
{
	u32 node_id = apart->aperture->node_id;
	int ret;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	/* This operation will do first 4 steps of sequence */
	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_COL_RST);
	if (ret < 0)
		goto exit;

	/* Reset shims */
	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_SHIM_RST);
	if (ret < 0)
		goto exit;

	/* Zeroize mem */
	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_ZEROISATION);
	if (ret < 0)
		goto exit;

	/* Gate all columns */
	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_DIS_COL_CLK_BUFF);
	if (ret < 0)
		goto exit;

	/* Clear resources */
	aie_resource_clear_all(&apart->tiles_inuse);
	aie_resource_clear_all(&apart->cores_clk_state);
	aie_part_clear_cached_events(apart);
	aie_part_rscmgr_reset(apart);
exit:
	mutex_unlock(&apart->mlock);
	return ret;
}
