// SPDX-License-Identifier: GPL-2.0+
/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2014-2018 Xilinx
 *
 *  Michal Simek <michal.simek@xilinx.com>
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
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>

#include <linux/soc/xilinx/zynqmp/firmware.h>

/**
 * zynqmp_pm_ret_code - Convert PMU-FW error codes to Linux error codes
 * @ret_status:		PMUFW return code
 *
 * Return:		corresponding Linux error code
 */
int zynqmp_pm_ret_code(u32 ret_status)
{
	switch (ret_status) {
	case XST_PM_SUCCESS:
	case XST_PM_DOUBLE_REQ:
		return 0;
	case XST_PM_NO_ACCESS:
		return -EACCES;
	case XST_PM_ABORT_SUSPEND:
		return -ECANCELED;
	case XST_PM_INTERNAL:
	case XST_PM_CONFLICT:
	case XST_PM_INVALID_NODE:
	default:
		return -EINVAL;
	}
}

static noinline int do_fw_call_fail(u64 arg0, u64 arg1, u64 arg2,
				    u32 *ret_payload)
{
	return -ENODEV;
}

/*
 * PM function call wrapper
 * Invoke do_fw_call_smc or do_fw_call_hvc, depending on the configuration
 */
static int (*do_fw_call)(u64, u64, u64, u32 *ret_payload) = do_fw_call_fail;

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
	struct arm_smccc_res res;

	arm_smccc_smc(arg0, arg1, arg2, 0, 0, 0, 0, 0, &res);

	if (ret_payload != NULL) {
		ret_payload[0] = lower_32_bits(res.a0);
		ret_payload[1] = upper_32_bits(res.a0);
		ret_payload[2] = lower_32_bits(res.a1);
		ret_payload[3] = upper_32_bits(res.a1);
		ret_payload[4] = lower_32_bits(res.a2);
	}

	return zynqmp_pm_ret_code((enum pm_ret_status)res.a0);
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
	struct arm_smccc_res res;

	arm_smccc_hvc(arg0, arg1, arg2, 0, 0, 0, 0, 0, &res);

	if (ret_payload != NULL) {
		ret_payload[0] = lower_32_bits(res.a0);
		ret_payload[1] = upper_32_bits(res.a0);
		ret_payload[2] = lower_32_bits(res.a1);
		ret_payload[3] = upper_32_bits(res.a1);
		ret_payload[4] = lower_32_bits(res.a2);
	}

	return zynqmp_pm_ret_code((enum pm_ret_status)res.a0);
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
int invoke_pm_fn(u32 pm_api_id, u32 arg0, u32 arg1, u32 arg2, u32 arg3,
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

static u32 pm_api_version;

/**
 * zynqmp_pm_get_api_version - Get version number of PMU PM firmware
 * @version:	Returned version value
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_get_api_version(u32 *version)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (version == NULL)
		return -EINVAL;

	/* Check is PM API version already verified */
	if (pm_api_version > 0) {
		*version = pm_api_version;
		return 0;
	}
	ret = invoke_pm_fn(GET_API_VERSION, 0, 0, 0, 0, ret_payload);
	*version = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_get_chipid - Get silicon ID registers
 * @idcode:	IDCODE register
 * @version:	version register
 *
 * Return:	Returns the status of the operation and the idcode and version
 *		registers in @idcode and @version.
 */
static int zynqmp_pm_get_chipid(u32 *idcode, u32 *version)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!idcode || !version)
		return -EINVAL;

	ret = invoke_pm_fn(GET_CHIPID, 0, 0, 0, 0, ret_payload);
	*idcode = ret_payload[1];
	*version = ret_payload[2];

	return ret;
}

/**
 * get_set_conduit_method - Choose SMC or HVC based communication
 * @np:	Pointer to the device_node structure
 *
 * Use SMC or HVC-based functions to communicate with EL2/EL3
 *
 * Return: Returns 0 on success or error code
 */
static int get_set_conduit_method(struct device_node *np)
{
	const char *method;

	if (of_property_read_string(np, "method", &method)) {
		pr_warn("%s missing \"method\" property\n", __func__);
		return -ENXIO;
	}

	if (!strcmp("hvc", method)) {
		do_fw_call = do_fw_call_hvc;
	} else if (!strcmp("smc", method)) {
		do_fw_call = do_fw_call_smc;
	} else {
		pr_warn("%s Invalid \"method\" property: %s\n",
			__func__, method);
		return -EINVAL;
	}

	return 0;
}

/**
 * zynqmp_pm_reset_assert - Request setting of reset (1 - assert, 0 - release)
 * @reset:		Reset to be configured
 * @assert_flag:	Flag stating should reset be asserted (1) or
 *			released (0)
 *
 * Return:		Returns status, either success or error+reason
 */
static int zynqmp_pm_reset_assert(const enum zynqmp_pm_reset reset,
			   const enum zynqmp_pm_reset_action assert_flag)
{
	return invoke_pm_fn(RESET_ASSERT, reset, assert_flag, 0, 0, NULL);
}

/**
 * zynqmp_pm_reset_get_status - Get status of the reset
 * @reset:	Reset whose status should be returned
 * @status:	Returned status
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_reset_get_status(const enum zynqmp_pm_reset reset,
				      u32 *status)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (status == NULL)
		return -EINVAL;

	ret = invoke_pm_fn(RESET_GET_STATUS, reset, 0, 0, 0, ret_payload);
	*status = ret_payload[1];

	return ret;
}

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

/**
 * zynqmp_pm_fpga_load - Perform the fpga load
 * @address:    Address to write to
 * @size:       pl bitstream size
 * @flags:
 *	BIT(0) - Bit-stream type.
 *		 0 - Full Bit-stream.
 *		 1 - Partial Bit-stream.
 *	BIT(1) - Authentication.
 *		 1 - Enable.
 *		 0 - Disable.
 *	BIT(2) - Encryption.
 *		 1 - Enable.
 *		 0 - Disable.
 * NOTE -
 *	The current implementation supports only Full Bit-stream.
 *
 * This function provides access to xilfpga library to transfer
 * the required bitstream into PL.
 *
 * Return:      Returns status, either success or error+reason
 */
static int zynqmp_pm_fpga_load(const u64 address, const u32 size,
			       const u32 flags)
{
	return invoke_pm_fn(FPGA_LOAD, (u32)address,
			((u32)(address >> 32)), size, flags, NULL);
}

/**
 * zynqmp_pm_fpga_get_status - Read value from PCAP status register
 * @value:      Value to read
 *
 *This function provides access to the xilfpga library to get
 *the PCAP status
 *
 * Return:      Returns status, either success or error+reason
 */
static int zynqmp_pm_fpga_get_status(u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!value)
		return -EINVAL;

	ret = invoke_pm_fn(FPGA_GET_STATUS, 0, 0, 0, 0, ret_payload);
	*value = ret_payload[1];

	return ret;
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
static int zynqmp_pm_request_suspend(const u32 node,
			      const enum zynqmp_pm_request_ack ack,
			      const u32 latency,
			      const u32 state)
{
	return invoke_pm_fn(REQUEST_SUSPEND, node, ack,
						latency, state, NULL);
}

/**
 * zynqmp_pm_force_powerdown - PM call to request for another PU or subsystem to
 *				be powered down forcefully
 * @target:	Node ID of the targeted PU or subsystem
 * @ack:	Flag to specify whether acknowledge is requested
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_force_powerdown(const u32 target,
				     const enum zynqmp_pm_request_ack ack)
{
	return invoke_pm_fn(FORCE_POWERDOWN, target, ack, 0, 0, NULL);
}

/**
 * zynqmp_pm_request_wakeup - PM call to wake up selected master or subsystem
 * @node:	Node ID of the master or subsystem
 * @set_addr:	Specifies whether the address argument is relevant
 * @address:	Address from which to resume when woken up
 * @ack:	Flag to specify whether acknowledge requested
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_request_wakeup(const u32 node,
				    const bool set_addr,
				    const u64 address,
				    const enum zynqmp_pm_request_ack ack)
{
	/* set_addr flag is encoded into 1st bit of address */
	return invoke_pm_fn(REQUEST_WAKEUP, node, address | set_addr,
				address >> 32, ack, NULL);
}

/**
 * zynqmp_pm_set_wakeup_source - PM call to specify the wakeup source
 *					while suspended
 * @target:	Node ID of the targeted PU or subsystem
 * @wakeup_node:Node ID of the wakeup peripheral
 * @enable:	Enable or disable the specified peripheral as wake source
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_set_wakeup_source(const u32 target,
				       const u32 wakeup_node,
				       const u32 enable)
{
	return invoke_pm_fn(SET_WAKEUP_SOURCE, target,
					wakeup_node, enable, 0, NULL);
}

/**
 * zynqmp_pm_system_shutdown - PM call to request a system shutdown or restart
 * @type:	Shutdown or restart? 0 for shutdown, 1 for restart
 * @subtype:	Specifies which system should be restarted or shut down
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_system_shutdown(const u32 type, const u32 subtype)
{
	return invoke_pm_fn(SYSTEM_SHUTDOWN, type, subtype, 0, 0, NULL);
}

/**
 * zynqmp_pm_request_node - PM call to request a node with specific capabilities
 * @node:		Node ID of the slave
 * @capabilities:	Requested capabilities of the slave
 * @qos:		Quality of service (not supported)
 * @ack:		Flag to specify whether acknowledge is requested
 *
 * Return:		Returns status, either success or error+reason
 */
static int zynqmp_pm_request_node(const u32 node, const u32 capabilities,
				  const u32 qos,
				  const enum zynqmp_pm_request_ack ack)
{
	return invoke_pm_fn(REQUEST_NODE, node, capabilities,
						qos, ack, NULL);
}

/**
 * zynqmp_pm_release_node - PM call to release a node
 * @node:	Node ID of the slave
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_release_node(const u32 node)
{
	return invoke_pm_fn(RELEASE_NODE, node, 0, 0, 0, NULL);
}

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
static int zynqmp_pm_set_requirement(const u32 node, const u32 capabilities,
				     const u32 qos,
				     const enum zynqmp_pm_request_ack ack)
{
	return invoke_pm_fn(SET_REQUIREMENT, node, capabilities,
						qos, ack, NULL);
}

/**
 * zynqmp_pm_set_max_latency - PM call to set wakeup latency requirements
 * @node:	Node ID of the slave
 * @latency:	Requested maximum wakeup latency
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_set_max_latency(const u32 node, const u32 latency)
{
	return invoke_pm_fn(SET_MAX_LATENCY, node,
					latency, 0, 0, NULL);
}

/**
 * zynqmp_pm_set_configuration - PM call to set system configuration
 * @physical_addr:	Physical 32-bit address of data structure in memory
 *
 * Return:		Returns status, either success or error+reason
 */
static int zynqmp_pm_set_configuration(const u32 physical_addr)
{
	return invoke_pm_fn(SET_CONFIGURATION, physical_addr, 0, 0, 0, NULL);
}

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
static int zynqmp_pm_get_node_status(const u32 node, u32 *const status,
				     u32 *const requirements, u32 *const usage)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!status)
		return -EINVAL;

	ret = invoke_pm_fn(GET_NODE_STATUS, node, 0, 0, 0, ret_payload);
	if (ret_payload[0] == XST_PM_SUCCESS) {
		*status = ret_payload[1];
		if (requirements)
			*requirements = ret_payload[2];
		if (usage)
			*usage = ret_payload[3];
	}

	return ret;
}

/**
 * zynqmp_pm_get_operating_characteristic - PM call to request operating
 *						characteristic information
 * @node:	Node ID of the slave
 * @type:	Type of the operating characteristic requested
 * @result:	Used to return the requsted operating characteristic
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_get_operating_characteristic(const u32 node,
						const enum zynqmp_pm_opchar_type
						type, u32 *const result)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!result)
		return -EINVAL;

	ret = invoke_pm_fn(GET_OPERATING_CHARACTERISTIC,
			   node, type, 0, 0, ret_payload);
	if (ret_payload[0] == XST_PM_SUCCESS)
		*result = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_init_finalize - PM call to informi firmware that the caller master
 *				has initialized its own power management
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_init_finalize(void)
{
	return invoke_pm_fn(PM_INIT_FINALIZE, 0, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_get_callback_data - Get callback data from firmware
 * @buf:	Buffer to store payload data
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_get_callback_data(u32 *buf)
{
	return invoke_pm_fn(GET_CALLBACK_DATA, 0, 0, 0, 0, buf);
}

/**
 * zynqmp_pm_set_suspend_mode	- Set system suspend mode
 *
 * @mode:	Mode to set for system suspend
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_set_suspend_mode(u32 mode)
{
	return invoke_pm_fn(SET_SUSPEND_MODE, mode, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_sha_hash - Access the SHA engine to calculate the hash
 * @address:	Address of the data/ Address of output buffer where
 *		hash should be stored.
 * @size:	Size of the data.
 * @flags:
 *	BIT(0) - Sha3 init (Here address and size inputs can be NULL)
 *	BIT(1) - Sha3 update (address should holds the )
 *	BIT(2) - Sha3 final (address should hold the address of
 *		 buffer to store hash)
 *
 * Return:	Returns status, either success or error code.
 */
static int zynqmp_pm_sha_hash(const u64 address, const u32 size,
			      const u32 flags)
{
	u32 lower_32_bits = (u32)address;
	u32 upper_32_bits = (u32)(address >> 32);

	return invoke_pm_fn(SECURE_SHA, upper_32_bits, lower_32_bits,
				size, flags, NULL);
}

/**
 * zynqmp_pm_rsa - Access RSA hardware to encrypt/decrypt the data with RSA.
 * @address:	Address of the data
 * @size:	Size of the data.
 * @flags:
 *		BIT(0) - Encryption/Decryption
 *			 0 - RSA decryption with private key
 *			 1 - RSA encryption with public key.
 *
 * Return:	Returns status, either success or error code.
 */
static int zynqmp_pm_rsa(const u64 address, const u32 size, const u32 flags)
{
	u32 lower_32_bits = (u32)address;
	u32 upper_32_bits = (u32)(address >> 32);

	return invoke_pm_fn(SECURE_RSA, upper_32_bits, lower_32_bits,
				size, flags, NULL);
}

/**
 * zynqmp_pm_pinctrl_request - Request Pin from firmware
 * @pin:	Pin number to request
 *
 * This function requests pin from firmware.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_pinctrl_request(const u32 pin)
{
	return invoke_pm_fn(PINCTRL_REQUEST, pin, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_pinctrl_release - Inform firmware that Pin control is released
 * @pin:	Pin number to release
 *
 * This function release pin from firmware.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_pinctrl_release(const u32 pin)
{
	return invoke_pm_fn(PINCTRL_RELEASE, pin, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_pinctrl_get_function - Read function id set for the given pin
 * @pin:	Pin number
 * @node:	Buffer to store node ID matching current function
 *
 * This function provides the function currently set for the given pin.
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_pinctrl_get_function(const u32 pin, u32 *node)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!node)
		return -EINVAL;

	ret = invoke_pm_fn(PINCTRL_GET_FUNCTION, pin, 0, 0, 0, ret_payload);
	*node = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_pinctrl_set_function - Set requested function for the pin
 * @pin:	Pin number
 * @node:	Node ID mapped with the requested function
 *
 * This function sets requested function for the given pin.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_pinctrl_set_function(const u32 pin, const u32 node)
{
	return invoke_pm_fn(PINCTRL_SET_FUNCTION, pin, node, 0, 0, NULL);
}

/**
 * zynqmp_pm_pinctrl_get_config - Get configuration parameter for the pin
 * @pin:	Pin number
 * @param:	Parameter to get
 * @value:	Buffer to store parameter value
 *
 * This function gets requested configuration parameter for the given pin.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_pinctrl_get_config(const u32 pin, const u32 param,
					u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!value)
		return -EINVAL;

	ret = invoke_pm_fn(PINCTRL_CONFIG_PARAM_GET, pin,
			   param, 0, 0, ret_payload);
	*value = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_pinctrl_set_config - Set configuration parameter for the pin
 * @pin:	Pin number
 * @param:	Parameter to set
 * @value:	Parameter value to set
 *
 * This function sets requested configuration parameter for the given pin.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_pinctrl_set_config(const u32 pin, const u32 param,
					u32 value)
{
	return invoke_pm_fn(PINCTRL_CONFIG_PARAM_SET, pin,
			    param, value, 0, NULL);
}

/**
 * zynqmp_pm_ioctl - PM IOCTL API for device control and configs
 * @node_id:	Node ID of the device
 * @ioctl_id:	ID of the requested IOCTL
 * @arg1:	Argument 1 to requested IOCTL call
 * @arg2:	Argument 2 to requested IOCTL call
 * @out:	Returned output value
 *
 * This function calls IOCTL to firmware for device control and configuration.
 *
 * Return:		Returns status, either success or error+reason
 */
static int zynqmp_pm_ioctl(u32 node_id, u32 ioctl_id, u32 arg1, u32 arg2,
			   u32 *out)
{
	return invoke_pm_fn(IOCTL, node_id, ioctl_id, arg1, arg2, out);
}

static int zynqmp_pm_query_data(struct zynqmp_pm_query_data qdata, u32 *out)
{
	return invoke_pm_fn(QUERY_DATA, qdata.qid, qdata.arg1,
			    qdata.arg2, qdata.arg3, out);
}

/**
 * zynqmp_pm_clock_enable - Enable the clock for given id
 * @clock_id:	ID of the clock to be enabled
 *
 * This function is used by master to enable the clock
 * including peripherals and PLL clocks.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_enable(u32 clock_id)
{
	return invoke_pm_fn(CLOCK_ENABLE, clock_id, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_clock_disable - Disable the clock for given id
 * @clock_id:	ID of the clock to be disable
 *
 * This function is used by master to disable the clock
 * including peripherals and PLL clocks.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_disable(u32 clock_id)
{
	return invoke_pm_fn(CLOCK_DISABLE, clock_id, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_clock_getstate - Get the clock state for given id
 * @clock_id:	ID of the clock to be queried
 * @state:	1/0 (Enabled/Disabled)
 *
 * This function is used by master to get the state of clock
 * including peripherals and PLL clocks.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_getstate(u32 clock_id, u32 *state)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = invoke_pm_fn(CLOCK_GETSTATE, clock_id, 0, 0, 0, ret_payload);
	*state = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_clock_setdivider - Set the clock divider for given id
 * @clock_id:	ID of the clock
 * @divider:	divider value.
 *
 * This function is used by master to set divider for any clock
 * to achieve desired rate.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_setdivider(u32 clock_id, u32 divider)
{
	return invoke_pm_fn(CLOCK_SETDIVIDER, clock_id, divider, 0, 0, NULL);
}

/**
 * zynqmp_pm_clock_getdivider - Get the clock divider for given id
 * @clock_id:	ID of the clock
 * @divider:	divider value.
 *
 * This function is used by master to get divider values
 * for any clock.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_getdivider(u32 clock_id, u32 *divider)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = invoke_pm_fn(CLOCK_GETDIVIDER, clock_id, 0, 0, 0, ret_payload);
	*divider = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_clock_setrate - Set the clock rate for given id
 * @clock_id:	ID of the clock
 * @rate:	rate value in hz
 *
 * This function is used by master to set rate for any clock.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_setrate(u32 clock_id, u32 rate)
{
	return invoke_pm_fn(CLOCK_SETRATE, clock_id, rate, 0, 0, NULL);
}

/**
 * zynqmp_pm_clock_getrate - Get the clock rate for given id
 * @clock_id:	ID of the clock
 * @rate:	rate value in hz
 *
 * This function is used by master to get rate
 * for any clock.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_getrate(u32 clock_id, u32 *rate)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = invoke_pm_fn(CLOCK_GETRATE, clock_id, 0, 0, 0, ret_payload);
	*rate = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_clock_setparent - Set the clock parent for given id
 * @clock_id:	ID of the clock
 * @parent_id:	parent id
 *
 * This function is used by master to set parent for any clock.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_setparent(u32 clock_id, u32 parent_id)
{
	return invoke_pm_fn(CLOCK_SETPARENT, clock_id, parent_id, 0, 0, NULL);
}

/**
 * zynqmp_pm_clock_getparent - Get the clock parent for given id
 * @clock_id:	ID of the clock
 * @parent_id:	parent id
 *
 * This function is used by master to get parent index
 * for any clock.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_getparent(u32 clock_id, u32 *parent_id)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = invoke_pm_fn(CLOCK_GETPARENT, clock_id, 0, 0, 0, ret_payload);
	*parent_id = ret_payload[1];

	return ret;
}

static const struct zynqmp_eemi_ops eemi_ops  = {
	.get_api_version = zynqmp_pm_get_api_version,
	.get_chipid = zynqmp_pm_get_chipid,
	.reset_assert = zynqmp_pm_reset_assert,
	.reset_get_status = zynqmp_pm_reset_get_status,
	.mmio_write = zynqmp_pm_mmio_write,
	.mmio_read = zynqmp_pm_mmio_read,
	.fpga_load = zynqmp_pm_fpga_load,
	.fpga_get_status = zynqmp_pm_fpga_get_status,
	.sha_hash = zynqmp_pm_sha_hash,
	.rsa = zynqmp_pm_rsa,
	.request_suspend = zynqmp_pm_request_suspend,
	.force_powerdown = zynqmp_pm_force_powerdown,
	.request_wakeup = zynqmp_pm_request_wakeup,
	.set_wakeup_source = zynqmp_pm_set_wakeup_source,
	.system_shutdown = zynqmp_pm_system_shutdown,
	.request_node = zynqmp_pm_request_node,
	.release_node = zynqmp_pm_release_node,
	.set_requirement = zynqmp_pm_set_requirement,
	.set_max_latency = zynqmp_pm_set_max_latency,
	.set_configuration = zynqmp_pm_set_configuration,
	.get_node_status = zynqmp_pm_get_node_status,
	.get_operating_characteristic = zynqmp_pm_get_operating_characteristic,
	.init_finalize = zynqmp_pm_init_finalize,
	.get_callback_data = zynqmp_pm_get_callback_data,
	.set_suspend_mode = zynqmp_pm_set_suspend_mode,
	.ioctl = zynqmp_pm_ioctl,
	.query_data = zynqmp_pm_query_data,
	.pinctrl_request = zynqmp_pm_pinctrl_request,
	.pinctrl_release = zynqmp_pm_pinctrl_release,
	.pinctrl_get_function = zynqmp_pm_pinctrl_get_function,
	.pinctrl_set_function = zynqmp_pm_pinctrl_set_function,
	.pinctrl_get_config = zynqmp_pm_pinctrl_get_config,
	.pinctrl_set_config = zynqmp_pm_pinctrl_set_config,
	.clock_enable = zynqmp_pm_clock_enable,
	.clock_disable = zynqmp_pm_clock_disable,
	.clock_getstate = zynqmp_pm_clock_getstate,
	.clock_setdivider = zynqmp_pm_clock_setdivider,
	.clock_getdivider = zynqmp_pm_clock_getdivider,
	.clock_setrate = zynqmp_pm_clock_setrate,
	.clock_getrate = zynqmp_pm_clock_getrate,
	.clock_setparent = zynqmp_pm_clock_setparent,
	.clock_getparent = zynqmp_pm_clock_getparent,
};

/**
 * get_eemi_ops	- Get eemi ops functions
 *
 * Return:	- pointer of eemi_ops structure
 */
const struct zynqmp_eemi_ops *get_eemi_ops(void)
{
	return &eemi_ops;
}
EXPORT_SYMBOL_GPL(get_eemi_ops);

static int __init zynqmp_plat_init(void)
{
	struct device_node *np;
	int ret = 0;

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynqmp");
	if (!np)
		return 0;
	of_node_put(np);

	/* We're running on a ZynqMP machine, the PM node is mandatory. */
	np = of_find_compatible_node(NULL, NULL, "xlnx,zynqmp-pm");
	if (!np) {
		pr_warn("%s: pm node not found\n", __func__);
		return -ENXIO;
	}

	ret = get_set_conduit_method(np);
	if (ret) {
		of_node_put(np);
		return ret;
	}

	/* Check PM API version number */
	zynqmp_pm_get_api_version(&pm_api_version);
	if (pm_api_version != ZYNQMP_PM_VERSION) {
		panic("%s power management API version error. Expected: v%d.%d - Found: v%d.%d\n",
		       __func__,
		       ZYNQMP_PM_VERSION_MAJOR, ZYNQMP_PM_VERSION_MINOR,
		       pm_api_version >> 16, pm_api_version & 0xffff);
	}

	pr_info("%s Power management API v%d.%d\n", __func__,
		ZYNQMP_PM_VERSION_MAJOR, ZYNQMP_PM_VERSION_MINOR);

	of_node_put(np);
	return ret;
}

early_initcall(zynqmp_plat_init);
