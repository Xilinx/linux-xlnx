/*
 * adapter.c
 *
 * Xilinx PS/2 Adapter component to interface PS/2 component to Linux
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2005 (c)MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * This driver is a bit unusual in that it is composed of two logical
 * parts where one part is the OS independent code and the other part is
 * the OS dependent code.  Xilinx provides their drivers split in this
 * fashion.  This file represents the Linux OS dependent part known as
 * the Linux adapter.  The other files in this directory are the OS
 * independent files as provided by Xilinx with no changes made to them.
 * The names exported by those files begin with XPs2_.  All functions
 * in this file that are called by Linux have names that begin with
 * xps2_.  Any other functions are static helper functions.
 */

#include <linux/module.h>
#include <linux/serio.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/xilinx_devices.h>

#include <asm/io.h>

#include "xps2.h"

#define DRIVER_NAME		"xilinx_ps2"
#define DRIVER_DESCRIPTION	"Xilinx PS/2 driver"

#define XPS2_NAME_DESC		"Xilinx PS/2 Port #%d"
#define XPS2_PHYS_DESC		"xilinxps2/serio%d"

struct xps2data {
	int irq;
	u32 phys_addr;
	u32 remap_size;
	struct pt_regs *saved_regs;
	spinlock_t lock;
	u8 rxb;			/* Rx buffer */
	unsigned long tx_end;
	unsigned int dfl;
	/*
	 * The underlying OS independent code needs space as well.  A
	 * pointer to the following XPs2 structure will be passed to
	 * any XPs2_ function that requires it.  However, we treat the
	 * data as an opaque object in this file (meaning that we never
	 * reference any of the fields inside of the structure).
	 */
	XPs2 ps2;
	/*
	 * serio
	 */
	struct serio serio;
};

/*******************************************************************************
 * This configuration stuff should become unnecessary after EDK version 8.x is
 * released.
 ******************************************************************************/

static DECLARE_MUTEX(cfg_sem);

/*********************/
/* Interrupt handler */
/*********************/

static irqreturn_t xps2_interrupt(int irq, void *dev_id)
{
	struct xps2data *drvdata = (struct xps2data *)dev_id;

	/* Call EDK handler */
	XPs2_InterruptHandler(&drvdata->ps2);

	return IRQ_HANDLED;
}

static void sxps2_handler(void *CallbackRef, u32 Event, unsigned int EventData)
{
	struct xps2data *drvdata = (struct xps2data *)CallbackRef;
	u8 c;

	switch (Event) {
	case XPS2_EVENT_RECV_OVF:
		printk(KERN_ERR "%s: receive overrun error.\n",
		       drvdata->serio.name);
	case XPS2_EVENT_RECV_ERROR:
		drvdata->dfl |= SERIO_PARITY;
		break;
	case XPS2_EVENT_SENT_NOACK:
	case XPS2_EVENT_TIMEOUT:
		drvdata->dfl |= SERIO_TIMEOUT;
		break;
	case XPS2_EVENT_RECV_DATA:
		if (EventData > 0) {
			if (EventData != 1) {
				printk(KERN_ERR
				       "%s: wrong rcvd byte count (%d).\n",
				       drvdata->serio.name, EventData);
			}
			c = drvdata->rxb;

			XPs2_Recv(&drvdata->ps2, &drvdata->rxb, 1);
			serio_interrupt(&drvdata->serio, c, drvdata->dfl);
			drvdata->dfl = 0;
		}
		break;
	case XPS2_EVENT_SENT_DATA:
		break;
	default:
		printk(KERN_ERR "%s: unrecognized event %u.\n",
		       drvdata->serio.name, Event);
	}
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
 * future" value, as the very first attempt to XPs2_Send() a byte
 * is always successfull, and drvdata->tx_end will be set to a proper
 * value at that moment - before the 1st use in the comparison.
 */
static int sxps2_write(struct serio *pserio, unsigned char c)
{
	struct xps2data *drvdata = pserio->port_data;
	unsigned long flags;
	int retval;

	do {
		spin_lock_irqsave(&drvdata->lock, flags);
		retval = XPs2_Send(&drvdata->ps2, &c, 1);
		spin_unlock_irqrestore(&drvdata->lock, flags);

		if (retval == 1) {
			drvdata->tx_end = jiffies + HZ;
			return 0;	/* success */
		}
	} while (!time_after(jiffies, drvdata->tx_end));

	return 1;		/* transmission is frozen */
}

/*
 * sxps2_open() is called when a port is open by the higher layer.
 */

static int sxps2_open(struct serio *pserio)
{
	struct xps2data *drvdata = pserio->port_data;
	int retval;

	retval = request_irq(drvdata->irq, &xps2_interrupt, 0,
			     "xilinx_ps2", drvdata);
	if (retval) {
		printk(KERN_ERR
		       "%s: Couldn't allocate interrupt %d.\n",
		       drvdata->serio.name, drvdata->irq);
		return retval;
	}

	/* start receiption */
	XPs2_EnableInterrupt(&drvdata->ps2);
	XPs2_Recv(&drvdata->ps2, &drvdata->rxb, 1);

	return 0;		/* success */
}

/*
 * sxps2_close() frees the interrupt.
 */

static void sxps2_close(struct serio *pserio)
{
	struct xps2data *drvdata = pserio->port_data;

	XPs2_DisableInterrupt(&drvdata->ps2);
	free_irq(drvdata->irq, drvdata);
}

/******************************
 * The platform device driver *
 ******************************/

static int xps2_probe(struct device *dev)
{
	XPs2_Config xps2_cfg;
	struct platform_device *pdev = to_platform_device(dev);
	struct xps2data *drvdata;
	struct resource *irq_res, *regs_res;
	unsigned long remap_size;
	int retval;

	if (!dev)
		return -EINVAL;

	drvdata = kzalloc(sizeof(struct xps2data), GFP_KERNEL);
	if (!drvdata) {
		printk(KERN_ERR
		       "%s #%d: Couldn't allocate device private record\n",
		       DRIVER_NAME, pdev->id);
		return -ENOMEM;
	}
	spin_lock_init(&drvdata->lock);
	dev_set_drvdata(dev, (void *)drvdata);

	/* Find irq number, map the control registers in */
	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	regs_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs_res || !irq_res) {
		printk(KERN_ERR "%s #%d: IO resource(s) not found\n",
		       DRIVER_NAME, pdev->id);
		retval = -EFAULT;
		goto failed1;
	}
	drvdata->irq = irq_res->start;

	remap_size = regs_res->end - regs_res->start + 1;
	if (!request_mem_region(regs_res->start, remap_size, DRIVER_NAME)) {
		printk(KERN_ERR
		       "%s #%d: Couldn't lock memory region at 0x%08lX\n",
		       DRIVER_NAME, pdev->id, regs_res->start);
		retval = -EBUSY;
		goto failed1;
	}

	/* Fill in cfg data and add them to the list */
	drvdata->phys_addr = regs_res->start;
	drvdata->remap_size = remap_size;
	xps2_cfg.DeviceId = pdev->id;
	xps2_cfg.BaseAddress = (u32) ioremap(regs_res->start, remap_size);
	if (xps2_cfg.BaseAddress == 0) {
		printk(KERN_ERR
		       "%s #%d: Couldn't ioremap memory at 0x%08lX\n",
		       DRIVER_NAME, pdev->id, regs_res->start);
		retval = -EFAULT;
		goto failed2;
	}

	/* Tell the Xilinx code to bring this PS/2 interface up. */
	down(&cfg_sem);
	if (XPs2_CfgInitialize(&drvdata->ps2, &xps2_cfg, xps2_cfg.BaseAddress)
	    != XST_SUCCESS) {
		up(&cfg_sem);
		printk(KERN_ERR
		       "%s #%d: Could not initialize device.\n",
		       DRIVER_NAME, pdev->id);
		retval = -ENODEV;
		goto failed3;
	}
	up(&cfg_sem);

	/* Set up the interrupt handler. */
	XPs2_SetHandler(&drvdata->ps2, sxps2_handler, drvdata);

	printk(KERN_INFO "%s #%d at 0x%08X mapped to 0x%08X\n",
	       DRIVER_NAME, pdev->id,
	       drvdata->phys_addr, drvdata->ps2.BaseAddress);

	drvdata->serio.id.type = SERIO_8042;
	drvdata->serio.write = sxps2_write;
	drvdata->serio.open = sxps2_open;
	drvdata->serio.close = sxps2_close;
	drvdata->serio.port_data = drvdata;
	drvdata->serio.dev.parent = dev;
	snprintf(drvdata->serio.name, sizeof(drvdata->serio.name),
		 XPS2_NAME_DESC, pdev->id);
	snprintf(drvdata->serio.phys, sizeof(drvdata->serio.phys),
		 XPS2_PHYS_DESC, pdev->id);
	serio_register_port(&drvdata->serio);

	return 0;		/* success */

      failed3:
	iounmap((void *)(xps2_cfg.BaseAddress));

      failed2:
	release_mem_region(regs_res->start, remap_size);

      failed1:
	kfree(drvdata);
	dev_set_drvdata(dev, NULL);

	return retval;
}

static int xps2_remove(struct device *dev)
{
	struct xps2data *drvdata;

	if (!dev)
		return -EINVAL;

	drvdata = (struct xps2data *)dev_get_drvdata(dev);

	serio_unregister_port(&drvdata->serio);

	iounmap((void *)(drvdata->ps2.BaseAddress));

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

static int __init xps2_init(void)
{
	return driver_register(&xps2_driver);
}

static void __exit xps2_cleanup(void)
{
	driver_unregister(&xps2_driver);
}

module_init(xps2_init);
module_exit(xps2_cleanup);

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL");
