/*
 * Copyright (C) 2008 Atmel Corporation
 *
 * Backlight driver using Atmel PWM peripheral.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/backlight.h>
#include <linux/atmel_pwm.h>
#include <linux/atmel-pwm-bl.h>
#include <linux/slab.h>

struct atmel_pwm_bl {
	const struct atmel_pwm_bl_platform_data	*pdata;
	struct backlight_device			*bldev;
	struct platform_device			*pdev;
	struct pwm_channel			pwmc;
	int					gpio_on;
};

static void atmel_pwm_bl_set_gpio_on(struct atmel_pwm_bl *pwmbl, int on)
{
	if (!gpio_is_valid(pwmbl->gpio_on))
		return;

	gpio_set_value(pwmbl->gpio_on, on ^ pwmbl->pdata->on_active_low);
}

static int atmel_pwm_bl_set_intensity(struct backlight_device *bd)
{
	struct atmel_pwm_bl *pwmbl = bl_get_data(bd);
	int intensity = bd->props.brightness;
	int pwm_duty;

	if (bd->props.power != FB_BLANK_UNBLANK)
		intensity = 0;
	if (bd->props.fb_blank != FB_BLANK_UNBLANK)
		intensity = 0;

	if (pwmbl->pdata->pwm_active_low)
		pwm_duty = pwmbl->pdata->pwm_duty_min + intensity;
	else
		pwm_duty = pwmbl->pdata->pwm_duty_max - intensity;

	if (pwm_duty > pwmbl->pdata->pwm_duty_max)
		pwm_duty = pwmbl->pdata->pwm_duty_max;
	if (pwm_duty < pwmbl->pdata->pwm_duty_min)
		pwm_duty = pwmbl->pdata->pwm_duty_min;

	if (!intensity) {
		atmel_pwm_bl_set_gpio_on(pwmbl, 0);
		pwm_channel_writel(&pwmbl->pwmc, PWM_CUPD, pwm_duty);
		pwm_channel_disable(&pwmbl->pwmc);
	} else {
		pwm_channel_enable(&pwmbl->pwmc);
		pwm_channel_writel(&pwmbl->pwmc, PWM_CUPD, pwm_duty);
		atmel_pwm_bl_set_gpio_on(pwmbl, 1);
	}

	return 0;
}

static int atmel_pwm_bl_get_intensity(struct backlight_device *bd)
{
	struct atmel_pwm_bl *pwmbl = bl_get_data(bd);
	u32 cdty;
	u32 intensity;

	cdty = pwm_channel_readl(&pwmbl->pwmc, PWM_CDTY);
	if (pwmbl->pdata->pwm_active_low)
		intensity = cdty - pwmbl->pdata->pwm_duty_min;
	else
		intensity = pwmbl->pdata->pwm_duty_max - cdty;

	return intensity & 0xffff;
}

static int atmel_pwm_bl_init_pwm(struct atmel_pwm_bl *pwmbl)
{
	unsigned long pwm_rate = pwmbl->pwmc.mck;
	unsigned long prescale = DIV_ROUND_UP(pwm_rate,
			(pwmbl->pdata->pwm_frequency *
			 pwmbl->pdata->pwm_compare_max)) - 1;

	/*
	 * Prescale must be power of two and maximum 0xf in size because of
	 * hardware limit. PWM speed will be:
	 *	PWM module clock speed / (2 ^ prescale).
	 */
	prescale = fls(prescale);
	if (prescale > 0xf)
		prescale = 0xf;

	pwm_channel_writel(&pwmbl->pwmc, PWM_CMR, prescale);
	pwm_channel_writel(&pwmbl->pwmc, PWM_CDTY,
			pwmbl->pdata->pwm_duty_min +
			pwmbl->bldev->props.brightness);
	pwm_channel_writel(&pwmbl->pwmc, PWM_CPRD,
			pwmbl->pdata->pwm_compare_max);

	dev_info(&pwmbl->pdev->dev, "Atmel PWM backlight driver (%lu Hz)\n",
		pwmbl->pwmc.mck / pwmbl->pdata->pwm_compare_max /
		(1 << prescale));

	return pwm_channel_enable(&pwmbl->pwmc);
}

static const struct backlight_ops atmel_pwm_bl_ops = {
	.get_brightness = atmel_pwm_bl_get_intensity,
	.update_status  = atmel_pwm_bl_set_intensity,
};

static int atmel_pwm_bl_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	const struct atmel_pwm_bl_platform_data *pdata;
	struct backlight_device *bldev;
	struct atmel_pwm_bl *pwmbl;
	unsigned long flags;
	int retval;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		return -ENODEV;

	if (pdata->pwm_compare_max < pdata->pwm_duty_max ||
			pdata->pwm_duty_min > pdata->pwm_duty_max ||
			pdata->pwm_frequency == 0)
		return -EINVAL;

	pwmbl = devm_kzalloc(&pdev->dev, sizeof(struct atmel_pwm_bl),
				GFP_KERNEL);
	if (!pwmbl)
		return -ENOMEM;

	pwmbl->pdev = pdev;
	pwmbl->pdata = pdata;
	pwmbl->gpio_on = pdata->gpio_on;

	retval = pwm_channel_alloc(pdata->pwm_channel, &pwmbl->pwmc);
	if (retval)
		return retval;

	if (gpio_is_valid(pwmbl->gpio_on)) {
		/* Turn display off by default. */
		if (pdata->on_active_low)
			flags = GPIOF_OUT_INIT_HIGH;
		else
			flags = GPIOF_OUT_INIT_LOW;

		retval = devm_gpio_request_one(&pdev->dev, pwmbl->gpio_on,
						flags, "gpio_atmel_pwm_bl");
		if (retval)
			goto err_free_pwm;
	}

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = pdata->pwm_duty_max - pdata->pwm_duty_min;
	bldev = devm_backlight_device_register(&pdev->dev, "atmel-pwm-bl",
					&pdev->dev, pwmbl, &atmel_pwm_bl_ops,
					&props);
	if (IS_ERR(bldev)) {
		retval = PTR_ERR(bldev);
		goto err_free_pwm;
	}

	pwmbl->bldev = bldev;

	platform_set_drvdata(pdev, pwmbl);

	/* Power up the backlight by default at middle intesity. */
	bldev->props.power = FB_BLANK_UNBLANK;
	bldev->props.brightness = bldev->props.max_brightness / 2;

	retval = atmel_pwm_bl_init_pwm(pwmbl);
	if (retval)
		goto err_free_pwm;

	atmel_pwm_bl_set_intensity(bldev);

	return 0;

err_free_pwm:
	pwm_channel_free(&pwmbl->pwmc);

	return retval;
}

static int atmel_pwm_bl_remove(struct platform_device *pdev)
{
	struct atmel_pwm_bl *pwmbl = platform_get_drvdata(pdev);

	atmel_pwm_bl_set_gpio_on(pwmbl, 0);
	pwm_channel_disable(&pwmbl->pwmc);
	pwm_channel_free(&pwmbl->pwmc);

	return 0;
}

static struct platform_driver atmel_pwm_bl_driver = {
	.driver = {
		.name = "atmel-pwm-bl",
	},
	/* REVISIT add suspend() and resume() */
	.probe = atmel_pwm_bl_probe,
	.remove = atmel_pwm_bl_remove,
};

module_platform_driver(atmel_pwm_bl_driver);

MODULE_AUTHOR("Hans-Christian egtvedt <hans-christian.egtvedt@atmel.com>");
MODULE_DESCRIPTION("Atmel PWM backlight driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:atmel-pwm-bl");
