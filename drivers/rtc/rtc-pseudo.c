/*
 * Pseudo RTC
 *
 * Copyright 2019 GROOVE X, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

static struct platform_device *_pdev = NULL;

static int enable = 0;
module_param(enable, int, S_IRUGO);
MODULE_PARM_DESC(enable, "1 for enable, 0 for disable.");

static long timestamp = 1552102779;
module_param(timestamp, long, S_IRUGO);
MODULE_PARM_DESC(timestamp, "Initial timestamp which the RTC provides.");

struct pseudo_rtc_dev {
	struct rtc_device *rtc;
	time64_t last_time;
	unsigned long last_jiffies;
};

static int pseudo_rtc_get_time(struct device *dev, struct rtc_time *tm)
{
	struct pseudo_rtc_dev *prtc = dev_get_drvdata(dev);
	unsigned long now_jiffies = jiffies, diff;

	if (prtc->last_jiffies <= now_jiffies) {
		diff = now_jiffies - prtc->last_jiffies;
	} else {
		diff = now_jiffies + (UINT_MAX - prtc->last_jiffies);
	}

	rtc_time64_to_tm(prtc->last_time + (jiffies_to_msecs(diff) / 1000), tm);

	if (rtc_valid_tm(tm)) {
		dev_err(dev, "invalid time!\n");
		return -EINVAL;
	}

	return 0;
}

static int pseudo_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pseudo_rtc_dev *prtc = dev_get_drvdata(dev);

	dev_info(dev, "got new time: %04d/%02d/%02d %02d:%02d:%02d\n",
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday + 1, tm->tm_hour, tm->tm_min, tm->tm_sec);
	prtc->last_time = rtc_tm_to_time64(tm);
	prtc->last_jiffies = jiffies;
	return 0;
}

static const struct rtc_class_ops pseudo_rtc_ops = {
	.read_time = pseudo_rtc_get_time,
	.set_time = pseudo_rtc_set_time,
};

static int pseudo_rtc_probe(struct platform_device *dev)
{
	struct pseudo_rtc_dev *prtc_dev;

	prtc_dev = devm_kzalloc(&dev->dev, sizeof(*prtc_dev), GFP_KERNEL);
	if (!prtc_dev)
		return -ENOMEM;
	platform_set_drvdata(dev, prtc_dev);

	prtc_dev->last_time = (time64_t)timestamp;
	prtc_dev->last_jiffies = jiffies;

	prtc_dev->rtc = devm_rtc_device_register(
		&dev->dev, "rtc-pseudo", &pseudo_rtc_ops, THIS_MODULE);
	if (IS_ERR(prtc_dev->rtc))
		return PTR_ERR(prtc_dev->rtc);

	pr_info("rtc-pseudo: succesfully probed the pseudo RTC driver\n");
	pr_info("rtc-pseudo: initial timestamp: %lld\n", prtc_dev->last_time);
	return 0;
}

static int pseudo_rtc_remove(struct platform_device *dev)
{
	return 0;
}

static struct platform_driver pseudo_rtc_driver = {
	.remove = pseudo_rtc_remove,
	.driver = {
		.name = "rtc-pseudo",
	},
};

static int __init pseudo_rtc_init(void)
{
	if (!enable) {
		return -EINVAL;
	}

	_pdev = platform_device_alloc("rtc-pseudo", 0);
	if (!_pdev) {
		return -ENOMEM;
	}

	if (platform_device_add(_pdev)) {
		platform_device_put(_pdev);
		return -ENOMEM;
	}

	pr_info("rtc-pseudo: successfully initialized a device\n");

	return platform_driver_probe(&pseudo_rtc_driver, pseudo_rtc_probe);
}

static void __exit pseudo_rtc_exit(void)
{
	platform_device_del(_pdev);
	platform_driver_unregister(&pseudo_rtc_driver);
}

module_init(pseudo_rtc_init);
module_exit(pseudo_rtc_exit);

MODULE_AUTHOR("GROOVE X, Inc.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Pseudo RTC");
MODULE_ALIAS("platform:rtc-pseudo");
