/*
 * Xilinx DisplayPort Sound Codec support
 *
 *  Copyright (C) 2015 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyunk@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

/**
 * struct xilinx_dp_codec - DisplayPort codec
 * @aud_clk: audio clock
 */
struct xilinx_dp_codec {
	struct clk *aud_clk;
};

struct xilinx_dp_codec_fmt {
	unsigned long rate;
	unsigned int snd_rate;
};

static struct snd_soc_dai_driver xilinx_dp_codec_dai = {
	.name		= "xilinx-dp-snd-codec-dai",
	.playback	= {
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_44100,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
	},
};

static const struct xilinx_dp_codec_fmt rates[] = {
	{
		.rate	= 48000 * 512,
		.snd_rate = SNDRV_PCM_RATE_48000
	},
	{
		.rate	= 44100 * 512,
		.snd_rate = SNDRV_PCM_RATE_44100
	}
};

static const struct snd_soc_codec_driver xilinx_dp_codec_codec_driver = {
};

static int xilinx_dp_codec_probe(struct platform_device *pdev)
{
	struct xilinx_dp_codec *codec;
	unsigned int i;
	unsigned long rate;
	int ret;

	codec = devm_kzalloc(&pdev->dev, sizeof(*codec), GFP_KERNEL);
	if (!codec)
		return -ENOMEM;

	codec->aud_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(codec->aud_clk))
		return PTR_ERR(codec->aud_clk);

	ret = clk_prepare_enable(codec->aud_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable the aud_clk\n");
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(rates); i++) {
		clk_disable_unprepare(codec->aud_clk);
		ret = clk_set_rate(codec->aud_clk, rates[i].rate);
		clk_prepare_enable(codec->aud_clk);
		if (ret)
			continue;

		rate = clk_get_rate(codec->aud_clk);
		/* Ignore some offset +- 10 */
		if (abs(rates[i].rate - rate) < 10) {
			xilinx_dp_codec_dai.playback.rates = rates[i].snd_rate;
			break;
		}
		ret = -EINVAL;
	}

	if (ret) {
		dev_err(&pdev->dev, "Failed to get required clock freq\n");
		goto error_clk;
	}

	ret = snd_soc_register_codec(&pdev->dev, &xilinx_dp_codec_codec_driver,
				     &xilinx_dp_codec_dai, 1);
	if (ret)
		goto error_clk;

	platform_set_drvdata(pdev, codec);

	dev_info(&pdev->dev, "Xilinx DisplayPort Sound Codec probed\n");

	return 0;

error_clk:
	clk_disable_unprepare(codec->aud_clk);
	return ret;
}

static int xilinx_dp_codec_dev_remove(struct platform_device *pdev)
{
	struct xilinx_dp_codec *codec = platform_get_drvdata(pdev);

	snd_soc_unregister_codec(&pdev->dev);
	clk_disable_unprepare(codec->aud_clk);

	return 0;
}

static int __maybe_unused xilinx_dp_codec_pm_suspend(struct device *dev)
{
	struct xilinx_dp_codec *codec = dev_get_drvdata(dev);

	clk_disable_unprepare(codec->aud_clk);

	return 0;
}

static int __maybe_unused xilinx_dp_codec_pm_resume(struct device *dev)
{
	struct xilinx_dp_codec *codec = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(codec->aud_clk);
	if (ret)
		dev_err(dev, "failed to enable the aud_clk\n");

	return ret;
}

static const struct dev_pm_ops xilinx_dp_codec_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xilinx_dp_codec_pm_suspend,
				xilinx_dp_codec_pm_resume)
};

static const struct of_device_id xilinx_dp_codec_of_match[] = {
	{ .compatible = "xlnx,dp-snd-codec", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xilinx_dp_codec_of_match);

static struct platform_driver xilinx_dp_codec_driver = {
	.driver	= {
		.name		= "xilinx-dp-snd-codec",
		.of_match_table	= xilinx_dp_codec_of_match,
		.pm		= &xilinx_dp_codec_pm_ops,
	},
	.probe	= xilinx_dp_codec_probe,
	.remove	= xilinx_dp_codec_dev_remove,
};
module_platform_driver(xilinx_dp_codec_driver);

MODULE_DESCRIPTION("Xilinx DisplayPort Sound Codec module");
MODULE_LICENSE("GPL v2");
