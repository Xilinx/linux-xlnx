// SPDX-License-Identifier: GPL-2.0
/*
 * Multimedia Integrated Display Controller Driver - Audio Support
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: AMD, Inc.
 *
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>

#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "mmi_dc.h"
#include "mmi_dc_audio.h"

#define MMI_DC_AVBUF_AUDSTRM_SEL_MEM	2

#define MMI_DC_AVBUF_OUTPUT_AUDSTREAM1_SEL_MASK	GENMASK(5, 4)

#define MMI_DC_AUD_CH_STATUS(x)		(0x8 + ((x) * 4))

#define MMI_DC_AV_CHBUF_AUD		0xC08
#define MMI_DC_AV_CHBUF_AUD_EN		BIT(0)
#define MMI_DC_AV_CHBUF_AUD_FLUSH	BIT(1)
#define MMI_DC_AV_CHBUF_AUD_BURST_LEN	GENMASK(6, 2)

#define MMI_DC_AUD_CLK			0xC60
#define MMI_DC_AUDIO			0xC68
#define MMI_DC_AUDIO_EN			BIT(0)
#define MMI_DC_AUD_SOFT_RESET		0xC00
#define MMI_DISP_AUD_FS_PL_MULT		512

struct mmi_audio {
	struct snd_soc_card card;

	const char *dai_name;
	const char *link_name;
	const char *pcm_name;

	struct snd_soc_dai_driver dai_driver;
	struct snd_dmaengine_pcm_config pcm_config;

	struct snd_soc_dai_link link;

	struct {
		struct snd_soc_dai_link_component cpu;
		struct snd_soc_dai_link_component codec;
		struct snd_soc_dai_link_component platform;
	} components;

	/* Protect the frequency */
	struct mutex enable_lock;

	u32 current_rate;
};

static const struct snd_pcm_hardware mmi_dc_pcm_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME |
		SNDRV_PCM_INFO_NO_PERIOD_WAKEUP,

	.buffer_bytes_max	= 128 * 1024,
	.period_bytes_min	= 256,
	.period_bytes_max	= 1024 * 1024,
	.periods_min		= 2,
	.periods_max		= 256,
};

static int mmi_dc_aud_snd_ops_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				   256);

	return 0;
}

static const struct snd_soc_ops mmi_dc_ops = {
	.startup = mmi_dc_aud_snd_ops_startup,
};

static int dc_dai_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *socdai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct mmi_dc *dc =
		snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	struct mmi_audio *audio = dc->audio;
	int ret;
	u32 sample_rate;
	struct snd_aes_iec958 iec = { 0 };
	u32 val;
	u64 rate;

	sample_rate = params_rate(params);

	/* TODO : Add support for other sampling rates i.e 44.1 kHz, 96 kHz, etc. */
	if (sample_rate != 48000)
		return -EINVAL;

	guard(mutex)(&audio->enable_lock);

	audio->current_rate = sample_rate;

	/* Note: clock rate can only be changed if the clock is disabled */
	ret = clk_set_rate(dc->aud_clk,
			   sample_rate * MMI_DISP_AUD_FS_PL_MULT);
	if (ret) {
		dev_err(dc->dev, "can't set aud_clk to %u err:%d\n",
			sample_rate * MMI_DISP_AUD_FS_PL_MULT, ret);
		return ret;
	}

	clk_prepare_enable(dc->aud_clk);

	rate = clk_get_rate(dc->aud_clk);

	dev_dbg(dc->dev, "get rate value = %llu\n", rate);

	pm_runtime_get_sync(dc->dev);

	/* Clear the audio soft reset register as it's an non-reset flop. */
	dc_write_misc(dc, MMI_DC_AUD_SOFT_RESET, 0x1);
	dc_write_misc(dc, MMI_DC_AUD_SOFT_RESET, 0);

	/* TODO: Set this audio register based on available clock i.e PS/PL */
	dc_write_misc(dc, MMI_DC_AUD_CLK, 0x0);

	/* DC Audio Enabled */
	dc_write_misc(dc, MMI_DC_AUDIO, MMI_DC_AUDIO_EN);

	/* DC Audio Flush */
	dc_write_misc(dc, MMI_DC_AV_CHBUF_AUD, MMI_DC_AV_CHBUF_AUD_FLUSH);
	val = FIELD_PREP(MMI_DC_AV_CHBUF_AUD_BURST_LEN, 0xF) |
	      MMI_DC_AV_CHBUF_AUD_EN;
	dc_write_misc(dc, MMI_DC_AV_CHBUF_AUD, val);

	/* Audio channel status */
	if (sample_rate == 48000)
		iec.status[3] = IEC958_AES3_CON_FS_48000;
	for (unsigned int i = 0; i < AES_IEC958_STATUS_SIZE / 4; ++i) {
		u32 v;

		v = (iec.status[(i * 4) + 0] << 0) |
		    (iec.status[(i * 4) + 1] << 8) |
		    (iec.status[(i * 4) + 2] << 16) |
		    (iec.status[(i * 4) + 3] << 24);

		dc_write_misc(dc, MMI_DC_AUD_CH_STATUS(i), v);
	}

	/* Read and modify the AVBUF output select register */
	val = dc_read_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT);
	val &= ~(MMI_DC_AVBUF_OUTPUT_AUDSTREAM1_SEL_MASK);
	val |= FIELD_PREP(MMI_DC_AVBUF_OUTPUT_AUDSTREAM1_SEL_MASK,
				  MMI_DC_AVBUF_AUDSTRM_SEL_MEM);
	dc_write_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT, val);

	return 0;
}

static int dc_dai_hw_free(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *socdai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct mmi_dc *dc =
		snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	struct mmi_audio *audio = dc->audio;

	guard(mutex)(&audio->enable_lock);

	pm_runtime_put(dc->dev);

	clk_disable_unprepare(dc->aud_clk);

	audio->current_rate = 0;

	return 0;
}

static const struct snd_soc_dai_ops mmi_dc_dai_ops = {
	.hw_params	= dc_dai_hw_params,
	.hw_free	= dc_dai_hw_free,
};

static const struct snd_soc_component_driver mmi_dc_component_driver = {
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

int mmi_dc_audio_init(struct mmi_dc *dc)
{
	struct device *dev = dc->dev;
	struct mmi_audio *audio;
	struct snd_soc_card *card;
	struct snd_dmaengine_pcm_config *pcm_config;
	void *dev_data;
	int ret;

	if (!dc->aud_clk)
		return -EINVAL;

	audio = devm_kzalloc(dev, sizeof(*audio), GFP_KERNEL);
	if (!audio)
		return -ENOMEM;

	dc->audio = audio;

	mutex_init(&audio->enable_lock);

	audio->dai_name = devm_kasprintf(dev, GFP_KERNEL,
					 "%s-dai", dev_name(dev));

	audio->link_name = devm_kasprintf(dev, GFP_KERNEL,
					  "%s-dc-%u", dev_name(dev), 0);
	audio->pcm_name = devm_kasprintf(dev, GFP_KERNEL,
					 "%s-pcm-%u", dev_name(dev), 0);

	/* Create CPU DAI */

	audio->dai_driver = (struct snd_soc_dai_driver) {
		.name		= audio->dai_name,
		.ops		= &mmi_dc_dai_ops,
		.playback	= {
	/* TODO: Add audio support from 1 to 8 channels */
			.channels_min	= 8,
			.channels_max	= 8,
			.rates		= SNDRV_PCM_RATE_48000,
			.formats	= SNDRV_PCM_FMTBIT_S16_LE,
		},
	};

	ret = devm_snd_soc_register_component(dev, &mmi_dc_component_driver,
					      &audio->dai_driver, 1);
	if (ret) {
		dev_err(dev, "Failed to register CPU DAI\n");
		return ret;
	}

	/* Create PCMs */
	pcm_config = &audio->pcm_config;

	*pcm_config = (struct snd_dmaengine_pcm_config) {
			.name = audio->pcm_name,
			.pcm_hardware = &mmi_dc_pcm_hw,
			.prealloc_buffer_size = 64 * 1024,
			.chan_names[SNDRV_PCM_STREAM_PLAYBACK] = "aud",
		};

	ret = devm_snd_dmaengine_pcm_register(dev, pcm_config, 0);
	if (ret) {
		dev_err(dev, "Failed to register PCM\n");
		return ret;
	}

	/* Create card */

	card = &audio->card;
	card->name = "MMI_DC_AUDIO";
	card->long_name = "Multimedia Integrated Display Controller Audio";
	card->driver_name = "mmi_dc";
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->num_links = 1;
	card->dai_link = &audio->link;

	struct snd_soc_dai_link *link = card->dai_link;

	link->ops = &mmi_dc_ops;

	link->name = audio->link_name;
	link->stream_name = audio->link_name;

	link->cpus = &audio->components.cpu;
	link->num_cpus = 1;
	link->cpus->dai_name = audio->dai_name;

	link->codecs = &audio->components.codec;
	link->num_codecs = 1;
	link->codecs->name = "snd-soc-dummy";
	link->codecs->dai_name = "snd-soc-dummy-dai";

	link->platforms = &audio->components.platform;
	link->num_platforms = 1;
	link->platforms->name = audio->pcm_name;

	/*
	 * HACK: devm_snd_soc_register_card() overwrites current drvdata
	 * so we need to hack it back.
	 */
	dev_data = dev_get_drvdata(dev);
	ret = devm_snd_soc_register_card(dev, card);
	dev_set_drvdata(dev, dev_data);
	if (ret) {
		dev_err(dev, "Failed to register sound card, disabling audio support\n");

		devm_kfree(dev, audio);
		dc->audio = NULL;
	}
	return 0;
}

void mmi_dc_audio_uninit(struct mmi_dc *dc)
{
	struct mmi_audio *audio = dc->audio;

	if (!audio)
		return;

	if (!dc->aud_clk)
		return;

	mutex_destroy(&audio->enable_lock);
}
