/*
 * pmodad1.c - Digilent PmodAD1 driver
 *
 * Copyright (c) 2012 Digilent. All right reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define DRIVER_NAME "pmodad1"
#define SPI_DRIVER_NAME "pmodad1-spi"

#define DEFAULT_SPI_SPEED 625000
#define MAX_PMODAD1_DEV_NUM 16
#define TXT_BUF_SIZE 1024
#define MAX_NO_ROWS 2 /* The device has 2 rows */

static dev_t pmodad1_dev_id;
static unsigned int device_num;
static unsigned int cur_minor;
static unsigned int spi_drv_registered;
static struct class *pmodad1_class;

/* Kernel space buffer size (in bytes) for the ad1_read function.
 * Can be entered from the command line during insmod
 */
static int read_buf_size = 512;
module_param(read_buf_size, int, 0);

struct pmodad1_device {
	char *name;
	/* R/W Mutex Lock */
	struct mutex mutex;

	unsigned short *val_buf;

	/* Pin Assignment */

	unsigned long iSCLK;
	unsigned long iSDOUT;
	unsigned long iCS;

	/* SPI Info */
	uint32_t spi_speed;
	uint32_t spi_id;
	/* platform device structures */
	struct platform_device *pdev;
	/* Char Device */
	struct cdev cdev;
	struct spi_device *spi;
	dev_t dev_id;
};

/*
 * Driver read function
 *
 * This function uses a generic SPI read to read values from the Pmod.
 * It will only read full values, so if the length from user space is
 * not a multiple of 2, it will read up to length - 1 bytes.
 *
 * Function can possibly error out if:
 *		The mutex cannot be locked
 *		spi_read fails on the first read
 *
 * Otherwise, the function returns the number of successful values read,
 * each with a size of 2 bytes. So for instance, if 13 bytes are read,
 * the function will return 12, indicating 6 values were read successfully
 * from the pmod. Additionally, if copy_to_user cannot successfully
 * copy everything, the number of successfully copied full values (2 bytes)
 * will be returned.
 *
 * We use goto in this function because there are multiple exit points,
 * and it prevents us from having to call mutex_unlock() for the mutex
 * each time.
 */
static ssize_t pmodad1_read(struct file *fp, char __user *buffer, size_t length, loff_t *offset)
{
	int status;             /* spi_read return value */
	int num_reads;          /* Number of values to read from Pmod */
	int i;
	ssize_t retval;         /* Function return value */
	ssize_t ret;            /* copy_to_user return value */
	unsigned short buf;     /* Temporary storage for each read value */
	struct pmodad1_device *dev;

	dev = fp->private_data;
	status = 0;
	num_reads = length / 2;

	if (mutex_lock_interruptible(&dev->mutex)) {
		retval = -ERESTARTSYS;
		goto lock_err;
	}

	if (buffer == NULL) {
		retval = -EINVAL;
		goto read_out;
	}

	for (i = 0; i < num_reads; i++) {
		/* Use generic SPI read */
		status = spi_read(dev->spi, &buf, 2);
		if (status)
			break;
		/* Change endianness of result, if necessary
		 * The result from the Pmod hardware is big endian,
		 * whereas Microblaze and other CPU architectures are
		 * little endian.
		 */
		dev->val_buf[i] = be16_to_cpu(buf) & 0x0FFF; /* only 12 bits matters */
	}

	if (i == 0) {
		dev_err(&dev->spi->dev, "SPI read failure: %d\n", status);
		retval = status;
		goto read_out;
	}

	/*
	 * Only copy full values (2 bytes) in the case of a user space length
	 *	that is not a multiple of 2.
	 */
	ret = copy_to_user(buffer, (void *)dev->val_buf, i * 2);

	retval = num_reads * 2 - (ret + (ret % 2));
read_out:
	mutex_unlock(&dev->mutex);
lock_err:
	return retval;
}

/**
 * A basic open function.
 */
static int pmodad1_open(struct inode *inode, struct file *fp)
{
	struct pmodad1_device *dev;

	dev = container_of(inode->i_cdev, struct pmodad1_device, cdev);
	fp->private_data = dev;

	return 0;
}

static const struct file_operations pmodad1_cdev_fops = {
	.owner	= THIS_MODULE,
	.open	= pmodad1_open,
	.read	= pmodad1_read,
};

/**
 * add_pmodad1_device_to_bus - Add device to SPI bus, initialize SPI data.
 * @dev: pointer to device tree node
 *
 * This function adds device to SPI bus, initialize SPI data.
 */
static int add_pmodad1_device_to_bus(struct pmodad1_device *dev)
{
	struct spi_master *spi_master;
	struct spi_device *spi_device;
	int status = 0;

	spi_master = spi_busnum_to_master(dev->spi_id);
	if (!spi_master) {
		dev_err(&dev->pdev->dev, "spi_busnum_to_master(%d) returned NULL\n", dev->spi_id);
		return -ENOSYS;
	}

	spi_device = spi_alloc_device(spi_master);
	if (!spi_device) {
		put_device(&spi_master->dev);
		dev_err(&dev->pdev->dev, "spi_alloc_device() failed\n");
		return -ENOMEM;
	}

	spi_device->chip_select = 0;
	spi_device->max_speed_hz = dev->spi_speed;
/*	spi_device->max_speed_hz = 625000; */
	spi_device->mode = SPI_MODE_0;
	spi_device->bits_per_word = 8;
	spi_device->controller_data = (void *)dev->iCS;
	spi_device->dev.platform_data = dev;
	strlcpy(spi_device->modalias, SPI_DRIVER_NAME, sizeof(SPI_DRIVER_NAME));

	status = spi_add_device(spi_device);
	if (status < 0) {
		spi_dev_put(spi_device);
		dev_err(&dev->pdev->dev, "spi_add_device() failed %d\n", status);
		return status;
	}
	dev->spi = spi_device;

	put_device(&spi_master->dev);
	pr_info(DRIVER_NAME " SPI initialized, max_speed_hz\t%d\n", spi_device->max_speed_hz);
	return status;
}

/**
 * pmodad1_setup_cdev - Setup Char Device for ZED PmodAD1 device.
 * @dev: pointer to device tree node
 * @dev_id: pointer to device major and minor number
 * @spi: pointer to spi_device structure
 *
 * This function initializes char device for PmodAD1 device, and add it into
 * kernel device structure. It returns 0, if the cdev is successfully
 * initialized, or a negative value if there is an error.
 */
static int pmodad1_setup_cdev(struct pmodad1_device *dev, dev_t *dev_id, struct spi_device *spi)
{
	int status = 0;
	struct device *device;

	cdev_init(&dev->cdev, &pmodad1_cdev_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &pmodad1_cdev_fops;
	dev->spi = spi;

	*dev_id = MKDEV(MAJOR(pmodad1_dev_id), cur_minor++);
	status = cdev_add(&dev->cdev, *dev_id, 1);
	if (status < 0)
		return status;

	/* Add Device node in system */
	device = device_create(pmodad1_class, NULL,
			       *dev_id, NULL,
			       "%s", dev->name);
	if (IS_ERR(device)) {
		status = PTR_ERR(device);
		dev_err(&spi->dev, "failed to create device node %s, err %d\n",
			dev->name, status);
		cdev_del(&dev->cdev);
	}

	return status;
}

/**
 * SPI hardware probe. Sets correct SPI mode, attempts
 * to obtain memory needed by the driver, and performs
 * a simple initialization of the device.
 * @spi	: pointer to spi device being initialized.
 */
static int pmodad1_spi_probe(struct spi_device *spi)
{
	int status = 0;
	struct pmodad1_device *pmodad1_dev;

	if (spi->master->flags & SPI_MASTER_HALF_DUPLEX) {
		status = -EINVAL;
		pr_info(SPI_DRIVER_NAME "SPI settings incorrect: %d\n", status);
		goto spi_err;
	}

	/* use SPI_MODE_0 */
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;

	status = spi_setup(spi);
	if (status < 0) {
		dev_err(&spi->dev, "needs SPI mode %02x, %d KHz; %d\n",
			spi->mode, spi->max_speed_hz / 1000,
			status);
		goto spi_err;
	}

	/* Get pmodad1_device structure */
	pmodad1_dev = (struct pmodad1_device *)spi->dev.platform_data;
	if (pmodad1_dev == NULL) {
		dev_err(&spi->dev, "Cannot get pmodad1_device.\n");
		status = -EINVAL;
		goto spi_platform_data_err;
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(SPI_DRIVER_NAME " [%s] spi_probe: setup char device\n", pmodad1_dev->name);
#endif

	/* Setup char driver */
	status = pmodad1_setup_cdev(pmodad1_dev, &(pmodad1_dev->dev_id), spi);
	if (status) {
		pr_info(" spi_probe: Error adding %s device: %d\n", SPI_DRIVER_NAME, status);
		dev_err(&spi->dev, "spi_probe: Error adding %s device: %d\n", SPI_DRIVER_NAME, status);
		goto cdev_add_err;
	}

	/* Initialize Mutex */
	mutex_init(&pmodad1_dev->mutex);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(SPI_DRIVER_NAME " [%s] spi_probe: initialize device\n", pmodad1_dev->name);
#endif

	return status;

cdev_add_err:
spi_platform_data_err:
spi_err:
	return status;
}

/**
 * pmodad1_spi_remove - SPI hardware remove.
 * Performs tasks required when SPI is removed.
 * @spi	: pointer to spi device being removed
 */
static int pmodad1_spi_remove(struct spi_device *spi)
{
	int status;
	struct pmodad1_device *dev;

	dev = (struct pmodad1_device *)spi->dev.platform_data;

	if (dev == NULL) {
		dev_err(&spi->dev, "spi_remove: Error fetch pmodad1_device struct\n");
		return -EINVAL;
	}

	if (&dev->cdev) {
#ifdef CONFIG_PMODS_DEBUG
		pr_info(SPI_DRIVER_NAME " [%s] spi_remove: Destroy Char Device\n", dev->name);
#endif
		device_destroy(pmodad1_class, dev->dev_id);
		cdev_del(&dev->cdev);
	}

	cur_minor--;

	return status;
}

static struct spi_driver pmodad1_spi_driver = {
	.driver		= {
		.name	= SPI_DRIVER_NAME,
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= pmodad1_spi_probe,
	.remove		= pmodad1_spi_remove,
};

static const struct of_device_id pmodad1_of_match[] = {
	{ .compatible = "dglnt,pmodad1", },
	{},
};
MODULE_DEVICE_TABLE(of, pmodad1_of_match);

/**
 * pmodad1_of_probe - Probe method for PmodAD1 device (over GPIO).
 * @pdev: pointer to platform devices
 *
 * This function probes the PmodAD1 device in the device tree. It initializes the
 * PmodAD1 driver data structure. It returns 0, if the driver is bound to the PmodAD1
 * device, or a negative value if there is an error.
 */
static int pmodad1_of_probe(struct platform_device *pdev)
{
	struct pmodad1_device *pmodad1_dev;
	struct platform_device *pmodad1_pdev;
	struct spi_gpio_platform_data *pmodad1_pdata;

	struct device_node *np = pdev->dev.of_node;

	const u32 *tree_info;
	const u32 *spi_speed;
	int status = 0;

	/* Alloc Space for platform device structure */
	pmodad1_dev = kzalloc(sizeof(*pmodad1_dev), GFP_KERNEL);
	if (!pmodad1_dev) {
		status = -ENOMEM;
		dev_err(&pdev->dev, "Platform device structure allocation failed: %d\n", status);
		goto dev_alloc_err;
	}

	pmodad1_dev->val_buf = kmalloc(read_buf_size, GFP_KERNEL);
	if (!pmodad1_dev->val_buf) {
		status = -ENOMEM;
		dev_err(&pdev->dev, "Device value buffer allocation failed: %d\n", status);
		goto buf_alloc_err;
	}

	/* Get the GPIO Pins */

	pmodad1_dev->iSCLK = of_get_named_gpio(np, "spi-sclk-gpio", 0);
	pmodad1_dev->iSDOUT = of_get_named_gpio(np, "spi-sdout-gpio", 0);
	status = of_get_named_gpio(np, "spi-cs-gpio", 0);
	pmodad1_dev->iCS = (status < 0) ? SPI_GPIO_NO_CHIPSELECT : status;

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: iSCLK: 0x%lx\n", np->name, pmodad1_dev->iSCLK);
	pr_info(DRIVER_NAME " %s: iSDOUT: 0x%lx\n", np->name, pmodad1_dev->iSDOUT);
	pr_info(DRIVER_NAME " %s: iCS : 0x%lx\n", np->name, pmodad1_dev->iCS);
#endif

	/* Get SPI Related Params */
	tree_info = of_get_property(np, "spi-bus-num", NULL);
	if (tree_info) {
		pmodad1_dev->spi_id = be32_to_cpup((tree_info));
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " %s: BUS_ID\t%x\n", np->name, pmodad1_dev->spi_id);
#endif
	}

	spi_speed = of_get_property(np, "spi-speed-hz", NULL);
	if (spi_speed) {
		pmodad1_dev->spi_speed = be32_to_cpup((spi_speed));
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " %s: SPI_SPEED\t%x\n", np->name, pmodad1_dev->spi_speed);
#endif
	} else {
		pmodad1_dev->spi_speed = DEFAULT_SPI_SPEED;
	}

	/* Alloc Space for platform data structure */
	pmodad1_pdata = kzalloc(sizeof(*pmodad1_pdata), GFP_KERNEL);
	if (!pmodad1_pdata) {
		status = -ENOMEM;
		goto pdata_alloc_err;
	}

	/* Fill up Platform Data Structure */
	pmodad1_pdata->sck = pmodad1_dev->iSCLK;
	pmodad1_pdata->miso = pmodad1_dev->iSDOUT;
	pmodad1_pdata->mosi = SPI_GPIO_NO_MOSI;
	pmodad1_pdata->num_chipselect = 1;

	/* Alloc Space for platform data structure */
	pmodad1_pdev = kzalloc(sizeof(*pmodad1_pdev), GFP_KERNEL);
	if (!pmodad1_pdev) {
		status = -ENOMEM;
		goto pdev_alloc_err;
	}

	/* Fill up Platform Device Structure */
	pmodad1_pdev->name = "spi_gpio";
	pmodad1_pdev->id = pmodad1_dev->spi_id;
	pmodad1_pdev->dev.platform_data = pmodad1_pdata;
	pmodad1_dev->pdev = pmodad1_pdev;

	/* Register spi_gpio master */
	status = platform_device_register(pmodad1_dev->pdev);
	if (status < 0) {
		dev_err(&pdev->dev, "platform_device_register failed: %d\n", status);
		goto pdev_reg_err;
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: spi_gpio platform device registered.\n", np->name);
#endif
	pmodad1_dev->name = (char *)np->name;

	/* Fill up Board Info for SPI device */
	status = add_pmodad1_device_to_bus(pmodad1_dev);
	if (status < 0) {
		dev_err(&pdev->dev, "add_pmodad1_device_to_bus failed: %d\n", status);
		goto spi_add_err;
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: spi device registered.\n", np->name);
#endif

	/* Point device node data to pmodad1_device structure */
	if (np->data == NULL)
		np->data = pmodad1_dev;

	if (pmodad1_dev_id == 0) {
		/* Alloc Major & Minor number for char device */
		status = alloc_chrdev_region(&pmodad1_dev_id, 0, MAX_PMODAD1_DEV_NUM, DRIVER_NAME);
		if (status) {
			dev_err(&pdev->dev, "Character device region not allocated correctly: %d\n", status);
			goto err_alloc_chrdev_region;
		}
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Char Device Region Registered, with Major: %d.\n",
			MAJOR(pmodad1_dev_id));
#endif
	}

	if (pmodad1_class == NULL) {
		/* Create Pmodad1 Device Class */
		pmodad1_class = class_create(THIS_MODULE, DRIVER_NAME);
		if (IS_ERR(pmodad1_class)) {
			status = PTR_ERR(pmodad1_class);
			goto err_create_class;
		}
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : pmodad1 device class registered.\n");
#endif
	}

	if (spi_drv_registered == 0) {
		/* Register SPI Driver for Pmodad1 Device */
		status = spi_register_driver(&pmodad1_spi_driver);
		if (status < 0) {
			dev_err(&pdev->dev, "pmodad1_spi_driver register failed: %d\n", status);
			goto err_spi_register;
		}
		spi_drv_registered = 1;
	}

	device_num++;

	return status;

err_spi_register:
	class_destroy(pmodad1_class);
	pmodad1_class = NULL;
err_create_class:
	unregister_chrdev_region(pmodad1_dev_id, MAX_PMODAD1_DEV_NUM);
	pmodad1_dev_id = 0;
err_alloc_chrdev_region:
	spi_unregister_device(pmodad1_dev->spi);
spi_add_err:
	platform_device_unregister(pmodad1_dev->pdev);
pdev_reg_err:
	kfree(pmodad1_pdev);
pdev_alloc_err:
	kfree(pmodad1_pdata);
pdata_alloc_err:
	kfree(pmodad1_dev->val_buf);
buf_alloc_err:
	kfree(pmodad1_dev);
dev_alloc_err:
	return status;
}

/**
 * pmodad1_of_remove - Remove method for ZED PmodAD1 device.
 * @np: pointer to device tree node
 *
 * This function removes the PmodAD1 device in the device tree. It frees the
 * PmodAD1 driver data structure. It returns 0, if the driver is successfully
 * removed, or a negative value if there is an error.
 */
static int pmodad1_of_remove(struct platform_device *pdev)
{
	struct pmodad1_device *pmodad1_dev;
	struct device_node *np = pdev->dev.of_node;

	if (np->data == NULL) {
		dev_err(&pdev->dev, "pmodad1 %s: ERROR: No pmodad1_device structure found!\n", np->name);
		return -ENOSYS;
	}
	pmodad1_dev = (struct pmodad1_device *)(np->data);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s : Free display buffer.\n", np->name);
#endif

	if (pmodad1_dev->val_buf != NULL)
		kfree(pmodad1_dev->val_buf);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s : Unregister gpio_spi Platform Devices.\n", np->name);
#endif

	if (pmodad1_dev->pdev != NULL)
		platform_device_unregister(pmodad1_dev->pdev);

	np->data = NULL;
	device_num--;

	/* Unregister SPI Driver, Destroy pmodad1 class, Release device id Region after
	 * all pmodad1 devices have been removed.
	 */
	if (device_num == 0) {
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Unregister SPI Driver.\n");
#endif
		spi_unregister_driver(&pmodad1_spi_driver);
		spi_drv_registered = 0;

#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Destroy pmodad1 Class.\n");
#endif

		if (pmodad1_class)
			class_destroy(pmodad1_class);
		pmodad1_class = NULL;

#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Release Char Device Region.\n");
#endif

		unregister_chrdev_region(pmodad1_dev_id, MAX_PMODAD1_DEV_NUM);
		pmodad1_dev_id = 0;
	}

	return 0;
}

static struct platform_driver pmodad1_driver = {
	.driver			= {
		.name		= DRIVER_NAME,
		.owner		= THIS_MODULE,
		.of_match_table = pmodad1_of_match,
	},
	.probe			= pmodad1_of_probe,
	.remove			= pmodad1_of_remove,
};

module_platform_driver(pmodad1_driver);

MODULE_AUTHOR("Cristian Fatu");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_NAME ": PmodAD1 driver");
MODULE_ALIAS(DRIVER_NAME);
