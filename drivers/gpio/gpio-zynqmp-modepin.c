// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the ps-mode pin configuration.
 *
 * Copyright (c) 2021 Xilinx, Inc.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/firmware/xlnx-zynqmp.h>

#define MODE_PINS			4
#define GET_OUTEN_PIN(pin)		(1U << (pin))

/**
 * modepin_gpio_get_value - Get the state of the specified pin of GPIO device
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * This function reads the state of the specified pin of the GPIO device.
 *
 * Return: 0 if the pin is low, 1 if pin is high, -EINVAL wrong pin configured
 *         or error value.
 */
static int modepin_gpio_get_value(struct gpio_chip *chip, unsigned int pin)
{
	u32 out_en;
	u32 regval = 0;
	int ret;

	out_en = GET_OUTEN_PIN(pin);

	ret = zynqmp_pm_bootmode_read(&regval);
	if (ret) {
		pr_err("modepin: get value err %d\n", ret);
		return ret;
	}

	return (out_en & (regval >> 8U)) ? 1 : 0;
}

/**
 * modepin_gpio_set_value - Modify the state of the pin with specified value
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 * @state:	value used to modify the state of the specified pin
 *
 * Return:	None.
 */
static void modepin_gpio_set_value(struct gpio_chip *chip, unsigned int pin,
				   int state)
{
	u32 out_en;
	u32 bootpin_val = 0;
	int ret;

	out_en = GET_OUTEN_PIN(pin);
	state = state != 0 ? out_en : 0;
	bootpin_val = (state << (8U)) | out_en;

	/* Configure bootpin value */
	ret = zynqmp_pm_bootmode_write(bootpin_val);
	if (ret)
		pr_err("modepin: %s failed\n", __func__);
}

/**
 * modepin_gpio_dir_in - Set the direction of the specified GPIO pin as input
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * Return: 0 always
 */
static int modepin_gpio_dir_in(struct gpio_chip *chip, unsigned int pin)
{
	return 0;
}

/**
 * modepin_gpio_dir_out - Set the direction of the specified GPIO pin as output
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 * @state:	value to be written to specified pin
 *
 * Return: 0 always
 */
static int modepin_gpio_dir_out(struct gpio_chip *chip, unsigned int pin,
				int state)
{
	return 0;
}

/**
 * modepin_gpio_probe - Initialization method for modepin_gpio
 * @pdev:		platform device instance
 *
 * Return: 0 on success, negative error otherwise.
 */
static int modepin_gpio_probe(struct platform_device *pdev)
{
	struct gpio_chip *chip;
	int status;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	platform_set_drvdata(pdev, chip);

	/* configure the gpio chip */
	chip->base = -1;
	chip->ngpio = MODE_PINS;
	chip->owner = THIS_MODULE;
	chip->parent = &pdev->dev;
	chip->get = modepin_gpio_get_value;
	chip->set = modepin_gpio_set_value;
	chip->direction_input = modepin_gpio_dir_in;
	chip->direction_output = modepin_gpio_dir_out;
	chip->label = dev_name(&pdev->dev);

	/* modepin gpio registration */
	status = devm_gpiochip_add_data(&pdev->dev, chip, chip);
	if (status)
		dev_err_probe(&pdev->dev, status,
			      "Failed to add GPIO chip\n");

	return status;
}

static const struct of_device_id modepin_platform_id[] = {
	{ .compatible = "xlnx,zynqmp-gpio-modepin", },
	{ }
};

static struct platform_driver modepin_platform_driver = {
	.driver = {
		.name = "modepin-gpio",
		.of_match_table = modepin_platform_id,
	},
	.probe = modepin_gpio_probe,
};

module_platform_driver(modepin_platform_driver);

MODULE_AUTHOR("Piyush Mehta <piyush.mehta@xilinx.com>");
MODULE_DESCRIPTION("ZynqMP Boot PS_MODE Configuration");
MODULE_LICENSE("GPL v2");
