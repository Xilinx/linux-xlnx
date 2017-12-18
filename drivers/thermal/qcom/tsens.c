/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include "tsens.h"

static int tsens_get_temp(void *data, int *temp)
{
	const struct tsens_sensor *s = data;
	struct tsens_device *tmdev = s->tmdev;

	return tmdev->ops->get_temp(tmdev, s->id, temp);
}

static int tsens_get_trend(void *p, int trip, enum thermal_trend *trend)
{
	const struct tsens_sensor *s = p;
	struct tsens_device *tmdev = s->tmdev;

	if (tmdev->ops->get_trend)
		return  tmdev->ops->get_trend(tmdev, s->id, trend);

	return -ENOTSUPP;
}

static int  __maybe_unused tsens_suspend(struct device *dev)
{
	struct tsens_device *tmdev = dev_get_drvdata(dev);

	if (tmdev->ops && tmdev->ops->suspend)
		return tmdev->ops->suspend(tmdev);

	return 0;
}

static int __maybe_unused tsens_resume(struct device *dev)
{
	struct tsens_device *tmdev = dev_get_drvdata(dev);

	if (tmdev->ops && tmdev->ops->resume)
		return tmdev->ops->resume(tmdev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(tsens_pm_ops, tsens_suspend, tsens_resume);

static const struct of_device_id tsens_table[] = {
	{
		.compatible = "qcom,msm8916-tsens",
		.data = &data_8916,
	}, {
		.compatible = "qcom,msm8974-tsens",
		.data = &data_8974,
	}, {
		.compatible = "qcom,msm8996-tsens",
		.data = &data_8996,
	},
	{}
};
MODULE_DEVICE_TABLE(of, tsens_table);

static const struct thermal_zone_of_device_ops tsens_of_ops = {
	.get_temp = tsens_get_temp,
	.get_trend = tsens_get_trend,
};

static int tsens_register(struct tsens_device *tmdev)
{
	int i;
	struct thermal_zone_device *tzd;
	u32 *hw_id, n = tmdev->num_sensors;

	hw_id = devm_kcalloc(tmdev->dev, n, sizeof(u32), GFP_KERNEL);
	if (!hw_id)
		return -ENOMEM;

	for (i = 0;  i < tmdev->num_sensors; i++) {
		tmdev->sensor[i].tmdev = tmdev;
		tmdev->sensor[i].id = i;
		tzd = devm_thermal_zone_of_sensor_register(tmdev->dev, i,
							   &tmdev->sensor[i],
							   &tsens_of_ops);
		if (IS_ERR(tzd))
			continue;
		tmdev->sensor[i].tzd = tzd;
		if (tmdev->ops->enable)
			tmdev->ops->enable(tmdev, i);
	}
	return 0;
}

static int tsens_probe(struct platform_device *pdev)
{
	int ret, i;
	struct device *dev;
	struct device_node *np;
	struct tsens_sensor *s;
	struct tsens_device *tmdev;
	const struct tsens_data *data;
	const struct of_device_id *id;

	if (pdev->dev.of_node)
		dev = &pdev->dev;
	else
		dev = pdev->dev.parent;

	np = dev->of_node;

	id = of_match_node(tsens_table, np);
	if (id)
		data = id->data;
	else
		data = &data_8960;

	if (data->num_sensors <= 0) {
		dev_err(dev, "invalid number of sensors\n");
		return -EINVAL;
	}

	tmdev = devm_kzalloc(dev, sizeof(*tmdev) +
			     data->num_sensors * sizeof(*s), GFP_KERNEL);
	if (!tmdev)
		return -ENOMEM;

	tmdev->dev = dev;
	tmdev->num_sensors = data->num_sensors;
	tmdev->ops = data->ops;
	for (i = 0;  i < tmdev->num_sensors; i++) {
		if (data->hw_ids)
			tmdev->sensor[i].hw_id = data->hw_ids[i];
		else
			tmdev->sensor[i].hw_id = i;
	}

	if (!tmdev->ops || !tmdev->ops->init || !tmdev->ops->get_temp)
		return -EINVAL;

	ret = tmdev->ops->init(tmdev);
	if (ret < 0) {
		dev_err(dev, "tsens init failed\n");
		return ret;
	}

	if (tmdev->ops->calibrate) {
		ret = tmdev->ops->calibrate(tmdev);
		if (ret < 0) {
			dev_err(dev, "tsens calibration failed\n");
			return ret;
		}
	}

	ret = tsens_register(tmdev);

	platform_set_drvdata(pdev, tmdev);

	return ret;
}

static int tsens_remove(struct platform_device *pdev)
{
	struct tsens_device *tmdev = platform_get_drvdata(pdev);

	if (tmdev->ops->disable)
		tmdev->ops->disable(tmdev);

	return 0;
}

static struct platform_driver tsens_driver = {
	.probe = tsens_probe,
	.remove = tsens_remove,
	.driver = {
		.name = "qcom-tsens",
		.pm	= &tsens_pm_ops,
		.of_match_table = tsens_table,
	},
};
module_platform_driver(tsens_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QCOM Temperature Sensor driver");
MODULE_ALIAS("platform:qcom-tsens");
