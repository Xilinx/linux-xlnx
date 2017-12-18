/*
 * FPGA Framework
 *
 *  Copyright (C) 2013-2015 Altera Corporation
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
#include <linux/mutex.h>
#include <linux/platform_device.h>

#ifndef _LINUX_FPGA_MGR_H
#define _LINUX_FPGA_MGR_H

#define ENCRYPTED_KEY_LEN	64 /* Bytes */
#define ENCRYPTED_IV_LEN	24 /* Bytes */

struct fpga_manager;

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
	/* default FPGA states */
	FPGA_MGR_STATE_UNKNOWN,
	FPGA_MGR_STATE_POWER_OFF,
	FPGA_MGR_STATE_POWER_UP,
	FPGA_MGR_STATE_RESET,

	/* getting an image for loading */
	FPGA_MGR_STATE_FIRMWARE_REQ,
	FPGA_MGR_STATE_FIRMWARE_REQ_ERR,

	/* write sequence: init, write, complete */
	FPGA_MGR_STATE_WRITE_INIT,
	FPGA_MGR_STATE_WRITE_INIT_ERR,
	FPGA_MGR_STATE_WRITE,
	FPGA_MGR_STATE_WRITE_ERR,
	FPGA_MGR_STATE_WRITE_COMPLETE,
	FPGA_MGR_STATE_WRITE_COMPLETE_ERR,

	/* fpga is programmed and operating */
	FPGA_MGR_STATE_OPERATING,
};

/*
 * FPGA Manager flags
 * FPGA_MGR_PARTIAL_RECONFIG: do partial reconfiguration if supported
 * FPGA_MGR_EXTERNAL_CONFIG: FPGA has been configured prior to Linux booting
 */
#define FPGA_MGR_PARTIAL_RECONFIG	BIT(0)
#define FPGA_MGR_EXTERNAL_CONFIG	BIT(1)

/**
 * struct fpga_image_info - information specific to a FPGA image
 * @flags: boolean flags as defined above
 * @enable_timeout_us: maximum time to enable traffic through bridge (uSec)
 * @disable_timeout_us: maximum time to disable traffic through bridge (uSec)
 */
struct fpga_image_info {
	u32 flags;
	u32 enable_timeout_us;
	u32 disable_timeout_us;
};

/**
 * struct fpga_manager_ops - ops for low level fpga manager drivers
 * @state: returns an enum value of the FPGA's state
 * @write_init: prepare the FPGA to receive confuration data
 * @write: write count bytes of configuration data to the FPGA
 * @write_complete: set FPGA to operating state after writing is done
 * @fpga_remove: optional: Set FPGA into a specific state during driver remove
 *
 * fpga_manager_ops are the low level functions implemented by a specific
 * fpga manager driver.  The optional ones are tested for NULL before being
 * called, so leaving them out is fine.
 */
struct fpga_manager_ops {
	enum fpga_mgr_states (*state)(struct fpga_manager *mgr);
	int (*write_init)(struct fpga_manager *mgr,
			  struct fpga_image_info *info,
			  const char *buf, size_t count);
	int (*write)(struct fpga_manager *mgr, const char *buf, size_t count);
	int (*write_complete)(struct fpga_manager *mgr,
			      struct fpga_image_info *info);
	void (*fpga_remove)(struct fpga_manager *mgr);
};

/**
 * struct fpga_manager - fpga manager structure
 * @name: name of low level fpga manager
 * @dev: fpga manager device
 * @ref_mutex: only allows one reference to fpga manager
 * @state: state of fpga manager
 * @mops: pointer to struct of fpga manager ops
 * @priv: low level driver private date
 */
struct fpga_manager {
	const char *name;
	long int flags;
	char key[ENCRYPTED_KEY_LEN];
	char iv[ENCRYPTED_IV_LEN];
	struct device dev;
	struct mutex ref_mutex;
	enum fpga_mgr_states state;
	const struct fpga_manager_ops *mops;
	void *priv;
};

#define to_fpga_manager(d) container_of(d, struct fpga_manager, dev)

int fpga_mgr_buf_load(struct fpga_manager *mgr, struct fpga_image_info *info,
		      const char *buf, size_t count);

int fpga_mgr_firmware_load(struct fpga_manager *mgr,
			   struct fpga_image_info *info,
			   const char *image_name);

struct fpga_manager *of_fpga_mgr_get(struct device_node *node);

struct fpga_manager *fpga_mgr_get(struct device *dev);

void fpga_mgr_put(struct fpga_manager *mgr);

int fpga_mgr_register(struct device *dev, const char *name,
		      const struct fpga_manager_ops *mops, void *priv);

void fpga_mgr_unregister(struct device *dev);

#endif /*_LINUX_FPGA_MGR_H */
