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

#include <linux/device.h>

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

/* Feature check status */
#define PM_FEATURE_INVALID		-1
#define PM_FEATURE_UNCHECKED		0

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
	PM_SET_CONFIGURATION,
	PM_GET_NODE_STATUS,
	PM_GET_OPERATING_CHARACTERISTIC,
	PM_REGISTER_NOTIFIER,
	/* API for suspending */
	PM_REQUEST_SUSPEND,
	PM_SELF_SUSPEND,
	PM_FORCE_POWERDOWN,
	PM_ABORT_SUSPEND,
	PM_REQUEST_WAKEUP,
	PM_SET_WAKEUP_SOURCE,
	PM_SYSTEM_SHUTDOWN,
	/* API for managing PM slaves: */
	PM_REQUEST_NODE,
	PM_RELEASE_NODE,
	PM_SET_REQUIREMENT,
	PM_SET_MAX_LATENCY,
	/* Direct control API functions: */
	PM_RESET_ASSERT,
	PM_RESET_GET_STATUS,
	PM_PM_INIT_FINALIZE = 21,
	PM_FPGA_LOAD,
	PM_FPGA_GET_STATUS,
	PM_GET_CHIPID = 24,
	/* ID 25 is been used by U-boot to process secure boot images */
	/* Secure library generic API functions */
	PM_SECURE_SHA = 26,
	PM_SECURE_RSA,
	/* Pin control API functions */
	PM_PINCTRL_REQUEST,
	PM_PINCTRL_RELEASE,
	PM_PINCTRL_GET_FUNCTION,
	PM_PINCTRL_SET_FUNCTION,
	PM_PINCTRL_CONFIG_PARAM_GET,
	PM_PINCTRL_CONFIG_PARAM_SET,
	PM_IOCTL,
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
	PM_SECURE_IMAGE,
	PM_FPGA_READ = 46,
	PM_SECURE_AES,
	/* PM_REGISTER_ACCESS API */
	PM_REGISTER_ACCESS = 52,
	PM_EFUSE_ACCESS = 53,
	PM_FEATURE_CHECK = 63,
	PM_API_MAX,
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
	IOCTL_GET_RPU_OPER_MODE,
	IOCTL_SET_RPU_OPER_MODE,
	IOCTL_RPU_BOOT_ADDR_CONFIG,
	IOCTL_TCM_COMB_CONFIG,
	IOCTL_SET_TAPDELAY_BYPASS,
	IOCTL_SET_SGMII_MODE,
	IOCTL_SD_DLL_RESET,
	IOCTL_SET_SD_TAPDELAY,
	IOCTL_SET_PLL_FRAC_MODE,
	IOCTL_GET_PLL_FRAC_MODE,
	IOCTL_SET_PLL_FRAC_DATA,
	IOCTL_GET_PLL_FRAC_DATA,
	IOCTL_WRITE_GGS,
	IOCTL_READ_GGS,
	IOCTL_WRITE_PGGS,
	IOCTL_READ_PGGS,
	/* IOCTL for ULPI reset */
	IOCTL_ULPI_RESET,
	/* Set healthy bit value*/
	IOCTL_SET_BOOT_HEALTH_STATUS,
	IOCTL_AFI,
	/* Probe counter read/write */
	IOCTL_PROBE_COUNTER_READ,
	IOCTL_PROBE_COUNTER_WRITE,
	IOCTL_OSPI_MUX_SELECT,
	/* IOCTL for USB power request */
	IOCTL_USB_SET_STATE,
	/* IOCTL to get last reset reason */
	IOCTL_GET_LAST_RESET_REASON,
	/* AIE ISR Clear */
	IOCTL_AIE_ISR_CLEAR,
};

enum pm_query_id {
	PM_QID_INVALID,
	PM_QID_CLOCK_GET_NAME,
	PM_QID_CLOCK_GET_TOPOLOGY,
	PM_QID_CLOCK_GET_FIXEDFACTOR_PARAMS,
	PM_QID_CLOCK_GET_PARENTS,
	PM_QID_CLOCK_GET_ATTRIBUTES,
	PM_QID_PINCTRL_GET_NUM_PINS,
	PM_QID_PINCTRL_GET_NUM_FUNCTIONS,
	PM_QID_PINCTRL_GET_NUM_FUNCTION_GROUPS,
	PM_QID_PINCTRL_GET_FUNCTION_NAME,
	PM_QID_PINCTRL_GET_FUNCTION_GROUPS,
	PM_QID_PINCTRL_GET_PIN_GROUPS,
	PM_QID_CLOCK_GET_NUM_CLOCKS,
	PM_QID_CLOCK_GET_MAX_DIVISOR,
	PM_QID_PLD_GET_PARENT,
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
	ZYNQMP_PM_ABORT_REASON_POWER_UNIT_BUSY,
	ZYNQMP_PM_ABORT_REASON_NO_POWERDOWN,
	ZYNQMP_PM_ABORT_REASON_UNKNOWN,
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
	PM_PINCTRL_CONFIG_SLEW_RATE,
	PM_PINCTRL_CONFIG_BIAS_STATUS,
	PM_PINCTRL_CONFIG_PULL_CTRL,
	PM_PINCTRL_CONFIG_SCHMITT_CMOS,
	PM_PINCTRL_CONFIG_DRIVE_STRENGTH,
	PM_PINCTRL_CONFIG_VOLTAGE_STATUS,
	PM_PINCTRL_CONFIG_TRI_STATE,
	PM_PINCTRL_CONFIG_MAX,
};

enum pm_pinctrl_slew_rate {
	PM_PINCTRL_SLEW_RATE_FAST,
	PM_PINCTRL_SLEW_RATE_SLOW,
};

enum pm_pinctrl_bias_status {
	PM_PINCTRL_BIAS_DISABLE,
	PM_PINCTRL_BIAS_ENABLE,
};

enum pm_pinctrl_pull_ctrl {
	PM_PINCTRL_BIAS_PULL_DOWN,
	PM_PINCTRL_BIAS_PULL_UP,
};

enum pm_pinctrl_schmitt_cmos {
	PM_PINCTRL_INPUT_TYPE_CMOS,
	PM_PINCTRL_INPUT_TYPE_SCHMITT,
};

enum zynqmp_pm_opchar_type {
	ZYNQMP_PM_OPERATING_CHARACTERISTIC_POWER = 1,
	ZYNQMP_PM_OPERATING_CHARACTERISTIC_ENERGY,
	ZYNQMP_PM_OPERATING_CHARACTERISTIC_TEMPERATURE,
};

enum pm_pinctrl_drive_strength {
	PM_PINCTRL_DRIVE_STRENGTH_2MA,
	PM_PINCTRL_DRIVE_STRENGTH_4MA,
	PM_PINCTRL_DRIVE_STRENGTH_8MA,
	PM_PINCTRL_DRIVE_STRENGTH_12MA,
};

enum pm_pinctrl_tri_state {
	PM_PINCTRL_TRI_STATE_DISABLE = 0,
	PM_PINCTRL_TRI_STATE_ENABLE,
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

enum rpu_oper_mode {
	PM_RPU_MODE_LOCKSTEP,
	PM_RPU_MODE_SPLIT,
};

enum rpu_boot_mem {
	PM_RPU_BOOTMEM_LOVEC,
	PM_RPU_BOOTMEM_HIVEC,
};

enum rpu_tcm_comb {
	PM_RPU_TCM_SPLIT,
	PM_RPU_TCM_COMB,
};

enum tap_delay_signal_type {
	PM_TAPDELAY_NAND_DQS_IN,
	PM_TAPDELAY_NAND_DQS_OUT,
	PM_TAPDELAY_QSPI,
	PM_TAPDELAY_MAX,
};

enum tap_delay_bypass_ctrl {
	PM_TAPDELAY_BYPASS_DISABLE,
	PM_TAPDELAY_BYPASS_ENABLE,
};

enum sgmii_mode {
	PM_SGMII_DISABLE,
	PM_SGMII_ENABLE,
};

enum pm_register_access_id {
	CONFIG_REG_WRITE,
	CONFIG_REG_READ,
};

enum ospi_mux_select_type {
	PM_OSPI_MUX_SEL_DMA,
	PM_OSPI_MUX_SEL_LINEAR,
	PM_OSPI_MUX_GET_MODE,
};

enum pm_node_id {
	NODE_UNKNOWN = 0,
	NODE_APU,
	NODE_APU_0,
	NODE_APU_1,
	NODE_APU_2,
	NODE_APU_3,
	NODE_RPU,
	NODE_RPU_0,
	NODE_RPU_1,
	NODE_PLD,
	NODE_FPD,
	NODE_OCM_BANK_0,
	NODE_OCM_BANK_1,
	NODE_OCM_BANK_2,
	NODE_OCM_BANK_3,
	NODE_TCM_0_A,
	NODE_TCM_0_B,
	NODE_TCM_1_A,
	NODE_TCM_1_B,
	NODE_L2,
	NODE_GPU_PP_0,
	NODE_GPU_PP_1,
	NODE_USB_0,
	NODE_USB_1,
	NODE_TTC_0,
	NODE_TTC_1,
	NODE_TTC_2,
	NODE_TTC_3,
	NODE_SATA,
	NODE_ETH_0,
	NODE_ETH_1,
	NODE_ETH_2,
	NODE_ETH_3,
	NODE_UART_0,
	NODE_UART_1,
	NODE_SPI_0,
	NODE_SPI_1,
	NODE_I2C_0,
	NODE_I2C_1,
	NODE_SD_0,
	NODE_SD_1,
	NODE_DP,
	NODE_GDMA,
	NODE_ADMA,
	NODE_NAND,
	NODE_QSPI,
	NODE_GPIO,
	NODE_CAN_0,
	NODE_CAN_1,
	NODE_EXTERN,
	NODE_APLL,
	NODE_VPLL,
	NODE_DPLL,
	NODE_RPLL,
	NODE_IOPLL,
	NODE_DDR,
	NODE_IPI_APU,
	NODE_IPI_RPU_0,
	NODE_GPU,
	NODE_PCIE,
	NODE_PCAP,
	NODE_RTC,
	NODE_LPD,
	NODE_VCU,
	NODE_IPI_RPU_1,
	NODE_IPI_PL_0,
	NODE_IPI_PL_1,
	NODE_IPI_PL_2,
	NODE_IPI_PL_3,
	NODE_PL,
	NODE_GEM_TSU,
	NODE_SWDT_0,
	NODE_SWDT_1,
	NODE_CSU,
	NODE_PJTAG,
	NODE_TRACE,
	NODE_TESTSCAN,
	NODE_PMU,
	NODE_MAX,
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

struct zynqmp_eemi_ops {
	int (*get_api_version)(u32 *version);
	int (*get_chipid)(u32 *idcode, u32 *version);
	int (*fpga_load)(const u64 address, const u32 size, const u32 flags);
	int (*fpga_get_status)(u32 *value);
	int (*query_data)(struct zynqmp_pm_query_data qdata, u32 *out);
	int (*clock_enable)(u32 clock_id);
	int (*clock_disable)(u32 clock_id);
	int (*clock_getstate)(u32 clock_id, u32 *state);
	int (*clock_setdivider)(u32 clock_id, u32 divider);
	int (*clock_getdivider)(u32 clock_id, u32 *divider);
	int (*clock_setrate)(u32 clock_id, u64 rate);
	int (*clock_getrate)(u32 clock_id, u64 *rate);
	int (*clock_setparent)(u32 clock_id, u32 parent_id);
	int (*clock_getparent)(u32 clock_id, u32 *parent_id);
	int (*ioctl)(u32 node_id, u32 ioctl_id, u32 arg1, u32 arg2, u32 *out);
	int (*reset_assert)(const u32 reset,
			    const enum zynqmp_pm_reset_action assert_flag);
	int (*reset_get_status)(const u32 reset, u32 *status);
	int (*init_finalize)(void);
	int (*set_suspend_mode)(u32 mode);
	int (*request_node)(const u32 node,
			    const u32 capabilities,
			    const u32 qos,
			    const enum zynqmp_pm_request_ack ack);
	int (*release_node)(const u32 node);
	int (*set_requirement)(const u32 node,
			       const u32 capabilities,
			       const u32 qos,
			       const enum zynqmp_pm_request_ack ack);
	int (*fpga_read)(const u32 reg_numframes, const u64 phys_address,
			 u32 readback_type, u32 *value);
	int (*sha_hash)(const u64 address, const u32 size, const u32 flags);
	int (*rsa)(const u64 address, const u32 size, const u32 flags);
	int (*request_suspend)(const u32 node,
			       const enum zynqmp_pm_request_ack ack,
			       const u32 latency,
			       const u32 state);
	int (*force_powerdown)(const u32 target,
			       const enum zynqmp_pm_request_ack ack);
	int (*request_wakeup)(const u32 node,
			      const bool set_addr,
			      const u64 address,
			      const enum zynqmp_pm_request_ack ack);
	int (*set_wakeup_source)(const u32 target,
				 const u32 wakeup_node,
				 const u32 enable);
	int (*system_shutdown)(const u32 type, const u32 subtype);
	int (*set_max_latency)(const u32 node, const u32 latency);
	int (*set_configuration)(const u32 physical_addr);
	int (*get_node_status)(const u32 node, u32 *const status,
			       u32 *const requirements, u32 *const usage);
	int (*get_operating_characteristic)(const u32 node,
					    const enum zynqmp_pm_opchar_type
					    type, u32 *const result);
	int (*pinctrl_request)(const u32 pin);
	int (*pinctrl_release)(const u32 pin);
	int (*pinctrl_get_function)(const u32 pin, u32 *id);
	int (*pinctrl_set_function)(const u32 pin, const u32 id);
	int (*pinctrl_get_config)(const u32 pin, const u32 param, u32 *value);
	int (*pinctrl_set_config)(const u32 pin, const u32 param, u32 value);
	int (*register_access)(u32 register_access_id, u32 address,
			       u32 mask, u32 value, u32 *out);
	int (*aes)(const u64 address, u32 *out);
	int (*efuse_access)(const u64 address, u32 *out);
	int (*secure_image)(const u64 src_addr, u64 key_addr, u64 *dst);
	int (*pdi_load)(const u32 src, const u64 address);
};

int zynqmp_pm_invoke_fn(u32 pm_api_id, u32 arg0, u32 arg1,
			u32 arg2, u32 arg3, u32 *ret_payload);

int zynqmp_pm_ggs_init(struct kobject *parent_kobj);

#if IS_REACHABLE(CONFIG_ARCH_ZYNQMP)
const struct zynqmp_eemi_ops *zynqmp_pm_get_eemi_ops(void);
int zynqmp_pm_get_last_reset_reason(u32 *reset_reason);
#else
static inline struct zynqmp_eemi_ops *zynqmp_pm_get_eemi_ops(void)
{
	return ERR_PTR(-ENODEV);
}

static inline int zynqmp_pm_get_last_reset_reason(u32 *reset_reason)
{
	return -ENODEV;
}
#endif

#endif /* __FIRMWARE_ZYNQMP_H__ */
