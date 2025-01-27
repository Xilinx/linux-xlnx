/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Firmware layer for XilFPGA APIs.
 *
 * Copyright (C), 2025 Advanced Micro Devices, Inc.
 */

#ifndef __FIRMWARE_ZYNQMP_FPGA_H__
#define __FIRMWARE_ZYNQMP_FPGA_H__

/*
 * Firmware FPGA Manager flags
 * XILINX_ZYNQMP_PM_FPGA_FULL:	FPGA full reconfiguration
 * XILINX_ZYNQMP_PM_FPGA_PARTIAL: FPGA partial reconfiguration
 */
#define XILINX_ZYNQMP_PM_FPGA_FULL	0x0U
#define XILINX_ZYNQMP_PM_FPGA_PARTIAL	BIT(0)
#define XILINX_ZYNQMP_PM_FPGA_AUTHENTICATION_DDR	BIT(1)
#define XILINX_ZYNQMP_PM_FPGA_AUTHENTICATION_OCM	BIT(2)
#define XILINX_ZYNQMP_PM_FPGA_ENCRYPTION_USERKEY	BIT(3)
#define XILINX_ZYNQMP_PM_FPGA_ENCRYPTION_DEVKEY		BIT(4)

/* FPGA Status Reg */
#define XILINX_ZYNQMP_PM_FPGA_CONFIG_STAT_OFFSET	7U
#define XILINX_ZYNQMP_PM_FPGA_READ_CONFIG_REG		0U

#if IS_REACHABLE(CONFIG_ZYNQMP_FIRMWARE)
int zynqmp_pm_fpga_read(const u32 reg_numframes, const u64 phys_address,
			u32 readback_type, u32 *value);
int zynqmp_pm_fpga_load(const u64 address, const u32 size, const u32 flags);
int zynqmp_pm_fpga_get_status(u32 *value);
int zynqmp_pm_fpga_get_config_status(u32 *value);
int zynqmp_pm_fpga_get_version(u32 *value);
int zynqmp_pm_fpga_get_feature_list(u32 *value);
#else
static inline int zynqmp_pm_fpga_read(const u32 reg_numframes,
				      const u64 phys_address, u32 readback_type,
				      u32 *value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_fpga_load(const u64 address, const u32 size,
				      const u32 flags)
{
	return -ENODEV;
}

static inline int zynqmp_pm_fpga_get_status(u32 *value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_fpga_get_config_status(u32 *value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_fpga_get_version(u32 *value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_fpga_get_feature_list(u32 *value)
{
	return -ENODEV;
}
#endif

#endif /* __FIRMWARE_ZYNQMP_FPGA_H__ */
