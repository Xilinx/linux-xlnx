/*
 *  toshiba_acpi.c - Toshiba Laptop ACPI Extras
 *
 *
 *  Copyright (C) 2002-2004 John Belmonte
 *  Copyright (C) 2008 Philip Langdale
 *  Copyright (C) 2010 Pierre Ducroquet
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 *  The devolpment page for this driver is located at
 *  http://memebeam.org/toys/ToshibaAcpiDriver.
 *
 *  Credits:
 *	Jonathan A. Buzzard - Toshiba HCI info, and critical tips on reverse
 *		engineering the Windows drivers
 *	Yasushi Nagato - changes for linux kernel 2.4 -> 2.5
 *	Rob Miller - TV out and hotkeys help
 *
 *
 *  TODO
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define TOSHIBA_ACPI_VERSION	"0.19"
#define PROC_INTERFACE_VERSION	1

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/backlight.h>
#include <linux/rfkill.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/i8042.h>

#include <asm/uaccess.h>

#include <acpi/acpi_drivers.h>

MODULE_AUTHOR("John Belmonte");
MODULE_DESCRIPTION("Toshiba Laptop ACPI Extras Driver");
MODULE_LICENSE("GPL");

#define TOSHIBA_WMI_EVENT_GUID "59142400-C6A3-40FA-BADB-8A2652834100"

/* Scan code for Fn key on TOS1900 models */
#define TOS1900_FN_SCAN		0x6e

/* Toshiba ACPI method paths */
#define METHOD_VIDEO_OUT	"\\_SB_.VALX.DSSX"

/* Toshiba HCI interface definitions
 *
 * HCI is Toshiba's "Hardware Control Interface" which is supposed to
 * be uniform across all their models.  Ideally we would just call
 * dedicated ACPI methods instead of using this primitive interface.
 * However the ACPI methods seem to be incomplete in some areas (for
 * example they allow setting, but not reading, the LCD brightness value),
 * so this is still useful.
 */

#define HCI_WORDS			6

/* operations */
#define HCI_SET				0xff00
#define HCI_GET				0xfe00

/* return codes */
#define HCI_SUCCESS			0x0000
#define HCI_FAILURE			0x1000
#define HCI_NOT_SUPPORTED		0x8000
#define HCI_EMPTY			0x8c00

/* registers */
#define HCI_FAN				0x0004
#define HCI_TR_BACKLIGHT		0x0005
#define HCI_SYSTEM_EVENT		0x0016
#define HCI_VIDEO_OUT			0x001c
#define HCI_HOTKEY_EVENT		0x001e
#define HCI_LCD_BRIGHTNESS		0x002a
#define HCI_WIRELESS			0x0056

/* field definitions */
#define HCI_HOTKEY_DISABLE		0x0b
#define HCI_HOTKEY_ENABLE		0x09
#define HCI_LCD_BRIGHTNESS_BITS		3
#define HCI_LCD_BRIGHTNESS_SHIFT	(16-HCI_LCD_BRIGHTNESS_BITS)
#define HCI_LCD_BRIGHTNESS_LEVELS	(1 << HCI_LCD_BRIGHTNESS_BITS)
#define HCI_VIDEO_OUT_LCD		0x1
#define HCI_VIDEO_OUT_CRT		0x2
#define HCI_VIDEO_OUT_TV		0x4
#define HCI_WIRELESS_KILL_SWITCH	0x01
#define HCI_WIRELESS_BT_PRESENT		0x0f
#define HCI_WIRELESS_BT_ATTACH		0x40
#define HCI_WIRELESS_BT_POWER		0x80

struct toshiba_acpi_dev {
	struct acpi_device *acpi_dev;
	const char *method_hci;
	struct rfkill *bt_rfk;
	struct input_dev *hotkey_dev;
	struct work_struct hotkey_work;
	struct backlight_device *backlight_dev;
	struct led_classdev led_dev;

	int force_fan;
	int last_key_event;
	int key_event_valid;

	unsigned int illumination_supported:1;
	unsigned int video_supported:1;
	unsigned int fan_supported:1;
	unsigned int system_event_supported:1;
	unsigned int ntfy_supported:1;
	unsigned int info_supported:1;
	unsigned int tr_backlight_supported:1;

	struct mutex mutex;
};

static struct toshiba_acpi_dev *toshiba_acpi;

static const struct acpi_device_id toshiba_device_ids[] = {
	{"TOS6200", 0},
	{"TOS6208", 0},
	{"TOS1900", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, toshiba_device_ids);

static const struct key_entry toshiba_acpi_keymap[] = {
	{ KE_KEY, 0x101, { KEY_MUTE } },
	{ KE_KEY, 0x102, { KEY_ZOOMOUT } },
	{ KE_KEY, 0x103, { KEY_ZOOMIN } },
	{ KE_KEY, 0x12c, { KEY_KBDILLUMTOGGLE } },
	{ KE_KEY, 0x139, { KEY_ZOOMRESET } },
	{ KE_KEY, 0x13b, { KEY_COFFEE } },
	{ KE_KEY, 0x13c, { KEY_BATTERY } },
	{ KE_KEY, 0x13d, { KEY_SLEEP } },
	{ KE_KEY, 0x13e, { KEY_SUSPEND } },
	{ KE_KEY, 0x13f, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x140, { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY, 0x141, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0x142, { KEY_WLAN } },
	{ KE_KEY, 0x143, { KEY_TOUCHPAD_TOGGLE } },
	{ KE_KEY, 0x17f, { KEY_FN } },
	{ KE_KEY, 0xb05, { KEY_PROG2 } },
	{ KE_KEY, 0xb06, { KEY_WWW } },
	{ KE_KEY, 0xb07, { KEY_MAIL } },
	{ KE_KEY, 0xb30, { KEY_STOP } },
	{ KE_KEY, 0xb31, { KEY_PREVIOUSSONG } },
	{ KE_KEY, 0xb32, { KEY_NEXTSONG } },
	{ KE_KEY, 0xb33, { KEY_PLAYPAUSE } },
	{ KE_KEY, 0xb5a, { KEY_MEDIA } },
	{ KE_IGNORE, 0x1430, { KEY_RESERVED } },
	{ KE_END, 0 },
};

/* utility
 */

static __inline__ void _set_bit(u32 * word, u32 mask, int value)
{
	*word = (*word & ~mask) | (mask * value);
}

/* acpi interface wrappers
 */

static int write_acpi_int(const char *methodName, int val)
{
	acpi_status status;

	status = acpi_execute_simple_method(NULL, (char *)methodName, val);
	return (status == AE_OK) ? 0 : -EIO;
}

/* Perform a raw HCI call.  Here we don't care about input or output buffer
 * format.
 */
static acpi_status hci_raw(struct toshiba_acpi_dev *dev,
			   const u32 in[HCI_WORDS], u32 out[HCI_WORDS])
{
	struct acpi_object_list params;
	union acpi_object in_objs[HCI_WORDS];
	struct acpi_buffer results;
	union acpi_object out_objs[HCI_WORDS + 1];
	acpi_status status;
	int i;

	params.count = HCI_WORDS;
	params.pointer = in_objs;
	for (i = 0; i < HCI_WORDS; ++i) {
		in_objs[i].type = ACPI_TYPE_INTEGER;
		in_objs[i].integer.value = in[i];
	}

	results.length = sizeof(out_objs);
	results.pointer = out_objs;

	status = acpi_evaluate_object(dev->acpi_dev->handle,
				      (char *)dev->method_hci, &params,
				      &results);
	if ((status == AE_OK) && (out_objs->package.count <= HCI_WORDS)) {
		for (i = 0; i < out_objs->package.count; ++i) {
			out[i] = out_objs->package.elements[i].integer.value;
		}
	}

	return status;
}

/* common hci tasks (get or set one or two value)
 *
 * In addition to the ACPI status, the HCI system returns a result which
 * may be useful (such as "not supported").
 */

static acpi_status hci_write1(struct toshiba_acpi_dev *dev, u32 reg,
			      u32 in1, u32 *result)
{
	u32 in[HCI_WORDS] = { HCI_SET, reg, in1, 0, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status = hci_raw(dev, in, out);
	*result = (status == AE_OK) ? out[0] : HCI_FAILURE;
	return status;
}

static acpi_status hci_read1(struct toshiba_acpi_dev *dev, u32 reg,
			     u32 *out1, u32 *result)
{
	u32 in[HCI_WORDS] = { HCI_GET, reg, 0, 0, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status = hci_raw(dev, in, out);
	*out1 = out[2];
	*result = (status == AE_OK) ? out[0] : HCI_FAILURE;
	return status;
}

static acpi_status hci_write2(struct toshiba_acpi_dev *dev, u32 reg,
			      u32 in1, u32 in2, u32 *result)
{
	u32 in[HCI_WORDS] = { HCI_SET, reg, in1, in2, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status = hci_raw(dev, in, out);
	*result = (status == AE_OK) ? out[0] : HCI_FAILURE;
	return status;
}

static acpi_status hci_read2(struct toshiba_acpi_dev *dev, u32 reg,
			     u32 *out1, u32 *out2, u32 *result)
{
	u32 in[HCI_WORDS] = { HCI_GET, reg, *out1, *out2, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status = hci_raw(dev, in, out);
	*out1 = out[2];
	*out2 = out[3];
	*result = (status == AE_OK) ? out[0] : HCI_FAILURE;
	return status;
}

/* Illumination support */
static int toshiba_illumination_available(struct toshiba_acpi_dev *dev)
{
	u32 in[HCI_WORDS] = { 0, 0, 0, 0, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status;

	in[0] = 0xf100;
	status = hci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_info("Illumination device not available\n");
		return 0;
	}
	in[0] = 0xf400;
	status = hci_raw(dev, in, out);
	return 1;
}

static void toshiba_illumination_set(struct led_classdev *cdev,
				     enum led_brightness brightness)
{
	struct toshiba_acpi_dev *dev = container_of(cdev,
			struct toshiba_acpi_dev, led_dev);
	u32 in[HCI_WORDS] = { 0, 0, 0, 0, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status;

	/* First request : initialize communication. */
	in[0] = 0xf100;
	status = hci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_info("Illumination device not available\n");
		return;
	}

	if (brightness) {
		/* Switch the illumination on */
		in[0] = 0xf400;
		in[1] = 0x14e;
		in[2] = 1;
		status = hci_raw(dev, in, out);
		if (ACPI_FAILURE(status)) {
			pr_info("ACPI call for illumination failed\n");
			return;
		}
	} else {
		/* Switch the illumination off */
		in[0] = 0xf400;
		in[1] = 0x14e;
		in[2] = 0;
		status = hci_raw(dev, in, out);
		if (ACPI_FAILURE(status)) {
			pr_info("ACPI call for illumination failed.\n");
			return;
		}
	}

	/* Last request : close communication. */
	in[0] = 0xf200;
	in[1] = 0;
	in[2] = 0;
	hci_raw(dev, in, out);
}

static enum led_brightness toshiba_illumination_get(struct led_classdev *cdev)
{
	struct toshiba_acpi_dev *dev = container_of(cdev,
			struct toshiba_acpi_dev, led_dev);
	u32 in[HCI_WORDS] = { 0, 0, 0, 0, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status;
	enum led_brightness result;

	/* First request : initialize communication. */
	in[0] = 0xf100;
	status = hci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_info("Illumination device not available\n");
		return LED_OFF;
	}

	/* Check the illumination */
	in[0] = 0xf300;
	in[1] = 0x14e;
	status = hci_raw(dev, in, out);
	if (ACPI_FAILURE(status)) {
		pr_info("ACPI call for illumination failed.\n");
		return LED_OFF;
	}

	result = out[2] ? LED_FULL : LED_OFF;

	/* Last request : close communication. */
	in[0] = 0xf200;
	in[1] = 0;
	in[2] = 0;
	hci_raw(dev, in, out);

	return result;
}

/* Bluetooth rfkill handlers */

static u32 hci_get_bt_present(struct toshiba_acpi_dev *dev, bool *present)
{
	u32 hci_result;
	u32 value, value2;

	value = 0;
	value2 = 0;
	hci_read2(dev, HCI_WIRELESS, &value, &value2, &hci_result);
	if (hci_result == HCI_SUCCESS)
		*present = (value & HCI_WIRELESS_BT_PRESENT) ? true : false;

	return hci_result;
}

static u32 hci_get_radio_state(struct toshiba_acpi_dev *dev, bool *radio_state)
{
	u32 hci_result;
	u32 value, value2;

	value = 0;
	value2 = 0x0001;
	hci_read2(dev, HCI_WIRELESS, &value, &value2, &hci_result);

	*radio_state = value & HCI_WIRELESS_KILL_SWITCH;
	return hci_result;
}

static int bt_rfkill_set_block(void *data, bool blocked)
{
	struct toshiba_acpi_dev *dev = data;
	u32 result1, result2;
	u32 value;
	int err;
	bool radio_state;

	value = (blocked == false);

	mutex_lock(&dev->mutex);
	if (hci_get_radio_state(dev, &radio_state) != HCI_SUCCESS) {
		err = -EIO;
		goto out;
	}

	if (!radio_state) {
		err = 0;
		goto out;
	}

	hci_write2(dev, HCI_WIRELESS, value, HCI_WIRELESS_BT_POWER, &result1);
	hci_write2(dev, HCI_WIRELESS, value, HCI_WIRELESS_BT_ATTACH, &result2);

	if (result1 != HCI_SUCCESS || result2 != HCI_SUCCESS)
		err = -EIO;
	else
		err = 0;
 out:
	mutex_unlock(&dev->mutex);
	return err;
}

static void bt_rfkill_poll(struct rfkill *rfkill, void *data)
{
	bool new_rfk_state;
	bool value;
	u32 hci_result;
	struct toshiba_acpi_dev *dev = data;

	mutex_lock(&dev->mutex);

	hci_result = hci_get_radio_state(dev, &value);
	if (hci_result != HCI_SUCCESS) {
		/* Can't do anything useful */
		mutex_unlock(&dev->mutex);
		return;
	}

	new_rfk_state = value;

	mutex_unlock(&dev->mutex);

	if (rfkill_set_hw_state(rfkill, !new_rfk_state))
		bt_rfkill_set_block(data, true);
}

static const struct rfkill_ops toshiba_rfk_ops = {
	.set_block = bt_rfkill_set_block,
	.poll = bt_rfkill_poll,
};

static int get_tr_backlight_status(struct toshiba_acpi_dev *dev, bool *enabled)
{
	u32 hci_result;
	u32 status;

	hci_read1(dev, HCI_TR_BACKLIGHT, &status, &hci_result);
	*enabled = !status;
	return hci_result == HCI_SUCCESS ? 0 : -EIO;
}

static int set_tr_backlight_status(struct toshiba_acpi_dev *dev, bool enable)
{
	u32 hci_result;
	u32 value = !enable;

	hci_write1(dev, HCI_TR_BACKLIGHT, value, &hci_result);
	return hci_result == HCI_SUCCESS ? 0 : -EIO;
}

static struct proc_dir_entry *toshiba_proc_dir /*= 0*/ ;

static int __get_lcd_brightness(struct toshiba_acpi_dev *dev)
{
	u32 hci_result;
	u32 value;
	int brightness = 0;

	if (dev->tr_backlight_supported) {
		bool enabled;
		int ret = get_tr_backlight_status(dev, &enabled);
		if (ret)
			return ret;
		if (enabled)
			return 0;
		brightness++;
	}

	hci_read1(dev, HCI_LCD_BRIGHTNESS, &value, &hci_result);
	if (hci_result == HCI_SUCCESS)
		return brightness + (value >> HCI_LCD_BRIGHTNESS_SHIFT);

	return -EIO;
}

static int get_lcd_brightness(struct backlight_device *bd)
{
	struct toshiba_acpi_dev *dev = bl_get_data(bd);
	return __get_lcd_brightness(dev);
}

static int lcd_proc_show(struct seq_file *m, void *v)
{
	struct toshiba_acpi_dev *dev = m->private;
	int value;
	int levels;

	if (!dev->backlight_dev)
		return -ENODEV;

	levels = dev->backlight_dev->props.max_brightness + 1;
	value = get_lcd_brightness(dev->backlight_dev);
	if (value >= 0) {
		seq_printf(m, "brightness:              %d\n", value);
		seq_printf(m, "brightness_levels:       %d\n", levels);
		return 0;
	}

	pr_err("Error reading LCD brightness\n");
	return -EIO;
}

static int lcd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, lcd_proc_show, PDE_DATA(inode));
}

static int set_lcd_brightness(struct toshiba_acpi_dev *dev, int value)
{
	u32 hci_result;

	if (dev->tr_backlight_supported) {
		bool enable = !value;
		int ret = set_tr_backlight_status(dev, enable);
		if (ret)
			return ret;
		if (value)
			value--;
	}

	value = value << HCI_LCD_BRIGHTNESS_SHIFT;
	hci_write1(dev, HCI_LCD_BRIGHTNESS, value, &hci_result);
	return hci_result == HCI_SUCCESS ? 0 : -EIO;
}

static int set_lcd_status(struct backlight_device *bd)
{
	struct toshiba_acpi_dev *dev = bl_get_data(bd);
	return set_lcd_brightness(dev, bd->props.brightness);
}

static ssize_t lcd_proc_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	struct toshiba_acpi_dev *dev = PDE_DATA(file_inode(file));
	char cmd[42];
	size_t len;
	int value;
	int ret;
	int levels = dev->backlight_dev->props.max_brightness + 1;

	len = min(count, sizeof(cmd) - 1);
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	if (sscanf(cmd, " brightness : %i", &value) == 1 &&
	    value >= 0 && value < levels) {
		ret = set_lcd_brightness(dev, value);
		if (ret == 0)
			ret = count;
	} else {
		ret = -EINVAL;
	}
	return ret;
}

static const struct file_operations lcd_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= lcd_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= lcd_proc_write,
};

static int get_video_status(struct toshiba_acpi_dev *dev, u32 *status)
{
	u32 hci_result;

	hci_read1(dev, HCI_VIDEO_OUT, status, &hci_result);
	return hci_result == HCI_SUCCESS ? 0 : -EIO;
}

static int video_proc_show(struct seq_file *m, void *v)
{
	struct toshiba_acpi_dev *dev = m->private;
	u32 value;
	int ret;

	ret = get_video_status(dev, &value);
	if (!ret) {
		int is_lcd = (value & HCI_VIDEO_OUT_LCD) ? 1 : 0;
		int is_crt = (value & HCI_VIDEO_OUT_CRT) ? 1 : 0;
		int is_tv = (value & HCI_VIDEO_OUT_TV) ? 1 : 0;
		seq_printf(m, "lcd_out:                 %d\n", is_lcd);
		seq_printf(m, "crt_out:                 %d\n", is_crt);
		seq_printf(m, "tv_out:                  %d\n", is_tv);
	}

	return ret;
}

static int video_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, video_proc_show, PDE_DATA(inode));
}

static ssize_t video_proc_write(struct file *file, const char __user *buf,
				size_t count, loff_t *pos)
{
	struct toshiba_acpi_dev *dev = PDE_DATA(file_inode(file));
	char *cmd, *buffer;
	int ret;
	int value;
	int remain = count;
	int lcd_out = -1;
	int crt_out = -1;
	int tv_out = -1;
	u32 video_out;

	cmd = kmalloc(count + 1, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	if (copy_from_user(cmd, buf, count)) {
		kfree(cmd);
		return -EFAULT;
	}
	cmd[count] = '\0';

	buffer = cmd;

	/* scan expression.  Multiple expressions may be delimited with ;
	 *
	 *  NOTE: to keep scanning simple, invalid fields are ignored
	 */
	while (remain) {
		if (sscanf(buffer, " lcd_out : %i", &value) == 1)
			lcd_out = value & 1;
		else if (sscanf(buffer, " crt_out : %i", &value) == 1)
			crt_out = value & 1;
		else if (sscanf(buffer, " tv_out : %i", &value) == 1)
			tv_out = value & 1;
		/* advance to one character past the next ; */
		do {
			++buffer;
			--remain;
		}
		while (remain && *(buffer - 1) != ';');
	}

	kfree(cmd);

	ret = get_video_status(dev, &video_out);
	if (!ret) {
		unsigned int new_video_out = video_out;
		if (lcd_out != -1)
			_set_bit(&new_video_out, HCI_VIDEO_OUT_LCD, lcd_out);
		if (crt_out != -1)
			_set_bit(&new_video_out, HCI_VIDEO_OUT_CRT, crt_out);
		if (tv_out != -1)
			_set_bit(&new_video_out, HCI_VIDEO_OUT_TV, tv_out);
		/* To avoid unnecessary video disruption, only write the new
		 * video setting if something changed. */
		if (new_video_out != video_out)
			ret = write_acpi_int(METHOD_VIDEO_OUT, new_video_out);
	}

	return ret ? ret : count;
}

static const struct file_operations video_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= video_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= video_proc_write,
};

static int get_fan_status(struct toshiba_acpi_dev *dev, u32 *status)
{
	u32 hci_result;

	hci_read1(dev, HCI_FAN, status, &hci_result);
	return hci_result == HCI_SUCCESS ? 0 : -EIO;
}

static int fan_proc_show(struct seq_file *m, void *v)
{
	struct toshiba_acpi_dev *dev = m->private;
	int ret;
	u32 value;

	ret = get_fan_status(dev, &value);
	if (!ret) {
		seq_printf(m, "running:                 %d\n", (value > 0));
		seq_printf(m, "force_on:                %d\n", dev->force_fan);
	}

	return ret;
}

static int fan_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, fan_proc_show, PDE_DATA(inode));
}

static ssize_t fan_proc_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	struct toshiba_acpi_dev *dev = PDE_DATA(file_inode(file));
	char cmd[42];
	size_t len;
	int value;
	u32 hci_result;

	len = min(count, sizeof(cmd) - 1);
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	if (sscanf(cmd, " force_on : %i", &value) == 1 &&
	    value >= 0 && value <= 1) {
		hci_write1(dev, HCI_FAN, value, &hci_result);
		if (hci_result != HCI_SUCCESS)
			return -EIO;
		else
			dev->force_fan = value;
	} else {
		return -EINVAL;
	}

	return count;
}

static const struct file_operations fan_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= fan_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= fan_proc_write,
};

static int keys_proc_show(struct seq_file *m, void *v)
{
	struct toshiba_acpi_dev *dev = m->private;
	u32 hci_result;
	u32 value;

	if (!dev->key_event_valid && dev->system_event_supported) {
		hci_read1(dev, HCI_SYSTEM_EVENT, &value, &hci_result);
		if (hci_result == HCI_SUCCESS) {
			dev->key_event_valid = 1;
			dev->last_key_event = value;
		} else if (hci_result == HCI_EMPTY) {
			/* better luck next time */
		} else if (hci_result == HCI_NOT_SUPPORTED) {
			/* This is a workaround for an unresolved issue on
			 * some machines where system events sporadically
			 * become disabled. */
			hci_write1(dev, HCI_SYSTEM_EVENT, 1, &hci_result);
			pr_notice("Re-enabled hotkeys\n");
		} else {
			pr_err("Error reading hotkey status\n");
			return -EIO;
		}
	}

	seq_printf(m, "hotkey_ready:            %d\n", dev->key_event_valid);
	seq_printf(m, "hotkey:                  0x%04x\n", dev->last_key_event);
	return 0;
}

static int keys_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, keys_proc_show, PDE_DATA(inode));
}

static ssize_t keys_proc_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *pos)
{
	struct toshiba_acpi_dev *dev = PDE_DATA(file_inode(file));
	char cmd[42];
	size_t len;
	int value;

	len = min(count, sizeof(cmd) - 1);
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	if (sscanf(cmd, " hotkey_ready : %i", &value) == 1 && value == 0) {
		dev->key_event_valid = 0;
	} else {
		return -EINVAL;
	}

	return count;
}

static const struct file_operations keys_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= keys_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= keys_proc_write,
};

static int version_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "driver:                  %s\n", TOSHIBA_ACPI_VERSION);
	seq_printf(m, "proc_interface:          %d\n", PROC_INTERFACE_VERSION);
	return 0;
}

static int version_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, version_proc_show, PDE_DATA(inode));
}

static const struct file_operations version_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= version_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* proc and module init
 */

#define PROC_TOSHIBA		"toshiba"

static void create_toshiba_proc_entries(struct toshiba_acpi_dev *dev)
{
	if (dev->backlight_dev)
		proc_create_data("lcd", S_IRUGO | S_IWUSR, toshiba_proc_dir,
				 &lcd_proc_fops, dev);
	if (dev->video_supported)
		proc_create_data("video", S_IRUGO | S_IWUSR, toshiba_proc_dir,
				 &video_proc_fops, dev);
	if (dev->fan_supported)
		proc_create_data("fan", S_IRUGO | S_IWUSR, toshiba_proc_dir,
				 &fan_proc_fops, dev);
	if (dev->hotkey_dev)
		proc_create_data("keys", S_IRUGO | S_IWUSR, toshiba_proc_dir,
				 &keys_proc_fops, dev);
	proc_create_data("version", S_IRUGO, toshiba_proc_dir,
			 &version_proc_fops, dev);
}

static void remove_toshiba_proc_entries(struct toshiba_acpi_dev *dev)
{
	if (dev->backlight_dev)
		remove_proc_entry("lcd", toshiba_proc_dir);
	if (dev->video_supported)
		remove_proc_entry("video", toshiba_proc_dir);
	if (dev->fan_supported)
		remove_proc_entry("fan", toshiba_proc_dir);
	if (dev->hotkey_dev)
		remove_proc_entry("keys", toshiba_proc_dir);
	remove_proc_entry("version", toshiba_proc_dir);
}

static const struct backlight_ops toshiba_backlight_data = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = get_lcd_brightness,
	.update_status  = set_lcd_status,
};

static bool toshiba_acpi_i8042_filter(unsigned char data, unsigned char str,
				      struct serio *port)
{
	if (str & 0x20)
		return false;

	if (unlikely(data == 0xe0))
		return false;

	if ((data & 0x7f) == TOS1900_FN_SCAN) {
		schedule_work(&toshiba_acpi->hotkey_work);
		return true;
	}

	return false;
}

static void toshiba_acpi_hotkey_work(struct work_struct *work)
{
	acpi_handle ec_handle = ec_get_handle();
	acpi_status status;

	if (!ec_handle)
		return;

	status = acpi_evaluate_object(ec_handle, "NTFY", NULL, NULL);
	if (ACPI_FAILURE(status))
		pr_err("ACPI NTFY method execution failed\n");
}

/*
 * Returns hotkey scancode, or < 0 on failure.
 */
static int toshiba_acpi_query_hotkey(struct toshiba_acpi_dev *dev)
{
	unsigned long long value;
	acpi_status status;

	status = acpi_evaluate_integer(dev->acpi_dev->handle, "INFO",
				      NULL, &value);
	if (ACPI_FAILURE(status)) {
		pr_err("ACPI INFO method execution failed\n");
		return -EIO;
	}

	return value;
}

static void toshiba_acpi_report_hotkey(struct toshiba_acpi_dev *dev,
				       int scancode)
{
	if (scancode == 0x100)
		return;

	/* act on key press; ignore key release */
	if (scancode & 0x80)
		return;

	if (!sparse_keymap_report_event(dev->hotkey_dev, scancode, 1, true))
		pr_info("Unknown key %x\n", scancode);
}

static int toshiba_acpi_setup_keyboard(struct toshiba_acpi_dev *dev)
{
	acpi_status status;
	acpi_handle ec_handle;
	int error;
	u32 hci_result;

	dev->hotkey_dev = input_allocate_device();
	if (!dev->hotkey_dev)
		return -ENOMEM;

	dev->hotkey_dev->name = "Toshiba input device";
	dev->hotkey_dev->phys = "toshiba_acpi/input0";
	dev->hotkey_dev->id.bustype = BUS_HOST;

	error = sparse_keymap_setup(dev->hotkey_dev, toshiba_acpi_keymap, NULL);
	if (error)
		goto err_free_dev;

	/*
	 * For some machines the SCI responsible for providing hotkey
	 * notification doesn't fire. We can trigger the notification
	 * whenever the Fn key is pressed using the NTFY method, if
	 * supported, so if it's present set up an i8042 key filter
	 * for this purpose.
	 */
	status = AE_ERROR;
	ec_handle = ec_get_handle();
	if (ec_handle && acpi_has_method(ec_handle, "NTFY")) {
		INIT_WORK(&dev->hotkey_work, toshiba_acpi_hotkey_work);

		error = i8042_install_filter(toshiba_acpi_i8042_filter);
		if (error) {
			pr_err("Error installing key filter\n");
			goto err_free_keymap;
		}

		dev->ntfy_supported = 1;
	}

	/*
	 * Determine hotkey query interface. Prefer using the INFO
	 * method when it is available.
	 */
	if (acpi_has_method(dev->acpi_dev->handle, "INFO"))
		dev->info_supported = 1;
	else {
		hci_write1(dev, HCI_SYSTEM_EVENT, 1, &hci_result);
		if (hci_result == HCI_SUCCESS)
			dev->system_event_supported = 1;
	}

	if (!dev->info_supported && !dev->system_event_supported) {
		pr_warn("No hotkey query interface found\n");
		goto err_remove_filter;
	}

	status = acpi_evaluate_object(dev->acpi_dev->handle, "ENAB", NULL, NULL);
	if (ACPI_FAILURE(status)) {
		pr_info("Unable to enable hotkeys\n");
		error = -ENODEV;
		goto err_remove_filter;
	}

	error = input_register_device(dev->hotkey_dev);
	if (error) {
		pr_info("Unable to register input device\n");
		goto err_remove_filter;
	}

	hci_write1(dev, HCI_HOTKEY_EVENT, HCI_HOTKEY_ENABLE, &hci_result);
	return 0;

 err_remove_filter:
	if (dev->ntfy_supported)
		i8042_remove_filter(toshiba_acpi_i8042_filter);
 err_free_keymap:
	sparse_keymap_free(dev->hotkey_dev);
 err_free_dev:
	input_free_device(dev->hotkey_dev);
	dev->hotkey_dev = NULL;
	return error;
}

static int toshiba_acpi_setup_backlight(struct toshiba_acpi_dev *dev)
{
	struct backlight_properties props;
	int brightness;
	int ret;
	bool enabled;

	/*
	 * Some machines don't support the backlight methods at all, and
	 * others support it read-only. Either of these is pretty useless,
	 * so only register the backlight device if the backlight method
	 * supports both reads and writes.
	 */
	brightness = __get_lcd_brightness(dev);
	if (brightness < 0)
		return 0;
	ret = set_lcd_brightness(dev, brightness);
	if (ret) {
		pr_debug("Backlight method is read-only, disabling backlight support\n");
		return 0;
	}

	/* Determine whether or not BIOS supports transflective backlight */
	ret = get_tr_backlight_status(dev, &enabled);
	dev->tr_backlight_supported = !ret;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = HCI_LCD_BRIGHTNESS_LEVELS - 1;

	/* adding an extra level and having 0 change to transflective mode */
	if (dev->tr_backlight_supported)
		props.max_brightness++;

	dev->backlight_dev = backlight_device_register("toshiba",
						       &dev->acpi_dev->dev,
						       dev,
						       &toshiba_backlight_data,
						       &props);
	if (IS_ERR(dev->backlight_dev)) {
		ret = PTR_ERR(dev->backlight_dev);
		pr_err("Could not register toshiba backlight device\n");
		dev->backlight_dev = NULL;
		return ret;
	}

	dev->backlight_dev->props.brightness = brightness;
	return 0;
}

static int toshiba_acpi_remove(struct acpi_device *acpi_dev)
{
	struct toshiba_acpi_dev *dev = acpi_driver_data(acpi_dev);

	remove_toshiba_proc_entries(dev);

	if (dev->ntfy_supported) {
		i8042_remove_filter(toshiba_acpi_i8042_filter);
		cancel_work_sync(&dev->hotkey_work);
	}

	if (dev->hotkey_dev) {
		input_unregister_device(dev->hotkey_dev);
		sparse_keymap_free(dev->hotkey_dev);
	}

	if (dev->bt_rfk) {
		rfkill_unregister(dev->bt_rfk);
		rfkill_destroy(dev->bt_rfk);
	}

	if (dev->backlight_dev)
		backlight_device_unregister(dev->backlight_dev);

	if (dev->illumination_supported)
		led_classdev_unregister(&dev->led_dev);

	if (toshiba_acpi)
		toshiba_acpi = NULL;

	kfree(dev);

	return 0;
}

static const char *find_hci_method(acpi_handle handle)
{
	if (acpi_has_method(handle, "GHCI"))
		return "GHCI";

	if (acpi_has_method(handle, "SPFC"))
		return "SPFC";

	return NULL;
}

static int toshiba_acpi_add(struct acpi_device *acpi_dev)
{
	struct toshiba_acpi_dev *dev;
	const char *hci_method;
	u32 dummy;
	bool bt_present;
	int ret = 0;

	if (toshiba_acpi)
		return -EBUSY;

	pr_info("Toshiba Laptop ACPI Extras version %s\n",
	       TOSHIBA_ACPI_VERSION);

	hci_method = find_hci_method(acpi_dev->handle);
	if (!hci_method) {
		pr_err("HCI interface not found\n");
		return -ENODEV;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->acpi_dev = acpi_dev;
	dev->method_hci = hci_method;
	acpi_dev->driver_data = dev;

	if (toshiba_acpi_setup_keyboard(dev))
		pr_info("Unable to activate hotkeys\n");

	mutex_init(&dev->mutex);

	ret = toshiba_acpi_setup_backlight(dev);
	if (ret)
		goto error;

	/* Register rfkill switch for Bluetooth */
	if (hci_get_bt_present(dev, &bt_present) == HCI_SUCCESS && bt_present) {
		dev->bt_rfk = rfkill_alloc("Toshiba Bluetooth",
					   &acpi_dev->dev,
					   RFKILL_TYPE_BLUETOOTH,
					   &toshiba_rfk_ops,
					   dev);
		if (!dev->bt_rfk) {
			pr_err("unable to allocate rfkill device\n");
			ret = -ENOMEM;
			goto error;
		}

		ret = rfkill_register(dev->bt_rfk);
		if (ret) {
			pr_err("unable to register rfkill device\n");
			rfkill_destroy(dev->bt_rfk);
			goto error;
		}
	}

	if (toshiba_illumination_available(dev)) {
		dev->led_dev.name = "toshiba::illumination";
		dev->led_dev.max_brightness = 1;
		dev->led_dev.brightness_set = toshiba_illumination_set;
		dev->led_dev.brightness_get = toshiba_illumination_get;
		if (!led_classdev_register(&acpi_dev->dev, &dev->led_dev))
			dev->illumination_supported = 1;
	}

	/* Determine whether or not BIOS supports fan and video interfaces */

	ret = get_video_status(dev, &dummy);
	dev->video_supported = !ret;

	ret = get_fan_status(dev, &dummy);
	dev->fan_supported = !ret;

	create_toshiba_proc_entries(dev);

	toshiba_acpi = dev;

	return 0;

error:
	toshiba_acpi_remove(acpi_dev);
	return ret;
}

static void toshiba_acpi_notify(struct acpi_device *acpi_dev, u32 event)
{
	struct toshiba_acpi_dev *dev = acpi_driver_data(acpi_dev);
	u32 hci_result, value;
	int retries = 3;
	int scancode;

	if (event != 0x80)
		return;

	if (dev->info_supported) {
		scancode = toshiba_acpi_query_hotkey(dev);
		if (scancode < 0)
			pr_err("Failed to query hotkey event\n");
		else if (scancode != 0)
			toshiba_acpi_report_hotkey(dev, scancode);
	} else if (dev->system_event_supported) {
		do {
			hci_read1(dev, HCI_SYSTEM_EVENT, &value, &hci_result);
			switch (hci_result) {
			case HCI_SUCCESS:
				toshiba_acpi_report_hotkey(dev, (int)value);
				break;
			case HCI_NOT_SUPPORTED:
				/*
				 * This is a workaround for an unresolved
				 * issue on some machines where system events
				 * sporadically become disabled.
				 */
				hci_write1(dev, HCI_SYSTEM_EVENT, 1,
					   &hci_result);
				pr_notice("Re-enabled hotkeys\n");
				/* fall through */
			default:
				retries--;
				break;
			}
		} while (retries && hci_result != HCI_EMPTY);
	}
}

#ifdef CONFIG_PM_SLEEP
static int toshiba_acpi_suspend(struct device *device)
{
	struct toshiba_acpi_dev *dev = acpi_driver_data(to_acpi_device(device));
	u32 result;

	if (dev->hotkey_dev)
		hci_write1(dev, HCI_HOTKEY_EVENT, HCI_HOTKEY_DISABLE, &result);

	return 0;
}

static int toshiba_acpi_resume(struct device *device)
{
	struct toshiba_acpi_dev *dev = acpi_driver_data(to_acpi_device(device));
	u32 result;

	if (dev->hotkey_dev)
		hci_write1(dev, HCI_HOTKEY_EVENT, HCI_HOTKEY_ENABLE, &result);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(toshiba_acpi_pm,
			 toshiba_acpi_suspend, toshiba_acpi_resume);

static struct acpi_driver toshiba_acpi_driver = {
	.name	= "Toshiba ACPI driver",
	.owner	= THIS_MODULE,
	.ids	= toshiba_device_ids,
	.flags	= ACPI_DRIVER_ALL_NOTIFY_EVENTS,
	.ops	= {
		.add		= toshiba_acpi_add,
		.remove		= toshiba_acpi_remove,
		.notify		= toshiba_acpi_notify,
	},
	.drv.pm	= &toshiba_acpi_pm,
};

static int __init toshiba_acpi_init(void)
{
	int ret;

	/*
	 * Machines with this WMI guid aren't supported due to bugs in
	 * their AML. This check relies on wmi initializing before
	 * toshiba_acpi to guarantee guids have been identified.
	 */
	if (wmi_has_guid(TOSHIBA_WMI_EVENT_GUID))
		return -ENODEV;

	toshiba_proc_dir = proc_mkdir(PROC_TOSHIBA, acpi_root_dir);
	if (!toshiba_proc_dir) {
		pr_err("Unable to create proc dir " PROC_TOSHIBA "\n");
		return -ENODEV;
	}

	ret = acpi_bus_register_driver(&toshiba_acpi_driver);
	if (ret) {
		pr_err("Failed to register ACPI driver: %d\n", ret);
		remove_proc_entry(PROC_TOSHIBA, acpi_root_dir);
	}

	return ret;
}

static void __exit toshiba_acpi_exit(void)
{
	acpi_bus_unregister_driver(&toshiba_acpi_driver);
	if (toshiba_proc_dir)
		remove_proc_entry(PROC_TOSHIBA, acpi_root_dir);
}

module_init(toshiba_acpi_init);
module_exit(toshiba_acpi_exit);
