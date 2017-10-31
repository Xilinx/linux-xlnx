/* $Id: bigdigits.h $ */

/** @file
    Interface to core BigDigits "mp" functions using fixed-length arrays
*/

/***** BEGIN LICENSE BLOCK *****
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2001-15 David Ireland, D.I. Management Services Pty Limited
 * <http://www.di-mgt.com.au/bigdigits.html>. All rights reserved.
  *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License Version 2, as described below:
 * 
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details
 *
 ***** END LICENSE BLOCK *****/
/*
 * Last updated:
 * $Date: 2015-10-22 10:23:00 $
 * $Revision: 2.5.0 $
 * $Author: dai $
 */

#ifndef BIGDIGITS_H_
#define BIGDIGITS_H_ 1

#include <linux/types.h>

/* Sizes to match */
#define MAX_DIGIT 0xFFFFFFFFUL
#define BITS_PER_DIGIT 32
#define HIBITMASK 0x80000000UL

volatile char *copyright_notice(void);
	/* Forces linker to include copyright notice in executable */
/** @endcond */

/** Computes w = u + v, returns carry */
u32 mpAdd(u32 w[], const u32 u[], const u32 v[], size_t ndigits);

/** Computes w = u - v, returns borrow */
u32 mpSubtract(u32 w[], const u32 u[], const u32 v[], size_t ndigits);

/** Computes product w = u * v
@param[out] w To receive the product, an array of size 2 x `ndigits`
@param[in] u An array of size `ndigits`
@param[in] v An array of size `ndigits`
@param[in] ndigits size of arrays `u` and `v`
@warning The product must be of size 2 x `ndigits`
*/
int mpMultiply(u32 w[], const u32 u[], const u32 v[], size_t ndigits);

/** Computes integer division of u by v such that u=qv+r
@param[out] q to receive quotient = u div v, an array of size `udigits`
@param[out] r to receive divisor = u mod v, an array of size `udigits`
@param[in]  u dividend of size `udigits`
@param[in] udigits size of arrays `q` `r` and `u`
@param[in]  v divisor of size `vdigits`
@param[in] vdigits size of array `v`
@warning Trashes q and r first
*/
int mpDivide(u32 q[], u32 r[], const u32 u[],
	size_t udigits, u32 v[], size_t vdigits);


/** Computes remainder r = u mod v
@param[out] r to receive divisor = u mod v, an array of size `vdigits`
@param[in]  u dividend of size `udigits`
@param[in] udigits size of arrays `r` and `u`
@param[in]  v divisor of size `vdigits`
@param[in] vdigits size of array `v`
@remark Note that `r` is `vdigits` long here, but is `udigits` long in mpDivide().
*/
int mpModulo(u32 r[], const u32 u[], size_t udigits, u32 v[], size_t vdigits);

/** Computes square w = x^2
@param[out] w array of size 2 x `ndigits` to receive square
@param[in] x array of size `ndigits`
@param[in] ndigits size of array `x`
@warning The product `w` must be of size 2 x `ndigits`
*/
int mpSquare(u32 w[], const u32 x[], size_t ndigits);

/** Returns true if a is zero, else false, using constant-time algorithm
 *  @remark Constant-time with respect to `ndigits`
 */
int mpIsZero(const u32 a[], size_t ndigits);

/*************************/
/* COMPARISON OPERATIONS */
/*************************/
/* [v2.5] Changed to constant-time algorithms */

/** Returns true if a == b, else false, using constant-time algorithm
 *  @remark Constant-time with respect to `ndigits`
 */
int mpEqual(const u32 a[], const u32 b[], size_t ndigits);

/** Returns sign of `(a-b)` as `{-1,0,+1}` using constant-time algorithm
 *  @remark Constant-time with respect to `ndigits`
 */
int mpCompare(const u32 a[], const u32 b[], size_t ndigits);

/** Returns true if a is zero, else false, using constant-time algorithm
 *  @remark Constant-time with respect to `ndigits`
 */
int mpIsZero(const u32 a[], size_t ndigits);

/** Computes a = (x * y) mod m */
int mpModMult(u32 a[], const u32 x[], const u32 y[], u32 m[], size_t ndigits);

/** Computes the inverse of `u` modulo `m`, inv = u^{-1} mod m */
int mpModInv(u32 inv[], const u32 u[], const u32 m[], size_t ndigits);

/** Returns number of significant bits in a */
size_t mpBitLength(const u32 a[], size_t ndigits);

/** Computes a = b << x */
u32 mpShiftLeft(u32 a[], const u32 b[], size_t x, size_t ndigits);

/** Computes a = b >> x */
u32 mpShiftRight(u32 a[], const u32 b[], size_t x, size_t ndigits);

/** Sets bit n of a (0..nbits-1) with value 1 or 0 */
int mpSetBit(u32 a[], size_t ndigits, size_t n, int value);

/** Sets a = 0 */
volatile u32 mpSetZero(volatile u32 a[], size_t ndigits);

/** Sets a = d where d is a single digit */
void mpSetDigit(u32 a[], u32 d, size_t ndigits);

/** Sets a = b */
void mpSetEqual(u32 a[], const u32 b[], size_t ndigits);

/** Returns value 1 or 0 of bit n (0..nbits-1) */
int mpGetBit(u32 a[], size_t ndigits, size_t n);

/** Returns number of significant non-zero digits in a */
size_t mpSizeof(const u32 a[], size_t ndigits);

/** Computes quotient q = u div d, returns remainder */
u32 mpShortDiv(u32 q[], const u32 u[], u32 d, size_t ndigits);

/** Returns sign of (a - d) where d is a single digit */
int mpShortCmp(const u32 a[], u32 d, size_t ndigits);

/** Computes p = x * y, where x and y are single digits */
int spMultiply(u32 p[2], u32 x, u32 y);

/** Computes quotient q = u div v, remainder r = u mod v, where q, r and v are single digits */
u32 spDivide(u32 *q, u32 *r, const u32 u[2], u32 v);

size_t mpConvFromOctets(u32 a[], size_t ndigits, const unsigned char *c, size_t nbytes);
/** Converts big digit a into string of octets, in big-endian order, padding to nbytes or truncating if necessary.
@returns number of non-zero octets required. */
size_t mpConvToOctets(const u32 a[], size_t ndigits, unsigned char *c, size_t nbytes);

/** Computes y = x^e mod m */
int mpModExp(u32 y[], const u32 x[], const u32 e[], u32 m[], size_t ndigits);

/** @endcond */

#endif	/* BIGDIGITS_H_ */
