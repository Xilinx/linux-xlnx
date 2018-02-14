// SPDX-License-Identifier: GPL-2.0+
/*
 * Xilinx Zynq MPSoC Power Management
 *
 *  Copyright (C) 2014-2018 Xilinx, Inc.
 *
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajanv@xilinx.com>
 */

#include <linux/compiler.h>
#include <linux/arm-smccc.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/soc/xilinx/zynqmp/pm.h>
#include <linux/soc/xilinx/zynqmp/firmware-debug.h>

#define DRIVER_NAME	"zynqmp_pm"

/**
 * struct zynqmp_pm_work_struct - Wrapper for struct work_struct
 * @callback_work:	Work structure
 * @args:		Callback arguments
 */
struct zynqmp_pm_work_struct {
	struct work_struct callback_work;
	u32 args[CB_ARG_CNT];
};

static struct zynqmp_pm_work_struct *zynqmp_pm_init_suspend_work;

static u32 pm_api_version;

enum pm_suspend_mode {
	PM_SUSPEND_MODE_STD,
	PM_SUSPEND_MODE_POWER_OFF,
};

#define PM_SUSPEND_MODE_FIRST	PM_SUSPEND_MODE_STD

static const char *const suspend_modes[] = {
	[PM_SUSPEND_MODE_STD] = "standard",
	[PM_SUSPEND_MODE_POWER_OFF] = "power-off",
};

static enum pm_suspend_mode suspend_mode = PM_SUSPEND_MODE_STD;

enum pm_api_cb_id {
	PM_INIT_SUSPEND_CB = 30,
	PM_ACKNOWLEDGE_CB,
	PM_NOTIFY_CB,
};

static irqreturn_t zynqmp_pm_isr(int irq, void *data)
{
	u32 payload[CB_PAYLOAD_SIZE];
	const struct zynqmp_eemi_ops *eemi_ops = get_eemi_ops();

	if (!eemi_ops || !eemi_ops->get_callback_data)
		return IRQ_NONE;

	eemi_ops->get_callback_data(payload);

	if (!payload[0])
		return IRQ_NONE;

	/* First element is callback API ID, others are callback arguments */
	if (payload[0] == PM_INIT_SUSPEND_CB) {

		if (work_pending(&zynqmp_pm_init_suspend_work->callback_work))
			goto done;

		/* Copy callback arguments into work's structure */
		memcpy(zynqmp_pm_init_suspend_work->args, &payload[1],
			sizeof(zynqmp_pm_init_suspend_work->args));

		queue_work(system_unbound_wq,
				&zynqmp_pm_init_suspend_work->callback_work);
	}

done:
	return IRQ_HANDLED;
}

#ifdef CONFIG_ZYNQMP_PM_API_DEBUGFS
/**
 * zynqmp_pm_argument_value - Extract argument value from a PM-API request
 * @arg:	Entered PM-API argument in string format
 *
 * Return:	Argument value in unsigned integer format on success
 *		0 otherwise
 */
static u64 zynqmp_pm_argument_value(char *arg)
{
	u64 value;

	if (!arg)
		return 0;

	if (!kstrtou64(arg, 0, &value))
		return value;

	return 0;
}

static struct dentry *zynqmp_pm_debugfs_dir;
static struct dentry *zynqmp_pm_debugfs_power;
static struct dentry *zynqmp_pm_debugfs_api_version;

/**
 * zynqmp_pm_debugfs_api_write - debugfs write function
 * @file:	User file structure
 * @ptr:	User entered PM-API string
 * @len:	Length of the userspace buffer
 * @off:	Offset within the file
 *
 * Return:	Number of bytes copied if PM-API request succeeds,
 *		the corresponding error code otherwise
 *
 * Used for triggering pm api functions by writing
 * echo <pm_api_id>    > /sys/kernel/debug/zynqmp_pm/power or
 * echo <pm_api_name>  > /sys/kernel/debug/zynqmp_pm/power
 */
static ssize_t zynqmp_pm_debugfs_api_write(struct file *file,
		    const char __user *ptr, size_t len, loff_t *off)
{
	char *kern_buff, *tmp_buff;
	char *pm_api_req;
	u32 pm_id = 0;
	u64 pm_api_arg[4];
	/* Return values from PM APIs calls */
	u32 pm_api_ret[4] = {0, 0, 0, 0};
	int ret;
	int i = 0;
	const struct zynqmp_eemi_ops *eemi_ops = get_eemi_ops();

	if (!eemi_ops)
		return -ENXIO;

	if (*off != 0 || len <= 0)
		return -EINVAL;

	kern_buff = kzalloc(len, GFP_KERNEL);
	if (!kern_buff)
		return -ENOMEM;
	tmp_buff = kern_buff;

	while (i < ARRAY_SIZE(pm_api_arg))
		pm_api_arg[i++] = 0;

	ret = strncpy_from_user(kern_buff, ptr, len);
	if (ret < 0) {
		ret = -EFAULT;
		goto err;
	}

	/* Read the API name from a user request */
	pm_api_req = strsep(&kern_buff, " ");

	if (strncasecmp(pm_api_req, "REQUEST_SUSPEND", 15) == 0)
		pm_id = REQUEST_SUSPEND;
	else if (strncasecmp(pm_api_req, "SELF_SUSPEND", 12) == 0)
		pm_id = SELF_SUSPEND;
	else if (strncasecmp(pm_api_req, "FORCE_POWERDOWN", 15) == 0)
		pm_id = FORCE_POWERDOWN;
	else if (strncasecmp(pm_api_req, "ABORT_SUSPEND", 13) == 0)
		pm_id = ABORT_SUSPEND;
	else if (strncasecmp(pm_api_req, "REQUEST_WAKEUP", 14) == 0)
		pm_id = REQUEST_WAKEUP;
	else if (strncasecmp(pm_api_req, "SET_WAKEUP_SOURCE", 17) == 0)
		pm_id = SET_WAKEUP_SOURCE;
	else if (strncasecmp(pm_api_req, "SYSTEM_SHUTDOWN", 15) == 0)
		pm_id = SYSTEM_SHUTDOWN;
	else if (strncasecmp(pm_api_req, "REQUEST_NODE", 12) == 0)
		pm_id = REQUEST_NODE;
	else if (strncasecmp(pm_api_req, "RELEASE_NODE", 12) == 0)
		pm_id = RELEASE_NODE;
	else if (strncasecmp(pm_api_req, "SET_REQUIREMENT", 15) == 0)
		pm_id = SET_REQUIREMENT;
	else if (strncasecmp(pm_api_req, "SET_MAX_LATENCY", 15) == 0)
		pm_id = SET_MAX_LATENCY;
	else if (strncasecmp(pm_api_req, "GET_API_VERSION", 15) == 0)
		pm_id = GET_API_VERSION;
	else if (strncasecmp(pm_api_req, "SET_CONFIGURATION", 17) == 0)
		pm_id = SET_CONFIGURATION;
	else if (strncasecmp(pm_api_req, "GET_NODE_STATUS", 15) == 0)
		pm_id = GET_NODE_STATUS;
	else if (strncasecmp(pm_api_req,
				"GET_OPERATING_CHARACTERISTIC", 28) == 0)
		pm_id = GET_OPERATING_CHARACTERISTIC;
	else if (strncasecmp(pm_api_req, "REGISTER_NOTIFIER", 17) == 0)
		pm_id = REGISTER_NOTIFIER;
	else if (strncasecmp(pm_api_req, "RESET_ASSERT", 12) == 0)
		pm_id = RESET_ASSERT;
	else if (strncasecmp(pm_api_req, "RESET_GET_STATUS", 16) == 0)
		pm_id = RESET_GET_STATUS;
	else if (strncasecmp(pm_api_req, "MMIO_READ", 9) == 0)
		pm_id = MMIO_READ;
	else if (strncasecmp(pm_api_req, "MMIO_WRITE", 10) == 0)
		pm_id = MMIO_WRITE;
	else if (strncasecmp(pm_api_req, "GET_CHIPID", 9) == 0)
		pm_id = GET_CHIPID;
	else if (strncasecmp(pm_api_req, "PINCTRL_GET_FUNCTION", 21) == 0)
		pm_id = PINCTRL_GET_FUNCTION;
	else if (strncasecmp(pm_api_req, "PINCTRL_SET_FUNCTION", 21) == 0)
		pm_id = PINCTRL_SET_FUNCTION;
	else if (strncasecmp(pm_api_req,
			     "PINCTRL_CONFIG_PARAM_GET", 25) == 0)
		pm_id = PINCTRL_CONFIG_PARAM_GET;
	else if (strncasecmp(pm_api_req,
			     "PINCTRL_CONFIG_PARAM_SET", 25) == 0)
		pm_id = PINCTRL_CONFIG_PARAM_SET;
	else if (strncasecmp(pm_api_req, "IOCTL", 6) == 0)
		pm_id = IOCTL;
	else if (strncasecmp(pm_api_req, "CLOCK_ENABLE", 12) == 0)
		pm_id = CLOCK_ENABLE;
	else if (strncasecmp(pm_api_req, "CLOCK_DISABLE", 13) == 0)
		pm_id = CLOCK_DISABLE;
	else if (strncasecmp(pm_api_req, "CLOCK_GETSTATE", 14) == 0)
		pm_id = CLOCK_GETSTATE;
	else if (strncasecmp(pm_api_req, "CLOCK_SETDIVIDER", 16) == 0)
		pm_id = CLOCK_SETDIVIDER;
	else if (strncasecmp(pm_api_req, "CLOCK_GETDIVIDER", 16) == 0)
		pm_id = CLOCK_GETDIVIDER;
	else if (strncasecmp(pm_api_req, "CLOCK_SETRATE", 13) == 0)
		pm_id = CLOCK_SETRATE;
	else if (strncasecmp(pm_api_req, "CLOCK_GETRATE", 13) == 0)
		pm_id = CLOCK_GETRATE;
	else if (strncasecmp(pm_api_req, "CLOCK_SETPARENT", 15) == 0)
		pm_id = CLOCK_SETPARENT;
	else if (strncasecmp(pm_api_req, "CLOCK_GETPARENT", 15) == 0)
		pm_id = CLOCK_GETPARENT;
	else if (strncasecmp(pm_api_req, "QUERY_DATA", 22) == 0)
		pm_id = QUERY_DATA;

	/* If no name was entered look for PM-API ID instead */
	else if (kstrtouint(pm_api_req, 10, &pm_id))
		ret = -EINVAL;

	/* Read node_id and arguments from the PM-API request */
	i = 0;
	pm_api_req = strsep(&kern_buff, " ");
	while ((i < ARRAY_SIZE(pm_api_arg)) && pm_api_req) {
		pm_api_arg[i++] = zynqmp_pm_argument_value(pm_api_req);
		pm_api_req = strsep(&kern_buff, " ");
	}

	switch (pm_id) {
	case GET_API_VERSION:
		pr_info("%s PM-API Version = %d.%d\n", __func__,
				pm_api_version >> 16, pm_api_version & 0xffff);
		break;
	case REQUEST_SUSPEND:
		ret = eemi_ops->request_suspend(pm_api_arg[0],
				pm_api_arg[1] ? pm_api_arg[1] :
						ZYNQMP_PM_REQUEST_ACK_NO,
				pm_api_arg[2] ? pm_api_arg[2] :
						ZYNQMP_PM_MAX_LATENCY, 0);
		break;
	case SELF_SUSPEND:
		ret = zynqmp_pm_self_suspend(pm_api_arg[0],
				pm_api_arg[1] ? pm_api_arg[1] :
						ZYNQMP_PM_MAX_LATENCY, 0);
		break;
	case FORCE_POWERDOWN:
		ret = eemi_ops->force_powerdown(pm_api_arg[0],
				pm_api_arg[1] ? pm_api_arg[1] :
						ZYNQMP_PM_REQUEST_ACK_NO);
		break;
	case ABORT_SUSPEND:
		ret = zynqmp_pm_abort_suspend(
			pm_api_arg[0] ? pm_api_arg[0] :
					ZYNQMP_PM_ABORT_REASON_UNKNOWN);
		break;
	case REQUEST_WAKEUP:
		ret = eemi_ops->request_wakeup(pm_api_arg[0],
				pm_api_arg[1], pm_api_arg[2],
				pm_api_arg[3] ? pm_api_arg[3] :
						ZYNQMP_PM_REQUEST_ACK_NO);
		break;
	case SET_WAKEUP_SOURCE:
		ret = eemi_ops->set_wakeup_source(pm_api_arg[0],
					pm_api_arg[1], pm_api_arg[2]);
		break;
	case SYSTEM_SHUTDOWN:
		ret = eemi_ops->system_shutdown(pm_api_arg[0], pm_api_arg[1]);
		break;
	case REQUEST_NODE:
		ret = eemi_ops->request_node(pm_api_arg[0],
			pm_api_arg[1] ? pm_api_arg[1] :
					ZYNQMP_PM_CAPABILITY_ACCESS,
			pm_api_arg[2] ? pm_api_arg[2] : 0,
			pm_api_arg[3] ? pm_api_arg[3] :
				ZYNQMP_PM_REQUEST_ACK_BLOCKING);
		break;
	case RELEASE_NODE:
		ret = eemi_ops->release_node(pm_api_arg[0]);
		break;
	case SET_REQUIREMENT:
		ret = eemi_ops->set_requirement(pm_api_arg[0],
			pm_api_arg[1] ? pm_api_arg[1] :
					ZYNQMP_PM_CAPABILITY_CONTEXT,
			pm_api_arg[2] ? pm_api_arg[2] : 0,
			pm_api_arg[3] ? pm_api_arg[3] :
				ZYNQMP_PM_REQUEST_ACK_BLOCKING);
		break;
	case SET_MAX_LATENCY:
		ret = eemi_ops->set_max_latency(pm_api_arg[0],
				pm_api_arg[1] ? pm_api_arg[1] :
						ZYNQMP_PM_MAX_LATENCY);
		break;
	case SET_CONFIGURATION:
		ret = eemi_ops->set_configuration(pm_api_arg[0]);
		break;
	case GET_NODE_STATUS:
		ret = eemi_ops->get_node_status(pm_api_arg[0],
						&pm_api_ret[0],
						&pm_api_ret[1],
						&pm_api_ret[2]);
		if (!ret)
			pr_info("GET_NODE_STATUS:\n\tNodeId: %llu\n\tStatus: %u\n\tRequirements: %u\n\tUsage: %u\n",
				pm_api_arg[0], pm_api_ret[0],
				pm_api_ret[1], pm_api_ret[2]);
		break;
	case GET_OPERATING_CHARACTERISTIC:
		ret = eemi_ops->get_operating_characteristic(pm_api_arg[0],
				pm_api_arg[1] ? pm_api_arg[1] :
				ZYNQMP_PM_OPERATING_CHARACTERISTIC_POWER,
				&pm_api_ret[0]);
		if (!ret)
			pr_info("GET_OPERATING_CHARACTERISTIC:\n\tNodeId: %llu\n\tType: %llu\n\tResult: %u\n",
				pm_api_arg[0], pm_api_arg[1], pm_api_ret[0]);
		break;
	case REGISTER_NOTIFIER:
		ret = zynqmp_pm_register_notifier(pm_api_arg[0],
				pm_api_arg[1] ? pm_api_arg[1] : 0,
				pm_api_arg[2] ? pm_api_arg[2] : 0,
				pm_api_arg[3] ? pm_api_arg[3] : 0);
		break;
	case RESET_ASSERT:
		ret = eemi_ops->reset_assert(pm_api_arg[0], pm_api_arg[1]);
		break;
	case RESET_GET_STATUS:
		ret = eemi_ops->reset_get_status(pm_api_arg[0], &pm_api_ret[0]);
		pr_info("%s Reset status: %u\n", __func__, pm_api_ret[0]);
		break;
	case MMIO_READ:
		ret = eemi_ops->mmio_read(pm_api_arg[0], &pm_api_ret[0]);
		pr_info("%s MMIO value: %#x\n", __func__, pm_api_ret[0]);
		break;
	case MMIO_WRITE:
		ret = eemi_ops->mmio_write(pm_api_arg[0],
				     pm_api_arg[1], pm_api_arg[2]);
		break;
	case GET_CHIPID:
		ret = eemi_ops->get_chipid(&pm_api_ret[0], &pm_api_ret[1]);
		pr_info("%s idcode: %#x, version:%#x\n",
			__func__, pm_api_ret[0], pm_api_ret[1]);
		break;
	case PINCTRL_GET_FUNCTION:
		ret = eemi_ops->pinctrl_get_function(pm_api_arg[0],
						     &pm_api_ret[0]);
		pr_info("%s Current set function for the pin: %u\n",
			__func__, pm_api_ret[0]);
		break;
	case PINCTRL_SET_FUNCTION:
		ret = eemi_ops->pinctrl_set_function(pm_api_arg[0],
						     pm_api_arg[1]);
		break;
	case PINCTRL_CONFIG_PARAM_GET:
		ret = eemi_ops->pinctrl_get_config(pm_api_arg[0], pm_api_arg[1],
						   &pm_api_ret[0]);
		pr_info("%s pin: %llu, param: %llu, value: %u\n",
			__func__, pm_api_arg[0], pm_api_arg[1],
			pm_api_ret[0]);
		break;
	case PINCTRL_CONFIG_PARAM_SET:
		ret = eemi_ops->pinctrl_set_config(pm_api_arg[0],
						   pm_api_arg[1],
						   pm_api_arg[2]);
		break;
	case IOCTL:
		ret = eemi_ops->ioctl(pm_api_arg[0], pm_api_arg[1],
				      pm_api_arg[2], pm_api_arg[3],
				      &pm_api_ret[0]);
		if (pm_api_arg[1] == IOCTL_GET_RPU_OPER_MODE ||
		    pm_api_arg[1] == IOCTL_GET_PLL_FRAC_MODE ||
		    pm_api_arg[1] == IOCTL_GET_PLL_FRAC_DATA)
			pr_info("%s Value: %u\n",
				__func__, pm_api_ret[1]);
		break;
	case CLOCK_ENABLE:
		ret = eemi_ops->clock_enable(pm_api_arg[0]);
		break;
	case CLOCK_DISABLE:
		ret = eemi_ops->clock_disable(pm_api_arg[0]);
		break;
	case CLOCK_GETSTATE:
		ret = eemi_ops->clock_getstate(pm_api_arg[0], &pm_api_ret[0]);
		pr_info("%s state: %u\n", __func__, pm_api_ret[0]);
		break;
	case CLOCK_SETDIVIDER:
		ret = eemi_ops->clock_setdivider(pm_api_arg[0], pm_api_arg[1]);
		break;
	case CLOCK_GETDIVIDER:
		ret = eemi_ops->clock_getdivider(pm_api_arg[0], &pm_api_ret[0]);
		pr_info("%s Divider Value: %d\n", __func__, pm_api_ret[0]);
		break;
	case CLOCK_SETRATE:
		ret = eemi_ops->clock_setrate(pm_api_arg[0], pm_api_arg[1]);
		break;
	case CLOCK_GETRATE:
		ret = eemi_ops->clock_getrate(pm_api_arg[0], &pm_api_ret[0]);
		pr_info("%s Rate Value: %u\n", __func__, pm_api_ret[0]);
		break;
	case CLOCK_SETPARENT:
		ret = eemi_ops->clock_setparent(pm_api_arg[0], pm_api_arg[1]);
		break;
	case CLOCK_GETPARENT:
		ret = eemi_ops->clock_getparent(pm_api_arg[0], &pm_api_ret[0]);
		pr_info("%s Parent Index: %u\n", __func__, pm_api_ret[0]);
		break;
	case QUERY_DATA:
	{
		struct zynqmp_pm_query_data qdata = {0};

		qdata.qid = pm_api_arg[0];
		qdata.arg1 = pm_api_arg[1];
		qdata.arg2 = pm_api_arg[2];
		qdata.arg3 = pm_api_arg[3];

		ret = eemi_ops->query_data(qdata, pm_api_ret);

		pr_info("%s: data[0] = 0x%08x\n", __func__, pm_api_ret[0]);
		pr_info("%s: data[1] = 0x%08x\n", __func__, pm_api_ret[1]);
		pr_info("%s: data[2] = 0x%08x\n", __func__, pm_api_ret[2]);
		pr_info("%s: data[3] = 0x%08x\n", __func__, pm_api_ret[3]);
		break;
	}
	default:
		pr_err("%s Unsupported PM-API request\n", __func__);
		ret = -EINVAL;
	}

 err:
	kfree(tmp_buff);
	if (ret)
		return ret;

	return len;
}

/**
 * zynqmp_pm_debugfs_api_version_read - debugfs read function
 * @file:	User file structure
 * @ptr:	Requested pm_api_version string
 * @len:	Length of the userspace buffer
 * @off:	Offset within the file
 *
 * Return:	Length of the version string on success
 *		-EFAULT otherwise
 *
 * Used to display the pm api version.
 * cat /sys/kernel/debug/zynqmp_pm/pm_api_version
 */
static ssize_t zynqmp_pm_debugfs_api_version_read(struct file *file,
			char __user *ptr, size_t len, loff_t *off)
{
	char *kern_buff;
	int ret;
	int kern_buff_len;

	if (len <= 0)
		return -EINVAL;

	if (*off != 0)
		return 0;

	kern_buff = kzalloc(len, GFP_KERNEL);
	if (!kern_buff)
		return -ENOMEM;

	sprintf(kern_buff, "PM-API Version = %d.%d\n",
				pm_api_version >> 16, pm_api_version & 0xffff);
	kern_buff_len = strlen(kern_buff) + 1;

	if (len > kern_buff_len)
		len = kern_buff_len;
	ret = copy_to_user(ptr, kern_buff, len);

	kfree(kern_buff);
	if (ret)
		return -EFAULT;

	*off = len + 1;
	return len;
}

/* Setup debugfs fops */
static const struct file_operations fops_zynqmp_pm_dbgfs = {
	.owner  =	THIS_MODULE,
	.write  =	zynqmp_pm_debugfs_api_write,
	.read   =	zynqmp_pm_debugfs_api_version_read,
};

/**
 * zynqmp_pm_api_debugfs_init - Initialize debugfs interface
 * @dev:        Pointer to device structure
 *
 * Return:      Returns 0 on success
 *		Corresponding error code otherwise
 */
static int zynqmp_pm_api_debugfs_init(struct device *dev)
{
	int err;

	/* Initialize debugfs interface */
	zynqmp_pm_debugfs_dir = debugfs_create_dir(DRIVER_NAME, NULL);
	if (!zynqmp_pm_debugfs_dir) {
		dev_err(dev, "debugfs_create_dir failed\n");
		return -ENODEV;
	}

	zynqmp_pm_debugfs_power =
		debugfs_create_file("power", 0220, zynqmp_pm_debugfs_dir, NULL,
				    &fops_zynqmp_pm_dbgfs);
	if (!zynqmp_pm_debugfs_power) {
		dev_err(dev, "debugfs_create_file power failed\n");
		err = -ENODEV;
		goto err_dbgfs;
	}

	zynqmp_pm_debugfs_api_version =
		debugfs_create_file("api_version", 0444, zynqmp_pm_debugfs_dir,
				    NULL, &fops_zynqmp_pm_dbgfs);
	if (!zynqmp_pm_debugfs_api_version) {
		dev_err(dev, "debugfs_create_file api_version failed\n");
		err = -ENODEV;
		goto err_dbgfs;
	}
	return 0;

 err_dbgfs:
	debugfs_remove_recursive(zynqmp_pm_debugfs_dir);
	zynqmp_pm_debugfs_dir = NULL;
	return err;
}

#else
static int zynqmp_pm_api_debugfs_init(struct device *dev)
{
	return 0;
}

#endif /* CONFIG_ZYNQMP_PM_API_DEBUGFS */

static const struct of_device_id pm_of_match[] = {
	{ .compatible = "xlnx,zynqmp-pm", },
	{ /* end of table */ },
};

MODULE_DEVICE_TABLE(of, pm_of_match);

/**
 * zynqmp_pm_init_suspend_work_fn - Initialize suspend
 * @work:	Pointer to work_struct
 *
 * Bottom-half of PM callback IRQ handler.
 */
static void zynqmp_pm_init_suspend_work_fn(struct work_struct *work)
{
	struct zynqmp_pm_work_struct *pm_work =
		container_of(work, struct zynqmp_pm_work_struct, callback_work);

	if (pm_work->args[0] == ZYNQMP_PM_SUSPEND_REASON_SYSTEM_SHUTDOWN) {
		orderly_poweroff(true);
	} else if (pm_work->args[0] ==
			ZYNQMP_PM_SUSPEND_REASON_POWER_UNIT_REQUEST) {
		pm_suspend(PM_SUSPEND_MEM);
	} else {
		pr_err("%s Unsupported InitSuspendCb reason code %d.\n"
				, __func__, pm_work->args[0]);
	}
}

static ssize_t suspend_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	char *s = buf;
	int md;

	for (md = PM_SUSPEND_MODE_FIRST; md < ARRAY_SIZE(suspend_modes); md++)
		if (suspend_modes[md]) {
			if (md == suspend_mode)
				s += sprintf(s, "[%s] ", suspend_modes[md]);
			else
				s += sprintf(s, "%s ", suspend_modes[md]);
		}

	/* Convert last space to newline */
	if (s != buf)
		*(s - 1) = '\n';
	return (s - buf);
}

static ssize_t suspend_mode_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int md, ret = -EINVAL;
	const struct zynqmp_eemi_ops *eemi_ops = get_eemi_ops();

	if (!eemi_ops || !eemi_ops->set_suspend_mode)
		return ret;

	for (md = PM_SUSPEND_MODE_FIRST; md < ARRAY_SIZE(suspend_modes); md++)
		if (suspend_modes[md] &&
		    sysfs_streq(suspend_modes[md], buf)) {
			ret = 0;
			break;
		}

	if (!ret && (md != suspend_mode)) {
		ret = eemi_ops->set_suspend_mode(md);
		if (likely(!ret))
			suspend_mode = md;
	}

	return ret ? ret : count;
}

static DEVICE_ATTR_RW(suspend_mode);

/**
 * zynqmp_pm_sysfs_init - Initialize PM driver sysfs interface
 * @dev:	Pointer to device structure
 *
 * Return:	0 on success, negative error code otherwise
 */
static int zynqmp_pm_sysfs_init(struct device *dev)
{
	return sysfs_create_file(&dev->kobj, &dev_attr_suspend_mode.attr);
}

/**
 * ggs_show - Show global general storage (ggs) sysfs attribute
 * @dev: Device structure
 * @attr: Device attribute structure
 * @buf: Requested available shutdown_scope attributes string
 * @reg: Register number
 *
 * Return:Number of bytes printed into the buffer.
 *
 * Helper function for viewing a ggs register value.
 *
 * User-space interface for viewing the content of the ggs0 register.
 * cat /sys/devices/platform/firmware/ggs0
 */
static ssize_t ggs_show(struct device *dev,
			struct device_attribute *attr,
			char *buf,
			u32 reg)
{
	u32 value;
	int len;
	int ret;

	ret = zynqmp_pm_mmio_read(GGS_BASEADDR + (reg << 2), &value);
	if (ret)
		return ret;

	len = sprintf(buf, "0x%x", (s32)value);
	if (len <= 0)
		return 0;

	strcat(buf, "\n");

	return strlen(buf);
}

/**
 * ggs_store - Store global general storage (ggs) sysfs attribute
 * @dev: Device structure
 * @attr: Device attribute structure
 * @buf: User entered shutdown_scope attribute string
 * @count: Size of buf
 * @reg: Register number
 *
 * Return: count argument if request succeeds, the corresponding
 * error code otherwise
 *
 * Helper function for storing a ggs register value.
 *
 * For example, the user-space interface for storing a value to the
 * ggs0 register:
 * echo 0xFFFFFFFF 0x1234ABCD > /sys/devices/platform/firmware/ggs0
 */
static ssize_t ggs_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf,
			 size_t count,
			 u32 reg)
{
	char *kern_buff;
	char *inbuf;
	char *tok;
	long mask;
	long value;
	int ret;

	if (!dev || !attr || !buf || !count || (reg >= GSS_NUM_REGS))
		return -EINVAL;

	kern_buff = kzalloc(count, GFP_KERNEL);
	if (!kern_buff)
		return -ENOMEM;

	ret = strlcpy(kern_buff, buf, count);
	if (ret < 0) {
		ret = -EFAULT;
		goto err;
	}

	inbuf = kern_buff;

	/* Read the write mask */
	tok = strsep(&inbuf, " ");
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}

	ret = kstrtol(tok, 16, &mask);
	if (ret) {
		ret = -EFAULT;
		goto err;
	}

	/* Read the write value */
	tok = strsep(&inbuf, " ");
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}

	ret = kstrtol(tok, 16, &value);
	if (ret) {
		ret = -EFAULT;
		goto err;
	}

	ret = zynqmp_pm_mmio_write(GGS_BASEADDR + (reg << 2),
				   (u32)mask, (u32)value);
	if (ret)
		ret = -EFAULT;

err:
	kfree(kern_buff);
	if (ret)
		return ret;

	return count;
}

/* GGS register show functions */
#define GGS0_SHOW(N) \
	ssize_t ggs##N##_show(struct device *dev, \
			 struct device_attribute *attr, \
			 char *buf) \
	{ \
		return ggs_show(dev, attr, buf, N); \
	}

static GGS0_SHOW(0);
static GGS0_SHOW(1);
static GGS0_SHOW(2);
static GGS0_SHOW(3);

/* GGS register store function */
#define GGS0_STORE(N) \
	ssize_t ggs##N##_store(struct device *dev, \
				   struct device_attribute *attr, \
				   const char *buf, \
				   size_t count) \
	{ \
		return ggs_store(dev, attr, buf, count, N); \
	}

static GGS0_STORE(0);
static GGS0_STORE(1);
static GGS0_STORE(2);
static GGS0_STORE(3);

/* GGS register device attributes */
static DEVICE_ATTR_RW(ggs0);
static DEVICE_ATTR_RW(ggs1);
static DEVICE_ATTR_RW(ggs2);
static DEVICE_ATTR_RW(ggs3);

#define CREATE_GGS_DEVICE(N) \
do { \
	if (device_create_file(&pdev->dev, &dev_attr_ggs##N)) \
		dev_err(&pdev->dev, "unable to create ggs%d attribute\n", N); \
} while (0)

/**
 * pggs_show - Show persistent global general storage (pggs) sysfs attribute
 * @dev: Device structure
 * @attr: Device attribute structure
 * @buf: Requested available shutdown_scope attributes string
 * @reg: Register number
 *
 * Return:Number of bytes printed into the buffer.
 *
 * Helper function for viewing a pggs register value.
 */
static ssize_t pggs_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf,
			 u32 reg)
{
	u32 value;
	int len;
	int ret;

	ret = zynqmp_pm_mmio_read(PGGS_BASEADDR + (reg << 2), &value);
	if (ret)
		return ret;

	len = sprintf(buf, "0x%x", (s32)value);
	if (len <= 0)
		return 0;

	strcat(buf, "\n");

	return strlen(buf);
}

/**
 * pggs_store - Store persistent global general storage (pggs) sysfs attribute
 * @dev: Device structure
 * @attr: Device attribute structure
 * @buf: User entered shutdown_scope attribute string
 * @count: Size of buf
 * @reg: Register number
 *
 * Return: count argument if request succeeds, the corresponding
 * error code otherwise
 *
 * Helper function for storing a pggs register value.
 */
static ssize_t pggs_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf,
			  size_t count,
			  u32 reg)
{
	char *kern_buff;
	char *inbuf;
	char *tok;
	long mask;
	long value;
	int ret;

	if (!dev || !attr || !buf || !count || (reg >= PGSS_NUM_REGS))
		return -EINVAL;

	kern_buff = kzalloc(count, GFP_KERNEL);
	if (!kern_buff)
		return -ENOMEM;

	ret = strlcpy(kern_buff, buf, count);
	if (ret < 0) {
		ret = -EFAULT;
		goto err;
	}

	inbuf = kern_buff;

	/* Read the write mask */
	tok = strsep(&inbuf, " ");
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}

	ret = kstrtol(tok, 16, &mask);
	if (ret) {
		ret = -EFAULT;
		goto err;
	}

	/* Read the write value */
	tok = strsep(&inbuf, " ");
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}

	ret = kstrtol(tok, 16, &value);
	if (ret) {
		ret = -EFAULT;
		goto err;
	}

	ret = zynqmp_pm_mmio_write(PGGS_BASEADDR + (reg << 2),
				   (u32)mask, (u32)value);
	if (ret)
		ret = -EFAULT;

err:
	kfree(kern_buff);
	if (ret)
		return ret;

	return count;
}

#define PGGS0_SHOW(N) \
	ssize_t pggs##N##_show(struct device *dev, \
			 struct device_attribute *attr, \
			 char *buf) \
	{ \
		return pggs_show(dev, attr, buf, N); \
	}

/* PGGS register show functions */
static PGGS0_SHOW(0);
static PGGS0_SHOW(1);
static PGGS0_SHOW(2);
static PGGS0_SHOW(3);

#define PGGS0_STORE(N) \
	ssize_t pggs##N##_store(struct device *dev, \
				   struct device_attribute *attr, \
				   const char *buf, \
				   size_t count) \
	{ \
		return pggs_store(dev, attr, buf, count, N); \
	}

/* PGGS register store functions */
static PGGS0_STORE(0);
static PGGS0_STORE(1);
static PGGS0_STORE(2);
static PGGS0_STORE(3);

/* PGGS register device attributes */
static DEVICE_ATTR_RW(pggs0);
static DEVICE_ATTR_RW(pggs1);
static DEVICE_ATTR_RW(pggs2);
static DEVICE_ATTR_RW(pggs3);

#define CREATE_PGGS_DEVICE(N) \
do { \
	if (device_create_file(&pdev->dev, &dev_attr_pggs##N)) \
		dev_err(&pdev->dev, "unable to create pggs%d attribute\n", N); \
} while (0)

/**
 * zynqmp_pm_probe - Probe existence of the PMU Firmware
 *			and initialize debugfs interface
 *
 * @pdev:	Pointer to the platform_device structure
 *
 * Return:	Returns 0 on success
 *		Negative error code otherwise
 */
static int zynqmp_pm_probe(struct platform_device *pdev)
{
	int ret, irq;
	const struct zynqmp_eemi_ops *eemi_ops = get_eemi_ops();

	if (!eemi_ops || !eemi_ops->get_api_version)
		return -ENXIO;

	eemi_ops->get_api_version(&pm_api_version);

	/* Check PM API version number */
	if (pm_api_version < ZYNQMP_PM_VERSION)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return -ENXIO;

	ret = devm_request_irq(&pdev->dev, irq, zynqmp_pm_isr, IRQF_SHARED,
			       DRIVER_NAME, pdev);
	if (ret) {
		dev_err(&pdev->dev, "request_irq '%d' failed with %d\n",
			irq, ret);
		return ret;
	}

	zynqmp_pm_init_suspend_work = devm_kzalloc(&pdev->dev,
			sizeof(struct zynqmp_pm_work_struct), GFP_KERNEL);
	if (!zynqmp_pm_init_suspend_work)
		return -ENOMEM;

	INIT_WORK(&zynqmp_pm_init_suspend_work->callback_work,
		zynqmp_pm_init_suspend_work_fn);

	ret = zynqmp_pm_sysfs_init(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "unable to initialize sysfs interface\n");
		return ret;
	}

	dev_info(&pdev->dev, "Power management API v%d.%d\n",
		pm_api_version >> 16, pm_api_version & 0xFFFF);

	zynqmp_pm_api_debugfs_init(&pdev->dev);


	/* Create Global General Storage register. */
	CREATE_GGS_DEVICE(0);
	CREATE_GGS_DEVICE(1);
	CREATE_GGS_DEVICE(2);
	CREATE_GGS_DEVICE(3);

	/* Create Persistent Global General Storage register. */
	CREATE_PGGS_DEVICE(0);
	CREATE_PGGS_DEVICE(1);
	CREATE_PGGS_DEVICE(2);
	CREATE_PGGS_DEVICE(3);

	return 0;
}

static struct platform_driver zynqmp_pm_platform_driver = {
	.probe   = zynqmp_pm_probe,
	.driver  = {
			.name             = DRIVER_NAME,
			.of_match_table   = pm_of_match,
		   },
};
builtin_platform_driver(zynqmp_pm_platform_driver);

/**
 * zynqmp_pm_init - Notify PM firmware that initialization is completed
 *
 * Return:	Status returned from the PM firmware
 */
static int __init zynqmp_pm_init(void)
{
	const struct zynqmp_eemi_ops *eemi_ops = get_eemi_ops();

	if (!eemi_ops || !eemi_ops->init_finalize)
		return -ENXIO;

	return eemi_ops->init_finalize();
}

late_initcall_sync(zynqmp_pm_init);
