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
#include <linux/property.h>

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

/* Temperature channels for remote chip monitoring */
static const struct iio_chan_spec remote_temp_channels[] = {
	SYSMON_CHAN_TEMP(TEMP_MAX, "temp"),
};

/* Fixed supply names for remote chip */
static const char * const supply_names[] = {
	"vccaux", "vccaux_pmc", "vcc_pmc", "vcc_psfp",
	"vcc_pslp", "vcc_soc", "vp_vn"
};

static int sysmon_remote_setup_channels(struct iio_dev *indio_dev, struct device *dev)
{
	u32 chan_size = sizeof(struct iio_chan_spec);
	struct iio_chan_spec *sysmon_channels;
	struct sysmon *sysmon;
	u32 total_channels, i;

	sysmon = iio_priv(indio_dev);

	/* Remote chip supports fixed voltage supplies + temperature channels */
	sysmon->num_supply_chan = ARRAY_SIZE(supply_names);
	sysmon->num_aie_temp_chan = 0;
	total_channels = sysmon->num_supply_chan + ARRAY_SIZE(remote_temp_channels);

	INIT_LIST_HEAD(&sysmon->region_list);

	/* Initialize buffer for channel specification */
	sysmon_channels = devm_kzalloc(dev, chan_size * total_channels, GFP_KERNEL);
	if (!sysmon_channels)
		return -ENOMEM;

	/* Configure fixed voltage supply channels for remote monitoring */
	for (i = 0; i < sysmon->num_supply_chan; i++) {
		sysmon_channels[i].type = IIO_VOLTAGE;
		sysmon_channels[i].indexed = 1;
		sysmon_channels[i].address = i;
		sysmon_channels[i].channel = i;
		sysmon_channels[i].info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_PROCESSED);
		sysmon_channels[i].scan_index = i;
		sysmon_channels[i].scan_type.realbits = 19;
		sysmon_channels[i].scan_type.storagebits = 32;
		sysmon_channels[i].scan_type.endianness = IIO_CPU;
		sysmon_channels[i].scan_type.sign = 'u';
		sysmon_channels[i].extend_name = supply_names[i];
	}

	/* Copy temperature channels after voltage channels */
	memcpy(sysmon_channels + sysmon->num_supply_chan, remote_temp_channels,
	       sizeof(remote_temp_channels));

	/* Reset oversampling fields for temperature channels */
	for (i = sysmon->num_supply_chan; i < total_channels; i++) {
		sysmon_channels[i].info_mask_shared_by_type = 0;
		sysmon_channels[i].info_mask_shared_by_type_available = 0;
	}

	indio_dev->num_channels = total_channels;
	indio_dev->channels = sysmon_channels;

	return 0;
}

static const struct sysmon_ops i2c_access = {
	.read_reg = sysmon_i2c_read_reg,
	.write_reg = sysmon_i2c_write_reg,
	.update_reg = sysmon_i2c_update_reg,
	.setup_channels = sysmon_parse_dt,
};

static const struct sysmon_ops remote_i2c_access = {
	.read_reg = sysmon_i2c_read_reg,
	.write_reg = sysmon_i2c_write_reg,
	.update_reg = sysmon_i2c_update_reg,
	.setup_channels = sysmon_remote_setup_channels,
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
	sysmon->ops = device_get_match_data(&client->dev);
	sysmon_write_reg(sysmon, SYSMON_NPI_LOCK, NPI_UNLOCK);
	sysmon_write_reg(sysmon, SYSMON_IDR, SYSMON_INTR_ALL_MASK);
	sysmon->master_slr = true;

	ret = sysmon->ops->setup_channels(indio_dev, &client->dev);
	if (ret)
		return ret;

	sysmon->temp_read = &sysmon_i2c_temp_read;
	dev_set_drvdata(&client->dev, indio_dev);

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct of_device_id sysmon_i2c_of_match_table[] = {
	{
		.compatible = "xlnx,versal-sysmon",
		.data = &i2c_access,
	},
	{
		.compatible = "xlnx,versal-sysmon-remote",
		.data = &remote_i2c_access
	},
	{}
};
MODULE_DEVICE_TABLE(of, sysmon_i2c_of_match_table);

static struct i2c_driver sysmon_i2c_driver = {
	.probe = sysmon_i2c_probe,
	.driver = {
		.name = "sysmon_i2c",
		.of_match_table = sysmon_i2c_of_match_table,
	},
};
module_i2c_driver(sysmon_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xilinx Versal I2C SysMon Driver");
MODULE_AUTHOR("Conall O Griofa <conall.ogriofa@amd.com>");
