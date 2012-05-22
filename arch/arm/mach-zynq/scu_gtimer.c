/*
 * Xilinx SCU Global Timer driver
 *
 * The only purpose for this driver is to create sysfs attributes under the 
 * driver that allow all the registers of the SCU Global timer to be read and
 * written from user space easily.
 *
 * The attributes will be visible in the /sys/devices/platform/xscugtimer.0
 * and this driver is a prototype to see if it really meets the needs.
 *
 * The counter and compare registers are provided as 32 bit attributes which 
 * map to the hardware registers and as 64 bit attributes for easier use.
 *
 * Copyright (c) 2011 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 * 02139, USA.
 */

#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <mach/zynq_soc.h>

#define DRIVER_NAME "xscugtimer"

#define XSCUGTIMER_COUNTER0_OFFSET		0x00 /* Control Register */
#define XSCUGTIMER_COUNTER1_OFFSET		0x04 /* Control Register */
#define XSCUGTIMER_CONTROL_OFFSET		0x08 /* Control Register */
#define XSCUGTIMER_IRQ_STATUS_OFFSET		0x0C /* Control Register */
#define XSCUGTIMER_COMPARE0_OFFSET		0x10 /* Control Register */
#define XSCUGTIMER_COMPARE1_OFFSET		0x14 /* Control Register */
#define XSCUGTIMER_AUTOINCR_OFFSET		0x18 /* Control Register */

/************ Constant Definitions *************/

/* There can only ever be one instance of this device since there is only 
 * one global timer in one SCU. No spin lock is being used as there is no
 * read modify writes happening at this point since it's real simple.
 */
static void __iomem *base_address;

/*
 * Register read/write access routines
 */
#define xscugtimer_writereg(offset, val)	__raw_writel(val, base_address + offset)
#define xscugtimer_readreg(offset)		__raw_readl(base_address + offset)

/* For now, the following macro is used to generate unique functions for each 
 * register in the SCU global timer that correlate to sysfs attributes. This 
 * should be able to be done without the macro by using the attr input in a 
 * function but it was not working. This is a bit ugly as a get and set function
 * is generated for each attribute.
 */

/**
 * xscugtimer_set_xxxx_reg() - This function sets a register in the timer
 * 	with the given value.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	negative error if the string could not be converted
 *		or the size of the buffer.
 *
 **/
/**
 * xscugtimer_get_xxxx_reg() - The function returns the value read from the timer
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	Size of the buffer.
 *
 **/
#define xscugtimer_config_attr(name,offset)				\
static ssize_t xscugtimer_set_##name##_reg(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t size)	\
{									\
	unsigned long reg;						\
	int status;							\
									\
	status = strict_strtoul(buf, 16, &reg);				\
	if (status)							\
		return status;						\
									\
	xscugtimer_writereg(offset, reg);				\
	return size;							\
}									\
static ssize_t xscugtimer_get_##name##_reg(struct device *dev,		\
	struct device_attribute *attr,					\
	char *buf)							\
{									\
	u32 reg;							\
	int status;							\
									\
	reg = xscugtimer_readreg(offset); 				\
	status = sprintf(buf, "%X\n", reg);				\
									\
	return status;							\
}									\
static DEVICE_ATTR(name, 0644, xscugtimer_get_##name##_reg,		\
				xscugtimer_set_##name##_reg);

/**
 * xscugtimer_set64_xxxx_reg() - This function sets a register in the timer
 * 	with the given 64 bit value.
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * @size:	The number of bytes used from the buffer
 * returns:	negative error if the string could not be converted
 *		or the size of the buffer.
 *
 **/
/**
 * xscugtimer_get64_xxxx_reg() - The function returns the 64 bit value read 
 * 	from the timer register
 * @dev:	Pointer to the device structure.
 * @attr:	Pointer to the device attribute structure.
 * @buf:	Pointer to the buffer location for the configuration
 *		data.
 * returns:	Size of the buffer.
 *
 **/
#define xscugtimer_config_attr64(name,offset)				\
static ssize_t xscugtimer_set_##name##64_reg(struct device *dev,	\
				     struct device_attribute *attr,	\
				     const char *buf, size_t size)	\
{									\
	unsigned long long reg;						\
	int status;							\
									\
	status = strict_strtoull(buf, 16, &reg);			\
	if (status)							\
		return status;						\
									\
	xscugtimer_writereg(offset + 4, (reg >> 32));			\
	xscugtimer_writereg(offset, reg & 0xFFFFFFFF);			\
	return size;							\
}									\
static ssize_t xscugtimer_get_##name##64_reg(struct device *dev,	\
	struct device_attribute *attr,					\
	char *buf)							\
{									\
	unsigned long long reg;						\
	int status;							\
									\
	reg = (((unsigned long long)xscugtimer_readreg(offset + 4) << 32) | \
			xscugtimer_readreg(offset)); 			\
	status = sprintf(buf, "%llX\n", reg);			\
									\
	return status;							\
}									\
static DEVICE_ATTR(name, 0644, xscugtimer_get_##name##64_reg,		\
				xscugtimer_set_##name##64_reg);


/* create the sysfs attributes for each SCU global timer register, the
 * counter and compare registers are provided as 32 attributes which map
 * to the hardware and 64 bit attributes for easier use
 */
xscugtimer_config_attr64(counter, XSCUGTIMER_COUNTER0_OFFSET);
xscugtimer_config_attr(counter0, XSCUGTIMER_COUNTER0_OFFSET);
xscugtimer_config_attr(counter1, XSCUGTIMER_COUNTER1_OFFSET);
xscugtimer_config_attr(control, XSCUGTIMER_CONTROL_OFFSET);	
xscugtimer_config_attr(irq_status, XSCUGTIMER_IRQ_STATUS_OFFSET);
xscugtimer_config_attr64(compare, XSCUGTIMER_COMPARE0_OFFSET);
xscugtimer_config_attr(compare0, XSCUGTIMER_COMPARE0_OFFSET);
xscugtimer_config_attr(compare1, XSCUGTIMER_COMPARE1_OFFSET);
xscugtimer_config_attr(autoincr, XSCUGTIMER_AUTOINCR_OFFSET);

static const struct attribute *xscugtimer_attrs[] = {
	&dev_attr_counter.attr, 
	&dev_attr_counter0.attr, 
	&dev_attr_counter1.attr, 
	&dev_attr_control.attr,
	&dev_attr_irq_status.attr,
	&dev_attr_compare.attr,
	&dev_attr_compare0.attr,
	&dev_attr_compare1.attr,
	&dev_attr_autoincr.attr,
	NULL,
};

static const struct attribute_group xscugtimer_attr_group = {
	.attrs = (struct attribute **) xscugtimer_attrs,
};

/**
 * xscugtimer_drv_probe -  Probe call for the device.
 *
 * @pdev:	handle to the platform device structure.
 *
 * It does all the memory allocation and registration for the device.
 * Returns 0 on success, negative error otherwise.
 **/
static int __devinit xscugtimer_drv_probe(struct platform_device *pdev)
{
	struct resource regs_res;
	int retval = -ENOMEM;

	regs_res.start = (int)SCU_GLOBAL_TIMER_BASE;
	regs_res.end = (int)SCU_GLOBAL_TIMER_BASE + 0x3FF;

	if (!request_mem_region(regs_res.start,
					regs_res.end - regs_res.start + 1,
					DRIVER_NAME)) {
		dev_err(&pdev->dev, "Couldn't lock memory region at %Lx\n",
			(unsigned long long) regs_res.start);
		retval = -EBUSY;
		goto failed0;
	}

	base_address = ioremap(regs_res.start,
				(regs_res.end - regs_res.start + 1));
	if (!base_address) {
		dev_err(&pdev->dev, "ioremap() failed\n");
		goto failed1;
	}

	dev_info(&pdev->dev, "ioremap %llx to %p with size %llx\n",
		 (unsigned long long) regs_res.start,
		 base_address,
		 (unsigned long long) (regs_res.end - regs_res.start + 1));


	/* create sysfs files for the device */
	retval = sysfs_create_group(&(pdev->dev.kobj), &xscugtimer_attr_group);
	if (retval) {
		dev_err(&pdev->dev, "Failed to create sysfs attr group\n");
		goto failed2;
	}

	return 0;		/* Success */

 failed2:
	iounmap(base_address);

 failed1:
	release_mem_region(regs_res.start,
				regs_res.end - regs_res.start + 1);
 failed0:

	return retval;
}

/**
 * xscugtimer_drv_remove -  Remove call for the device.
 *
 * @pdev:	handle to the platform device structure.
 *
 * Unregister the device after releasing the resources.
 * Returns 0 or error status.
 **/
static int __devexit xscugtimer_drv_remove(struct platform_device *pdev)
{
	struct xscugtimer_drvdata *drvdata;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	drvdata = (struct xscugtimer_drvdata *)dev_get_drvdata(&pdev->dev);

	if (!drvdata)
		return -ENODEV;


	sysfs_remove_group(&pdev->dev.kobj, &xscugtimer_attr_group);


	iounmap(base_address);
	release_mem_region(res->start, res->end - res->start + 1);

	return 0;		/* Success */
}

/* Driver Structure */
static struct platform_driver xscugtimer_platform_driver = {
	.probe = xscugtimer_drv_probe,
	.remove = __devexit_p(xscugtimer_drv_remove),
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
	},
};

struct platform_device xilinx_scutimer_device = {
	.name = "xscugtimer",
	.dev.platform_data = NULL,
};

/**
 * xscugtimer_module_init -  register the Device Configuration.
 *
 * Returns 0 on success, otherwise negative error.
 */
static int __init xscugtimer_init(void)
{
	platform_device_register(&xilinx_scutimer_device);
	return platform_driver_register(&xscugtimer_platform_driver);
}

/**
 * xscugtimer_module_exit -  Unregister the Device Configuration.
 */
static void __exit xscugtimer_exit(void)
{
	platform_driver_unregister(&xscugtimer_platform_driver);

}

/* This driver is assumed to be in the BSP and started up all the time. */

device_initcall(xscugtimer_init);
