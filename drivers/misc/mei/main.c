/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/aio.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/uuid.h>
#include <linux/compat.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hw-me.h"
#include "client.h"

/**
 * mei_open - the open function
 *
 * @inode: pointer to inode structure
 * @file: pointer to file structure
 e
 * returns 0 on success, <0 on error
 */
static int mei_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct pci_dev *pdev;
	struct mei_cl *cl;
	struct mei_device *dev;

	int err;

	if (!misc->parent)
		return -ENODEV;

	pdev = container_of(misc->parent, struct pci_dev, dev);

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	cl = NULL;

	err = -ENODEV;
	if (dev->dev_state != MEI_DEV_ENABLED) {
		dev_dbg(&dev->pdev->dev, "dev_state != MEI_ENABLED  dev_state = %s\n",
		    mei_dev_state_str(dev->dev_state));
		goto err_unlock;
	}

	err = -ENOMEM;
	cl = mei_cl_allocate(dev);
	if (!cl)
		goto err_unlock;

	/* open_handle_count check is handled in the mei_cl_link */
	err = mei_cl_link(cl, MEI_HOST_CLIENT_ID_ANY);
	if (err)
		goto err_unlock;

	file->private_data = cl;

	mutex_unlock(&dev->device_lock);

	return nonseekable_open(inode, file);

err_unlock:
	mutex_unlock(&dev->device_lock);
	kfree(cl);
	return err;
}

/**
 * mei_release - the release function
 *
 * @inode: pointer to inode structure
 * @file: pointer to file structure
 *
 * returns 0 on success, <0 on error
 */
static int mei_release(struct inode *inode, struct file *file)
{
	struct mei_cl *cl = file->private_data;
	struct mei_cl_cb *cb;
	struct mei_device *dev;
	int rets = 0;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);
	if (cl == &dev->iamthif_cl) {
		rets = mei_amthif_release(dev, file);
		goto out;
	}
	if (cl->state == MEI_FILE_CONNECTED) {
		cl->state = MEI_FILE_DISCONNECTING;
		dev_dbg(&dev->pdev->dev,
			"disconnecting client host client = %d, "
		    "ME client = %d\n",
		    cl->host_client_id,
		    cl->me_client_id);
		rets = mei_cl_disconnect(cl);
	}
	mei_cl_flush_queues(cl);
	dev_dbg(&dev->pdev->dev, "remove client host client = %d, ME client = %d\n",
	    cl->host_client_id,
	    cl->me_client_id);

	mei_cl_unlink(cl);


	/* free read cb */
	cb = NULL;
	if (cl->read_cb) {
		cb = mei_cl_find_read_cb(cl);
		/* Remove entry from read list */
		if (cb)
			list_del(&cb->list);

		cb = cl->read_cb;
		cl->read_cb = NULL;
	}

	file->private_data = NULL;

	mei_io_cb_free(cb);

	kfree(cl);
out:
	mutex_unlock(&dev->device_lock);
	return rets;
}


/**
 * mei_read - the read function.
 *
 * @file: pointer to file structure
 * @ubuf: pointer to user buffer
 * @length: buffer length
 * @offset: data offset in buffer
 *
 * returns >=0 data length on success , <0 on error
 */
static ssize_t mei_read(struct file *file, char __user *ubuf,
			size_t length, loff_t *offset)
{
	struct mei_cl *cl = file->private_data;
	struct mei_cl_cb *cb_pos = NULL;
	struct mei_cl_cb *cb = NULL;
	struct mei_device *dev;
	int rets;
	int err;


	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;


	mutex_lock(&dev->device_lock);
	if (dev->dev_state != MEI_DEV_ENABLED) {
		rets = -ENODEV;
		goto out;
	}

	if (length == 0) {
		rets = 0;
		goto out;
	}

	if (cl == &dev->iamthif_cl) {
		rets = mei_amthif_read(dev, file, ubuf, length, offset);
		goto out;
	}

	if (cl->read_cb) {
		cb = cl->read_cb;
		/* read what left */
		if (cb->buf_idx > *offset)
			goto copy_buffer;
		/* offset is beyond buf_idx we have no more data return 0 */
		if (cb->buf_idx > 0 && cb->buf_idx <= *offset) {
			rets = 0;
			goto free;
		}
		/* Offset needs to be cleaned for contiguous reads*/
		if (cb->buf_idx == 0 && *offset > 0)
			*offset = 0;
	} else if (*offset > 0) {
		*offset = 0;
	}

	err = mei_cl_read_start(cl, length);
	if (err && err != -EBUSY) {
		dev_dbg(&dev->pdev->dev,
			"mei start read failure with status = %d\n", err);
		rets = err;
		goto out;
	}

	if (MEI_READ_COMPLETE != cl->reading_state &&
			!waitqueue_active(&cl->rx_wait)) {
		if (file->f_flags & O_NONBLOCK) {
			rets = -EAGAIN;
			goto out;
		}

		mutex_unlock(&dev->device_lock);

		if (wait_event_interruptible(cl->rx_wait,
				MEI_READ_COMPLETE == cl->reading_state ||
				mei_cl_is_transitioning(cl))) {

			if (signal_pending(current))
				return -EINTR;
			return -ERESTARTSYS;
		}

		mutex_lock(&dev->device_lock);
		if (mei_cl_is_transitioning(cl)) {
			rets = -EBUSY;
			goto out;
		}
	}

	cb = cl->read_cb;

	if (!cb) {
		rets = -ENODEV;
		goto out;
	}
	if (cl->reading_state != MEI_READ_COMPLETE) {
		rets = 0;
		goto out;
	}
	/* now copy the data to user space */
copy_buffer:
	dev_dbg(&dev->pdev->dev, "buf.size = %d buf.idx= %ld\n",
	    cb->response_buffer.size, cb->buf_idx);
	if (length == 0 || ubuf == NULL || *offset > cb->buf_idx) {
		rets = -EMSGSIZE;
		goto free;
	}

	/* length is being truncated to PAGE_SIZE,
	 * however buf_idx may point beyond that */
	length = min_t(size_t, length, cb->buf_idx - *offset);

	if (copy_to_user(ubuf, cb->response_buffer.data + *offset, length)) {
		rets = -EFAULT;
		goto free;
	}

	rets = length;
	*offset += length;
	if ((unsigned long)*offset < cb->buf_idx)
		goto out;

free:
	cb_pos = mei_cl_find_read_cb(cl);
	/* Remove entry from read list */
	if (cb_pos)
		list_del(&cb_pos->list);
	mei_io_cb_free(cb);
	cl->reading_state = MEI_IDLE;
	cl->read_cb = NULL;
out:
	dev_dbg(&dev->pdev->dev, "end mei read rets= %d\n", rets);
	mutex_unlock(&dev->device_lock);
	return rets;
}
/**
 * mei_write - the write function.
 *
 * @file: pointer to file structure
 * @ubuf: pointer to user buffer
 * @length: buffer length
 * @offset: data offset in buffer
 *
 * returns >=0 data length on success , <0 on error
 */
static ssize_t mei_write(struct file *file, const char __user *ubuf,
			 size_t length, loff_t *offset)
{
	struct mei_cl *cl = file->private_data;
	struct mei_cl_cb *write_cb = NULL;
	struct mei_device *dev;
	unsigned long timeout = 0;
	int rets;
	int id;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	if (dev->dev_state != MEI_DEV_ENABLED) {
		rets = -ENODEV;
		goto out;
	}

	id = mei_me_cl_by_id(dev, cl->me_client_id);
	if (id < 0) {
		rets = -ENODEV;
		goto out;
	}

	if (length == 0) {
		rets = 0;
		goto out;
	}

	if (length > dev->me_clients[id].props.max_msg_length) {
		rets = -EFBIG;
		goto out;
	}

	if (cl->state != MEI_FILE_CONNECTED) {
		dev_err(&dev->pdev->dev, "host client = %d,  is not connected to ME client = %d",
			cl->host_client_id, cl->me_client_id);
		rets = -ENODEV;
		goto out;
	}
	if (cl == &dev->iamthif_cl) {
		write_cb = mei_amthif_find_read_list_entry(dev, file);

		if (write_cb) {
			timeout = write_cb->read_time +
				mei_secs_to_jiffies(MEI_IAMTHIF_READ_TIMER);

			if (time_after(jiffies, timeout) ||
			    cl->reading_state == MEI_READ_COMPLETE) {
				*offset = 0;
				list_del(&write_cb->list);
				mei_io_cb_free(write_cb);
				write_cb = NULL;
			}
		}
	}

	/* free entry used in read */
	if (cl->reading_state == MEI_READ_COMPLETE) {
		*offset = 0;
		write_cb = mei_cl_find_read_cb(cl);
		if (write_cb) {
			list_del(&write_cb->list);
			mei_io_cb_free(write_cb);
			write_cb = NULL;
			cl->reading_state = MEI_IDLE;
			cl->read_cb = NULL;
		}
	} else if (cl->reading_state == MEI_IDLE)
		*offset = 0;


	write_cb = mei_io_cb_init(cl, file);
	if (!write_cb) {
		dev_err(&dev->pdev->dev, "write cb allocation failed\n");
		rets = -ENOMEM;
		goto out;
	}
	rets = mei_io_cb_alloc_req_buf(write_cb, length);
	if (rets)
		goto out;

	rets = copy_from_user(write_cb->request_buffer.data, ubuf, length);
	if (rets) {
		dev_err(&dev->pdev->dev, "failed to copy data from userland\n");
		rets = -EFAULT;
		goto out;
	}

	if (cl == &dev->iamthif_cl) {
		rets = mei_amthif_write(dev, write_cb);

		if (rets) {
			dev_err(&dev->pdev->dev,
				"amthif write failed with status = %d\n", rets);
			goto out;
		}
		mutex_unlock(&dev->device_lock);
		return length;
	}

	rets = mei_cl_write(cl, write_cb, false);
out:
	mutex_unlock(&dev->device_lock);
	if (rets < 0)
		mei_io_cb_free(write_cb);
	return rets;
}

/**
 * mei_ioctl_connect_client - the connect to fw client IOCTL function
 *
 * @dev: the device structure
 * @data: IOCTL connect data, input and output parameters
 * @file: private data of the file object
 *
 * Locking: called under "dev->device_lock" lock
 *
 * returns 0 on success, <0 on failure.
 */
static int mei_ioctl_connect_client(struct file *file,
			struct mei_connect_client_data *data)
{
	struct mei_device *dev;
	struct mei_client *client;
	struct mei_cl *cl;
	int i;
	int rets;

	cl = file->private_data;
	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	if (dev->dev_state != MEI_DEV_ENABLED) {
		rets = -ENODEV;
		goto end;
	}

	if (cl->state != MEI_FILE_INITIALIZING &&
	    cl->state != MEI_FILE_DISCONNECTED) {
		rets = -EBUSY;
		goto end;
	}

	/* find ME client we're trying to connect to */
	i = mei_me_cl_by_uuid(dev, &data->in_client_uuid);
	if (i < 0 || dev->me_clients[i].props.fixed_address) {
		dev_dbg(&dev->pdev->dev, "Cannot connect to FW Client UUID = %pUl\n",
				&data->in_client_uuid);
		rets = -ENODEV;
		goto end;
	}

	cl->me_client_id = dev->me_clients[i].client_id;
	cl->state = MEI_FILE_CONNECTING;

	dev_dbg(&dev->pdev->dev, "Connect to FW Client ID = %d\n",
			cl->me_client_id);
	dev_dbg(&dev->pdev->dev, "FW Client - Protocol Version = %d\n",
			dev->me_clients[i].props.protocol_version);
	dev_dbg(&dev->pdev->dev, "FW Client - Max Msg Len = %d\n",
			dev->me_clients[i].props.max_msg_length);

	/* if we're connecting to amthif client then we will use the
	 * existing connection
	 */
	if (uuid_le_cmp(data->in_client_uuid, mei_amthif_guid) == 0) {
		dev_dbg(&dev->pdev->dev, "FW Client is amthi\n");
		if (dev->iamthif_cl.state != MEI_FILE_CONNECTED) {
			rets = -ENODEV;
			goto end;
		}
		mei_cl_unlink(cl);

		kfree(cl);
		cl = NULL;
		dev->iamthif_open_count++;
		file->private_data = &dev->iamthif_cl;

		client = &data->out_client_properties;
		client->max_msg_length =
			dev->me_clients[i].props.max_msg_length;
		client->protocol_version =
			dev->me_clients[i].props.protocol_version;
		rets = dev->iamthif_cl.status;

		goto end;
	}


	/* prepare the output buffer */
	client = &data->out_client_properties;
	client->max_msg_length = dev->me_clients[i].props.max_msg_length;
	client->protocol_version = dev->me_clients[i].props.protocol_version;
	dev_dbg(&dev->pdev->dev, "Can connect?\n");


	rets = mei_cl_connect(cl, file);

end:
	return rets;
}


/**
 * mei_ioctl - the IOCTL function
 *
 * @file: pointer to file structure
 * @cmd: ioctl command
 * @data: pointer to mei message structure
 *
 * returns 0 on success , <0 on error
 */
static long mei_ioctl(struct file *file, unsigned int cmd, unsigned long data)
{
	struct mei_device *dev;
	struct mei_cl *cl = file->private_data;
	struct mei_connect_client_data *connect_data = NULL;
	int rets;

	if (cmd != IOCTL_MEI_CONNECT_CLIENT)
		return -EINVAL;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	dev_dbg(&dev->pdev->dev, "IOCTL cmd = 0x%x", cmd);

	mutex_lock(&dev->device_lock);
	if (dev->dev_state != MEI_DEV_ENABLED) {
		rets = -ENODEV;
		goto out;
	}

	dev_dbg(&dev->pdev->dev, ": IOCTL_MEI_CONNECT_CLIENT.\n");

	connect_data = kzalloc(sizeof(struct mei_connect_client_data),
							GFP_KERNEL);
	if (!connect_data) {
		rets = -ENOMEM;
		goto out;
	}
	dev_dbg(&dev->pdev->dev, "copy connect data from user\n");
	if (copy_from_user(connect_data, (char __user *)data,
				sizeof(struct mei_connect_client_data))) {
		dev_err(&dev->pdev->dev, "failed to copy data from userland\n");
		rets = -EFAULT;
		goto out;
	}

	rets = mei_ioctl_connect_client(file, connect_data);

	/* if all is ok, copying the data back to user. */
	if (rets)
		goto out;

	dev_dbg(&dev->pdev->dev, "copy connect data to user\n");
	if (copy_to_user((char __user *)data, connect_data,
				sizeof(struct mei_connect_client_data))) {
		dev_dbg(&dev->pdev->dev, "failed to copy data to userland\n");
		rets = -EFAULT;
		goto out;
	}

out:
	kfree(connect_data);
	mutex_unlock(&dev->device_lock);
	return rets;
}

/**
 * mei_compat_ioctl - the compat IOCTL function
 *
 * @file: pointer to file structure
 * @cmd: ioctl command
 * @data: pointer to mei message structure
 *
 * returns 0 on success , <0 on error
 */
#ifdef CONFIG_COMPAT
static long mei_compat_ioctl(struct file *file,
			unsigned int cmd, unsigned long data)
{
	return mei_ioctl(file, cmd, (unsigned long)compat_ptr(data));
}
#endif


/**
 * mei_poll - the poll function
 *
 * @file: pointer to file structure
 * @wait: pointer to poll_table structure
 *
 * returns poll mask
 */
static unsigned int mei_poll(struct file *file, poll_table *wait)
{
	struct mei_cl *cl = file->private_data;
	struct mei_device *dev;
	unsigned int mask = 0;

	if (WARN_ON(!cl || !cl->dev))
		return POLLERR;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	if (!mei_cl_is_connected(cl)) {
		mask = POLLERR;
		goto out;
	}

	mutex_unlock(&dev->device_lock);


	if (cl == &dev->iamthif_cl)
		return mei_amthif_poll(dev, file, wait);

	poll_wait(file, &cl->tx_wait, wait);

	mutex_lock(&dev->device_lock);

	if (!mei_cl_is_connected(cl)) {
		mask = POLLERR;
		goto out;
	}

	if (MEI_WRITE_COMPLETE == cl->writing_state)
		mask |= (POLLIN | POLLRDNORM);

out:
	mutex_unlock(&dev->device_lock);
	return mask;
}

/*
 * file operations structure will be used for mei char device.
 */
static const struct file_operations mei_fops = {
	.owner = THIS_MODULE,
	.read = mei_read,
	.unlocked_ioctl = mei_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = mei_compat_ioctl,
#endif
	.open = mei_open,
	.release = mei_release,
	.write = mei_write,
	.poll = mei_poll,
	.llseek = no_llseek
};

/*
 * Misc Device Struct
 */
static struct miscdevice  mei_misc_device = {
		.name = "mei",
		.fops = &mei_fops,
		.minor = MISC_DYNAMIC_MINOR,
};


int mei_register(struct mei_device *dev)
{
	int ret;
	mei_misc_device.parent = &dev->pdev->dev;
	ret = misc_register(&mei_misc_device);
	if (ret)
		return ret;

	if (mei_dbgfs_register(dev, mei_misc_device.name))
		dev_err(&dev->pdev->dev, "cannot register debugfs\n");

	return 0;
}
EXPORT_SYMBOL_GPL(mei_register);

void mei_deregister(struct mei_device *dev)
{
	mei_dbgfs_deregister(dev);
	misc_deregister(&mei_misc_device);
	mei_misc_device.parent = NULL;
}
EXPORT_SYMBOL_GPL(mei_deregister);

static int __init mei_init(void)
{
	return mei_cl_bus_init();
}

static void __exit mei_exit(void)
{
	mei_cl_bus_exit();
}

module_init(mei_init);
module_exit(mei_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) Management Engine Interface");
MODULE_LICENSE("GPL v2");

