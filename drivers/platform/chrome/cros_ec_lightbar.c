/*
 * cros_ec_lightbar - expose the Chromebook Pixel lightbar to userspace
 *
 * Copyright (C) 2014 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "cros_ec_lightbar: " fmt

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "cros_ec_dev.h"

/* Rate-limit the lightbar interface to prevent DoS. */
static unsigned long lb_interval_jiffies = 50 * HZ / 1000;

static ssize_t interval_msec_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	unsigned long msec = lb_interval_jiffies * 1000 / HZ;

	return scnprintf(buf, PAGE_SIZE, "%lu\n", msec);
}

static ssize_t interval_msec_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned long msec;

	if (kstrtoul(buf, 0, &msec))
		return -EINVAL;

	lb_interval_jiffies = msec * HZ / 1000;

	return count;
}

static DEFINE_MUTEX(lb_mutex);
/* Return 0 if able to throttle correctly, error otherwise */
static int lb_throttle(void)
{
	static unsigned long last_access;
	unsigned long now, next_timeslot;
	long delay;
	int ret = 0;

	mutex_lock(&lb_mutex);

	now = jiffies;
	next_timeslot = last_access + lb_interval_jiffies;

	if (time_before(now, next_timeslot)) {
		delay = (long)(next_timeslot) - (long)now;
		set_current_state(TASK_INTERRUPTIBLE);
		if (schedule_timeout(delay) > 0) {
			/* interrupted - just abort */
			ret = -EINTR;
			goto out;
		}
		now = jiffies;
	}

	last_access = now;
out:
	mutex_unlock(&lb_mutex);

	return ret;
}

static struct cros_ec_command *alloc_lightbar_cmd_msg(struct cros_ec_dev *ec)
{
	struct cros_ec_command *msg;
	int len;

	len = max(sizeof(struct ec_params_lightbar),
		  sizeof(struct ec_response_lightbar));

	msg = kmalloc(sizeof(*msg) + len, GFP_KERNEL);
	if (!msg)
		return NULL;

	msg->version = 0;
	msg->command = EC_CMD_LIGHTBAR_CMD + ec->cmd_offset;
	msg->outsize = sizeof(struct ec_params_lightbar);
	msg->insize = sizeof(struct ec_response_lightbar);

	return msg;
}

static int get_lightbar_version(struct cros_ec_dev *ec,
				uint32_t *ver_ptr, uint32_t *flg_ptr)
{
	struct ec_params_lightbar *param;
	struct ec_response_lightbar *resp;
	struct cros_ec_command *msg;
	int ret;

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return 0;

	param = (struct ec_params_lightbar *)msg->data;
	param->cmd = LIGHTBAR_CMD_VERSION;
	ret = cros_ec_cmd_xfer(ec->ec_dev, msg);
	if (ret < 0) {
		ret = 0;
		goto exit;
	}

	switch (msg->result) {
	case EC_RES_INVALID_PARAM:
		/* Pixel had no version command. */
		if (ver_ptr)
			*ver_ptr = 0;
		if (flg_ptr)
			*flg_ptr = 0;
		ret = 1;
		goto exit;

	case EC_RES_SUCCESS:
		resp = (struct ec_response_lightbar *)msg->data;

		/* Future devices w/lightbars should implement this command */
		if (ver_ptr)
			*ver_ptr = resp->version.num;
		if (flg_ptr)
			*flg_ptr = resp->version.flags;
		ret = 1;
		goto exit;
	}

	/* Anything else (ie, EC_RES_INVALID_COMMAND) - no lightbar */
	ret = 0;
exit:
	kfree(msg);
	return ret;
}

static ssize_t version_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	uint32_t version = 0, flags = 0;
	struct cros_ec_dev *ec = container_of(dev,
					      struct cros_ec_dev, class_dev);
	int ret;

	ret = lb_throttle();
	if (ret)
		return ret;

	/* This should always succeed, because we check during init. */
	if (!get_lightbar_version(ec, &version, &flags))
		return -EIO;

	return scnprintf(buf, PAGE_SIZE, "%d %d\n", version, flags);
}

static ssize_t brightness_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct ec_params_lightbar *param;
	struct cros_ec_command *msg;
	int ret;
	unsigned int val;
	struct cros_ec_dev *ec = container_of(dev,
					      struct cros_ec_dev, class_dev);

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return -ENOMEM;

	param = (struct ec_params_lightbar *)msg->data;
	param->cmd = LIGHTBAR_CMD_SET_BRIGHTNESS;
	param->set_brightness.num = val;
	ret = lb_throttle();
	if (ret)
		goto exit;

	ret = cros_ec_cmd_xfer(ec->ec_dev, msg);
	if (ret < 0)
		goto exit;

	if (msg->result != EC_RES_SUCCESS) {
		ret = -EINVAL;
		goto exit;
	}

	ret = count;
exit:
	kfree(msg);
	return ret;
}


/*
 * We expect numbers, and we'll keep reading until we find them, skipping over
 * any whitespace (sysfs guarantees that the input is null-terminated). Every
 * four numbers are sent to the lightbar as <LED,R,G,B>. We fail at the first
 * parsing error, if we don't parse any numbers, or if we have numbers left
 * over.
 */
static ssize_t led_rgb_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ec_params_lightbar *param;
	struct cros_ec_command *msg;
	struct cros_ec_dev *ec = container_of(dev,
					      struct cros_ec_dev, class_dev);
	unsigned int val[4];
	int ret, i = 0, j = 0, ok = 0;

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return -ENOMEM;

	do {
		/* Skip any whitespace */
		while (*buf && isspace(*buf))
			buf++;

		if (!*buf)
			break;

		ret = sscanf(buf, "%i", &val[i++]);
		if (ret == 0)
			goto exit;

		if (i == 4) {
			param = (struct ec_params_lightbar *)msg->data;
			param->cmd = LIGHTBAR_CMD_SET_RGB;
			param->set_rgb.led = val[0];
			param->set_rgb.red = val[1];
			param->set_rgb.green = val[2];
			param->set_rgb.blue = val[3];
			/*
			 * Throttle only the first of every four transactions,
			 * so that the user can update all four LEDs at once.
			 */
			if ((j++ % 4) == 0) {
				ret = lb_throttle();
				if (ret)
					goto exit;
			}

			ret = cros_ec_cmd_xfer(ec->ec_dev, msg);
			if (ret < 0)
				goto exit;

			if (msg->result != EC_RES_SUCCESS)
				goto exit;

			i = 0;
			ok = 1;
		}

		/* Skip over the number we just read */
		while (*buf && !isspace(*buf))
			buf++;

	} while (*buf);

exit:
	kfree(msg);
	return (ok && i == 0) ? count : -EINVAL;
}

static char const *seqname[] = {
	"ERROR", "S5", "S3", "S0", "S5S3", "S3S0",
	"S0S3", "S3S5", "STOP", "RUN", "PULSE", "TEST", "KONAMI",
};

static ssize_t sequence_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct ec_params_lightbar *param;
	struct ec_response_lightbar *resp;
	struct cros_ec_command *msg;
	int ret;
	struct cros_ec_dev *ec = container_of(dev,
					      struct cros_ec_dev, class_dev);

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return -ENOMEM;

	param = (struct ec_params_lightbar *)msg->data;
	param->cmd = LIGHTBAR_CMD_GET_SEQ;
	ret = lb_throttle();
	if (ret)
		goto exit;

	ret = cros_ec_cmd_xfer(ec->ec_dev, msg);
	if (ret < 0)
		goto exit;

	if (msg->result != EC_RES_SUCCESS) {
		ret = scnprintf(buf, PAGE_SIZE,
				"ERROR: EC returned %d\n", msg->result);
		goto exit;
	}

	resp = (struct ec_response_lightbar *)msg->data;
	if (resp->get_seq.num >= ARRAY_SIZE(seqname))
		ret = scnprintf(buf, PAGE_SIZE, "%d\n", resp->get_seq.num);
	else
		ret = scnprintf(buf, PAGE_SIZE, "%s\n",
				seqname[resp->get_seq.num]);

exit:
	kfree(msg);
	return ret;
}

static ssize_t sequence_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct ec_params_lightbar *param;
	struct cros_ec_command *msg;
	unsigned int num;
	int ret, len;
	struct cros_ec_dev *ec = container_of(dev,
					      struct cros_ec_dev, class_dev);

	for (len = 0; len < count; len++)
		if (!isalnum(buf[len]))
			break;

	for (num = 0; num < ARRAY_SIZE(seqname); num++)
		if (!strncasecmp(seqname[num], buf, len))
			break;

	if (num >= ARRAY_SIZE(seqname)) {
		ret = kstrtouint(buf, 0, &num);
		if (ret)
			return ret;
	}

	msg = alloc_lightbar_cmd_msg(ec);
	if (!msg)
		return -ENOMEM;

	param = (struct ec_params_lightbar *)msg->data;
	param->cmd = LIGHTBAR_CMD_SEQ;
	param->seq.num = num;
	ret = lb_throttle();
	if (ret)
		goto exit;

	ret = cros_ec_cmd_xfer(ec->ec_dev, msg);
	if (ret < 0)
		goto exit;

	if (msg->result != EC_RES_SUCCESS) {
		ret = -EINVAL;
		goto exit;
	}

	ret = count;
exit:
	kfree(msg);
	return ret;
}

/* Module initialization */

static DEVICE_ATTR_RW(interval_msec);
static DEVICE_ATTR_RO(version);
static DEVICE_ATTR_WO(brightness);
static DEVICE_ATTR_WO(led_rgb);
static DEVICE_ATTR_RW(sequence);
static struct attribute *__lb_cmds_attrs[] = {
	&dev_attr_interval_msec.attr,
	&dev_attr_version.attr,
	&dev_attr_brightness.attr,
	&dev_attr_led_rgb.attr,
	&dev_attr_sequence.attr,
	NULL,
};

static umode_t cros_ec_lightbar_attrs_are_visible(struct kobject *kobj,
						  struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct cros_ec_dev *ec = container_of(dev,
					      struct cros_ec_dev, class_dev);
	struct platform_device *pdev = to_platform_device(ec->dev);
	struct cros_ec_platform *pdata = pdev->dev.platform_data;
	int is_cros_ec;

	is_cros_ec = strcmp(pdata->ec_name, CROS_EC_DEV_NAME);

	if (is_cros_ec != 0)
		return 0;

	/* Only instantiate this stuff if the EC has a lightbar */
	if (get_lightbar_version(ec, NULL, NULL))
		return a->mode;
	else
		return 0;
}

struct attribute_group cros_ec_lightbar_attr_group = {
	.name = "lightbar",
	.attrs = __lb_cmds_attrs,
	.is_visible = cros_ec_lightbar_attrs_are_visible,
};
