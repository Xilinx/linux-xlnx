// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx gpio driver for xps/axi_gpio IP.
 *
 * Copyright 2008 - 2013 Xilinx, Inc.
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/gpio/driver.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>

/* Register Offset Definitions */
#define XGPIO_DATA_OFFSET   (0x0)	/* Data register  */
#define XGPIO_TRI_OFFSET    (0x4)	/* I/O direction register  */

#define XGPIO_CHANNEL_OFFSET	0x8

#define XGPIO_GIER_OFFSET      0x11c /* Global Interrupt Enable */
#define XGPIO_GIER_IE          BIT(31)
#define XGPIO_IPISR_OFFSET     0x120 /* IP Interrupt Status */
#define XGPIO_IPIER_OFFSET     0x128 /* IP Interrupt Enable */

/* Read/Write access to the GPIO registers */
#if defined(CONFIG_ARCH_ZYNQ) || defined(CONFIG_X86) || defined(CONFIG_ARM64)
# define xgpio_readreg(offset)		readl(offset)
# define xgpio_writereg(offset, val)	writel(val, offset)
#else
# define xgpio_readreg(offset)		__raw_readl(offset)
# define xgpio_writereg(offset, val)	__raw_writel(val, offset)
#endif

/**
 * struct xgpio_instance - Stores information about GPIO device
 * @gc: GPIO chip
 * @regs: register block
 * @gpio_width: GPIO width for every channel
 * @gpio_state: GPIO state shadow register
 * @gpio_dir: GPIO direction shadow register
 * @gpio_lock: Lock used for synchronization
 * @clk: clock resource for this driver
 * @irq_base: GPIO channel irq base address
 * @irq_enable: GPIO irq enable/disable bitfield
 * @irq_domain: irq_domain of the controller
 */

struct xgpio_instance {
	struct gpio_chip gc;
	void __iomem *regs;
	unsigned int gpio_width[2];
	u32 gpio_state[2];
	u32 gpio_dir[2];
	spinlock_t gpio_lock[2];	/* For serializing operations */
	struct clk *clk;
	int irq_base;
	u32 irq_enable;
	struct irq_domain *irq_domain;
};

static inline int xgpio_index(struct xgpio_instance *chip, int gpio)
{
	if (gpio >= chip->gpio_width[0])
		return 1;

	return 0;
}

static inline int xgpio_regoffset(struct xgpio_instance *chip, int gpio)
{
	if (xgpio_index(chip, gpio))
		return XGPIO_CHANNEL_OFFSET;

	return 0;
}

static inline int xgpio_offset(struct xgpio_instance *chip, int gpio)
{
	if (xgpio_index(chip, gpio))
		return gpio - chip->gpio_width[0];

	return gpio;
}

/**
 * xgpio_get - Read the specified signal of the GPIO device.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 *
 * This function reads the specified signal of the GPIO device.
 *
 * Return:
 * 0 if direction of GPIO signals is set as input otherwise it
 * returns negative error value.
 */
static int xgpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct xgpio_instance *chip = gpiochip_get_data(gc);
	u32 val;

	val = xgpio_readreg(chip->regs + XGPIO_DATA_OFFSET +
			    xgpio_regoffset(chip, gpio));

	return !!(val & BIT(xgpio_offset(chip, gpio)));
}

/**
 * xgpio_set - Write the specified signal of the GPIO device.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 * @val:    Value to be written to specified signal.
 *
 * This function writes the specified value in to the specified signal of the
 * GPIO device.
 */
static void xgpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	unsigned long flags;
	struct xgpio_instance *chip = gpiochip_get_data(gc);
	int index =  xgpio_index(chip, gpio);
	int offset =  xgpio_offset(chip, gpio);

	spin_lock_irqsave(&chip->gpio_lock[index], flags);

	/* Write to GPIO signal and set its direction to output */
	if (val)
		chip->gpio_state[index] |= BIT(offset);
	else
		chip->gpio_state[index] &= ~BIT(offset);

	xgpio_writereg(chip->regs + XGPIO_DATA_OFFSET +
		       xgpio_regoffset(chip, gpio), chip->gpio_state[index]);

	spin_unlock_irqrestore(&chip->gpio_lock[index], flags);
}

/**
 * xgpio_set_multiple - Write the specified signals of the GPIO device.
 * @gc:     Pointer to gpio_chip device structure.
 * @mask:   Mask of the GPIOS to modify.
 * @bits:   Value to be wrote on each GPIO
 *
 * This function writes the specified values into the specified signals of the
 * GPIO devices.
 */
static void xgpio_set_multiple(struct gpio_chip *gc, unsigned long *mask,
			       unsigned long *bits)
{
	unsigned long flags;
	struct xgpio_instance *chip = gpiochip_get_data(gc);
	int index = xgpio_index(chip, 0);
	int offset, i;

	spin_lock_irqsave(&chip->gpio_lock[index], flags);

	/* Write to GPIO signals */
	for (i = 0; i < gc->ngpio; i++) {
		if (*mask == 0)
			break;
		if (index !=  xgpio_index(chip, i)) {
			xgpio_writereg(chip->regs + XGPIO_DATA_OFFSET +
				       xgpio_regoffset(chip, i),
				       chip->gpio_state[index]);
			spin_unlock_irqrestore(&chip->gpio_lock[index], flags);
			index =  xgpio_index(chip, i);
			spin_lock_irqsave(&chip->gpio_lock[index], flags);
		}
		if (__test_and_clear_bit(i, mask)) {
			offset =  xgpio_offset(chip, i);
			if (test_bit(i, bits))
				chip->gpio_state[index] |= BIT(offset);
			else
				chip->gpio_state[index] &= ~BIT(offset);
		}
	}

	xgpio_writereg(chip->regs + XGPIO_DATA_OFFSET +
		       xgpio_regoffset(chip, i), chip->gpio_state[index]);

	spin_unlock_irqrestore(&chip->gpio_lock[index], flags);
}

/**
 * xgpio_dir_in - Set the direction of the specified GPIO signal as input.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 *
 * Return:
 * 0 - if direction of GPIO signals is set as input
 * otherwise it returns negative error value.
 */
static int xgpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	unsigned long flags;
	struct xgpio_instance *chip = gpiochip_get_data(gc);
	int index =  xgpio_index(chip, gpio);
	int offset =  xgpio_offset(chip, gpio);

	spin_lock_irqsave(&chip->gpio_lock[index], flags);

	/* Set the GPIO bit in shadow register and set direction as input */
	chip->gpio_dir[index] |= BIT(offset);
	xgpio_writereg(chip->regs + XGPIO_TRI_OFFSET +
		       xgpio_regoffset(chip, gpio), chip->gpio_dir[index]);

	spin_unlock_irqrestore(&chip->gpio_lock[index], flags);

	return 0;
}

/**
 * xgpio_dir_out - Set the direction of the specified GPIO signal as output.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 * @val:    Value to be written to specified signal.
 *
 * This function sets the direction of specified GPIO signal as output.
 *
 * Return:
 * If all GPIO signals of GPIO chip is configured as input then it returns
 * error otherwise it returns 0.
 */
static int xgpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	unsigned long flags;
	struct xgpio_instance *chip = gpiochip_get_data(gc);
	int index =  xgpio_index(chip, gpio);
	int offset =  xgpio_offset(chip, gpio);

	spin_lock_irqsave(&chip->gpio_lock[index], flags);

	/* Write state of GPIO signal */
	if (val)
		chip->gpio_state[index] |= BIT(offset);
	else
		chip->gpio_state[index] &= ~BIT(offset);
	xgpio_writereg(chip->regs + XGPIO_DATA_OFFSET +
			xgpio_regoffset(chip, gpio), chip->gpio_state[index]);

	/* Clear the GPIO bit in shadow register and set direction as output */
	chip->gpio_dir[index] &= ~BIT(offset);
	xgpio_writereg(chip->regs + XGPIO_TRI_OFFSET +
			xgpio_regoffset(chip, gpio), chip->gpio_dir[index]);

	spin_unlock_irqrestore(&chip->gpio_lock[index], flags);

	return 0;
}

/**
 * xgpio_save_regs - Set initial values of GPIO pins
 * @chip: Pointer to GPIO instance
 */
static void xgpio_save_regs(struct xgpio_instance *chip)
{
	xgpio_writereg(chip->regs + XGPIO_DATA_OFFSET,	chip->gpio_state[0]);
	xgpio_writereg(chip->regs + XGPIO_TRI_OFFSET, chip->gpio_dir[0]);

	if (!chip->gpio_width[1])
		return;

	xgpio_writereg(chip->regs + XGPIO_DATA_OFFSET + XGPIO_CHANNEL_OFFSET,
		       chip->gpio_state[1]);
	xgpio_writereg(chip->regs + XGPIO_TRI_OFFSET + XGPIO_CHANNEL_OFFSET,
		       chip->gpio_dir[1]);
}

static int xgpio_request(struct gpio_chip *chip, unsigned int offset)
{
	int ret = pm_runtime_get_sync(chip->parent);

	/*
	 * If the device is already active pm_runtime_get() will return 1 on
	 * success, but gpio_request still needs to return 0.
	 */
	return ret < 0 ? ret : 0;
}

static void xgpio_free(struct gpio_chip *chip, unsigned int offset)
{
	pm_runtime_put(chip->parent);
}

static int __maybe_unused xgpio_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int irq = platform_get_irq(pdev, 0);
	struct irq_data *data = irq_get_irq_data(irq);

	if (!irqd_is_wakeup_set(data))
		return pm_runtime_force_suspend(dev);

	return 0;
}

static int __maybe_unused xgpio_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int irq = platform_get_irq(pdev, 0);
	struct irq_data *data = irq_get_irq_data(irq);

	if (!irqd_is_wakeup_set(data))
		return pm_runtime_force_resume(dev);

	return 0;
}

static int __maybe_unused xgpio_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xgpio_instance *gpio = platform_get_drvdata(pdev);

	clk_disable(gpio->clk);

	return 0;
}

static int __maybe_unused xgpio_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xgpio_instance *gpio = platform_get_drvdata(pdev);

	return clk_enable(gpio->clk);
}

static const struct dev_pm_ops xgpio_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xgpio_suspend, xgpio_resume)
	SET_RUNTIME_PM_OPS(xgpio_runtime_suspend,
			   xgpio_runtime_resume, NULL)
};

/**
 * xgpiops_irq_mask - Write the specified signal of the GPIO device.
 * @irq_data: per irq and chip data passed down to chip functions
 */
static void xgpio_irq_mask(struct irq_data *irq_data)
{
	unsigned long flags;
	struct xgpio_instance *chip = irq_data_get_irq_chip_data(irq_data);
	u32 offset = irq_data->irq - chip->irq_base;
	u32 temp;
	s32 val;
	int index = xgpio_index(chip, 0);

	pr_debug("%s: Disable %d irq, irq_enable_mask 0x%x\n",
		 __func__, offset, chip->irq_enable);

	spin_lock_irqsave(&chip->gpio_lock[index], flags);

	chip->irq_enable &= ~BIT(offset);

	if (!chip->irq_enable) {
		/* Enable per channel interrupt */
		temp = xgpio_readreg(chip->regs + XGPIO_IPIER_OFFSET);
		val = offset - chip->gpio_width[0] + 1;
		if (val > 0)
			temp &= 1;
		else
			temp &= 2;
		xgpio_writereg(chip->regs + XGPIO_IPIER_OFFSET, temp);

		/* Disable global interrupt if channel interrupts are unused */
		temp = xgpio_readreg(chip->regs + XGPIO_IPIER_OFFSET);
		if (!temp)
			xgpio_writereg(chip->regs + XGPIO_GIER_OFFSET,
				       ~XGPIO_GIER_IE);
	}
	spin_unlock_irqrestore(&chip->gpio_lock[index], flags);
}

/**
 * xgpio_irq_unmask - Write the specified signal of the GPIO device.
 * @irq_data: per irq and chip data passed down to chip functions
 */
static void xgpio_irq_unmask(struct irq_data *irq_data)
{
	unsigned long flags;
	struct xgpio_instance *chip = irq_data_get_irq_chip_data(irq_data);
	u32 offset = irq_data->irq - chip->irq_base;
	u32 temp;
	s32 val;
	int index = xgpio_index(chip, 0);

	pr_debug("%s: Enable %d irq, irq_enable_mask 0x%x\n",
		 __func__, offset, chip->irq_enable);

	/* Setup pin as input */
	xgpio_dir_in(&chip->gc, offset);

	spin_lock_irqsave(&chip->gpio_lock[index], flags);

	chip->irq_enable |= BIT(offset);

	if (chip->irq_enable) {
		/* Enable per channel interrupt */
		temp = xgpio_readreg(chip->regs + XGPIO_IPIER_OFFSET);
		val = offset - (chip->gpio_width[0] - 1);
		if (val > 0)
			temp |= 2;
		else
			temp |= 1;
		xgpio_writereg(chip->regs + XGPIO_IPIER_OFFSET, temp);

		/* Enable global interrupts */
		xgpio_writereg(chip->regs + XGPIO_GIER_OFFSET, XGPIO_GIER_IE);
	}

	spin_unlock_irqrestore(&chip->gpio_lock[index], flags);
}

/**
 * xgpio_set_irq_type - Write the specified signal of the GPIO device.
 * @irq_data: Per irq and chip data passed down to chip functions
 * @type: Interrupt type that is to be set for the gpio pin
 *
 * Return:
 * 0 if interrupt type is supported otherwise otherwise -EINVAL
 */
static int xgpio_set_irq_type(struct irq_data *irq_data, unsigned int type)
{
	/* Only rising edge case is supported now */
	if (type & IRQ_TYPE_EDGE_RISING)
		return 0;

	return -EINVAL;
}

/* irq chip descriptor */
static struct irq_chip xgpio_irqchip = {
	.name           = "xgpio",
	.irq_mask       = xgpio_irq_mask,
	.irq_unmask     = xgpio_irq_unmask,
	.irq_set_type   = xgpio_set_irq_type,
};

/**
 * xgpio_to_irq - Find out gpio to Linux irq mapping
 * @gc: Pointer to gpio_chip device structure.
 * @offset: Gpio pin offset
 *
 * Return:
 * irq number otherwise -EINVAL
 */
static int xgpio_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct xgpio_instance *chip = gpiochip_get_data(gc);

	return irq_find_mapping(chip->irq_domain, offset);
}

/**
 * xgpio_irqhandler - Gpio interrupt service routine
 * @desc: Pointer to interrupt description
 */
static void xgpio_irqhandler(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct xgpio_instance *chip = (struct xgpio_instance *)
		irq_get_handler_data(irq);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	u32 offset, status, channel = 1;
	unsigned long val;

	chained_irq_enter(irqchip, desc);

	val = xgpio_readreg(chip->regs);
	if (!val) {
		channel = 2;
		val = xgpio_readreg(chip->regs + XGPIO_CHANNEL_OFFSET);
		val = val << chip->gpio_width[0];
	}

	/* Only rising edge is supported */
	val &= chip->irq_enable;
	for_each_set_bit(offset, &val, chip->gc.ngpio) {
		generic_handle_irq(chip->irq_base + offset);
	}

	status = xgpio_readreg(chip->regs + XGPIO_IPISR_OFFSET);
	xgpio_writereg(chip->regs + XGPIO_IPISR_OFFSET, channel);

	chained_irq_exit(irqchip, desc);
}

static struct lock_class_key gpio_lock_class;
static struct lock_class_key gpio_request_class;

/**
 * xgpio_irq_setup - Allocate irq for gpio and setup appropriate functions
 * @np: Device node of the GPIO chip
 * @chip: Pointer to private gpio channel structure
 *
 * Return:
 * 0 if success, otherwise -1
 */
static int xgpio_irq_setup(struct device_node *np, struct xgpio_instance *chip)
{
	u32 pin_num;
	struct resource res;
	int ret = of_irq_to_resource(np, 0, &res);

	if (ret <= 0) {
		pr_info("GPIO IRQ not connected\n");
		return 0;
	}

	chip->gc.to_irq = xgpio_to_irq;
	chip->irq_base = irq_alloc_descs(-1, 0, chip->gc.ngpio, 0);
	if (chip->irq_base < 0) {
		pr_err("Couldn't allocate IRQ numbers\n");
		return -1;
	}
	chip->irq_domain = irq_domain_add_legacy(np, chip->gc.ngpio,
						 chip->irq_base, 0,
						 &irq_domain_simple_ops, NULL);
	/*
	 * set the irq chip, handler and irq chip data for callbacks for
	 * each pin
	 */
	for (pin_num = 0; pin_num < chip->gc.ngpio; pin_num++) {
		u32 gpio_irq = irq_find_mapping(chip->irq_domain, pin_num);

		irq_set_lockdep_class(gpio_irq, &gpio_lock_class,
				      &gpio_request_class);
		pr_debug("IRQ Base: %d, Pin %d = IRQ %d\n",
			 chip->irq_base, pin_num, gpio_irq);
		irq_set_chip_and_handler(gpio_irq, &xgpio_irqchip,
					 handle_simple_irq);
		irq_set_chip_data(gpio_irq, (void *)chip);
	}
	irq_set_handler_data(res.start, (void *)chip);
	irq_set_chained_handler(res.start, xgpio_irqhandler);

	return 0;
}

/**
 * xgpio_of_probe - Probe method for the GPIO device.
 * @pdev: pointer to the platform device
 *
 * Return:
 * It returns 0, if the driver is bound to the GPIO device, or
 * a negative value if there is an error.
 */
static int xgpio_probe(struct platform_device *pdev)
{
	struct xgpio_instance *chip;
	int status = 0;
	struct device_node *np = pdev->dev.of_node;
	u32 is_dual;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	platform_set_drvdata(pdev, chip);

	/* Update GPIO state shadow register with default value */
	of_property_read_u32(np, "xlnx,dout-default", &chip->gpio_state[0]);

	/* Update GPIO direction shadow register with default value */
	if (of_property_read_u32(np, "xlnx,tri-default", &chip->gpio_dir[0]))
		chip->gpio_dir[0] = 0xFFFFFFFF;

	/*
	 * Check device node and parent device node for device width
	 * and assume default width of 32
	 */
	if (of_property_read_u32(np, "xlnx,gpio-width", &chip->gpio_width[0]))
		chip->gpio_width[0] = 32;

	spin_lock_init(&chip->gpio_lock[0]);

	if (of_property_read_u32(np, "xlnx,is-dual", &is_dual))
		is_dual = 0;

	if (is_dual) {
		/* Update GPIO state shadow register with default value */
		of_property_read_u32(np, "xlnx,dout-default-2",
				     &chip->gpio_state[1]);

		/* Update GPIO direction shadow register with default value */
		if (of_property_read_u32(np, "xlnx,tri-default-2",
					 &chip->gpio_dir[1]))
			chip->gpio_dir[1] = 0xFFFFFFFF;

		/*
		 * Check device node and parent device node for device width
		 * and assume default width of 32
		 */
		if (of_property_read_u32(np, "xlnx,gpio2-width",
					 &chip->gpio_width[1]))
			chip->gpio_width[1] = 32;

		spin_lock_init(&chip->gpio_lock[1]);
	}

	chip->gc.base = -1;
	chip->gc.ngpio = chip->gpio_width[0] + chip->gpio_width[1];
	chip->gc.parent = &pdev->dev;
	chip->gc.direction_input = xgpio_dir_in;
	chip->gc.direction_output = xgpio_dir_out;
	chip->gc.get = xgpio_get;
	chip->gc.set = xgpio_set;
	chip->gc.request = xgpio_request;
	chip->gc.free = xgpio_free;
	chip->gc.set_multiple = xgpio_set_multiple;

	chip->gc.label = dev_name(&pdev->dev);

	chip->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->regs)) {
		dev_err(&pdev->dev, "failed to ioremap memory resource\n");
		return PTR_ERR(chip->regs);
	}

	chip->clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(chip->clk)) {
		if (PTR_ERR(chip->clk) != -ENOENT) {
			if (PTR_ERR(chip->clk) != -EPROBE_DEFER)
				dev_err(&pdev->dev, "Input clock not found\n");
			return PTR_ERR(chip->clk);
		}
		/*
		 * Clock framework support is optional, continue on
		 * anyways if we don't find a matching clock.
		 */
		chip->clk = NULL;
	}
	status = clk_prepare_enable(chip->clk);
	if (status < 0) {
		dev_err(&pdev->dev, "Failed to prepare clk\n");
		return status;
	}
	pm_runtime_enable(&pdev->dev);
	status = pm_runtime_get_sync(&pdev->dev);
	if (status < 0)
		goto err_unprepare_clk;

	xgpio_save_regs(chip);

	status = devm_gpiochip_add_data(&pdev->dev, &chip->gc, chip);
	if (status) {
		dev_err(&pdev->dev, "failed to add GPIO chip\n");
		goto err_pm_put;
	}

	status = xgpio_irq_setup(np, chip);
	if (status) {
		pr_err("%s: GPIO IRQ initialization failed %d\n",
		       np->full_name, status);
		goto err_pm_put;
	}
	pr_info("XGpio: %s: registered, base is %d\n", np->full_name,
		chip->gc.base);

	pm_runtime_put(&pdev->dev);
	return 0;
err_pm_put:
	pm_runtime_put(&pdev->dev);
err_unprepare_clk:
	pm_runtime_disable(&pdev->dev);
	clk_unprepare(chip->clk);
	return status;
}

static const struct of_device_id xgpio_of_match[] = {
	{ .compatible = "xlnx,xps-gpio-1.00.a", },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, xgpio_of_match);

static struct platform_driver xgpio_plat_driver = {
	.probe		= xgpio_probe,
	.driver		= {
			.name = "gpio-xilinx",
			.of_match_table	= xgpio_of_match,
			.pm = &xgpio_dev_pm_ops,
	},
};

static int __init xgpio_init(void)
{
	return platform_driver_register(&xgpio_plat_driver);
}

subsys_initcall(xgpio_init);

static void __exit xgpio_exit(void)
{
	platform_driver_unregister(&xgpio_plat_driver);
}
module_exit(xgpio_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx GPIO driver");
MODULE_LICENSE("GPL");
