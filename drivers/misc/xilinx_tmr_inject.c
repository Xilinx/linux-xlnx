// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Xilinx TMR Inject IP.
 *
 * Copyright (C) 2022 Xilinx, Inc.
 *
 * Description:
 * This driver is developed for TMR Inject IP,The Triple Modular Redundancy(TMR)
 * Inject provides fault injection.
 * Fault injection and detection features are provided through sysfs entries
 * which allow the user to generate a fault.
 */

#include <asm/xilinx_mb_manager.h>
#include <linux/module.h>
#include <linux/of_device.h>

/* TMR Inject Register offsets */
#define XTMR_INJECT_CR_OFFSET		0x0
#define XTMR_INJECT_AIR_OFFSET		0x4
#define XTMR_INJECT_IIR_OFFSET		0xC
#define XTMR_INJECT_EAIR_OFFSET		0x10
#define XTMR_INJECT_ERR_OFFSET		0x204

/* Register Bitmasks/shifts */
#define XTMR_INJECT_CR_CPUID_SHIFT	8
#define XTMR_INJECT_CR_IE_SHIFT		10
#define XTMR_INJECT_IIR_ADDR_MASK	GENMASK(31, 16)

/**
 * struct xtmr_inject_dev - Driver data for TMR Inject
 * @regs: device physical base address
 * @dev: pointer to device struct
 * @cr_val: control register value
 * @magic: Magic hardware configuration value
 * @err_cnt: error statistics count
 */
struct xtmr_inject_dev {
	void __iomem *regs;
	struct device *dev;
	u32 cr_val;
	u32 magic;
	u32 err_cnt;
};

/* IO accessors */
static inline void xtmr_inject_write(struct xtmr_inject_dev *xtmr_inject, u32 addr,
				     u32 value)
{
	iowrite32(value, xtmr_inject->regs + addr);
}

static inline u32 xtmr_inject_read(struct xtmr_inject_dev *xtmr_inject, u32 addr)
{
	return ioread32(xtmr_inject->regs + addr);
}

static ssize_t inject_err_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t size)
{
	int ret;
	long value;

	ret = kstrtol(buf, 16, &value);
	if (ret)
		return ret;

	if (value > 1)
		return -EINVAL;

	xmb_inject_err();

	return size;
}
static DEVICE_ATTR_WO(inject_err);

static ssize_t inject_cpuid_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct xtmr_inject_dev *xtmr_inject = dev_get_drvdata(dev);
	int ret;
	long value;

	ret = kstrtol(buf, 0, &value);
	if (ret)
		return ret;

	if (value > 3)
		return -EINVAL;

	xtmr_inject->cr_val |= (value << XTMR_INJECT_CR_CPUID_SHIFT);
	xtmr_inject_write(xtmr_inject, XTMR_INJECT_CR_OFFSET,
			  xtmr_inject->cr_val);

	return size;
}
static DEVICE_ATTR_WO(inject_cpuid);

static struct attribute *xtmr_inject_attrs[] = {
	&dev_attr_inject_err.attr,
	&dev_attr_inject_cpuid.attr,
	NULL,
};
ATTRIBUTE_GROUPS(xtmr_inject);

static void xtmr_inject_init(struct xtmr_inject_dev *xtmr_inject)
{
	/* Allow fault injection */
	xtmr_inject->cr_val = xtmr_inject->magic |
				(1 << XTMR_INJECT_CR_IE_SHIFT) |
				(1 << XTMR_INJECT_CR_CPUID_SHIFT);
	xtmr_inject_write(xtmr_inject, XTMR_INJECT_CR_OFFSET,
			  xtmr_inject->cr_val);
	/* Initialize the address inject and instruction inject registers */
	xtmr_inject_write(xtmr_inject, XTMR_INJECT_AIR_OFFSET,
			  XMB_INJECT_ERR_OFFSET);
	xtmr_inject_write(xtmr_inject, XTMR_INJECT_IIR_OFFSET,
			  XMB_INJECT_ERR_OFFSET & XTMR_INJECT_IIR_ADDR_MASK);
}

/**
 * xtmr_inject_probe - Driver probe function
 * @pdev: Pointer to the platform_device structure
 *
 * This is the driver probe routine. It does all the memory
 * allocation and creates sysfs entries for the device.
 *
 * Return: 0 on success and failure value on error
 */
static int xtmr_inject_probe(struct platform_device *pdev)
{
	struct xtmr_inject_dev *xtmr_inject;
	struct resource *res;
	int err;

	xtmr_inject = devm_kzalloc(&pdev->dev, sizeof(*xtmr_inject), GFP_KERNEL);
	if (!xtmr_inject)
		return -ENOMEM;

	xtmr_inject->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xtmr_inject->regs = devm_ioremap_resource(xtmr_inject->dev, res);
	if (IS_ERR(xtmr_inject->regs))
		return PTR_ERR(xtmr_inject->regs);

	err = of_property_read_u32(pdev->dev.of_node, "xlnx,magic",
				   &xtmr_inject->magic);
	if (err < 0) {
		dev_err(&pdev->dev, "unable to read xlnx,magic property");
		return err;
	}

	/* Initialize TMR Inject */
	xtmr_inject_init(xtmr_inject);

	err = sysfs_create_groups(&xtmr_inject->dev->kobj, xtmr_inject_groups);
	if (err < 0) {
		dev_err(&pdev->dev, "unable to create sysfs entries\n");
		return err;
	}

	platform_set_drvdata(pdev, xtmr_inject);

	return 0;
}

static int xtmr_inject_remove(struct platform_device *pdev)
{
	sysfs_remove_groups(&pdev->dev.kobj, xtmr_inject_groups);

	return 0;
}

static const struct of_device_id xtmr_inject_of_match[] = {
	{
		.compatible = "xlnx,tmr-inject-1.0",
	},
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, xtmr_inject_of_match);

static struct platform_driver xtmr_inject_driver = {
	.driver = {
		.name = "xilinx-tmr_inject",
		.of_match_table = xtmr_inject_of_match,
	},
	.probe = xtmr_inject_probe,
	.remove = xtmr_inject_remove,
};
module_platform_driver(xtmr_inject_driver);
MODULE_AUTHOR("Xilinx, Inc");
MODULE_DESCRIPTION("Xilinx TMR Inject Driver");
MODULE_LICENSE("GPL");
