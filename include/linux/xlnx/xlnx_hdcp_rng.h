/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx HDCP 2X Random Number Generator Driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 */

#ifndef _XLNX_HDCP_RNG_H_
#define _XLNX_HDCP_RNG_H_

#include <linux/types.h>

/**
 * struct xlnx_hdcp2x_rng_hw - HDCP 2X random number generator configuration structure
 * @rng_coreaddress: HDCP 2X random number generator core address
 */
struct xlnx_hdcp2x_rng_hw {
	void __iomem *rng_coreaddress;
};

#define XHDCP2X_RNG_VER_BASE			(0 * 64)
#define XHDCP2X_RNG_VER_ID_OFFSET		((XHDCP2X_RNG_VER_BASE) + (0 * 4))
#define XHDCP2X_RNG_VER_VERSION_OFFSET		((XHDCP2X_RNG_VER_BASE) + (1 * 4))
#define XHDCP2X_RNG_REG_BASE			(1 * 64)
#define XHDCP2X_RNG_REG_CTRL_OFFSET		((XHDCP2X_RNG_REG_BASE) + (0 * 4))
#define XHDCP2X_RNG_REG_CTRL_SET_OFFSET		((XHDCP2X_RNG_REG_BASE) + (1 * 4))
#define XHDCP2X_RNG_REG_CTRL_CLR_OFFSET		((XHDCP2X_RNG_REG_BASE) + (2 * 4))
#define XHDCP2X_RNG_REG_STA_OFFSET		((XHDCP2X_RNG_REG_BASE) + (3 * 4))
#define XHDCP2X_RNG_REG_RN_1_OFFSET		((XHDCP2X_RNG_REG_BASE) + (4 * 4))
#define XHDCP2X_RNG_SHIFT_16			16
#define XHDCP2X_RNG_MASK_16			GENMASK(31, 16)
#define XHDCP2X_RNG_VER_ID			0x2200
#define XHDCP2X_RNG_REG_CTRL_RUN_MASK		BIT(0)

int xlnx_hdcp2x_rng_cfg_init(struct xlnx_hdcp2x_rng_hw *rng_cfg);
void xlnx_hdcp2x_rng_get_random_number(struct xlnx_hdcp2x_rng_hw *rng_cfg,
				       u8 *writeptr, u16 length, u16 randomlength);
void xlnx_hdcp2x_rng_enable(struct xlnx_hdcp2x_rng_hw *rng_cfg);
void xlnx_hdcp2x_rng_disable(struct xlnx_hdcp2x_rng_hw *rng_cfg);

#endif
