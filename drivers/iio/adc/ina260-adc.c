// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INA260 power monitor driver
 * Based on drivers/iio/adc/ina2xx-adc.c
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */

#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/sysfs.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/sched/task.h>
#include <linux/util_macros.h>

/* INA260 registers definition */
#define INA260_CONFIG		0x00
#define INA260_CURRENT		0x01
#define INA260_VOLTAGE		0x02
#define INA260_POWER		0x03
#define INA260_MASK_ENABLE	0x06
#define INA260_ALERT_LIMIT	0x07
#define INA260_MANF_ID		0xFE
#define INA260_DIE_ID		0xFF

#define INA260_CONFIG_DEFAULT	0x6327

#define INA260_CURRENT_LSB	1250
#define INA260_VOLTAGE_LSB	1250
#define INA260_POWER_LSB	10

/* Bits */
#define INA260_CVRF		BIT(3)

#define INA260_MODE_MASK	GENMASK(2, 0)
#define INA260_VOLT_MASK	GENMASK(8, 6)
#define INA260_SHIFT_VOLT(val)	((val) << 6)
#define INA260_CURR_MASK	GENMASK(5, 3)
#define INA260_SHIFT_CURR(val)	((val) << 3)
#define INA260_AVG_MASK		GENMASK(11, 9)
#define INA260_SHIFT_AVG(val)	((val) << 9)

#define SAMPLING_PERIOD(x) ({		\
	typeof(x) _x = (x);		\
	(_x->config->volt_conv_time	\
	+ _x->config->curr_conv_time)	\
	* _x->config->avgs; })

static bool ina260_is_writeable_reg(struct device *dev, unsigned int reg)
{
	return (reg == INA260_CONFIG) || (reg == INA260_MASK_ENABLE) ||
		(reg == INA260_ALERT_LIMIT);
}

static bool ina260_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return (reg != INA260_CONFIG);
}

static inline bool is_signed_reg(unsigned int reg)
{
	return (reg == INA260_CURRENT);
}

static const struct regmap_config ina260_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 8,
	.writeable_reg = ina260_is_writeable_reg,
	.volatile_reg = ina260_is_volatile_reg,
};

enum ina260_ids {ina260 = 0};

/**
 * struct ina260_config - For configurable parameters
 * @config_default: Default configuration
 * @volt_conv_time: Bus voltage conversion time
 * @curr_conv_time: Shunt current conversion time
 * @avgs: Number of samples collected and averaged
 */
struct ina260_config {
	u16 config_default;
	int volt_conv_time;
	int curr_conv_time;
	int avgs;
};

/**
 * struct ina260_chip - For device specific data
 * @regmap: Regmap pointer to device registers
 * @task: Pointer to task created by buffer mode
 * @lock: Mutex to enable use of multiple user apps
 * @chip_id: Id to determine chip
 * @config: Pointer to config structure
 */
struct ina260_chip {
	struct regmap *regmap;
	struct task_struct *task;
	struct mutex lock; /* Lock for device writes */
	enum ina260_ids chip_id;
	struct ina260_config *config;
};

static struct ina260_config ina260_config[] = {
	[ina260] = {
		.config_default = INA260_CONFIG_DEFAULT,
		.volt_conv_time = 1100,
		.curr_conv_time = 1100,
		.avgs = 4,
	},
};

static int ina260_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ina260_chip *chip = iio_priv(indio_dev);
	unsigned int regval;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (iio_buffer_enabled(indio_dev))
			return -EBUSY;

		ret = regmap_read(chip->regmap, chan->address, &regval);
		if (ret)
			return ret;

		if (is_signed_reg(chan->address))
			*val = (s16)regval;
		else
			*val = regval;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->address) {
		case INA260_CURRENT:
			*val = INA260_CURRENT_LSB;
			*val2 = 1000;
			return IIO_VAL_FRACTIONAL;

		case INA260_VOLTAGE:
			*val = INA260_VOLTAGE_LSB;
			*val2 = 1000;
			return IIO_VAL_FRACTIONAL;

		case INA260_POWER:
			*val = INA260_POWER_LSB;
			return IIO_VAL_INT;
		}
		return -EINVAL;

	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		if (chan->address == INA260_VOLTAGE)
			*val2 = chip->config->volt_conv_time;
		else
			*val2 = chip->config->curr_conv_time;

		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = DIV_ROUND_CLOSEST(1000000, SAMPLING_PERIOD(chip));
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*val = chip->config->avgs;
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static const int ina260_avg_tab[] = { 1, 4, 16, 64, 128, 256, 512, 1024 };

/* Conversion times in uS */
static const int ina260_conv_time_tab[] = { 140, 204, 332, 588, 1100, 2116, 4156, 8244 };

static int ina260_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ina260_chip *chip = iio_priv(indio_dev);
	unsigned int config;
	int ret, bits;

	if (iio_buffer_enabled(indio_dev))
		return -EBUSY;

	mutex_lock(&chip->lock);

	ret = regmap_read(chip->regmap, INA260_CONFIG, &config);
	if (ret)
		goto err;

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		if (val2 > 8244 || val2 < 140) {
			ret = -EINVAL;
			goto err;
		}

		bits = find_closest(val2, ina260_conv_time_tab,
				    ARRAY_SIZE(ina260_conv_time_tab));

		if (chan->address == INA260_VOLTAGE) {
			chip->config->volt_conv_time = ina260_conv_time_tab[bits];
			config &= ~INA260_VOLT_MASK;
			config |= INA260_SHIFT_VOLT(bits) & INA260_VOLT_MASK;
		} else {
			chip->config->curr_conv_time = ina260_conv_time_tab[bits];
			config &= ~INA260_CURR_MASK;
			config |= INA260_SHIFT_CURR(bits) & INA260_CURR_MASK;
		}
		break;

	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		bits = find_closest(val, ina260_avg_tab, ARRAY_SIZE(ina260_avg_tab));
		chip->config->avgs = ina260_avg_tab[bits];
		config &= ~INA260_AVG_MASK;
		config |= INA260_SHIFT_AVG(bits) & INA260_AVG_MASK;
		break;

	default:
		ret = -EINVAL;
	}
	if (!ret)
		ret = regmap_write(chip->regmap, INA260_CONFIG, config);
err:
	mutex_unlock(&chip->lock);

	return ret;
}

static int ina260_debug_reg(struct iio_dev *indio_dev,
			    unsigned int reg, unsigned int writeval, unsigned int *readval)
{
	struct ina260_chip *chip = iio_priv(indio_dev);

	if (!readval)
		return regmap_write(chip->regmap, reg, writeval);

	return regmap_read(chip->regmap, reg, readval);
}

#define INA260_CHAN_VOLTAGE(_index, _address) { \
	.type = IIO_VOLTAGE, \
	.address = (_address), \
	.indexed = 1, \
	.channel = (_index), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
	BIT(IIO_CHAN_INFO_SCALE) | \
	BIT(IIO_CHAN_INFO_INT_TIME), \
	.info_mask_shared_by_dir = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
	BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
	.scan_index = (_index), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	} \
}

#define INA260_CHAN_CURRENT(_index, _address) { \
	.type = IIO_CURRENT, \
	.address = (_address), \
	.indexed = 1, \
	.channel = (_index), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
	BIT(IIO_CHAN_INFO_SCALE) | \
	BIT(IIO_CHAN_INFO_INT_TIME), \
	.info_mask_shared_by_dir = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
	BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
	.scan_index = (_index), \
	.scan_type = { \
		.sign = 's', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	} \
}

#define INA260_CHAN_POWER(_index, _address) { \
	.type = IIO_POWER, \
	.address = (_address), \
	.indexed = 1, \
	.channel = (_index), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
	BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_dir = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
	BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
	.scan_index = (_index), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	} \
}

static const struct iio_chan_spec ina260_channels[] = {
	INA260_CHAN_CURRENT(0, INA260_CURRENT),
	INA260_CHAN_VOLTAGE(1, INA260_VOLTAGE),
	INA260_CHAN_POWER(2, INA260_POWER),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

/* Possible integration times for current and voltage */
static IIO_CONST_ATTR_NAMED(ina260_integration_time_available,
			    integration_time_available,
			    "0.000140 0.000204 0.000332 0.000588 0.001100 0.002116 0.004156 0.008244");

static struct attribute *ina260_attributes[] = {
	&iio_const_attr_ina260_integration_time_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ina260_attribute_group = {
	.attrs = ina260_attributes,
};

static const struct iio_info ina260_info = {
	.attrs = &ina260_attribute_group,
	.read_raw = ina260_read_raw,
	.write_raw = ina260_write_raw,
	.debugfs_reg_access = ina260_debug_reg,
};

static int ina260_conversion_ready(struct iio_dev *indio_dev)
{
	struct ina260_chip *chip = iio_priv(indio_dev);
	unsigned int alert;
	int ret;

	ret = regmap_read(chip->regmap, INA260_MASK_ENABLE, &alert);
	if (ret < 0)
		return ret;

	return (alert & INA260_CVRF);
}

static int ina260_work_buffer(struct iio_dev *indio_dev)
{
	/* data buffer needs space for channel data and timestap */
	unsigned short data[3 + sizeof(s64) / sizeof(short)];
	struct ina260_chip *chip = iio_priv(indio_dev);
	int bit, ret, i = 0;
	s64 time;

	time = iio_get_time_ns(indio_dev);

	/*
	 * Read current, voltage and power from device
	 */
	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		unsigned int val;

		ret = regmap_read(chip->regmap, INA260_CURRENT + bit, &val);
		if (ret < 0)
			return ret;

		data[i++] = val;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, data, time);

	return 0;
}

static int ina260_capture_thread(void *data)
{
	struct iio_dev *indio_dev = data;
	struct ina260_chip *chip = iio_priv(indio_dev);
	int sampling_us = SAMPLING_PERIOD(chip);
	struct timespec64 next, now, delta;
	s64 delay_us;
	int ret;

	ktime_get_ts64(&next);

	do {
		while (1) {
			/* Check if conversion is ready */
			ret = ina260_conversion_ready(indio_dev);
			if (ret < 0)
				return ret;

			/*
			 * If the conversion was not yet finished,
			 * reset the reference timestamp.
			 */
			if (ret == 0)
				ktime_get_ts64(&next);
			else
				break;
		}
		/* Read the data from sensor and push it to buffers */
		ret = ina260_work_buffer(indio_dev);
		if (ret < 0)
			return ret;

		ktime_get_ts64(&now);
		/*
		 * Advance the timestamp for the next poll by one sampling
		 * interval, and sleep for the remainder (next - now)
		 * In case "next" has already passed, the interval is added
		 * multiple times, i.e. samples are dropped.
		 */
		do {
			timespec64_add_ns(&next, 1000 * sampling_us);
			delta = timespec64_sub(next, now);
			delay_us = div_s64(timespec64_to_ns(&delta), 1000);
		} while (delay_us <= 0);

		usleep_range(delay_us, (delay_us * 3) >> 1);

	} while (!kthread_should_stop());

	return 0;
}

static int ina260_buffer_enable(struct iio_dev *indio_dev)
{
	struct ina260_chip *chip = iio_priv(indio_dev);
	unsigned int sampling_us = SAMPLING_PERIOD(chip);
	struct task_struct *task;

	task = kthread_create(ina260_capture_thread, (void *)indio_dev,
			      "%s:%d-%uus", indio_dev->name, iio_device_id(indio_dev),
			      sampling_us);

	if (IS_ERR(task))
		return PTR_ERR(task);

	get_task_struct(task);
	wake_up_process(task);
	chip->task = task;

	return 0;
}

static int ina260_buffer_disable(struct iio_dev *indio_dev)
{
	struct ina260_chip *chip = iio_priv(indio_dev);

	if (chip->task) {
		kthread_stop(chip->task);
		put_task_struct(chip->task);
		chip->task = NULL;
	}

	return 0;
}

static const struct iio_buffer_setup_ops ina260_setup_ops = {
	.postenable = &ina260_buffer_enable,
	.predisable = &ina260_buffer_disable,
};

static int ina260_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct ina260_chip *chip;
	enum ina260_ids type = 0;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);

	chip->regmap = devm_regmap_init_i2c(client, &ina260_regmap_config);
	if (IS_ERR(chip->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(chip->regmap);
	}

	mutex_init(&chip->lock);

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->dev.parent = &client->dev;
	indio_dev->dev.of_node = client->dev.of_node;

	chip->config = &ina260_config[type];
	ret = regmap_write(chip->regmap, INA260_CONFIG,
			   chip->config->config_default);
	if (ret) {
		dev_err(&client->dev, "Error configuring the device\n");
		return ret;
	}
	indio_dev->channels = ina260_channels;
	indio_dev->num_channels = ARRAY_SIZE(ina260_channels);
	indio_dev->info = &ina260_info;
	indio_dev->name = id->name;

	ret = devm_iio_kfifo_buffer_setup(&indio_dev->dev, indio_dev,
					  INDIO_BUFFER_SOFTWARE,
					  &ina260_setup_ops);
	if (ret)
		return ret;

	return iio_device_register(indio_dev);
}

static int ina260_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ina260_chip *chip = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	/* Power down */
	return regmap_update_bits(chip->regmap, INA260_CONFIG, INA260_MODE_MASK, 0);
}

static const struct i2c_device_id ina260_id[] = {
	{"ina260", ina260},
	{}
};
MODULE_DEVICE_TABLE(i2c, ina260_id);

static const struct of_device_id ina260_of_match[] = {
	{
		.compatible = "ti,ina260",
		.data = (void *)ina260
	},
	{},
};
MODULE_DEVICE_TABLE(of, ina260_of_match);

static struct i2c_driver ina260_driver = {
	.driver = {
		.name = "ina260-adc",
		.of_match_table = ina260_of_match,
	},
	.probe = ina260_probe,
	.remove = ina260_remove,
	.id_table = ina260_id,
};
module_i2c_driver(ina260_driver);

MODULE_AUTHOR("Raviteja Narayanam <raviteja.narayanam@xilinx.com>");
MODULE_DESCRIPTION("Texas Instruments INA 260 ADC driver");
MODULE_LICENSE("GPL v2");
