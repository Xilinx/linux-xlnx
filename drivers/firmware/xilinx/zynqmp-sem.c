// SPDX-License-Identifier: GPL-2.0
/*
 * Firmware layer for XilSEM APIs.
 *
 * Copyright (C), 2025 Advanced Micro Devices, Inc.
 */

#include <linux/export.h>
#include <linux/firmware/xlnx-zynqmp.h>

/**
 * zynqmp_pm_xilsem_cntrl_ops - PM call to perform XilSEM operations
 * @cmd:	Command for XilSEM scan control operations
 * @response:	Output response (command header, error code or status)
 *
 * Return: Returns 0 on success or error value on failure.
 */
int zynqmp_pm_xilsem_cntrl_ops(u32 cmd, u32 *const response)
{
	u32 ret_buf[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_XSEM_HEADER | cmd, ret_buf, 0);
	response[0] = ret_buf[1];
	response[1] = ret_buf[2];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_xilsem_cntrl_ops);

/**
 * zynqmp_pm_xilsem_cram_errinj - PM call to perform CRAM error injection
 * @frame:	Frame number to be used for error injection
 * @qword:	Word number to be used for error injection
 * @bit:	Bit location to be used for error injection
 * @row:	CFRAME row number to be used for error injection
 * @response:	Output response (command header, error code or status)
 *
 * Return: Returns 0 on success or error value on failure.
 */
int zynqmp_pm_xilsem_cram_errinj(u32 frame, u32 qword, u32 bit, u32 row,
				 u32 *const response)
{
	u32 ret_buf[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_XSEM_CRAM_ERRINJ, ret_buf, 4, frame,
				  qword, bit, row);
	response[0] = ret_buf[1];
	response[1] = ret_buf[2];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_xilsem_cram_errinj);

/**
 * zynqmp_pm_xilsem_cram_readecc - PM call to perform CFRAME ECC read
 * @frame:	Frame number to be used for reading ECC
 * @row:	CFRAME row number to be used for reading ECC
 * @response:	Output response (status, Frame ecc header, ECC values)
 *
 * Return: Returns 0 on success or error value on failure.
 */
int zynqmp_pm_xilsem_cram_readecc(u32 frame, u32 row, u32 *const response)
{
	u32 ret_buf[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_XSEM_CRAM_RD_ECC, ret_buf, 2, frame, row);
	response[0] = ret_buf[0];
	response[1] = ret_buf[1];
	response[2] = ret_buf[2];
	response[3] = ret_buf[3];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_xilsem_cram_readecc);

/**
 * zynqmp_pm_xilsem_read_cfg - PM call to perform Xilsem configuration read
 * @response:	Output response (status, config header, Xilsem config)
 *
 * Return: Returns 0 on success or error value on failure.
 */
int zynqmp_pm_xilsem_read_cfg(u32 *const response)
{
	u32 ret_buf[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_XSEM_RD_CONFIG, ret_buf, 0);
	response[0] = ret_buf[0];
	response[1] = ret_buf[1];
	response[2] = ret_buf[2];
	response[3] = ret_buf[3];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_xilsem_read_cfg);
