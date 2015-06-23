/*
 * FPGA Manager Core
 *
 *  Copyright (C) 2013-2014 Altera Corporation
 *
 * With code from the mailing list:
 * Copyright (C) 2013 Xilinx, Inc.
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
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/slab.h>

static DEFINE_MUTEX(fpga_mgr_mutex);
static DEFINE_IDA(fpga_mgr_ida);
static struct class *fpga_mgr_class;

static LIST_HEAD(fpga_manager_list);

/**
 * fpga_mgr_low_level_state - get FPGA state from low level driver
 * @mgr: fpga manager
 *
 * This will be used to initialize framework state
 *
 * Return: an enum value for state.
 */
static enum fpga_mgr_states fpga_mgr_low_level_state(struct fpga_manager *mgr)
{
	if (!mgr || !mgr->mops || !mgr->mops->state)
		return FPGA_MGR_STATE_UNKNOWN;

	return mgr->mops->state(mgr);
}

/**
 * __fpga_mgr_reset - unlocked version of fpga_mgr_reset
 * @mgr: fpga manager
 *
 * Return: 0 on success, error code otherwise.
 */
static int __fpga_mgr_reset(struct fpga_manager *mgr)
{
	int ret;

	if (!mgr->mops->reset)
		return -EINVAL;

	ret = mgr->mops->reset(mgr);

	mgr->state = fpga_mgr_low_level_state(mgr);
	kfree(mgr->image_name);
	mgr->image_name = NULL;

	return ret;
}

/**
 * fpga_mgr_reset - reset the fpga
 * @mgr: fpga manager
 *
 * Return: 0 on success, error code otherwise.
 */
int fpga_mgr_reset(struct fpga_manager *mgr)
{
	int ret;

	if (!mutex_trylock(&mgr->lock))
		return -EBUSY;

	ret = __fpga_mgr_reset(mgr);

	mutex_unlock(&mgr->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(fpga_mgr_reset);

/**
 * __fpga_mgr_stage_init - prepare fpga for configuration
 * @mgr: fpga manager
 *
 * Return: 0 on success, error code otherwise.
 */
static int __fpga_mgr_stage_write_init(struct fpga_manager *mgr)
{
	int ret;

	if (mgr->mops->write_init) {
		mgr->state = FPGA_MGR_STATE_WRITE_INIT;
		ret = mgr->mops->write_init(mgr);
		if (ret) {
			mgr->state = FPGA_MGR_STATE_WRITE_INIT_ERR;
			return ret;
		}
	}

	return 0;
}

/**
 * __fpga_mgr_stage_write - write buffer to fpga
 * @mgr: fpga manager
 * @buf: buffer contain fpga image
 * @count: byte count of buf
 *
 * Return: 0 on success, error code otherwise.
 */
static int __fpga_mgr_stage_write(struct fpga_manager *mgr, const char *buf,
				  size_t count)
{
	int ret;

	mgr->state = FPGA_MGR_STATE_WRITE;
	ret = mgr->mops->write(mgr, buf, count);
	if (ret) {
		mgr->state = FPGA_MGR_STATE_WRITE_ERR;
		return ret;
	}

	return 0;
}

/**
 * __fpga_mgr_stage_complete - after writing, place fpga in operating state
 * @mgr: fpga manager
 *
 * Return: 0 on success, error code otherwise.
 */
static int __fpga_mgr_stage_write_complete(struct fpga_manager *mgr)
{
	int ret;

	if (mgr->mops->write_complete) {
		mgr->state = FPGA_MGR_STATE_WRITE_COMPLETE;
		ret = mgr->mops->write_complete(mgr);
		if (ret) {
			mgr->state = FPGA_MGR_STATE_WRITE_COMPLETE_ERR;
			return ret;
		}
	}

	mgr->state = fpga_mgr_low_level_state(mgr);

	return 0;
}

/**
 * __fpga_mgr_write - whole fpga image write cycle
 * @mgr: fpga manager
 * @buf: buffer contain fpga image
 * @count: byte count of buf
 *
 * Return: 0 on success, error code otherwise.
 */
static int __fpga_mgr_write(struct fpga_manager *mgr, const char *buf,
			    size_t count)
{
	int ret;

	ret = __fpga_mgr_stage_write_init(mgr);
	if (ret)
		return ret;

	ret = __fpga_mgr_stage_write(mgr, buf, count);
	if (ret)
		return ret;

	return __fpga_mgr_stage_write_complete(mgr);
}

/**
 * fpga_mgr_write - do complete fpga image write cycle
 * @mgr: fpga manager
 * @buf: buffer contain fpga image
 * @count: byte count of buf
 *
 * Return: 0 on success, error code otherwise.
 */
int fpga_mgr_write(struct fpga_manager *mgr, const char *buf, size_t count)
{
	int ret;

	if (!mutex_trylock(&mgr->lock))
		return -EBUSY;

	dev_info(&mgr->dev, "writing buffer to %s\n", mgr->name);

	ret = __fpga_mgr_write(mgr, buf, count);
	mutex_unlock(&mgr->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(fpga_mgr_write);

/**
 * fpga_mgr_firmware_write - request firmware and write to fpga
 * @mgr: fpga manager
 * @image_name: name of image file on the firmware search path
 *
 * Grab lock, request firmware, and write out to the FPGA.
 * Update the state before each step to provide info on what step
 * failed if there is a failure.
 *
 * Return: 0 on success, error code otherwise.
 */
int fpga_mgr_firmware_write(struct fpga_manager *mgr, const char *image_name)
{
	const struct firmware *fw;
	int ret;

	if (!mutex_trylock(&mgr->lock))
		return -EBUSY;

	dev_info(&mgr->dev, "writing %s to %s\n", image_name, mgr->name);

	mgr->state = FPGA_MGR_STATE_FIRMWARE_REQ;
	ret = request_firmware(&fw, image_name, &mgr->dev);
	if (ret) {
		mgr->state = FPGA_MGR_STATE_FIRMWARE_REQ_ERR;
		goto fw_wr_exit;
	}

	ret = __fpga_mgr_write(mgr, fw->data, fw->size);
	if (ret)
		goto fw_wr_exit;

	kfree(mgr->image_name);
	mgr->image_name = kstrdup(image_name, GFP_KERNEL);

fw_wr_exit:
	mutex_unlock(&mgr->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(fpga_mgr_firmware_write);

/**
 * fpga_mgr_name - returns the fpga manager name
 * @mgr: fpga manager
 * @buf: buffer to receive the name
 *
 * Return: number of chars in buf excluding null byte on success;
 * error code otherwise.
 */
int fpga_mgr_name(struct fpga_manager *mgr, char *buf)
{
	if (!mgr)
		return -ENODEV;

	return sprintf(buf, "%s\n", mgr->name);
}
EXPORT_SYMBOL_GPL(fpga_mgr_name);

static const char * const state_str[] = {
	[FPGA_MGR_STATE_UNKNOWN] =		"unknown",
	[FPGA_MGR_STATE_POWER_OFF] =		"power_off",
	[FPGA_MGR_STATE_POWER_UP] =		"power_up",
	[FPGA_MGR_STATE_RESET] =		"reset",

	/* write sequence */
	[FPGA_MGR_STATE_FIRMWARE_REQ] =		"firmware_request",
	[FPGA_MGR_STATE_FIRMWARE_REQ_ERR] =	"firmware_request_err",
	[FPGA_MGR_STATE_WRITE_INIT] =		"write_init",
	[FPGA_MGR_STATE_WRITE_INIT_ERR] =	"write_init_err",
	[FPGA_MGR_STATE_WRITE] =		"write",
	[FPGA_MGR_STATE_WRITE_ERR] =		"write_err",
	[FPGA_MGR_STATE_WRITE_COMPLETE] =	"write_complete",
	[FPGA_MGR_STATE_WRITE_COMPLETE_ERR] =	"write_complete_err",

	[FPGA_MGR_STATE_OPERATING] =		"operating",
};

/*
 * class attributes
 */
static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	return fpga_mgr_name(mgr, buf);
}

static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	return sprintf(buf, "%s\n", state_str[mgr->state]);
}

static ssize_t firmware_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	if (!mgr->image_name)
		return 0;

	return sprintf(buf, "%s\n", mgr->image_name);
}

static ssize_t firmware_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);
	unsigned int len;
	char image_name[NAME_MAX];
	int ret;

	/* lose terminating \n */
	strcpy(image_name, buf);
	len = strlen(image_name);
	if (image_name[len - 1] == '\n')
		image_name[len - 1] = 0;

	ret = fpga_mgr_firmware_write(mgr, image_name);
	if (ret)
		return ret;

	return count;
}

static ssize_t reset_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val == 1) {
		ret = fpga_mgr_reset(mgr);
		if (ret)
			return ret;
	} else {
		return -EINVAL;
	}

	return count;
}

static DEVICE_ATTR_RO(name);
static DEVICE_ATTR_RO(state);
static DEVICE_ATTR_RW(firmware);
static DEVICE_ATTR_WO(reset);

static struct attribute *fpga_mgr_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_state.attr,
	&dev_attr_firmware.attr,
	&dev_attr_reset.attr,
	NULL,
};
ATTRIBUTE_GROUPS(fpga_mgr);

static int fpga_mgr_suspend(struct device *dev)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	if (!mgr)
		return -ENODEV;

	if (mgr->mops->suspend)
		return mgr->mops->suspend(mgr);

	return 0;
}

static int fpga_mgr_resume(struct device *dev)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);
	int ret = 0;

	if (!mgr)
		return -ENODEV;

	if (mgr->mops->resume) {
		ret = mgr->mops->resume(mgr);
		if (ret)
			return ret;
	}

	if (strlen(mgr->image_name) != 0)
		fpga_mgr_firmware_write(mgr, mgr->image_name);

	return 0;
}

static const struct dev_pm_ops fpga_mgr_dev_pm_ops = {
	.suspend	= fpga_mgr_suspend,
	.resume		= fpga_mgr_resume,
};

static void fpga_mgr_dev_release(struct device *dev)
{
	struct fpga_manager *mgr = to_fpga_manager(dev);

	dev_dbg(dev, "releasing '%s'\n", mgr->name);

	if (mgr->mops->fpga_remove)
		mgr->mops->fpga_remove(mgr);

	mgr->mops = NULL;

	mutex_lock(&fpga_mgr_mutex);
	list_del(&mgr->list);
	mutex_unlock(&fpga_mgr_mutex);

	ida_simple_remove(&fpga_mgr_ida, mgr->dev.id);
	kfree(mgr->image_name);
	kfree(mgr);
}

/**
 * fpga_mgr_register - register a low level fpga manager driver
 * @dev: fpga manager device
 * @name: fpga manager name
 * @mops: pointer to structure of fpga manager ops
 * @priv: fpga manager private data
 *
 * Return: 0 on success, error code otherwise.
 */
int fpga_mgr_register(struct device *dev, const char *name,
		      const struct fpga_manager_ops *mops,
		      void *priv)
{
	struct fpga_manager *mgr;
	int id, ret;

	if (!mops || !name || !strlen(name))
		return -EINVAL;

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return -ENOMEM;

	id = ida_simple_get(&fpga_mgr_ida, 0, 0, GFP_KERNEL);
	if (id < 0)
		return id;

	mutex_init(&mgr->lock);

	mgr->name = name;
	mgr->mops = mops;
	mgr->priv = priv;

	/*
	 * Initialize framework state by requesting low level driver read state
	 * from device.  FPGA may be in reset mode or may have been programmed
	 * by bootloader or EEPROM.
	 */
	mgr->state = fpga_mgr_low_level_state(mgr);

	INIT_LIST_HEAD(&mgr->list);
	mutex_lock(&fpga_mgr_mutex);
	list_add(&mgr->list, &fpga_manager_list);
	mutex_unlock(&fpga_mgr_mutex);

	device_initialize(&mgr->dev);
	mgr->dev.class = fpga_mgr_class;
	mgr->dev.parent = dev;
	mgr->dev.of_node = dev->of_node;
	mgr->dev.release = fpga_mgr_dev_release;
	mgr->dev.id = id;
	dev_set_name(&mgr->dev, "%d", id);
	ret = device_add(&mgr->dev);
	if (ret)
		goto error_device;

	dev_info(&mgr->dev, "%s registered\n", mgr->name);

	return 0;

error_device:
	ida_simple_remove(&fpga_mgr_ida, id);
	kfree(mgr);

	return ret;
}
EXPORT_SYMBOL_GPL(fpga_mgr_register);

/**
 * fpga_mgr_remove - remove a low level fpga manager driver
 * @pdev: fpga manager device
 */
void fpga_mgr_remove(struct platform_device *pdev)
{
	struct list_head *tmp;
	struct fpga_manager *mgr = NULL;

	list_for_each(tmp, &fpga_manager_list) {
		mgr = list_entry(tmp, struct fpga_manager, list);
		if (mgr->dev.parent == &pdev->dev) {
			device_unregister(&mgr->dev);
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(fpga_mgr_remove);

static int __init fpga_mgr_dev_init(void)
{
	pr_info("FPGA Manager framework driver\n");

	fpga_mgr_class = class_create(THIS_MODULE, "fpga_manager");
	if (IS_ERR(fpga_mgr_class))
		return PTR_ERR(fpga_mgr_class);

	if (IS_ENABLED(CONFIG_FPGA_MGR_SYSFS))
		fpga_mgr_class->dev_groups = fpga_mgr_groups;

	fpga_mgr_class->pm = &fpga_mgr_dev_pm_ops;

	return 0;
}

static void __exit fpga_mgr_dev_exit(void)
{
	class_destroy(fpga_mgr_class);
	ida_destroy(&fpga_mgr_ida);
}

MODULE_AUTHOR("Alan Tull <atull@opensource.altera.com>");
MODULE_DESCRIPTION("FPGA Manager framework driver");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_mgr_dev_init);
module_exit(fpga_mgr_dev_exit);
