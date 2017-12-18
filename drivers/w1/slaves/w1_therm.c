/*
 *	w1_therm.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <zbr@ioremap.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the therms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <asm/types.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "../w1.h"
#include "../w1_int.h"
#include "../w1_family.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <zbr@ioremap.net>");
MODULE_DESCRIPTION("Driver for 1-wire Dallas network protocol, temperature family.");
MODULE_ALIAS("w1-family-" __stringify(W1_THERM_DS18S20));
MODULE_ALIAS("w1-family-" __stringify(W1_THERM_DS1822));
MODULE_ALIAS("w1-family-" __stringify(W1_THERM_DS18B20));
MODULE_ALIAS("w1-family-" __stringify(W1_THERM_DS1825));
MODULE_ALIAS("w1-family-" __stringify(W1_THERM_DS28EA00));

/* Allow the strong pullup to be disabled, but default to enabled.
 * If it was disabled a parasite powered device might not get the require
 * current to do a temperature conversion.  If it is enabled parasite powered
 * devices have a better chance of getting the current required.
 * In case the parasite power-detection is not working (seems to be the case
 * for some DS18S20) the strong pullup can also be forced, regardless of the
 * power state of the devices.
 *
 * Summary of options:
 * - strong_pullup = 0	Disable strong pullup completely
 * - strong_pullup = 1	Enable automatic strong pullup detection
 * - strong_pullup = 2	Force strong pullup
 */
static int w1_strong_pullup = 1;
module_param_named(strong_pullup, w1_strong_pullup, int, 0);

struct w1_therm_family_data {
	uint8_t rom[9];
	atomic_t refcnt;
};

/* return the address of the refcnt in the family data */
#define THERM_REFCNT(family_data) \
	(&((struct w1_therm_family_data *)family_data)->refcnt)

static int w1_therm_add_slave(struct w1_slave *sl)
{
	sl->family_data = kzalloc(sizeof(struct w1_therm_family_data),
		GFP_KERNEL);
	if (!sl->family_data)
		return -ENOMEM;
	atomic_set(THERM_REFCNT(sl->family_data), 1);
	return 0;
}

static void w1_therm_remove_slave(struct w1_slave *sl)
{
	int refcnt = atomic_sub_return(1, THERM_REFCNT(sl->family_data));

	while (refcnt) {
		msleep(1000);
		refcnt = atomic_read(THERM_REFCNT(sl->family_data));
	}
	kfree(sl->family_data);
	sl->family_data = NULL;
}

static ssize_t w1_slave_show(struct device *device,
	struct device_attribute *attr, char *buf);

static ssize_t w1_slave_store(struct device *device,
	struct device_attribute *attr, const char *buf, size_t size);

static ssize_t w1_seq_show(struct device *device,
	struct device_attribute *attr, char *buf);

static DEVICE_ATTR_RW(w1_slave);
static DEVICE_ATTR_RO(w1_seq);

static struct attribute *w1_therm_attrs[] = {
	&dev_attr_w1_slave.attr,
	NULL,
};

static struct attribute *w1_ds28ea00_attrs[] = {
	&dev_attr_w1_slave.attr,
	&dev_attr_w1_seq.attr,
	NULL,
};
ATTRIBUTE_GROUPS(w1_therm);
ATTRIBUTE_GROUPS(w1_ds28ea00);

static struct w1_family_ops w1_therm_fops = {
	.add_slave	= w1_therm_add_slave,
	.remove_slave	= w1_therm_remove_slave,
	.groups		= w1_therm_groups,
};

static struct w1_family_ops w1_ds28ea00_fops = {
	.add_slave	= w1_therm_add_slave,
	.remove_slave	= w1_therm_remove_slave,
	.groups		= w1_ds28ea00_groups,
};

static struct w1_family w1_therm_family_DS18S20 = {
	.fid = W1_THERM_DS18S20,
	.fops = &w1_therm_fops,
};

static struct w1_family w1_therm_family_DS18B20 = {
	.fid = W1_THERM_DS18B20,
	.fops = &w1_therm_fops,
};

static struct w1_family w1_therm_family_DS1822 = {
	.fid = W1_THERM_DS1822,
	.fops = &w1_therm_fops,
};

static struct w1_family w1_therm_family_DS28EA00 = {
	.fid = W1_THERM_DS28EA00,
	.fops = &w1_ds28ea00_fops,
};

static struct w1_family w1_therm_family_DS1825 = {
	.fid = W1_THERM_DS1825,
	.fops = &w1_therm_fops,
};

struct w1_therm_family_converter {
	u8			broken;
	u16			reserved;
	struct w1_family	*f;
	int			(*convert)(u8 rom[9]);
	int			(*precision)(struct device *device, int val);
	int			(*eeprom)(struct device *device);
};

/* write configuration to eeprom */
static inline int w1_therm_eeprom(struct device *device);

/* Set precision for conversion */
static inline int w1_DS18B20_precision(struct device *device, int val);
static inline int w1_DS18S20_precision(struct device *device, int val);

/* The return value is millidegrees Centigrade. */
static inline int w1_DS18B20_convert_temp(u8 rom[9]);
static inline int w1_DS18S20_convert_temp(u8 rom[9]);

static struct w1_therm_family_converter w1_therm_families[] = {
	{
		.f		= &w1_therm_family_DS18S20,
		.convert	= w1_DS18S20_convert_temp,
		.precision	= w1_DS18S20_precision,
		.eeprom		= w1_therm_eeprom
	},
	{
		.f		= &w1_therm_family_DS1822,
		.convert	= w1_DS18B20_convert_temp,
		.precision	= w1_DS18S20_precision,
		.eeprom		= w1_therm_eeprom
	},
	{
		.f		= &w1_therm_family_DS18B20,
		.convert	= w1_DS18B20_convert_temp,
		.precision	= w1_DS18B20_precision,
		.eeprom		= w1_therm_eeprom
	},
	{
		.f		= &w1_therm_family_DS28EA00,
		.convert	= w1_DS18B20_convert_temp,
		.precision	= w1_DS18S20_precision,
		.eeprom		= w1_therm_eeprom
	},
	{
		.f		= &w1_therm_family_DS1825,
		.convert	= w1_DS18B20_convert_temp,
		.precision	= w1_DS18S20_precision,
		.eeprom		= w1_therm_eeprom
	}
};

static inline int w1_therm_eeprom(struct device *device)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct w1_master *dev = sl->master;
	u8 rom[9], external_power;
	int ret, max_trying = 10;
	u8 *family_data = sl->family_data;

	ret = mutex_lock_interruptible(&dev->bus_mutex);
	if (ret != 0)
		goto post_unlock;

	if (!sl->family_data) {
		ret = -ENODEV;
		goto pre_unlock;
	}

	/* prevent the slave from going away in sleep */
	atomic_inc(THERM_REFCNT(family_data));
	memset(rom, 0, sizeof(rom));

	while (max_trying--) {
		if (!w1_reset_select_slave(sl)) {
			unsigned int tm = 10;
			unsigned long sleep_rem;

			/* check if in parasite mode */
			w1_write_8(dev, W1_READ_PSUPPLY);
			external_power = w1_read_8(dev);

			if (w1_reset_select_slave(sl))
				continue;

			/* 10ms strong pullup/delay after the copy command */
			if (w1_strong_pullup == 2 ||
			    (!external_power && w1_strong_pullup))
				w1_next_pullup(dev, tm);

			w1_write_8(dev, W1_COPY_SCRATCHPAD);

			if (external_power) {
				mutex_unlock(&dev->bus_mutex);

				sleep_rem = msleep_interruptible(tm);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto post_unlock;
				}

				ret = mutex_lock_interruptible(&dev->bus_mutex);
				if (ret != 0)
					goto post_unlock;
			} else if (!w1_strong_pullup) {
				sleep_rem = msleep_interruptible(tm);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto pre_unlock;
				}
			}

			break;
		}
	}

pre_unlock:
	mutex_unlock(&dev->bus_mutex);

post_unlock:
	atomic_dec(THERM_REFCNT(family_data));
	return ret;
}

/* DS18S20 does not feature configuration register */
static inline int w1_DS18S20_precision(struct device *device, int val)
{
	return 0;
}

static inline int w1_DS18B20_precision(struct device *device, int val)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct w1_master *dev = sl->master;
	u8 rom[9], crc;
	int ret, max_trying = 10;
	u8 *family_data = sl->family_data;
	uint8_t precision_bits;
	uint8_t mask = 0x60;

	if (val > 12 || val < 9) {
		pr_warn("Unsupported precision\n");
		return -1;
	}

	ret = mutex_lock_interruptible(&dev->bus_mutex);
	if (ret != 0)
		goto post_unlock;

	if (!sl->family_data) {
		ret = -ENODEV;
		goto pre_unlock;
	}

	/* prevent the slave from going away in sleep */
	atomic_inc(THERM_REFCNT(family_data));
	memset(rom, 0, sizeof(rom));

	/* translate precision to bitmask (see datasheet page 9) */
	switch (val) {
	case 9:
		precision_bits = 0x00;
		break;
	case 10:
		precision_bits = 0x20;
		break;
	case 11:
		precision_bits = 0x40;
		break;
	case 12:
	default:
		precision_bits = 0x60;
		break;
	}

	while (max_trying--) {
		crc = 0;

		if (!w1_reset_select_slave(sl)) {
			int count = 0;

			/* read values to only alter precision bits */
			w1_write_8(dev, W1_READ_SCRATCHPAD);
			count = w1_read_block(dev, rom, 9);
			if (count != 9)
				dev_warn(device, "w1_read_block() returned %u instead of 9.\n",	count);

			crc = w1_calc_crc8(rom, 8);
			if (rom[8] == crc) {
				rom[4] = (rom[4] & ~mask) | (precision_bits & mask);

				if (!w1_reset_select_slave(sl)) {
					w1_write_8(dev, W1_WRITE_SCRATCHPAD);
					w1_write_8(dev, rom[2]);
					w1_write_8(dev, rom[3]);
					w1_write_8(dev, rom[4]);

					break;
				}
			}
		}
	}

pre_unlock:
	mutex_unlock(&dev->bus_mutex);

post_unlock:
	atomic_dec(THERM_REFCNT(family_data));
	return ret;
}

static inline int w1_DS18B20_convert_temp(u8 rom[9])
{
	s16 t = le16_to_cpup((__le16 *)rom);

	return t*1000/16;
}

static inline int w1_DS18S20_convert_temp(u8 rom[9])
{
	int t, h;

	if (!rom[7])
		return 0;

	if (rom[1] == 0)
		t = ((s32)rom[0] >> 1)*1000;
	else
		t = 1000*(-1*(s32)(0x100-rom[0]) >> 1);

	t -= 250;
	h = 1000*((s32)rom[7] - (s32)rom[6]);
	h /= (s32)rom[7];
	t += h;

	return t;
}

static inline int w1_convert_temp(u8 rom[9], u8 fid)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(w1_therm_families); ++i)
		if (w1_therm_families[i].f->fid == fid)
			return w1_therm_families[i].convert(rom);

	return 0;
}

static ssize_t w1_slave_store(struct device *device,
			      struct device_attribute *attr, const char *buf,
			      size_t size)
{
	int val, ret;
	struct w1_slave *sl = dev_to_w1_slave(device);
	int i;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(w1_therm_families); ++i) {
		if (w1_therm_families[i].f->fid == sl->family->fid) {
			/* zero value indicates to write current configuration to eeprom */
			if (val == 0)
				ret = w1_therm_families[i].eeprom(device);
			else
				ret = w1_therm_families[i].precision(device, val);
			break;
		}
	}
	return ret ? : size;
}

static ssize_t w1_slave_show(struct device *device,
	struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	struct w1_master *dev = sl->master;
	u8 rom[9], crc, verdict, external_power;
	int i, ret, max_trying = 10;
	ssize_t c = PAGE_SIZE;
	u8 *family_data = sl->family_data;

	ret = mutex_lock_interruptible(&dev->bus_mutex);
	if (ret != 0)
		goto post_unlock;

	if (!sl->family_data) {
		ret = -ENODEV;
		goto pre_unlock;
	}

	/* prevent the slave from going away in sleep */
	atomic_inc(THERM_REFCNT(family_data));
	memset(rom, 0, sizeof(rom));

	while (max_trying--) {

		verdict = 0;
		crc = 0;

		if (!w1_reset_select_slave(sl)) {
			int count = 0;
			unsigned int tm = 750;
			unsigned long sleep_rem;

			w1_write_8(dev, W1_READ_PSUPPLY);
			external_power = w1_read_8(dev);

			if (w1_reset_select_slave(sl))
				continue;

			/* 750ms strong pullup (or delay) after the convert */
			if (w1_strong_pullup == 2 ||
					(!external_power && w1_strong_pullup))
				w1_next_pullup(dev, tm);

			w1_write_8(dev, W1_CONVERT_TEMP);

			if (external_power) {
				mutex_unlock(&dev->bus_mutex);

				sleep_rem = msleep_interruptible(tm);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto post_unlock;
				}

				ret = mutex_lock_interruptible(&dev->bus_mutex);
				if (ret != 0)
					goto post_unlock;
			} else if (!w1_strong_pullup) {
				sleep_rem = msleep_interruptible(tm);
				if (sleep_rem != 0) {
					ret = -EINTR;
					goto pre_unlock;
				}
			}

			if (!w1_reset_select_slave(sl)) {

				w1_write_8(dev, W1_READ_SCRATCHPAD);
				count = w1_read_block(dev, rom, 9);
				if (count != 9) {
					dev_warn(device, "w1_read_block() "
						"returned %u instead of 9.\n",
						count);
				}

				crc = w1_calc_crc8(rom, 8);

				if (rom[8] == crc)
					verdict = 1;
			}
		}

		if (verdict)
			break;
	}

	for (i = 0; i < 9; ++i)
		c -= snprintf(buf + PAGE_SIZE - c, c, "%02x ", rom[i]);
	c -= snprintf(buf + PAGE_SIZE - c, c, ": crc=%02x %s\n",
		      crc, (verdict) ? "YES" : "NO");
	if (verdict)
		memcpy(family_data, rom, sizeof(rom));
	else
		dev_warn(device, "Read failed CRC check\n");

	for (i = 0; i < 9; ++i)
		c -= snprintf(buf + PAGE_SIZE - c, c, "%02x ",
			      ((u8 *)family_data)[i]);

	c -= snprintf(buf + PAGE_SIZE - c, c, "t=%d\n",
		w1_convert_temp(rom, sl->family->fid));
	ret = PAGE_SIZE - c;

pre_unlock:
	mutex_unlock(&dev->bus_mutex);

post_unlock:
	atomic_dec(THERM_REFCNT(family_data));
	return ret;
}

#define W1_42_CHAIN	0x99
#define W1_42_CHAIN_OFF	0x3C
#define W1_42_CHAIN_OFF_INV	0xC3
#define W1_42_CHAIN_ON	0x5A
#define W1_42_CHAIN_ON_INV	0xA5
#define W1_42_CHAIN_DONE 0x96
#define W1_42_CHAIN_DONE_INV 0x69
#define W1_42_COND_READ	0x0F
#define W1_42_SUCCESS_CONFIRM_BYTE 0xAA
#define W1_42_FINISHED_BYTE 0xFF
static ssize_t w1_seq_show(struct device *device,
	struct device_attribute *attr, char *buf)
{
	struct w1_slave *sl = dev_to_w1_slave(device);
	ssize_t c = PAGE_SIZE;
	int rv;
	int i;
	u8 ack;
	u64 rn;
	struct w1_reg_num *reg_num;
	int seq = 0;

	mutex_lock(&sl->master->bus_mutex);
	/* Place all devices in CHAIN state */
	if (w1_reset_bus(sl->master))
		goto error;
	w1_write_8(sl->master, W1_SKIP_ROM);
	w1_write_8(sl->master, W1_42_CHAIN);
	w1_write_8(sl->master, W1_42_CHAIN_ON);
	w1_write_8(sl->master, W1_42_CHAIN_ON_INV);
	msleep(sl->master->pullup_duration);

	/* check for acknowledgment */
	ack = w1_read_8(sl->master);
	if (ack != W1_42_SUCCESS_CONFIRM_BYTE)
		goto error;

	/* In case the bus fails to send 0xFF, limit*/
	for (i = 0; i <= 64; i++) {
		if (w1_reset_bus(sl->master))
			goto error;

		w1_write_8(sl->master, W1_42_COND_READ);
		rv = w1_read_block(sl->master, (u8 *)&rn, 8);
		reg_num = (struct w1_reg_num *) &rn;
		if (reg_num->family == W1_42_FINISHED_BYTE)
			break;
		if (sl->reg_num.id == reg_num->id)
			seq = i;

		w1_write_8(sl->master, W1_42_CHAIN);
		w1_write_8(sl->master, W1_42_CHAIN_DONE);
		w1_write_8(sl->master, W1_42_CHAIN_DONE_INV);
		w1_read_block(sl->master, &ack, sizeof(ack));

		/* check for acknowledgment */
		ack = w1_read_8(sl->master);
		if (ack != W1_42_SUCCESS_CONFIRM_BYTE)
			goto error;

	}

	/* Exit from CHAIN state */
	if (w1_reset_bus(sl->master))
		goto error;
	w1_write_8(sl->master, W1_SKIP_ROM);
	w1_write_8(sl->master, W1_42_CHAIN);
	w1_write_8(sl->master, W1_42_CHAIN_OFF);
	w1_write_8(sl->master, W1_42_CHAIN_OFF_INV);

	/* check for acknowledgment */
	ack = w1_read_8(sl->master);
	if (ack != W1_42_SUCCESS_CONFIRM_BYTE)
		goto error;
	mutex_unlock(&sl->master->bus_mutex);

	c -= snprintf(buf + PAGE_SIZE - c, c, "%d\n", seq);
	return PAGE_SIZE - c;
error:
	mutex_unlock(&sl->master->bus_mutex);
	return -EIO;
}

static int __init w1_therm_init(void)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(w1_therm_families); ++i) {
		err = w1_register_family(w1_therm_families[i].f);
		if (err)
			w1_therm_families[i].broken = 1;
	}

	return 0;
}

static void __exit w1_therm_fini(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(w1_therm_families); ++i)
		if (!w1_therm_families[i].broken)
			w1_unregister_family(w1_therm_families[i].f);
}

module_init(w1_therm_init);
module_exit(w1_therm_fini);
