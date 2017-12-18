/*
 * tps65218-regulator.c
 *
 * Regulator driver for TPS65218 PMIC
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether expressed or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/tps65218.h>

enum tps65218_regulators { DCDC1, DCDC2, DCDC3, DCDC4,
			   DCDC5, DCDC6, LDO1, LS3 };

#define TPS65218_REGULATOR(_name, _id, _type, _ops, _n, _vr, _vm, _er, _em, \
			   _cr, _cm, _lr, _nlr, _delay, _fuv, _sr, _sm)	\
	{							\
		.name			= _name,		\
		.id			= _id,			\
		.ops			= &_ops,		\
		.n_voltages		= _n,			\
		.type			= _type,	\
		.owner			= THIS_MODULE,		\
		.vsel_reg		= _vr,			\
		.vsel_mask		= _vm,			\
		.csel_reg		= _cr,			\
		.csel_mask		= _cm,			\
		.enable_reg		= _er,			\
		.enable_mask		= _em,			\
		.volt_table		= NULL,			\
		.linear_ranges		= _lr,			\
		.n_linear_ranges	= _nlr,			\
		.ramp_delay		= _delay,		\
		.fixed_uV		= _fuv,			\
		.bypass_reg	= _sr,				\
		.bypass_mask	= _sm,				\
	}							\

#define TPS65218_INFO(_id, _nm, _min, _max)	\
	[_id] = {					\
		.id		= _id,			\
		.name		= _nm,			\
		.min_uV		= _min,			\
		.max_uV		= _max,			\
	}

static const struct regulator_linear_range dcdc1_dcdc2_ranges[] = {
	REGULATOR_LINEAR_RANGE(850000, 0x0, 0x32, 10000),
	REGULATOR_LINEAR_RANGE(1375000, 0x33, 0x3f, 25000),
};

static const struct regulator_linear_range ldo1_dcdc3_ranges[] = {
	REGULATOR_LINEAR_RANGE(900000, 0x0, 0x1a, 25000),
	REGULATOR_LINEAR_RANGE(1600000, 0x1b, 0x3f, 50000),
};

static const struct regulator_linear_range dcdc4_ranges[] = {
	REGULATOR_LINEAR_RANGE(1175000, 0x0, 0xf, 25000),
	REGULATOR_LINEAR_RANGE(1600000, 0x10, 0x34, 50000),
};

static struct tps_info tps65218_pmic_regs[] = {
	TPS65218_INFO(DCDC1, "DCDC1", 850000, 1675000),
	TPS65218_INFO(DCDC2, "DCDC2", 850000, 1675000),
	TPS65218_INFO(DCDC3, "DCDC3", 900000, 3400000),
	TPS65218_INFO(DCDC4, "DCDC4", 1175000, 3400000),
	TPS65218_INFO(DCDC5, "DCDC5", 1000000, 1000000),
	TPS65218_INFO(DCDC6, "DCDC6", 1800000, 1800000),
	TPS65218_INFO(LDO1, "LDO1", 900000, 3400000),
	TPS65218_INFO(LS3, "LS3", -1, -1),
};

#define TPS65218_OF_MATCH(comp, label) \
	{ \
		.compatible = comp, \
		.data = &label, \
	}

static const struct of_device_id tps65218_of_match[] = {
	TPS65218_OF_MATCH("ti,tps65218-dcdc1", tps65218_pmic_regs[DCDC1]),
	TPS65218_OF_MATCH("ti,tps65218-dcdc2", tps65218_pmic_regs[DCDC2]),
	TPS65218_OF_MATCH("ti,tps65218-dcdc3", tps65218_pmic_regs[DCDC3]),
	TPS65218_OF_MATCH("ti,tps65218-dcdc4", tps65218_pmic_regs[DCDC4]),
	TPS65218_OF_MATCH("ti,tps65218-dcdc5", tps65218_pmic_regs[DCDC5]),
	TPS65218_OF_MATCH("ti,tps65218-dcdc6", tps65218_pmic_regs[DCDC6]),
	TPS65218_OF_MATCH("ti,tps65218-ldo1", tps65218_pmic_regs[LDO1]),
	TPS65218_OF_MATCH("ti,tps65218-ls3", tps65218_pmic_regs[LS3]),
	{ }
};
MODULE_DEVICE_TABLE(of, tps65218_of_match);

static int tps65218_pmic_set_voltage_sel(struct regulator_dev *dev,
					 unsigned selector)
{
	int ret;
	struct tps65218 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	/* Set the voltage based on vsel value and write protect level is 2 */
	ret = tps65218_set_bits(tps, dev->desc->vsel_reg, dev->desc->vsel_mask,
				selector, TPS65218_PROTECT_L1);

	/* Set GO bit for DCDC1/2 to initiate voltage transistion */
	switch (rid) {
	case TPS65218_DCDC_1:
	case TPS65218_DCDC_2:
		ret = tps65218_set_bits(tps, TPS65218_REG_CONTRL_SLEW_RATE,
					TPS65218_SLEW_RATE_GO,
					TPS65218_SLEW_RATE_GO,
					TPS65218_PROTECT_L1);
		break;
	}

	return ret;
}

static int tps65218_pmic_enable(struct regulator_dev *dev)
{
	struct tps65218 *tps = rdev_get_drvdata(dev);
	int rid = rdev_get_id(dev);

	if (rid < TPS65218_DCDC_1 || rid > TPS65218_LDO_1)
		return -EINVAL;

	/* Enable the regulator and password protection is level 1 */
	return tps65218_set_bits(tps, dev->desc->enable_reg,
				 dev->desc->enable_mask, dev->desc->enable_mask,
				 TPS65218_PROTECT_L1);
}

static int tps65218_pmic_disable(struct regulator_dev *dev)
{
	struct tps65218 *tps = rdev_get_drvdata(dev);
	int rid = rdev_get_id(dev);

	if (rid < TPS65218_DCDC_1 || rid > TPS65218_LDO_1)
		return -EINVAL;

	/* Disable the regulator and password protection is level 1 */
	return tps65218_clear_bits(tps, dev->desc->enable_reg,
				   dev->desc->enable_mask, TPS65218_PROTECT_L1);
}

static int tps65218_pmic_set_suspend_enable(struct regulator_dev *dev)
{
	struct tps65218 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	if (rid < TPS65218_DCDC_1 || rid > TPS65218_LDO_1)
		return -EINVAL;

	return tps65218_clear_bits(tps, dev->desc->bypass_reg,
				   dev->desc->bypass_mask,
				   TPS65218_PROTECT_L1);
}

static int tps65218_pmic_set_suspend_disable(struct regulator_dev *dev)
{
	struct tps65218 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	if (rid < TPS65218_DCDC_1 || rid > TPS65218_LDO_1)
		return -EINVAL;

	/*
	 * Certain revisions of TPS65218 will need to have DCDC3 regulator
	 * enabled always, otherwise an immediate system reboot will occur
	 * during poweroff.
	 */
	if (rid == TPS65218_DCDC_3 && tps->rev == TPS65218_REV_2_1)
		return 0;

	if (!tps->info[rid]->strobe) {
		if (rid == TPS65218_DCDC_3)
			tps->info[rid]->strobe = 3;
		else
			return -EINVAL;
	}

	return tps65218_set_bits(tps, dev->desc->bypass_reg,
				 dev->desc->bypass_mask,
				 tps->info[rid]->strobe,
				 TPS65218_PROTECT_L1);
}

/* Operations permitted on DCDC1, DCDC2 */
static struct regulator_ops tps65218_dcdc12_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= tps65218_pmic_enable,
	.disable		= tps65218_pmic_disable,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= tps65218_pmic_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_suspend_enable	= tps65218_pmic_set_suspend_enable,
	.set_suspend_disable	= tps65218_pmic_set_suspend_disable,
};

/* Operations permitted on DCDC3, DCDC4 and LDO1 */
static struct regulator_ops tps65218_ldo1_dcdc34_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= tps65218_pmic_enable,
	.disable		= tps65218_pmic_disable,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= tps65218_pmic_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.set_suspend_enable	= tps65218_pmic_set_suspend_enable,
	.set_suspend_disable	= tps65218_pmic_set_suspend_disable,
};

static const int ls3_currents[] = { 100, 200, 500, 1000 };

static int tps65218_pmic_set_input_current_lim(struct regulator_dev *dev,
					       int lim_uA)
{
	unsigned int index = 0;
	unsigned int num_currents = ARRAY_SIZE(ls3_currents);
	struct tps65218 *tps = rdev_get_drvdata(dev);

	while (index < num_currents && ls3_currents[index] != lim_uA)
		index++;

	if (index == num_currents)
		return -EINVAL;

	return tps65218_set_bits(tps, dev->desc->csel_reg, dev->desc->csel_mask,
				 index << 2, TPS65218_PROTECT_L1);
}

static int tps65218_pmic_set_current_limit(struct regulator_dev *dev,
					   int min_uA, int max_uA)
{
	int index = 0;
	unsigned int num_currents = ARRAY_SIZE(ls3_currents);
	struct tps65218 *tps = rdev_get_drvdata(dev);

	while (index < num_currents && ls3_currents[index] < max_uA)
		index++;

	index--;

	if (index < 0 || ls3_currents[index] < min_uA)
		return -EINVAL;

	return tps65218_set_bits(tps, dev->desc->csel_reg, dev->desc->csel_mask,
				 index << 2, TPS65218_PROTECT_L1);
}

static int tps65218_pmic_get_current_limit(struct regulator_dev *dev)
{
	int retval;
	unsigned int index;
	struct tps65218 *tps = rdev_get_drvdata(dev);

	retval = tps65218_reg_read(tps, dev->desc->csel_reg, &index);
	if (retval < 0)
		return retval;

	index = (index & dev->desc->csel_mask) >> 2;

	return ls3_currents[index];
}

static struct regulator_ops tps65218_ls3_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= tps65218_pmic_enable,
	.disable		= tps65218_pmic_disable,
	.set_input_current_limit = tps65218_pmic_set_input_current_lim,
	.set_current_limit	= tps65218_pmic_set_current_limit,
	.get_current_limit	= tps65218_pmic_get_current_limit,
};

/* Operations permitted on DCDC5, DCDC6 */
static struct regulator_ops tps65218_dcdc56_pmic_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= tps65218_pmic_enable,
	.disable		= tps65218_pmic_disable,
	.set_suspend_enable	= tps65218_pmic_set_suspend_enable,
	.set_suspend_disable	= tps65218_pmic_set_suspend_disable,
};

static const struct regulator_desc regulators[] = {
	TPS65218_REGULATOR("DCDC1", TPS65218_DCDC_1, REGULATOR_VOLTAGE,
			   tps65218_dcdc12_ops, 64, TPS65218_REG_CONTROL_DCDC1,
			   TPS65218_CONTROL_DCDC1_MASK, TPS65218_REG_ENABLE1,
			   TPS65218_ENABLE1_DC1_EN, 0, 0, dcdc1_dcdc2_ranges,
			   2, 4000, 0, TPS65218_REG_SEQ3,
			   TPS65218_SEQ3_DC1_SEQ_MASK),
	TPS65218_REGULATOR("DCDC2", TPS65218_DCDC_2, REGULATOR_VOLTAGE,
			   tps65218_dcdc12_ops, 64, TPS65218_REG_CONTROL_DCDC2,
			   TPS65218_CONTROL_DCDC2_MASK, TPS65218_REG_ENABLE1,
			   TPS65218_ENABLE1_DC2_EN, 0, 0, dcdc1_dcdc2_ranges,
			   2, 4000, 0, TPS65218_REG_SEQ3,
			   TPS65218_SEQ3_DC2_SEQ_MASK),
	TPS65218_REGULATOR("DCDC3", TPS65218_DCDC_3, REGULATOR_VOLTAGE,
			   tps65218_ldo1_dcdc34_ops, 64,
			   TPS65218_REG_CONTROL_DCDC3,
			   TPS65218_CONTROL_DCDC3_MASK, TPS65218_REG_ENABLE1,
			   TPS65218_ENABLE1_DC3_EN, 0, 0, ldo1_dcdc3_ranges, 2,
			   0, 0, TPS65218_REG_SEQ4, TPS65218_SEQ4_DC3_SEQ_MASK),
	TPS65218_REGULATOR("DCDC4", TPS65218_DCDC_4, REGULATOR_VOLTAGE,
			   tps65218_ldo1_dcdc34_ops, 53,
			   TPS65218_REG_CONTROL_DCDC4,
			   TPS65218_CONTROL_DCDC4_MASK, TPS65218_REG_ENABLE1,
			   TPS65218_ENABLE1_DC4_EN, 0, 0, dcdc4_ranges, 2,
			   0, 0, TPS65218_REG_SEQ4, TPS65218_SEQ4_DC4_SEQ_MASK),
	TPS65218_REGULATOR("DCDC5", TPS65218_DCDC_5, REGULATOR_VOLTAGE,
			   tps65218_dcdc56_pmic_ops, 1, -1, -1,
			   TPS65218_REG_ENABLE1, TPS65218_ENABLE1_DC5_EN, 0, 0,
			   NULL, 0, 0, 1000000, TPS65218_REG_SEQ5,
			   TPS65218_SEQ5_DC5_SEQ_MASK),
	TPS65218_REGULATOR("DCDC6", TPS65218_DCDC_6, REGULATOR_VOLTAGE,
			   tps65218_dcdc56_pmic_ops, 1, -1, -1,
			   TPS65218_REG_ENABLE1, TPS65218_ENABLE1_DC6_EN, 0, 0,
			   NULL, 0, 0, 1800000, TPS65218_REG_SEQ5,
			   TPS65218_SEQ5_DC6_SEQ_MASK),
	TPS65218_REGULATOR("LDO1", TPS65218_LDO_1, REGULATOR_VOLTAGE,
			   tps65218_ldo1_dcdc34_ops, 64,
			   TPS65218_REG_CONTROL_LDO1,
			   TPS65218_CONTROL_LDO1_MASK, TPS65218_REG_ENABLE2,
			   TPS65218_ENABLE2_LDO1_EN, 0, 0, ldo1_dcdc3_ranges,
			   2, 0, 0, TPS65218_REG_SEQ6,
			   TPS65218_SEQ6_LDO1_SEQ_MASK),
	TPS65218_REGULATOR("LS3", TPS65218_LS_3, REGULATOR_CURRENT,
			   tps65218_ls3_ops, 0, 0, 0, TPS65218_REG_ENABLE2,
			   TPS65218_ENABLE2_LS3_EN, TPS65218_REG_CONFIG2,
			   TPS65218_CONFIG2_LS3ILIM_MASK, NULL, 0, 0, 0, 0, 0),
};

static int tps65218_regulator_probe(struct platform_device *pdev)
{
	struct tps65218 *tps = dev_get_drvdata(pdev->dev.parent);
	struct regulator_init_data *init_data;
	const struct tps_info	*template;
	struct regulator_dev *rdev;
	const struct of_device_id	*match;
	struct regulator_config config = { };
	int id, ret;
	unsigned int val;

	match = of_match_device(tps65218_of_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	template = match->data;
	id = template->id;
	init_data = of_get_regulator_init_data(&pdev->dev, pdev->dev.of_node,
					       &regulators[id]);

	platform_set_drvdata(pdev, tps);

	tps->info[id] = &tps65218_pmic_regs[id];
	config.dev = &pdev->dev;
	config.init_data = init_data;
	config.driver_data = tps;
	config.regmap = tps->regmap;
	config.of_node = pdev->dev.of_node;

	rdev = devm_regulator_register(&pdev->dev, &regulators[id], &config);
	if (IS_ERR(rdev)) {
		dev_err(tps->dev, "failed to register %s regulator\n",
			pdev->name);
		return PTR_ERR(rdev);
	}

	ret = tps65218_reg_read(tps, regulators[id].bypass_reg, &val);
	if (ret)
		return ret;

	tps->info[id]->strobe = val & regulators[id].bypass_mask;

	return 0;
}

static struct platform_driver tps65218_regulator_driver = {
	.driver = {
		.name = "tps65218-pmic",
		.of_match_table = tps65218_of_match,
	},
	.probe = tps65218_regulator_probe,
};

module_platform_driver(tps65218_regulator_driver);

MODULE_AUTHOR("J Keerthy <j-keerthy@ti.com>");
MODULE_DESCRIPTION("TPS65218 voltage regulator driver");
MODULE_ALIAS("platform:tps65218-pmic");
MODULE_LICENSE("GPL v2");
