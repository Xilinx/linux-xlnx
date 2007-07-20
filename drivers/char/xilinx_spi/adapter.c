/*
 * xspi_adapter.c
 *
 * Xilinx Adapter component to interface SPI component to Linux
 *
 * Only master mode is supported. One or more slaves can be served.
 *
 * Author: MontaVista Software, Inc.
 *         akonovalov@ru.mvista.com, or source@mvista.com
 *
 * 2004-2006 (c) MontaVista, Software, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>	/* wait_event_interruptible */
#include <linux/bitops.h>	/* ffs() */
#include <linux/slab.h>		/* kmalloc() etc. */
#include <linux/moduleparam.h>
#include <linux/xilinx_devices.h>
#include <linux/device.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/page.h>		/* PAGE_SIZE */

#include "xspi.h"
#include "xspi_i.h"
#include "xspi_ioctl.h"

#define XSPI_DEFAULT_MAJOR	123
#define XSPI_DEFAULT_MINOR	0	/* The minors start from this value */
#define XSPI_MINORS		4	/* Allocate 4 minors for this driver */

static int xspi_major = XSPI_DEFAULT_MAJOR;
static int xspi_minor = XSPI_DEFAULT_MINOR;
static int xspi_no_minors = XSPI_MINORS;
module_param(xspi_major, int, S_IRUGO);
module_param(xspi_minor, int, S_IRUGO);

#define XSPI_NAME "xilinx_spi"

/*
 * Debugging macros
 */

#define DEBUG_FLOW   0x0001
#define DEBUG_STAT   0x0002

#define DEBUG_MASK   0x0000

#if (DEBUG_MASK != 0)
#define d_printk(str...)  printk(str)
#else
#define d_printk(str...)	/* nothing */
#endif

#if ((DEBUG_MASK & DEBUG_FLOW) != 0)
#define func_enter()      printk("xspi: enter %s\n", __FUNCTION__)
#define func_exit()       printk("xspi: exit  %s\n", __FUNCTION__)
#else
#define func_enter()
#define func_exit()
#endif

/* These options are always set by the driver. */
#define XSPI_DEFAULT_OPTIONS	(XSP_MASTER_OPTION | XSP_MANUAL_SSELECT_OPTION)
/* These options can be changed by the user. */
#define XSPI_CHANGEABLE_OPTIONS	(XSP_CLK_ACTIVE_LOW_OPTION | XSP_CLK_PHASE_1_OPTION \
				| XSP_LOOPBACK_OPTION)

/* Our private per interface data. */
struct xspi_instance {
	u32 phys_addr;		/* Saved physical base address */
	ulong remap_size;
	u32 device_id;
	unsigned int irq;	/* device IRQ number */
	wait_queue_head_t waitq;	/* For those waiting until SPI is busy */
	struct semaphore sem;
	int use_count;

	struct cdev cdev;	/* Char device structure */

	/* The flag ISR uses to tell the transfer completion status
	 * (the values are defined in "xstatus.h"; set to 0 before the transfer) */
	int completion_status;
	/* The actual number of bytes transferred */
	int tx_count;

	/* The object used by Xilinx OS independent code */
	XSpi Spi;
};

/*******************************************************************************
 * This configuration stuff should become unnecessary after EDK version 8.x is
 * released.
 ******************************************************************************/

static DECLARE_MUTEX(cfg_sem);

static int convert_status(int status)
{
	switch (status) {
	case XST_SUCCESS:
		return 0;
	case XST_DEVICE_NOT_FOUND:
		return -ENODEV;
	case XST_DEVICE_BUSY:
		return -EBUSY;
	default:
		return -EIO;
	}
}

/*
 * Simple function that hands an interrupt to the Xilinx code.
 * dev_id contains a pointer to proper XSpi instance.
 */
static irqreturn_t xspi_isr(int irq, void *dev_id, struct pt_regs *regs)
{
	XSpi_InterruptHandler((XSpi *) dev_id);
	return IRQ_HANDLED;
}

/*
 * This function is called back from the XSpi interrupt handler
 * when one of the following status events occures:
 * 	XST_SPI_TRANSFER_DONE - the requested data transfer is done,
 * 	XST_SPI_RECEIVE_OVERRUN - Rx FIFO overrun, transmission continues,
 * 	XST_SPI_MODE_FAULT - should not happen: the driver doesn't support multiple masters,
 * 	XST_SPI_TRANSMIT_UNDERRUN,
 * 	XST_SPI_SLAVE_MODE_FAULT - should not happen: the driver doesn't support slave mode.
 */
static void xspi_status_handler(void *CallBackRef, u32 StatusEvent,
				unsigned int ByteCount)
{
	struct xspi_instance *dev = (struct xspi_instance *) CallBackRef;

	dev->completion_status = StatusEvent;

	if (StatusEvent == XST_SPI_TRANSFER_DONE) {
		dev->tx_count = (int) ByteCount;
		wake_up_interruptible(&dev->waitq);
	}
	else if (StatusEvent == XST_SPI_RECEIVE_OVERRUN) {
		/* As both Rx and Tx FIFO have the same sizes
		   this should not happen in master mode.
		   That is why we consider Rx overrun as severe error
		   and abort the transfer */
		dev->tx_count = (int) ByteCount;
		XSpi_Abort(&dev->Spi);
		wake_up_interruptible(&dev->waitq);
		printk(KERN_ERR XSPI_NAME " %d: Rx overrun!!!.\n",
		       dev->device_id);
	}
	else if (StatusEvent == XST_SPI_MODE_FAULT) {
		wake_up_interruptible(&dev->waitq);
	}
	else {
		printk(KERN_ERR XSPI_NAME " %d: Invalid status event %u.\n",
		       dev->device_id, (u32) StatusEvent);
	}
}

/*
 * To be called from xspi_ioctl(), xspi_read(), and xspi_write().
 *
 * xspi_ioctl() uses both wr_buf and rd_buf.
 * xspi_read() doesn't care of what is sent, and sets wr_buf to NULL.
 * xspi_write() doesn't care of what it receives, and sets rd_buf to NULL.
 *
 * Set slave_ind to negative value if the currently selected SPI slave
 * device is to be used.
 *
 * Returns the number of bytes transferred (0 or positive value)
 * or error code (negative value).
 */
static int xspi_transfer(struct xspi_instance *dev, const char *wr_buf,
			 char *rd_buf, int count, int slave_ind)
{
	int retval;
	unsigned char *tmp_buf;

	if (count <= 0)
		return 0;

	/* Limit the count value to the small enough one.
	   This prevents a denial-of-service attack by using huge count values
	   thus making everything to be swapped out to free the space
	   for this huge buffer */
	if (count > 8192)
		count = 8192;

	/* Allocate buffer in the kernel space (it is first filled with
	   the data to send, then these data are overwritten with the
	   received data) */
	tmp_buf = kmalloc(count, GFP_KERNEL);
	if (tmp_buf == NULL)
		return -ENOMEM;

	/* Fill the buffer with data to send */
	if (wr_buf == NULL) {
		/* zero the buffer not to expose the kernel data */
		memset(tmp_buf, 0, count);
	}
	else {
		if (copy_from_user(tmp_buf, wr_buf, count) != 0) {
			kfree(tmp_buf);
			return -EFAULT;
		}
	}

	/* Lock the device */
	if (down_interruptible(&dev->sem)) {
		kfree(tmp_buf);
		return -ERESTARTSYS;
	}

	/* The while cycle below never loops - this is just a convenient
	   way to handle the errors */
	while (TRUE) {
		/* Select the proper slave if requested to do so */
		if (slave_ind >= 0) {
			retval = convert_status(XSpi_SetSlaveSelect
						(&dev->Spi,
						 0x00000001 << slave_ind));
			if (retval != 0)
				break;
		}

		/* Initiate transfer */
		dev->completion_status = 0;
		retval = convert_status(XSpi_Transfer(&dev->Spi, tmp_buf,
						      (rd_buf ==
						       NULL) ? NULL : tmp_buf,
						      count));
		if (retval != 0)
			break;

		/* Put the process to sleep */
		if (wait_event_interruptible(dev->waitq,
					     dev->completion_status != 0) !=
		    0) {
			/* ... woken up by the signal */
			retval = -ERESTARTSYS;
			break;
		}
		/* ... woken up by the transfer completed interrupt */
		if (dev->completion_status != XST_SPI_TRANSFER_DONE) {
			retval = -EIO;
			break;
		}

		/* Copy the received data to user if rd_buf != NULL */
		if (rd_buf != NULL &&
		    copy_to_user(rd_buf, tmp_buf, dev->tx_count) != 0) {
			retval = -EFAULT;
			break;
		}

		retval = dev->tx_count;
		break;
	}			/* while(TRUE) */

	/* Unlock the device, free the buffer and return */
	up(&dev->sem);
	kfree(tmp_buf);
	return retval;
}

static int
xspi_ioctl(struct inode *inode, struct file *filp,
	   unsigned int cmd, unsigned long arg)
{
	struct xspi_instance *dev = filp->private_data;

	/* paranoia check */
	if (!dev)
		return -ENODEV;

	switch (cmd) {
	case XSPI_IOC_GETSLAVESELECT:
		{
			int i;

			i = ffs(XSpi_GetSlaveSelect(&dev->Spi)) - 1;
			return put_user(i, (int *) arg);	/* -1 means nothing selected */
		}
		break;
	case XSPI_IOC_SETSLAVESELECT:
		{
			int i;
			int retval;

			if (get_user(i, (int *) arg) != 0)
				return -EFAULT;

			if (i < -1 || i > 31)
				return -EINVAL;

			/* Lock the device. */
			if (down_interruptible(&dev->sem))
				return -ERESTARTSYS;

			if (i == -1)
				retval = convert_status(XSpi_SetSlaveSelect
							(&dev->Spi, 0));
			else
				retval = convert_status(XSpi_SetSlaveSelect
							(&dev->Spi,
							 (u32) 1 << i));

			/* Unlock the device. */
			up(&dev->sem);

			return retval;
		}
		break;
	case XSPI_IOC_GETOPTS:
		{
			struct xspi_ioc_options xspi_opts;
			u32 xspi_options;

			xspi_options = XSpi_GetOptions(&dev->Spi);

			memset(&xspi_opts, 0, sizeof(xspi_opts));
			if (dev->Spi.HasFifos)
				xspi_opts.has_fifo = 1;
			if (xspi_options & XSP_CLK_ACTIVE_LOW_OPTION)
				xspi_opts.clk_level = 1;
			if (xspi_options & XSP_CLK_PHASE_1_OPTION)
				xspi_opts.clk_phase = 1;
			if (xspi_options & XSP_LOOPBACK_OPTION)
				xspi_opts.loopback = 1;
			xspi_opts.slave_selects = dev->Spi.NumSlaveBits;

			return put_user(xspi_opts,
					(struct xspi_ioc_options *) arg);
		}
		break;
	case XSPI_IOC_SETOPTS:
		{
			struct xspi_ioc_options xspi_opts;
			u32 xspi_options;
			int retval;

			if (copy_from_user(&xspi_opts,
					   (struct xspi_ioc_options *) arg,
					   sizeof(struct xspi_ioc_options)) !=
			    0)
				return -EFAULT;

			/* Lock the device. */
			if (down_interruptible(&dev->sem))
				return -ERESTARTSYS;

			/* Read current settings and set the changeable ones. */
			xspi_options = XSpi_GetOptions(&dev->Spi)
				& ~XSPI_CHANGEABLE_OPTIONS;
			if (xspi_opts.clk_level != 0)
				xspi_options |= XSP_CLK_ACTIVE_LOW_OPTION;
			if (xspi_opts.clk_phase != 0)
				xspi_options |= XSP_CLK_PHASE_1_OPTION;
			if (xspi_opts.loopback != 0)
				xspi_options |= XSP_LOOPBACK_OPTION;

			retval = convert_status(XSpi_SetOptions
						(&dev->Spi, xspi_options));

			/* Unlock the device. */
			up(&dev->sem);

			return retval;
		}
		break;
	case XSPI_IOC_TRANSFER:
		{
			struct xspi_ioc_transfer_data trans_data;
			int retval;

			if (copy_from_user(&trans_data,
					   (struct xspi_ioc_transfer_data *)
					   arg,
					   sizeof(struct
						  xspi_ioc_transfer_data)) != 0)
				return -EFAULT;

			/* Transfer the data. */
			retval = xspi_transfer(dev, trans_data.write_buf,
					       trans_data.read_buf,
					       trans_data.count,
					       trans_data.slave_index);
			if (retval > 0)
				return 0;
			else
				return retval;
		}
		break;
	default:
		return -ENOTTY;	/* redundant */
	}			/* switch(cmd) */

	return -ENOTTY;
}

static ssize_t
xspi_read(struct file *filp, char *buf, size_t count, loff_t * not_used)
{
	struct xspi_instance *dev = filp->private_data;

	/* Set the 2nd arg to NULL to indicate we don't care what to send;
	   set the last arg to -1 to talk to the currently selected SPI
	   slave */
	return xspi_transfer(dev, NULL, buf, count, -1);
}

static ssize_t
xspi_write(struct file *filp, const char *buf, size_t count, loff_t * not_used)
{
	struct xspi_instance *dev = filp->private_data;

	/* Set the 3d arg to NULL to indicate we are not interested in
	   the data read; set the last arg to -1 to talk to the currently
	   selected SPI slave */
	return xspi_transfer(dev, buf, NULL, count, -1);
}

static int xspi_open(struct inode *inode, struct file *filp)
{
	int retval = 0;
	struct xspi_instance *dev;

	func_enter();

	dev = container_of(inode->i_cdev, struct xspi_instance, cdev);

	filp->private_data = dev;	/* for other methods */

	if (dev == NULL)
		return -ENODEV;

	if (down_interruptible(&dev->sem))
		return -EINTR;

	while (dev->use_count++ == 0) {
		/*
		 * This was the first opener; we need  to get the IRQ,
		 * and to setup the device as master.
		 */
		retval = request_irq(dev->irq, xspi_isr, 0, XSPI_NAME,
				     &dev->Spi);
		if (retval != 0) {
			printk(KERN_ERR XSPI_NAME
			       "%d: Could not allocate interrupt %d.\n",
			       dev->device_id, dev->irq);
			break;
		}

		if (XSpi_SetOptions(&dev->Spi, XSPI_DEFAULT_OPTIONS) !=
		    XST_SUCCESS) {
			printk(KERN_ERR XSPI_NAME
			       "%d: Could not set device options.\n",
			       dev->device_id);
			free_irq(dev->irq, &dev->Spi);
			retval = -EIO;
			break;
		}

		if (XSpi_Start(&dev->Spi) != XST_SUCCESS) {
			printk(KERN_ERR XSPI_NAME
			       "%d: Could not start the device.\n",
			       dev->device_id);
			free_irq(dev->irq, &dev->Spi);
			retval = -EIO;
			break;
		}

		break;
	}

	if (retval != 0)
		--dev->use_count;

	up(&dev->sem);
	return retval;
}

static int xspi_release(struct inode *inode, struct file *filp)
{
	struct xspi_instance *dev = filp->private_data;

	func_enter();

	if (down_interruptible(&dev->sem))
		return -EINTR;

	if (--dev->use_count == 0) {
		/* This was the last closer: stop the device and free the IRQ */
		if (wait_event_interruptible(dev->waitq,
					     XSpi_Stop(&dev->Spi) !=
					     XST_DEVICE_BUSY) != 0) {
			/* Abort transfer by brute force */
			XSpi_Abort(&dev->Spi);
		}
		disable_irq(dev->irq);
		free_irq(dev->irq, &dev->Spi);
	}

	up(&dev->sem);
	return 0;
}

struct file_operations xspi_fops = {
	.open = xspi_open,
	.release = xspi_release,
	.read = xspi_read,
	.write = xspi_write,
	.ioctl = xspi_ioctl,
};

static int __init check_spi_config(XSpi_Config * cfg)
{
	if (cfg->SlaveOnly || cfg->NumSlaveBits == 0)
		return -1;
	else
		return 0;	/* the configuration is supported by this driver */
}

/******************************
 * The platform device driver *
 ******************************/

static int xspi_probe(struct device *dev)
{
	dev_t devt;
	XSpi_Config xspi_cfg;
	struct platform_device *pdev = to_platform_device(dev);
	struct xspi_platform_data *pdata;
	struct xspi_instance *inst;
	struct resource *irq_res, *regs_res;
	unsigned long remap_size;
	u32 virtaddr;
	int retval;

	if (!dev)
		return -EINVAL;

	pdata = (struct xspi_platform_data *) pdev->dev.platform_data;
	if (!pdata) {
		printk(KERN_ERR XSPI_NAME " %d: Couldn't find platform data.\n",
		       pdev->id);

		return -ENODEV;
	}

	devt = MKDEV(xspi_major, xspi_minor + pdev->id);

	inst = kmalloc(sizeof(struct xspi_instance), GFP_KERNEL);
	memset(inst, 0, sizeof(*inst));

	if (!inst) {
		printk(KERN_ERR XSPI_NAME " #%d: Could not allocate device.\n",
		       pdev->id);
		return -ENOMEM;
	}
	dev_set_drvdata(dev, (void *) inst);
	init_MUTEX(&inst->sem);
	init_waitqueue_head(&inst->waitq);

	/* Find irq number, map the control registers in */

	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	regs_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs_res || !irq_res) {
		printk(KERN_ERR XSPI_NAME " #%d: IO resource(s) not found\n",
		       pdev->id);
		retval = -EFAULT;
		goto failed1;
	}
	inst->irq = irq_res->start;

	remap_size = regs_res->end - regs_res->start + 1;
	if (!request_mem_region(regs_res->start, remap_size, XSPI_NAME)) {
		printk(KERN_ERR XSPI_NAME
		       " #%d: Couldn't lock memory region at 0x%08lX\n",
		       pdev->id, regs_res->start);
		retval = -EBUSY;
		goto failed1;
	}
	inst->remap_size = remap_size;
	inst->phys_addr = regs_res->start;
	inst->device_id = pdev->id;
	xspi_cfg.DeviceId = pdev->id;
	xspi_cfg.HasFifos = (pdata->device_flags & XSPI_HAS_FIFOS) ? 1 : 0;
	xspi_cfg.SlaveOnly = (pdata->device_flags & XSPI_SLAVE_ONLY) ? 1 : 0;
	xspi_cfg.NumSlaveBits = pdata->num_slave_bits;

	if (check_spi_config(&xspi_cfg)) {
		printk(KERN_ERR XSPI_NAME
		       " #%d: Unsupported hardware configuration\n", pdev->id);
		retval = -ENODEV;
		goto failed1;
	}

	virtaddr = (u32) ioremap(regs_res->start, remap_size);
	if (virtaddr == 0) {
		printk(KERN_ERR XSPI_NAME
		       " #%d: Couldn't ioremap memory at 0x%08lX\n",
		       pdev->id, regs_res->start);
		retval = -EFAULT;
		goto failed2;
	}

	/* Tell the Xilinx code to bring this SPI interface up. */
	if (XSpi_CfgInitialize(&inst->Spi, &xspi_cfg, virtaddr) != XST_SUCCESS) {
		printk(KERN_ERR XSPI_NAME
		       " #%d: Could not initialize device.\n", pdev->id);
		retval = -ENODEV;
		goto failed3;
	}

	/* Set interrupt callback */
	XSpi_SetStatusHandler(&inst->Spi, inst, xspi_status_handler);
	/* request_irq() is done in open() */

	cdev_init(&inst->cdev, &xspi_fops);
	inst->cdev.owner = THIS_MODULE;
	retval = cdev_add(&inst->cdev, devt, 1);
	if (retval) {
		printk(KERN_ERR XSPI_NAME " #%d: cdev_add() failed\n",
		       pdev->id);
		goto failed3;
	}

	printk(KERN_INFO XSPI_NAME
	       " %d: at 0x%08X mapped to 0x%08X, irq=%d\n",
	       pdev->id, inst->phys_addr, (u32) inst->Spi.BaseAddr, inst->irq);

	return 0;		/* success */

failed3:
	iounmap((void *) (xspi_cfg.BaseAddress));

failed2:
	release_mem_region(regs_res->start, remap_size);

failed1:
	kfree(inst);

	return retval;
}

static int xspi_remove(struct device *dev)
{
	struct xspi_instance *inst;

	if (!dev)
		return -EINVAL;

	inst = (struct xspi_instance *) dev_get_drvdata(dev);

	cdev_del(&inst->cdev);
	iounmap((void *) (inst->Spi.BaseAddr));
	release_mem_region(inst->phys_addr, inst->remap_size);
	kfree(inst);
	dev_set_drvdata(dev, NULL);

	return 0;		/* success */
}

static struct device_driver xspi_driver = {
	.name = XSPI_NAME,
	.bus = &platform_bus_type,
	.probe = xspi_probe,
	.remove = xspi_remove
};

static int __init xspi_init(void)
{
	dev_t devt;
	int retval;

	if (xspi_major) {
		devt = MKDEV(xspi_major, xspi_minor);
		retval = register_chrdev_region(devt, xspi_no_minors,
						XSPI_NAME);
	}
	else {
		retval = alloc_chrdev_region(&devt, xspi_minor, xspi_no_minors,
					     XSPI_NAME);
		xspi_major = MAJOR(devt);
	}
	if (retval < 0) {
		xspi_major = 0;
		return retval;
	}

	retval = driver_register(&xspi_driver);
	if (retval) {
		unregister_chrdev_region(devt, xspi_no_minors);
	}

	return retval;
}

static void __exit xspi_cleanup(void)
{
	dev_t devt = MKDEV(xspi_major, xspi_minor);

	driver_unregister(&xspi_driver);
	unregister_chrdev_region(devt, xspi_no_minors);
}

module_init(xspi_init);
module_exit(xspi_cleanup);

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION("Xilinx SPI driver");
MODULE_LICENSE("GPL");
