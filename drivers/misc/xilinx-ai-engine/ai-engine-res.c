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
 * aie_resource_check_region() - check availability of requested resource
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

/**
 * aie_resource_check_common_avail() - check common available bits
 *				       of two resources table
 * @res0: pointer to AI engine resource0
 * @res1: pointer to AI engine resource1
 * @sbit: start bit to check
 * @nbits: number of bits to check
 * @return: number of common bits, or negative value for failure
 */
int aie_resource_check_common_avail(struct aie_resource *res0,
				    struct aie_resource *res1,
				    u32 sbit, u32 nbits)
{
	u32 ebit, avails;

	if (!nbits || !res0 || !res1 || !res0->bitmap || !res1->bitmap ||
	    (sbit + nbits) > res0->total || (sbit + nbits) > res1->total)
		return -EINVAL;

	ebit = sbit + nbits - 1;
	avails = 0;
	while (sbit <= ebit) {
		unsigned long  *bitmap0, *bitmap1, tbits;
		u32 tlbit, lbit = sbit % BITS_PER_LONG;
		u32 lnbits = ebit - sbit + 1;

		if (lnbits + lbit > BITS_PER_LONG)
			lnbits = BITS_PER_LONG - lbit;

		bitmap0 = &res0->bitmap[sbit / BITS_PER_LONG];
		bitmap1 = &res1->bitmap[sbit / BITS_PER_LONG];
		bitmap_or(&tbits, bitmap0, bitmap1, BITS_PER_LONG);
		tlbit = lbit;
		while (tlbit < lbit + lnbits) {
			u32 b = bitmap_find_next_zero_area(&tbits,
							   BITS_PER_LONG, tlbit,
							   1, 0);
			if (b >= lbit + lnbits)
				break;
			avails++;
			tlbit = b + 1;
		}
		sbit += lnbits;
	};

	return avails;
}

/**
 * aie_resource_get_common_avail() - get common available bits
 *				     of two resources table
 * @rres: pointer to AI engine runtime resource, runtime resource bitmap will
 *	  be updated if the required resources are available.
 * @sres: pointer to AI engine static resource, static resource bitmap will
 *	  not be updated even if the required resources are available.
 * @sbit: start bit to check
 * @nbits: number of bits to get
 * @total: total number of bits to check
 * @rscs: resources array to return for resources ids
 * @return: number of allocated bits for success, negative value for failure
 */
int aie_resource_get_common_avail(struct aie_resource *rres,
				  struct aie_resource *sres,
				  u32 sbit, u32 nbits, u32 total,
				  struct aie_rsc *rscs)
{
	u32 ebit, tsbit, tnbits;

	if (!nbits || !rres || !sres || !rres->bitmap || !sres->bitmap ||
	    nbits > total || (sbit + total) > rres->total ||
	    (sbit + total) > sres->total)
		return -EINVAL;

	ebit = sbit + total - 1;
	tsbit = sbit;
	tnbits = 0;
	while (tsbit <= ebit && tnbits != nbits) {
		unsigned long *rbitmap, *sbitmap, tbits;
		u32 tlbit, lbit = tsbit % BITS_PER_LONG;
		u32 lnbits = ebit - tsbit + 1;

		if (lnbits + lbit > BITS_PER_LONG)
			lnbits = BITS_PER_LONG - lbit;

		rbitmap = &rres->bitmap[sbit / BITS_PER_LONG];
		sbitmap = &sres->bitmap[sbit / BITS_PER_LONG];
		bitmap_or(&tbits, rbitmap, sbitmap, BITS_PER_LONG);
		tlbit = lbit;
		while (tlbit < lbit + lnbits && tnbits != nbits) {
			u32 b = bitmap_find_next_zero_area(&tbits,
							   BITS_PER_LONG, tlbit,
							   1, 0);
			if (b >= lbit + lnbits)
				break;
			rscs[tnbits].id = tsbit - sbit + b - lbit;
			tnbits++;
			tlbit = b + 1;
		}
		tsbit += lnbits;
	};

	if (tnbits != nbits)
		return -EINVAL;

	while (tnbits--)
		aie_resource_set(rres, sbit + rscs[tnbits].id, 1);

	return nbits;
}

/**
 * aie_resource_check_pattern_region() - check availability of requested
 *					 contiguous resources of a pattern
 * @res: pointer to AI engine resource to check
 * @start: start index of the required resource
 *	   It will check a continuous block of available resource starting from
 *	   @start.
 * @end: end index to check
 * @count: number of requested element
 * @return: start resource id if the requested number of resources are available
 *	    It will return negative value of errors.
 *
 * This function will check the availability. It will return start resource id
 * if the requested number of resources are available.
 * The contiguous resources of a pattern is e.g.
 * @count is 0, the resources starting from @start needs to be 0,1; or 2,3 and
 * beyond
 */
int aie_resource_check_pattern_region(struct aie_resource *res,
				      u32 start, u32 end, u32 count)
{
	unsigned long id;
	u32 lstart;

	if (!res || !res->bitmap || !count)
		return -EINVAL;
	lstart = start;
	while (lstart < end) {
		id = bitmap_find_next_zero_area(res->bitmap, res->total, lstart,
						count, 0);
		if (id + count > end + 1)
			return -ERANGE;
		else if (!((id - lstart) % count))
			return (int)id;

		lstart += count;
	}

	return -ERANGE;
}

/**
 * aie_resource_check_common_pattern_region() - check common available region
 *						of two resources table
 * @res0: pointer to AI engine resource0
 * @res1: pointer to AI engine resource1
 * @sbit: start bit to check
 * @nbits: number of bits to check
 * @total: total number of bits to check
 * @return: start bit of common region if it is found, negative value for
 *	    failure
 */
int aie_resource_check_common_pattern_region(struct aie_resource *res0,
					     struct aie_resource *res1,
					     u32 sbit, u32 nbits, u32 total)
{
	int sbit0, sbit1;

	if (!nbits || !res0 || !res1 || !res0->bitmap || !res1->bitmap ||
	    nbits > total || (sbit + total) > res0->total ||
	    (sbit + total) > res1->total)
		return -EINVAL;

	sbit0 = aie_resource_check_pattern_region(res0, sbit,
						  sbit + total - 1, nbits);
	if (sbit0 < 0)
		return sbit0;

	if ((u32)sbit0 + nbits > sbit + total)
		return -EINVAL;

	sbit1 = aie_resource_check_pattern_region(res1, sbit0,
						  sbit0 + nbits - 1, nbits);
	if (sbit1 != sbit0)
		return -EINVAL;

	return sbit1;
}

/**
 * aie_resource_get_common_pattern_region() - get common available region
 *					      of two resources table
 * @res0: pointer to AI engine resource0
 * @res1: pointer to AI engine resource1
 * @sbit: start bit to check
 * @nbits: number of bits to get
 * @total: total number of bits to check
 * @rscs: resources array to return for resources ids
 * @return: start bit of the common region if it is found, negative value for
 *	    failure
 *
 * The common pattern region is a contiguous block of resources which needs
 * to be very number of @nbits.
 * e.g. if nbits is 2, the offset to the start bit @sbit of returned resources
 * needs to be: 0,1; 2,3 ...
 */
int aie_resource_get_common_pattern_region(struct aie_resource *res0,
					   struct aie_resource *res1,
					   u32 sbit, u32 nbits, u32 total,
					   struct aie_rsc *rscs)
{
	int rsbit, ret;

	rsbit = aie_resource_check_common_pattern_region(res0, res1, sbit,
							 nbits, total);
	if (rsbit < 0)
		return rsbit;

	ret = aie_resource_get_region(res0, rsbit, nbits);
	if (ret < 0)
		return ret;

	if (ret != rsbit) {
		aie_resource_put_region(res0, ret, nbits);
		return -EINVAL;
	}

	ret = aie_resource_get_region(res1, rsbit, nbits);
	if (ret < 0)
		return ret;

	if (ret != rsbit) {
		aie_resource_put_region(res0, rsbit, nbits);
		aie_resource_put_region(res1, ret, nbits);
		return -EINVAL;
	}

	if (rscs) {
		u32 i;

		for (i = 0; i < nbits; i++, rscs++)
			rscs->id = rsbit - sbit + i;
	}

	return rsbit;
}
