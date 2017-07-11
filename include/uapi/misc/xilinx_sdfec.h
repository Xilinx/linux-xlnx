/*
 * Xilinx SDFEC
 *
 * Copyright (C) 2016 - 2017 Xilinx, Inc.
 *
 * Description:
 * This driver is developed for SDFEC16 IP. It provides a char device
 * in sysfs and supports file operations like  open(), close() and ioctl().
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
#ifndef __XILINX_SDFEC_H__
#define __XILINX_SDFEC_H__

/* Shared LDPC Tables */
#define XSDFEC_LDPC_SC_TABLE_ADDR_BASE		(0x10000)
#define XSDFEC_LDPC_SC_TABLE_ADDR_HIGH		(0x103FC)
#define XSDFEC_LDPC_LA_TABLE_ADDR_BASE		(0x18000)
#define XSDFEC_LDPC_LA_TABLE_ADDR_HIGH		(0x18FFC)
#define XSDFEC_LDPC_QC_TABLE_ADDR_BASE		(0x20000)
#define XSDFEC_LDPC_QC_TABLE_ADDR_HIGH		(0x27FFC)

enum xsdfec_code {
	XSDFEC_CODE_INVALID = 0,
	XSDFEC_TURBO_CODE,
	XSDFEC_LDPC_CODE,
};

enum xsdfec_order {
	INVALID_ORDER = 0,
	MAINTAIN_ORDER,
	OUT_OF_ORDER,
};

enum xsdfec_state {
	XSDFEC_INIT = 0,
	XSDFEC_STARTED,
	XSDFEC_STOPPED,
	XSDFEC_NEEDS_RESET,
};

enum xsdfec_op_mode {
	XSDFEC_UNKNOWN_MODE = 0,
	XSDFEC_ENCODE,
	XSDFEC_DECODE,
};

/**
 * struct xsdfec_turbo - User data for Turbo Codes
 * @alg: Algorithm used by Turbo Codes
 * @scale: Scale Factor
 * Turbo Code structure to communicate parameters to XSDFEC driver
 */
struct xsdfec_turbo {
	bool alg;
	u8 scale;
};

/**
 * struct xsdfec_ldpc - User data for LDPC Codes
 * @n: Number of code word bits
 * @k: Number of information bits
 * @psize: Size of sub-matrix
 * @nlayers: Number of layers in code
 * @nqc: Quasi Cyclic Number
 * @nmqc: Number of M-sized QC operations in parity check matrix
 * @nm: Number of M-size vectors in N
 * @norm_type: Normalization required or not
 * @no_packing: Determines if multiple QC ops should be performed
 * @special_qc: Sub-Matrix property for Circulant weight > 0
 * @no_final_parity: Decide if final parity check needs to be performed
 * @max_schedule: Experimental code word scheduling limit
 * @lat_ctrl: Latency Control
 * @sc_off: SC offset
 * @la_off: LA offset
 * @qc_off: QC offset
 * @sc_table: SC Table
 * @la_table: LA Table
 * @qc_table: QC Table
 * @code_id: LDPC Code
 *
 * This structure describes the LDPC code that is passed to the driver
 * by the application.
 */
struct xsdfec_ldpc {
	u32 n;
	u32 k;
	u32 psize;
	u32 nlayers;
	u32 nqc;
	u32 nmqc;
	u32 nm;
	u32 norm_type;
	u32 no_packing;
	u32 special_qc;
	u32 no_final_parity;
	u32 max_schedule;
	u32 lat_ctrl;
	u32 sc_off;
	u32 la_off;
	u32 qc_off;
	u32 sc_table[XSDFEC_LDPC_SC_TABLE_ADDR_HIGH -
			XSDFEC_LDPC_SC_TABLE_ADDR_BASE];
	u32 la_table[XSDFEC_LDPC_LA_TABLE_ADDR_HIGH -
			XSDFEC_LDPC_LA_TABLE_ADDR_BASE];
	u32 qc_table[XSDFEC_LDPC_QC_TABLE_ADDR_HIGH -
			XSDFEC_LDPC_QC_TABLE_ADDR_BASE];
	u16 code_id;
};

/**
 * struct xsdfec_status - Status of SDFEC device
 * @fec_id: ID of SDFEC instance
 * @code: The codes being used by the SDFEC instance
 * @order: Order of Operation
 * @state: State of the SDFEC device
 * @mode: Mode of Operation
 * @activity: Describes if the SDFEC instance is Active
 * @cecc_count: Count of the Correctable ECC Errors occurred
 */
struct xsdfec_status {
	s32 fec_id;
	enum xsdfec_code code;
	enum xsdfec_order order;
	enum xsdfec_state state;
	enum xsdfec_op_mode mode;
	bool activity;
	int cecc_count;
};

/**
 * struct xsdfec_irq - Enabling or Disabling Interrupts
 * @enable_isr: If true enables the ISR
 * @enable_ecc_isr: If true enables the ECC ISR
 */
struct xsdfec_irq {
	bool enable_isr;
	bool enable_ecc_isr;
};

/*
 * XSDFEC IOCTL List
 */
#define XSDFEC_MAGIC		'f'
/* ioctl to start sdfec device */
#define XSDFEC_START_DEV	_IO(XSDFEC_MAGIC, 0)
/* ioctl to stop the device */
#define XSDFEC_STOP_DEV		_IO(XSDFEC_MAGIC, 1)
/* ioctl to communicate to the driver that device has been reset */
#define XSDFEC_RESET_REQ	_IO(XSDFEC_MAGIC, 2)
/* ioctl that returns status of sdfec device */
#define XSDFEC_GET_STATUS	_IOR(XSDFEC_MAGIC, 3, struct xsdfec_status *)
/* ioctl to enable or disable irq */
#define XSDFEC_SET_IRQ		_IOW(XSDFEC_MAGIC, 4, struct xsdfec_irq *)
/* ioctl to enable turbo params for sdfec device */
#define XSDFEC_SET_TURBO	_IOW(XSDFEC_MAGIC, 5, struct xsdfec_turbo *)
/* ioctl to add an LDPC code to the sdfec ldpc codes */
#define XSDFEC_ADD_LDPC		_IOW(XSDFEC_MAGIC, 6, struct xsdfec_ldpc *)

#endif /* __XILINX_SDFEC_H__ */
