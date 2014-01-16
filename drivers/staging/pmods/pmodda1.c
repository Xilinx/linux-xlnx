/*
 * pmodda1.c - Digilent PmodDA1 driver
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
#include <linux/uaccess.h>

#define DRIVER_NAME "pmodda1"
#define SPI_DRIVER_NAME "pmodda1-spi"

#define DEFAULT_SPI_SPEED 625000
/* only 2 channels as SPI does not allow write on Data In line. */
#define PMODDA1_DEV_NUM 2

#define DEFAULT_BUF_SZ 512
/*
 * default size of the buffer for each DAC on the device. Can be
 * changed from the default during insmod.
 */
static int buf_sz = DEFAULT_BUF_SZ;
module_param(buf_sz, int, 0);

static dev_t pmodda1_first_dev_id;
static struct spi_device *spi_device;
static unsigned int spi_drv_registered;
static struct class *pmodda1_class;

struct pmodda1_device {
	char *name;

	unsigned int minor_id;
	/* Data Buffer */
	unsigned char *buf;
	/* Pin Assignment */

	unsigned long iSCLK;
	unsigned long iSDIN;
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
 * create a shadow register to configure the devices using a
 * struct for the physical D to A parts used on the Pmod.
 * this is intended to hold the state for each D to A such
 * that a control word can be built for the device when a
 * write request comes for any of the channels.
 */
struct ad7303 {
	bool ext;
	bool ldac;
	bool pdb;
	bool pda;
	bool sel; /* !A/B in the datasheet for the AD7303, bit 10 in the control reg */
	bool cr1;
	bool cr0;
	uint8_t a_val;
	uint8_t b_val;
	struct mutex mutex;
};

static struct pmodda1_device *rgpmodda1_devices[PMODDA1_DEV_NUM]; /* create pointer array to hold device info */

static struct ad7303 dac1;
/* Forward definitions */
static uint16_t make_cmd_from_shadow_regs(struct ad7303 *dac);

/* make_cmd_from_shadow_regs
 * @dac the ad7303 structure who's bits are used.
 * this function places the configuration bits in the proper
 * bit position to form the command word the AD73703 expects
 * to receive.
 */
static uint16_t make_cmd_from_shadow_regs(struct ad7303 *dac)
{
	uint16_t cd = 0;

	cd = ((dac->ext & 1) << 15);
	cd |= ((dac->ldac & 1) << 13);
	cd |= ((dac->pdb & 1) << 12);
	cd |= ((dac->pda & 1) << 11);
	cd |= ((dac->sel & 1) << 10);
	cd |= ((dac->cr1 & 1) << 9);
	cd |= ((dac->cr0 & 1) << 8);

	return cd;
}

/**
 * write_spi_16 - Write a 16 bit variable to SPI in High - Low order.
 * @spi: pointer to spi_device structure
 * @cmd_data: the 16 bits variable containing data to be written to spi
 *
 * This function writes to spi the 16 bits of data, big endian (first High and then Low bytes), regardless of CPU representation.
 * It returns the spi write return value (0 on success).
 */
static int write_spi_16(struct spi_device *spi, uint16_t cmd_data)
{
	int status; /* spi_write return value */
	/* Change endianness of data, if necessary
	 * The data must be written to SPI in big endian,
	 * whereas some CPU architectures are little endian.
	 */
	uint16_t write_cmd_data = cpu_to_be16(cmd_data);

	status = spi_write(spi, &write_cmd_data, 2);
	return status;
}
/**
 * A basic open function.
 */
static int pmodda1_open(struct inode *inode, struct file *fp)
{
	struct pmodda1_device *dev;

	dev = container_of(inode->i_cdev, struct pmodda1_device, cdev);
	dev->minor_id = iminor(inode);
	fp->private_data = dev;

	return 0;
}

/**
 * A basic close function, do nothing.
 */
static int pmodda1_close(struct inode *inode, struct file *fp)
{
	return 0;
}

/*
 * Driver write function
 *
 * This function uses a generic SPI write to send values to the Pmod device.
 * It takes a string from the app in the buffer.
 * It sends the commands and the text to PmodDA1 over the standard SPI interface.
 *
 */
static ssize_t pmodda1_write(struct file *fp, const char __user *buffer, size_t length, loff_t *offset)
{
	ssize_t retval = 0;
	int status; /* spi_write return value */
	int cnt;
	int i;
	unsigned int minor_id;
	struct mutex *mutex;
	uint16_t cmd_data;
	struct pmodda1_device *dev;

	dev = fp->private_data;
	minor_id = dev->minor_id;

	if (minor_id > PMODDA1_DEV_NUM - 1) {
		dev_err(&dev->spi->dev, "da1_write: ERROR: Attempt to read a non-existant device: %d\n", minor_id);
		retval = -ENOTTY;
		goto bad_device;
	}
	dev = rgpmodda1_devices[minor_id];
	mutex = &dac1.mutex; /* get the mutex for the part we will be programming */

	if (mutex_lock_interruptible(mutex)) {
		retval = -ERESTARTSYS;
		goto write_lock_err;
	}

	if (length > buf_sz)
		cnt = buf_sz;
	else
		cnt = length;

	if (copy_from_user(dev->buf, buffer, cnt)) {
		retval = -EFAULT;
		goto quit_write;
	}
	retval = cnt;

	dev->buf[cnt] = '\0';
	/* use the minor id number to select which channel to program */

	/* the command word is constructed here */
	if ((minor_id == 0)) {
		dev_dbg(&dev->spi->dev, "da1_write: setting DAC_A (or even number DAC)\n");
		dac1.pda = 0;   /* want DAC A powered up, don't touch DAC B's setting */
		dac1.sel = 0;   /* this will indicate to load DAC A */
	} else {
		dev_dbg(&dev->spi->dev, "da1_write: setting DAC_B (or odd number DAC)\n");
		dac1.pdb = 0;   /* want DAC B powered up, don't touch DAC A's setting */
		dac1.sel = 1;   /* this will indicate to load DAC B */
	}
	dac1.ext = 0;           /* select internal reference for now */
	dac1.ldac = 1;          /* will program DAC input reg from shift reg and update both DAC registers */

	cmd_data = make_cmd_from_shadow_regs(&dac1);
	for (i = 0; i < cnt; i++) {
		cmd_data &= 0xFF00;
		cmd_data |= dev->buf[i];

		status = write_spi_16(dev->spi, cmd_data);

		if (!status)            /* seems like spi_write returns 0 on success */
			retval = i + 1; /* but system write function expects to see number of bytes written */
		else
			retval = -EIO;
	}

	/* save the last value written to the DAC */
	switch (minor_id) {
	case 0:
		dac1.a_val = dev->buf[i - 1];
		break;
	case 1:
		dac1.b_val = dev->buf[i - 1];
		break;
	}
quit_write:
	mutex_unlock(mutex);
	dev_dbg(&dev->spi->dev, "da1_write: Writing to display complete\n");
bad_device:
write_lock_err:

	return retval;
}

/*
 * Driver read function
 *
 * This function does not actually read the Pmod as it is a read-only device. Instead
 * it returns a shadowed copy of the value that was used when the DAC was last programmed.
 */
static ssize_t pmodda1_read(struct file *fp, char __user *buffer, size_t length, loff_t *offset)
{
	ssize_t retval = 0;
	int i;
	struct pmodda1_device *dev;
	unsigned int minor_id;
	uint8_t rd_val;
	struct mutex *mutex;
	int cnt;

	dev = fp->private_data;
	minor_id = dev->minor_id;

	if (length > buf_sz)
		cnt = buf_sz;
	else
		cnt = length;
	if (minor_id > PMODDA1_DEV_NUM - 1) {
		dev_err(&dev->spi->dev, "da1_read: ERROR: Attempt to read a non-existant device: %d\n", minor_id);
		retval = -ENOTTY;
		goto bad_device;
	}

	if (minor_id < 2)
		mutex = &dac1.mutex;

	if (mutex_lock_interruptible(mutex)) {
		retval = -ERESTARTSYS;
		goto lock_err;
	}

	if (buffer == NULL) {
		dev_err(&dev->spi->dev, "da1_read: ERROR: invalid buffer address: 0x%08lx\n", (__force unsigned long)buffer);
		retval = -EINVAL;
		goto quit_read;
	}

	/* ok, can use the minor id number to select which DAC value to return */
	switch (minor_id) {
	case 0:
		rd_val = dac1.a_val;
		break;
	case 1:
		rd_val = dac1.b_val;
		break;
	default:
		rd_val = 0; /* git rid of warning about rd_val may be used uninitialized */
	}
/* tmp, read value from spi */
	spi_read(dev->spi, &rd_val, 2);
	pr_info(DRIVER_NAME "Read values Last Value\t%X\n", rd_val);
	for (i = 0; i < cnt; i++)
		dev->buf[i] = rd_val;

	retval = copy_to_user(buffer, (void *)dev->buf, cnt);
	if (!retval)
		retval = cnt; /* copy success, return amount in buffer */

quit_read:
	mutex_unlock(mutex);
lock_err:
bad_device:
	return retval;
}

static const struct file_operations pmodda1_cdev_fops = {
	.owner		= THIS_MODULE,
	.write		= pmodda1_write,
	.read		= pmodda1_read,
	.open		= pmodda1_open,
	.release	= pmodda1_close,
};

/**
 * add_pmodda1_device_to_bus - Add device to SPI bus, initialize SPI data.
 * @dev: pointer to device tree node
 *
 * This function adds device to SPI bus, initialize SPI data.
 */
static int add_pmodda1_device_to_bus(struct pmodda1_device *dev)
{
	struct spi_master *spi_master;
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
 * pmodda1_setup_cdev - Setup Char Device for ZED PmodDA1 device.
 * @dev: pointer to device tree node
 * @dev_id: pointer to device major and minor number
 * @spi: pointer to spi_device structure
 *
 * This function initializes char device for PmodDA1 device, and add it into
 * kernel device structure. It returns 0, if the cdev is successfully
 * initialized, or a negative value if there is an error.
 */
static int pmodda1_setup_cdev(struct pmodda1_device *dev, dev_t *dev_id, int idx, struct spi_device *spi)
{
	int status = 0;
	struct device *device;
	unsigned int major_id = MAJOR(pmodda1_first_dev_id);
	unsigned int minor_id = MINOR(pmodda1_first_dev_id) + idx;
	char min_name[50];

	cdev_init(&dev->cdev, &pmodda1_cdev_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &pmodda1_cdev_fops;
	dev->spi = spi;

	*dev_id = MKDEV(major_id, minor_id);
	status = cdev_add(&dev->cdev, *dev_id, 1);
	if (status < 0)
		return status;

	/* Add Device node in system */
	sprintf(min_name, "%s_%d", dev->name, idx);
	device = device_create(pmodda1_class, NULL,
			       *dev_id, NULL,
			       min_name);
	if (IS_ERR(device)) {
		status = PTR_ERR(device);
		dev_err(&spi->dev, "failed to create device node %s, err %d\n",
			dev->name, status);
		cdev_del(&dev->cdev);
	}
	pr_info(SPI_DRIVER_NAME "pmodda1_setup_cdev: Create device %s, major %d, minor %d\n",
		min_name, major_id, minor_id);
	return status;
}

/**
 * SPI hardware probe. Sets correct SPI mode, attempts
 * to obtain memory needed by the driver and, for each
 * desired minor number device, it performs a simple
 * initialization of the corresponding device.
 */
static int pmodda1_spi_probe(struct spi_device *spi)
{
	int status = 0;
	struct pmodda1_device *pmodda1_dev;
	int i;

	/* We must use SPI_MODE_0 */
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;

	status = spi_setup(spi);
	if (status < 0) {
		dev_err(&spi->dev, "needs SPI mode %02x, %d KHz; %d\n",
			spi->mode, spi->max_speed_hz / 1000,
			status);
		goto spi_err;
	}

	/* Get pmodda1_device structure */
	pmodda1_dev = (struct pmodda1_device *)spi->dev.platform_data;
	if (pmodda1_dev == NULL) {
		dev_err(&spi->dev, "Cannot get pmodda1_device.\n");
		status = -EINVAL;
		goto spi_platform_data_err;
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(SPI_DRIVER_NAME " [%s] spi_probe: setup char device\n", pmodda1_dev->name);
#endif
	for (i = 0; i < PMODDA1_DEV_NUM; i++) {
		rgpmodda1_devices[i] = kmalloc(sizeof(struct pmodda1_device), GFP_KERNEL);
		if (!rgpmodda1_devices[i]) {
			status = -ENOMEM;
			dev_err(&spi->dev, "da1_spi_probe: Device structure allocation failed: %d for device %d\n", status, i);
			goto dev_alloc_err;
		}
		rgpmodda1_devices[i]->buf = NULL;
	}

	for (i = 0; i < PMODDA1_DEV_NUM; i++) {
		rgpmodda1_devices[i]->buf = kmalloc(buf_sz, GFP_KERNEL);
		if (!rgpmodda1_devices[i]->buf) {
			status = -ENOMEM;
			dev_err(&spi->dev, "Device value buffer allocation failed: %d\n", status);
			goto buf_alloc_err;
		}
		rgpmodda1_devices[i]->spi = spi_device;
	}

	/* Setup char driver for each device*/
	for (i = 0; i < PMODDA1_DEV_NUM; i++) {
		status = pmodda1_setup_cdev(pmodda1_dev, &(pmodda1_dev->dev_id), i, spi);
		if (status) {
			dev_err(&spi->dev, "pmodda1_spi_probe: Error adding da1_spi device: %d for device %d\n", status, i);
			goto cdev_add_err;
		}
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(SPI_DRIVER_NAME " [%s] spi_probe: initialize device\n", pmodda1_dev->name);
#endif

	return status;
buf_alloc_err:
	for (i = 0; i < PMODDA1_DEV_NUM; i++)
		kfree(rgpmodda1_devices[i]->buf);
dev_alloc_err:
	for (i = 0; i < PMODDA1_DEV_NUM; i++)
		kfree(rgpmodda1_devices[i]);
cdev_add_err:
spi_platform_data_err:
spi_err:
	return status;
}

/**
 * pmodda1_spi_remove - SPI hardware remove.
 * Performs tasks required when SPI is removed.
 */
static int pmodda1_spi_remove(struct spi_device *spi)
{
	int status;
	struct pmodda1_device *dev;

	dev = (struct pmodda1_device *)spi->dev.platform_data;

	if (dev == NULL) {
		dev_err(&spi->dev, "spi_remove: Error fetch pmodda1_device struct\n");
		return -EINVAL;
	}

	if (&dev->cdev) {
#ifdef CONFIG_PMODS_DEBUG
		pr_info(SPI_DRIVER_NAME " [%s] spi_remove: Destroy Char Device\n", dev->name);
#endif
		device_destroy(pmodda1_class, dev->dev_id);
		cdev_del(&dev->cdev);
	}

	return status;
}

static struct spi_driver pmodda1_spi_driver = {
	.driver		= {
		.name	= SPI_DRIVER_NAME,
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= pmodda1_spi_probe,
	.remove		= pmodda1_spi_remove,
};

static const struct of_device_id pmodda1_of_match[] = {
	{ .compatible = "dglnt,pmodda1", },
	{},
};
MODULE_DEVICE_TABLE(of, pmodda1_of_match);

/**
 * pmodda1_of_probe - Probe method for PmodDA1 device (over GPIO).
 * @pdev: pointer to platform devices
 *
 * This function probes the PmodDA1 device in the device tree. It initializes the
 * PmodDA1 driver data structure. It returns 0, if the driver is bound to the PmodDA1
 * device, or a negative value if there is an error.
 */
static int pmodda1_of_probe(struct platform_device *pdev)
{
	struct pmodda1_device *pmodda1_dev;
	struct platform_device *pmodda1_pdev;
	struct spi_gpio_platform_data *pmodda1_pdata;

	struct device_node *np = pdev->dev.of_node;

	const u32 *tree_info;
	const u32 *spi_speed;
	int status = 0;
	uint16_t cmd_data;

	/* Alloc Space for platform device structure */
	pmodda1_dev = kzalloc(sizeof(*pmodda1_dev), GFP_KERNEL);
	if (!pmodda1_dev) {
		status = -ENOMEM;
		goto dev_alloc_err;
	}

	pmodda1_dev->buf = kmalloc(buf_sz, GFP_KERNEL);
	if (!pmodda1_dev->buf) {
		status = -ENOMEM;
		pr_info(DRIVER_NAME "Device value buffer allocation failed: %d\n", status);
		goto buf_alloc_err;
	}

	/* Get the GPIO Pins */

	pmodda1_dev->iSCLK = of_get_named_gpio(np, "spi-sclk-gpio", 0);
	pmodda1_dev->iSDIN = of_get_named_gpio(np, "spi-sdin-gpio", 0);
	status = of_get_named_gpio(np, "spi-cs-gpio", 0);
	pmodda1_dev->iCS = (status < 0) ? SPI_GPIO_NO_CHIPSELECT : status;

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: iSCLK: 0x%lx\n", np->name, pmodda1_dev->iSCLK);
	pr_info(DRIVER_NAME " %s: iSDIN: 0x%lx\n", np->name, pmodda1_dev->iSDIN);
	pr_info(DRIVER_NAME " %s: iCS : 0x%lx\n", np->name, pmodda1_dev->iCS);
#endif

	/* Get SPI Related Params */
	tree_info = of_get_property(np, "spi-bus-num", NULL);
	if (tree_info) {
		pmodda1_dev->spi_id = be32_to_cpup((tree_info));
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " %s: BUS_ID\t%x\n", np->name, pmodda1_dev->spi_id);
#endif
	}

	spi_speed = of_get_property(np, "spi-speed-hz", NULL);
	if (spi_speed) {
		pmodda1_dev->spi_speed = be32_to_cpup((spi_speed));
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " %s: SPI_SPEED\t%x\n", np->name, pmodda1_dev->spi_speed);
#endif
	} else {
		pmodda1_dev->spi_speed = DEFAULT_SPI_SPEED;
	}
	/* Alloc Space for platform data structure */
	pmodda1_pdata = kzalloc(sizeof(*pmodda1_pdata), GFP_KERNEL);
	if (!pmodda1_pdata) {
		status = -ENOMEM;
		goto pdata_alloc_err;
	}

	/* Fill up Platform Data Structure */
	pmodda1_pdata->sck = pmodda1_dev->iSCLK;
	pmodda1_pdata->miso = SPI_GPIO_NO_MISO;
	pmodda1_pdata->mosi = pmodda1_dev->iSDIN;
	pmodda1_pdata->num_chipselect = 1;

	/* Alloc Space for platform data structure */
	pmodda1_pdev = kzalloc(sizeof(*pmodda1_pdev), GFP_KERNEL);
	if (!pmodda1_pdev) {
		status = -ENOMEM;
		goto pdev_alloc_err;
	}

	/* Fill up Platform Device Structure */
	pmodda1_pdev->name = "spi_gpio";
	pmodda1_pdev->id = pmodda1_dev->spi_id;
	pmodda1_pdev->dev.platform_data = pmodda1_pdata;
	pmodda1_dev->pdev = pmodda1_pdev;

	/* Register spi_gpio master */
	status = platform_device_register(pmodda1_dev->pdev);
	if (status < 0) {
		dev_err(&pdev->dev, "platform_device_register failed: %d\n", status);
		goto pdev_reg_err;
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: spi_gpio platform device registered.\n", np->name);
#endif
	pmodda1_dev->name = (char *)np->name;

	if (pmodda1_first_dev_id == 0) {
		/* Alloc Major & Minor number for char device */
		status = alloc_chrdev_region(&pmodda1_first_dev_id, 0, PMODDA1_DEV_NUM, DRIVER_NAME);
		if (status) {
			dev_err(&pdev->dev, "Character device region not allocated correctly: %d\n", status);
			goto err_alloc_chrdev_region;
		}
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Char Device Region Registered, with Major: %d.\n",
			MAJOR(pmodda1_first_dev_id));
#endif
	}

	/* Point device node data to pmodda1_device structure */
	if (np->data == NULL)
		np->data = pmodda1_dev;

	if (pmodda1_class == NULL) {
		/* Create Pmodda1 Device Class */
		pmodda1_class = class_create(THIS_MODULE, DRIVER_NAME);
		if (IS_ERR(pmodda1_class)) {
			status = PTR_ERR(pmodda1_class);
			goto err_create_class;
		}
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : pmodda1 device class registered.\n");
#endif
	}

	/* Fill up Board Info for SPI device */
	status = add_pmodda1_device_to_bus(pmodda1_dev);
	if (status < 0) {
		dev_err(&pdev->dev, "add_pmodda1_device_to_bus failed: %d\n", status);
		goto spi_add_err;
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: spi device registered.\n", np->name);
#endif

	if (spi_drv_registered == 0) {
		/* Register SPI Driver for Pmodda1 Device */
		status = spi_register_driver(&pmodda1_spi_driver);
		if (status < 0) {
			dev_err(&pdev->dev, "pmodda1_spi_driver register failed: %d\n", status);
			goto err_spi_register;
		}
		spi_drv_registered = 1;
	}

	/*
	 * although a well-designed part will power-up into a known good state, this is
	 * a good time to force it into a known good state just to be sure. In this case,
	 * the desired known good state is both DACs powered down.
	 */
	dac1.ext = 0;   /* select internal reference for now */
	dac1.ldac = 0;  /* want to be able to load both DACs together */
	dac1.pda = 1;   /* want DAC A powered down */
	dac1.pdb = 1;   /* want DAC B powered down */
	dac1.sel = 0;   /* this won't matter this time since both devices will be loaded from the shift register */
	dac1.cr0 = 0;   /* in conjunction with cr1 will load both devices from the shift register */
	dac1.cr1 = 0;   /* in conjunction with cr0 will load both devices from the shift register */
	mutex_init(&dac1.mutex);
	cmd_data = make_cmd_from_shadow_regs(&dac1);
	/* cmd_data &= 0xFFFF0000; *//* zeroes out the low order bits so that the DAC could be powered up and */
	/* the output would still be zero. */
	status = write_spi_16(rgpmodda1_devices[0]->spi, cmd_data);
	if (status) {
		dev_err(&pdev->dev, "da1_spi_probe: Error writing to device to initally power down: %d\n", status);
		goto initial_state_err;
	}

	return status;
initial_state_err:
err_spi_register:
	class_destroy(pmodda1_class);
	pmodda1_class = NULL;
err_create_class:
	unregister_chrdev_region(pmodda1_first_dev_id, PMODDA1_DEV_NUM);
	pmodda1_first_dev_id = 0;
err_alloc_chrdev_region:
	spi_unregister_device(pmodda1_dev->spi);
spi_add_err:
	platform_device_unregister(pmodda1_dev->pdev);
pdev_reg_err:
	kfree(pmodda1_pdev);
pdev_alloc_err:
	kfree(pmodda1_pdata);
buf_alloc_err:
pdata_alloc_err:
	kfree(pmodda1_dev->buf);
	kfree(pmodda1_dev);
dev_alloc_err:
	return status;
}

/**
 * pmodda1_of_remove - Remove method for ZED PmodDA1 device.
 * @np: pointer to device tree node
 *
 * This function removes the PmodDA1 device in the device tree. It frees the
 * PmodDA1 driver data structure. It returns 0, if the driver is successfully
 * removed, or a negative value if there is an error.
 */
static int pmodda1_of_remove(struct platform_device *pdev)
{
	struct pmodda1_device *pmodda1_dev;
	struct device_node *np = pdev->dev.of_node;

	if (np->data == NULL) {
		dev_err(&pdev->dev, "pmodda1 %s: ERROR: No pmodda1_device structure found!\n", np->name);
		return -ENOSYS;
	}
	pmodda1_dev = (struct pmodda1_device *)(np->data);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s : Free display buffer.\n", np->name);
#endif

	if (pmodda1_dev->buf != NULL)
		kfree(pmodda1_dev->buf);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s : Unregister gpio_spi Platform Devices.\n", np->name);
#endif

	if (pmodda1_dev->pdev != NULL)
		platform_device_unregister(pmodda1_dev->pdev);

	np->data = NULL;

	/* Unregister SPI Driver, Destroy pmodda1 class, Release device id Region
	 */

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " : Unregister SPI Driver.\n");
#endif
	spi_unregister_driver(&pmodda1_spi_driver);
	spi_drv_registered = 0;

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " : Destroy pmodda1_gpio Class.\n");
#endif

	if (pmodda1_class)
		class_destroy(pmodda1_class);
	pmodda1_class = NULL;

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " : Release Char Device Region.\n");
#endif

	unregister_chrdev_region(pmodda1_first_dev_id, PMODDA1_DEV_NUM);
	pmodda1_first_dev_id = 0;

	return 0;
}

static struct platform_driver pmodda1_driver = {
	.driver			= {
		.name		= DRIVER_NAME,
		.owner		= THIS_MODULE,
		.of_match_table = pmodda1_of_match,
	},
	.probe			= pmodda1_of_probe,
	.remove			= pmodda1_of_remove,
};

module_platform_driver(pmodda1_driver);

MODULE_AUTHOR("Digilent, Inc.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_NAME ": PmodDA1 display driver");
MODULE_ALIAS(DRIVER_NAME);
