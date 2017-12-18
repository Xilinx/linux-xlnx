/*
 * Copyright (C) 2013 Fusion IO.  All rights reserved.
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

#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include "btrfs-tests.h"
#include "../ctree.h"
#include "../extent_io.h"

#define PROCESS_UNLOCK		(1 << 0)
#define PROCESS_RELEASE		(1 << 1)
#define PROCESS_TEST_LOCKED	(1 << 2)

static noinline int process_page_range(struct inode *inode, u64 start, u64 end,
				       unsigned long flags)
{
	int ret;
	struct page *pages[16];
	unsigned long index = start >> PAGE_SHIFT;
	unsigned long end_index = end >> PAGE_SHIFT;
	unsigned long nr_pages = end_index - index + 1;
	int i;
	int count = 0;
	int loops = 0;

	while (nr_pages > 0) {
		ret = find_get_pages_contig(inode->i_mapping, index,
				     min_t(unsigned long, nr_pages,
				     ARRAY_SIZE(pages)), pages);
		for (i = 0; i < ret; i++) {
			if (flags & PROCESS_TEST_LOCKED &&
			    !PageLocked(pages[i]))
				count++;
			if (flags & PROCESS_UNLOCK && PageLocked(pages[i]))
				unlock_page(pages[i]);
			put_page(pages[i]);
			if (flags & PROCESS_RELEASE)
				put_page(pages[i]);
		}
		nr_pages -= ret;
		index += ret;
		cond_resched();
		loops++;
		if (loops > 100000) {
			printk(KERN_ERR "stuck in a loop, start %Lu, end %Lu, nr_pages %lu, ret %d\n", start, end, nr_pages, ret);
			break;
		}
	}
	return count;
}

static int test_find_delalloc(u32 sectorsize)
{
	struct inode *inode;
	struct extent_io_tree tmp;
	struct page *page;
	struct page *locked_page = NULL;
	unsigned long index = 0;
	u64 total_dirty = SZ_256M;
	u64 max_bytes = SZ_128M;
	u64 start, end, test_start;
	u64 found;
	int ret = -EINVAL;

	test_msg("Running find delalloc tests\n");

	inode = btrfs_new_test_inode();
	if (!inode) {
		test_msg("Failed to allocate test inode\n");
		return -ENOMEM;
	}

	extent_io_tree_init(&tmp, &inode->i_data);

	/*
	 * First go through and create and mark all of our pages dirty, we pin
	 * everything to make sure our pages don't get evicted and screw up our
	 * test.
	 */
	for (index = 0; index < (total_dirty >> PAGE_SHIFT); index++) {
		page = find_or_create_page(inode->i_mapping, index, GFP_KERNEL);
		if (!page) {
			test_msg("Failed to allocate test page\n");
			ret = -ENOMEM;
			goto out;
		}
		SetPageDirty(page);
		if (index) {
			unlock_page(page);
		} else {
			get_page(page);
			locked_page = page;
		}
	}

	/* Test this scenario
	 * |--- delalloc ---|
	 * |---  search  ---|
	 */
	set_extent_delalloc(&tmp, 0, sectorsize - 1, NULL);
	start = 0;
	end = 0;
	found = find_lock_delalloc_range(inode, &tmp, locked_page, &start,
					 &end, max_bytes);
	if (!found) {
		test_msg("Should have found at least one delalloc\n");
		goto out_bits;
	}
	if (start != 0 || end != (sectorsize - 1)) {
		test_msg("Expected start 0 end %u, got start %llu end %llu\n",
			sectorsize - 1, start, end);
		goto out_bits;
	}
	unlock_extent(&tmp, start, end);
	unlock_page(locked_page);
	put_page(locked_page);

	/*
	 * Test this scenario
	 *
	 * |--- delalloc ---|
	 *           |--- search ---|
	 */
	test_start = SZ_64M;
	locked_page = find_lock_page(inode->i_mapping,
				     test_start >> PAGE_SHIFT);
	if (!locked_page) {
		test_msg("Couldn't find the locked page\n");
		goto out_bits;
	}
	set_extent_delalloc(&tmp, sectorsize, max_bytes - 1, NULL);
	start = test_start;
	end = 0;
	found = find_lock_delalloc_range(inode, &tmp, locked_page, &start,
					 &end, max_bytes);
	if (!found) {
		test_msg("Couldn't find delalloc in our range\n");
		goto out_bits;
	}
	if (start != test_start || end != max_bytes - 1) {
		test_msg("Expected start %Lu end %Lu, got start %Lu, end "
			 "%Lu\n", test_start, max_bytes - 1, start, end);
		goto out_bits;
	}
	if (process_page_range(inode, start, end,
			       PROCESS_TEST_LOCKED | PROCESS_UNLOCK)) {
		test_msg("There were unlocked pages in the range\n");
		goto out_bits;
	}
	unlock_extent(&tmp, start, end);
	/* locked_page was unlocked above */
	put_page(locked_page);

	/*
	 * Test this scenario
	 * |--- delalloc ---|
	 *                    |--- search ---|
	 */
	test_start = max_bytes + sectorsize;
	locked_page = find_lock_page(inode->i_mapping, test_start >>
				     PAGE_SHIFT);
	if (!locked_page) {
		test_msg("Couldn't find the locked page\n");
		goto out_bits;
	}
	start = test_start;
	end = 0;
	found = find_lock_delalloc_range(inode, &tmp, locked_page, &start,
					 &end, max_bytes);
	if (found) {
		test_msg("Found range when we shouldn't have\n");
		goto out_bits;
	}
	if (end != (u64)-1) {
		test_msg("Did not return the proper end offset\n");
		goto out_bits;
	}

	/*
	 * Test this scenario
	 * [------- delalloc -------|
	 * [max_bytes]|-- search--|
	 *
	 * We are re-using our test_start from above since it works out well.
	 */
	set_extent_delalloc(&tmp, max_bytes, total_dirty - 1, NULL);
	start = test_start;
	end = 0;
	found = find_lock_delalloc_range(inode, &tmp, locked_page, &start,
					 &end, max_bytes);
	if (!found) {
		test_msg("Didn't find our range\n");
		goto out_bits;
	}
	if (start != test_start || end != total_dirty - 1) {
		test_msg("Expected start %Lu end %Lu, got start %Lu end %Lu\n",
			 test_start, total_dirty - 1, start, end);
		goto out_bits;
	}
	if (process_page_range(inode, start, end,
			       PROCESS_TEST_LOCKED | PROCESS_UNLOCK)) {
		test_msg("Pages in range were not all locked\n");
		goto out_bits;
	}
	unlock_extent(&tmp, start, end);

	/*
	 * Now to test where we run into a page that is no longer dirty in the
	 * range we want to find.
	 */
	page = find_get_page(inode->i_mapping,
			     (max_bytes + SZ_1M) >> PAGE_SHIFT);
	if (!page) {
		test_msg("Couldn't find our page\n");
		goto out_bits;
	}
	ClearPageDirty(page);
	put_page(page);

	/* We unlocked it in the previous test */
	lock_page(locked_page);
	start = test_start;
	end = 0;
	/*
	 * Currently if we fail to find dirty pages in the delalloc range we
	 * will adjust max_bytes down to PAGE_SIZE and then re-search.  If
	 * this changes at any point in the future we will need to fix this
	 * tests expected behavior.
	 */
	found = find_lock_delalloc_range(inode, &tmp, locked_page, &start,
					 &end, max_bytes);
	if (!found) {
		test_msg("Didn't find our range\n");
		goto out_bits;
	}
	if (start != test_start && end != test_start + PAGE_SIZE - 1) {
		test_msg("Expected start %Lu end %Lu, got start %Lu end %Lu\n",
			 test_start, test_start + PAGE_SIZE - 1, start,
			 end);
		goto out_bits;
	}
	if (process_page_range(inode, start, end, PROCESS_TEST_LOCKED |
			       PROCESS_UNLOCK)) {
		test_msg("Pages in range were not all locked\n");
		goto out_bits;
	}
	ret = 0;
out_bits:
	clear_extent_bits(&tmp, 0, total_dirty - 1, (unsigned)-1);
out:
	if (locked_page)
		put_page(locked_page);
	process_page_range(inode, 0, total_dirty - 1,
			   PROCESS_UNLOCK | PROCESS_RELEASE);
	iput(inode);
	return ret;
}

static int check_eb_bitmap(unsigned long *bitmap, struct extent_buffer *eb,
			   unsigned long len)
{
	unsigned long i;

	for (i = 0; i < len * BITS_PER_BYTE; i++) {
		int bit, bit1;

		bit = !!test_bit(i, bitmap);
		bit1 = !!extent_buffer_test_bit(eb, 0, i);
		if (bit1 != bit) {
			test_msg("Bits do not match\n");
			return -EINVAL;
		}

		bit1 = !!extent_buffer_test_bit(eb, i / BITS_PER_BYTE,
						i % BITS_PER_BYTE);
		if (bit1 != bit) {
			test_msg("Offset bits do not match\n");
			return -EINVAL;
		}
	}
	return 0;
}

static int __test_eb_bitmaps(unsigned long *bitmap, struct extent_buffer *eb,
			     unsigned long len)
{
	unsigned long i, j;
	u32 x;
	int ret;

	memset(bitmap, 0, len);
	memset_extent_buffer(eb, 0, 0, len);
	if (memcmp_extent_buffer(eb, bitmap, 0, len) != 0) {
		test_msg("Bitmap was not zeroed\n");
		return -EINVAL;
	}

	bitmap_set(bitmap, 0, len * BITS_PER_BYTE);
	extent_buffer_bitmap_set(eb, 0, 0, len * BITS_PER_BYTE);
	ret = check_eb_bitmap(bitmap, eb, len);
	if (ret) {
		test_msg("Setting all bits failed\n");
		return ret;
	}

	bitmap_clear(bitmap, 0, len * BITS_PER_BYTE);
	extent_buffer_bitmap_clear(eb, 0, 0, len * BITS_PER_BYTE);
	ret = check_eb_bitmap(bitmap, eb, len);
	if (ret) {
		test_msg("Clearing all bits failed\n");
		return ret;
	}

	/* Straddling pages test */
	if (len > PAGE_SIZE) {
		bitmap_set(bitmap,
			(PAGE_SIZE - sizeof(long) / 2) * BITS_PER_BYTE,
			sizeof(long) * BITS_PER_BYTE);
		extent_buffer_bitmap_set(eb, PAGE_SIZE - sizeof(long) / 2, 0,
					sizeof(long) * BITS_PER_BYTE);
		ret = check_eb_bitmap(bitmap, eb, len);
		if (ret) {
			test_msg("Setting straddling pages failed\n");
			return ret;
		}

		bitmap_set(bitmap, 0, len * BITS_PER_BYTE);
		bitmap_clear(bitmap,
			(PAGE_SIZE - sizeof(long) / 2) * BITS_PER_BYTE,
			sizeof(long) * BITS_PER_BYTE);
		extent_buffer_bitmap_set(eb, 0, 0, len * BITS_PER_BYTE);
		extent_buffer_bitmap_clear(eb, PAGE_SIZE - sizeof(long) / 2, 0,
					sizeof(long) * BITS_PER_BYTE);
		ret = check_eb_bitmap(bitmap, eb, len);
		if (ret) {
			test_msg("Clearing straddling pages failed\n");
			return ret;
		}
	}

	/*
	 * Generate a wonky pseudo-random bit pattern for the sake of not using
	 * something repetitive that could miss some hypothetical off-by-n bug.
	 */
	x = 0;
	bitmap_clear(bitmap, 0, len * BITS_PER_BYTE);
	extent_buffer_bitmap_clear(eb, 0, 0, len * BITS_PER_BYTE);
	for (i = 0; i < len * BITS_PER_BYTE / 32; i++) {
		x = (0x19660dULL * (u64)x + 0x3c6ef35fULL) & 0xffffffffU;
		for (j = 0; j < 32; j++) {
			if (x & (1U << j)) {
				bitmap_set(bitmap, i * 32 + j, 1);
				extent_buffer_bitmap_set(eb, 0, i * 32 + j, 1);
			}
		}
	}

	ret = check_eb_bitmap(bitmap, eb, len);
	if (ret) {
		test_msg("Random bit pattern failed\n");
		return ret;
	}

	return 0;
}

static int test_eb_bitmaps(u32 sectorsize, u32 nodesize)
{
	unsigned long len;
	unsigned long *bitmap;
	struct extent_buffer *eb;
	int ret;

	test_msg("Running extent buffer bitmap tests\n");

	/*
	 * In ppc64, sectorsize can be 64K, thus 4 * 64K will be larger than
	 * BTRFS_MAX_METADATA_BLOCKSIZE.
	 */
	len = (sectorsize < BTRFS_MAX_METADATA_BLOCKSIZE)
		? sectorsize * 4 : sectorsize;

	bitmap = kmalloc(len, GFP_KERNEL);
	if (!bitmap) {
		test_msg("Couldn't allocate test bitmap\n");
		return -ENOMEM;
	}

	eb = __alloc_dummy_extent_buffer(NULL, 0, len);
	if (!eb) {
		test_msg("Couldn't allocate test extent buffer\n");
		kfree(bitmap);
		return -ENOMEM;
	}

	ret = __test_eb_bitmaps(bitmap, eb, len);
	if (ret)
		goto out;

	/* Do it over again with an extent buffer which isn't page-aligned. */
	free_extent_buffer(eb);
	eb = __alloc_dummy_extent_buffer(NULL, nodesize / 2, len);
	if (!eb) {
		test_msg("Couldn't allocate test extent buffer\n");
		kfree(bitmap);
		return -ENOMEM;
	}

	ret = __test_eb_bitmaps(bitmap, eb, len);
out:
	free_extent_buffer(eb);
	kfree(bitmap);
	return ret;
}

int btrfs_test_extent_io(u32 sectorsize, u32 nodesize)
{
	int ret;

	test_msg("Running extent I/O tests\n");

	ret = test_find_delalloc(sectorsize);
	if (ret)
		goto out;

	ret = test_eb_bitmaps(sectorsize, nodesize);
out:
	test_msg("Extent I/O tests finished\n");
	return ret;
}
