/*
 *  linux/drivers/char/altera_pio_button.c
 *  A simple character driver that takes buttons on Nios Development
 *  Kit as an input device (major 62)
 *  
 *  The characters input can be  '1', '2', '4' or '8'
 *  
 *  Copyright (C) 2004 Microtronix Datacom Ltd
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 * 
 *  Written by Wentao Xu <wentao@microtronix.com>
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/capability.h>
#include <linux/uio.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#define BUTTON_MAJOR	62
int button_major = BUTTON_MAJOR;
int button_minor = 0;

#define PIO_BUTTON_BASE	na_button_pio
#define PIO_BUTTON_IRQ	na_button_pio_irq
#define PIO_BUTTON_SIZE sizeof(np_pio)

#define BUTTON_BUF_SIZE 	100
struct button_dev {
	int count;
	int head;
	int tail;
	char	buf[BUTTON_BUF_SIZE];

	int started;
	struct cdev cdev;
	wait_queue_head_t rxq;
	struct semaphore mutex;
} _button_dev;


static void button_handle_event(void *dev_id)
{
	static int old = 0;
	int status, key;
	struct button_dev * dev=(struct button_dev*)dev_id;
	np_pio* pio = (np_pio *)(PIO_BUTTON_BASE);

	outl(0, &pio->np_pioedgecapture);
	/* read input, check 4 buttons */
	status = (~inl(&pio->np_piodata)) & 0xF;
	key = status - old;
	old = status;
	
	if (key > 0) {
		down(&dev->mutex);
		/* we simply discard new inputs if buffer overflows */
		if (dev->count < BUTTON_BUF_SIZE) {
			dev->buf[dev->tail] = key + '0';
			dev->tail = (dev->tail+1) % BUTTON_BUF_SIZE;
			dev->count++; 
		}
		up(&dev->mutex);
		
		/* wake up any waiting reader */
		if (waitqueue_active(&dev->rxq)) {
			wake_up(&dev->rxq);
		}
	}

	/* re-enable interrupts */
	outl(-1, &pio->np_piointerruptmask);
}

static DECLARE_WORK(button_work, button_handle_event, (void*)&_button_dev);
static irqreturn_t pio_button_isr(int irq, void *dev_id, struct pt_regs *regs)
{
	np_pio* pio = (np_pio *)PIO_BUTTON_BASE;
	
	if (!pio) 
		return IRQ_NONE;

	
	/* disable interrupt */
	outl(0, &pio->np_pioedgecapture);
	outl(0, &pio->np_piointerruptmask);

	/* activate the bottom half */
	schedule_work(&button_work);
	
	return IRQ_HANDLED;
}
static int button_start(struct button_dev *dev)
{
	np_pio *pio=(np_pio *)(PIO_BUTTON_BASE);
	
	outl(0, &pio->np_pioedgecapture);
	outl(0, &pio->np_piodirection); 

	/* register interrupt */
	if (request_irq(PIO_BUTTON_IRQ, pio_button_isr, SA_INTERRUPT, "pio_button", 
					(void*)(dev))) {
		printk("pio_button: unable to register interrupt %d\n", PIO_BUTTON_IRQ); 
		return -1;
	}
	outl(-1, &pio->np_piointerruptmask);

	return 0;
}
/*
 * Open/close .
 */
static int button_open(struct inode *inode, struct file *filp)
{
	struct button_dev *dev;
	
	dev = container_of(inode->i_cdev, struct button_dev, cdev);
	filp->private_data = dev;

	preempt_disable();
	dev->started++;
	if (dev->started!=1) {
		preempt_enable();
		return 0;
	}
		
	/* init buffon info */
	dev->count=0;
	dev->head=0;
	dev->tail=0;
	init_waitqueue_head(&dev->rxq);
	init_MUTEX(&dev->mutex);
	/* init buttons */	
	button_start(dev);
	preempt_enable();
	
	return 0;
}

/*
 */
static int button_release(struct inode *inode, struct file *filp)
{
	np_pio *pio=(np_pio *)(PIO_BUTTON_BASE);
	struct button_dev *dev = (struct button_dev*)filp->private_data;
	
	preempt_disable();
	dev->started--;
	if (dev->started != 0) {
		preempt_enable();
		return 0;
	}
	preempt_enable();
	
	/*disable this interrupts */
	outl(0, &pio->np_piointerruptmask);
	free_irq(PIO_BUTTON_IRQ, (void*)(dev));
	return 0;
}

/*
 */
static int
button_ioctl(struct inode *inode, struct file *filp,
		  unsigned int command, unsigned long arg)
{
	return -EINVAL;
}

static ssize_t button_read(struct file *filp, char *buf,
			 size_t count, loff_t * ppos)
{
	int i, total;
	struct button_dev *dev = (struct button_dev*)filp->private_data;
	
	if (dev->count==0) {
		DEFINE_WAIT(wait);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
			
		while (!signal_pending(current) && (dev->count == 0)) {
			prepare_to_wait(&dev->rxq, &wait, TASK_INTERRUPTIBLE);
			if (!signal_pending(current) && (dev->count == 0))
				schedule();
			finish_wait(&dev->rxq, &wait);
		}
		if (signal_pending(current) && (dev->count == 0))
			return -ERESTARTSYS;
	}

	if (down_interruptible(&dev->mutex))
		return -ERESTARTSYS;
		
	/* return data */
	total = (count < dev->count) ? count : dev->count;
	for (i=0; i < total; i++) {
		put_user(dev->buf[dev->head], buf+i);
		dev->head = (dev->head + 1) % BUTTON_BUF_SIZE;
		dev->count--;
	}	
	up(&dev->mutex);
	
	return total;
}

static unsigned int button_poll(struct file *filp, poll_table *wait)
{
	struct button_dev *dev = (struct button_dev*)filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &dev->rxq, wait);
	
	if (dev->count > 0)
	mask |= POLLIN | POLLRDNORM; /* readable */
	return mask;
}

static struct file_operations button_fops = {
	.read	=	button_read,
	.open	=	button_open,
	.release=	button_release,
	.ioctl	=	button_ioctl,
	.poll	=	button_poll,
	.owner	=	THIS_MODULE,
};


static int __init button_init(void)
{
	int i;
	dev_t devno;
	
	if (!(request_mem_region((unsigned long)PIO_BUTTON_BASE, PIO_BUTTON_SIZE, "pio_button")))
		return -1;
		
	devno = MKDEV(button_major, button_minor);
	cdev_init(&_button_dev.cdev, &button_fops);
	_button_dev.cdev.owner = THIS_MODULE;
	_button_dev.started = 0;
	
	i = register_chrdev_region(devno, 1, "pio_button");
	if (i) {
		printk(KERN_NOTICE "Can't get major %d for PIO buttons", button_major);
		goto error1;
	}
	i = cdev_add(&_button_dev.cdev, devno, 1);
	if (i) {
		printk(KERN_NOTICE "Error %d adding PIO buttons", i);
		goto error2;
	}
error2:
	unregister_chrdev_region(devno, 1);
error1:
	release_mem_region((unsigned long)PIO_BUTTON_BASE, PIO_BUTTON_SIZE);
	
	return i;
}

static void __exit button_exit(void)
{
	cdev_del(&_button_dev.cdev);
	unregister_chrdev_region(MKDEV(button_major, button_minor), 1);
	release_mem_region((unsigned long)PIO_BUTTON_BASE, PIO_BUTTON_SIZE);
}

module_init(button_init);
module_exit(button_exit);
MODULE_LICENSE("GPL");
