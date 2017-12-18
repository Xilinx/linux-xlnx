/*
 * Copyright (C) 2015, 2016 IBM Corporation
 *
 * Author: Stefan Berger <stefanb@us.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Device driver for vTPM (vTPM proxy driver)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 */

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/miscdevice.h>
#include <linux/vtpm_proxy.h>
#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <linux/poll.h>
#include <linux/compat.h>

#include "tpm.h"

#define VTPM_PROXY_REQ_COMPLETE_FLAG  BIT(0)

struct proxy_dev {
	struct tpm_chip *chip;

	u32 flags;                   /* public API flags */

	wait_queue_head_t wq;

	struct mutex buf_lock;       /* protect buffer and flags */

	long state;                  /* internal state */
#define STATE_OPENED_FLAG        BIT(0)
#define STATE_WAIT_RESPONSE_FLAG BIT(1)  /* waiting for emulator response */

	size_t req_len;              /* length of queued TPM request */
	size_t resp_len;             /* length of queued TPM response */
	u8 buffer[TPM_BUFSIZE];      /* request/response buffer */

	struct work_struct work;     /* task that retrieves TPM timeouts */
};

/* all supported flags */
#define VTPM_PROXY_FLAGS_ALL  (VTPM_PROXY_FLAG_TPM2)

static struct workqueue_struct *workqueue;

static void vtpm_proxy_delete_device(struct proxy_dev *proxy_dev);

/*
 * Functions related to 'server side'
 */

/**
 * vtpm_proxy_fops_read - Read TPM commands on 'server side'
 *
 * Return value:
 *	Number of bytes read or negative error code
 */
static ssize_t vtpm_proxy_fops_read(struct file *filp, char __user *buf,
				    size_t count, loff_t *off)
{
	struct proxy_dev *proxy_dev = filp->private_data;
	size_t len;
	int sig, rc;

	sig = wait_event_interruptible(proxy_dev->wq,
		proxy_dev->req_len != 0 ||
		!(proxy_dev->state & STATE_OPENED_FLAG));
	if (sig)
		return -EINTR;

	mutex_lock(&proxy_dev->buf_lock);

	if (!(proxy_dev->state & STATE_OPENED_FLAG)) {
		mutex_unlock(&proxy_dev->buf_lock);
		return -EPIPE;
	}

	len = proxy_dev->req_len;

	if (count < len) {
		mutex_unlock(&proxy_dev->buf_lock);
		pr_debug("Invalid size in recv: count=%zd, req_len=%zd\n",
			 count, len);
		return -EIO;
	}

	rc = copy_to_user(buf, proxy_dev->buffer, len);
	memset(proxy_dev->buffer, 0, len);
	proxy_dev->req_len = 0;

	if (!rc)
		proxy_dev->state |= STATE_WAIT_RESPONSE_FLAG;

	mutex_unlock(&proxy_dev->buf_lock);

	if (rc)
		return -EFAULT;

	return len;
}

/**
 * vtpm_proxy_fops_write - Write TPM responses on 'server side'
 *
 * Return value:
 *	Number of bytes read or negative error value
 */
static ssize_t vtpm_proxy_fops_write(struct file *filp, const char __user *buf,
				     size_t count, loff_t *off)
{
	struct proxy_dev *proxy_dev = filp->private_data;

	mutex_lock(&proxy_dev->buf_lock);

	if (!(proxy_dev->state & STATE_OPENED_FLAG)) {
		mutex_unlock(&proxy_dev->buf_lock);
		return -EPIPE;
	}

	if (count > sizeof(proxy_dev->buffer) ||
	    !(proxy_dev->state & STATE_WAIT_RESPONSE_FLAG)) {
		mutex_unlock(&proxy_dev->buf_lock);
		return -EIO;
	}

	proxy_dev->state &= ~STATE_WAIT_RESPONSE_FLAG;

	proxy_dev->req_len = 0;

	if (copy_from_user(proxy_dev->buffer, buf, count)) {
		mutex_unlock(&proxy_dev->buf_lock);
		return -EFAULT;
	}

	proxy_dev->resp_len = count;

	mutex_unlock(&proxy_dev->buf_lock);

	wake_up_interruptible(&proxy_dev->wq);

	return count;
}

/*
 * vtpm_proxy_fops_poll: Poll status on 'server side'
 *
 * Return value:
 *      Poll flags
 */
static unsigned int vtpm_proxy_fops_poll(struct file *filp, poll_table *wait)
{
	struct proxy_dev *proxy_dev = filp->private_data;
	unsigned ret;

	poll_wait(filp, &proxy_dev->wq, wait);

	ret = POLLOUT;

	mutex_lock(&proxy_dev->buf_lock);

	if (proxy_dev->req_len)
		ret |= POLLIN | POLLRDNORM;

	if (!(proxy_dev->state & STATE_OPENED_FLAG))
		ret |= POLLHUP;

	mutex_unlock(&proxy_dev->buf_lock);

	return ret;
}

/*
 * vtpm_proxy_fops_open - Open vTPM device on 'server side'
 *
 * Called when setting up the anonymous file descriptor
 */
static void vtpm_proxy_fops_open(struct file *filp)
{
	struct proxy_dev *proxy_dev = filp->private_data;

	proxy_dev->state |= STATE_OPENED_FLAG;
}

/**
 * vtpm_proxy_fops_undo_open - counter-part to vtpm_fops_open
 *
 * Call to undo vtpm_proxy_fops_open
 */
static void vtpm_proxy_fops_undo_open(struct proxy_dev *proxy_dev)
{
	mutex_lock(&proxy_dev->buf_lock);

	proxy_dev->state &= ~STATE_OPENED_FLAG;

	mutex_unlock(&proxy_dev->buf_lock);

	/* no more TPM responses -- wake up anyone waiting for them */
	wake_up_interruptible(&proxy_dev->wq);
}

/*
 * vtpm_proxy_fops_release: Close 'server side'
 *
 * Return value:
 *      Always returns 0.
 */
static int vtpm_proxy_fops_release(struct inode *inode, struct file *filp)
{
	struct proxy_dev *proxy_dev = filp->private_data;

	filp->private_data = NULL;

	vtpm_proxy_delete_device(proxy_dev);

	return 0;
}

static const struct file_operations vtpm_proxy_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = vtpm_proxy_fops_read,
	.write = vtpm_proxy_fops_write,
	.poll = vtpm_proxy_fops_poll,
	.release = vtpm_proxy_fops_release,
};

/*
 * Functions invoked by the core TPM driver to send TPM commands to
 * 'server side' and receive responses from there.
 */

/*
 * Called when core TPM driver reads TPM responses from 'server side'
 *
 * Return value:
 *      Number of TPM response bytes read, negative error value otherwise
 */
static int vtpm_proxy_tpm_op_recv(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct proxy_dev *proxy_dev = dev_get_drvdata(&chip->dev);
	size_t len;

	/* process gone ? */
	mutex_lock(&proxy_dev->buf_lock);

	if (!(proxy_dev->state & STATE_OPENED_FLAG)) {
		mutex_unlock(&proxy_dev->buf_lock);
		return -EPIPE;
	}

	len = proxy_dev->resp_len;
	if (count < len) {
		dev_err(&chip->dev,
			"Invalid size in recv: count=%zd, resp_len=%zd\n",
			count, len);
		len = -EIO;
		goto out;
	}

	memcpy(buf, proxy_dev->buffer, len);
	proxy_dev->resp_len = 0;

out:
	mutex_unlock(&proxy_dev->buf_lock);

	return len;
}

/*
 * Called when core TPM driver forwards TPM requests to 'server side'.
 *
 * Return value:
 *      0 in case of success, negative error value otherwise.
 */
static int vtpm_proxy_tpm_op_send(struct tpm_chip *chip, u8 *buf, size_t count)
{
	struct proxy_dev *proxy_dev = dev_get_drvdata(&chip->dev);
	int rc = 0;

	if (count > sizeof(proxy_dev->buffer)) {
		dev_err(&chip->dev,
			"Invalid size in send: count=%zd, buffer size=%zd\n",
			count, sizeof(proxy_dev->buffer));
		return -EIO;
	}

	mutex_lock(&proxy_dev->buf_lock);

	if (!(proxy_dev->state & STATE_OPENED_FLAG)) {
		mutex_unlock(&proxy_dev->buf_lock);
		return -EPIPE;
	}

	proxy_dev->resp_len = 0;

	proxy_dev->req_len = count;
	memcpy(proxy_dev->buffer, buf, count);

	proxy_dev->state &= ~STATE_WAIT_RESPONSE_FLAG;

	mutex_unlock(&proxy_dev->buf_lock);

	wake_up_interruptible(&proxy_dev->wq);

	return rc;
}

static void vtpm_proxy_tpm_op_cancel(struct tpm_chip *chip)
{
	/* not supported */
}

static u8 vtpm_proxy_tpm_op_status(struct tpm_chip *chip)
{
	struct proxy_dev *proxy_dev = dev_get_drvdata(&chip->dev);

	if (proxy_dev->resp_len)
		return VTPM_PROXY_REQ_COMPLETE_FLAG;

	return 0;
}

static bool vtpm_proxy_tpm_req_canceled(struct tpm_chip  *chip, u8 status)
{
	struct proxy_dev *proxy_dev = dev_get_drvdata(&chip->dev);
	bool ret;

	mutex_lock(&proxy_dev->buf_lock);

	ret = !(proxy_dev->state & STATE_OPENED_FLAG);

	mutex_unlock(&proxy_dev->buf_lock);

	return ret;
}

static const struct tpm_class_ops vtpm_proxy_tpm_ops = {
	.flags = TPM_OPS_AUTO_STARTUP,
	.recv = vtpm_proxy_tpm_op_recv,
	.send = vtpm_proxy_tpm_op_send,
	.cancel = vtpm_proxy_tpm_op_cancel,
	.status = vtpm_proxy_tpm_op_status,
	.req_complete_mask = VTPM_PROXY_REQ_COMPLETE_FLAG,
	.req_complete_val = VTPM_PROXY_REQ_COMPLETE_FLAG,
	.req_canceled = vtpm_proxy_tpm_req_canceled,
};

/*
 * Code related to the startup of the TPM 2 and startup of TPM 1.2 +
 * retrieval of timeouts and durations.
 */

static void vtpm_proxy_work(struct work_struct *work)
{
	struct proxy_dev *proxy_dev = container_of(work, struct proxy_dev,
						   work);
	int rc;

	rc = tpm_chip_register(proxy_dev->chip);
	if (rc)
		goto err;

	return;

err:
	vtpm_proxy_fops_undo_open(proxy_dev);
}

/*
 * vtpm_proxy_work_stop: make sure the work has finished
 *
 * This function is useful when user space closed the fd
 * while the driver still determines timeouts.
 */
static void vtpm_proxy_work_stop(struct proxy_dev *proxy_dev)
{
	vtpm_proxy_fops_undo_open(proxy_dev);
	flush_work(&proxy_dev->work);
}

/*
 * vtpm_proxy_work_start: Schedule the work for TPM 1.2 & 2 initialization
 */
static inline void vtpm_proxy_work_start(struct proxy_dev *proxy_dev)
{
	queue_work(workqueue, &proxy_dev->work);
}

/*
 * Code related to creation and deletion of device pairs
 */
static struct proxy_dev *vtpm_proxy_create_proxy_dev(void)
{
	struct proxy_dev *proxy_dev;
	struct tpm_chip *chip;
	int err;

	proxy_dev = kzalloc(sizeof(*proxy_dev), GFP_KERNEL);
	if (proxy_dev == NULL)
		return ERR_PTR(-ENOMEM);

	init_waitqueue_head(&proxy_dev->wq);
	mutex_init(&proxy_dev->buf_lock);
	INIT_WORK(&proxy_dev->work, vtpm_proxy_work);

	chip = tpm_chip_alloc(NULL, &vtpm_proxy_tpm_ops);
	if (IS_ERR(chip)) {
		err = PTR_ERR(chip);
		goto err_proxy_dev_free;
	}
	dev_set_drvdata(&chip->dev, proxy_dev);

	proxy_dev->chip = chip;

	return proxy_dev;

err_proxy_dev_free:
	kfree(proxy_dev);

	return ERR_PTR(err);
}

/*
 * Undo what has been done in vtpm_create_proxy_dev
 */
static inline void vtpm_proxy_delete_proxy_dev(struct proxy_dev *proxy_dev)
{
	put_device(&proxy_dev->chip->dev); /* frees chip */
	kfree(proxy_dev);
}

/*
 * Create a /dev/tpm%d and 'server side' file descriptor pair
 *
 * Return value:
 *      Returns file pointer on success, an error value otherwise
 */
static struct file *vtpm_proxy_create_device(
				 struct vtpm_proxy_new_dev *vtpm_new_dev)
{
	struct proxy_dev *proxy_dev;
	int rc, fd;
	struct file *file;

	if (vtpm_new_dev->flags & ~VTPM_PROXY_FLAGS_ALL)
		return ERR_PTR(-EOPNOTSUPP);

	proxy_dev = vtpm_proxy_create_proxy_dev();
	if (IS_ERR(proxy_dev))
		return ERR_CAST(proxy_dev);

	proxy_dev->flags = vtpm_new_dev->flags;

	/* setup an anonymous file for the server-side */
	fd = get_unused_fd_flags(O_RDWR);
	if (fd < 0) {
		rc = fd;
		goto err_delete_proxy_dev;
	}

	file = anon_inode_getfile("[vtpms]", &vtpm_proxy_fops, proxy_dev,
				  O_RDWR);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		goto err_put_unused_fd;
	}

	/* from now on we can unwind with put_unused_fd() + fput() */
	/* simulate an open() on the server side */
	vtpm_proxy_fops_open(file);

	if (proxy_dev->flags & VTPM_PROXY_FLAG_TPM2)
		proxy_dev->chip->flags |= TPM_CHIP_FLAG_TPM2;

	vtpm_proxy_work_start(proxy_dev);

	vtpm_new_dev->fd = fd;
	vtpm_new_dev->major = MAJOR(proxy_dev->chip->dev.devt);
	vtpm_new_dev->minor = MINOR(proxy_dev->chip->dev.devt);
	vtpm_new_dev->tpm_num = proxy_dev->chip->dev_num;

	return file;

err_put_unused_fd:
	put_unused_fd(fd);

err_delete_proxy_dev:
	vtpm_proxy_delete_proxy_dev(proxy_dev);

	return ERR_PTR(rc);
}

/*
 * Counter part to vtpm_create_device.
 */
static void vtpm_proxy_delete_device(struct proxy_dev *proxy_dev)
{
	vtpm_proxy_work_stop(proxy_dev);

	/*
	 * A client may hold the 'ops' lock, so let it know that the server
	 * side shuts down before we try to grab the 'ops' lock when
	 * unregistering the chip.
	 */
	vtpm_proxy_fops_undo_open(proxy_dev);

	tpm_chip_unregister(proxy_dev->chip);

	vtpm_proxy_delete_proxy_dev(proxy_dev);
}

/*
 * Code related to the control device /dev/vtpmx
 */

/*
 * vtpmx_fops_ioctl: ioctl on /dev/vtpmx
 *
 * Return value:
 *      Returns 0 on success, a negative error code otherwise.
 */
static long vtpmx_fops_ioctl(struct file *f, unsigned int ioctl,
				   unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct vtpm_proxy_new_dev __user *vtpm_new_dev_p;
	struct vtpm_proxy_new_dev vtpm_new_dev;
	struct file *file;

	switch (ioctl) {
	case VTPM_PROXY_IOC_NEW_DEV:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		vtpm_new_dev_p = argp;
		if (copy_from_user(&vtpm_new_dev, vtpm_new_dev_p,
				   sizeof(vtpm_new_dev)))
			return -EFAULT;
		file = vtpm_proxy_create_device(&vtpm_new_dev);
		if (IS_ERR(file))
			return PTR_ERR(file);
		if (copy_to_user(vtpm_new_dev_p, &vtpm_new_dev,
				 sizeof(vtpm_new_dev))) {
			put_unused_fd(vtpm_new_dev.fd);
			fput(file);
			return -EFAULT;
		}

		fd_install(vtpm_new_dev.fd, file);
		return 0;

	default:
		return -ENOIOCTLCMD;
	}
}

#ifdef CONFIG_COMPAT
static long vtpmx_fops_compat_ioctl(struct file *f, unsigned int ioctl,
					  unsigned long arg)
{
	return vtpmx_fops_ioctl(f, ioctl, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations vtpmx_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = vtpmx_fops_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vtpmx_fops_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct miscdevice vtpmx_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "vtpmx",
	.fops = &vtpmx_fops,
};

static int vtpmx_init(void)
{
	return misc_register(&vtpmx_miscdev);
}

static void vtpmx_cleanup(void)
{
	misc_deregister(&vtpmx_miscdev);
}

static int __init vtpm_module_init(void)
{
	int rc;

	rc = vtpmx_init();
	if (rc) {
		pr_err("couldn't create vtpmx device\n");
		return rc;
	}

	workqueue = create_workqueue("tpm-vtpm");
	if (!workqueue) {
		pr_err("couldn't create workqueue\n");
		rc = -ENOMEM;
		goto err_vtpmx_cleanup;
	}

	return 0;

err_vtpmx_cleanup:
	vtpmx_cleanup();

	return rc;
}

static void __exit vtpm_module_exit(void)
{
	destroy_workqueue(workqueue);
	vtpmx_cleanup();
}

module_init(vtpm_module_init);
module_exit(vtpm_module_exit);

MODULE_AUTHOR("Stefan Berger (stefanb@us.ibm.com)");
MODULE_DESCRIPTION("vTPM Driver");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
