/* $Id: bigdigits.c $ */

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

#include "bigdigits.h"
#include <linux/types.h>
#include <linux/math64.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm-generic/bug.h>

#define assert(a) BUG_ON(!(a))

/* Useful definitions */
#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

/* Added in [v2.4] for ALLOC_BYTES and FREE_BYTES */
volatile uint8_t zeroise_bytes(volatile void *v, size_t n)
{	/* Zeroise byte array b and make sure optimiser does not ignore this */
	volatile uint8_t optdummy;
	volatile uint8_t *b = (uint8_t*)v;
	while(n--)
		b[n] = 0;
	optdummy = *b;
	return optdummy;
}

/* Force linker to include copyright notice in executable object image */
volatile char *copyright_notice(void)
{
	return
"Contains multiple-precision arithmetic code originally written by David Ireland,"
" copyright (c) 2001-15 by D.I. Management Services Pty Limited <www.di-mgt.com.au>.";
}

#define IS_NONZERO_DIGIT(x) (((x)|(~(x)+1)) >> (BITS_PER_DIGIT-1))
#define IS_ZER0_DIGIT(x) (1 ^ IS_NONZERO_DIGIT((x)))

/** Returns 1 if a == b, else 0 (constant-time) */
int mpEqual(const u32 a[], const u32 b[], size_t ndigits)
{
	u32 dif = 0;

	while (ndigits--) {
		dif |= a[ndigits] ^ b[ndigits];
	}

	return (IS_ZER0_DIGIT(dif));
}

/** Returns 1 if a == 0, else 0 (constant-time) */
int mpIsZero(const u32 a[], size_t ndigits)
{
	u32 dif = 0;
	const u32 ZERO = 0;

	while (ndigits--) {
		dif |= a[ndigits] ^ ZERO;
	}

	return (IS_ZER0_DIGIT(dif));
}

int spMultiply(u32 p[2], u32 x, u32 y)
{
	/* Use a 64-bit temp for product */
	uint64_t t = (uint64_t)x * (uint64_t)y;
	/* then split into two parts */
	p[1] = (u32)(t >> 32);
	p[0] = (u32)(t & 0xFFFFFFFF);

	return 0;
}

u32 spDivide(u32 *pq, u32 *pr, const u32 u[2], u32 v)
{
	uint64_t uu, q;
	uu = (uint64_t)u[1] << 32 | (uint64_t)u[0];
	q = div64_ul(uu, (uint64_t)v);
	//r = uu % (uint64_t)v;
	*pr = (u32)(uu - q * v);
	*pq = (u32)(q & 0xFFFFFFFF);
	return (u32)(q >> 32);
}

u32 mpAdd(u32 w[], const u32 u[], const u32 v[], size_t ndigits)
{
	/*	Calculates w = u + v
		where w, u, v are multiprecision integers of ndigits each
		Returns carry if overflow. Carry = 0 or 1.

		Ref: Knuth Vol 2 Ch 4.3.1 p 266 Algorithm A.
	*/

	u32 k;
	size_t j;

	assert(w != v);

	/* Step A1. Initialise */
	k = 0;

	for (j = 0; j < ndigits; j++)
	{
		/*	Step A2. Add digits w_j = (u_j + v_j + k)
			Set k = 1 if carry (overflow) occurs
		*/
		w[j] = u[j] + k;
		if (w[j] < k)
			k = 1;
		else
			k = 0;

		w[j] += v[j];
		if (w[j] < v[j])
			k++;

	}	/* Step A3. Loop on j */

	return k;	/* w_n = k */
}

u32 mpSubtract(u32 w[], const u32 u[], const u32 v[], size_t ndigits)
{
	/*	Calculates w = u - v where u >= v
		w, u, v are multiprecision integers of ndigits each
		Returns 0 if OK, or 1 if v > u.

		Ref: Knuth Vol 2 Ch 4.3.1 p 267 Algorithm S.
	*/

	u32 k;
	size_t j;

	assert(w != v);

	/* Step S1. Initialise */
	k = 0;

	for (j = 0; j < ndigits; j++)
	{
		/*	Step S2. Subtract digits w_j = (u_j - v_j - k)
			Set k = 1 if borrow occurs.
		*/
		w[j] = u[j] - k;
		if (w[j] > MAX_DIGIT - k)
			k = 1;
		else
			k = 0;

		w[j] -= v[j];
		if (w[j] > MAX_DIGIT - v[j])
			k++;

	}	/* Step S3. Loop on j */

	return k;	/* Should be zero if u >= v */
}

int mpMultiply(u32 w[], const u32 u[], const u32 v[], size_t ndigits)
{
	/*	Computes product w = u * v
		where u, v are multiprecision integers of ndigits each
		and w is a multiprecision integer of 2*ndigits

		Ref: Knuth Vol 2 Ch 4.3.1 p 268 Algorithm M.
	*/

	u32 k, t[2];
	size_t i, j, m, n;

	assert(w != u && w != v);

	m = n = ndigits;

	/* Step M1. Initialise */
	for (i = 0; i < 2 * m; i++)
		w[i] = 0;

	for (j = 0; j < n; j++)
	{
		/* Step M2. Zero multiplier? */
		if (v[j] == 0)
		{
			w[j + m] = 0;
		}
		else
		{
			/* Step M3. Initialise i */
			k = 0;
			for (i = 0; i < m; i++)
			{
				/* Step M4. Multiply and add */
				/* t = u_i * v_j + w_(i+j) + k */
				spMultiply(t, u[i], v[j]);

				t[0] += k;
				if (t[0] < k)
					t[1]++;
				t[0] += w[i+j];
				if (t[0] < w[i+j])
					t[1]++;

				w[i+j] = t[0];
				k = t[1];
			}
			/* Step M5. Loop on i, set w_(j+m) = k */
			w[j+m] = k;
		}
	}	/* Step M6. Loop on j */

	return 0;
}

static u32 mpMultSub(u32 wn, u32 w[], const u32 v[],
					   u32 q, size_t n)
{	/*	Compute w = w - qv
		where w = (WnW[n-1]...W[0])
		return modified Wn.
	*/
	u32 k, t[2];
	size_t i;

	if (q == 0)	/* No change */
		return wn;

	k = 0;

	for (i = 0; i < n; i++)
	{
		spMultiply(t, q, v[i]);
		w[i] -= k;
		if (w[i] > MAX_DIGIT - k)
			k = 1;
		else
			k = 0;
		w[i] -= t[0];
		if (w[i] > MAX_DIGIT - t[0])
			k++;
		k += t[1];
	}

	/* Cope with Wn not stored in array w[0..n-1] */
	wn -= k;

	return wn;
}

static int QhatTooBig(u32 qhat, u32 rhat,
					  u32 vn2, u32 ujn2)
{	/*	Returns true if Qhat is too big
		i.e. if (Qhat * Vn-2) > (b.Rhat + Uj+n-2)
	*/
	u32 t[2];

	spMultiply(t, qhat, vn2);
	if (t[1] < rhat)
		return 0;
	else if (t[1] > rhat)
		return 1;
	else if (t[0] > ujn2)
		return 1;

	return 0;
}

int mpDivide(u32 q[], u32 r[], const u32 u[],
	size_t udigits, u32 v[], size_t vdigits)
{	/*	Computes quotient q = u / v and remainder r = u mod v
		where q, r, u are multiple precision digits
		all of udigits and the divisor v is vdigits.

		Ref: Knuth Vol 2 Ch 4.3.1 p 272 Algorithm D.

		Do without extra storage space, i.e. use r[] for
		normalised u[], unnormalise v[] at end, and cope with
		extra digit Uj+n added to u after normalisation.

		WARNING: this trashes q and r first, so cannot do
		u = u / v or v = u mod v.
		It also changes v temporarily so cannot make it const.
	*/
	size_t shift;
	int n, m, j;
	u32 bitmask, overflow;
	u32 qhat, rhat, t[2];
	u32 *uu, *ww;
	int qhatOK, cmp;

	/* Clear q and r */
	mpSetZero(q, udigits);
	mpSetZero(r, udigits);

	/* Work out exact sizes of u and v */
	n = (int)mpSizeof(v, vdigits);
	m = (int)mpSizeof(u, udigits);
	m -= n;

	/* Catch special cases */
	if (n == 0)
		return -1;	/* Error: divide by zero */

	if (n == 1)
	{	/* Use short division instead */
		r[0] = mpShortDiv(q, u, v[0], udigits);
		return 0;
	}

	if (m < 0)
	{	/* v > u, so just set q = 0 and r = u */
		mpSetEqual(r, u, udigits);
		return 0;
	}

	if (m == 0)
	{	/* u and v are the same length */
		cmp = mpCompare(u, v, (size_t)n);
		if (cmp < 0)
		{	/* v > u, as above */
			mpSetEqual(r, u, udigits);
			return 0;
		}
		else if (cmp == 0)
		{	/* v == u, so set q = 1 and r = 0 */
			mpSetDigit(q, 1, udigits);
			return 0;
		}
	}

	/*	In Knuth notation, we have:
		Given
		u = (Um+n-1 ... U1U0)
		v = (Vn-1 ... V1V0)
		Compute
		q = u/v = (QmQm-1 ... Q0)
		r = u mod v = (Rn-1 ... R1R0)
	*/

	/*	Step D1. Normalise */
	/*	Requires high bit of Vn-1
		to be set, so find most signif. bit then shift left,
		i.e. d = 2^shift, u' = u * d, v' = v * d.
	*/
	bitmask = HIBITMASK;
	for (shift = 0; shift < BITS_PER_DIGIT; shift++)
	{
		if (v[n-1] & bitmask)
			break;
		bitmask >>= 1;
	}

	/* Normalise v in situ - NB only shift non-zero digits */
	overflow = mpShiftLeft(v, v, shift, n);

	/* Copy normalised dividend u*d into r */
	overflow = mpShiftLeft(r, u, shift, n + m);
	uu = r;	/* Use ptr to keep notation constant */

	t[0] = overflow;	/* Extra digit Um+n */

	/* Step D2. Initialise j. Set j = m */
	for (j = m; j >= 0; j--)
	{
		/* Step D3. Set Qhat = [(b.Uj+n + Uj+n-1)/Vn-1]
		   and Rhat = remainder */
		qhatOK = 0;
		t[1] = t[0];	/* This is Uj+n */
		t[0] = uu[j+n-1];
		overflow = spDivide(&qhat, &rhat, t, v[n-1]);

		/* Test Qhat */
		if (overflow)
		{	/* Qhat == b so set Qhat = b - 1 */
			qhat = MAX_DIGIT;
			rhat = uu[j+n-1];
			rhat += v[n-1];
			if (rhat < v[n-1])	/* Rhat >= b, so no re-test */
				qhatOK = 1;
		}
		/* [VERSION 2: Added extra test "qhat && "] */
		if (qhat && !qhatOK && QhatTooBig(qhat, rhat, v[n-2], uu[j+n-2]))
		{	/* If Qhat.Vn-2 > b.Rhat + Uj+n-2
			   decrease Qhat by one, increase Rhat by Vn-1
			*/
			qhat--;
			rhat += v[n-1];
			/* Repeat this test if Rhat < b */
			if (!(rhat < v[n-1]))
				if (QhatTooBig(qhat, rhat, v[n-2], uu[j+n-2]))
					qhat--;
		}


		/* Step D4. Multiply and subtract */
		ww = &uu[j];
		overflow = mpMultSub(t[1], ww, v, qhat, (size_t)n);

		/* Step D5. Test remainder. Set Qj = Qhat */
		q[j] = qhat;
		if (overflow)
		{	/* Step D6. Add back if D4 was negative */
			q[j]--;
			overflow = mpAdd(ww, ww, v, (size_t)n);
		}

		t[0] = uu[j+n-1];	/* Uj+n on next round */

	}	/* Step D7. Loop on j */

	/* Clear high digits in uu */
	for (j = n; j < m+n; j++)
		uu[j] = 0;

	/* Step D8. Unnormalise. */

	mpShiftRight(r, r, shift, n);
	mpShiftRight(v, v, shift, n);

	return 0;
}

int mpSquare(u32 w[], const u32 x[], size_t ndigits)
/* New in Version 2.0 */
{
	/*	Computes square w = x * x
		where x is a multiprecision integer of ndigits
		and w is a multiprecision integer of 2*ndigits

		Ref: Menezes p596 Algorithm 14.16 with errata.
	*/

	u32 k, p[2], u[2], cbit, carry;
	size_t i, j, t, i2, cpos;

	assert(w != x);

	t = ndigits;

	/* 1. For i from 0 to (2t-1) do: w_i = 0 */
	i2 = t << 1;
	for (i = 0; i < i2; i++)
		w[i] = 0;

	carry = 0;
	cpos = i2-1;
	/* 2. For i from 0 to (t-1) do: */
	for (i = 0; i < t; i++)
	{
		/* 2.1 (uv) = w_2i + x_i * x_i, w_2i = v, c = u
		   Careful, w_2i may be double-prec
		*/
		i2 = i << 1; /* 2*i */
		spMultiply(p, x[i], x[i]);
		p[0] += w[i2];
		if (p[0] < w[i2])
			p[1]++;
		k = 0;	/* p[1] < b, so no overflow here */
		if (i2 == cpos && carry)
		{
			p[1] += carry;
			if (p[1] < carry)
				k++;
			carry = 0;
		}
		w[i2] = p[0];
		u[0] = p[1];
		u[1] = k;

		/* 2.2 for j from (i+1) to (t-1) do:
		   (uv) = w_{i+j} + 2x_j * x_i + c,
		   w_{i+j} = v, c = u,
		   u is double-prec
		   w_{i+j} is dbl if [i+j] == cpos
		*/
		k = 0;
		for (j = i+1; j < t; j++)
		{
			/* p = x_j * x_i */
			spMultiply(p, x[j], x[i]);
			/* p = 2p <=> p <<= 1 */
			cbit = (p[0] & HIBITMASK) != 0;
			k =  (p[1] & HIBITMASK) != 0;
			p[0] <<= 1;
			p[1] <<= 1;
			p[1] |= cbit;
			/* p = p + c */
			p[0] += u[0];
			if (p[0] < u[0])
			{
				p[1]++;
				if (p[1] == 0)
					k++;
			}
			p[1] += u[1];
			if (p[1] < u[1])
				k++;
			/* p = p + w_{i+j} */
			p[0] += w[i+j];
			if (p[0] < w[i+j])
			{
				p[1]++;
				if (p[1] == 0)
					k++;
			}
			if ((i+j) == cpos && carry)
			{	/* catch overflow from last round */
				p[1] += carry;
				if (p[1] < carry)
					k++;
				carry = 0;
			}
			/* w_{i+j} = v, c = u */
			w[i+j] = p[0];
			u[0] = p[1];
			u[1] = k;
		}
		/* 2.3 w_{i+t} = u */
		w[i+t] = u[0];
		/* remember overflow in w_{i+t} */
		carry = u[1];
		cpos = i+t;
	}

	/* (NB original step 3 deleted in Menezes errata) */

	/* Return w */

	return 0;
}

/** Returns sign of (a - b) as 0, +1 or -1 (constant-time) */
int mpCompare(const u32 a[], const u32 b[], size_t ndigits)
{
	/* All these vars are either 0 or 1 */
	unsigned int gt = 0;
	unsigned int lt = 0;
	unsigned int mask = 1;	/* Set to zero once first inequality found */
	unsigned int c;

	while (ndigits--) {
		gt |= (a[ndigits] > b[ndigits]) & mask;
		lt |= (a[ndigits] < b[ndigits]) & mask;
		c = (gt | lt);
		mask &= (c-1);	/* Unchanged if c==0 or mask==0, else mask=0 */
	}

	return (int)gt - (int)lt;	/* EQ=0 GT=+1 LT=-1 */
}

/** Returns number of significant digits in a */
size_t mpSizeof(const u32 a[], size_t ndigits)
{
	while(ndigits--)
	{
		if (a[ndigits] != 0)
			return (++ndigits);
	}
	return 0;
}

size_t mpBitLength(const u32 d[], size_t ndigits)
/* Returns no of significant bits in d */
{
	size_t n, i, bits;
	u32 mask;

	if (!d || ndigits == 0)
		return 0;

	n = mpSizeof(d, ndigits);
	if (0 == n) return 0;

	for (i = 0, mask = HIBITMASK; mask > 0; mask >>= 1, i++)
	{
		if (d[n-1] & mask)
			break;
	}

	bits = n * BITS_PER_DIGIT - i;

	return bits;
}

void mpSetEqual(u32 a[], const u32 b[], size_t ndigits)
{	/* Sets a = b */
	size_t i;

	for (i = 0; i < ndigits; i++)
	{
		a[i] = b[i];
	}
}

volatile u32 mpSetZero(volatile u32 a[], size_t ndigits)
{	/* Sets a = 0 */

	/* Prevent optimiser ignoring this */
	volatile u32 optdummy;
	volatile u32 *p = a;

	while (ndigits--)
		a[ndigits] = 0;

	optdummy = *p;
	return optdummy;
}

int mpGetBit(u32 a[], size_t ndigits, size_t ibit)
	/* Returns value 1 or 0 of bit n (0..nbits-1); or -1 if out of range */
{
	size_t idigit, bit_to_get;
	u32 mask;

	/* Which digit? (0-based) */
	idigit = ibit / BITS_PER_DIGIT;
	if (idigit >= ndigits)
		return -1;

	/* Set mask */
	bit_to_get = ibit % BITS_PER_DIGIT;
	mask = 0x01 << bit_to_get;

	return ((a[idigit] & mask) ? 1 : 0);
}

void mpSetDigit(u32 a[], u32 d, size_t ndigits)
{	/* Sets a = d where d is a single digit */
	size_t i;

	for (i = 1; i < ndigits; i++)
	{
		a[i] = 0;
	}
	a[0] = d;
}

u32 mpShiftLeft(u32 a[], const u32 *b,
	size_t shift, size_t ndigits)
{	/* Computes a = b << shift */
	/* [v2.1] Modified to cope with shift > BITS_PERDIGIT */
	size_t i, y, nw, bits;
	u32 mask, carry, nextcarry;

	/* Do we shift whole digits? */
	if (shift >= BITS_PER_DIGIT)
	{
		nw = shift / BITS_PER_DIGIT;
		i = ndigits;
		while (i--)
		{
			if (i >= nw)
				a[i] = b[i-nw];
			else
				a[i] = 0;
		}
		/* Call again to shift bits inside digits */
		bits = shift % BITS_PER_DIGIT;
		carry = b[ndigits-nw] << bits;
		if (bits)
			carry |= mpShiftLeft(a, a, bits, ndigits);
		return carry;
	}
	else
	{
		bits = shift;
	}

	/* Construct mask = high bits set */
	mask = ~(~(u32)0 >> bits);

	y = BITS_PER_DIGIT - bits;
	carry = 0;
	for (i = 0; i < ndigits; i++)
	{
		nextcarry = (b[i] & mask) >> y;
		a[i] = b[i] << bits | carry;
		carry = nextcarry;
	}

	return carry;
}

u32 mpShiftRight(u32 a[], const u32 b[], size_t shift, size_t ndigits)
{	/* Computes a = b >> shift */
	/* [v2.1] Modified to cope with shift > BITS_PERDIGIT */
	size_t i, y, nw, bits;
	u32 mask, carry, nextcarry;

	/* Do we shift whole digits? */
	if (shift >= BITS_PER_DIGIT)
	{
		nw = shift / BITS_PER_DIGIT;
		for (i = 0; i < ndigits; i++)
		{
			if ((i+nw) < ndigits)
				a[i] = b[i+nw];
			else
				a[i] = 0;
		}
		/* Call again to shift bits inside digits */
		bits = shift % BITS_PER_DIGIT;
		carry = b[nw-1] >> bits;
		if (bits)
			carry |= mpShiftRight(a, a, bits, ndigits);
		return carry;
	}
	else
	{
		bits = shift;
	}

	/* Construct mask to set low bits */
	/* (thanks to Jesse Chisholm for suggesting this improved technique) */
	mask = ~(~(u32)0 << bits);

	y = BITS_PER_DIGIT - bits;
	carry = 0;
	i = ndigits;
	while (i--)
	{
		nextcarry = (b[i] & mask) << y;
		a[i] = b[i] >> bits | carry;
		carry = nextcarry;
	}

	return carry;
}

u32 mpShortDiv(u32 q[], const u32 u[], u32 v,
				   size_t ndigits)
{
	/*	Calculates quotient q = u div v
		Returns remainder r = u mod v
		where q, u are multiprecision integers of ndigits each
		and r, v are single precision digits.

		Makes no assumptions about normalisation.

		Ref: Knuth Vol 2 Ch 4.3.1 Exercise 16 p625
	*/
	size_t j;
	u32 t[2], r;
	size_t shift;
	u32 bitmask, overflow, *uu;

	if (ndigits == 0) return 0;
	if (v == 0)	return 0;	/* Divide by zero error */

	/*	Normalise first */
	/*	Requires high bit of V
		to be set, so find most signif. bit then shift left,
		i.e. d = 2^shift, u' = u * d, v' = v * d.
	*/
	bitmask = HIBITMASK;
	for (shift = 0; shift < BITS_PER_DIGIT; shift++)
	{
		if (v & bitmask)
			break;
		bitmask >>= 1;
	}

	v <<= shift;
	overflow = mpShiftLeft(q, u, shift, ndigits);
	uu = q;

	/* Step S1 - modified for extra digit. */
	r = overflow;	/* New digit Un */
	j = ndigits;
	while (j--)
	{
		/* Step S2. */
		t[1] = r;
		t[0] = uu[j];
		overflow = spDivide(&q[j], &r, t, v);
	}

	/* Unnormalise */
	r >>= shift;

	return r;
}

/** Returns sign of (a - d) where d is a single digit */
int mpShortCmp(const u32 a[], u32 d, size_t ndigits)
{
	size_t i;
	int gt = 0;
	int lt = 0;

	/* Zero-length a => a is zero */
	if (ndigits == 0) return (d ? -1 : 0);

	/* If |a| > 1 then a > d */
	for (i = 1; i < ndigits; i++) {
		if (a[i] != 0)
			return 1;	/* GT */
	}

	lt = (a[0] < d);
	gt = (a[0] > d);

	return gt - lt;	/* EQ=0 GT=+1 LT=-1 */
}

int mpModulo(u32 r[], const u32 u[], size_t udigits,
			 u32 v[], size_t vdigits)
{
	/*	Computes r = u mod v
		where r, v are multiprecision integers of length vdigits
		and u is a multiprecision integer of length udigits.
		r may overlap v.

		Note that r here is only vdigits long,
		whereas in mpDivide it is udigits long.

		Use remainder from mpDivide function.
	*/
	u32 *qq, *rr;

	size_t nn = max(udigits, vdigits);
	qq = kzalloc(udigits * sizeof(u32), GFP_KERNEL);
	BUG_ON(!qq);
	rr = kzalloc(nn * sizeof(u32), GFP_KERNEL);
	BUG_ON(!rr);

	/* rr[nn] = u mod v */
	mpDivide(qq, rr, u, udigits, v, vdigits);

	/* Final r is only vdigits long */
	mpSetEqual(r, rr, vdigits);

	kzfree(rr);
	kzfree(qq);

	return 0;
}

int mpModMult(u32 a[], const u32 x[], const u32 y[],
			  u32 m[], size_t ndigits)
{	/*	Computes a = (x * y) mod m */

	/* Double-length temp variable p */
	u32 *p;
	p = kzalloc(ndigits * 2 * sizeof(u32), GFP_KERNEL);
	BUG_ON(!p);

	/* Calc p[2n] = x * y */
	mpMultiply(p, x, y, ndigits);

	/* Then modulo (NOTE: a is OK at only ndigits long) */
	mpModulo(a, p, ndigits * 2, m, ndigits);

	kzfree(p);

	return 0;
}

int mpModInv(u32 inv[], const u32 u[], const u32 v[], size_t ndigits)
{	/*	Computes inv = u^(-1) mod v */
	/*	Ref: Knuth Algorithm X Vol 2 p 342
		ignoring u2, v2, t2
		and avoiding negative numbers.
		Returns non-zero if inverse undefined.
	*/
	int bIterations;
	int result;

	/* 1 * ndigits each, except w = 2 * ndigits */
	u32 *u1, *u3, *v1, *v3, *t1, *t3, *q, *w;
	/* allocate 9 * ndigits */
	u1 = kzalloc(9 * ndigits * sizeof(u32), GFP_KERNEL);
	BUG_ON(!u1);
	u3 = &u1[1 * ndigits];
	v1 = &u1[2 * ndigits];
	v3 = &u1[3 * ndigits];
	t1 = &u1[4 * ndigits];
	t3 = &u1[5 * ndigits];
	q  = &u1[6 * ndigits];
	/* 2 * ndigits each */
	w =  &u1[7 * ndigits];

	/* Step X1. Initialise */
	mpSetDigit(u1, 1, ndigits);		/* u1 = 1 */
	mpSetEqual(u3, u, ndigits);		/* u3 = u */
	mpSetZero(v1, ndigits);			/* v1 = 0 */
	mpSetEqual(v3, v, ndigits);		/* v3 = v */

	bIterations = 1;	/* Remember odd/even iterations */
	while (!mpIsZero(v3, ndigits))		/* Step X2. Loop while v3 != 0 */
	{					/* Step X3. Divide and "Subtract" */
		mpDivide(q, t3, u3, ndigits, v3, ndigits);
						/* q = u3 / v3, t3 = u3 % v3 */
		mpMultiply(w, q, v1, ndigits);	/* w = q * v1 */
		mpAdd(t1, u1, w, ndigits);		/* t1 = u1 + w */

		/* Swap u1 = v1; v1 = t1; u3 = v3; v3 = t3 */
		mpSetEqual(u1, v1, ndigits);
		mpSetEqual(v1, t1, ndigits);
		mpSetEqual(u3, v3, ndigits);
		mpSetEqual(v3, t3, ndigits);

		bIterations = -bIterations;
	}

	if (bIterations < 0)
		mpSubtract(inv, v, u1, ndigits);	/* inv = v - u1 */
	else
		mpSetEqual(inv, u1, ndigits);	/* inv = u1 */

	/* Make sure u3 = gcd(u,v) == 1 */
	if (mpShortCmp(u3, 1, ndigits) != 0)
	{
		result = 1;
		mpSetZero(inv, ndigits);
	}
	else
		result = 0;

	/* Clear up */
	kzfree(u1);

	return result;
}

size_t mpConvFromOctets(u32 a[], size_t ndigits, const unsigned char *c, size_t nbytes)
/* Converts nbytes octets into big digit a of max size ndigits
   Returns actual number of digits set (may be larger than mpSizeof)
*/
{
	size_t i;
	int j, k;
	u32 t;

	mpSetZero(a, ndigits);

	/* Read in octets, least significant first */
	/* i counts into big_d, j along c, and k is # bits to shift */
	for (i = 0, j = (int)nbytes - 1; i < ndigits && j >= 0; i++)
	{
		t = 0;
		for (k = 0; j >= 0 && k < BITS_PER_DIGIT; j--, k += 8)
			t |= ((u32)c[j]) << k;
		a[i] = t;
	}

	return i;
}

size_t mpConvToOctets(const u32 a[], size_t ndigits, unsigned char *c, size_t nbytes)
/* Convert big digit a into string of octets, in big-endian order,
   padding on the left to nbytes or truncating if necessary.
   Return number of octets required excluding leading zero bytes.
*/
{
	int j, k, len;
	u32 t;
	size_t i, noctets, nbits;

	nbits = mpBitLength(a, ndigits);
	noctets = (nbits + 7) / 8;

	len = (int)nbytes;

	for (i = 0, j = len - 1; i < ndigits && j >= 0; i++)
	{
		t = a[i];
		for (k = 0; j >= 0 && k < BITS_PER_DIGIT; j--, k += 8)
			c[j] = (unsigned char)(t >> k);
	}

	for ( ; j >= 0; j--)
		c[j] = 0;

	return (size_t)noctets;
}

/* MACROS TO DO MODULAR SQUARING AND MULTIPLICATION USING PRE-ALLOCATED TEMPS */
/* Required lengths |y|=|t1|=|t2|=2*n, |m|=n; but final |y|=n */
/* Square: y = (y * y) mod m */
#define mpMODSQUARETEMP(y,m,n,t1,t2) do{mpSquare(t1,y,n);mpDivide(t2,y,t1,n*2,m,n);}while(0)
/* Mult:   y = (y * x) mod m */
#define mpMODMULTTEMP(y,x,m,n,t1,t2) do{mpMultiply(t1,x,y,n);mpDivide(t2,y,t1,n*2,m,n);}while(0)
/* Mult:   w = (y * x) mod m */
#define mpMODMULTXYTEMP(w,y,x,m,n,t1,t2) do{mpMultiply(t1,x,y,(n));mpDivide(t2,w,t1,(n)*2,m,(n));}while(0)

#define mpNEXTBITMASK(mask, n) do{if(mask==1){mask=HIBITMASK;n--;}else{mask>>=1;}}while(0)


int mpModExp(u32 yout[], const u32 x[], const u32 e[], u32 m[], size_t ndigits)
{	/*	Computes y = x^e mod m */
	/*	"Classic" binary left-to-right method */
	/*  [v2.2] removed const restriction on m[] to avoid using an extra alloc'd var
		(m is changed in-situ during the divide operation then restored) */
	u32 mask;
	size_t n;
	size_t nn = ndigits * 2;
	/* Create some double-length temps */

	u32 *t1, *t2, *y;
	t1 = kzalloc(nn * 3 * sizeof(u32), GFP_KERNEL);
	t2 = &t1[nn * 1];
	y  = &t1[nn * 2];

	assert(ndigits != 0);

	n = mpSizeof(e, ndigits);
	/* Catch e==0 => x^0=1 */
	if (0 == n)
	{
		mpSetDigit(yout, 1, ndigits);
		goto done;
	}
	/* Find second-most significant bit in e */
	for (mask = HIBITMASK; mask > 0; mask >>= 1)
	{
		if (e[n-1] & mask)
			break;
	}
	mpNEXTBITMASK(mask, n);

	/* Set y = x */
	mpSetEqual(y, x, ndigits);

	/* For bit j = k-2 downto 0 */
	while (n)
	{
		/* Square y = y * y mod n */
		mpMODSQUARETEMP(y, m, ndigits, t1, t2);
		if (e[n-1] & mask)
		{	/*	if e(j) == 1 then multiply
				y = y * x mod n */
			mpMODMULTTEMP(y, x, m, ndigits, t1, t2);
		}

		/* Move to next bit */
		mpNEXTBITMASK(mask, n);
	}

	/* Return y */
	mpSetEqual(yout, y, ndigits);

done:
	kzfree(t1);

	return 0;
}
