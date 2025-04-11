// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2014-2022 Xilinx, Inc.
 *  Copyright (C) 2022 - 2025, Advanced Micro Devices, Inc.
 *
 *  Michal Simek <michal.simek@amd.com>
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajanv@xilinx.com>
 */

#include <linux/arm-smccc.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/hashtable.h>

#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/firmware/xlnx-event-manager.h>
#include "zynqmp-debug.h"

/* CRL registers and bitfields */
#define CRL_APB_BASE			0xFF5E0000U
/* BOOT_PIN_CTRL- Used to control the mode pins after boot */
#define CRL_APB_BOOT_PIN_CTRL		(CRL_APB_BASE + (0x250U))
/* BOOT_PIN_CTRL_MASK- out_val[11:8], out_en[3:0] */
#define CRL_APB_BOOTPIN_CTRL_MASK	0xF0FU

static unsigned long register_address;

int zynqmp_pm_register_sgi(u32 sgi_num, u32 reset)
{
	int ret;

	ret = zynqmp_pm_invoke_fn(TF_A_PM_REGISTER_SGI, NULL, 2, sgi_num, reset);
	if (ret != -EOPNOTSUPP && !ret)
		return ret;

	/* try old implementation as fallback strategy if above fails */
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 3, IOCTL_REGISTER_SGI, sgi_num, reset);
}

/**
 * zynqmp_pm_get_api_version() - Get version number of PMU PM firmware
 * @version:	Returned version value
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_get_api_version(u32 *version)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	static u32 pm_api_version;
	int ret;

	if (!version)
		return -EINVAL;

	/* Check is PM API version already verified */
	if (pm_api_version > 0) {
		*version = pm_api_version;
		return 0;
	}
	ret = zynqmp_pm_invoke_fn(PM_GET_API_VERSION, ret_payload, 0);
	*version = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_api_version);

/**
 * zynqmp_pm_get_chipid - Get silicon ID registers
 * @idcode:     IDCODE register
 * @version:    version register
 *
 * Return:      Returns the status of the operation and the idcode and version
 *              registers in @idcode and @version.
 */
int zynqmp_pm_get_chipid(u32 *idcode, u32 *version)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!idcode || !version)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_GET_CHIPID, ret_payload, 0);
	*idcode = ret_payload[1];
	*version = ret_payload[2];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_chipid);

/**
 * zynqmp_pm_get_family_info() - Get family info of platform
 * @family:	Returned family code value
 * @subfamily:	Returned sub-family code value
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_get_family_info(u32 *family, u32 *subfamily)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	u32 idcode;
	static u32 pm_family_code;
	static u32 pm_sub_family_code;
	int ret;

	/* Check is family or sub-family code already received */
	if (pm_family_code && pm_sub_family_code) {
		*family = pm_family_code;
		*subfamily = pm_sub_family_code;
		return 0;
	}

	ret = zynqmp_pm_invoke_fn(PM_GET_CHIPID, ret_payload, 0);
	if (ret < 0)
		return ret;

	idcode = ret_payload[1];
	pm_family_code = FIELD_GET(FAMILY_CODE_MASK, idcode);
	pm_sub_family_code = FIELD_GET(SUB_FAMILY_CODE_MASK, idcode);
	*family = pm_family_code;
	*subfamily = pm_sub_family_code;

	return 0;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_family_info);

/**
 * zynqmp_pm_get_trustzone_version() - Get secure trustzone firmware version
 * @version:	Returned version value
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_get_trustzone_version(u32 *version)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	static u32 pm_tz_version;
	int ret;

	if (!version)
		return -EINVAL;

	/* Check is PM trustzone version already verified */
	if (pm_tz_version > 0) {
		*version = pm_tz_version;
		return 0;
	}
	ret = zynqmp_pm_invoke_fn(PM_GET_TRUSTZONE_VERSION, ret_payload, 0);
	*version = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_trustzone_version);

/**
 * zynqmp_pm_query_data() - Get query data from firmware
 * @qdata:	Variable to the zynqmp_pm_query_data structure
 * @out:	Returned output value
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_query_data(struct zynqmp_pm_query_data qdata, u32 *out)
{
	int ret, i = 0;
	u32 sip_svc_version;
	u32 ret_payload[PAYLOAD_ARG_CNT] = {0};

	/* Get SiP SVC version number */
	ret = zynqmp_pm_get_sip_svc_version(&sip_svc_version);
	if (ret)
		return ret;

	if (sip_svc_version >= SIP_SVC_PASSTHROUGH_VERSION) {
		ret = zynqmp_pm_fw_call_extended(PM_QUERY_DATA, ret_payload, 4,
						 qdata.qid, qdata.arg1,
						 qdata.arg2, qdata.arg3);
		/* To support backward compatibility */
		if (!ret && !ret_payload[0]) {
			/*
			 * TF-A passes return status on 0th index but
			 * api to get clock name reads data from 0th
			 * index so pass data at 0th index instead of
			 * return status
			 */
			if (qdata.qid == PM_QID_CLOCK_GET_NAME ||
			    qdata.qid == PM_QID_PINCTRL_GET_FUNCTION_NAME)
				i = 1;

			for (; i < PAYLOAD_ARG_CNT; i++, out++)
				*out = ret_payload[i];

			return ret;
		}
	}

	ret = zynqmp_pm_invoke_fn(PM_QUERY_DATA, out, 4, qdata.qid,
				  qdata.arg1, qdata.arg2, qdata.arg3);

	/*
	 * For clock name query, all bytes in SMC response are clock name
	 * characters and return code is always success. For invalid clocks,
	 * clock name bytes would be zeros.
	 */
	return qdata.qid == PM_QID_CLOCK_GET_NAME ? 0 : ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_query_data);

/**
 * zynqmp_pm_clock_enable() - Enable the clock for given id
 * @clock_id:	ID of the clock to be enabled
 *
 * This function is used by master to enable the clock
 * including peripherals and PLL clocks.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_clock_enable(u32 clock_id)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_ENABLE, NULL, 1, clock_id);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_enable);

/**
 * zynqmp_pm_clock_disable() - Disable the clock for given id
 * @clock_id:	ID of the clock to be disable
 *
 * This function is used by master to disable the clock
 * including peripherals and PLL clocks.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_clock_disable(u32 clock_id)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_DISABLE, NULL, 1, clock_id);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_disable);

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
int zynqmp_pm_clock_getstate(u32 clock_id, u32 *state)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_CLOCK_GETSTATE, ret_payload, 1, clock_id);
	*state = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_getstate);

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
int zynqmp_pm_clock_setdivider(u32 clock_id, u32 divider)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_SETDIVIDER, NULL, 2, clock_id, divider);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_setdivider);

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
int zynqmp_pm_clock_getdivider(u32 clock_id, u32 *divider)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_CLOCK_GETDIVIDER, ret_payload, 1, clock_id);
	*divider = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_getdivider);

/**
 * zynqmp_pm_clock_setparent() - Set the clock parent for given id
 * @clock_id:	ID of the clock
 * @parent_id:	parent id
 *
 * This function is used by master to set parent for any clock.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_clock_setparent(u32 clock_id, u32 parent_id)
{
	return zynqmp_pm_invoke_fn(PM_CLOCK_SETPARENT, NULL, 2, clock_id, parent_id);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_setparent);

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
int zynqmp_pm_clock_getparent(u32 clock_id, u32 *parent_id)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_CLOCK_GETPARENT, ret_payload, 1, clock_id);
	*parent_id = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_clock_getparent);

/**
 * zynqmp_pm_set_pll_frac_mode() - PM API for set PLL mode
 *
 * @clk_id:	PLL clock ID
 * @mode:	PLL mode (PLL_MODE_FRAC/PLL_MODE_INT)
 *
 * This function sets PLL mode
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_set_pll_frac_mode(u32 clk_id, u32 mode)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 4, 0, IOCTL_SET_PLL_FRAC_MODE, clk_id, mode);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_pll_frac_mode);

/**
 * zynqmp_pm_get_pll_frac_mode() - PM API for get PLL mode
 *
 * @clk_id:	PLL clock ID
 * @mode:	PLL mode
 *
 * This function return current PLL mode
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_get_pll_frac_mode(u32 clk_id, u32 *mode)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, mode, 3, 0, IOCTL_GET_PLL_FRAC_MODE, clk_id);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_pll_frac_mode);

/**
 * zynqmp_pm_set_pll_frac_data() - PM API for setting pll fraction data
 *
 * @clk_id:	PLL clock ID
 * @data:	fraction data
 *
 * This function sets fraction data.
 * It is valid for fraction mode only.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_set_pll_frac_data(u32 clk_id, u32 data)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 4, 0, IOCTL_SET_PLL_FRAC_DATA, clk_id, data);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_pll_frac_data);

/**
 * zynqmp_pm_get_pll_frac_data() - PM API for getting pll fraction data
 *
 * @clk_id:	PLL clock ID
 * @data:	fraction data
 *
 * This function returns fraction data value.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_get_pll_frac_data(u32 clk_id, u32 *data)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, data, 3, 0, IOCTL_GET_PLL_FRAC_DATA, clk_id);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_pll_frac_data);

/**
 * zynqmp_pm_set_sd_tapdelay() -  Set tap delay for the SD device
 *
 * @node_id:	Node ID of the device
 * @type:	Type of tap delay to set (input/output)
 * @value:	Value to set fot the tap delay
 *
 * This function sets input/output tap delay for the SD device.
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_set_sd_tapdelay(u32 node_id, u32 type, u32 value)
{
	u32 reg = (type == PM_TAPDELAY_INPUT) ? SD_ITAPDLY : SD_OTAPDLYSEL;
	u32 mask = (node_id == NODE_SD_0) ? GENMASK(15, 0) : GENMASK(31, 16);

	if (value) {
		return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 4, node_id, IOCTL_SET_SD_TAPDELAY, type,
					   value);
	}

	/*
	 * Work around completely misdesigned firmware API on Xilinx ZynqMP.
	 * The IOCTL_SET_SD_TAPDELAY firmware call allows the caller to only
	 * ever set IOU_SLCR SD_ITAPDLY Register SD0_ITAPDLYENA/SD1_ITAPDLYENA
	 * bits, but there is no matching call to clear those bits. If those
	 * bits are not cleared, SDMMC tuning may fail.
	 *
	 * Luckily, there are PM_MMIO_READ/PM_MMIO_WRITE calls which seem to
	 * allow complete unrestricted access to all address space, including
	 * IOU_SLCR SD_ITAPDLY Register and all the other registers, access
	 * to which was supposed to be protected by the current firmware API.
	 *
	 * Use PM_MMIO_READ/PM_MMIO_WRITE to re-implement the missing counter
	 * part of IOCTL_SET_SD_TAPDELAY which clears SDx_ITAPDLYENA bits.
	 */
	return zynqmp_pm_invoke_fn(PM_MMIO_WRITE, NULL, 2, reg, mask);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_sd_tapdelay);

/**
 * zynqmp_pm_sd_dll_reset() - Reset DLL logic
 *
 * @node_id:	Node ID of the device
 * @type:	Reset type
 *
 * This function resets DLL logic for the SD device.
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_sd_dll_reset(u32 node_id, u32 type)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 3, node_id, IOCTL_SD_DLL_RESET, type);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_sd_dll_reset);

/**
 * zynqmp_pm_ospi_mux_select() - OSPI Mux selection
 *
 * @dev_id:	Device Id of the OSPI device.
 * @select:	OSPI Mux select value.
 *
 * This function select the OSPI Mux.
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_ospi_mux_select(u32 dev_id, u32 select)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 3, dev_id, IOCTL_OSPI_MUX_SELECT, select);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_ospi_mux_select);

/**
 * zynqmp_pm_write_ggs() - PM API for writing global general storage (ggs)
 * @index:	GGS register index
 * @value:	Register value to be written
 *
 * This function writes value to GGS register.
 *
 * Return:      Returns status, either success or error+reason
 */
int zynqmp_pm_write_ggs(u32 index, u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 4, 0, IOCTL_WRITE_GGS, index, value);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_write_ggs);

/**
 * zynqmp_pm_read_ggs() - PM API for reading global general storage (ggs)
 * @index:	GGS register index
 * @value:	Register value to be written
 *
 * This function returns GGS register value.
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_read_ggs(u32 index, u32 *value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, value, 3, 0, IOCTL_READ_GGS, index);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_read_ggs);

/**
 * zynqmp_pm_write_pggs() - PM API for writing persistent global general
 *			     storage (pggs)
 * @index:	PGGS register index
 * @value:	Register value to be written
 *
 * This function writes value to PGGS register.
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_write_pggs(u32 index, u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 4, 0, IOCTL_WRITE_PGGS, index, value);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_write_pggs);

/**
 * zynqmp_pm_read_pggs() - PM API for reading persistent global general
 *			     storage (pggs)
 * @index:	PGGS register index
 * @value:	Register value to be written
 *
 * This function returns PGGS register value.
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_read_pggs(u32 index, u32 *value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, value, 3, 0, IOCTL_READ_PGGS, index);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_read_pggs);

int zynqmp_pm_set_tapdelay_bypass(u32 index, u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 4, 0, IOCTL_SET_TAPDELAY_BYPASS, index, value);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_tapdelay_bypass);

int zynqmp_pm_usb_set_state(u32 node, u32 state, u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 4, node, IOCTL_USB_SET_STATE,
				   state, value);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_usb_set_state);

int zynqmp_pm_get_last_reset_reason(u32 *reset_reason)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, reset_reason, 2, 0, IOCTL_GET_LAST_RESET_REASON);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_last_reset_reason);

int zynqmp_pm_afi(u32 index, u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 4, 0, IOCTL_AFI, index, value);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_afi);

/**
 * zynqmp_pm_set_boot_health_status() - PM API for setting healthy boot status
 * @value:	Status value to be written
 *
 * This function sets healthy bit value to indicate boot health status
 * to firmware.
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_set_boot_health_status(u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 3, 0, IOCTL_SET_BOOT_HEALTH_STATUS, value);
}

/**
 * zynqmp_pm_aie_operation - AI engine run time operations
 * @node:	AI engine node id
 * @start_col:	Starting column of AI partition
 * @num_col:	Number of column in AI partition
 * @operation:	ORed value of operations
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_aie_operation(u32 node, u16 start_col, u16 num_col, u32 operation)
{
	u32 partition;

	partition = num_col;
	partition = ((partition << 16U) | start_col);
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 4, node, IOCTL_AIE_OPS,
				   partition, operation);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_aie_operation);

/**
 * versal2_pm_aie2ps_operation - AIE2PS run time operations
 * @node:	AI engine node id
 * @size:	Size of the DMA addr
 * @addr_high:	higher 32 bits of DMA addr
 * @addr_low:	lower 32 bits of DMA addr
 *
 * Return: Returns status, either success or error+reason
 */
int versal2_pm_aie2ps_operation(u32 node, u32 size, u32 addr_high, u32 addr_low)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 5, node, IOCTL_AIE2PS_OPS,
				   size, addr_high, addr_low);
}
EXPORT_SYMBOL_GPL(versal2_pm_aie2ps_operation);

/**
 * zynqmp_pm_reset_assert - Request setting of reset (1 - assert, 0 - release)
 * @reset:		Reset to be configured
 * @assert_flag:	Flag stating should reset be asserted (1) or
 *			released (0)
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_reset_assert(const u32 reset,
			   const enum zynqmp_pm_reset_action assert_flag)
{
	return zynqmp_pm_invoke_fn(PM_RESET_ASSERT, NULL, 2, reset, assert_flag);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_reset_assert);

/**
 * zynqmp_pm_reset_get_status - Get status of the reset
 * @reset:      Reset whose status should be returned
 * @status:     Returned status
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_reset_get_status(const u32 reset, u32 *status)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!status)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_RESET_GET_STATUS, ret_payload, 1, reset);
	*status = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_reset_get_status);

/**
 * zynqmp_pm_pinctrl_request - Request Pin from firmware
 * @pin: Pin number to request
 *
 * This function requests pin from firmware.
 *
 * Return: Returns status, either success or error+reason.
 */
int zynqmp_pm_pinctrl_request(const u32 pin)
{
	return zynqmp_pm_invoke_fn(PM_PINCTRL_REQUEST, NULL, 1, pin);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_pinctrl_request);

/**
 * zynqmp_pm_pinctrl_release - Inform firmware that Pin control is released
 * @pin: Pin number to release
 *
 * This function release pin from firmware.
 *
 * Return: Returns status, either success or error+reason.
 */
int zynqmp_pm_pinctrl_release(const u32 pin)
{
	return zynqmp_pm_invoke_fn(PM_PINCTRL_RELEASE, NULL, 1, pin);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_pinctrl_release);

/**
 * zynqmp_pm_pinctrl_set_function - Set requested function for the pin
 * @pin: Pin number
 * @id: Function ID to set
 *
 * This function sets requested function for the given pin.
 *
 * Return: Returns status, either success or error+reason.
 */
int zynqmp_pm_pinctrl_set_function(const u32 pin, const u32 id)
{
	return zynqmp_pm_invoke_fn(PM_PINCTRL_SET_FUNCTION, NULL, 2, pin, id);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_pinctrl_set_function);

/**
 * zynqmp_pm_pinctrl_get_config - Get configuration parameter for the pin
 * @pin: Pin number
 * @param: Parameter to get
 * @value: Buffer to store parameter value
 *
 * This function gets requested configuration parameter for the given pin.
 *
 * Return: Returns status, either success or error+reason.
 */
int zynqmp_pm_pinctrl_get_config(const u32 pin, const u32 param,
				 u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!value)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_PINCTRL_CONFIG_PARAM_GET, ret_payload, 2, pin, param);
	*value = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_pinctrl_get_config);

/**
 * zynqmp_pm_pinctrl_set_config - Set configuration parameter for the pin
 * @pin: Pin number
 * @param: Parameter to set
 * @value: Parameter value to set
 *
 * This function sets requested configuration parameter for the given pin.
 *
 * Return: Returns status, either success or error+reason.
 */
int zynqmp_pm_pinctrl_set_config(const u32 pin, const u32 param,
				 u32 value)
{
	u32 pm_family_code;
	u32 pm_sub_family_code;
	int ret;

	/* Get the Family code and sub family code of platform */
	ret = zynqmp_pm_get_family_info(&pm_family_code, &pm_sub_family_code);
	if (ret < 0)
		return ret;

	if (pm_family_code == ZYNQMP_FAMILY_CODE &&
	    param == PM_PINCTRL_CONFIG_TRI_STATE) {
		ret = zynqmp_pm_feature(PM_PINCTRL_CONFIG_PARAM_SET);
		if (ret < PM_PINCTRL_PARAM_SET_VERSION) {
			pr_warn("The requested pinctrl feature is not supported in the current firmware.\n"
				"Expected firmware version is 2023.1 and above for this feature to work.\r\n");
			return -EOPNOTSUPP;
		}
	}

	return zynqmp_pm_invoke_fn(PM_PINCTRL_CONFIG_PARAM_SET, NULL, 3, pin, param, value);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_pinctrl_set_config);

/**
 * zynqmp_pm_bootmode_read() - PM Config API for read bootpin status
 * @ps_mode: Returned output value of ps_mode
 *
 * This API function is to be used for notify the power management controller
 * to read bootpin status.
 *
 * Return: status, either success or error+reason
 */
unsigned int zynqmp_pm_bootmode_read(u32 *ps_mode)
{
	unsigned int ret;
	u32 ret_payload[PAYLOAD_ARG_CNT];

	ret = zynqmp_pm_invoke_fn(PM_MMIO_READ, ret_payload, 1, CRL_APB_BOOT_PIN_CTRL);

	*ps_mode = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_bootmode_read);

/**
 * zynqmp_pm_bootmode_write() - PM Config API for Configure bootpin
 * @ps_mode: Value to be written to the bootpin ctrl register
 *
 * This API function is to be used for notify the power management controller
 * to configure bootpin.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_bootmode_write(u32 ps_mode)
{
	return zynqmp_pm_invoke_fn(PM_MMIO_WRITE, NULL, 3, CRL_APB_BOOT_PIN_CTRL,
				   CRL_APB_BOOTPIN_CTRL_MASK, ps_mode);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_bootmode_write);

/**
 * zynqmp_pm_init_finalize() - PM call to inform firmware that the caller
 *			       master has initialized its own power management
 *
 * Return: Returns status, either success or error+reason
 *
 * This API function is to be used for notify the power management controller
 * about the completed power management initialization.
 */
int zynqmp_pm_init_finalize(void)
{
	return zynqmp_pm_invoke_fn(PM_PM_INIT_FINALIZE, NULL, 0);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_init_finalize);

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
int zynqmp_pm_config_reg_access(u32 register_access_id, u32 address,
				u32 mask, u32 value, u32 *out)
{
	return zynqmp_pm_invoke_fn(PM_REGISTER_ACCESS, out, 4,
				   register_access_id,
				   address, mask, value);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_config_reg_access);

/**
 * zynqmp_pm_set_suspend_mode()	- Set system suspend mode
 * @mode:	Mode to set for system suspend
 *
 * This API function is used to set mode of system suspend.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_set_suspend_mode(u32 mode)
{
	return zynqmp_pm_invoke_fn(PM_SET_SUSPEND_MODE, NULL, 1, mode);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_suspend_mode);

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
int zynqmp_pm_request_node(const u32 node, const u32 capabilities,
			   const u32 qos, const enum zynqmp_pm_request_ack ack)
{
	return zynqmp_pm_invoke_fn(PM_REQUEST_NODE, NULL, 4, node, capabilities, qos, ack);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_request_node);

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
int zynqmp_pm_release_node(const u32 node)
{
	return zynqmp_pm_invoke_fn(PM_RELEASE_NODE, NULL, 1, node);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_release_node);

/**
 * zynqmp_pm_get_rpu_mode() - Get RPU mode
 * @node_id:	Node ID of the device
 * @rpu_mode:	return by reference value
 *		either split or lockstep
 *
 * Return:	return 0 on success or error+reason.
 *		if success, then  rpu_mode will be set
 *		to current rpu mode.
 */
int zynqmp_pm_get_rpu_mode(u32 node_id, enum rpu_oper_mode *rpu_mode)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_IOCTL, ret_payload, 2, node_id, IOCTL_GET_RPU_OPER_MODE);

	/* only set rpu_mode if no error */
	if (ret == XST_PM_SUCCESS)
		*rpu_mode = ret_payload[0];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_rpu_mode);

/**
 * zynqmp_pm_set_rpu_mode() - Set RPU mode
 * @node_id:	Node ID of the device
 * @rpu_mode:	Argument 1 to requested IOCTL call. either split or lockstep
 *
 *		This function is used to set RPU mode to split or
 *		lockstep
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_set_rpu_mode(u32 node_id, enum rpu_oper_mode rpu_mode)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 3, node_id, IOCTL_SET_RPU_OPER_MODE,
				   (u32)rpu_mode);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_rpu_mode);

/**
 * zynqmp_pm_set_tcm_config - configure TCM
 * @node_id:	Firmware specific TCM subsystem ID
 * @tcm_mode:	Argument 1 to requested IOCTL call
 *              either PM_RPU_TCM_COMB or PM_RPU_TCM_SPLIT
 *
 * This function is used to set RPU mode to split or combined
 *
 * Return: status: 0 for success, else failure
 */
int zynqmp_pm_set_tcm_config(u32 node_id, enum rpu_tcm_comb tcm_mode)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 3, node_id, IOCTL_TCM_COMB_CONFIG,
				   (u32)tcm_mode);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_tcm_config);

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
int zynqmp_pm_get_node_status(const u32 node, u32 *const status,
			      u32 *const requirements, u32 *const usage)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!status || !requirements || !usage)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_GET_NODE_STATUS, ret_payload, 1, node);
	if (ret_payload[0] == XST_PM_SUCCESS) {
		*status = ret_payload[1];
		*requirements = ret_payload[2];
		*usage = ret_payload[3];
	}

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_node_status);

/**
 * zynqmp_pm_force_pwrdwn - PM call to request for another PU or subsystem to
 *             be powered down forcefully
 * @node:  Node ID of the targeted PU or subsystem
 * @ack:   Flag to specify whether acknowledge is requested
 *
 * Return: status, either success or error+reason
 */
int zynqmp_pm_force_pwrdwn(const u32 node,
			   const enum zynqmp_pm_request_ack ack)
{
	return zynqmp_pm_invoke_fn(PM_FORCE_POWERDOWN, NULL, 2, node, ack);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_force_pwrdwn);

/**
 * zynqmp_pm_request_wake - PM call to wake up selected master or subsystem
 * @node:  Node ID of the master or subsystem
 * @set_addr:  Specifies whether the address argument is relevant
 * @address:   Address from which to resume when woken up
 * @ack:   Flag to specify whether acknowledge requested
 *
 * Return: status, either success or error+reason
 */
int zynqmp_pm_request_wake(const u32 node,
			   const bool set_addr,
			   const u64 address,
			   const enum zynqmp_pm_request_ack ack)
{
	/* set_addr flag is encoded into 1st bit of address */
	return zynqmp_pm_invoke_fn(PM_REQUEST_WAKEUP, NULL, 4, node, address | set_addr,
				   address >> 32, ack);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_request_wake);

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
int zynqmp_pm_set_requirement(const u32 node, const u32 capabilities,
			      const u32 qos,
			      const enum zynqmp_pm_request_ack ack)
{
	return zynqmp_pm_invoke_fn(PM_SET_REQUIREMENT, NULL, 4, node, capabilities, qos, ack);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_requirement);

/**
 * zynqmp_pm_register_notifier() - PM API for register a subsystem
 *                                to be notified about specific
 *                                event/error.
 * @node:	Node ID to which the event is related.
 * @event:	Event Mask of Error events for which wants to get notified.
 * @wake:	Wake subsystem upon capturing the event if value 1
 * @enable:	Enable the registration for value 1, disable for value 0
 *
 * This function is used to register/un-register for particular node-event
 * combination in firmware.
 *
 * Return: Returns status, either success or error+reason
 */

int zynqmp_pm_register_notifier(const u32 node, const u32 event,
				const u32 wake, const u32 enable)
{
	return zynqmp_pm_invoke_fn(PM_REGISTER_NOTIFIER, NULL, 4, node, event, wake, enable);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_register_notifier);

/**
 * zynqmp_pm_system_shutdown - PM call to request a system shutdown or restart
 * @type:	Shutdown or restart? 0 for shutdown, 1 for restart
 * @subtype:	Specifies which system should be restarted or shut down
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_system_shutdown(const u32 type, const u32 subtype)
{
	return zynqmp_pm_invoke_fn(PM_SYSTEM_SHUTDOWN, NULL, 2, type, subtype);
}

/**
 * zynqmp_pm_set_feature_config - PM call to request IOCTL for feature config
 * @id:         The config ID of the feature to be configured
 * @value:      The config value of the feature to be configured
 *
 * Return:      Returns 0 on success or error value on failure.
 */
int zynqmp_pm_set_feature_config(enum pm_feature_config_id id, u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 4, 0, IOCTL_SET_FEATURE_CONFIG, id, value);
}

/**
 * zynqmp_pm_get_feature_config - PM call to get value of configured feature
 * @id:         The config id of the feature to be queried
 * @payload:    Returned value array
 *
 * Return:      Returns 0 on success or error value on failure.
 */
int zynqmp_pm_get_feature_config(enum pm_feature_config_id id,
				 u32 *payload)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, payload, 3, 0, IOCTL_GET_FEATURE_CONFIG, id);
}

/**
 * zynqmp_pm_sec_read_reg - PM call to securely read from given offset
 *		of the node
 * @node_id:	Node Id of the device
 * @offset:	Offset to be used (20-bit)
 * @ret_value:	Output data read from the given offset after
 *		firmware access policy is successfully enforced
 *
 * Return:	Returns 0 on success or error value on failure
 */
int zynqmp_pm_sec_read_reg(u32 node_id, u32 offset, u32 *ret_value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	u32 count = 1;
	int ret;

	if (!ret_value)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_IOCTL, ret_payload, 4, node_id, IOCTL_READ_REG,
				  offset, count);

	*ret_value = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_sec_read_reg);

/**
 * zynqmp_pm_sec_mask_write_reg - PM call to securely write to given offset
 *		of the node
 * @node_id:	Node Id of the device
 * @offset:	Offset to be used (20-bit)
 * @mask:	Mask to be used
 * @value:	Value to be written
 *
 * Return:	Returns 0 on success or error value on failure
 */
int zynqmp_pm_sec_mask_write_reg(const u32 node_id, const u32 offset, u32 mask,
				 u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 5, node_id, IOCTL_MASK_WRITE_REG,
				   offset, mask, value);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_sec_mask_write_reg);

/**
 * zynqmp_pm_get_qos - PM call to query default and current QoS of the node
 * @node:	Node Id of the device
 * @def_qos:	Default QoS value
 * @qos:	Current QoS value
 *
 * Return:	Returns 0 on success and the default and current QoS registers in
 *		@def_qos and @qos or error value on failure
 */
int zynqmp_pm_get_qos(u32 node, u32 *const def_qos, u32 *const qos)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!def_qos || !qos)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_IOCTL, ret_payload, 2, node, IOCTL_GET_QOS);

	*def_qos = ret_payload[1];
	*qos = ret_payload[2];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_get_qos);

/**
 * zynqmp_pm_set_sd_config - PM call to set value of SD config registers
 * @node:	SD node ID
 * @config:	The config type of SD registers
 * @value:	Value to be set
 *
 * Return:	Returns 0 on success or error value on failure.
 */
int zynqmp_pm_set_sd_config(u32 node, enum pm_sd_config_type config, u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 4, node, IOCTL_SET_SD_CONFIG, config, value);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_sd_config);

/**
 * zynqmp_pm_set_gem_config - PM call to set value of GEM config registers
 * @node:	GEM node ID
 * @config:	The config type of GEM registers
 * @value:	Value to be set
 *
 * Return:	Returns 0 on success or error value on failure.
 */
int zynqmp_pm_set_gem_config(u32 node, enum pm_gem_config_type config,
			     u32 value)
{
	return zynqmp_pm_invoke_fn(PM_IOCTL, NULL, 4, node, IOCTL_SET_GEM_CONFIG, config, value);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_set_gem_config);

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

static ssize_t shutdown_scope_show(struct device *device,
				   struct device_attribute *attr,
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

static ssize_t shutdown_scope_store(struct device *device,
				    struct device_attribute *attr,
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

static DEVICE_ATTR_RW(shutdown_scope);

static ssize_t health_status_store(struct device *device,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ret;
	unsigned int value;

	ret = kstrtouint(buf, 10, &value);
	if (ret)
		return ret;

	ret = zynqmp_pm_set_boot_health_status(value);
	if (ret) {
		dev_err(device, "unable to set healthy bit value to %u\n",
			value);
		return ret;
	}

	return count;
}

static DEVICE_ATTR_WO(health_status);

static ssize_t ggs_show(struct device *device,
			struct device_attribute *attr,
			char *buf,
			u32 reg)
{
	int ret;
	u32 ret_payload[PAYLOAD_ARG_CNT];

	ret = zynqmp_pm_read_ggs(reg, ret_payload);
	if (ret)
		return ret;

	return sprintf(buf, "0x%x\n", ret_payload[1]);
}

static ssize_t ggs_store(struct device *device,
			 struct device_attribute *attr,
			 const char *buf, size_t count,
			 u32 reg)
{
	long value;
	int ret;

	if (reg >= GSS_NUM_REGS)
		return -EINVAL;

	ret = kstrtol(buf, 16, &value);
	if (ret) {
		count = -EFAULT;
		goto err;
	}

	ret = zynqmp_pm_write_ggs(reg, value);
	if (ret)
		count = -EFAULT;
err:
	return count;
}

/* GGS register show functions */
#define GGS0_SHOW(N)						\
	ssize_t ggs##N##_show(struct device *device,		\
			      struct device_attribute *attr,	\
			      char *buf)			\
	{							\
		return ggs_show(device, attr, buf, N);		\
	}

static GGS0_SHOW(0);
static GGS0_SHOW(1);
static GGS0_SHOW(2);
static GGS0_SHOW(3);

/* GGS register store function */
#define GGS0_STORE(N)						\
	ssize_t ggs##N##_store(struct device *device,		\
			       struct device_attribute *attr,	\
			       const char *buf,			\
			       size_t count)			\
	{							\
		return ggs_store(device, attr, buf, count, N);	\
	}

static GGS0_STORE(0);
static GGS0_STORE(1);
static GGS0_STORE(2);
static GGS0_STORE(3);

static ssize_t pggs_show(struct device *device,
			 struct device_attribute *attr,
			 char *buf,
			 u32 reg)
{
	int ret;
	u32 ret_payload[PAYLOAD_ARG_CNT];

	ret = zynqmp_pm_read_pggs(reg, ret_payload);
	if (ret)
		return ret;

	return sprintf(buf, "0x%x\n", ret_payload[1]);
}

static ssize_t pggs_store(struct device *device,
			  struct device_attribute *attr,
			  const char *buf, size_t count,
			  u32 reg)
{
	long value;
	int ret;

	if (reg >= GSS_NUM_REGS)
		return -EINVAL;

	ret = kstrtol(buf, 16, &value);
	if (ret) {
		count = -EFAULT;
		goto err;
	}

	ret = zynqmp_pm_write_pggs(reg, value);
	if (ret)
		count = -EFAULT;

err:
	return count;
}

#define PGGS0_SHOW(N)						\
	ssize_t pggs##N##_show(struct device *device,		\
			       struct device_attribute *attr,	\
			       char *buf)			\
	{							\
		return pggs_show(device, attr, buf, N);		\
	}

#define PGGS0_STORE(N)						\
	ssize_t pggs##N##_store(struct device *device,		\
				struct device_attribute *attr,	\
				const char *buf,		\
				size_t count)			\
	{							\
		return pggs_store(device, attr, buf, count, N);	\
	}

/* PGGS register show functions */
static PGGS0_SHOW(0);
static PGGS0_SHOW(1);
static PGGS0_SHOW(2);
static PGGS0_SHOW(3);

/* PGGS register store functions */
static PGGS0_STORE(0);
static PGGS0_STORE(1);
static PGGS0_STORE(2);
static PGGS0_STORE(3);

/* GGS register attributes */
static DEVICE_ATTR_RW(ggs0);
static DEVICE_ATTR_RW(ggs1);
static DEVICE_ATTR_RW(ggs2);
static DEVICE_ATTR_RW(ggs3);

/* PGGS register attributes */
static DEVICE_ATTR_RW(pggs0);
static DEVICE_ATTR_RW(pggs1);
static DEVICE_ATTR_RW(pggs2);
static DEVICE_ATTR_RW(pggs3);

static ssize_t feature_config_id_show(struct device *device,
				      struct device_attribute *attr,
				      char *buf)
{
	struct zynqmp_devinfo *devinfo = dev_get_drvdata(device);

	return sysfs_emit(buf, "%d\n", devinfo->feature_conf_id);
}

static ssize_t feature_config_id_store(struct device *device,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	u32 config_id;
	int ret;
	struct zynqmp_devinfo *devinfo = dev_get_drvdata(device);

	if (!buf)
		return -EINVAL;

	ret = kstrtou32(buf, 10, &config_id);
	if (ret)
		return ret;

	devinfo->feature_conf_id = config_id;

	return count;
}

static DEVICE_ATTR_RW(feature_config_id);

static ssize_t feature_config_value_show(struct device *device,
					 struct device_attribute *attr,
					 char *buf)
{
	int ret;
	u32 ret_payload[PAYLOAD_ARG_CNT];
	struct zynqmp_devinfo *devinfo = dev_get_drvdata(device);

	ret = zynqmp_pm_get_feature_config(devinfo->feature_conf_id,
					   ret_payload);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", ret_payload[1]);
}

static ssize_t feature_config_value_store(struct device *device,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	u32 value;
	int ret;
	struct zynqmp_devinfo *devinfo = dev_get_drvdata(device);

	if (!buf)
		return -EINVAL;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	ret = zynqmp_pm_set_feature_config(devinfo->feature_conf_id,
					   value);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(feature_config_value);

static ssize_t last_reset_reason_show(struct device *device,
				      struct device_attribute *attr,
				      char *buf)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_get_last_reset_reason(ret_payload);
	if (ret)
		return ret;

	switch (ret_payload[1]) {
	case PM_RESET_REASON_EXT_POR:
		return sprintf(buf, "ext_por\n");
	case PM_RESET_REASON_SW_POR:
		return sprintf(buf, "sw_por\n");
	case PM_RESET_REASON_SLR_POR:
		return sprintf(buf, "slr_por\n");
	case PM_RESET_REASON_ERR_POR:
		return sprintf(buf, "err_por\n");
	case PM_RESET_REASON_DAP_SRST:
		return sprintf(buf, "dap_srst\n");
	case PM_RESET_REASON_ERR_SRST:
		return sprintf(buf, "err_srst\n");
	case PM_RESET_REASON_SW_SRST:
		return sprintf(buf, "sw_srst\n");
	case PM_RESET_REASON_SLR_SRST:
		return sprintf(buf, "slr_srst\n");
	default:
		return sprintf(buf, "unknown reset\n");
	}
}

static DEVICE_ATTR_RO(last_reset_reason);

static const struct attribute *zynqmp_firmware_attrs[] = {
	&dev_attr_ggs0.attr,
	&dev_attr_ggs1.attr,
	&dev_attr_ggs2.attr,
	&dev_attr_ggs3.attr,
	&dev_attr_pggs0.attr,
	&dev_attr_pggs1.attr,
	&dev_attr_pggs2.attr,
	&dev_attr_pggs3.attr,
	&dev_attr_shutdown_scope.attr,
	&dev_attr_health_status.attr,
	&dev_attr_feature_config_id.attr,
	&dev_attr_feature_config_value.attr,
	&dev_attr_last_reset_reason.attr,
	NULL,
};

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

	kern_buff = kzalloc(count + 1, GFP_KERNEL);
	if (!kern_buff)
		return -ENOMEM;

	ret = strscpy(kern_buff, buf, count + 1);
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
	if (ret) {
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
	&zynqmp_attr_config_reg.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs = attrs,
	NULL,
};

int zynqmp_firmware_pm_sysfs_entry(struct platform_device *pdev)
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
		return ret;
	}


	ret = sysfs_create_files(&pdev->dev.kobj, zynqmp_firmware_attrs);
	if (ret) {
		pr_err("%s() Failed to create PM firmware attrs, err=%d\n",
		       __func__, ret);
	}

	return ret;
}
