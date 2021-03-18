// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine partition resource manager
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */

#include "ai-engine-internal.h"

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
		if (t == AIE_TILE_TYPE_SHIMNOC)
			*trscs = apart->trscs[AIE_TILE_TYPE_SHIMPL];

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
