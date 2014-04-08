/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
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
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/watchdog.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hbm.h"
#include "hw-me.h"
#include "client.h"

static const u8 mei_start_wd_params[] = { 0x02, 0x12, 0x13, 0x10 };
static const u8 mei_stop_wd_params[] = { 0x02, 0x02, 0x14, 0x10 };

/*
 * AMT Watchdog Device
 */
#define INTEL_AMT_WATCHDOG_ID "INTCAMT"

/* UUIDs for AMT F/W clients */
const uuid_le mei_wd_guid = UUID_LE(0x05B79A6F, 0x4628, 0x4D7F, 0x89,
						0x9D, 0xA9, 0x15, 0x14, 0xCB,
						0x32, 0xAB);

static void mei_wd_set_start_timeout(struct mei_device *dev, u16 timeout)
{
	dev_dbg(&dev->pdev->dev, "wd: set timeout=%d.\n", timeout);
	memcpy(dev->wd_data, mei_start_wd_params, MEI_WD_HDR_SIZE);
	memcpy(dev->wd_data + MEI_WD_HDR_SIZE, &timeout, sizeof(u16));
}

/**
 * mei_wd_host_init - connect to the watchdog client
 *
 * @dev: the device structure
 *
 * returns -ENENT if wd client cannot be found
 *         -EIO if write has failed
 *         0 on success
 */
int mei_wd_host_init(struct mei_device *dev)
{
	struct mei_cl *cl = &dev->wd_cl;
	int id;
	int ret;

	mei_cl_init(cl, dev);

	dev->wd_timeout = MEI_WD_DEFAULT_TIMEOUT;
	dev->wd_state = MEI_WD_IDLE;


	/* check for valid client id */
	id = mei_me_cl_by_uuid(dev, &mei_wd_guid);
	if (id < 0) {
		dev_info(&dev->pdev->dev, "wd: failed to find the client\n");
		return id;
	}

	cl->me_client_id = dev->me_clients[id].client_id;

	ret = mei_cl_link(cl, MEI_WD_HOST_CLIENT_ID);

	if (ret < 0) {
		dev_info(&dev->pdev->dev, "wd: failed link client\n");
		return ret;
	}

	cl->state = MEI_FILE_CONNECTING;

	if (mei_hbm_cl_connect_req(dev, cl)) {
		dev_err(&dev->pdev->dev, "wd: failed to connect to the client\n");
		cl->state = MEI_FILE_DISCONNECTED;
		cl->host_client_id = 0;
		return -EIO;
	}
	cl->timer_count = MEI_CONNECT_TIMEOUT;

	return 0;
}

/**
 * mei_wd_send - sends watch dog message to fw.
 *
 * @dev: the device structure
 *
 * returns 0 if success,
 *	-EIO when message send fails
 *	-EINVAL when invalid message is to be sent
 */
int mei_wd_send(struct mei_device *dev)
{
	struct mei_msg_hdr hdr;

	hdr.host_addr = dev->wd_cl.host_client_id;
	hdr.me_addr = dev->wd_cl.me_client_id;
	hdr.msg_complete = 1;
	hdr.reserved = 0;

	if (!memcmp(dev->wd_data, mei_start_wd_params, MEI_WD_HDR_SIZE))
		hdr.length = MEI_WD_START_MSG_SIZE;
	else if (!memcmp(dev->wd_data, mei_stop_wd_params, MEI_WD_HDR_SIZE))
		hdr.length = MEI_WD_STOP_MSG_SIZE;
	else
		return -EINVAL;

	return mei_write_message(dev, &hdr, dev->wd_data);
}

/**
 * mei_wd_stop - sends watchdog stop message to fw.
 *
 * @dev: the device structure
 * @preserve: indicate if to keep the timeout value
 *
 * returns 0 if success,
 *	-EIO when message send fails
 *	-EINVAL when invalid message is to be sent
 */
int mei_wd_stop(struct mei_device *dev)
{
	int ret;

	if (dev->wd_cl.state != MEI_FILE_CONNECTED ||
	    dev->wd_state != MEI_WD_RUNNING)
		return 0;

	memcpy(dev->wd_data, mei_stop_wd_params, MEI_WD_STOP_MSG_SIZE);

	dev->wd_state = MEI_WD_STOPPING;

	ret = mei_cl_flow_ctrl_creds(&dev->wd_cl);
	if (ret < 0)
		goto out;

	if (ret && dev->hbuf_is_ready) {
		ret = 0;
		dev->hbuf_is_ready = false;

		if (!mei_wd_send(dev)) {
			ret = mei_cl_flow_ctrl_reduce(&dev->wd_cl);
			if (ret)
				goto out;
		} else {
			dev_err(&dev->pdev->dev, "wd: send stop failed\n");
		}

		dev->wd_pending = false;
	} else {
		dev->wd_pending = true;
	}

	mutex_unlock(&dev->device_lock);

	ret = wait_event_interruptible_timeout(dev->wait_stop_wd,
					dev->wd_state == MEI_WD_IDLE,
					msecs_to_jiffies(MEI_WD_STOP_TIMEOUT));
	mutex_lock(&dev->device_lock);
	if (dev->wd_state == MEI_WD_IDLE) {
		dev_dbg(&dev->pdev->dev, "wd: stop completed ret=%d.\n", ret);
		ret = 0;
	} else {
		if (!ret)
			ret = -ETIMEDOUT;
		dev_warn(&dev->pdev->dev,
			"wd: stop failed to complete ret=%d.\n", ret);
	}

out:
	return ret;
}

/*
 * mei_wd_ops_start - wd start command from the watchdog core.
 *
 * @wd_dev - watchdog device struct
 *
 * returns 0 if success, negative errno code for failure
 */
static int mei_wd_ops_start(struct watchdog_device *wd_dev)
{
	int err = -ENODEV;
	struct mei_device *dev;

	dev = watchdog_get_drvdata(wd_dev);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	if (dev->dev_state != MEI_DEV_ENABLED) {
		dev_dbg(&dev->pdev->dev,
			"wd: dev_state != MEI_DEV_ENABLED  dev_state = %s\n",
			mei_dev_state_str(dev->dev_state));
		goto end_unlock;
	}

	if (dev->wd_cl.state != MEI_FILE_CONNECTED)	{
		dev_dbg(&dev->pdev->dev,
			"MEI Driver is not connected to Watchdog Client\n");
		goto end_unlock;
	}

	mei_wd_set_start_timeout(dev, dev->wd_timeout);

	err = 0;
end_unlock:
	mutex_unlock(&dev->device_lock);
	return err;
}

/*
 * mei_wd_ops_stop -  wd stop command from the watchdog core.
 *
 * @wd_dev - watchdog device struct
 *
 * returns 0 if success, negative errno code for failure
 */
static int mei_wd_ops_stop(struct watchdog_device *wd_dev)
{
	struct mei_device *dev;

	dev = watchdog_get_drvdata(wd_dev);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);
	mei_wd_stop(dev);
	mutex_unlock(&dev->device_lock);

	return 0;
}

/*
 * mei_wd_ops_ping - wd ping command from the watchdog core.
 *
 * @wd_dev - watchdog device struct
 *
 * returns 0 if success, negative errno code for failure
 */
static int mei_wd_ops_ping(struct watchdog_device *wd_dev)
{
	int ret = 0;
	struct mei_device *dev;

	dev = watchdog_get_drvdata(wd_dev);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	if (dev->wd_cl.state != MEI_FILE_CONNECTED) {
		dev_err(&dev->pdev->dev, "wd: not connected.\n");
		ret = -ENODEV;
		goto end;
	}

	dev->wd_state = MEI_WD_RUNNING;

	/* Check if we can send the ping to HW*/
	if (dev->hbuf_is_ready && mei_cl_flow_ctrl_creds(&dev->wd_cl) > 0) {

		dev->hbuf_is_ready = false;
		dev_dbg(&dev->pdev->dev, "wd: sending ping\n");

		if (mei_wd_send(dev)) {
			dev_err(&dev->pdev->dev, "wd: send failed.\n");
			ret = -EIO;
			goto end;
		}

		if (mei_cl_flow_ctrl_reduce(&dev->wd_cl)) {
			dev_err(&dev->pdev->dev,
				"wd: mei_cl_flow_ctrl_reduce() failed.\n");
			ret = -EIO;
			goto end;
		}

	} else {
		dev->wd_pending = true;
	}

end:
	mutex_unlock(&dev->device_lock);
	return ret;
}

/*
 * mei_wd_ops_set_timeout - wd set timeout command from the watchdog core.
 *
 * @wd_dev - watchdog device struct
 * @timeout - timeout value to set
 *
 * returns 0 if success, negative errno code for failure
 */
static int mei_wd_ops_set_timeout(struct watchdog_device *wd_dev,
		unsigned int timeout)
{
	struct mei_device *dev;

	dev = watchdog_get_drvdata(wd_dev);
	if (!dev)
		return -ENODEV;

	/* Check Timeout value */
	if (timeout < MEI_WD_MIN_TIMEOUT || timeout > MEI_WD_MAX_TIMEOUT)
		return -EINVAL;

	mutex_lock(&dev->device_lock);

	dev->wd_timeout = timeout;
	wd_dev->timeout = timeout;
	mei_wd_set_start_timeout(dev, dev->wd_timeout);

	mutex_unlock(&dev->device_lock);

	return 0;
}

/*
 * Watchdog Device structs
 */
static const struct watchdog_ops wd_ops = {
		.owner = THIS_MODULE,
		.start = mei_wd_ops_start,
		.stop = mei_wd_ops_stop,
		.ping = mei_wd_ops_ping,
		.set_timeout = mei_wd_ops_set_timeout,
};
static const struct watchdog_info wd_info = {
		.identity = INTEL_AMT_WATCHDOG_ID,
		.options = WDIOF_KEEPALIVEPING |
			   WDIOF_SETTIMEOUT |
			   WDIOF_ALARMONLY,
};

static struct watchdog_device amt_wd_dev = {
		.info = &wd_info,
		.ops = &wd_ops,
		.timeout = MEI_WD_DEFAULT_TIMEOUT,
		.min_timeout = MEI_WD_MIN_TIMEOUT,
		.max_timeout = MEI_WD_MAX_TIMEOUT,
};


void mei_watchdog_register(struct mei_device *dev)
{
	if (watchdog_register_device(&amt_wd_dev)) {
		dev_err(&dev->pdev->dev,
			"wd: unable to register watchdog device.\n");
		return;
	}

	dev_dbg(&dev->pdev->dev,
		"wd: successfully register watchdog interface.\n");
	watchdog_set_drvdata(&amt_wd_dev, dev);
}

void mei_watchdog_unregister(struct mei_device *dev)
{
	if (watchdog_get_drvdata(&amt_wd_dev) == NULL)
		return;

	watchdog_set_drvdata(&amt_wd_dev, NULL);
	watchdog_unregister_device(&amt_wd_dev);
}

