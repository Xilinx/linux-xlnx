/*
 * MCP23S08 SPI/I2C GPIO gpio expander driver
 *
 * The inputs and outputs of the mcp23s08, mcp23s17, mcp23008 and mcp23017 are
 * supported.
 * For the I2C versions of the chips (mcp23008 and mcp23017) generation of
 * interrupts is also supported.
 * The hardware of the SPI versions of the chips (mcp23s08 and mcp23s17) is
 * also capable of generating interrupts, but the linux driver does not
 * support that yet.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/spi/mcp23s08.h>
#include <linux/slab.h>
#include <asm/byteorder.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>

/**
 * MCP types supported by driver
 */
#define MCP_TYPE_S08	0
#define MCP_TYPE_S17	1
#define MCP_TYPE_008	2
#define MCP_TYPE_017	3
#define MCP_TYPE_S18    4

/* Registers are all 8 bits wide.
 *
 * The mcp23s17 has twice as many bits, and can be configured to work
 * with either 16 bit registers or with two adjacent 8 bit banks.
 */
#define MCP_IODIR	0x00		/* init/reset:  all ones */
#define MCP_IPOL	0x01
#define MCP_GPINTEN	0x02
#define MCP_DEFVAL	0x03
#define MCP_INTCON	0x04
#define MCP_IOCON	0x05
#	define IOCON_MIRROR	(1 << 6)
#	define IOCON_SEQOP	(1 << 5)
#	define IOCON_HAEN	(1 << 3)
#	define IOCON_ODR	(1 << 2)
#	define IOCON_INTPOL	(1 << 1)
#	define IOCON_INTCC	(1)
#define MCP_GPPU	0x06
#define MCP_INTF	0x07
#define MCP_INTCAP	0x08
#define MCP_GPIO	0x09
#define MCP_OLAT	0x0a

struct mcp23s08;

struct mcp23s08_ops {
	int	(*read)(struct mcp23s08 *mcp, unsigned reg);
	int	(*write)(struct mcp23s08 *mcp, unsigned reg, unsigned val);
	int	(*read_regs)(struct mcp23s08 *mcp, unsigned reg,
			     u16 *vals, unsigned n);
};

struct mcp23s08 {
	u8			addr;
	bool			irq_active_high;

	u16			cache[11];
	u16			irq_rise;
	u16			irq_fall;
	int			irq;
	bool			irq_controller;
	/* lock protects the cached values */
	struct mutex		lock;
	struct mutex		irq_lock;

	struct gpio_chip	chip;

	const struct mcp23s08_ops	*ops;
	void			*data; /* ops specific data */
};

/* A given spi_device can represent up to eight mcp23sxx chips
 * sharing the same chipselect but using different addresses
 * (e.g. chips #0 and #3 might be populated, but not #1 or $2).
 * Driver data holds all the per-chip data.
 */
struct mcp23s08_driver_data {
	unsigned		ngpio;
	struct mcp23s08		*mcp[8];
	struct mcp23s08		chip[];
};

/*----------------------------------------------------------------------*/

#if IS_ENABLED(CONFIG_I2C)

static int mcp23008_read(struct mcp23s08 *mcp, unsigned reg)
{
	return i2c_smbus_read_byte_data(mcp->data, reg);
}

static int mcp23008_write(struct mcp23s08 *mcp, unsigned reg, unsigned val)
{
	return i2c_smbus_write_byte_data(mcp->data, reg, val);
}

static int
mcp23008_read_regs(struct mcp23s08 *mcp, unsigned reg, u16 *vals, unsigned n)
{
	while (n--) {
		int ret = mcp23008_read(mcp, reg++);
		if (ret < 0)
			return ret;
		*vals++ = ret;
	}

	return 0;
}

static int mcp23017_read(struct mcp23s08 *mcp, unsigned reg)
{
	return i2c_smbus_read_word_data(mcp->data, reg << 1);
}

static int mcp23017_write(struct mcp23s08 *mcp, unsigned reg, unsigned val)
{
	return i2c_smbus_write_word_data(mcp->data, reg << 1, val);
}

static int
mcp23017_read_regs(struct mcp23s08 *mcp, unsigned reg, u16 *vals, unsigned n)
{
	while (n--) {
		int ret = mcp23017_read(mcp, reg++);
		if (ret < 0)
			return ret;
		*vals++ = ret;
	}

	return 0;
}

static const struct mcp23s08_ops mcp23008_ops = {
	.read		= mcp23008_read,
	.write		= mcp23008_write,
	.read_regs	= mcp23008_read_regs,
};

static const struct mcp23s08_ops mcp23017_ops = {
	.read		= mcp23017_read,
	.write		= mcp23017_write,
	.read_regs	= mcp23017_read_regs,
};

#endif /* CONFIG_I2C */

/*----------------------------------------------------------------------*/

#ifdef CONFIG_SPI_MASTER

static int mcp23s08_read(struct mcp23s08 *mcp, unsigned reg)
{
	u8	tx[2], rx[1];
	int	status;

	tx[0] = mcp->addr | 0x01;
	tx[1] = reg;
	status = spi_write_then_read(mcp->data, tx, sizeof(tx), rx, sizeof(rx));
	return (status < 0) ? status : rx[0];
}

static int mcp23s08_write(struct mcp23s08 *mcp, unsigned reg, unsigned val)
{
	u8	tx[3];

	tx[0] = mcp->addr;
	tx[1] = reg;
	tx[2] = val;
	return spi_write_then_read(mcp->data, tx, sizeof(tx), NULL, 0);
}

static int
mcp23s08_read_regs(struct mcp23s08 *mcp, unsigned reg, u16 *vals, unsigned n)
{
	u8	tx[2], *tmp;
	int	status;

	if ((n + reg) > sizeof(mcp->cache))
		return -EINVAL;
	tx[0] = mcp->addr | 0x01;
	tx[1] = reg;

	tmp = (u8 *)vals;
	status = spi_write_then_read(mcp->data, tx, sizeof(tx), tmp, n);
	if (status >= 0) {
		while (n--)
			vals[n] = tmp[n]; /* expand to 16bit */
	}
	return status;
}

static int mcp23s17_read(struct mcp23s08 *mcp, unsigned reg)
{
	u8	tx[2], rx[2];
	int	status;

	tx[0] = mcp->addr | 0x01;
	tx[1] = reg << 1;
	status = spi_write_then_read(mcp->data, tx, sizeof(tx), rx, sizeof(rx));
	return (status < 0) ? status : (rx[0] | (rx[1] << 8));
}

static int mcp23s17_write(struct mcp23s08 *mcp, unsigned reg, unsigned val)
{
	u8	tx[4];

	tx[0] = mcp->addr;
	tx[1] = reg << 1;
	tx[2] = val;
	tx[3] = val >> 8;
	return spi_write_then_read(mcp->data, tx, sizeof(tx), NULL, 0);
}

static int
mcp23s17_read_regs(struct mcp23s08 *mcp, unsigned reg, u16 *vals, unsigned n)
{
	u8	tx[2];
	int	status;

	if ((n + reg) > sizeof(mcp->cache))
		return -EINVAL;
	tx[0] = mcp->addr | 0x01;
	tx[1] = reg << 1;

	status = spi_write_then_read(mcp->data, tx, sizeof(tx),
				     (u8 *)vals, n * 2);
	if (status >= 0) {
		while (n--)
			vals[n] = __le16_to_cpu((__le16)vals[n]);
	}

	return status;
}

static const struct mcp23s08_ops mcp23s08_ops = {
	.read		= mcp23s08_read,
	.write		= mcp23s08_write,
	.read_regs	= mcp23s08_read_regs,
};

static const struct mcp23s08_ops mcp23s17_ops = {
	.read		= mcp23s17_read,
	.write		= mcp23s17_write,
	.read_regs	= mcp23s17_read_regs,
};

#endif /* CONFIG_SPI_MASTER */

/*----------------------------------------------------------------------*/

static int mcp23s08_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct mcp23s08	*mcp = gpiochip_get_data(chip);
	int status;

	mutex_lock(&mcp->lock);
	mcp->cache[MCP_IODIR] |= (1 << offset);
	status = mcp->ops->write(mcp, MCP_IODIR, mcp->cache[MCP_IODIR]);
	mutex_unlock(&mcp->lock);
	return status;
}

static int mcp23s08_get(struct gpio_chip *chip, unsigned offset)
{
	struct mcp23s08	*mcp = gpiochip_get_data(chip);
	int status;

	mutex_lock(&mcp->lock);

	/* REVISIT reading this clears any IRQ ... */
	status = mcp->ops->read(mcp, MCP_GPIO);
	if (status < 0)
		status = 0;
	else {
		mcp->cache[MCP_GPIO] = status;
		status = !!(status & (1 << offset));
	}
	mutex_unlock(&mcp->lock);
	return status;
}

static int __mcp23s08_set(struct mcp23s08 *mcp, unsigned mask, int value)
{
	unsigned olat = mcp->cache[MCP_OLAT];

	if (value)
		olat |= mask;
	else
		olat &= ~mask;
	mcp->cache[MCP_OLAT] = olat;
	return mcp->ops->write(mcp, MCP_OLAT, olat);
}

static void mcp23s08_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct mcp23s08	*mcp = gpiochip_get_data(chip);
	unsigned mask = 1 << offset;

	mutex_lock(&mcp->lock);
	__mcp23s08_set(mcp, mask, value);
	mutex_unlock(&mcp->lock);
}

static int
mcp23s08_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct mcp23s08	*mcp = gpiochip_get_data(chip);
	unsigned mask = 1 << offset;
	int status;

	mutex_lock(&mcp->lock);
	status = __mcp23s08_set(mcp, mask, value);
	if (status == 0) {
		mcp->cache[MCP_IODIR] &= ~mask;
		status = mcp->ops->write(mcp, MCP_IODIR, mcp->cache[MCP_IODIR]);
	}
	mutex_unlock(&mcp->lock);
	return status;
}

/*----------------------------------------------------------------------*/
static irqreturn_t mcp23s08_irq(int irq, void *data)
{
	struct mcp23s08 *mcp = data;
	int intcap, intf, i;
	unsigned int child_irq;

	mutex_lock(&mcp->lock);
	intf = mcp->ops->read(mcp, MCP_INTF);
	if (intf < 0) {
		mutex_unlock(&mcp->lock);
		return IRQ_HANDLED;
	}

	mcp->cache[MCP_INTF] = intf;

	intcap = mcp->ops->read(mcp, MCP_INTCAP);
	if (intcap < 0) {
		mutex_unlock(&mcp->lock);
		return IRQ_HANDLED;
	}

	mcp->cache[MCP_INTCAP] = intcap;
	mutex_unlock(&mcp->lock);


	for (i = 0; i < mcp->chip.ngpio; i++) {
		if ((BIT(i) & mcp->cache[MCP_INTF]) &&
		    ((BIT(i) & intcap & mcp->irq_rise) ||
		     (mcp->irq_fall & ~intcap & BIT(i)) ||
		     (BIT(i) & mcp->cache[MCP_INTCON]))) {
			child_irq = irq_find_mapping(mcp->chip.irqdomain, i);
			handle_nested_irq(child_irq);
		}
	}

	return IRQ_HANDLED;
}

static void mcp23s08_irq_mask(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct mcp23s08 *mcp = gpiochip_get_data(gc);
	unsigned int pos = data->hwirq;

	mcp->cache[MCP_GPINTEN] &= ~BIT(pos);
}

static void mcp23s08_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct mcp23s08 *mcp = gpiochip_get_data(gc);
	unsigned int pos = data->hwirq;

	mcp->cache[MCP_GPINTEN] |= BIT(pos);
}

static int mcp23s08_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct mcp23s08 *mcp = gpiochip_get_data(gc);
	unsigned int pos = data->hwirq;
	int status = 0;

	if ((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH) {
		mcp->cache[MCP_INTCON] &= ~BIT(pos);
		mcp->irq_rise |= BIT(pos);
		mcp->irq_fall |= BIT(pos);
	} else if (type & IRQ_TYPE_EDGE_RISING) {
		mcp->cache[MCP_INTCON] &= ~BIT(pos);
		mcp->irq_rise |= BIT(pos);
		mcp->irq_fall &= ~BIT(pos);
	} else if (type & IRQ_TYPE_EDGE_FALLING) {
		mcp->cache[MCP_INTCON] &= ~BIT(pos);
		mcp->irq_rise &= ~BIT(pos);
		mcp->irq_fall |= BIT(pos);
	} else if (type & IRQ_TYPE_LEVEL_HIGH) {
		mcp->cache[MCP_INTCON] |= BIT(pos);
		mcp->cache[MCP_DEFVAL] &= ~BIT(pos);
	} else if (type & IRQ_TYPE_LEVEL_LOW) {
		mcp->cache[MCP_INTCON] |= BIT(pos);
		mcp->cache[MCP_DEFVAL] |= BIT(pos);
	} else
		return -EINVAL;

	return status;
}

static void mcp23s08_irq_bus_lock(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct mcp23s08 *mcp = gpiochip_get_data(gc);

	mutex_lock(&mcp->irq_lock);
}

static void mcp23s08_irq_bus_unlock(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct mcp23s08 *mcp = gpiochip_get_data(gc);

	mutex_lock(&mcp->lock);
	mcp->ops->write(mcp, MCP_GPINTEN, mcp->cache[MCP_GPINTEN]);
	mcp->ops->write(mcp, MCP_DEFVAL, mcp->cache[MCP_DEFVAL]);
	mcp->ops->write(mcp, MCP_INTCON, mcp->cache[MCP_INTCON]);
	mutex_unlock(&mcp->lock);
	mutex_unlock(&mcp->irq_lock);
}

static struct irq_chip mcp23s08_irq_chip = {
	.name = "gpio-mcp23xxx",
	.irq_mask = mcp23s08_irq_mask,
	.irq_unmask = mcp23s08_irq_unmask,
	.irq_set_type = mcp23s08_irq_set_type,
	.irq_bus_lock = mcp23s08_irq_bus_lock,
	.irq_bus_sync_unlock = mcp23s08_irq_bus_unlock,
};

static int mcp23s08_irq_setup(struct mcp23s08 *mcp)
{
	struct gpio_chip *chip = &mcp->chip;
	int err;
	unsigned long irqflags = IRQF_ONESHOT | IRQF_SHARED;

	mutex_init(&mcp->irq_lock);

	if (mcp->irq_active_high)
		irqflags |= IRQF_TRIGGER_HIGH;
	else
		irqflags |= IRQF_TRIGGER_LOW;

	err = devm_request_threaded_irq(chip->parent, mcp->irq, NULL,
					mcp23s08_irq,
					irqflags, dev_name(chip->parent), mcp);
	if (err != 0) {
		dev_err(chip->parent, "unable to request IRQ#%d: %d\n",
			mcp->irq, err);
		return err;
	}

	err =  gpiochip_irqchip_add(chip,
				    &mcp23s08_irq_chip,
				    0,
				    handle_simple_irq,
				    IRQ_TYPE_NONE);
	if (err) {
		dev_err(chip->parent,
			"could not connect irqchip to gpiochip: %d\n", err);
		return err;
	}

	gpiochip_set_chained_irqchip(chip,
				     &mcp23s08_irq_chip,
				     mcp->irq,
				     NULL);

	return 0;
}

/*----------------------------------------------------------------------*/

#ifdef CONFIG_DEBUG_FS

#include <linux/seq_file.h>

/*
 * This shows more info than the generic gpio dump code:
 * pullups, deglitching, open drain drive.
 */
static void mcp23s08_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct mcp23s08	*mcp;
	char		bank;
	int		t;
	unsigned	mask;

	mcp = gpiochip_get_data(chip);

	/* NOTE: we only handle one bank for now ... */
	bank = '0' + ((mcp->addr >> 1) & 0x7);

	mutex_lock(&mcp->lock);
	t = mcp->ops->read_regs(mcp, 0, mcp->cache, ARRAY_SIZE(mcp->cache));
	if (t < 0) {
		seq_printf(s, " I/O ERROR %d\n", t);
		goto done;
	}

	for (t = 0, mask = 1; t < chip->ngpio; t++, mask <<= 1) {
		const char	*label;

		label = gpiochip_is_requested(chip, t);
		if (!label)
			continue;

		seq_printf(s, " gpio-%-3d P%c.%d (%-12s) %s %s %s",
			chip->base + t, bank, t, label,
			(mcp->cache[MCP_IODIR] & mask) ? "in " : "out",
			(mcp->cache[MCP_GPIO] & mask) ? "hi" : "lo",
			(mcp->cache[MCP_GPPU] & mask) ? "up" : "  ");
		/* NOTE:  ignoring the irq-related registers */
		seq_puts(s, "\n");
	}
done:
	mutex_unlock(&mcp->lock);
}

#else
#define mcp23s08_dbg_show	NULL
#endif

/*----------------------------------------------------------------------*/

static int mcp23s08_probe_one(struct mcp23s08 *mcp, struct device *dev,
			      void *data, unsigned addr, unsigned type,
			      struct mcp23s08_platform_data *pdata, int cs)
{
	int status;
	bool mirror = false;

	mutex_init(&mcp->lock);

	mcp->data = data;
	mcp->addr = addr;
	mcp->irq_active_high = false;

	mcp->chip.direction_input = mcp23s08_direction_input;
	mcp->chip.get = mcp23s08_get;
	mcp->chip.direction_output = mcp23s08_direction_output;
	mcp->chip.set = mcp23s08_set;
	mcp->chip.dbg_show = mcp23s08_dbg_show;
#ifdef CONFIG_OF_GPIO
	mcp->chip.of_gpio_n_cells = 2;
	mcp->chip.of_node = dev->of_node;
#endif

	switch (type) {
#ifdef CONFIG_SPI_MASTER
	case MCP_TYPE_S08:
		mcp->ops = &mcp23s08_ops;
		mcp->chip.ngpio = 8;
		mcp->chip.label = "mcp23s08";
		break;

	case MCP_TYPE_S17:
		mcp->ops = &mcp23s17_ops;
		mcp->chip.ngpio = 16;
		mcp->chip.label = "mcp23s17";
		break;

	case MCP_TYPE_S18:
		mcp->ops = &mcp23s17_ops;
		mcp->chip.ngpio = 16;
		mcp->chip.label = "mcp23s18";
		break;
#endif /* CONFIG_SPI_MASTER */

#if IS_ENABLED(CONFIG_I2C)
	case MCP_TYPE_008:
		mcp->ops = &mcp23008_ops;
		mcp->chip.ngpio = 8;
		mcp->chip.label = "mcp23008";
		break;

	case MCP_TYPE_017:
		mcp->ops = &mcp23017_ops;
		mcp->chip.ngpio = 16;
		mcp->chip.label = "mcp23017";
		break;
#endif /* CONFIG_I2C */

	default:
		dev_err(dev, "invalid device type (%d)\n", type);
		return -EINVAL;
	}

	mcp->chip.base = pdata->base;
	mcp->chip.can_sleep = true;
	mcp->chip.parent = dev;
	mcp->chip.owner = THIS_MODULE;

	/* verify MCP_IOCON.SEQOP = 0, so sequential reads work,
	 * and MCP_IOCON.HAEN = 1, so we work with all chips.
	 */

	status = mcp->ops->read(mcp, MCP_IOCON);
	if (status < 0)
		goto fail;

	mcp->irq_controller = pdata->irq_controller;
	if (mcp->irq && mcp->irq_controller) {
		mcp->irq_active_high =
			of_property_read_bool(mcp->chip.parent->of_node,
					      "microchip,irq-active-high");

		mirror = pdata->mirror;
	}

	if ((status & IOCON_SEQOP) || !(status & IOCON_HAEN) || mirror ||
	     mcp->irq_active_high) {
		/* mcp23s17 has IOCON twice, make sure they are in sync */
		status &= ~(IOCON_SEQOP | (IOCON_SEQOP << 8));
		status |= IOCON_HAEN | (IOCON_HAEN << 8);
		if (mcp->irq_active_high)
			status |= IOCON_INTPOL | (IOCON_INTPOL << 8);
		else
			status &= ~(IOCON_INTPOL | (IOCON_INTPOL << 8));

		if (mirror)
			status |= IOCON_MIRROR | (IOCON_MIRROR << 8);

		if (type == MCP_TYPE_S18)
			status |= IOCON_INTCC | (IOCON_INTCC << 8);

		status = mcp->ops->write(mcp, MCP_IOCON, status);
		if (status < 0)
			goto fail;
	}

	/* configure ~100K pullups */
	status = mcp->ops->write(mcp, MCP_GPPU, pdata->chip[cs].pullups);
	if (status < 0)
		goto fail;

	status = mcp->ops->read_regs(mcp, 0, mcp->cache, ARRAY_SIZE(mcp->cache));
	if (status < 0)
		goto fail;

	/* disable inverter on input */
	if (mcp->cache[MCP_IPOL] != 0) {
		mcp->cache[MCP_IPOL] = 0;
		status = mcp->ops->write(mcp, MCP_IPOL, 0);
		if (status < 0)
			goto fail;
	}

	/* disable irqs */
	if (mcp->cache[MCP_GPINTEN] != 0) {
		mcp->cache[MCP_GPINTEN] = 0;
		status = mcp->ops->write(mcp, MCP_GPINTEN, 0);
		if (status < 0)
			goto fail;
	}

	status = gpiochip_add_data(&mcp->chip, mcp);
	if (status < 0)
		goto fail;

	if (mcp->irq && mcp->irq_controller) {
		status = mcp23s08_irq_setup(mcp);
		if (status) {
			goto fail;
		}
	}
fail:
	if (status < 0)
		dev_dbg(dev, "can't setup chip %d, --> %d\n",
			addr, status);
	return status;
}

/*----------------------------------------------------------------------*/

#ifdef CONFIG_OF
#ifdef CONFIG_SPI_MASTER
static const struct of_device_id mcp23s08_spi_of_match[] = {
	{
		.compatible = "microchip,mcp23s08",
		.data = (void *) MCP_TYPE_S08,
	},
	{
		.compatible = "microchip,mcp23s17",
		.data = (void *) MCP_TYPE_S17,
	},
	{
		.compatible = "microchip,mcp23s18",
		.data = (void *) MCP_TYPE_S18,
	},
/* NOTE: The use of the mcp prefix is deprecated and will be removed. */
	{
		.compatible = "mcp,mcp23s08",
		.data = (void *) MCP_TYPE_S08,
	},
	{
		.compatible = "mcp,mcp23s17",
		.data = (void *) MCP_TYPE_S17,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, mcp23s08_spi_of_match);
#endif

#if IS_ENABLED(CONFIG_I2C)
static const struct of_device_id mcp23s08_i2c_of_match[] = {
	{
		.compatible = "microchip,mcp23008",
		.data = (void *) MCP_TYPE_008,
	},
	{
		.compatible = "microchip,mcp23017",
		.data = (void *) MCP_TYPE_017,
	},
/* NOTE: The use of the mcp prefix is deprecated and will be removed. */
	{
		.compatible = "mcp,mcp23008",
		.data = (void *) MCP_TYPE_008,
	},
	{
		.compatible = "mcp,mcp23017",
		.data = (void *) MCP_TYPE_017,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, mcp23s08_i2c_of_match);
#endif
#endif /* CONFIG_OF */


#if IS_ENABLED(CONFIG_I2C)

static int mcp230xx_probe(struct i2c_client *client,
				    const struct i2c_device_id *id)
{
	struct mcp23s08_platform_data *pdata, local_pdata;
	struct mcp23s08 *mcp;
	int status;
	const struct of_device_id *match;

	match = of_match_device(of_match_ptr(mcp23s08_i2c_of_match),
					&client->dev);
	if (match) {
		pdata = &local_pdata;
		pdata->base = -1;
		pdata->chip[0].pullups = 0;
		pdata->irq_controller =	of_property_read_bool(
					client->dev.of_node,
					"interrupt-controller");
		pdata->mirror = of_property_read_bool(client->dev.of_node,
						      "microchip,irq-mirror");
		client->irq = irq_of_parse_and_map(client->dev.of_node, 0);
	} else {
		pdata = dev_get_platdata(&client->dev);
		if (!pdata) {
			pdata = devm_kzalloc(&client->dev,
					sizeof(struct mcp23s08_platform_data),
					GFP_KERNEL);
			if (!pdata)
				return -ENOMEM;
			pdata->base = -1;
		}
	}

	mcp = kzalloc(sizeof(*mcp), GFP_KERNEL);
	if (!mcp)
		return -ENOMEM;

	mcp->irq = client->irq;
	status = mcp23s08_probe_one(mcp, &client->dev, client, client->addr,
				    id->driver_data, pdata, 0);
	if (status)
		goto fail;

	i2c_set_clientdata(client, mcp);

	return 0;

fail:
	kfree(mcp);

	return status;
}

static int mcp230xx_remove(struct i2c_client *client)
{
	struct mcp23s08 *mcp = i2c_get_clientdata(client);

	gpiochip_remove(&mcp->chip);
	kfree(mcp);

	return 0;
}

static const struct i2c_device_id mcp230xx_id[] = {
	{ "mcp23008", MCP_TYPE_008 },
	{ "mcp23017", MCP_TYPE_017 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mcp230xx_id);

static struct i2c_driver mcp230xx_driver = {
	.driver = {
		.name	= "mcp230xx",
		.of_match_table = of_match_ptr(mcp23s08_i2c_of_match),
	},
	.probe		= mcp230xx_probe,
	.remove		= mcp230xx_remove,
	.id_table	= mcp230xx_id,
};

static int __init mcp23s08_i2c_init(void)
{
	return i2c_add_driver(&mcp230xx_driver);
}

static void mcp23s08_i2c_exit(void)
{
	i2c_del_driver(&mcp230xx_driver);
}

#else

static int __init mcp23s08_i2c_init(void) { return 0; }
static void mcp23s08_i2c_exit(void) { }

#endif /* CONFIG_I2C */

/*----------------------------------------------------------------------*/

#ifdef CONFIG_SPI_MASTER

static int mcp23s08_probe(struct spi_device *spi)
{
	struct mcp23s08_platform_data	*pdata, local_pdata;
	unsigned			addr;
	int				chips = 0;
	struct mcp23s08_driver_data	*data;
	int				status, type;
	unsigned			ngpio = 0;
	const struct			of_device_id *match;
	u32				spi_present_mask = 0;

	match = of_match_device(of_match_ptr(mcp23s08_spi_of_match), &spi->dev);
	if (match) {
		type = (int)(uintptr_t)match->data;
		status = of_property_read_u32(spi->dev.of_node,
			    "microchip,spi-present-mask", &spi_present_mask);
		if (status) {
			status = of_property_read_u32(spi->dev.of_node,
				    "mcp,spi-present-mask", &spi_present_mask);
			if (status) {
				dev_err(&spi->dev,
					"DT has no spi-present-mask\n");
				return -ENODEV;
			}
		}
		if ((spi_present_mask <= 0) || (spi_present_mask >= 256)) {
			dev_err(&spi->dev, "invalid spi-present-mask\n");
			return -ENODEV;
		}

		pdata = &local_pdata;
		pdata->base = -1;
		for (addr = 0; addr < ARRAY_SIZE(pdata->chip); addr++) {
			pdata->chip[addr].pullups = 0;
			if (spi_present_mask & (1 << addr))
				chips++;
		}
		pdata->irq_controller =	of_property_read_bool(
					spi->dev.of_node,
					"interrupt-controller");
		pdata->mirror = of_property_read_bool(spi->dev.of_node,
						      "microchip,irq-mirror");
	} else {
		type = spi_get_device_id(spi)->driver_data;
		pdata = dev_get_platdata(&spi->dev);
		if (!pdata) {
			pdata = devm_kzalloc(&spi->dev,
					sizeof(struct mcp23s08_platform_data),
					GFP_KERNEL);
			pdata->base = -1;
		}

		for (addr = 0; addr < ARRAY_SIZE(pdata->chip); addr++) {
			if (!pdata->chip[addr].is_present)
				continue;
			chips++;
			if ((type == MCP_TYPE_S08) && (addr > 3)) {
				dev_err(&spi->dev,
					"mcp23s08 only supports address 0..3\n");
				return -EINVAL;
			}
			spi_present_mask |= 1 << addr;
		}
	}

	if (!chips)
		return -ENODEV;

	data = devm_kzalloc(&spi->dev,
			    sizeof(*data) + chips * sizeof(struct mcp23s08),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	spi_set_drvdata(spi, data);

	spi->irq = irq_of_parse_and_map(spi->dev.of_node, 0);

	for (addr = 0; addr < ARRAY_SIZE(pdata->chip); addr++) {
		if (!(spi_present_mask & (1 << addr)))
			continue;
		chips--;
		data->mcp[addr] = &data->chip[chips];
		data->mcp[addr]->irq = spi->irq;
		status = mcp23s08_probe_one(data->mcp[addr], &spi->dev, spi,
					    0x40 | (addr << 1), type, pdata,
					    addr);
		if (status < 0)
			goto fail;

		if (pdata->base != -1)
			pdata->base += data->mcp[addr]->chip.ngpio;
		ngpio += data->mcp[addr]->chip.ngpio;
	}
	data->ngpio = ngpio;

	/* NOTE:  these chips have a relatively sane IRQ framework, with
	 * per-signal masking and level/edge triggering.  It's not yet
	 * handled here...
	 */

	return 0;

fail:
	for (addr = 0; addr < ARRAY_SIZE(data->mcp); addr++) {

		if (!data->mcp[addr])
			continue;
		gpiochip_remove(&data->mcp[addr]->chip);
	}
	return status;
}

static int mcp23s08_remove(struct spi_device *spi)
{
	struct mcp23s08_driver_data	*data = spi_get_drvdata(spi);
	unsigned			addr;

	for (addr = 0; addr < ARRAY_SIZE(data->mcp); addr++) {

		if (!data->mcp[addr])
			continue;

		gpiochip_remove(&data->mcp[addr]->chip);
	}

	return 0;
}

static const struct spi_device_id mcp23s08_ids[] = {
	{ "mcp23s08", MCP_TYPE_S08 },
	{ "mcp23s17", MCP_TYPE_S17 },
	{ "mcp23s18", MCP_TYPE_S18 },
	{ },
};
MODULE_DEVICE_TABLE(spi, mcp23s08_ids);

static struct spi_driver mcp23s08_driver = {
	.probe		= mcp23s08_probe,
	.remove		= mcp23s08_remove,
	.id_table	= mcp23s08_ids,
	.driver = {
		.name	= "mcp23s08",
		.of_match_table = of_match_ptr(mcp23s08_spi_of_match),
	},
};

static int __init mcp23s08_spi_init(void)
{
	return spi_register_driver(&mcp23s08_driver);
}

static void mcp23s08_spi_exit(void)
{
	spi_unregister_driver(&mcp23s08_driver);
}

#else

static int __init mcp23s08_spi_init(void) { return 0; }
static void mcp23s08_spi_exit(void) { }

#endif /* CONFIG_SPI_MASTER */

/*----------------------------------------------------------------------*/

static int __init mcp23s08_init(void)
{
	int ret;

	ret = mcp23s08_spi_init();
	if (ret)
		goto spi_fail;

	ret = mcp23s08_i2c_init();
	if (ret)
		goto i2c_fail;

	return 0;

 i2c_fail:
	mcp23s08_spi_exit();
 spi_fail:
	return ret;
}
/* register after spi/i2c postcore initcall and before
 * subsys initcalls that may rely on these GPIOs
 */
subsys_initcall(mcp23s08_init);

static void __exit mcp23s08_exit(void)
{
	mcp23s08_spi_exit();
	mcp23s08_i2c_exit();
}
module_exit(mcp23s08_exit);

MODULE_LICENSE("GPL");
