/*
 * Xilinx Zynq MPSoC Power Management
 *
 *  Copyright (C) 2014-2015 Xilinx, Inc.
 *
 *  Davorin Mista <davorin.mista@aggios.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/compiler.h>
#include <linux/arm-smccc.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/suspend.h>
#include <linux/soc/xilinx/zynqmp/pm.h>

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

/* PM-APIs for suspending of APU */

#ifdef CONFIG_ZYNQMP_PM_API_DEBUGFS

/**
 * zynqmp_pm_self_suspend - PM call for master to suspend itself
 * @node:	Node ID of the master or subsystem
 * @latency:	Requested maximum wakeup latency (not supported)
 * @state:	Requested state (not supported)
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_self_suspend(const u32 node,
				  const u32 latency,
				  const u32 state)
{
	return invoke_pm_fn(SELF_SUSPEND, node, latency, state, 0, NULL);
}

/**
 * zynqmp_pm_abort_suspend - PM call to announce that a prior suspend request
 *				is to be aborted.
 * @reason:	Reason for the abort
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_abort_suspend(const enum zynqmp_pm_abort_reason reason)
{
	return invoke_pm_fn(ABORT_SUSPEND, reason, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_register_notifier - Register the PU to be notified of PM events
 * @node:	Node ID of the slave
 * @event:	The event to be notified about
 * @wake:	Wake up on event
 * @enable:	Enable or disable the notifier
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_register_notifier(const u32 node, const u32 event,
				       const u32 wake, const u32 enable)
{
	return invoke_pm_fn(REGISTER_NOTIFIER, node, event,
						wake, enable, NULL);
}
#endif

/**
 * zynqmp_pm_request_suspend - PM call to request for another PU or subsystem to
 *					be suspended gracefully.
 * @node:	Node ID of the targeted PU or subsystem
 * @ack:	Flag to specify whether acknowledge is requested
 * @latency:	Requested wakeup latency (not supported)
 * @state:	Requested state (not supported)
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_request_suspend(const u32 node,
				      const enum zynqmp_pm_request_ack ack,
				      const u32 latency,
				      const u32 state)
{
	return invoke_pm_fn(REQUEST_SUSPEND, node, ack,
						latency, state, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_request_suspend);

/**
 * zynqmp_pm_force_powerdown - PM call to request for another PU or subsystem to
 *				be powered down forcefully
 * @target:	Node ID of the targeted PU or subsystem
 * @ack:	Flag to specify whether acknowledge is requested
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_force_powerdown(const u32 target,
					  const enum zynqmp_pm_request_ack ack)
{
	return invoke_pm_fn(FORCE_POWERDOWN, target, ack, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_force_powerdown);

/**
 * zynqmp_pm_request_wakeup - PM call for to wake up selected master or subsystem
 * @node:	Node ID of the master or subsystem
 * @set_addr:	Specifies whether the address argument is relevant
 * @address:	Address from which to resume when woken up
 * @ack:	Flag to specify whether acknowledge requested
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_request_wakeup(const u32 node,
				     const bool set_addr,
				     const u64 address,
				     const enum zynqmp_pm_request_ack ack)
{
	/* set_addr flag is encoded into 1st bit of address */
	return invoke_pm_fn(REQUEST_WAKEUP, node, address | set_addr,
				address >> 32, ack, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_request_wakeup);

/**
 * zynqmp_pm_set_wakeup_source - PM call to specify the wakeup source
 *					while suspended
 * @target:	Node ID of the targeted PU or subsystem
 * @wakeup_node:Node ID of the wakeup peripheral
 * @enable:	Enable or disable the specified peripheral as wake source
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_set_wakeup_source(const u32 target,
					    const u32 wakeup_node,
					    const u32 enable)
{
	return invoke_pm_fn(SET_WAKEUP_SOURCE, target,
					wakeup_node, enable, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_wakeup_source);

/**
 * zynqmp_pm_system_shutdown - PM call to request a system shutdown or restart
 * @type:	Shutdown or restart? 0 for shutdown, 1 for restart
 * @subtype:	Specifies which system should be restarted or shut down
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_system_shutdown(const u32 type, const u32 subtype)
{
	return invoke_pm_fn(SYSTEM_SHUTDOWN, type, subtype, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_system_shutdown);

/* API functions for managing PM Slaves */

/**
 * zynqmp_pm_request_node - PM call to request a node with specific capabilities
 * @node:		Node ID of the slave
 * @capabilities:	Requested capabilities of the slave
 * @qos:		Quality of service (not supported)
 * @ack:		Flag to specify whether acknowledge is requested
 *
 * Return:		Returns status, either success or error+reason
 */
int zynqmp_pm_request_node(const u32 node,
				   const u32 capabilities,
				   const u32 qos,
				   const enum zynqmp_pm_request_ack ack)
{
	return invoke_pm_fn(REQUEST_NODE, node, capabilities,
						qos, ack, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_request_node);

/**
 * zynqmp_pm_release_node - PM call to release a node
 * @node:	Node ID of the slave
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_release_node(const u32 node)
{
	return invoke_pm_fn(RELEASE_NODE, node, 0, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_release_node);

/**
 * zynqmp_pm_set_requirement - PM call to set requirement for PM slaves
 * @node:		Node ID of the slave
 * @capabilities:	Requested capabilities of the slave
 * @qos:		Quality of service (not supported)
 * @ack:		Flag to specify whether acknowledge is requested
 *
 * This API function is to be used for slaves a PU already has requested
 *
 * Return:		Returns status, either success or error+reason
 */
int zynqmp_pm_set_requirement(const u32 node,
				const u32 capabilities,
				const u32 qos,
				const enum zynqmp_pm_request_ack ack)
{
	return invoke_pm_fn(SET_REQUIREMENT, node, capabilities,
						qos, ack, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_requirement);

/**
 * zynqmp_pm_set_max_latency - PM call to set wakeup latency requirements
 * @node:	Node ID of the slave
 * @latency:	Requested maximum wakeup latency
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_set_max_latency(const u32 node,
					  const u32 latency)
{
	return invoke_pm_fn(SET_MAX_LATENCY, node,
					latency, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_max_latency);

/* Miscellaneous API functions */

/**
 * zynqmp_pm_set_configuration - PM call to set system configuration
 * @physical_addr:	Physical 32-bit address of data structure in memory
 *
 * Return:		Returns status, either success or error+reason
 */
int zynqmp_pm_set_configuration(const u32 physical_addr)
{
	return invoke_pm_fn(SET_CONFIGURATION, physical_addr, 0, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_configuration);

/**
 * zynqmp_pm_get_node_status - PM call to request a node's current power state
 * @node:		ID of the component or sub-system in question
 * @status:		Current operating state of the requested node
 * @requirements:	Current requirements asserted on the node,
 *			used for slave nodes only.
 * @usage:		Usage information, used for slave nodes only:
 *			0 - No master is currently using the node
 *			1 - Only requesting master is currently using the node
 *			2 - Only other masters are currently using the node
 *			3 - Both the current and at least one other master
 *			is currently using the node
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_get_node_status(const u32 node,
				u32 *const status,
				u32 *const requirements,
				u32 *const usage)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];

	if (!status)
		return -EINVAL;

	invoke_pm_fn(GET_NODE_STATUS, node, 0, 0, 0, ret_payload);
	if (ret_payload[0] == XST_PM_SUCCESS) {
		*status = ret_payload[1];
		if (requirements)
			*requirements = ret_payload[2];
		if (usage)
			*usage = ret_payload[3];
	}

	return zynqmp_pm_ret_code((enum pm_ret_status)ret_payload[0]);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_node_status);

/**
 * zynqmp_pm_get_operating_characteristic - PM call to request operating
 *						characteristic information
 * @node:	Node ID of the slave
 * @type:	Type of the operating characteristic requested
 * @result:	Used to return the requsted operating characteristic
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_get_operating_characteristic(const u32 node,
					const enum zynqmp_pm_opchar_type type,
					u32 *const result)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];

	if (!result)
		return -EINVAL;

	invoke_pm_fn(GET_OPERATING_CHARACTERISTIC,
			node, type, 0, 0, ret_payload);
	if (ret_payload[0] == XST_PM_SUCCESS)
		*result = ret_payload[1];

	return zynqmp_pm_ret_code((enum pm_ret_status)ret_payload[0]);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_operating_characteristic);

/* Direct-Control API functions */

static void zynqmp_pm_get_callback_data(u32 *buf)
{
	invoke_pm_fn(GET_CALLBACK_DATA, 0, 0, 0, 0, buf);
}

static irqreturn_t zynqmp_pm_isr(int irq, void *data)
{
	u32 payload[CB_PAYLOAD_SIZE];

	zynqmp_pm_get_callback_data(payload);

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
	u32 pm_api_ret[3] = {0, 0, 0};
	int ret;
	int i = 0;

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

	/* Read the API name from an user request */
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
		ret = zynqmp_pm_request_suspend(pm_api_arg[0],
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
		ret = zynqmp_pm_force_powerdown(pm_api_arg[0],
				pm_api_arg[1] ? pm_api_arg[1] :
						ZYNQMP_PM_REQUEST_ACK_NO);
		break;
	case ABORT_SUSPEND:
		ret = zynqmp_pm_abort_suspend(
			pm_api_arg[0] ? pm_api_arg[0] :
					ZYNQMP_PM_ABORT_REASON_UNKNOWN);
		break;
	case REQUEST_WAKEUP:
		ret = zynqmp_pm_request_wakeup(pm_api_arg[0],
				pm_api_arg[1], pm_api_arg[2],
				pm_api_arg[3] ? pm_api_arg[3] :
						ZYNQMP_PM_REQUEST_ACK_NO);
		break;
	case SET_WAKEUP_SOURCE:
		ret = zynqmp_pm_set_wakeup_source(pm_api_arg[0],
					pm_api_arg[1], pm_api_arg[2]);
		break;
	case SYSTEM_SHUTDOWN:
		ret = zynqmp_pm_system_shutdown(pm_api_arg[0], pm_api_arg[1]);
		break;
	case REQUEST_NODE:
		ret = zynqmp_pm_request_node(pm_api_arg[0],
			pm_api_arg[1] ? pm_api_arg[1] :
					ZYNQMP_PM_CAPABILITY_ACCESS,
			pm_api_arg[2] ? pm_api_arg[2] : 0,
			pm_api_arg[3] ? pm_api_arg[3] :
				ZYNQMP_PM_REQUEST_ACK_BLOCKING);
		break;
	case RELEASE_NODE:
		ret = zynqmp_pm_release_node(pm_api_arg[0]);
		break;
	case SET_REQUIREMENT:
		ret = zynqmp_pm_set_requirement(pm_api_arg[0],
			pm_api_arg[1] ? pm_api_arg[1] :
					ZYNQMP_PM_CAPABILITY_CONTEXT,
			pm_api_arg[2] ? pm_api_arg[2] : 0,
			pm_api_arg[3] ? pm_api_arg[3] :
				ZYNQMP_PM_REQUEST_ACK_BLOCKING);
		break;
	case SET_MAX_LATENCY:
		ret = zynqmp_pm_set_max_latency(pm_api_arg[0],
				pm_api_arg[1] ? pm_api_arg[1] :
						ZYNQMP_PM_MAX_LATENCY);
		break;
	case SET_CONFIGURATION:
		ret = zynqmp_pm_set_configuration(pm_api_arg[0]);
		break;
	case GET_NODE_STATUS:
		ret = zynqmp_pm_get_node_status(pm_api_arg[0],
						&pm_api_ret[0],
						&pm_api_ret[1],
						&pm_api_ret[2]);
		if (!ret)
			pr_info("GET_NODE_STATUS:\n\tNodeId: %llu\n\tStatus: %u\n\tRequirements: %u\n\tUsage: %u\n",
				pm_api_arg[0], pm_api_ret[0],
				pm_api_ret[1], pm_api_ret[2]);
		break;
	case GET_OPERATING_CHARACTERISTIC:
		ret = zynqmp_pm_get_operating_characteristic(pm_api_arg[0],
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
		ret = zynqmp_pm_reset_assert(pm_api_arg[0], pm_api_arg[1]);
		break;
	case RESET_GET_STATUS:
		ret = zynqmp_pm_reset_get_status(pm_api_arg[0], &pm_api_ret[0]);
		pr_info("%s Reset status: %u\n", __func__, pm_api_ret[0]);
		break;
	case MMIO_READ:
		ret = zynqmp_pm_mmio_read(pm_api_arg[0], &pm_api_ret[0]);
		pr_info("%s MMIO value: %#x\n", __func__, pm_api_ret[0]);
		break;
	case MMIO_WRITE:
		ret = zynqmp_pm_mmio_write(pm_api_arg[0],
				     pm_api_arg[1], pm_api_arg[2]);
		break;
	case GET_CHIPID:
		ret = zynqmp_pm_get_chipid(&pm_api_ret[0], &pm_api_ret[1]);
		pr_info("%s idcode: %#x, version:%#x\n",
			__func__, pm_api_ret[0], pm_api_ret[1]);
		break;
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
		debugfs_create_file("power", S_IWUSR | S_IWGRP | S_IWOTH,
					zynqmp_pm_debugfs_dir, NULL,
					&fops_zynqmp_pm_dbgfs);
	if (!zynqmp_pm_debugfs_power) {
		dev_err(dev, "debugfs_create_file power failed\n");
		err = -ENODEV;
		goto err_dbgfs;
	}

	zynqmp_pm_debugfs_api_version =
		debugfs_create_file("api_version", S_IRUSR | S_IRGRP | S_IROTH,
					zynqmp_pm_debugfs_dir, NULL,
					&fops_zynqmp_pm_dbgfs);
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
	pm_suspend(PM_SUSPEND_MEM);
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

	for (md = PM_SUSPEND_MODE_FIRST; md < ARRAY_SIZE(suspend_modes); md++)
		if (suspend_modes[md] &&
		    sysfs_streq(suspend_modes[md], buf)) {
			ret = 0;
			break;
		}

	if (!ret && (md != suspend_mode)) {
		ret = invoke_pm_fn(SET_SUSPEND_MODE, md, 0, 0, 0, NULL);
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

/* GGS regsiter device attributes */
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
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;

	zynqmp_pm_get_api_version(&pm_api_version);

	/* Check PM API version number */
	if (pm_api_version != ZYNQMP_PM_VERSION)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		return -ENXIO;
	}

	ret = request_irq(irq, zynqmp_pm_isr, 0, DRIVER_NAME, pdev);
	if (ret) {
		dev_err(&pdev->dev, "request_irq '%d' failed with %d\n",
			irq, ret);
		return ret;
	}

	zynqmp_pm_init_suspend_work = devm_kzalloc(&pdev->dev,
			sizeof(struct zynqmp_pm_work_struct), GFP_KERNEL);
	if (!zynqmp_pm_init_suspend_work) {
		ret = -ENOMEM;
		goto error;
	}

	INIT_WORK(&zynqmp_pm_init_suspend_work->callback_work,
		zynqmp_pm_init_suspend_work_fn);

	ret = zynqmp_pm_sysfs_init(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "unable to initialize sysfs interface\n");
		goto error;
	}

	dev_info(&pdev->dev, "Power management API v%d.%d\n",
		ZYNQMP_PM_VERSION_MAJOR, ZYNQMP_PM_VERSION_MINOR);

	zynqmp_pm_api_debugfs_init(&pdev->dev);

	pinctrl = devm_pinctrl_get(&pdev->dev);
	if (!IS_ERR(pinctrl)) {
		pins_default = pinctrl_lookup_state(pinctrl,
						    PINCTRL_STATE_DEFAULT);
		if (IS_ERR(pins_default)) {
			dev_err(&pdev->dev, "Missing default pinctrl config\n");
			return IS_ERR(pins_default);
		}

		pinctrl_select_state(pinctrl, pins_default);
	}

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

error:
	free_irq(irq, 0);
	return ret;
}

static struct platform_driver zynqmp_pm_platform_driver = {
	.probe   = zynqmp_pm_probe,
	.driver  = {
			.name             = DRIVER_NAME,
			.of_match_table   = pm_of_match,
		   },
};
builtin_platform_driver(zynqmp_pm_platform_driver);

#ifdef CONFIG_PM
/**
 * zynqmp_pm_init_finalize - Notify PM firmware that initialization is completed
 *
 * Return:	Status returned from the PM firmware
 */
static int __init zynqmp_pm_init_finalize(void)
{
	return invoke_pm_fn(PM_INIT_FINALIZE, 0, 0, 0, 0, NULL);
}

late_initcall_sync(zynqmp_pm_init_finalize);
#endif
