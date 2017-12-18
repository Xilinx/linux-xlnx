/*
 * Driver for the Epson RTC module RX-6110 SA
 *
 * Copyright(C) 2015 Pengutronix, Steffen Trumtrar <kernel@pengutronix.de>
 * Copyright(C) SEIKO EPSON CORPORATION 2013. All rights reserved.
 *
 * This driver software is distributed as is, without any warranty of any kind,
 * either express or implied as further specified in the GNU Public License.
 * This software may be used and distributed according to the terms of the GNU
 * Public License, version 2 as published by the Free Software Foundation.
 * See the file COPYING in the main directory of this archive for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/bcd.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/rtc.h>
#include <linux/spi/spi.h>

/* RX-6110 Register definitions */
#define RX6110_REG_SEC		0x10
#define RX6110_REG_MIN		0x11
#define RX6110_REG_HOUR		0x12
#define RX6110_REG_WDAY		0x13
#define RX6110_REG_MDAY		0x14
#define RX6110_REG_MONTH	0x15
#define RX6110_REG_YEAR		0x16
#define RX6110_REG_RES1		0x17
#define RX6110_REG_ALMIN	0x18
#define RX6110_REG_ALHOUR	0x19
#define RX6110_REG_ALWDAY	0x1A
#define RX6110_REG_TCOUNT0	0x1B
#define RX6110_REG_TCOUNT1	0x1C
#define RX6110_REG_EXT		0x1D
#define RX6110_REG_FLAG		0x1E
#define RX6110_REG_CTRL		0x1F
#define RX6110_REG_USER0	0x20
#define RX6110_REG_USER1	0x21
#define RX6110_REG_USER2	0x22
#define RX6110_REG_USER3	0x23
#define RX6110_REG_USER4	0x24
#define RX6110_REG_USER5	0x25
#define RX6110_REG_USER6	0x26
#define RX6110_REG_USER7	0x27
#define RX6110_REG_USER8	0x28
#define RX6110_REG_USER9	0x29
#define RX6110_REG_USERA	0x2A
#define RX6110_REG_USERB	0x2B
#define RX6110_REG_USERC	0x2C
#define RX6110_REG_USERD	0x2D
#define RX6110_REG_USERE	0x2E
#define RX6110_REG_USERF	0x2F
#define RX6110_REG_RES2		0x30
#define RX6110_REG_RES3		0x31
#define RX6110_REG_IRQ		0x32

#define RX6110_BIT_ALARM_EN		BIT(7)

/* Extension Register (1Dh) bit positions */
#define RX6110_BIT_EXT_TSEL0		BIT(0)
#define RX6110_BIT_EXT_TSEL1		BIT(1)
#define RX6110_BIT_EXT_TSEL2		BIT(2)
#define RX6110_BIT_EXT_WADA		BIT(3)
#define RX6110_BIT_EXT_TE		BIT(4)
#define RX6110_BIT_EXT_USEL		BIT(5)
#define RX6110_BIT_EXT_FSEL0		BIT(6)
#define RX6110_BIT_EXT_FSEL1		BIT(7)

/* Flag Register (1Eh) bit positions */
#define RX6110_BIT_FLAG_VLF		BIT(1)
#define RX6110_BIT_FLAG_AF		BIT(3)
#define RX6110_BIT_FLAG_TF		BIT(4)
#define RX6110_BIT_FLAG_UF		BIT(5)

/* Control Register (1Fh) bit positions */
#define RX6110_BIT_CTRL_TBKE		BIT(0)
#define RX6110_BIT_CTRL_TBKON		BIT(1)
#define RX6110_BIT_CTRL_TSTP		BIT(2)
#define RX6110_BIT_CTRL_AIE		BIT(3)
#define RX6110_BIT_CTRL_TIE		BIT(4)
#define RX6110_BIT_CTRL_UIE		BIT(5)
#define RX6110_BIT_CTRL_STOP		BIT(6)
#define RX6110_BIT_CTRL_TEST		BIT(7)

enum {
	RTC_SEC = 0,
	RTC_MIN,
	RTC_HOUR,
	RTC_WDAY,
	RTC_MDAY,
	RTC_MONTH,
	RTC_YEAR,
	RTC_NR_TIME
};

#define RX6110_DRIVER_NAME		"rx6110"

struct rx6110_data {
	struct rtc_device *rtc;
	struct regmap *regmap;
};

/**
 * rx6110_rtc_tm_to_data - convert rtc_time to native time encoding
 *
 * @tm: holds date and time
 * @data: holds the encoding in rx6110 native form
 */
static int rx6110_rtc_tm_to_data(struct rtc_time *tm, u8 *data)
{
	pr_debug("%s: date %ds %dm %dh %dmd %dm %dy\n", __func__,
		 tm->tm_sec, tm->tm_min, tm->tm_hour,
		 tm->tm_mday, tm->tm_mon, tm->tm_year);

	/*
	 * The year in the RTC is a value between 0 and 99.
	 * Assume that this represents the current century
	 * and disregard all other values.
	 */
	if (tm->tm_year < 100 || tm->tm_year >= 200)
		return -EINVAL;

	data[RTC_SEC] = bin2bcd(tm->tm_sec);
	data[RTC_MIN] = bin2bcd(tm->tm_min);
	data[RTC_HOUR] = bin2bcd(tm->tm_hour);
	data[RTC_WDAY] = BIT(bin2bcd(tm->tm_wday));
	data[RTC_MDAY] = bin2bcd(tm->tm_mday);
	data[RTC_MONTH] = bin2bcd(tm->tm_mon + 1);
	data[RTC_YEAR] = bin2bcd(tm->tm_year % 100);

	return 0;
}

/**
 * rx6110_data_to_rtc_tm - convert native time encoding to rtc_time
 *
 * @data: holds the encoding in rx6110 native form
 * @tm: holds date and time
 */
static int rx6110_data_to_rtc_tm(u8 *data, struct rtc_time *tm)
{
	tm->tm_sec = bcd2bin(data[RTC_SEC] & 0x7f);
	tm->tm_min = bcd2bin(data[RTC_MIN] & 0x7f);
	/* only 24-hour clock */
	tm->tm_hour = bcd2bin(data[RTC_HOUR] & 0x3f);
	tm->tm_wday = ffs(data[RTC_WDAY] & 0x7f);
	tm->tm_mday = bcd2bin(data[RTC_MDAY] & 0x3f);
	tm->tm_mon = bcd2bin(data[RTC_MONTH] & 0x1f) - 1;
	tm->tm_year = bcd2bin(data[RTC_YEAR]) + 100;

	pr_debug("%s: date %ds %dm %dh %dmd %dm %dy\n", __func__,
		 tm->tm_sec, tm->tm_min, tm->tm_hour,
		 tm->tm_mday, tm->tm_mon, tm->tm_year);

	/*
	 * The year in the RTC is a value between 0 and 99.
	 * Assume that this represents the current century
	 * and disregard all other values.
	 */
	if (tm->tm_year < 100 || tm->tm_year >= 200)
		return -EINVAL;

	return 0;
}

/**
 * rx6110_set_time - set the current time in the rx6110 registers
 *
 * @dev: the rtc device in use
 * @tm: holds date and time
 *
 * BUG: The HW assumes every year that is a multiple of 4 to be a leap
 * year. Next time this is wrong is 2100, which will not be a leap year
 *
 * Note: If STOP is not set/cleared, the clock will start when the seconds
 *       register is written
 *
 */
static int rx6110_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rx6110_data *rx6110 = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	ret = rx6110_rtc_tm_to_data(tm, data);
	if (ret < 0)
		return ret;

	/* set STOP bit before changing clock/calendar */
	ret = regmap_update_bits(rx6110->regmap, RX6110_REG_CTRL,
				 RX6110_BIT_CTRL_STOP, RX6110_BIT_CTRL_STOP);
	if (ret)
		return ret;

	ret = regmap_bulk_write(rx6110->regmap, RX6110_REG_SEC, data,
				RTC_NR_TIME);
	if (ret)
		return ret;

	/* The time in the RTC is valid. Be sure to have VLF cleared. */
	ret = regmap_update_bits(rx6110->regmap, RX6110_REG_FLAG,
				 RX6110_BIT_FLAG_VLF, 0);
	if (ret)
		return ret;

	/* clear STOP bit after changing clock/calendar */
	ret = regmap_update_bits(rx6110->regmap, RX6110_REG_CTRL,
				 RX6110_BIT_CTRL_STOP, 0);

	return ret;
}

/**
 * rx6110_get_time - get the current time from the rx6110 registers
 * @dev: the rtc device in use
 * @tm: holds date and time
 */
static int rx6110_get_time(struct device *dev, struct rtc_time *tm)
{
	struct rx6110_data *rx6110 = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int flags;
	int ret;

	ret = regmap_read(rx6110->regmap, RX6110_REG_FLAG, &flags);
	if (ret)
		return -EINVAL;

	/* check for VLF Flag (set at power-on) */
	if ((flags & RX6110_BIT_FLAG_VLF)) {
		dev_warn(dev, "Voltage low, data is invalid.\n");
		return -EINVAL;
	}

	/* read registers to date */
	ret = regmap_bulk_read(rx6110->regmap, RX6110_REG_SEC, data,
			       RTC_NR_TIME);
	if (ret)
		return ret;

	ret = rx6110_data_to_rtc_tm(data, tm);
	if (ret)
		return ret;

	dev_dbg(dev, "%s: date %ds %dm %dh %dmd %dm %dy\n", __func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year);

	return rtc_valid_tm(tm);
}

static const struct reg_sequence rx6110_default_regs[] = {
	{ RX6110_REG_RES1,   0xB8 },
	{ RX6110_REG_RES2,   0x00 },
	{ RX6110_REG_RES3,   0x10 },
	{ RX6110_REG_IRQ,    0x00 },
	{ RX6110_REG_ALMIN,  0x00 },
	{ RX6110_REG_ALHOUR, 0x00 },
	{ RX6110_REG_ALWDAY, 0x00 },
};

/**
 * rx6110_init - initialize the rx6110 registers
 *
 * @rx6110: pointer to the rx6110 struct in use
 *
 */
static int rx6110_init(struct rx6110_data *rx6110)
{
	struct rtc_device *rtc = rx6110->rtc;
	int flags;
	int ret;

	ret = regmap_update_bits(rx6110->regmap, RX6110_REG_EXT,
				 RX6110_BIT_EXT_TE, 0);
	if (ret)
		return ret;

	ret = regmap_register_patch(rx6110->regmap, rx6110_default_regs,
				    ARRAY_SIZE(rx6110_default_regs));
	if (ret)
		return ret;

	ret = regmap_read(rx6110->regmap, RX6110_REG_FLAG, &flags);
	if (ret)
		return ret;

	/* check for VLF Flag (set at power-on) */
	if ((flags & RX6110_BIT_FLAG_VLF))
		dev_warn(&rtc->dev, "Voltage low, data loss detected.\n");

	/* check for Alarm Flag */
	if (flags & RX6110_BIT_FLAG_AF)
		dev_warn(&rtc->dev, "An alarm may have been missed.\n");

	/* check for Periodic Timer Flag */
	if (flags & RX6110_BIT_FLAG_TF)
		dev_warn(&rtc->dev, "Periodic timer was detected\n");

	/* check for Update Timer Flag */
	if (flags & RX6110_BIT_FLAG_UF)
		dev_warn(&rtc->dev, "Update timer was detected\n");

	/* clear all flags BUT VLF */
	ret = regmap_update_bits(rx6110->regmap, RX6110_REG_FLAG,
				 RX6110_BIT_FLAG_AF |
				 RX6110_BIT_FLAG_UF |
				 RX6110_BIT_FLAG_TF,
				 0);

	return ret;
}

static const struct rtc_class_ops rx6110_rtc_ops = {
	.read_time = rx6110_get_time,
	.set_time = rx6110_set_time,
};

static struct regmap_config regmap_spi_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RX6110_REG_IRQ,
	.read_flag_mask = 0x80,
};

/**
 * rx6110_probe - initialize rtc driver
 * @spi: pointer to spi device
 */
static int rx6110_probe(struct spi_device *spi)
{
	struct rx6110_data *rx6110;
	int err;

	if ((spi->bits_per_word && spi->bits_per_word != 8) ||
	    (spi->max_speed_hz > 2000000) ||
	    (spi->mode != (SPI_CS_HIGH | SPI_CPOL | SPI_CPHA))) {
		dev_warn(&spi->dev, "SPI settings: bits_per_word: %d, max_speed_hz: %d, mode: %xh\n",
			 spi->bits_per_word, spi->max_speed_hz, spi->mode);
		dev_warn(&spi->dev, "driving device in an unsupported mode");
	}

	rx6110 = devm_kzalloc(&spi->dev, sizeof(*rx6110), GFP_KERNEL);
	if (!rx6110)
		return -ENOMEM;

	rx6110->regmap = devm_regmap_init_spi(spi, &regmap_spi_config);
	if (IS_ERR(rx6110->regmap)) {
		dev_err(&spi->dev, "regmap init failed for rtc rx6110\n");
		return PTR_ERR(rx6110->regmap);
	}

	spi_set_drvdata(spi, rx6110);

	rx6110->rtc = devm_rtc_device_register(&spi->dev,
					       RX6110_DRIVER_NAME,
					       &rx6110_rtc_ops, THIS_MODULE);

	if (IS_ERR(rx6110->rtc))
		return PTR_ERR(rx6110->rtc);

	err = rx6110_init(rx6110);
	if (err)
		return err;

	rx6110->rtc->max_user_freq = 1;

	return 0;
}

static int rx6110_remove(struct spi_device *spi)
{
	return 0;
}

static const struct spi_device_id rx6110_id[] = {
	{ "rx6110", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, rx6110_id);

static struct spi_driver rx6110_driver = {
	.driver = {
		.name = RX6110_DRIVER_NAME,
	},
	.probe		= rx6110_probe,
	.remove		= rx6110_remove,
	.id_table	= rx6110_id,
};

module_spi_driver(rx6110_driver);

MODULE_AUTHOR("Val Krutov <val.krutov@erd.epson.com>");
MODULE_DESCRIPTION("RX-6110 SA RTC driver");
MODULE_LICENSE("GPL");
