/*
 * xilinx_ps2.c
 *
 * Xilinx PS/2 driver to interface PS/2 component to Linux
 *
 * Author: MontaVista Software, Inc.
 *	   source@mvista.com
 *
 * (c) 2005 MontaVista Software, Inc.
 * (c) 2008 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/module.h>
#include <linux/serio.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/xilinx_devices.h>
#include <asm/io.h>

#ifdef CONFIG_OF		/* For open firmware */
 #include <linux/of_device.h>
 #include <linux/of_platform.h>
#endif /* CONFIG_OF */

#include "xilinx_ps2.h"

#define DRIVER_NAME		"xilinx_ps2"
#define DRIVER_DESCRIPTION	"Xilinx XPS PS/2 driver"

#define XPS2_NAME_DESC		"Xilinx XPS PS/2 Port #%d"
#define XPS2_PHYS_DESC		"xilinxps2/serio%d"


static DECLARE_MUTEX(cfg_sem);

/*********************/
/* Interrupt handler */
/*********************/
static irqreturn_t xps2_interrupt(int irq, void *dev_id)
{
	struct xps2data *drvdata = (struct xps2data *)dev_id;
	u32 intr_sr;
	u32 ier;
	u8 c;
	u8 retval;

	/* Get the PS/2 interrupts and clear them */
	intr_sr = in_be32(drvdata->base_address + XPS2_IPISR_OFFSET);
	out_be32(drvdata->base_address + XPS2_IPISR_OFFSET, intr_sr);

	/* Check which interrupt is active */
	if (intr_sr & XPS2_IPIXR_RX_OVF) {
		printk(KERN_ERR "%s: receive overrun error\n",
			drvdata->serio.name);
	}

	if (intr_sr & XPS2_IPIXR_RX_ERR) {
		drvdata->dfl |= SERIO_PARITY;
	}

	if (intr_sr & (XPS2_IPIXR_TX_NOACK | XPS2_IPIXR_WDT_TOUT)) {
		drvdata->dfl |= SERIO_TIMEOUT;
	}

	if (intr_sr & XPS2_IPIXR_RX_FULL) {
		retval = xps2_recv(drvdata, &drvdata->rxb);

		/* Error, if 1 byte is not received */
		if (retval != 1) {
			printk(KERN_ERR
				"%s: wrong rcvd byte count (%d)\n",
				drvdata->serio.name, retval);
		}
		c = drvdata->rxb;
		serio_interrupt(&drvdata->serio, c, drvdata->dfl);
		drvdata->dfl = 0;
	}

	if (intr_sr & XPS2_IPIXR_TX_ACK) {

		/* Disable the TX interrupts after the transmission is
		 * complete */
		ier = in_be32(drvdata->base_address + XPS2_IPIER_OFFSET);
		ier &= (~(XPS2_IPIXR_TX_ACK & XPS2_IPIXR_ALL ));
		out_be32(drvdata->base_address + XPS2_IPIER_OFFSET, ier);
		drvdata->dfl = 0;
	}

	return IRQ_HANDLED;
}

/*******************/
/* serio callbacks */
/*******************/

/*
 * sxps2_write() sends a byte out through the PS/2 interface.
 *
 * The sole purpose of drvdata->tx_end is to prevent the driver
 * from locking up in the do {} while; loop when nothing is connected
 * to the given PS/2 port. That's why we do not try to recover
 * from the transmission failure.
 * drvdata->tx_end needs not to be initialized to some "far in the
 * future" value, as the very first attempt to xps2_send() a byte
 * is always successful, and drvdata->tx_end will be set to a proper
 * value at that moment - before the 1st use in the comparison.
 */
static int sxps2_write(struct serio *pserio, unsigned char c)
{
	struct xps2data *drvdata = pserio->port_data;
	unsigned long flags;
	int retval;

	do {
		spin_lock_irqsave(&drvdata->lock, flags);
		retval = xps2_send(drvdata, &c);
		spin_unlock_irqrestore(&drvdata->lock, flags);

		if (retval == 1) {
			drvdata->tx_end = jiffies + HZ;
			return 0;	/* success */
		}
	} while (!time_after(jiffies, drvdata->tx_end));

	return 1;			/* transmission is frozen */
}

/*
 * sxps2_open() is called when a port is open by the higher layer.
 */
static int sxps2_open(struct serio *pserio)
{
	struct xps2data *drvdata = pserio->port_data;
	int retval;

	retval = request_irq(drvdata->irq, &xps2_interrupt, 0,
				DRIVER_NAME, drvdata);
	if (retval) {
		printk(KERN_ERR
			"%s: Couldn't allocate interrupt %d\n",
			drvdata->serio.name, drvdata->irq);
		return retval;
	}

	/* start reception by enabling the interrupts */
	out_be32(drvdata->base_address + XPS2_GIER_OFFSET, XPS2_GIER_GIE_MASK);
	out_be32(drvdata->base_address + XPS2_IPIER_OFFSET, XPS2_IPIXR_RX_ALL);
	(void)xps2_recv(drvdata, &drvdata->rxb);

	return 0;		/* success */
}

/*
 * sxps2_close() frees the interrupt.
 */
static void sxps2_close(struct serio *pserio)
{
	struct xps2data *drvdata = pserio->port_data;

	/* Disable the PS2 interrupts */
	out_be32(drvdata->base_address + XPS2_GIER_OFFSET, 0x00);
	out_be32(drvdata->base_address + XPS2_IPIER_OFFSET, 0x00);
	free_irq(drvdata->irq, drvdata);
}

/*************************/
/* XPS PS/2 driver calls */
/*************************/

/*
 * xps2_initialize() initializes the Xilinx PS/2 device.
 */
static int xps2_initialize(struct xps2data *drvdata)
{
	/* Disable all the interrupts just in case */
	out_be32(drvdata->base_address + XPS2_IPIER_OFFSET, 0);

	/* Reset the PS2 device and abort any current transaction, to make sure
	 * we have the PS2 in a good state */
	out_be32(drvdata->base_address + XPS2_SRST_OFFSET, XPS2_SRST_RESET);

	return 0;
}

/*
 * xps2_send() sends the specified byte of data to the PS/2 port in interrupt
 * mode.
 */
static u8 xps2_send(struct xps2data *drvdata, u8 *byte)
{
	u32 sr;
	u32 ier;
	u8 retval = 0;

	/* Enter a critical region by disabling the PS/2 transmit interrupts to
	 * allow this call to stop a previous operation that may be interrupt
	 * driven. Only stop the transmit interrupt since this critical region
	 * is not really exited in the normal manner */
	ier = in_be32(drvdata->base_address + XPS2_IPIER_OFFSET);
	ier &= (~(XPS2_IPIXR_TX_ALL & XPS2_IPIXR_ALL ));
	out_be32(drvdata->base_address + XPS2_IPIER_OFFSET, ier);

	/* If the PS/2 transmitter is empty send a byte of data */
	sr = in_be32(drvdata->base_address + XPS2_STATUS_OFFSET);
	if ((sr & XPS2_STATUS_TX_FULL) == 0) {
		out_be32(drvdata->base_address + XPS2_TX_DATA_OFFSET, *byte);
		retval = 1;
	}

	/* Enable the TX interrupts to track the status of the transmission */
	ier = in_be32(drvdata->base_address + XPS2_IPIER_OFFSET);
	ier |= ((XPS2_IPIXR_TX_ALL | XPS2_IPIXR_WDT_TOUT ));
	out_be32(drvdata->base_address + XPS2_IPIER_OFFSET, ier);

	return retval;		/* no. of bytes sent */
}

/*
 * xps2_recv() will attempt to receive a byte of data from the PS/2 port.
 */
static u8 xps2_recv(struct xps2data *drvdata, u8 *byte)
{
	u32 sr;
	u8 retval = 0;

	/* If there is data available in the PS/2 receiver, read it */
	sr = in_be32(drvdata->base_address + XPS2_STATUS_OFFSET);
	if (sr & XPS2_STATUS_RX_FULL) {
		*byte = in_be32(drvdata->base_address + XPS2_RX_DATA_OFFSET);
		retval = 1;
	}

	return retval;		/* no. of bytes received */
}

/******************************/
/* The platform device driver */
/******************************/

static int xps2_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	struct resource *irq_res = NULL;	/* Interrupt resources */
	struct resource *regs_res = NULL;	/* IO mem resources */

	if (!dev) {
		dev_err(dev, "Probe called with NULL param\n");
		return -EINVAL;
	}

	/* Find irq number, map the control registers in */
	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	regs_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	return xps2_setup(dev, pdev->id, regs_res, irq_res);
}

/*
 * Shared device initialization code.
 */
static int xps2_setup(struct device *dev, int id, struct resource *regs_res,
			struct resource *irq_res)
{
	struct xps2data *drvdata;
	unsigned long remap_size;
	int retval;

	if (!dev)
		return -EINVAL;

	drvdata = kzalloc(sizeof(struct xps2data), GFP_KERNEL);
	if (!drvdata) {
		dev_err(dev, "Couldn't allocate device private record\n");
		return -ENOMEM;
	}
	spin_lock_init(&drvdata->lock);
	dev_set_drvdata(dev, (void *)drvdata);

	if (!regs_res || !irq_res) {
		dev_err(dev, "IO resource(s) not found\n");
		retval = -EFAULT;
		goto failed1;
	}

	drvdata->irq = irq_res->start;
	remap_size = regs_res->end - regs_res->start + 1;
	if (!request_mem_region(regs_res->start, remap_size, DRIVER_NAME)) {

		dev_err(dev,"Couldn't lock memory region at 0x%08X\n",
			regs_res->start);
		retval = -EBUSY;
		goto failed1;
	}

	/* Fill in configuration data and add them to the list */
	drvdata->phys_addr = regs_res->start;
	drvdata->remap_size = remap_size;
	drvdata->base_address = ioremap(regs_res->start, remap_size);
	if (drvdata->base_address == NULL) {

		dev_err(dev,"Couldn't ioremap memory at 0x%08X\n",
			regs_res->start);
		retval = -EFAULT;
		goto failed2;
	}

	/* Initialize the PS/2 interface */
	down(&cfg_sem);
	if (xps2_initialize(drvdata)) {
		up(&cfg_sem);
		dev_err(dev,"Could not initialize device\n");
		retval = -ENODEV;
		goto failed3;
	}
	up(&cfg_sem);

	dev_info(dev, "Xilinx PS2 at 0x%08X mapped to 0x%08X, irq=%d\n",
		drvdata->phys_addr, (u32)drvdata->base_address, drvdata->irq);

	drvdata->serio.id.type = SERIO_8042;
	drvdata->serio.write = sxps2_write;
	drvdata->serio.open = sxps2_open;
	drvdata->serio.close = sxps2_close;
	drvdata->serio.port_data = drvdata;
	drvdata->serio.dev.parent = dev;
	snprintf(drvdata->serio.name, sizeof(drvdata->serio.name),
		 XPS2_NAME_DESC, id);
	snprintf(drvdata->serio.phys, sizeof(drvdata->serio.phys),
		 XPS2_PHYS_DESC, id);
	serio_register_port(&drvdata->serio);

	return 0;		/* success */

failed3:
	iounmap(drvdata->base_address);

failed2:
	release_mem_region(regs_res->start, remap_size);

failed1:
	kfree(drvdata);
	dev_set_drvdata(dev, NULL);

	return retval;
}

/*
 * xps2_remove() dissociates the driver with the Xilinx PS/2 device.
 */
static int xps2_remove(struct device *dev)
{
	struct xps2data *drvdata;

	if (!dev)
		return -EINVAL;

	drvdata = (struct xps2data *)dev_get_drvdata(dev);

	serio_unregister_port(&drvdata->serio);

	iounmap(drvdata->base_address);

	release_mem_region(drvdata->phys_addr, drvdata->remap_size);

	kfree(drvdata);
	dev_set_drvdata(dev, NULL);

	return 0;		/* success */
}

static struct device_driver xps2_driver = {
	.name = DRIVER_NAME,
	.bus = &platform_bus_type,
	.probe = xps2_probe,
	.remove = xps2_remove
};

#ifdef CONFIG_OF
static int __devinit xps2_of_probe(struct of_device *ofdev, const struct
				   of_device_id *match)
{
	struct resource r_irq_struct;
	struct resource r_mem_struct;
	struct resource *r_irq = &r_irq_struct;	/* Interrupt resources */
	struct resource *r_mem = &r_mem_struct;	/* IO mem resources */
	int rc = 0;
	const unsigned int *id;

	printk(KERN_INFO "Device Tree Probing \'%s\'\n",
			ofdev->node->name);

	/* Get iospace for the device */
	rc = of_address_to_resource(ofdev->node, 0, r_mem);
	if(rc) {
		dev_warn(&ofdev->dev, "invalid address\n");
		return rc;
	}

	/* Get IRQ for the device */
	rc = of_irq_to_resource(ofdev->node, 0, r_irq);
	if(rc == NO_IRQ) {
		dev_warn(&ofdev->dev, "no IRQ found\n");
		return rc;
	}

	id = of_get_property(ofdev->node, "port-number", NULL);
	return xps2_setup(&ofdev->dev, id ? *id : -1, r_mem, r_irq);
}

static int __devexit xps2_of_remove(struct of_device *dev)
{
	return xps2_remove(&dev->dev);
}

static struct of_device_id __devinitdata xps2_of_match[] = {
	{ .compatible = "xlnx,xps-ps2-1.00.a", },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, xps2_of_match);

static struct of_platform_driver xps2_of_driver = {
	.name		= DRIVER_NAME,
	.match_table	= xps2_of_match,
	.probe		= xps2_of_probe,
	.remove		= __devexit_p(xps2_of_remove),
};
#endif /* CONFIG_OF */

static int __init xps2_init(void)
{
	int status = driver_register(&xps2_driver);
#ifdef CONFIG_OF
	status |= of_register_platform_driver(&xps2_of_driver);
#endif /* CONFIG_OF */
	return status;
}

static void __exit xps2_cleanup(void)
{
	driver_unregister(&xps2_driver);
#ifdef CONFIG_OF
	of_unregister_platform_driver(&xps2_of_driver);
#endif /* CONFIG_OF */
}

module_init(xps2_init);
module_exit(xps2_cleanup);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL");

