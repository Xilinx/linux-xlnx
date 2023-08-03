// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx HDCP Transmitter Interface driver
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: Lakshmi Prasanna Eachuri <lakshmi.prasanna.eachuri@amd.com>
 *
 * This driver acts like an interface layer between HDCP1X and HDCP2X protocols
 * for Xilinx transmitter subsystem devices.
 *
 * This driver initializes the HDCP IP and its internal modules based on
 * downstream capabilities and starts Authentication.
 *
 * Currently HDCP2X protocol and its functionalities only enabled in this driver.
 */

#include <linux/xlnx/xlnx_hdcp_rng.h>
#include <linux/xlnx/xlnx_hdcp_common.h>
#include "xlnx_hdcp_tx.h"
#include "xlnx_hdcp1x_tx.h"
#include "xlnx_hdcp2x_tx.h"

#define XDPTX_TIMER_CLOCK_FREQ_HZ 99999001U

void xlnx_hdcptx_read_ds_sink_capability(struct xlnx_hdcptx *xtxhdcp)
{
	int status = 0;

	if (xtxhdcp->hdcp2xenable) {
		if (xlnx_hdcp2x_downstream_capbility(xtxhdcp->xhdcp2x)) {
			xtxhdcp->hdcp_protocol = XHDCPTX_HDCP_2X;
			status = true;
		}
	}
	if (xtxhdcp->hdcp1xenable) {
		if (xlnx_hdcp1x_downstream_capbility(xtxhdcp->xhdcp1x)) {
			xtxhdcp->hdcp_protocol = XHDCPTX_HDCP_1X;
			status = true;
		}
	}
	if (!status)
		xtxhdcp->hdcp_protocol = XHDCPTX_HDCP_NONE;
}

static void hdcp_task_monitor_fun(struct work_struct *work)
{
	struct xlnx_hdcptx *xtxhdcp;

	xtxhdcp = container_of(work, struct xlnx_hdcptx, hdcp_task_monitor.work);
	if (xtxhdcp->hdcp_protocol == XHDCPTX_HDCP_2X) {
		struct xlnx_hdcp2x_config  *xhdcp2x = xtxhdcp->xhdcp2x;

		mutex_lock(&xtxhdcp->hdcptx_mutex);
		xtxhdcp->auth_status = xlnx_hdcp2x_task_monitor(xhdcp2x);
		if (xhdcp2x->handlers.notify_handler)
			xhdcp2x->handlers.notify_handler(xhdcp2x->interface_ref,
							 xtxhdcp->auth_status);
		mutex_unlock(&xtxhdcp->hdcptx_mutex);
	} else if (xtxhdcp->hdcp_protocol == XHDCPTX_HDCP_1X) {
		struct xlnx_hdcp1x_config  *xhdcp1x = xtxhdcp->xhdcp1x;

		mutex_lock(&xtxhdcp->hdcptx_mutex);
		xtxhdcp->auth_status = xlnx_hdcp1x_task_monitor(xhdcp1x);
		if (xhdcp1x->handlers.notify_handler)
			xhdcp1x->handlers.notify_handler(xhdcp1x->interface_ref,
							  xtxhdcp->auth_status);
		mutex_unlock(&xtxhdcp->hdcptx_mutex);
	} else {
		dev_err(xtxhdcp->dev, "Task Monitor is Failed\n");
		dev_err(xtxhdcp->dev, "Unsupported protocol\n");
	}
}

void xlnx_hdcp_tx_process_cp_irq(struct xlnx_hdcptx *xtxhdcp)
{
	if (xtxhdcp->hdcp2xenable) {
		struct xlnx_hdcp2x_config  *xhdcp2x = xtxhdcp->xhdcp2x;

		if (xtxhdcp->hdcp_protocol == XHDCPTX_HDCP_2X)
			xlnx_hdcp2x_tx_process_cp_irq(xhdcp2x);
	}
}

/**
 * xlnx_hdcp_tx_init - Initialize HDCP transmitter based on hardware selection
 * and downstream capability
 * @dev: device structure
 * @protocol_ref: DP/HDMI structure reference
 * @xtxhdcp: Xilinx HDCP core driver structure
 * @hdcp_base_address: HDCP core address
 * @is_repeater: Repeater selection
 * @hdcp_type: HDCP protocol selection
 * @lane_count: Number of lanes data to be encrypted
 * @hw_protocol: Interface type HDMI or DP
 * @key_base_address: HDCP Key Management base address
 * return: HDCP 1x/2x driver structure if success or return memory allocation error
 */
void *xlnx_hdcp_tx_init(struct device *dev, void *protocol_ref,
			struct xlnx_hdcptx *xtxhdcp, void __iomem *hdcp_base_address,
			u8 is_repeater,	enum xlnx_hdcptx_protocol_type hdcp_type, u8 lane_count,
			int hw_protocol, void __iomem *key_base_address)
{
	struct xlnx_hdcp2x_config  *xhdcp2x;
	struct xlnx_hdcp1x_config  *xhdcp1x;
	void *hdcp_drv_address;
	int ret;

	if (hdcp_type == XHDCPTX_HDCP_2X) {
		xhdcp2x = devm_kzalloc(dev, sizeof(*xhdcp2x), GFP_KERNEL);
		if (!xhdcp2x)
			return ERR_PTR(-ENOMEM);

		hdcp_drv_address = xhdcp2x;
		xhdcp2x->xhdcp2x_hw.hdcp2xcore_address =
					(void __iomem *)hdcp_base_address;

		xhdcp2x->xhdcp2x_hw.protocol = hw_protocol;
		xhdcp2x->dev = dev;
		xhdcp2x->interface_ref = protocol_ref;
		xhdcp2x->interface_base = hdcp_base_address;
		xhdcp2x->is_repeater = is_repeater ? 1 : 0;
		xhdcp2x->lane_count = lane_count;

		ret = xlnx_hdcp2x_tx_init(xhdcp2x, xhdcp2x->is_repeater);
		if (ret < 0) {
			dev_err(xhdcp2x->dev, "Failed to initialize HDCP2X engine\n");
			goto hdcp2x_error;
		}
	}
	if (hdcp_type == XHDCPTX_HDCP_1X) {
		xhdcp1x = devm_kzalloc(dev, sizeof(*xhdcp1x), GFP_KERNEL);
		if (!xhdcp1x)
			return ERR_PTR(-ENOMEM);

		hdcp_drv_address = xhdcp1x;
		xhdcp1x->dev = dev;
		xhdcp1x->interface_ref = protocol_ref;
		xhdcp1x->interface_base = hdcp_base_address;
		xhdcp1x->is_repeater = is_repeater ? 1 : 0;
		xhdcp1x->lane_count = lane_count;
		xhdcp1x->hdcp1x_keymgmt_base = (void __iomem *)key_base_address;
		ret = xlnx_hdcp1x_tx_init(xhdcp1x, xhdcp1x->is_repeater);
		if (ret < 0) {
			dev_err(xhdcp1x->dev, "Failed to initialize HDCP1X engine\n");
			goto hdcp1x_error;
		}
	}
	if (hdcp_type == XHDCPTX_HDCP_NONE)
		return ERR_PTR(-ENOMEM);

	mutex_init(&xtxhdcp->hdcptx_mutex);
	INIT_DELAYED_WORK(&xtxhdcp->hdcp_task_monitor, hdcp_task_monitor_fun);

	return (void *)hdcp_drv_address;

hdcp1x_error:
	devm_kfree(dev, xhdcp1x);

hdcp2x_error:
	devm_kfree(dev, xhdcp2x);

	return ERR_PTR(-ENOMEM);
}

/**
 * xlnx_hdcp_timer_init - This function initializes timer submodule
 * and driver structure parameters
 * @dev: device structure
 * @timer_base_address: Xilinx timer core address
 * return: timer driver structure address if success or return memory
 * allocation error
 */
void *xlnx_hdcp_timer_init(struct device *dev, void __iomem *timer_base_address)
{
	struct xlnx_hdcp_timer_config  *xhdcptmr;
	int ret;

	xhdcptmr = devm_kzalloc(dev, sizeof(*xhdcptmr), GFP_KERNEL);
	if (!xhdcptmr)
		return ERR_PTR(-ENOMEM);

	xhdcptmr->hw_config.coreaddress = (void __iomem *)timer_base_address;
	xhdcptmr->hw_config.sys_clock_freq = XDPTX_TIMER_CLOCK_FREQ_HZ;

	ret = xlnx_hdcp_tmrcntr_init(xhdcptmr);
	if (ret < 0)
		goto error;

	return xhdcptmr;

error:
	devm_kfree(dev, xhdcptmr);

	return ERR_PTR(-ENOMEM);
}

int xlnx_hdcp_tx_exit(struct xlnx_hdcptx *xtxhdcp)
{
	struct xlnx_hdcp2x_config  *xhdcp2x = xtxhdcp->xhdcp2x;
	struct xlnx_hdcp1x_config  *xhdcp1x = xtxhdcp->xhdcp1x;

	if (xtxhdcp->hdcp2xenable) {
		if (xtxhdcp->xhdcp2x) {
			devm_kfree(xtxhdcp->dev, xhdcp2x);
		} else {
			dev_err(xtxhdcp->dev, "HDCP2X is not initialized\n");
			goto hdcp_error;
		}
	}
	if (xtxhdcp->hdcp1xenable) {
		if (xtxhdcp->xhdcp1x) {
			devm_kfree(xtxhdcp->dev, xhdcp1x);
		} else {
			dev_err(xtxhdcp->dev, "Hdcp1x is not initialized\n");
			goto hdcp_error;
		}
	}
	return 0;

hdcp_error:
	return -EINVAL;
}

void xlnx_hdcp_tx_timer_exit(struct xlnx_hdcptx *xtxhdcp)
{
	struct xlnx_hdcp_timer_config  *xhdcptmr = xtxhdcp->xhdcptmr;

	if (xtxhdcp->xhdcptmr)
		devm_kfree(xtxhdcp->dev, xhdcptmr);
}

int xlnx_hdcp_tx_set_keys(struct xlnx_hdcptx *xtxhdcp, const u8 *data)
{
	int ret = 0;
	struct xlnx_hdcp2x_config  *xhdcp2x = xtxhdcp->xhdcp2x;
	struct xlnx_hdcp1x_config  *xhdcp1x = xtxhdcp->xhdcp1x;
	u8 local_srm[XHDCP2X_TX_SRM_SIZE];
	u8 local_lc128[XHDCP2X_TX_LC128_SIZE];

	memcpy(local_lc128, data, XHDCP2X_TX_LC128_SIZE);
	memcpy(local_srm,
	       data + XHDCP2X_TX_LC128_SIZE, XHDCP2X_TX_SRM_SIZE);

	if (xtxhdcp->hdcp2xenable) {
		ret = xlnx_hdcp2x_loadkeys(xhdcp2x, local_srm, local_lc128);
		if (ret < 0)
			return -EINVAL;
	}
	if (xtxhdcp->hdcp2xenable) {
		ret = xlnx_hdcp1x_set_keys(xhdcp1x, (u8 *)data);
		if (ret < 0)
			return -EINVAL;
	}

	xtxhdcp->is_enckey_available = true;

	return ret;
}

int xlnx_hdcp_tx_reset(struct xlnx_hdcptx *xtxhdcp)
{
	int ret;

	if (!(xtxhdcp->hdcp2xenable || xtxhdcp->hdcp1xenable))
		return -EINVAL;

	if (xtxhdcp->hdcp2xenable) {
		struct xlnx_hdcp2x_config  *xhdcp2x = xtxhdcp->xhdcp2x;

		cancel_delayed_work_sync(&xtxhdcp->hdcp_task_monitor);
		xtxhdcp->hdcp_protocol = XHDCPTX_HDCP_NONE;
		mutex_lock(&xtxhdcp->hdcptx_mutex);

		ret = xlnx_hdcp2x_tx_reset(xhdcp2x);
		if (ret < 0) {
			mutex_unlock(&xtxhdcp->hdcptx_mutex);
			return -EINVAL;
		}
		mutex_unlock(&xtxhdcp->hdcptx_mutex);
	}

	if (xtxhdcp->hdcp1xenable) {
		struct xlnx_hdcp1x_config  *xhdcp1x = xtxhdcp->xhdcp1x;

		cancel_delayed_work_sync(&xtxhdcp->hdcp_task_monitor);
		xtxhdcp->hdcp_protocol = XHDCPTX_HDCP_NONE;
		mutex_lock(&xtxhdcp->hdcptx_mutex);

		ret = xlnx_hdcp1x_tx_reset(xhdcp1x);
		if (ret < 0) {
			mutex_unlock(&xtxhdcp->hdcptx_mutex);
			return -EINVAL;
		}
		mutex_unlock(&xtxhdcp->hdcptx_mutex);
	}

	return 0;
}

static void xlnx_hcdp_tx_timer_callback(void *xtxhdcptr, u8 tmrcntr_number)
{
	struct xlnx_hdcptx *xtxhdcp = xtxhdcptr;
	struct xlnx_hdcp2x_config  *xhdcp2x = xtxhdcp->xhdcp2x;

	mutex_lock(&xtxhdcp->hdcptx_mutex);
	xlnx_hdcp2x_tx_timer_handler((void *)xhdcp2x, tmrcntr_number);
	mutex_unlock(&xtxhdcp->hdcptx_mutex);
}

int xlnx_start_hdcp_engine(struct xlnx_hdcptx *xtxhdcp, u8 lanecount)
{
	if (!(xtxhdcp->hdcp2xenable || xtxhdcp->hdcp1xenable))
		return -EINVAL;

	if (!xtxhdcp->is_enckey_available)
		return -EINVAL;

	if (xtxhdcp->hdcp2xenable) {
		struct xlnx_hdcp2x_config  *xhdcp2x = xtxhdcp->xhdcp2x;

		xlnx_hdcptx_read_ds_sink_capability(xtxhdcp);

		if (xtxhdcp->hdcp_protocol == XHDCPTX_HDCP_2X) {
			xlnx_hdcp2x_tx_timer_init(xhdcp2x, xtxhdcp->xhdcptmr);
			xlnx_hdcp_tmrcntr_set_handler(xtxhdcp->xhdcptmr,
						      xlnx_hcdp_tx_timer_callback,
						      (void *)xtxhdcp);
			xhdcp2x->lane_count = lanecount;
			xlnx_start_hdcp2x_engine(xhdcp2x);
			schedule_delayed_work(&xtxhdcp->hdcp_task_monitor, 0);
		}
	} else if (xtxhdcp->hdcp1xenable) {
		struct xlnx_hdcp1x_config  *xhdcp1x = xtxhdcp->xhdcp1x;

		xlnx_hdcptx_read_ds_sink_capability(xtxhdcp);
		if (xtxhdcp->hdcp_protocol == XHDCPTX_HDCP_1X) {
			xhdcp1x->lane_count = lanecount;
			xlnx_start_hdcp1x_engine(xhdcp1x);
			schedule_delayed_work(&xtxhdcp->hdcp_task_monitor, 0);
		}
	} else {
		dev_err(xtxhdcp->dev, "Downstream is HDCP1x\n");
		dev_err(xtxhdcp->dev, "Not supported Currently\n");
		return -EINVAL;
	}

	return 0;
}

int xlnx_hdcp_tx_set_callback(void *ref,
			      enum xlnx_hdcptx_callback_type callback_type,
			      void *callbackfunc)
{
	int ret = 0;
	struct xlnx_hdcptx *xtxhdcp = (struct xlnx_hdcptx *)ref;

	if (xtxhdcp->hdcp2xenable) {
		struct xlnx_hdcp2x_config  *xhdcp2x = xtxhdcp->xhdcp2x;

		switch (callback_type) {
		case XHDCP2X_TX_HANDLER_DP_AUX_READ:
			xhdcp2x->handlers.rd_handler = callbackfunc;
			break;
		case XHDCP2X_TX_HANDLER_DP_AUX_WRITE:
			xhdcp2x->handlers.wr_handler = callbackfunc;
			break;
		case XHDCP2X_TX_HANDLER_HDCP_STATUS:
			xhdcp2x->handlers.notify_handler = callbackfunc;
			break;
		default:
			dev_err(xtxhdcp->dev, "Invalid handler type\n");
			ret = -EINVAL;
			break;
		}
	}
	if (xtxhdcp->hdcp1xenable) {
		struct xlnx_hdcp1x_config  *xhdcp1x = xtxhdcp->xhdcp1x;

		switch (callback_type) {
		case XHDCP1X_TX_HANDLER_DP_AUX_READ:
			xhdcp1x->handlers.rd_handler = callbackfunc;
			break;
		case XHDCP1X_TX_HANDLER_DP_AUX_WRITE:
			xhdcp1x->handlers.wr_handler = callbackfunc;
			break;
		case XHDCP1X_TX_HANDLER_HDCP_STATUS:
			xhdcp1x->handlers.notify_handler = callbackfunc;
			break;
		default:
			dev_err(xtxhdcp->dev, "Invalid handler type\n");
			ret = -EINVAL;
			break;
		}
	}
	return ret;
}
