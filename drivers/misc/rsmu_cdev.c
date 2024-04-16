// SPDX-License-Identifier: GPL-2.0+
/*
 * This driver is developed for the IDT ClockMatrix(TM) and 82P33xxx families
 * of timing and synchronization devices. It will be used by Renesas PTP Clock
 * Manager for Linux (pcm4l) software to provide support to GNSS assisted
 * partial timing support (APTS) and other networking timing functions.
 *
 * Please note it must work with Renesas MFD driver to access device through
 * I2C/SPI.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mfd/rsmu.h>
#include "rsmu_cdev.h"

static DEFINE_IDA(rsmu_cdev_map);

/*
 * The name of the firmware file to be loaded
 * over-rides any automatic selection
 */
static char *firmware;
module_param(firmware, charp, 0);

static struct rsmu_ops *ops_array[] = {
	[0] = &cm_ops,
	[1] = &sabre_ops,
};

static int
rsmu_set_combomode(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_ops *ops = rsmu->ops;
	struct rsmu_combomode mode;
	int err;

	if (copy_from_user(&mode, arg, sizeof(mode)))
		return -EFAULT;

	if (ops->set_combomode == NULL)
		return -EOPNOTSUPP;

	mutex_lock(rsmu->lock);
	err = ops->set_combomode(rsmu, mode.dpll, mode.mode);
	mutex_unlock(rsmu->lock);

	return err;
}

static int
rsmu_get_dpll_state(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_ops *ops = rsmu->ops;
	struct rsmu_get_state state_request;
	u8 state;
	int err;

	if (copy_from_user(&state_request, arg, sizeof(state_request)))
		return -EFAULT;

	if (ops->get_dpll_state == NULL)
		return -EOPNOTSUPP;

	mutex_lock(rsmu->lock);
	err = ops->get_dpll_state(rsmu, state_request.dpll, &state);
	mutex_unlock(rsmu->lock);

	state_request.state = state;
	if (copy_to_user(arg, &state_request, sizeof(state_request)))
		return -EFAULT;

	return err;
}

static int
rsmu_get_dpll_ffo(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_ops *ops = rsmu->ops;
	struct rsmu_get_ffo ffo_request;
	int err;

	if (copy_from_user(&ffo_request, arg, sizeof(ffo_request)))
		return -EFAULT;

	if (ops->get_dpll_ffo == NULL)
		return -EOPNOTSUPP;

	mutex_lock(rsmu->lock);
	err = ops->get_dpll_ffo(rsmu, ffo_request.dpll, &ffo_request);
	mutex_unlock(rsmu->lock);

	if (copy_to_user(arg, &ffo_request, sizeof(ffo_request)))
		return -EFAULT;

	return err;
}

static int
rsmu_set_holdover_mode(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_ops *ops = rsmu->ops;
	struct rsmu_holdover_mode request;
	int err;

	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;

	if (ops->set_holdover_mode == NULL)
		return -EOPNOTSUPP;

	mutex_lock(rsmu->lock);
	err = ops->set_holdover_mode(rsmu, request.dpll, request.enable, request.mode);
	mutex_unlock(rsmu->lock);

	return err;
}

static int
rsmu_set_output_tdc_go(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_ops *ops = rsmu->ops;
	struct rsmu_set_output_tdc_go request;
	int err;

	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;

	if (ops->set_output_tdc_go == NULL)
		return -EOPNOTSUPP;

	mutex_lock(rsmu->lock);
	err = ops->set_output_tdc_go(rsmu, request.tdc, request.enable);
	mutex_unlock(rsmu->lock);

	return err;
}

static int
rsmu_reg_read(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_reg_rw data;
	int err;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;

	mutex_lock(rsmu->lock);
	err = regmap_bulk_read(rsmu->regmap, data.offset, &data.bytes[0], data.byte_count);
	mutex_unlock(rsmu->lock);

	if (copy_to_user(arg, &data, sizeof(data)))
		return -EFAULT;

	return err;
}

static int
rsmu_reg_write(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_reg_rw data;
	int err;

	if (copy_from_user(&data, arg, sizeof(data)))
		return -EFAULT;

	mutex_lock(rsmu->lock);
	err = regmap_bulk_write(rsmu->regmap, data.offset, &data.bytes[0], data.byte_count);
	mutex_unlock(rsmu->lock);

	return err;
}

static int
rsmu_get_clock_index(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_ops *ops = rsmu->ops;
	struct rsmu_current_clock_index request;
	s8 clock_index;
	int err;

	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;

	if (ops->get_clock_index == NULL)
		return -EOPNOTSUPP;

	mutex_lock(rsmu->lock);
	err = ops->get_clock_index(rsmu, request.dpll, &clock_index);
	mutex_unlock(rsmu->lock);

	request.clock_index = clock_index;
	if (copy_to_user(arg, &request, sizeof(request)))
		return -EFAULT;

	return err;
}

static int
rsmu_set_clock_priorities(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_ops *ops = rsmu->ops;
	struct rsmu_clock_priorities request;
	int err;

	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;

	if (ops->set_clock_priorities == NULL)
		return -EOPNOTSUPP;

	mutex_lock(rsmu->lock);
	err = ops->set_clock_priorities(rsmu, request.dpll, request.num_entries, request.priority_entry);
	mutex_unlock(rsmu->lock);

	return err;
}

static int
rsmu_get_reference_monitor_status(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_ops *ops = rsmu->ops;
	struct rsmu_reference_monitor_status request;
	struct rsmu_reference_monitor_status_alarms alarms;
	int err;

	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;

	if (ops->get_reference_monitor_status == NULL)
		return -EOPNOTSUPP;

	mutex_lock(rsmu->lock);
	err = ops->get_reference_monitor_status(rsmu, request.clock_index, &alarms);
	mutex_unlock(rsmu->lock);

	memcpy(&request.alarms, &alarms, sizeof(alarms));
	if (copy_to_user(arg, &request, sizeof(request)))
		return -EFAULT;

	return err;
}

static struct rsmu_cdev *file2rsmu(struct file *file)
{
	return container_of(file->private_data, struct rsmu_cdev, miscdev);
}

static long
rsmu_ioctl(struct file *fptr, unsigned int cmd, unsigned long data)
{
	struct rsmu_cdev *rsmu = file2rsmu(fptr);
	void __user *arg = (void __user *)data;
	int err = 0;

	switch (cmd) {
	case RSMU_SET_COMBOMODE:
		err = rsmu_set_combomode(rsmu, arg);
		break;
	case RSMU_GET_STATE:
		err = rsmu_get_dpll_state(rsmu, arg);
		break;
	case RSMU_GET_FFO:
		err = rsmu_get_dpll_ffo(rsmu, arg);
		break;
	case RSMU_SET_HOLDOVER_MODE:
		err = rsmu_set_holdover_mode(rsmu, arg);
		break;
	case RSMU_SET_OUTPUT_TDC_GO:
		err = rsmu_set_output_tdc_go(rsmu, arg);
		break;
	case RSMU_GET_CURRENT_CLOCK_INDEX:
		err = rsmu_get_clock_index(rsmu, arg);
		break;
	case RSMU_SET_CLOCK_PRIORITIES:
		err = rsmu_set_clock_priorities(rsmu, arg);
		break;
	case RSMU_GET_REFERENCE_MONITOR_STATUS:
		err = rsmu_get_reference_monitor_status(rsmu, arg);
		break;
	case RSMU_REG_READ:
		err = rsmu_reg_read(rsmu, arg);
		break;
	case RSMU_REG_WRITE:
		err = rsmu_reg_write(rsmu, arg);
		break;
	default:
		/* Should not get here */
		dev_err(rsmu->dev, "Undefined RSMU IOCTL");
		err = -EINVAL;
		break;
	}

	return err;
}

static long rsmu_compat_ioctl(struct file *fptr, unsigned int cmd,
			      unsigned long data)
{
	return rsmu_ioctl(fptr, cmd, data);
}

static const struct file_operations rsmu_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = rsmu_ioctl,
	.compat_ioctl =	rsmu_compat_ioctl,
};

static int rsmu_init_ops(struct rsmu_cdev *rsmu)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ops_array); i++)
		if (ops_array[i]->type == rsmu->type)
			break;

	if (i == ARRAY_SIZE(ops_array))
		return -EINVAL;

	rsmu->ops = ops_array[i];
	return 0;
}

static int
rsmu_probe(struct platform_device *pdev)
{
	struct rsmu_ddata *ddata = dev_get_drvdata(pdev->dev.parent);
	struct rsmu_cdev *rsmu;
	int err;

	rsmu = devm_kzalloc(&pdev->dev, sizeof(*rsmu), GFP_KERNEL);
	if (!rsmu)
		return -ENOMEM;

	/* Save driver private data */
	platform_set_drvdata(pdev, rsmu);

	rsmu->dev = &pdev->dev;
	rsmu->mfd = pdev->dev.parent;
	rsmu->type = ddata->type;
	rsmu->lock = &ddata->lock;
	rsmu->regmap = ddata->regmap;
	rsmu->index = ida_simple_get(&rsmu_cdev_map, 0, MINORMASK + 1, GFP_KERNEL);
	if (rsmu->index < 0) {
		dev_err(rsmu->dev, "Unable to get index %d\n", rsmu->index);
		return rsmu->index;
	}
	snprintf(rsmu->name, sizeof(rsmu->name), "rsmu%d", rsmu->index);

	err = rsmu_init_ops(rsmu);
	if (err) {
		dev_err(rsmu->dev, "Unknown SMU type %d", rsmu->type);
		ida_simple_remove(&rsmu_cdev_map, rsmu->index);
		return err;
	}

	if (rsmu->ops->get_fw_version) {
		err = rsmu->ops->get_fw_version(rsmu);
		if (err) {
			dev_err(rsmu->dev, "Unable to get firmware version\n");
			ida_simple_remove(&rsmu_cdev_map, rsmu->index);
			return err;
		}
	}

	if (rsmu->ops->load_firmware)
		(void)rsmu->ops->load_firmware(rsmu, firmware);

	/* Initialize and register the miscdev */
	rsmu->miscdev.minor = MISC_DYNAMIC_MINOR;
	rsmu->miscdev.fops = &rsmu_fops;
	rsmu->miscdev.name = rsmu->name;
	err = misc_register(&rsmu->miscdev);
	if (err) {
		dev_err(rsmu->dev, "Unable to register device\n");
		ida_simple_remove(&rsmu_cdev_map, rsmu->index);
		return -ENODEV;
	}

	dev_info(rsmu->dev, "Probe %s successful\n", rsmu->name);
	return 0;
}

static int
rsmu_remove(struct platform_device *pdev)
{
	struct rsmu_cdev *rsmu = platform_get_drvdata(pdev);

	misc_deregister(&rsmu->miscdev);
	ida_simple_remove(&rsmu_cdev_map, rsmu->index);

	return 0;
}

static const struct platform_device_id rsmu_id_table[] = {
	{ "8a3400x-cdev", RSMU_CM },
	{ "82p33x1x-cdev", RSMU_SABRE },
	{}
};
MODULE_DEVICE_TABLE(platform, rsmu_id_table);

static struct platform_driver rsmu_driver = {
	.driver = {
		.name = "rsmu-cdev",
	},
	.probe = rsmu_probe,
	.remove =  rsmu_remove,
	.id_table = rsmu_id_table,
};

module_platform_driver(rsmu_driver);

MODULE_DESCRIPTION("Renesas SMU character device driver");
MODULE_LICENSE("GPL");
