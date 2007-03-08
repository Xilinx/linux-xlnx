/****************************************************************************/

/*
 * drivers/rtc/rtc-ds1302.c -- DS1302 RTC code
 *
 *  Copyright (C) 2002  David McCullough <davidm@snapgear.com>
 *  Copyright (C) 2003  Paul Mundt <lethal@linux-sh.org>
 *  Copyright (C) 2006  Greg Ungerer <gerg@snapgear.com>
 *
 * Support for the DS1302 on some Snapgear SH based boards.
 */

/****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/snapgear.h>

/****************************************************************************/
/*
 *	we need to implement a DS1302 driver here that can operate in
 *	conjunction with the builtin rtc driver which is already quite friendly
 */
/*****************************************************************************/

#define	RTC_CMD_READ	0x81		/* Read command */
#define	RTC_CMD_WRITE	0x80		/* Write command */

#define	RTC_ADDR_YEAR	0x06		/* Address of year register */
#define	RTC_ADDR_DAY	0x05		/* Address of day of week register */
#define	RTC_ADDR_MON	0x04		/* Address of month register */
#define	RTC_ADDR_DATE	0x03		/* Address of day of month register */
#define	RTC_ADDR_HOUR	0x02		/* Address of hour register */
#define	RTC_ADDR_MIN	0x01		/* Address of minute register */
#define	RTC_ADDR_SEC	0x00		/* Address of second register */

#define	RTC_RESET	0x1000
#define	RTC_IODATA	0x0800
#define	RTC_SCLK	0x0400

#define set_dirp(x)
#define get_dirp(x) 0
#define set_dp(x)	SECUREEDGE_WRITE_IOPORT(x, 0x1c00)
#define get_dp(x)	SECUREEDGE_READ_IOPORT()

static void ds1302_sendbits(unsigned int val)
{
	int	i;

	for (i = 8; (i); i--, val >>= 1) {
		set_dp((get_dp() & ~RTC_IODATA) | ((val & 0x1) ? RTC_IODATA : 0));
		set_dp(get_dp() | RTC_SCLK);	// clock high
		set_dp(get_dp() & ~RTC_SCLK);	// clock low
	}
}

static unsigned int ds1302_recvbits(void)
{
	unsigned int	val;
	int		i;

	for (i = 0, val = 0; (i < 8); i++) {
		val |= (((get_dp() & RTC_IODATA) ? 1 : 0) << i);
		set_dp(get_dp() | RTC_SCLK);	// clock high
		set_dp(get_dp() & ~RTC_SCLK);	// clock low
	}
	return(val);
}

static unsigned int ds1302_readbyte(unsigned int addr)
{
	unsigned int	val;
	unsigned long	flags;

	local_irq_save(flags);
	set_dirp(get_dirp() | RTC_RESET | RTC_IODATA | RTC_SCLK);
	set_dp(get_dp() & ~(RTC_RESET | RTC_IODATA | RTC_SCLK));

	set_dp(get_dp() | RTC_RESET);
	ds1302_sendbits(((addr & 0x3f) << 1) | RTC_CMD_READ);
	set_dirp(get_dirp() & ~RTC_IODATA);
	val = ds1302_recvbits();
	set_dp(get_dp() & ~RTC_RESET);
	local_irq_restore(flags);

	return(val);
}

static void ds1302_writebyte(unsigned int addr, unsigned int val)
{
	unsigned long	flags;

	local_irq_save(flags);
	set_dirp(get_dirp() | RTC_RESET | RTC_IODATA | RTC_SCLK);
	set_dp(get_dp() & ~(RTC_RESET | RTC_IODATA | RTC_SCLK));
	set_dp(get_dp() | RTC_RESET);
	ds1302_sendbits(((addr & 0x3f) << 1) | RTC_CMD_WRITE);
	ds1302_sendbits(val);
	set_dp(get_dp() & ~RTC_RESET);
	local_irq_restore(flags);
}

static void ds1302_reset(void)
{
	unsigned long	flags;
	/* Hardware dependant reset/init */
	local_irq_save(flags);
	set_dirp(get_dirp() | RTC_RESET | RTC_IODATA | RTC_SCLK);
	set_dp(get_dp() & ~(RTC_RESET | RTC_IODATA | RTC_SCLK));
	local_irq_restore(flags);
}

/****************************************************************************/

static inline int bcd2bin(int val)
{
	return BCD2BIN(val);
}

static int ds1302_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	tm->tm_sec = bcd2bin(ds1302_readbyte(RTC_ADDR_SEC) & 0x7f);
	tm->tm_min = bcd2bin(ds1302_readbyte(RTC_ADDR_MIN) & 0x7f);
	tm->tm_hour = bcd2bin(ds1302_readbyte(RTC_ADDR_HOUR) & 0x3f);
	tm->tm_wday = bcd2bin(ds1302_readbyte(RTC_ADDR_DAY) & 0x07) - 1;
	tm->tm_mday = bcd2bin(ds1302_readbyte(RTC_ADDR_DATE) & 0x3f);
	tm->tm_mon = bcd2bin(ds1302_readbyte(RTC_ADDR_MON) & 0x1f) - 1;
	tm->tm_year = bcd2bin(ds1302_readbyte(RTC_ADDR_YEAR)) + 100;

	return 0;
}

static int ds1302_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	/* STOP RTC */
	ds1302_writebyte(RTC_ADDR_SEC, 0x80);

	ds1302_writebyte(RTC_ADDR_MIN, BIN2BCD(tm->tm_min));
	ds1302_writebyte(RTC_ADDR_HOUR, BIN2BCD(tm->tm_hour));
	ds1302_writebyte(RTC_ADDR_DAY, BIN2BCD(tm->tm_wday + 1));
	ds1302_writebyte(RTC_ADDR_DATE, BIN2BCD(tm->tm_mday));
	ds1302_writebyte(RTC_ADDR_MON, BIN2BCD(tm->tm_mon + 1));
	ds1302_writebyte(RTC_ADDR_YEAR, BIN2BCD(tm->tm_year - 100));

	/* RESTARTS RTC */
	ds1302_writebyte(RTC_ADDR_SEC, BIN2BCD(tm->tm_sec));

	return 0;
}

/****************************************************************************/

static struct rtc_class_ops ds1302_rtc_ops = {
	.read_time	= ds1302_rtc_read_time,
	.set_time	= ds1302_rtc_set_time,
};

/****************************************************************************/

static int __devinit ds1302_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rdev;
	unsigned char *test = "snapgear";
	int i;

	ds1302_reset();

	for (i = 0; test[i]; i++)
		ds1302_writebyte(32 + i, test[i]);

	for (i = 0; test[i]; i++) {
		if (ds1302_readbyte(32 + i) != test[i])
			return -ENOENT;
	}

	rdev = rtc_device_register("ds1302", &pdev->dev, &ds1302_rtc_ops, THIS_MODULE);

	printk("SnapGear RTC: using ds1302 rtc.\n");

	platform_set_drvdata(pdev, rdev);
	return 0;
}

/****************************************************************************/

static int __devexit ds1302_rtc_remove(struct platform_device *pdev)
{
	struct rtc_device *rdev = platform_get_drvdata(pdev);

	if (rdev)
		rtc_device_unregister(rdev);
	return 0;
}

/****************************************************************************/

static struct platform_driver ds1302_rtc_platform_driver = {
	.driver		= {
		.name	= "ds1302",
		.owner	= THIS_MODULE,
	},
	.probe		= ds1302_rtc_probe,
	.remove		= __devexit_p(ds1302_rtc_remove),
};

/****************************************************************************/

static int __init ds1302_rtc_init(void)
{
	return platform_driver_register(&ds1302_rtc_platform_driver);
}

static void __exit ds1302_rtc_exit(void)
{
	platform_driver_unregister(&ds1302_rtc_platform_driver);
}

/****************************************************************************/

module_init(ds1302_rtc_init);
module_exit(ds1302_rtc_exit);

/****************************************************************************/

MODULE_DESCRIPTION("DS1302 on SnapGear SH hardware platforms");
MODULE_AUTHOR("David McCullough <davidm@snapgear.com>, Paul Mundt <lethal@linux-sh.org>, Greg Ungerer <gerg@snapgear.com>");
MODULE_LICENSE("GPL");

/****************************************************************************/
