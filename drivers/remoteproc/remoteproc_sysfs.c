// SPDX-License-Identifier: GPL-2.0-only
/*
 * Remote Processor Framework
 */

#include <linux/remoteproc.h>

#include "remoteproc_internal.h"

#define to_rproc(d) container_of(d, struct rproc, dev)

/* Expose the loaded / running firmware name via sysfs */
static ssize_t firmware_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct rproc *rproc = to_rproc(dev);

	return sprintf(buf, "%s\n", rproc->firmware);
}

/* Change firmware name via sysfs */
static ssize_t firmware_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rproc *rproc = to_rproc(dev);
	char *p;
	int err, len = count;

	err = mutex_lock_interruptible(&rproc->lock);
	if (err) {
		dev_err(dev, "can't lock rproc %s: %d\n", rproc->name, err);
		return -EINVAL;
	}

	if (rproc->state != RPROC_OFFLINE) {
		dev_err(dev, "can't change firmware while running\n");
		err = -EBUSY;
		goto out;
	}

	len = strcspn(buf, "\n");
	if (!len) {
		dev_err(dev, "can't provide a NULL firmware\n");
		err = -EINVAL;
		goto out;
	}

	p = kstrndup(buf, len, GFP_KERNEL);
	if (!p) {
		err = -ENOMEM;
		goto out;
	}

	kfree(rproc->firmware);
	rproc->firmware = p;
out:
	mutex_unlock(&rproc->lock);

	return err ? err : count;
}
static DEVICE_ATTR_RW(firmware);

/*
 * A state-to-string lookup table, for exposing a human readable state
 * via sysfs. Always keep in sync with enum rproc_state
 */
static const char * const rproc_state_string[] = {
	[RPROC_OFFLINE]		= "offline",
	[RPROC_SUSPENDED]	= "suspended",
	[RPROC_RUNNING]		= "running",
	[RPROC_CRASHED]		= "crashed",
	[RPROC_DELETED]		= "deleted",
	[RPROC_LAST]		= "invalid",
};

/* Expose the state of the remote processor via sysfs */
static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct rproc *rproc = to_rproc(dev);
	unsigned int state;

	state = rproc->state > RPROC_LAST ? RPROC_LAST : rproc->state;
	return sprintf(buf, "%s\n", rproc_state_string[state]);
}

/* Change remote processor state via sysfs */
static ssize_t state_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct rproc *rproc = to_rproc(dev);
	int ret = 0;

	if (sysfs_streq(buf, "start")) {
		if (rproc->state == RPROC_RUNNING)
			return -EBUSY;

		ret = rproc_boot(rproc);
		if (ret)
			dev_err(&rproc->dev, "Boot failed: %d\n", ret);
	} else if (sysfs_streq(buf, "stop")) {
		if (rproc->state != RPROC_RUNNING)
			return -EINVAL;

		rproc_shutdown(rproc);
	} else {
		dev_err(&rproc->dev, "Unrecognised option: %s\n", buf);
		ret = -EINVAL;
	}
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(state);

/**
 * kick_store() - Kick remote from sysfs.
 * @dev: remoteproc device
 * @attr: sysfs device attribute
 * @buf: sysfs buffer
 * @count: size of the contents in buf
 *
 * It will just raise a signal, no content is expected for now.
 *
 * Return: the input count if it allows kick from sysfs,
 * as it is always expected to succeed.
 */
static ssize_t kick_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct rproc *rproc = to_rproc(dev);

	(void)attr;
	(void)buf;

	if (rproc->ops->kick)
		rproc->ops->kick(rproc, 0);
	else
		count = -EINVAL;
	return count;
}
static DEVICE_ATTR_WO(kick);

/**
 * remote_kick_show() - Check if remote has kicked
 * @dev: remoteproc device
 * @attr: sysfs device attribute
 * @buf: sysfs buffer
 *
 * It will check if the remote has kicked.
 *
 * Return: always 2, and the value in the sysfs buffer
 * shows if the remote has kicked. '0' - not kicked, '1' - kicked.
 */
static ssize_t remote_kick_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rproc *rproc = to_rproc(dev);

	buf[0] = '0';
	buf[1] = '\n';
	if (rproc_peek_remote_kick(rproc))
		buf[0] = '1';
	return 2;
}

/**
 * remote_kick_store() - Ack the kick from remote
 * @dev: remoteproc device
 * @attr: sysfs device attribute
 * @buf: sysfs buffer
 * @count: size of the contents in buf
 *
 * It will ack the remote, no response contents is expected.
 *
 * Return: the input count if it allows kick from sysfs,
 * as it is always expected to succeed.
 */
static ssize_t remote_kick_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct rproc *rproc = to_rproc(dev);

	rproc_ack_remote_kick(rproc);
	return count;
}
static DEVICE_ATTR_RW(remote_kick);

/* Expose the name of the remote processor via sysfs */
static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct rproc *rproc = to_rproc(dev);

	return sprintf(buf, "%s\n", rproc->name);
}
static DEVICE_ATTR_RO(name);

static struct attribute *rproc_attrs[] = {
	&dev_attr_firmware.attr,
	&dev_attr_state.attr,
	&dev_attr_name.attr,
	NULL
};

static const struct attribute_group rproc_devgroup = {
	.attrs = rproc_attrs
};

static const struct attribute_group *rproc_devgroups[] = {
	&rproc_devgroup,
	NULL
};

struct class rproc_class = {
	.name		= "remoteproc",
	.dev_groups	= rproc_devgroups,
};

/**
 * rproc_create_kick_sysfs() - create kick remote sysfs entry
 * @rproc: remoteproc
 *
 * It will create kick remote sysfs entry if kick remote
 * from sysfs is allowed.
 *
 * Return: 0 for success, and negative value for failure.
 */
int rproc_create_kick_sysfs(struct rproc *rproc)
{
	struct device *dev = &rproc->dev;
	int ret;

	if (!rproc_allow_sysfs_kick(rproc))
		return -EINVAL;
	ret = sysfs_create_file(&dev->kobj, &dev_attr_kick.attr);
	if (ret) {
		dev_err(dev, "failed to create sysfs for kick.\n");
		return ret;
	}
	ret = sysfs_create_file(&dev->kobj, &dev_attr_remote_kick.attr);
	if (ret)
		dev_err(dev, "failed to create sysfs for remote kick.\n");
	return ret;
}
EXPORT_SYMBOL(rproc_create_kick_sysfs);

int __init rproc_init_sysfs(void)
{
	/* create remoteproc device class for sysfs */
	int err = class_register(&rproc_class);

	if (err)
		pr_err("remoteproc: unable to register class\n");
	return err;
}

void __exit rproc_exit_sysfs(void)
{
	class_unregister(&rproc_class);
}
