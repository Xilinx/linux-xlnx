// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Specific HDCP1x driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Katta Dhanunjanrao <katta.dhanunjanrao@amd.com>
 *
 */

#include <linux/types.h>
#include <linux/uaccess.h>

#include "xlnx_hdcp_tx.h"
#include "xhdcp1x_tx.h"
#include "xlnx_hdcp1x_tx.h"
#include "xlnx_hdcp2x_tx.h"

/* DEBUG CONSTANTS */
#define HDCP1X_KEYMGMT_REG_VERSION		0x0000
#define HDCP1X_KEYMGMT_REG_TYPE			0x0004
#define HDCP1X_KEYMGMT_REG_CTRL			0x000C
#define HDCP1X_KEYMGMT_REG_TBL_CTRL		0x0020
#define HDCP1X_KEYMGMT_REG_TBL_STATUS		0x0024
#define HDCP1X_KEYMGMT_REG_TBL_ADDR		0x0028
#define HDCP1X_KEYMGMT_REG_TBL_DAT_H		0x002C
#define HDCP1X_KEYMGMT_REG_TBL_DAT_L		0x0030

#define HDCP1X_KEYMGMT_REG_CTRL_RST_MASK	BIT(31)
#define HDCP1X_KEYMGMT_REG_CTRL_DISABLE_MASK	GENMASK(31, 1)
#define HDCP1X_KEYMGMT_REG_CTRL_ENABLE_MASK	BIT(0)
#define HDCP1X_KEYMGMT_REG_TBL_STATUS_RETRY	0x400
#define HDCP1X_KEYMGMT_TBLID_0			0
#define HDCP1X_KEYMGMT_REG_TBL_CTRL_WR_MASK	BIT(0)
#define HDCP1X_KEYMGMT_REG_TBL_CTRL_RD_MASK	BIT(1)
#define HDCP1X_KEYMGMT_REG_TBL_CTRL_EN_MASK	BIT(31)
#define HDCP1X_KEYMGMT_REG_TBL_STATUS_DONE_MASK	BIT(0)
#define HDCP1X_KEYMGMT_MAX_TBLS			8
#define HDCP1X_KEYS_SIZE			336
#define HDCP1X_KEYMGMT_MAX_ROWS_PER_TBL		41

union hdcp1x_key_table {
	u8 data_u8[HDCP1X_KEYS_SIZE];
	u64 data_u64[HDCP1X_KEYS_SIZE / (sizeof(u64))];
};

/* Register related operations */
static inline void xdptx_hdcp1x_keymgmt_reset(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u32 data;

	if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
			HDCP1X_KEYMGMT_REG_CTRL, &data))
		return;
	data |= HDCP1X_KEYMGMT_REG_CTRL_RST_MASK;
	if (regmap_update_bits(xhdcp1x_tx->hdcp1x_keymgmt_base,
			       HDCP1X_KEYMGMT_REG_CTRL, HDCP1X_KEYMGMT_REG_CTRL_RST_MASK,
			       data))
		return;
	if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
			HDCP1X_KEYMGMT_REG_CTRL, &data))
		return;
	data &= ~HDCP1X_KEYMGMT_REG_CTRL_RST_MASK;
	regmap_update_bits(xhdcp1x_tx->hdcp1x_keymgmt_base, HDCP1X_KEYMGMT_REG_CTRL,
			   HDCP1X_KEYMGMT_REG_CTRL_RST_MASK, data);
}

static inline void xdptx_hdcp1x_keymgmt_enable(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u32 data;

	if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
			HDCP1X_KEYMGMT_REG_CTRL, &data))
		return;
	data |= HDCP1X_KEYMGMT_REG_CTRL_ENABLE_MASK;
	if (regmap_update_bits(xhdcp1x_tx->hdcp1x_keymgmt_base,
			       HDCP1X_KEYMGMT_REG_CTRL, HDCP1X_KEYMGMT_REG_CTRL_ENABLE_MASK,
			       data))
		return;

	if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
			HDCP1X_KEYMGMT_REG_TBL_CTRL, &data))
		return;
	data |= HDCP1X_KEYMGMT_REG_TBL_CTRL_EN_MASK;
	regmap_update_bits(xhdcp1x_tx->hdcp1x_keymgmt_base, HDCP1X_KEYMGMT_REG_TBL_CTRL,
			   HDCP1X_KEYMGMT_REG_TBL_CTRL_EN_MASK, data);
}

static inline void xdptx_hdcp1x_keymgmt_disable(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	u32 data;

	if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
			HDCP1X_KEYMGMT_REG_CTRL, &data))
		return;
	data &= HDCP1X_KEYMGMT_REG_CTRL_DISABLE_MASK;
	regmap_update_bits(xhdcp1x_tx->hdcp1x_keymgmt_base, HDCP1X_KEYMGMT_REG_CTRL,
			   HDCP1X_KEYMGMT_REG_CTRL_DISABLE_MASK, data);
}

static int xdptx_hdcp1x_keymgmt_is_table_config_done(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	int retry = HDCP1X_KEYMGMT_REG_TBL_STATUS_RETRY;
	u32 data;

	while (retry) {
		if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
				HDCP1X_KEYMGMT_REG_TBL_STATUS, &data))
			return 0;
		if (!(data & HDCP1X_KEYMGMT_REG_TBL_STATUS_DONE_MASK))
			break;
		retry--;
		usleep_range(50, 100);
	}

	return retry;
}

static int xdptx_hdcp1x_keymgmt_table_read(struct xlnx_hdcp1x_config *xhdcp1x_tx,
					   u8 table_id, u8 row_id, u64 *read_val)
{
	u64 temp;
	u32 addr, data;

	addr = table_id;
	addr <<= BITS_PER_BYTE;
	addr |= row_id;

	if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
			HDCP1X_KEYMGMT_REG_TBL_CTRL, &data))
		return -EIO;
	data &= ~HDCP1X_KEYMGMT_REG_TBL_CTRL_WR_MASK;
	data |= HDCP1X_KEYMGMT_REG_TBL_CTRL_RD_MASK;
	if (regmap_write(xhdcp1x_tx->hdcp1x_keymgmt_base,
			 HDCP1X_KEYMGMT_REG_TBL_CTRL, data))
		return -EIO;
	if (regmap_write(xhdcp1x_tx->hdcp1x_keymgmt_base,
			 HDCP1X_KEYMGMT_REG_TBL_ADDR, addr))
		return -EIO;
	if (!xdptx_hdcp1x_keymgmt_is_table_config_done(xhdcp1x_tx))
		return -EIO;

	if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
			HDCP1X_KEYMGMT_REG_TBL_DAT_H, &data))
		return -EIO;
	temp = data;
	temp <<= BITS_PER_BYTE * sizeof(u32);
	if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
			HDCP1X_KEYMGMT_REG_TBL_DAT_L, &data))
		return -EIO;
	temp |= data;
	*read_val = temp;

	return 0;
}

static int xdptx_hdcp1x_keymgmt_table_write(struct xlnx_hdcp1x_config *xhdcp1x_tx,
					    u8 table_id, u8 row_id, u64 write_val)
{
	u32 addr, data;

	if (regmap_write(xhdcp1x_tx->hdcp1x_keymgmt_base,
			 HDCP1X_KEYMGMT_REG_TBL_DAT_L,
			 lower_32_bits(write_val)))
		return -EIO;
	if (regmap_write(xhdcp1x_tx->hdcp1x_keymgmt_base,
			 HDCP1X_KEYMGMT_REG_TBL_DAT_H,
			 upper_32_bits(write_val)))
		return -EIO;

	if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
			HDCP1X_KEYMGMT_REG_TBL_CTRL, &data))
		return -EIO;
	data &= ~HDCP1X_KEYMGMT_REG_TBL_CTRL_RD_MASK;
	data |= HDCP1X_KEYMGMT_REG_TBL_CTRL_WR_MASK;
	if (regmap_write(xhdcp1x_tx->hdcp1x_keymgmt_base,
			 HDCP1X_KEYMGMT_REG_TBL_CTRL, data))
		return -EIO;

	addr = table_id;
	addr <<= BITS_PER_BYTE;
	addr |= row_id;
	if (regmap_write(xhdcp1x_tx->hdcp1x_keymgmt_base,
			 HDCP1X_KEYMGMT_REG_TBL_ADDR, addr))
		return -EIO;
	if (!xdptx_hdcp1x_keymgmt_is_table_config_done(xhdcp1x_tx))
		return -EIO;

	return 0;
}

static void xdptx_hdcp1x_keymgmt_get_num_of_tables_rows(struct xlnx_hdcp1x_config *xhdcp1x_tx,
							u8 *num_tables, u8 *num_rows_per_table)
{
	u32 data;

	if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
			HDCP1X_KEYMGMT_REG_TYPE, &data))
		return;

	if (data) {
		*num_tables = (data >> 8) & 0xFF;
		*num_rows_per_table = data & 0xFF;
	} else {
		*num_tables = HDCP1X_KEYMGMT_MAX_TBLS;
		*num_rows_per_table = HDCP1X_KEYMGMT_MAX_ROWS_PER_TBL;
	}
}

static int xdptx_hdcp1x_keymgmt_init_tables(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	int ret = 0;
	u8 num_tables = 0, num_rows_per_table = 0, table_id, row_id;

	xdptx_hdcp1x_keymgmt_get_num_of_tables_rows(xhdcp1x_tx, &num_tables,
						    &num_rows_per_table);
	for (table_id = 0; table_id < num_tables; table_id++)
		for (row_id = 0; row_id < num_rows_per_table; row_id++)
			if (xdptx_hdcp1x_keymgmt_table_write(xhdcp1x_tx, table_id,
							     row_id, 0))
				return -EIO;
	return ret;
}

static int xdptx_hdcp1x_keymgmt_load_keys(struct xlnx_hdcp1x_config *xhdcp1x_tx,
					  union hdcp1x_key_table *key_table,
					  u32 key_table_size)
{
	int ret = 0;
	u8 row_id;

	for (row_id = 0; row_id < (key_table_size / sizeof(u64)); row_id++)
		if (xdptx_hdcp1x_keymgmt_table_write(xhdcp1x_tx, HDCP1X_KEYMGMT_TBLID_0,
						     row_id, key_table->data_u64[row_id]))
			ret = -EIO;

	return ret;
}

static int xdptx_hdcp1x_keymgmt_verify_keys(struct xlnx_hdcp1x_config *xhdcp1x_tx,
					    union hdcp1x_key_table *key_table,
					    u32 key_table_size)
{
	u64 data;
	int ret = 0;
	u8 row_id;

	for (row_id = 0; row_id < (key_table_size / sizeof(u64)); row_id++) {
		data = 0;
		xdptx_hdcp1x_keymgmt_table_read(xhdcp1x_tx, HDCP1X_KEYMGMT_TBLID_0,
						row_id, &data);
		if (data != key_table->data_u64[row_id])
			ret = -EIO;
	}

	return ret;
}

static int xdptx_hdcp1x_keymgmt_set_key(struct xlnx_hdcp1x_config *xhdcp1x_tx)
{
	union hdcp1x_key_table key_table;
	int ret = 0;
	u32 version, type;
	u8 index;

	if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
			HDCP1X_KEYMGMT_REG_VERSION, &version))
		return -EIO;
	if (regmap_read(xhdcp1x_tx->hdcp1x_keymgmt_base,
			HDCP1X_KEYMGMT_REG_TYPE, &type))
		return -EIO;
	if (!version && !type) {
		dev_err(xhdcp1x_tx->dev, "hdcp1x keymgmt core is not present\n");
		return -ENODEV;
	}

	xdptx_hdcp1x_keymgmt_reset(xhdcp1x_tx);
	ret = xdptx_hdcp1x_keymgmt_init_tables(xhdcp1x_tx);
	if (ret)
		return ret;
	xdptx_hdcp1x_keymgmt_disable(xhdcp1x_tx);
	memcpy(key_table.data_u8, xhdcp1x_tx->hdcp1x_key, HDCP1X_KEYS_SIZE);
	/* adjust the endian-ness to host order */
	for (index = 0; index < HDCP1X_KEYS_SIZE / sizeof(u64); index++)
		key_table.data_u64[index] =
			be64_to_cpu(*((__be64 *)&key_table.data_u64[index]));

	ret = xdptx_hdcp1x_keymgmt_load_keys(xhdcp1x_tx, &key_table,
					     HDCP1X_KEYS_SIZE);
	if (ret)
		return ret;
	ret = xdptx_hdcp1x_keymgmt_verify_keys(xhdcp1x_tx, &key_table,
					       HDCP1X_KEYS_SIZE);
	if (ret)
		return ret;
	xdptx_hdcp1x_keymgmt_enable(xhdcp1x_tx);

	return ret;
}

static int xdptx_hdcp1x_key_write(struct xlnx_hdcp1x_config *xhdcp1x_tx, u8 *data)
{
	int ret = 0;

	xhdcp1x_tx->hdcp1x_key = devm_kzalloc(xhdcp1x_tx->dev, HDCP1X_KEYS_SIZE,
					      GFP_KERNEL);
	if (!xhdcp1x_tx->hdcp1x_key)
		return -ENOMEM;

	memcpy(xhdcp1x_tx->hdcp1x_key, data, HDCP1X_KEYS_SIZE);
	xhdcp1x_tx->hdcp1x_key_available = true;
	ret = xdptx_hdcp1x_keymgmt_set_key(xhdcp1x_tx);

	if (ret < 0)
		return ret;

	xhdcp1x_tx_set_keyselect(xhdcp1x_tx, 0);
	xhdcp1x_tx_load_aksv(xhdcp1x_tx);
	//msleep(5);

	return ret;
}

int xlnx_hdcp1x_keymngt_init(struct xlnx_hdcp1x_config *xhdcp1x_tx, u8 *data)
{
	int ret = 0;

	if (!(xhdcp1x_tx->keyinit)) {
		/* Key Management Initialize */
		ret = xdptx_hdcp1x_key_write(xhdcp1x_tx, data);
		if (ret < 0)
			return ret;
		xhdcp1x_tx->keyinit = true;
	}

	return ret;
}
