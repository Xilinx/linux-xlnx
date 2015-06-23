/*
 * FPGA Framework
 *
 *  Copyright (C) 2013-2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#ifndef _LINUX_FPGA_MGR_H
#define _LINUX_FPGA_MGR_H

struct fpga_manager;

/**
 * struct fpga_manager_ops - ops for low level fpga manager drivers
 * @state: returns an enum value of the FPGA's state
 * @reset: put FPGA into reset state
 * @write_init: prepare the FPGA to receive confuration data
 * @write: write count bytes of configuration data to the FPGA
 * @write_complete: set FPGA to operating state after writing is done
 * @fpga_remove: optional: Set FPGA into a specific state during driver remove
 * @suspend: optional: low level fpga suspend
 * @resume: optional: low level fpga resume
 *
 * fpga_manager_ops are the low level functions implemented by a specific
 * fpga manager driver.  The optional ones are tested for NULL before being
 * called, so leaving them out is fine.
 */
struct fpga_manager_ops {
	enum fpga_mgr_states (*state)(struct fpga_manager *mgr);
	int (*reset)(struct fpga_manager *mgr);
	int (*write_init)(struct fpga_manager *mgr);
	int (*write)(struct fpga_manager *mgr, const char *buf, size_t count);
	int (*write_complete)(struct fpga_manager *mgr);
	void (*fpga_remove)(struct fpga_manager *mgr);
	int (*suspend)(struct fpga_manager *mgr);
	int (*resume)(struct fpga_manager *mgr);
};

/**
 * enum fpga_mgr_states - fpga framework states
 * @FPGA_MGR_STATE_UNKNOWN: can't determine state
 * @FPGA_MGR_STATE_POWER_OFF: FPGA power is off
 * @FPGA_MGR_STATE_POWER_UP: FPGA reports power is up
 * @FPGA_MGR_STATE_RESET: FPGA in reset state
 * @FPGA_MGR_STATE_FIRMWARE_REQ: firmware request in progress
 * @FPGA_MGR_STATE_FIRMWARE_REQ_ERR: firmware request failed
 * @FPGA_MGR_STATE_WRITE_INIT: preparing FPGA for programming
 * @FPGA_MGR_STATE_WRITE_INIT_ERR: Error during WRITE_INIT stage
 * @FPGA_MGR_STATE_WRITE: writing image to FPGA
 * @FPGA_MGR_STATE_WRITE_ERR: Error while writing FPGA
 * @FPGA_MGR_STATE_WRITE_COMPLETE: Doing post programming steps
 * @FPGA_MGR_STATE_WRITE_COMPLETE_ERR: Error during WRITE_COMPLETE
 * @FPGA_MGR_STATE_OPERATING: FPGA is programmed and operating
 */
enum fpga_mgr_states {
	FPGA_MGR_STATE_UNKNOWN,
	FPGA_MGR_STATE_POWER_OFF,
	FPGA_MGR_STATE_POWER_UP,
	FPGA_MGR_STATE_RESET,

	/* write sequence */
	FPGA_MGR_STATE_FIRMWARE_REQ,
	FPGA_MGR_STATE_FIRMWARE_REQ_ERR,
	FPGA_MGR_STATE_WRITE_INIT,
	FPGA_MGR_STATE_WRITE_INIT_ERR,
	FPGA_MGR_STATE_WRITE,
	FPGA_MGR_STATE_WRITE_ERR,
	FPGA_MGR_STATE_WRITE_COMPLETE,
	FPGA_MGR_STATE_WRITE_COMPLETE_ERR,

	FPGA_MGR_STATE_OPERATING,
};

/**
 * struct fpga_manager - fpga manager structure
 * @name: name of low level fpga manager
 * @dev: fpga manager device
 * @list: entry in list of all fpga managers
 * @lock: lock on calls to fpga manager ops
 * @state: state of fpga manager
 * @image_name: name of fpga image file if any
 * @mops: pointer to struct of fpga manager ops
 * @priv: low level driver private date
 */
struct fpga_manager {
	const char *name;
	struct device dev;
	struct list_head list;
	struct mutex lock;	/* lock on calls to ops */
	enum fpga_mgr_states state;
	char *image_name;

	const struct fpga_manager_ops *mops;
	void *priv;
};

#define to_fpga_manager(d) container_of(d, struct fpga_manager, dev)

int fpga_mgr_firmware_write(struct fpga_manager *mgr, const char *image_name);
int fpga_mgr_write(struct fpga_manager *mgr, const char *buf, size_t count);
int fpga_mgr_name(struct fpga_manager *mgr, char *buf);
int fpga_mgr_reset(struct fpga_manager *mgr);
int fpga_mgr_register(struct device *pdev, const char *name,
		      const struct fpga_manager_ops *mops, void *priv);
void fpga_mgr_remove(struct platform_device *pdev);

#endif /*_LINUX_FPGA_MGR_H */
