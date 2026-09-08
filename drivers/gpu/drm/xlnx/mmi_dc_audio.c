// SPDX-License-Identifier: GPL-2.0
/*
 * Multimedia Integrated Display Controller Driver - Audio Support
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
 *
 * Author: AMD, Inc.
 *
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/math64.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>

#include <linux/dma/mmi_dcdma.h>
#include <linux/xlnx/mmi_audio.h>

#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "mmi_dc.h"
#include "mmi_dc_audio.h"

#define MMI_DC_AVBUF_AUDSTRM_SEL_NONE	1
#define MMI_DC_AVBUF_AUDSTRM_SEL_MEM	2

#define MMI_DC_AVBUF_OUTPUT_AUDSTREAM1_SEL_MASK	GENMASK(5, 4)

#define MMI_DC_AUD_CHANNELS_SELECT		0x0
#define MMI_DC_AUD_CHANNELS_SELECT_CH_STATUS	BIT(8)

#define MMI_DC_AUD_CH_STATUS(x)		(0x8 + ((x) * 4))

#define MMI_DC_AV_CHBUF_AUD		0xC08
#define MMI_DC_NL_AUD			0xC0C
#define MMI_DC_AV_CHBUF_AUD_EN		BIT(0)
#define MMI_DC_AV_CHBUF_AUD_FLUSH	BIT(1)
#define MMI_DC_AV_CHBUF_AUD_BURST_LEN	GENMASK(6, 2)

#define MMI_DC_NL_AUD_CHANNEL_SEL	GENMASK(2, 0)
#define MMI_DC_NL_AUD_SAMPLE_RATE	GENMASK(14, 3)
#define MMI_DC_NL_AUD_CHANNEL_SEL_8CH	0x7
#define MMI_DC_NL_AUD_SAMPLE_RATE_ALL	0x1FF

#define MMI_DC_AUDIO			0xC68
#define MMI_DC_AUDIO_EN			BIT(0)
#define MMI_DC_AUD_SOFT_RESET		0xC00
#define MMI_DC_AV_CHBUF_AUD_MAX_BURST	0xF
#define MMI_DC_AUDIO_CH_MIN		1
#define MMI_DC_AUDIO_CH_MAX		8
#define MMI_DISP_AUD_FS_PL_MULT		512
#define MMI_DISP_AUD_FS_PS_MULT		64

/*
 * The audio clock cannot run below 3 MHz. At the PS multiplier of 64x only
 * 44.1 kHz falls under that floor, so that rate needs the PL audio clock.
 */
#define MMI_DC_AUD_CLK_MIN_RATE		3000000U

/* Reject a clock the provider rounded more than 0.1% away from the request. */
#define MMI_DC_AUD_CLK_MAX_ERR_PPM	1000U

/*
 * IEC 60958 channel status. Each pair of channels has its own group of six
 * words, 0xC008 for channels 0 and 1 through 0xC050 for channels 6 and 7, and
 * a group is one 192-bit channel status image with the words in reverse order,
 * so REG5 carries the low bits and REG0 the high ones.
 *
 * The shifts below are the positions the DC register definition gives, which
 * sit one bit below where the IEC 60958-3 byte layout alone would put them:
 * sampling frequency is IEC bits 24..27 but REG5 bits 23..26, and original
 * sampling frequency is IEC bits 36..39 but REG4 bits 3..6. Deriving them from
 * the IEC layout instead of from the register definition gets them wrong by
 * that one bit.
 *
 * CH_COUNT0 and CH_COUNT1 are two channel count nibbles rather than the IEC
 * channel number and source number that fall at those positions. The hardware
 * carries the active channel count in both, in every group.
 */
#define MMI_DC_AUD_CH_STATUS_PAIRS		4
#define MMI_DC_AUD_CH_STATUS_REGS_PER_PAIR	6
#define MMI_DC_AUD_CH_STATUS_REG4_VALID		BIT(0)
#define MMI_DC_AUD_CH_STATUS_REG4_FS_SHIFT	3
#define MMI_DC_AUD_CH_STATUS_REG5_FS_SHIFT	23
#define MMI_DC_AUD_CH_STATUS_REG5_CH_COUNT0_SHIFT	19
#define MMI_DC_AUD_CH_STATUS_REG5_CH_COUNT1_SHIFT	15

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
		SNDRV_PCM_INFO_RESUME,

	.buffer_bytes_max	= 128 * 1024,
	.period_bytes_min	= 256,
	.period_bytes_max	= 128 * 1024,
	.periods_min		= 2,
	.periods_max		= 256,
};

/*
 * Streams narrower than eight channels are expanded into a shadow buffer by the
 * DMA driver, which costs eight times the application buffer in coherent memory
 * and one period of memcpy() per DONE interrupt. Both want an upper bound, but
 * an eight channel stream is transferred straight out of the application buffer
 * and needs neither.
 *
 * snd_pcm_hardware is applied at open(), before the channel count is known, so
 * putting these in mmi_dc_pcm_hw would charge the pass-through case for an
 * expansion it never does. Apply them as hw rules on the channel count instead.
 */
#define MMI_DC_AUDIO_EXPAND_BUFFER_BYTES_MAX	(64 * 1024)
#define MMI_DC_AUDIO_EXPAND_PERIOD_BYTES_MAX	(8 * 1024)

/*
 * The DMA driver refreshes the period before the one each DONE completes, so
 * the hardware read pointer stays periods - 2 periods away from the period
 * being written. Four keeps that margin at two periods; below three it vanishes
 * entirely.
 */
#define MMI_DC_AUDIO_EXPAND_PERIODS_MIN		4

/* Returns true once the channel count rules out the pass-through path. */
static bool mmi_dc_audio_expands(const struct snd_pcm_hw_params *params)
{
	const struct snd_interval *channels =
		hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);

	return channels->max < MMI_DC_AUDIO_CH_MAX;
}

static int mmi_dc_audio_rule_expand_max(struct snd_pcm_hw_params *params,
					snd_pcm_hw_param_t var,
					unsigned int bytes_max)
{
	struct snd_interval limit;

	if (!mmi_dc_audio_expands(params))
		return 0;

	snd_interval_any(&limit);
	limit.max = bytes_max;

	return snd_interval_refine(hw_param_interval(params, var), &limit);
}

static int mmi_dc_audio_rule_periods(struct snd_pcm_hw_params *params,
				     struct snd_pcm_hw_rule *rule)
{
	struct snd_interval limit;

	if (!mmi_dc_audio_expands(params))
		return 0;

	snd_interval_any(&limit);
	limit.min = MMI_DC_AUDIO_EXPAND_PERIODS_MIN;

	return snd_interval_refine(hw_param_interval(params,
						     SNDRV_PCM_HW_PARAM_PERIODS),
				   &limit);
}

static int mmi_dc_audio_rule_buffer_bytes(struct snd_pcm_hw_params *params,
					  struct snd_pcm_hw_rule *rule)
{
	return mmi_dc_audio_rule_expand_max(params,
					    SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					    MMI_DC_AUDIO_EXPAND_BUFFER_BYTES_MAX);
}

static int mmi_dc_audio_rule_period_bytes(struct snd_pcm_hw_params *params,
					  struct snd_pcm_hw_rule *rule)
{
	return mmi_dc_audio_rule_expand_max(params,
					    SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
					    MMI_DC_AUDIO_EXPAND_PERIOD_BYTES_MAX);
}

static int mmi_dc_aud_snd_ops_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	ret = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 256);
	if (ret < 0)
		return ret;

	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				  mmi_dc_audio_rule_buffer_bytes, NULL,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (ret < 0)
		return ret;

	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				  mmi_dc_audio_rule_period_bytes, NULL,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (ret < 0)
		return ret;

	return snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIODS,
				   mmi_dc_audio_rule_periods, NULL,
				   SNDRV_PCM_HW_PARAM_CHANNELS, -1);
}

static const struct snd_soc_ops mmi_dc_ops = {
	.startup = mmi_dc_aud_snd_ops_startup,
};

static void mmi_dc_audio_disable_clk(struct mmi_dc *dc)
{
	struct clk *aud_clk = mmi_dc_get_aud_clk(dc);

	if (aud_clk)
		clk_disable_unprepare(aud_clk);
}

/**
 * struct mmi_dc_audio_rate - Supported audio sample rate description
 * @rate: sample rate in Hz
 * @pcm_rate: matching SNDRV_PCM_RATE_* flag
 * @fs_nibble: IEC 60958 sampling-frequency nibble for REG5
 */
struct mmi_dc_audio_rate {
	u32 rate;
	unsigned int pcm_rate;
	u8 fs_nibble;
};

static const struct mmi_dc_audio_rate mmi_dc_audio_rates[] = {
	{  44100, SNDRV_PCM_RATE_44100,  IEC958_AES3_CON_FS_44100  },
	{  48000, SNDRV_PCM_RATE_48000,  IEC958_AES3_CON_FS_48000  },
	{  88200, SNDRV_PCM_RATE_88200,  IEC958_AES3_CON_FS_88200  },
	{  96000, SNDRV_PCM_RATE_96000,  IEC958_AES3_CON_FS_96000  },
	{ 176400, SNDRV_PCM_RATE_176400, IEC958_AES3_CON_FS_176400 },
	{ 192000, SNDRV_PCM_RATE_192000, IEC958_AES3_CON_FS_192000 },
};

static const struct mmi_dc_audio_rate *mmi_dc_audio_find_rate(u32 sample_rate)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mmi_dc_audio_rates); i++)
		if (mmi_dc_audio_rates[i].rate == sample_rate)
			return &mmi_dc_audio_rates[i];

	return NULL;
}

static u32 mmi_dc_audio_clk_mult(struct mmi_dc *dc)
{
	return mmi_dc_get_aud_clk_src(dc) == MMIDC_PL_AUD_CLK ?
	       MMI_DISP_AUD_FS_PL_MULT : MMI_DISP_AUD_FS_PS_MULT;
}

static bool mmi_dc_audio_rate_supported(struct mmi_dc *dc, u32 sample_rate)
{
	if (!mmi_dc_audio_find_rate(sample_rate))
		return false;

	return sample_rate * mmi_dc_audio_clk_mult(dc) >=
	       MMI_DC_AUD_CLK_MIN_RATE;
}

/* Rates the currently selected audio clock can actually reach. */
static unsigned int mmi_dc_audio_rate_mask(struct mmi_dc *dc)
{
	unsigned int mask = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mmi_dc_audio_rates); i++)
		if (mmi_dc_audio_rate_supported(dc, mmi_dc_audio_rates[i].rate))
			mask |= mmi_dc_audio_rates[i].pcm_rate;

	return mask;
}

static bool mmi_dc_audio_channels_supported(u32 num_audio_channels)
{
	return num_audio_channels >= MMI_DC_AUDIO_CH_MIN &&
	       num_audio_channels <= MMI_DC_AUDIO_CH_MAX;
}

/**
 * mmi_dc_audio_setup_clk - Configure and enable audio clock
 * @dc: MMI DC device
 * @sample_rate: requested audio sample rate in Hz
 *
 * On success, takes a runtime PM reference which dc_dai_hw_free() releases.
 * Nothing is left held if this fails.
 *
 * Return: 0 on success or negative error code.
 */
static int mmi_dc_audio_setup_clk(struct mmi_dc *dc, u32 sample_rate)
{
	struct clk *aud_clk = mmi_dc_get_aud_clk(dc);
	bool aud_clk_is_pl;
	u32 aud_clk_mult;
	u32 wanted, err_ppm;
	u64 rate;
	int ret;

	if (!aud_clk)
		return -EINVAL;

	aud_clk_is_pl = mmi_dc_get_aud_clk_src(dc) == MMIDC_PL_AUD_CLK;
	aud_clk_mult = mmi_dc_audio_clk_mult(dc);
	wanted = sample_rate * aud_clk_mult;

	ret = clk_set_rate(aud_clk, wanted);
	if (ret) {
		dev_err(dc->dev, "can't set aud_clk to %u err:%d\n", wanted, ret);
		return ret;
	}

	/*
	 * clk_set_rate() succeeding only means the provider accepted the
	 * request, not that it landed on it. Playing back from a clock that is
	 * off by more than the tolerance would shift the pitch.
	 */
	rate = clk_get_rate(aud_clk);
	err_ppm = div64_u64(abs((s64)rate - (s64)wanted) * 1000000ULL, wanted);
	if (err_ppm > MMI_DC_AUD_CLK_MAX_ERR_PPM) {
		dev_err(dc->dev,
			"aud_clk off by %u ppm (req=%u actual=%llu)\n",
			err_ppm, wanted, rate);
		return -EINVAL;
	}

	ret = clk_prepare_enable(aud_clk);
	if (ret) {
		dev_err(dc->dev, "failed to enable aud_clk: %d\n", ret);
		return ret;
	}

	dev_dbg(dc->dev, "using %s audio clock, req=%u actual=%llu\n",
		aud_clk_is_pl ? "PL" : "PS", wanted, rate);

	pm_runtime_get_sync(dc->dev);

	return 0;
}

/**
 * mmi_dc_audio_configure_hw - Reset and configure audio hardware
 * @dc: MMI DC device
 * @num_audio_channels: Number of audio channels (1 to 8)
 *
 * Performs audio soft reset, enables audio, and configures
 * the audio channel buffer.
 */
static void mmi_dc_audio_configure_hw(struct mmi_dc *dc, u32 num_audio_channels)
{
	u32 val;

	/* Clear the audio soft reset register as it's a non-reset flop */
	dc_write_misc(dc, MMI_DC_AUD_SOFT_RESET, 0x1);
	dc_write_misc(dc, MMI_DC_AUD_SOFT_RESET, 0);

	/* Enable DC Audio */
	dc_write_misc(dc, MMI_DC_AUDIO, MMI_DC_AUDIO_EN);

	/* Flush and enable audio channel buffer */
	dc_write_misc(dc, MMI_DC_AV_CHBUF_AUD, MMI_DC_AV_CHBUF_AUD_FLUSH);
	val = FIELD_PREP(MMI_DC_AV_CHBUF_AUD_BURST_LEN,
			 MMI_DC_AV_CHBUF_AUD_MAX_BURST) |
	      MMI_DC_AV_CHBUF_AUD_EN;
	dc_write_misc(dc, MMI_DC_AV_CHBUF_AUD, val);

	val = FIELD_PREP(MMI_DC_NL_AUD_CHANNEL_SEL,
			 MMI_DC_NL_AUD_CHANNEL_SEL_8CH) |
	      FIELD_PREP(MMI_DC_NL_AUD_SAMPLE_RATE,
			 MMI_DC_NL_AUD_SAMPLE_RATE_ALL);
	dc_write_misc(dc, MMI_DC_NL_AUD, val);

	/*
	 * Mute the unused channels: clear bits 0..(N-1), set bits N..7. Bit 8
	 * stays low here so mmi_dc_audio_set_channel_status() can raise it once
	 * the channel status words are in place.
	 */
	val = GENMASK(MMI_DC_AUDIO_CH_MAX - 1, 0) &
	      ~(u32)GENMASK(num_audio_channels - 1, 0);
	dc_write_misc(dc, MMI_DC_AUD_CHANNELS_SELECT, val);
}

/**
 * mmi_dc_audio_set_channel_status - Program IEC 60958 channel-status words
 * @dc: MMI DC device
 * @sample_rate: audio sample rate in Hz
 * @num_audio_channels: active audio channel count (1..8)
 */
static void mmi_dc_audio_set_channel_status(struct mmi_dc *dc, u32 sample_rate,
					    u32 num_audio_channels)
{
	const struct mmi_dc_audio_rate *entry =
		mmi_dc_audio_find_rate(sample_rate);
	u32 fs_lo, fs_hi, reg4, reg5, val;
	unsigned int pair;

	if (!entry)
		return;

	/*
	 * fs_hi -> REG5[26:23], fs_lo -> REG4[6:3]. The original sampling
	 * frequency nibble is the one's complement of the sampling frequency
	 * nibble, which is how IEC 60958-3 defines it.
	 */
	fs_hi = entry->fs_nibble;
	fs_lo = ~fs_hi & 0xF;

	reg4 = MMI_DC_AUD_CH_STATUS_REG4_VALID |
	       (fs_lo << MMI_DC_AUD_CH_STATUS_REG4_FS_SHIFT);
	reg5 = (fs_hi << MMI_DC_AUD_CH_STATUS_REG5_FS_SHIFT) |
	       (num_audio_channels << MMI_DC_AUD_CH_STATUS_REG5_CH_COUNT0_SHIFT) |
	       (num_audio_channels << MMI_DC_AUD_CH_STATUS_REG5_CH_COUNT1_SHIFT);

	for (pair = 0; pair < MMI_DC_AUD_CH_STATUS_PAIRS; pair++) {
		unsigned int base = pair * MMI_DC_AUD_CH_STATUS_REGS_PER_PAIR;

		dc_write_misc(dc, MMI_DC_AUD_CH_STATUS(base + 0), 0);
		dc_write_misc(dc, MMI_DC_AUD_CH_STATUS(base + 1), 0);
		dc_write_misc(dc, MMI_DC_AUD_CH_STATUS(base + 2), 0);
		dc_write_misc(dc, MMI_DC_AUD_CH_STATUS(base + 3), 0);
		dc_write_misc(dc, MMI_DC_AUD_CH_STATUS(base + 4), reg4);
		dc_write_misc(dc, MMI_DC_AUD_CH_STATUS(base + 5), reg5);
	}

	/* Latch the words written above. */
	val = dc_read_misc(dc, MMI_DC_AUD_CHANNELS_SELECT);
	dc_write_misc(dc, MMI_DC_AUD_CHANNELS_SELECT,
		      val | MMI_DC_AUD_CHANNELS_SELECT_CH_STATUS);
}

/*
 * The buffer geometry comes from @params rather than from substream->runtime,
 * because snd_pcm_hw_params() only copies the period and buffer sizes into the
 * runtime after the DAI hw_params() callback has returned.
 */
static int mmi_dc_audio_configure_dma(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      u32 num_audio_channels)
{
	size_t period_bytes = params_period_bytes(params);
	struct mmi_dcdma_peripheral_config pconfig = {
		.video_group = false,
		.num_audio_channels = num_audio_channels,
		.audio_src_vaddr = substream->runtime->dma_area,
		.audio_buffer_bytes = period_bytes * params_periods(params),
		.audio_period_bytes = period_bytes,
	};
	struct dma_slave_config slave_config = { 0 };
	struct dma_chan *dma_chan = snd_dmaengine_pcm_get_chan(substream);

	if (!dma_chan)
		return 0;

	slave_config.direction = DMA_MEM_TO_DEV;
	slave_config.peripheral_config = &pconfig;
	slave_config.peripheral_size = sizeof(pconfig);

	return dmaengine_slave_config(dma_chan, &slave_config);
}

/*
 * Drop the channel count and the pointer to the managed PCM buffer, which
 * hw_free() is about to let go of, and release the expansion buffer sized
 * against it. Without this the DMA driver would keep a pointer to dma_area
 * until the channel is released at close().
 */
static void mmi_dc_audio_release_dma(struct snd_pcm_substream *substream)
{
	struct mmi_dcdma_peripheral_config pconfig = {
		.video_group = false,
	};
	struct dma_slave_config slave_config = { 0 };
	struct dma_chan *dma_chan = snd_dmaengine_pcm_get_chan(substream);

	if (!dma_chan)
		return;

	slave_config.direction = DMA_MEM_TO_DEV;
	slave_config.peripheral_config = &pconfig;
	slave_config.peripheral_size = sizeof(pconfig);

	dmaengine_slave_config(dma_chan, &slave_config);
}

static int dc_dai_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *socdai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct mmi_audio *audio;
	struct mmi_dc *dc;
	u32 num_audio_channels;
	u32 sample_rate;
	u32 val;
	int ret;

	dc = snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	audio = dc->audio;
	if (!audio)
		return -ENODEV;

	sample_rate = params_rate(params);
	num_audio_channels = params_channels(params);

	if (!mmi_dc_audio_channels_supported(num_audio_channels)) {
		dev_err(dc->dev, "unsupported channel count: %u\n",
			num_audio_channels);
		return -EINVAL;
	}

	if (!mmi_dc_audio_rate_supported(dc, sample_rate)) {
		dev_err(dc->dev, "unsupported sample rate: %u\n", sample_rate);
		return -EINVAL;
	}

	guard(mutex)(&audio->enable_lock);

	ret = mmi_dc_audio_setup_clk(dc, sample_rate);
	if (ret)
		return ret;

	audio->current_rate = sample_rate;

	/*
	 * Push to DPTX only after the clock is up. A rate the provider cannot
	 * deliver would otherwise leave DPTX programmed for a stream that never
	 * starts, with nothing to put the previous channel count back.
	 *
	 * -ENODEV just means no DPTX is bound; audio on other sinks still works.
	 */
	ret = mmi_dp_set_audio_clk_src(mmi_dc_get_aud_clk_src(dc));
	if (ret && ret != -ENODEV)
		dev_warn(dc->dev, "failed to push audio clock source to dptx: %d\n",
			 ret);

	ret = mmi_dp_set_audio_channels(num_audio_channels);
	if (ret && ret != -ENODEV)
		dev_warn(dc->dev, "failed to push audio channels to dptx: %d\n", ret);

	mmi_dc_audio_configure_hw(dc, num_audio_channels);
	mmi_dc_audio_set_channel_status(dc, sample_rate, num_audio_channels);

	/* Set AVBUF output to select memory audio stream */
	val = dc_read_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT);
	val &= ~(MMI_DC_AVBUF_OUTPUT_AUDSTREAM1_SEL_MASK);
	val |= FIELD_PREP(MMI_DC_AVBUF_OUTPUT_AUDSTREAM1_SEL_MASK,
			  MMI_DC_AVBUF_AUDSTRM_SEL_MEM);
	dc_write_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT, val);

	/* Configure DMA with channel count for audio remapping. */
	ret = mmi_dc_audio_configure_dma(substream, params, num_audio_channels);
	if (ret) {
		dev_err(dc->dev, "failed to configure DMA: %d\n", ret);
		return ret;
	}

	return 0;
}

static int dc_dai_hw_free(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *socdai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct mmi_dc *dc =
		snd_soc_dai_get_drvdata(snd_soc_rtd_to_cpu(rtd, 0));
	struct mmi_audio *audio = dc->audio;
	u32 val;

	if (!audio)
		return 0;

	/*
	 * Safe here rather than at stop: ALSA only reaches hw_free() with the
	 * stream out of the running state, and mmi_dcdma_synchronize() has
	 * already fenced out the shadow buffer refresh.
	 */
	mmi_dc_audio_release_dma(substream);

	guard(mutex)(&audio->enable_lock);

	/* Nothing was configured, so there is no clock or PM reference held. */
	if (!audio->current_rate)
		return 0;

	audio->current_rate = 0;

	val = dc_read_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT);
	val &= ~MMI_DC_AVBUF_OUTPUT_AUDSTREAM1_SEL_MASK;
	val |= FIELD_PREP(MMI_DC_AVBUF_OUTPUT_AUDSTREAM1_SEL_MASK,
			  MMI_DC_AVBUF_AUDSTRM_SEL_NONE);
	dc_write_avbuf(dc, MMI_DC_AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT, val);

	pm_runtime_put(dc->dev);
	mmi_dc_audio_disable_clk(dc);

	return 0;
}

static const struct snd_soc_dai_ops mmi_dc_dai_ops = {
	.hw_params	= dc_dai_hw_params,
	.hw_free	= dc_dai_hw_free,
};

/**
 * mmi_dc_audio_set_clk_src - Switch the audio clock source
 * @dc: MMI DC device
 * @audclksrc: requested audio clock source
 *
 * dc_dai_hw_params() enables the clock that is selected when it runs and
 * dc_dai_hw_free() disables whichever one is selected later, so the source
 * cannot move while a stream holds it. Refreshes the advertised rates, since
 * the two clocks do not reach the same set.
 *
 * Return: 0 on success, -EBUSY if a stream is configured, or a negative error
 * code from mmi_dc_set_aud_clk_src().
 */
int mmi_dc_audio_set_clk_src(struct mmi_dc *dc, enum mmi_dc_aud_clk_src audclksrc)
{
	struct mmi_audio *audio = dc->audio;
	int ret;

	if (!audio)
		return mmi_dc_set_aud_clk_src(dc, audclksrc);

	guard(mutex)(&audio->enable_lock);

	if (audio->current_rate)
		return -EBUSY;

	ret = mmi_dc_set_aud_clk_src(dc, audclksrc);
	if (ret)
		return ret;

	audio->dai_driver.playback.rates = mmi_dc_audio_rate_mask(dc);

	return 0;
}

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

	if (!mmi_dc_get_aud_clk(dc))
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

	if (!audio->dai_name || !audio->link_name || !audio->pcm_name) {
		ret = -ENOMEM;
		goto err_disable;
	}

	/* Create CPU DAI */

	audio->dai_driver = (struct snd_soc_dai_driver) {
		.name		= audio->dai_name,
		.ops		= &mmi_dc_dai_ops,
		.playback	= {
			.channels_min	= MMI_DC_AUDIO_CH_MIN,
			.channels_max	= MMI_DC_AUDIO_CH_MAX,
			.rates		= mmi_dc_audio_rate_mask(dc),
			.formats	= SNDRV_PCM_FMTBIT_S16_LE,
		},
	};

	ret = devm_snd_soc_register_component(dev, &mmi_dc_component_driver,
					      &audio->dai_driver, 1);
	if (ret) {
		dev_err(dev, "failed to register CPU DAI\n");
		goto err_disable;
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
		dev_err(dev, "failed to register PCM\n");
		goto err_disable;
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
		dev_err(dev,
			"failed to register sound card, disabling audio support\n");
		goto err_disable;
	}

	return 0;

	/*
	 * mmi_dc_init() only warns when this fails, so probe carries on with
	 * audio disabled. Clear dc->audio on every failure path, since it is
	 * what the audio entry points test before touching anything.
	 */
err_disable:
	dc->audio = NULL;
	return ret;
}

void mmi_dc_audio_uninit(struct mmi_dc *dc)
{
	struct mmi_audio *audio = dc->audio;

	if (!audio)
		return;

	mutex_destroy(&audio->enable_lock);
}
