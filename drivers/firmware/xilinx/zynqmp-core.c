// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Zynq MPSoC Firmware layer core APIs.
 *
 * Copyright (C), 2025 Advanced Micro Devices, Inc.
 */

#include <linux/arm-smccc.h>
#include <linux/dma-mapping.h>
#include <linux/hashtable.h>
#include <linux/mfd/core.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <linux/firmware/xlnx-zynqmp.h>
#include "zynqmp-debug.h"

/* Max HashMap Order for PM API feature check (1<<7 = 128) */
#define PM_API_FEATURE_CHECK_MAX_ORDER	7

/* IOCTL/QUERY feature payload size */
#define FEATURE_PAYLOAD_SIZE		2

static bool feature_check_enabled;
static DEFINE_HASHTABLE(pm_api_features_map, PM_API_FEATURE_CHECK_MAX_ORDER);
static u32 ioctl_features[FEATURE_PAYLOAD_SIZE];
static u32 query_features[FEATURE_PAYLOAD_SIZE];

static u32 sip_svc_version;
static struct platform_device *em_dev;

static const struct mfd_cell firmware_devs[] = {
	{
		.name = "zynqmp_power_controller",
	},
};

/**
 * struct pm_api_feature_data - PM API Feature data
 * @pm_api_id:		PM API Id, used as key to index into hashmap
 * @feature_status:	status of PM API feature: valid, invalid
 * @hentry:		hlist_node that hooks this entry into hashtable
 */
struct pm_api_feature_data {
	u32 pm_api_id;
	int feature_status;
	struct hlist_node hentry;
};

struct platform_fw_data {
	/*
	 * Invokes the platform-specific feature check PM FW API call.
	 * Uses either the basic or extended SMCCC frame format based on the
	 * platform.
	 */
	int (*do_feature_check)(const u32 api_id, u32 *ret_payload);

	/*
	 * Invokes all other platform-specific PM FW APIs.
	 * Uses either the basic or extended SMCCC frame format based
	 * on the platform.
	 */
	int (*zynqmp_pm_fw_call)(u32 pm_api_id, u32 *ret_payload,
				 u32 num_args, va_list *arg_list);

	/*
	 * Prepares the PLM command header for the platform.
	 * The header will either use the PM_API_FEATURES or PM_FEATURE_CHECK,
	 * depending on the platform.
	 */
	uint64_t (*prep_pm_cmd_header)(u32 module_id);

	/*
	 * Indicates whether the word swap required for the memory address
	 * while loading PDI image based on the platform
	 */
	bool load_pdi_word_swap;
};

static struct platform_fw_data *active_platform_fw_data;

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
	case XST_PM_INVALID_VERSION:
		return -EOPNOTSUPP;
	case XST_PM_NO_ACCESS:
		return -EACCES;
	case XST_PM_ABORT_SUSPEND:
		return -ECANCELED;
	case XST_PM_MULT_USER:
		return -EUSERS;
	case XST_PM_INTERNAL:
	case XST_PM_CONFLICT:
	case XST_PM_INVALID_NODE:
	case XST_PM_INVALID_CRC:
	default:
		return -EINVAL;
	}
}

static noinline int do_fw_call_fail(u32 *ret_payload, u32 num_args, ...)
{
	return -ENODEV;
}

/*
 * PM function call wrapper
 * Invoke do_fw_call_smc or do_fw_call_hvc, depending on the configuration
 */
static int (*do_fw_call)(u32 *ret_payload, u32, ...) = do_fw_call_fail;

/**
 * do_fw_call_smc() - Call system-level platform management layer (SMC)
 * @num_args:		Number of variable arguments should be <= 8
 * @ret_payload:	Returned value array
 *
 * Invoke platform management function via SMC call (no hypervisor present).
 *
 * Return: Returns status, either success or error+reason
 */
static noinline int do_fw_call_smc(u32 *ret_payload, u32 num_args, ...)
{
	struct arm_smccc_res res;
	u64 args[8] = {0};
	va_list arg_list;
	u8 i;

	if (num_args > 8)
		return -EINVAL;

	va_start(arg_list, num_args);

	for (i = 0; i < num_args; i++)
		args[i] = va_arg(arg_list, u64);

	va_end(arg_list);

	arm_smccc_smc(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], &res);

	if (ret_payload) {
		ret_payload[0] = lower_32_bits(res.a0);
		ret_payload[1] = upper_32_bits(res.a0);
		ret_payload[2] = lower_32_bits(res.a1);
		ret_payload[3] = upper_32_bits(res.a1);
		ret_payload[4] = lower_32_bits(res.a2);
		ret_payload[5] = upper_32_bits(res.a2);
		ret_payload[6] = lower_32_bits(res.a3);
	}

	return zynqmp_pm_ret_code((enum pm_ret_status)res.a0);
}

/**
 * do_fw_call_hvc() - Call system-level platform management layer (HVC)
 * @num_args:		Number of variable arguments should be <= 8
 * @ret_payload:	Returned value array
 *
 * Invoke platform management function via HVC
 * HVC-based for communication through hypervisor
 * (no direct communication with ATF).
 *
 * Return: Returns status, either success or error+reason
 */
static noinline int do_fw_call_hvc(u32 *ret_payload, u32 num_args, ...)
{
	struct arm_smccc_res res;
	u64 args[8] = {0};
	va_list arg_list;
	u8 i;

	if (num_args > 8)
		return -EINVAL;

	va_start(arg_list, num_args);

	for (i = 0; i < num_args; i++)
		args[i] = va_arg(arg_list, u64);

	va_end(arg_list);

	arm_smccc_hvc(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], &res);

	if (ret_payload) {
		ret_payload[0] = lower_32_bits(res.a0);
		ret_payload[1] = upper_32_bits(res.a0);
		ret_payload[2] = lower_32_bits(res.a1);
		ret_payload[3] = upper_32_bits(res.a1);
		ret_payload[4] = lower_32_bits(res.a2);
		ret_payload[5] = upper_32_bits(res.a2);
		ret_payload[6] = lower_32_bits(res.a3);
	}

	return zynqmp_pm_ret_code((enum pm_ret_status)res.a0);
}

static uint64_t prep_pm_hdr_feature_check(u32 module_id)
{
	/* Ignore module_id argument but it is PM_MODULE_ID=0 used here */
	return PM_SIP_SVC | PM_FEATURE_CHECK;
}

static uint64_t prep_pm_hdr_api_features(u32 module_id)
{
	return PM_SIP_SVC | FIELD_PREP(MODULE_ID_MASK, module_id) | PM_API_FEATURES;
}

/**
 * do_feature_check_for_tfa_apis - Perform feature check for TF-A APIs.
 * @api_id: API ID to be checked.
 * @ret_payload: Pointer to store the firmware's response payload.
 *
 * Prepares the command header and payload for TF-A APIs and makes the FW call
 *
 * Return:
 * - 0 on success
 * - -EOPNOTSUPP if the firmware call fails.
 * - -ENODEV if the active_platform_fw_data is NULL.
 */
static int do_feature_check_for_tfa_apis(const u32 api_id, u32 *ret_payload)
{
	u32 module_id;
	u64 smc_arg[2];
	int ret;

	if (!active_platform_fw_data)
		return -ENODEV;

	module_id = FIELD_GET(MODULE_ID_MASK, api_id);

	smc_arg[0] = active_platform_fw_data->prep_pm_cmd_header(module_id);
	smc_arg[1] = api_id;

	ret = do_fw_call(ret_payload, 2, smc_arg[0], smc_arg[1]);

	if (ret)
		return -EOPNOTSUPP;

	return ret_payload[1];
}

/**
 * do_feature_check_extended - Perform feature check for an API ID
 *				 using extended SMCCC format.
 * @api_id: API ID to be checked.
 * @ret_payload: Pointer to store the firmware's response payload.
 *
 * Determines the appropriate API (PM_FEATURE_CHECK or PM_API_FEATURES) based on
 * the module ID in the given API ID. Frames the arguments in the extended
 * SMCCC format, executes the firmware call, and processes the result.
 *
 * Return:
 * - 0 on success
 * - -EOPNOTSUPP if the firmware call fails.
 */
static int do_feature_check_extended(const u32 api_id, u32 *ret_payload)
{
	int ret;
	u64 smc_arg[2];
	u32 module_id;
	u32 feature_check_api_id;

	module_id = FIELD_GET(MODULE_ID_MASK, api_id);

	/*
	 * Feature check of APIs belonging to PM and XSEM are handled by calling
	 * PM_FEATURE_CHECK API. For other modules, call PM_API_FEATURES API.
	 */
	if (module_id == PM_MODULE_ID || module_id == XSEM_MODULE_ID)
		feature_check_api_id = PM_FEATURE_CHECK;
	else
		feature_check_api_id = PM_API_FEATURES;

	if (module_id == PM_MODULE_ID)
		module_id = XPM_MODULE_ID;

	/* Frame extended SMC format */
	smc_arg[0] = PM_SIP_SVC | PASS_THROUGH_FW_CMD_ID;
	smc_arg[1] = ((api_id & API_ID_MASK)  << 32) |
		      FIELD_PREP(MODULE_ID_MASK, module_id) |
		      feature_check_api_id;

	ret = do_fw_call(ret_payload, 2, smc_arg[0], smc_arg[1]);
	if (ret)
		return -EOPNOTSUPP;

	return ret_payload[1];
}

/**
 * do_feature_check_basic - Perform feature check for an API ID with
 *			    basic SMC format.
 * @api_id: API ID to be checked.
 * @ret_payload: Pointer to store the firmware's response payload.
 *
 * Determines the appropriate API (PM_FEATURE_CHECK or PM_API_FEATURES) based on
 * the module ID in the given API ID. Frames the SMC call arguments in the basic
 * format, executes the firmware call, and processes the result.
 *
 * Return: Returns status, either success or error+reason
 */
static int do_feature_check_basic(const u32 api_id, u32 *ret_payload)
{
	u32 module_id;
	u64 smc_arg[2];
	u32 feature_check_api_id;
	int ret;

	module_id = FIELD_GET(MODULE_ID_MASK, api_id);

	/*
	 * Feature check of APIs belonging to PM, XSEM are handled by calling
	 * PM_FEATURE_CHECK API. For other modules, call PM_API_FEATURES API.
	 */
	if (module_id == PM_MODULE_ID || module_id == XSEM_MODULE_ID)
		feature_check_api_id = PM_FEATURE_CHECK;
	else
		feature_check_api_id = PM_API_FEATURES;

	smc_arg[0] = PM_SIP_SVC | FIELD_PREP(MODULE_ID_MASK, module_id) | feature_check_api_id;
	smc_arg[1] = (api_id & API_ID_MASK);

	ret = do_fw_call(ret_payload, 2, smc_arg[0], smc_arg[1]);
	if (ret)
		ret = -EOPNOTSUPP;
	else
		ret = ret_payload[1];

	return ret;
}

/**
 * dispatch_feature_check - Dispatch feature check based on module ID.
 * @api_id: API ID to be checked.
 * @ret_payload: Pointer to store the firmware's response payload.
 *
 * Determines the appropriate feature check function to call based on the
 * module ID extracted from the API ID. If the module ID corresponds to
 * TF-A, it calls do_feature_check_for_tfa_apis(); otherwise, it calls
 * do_feature_check_basic which uses basic SMCCC format
 *
 * Return: Returns status, either success or error+reason
 */
static int dispatch_feature_check(const u32 api_id, u32 *ret_payload)
{
	u32 module_id;

	module_id = FIELD_GET(MODULE_ID_MASK, api_id);

	if (module_id == TF_A_MODULE_ID)
		return do_feature_check_for_tfa_apis(api_id, ret_payload);

	if (active_platform_fw_data)
		return active_platform_fw_data->do_feature_check(api_id, ret_payload);

	return -ENODEV;
}

static int do_feature_check_call(const u32 api_id)
{
	int ret;
	u32 ret_payload[PAYLOAD_ARG_CNT];
	struct pm_api_feature_data *feature_data;

	/* Check for existing entry in hash table for given api */
	hash_for_each_possible(pm_api_features_map, feature_data, hentry,
			       api_id) {
		if (feature_data->pm_api_id == api_id)
			return feature_data->feature_status;
	}

	/* Add new entry if not present */
	feature_data = kmalloc(sizeof(*feature_data), GFP_ATOMIC);
	if (!feature_data)
		return -ENOMEM;

	feature_data->pm_api_id = api_id;
	ret = dispatch_feature_check(api_id, ret_payload);

	feature_data->feature_status = ret;
	hash_add(pm_api_features_map, &feature_data->hentry, api_id);

	if (api_id == PM_IOCTL)
		/* Store supported IOCTL IDs mask */
		memcpy(ioctl_features, &ret_payload[2], FEATURE_PAYLOAD_SIZE * 4);
	else if (api_id == PM_QUERY_DATA)
		/* Store supported QUERY IDs mask */
		memcpy(query_features, &ret_payload[2], FEATURE_PAYLOAD_SIZE * 4);

	return ret;
}

/**
 * zynqmp_pm_feature() - Check whether given feature is supported or not and
 *			 store supported IOCTL/QUERY ID mask
 * @api_id:		API ID to check
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_feature(const u32 api_id)
{
	int ret;

	if (!feature_check_enabled)
		return 0;

	ret = do_feature_check_call(api_id);

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_feature);

/**
 * zynqmp_pm_is_function_supported() - Check whether given IOCTL/QUERY function
 *				       is supported or not
 * @api_id:		PM_IOCTL or PM_QUERY_DATA
 * @id:			IOCTL or QUERY function IDs
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_is_function_supported(const u32 api_id, const u32 id)
{
	int ret;
	u32 *bit_mask;

	/* Input arguments validation */
	if (id >= 64 || (api_id != PM_IOCTL && api_id != PM_QUERY_DATA))
		return -EINVAL;

	/* Check feature check API version */
	ret = do_feature_check_call(PM_FEATURE_CHECK);
	if (ret < 0)
		return ret;

	/* Check if feature check version 2 is supported or not */
	if ((ret & FIRMWARE_VERSION_MASK) == PM_API_VERSION_2) {
		/*
		 * Call feature check for IOCTL/QUERY API to get IOCTL ID or
		 * QUERY ID feature status.
		 */
		ret = do_feature_check_call(api_id);
		if (ret < 0)
			return ret;

		bit_mask = (api_id == PM_IOCTL) ? ioctl_features : query_features;

		if ((bit_mask[(id / 32)] & BIT((id % 32))) == 0U)
			return -EOPNOTSUPP;
	} else {
		return -ENODATA;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_is_function_supported);

/**
 * __zynqmp_pm_fw_call_extended() - Invoke the system-level platform management layer
 *			caller function depending on the configuration
 * @pm_api_id:		Requested PM-API call
 * @ret_payload:	Returned value array
 * @num_args:		Number of arguments to requested PM-API call
 * @arg_list:		va_list initialized with va_start, containing arguments passed
 *			to the firmware.
 *
 * Invoke platform management function for SMC or HVC call, depending on
 * configuration.
 * Following SMC Calling Convention (SMCCC) for SMC64:
 * Pm Function Identifier,
 * PM_SIP_SVC + PASS_THROUGH_FW_CMD_ID =
 *	((SMC_TYPE_FAST << FUNCID_TYPE_SHIFT)
 *	((SMC_64) << FUNCID_CC_SHIFT)
 *	((SIP_START) << FUNCID_OEN_SHIFT)
 *	(PASS_THROUGH_FW_CMD_ID))
 *
 * PM_SIP_SVC - Registered ZynqMP SIP Service Call.
 * PASS_THROUGH_FW_CMD_ID - Fixed SiP SVC call ID for FW specific calls.
 *
 * Return: Returns status, either success or error+reason
 */
static int __zynqmp_pm_fw_call_extended(u32 pm_api_id, u32 *ret_payload,
					u32 num_args, va_list *arg_list)
{
	/*
	 * Added SIP service call Function Identifier
	 * Make sure to stay in x0 register
	 */
	u64 smc_arg[SMC_ARG_CNT_64];
	int ret, i;
	u32 args[SMC_ARG_CNT_32] = {0};
	u32 module_id;

	/*
	 * According to the SMCCC: The total number of registers available for
	 * arguments is 16.
	 *
	 * In the Extended SMC format, 3 registers are used for headers, leaving
	 * up to 13 registers for arguments.
	 */
	if (num_args > SMC_ARG_CNT_32)
		return -EINVAL;

	/* Check if feature is supported or not */
	ret = zynqmp_pm_feature(pm_api_id);
	if (ret < 0)
		return ret;

	for (i = 0; i < num_args; i++)
		args[i] = va_arg(*arg_list, u32);

	module_id = FIELD_GET(PLM_MODULE_ID_MASK, pm_api_id);

	if (module_id == 0)
		module_id = XPM_MODULE_ID;

	smc_arg[0] = PM_SIP_SVC | PASS_THROUGH_FW_CMD_ID;
	smc_arg[1] = ((u64)args[0] << 32U) | FIELD_PREP(PLM_MODULE_ID_MASK, module_id) |
		      (pm_api_id & API_ID_MASK);
	for (i = 1; i < (SMC_ARG_CNT_64 - 1); i++)
		smc_arg[i + 1] = ((u64)args[(i * 2)] << 32U) | args[(i * 2) - 1];

	return do_fw_call(ret_payload, 8, smc_arg[0], smc_arg[1], smc_arg[2], smc_arg[3],
			  smc_arg[4], smc_arg[5], smc_arg[6], smc_arg[7]);
}

/**
 * zynqmp_pm_fw_call_extended - Invoke a PM function with variable arguments
 * @pm_api_id: ID of the PM API to be called
 * @ret_payload: Pointer to the buffer for storing the return payload
 * @num_args: Number of arguments to pass to the PM API function
 *
 * This function serves as a wrapper around zynqmp_pm_invoke_fn_extended(),
 * facilitating the invocation of platform management (PM) functions that
 * require an extended SMC (Secure Monitor Call) format with variable
 * arguments. Specifically, the PM_QUERY_DATA API necessitates this extended
 * payload format, making it essential to retain zynqmp_pm_fw_call_extended
 * with variable arguments.
 *
 * Return: 0 on success; a negative error code on failure.
 */
int zynqmp_pm_fw_call_extended(u32 pm_api_id, u32 *ret_payload, u32 num_args, ...)
{
	va_list arg_list;
	int ret;

	va_start(arg_list, num_args);
	ret = __zynqmp_pm_fw_call_extended(pm_api_id, ret_payload,
					   num_args, &arg_list);
	va_end(arg_list);
	return ret;
}

/**
 * __zynqmp_pm_fw_call_basic() - Invoke the system-level platform management layer
 *				 supporting basic SMC format.
 *
 * @pm_api_id:		Requested PM-API call
 * @ret_payload:	Returned value array
 * @num_args:		Number of arguments to requested PM-API call
 * @arg_list:		va_list initialized with va_start, containing arguments passed
 *			to the firmware.
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
static int __zynqmp_pm_fw_call_basic(u32 pm_api_id, u32 *ret_payload,
				     u32 num_args, va_list *arg_list)
{
	/*
	 * Added SIP service call Function Identifier
	 * Make sure to stay in x0 register
	 */
	u64 smc_arg[8];
	int ret, i;
	u32 args[SMC_ARG_CNT_BASIC_32] = {0};

	/*
	 * According to the SMCCC: The total number of registers available for
	 * arguments is 16.
	 *
	 * In the Basic SMC format, 2 registers are used for headers, leaving
	 * up to 14 registers for arguments.
	 */
	if (num_args > SMC_ARG_CNT_BASIC_32)
		return -EINVAL;

	/* Check if feature is supported or not */
	ret = zynqmp_pm_feature(pm_api_id);
	if (ret < 0)
		return ret;

	for (i = 0; i < num_args; i++)
		args[i] = va_arg(*arg_list, u32);

	smc_arg[0] = PM_SIP_SVC | pm_api_id;
	for (i = 0; i < 7; i++)
		smc_arg[i + 1] = ((u64)args[(i * 2) + 1] << 32) | args[i * 2];

	return do_fw_call(ret_payload, 8, smc_arg[0], smc_arg[1], smc_arg[2], smc_arg[3],
			  smc_arg[4], smc_arg[5], smc_arg[6], smc_arg[7]);
}

/**
 * zynqmp_pm_invoke_fn() - Invokes the platform-specific PM FW API.
 * @pm_api_id:		Requested PM-API call
 * @ret_payload:	Returned value array
 * @num_args:		Number of arguments to requested PM-API call
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_invoke_fn(u32 pm_api_id, u32 *ret_payload, u32 num_args, ...)
{
	va_list arg_list;
	u32 module_id;
	int ret = -ENODEV;

	/*
	 * According to the SMCCC: The total number of registers available for
	 * arguments is 16.
	 *
	 * In the Basic SMC format, 2 registers are used for headers, leaving
	 * up to 14 registers for arguments.
	 *
	 * In the Extended SMC format, 3 registers are used for headers, leaving
	 * up to 13 registers for arguments.
	 *
	 * To accommodate both formats, this comparison imposes a limit of 14
	 * arguments. This ensures that callers do not exceed the maximum number
	 * of registers available for arguments in either format. Each specific
	 * handler (basic or extended) will further validate the exact number of
	 * arguments based on its respective format requirements.
	 */
	if (num_args > 14)
		return -EINVAL;

	va_start(arg_list, num_args);

	module_id = FIELD_GET(MODULE_ID_MASK, pm_api_id);

	/*
	 * Invoke the platform-specific PM FW API.
	 * based on the platform type.
	 *
	 * The only exception is the TF-A module, which supports the basic
	 * SMC format only
	 */
	if (module_id == TF_A_MODULE_ID)
		ret = __zynqmp_pm_fw_call_basic(pm_api_id, ret_payload, num_args, &arg_list);
	else
		if (active_platform_fw_data)
			ret = active_platform_fw_data->zynqmp_pm_fw_call(pm_api_id, ret_payload,
									 num_args, &arg_list);

	va_end(arg_list);
	return ret;
}

/**
 * zynqmp_pm_load_pdi_word_swap - Perform word swapping on a memory address.
 * @address: Memory address to be word-swapped.
 * @swapped_address: Pointer to store the resulting swapped address.
 *
 * This function checks if the active platform's firmware data specifies that
 * word swapping is required when loading a Programmable Device Image (PDI).
 * If so, it performs the necessary word swapping on the provided memory
 * address. The swapped address is stored in the provided pointer.
 *
 * Return:
 * - 0 on success.
 * - -ENODEV if the active_platform_fw_data is NULL.
 */
int zynqmp_pm_load_pdi_word_swap(const u64 address, u64 *swapped_address)
{
	if (!active_platform_fw_data)
		return -ENODEV;

	if (active_platform_fw_data->load_pdi_word_swap)
		*swapped_address = (address << 32) | (address >> 32);
	else
		*swapped_address = address;

	return 0;
}

/**
 * zynqmp_pm_get_sip_svc_version() - Get SiP service call version
 * @version:	Returned version value
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_get_sip_svc_version(u32 *version)
{
	struct arm_smccc_res res;
	u64 args[SMC_ARG_CNT_64] = {0};

	if (!version)
		return -EINVAL;

	/* Check if SiP SVC version already verified */
	if (sip_svc_version > 0) {
		*version = sip_svc_version;
		return 0;
	}

	args[0] = GET_SIP_SVC_VERSION;

	arm_smccc_smc(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], &res);

	*version = ((lower_32_bits(res.a0) << 16U) | lower_32_bits(res.a1));

	return zynqmp_pm_ret_code(XST_PM_SUCCESS);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_sip_svc_version);

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

static int zynqmp_firmware_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zynqmp_devinfo *devinfo;
	u32 pm_api_version;
	u32 pm_tz_version;
	u32 pm_family_code;
	u32 pm_sub_family_code;
	int ret;

	ret = get_set_conduit_method(dev->of_node);
	if (ret)
		return ret;

	active_platform_fw_data = (struct platform_fw_data *)device_get_match_data(dev);
	if (!active_platform_fw_data)
		return -EINVAL;

	/* Get SiP SVC version number */
	ret = zynqmp_pm_get_sip_svc_version(&sip_svc_version);
	if (ret)
		return ret;

	ret = do_feature_check_call(PM_FEATURE_CHECK);
	if (ret >= 0 && ((ret & FIRMWARE_VERSION_MASK) >= PM_API_VERSION_1))
		feature_check_enabled = true;

	devinfo = devm_kzalloc(dev, sizeof(*devinfo), GFP_KERNEL);
	if (!devinfo)
		return -ENOMEM;

	devinfo->dev = dev;

	platform_set_drvdata(pdev, devinfo);

	/* Check PM API version number */
	ret = zynqmp_pm_get_api_version(&pm_api_version);
	if (ret)
		return ret;

	if (pm_api_version < ZYNQMP_PM_VERSION) {
		panic("%s Platform Management API version error. Expected: v%d.%d - Found: v%d.%d\n",
		      __func__,
		      ZYNQMP_PM_VERSION_MAJOR, ZYNQMP_PM_VERSION_MINOR,
		      pm_api_version >> 16, pm_api_version & 0xFFFF);
	}

	pr_info("%s Platform Management API v%d.%d\n", __func__,
		pm_api_version >> 16, pm_api_version & 0xFFFF);

	/* Get the Family code and sub family code of platform */
	ret = zynqmp_pm_get_family_info(&pm_family_code, &pm_sub_family_code);
	if (ret < 0)
		return ret;

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

	ret = mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE, firmware_devs,
			      ARRAY_SIZE(firmware_devs), NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to add MFD devices %d\n", ret);
		return ret;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret < 0) {
		dev_err(dev, "no usable DMA configuration");
		return ret;
	}

	ret = zynqmp_firmware_pm_sysfs_entry(pdev);
	if (ret) {
		pr_err("%s() Failed to create sysfs file with error%d\n",
		       __func__, ret);
		return ret;
	}

	ret = zynqmp_firmware_pdi_sysfs_entry(pdev);
	if (ret) {
		pr_err("%s() Failed to create sysfs binary file with error%d\n",
		       __func__, ret);
		return ret;
	}

	zynqmp_pm_api_debugfs_init();

	if (pm_family_code == VERSAL_FAMILY_CODE) {
		em_dev = platform_device_register_data(&pdev->dev, "xlnx_event_manager",
						       -1, NULL, 0);
		if (IS_ERR(em_dev))
			dev_err_probe(&pdev->dev, PTR_ERR(em_dev), "EM register fail with error\n");
	}

	return of_platform_populate(dev->of_node, NULL, NULL, dev);
}

static void zynqmp_firmware_remove(struct platform_device *pdev)
{
	struct pm_api_feature_data *feature_data;
	struct hlist_node *tmp;
	int i;

	mfd_remove_devices(&pdev->dev);
	zynqmp_pm_api_debugfs_exit();

	hash_for_each_safe(pm_api_features_map, i, tmp, feature_data, hentry) {
		hash_del(&feature_data->hentry);
		kfree(feature_data);
	}

	platform_device_unregister(em_dev);
}

static const struct platform_fw_data platform_fw_data_versal2 = {
	.do_feature_check = do_feature_check_extended,
	.zynqmp_pm_fw_call = __zynqmp_pm_fw_call_extended,
	.prep_pm_cmd_header = prep_pm_hdr_api_features,
	/* TF-A does only transparent forwarding do word swapping here */
	.load_pdi_word_swap = true,
};

static const struct platform_fw_data platform_fw_data_zynqmp_and_versal = {
	.do_feature_check = do_feature_check_basic,
	.zynqmp_pm_fw_call = __zynqmp_pm_fw_call_basic,
	.prep_pm_cmd_header = prep_pm_hdr_feature_check,
	/* the word swapping is done in TF-A */
	.load_pdi_word_swap = false,
};

static const struct of_device_id zynqmp_firmware_of_match[] = {
	{
		.compatible = "xlnx,zynqmp-firmware",
		.data = &platform_fw_data_zynqmp_and_versal,
	},
	{
		.compatible = "xlnx,versal-firmware",
		.data = &platform_fw_data_zynqmp_and_versal,
	},
	{
		.compatible = "xlnx,versal2-firmware",
		.data = &platform_fw_data_versal2,
	},
	{},
};
MODULE_DEVICE_TABLE(of, zynqmp_firmware_of_match);

static struct platform_driver zynqmp_firmware_driver = {
	.driver = {
		.name = "zynqmp_firmware",
		.of_match_table = zynqmp_firmware_of_match,
	},
	.probe = zynqmp_firmware_probe,
	.remove_new = zynqmp_firmware_remove,
};
module_platform_driver(zynqmp_firmware_driver);
