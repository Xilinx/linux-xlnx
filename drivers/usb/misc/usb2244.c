// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Microchip USB2244 Ultra Fast USB 2.0 Multi-Format,
 * SD/MMC, and MS Flash Media Controllers
 *
 * Copyright (c) 2021 Xilinx, Inc.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>

struct usb2244 {
	struct gpio_desc *reset_gpio;
};

static int usb2244_init_hw(struct device *dev, struct usb2244 *data)
{
	data = devm_kzalloc(dev, sizeof(struct usb2244), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(data->reset_gpio)) {
		return dev_err_probe(dev, PTR_ERR(data->reset_gpio),
				     "Failed to request reset GPIO\n");
	}

	/* Toggle RESET_N to reset the hub. */
	gpiod_set_value_cansleep(data->reset_gpio, 1);
	usleep_range(5, 10);
	gpiod_set_value_cansleep(data->reset_gpio, 0);
	msleep(5);

	return 0;
}

static int usb2244_probe(struct platform_device *pdev)
{
	struct usb2244 *data = NULL;

	/* Trigger gpio reset to the hub. */
	return usb2244_init_hw(&pdev->dev, data);
}

static const struct of_device_id usb2244_of_match[] = {
	{ .compatible = "microchip,usb2244", },
	{ }
};

static struct platform_driver usb2244_driver = {
	.driver = {
		.name = "microchip,usb2244",
		.of_match_table	= usb2244_of_match,
	},
	.probe = usb2244_probe,
};

module_platform_driver(usb2244_driver);

MODULE_AUTHOR("Piyush Mehta <piyush.mehta@xilinx.com>");
MODULE_DESCRIPTION("USB2244 Ultra Fast SD-Controller");
MODULE_LICENSE("GPL v2");
