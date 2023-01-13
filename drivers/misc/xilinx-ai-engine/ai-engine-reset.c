// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver resets implementation
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include <linux/bitfield.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/io.h>

#include "ai-engine-internal.h"

/**
 * aie_part_clear_core_regs_of_tile() - clear registers of aie core
 * @apart: AI engine partition
 * @loc: location of aie tile to clear
 */
static void aie_part_clear_core_regs_of_tile(struct aie_partition *apart,
					     struct aie_location loc)
{
	struct aie_device *adev = apart->adev;
	struct aie_aperture *aperture = apart->aperture;
	const struct aie_core_regs_attr *regs = adev->core_regs;
	u32 i;

	for (i = 0; i < adev->num_core_regs; i++) {
		u32 j, soff, eoff, reg;

		soff = aie_cal_regoff(adev, loc, regs[i].core_regs->soff);
		eoff = aie_cal_regoff(adev, loc, regs[i].core_regs->eoff);

		for (reg = soff; reg <= eoff; reg += AIE_CORE_REGS_STEP) {
			for (j = 0; j < regs[i].width; j++)
				iowrite32(0, aperture->base + reg + j * 4);
		}
	}
}

/**
 * aie_part_clear_core_regs - clear registers of aie core of a partition
 * @apart: AI engine partition
 */
static void aie_part_clear_core_regs(struct aie_partition *apart)
{
	struct aie_range *range = &apart->range;
	u32 c, r;

	/* clear core registers for each tile in the partition */
	for (c = range->start.col; c < range->start.col + range->size.col;
			c++) {
		for (r = range->start.row;
				r < range->start.row + range->size.row; r++) {
			struct aie_location loc;
			u32 ttype;

			loc.row = r;
			loc.col = c;
			ttype = apart->adev->ops->get_tile_type(apart->adev,
								&loc);
			if (ttype == AIE_TILE_TYPE_TILE &&
			    aie_part_check_clk_enable_loc(apart, &loc))
				aie_part_clear_core_regs_of_tile(apart, loc);
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
	u32 node_id = apart->adev->pm_node_id;
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

	if (aie_part_clear_data_mem(apart))
		dev_warn(&apart->dev, "failed to clear data memory.\n");

	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_SET_L2_CTRL_NPI_INTR);
exit:
	mutex_unlock(&apart->mlock);

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
	u32 node_id = apart->adev->pm_node_id;
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
	aie_part_clear_core_regs(apart);
	ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
				      apart->range.size.col,
				      XILINX_AIE_OPS_DIS_COL_CLK_BUFF);
	if (ret < 0)
		return ret;

	aie_resource_clear_all(&apart->cores_clk_state);

	return 0;
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
	u32 node_id = apart->adev->pm_node_id;
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

/**
 * aie_part_initialize() - AI engine partition initialization
 * @apart: AI engine partition
 * @user_args: User initialization options
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
int aie_part_initialize(struct aie_partition *apart, void __user *user_args)
{
	u32 node_id = apart->adev->pm_node_id;
	struct aie_partition_init_args args;
	struct aie_location *locs = NULL;
	int ret;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	/* Clear resources */
	aie_part_clear_cached_events(apart);
	aie_part_rscmgr_reset(apart);
	aie_resource_clear_all(&apart->tiles_inuse);
	aie_resource_clear_all(&apart->cores_clk_state);

	/* This operation will do first 4 steps of sequence */
	if (args.init_opts & AIE_PART_INIT_OPT_COLUMN_RST) {
		ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
					      apart->range.size.col,
					      XILINX_AIE_OPS_COL_RST);
		if (ret < 0)
			goto exit;
	}

	/* Reset Shims */
	if (args.init_opts & AIE_PART_INIT_OPT_SHIM_RST) {
		ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
					      apart->range.size.col,
					      XILINX_AIE_OPS_SHIM_RST);
		if (ret < 0)
			goto exit;
	}

	/* Setup AXIMM events */
	if (args.init_opts & AIE_PART_INIT_OPT_BLOCK_NOCAXIMMERR) {
		ret = zynqmp_pm_aie_operation(node_id, apart->range.start.col,
					      apart->range.size.col,
					      XILINX_AIE_OPS_ENB_AXI_MM_ERR_EVENT);
		if (ret < 0)
			goto exit;
	}

	/* Setup partition isolation */
	if (args.init_opts & AIE_PART_INIT_OPT_ISOLATE) {
		ret = aie_part_init_isolation(apart);
		if (ret < 0)
			goto exit;
	}

	/* Zeroize memory */
	if (args.init_opts & AIE_PART_INIT_OPT_ZEROIZEMEM) {
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
	if (args.num_tiles) {
		locs = kmalloc_array(args.num_tiles, sizeof(*locs),
				     GFP_KERNEL);
		if (!locs) {
			ret = -ENOMEM;
			goto exit;
		}

		if (copy_from_user(locs, (void __user *)args.locs,
				   args.num_tiles * sizeof(*locs))) {
			kfree(locs);
			ret = -EFAULT;
			goto exit;
		}
	}
	ret = aie_part_request_tiles(apart, args.num_tiles, locs);
	kfree(locs);

exit:
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
	u32 node_id = apart->adev->pm_node_id;
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
