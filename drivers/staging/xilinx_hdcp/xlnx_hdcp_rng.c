// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDCP2X Random Number Generator driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 *
 * This driver initializes Random Number Generator-RNG, which is used to
 * produce random numbers during the HDCP authentication and Key exchange.
 */

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/xlnx/xlnx_hdcp_rng.h>

static int xlnx_hdcp2x_rng_read(void __iomem *rng_coreaddress, int reg_offset)
{
	return readl(rng_coreaddress + reg_offset);
}

static void xlnx_hdcp2x_rng_write(void __iomem *rng_coreaddress, int reg_offset,
				  u32 data)
{
	writel(data, rng_coreaddress + reg_offset);
}

int xlnx_hdcp2x_rng_cfg_init(struct xlnx_hdcp2x_rng_hw *rng_cfg)
{
	u32 reg_read;

	reg_read = xlnx_hdcp2x_rng_read(rng_cfg->rng_coreaddress,
					XHDCP2X_RNG_VER_ID_OFFSET);
	reg_read = FIELD_GET(XHDCP2X_RNG_MASK_16, reg_read);
	if (reg_read != XHDCP2X_RNG_VER_ID)
		return (-EINVAL);

	return 0;
}

void xlnx_hdcp2x_rng_get_random_number(struct xlnx_hdcp2x_rng_hw *rng_cfg,
				       u8 *writeptr, u16 length, u16 randomlength)
{
	u32 i, j;
	u32 offset = 0;
	u32 random_word;
	u8 *read_ptr = (u8 *)&random_word;

	/*
	 * randomlength is the requested length of the random number in bytes.
	 * The length must be a multiple of 4
	 */
	for (i = 0; i < randomlength; i += 4) {
		random_word = xlnx_hdcp2x_rng_read(rng_cfg->rng_coreaddress,
						   XHDCP2X_RNG_REG_RN_1_OFFSET + offset);
		for (j = 0; j < 4; j++)
			writeptr[i + j] = read_ptr[j];
		offset = (offset + 4) % 16;
	}
}

void xlnx_hdcp2x_rng_enable(struct xlnx_hdcp2x_rng_hw *rng_cfg)
{
	xlnx_hdcp2x_rng_write(rng_cfg->rng_coreaddress, XHDCP2X_RNG_REG_CTRL_SET_OFFSET,
			      XHDCP2X_RNG_REG_CTRL_RUN_MASK);
}

void xlnx_hdcp2x_rng_disable(struct xlnx_hdcp2x_rng_hw *rng_cfg)
{
	xlnx_hdcp2x_rng_write(rng_cfg->rng_coreaddress, XHDCP2X_RNG_REG_CTRL_CLR_OFFSET,
			      XHDCP2X_RNG_REG_CTRL_RUN_MASK);
}
