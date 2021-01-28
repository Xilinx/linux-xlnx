// SPDX-License-Identifier: GPL-2.0+
/*
 * DA9121 Single-channel dual-phase 10A buck converter
 * DA9130 Single-channel dual-phase 10A buck converter (Automotive)
 * DA9217 Single-channel dual-phase  6A buck converter
 * DA9122 Dual-channel single-phase  5A buck converter
 * DA9131 Dual-channel single-phase  5A buck converter (Automotive)
 * DA9220 Dual-channel single-phase  3A buck converter
 * DA9132 Dual-channel single-phase  3A buck converter (Automotive)
 *
 * Copyright (C) 2019  Dialog Semiconductor
 *
 * Author: Steve Twiss for Dialog Semiconductor
 * Author: Adam Ward for Dialog Semiconductor
 *
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regmap.h>
#include <linux/regulator/of_regulator.h>
#include <linux/workqueue.h>
#include "da9121-regulator.h"

/*
 * Minimum, maximum and default polling millisecond periods are provided
 * here as an example. It is expected that any final implementation will
 * include a modification of these settings to match the required
 * application.
 */
#define DA9121_DEFAULT_POLLING_PERIOD_MS	3000
#define DA9121_MAX_POLLING_PERIOD_MS		10000
#define DA9121_MIN_POLLING_PERIOD_MS		1000

/* Device ID list */
#define DA9121_DEVICE_ID	0x05
#define DA9121_VARIANT_MRC_BASE	0x2
#define DA9130_VARIANT_VRC	0X0
#define DA9131_VARIANT_VRC	0X1
#define DA9122_VARIANT_VRC	0x2
#define DA9217_VARIANT_VRC	0x7

/* Regulator types - first index into local_da9121_regulators[][] */
enum device_variant {
	DA9121_TYPE_DA9121_DA9130,
	DA9121_TYPE_DA9220_DA9132,
	DA9121_TYPE_DA9122_DA9131,
	DA9121_TYPE_DA9217,
	DA9121_TYPE_NUM
};

/*
 * Regulator Names - for da9121_matches[], chip->rdev[], and second index into
 * local_da9121_regulators[][]
 */
enum {
	DA9121_INDEX_BUCK1,
	DA9121_INDEX_BUCK2,
	DA9121_MAX_REGULATORS
};

/*
 * Regulator IDs - buck id in local_da9121_regulators[][], used to find buck's
 * current/mode register info
 */
enum {
	DA9121_DA9130_ID_BUCK1,
	DA9220_DA9132_ID_BUCK1,
	DA9220_DA9132_ID_BUCK2,
	DA9122_DA9131_ID_BUCK1,
	DA9122_DA9131_ID_BUCK2,
	DA9217_ID_BUCK1
};

/* Device tree data */
struct da9121_dt_data {
	unsigned int num_matches;
	struct gpio_desc *gpiod_ren[DA9121_MAX_REGULATORS];
	struct device_node *reg_node[DA9121_MAX_REGULATORS];
	struct regulator_init_data *init_data[DA9121_MAX_REGULATORS];
};

/* Chip data */
struct da9121 {
	struct device *dev;
	struct delayed_work work;
	struct regmap *regmap;
	struct da9121_dt_data *dt_data;
	struct regulator_dev *rdev[DA9121_MAX_REGULATORS];
	unsigned int persistent[2];
	unsigned int passive_delay;
	int chip_irq;
	int variant_id;
};

/*
 * Define ranges for different variants, enabling translation to/from
 * registers. Maximums give scope to allow for transients.
 */
struct da9121_range {
	int val_min;
	int val_max;
	int val_stp;
	int reg_min;
	int reg_max;
};

struct da9121_range da9121_10A_2phase_current = {
	.val_min =  7000000,
	.val_max = 20000000,
	.val_stp =  1000000,
	.reg_min = 1,
	.reg_max = 14,
};

struct da9121_range da9121_6A_2phase_current = {
	.val_min =  7000000,
	.val_max = 12000000,
	.val_stp =  1000000,
	.reg_min = 1,
	.reg_max = 6,
};

struct da9121_range da9121_5A_1phase_current = {
	.val_min =  3500000,
	.val_max = 10000000,
	.val_stp =   500000,
	.reg_min = 1,
	.reg_max = 14,
};

struct da9121_range da9121_3A_1phase_current = {
	.val_min = 3500000,
	.val_max = 6000000,
	.val_stp =  500000,
	.reg_min = 1,
	.reg_max = 6,
};

struct da9121_variant {
	int num_bucks;
	int num_phases;
	struct da9121_range *current_range;
};

static const struct da9121_variant variant_parameters[] = {
	{ 1, 2, &da9121_10A_2phase_current },	/* DA9121_TYPE_DA9121_DA9130 */
	{ 2, 1, &da9121_3A_1phase_current  },	/* DA9121_TYPE_DA9220_DA9132 */
	{ 2, 1, &da9121_5A_1phase_current  },	/* DA9121_TYPE_DA9122_DA9131 */
	{ 1, 2, &da9121_6A_2phase_current  },	/* DA9121_TYPE_DA9217 */
};

static void da9121_status_poll_on(struct work_struct *work)
{
	struct da9121 *chip = container_of(work, struct da9121, work.work);
	enum { R0 = 0, R1, R2, REG_MAX_NUM };
	int status[REG_MAX_NUM] = {0};
	int clear[REG_MAX_NUM] = {0};
	unsigned long delay;
	int i;
	int ret;

	/*
	 * If persistent-notification, status will be true
	 * If not persistent-notification any longer, status will be false
	 */
	ret = regmap_bulk_read(chip->regmap, DA9121_REG_SYS_STATUS_0,
			       (void *)status, (size_t)REG_MAX_NUM);
	if (ret < 0) {
		dev_err(chip->dev,
			"Failed to read STATUS registers: %d\n", ret);
		goto error;
	}

	/* 0 TEMP_CRIT */
	if ((chip->persistent[R0] & DA9121_MASK_SYS_EVENT_0_E_TEMP_CRIT) &&
	    !(status[R0] & DA9121_MASK_SYS_STATUS_0_TEMP_CRIT)) {
		clear[R0] |= DA9121_MASK_SYS_MASK_0_M_TEMP_CRIT;
		chip->persistent[R0] &= ~DA9121_MASK_SYS_EVENT_0_E_TEMP_CRIT;
	}
	/* 0 TEMP_WARN */
	if ((chip->persistent[R0] & DA9121_MASK_SYS_EVENT_0_E_TEMP_WARN) &&
	    !(status[R0] & DA9121_MASK_SYS_STATUS_0_TEMP_WARN)) {
		clear[R0] |= DA9121_MASK_SYS_MASK_0_M_TEMP_WARN;
		chip->persistent[R0] &= ~DA9121_MASK_SYS_EVENT_0_E_TEMP_WARN;
	}

	/* 1 OV1 */
	if ((chip->persistent[R1] & DA9121_MASK_SYS_EVENT_1_E_OV1) &&
	    !(status[R1] & DA9121_MASK_SYS_STATUS_1_OV1)) {
		clear[R1] |= DA9121_MASK_SYS_MASK_1_M_OV1;
		chip->persistent[R1] &= ~DA9121_MASK_SYS_EVENT_1_E_OV1;
	}
	/* 1 UV1 */
	if ((chip->persistent[R1] & DA9121_MASK_SYS_EVENT_1_E_UV1) &&
	    !(status[R1] & DA9121_MASK_SYS_STATUS_1_UV1)) {
		clear[R1] |= DA9121_MASK_SYS_MASK_1_M_UV1;
		chip->persistent[R1] &= ~DA9121_MASK_SYS_EVENT_1_E_UV1;
	}
	/* 1 OC1 */
	if ((chip->persistent[R1] & DA9121_MASK_SYS_EVENT_1_E_OC1) &&
	    !(status[R1] & DA9121_MASK_SYS_STATUS_1_OC1)) {
		clear[R1] |= DA9121_MASK_SYS_MASK_1_M_OC1;
		chip->persistent[R1] &= ~DA9121_MASK_SYS_EVENT_1_E_OC1;
	}

	if (variant_parameters[chip->variant_id].num_bucks == 2) {
		/* 1 OV2 */
		if ((chip->persistent[R1] & DA9xxx_MASK_SYS_EVENT_1_E_OV2) &&
		    !(status[R1] & DA9xxx_MASK_SYS_STATUS_1_OV2)) {
			clear[R1] |= DA9xxx_MASK_SYS_MASK_1_M_OV2;
			chip->persistent[R1] &= ~DA9xxx_MASK_SYS_EVENT_1_E_OV2;
		}
		/* 1 UV2 */
		if ((chip->persistent[R1] & DA9xxx_MASK_SYS_EVENT_1_E_UV2) &&
		    !(status[R1] & DA9xxx_MASK_SYS_STATUS_1_UV2)) {
			clear[R1] |= DA9xxx_MASK_SYS_MASK_1_M_UV2;
			chip->persistent[R1] &= ~DA9xxx_MASK_SYS_EVENT_1_E_UV2;
		}
		/* 1 OC2 */
		if ((chip->persistent[R1] & DA9xxx_MASK_SYS_EVENT_1_E_OC2) &&
		    !(status[R1] & DA9xxx_MASK_SYS_STATUS_1_OC2)) {
			clear[R1] |= DA9xxx_MASK_SYS_MASK_1_M_OC2;
			chip->persistent[R1] &= ~DA9xxx_MASK_SYS_EVENT_1_E_OC2;
		}
	}

	for (i = R0; i < REG_MAX_NUM - 1; i++) {
		if (clear[i]) {
			unsigned int reg = DA9121_REG_SYS_MASK_0 + i;
			unsigned int mbit = clear[i];

			ret = regmap_update_bits(chip->regmap, reg, mbit, 0);
			if (ret < 0) {
				dev_err(chip->dev,
					"Failed to unmask 0x%02x %d\n",
					reg, ret);
				goto error;
			}
		}
	}

	if (chip->persistent[R0] | chip->persistent[R1]) {
		delay = msecs_to_jiffies(chip->passive_delay);
		queue_delayed_work(system_freezable_wq, &chip->work, delay);
	}

error:
	return;
}

static bool da9121_rdev_to_buck_reg_mask(struct regulator_dev *rdev, bool mode,
					 unsigned int *reg, unsigned int *msk)
{
	struct da9121 *chip = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);

	switch (id) {
	case DA9121_DA9130_ID_BUCK1:
	case DA9220_DA9132_ID_BUCK1:
	case DA9122_DA9131_ID_BUCK1:
	case DA9217_ID_BUCK1:
		if (mode) {
			*reg = DA9121_REG_BUCK_BUCK1_4;
			*msk = DA9121_MASK_BUCK_BUCKX_4_CHX_A_MODE;
		} else {
			*reg = DA9121_REG_BUCK_BUCK1_2;
			*msk = DA9121_MASK_BUCK_BUCKX_2_CHX_ILIM;
		}
		break;
	case DA9220_DA9132_ID_BUCK2:
	case DA9122_DA9131_ID_BUCK2:
		if (mode) {
			*reg = DA9xxx_REG_BUCK_BUCK2_4;
			*msk = DA9121_MASK_BUCK_BUCKX_4_CHX_A_MODE;
		} else {
			*reg = DA9xxx_REG_BUCK_BUCK2_2;
			*msk = DA9121_MASK_BUCK_BUCKX_2_CHX_ILIM;
		}
		break;
	default:
		dev_err(chip->dev, "Invalid regulator ID\n");
		return false;
	}
	return true;
}

static int da9121_get_current_limit(struct regulator_dev *rdev)
{
	struct da9121 *chip = rdev_get_drvdata(rdev);
	struct da9121_range *current_range = variant_parameters[chip->variant_id].current_range;
	unsigned int reg = 0;
	unsigned int msk = 0;
	unsigned int val = 0;
	int ret = 0;

	if (!da9121_rdev_to_buck_reg_mask(rdev, false, &reg, &msk))
		return -EINVAL;

	ret = regmap_read(chip->regmap, reg, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read BUCK register: %d\n", ret);
		goto error;
	}

	if (val < current_range->reg_min) {
		ret = -EACCES;
		goto error;
	}

	if (val > current_range->reg_max) {
		ret = -EINVAL;
		goto error;
	}

	return current_range->val_min + (current_range->val_stp * (val - current_range->reg_min));
error:
	return ret;
}

static int da9121_ceiling_selector(struct regulator_dev *rdev,
				   int min, int max, unsigned int *selector)
{
	struct da9121 *chip = rdev_get_drvdata(rdev);
	struct da9121_range *current_range = variant_parameters[chip->variant_id].current_range;
	unsigned int level;
	unsigned int i = 0;
	unsigned int sel = 0;
	int ret = 0;

	if (current_range->val_min > max || current_range->val_max < min) {
		dev_err(chip->dev,
			"Requested current out of regulator capability\n");
		ret = -EINVAL;
		goto error;
	}

	level = current_range->val_max;
	for (i = current_range->reg_max; i >= current_range->reg_min; i--) {
		if (level <= max) {
			sel = i;
			break;
		}
		level -= current_range->val_stp;
	}

	if (level < min) {
		dev_err(chip->dev,
			"Best match falls below minimum requested current\n");
		ret = -EINVAL;
		goto error;
	}

	*selector = sel;
error:
	return ret;
}

static int da9121_set_current_limit(struct regulator_dev *rdev,
				    int min_ua, int max_ua)
{
	struct da9121 *chip = rdev_get_drvdata(rdev);
	struct da9121_range *current_range = variant_parameters[chip->variant_id].current_range;
	unsigned int sel = 0;
	unsigned int reg = 0;
	unsigned int msk = 0;
	int ret = 0;

	if (min_ua < current_range->val_min ||
	    max_ua > current_range->val_max) {
		ret = -EINVAL;
		goto error;
	}

	ret = da9121_ceiling_selector(rdev, min_ua, max_ua, &sel);
	if (ret < 0)
		goto error;

	if (!da9121_rdev_to_buck_reg_mask(rdev, false, &reg, &msk))
		return -EINVAL;

	ret = regmap_update_bits(chip->regmap, reg, msk, (unsigned int)sel);
	if (ret < 0)
		dev_err(chip->dev, "Cannot update BUCK register %02x, err: %d\n", reg, ret);

error:
	return ret;
}

static unsigned int da9121_map_mode(unsigned int mode)
{
	switch (mode) {
	case DA9121_BUCK_MODE_FORCE_PWM:
		return REGULATOR_MODE_FAST;
	case DA9121_BUCK_MODE_FORCE_PWM_SHEDDING:
		return REGULATOR_MODE_NORMAL;
	case DA9121_BUCK_MODE_AUTO:
		return REGULATOR_MODE_IDLE;
	case DA9121_BUCK_MODE_FORCE_PFM:
		return REGULATOR_MODE_STANDBY;
	default:
		return -EINVAL;
	}
}

static int da9121_buck_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct da9121 *chip = rdev_get_drvdata(rdev);
	unsigned int val;
	unsigned int reg;
	unsigned int msk;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = DA9121_BUCK_MODE_FORCE_PWM;
		break;
	case REGULATOR_MODE_NORMAL:
		val = DA9121_BUCK_MODE_FORCE_PWM_SHEDDING;
		break;
	case REGULATOR_MODE_IDLE:
		val = DA9121_BUCK_MODE_AUTO;
		break;
	case REGULATOR_MODE_STANDBY:
		val = DA9121_BUCK_MODE_FORCE_PFM;
		break;
	default:
		return -EINVAL;
	}

	if (!da9121_rdev_to_buck_reg_mask(rdev, true, &reg, &msk))
		return -EINVAL;

	return regmap_update_bits(chip->regmap, reg, msk, val);
}

static unsigned int da9121_buck_get_mode(struct regulator_dev *rdev)
{
	struct da9121 *chip = rdev_get_drvdata(rdev);
	unsigned int reg;
	unsigned int msk;
	unsigned int val;
	int ret = 0;

	if (!da9121_rdev_to_buck_reg_mask(rdev, true, &reg, &msk))
		return -EINVAL;

	ret = regmap_read(chip->regmap, reg, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read BUCK register: %d\n", ret);
		return -EINVAL;
	}

	return da9121_map_mode(val & msk);
}

static const struct regulator_ops da9121_buck_ops = {
	.enable            = regulator_enable_regmap,
	.disable           = regulator_disable_regmap,
	.is_enabled        = regulator_is_enabled_regmap,
	.set_voltage_sel   = regulator_set_voltage_sel_regmap,
	.get_voltage_sel   = regulator_get_voltage_sel_regmap,
	.list_voltage      = regulator_list_voltage_linear,
	.get_current_limit = da9121_get_current_limit,
	.set_current_limit = da9121_set_current_limit,
	.set_mode          = da9121_buck_set_mode,
	.get_mode          = da9121_buck_get_mode,
};

static struct of_regulator_match da9121_matches[] = {
	[DA9121_INDEX_BUCK1] = { .name = "buck1" },
	[DA9121_INDEX_BUCK2] = { .name = "buck2" },
};

/* DA9121 regulator model */
static struct regulator_desc local_da9121_regulators[DA9121_TYPE_NUM][DA9121_MAX_REGULATORS] = {
	[DA9121_TYPE_DA9121_DA9130] = {
		[DA9121_INDEX_BUCK1] = {
			.id = DA9121_DA9130_ID_BUCK1,
			.name = "DA9121/DA9130 BUCK1",
			.of_match = of_match_ptr((const char *)
				     &da9121_matches[DA9121_INDEX_BUCK1].name),
			.of_map_mode = da9121_map_mode,
			.regulators_node = of_match_ptr("regulators"),
			.ops = &da9121_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.enable_reg = DA9121_REG_BUCK_BUCK1_0,
			.enable_mask = DA9121_MASK_BUCK_BUCKX_0_CHX_EN,
			.vsel_reg = DA9121_REG_BUCK_BUCK1_5,
			.vsel_mask = DA9121_MASK_BUCK_BUCKX_5_CHX_A_VOUT,
			.linear_min_sel = 30,
			.n_voltages = 191,
			.min_uV = 300000,
			.uV_step = 10000,
			.owner = THIS_MODULE,
		}
	},
	[DA9121_TYPE_DA9220_DA9132] = {
		[DA9121_INDEX_BUCK1] = {
			.id = DA9220_DA9132_ID_BUCK1,
			.name = "DA9220/DA9132 BUCK1",
			.of_match = of_match_ptr((const char *)
				     &da9121_matches[DA9121_INDEX_BUCK1].name),
			.of_map_mode = da9121_map_mode,
			.regulators_node = of_match_ptr("regulators"),
			.ops = &da9121_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.enable_reg = DA9121_REG_BUCK_BUCK1_0,
			.enable_mask = DA9121_MASK_BUCK_BUCKX_0_CHX_EN,
			.vsel_reg = DA9121_REG_BUCK_BUCK1_5,
			.vsel_mask = DA9121_MASK_BUCK_BUCKX_5_CHX_A_VOUT,
			.linear_min_sel = 30,
			.n_voltages = 191,
			.min_uV = 300000,
			.uV_step = 10000,
			.owner = THIS_MODULE,
		},
		[DA9121_INDEX_BUCK2] = {
			.id = DA9220_DA9132_ID_BUCK2,
			.name = "DA9220/DA9132 BUCK2",
			.of_match = of_match_ptr((const char *)
				     &da9121_matches[DA9121_INDEX_BUCK2].name),
			.of_map_mode = da9121_map_mode,
			.regulators_node = of_match_ptr("regulators"),
			.ops = &da9121_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.enable_reg = DA9xxx_REG_BUCK_BUCK2_0,
			.enable_mask = DA9121_MASK_BUCK_BUCKX_0_CHX_EN,
			.vsel_reg = DA9xxx_REG_BUCK_BUCK2_5,
			.vsel_mask = DA9121_MASK_BUCK_BUCKX_5_CHX_A_VOUT,
			.linear_min_sel = 30,
			.n_voltages = 191,
			.min_uV = 300000,
			.uV_step = 10000,
			.owner = THIS_MODULE,
		}
	},
	[DA9121_TYPE_DA9122_DA9131] = {
		[DA9121_INDEX_BUCK1] = {
			.id = DA9122_DA9131_ID_BUCK1,
			.name = "DA9122/DA9131 BUCK1",
			.of_match = of_match_ptr((const char *)
				     &da9121_matches[DA9121_INDEX_BUCK1].name),
			.of_map_mode = da9121_map_mode,
			.regulators_node = of_match_ptr("regulators"),
			.ops = &da9121_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.enable_reg = DA9121_REG_BUCK_BUCK1_0,
			.enable_mask = DA9121_MASK_BUCK_BUCKX_0_CHX_EN,
			.vsel_reg = DA9121_REG_BUCK_BUCK1_5,
			.vsel_mask = DA9121_MASK_BUCK_BUCKX_5_CHX_A_VOUT,
			.linear_min_sel = 30,
			.n_voltages = 191,
			.min_uV = 300000,
			.uV_step = 10000,
			.owner = THIS_MODULE,
		},
		[DA9121_INDEX_BUCK2] = {
			.id = DA9122_DA9131_ID_BUCK2,
			.name = "DA9122/DA9131 BUCK2",
			.of_match = of_match_ptr((const char *)
				     &da9121_matches[DA9121_INDEX_BUCK2].name),
			.of_map_mode = da9121_map_mode,
			.regulators_node = of_match_ptr("regulators"),
			.ops = &da9121_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.enable_reg = DA9xxx_REG_BUCK_BUCK2_0,
			.enable_mask = DA9121_MASK_BUCK_BUCKX_0_CHX_EN,
			.vsel_reg = DA9xxx_REG_BUCK_BUCK2_5,
			.vsel_mask = DA9121_MASK_BUCK_BUCKX_5_CHX_A_VOUT,
			.linear_min_sel = 30,
			.n_voltages = 191,
			.min_uV = 300000,
			.uV_step = 10000,
			.owner = THIS_MODULE,
		}
	},
	[DA9121_TYPE_DA9217] = {
		[DA9121_INDEX_BUCK1] = {
			.id = DA9217_ID_BUCK1,
			.name = "DA9217 BUCK1",
			.of_match = of_match_ptr((const char *)
				     &da9121_matches[DA9121_INDEX_BUCK1].name),
			.of_map_mode = da9121_map_mode,
			.regulators_node = of_match_ptr("regulators"),
			.ops = &da9121_buck_ops,
			.type = REGULATOR_VOLTAGE,
			.enable_reg = DA9121_REG_BUCK_BUCK1_0,
			.enable_mask = DA9121_MASK_BUCK_BUCKX_0_CHX_EN,
			.vsel_reg = DA9121_REG_BUCK_BUCK1_5,
			.vsel_mask = DA9121_MASK_BUCK_BUCKX_5_CHX_A_VOUT,
			.linear_min_sel = 30,
			.n_voltages = 191,
			.min_uV = 300000,
			.uV_step = 10000,
			.owner = THIS_MODULE,
		}
	}
};

static int da9121_parse_regulators_dt(struct da9121 *chip)
{
	struct da9121_dt_data *data = NULL;
	struct device_node *node;
	int num_matches = 0;
	int ret = 0;
	int i, n;

	node = of_get_child_by_name(chip->dev->of_node, "regulators");
	if (!node) {
		dev_err(chip->dev, "Regulators node not found\n");
		ret = -ENODEV;
		goto error;
	}

	num_matches = of_regulator_match(chip->dev, node, da9121_matches,
					 ARRAY_SIZE(da9121_matches));
	of_node_put(node);
	if (num_matches < 0) {
		dev_err(chip->dev, "Failed while matching regulators\n");
		ret = -EINVAL;
		goto error;
	}

	/* Interrupt assumptions require at least one buck to be configured */
	if (num_matches == 0) {
		dev_err(chip->dev, "Did not match any regulators in the DT\n");
		goto error;
	}

	data = devm_kzalloc(chip->dev,
			    sizeof(struct da9121_dt_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto error;
	}

	data->num_matches = (unsigned int)num_matches;

	n = 0;
	for (i = 0; i < ARRAY_SIZE(da9121_matches); i++) {
		if (!da9121_matches[i].init_data)
			continue;

		data->init_data[n] = da9121_matches[i].init_data;
		data->reg_node[n] = da9121_matches[i].of_node;
		data->gpiod_ren[n] = devm_gpiod_get_from_of_node(chip->dev,
								 da9121_matches[i].of_node,
								 "enable-gpio",
								 0,
								 GPIOD_OUT_HIGH,
								 "da9121-enable");
		if (IS_ERR(data->gpiod_ren[n]))
			data->gpiod_ren[n] = NULL;

		if (variant_parameters[chip->variant_id].num_bucks == 2) {
			u32 ripple_cancel;
			u32 reg = (i ? DA9xxx_REG_BUCK_BUCK2_7
					  : DA9121_REG_BUCK_BUCK1_7);
			if (!of_property_read_u32(da9121_matches[i].of_node,
						  "dlg,ripple-cancel",
						  &ripple_cancel)) {
				/* Write to BUCK_BUCKx_7 : CHx_RIPPLE_CANCEL */
				ret = regmap_update_bits(chip->regmap, reg,
							 DA9xxx_MASK_BUCK_BUCKX_7_CHX_RIPPLE_CANCEL,
							 ripple_cancel);
				if (ret < 0)
					dev_err(chip->dev,
						"Cannot update BUCK register %02x, err: %d\n",
						reg, ret);
			}
		}
		n++;
	}

	chip->dt_data = data;
error:
	return ret;
}

static inline int da9121_handle_notifier(struct da9121 *chip,
					 struct regulator_dev *rdev,
					 unsigned int event_bank,
					 unsigned int event,
					 unsigned int ebit)
{
	enum { R0 = 0, R1, R2, REG_MAX_NUM };
	unsigned long notification = 0;
	int ret = 0;

	if (event & ebit) {
		switch (event_bank) {
		case DA9121_REG_SYS_EVENT_0:
			switch (event & ebit) {
			case DA9121_MASK_SYS_EVENT_0_E_TEMP_CRIT:
				chip->persistent[R0] |= DA9121_MASK_SYS_EVENT_0_E_TEMP_CRIT;
				notification |= REGULATOR_EVENT_OVER_TEMP |
						REGULATOR_EVENT_DISABLE;
				break;
			case DA9121_MASK_SYS_EVENT_0_E_TEMP_WARN:
				chip->persistent[R0] |= DA9121_MASK_SYS_EVENT_0_E_TEMP_WARN;
				notification |= REGULATOR_EVENT_OVER_TEMP;
				break;
			default:
				dev_warn(chip->dev,
					 "Unhandled event in bank0 0x%02x\n",
					 event & ebit);
				ret = -EINVAL;
				break;
			}
			break;
		case DA9121_REG_SYS_EVENT_1:
			switch (event & ebit) {
			case DA9121_MASK_SYS_EVENT_1_E_OV1:
				chip->persistent[R1] |= DA9121_MASK_SYS_EVENT_1_E_OV1;
				notification |= REGULATOR_EVENT_REGULATION_OUT;
				break;
			case DA9121_MASK_SYS_EVENT_1_E_UV1:
				chip->persistent[R1] |= DA9121_MASK_SYS_EVENT_1_E_UV1;
				notification |= REGULATOR_EVENT_UNDER_VOLTAGE;
				break;
			case DA9121_MASK_SYS_EVENT_1_E_OC1:
				chip->persistent[R1] |= DA9121_MASK_SYS_EVENT_1_E_OC1;
				notification |= REGULATOR_EVENT_OVER_CURRENT;
				break;
			case DA9xxx_MASK_SYS_EVENT_1_E_OV2:
				chip->persistent[R1] |= DA9xxx_MASK_SYS_EVENT_1_E_OV2;
				notification |= REGULATOR_EVENT_REGULATION_OUT;
				break;
			case DA9xxx_MASK_SYS_EVENT_1_E_UV2:
				chip->persistent[R1] |= DA9xxx_MASK_SYS_EVENT_1_E_UV2;
				notification |= REGULATOR_EVENT_UNDER_VOLTAGE;
				break;
			case DA9xxx_MASK_SYS_EVENT_1_E_OC2:
				chip->persistent[R1] |= DA9xxx_MASK_SYS_EVENT_1_E_OC2;
				notification |= REGULATOR_EVENT_OVER_CURRENT;
				break;
			default:
				dev_warn(chip->dev,
					 "Unhandled event in bank1 0x%02x\n",
					 event & ebit);
				ret = -EINVAL;
				break;
			}
			break;
		default:
			dev_warn(chip->dev,
				 "Unhandled event bank 0x%02x\n", event_bank);
			ret = -EINVAL;
			goto error;
		}

		regulator_lock(rdev);
		regulator_notifier_call_chain(rdev, notification, NULL);
		regulator_unlock(rdev);
	}

error:
	return ret;
}

static irqreturn_t da9121_irq_handler(int irq, void *data)
{
	struct da9121 *chip = data;
	struct regulator_dev *rdev;
	enum { R0 = 0, R1, R2, REG_MAX_NUM };
	int event[REG_MAX_NUM] = {0};
	int handled[REG_MAX_NUM] = {0};
	int mask[REG_MAX_NUM] = {0};
	int ret = IRQ_NONE;
	int i;
	int err;

	err = regmap_bulk_read(chip->regmap, DA9121_REG_SYS_EVENT_0,
			       (void *)event, (size_t)REG_MAX_NUM);
	if (err < 0) {
		dev_err(chip->dev, "Failed to read EVENT registers %d\n", err);
		ret = IRQ_NONE;
		goto error;
	}

	err = regmap_bulk_read(chip->regmap, DA9121_REG_SYS_MASK_0,
			       (void *)mask, (size_t)REG_MAX_NUM);
	if (err < 0) {
		dev_err(chip->dev,
			"Failed to read MASK registers: %d\n", ret);
		ret = IRQ_NONE;
		goto error;
	}

	rdev = chip->rdev[DA9121_INDEX_BUCK1];

	/* 0 SYSTEM_GOOD */
	if (!(mask[R0] & DA9xxx_MASK_SYS_MASK_0_M_SG) &&
	    (event[R0] & DA9xxx_MASK_SYS_EVENT_0_E_SG)) {
		dev_warn(chip->dev, "Handled E_SG\n");
		handled[R0] |= DA9xxx_MASK_SYS_EVENT_0_E_SG;
		ret = IRQ_HANDLED;
	}

	/* 0 TEMP_CRIT */
	if (!(mask[R0] & DA9121_MASK_SYS_MASK_0_M_TEMP_CRIT) &&
	    (event[R0] & DA9121_MASK_SYS_EVENT_0_E_TEMP_CRIT)) {
		err = da9121_handle_notifier(chip, rdev,
					     DA9121_REG_SYS_EVENT_0, event[R0],
					     DA9121_MASK_SYS_EVENT_0_E_TEMP_CRIT);
		if (!err) {
			handled[R0] |= DA9121_MASK_SYS_EVENT_0_E_TEMP_CRIT;
			ret = IRQ_HANDLED;
		}
	}

	/* 0 TEMP_WARN */
	if (!(mask[R0] & DA9121_MASK_SYS_MASK_0_M_TEMP_WARN) &&
	    (event[R0] & DA9121_MASK_SYS_EVENT_0_E_TEMP_WARN)) {
		err = da9121_handle_notifier(chip, rdev,
					     DA9121_REG_SYS_EVENT_0, event[R0],
					     DA9121_MASK_SYS_EVENT_0_E_TEMP_WARN);
		if (!err) {
			handled[R0] |= DA9121_MASK_SYS_EVENT_0_E_TEMP_WARN;
			ret = IRQ_HANDLED;
		}
	}

	if (event[R0] != handled[R0]) {
		dev_warn(chip->dev,
			 "Unhandled event in bank0 0x%02x\n",
			 event[R0] ^ handled[R0]);
	}

	/* 1 PG1 */
	if (!(mask[R1] & DA9121_MASK_SYS_MASK_1_M_PG1) &&
	    (event[R1] & DA9121_MASK_SYS_EVENT_1_E_PG1)) {
		dev_warn(chip->dev, "Handled E_PG1\n");
		handled[R1] |= DA9121_MASK_SYS_EVENT_1_E_PG1;
		ret = IRQ_HANDLED;
	}

	/* 1 OV1 */
	if (!(mask[R1] & DA9121_MASK_SYS_MASK_1_M_OV1) &&
	    (event[R1] & DA9121_MASK_SYS_EVENT_1_E_OV1)) {
		err = da9121_handle_notifier(chip, rdev,
					     DA9121_REG_SYS_EVENT_1, event[R1],
					     DA9121_MASK_SYS_EVENT_1_E_OV1);
		if (!err) {
			handled[R1] |= DA9121_MASK_SYS_EVENT_1_E_OV1;
			ret = IRQ_HANDLED;
		}
	}

	/* 1 UV1 */
	if (!(mask[R1] & DA9121_MASK_SYS_MASK_1_M_UV1) &&
	    (event[R1] & DA9121_MASK_SYS_EVENT_1_E_UV1)) {
		err = da9121_handle_notifier(chip, rdev,
					     DA9121_REG_SYS_EVENT_1, event[R1],
					     DA9121_MASK_SYS_EVENT_1_E_UV1);
		if (!err) {
			handled[R1] |= DA9121_MASK_SYS_EVENT_1_E_UV1;
			ret = IRQ_HANDLED;
		}
	}

	/* 1 OC1 */
	if (!(mask[R1] & DA9121_MASK_SYS_MASK_1_M_OC1) &&
	    (event[R1] & DA9121_MASK_SYS_EVENT_1_E_OC1)) {
		err = da9121_handle_notifier(chip, rdev,
					     DA9121_REG_SYS_EVENT_1, event[R1],
					     DA9121_MASK_SYS_EVENT_1_E_OC1);
		if (!err) {
			handled[R1] |= DA9121_MASK_SYS_EVENT_1_E_OC1;
			ret = IRQ_HANDLED;
		}
	}

	if (variant_parameters[chip->variant_id].num_bucks == 2) {
		struct regulator_dev *rdev2;

		rdev2 = chip->rdev[DA9121_INDEX_BUCK2];

		/* 1 PG2 */
		if (!(mask[R1] & DA9xxx_MASK_SYS_MASK_1_M_PG2) &&
		    (event[R1] & DA9xxx_MASK_SYS_EVENT_1_E_PG2)) {
			dev_warn(chip->dev, "Handled E_PG2\n");
			handled[R1] |= DA9xxx_MASK_SYS_EVENT_1_E_PG2;
			ret = IRQ_HANDLED;
		}

		/* 1 OV2 */
		if (!(mask[R1] & DA9xxx_MASK_SYS_MASK_1_M_OV2) &&
		    (event[R1] & DA9xxx_MASK_SYS_EVENT_1_E_OV2)) {
			err = da9121_handle_notifier(chip, rdev2,
						     DA9121_REG_SYS_EVENT_1, event[R1],
						     DA9xxx_MASK_SYS_EVENT_1_E_OV2);
			if (!err) {
				handled[R1] |= DA9xxx_MASK_SYS_EVENT_1_E_OV2;
				ret = IRQ_HANDLED;
			}
		}

		/* 1 UV2 */
		if (!(mask[R1] & DA9xxx_MASK_SYS_MASK_1_M_UV2) &&
		    (event[R1] & DA9xxx_MASK_SYS_EVENT_1_E_UV2)) {
			err = da9121_handle_notifier(chip, rdev2,
						     DA9121_REG_SYS_EVENT_1, event[R1],
						     DA9xxx_MASK_SYS_EVENT_1_E_UV2);
			if (!err) {
				handled[R1] |= DA9xxx_MASK_SYS_EVENT_1_E_UV2;
				ret = IRQ_HANDLED;
			}
		}

		/* 1 OC2 */
		if (!(mask[R1] & DA9xxx_MASK_SYS_MASK_1_M_OC2) &&
		    (event[R1] & DA9xxx_MASK_SYS_EVENT_1_E_OC2)) {
			err = da9121_handle_notifier(chip, rdev2,
						     DA9121_REG_SYS_EVENT_1, event[R1],
						     DA9xxx_MASK_SYS_EVENT_1_E_OC2);
			if (!err) {
				handled[R1] |= DA9xxx_MASK_SYS_EVENT_1_E_OC2;
				ret = IRQ_HANDLED;
			}
		}
	}

	if (event[R1] != handled[R1]) {
		dev_warn(chip->dev,
			 "Unhandled event in bank1 0x%02x\n",
			 event[R1] ^ handled[R1]);
	}

	/* DA9121_REG_SYS_EVENT_2 */
	if (!(mask[R2] & DA9121_MASK_SYS_MASK_2_M_GPIO2) &&
	    (event[R2] & DA9121_MASK_SYS_EVENT_2_E_GPIO2)) {
		dev_warn(chip->dev, "Handled E_GPIO2\n");
		handled[R2] |= DA9121_MASK_SYS_EVENT_2_E_GPIO2;
		ret = IRQ_HANDLED;
	}

	if (!(mask[R2] & DA9121_MASK_SYS_MASK_2_M_GPIO1) &&
	    (event[R2] & DA9121_MASK_SYS_EVENT_2_E_GPIO1)) {
		dev_warn(chip->dev, "Handled E_GPIO1\n");
		handled[R2] |= DA9121_MASK_SYS_EVENT_2_E_GPIO1;
		ret = IRQ_HANDLED;
	}

	if (!(mask[R2] & DA9121_MASK_SYS_MASK_2_M_GPIO0) &&
	    (event[R2] & DA9121_MASK_SYS_EVENT_2_E_GPIO0)) {
		dev_warn(chip->dev, "Handled E_GPIO0\n");
		handled[R2] |= DA9121_MASK_SYS_EVENT_2_E_GPIO0;
		ret = IRQ_HANDLED;
	}

	if (event[R2] != handled[R2]) {
		dev_warn(chip->dev,
			 "Unhandled event in bank2 0x%02x\n",
			 event[R2] ^ handled[R2]);
	}

	/* Mask the interrupts for persistent events OV, OC, UV, WARN, CRIT */
	for (i = R0; i < REG_MAX_NUM - 1; i++) {
		if (handled[i]) {
			unsigned int reg = DA9121_REG_SYS_MASK_0 + i;
			unsigned int mbit = handled[i];

			err = regmap_update_bits(chip->regmap, reg, mbit, mbit);
			if (err < 0) {
				dev_err(chip->dev,
					"Failed to mask 0x%02x interrupt %d\n",
					reg, err);
				ret = IRQ_NONE;
				goto error;
			}
		}
	}

	/* clear the events */
	if (handled[R0] | handled[R1] | handled[R2]) {
		err = regmap_bulk_write(chip->regmap, DA9121_REG_SYS_EVENT_0,
					(const void *)handled, (size_t)REG_MAX_NUM);
		if (err < 0) {
			dev_err(chip->dev, "Fail to write EVENTs %d\n", err);
			ret = IRQ_NONE;
			goto error;
		}
	}

	queue_delayed_work(system_freezable_wq, &chip->work, 0);
error:
	return ret;
}

static int da9121_set_regulator_config(struct da9121 *chip)
{
	struct regulator_config config = { };
	unsigned int max_matches = chip->dt_data->num_matches;
	int ret = 0;
	int i;

	if (max_matches > variant_parameters[chip->variant_id].num_bucks) {
		dev_err(chip->dev, "Too many regulators in the DT\n");
		ret = -EINVAL;
		goto error;
	}

	for (i = 0; i < max_matches; i++) {
		struct regulator_desc *regl_desc = &local_da9121_regulators[chip->variant_id][i];
		int id = regl_desc->id;

		config.init_data = chip->dt_data->init_data[i];
		config.dev = chip->dev;
		config.driver_data = chip;
		config.regmap = chip->regmap;
		config.of_node = chip->dt_data->reg_node[i];

		switch (id) {
		case DA9121_DA9130_ID_BUCK1:
		case DA9220_DA9132_ID_BUCK1:
		case DA9122_DA9131_ID_BUCK1:
		case DA9217_ID_BUCK1:
		case DA9220_DA9132_ID_BUCK2:
		case DA9122_DA9131_ID_BUCK2:
			config.ena_gpiod = chip->dt_data->gpiod_ren[i];
			break;
		default:
			dev_err(chip->dev, "Invalid regulator ID\n");
			ret = -EINVAL;
			goto error;
		}

		chip->rdev[i] = devm_regulator_register(chip->dev,
							regl_desc, &config);
		if (IS_ERR(chip->rdev[i])) {
			dev_err(chip->dev, "Failed to register regulator %s, %d/%d of_map_mode:%p\n",
				regl_desc->name, (i + 1), max_matches, regl_desc->of_map_mode);
			ret = PTR_ERR(chip->rdev[i]);
			goto error;
		}
	}

error:
	return ret;
}

/* DA9121 chip register model */
static const struct regmap_range da9121_1ch_2ph_readable_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_STATUS_0, DA9121_REG_SYS_MASK_3),
	regmap_reg_range(DA9121_REG_SYS_CONFIG_2, DA9121_REG_SYS_CONFIG_3),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_6),
	regmap_reg_range(DA9121_REG_OTP_DEVICE_ID, DA9121_REG_OTP_CONFIG_ID),
};

static const struct regmap_access_table da9121_1ch_2ph_readable_table = {
	.yes_ranges = da9121_1ch_2ph_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_1ch_2ph_readable_ranges),
};

static const struct regmap_range da9121_2ch_1ph_readable_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_STATUS_0, DA9121_REG_SYS_MASK_3),
	regmap_reg_range(DA9121_REG_SYS_CONFIG_2, DA9121_REG_SYS_CONFIG_3),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_7),
	regmap_reg_range(DA9xxx_REG_BUCK_BUCK2_0, DA9xxx_REG_BUCK_BUCK2_7),
	regmap_reg_range(DA9121_REG_OTP_DEVICE_ID, DA9121_REG_OTP_CONFIG_ID),
};

static const struct regmap_access_table da9121_2ch_1ph_readable_table = {
	.yes_ranges = da9121_2ch_1ph_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_2ch_1ph_readable_ranges),
};

static const struct regmap_range da9121_1ch_2ph_writeable_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_EVENT_0, DA9121_REG_SYS_MASK_3),
	regmap_reg_range(DA9121_REG_SYS_CONFIG_2, DA9121_REG_SYS_CONFIG_3),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_2),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_4, DA9121_REG_BUCK_BUCK1_6),
};

static const struct regmap_access_table da9121_1ch_2ph_writeable_table = {
	.yes_ranges = da9121_1ch_2ph_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_1ch_2ph_writeable_ranges),
};

static const struct regmap_range da9121_2ch_1ph_writeable_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_EVENT_0, DA9121_REG_SYS_MASK_3),
	regmap_reg_range(DA9121_REG_SYS_CONFIG_2, DA9121_REG_SYS_CONFIG_3),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_2),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_4, DA9121_REG_BUCK_BUCK1_7),
	regmap_reg_range(DA9xxx_REG_BUCK_BUCK2_0, DA9xxx_REG_BUCK_BUCK2_2),
	regmap_reg_range(DA9xxx_REG_BUCK_BUCK2_4, DA9xxx_REG_BUCK_BUCK2_7),
};

static const struct regmap_access_table da9121_2ch_1ph_writeable_table = {
	.yes_ranges = da9121_2ch_1ph_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_2ch_1ph_writeable_ranges),
};

static const struct regmap_range da9121_volatile_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_STATUS_0, DA9121_REG_SYS_EVENT_2),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_6),
};

static const struct regmap_access_table da9121_volatile_table = {
	.yes_ranges = da9121_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_volatile_ranges),
};

/* DA9121 regmap config for 1 channel 2 phase variants */
static struct regmap_config da9121_1ch_2ph_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = DA9121_REG_OTP_CONFIG_ID,
	.rd_table = &da9121_1ch_2ph_readable_table,
	.wr_table = &da9121_1ch_2ph_writeable_table,
	.volatile_table = &da9121_volatile_table,
	.cache_type = REGCACHE_RBTREE,
};

/* DA9121 regmap config for 2 channel 1 phase variants */
static struct regmap_config da9121_2ch_1ph_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = DA9121_REG_OTP_CONFIG_ID,
	.rd_table = &da9121_2ch_1ph_readable_table,
	.wr_table = &da9121_2ch_1ph_writeable_table,
	.volatile_table = &da9121_volatile_table,
	.cache_type = REGCACHE_RBTREE,
};

static int da9121_i2c_reg_read(struct i2c_client *client, u8 addr,
			       u8 *buf, int count)
{
	struct i2c_msg xfer[2];
	int ret;

	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = &addr;

	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 1;
	xfer[1].buf = buf;

	ret = i2c_transfer(client->adapter, xfer, 2);
	if (ret < 0) {
		dev_err(&client->dev, "Device read failed: %d\n", ret);
		return ret;
	}

	if (ret != 2) {
		dev_err(&client->dev, "Device read failed to complete\n");
		return -EIO;
	}

	return 0;
}

static int da9121_get_device_type(struct i2c_client *i2c, struct da9121 *chip)
{
	u8 device_id;
	u8 variant_id;
	u8 variant_mrc, variant_vrc;
	char *type;
	const char *name;
	bool device_config_match = false;
	int ret = 0;

	ret = da9121_i2c_reg_read(i2c, DA9121_REG_OTP_DEVICE_ID,
				  &device_id, 1);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read device ID: %d\n", ret);
		goto error;
	}

	ret = da9121_i2c_reg_read(i2c, DA9121_REG_OTP_VARIANT_ID,
				  &variant_id, 1);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read chip variant ID: %d\n", ret);
		goto error;
	}

	if (device_id != DA9121_DEVICE_ID) {
		dev_err(chip->dev, "Invalid device ID: 0x%02x\n", device_id);
		ret = -ENODEV;
		goto error;
	}

	name = of_get_property(chip->dev->of_node, "compatible", NULL);
	if (!name) {
		dev_err(chip->dev, "Cannot get device not compatible string.\n");
		goto error;
	}

	variant_vrc = variant_id & DA9121_MASK_OTP_VARIANT_ID_VRC;

	switch (variant_vrc) {
	case DA9130_VARIANT_VRC:
		type = "DA9121/DA9130";
		device_config_match = (chip->variant_id == DA9121_TYPE_DA9121_DA9130);
		break;
	case DA9131_VARIANT_VRC:
		type = "DA9122/DA9131";
		device_config_match = (chip->variant_id == DA9121_TYPE_DA9122_DA9131);
		break;
	case DA9217_VARIANT_VRC:
		type = "DA9217";
		device_config_match = (chip->variant_id == DA9121_TYPE_DA9217);
		break;
	default:
		type = "Unknown";
		break;
	}

	dev_info(chip->dev,
		 "Device detected (device-ID: 0x%02X, var-ID: 0x%02X, %s)\n",
		 device_id, variant_id, type);

	if (!device_config_match) {
		dev_err(chip->dev, "Device tree configuration '%s'does not match detected device.\n"
			, name);
		goto error;
	}

	variant_mrc = (variant_id & DA9121_MASK_OTP_VARIANT_ID_MRC)
			>> DA9121_SHIFT_OTP_VARIANT_ID_MRC;

	if (variant_mrc < DA9121_VARIANT_MRC_BASE) {
		dev_err(chip->dev,
			"Cannot support variant MRC: 0x%02X\n", variant_mrc);
		ret = -ENODEV;
	}
error:
	return ret;
}

static int da9121_assign_chip_model(struct i2c_client *i2c,
				    struct da9121 *chip)
{
	struct regmap_config *regmap = NULL;
	int ret = 0;

	chip->dev = &i2c->dev;

	ret = da9121_get_device_type(i2c, chip);
	if (ret)
		return ret;

	switch (chip->variant_id) {
	case DA9121_TYPE_DA9121_DA9130:
	case DA9121_TYPE_DA9217:
		regmap = &da9121_1ch_2ph_regmap_config;
		break;
	case DA9121_TYPE_DA9122_DA9131:
	case DA9121_TYPE_DA9220_DA9132:
		regmap = &da9121_2ch_1ph_regmap_config;
	}

	/* Set these up for of_regulator_match call which may want .of_map_modes */
	da9121_matches[0].desc = &local_da9121_regulators[chip->variant_id][0];
	da9121_matches[1].desc = &local_da9121_regulators[chip->variant_id][1];

	chip->regmap = devm_regmap_init_i2c(i2c, regmap);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(chip->dev, "Failed to configure a register map: %d\n",
			ret);
	}

	return ret;
}

static int da9121_set_irq_masks(struct da9121 *chip, bool mask_irqs)
{
	unsigned int mask0, update0;
	unsigned int mask1, update1;
	unsigned int mask3;
	int ret = 0;

	if (chip->chip_irq != 0) {
		mask0 = DA9121_MASK_SYS_MASK_0_M_TEMP_CRIT |
			DA9121_MASK_SYS_MASK_0_M_TEMP_WARN;

		mask1 = DA9121_MASK_SYS_MASK_1_M_OV1 |
			DA9121_MASK_SYS_MASK_1_M_UV1 |
			DA9121_MASK_SYS_MASK_1_M_OC1;

		if (mask_irqs) {
			update0 = mask0;
			update1 = mask1;
		} else {
			update0 = 0;
			update1 = 0;
		}

		ret = regmap_update_bits(chip->regmap,
					 DA9121_REG_SYS_MASK_0, mask0, update0);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to write MASK 0 reg %d\n",
				ret);
			goto error;
		}

		ret = regmap_update_bits(chip->regmap,
					 DA9121_REG_SYS_MASK_1, mask1, update1);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to write MASK 1 reg %d\n",
				ret);
			goto error;
		}

		/* permanently disable IRQs for VR_HOT and PG1_STAT */
		mask3 = DA9121_MASK_SYS_MASK_3_M_VR_HOT |
			DA9121_MASK_SYS_MASK_3_M_PG1_STAT;

		ret = regmap_update_bits(chip->regmap,
					 DA9121_REG_SYS_MASK_3, mask3, mask3);
		if (ret < 0) {
			dev_err(chip->dev, "Failed to write MASK 3 reg %d\n",
				ret);
			goto error;
		}
	}

error:
	return ret;
}

static int da9121_config_irq(struct i2c_client *i2c, struct da9121 *chip)
{
	unsigned int p_delay = DA9121_DEFAULT_POLLING_PERIOD_MS;
	int ret = 0;

	chip->chip_irq = i2c->irq;

	if (chip->chip_irq != 0) {
		if (!of_property_read_u32(chip->dev->of_node,
					  "dlg,irq-polling-delay-passive",
					  &p_delay)) {
			if (p_delay < DA9121_MIN_POLLING_PERIOD_MS ||
			    p_delay > DA9121_MAX_POLLING_PERIOD_MS) {
				dev_warn(chip->dev,
					 "Out-of-range polling period %d ms\n",
					 p_delay);
				p_delay = DA9121_DEFAULT_POLLING_PERIOD_MS;
			}
		}

		chip->passive_delay = p_delay;

		ret = devm_request_threaded_irq(chip->dev, chip->chip_irq, NULL,
						da9121_irq_handler,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						"da9121", chip);
		if (ret != 0) {
			dev_err(chip->dev, "Failed IRQ request: %d\n",
				chip->chip_irq);
			goto error;
		}

		ret = da9121_set_irq_masks(chip, false);
		if (ret != 0) {
			dev_err(chip->dev, "Failed to set IRQ masks: %d\n",
				ret);
			goto error;
		}

		INIT_DELAYED_WORK(&chip->work, da9121_status_poll_on);
		dev_info(chip->dev, "Interrupt polling period set at %d ms\n",
			 chip->passive_delay);
	}
error:
	return ret;
}

static const struct of_device_id da9121_dt_ids[] = {
	{ .compatible = "dlg,da9121", .data = (void *)DA9121_TYPE_DA9121_DA9130 },
	{ .compatible = "dlg,da9130", .data = (void *)DA9121_TYPE_DA9121_DA9130 },
	{ .compatible = "dlg,da9217", .data = (void *)DA9121_TYPE_DA9217 },
	{ .compatible = "dlg,da9122", .data = (void *)DA9121_TYPE_DA9122_DA9131 },
	{ .compatible = "dlg,da9131", .data = (void *)DA9121_TYPE_DA9122_DA9131 },
	{ .compatible = "dlg,da9220", .data = (void *)DA9121_TYPE_DA9220_DA9132 },
	{ .compatible = "dlg,da9132", .data = (void *)DA9121_TYPE_DA9220_DA9132 },
	{}
};

MODULE_DEVICE_TABLE(of, da9121_dt_ids);

static inline int da9121_of_get_id(struct device *dev)
{
	const struct of_device_id *id = of_match_device(da9121_dt_ids, dev);

	if (id)
		return (uintptr_t)id->data;

	dev_err(dev, "%s: Failed\n", __func__);
	return -EINVAL;
}

static int da9121_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct da9121 *chip;
	int ret = 0;

	chip = devm_kzalloc(&i2c->dev, sizeof(struct da9121), GFP_KERNEL);
	if (!chip) {
		ret = -ENOMEM;
		goto error;
	}

	chip->variant_id = da9121_of_get_id(&i2c->dev);

	ret = da9121_assign_chip_model(i2c, chip);
	if (ret < 0)
		goto error;

	ret = da9121_set_irq_masks(chip, true);
	if (ret != 0) {
		dev_err(chip->dev, "Failed to set IRQ masks: %d\n", ret);
		goto error;
	}

	ret = da9121_parse_regulators_dt(chip);
	if (ret < 0)
		goto error;

	ret = da9121_set_regulator_config(chip);
	if (ret < 0)
		goto error;

	ret = da9121_config_irq(i2c, chip);
	if (ret < 0)
		goto error;

error:
	return ret;
}

static int da9121_i2c_remove(struct i2c_client *i2c)
{
	struct da9121 *chip = i2c_get_clientdata(i2c);
	int ret = 0;

	ret = da9121_set_irq_masks(chip, true);
	if (ret != 0) {
		dev_err(chip->dev, "Failed to set IRQ masks: %d\n", ret);
		goto error;
	}

	cancel_delayed_work(&chip->work);
error:
	return ret;
}

static struct i2c_driver da9121_regulator_driver = {
	.driver = {
		.name = "da9121",
		.of_match_table = of_match_ptr(da9121_dt_ids),
	},
	.probe = da9121_i2c_probe,
	.remove = da9121_i2c_remove,
};

module_i2c_driver(da9121_regulator_driver);

MODULE_AUTHOR("Steve Twiss <stwiss.opensource@diasemi.com>");
MODULE_AUTHOR("Adam Ward <award.opensource@diasemi.com>");
MODULE_DESCRIPTION("DA9121 Buck regulator driver");
MODULE_LICENSE("GPL");
