/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx HDCP Common driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 */

#ifndef _XLNX_HDCP_COMMON_H_
#define _XLNX_HDCP_COMMON_H_

#include <linux/types.h>

int mp_mod_exp(unsigned int y[], const unsigned int x[], const unsigned int n[],
	       unsigned int d[], size_t ndigits);
size_t mp_conv_to_octets(const unsigned int a[], size_t ndigits, unsigned char *c,
			 size_t nbytes);
size_t mp_conv_from_octets(unsigned int a[], size_t ndigits, const unsigned char *c,
			   size_t nbytes);
u32 mp_subtract(u32 w[], const u32 u[], const u32 v[], size_t ndigits);
unsigned int mp_add(unsigned int w[], const unsigned int u[], const unsigned int v[],
		    size_t ndigits);
unsigned int mp_shift_left(unsigned int a[], const unsigned int *b, size_t shift, size_t ndigits);
int mp_multiply(unsigned int w[], const unsigned int u[], const unsigned int v[], size_t ndigits);
int mp_mod_mult(u32 a[], const u32 x[], const u32 y[], u32 m[], size_t ndigits);
int mp_modulo(u32 r[], const u32 u[], size_t udigits, u32 v[], size_t vdigits);
int mp_mod_inv(u32 inv[], const u32 u[], const u32 v[], size_t ndigits);
int mp_equal(const u32 a[], const u32 b[], size_t ndigits);
int mp_get_bit(u32 a[], size_t ndigits, size_t ibit);
int mp_short_cmp(const u32 a[], u32 d, size_t ndigits);
int mp_divide(unsigned int q[], unsigned int r[], const unsigned int u[],
	      size_t udigits, unsigned int v[], size_t vdigits);
#endif
