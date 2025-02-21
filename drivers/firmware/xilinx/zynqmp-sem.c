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
 * @slrid:	SLR id on which scan operation to be done
 * @response:	Output response (command header, error code or status, slr id)
 *
 * Return: Returns 0 on success or error value on failure.
 */
int zynqmp_pm_xilsem_cntrl_ops(u32 cmd, u32 slrid, u32 *const response)
{
	u32 ret_buf[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_XSEM_HEADER | cmd, ret_buf, 1, slrid);
	response[0] = ret_buf[1];
	response[1] = ret_buf[2];
	response[2] = ret_buf[3];
	response[3] = ret_buf[4];
	response[4] = ret_buf[5];
	response[5] = ret_buf[6];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_xilsem_cntrl_ops);

/**
 * zynqmp_pm_xilsem_cram_errinj - PM call to perform CRAM error injection
 * @slrid:	SLR id to inject error in CRAM
 * @frame:	Frame number to be used for error injection
 * @qword:	Word number to be used for error injection
 * @bit:	Bit location to be used for error injection
 * @row:	CFRAME row number to be used for error injection
 * @response:	Output response (command header, error code or status, slr id)
 *
 * Return: Returns 0 on success or error value on failure.
 */
int zynqmp_pm_xilsem_cram_errinj(u32 slrid, u32 frame, u32 qword, u32 bit, u32 row,
				 u32 *const response)
{
	u32 ret_buf[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_XSEM_CRAM_ERRINJ, ret_buf, 5, slrid, frame, qword, bit, row);
	response[0] = ret_buf[1];
	response[1] = ret_buf[2];
	response[2] = ret_buf[3];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_xilsem_cram_errinj);

/**
 * zynqmp_pm_xilsem_cram_readecc - PM call to perform CFRAME ECC read
 * @slrid:	SLR id on which Frame ECC read to be done
 * @frame:	Frame number to be used for reading ECC
 * @row:	CFRAME row number to be used for reading ECC
 * @response:	Output response (Frame ecc header, ECC values, status)
 *
 * Return: Returns 0 on success or error value on failure.
 */
int zynqmp_pm_xilsem_cram_readecc(u32 slrid, u32 frame, u32 row, u32 *const response)
{
	u32 ret_buf[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_XSEM_CRAM_RD_ECC, ret_buf, 3, slrid, frame, row);
	response[0] = ret_buf[1];
	response[1] = ret_buf[2];
	response[2] = ret_buf[3];
	response[3] = ret_buf[4];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_xilsem_cram_readecc);

/**
 * zynqmp_pm_xilsem_read_cfg - PM call to perform Xilsem configuration read
 * @slrid:	SLR id for which configuration to be read
 * @response:	Output response (config header, Xilsem config, status)
 *
 * Return: Returns 0 on success or error value on failure.
 */
int zynqmp_pm_xilsem_read_cfg(u32 slrid, u32 *const response)
{
	u32 ret_buf[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_XSEM_RD_CONFIG, ret_buf, 1, slrid);
	response[0] = ret_buf[1];
	response[1] = ret_buf[2];
	response[2] = ret_buf[3];
	response[3] = ret_buf[4];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_xilsem_read_cfg);

/**
 * zynqmp_pm_xilsem_read_ssit_status - PM call to perform Xilsem SSIT status
 * @slrid:	SLR id for which ECC read to be done
 * @bufaddr:	Buffer address to get the status information
 * @response:	Output response (status read header, slr id)
 *
 * Return: Returns 0 on success or error value on failure.
 */
int zynqmp_pm_xilsem_read_ssit_status(u32 slrid, u32 bufaddr, u32 *const response)
{
	u32 ret_buf[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_XSEM_SSIT_RD_STS, ret_buf, 2, slrid, bufaddr);
	response[0] = ret_buf[1];
	response[1] = ret_buf[2];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_xilsem_read_ssit_status);

/**
 * zynqmp_pm_xilsem_cram_getcrc - PM call to perform CRAM Row CRC read
 * @slrid:	SLR id for which CRC read to be done
 * @rowindex:	CFRAME row number to be used for reading CRC
 * @response:	Output response (Get CRC header, CRC values, status)
 *
 * Return: Returns 0 on success or error value on failure.
 */
int zynqmp_pm_xilsem_cram_getcrc(u32 slrid, u32 rowindex, u32 *const response)
{
	u32 ret_buf[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_XSEM_SSIT_GET_CRC, ret_buf, 2, slrid, rowindex);
	response[0] = ret_buf[1];
	response[1] = ret_buf[2];
	response[2] = ret_buf[3];
	response[3] = ret_buf[4];
	response[4] = ret_buf[5];
	response[5] = ret_buf[6];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_xilsem_cram_getcrc);

/**
 * zynqmp_pm_xilsem_cram_ssit_totframes - PM call to perform total frames read
 * @slrid:	SLR id for which total frames read to be done
 * @row:	CFRAME row number to be used for reading ECC
 * @framecnt: Buffer address to get toral frames data
 * @response:	Output response (Total frames header, slr id, row, status)
 *
 * Return: Returns 0 on success or error value on failure.
 */
int zynqmp_pm_xilsem_cram_ssit_totframes(u32 slrid, u32 row, u32 framecnt, u32 *const response)
{
	u32 ret_buf[PAYLOAD_ARG_CNT];
	int ret;

	ret = zynqmp_pm_invoke_fn(PM_XSEM_SSIT_GET_FRAMES, ret_buf, 3, slrid, row, framecnt);
	response[0] = ret_buf[1];
	response[1] = ret_buf[2];
	response[2] = ret_buf[3];
	response[3] = ret_buf[4];

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_pm_xilsem_cram_ssit_totframes);
