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
	XSDFEC_INVALID_ORDER = 0,
	XSDFEC_MAINTAIN_ORDER,
	XSDFEC_OUT_OF_ORDER,
	XSDFEC_ORDER_MAX,
};

enum xsdfec_state {
	XSDFEC_INIT = 0,
	XSDFEC_STARTED,
	XSDFEC_STOPPED,
	XSDFEC_NEEDS_RESET,
};

enum xsdfec_axis_width {
	XSDFEC_1x128b = 1,
	XSDFEC_2x128b = 2,
	XSDFEC_4x128b = 4,
};

enum xsdfec_axis_word_include {
	XSDFEC_FIXED_VALUE = 0,
	XSDFEC_IN_BLOCK,
	XSDFEC_PER_AXI_TRANSACTION,
	XSDFEC_AXIS_WORDS_INCLUDE_MAX,
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
 * struct xsdfec_ldpc_params - User data for LDPC Codes
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
struct xsdfec_ldpc_params {
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
 * @state: State of the SDFEC device
 * @activity: Describes if the SDFEC instance is Active
 */
struct xsdfec_status {
	s32 fec_id;
	enum xsdfec_state state;
	bool activity;
};

/**
 * struct xsdfec_config - Configuration of SDFEC device
 * @fec_id: ID of SDFEC instance
 * @code: The codes being used by the SDFEC instance
 * @order: Order of Operation
 * @din_width: Width of the DIN AXI Stream
 * @din_word_include: How DIN_WORDS are inputted
 * @dout_width: Width of the DOUT AXI Stream
 * @dout_word_include: HOW DOUT_WORDS are outputted
 */
struct xsdfec_config {
	s32 fec_id;
	enum xsdfec_code code;
	enum xsdfec_order order;
	enum xsdfec_axis_width din_width;
	enum xsdfec_axis_word_include din_word_include;
	enum xsdfec_axis_width dout_width;
	enum xsdfec_axis_word_include dout_word_include;
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

/**
 * struct xsdfec_ioctl_stats - Stats retrived by ioctl XSDFEC_GET_STATS. Used
 *			       to buffer atomic_t variables from struct
 *			       xsdfec_dev.
 * @isr_err_count: Count of ISR errors
 * @cecc_count: Count of Correctable ECC errors (SBE)
 * @uecc_count: Count of Uncorrectable ECC errors (MBE)
 */
struct xsdfec_stats {
	u32 isr_err_count;
	u32 cecc_count;
	u32 uecc_count;
};

/*
 * XSDFEC IOCTL List
 */
#define XSDFEC_MAGIC		'f'
/* ioctl to start sdfec device */
#define XSDFEC_START_DEV	_IO(XSDFEC_MAGIC, 0)
/* ioctl to stop the device */
#define XSDFEC_STOP_DEV		_IO(XSDFEC_MAGIC, 1)
/* ioctl that returns status of sdfec device */
#define XSDFEC_GET_STATUS	_IOR(XSDFEC_MAGIC, 3, struct xsdfec_status *)
/* ioctl to enable or disable irq */
#define XSDFEC_SET_IRQ		_IOW(XSDFEC_MAGIC, 4, struct xsdfec_irq *)
/* ioctl to enable turbo params for sdfec device */
#define XSDFEC_SET_TURBO	_IOW(XSDFEC_MAGIC, 5, struct xsdfec_turbo *)
/* ioctl to add an LDPC code to the sdfec ldpc codes */
#define XSDFEC_ADD_LDPC_CODE_PARAMS	\
	_IOW(XSDFEC_MAGIC, 6, struct xsdfec_ldpc_params *)
/* ioctl that returns sdfec device configuration */
#define XSDFEC_GET_CONFIG	_IOR(XSDFEC_MAGIC, 7, struct xsdfec_config *)
/* ioctl that returns sdfec turbo param values */
#define XSDFEC_GET_TURBO	_IOR(XSDFEC_MAGIC, 8, struct xsdfec_turbo *)
/* ioctl that returns sdfec LDPC code param values, code_id must be specified */
#define XSDFEC_GET_LDPC_CODE_PARAMS \
	_IOWR(XSDFEC_MAGIC, 9, struct xsdfec_ldpc_params *)
/* ioctl that sets order, if order of blocks can change from input to output */
#define XSDFEC_SET_ORDER	_IOW(XSDFEC_MAGIC, 10, unsigned long *)
/*
 * ioctl that sets bypass.
 * setting a value of 0 results in normal operation.
 * setting a value of 1 results in the sdfec performing the configured
 * operations (same number of cycles) but output data matches the input data
 */
#define XSDFEC_SET_BYPASS	_IOW(XSDFEC_MAGIC, 11, unsigned long *)
/* ioctl that determines if sdfec is processing data */
#define XSDFEC_IS_ACTIVE	_IOR(XSDFEC_MAGIC, 12, bool *)
/* ioctl that clears error stats collected during interrupts */
#define XSDFEC_CLEAR_STATS	_IO(XSDFEC_MAGIC, 13)
/* ioctl that returns sdfec device stats */
#define XSDFEC_GET_STATS	_IOR(XSDFEC_MAGIC, 14, struct xsdfec_stats *)
/* ioctl that returns sdfec device to default config, use after a reset */
#define XSDFEC_SET_DEFAULT_CONFIG _IO(XSDFEC_MAGIC, 15)

#endif /* __XILINX_SDFEC_H__ */
