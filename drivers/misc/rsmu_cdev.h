/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * This driver is developed for the IDT ClockMatrix(TM) of
 * timing and synchronization devices.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */
#ifndef __LINUX_RSMU_CDEV_H
#define __LINUX_RSMU_CDEV_H

#include <linux/firmware.h>
#include <linux/miscdevice.h>
#include <linux/regmap.h>
#include <uapi/linux/rsmu.h>

struct rsmu_ops;

#define FW_NAME_LEN_MAX	256

/**
 * Define function to set bitfield value of read data from device
 *
 * This macro function can be used after a register is read from a
 * device.  This macro <b>doesn't</b> access the device.
 *
 * regVal  register data value.
 * mask    bitfield mask
 * lsb     least significant bit
 * data    bitfield data to set
 */
#define rsmu_set_bitfield(regVal, mask, lsb, data) \
({						   \
	regVal = ((regVal & ~(mask)) |             \
		  ((data << lsb) & (mask))         \
		 );				   \
})

/**
 * Define function to get bitfield value of read data from device
 *
 * This macro function can be used after a register is read from a
 * device.  This macro <b>doesn't</b> access the device.
 *
 * regVal  register data value.
 * mask    bitfield mask
 * lsb     least significant bit
 */
#define rsmu_get_bitfield(regVal, mask, lsb) \
	((regVal & mask) >> lsb)


enum holdover_mode {
	HOLDOVER_MODE_AUTOMATIC = 0,
	HOLDOVER_MODE_MANUAL = 1,
	HOLDOVER_MODE_MAX = HOLDOVER_MODE_MANUAL,
};

/**
 * struct rsmu_cdev - Driver data for RSMU character device
 * @name: rsmu device name as rsmu[index]
 * @dev: pointer to device
 * @mfd: pointer to MFD device
 * @miscdev: character device handle
 * @regmap: I2C/SPI regmap handle
 * @lock: mutex to protect operations from being interrupted
 * @type: rsmu device type, passed through platform data
 * @ops: rsmu device methods
 * @index: rsmu device index
 */
struct rsmu_cdev {
	char name[16];
	struct device *dev;
	struct device *mfd;
	struct miscdevice miscdev;
	struct regmap *regmap;
	struct mutex *lock;
	enum rsmu_type type;
	struct rsmu_ops *ops;
	u8 fw_version;
	int index;
};

extern struct rsmu_ops cm_ops;
extern struct rsmu_ops sabre_ops;

struct rsmu_ops {
	enum rsmu_type type;
	int (*set_combomode)(struct rsmu_cdev *rsmu, u8 dpll, u8 mode);
	int (*get_dpll_state)(struct rsmu_cdev *rsmu, u8 dpll, u8 *state);
	int (*get_fw_version)(struct rsmu_cdev *rsmu);
	int (*get_dpll_ffo)(struct rsmu_cdev *rsmu, u8 dpll,
			    struct rsmu_get_ffo *ffo);
	int (*set_holdover_mode)(struct rsmu_cdev *rsmu, u8 dpll,
				 u8 enable, u8 mode);
	int (*set_output_tdc_go)(struct rsmu_cdev *rsmu, u8 tdc,
				 u8 enable);
	int (*load_firmware)(struct rsmu_cdev *rsmu, char fwname[FW_NAME_LEN_MAX]);
	int (*get_clock_index)(struct rsmu_cdev *rsmu, u8 dpll, s8 *clock_index);
	int (*set_clock_priorities)(struct rsmu_cdev *rsmu, u8 dpll, u8 number_entries,
				    struct rsmu_priority_entry *priority_entry);
	int (*get_reference_monitor_status)(struct rsmu_cdev *rsmu, u8 clock_index,
					    struct rsmu_reference_monitor_status_alarms *alarms);
};

/**
 * Enumerated type listing DPLL combination modes
 */
enum rsmu_dpll_combomode {
	E_COMBOMODE_CURRENT = 0,
	E_COMBOMODE_FASTAVG,
	E_COMBOMODE_SLOWAVG,
	E_COMBOMODE_HOLDOVER,
	E_COMBOMODE_MAX
};

/**
 * An id used to identify the respective child class states.
 */
enum rsmu_class_state {
	E_SRVLOINITIALSTATE = 0,
	E_SRVLOUNQUALIFIEDSTATE = 1,
	E_SRVLOLOCKACQSTATE = 2,
	E_SRVLOFREQUENCYLOCKEDSTATE = 3,
	E_SRVLOTIMELOCKEDSTATE = 4,
	E_SRVLOHOLDOVERINSPECSTATE = 5,
	E_SRVLOHOLDOVEROUTOFSPECSTATE = 6,
	E_SRVLOFREERUNSTATE = 7,
	E_SRVNUMBERLOSTATES = 8,
	E_SRVLOSTATEINVALID = 9,
};
#endif
