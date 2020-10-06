// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */

#include <linux/bitmap.h>

#include "ai-engine-internal.h"

/**
 * aie_resource_initialize() - initialize AI engine resource
 * @res: pointer to AI engine resource
 * @count: total number of element of this resource
 * @return: 0 for success, negative value for failure.
 *
 * This function will initialize the data structure for the
 * resource.
 */
int aie_resource_initialize(struct aie_resource *res, int count)
{
	if (!res || !count)
		return -EINVAL;
	res->bitmap = bitmap_zalloc(count, GFP_KERNEL);
	if (!res->bitmap)
		return -ENOMEM;
	res->total = count;

	return 0;
}

/**
 * aie_resource_uninitialize() - uninitialize AI engine resource
 * @res: pointer to AI engine resource
 *
 * This function will release the AI engine resource data members.
 */
void aie_resource_uninitialize(struct aie_resource *res)
{
	res->total = 0;
	if (res->bitmap)
		bitmap_free(res->bitmap);
}

/**
 * aie_resource_check() - check availability of requested resource
 * @res: pointer to AI engine resource to check
 * @start: start index of the required resource, it will only be used if
 *	   @continuous is 1. It will check the available resource starting from
 *	   @start
 * @count: number of requested element
 * @return: start resource id if the requested number of resources are available
 *	    It will return negative value of errors.
 *
 * This function will check the availability. It will return start resource id
 * if the requested number of resources are available.
 */
int aie_resource_check_region(struct aie_resource *res,
			      u32 start, u32 count)
{
	unsigned long id;

	if (!res || !res->bitmap || !count)
		return -EINVAL;
	id = bitmap_find_next_zero_area(res->bitmap, res->total, start,
					count, 0);
	if (id >= res->total)
		return -ERANGE;

	return (int)id;
}

/**
 * aie_resource_get_region() - get requested AI engine resource
 * @res: pointer to AI engine resource to check
 * @count: number of requested element
 * @start: start index of the required resource
 * @return: start resource id for success, and negative value for failure.
 *
 * This function check if the requested AI engine resource is available.
 * If it is available, mark it used and return the start resource id.
 */
int aie_resource_get_region(struct aie_resource *res, u32 start, u32 count)
{
	unsigned long off;

	if (!res || !res->bitmap || !count)
		return -EINVAL;
	off = bitmap_find_next_zero_area(res->bitmap, res->total, start,
					 count, 0);
	if (off >= res->total) {
		pr_err("Failed to get available AI engine resource.\n");
		return -ERANGE;
	}
	bitmap_set(res->bitmap, off, count);

	return (int)off;
}

/**
 * aie_resource_put_region() - release requested AI engine resource
 * @res: pointer to AI engine resource to check
 * @start: start index of the resource to release
 * @count: number of elements to release
 *
 * This function release the requested AI engine resource.
 */
void aie_resource_put_region(struct aie_resource *res, int start, u32 count)
{
	if (!res || !count)
		return;
	bitmap_clear(res->bitmap, start, count);
}

/**
 * aie_resource_set() - set the AI engine resource bits
 * @res: pointer to AI engine resource
 * @start: start bit to set
 * @count: number of bits to set
 * @return: 0 for success and negative value for failure
 *
 * This function sets the specified number bits in the resource.
 */
int aie_resource_set(struct aie_resource *res, u32 start, u32 count)
{
	if (!res || !res->bitmap || !count || start + count > res->total)
		return -EINVAL;

	bitmap_set(res->bitmap, start, count);
	return 0;
}

/**
 * aie_resource_cpy_from_arr32() - copies nbits from u32[] to bitmap.
 * @res: pointer to AI engine resource
 * @start: start bit in bitmap
 * @src: source buffer
 * @nbits: number of bits to copy from u32[]
 * @return: 0 for success and negative value for failure
 */
int aie_resource_cpy_from_arr32(struct aie_resource *res, u32 start,
				const u32 *src, u32 nbits)
{
	if (!res || !res->bitmap || !nbits || start + nbits  > res->total ||
	    !src)
		return -EINVAL;

	bitmap_from_arr32(res->bitmap + BIT_WORD(start), src, nbits);
	return 0;
}

/**
 * aie_resource_cpy_to_arr32() - copies nbits to u32[] from bitmap.
 * @res: pointer to AI engine resource
 * @start: start bit in bitmap
 * @dst: destination buffer
 * @nbits: number of bits to copy to u32[]
 * @return: 0 for success and negative value for failure
 */
int aie_resource_cpy_to_arr32(struct aie_resource *res, u32 start, u32 *dst,
			      u32 nbits)
{
	if (!res || !res->bitmap || !nbits || start + nbits  > res->total ||
	    !dst)
		return -EINVAL;

	bitmap_to_arr32(dst, res->bitmap + BIT_WORD(start), nbits);
	return 0;
}

/**
 * aie_resource_clear() - clear the AI engine resource bits
 * @res: pointer to AI engine resource
 * @start: start bit to set
 * @count: number of bits to clear
 * @return: 0 for success and negative value for failure
 *
 * This function clears the specified number bits in the resource.
 */
int aie_resource_clear(struct aie_resource *res, u32 start, u32 count)
{
	if (!res || !res->bitmap || !count || start + count > res->total)
		return -EINVAL;

	bitmap_clear(res->bitmap, start, count);
	return 0;
}

/**
 * aie_resource_clear_all() - clear all the AI engine resource bits
 * @res: pointer to AI engine resource
 * @return: 0 for success and negative value for failure
 *
 * This function clears all the bits in the resource.
 */
int aie_resource_clear_all(struct aie_resource *res)
{
	if (!res || !res->bitmap)
		return -EINVAL;

	bitmap_clear(res->bitmap, 0, res->total);
	return 0;
}

/**
 * aie_resource_testbit() - test if a bit is set in a AI engine resource
 * @res: pointer to AI engine resource
 * @bit: bit to check
 * @return: true for set, false for not set
 */
bool aie_resource_testbit(struct aie_resource *res, u32 bit)
{
	if (!res || !res->bitmap || bit >= res->total)
		return false;

	/* Locate the unsigned long the required bit belongs to */
	return test_bit(bit, res->bitmap);
}
