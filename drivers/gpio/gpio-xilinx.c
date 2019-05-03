/*
 * Xilinx gpio driver for xps/axi_gpio IP.
 *
 * Copyright 2008 - 2013 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>

/* Register Offset Definitions */
#define XGPIO_DATA_OFFSET	0x0 /* Data register */
#define XGPIO_TRI_OFFSET	0x4 /* I/O direction register */
#define XGPIO_GIER_OFFSET	0x11c /* Global Interrupt Enable */
#define XGPIO_GIER_IE		BIT(31)

#define XGPIO_IPISR_OFFSET	0x120 /* IP Interrupt Status */
#define XGPIO_IPIER_OFFSET	0x128 /* IP Interrupt Enable */

#define XGPIO_CHANNEL_OFFSET	0x8

/* Read/Write access to the GPIO registers */
#if defined(CONFIG_ARCH_ZYNQ) || defined(CONFIG_ARM64)
# define xgpio_readreg(offset)		readl(offset)
# define xgpio_writereg(offset, val)	writel(val, offset)
#else
# define xgpio_readreg(offset)		__raw_readl(offset)
# define xgpio_writereg(offset, val)	__raw_writel(val, offset)
#endif

/**
 * struct xgpio_instance - Stores information about GPIO device
 * @mmchip: OF GPIO chip for memory mapped banks
 * @mmchip_dual: Pointer to the OF dual gpio chip
 * @gpio_state: GPIO state shadow register
 * @gpio_dir: GPIO direction shadow register
 * @offset: GPIO channel offset
 * @irq_base: GPIO channel irq base address
 * @irq_enable: GPIO irq enable/disable bitfield
 * @no_init: No intitialisation at probe
 * @gpio_lock: Lock used for synchronization
 * @irq_domain: irq_domain of the controller
 * @clk: clock resource for this driver
 */
struct xgpio_instance {
	struct of_mm_gpio_chip mmchip;
	struct of_mm_gpio_chip *mmchip_dual;
	u32 gpio_state;
	u32 gpio_dir;
	u32 offset;
	int irq_base;
	u32 irq_enable;
	bool no_init;
	spinlock_t gpio_lock;
	struct irq_domain *irq_domain;
	struct clk *clk;
};

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
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);

	void __iomem *regs = mm_gc->regs + chip->offset;

	return !!(xgpio_readreg(regs + XGPIO_DATA_OFFSET) & BIT(gpio));
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
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);
	void __iomem *regs = mm_gc->regs;

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Write to GPIO signal and set its direction to output */
	if (val)
		chip->gpio_state |= BIT(gpio);
	else
		chip->gpio_state &= ~BIT(gpio);

	xgpio_writereg(regs + chip->offset + XGPIO_DATA_OFFSET,
							 chip->gpio_state);

	spin_unlock_irqrestore(&chip->gpio_lock, flags);
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
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);
	void __iomem *regs = mm_gc->regs;
	int i;

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Write to GPIO signals */
	for (i = 0; i < gc->ngpio; i++) {
		if (*mask == 0)
			break;
		if (__test_and_clear_bit(i, mask)) {
			if (test_bit(i, bits))
				chip->gpio_state |= BIT(i);
			else
				chip->gpio_state &= ~BIT(i);
		}
	}

	xgpio_writereg(regs + chip->offset + XGPIO_DATA_OFFSET,
		       chip->gpio_state);

	spin_unlock_irqrestore(&chip->gpio_lock, flags);
}

/**
 * xgpio_dir_in - Set the direction of the specified GPIO signal as input.
 * @gc:     Pointer to gpio_chip device structure.
 * @gpio:   GPIO signal number.
 *
 * This function sets the direction of specified GPIO signal as input.
 *
 * Return:
 * 0 - if direction of GPIO signals is set as input
 * otherwise it returns negative error value.
 */
static int xgpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	unsigned long flags;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);
	void __iomem *regs = mm_gc->regs;

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Set the GPIO bit in shadow register and set direction as input */
	chip->gpio_dir |= BIT(gpio);
	xgpio_writereg(regs + chip->offset + XGPIO_TRI_OFFSET, chip->gpio_dir);

	spin_unlock_irqrestore(&chip->gpio_lock, flags);

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
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);
	void __iomem *regs = mm_gc->regs;

	spin_lock_irqsave(&chip->gpio_lock, flags);

	/* Write state of GPIO signal */
	if (val)
		chip->gpio_state |= BIT(gpio);
	else
		chip->gpio_state &= ~BIT(gpio);
	xgpio_writereg(regs + chip->offset + XGPIO_DATA_OFFSET,
		       chip->gpio_state);

	/* Clear the GPIO bit in shadow register and set direction as output */
	chip->gpio_dir &= ~BIT(gpio);
	xgpio_writereg(regs + chip->offset + XGPIO_TRI_OFFSET, chip->gpio_dir);

	spin_unlock_irqrestore(&chip->gpio_lock, flags);

	return 0;
}

/**
 * xgpio_save_regs - Set initial values of GPIO pins
 * @mm_gc: Pointer to memory mapped GPIO chip structure
 */
static void xgpio_save_regs(struct of_mm_gpio_chip *mm_gc)
{
	struct xgpio_instance *chip =
	    container_of(mm_gc, struct xgpio_instance, mmchip);
	if (chip->no_init) {
		chip->gpio_state = xgpio_readreg(mm_gc->regs +
						 XGPIO_DATA_OFFSET);
		chip->gpio_dir = xgpio_readreg(mm_gc->regs + XGPIO_TRI_OFFSET);
	} else {
		xgpio_writereg(mm_gc->regs + chip->offset + XGPIO_DATA_OFFSET,
			       chip->gpio_state);
		xgpio_writereg(mm_gc->regs + chip->offset + XGPIO_TRI_OFFSET,
			       chip->gpio_dir);
	}
}

/**
 * xgpio_xlate - Translate gpio_spec to the GPIO number and flags
 * @gc: Pointer to gpio_chip device structure.
 * @gpiospec:  gpio specifier as found in the device tree
 * @flags: A flags pointer based on binding
 *
 * Return:
 * irq number otherwise -EINVAL
 */
static int xgpio_xlate(struct gpio_chip *gc,
		       const struct of_phandle_args *gpiospec, u32 *flags)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip = container_of(mm_gc, struct xgpio_instance,
						   mmchip);
	if (gc->of_gpio_n_cells == 3 && flags)
		*flags = gpiospec->args[2];

	if (gpiospec->args[1] == chip->offset)
		return gpiospec->args[0];

	return -EINVAL;
}

/**
 * xgpio_irq_mask - Write the specified signal of the GPIO device.
 * @irq_data: per irq and chip data passed down to chip functions
 */
static void xgpio_irq_mask(struct irq_data *irq_data)
{
	unsigned long flags;
	struct xgpio_instance *chip = irq_data_get_irq_chip_data(irq_data);
	struct of_mm_gpio_chip *mm_gc = &chip->mmchip;
	u32 offset = irq_data->irq - chip->irq_base;
	u32 temp;

	pr_debug("%s: Disable %d irq, irq_enable_mask 0x%x\n",
		__func__, offset, chip->irq_enable);

	spin_lock_irqsave(&chip->gpio_lock, flags);

	chip->irq_enable &= ~BIT(offset);

	if (!chip->irq_enable) {
		/* Enable per channel interrupt */
		temp = xgpio_readreg(mm_gc->regs + XGPIO_IPIER_OFFSET);
		temp &= chip->offset / XGPIO_CHANNEL_OFFSET + 1;
		xgpio_writereg(mm_gc->regs + XGPIO_IPIER_OFFSET, temp);

		/* Disable global interrupt if channel interrupts are unused */
		temp = xgpio_readreg(mm_gc->regs + XGPIO_IPIER_OFFSET);
		if (!temp)
			xgpio_writereg(mm_gc->regs + XGPIO_GIER_OFFSET,
				       ~XGPIO_GIER_IE);

	}
	spin_unlock_irqrestore(&chip->gpio_lock, flags);
}

/**
 * xgpio_irq_unmask - Write the specified signal of the GPIO device.
 * @irq_data: per irq and chip data passed down to chip functions
 */
static void xgpio_irq_unmask(struct irq_data *irq_data)
{
	unsigned long flags;
	struct xgpio_instance *chip = irq_data_get_irq_chip_data(irq_data);
	struct of_mm_gpio_chip *mm_gc = &chip->mmchip;
	u32 offset = irq_data->irq - chip->irq_base;
	u32 temp;

	pr_debug("%s: Enable %d irq, irq_enable_mask 0x%x\n",
		__func__, offset, chip->irq_enable);

	/* Setup pin as input */
	xgpio_dir_in(&mm_gc->gc, offset);

	spin_lock_irqsave(&chip->gpio_lock, flags);

	chip->irq_enable |= BIT(offset);

	if (chip->irq_enable) {

		/* Enable per channel interrupt */
		temp = xgpio_readreg(mm_gc->regs + XGPIO_IPIER_OFFSET);
		temp |= chip->offset / XGPIO_CHANNEL_OFFSET + 1;
		xgpio_writereg(mm_gc->regs + XGPIO_IPIER_OFFSET, temp);

		/* Enable global interrupts */
		xgpio_writereg(mm_gc->regs + XGPIO_GIER_OFFSET, XGPIO_GIER_IE);
	}

	spin_unlock_irqrestore(&chip->gpio_lock, flags);
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
	.name		= "xgpio",
	.irq_mask	= xgpio_irq_mask,
	.irq_unmask	= xgpio_irq_unmask,
	.irq_set_type	= xgpio_set_irq_type,
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
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct xgpio_instance *chip = container_of(mm_gc, struct xgpio_instance,
						   mmchip);

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
	struct of_mm_gpio_chip *mm_gc = &chip->mmchip;
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	int offset;
	unsigned long val;

	chained_irq_enter(irqchip, desc);

	val = xgpio_readreg(mm_gc->regs + chip->offset);
	/* Only rising edge is supported */
	val &= chip->irq_enable;

	for_each_set_bit(offset, &val, chip->mmchip.gc.ngpio) {
		generic_handle_irq(chip->irq_base + offset);
	}

	xgpio_writereg(mm_gc->regs + XGPIO_IPISR_OFFSET,
		       chip->offset / XGPIO_CHANNEL_OFFSET + 1);

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

	chip->mmchip.gc.to_irq = xgpio_to_irq;

	chip->irq_base = irq_alloc_descs(-1, 0, chip->mmchip.gc.ngpio, 0);
	if (chip->irq_base < 0) {
		pr_err("Couldn't allocate IRQ numbers\n");
		return -1;
	}
	chip->irq_domain = irq_domain_add_legacy(np, chip->mmchip.gc.ngpio,
						 chip->irq_base, 0,
						 &irq_domain_simple_ops, NULL);

	/*
	 * set the irq chip, handler and irq chip data for callbacks for
	 * each pin
	 */
	for (pin_num = 0; pin_num < chip->mmchip.gc.ngpio; pin_num++) {
		u32 gpio_irq = irq_find_mapping(chip->irq_domain, pin_num);

		irq_set_lockdep_class(gpio_irq, &gpio_lock_class,
				      &gpio_request_class);
		pr_debug("IRQ Base: %d, Pin %d = IRQ %d\n",
			chip->irq_base,	pin_num, gpio_irq);
		irq_set_chip_and_handler(gpio_irq, &xgpio_irqchip,
					 handle_simple_irq);
		irq_set_chip_data(gpio_irq, (void *)chip);
	}
	irq_set_handler_data(res.start, (void *)chip);
	irq_set_chained_handler(res.start, xgpio_irqhandler);

	return 0;
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
	int irq;
	struct irq_data *data;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_dbg(dev, "failed to get IRQ\n");
		return 0;
	}

	data = irq_get_irq_data(irq);
	if (!irqd_is_wakeup_set(data))
		return pm_runtime_force_suspend(dev);

	return 0;
}

static int __maybe_unused xgpio_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int irq;
	struct irq_data *data;


	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_dbg(dev, "failed to get IRQ\n");
		return 0;
	}

	data = irq_get_irq_data(irq);
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
 * xgpio_remove - Remove method for the GPIO device.
 * @pdev: pointer to the platform device
 *
 * This function remove gpiochips and frees all the allocated resources.
 *
 * Return: 0 always
 */
static int xgpio_remove(struct platform_device *pdev)
{
	struct xgpio_instance *chip = platform_get_drvdata(pdev);

	of_mm_gpiochip_remove(&chip->mmchip);
	if (chip->mmchip_dual)
		of_mm_gpiochip_remove(chip->mmchip_dual);
	if (!pm_runtime_suspended(&pdev->dev))
		clk_disable(chip->clk);
	clk_unprepare(chip->clk);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

/**
 * xgpio_of_probe - Probe method for the GPIO device.
 * @pdev:       platform device instance
 *
 * This function probes the GPIO device in the device tree. It initializes the
 * driver data structure.
 *
 * Return:
 * It returns 0, if the driver is bound to the GPIO device, or
 * a negative value if there is an error.
 */
static int xgpio_of_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct xgpio_instance *chip, *chip_dual;
	int status = 0;
	const u32 *tree_info;
	u32 ngpio;
	u32 cells = 2;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	/* Update GPIO state shadow register with default value */
	of_property_read_u32(np, "xlnx,dout-default", &chip->gpio_state);

	/* By default, all pins are inputs */
	chip->gpio_dir = 0xFFFFFFFF;

	/* Update GPIO direction shadow register with default value */
	of_property_read_u32(np, "xlnx,tri-default", &chip->gpio_dir);

	chip->no_init = of_property_read_bool(np, "xlnx,no-init");

	/* Update cells with gpio-cells value */
	of_property_read_u32(np, "#gpio-cells", &cells);

	/*
	 * Check device node and parent device node for device width
	 * and assume default width of 32
	 */
	if (of_property_read_u32(np, "xlnx,gpio-width", &ngpio))
		ngpio = 32;
	chip->mmchip.gc.ngpio = (u16)ngpio;

	spin_lock_init(&chip->gpio_lock);

	chip->mmchip.gc.parent = &pdev->dev;
	chip->mmchip.gc.owner = THIS_MODULE;
	chip->mmchip.gc.of_xlate = xgpio_xlate;
	chip->mmchip.gc.of_gpio_n_cells = cells;
	chip->mmchip.gc.direction_input = xgpio_dir_in;
	chip->mmchip.gc.direction_output = xgpio_dir_out;
	chip->mmchip.gc.get = xgpio_get;
	chip->mmchip.gc.set = xgpio_set;
	chip->mmchip.gc.request = xgpio_request;
	chip->mmchip.gc.free = xgpio_free;
	chip->mmchip.gc.set_multiple = xgpio_set_multiple;

	chip->mmchip.save_regs = xgpio_save_regs;

	platform_set_drvdata(pdev, chip);

	chip->clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(chip->clk)) {
		if ((PTR_ERR(chip->clk) != -ENOENT) ||
				(PTR_ERR(chip->clk) != -EPROBE_DEFER)) {
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

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	/* Call the OF gpio helper to setup and register the GPIO device */
	status = of_mm_gpiochip_add(np, &chip->mmchip);
	if (status) {
		pr_err("%pOF: error in probe function with status %d\n",
		       np, status);
		goto err_unprepare_clk;
	}

	status = xgpio_irq_setup(np, chip);
	if (status) {
		pr_err("%s: GPIO IRQ initialization failed %d\n",
		       np->full_name, status);
		goto err_pm_put;
	}

	pr_info("XGpio: %s: registered, base is %d\n", np->full_name,
							chip->mmchip.gc.base);

	tree_info = of_get_property(np, "xlnx,is-dual", NULL);
	if (tree_info && be32_to_cpup(tree_info)) {
		chip_dual = devm_kzalloc(&pdev->dev, sizeof(*chip_dual),
					 GFP_KERNEL);
		if (!chip_dual)
			goto err_pm_put;

		/* Add dual channel offset */
		chip_dual->offset = XGPIO_CHANNEL_OFFSET;

		/* Update GPIO state shadow register with default value */
		of_property_read_u32(np, "xlnx,dout-default-2",
				     &chip_dual->gpio_state);

		/* By default, all pins are inputs */
		chip_dual->gpio_dir = 0xFFFFFFFF;

		/* Update GPIO direction shadow register with default value */
		of_property_read_u32(np, "xlnx,tri-default-2",
				     &chip_dual->gpio_dir);

		/*
		 * Check device node and parent device node for device width
		 * and assume default width of 32
		 */
		if (of_property_read_u32(np, "xlnx,gpio2-width", &ngpio))
			ngpio = 32;
		chip_dual->mmchip.gc.ngpio = (u16)ngpio;

		spin_lock_init(&chip_dual->gpio_lock);

		chip_dual->mmchip.gc.parent = &pdev->dev;
		chip_dual->mmchip.gc.owner = THIS_MODULE;
		chip_dual->mmchip.gc.of_xlate = xgpio_xlate;
		chip_dual->mmchip.gc.of_gpio_n_cells = cells;
		chip_dual->mmchip.gc.direction_input = xgpio_dir_in;
		chip_dual->mmchip.gc.direction_output = xgpio_dir_out;
		chip_dual->mmchip.gc.get = xgpio_get;
		chip_dual->mmchip.gc.set = xgpio_set;
		chip_dual->mmchip.gc.request = xgpio_request;
		chip_dual->mmchip.gc.free = xgpio_free;
		chip_dual->mmchip.gc.set_multiple = xgpio_set_multiple;

		chip_dual->mmchip.save_regs = xgpio_save_regs;

		chip->mmchip_dual = &chip_dual->mmchip;

		status = xgpio_irq_setup(np, chip_dual);
		if (status) {
			pr_err("%s: GPIO IRQ initialization failed %d\n",
			      np->full_name, status);
			goto err_pm_put;
		}

		/* Call the OF gpio helper to setup and register the GPIO dev */
		status = of_mm_gpiochip_add(np, &chip_dual->mmchip);
		if (status) {
			pr_err("%s: error in probe function with status %d\n",
			       np->full_name, status);
			goto err_pm_put;
		}
		pr_info("XGpio: %s: dual channel registered, base is %d\n",
			np->full_name, chip_dual->mmchip.gc.base);
	}

	pm_runtime_put(&pdev->dev);
	return 0;

err_pm_put:
	pm_runtime_put(&pdev->dev);
err_unprepare_clk:
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(chip->clk);
	return status;
}

static const struct of_device_id xgpio_of_match[] = {
	{ .compatible = "xlnx,xps-gpio-1.00.a", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, xgpio_of_match);

static struct platform_driver xilinx_gpio_driver = {
	.probe = xgpio_of_probe,
	.remove = xgpio_remove,
	.driver = {
		.name = "xilinx-gpio",
		.of_match_table = xgpio_of_match,
		.pm = &xgpio_dev_pm_ops,
	},
};

static int __init xgpio_init(void)
{
	return platform_driver_register(&xilinx_gpio_driver);
}

/* Make sure we get initialized before anyone else tries to use us */
subsys_initcall(xgpio_init);

static void __exit xgpio_exit(void)
{
	platform_driver_unregister(&xilinx_gpio_driver);
}
module_exit(xgpio_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx GPIO driver");
MODULE_LICENSE("GPL");
