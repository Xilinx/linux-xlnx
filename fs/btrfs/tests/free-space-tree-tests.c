/*
 * Copyright (C) 2015 Facebook.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/types.h>
#include "btrfs-tests.h"
#include "../ctree.h"
#include "../disk-io.h"
#include "../free-space-tree.h"
#include "../transaction.h"

struct free_space_extent {
	u64 start;
	u64 length;
};

static int __check_free_space_extents(struct btrfs_trans_handle *trans,
				      struct btrfs_fs_info *fs_info,
				      struct btrfs_block_group_cache *cache,
				      struct btrfs_path *path,
				      const struct free_space_extent * const extents,
				      unsigned int num_extents)
{
	struct btrfs_free_space_info *info;
	struct btrfs_key key;
	int prev_bit = 0, bit;
	u64 extent_start = 0, offset, end;
	u32 flags, extent_count;
	unsigned int i;
	int ret;

	info = search_free_space_info(trans, fs_info, cache, path, 0);
	if (IS_ERR(info)) {
		test_msg("Could not find free space info\n");
		ret = PTR_ERR(info);
		goto out;
	}
	flags = btrfs_free_space_flags(path->nodes[0], info);
	extent_count = btrfs_free_space_extent_count(path->nodes[0], info);

	if (extent_count != num_extents) {
		test_msg("Extent count is wrong\n");
		ret = -EINVAL;
		goto out;
	}
	if (flags & BTRFS_FREE_SPACE_USING_BITMAPS) {
		if (path->slots[0] != 0)
			goto invalid;
		end = cache->key.objectid + cache->key.offset;
		i = 0;
		while (++path->slots[0] < btrfs_header_nritems(path->nodes[0])) {
			btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
			if (key.type != BTRFS_FREE_SPACE_BITMAP_KEY)
				goto invalid;
			offset = key.objectid;
			while (offset < key.objectid + key.offset) {
				bit = free_space_test_bit(cache, path, offset);
				if (prev_bit == 0 && bit == 1) {
					extent_start = offset;
				} else if (prev_bit == 1 && bit == 0) {
					if (i >= num_extents)
						goto invalid;
					if (i >= num_extents ||
					    extent_start != extents[i].start ||
					    offset - extent_start != extents[i].length)
						goto invalid;
					i++;
				}
				prev_bit = bit;
				offset += cache->sectorsize;
			}
		}
		if (prev_bit == 1) {
			if (i >= num_extents ||
			    extent_start != extents[i].start ||
			    end - extent_start != extents[i].length)
				goto invalid;
			i++;
		}
		if (i != num_extents)
			goto invalid;
	} else {
		if (btrfs_header_nritems(path->nodes[0]) != num_extents + 1 ||
		    path->slots[0] != 0)
			goto invalid;
		for (i = 0; i < num_extents; i++) {
			path->slots[0]++;
			btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
			if (key.type != BTRFS_FREE_SPACE_EXTENT_KEY ||
			    key.objectid != extents[i].start ||
			    key.offset != extents[i].length)
				goto invalid;
		}
	}

	ret = 0;
out:
	btrfs_release_path(path);
	return ret;
invalid:
	test_msg("Free space tree is invalid\n");
	ret = -EINVAL;
	goto out;
}

static int check_free_space_extents(struct btrfs_trans_handle *trans,
				    struct btrfs_fs_info *fs_info,
				    struct btrfs_block_group_cache *cache,
				    struct btrfs_path *path,
				    const struct free_space_extent * const extents,
				    unsigned int num_extents)
{
	struct btrfs_free_space_info *info;
	u32 flags;
	int ret;

	info = search_free_space_info(trans, fs_info, cache, path, 0);
	if (IS_ERR(info)) {
		test_msg("Could not find free space info\n");
		btrfs_release_path(path);
		return PTR_ERR(info);
	}
	flags = btrfs_free_space_flags(path->nodes[0], info);
	btrfs_release_path(path);

	ret = __check_free_space_extents(trans, fs_info, cache, path, extents,
					 num_extents);
	if (ret)
		return ret;

	/* Flip it to the other format and check that for good measure. */
	if (flags & BTRFS_FREE_SPACE_USING_BITMAPS) {
		ret = convert_free_space_to_extents(trans, fs_info, cache, path);
		if (ret) {
			test_msg("Could not convert to extents\n");
			return ret;
		}
	} else {
		ret = convert_free_space_to_bitmaps(trans, fs_info, cache, path);
		if (ret) {
			test_msg("Could not convert to bitmaps\n");
			return ret;
		}
	}
	return __check_free_space_extents(trans, fs_info, cache, path, extents,
					  num_extents);
}

static int test_empty_block_group(struct btrfs_trans_handle *trans,
				  struct btrfs_fs_info *fs_info,
				  struct btrfs_block_group_cache *cache,
				  struct btrfs_path *path,
				  u32 alignment)
{
	const struct free_space_extent extents[] = {
		{cache->key.objectid, cache->key.offset},
	};

	return check_free_space_extents(trans, fs_info, cache, path,
					extents, ARRAY_SIZE(extents));
}

static int test_remove_all(struct btrfs_trans_handle *trans,
			   struct btrfs_fs_info *fs_info,
			   struct btrfs_block_group_cache *cache,
			   struct btrfs_path *path,
			   u32 alignment)
{
	const struct free_space_extent extents[] = {};
	int ret;

	ret = __remove_from_free_space_tree(trans, fs_info, cache, path,
					    cache->key.objectid,
					    cache->key.offset);
	if (ret) {
		test_msg("Could not remove free space\n");
		return ret;
	}

	return check_free_space_extents(trans, fs_info, cache, path,
					extents, ARRAY_SIZE(extents));
}

static int test_remove_beginning(struct btrfs_trans_handle *trans,
				 struct btrfs_fs_info *fs_info,
				 struct btrfs_block_group_cache *cache,
				 struct btrfs_path *path,
				 u32 alignment)
{
	const struct free_space_extent extents[] = {
		{cache->key.objectid + alignment,
			cache->key.offset - alignment},
	};
	int ret;

	ret = __remove_from_free_space_tree(trans, fs_info, cache, path,
					    cache->key.objectid, alignment);
	if (ret) {
		test_msg("Could not remove free space\n");
		return ret;
	}

	return check_free_space_extents(trans, fs_info, cache, path,
					extents, ARRAY_SIZE(extents));

}

static int test_remove_end(struct btrfs_trans_handle *trans,
			   struct btrfs_fs_info *fs_info,
			   struct btrfs_block_group_cache *cache,
			   struct btrfs_path *path,
			   u32 alignment)
{
	const struct free_space_extent extents[] = {
		{cache->key.objectid, cache->key.offset - alignment},
	};
	int ret;

	ret = __remove_from_free_space_tree(trans, fs_info, cache, path,
					    cache->key.objectid +
					    cache->key.offset - alignment,
					    alignment);
	if (ret) {
		test_msg("Could not remove free space\n");
		return ret;
	}

	return check_free_space_extents(trans, fs_info, cache, path,
					extents, ARRAY_SIZE(extents));
}

static int test_remove_middle(struct btrfs_trans_handle *trans,
			      struct btrfs_fs_info *fs_info,
			      struct btrfs_block_group_cache *cache,
			      struct btrfs_path *path,
			      u32 alignment)
{
	const struct free_space_extent extents[] = {
		{cache->key.objectid, alignment},
		{cache->key.objectid + 2 * alignment,
			cache->key.offset - 2 * alignment},
	};
	int ret;

	ret = __remove_from_free_space_tree(trans, fs_info, cache, path,
					    cache->key.objectid + alignment,
					    alignment);
	if (ret) {
		test_msg("Could not remove free space\n");
		return ret;
	}

	return check_free_space_extents(trans, fs_info, cache, path,
					extents, ARRAY_SIZE(extents));
}

static int test_merge_left(struct btrfs_trans_handle *trans,
			   struct btrfs_fs_info *fs_info,
			   struct btrfs_block_group_cache *cache,
			   struct btrfs_path *path,
			   u32 alignment)
{
	const struct free_space_extent extents[] = {
		{cache->key.objectid, 2 * alignment},
	};
	int ret;

	ret = __remove_from_free_space_tree(trans, fs_info, cache, path,
					    cache->key.objectid,
					    cache->key.offset);
	if (ret) {
		test_msg("Could not remove free space\n");
		return ret;
	}

	ret = __add_to_free_space_tree(trans, fs_info, cache, path,
				       cache->key.objectid, alignment);
	if (ret) {
		test_msg("Could not add free space\n");
		return ret;
	}

	ret = __add_to_free_space_tree(trans, fs_info, cache, path,
				       cache->key.objectid + alignment,
				       alignment);
	if (ret) {
		test_msg("Could not add free space\n");
		return ret;
	}

	return check_free_space_extents(trans, fs_info, cache, path,
					extents, ARRAY_SIZE(extents));
}

static int test_merge_right(struct btrfs_trans_handle *trans,
			   struct btrfs_fs_info *fs_info,
			   struct btrfs_block_group_cache *cache,
			   struct btrfs_path *path,
			   u32 alignment)
{
	const struct free_space_extent extents[] = {
		{cache->key.objectid + alignment, 2 * alignment},
	};
	int ret;

	ret = __remove_from_free_space_tree(trans, fs_info, cache, path,
					    cache->key.objectid,
					    cache->key.offset);
	if (ret) {
		test_msg("Could not remove free space\n");
		return ret;
	}

	ret = __add_to_free_space_tree(trans, fs_info, cache, path,
				       cache->key.objectid + 2 * alignment,
				       alignment);
	if (ret) {
		test_msg("Could not add free space\n");
		return ret;
	}

	ret = __add_to_free_space_tree(trans, fs_info, cache, path,
				       cache->key.objectid + alignment,
				       alignment);
	if (ret) {
		test_msg("Could not add free space\n");
		return ret;
	}

	return check_free_space_extents(trans, fs_info, cache, path,
					extents, ARRAY_SIZE(extents));
}

static int test_merge_both(struct btrfs_trans_handle *trans,
			   struct btrfs_fs_info *fs_info,
			   struct btrfs_block_group_cache *cache,
			   struct btrfs_path *path,
			   u32 alignment)
{
	const struct free_space_extent extents[] = {
		{cache->key.objectid, 3 * alignment},
	};
	int ret;

	ret = __remove_from_free_space_tree(trans, fs_info, cache, path,
					    cache->key.objectid,
					    cache->key.offset);
	if (ret) {
		test_msg("Could not remove free space\n");
		return ret;
	}

	ret = __add_to_free_space_tree(trans, fs_info, cache, path,
				       cache->key.objectid, alignment);
	if (ret) {
		test_msg("Could not add free space\n");
		return ret;
	}

	ret = __add_to_free_space_tree(trans, fs_info, cache, path,
				       cache->key.objectid + 2 * alignment,
				       alignment);
	if (ret) {
		test_msg("Could not add free space\n");
		return ret;
	}

	ret = __add_to_free_space_tree(trans, fs_info, cache, path,
				       cache->key.objectid + alignment,
				       alignment);
	if (ret) {
		test_msg("Could not add free space\n");
		return ret;
	}

	return check_free_space_extents(trans, fs_info, cache, path,
					extents, ARRAY_SIZE(extents));
}

static int test_merge_none(struct btrfs_trans_handle *trans,
			   struct btrfs_fs_info *fs_info,
			   struct btrfs_block_group_cache *cache,
			   struct btrfs_path *path,
			   u32 alignment)
{
	const struct free_space_extent extents[] = {
		{cache->key.objectid, alignment},
		{cache->key.objectid + 2 * alignment, alignment},
		{cache->key.objectid + 4 * alignment, alignment},
	};
	int ret;

	ret = __remove_from_free_space_tree(trans, fs_info, cache, path,
					    cache->key.objectid,
					    cache->key.offset);
	if (ret) {
		test_msg("Could not remove free space\n");
		return ret;
	}

	ret = __add_to_free_space_tree(trans, fs_info, cache, path,
				       cache->key.objectid, alignment);
	if (ret) {
		test_msg("Could not add free space\n");
		return ret;
	}

	ret = __add_to_free_space_tree(trans, fs_info, cache, path,
				       cache->key.objectid + 4 * alignment,
				       alignment);
	if (ret) {
		test_msg("Could not add free space\n");
		return ret;
	}

	ret = __add_to_free_space_tree(trans, fs_info, cache, path,
				       cache->key.objectid + 2 * alignment,
				       alignment);
	if (ret) {
		test_msg("Could not add free space\n");
		return ret;
	}

	return check_free_space_extents(trans, fs_info, cache, path,
					extents, ARRAY_SIZE(extents));
}

typedef int (*test_func_t)(struct btrfs_trans_handle *,
			   struct btrfs_fs_info *,
			   struct btrfs_block_group_cache *,
			   struct btrfs_path *,
			   u32 alignment);

static int run_test(test_func_t test_func, int bitmaps, u32 sectorsize,
		    u32 nodesize, u32 alignment)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_root *root = NULL;
	struct btrfs_block_group_cache *cache = NULL;
	struct btrfs_trans_handle trans;
	struct btrfs_path *path = NULL;
	int ret;

	fs_info = btrfs_alloc_dummy_fs_info();
	if (!fs_info) {
		test_msg("Couldn't allocate dummy fs info\n");
		ret = -ENOMEM;
		goto out;
	}

	root = btrfs_alloc_dummy_root(fs_info, sectorsize, nodesize);
	if (IS_ERR(root)) {
		test_msg("Couldn't allocate dummy root\n");
		ret = PTR_ERR(root);
		goto out;
	}

	btrfs_set_super_compat_ro_flags(root->fs_info->super_copy,
					BTRFS_FEATURE_COMPAT_RO_FREE_SPACE_TREE);
	root->fs_info->free_space_root = root;
	root->fs_info->tree_root = root;

	root->node = alloc_test_extent_buffer(root->fs_info,
		nodesize, nodesize);
	if (!root->node) {
		test_msg("Couldn't allocate dummy buffer\n");
		ret = -ENOMEM;
		goto out;
	}
	btrfs_set_header_level(root->node, 0);
	btrfs_set_header_nritems(root->node, 0);
	root->alloc_bytenr += 2 * nodesize;

	cache = btrfs_alloc_dummy_block_group(8 * alignment, sectorsize);
	if (!cache) {
		test_msg("Couldn't allocate dummy block group cache\n");
		ret = -ENOMEM;
		goto out;
	}
	cache->bitmap_low_thresh = 0;
	cache->bitmap_high_thresh = (u32)-1;
	cache->needs_free_space = 1;
	cache->fs_info = root->fs_info;

	btrfs_init_dummy_trans(&trans);

	path = btrfs_alloc_path();
	if (!path) {
		test_msg("Couldn't allocate path\n");
		return -ENOMEM;
	}

	ret = add_block_group_free_space(&trans, root->fs_info, cache);
	if (ret) {
		test_msg("Could not add block group free space\n");
		goto out;
	}

	if (bitmaps) {
		ret = convert_free_space_to_bitmaps(&trans, root->fs_info,
						    cache, path);
		if (ret) {
			test_msg("Could not convert block group to bitmaps\n");
			goto out;
		}
	}

	ret = test_func(&trans, root->fs_info, cache, path, alignment);
	if (ret)
		goto out;

	ret = remove_block_group_free_space(&trans, root->fs_info, cache);
	if (ret) {
		test_msg("Could not remove block group free space\n");
		goto out;
	}

	if (btrfs_header_nritems(root->node) != 0) {
		test_msg("Free space tree has leftover items\n");
		ret = -EINVAL;
		goto out;
	}

	ret = 0;
out:
	btrfs_free_path(path);
	btrfs_free_dummy_block_group(cache);
	btrfs_free_dummy_root(root);
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}

static int run_test_both_formats(test_func_t test_func, u32 sectorsize,
				 u32 nodesize, u32 alignment)
{
	int test_ret = 0;
	int ret;

	ret = run_test(test_func, 0, sectorsize, nodesize, alignment);
	if (ret) {
		test_msg("%pf failed with extents, sectorsize=%u, nodesize=%u, alignment=%u\n",
			 test_func, sectorsize, nodesize, alignment);
		test_ret = ret;
	}

	ret = run_test(test_func, 1, sectorsize, nodesize, alignment);
	if (ret) {
		test_msg("%pf failed with bitmaps, sectorsize=%u, nodesize=%u, alignment=%u\n",
			 test_func, sectorsize, nodesize, alignment);
		test_ret = ret;
	}

	return test_ret;
}

int btrfs_test_free_space_tree(u32 sectorsize, u32 nodesize)
{
	test_func_t tests[] = {
		test_empty_block_group,
		test_remove_all,
		test_remove_beginning,
		test_remove_end,
		test_remove_middle,
		test_merge_left,
		test_merge_right,
		test_merge_both,
		test_merge_none,
	};
	u32 bitmap_alignment;
	int test_ret = 0;
	int i;

	/*
	 * Align some operations to a page to flush out bugs in the extent
	 * buffer bitmap handling of highmem.
	 */
	bitmap_alignment = BTRFS_FREE_SPACE_BITMAP_BITS * PAGE_SIZE;

	test_msg("Running free space tree tests\n");
	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		int ret;

		ret = run_test_both_formats(tests[i], sectorsize, nodesize,
					    sectorsize);
		if (ret)
			test_ret = ret;

		ret = run_test_both_formats(tests[i], sectorsize, nodesize,
					    bitmap_alignment);
		if (ret)
			test_ret = ret;
	}

	return test_ret;
}
