/*
 * drivers/i2c/chips/m41t11.c
 *
 * I2C client driver for the ST M41T11 Real Time Clock chip.
 *
 * (C) Copyright (C) 2006, Greg Ungerer <gerg@snapgear.com>
 */

/*
 * This driver is very much a hybrid RTC and I2C driver. It has interfaces
 * into both sub-systems (well the RTC is really a misc device). Ultimately
 * I want to be able to use hwclock "as is" on the RTC. But the hardware is
 * a true I2c device...
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <asm/uaccess.h>

#define	M41T11_DRV_NAME		"m41t11"

/*
 * Size of RTC region. 64 bytes total, the first 10 are the RTC.
 */
#define	M41T11_MSIZE	0x3f

/*
 * M41T11 register offsets.
 */
#define	M41T11_SEC	0x00		/* Address of second register */
#define	M41T11_MIN	0x01		/* Address of minute register */
#define	M41T11_HOUR	0x02		/* Address of hour register */
#define	M41T11_WDAY	0x03		/* Address of day of week register */
#define	M41T11_MDAY	0x04		/* Address of day of month register */
#define	M41T11_MON	0x05		/* Address of month register */
#define	M41T11_YEAR	0x06		/* Address of year register */
#define	M41T11_FTOUT	0x07		/* Address of control register */

static DECLARE_MUTEX(m41t11_mutex);

/*
 * We keep a copy of the client device found, since we are really only
 * need one device for the real misc device interface.
 */
static struct i2c_client *client;

#define	m41t11_readbyte(a)	i2c_smbus_read_byte_data(client, a)
#define	m41t11_writebyte(a,v)	i2c_smbus_write_byte_data(client, a, v);

/*
 *****************************************************************************
 *
 *	RTC Driver Interface
 *
 *****************************************************************************
 */

static ssize_t m41t11_read(struct file *fp, char __user *buf, size_t count, loff_t *ptr)
{
	int total;

	if (fp->f_pos >= M41T11_MSIZE)
		return 0;

	if (count > (M41T11_MSIZE - fp->f_pos))
		count = M41T11_MSIZE - fp->f_pos;

	down(&m41t11_mutex);
	for (total = 0; (total < count); total++)
		put_user(m41t11_readbyte(fp->f_pos + total), buf++);
	up(&m41t11_mutex);

	fp->f_pos += total;
	return total;
}

static ssize_t m41t11_write(struct file *fp, const char __user *buf, size_t count, loff_t *ptr)
{
	int total;
	char val;

	if (fp->f_pos >= M41T11_MSIZE)
		return 0;

	if (count > (M41T11_MSIZE - fp->f_pos))
		count = M41T11_MSIZE - fp->f_pos;

	down(&m41t11_mutex);
	for (total = 0; (total < count); total++, buf++) {
		get_user(val,buf);
		m41t11_writebyte((fp->f_pos + total), val);
	}
	up(&m41t11_mutex);

	fp->f_pos += total;
	return total;
}

/*
 *	Do some consistency checks on the time. On first power up the
 *	RTC may contain completely bogus junk, this will clean it up.
 *	Just for good measure we do this when writing to the RTC as well.
 */
static void m41t11_validatetime(struct rtc_time *rtime)
{
	if ((rtime->tm_year < 70) || (rtime->tm_year >= 200))
		rtime->tm_year = 70;
	if ((rtime->tm_mon < 0) || (rtime->tm_mon >= 12))
		rtime->tm_mon = 0;
	if ((rtime->tm_mday < 1) || (rtime->tm_mday > 31))
		rtime->tm_mday = 1;
	if ((rtime->tm_wday < 0) || (rtime->tm_wday >= 7))
		rtime->tm_wday = 0;
	if ((rtime->tm_hour < 0) || (rtime->tm_hour >= 24))
		rtime->tm_hour = 0;
	if ((rtime->tm_min < 0) || (rtime->tm_min >= 60))
		rtime->tm_min = 0;
	if ((rtime->tm_sec < 0) || (rtime->tm_sec >= 60))
		rtime->tm_sec = 0;
}

static void m41t11_readtime(struct rtc_time *rtime)
{
	down(&m41t11_mutex);
	memset(rtime, 0, sizeof(*rtime));
	rtime->tm_year = BCD2BIN(m41t11_readbyte(M41T11_YEAR)) +
		((m41t11_readbyte(M41T11_HOUR) & 0x40) ? 100 : 0);
	rtime->tm_mon = BCD2BIN(m41t11_readbyte(M41T11_MON & 0x1f)) - 1;
	rtime->tm_mday = BCD2BIN(m41t11_readbyte(M41T11_MDAY & 0x3f));
	rtime->tm_wday = BCD2BIN(m41t11_readbyte(M41T11_WDAY) & 0x7) - 1;
	rtime->tm_hour = BCD2BIN(m41t11_readbyte(M41T11_HOUR) & 0x3f);
	rtime->tm_min = BCD2BIN(m41t11_readbyte(M41T11_MIN) & 0x7f);
	rtime->tm_sec = BCD2BIN(m41t11_readbyte(M41T11_SEC) & 0x7f);
	up(&m41t11_mutex);
}

static void m41t11_settime(struct rtc_time *rtime)
{
	down(&m41t11_mutex);
	m41t11_writebyte(M41T11_YEAR, BIN2BCD(rtime->tm_year));
	m41t11_writebyte(M41T11_MON, BIN2BCD(rtime->tm_mon+1));
	m41t11_writebyte(M41T11_MDAY, BIN2BCD(rtime->tm_mday));
	m41t11_writebyte(M41T11_WDAY, BIN2BCD(rtime->tm_wday+1));
	m41t11_writebyte(M41T11_HOUR, BIN2BCD(rtime->tm_hour) |
		((rtime->tm_year > 99) ? 0xc0 : 0x80));
	m41t11_writebyte(M41T11_MIN, BIN2BCD(rtime->tm_min));
	m41t11_writebyte(M41T11_SEC, BIN2BCD(rtime->tm_sec));
	m41t11_writebyte(M41T11_FTOUT, 0x90);
	up(&m41t11_mutex);
}

static int m41t11_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rtc_time rtime;

	switch (cmd) {

	case RTC_RD_TIME:
		m41t11_readtime(&rtime);
		m41t11_validatetime(&rtime);
		if (copy_to_user((void __user *) arg, &rtime, sizeof(rtime)))
			return -EFAULT;
		break;

	case RTC_SET_TIME:
		if (!capable(CAP_SYS_TIME))
			return -EACCES;
		m41t11_validatetime(&rtime);
		if (copy_from_user(&rtime, (void __user *) arg, sizeof(rtime)))
			return -EFAULT;
		m41t11_settime(&rtime);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 *****************************************************************************
 *
 *	I2C Driver Interface
 *
 *****************************************************************************
 */

static unsigned short ignore[] = { I2C_CLIENT_END };
static unsigned short normal_addr[] = { 0x68, I2C_CLIENT_END };

static struct i2c_client_address_data addr_data = {
	.normal_i2c		= normal_addr,
	.probe			= ignore,
	.ignore			= ignore,
};

static struct i2c_driver m41t11_i2cdrv;

static int m41t11_probe(struct i2c_adapter *adap, int addr, int kind)
{
	struct i2c_client *c;
	int rc;
	int val;

	c = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	strncpy(c->name, M41T11_DRV_NAME, I2C_NAME_SIZE);
	c->addr = addr;
	c->adapter = adap;
	c->driver = &m41t11_i2cdrv;

	if ((rc = i2c_attach_client(c)) != 0) {
		kfree(c);
		return rc;
	}

	client = c;

	/* Start the oscillator if needed */
	val = m41t11_readbyte(M41T11_SEC);
	if (val & 0x80)
		m41t11_writebyte(M41T11_SEC, val & 0x7f);

	return 0;
}

static int m41t11_attach(struct i2c_adapter *adap)
{
	return i2c_probe(adap, &addr_data, m41t11_probe);
}

static int m41t11_detach(struct i2c_client *c)
{
	int rc;

	if ((rc = i2c_detach_client(c)) < 0)
		return rc;
	kfree(c);
	return 0;
}

/*
 *****************************************************************************
 *
 *	Driver Interface
 *
 *****************************************************************************
 */

static struct i2c_driver m41t11_i2cdrv = {
	.driver = {
		.name	= M41T11_DRV_NAME,
	},
	.id		= I2C_DRIVERID_STM41T00,
	.attach_adapter	= m41t11_attach,
	.detach_client	= m41t11_detach,
};

static struct file_operations m41t11_fops = {
	.owner		= THIS_MODULE,
	.read		= m41t11_read,
	.write		= m41t11_write,
	.ioctl		= m41t11_ioctl,
};

static struct miscdevice m41t11_miscdrv = {
	.minor		= RTC_MINOR,
	.name		= "rtc",
	.fops		= &m41t11_fops,
};

static int __init m41t11_init(void)
{
	int rc;

	if ((rc = i2c_add_driver(&m41t11_i2cdrv)) < 0)
		return rc;
	if ((rc = misc_register(&m41t11_miscdrv)) < 0) {
		i2c_del_driver(&m41t11_i2cdrv);
		return rc;
	}

	printk("M41T11: RTC I2C driver registered\n");
	return 0;
}

static void __exit m41t11_exit(void)
{
	misc_deregister(&m41t11_miscdrv);
	i2c_del_driver(&m41t11_i2cdrv);
	return;
}

module_init(m41t11_init);
module_exit(m41t11_exit);

MODULE_AUTHOR("Greg Ungerer <gerg@snapgear.com>");
MODULE_DESCRIPTION("ST Microelectronics M41T11 RTC I2C Client Driver");
MODULE_LICENSE("GPL");
