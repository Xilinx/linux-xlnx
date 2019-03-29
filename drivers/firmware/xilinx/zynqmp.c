// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2014-2018 Xilinx, Inc.
 *
 *  Michal Simek <michal.simek@xilinx.com>
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajanv@xilinx.com>
 */

#include <linux/arm-smccc.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/firmware/xlnx-zynqmp.h>
#include "zynqmp-debug.h"

static unsigned long register_address;

static const struct zynqmp_eemi_ops *eemi_ops_tbl;

static bool feature_check_enabled;
static u32 zynqmp_pm_features[PM_API_MAX];

static const struct mfd_cell firmware_devs[] = {
	{
		.name = "zynqmp_power_controller",
	},
};

/**
 * zynqmp_pm_ret_code() - Convert PMU-FW error codes to Linux error codes
 * @ret_status:		PMUFW return code
 *
 * Return: corresponding Linux error code
 */
static int zynqmp_pm_ret_code(u32 ret_status)
{
	switch (ret_status) {
	case XST_PM_SUCCESS:
	case XST_PM_DOUBLE_REQ:
		return 0;
	case XST_PM_NO_FEATURE:
		return -ENOTSUPP;
	case XST_PM_NO_ACCESS:
		return -EACCES;
	case XST_PM_ABORT_SUSPEND:
		return -ECANCELED;
	case XST_PM_MULT_USER:
		return -EUSERS;
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
 * do_fw_call_smc() - Call system-level platform management layer (SMC)
 * @arg0:		Argument 0 to SMC call
 * @arg1:		Argument 1 to SMC call
 * @arg2:		Argument 2 to SMC call
 * @ret_payload:	Returned value array
 *
 * Invoke platform management function via SMC call (no hypervisor present).
 *
 * Return: Returns status, either success or error+reason
 */
static noinline int do_fw_call_smc(u64 arg0, u64 arg1, u64 arg2,
				   u32 *ret_payload)
{
	struct arm_smccc_res res;

	arm_smccc_smc(arg0, arg1, arg2, 0, 0, 0, 0, 0, &res);

	if (ret_payload) {
		ret_payload[0] = lower_32_bits(res.a0);
		ret_payload[1] = upper_32_bits(res.a0);
		ret_payload[2] = lower_32_bits(res.a1);
		ret_payload[3] = upper_32_bits(res.a1);
	}

	return zynqmp_pm_ret_code((enum pm_ret_status)res.a0);
}

/**
 * do_fw_call_hvc() - Call system-level platform management layer (HVC)
 * @arg0:		Argument 0 to HVC call
 * @arg1:		Argument 1 to HVC call
 * @arg2:		Argument 2 to HVC call
 * @ret_payload:	Returned value array
 *
 * Invoke platform management function via HVC
 * HVC-based for communication through hypervisor
 * (no direct communication with ATF).
 *
 * Return: Returns status, either success or error+reason
 */
static noinline int do_fw_call_hvc(u64 arg0, u64 arg1, u64 arg2,
				   u32 *ret_payload)
{
	struct arm_smccc_res res;

	arm_smccc_hvc(arg0, arg1, arg2, 0, 0, 0, 0, 0, &res);

	if (ret_payload) {
		ret_payload[0] = lower_32_bits(res.a0);
		ret_payload[1] = upper_32_bits(res.a0);
		ret_payload[2] = lower_32_bits(res.a1);
		ret_payload[3] = upper_32_bits(res.a1);
	}

	return zynqmp_pm_ret_code((enum pm_ret_status)res.a0);
}

/**
 * zynqmp_pm_feature() - Check weather given feature is supported or not
 * @api_id:		API ID to check
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_feature(u32 api_id)
{
	int ret;
	u32 ret_payload[PAYLOAD_ARG_CNT];
	u64 smc_arg[2];

	if (!feature_check_enabled)
		return 0;

	/* Return value if feature is already checked */
	if (zynqmp_pm_features[api_id] != PM_FEATURE_UNCHECKED)
		return zynqmp_pm_features[api_id];

	smc_arg[0] = PM_SIP_SVC | PM_FEATURE_CHECK;
	smc_arg[1] = api_id;

	ret = do_fw_call(smc_arg[0], smc_arg[1], 0, ret_payload);
	if (ret) {
		zynqmp_pm_features[api_id] = PM_FEATURE_INVALID;
		return PM_FEATURE_INVALID;
	}

	zynqmp_pm_features[api_id] = ret_payload[1];

	return zynqmp_pm_features[api_id];
}

/**
 * zynqmp_pm_invoke_fn() - Invoke the system-level platform management layer
 *			   caller function depending on the configuration
 * @pm_api_id:		Requested PM-API call
 * @arg0:		Argument 0 to requested PM-API call
 * @arg1:		Argument 1 to requested PM-API call
 * @arg2:		Argument 2 to requested PM-API call
 * @arg3:		Argument 3 to requested PM-API call
 * @ret_payload:	Returned value array
 *
 * Invoke platform management function for SMC or HVC call, depending on
 * configuration.
 * Following SMC Calling Convention (SMCCC) for SMC64:
 * Pm Function Identifier,
 * PM_SIP_SVC + PM_API_ID =
 *	((SMC_TYPE_FAST << FUNCID_TYPE_SHIFT)
 *	((SMC_64) << FUNCID_CC_SHIFT)
 *	((SIP_START) << FUNCID_OEN_SHIFT)
 *	((PM_API_ID) & FUNCID_NUM_MASK))
 *
 * PM_SIP_SVC	- Registered ZynqMP SIP Service Call.
 * PM_API_ID	- Platform Management API ID.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_invoke_fn(u32 pm_api_id, u32 arg0, u32 arg1,
			u32 arg2, u32 arg3, u32 *ret_payload)
{
	/*
	 * Added SIP service call Function Identifier
	 * Make sure to stay in x0 register
	 */
	u64 smc_arg[4];

	if (zynqmp_pm_feature(pm_api_id) == PM_FEATURE_INVALID)
		return -ENOTSUPP;

	smc_arg[0] = PM_SIP_SVC | pm_api_id;
	smc_arg[1] = ((u64)arg1 << 32) | arg0;
	smc_arg[2] = ((u64)arg3 << 32) | arg2;

	return do_fw_call(smc_arg[0], smc_arg[1], smc_arg[2], ret_payload);
}

static u32 pm_api_version;
static u32 pm_tz_version;

/**
 * zynqmp_pm_get_api_version() - Get version number of PMU PM firmware
 * @version:	Returned version value
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_get_api_version(u32 *version)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!version)
		return -EINVAL;

	/* Check is PM API version already verified */
	if (pm_api_version > 0) {
		*version = pm_api_version;
		return 0;
	}
	ret = zynqmp_pm_invoke_fn(PM_GET_API_VERSION, 0, 0, 0, 0, ret_payload);
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

	ret = zynqmp_pm_invoke_fn(PM_GET_CHIPID, 0, 0, 0, 0, ret_payload);
	*idcode = ret_payload[1];
	*version = ret_payload[2];

	return ret;
}

/**
 * zynqmp_pm_get_trustzone_version() - Get secure trustzone firmware version
 * @version:	Returned version value
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_get_trustzone_version(u32 *version)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!version)
		return -EINVAL;

	/* Check is PM trustzone version already verified */
	if (pm_tz_version > 0) {
		*version = pm_tz_version;
		return 0;
	}
	ret = zynqmp_pm_invoke_fn(PM_GET_TRUSTZONE_VERSION, 0, 0,
				  0, 0, ret_payload);
	*version = ret_payload[1];

	return ret;
}

/**
 * get_set_conduit_method() - Choose SMC or HVC based communication
 * @np:		Pointer to the device_node structure
 *
 * Use SMC or HVC-based functions to communicate with EL2/EL3.
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
 * zynqmp_pm_query_data() - Get query data from firmware
 * @qdata:	Variable to the zynqmp_pm_query_data structure
 * @out:	Returned output value
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_query_data(struct zynqmp_pm_query_data qdata, u32 *out)
{
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_QUERY_DATA, qdata.qid, qdata.arg1,
				  qdata.arg2, qdata.arg3, out);

	/*
	 * For clock name query, all bytes in SMC response are clock name
	 * characters and return code is always success. For invalid clocks,
	 * clock name bytes would be zeros.
	 */
	return qdata.qid == PM_QID_CLOCK_GET_NAME ? 0 : ret;
}

/**
 * zynqmp_pm_clock_enable() - Enable the clock for given id
 * @clock_id:	ID of the clock to be enabled
 *
 * This function is used by master to enable the clock
 * including peripherals and PLL clocks.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_clock_enable(u32 clock_id)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_ENABLE, clock_id, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_clock_disable() - Disable the clock for given id
 * @clock_id:	ID of the clock to be disable
 *
 * This function is used by master to disable the clock
 * including peripherals and PLL clocks.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_clock_disable(u32 clock_id)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_DISABLE, clock_id, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_clock_getstate() - Get the clock state for given id
 * @clock_id:	ID of the clock to be queried
 * @state:	1/0 (Enabled/Disabled)
 *
 * This function is used by master to get the state of clock
 * including peripherals and PLL clocks.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_clock_getstate(u32 clock_id, u32 *state)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_CLOCK_GETSTATE, clock_id, 0,
				  0, 0, ret_payload);
	*state = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_clock_setdivider() - Set the clock divider for given id
 * @clock_id:	ID of the clock
 * @divider:	divider value
 *
 * This function is used by master to set divider for any clock
 * to achieve desired rate.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_clock_setdivider(u32 clock_id, u32 divider)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_SETDIVIDER, clock_id, divider,
				   0, 0, NULL);
}

/**
 * zynqmp_pm_clock_getdivider() - Get the clock divider for given id
 * @clock_id:	ID of the clock
 * @divider:	divider value
 *
 * This function is used by master to get divider values
 * for any clock.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_clock_getdivider(u32 clock_id, u32 *divider)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_CLOCK_GETDIVIDER, clock_id, 0,
				  0, 0, ret_payload);
	*divider = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_clock_setrate() - Set the clock rate for given id
 * @clock_id:	ID of the clock
 * @rate:	rate value in hz
 *
 * This function is used by master to set rate for any clock.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_clock_setrate(u32 clock_id, u64 rate)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_SETRATE, clock_id,
				   lower_32_bits(rate),
				   upper_32_bits(rate),
				   0, NULL);
}

/**
 * zynqmp_pm_clock_getrate() - Get the clock rate for given id
 * @clock_id:	ID of the clock
 * @rate:	rate value in hz
 *
 * This function is used by master to get rate
 * for any clock.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_clock_getrate(u32 clock_id, u64 *rate)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_CLOCK_GETRATE, clock_id, 0,
				  0, 0, ret_payload);
	*rate = ((u64)ret_payload[2] << 32) | ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_clock_setparent() - Set the clock parent for given id
 * @clock_id:	ID of the clock
 * @parent_id:	parent id
 *
 * This function is used by master to set parent for any clock.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_clock_setparent(u32 clock_id, u32 parent_id)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_SETPARENT, clock_id,
				   parent_id, 0, 0, NULL);
}

/**
 * zynqmp_pm_clock_getparent() - Get the clock parent for given id
 * @clock_id:	ID of the clock
 * @parent_id:	parent id
 *
 * This function is used by master to get parent index
 * for any clock.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_clock_getparent(u32 clock_id, u32 *parent_id)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_CLOCK_GETPARENT, clock_id, 0,
				  0, 0, ret_payload);
	*parent_id = ret_payload[1];

	return ret;
}

/**
 * zynqmp_is_valid_ioctl() - Check whether IOCTL ID is valid or not
 * @ioctl_id:	IOCTL ID
 *
 * Return: 1 if IOCTL is valid else 0
 */
static inline int zynqmp_is_valid_ioctl(u32 ioctl_id)
{
	switch (ioctl_id) {
	case IOCTL_SET_PLL_FRAC_MODE:
	case IOCTL_GET_PLL_FRAC_MODE:
	case IOCTL_SET_PLL_FRAC_DATA:
	case IOCTL_GET_PLL_FRAC_DATA:
	case IOCTL_GET_RPU_OPER_MODE:
	case IOCTL_SET_RPU_OPER_MODE:
	case IOCTL_RPU_BOOT_ADDR_CONFIG:
	case IOCTL_TCM_COMB_CONFIG:
	case IOCTL_SET_TAPDELAY_BYPASS:
	case IOCTL_SET_SGMII_MODE:
	case IOCTL_SD_DLL_RESET:
	case IOCTL_SET_SD_TAPDELAY:
	case IOCTL_WRITE_GGS:
	case IOCTL_READ_GGS:
	case IOCTL_WRITE_PGGS:
	case IOCTL_READ_PGGS:
	case IOCTL_ULPI_RESET:
	case IOCTL_SET_BOOT_HEALTH_STATUS:
	case IOCTL_AFI:
		return 1;
	default:
		return 0;
	}
}

/**
 * zynqmp_pm_ioctl() - PM IOCTL API for device control and configs
 * @node_id:	Node ID of the device
 * @ioctl_id:	ID of the requested IOCTL
 * @arg1:	Argument 1 to requested IOCTL call
 * @arg2:	Argument 2 to requested IOCTL call
 * @out:	Returned output value
 *
 * This function calls IOCTL to firmware for device control and configuration.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_ioctl(u32 node_id, u32 ioctl_id, u32 arg1, u32 arg2,
			   u32 *out)
{
	if (!zynqmp_is_valid_ioctl(ioctl_id))
		return -EINVAL;

	return zynqmp_pm_invoke_fn(PM_IOCTL, node_id, ioctl_id,
				   arg1, arg2, out);
}

/**
 * zynqmp_pm_reset_assert - Request setting of reset (1 - assert, 0 - release)
 * @reset:		Reset to be configured
 * @assert_flag:	Flag stating should reset be asserted (1) or
 *			released (0)
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_reset_assert(const enum zynqmp_pm_reset reset,
				  const enum zynqmp_pm_reset_action assert_flag)
{
	return zynqmp_pm_invoke_fn(PM_RESET_ASSERT, reset, assert_flag,
				   0, 0, NULL);
}

/**
 * zynqmp_pm_reset_get_status - Get status of the reset
 * @reset:      Reset whose status should be returned
 * @status:     Returned status
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_reset_get_status(const enum zynqmp_pm_reset reset,
				      u32 *status)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!status)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_RESET_GET_STATUS, reset, 0,
				  0, 0, ret_payload);
	*status = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_init_finalize() - PM call to inform firmware that the caller
 *			       master has initialized its own power management
 *
 * This API function is to be used for notify the power management controller
 * about the completed power management initialization.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_init_finalize(void)
{
	return zynqmp_pm_invoke_fn(PM_PM_INIT_FINALIZE, 0, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_set_suspend_mode()	- Set system suspend mode
 * @mode:	Mode to set for system suspend
 *
 * This API function is used to set mode of system suspend.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_set_suspend_mode(u32 mode)
{
	return zynqmp_pm_invoke_fn(PM_SET_SUSPEND_MODE, mode, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_request_node() - Request a node with specific capabilities
 * @node:		Node ID of the slave
 * @capabilities:	Requested capabilities of the slave
 * @qos:		Quality of service (not supported)
 * @ack:		Flag to specify whether acknowledge is requested
 *
 * This function is used by master to request particular node from firmware.
 * Every master must request node before using it.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_request_node(const u32 node, const u32 capabilities,
				  const u32 qos,
				  const enum zynqmp_pm_request_ack ack)
{
	return zynqmp_pm_invoke_fn(PM_REQUEST_NODE, node, capabilities,
				   qos, ack, NULL);
}

/**
 * zynqmp_pm_release_node() - Release a node
 * @node:	Node ID of the slave
 *
 * This function is used by master to inform firmware that master
 * has released node. Once released, master must not use that node
 * without re-request.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_release_node(const u32 node)
{
	return zynqmp_pm_invoke_fn(PM_RELEASE_NODE, node, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_set_requirement() - PM call to set requirement for PM slaves
 * @node:		Node ID of the slave
 * @capabilities:	Requested capabilities of the slave
 * @qos:		Quality of service (not supported)
 * @ack:		Flag to specify whether acknowledge is requested
 *
 * This API function is to be used for slaves a PU already has requested
 * to change its capabilities.
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_set_requirement(const u32 node, const u32 capabilities,
				     const u32 qos,
				     const enum zynqmp_pm_request_ack ack)
{
	return zynqmp_pm_invoke_fn(PM_SET_REQUIREMENT, node, capabilities,
				   qos, ack, NULL);
}

/**
 * zynqmp_pm_load_pdi - Load and process pdi
 * @src:	Source device where PDI is located
 * @address:	Pdi src address
 *
 * This function provides support to load pdi from linux
 *
 * Return: Returns status, either success or error+reason
 */
static int zynqmp_pm_load_pdi(const u32 src, const u64 address)
{
	return zynqmp_pm_invoke_fn(PM_LOAD_PDI, src,
				   lower_32_bits(address),
				   upper_32_bits(address), 0, NULL);
}

/**
 * zynqmp_pm_fpga_load - Perform the fpga load
 * @address:	Address to write to
 * @size:	pl bitstream size
 * @flags:
 *	BIT(0) - Bitstream type.
 *		 0 - Full Bitstream.
 *		 1 - Partial Bitstream.
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
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_fpga_load(const u64 address, const u32 size,
			       const u32 flags)
{
	return zynqmp_pm_invoke_fn(PM_FPGA_LOAD, (u32)address,
				   ((u32)(address >> 32)), size, flags, NULL);
}

/**
 * zynqmp_pm_fpga_get_status - Read value from PCAP status register
 * @value:	Value to read
 *
 * This function provides access to the xilfpga library to get
 * the PCAP status
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_fpga_get_status(u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!value)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_FPGA_GET_STATUS, 0, 0, 0, 0, ret_payload);
	*value = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_fpga_read - Perform the fpga configuration readback
 * @reg_numframes: Configuration register offset (or) Number of frames to read
 * @phys_address: Physical Address of the buffer
 * @readback_type: Type of fpga readback operation
 * @value: Value to read
 *
 * This function provides access to xilfpga library to perform
 * fpga configuration readback.
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_fpga_read(const u32 reg_numframes, const u64 phys_address,
			       u32 readback_type, u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!value)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_FPGA_READ, reg_numframes,
				  lower_32_bits(phys_address),
				  upper_32_bits(phys_address), readback_type,
				  ret_payload);
	*value = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_sha_hash - Access the SHA engine to calculate the hash
 * @address:	Address of the data/ Address of output buffer where
 *		hash should be stored.
 * @size:	Size of the data.
 * @flags:
 *	BIT(0) - for initializing csudma driver and SHA3(Here address
 *		 and size inputs can be NULL).
 *	BIT(1) - to call Sha3_Update API which can be called multiple
 *		 times when data is not contiguous.
 *	BIT(2) - to get final hash of the whole updated data.
 *		 Hash will be overwritten at provided address with
 *		 48 bytes.
 *
 * Return:	Returns status, either success or error code.
 */
static int zynqmp_pm_sha_hash(const u64 address, const u32 size,
			      const u32 flags)
{
	u32 lower_32_bits = (u32)address;
	u32 upper_32_bits = (u32)(address >> 32);

	return zynqmp_pm_invoke_fn(PM_SECURE_SHA, upper_32_bits, lower_32_bits,
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

	return zynqmp_pm_invoke_fn(PM_SECURE_RSA, upper_32_bits, lower_32_bits,
				   size, flags, NULL);
}

/**
 * zynqmp_pm_aes - Access AES hardware to encrypt/decrypt the data using
 * AES-GCM core.
 * @address:	Address of the AesParams structure.
 * @out:	Returned output value
 *
 * Return:	Returns status, either success or error code.
 */
static int zynqmp_pm_aes_engine(const u64 address, u32 *out)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!out)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_SECURE_AES, upper_32_bits(address),
				  lower_32_bits(address),
				  0, 0, ret_payload);
	*out = ret_payload[1];
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
	return zynqmp_pm_invoke_fn(PM_REQUEST_SUSPEND, node, ack,
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
	return zynqmp_pm_invoke_fn(PM_FORCE_POWERDOWN, target, ack, 0, 0, NULL);
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
	return zynqmp_pm_invoke_fn(PM_REQUEST_WAKEUP, node, address | set_addr,
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
	return zynqmp_pm_invoke_fn(PM_SET_WAKEUP_SOURCE, target,
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
	return zynqmp_pm_invoke_fn(PM_SYSTEM_SHUTDOWN, type, subtype,
				   0, 0, NULL);
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
	return zynqmp_pm_invoke_fn(PM_SET_MAX_LATENCY, node, latency,
				   0, 0, NULL);
}

/**
 * zynqmp_pm_set_configuration - PM call to set system configuration
 * @physical_addr:	Physical 32-bit address of data structure in memory
 *
 * Return:		Returns status, either success or error+reason
 */
static int zynqmp_pm_set_configuration(const u32 physical_addr)
{
	return zynqmp_pm_invoke_fn(PM_SET_CONFIGURATION, physical_addr, 0,
				   0, 0, NULL);
}

/**
 * zynqmp_pm_get_node_status - PM call to request a node's current power state
 * @node:		ID of the component or sub-system in question
 * @status:		Current operating state of the requested node
 * @requirements:	Current requirements asserted on the node,
 *			used for slave nodes only.
 * @usage:		Usage information, used for slave nodes only:
 *			PM_USAGE_NO_MASTER	- No master is currently using
 *						  the node
 *			PM_USAGE_CURRENT_MASTER	- Only requesting master is
 *						  currently using the node
 *			PM_USAGE_OTHER_MASTER	- Only other masters are
 *						  currently using the node
 *			PM_USAGE_BOTH_MASTERS	- Both the current and at least
 *						  one other master is currently
 *						  using the node
 *
 * Return:		Returns status, either success or error+reason
 */
static int zynqmp_pm_get_node_status(const u32 node, u32 *const status,
				     u32 *const requirements, u32 *const usage)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!status)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_GET_NODE_STATUS, node, 0, 0,
				  0, ret_payload);
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
		const enum zynqmp_pm_opchar_type type,
		u32 *const result)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!result)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_GET_OPERATING_CHARACTERISTIC,
				  node, type, 0, 0, ret_payload);
	if (ret_payload[0] == XST_PM_SUCCESS)
		*result = ret_payload[1];

	return ret;
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
	return zynqmp_pm_invoke_fn(PM_PINCTRL_REQUEST, pin, 0, 0, 0, NULL);
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
	return zynqmp_pm_invoke_fn(PM_PINCTRL_RELEASE, pin, 0, 0, 0, NULL);
}

/**
 * zynqmp_pm_pinctrl_get_function - Read function id set for the given pin
 * @pin:	Pin number
 * @id:		Buffer to store function ID
 *
 * This function provides the function currently set for the given pin.
 *
 * Return:	Returns status, either success or error+reason
 */
static int zynqmp_pm_pinctrl_get_function(const u32 pin, u32 *id)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!id)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_PINCTRL_GET_FUNCTION, pin, 0,
				  0, 0, ret_payload);
	*id = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_pinctrl_set_function - Set requested function for the pin
 * @pin:	Pin number
 * @id:		Function ID to set
 *
 * This function sets requested function for the given pin.
 *
 * Return:	Returns status, either success or error+reason.
 */
static int zynqmp_pm_pinctrl_set_function(const u32 pin, const u32 id)
{
	return zynqmp_pm_invoke_fn(PM_PINCTRL_SET_FUNCTION, pin, id,
				   0, 0, NULL);
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

	ret = zynqmp_pm_invoke_fn(PM_PINCTRL_CONFIG_PARAM_GET, pin, param,
				  0, 0, ret_payload);
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
	return zynqmp_pm_invoke_fn(PM_PINCTRL_CONFIG_PARAM_SET, pin,
				   param, value, 0, NULL);
}

/**
 * zynqmp_pm_config_reg_access - PM Config API for Config register access
 * @register_access_id:	ID of the requested REGISTER_ACCESS
 * @address:		Address of the register to be accessed
 * @mask:		Mask to be written to the register
 * @value:		Value to be written to the register
 * @out:		Returned output value
 *
 * This function calls REGISTER_ACCESS to configure CSU/PMU registers.
 *
 * Return:	Returns status, either success or error+reason
 */

static int zynqmp_pm_config_reg_access(u32 register_access_id, u32 address,
				       u32 mask, u32 value, u32 *out)
{
	return zynqmp_pm_invoke_fn(PM_REGISTER_ACCESS, register_access_id,
				   address, mask, value, out);
}

/**
 * zynqmp_pm_efuse_access - Provides access to efuse memory.
 * @address:	Address of the efuse params structure
 * @out:		Returned output value
 *
 * Return:	Returns status, either success or error code.
 */
static int zynqmp_pm_efuse_access(const u64 address, u32 *out)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!out)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_EFUSE_ACCESS, upper_32_bits(address),
				  lower_32_bits(address), 0, 0, ret_payload);
	*out = ret_payload[1];

	return ret;
}

static int zynqmp_pm_secure_load(const u64 src_addr, u64 key_addr, u64 *dst)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret_value;

	if (!dst)
		return -EINVAL;

	ret_value = zynqmp_pm_invoke_fn(PM_SECURE_IMAGE,
					lower_32_bits(src_addr),
					upper_32_bits(src_addr),
					lower_32_bits(key_addr),
					upper_32_bits(key_addr),
					ret_payload);
	*dst = ((u64)ret_payload[1] << 32) | ret_payload[2];

	return ret_value;
}

static const struct zynqmp_eemi_ops eemi_ops = {
	.get_api_version = zynqmp_pm_get_api_version,
	.get_chipid = zynqmp_pm_get_chipid,
	.query_data = zynqmp_pm_query_data,
	.clock_enable = zynqmp_pm_clock_enable,
	.clock_disable = zynqmp_pm_clock_disable,
	.clock_getstate = zynqmp_pm_clock_getstate,
	.clock_setdivider = zynqmp_pm_clock_setdivider,
	.clock_getdivider = zynqmp_pm_clock_getdivider,
	.clock_setrate = zynqmp_pm_clock_setrate,
	.clock_getrate = zynqmp_pm_clock_getrate,
	.clock_setparent = zynqmp_pm_clock_setparent,
	.clock_getparent = zynqmp_pm_clock_getparent,
	.ioctl = zynqmp_pm_ioctl,
	.reset_assert = zynqmp_pm_reset_assert,
	.reset_get_status = zynqmp_pm_reset_get_status,
	.init_finalize = zynqmp_pm_init_finalize,
	.set_suspend_mode = zynqmp_pm_set_suspend_mode,
	.request_node = zynqmp_pm_request_node,
	.release_node = zynqmp_pm_release_node,
	.set_requirement = zynqmp_pm_set_requirement,
	.fpga_load = zynqmp_pm_fpga_load,
	.fpga_get_status = zynqmp_pm_fpga_get_status,
	.fpga_read = zynqmp_pm_fpga_read,
	.sha_hash = zynqmp_pm_sha_hash,
	.rsa = zynqmp_pm_rsa,
	.request_suspend = zynqmp_pm_request_suspend,
	.force_powerdown = zynqmp_pm_force_powerdown,
	.request_wakeup = zynqmp_pm_request_wakeup,
	.set_wakeup_source = zynqmp_pm_set_wakeup_source,
	.system_shutdown = zynqmp_pm_system_shutdown,
	.set_max_latency = zynqmp_pm_set_max_latency,
	.set_configuration = zynqmp_pm_set_configuration,
	.get_node_status = zynqmp_pm_get_node_status,
	.get_operating_characteristic = zynqmp_pm_get_operating_characteristic,
	.pinctrl_request = zynqmp_pm_pinctrl_request,
	.pinctrl_release = zynqmp_pm_pinctrl_release,
	.pinctrl_get_function = zynqmp_pm_pinctrl_get_function,
	.pinctrl_set_function = zynqmp_pm_pinctrl_set_function,
	.pinctrl_get_config = zynqmp_pm_pinctrl_get_config,
	.pinctrl_set_config = zynqmp_pm_pinctrl_set_config,
	.register_access = zynqmp_pm_config_reg_access,
	.aes = zynqmp_pm_aes_engine,
	.efuse_access = zynqmp_pm_efuse_access,
	.secure_image = zynqmp_pm_secure_load,
	.pdi_load = zynqmp_pm_load_pdi,
};

/**
 * zynqmp_pm_get_eemi_ops - Get eemi ops functions
 *
 * Return: Pointer of eemi_ops structure
 */
const struct zynqmp_eemi_ops *zynqmp_pm_get_eemi_ops(void)
{
	if (eemi_ops_tbl)
		return eemi_ops_tbl;
	else
		return ERR_PTR(-EPROBE_DEFER);

}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_eemi_ops);

/**
 * struct zynqmp_pm_shutdown_scope - Struct for shutdown scope
 * @subtype:	Shutdown subtype
 * @name:	Matching string for scope argument
 *
 * This struct encapsulates mapping between shutdown scope ID and string.
 */
struct zynqmp_pm_shutdown_scope {
	const enum zynqmp_pm_shutdown_subtype subtype;
	const char *name;
};

static struct zynqmp_pm_shutdown_scope shutdown_scopes[] = {
	[ZYNQMP_PM_SHUTDOWN_SUBTYPE_SUBSYSTEM] = {
		.subtype = ZYNQMP_PM_SHUTDOWN_SUBTYPE_SUBSYSTEM,
		.name = "subsystem",
	},
	[ZYNQMP_PM_SHUTDOWN_SUBTYPE_PS_ONLY] = {
		.subtype = ZYNQMP_PM_SHUTDOWN_SUBTYPE_PS_ONLY,
		.name = "ps_only",
	},
	[ZYNQMP_PM_SHUTDOWN_SUBTYPE_SYSTEM] = {
		.subtype = ZYNQMP_PM_SHUTDOWN_SUBTYPE_SYSTEM,
		.name = "system",
	},
};

static struct zynqmp_pm_shutdown_scope *selected_scope =
		&shutdown_scopes[ZYNQMP_PM_SHUTDOWN_SUBTYPE_SYSTEM];

/**
 * zynqmp_pm_is_shutdown_scope_valid - Check if shutdown scope string is valid
 * @scope_string:	Shutdown scope string
 *
 * Return:		Return pointer to matching shutdown scope struct from
 *			array of available options in system if string is valid,
 *			otherwise returns NULL.
 */
static struct zynqmp_pm_shutdown_scope*
		zynqmp_pm_is_shutdown_scope_valid(const char *scope_string)
{
	int count;

	for (count = 0; count < ARRAY_SIZE(shutdown_scopes); count++)
		if (sysfs_streq(scope_string, shutdown_scopes[count].name))
			return &shutdown_scopes[count];

	return NULL;
}

/**
 * shutdown_scope_show - Show shutdown_scope sysfs attribute
 * @kobj:	Kobject structure
 * @attr:	Kobject attribute structure
 * @buf:	Requested available shutdown_scope attributes string
 *
 * User-space interface for viewing the available scope options for system
 * shutdown. Scope option for next shutdown call is marked with [].
 *
 * Usage: cat /sys/firmware/zynqmp/shutdown_scope
 *
 * Return:	Number of bytes printed into the buffer.
 */
static ssize_t shutdown_scope_show(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   char *buf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(shutdown_scopes); i++) {
		if (&shutdown_scopes[i] == selected_scope) {
			strcat(buf, "[");
			strcat(buf, shutdown_scopes[i].name);
			strcat(buf, "]");
		} else {
			strcat(buf, shutdown_scopes[i].name);
		}
		strcat(buf, " ");
	}
	strcat(buf, "\n");

	return strlen(buf);
}

/**
 * shutdown_scope_store - Store shutdown_scope sysfs attribute
 * @kobj:	Kobject structure
 * @attr:	Kobject attribute structure
 * @buf:	User entered shutdown_scope attribute string
 * @count:	Buffer size
 *
 * User-space interface for setting the scope for the next system shutdown.
 * Usage: echo <scope> > /sys/firmware/zynqmp/shutdown_scope
 *
 * The Linux shutdown functionality implemented via PSCI system_off does not
 * include an option to set a scope, i.e. which parts of the system to shut
 * down.
 *
 * This API function allows to set the shutdown scope for the next shutdown
 * request by passing it to the ATF running in EL3. When the next shutdown
 * is performed, the platform specific portion of PSCI-system_off can use
 * the chosen shutdown scope.
 *
 * subsystem:	Only the APU along with all of its peripherals not used by other
 *		processing units will be shut down. This may result in the FPD
 *		power domain being shut down provided that no other processing
 *		unit uses FPD peripherals or DRAM.
 * ps_only:	The complete PS will be shut down, including the RPU, PMU, etc.
 *		Only the PL domain (FPGA) remains untouched.
 * system:	The complete system/device is shut down.
 *
 * Return:	count argument if request succeeds, the corresponding error
 *		code otherwise
 */
static ssize_t shutdown_scope_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t count)
{
	int ret;
	struct zynqmp_pm_shutdown_scope *scope;

	scope = zynqmp_pm_is_shutdown_scope_valid(buf);
	if (!scope)
		return -EINVAL;

	ret = zynqmp_pm_system_shutdown(ZYNQMP_PM_SHUTDOWN_TYPE_SETSCOPE_ONLY,
					scope->subtype);
	if (ret) {
		pr_err("unable to set shutdown scope %s\n", buf);
		return ret;
	}

	selected_scope = scope;

	return count;
}

static struct kobj_attribute zynqmp_attr_shutdown_scope =
						__ATTR_RW(shutdown_scope);

/**
 * health_status_store - Store health_status sysfs attribute
 * @kobj:	Kobject structure
 * @attr:	Kobject attribute structure
 * @buf:	User entered health_status attribute string
 * @count:	Buffer size
 *
 * User-space interface for setting the boot health status.
 * Usage: echo <value> > /sys/firmware/zynqmp/health_status
 *
 * Value:
 *	1 - Set healthy bit to 1
 *	0 - Unset healthy bit
 *
 * Return:	count argument if request succeeds, the corresponding error
 *		code otherwise
 */
static ssize_t health_status_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	int ret;
	unsigned int value;

	ret = kstrtouint(buf, 10, &value);
	if (ret)
		return ret;

	ret = zynqmp_pm_ioctl(0, IOCTL_SET_BOOT_HEALTH_STATUS, value, 0, NULL);
	if (ret) {
		pr_err("unable to set healthy bit value to %u\n", value);
		return ret;
	}

	return count;
}

static struct kobj_attribute zynqmp_attr_health_status =
						__ATTR_WO(health_status);

/**
 * config_reg_store - Write config_reg sysfs attribute
 * @kobj:	Kobject structure
 * @attr:	Kobject attribute structure
 * @buf:	User entered health_status attribute string
 * @count:	Buffer size
 *
 * User-space interface for setting the config register.
 *
 * To write any CSU/PMU register
 * echo <address> <mask> <values> > /sys/firmware/zynqmp/config_reg
 * Usage:
 * echo 0x345AB234 0xFFFFFFFF 0x1234ABCD > /sys/firmware/zynqmp/config_reg
 *
 * To Read any CSU/PMU register, write address to the variable like below
 * echo <address> > /sys/firmware/zynqmp/config_reg
 *
 * Return:	count argument if request succeeds, the corresponding error
 *		code otherwise
 */
static ssize_t config_reg_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	char *kern_buff, *inbuf, *tok;
	unsigned long address, value, mask;
	int ret;

	kern_buff = kzalloc(count, GFP_KERNEL);
	if (!kern_buff)
		return -ENOMEM;

	ret = strlcpy(kern_buff, buf, count);
	if (ret < 0) {
		ret = -EFAULT;
		goto err;
	}

	inbuf = kern_buff;

	/* Read the addess */
	tok = strsep(&inbuf, " ");
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}
	ret = kstrtol(tok, 16, &address);
	if (ret) {
		ret = -EFAULT;
		goto err;
	}
	/* Read the write value */
	tok = strsep(&inbuf, " ");
	/*
	 * If parameter provided is only address, then its a read operation.
	 * Store the address in a global variable and retrieve whenever
	 * required.
	 */
	if (!tok) {
		register_address = address;
		goto err;
	}
	register_address = address;

	ret = kstrtol(tok, 16, &mask);
	if (ret) {
		ret = -EFAULT;
		goto err;
	}
	tok = strsep(&inbuf, " ");
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}
	ret = kstrtol(tok, 16, &value);
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}
	ret = zynqmp_pm_config_reg_access(CONFIG_REG_WRITE, address,
					  mask, value, NULL);
	if (ret)
		pr_err("unable to write value to %lx\n", value);
err:
	kfree(kern_buff);
	if (ret)
		return ret;
	return count;
}

/**
 * config_reg_show - Read config_reg sysfs attribute
 * @kobj:	Kobject structure
 * @attr:	Kobject attribute structure
 * @buf:	User entered health_status attribute string
 *
 * User-space interface for getting the config register.
 *
 * To Read any CSU/PMU register, write address to the variable like below
 * echo <address> > /sys/firmware/zynqmp/config_reg
 *
 * Then Read the address using below command
 * cat /sys/firmware/zynqmp/config_reg
 *
 * Return: number of chars written to buf.
 */
static ssize_t config_reg_show(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       char *buf)
{
	int ret;
	u32 ret_payload[PAYLOAD_ARG_CNT];

	ret = zynqmp_pm_config_reg_access(CONFIG_REG_READ, register_address,
					  0, 0, ret_payload);
	if (ret)
		return ret;

	return sprintf(buf, "0x%x\n", ret_payload[1]);
}

static struct kobj_attribute zynqmp_attr_config_reg =
					__ATTR_RW(config_reg);

static struct attribute *attrs[] = {
	&zynqmp_attr_shutdown_scope.attr,
	&zynqmp_attr_health_status.attr,
	&zynqmp_attr_config_reg.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs = attrs,
	NULL,
};

static int zynqmp_pm_sysfs_init(void)
{
	struct kobject *zynqmp_kobj;
	int ret;

	zynqmp_kobj = kobject_create_and_add("zynqmp", firmware_kobj);
	if (!zynqmp_kobj) {
		pr_err("zynqmp: Firmware kobj add failed.\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(zynqmp_kobj, &attr_group);
	if (ret) {
		pr_err("%s() sysfs creation fail with error %d\n",
		       __func__, ret);
		goto err;
	}

	ret = zynqmp_pm_ggs_init(zynqmp_kobj);
	if (ret) {
		pr_err("%s() GGS init fail with error %d\n",
		       __func__, ret);
		goto err;
	}
err:
	return ret;
}

static int zynqmp_firmware_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynqmp");
	if (!np) {
		np = of_find_compatible_node(NULL, NULL, "xlnx,versal");
		if (!np)
			return 0;

		feature_check_enabled = true;
	}
	of_node_put(np);

	ret = get_set_conduit_method(dev->of_node);
	if (ret)
		return ret;

	/* Check PM API version number */
	zynqmp_pm_get_api_version(&pm_api_version);
	if (pm_api_version < ZYNQMP_PM_VERSION) {
		panic("%s Platform Management API version error. Expected: v%d.%d - Found: v%d.%d\n",
		      __func__,
		      ZYNQMP_PM_VERSION_MAJOR, ZYNQMP_PM_VERSION_MINOR,
		      pm_api_version >> 16, pm_api_version & 0xFFFF);
	}

	pr_info("%s Platform Management API v%d.%d\n", __func__,
		pm_api_version >> 16, pm_api_version & 0xFFFF);

	/* Check trustzone version number */
	ret = zynqmp_pm_get_trustzone_version(&pm_tz_version);
	if (ret)
		panic("Legacy trustzone found without version support\n");

	if (pm_tz_version < ZYNQMP_TZ_VERSION)
		panic("%s Trustzone version error. Expected: v%d.%d - Found: v%d.%d\n",
		      __func__,
		      ZYNQMP_TZ_VERSION_MAJOR, ZYNQMP_TZ_VERSION_MINOR,
		      pm_tz_version >> 16, pm_tz_version & 0xFFFF);

	pr_info("%s Trustzone version v%d.%d\n", __func__,
		pm_tz_version >> 16, pm_tz_version & 0xFFFF);

	/* Assign eemi_ops_table */
	eemi_ops_tbl = &eemi_ops;

	ret = zynqmp_pm_sysfs_init();
	if (ret) {
		pr_err("%s() sysfs init fail with error %d\n", __func__, ret);
		return ret;
	}

	zynqmp_pm_api_debugfs_init();

	ret = mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE, firmware_devs,
			      ARRAY_SIZE(firmware_devs), NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to add MFD devices %d\n", ret);
		return ret;
	}

	return of_platform_populate(dev->of_node, NULL, NULL, dev);
}

static int zynqmp_firmware_remove(struct platform_device *pdev)
{
	mfd_remove_devices(&pdev->dev);
	zynqmp_pm_api_debugfs_exit();

	return 0;
}

static const struct of_device_id zynqmp_firmware_of_match[] = {
	{.compatible = "xlnx,zynqmp-firmware"},
	{.compatible = "xlnx,versal-firmware-wip"},
	{},
};
MODULE_DEVICE_TABLE(of, zynqmp_firmware_of_match);

static struct platform_driver zynqmp_firmware_driver = {
	.driver = {
		.name = "zynqmp_firmware",
		.of_match_table = zynqmp_firmware_of_match,
	},
	.probe = zynqmp_firmware_probe,
	.remove = zynqmp_firmware_remove,
};
module_platform_driver(zynqmp_firmware_driver);
