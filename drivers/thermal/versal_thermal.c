// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Versal Thermal Driver
 * for Versal Devices
 *
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
 *
 * Author: Salih Erim <salih.erim@amd.com>
 */

#include <linux/iio/consumer.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#include "thermal_hwmon.h"

#define SYSMON_TEMP_CH_NAME "sysmon-temp-channel"
#define SYSMON_FRACTIONAL_DENOM	128

struct versal_thermal_info {
	struct device *dev;
	struct thermal_zone_device *tzd;
	struct iio_channel *channel;
};

static int temperature_sensor_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct versal_thermal_info *vti = thermal_zone_device_priv(tz);
	int ret, val;

	ret = iio_read_channel_processed(vti->channel, &val);
	if (ret == IIO_VAL_FRACTIONAL) {
		/* Convert raw value to temperature in millidegrees Celsius */
		*temp = val * 1000;
		*temp /= SYSMON_FRACTIONAL_DENOM;
	} else if (ret == IIO_VAL_INT) {
		*temp = val;
	} else {
		dev_err(vti->dev, "iio_read_channel_processed failed, ret code = %d\n", ret);
		return ret;
	}
	return 0;
}

static const struct thermal_zone_device_ops thermal_zone_ops = {
	.get_temp = temperature_sensor_get_temp,
};

static int versal_thermal_probe(struct platform_device *pdev)
{
	struct versal_thermal_info *vti;

	vti = devm_kzalloc(&pdev->dev, sizeof(struct versal_thermal_info), GFP_KERNEL);
	if (!vti)
		return -ENOMEM;

	vti->channel = devm_iio_channel_get(&pdev->dev, SYSMON_TEMP_CH_NAME);
	if (IS_ERR(vti->channel))
		return dev_err_probe(&pdev->dev, PTR_ERR(vti->channel),
				     "IIO channel not found\n");

	vti->dev = &pdev->dev;

	vti->tzd = devm_thermal_of_zone_register(&pdev->dev, 0, vti, &thermal_zone_ops);
	if (IS_ERR(vti->tzd))
		return dev_err_probe(&pdev->dev, PTR_ERR(vti->tzd),
				     "Thermal zone sensor register failed\n");

	return devm_thermal_add_hwmon_sysfs(&pdev->dev, vti->tzd);
}

static const struct of_device_id versal_thermal_of_match[] = {
	{ .compatible = "xlnx,versal-thermal", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, versal_thermal_of_match);

static struct platform_driver versal_thermal_driver = {
	.driver = {
		.name = "versal-thermal",
		.of_match_table	= versal_thermal_of_match,
	},
	.probe = versal_thermal_probe,
};

module_platform_driver(versal_thermal_driver);

MODULE_AUTHOR("Salih Erim <salih.erim@amd.com>");
MODULE_DESCRIPTION("XILINX Versal Thermal Driver");
MODULE_LICENSE("GPL");
