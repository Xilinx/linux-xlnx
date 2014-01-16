/*
 * pmodcls.c - Digilent PmodCLS driver
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

#define DRIVER_NAME "pmodcls"
#define SPI_DRIVER_NAME "pmodcls-spi"

#define DEFAULT_SPI_SPEED 625000
#define MAX_PMODCLS_DEV_NUM 16
#define TXT_BUF_SIZE 1024
#define MAX_NO_ROWS 2 /* The device has 2 rows */

static dev_t pmodcls_dev_id;
static unsigned int device_num;
static unsigned int cur_minor;
static unsigned int spi_drv_registered;
static struct class *pmodcls_class;

struct pmodcls_device {
	char *name;
	/* R/W Mutex Lock */
	struct mutex mutex;
	/* Text Buffer */
	char *txt_buf;          /* Device Text buffer */
	int cur_row;            /* Maintain current row */
	int exceeded_rows;      /* Flag for situation where maximum number of rows is exceeded */
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

/* Forward definitions */
static int parse_text(char *txt_buf, int cnt, struct pmodcls_device *dev);
static int txt_buf_to_display(char *txt_buf, int cnt, struct pmodcls_device *dev);

/**
 * A basic open function.
 */
static int pmodcls_open(struct inode *inode, struct file *fp)
{
	struct pmodcls_device *dev;

	dev = container_of(inode->i_cdev, struct pmodcls_device, cdev);
	fp->private_data = dev;

	return 0;
}

/**
 * A basic close function, do nothing.
 */
static int pmodcls_close(struct inode *inode, struct file *fp)
{
	return 0;
}

/*
 * Driver write function
 *
 * This function uses a generic SPI write to send values to the Pmod device.
 * It takes a string from the app in the buffer.
 * It sends the commands and the text to PmodCLS over the standard SPI interface.
 *
 */
static ssize_t pmodcls_write(struct file *fp, const char __user *buffer, size_t length, loff_t *offset)
{
	ssize_t retval = 0;
	int status; /* spi_write return value */
	int cnt;
	struct pmodcls_device *dev;

	dev = fp->private_data;

	if (mutex_lock_interruptible(&dev->mutex)) {
		retval = -ERESTARTSYS;
		goto write_lock_err;
	}

	cnt = length;

	if (copy_from_user(dev->txt_buf, buffer, cnt)) {
		retval = -EFAULT;
		goto quit_write;
	}
	retval = cnt;

	dev->txt_buf[cnt] = '\0';
	status = parse_text(dev->txt_buf, cnt, dev);
	dev_dbg(&dev->spi->dev, "cls_write: Writing \"%s\" to display\n",
		dev->txt_buf);

	if (status) {
		dev_err(&dev->spi->dev, "cls_write: Error writing text to SPI device\n");
		retval = -EFAULT;
		goto quit_write;
	}
	dev_dbg(&dev->spi->dev, "cls_write: Writing to display complete\n");

quit_write:
	mutex_unlock(&dev->mutex);
write_lock_err:
	return retval;
}

/**
 * parse_text - This function builds the commands to be sent for each recognized escape sequence.
 *
 * Parameters
 * @char *txt_buf: the text array to be parsed
 * @int cnt the number of charcaters to be parsed in the text array
 * @struct pmodclp_device *dev	pointer to device structure
 *
 *
 * This function parses a text array, containing a sequence of one or more text or commands to be sent to PmodCLS. Its purpose is:
 * - recognize, interpret the commands:
 * - maintain a shadow value of the current row (this is because PmodCLS is a "write only" device, the cursor position cannot be read)
 * - split the separate commands / text and send individually to the device.
 * - recognize LF character ('\n') inside a text to be sent to the device
 * - if current line is the first, move the cursor to the beginning of the next line
 * - if current line is the second, there is no room for new line. Text characters after LF are ignored, commands are still interpreted.
 *
 */
static int parse_text(char *txt_buf, int cnt, struct pmodcls_device *dev)
{
	int status = 0; /* spi_write return value */
	int is_ignore_txt;
	int is_par1 = 0;
	int is_cmd = 0;
	int par1 = 0, par2;
	char txt_LF_cmd[10];
	char *parse_buf, *sent_buf;

	parse_buf = txt_buf;
	sent_buf = txt_buf - 1;
	is_par1 = 0;
	is_cmd = 0;
	is_ignore_txt = dev->exceeded_rows;
	while ((!status) && (parse_buf < (txt_buf + cnt))) {
		/* recognize command - look for ESC code, followed by '[' */
		if ((!is_cmd) && ((*parse_buf) == 0x1B) && (parse_buf[1] == '[')) {
			/* enter command mode */
			is_cmd = 1;
			is_par1 = 1;
			par1 = 0;
			/* send previous text (before the ESC sequence) */
			if ((parse_buf - sent_buf) > 1) {
				status = txt_buf_to_display(sent_buf + 1, parse_buf - 1 - sent_buf, dev);
				sent_buf = parse_buf - 1;
			}
			parse_buf++; /* skip '[' char */

		} else {
			if (is_cmd) {
				if ((*parse_buf) >= '0' && (*parse_buf) <= '9') { /* numeric char, build parameters values */
					if (is_par1)
						par1 = 10 * par1 + (*parse_buf) - '0';
					else
						par2 = 10 * par2 + (*parse_buf) - '0';
				} else {
					if ((*parse_buf) == ';') { /* parameters separator */
						is_par1 = 0;
						par2 = 0;
					} else {
						/* look for some commands */
						switch (*parse_buf) {
						case 'H': /* set cursor position */
							dev->cur_row = par1;
							if (dev->cur_row < MAX_NO_ROWS)
								is_ignore_txt = 0;
							else
								is_ignore_txt = 1;
							break;
						case 'j':       /* clear display and home cursor */
						case '*':       /* reset */
							dev->cur_row = 0;
							is_ignore_txt = 0;
							break;
						case 's':       /* save cursor position */
						case 'u':       /* restore saved cursor position */
						case 'K':       /* erase within line */
						case 'N':       /* erase field */
						case '@':       /* scroll left */
						case 'A':       /* scroll right */
						case 'h':       /* set display mode (wrap line) */
						case 'c':       /* set cursor mode */
						case 'p':       /* program char table into LCD */
						case 't':       /* save RAM character table to EEPROM */
						case 'l':       /* load EEPROM character table to RAM */
						case 'd':       /* define user programmable character */
						case 'm':       /* save communication mode to EEPROM */
						case 'w':       /* enable write to EEPROM */
						case 'n':       /* save cursor mode to EEPROM */
						case 'o':       /* save display mode to EEPROM */
								/* cursor is not affected */
							break;

						default:
							/* no command was recognized */
							is_cmd = 0; /* comand mode is abandonned */
						}
						if (is_cmd) {
							/* send text including the command char */
							if ((parse_buf - sent_buf) > 1) {
								status = txt_buf_to_display(sent_buf + 1, parse_buf - sent_buf, dev);
								sent_buf = parse_buf;
								is_cmd = 0; /* comand mode is abandonned */
							}
						}
					}
				}
			} else {
				/* free text, not inside a command */
				if (is_ignore_txt) {
					sent_buf = parse_buf;
				} else {
					if ((*parse_buf) == '\n') { /* LF */
						/* send text before the LF char */
						if ((parse_buf - sent_buf) > 0)
							txt_buf_to_display(sent_buf + 1, parse_buf - 1 - sent_buf, dev);
						/* position the cursor on the beginning of the next line */
						if (dev->cur_row < (MAX_NO_ROWS - 1)) {
							dev->cur_row++;
							strcpy(txt_LF_cmd, "0[0;0H");
							txt_LF_cmd[0] = 0x1B; /* ESC */
							txt_LF_cmd[2] = '0' + (unsigned char)dev->cur_row;
							status = txt_buf_to_display(txt_LF_cmd, 6, dev);
						} else {
							/* there is no room to place a third line. Ignore text (still look for the comands) */
							is_ignore_txt = 1;
						}

						/* advance the pointers so that LF char is skipped next time when chars are sent */
						sent_buf = parse_buf;
					}

				}
			}

		}
		parse_buf++; /* advance one character */
	}
	parse_buf--;
	/* send remaining chars */

	if ((!status) && ((parse_buf - sent_buf) > 0))
		status = txt_buf_to_display(sent_buf + 1, parse_buf - sent_buf, dev);

	dev->exceeded_rows = is_ignore_txt;

	return status;
}

/**
 * txt_buf_to_display - This function breaks sends the string to the PmodCLS device over the SPI.
	 prior to .
 *
 * Parameters
 * @char *txt_buf: the text array to be parsed
 * @int cnt the number of charcaters to be parsed in the text array
 * @struct pmodclp_device *dev	pointer to device structure
 *
 *
 * This function breaks sends the string to the PmodCLS device over the SPI.
 * It breaks the input string in chunks of 3 bytes in order to reduce the load on the receiving
 * PmodCLS.
 *
 */
static int txt_buf_to_display(char *txt_buf, int cnt, struct pmodcls_device *dev)
{
	int status; /* spi_write return value */
	int short_cnt;

	/*
	 * Break writes into three byte chunks so that the microcontroller on
	 * the PmodCLS can keep up. Prior to breaking up writes to these
	 * smaller chunks, every 4th character would not be displayed.
	 *
	 * NOTE: The usleep_range delay is not needed, but allows the driver
	 *	 to relinquish control to other tasks.
	 */
	status = 0;
	while ((cnt > 0) && (!status)) {
		short_cnt = (cnt > 3) ? 3 : cnt;
		status = spi_write(dev->spi, txt_buf, short_cnt);
		cnt -= short_cnt;
		txt_buf += short_cnt;
		usleep_range(10, 100);
	}
	return status;

}

/**
 * Driver Read Function
 *
 * This function does not actually read the PmodCLP as it is a write-only device.
 */
static ssize_t pmodcls_read(struct file *fp, char __user *buffer, size_t length, loff_t *offset)
{
	ssize_t retval = 0;

	return retval;
}

static const struct file_operations pmodcls_cdev_fops = {
	.owner		= THIS_MODULE,
	.write		= pmodcls_write,
	.read		= pmodcls_read,
	.open		= pmodcls_open,
	.release	= pmodcls_close,
};

/**
 * add_pmodcls_device_to_bus - Add device to SPI bus, initialize SPI data.
 * @dev: pointer to device tree node
 *
 * This function adds device to SPI bus, initialize SPI data.
 */
static int add_pmodcls_device_to_bus(struct pmodcls_device *dev)
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
 * pmodcls_setup_cdev - Setup Char Device for ZED PmodCLP device.
 * @dev: pointer to device tree node
 * @dev_id: pointer to device major and minor number
 * @spi: pointer to spi_device structure
 *
 * This function initializes char device for PmodCLS device, and add it into
 * kernel device structure. It returns 0, if the cdev is successfully
 * initialized, or a negative value if there is an error.
 */
static int pmodcls_setup_cdev(struct pmodcls_device *dev, dev_t *dev_id, struct spi_device *spi)
{
	int status = 0;
	struct device *device;

	cdev_init(&dev->cdev, &pmodcls_cdev_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &pmodcls_cdev_fops;
	dev->spi = spi;

	*dev_id = MKDEV(MAJOR(pmodcls_dev_id), cur_minor++);
	status = cdev_add(&dev->cdev, *dev_id, 1);
	if (status < 0)
		return status;

	/* Add Device node in system */
	device = device_create(pmodcls_class, NULL,
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
 */
static int pmodcls_spi_probe(struct spi_device *spi)
{
	int status = 0;
	struct pmodcls_device *pmodcls_dev;

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

	/* Get pmodcls_device structure */
	pmodcls_dev = (struct pmodcls_device *)spi->dev.platform_data;
	if (pmodcls_dev == NULL) {
		dev_err(&spi->dev, "Cannot get pmodcls_device.\n");
		status = -EINVAL;
		goto spi_platform_data_err;
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(SPI_DRIVER_NAME " [%s] spi_probe: setup char device\n", pmodcls_dev->name);
#endif

	/* Setup char driver */
	status = pmodcls_setup_cdev(pmodcls_dev, &(pmodcls_dev->dev_id), spi);
	if (status) {
		pr_info(" spi_probe: Error adding %s device: %d\n", SPI_DRIVER_NAME, status);
		dev_err(&spi->dev, "spi_probe: Error adding %s device: %d\n", SPI_DRIVER_NAME, status);
		goto cdev_add_err;
	}

	/* Initialize Mutex */
	mutex_init(&pmodcls_dev->mutex);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(SPI_DRIVER_NAME " [%s] spi_probe: initialize device\n", pmodcls_dev->name);
#endif

	return status;

cdev_add_err:
spi_platform_data_err:
spi_err:
	return status;
}

static int pmodcls_spi_remove(struct spi_device *spi)
{
	int status;
	struct pmodcls_device *dev;

	dev = (struct pmodcls_device *)spi->dev.platform_data;

	if (dev == NULL) {
		dev_err(&spi->dev, "spi_remove: Error fetch pmodcls_device struct\n");
		return -EINVAL;
	}

	if (&dev->cdev) {
#ifdef CONFIG_PMODS_DEBUG
		pr_info(SPI_DRIVER_NAME " [%s] spi_remove: Destroy Char Device\n", dev->name);
#endif
		device_destroy(pmodcls_class, dev->dev_id);
		cdev_del(&dev->cdev);
	}

	cur_minor--;

	return status;
}

static struct spi_driver pmodcls_spi_driver = {
	.driver		= {
		.name	= SPI_DRIVER_NAME,
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= pmodcls_spi_probe,
	.remove		= pmodcls_spi_remove,
};

static const struct of_device_id pmodcls_of_match[] = {
	{ .compatible = "dglnt,pmodcls", },
	{},
};
MODULE_DEVICE_TABLE(of, pmodcls_of_match);

/**
 * pmodcls_of_probe - Probe method for PmodCLS device (over GPIO).
 * @pdev: pointer to platform devices
 *
 * This function probes the PmodCLS device in the device tree. It initializes the
 * PmodCLS driver data structure. It returns 0, if the driver is bound to the PmodCLS
 * device, or a negative value if there is an error.
 */
static int pmodcls_of_probe(struct platform_device *pdev)
{
	struct pmodcls_device *pmodcls_dev;
	struct platform_device *pmodcls_pdev;
	struct spi_gpio_platform_data *pmodcls_pdata;

	struct device_node *np = pdev->dev.of_node;

	const u32 *tree_info;
	const u32 *spi_speed;
	int status = 0;

	/* Alloc Space for platform device structure */
	pmodcls_dev = kzalloc(sizeof(*pmodcls_dev), GFP_KERNEL);
	if (!pmodcls_dev) {
		status = -ENOMEM;
		goto dev_alloc_err;
	}

	/* Alloc Text Buffer for device */
	pmodcls_dev->txt_buf = kmalloc(TXT_BUF_SIZE, GFP_KERNEL);
	if (!pmodcls_dev->txt_buf) {
		status = -ENOMEM;
		dev_err(&pdev->dev, "Device Display data buffer allocation failed: %d\n", status);
		goto txt_buf_alloc_err;
	}

	/* Get the GPIO Pins */

	pmodcls_dev->iSCLK = of_get_named_gpio(np, "spi-sclk-gpio", 0);
	pmodcls_dev->iSDIN = of_get_named_gpio(np, "spi-sdin-gpio", 0);
	status = of_get_named_gpio(np, "spi-cs-gpio", 0);
	pmodcls_dev->iCS = (status < 0) ? SPI_GPIO_NO_CHIPSELECT : status;

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: iSCLK: 0x%lx\n", np->name, pmodcls_dev->iSCLK);
	pr_info(DRIVER_NAME " %s: iSDIN: 0x%lx\n", np->name, pmodcls_dev->iSDIN);
	pr_info(DRIVER_NAME " %s: iCS : 0x%lx\n", np->name, pmodcls_dev->iCS);
#endif

	/* Get SPI Related Params */
	tree_info = of_get_property(np, "spi-bus-num", NULL);
	if (tree_info) {
		pmodcls_dev->spi_id = be32_to_cpup((tree_info));
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " %s: BUS_ID\t%x\n", np->name, pmodcls_dev->spi_id);
#endif
	}

	spi_speed = of_get_property(np, "spi-speed-hz", NULL);
	if (spi_speed) {
		pmodcls_dev->spi_speed = be32_to_cpup((spi_speed));
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " %s: SPI_SPEED\t%x\n", np->name, pmodcls_dev->spi_speed);
#endif
	} else {
		pmodcls_dev->spi_speed = DEFAULT_SPI_SPEED;
	}
	/* Alloc Space for platform data structure */
	pmodcls_pdata = kzalloc(sizeof(*pmodcls_pdata), GFP_KERNEL);
	if (!pmodcls_pdata) {
		status = -ENOMEM;
		goto pdata_alloc_err;
	}

	/* Fill up Platform Data Structure */
	pmodcls_pdata->sck = pmodcls_dev->iSCLK;
	pmodcls_pdata->miso = SPI_GPIO_NO_MISO;
	pmodcls_pdata->mosi = pmodcls_dev->iSDIN;
	pmodcls_pdata->num_chipselect = 1;

	/* Alloc Space for platform data structure */
	pmodcls_pdev = kzalloc(sizeof(*pmodcls_pdev), GFP_KERNEL);
	if (!pmodcls_pdev) {
		status = -ENOMEM;
		goto pdev_alloc_err;
	}

	/* Fill up Platform Device Structure */
	pmodcls_pdev->name = "spi_gpio";
	pmodcls_pdev->id = pmodcls_dev->spi_id;
	pmodcls_pdev->dev.platform_data = pmodcls_pdata;
	pmodcls_dev->pdev = pmodcls_pdev;

	/* Register spi_gpio master */
	status = platform_device_register(pmodcls_dev->pdev);
	if (status < 0) {
		dev_err(&pdev->dev, "platform_device_register failed: %d\n", status);
		goto pdev_reg_err;
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: spi_gpio platform device registered.\n", np->name);
#endif
	pmodcls_dev->name = (char *)np->name;

	/* Fill up Board Info for SPI device */
	status = add_pmodcls_device_to_bus(pmodcls_dev);
	if (status < 0) {
		dev_err(&pdev->dev, "add_pmodcls_device_to_bus failed: %d\n", status);
		goto spi_add_err;
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: spi device registered.\n", np->name);
#endif

	/* Point device node data to pmodcls_device structure */
	if (np->data == NULL)
		np->data = pmodcls_dev;

	if (pmodcls_dev_id == 0) {
		/* Alloc Major & Minor number for char device */
		status = alloc_chrdev_region(&pmodcls_dev_id, 0, MAX_PMODCLS_DEV_NUM, DRIVER_NAME);
		if (status) {
			dev_err(&pdev->dev, "Character device region not allocated correctly: %d\n", status);
			goto err_alloc_chrdev_region;
		}
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Char Device Region Registered, with Major: %d.\n",
			MAJOR(pmodcls_dev_id));
#endif
	}

	if (pmodcls_class == NULL) {
		/* Create Pmodcls Device Class */
		pmodcls_class = class_create(THIS_MODULE, DRIVER_NAME);
		if (IS_ERR(pmodcls_class)) {
			status = PTR_ERR(pmodcls_class);
			goto err_create_class;
		}
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : pmodcls device class registered.\n");
#endif
	}

	if (spi_drv_registered == 0) {
		/* Register SPI Driver for Pmodcls Device */
		status = spi_register_driver(&pmodcls_spi_driver);
		if (status < 0) {
			dev_err(&pdev->dev, "pmodcls_spi_driver register failed: %d\n", status);
			goto err_spi_register;
		}
		spi_drv_registered = 1;
	}

	device_num++;

	return status;

err_spi_register:
	class_destroy(pmodcls_class);
	pmodcls_class = NULL;
err_create_class:
	unregister_chrdev_region(pmodcls_dev_id, MAX_PMODCLS_DEV_NUM);
	pmodcls_dev_id = 0;
err_alloc_chrdev_region:
	spi_unregister_device(pmodcls_dev->spi);
spi_add_err:
	platform_device_unregister(pmodcls_dev->pdev);
pdev_reg_err:
	kfree(pmodcls_pdev);
pdev_alloc_err:
	kfree(pmodcls_pdata);
pdata_alloc_err:
	kfree(pmodcls_dev->txt_buf);
txt_buf_alloc_err:
	kfree(pmodcls_dev);
dev_alloc_err:
	return status;
}

/**
 * pmodcls_of_remove - Remove method for ZED PmodCLS device.
 * @np: pointer to device tree node
 *
 * This function removes the PmodCLS device in the device tree. It frees the
 * PmodCLS driver data structure. It returns 0, if the driver is successfully
 * removed, or a negative value if there is an error.
 */
static int pmodcls_of_remove(struct platform_device *pdev)
{
	struct pmodcls_device *pmodcls_dev;
	struct device_node *np = pdev->dev.of_node;

	if (np->data == NULL) {
		dev_err(&pdev->dev, "pmodcls %s: ERROR: No pmodcls_device structure found!\n", np->name);
		return -ENOSYS;
	}
	pmodcls_dev = (struct pmodcls_device *)(np->data);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s : Free display buffer.\n", np->name);
#endif

	if (pmodcls_dev->txt_buf != NULL)
		kfree(pmodcls_dev->txt_buf);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s : Unregister gpio_spi Platform Devices.\n", np->name);
#endif

	if (pmodcls_dev->pdev != NULL)
		platform_device_unregister(pmodcls_dev->pdev);

	np->data = NULL;
	device_num--;

	/* Unregister SPI Driver, Destroy pmodcls class, Release device id Region after
	 * all pmodcls devices have been removed.
	 */
	if (device_num == 0) {
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Unregister SPI Driver.\n");
#endif
		spi_unregister_driver(&pmodcls_spi_driver);
		spi_drv_registered = 0;

#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Destroy pmodcls_gpio Class.\n");
#endif

		if (pmodcls_class)
			class_destroy(pmodcls_class);
		pmodcls_class = NULL;

#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Release Char Device Region.\n");
#endif

		unregister_chrdev_region(pmodcls_dev_id, MAX_PMODCLS_DEV_NUM);
		pmodcls_dev_id = 0;
	}

	return 0;
}

static struct platform_driver pmodcls_driver = {
	.driver			= {
		.name		= DRIVER_NAME,
		.owner		= THIS_MODULE,
		.of_match_table = pmodcls_of_match,
	},
	.probe			= pmodcls_of_probe,
	.remove			= pmodcls_of_remove,
};

module_platform_driver(pmodcls_driver);

MODULE_AUTHOR("Digilent, Inc.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_NAME ": PmodCLS display driver");
MODULE_ALIAS(DRIVER_NAME);
