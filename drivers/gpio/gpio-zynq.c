/*
 * Xilinx Zynq GPIO device driver
 *
 * Copyright (C) 2009 - 2014 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 */

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define DRIVER_NAME "zynq-gpio"
#define ZYNQ_GPIO_NR_GPIOS	118

static struct irq_domain *irq_domain;

/* Register offsets for the GPIO device */

/* LSW Mask & Data -WO */
#define ZYNQ_GPIO_DATA_LSW_OFFSET(BANK)	(0x000 + (8 * BANK))
/* MSW Mask & Data -WO */
#define ZYNQ_GPIO_DATA_MSW_OFFSET(BANK)	(0x004 + (8 * BANK))
/* Data Register-RW */
#define ZYNQ_GPIO_DATA_OFFSET(BANK)	(0x040 + (4 * BANK))
/* Direction mode reg-RW */
#define ZYNQ_GPIO_DIRM_OFFSET(BANK)	(0x204 + (0x40 * BANK))
/* Output enable reg-RW */
#define ZYNQ_GPIO_OUTEN_OFFSET(BANK)	(0x208 + (0x40 * BANK))
/* Interrupt mask reg-RO */
#define ZYNQ_GPIO_INTMASK_OFFSET(BANK)	(0x20C + (0x40 * BANK))
/* Interrupt enable reg-WO */
#define ZYNQ_GPIO_INTEN_OFFSET(BANK)	(0x210 + (0x40 * BANK))
/* Interrupt disable reg-WO */
#define ZYNQ_GPIO_INTDIS_OFFSET(BANK)	(0x214 + (0x40 * BANK))
/* Interrupt status reg-RO */
#define ZYNQ_GPIO_INTSTS_OFFSET(BANK)	(0x218 + (0x40 * BANK))
/* Interrupt type reg-RW */
#define ZYNQ_GPIO_INTTYPE_OFFSET(BANK)	(0x21C + (0x40 * BANK))
/* Interrupt polarity reg-RW */
#define ZYNQ_GPIO_INTPOL_OFFSET(BANK)	(0x220 + (0x40 * BANK))
/* Interrupt on any, reg-RW */
#define ZYNQ_GPIO_INTANY_OFFSET(BANK)	(0x224 + (0x40 * BANK))

/* Read/Write access to the GPIO PS registers */
static inline u32 zynq_gpio_readreg(void __iomem *offset)
{
	return readl_relaxed(offset);
}

static inline void zynq_gpio_writereg(void __iomem *offset, u32 val)
{
	writel_relaxed(val, offset);
}

static unsigned int zynq_gpio_pin_table[] = {
	31, /* 0 - 31 */
	53, /* 32 - 53 */
	85, /* 54 - 85 */
	117 /* 86 - 117 */
};

/* Maximum banks */
#define ZYNQ_GPIO_MAX_BANK	4

/* Disable all interrupts mask */
#define ZYNQ_GPIO_IXR_DISABLE_ALL	0xFFFFFFFF

/* GPIO pin high */
#define ZYNQ_GPIO_PIN_HIGH 1

/* Mid pin number of a bank */
#define ZYNQ_GPIO_MID_PIN_NUM 16

/* GPIO upper 16 bit mask */
#define ZYNQ_GPIO_UPPER_MASK 0xFFFF0000

/**
 * struct zynq_gpio - gpio device private data structure
 * @chip:	instance of the gpio_chip
 * @base_addr:	base address of the GPIO device
 * @irq:	irq associated with the controller
 * @irq_base:	base of IRQ number for interrupt
 * @clk:	clock resource for this controller
 */
struct zynq_gpio {
	struct gpio_chip chip;
	void __iomem *base_addr;
	unsigned int irq;
	unsigned int irq_base;
	struct clk *clk;
};

/**
 * zynq_gpio_get_bank_pin - Get the bank number and pin number within that bank
 * for a given pin in the GPIO device
 * @pin_num:	gpio pin number within the device
 * @bank_num:	an output parameter used to return the bank number of the gpio
 *		pin
 * @bank_pin_num: an output parameter used to return pin number within a bank
 *		  for the given gpio pin
 *
 * Returns the bank number.
 */
static inline void zynq_gpio_get_bank_pin(unsigned int pin_num,
					  unsigned int *bank_num,
					  unsigned int *bank_pin_num)
{
	for (*bank_num = 0; *bank_num < ZYNQ_GPIO_MAX_BANK; (*bank_num)++)
		if (pin_num <= zynq_gpio_pin_table[*bank_num])
			break;

	if (!(*bank_num))
		*bank_pin_num = pin_num;
	else
		*bank_pin_num = pin_num %
				(zynq_gpio_pin_table[*bank_num - 1] + 1);
}

/**
 * zynq_gpio_get_value - Get the state of the specified pin of GPIO device
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * This function reads the state of the specified pin of the GPIO device.
 *
 * Return: 0 if the pin is low, 1 if pin is high.
 */
static int zynq_gpio_get_value(struct gpio_chip *chip, unsigned int pin)
{
	unsigned int bank_num, bank_pin_num, data;
	struct zynq_gpio *gpio = container_of(chip, struct zynq_gpio, chip);

	zynq_gpio_get_bank_pin(pin, &bank_num, &bank_pin_num);

	data = zynq_gpio_readreg(gpio->base_addr +
				 ZYNQ_GPIO_DATA_OFFSET(bank_num));
	return (data >> bank_pin_num) & ZYNQ_GPIO_PIN_HIGH;
}

/**
 * zynq_gpio_set_value - Modify the state of the pin with specified value
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 * @state:	value used to modify the state of the specified pin
 *
 * This function calculates the register offset (i.e to lower 16 bits or
 * upper 16 bits) based on the given pin number and sets the state of a
 * gpio pin to the specified value. The state is either 0 or non-zero.
 */
static void zynq_gpio_set_value(struct gpio_chip *chip, unsigned int pin,
				int state)
{
	unsigned int reg_offset, bank_num, bank_pin_num;
	struct zynq_gpio *gpio = container_of(chip, struct zynq_gpio, chip);

	zynq_gpio_get_bank_pin(pin, &bank_num, &bank_pin_num);

	if (bank_pin_num >= ZYNQ_GPIO_MID_PIN_NUM) {
		/* only 16 data bits in bit maskable reg */
		bank_pin_num -= ZYNQ_GPIO_MID_PIN_NUM;
		reg_offset = ZYNQ_GPIO_DATA_MSW_OFFSET(bank_num);
	} else {
		reg_offset = ZYNQ_GPIO_DATA_LSW_OFFSET(bank_num);
	}

	/*
	 * get the 32 bit value to be written to the mask/data register where
	 * the upper 16 bits is the mask and lower 16 bits is the data
	 */
	if (state)
		state = 1;
	state = ~(1 << (bank_pin_num + ZYNQ_GPIO_MID_PIN_NUM)) &
		((state << bank_pin_num) | ZYNQ_GPIO_UPPER_MASK);

	zynq_gpio_writereg(gpio->base_addr + reg_offset, state);
}

/**
 * zynq_gpio_dir_in - Set the direction of the specified GPIO pin as input
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * This function uses the read-modify-write sequence to set the direction of
 * the gpio pin as input.
 *
 * Return: 0 always
 */
static int zynq_gpio_dir_in(struct gpio_chip *chip, unsigned int pin)
{
	unsigned int reg, bank_num, bank_pin_num;
	struct zynq_gpio *gpio = container_of(chip, struct zynq_gpio, chip);

	zynq_gpio_get_bank_pin(pin, &bank_num, &bank_pin_num);
	/* clear the bit in direction mode reg to set the pin as input */
	reg = zynq_gpio_readreg(gpio->base_addr +
				ZYNQ_GPIO_DIRM_OFFSET(bank_num));
	reg &= ~(1 << bank_pin_num);
	zynq_gpio_writereg(gpio->base_addr + ZYNQ_GPIO_DIRM_OFFSET(bank_num),
			   reg);

	return 0;
}

/**
 * zynq_gpio_dir_out - Set the direction of the specified GPIO pin as output
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 * @state:	value to be written to specified pin
 *
 * This function sets the direction of specified GPIO pin as output, configures
 * the Output Enable register for the pin and uses zynq_gpio_set to set
 * the state of the pin to the value specified.
 *
 * Return: 0 always
 */
static int zynq_gpio_dir_out(struct gpio_chip *chip, unsigned int pin,
			     int state)
{
	struct zynq_gpio *gpio = container_of(chip, struct zynq_gpio, chip);
	unsigned int reg, bank_num, bank_pin_num;

	zynq_gpio_get_bank_pin(pin, &bank_num, &bank_pin_num);

	/* set the GPIO pin as output */
	reg = zynq_gpio_readreg(gpio->base_addr +
				ZYNQ_GPIO_DIRM_OFFSET(bank_num));
	reg |= 1 << bank_pin_num;
	zynq_gpio_writereg(gpio->base_addr + ZYNQ_GPIO_DIRM_OFFSET(bank_num),
			   reg);

	/* configure the output enable reg for the pin */
	reg = zynq_gpio_readreg(gpio->base_addr +
				ZYNQ_GPIO_OUTEN_OFFSET(bank_num));
	reg |= 1 << bank_pin_num;
	zynq_gpio_writereg(gpio->base_addr + ZYNQ_GPIO_OUTEN_OFFSET(bank_num),
			   reg);

	/* set the state of the pin */
	zynq_gpio_set_value(chip, pin, state);
	return 0;
}

static int zynq_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return irq_find_mapping(irq_domain, offset);
}

/**
 * zynq_gpio_irq_ack - Acknowledge the interrupt of a gpio pin
 * @irq_data:	irq data containing irq number of gpio pin for the interrupt
 *		to ack
 *
 * This function calculates gpio pin number from irq number and sets the bit
 * in the Interrupt Status Register of the corresponding bank, to ACK the irq.
 */
static void zynq_gpio_irq_ack(struct irq_data *irq_data)
{
	struct zynq_gpio *gpio = (struct zynq_gpio *)
				 irq_data_get_irq_chip_data(irq_data);
	unsigned int device_pin_num, bank_num, bank_pin_num;

	device_pin_num = irq_data->hwirq;
	zynq_gpio_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num);
	zynq_gpio_writereg(gpio->base_addr + ZYNQ_GPIO_INTSTS_OFFSET(bank_num),
			   1 << bank_pin_num);
}

/**
 * zynq_gpio_irq_mask - Disable the interrupts for a gpio pin
 * @irq_data:	per irq and chip data passed down to chip functions
 *
 * This function calculates gpio pin number from irq number and sets the
 * bit in the Interrupt Disable register of the corresponding bank to disable
 * interrupts for that pin.
 */
static void zynq_gpio_irq_mask(struct irq_data *irq_data)
{
	struct zynq_gpio *gpio = (struct zynq_gpio *)
				 irq_data_get_irq_chip_data(irq_data);
	unsigned int device_pin_num, bank_num, bank_pin_num;

	device_pin_num = irq_data->hwirq;
	zynq_gpio_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num);
	zynq_gpio_writereg(gpio->base_addr + ZYNQ_GPIO_INTDIS_OFFSET(bank_num),
			   1 << bank_pin_num);
}

/**
 * zynq_gpio_irq_unmask - Enable the interrupts for a gpio pin
 * @irq_data:	irq data containing irq number of gpio pin for the interrupt
 *		to enable
 *
 * This function calculates the gpio pin number from irq number and sets the
 * bit in the Interrupt Enable register of the corresponding bank to enable
 * interrupts for that pin.
 */
static void zynq_gpio_irq_unmask(struct irq_data *irq_data)
{
	struct zynq_gpio *gpio = irq_data_get_irq_chip_data(irq_data);
	unsigned int device_pin_num, bank_num, bank_pin_num;

	device_pin_num = irq_data->hwirq;
	zynq_gpio_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num);
	zynq_gpio_writereg(gpio->base_addr + ZYNQ_GPIO_INTEN_OFFSET(bank_num),
			   1 << bank_pin_num);
}

/**
 * zynq_gpio_set_irq_type - Set the irq type for a gpio pin
 * @irq_data:	irq data containing irq number of gpio pin
 * @type:	interrupt type that is to be set for the gpio pin
 *
 * This function gets the gpio pin number and its bank from the gpio pin number
 * and configures the INT_TYPE, INT_POLARITY and INT_ANY registers.
 *
 * Return: 0, negative error otherwise.
 * TYPE-EDGE_RISING,  INT_TYPE - 1, INT_POLARITY - 1,  INT_ANY - 0;
 * TYPE-EDGE_FALLING, INT_TYPE - 1, INT_POLARITY - 0,  INT_ANY - 0;
 * TYPE-EDGE_BOTH,    INT_TYPE - 1, INT_POLARITY - NA, INT_ANY - 1;
 * TYPE-LEVEL_HIGH,   INT_TYPE - 0, INT_POLARITY - 1,  INT_ANY - NA;
 * TYPE-LEVEL_LOW,    INT_TYPE - 0, INT_POLARITY - 0,  INT_ANY - NA
 */
static int zynq_gpio_set_irq_type(struct irq_data *irq_data, unsigned int type)
{
	struct zynq_gpio *gpio = irq_data_get_irq_chip_data(irq_data);
	unsigned int device_pin_num, bank_num, bank_pin_num;
	unsigned int int_type, int_pol, int_any;

	device_pin_num = irq_data->hwirq;
	zynq_gpio_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num);

	int_type = zynq_gpio_readreg(gpio->base_addr +
				     ZYNQ_GPIO_INTTYPE_OFFSET(bank_num));
	int_pol = zynq_gpio_readreg(gpio->base_addr +
				    ZYNQ_GPIO_INTPOL_OFFSET(bank_num));
	int_any = zynq_gpio_readreg(gpio->base_addr +
				    ZYNQ_GPIO_INTANY_OFFSET(bank_num));

	/*
	 * based on the type requested, configure the INT_TYPE, INT_POLARITY
	 * and INT_ANY registers
	 */
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		int_type |= (1 << bank_pin_num);
		int_pol |= (1 << bank_pin_num);
		int_any &= ~(1 << bank_pin_num);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		int_type |= (1 << bank_pin_num);
		int_pol &= ~(1 << bank_pin_num);
		int_any &= ~(1 << bank_pin_num);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		int_type |= (1 << bank_pin_num);
		int_any |= (1 << bank_pin_num);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		int_type &= ~(1 << bank_pin_num);
		int_pol |= (1 << bank_pin_num);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		int_type &= ~(1 << bank_pin_num);
		int_pol &= ~(1 << bank_pin_num);
		break;
	default:
		return -EINVAL;
	}

	zynq_gpio_writereg(gpio->base_addr +
			   ZYNQ_GPIO_INTTYPE_OFFSET(bank_num), int_type);
	zynq_gpio_writereg(gpio->base_addr + ZYNQ_GPIO_INTPOL_OFFSET(bank_num),
			   int_pol);
	zynq_gpio_writereg(gpio->base_addr + ZYNQ_GPIO_INTANY_OFFSET(bank_num),
			   int_any);
	return 0;
}

static int zynq_gpio_set_wake(struct irq_data *data, unsigned int on)
{
	if (on)
		zynq_gpio_irq_unmask(data);
	else
		zynq_gpio_irq_mask(data);

	return 0;
}

/* irq chip descriptor */
static struct irq_chip zynq_gpio_irqchip = {
	.name		= DRIVER_NAME,
	.irq_ack	= zynq_gpio_irq_ack,
	.irq_mask	= zynq_gpio_irq_mask,
	.irq_unmask	= zynq_gpio_irq_unmask,
	.irq_set_type	= zynq_gpio_set_irq_type,
	.irq_set_wake	= zynq_gpio_set_wake,
};

/**
 * zynq_gpio_irqhandler - IRQ handler for the gpio banks of a gpio device
 * @irq:	irq number of the gpio bank where interrupt has occurred
 * @desc:	irq descriptor instance of the 'irq'
 *
 * This function reads the Interrupt Status Register of each bank to get the
 * gpio pin number which has triggered an interrupt. It then acks the triggered
 * interrupt and calls the pin specific handler set by the higher layer
 * application for that pin.
 * Note: A bug is reported if no handler is set for the gpio pin.
 */
static void zynq_gpio_irqhandler(unsigned int irq, struct irq_desc *desc)
{
	struct zynq_gpio *gpio = (struct zynq_gpio *)irq_get_handler_data(irq);
	int gpio_irq = gpio->irq_base;
	unsigned int int_sts, int_enb, bank_num;
	struct irq_desc *gpio_irq_desc;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	for (bank_num = 0; bank_num < ZYNQ_GPIO_MAX_BANK; bank_num++) {
		int_sts = zynq_gpio_readreg(gpio->base_addr +
					    ZYNQ_GPIO_INTSTS_OFFSET(bank_num));
		int_enb = zynq_gpio_readreg(gpio->base_addr +
					    ZYNQ_GPIO_INTMASK_OFFSET(bank_num));
		int_sts &= ~int_enb;

		for (; int_sts != 0; int_sts >>= 1, gpio_irq++) {
			if (!(int_sts & 1))
				continue;
			gpio_irq_desc = irq_to_desc(gpio_irq);
			BUG_ON(!gpio_irq_desc);
			chip = irq_desc_get_chip(gpio_irq_desc);
			BUG_ON(!chip);
			chip->irq_ack(&gpio_irq_desc->irq_data);

			/* call the pin specific handler */
			generic_handle_irq(gpio_irq);
		}
		/* shift to first virtual irq of next bank */
		gpio_irq = gpio->irq_base + zynq_gpio_pin_table[bank_num] + 1;
	}

	chip = irq_desc_get_chip(desc);
	chained_irq_exit(chip, desc);
}

static int __maybe_unused zynq_gpio_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct zynq_gpio *gpio = platform_get_drvdata(pdev);

	if (!device_may_wakeup(dev)) {
		if (!pm_runtime_suspended(dev))
			clk_disable(gpio->clk);
		return 0;
	}

	return 0;
}

static int __maybe_unused zynq_gpio_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct zynq_gpio *gpio = platform_get_drvdata(pdev);

	if (!device_may_wakeup(dev)) {
		if (!pm_runtime_suspended(dev))
			return clk_enable(gpio->clk);
	}

	return 0;
}

static int __maybe_unused zynq_gpio_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct zynq_gpio *gpio = platform_get_drvdata(pdev);

	clk_disable(gpio->clk);

	return 0;
}

static int __maybe_unused zynq_gpio_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct zynq_gpio *gpio = platform_get_drvdata(pdev);

	return clk_enable(gpio->clk);
}

static int __maybe_unused zynq_gpio_idle(struct device *dev)
{
	return pm_schedule_suspend(dev, 1);
}

static int zynq_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	int ret;

	ret = pm_runtime_get_sync(chip->dev);

	/*
	 * If the device is already active pm_runtime_get() will return 1 on
	 * success, but gpio_request still needs to return 0.
	 */
	return ret < 0 ? ret : 0;
}

static void zynq_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pm_runtime_put_sync(chip->dev);
}

static const struct dev_pm_ops zynq_gpio_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(zynq_gpio_suspend, zynq_gpio_resume)
	SET_RUNTIME_PM_OPS(zynq_gpio_runtime_suspend, zynq_gpio_runtime_resume,
			   zynq_gpio_idle)
};

/**
 * zynq_gpio_probe - Initialization method for a zynq_gpio device
 * @pdev:	platform device instance
 *
 * This function allocates memory resources for the gpio device and registers
 * all the banks of the device. It will also set up interrupts for the gpio
 * pins.
 * Note: Interrupts are disabled for all the banks during initialization.
 *
 * Return: 0 on success, negative error otherwise.
 */
static int zynq_gpio_probe(struct platform_device *pdev)
{
	int ret, pin_num, bank_num, gpio_irq;
	unsigned int irq_num;
	struct zynq_gpio *gpio;
	struct gpio_chip *chip;
	struct resource *res;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	platform_set_drvdata(pdev, gpio);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gpio->base_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gpio->base_addr))
		return PTR_ERR(gpio->base_addr);

	irq_num = platform_get_irq(pdev, 0);
	gpio->irq = irq_num;

	/* configure the gpio chip */
	chip = &gpio->chip;
	chip->label = "zynq_gpio";
	chip->owner = THIS_MODULE;
	chip->dev = &pdev->dev;
	chip->get = zynq_gpio_get_value;
	chip->set = zynq_gpio_set_value;
	chip->request = zynq_gpio_request;
	chip->free = zynq_gpio_free;
	chip->direction_input = zynq_gpio_dir_in;
	chip->direction_output = zynq_gpio_dir_out;
	chip->to_irq = zynq_gpio_to_irq;
	chip->dbg_show = NULL;
	chip->base = 0;		/* default pin base */
	chip->ngpio = ZYNQ_GPIO_NR_GPIOS;
	chip->can_sleep = 0;

	gpio->irq_base = irq_alloc_descs(-1, 0, chip->ngpio, 0);
	if (gpio->irq_base < 0) {
		dev_err(&pdev->dev, "Couldn't allocate IRQ numbers\n");
		return -ENODEV;
	}

	irq_domain = irq_domain_add_legacy(pdev->dev.of_node,
					   chip->ngpio, gpio->irq_base, 0,
					   &irq_domain_simple_ops, NULL);

	/* report a bug if gpio chip registration fails */
	ret = gpiochip_add(chip);
	if (ret < 0)
		return ret;

	/* Enable GPIO clock */
	gpio->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(gpio->clk)) {
		dev_err(&pdev->dev, "input clock not found.\n");
		if (gpiochip_remove(chip))
			dev_err(&pdev->dev, "Failed to remove gpio chip\n");
		return PTR_ERR(gpio->clk);
	}
	ret = clk_prepare_enable(gpio->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable clock.\n");
		if (gpiochip_remove(chip))
			dev_err(&pdev->dev, "Failed to remove gpio chip\n");
		return ret;
	}

	/* disable interrupts for all banks */
	for (bank_num = 0; bank_num < ZYNQ_GPIO_MAX_BANK; bank_num++) {
		zynq_gpio_writereg(gpio->base_addr +
				   ZYNQ_GPIO_INTDIS_OFFSET(bank_num),
				   ZYNQ_GPIO_IXR_DISABLE_ALL);
	}

	/*
	 * set the irq chip, handler and irq chip data for callbacks for
	 * each pin
	 */
	for (pin_num = 0; pin_num < min_t(int, ZYNQ_GPIO_NR_GPIOS,
					  (int)chip->ngpio); pin_num++) {
		gpio_irq = irq_find_mapping(irq_domain, pin_num);
		irq_set_chip_and_handler(gpio_irq, &zynq_gpio_irqchip,
					 handle_simple_irq);
		irq_set_chip_data(gpio_irq, (void *)gpio);
		set_irq_flags(gpio_irq, IRQF_VALID);
	}

	irq_set_handler_data(irq_num, (void *)gpio);
	irq_set_chained_handler(irq_num, zynq_gpio_irqhandler);

	pm_runtime_enable(&pdev->dev);

	device_set_wakeup_capable(&pdev->dev, 1);

	return 0;
}

/**
 * zynq_gpio_remove - Driver removal function
 * @pdev:	platform device instance
 *
 * Return: 0 always
 */
static int zynq_gpio_remove(struct platform_device *pdev)
{
	struct zynq_gpio *gpio = platform_get_drvdata(pdev);

	clk_disable_unprepare(gpio->clk);
	device_set_wakeup_capable(&pdev->dev, 0);
	return 0;
}

static struct of_device_id zynq_gpio_of_match[] = {
	{ .compatible = "xlnx,zynq-gpio-1.0", },
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, zynq_gpio_of_match);

static struct platform_driver zynq_gpio_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.pm	= &zynq_gpio_dev_pm_ops,
		.of_match_table = zynq_gpio_of_match,
	},
	.probe		= zynq_gpio_probe,
	.remove		= zynq_gpio_remove,
};

/**
 * zynq_gpio_init - Initial driver registration call
 *
 * Return: value from platform_driver_register
 */
static int __init zynq_gpio_init(void)
{
	return platform_driver_register(&zynq_gpio_driver);
}

postcore_initcall(zynq_gpio_init);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Zynq GPIO driver");
MODULE_LICENSE("GPL");
