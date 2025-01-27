/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Firmware layer for XilSEM APIs.
 *
 * Copyright (C), 2025 Advanced Micro Devices, Inc.
 */

#ifndef __FIRMWARE_ZYNQMP_SEM_H__
#define __FIRMWARE_ZYNQMP_SEM_H__

/* XilSEM commands */
#define PM_XSEM_HEADER			0x300
#define PM_XSEM_CRAM_ERRINJ		0x304
#define PM_XSEM_RD_CONFIG		0x309
#define PM_XSEM_CRAM_RD_ECC		0x30B

#if IS_REACHABLE(CONFIG_ZYNQMP_FIRMWARE)
int zynqmp_pm_xilsem_cntrl_ops(u32 cmd, u32 *const response);
int zynqmp_pm_xilsem_cram_errinj(u32 frame, u32 qword, u32 bit, u32 row, u32 *const response);
int zynqmp_pm_xilsem_cram_readecc(u32 frame, u32 row, u32 *const response);
int zynqmp_pm_xilsem_read_cfg(u32 *const response);
#else
static inline int zynqmp_pm_xilsem_cntrl_ops(u32 cmd, u32 *const response)
{
	return -ENODEV;
}

static inline int zynqmp_pm_xilsem_cram_readecc(u32 frame, u32 row, u32 *const response)
{
	return -ENODEV;
}

static inline int zynqmp_pm_xilsem_cram_errinj(u32 frame, u32 qword, u32 bit,
					       u32 row, u32 *const response)
{
	return -ENODEV;
}

static inline int zynqmp_pm_xilsem_read_cfg(u32 *const response)
{
	return -ENODEV;
}
#endif

#endif /* __FIRMWARE_ZYNQMP_SEM_H__ */
