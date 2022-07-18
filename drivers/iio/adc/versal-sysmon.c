// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx SYSMON for Versal
 *
 * Copyright (C) 2019 - 2022 Xilinx, Inc.
 *
 * Description:
 * This driver is developed for SYSMON on Versal. The driver supports INDIO Mode
 * and supports voltage and temperature monitoring via IIO sysfs interface and
 * in kernel event monitoring for some modules.
 */

#include <linux/bits.h>
#include <dt-bindings/power/xlnx-versal-power.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/moduleparam.h>
#include "versal-sysmon.h"

#define SYSMON_EVENT_WORK_DELAY_MS	1000
#define SYSMON_UNMASK_WORK_DELAY_MS	500

static bool secure_mode;
module_param(secure_mode, bool, 0444);
MODULE_PARM_DESC(secure_mode,
		 "Allow sysmon to access register space using EEMI, when direct register access is restricted (default: Direct Access mode)");

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
};

/* Temperature event attributes */
static const struct iio_chan_spec temp_events[] = {
	SYSMON_CHAN_TEMP_EVENT(TEMP_EVENT, "temp", sysmon_temp_events),
	SYSMON_CHAN_TEMP_EVENT(OT_EVENT, "ot", sysmon_temp_events),
};

static inline void sysmon_direct_read_reg(struct sysmon *sysmon, u32 offset, u32 *data)
{
	*data = readl(sysmon->base + offset);
}

static inline void sysmon_direct_write_reg(struct sysmon *sysmon, u32 offset, u32 data)
{
	writel(data, sysmon->base + offset);
}

static inline void sysmon_direct_update_reg(struct sysmon *sysmon, u32 offset,
					    u32 mask, u32 data)
{
	u32 val;

	sysmon_direct_read_reg(sysmon, offset, &val);
	sysmon_direct_write_reg(sysmon, offset, (val & ~mask) | (mask & data));
}

static struct sysmon_ops direct_access = {
	.read_reg = sysmon_direct_read_reg,
	.write_reg = sysmon_direct_write_reg,
	.update_reg = sysmon_direct_update_reg,
};

static inline void sysmon_secure_read_reg(struct sysmon *sysmon, u32 offset, u32 *data)
{
	zynqmp_pm_sec_read_reg(PM_DEV_AMS_ROOT, offset, data);
}

static inline void sysmon_secure_write_reg(struct sysmon *sysmon, u32 offset, u32 data)
{
	zynqmp_pm_sec_mask_write_reg(PM_DEV_AMS_ROOT, offset, GENMASK(31, 0), data);
}

static inline void sysmon_secure_update_reg(struct sysmon *sysmon, u32 offset,
					    u32 mask, u32 data)
{
	u32 val;

	zynqmp_pm_sec_read_reg(PM_DEV_AMS_ROOT, offset, &val);
	zynqmp_pm_sec_mask_write_reg(PM_DEV_AMS_ROOT, offset, GENMASK(31, 0),
				     (val & ~mask) | (mask & data));
}

static struct sysmon_ops secure_access = {
	.read_reg = sysmon_secure_read_reg,
	.write_reg = sysmon_secure_write_reg,
	.update_reg = sysmon_secure_update_reg,
};

static void sysmon_read_reg(struct sysmon *sysmon, u32 offset, u32 *data)
{
	sysmon->ops->read_reg(sysmon, offset, data);
}

static void sysmon_write_reg(struct sysmon *sysmon, u32 offset, u32 data)
{
	sysmon->ops->write_reg(sysmon, offset, data);
}

static void sysmon_update_reg(struct sysmon *sysmon, u32 offset, u32 mask, u32 data)
{
	sysmon->ops->update_reg(sysmon, offset, mask, data);
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
		if (state) {
			sysmon_write_reg(sysmon, SYSMON_IER, ier);
			sysmon->temp_mask &= ~ier;
		} else {
			sysmon_write_reg(sysmon, SYSMON_IDR, ier);
			sysmon->temp_mask |= ier;
		}
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

/* sysmon instance for in kernel exported functions */
static struct sysmon *g_sysmon;

/**
 * sysmon_register_temp_ops - register temperature based event handler for a
 *			      given region.
 * @cb: callback function pointer.
 * @data: private data to be passed to the callback.
 * @region_id: id of the region for which the callback is to be set.
 * @return: 0 for success and negative number in case of failure.
 */
int sysmon_register_temp_ops(void (*cb)(void *data, struct regional_node *node),
			     void *data, enum sysmon_region region_id)
{
	struct sysmon *sysmon = g_sysmon;
	struct region_info *region;
	int ret = 0, found = 0;

	if (!cb || !sysmon)
		return -EINVAL;

	ret = mutex_lock_interruptible(&sysmon->mutex);
	if (ret) {
		dev_err(sysmon->dev, "Failed to acquire a lock. Process was interrupted by fatal signals");
		return ret;
	}

	if (list_empty(&sysmon->region_list)) {
		dev_err(sysmon->dev, "Failed to set a callback. HW node info missing in the device tree/ Not supported for this device");
		ret = -EINVAL;
		goto exit;
	}

	list_for_each_entry(region, &sysmon->region_list, list) {
		if (region->id == region_id) {
			found = 1;
			if (region->cb) {
				dev_err(sysmon->dev, "Error callback already set. Unregister the existing callback to set a new one.");
				ret = -EINVAL;
				goto exit;
			}
			region->cb = cb;
			region->data = data;
			break;
		}
	}

	if (!found) {
		dev_err(sysmon->dev, "Error invalid region. Please select the correct region");
		ret = -EINVAL;
	}

exit:
	mutex_unlock(&sysmon->mutex);
	return ret;
}
EXPORT_SYMBOL(sysmon_register_temp_ops);

/**
 * sysmon_unregister_temp_ops - Unregister the callback for temperature
 *				notification.
 * @region_id: id of the region for which the callback is to be set.
 * @return: 0 for success and negative number in case of failure.
 */
int sysmon_unregister_temp_ops(enum sysmon_region region_id)
{
	struct sysmon *sysmon = g_sysmon;
	struct region_info *region;
	int ret = 0, found = 0;

	if (!sysmon)
		return -EINVAL;

	ret = mutex_lock_interruptible(&sysmon->mutex);
	if (ret) {
		dev_err(sysmon->dev, "Failed to acquire a lock. Process was interrupted by fatal signals");
		return ret;
	}

	if (list_empty(&sysmon->region_list)) {
		dev_err(sysmon->dev, "Failed to set a callback. HW node info missing in the device tree/ Not supported for this device");
		ret = -EINVAL;
		goto exit;
	}

	list_for_each_entry(region, &sysmon->region_list, list) {
		if (region->id == region_id) {
			found = 1;
			region->cb = NULL;
			region->data = NULL;
			break;
		}
	}

	if (!found) {
		dev_err(sysmon->dev, "Error no such region. Please select the correct region");
		ret = -EINVAL;
	}

exit:
	mutex_unlock(&sysmon->mutex);
	return ret;
}
EXPORT_SYMBOL(sysmon_unregister_temp_ops);

/**
 * sysmon_nodes_by_region - returns the nodes list for a particular region.
 * @region_id: id for the region for which nodes are requested.
 * @return: Pointer to the linked list or NULL if region is not present.
 */
struct list_head *sysmon_nodes_by_region(enum sysmon_region region_id)
{
	struct sysmon *sysmon = g_sysmon;
	struct region_info *region;

	if (!sysmon)
		return NULL;

	list_for_each_entry(region, &sysmon->region_list, list) {
		if (region->id == region_id)
			return &region->node_list;
	}

	dev_err(sysmon->dev, "Error invalid region. Please select the correct region");

	return NULL;
}
EXPORT_SYMBOL(sysmon_nodes_by_region);

/**
 * sysmon_get_node_value - returns value of the sensor at a node.
 * @sat_id: id of the node.
 * @return: -EINVAL if not initialized or returns raw value of the sensor.
 */
int sysmon_get_node_value(int sat_id)
{
	struct sysmon *sysmon = g_sysmon;
	u32 raw;

	if (!sysmon)
		return -EINVAL;

	sysmon_read_reg(sysmon, SYSMON_NODE_OFFSET, &raw);

	return raw;
}
EXPORT_SYMBOL(sysmon_get_node_value);

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

static void sysmon_region_event_handler(struct sysmon *sysmon)
{
	struct region_info *region;
	struct regional_node *node, *eventnode;
	u32 regval, event = 0;
	u16 thresh_up, val;

	sysmon_read_reg(sysmon, SYSMON_TEMP_TH_UP, &regval);
	thresh_up = (u16)regval;

	list_for_each_entry(region, &sysmon->region_list, list) {
		list_for_each_entry(node, &region->node_list,
				    regional_node_list) {
			val = sysmon_get_node_value(node->sat_id);

			/* Find the highest value */
			if (compare(val, thresh_up)) {
				eventnode = node;
				eventnode->temp = val;
				thresh_up = val;
				event = 1;
			}
		}
		if (event && region->cb)
			region->cb(region->data, eventnode);
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
		sysmon->masked_temp |= BIT(SYSMON_BIT_TEMP);
		sysmon_region_event_handler(sysmon);
		break;

	case SYSMON_BIT_OT:
		address = OT_EVENT;
		sysmon_push_event(indio_dev, address);
		sysmon_write_reg(sysmon, SYSMON_IDR, BIT(SYSMON_BIT_OT));
		sysmon->masked_temp |= BIT(SYSMON_BIT_OT);
		sysmon_region_event_handler(sysmon);
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

static void sysmon_unmask_temp(struct sysmon *sysmon, unsigned int isr)
{
	unsigned int unmask, status;

	status = isr & SYSMON_TEMP_MASK;

	/* clear bits that are not active any more */
	unmask = (sysmon->masked_temp ^ status) & sysmon->masked_temp;
	sysmon->masked_temp &= status;

	/* clear status of disabled alarm */
	unmask &= ~sysmon->temp_mask;

	sysmon_write_reg(sysmon, SYSMON_IER, unmask);
}

/*
 * The Versal threshold interrupts are level sensitive. Since we can't make the
 * threshold condition go way from within the interrupt handler, this means as
 * soon as a threshold condition is present we would enter the interrupt handler
 * again and again. To work around this we mask all active thresholds interrupts
 * in the interrupt handler and start a timer. In this timer we poll the
 * interrupt status and only if the interrupt is inactive we unmask it again.
 */
static void sysmon_unmask_worker(struct work_struct *work)
{
	struct sysmon *sysmon = container_of(work, struct sysmon,
					     sysmon_unmask_work.work);
	unsigned int isr;

	spin_lock_irq(&sysmon->lock);

	/* Read the current interrupt status */
	sysmon_read_reg(sysmon, SYSMON_ISR, &isr);

	/* Clear interrupts */
	sysmon_write_reg(sysmon, SYSMON_ISR, isr);

	sysmon_unmask_temp(sysmon, isr);

	spin_unlock_irq(&sysmon->lock);

	/* if still pending some alarm re-trigger the timer */
	if (sysmon->masked_temp)
		schedule_delayed_work(&sysmon->sysmon_unmask_work,
				      msecs_to_jiffies(SYSMON_UNMASK_WORK_DELAY_MS));
	else
		/*
		 * Reset the temp_max_max and temp_min_min values to reset the
		 * previously reached high/low values during an alarm.
		 * This will enable the user to see the high/low values attained
		 * during an event
		 */
		sysmon_write_reg(sysmon, SYSMON_STATUS_RESET, 1);
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
		sysmon_handle_events(indio_dev, isr);

		schedule_delayed_work(&sysmon->sysmon_unmask_work,
				      msecs_to_jiffies(SYSMON_UNMASK_WORK_DELAY_MS));
	}

	spin_unlock(&sysmon->lock);

	return IRQ_HANDLED;
}

static void sysmon_events_worker(struct work_struct *work)
{
	u32 isr, imr;
	struct sysmon *sysmon = container_of(work, struct sysmon,
					     sysmon_events_work.work);

	spin_lock(&sysmon->lock);

	sysmon_read_reg(sysmon, SYSMON_ISR, &isr);
	sysmon_read_reg(sysmon, SYSMON_IMR, &imr);

	/* only process alarm that are not masked */
	isr &= ~imr;

	/* clear interrupt */
	sysmon_write_reg(sysmon, SYSMON_ISR, isr);

	if (isr) {
		sysmon_handle_events(sysmon->indio_dev, isr);
		schedule_delayed_work(&sysmon->sysmon_unmask_work,
				      msecs_to_jiffies(SYSMON_UNMASK_WORK_DELAY_MS));
	}
	spin_unlock(&sysmon->lock);

	schedule_delayed_work(&sysmon->sysmon_events_work,
			      msecs_to_jiffies(SYSMON_EVENT_WORK_DELAY_MS));
}

static int get_hw_node_properties(struct platform_device *pdev,
				  struct list_head *region_list)
{
	struct region_info *region = NULL;
	struct regional_node *nodes;
	struct device_node *np = pdev->dev.of_node;
	int size;
	u32 id, satid, x, y, i, offset, prev = 0;

	/* get hw-node-info */
	if (!of_get_property(np, "hw-node", &size))
		return 0;

	if (size % 16) {
		dev_info(&pdev->dev, "HW-Node properties not correct");
		return -EINVAL;
	}

	for (i = 0; i < (size / 16); i++) {
		offset = i * 4;
		of_property_read_u32_index(np, "hw-node", offset, &id);
		of_property_read_u32_index(np, "hw-node", offset + 1, &satid);
		of_property_read_u32_index(np, "hw-node", offset + 2, &x);
		of_property_read_u32_index(np, "hw-node", offset + 3, &y);

		if (list_empty(region_list) || prev != id) {
			region = devm_kzalloc(&pdev->dev, sizeof(*region),
					      GFP_KERNEL);
			if (!region)
				return -ENOMEM;

			region->id = id;
			INIT_LIST_HEAD(&region->node_list);
			list_add(&region->list, region_list);
		}

		prev = id;
		nodes = devm_kzalloc(&pdev->dev, sizeof(*nodes), GFP_KERNEL);
		if (!nodes)
			return -ENOMEM;
		nodes->sat_id = satid;
		nodes->x = x;
		nodes->y = y;
		list_add(&nodes->regional_node_list, &region->node_list);
	}

	return 0;
}

static int sysmon_parse_dt(struct iio_dev *indio_dev,
			   struct platform_device *pdev)
{
	struct sysmon *sysmon;
	struct iio_chan_spec *sysmon_channels;
	struct device_node *child_node = NULL, *np = pdev->dev.of_node;
	int ret, i = 0;
	u8 num_supply_chan = 0;
	u32 reg = 0, num_temp_chan = 0;
	const char *name;
	u32 chan_size = sizeof(struct iio_chan_spec);
	u32 temp_chan_size;

	sysmon = iio_priv(indio_dev);
	ret = of_property_read_u8(np, "xlnx,numchannels", &num_supply_chan);
	if (ret < 0)
		return ret;

	INIT_LIST_HEAD(&sysmon->region_list);

	if (sysmon->irq > 0)
		get_hw_node_properties(pdev, &sysmon->region_list);

	/* Initialize buffer for channel specification */
	temp_chan_size = (sizeof(temp_channels) + sizeof(temp_events));

	num_temp_chan = ARRAY_SIZE(temp_channels);

	sysmon_channels = devm_kzalloc(&pdev->dev,
				       (chan_size * num_supply_chan) +
				       temp_chan_size, GFP_KERNEL);

	for_each_child_of_node(np, child_node) {
		ret = of_property_read_u32(child_node, "reg", &reg);
		if (ret < 0) {
			of_node_put(child_node);
			return ret;
		}

		ret = of_property_read_string(child_node, "xlnx,name", &name);
		if (ret < 0) {
			of_node_put(child_node);
			return ret;
		}

		sysmon_channels[i].type = IIO_VOLTAGE;
		sysmon_channels[i].indexed = 1;
		sysmon_channels[i].address = reg;
		sysmon_channels[i].channel = reg;
		sysmon_channels[i].info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_PROCESSED);

		sysmon_channels[i].event_spec = sysmon_supply_events;
		sysmon_channels[i].num_event_specs = ARRAY_SIZE(sysmon_supply_events);

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
	indio_dev->num_channels = num_supply_chan + ARRAY_SIZE(temp_channels);

	memcpy(sysmon_channels + num_supply_chan + num_temp_chan,
	       temp_events, sizeof(temp_events));
	indio_dev->num_channels += ARRAY_SIZE(temp_events);

	indio_dev->channels = sysmon_channels;

	return 0;
}

static void sysmon_init_interrupt(struct sysmon *sysmon)
{
	u32 imr;

	/* Read default Interrupt Mask */
	sysmon_read_reg(sysmon, SYSMON_IMR, &imr);
	sysmon->temp_mask = imr & SYSMON_TEMP_MASK;
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

	sysmon->dev = &pdev->dev;
	sysmon->indio_dev = indio_dev;

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

	if (secure_mode) {
		ret = zynqmp_pm_feature(PM_IOCTL);
		if (ret < 0) {
			dev_err(&pdev->dev, "Feature check failed with %d\n", ret);
			return ret;
		}
		if ((ret & FIRMWARE_VERSION_MASK) < PM_API_VERSION_2) {
			dev_err(&pdev->dev, "IOCTL firmware version error. Expected: v%d - Found: v%d\n",
				PM_API_VERSION_2, ret & FIRMWARE_VERSION_MASK);
			return -EOPNOTSUPP;
		}
		sysmon->ops = &secure_access;
	} else {
		sysmon->ops = &direct_access;
	}

	sysmon_write_reg(sysmon, SYSMON_NPI_LOCK, NPI_UNLOCK);

	sysmon->irq = platform_get_irq_optional(pdev, 0);

	ret = sysmon_parse_dt(indio_dev, pdev);
	if (ret)
		return ret;

	INIT_DELAYED_WORK(&sysmon->sysmon_unmask_work,
			  sysmon_unmask_worker);
	sysmon_init_interrupt(sysmon);
	if (sysmon->irq > 0) {
		g_sysmon = sysmon;
		ret = devm_request_irq(&pdev->dev, sysmon->irq, &sysmon_iio_irq,
				       0, "sysmon-irq", indio_dev);
		if (ret < 0)
			return ret;
	} else if (sysmon->irq == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else {
		INIT_DELAYED_WORK(&sysmon->sysmon_events_work,
				  sysmon_events_worker);
		schedule_delayed_work(&sysmon->sysmon_events_work,
				      msecs_to_jiffies(SYSMON_EVENT_WORK_DELAY_MS));
	}

	platform_set_drvdata(pdev, indio_dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		return ret;

	dev_info(&pdev->dev, "Successfully registered Versal Sysmon");

	return 0;
}

static int sysmon_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct sysmon *sysmon = iio_priv(indio_dev);

	/* cancel SSIT based events */
	if (sysmon->irq < 0)
		cancel_delayed_work_sync(&sysmon->sysmon_events_work);

	cancel_delayed_work_sync(&sysmon->sysmon_unmask_work);

	/* Unregister the device */
	iio_device_unregister(indio_dev);
	return 0;
}

static int sysmon_resume(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct sysmon *sysmon = iio_priv(indio_dev);

	sysmon_write_reg(sysmon, SYSMON_NPI_LOCK, NPI_UNLOCK);

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
	.resume = sysmon_resume,
	.driver = {
		.name = "sysmon",
		.of_match_table = sysmon_of_match_table,
	},
};
module_platform_driver(sysmon_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Xilinx Versal SysMon Driver");
MODULE_AUTHOR("Anand Ashok Dumbre <anand.ashok.dumbre@xilinx.com>");
