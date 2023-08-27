// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDCP2X Random Number Generator driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Kunal Vasant Rane <kunal.rane@amd.com>
 *
 * This driver initializes Montogomery Multiplier IP, which is used to
 * exchange of the master key during authentication and key exchange is performed using
 * the public key cryptography system which is based on the RSA algorithm.
 * reference: https://docs.xilinx.com/v/u/en-US/pg249-hdcp22
 */

#include <linux/bitfield.h>
#include <linux/xlnx/xlnx_hdcp2x_mmult.h>

int arr[XHDCP2X_MMULT_MAX_TYPES][XHDCP2X_MMULT_ADDR] = {
	{XHDCP2X_MMULT_A_BASE, XHDCP2X_MMULT_A_HIGH},
	{XHDCP2X_MMULT_B_BASE, XHDCP2X_MMULT_B_HIGH},
	{XHDCP2X_MMULT_N_BASE, XHDCP2X_MMULT_N_HIGH},
	{XHDCP2X_MMULT_NPRIME_BASE, XHDCP2X_MMULT_NPRIME_HIGH},
};

static int xlnx_hdcp2x_mmult_read(void __iomem *mmult_coreaddress, int reg_offset)
{
	return readl(mmult_coreaddress + reg_offset);
}

static void xlnx_hdcp2x_mmult_write(void __iomem *mmult_coreaddress, int reg_offset, u32 data)
{
	writel(data, mmult_coreaddress + reg_offset);
}

int xlnx_hdcp2x_mmult_cfginit(struct xlnx_hdcp2x_mmult_hw *mmult_cfg)
{
	int reg_read;

	reg_read = xlnx_hdcp2x_mmult_read(mmult_cfg->mmult_coreaddress,
					  XHDCP2X_MMULT_ADDR_AP);

	return reg_read;
}

void xlnx_hdcp2x_mmult_enable(struct xlnx_hdcp2x_mmult_hw *mmult_cfg)
{
	u32 data;

	data = xlnx_hdcp2x_mmult_read(mmult_cfg->mmult_coreaddress,
				      XHDCP2X_MMULT_ADDR_AP) & XHDCP2X_MMULT_ADDR_AP_RD;
	xlnx_hdcp2x_mmult_write(mmult_cfg->mmult_coreaddress,
				XHDCP2X_MMULT_ADDR_AP, data | XHDCP2X_MMULT_ADDR_AP_WR);
}

u32 xlnx_hdcp2x_mmult_is_done(struct xlnx_hdcp2x_mmult_hw *mmult_cfg)
{
	u32 data;

	data = xlnx_hdcp2x_mmult_read(mmult_cfg->mmult_coreaddress,
				      XHDCP2X_MMULT_ADDR_AP);

	return (data & XHDCP2X_MMULT_DONE) ? 1 : 0;
}

u32 xlnx_hdcp2x_mmult_is_ready(struct xlnx_hdcp2x_mmult_hw *mmult_cfg)
{
	u32 data;

	data = xlnx_hdcp2x_mmult_read(mmult_cfg->mmult_coreaddress,
				      XHDCP2X_MMULT_ADDR_AP);

	return !(data & XHDCP2X_MMULT_READY);
}

u32 xlnx_hdcp2x_mmult_read_u_words(struct xlnx_hdcp2x_mmult_hw *mmult_cfg,
				   int offset, int *data, int length)
{
	int i;

	if ((offset + length) * XHDCP2X_MMULT_OFFSET_MULT
			> (XHDCP2X_MMULT_ADDR_U_HIGH - XHDCP2X_MMULT_ADDR_U_BASE + 1))
		return 0;

	for (i = 0; i < length; i++)
		*(data + i) =
			xlnx_hdcp2x_mmult_read(mmult_cfg->mmult_coreaddress,
					       XHDCP2X_MMULT_ADDR_U_BASE
					       + (offset + i) * XHDCP2X_MMULT_OFFSET_MULT);

	return length;
}

int xlnx_hdcp2x_mmult_write_type(struct xlnx_hdcp2x_mmult_hw *mmult_cfg, int offset, int *data,
				 int length, int type)
{
	int i, base, high;

	base = arr[type][0];
	high = arr[type][1];

	if ((offset +  length) * XHDCP2X_MMULT_OFFSET_MULT > (high - base + 1))
		return 0;

	for (i = 0; i < length; i++)
		xlnx_hdcp2x_mmult_write(mmult_cfg->mmult_coreaddress, base +
			(offset + i) * XHDCP2X_MMULT_OFFSET_MULT, *(data + i));
	return length;
}
