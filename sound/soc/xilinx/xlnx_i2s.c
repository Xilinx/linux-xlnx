// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ASoC I2S audio support
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Author: Praveen Vuppala <praveenv@xilinx.com>
 * Author: Maruthi Srinivas Bayyavarapu <maruthis@xilinx.com>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define DRV_NAME "xlnx_i2s"

#define I2S_CORE_CTRL_OFFSET		0x08
#define I2S_I2STIM_OFFSET		0x20
#define I2S_CH0_OFFSET			0x30
#define I2S_I2STIM_VALID_MASK		GENMASK(7, 0)

struct xlnx_i2s_dev_data {
	void __iomem *base;
	struct clk *axi_clk;
	struct clk *axis_clk;
	struct clk *aud_mclk;
};

static int xlnx_i2s_set_sclkout_div(struct snd_soc_dai *cpu_dai,
				    int div_id, int div)
{
	struct xlnx_i2s_dev_data *dev_data = snd_soc_dai_get_drvdata(cpu_dai);

	if (!div || (div & ~I2S_I2STIM_VALID_MASK))
		return -EINVAL;

	writel(div, dev_data->base + I2S_I2STIM_OFFSET);

	return 0;
}

static int xlnx_i2s_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *i2s_dai)
{
	u32 reg_off, chan_id;
	struct xlnx_i2s_dev_data *dev_data = snd_soc_dai_get_drvdata(i2s_dai);

	chan_id = params_channels(params) / 2;

	while (chan_id > 0) {
		reg_off = I2S_CH0_OFFSET + ((chan_id - 1) * 4);
		writel(chan_id, dev_data->base + reg_off);
		chan_id--;
	}

	return 0;
}

static int xlnx_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			    struct snd_soc_dai *i2s_dai)
{
	struct xlnx_i2s_dev_data *dev_data = snd_soc_dai_get_drvdata(i2s_dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		writel(1, dev_data->base + I2S_CORE_CTRL_OFFSET);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		writel(0, dev_data->base + I2S_CORE_CTRL_OFFSET);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops xlnx_i2s_dai_ops = {
	.trigger = xlnx_i2s_trigger,
	.set_clkdiv = xlnx_i2s_set_sclkout_div,
	.hw_params = xlnx_i2s_hw_params
};

static const struct snd_soc_component_driver xlnx_i2s_component = {
	.name = DRV_NAME,
};

static const struct of_device_id xlnx_i2s_of_match[] = {
	{ .compatible = "xlnx,i2s-transmitter-1.0", },
	{ .compatible = "xlnx,i2s-receiver-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, xlnx_i2s_of_match);

static int xlnx_i2s_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct snd_soc_dai_driver *dai_drv;
	struct xlnx_i2s_dev_data *dev_data;
	int ret;
	u32 ch, format, data_width;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;

	dai_drv = devm_kzalloc(&pdev->dev, sizeof(*dai_drv), GFP_KERNEL);
	if (!dai_drv)
		return -ENOMEM;

	dev_data = devm_kzalloc(&pdev->dev, sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data)
		return -ENOMEM;

	dev_data->axi_clk = devm_clk_get(&pdev->dev, "s_axi_ctrl_aclk");
	if (IS_ERR(dev_data->axi_clk)) {
		ret = PTR_ERR(dev_data->axi_clk);
		dev_err(&pdev->dev, "failed to get s_axi_ctrl_aclk(%d)\n", ret);
		return ret;
	}

	ret = of_property_read_u32(node, "xlnx,num-channels", &ch);
	if (ret < 0) {
		dev_err(dev, "cannot get supported channels\n");
		return ret;
	}
	ch = ch * 2;

	ret = of_property_read_u32(node, "xlnx,dwidth", &data_width);
	if (ret < 0) {
		dev_err(dev, "cannot get data width\n");
		return ret;
	}
	switch (data_width) {
	case 16:
		format = SNDRV_PCM_FMTBIT_S16_LE;
		break;
	case 24:
		format = SNDRV_PCM_FMTBIT_S24_LE;
		break;
	default:
		return -EINVAL;
	}

	if (of_device_is_compatible(node, "xlnx,i2s-transmitter-1.0")) {
		dai_drv->name = "xlnx_i2s_playback";
		dai_drv->playback.stream_name = "Playback";
		dai_drv->playback.formats = format;
		dai_drv->playback.channels_min = ch;
		dai_drv->playback.channels_max = ch;
		dai_drv->playback.rates	= SNDRV_PCM_RATE_8000_192000;
		dai_drv->ops = &xlnx_i2s_dai_ops;

		dev_data->axis_clk = devm_clk_get(&pdev->dev,
						  "s_axis_aud_aclk");
		if (IS_ERR(dev_data->axis_clk)) {
			ret = PTR_ERR(dev_data->axis_clk);
			dev_err(&pdev->dev,
				"failed to get s_axis_aud_aclk(%d)\n", ret);
			return ret;
		}
	} else if (of_device_is_compatible(node, "xlnx,i2s-receiver-1.0")) {
		dai_drv->name = "xlnx_i2s_capture";
		dai_drv->capture.stream_name = "Capture";
		dai_drv->capture.formats = format;
		dai_drv->capture.channels_min = ch;
		dai_drv->capture.channels_max = ch;
		dai_drv->capture.rates = SNDRV_PCM_RATE_8000_192000;
		dai_drv->ops = &xlnx_i2s_dai_ops;

		dev_data->axis_clk = devm_clk_get(&pdev->dev,
						  "m_axis_aud_aclk");
		if (IS_ERR(dev_data->axis_clk)) {
			ret = PTR_ERR(dev_data->axis_clk);
			dev_err(&pdev->dev,
				"failed to get m_axis_aud_aclk(%d)\n", ret);
			return ret;
		}
	} else {
		return -ENODEV;
	}

	dev_data->aud_mclk = devm_clk_get(&pdev->dev, "aud_mclk");
	if (IS_ERR(dev_data->aud_mclk)) {
		ret = PTR_ERR(dev_data->aud_mclk);
		dev_err(&pdev->dev, "failed to get aud_mclk(%d)\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(dev_data->axi_clk);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to enable s_axi_ctrl_aclk(%d)\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(dev_data->axis_clk);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to enable axis_aud_aclk(%d)\n", ret);
		goto err_axis_clk;
	}

	ret = clk_prepare_enable(dev_data->aud_mclk);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to enable aud_mclk(%d)\n", ret);
		goto err_aud_mclk;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev_data->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev_data->base)) {
		ret = PTR_ERR(dev_data->base);
		goto clk_err;
	}

	dev_set_drvdata(&pdev->dev, dev_data);

	ret = devm_snd_soc_register_component(&pdev->dev, &xlnx_i2s_component,
					      dai_drv, 1);
	if (ret) {
		dev_err(&pdev->dev, "i2s component registration failed\n");
		goto clk_err;
	}

	dev_info(&pdev->dev, "%s DAI registered\n", dai_drv->name);

	return 0;
clk_err:
	clk_disable_unprepare(dev_data->aud_mclk);
err_aud_mclk:
	clk_disable_unprepare(dev_data->axis_clk);
err_axis_clk:
	clk_disable_unprepare(dev_data->axi_clk);

	return ret;
}

static int xlnx_i2s_remove(struct platform_device *pdev)
{
	struct xlnx_i2s_dev_data *dev_data = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(dev_data->aud_mclk);
	clk_disable_unprepare(dev_data->axis_clk);
	clk_disable_unprepare(dev_data->axi_clk);

	return 0;
}

static struct platform_driver xlnx_i2s_aud_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = xlnx_i2s_of_match,
	},
	.probe = xlnx_i2s_probe,
	.remove = xlnx_i2s_remove,
};

module_platform_driver(xlnx_i2s_aud_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Praveen Vuppala  <praveenv@xilinx.com>");
MODULE_AUTHOR("Maruthi Srinivas Bayyavarapu <maruthis@xilinx.com>");
