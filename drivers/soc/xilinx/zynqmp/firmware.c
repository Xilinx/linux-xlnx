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
 * zynqmp_pm_get_chipid - Get silicon ID registers
 * @idcode:	IDCODE register
 * @version:	version register
 *
 * Return:	Returns the status of the operation and the idcode and version
 *		registers in @idcode and @version.
 */
int zynqmp_pm_get_chipid(u32 *idcode, u32 *version)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];

	if (!idcode || !version)
		return -EINVAL;

	invoke_pm_fn(GET_CHIPID, 0, 0, 0, 0, ret_payload);
	*idcode = ret_payload[1];
	*version = ret_payload[2];

	return zynqmp_pm_ret_code((enum pm_ret_status)ret_payload[0]);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_chipid);

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
int zynqmp_pm_reset_assert(const enum zynqmp_pm_reset reset,
			   const enum zynqmp_pm_reset_action assert_flag)
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
int zynqmp_pm_reset_get_status(const enum zynqmp_pm_reset reset, u32 *status)
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
int zynqmp_pm_fpga_load(const u64 address, const u32 size, const u32 flags)
{
	return invoke_pm_fn(FPGA_LOAD, (u32)address,
			((u32)(address >> 32)), size, flags, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_fpga_load);

/**
 * zynqmp_pm_fpga_get_status - Read value from PCAP status register
 * @value:      Value to read
 *
 *This function provides access to the xilfpga library to get
 *the PCAP status
 *
 * Return:      Returns status, either success or error+reason
 */
int zynqmp_pm_fpga_get_status(u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];

	if (!value)
		return -EINVAL;

	invoke_pm_fn(FPGA_GET_STATUS, 0, 0, 0, 0, ret_payload);
	*value = ret_payload[1];

	return zynqmp_pm_ret_code((enum pm_ret_status)ret_payload[0]);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_fpga_get_status);

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
int zynqmp_pm_sha_hash(const u64 address, const u32 size, const u32 flags)
{
	u32 lower_32_bits = (u32)address;
	u32 upper_32_bits = (u32)(address >> 32);

	return invoke_pm_fn(SECURE_SHA, upper_32_bits, lower_32_bits,
				size, flags, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_sha_hash);

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
int zynqmp_pm_rsa(const u64 address, const u32 size, const u32 flags)
{
	u32 lower_32_bits = (u32)address;
	u32 upper_32_bits = (u32)(address >> 32);

	return invoke_pm_fn(SECURE_RSA, upper_32_bits, lower_32_bits,
				size, flags, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_rsa);

/**
 * zynqmp_pm_pinctrl_request - Request Pin from firmware
 * @pin:	Pin number to request
 *
 * This function requests pin from firmware.
 *
 * Return:	Returns status, either success or error+reason.
 */
int zynqmp_pm_pinctrl_request(const u32 pin)
{
	return invoke_pm_fn(PINCTRL_REQUEST, pin, 0, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_pinctrl_request);

/**
 * zynqmp_pm_pinctrl_release - Inform firmware that Pin control is released
 * @pin:	Pin number to release
 *
 * This function release pin from firmware.
 *
 * Return:	Returns status, either success or error+reason.
 */
int zynqmp_pm_pinctrl_release(const u32 pin)
{
	return invoke_pm_fn(PINCTRL_RELEASE, pin, 0, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_pinctrl_release);

/**
 * zynqmp_pm_pinctrl_get_function - Read function id set for the given pin
 * @pin:	Pin number
 * @node:	Buffer to store node ID matching current function
 *
 * This function provides the function currently set for the given pin.
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_pinctrl_get_function(const u32 pin, u32 *node)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];

	if (!node)
		return -EINVAL;

	invoke_pm_fn(PINCTRL_GET_FUNCTION, pin, 0, 0, 0, ret_payload);
	*node = ret_payload[1];

	return zynqmp_pm_ret_code((enum pm_ret_status)ret_payload[0]);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_pinctrl_get_function);

/**
 * zynqmp_pm_pinctrl_set_function - Set requested function for the pin
 * @pin:	Pin number
 * @node:	Node ID mapped with the requested function
 *
 * This function sets requested function for the given pin.
 *
 * Return:	Returns status, either success or error+reason.
 */
int zynqmp_pm_pinctrl_set_function(const u32 pin, const u32 node)
{
	return invoke_pm_fn(PINCTRL_SET_FUNCTION, pin, node, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_pinctrl_set_function);

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
int zynqmp_pm_pinctrl_get_config(const u32 pin, const u32 param, u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];

	if (!value)
		return -EINVAL;

	invoke_pm_fn(PINCTRL_CONFIG_PARAM_GET, pin,
		     param, 0, 0, ret_payload);
	*value = ret_payload[1];

	return zynqmp_pm_ret_code((enum pm_ret_status)ret_payload[0]);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_pinctrl_get_config);

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
int zynqmp_pm_pinctrl_set_config(const u32 pin, const u32 param, u32 value)
{
	return invoke_pm_fn(PINCTRL_CONFIG_PARAM_SET, pin,
			    param, value, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_pinctrl_set_config);

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
int zynqmp_pm_ioctl(u32 node_id, u32 ioctl_id, u32 arg1, u32 arg2, u32 *out)
{
	return invoke_pm_fn(IOCTL, node_id, ioctl_id, arg1, arg2, out);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_ioctl);

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
