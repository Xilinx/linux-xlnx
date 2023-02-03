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

#endif
