/*
 * drivers/i2c/busses/i2c-gpio.c for Nios2
 *
 * drivers/i2c/busses/i2c-ixp2000.c
 *
 * I2C adapter for IXP2000 systems using GPIOs for I2C bus
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 * Based on IXDP2400 code by: Naeem M. Afzal <naeem.m.afzal@intel.com>
 * Made generic by: Jeff Daly <jeffrey.daly@intel.com>
 *
 * Copyright (c) 2003-2004 MontaVista Software Inc.
 *
 * This file is licensed under  the terms of the GNU General Public 
 * License version 2. This program is licensed "as is" without any 
 * warranty of any kind, whether express or implied.
 *
 * From Jeff Daly:
 *
 * I2C adapter driver for Intel IXDP2xxx platforms. This should work for any
 * IXP2000 platform if it uses the HW GPIO in the same manner.  Basically, 
 * SDA and SCL GPIOs have external pullups.  Setting the respective GPIO to 
 * an input will make the signal a '1' via the pullup.  Setting them to 
 * outputs will pull them down. 
 *
 * The GPIOs are open drain signals and are used as configuration strap inputs
 * during power-up so there's generally a buffer on the board that needs to be 
 * 'enabled' to drive the GPIOs.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/i2c-id.h>

#include <asm/io.h>
#include <asm/gpio.h>

static inline int gpio_scl_pin(void *data)
{
	return ((struct gpio_i2c_pins*)data)->scl_pin;
}

static inline int gpio_sda_pin(void *data)
{
	return ((struct gpio_i2c_pins*)data)->sda_pin;
}


static void gpio_bit_setscl(void *data, int val)
{
	int i = 5000;

	if (val) {
		outl(3,gpio_scl_pin(data));
		while(!(inl(gpio_scl_pin(data)) & 1) && i--);
	} else {
		outl(2,gpio_scl_pin(data));
	}
}

static void gpio_bit_setsda(void *data, int val)
{
	if (val) {
	        outl(1,gpio_sda_pin(data));
	} else {
		outl(0,gpio_sda_pin(data));
	}
}

static int gpio_bit_getscl(void *data)
{
	return inl(gpio_scl_pin(data)) & 1;
}

static int gpio_bit_getsda(void *data)
{
	return inl(gpio_sda_pin(data)) & 1;
}

struct gpio_i2c_data {
	struct gpio_i2c_pins *gpio_pins;
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo_data;
};

static int gpio_i2c_remove(struct platform_device *plat_dev)
{
	struct gpio_i2c_data *drv_data = platform_get_drvdata(plat_dev);

	platform_set_drvdata(plat_dev, NULL);

	i2c_bit_del_bus(&drv_data->adapter);

	kfree(drv_data);

	return 0;
}

static int gpio_i2c_probe(struct platform_device *plat_dev)
{
	int err;
	struct gpio_i2c_pins *gpio = plat_dev->dev.platform_data;
	struct gpio_i2c_data *drv_data = 
		kzalloc(sizeof(struct gpio_i2c_data), GFP_KERNEL);

	if (!drv_data)
		return -ENOMEM;
	drv_data->gpio_pins = gpio;

	drv_data->algo_data.data = gpio;
	drv_data->algo_data.setsda = gpio_bit_setsda;
	drv_data->algo_data.setscl = gpio_bit_setscl;
	drv_data->algo_data.getsda = gpio_bit_getsda;
	drv_data->algo_data.getscl = gpio_bit_getscl;
	drv_data->algo_data.udelay = 6;
	drv_data->algo_data.timeout = 100;

	drv_data->adapter.id = I2C_HW_B_IXP2000,  // borrowed,
	strlcpy(drv_data->adapter.name, plat_dev->dev.driver->name,
		I2C_NAME_SIZE);
	drv_data->adapter.algo_data = &drv_data->algo_data,

	drv_data->adapter.dev.parent = &plat_dev->dev;
	drv_data->adapter.class = I2C_CLASS_ALL;

	outl(1,gpio->sda_pin);
	outl(1,gpio->scl_pin);

	if ((err = i2c_bit_add_bus(&drv_data->adapter)) != 0) {
		dev_err(&plat_dev->dev, "Could not install, error %d\n", err);
		kfree(drv_data);
		return err;
	} 

	platform_set_drvdata(plat_dev, drv_data);
	printk("i2c-gpio driver at %08x\n",gpio->sda_pin);

	return 0;
}

static struct platform_driver gpio_i2c_driver = {
	.probe		= gpio_i2c_probe,
	.remove		= gpio_i2c_remove,
	.driver		= {
		.name	= "GPIO-I2C",
		.owner	= THIS_MODULE,
	},
};

static int __init gpio_i2c_init(void)
{
	return platform_driver_register(&gpio_i2c_driver);
}

static void __exit gpio_i2c_exit(void)
{
	platform_driver_unregister(&gpio_i2c_driver);
}

module_init(gpio_i2c_init);
module_exit(gpio_i2c_exit);

