/*
 * Xilinx Zynq MPSoC Firmware layer
 *
 *  Copyright (C) 2014-2017 Xilinx
 *
 *  Michal Simek <michal.simek@xilinx.com>
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

#ifndef __SOC_ZYNQMP_FIRMWARE_H__
#define __SOC_ZYNQMP_FIRMWARE_H__

/* SMC SIP service Call Function Identifier Prefix */
#define PM_SIP_SVC	0xC2000000
#define GET_CALLBACK_DATA 0xa01
#define SET_SUSPEND_MODE  0xa02

/* Number of 32bits values in payload */
#define PAYLOAD_ARG_CNT	5U

/* Number of arguments for a callback */
#define CB_ARG_CNT	4

/* Payload size (consists of callback API ID + arguments) */
#define CB_PAYLOAD_SIZE	(CB_ARG_CNT + 1)

/* Global general storage register base address */
#define GGS_BASEADDR	(0xFFD80030U)
#define GSS_NUM_REGS	(4)

/* Persistent global general storage register base address */
#define PGGS_BASEADDR	(0xFFD80050U)
#define PGSS_NUM_REGS	(4)

enum pm_api_id {
	/* Miscellaneous API functions: */
	GET_API_VERSION = 1,
	SET_CONFIGURATION,
	GET_NODE_STATUS,
	GET_OPERATING_CHARACTERISTIC,
	REGISTER_NOTIFIER,
	/* API for suspending of PUs: */
	REQUEST_SUSPEND,
	SELF_SUSPEND,
	FORCE_POWERDOWN,
	ABORT_SUSPEND,
	REQUEST_WAKEUP,
	SET_WAKEUP_SOURCE,
	SYSTEM_SHUTDOWN,
	/* API for managing PM slaves: */
	REQUEST_NODE,
	RELEASE_NODE,
	SET_REQUIREMENT,
	SET_MAX_LATENCY,
	/* Direct control API functions: */
	RESET_ASSERT,
	RESET_GET_STATUS,
	MMIO_WRITE,
	MMIO_READ,
	PM_INIT_FINALIZE,
	FPGA_LOAD,
	FPGA_GET_STATUS,
	GET_CHIPID,
};

/* PMU-FW return status codes */
enum pm_ret_status {
	XST_PM_SUCCESS = 0,
	XST_PM_INTERNAL	= 2000,
	XST_PM_CONFLICT,
	XST_PM_NO_ACCESS,
	XST_PM_INVALID_NODE,
	XST_PM_DOUBLE_REQ,
	XST_PM_ABORT_SUSPEND,
};

#endif /* __SOC_ZYNQMP_FIRMWARE_H__ */
