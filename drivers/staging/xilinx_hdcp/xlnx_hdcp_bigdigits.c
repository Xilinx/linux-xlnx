// SPDX-License-Identifier: GPL-2.0
// Id: bigdigits.c

/***** BEGIN LICENSE BLOCK *****
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2001-15 David Ireland, D.I. Management Services Pty Limited
 * <http://www.di-mgt.com.au/bigdigits.html>. All rights reserved.
 *
 ***** END LICENSE BLOCK *****/
/*
 * Last updated:
 * Date: 2015-10-22 10:23:00 $
 * Revision: 2.5.0
 * Author: dai
 */

/* Core code for BigDigits library "mp" functions */

/* Some part of Code for BigDigits is modified according to Xilinx standards */

#include <linux/slab.h>
#include <linux/xlnx/xlnx_hdcp_common.h>

#define MAX_DIGIT 0xFFFFFFFFUL
#define MAX_HALF_DIGIT 0xFFFFUL	/* NB 'L' */
#define XBITS_PER_DIGIT 32
#define XMP_HI_BIT_MASK 0x80000000UL
#define XBITS_PER_HALF_DIGIT (XBITS_PER_DIGIT / 2)
#define XBIGDIG_LOHALF(x) ((unsigned int)((x) & MAX_HALF_DIGIT))
#define XBIGDIG_HIHALF(x) ((unsigned int)((x) >> XBITS_PER_HALF_DIGIT & MAX_HALF_DIGIT))
#define XBIGDIG_TOHALF(x) ((unsigned int)((x) << XBITS_PER_HALF_DIGIT))

#define IS_NONZERO_DIGIT(x) \
({\
	typeof(x) _x = x;\
	((_x) | ((~(_x)) + 1)) >> (XBITS_PER_DIGIT - 1); \
})

#define IS_ZERO_DIGIT(x) (1 ^ IS_NONZERO_DIGIT((x)))

static void mp_next_bit_mask(unsigned int *mask, unsigned int *n)
{
	if ((*mask) == 1) {
		*mask = XMP_HI_BIT_MASK; (*n)--;
	} else {
		*mask >>= 1;
	}
}

static int sp_multiply(unsigned int p[2], unsigned int x, unsigned int y)
{
	unsigned int x0, y0, x1, y1;
	unsigned int t, u, carry;

	/*
	 *	Split each x,y into two halves
	 *	x = x0 + B*x1
	 *	y = y0 + B*y1
	 *	where B = 2^16, half the digit size
	 *	Product is
	 *	xy = x0y0 + B(x0y1 + x1y0) + B^2(x1y1)
	 */
	x0 = XBIGDIG_LOHALF(x);
	x1 = XBIGDIG_HIHALF(x);
	y0 = XBIGDIG_LOHALF(y);
	y1 = XBIGDIG_HIHALF(y);

	/* Calc low part - no carry */
	p[0] = x0 * y0;

	/* Calc middle part */
	t = x0 * y1;
	u = x1 * y0;
	t += u;
	if (t < u)
		carry = 1;
	else
		carry = 0;

	/*
	 *	This carry will go to high half of p[1]
	 *	+ high half of t into low half of p[1]
	 */
	carry = XBIGDIG_TOHALF(carry) + XBIGDIG_HIHALF(t);

	/* Add low half of t to high half of p[0] */
	t = XBIGDIG_TOHALF(t);
	p[0] += t;
	if (p[0] < t)
		carry++;

	p[1] = x1 * y1;
	p[1] += carry;

	return 0;
}

#define B (MAX_HALF_DIGIT + 1)

static void sp_mult_sub(unsigned int uu[2], unsigned int qhat,
			unsigned int v1, unsigned int v0)
{
	/*
	 *  Compute uu = uu - q(v1v0)
	 *  where uu = u3u2u1u0, u3 = 0
	 *  and u_n, v_n are all half-digits
	 *  even though v1, v2 are passed as full digits.
	 */
	unsigned int p0, p1, t;

	p0 = qhat * v0;
	p1 = qhat * v1;
	t = p0 + XBIGDIG_TOHALF(XBIGDIG_LOHALF(p1));
	uu[0] -= t;
	if (uu[0] > MAX_DIGIT - t)
		uu[1]--;	/* Borrow */
	uu[1] -= XBIGDIG_HIHALF(p1);
}

static unsigned int sp_divide(unsigned int *q, unsigned int *r,
			      const unsigned int u[2], unsigned int v)
{
	unsigned int qhat, rhat, t, v0, v1, u0, u1, u2, u3;
	unsigned int uu[2], q2;

	/* Check for normalisation */
	if (!(v & XMP_HI_BIT_MASK)) {	/* Stop if assert is working, else return error */
		/* assert(v & XMP_HI_BIT_MASK); */
		*q = *r = 0;
		return MAX_DIGIT;
	}

	/* Split up into half-digits */
	v0 = XBIGDIG_LOHALF(v);
	v1 = XBIGDIG_HIHALF(v);
	u0 = XBIGDIG_LOHALF(u[0]);
	u1 = XBIGDIG_HIHALF(u[0]);
	u2 = XBIGDIG_LOHALF(u[1]);
	u3 = XBIGDIG_HIHALF(u[1]);

	qhat = (u3 < v1 ? 0 : 1);
	if (qhat > 0) {	/* qhat is one, so no need to mult */
		rhat = u3 - v1;
		/* t = r.b + u2 */
		t = XBIGDIG_TOHALF(rhat) | u2;
		if (v0 > t)
			qhat--;
	}

	uu[1] = 0;		/* (u4) */
	uu[0] = u[1];	/* (u3u2) */
	if (qhat > 0) {
		/* (u4u3u2) -= qhat(v1v0) where u4 = 0 */
		sp_mult_sub(uu, qhat, v1, v0);
		if (XBIGDIG_HIHALF(uu[1]) != 0)	{	/* Add back */
			qhat--;
			uu[0] += v;
			uu[1] = 0;
		}
	}
	q2 = qhat;

	/* ROUND 2. Set j = 1 and calculate q1 */

	/*
	 * Estimate qhat = (u3u2) / v1
	 * then set (u3u2u1) -= qhat(v1v0)
	 */
	t = uu[0];
	qhat = t / v1;
	rhat = t - qhat * v1;
	/* Test on v0 */
	t = XBIGDIG_TOHALF(rhat) | u1;
	if (qhat == B || (qhat * v0 > t)) {
		qhat--;
		rhat += v1;
		t = XBIGDIG_TOHALF(rhat) | u1;
		if (rhat < B && (qhat * v0 > t))
			qhat--;
	}

	/*
	 * Multiply and subtract
	 * (u3u2u1)' = (u3u2u1) - qhat(v1v0)
	 */
	uu[1] = XBIGDIG_HIHALF(uu[0]);	/* (0u3) */
	uu[0] = XBIGDIG_TOHALF(XBIGDIG_LOHALF(uu[0])) | u1;	/* (u2u1) */
	sp_mult_sub(uu, qhat, v1, v0);
	if (XBIGDIG_HIHALF(uu[1]) != 0) {	/* Add back */
		qhat--;
		uu[0] += v;
		uu[1] = 0;
	}

	/* q1 = qhat */
	*q = XBIGDIG_TOHALF(qhat);

	/* ROUND 3. Set j = 0 and calculate q0 */
	/*
	 *	Estimate qhat = (u2u1) / v1
	 *	then set (u2u1u0) -= qhat(v1v0)
	 */
	t = uu[0];
	qhat = t / v1;
	rhat = t - qhat * v1;
	/* Test on v0 */
	t = XBIGDIG_TOHALF(rhat) | u0;
	if (qhat == B || (qhat * v0 > t)) {
		qhat--;
		rhat += v1;
		t = XBIGDIG_TOHALF(rhat) | u0;
		if (rhat < B && (qhat * v0 > t))
			qhat--;
	}

	/*
	 *	Multiply and subtract
	 *	(u2u1u0)" = (u2u1u0)' - qhat(v1v0)
	 */
	uu[1] = XBIGDIG_HIHALF(uu[0]);	/* (0u2) */
	uu[0] = XBIGDIG_TOHALF(XBIGDIG_LOHALF(uu[0])) | u0;	/* (u1u0) */
	sp_mult_sub(uu, qhat, v1, v0);
	if (XBIGDIG_HIHALF(uu[1]) != 0) {	/* Add back */
		qhat--;
		uu[0] += v;
		uu[1] = 0;
	}

	/* q0 = qhat */
	*q |= XBIGDIG_LOHALF(qhat);

	/* Remainder is in (u1u0) i.e. uu[0] */
	*r = uu[0];
	return q2;
}

unsigned int mp_add(unsigned int w[], const unsigned int u[], const unsigned int v[],
		    size_t ndigits)
{
	/*
	 *	Calculates w = u + v
	 *	where w, u, v are multiprecision integers of ndigits each
	 *	Returns carry if overflow. Carry = 0 or 1.
	 *	Ref: Knuth Vol 2 Ch 4.3.1 p 266 Algorithm A.
	 */

	unsigned int k;
	size_t j;

	k = 0;

	for (j = 0; j < ndigits; j++) {
		w[j] = u[j] + k;
		if (w[j] < k)
			k = 1;
		else
			k = 0;

		w[j] += v[j];
		if (w[j] < v[j])
			k++;
	}

	return k;
}

int mp_multiply(unsigned int w[], const unsigned int u[], const unsigned int v[], size_t ndigits)
{
	/*
	 *	Computes product w = u * v
	 *	where u, v are multiprecision integers of ndigits each
	 *	and w is a multiprecision integer of 2*ndigits
	 *	Ref: Knuth Vol 2 Ch 4.3.1 p 268 Algorithm M.
	 */

	unsigned int k, t[2];
	size_t i, j, m, n;

	m = ndigits;
	n = ndigits;

	/* Step M1. Initialise */
	for (i = 0; i < 2 * m; i++)
		w[i] = 0;

	for (j = 0; j < n; j++) {
		/* Step M2. Zero multiplier? */
		if (v[j] == 0) {
			w[j + m] = 0;
		} else {
			/* Step M3. Initialise i */
			k = 0;
			for (i = 0; i < m; i++) {
				/* Step M4. Multiply and add */
				/* t = u_i * v_j + w_(i+j) + k */
				sp_multiply(t, u[i], v[j]);

				t[0] += k;
				if (t[0] < k)
					t[1]++;
				t[0] += w[i + j];
				if (t[0] < w[i + j])
					t[1]++;

				w[i + j] = t[0];
				k = t[1];
			}
			/* Step M5. Loop on i, set w_(j+m) = k */
			w[j + m] = k;
		}
	}	/* Step M6. Loop on j */

	return 0;
}

static unsigned int mp_mult_sub(unsigned int wn, unsigned int w[],
				const unsigned int v[],
				unsigned int q, size_t n)
{
	/*
	 *	Compute w = w - qv
	 *	where w = (WnW[n-1]...W[0])
	 *	return modified Wn.
	 */
	unsigned int k, t[2];
	size_t i;

	if (q == 0)	/* No change */
		return wn;

	k = 0;

	for (i = 0; i < n; i++) {
		sp_multiply(t, q, v[i]);
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

static int qhat_too_big(unsigned int qhat, unsigned int rhat,
			unsigned int vn2, unsigned int ujn2)
{
	/*
	 *	Returns true if Qhat is too big
	 *	i.e. if (Qhat * Vn-2) > (b.Rhat + Uj+n-2)
	 */
	unsigned int t[2];

	sp_multiply(t, qhat, vn2);
	if (t[1] < rhat)
		return 0;
	else if (t[1] > rhat)
		return 1;
	else if (t[0] > ujn2)
		return 1;

	return 0;
}

/** Returns 1 if a == 0, else 0 (constant-time) */
static int mp_is_zero(const u32 a[], size_t ndigits)
{
	u32 dif = 0;
	const u32 ZERO = 0;

	while (ndigits--)
		dif |= a[ndigits] ^ ZERO;

	return IS_ZERO_DIGIT(dif);
}

static int mp_compare(const unsigned int a[], const unsigned int b[],
		      size_t ndigits)
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
		mask &= (c - 1);	/* Unchanged if c==0 or mask==0, else mask=0 */
	}

	return (int)gt - (int)lt;	/* EQ=0 GT=+1 LT=-1 */
}

static size_t mp_sizeof(const unsigned int a[], size_t ndigits)
{
	while (ndigits--) {
		if (a[ndigits] != 0)
			return (++ndigits);
	}
	return 0;
}

static void mp_set_equal(unsigned int a[], const unsigned int b[], size_t ndigits)
{
	/* Sets a = b */
	size_t i;

	for (i = 0; i < ndigits; i++)
		a[i] = b[i];
}

static unsigned int mp_set_zero(unsigned int a[], size_t ndigits)
{
	/* Sets a = 0 */

	/* Prevent optimiser ignoring this */
	unsigned int optdummy;
	unsigned int *p = a;

	while (ndigits--)
		a[ndigits] = 0;

	optdummy = *p;
	return optdummy;
}

static void mp_set_digit(unsigned int a[], unsigned int d, size_t ndigits)
{
	/* Sets a = d where d is a single digit */
	size_t i;

	for (i = 1; i < ndigits; i++)
		a[i] = 0;
	a[0] = d;
}

static unsigned int mp_short_div(unsigned int q[], const unsigned int u[], unsigned int v,
				 size_t ndigits)
{
	size_t j;
	unsigned int t[2], r;
	size_t shift;
	unsigned int bitmask, overflow, *uu;

	if (ndigits == 0)
		return 0;
	if (v == 0)
		return 0;

	bitmask = XMP_HI_BIT_MASK;
	for (shift = 0; shift < XBITS_PER_DIGIT; shift++) {
		if (v & bitmask)
			break;
		bitmask >>= 1;
	}

	v <<= shift;
	overflow = mp_shift_left(q, u, shift, ndigits);
	uu = q;

	/* Step S1 - modified for extra digit. */
	r = overflow;	/* New digit Un */
	j = ndigits;
	while (j--) {
		/* Step S2. */
		t[1] = r;
		t[0] = uu[j];
		overflow = sp_divide(&q[j], &r, t, v);
	}
	r >>= shift;

	return r;
}

static unsigned int mp_shift_right(unsigned int a[], const unsigned int b[], size_t shift,
				   size_t ndigits)
{
	/* Computes a = b >> shift */
	/* [v2.1] Modified to cope with shift > BITS_PERDIGIT */
	size_t i, y, nw, bits;
	unsigned int mask, carry, nextcarry;
	u8 shift_bit = 0;

	/* Do we shift whole digits? */
	if (shift >= XBITS_PER_DIGIT) {
		nw = shift / XBITS_PER_DIGIT;
		for (i = 0; i < ndigits; i++) {
			if ((i + nw) < ndigits)
				a[i] = b[i + nw];
			else
				a[i] = 0;
		}
		/* Call again to shift bits inside digits */
		bits = shift % XBITS_PER_DIGIT;
		carry = b[nw - 1] >> bits;
		if (bits)
			carry |= mp_shift_right(a, a, bits, ndigits);
		return carry;
	}
	bits = shift;
	/* Construct mask to set low bits */
	mask = ~(~(unsigned int)shift_bit << bits);

	y = XBITS_PER_DIGIT - bits;
	carry = 0;
	i = ndigits;
	while (i--) {
		nextcarry = (b[i] & mask) << y;
		a[i] = b[i] >> bits | carry;
		carry = nextcarry;
	}

	return carry;
}

int mp_divide(unsigned int q[], unsigned int r[], const unsigned int u[],
	      size_t udigits, unsigned int v[], size_t vdigits)
{
	size_t shift;
	int n, m, j;
	unsigned int bitmask, overflow;
	unsigned int qhat, rhat, t[2];
	unsigned int *uu, *ww;
	int qhat_ok, cmp;

	/* Clear q and r */
	mp_set_zero(q, udigits);
	mp_set_zero(r, udigits);

	/* Work out exact sizes of u and v */
	n = (int)mp_sizeof(v, vdigits);
	m = (int)mp_sizeof(u, udigits);
	m -= n;

	/* Catch special cases */
	if (n == 0)
		return -1;	/* Error: divide by zero */

	if (n == 1) {	/* Use short division instead */
		r[0] = mp_short_div(q, u, v[0], udigits);
		return 0;
	}

	if (m < 0) {	/* v > u, so just set q = 0 and r = u */
		mp_set_equal(r, u, udigits);
		return 0;
	}

	if (m == 0) {	/* u and v are the same length */
		cmp = mp_compare(u, v, (size_t)n);
		if (cmp < 0) {	/* v > u, as above */
			mp_set_equal(r, u, udigits);
			return 0;
		} else if (cmp == 0) {	/* v == u, so set q = 1 and r = 0 */
			mp_set_digit(q, 1, udigits);
			return 0;
		}
	}

	bitmask = XMP_HI_BIT_MASK;
	for (shift = 0; shift < XBITS_PER_DIGIT; shift++) {
		if (v[n - 1] & bitmask)
			break;
		bitmask >>= 1;
	}

	/* Normalise v in situ - NB only shift non-zero digits */
	overflow = mp_shift_left(v, v, shift, n);

	/* Copy normalised dividend u*d into r */
	overflow = mp_shift_left(r, u, shift, n + m);
	uu = r;	/* Use ptr to keep notation constant */

	t[0] = overflow;	/* Extra digit Um+n */

	/* Step D2. Initialise j. Set j = m */
	for (j = m; j >= 0; j--) {
		/*
		 * Step D3. Set Qhat = [(b.Uj+n + Uj+n-1)/Vn-1]
		 * and Rhat = remainder
		 */
		qhat_ok = 0;
		t[1] = t[0];	/* This is Uj+n */
		t[0] = uu[j + n - 1];
		overflow = sp_divide(&qhat, &rhat, t, v[n - 1]);

		/* Test Qhat */
		if (overflow) {	/* Qhat == b so set Qhat = b - 1 */
			qhat = MAX_DIGIT;
			rhat = uu[j + n - 1];
			rhat += v[n - 1];
			if (rhat < v[n - 1])	/* Rhat >= b, so no re-test */
				qhat_ok = 1;
		}
		/* [VERSION 2: Added extra test "qhat && "] */
		if (qhat && !qhat_ok && qhat_too_big(qhat, rhat,
						     v[n - 2], uu[j + n - 2])) {
			/*
			 * If Qhat.Vn-2 > b.Rhat + Uj+n-2
			 * decrease Qhat by one, increase Rhat by Vn-1
			 */
			qhat--;
			rhat += v[n - 1];
			/* Repeat this test if Rhat < b */
			if (!(rhat < v[n - 1]))
				if (qhat_too_big(qhat, rhat, v[n - 2], uu[j + n - 2]))
					qhat--;
		}

		/* Step D4. Multiply and subtract */
		ww = &uu[j];
		overflow = mp_mult_sub(t[1], ww, v, qhat, (size_t)n);

		/* Step D5. Test remainder. Set Qj = Qhat */
		q[j] = qhat;
		if (overflow) {	/* Step D6. Add back if D4 was negative */
			q[j]--;
			overflow = mp_add(ww, ww, v, (size_t)n);
		}

		t[0] = uu[j + n - 1];	/* Uj+n on next round */

	}	/* Step D7. Loop on j */

	/* Clear high digits in uu */
	for (j = n; j < m + n; j++)
		uu[j] = 0;

	/* Step D8. Unnormalise. */

	mp_shift_right(r, r, shift, n);
	mp_shift_right(v, v, shift, n);

	return 0;
}

int mp_mod_inv(u32 inv[], const u32 u[], const u32 v[], size_t ndigits)
{
	/* Computes inv = u^(-1) mod v */

	/*
	 * Ref: Knuth Algorithm X Vol 2 p 342
	 * ignoring u2, v2, t2
	 * and avoiding negative numbers.
	 * Returns non-zero if inverse undefined.
	 */
	int b_iter;
	int result;

	/* 1 * ndigits each, except w = 2 * ndigits */
	u32 *u1, *u3, *v1, *v3, *t1, *t3, *q, *w;
	/* allocate 9 * ndigits */
	u1 = kzalloc(9 * ndigits * sizeof(u32), GFP_KERNEL);
	if (!u1)
		return -ENOMEM;

	u3 = &u1[1 * ndigits];
	v1 = &u1[2 * ndigits];
	v3 = &u1[3 * ndigits];
	t1 = &u1[4 * ndigits];
	t3 = &u1[5 * ndigits];
	q = &u1[6 * ndigits];
	/* 2 * ndigits each */
	w = &u1[7 * ndigits];

	/* Step X1. Initialise */
	mp_set_digit(u1, 1, ndigits);
	mp_set_equal(u3, u, ndigits);
	mp_set_zero(v1, ndigits);
	mp_set_equal(v3, v, ndigits);

	b_iter = 1;
	while (!mp_is_zero(v3, ndigits)) {
		mp_divide(q, t3, u3, ndigits, v3, ndigits);
		mp_multiply(w, q, v1, ndigits);
		mp_add(t1, u1, w, ndigits);
		/* Swap u1 = v1; v1 = t1; u3 = v3; v3 = t3 */
		mp_set_equal(u1, v1, ndigits);
		mp_set_equal(v1, t1, ndigits);
		mp_set_equal(u3, v3, ndigits);
		mp_set_equal(v3, t3, ndigits);

		b_iter = -b_iter;
	}

	if (b_iter < 0)
		mp_subtract(inv, v, u1, ndigits);/* inv = v - u1 */
	else
		mp_set_equal(inv, u1, ndigits);	/* inv = u1 */

	/* Make sure u3 = gcd(u,v) == 1 */
	if (mp_short_cmp(u3, 1, ndigits) != 0) {
		result = 1;
		mp_set_zero(inv, ndigits);
	} else {
		result = 0;
	}
	/* Clear up */
	kfree(u1);

	return result;
}

int mp_modulo(u32 r[], const u32 u[], size_t udigits,
	      u32 v[], size_t vdigits)
{
	/*
	 * Computes r = u mod v
	 * where r, v are multiprecision integers of length vdigits
	 * and u is a multiprecision integer of length udigits.
	 * r may overlap v.
	 * Note that r here is only vdigits long,
	 * whereas in mpDivide it is udigits long.
	 * Use remainder from mpDivide function.
	 */
	u32 *qq, *rr;
	size_t nn = max(udigits, vdigits);

	qq = kcalloc(udigits, udigits * sizeof(u32), GFP_KERNEL);
	if (!qq)
		return -ENOMEM;

	rr = kcalloc(nn, nn * sizeof(u32), GFP_KERNEL);
	if (!rr) {
		kfree(qq);
		return -ENOMEM;
	}
	/* rr[nn] = u mod v */
	mp_divide(qq, rr, u, udigits, v, vdigits);

	/* Final r is only vdigits long */
	mp_set_equal(r, rr, vdigits);

	kfree(rr);
	kfree(qq);

	return 0;
}

int mp_get_bit(u32 a[], size_t ndigits, size_t ibit)
{
	/* Returns value 1 or 0 of bit n (0..nbits-1); or -1 if out of range */

	size_t idigit, bit_to_get;
	u32 mask;

	/* Which digit? (0-based) */
	idigit = ibit / XBITS_PER_DIGIT;
	if (idigit >= ndigits)
		return -1;

	/* Set mask */
	bit_to_get = ibit % XBITS_PER_DIGIT;
	mask = 0x01 << bit_to_get;

	return ((a[idigit] & mask) ? 1 : 0);
}

int mp_mod_mult(u32 a[], const u32 x[], const u32 y[], u32 m[], size_t ndigits)
{
	/* Computes a = (x * y) mod m */
	/* Double-length temp variable p */
	u32 *p;

	p = kzalloc(ndigits * 2 * sizeof(u32), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	/* Calc p[2n] = x * y */
	mp_multiply(p, x, y, ndigits);

	/* Then modulo (NOTE: a is OK at only ndigits long) */
	mp_modulo(a, p, ndigits * 2, m, ndigits);

	kfree(p);

	return 0;
}

/** Returns 1 if a == b, else 0 (constant-time) */
int mp_equal(const u32 a[], const u32 b[], size_t ndigits)
{
	u32 dif = 0;

	while (ndigits--)
		dif |= a[ndigits] ^ b[ndigits];

	return IS_ZERO_DIGIT(dif);
}

u32 mp_subtract(u32 w[], const u32 u[], const u32 v[], size_t ndigits)
{
	/*
	 * Calculates w = u - v where u >= v
	 * w, u, v are multiprecision integers of ndigits each
	 * Returns 0 if OK, or 1 if v > u.
	 * Ref: Knuth Vol 2 Ch 4.3.1 p 267 Algorithm S.
	 */

	u32 k;
	size_t j;

	/* Step S1. Initialise */
	k = 0;

	for (j = 0; j < ndigits; j++) {
		/*
		 * Step S2. Subtract digits w_j = (u_j - v_j - k)
		 * Set k = 1 if borrow occurs.
		 */

		w[j] = u[j] - k;
		if (w[j] > MAX_DIGIT - k)
			k = 1;
		else
			k = 0;

		w[j] -= v[j];
		if (w[j] > MAX_DIGIT - v[j])
			k++;
	}
	/* Step S3. Loop on j */
	/* Should be zero if u >= v */
	return k;
}

/** Returns sign of (a - d) where d is a single digit */
int mp_short_cmp(const u32 a[], u32 d, size_t ndigits)
{
	size_t i;
	int gt = 0;
	int lt = 0;

	/* Zero-length a => a is zero */
	if (ndigits == 0)
		return (d ? -1 : 0);

	/* If |a| > 1 then a > d */
	for (i = 1; i < ndigits; i++) {
		if (a[i] != 0)
			return 1;	/* GT */
	}

	lt = (a[0] < d);
	gt = (a[0] > d);

	return gt - lt;	/* EQ=0 GT=+1 LT=-1 */
}

unsigned int mp_shift_left(unsigned int a[], const unsigned int *b, size_t shift, size_t ndigits)
{
	/* Computes a = b << shift */
	/* [v2.1] Modified to cope with shift > BITS_PERDIGIT */
	size_t i, y, nw, bits;
	unsigned int mask, carry, nextcarry;
	u8 shift_bit = 0;

	/* Do we shift whole digits? */
	if (shift >= XBITS_PER_DIGIT) {
		nw = shift / XBITS_PER_DIGIT;
		i = ndigits;
		while (i--) {
			if (i >= nw)
				a[i] = b[i - nw];
			else
				a[i] = 0;
		}
		/* Call again to shift bits inside digits */
		bits = shift % XBITS_PER_DIGIT;
		carry = b[ndigits - nw] << bits;
		if (bits)
			carry |= mp_shift_left(a, a, bits, ndigits);
		return carry;
	}
	bits = shift;
	/* Construct mask = high bits set */
	mask = ~(~(unsigned int)shift_bit >> bits);

	y = XBITS_PER_DIGIT - bits;
	carry = 0;
	for (i = 0; i < ndigits; i++) {
		nextcarry = (b[i] & mask) >> y;
		a[i] = b[i] << bits | carry;
		carry = nextcarry;
	}

	return carry;
}

static int mp_square(unsigned int w[], const unsigned int x[], size_t ndigits)
{
	unsigned int k, p[2], u[2], cbit, carry;
	size_t i, j, t, i2, cpos;

	t = ndigits;
	/* 1. For i from 0 to (2t-1) do: w_i = 0 */
	i2 = t << 1;
	for (i = 0; i < i2; i++)
		w[i] = 0;

	carry = 0;
	cpos = i2 - 1;
	/* 2. For i from 0 to (t-1) do: */
	for (i = 0; i < t; i++) {
	/*
	 * 2.1 (uv) = w_2i + x_i * x_i, w_2i = v, c = u
	 *  Careful, w_2i may be double-prec
	 */
		i2 = i << 1; /* 2*i */
		sp_multiply(p, x[i], x[i]);
		p[0] += w[i2];
		if (p[0] < w[i2])
			p[1]++;
		k = 0;	/* p[1] < b, so no overflow here */
		if (i2 == cpos && carry) {
			p[1] += carry;
			if (p[1] < carry)
				k++;
			carry = 0;
		}
		w[i2] = p[0];
		u[0] = p[1];
		u[1] = k;

		/*
		 * 2.2 for j from (i+1) to (t-1) do:
		 * (uv) = w_{i+j} + 2x_j * x_i + c,
		 * w_{i+j} = v, c = u,
		 * u is double-prec
		 * w_{i+j} is dbl if [i+j] == cpos
		 */
		k = 0;
		for (j = i + 1; j < t; j++) {
			/* p = x_j * x_i */
			sp_multiply(p, x[j], x[i]);
			/* p = 2p <=> p <<= 1 */
			cbit = (p[0] & XMP_HI_BIT_MASK) != 0;
			k =  (p[1] & XMP_HI_BIT_MASK) != 0;
			p[0] <<= 1;
			p[1] <<= 1;
			p[1] |= cbit;
			/* p = p + c */
			p[0] += u[0];
			if (p[0] < u[0]) {
				p[1]++;
				if (p[1] == 0)
					k++;
			}
			p[1] += u[1];
			if (p[1] < u[1])
				k++;
			/* p = p + w_{i+j} */
			p[0] += w[i + j];
			if (p[0] < w[i + j]) {
				p[1]++;
				if (p[1] == 0)
					k++;
			}
			if ((i + j) == cpos && carry) {/* catch overflow from last round */
				p[1] += carry;
				if (p[1] < carry)
					k++;
				carry = 0;
			}
			/* w_{i+j} = v, c = u */
			w[i + j] = p[0];
			u[0] = p[1];
			u[1] = k;
		}
		/* 2.3 w_{i+t} = u */
		w[i + t] = u[0];
		/* remember overflow in w_{i+t} */
		carry = u[1];
		cpos = i + t;
	}

	return 0;
}

size_t mp_conv_from_octets(unsigned int a[], size_t ndigits, const unsigned char *c,
			   size_t nbytes)
{
	/*
	 *	Converts nbytes octets into big digit a of max size ndigits
	 *	Returns actual number of digits set (may be larger than mp_sizeof)
	 */

	size_t i;
	int j, k = 0;
	unsigned int t;

	mp_set_zero(a, ndigits);
	/* Read in octets, least significant first */
	/* i counts into big_d, j along c, and k is # bits to shift */
	for (i = 0, j = (int)nbytes - 1; i < ndigits && j >= 0; i++) {
		t = 0;
		for (k = 0; (j >= 0) && (k < XBITS_PER_DIGIT) ; j--, k = k + 8)
			t |= ((unsigned int)c[j]) << k;
		a[i] = t;
	}
	return i;
}

static size_t mp_bit_length(const unsigned int d[], size_t ndigits)
{
	/* Returns no of significant bits in d */
	size_t n, i, bits;
	unsigned int mask;

	if (!d || ndigits == 0)
		return 0;

	n = mp_sizeof(d, ndigits);
	if (n == 0)
		return 0;

	for (i = 0, mask = XMP_HI_BIT_MASK; mask > 0; mask >>= 1, i++) {
		if (d[n - 1] & mask)
			break;
	}

	bits = n * XBITS_PER_DIGIT - i;

	return bits;
}

size_t mp_conv_to_octets(const unsigned int a[], size_t ndigits, unsigned char *c,
			 size_t nbytes)
{
	/*
	 *	Convert big digit a into string of octets, in big-endian order,
	 *	padding on the left to nbytes or truncating if necessary.
	 *	Return number of octets required excluding leading zero bytes.
	 */

	int j, k, len;
	unsigned int t;
	size_t i, noctets, nbits;

	nbits = mp_bit_length(a, ndigits);
	noctets = (nbits + 7) / 8;

	len = (int)nbytes;

	for (i = 0, j = len - 1; i < ndigits && j >= 0; i++) {
		t = a[i];
		for (k = 0; j >= 0 && k < XBITS_PER_DIGIT; j--, k += 8)
			c[j] = (unsigned char)(t >> k);
	}

	for ( ; j >= 0; j--)
		c[j] = 0;

	return (size_t)noctets;
}

static void mp_mod_square_temp(unsigned int y[], unsigned int m[], size_t ndigits,
			       unsigned int t1[], unsigned int t2[])
{
	mp_square(t1, y, ndigits);
	mp_divide(t2, y, t1, ndigits * 2, m, ndigits);
}

static void mp_mod_mult_temp(unsigned int y[], const unsigned int x[], unsigned int m[],
			     size_t ndigits, unsigned int t1[], unsigned int t2[])
{
	mp_multiply(t1, x, y, ndigits);
	mp_divide(t2, y, t1, ndigits * 2, m, ndigits);
}

static int mp_mod_exp_1(unsigned int yout[], const unsigned int x[],
			const unsigned int e[], unsigned int m[], size_t ndigits)
{
	/* Computes y = x^e mod m */
	/* Classic binary left-to-right method */
	unsigned int mask;
	size_t n;
	size_t nn = ndigits * 2;
	u32 *t1, *t2, *y;

	/*
	 * [v2.2] removed const restriction on m[] to avoid using an extra alloc'd
	 * var (m is changed in-situ during the divide operation then restored)
	 */

	t1 = kzalloc(nn * 3 * sizeof(u32), GFP_KERNEL);
	t2 = &t1[nn * 1];
	y  = &t1[nn * 2];

	n = mp_sizeof(e, ndigits);
	/* Catch e==0 => x^0=1 */
	if (n == 0) {
		mp_set_digit(yout, 1, ndigits);
		goto done;
	}
	/* Find second-most significant bit in e */
	for (mask = XMP_HI_BIT_MASK; mask > 0; mask >>= 1) {
		if (e[n - 1] & mask)
			break;
	}
	mp_next_bit_mask(&mask, (unsigned int *)&n);

	/* Set y = x */
	mp_set_equal(y, x, ndigits);

	/* For bit j = k-2 downto 0 */
	while (n) {
		/* Square y = y * y mod n */
		mp_mod_square_temp(y, m, ndigits, t1, t2);
		if (e[n - 1] & mask) {
			/*	if e(j) == 1 then multiply
			 *	y = y * x mod
			 */
			mp_mod_mult_temp(y, x, m, ndigits, t1, t2);
		}

		/* Move to next bit */
		mp_next_bit_mask(&mask, (unsigned int *)&n);
	}

	/* Return y */
	mp_set_equal(yout, y, ndigits);

done:
	kfree(t1);

	return 0;
}

int mp_mod_exp(unsigned int y[], const unsigned int x[], const unsigned int n[],
	       unsigned int d[], size_t ndigits)
{
	/* Computes y = x^n mod d */

	return mp_mod_exp_1(y, x, n, d, ndigits);
}
