/*
 * Xilinx Zynq MPSoC Power Management
 *
 *  Copyright (C) 2014-2015 Xilinx
 *
 *  Davorin Mista <davorin.mista@aggios.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __SOC_ZYNQMP_PM_H__
#define __SOC_ZYNQMP_PM_H__

#include <linux/soc/xilinx/zynqmp/firmware.h>

#define ZYNQMP_PM_VERSION_MAJOR	0
#define ZYNQMP_PM_VERSION_MINOR	3

#define ZYNQMP_PM_VERSION	((ZYNQMP_PM_VERSION_MAJOR << 16) | \
					ZYNQMP_PM_VERSION_MINOR)

#define ZYNQMP_PM_MAX_LATENCY	(~0U)
#define ZYNQMP_PM_MAX_QOS	100U

/* Capabilities for RAM */
#define	ZYNQMP_PM_CAPABILITY_ACCESS	0x1U
#define	ZYNQMP_PM_CAPABILITY_CONTEXT	0x2U
#define	ZYNQMP_PM_CAPABILITY_WAKEUP	0x4U
#define	ZYNQMP_PM_CAPABILITY_POWER	0x8U

enum zynqmp_pm_request_ack {
	ZYNQMP_PM_REQUEST_ACK_NO = 1,
	ZYNQMP_PM_REQUEST_ACK_BLOCKING,
	ZYNQMP_PM_REQUEST_ACK_NON_BLOCKING,
};

enum zynqmp_pm_abort_reason {
	ZYNQMP_PM_ABORT_REASON_WAKEUP_EVENT = 100,
	ZYNQMP_PM_ABORT_REASON_POWER_UNIT_BUSY,
	ZYNQMP_PM_ABORT_REASON_NO_POWERDOWN,
	ZYNQMP_PM_ABORT_REASON_UNKNOWN,
};

enum zynqmp_pm_suspend_reason {
	ZYNQMP_PM_SUSPEND_REASON_POWER_UNIT_REQUEST = 201,
	ZYNQMP_PM_SUSPEND_REASON_ALERT,
	ZYNQMP_PM_SUSPEND_REASON_SYSTEM_SHUTDOWN,
};

enum zynqmp_pm_ram_state {
	ZYNQMP_PM_RAM_STATE_OFF = 1,
	ZYNQMP_PM_RAM_STATE_RETENTION,
	ZYNQMP_PM_RAM_STATE_ON,
};

enum zynqmp_pm_opchar_type {
	ZYNQMP_PM_OPERATING_CHARACTERISTIC_POWER = 1,
	ZYNQMP_PM_OPERATING_CHARACTERISTIC_ENERGY,
	ZYNQMP_PM_OPERATING_CHARACTERISTIC_TEMPERATURE,
};

enum zynqmp_pm_reset_action {
	PM_RESET_ACTION_RELEASE,
	PM_RESET_ACTION_ASSERT,
	PM_RESET_ACTION_PULSE,
};

enum zynqmp_pm_reset {
	ZYNQMP_PM_RESET_START = 999,
	ZYNQMP_PM_RESET_PCIE_CFG,
	ZYNQMP_PM_RESET_PCIE_BRIDGE,
	ZYNQMP_PM_RESET_PCIE_CTRL,
	ZYNQMP_PM_RESET_DP,
	ZYNQMP_PM_RESET_SWDT_CRF,
	ZYNQMP_PM_RESET_AFI_FM5,
	ZYNQMP_PM_RESET_AFI_FM4,
	ZYNQMP_PM_RESET_AFI_FM3,
	ZYNQMP_PM_RESET_AFI_FM2,
	ZYNQMP_PM_RESET_AFI_FM1,
	ZYNQMP_PM_RESET_AFI_FM0,
	ZYNQMP_PM_RESET_GDMA,
	ZYNQMP_PM_RESET_GPU_PP1,
	ZYNQMP_PM_RESET_GPU_PP0,
	ZYNQMP_PM_RESET_GPU,
	ZYNQMP_PM_RESET_GT,
	ZYNQMP_PM_RESET_SATA,
	ZYNQMP_PM_RESET_ACPU3_PWRON,
	ZYNQMP_PM_RESET_ACPU2_PWRON,
	ZYNQMP_PM_RESET_ACPU1_PWRON,
	ZYNQMP_PM_RESET_ACPU0_PWRON,
	ZYNQMP_PM_RESET_APU_L2,
	ZYNQMP_PM_RESET_ACPU3,
	ZYNQMP_PM_RESET_ACPU2,
	ZYNQMP_PM_RESET_ACPU1,
	ZYNQMP_PM_RESET_ACPU0,
	ZYNQMP_PM_RESET_DDR,
	ZYNQMP_PM_RESET_APM_FPD,
	ZYNQMP_PM_RESET_SOFT,
	ZYNQMP_PM_RESET_GEM0,
	ZYNQMP_PM_RESET_GEM1,
	ZYNQMP_PM_RESET_GEM2,
	ZYNQMP_PM_RESET_GEM3,
	ZYNQMP_PM_RESET_QSPI,
	ZYNQMP_PM_RESET_UART0,
	ZYNQMP_PM_RESET_UART1,
	ZYNQMP_PM_RESET_SPI0,
	ZYNQMP_PM_RESET_SPI1,
	ZYNQMP_PM_RESET_SDIO0,
	ZYNQMP_PM_RESET_SDIO1,
	ZYNQMP_PM_RESET_CAN0,
	ZYNQMP_PM_RESET_CAN1,
	ZYNQMP_PM_RESET_I2C0,
	ZYNQMP_PM_RESET_I2C1,
	ZYNQMP_PM_RESET_TTC0,
	ZYNQMP_PM_RESET_TTC1,
	ZYNQMP_PM_RESET_TTC2,
	ZYNQMP_PM_RESET_TTC3,
	ZYNQMP_PM_RESET_SWDT_CRL,
	ZYNQMP_PM_RESET_NAND,
	ZYNQMP_PM_RESET_ADMA,
	ZYNQMP_PM_RESET_GPIO,
	ZYNQMP_PM_RESET_IOU_CC,
	ZYNQMP_PM_RESET_TIMESTAMP,
	ZYNQMP_PM_RESET_RPU_R50,
	ZYNQMP_PM_RESET_RPU_R51,
	ZYNQMP_PM_RESET_RPU_AMBA,
	ZYNQMP_PM_RESET_OCM,
	ZYNQMP_PM_RESET_RPU_PGE,
	ZYNQMP_PM_RESET_USB0_CORERESET,
	ZYNQMP_PM_RESET_USB1_CORERESET,
	ZYNQMP_PM_RESET_USB0_HIBERRESET,
	ZYNQMP_PM_RESET_USB1_HIBERRESET,
	ZYNQMP_PM_RESET_USB0_APB,
	ZYNQMP_PM_RESET_USB1_APB,
	ZYNQMP_PM_RESET_IPI,
	ZYNQMP_PM_RESET_APM_LPD,
	ZYNQMP_PM_RESET_RTC,
	ZYNQMP_PM_RESET_SYSMON,
	ZYNQMP_PM_RESET_AFI_FM6,
	ZYNQMP_PM_RESET_LPD_SWDT,
	ZYNQMP_PM_RESET_FPD,
	ZYNQMP_PM_RESET_RPU_DBG1,
	ZYNQMP_PM_RESET_RPU_DBG0,
	ZYNQMP_PM_RESET_DBG_LPD,
	ZYNQMP_PM_RESET_DBG_FPD,
	ZYNQMP_PM_RESET_APLL,
	ZYNQMP_PM_RESET_DPLL,
	ZYNQMP_PM_RESET_VPLL,
	ZYNQMP_PM_RESET_IOPLL,
	ZYNQMP_PM_RESET_RPLL,
	ZYNQMP_PM_RESET_GPO3_PL_0,
	ZYNQMP_PM_RESET_GPO3_PL_1,
	ZYNQMP_PM_RESET_GPO3_PL_2,
	ZYNQMP_PM_RESET_GPO3_PL_3,
	ZYNQMP_PM_RESET_GPO3_PL_4,
	ZYNQMP_PM_RESET_GPO3_PL_5,
	ZYNQMP_PM_RESET_GPO3_PL_6,
	ZYNQMP_PM_RESET_GPO3_PL_7,
	ZYNQMP_PM_RESET_GPO3_PL_8,
	ZYNQMP_PM_RESET_GPO3_PL_9,
	ZYNQMP_PM_RESET_GPO3_PL_10,
	ZYNQMP_PM_RESET_GPO3_PL_11,
	ZYNQMP_PM_RESET_GPO3_PL_12,
	ZYNQMP_PM_RESET_GPO3_PL_13,
	ZYNQMP_PM_RESET_GPO3_PL_14,
	ZYNQMP_PM_RESET_GPO3_PL_15,
	ZYNQMP_PM_RESET_GPO3_PL_16,
	ZYNQMP_PM_RESET_GPO3_PL_17,
	ZYNQMP_PM_RESET_GPO3_PL_18,
	ZYNQMP_PM_RESET_GPO3_PL_19,
	ZYNQMP_PM_RESET_GPO3_PL_20,
	ZYNQMP_PM_RESET_GPO3_PL_21,
	ZYNQMP_PM_RESET_GPO3_PL_22,
	ZYNQMP_PM_RESET_GPO3_PL_23,
	ZYNQMP_PM_RESET_GPO3_PL_24,
	ZYNQMP_PM_RESET_GPO3_PL_25,
	ZYNQMP_PM_RESET_GPO3_PL_26,
	ZYNQMP_PM_RESET_GPO3_PL_27,
	ZYNQMP_PM_RESET_GPO3_PL_28,
	ZYNQMP_PM_RESET_GPO3_PL_29,
	ZYNQMP_PM_RESET_GPO3_PL_30,
	ZYNQMP_PM_RESET_GPO3_PL_31,
	ZYNQMP_PM_RESET_RPU_LS,
	ZYNQMP_PM_RESET_PS_ONLY,
	ZYNQMP_PM_RESET_PL,
	ZYNQMP_PM_RESET_END
};

/*
 * OS-level PM API
 */

/* API for suspending of APU */
int zynqmp_pm_request_suspend(const u32 node,
				      const enum zynqmp_pm_request_ack ack,
				      const u32 latency,
				      const u32 state);
int zynqmp_pm_request_wakeup(const u32 node,
				     const bool set_addr,
				     const u64 address,
				     const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_set_wakeup_source(const u32 target,
					const u32 wakeup_node,
					const u32 enable);
int zynqmp_pm_system_shutdown(const u32 type, const u32 subtype);

/* API for suspending of RPU */
int zynqmp_pm_force_powerdown(const u32 target,
					const enum zynqmp_pm_request_ack ack);

/* API functions for managing PM Slaves */
int zynqmp_pm_request_node(const u32 node,
				   const u32 capabilities,
				   const u32 qos,
				   const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_release_node(const u32 node);
int zynqmp_pm_set_requirement(const u32 node,
				   const u32 capabilities,
				   const u32 qos,
				   const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_set_max_latency(const u32 node,
					  const u32 latency);

/* Miscellaneous API functions */
int zynqmp_pm_get_api_version(u32 *version);
int zynqmp_pm_set_configuration(const u32 physical_addr);
int zynqmp_pm_get_node_status(const u32 node,
				u32 *const status,
				u32 *const requirements,
				u32 *const usage);
int zynqmp_pm_get_operating_characteristic(const u32 node,
					const enum zynqmp_pm_opchar_type type,
					u32 *const result);

/* Direct-Control API functions */
int zynqmp_pm_reset_assert(const enum zynqmp_pm_reset reset,
				const enum zynqmp_pm_reset_action assert_flag);
int zynqmp_pm_reset_get_status(const enum zynqmp_pm_reset reset,
					u32 *status);
int zynqmp_pm_mmio_write(const u32 address,
				     const u32 mask,
				     const u32 value);
int zynqmp_pm_mmio_read(const u32 address, u32 *value);
int zynqmp_pm_fpga_load(const u64 address, const u32 size, const u32 flags);
int zynqmp_pm_fpga_get_status(u32 *value);
int zynqmp_pm_get_chipid(u32 *idcode, u32 *version);

#endif /* __SOC_ZYNQMP_PM_H__ */
