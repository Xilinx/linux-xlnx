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
#include <linux/of.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/soc/xilinx/zynqmp/pm.h>

/* SMC SIP service Call Function Identifier Prefix */
#define PM_SIP_SVC	0xC2000000

/* Number of 32bits values in payload */
#define PAYLOAD_ARG_CNT	5U

#define DRIVER_NAME	"zynqmp_pm"

static u32 pm_api_version;

enum pm_api_id {
	/* Miscellaneous API functions: */
	GET_API_VERSION = 1,
	SET_CONFIGURATION,
	GET_NODE_STATUS,
	GET_OPERATING_CHARACTERISTIC,
	REGISTER_NOTIFIER,
	/* API for suspending of PUs: */
	REQUEST_SUSPEND,
	SELF_SUSPEND,
	FORCE_POWERDOWN,
	ABORT_SUSPEND,
	REQUEST_WAKEUP,
	SET_WAKEUP_SOURCE,
	SYSTEM_SHUTDOWN,
	/* API for managing PM slaves: */
	REQUEST_NODE,
	RELEASE_NODE,
	SET_REQUIREMENT,
	SET_MAX_LATENCY,
	/* Direct control API functions: */
	RESET_ASSERT,
	RESET_GET_STATUS,
	MMIO_WRITE,
	MMIO_READ,
};

/* PMU-FW return status codes */
enum pm_ret_status {
	XST_PM_SUCCESS = 0,
	XST_PM_INTERNAL	= 2000,
	XST_PM_CONFLICT,
	XST_PM_NO_ACCESS,
	XST_PM_INVALID_NODE,
	XST_PM_DOUBLE_REQ,
	XST_PM_ABORT_SUSPEND,
};

/**
 * zynqmp_pm_ret_code - Convert PMU-FW error codes to Linux error codes
 * @ret_status:		PMUFW return code
 *
 * Return:		corresponding Linux error code
 */
static int zynqmp_pm_ret_code(u32 ret_status)
{
	switch (ret_status) {
	case XST_PM_SUCCESS:
		return 0;
	case XST_PM_NO_ACCESS:
		return -EACCES;
	case XST_PM_ABORT_SUSPEND:
		return -ECANCELED;
	case XST_PM_INTERNAL:
	case XST_PM_CONFLICT:
	case XST_PM_INVALID_NODE:
	case XST_PM_DOUBLE_REQ:
	default:
		return -EINVAL;
	}
}

/*
 * PM function call wrapper
 * Invoke do_fw_call_smc or do_fw_call_hvc, depending on the configuration
 */
static int (*do_fw_call)(u64, u64, u64, u32 *ret_payload);

/**
 * do_fw_call_smc - Call system-level power management layer (SMC)
 * @arg0:		Argument 0 to SMC call
 * @arg1:		Argument 1 to SMC call
 * @arg2:		Argument 2 to SMC call
 * @ret_payload:	Returned value array
 *
 * Return:		Returns status, either success or error+reason
 *
 * Invoke power management function via SMC call (no hypervisor present)
 */
static noinline int do_fw_call_smc(u64 arg0, u64 arg1, u64 arg2,
						u32 *ret_payload)
{
	/*
	 * This firmware calling code may be moved to an assembly file
	 * so as to compile it successfully with GCC 5, as per the
	 * reference git commit f5e0a12ca2d939e47995f73428d9bf1ad372b289
	 */
	asm volatile(
		__asmeq("%0", "x0")
		__asmeq("%1", "x1")
		__asmeq("%2", "x2")
		"smc	#0\n"
		: "+r" (arg0), "+r" (arg1), "+r" (arg2)
		: /* no input only */
		: "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12",
		  "x13", "x14", "x15", "x16", "x17"
		);

	if (ret_payload != NULL) {
		ret_payload[0] = (u32)arg0;
		ret_payload[1] = (u32)(arg0 >> 32);
		ret_payload[2] = (u32)arg1;
		ret_payload[3] = (u32)(arg1 >> 32);
		ret_payload[4] = (u32)arg2;
	}
	return zynqmp_pm_ret_code((enum pm_ret_status)arg0);
}

/**
 * do_fw_call_hvc - Call system-level power management layer (HVC)
 * @arg0:		Argument 0 to HVC call
 * @arg1:		Argument 1 to HVC call
 * @arg2:		Argument 2 to HVC call
 * @ret_payload:	Returned value array
 *
 * Return:		Returns status, either success or error+reason
 *
 * Invoke power management function via HVC
 * HVC-based for communication through hypervisor
 * (no direct communication with ATF)
 */
static noinline int do_fw_call_hvc(u64 arg0, u64 arg1, u64 arg2,
						u32 *ret_payload)
{
	/*
	 * This firmware calling code may be moved to an assembly file
	 * so as to compile it successfully with GCC 5, as per the
	 * reference git commit f5e0a12ca2d939e47995f73428d9bf1ad372b289
	 */
	asm volatile(
		__asmeq("%0", "x0")
		__asmeq("%1", "x1")
		__asmeq("%2", "x2")
		"hvc	#0\n"
		: "+r" (arg0), "+r" (arg1), "+r" (arg2)
		: /* no input only */
		: "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12",
		  "x13", "x14", "x15", "x16", "x17"
		);

	if (ret_payload != NULL) {
		ret_payload[0] = (u32)arg0;
		ret_payload[1] = (u32)(arg0 >> 32);
		ret_payload[2] = (u32)arg1;
		ret_payload[3] = (u32)(arg1 >> 32);
		ret_payload[4] = (u32)arg2;
	}

	return zynqmp_pm_ret_code((enum pm_ret_status)arg0);
}

/**
 * invoke_pm_fn - Invoke the system-level power management layer caller
 *			function depending on the configuration
 * @pm_api_id:         Requested PM-API call
 * @arg0:              Argument 0 to requested PM-API call
 * @arg1:              Argument 1 to requested PM-API call
 * @arg2:              Argument 2 to requested PM-API call
 * @arg3:              Argument 3 to requested PM-API call
 * @ret_payload:       Returned value array
 *
 * Return:             Returns status, either success or error+reason
 *
 * Invoke power management function for SMC or HVC call, depending on
 * configuration
 * Following SMC Calling Convention (SMCCC) for SMC64:
 * Pm Function Identifier,
 * PM_SIP_SVC + PM_API_ID =
 *     ((SMC_TYPE_FAST << FUNCID_TYPE_SHIFT)
 *     ((SMC_64) << FUNCID_CC_SHIFT)
 *     ((SIP_START) << FUNCID_OEN_SHIFT)
 *     ((PM_API_ID) & FUNCID_NUM_MASK))
 *
 * PM_SIP_SVC  - Registered ZynqMP SIP Service Call
 * PM_API_ID   - Power Management API ID
 */
static int invoke_pm_fn(u32 pm_api_id, u32 arg0, u32 arg1, u32 arg2, u32 arg3,
							u32 *ret_payload)
{
	/*
	 * Added SIP service call Function Identifier
	 * Make sure to stay in x0 register
	 */
	u64 smc_arg[4];

	smc_arg[0] = PM_SIP_SVC | pm_api_id;
	smc_arg[1] = ((u64)arg1 << 32) | arg0;
	smc_arg[2] = ((u64)arg3 << 32) | arg2;

	return do_fw_call(smc_arg[0], smc_arg[1], smc_arg[2], ret_payload);
}

/* PM-APIs for suspending of APU */

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
 * zynqmp_pm_request_wakeup - PM call for to wake up selected master or subsystem
 * @node:	Node ID of the master or subsystem
 * @ack:	Flag to specify whether acknowledge requested
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_request_wakeup(const u32 node,
				     const enum zynqmp_pm_request_ack ack)
{
	return invoke_pm_fn(REQUEST_WAKEUP, node, ack, 0, 0, NULL);
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
 * @restart:	Shutdown or restart? 0 for shutdown, 1 for restart
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_system_shutdown(const u32 restart)
{
	return invoke_pm_fn(SYSTEM_SHUTDOWN, restart, 0, 0, 0, NULL);
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
 * @latency:	Requested maximum wakeup latency
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_release_node(const u32 node, const u32 latency)
{
	return invoke_pm_fn(RELEASE_NODE, node, latency, 0, 0, NULL);
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
 * zynqmp_pm_get_api_version - Get version number of PMU PM firmware
 * @version:	Returned version value
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_get_api_version(u32 *version)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];

	if (version == NULL)
		return zynqmp_pm_ret_code(XST_PM_CONFLICT);

	/* Check is PM API version already verified */
	if (pm_api_version > 0) {
		*version = pm_api_version;
		return XST_PM_SUCCESS;
	}
	invoke_pm_fn(GET_API_VERSION, 0, 0, 0, 0, ret_payload);
	*version = ret_payload[1];

	return zynqmp_pm_ret_code((enum pm_ret_status)ret_payload[0]);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_api_version);

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
 * @node:	Node ID of the slave
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_get_node_status(const u32 node)
{
	return invoke_pm_fn(GET_NODE_STATUS, node, 0, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_node_status);

/**
 * zynqmp_pm_get_operating_characteristic - PM call to request operating
 *						characteristic information
 * @node:	Node ID of the slave
 * @type:	Type of the operating characteristic requested
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_get_operating_characteristic(const u32 node,
					const enum zynqmp_pm_opchar_type type)
{
	return invoke_pm_fn(GET_OPERATING_CHARACTERISTIC,
						node, type, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_operating_characteristic);

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

/* Direct-Control API functions */

/**
 * zynqmp_pm_reset_assert - Request setting of reset (1 - assert, 0 - release)
 * @reset:		Reset to be configured
 * @assert_flag:	Flag stating should reset be asserted (1) or
 *			released (0)
 *
 * Return:		Returns status, either success or error+reason
 */
int zynqmp_pm_reset_assert(const u32 reset, const u32 assert_flag)
{
	return invoke_pm_fn(RESET_ASSERT, reset, assert_flag, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_reset_assert);

/**
 * zynqmp_pm_reset_get_status - Get status of the reset
 * @reset:	Reset whose status should be returned
 * @status:	Returned status
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_reset_get_status(const u32 reset, u32 *status)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];

	if (status == NULL)
		return zynqmp_pm_ret_code(XST_PM_CONFLICT);

	invoke_pm_fn(RESET_GET_STATUS, reset, 0, 0, 0, ret_payload);
	*status = ret_payload[1];

	return zynqmp_pm_ret_code((enum pm_ret_status)ret_payload[0]);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_reset_get_status);

/**
 * zynqmp_pm_mmio_write - Perform write to protected mmio
 * @address:	Address to write to
 * @mask:	Mask to apply
 * @value:	Value to write
 *
 * This function provides access to PM-related control registers
 * that may not be directly accessible by a particular PU.
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_mmio_write(const u32 address,
				     const u32 mask,
				     const u32 value)
{
	return invoke_pm_fn(MMIO_WRITE, address, mask, value, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_mmio_write);

/**
 * zynqmp_pm_mmio_read - Read value from protected mmio
 * @address:	Address to write to
 * @value:	Value to read
 *
 * This function provides access to PM-related control registers
 * that may not be directly accessible by a particular PU.
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_mmio_read(const u32 address, u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];

	if (!value)
		return -EINVAL;

	invoke_pm_fn(MMIO_READ, address, 0, 0, 0, ret_payload);
	*value = ret_payload[1];

	return zynqmp_pm_ret_code((enum pm_ret_status)ret_payload[0]);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_mmio_read);

#ifdef CONFIG_ZYNQMP_PM_API_DEBUGFS
/**
 * zynqmp_pm_argument_value - Extract argument value from a PM-API request
 * @arg:	Entered PM-API argument in string format
 *
 * Return:	Argument value in unsigned integer format on success
 *		0 otherwise
 */
static u32 zynqmp_pm_argument_value(char *arg)
{
	u32 value;

	if (!arg)
		return 0;

	if (!kstrtouint(arg, 0, &value))
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
	char *kern_buff;
	char *pm_api_req;
	u32 pm_id = 0;
	u32 pm_api_arg[4];
	int ret;
	int i = 0;

	if (*off != 0 || len <= 0)
		return -EINVAL;

	kern_buff = kzalloc(len, GFP_KERNEL);
	if (!kern_buff)
		return -ENOMEM;

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
				pm_api_arg[1] ? pm_api_arg[1] :
						ZYNQMP_PM_REQUEST_ACK_NO);
		break;
	case SET_WAKEUP_SOURCE:
		ret = zynqmp_pm_set_wakeup_source(pm_api_arg[0],
					pm_api_arg[1], pm_api_arg[2]);
		break;
	case SYSTEM_SHUTDOWN:
		ret = zynqmp_pm_system_shutdown(pm_api_arg[0]);
		break;
	case REQUEST_NODE:
		ret = zynqmp_pm_request_node(pm_api_arg[0],
			pm_api_arg[1] ? pm_api_arg[1] :
					ZYNQMP_PM_CAPABILITY_ACCESS,
			pm_api_arg[2] ? pm_api_arg[2] : 0,
			pm_api_arg[3] ? pm_api_arg[3] :
				ZYNQMP_PM_REQUEST_ACK_CALLBACK_STANDARD);
		break;
	case RELEASE_NODE:
		ret = zynqmp_pm_release_node(pm_api_arg[0],
				pm_api_arg[1] ? pm_api_arg[1] :
						ZYNQMP_PM_MAX_LATENCY);
		break;
	case SET_REQUIREMENT:
		ret = zynqmp_pm_set_requirement(pm_api_arg[0],
			pm_api_arg[1] ? pm_api_arg[1] :
					ZYNQMP_PM_CAPABILITY_CONTEXT,
			pm_api_arg[2] ? pm_api_arg[2] : 0,
			pm_api_arg[3] ? pm_api_arg[3] :
				ZYNQMP_PM_REQUEST_ACK_CALLBACK_STANDARD);
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
		ret = zynqmp_pm_get_node_status(pm_api_arg[0]);
		break;
	case GET_OPERATING_CHARACTERISTIC:
		ret = zynqmp_pm_get_operating_characteristic(pm_api_arg[0],
				pm_api_arg[1] ? pm_api_arg[1] :
				ZYNQMP_PM_OPERATING_CHARACTERISTIC_POWER);
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
		ret = zynqmp_pm_reset_get_status(pm_api_arg[0], &pm_api_arg[1]);
		pr_info("%s Reset status: %u\n", __func__, pm_api_arg[1]);
		break;
	case MMIO_READ:
		ret = zynqmp_pm_mmio_read(pm_api_arg[0], &pm_api_arg[1]);
		pr_info("%s MMIO value: %#x\n", __func__, pm_api_arg[1]);
		break;
	case MMIO_WRITE:
		ret = zynqmp_pm_mmio_write(pm_api_arg[0],
				     pm_api_arg[1], pm_api_arg[2]);
		break;
	default:
		pr_err("%s Unsupported PM-API request\n", __func__);
		ret = -EINVAL;
	}

 err:
	kfree(kern_buff);
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
 *
 * Return:      Returns 0 on success
 *		Corresponding error code otherwise
 */
static int zynqmp_pm_api_debugfs_init(void)
{
	int err;

	/* Initialize debugfs interface */
	zynqmp_pm_debugfs_dir = debugfs_create_dir(DRIVER_NAME, NULL);
	if (!zynqmp_pm_debugfs_dir) {
		pr_err("%s debugfs_create_dir failed\n", __func__);
		return -ENODEV;
	}

	zynqmp_pm_debugfs_power =
		debugfs_create_file("power", S_IWUSR | S_IWGRP | S_IWOTH,
					zynqmp_pm_debugfs_dir, NULL,
					&fops_zynqmp_pm_dbgfs);
	if (!zynqmp_pm_debugfs_power) {
		pr_err("%s debugfs_create_file power failed\n", __func__);
		err = -ENODEV;
		goto err_dbgfs;
	}

	zynqmp_pm_debugfs_api_version =
		debugfs_create_file("api_version", S_IRUSR | S_IRGRP | S_IROTH,
					zynqmp_pm_debugfs_dir, NULL,
					&fops_zynqmp_pm_dbgfs);
	if (!zynqmp_pm_debugfs_api_version) {
		pr_err("%s debugfs_create_file api_version failed\n",
								__func__);
		err = -ENODEV;
		goto err_dbgfs;
	}
	return 0;

 err_dbgfs:
	debugfs_remove_recursive(zynqmp_pm_debugfs_dir);
	zynqmp_pm_debugfs_dir = NULL;
	return err;
}

/**
 * zynqmp_pm_api_debugfs_remove - Remove debugfs functionality
 *
 * Return:	Returns 0
 */
static int zynqmp_pm_api_debugfs_remove(void)
{
	debugfs_remove_recursive(zynqmp_pm_debugfs_dir);
	zynqmp_pm_debugfs_dir = NULL;
	return 0;
}

#else
static int zynqmp_pm_api_debugfs_init(void)
{
	return 0;
}

static int zynqmp_pm_api_debugfs_remove(void)
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
 * get_set_conduit_method - Choose SMC or HVC based communication
 * @np:	Pointer to the device_node structure
 *
 * Use SMC or HVC-based functions to communicate with EL2/EL3
 */
static void get_set_conduit_method(struct device_node *np)
{
	const char *method;
	struct device *dev;

	dev = container_of(&np, struct device, of_node);

	if (of_property_read_string(np, "method", &method)) {
		dev_warn(dev, "%s Missing \"method\" property - defaulting to smc\n",
			__func__);
		do_fw_call = do_fw_call_smc;
		return;
	}

	if (!strcmp("hvc", method)) {
		do_fw_call = do_fw_call_hvc;

	} else if (!strcmp("smc", method)) {
		do_fw_call = do_fw_call_smc;
	} else {
		dev_warn(dev, "%s Invalid \"method\" property: %s - defaulting to smc\n",
			__func__, method);
		do_fw_call = do_fw_call_smc;
	}
}

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
	struct device_node *np;

	np = pdev->dev.of_node;

	get_set_conduit_method(np);

	/* Check PM API version number */
	zynqmp_pm_get_api_version(&pm_api_version);
	if (pm_api_version != ZYNQMP_PM_VERSION) {
		pr_err("%s power management API version error. Expected: v%d.%d - Found: v%d.%d\n",
		       __func__,
		       ZYNQMP_PM_VERSION_MAJOR, ZYNQMP_PM_VERSION_MINOR,
		       pm_api_version >> 16, pm_api_version & 0xffff);
		return -EIO;
	}

	pr_info("%s Power management API v%d.%d\n", __func__,
		ZYNQMP_PM_VERSION_MAJOR, ZYNQMP_PM_VERSION_MINOR);

	zynqmp_pm_api_debugfs_init();

	return 0;
}

/**
 * zynqmp_pm_remove - Remove debugfs interface
 *
 * @pdev:	Pointer to the platform_device structure
 *
 * Return:	Returns 0
 */
static int zynqmp_pm_remove(struct platform_device *pdev)
{
	zynqmp_pm_api_debugfs_remove();

	return 0;
}

static struct platform_driver zynqmp_pm_platform_driver = {
	.probe   = zynqmp_pm_probe,
	.remove  = zynqmp_pm_remove,
	.driver  = {
			.name             = DRIVER_NAME,
			.of_match_table   = pm_of_match,
		   },
};

module_platform_driver(zynqmp_pm_platform_driver);

MODULE_DESCRIPTION("Xilinx Zynq MPSoC Power Management API platform driver.");
MODULE_LICENSE("GPL");
