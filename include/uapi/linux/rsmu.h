/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Driver for the IDT ClockMatrix(TM) and 82p33xxx families of
 * timing and synchronization devices.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */

#ifndef __UAPI_LINUX_RSMU_CDEV_H
#define __UAPI_LINUX_RSMU_CDEV_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define MAX_NUM_PRIORITY_ENTRIES 32
#define TDC_FIFO_SIZE 16

/* Set dpll combomode */
struct rsmu_combomode {
	__u8 dpll;
	__u8 mode;
};

/* Get dpll state */
struct rsmu_get_state {
	__u8 dpll;
	__u8 state;
};

/* Get dpll fractional frequency offset (ffo) in ppqt */
struct rsmu_get_ffo {
	__u8 dpll;
	__s64 ffo;
};

/* Set holdover mode */
struct rsmu_holdover_mode {
	__u8 dpll;
	__u8 enable;
	__u8 mode;
};

/* Set output TDC go bit */
struct rsmu_set_output_tdc_go {
	__u8 tdc;
	__u8 enable;
};

/* Read/write register */
struct rsmu_reg_rw {
	__u32 offset;
	__u8 byte_count;
	__u8 bytes[256];
};

/* Get current clock index */
struct rsmu_current_clock_index {
	__u8 dpll;
	__s8 clock_index;
};

struct rsmu_priority_entry {
	__u8 clock_index;
	__u8 priority;
};

/* Set clock priorities */
struct rsmu_clock_priorities {
	__u8 dpll;
	__u8 num_entries;
	struct rsmu_priority_entry priority_entry[MAX_NUM_PRIORITY_ENTRIES];
};

struct rsmu_reference_monitor_status_alarms {
	__u8 los;
	__u8 no_activity;
	__u8 frequency_offset_limit;
};

/* Get reference monitor status */
struct rsmu_reference_monitor_status {
	__u8 clock_index;
	struct rsmu_reference_monitor_status_alarms alarms;
};

/* Get a TDC single-shot measurement in nanosecond */
struct rsmu_get_tdc_meas {
	bool continuous;
	__s64 offset;
};

/*
 * RSMU IOCTL List
 */
#define RSMU_MAGIC '?'

/**
 * @Description
 * ioctl to set SMU combo mode. Combo mode provides physical layer frequency
 * support from the Ethernet Equipment Clock to the PTP clock.
 *
 * @Parameters
 * pointer to struct rsmu_combomode that contains dpll combomode setting
 */
#define RSMU_SET_COMBOMODE  _IOW(RSMU_MAGIC, 1, struct rsmu_combomode)

/**
 * @Description
 * ioctl to get SMU dpll state. Application can call this API to tell if
 * SMU is locked to the GNSS signal.
 *
 * @Parameters
 * pointer to struct rsmu_get_state that contains dpll state
 */
#define RSMU_GET_STATE  _IOR(RSMU_MAGIC, 2, struct rsmu_get_state)

/**
 * @Description
 * ioctl to get SMU dpll fractional frequency offset (ffo).
 *
 * @Parameters
 * pointer to struct rsmu_get_ffo that contains dpll ffo in ppqt
 */
#define RSMU_GET_FFO  _IOR(RSMU_MAGIC, 3, struct rsmu_get_ffo)

/**
 * @Description
 * ioctl to enable/disable SMU HW holdover mode.
 *
 * @Parameters
 * pointer to struct rsmu_holdover_mode that contains enable flag
 */
#define RSMU_SET_HOLDOVER_MODE  _IOW(RSMU_MAGIC, 4, struct rsmu_holdover_mode)

/**
 * @Description
 * ioctl to set SMU output TDC go bit.
 *
 * @Parameters
 * pointer to struct rsmu_set_output_tdc_go that contains enable flag
 */
#define RSMU_SET_OUTPUT_TDC_GO  _IOW(RSMU_MAGIC, 5, struct rsmu_set_output_tdc_go)

/**
 * @Description
 * ioctl to get current SMU dpll clock index.
 *
 * @Parameters
 * pointer to struct rsmu_current_clock_index that contains clock index
 */
#define RSMU_GET_CURRENT_CLOCK_INDEX  _IOR(RSMU_MAGIC, 6, struct rsmu_current_clock_index)

/**
 * @Description
 * ioctl to set SMU dpll clock priorities.
 *
 * @Parameters
 * pointer to struct rsmu_clock_priorities that contains number of entries, clock index, and priority
 */
#define RSMU_SET_CLOCK_PRIORITIES  _IOW(RSMU_MAGIC, 7, struct rsmu_clock_priorities)

/**
 * @Description
 * ioctl to get SMU reference monitor status.
 *
 * @Parameters
 * pointer to struct rsmu_reference_monitor_status that contains reference monitor status
 */
#define RSMU_GET_REFERENCE_MONITOR_STATUS  _IOR(RSMU_MAGIC, 8, struct rsmu_reference_monitor_status)

/**
 * @Description
 * ioctl to get a one-shot tdc measurement (FC3W only).
 *
 * @Parameters
 * pointer to struct rsmu_get_tdc_meas that contains a one-shot tdc measurement
 */
#define RSMU_GET_TDC_MEAS  _IOR(RSMU_MAGIC, 9, struct rsmu_get_tdc_meas)

#define RSMU_REG_READ   _IOR(RSMU_MAGIC, 100, struct rsmu_reg_rw)
#define RSMU_REG_WRITE  _IOR(RSMU_MAGIC, 101, struct rsmu_reg_rw)
#endif /* __UAPI_LINUX_RSMU_CDEV_H */
