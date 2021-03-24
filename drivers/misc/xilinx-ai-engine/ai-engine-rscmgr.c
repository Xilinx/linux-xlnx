// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine partition resource manager
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */

#include "ai-engine-internal.h"
#include <linux/slab.h>

/**
 * aie_dev_get_tile_attr - helper function to get tile attributes
 * @adev: AI engine device
 * @ttype: tile type
 * @return: attributes of an AI engine tile type
 */
static inline
struct aie_tile_attr *aie_dev_get_tile_attr(struct aie_device *adev,
					    enum aie_tile_type ttype)
{
	return &adev->ttype_attr[ttype];
}

/**
 * aie_dev_get_tile_rsc_attr - helper function to get resource attribute of a
 *			       tile type of an AI engine device
 * @adev: AI engine device
 * @ttype: tile type
 * @rtype: resource type
 * @return: attributes of an AI engine resource type of a tile type
 */
static inline const
struct aie_tile_rsc_attr *aie_dev_get_tile_rsc_attr(struct aie_device *adev,
						    enum aie_tile_type ttype,
						    enum aie_rsc_type rtype)
{
	return &adev->ttype_attr[ttype].rscs_attr[rtype];
}

/**
 * aie_dev_get_mod_id - helper function to get module id of a module of a tile
 *			type. The module ID can be used to indexing the resource
 *			attributes of a module type of a tile type or indexing
 *			resource status bitmaps.
 *
 * @adev: AI engine device
 * @ttype: tile type
 * @mod: module type
 * @return: module type index
 */
static int aie_dev_get_mod_id(struct aie_device *adev,
			      enum aie_tile_type ttype,
			      enum aie_module_type mod)
{
	struct aie_tile_attr *tattr = &adev->ttype_attr[ttype];

	int ret;

	if (ttype == AIE_TILE_TYPE_TILE)
		ret = AIE_MOD_ID(TILE, mod);
	else if (ttype == AIE_TILE_TYPE_SHIMPL)
		ret = AIE_MOD_ID(SHIMPL, mod);
	else
		ret = AIE_MOD_ID(SHIMNOC, mod);

	if (ret < 0 || ret > tattr->num_mods)
		return -EINVAL;

	return ret;
}

/**
 * aie_dev_get_mod_rsc_attr - helper function to get resource attribute of a
 *			      module of an AI engine device
 * @adev: AI engine device
 * @ttype: tile type
 * @mod: module type
 * @rtype: resource type
 * @return: module type resource attributes
 */
static const
struct aie_mod_rsc_attr *aie_dev_get_mod_rsc_attr(struct aie_device *adev,
						  enum aie_tile_type ttype,
						  enum aie_module_type mod,
						  enum aie_rsc_type rtype)
{
	const struct aie_tile_rsc_attr *rsc = aie_dev_get_tile_rsc_attr(adev,
									ttype,
									rtype);
	const struct aie_mod_rsc_attr *mrsc = NULL;
	int mod_id = aie_dev_get_mod_id(adev, ttype, mod);

	if (mod_id < 0)
		return NULL;

	mrsc = &rsc->mod_attr[mod_id];
	if (mrsc && !mrsc->num_rscs)
		return NULL;

	return mrsc;
}

/**
 * aie_part_get_rsc_bitmaps - helper function to get bitmap of a resource
 *
 * @apart: AI engine partition
 * @loc: tile location
 * @mod: module type
 * @rtype: resource type
 * @return: pointer to AI engine resource status bitmaps if resource is found,
 *	    otherwise NULL
 */
static
struct aie_rsc_stat *aie_part_get_rsc_bitmaps(struct aie_partition *apart,
					      struct aie_location loc,
					      enum aie_module_type mod,
					      enum aie_rsc_type rtype)
{
	u32 ttype = apart->adev->ops->get_tile_type(&loc);
	int mod_id = aie_dev_get_mod_id(apart->adev, ttype, mod);
	struct aie_mod_rscs *mrscs;

	if (mod_id < 0)
		return NULL;

	if (rtype >= AIE_RSCTYPE_MAX)
		return NULL;

	mrscs = apart->trscs[ttype].mod_rscs[rtype];
	if (!mrscs)
		return NULL;

	return mrscs[mod_id].rscs_stat;
}

/**
 * aie_part_get_mod_num_rscs - helper function to get number of resources
 *			       of a module of a tile
 *
 * @apart: AI engine partition
 * @loc: tile location
 * @mod: module type
 * @rtype: resource type
 * @return: number of max resources of a module of a tile
 */
static
int aie_part_get_mod_num_rscs(struct aie_partition *apart,
			      struct aie_location loc,
			      enum aie_module_type mod,
			      enum aie_rsc_type rtype)
{
	u32 ttype = apart->adev->ops->get_tile_type(&loc);
	const struct aie_mod_rsc_attr *mattr;

	mattr = aie_dev_get_mod_rsc_attr(apart->adev, ttype, mod, rtype);
	if (!mattr)
		return 0;

	return mattr->num_rscs;
}

/**
 * aie_part_get_rsc_startbit - helper function to get the start bit of a
 *			       resource of a module of a tile.
 *
 * @apart: AI engine partition
 * @loc: tile location
 * @mod: module type
 * @rtype: resource type
 * @return: pointer to AI engine resource status bitmaps if resource is found,
 *	    otherwise NULL
 *
 */
static
int aie_part_get_rsc_startbit(struct aie_partition *apart,
			      struct aie_location loc,
			      enum aie_module_type mod,
			      enum aie_rsc_type rtype)
{
	struct aie_device *adev = apart->adev;
	u32 ttype;
	const struct aie_mod_rsc_attr *mattr;
	int num_rows;
	struct aie_tile_attr *tattr;

	ttype = adev->ops->get_tile_type(&loc);

	mattr = aie_dev_get_mod_rsc_attr(adev, ttype, mod, rtype);
	if (!mattr)
		return -EINVAL;

	num_rows = aie_part_get_tile_rows(apart, ttype);
	tattr = &adev->ttype_attr[ttype];
	return mattr->num_rscs *
	       ((loc.col - apart->range.start.col) * num_rows +
		loc.row - tattr->start_row);
}

/**
 * aie_part_adjust_loc - adjust relative tile location to partition to
 *				absolute location in AI engine device
 * @apart: AI engine partition
 * @rloc: relative location in AI engine partition
 * @loc: returns absolute location in AI engine device
 * @return: 0 for success, negative value for failure
 */
static
int aie_part_adjust_loc(struct aie_partition *apart,
			struct aie_location rloc, struct aie_location *loc)
{
	loc->col = rloc.col + apart->range.start.col;
	loc->row = rloc.row + apart->range.start.row;

	if (aie_validate_location(apart, *loc) < 0) {
		dev_err(&apart->dev,
			"invalid loc (%u,%u) in (%u,%u).\n",
			rloc.col, rloc.row,
			apart->range.size.col, apart->range.size.row);
		return -EINVAL;
	}

	return 0;
}

/**
 * aie_part_rscmgr_init() - initialize AI engine partition resource status
 *			    bitmaps
 * @apart: AI engine partition
 * @return: 0 for success, negative value for failure.
 *
 * This function will create the hardware resources status bitmaps for the whole
 * partition.
 * Each partition contains an array of hardware resources status bitmaps of all
 * defined tiles types.:
 * aie_partition
 *   |- trscs[<all_tile_types>]
 *       |-mod_rscs[<all_resources_types>]
 *         |-rscs_stat - resources status bitmaps of a module type of a tile
 *			 type of the AI engine partition.
 */
int aie_part_rscmgr_init(struct aie_partition *apart)
{
	struct aie_device *adev = apart->adev;
	u32 t;

	for (t = AIE_TILE_TYPE_TILE; t < AIE_TILE_TYPE_MAX; t++) {
		struct aie_tile_rscs *trscs = &apart->trscs[t];
		struct aie_tile_attr *tattr;
		int num_rows, num_cols;
		u32 r;

		/*
		 * SHIMNOC tile resource bitmaps reuse the SHIMPL resource
		 * bitmaps. In future, for DMA resources, SHIMNOC tile will
		 * have DMA resources bitmap which will be the unique to
		 * SHIMNOC tiles.
		 */
		if (t == AIE_TILE_TYPE_SHIMNOC) {
			*trscs = apart->trscs[AIE_TILE_TYPE_SHIMPL];
			continue;
		}

		/*
		 * Get the number of rows of a tile type and the number
		 * of columns of the partition, which will be used to
		 * calculate the size of the bitmap is a resource.
		 */
		tattr = aie_dev_get_tile_attr(adev, t);
		num_rows = aie_part_get_tile_rows(apart, t);
		num_cols = apart->range.size.col;

		for (r = AIE_RSCTYPE_PERF; r < AIE_RSCTYPE_MAX; r++) {
			const struct aie_tile_rsc_attr *trsc_attr;
			struct aie_mod_rscs *mod_rscs;
			u32 m;

			trsc_attr = aie_dev_get_tile_rsc_attr(adev, t, r);
			if (!trsc_attr)
				continue;

			mod_rscs = devm_kcalloc(&apart->dev, tattr->num_mods,
						sizeof(*mod_rscs), GFP_KERNEL);
			if (!mod_rscs)
				return -ENOMEM;

			trscs->mod_rscs[r] = mod_rscs;
			for (m = 0 ; m < tattr->num_mods; m++) {
				struct aie_rsc_stat *rscs_stat;
				int num_mrscs = trsc_attr->mod_attr[m].num_rscs;
				int ret, total_rscs;

				/*
				 * if number of resources of this module type in
				 * this tile type is 0, skip allocating bitmap
				 * for the resource of this module type.
				 */
				if (!num_mrscs)
					continue;

				rscs_stat = devm_kzalloc(&apart->dev,
							 sizeof(*rscs_stat),
							 GFP_KERNEL);
				if (!rscs_stat)
					return -ENOMEM;

				mod_rscs[m].rscs_stat = rscs_stat;
				total_rscs = num_mrscs * num_rows * num_cols;
				/*
				 * initialize bitmaps for static resources and
				 * runtime allocated resources.
				 */
				ret = aie_resource_initialize(&rscs_stat->rbits,
							      total_rscs);
				if (ret)
					return ret;
				ret = aie_resource_initialize(&rscs_stat->sbits,
							      total_rscs);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}

/**
 * aie_part_rscmgr_finish() - uninitialize AI engine partition resource status
 *			      bitmaps.
 * @apart: AI engine partition
 */
void aie_part_rscmgr_finish(struct aie_partition *apart)
{
	struct aie_device *adev = apart->adev;
	u32 t;

	for (t = AIE_TILE_TYPE_TILE; t < AIE_TILE_TYPE_MAX; t++) {
		struct aie_tile_rscs *trscs = &apart->trscs[t];
		struct aie_tile_attr *tattr;
		u32 r;

		/* SHIMNOC reuses SHIMPL resources bitmap */
		if (t == AIE_TILE_TYPE_SHIMNOC)
			continue;

		tattr = aie_dev_get_tile_attr(adev, t);
		for (r = AIE_RSCTYPE_PERF; r < AIE_RSCTYPE_MAX; r++) {
			struct aie_mod_rscs *mod_rscs;
			u32 m;

			mod_rscs = trscs->mod_rscs[r];
			if (!mod_rscs)
				continue;

			for (m = 0 ; m < tattr->num_mods; m++) {
				struct aie_rsc_stat *rscs_stat;

				rscs_stat = mod_rscs[m].rscs_stat;
				if (!rscs_stat)
					continue;

				aie_resource_uninitialize(&rscs_stat->rbits);
				aie_resource_uninitialize(&rscs_stat->sbits);
			}
		}
	}
}

/**
 * aie_part_rscmgr_reset() - reset AI engine partition resource status bitmaps
 *
 * @apart: AI engine partition
 *
 * This function expect caller to lock the partition before calling this
 * function.
 */
void aie_part_rscmgr_reset(struct aie_partition *apart)
{
	struct aie_device *adev = apart->adev;
	u32 t;

	for (t = AIE_TILE_TYPE_TILE; t < AIE_TILE_TYPE_MAX; t++) {
		struct aie_tile_rscs *trscs = &apart->trscs[t];
		struct aie_tile_attr *tattr;
		u32 r;

		/* SHIMNOC reuses SHIMPL resources bitmap */
		if (t == AIE_TILE_TYPE_SHIMNOC)
			continue;

		tattr = aie_dev_get_tile_attr(adev, t);
		for (r = AIE_RSCTYPE_PERF; r < AIE_RSCTYPE_MAX; r++) {
			struct aie_mod_rscs *mod_rscs;
			u32 m;

			mod_rscs = trscs->mod_rscs[r];
			if (!mod_rscs)
				continue;

			for (m = 0 ; m < tattr->num_mods; m++) {
				struct aie_rsc_stat *rscs_stat;

				rscs_stat = mod_rscs[m].rscs_stat;
				if (!rscs_stat)
					continue;

				aie_resource_clear_all(&rscs_stat->rbits);
				aie_resource_clear_all(&rscs_stat->sbits);
			}
		}
	}
}

/**
 * aie_part_rscmgr_rsc_req() - request a type of resource from a module of a
 *			       tile of an AI engine partition
 *
 * @apart: AI engine partition
 * @user_args: user resource request arguments
 *
 * @return: 0 for success, negative value for failure
 *
 * This function will check if there the specified number of free resources
 * available. If yes, allocated the specified number of resources.
 */
long aie_part_rscmgr_rsc_req(struct aie_partition *apart,
			     void __user *user_args)
{
	struct aie_rsc_req_rsp args;
	struct aie_location loc;
	struct aie_rsc_stat *rstat;
	long ret;
	int mod_num_rscs, start_bit;
	struct aie_rsc *rscs;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	if (!args.rscs) {
		dev_err(&apart->dev,
			"invalid resource request, empty resources list.\n");
		return -EINVAL;
	}

	ret = aie_part_adjust_loc(apart, args.req.loc, &loc);
	if (ret < 0)
		return ret;

	if (args.req.type > AIE_RSCTYPE_MAX) {
		dev_err(&apart->dev,
			"invalid resource request, invalid resource type %d.\n",
			args.req.type);
		return -EINVAL;
	}

	rstat = aie_part_get_rsc_bitmaps(apart, loc, args.req.mod,
					 args.req.type);
	start_bit = aie_part_get_rsc_startbit(apart, loc, args.req.mod,
					      args.req.type);
	if (!rstat || start_bit < 0) {
		dev_err(&apart->dev,
			"invalid resource request(%u,%u), mod:%u, rsc:%u.\n",
			args.req.loc.col, args.req.loc.row, args.req.mod,
			args.req.type);
		return -EINVAL;
	}

	mod_num_rscs = aie_part_get_mod_num_rscs(apart, loc, args.req.mod,
						 args.req.type);
	if (!args.req.num_rscs || args.req.num_rscs > mod_num_rscs) {
		dev_err(&apart->dev,
			"invalid resource req(%u,%u),mod:%u,rsc:%u,expect=%u,max=%u.\n",
			args.req.loc.col, args.req.loc.row, args.req.mod,
			args.req.type, args.req.num_rscs, mod_num_rscs);
		return -EINVAL;
	}

	rscs = kmalloc_array(args.req.num_rscs, sizeof(*rscs), GFP_KERNEL);
	if (!rscs)
		return -ENOMEM;

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	/*
	 * There can be some resources needs to be contiguous, such as combo events.
	 * It needs to be 0,1; 2,3; or 0,1,2; or 0,1,2,3
	 */
	if (!(args.req.flag & XAIE_RSC_PATTERN_BLOCK)) {
		ret = aie_resource_get_common_avail(&rstat->rbits, &rstat->sbits,
						    start_bit,
						    args.req.num_rscs,
						    mod_num_rscs, rscs);
	} else {
		ret = aie_resource_get_common_pattern_region(&rstat->rbits,
							     &rstat->sbits,
							     start_bit,
							     args.req.num_rscs,
							     mod_num_rscs,
							     rscs);
	}
	mutex_unlock(&apart->mlock);

	if (ret < 0) {
		if (!(args.req.flag & XAIE_RSC_PATTERN_BLOCK)) {
			dev_err(&apart->dev,
				"invalid resource req(%u,%u),mod:%u,rsc:%u,expect=%u not avail.\n",
				args.req.loc.col, args.req.loc.row,
				args.req.mod, args.req.type,
				args.req.num_rscs);
		} else {
			dev_err(&apart->dev,
				"invalid contiguous resource req(%u,%u),mod:%u,rsc:%u,expect=%u not avail.\n",
				args.req.loc.col, args.req.loc.row,
				args.req.mod, args.req.type, args.req.num_rscs);
		}
		return ret;
	}

	if (copy_to_user((void __user *)args.rscs, rscs,
			 sizeof(*rscs) * args.req.num_rscs))
		return -EFAULT;

	return 0;
}

/**
 * aie_part_rscmgr_rsc_clearbit() - clear resource status of a module of a
 *				    tile of an AI engine partition
 *
 * @apart: AI engine partition
 * @user_args: user resource release arguments
 * @is_release: true to clear the status from both runtime and static bitmaps
 *
 * @return: 0 for success, negative value for failure
 *
 * This function will clear the status of a resource of both runtime status
 * bitmap and static status bitmap or both based on the @is_release setting.
 */
static long aie_part_rscmgr_rsc_clearbit(struct aie_partition *apart,
					 void __user *user_args,
					 bool is_release)
{
	struct aie_rsc args;
	struct aie_location loc, rloc;
	struct aie_rsc_stat *rstat;
	long ret;
	int mod_num_rscs, start_bit;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	rloc.col = (u32)(args.loc.col & 0xFF);
	rloc.row = (u32)(args.loc.row & 0xFF);
	ret = aie_part_adjust_loc(apart, rloc, &loc);
	if (ret < 0)
		return ret;

	if (args.type > AIE_RSCTYPE_MAX) {
		dev_err(&apart->dev,
			"invalid resource to release, invalid resource type %d.\n",
			args.type);
		return -EINVAL;
	}

	rstat = aie_part_get_rsc_bitmaps(apart, loc, args.mod, args.type);
	start_bit = aie_part_get_rsc_startbit(apart, loc, args.mod,
					      args.type);
	if (!rstat || start_bit < 0) {
		dev_err(&apart->dev,
			"invalid resource to release(%u,%u),mod:%u,rsc:%u.\n",
			rloc.col, rloc.row, args.mod, args.type);
		return -EINVAL;
	}

	mod_num_rscs = aie_part_get_mod_num_rscs(apart, loc, args.mod,
						 args.type);
	if (args.id > mod_num_rscs) {
		dev_err(&apart->dev,
			"invalid resource to release(%u,%u),mod:%u,rsc:%u,id=%u.\n",
			rloc.col, rloc.row, args.mod, args.type, args.id);
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	if (!aie_resource_testbit(&rstat->rbits, start_bit + args.id)) {
		dev_err(&apart->dev,
			"invalid resource to release(%u,%u),mod:%u,rsc:%u,id=%u. not requested\n",
			rloc.col, rloc.row, args.mod, args.type, args.id);
		mutex_unlock(&apart->mlock);
		return -EINVAL;
	}

	aie_resource_clear(&rstat->rbits, start_bit + args.id, 1);
	if (is_release)
		aie_resource_clear(&rstat->sbits, start_bit + args.id, 1);

	mutex_unlock(&apart->mlock);

	return 0;
}

/**
 * aie_part_rscmgr_rsc_release() - release resource of a module of a tile of
 *				   an AI engine partition
 *
 * @apart: AI engine partition
 * @user_args: user resource releasearguments
 *
 * @return: 0 for success, negative value for failure
 *
 * This function will clear the bit of the resource runtime and static status
 * bitmap.
 */
long aie_part_rscmgr_rsc_release(struct aie_partition *apart,
				 void __user *user_args)
{
	return aie_part_rscmgr_rsc_clearbit(apart, user_args, true);
}

/**
 * aie_part_rscmgr_rsc_free() - free resource of a module of a tile of an AI
 *				engine partition
 *
 * @apart: AI engine partition
 * @user_args: user resource free arguments
 *
 * @return: 0 for success, negative value for failure
 *
 * This function will clear the bit of the resource runtime status bitmap.
 */
long aie_part_rscmgr_rsc_free(struct aie_partition *apart,
			      void __user *user_args)
{
	return aie_part_rscmgr_rsc_clearbit(apart, user_args, false);
}

/**
 * aie_part_rscmgr_rsc_req_specific() - request for specific resource of a
 *					module of a tile of an AI engine
 *					partition
 *
 * @apart: AI engine partition
 * @user_args: user resource free arguments
 *
 * @return: 0 for success, negative value for failure
 *
 * This function requires the specified resource is set in the static
 * status bitmap
 */
long aie_part_rscmgr_rsc_req_specific(struct aie_partition *apart,
				      void __user *user_args)
{
	struct aie_rsc args;
	struct aie_location loc, rloc;
	struct aie_rsc_stat *rstat;
	long ret;
	int mod_num_rscs, start_bit;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	rloc.col = (u32)(args.loc.col & 0xFF);
	rloc.row = (u32)(args.loc.row & 0xFF);
	ret = aie_part_adjust_loc(apart, rloc, &loc);
	if (ret < 0)
		return ret;

	if (args.type > AIE_RSCTYPE_MAX) {
		dev_err(&apart->dev,
			"invalid resource to request, invalid resource type %d.\n",
			args.type);
		return -EINVAL;
	}

	rstat = aie_part_get_rsc_bitmaps(apart, loc, args.mod, args.type);
	start_bit = aie_part_get_rsc_startbit(apart, loc, args.mod,
					      args.type);
	if (!rstat || start_bit < 0) {
		dev_err(&apart->dev,
			"invalid resource to request(%u,%u),mod:%u,rsc:%u.\n",
			rloc.col, rloc.row, args.mod, args.type);
		return -EINVAL;
	}

	mod_num_rscs = aie_part_get_mod_num_rscs(apart, loc, args.mod,
						 args.type);
	if (args.id > mod_num_rscs) {
		dev_err(&apart->dev,
			"invalid resource to request(%u,%u),mod:%u, rsc:%u,id=%u.\n",
			rloc.col, rloc.row, args.mod, args.type, args.id);
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	/* Check if the resource is in the runtime status bitmap */
	if (aie_resource_testbit(&rstat->rbits, start_bit + args.id)) {
		dev_err(&apart->dev,
			"invalid resource to request(%u,%u),mod:%u,rsc:%u,id=%u, resource in use.\n",
			rloc.col, rloc.row, args.mod, args.type, args.id);
		mutex_unlock(&apart->mlock);
		return -EINVAL;
	}

	aie_resource_set(&rstat->rbits, start_bit + args.id, 1);

	mutex_unlock(&apart->mlock);

	return 0;
}

/**
 * aie_part_rscmgr_rsc_check_avail() - check how many resources vailable for
 *				       the specified resource type
 *
 * @apart: AI engine partition
 * @user_args: user resource free arguments
 *
 * @return: 0 for success, negative value for failure
 *
 * This function requires the specified resource is set in the static
 * status bitmap
 */
long aie_part_rscmgr_rsc_check_avail(struct aie_partition *apart,
				     void __user *user_args)
{
	struct aie_rsc_stat *rstat;
	struct aie_location loc;
	long ret;
	int mod_num_rscs, start_bit;
	struct aie_rsc_req args;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	ret = aie_part_adjust_loc(apart, args.loc, &loc);
	if (ret < 0)
		return ret;

	if (args.type > AIE_RSCTYPE_MAX) {
		dev_err(&apart->dev,
			"invalid resource to request, invalid resource type %d.\n",
			args.type);
		return -EINVAL;
	}

	rstat = aie_part_get_rsc_bitmaps(apart, loc, args.mod, args.type);
	start_bit = aie_part_get_rsc_startbit(apart, loc, args.mod,
					      args.type);
	if (!rstat || start_bit < 0) {
		dev_err(&apart->dev,
			"invalid resource to request(%u,%u),mod:%u,rsc:%u.\n",
			args.loc.col, args.loc.row, args.mod, args.type);
		return -EINVAL;
	}

	mod_num_rscs = aie_part_get_mod_num_rscs(apart, loc, args.mod,
						 args.type);
	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret)
		return ret;

	args.num_rscs = aie_resource_check_common_avail(&rstat->rbits,
							&rstat->sbits,
							start_bit,
							mod_num_rscs);
	mutex_unlock(&apart->mlock);

	if (copy_to_user(user_args, &args, sizeof(args)))
		return -EFAULT;

	return 0;
}
