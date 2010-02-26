/*
 * drivers/block/xilinx_sysace/xsysace_linux.c
 *
 * Xilinx System ACE xsysace component to interface System ACE to Linux
 *
 * Authors: Dmitry Chigirev  <chigirev@ru.mvista.com>
 *          Sergey Podstavin <spodstavin@ru.mvista.com>
 *
 * 2002-2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * Through System ACE, the processor can access the CompactFlash and the
 * JTAG chain.  In addition, the System ACE controls system reset and
 * which configuration will be loaded into the JTAG chain at that time.
 * This driver provides two different interfaces.  The first is handling
 * reset by tying into the system's reset code as well as providing a
 * /proc interface to read and write which configuration should be used
 * when the system is reset.  The second is to expose a block interface
 * to the CompactFlash.
 *
 * This driver is a bit unusual in that it is composed of two logical
 * parts where one part is the OS independent code and the other part is
 * the OS dependent code.  Xilinx provides their drivers split in this
 * fashion.  This file represents the Linux OS dependent part known as
 * the Linux xsysace.  The other files in this directory are the OS
 * independent files as provided by Xilinx with no changes made to them.
 * The names exported by those files begin with XSysAce_.  All functions
 * in this file that are called by Linux have names that begin with
 * xsysace_.  Any other functions are static helper functions.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/hdreg.h>
#include <linux/slab.h>
#include <linux/blkpg.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/xilinx_devices.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include "xbasic_types.h"
#include "xsysace.h"

static DEFINE_SPINLOCK(xsysace_lock);

/* Use xsa_major to support non-devfs configuration */
static int xsa_major = 125;

#define MAJOR_NAME "xsa"
#define DEVICE_NAME "System ACE"

static u32 xsa_phys_addr;	/* Saved physical base address */
static unsigned long xsa_remap_size;
static int xsa_irq;

static void (*old_restart) (char *cmd) = NULL;	/* old ppc_md.restart */

static unsigned char heads;
static unsigned char sectors;
static unsigned short cylinders;

struct gendisk *xsa_gendisk;

static struct request *xsysace_req;	/* current request */
static struct request_queue *xsysace_queue;	/* current queue */

static void do_read_write(struct work_struct *work);
static DECLARE_WORK(xsysace_read_write_work, do_read_write);

/*
 * The underlying OS independent code needs space as well.  A pointer to
 * the following XSysAce structure will be passed to any XSysAce_
 * function that requires it.
 */
static XSysAce SysAce;

static void xsa_complete_request(int get_uptodate);

/*req_fnc will be XSysAce_SectorRead or XSysAce_SectorWrite.  */
static int (*req_fnc) (XSysAce * InstancePtr, u32 StartSector,
		       int NumSectors, u8 *BufferPtr);

/* req_str will be used for errors and will be either "reading" or "writing" */
static char *req_str;

/*******************************************************************************
 * This configuration stuff should become unnecessary after EDK version 8.x is
 * released.
 ******************************************************************************/

static DECLARE_MUTEX(cfg_sem);

/*
 * The following block of code implements the reset handling.  The first
 * part implements /proc/xsysace/cfgaddr.  When read, it will yield a
 * number from 0 to 7 that represents which configuration will be used
 * next (the configuration address).  Writing a number to it will change
 * the configuration address.  After that is the function that is hooked
 * into the system's reset handler.
 */
#ifndef CONFIG_PROC_FS
#define proc_init() 0
#define proc_cleanup()
#else
#define CFGADDR_NAME "cfgaddr"
static struct proc_dir_entry *xsysace_dir = NULL;
static struct proc_dir_entry *cfgaddr_file = NULL;

static unsigned int XSysAce_GetCfgAddr(XSysAce * InstancePtr)
{
	u32 Status;

	XASSERT_NONVOID(InstancePtr != NULL);
	XASSERT_NONVOID(InstancePtr->IsReady == XCOMPONENT_IS_READY);

	Status = XSysAce_mGetControlReg(InstancePtr->BaseAddress);
	if (!(Status & XSA_CR_FORCECFGADDR_MASK))
		Status = XSysAce_mGetStatusReg(InstancePtr->BaseAddress);

	return (unsigned int) ((Status & XSA_SR_CFGADDR_MASK) >>
			       XSA_CR_CFGADDR_SHIFT);
}

static int cfgaddr_read(char *page, char **start,
			off_t off, int count, int *eof, void *data)
{
	unsigned int cfgaddr;

	/* Make sure we have room for a digit (0-7), a newline and a NULL */
	if (count < 3)
		return -EINVAL;

	cfgaddr = XSysAce_GetCfgAddr(&SysAce);
	count = sprintf(page + off, "%d\n", cfgaddr);
	*eof = 1;
	return count;
}

static int cfgaddr_write(struct file *file,
			 const char *buffer, unsigned long count, void *data)
{
	char val[2];

	if (count < 1 || count > 2)
		return -EINVAL;

	if (copy_from_user(val, buffer, count)) {
		return -EFAULT;
	}

	if (val[0] < '0' || val[0] > '7' || (count == 2 && !(val[1] == '\n' ||
							     val[1] == '\0'))) {
		return -EINVAL;
	}

	XSysAce_SetCfgAddr(&SysAce, val[0] - '0');
	return count;
}

static int proc_init(void)
{
	xsysace_dir = proc_mkdir(MAJOR_NAME, NULL);
	if (!xsysace_dir)
		return -ENOMEM;
	xsysace_dir->owner = THIS_MODULE;

	cfgaddr_file = create_proc_entry(CFGADDR_NAME, 0644, xsysace_dir);
	if (!cfgaddr_file) {
		remove_proc_entry(MAJOR_NAME, NULL);
		return -ENOMEM;
	}
	cfgaddr_file->read_proc = cfgaddr_read;
	cfgaddr_file->write_proc = cfgaddr_write;
	cfgaddr_file->owner = THIS_MODULE;
	return 0;
}

static void proc_cleanup(void)
{
	if (cfgaddr_file)
		remove_proc_entry(CFGADDR_NAME, xsysace_dir);
	if (xsysace_dir)
		remove_proc_entry(MAJOR_NAME, NULL);
}
#endif /* CONFIG_PROC_FS */

static void xsysace_restart(char *cmd)
{
	XSysAce_ResetCfg(&SysAce);

	/* Wait for reset. */
	for (;;);
}

/* Simple function that hands an interrupt to the Xilinx code. */
static irqreturn_t xsysace_interrupt(int irq, void *dev_id)
{
	XSysAce_InterruptHandler(&SysAce);
	return IRQ_HANDLED;
}

void xsysace_end_request(struct request *req, int uptodate)
{
	if (!end_that_request_first(req, uptodate, req->hard_cur_sectors)) {
		blkdev_dequeue_request(req);
		end_that_request_last(req, 1);
	}
}

static void xsa_complete_request(int uptodate)
{
	XSysAce_Unlock(&SysAce);
	spin_lock_irq(&xsysace_lock);
	xsysace_end_request(xsysace_req, uptodate);
	xsysace_req = 0;
	spin_unlock_irq(&xsysace_lock);
	schedule_work(&xsysace_read_write_work);
}

static void do_read_write(struct work_struct *work)
{
	int stat;
	struct request *req;
	request_queue_t *q;

	q = xsysace_queue;
	spin_lock_irq(&xsysace_lock);

	if (blk_queue_plugged(q)) {
		printk(KERN_ERR "XSysAce: Queue is plugged\n");
		spin_unlock_irq(&xsysace_lock);
		return;
	}

	while ((req = elv_next_request(q)) != NULL) {
		if (!blk_fs_request(req)) {
			printk(KERN_NOTICE "Skip non-fs request\n");
			xsysace_end_request(req, 0);
			continue;
		}
		if (rq_data_dir(req) == WRITE) {
			req_str = "writing";
			req_fnc = XSysAce_SectorWrite;
		}
		else {
			req_str = "reading";
			req_fnc = XSysAce_SectorRead;
		}
		xsysace_req = req;
		break;
	}
	spin_unlock_irq(&xsysace_lock);

	if (!req)
		return;

	/* We have a request. */
	while ((stat = XSysAce_Lock(&SysAce, 0)) == XST_DEVICE_BUSY) {
		msleep_interruptible(1);
	}
	if (stat != XST_SUCCESS) {
		printk(KERN_ERR "%s: Error %d when locking.\n",
		       DEVICE_NAME, stat);
		xsa_complete_request(0);	/* Request failed. */
	}

	while ((stat = req_fnc(&SysAce, xsysace_req->sector,
			       xsysace_req->current_nr_sectors,
			       xsysace_req->buffer)) == XST_DEVICE_BUSY) {
		msleep_interruptible(1);
	}
	/*
	 * If the stat is XST_SUCCESS, we have successfully
	 * gotten the request started on the hardware.  The
	 * completion (or error) interrupt will unlock the
	 * CompactFlash and complete the request, so we don't
	 * need to do anything except just loop around and wait
	 * for the next request.  If the status is not
	 * XST_SUCCESS, we need to finish the request with an
	 * error before waiting for the next request.
	 */
	if (stat != XST_SUCCESS) {
		printk(KERN_ERR "%s: Error %d when %s sector %lu.\n",
		       DEVICE_NAME, stat, req_str, xsysace_req->sector);
		xsa_complete_request(0);	/* Request failed. */
	}
}

static void xsysace_do_request(request_queue_t * q)
{
	/* We're already handling a request.  Don't accept another. */
	if (xsysace_req)
		return;
	schedule_work(&xsysace_read_write_work);
}

/* Called by the Xilinx interrupt handler to give us an event. */
static void EventHandler(void *CallbackRef, int Event)
{
	u32 ErrorMask;

	switch (Event) {
	case XSA_EVENT_DATA_DONE:
		xsa_complete_request(1);	/* The request succeeded. */
		break;

	case XSA_EVENT_ERROR:
		ErrorMask = XSysAce_GetErrors(&SysAce);
		/* Print out what went wrong. */
		if (ErrorMask & XSA_ER_CARD_RESET)
			printk(KERN_ERR "CompactFlash failed to reset\n");
		if (ErrorMask & XSA_ER_CARD_READY)
			printk(KERN_ERR "CompactFlash failed to ready\n");
		if (ErrorMask & XSA_ER_CARD_READ)
			printk(KERN_ERR "CompactFlash read command failed\n");
		if (ErrorMask & XSA_ER_CARD_WRITE)
			printk(KERN_ERR "CompactFlash write command failed\n");
		if (ErrorMask & XSA_ER_SECTOR_READY)
			printk(KERN_ERR
			       "CompactFlash sector failed to ready\n");
		if (ErrorMask & XSA_ER_BAD_BLOCK)
			printk(KERN_ERR "CompactFlash bad block detected\n");
		if (ErrorMask & XSA_ER_UNCORRECTABLE)
			printk(KERN_ERR "CompactFlash uncorrectable error\n");
		if (ErrorMask & XSA_ER_SECTOR_ID)
			printk(KERN_ERR "CompactFlash sector ID not found\n");
		if (ErrorMask & XSA_ER_ABORT)
			printk(KERN_ERR "CompactFlash command aborted\n");
		if (ErrorMask & XSA_ER_GENERAL)
			printk(KERN_ERR "CompactFlash general error\n");

		if (ErrorMask & XSA_ER_CFG_READ)
			printk(KERN_ERR
			       "JTAG controller couldn't read configuration from the CompactFlash\n");
		if (ErrorMask & XSA_ER_CFG_ADDR)
			printk(KERN_ERR
			       "Invalid address given to JTAG controller\n");
		if (ErrorMask & XSA_ER_CFG_FAIL)
			printk(KERN_ERR
			       "JTAG controller failed to configure a device\n");
		if (ErrorMask & XSA_ER_CFG_INSTR)
			printk(KERN_ERR
			       "Invalid instruction during JTAG configuration\n");
		if (ErrorMask & XSA_ER_CFG_INIT)
			printk(KERN_ERR "JTAG CFGINIT pin error\n");

		/* Check for errors that should reset the CompactFlash */
		if (ErrorMask & (XSA_ER_CARD_RESET |
				 XSA_ER_CARD_READY |
				 XSA_ER_CARD_READ |
				 XSA_ER_CARD_WRITE |
				 XSA_ER_SECTOR_READY |
				 XSA_ER_BAD_BLOCK |
				 XSA_ER_UNCORRECTABLE |
				 XSA_ER_SECTOR_ID | XSA_ER_ABORT |
				 XSA_ER_GENERAL)) {
			if (XSysAce_ResetCF(&SysAce) != XST_SUCCESS)
				printk(KERN_ERR
				       "Could not reset CompactFlash\n");
			xsa_complete_request(0);	/* The request failed. */
		}
		break;
	case XSA_EVENT_CFG_DONE:
		printk(KERN_WARNING "XSA_EVENT_CFG_DONE not handled yet.\n");
		break;
	default:
		printk(KERN_ERR "%s: unrecognized event %d\n",
		       DEVICE_NAME, Event);
		break;
	}
}

static int
xsysace_ioctl(struct inode *inode, struct file *file,
	      unsigned int cmd, unsigned long arg)
{
	struct hd_geometry __user *geo = (struct hd_geometry __user *) arg;
	struct hd_geometry g;

	switch (cmd) {
	case HDIO_GETGEO:
		{
			g.heads = heads;
			g.sectors = sectors;
			g.cylinders = cylinders;
			g.start = 0;
			return copy_to_user(geo, &g, sizeof(g)) ? -EFAULT : 0;
		}
	default:
		return -ENOTTY;
	}
}

static struct block_device_operations xsysace_fops = {
	.owner = THIS_MODULE,
	.ioctl = xsysace_ioctl,
};

/******************************
 * The platform device driver *
 ******************************/

/*
 * Currently the driver supports just one System ACE device.
 * Most of the code below could be easily extended to handle
 * several devices except for proc_init()/proc_cleanup() and
 * ppc_md.restart handling.
 */

#define DRIVER_NAME		"xsysace"

static int xsysace_probe(struct device *dev)
{
	XSysAce_Config xsysace_cfg;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *irq_res, *regs_res;
	unsigned long remap_size;
	int stat;
	long size;
	XSysAce_CFParameters ident;
	int retval;

	if (!dev)
		return -EINVAL;

	/* Find irq number, map the control registers in */
	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	regs_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs_res || !irq_res) {
		printk(KERN_ERR "%s #%d: IO resource(s) not found\n",
		       DRIVER_NAME, pdev->id);
		retval = -EFAULT;
		goto failed1;
	}
	xsa_irq = irq_res->start;
	xsa_phys_addr = regs_res->start;
	remap_size = regs_res->end - regs_res->start + 1;
	if (!request_mem_region(regs_res->start, remap_size, DRIVER_NAME)) {
		printk(KERN_ERR
		       "%s #%d: Couldn't lock memory region at 0x%08X\n",
		       DRIVER_NAME, pdev->id, regs_res->start);
		retval = -EBUSY;
		goto failed1;
	}

	/* Fill in cfg data and add them to the list */
	xsa_remap_size = remap_size;
	xsysace_cfg.DeviceId = pdev->id;
	xsysace_cfg.BaseAddress = (u32) ioremap(regs_res->start, remap_size);
	if (xsysace_cfg.BaseAddress == 0) {
		printk(KERN_ERR
		       "%s #%d: Couldn't ioremap memory at 0x%08X\n",
		       DRIVER_NAME, pdev->id, regs_res->start);
		retval = -EFAULT;
		goto failed2;
	}

	/* Tell the Xilinx code to bring this SystemACE interface up. */
	down(&cfg_sem);
	if (XSysAce_CfgInitialize
	    (&SysAce, &xsysace_cfg, xsysace_cfg.BaseAddress) != XST_SUCCESS) {
		up(&cfg_sem);
		printk(KERN_ERR
		       "%s #%d: Could not initialize device.\n",
		       DRIVER_NAME, pdev->id);
		retval = -ENODEV;
		goto failed3;
	}
	up(&cfg_sem);

	retval = request_irq(xsa_irq, xsysace_interrupt, 0, DEVICE_NAME, NULL);
	if (retval) {
		printk(KERN_ERR
		       "%s #%d: Couldn't allocate interrupt %d.\n",
		       DRIVER_NAME, pdev->id, xsa_irq);
		goto failed3;
	}

	XSysAce_SetEventHandler(&SysAce, EventHandler, (void *) NULL);
	XSysAce_EnableInterrupt(&SysAce);

	/* Time to identify the drive. */
	while (XSysAce_Lock(&SysAce, 0) == XST_DEVICE_BUSY);
	while ((stat = XSysAce_IdentifyCF(&SysAce, &ident)) == XST_DEVICE_BUSY);
	XSysAce_Unlock(&SysAce);
	if (stat != XST_SUCCESS) {
		printk(KERN_ERR "%s: Could not send identify command.\n",
		       DEVICE_NAME);
		retval = -ENODEV;
		goto failed4;
	}

	/* Fill in what we learned. */
	heads = ident.NumHeads;
	sectors = ident.NumSectorsPerTrack;
	cylinders = ident.NumCylinders;
	size = (long) cylinders *(long) heads *(long) sectors;

	xsysace_queue = blk_init_queue(xsysace_do_request, &xsysace_lock);
	if (!xsysace_queue) {
		retval = -ENODEV;
		goto failed4;
	}

	if (register_blkdev(xsa_major, MAJOR_NAME)) {
		retval = -EBUSY;
		goto failed5;
	}

	xsa_gendisk = alloc_disk(16);
	if (!xsa_gendisk) {
		retval = -ENODEV;
		goto failed6;
	}

	strcpy(xsa_gendisk->disk_name, MAJOR_NAME);
	xsa_gendisk->fops = &xsysace_fops;
	xsa_gendisk->major = xsa_major;
	xsa_gendisk->first_minor = 0;
	xsa_gendisk->minors = 16;
	xsa_gendisk->queue = xsysace_queue;

	set_capacity(xsa_gendisk, size);

	printk(KERN_INFO
	       "%s at 0x%08X mapped to 0x%08X, irq=%d, %ldKB\n",
	       DEVICE_NAME, xsa_phys_addr, SysAce.BaseAddress, xsa_irq,
	       size / 2);

	/* Hook our reset function into system's restart code. */
	if (old_restart == NULL) {
		old_restart = ppc_md.restart;
		ppc_md.restart = xsysace_restart;
	}

	if (proc_init())
		printk(KERN_WARNING "%s: could not register /proc interface.\n",
		       DEVICE_NAME);

	add_disk(xsa_gendisk);

	return 0;		/* success */

      failed6:
	unregister_blkdev(xsa_major, MAJOR_NAME);

      failed5:
	blk_cleanup_queue(xsysace_queue);

      failed4:
	XSysAce_DisableInterrupt(&SysAce);
	free_irq(xsa_irq, NULL);

      failed3:
	iounmap((void *) (xsysace_cfg.BaseAddress));

      failed2:
	release_mem_region(regs_res->start, remap_size);

      failed1:
	return retval;
}

static int xsysace_remove(struct device *dev)
{
	if (!dev)
		return -EINVAL;

	proc_cleanup();

	if (old_restart)
		ppc_md.restart = old_restart;

	unregister_blkdev(xsa_major, MAJOR_NAME);
	del_gendisk(xsa_gendisk);
	blk_cleanup_queue(xsysace_queue);
	XSysAce_DisableInterrupt(&SysAce);
	free_irq(xsa_irq, NULL);
	iounmap((void *) (SysAce.BaseAddress));
	release_mem_region(xsa_phys_addr, xsa_remap_size);

	return 0;		/* success */
}

static struct device_driver xsysace_driver = {
	.name = DRIVER_NAME,
	.bus = &platform_bus_type,
	.probe = xsysace_probe,
	.remove = xsysace_remove
};

static int __init xsysace_init(void)
{
	return driver_register(&xsysace_driver);
}

static void __exit xsysace_cleanup(void)
{
	driver_unregister(&xsysace_driver);
}

module_init(xsysace_init);
module_exit(xsysace_cleanup);

MODULE_AUTHOR
	("Dmitry Chigirev  <chigirev@ru.mvista.com>, Sergey Podstavin <spodstavin@ru.mvista.com>");
MODULE_DESCRIPTION("Xilinx System ACE block driver");
MODULE_LICENSE("GPL");
