// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx SYSMON for Versal
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc.
 *
 * Description:
 * This driver is developed for SYSMON on Versal. The driver supports I2C Mode
 * and supports voltage and temperature monitoring via IIO sysfs interface.
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/i2c.h>

#include "versal-sysmon.h"

#define SYSMON_READ_DATA_SIZE	4U
#define SYSMON_WRITE_DATA_SIZE	8U
#define SYSMON_INSTR_READ	BIT(2)
#define SYSMON_INSTR_WRITE	BIT(3)

#define SYSMON_INTR_ALL_MASK	GENMASK(31, 0)

#define SYSMON_PYLD_DATA0_MASK	GENMASK(7, 0)
#define SYSMON_PYLD_DATA1_MASK	GENMASK(15, 8)
#define SYSMON_PYLD_DATA2_MASK	GENMASK(23, 16)
#define SYSMON_PYLD_DATA3_MASK	GENMASK(31, 24)

#define SYSMON_PYLD_OFS_LOW_MASK	GENMASK(9, 2)
#define SYSMON_PYLD_OFS_HIGH_MASK	GENMASK(15, 10)

#define SYSMON_PYLD_DATA0_IDX	0
#define SYSMON_PYLD_DATA1_IDX	1
#define SYSMON_PYLD_DATA2_IDX	2
#define SYSMON_PYLD_DATA3_IDX	3
#define SYSMON_PYLD_OFS_LOW_IDX	4
#define SYSMON_PYLD_OFS_HIGH_IDX	5
#define SYSMON_PYLD_INSTR_IDX	6

static inline void sysmon_i2c_write_reg(struct sysmon *sysmon, u32 offset, u32 data)
{
	char write_data[SYSMON_WRITE_DATA_SIZE] = { 0 };

	write_data[SYSMON_PYLD_DATA0_IDX] = (u8)(FIELD_GET(SYSMON_PYLD_DATA0_MASK, data));
	write_data[SYSMON_PYLD_DATA1_IDX] = (u8)(FIELD_GET(SYSMON_PYLD_DATA1_MASK, data));
	write_data[SYSMON_PYLD_DATA2_IDX] = (u8)(FIELD_GET(SYSMON_PYLD_DATA2_MASK, data));
	write_data[SYSMON_PYLD_DATA3_IDX] = (u8)(FIELD_GET(SYSMON_PYLD_DATA3_MASK, data));
	write_data[SYSMON_PYLD_OFS_LOW_IDX] = (u8)(FIELD_GET(SYSMON_PYLD_OFS_LOW_MASK, offset));
	write_data[SYSMON_PYLD_OFS_HIGH_IDX] = (u8)(FIELD_GET(SYSMON_PYLD_OFS_HIGH_MASK, offset));
	write_data[SYSMON_PYLD_INSTR_IDX] = (u8)(SYSMON_INSTR_WRITE);
	(void)i2c_master_send(sysmon->client, write_data, SYSMON_WRITE_DATA_SIZE);
}

static inline int sysmon_i2c_read_reg(struct sysmon *sysmon, u32 offset, u32 *data)
{
	char read_data[SYSMON_READ_DATA_SIZE];
	char write_data[SYSMON_WRITE_DATA_SIZE] = { 0 };
	int ret;

	write_data[SYSMON_PYLD_OFS_LOW_IDX] = (u8)(FIELD_GET(SYSMON_PYLD_OFS_LOW_MASK, offset));
	write_data[SYSMON_PYLD_OFS_HIGH_IDX] = (u8)(FIELD_GET(SYSMON_PYLD_OFS_HIGH_MASK, offset));
	write_data[SYSMON_PYLD_INSTR_IDX] = (u8)(SYSMON_INSTR_READ);
	(void)i2c_master_send(sysmon->client, write_data, SYSMON_WRITE_DATA_SIZE);
	ret = i2c_master_recv(sysmon->client, read_data, SYSMON_READ_DATA_SIZE);
	*data = (FIELD_PREP(SYSMON_PYLD_DATA0_MASK, read_data[0]) |
		 FIELD_PREP(SYSMON_PYLD_DATA1_MASK, read_data[1]) |
		 FIELD_PREP(SYSMON_PYLD_DATA2_MASK, read_data[2]) |
		 FIELD_PREP(SYSMON_PYLD_DATA3_MASK, read_data[3]));

	return ret;
}

static inline void sysmon_i2c_update_reg(struct sysmon *sysmon, u32 offset, u32 mask, u32 data)
{
	u32 val;

	sysmon_i2c_read_reg(sysmon, offset, &val);
	sysmon_i2c_write_reg(sysmon, offset, (u32)((val & ~mask) | (mask & data)));
}

static struct sysmon_ops i2c_access = {
	.read_reg = sysmon_i2c_read_reg,
	.write_reg = sysmon_i2c_write_reg,
	.update_reg = sysmon_i2c_update_reg,
};

static int sysmon_i2c_temp_read(struct sysmon *sysmon, int offset)
{
	u32 regval;

	if (sysmon_read_reg(sysmon, offset, &regval) < 0)
		regval = SYSMON_UPPER_SATURATION_SIGNED;

	return regval;
}

static int sysmon_i2c_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct sysmon *sysmon;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*sysmon));
	if (!indio_dev)
		return -ENOMEM;

	sysmon = iio_priv(indio_dev);

	sysmon->dev = &client->dev;
	sysmon->indio_dev = indio_dev;

	mutex_init(&sysmon->mutex);
	spin_lock_init(&sysmon->lock);

	indio_dev->dev.parent = &client->dev;
	indio_dev->dev.of_node = client->dev.of_node;
	indio_dev->name = "xlnx,versal-sysmon";
	sysmon_set_iio_dev_info(indio_dev);
	indio_dev->modes = INDIO_DIRECT_MODE;

	i2c_set_clientdata(client, sysmon);
	sysmon->client = client;
	sysmon->ops = &i2c_access;
	sysmon_write_reg(sysmon, SYSMON_NPI_LOCK, NPI_UNLOCK);
	sysmon_write_reg(sysmon, SYSMON_IDR, SYSMON_INTR_ALL_MASK);
	sysmon->master_slr = true;

	ret = sysmon_parse_dt(indio_dev, &client->dev);
	if (ret)
		return ret;

	sysmon->temp_read = &sysmon_i2c_temp_read;
	dev_set_drvdata(&client->dev, indio_dev);

	return iio_device_register(indio_dev);
}

static void sysmon_i2c_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = dev_get_drvdata(&client->dev);

	iio_device_unregister(indio_dev);
}

static const struct of_device_id sysmon_i2c_of_match_table[] = {
	{ .compatible = "xlnx,versal-sysmon" },
	{}
};
MODULE_DEVICE_TABLE(of, sysmon_i2c_of_match_table);

static struct i2c_driver sysmon_i2c_driver = {
	.probe = sysmon_i2c_probe,
	.remove = sysmon_i2c_remove,
	.driver = {
		.name = "sysmon_i2c",
		.of_match_table = sysmon_i2c_of_match_table,
	},
};
module_i2c_driver(sysmon_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xilinx Versal I2C SysMon Driver");
MODULE_AUTHOR("Conall O Griofa <conall.ogriofa@amd.com>");
