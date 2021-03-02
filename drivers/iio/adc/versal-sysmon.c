// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx SYSMON for Versal
 *
 * Copyright (C) 2019 - 2020 Xilinx, Inc.
 *
 * Description:
 * This driver is developed for SYSMON on Versal. The driver supports INDIO Mode
 * and supports voltage and temperature monitoring via IIO sysfs interface.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/sysfs.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_address.h>

/* Channel IDs for Temp Channels */
/* TEMP_MAX gives the current temperature for Production
 * silicon.
 * TEMP_MAX gives the current maximum temperature for ES1
 * silicon.
 */
#define TEMP_MAX	160

/* TEMP_MIN is not applicable for Production silicon.
 * TEMP_MIN gives the current minimum temperature for ES1 silicon.
 */
#define TEMP_MIN	161

#define TEMP_MAX_MAX	162
#define TEMP_MIN_MIN	163
#define TEMP_EVENT	164
#define OT_EVENT	165

/* Register Unlock Code */
#define NPI_UNLOCK	0xF9E8D7C6

/* Register Offsets */
#define SYSMON_NPI_LOCK		0x000C
#define SYSMON_ISR		0x0044
#define SYSMON_IMR		0x0048
#define SYSMON_IER		0x004C
#define SYSMON_IDR		0x0050
#define SYSMON_ALARM_FLAG	0x1018
#define SYSMON_TEMP_MAX		0x1030
#define SYSMON_TEMP_MIN		0x1034
#define SYSMON_SUPPLY_BASE	0x1040
#define SYSMON_ALARM_REG	0x1940
#define SYSMON_TEMP_TH_LOW	0x1970
#define SYSMON_TEMP_TH_UP	0x1974
#define SYSMON_OT_TH_LOW	0x1978
#define SYSMON_OT_TH_UP		0x197C
#define SYSMON_SUPPLY_TH_LOW	0x1980
#define SYSMON_SUPPLY_TH_UP	0x1C80
#define SYSMON_TEMP_MAX_MAX	0x1F90
#define SYSMON_TEMP_MIN_MIN	0x1F8C
#define SYSMON_TEMP_EV_CFG	0x1F84

#define SYSMON_NO_OF_EVENTS	32

/* Supply Voltage Conversion macros */
#define SYSMON_MANTISSA_MASK		0xFFFF
#define SYSMON_FMT_MASK			0x10000
#define SYSMON_FMT_SHIFT		16
#define SYSMON_MODE_MASK		0x60000
#define SYSMON_MODE_SHIFT		17
#define SYSMON_MANTISSA_SIGN_SHIFT	15
#define SYSMON_UPPER_SATURATION_SIGNED	32767
#define SYSMON_LOWER_SATURATION_SIGNED	-32768
#define SYSMON_UPPER_SATURATION		65535
#define SYSMON_LOWER_SATURATION		0

#define SYSMON_CHAN_TEMP_EVENT(_address, _ext, _events) { \
	.type = IIO_TEMP, \
	.indexed = 1, \
	.address = _address, \
	.channel = _address, \
	.event_spec = _events, \
	.num_event_specs = ARRAY_SIZE(_events), \
	.scan_type = { \
		.sign = 's', \
		.realbits = 15, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	}, \
	.extend_name = _ext, \
	}

#define SYSMON_CHAN_TEMP(_address, _ext) { \
	.type = IIO_TEMP, \
	.indexed = 1, \
	.address = _address, \
	.channel = _address, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_PROCESSED), \
	.scan_type = { \
		.sign = 's', \
		.realbits = 15, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	}, \
	.extend_name = _ext, \
}

#define twoscomp(val) ((((val) ^ 0xFFFF) + 1) & 0x0000FFFF)
#define ALARM_REG(address) ((address) / 32)
#define ALARM_SHIFT(address) ((address) % 32)

enum sysmon_alarm_bit {
	SYSMON_BIT_ALARM0 = 0,
	SYSMON_BIT_ALARM1 = 1,
	SYSMON_BIT_ALARM2 = 2,
	SYSMON_BIT_ALARM3 = 3,
	SYSMON_BIT_ALARM4 = 4,
	SYSMON_BIT_ALARM5 = 5,
	SYSMON_BIT_ALARM6 = 6,
	SYSMON_BIT_ALARM7 = 7,
	SYSMON_BIT_OT = 8,
	SYSMON_BIT_TEMP = 9,
};

/**
 * struct sysmon - Driver data for Sysmon
 * @base: physical base address of device
 * @dev: pointer to device struct
 * @mutex: to handle multiple user interaction
 * @lock: to help manage interrupt registers correctly
 * @irq: interrupt number of the sysmon
 *
 * This structure contains necessary state for Sysmon driver to operate
 */
struct sysmon {
	void __iomem *base;
	struct device *dev;
	/* kernel doc above */
	struct mutex mutex;
	/* kernel doc above*/
	spinlock_t lock;
	int irq;
};

/* This structure describes temperature events */
static const struct iio_event_spec sysmon_temp_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate =
			BIT(IIO_EV_INFO_ENABLE) | BIT(IIO_EV_INFO_HYSTERESIS),
	},
};

/* This structure describes voltage events */
static const struct iio_event_spec sysmon_supply_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

/* Temperature channel attributes */
static const struct iio_chan_spec temp_channels[] = {
	SYSMON_CHAN_TEMP(TEMP_MAX, "temp"),
	SYSMON_CHAN_TEMP(TEMP_MIN, "min"),
	SYSMON_CHAN_TEMP(TEMP_MAX_MAX, "max_max"),
	SYSMON_CHAN_TEMP(TEMP_MIN_MIN, "min_min"),
	SYSMON_CHAN_TEMP_EVENT(TEMP_EVENT, "temp", sysmon_temp_events),
	SYSMON_CHAN_TEMP_EVENT(OT_EVENT, "ot", sysmon_temp_events),
};

static inline void sysmon_read_reg(struct sysmon *sysmon, u32 offset, u32 *data)
{
	*data = readl(sysmon->base + offset);
}

static inline void sysmon_write_reg(struct sysmon *sysmon, u32 offset, u32 data)
{
	writel(data, sysmon->base + offset);
}

static inline void sysmon_update_reg(struct sysmon *sysmon, u32 offset,
				     u32 mask, u32 data)
{
	u32 val;

	sysmon_read_reg(sysmon, offset, &val);
	sysmon_write_reg(sysmon, offset, (val & ~mask) | (mask & data));
}

static u32 sysmon_temp_offset(int address)
{
	switch (address) {
	case TEMP_MAX:
		return SYSMON_TEMP_MAX;
	case TEMP_MIN:
		return SYSMON_TEMP_MIN;
	case TEMP_MAX_MAX:
		return SYSMON_TEMP_MAX_MAX;
	case TEMP_MIN_MIN:
		return SYSMON_TEMP_MIN_MIN;
	default:
		return -EINVAL;
	}
	return -EINVAL;
}

static u32 sysmon_temp_thresh_offset(int address, enum iio_event_direction dir)
{
	switch (address) {
	case TEMP_EVENT:
		return (dir == IIO_EV_DIR_RISING) ? SYSMON_TEMP_TH_UP :
						    SYSMON_TEMP_TH_LOW;
	case OT_EVENT:
		return (dir == IIO_EV_DIR_RISING) ? SYSMON_OT_TH_UP :
						    SYSMON_OT_TH_LOW;
	default:
		return -EINVAL;
	}
	return -EINVAL;
}

static u32 sysmon_supply_offset(int address)
{
	return (address * 4) + SYSMON_SUPPLY_BASE;
}

static u32 sysmon_supply_thresh_offset(int address,
				       enum iio_event_direction dir)
{
	if (dir == IIO_EV_DIR_RISING)
		return (address * 4) + SYSMON_SUPPLY_TH_UP;
	else if (dir == IIO_EV_DIR_FALLING)
		return (address * 4) + SYSMON_SUPPLY_TH_LOW;

	return -EINVAL;
}

/**
 * sysmon_q8p7_to_celsius() - converts fixed point Q8.7 format to a fraction.
 * @raw_data: Raw ADC value
 * @val: The numerator of the fraction needed by IIO_VAL_PROCESSED
 * @val2: Denominator of the fraction needed by IIO_VAL_PROCESSED
 *
 * The function returns a fraction which returns celsius
 */
static void sysmon_q8p7_to_celsius(int raw_data, int *val, int *val2)
{
	*val = (raw_data & 0x8000) ? -(twoscomp(raw_data)) : raw_data;
	*val2 = 128;
}

/**
 * sysmon_celsius_to_q8p7() - converts value from IIO Framework to ADC Raw data
 * @raw_data: Raw ADC value
 * @val: The numerator of the fraction provided by the IIO Framework
 * @val2: Denominator of the fraction provided by the IIO Framework
 *
 * The function takes in exponent and mantissa as val and val2 respectively
 * of temperature value in Deg Celsius and returns raw adc value for the
 * given temperature.
 */
static void sysmon_celsius_to_q8p7(u32 *raw_data, int val, int val2)
{
	int scale = 1 << 7;

	/* The value is scaled by 10^6 in the IIO framework
	 * dividing by 1000 twice to avoid overflow
	 */
	val2 = val2 / 1000;
	*raw_data = (val * scale) + ((val2 * scale) / 1000);
}

static void sysmon_supply_rawtoprocessed(int raw_data, int *val, int *val2)
{
	int mantissa, format, exponent;

	mantissa = raw_data & SYSMON_MANTISSA_MASK;
	exponent = (raw_data & SYSMON_MODE_MASK) >> SYSMON_MODE_SHIFT;
	format = (raw_data & SYSMON_FMT_MASK) >> SYSMON_FMT_SHIFT;

	*val2 = 1 << (16 - exponent);
	*val = mantissa;
	if (format && (mantissa >> SYSMON_MANTISSA_SIGN_SHIFT))
		*val = (~(mantissa) & SYSMON_MANTISSA_MASK) * -1;
}

static void sysmon_supply_processedtoraw(int val, int val2, u32 reg_val,
					 u32 *raw_data)
{
	int exponent = (reg_val & SYSMON_MODE_MASK) >> SYSMON_MODE_SHIFT;
	int format = (reg_val & SYSMON_FMT_MASK) >> SYSMON_FMT_SHIFT;
	int scale = 1 << (16 - exponent);
	int tmp;

	/**
	 * The value is scaled by 10^6 in the IIO framework
	 * dividing by 1000 twice to avoid overflow
	 */
	val2 = val2 / 1000;
	tmp = (val * scale) + ((val2 * scale) / 1000);

	/* Set out of bound values to saturation levels */
	if (format) {
		if (tmp > SYSMON_UPPER_SATURATION_SIGNED)
			tmp = 0x7fff;
		else if (tmp < SYSMON_LOWER_SATURATION_SIGNED)
			tmp = 0x8000;

	} else {
		if (tmp > SYSMON_UPPER_SATURATION)
			tmp = 0xffff;
		else if (tmp < SYSMON_LOWER_SATURATION)
			tmp = 0x0000;
	}

	*raw_data = tmp & 0xffff;
}

static int sysmon_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	struct sysmon *sysmon = iio_priv(indio_dev);
	u32 offset, regval;
	u32 ret = -EINVAL;

	mutex_lock(&sysmon->mutex);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_TEMP:
			offset = sysmon_temp_offset(chan->address);
			sysmon_read_reg(sysmon, offset, val);
			*val2 = 0;
			ret = IIO_VAL_INT;
			break;

		case IIO_VOLTAGE:
			offset = sysmon_supply_offset(chan->address);
			sysmon_read_reg(sysmon, offset, val);
			*val2 = 0;
			ret = IIO_VAL_INT;
			break;

		default:
			break;
		}
		break;

	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_TEMP:
			/* In Deg C */
			offset = sysmon_temp_offset(chan->address);
			sysmon_read_reg(sysmon, offset, &regval);
			sysmon_q8p7_to_celsius(regval, val, val2);
			ret = IIO_VAL_FRACTIONAL;
			break;

		case IIO_VOLTAGE:
			/* In Volts */
			offset = sysmon_supply_offset(chan->address);
			sysmon_read_reg(sysmon, offset, &regval);
			sysmon_supply_rawtoprocessed(regval, val, val2);
			ret = IIO_VAL_FRACTIONAL;
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}

	mutex_unlock(&sysmon->mutex);
	return ret;
}

static int sysmon_get_event_mask(unsigned long address)
{
	if (address == TEMP_EVENT)
		return BIT(SYSMON_BIT_TEMP);
	else if (address == OT_EVENT)
		return BIT(SYSMON_BIT_OT);

	/* return supply */
	return BIT(address / 32);
}

static int sysmon_read_alarm_config(struct sysmon *sysmon,
				    unsigned long address)
{
	u32 reg_val;
	u32 alarm_reg_num = ALARM_REG(address);
	u32 shift = ALARM_SHIFT(address);
	u32 offset = SYSMON_ALARM_REG + (4 * alarm_reg_num);

	sysmon_read_reg(sysmon, offset, &reg_val);

	return reg_val & BIT(shift);
}

static void sysmon_write_alarm_config(struct sysmon *sysmon,
				      unsigned long address, u32 val)
{
	u32 alarm_reg_num = ALARM_REG(address);
	u32 shift = ALARM_SHIFT(address);
	u32 offset = SYSMON_ALARM_REG + (4 * alarm_reg_num);

	sysmon_update_reg(sysmon, offset, BIT(shift), (val << shift));
}

static int sysmon_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct sysmon *sysmon = iio_priv(indio_dev);
	u32 config_value, imr;
	u32 alarm_event_mask = sysmon_get_event_mask(chan->address);

	sysmon_read_reg(sysmon, SYSMON_IMR, &imr);

	/* Getting the unmasked interrupts */
	imr = ~imr;

	if (chan->type == IIO_VOLTAGE) {
		config_value = sysmon_read_alarm_config(sysmon, chan->address);

		return (config_value && (imr & alarm_event_mask));
	}

	return (imr & sysmon_get_event_mask(chan->address)) ? 1 : 0;
}

static int sysmon_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir, int state)
{
	struct sysmon *sysmon = iio_priv(indio_dev);
	u32 alarm_reg_num = ALARM_REG(chan->address);
	u32 offset = SYSMON_ALARM_REG + (4 * alarm_reg_num);
	u32 ier = sysmon_get_event_mask(chan->address);
	u32 alarm_config;
	unsigned long flags;

	mutex_lock(&sysmon->mutex);
	spin_lock_irqsave(&sysmon->lock, flags);

	if (chan->type == IIO_VOLTAGE) {
		sysmon_write_alarm_config(sysmon, chan->address, state);

		sysmon_read_reg(sysmon, offset, &alarm_config);

		if (alarm_config)
			sysmon_write_reg(sysmon, SYSMON_IER, ier);
		else
			sysmon_write_reg(sysmon, SYSMON_IDR, ier);

	} else {
		if (state)
			sysmon_write_reg(sysmon, SYSMON_IER, ier);
		else
			sysmon_write_reg(sysmon, SYSMON_IDR, ier);
	}

	spin_unlock_irqrestore(&sysmon->lock, flags);
	mutex_unlock(&sysmon->mutex);

	return 0;
}

static int sysmon_read_event_value(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir,
				   enum iio_event_info info, int *val,
				   int *val2)
{
	struct sysmon *sysmon = iio_priv(indio_dev);
	u32 offset, reg_val;
	u32 mask, shift;
	u32 ret = -EINVAL;

	mutex_lock(&sysmon->mutex);
	if (chan->type == IIO_TEMP) {
		if (info == IIO_EV_INFO_VALUE) {
			offset = sysmon_temp_thresh_offset(chan->address, dir);
			sysmon_read_reg(sysmon, offset, &reg_val);
			sysmon_q8p7_to_celsius(reg_val, val, val2);
			ret = IIO_VAL_FRACTIONAL;
		} else if (info == IIO_EV_INFO_HYSTERESIS) {
			mask = (chan->address == OT_EVENT) ? 0x1 : 0x2;
			shift = mask - 1;
			sysmon_read_reg(sysmon, SYSMON_TEMP_EV_CFG, &reg_val);
			*val = (reg_val & mask) >> shift;
			*val2 = 0;
			ret = IIO_VAL_INT;
		}
	} else if (chan->type == IIO_VOLTAGE) {
		offset = sysmon_supply_thresh_offset(chan->address, dir);
		sysmon_read_reg(sysmon, offset, &reg_val);
		sysmon_supply_rawtoprocessed(reg_val, val, val2);
		ret = IIO_VAL_FRACTIONAL;
	}

	mutex_unlock(&sysmon->mutex);
	return ret;
}

static int sysmon_write_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info, int val, int val2)
{
	struct sysmon *sysmon = iio_priv(indio_dev);
	u32 offset, raw_val, reg_val;
	u32 mask, shift;

	mutex_lock(&sysmon->mutex);
	if (chan->type == IIO_TEMP) {
		if (info == IIO_EV_INFO_VALUE) {
			offset = sysmon_temp_thresh_offset(chan->address, dir);
			sysmon_celsius_to_q8p7(&reg_val, val, val2);
			sysmon_write_reg(sysmon, offset, reg_val);
		} else if (info == IIO_EV_INFO_HYSTERESIS) {
			/* calculating the mask value for OT and TEMP Alarms */
			mask = (chan->address == OT_EVENT) ? 1 : 2;
			shift = mask - 1;
			sysmon_update_reg(sysmon, SYSMON_TEMP_EV_CFG, mask,
					  (val << shift));
		}
	} else if (chan->type == IIO_VOLTAGE) {
		offset = sysmon_supply_thresh_offset(chan->address, dir);
		sysmon_read_reg(sysmon, offset, &reg_val);
		sysmon_supply_processedtoraw(val, val2, reg_val, &raw_val);
		sysmon_write_reg(sysmon, offset, raw_val);
	}

	mutex_unlock(&sysmon->mutex);
	return 0;
}

static const struct iio_info iio_dev_info = {
	.read_raw = sysmon_read_raw,
	.read_event_config = sysmon_read_event_config,
	.write_event_config = sysmon_write_event_config,
	.read_event_value = sysmon_read_event_value,
	.write_event_value = sysmon_write_event_value,
};

static void sysmon_push_event(struct iio_dev *indio_dev, u32 address)
{
	u32 i;
	const struct iio_chan_spec *chan;

	for (i = 0; i < indio_dev->num_channels; i++) {
		if (indio_dev->channels[i].address == address) {
			chan = &indio_dev->channels[i];
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(chan->type,
							    chan->channel,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_EITHER),
				       iio_get_time_ns(indio_dev));
		}
	}
}

static void sysmon_handle_event(struct iio_dev *indio_dev, u32 event)
{
	struct sysmon *sysmon = iio_priv(indio_dev);
	unsigned long alarm_flag_reg;
	u32 address, bit, reg_val;
	u32 alarm_flag_offset = SYSMON_ALARM_FLAG + (event * 4);
	u32 alarm_reg_offset = SYSMON_ALARM_REG + (event * 4);

	switch (event) {
	case SYSMON_BIT_TEMP:
		address = TEMP_EVENT;
		sysmon_push_event(indio_dev, address);
		sysmon_write_reg(sysmon, SYSMON_IDR, BIT(SYSMON_BIT_TEMP));
		break;

	case SYSMON_BIT_OT:
		address = OT_EVENT;
		sysmon_push_event(indio_dev, address);
		sysmon_write_reg(sysmon, SYSMON_IDR, BIT(SYSMON_BIT_TEMP));
		break;

	case SYSMON_BIT_ALARM4:
	case SYSMON_BIT_ALARM3:
	case SYSMON_BIT_ALARM2:
	case SYSMON_BIT_ALARM1:
	case SYSMON_BIT_ALARM0:
		/* Read enabled alarms */
		sysmon_read_reg(sysmon, alarm_flag_offset, &reg_val);
		alarm_flag_reg = (unsigned long)reg_val;

		for_each_set_bit(bit, &alarm_flag_reg, 32) {
			address = bit + (32 * event);
			sysmon_push_event(indio_dev, address);
			/* disable alarm */
			sysmon_update_reg(sysmon, alarm_reg_offset, BIT(bit),
					  0);
		}
		/* clear alarms */
		sysmon_write_reg(sysmon, alarm_flag_offset, alarm_flag_reg);
		break;

	default:
		break;
	}
}

static void sysmon_handle_events(struct iio_dev *indio_dev,
				 unsigned long events)
{
	unsigned int bit;

	for_each_set_bit(bit, &events, SYSMON_NO_OF_EVENTS)
		sysmon_handle_event(indio_dev, bit);
}

static irqreturn_t sysmon_iio_irq(int irq, void *data)
{
	u32 isr, imr;
	struct iio_dev *indio_dev = data;
	struct sysmon *sysmon = iio_priv(indio_dev);

	spin_lock(&sysmon->lock);

	sysmon_read_reg(sysmon, SYSMON_ISR, &isr);
	sysmon_read_reg(sysmon, SYSMON_IMR, &imr);

	/* only process alarm that are not masked */
	isr &= ~imr;

	/* clear interrupt */
	sysmon_write_reg(sysmon, SYSMON_ISR, isr);

	if (isr) {
		/* Clear the interrupts*/
		sysmon_handle_events(indio_dev, isr);
	}

	spin_unlock(&sysmon->lock);

	return IRQ_HANDLED;
}

static int sysmon_parse_dt(struct iio_dev *indio_dev,
			   struct platform_device *pdev)
{
	struct iio_chan_spec *sysmon_channels;
	struct device_node *child_node = NULL, *np = pdev->dev.of_node;
	int ret, i = 0;
	u8 num_supply_chan = 0;
	u32 reg = 0;
	const char *name;
	u32 chan_size = sizeof(struct iio_chan_spec);

	ret = of_property_read_u8(np, "xlnx,numchannels", &num_supply_chan);
	if (ret < 0)
		return ret;

	/* Initialize buffer for channel specification */
	sysmon_channels = devm_kzalloc(&pdev->dev,
				       (chan_size * num_supply_chan) +
				       sizeof(temp_channels),
				       GFP_KERNEL);

	for_each_child_of_node(np, child_node) {
		ret = of_property_read_u32(child_node, "reg", &reg);
		if (ret < 0)
			return ret;

		ret = of_property_read_string(child_node, "xlnx,name", &name);
		if (ret < 0)
			return ret;

		sysmon_channels[i].type = IIO_VOLTAGE;
		sysmon_channels[i].indexed = 1;
		sysmon_channels[i].address = reg;
		sysmon_channels[i].channel = reg;
		sysmon_channels[i].info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_PROCESSED);
		sysmon_channels[i].event_spec = sysmon_supply_events;
		sysmon_channels[i].num_event_specs =
			ARRAY_SIZE(sysmon_supply_events);
		sysmon_channels[i].scan_index = i;
		sysmon_channels[i].scan_type.realbits = 19;
		sysmon_channels[i].scan_type.storagebits = 32;

		sysmon_channels[i].scan_type.endianness = IIO_CPU;
		sysmon_channels[i].extend_name = name;

		if (of_property_read_bool(child_node, "xlnx,bipolar"))
			sysmon_channels[i].scan_type.sign = 's';
		else
			sysmon_channels[i].scan_type.sign = 'u';

		i++;
	}

	/* Append static temperature channels to the channel list */
	memcpy(sysmon_channels + num_supply_chan, temp_channels,
	       sizeof(temp_channels));

	indio_dev->channels = sysmon_channels;
	indio_dev->num_channels = num_supply_chan + ARRAY_SIZE(temp_channels);

	return 0;
}

static int sysmon_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct sysmon *sysmon;
	struct resource *mem;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*sysmon));
	if (!indio_dev)
		return -ENOMEM;

	sysmon = iio_priv(indio_dev);

	sysmon->irq = platform_get_irq(pdev, 0);
	if (sysmon->irq <= 0)
		return -ENXIO;

	mutex_init(&sysmon->mutex);
	spin_lock_init(&sysmon->lock);

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->name = "xlnx,versal-sysmon";
	indio_dev->info = &iio_dev_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sysmon->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(sysmon->base))
		return PTR_ERR(sysmon->base);

	sysmon_write_reg(sysmon, SYSMON_NPI_LOCK, NPI_UNLOCK);

	ret = sysmon_parse_dt(indio_dev, pdev);
	if (ret)
		return ret;

	ret = devm_request_irq(&pdev->dev, sysmon->irq, &sysmon_iio_irq, 0,
			       "sysmon-irq", indio_dev);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, indio_dev);

	dev_info(&pdev->dev, "Successfully registered Versal Sysmon");

	return iio_device_register(indio_dev);
}

static int sysmon_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	/* Unregister the device */
	iio_device_unregister(indio_dev);
	return 0;
}

static const struct of_device_id sysmon_of_match_table[] = {
	{ .compatible = "xlnx,versal-sysmon" },
	{}
};
MODULE_DEVICE_TABLE(of, sysmon_of_match_table);

static struct platform_driver sysmon_driver = {
	.probe = sysmon_probe,
	.remove = sysmon_remove,
	.driver = {
		.name = "sysmon",
		.of_match_table = sysmon_of_match_table,
	},
};
module_platform_driver(sysmon_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Xilinx Versal SysMon Driver");
MODULE_AUTHOR("Anand Ashok Dumbre <anand.ashok.dumbre@xilinx.com>");
