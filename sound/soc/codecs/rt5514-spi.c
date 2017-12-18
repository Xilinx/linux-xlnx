/*
 * rt5514-spi.c  --  RT5514 SPI driver
 *
 * Copyright 2015 Realtek Semiconductor Corp.
 * Author: Oder Chiou <oder_chiou@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_qos.h>
#include <linux/sysfs.h>
#include <linux/clk.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rt5514-spi.h"

static struct spi_device *rt5514_spi;

struct rt5514_dsp {
	struct device *dev;
	struct delayed_work copy_work;
	struct mutex dma_lock;
	struct snd_pcm_substream *substream;
	unsigned int buf_base, buf_limit, buf_rp;
	size_t buf_size;
	size_t dma_offset;
	size_t dsp_offset;
};

static const struct snd_pcm_hardware rt5514_spi_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.period_bytes_min	= PAGE_SIZE,
	.period_bytes_max	= 0x20000 / 8,
	.periods_min		= 8,
	.periods_max		= 8,
	.channels_min		= 1,
	.channels_max		= 1,
	.buffer_bytes_max	= 0x20000,
};

static struct snd_soc_dai_driver rt5514_spi_dai = {
	.name = "rt5514-dsp-cpu-dai",
	.id = 0,
	.capture = {
		.stream_name = "DSP Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_16000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};

static void rt5514_spi_copy_work(struct work_struct *work)
{
	struct rt5514_dsp *rt5514_dsp =
		container_of(work, struct rt5514_dsp, copy_work.work);
	struct snd_pcm_runtime *runtime;
	size_t period_bytes, truncated_bytes = 0;

	mutex_lock(&rt5514_dsp->dma_lock);
	if (!rt5514_dsp->substream) {
		dev_err(rt5514_dsp->dev, "No pcm substream\n");
		goto done;
	}

	runtime = rt5514_dsp->substream->runtime;
	period_bytes = snd_pcm_lib_period_bytes(rt5514_dsp->substream);

	if (rt5514_dsp->buf_size - rt5514_dsp->dsp_offset <  period_bytes)
		period_bytes = rt5514_dsp->buf_size - rt5514_dsp->dsp_offset;

	if (rt5514_dsp->buf_rp + period_bytes <= rt5514_dsp->buf_limit) {
		rt5514_spi_burst_read(rt5514_dsp->buf_rp,
			runtime->dma_area + rt5514_dsp->dma_offset,
			period_bytes);

		if (rt5514_dsp->buf_rp + period_bytes == rt5514_dsp->buf_limit)
			rt5514_dsp->buf_rp = rt5514_dsp->buf_base;
		else
			rt5514_dsp->buf_rp += period_bytes;
	} else {
		truncated_bytes = rt5514_dsp->buf_limit - rt5514_dsp->buf_rp;
		rt5514_spi_burst_read(rt5514_dsp->buf_rp,
			runtime->dma_area + rt5514_dsp->dma_offset,
			truncated_bytes);

		rt5514_spi_burst_read(rt5514_dsp->buf_base,
			runtime->dma_area + rt5514_dsp->dma_offset +
			truncated_bytes, period_bytes - truncated_bytes);

			rt5514_dsp->buf_rp = rt5514_dsp->buf_base +
				period_bytes - truncated_bytes;
	}

	rt5514_dsp->dma_offset += period_bytes;
	if (rt5514_dsp->dma_offset >= runtime->dma_bytes)
		rt5514_dsp->dma_offset = 0;

	rt5514_dsp->dsp_offset += period_bytes;

	snd_pcm_period_elapsed(rt5514_dsp->substream);

	if (rt5514_dsp->dsp_offset < rt5514_dsp->buf_size)
		schedule_delayed_work(&rt5514_dsp->copy_work, 5);
done:
	mutex_unlock(&rt5514_dsp->dma_lock);
}

/* PCM for streaming audio from the DSP buffer */
static int rt5514_spi_pcm_open(struct snd_pcm_substream *substream)
{
	snd_soc_set_runtime_hwparams(substream, &rt5514_spi_pcm_hardware);

	return 0;
}

static int rt5514_spi_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *hw_params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rt5514_dsp *rt5514_dsp =
			snd_soc_platform_get_drvdata(rtd->platform);
	int ret;

	mutex_lock(&rt5514_dsp->dma_lock);
	ret = snd_pcm_lib_alloc_vmalloc_buffer(substream,
			params_buffer_bytes(hw_params));
	rt5514_dsp->substream = substream;
	mutex_unlock(&rt5514_dsp->dma_lock);

	return ret;
}

static int rt5514_spi_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rt5514_dsp *rt5514_dsp =
			snd_soc_platform_get_drvdata(rtd->platform);

	mutex_lock(&rt5514_dsp->dma_lock);
	rt5514_dsp->substream = NULL;
	mutex_unlock(&rt5514_dsp->dma_lock);

	cancel_delayed_work_sync(&rt5514_dsp->copy_work);

	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int rt5514_spi_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rt5514_dsp *rt5514_dsp =
			snd_soc_platform_get_drvdata(rtd->platform);
	u8 buf[8];

	rt5514_dsp->dma_offset = 0;
	rt5514_dsp->dsp_offset = 0;

	/**
	 * The address area x1800XXXX is the register address, and it cannot
	 * support spi burst read perfectly. So we use the spi burst read
	 * individually to make sure the data correctly.
	*/
	rt5514_spi_burst_read(RT5514_BUFFER_VOICE_BASE, (u8 *)&buf,
		sizeof(buf));
	rt5514_dsp->buf_base = buf[0] | buf[1] << 8 | buf[2] << 16 |
				buf[3] << 24;

	rt5514_spi_burst_read(RT5514_BUFFER_VOICE_LIMIT, (u8 *)&buf,
		sizeof(buf));
	rt5514_dsp->buf_limit = buf[0] | buf[1] << 8 | buf[2] << 16 |
				buf[3] << 24;

	rt5514_spi_burst_read(RT5514_BUFFER_VOICE_RP, (u8 *)&buf,
		sizeof(buf));
	rt5514_dsp->buf_rp = buf[0] | buf[1] << 8 | buf[2] << 16 |
				buf[3] << 24;

	rt5514_spi_burst_read(RT5514_BUFFER_VOICE_SIZE, (u8 *)&buf,
		sizeof(buf));
	rt5514_dsp->buf_size = buf[0] | buf[1] << 8 | buf[2] << 16 |
				buf[3] << 24;

	return 0;
}

static int rt5514_spi_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rt5514_dsp *rt5514_dsp =
			snd_soc_platform_get_drvdata(rtd->platform);

	if (cmd == SNDRV_PCM_TRIGGER_START) {
		if (rt5514_dsp->buf_base && rt5514_dsp->buf_limit &&
			rt5514_dsp->buf_rp && rt5514_dsp->buf_size)
			schedule_delayed_work(&rt5514_dsp->copy_work, 0);
	}

	return 0;
}

static snd_pcm_uframes_t rt5514_spi_pcm_pointer(
		struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rt5514_dsp *rt5514_dsp =
		snd_soc_platform_get_drvdata(rtd->platform);

	return bytes_to_frames(runtime, rt5514_dsp->dma_offset);
}

static const struct snd_pcm_ops rt5514_spi_pcm_ops = {
	.open		= rt5514_spi_pcm_open,
	.hw_params	= rt5514_spi_hw_params,
	.hw_free	= rt5514_spi_hw_free,
	.trigger	= rt5514_spi_trigger,
	.prepare	= rt5514_spi_prepare,
	.pointer	= rt5514_spi_pcm_pointer,
	.mmap		= snd_pcm_lib_mmap_vmalloc,
	.page		= snd_pcm_lib_get_vmalloc_page,
};

static int rt5514_spi_pcm_probe(struct snd_soc_platform *platform)
{
	struct rt5514_dsp *rt5514_dsp;

	rt5514_dsp = devm_kzalloc(platform->dev, sizeof(*rt5514_dsp),
			GFP_KERNEL);

	rt5514_dsp->dev = &rt5514_spi->dev;
	mutex_init(&rt5514_dsp->dma_lock);
	INIT_DELAYED_WORK(&rt5514_dsp->copy_work, rt5514_spi_copy_work);
	snd_soc_platform_set_drvdata(platform, rt5514_dsp);

	return 0;
}

static struct snd_soc_platform_driver rt5514_spi_platform = {
	.probe = rt5514_spi_pcm_probe,
	.ops = &rt5514_spi_pcm_ops,
};

static const struct snd_soc_component_driver rt5514_spi_dai_component = {
	.name		= "rt5514-spi-dai",
};

/**
 * rt5514_spi_burst_read - Read data from SPI by rt5514 address.
 * @addr: Start address.
 * @rxbuf: Data Buffer for reading.
 * @len: Data length, it must be a multiple of 8.
 *
 *
 * Returns true for success.
 */
int rt5514_spi_burst_read(unsigned int addr, u8 *rxbuf, size_t len)
{
	u8 spi_cmd = RT5514_SPI_CMD_BURST_READ;
	int status;
	u8 write_buf[8];
	unsigned int i, end, offset = 0;

	struct spi_message message;
	struct spi_transfer x[3];

	while (offset < len) {
		if (offset + RT5514_SPI_BUF_LEN <= len)
			end = RT5514_SPI_BUF_LEN;
		else
			end = len % RT5514_SPI_BUF_LEN;

		write_buf[0] = spi_cmd;
		write_buf[1] = ((addr + offset) & 0xff000000) >> 24;
		write_buf[2] = ((addr + offset) & 0x00ff0000) >> 16;
		write_buf[3] = ((addr + offset) & 0x0000ff00) >> 8;
		write_buf[4] = ((addr + offset) & 0x000000ff) >> 0;

		spi_message_init(&message);
		memset(x, 0, sizeof(x));

		x[0].len = 5;
		x[0].tx_buf = write_buf;
		spi_message_add_tail(&x[0], &message);

		x[1].len = 4;
		x[1].tx_buf = write_buf;
		spi_message_add_tail(&x[1], &message);

		x[2].len = end;
		x[2].rx_buf = rxbuf + offset;
		spi_message_add_tail(&x[2], &message);

		status = spi_sync(rt5514_spi, &message);

		if (status)
			return false;

		offset += RT5514_SPI_BUF_LEN;
	}

	for (i = 0; i < len; i += 8) {
		write_buf[0] = rxbuf[i + 0];
		write_buf[1] = rxbuf[i + 1];
		write_buf[2] = rxbuf[i + 2];
		write_buf[3] = rxbuf[i + 3];
		write_buf[4] = rxbuf[i + 4];
		write_buf[5] = rxbuf[i + 5];
		write_buf[6] = rxbuf[i + 6];
		write_buf[7] = rxbuf[i + 7];

		rxbuf[i + 0] = write_buf[7];
		rxbuf[i + 1] = write_buf[6];
		rxbuf[i + 2] = write_buf[5];
		rxbuf[i + 3] = write_buf[4];
		rxbuf[i + 4] = write_buf[3];
		rxbuf[i + 5] = write_buf[2];
		rxbuf[i + 6] = write_buf[1];
		rxbuf[i + 7] = write_buf[0];
	}

	return true;
}

/**
 * rt5514_spi_burst_write - Write data to SPI by rt5514 address.
 * @addr: Start address.
 * @txbuf: Data Buffer for writng.
 * @len: Data length, it must be a multiple of 8.
 *
 *
 * Returns true for success.
 */
int rt5514_spi_burst_write(u32 addr, const u8 *txbuf, size_t len)
{
	u8 spi_cmd = RT5514_SPI_CMD_BURST_WRITE;
	u8 *write_buf;
	unsigned int i, end, offset = 0;

	write_buf = kmalloc(RT5514_SPI_BUF_LEN + 6, GFP_KERNEL);

	if (write_buf == NULL)
		return -ENOMEM;

	while (offset < len) {
		if (offset + RT5514_SPI_BUF_LEN <= len)
			end = RT5514_SPI_BUF_LEN;
		else
			end = len % RT5514_SPI_BUF_LEN;

		write_buf[0] = spi_cmd;
		write_buf[1] = ((addr + offset) & 0xff000000) >> 24;
		write_buf[2] = ((addr + offset) & 0x00ff0000) >> 16;
		write_buf[3] = ((addr + offset) & 0x0000ff00) >> 8;
		write_buf[4] = ((addr + offset) & 0x000000ff) >> 0;

		for (i = 0; i < end; i += 8) {
			write_buf[i + 12] = txbuf[offset + i + 0];
			write_buf[i + 11] = txbuf[offset + i + 1];
			write_buf[i + 10] = txbuf[offset + i + 2];
			write_buf[i +  9] = txbuf[offset + i + 3];
			write_buf[i +  8] = txbuf[offset + i + 4];
			write_buf[i +  7] = txbuf[offset + i + 5];
			write_buf[i +  6] = txbuf[offset + i + 6];
			write_buf[i +  5] = txbuf[offset + i + 7];
		}

		write_buf[end + 5] = spi_cmd;

		spi_write(rt5514_spi, write_buf, end + 6);

		offset += RT5514_SPI_BUF_LEN;
	}

	kfree(write_buf);

	return 0;
}
EXPORT_SYMBOL_GPL(rt5514_spi_burst_write);

static int rt5514_spi_probe(struct spi_device *spi)
{
	int ret;

	rt5514_spi = spi;

	ret = devm_snd_soc_register_platform(&spi->dev, &rt5514_spi_platform);
	if (ret < 0) {
		dev_err(&spi->dev, "Failed to register platform.\n");
		return ret;
	}

	ret = devm_snd_soc_register_component(&spi->dev,
					      &rt5514_spi_dai_component,
					      &rt5514_spi_dai, 1);
	if (ret < 0) {
		dev_err(&spi->dev, "Failed to register component.\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id rt5514_of_match[] = {
	{ .compatible = "realtek,rt5514", },
	{},
};
MODULE_DEVICE_TABLE(of, rt5514_of_match);

static struct spi_driver rt5514_spi_driver = {
	.driver = {
		.name = "rt5514",
		.of_match_table = of_match_ptr(rt5514_of_match),
	},
	.probe = rt5514_spi_probe,
};
module_spi_driver(rt5514_spi_driver);

MODULE_DESCRIPTION("RT5514 SPI driver");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
