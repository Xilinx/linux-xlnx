// SPDX-License-Identifier: GPL-2.0
/*
 * Firmware layer for XilFPGA APIs.
 *
 * Copyright (C), 2025 Advanced Micro Devices, Inc.
 */

#include <linux/export.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/kernel.h>

/**
 * zynqmp_pm_fpga_load - Perform the fpga load
 * @address:	Address to write to
 * @size:	pl bitstream size
 * @flags:	Bitstream type
 *	-XILINX_ZYNQMP_PM_FPGA_FULL:  FPGA full reconfiguration
 *	-XILINX_ZYNQMP_PM_FPGA_PARTIAL: FPGA partial reconfiguration
 *
 * This function provides access to pmufw. To transfer
 * the required bitstream into PL.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_fpga_load(const u64 address, const u32 size, const u32 flags)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_FPGA_LOAD, ret_payload, 4, lower_32_bits(address),
				  upper_32_bits(address), size, flags);
	if (ret_payload[0])
		return -ret_payload[0];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_fpga_load);

/**
 * zynqmp_pm_fpga_get_status - Read value from PCAP status register
 * @value: Value to read
 *
 * This function provides access to the pmufw to get the PCAP
 * status
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_fpga_get_status(u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!value)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_FPGA_GET_STATUS, ret_payload, 0);
	*value = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_fpga_get_status);

/**
 * zynqmp_pm_fpga_get_config_status - Get the FPGA configuration status.
 * @value: Buffer to store FPGA configuration status.
 *
 * This function provides access to the pmufw to get the FPGA configuration
 * status
 *
 * Return: 0 on success, a negative value on error
 */
int zynqmp_pm_fpga_get_config_status(u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!value)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_FPGA_READ, ret_payload, 4,
				  XILINX_ZYNQMP_PM_FPGA_CONFIG_STAT_OFFSET, 0, 0,
				  XILINX_ZYNQMP_PM_FPGA_READ_CONFIG_REG);

	*value = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_fpga_get_config_status);

/**
 * zynqmp_pm_fpga_get_version -Get xilfpga component version info
 * @value: Value to read
 *
 * This function provides access to the pmufw to get the xilfpga
 * component version info.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_fpga_get_version(u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!value)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_FPGA_GET_VERSION, ret_payload, 0);
	*value = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_fpga_get_version);

/**
 * zynqmp_pm_fpga_get_feature_list - Get xilfpga component supported feature
 * list.
 * @value: Value to read
 *
 * This function provides access to the pmufw to get the xilfpga component
 * supported feature list.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_fpga_get_feature_list(u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!value)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_FPGA_GET_FEATURE_LIST, ret_payload, 0);

	*value = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_fpga_get_feature_list);

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
int zynqmp_pm_fpga_read(const u32 reg_numframes, const u64 phys_address,
			u32 readback_type, u32 *value)
{
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	if (!value)
		return -EINVAL;

	ret = zynqmp_pm_invoke_fn(PM_FPGA_READ, ret_payload, 4, reg_numframes,
				  lower_32_bits(phys_address),
				  upper_32_bits(phys_address),
				  readback_type);
	*value = ret_payload[1];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_fpga_read);
