/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2014-2019 Xilinx
 *
 *  Michal Simek <michal.simek@xilinx.com>
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajanv@xilinx.com>
 */

#ifndef __FIRMWARE_ZYNQMP_H__
#define __FIRMWARE_ZYNQMP_H__
#include <linux/types.h>

#include <linux/err.h>

#define ZYNQMP_PM_VERSION_MAJOR	1
#define ZYNQMP_PM_VERSION_MINOR	0

#define ZYNQMP_PM_VERSION	((ZYNQMP_PM_VERSION_MAJOR << 16) | \
					ZYNQMP_PM_VERSION_MINOR)

#define ZYNQMP_TZ_VERSION_MAJOR	1
#define ZYNQMP_TZ_VERSION_MINOR	0

#define ZYNQMP_TZ_VERSION	((ZYNQMP_TZ_VERSION_MAJOR << 16) | \
					ZYNQMP_TZ_VERSION_MINOR)

/* SMC SIP service Call Function Identifier Prefix */
#define PM_SIP_SVC			0xC2000000

/* ATF only commands */
#define PM_GET_TRUSTZONE_VERSION	0xa03
#define PM_SET_SUSPEND_MODE		0xa02
#define GET_CALLBACK_DATA		0xa01

/* Loader commands */
#define PM_LOAD_PDI			0x701

/* Number of 32bits values in payload */
#define PAYLOAD_ARG_CNT	4U

/* Number of arguments for a callback */
#define CB_ARG_CNT     4

/* Payload size (consists of callback API ID + arguments) */
#define CB_PAYLOAD_SIZE (CB_ARG_CNT + 1)

#define ZYNQMP_PM_MAX_LATENCY	(~0U)
#define ZYNQMP_PM_MAX_QOS	100U

/* Usage status, returned by PmGetNodeStatus */
#define PM_USAGE_NO_MASTER			0x0U
#define PM_USAGE_CURRENT_MASTER			0x1U
#define PM_USAGE_OTHER_MASTER			0x2U
#define PM_USAGE_BOTH_MASTERS			(PM_USAGE_CURRENT_MASTER | \
						 PM_USAGE_OTHER_MASTER)

#define GSS_NUM_REGS	(4)

/* Node capabilities */
#define	ZYNQMP_PM_CAPABILITY_ACCESS	0x1U
#define	ZYNQMP_PM_CAPABILITY_CONTEXT	0x2U
#define	ZYNQMP_PM_CAPABILITY_WAKEUP	0x4U
#define	ZYNQMP_PM_CAPABILITY_UNUSABLE	0x8U

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

enum pm_api_id {
	PM_GET_API_VERSION = 1,
	PM_SET_CONFIGURATION = 2,
	PM_GET_NODE_STATUS = 3,
	PM_GET_OPERATING_CHARACTERISTIC = 4,
	PM_REGISTER_NOTIFIER = 5,
	/* API for suspending */
	PM_REQUEST_SUSPEND = 6,
	PM_SELF_SUSPEND = 7,
	PM_FORCE_POWERDOWN = 8,
	PM_ABORT_SUSPEND = 9,
	PM_REQUEST_WAKEUP = 10,
	PM_SET_WAKEUP_SOURCE = 11,
	PM_SYSTEM_SHUTDOWN = 12,
	PM_REQUEST_NODE = 13,
	PM_RELEASE_NODE,
	PM_SET_REQUIREMENT,
	PM_SET_MAX_LATENCY = 16,
	/* Direct control API functions: */
	PM_RESET_ASSERT = 17,
	PM_RESET_GET_STATUS,
	PM_PM_INIT_FINALIZE = 21,
	PM_FPGA_LOAD,
	PM_FPGA_GET_STATUS,
	PM_GET_CHIPID = 24,
	/* ID 25 is been used by U-boot to process secure boot images */
	/* Secure library generic API functions */
	PM_SECURE_SHA = 26,
	PM_SECURE_RSA,
	PM_PINCTRL_REQUEST = 28,
	PM_PINCTRL_RELEASE = 29,
	PM_PINCTRL_GET_FUNCTION = 30,
	PM_PINCTRL_SET_FUNCTION = 31,
	PM_PINCTRL_CONFIG_PARAM_GET = 32,
	PM_PINCTRL_CONFIG_PARAM_SET = 33,
	PM_IOCTL = 34,
	PM_QUERY_DATA,
	PM_CLOCK_ENABLE,
	PM_CLOCK_DISABLE,
	PM_CLOCK_GETSTATE,
	PM_CLOCK_SETDIVIDER,
	PM_CLOCK_GETDIVIDER,
	PM_CLOCK_SETRATE,
	PM_CLOCK_GETRATE,
	PM_CLOCK_SETPARENT,
	PM_CLOCK_GETPARENT,
	PM_SECURE_IMAGE = 45,
	PM_FPGA_READ = 46,
	PM_SECURE_AES = 47,
	/* PM_REGISTER_ACCESS API */
	PM_REGISTER_ACCESS = 52,
	PM_EFUSE_ACCESS = 53,
	PM_FEATURE_CHECK = 63,
};

/* PMU-FW return status codes */
enum pm_ret_status {
	XST_PM_SUCCESS = 0,
	XST_PM_NO_FEATURE = 19,
	XST_PM_INTERNAL = 2000,
	XST_PM_CONFLICT,
	XST_PM_NO_ACCESS,
	XST_PM_INVALID_NODE,
	XST_PM_DOUBLE_REQ,
	XST_PM_ABORT_SUSPEND,
	XST_PM_MULT_USER = 2008,
};

enum pm_ioctl_id {
	IOCTL_GET_RPU_OPER_MODE = 0,
	IOCTL_SET_RPU_OPER_MODE = 1,
	IOCTL_RPU_BOOT_ADDR_CONFIG = 2,
	IOCTL_TCM_COMB_CONFIG = 3,
	IOCTL_SET_TAPDELAY_BYPASS = 4,
	IOCTL_SET_SGMII_MODE = 5,
	IOCTL_SD_DLL_RESET = 6,
	IOCTL_SET_SD_TAPDELAY = 7,
	IOCTL_SET_PLL_FRAC_MODE,
	IOCTL_GET_PLL_FRAC_MODE,
	IOCTL_SET_PLL_FRAC_DATA,
	IOCTL_GET_PLL_FRAC_DATA,
	IOCTL_WRITE_GGS = 12,
	IOCTL_READ_GGS = 13,
	IOCTL_WRITE_PGGS = 14,
	IOCTL_READ_PGGS = 15,
	/* IOCTL for ULPI reset */
	IOCTL_ULPI_RESET = 16,
	/* Set healthy bit value */
	IOCTL_SET_BOOT_HEALTH_STATUS = 17,
	IOCTL_AFI = 18,
	/* Probe counter read/write */
	IOCTL_PROBE_COUNTER_READ = 19,
	IOCTL_PROBE_COUNTER_WRITE = 20,
	IOCTL_OSPI_MUX_SELECT = 21,
	/* IOCTL for USB power request */
	IOCTL_USB_SET_STATE = 22,
	/* IOCTL to get last reset reason */
	IOCTL_GET_LAST_RESET_REASON = 23,
	/* AI engine NPI ISR clear */
	IOCTL_AIE_ISR_CLEAR = 24,
};

enum pm_query_id {
	PM_QID_INVALID,
	PM_QID_CLOCK_GET_NAME,
	PM_QID_CLOCK_GET_TOPOLOGY,
	PM_QID_CLOCK_GET_FIXEDFACTOR_PARAMS,
	PM_QID_CLOCK_GET_PARENTS,
	PM_QID_CLOCK_GET_ATTRIBUTES,
	PM_QID_PINCTRL_GET_NUM_PINS = 6,
	PM_QID_PINCTRL_GET_NUM_FUNCTIONS = 7,
	PM_QID_PINCTRL_GET_NUM_FUNCTION_GROUPS = 8,
	PM_QID_PINCTRL_GET_FUNCTION_NAME = 9,
	PM_QID_PINCTRL_GET_FUNCTION_GROUPS = 10,
	PM_QID_PINCTRL_GET_PIN_GROUPS = 11,
	PM_QID_CLOCK_GET_NUM_CLOCKS = 12,
	PM_QID_CLOCK_GET_MAX_DIVISOR,
	PM_QID_PLD_GET_PARENT,
};

enum rpu_oper_mode {
	PM_RPU_MODE_LOCKSTEP = 0,
	PM_RPU_MODE_SPLIT = 1,
};

enum rpu_boot_mem {
	PM_RPU_BOOTMEM_LOVEC = 0,
	PM_RPU_BOOTMEM_HIVEC = 1,
};

enum rpu_tcm_comb {
	PM_RPU_TCM_SPLIT = 0,
	PM_RPU_TCM_COMB = 1,
};

enum zynqmp_pm_reset_action {
	PM_RESET_ACTION_RELEASE,
	PM_RESET_ACTION_ASSERT,
	PM_RESET_ACTION_PULSE,
};

enum zynqmp_pm_reset {
	ZYNQMP_PM_RESET_START = 1000,
	ZYNQMP_PM_RESET_PCIE_CFG = ZYNQMP_PM_RESET_START,
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
	ZYNQMP_PM_RESET_PS_PL0,
	ZYNQMP_PM_RESET_PS_PL1,
	ZYNQMP_PM_RESET_PS_PL2,
	ZYNQMP_PM_RESET_PS_PL3,
	ZYNQMP_PM_RESET_END = ZYNQMP_PM_RESET_PS_PL3
};

enum zynqmp_pm_abort_reason {
	ZYNQMP_PM_ABORT_REASON_WAKEUP_EVENT = 100,
	ZYNQMP_PM_ABORT_REASON_POWER_UNIT_BUSY = 101,
	ZYNQMP_PM_ABORT_REASON_NO_POWERDOWN = 102,
	ZYNQMP_PM_ABORT_REASON_UNKNOWN = 103,
};

enum zynqmp_pm_suspend_reason {
	SUSPEND_POWER_REQUEST = 201,
	SUSPEND_ALERT,
	SUSPEND_SYSTEM_SHUTDOWN,
};

enum zynqmp_pm_request_ack {
	ZYNQMP_PM_REQUEST_ACK_NO = 1,
	ZYNQMP_PM_REQUEST_ACK_BLOCKING,
	ZYNQMP_PM_REQUEST_ACK_NON_BLOCKING,
};

enum pm_node_id {
	NODE_UNKNOWN = 0,
	NODE_APU = 1,
	NODE_APU_0 = 2,
	NODE_APU_1 = 3,
	NODE_APU_2 = 4,
	NODE_APU_3 = 5,
	NODE_RPU = 6,
	NODE_RPU_0 = 7,
	NODE_RPU_1 = 8,
	NODE_PLD = 9,
	NODE_FPD = 10,
	NODE_OCM_BANK_0 = 11,
	NODE_OCM_BANK_1 = 12,
	NODE_OCM_BANK_2 = 13,
	NODE_OCM_BANK_3 = 14,
	NODE_TCM_0_A = 15,
	NODE_TCM_0_B = 16,
	NODE_TCM_1_A = 17,
	NODE_TCM_1_B = 18,
	NODE_L2 = 19,
	NODE_GPU_PP_0 = 20,
	NODE_GPU_PP_1 = 21,
	NODE_USB_0 = 22,
	NODE_USB_1 = 23,
	NODE_TTC_0 = 24,
	NODE_TTC_1 = 25,
	NODE_TTC_2 = 26,
	NODE_TTC_3 = 27,
	NODE_SATA = 28,
	NODE_ETH_0 = 29,
	NODE_ETH_1 = 30,
	NODE_ETH_2 = 31,
	NODE_ETH_3 = 32,
	NODE_UART_0 = 33,
	NODE_UART_1 = 34,
	NODE_SPI_0 = 35,
	NODE_SPI_1 = 36,
	NODE_I2C_0 = 37,
	NODE_I2C_1 = 38,
	NODE_SD_0 = 39,
	NODE_SD_1 = 40,
	NODE_DP = 41,
	NODE_GDMA = 42,
	NODE_ADMA = 43,
	NODE_NAND = 44,
	NODE_QSPI = 45,
	NODE_GPIO = 46,
	NODE_CAN_0 = 47,
	NODE_CAN_1 = 48,
	NODE_EXTERN = 49,
	NODE_APLL = 50,
	NODE_VPLL = 51,
	NODE_DPLL = 52,
	NODE_RPLL = 53,
	NODE_IOPLL = 54,
	NODE_DDR = 55,
	NODE_IPI_APU = 56,
	NODE_IPI_RPU_0 = 57,
	NODE_GPU = 58,
	NODE_PCIE = 59,
	NODE_PCAP = 60,
	NODE_RTC = 61,
	NODE_LPD = 62,
	NODE_VCU = 63,
	NODE_IPI_RPU_1 = 64,
	NODE_IPI_PL_0 = 65,
	NODE_IPI_PL_1 = 66,
	NODE_IPI_PL_2 = 67,
	NODE_IPI_PL_3 = 68,
	NODE_PL = 69,
	NODE_GEM_TSU = 70,
	NODE_SWDT_0 = 71,
	NODE_SWDT_1 = 72,
	NODE_CSU = 73,
	NODE_PJTAG = 74,
	NODE_TRACE = 75,
	NODE_TESTSCAN = 76,
	NODE_PMU = 77,
	NODE_MAX = 78,
};

enum tap_delay_type {
	PM_TAPDELAY_INPUT = 0,
	PM_TAPDELAY_OUTPUT,
};

enum dll_reset_type {
	PM_DLL_RESET_ASSERT,
	PM_DLL_RESET_RELEASE,
	PM_DLL_RESET_PULSE,
};

enum pm_pinctrl_config_param {
	PM_PINCTRL_CONFIG_SLEW_RATE = 0,
	PM_PINCTRL_CONFIG_BIAS_STATUS = 1,
	PM_PINCTRL_CONFIG_PULL_CTRL = 2,
	PM_PINCTRL_CONFIG_SCHMITT_CMOS = 3,
	PM_PINCTRL_CONFIG_DRIVE_STRENGTH = 4,
	PM_PINCTRL_CONFIG_VOLTAGE_STATUS = 5,
	PM_PINCTRL_CONFIG_TRI_STATE = 6,
	PM_PINCTRL_CONFIG_MAX = 7,
};

enum pm_pinctrl_slew_rate {
	PM_PINCTRL_SLEW_RATE_FAST = 0,
	PM_PINCTRL_SLEW_RATE_SLOW = 1,
};

enum pm_pinctrl_bias_status {
	PM_PINCTRL_BIAS_DISABLE = 0,
	PM_PINCTRL_BIAS_ENABLE = 1,
};

enum pm_pinctrl_pull_ctrl {
	PM_PINCTRL_BIAS_PULL_DOWN = 0,
	PM_PINCTRL_BIAS_PULL_UP = 1,
};

enum pm_pinctrl_schmitt_cmos {
	PM_PINCTRL_INPUT_TYPE_CMOS = 0,
	PM_PINCTRL_INPUT_TYPE_SCHMITT = 1,
};

enum zynqmp_pm_opchar_type {
	ZYNQMP_PM_OPERATING_CHARACTERISTIC_POWER = 1,
	ZYNQMP_PM_OPERATING_CHARACTERISTIC_ENERGY = 2,
	ZYNQMP_PM_OPERATING_CHARACTERISTIC_TEMPERATURE = 3,
};

enum pm_pinctrl_drive_strength {
	PM_PINCTRL_DRIVE_STRENGTH_2MA = 0,
	PM_PINCTRL_DRIVE_STRENGTH_4MA = 1,
	PM_PINCTRL_DRIVE_STRENGTH_8MA = 2,
	PM_PINCTRL_DRIVE_STRENGTH_12MA = 3,
};

enum pm_pinctrl_tri_state {
	PM_PINCTRL_TRI_STATE_DISABLE = 0,
	PM_PINCTRL_TRI_STATE_ENABLE = 1,
};

enum zynqmp_pm_shutdown_type {
	ZYNQMP_PM_SHUTDOWN_TYPE_SHUTDOWN,
	ZYNQMP_PM_SHUTDOWN_TYPE_RESET,
	ZYNQMP_PM_SHUTDOWN_TYPE_SETSCOPE_ONLY,
};

enum zynqmp_pm_shutdown_subtype {
	ZYNQMP_PM_SHUTDOWN_SUBTYPE_SUBSYSTEM,
	ZYNQMP_PM_SHUTDOWN_SUBTYPE_PS_ONLY,
	ZYNQMP_PM_SHUTDOWN_SUBTYPE_SYSTEM,
};

enum tap_delay_signal_type {
	PM_TAPDELAY_NAND_DQS_IN = 0,
	PM_TAPDELAY_NAND_DQS_OUT = 1,
	PM_TAPDELAY_QSPI = 2,
	PM_TAPDELAY_MAX = 3,
};

enum tap_delay_bypass_ctrl {
	PM_TAPDELAY_BYPASS_DISABLE = 0,
	PM_TAPDELAY_BYPASS_ENABLE = 1,
};

enum sgmii_mode {
	PM_SGMII_DISABLE = 0,
	PM_SGMII_ENABLE = 1,
};

enum pm_register_access_id {
	CONFIG_REG_WRITE,
	CONFIG_REG_READ,
};

enum ospi_mux_select_type {
	PM_OSPI_MUX_SEL_DMA = 0,
	PM_OSPI_MUX_SEL_LINEAR = 1,
	PM_OSPI_MUX_GET_MODE = 2,
};

enum pm_reset_reason {
	PM_RESET_REASON_EXT_POR = 0,
	PM_RESET_REASON_SW_POR = 1,
	PM_RESET_REASON_SLR_POR = 2,
	PM_RESET_REASON_ERR_POR = 3,
	PM_RESET_REASON_DAP_SRST = 7,
	PM_RESET_REASON_ERR_SRST = 8,
	PM_RESET_REASON_SW_SRST = 9,
	PM_RESET_REASON_SLR_SRST = 10,
};

/**
 * struct zynqmp_pm_query_data - PM query data
 * @qid:	query ID
 * @arg1:	Argument 1 of query data
 * @arg2:	Argument 2 of query data
 * @arg3:	Argument 3 of query data
 */
struct zynqmp_pm_query_data {
	u32 qid;
	u32 arg1;
	u32 arg2;
	u32 arg3;
};

int zynqmp_pm_invoke_fn(u32 pm_api_id, u32 arg0, u32 arg1,
			u32 arg2, u32 arg3, u32 *ret_payload);

#if IS_REACHABLE(CONFIG_ZYNQMP_FIRMWARE)
int zynqmp_pm_get_api_version(u32 *version);
int zynqmp_pm_get_chipid(u32 *idcode, u32 *version);
int zynqmp_pm_query_data(struct zynqmp_pm_query_data qdata, u32 *out);
int zynqmp_pm_clock_enable(u32 clock_id);
int zynqmp_pm_clock_disable(u32 clock_id);
int zynqmp_pm_clock_getstate(u32 clock_id, u32 *state);
int zynqmp_pm_clock_setdivider(u32 clock_id, u32 divider);
int zynqmp_pm_clock_getdivider(u32 clock_id, u32 *divider);
int zynqmp_pm_clock_setrate(u32 clock_id, u64 rate);
int zynqmp_pm_clock_getrate(u32 clock_id, u64 *rate);
int zynqmp_pm_clock_setparent(u32 clock_id, u32 parent_id);
int zynqmp_pm_clock_getparent(u32 clock_id, u32 *parent_id);
int zynqmp_pm_set_pll_frac_mode(u32 clk_id, u32 mode);
int zynqmp_pm_get_pll_frac_mode(u32 clk_id, u32 *mode);
int zynqmp_pm_set_pll_frac_data(u32 clk_id, u32 data);
int zynqmp_pm_get_pll_frac_data(u32 clk_id, u32 *data);
int zynqmp_pm_set_sd_tapdelay(u32 node_id, u32 type, u32 value);
int zynqmp_pm_sd_dll_reset(u32 node_id, u32 type);
int zynqmp_pm_reset_assert(const u32 reset,
			   const enum zynqmp_pm_reset_action assert_flag);
int zynqmp_pm_reset_get_status(const u32 reset, u32 *status);
int zynqmp_pm_init_finalize(void);
int zynqmp_pm_set_suspend_mode(u32 mode);
int zynqmp_pm_request_node(const u32 node, const u32 capabilities,
			   const u32 qos, const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_release_node(const u32 node);
int zynqmp_pm_set_requirement(const u32 node, const u32 capabilities,
			      const u32 qos,
			      const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_aes_engine(const u64 address, u32 *out);
int zynqmp_pm_efuse_access(const u64 address, u32 *out);
int zynqmp_pm_secure_load(const u64 src_addr, u64 key_addr, u64 *dst);
int zynqmp_pm_load_pdi(const u32 src, const u64 address);
int zynqmp_pm_fpga_read(const u32 reg_numframes, const u64 phys_address, u32 readback_type, u32 *value);
int zynqmp_pm_sha_hash(const u64 address, const u32 size, const u32 flags);
int zynqmp_pm_rsa(const u64 address, const u32 size, const u32 flags);
int zynqmp_pm_config_reg_access(u32 register_access_id, u32 address, u32 mask,
				u32 value, u32 *out);
int zynqmp_pm_request_suspend(const u32 node,const enum zynqmp_pm_request_ack ack,
			      const u32 latency, const u32 state);
int zynqmp_pm_set_max_latency(const u32 node, const u32 latency);
int zynqmp_pm_set_configuration(const u32 physical_addr);
int zynqmp_pm_get_node_status(const u32 node, u32 *const status, u32 *const requirements, u32 *const usage);
int zynqmp_pm_get_operating_characteristic(const u32 node, const enum zynqmp_pm_opchar_type type, u32 *const result);
int zynqmp_pm_force_powerdown(const u32 target, const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_request_wakeup(const u32 node, const bool set_addr, const u64 address, const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_set_wakeup_source(const u32 target, const u32 wakeup_node, const u32 enable);
int zynqmp_pm_fpga_load(const u64 address, const u32 size, const u32 flags);
int zynqmp_pm_fpga_get_status(u32 *value);
int zynqmp_pm_write_ggs(u32 index, u32 value);
int zynqmp_pm_read_ggs(u32 index, u32 *value);
int zynqmp_pm_write_pggs(u32 index, u32 value);
int zynqmp_pm_read_pggs(u32 index, u32 *value);
int zynqmp_pm_usb_set_state(u32 node, u32 state, u32 value);
int zynqmp_pm_afi(u32 index, u32 value);
int zynqmp_pm_set_tapdelay_bypass(u32 index, u32 value);
int zynqmp_pm_set_sgmii_mode(u32 enable);
int zynqmp_pm_ulpi_reset(void);
int zynqmp_pm_probe_counter_read(u32 domain, u32 reg, u32 *value);
int zynqmp_pm_probe_counter_write(u32 domain, u32 reg, u32 value);
int zynqmp_pm_ospi_mux_select(u32 dev_id, u32 select);
int zynqmp_pm_get_last_reset_reason(u32 *reset_reason);
int zynqmp_pm_system_shutdown(const u32 type, const u32 subtype);
int zynqmp_pm_set_boot_health_status(u32 value);
int zynqmp_pm_force_pwrdwn(const u32 target,
			   const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_request_wake(const u32 node,
			   const bool set_addr,
			   const u64 address,
			   const enum zynqmp_pm_request_ack ack);
int zynqmp_pm_get_rpu_mode(u32 node_id, enum rpu_oper_mode *rpu_mode);
int zynqmp_pm_set_rpu_mode(u32 node_id, u32 arg1);
int zynqmp_pm_set_tcm_config(u32 node_id, u32 arg1);
int zynqmp_pm_clear_aie_npi_isr(u32 node, u32 irq_mask);
int zynqmp_pm_pinctrl_request(const u32 pin);
int zynqmp_pm_pinctrl_release(const u32 pin);
int zynqmp_pm_pinctrl_get_function(const u32 pin, u32 *id);
int zynqmp_pm_pinctrl_set_function(const u32 pin, const u32 id);
int zynqmp_pm_pinctrl_get_config(const u32 pin, const u32 param,
				 u32 *value);
int zynqmp_pm_pinctrl_set_config(const u32 pin, const u32 param,
				 u32 value);
#else
static inline struct zynqmp_eemi_ops *zynqmp_pm_get_eemi_ops(void)
{
	return ERR_PTR(-ENODEV);
}

static inline int zynqmp_pm_get_api_version(u32 *version)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_chipid(u32 *idcode, u32 *version)
{
	return -ENODEV;
}

static inline int zynqmp_pm_query_data(struct zynqmp_pm_query_data qdata,
				       u32 *out)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_enable(u32 clock_id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_disable(u32 clock_id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_getstate(u32 clock_id, u32 *state)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_setdivider(u32 clock_id, u32 divider)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_getdivider(u32 clock_id, u32 *divider)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_setrate(u32 clock_id, u64 rate)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_getrate(u32 clock_id, u64 *rate)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_setparent(u32 clock_id, u32 parent_id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clock_getparent(u32 clock_id, u32 *parent_id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_pll_frac_mode(u32 clk_id, u32 mode)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_pll_frac_mode(u32 clk_id, u32 *mode)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_pll_frac_data(u32 clk_id, u32 data)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_pll_frac_data(u32 clk_id, u32 *data)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_sd_tapdelay(u32 node_id, u32 type, u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_sd_dll_reset(u32 node_id, u32 type)
{
	return -ENODEV;
}

static inline int zynqmp_pm_reset_assert(const enum zynqmp_pm_reset reset,
					 const enum zynqmp_pm_reset_action assert_flag)
{
	return -ENODEV;
}

static inline int zynqmp_pm_reset_get_status(const enum zynqmp_pm_reset reset,
					     u32 *status)
{
	return -ENODEV;
}

static inline int zynqmp_pm_init_finalize(void)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_suspend_mode(u32 mode)
{
	return -ENODEV;
}

static inline int zynqmp_pm_request_node(const u32 node, const u32 capabilities,
					 const u32 qos,
					 const enum zynqmp_pm_request_ack ack)
{
	return -ENODEV;
}

static inline int zynqmp_pm_release_node(const u32 node)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_requirement(const u32 node,
					    const u32 capabilities,
					    const u32 qos,
					    const enum zynqmp_pm_request_ack ack)
{
	return -ENODEV;
}

static inline int zynqmp_pm_aes_engine(const u64 address, u32 *out)
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

static inline int zynqmp_pm_write_ggs(u32 index, u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_read_ggs(u32 index, u32 *value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_write_pggs(u32 index, u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_read_pggs(u32 index, u32 *value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_usb_set_state(u32 state, u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_afi(u32 index, u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_tapdelay_bypass(u32 index, u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_sgmii_mode(u32 enable)
{
	return -ENODEV;
}

static inline int zynqmp_pm_ulpi_reset(void)
{
	return -ENODEV;
}

static inline int zynqmp_pm_system_shutdown(const u32 type, const u32 subtype)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_boot_health_status(u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_force_pwrdwn(const u32 target,
					 const enum zynqmp_pm_request_ack ack)
{
	return -ENODEV;
}

static inline int zynqmp_pm_request_wake(const u32 node,
					 const bool set_addr,
					 const u64 address,
					 const enum zynqmp_pm_request_ack ack)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_rpu_mode(u32 node_id, enum rpu_oper_mode *rpu_mode)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_rpu_mode(u32 node_id, u32 arg1)
{
	return -ENODEV;
}

static inline int zynqmp_pm_set_tcm_config(u32 node_id, u32 arg1)
{
	return -ENODEV;
}

static inline int zynqmp_pm_clear_aie_npi_isr(u32 node, u32 irq_mask)
{
	return -ENODEV;
}

static inline int zynqmp_pm_pinctrl_request(const u32 pin)
{
	return -ENODEV;
}

static inline int zynqmp_pm_pinctrl_release(const u32 pin)
{
	return -ENODEV;
}

static inline int zynqmp_pm_pinctrl_get_function(const u32 pin, u32 *id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_pinctrl_set_function(const u32 pin, const u32 id)
{
	return -ENODEV;
}

static inline int zynqmp_pm_pinctrl_get_config(const u32 pin, const u32 param,
					       u32 *value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_pinctrl_set_config(const u32 pin, const u32 param,
					       u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_efuse_access(const u64 address, u32 *out)
{
	return -ENODEV;
}

static inline int zynqmp_pm_sha_hash(const u64 address, const u32 size,
				     const u32 flags)
{
	return -ENODEV;
}
static inline int zynqmp_pm_rsa(const u64 address, const u32 size,
				const u32 flags)
{
	return -ENODEV;
}

static inline int zynqmp_pm_config_reg_access(u32 register_access_id,
					      u32 address, u32 mask, u32 value,
					      u32 *out)
{
	return -ENODEV;
}

static inline int zynqmp_pm_request_suspend(const u32 node,
					    const enum zynqmp_pm_request_ack ack,
					    const u32 latency, const u32 state)
{
	return -ENODEV;
}
static inline int zynqmp_pm_set_max_latency(const u32 node, const u32 latency)
{
	return -ENODEV;
}
static inline int zynqmp_pm_set_configuration(const u32 physical_addr)
{
	return -ENODEV;
}
static inline int zynqmp_pm_get_node_status(const u32 node, u32 *const status,
					    u32 *const requirements,
					    u32 *const usage)
{
	return -ENODEV;
}
static inline int zynqmp_pm_get_operating_characteristic(const u32 node,
							 const enum zynqmp_pm_opchar_type type,
							 u32 *const result)
{
	return -ENODEV;
}
static inline int zynqmp_pm_force_powerdown(const u32 target,
					    const enum zynqmp_pm_request_ack ack)
{
	return -ENODEV;
}
static inline int zynqmp_pm_request_wakeup(const u32 node, const bool set_addr,
					   const u64 address,
					   const enum zynqmp_pm_request_ack ack)
{
	return -ENODEV;
}
static inline int zynqmp_pm_set_wakeup_source(const u32 target,
					      const u32 wakeup_node,
					      const u32 enable)
{
	return -ENODEV;
}

static inline int zynqmp_pm_fpga_read(const u32 reg_numframes,
				      const u64 phys_address, u32 readback_type,
				      u32 *value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_load_pdi(const u32 src, const u64 address)
{
	return -ENODEV;
}

static inline int zynqmp_pm_probe_counter_read(u32 deviceid, u32 reg, u32 *value)
{
	return -ENODEV;
}
static inline int zynqmp_pm_probe_counter_write(u32 reg, u32 value)
{
	return -ENODEV;
}

static inline int zynqmp_pm_ospi_mux_select(u32 dev_id, u32 select)
{
	return -ENODEV;
}

static inline int zynqmp_pm_secure_load(const u64 src_addr, u64 key_addr, u64 *dst)
{
	return -ENODEV;
}

static inline int zynqmp_pm_get_last_reset_reason(u32 *reset_reason)
{
	return -ENODEV;
}

#endif

#endif /* __FIRMWARE_ZYNQMP_H__ */
