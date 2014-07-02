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

static struct snd_soc_dai_driver xilinx_dp_codec_dai = {
	.name		= "xilinx-dp-snd-codec-dai",
	.playback	= {
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_44100,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
	},
};

static const struct snd_soc_codec_driver xilinx_dp_codec_codec_driver = {
};

static int xilinx_dp_codec_probe(struct platform_device *pdev)
{
	struct xilinx_dp_codec *codec;
	int rate, ret;

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

	rate = clk_get_rate(codec->aud_clk) / 512;
	if (rate == 44100) {
		xilinx_dp_codec_dai.playback.rates = SNDRV_PCM_RATE_44100;
	} else if (rate == 48000) {
		xilinx_dp_codec_dai.playback.rates = SNDRV_PCM_RATE_48000;
	} else {
		ret = -EINVAL;
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

static const struct of_device_id xilinx_dp_codec_of_match[] = {
	{ .compatible = "xlnx,dp-snd-codec", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xilinx_dp_codec_of_match);

static struct platform_driver xilinx_dp_codec_driver = {
	.driver	= {
		.name		= "xilinx-dp-snd-codec",
		.of_match_table	= xilinx_dp_codec_of_match,
	},
	.probe	= xilinx_dp_codec_probe,
	.remove	= xilinx_dp_codec_dev_remove,
};
module_platform_driver(xilinx_dp_codec_driver);

MODULE_DESCRIPTION("Xilinx DisplayPort Sound Codec module");
MODULE_LICENSE("GPL v2");
