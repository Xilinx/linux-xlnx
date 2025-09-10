// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Versal Thermal Driver
 * for Versal Devices
 *
 * Copyright (C) 2024 - 2025 Advanced Micro Devices, Inc.
 *
 * Author: Salih Erim <salih.erim@amd.com>
 */

#include <linux/iio/consumer.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#include "thermal_hwmon.h"

#define SYSMON_TEMP_CH_NAME "sysmon-temp-channel"
#define SYSMON_FRACTIONAL_DENOM	128
#define SYSMON_STATIC_IIO_CH_COUNT	1
#define SYSMON_AIE_TEMP_CH		200
#define TEMP_MAX			160

struct versal_thermal_info {
	struct device *dev;
	struct thermal_zone_device *tzd;
	struct thermal_zone_device *tzd_aie;
	struct iio_channel *channel;
	struct iio_channel **channel_aie;
	u32 num_aie_channels;
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

static int temperature_sensor_get_temp_aie(struct thermal_zone_device *tz,
					   int *temp)
{
	struct versal_thermal_info *vti = thermal_zone_device_priv(tz);
	int ret, val;
	int max_temp = INT_MIN;
	u32 ch_index;

	for (ch_index = 0; ch_index < vti->num_aie_channels;
		 ch_index++) {
		ret = iio_read_channel_processed(vti->channel_aie[ch_index], &val);
		if (ret == IIO_VAL_FRACTIONAL) {
			/* Convert raw value to temperature in millidegrees Celsius */
			*temp = val * 1000;
			*temp /= SYSMON_FRACTIONAL_DENOM;
		} else if (ret == IIO_VAL_INT) {
			*temp = val;
		} else {
			dev_err(vti->dev, "iio_read_channel_processed failed aie ch%d, ret = %d\n",
				ch_index, ret);
			return ret;
		}
		if (*temp > max_temp)
			max_temp = *temp;
	}
	*temp = max_temp;
	return 0;
}

static const struct thermal_zone_device_ops thermal_zone_ops = {
	.get_temp = temperature_sensor_get_temp,
};

static const struct thermal_zone_device_ops thermal_zone_ops_aie = {
	.get_temp = temperature_sensor_get_temp_aie,
};

static int versal_thermal_probe(struct platform_device *pdev)
{
	struct versal_thermal_info *vti;
	int ch_index = 0;
	int ret = 0;
	u32 ch_count = 0;
	u32 num_aie_channels = 0;
	const char *aie_temp_chan_name;

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

	ret = devm_thermal_add_hwmon_sysfs(&pdev->dev, vti->tzd);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to add hwmon sysfs for sysmon temp\n");

	ch_count = of_property_count_strings(vti->dev->of_node, "io-channel-names");
	num_aie_channels = ch_count - 1;

	if (num_aie_channels > 0) {
		/* Allocate memory for the dynamic aie temperature channels */
		vti->channel_aie = devm_kcalloc(&pdev->dev, num_aie_channels,
						sizeof(*vti->channel_aie), GFP_KERNEL);
		if (!vti->channel_aie)
			return -ENOMEM;

		for (ch_index = 1; ch_index < ch_count; ch_index++) {
			if (!of_property_read_string_index(vti->dev->of_node,
							   "io-channel-names", ch_index,
							   &aie_temp_chan_name)) {
				vti->channel_aie[ch_index - 1] =
					devm_iio_channel_get(&pdev->dev, aie_temp_chan_name);
				if (IS_ERR(vti->channel_aie[ch_index]))
					return dev_err_probe(&pdev->dev,
							PTR_ERR(vti->channel_aie[ch_index - 1]),
							"IIO channel not found\n");
			} else {
				return -EINVAL;
			}
		}
		vti->tzd_aie = devm_thermal_of_zone_register(&pdev->dev, 1, vti,
							     &thermal_zone_ops_aie);
		if (IS_ERR(vti->tzd_aie))
			return dev_err_probe(&pdev->dev, PTR_ERR(vti->tzd_aie),
					      "Failed to register thermal zone aie temp\n");

		ret = devm_thermal_add_hwmon_sysfs(&pdev->dev, vti->tzd_aie);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "Failed to add hwmon sysfs for aie temp\n");
	}
	vti->num_aie_channels = num_aie_channels;
	platform_set_drvdata(pdev, vti);
	return 0;
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
