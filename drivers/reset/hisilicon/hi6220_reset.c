/*
 * Hisilicon Hi6220 reset controller driver
 *
 * Copyright (c) 2016 Linaro Limited.
 * Copyright (c) 2015-2016 Hisilicon Limited.
 *
 * Author: Feng Chen <puck.chen@hisilicon.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/reset-controller.h>
#include <linux/reset.h>
#include <linux/platform_device.h>

#define PERIPH_ASSERT_OFFSET      0x300
#define PERIPH_DEASSERT_OFFSET    0x304
#define PERIPH_MAX_INDEX          0x509

#define SC_MEDIA_RSTEN            0x052C
#define SC_MEDIA_RSTDIS           0x0530
#define MEDIA_MAX_INDEX           8

#define to_reset_data(x) container_of(x, struct hi6220_reset_data, rc_dev)

enum hi6220_reset_ctrl_type {
	PERIPHERAL,
	MEDIA,
};

struct hi6220_reset_data {
	struct reset_controller_dev rc_dev;
	struct regmap *regmap;
};

static int hi6220_peripheral_assert(struct reset_controller_dev *rc_dev,
				    unsigned long idx)
{
	struct hi6220_reset_data *data = to_reset_data(rc_dev);
	struct regmap *regmap = data->regmap;
	u32 bank = idx >> 8;
	u32 offset = idx & 0xff;
	u32 reg = PERIPH_ASSERT_OFFSET + bank * 0x10;

	return regmap_write(regmap, reg, BIT(offset));
}

static int hi6220_peripheral_deassert(struct reset_controller_dev *rc_dev,
				      unsigned long idx)
{
	struct hi6220_reset_data *data = to_reset_data(rc_dev);
	struct regmap *regmap = data->regmap;
	u32 bank = idx >> 8;
	u32 offset = idx & 0xff;
	u32 reg = PERIPH_DEASSERT_OFFSET + bank * 0x10;

	return regmap_write(regmap, reg, BIT(offset));
}

static const struct reset_control_ops hi6220_peripheral_reset_ops = {
	.assert = hi6220_peripheral_assert,
	.deassert = hi6220_peripheral_deassert,
};

static int hi6220_media_assert(struct reset_controller_dev *rc_dev,
			       unsigned long idx)
{
	struct hi6220_reset_data *data = to_reset_data(rc_dev);
	struct regmap *regmap = data->regmap;

	return regmap_write(regmap, SC_MEDIA_RSTEN, BIT(idx));
}

static int hi6220_media_deassert(struct reset_controller_dev *rc_dev,
				 unsigned long idx)
{
	struct hi6220_reset_data *data = to_reset_data(rc_dev);
	struct regmap *regmap = data->regmap;

	return regmap_write(regmap, SC_MEDIA_RSTDIS, BIT(idx));
}

static const struct reset_control_ops hi6220_media_reset_ops = {
	.assert = hi6220_media_assert,
	.deassert = hi6220_media_deassert,
};

static int hi6220_reset_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	enum hi6220_reset_ctrl_type type;
	struct hi6220_reset_data *data;
	struct regmap *regmap;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	type = (enum hi6220_reset_ctrl_type)of_device_get_match_data(dev);

	regmap = syscon_node_to_regmap(np);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to get reset controller regmap\n");
		return PTR_ERR(regmap);
	}

	data->regmap = regmap;
	data->rc_dev.of_node = np;
	if (type == MEDIA) {
		data->rc_dev.ops = &hi6220_media_reset_ops;
		data->rc_dev.nr_resets = MEDIA_MAX_INDEX;
	} else {
		data->rc_dev.ops = &hi6220_peripheral_reset_ops;
		data->rc_dev.nr_resets = PERIPH_MAX_INDEX;
	}

	return reset_controller_register(&data->rc_dev);
}

static const struct of_device_id hi6220_reset_match[] = {
	{
		.compatible = "hisilicon,hi6220-sysctrl",
		.data = (void *)PERIPHERAL,
	},
	{
		.compatible = "hisilicon,hi6220-mediactrl",
		.data = (void *)MEDIA,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, hi6220_reset_match);

static struct platform_driver hi6220_reset_driver = {
	.probe = hi6220_reset_probe,
	.driver = {
		.name = "reset-hi6220",
		.of_match_table = hi6220_reset_match,
	},
};

static int __init hi6220_reset_init(void)
{
	return platform_driver_register(&hi6220_reset_driver);
}

postcore_initcall(hi6220_reset_init);
