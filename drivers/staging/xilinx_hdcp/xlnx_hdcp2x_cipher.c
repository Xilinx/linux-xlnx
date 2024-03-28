// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDCP2X Cipher driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 *
 * This driver initializes the Cipher engine to implement AES-128
 * standard for encrypting and decrypting the audiovisual content.
 * The Cipher is required to be programmed with the Lc128, random number Riv,
 * and session key Ks before encryption is enabled.
 */

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/xlnx/xlnx_hdcp2x_cipher.h>

#define swap_bytes(i, buf, ptr, len) \
do {\
	typeof(i) (y) = (i); \
	typeof(len) (x) = (len); \
	for (y = 0; y < (x); y++) {\
		buf[(((x) - 1) - y)] = ptr[y]; \
	} \
} while (0)

void  xlnx_hdcp2x_tx_cipher_update_encryption(struct xlnx_hdcp2x_cipher_hw *cipher_cfg,
					      u8 enable)
{
	if (enable)
		xlnx_hdcp2x_cipher_write(cipher_cfg->cipher_coreaddress,
					 XHDCP2X_CIPHER_REG_CTRL_SET_OFFSET,
					 XHDCP2X_CIPHER_REG_CTRL_ENCRYPT_MASK);
	else
		xlnx_hdcp2x_cipher_write(cipher_cfg->cipher_coreaddress,
					 XHDCP2X_CIPHER_REG_CTRL_CLR_OFFSET,
					 XHDCP2X_CIPHER_REG_CTRL_ENCRYPT_MASK);
}

void xlnx_hdcp2x_cipher_init(struct xlnx_hdcp2x_cipher_hw *cipher_cfg)
{
	xlnx_hdcp2x_cipher_enable(cipher_cfg->cipher_coreaddress);
	xlnx_hdcp2x_cipher_set_txmode(cipher_cfg->cipher_coreaddress);
	xlnx_hdcp2x_tx_cipher_update_encryption(cipher_cfg, 0);
	xlnx_hdcp2x_cipher_disable(cipher_cfg->cipher_coreaddress);
}

void xlnx_hdcp2x_rx_cipher_init(struct xlnx_hdcp2x_cipher_hw *cipher_cfg)
{
	xlnx_hdcp2x_cipher_enable(cipher_cfg->cipher_coreaddress);
	xlnx_hdcp2x_cipher_set_rxmode(cipher_cfg->cipher_coreaddress);
	xlnx_hdcp2x_cipher_disable(cipher_cfg->cipher_coreaddress);
}

int xlnx_hdcp2x_cipher_cfg_init(struct xlnx_hdcp2x_cipher_hw *cipher_cfg)
{
	u32 reg_read;

	reg_read = xlnx_hdcp2x_cipher_read(cipher_cfg->cipher_coreaddress,
					   XHDCP2X_CIPHER_VER_ID_OFFSET);
	reg_read = FIELD_GET(XHDCP2X_CIPHER_MASK_16, reg_read);
	if (reg_read != XHDCP2X_CIPHER_VER_ID)
		return (-EINVAL);

	return reg_read;
}

void xlnx_hdcp2x_cipher_set_keys(struct xlnx_hdcp2x_cipher_hw *cipher_cfg,
				 const u8 *cipherkey, u32 offset, u16 len)
{
	u8 buf[XHDCP2X_CIPHER_KEY_LENGTH];
	u32 *bufptr;
	u8 i = 0;

	swap_bytes(i, buf, cipherkey, len);

	for (i = 0; i < len; i += 4) {
		bufptr = (u32 *)&buf[i];
		xlnx_hdcp2x_cipher_write(cipher_cfg->cipher_coreaddress,
					 offset, *bufptr);
		/* Increase offset to the next register */
		offset += 4;
	}
}

void xlnx_hdcp2x_cipher_set_lanecount(struct xlnx_hdcp2x_cipher_hw *cipher_cfg,
				      u8 lanecount)
{
	xlnx_hdcp2x_cipher_write(cipher_cfg->cipher_coreaddress,
				 XHDCP2X_CIPHER_REG_CTRL_CLR_OFFSET,
				 XHDCP2X_CIPHER_REG_CTRL_LANE_CNT_MASK);

	xlnx_hdcp2x_cipher_write(cipher_cfg->cipher_coreaddress,
				 XHDCP2X_CIPHER_REG_CTRL_SET_OFFSET,
				 lanecount << XHDCP2X_CIPHER_REG_CTRL_LANE_CNT_BIT_POS);
}
