// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ASoC sound card support
 *
 * Copyright (C) 2018 Xilinx, Inc.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "xlnx_snd_common.h"

#define I2S_CLOCK_RATIO 384

enum {
	I2S_AUDIO = 0,
	HDMI_AUDIO,
	SDI_AUDIO,
	XLNX_MAX_IFACE,
};

static const char *dev_compat[][XLNX_MAX_IFACE] = {
	[XLNX_PLAYBACK] = {
		"xlnx,i2s-transmitter-1.0",
		"xlnx,v-hdmi-tx-ss-3.1",
		"xlnx,v-uhdsdi-audio-1.0",
	},
	[XLNX_CAPTURE] = {
		"xlnx,i2s-receiver-1.0",
		"xlnx,v-hdmi-rx-ss-3.1",
		"xlnx,v-uhdsdi-audio-1.0",
	},
};

static struct snd_soc_card xlnx_card = {
	.name = "xilinx FPGA sound card",
	.owner = THIS_MODULE,
};

static int xlnx_i2s_card_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	int ret, clk_div;
	u32 ch, data_width, sample_rate;

	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	ch = params_channels(params);
	data_width = params_width(params);
	sample_rate = params_rate(params);

	/*
	 * Supports only a fixed combination of 48khz, 24 bits/sample,
	 * 2 channels.
	 */
	if (ch != 2 || data_width != 24 || sample_rate != 48000)
		return -EINVAL;

	/*
	 * For the fixed Mclk, I2S_CLOCK_RATIO of 384 is ued to get 48KHz.
	 * Ex. For a master clock(MCLK) of 18.43MHz and to get 48KHz
	 * sampling rate, Mclk/srate = 384.
	 */
	clk_div = DIV_ROUND_UP(I2S_CLOCK_RATIO, ch * data_width);
	ret = snd_soc_dai_set_clkdiv(cpu_dai, 0, clk_div);

	return ret;
}

static const struct snd_soc_ops xlnx_i2s_card_ops = {
	.hw_params = xlnx_i2s_card_hw_params,
};

static struct snd_soc_dai_link xlnx_snd_dai[][XLNX_MAX_PATHS] = {
	[I2S_AUDIO] = {
		{
			.name = "xilinx-i2s_playback",
			.codec_dai_name = "snd-soc-dummy-dai",
			.codec_name = "snd-soc-dummy",
			.ops = &xlnx_i2s_card_ops,
		},
		{
			.name = "xilinx-i2s_capture",
			.codec_dai_name = "snd-soc-dummy-dai",
			.codec_name = "snd-soc-dummy",
			.ops = &xlnx_i2s_card_ops,
		},
	},
	[HDMI_AUDIO] = {
		{
			.name = "xilinx-hdmi-playback",
			.codec_dai_name = "i2s-hifi",
			.codec_name = "hdmi-audio-codec.0",
			.cpu_dai_name = "snd-soc-dummy-dai",
		},
		{
			.name = "xilinx-hdmi-capture",
			.codec_dai_name = "xlnx_hdmi_rx",
			.cpu_dai_name = "snd-soc-dummy-dai",
		},
	},
	[SDI_AUDIO] = {
		{
			.name = "xlnx-sdi-playback",
			.codec_dai_name = "xlnx_sdi_tx",
			.cpu_dai_name = "snd-soc-dummy-dai",
		},
		{
			.name = "xlnx-sdi-capture",
			.codec_dai_name = "xlnx_sdi_rx",
			.cpu_dai_name = "snd-soc-dummy-dai",
		},

	},
};

static int find_link(struct device_node *node, int direction)
{
	int ret;
	u32 i, size;
	const char **link_names = dev_compat[direction];

	size = ARRAY_SIZE(dev_compat[direction]);

	for (i = 0; i < size; i++) {
		ret = of_device_is_compatible(node, link_names[i]);
		if (ret)
			return i;
	}
	return -ENODEV;
}

static int xlnx_snd_probe(struct platform_device *pdev)
{
	u32 i;
	int ret, audio_interface;
	struct snd_soc_dai_link *dai;

	struct snd_soc_card *card = &xlnx_card;
	struct device_node **node = pdev->dev.platform_data;

	/*
	 * TODO:support multi instance of sound card later. currently,
	 * single instance supported.
	 */
	if (!node || card->instantiated)
		return -ENODEV;

	card->dev = &pdev->dev;

	card->dai_link = devm_kzalloc(card->dev,
				      sizeof(*dai) * XLNX_MAX_PATHS,
				      GFP_KERNEL);
	if (!card->dai_link)
		return -ENOMEM;

	card->num_links = 0;
	for (i = XLNX_PLAYBACK; i < XLNX_MAX_PATHS; i++) {
		dai = &card->dai_link[i];
		audio_interface = find_link(node[i], i);
		switch (audio_interface) {
		case I2S_AUDIO:
			*dai = xlnx_snd_dai[I2S_AUDIO][i];
			dai->platform_of_node = of_parse_phandle(node[i],
								 "xlnx,snd-pcm",
								 0);
			dai->cpu_of_node = node[i];
			card->num_links++;
			dev_dbg(card->dev, "%s registered\n",
				card->dai_link[i].name);
			break;
		case HDMI_AUDIO:
			*dai = xlnx_snd_dai[HDMI_AUDIO][i];
			dai->platform_of_node = of_parse_phandle(node[i],
								 "xlnx,snd-pcm",
								 0);
			if (i == XLNX_CAPTURE)
				dai->codec_of_node = node[i];
			card->num_links++;
			dev_dbg(card->dev, "%s registered\n",
				card->dai_link[i].name);
			break;
		case SDI_AUDIO:
			*dai = xlnx_snd_dai[SDI_AUDIO][i];
			dai->platform_of_node = of_parse_phandle(node[i],
								 "xlnx,snd-pcm",
								 0);
			dai->codec_of_node = node[i];
			card->num_links++;
			dev_dbg(card->dev, "%s registered\n",
				card->dai_link[i].name);
			break;
		default:
			dev_err(card->dev, "Invalid audio interface\n");
			return -ENODEV;
		}
	}

	if (card->num_links) {
		ret = devm_snd_soc_register_card(card->dev, card);
		if (ret) {
			dev_err(card->dev, "%s registration failed\n",
				card->name);
			return ret;
		}
	}
	dev_info(card->dev, "%s registered\n", card->name);

	return 0;
}

static struct platform_driver xlnx_snd_driver = {
	.driver = {
		.name = "xlnx_snd_card",
	},
	.probe = xlnx_snd_probe,
};

module_platform_driver(xlnx_snd_driver);

MODULE_DESCRIPTION("Xilinx FPGA sound card driver");
MODULE_AUTHOR("Maruthi Srinivas Bayyavarapu");
MODULE_LICENSE("GPL v2");
