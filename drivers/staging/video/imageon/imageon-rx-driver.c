/*
 * Driver for the IMAGEON-FMC board
 *
 * Copyright 2012-2013 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * based on cobalt-driver.c
 *
 * Licensed under the GPL-2.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of_i2c.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/v4l2-dv-timings.h>

#include <media/adv7604.h>
#include <media/v4l2-event.h>

#include "imageon-rx-driver.h"

MODULE_LICENSE("Dual BSD/GPL");

/* EDID for ADV7611 HDMI receiver */
#define EDID_SIZE 256
static u8 edid_data[EDID_SIZE] = {
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
	0x06, 0xD4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x16, 0x01, 0x03, 0x81, 0x46, 0x27, 0x78,
	0x0A, 0x32, 0x30, 0xA1, 0x54, 0x52, 0x9E, 0x26,
	0x0A, 0x49, 0x4B, 0xA3, 0x08, 0x00, 0x81, 0xC0,
	0x81, 0x00, 0x81, 0x0F, 0x81, 0x40, 0x81, 0x80,
	0x95, 0x00, 0xB3, 0x00, 0x01, 0x01, 0x02, 0x3A,
	0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C,
	0x45, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x1E,
	0xA9, 0x1A, 0x00, 0xA0, 0x50, 0x00, 0x16, 0x30,
	0x30, 0x20, 0x37, 0x00, 0xC4, 0x8E, 0x21, 0x00,
	0x00, 0x1A, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x46,
	0x4D, 0x43, 0x2D, 0x49, 0x4D, 0x41, 0x47, 0x45,
	0x4F, 0x4E, 0x0A, 0x20, 0x00, 0x00, 0x00, 0xFD,
	0x00, 0x38, 0x4B, 0x20, 0x44, 0x11, 0x00, 0x0A,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x54,
	0x02, 0x03, 0x1F, 0x71, 0x4B, 0x90, 0x03, 0x04,
	0x05, 0x12, 0x13, 0x14, 0x1F, 0x20, 0x07, 0x16,
	0x26, 0x15, 0x07, 0x50, 0x09, 0x07, 0x01, 0x67,
	0x03, 0x0C, 0x00, 0x10, 0x00, 0x00, 0x1E, 0x01,
	0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20, 0x6E,
	0x28, 0x55, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00,
	0x1E, 0x01, 0x1D, 0x80, 0x18, 0x71, 0x1C, 0x16,
	0x20, 0x58, 0x2C, 0x25, 0x00, 0xC4, 0x8E, 0x21,
	0x00, 0x00, 0x9E, 0x8C, 0x0A, 0xD0, 0x8A, 0x20,
	0xE0, 0x2D, 0x10, 0x10, 0x3E, 0x96, 0x00, 0xC4,
	0x8E, 0x21, 0x00, 0x00, 0x18, 0x01, 0x1D, 0x80,
	0x3E, 0x73, 0x38, 0x2D, 0x40, 0x7E, 0x2C, 0x45,
	0x80, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x1E, 0x1A,
	0x36, 0x80, 0xA0, 0x70, 0x38, 0x1F, 0x40, 0x30,
	0x20, 0x25, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00,
	0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};

static void imageon_rx_notify(struct v4l2_subdev *sd, unsigned int notification,
	void *arg)
{
	/* pr_err("%s\n",__func__); */

	struct imageon_rx *imageon_rx = to_imageon_rx(sd->v4l2_dev);
	long hotplug = (long)arg;

	switch (notification) {
	case ADV7604_HOTPLUG:
		gpio_set_value_cansleep(imageon_rx->hotplug_gpio, hotplug);
		break;
	default:
		break;
	}
}

static int imageon_rx_subdevs_init(struct imageon_rx *imageon_rx)
{
	static struct adv7604_platform_data adv7611_pdata = {
		.disable_pwrdnb = 1,
		.op_ch_sel = ADV7604_OP_CH_SEL_RGB,
		.blank_data = 1,
		.op_656_range = 1,
		.rgb_out = 0,
		.alt_data_sat = 1,
		.op_format_sel = ADV7604_OP_FORMAT_SEL_SDR_ITU656_16,
		.int1_config = ADV7604_INT1_CONFIG_OPEN_DRAIN,
		.connector_hdmi = 1,
		.insert_av_codes = 1,
		.i2c_cec = 0x40,
		.i2c_infoframe = 0x3e,
		.i2c_afe = 0x26,
		.i2c_repeater = 0x32,
		.i2c_edid = 0x36,
		.i2c_hdmi = 0x34,
		.i2c_cp = 0x22,
	};
	static struct i2c_board_info adv7611_info = {
		.type = "adv7611",
		.addr = 0x4c,
		.platform_data = &adv7611_pdata,
	};
	struct v4l2_subdev_edid edid = {
		.pad = 0,
		.start_block = 0,
		.blocks = 2,
		.edid = imageon_rx->edid_data,
	};

	struct imageon_rx_stream *s = &imageon_rx->stream;

	s->sd_adv7611 = v4l2_i2c_new_subdev_board(&imageon_rx->v4l2_dev,
		s->i2c_adap, &adv7611_info, NULL);
	if (!s->sd_adv7611)
		return -ENODEV;

	v4l2_subdev_call(s->sd_adv7611, pad, set_edid, &edid);
	return v4l2_subdev_call(s->sd_adv7611, video,
		s_routing, ADV7604_MODE_HDMI, 0, 0);
}

static int imageon_rx_load_edid(struct platform_device *pdev,
	struct imageon_rx *imageon_rx)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, "adv7611_edid.bin", &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to load firmware: %d\n", ret);
		/*Using default EDID setting in case of failure */
		dev_err(&pdev->dev, "Loading default EDID setting\n");
		/*Copy EDID data */
		memcpy(imageon_rx->edid_data, edid_data, EDID_SIZE);
	} else {

		if (fw->size > 256) {
			dev_err(&pdev->dev, "EDID firmware data too large.\n");
			release_firmware(fw);
			return -EINVAL;
		}

		memcpy(imageon_rx->edid_data, fw->data, fw->size);

		release_firmware(fw);
	}

	return 0;
}

static int imageon_rx_probe(struct platform_device *pdev)
{
	struct device_node *of_node;
	struct i2c_adapter *adap;
	struct imageon_rx *imageon_rx;
	int ret;

	of_node = of_parse_phandle(pdev->dev.of_node, "slave_adapter", 0);
	if (!of_node)
		return -ENXIO;

	adap = of_find_i2c_adapter_by_node(of_node);
	of_node_put(of_node);
	if (!adap)
		return -EPROBE_DEFER;

	imageon_rx =
	devm_kzalloc(&pdev->dev, sizeof(struct imageon_rx), GFP_KERNEL);
	if (imageon_rx == NULL) {
		dev_err(&pdev->dev, "Failed to allocate device\n");
		ret = -ENOMEM;
		goto err_i2c_put_adapter;
	}

	imageon_rx->hotplug_gpio =
	of_get_named_gpio(pdev->dev.of_node, "hpd-gpio", 0);
	if (!gpio_is_valid(imageon_rx->hotplug_gpio))
		return imageon_rx->hotplug_gpio;


	ret = devm_gpio_request_one(&pdev->dev,
		imageon_rx->hotplug_gpio, GPIOF_OUT_INIT_LOW, "HPD");
	if (ret < 0)
		return ret;
	imageon_rx->stream.i2c_adap = adap;


	ret = imageon_rx_load_edid(pdev, imageon_rx);
	if (ret)
		goto err_i2c_put_adapter;

	ret = v4l2_device_register(&pdev->dev, &imageon_rx->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register card: %d\n", ret);
		goto err_device_unregister;
	}
	snprintf(imageon_rx->v4l2_dev.name, sizeof(imageon_rx->v4l2_dev.name),
		"imageon_rx");

	imageon_rx->v4l2_dev.notify = imageon_rx_notify;

	ret = imageon_rx_subdevs_init(imageon_rx);
	if (ret)
		goto err_device_unregister;

	ret = v4l2_device_register_subdev_nodes(&imageon_rx->v4l2_dev);
	if (ret)
		goto err_device_unregister;

	video_set_drvdata(&imageon_rx->stream.vdev, imageon_rx);

	return 0;

err_device_unregister:
	v4l2_device_unregister(&imageon_rx->v4l2_dev);
err_i2c_put_adapter:
	i2c_put_adapter(adap);
	return ret;
}

static int imageon_rx_remove(struct platform_device *pdev)
{
	struct imageon_rx *imageon_rx = platform_get_drvdata(pdev);

	v4l2_device_unregister(&imageon_rx->v4l2_dev);
	i2c_put_adapter(imageon_rx->stream.i2c_adap);

	return 0;
}

static const struct of_device_id imageon_rx_of_match[] = {
	{ .compatible = "xlnx,imageon-rx", },
	{},
};
MODULE_DEVICE_TABLE(of, imageon_rx_of_match);

static struct platform_driver imageon_rx_driver = {
	.driver = {
		.name = "imageon-rx",
		.owner = THIS_MODULE,
		.of_match_table = imageon_rx_of_match,
	},
	.probe = imageon_rx_probe,
	.remove = imageon_rx_remove,
};
module_platform_driver(imageon_rx_driver);
