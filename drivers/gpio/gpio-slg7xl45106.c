// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for slg7xl45106 I2C GPO expander
 * Based on gpio-pca9570.c
 *
 * Copyright (C) 2021 Xilinx, Inc.
 */

#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>

#define SLG7XL45106_GPO_REG	0xDB

/**
 * struct slg7xl45106 - GPIO driver data
 * @chip: GPIO controller chip
 * @lock: Protects write sequences
 */
struct slg7xl45106 {
	struct gpio_chip chip;
	struct mutex lock;	/* To protect writes */
};

static int slg7xl45106_read(struct slg7xl45106 *gpio)
{
	struct i2c_client *client = to_i2c_client(gpio->chip.parent);

	return i2c_smbus_read_byte_data(client, SLG7XL45106_GPO_REG);
}

static int slg7xl45106_write(struct slg7xl45106 *gpio, u8 value)
{
	struct i2c_client *client = to_i2c_client(gpio->chip.parent);

	return i2c_smbus_write_byte_data(client, SLG7XL45106_GPO_REG, value);
}

static int slg7xl45106_get_direction(struct gpio_chip *chip,
				     unsigned offset)
{
	/* This device always output */
	return GPIO_LINE_DIRECTION_OUT;
}

static int slg7xl45106_get(struct gpio_chip *chip, unsigned offset)
{
	struct slg7xl45106 *gpio = gpiochip_get_data(chip);
	int ret;

	ret = slg7xl45106_read(gpio);
	if (ret < 0)
		return ret;

	return !!(ret & BIT(offset));
}

static void slg7xl45106_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct slg7xl45106 *gpio = gpiochip_get_data(chip);
	u8 buffer;

	mutex_lock(&gpio->lock);

	buffer = slg7xl45106_read(gpio);
	if (value)
		buffer |= BIT(offset);
	else
		buffer &= ~BIT(offset);

	slg7xl45106_write(gpio, buffer);

	mutex_unlock(&gpio->lock);
}

static int slg7xl45106_probe(struct i2c_client *client)
{
	struct slg7xl45106 *gpio;

	gpio = devm_kzalloc(&client->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->chip.label = client->name;
	gpio->chip.parent = &client->dev;
	gpio->chip.owner = THIS_MODULE;
	gpio->chip.get_direction = slg7xl45106_get_direction;
	gpio->chip.get = slg7xl45106_get;
	gpio->chip.set = slg7xl45106_set;
	gpio->chip.base = -1;
	gpio->chip.ngpio = (uintptr_t)device_get_match_data(&client->dev);
	gpio->chip.can_sleep = true;

	mutex_init(&gpio->lock);

	i2c_set_clientdata(client, gpio);

	return devm_gpiochip_add_data(&client->dev, &gpio->chip, gpio);
}

static const struct i2c_device_id slg7xl45106_id_table[] = {
	{ "slg7xl45106", 8 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, slg7xl45106_id_table);

static const struct of_device_id slg7xl45106_of_match_table[] = {
	{ .compatible = "dlg,slg7xl45106", .data = (void *)8 },
	{ }
};
MODULE_DEVICE_TABLE(of, slg7xl45106_of_match_table);

static struct i2c_driver slg7xl45106_driver = {
	.driver = {
		.name = "slg7xl45106",
		.of_match_table = slg7xl45106_of_match_table,
	},
	.probe_new = slg7xl45106_probe,
	.id_table = slg7xl45106_id_table,
};
module_i2c_driver(slg7xl45106_driver);

MODULE_AUTHOR("Raviteja Narayanam <raviteja.narayanam@xilinx.com>");
MODULE_DESCRIPTION("GPIO expander driver for slg7xl45106");
MODULE_LICENSE("GPL v2");
