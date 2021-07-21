// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ASoC sound card support
 *
 * Copyright (C) 2018 Xilinx, Inc.
 */

#include <linux/clk.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "xlnx_snd_common.h"

#define I2S_CLOCK_RATIO 384
#define XLNX_MAX_PL_SND_DEV 6

static DEFINE_IDA(xlnx_snd_card_dev);

enum {
	I2S_AUDIO = 0,
	HDMI_AUDIO,
	SDI_AUDIO,
	SPDIF_AUDIO,
	DP_AUDIO,
	XLNX_MAX_IFACE,
};

static const char *xlnx_snd_card_name[XLNX_MAX_IFACE] = {
	[I2S_AUDIO]	= "xlnx-i2s-snd-card",
	[HDMI_AUDIO]	= "xlnx-hdmi-snd-card",
	[SDI_AUDIO]	= "xlnx-sdi-snd-card",
	[SPDIF_AUDIO]	= "xlnx-spdif-snd-card",
	[DP_AUDIO]	= "xlnx-dp-snd-card",
};

static const char *dev_compat[][XLNX_MAX_IFACE] = {
	[XLNX_PLAYBACK] = {
		"xlnx,i2s-transmitter-1.0",
		"xlnx,v-hdmi-tx-ss-3.1",
		"xlnx,v-uhdsdi-audio-2.0",
		"xlnx,spdif-2.0",
		"xlnx,v-dp-txss-3.0"
	},

	[XLNX_CAPTURE] = {
		"xlnx,i2s-receiver-1.0",
		"xlnx,v-hdmi-rx-ss-3.1",
		"xlnx,v-uhdsdi-audio-2.0",
		"xlnx,spdif-2.0",
		"xlnx,v-dp-rxss-3.0",
	},
};

static int xlnx_spdif_card_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct pl_card_data *prv = snd_soc_card_get_drvdata(rtd->card);
	u32 sample_rate = params_rate(params);

	/* mclk must be >=1024 * sampleing rate */
	prv->mclk_val = 1024 * sample_rate;
	prv->mclk_ratio = 1024;
	return clk_set_rate(prv->mclk, prv->mclk_val);
}

static int xlnx_sdi_card_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct pl_card_data *prv = snd_soc_card_get_drvdata(rtd->card);
	u32 sample_rate = params_rate(params);

	prv->mclk_val = prv->mclk_ratio * sample_rate;
	return clk_set_rate(prv->mclk, prv->mclk_val);
}

static int xlnx_dp_card_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct pl_card_data *prv = snd_soc_card_get_drvdata(rtd->card);
	u32 sample_rate = params_rate(params);

	switch (sample_rate) {
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
	case 176400:
	case 192000:
		prv->mclk_ratio = 512;
		break;
	default:
		return -EINVAL;
	}

	prv->mclk_val = prv->mclk_ratio * sample_rate;
	return clk_set_rate(prv->mclk, prv->mclk_val);
}

static int xlnx_hdmi_card_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct pl_card_data *prv = snd_soc_card_get_drvdata(rtd->card);
	u32 sample_rate = params_rate(params);

	switch (sample_rate) {
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
	case 176400:
	case 192000:
		prv->mclk_ratio = 512;
		break;
	default:
		return -EINVAL;
	}

	prv->mclk_val = prv->mclk_ratio * sample_rate;
	return clk_set_rate(prv->mclk, prv->mclk_val);
}

static int xlnx_i2s_card_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	int ret, clk_div;
	u32 ch, data_width, sample_rate;
	struct pl_card_data *prv;

	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);

	ch = params_channels(params);
	data_width = params_width(params);
	sample_rate = params_rate(params);

	/* only 2 channels supported */
	if (ch != 2)
		return -EINVAL;

	prv = snd_soc_card_get_drvdata(rtd->card);
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		switch (sample_rate) {
		case 5512:
		case 8000:
		case 11025:
		case 16000:
		case 22050:
		case 32000:
		case 44100:
		case 48000:
		case 64000:
		case 88200:
		case 96000:
			prv->mclk_ratio = 384;
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (sample_rate) {
		case 32000:
		case 44100:
		case 48000:
		case 88200:
		case 96000:
			prv->mclk_ratio = 384;
			break;
		case 64000:
		case 176400:
		case 192000:
			prv->mclk_ratio = 192;
			break;
		default:
			return -EINVAL;
		}
	}

	prv->mclk_val = prv->mclk_ratio * sample_rate;
	clk_div = DIV_ROUND_UP(prv->mclk_ratio, 2 * ch * data_width);
	ret = snd_soc_dai_set_clkdiv(cpu_dai, 0, clk_div);
	if (ret)
		return ret;

	return clk_set_rate(prv->mclk, prv->mclk_val);
}

static const struct snd_soc_ops xlnx_sdi_card_ops = {
	.hw_params = xlnx_sdi_card_hw_params,
};

static const struct snd_soc_ops xlnx_i2s_card_ops = {
	.hw_params = xlnx_i2s_card_hw_params,
};

static const struct snd_soc_ops xlnx_hdmi_card_ops = {
	.hw_params = xlnx_hdmi_card_hw_params,
};

static const struct snd_soc_ops xlnx_dp_card_ops = {
	.hw_params = xlnx_dp_card_hw_params,
};

static const struct snd_soc_ops xlnx_spdif_card_ops = {
	.hw_params = xlnx_spdif_card_hw_params,
};

SND_SOC_DAILINK_DEFS(xlnx_i2s_capture,
		     DAILINK_COMP_ARRAY(COMP_CPU("xlnx_i2s_capture")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_PLATFORM(NULL)));

SND_SOC_DAILINK_DEFS(xlnx_i2s_playback,
		     DAILINK_COMP_ARRAY(COMP_CPU("xlnx_i2s_playback")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_PLATFORM(NULL)));

SND_SOC_DAILINK_DEFS(xlnx_hdmi_tx,
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_CODEC("hdmi-audio-codec.0", "i2s-hifi")),
		     DAILINK_COMP_ARRAY(COMP_PLATFORM(NULL)));

SND_SOC_DAILINK_DEFS(xlnx_hdmi_rx,
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "xlnx_hdmi_rx")),
		     DAILINK_COMP_ARRAY(COMP_PLATFORM(NULL)));

SND_SOC_DAILINK_DEFS(xlnx_dp_tx,
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_CODEC("hdmi-audio-codec.0", "i2s-hifi")),
		     DAILINK_COMP_ARRAY(COMP_PLATFORM(NULL)));

SND_SOC_DAILINK_DEFS(xlnx_dp_rx,
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "xlnx_dp_rx")),
		     DAILINK_COMP_ARRAY(COMP_PLATFORM(NULL)));

SND_SOC_DAILINK_DEFS(xlnx_sdi_tx,
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "xlnx_sdi_tx")),
		     DAILINK_COMP_ARRAY(COMP_PLATFORM(NULL)));

SND_SOC_DAILINK_DEFS(xlnx_sdi_rx,
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "xlnx_sdi_rx")),
		     DAILINK_COMP_ARRAY(COMP_PLATFORM(NULL)));

SND_SOC_DAILINK_DEFS(xlnx_spdif,
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_PLATFORM(NULL)));

static struct snd_soc_dai_link xlnx_snd_dai[][XLNX_MAX_PATHS] = {
	[I2S_AUDIO] = {
		{
			.name = "xilinx-i2s_playback",
			SND_SOC_DAILINK_REG(xlnx_i2s_playback),
			.ops = &xlnx_i2s_card_ops,
		},
		{
			.name = "xilinx-i2s_capture",
			SND_SOC_DAILINK_REG(xlnx_i2s_capture),
			.ops = &xlnx_i2s_card_ops,
		},
	},
	[HDMI_AUDIO] = {
		{
			.name = "xilinx-hdmi-playback",
			SND_SOC_DAILINK_REG(xlnx_hdmi_tx),
			.ops = &xlnx_hdmi_card_ops,
		},
		{
			.name = "xilinx-hdmi-capture",
			SND_SOC_DAILINK_REG(xlnx_hdmi_rx),
		},
	},
	[SDI_AUDIO] = {
		{
			.name = "xlnx-sdi-playback",
			SND_SOC_DAILINK_REG(xlnx_sdi_tx),
			.ops = &xlnx_sdi_card_ops,
		},
		{
			.name = "xlnx-sdi-capture",
			SND_SOC_DAILINK_REG(xlnx_sdi_rx),
		},
	},
	[SPDIF_AUDIO] = {
		{
			.name = "xilinx-spdif_playback",
			SND_SOC_DAILINK_REG(xlnx_spdif),
			.ops = &xlnx_spdif_card_ops,
		},
		{
			.name = "xilinx-spdif_capture",
			SND_SOC_DAILINK_REG(xlnx_spdif),
			.ops = &xlnx_spdif_card_ops,
		},
	},
	[DP_AUDIO] = {
		{
			.name = "xilinx-dp-playback",
			SND_SOC_DAILINK_REG(xlnx_dp_tx),
			.ops = &xlnx_dp_card_ops,
		},
		{
			.name = "xilinx-dp-capture",
			SND_SOC_DAILINK_REG(xlnx_dp_rx),
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
	u32 i, max_links = 0, start_count = 0;
	size_t sz;
	char *buf;
	int ret, audio_interface;
	struct snd_soc_dai_link *dai;
	struct pl_card_data *prv;
	struct platform_device *iface_pdev;

	struct snd_soc_card *card;
	struct device_node **node = pdev->dev.platform_data;

	if (!node)
		return -ENODEV;

	if (node[XLNX_PLAYBACK] && node[XLNX_CAPTURE]) {
		max_links = 2;
		start_count = XLNX_PLAYBACK;
	} else if (node[XLNX_PLAYBACK]) {
		max_links = 1;
		start_count = XLNX_PLAYBACK;
	} else if (node[XLNX_CAPTURE]) {
		max_links = 1;
		start_count = XLNX_CAPTURE;
	}

	card = devm_kzalloc(&pdev->dev, sizeof(struct snd_soc_card),
			    GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->dev = &pdev->dev;

	card->dai_link = devm_kzalloc(card->dev,
				      sizeof(*dai) * max_links,
				      GFP_KERNEL);
	if (!card->dai_link)
		return -ENOMEM;

	prv = devm_kzalloc(card->dev,
			   sizeof(struct pl_card_data),
			   GFP_KERNEL);
	if (!prv)
		return -ENOMEM;

	card->num_links = 0;
	for (i = start_count; i < (start_count + max_links); i++) {
		struct device_node *pnode = of_parse_phandle(node[i],
							     "xlnx,snd-pcm", 0);
		if (!pnode) {
			dev_err(card->dev, "platform node not found\n");
			of_node_put(pnode);
			return -ENODEV;
		}

		/*
		 * Check for either playback or capture is enough, as
		 * same clock is used for both.
		 */
		if (i == XLNX_PLAYBACK) {
			iface_pdev = of_find_device_by_node(pnode);
			if (!iface_pdev) {
				of_node_put(pnode);
				return -ENODEV;
			}

			prv->mclk = devm_clk_get(&iface_pdev->dev, "aud_mclk");
			if (IS_ERR(prv->mclk))
				return PTR_ERR(prv->mclk);

		}
		of_node_put(pnode);

		if (max_links == 2)
			dai = &card->dai_link[i];
		else
			dai = &card->dai_link[0];

		audio_interface = find_link(node[i], i);
		switch (audio_interface) {
		case I2S_AUDIO:
			*dai = xlnx_snd_dai[I2S_AUDIO][i];
			dai->platforms->of_node = pnode;
			dai->cpus->of_node = node[i];
			card->num_links++;
			snd_soc_card_set_drvdata(card, prv);
			dev_dbg(card->dev, "%s registered\n",
				card->dai_link[i].name);
			break;
		case HDMI_AUDIO:
			*dai = xlnx_snd_dai[HDMI_AUDIO][i];
			dai->platforms->of_node = pnode;
			if (i == XLNX_CAPTURE)
				dai->codecs->of_node = node[i];
			card->num_links++;
			/* TODO: support multiple sampling rates */
			prv->mclk_ratio = 384;
			snd_soc_card_set_drvdata(card, prv);
			dev_dbg(card->dev, "%s registered\n",
				card->dai_link[i].name);
			break;
		case SDI_AUDIO:
			*dai = xlnx_snd_dai[SDI_AUDIO][i];
			dai->platforms->of_node = pnode;
			dai->codecs->of_node = node[i];
			card->num_links++;
			/* TODO: support multiple sampling rates */
			prv->mclk_ratio = 384;
			snd_soc_card_set_drvdata(card, prv);
			dev_dbg(card->dev, "%s registered\n",
				card->dai_link[i].name);
			break;
		case SPDIF_AUDIO:
			*dai = xlnx_snd_dai[SPDIF_AUDIO][i];
			dai->platforms->of_node = pnode;
			dai->codecs->of_node = node[i];
			card->num_links++;
			prv->mclk_ratio = 384;
			snd_soc_card_set_drvdata(card, prv);
			dev_dbg(card->dev, "%s registered\n",
				card->dai_link[i].name);
			break;
		case DP_AUDIO:
			*dai = xlnx_snd_dai[DP_AUDIO][i];
			dai->platforms->of_node = pnode;
			if (i == XLNX_CAPTURE)
				dai->codecs->of_node = node[i];
			card->num_links++;
			/* TODO: support multiple sampling rates */
			prv->mclk_ratio = 512;
			snd_soc_card_set_drvdata(card, prv);
			dev_dbg(card->dev, "%s registered\n",
				card->dai_link[i].name);
			break;
		default:
			dev_err(card->dev, "Invalid audio interface\n");
			return -ENODEV;
		}
	}

	if (card->num_links) {
		/*
		 *  Example : i2s card name = xlnx-i2s-snd-card-0
		 *  length = number of chars in "xlnx-i2s-snd-card"
		 *	    + 1 ('-'), + 1 (card instance num)
		 *	    + 1 ('\0')
		 */
		sz = strlen(xlnx_snd_card_name[audio_interface]) + 3;
		buf = devm_kzalloc(card->dev, sz, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		prv->xlnx_snd_dev_id = ida_simple_get(&xlnx_snd_card_dev, 0,
						      XLNX_MAX_PL_SND_DEV,
						      GFP_KERNEL);
		if (prv->xlnx_snd_dev_id < 0)
			return prv->xlnx_snd_dev_id;

		snprintf(buf, sz, "%s-%d", xlnx_snd_card_name[audio_interface],
			 prv->xlnx_snd_dev_id);
		card->name = buf;

		ret = devm_snd_soc_register_card(card->dev, card);
		if (ret) {
			dev_err(card->dev, "%s registration failed\n",
				card->name);
			ida_simple_remove(&xlnx_snd_card_dev,
					  prv->xlnx_snd_dev_id);
			return ret;
		}

		dev_set_drvdata(card->dev, prv);
		dev_info(card->dev, "%s registered\n", card->name);
	}

	return 0;
}

static int xlnx_snd_remove(struct platform_device *pdev)
{
	struct pl_card_data *pdata = dev_get_drvdata(&pdev->dev);

	ida_simple_remove(&xlnx_snd_card_dev, pdata->xlnx_snd_dev_id);
	return 0;
}

static struct platform_driver xlnx_snd_driver = {
	.driver = {
		.name = "xlnx_snd_card",
	},
	.probe = xlnx_snd_probe,
	.remove = xlnx_snd_remove,
};

module_platform_driver(xlnx_snd_driver);

MODULE_DESCRIPTION("Xilinx FPGA sound card driver");
MODULE_AUTHOR("Maruthi Srinivas Bayyavarapu");
MODULE_LICENSE("GPL v2");
