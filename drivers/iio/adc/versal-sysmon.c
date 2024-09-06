// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx SYSMON for Versal
 *
 * Copyright (C) 2023 - 2024, Advanced Micro Devices, Inc.
 *
 * Description:
 * This driver is developed for SYSMON on Versal. The driver supports INDIO Mode
 * and supports voltage and temperature monitoring via IIO sysfs interface and
 * in kernel event monitoring for some modules.
 */

#include <linux/bits.h>
#include <linux/moduleparam.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include "versal-sysmon.h"

#define SYSMON_EVENT_WORK_DELAY_MS	1000

static LIST_HEAD(sysmon_list_head);

static bool secure_mode;
module_param(secure_mode, bool, 0444);
MODULE_PARM_DESC(secure_mode,
		 "Allow sysmon to access register space using EEMI, when direct register access is restricted or Direct Access Mode (default: Direct Access mode)");

static struct iio_map sysmon_to_thermal_iio_maps[] = {
	IIO_MAP("temp", "versal-thermal", "sysmon-temp-channel"),
	{}
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
	zynqmp_pm_sec_read_reg(sysmon->pm_info, offset, data);
}

static inline void sysmon_secure_write_reg(struct sysmon *sysmon, u32 offset, u32 data)
{
	zynqmp_pm_sec_mask_write_reg(sysmon->pm_info, offset, GENMASK(31, 0), data);
}

static inline void sysmon_secure_update_reg(struct sysmon *sysmon, u32 offset,
					    u32 mask, u32 data)
{
	u32 val;

	zynqmp_pm_sec_read_reg(sysmon->pm_info, offset, &val);
	zynqmp_pm_sec_mask_write_reg(sysmon->pm_info, offset, GENMASK(31, 0),
				     (val & ~mask) | (mask & data));
}

static struct sysmon_ops secure_access = {
	.read_reg = sysmon_secure_read_reg,
	.write_reg = sysmon_secure_write_reg,
	.update_reg = sysmon_secure_update_reg,
};

/**
 * sysmon_find_extreme_temp() - Finds extreme temperature
 * value read from each device.
 * @sysmon: Pointer to the sysmon instance
 * @offset: Register offset address of temperature channels
 *
 * The function takes offset address of temperature channels
 * returns extreme value (highest/lowest) of that channel
 *
 * @return: - The highest/lowest temperature found from
 * current or historic min/max temperature of all devices.
 */
static int sysmon_find_extreme_temp(struct sysmon *sysmon, int offset)
{
	u32 extreme_val = SYSMON_LOWER_SATURATION_SIGNED;
	bool is_min_channel = false, skip_hbm = true;
	u32 regval;

	if (offset == SYSMON_TEMP_MIN || offset == SYSMON_TEMP_MIN_MIN) {
		is_min_channel = true;
		extreme_val = SYSMON_UPPER_SATURATION_SIGNED;
	} else if (offset == SYSMON_TEMP_HBM) {
		skip_hbm = false;
	}

	list_for_each_entry(sysmon, &sysmon_list_head, list) {
		if (skip_hbm && sysmon->hbm_slr)
			/* Skip if HBM SLR and need non HBM reading */
			continue;

		if (!skip_hbm && !sysmon->hbm_slr)
			/* Skip if not HBM SLR and need a HBM reading */
			continue;

		sysmon_read_reg(sysmon, offset, &regval);

		if (sysmon->hbm_slr)
			return regval;

		if (!is_min_channel) {
			/* Find the highest value */
			if (compare(regval, extreme_val))
				extreme_val = regval;
		} else {
			/* Find the lowest value */
			if (compare(extreme_val, regval))
				extreme_val = regval;
		}
	}

	return extreme_val;
}

static int sysmon_probe(struct platform_device *pdev)
{
	struct sysmon *sysmon, *temp_sysmon;
	struct iio_dev *indio_dev;
	struct resource *mem;
	bool exist = false;
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
	sysmon_set_iio_dev_info(indio_dev);
	indio_dev->modes = INDIO_DIRECT_MODE;

	sysmon->base = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(sysmon->base))
		return PTR_ERR(sysmon->base);

	if (secure_mode) {
		ret = of_property_read_u32(pdev->dev.of_node,
					   "xlnx,nodeid", &sysmon->pm_info);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to read SLR node id\n");
			return ret;
		}

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

	INIT_LIST_HEAD(&sysmon->list);

	mutex_lock(&sysmon->mutex);

	if (list_empty(&sysmon_list_head)) {
		sysmon->master_slr = true;
	} else {
		list_for_each_entry(temp_sysmon, &sysmon_list_head, list) {
			if (temp_sysmon->master_slr)
				exist = true;
		}
		sysmon->master_slr = !exist;
	}

	mutex_unlock(&sysmon->mutex);

	sysmon->hbm_slr = of_property_read_bool(pdev->dev.of_node, "xlnx,hbm");
	if (!sysmon->hbm_slr) {
		sysmon_write_reg(sysmon, SYSMON_NPI_LOCK, NPI_UNLOCK);
		sysmon_write_reg(sysmon, SYSMON_IDR, 0xffffffff);
		sysmon_write_reg(sysmon, SYSMON_ISR, 0xffffffff);
		sysmon->irq = platform_get_irq_optional(pdev, 0);
	}

	ret = sysmon_parse_dt(indio_dev, &pdev->dev);
	if (ret)
		return ret;

	if (!sysmon->hbm_slr) {
		ret = sysmon_init_interrupt(sysmon);
			if (ret)
				return ret;
	}

	/*
	 * Sysmon dev info is cleared initially.
	 * temperature satellites and supply channels
	 * oversampling values will be 0. No need to
	 * assign them again.
	 */
	sysmon->oversampling_avail = sysmon_oversampling_avail;
	sysmon->oversampling_num = ARRAY_SIZE(sysmon_oversampling_avail);

	sysmon->temp_read = &sysmon_find_extreme_temp;

	platform_set_drvdata(pdev, indio_dev);

	if (sysmon->master_slr) {
		ret = devm_iio_map_array_register(&pdev->dev, indio_dev,
						  sysmon_to_thermal_iio_maps);
		if (ret < 0)
			return dev_err_probe(&pdev->dev, ret, "IIO map register failed\n");
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto error_exit;

	/* Create the sysfs entries for the averaging enable bits */
	ret = sysmon_create_avg_en_sysfs_entries(indio_dev);
	if (ret < 0)
		goto error_exit;

	mutex_lock(&sysmon->mutex);
	list_add(&sysmon->list, &sysmon_list_head);
	mutex_unlock(&sysmon->mutex);

	return 0;

error_exit:
	if (sysmon->irq < 0)
		cancel_delayed_work_sync(&sysmon->sysmon_events_work);

	cancel_delayed_work_sync(&sysmon->sysmon_unmask_work);
	return ret;
}

static int sysmon_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct sysmon *sysmon = iio_priv(indio_dev);

	/* cancel SSIT based events */
	if (sysmon->irq < 0)
		cancel_delayed_work_sync(&sysmon->sysmon_events_work);

	cancel_delayed_work_sync(&sysmon->sysmon_unmask_work);

	mutex_lock(&sysmon->mutex);
	list_del(&sysmon->list);
	mutex_unlock(&sysmon->mutex);

	sysfs_remove_group(&indio_dev->dev.kobj, &sysmon->avg_attr_group);
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

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xilinx Versal SysMon Driver");
MODULE_AUTHOR("Conall O Griofa <conall.ogriofa@amd.com>");
