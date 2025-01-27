// SPDX-License-Identifier: GPL-2.0
/*
 * Firmware layer for XilNVM APIs.
 *
 * Copyright (C), 2025 Advanced Micro Devices, Inc.
 */

#include <linux/export.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/kernel.h>

/**
 * zynqmp_pm_bbram_write_aeskey - Write AES key in BBRAM
 * @keylen:	Size of the input key to be written
 * @keyaddr: Address of a buffer which should contain the key
 *			to be written
 *
 * This function provides support to write AES keys into BBRAM.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_bbram_write_aeskey(u32 keylen, const u64 keyaddr)
{
	return zynqmp_pm_invoke_fn(PM_BBRAM_WRITE_KEY, NULL, 4, keylen,
				   lower_32_bits(keyaddr),
				   upper_32_bits(keyaddr));
}
EXPORT_SYMBOL_GPL(zynqmp_pm_bbram_write_aeskey);

/**
 * zynqmp_pm_bbram_write_usrdata - Write user data in BBRAM
 * @data: User data to be written in BBRAM
 *
 * This function provides support to write user data into BBRAM.
 * The size of the user data must be 4 bytes.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_bbram_write_usrdata(u32 data)
{
	return zynqmp_pm_invoke_fn(PM_BBRAM_WRITE_USERDATA, NULL, 1, data);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_bbram_write_usrdata);

/**
 * zynqmp_pm_bbram_read_usrdata - Read user data in BBRAM
 * @outaddr: Address of a buffer to store the user data read from BBRAM
 *
 * This function provides support to read user data in BBRAM.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_bbram_read_usrdata(const u64 outaddr)
{
	return zynqmp_pm_invoke_fn(PM_BBRAM_READ_USERDATA, NULL, 1, outaddr);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_bbram_read_usrdata);

/**
 * zynqmp_pm_bbram_zeroize - Zeroizes AES key in BBRAM
 *
 * Description:
 * This function provides support to zeroize AES key in BBRAM.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_bbram_zeroize(void)
{
	return zynqmp_pm_invoke_fn(PM_BBRAM_ZEROIZE, NULL, 0);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_bbram_zeroize);

/**
 * zynqmp_pm_bbram_lock_userdata - Locks user data for write
 *
 * Description:
 * This function disables writing user data into BBRAM.
 *
 * Return: Returns status, either success or error+reason
 */
int zynqmp_pm_bbram_lock_userdata(void)
{
	return zynqmp_pm_invoke_fn(PM_BBRAM_LOCK_USERDATA, NULL, 0);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_bbram_lock_userdata);
