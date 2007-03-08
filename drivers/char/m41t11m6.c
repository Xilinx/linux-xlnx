/*****************************************************************************/

/*
 *	m41t11m6.c -- driver for M41T11M6 Real Time Clock.
 *
 * 	(C) Copyright 2004-2005, Greg Ungerer <gerg@snapgear.com>
 */

/*****************************************************************************/

#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/module.h> 
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/rtc.h>
#include <asm/hardware.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>

/*****************************************************************************/

/*
 *	Size of RTC region. 64 bytes total, the first 10 are the RTC.
 */
#define	M41T11M6_MSIZE	0x3f

/*****************************************************************************/

/*
 *	M41T11M6 defines.
 */
#define	M41T11M6_ADDR	0xd0		/* IIC address of M41T11M6 device */
#define	M41T11M6_RD	1		/* Read bit command */
#define	M41T11M6_WR	0		/* Write bit command */

#define	M41T11M6_SEC	0x00		/* Address of second register */
#define	M41T11M6_MIN	0x01		/* Address of minute register */
#define	M41T11M6_HOUR	0x02		/* Address of hour register */
#define	M41T11M6_WDAY	0x03		/* Address of day of week register */
#define	M41T11M6_MDAY	0x04		/* Address of day of month register */
#define	M41T11M6_MON	0x05		/* Address of month register */
#define	M41T11M6_YEAR	0x06		/* Address of year register */
#define	M41T11M6_FTOUT	0x07		/* Address of control register */

/*****************************************************************************/
#ifdef CONFIG_MACH_IPD
/*****************************************************************************/

#include <asm/io.h>

/*
 * On the EP9312/IPD,  the clock in on EGIO[1] and the data is on EGPIO[3]
 */
#define	SDA	0x8
#define	SCL	0x2
#define IN	0
#define OUT	1

static void gpio_line_config(int line, int dir)
{
	unsigned long flags;

	save_flags(flags); cli();
	if (dir == OUT)
		outl(inl(GPIO_PADDR) | line, GPIO_PADDR); /* data is output */
	else
		outl(inl(GPIO_PADDR) & ~line, GPIO_PADDR); /* data is input */
	restore_flags(flags);
}

static void gpio_line_set(int line, int val)
{
	unsigned long flags;

	save_flags(flags); cli();
	if (val)
		outl(inl(GPIO_PADR) | line, GPIO_PADR);
	else
		outl(inl(GPIO_PADR) & ~line, GPIO_PADR);
	restore_flags(flags);
}

static inline void gpio_line_get(int line, int *val)
{
	*val = (inl(GPIO_PADR) & line) ? 1 : 0;
}

/*****************************************************************************/
#elif defined(CONFIG_MACH_CM41xx) || defined(CONFIG_MACH_CM4008)
/*****************************************************************************/

#include <asm/io.h>

/*
 *	GPIO lines 6, 7 and 8 are used for the RTC.
 */
#define	SDAT	6		/* SDA transmit */
#define	SDAR	7		/* SDA receiver */
#define	SCL	8		/* SCL - clock */
#define	SDA	SDAR

#define IN	0
#define OUT	1

#define	SDAT_B	(1 << SDAT)
#define	SDAR_B	(1 << SDAR)
#define	SCL_B	(1 << SCL)

static volatile unsigned int *gpdatap = (volatile unsigned int *) (IO_ADDRESS(KS8695_IO_BASE) + KS8695_GPIO_DATA);
static volatile unsigned int *gpmodep = (volatile unsigned int *) (IO_ADDRESS(KS8695_IO_BASE) + KS8695_GPIO_MODE);

static inline void gpio_line_config(int line, int dir)
{
	if (line == SDA) {
		if (dir == IN)
			*gpdatap |= SDAT_B;
	}
	if (line == SCL) {
		/* We do normal initialization for all GPIO bits here */
		*gpmodep |= SCL_B | SDAT_B;
		*gpmodep &= ~SDAR_B;
	}
}

static inline void gpio_line_set(int line, int val)
{
	if (line == SCL) {
		if (val)
			*gpdatap |= SCL_B;
		else
			*gpdatap &= ~SCL_B;
	} else {
		if (val)
			*gpdatap |= SDAT_B;
		else
			*gpdatap &= ~SDAT_B;
	}
}

static inline void gpio_line_get(int line, int *val)
{
	*val = (*gpdatap & SDAR_B) ? 1 : 0;
}

/*****************************************************************************/
#else
/*****************************************************************************/

/*
 *	The IIC lines to the M41T11M6 are GPIO lines from the IXP4xx.
 *	The clock line is on GPIO12, and the data line on GPIO11.
 */
#define	SDA	11
#define	SCL	12
#define	IN	IXP4XX_GPIO_IN
#define	OUT	IXP4XX_GPIO_OUT

/*****************************************************************************/
#endif
/*****************************************************************************/

static void gpio_line_config_slow(u8 line, u32 style)
{
	gpio_line_config(line, style);
	udelay(10);
}

static void gpio_line_set_slow(u8 line, int value)
{
	gpio_line_set(line, value);
	udelay(10);
}

static void gpio_line_get_slow(u8 line, int *value)
{
	gpio_line_get(line, value);
	udelay(10);
}

/*****************************************************************************/

void m41t11m6_readack(void)
{
	unsigned int ack;

	gpio_line_config_slow(SDA, IN);
	gpio_line_set_slow(SCL, 1);
	gpio_line_get_slow(SDA, &ack);
	gpio_line_set_slow(SCL, 0);
	gpio_line_config_slow(SDA, OUT);
}

void m41t11m6_writeack(void)
{
	gpio_line_set_slow(SDA, 0);
	gpio_line_set_slow(SCL, 1);
	gpio_line_set_slow(SCL, 0);
}

void m41t11m6_sendbits(unsigned int val)
{
	int i;

	gpio_line_set_slow(SCL, 0);
	for (i = 7; (i >= 0); i--) {
		gpio_line_set_slow(SDA, ((val >> i) & 0x1));
		gpio_line_set_slow(SCL, 1);
		gpio_line_set_slow(SCL, 0);
	}
}

unsigned int m41t11m6_recvbits(void)
{
	unsigned int val, bit;
	int i;

	gpio_line_set_slow(SCL, 0);
	gpio_line_config_slow(SDA, IN);
	for (i = 0, val = 0; (i < 8); i++) {
		gpio_line_set_slow(SCL, 1);
		gpio_line_get_slow(SDA, &bit);
		val = (val << 1) | bit;
		gpio_line_set_slow(SCL, 0);
	}

	gpio_line_config_slow(SDA, OUT);
	return val;
}

/*****************************************************************************/
DECLARE_MUTEX(m41t11m6_sem);

/* 
 *	The read byte sequenece is actually a write sequence followed
 *	by the read sequenece. The first write is to set the register
 *	address, and is a complete cycle itself.
 */
unsigned int m41t11m6_readbyte(unsigned int addr)
{
	unsigned int val;

	down(&m41t11m6_sem);
#if 0
	printk("m41t11m6_readbyte(addr=%x)\n", addr);
#endif

	/* Send start signal */
	gpio_line_set_slow(SCL, 1);
	gpio_line_set_slow(SDA, 1);
	gpio_line_set_slow(SDA, 0);

	/* Send M41T11M6 device address byte, and write command for addr */
	m41t11m6_sendbits(M41T11M6_ADDR | M41T11M6_WR);
	m41t11m6_readack();
	m41t11m6_sendbits(addr);
	m41t11m6_readack();

	/* Now send sequence to read bytes, starting with start signal */
	gpio_line_set_slow(SDA, 1);
	gpio_line_set_slow(SCL, 1);
	gpio_line_set_slow(SDA, 1);
	gpio_line_set_slow(SDA, 0);

	/* Send M41T11M6 device address byte, and read command for addr */
	m41t11m6_sendbits(M41T11M6_ADDR | M41T11M6_RD);
	m41t11m6_writeack();
	val = m41t11m6_recvbits();

	/* Send stop signal */
	gpio_line_set_slow(SDA, 0);
	gpio_line_set_slow(SCL, 1);
	gpio_line_set_slow(SDA, 1);

	up(&m41t11m6_sem);

	return val;
}

void m41t11m6_writebyte(unsigned int addr, unsigned int val)
{
	down(&m41t11m6_sem);
#if 0
	printk("m41t11m6_writebyte(addr=%x)\n", addr);
#endif

	/* Send start signal */
	gpio_line_set_slow(SCL, 1);
	gpio_line_set_slow(SDA, 1);
	gpio_line_set_slow(SDA, 0);

	/* Send M41T11M6 device address byte, and write command */
	m41t11m6_sendbits(M41T11M6_ADDR | M41T11M6_WR);
	m41t11m6_readack();

	/* Send word address and data to write */
	m41t11m6_sendbits(addr);
	m41t11m6_readack();
	m41t11m6_sendbits(val);
	m41t11m6_readack();

	/* Send stop signal */
	gpio_line_set_slow(SDA, 0);
	gpio_line_set_slow(SCL, 1);
	gpio_line_set_slow(SDA, 1);

	up(&m41t11m6_sem);
}

/*****************************************************************************/

void m41t11m6_setup(void)
{
	down(&m41t11m6_sem);

	/* Initially set the IIC lines to be outputs from the IXP4xx */
	gpio_line_config(SCL, OUT);
	gpio_line_config(SDA, OUT);

	/* Set IIC bus into idle mode */
	gpio_line_set(SCL, 1);
	gpio_line_set(SDA, 1);

	up(&m41t11m6_sem);
}

/*****************************************************************************/

int bcd2bin(int val)
{
	return ((((val & 0xf0) >> 4) * 10) + (val & 0xf));
}

int bin2bcd(int val)
{
	val &= 0xff;
	return (((val / 10) << 4) + (val % 10));
}

/*****************************************************************************/

static ssize_t m41t11m6_read(struct file *fp, char __user *buf, size_t count, loff_t *ptr)
{
	int total;

#if 0
	printk("m41t11m6_read(buf=%x,count=%d)\n", (int) buf, count);
#endif

	if (fp->f_pos >= M41T11M6_MSIZE)
		return 0;

	if (count > (M41T11M6_MSIZE - fp->f_pos))
		count = M41T11M6_MSIZE - fp->f_pos;

	for (total = 0; (total < count); total++)
		put_user(m41t11m6_readbyte(fp->f_pos + total), buf++);

	fp->f_pos += total;
	return total;
}

/*****************************************************************************/

static ssize_t m41t11m6_write(struct file *fp, const char __user *buf, size_t count, loff_t *ptr)
{
	int total;
	char val;

#if 0
	printk("m41t11m6_write(buf=%x,count=%d)\n", (int) buf, count);
#endif

	if (fp->f_pos >= M41T11M6_MSIZE)
		return 0;

	if (count > (M41T11M6_MSIZE - fp->f_pos))
		count = M41T11M6_MSIZE - fp->f_pos;

	for (total = 0; (total < count); total++, buf++) {
		get_user(val,buf);
		m41t11m6_writebyte((fp->f_pos + total), val);
	}

	fp->f_pos += total;
	return total;
}

/*****************************************************************************/

/*
 *	Do some consistency checks on the time. On first power up the
 *	RTC may contain completely bogus junk, this will clean it up.
 *	Just for good measure we do this when writing to the RTC as well.
 */

static void m41t11m6_validatetime(struct rtc_time *rtime)
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

/*****************************************************************************/

static void m41t11m6_readtime(struct rtc_time *rtime)
{
	memset(rtime, 0, sizeof(*rtime));
	rtime->tm_year = bcd2bin(m41t11m6_readbyte(M41T11M6_YEAR)) +
		((m41t11m6_readbyte(M41T11M6_HOUR) & 0x40) ? 100 : 0);
	rtime->tm_mon = bcd2bin(m41t11m6_readbyte(M41T11M6_MON & 0x1f)) - 1;
	rtime->tm_mday = bcd2bin(m41t11m6_readbyte(M41T11M6_MDAY & 0x3f));
	rtime->tm_wday = bcd2bin(m41t11m6_readbyte(M41T11M6_WDAY) & 0x7) - 1;
	rtime->tm_hour = bcd2bin(m41t11m6_readbyte(M41T11M6_HOUR) & 0x3f);
	rtime->tm_min = bcd2bin(m41t11m6_readbyte(M41T11M6_MIN) & 0x7f);
	rtime->tm_sec = bcd2bin(m41t11m6_readbyte(M41T11M6_SEC) & 0x7f);
}

/*****************************************************************************/

static void m41t11m6_settime(struct rtc_time *rtime)
{
	m41t11m6_writebyte(M41T11M6_YEAR, bin2bcd(rtime->tm_year));
	m41t11m6_writebyte(M41T11M6_MON, bin2bcd(rtime->tm_mon+1));
	m41t11m6_writebyte(M41T11M6_MDAY, bin2bcd(rtime->tm_mday));
	m41t11m6_writebyte(M41T11M6_WDAY, bin2bcd(rtime->tm_wday+1));
	m41t11m6_writebyte(M41T11M6_HOUR, bin2bcd(rtime->tm_hour) |
		((rtime->tm_year > 99) ? 0xc0 : 0x80));
	m41t11m6_writebyte(M41T11M6_MIN, bin2bcd(rtime->tm_min));
	m41t11m6_writebyte(M41T11M6_SEC, bin2bcd(rtime->tm_sec));
	m41t11m6_writebyte(M41T11M6_FTOUT, 0x90);
}

/*****************************************************************************/

static int m41t11m6_ioctl(struct inode *inode, struct file *file, unsigned cmd, unsigned long arg)
{
	struct rtc_time rtime;

#if 0
	printk("m41t11m6_ioctl(cmd=%x,arg=%x)\n", cmd, arg);
#endif

	switch (cmd) {

	case RTC_RD_TIME:
		m41t11m6_readtime(&rtime);
		m41t11m6_validatetime(&rtime);
		if (copy_to_user((void __user *) arg, &rtime, sizeof(rtime)))
			return -EFAULT;
		break;

	case RTC_SET_TIME:
		if (!capable(CAP_SYS_TIME))
			return -EACCES;
		m41t11m6_validatetime(&rtime);
		if (copy_from_user(&rtime, (void __user *) arg, sizeof(rtime)))
			return -EFAULT;
		m41t11m6_settime(&rtime);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*****************************************************************************/

/*
 *	Exported file operations structure for driver...
 */
static struct file_operations m41t11m6_fops =
{
	.owner = THIS_MODULE, 
	.read =  m41t11m6_read,
	.write = m41t11m6_write,
	.ioctl = m41t11m6_ioctl,
};

static struct miscdevice m41t11m6_dev =
{
	RTC_MINOR,
	"rtc",
	&m41t11m6_fops
};

/*****************************************************************************/

static int __init m41t11m6_init(void)
{
	m41t11m6_setup();
	misc_register(&m41t11m6_dev);
	printk ("M41T11M6: Real Time Clock driver\n");
	return 0;
}

static void __exit m41t11m6_exit(void)
{
	misc_deregister(&m41t11m6_dev);
}

/*****************************************************************************/

module_init(m41t11m6_init);
module_exit(m41t11m6_exit);

MODULE_AUTHOR("Greg Ungerer <gerg@snapgear.com>");
MODULE_LICENSE("GPL");

/*****************************************************************************/
