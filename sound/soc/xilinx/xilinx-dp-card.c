/*
 * Xilinx DisplayPort SoC Sound Card support
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

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

static int xilinx_dp_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_hw_constraint_step(runtime, 0,
				   SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 256);
	return 0;
}

static const struct snd_soc_ops xilinx_dp_ops = {
	.startup	= xilinx_dp_startup,
};

static struct snd_soc_dai_link xilinx_dp_dai_links[] = {
	{
		.name		= "xilinx-dp0",
		.codec_dai_name	= "xilinx-dp-snd-codec-dai",
		.ops		= &xilinx_dp_ops,
	},
	{
		.name		= "xilinx-dp1",
		.codec_dai_name	= "xilinx-dp-snd-codec-dai",
		.ops		= &xilinx_dp_ops,
	},

};

static struct snd_soc_card xilinx_dp_card = {
	.name		= "DisplayPort monitor",
	.owner		= THIS_MODULE,
	.dai_link	= xilinx_dp_dai_links,
	.num_links	= 2,
};

static int xilinx_dp_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &xilinx_dp_card;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *codec, *pcm;
	int ret;

	card->dev = &pdev->dev;

	codec = of_parse_phandle(node, "xlnx,dp-snd-codec", 0);
	if (!codec)
		return -ENODEV;

	pcm = of_parse_phandle(node, "xlnx,dp-snd-pcm", 0);
	if (!pcm)
		return -ENODEV;
	xilinx_dp_dai_links[0].platform_of_node = pcm;
	xilinx_dp_dai_links[0].cpu_of_node = codec;
	xilinx_dp_dai_links[0].codec_of_node = codec;

	pcm = of_parse_phandle(node, "xlnx,dp-snd-pcm", 1);
	if (!pcm)
		return -ENODEV;
	xilinx_dp_dai_links[1].platform_of_node = pcm;
	xilinx_dp_dai_links[1].cpu_of_node = codec;
	xilinx_dp_dai_links[1].codec_of_node = codec;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "Xilinx DisplayPort Sound Card probed\n");

	return 0;
}

static int xilinx_dp_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id xilinx_dp_of_match[] = {
	{ .compatible = "xlnx,dp-snd-card", },
	{},
};
MODULE_DEVICE_TABLE(of, xilinx_dp_of_match);

static struct platform_driver xilinx_dp_aud_driver = {
	.driver	= {
		.name		= "xilinx-dp-snd-card",
		.of_match_table	= xilinx_dp_of_match,
		.pm		= &snd_soc_pm_ops,
	},
	.probe	= xilinx_dp_probe,
	.remove	= xilinx_dp_remove,
};
module_platform_driver(xilinx_dp_aud_driver);

MODULE_DESCRIPTION("Xilinx DisplayPort Sound Card module");
MODULE_LICENSE("GPL v2");
