// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ASoC audio formatter support
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "xlnx_snd_common.h"

#define DRV_NAME "xlnx_formatter_pcm"

#define XLNX_S2MM_OFFSET	0
#define XLNX_MM2S_OFFSET	0x100

#define XLNX_AUD_CORE_CONFIG	0x4
#define XLNX_AUD_CTRL		0x10
#define XLNX_AUD_STS		0x14

#define AUD_CTRL_RESET_MASK	BIT(1)
#define AUD_CFG_MM2S_MASK	BIT(15)
#define AUD_CFG_S2MM_MASK	BIT(31)

#define XLNX_AUD_FS_MULTIPLIER	0x18
#define XLNX_AUD_PERIOD_CONFIG	0x1C
#define XLNX_AUD_BUFF_ADDR_LSB	0x20
#define XLNX_AUD_BUFF_ADDR_MSB	0x24
#define XLNX_AUD_XFER_COUNT	0x28
#define XLNX_AUD_CH_STS_START	0x2C
#define XLNX_BYTES_PER_CH	0x44

#define AUD_STS_IOC_IRQ_MASK	BIT(31)
#define AUD_STS_CH_STS_MASK	BIT(29)
#define AUD_CTRL_IOC_IRQ_MASK	BIT(13)
#define AUD_CTRL_TOUT_IRQ_MASK	BIT(14)
#define AUD_CTRL_DMA_EN_MASK	BIT(0)

#define CFG_MM2S_CH_MASK	GENMASK(11, 8)
#define CFG_MM2S_CH_SHIFT	8
#define CFG_MM2S_XFER_MASK	GENMASK(14, 13)
#define CFG_MM2S_XFER_SHIFT	13
#define CFG_MM2S_PKG_MASK	BIT(12)

#define CFG_S2MM_CH_MASK	GENMASK(27, 24)
#define CFG_S2MM_CH_SHIFT	24
#define CFG_S2MM_XFER_MASK	GENMASK(30, 29)
#define CFG_S2MM_XFER_SHIFT	29
#define CFG_S2MM_PKG_MASK	BIT(28)

#define AUD_CTRL_DATA_WIDTH_SHIFT	16
#define AUD_CTRL_ACTIVE_CH_SHIFT	19
#define PERIOD_CFG_PERIODS_SHIFT	16

#define PERIODS_MIN		2
#define PERIODS_MAX		6
#define PERIOD_BYTES_MIN	192
#define PERIOD_BYTES_MAX	(50 * 1024)

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

static const struct snd_pcm_hardware xlnx_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_BATCH | SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S24_LE,
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_8000_192000,
	.rate_min = 8000,
	.rate_max = 192000,
	.buffer_bytes_max = PERIODS_MAX * PERIOD_BYTES_MAX,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = PERIOD_BYTES_MAX,
	.periods_min = PERIODS_MIN,
	.periods_max = PERIODS_MAX,
};

struct xlnx_pcm_drv_data {
	void __iomem *mmio;
	bool s2mm_presence;
	bool mm2s_presence;
	int s2mm_irq;
	int mm2s_irq;
	struct snd_pcm_substream *play_stream;
	struct snd_pcm_substream *capture_stream;
	struct platform_device *pdev;
	struct device_node *nodes[XLNX_MAX_PATHS];
	struct clk *axi_clk;
};

/*
 * struct xlnx_pcm_stream_param - stream configuration
 * @mmio: base address offset
 * @interleaved: audio channels arrangement in buffer
 * @xfer_mode: data formatting mode during transfer
 * @ch_limit: Maximum channels supported
 * @buffer_size: stream ring buffer size
 */
struct xlnx_pcm_stream_param {
	void __iomem *mmio;
	bool interleaved;
	u32 xfer_mode;
	u32 ch_limit;
	u64 buffer_size;
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

enum bit_depth {
	BIT_DEPTH_8,
	BIT_DEPTH_16,
	BIT_DEPTH_20,
	BIT_DEPTH_24,
	BIT_DEPTH_32,
};

enum {
	AES_TO_AES,
	AES_TO_PCM,
	PCM_TO_PCM,
	PCM_TO_AES
};

static int parse_professional_format(u32 chsts_reg1_val, u32 chsts_reg2_val,
				     struct audio_params *params)
{
	u32 padded, val;

	val = (chsts_reg1_val & PROF_SAMPLERATE_MASK) >> PROF_SAMPLERATE_SHIFT;
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
		return -EINVAL;
	}

	val = (chsts_reg1_val & PROF_CHANNEL_COUNT_MASK) >>
	       PROF_CHANNEL_COUNT_SHIFT;
	switch (val) {
	case PROF_CHANNELS_UNDEFINED:
	case PROF_STEREO_CHANNELS:
	case PROF_TWO_CHANNELS:
		params->channels = 2;
		break;
	default:
		/* TODO: handle more channels in future*/
		return -EINVAL;
	}

	val = (chsts_reg1_val & PROF_MAX_BITDEPTH_MASK) >>
	       PROF_MAX_BITDEPTH_SHIFT;
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
		return -EINVAL;
	}

	val = (chsts_reg1_val & PROF_BITDEPTH_MASK) >> PROF_BITDEPTH_SHIFT;
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
		return -EINVAL;
	}

	return 0;
}

static int parse_consumer_format(u32 chsts_reg1_val, u32 chsts_reg2_val,
				 struct audio_params *params)
{
	u32 padded, val;

	val = (chsts_reg1_val & CON_SAMPLE_RATE_MASK) >> CON_SAMPLE_RATE_SHIFT;
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
		return -EINVAL;
	}

	val = (chsts_reg1_val & CON_CHANNEL_COUNT_MASK) >>
	       CON_CHANNEL_COUNT_SHIFT;
	params->channels = val;

	/*
	 * if incorrect channel count embedded is less than 2, set it to
	 * supported default which is 2 in our case
	 */
	if (params->channels < 2)
		params->channels = 2;

	if (chsts_reg2_val & CON_MAX_BITDEPTH_MASK)
		padded = 4;
	else
		padded = 0;

	val = (chsts_reg2_val & CON_BITDEPTH_MASK) >> CON_BITDEPTH_SHIFT;
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
		return -EINVAL;
	}

	return 0;
}

static int xlnx_formatter_pcm_reset(void __iomem *mmio_base)
{
	u32 val, retries = 0;

	val = readl(mmio_base + XLNX_AUD_CTRL);
	val |= AUD_CTRL_RESET_MASK;
	writel(val, mmio_base + XLNX_AUD_CTRL);

	val = readl(mmio_base + XLNX_AUD_CTRL);
	/* Poll for maximum timeout of approximately 100ms (1 * 100)*/
	while ((val & AUD_CTRL_RESET_MASK) && (retries < 100)) {
		mdelay(1);
		retries++;
		val = readl(mmio_base + XLNX_AUD_CTRL);
	}
	if (val & AUD_CTRL_RESET_MASK)
		return -ENODEV;

	return 0;
}

static void xlnx_formatter_disable_irqs(void __iomem *mmio_base, int stream)
{
	u32 val;

	val = readl(mmio_base + XLNX_AUD_CTRL);
	val &= ~AUD_CTRL_IOC_IRQ_MASK;
	if (stream == SNDRV_PCM_STREAM_CAPTURE)
		val &= ~AUD_CTRL_TOUT_IRQ_MASK;

	writel(val, mmio_base + XLNX_AUD_CTRL);
}

static irqreturn_t xlnx_mm2s_irq_handler(int irq, void *arg)
{
	u32 val;
	void __iomem *reg;
	struct device *dev = arg;
	struct xlnx_pcm_drv_data *adata = dev_get_drvdata(dev);

	reg = adata->mmio + XLNX_MM2S_OFFSET + XLNX_AUD_STS;
	val = readl(reg);
	if (val & AUD_STS_IOC_IRQ_MASK) {
		writel(val & AUD_STS_IOC_IRQ_MASK, reg);
		if (adata->play_stream)
			snd_pcm_period_elapsed(adata->play_stream);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t xlnx_s2mm_irq_handler(int irq, void *arg)
{
	u32 val;
	void __iomem *reg;
	struct device *dev = arg;
	struct xlnx_pcm_drv_data *adata = dev_get_drvdata(dev);

	reg = adata->mmio + XLNX_S2MM_OFFSET + XLNX_AUD_STS;
	val = readl(reg);
	if (val & AUD_STS_IOC_IRQ_MASK) {
		writel(val & AUD_STS_IOC_IRQ_MASK, reg);
		if (adata->capture_stream)
			snd_pcm_period_elapsed(adata->capture_stream);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int xlnx_formatter_pcm_open(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	int err;
	u32 val, data_format_mode;
	u32 ch_count_mask, ch_count_shift, data_xfer_mode, data_xfer_shift;
	struct xlnx_pcm_stream_param *stream_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct xlnx_pcm_drv_data *adata = dev_get_drvdata(component->dev);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    !adata->mm2s_presence)
		return -ENODEV;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE &&
		 !adata->s2mm_presence)
		return -ENODEV;

	stream_data = kzalloc(sizeof(*stream_data), GFP_KERNEL);
	if (!stream_data)
		return -ENOMEM;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ch_count_mask = CFG_MM2S_CH_MASK;
		ch_count_shift = CFG_MM2S_CH_SHIFT;
		data_xfer_mode = CFG_MM2S_XFER_MASK;
		data_xfer_shift = CFG_MM2S_XFER_SHIFT;
		data_format_mode = CFG_MM2S_PKG_MASK;
		stream_data->mmio = adata->mmio + XLNX_MM2S_OFFSET;
		adata->play_stream = substream;

	} else {
		ch_count_mask = CFG_S2MM_CH_MASK;
		ch_count_shift = CFG_S2MM_CH_SHIFT;
		data_xfer_mode = CFG_S2MM_XFER_MASK;
		data_xfer_shift = CFG_S2MM_XFER_SHIFT;
		data_format_mode = CFG_S2MM_PKG_MASK;
		stream_data->mmio = adata->mmio + XLNX_S2MM_OFFSET;
		adata->capture_stream = substream;
	}

	val = readl(adata->mmio + XLNX_AUD_CORE_CONFIG);

	if (!(val & data_format_mode))
		stream_data->interleaved = true;

	stream_data->xfer_mode = (val & data_xfer_mode) >> data_xfer_shift;
	stream_data->ch_limit = (val & ch_count_mask) >> ch_count_shift;
	dev_info(component->dev,
		 "stream %d : format = %d mode = %d ch_limit = %d\n",
		 substream->stream, stream_data->interleaved,
		 stream_data->xfer_mode, stream_data->ch_limit);

	snd_soc_set_runtime_hwparams(substream, &xlnx_pcm_hardware);
	runtime->private_data = stream_data;

	/* Resize the period size divisible by 64 */
	err = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 64);
	if (err) {
		dev_err(component->dev,
			"unable to set constraint on period bytes\n");
		return err;
	}

	/* enable DMA IOC irq */
	val = readl(stream_data->mmio + XLNX_AUD_CTRL);
	val |= AUD_CTRL_IOC_IRQ_MASK;
	writel(val, stream_data->mmio + XLNX_AUD_CTRL);

	return 0;
}

static int xlnx_formatter_pcm_close(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	int ret;
	struct xlnx_pcm_stream_param *stream_data =
			substream->runtime->private_data;

	ret = xlnx_formatter_pcm_reset(stream_data->mmio);
	if (ret) {
		dev_err(component->dev, "audio formatter reset failed\n");
		goto err_reset;
	}
	xlnx_formatter_disable_irqs(stream_data->mmio, substream->stream);

err_reset:
	kfree(stream_data);
	return 0;
}

static snd_pcm_uframes_t
xlnx_formatter_pcm_pointer(struct snd_soc_component *component,
					struct snd_pcm_substream *substream)
{
	u32 pos;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct xlnx_pcm_stream_param *stream_data = runtime->private_data;

	pos = readl(stream_data->mmio + XLNX_AUD_XFER_COUNT);

	if (pos >= stream_data->buffer_size)
		pos = 0;

	return bytes_to_frames(runtime, pos);
}

static int xlnx_formatter_pcm_hw_params(struct snd_soc_component *component,
					struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	u32 low, high, active_ch, val, bits_per_sample, bytes_per_ch;
	u32 aes_reg1_val, aes_reg2_val, sample_rate;
	int status;
	u64 size;
	struct audio_params *aes_params;
	struct pl_card_data *prv;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct xlnx_pcm_stream_param *stream_data = runtime->private_data;
	struct xlnx_pcm_drv_data *adata = dev_get_drvdata(component->dev);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	aes_params = kzalloc(sizeof(*aes_params), GFP_KERNEL);
	if (!aes_params)
		return -ENOMEM;

	bits_per_sample = params_width(params);
	sample_rate = params_rate(params);
	active_ch = params_channels(params);
	if (active_ch > stream_data->ch_limit)
		return -EINVAL;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE &&
	    stream_data->xfer_mode == AES_TO_PCM &&
	    ((strstr(adata->nodes[XLNX_CAPTURE]->name, "hdmi")) ||
	    (strstr(adata->nodes[XLNX_CAPTURE]->name, "sdi")))) {
		/*
		 * If formatter is in AES_PCM mode for HDMI/SDI capture path,
		 * parse AES header
		 */
		val = readl(stream_data->mmio + XLNX_AUD_STS);
		if (val & AUD_STS_CH_STS_MASK) {
			aes_reg1_val = readl(stream_data->mmio +
					 XLNX_AUD_CH_STS_START);
			aes_reg2_val = readl(stream_data->mmio +
					 XLNX_AUD_CH_STS_START + 0x4);

			if (aes_reg1_val & AES_FORMAT_MASK)
				status = parse_professional_format(aes_reg1_val,
								   aes_reg2_val,
								   aes_params);
			else
				status = parse_consumer_format(aes_reg1_val,
							       aes_reg2_val,
							       aes_params);
			dev_info(component->dev, "rate = %d bit depth = %d ch = %d\n",
				 aes_params->srate, aes_params->sig_bits,
				 aes_params->channels);
			kfree(aes_params);
		}
	}

	size = params_buffer_bytes(params);
	status = snd_pcm_lib_malloc_pages(substream, size);
	if (status < 0)
		return status;

	stream_data->buffer_size = size;

	low = lower_32_bits(substream->dma_buffer.addr);
	high = upper_32_bits(substream->dma_buffer.addr);
	writel(low, stream_data->mmio + XLNX_AUD_BUFF_ADDR_LSB);
	writel(high, stream_data->mmio + XLNX_AUD_BUFF_ADDR_MSB);

	val = readl(stream_data->mmio + XLNX_AUD_CTRL);
	bits_per_sample = params_width(params);
	switch (bits_per_sample) {
	case 8:
		val |= (BIT_DEPTH_8 << AUD_CTRL_DATA_WIDTH_SHIFT);
		break;
	case 16:
		val |= (BIT_DEPTH_16 << AUD_CTRL_DATA_WIDTH_SHIFT);
		break;
	case 20:
		val |= (BIT_DEPTH_20 << AUD_CTRL_DATA_WIDTH_SHIFT);
		break;
	case 24:
		val |= (BIT_DEPTH_24 << AUD_CTRL_DATA_WIDTH_SHIFT);
		break;
	case 32:
		val |= (BIT_DEPTH_32 << AUD_CTRL_DATA_WIDTH_SHIFT);
		break;
	}

	val |= active_ch << AUD_CTRL_ACTIVE_CH_SHIFT;
	writel(val, stream_data->mmio + XLNX_AUD_CTRL);

	val = (params_periods(params) << PERIOD_CFG_PERIODS_SHIFT)
		| params_period_bytes(params);
	writel(val, stream_data->mmio + XLNX_AUD_PERIOD_CONFIG);
	bytes_per_ch = DIV_ROUND_UP(params_period_bytes(params), active_ch);
	writel(bytes_per_ch, stream_data->mmio + XLNX_BYTES_PER_CH);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		prv = snd_soc_card_get_drvdata(rtd->card);
		writel(prv->mclk_ratio,
		       stream_data->mmio + XLNX_AUD_FS_MULTIPLIER);
	}

	return 0;
}

static int xlnx_formatter_pcm_hw_free(struct snd_soc_component *component,
					struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int xlnx_formatter_pcm_trigger(struct snd_soc_component *component,
					struct snd_pcm_substream *substream, int cmd)
{
	u32 val;
	struct xlnx_pcm_stream_param *stream_data =
			substream->runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		val = readl(stream_data->mmio + XLNX_AUD_CTRL);
		val |= AUD_CTRL_DMA_EN_MASK;
		writel(val, stream_data->mmio + XLNX_AUD_CTRL);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		val = readl(stream_data->mmio + XLNX_AUD_CTRL);
		val &= ~AUD_CTRL_DMA_EN_MASK;
		writel(val, stream_data->mmio + XLNX_AUD_CTRL);
		break;
	}

	return 0;
}

static int xlnx_formatter_pcm_new(struct snd_soc_component *component,
					struct snd_soc_pcm_runtime *rtd)
{
	snd_pcm_lib_preallocate_pages_for_all(rtd->pcm,
			SNDRV_DMA_TYPE_DEV, component->dev,
			xlnx_pcm_hardware.buffer_bytes_max,
			xlnx_pcm_hardware.buffer_bytes_max);
	return 0;
}

static struct snd_soc_component_driver xlnx_asoc_component = {
	.name = DRV_NAME,
	.open = xlnx_formatter_pcm_open,
	.close = xlnx_formatter_pcm_close,
	.hw_params = xlnx_formatter_pcm_hw_params,
	.hw_free = xlnx_formatter_pcm_hw_free,
	.trigger = xlnx_formatter_pcm_trigger,
	.pointer = xlnx_formatter_pcm_pointer,
	.pcm_construct = xlnx_formatter_pcm_new,
};

static int xlnx_formatter_pcm_probe(struct platform_device *pdev)
{
	int ret;
	u32 val;
	size_t pdata_size;
	struct xlnx_pcm_drv_data *aud_drv_data;
	struct device *dev = &pdev->dev;

	aud_drv_data = devm_kzalloc(&pdev->dev,
				    sizeof(*aud_drv_data), GFP_KERNEL);
	if (!aud_drv_data)
		return -ENOMEM;

	aud_drv_data->axi_clk = devm_clk_get(&pdev->dev, "s_axi_lite_aclk");
	if (IS_ERR(aud_drv_data->axi_clk)) {
		ret = PTR_ERR(aud_drv_data->axi_clk);
		dev_err(&pdev->dev, "failed to get s_axi_lite_aclk(%d)\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(aud_drv_data->axi_clk);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to enable s_axi_lite_aclk(%d)\n", ret);
		return ret;
	}

	aud_drv_data->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(aud_drv_data->mmio)) {
		dev_err(dev, "audio formatter ioremap failed\n");
		ret = PTR_ERR(aud_drv_data->mmio);
		goto clk_err;
	}

	val = readl(aud_drv_data->mmio + XLNX_AUD_CORE_CONFIG);
	if (val & AUD_CFG_MM2S_MASK) {
		aud_drv_data->mm2s_presence = true;
		aud_drv_data->mm2s_irq = platform_get_irq_byname(pdev,
								 "irq_mm2s");
		if (aud_drv_data->mm2s_irq < 0) {
			ret = aud_drv_data->mm2s_irq;
			goto clk_err;
		}
		ret = devm_request_irq(&pdev->dev, aud_drv_data->mm2s_irq,
				       xlnx_mm2s_irq_handler, 0,
				       "xlnx_formatter_pcm_mm2s_irq",
				       &pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "xlnx audio mm2s irq request failed\n");
			goto clk_err;
		}
		ret = xlnx_formatter_pcm_reset(aud_drv_data->mmio +
					       XLNX_MM2S_OFFSET);
		if (ret) {
			dev_err(&pdev->dev, "audio formatter reset failed\n");
			goto clk_err;
		}
		xlnx_formatter_disable_irqs(aud_drv_data->mmio +
					    XLNX_MM2S_OFFSET,
					    SNDRV_PCM_STREAM_PLAYBACK);

		aud_drv_data->nodes[XLNX_PLAYBACK] =
			of_parse_phandle(dev->of_node, "xlnx,tx", 0);
		if (!aud_drv_data->nodes[XLNX_PLAYBACK])
			dev_err(&pdev->dev, "tx node not found\n");
		else
			dev_info(&pdev->dev,
				 "sound card device will use DAI link: %s\n",
				 (aud_drv_data->nodes[XLNX_PLAYBACK])->name);
		of_node_put(aud_drv_data->nodes[XLNX_PLAYBACK]);
	}
	if (val & AUD_CFG_S2MM_MASK) {
		aud_drv_data->s2mm_presence = true;
		aud_drv_data->s2mm_irq = platform_get_irq_byname(pdev,
								 "irq_s2mm");
		if (aud_drv_data->s2mm_irq < 0) {
			ret = aud_drv_data->s2mm_irq;
			goto clk_err;
		}
		ret = devm_request_irq(&pdev->dev, aud_drv_data->s2mm_irq,
				       xlnx_s2mm_irq_handler, 0,
				       "xlnx_formatter_pcm_s2mm_irq",
				       &pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "xlnx audio s2mm irq request failed\n");
			goto clk_err;
		}
		ret = xlnx_formatter_pcm_reset(aud_drv_data->mmio +
					       XLNX_S2MM_OFFSET);
		if (ret) {
			dev_err(&pdev->dev, "audio formatter reset failed\n");
			goto clk_err;
		}
		xlnx_formatter_disable_irqs(aud_drv_data->mmio +
					    XLNX_S2MM_OFFSET,
					    SNDRV_PCM_STREAM_CAPTURE);

		aud_drv_data->nodes[XLNX_CAPTURE] =
			of_parse_phandle(dev->of_node, "xlnx,rx", 0);
		if (!aud_drv_data->nodes[XLNX_CAPTURE])
			dev_err(&pdev->dev, "rx node not found\n");
		else
			dev_info(&pdev->dev,
				 "sound card device will use DAI link: %s\n",
				 (aud_drv_data->nodes[XLNX_CAPTURE])->name);
		of_node_put(aud_drv_data->nodes[XLNX_CAPTURE]);
	}

	dev_set_drvdata(&pdev->dev, aud_drv_data);

	ret = devm_snd_soc_register_component(&pdev->dev, &xlnx_asoc_component,
					      NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "pcm platform device register failed\n");
		goto clk_err;
	}

	pdata_size = sizeof(aud_drv_data->nodes);
	if (aud_drv_data->nodes[XLNX_PLAYBACK] ||
	    aud_drv_data->nodes[XLNX_CAPTURE])
		aud_drv_data->pdev =
			platform_device_register_resndata(&pdev->dev,
							  "xlnx_snd_card",
							  PLATFORM_DEVID_AUTO,
							  NULL, 0,
							  &aud_drv_data->nodes,
							  pdata_size);
	if (!aud_drv_data->pdev)
		dev_err(&pdev->dev, "sound card device creation failed\n");

	dev_info(&pdev->dev, "pcm platform device registered\n");
	return 0;

clk_err:
	clk_disable_unprepare(aud_drv_data->axi_clk);
	return ret;
}

static int xlnx_formatter_pcm_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct xlnx_pcm_drv_data *adata = dev_get_drvdata(&pdev->dev);

	platform_device_unregister(adata->pdev);

	if (adata->s2mm_presence)
		ret = xlnx_formatter_pcm_reset(adata->mmio + XLNX_S2MM_OFFSET);

	/* Try MM2S reset, even if S2MM  reset fails */
	if (adata->mm2s_presence)
		ret = xlnx_formatter_pcm_reset(adata->mmio + XLNX_MM2S_OFFSET);

	if (ret)
		dev_err(&pdev->dev, "audio formatter reset failed\n");

	clk_disable_unprepare(adata->axi_clk);
	return ret;
}

static const struct of_device_id xlnx_formatter_pcm_of_match[] = {
	{ .compatible = "xlnx,audio-formatter-1.0"},
	{},
};
MODULE_DEVICE_TABLE(of, xlnx_formatter_pcm_of_match);

static struct platform_driver xlnx_formatter_pcm_driver = {
	.probe	= xlnx_formatter_pcm_probe,
	.remove	= xlnx_formatter_pcm_remove,
	.driver	= {
		.name	= DRV_NAME,
		.of_match_table	= xlnx_formatter_pcm_of_match,
	},
};

module_platform_driver(xlnx_formatter_pcm_driver);
MODULE_AUTHOR("Maruthi Srinivas Bayyavarapu");
MODULE_LICENSE("GPL v2");
