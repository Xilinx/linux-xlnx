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
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <sound/soc.h>

#define ZYNQMP_DISP_AUD_CH_STATUS		0x8
#define ZYNQMP_DISP_AUD_CH_STATUS_44K		0x0
#define ZYNQMP_DISP_AUD_CH_STATUS_48K		0x2000000
#define ZYNQMP_DISP_AUD_SMPL_RATE_44K		44100
#define ZYNQMP_DISP_AUD_SMPL_RATE_48K		48000
#define ZYNQMP_DISP_AUD_SMPL_RATE_TO_CLK	512

/**
 * struct xilinx_dp_codec - DisplayPort codec
 * @aud_clk: audio clock
 * @aud_base: base address for DP audio
 * @dev: DP audio device
 */
struct xilinx_dp_codec {
	struct clk *aud_clk;
	struct regmap *aud_base;
	struct device *dev;
};

struct xilinx_dp_codec_fmt {
	unsigned long rate;
	unsigned int snd_rate;
};

static int dp_codec_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *socdai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct xilinx_dp_codec *codec =
		snd_soc_dai_get_drvdata(asoc_rtd_to_cpu(rtd, 0));
	unsigned long rate;
	int ret;

	u32 sample_rate = params_rate(params);

	if (sample_rate != ZYNQMP_DISP_AUD_SMPL_RATE_48K &&
	    sample_rate != ZYNQMP_DISP_AUD_SMPL_RATE_44K)
		return -EINVAL;

	clk_disable_unprepare(codec->aud_clk);
	ret = clk_set_rate(codec->aud_clk,
			   sample_rate * ZYNQMP_DISP_AUD_SMPL_RATE_TO_CLK);
	if (ret) {
		dev_err(codec->dev, "can't set aud_clk to %u err:%d\n",
			sample_rate * ZYNQMP_DISP_AUD_SMPL_RATE_TO_CLK, ret);
		return ret;
	}
	clk_prepare_enable(codec->aud_clk);
	rate = clk_get_rate(codec->aud_clk);

	/* Ignore some offset +- 10 */
	if (abs(sample_rate * ZYNQMP_DISP_AUD_SMPL_RATE_TO_CLK - rate) > 10) {
		dev_err(codec->dev, "aud_clk offset is higher: %ld\n",
			sample_rate * ZYNQMP_DISP_AUD_SMPL_RATE_TO_CLK - rate);
		ret = -EINVAL;
		goto err_clk;
	}

	if (sample_rate == ZYNQMP_DISP_AUD_SMPL_RATE_48K)
		regmap_write(codec->aud_base, ZYNQMP_DISP_AUD_CH_STATUS,
			     ZYNQMP_DISP_AUD_CH_STATUS_48K);
	else
		regmap_write(codec->aud_base, ZYNQMP_DISP_AUD_CH_STATUS,
			     ZYNQMP_DISP_AUD_CH_STATUS_44K);

	return 0;

err_clk:
	clk_disable_unprepare(codec->aud_clk);
	return ret;
}

static const struct snd_soc_dai_ops dp_codec_dai_ops = {
	.hw_params	= dp_codec_hw_params,
};

static struct snd_soc_dai_driver xilinx_dp_codec_dai = {
	.name		= "xilinx-dp-snd-codec-dai",
	.ops		= &dp_codec_dai_ops,
	.playback	= {
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE,
	},
};

static const struct xilinx_dp_codec_fmt rates[] = {
	{
		.rate	= ZYNQMP_DISP_AUD_SMPL_RATE_48K *
			  ZYNQMP_DISP_AUD_SMPL_RATE_TO_CLK,
		.snd_rate = SNDRV_PCM_RATE_48000
	},
	{
		.rate	= ZYNQMP_DISP_AUD_SMPL_RATE_44K *
			  ZYNQMP_DISP_AUD_SMPL_RATE_TO_CLK,
		.snd_rate = SNDRV_PCM_RATE_44100
	}
};

static const struct snd_soc_component_driver xilinx_dp_component_driver = {
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
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

	codec->aud_base =
		syscon_regmap_lookup_by_phandle(pdev->dev.parent->of_node,
						"xlnx,dpaud-reg");
	if (IS_ERR(codec->aud_base))
		return PTR_ERR(codec->aud_base);

	codec->dev = &pdev->dev;

	for (i = 0; i < ARRAY_SIZE(rates); i++) {
		clk_disable_unprepare(codec->aud_clk);
		ret = clk_set_rate(codec->aud_clk, rates[i].rate);
		clk_prepare_enable(codec->aud_clk);
		if (ret)
			continue;

		rate = clk_get_rate(codec->aud_clk);
		/* Ignore some offset +- 10 */
		if (abs(rates[i].rate - rate) < 10)
			break;
		ret = -EINVAL;
	}

	if (ret) {
		dev_err(&pdev->dev, "Failed to get required clock freq\n");
		goto error_clk;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &xilinx_dp_component_driver,
					      &xilinx_dp_codec_dai, 1);
	if (ret)
		goto error_clk;

	platform_set_drvdata(pdev, codec);
	dev_set_drvdata(&pdev->dev, codec);

	dev_info(&pdev->dev, "Xilinx DisplayPort Sound Codec probed\n");

	return 0;

error_clk:
	clk_disable_unprepare(codec->aud_clk);
	return ret;
}

static int xilinx_dp_codec_dev_remove(struct platform_device *pdev)
{
	struct xilinx_dp_codec *codec = platform_get_drvdata(pdev);

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
