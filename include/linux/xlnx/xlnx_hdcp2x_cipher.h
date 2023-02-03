/* SPDX-License-Identifier: GPL-2.0*/
/*
 * Xilinx HDCP2X Cipher driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 */

#ifndef _XLNX_HDCP2X_CIPHER_H_
#define _XLNX_HDCP2X_CIPHER_H_

#include <linux/types.h>

#define XHDCP2X_CIPHER_VER_BASE			(0 * 64)
#define XHDCP2X_CIPHER_VER_ID_OFFSET		((XHDCP2X_CIPHER_VER_BASE) + (0 * 4))
#define XHDCP2X_CIPHER_VER_VERSION_OFFSET	((XHDCP2X_CIPHER_VER_BASE) + (1 * 4))

#define XHDCP2X_CIPHER_REG_BASE			(1 * 64)
#define XHDCP2X_CIPHER_REG_CTRL_OFFSET		((XHDCP2X_CIPHER_REG_BASE) + (0 * 4))
#define XHDCP2X_CIPHER_REG_CTRL_SET_OFFSET	((XHDCP2X_CIPHER_REG_BASE) + (1 * 4))
#define XHDCP2X_CIPHER_REG_CTRL_CLR_OFFSET	((XHDCP2X_CIPHER_REG_BASE) + (2 * 4))
#define XHDCP2X_CIPHER_REG_STA_OFFSET		((XHDCP2X_CIPHER_REG_BASE) + (3 * 4))
#define XHDCP2X_CIPHER_REG_KS_1_OFFSET		((XHDCP2X_CIPHER_REG_BASE) + (4 * 4))
#define XHDCP2X_CIPHER_REG_KS_2_OFFSET		((XHDCP2X_CIPHER_REG_BASE) + (5 * 4))
#define XHDCP2X_CIPHER_REG_KS_3_OFFSET		((XHDCP2X_CIPHER_REG_BASE) + (6 * 4))
#define XHDCP2X_CIPHER_REG_KS_4_OFFSET		((XHDCP2X_CIPHER_REG_BASE) + (7 * 4))
#define XHDCP2X_CIPHER_REG_LC128_1_OFFSET	((XHDCP2X_CIPHER_REG_BASE) + (8 * 4))
#define XHDCP2X_CIPHER_REG_LC128_2_OFFSET	((XHDCP2X_CIPHER_REG_BASE) + (9 * 4))
#define XHDCP2X_CIPHER_REG_LC128_3_OFFSET	((XHDCP2X_CIPHER_REG_BASE) + (10 * 4))
#define XHDCP2X_CIPHER_REG_LC128_4_OFFSET	((XHDCP2X_CIPHER_REG_BASE) + (11 * 4))
#define XHDCP2X_CIPHER_REG_RIV_1_OFFSET		((XHDCP2X_CIPHER_REG_BASE) + (12 * 4))
#define XHDCP2X_CIPHER_REG_RIV_2_OFFSET		((XHDCP2X_CIPHER_REG_BASE) + (13 * 4))
#define XHDCP2X_CIPHER_REG_INPUTCTR_1_OFFSET	((XHDCP2X_CIPHER_REG_BASE) + (14 * 4))
#define XHDCP2X_CIPHER_REG_INPUTCTR_2_OFFSET	((XHDCP2X_CIPHER_REG_BASE) + (15 * 4))

#define XHDCP2X_CIPHER_REG_CTRL_RUN_MASK	BIT(0)
#define XHDCP2X_CIPHER_REG_CTRL_IE_MASK		BIT(1)
#define XHDCP2X_CIPHER_REG_CTRL_ENCRYPT_MASK	BIT(3)
#define XHDCP2X_CIPHER_REG_CTRL_BLANK_MASK	BIT(4)
#define XHDCP2X_CIPHER_REG_CTRL_NOISE_MASK	BIT(5)
#define XHDCP2X_CIPHER_REG_CTRL_LANE_CNT_MASK	GENMASK(9, 6)
#define XHDCP2X_CIPHER_REG_CTRL_LANE_CNT_BIT_POS	6

#define XHDCP2X_CIPHER_REG_STA_IRQ_MASK		BIT(0)
#define XHDCP2X_CIPHER_REG_STA_EVT_MASK		BIT(1)
#define XHDCP2X_CIPHER_REG_STA_ENCRYPTED_MASK	BIT(2)
#define XHDCP2X_CIPHER_REG_CTRL_MODE_MASK	BIT(2)

#define XHDCP2X_CIPHER_KEY_LENGTH		16
#define XHDCP2X_CIPHER_SHIFT_16			16
#define XHDCP2X_CIPHER_MASK_16			GENMASK(31, 16)
#define XHDCP2X_CIPHER_VER_ID			0x2200

/**
 * struct xlnx_hdcp2x_cipher_hw - HDCP2X internal cipher engine hardware structure
 * @cipher_coreaddress: HDCP2X cipher core address
 */
struct xlnx_hdcp2x_cipher_hw {
	void __iomem *cipher_coreaddress;
};

#define xlnx_hdcp2x_cipher_write(coreaddress, reg_offset, data) \
	writel(data, (coreaddress) + (reg_offset))

#define xlnx_hdcp2x_cipher_read(coreaddress, reg_offset) \
	readl((coreaddress) + (reg_offset))

#define xlnx_hdcp2x_cipher_get_status(cipher_address) \
	xlnx_hdcp2x_cipher_read(cipher_address, XHDCP2X_CIPHER_REG_STA_OFFSET)

#define xlnx_hdcp2x_cipher_is_encrypted(cipher_address) \
	((xlnx_hdcp2x_cipher_get_status(cipher_address) \
	& XHDCP2X_CIPHER_REG_STA_ENCRYPTED_MASK) == \
	XHDCP2X_CIPHER_REG_STA_ENCRYPTED_MASK)

#define xlnx_hdcp2x_cipher_enable(cipher_address) \
	xlnx_hdcp2x_cipher_write(cipher_address, \
	(XHDCP2X_CIPHER_REG_CTRL_SET_OFFSET), (XHDCP2X_CIPHER_REG_CTRL_RUN_MASK))

#define xlnx_hdcp2x_cipher_disable(cipher_address) \
		xlnx_hdcp2x_cipher_write(cipher_address, \
		(XHDCP2X_CIPHER_REG_CTRL_CLR_OFFSET), (XHDCP2X_CIPHER_REG_CTRL_RUN_MASK))

#define xlnx_hdcp2x_cipher_set_txmode(cipher_address) \
	xlnx_hdcp2x_cipher_write(cipher_address, \
	(XHDCP2X_CIPHER_REG_CTRL_CLR_OFFSET), (XHDCP2X_CIPHER_REG_CTRL_MODE_MASK))

#define xlnx_hdcp2x_cipher_set_rxmode(cipher_address) \
		xlnx_hdcp2x_cipher_write(cipher_address, \
		(XHDCP2X_CIPHER_REG_CTRL_SET_OFFSET), (XHDCP2X_CIPHER_REG_CTRL_MODE_MASK))

int xlnx_hdcp2x_cipher_cfg_init(struct xlnx_hdcp2x_cipher_hw *cipher_cfg);
void xlnx_hdcp2x_cipher_set_keys(struct xlnx_hdcp2x_cipher_hw *cipher_cfg,
				 const u8 *buf, u32 offset, u16 len);
void xlnx_hdcp2x_cipher_set_lanecount(struct xlnx_hdcp2x_cipher_hw *cipher_cfg,
				      u8 lanecount);
void xlnx_hdcp2x_cipher_init(struct xlnx_hdcp2x_cipher_hw *cipher_cfg);
void xlnx_hdcp2x_rx_cipher_init(struct xlnx_hdcp2x_cipher_hw *cipher_cfg);
void  xlnx_hdcp2x_tx_cipher_update_encryption(struct xlnx_hdcp2x_cipher_hw *cipher_cfg,
					      u8 enable);

#endif
