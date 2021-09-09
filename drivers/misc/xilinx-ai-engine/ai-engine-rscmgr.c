// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine partition resource manager
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */

#include "ai-engine-internal.h"
#include <linux/slab.h>

/*
 * Macros for the AI engine resource bitmap element header
 */
#define AIE_RSC_BITMAP_TILETYPE_BITSHIFT	0U
#define AIE_RSC_BITMAP_TILETYPE_BITWIDTH	4U
#define AIE_RSC_BITMAP_MODTYPE_BITSHIFT		4U
#define AIE_RSC_BITMAP_MODTYPE_BITWIDTH		4U
#define AIE_RSC_BITMAP_RSCTYPE_BITSHIFT		8U
#define AIE_RSC_BITMAP_RSCTYPE_BITWIDTH		8U
#define AIE_RSC_BITMAP_LENU64_BITSHIFT		16U
#define AIE_RSC_BITMAP_LENU64_BITWIDTH		32U

#define AIE_RSC_BITMAP_GEN_MASK(N) \
	GENMASK_ULL((AIE_RSC_BITMAP_##N ##_BITSHIFT + \
		     AIE_RSC_BITMAP_##N ##_BITWIDTH - 1), \
		    AIE_RSC_BITMAP_##N ##_BITSHIFT)
#define AIE_RSC_BITMAP_TILETYPE_MASK	AIE_RSC_BITMAP_GEN_MASK(TILETYPE)
#define AIE_RSC_BITMAP_MODTYPE_MASK	AIE_RSC_BITMAP_GEN_MASK(MODTYPE)
#define AIE_RSC_BITMAP_RSCTYPE_MASK	AIE_RSC_BITMAP_GEN_MASK(RSCTYPE)
#define AIE_RSC_BITMAP_LENU64_MASK	AIE_RSC_BITMAP_GEN_MASK(LENU64)

#define AIE_RSC_BITMAP_HEAD_VAL(N, v) \
	(((v) & AIE_RSC_BITMAP_##N ##_MASK) >> AIE_RSC_BITMAP_##N ##_BITSHIFT)

/*
 * enum for AI engine resource bitmap allocation types
 */
enum aie_rsc_alloc_type {
	AIE_RSC_ALLOC_STATIC = 0,
	AIE_RSC_ALLOC_AVAIL = 1,
	AIE_RSC_ALLOC_MAX = 2
};

/**
 * struct aie_rsc_meta_header - struct of a resource bitmaps meta data header
 * @stat: statistics information of the bitmaps, such as number of bitmaps
 * @bitmap_off: offset to the start of the binary of the first bitmap element
 */
struct aie_rsc_meta_header {
	u64 stat;
	u64 bitmap_off;
};

/**
 * struct aie_rsc_bitmap - struct of a resource bitmap element
 * @header: bitmap header, it contains the following information:
 *	    tile type, module type, resource type, and the bitmap
 *	    length.
 * @bitmap: the pointer of bitmap
 */
struct aie_rsc_bitmap {
	u64 header;
	u64 bitmap[0];
};

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
 * aie_part_get_ttype_rsc_bitmaps - helper function to get bitmap of a resource
 *				    with tile type, module type, and resource
 *				    type
 *
 * @apart: AI engine partition
 * @ttype: tile type
 * @mod: module type
 * @rtype: resource type
 * @return: pointer to AI engine resource status bitmaps if resource is found,
 *	    otherwise NULL
 */
static
struct aie_rsc_stat *aie_part_get_ttype_rsc_bitmaps(struct aie_partition *apart,
						    enum aie_tile_type ttype,
						    enum aie_module_type mod,
						    enum aie_rsc_type rtype)
{
	int mod_id;
	struct aie_mod_rscs *mrscs;

	if (ttype >= AIE_TILE_TYPE_MAX)
		return NULL;

	mod_id = aie_dev_get_mod_id(apart->adev, ttype, mod);
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

	return aie_part_get_ttype_rsc_bitmaps(apart, ttype, mod, rtype);
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

			mod_rscs = kcalloc(tattr->num_mods,
					   sizeof(*mod_rscs), GFP_KERNEL);
			if (!mod_rscs) {
				aie_part_rscmgr_finish(apart);
				return -ENOMEM;
			}

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

				rscs_stat = kzalloc(sizeof(*rscs_stat),
						    GFP_KERNEL);
				if (!rscs_stat) {
					aie_part_rscmgr_finish(apart);
					return -ENOMEM;
				}

				mod_rscs[m].rscs_stat = rscs_stat;
				total_rscs = num_mrscs * num_rows * num_cols;
				/*
				 * initialize bitmaps for static resources and
				 * runtime allocated resources.
				 */
				ret = aie_resource_initialize(&rscs_stat->rbits,
							      total_rscs);
				if (ret) {
					aie_part_rscmgr_finish(apart);
					return ret;
				}
				ret = aie_resource_initialize(&rscs_stat->sbits,
							      total_rscs);
				if (ret) {
					aie_part_rscmgr_finish(apart);
					return ret;
				}
			}
		}
	}

	/* Reserve resources for interrupts */
	return aie_part_set_intr_rscs(apart);
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

			kfree(mod_rscs);
			trscs->mod_rscs[r] = NULL;
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

	/* Always reserve resources for interrupt */
	(void)aie_part_set_intr_rscs(apart);
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
	if (ret) {
		kfree(rscs);
		return ret;
	}

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
			dev_warn(&apart->dev,
				 "invalid resource req(%u,%u),mod:%u,rsc:%u,expect=%u not avail.\n",
				args.req.loc.col, args.req.loc.row,
				args.req.mod, args.req.type,
				args.req.num_rscs);
		} else {
			dev_warn(&apart->dev,
				 "invalid contiguous resource req(%u,%u),mod:%u,rsc:%u,expect=%u not avail.\n",
				args.req.loc.col, args.req.loc.row,
				args.req.mod, args.req.type, args.req.num_rscs);
		}
		kfree(rscs);
		return ret;
	}

	if (copy_to_user((void __user *)args.rscs, rscs,
			 sizeof(*rscs) * args.req.num_rscs))
		ret = -EFAULT;
	else
		ret = 0;

	kfree(rscs);
	return ret;
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
		return -EBUSY;
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

/**
 * aie_part_rscmgr_get_ungated_bc_mods() - find the ungated modules of the full
 *					   partition and fill in the locations
 *					   information to the resources array.
 * @apart: AI engine partition
 * @num_rscs: number of broadcast resources, each module of a tile has a
 *	      broadcast resource in this array.
 * @onum_rscs: returns the number of actual ungated broadcast resources of the
 *	       whole partition.
 *
 * @rscs: broadcast resources array
 * @return: 0 for success, negative value for failure
 */
static int aie_part_rscmgr_get_ungated_bc_mods(struct aie_partition *apart,
					       u32 num_rscs, u32 *onum_rscs,
					       struct aie_rsc *rscs)
{
	struct aie_device *adev = apart->adev;
	u32 c, r, i = 0;

	for (c = 0; c < apart->range.size.col; c++) {
		for (r = 0; r < apart->range.size.row; r++) {
			struct aie_location l;
			u32 ttype, m;
			const struct aie_tile_attr *tattr;
			const struct aie_tile_rsc_attr *rattr;
			enum aie_rsc_type rtype = AIE_RSCTYPE_BROADCAST;

			l.col = apart->range.start.col + c;
			l.row = r;
			ttype = adev->ops->get_tile_type(&l);
			tattr = &adev->ttype_attr[ttype];
			rattr = &tattr->rscs_attr[rtype];
			for (m = 0; m < tattr->num_mods; m++) {
				/*
				 * if module doesn't have broadcast channel,
				 * skipped. This is not the case today.
				 */
				if (!rattr->mod_attr[m].num_rscs)
					continue;
				/* Check if the broadcast resource is gated */
				if (aie_part_check_clk_enable_loc(apart, &l)) {
					if (i >= num_rscs) {
						dev_err(&apart->dev,
							"failed to returns all ungated tiles, not enough resource elements.\n");
						return -EINVAL;
					}
					rscs[i].loc.col = (u8)(c & 0xFF);
					rscs[i].loc.row = (u8)(r & 0xFF);
					rscs[i].mod = tattr->mods[m];
					i++;
				}
			}
		}
	}

	*onum_rscs = i;
	return 0;
}

/**
 * aie_part_rscmgr_get_or_bc_stat() - get OR the broadcast resources stat of
 *				      specified modules in the specified
 *				      resources array.
 *
 * @apart: AI engine partition
 * @num_rscs: number of broadcast resources, every module has one broadcast
 *	      resource
 * @rscs: array of broadcast resources, each element contains the tile
 *	  location and module information of the broadcast channel
 * @runtime_only: true to only check the runtime allocated resources bitmap,
 *		  false to check both runtime and statically allocated resource
 *		  bitmaps.
 * @or_stat: returns result of OR all the modules broadcast resources status
 * @return: 0 for success, negative value for failure
 */
static int aie_part_rscmgr_get_or_bc_stat(struct aie_partition *apart,
					  u32 num_rscs, struct aie_rsc *rscs,
					  bool runtime_only,
					  unsigned long *or_stat)
{
	u32 i;

	*or_stat = 0;
	for (i = 0; i < num_rscs; i++) {
		struct aie_location l;
		struct aie_rsc_stat *rstat;
		int mod_num_rscs, start_bit;

		l.col = apart->range.start.col + rscs[i].loc.col;
		l.row = rscs[i].loc.row;
		rstat = aie_part_get_rsc_bitmaps(apart, l, rscs[i].mod,
						 AIE_RSCTYPE_BROADCAST);
		start_bit = aie_part_get_rsc_startbit(apart, l, rscs[i].mod,
						      AIE_RSCTYPE_BROADCAST);
		if (!rstat || start_bit < 0) {
			dev_err(&apart->dev,
				"failed to get broadcast bitmap for[%u]:tile(%u,%u), mod=%u.\n",
				i, rscs[i].loc.col, rscs[i].loc.row,
				rscs[i].mod);
			return -EINVAL;
		}
		mod_num_rscs = aie_part_get_mod_num_rscs(apart, l, rscs[i].mod,
							 AIE_RSCTYPE_BROADCAST);
		*or_stat |= aie_resource_or_get_valueul(&rstat->rbits,
							start_bit,
							mod_num_rscs);
		if (!runtime_only)
			*or_stat |= aie_resource_or_get_valueul(&rstat->sbits,
								start_bit,
								mod_num_rscs);
	}

	return 0;
}

/**
 * aie_part_rscmgr_get_common_bc() - get common broadcast id of specified
 *				     modules in the specified resources array.
 *
 * @apart: AI engine partition
 * @num_rscs: number of broadcast resources, every module has one broadcast
 *	      resource
 * @rscs: array of broadcast resources, each element contains the tile
 *	  location and module information of the broadcast channel
 * @return: common broadcast channel id for success, negative value for failure
 *
 * This function checks both runtime and static allocated resources bitmap.
 */
static int aie_part_rscmgr_get_common_bc(struct aie_partition *apart,
					 u32 num_rscs, struct aie_rsc *rscs)
{
	unsigned long or_stat, b;
	int ret;
	struct aie_location l;
	int mod_num_rscs;

	l.col = apart->range.start.col + (u32)rscs[0].loc.row;
	l.row = (u32)rscs[0].loc.row;

	ret = aie_part_rscmgr_get_or_bc_stat(apart, num_rscs, rscs, false,
					     &or_stat);
	if (ret)
		return ret;

	mod_num_rscs = aie_part_get_mod_num_rscs(apart, l, rscs[0].mod,
						 AIE_RSCTYPE_BROADCAST);
	b = bitmap_find_next_zero_area(&or_stat, mod_num_rscs, 0, 1, 0);
	if (b >= mod_num_rscs)
		return -EINVAL;

	return (int)b;
}

/**
 * aie_part_rscmgr_check_common_bc() - validate the specified common broadcast
 *				       id in the specified modules in the
 *				       specified resources array.
 *
 * @apart: AI engine partition
 * @bc: broadcast channel id to check
 * @num_rscs: number of broadcast resources, every module has one broadcast
 *	      resource
 * @rscs: array of broadcast resources, each element contains the tile
 *	  location and module information of the broadcast channel
 * @return: 0 if the specified broadcast channel id is available for all the
 *	    specified modules, negative value for failure
 *
 * This function only checks runtime allocated resources bitmap.
 */
static int aie_part_rscmgr_check_common_bc(struct aie_partition *apart,
					   u32 bc, u32 num_rscs,
					   struct aie_rsc *rscs)
{
	unsigned long or_stat;
	int ret;
	struct aie_location l;
	int mod_num_rscs;

	l.col = apart->range.start.col + (u32)rscs[0].loc.row;
	l.row = (u32)rscs[0].loc.row;

	mod_num_rscs = aie_part_get_mod_num_rscs(apart, l, rscs[0].mod,
						 AIE_RSCTYPE_BROADCAST);
	if (bc > mod_num_rscs) {
		dev_err(&apart->dev,
			"invalid specified broadcast id %u, max is %u.\n",
			bc, mod_num_rscs);
		return -EINVAL;
	}

	ret = aie_part_rscmgr_get_or_bc_stat(apart, num_rscs, rscs, true,
					     &or_stat);
	if (ret)
		return ret;

	if (test_bit(bc, &or_stat)) {
		dev_err(&apart->dev,
			"specified broadcast id %u is occupied.\n", bc);
		return -EBUSY;
	}

	return 0;
}

/**
 * aie_part_rscmgr_check_rscs_modules() - validate the modules of the array of
 *					input resources
 *
 * @apart: AI engine partition
 * @num_rscs: number of resources
 * @rscs: array of resources, each element contains the tile
 *	  location and module information of the resource
 * @return: 0 if the modules of all the resources are valid, negative value
 *	    for failure
 *
 * This function validate the modules and the tiles of the resources, and
 * check if resource module is gated.
 */
static int aie_part_rscmgr_check_rscs_modules(struct aie_partition *apart,
					      u32 num_rscs,
					      struct aie_rsc *rscs)
{
	struct aie_device *adev = apart->adev;
	u32 i;

	for (i = 0; i < num_rscs; i++) {
		struct aie_location l;

		l.col = apart->range.start.col + rscs[i].loc.col;
		l.row = rscs[i].loc.row;
		/* validate tile location */
		if (aie_validate_location(apart, l)) {
			dev_err(&apart->dev,
				"failed resource check tile(%u,%u) invalid.\n",
					rscs[i].loc.col, rscs[i].loc.row);
			return -EINVAL;
		}

		/* validate module */
		if (aie_dev_get_mod_id(adev, adev->ops->get_tile_type(&l),
				       rscs[i].mod) < 0) {
			dev_err(&apart->dev,
				"failed resource check, tile(%u,%u) mod %u invalid.\n",
					rscs[i].loc.col, rscs[i].loc.row,
					rscs[i].mod);
			return -EINVAL;
		}

		/* check if the resource module is gated */
		if (!aie_part_check_clk_enable_loc(apart, &l)) {
			dev_err(&apart->dev,
				"failed resource check, tile(%u,%u) mod=%u is gated.\n",
				rscs[i].loc.col, rscs[i].loc.row,
				rscs[i].mod);
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * aie_part_rscmgr_set_tile_broadcast() - set broadcast channel in use
 *					  of a module of a tile
 *
 * @apart: AI engine partition
 * @loc: tile location
 * @mod: module
 * @id: broadcast channel id
 * @return: 0 for success, negative value for failure
 *
 * This function will set the bit of the specified broadcast channel in the
 * runtime broadcast bitmap of the specified module of the specified tile.
 */
int aie_part_rscmgr_set_tile_broadcast(struct aie_partition *apart,
				       struct aie_location loc,
				       enum aie_module_type mod, uint32_t id)
{
	struct aie_rsc_stat *rstat;
	int start_bit;

	rstat = aie_part_get_rsc_bitmaps(apart, loc, mod,
					 AIE_RSCTYPE_BROADCAST);
	/* bitmap pointer cannot be NULL. */
	if (WARN_ON(!rstat || !rstat->rbits.bitmap))
		return -EFAULT;

	start_bit = aie_part_get_rsc_startbit(apart, loc, mod,
					      AIE_RSCTYPE_BROADCAST);
	aie_resource_set(&rstat->rbits, start_bit + id, 1);

	return 0;
}

/**
 * aie_part_rscmgr_get_broadcast() - get common broadcast channel of
 *				     the specified modules or the whole
 *				     partition.
 *
 * @apart: AI engine partition
 * @user_args: user resource free arguments
 *
 * @return: 0 for success, negative value for failure
 *
 * This function get a common broadcast channel for the specified set
 * of AI engine modules in the resources array. If the any of the input set of
 * tiles is gated, it will return failure. This ioctl will not check the
 * connection of the input modules set.
 * The driver will fill in the resource ID with the assigned broadcast channel
 * ID of the resources array.
 * If the XAIE_BROADCAST_ALL is set in the request flag, it will get the
 * broadcast channel for all the ungated tiles of the partition.
 * If a particular broadcast channel id is specified in the request, if will
 * check if the channel is available for the specified modules, or the whole
 * partition depends on if XAIE_BROADCAST_ALL is set.
 */
long aie_part_rscmgr_get_broadcast(struct aie_partition *apart,
				   void __user *user_args)
{
	struct aie_rsc_bc_req args;
	struct aie_rsc *rscs;
	u32 i;
	long ret;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	rscs = kmalloc_array(args.num_rscs, sizeof(*rscs), GFP_KERNEL);
	if (!rscs)
		return -ENOMEM;

	if (!(args.flag & XAIE_BROADCAST_ALL)) {
		if (copy_from_user(rscs, (void __user *)args.rscs,
				   sizeof(*rscs) * args.num_rscs)) {
			kfree(rscs);
			return -EFAULT;
		}
	}

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		kfree(rscs);
		return ret;
	}

	if (args.flag & XAIE_BROADCAST_ALL)
		/*
		 * It is to broadcast to the whole partition.
		 * Get all the ungated modules.
		 */
		ret = aie_part_rscmgr_get_ungated_bc_mods(apart, args.num_rscs,
							  &args.num_rscs,
							  rscs);
	else
		/*
		 * validate tiles and modules, and check if there are modules
		 * gated
		 */
		ret = aie_part_rscmgr_check_rscs_modules(apart, args.num_rscs,
							 rscs);
	if (ret)
		goto error;

	/* find the common broadcast signal among the specified modules */
	if (args.id == XAIE_BROADCAST_ID_ANY) {
		ret = aie_part_rscmgr_get_common_bc(apart, args.num_rscs, rscs);
		if (ret >= 0) {
			args.id = (u32)ret;
			ret = 0;
		} else {
			dev_warn(&apart->dev, "no available broadcast channel.\n");
		}
	} else {
		ret = aie_part_rscmgr_check_common_bc(apart, args.id,
						      args.num_rscs,
						      rscs);
	}
	if (ret)
		goto error;

	/* set the broadcast channel resource runtime status bit */
	for (i = 0; i < args.num_rscs; i++) {
		struct aie_location l;

		l.col = apart->range.start.col + rscs[i].loc.col;
		l.row = rscs[i].loc.row;
		ret = aie_part_rscmgr_set_tile_broadcast(apart, l, rscs[i].mod,
							 args.id);
		if (ret)
			goto error;

		rscs[i].id = args.id;
	}

	mutex_unlock(&apart->mlock);

	if (copy_to_user((void __user *)args.rscs, rscs,
			 sizeof(*rscs) * args.num_rscs)) {
		kfree(rscs);
		return -EFAULT;
	}

	/*
	 * If it is required to broadcast to whole partition, it needs to
	 * return the actual number of broadcast resources as some tiles
	 * can be gated
	 */
	if (args.flag & XAIE_BROADCAST_ALL) {
		struct aie_rsc_bc_req __user *uargs = user_args;

		if (copy_to_user((void __user *)&uargs->num_rscs,
				 &args.num_rscs, sizeof(args.num_rscs))) {
			kfree(rscs);
			return -EFAULT;
		}
	}

	kfree(rscs);
	return 0;
error:
	mutex_unlock(&apart->mlock);
	kfree(rscs);
	return ret;
}

/**
 * aie_part_rscmgr_set_static() - sets statically allocated resources bitmaps
 *
 * @apart: AI engine partition
 * @meta: meta data which contains the statically allocated resources bitmaps
 *
 * @return: 0 for success, negative value for failure
 *
 * This function takes the static bitmap information from meta data and fill
 * in the static bitmap.
 */
int aie_part_rscmgr_set_static(struct aie_partition *apart, void *meta)
{
	struct aie_rsc_meta_header *header = meta;
	struct aie_rsc_bitmap *bitmap;
	u64 i, num_bitmaps, offset;

	if (!header) {
		dev_err(&apart->dev,
			"failed to get static resources, meta data is NULL.\n");
		return -EINVAL;
	}

	/*
	 * For now, the stat field of the header only contains the number of
	 * bitmaps.
	 */
	num_bitmaps = header->stat;
	offset = header->bitmap_off;
	if (!num_bitmaps || offset < sizeof(*header)) {
		dev_err(&apart->dev,
			"failed to get static resources, invalid header.\n");
		return -EINVAL;
	}

	bitmap = (struct aie_rsc_bitmap *)(meta + offset);
	for (i = 0; i < num_bitmaps; i++) {
		struct aie_rsc_stat *rstat;
		const struct aie_mod_rsc_attr *mrattr;
		u64 header = bitmap->header;
		u32 lrlen, rlen, ttype, mtype, rtype, total;

		ttype = AIE_RSC_BITMAP_HEAD_VAL(TILETYPE, header);
		mtype = AIE_RSC_BITMAP_HEAD_VAL(MODTYPE, header);
		rtype = AIE_RSC_BITMAP_HEAD_VAL(RSCTYPE, header);
		rlen = AIE_RSC_BITMAP_HEAD_VAL(LENU64, header);

		if (!rlen) {
			dev_err(&apart->dev,
				"invalid static bitmap[%llu], length is 0.\n",
				i);
			return -EINVAL;
		}

		mrattr = aie_dev_get_mod_rsc_attr(apart->adev, ttype, mtype,
						  rtype);
		if (!mrattr) {
			dev_err(&apart->dev,
				"invalid static bitmap[%llu], invalid tile(%u)/module(%u)/rsce(%u) types combination.\n",
				i, ttype, mtype, rtype);
			return -EINVAL;
		}

		total = mrattr->num_rscs * apart->range.size.col *
			aie_part_get_tile_rows(apart, ttype);
		lrlen = BITS_TO_LONGS(total);
		if (rlen != lrlen) {
			dev_err(&apart->dev,
				"invalid static bitmap[%llu], tile(%u)/module(%u)/rscs(%u), expect len(%u), actual(%u).\n",
				i, ttype, mtype, rtype, lrlen, rlen);
			return -EINVAL;
		}

		rstat = aie_part_get_ttype_rsc_bitmaps(apart, ttype, mtype,
						       rtype);
		/* if bitmap length is not 0, bitmap pointer cannot be NULL. */
		if (WARN_ON(!rstat || !rstat->sbits.bitmap))
			return -EFAULT;

		/* copy the bitmap from meta data */
		bitmap_copy(rstat->sbits.bitmap,
			    (unsigned long *)bitmap->bitmap, total);

		bitmap = (struct aie_rsc_bitmap *)((void *)bitmap +
						   sizeof(header) +
						   rlen * sizeof(u64));
	}

	return 0;
}

/**
 * aie_part_rscmgr_check_static() - check the number of static resources
 *
 * @rstat: resource statistics structure which contains bitmaps of a resource
 *	   type of a module type of a tile type.
 * @sbit: start bit of the resource bitmap of a tile of a module
 * @total: number of total resources bits to check
 *
 * @return: number of static resources
 *
 * This function returns the number of static resources of a resource
 * bitmap.
 */
static int aie_part_rscmgr_check_static(struct aie_rsc_stat *rstat,
					u32 sbit, u32 total)
{
	u32 i;
	int num_static = 0;

	for (i = sbit; i < sbit + total; i++) {
		if (aie_resource_testbit(&rstat->sbits, i))
			num_static++;
	}

	return num_static;
}

/**
 * aie_part_rscmgr_check_avail() - check the number of available resources
 *
 * @rstat: resource statistics structure which contains bitmaps of a resource
 *	   type of a module type of a tile type.
 * @sbit: start bit of the resource bitmap of a tile of a module
 * @total: number of total resources bits to check
 *
 * @return: number of available resources for success, negative value for
 *	    failure
 *
 * This function returns the number of available resources of a resource
 * bitmap.
 */
static int aie_part_rscmgr_check_avail(struct aie_rsc_stat *rstat,
				       u32 sbit, u32 total)
{
	return aie_resource_check_common_avail(&rstat->rbits,
					       &rstat->sbits,
					       sbit, total);
}

/**
 * aie_part_rscmgr_get_statistics() - get resource statistics based on user
 *				      request
 *
 * @apart: AI engine partition
 * @user_args: user resource statistics request. it contains the number of
 *	       resource statistics wants to get followed by the statistics
 *	       array and the statistics type to specify if it is for static
 *	       allocated resources or available resources. Each statistics
 *	       element contains the tile location, module type and the resource
 *	       type.
 *
 * @return: 0 for success, negative value for failure
 *
 * This function returns the resource statistics based on the user request.
 * If user requests for available resource statistics, it returns the number
 * of available resources of each resource statistics entry. If user requests
 * for static resources statistics, it returns the number of static resources
 * of each resource statistics entry.
 */
long aie_part_rscmgr_get_statistics(struct aie_partition *apart,
				    void __user *user_args)
{
	struct aie_rsc_user_stat_array args;
	struct aie_rsc_user_stat __user *ustat_ptr;
	u32 i;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	if (args.stats_type >= AIE_RSC_STAT_TYPE_MAX) {
		dev_err(&apart->dev,
			"get rsc statistics failed, invalid rsc stat type %u.\n",
			args.stats_type);
		return -EINVAL;
	}

	ustat_ptr = (struct aie_rsc_user_stat __user *)args.stats;
	for (i = 0; i < args.num_stats; i++) {
		struct aie_rsc_user_stat ustat;
		struct aie_rsc_stat *rstat;
		struct aie_location rloc, loc;
		long ret;
		int max_rscs, start_bit;

		if (copy_from_user(&ustat, (void __user *)ustat_ptr,
				   sizeof(ustat)))
			return -EFAULT;

		/* convert user tile loc to kernel tile loc format */
		rloc.col = (u32)(ustat.loc.col & 0xFF);
		rloc.row = (u32)(ustat.loc.row & 0xFF);
		ret = aie_part_adjust_loc(apart, rloc, &loc);
		if (ret < 0)
			return ret;

		if (ustat.type > AIE_RSCTYPE_MAX) {
			dev_err(&apart->dev,
				"get rsc statistics failed, invalid resource type %d.\n",
				ustat.type);
			return -EINVAL;
		}

		rstat = aie_part_get_rsc_bitmaps(apart, loc, ustat.mod,
						 ustat.type);
		start_bit = aie_part_get_rsc_startbit(apart, loc, ustat.mod,
						      ustat.type);
		if (!rstat || start_bit < 0) {
			dev_err(&apart->dev,
				"get rsc statistics failed, invalid resource(%u,%u),mod:%u,rsc:%u.\n",
				loc.col, loc.row, ustat.mod, ustat.type);
			return -EINVAL;
		}

		max_rscs = aie_part_get_mod_num_rscs(apart, loc, ustat.mod,
						     ustat.type);
		ret = mutex_lock_interruptible(&apart->mlock);
		if (ret)
			return ret;

		if (args.stats_type == AIE_RSC_STAT_TYPE_STATIC)
			ustat.num_rscs = aie_part_rscmgr_check_static(rstat,
								      start_bit,
								      max_rscs);
		else
			ustat.num_rscs = aie_part_rscmgr_check_avail(rstat,
								     start_bit,
								     max_rscs);

		mutex_unlock(&apart->mlock);
		if (WARN_ON(ustat.num_rscs < 0))
			return -EFAULT;

		/* copy the information back to userspace */
		if (copy_to_user((void __user *)ustat_ptr, &ustat,
				 sizeof(ustat)))
			return -EFAULT;

		ustat_ptr++;
	}

	return 0;
}
