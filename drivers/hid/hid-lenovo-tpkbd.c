/*
 *  HID driver for Lenovo ThinkPad USB Keyboard with TrackPoint
 *
 *  Copyright (c) 2012 Bernhard Seibold
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/leds.h>

#include "hid-ids.h"

/* This is only used for the trackpoint part of the driver, hence _tp */
struct tpkbd_data_pointer {
	int led_state;
	struct led_classdev led_mute;
	struct led_classdev led_micmute;
	int press_to_select;
	int dragging;
	int release_to_select;
	int select_right;
	int sensitivity;
	int press_speed;
};

#define map_key_clear(c) hid_map_usage_clear(hi, usage, bit, max, EV_KEY, (c))

static int tpkbd_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	if (usage->hid == (HID_UP_BUTTON | 0x0010)) {
		/* mark the device as pointer */
		hid_set_drvdata(hdev, (void *)1);
		map_key_clear(KEY_MICMUTE);
		return 1;
	}
	return 0;
}

#undef map_key_clear

static int tpkbd_features_set(struct hid_device *hdev)
{
	struct hid_report *report;
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	report = hdev->report_enum[HID_FEATURE_REPORT].report_id_hash[4];

	report->field[0]->value[0]  = data_pointer->press_to_select   ? 0x01 : 0x02;
	report->field[0]->value[0] |= data_pointer->dragging          ? 0x04 : 0x08;
	report->field[0]->value[0] |= data_pointer->release_to_select ? 0x10 : 0x20;
	report->field[0]->value[0] |= data_pointer->select_right      ? 0x80 : 0x40;
	report->field[1]->value[0] = 0x03; // unknown setting, imitate windows driver
	report->field[2]->value[0] = data_pointer->sensitivity;
	report->field[3]->value[0] = data_pointer->press_speed;

	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
	return 0;
}

static ssize_t pointer_press_to_select_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->press_to_select);
}

static ssize_t pointer_press_to_select_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->press_to_select = value;
	tpkbd_features_set(hdev);

	return count;
}

static ssize_t pointer_dragging_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->dragging);
}

static ssize_t pointer_dragging_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->dragging = value;
	tpkbd_features_set(hdev);

	return count;
}

static ssize_t pointer_release_to_select_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->release_to_select);
}

static ssize_t pointer_release_to_select_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->release_to_select = value;
	tpkbd_features_set(hdev);

	return count;
}

static ssize_t pointer_select_right_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->select_right);
}

static ssize_t pointer_select_right_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->select_right = value;
	tpkbd_features_set(hdev);

	return count;
}

static ssize_t pointer_sensitivity_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		data_pointer->sensitivity);
}

static ssize_t pointer_sensitivity_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value) || value < 1 || value > 255)
		return -EINVAL;

	data_pointer->sensitivity = value;
	tpkbd_features_set(hdev);

	return count;
}

static ssize_t pointer_press_speed_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		data_pointer->press_speed);
}

static ssize_t pointer_press_speed_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value) || value < 1 || value > 255)
		return -EINVAL;

	data_pointer->press_speed = value;
	tpkbd_features_set(hdev);

	return count;
}

static struct device_attribute dev_attr_pointer_press_to_select =
	__ATTR(press_to_select, S_IWUSR | S_IRUGO,
			pointer_press_to_select_show,
			pointer_press_to_select_store);

static struct device_attribute dev_attr_pointer_dragging =
	__ATTR(dragging, S_IWUSR | S_IRUGO,
			pointer_dragging_show,
			pointer_dragging_store);

static struct device_attribute dev_attr_pointer_release_to_select =
	__ATTR(release_to_select, S_IWUSR | S_IRUGO,
			pointer_release_to_select_show,
			pointer_release_to_select_store);

static struct device_attribute dev_attr_pointer_select_right =
	__ATTR(select_right, S_IWUSR | S_IRUGO,
			pointer_select_right_show,
			pointer_select_right_store);

static struct device_attribute dev_attr_pointer_sensitivity =
	__ATTR(sensitivity, S_IWUSR | S_IRUGO,
			pointer_sensitivity_show,
			pointer_sensitivity_store);

static struct device_attribute dev_attr_pointer_press_speed =
	__ATTR(press_speed, S_IWUSR | S_IRUGO,
			pointer_press_speed_show,
			pointer_press_speed_store);

static struct attribute *tpkbd_attributes_pointer[] = {
	&dev_attr_pointer_press_to_select.attr,
	&dev_attr_pointer_dragging.attr,
	&dev_attr_pointer_release_to_select.attr,
	&dev_attr_pointer_select_right.attr,
	&dev_attr_pointer_sensitivity.attr,
	&dev_attr_pointer_press_speed.attr,
	NULL
};

static const struct attribute_group tpkbd_attr_group_pointer = {
	.attrs = tpkbd_attributes_pointer,
};

static enum led_brightness tpkbd_led_brightness_get(
			struct led_classdev *led_cdev)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int led_nr = 0;

	if (led_cdev == &data_pointer->led_micmute)
		led_nr = 1;

	return data_pointer->led_state & (1 << led_nr)
				? LED_FULL
				: LED_OFF;
}

static void tpkbd_led_brightness_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	struct hid_report *report;
	int led_nr = 0;

	if (led_cdev == &data_pointer->led_micmute)
		led_nr = 1;

	if (value == LED_OFF)
		data_pointer->led_state &= ~(1 << led_nr);
	else
		data_pointer->led_state |= 1 << led_nr;

	report = hdev->report_enum[HID_OUTPUT_REPORT].report_id_hash[3];
	report->field[0]->value[0] = (data_pointer->led_state >> 0) & 1;
	report->field[0]->value[1] = (data_pointer->led_state >> 1) & 1;
	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
}

static int tpkbd_probe_tp(struct hid_device *hdev)
{
	struct device *dev = &hdev->dev;
	struct tpkbd_data_pointer *data_pointer;
	size_t name_sz = strlen(dev_name(dev)) + 16;
	char *name_mute, *name_micmute;
	int i;

	/* Validate required reports. */
	for (i = 0; i < 4; i++) {
		if (!hid_validate_values(hdev, HID_FEATURE_REPORT, 4, i, 1))
			return -ENODEV;
	}
	if (!hid_validate_values(hdev, HID_OUTPUT_REPORT, 3, 0, 2))
		return -ENODEV;

	if (sysfs_create_group(&hdev->dev.kobj,
				&tpkbd_attr_group_pointer)) {
		hid_warn(hdev, "Could not create sysfs group\n");
	}

	data_pointer = devm_kzalloc(&hdev->dev,
				    sizeof(struct tpkbd_data_pointer),
				    GFP_KERNEL);
	if (data_pointer == NULL) {
		hid_err(hdev, "Could not allocate memory for driver data\n");
		return -ENOMEM;
	}

	// set same default values as windows driver
	data_pointer->sensitivity = 0xa0;
	data_pointer->press_speed = 0x38;

	name_mute = devm_kzalloc(&hdev->dev, name_sz, GFP_KERNEL);
	name_micmute = devm_kzalloc(&hdev->dev, name_sz, GFP_KERNEL);
	if (name_mute == NULL || name_micmute == NULL) {
		hid_err(hdev, "Could not allocate memory for led data\n");
		return -ENOMEM;
	}
	snprintf(name_mute, name_sz, "%s:amber:mute", dev_name(dev));
	snprintf(name_micmute, name_sz, "%s:amber:micmute", dev_name(dev));

	hid_set_drvdata(hdev, data_pointer);

	data_pointer->led_mute.name = name_mute;
	data_pointer->led_mute.brightness_get = tpkbd_led_brightness_get;
	data_pointer->led_mute.brightness_set = tpkbd_led_brightness_set;
	data_pointer->led_mute.dev = dev;
	led_classdev_register(dev, &data_pointer->led_mute);

	data_pointer->led_micmute.name = name_micmute;
	data_pointer->led_micmute.brightness_get = tpkbd_led_brightness_get;
	data_pointer->led_micmute.brightness_set = tpkbd_led_brightness_set;
	data_pointer->led_micmute.dev = dev;
	led_classdev_register(dev, &data_pointer->led_micmute);

	tpkbd_features_set(hdev);

	return 0;
}

static int tpkbd_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "hid_parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hid_hw_start failed\n");
		goto err;
	}

	if (hid_get_drvdata(hdev)) {
		hid_set_drvdata(hdev, NULL);
		ret = tpkbd_probe_tp(hdev);
		if (ret)
			goto err_hid;
	}

	return 0;
err_hid:
	hid_hw_stop(hdev);
err:
	return ret;
}

static void tpkbd_remove_tp(struct hid_device *hdev)
{
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	sysfs_remove_group(&hdev->dev.kobj,
			&tpkbd_attr_group_pointer);

	led_classdev_unregister(&data_pointer->led_micmute);
	led_classdev_unregister(&data_pointer->led_mute);

	hid_set_drvdata(hdev, NULL);
}

static void tpkbd_remove(struct hid_device *hdev)
{
	if (hid_get_drvdata(hdev))
		tpkbd_remove_tp(hdev);

	hid_hw_stop(hdev);
}

static const struct hid_device_id tpkbd_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_TPKBD) },
	{ }
};

MODULE_DEVICE_TABLE(hid, tpkbd_devices);

static struct hid_driver tpkbd_driver = {
	.name = "lenovo_tpkbd",
	.id_table = tpkbd_devices,
	.input_mapping = tpkbd_input_mapping,
	.probe = tpkbd_probe,
	.remove = tpkbd_remove,
};
module_hid_driver(tpkbd_driver);

MODULE_LICENSE("GPL");
