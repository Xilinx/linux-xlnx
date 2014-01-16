/*
 * pmodolde-gpio.c - PmodOLED-GPIO driver
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
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
#include <asm/uaccess.h>

#define DRIVER_NAME "pmodoled-gpio"
#define SPI_DRIVER_NAME "pmodoled-gpio-spi"
#define MAX_PMODOLED_GPIO_DEV_NUM 16
#define DISPLAY_BUF_SZ 512      /* 32 x 128 bit monochrome == 512 bytes */
#define MAX_LINE_LEN 16         /* 128 bits wide and current char width is 8 bit */
#define MAX_ROW 4
#define OLED_MAX_PG_CNT 4       /* number of display pages in OLED controller */
#define OLED_CONTROLLER_PG_SZ 128
#define OLED_CONTROLLER_CMD 0
#define OLED_CONTROLLER_DATA 1

/* commands for the OLED display controller	*/
#define OLED_SET_PG_ADDR 0x22
#define OLED_DISPLAY_OFF 0xAE
#define OLED_DISPLAY_ON 0xAF
#define OLED_CONTRAST_CTRL 0x81
#define OLED_SET_PRECHARGE_PERIOD 0xD9
#define OLED_SET_SEGMENT_REMAP 0xA1
#define OLED_SET_COM_DIR 0xC8
#define OLED_SET_COM_PINS 0xDA

static dev_t gpio_pmodoled_dev_id;
static unsigned int device_num;
static unsigned int cur_minor;
static unsigned int spi_drv_registered;
/* struct mutex minor_mutex; */
static struct class *gpio_pmodoled_class;

struct gpio_pmodoled_device {
	const char *name;
	/* R/W Mutex Lock */
	struct mutex mutex;
	/* Display Buffers */
	uint8_t disp_on;
	uint8_t *disp_buf;
	/* Pin Assignment */
	unsigned long iVBAT;
	unsigned long iVDD;
	unsigned long iRES;
	unsigned long iDC;
	unsigned long iSCLK;
	unsigned long iSDIN;
	unsigned long iCS;
	/* SPI Info */
	uint32_t spi_id;
	/* platform device structures */
	struct platform_device *pdev;
	/* Char Device */
	struct cdev cdev;
	struct spi_device *spi;
	dev_t dev_id;
};

/**
 * screen_buf_to_display -
 * @screen_buf -
 * @dev -
 *
 */
static int screen_buf_to_display(uint8_t *screen_buf, struct gpio_pmodoled_device *dev)
{
	uint32_t pg;
	int status;
	uint8_t lower_start_column = 0x00;
	uint8_t upper_start_column = 0x10;
	uint8_t wr_buf[10];

	for (pg = 0; pg < OLED_MAX_PG_CNT; pg++) {
		wr_buf[0] = OLED_SET_PG_ADDR;
		wr_buf[1] = pg;
		wr_buf[2] = lower_start_column;
		wr_buf[3] = upper_start_column;
		gpio_set_value(dev->iDC, OLED_CONTROLLER_CMD);
		status = spi_write(dev->spi, wr_buf, 4);
		if (status) {
			dev_err(&dev->spi->dev, "screen_buf_to_display: Error writing to SPI\n");
			break;
		}

		gpio_set_value(dev->iDC, OLED_CONTROLLER_DATA);
		status = spi_write(dev->spi, (uint8_t *)(screen_buf +
							 (pg * OLED_CONTROLLER_PG_SZ)), OLED_CONTROLLER_PG_SZ);
		if (status) {
			dev_err(&dev->spi->dev, "screen_buf_to_display: Error writing to SPI\n");
			break;
		}
	}
	return status;
}

/**
 * A basic open function. It exists mainly to save the id of
 * the OLED and some other basic information.
 */
static int gpio_pmodoled_open(struct inode *inode, struct file *fp)
{
	struct gpio_pmodoled_device *dev;

	dev = container_of(inode->i_cdev, struct gpio_pmodoled_device, cdev);
	fp->private_data = dev;

	return 0;
}

static int gpio_pmodoled_close(struct inode *inode, struct file *fp)
{
	return 0;
}

/**
 * Driver write function
 *
 * This function uses a generic SPI write to send values to the Pmod device
 * It takes a raw data array from the app in the buffer, copied it into
 * device dispay buffer, and finally sends the buffer to the OLED using SPI
 */
static ssize_t gpio_pmodoled_write(struct file *fp, const char __user *buffer, size_t length, loff_t *offset)
{
	ssize_t retval = 0;
	struct gpio_pmodoled_device *dev;
	unsigned int minor_id;
	int cnt;
	int status;

	dev = fp->private_data;
	minor_id = MINOR(dev->dev_id);

	if (mutex_lock_interruptible(&dev->mutex)) {
		retval = -ERESTARTSYS;
		goto write_lock_err;
	}

	if (buffer == NULL) {
		dev_err(&dev->spi->dev, "oled_write: ERROR: invalid buffer address: 0x%08lx\n",
			(__force unsigned long)buffer);
		retval = -EINVAL;
		goto quit_write;
	}

	if (length > DISPLAY_BUF_SZ)
		cnt = DISPLAY_BUF_SZ;
	else
		cnt = length;

	if (copy_from_user(dev->disp_buf, buffer, cnt)) {
		dev_err(&dev->spi->dev, "oled_write: copy_from_user failed\n");
		retval = -EFAULT;
		goto quit_write;
	} else
		retval = cnt;

	status = screen_buf_to_display(dev->disp_buf, dev);
	if (status) {
		dev_err(&dev->spi->dev, "oled_write: Error sending string to display\n");
		retval = -EFAULT;
		goto quit_write;
	}

quit_write:
	mutex_unlock(&dev->mutex);
write_lock_err:
	return retval;
}

/**
 * Driver Read Function
 *
 * This function does not actually read the Pmod as it is a write-only device. Instead
 * It returns data in the buffer generated for the display that was used when the OLED
 * was last programmed.
 */
static ssize_t gpio_pmodoled_read(struct file *fp, char __user *buffer, size_t length, loff_t *offset)
{
	ssize_t retval = 0;
	struct gpio_pmodoled_device *dev;
	unsigned int minor_id;
	int cnt;

	dev = fp->private_data;
	minor_id = MINOR(dev->dev_id);

	if (mutex_lock_interruptible(&dev->mutex)) {
		retval = -ERESTARTSYS;
		goto read_lock_err;
	}

	if (buffer == NULL) {
		dev_err(&dev->spi->dev, "OLED_read: ERROR: invalid buffer "
			"address: 0x%08lx\n", (__force unsigned long)buffer);
		retval = -EINVAL;
		goto quit_read;
	}

	if (length > DISPLAY_BUF_SZ)
		cnt = DISPLAY_BUF_SZ;
	else
		cnt = length;
	retval = copy_to_user((void __user *)buffer, dev->disp_buf, cnt);
	if (!retval)
		retval = cnt; /* copy success, return amount in buffer */

quit_read:
	mutex_unlock(&dev->mutex);
read_lock_err:
	return retval;
}

static struct file_operations gpio_pmodoled_cdev_fops = {
	.owner		= THIS_MODULE,
	.write		= gpio_pmodoled_write,
	.read		= gpio_pmodoled_read,
	.open		= gpio_pmodoled_open,
	.release	= gpio_pmodoled_close,
};

static int add_gpio_pmodoled_device_to_bus(struct gpio_pmodoled_device *dev)
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
	spi_device->max_speed_hz = 4000000;
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

	return status;
}

/**
 * gpio_pmodoled_setup_cdev - Setup Char Device for ZED on-board OLED device.
 * @dev: pointer to device tree node
 * @dev_id: pointer to device major and minor number
 * @spi: pointer to spi_device structure
 *
 * This function initializes char device for OLED device, and add it into
 * kernel device structure. It returns 0, if the cdev is successfully
 * initialized, or a negative value if there is an error.
 */
static int gpio_pmodoled_setup_cdev(struct gpio_pmodoled_device *dev, dev_t *dev_id, struct spi_device *spi)
{
	int status = 0;
	struct device *device;

	cdev_init(&dev->cdev, &gpio_pmodoled_cdev_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &gpio_pmodoled_cdev_fops;
	dev->spi = spi;

	*dev_id = MKDEV(MAJOR(gpio_pmodoled_dev_id), cur_minor++);
	status = cdev_add(&dev->cdev, *dev_id, 1);
	if (status < 0)
		return status;

	/* Add Device node in system */
	device = device_create(gpio_pmodoled_class, NULL,
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
 * gpio_pmodoled_init_gpio - Initialize GPIO for ZED Onboard OLED
 * @dev - gpio_pmodoled_device
 *
 * Initializes OLED GPIO Control Pins.
 * It returns 0, if the gpio pins are successfully
 * initialized, or a negative value if there is an error.
 */
static int gpio_pmodoled_init_gpio(struct gpio_pmodoled_device *dev)
{
	struct gpio gpio_pmodoled_ctrl[] = {
		{ dev->iVBAT, GPIOF_OUT_INIT_HIGH, "OLED VBat"	},
		{ dev->iVDD,  GPIOF_OUT_INIT_HIGH, "OLED VDD"	},
		{ dev->iRES,  GPIOF_OUT_INIT_HIGH, "OLED_RESET" },
		{ dev->iDC,   GPIOF_OUT_INIT_HIGH, "OLED_D/C"	},
	};
	int status;
	int i;

	for (i = 0; i < ARRAY_SIZE(gpio_pmodoled_ctrl); i++) {
		status = gpio_is_valid(gpio_pmodoled_ctrl[i].gpio);
		if (!status) {
			dev_err(&dev->spi->dev, "!! gpio_is_valid for GPIO %d, %s FAILED!, status: %d\n",
				gpio_pmodoled_ctrl[i].gpio, gpio_pmodoled_ctrl[i].label, status);
			goto gpio_invalid;
		}
	}

	status = gpio_request_array(gpio_pmodoled_ctrl, ARRAY_SIZE(gpio_pmodoled_ctrl));
	if (status) {
		dev_err(&dev->spi->dev, "!! gpio_request_array FAILED!\n");
		dev_err(&dev->spi->dev, " status is: %d\n", status);
		gpio_free_array(gpio_pmodoled_ctrl, 4);
		goto gpio_invalid;
	}

gpio_invalid:
	return status;
}

/**
 * gpio_pmodoled_disp_init -
 * @dev:
 *
 */
static void gpio_pmodoled_disp_init(struct gpio_pmodoled_device *dev)
{
	int status;
	uint8_t wr_buf[20];

	/* We are going to be sending commands
	 * so clear the data/cmd bit */
	gpio_set_value(dev->iDC, OLED_CONTROLLER_CMD);

	/* Start by turning VDD on and wait for the power to come up */
	gpio_set_value(dev->iVDD, 0);
	msleep(1);

	/* Display off Command */
	wr_buf[0] = OLED_DISPLAY_OFF;
	status = spi_write(dev->spi, wr_buf, 1);

	/* Bring Reset Low and then High */
	gpio_set_value(dev->iRES, 1);
	msleep(1);
	gpio_set_value(dev->iRES, 0);
	msleep(1);
	gpio_set_value(dev->iRES, 1);

	/* Send the set charge pump and set precharge period commands */
	wr_buf[0] = 0x8D;
	wr_buf[1] = 0x14;
	wr_buf[2] = OLED_SET_PRECHARGE_PERIOD;
	wr_buf[3] = 0xF1;

	status = spi_write(dev->spi, wr_buf, 4);

	/* Turn on VCC and wait 100ms */
	gpio_set_value(dev->iVBAT, 0);
	msleep(100);

	/* Set Display COntrast */
	wr_buf[0] = OLED_CONTRAST_CTRL;
	wr_buf[1] = 0x0F;

	/* Invert the display */
	wr_buf[2] = OLED_SET_SEGMENT_REMAP;     /* Remap Columns */
	wr_buf[3] = OLED_SET_COM_DIR;           /* Remap Rows */

	/* Select sequential COM configuration */
	wr_buf[4] = OLED_SET_COM_PINS;
	wr_buf[5] = 0x00;
	wr_buf[6] = 0xC0;
	wr_buf[7] = 0x20;
	wr_buf[8] = 0x00;

	/* Turn on Display */
	wr_buf[9] = OLED_DISPLAY_ON;

	status = spi_write(dev->spi, wr_buf, 10);
}

/**
 * SPI hardware probe. Sets correct SPI mode, attempts
 * to obtain memory needed by the driver, and performs
 * a simple initialization of the device.
 */
static int gpio_pmodoled_spi_probe(struct spi_device *spi)
{
	int status = 0;
	struct gpio_pmodoled_device *gpio_pmodoled_dev;

	/* We rely on full duplex transfers, mostly to reduce
	 * per transfer overheads (by making few transfers).
	 */
	if (spi->master->flags & SPI_MASTER_HALF_DUPLEX) {
		status = -EINVAL;
		dev_err(&spi->dev, "SPI settings incorrect: %d\n", status);
		goto spi_err;
	}

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

	/* Get gpio_pmodoled_device structure */
	gpio_pmodoled_dev = (struct gpio_pmodoled_device *)spi->dev.platform_data;
	if (gpio_pmodoled_dev == NULL) {
		dev_err(&spi->dev, "Cannot get gpio_pmodoled_device.\n");
		status = -EINVAL;
		goto spi_platform_data_err;
	}

	pr_info(SPI_DRIVER_NAME " [%s] SPI Probing\n", gpio_pmodoled_dev->name);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(SPI_DRIVER_NAME " [%s] spi_probe: setup char device\n", gpio_pmodoled_dev->name);
#endif

	/* Setup char driver */
	status = gpio_pmodoled_setup_cdev(gpio_pmodoled_dev, &(gpio_pmodoled_dev->dev_id), spi);
	if (status) {
		dev_err(&spi->dev, "spi_probe: Error adding %s device: %d\n", SPI_DRIVER_NAME, status);
		goto cdev_add_err;
	}

	/* Initialize Mutex */
	mutex_init(&gpio_pmodoled_dev->mutex);

	/**
	 * It is important to the OLED's longevity that the lines that
	 * control it's power are carefully controlled. This is a good
	 * time to ensure that the device is ot turned on until it is
	 * instructed to do so.
	 */
#ifdef CONFIG_PMODS_DEBUG
	pr_info(SPI_DRIVER_NAME " [%s] spi_probe: initialize device\n", gpio_pmodoled_dev->name);
#endif

	status = gpio_pmodoled_init_gpio(gpio_pmodoled_dev);
	if (status) {
		dev_err(&spi->dev, "spi_probe: Error initializing GPIO\n");
		goto oled_init_error;
	}

	gpio_pmodoled_disp_init(gpio_pmodoled_dev);

	memset(gpio_pmodoled_dev->disp_buf, 0x00, DISPLAY_BUF_SZ);

	status = screen_buf_to_display(gpio_pmodoled_dev->disp_buf, gpio_pmodoled_dev);
	if (status) {
		dev_err(&spi->dev, "spi_probe: Error sending initial Display String\n");
		goto oled_init_error;
	}
	return status;

oled_init_error:
	if (&gpio_pmodoled_dev->cdev)
		cdev_del(&gpio_pmodoled_dev->cdev);
cdev_add_err:
spi_platform_data_err:
spi_err:
	return status;
}

static int gpio_pmodoled_spi_remove(struct spi_device *spi)
{
	int status;
	struct gpio_pmodoled_device *dev;
	uint8_t wr_buf[10];

	dev = (struct gpio_pmodoled_device *)spi->dev.platform_data;

	if (dev == NULL) {
		dev_err(&spi->dev, "spi_remove: Error fetch gpio_pmodoled_device struct\n");
		return -EINVAL;
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(SPI_DRIVER_NAME " [%s] spi_remove: Clearing Display\n", dev->name);
#endif

	/* Clear Display */
	memset(dev->disp_buf, 0, DISPLAY_BUF_SZ);
	status = screen_buf_to_display(dev->disp_buf, dev);

	/* Turn off display */
	wr_buf[0] = OLED_DISPLAY_OFF;
	status = spi_write(spi, wr_buf, 1);
	if (status)
		dev_err(&spi->dev, "oled_spi_remove: Error writing to SPI device\n");

	/* Turn off VCC (VBAT) */
	gpio_set_value(dev->iVBAT, 1);
	msleep(100);
	/* TUrn off VDD Power */
	gpio_set_value(dev->iVDD, 1);
#ifdef CONFIG_PMODS_DEBUG
	pr_info(SPI_DRIVER_NAME " [%s] spi_remove: Free GPIOs\n", dev->name);
#endif

	{
		struct gpio gpio_pmodoled_ctrl[] = {
			{ dev->iVBAT, GPIOF_OUT_INIT_HIGH, "OLED VBat"	},
			{ dev->iVDD,  GPIOF_OUT_INIT_HIGH, "OLED VDD"	},
			{ dev->iRES,  GPIOF_OUT_INIT_HIGH, "OLED_RESET" },
			{ dev->iDC,   GPIOF_OUT_INIT_HIGH, "OLED_D/C"	},
		};

		gpio_free_array(gpio_pmodoled_ctrl, 4);
	}

	if (&dev->cdev) {
#ifdef CONFIG_PMODS_DEBUG
		pr_info(SPI_DRIVER_NAME " [%s] spi_remove: Destroy Char Device\n", dev->name);
#endif
		device_destroy(gpio_pmodoled_class, dev->dev_id);
		cdev_del(&dev->cdev);
	}

	cur_minor--;

	pr_info(SPI_DRIVER_NAME " [%s] spi_remove: Device Removed\n", dev->name);

	return status;
}

static struct spi_driver gpio_pmodoled_spi_driver = {
	.driver		= {
		.name	= SPI_DRIVER_NAME,
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= gpio_pmodoled_spi_probe,
	.remove		= gpio_pmodoled_spi_remove,
};

static const struct of_device_id gpio_pmodoled_of_match[] = {
	{ .compatible = "dglnt,pmodoled-gpio", },
	{},
};
MODULE_DEVICE_TABLE(of, gpio_pmodoled_of_match);

/**
 * gpio_pmodoled_of_probe - Probe method for PmodOLED device (over GPIO).
 * @pdev: pointer to platform devices
 *
 * This function probes the OLED device in the device tree. It initializes the
 * OLED driver data structure. It returns 0, if the driver is bound to the OLED
 * device, or a negative value if there is an error.
 */
static int gpio_pmodoled_of_probe(struct platform_device *pdev)
{
	struct gpio_pmodoled_device *gpio_pmodoled_dev;
	struct platform_device *gpio_pmodoled_pdev;
	struct spi_gpio_platform_data *gpio_pmodoled_pdata;

	struct device_node *np = pdev->dev.of_node;

	const u32 *tree_info;
	int status = 0;

	/* Alloc Space for platform device structure */
	gpio_pmodoled_dev = (struct gpio_pmodoled_device *)kzalloc(sizeof(*gpio_pmodoled_dev), GFP_KERNEL);
	if (!gpio_pmodoled_dev) {
		status = -ENOMEM;
		goto dev_alloc_err;
	}

	/* Alloc Graphic Buffer for device */
	gpio_pmodoled_dev->disp_buf = (uint8_t *)kmalloc(DISPLAY_BUF_SZ, GFP_KERNEL);
	if (!gpio_pmodoled_dev->disp_buf) {
		status = -ENOMEM;
		dev_err(&pdev->dev, "Device Display data buffer allocation failed: %d\n", status);
		goto disp_buf_alloc_err;
	}

	/* Get the GPIO Pins */
	gpio_pmodoled_dev->iVBAT = of_get_named_gpio(np, "vbat-gpio", 0);
	gpio_pmodoled_dev->iVDD = of_get_named_gpio(np, "vdd-gpio", 0);
	gpio_pmodoled_dev->iRES = of_get_named_gpio(np, "res-gpio", 0);
	gpio_pmodoled_dev->iDC = of_get_named_gpio(np, "dc-gpio", 0);
	gpio_pmodoled_dev->iSCLK = of_get_named_gpio(np, "spi-sclk-gpio", 0);
	gpio_pmodoled_dev->iSDIN = of_get_named_gpio(np, "spi-sdin-gpio", 0);
	status = of_get_named_gpio(np, "spi-cs-gpio", 0);
	gpio_pmodoled_dev->iCS = (status < 0) ? SPI_GPIO_NO_CHIPSELECT : status;
#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: iVBAT: 0x%lx\n", np->name, gpio_pmodoled_dev->iVBAT);
	pr_info(DRIVER_NAME " %s: iVDD : 0x%lx\n", np->name, gpio_pmodoled_dev->iVDD);
	pr_info(DRIVER_NAME " %s: iRES : 0x%lx\n", np->name, gpio_pmodoled_dev->iRES);
	pr_info(DRIVER_NAME " %s: iDC : 0x%lx\n", np->name, gpio_pmodoled_dev->iDC);
	pr_info(DRIVER_NAME " %s: iSCLK: 0x%lx\n", np->name, gpio_pmodoled_dev->iSCLK);
	pr_info(DRIVER_NAME " %s: iSDIN: 0x%lx\n", np->name, gpio_pmodoled_dev->iSDIN);
	pr_info(DRIVER_NAME " %s: iCS : 0x%lx\n", np->name, gpio_pmodoled_dev->iCS);
#endif

	/* Get SPI Related Params */
	tree_info = of_get_property(np, "spi-bus-num", NULL);
	if (tree_info) {
		gpio_pmodoled_dev->spi_id = be32_to_cpup((tree_info));
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " %s: BUS_ID\t%x\n", np->name, gpio_pmodoled_dev->spi_id);
#endif
	}

	/* Alloc Space for platform data structure */
	gpio_pmodoled_pdata = (struct spi_gpio_platform_data *)kzalloc(sizeof(*gpio_pmodoled_pdata), GFP_KERNEL);
	if (!gpio_pmodoled_pdata) {
		status = -ENOMEM;
		goto pdata_alloc_err;
	}

	/* Fill up Platform Data Structure */
	gpio_pmodoled_pdata->sck = gpio_pmodoled_dev->iSCLK;
	gpio_pmodoled_pdata->miso = SPI_GPIO_NO_MISO;
	gpio_pmodoled_pdata->mosi = gpio_pmodoled_dev->iSDIN;
	gpio_pmodoled_pdata->num_chipselect = 1;

	/* Alloc Space for platform data structure */
	gpio_pmodoled_pdev = (struct platform_device *)kzalloc(sizeof(*gpio_pmodoled_pdev), GFP_KERNEL);
	if (!gpio_pmodoled_pdev) {
		status = -ENOMEM;
		goto pdev_alloc_err;
	}

	/* Fill up Platform Device Structure */
	gpio_pmodoled_pdev->name = "spi_gpio";
	gpio_pmodoled_pdev->id = gpio_pmodoled_dev->spi_id;
	gpio_pmodoled_pdev->dev.platform_data = gpio_pmodoled_pdata;
	gpio_pmodoled_dev->pdev = gpio_pmodoled_pdev;

	/* Register spi_gpio master */
	status = platform_device_register(gpio_pmodoled_dev->pdev);
	if (status < 0) {
		dev_err(&pdev->dev, "platform_device_register failed: %d\n", status);
		goto pdev_reg_err;
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: spi_gpio platform device registered.\n", np->name);
#endif
	gpio_pmodoled_dev->name = np->name;

	/* Fill up Board Info for SPI device */
	status = add_gpio_pmodoled_device_to_bus(gpio_pmodoled_dev);
	if (status < 0) {
		dev_err(&pdev->dev, "add_gpio_pmodoled_device_to_bus failed: %d\n", status);
		goto spi_add_err;
	}

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s: spi device registered.\n", np->name);
#endif

	/* Point device node data to gpio_pmodoled_device structure */
	if (np->data == NULL)
		np->data = gpio_pmodoled_dev;

	if (gpio_pmodoled_dev_id == 0) {
		/* Alloc Major & Minor number for char device */
		status = alloc_chrdev_region(&gpio_pmodoled_dev_id, 0, MAX_PMODOLED_GPIO_DEV_NUM, DRIVER_NAME);
		if (status) {
			dev_err(&pdev->dev, "Character device region not allocated correctly: %d\n", status);
			goto err_alloc_chrdev_region;
		}
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Char Device Region Registered, with Major: %d.\n",
			MAJOR(gpio_pmodoled_dev_id));
#endif
	}

	if (gpio_pmodoled_class == NULL) {
		/* Create Pmodoled-gpio Device Class */
		gpio_pmodoled_class = class_create(THIS_MODULE, DRIVER_NAME);
		if (IS_ERR(gpio_pmodoled_class)) {
			status = PTR_ERR(gpio_pmodoled_class);
			goto err_create_class;
		}
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : pmodoled_gpio device class registered.\n");
#endif
	}

	if (spi_drv_registered == 0) {
		/* Register SPI Driver for Pmodoled Device */
		status = spi_register_driver(&gpio_pmodoled_spi_driver);
		if (status < 0) {
			dev_err(&pdev->dev, "gpio_pmodoled_spi_driver register failed: %d\n", status);
			goto err_spi_register;
		}
		spi_drv_registered = 1;
	}

	device_num++;

	return status;

err_spi_register:
	class_destroy(gpio_pmodoled_class);
	gpio_pmodoled_class = NULL;
err_create_class:
	unregister_chrdev_region(gpio_pmodoled_dev_id, MAX_PMODOLED_GPIO_DEV_NUM);
	gpio_pmodoled_dev_id = 0;
err_alloc_chrdev_region:
	spi_unregister_device(gpio_pmodoled_dev->spi);
spi_add_err:
	platform_device_unregister(gpio_pmodoled_dev->pdev);
pdev_reg_err:
	kfree(gpio_pmodoled_pdev);
pdev_alloc_err:
	kfree(gpio_pmodoled_pdata);
pdata_alloc_err:
	kfree(gpio_pmodoled_dev->disp_buf);
disp_buf_alloc_err:
	kfree(gpio_pmodoled_dev);
dev_alloc_err:
	return status;
}

/**
 * gpio_pmodoled_of_remove - Remove method for ZED on-board OLED device.
 * @np: pointer to device tree node
 *
 * This function removes the OLED device in the device tree. It frees the
 * OLED driver data structure. It returns 0, if the driver is successfully
 * removed, or a negative value if there is an error.
 */
static int gpio_pmodoled_of_remove(struct platform_device *pdev)
{
	struct gpio_pmodoled_device *gpio_pmodoled_dev;
	struct device_node *np = pdev->dev.of_node;

	if (np->data == NULL) {
		dev_err(&pdev->dev, "pmodoled %s: ERROR: No gpio_pmodoled_device structure found!\n", np->name);
		return -ENOSYS;
	}
	gpio_pmodoled_dev = (struct gpio_pmodoled_device *)(np->data);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s : Free display buffer.\n", np->name);
#endif

	if (gpio_pmodoled_dev->disp_buf != NULL)
		kfree(gpio_pmodoled_dev->disp_buf);

#ifdef CONFIG_PMODS_DEBUG
	pr_info(DRIVER_NAME " %s : Unregister gpio_spi Platform Devices.\n", np->name);
#endif

	if (gpio_pmodoled_dev->pdev != NULL)
		platform_device_unregister(gpio_pmodoled_dev->pdev);

	np->data = NULL;
	device_num--;

	/* Unregister SPI Driver, Destroy pmodoled-gpio class, Release device id Region after
	 * all pmodoled-gpio devices have been removed.
	 */
	if (device_num == 0) {
#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Unregister SPI Driver.\n");
#endif
		spi_unregister_driver(&gpio_pmodoled_spi_driver);
		spi_drv_registered = 0;

#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Destroy pmodoled_gpio Class.\n");
#endif

		if (gpio_pmodoled_class)
			class_destroy(gpio_pmodoled_class);

		gpio_pmodoled_class = NULL;

#ifdef CONFIG_PMODS_DEBUG
		pr_info(DRIVER_NAME " : Release Char Device Region.\n");
#endif

		unregister_chrdev_region(gpio_pmodoled_dev_id, MAX_PMODOLED_GPIO_DEV_NUM);
		gpio_pmodoled_dev_id = 0;
	}

	return 0;
}

static struct platform_driver gpio_pmodoled_driver = {
	.driver			= {
		.name		= DRIVER_NAME,
		.owner		= THIS_MODULE,
		.of_match_table = gpio_pmodoled_of_match,
	},
	.probe			= gpio_pmodoled_of_probe,
	.remove			= gpio_pmodoled_of_remove,
};

module_platform_driver(gpio_pmodoled_driver);

MODULE_AUTHOR("Digilent, Inc.");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(DRIVER_NAME ": PmodOLED display driver");
MODULE_ALIAS(DRIVER_NAME);
