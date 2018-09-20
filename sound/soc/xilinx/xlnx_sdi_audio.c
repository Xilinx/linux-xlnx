// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx SDI embed and extract audio support
 *
 * Copyright (c) 2018 Xilinx Pvt., Ltd
 *
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <drm/drm_modes.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define DRIVER_NAME "xlnx-sdi-audio"

#define XSDIAUD_CNTRL_REG_OFFSET		0x00
#define XSDIAUD_SOFT_RST_REG_OFFSET		0x04
#define XSDIAUD_VER_REG_OFFSET			0x08
#define XSDIAUD_INT_EN_REG_OFFSET		0x0C
#define XSDIAUD_INT_STS_REG_OFFSET		0x10
#define XSDIAUD_EMB_VID_CNTRL_REG_OFFSET	0X14
#define XSDIAUD_AUD_CNTRL_REG_OFFSET		0x18
#define XSDIAUD_CH_VALID_REG_OFFSET		0x20
#define XSDIAUD_CH_MUTE_REG_OFFSET		0x30
#define XSDIAUD_ACTIVE_GRP_REG_OFFSET		0X40
#define XSDIAUD_EXT_CH_STAT0_REG_OFFSET		0X48
#define XSDIAUD_GUI_PARAM_REG_OFFSET		0XFC

#define XSDIAUD_CNTRL_EN_MASK		BIT(0)
#define XSDIAUD_SOFT_RST_CONFIG_MASK	BIT(0)
#define XSDIAUD_SOFT_RST_CORE_MASK	BIT(1)
#define XSDIAUD_VER_MAJOR_MASK		GENMASK(31, 24)
#define XSDIAUD_VER_MINOR_MASK		GENMASK(23, 16)

#define XSDIAUD_EMB_CS_UPDATE_MASK	BIT(16)
#define XSDIAUD_EMB_VID_CNT_ELE_SHIFT	(16)
#define XSDIAUD_EMB_VID_CNT_ELE_MASK	BIT(16)
#define XSDIAUD_EMB_VID_CNT_TSCAN_MASK	BIT(8)
#define XSDIAUD_EMB_VID_CNT_TSCAN_SHIFT	(8)
#define XSDIAUD_EMB_VID_CNT_TRATE_SHIFT	(4)
#define XSDIAUD_EMB_AUD_CNT_SS_MASK	BIT(3)
#define XSDIAUD_EMB_AUD_CNT_ASYNC_AUDIO	BIT(4)

#define CH_STATUS_UPDATE_TIMEOUT	40
/* audio params macros */
#define PROF_SAMPLERATE_MASK		GENMASK(7, 6)
#define PROF_SAMPLERATE_SHIFT		6
#define PROF_CHANNEL_COUNT_MASK		GENMASK(11, 8)
#define PROF_CHANNEL_COUNT_SHIFT	8
#define PROF_MAX_BITDEPTH_MASK		GENMASK(18, 16)
#define PROF_MAX_BITDEPTH_SHIFT		16
#define PROF_BITDEPTH_MASK		GENMASK(21, 19)
#define PROF_BITDEPTH_SHIFT		19

#define AES_FORMAT_MASK			BIT(0)
#define PROF_SAMPLERATE_UNDEFINED	0
#define PROF_SAMPLERATE_44100		1
#define PROF_SAMPLERATE_48000		2
#define PROF_SAMPLERATE_32000		3
#define PROF_CHANNELS_UNDEFINED		0
#define PROF_TWO_CHANNELS		8
#define PROF_STEREO_CHANNELS		2
#define PROF_MAX_BITDEPTH_UNDEFINED	0
#define PROF_MAX_BITDEPTH_20		2
#define PROF_MAX_BITDEPTH_24		4

#define CON_SAMPLE_RATE_MASK		GENMASK(27, 24)
#define CON_SAMPLE_RATE_SHIFT		24
#define CON_CHANNEL_COUNT_MASK		GENMASK(23, 20)
#define CON_CHANNEL_COUNT_SHIFT		20
#define CON_MAX_BITDEPTH_MASK		BIT(1)
#define CON_BITDEPTH_MASK		GENMASK(3, 1)
#define CON_BITDEPTH_SHIFT		0x1

#define CON_SAMPLERATE_44100		0
#define CON_SAMPLERATE_48000		2
#define CON_SAMPLERATE_32000		3

enum IP_MODE {
	EMBED,
	EXTRACT,
};

enum channel_id {
	CHAN_ID_0 = 1,
	CHAN_ID_1,
};

enum sdi_transport_family {
	SDI_TRANSPORT_FAMILY_1920,
	SDI_TRANSPORT_FAMILY_1280,
	SDI_TRANSPORT_FAMILY_2048,
	SDI_TRANSPORT_FAMILY_NTSC = 8,
	SDI_TRANSPORT_FAMILY_PAL = 9,
};

/**
 * enum sdi_audio_samplerate - audio sampling rate
 * @XSDIAUD_SAMPRATE0:	48 KHz
 * @XSDIAUD_SAMPRATE1:	44.1 KHz
 * @XSDIAUD_SAMPRATE2:	32 KHz
 */
enum sdi_audio_samplerate {
	XSDIAUD_SAMPRATE0,
	XSDIAUD_SAMPRATE1,
	XSDIAUD_SAMPRATE2
};

/**
 * enum sdi_audio_samplesize - bits per sample
 * @XSDIAUD_SAMPSIZE0:	20 Bit Audio Sample
 * @XSDIAUD_SAMPSIZE1:	24 Bit Audio Sample
 */
enum sdi_audio_samplesize {
	XSDIAUD_SAMPSIZE0,
	XSDIAUD_SAMPSIZE1
};

/**
 * struct audio_params - audio stream parameters
 * @srate: sampling rate
 * @sig_bits: significant bits in container
 * @channels: number of channels
 */
struct audio_params {
	u32 srate;
	u32 sig_bits;
	u32 channels;
};

struct dev_ctx {
	enum IP_MODE mode;
	void __iomem *base;
	struct device *dev;
	struct audio_params *params;
	struct drm_display_mode *video_mode;
	struct snd_pcm_substream *stream;
	bool rx_prams_valid;
	wait_queue_head_t params_q;
};

static irqreturn_t xtract_irq_handler(int irq, void *dev_id)
{
	u32 val;
	struct dev_ctx *ctx = dev_id;

	val = readl(ctx->base + XSDIAUD_INT_STS_REG_OFFSET);
	if (val & XSDIAUD_EMB_CS_UPDATE_MASK) {
		writel(XSDIAUD_EMB_CS_UPDATE_MASK,
		       ctx->base + XSDIAUD_INT_STS_REG_OFFSET);
		val = readl(ctx->base + XSDIAUD_INT_EN_REG_OFFSET);
		/* Disable further interrupts. CH status got updated*/
		writel(val & ~XSDIAUD_EMB_CS_UPDATE_MASK,
		       ctx->base + XSDIAUD_INT_EN_REG_OFFSET);

		ctx->rx_prams_valid = true;
		wake_up_interruptible(&ctx->params_q);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void audio_enable(void __iomem *aud_base)
{
	u32 val;

	val = readl(aud_base + XSDIAUD_CNTRL_REG_OFFSET);
	val |= XSDIAUD_CNTRL_EN_MASK;
	writel(val, aud_base + XSDIAUD_CNTRL_REG_OFFSET);
}

static void audio_disable(void __iomem *aud_base)
{
	u32 val;

	val = readl(aud_base + XSDIAUD_CNTRL_REG_OFFSET);
	val &= ~XSDIAUD_CNTRL_EN_MASK;
	writel(val, aud_base + XSDIAUD_CNTRL_REG_OFFSET);
}

static void audio_reset_core(void __iomem *aud_base, bool reset)
{
	u32 val;

	if (reset) {
		/* reset the core */
		val = readl(aud_base + XSDIAUD_SOFT_RST_REG_OFFSET);
		val |= XSDIAUD_SOFT_RST_CORE_MASK;
		writel(val, aud_base + XSDIAUD_SOFT_RST_REG_OFFSET);
	} else {
		/* bring the core out of reset */
		val = readl(aud_base + XSDIAUD_SOFT_RST_REG_OFFSET);
		val &= ~XSDIAUD_SOFT_RST_CORE_MASK;
		writel(val, aud_base + XSDIAUD_SOFT_RST_REG_OFFSET);
	}
}

static struct audio_params *parse_professional_format(u32 reg1_val,
						      u32 reg2_val)
{
	u32 padded, val;
	struct audio_params *params;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return NULL;

	val = (reg1_val & PROF_SAMPLERATE_MASK) >> PROF_SAMPLERATE_SHIFT;
	switch (val) {
	case PROF_SAMPLERATE_44100:
		params->srate = 44100;
		break;
	case PROF_SAMPLERATE_48000:
		params->srate = 48000;
		break;
	case PROF_SAMPLERATE_32000:
		params->srate = 32000;
		break;
	case PROF_SAMPLERATE_UNDEFINED:
	default:
		/* not indicated */
		kfree(params);
		return NULL;
	}

	val = (reg1_val & PROF_CHANNEL_COUNT_MASK) >> PROF_CHANNEL_COUNT_SHIFT;
	switch (val) {
	case PROF_CHANNELS_UNDEFINED:
	case PROF_STEREO_CHANNELS:
	case PROF_TWO_CHANNELS:
		params->channels = 2;
		break;
	default:
		/* TODO: handle more channels in future*/
		kfree(params);
		return NULL;
	}

	val = (reg1_val & PROF_MAX_BITDEPTH_MASK) >> PROF_MAX_BITDEPTH_SHIFT;
	switch (val) {
	case PROF_MAX_BITDEPTH_UNDEFINED:
	case PROF_MAX_BITDEPTH_20:
		padded = 0;
		break;
	case PROF_MAX_BITDEPTH_24:
		padded = 4;
		break;
	default:
		/* user defined values are not supported */
		kfree(params);
		return NULL;
	}

	val = (reg1_val & PROF_BITDEPTH_MASK) >> PROF_BITDEPTH_SHIFT;
	switch (val) {
	case 1:
		params->sig_bits = 16 + padded;
		break;
	case 2:
		params->sig_bits = 18 + padded;
		break;
	case 4:
		params->sig_bits = 19 + padded;
		break;
	case 5:
		params->sig_bits = 20 + padded;
		break;
	case 6:
		params->sig_bits = 17 + padded;
		break;
	case 0:
	default:
		kfree(params);
		return NULL;
	}

	return params;
}

static struct audio_params *parse_consumer_format(u32 reg1_val, u32 reg2_val)
{
	u32 padded, val;
	struct audio_params *params;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return NULL;

	val = (reg1_val & CON_SAMPLE_RATE_MASK) >> CON_SAMPLE_RATE_SHIFT;
	switch (val) {
	case CON_SAMPLERATE_44100:
		params->srate = 44100;
		break;
	case CON_SAMPLERATE_48000:
		params->srate = 48000;
		break;
	case CON_SAMPLERATE_32000:
		params->srate = 32000;
		break;
	default:
		kfree(params);
		return NULL;
	}

	val = (reg1_val & CON_CHANNEL_COUNT_MASK) >> CON_CHANNEL_COUNT_SHIFT;
	params->channels = val;

	if (reg2_val & CON_MAX_BITDEPTH_MASK)
		padded = 4;
	else
		padded = 0;

	val = (reg2_val & CON_BITDEPTH_MASK) >> CON_BITDEPTH_SHIFT;
	switch (val) {
	case 1:
		params->sig_bits = 16 + padded;
		break;
	case 2:
		params->sig_bits = 18 + padded;
		break;
	case 4:
		params->sig_bits = 19 + padded;
		break;
	case 5:
		params->sig_bits = 20 + padded;
		break;
	case 6:
		params->sig_bits = 17 + padded;
		break;
	case 0:
	default:
		kfree(params);
		return NULL;
	}

	return params;
}

static int xlnx_sdi_rx_pcm_startup(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	int err;
	u32 reg1_val, reg2_val;

	struct snd_pcm_runtime *rtd = substream->runtime;
	struct dev_ctx *ctx = dev_get_drvdata(dai->dev);
	void __iomem *base = ctx->base;
	unsigned long jiffies = msecs_to_jiffies(CH_STATUS_UPDATE_TIMEOUT);

	audio_enable(base);
	writel(XSDIAUD_EMB_CS_UPDATE_MASK,
	       ctx->base + XSDIAUD_INT_EN_REG_OFFSET);
	err = wait_event_interruptible_timeout(ctx->params_q,
					       ctx->rx_prams_valid,
					       jiffies);

	if (!err) {
		dev_err(ctx->dev, "Didn't received valid audio data\n");
		return -EINVAL;
	}
	ctx->rx_prams_valid = false;

	reg1_val = readl(base + XSDIAUD_EXT_CH_STAT0_REG_OFFSET);
	reg2_val = readl(base + XSDIAUD_EXT_CH_STAT0_REG_OFFSET + 4);
	if (reg1_val & AES_FORMAT_MASK)
		ctx->params = parse_professional_format(reg1_val, reg2_val);
	else
		ctx->params = parse_consumer_format(reg1_val, reg2_val);

	if (!ctx->params)
		return -EINVAL;

	dev_info(ctx->dev,
		 "Audio properties: srate %d sig_bits = %d channels = %d\n",
		ctx->params->srate, ctx->params->sig_bits,
		ctx->params->channels);

	err = snd_pcm_hw_constraint_minmax(rtd, SNDRV_PCM_HW_PARAM_RATE,
					   ctx->params->srate,
					   ctx->params->srate);

	if (err < 0) {
		dev_err(ctx->dev, "failed to constrain samplerate to %dHz\n",
			ctx->params->srate);
		kfree(ctx->params);
		return err;
	}

	/*
	 * During record, after AES bits(8) are removed, pcm is at max 24bits.
	 * Out of 24 bits, sig_bits represent valid number of audio bits from
	 * input stream.
	 */
	err = snd_pcm_hw_constraint_msbits(rtd, 0, 24, ctx->params->sig_bits);

	if (err < 0) {
		dev_err(ctx->dev,
			"failed to constrain 'bits per sample' %d bits\n",
			ctx->params->sig_bits);
		kfree(ctx->params);
		return err;
	}

	err = snd_pcm_hw_constraint_minmax(rtd, SNDRV_PCM_HW_PARAM_CHANNELS,
					   ctx->params->channels,
					   ctx->params->channels);
	if (err < 0) {
		dev_err(ctx->dev,
			"failed to constrain channel count to %d\n",
			ctx->params->channels);
		kfree(ctx->params);
		return err;
	}

	dev_info(ctx->dev, " sdi rx audio enabled\n");
	return 0;
}

static void xlnx_sdi_rx_pcm_shutdown(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct dev_ctx *ctx = dev_get_drvdata(dai->dev);

	kfree(ctx->params);
	audio_disable(ctx->base);

	dev_info(dai->dev, " sdi rx audio disabled\n");
}

static int xlnx_sdi_tx_pcm_startup(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct dev_ctx *ctx = dev_get_drvdata(dai->dev);

	audio_enable(ctx->base);
	ctx->stream = substream;

	dev_info(ctx->dev, " sdi tx audio enabled\n");
	return 0;
}

static int xlnx_sdi_tx_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	u32 val = 0;
	u32 num_channels, sample_rate, sig_bits;

	struct dev_ctx *ctx = dev_get_drvdata(dai->dev);
	void __iomem *base = ctx->base;

	/* video mode properties needed by audio driver are shared to audio
	 * driver through a pointer in platform data. This is used here in
	 * audio driver. The solution may be needed to modify/extend to avoid
	 * probable error scenarios
	 */
	if (!ctx->video_mode || !ctx->video_mode->vdisplay ||
	    !drm_mode_vrefresh(ctx->video_mode)) {
		dev_err(ctx->dev, "couldn't find video display properties\n");
		return -EINVAL;
	}

	/* map video properties */
	switch (ctx->video_mode->hdisplay) {
	case 1920:
		val = SDI_TRANSPORT_FAMILY_1920;
		break;
	case 1280:
		val |= SDI_TRANSPORT_FAMILY_1280;
		break;
	case 2048:
		val |= SDI_TRANSPORT_FAMILY_2048;
		break;
	case 720:
		if (ctx->video_mode->vdisplay == 486)
			val |= SDI_TRANSPORT_FAMILY_NTSC;
		else if (ctx->video_mode->vdisplay == 576)
			val |= SDI_TRANSPORT_FAMILY_PAL;
		else
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	switch (drm_mode_vrefresh(ctx->video_mode)) {
	case 24:
		val |= (3 << XSDIAUD_EMB_VID_CNT_TRATE_SHIFT);
		break;
	case 25:
		val |= (5 << XSDIAUD_EMB_VID_CNT_TRATE_SHIFT);
		break;
	case 30:
		val |= (7 << XSDIAUD_EMB_VID_CNT_TRATE_SHIFT);
		break;
	case 48:
		val |= (8 << XSDIAUD_EMB_VID_CNT_TRATE_SHIFT);
		break;
	case 50:
		val |= (9 << XSDIAUD_EMB_VID_CNT_TRATE_SHIFT);
		break;
	case 60:
		val |= (11 << XSDIAUD_EMB_VID_CNT_TRATE_SHIFT);
		break;
	default:
		return -EINVAL;
	}

	if (!(ctx->video_mode->flags & DRM_MODE_FLAG_INTERLACE))
		val |= XSDIAUD_EMB_VID_CNT_TSCAN_MASK;

	val |= XSDIAUD_EMB_VID_CNT_ELE_MASK;

	writel(val, base + XSDIAUD_EMB_VID_CNTRL_REG_OFFSET);

	/* map audio properties */
	num_channels = params_channels(params);
	sample_rate = params_rate(params);
	sig_bits = snd_pcm_format_width(params_format(params));

	dev_info(ctx->dev,
		 "stream params: channels = %d sample_rate = %d bits = %d\n",
		 num_channels, sample_rate, sig_bits);

	val = 0;
	val |= XSDIAUD_EMB_AUD_CNT_ASYNC_AUDIO;

	switch (sample_rate) {
	case 48000:
		val |= XSDIAUD_SAMPRATE0;
		break;
	case 44100:
		val |= XSDIAUD_SAMPRATE1;
		break;
	case 32000:
		val |= XSDIAUD_SAMPRATE2;
		break;
	default:
		return -EINVAL;
	}

	if (sig_bits == 24)
		val |= XSDIAUD_EMB_AUD_CNT_SS_MASK;

	writel(val, base + XSDIAUD_AUD_CNTRL_REG_OFFSET);

	/* TODO: support more channels, currently only 2. */
	writel(CHAN_ID_1 | CHAN_ID_0, base + XSDIAUD_CH_VALID_REG_OFFSET);

	return 0;
}

static void xlnx_sdi_tx_pcm_shutdown(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct dev_ctx *ctx = dev_get_drvdata(dai->dev);
	void __iomem *base = ctx->base;

	audio_disable(base);
	ctx->stream = NULL;

	dev_info(ctx->dev, " sdi tx audio disabled\n");
}

static const struct snd_soc_component_driver xlnx_sdi_component = {
	.name = "xlnx-sdi-dai-component",
};

static const struct snd_soc_dai_ops xlnx_sdi_rx_dai_ops = {
	.startup = xlnx_sdi_rx_pcm_startup,
	.shutdown = xlnx_sdi_rx_pcm_shutdown,
};

static struct snd_soc_dai_driver xlnx_sdi_rx_dai = {
	.name = "xlnx_sdi_rx",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &xlnx_sdi_rx_dai_ops,
};

static const struct snd_soc_dai_ops xlnx_sdi_tx_dai_ops = {
	.startup =	xlnx_sdi_tx_pcm_startup,
	.hw_params =	xlnx_sdi_tx_hw_params,
	.shutdown =	xlnx_sdi_tx_pcm_shutdown,
};

static struct snd_soc_dai_driver xlnx_sdi_tx_dai = {
	.name = "xlnx_sdi_tx",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &xlnx_sdi_tx_dai_ops,
};

static int xlnx_sdi_audio_probe(struct platform_device *pdev)
{
	u32 val;
	int ret;
	struct dev_ctx *ctx;
	struct resource *res;
	struct device *video_dev;
	struct device_node *video_node;
	struct platform_device *video_pdev;
	struct snd_soc_dai_driver *snd_dai;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;

	/* TODO - remove before upstreaming */
	if (of_device_is_compatible(node, "xlnx,v-uhdsdi-audio-1.0")) {
		dev_err(&pdev->dev, "driver doesn't support sdi audio v1.0\n");
		return -ENODEV;
	}

	ctx = devm_kzalloc(&pdev->dev, sizeof(struct dev_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No IO MEM resource found\n");
		return -ENODEV;
	}

	ctx->base = devm_ioremap_resource(&pdev->dev, res);
	if (!ctx->base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return -EADDRNOTAVAIL;
	}

	ctx->dev = &pdev->dev;

	val = readl(ctx->base + XSDIAUD_GUI_PARAM_REG_OFFSET);
	if (val & BIT(6)) {
		ctx->mode = EXTRACT;
		res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
		if (!res) {
			dev_err(&pdev->dev, "No IRQ resource found\n");
			return -ENODEV;
		}
		ret = devm_request_irq(&pdev->dev, res->start,
				       xtract_irq_handler,
				       0, "XLNX_SDI_AUDIO_XTRACT", ctx);
		if (ret) {
			dev_err(&pdev->dev, "extract irq request failed\n");
			return -ENODEV;
		}

		init_waitqueue_head(&ctx->params_q);

		snd_dai = &xlnx_sdi_rx_dai;
	} else {
		ctx->mode = EMBED;

		video_node = of_graph_get_remote_node(pdev->dev.of_node, 0, 0);
		if (!video_node) {
			dev_err(ctx->dev, "video_node not found\n");
			of_node_put(video_node);
			return -ENODEV;
		}

		video_pdev = of_find_device_by_node(video_node);
		if (!video_pdev) {
			of_node_put(video_node);
			return -ENODEV;
		}

		video_dev = &video_pdev->dev;
		ctx->video_mode =
			(struct drm_display_mode *)video_dev->platform_data;
		/* invalid 'platform_data' implies video driver is not loaded */
		if (!ctx->video_mode) {
			of_node_put(video_node);
			return -EPROBE_DEFER;
		}

		snd_dai = &xlnx_sdi_tx_dai;
		of_node_put(video_node);
	}

	ret = devm_snd_soc_register_component(&pdev->dev, &xlnx_sdi_component,
					      snd_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "couldn't register codec DAI\n");
		return ret;
	}

	dev_set_drvdata(&pdev->dev, ctx);

	audio_reset_core(ctx->base, true);
	audio_reset_core(ctx->base, false);

	dev_info(&pdev->dev, "xlnx sdi codec dai component registered\n");
	return 0;
}

static int xlnx_sdi_audio_remove(struct platform_device *pdev)
{
	struct dev_ctx *ctx = dev_get_drvdata(&pdev->dev);

	audio_disable(ctx->base);
	audio_reset_core(ctx->base, true);

	return 0;
}

static const struct of_device_id xlnx_sdi_audio_of_match[] = {
	{ .compatible = "xlnx,v-uhdsdi-audio-1.0"},
	{ .compatible = "xlnx,v-uhdsdi-audio-2.0"},
	{ }
};
MODULE_DEVICE_TABLE(of, xlnx_sdi_audio_of_match);

static struct platform_driver xlnx_sdi_audio_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = xlnx_sdi_audio_of_match,
	},
	.probe = xlnx_sdi_audio_probe,
	.remove = xlnx_sdi_audio_remove,
};

module_platform_driver(xlnx_sdi_audio_driver);

MODULE_DESCRIPTION("xilinx sdi audio codec driver");
MODULE_AUTHOR("Maruthi Srinivas Bayyavarapu");
MODULE_LICENSE("GPL v2");
