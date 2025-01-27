/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Firmware layer for XilNVM APIs.
 *
 * Copyright (C), 2025 Advanced Micro Devices, Inc.
 */

#ifndef __FIRMWARE_ZYNQMP_NVM_H__
#define __FIRMWARE_ZYNQMP_NVM_H__

/* NVM Commands */
#define PM_BBRAM_WRITE_KEY		0xB01
#define PM_BBRAM_ZEROIZE		0xB02
#define PM_BBRAM_WRITE_USERDATA		0xB03
#define PM_BBRAM_READ_USERDATA		0xB04
#define PM_BBRAM_LOCK_USERDATA		0xB05
#define PM_EFUSE_READ_VERSAL		0xB17

#if IS_REACHABLE(CONFIG_ZYNQMP_FIRMWARE)
int zynqmp_pm_bbram_write_usrdata(u32 data);
int zynqmp_pm_bbram_read_usrdata(const u64 outaddr);
int zynqmp_pm_bbram_write_aeskey(u32 keylen, const u64 keyaddr);
int zynqmp_pm_bbram_zeroize(void);
int zynqmp_pm_bbram_lock_userdata(void);
#else
static inline int zynqmp_pm_bbram_write_usrdata(u32 data)
{
	return -ENODEV;
}

static inline int zynqmp_pm_bbram_read_usrdata(const u64 outaddr)
{
	return -ENODEV;
}

static inline int zynqmp_pm_bbram_write_aeskey(const u64 keyaddr, u16 keylen)
{
	return -ENODEV;
}

static inline int zynqmp_pm_bbram_zeroize(void)
{
	return -ENODEV;
}

static inline int zynqmp_pm_bbram_lock_userdata(void)
{
	return -ENODEV;
}
#endif

#endif /* __FIRMWARE_ZYNQMP_NVM_H__ */
