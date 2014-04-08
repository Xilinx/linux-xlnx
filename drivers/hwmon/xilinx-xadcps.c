/*
 * drivers/hwmon/xilinx-xadcps.c - Xilinx Zynq XADC support
 *
 * Copyright (c) 2012 Wind River Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/io.h>
#include <linux/spinlock.h>

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

/* XADC interface register offsets */
#define XADC_CONFIG	0x00
#define XADC_INTSTS	0x04
#define XADC_INTMSK	0x08
#define XADC_STATUS	0x0C
#define XADC_CFIFO	0x10
#define XADC_DFIFO	0x14
#define XADC_CTL	0x18

/* XADC interface register fields */
#define XADC_CONFIG_ENABLE		(1 << 31)
#define XADC_CONFIG_CFIFOTH_MSK		0xF
#define XADC_CONFIG_CFIFOTH_SHIFT	20
#define XADC_CONFIG_DFIFOTH_MSK		0xF
#define XADC_CONFIG_DFIFOTH_SHIFT	16
#define XADC_CONFIG_WEDGE		(1 << 13)
#define XADC_CONFIG_REDGE		(1 << 12)
#define XADC_CONFIG_TCKRATE_MSK		0x3
#define XADC_CONFIG_TCKRATE_SHIFT	8
#define XADC_CONFIG_IGAP_MSK		0x1F
#define XADC_CONFIG_IGAP_SHIFT		0

#define TCKRATE_DIV16			3

#define XADC_INT_CFIFO_LTH		(1 << 9)
#define XADC_INT_DFIFO_GTH		(1 << 8)

#define XADC_STATUS_CFIFO_LVL_MSK	0xF
#define XADC_STATUS_CFIFO_LVL_SHIFT	16
#define XADC_STATUS_DFIFO_EMPTY		(1 << 8)

#define XADC_FIFO_CMD_MSK		0xF
#define XADC_FIFO_CMD_SHIFT		26
#define XADC_FIFO_ADDR_MSK		0x3FF
#define XADC_FIFO_ADDR_SHIFT		16
#define XADC_FIFO_DATA_MSK		0xFFFF
#define XADC_FIFO_DATA_SHIFT		0

/* XADC commands */
#define XADC_CMD_NOP			0
#define XADC_CMD_READ			1
#define XADC_CMD_WRITE			2

/* XADC register offsets */
#define REG_TEMP		0x00
#define REG_VCCINT		0x01
#define REG_VCCAUX		0x02
#define REG_VPVN		0x03
#define REG_VCCBRAM		0x06

#define REG_MAX_TEMP		0x20
#define REG_MAX_VCCINT		0x21
#define REG_MAX_VCCAUX		0x22
#define REG_MAX_VCCBRAM		0x23
#define REG_MIN_TEMP		0x24
#define REG_MIN_VCCINT		0x25
#define REG_MIN_VCCAUX		0x26
#define REG_MIN_VCCBRAM		0x27

#define REG_FLAG		0x3F
#define REG_CFG1		0x41

#define REG_SEQ_SEL0		0x48
#define REG_SEQ_SEL1		0x49
#define REG_SEQ_AVG0		0x4A
#define REG_SEQ_AVG1		0x4B
#define REG_SEQ_BIP0		0x4C
#define REG_SEQ_BIP1		0x4D
#define REG_SEQ_ACQ0		0x4E
#define REG_SEQ_ACQ1		0x4F

/* XADC register fields */
#define REG_CFG1_CAL_ADCOG	(1 << 5)	/* ADC offset & gain */
#define REG_CFG1_CAL_SSOG	(1 << 7)	/* supply sensor offset &gain */

#define REG_CFG1_SEQ_MSK	0xF
#define REG_CFG1_SEQ_SHIFT	12

#define MODE_DEF		0	/* Internal sensors, no alarms */
#define MODE_IND		8	/* Independent:ADC A -int, ADC B -ext */

#define REG_FLAG_DIS		(1 << 8)
#define REG_FLAG_REF		(1 << 9)

/* Sequencer registers 0 */
#define REG_SEQ_V		(1 << 11)

#define READ(dev, reg) readl((dev->iobase + XADC_##reg))
#define WRITE(dev, reg, value) writel(value, dev->iobase+XADC_##reg)

#define GETFIELD(reg, field, value) \
	(((value) >> (reg##_##field##_SHIFT)) & reg##_##field##_MSK)

#define SETFIELD(reg, field, value) \
	(((value) & reg##_##field##_MSK) << reg##_##field##_SHIFT)

#define CLRFIELD(reg, field, value) \
	((value) & ~(reg##_##field##_MSK << reg##_##field##_SHIFT))

#define READOP(reg) \
		(SETFIELD(XADC_FIFO, CMD, XADC_CMD_READ) |\
		SETFIELD(XADC_FIFO, ADDR, reg))

#define WRITEOP(reg, val) \
		(SETFIELD(XADC_FIFO, CMD, XADC_CMD_WRITE) |\
		SETFIELD(XADC_FIFO, ADDR, reg) | SETFIELD(XADC_FIFO, DATA, val))

#define NOOP (SETFIELD(XADC_FIFO, CMD, XADC_CMD_NOP))

struct xadc_op {
	u32 cmd;
	u32 res;
};

struct xadc_batch {
	int count;
	int writeptr;
	int readptr;
	struct list_head q;
	struct completion comp;
	struct xadc_op *ops;
};


struct xadc_t {
	struct device *dev;
	struct device *hwmon;
	void __iomem *iobase;
	int irq;
	struct clk	*clk;
	spinlock_t slock;
	struct list_head runq;
	struct xadc_batch *curr;
	u32 chanmode[17];	/* Channel 0-15 VAUX, 16 is V */
	#define CHAN_ON		1
	#define CHAN_BIPOLAR	2
};


static void run_batch(struct xadc_t *xadc)
{
	u32 config, rdt;

	if (list_empty(&xadc->runq)) {
		xadc->curr = NULL;
		return;
	}
	xadc->curr = list_first_entry(&xadc->runq, struct xadc_batch, q);
	list_del(&xadc->curr->q);

	config = READ(xadc, CONFIG);
	config = CLRFIELD(XADC_CONFIG, CFIFOTH, config);
	config = CLRFIELD(XADC_CONFIG, DFIFOTH, config);

	rdt = xadc->curr->count-xadc->curr->readptr;
	if (rdt > 15) /* Trigger at half FIFO or count */
		rdt = 8;
	else
		rdt--;

	config |= SETFIELD(XADC_CONFIG, CFIFOTH, 0) | /* Just trigger */
		  SETFIELD(XADC_CONFIG, DFIFOTH, rdt);

	WRITE(xadc, CONFIG, config);

	/* unmask CFIFO,DFIFO interrupts */
	WRITE(xadc, INTMSK, READ(xadc, INTMSK) &
			~(XADC_INT_CFIFO_LTH | XADC_INT_DFIFO_GTH));
}

static void add_batch(struct xadc_t *xadc, struct xadc_batch *b)
{
	unsigned long flags;

	BUG_ON(b->count < 1);
	b->writeptr = 0;
	b->readptr = 0;
	init_completion(&b->comp);
	list_add_tail(&b->q, &xadc->runq);
	spin_lock_irqsave(&xadc->slock, flags);
	if (!xadc->curr)
		run_batch(xadc);
	spin_unlock_irqrestore(&xadc->slock, flags);
}

static inline u16 read_register(struct xadc_t *xadc, unsigned int reg)
{
	u16 ret;
	struct xadc_op *ops;
	struct xadc_batch *b = kzalloc(sizeof(*b), GFP_KERNEL);

	if (!b)
		return 0;
	ops = kzalloc(sizeof(*ops) * 2, GFP_KERNEL);
	if (!ops) {
		kfree(b);
		return 0;
	}

	b->count = 2;
	b->ops = ops;
	ops[0].cmd = READOP(reg);
	ops[1].cmd = NOOP;
	add_batch(xadc, b);
	wait_for_completion_interruptible(&b->comp);
	ret = GETFIELD(XADC_FIFO, DATA, b->ops[1].res);
	kfree(ops);
	kfree(b);
	return ret;
}

static inline void write_register(struct xadc_t *xadc, unsigned int reg,
		u16 val)
{
	struct xadc_op *ops;
	struct xadc_batch *b = kzalloc(sizeof(*b), GFP_KERNEL);

	if (!b)
		return;
	ops = kzalloc(sizeof(*ops), GFP_KERNEL);
	if (!ops) {
		kfree(b);
		return;
	}

	b->count = 1;
	b->ops = ops;
	ops[0].cmd = WRITEOP(reg, val);
	add_batch(xadc, b);
	wait_for_completion_interruptible(&b->comp);
	kfree(ops);
	kfree(b);
}

static inline int reg2temp(u16 reg)
{
	int val;

	val = (reg >> 4) & 0xFFF; /* Use only 12 bits */
	val = ((val * 503975) / 4096) - 273150; /* (X*503.975/4096) -273.15 */
	val = DIV_ROUND_CLOSEST(val, 1000);
	return val;
}

static inline unsigned reg2vcc(u16 reg)
{
	unsigned val;

	val = (reg >> 4) & 0xFFF; /* Use only 12 bits */
	val = ((val * 3000) / 4096); /* (X*3/4096) */
	/* Return voltage in mV */
	return val;
}

static inline unsigned reg2v(u16 reg)
{
	unsigned val;

	val = (reg >> 4) & 0xFFF; /*Use only 12 bits */
	val = (val * 1000 / 4096); /* (X/4096) */
	/* Return voltage in mV */
	return val;
}

static inline int reg2bv(u16 reg)
{
	int val;

	val = (reg >> 4) & 0xFFF; /* Use only 12 bits */
	if (val & 0x800)
		val = val - 0x1000;
	val = (val * 1000 / 4096); /* (X/4096) */
	/* Return voltage in mV */
	return val;
}

static ssize_t xadc_read_temp(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xadc_t *xadc = platform_get_drvdata(pdev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	unsigned int reg = attr->index;
	u16 regval;

	clk_enable(xadc->clk);

	regval = read_register(xadc, reg);

	clk_disable(xadc->clk);

	return sprintf(buf, "%d\n", reg2temp(regval));
}

static ssize_t xadc_read_vcc(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xadc_t *xadc = platform_get_drvdata(pdev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	unsigned int reg = attr->index;
	u16 regval;

	clk_enable(xadc->clk);

	regval = read_register(xadc, reg);

	clk_disable(xadc->clk);

	return sprintf(buf, "%u\n", reg2vcc(regval));
}

static ssize_t xadc_read_v(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xadc_t *xadc = platform_get_drvdata(pdev);
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	unsigned int reg = attr->index;
	unsigned int chan = attr->nr;
	u16 regval;

	if (!(xadc->chanmode[chan] & CHAN_ON))
		return sprintf(buf, "%d\n", 0);

	clk_enable(xadc->clk);

	regval = read_register(xadc, reg);

	clk_disable(xadc->clk);

	if ((xadc->chanmode[chan] & CHAN_BIPOLAR))
		return sprintf(buf, "%d\n", reg2bv(regval));

	return sprintf(buf, "%u\n", reg2v(regval));
}

#ifdef DEBUG
static ssize_t xadc_read_registers(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xadc_t *xadc = platform_get_drvdata(pdev);
	unsigned int i, count = 0;

	clk_enable(xadc->clk);

	for (i = 0; i < 0x60; i++)
		count += sprintf(buf+count, "%02X %04x\n", i,
				read_register(xadc, i));

	clk_disable(xadc->clk);

	return count;
}
#endif

static ssize_t xadc_read_flags(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xadc_t *xadc = platform_get_drvdata(pdev);
	u16 val;

	clk_enable(xadc->clk);

	val = read_register(xadc, REG_FLAG);

	clk_disable(xadc->clk);

	return sprintf(buf, "enabled:\t%s\nreference:\t%s\n",
		val & REG_FLAG_DIS ? "no" : "yes",
		val & REG_FLAG_REF ? "internal" : "external");
}

static ssize_t xadc_read_vmode(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xadc_t *xadc = platform_get_drvdata(pdev);
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	unsigned int channel = attr->nr;

	return sprintf(buf, "%s\n", (xadc->chanmode[channel] & CHAN_ON) ?
			((xadc->chanmode[channel] & CHAN_BIPOLAR) ?
			 "bipolar" : "unipolar") : "off");
}

static ssize_t xadc_write_vmode(struct device *dev,
		struct device_attribute *devattr,
		const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xadc_t *xadc = platform_get_drvdata(pdev);
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	unsigned int channel = attr->nr;
	unsigned int reg = attr->index;
	u32 mode;
	u16 val;

	if (!strncmp("off", buf, 3))
		mode = 0;
	else if (!strncmp("unipolar", buf, 8))
		mode = CHAN_ON;
	else if (!strncmp("bipolar", buf, 7))
		mode = CHAN_ON | CHAN_BIPOLAR;
	else
		return -EIO;

	if (mode == xadc->chanmode[channel])
		return count;

	xadc->chanmode[channel] = mode;

	clk_enable(xadc->clk);

	if (mode & CHAN_BIPOLAR) {
		val = read_register(xadc, reg + REG_SEQ_BIP0);
		if (0 == reg) /* only dedicated channel there */
			val |= REG_SEQ_V;
		else
			val |= 1 << channel;
		write_register(xadc, reg + REG_SEQ_BIP0, val);
	} else {
		val = read_register(xadc, reg + REG_SEQ_BIP0);
		if (0 == reg) /* only dedicated channel there */
			val &= ~REG_SEQ_V;
		else
			val &= ~(1 << channel);
		write_register(xadc, reg + REG_SEQ_BIP0, val);
	}

	if (mode & CHAN_ON) {
		val = read_register(xadc, reg + REG_SEQ_SEL0);
		if (0 == reg) /* only dedicated channel there */
			val |= REG_SEQ_V;
		else
			val |= 1 << channel;
		write_register(xadc, reg + REG_SEQ_SEL0, val);
	} else {
		val = read_register(xadc, reg + REG_SEQ_SEL0);
		if (0 == reg) /* only dedicated channel there */
			val &= ~REG_SEQ_V;
		else
			val &= ~(1 << channel);
		write_register(xadc, reg + REG_SEQ_SEL0, val);
	}

	clk_disable(xadc->clk);

	return count;
}

static ssize_t show_name(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "%s\n", "xadcps");
}

static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static SENSOR_DEVICE_ATTR(status, S_IRUGO, xadc_read_flags, NULL, 0);
static SENSOR_DEVICE_ATTR(temp, S_IRUGO, xadc_read_temp, NULL, REG_TEMP);
static SENSOR_DEVICE_ATTR(temp_min, S_IRUGO, xadc_read_temp, NULL,
		REG_MIN_TEMP);
static SENSOR_DEVICE_ATTR(temp_max, S_IRUGO, xadc_read_temp, NULL,
		REG_MAX_TEMP);
static SENSOR_DEVICE_ATTR(vccint, S_IRUGO, xadc_read_vcc, NULL, REG_VCCINT);
static SENSOR_DEVICE_ATTR(vccint_min, S_IRUGO, xadc_read_vcc, NULL,
		REG_MIN_VCCINT);
static SENSOR_DEVICE_ATTR(vccint_max, S_IRUGO, xadc_read_vcc, NULL,
		REG_MAX_VCCINT);
static SENSOR_DEVICE_ATTR(vccaux, S_IRUGO, xadc_read_vcc, NULL, REG_VCCAUX);
static SENSOR_DEVICE_ATTR(vccaux_min, S_IRUGO, xadc_read_vcc, NULL,
		REG_MIN_VCCAUX);
static SENSOR_DEVICE_ATTR(vccaux_max, S_IRUGO, xadc_read_vcc, NULL,
		REG_MAX_VCCAUX);
static SENSOR_DEVICE_ATTR(vccbram, S_IRUGO, xadc_read_vcc, NULL, REG_VCCBRAM);
static SENSOR_DEVICE_ATTR(vccbram_min, S_IRUGO, xadc_read_vcc, NULL,
		REG_MIN_VCCBRAM);
static SENSOR_DEVICE_ATTR(vccbram_max, S_IRUGO, xadc_read_vcc, NULL,
		REG_MAX_VCCBRAM);
/*
 * channel number, register
 * for VPVN  = 16, REG_VPVN
 * for VAUXi =  i, REG_VAUX0 + i
 */
static SENSOR_DEVICE_ATTR_2(v, S_IRUGO, xadc_read_v, NULL, 16, REG_VPVN);
/* channel number, offset from REG_SEQ_xxx_0
 * for VPVN  = 16, 0
 * for VAUXi =  i, 1
 */
static SENSOR_DEVICE_ATTR_2(v_mode, S_IWUSR|S_IRUGO, xadc_read_vmode,
		xadc_write_vmode, 16, 0);
#ifdef DEBUG
static SENSOR_DEVICE_ATTR(registers, S_IRUGO, xadc_read_registers, NULL, 0);
#endif

static struct attribute *xadc_attr[] = {
	&dev_attr_name.attr,
#ifdef DEBUG
	&sensor_dev_attr_registers.dev_attr.attr,
#endif
	&sensor_dev_attr_status.dev_attr.attr,
	&sensor_dev_attr_temp.dev_attr.attr,
	&sensor_dev_attr_temp_min.dev_attr.attr,
	&sensor_dev_attr_temp_max.dev_attr.attr,
	&sensor_dev_attr_vccint.dev_attr.attr,
	&sensor_dev_attr_vccint_min.dev_attr.attr,
	&sensor_dev_attr_vccint_max.dev_attr.attr,
	&sensor_dev_attr_vccaux.dev_attr.attr,
	&sensor_dev_attr_vccaux_min.dev_attr.attr,
	&sensor_dev_attr_vccaux_max.dev_attr.attr,
	&sensor_dev_attr_vccbram.dev_attr.attr,
	&sensor_dev_attr_vccbram_min.dev_attr.attr,
	&sensor_dev_attr_vccbram_max.dev_attr.attr,
	&sensor_dev_attr_v.dev_attr.attr,
	&sensor_dev_attr_v_mode.dev_attr.attr,
	NULL
};



static const struct attribute_group xadc_group = {
	.attrs = xadc_attr,
};

static irqreturn_t xadc_irq(int irq, void *data)
{
	struct xadc_t *xadc = data;
	u32 intsts, intmsk;


	intsts = READ(xadc, INTSTS);
	intmsk = READ(xadc, INTMSK);
	dev_dbg(xadc->dev, "intsts %08x intmsk %08x\n", intsts, intmsk);

	if (intsts & ~intmsk & XADC_INT_DFIFO_GTH) {

		while (!(READ(xadc, STATUS) & XADC_STATUS_DFIFO_EMPTY))
			xadc->curr->ops[xadc->curr->readptr++].res =
				READ(xadc, DFIFO);
		if ((xadc->curr->readptr) == xadc->curr->count) {
			intmsk |= XADC_INT_DFIFO_GTH;
			WRITE(xadc, INTMSK, intmsk);
			complete(&xadc->curr->comp);
			run_batch(xadc);
		} else {
			u32 config;
			int rdt;

			config = READ(xadc, CONFIG);
			config = CLRFIELD(XADC_CONFIG, DFIFOTH, config);
			rdt = xadc->curr->count-xadc->curr->readptr;
			if (rdt > 15)
				rdt = 8;
			else
				rdt--;
			config |= SETFIELD(XADC_CONFIG, DFIFOTH, rdt);
			WRITE(xadc, CONFIG, config);
		}
		WRITE(xadc, INTSTS, XADC_INT_DFIFO_GTH);
	}

	if (intsts & ~intmsk & XADC_INT_CFIFO_LTH) {
		int i, towrite;
		u32 status = READ(xadc, STATUS);

		/*Don't write more than FIFO capacity*/
		towrite = xadc->curr->count - xadc->curr->writeptr;
		towrite = (15 - GETFIELD(XADC_STATUS, CFIFO_LVL, status));
		towrite = min(towrite,
			xadc->curr->count - xadc->curr->writeptr);

		for (i = 0; i < towrite; i++)
			WRITE(xadc, CFIFO,
				xadc->curr->ops[xadc->curr->writeptr++].cmd);

		if (xadc->curr->writeptr == xadc->curr->count) {
			intmsk |= XADC_INT_CFIFO_LTH;
			WRITE(xadc, INTMSK, intmsk);
		} else {
			u32 config;
			int cmdt;
			config = READ(xadc, CONFIG);
			config = CLRFIELD(XADC_CONFIG, CFIFOTH, config);
			cmdt = xadc->curr->count-xadc->curr->writeptr;
			if (cmdt > 8) /* Half-full */
				cmdt = 8;
			else /* Will write all next time */
				cmdt--;
			config |= SETFIELD(XADC_CONFIG, CFIFOTH, cmdt);
			WRITE(xadc, CONFIG, config);
		}
		WRITE(xadc, INTSTS, XADC_INT_CFIFO_LTH);
	}

	return IRQ_HANDLED;
}

static struct xadc_op xadc_ops[] = {
	{.cmd = WRITEOP(REG_CFG1, REG_CFG1_CAL_SSOG |
			REG_CFG1_CAL_ADCOG |
			SETFIELD(REG_CFG1, SEQ, MODE_DEF)),},
	{.cmd = READOP(REG_FLAG),}, /* read flags */
	{.cmd = WRITEOP(REG_SEQ_SEL0, 0)},
	{.cmd = WRITEOP(REG_SEQ_AVG0, 0)},
	{.cmd = WRITEOP(REG_SEQ_BIP0, 0)},
	{.cmd = WRITEOP(REG_SEQ_ACQ0, 0)},
	{.cmd = WRITEOP(REG_SEQ_SEL1, 0)},
	{.cmd = WRITEOP(REG_SEQ_AVG1, 0)},
	{.cmd = WRITEOP(REG_SEQ_BIP1, 0)},
	{.cmd = WRITEOP(REG_SEQ_ACQ1, 0)},
	{.cmd = WRITEOP(REG_CFG1, REG_CFG1_CAL_SSOG | REG_CFG1_CAL_ADCOG |
			SETFIELD(REG_CFG1, SEQ, MODE_IND)),},
};
static struct xadc_batch setup = {
	.count = ARRAY_SIZE(xadc_ops),
	.ops = xadc_ops,
};

static int xadc_probe(struct platform_device *pdev)
{
	struct xadc_t *xadc;
	struct resource *res;
	u16 val;
	int ret;

	xadc = devm_kzalloc(&pdev->dev, sizeof(*xadc), GFP_KERNEL);
	if (!xadc)
		return -ENOMEM;

	xadc->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xadc->iobase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xadc->iobase))
		return PTR_ERR(xadc->iobase);

	xadc->irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, xadc->irq, &xadc_irq, IRQF_SHARED,
			       dev_name(&pdev->dev), xadc);
	if (ret) {
		dev_err(xadc->dev, "Failed to request irq: %d\n", xadc->irq);
		return ret;
	}

	xadc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(xadc->clk)) {
		dev_err(&pdev->dev, "input clock not found\n");
		return PTR_ERR(xadc->clk);
	}

	ret = clk_prepare_enable(xadc->clk);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable clock\n");
		return ret;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &xadc_group);
	if (ret)
		goto err_clk_disable;

	platform_set_drvdata(pdev, xadc);

	xadc->hwmon = hwmon_device_register(&pdev->dev);
	if (IS_ERR(xadc->hwmon)) {
		ret = PTR_ERR(xadc->hwmon);
		dev_err(xadc->dev, "Failed to register hwmon device\n");
		goto err_group;
	}

	WRITE(xadc, CONFIG, 0);
	WRITE(xadc, CTL, 0); /* ~RESET */

	WRITE(xadc, CONFIG, XADC_CONFIG_WEDGE | /* Default values */
		XADC_CONFIG_REDGE |
		SETFIELD(XADC_CONFIG, TCKRATE, TCKRATE_DIV16) |
		SETFIELD(XADC_CONFIG, IGAP, 20));

	WRITE(xadc, CONFIG, READ(xadc, CONFIG) | XADC_CONFIG_ENABLE);

	WRITE(xadc, INTSTS, ~0); /* clear all interrupts */
	WRITE(xadc, INTMSK, ~0); /* mask all interrupts */

	INIT_LIST_HEAD(&xadc->runq);
	spin_lock_init(&xadc->slock);

	add_batch(xadc, &setup);
	wait_for_completion_interruptible(&setup.comp);

	val = setup.ops[2].res;
	dev_info(xadc->dev, "enabled:\t%s\treference:\t%s\n",
		val & REG_FLAG_DIS ? "no" : "yes",
		val & REG_FLAG_REF ? "internal" : "external");

	clk_disable(xadc->clk);

	return 0;

err_group:
	sysfs_remove_group(&pdev->dev.kobj, &xadc_group);
err_clk_disable:
	clk_disable_unprepare(xadc->clk);

	return ret;
}

static int xadc_remove(struct platform_device *pdev)
{
	struct xadc_t *xadc = platform_get_drvdata(pdev);

	hwmon_device_unregister(xadc->hwmon);

	sysfs_remove_group(&pdev->dev.kobj, &xadc_group);

	clk_unprepare(xadc->clk);

	return 0;
}

static struct of_device_id xadcps_of_match[] = {
	{ .compatible = "xlnx,zynq-xadc-1.00.a", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, xadcps_of_match);

static struct platform_driver xadc_driver = {
	.probe = xadc_probe,
	.remove = xadc_remove,
	.driver = {
		.name = "xadcps",
		.owner = THIS_MODULE,
		.of_match_table = xadcps_of_match,
	},
};

module_platform_driver(xadc_driver);

MODULE_AUTHOR("Vlad Lungu <vlad.lungu@windriver.com>");
MODULE_DESCRIPTION("Xilinx Zynq XADC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:xadcps");
