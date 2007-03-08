/*
 *  altera DE2 PS/2
 *
 *  linux/drivers/input/serio/sa1111ps2.c
 *
 *  Copyright (C) 2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <asm/system.h>


struct ps2if {
	struct serio		*io;
	struct platform_device	*dev;
	unsigned base;
        unsigned irq;
};

/*
 * Read all bytes waiting in the PS2 port.  There should be
 * at the most one, but we loop for safety.  If there was a
 * framing error, we have to manually clear the status.
 */
static irqreturn_t ps2_rxint(int irq, void *dev_id)
{
	struct ps2if *ps2if = dev_id;
	unsigned int status;
	int handled = IRQ_NONE;

	while ((status = inl(ps2if->base)) & 0xffff0000) {
		serio_interrupt(ps2if->io, status & 0xff, 0);
		handled = IRQ_HANDLED;
	}
	return handled;
}

/*
 * Write a byte to the PS2 port.  We have to wait for the
 * port to indicate that the transmitter is empty.
 */
static int ps2_write(struct serio *io, unsigned char val)
{
	struct ps2if *ps2if = io->port_data;
	outl(val,ps2if->base);
	// should check command send error
	if (inl(ps2if->base+4) & (1<<10))
	  {
	    // printk("ps2 write error %02x\n",val);
	  }
	return 0;
}

static int ps2_open(struct serio *io)
{
	struct ps2if *ps2if = io->port_data;
	int ret;

	ret = request_irq(ps2if->irq, ps2_rxint, 0,
			  "altps2", ps2if);
	if (ret) {
		printk(KERN_ERR "altps2: could not allocate IRQ%d: %d\n",
			ps2if->irq, ret);
		return ret;
	}
	outl(1,ps2if->base+4);  // enable rx irq
	return 0;
}

static void ps2_close(struct serio *io)
{
	struct ps2if *ps2if = io->port_data;
	outl(0,ps2if->base);  // disable rx irq
	free_irq(ps2if->irq, ps2if);
}

/*
 * Add one device to this driver.
 */
static int ps2_probe(struct platform_device *dev)
{
	struct ps2if *ps2if;
	struct serio *serio;
	unsigned int status;
	int ret;

	ps2if = kmalloc(sizeof(struct ps2if), GFP_KERNEL);
	serio = kmalloc(sizeof(struct serio), GFP_KERNEL);
	if (!ps2if || !serio) {
		ret = -ENOMEM;
		goto free;
	}

	memset(ps2if, 0, sizeof(struct ps2if));
	memset(serio, 0, sizeof(struct serio));

	serio->id.type		= SERIO_8042;
	serio->write		= ps2_write;
	serio->open		= ps2_open;
	serio->close		= ps2_close;
	strlcpy(serio->name, dev->dev.bus_id, sizeof(serio->name));
	strlcpy(serio->phys, dev->dev.bus_id, sizeof(serio->phys));
	serio->port_data	= ps2if;
	serio->dev.parent	= &dev->dev;
	ps2if->io		= serio;
	ps2if->dev		= dev;
	platform_set_drvdata(dev, ps2if);

	/*
	 * Request the physical region for this PS2 port.
	 */
	if (dev->num_resources < 2) {
		ret = -ENODEV;
		goto out;
	}
	if (!request_mem_region(dev->resource[0].start,
				4,
				"altps2")) {
		ret = -EBUSY;
		goto free;
	}
	ps2if->base = dev->resource[0].start;
	ps2if->irq  = dev->resource[1].start;
	printk("altps2 : base %08x irq %d\n",ps2if->base,ps2if->irq);
	// clear fifo
	while ((status = inl(ps2if->base)) & 0xffff0000) {
	}

	serio_register_port(ps2if->io);
	return 0;

 out:
	release_mem_region(dev->resource[0].start,4);
 free:
	platform_set_drvdata(dev, NULL);
	kfree(ps2if);
	kfree(serio);
	return ret;
}

/*
 * Remove one device from this driver.
 */
static int ps2_remove(struct platform_device *dev)
{
	struct ps2if *ps2if = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);
	serio_unregister_port(ps2if->io);
	release_mem_region(dev->resource[0].start,4);

	kfree(ps2if);

	return 0;
}

/*
 * Our device driver structure
 */
static struct platform_driver ps2_driver = {
	.probe		= ps2_probe,
	.remove		= ps2_remove,
	.driver	= {
		.name	= "altps2",
	},
};

static int __init ps2_init(void)
{
	return platform_driver_register(&ps2_driver);
}

static void __exit ps2_exit(void)
{
	platform_driver_unregister(&ps2_driver);
}

module_init(ps2_init);
module_exit(ps2_exit);
