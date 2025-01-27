/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Firmware layer for XilPDI APIs.
 *
 * Copyright (C), 2025 Advanced Micro Devices, Inc.
 */

#ifndef __FIRMWARE_ZYNQMP_PDI_H__
#define __FIRMWARE_ZYNQMP_PDI_H__

#include <linux/platform_device.h>

/* Loader commands */
#define PM_LOAD_PDI			0x701
#define PM_GET_UID_INFO_LIST		0x705
#define PM_GET_META_HEADER_INFO_LIST	0x706

#define PDI_SRC_DDR	0xF

int zynqmp_firmware_pdi_sysfs_entry(struct platform_device *pdev);

#if IS_REACHABLE(CONFIG_ZYNQMP_FIRMWARE)
int zynqmp_pm_get_uid_info(const u64 address, const u32 size, u32 *count);
int zynqmp_pm_get_meta_header(const u64 src, const u64 dst,
			      const u32 size, u32 *count);
int zynqmp_pm_load_pdi(const u32 src, const u64 address);
int zynqmp_pm_rsa(const u64 address, const u32 size, const u32 flags);
#else
static inline int zynqmp_pm_load_pdi(const u32 src, const u64 address)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_uid_info(const u64 address, const u32 size,
					 u32 *count)
{
	return -ENODEV;
}

static inline int zynqmp_pm_rsa(const u64 address, const u32 size,
				const u32 flags)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_meta_header(const u64 src, const u64 dst,
					    const u32 size, u32 *count)
{
	return -ENODEV;
}
#endif

#endif /* __FIRMWARE_ZYNQMP_PDI_H__ */
