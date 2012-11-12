/*
 * Remote processor messaging transport - sample server driver
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/io.h>

#define MSG		("hello world!")
#define MSG_LIMIT	100

#define MSG_LAT		("12345678901234567890123456789012345678901234567890"\
			"12345678901234567890123456789012345678901234567890"\
			"12345678901234567890123456789012345678901234567890"\
			"12345678901234567890123456789012345678901234567890"\
			"12345678901234567890123456789012345678901234567890"\
			"12345678901234567890123456789012345678901234567890"\
			"12345678901234567890123456789012345678901234567890"\
			"12345678901234567890123456789012345678901234567890"\
			"12345678901234567890123456789012345678901234567890"\
			"123456789012345678901234567890123456789012345")
#define MSG_LAT_LIMIT	100000

u64 start; /* variable for storing jiffies */

/* TTC runs on 133 MHz - (1 / 133000000 = 8 ns) */
#define TTC_HZ	8

/* ttc ioremap pointer */
u8 *ttc_base;

/* Enable/disable latency measuring */
static int latency;

static void rpmsg_sample_cb(struct rpmsg_channel *rpdev, void *data, int len,
						void *priv, u32 src)
{
	int err;
	static int rx_count;

	if (latency) {
		static u32 min = 0x10000000;
		static u32 max;
		static u32 average;

		u32 value = __raw_readl(ttc_base + 0x1c); /* Read value */
		__raw_writel(0x11, ttc_base + 0x10); /* Stop TTC */

		if (value < min)
			min = value;
		if (value > max)
			max = value;

		average += value;
		/* count messages */
		++rx_count;

		if (rx_count >= MSG_LAT_LIMIT) {
			u64 end = get_jiffies_64();
			u32 time = end - start;
			u32 timeps = ((1000000 / MSG_LAT_LIMIT) * time) / HZ;

			printk(KERN_INFO "actual value %d ns, min %d ns, max %d"
				" ns, average %d ns\n", value * TTC_HZ,
						min * TTC_HZ, max * TTC_HZ,
						(average/rx_count) * TTC_HZ);
			printk(KERN_INFO "Start/end jiffies %llx/%llx, "
				"messages %d. Time: %d s, "
				"Messages per second %d\n", end, start,
				rx_count, time/HZ,
				1000000 / timeps);

			dev_info(&rpdev->dev, "goodbye!\n");
			return;
		}

		__raw_writel(0x10, ttc_base + 0x10); /* Start TTC */
		/* reply */
		err = rpmsg_sendto(rpdev, MSG_LAT, strlen(MSG_LAT), src);
	} else {
		dev_info(&rpdev->dev, "incoming msg %d (src: 0x%x)\n",
							++rx_count, src);
		print_hex_dump(KERN_DEBUG, __func__, DUMP_PREFIX_NONE, 16, 1,
			data, len,  true);

		/* samples should not live forever */
		if (rx_count >= MSG_LIMIT) {
			dev_info(&rpdev->dev, "goodbye!\n");
			return;
		}

		err = rpmsg_sendto(rpdev, MSG, strlen(MSG), src); /* reply */
	}

	if (err)
		pr_err("rpmsg_send failed: %d\n", err);
}

static int rpmsg_sample_probe(struct rpmsg_channel *rpdev)
{
	int err;

	if (latency) {
		dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!, len %d\n",
				rpdev->src, rpdev->dst, strlen(MSG_LAT));

		ttc_base = ioremap(0xf8002000, PAGE_SIZE); /* TTC base addr */
		if (!ttc_base) {
			pr_err("TTC Ioremap failed\n");
			return -1;
		}
		start = get_jiffies_64();
		__raw_writel(0x10, ttc_base + 0x10); /* Start TTC */

		err = rpmsg_sendto(rpdev, MSG_LAT, strlen(MSG_LAT), 50);
	} else {
		dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!, len %d\n",
				rpdev->src, rpdev->dst, strlen(MSG));

		err = rpmsg_sendto(rpdev, MSG, strlen(MSG), 50);
	}

	if (err) {
		pr_err("rpmsg_send failed: %d\n", err);
		return err;
	}

	return 0;
}

static void rpmsg_sample_remove(struct rpmsg_channel *rpdev)
{
	dev_info(&rpdev->dev, "rpmsg sample driver is removed\n");
}

static struct rpmsg_device_id rpmsg_driver_sample_id_table[] = {
	{ .name	= "rpmsg-server-sample" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_driver_sample_id_table);

static struct rpmsg_driver rpmsg_sample_server = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_driver_sample_id_table,
	.probe		= rpmsg_sample_probe,
	.callback	= rpmsg_sample_cb,
	.remove		= rpmsg_sample_remove,
};

static int __init init(void)
{
	return register_rpmsg_driver(&rpmsg_sample_server);
}

static void __exit fini(void)
{
	unregister_rpmsg_driver(&rpmsg_sample_server);
}
module_init(init);
module_exit(fini);

module_param(latency, int, 0);
MODULE_PARM_DESC(latency, "Enable latency measuring code.");

MODULE_DESCRIPTION("Virtio remote processor messaging sample driver");
MODULE_LICENSE("GPL v2");
