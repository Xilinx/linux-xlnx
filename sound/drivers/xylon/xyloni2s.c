/*
 * ALSA driver for logiI2S FPGA IP core
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
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

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "logii2s.h"

#ifdef DEBUG
#define XYLONI2S_DBG(f, x...) \
	do { \
		pr_info("%s: " f, __func__,## x); \
	} while (0)
#else
#define XYLONI2S_DBG(format, ...)
#endif

#define DRIVER_NAME			"logii2s"
#define LOGII2S_DRIVER_DESCRIPTION	"Xylon logiI2S driver"
#define LOGII2S_DRIVER_VERSION		"1.0"

#define PERIOD_BYTES_MIN	(1 * 4)
#define PERIODS_MIN		32
#define PERIODS_MAX		64
#define BUFFER_SIZE		(PERIODS_MIN * LOGII2S_FIFO_SIZE_MAX * 4)
#define MAX_BUFFER_SIZE		(PERIODS_MAX * LOGII2S_FIFO_SIZE_MAX * 4)

/*
 * logiI2S private parameter structure
 */
struct logii2s_data {
	struct platform_device *pdev;
	struct logii2s_port *port[LOGII2S_MAX_INST];
	dma_addr_t pbase;
	void __iomem *base;
	u32 core_clock_freq;
	unsigned int instances;
	int irq;
};

struct logii2s_pcm_data {
	struct logii2s_port *port;
	struct snd_pcm_substream *substream;
	spinlock_t lock;
	unsigned int buf_pos;
	unsigned int buf_sz;
	unsigned int xfer_dir;
};

static void xylon_i2s_handle_irq(struct logii2s_port *port)
{
	struct logii2s_pcm_data *pcm = port->private;
	struct snd_pcm_substream *substream = pcm->substream;
	struct snd_pcm_runtime *runtime;
	u32 *buf;
	unsigned int size = 0;

	if (substream) {
		runtime = substream->runtime;
		if (runtime && runtime->dma_area) {
			if ((pcm->buf_pos + (port->fifo_size * 4)) >=
			    pcm->buf_sz)
				size = (pcm->buf_sz - pcm->buf_pos) / 4;

			buf = (u32 *)(runtime->dma_area + pcm->buf_pos);
			pcm->buf_pos += logii2s_port_transfer_data(port, buf,
								   size);

			if (pcm->buf_pos >= (pcm->buf_sz - 1))
				pcm->buf_pos = 0;

			snd_pcm_period_elapsed(substream);
		}
	}
}

static irqreturn_t i2s_irq_handler(int irq, void *priv)
{
	struct logii2s_data *data = priv;
	u32 iur;
	int i;

	XYLONI2S_DBG("\n");

	iur = logii2s_get_device_iur(data->base);
	for (i = 0; i < data->instances; i++) {
		if (iur & (1 << i))
			xylon_i2s_handle_irq(data->port[i]);
	}

	return IRQ_HANDLED;
}

static struct snd_pcm_hardware xylon_i2s_playback_hardware = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_RESUME),
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_8000_192000,
	.rate_min = 8000,
	.rate_max = 192000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = MAX_BUFFER_SIZE,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = (LOGII2S_FIFO_SIZE_MAX * 4),
	.periods_min = PERIODS_MIN,
	.periods_max = PERIODS_MAX,
	.fifo_size = 0,
};

static struct snd_pcm_hardware xylon_i2s_capture_hardware = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_RESUME),
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_8000_192000,
	.rate_min = 8000,
	.rate_max = 192000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = MAX_BUFFER_SIZE,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = (LOGII2S_FIFO_SIZE_MAX * 4),
	.periods_min = PERIODS_MIN,
	.periods_max = PERIODS_MAX,
	.fifo_size = 0,
};

static void xylon_i2s_start(struct logii2s_pcm_data *pcm)
{
	XYLONI2S_DBG("\n");

	if (pcm->xfer_dir == LOGII2S_TX_INSTANCE)
		logii2s_port_unmask_int(pcm->port, LOGII2S_INT_FAE);
	else if (pcm->xfer_dir == LOGII2S_RX_INSTANCE)
		logii2s_port_unmask_int(pcm->port, LOGII2S_INT_FAF);

	logii2s_port_enable_xfer(pcm->port);
}

static void xylon_i2s_stop(struct logii2s_pcm_data *pcm)
{
	XYLONI2S_DBG("\n");

	logii2s_port_disable_xfer(pcm->port);

	if (pcm->xfer_dir == LOGII2S_TX_INSTANCE) {
		logii2s_port_mask_int(pcm->port, LOGII2S_INT_MASK_ALL);
		logii2s_port_reset(pcm->port);
	} else if (pcm->xfer_dir == LOGII2S_RX_INSTANCE) {
		logii2s_port_mask_int(pcm->port, LOGII2S_INT_MASK_ALL);
		logii2s_port_reset(pcm->port);
	}
}

static int xylon_i2s_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct logii2s_pcm_data *pcm = substream->private_data;
	struct logii2s_data *data = pcm->port->data;
	struct device *dev = &data->pdev->dev;

	XYLONI2S_DBG("\n");

	if ((pcm->port->id > data->instances) || (pcm->port->id < 0)) {
		dev_err(dev, "invalid port index\n");
		return -EINVAL;
	}

	xylon_i2s_playback_hardware.fifo_size = pcm->port->fifo_size;

	runtime->hw = xylon_i2s_playback_hardware;

	logii2s_port_reset(pcm->port);
	pcm->xfer_dir = LOGII2S_TX_INSTANCE;
	pcm->substream = substream;

	runtime->private_data = pcm;

	return 0;
}

static int xylon_i2s_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct logii2s_pcm_data *pcm = substream->private_data;
	struct logii2s_data *data = pcm->port->data;
	struct device *dev = &data->pdev->dev;

	XYLONI2S_DBG("\n");

	if ((pcm->port->id > data->instances) || (pcm->port->id < 0)) {
		dev_err(dev, "invalid port index\n");
		return -EINVAL;
	}

	xylon_i2s_capture_hardware.fifo_size = pcm->port->fifo_size;

	runtime->hw = xylon_i2s_capture_hardware;

	logii2s_port_reset(pcm->port);
	pcm->xfer_dir = LOGII2S_RX_INSTANCE;
	pcm->substream = substream;

	runtime->private_data = pcm;

	return 0;
}

static int xylon_i2s_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct logii2s_pcm_data *pcm = runtime->private_data;

	XYLONI2S_DBG("\n");

	logii2s_port_reset(pcm->port);

	return 0;
}

static int xylon_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params)
{
	XYLONI2S_DBG("\n");
	XYLONI2S_DBG("rate %d, format %d\n",
		     params_rate(hw_params), params_format(hw_params));

	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int xylon_i2s_hw_free(struct snd_pcm_substream *substream)
{
	XYLONI2S_DBG("\n");

	return snd_pcm_lib_free_pages(substream);
}

static int xylon_i2s_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct logii2s_pcm_data *pcm = runtime->private_data;
	struct logii2s_data *data = pcm->port->data;
	unsigned int sample_rate;

	XYLONI2S_DBG("\n");

	if (runtime->dma_area == NULL) {
		XYLONI2S_DBG("memory not available\n");
		return -EINVAL;
	}

	pcm->buf_sz = snd_pcm_lib_buffer_bytes(substream);
	sample_rate = logii2s_port_init_clock(pcm->port, data->core_clock_freq,
					      runtime->rate);
	if ((sample_rate != 0) && (sample_rate != runtime->rate))
		pr_info("Sample rate set to %dkHz\n", sample_rate);

	return 0;
}

static int xylon_i2s_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct logii2s_pcm_data *pcm = runtime->private_data;

	XYLONI2S_DBG("\n");

	spin_lock(&pcm->lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		XYLONI2S_DBG("START / RESUME %d\n", pcm->port->id);
		pcm->buf_pos = 0;
		xylon_i2s_start(pcm);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		XYLONI2S_DBG("STOP / SUSPEND %d\n", pcm->port->id);
		xylon_i2s_stop(pcm);
		break;

	default:
		return -EINVAL;
	}

	spin_unlock(&pcm->lock);

	return 0;
}

static
snd_pcm_uframes_t xylon_i2s_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct logii2s_pcm_data *pcm = runtime->private_data;
	snd_pcm_uframes_t p;

	XYLONI2S_DBG("\n");

	spin_lock(&pcm->lock);

	p = bytes_to_frames(runtime, pcm->buf_pos);

	if (p >= runtime->buffer_size)
		p = 0;

	spin_unlock(&pcm->lock);

	return p;
}

static struct snd_pcm_ops xylon_i2s_playback_ops = {
	.open = xylon_i2s_playback_open,
	.close = xylon_i2s_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = xylon_i2s_hw_params,
	.hw_free = xylon_i2s_hw_free,
	.prepare = xylon_i2s_pcm_prepare,
	.trigger = xylon_i2s_pcm_trigger,
	.pointer = xylon_i2s_pcm_pointer,
};

static struct snd_pcm_ops xylon_i2s_capture_ops = {
	.open = xylon_i2s_capture_open,
	.close = xylon_i2s_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = xylon_i2s_hw_params,
	.hw_free = xylon_i2s_hw_free,
	.prepare = xylon_i2s_pcm_prepare,
	.trigger = xylon_i2s_pcm_trigger,
	.pointer = xylon_i2s_pcm_pointer,
};

static void xylon_i2s_private_free(struct snd_pcm *snd_pcm)
{
	XYLONI2S_DBG("\n");
}

static int xylon_i2s_pcm_new(struct logii2s_pcm_data *pcm,
			     struct snd_card *card, int id)
{
	struct device *dev = &pcm->port->data->pdev->dev;
	struct snd_pcm *snd_pcm;
	int ret, tx, playback_new, capture_new;
	char card_id[25];

	XYLONI2S_DBG("\n");

	tx = logii2s_port_direction(pcm->port);

	if (tx) {
		sprintf(card_id, DRIVER_NAME"-tx-%d", id);

		playback_new = 1;
		capture_new = 0;
	} else {
		sprintf(card_id, DRIVER_NAME"-rx-%d", id);

		playback_new = 0;
		capture_new = 1;
	}

	ret = snd_pcm_new(card, card_id, id, playback_new, capture_new,
			  &snd_pcm);
	if (ret) {
		dev_err(dev, "failed new snd_pcm create\n");
		return ret;
	}

	if (tx) {
		sprintf(snd_pcm->name, DRIVER_NAME"-tx-%d PCM", id);
		snd_pcm_set_ops(snd_pcm, SNDRV_PCM_STREAM_PLAYBACK,
				&xylon_i2s_playback_ops);
	} else {
		sprintf(snd_pcm->name, DRIVER_NAME"-rx-%d PCM", id);
		snd_pcm_set_ops(snd_pcm, SNDRV_PCM_STREAM_CAPTURE,
				&xylon_i2s_capture_ops);
	}

	snd_pcm->private_data = pcm;
	snd_pcm->private_free = xylon_i2s_private_free;
	snd_pcm->info_flags = 0;

	snd_pcm_lib_preallocate_pages_for_all(snd_pcm,
					      SNDRV_DMA_TYPE_CONTINUOUS,
					      snd_dma_continuous_data(GFP_KERNEL),
					      BUFFER_SIZE, MAX_BUFFER_SIZE);

	return 0;
}

static int xylon_i2s_get_of_parameters(struct logii2s_data *data)
{
	struct device *dev = &data->pdev->dev;
	struct device_node *dn = dev->of_node;
	int ret;

	XYLONI2S_DBG("\n");

	data->instances = of_get_child_count(dn);
	if ((data->instances == 0) || (data->instances > LOGII2S_MAX_INST)) {
		dev_err(dev, "invalid number of instances");
		return -EINVAL;
	}

	ret = of_property_read_u32(dn, "core-clock-frequency",
				   &data->core_clock_freq);
	if (ret)
		dev_err(dev, "failed get core-clock-frequency\n");

	return ret;
}

static int xylon_i2s_get_port_of_parameters(struct logii2s_port *port)
{
	struct logii2s_data *data = port->data;
	struct device *dev = &data->pdev->dev;
	struct device_node *parent_dn = dev->of_node;
	struct device_node *dn;
	int id = port->id;
	int ret;
	char name[20+1];

	XYLONI2S_DBG("\n");

	sprintf(name, "instance_%d", id);

	dn = of_get_child_by_name(parent_dn, name);
	if (!dn)
		return -ENODEV;

	ret = of_property_read_u32(dn, "i2s-clock-frequency",
				   &port->clock_freq);
	if (ret) {
		dev_err(dev, "failed get i2s-clock-frequency for instance %d\n",
			id);
		return ret;
	}
	ret = of_property_read_u32(dn, "fifo-size", &port->fifo_size);
	if (ret) {
		dev_err(dev, "failed get fifo-size for instance %d\n", id);
		return ret;
	}
	ret = of_property_read_u32(dn, "almost-full-level", &port->almost_full);
	if (ret) {
		dev_err(dev, "failed get almost-full-level for instance %d\n",
			id);
		return ret;
	}
	ret = of_property_read_u32(dn, "almost-empty-level",
				   &port->almost_empty);
	if (ret) {
		dev_err(dev, "failed get almost-empty-level for instance %d\n",
			id);
		return ret;
	}

	return 0;
}

static int xylon_i2s_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem *base;
	struct logii2s_data *data;
	struct logii2s_port *port;
	struct snd_card *card;
	struct logii2s_pcm_data *pcm;
	int i, ret, irq;

	XYLONI2S_DBG("\n");

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(dev, "failed allocate data\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "failed get irq\n");
		return -EINVAL;
	}
	ret = devm_request_irq(dev, irq, i2s_irq_handler, IRQF_TRIGGER_HIGH,
			       DRIVER_NAME, (void *)data);
	if (ret) {
		dev_err(dev, "failed request irq\n");
		return ret;
	}

	data->pdev = pdev;
	data->pbase = res->start;
	data->base = base;
	data->irq = irq;

	ret = xylon_i2s_get_of_parameters(data);
	if (ret) {
		dev_err(dev, "failed get DTS parameters\n");
		return ret;
	}

	ret = snd_card_new(dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			   THIS_MODULE, 0, &card);
	if (ret) {
		dev_err(dev, "failed sound card create\n");
		return ret;
	}

	for (i = 0; i < data->instances; i++) {
		port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
		if (!port) {
			dev_err(dev, "failed allocate port\n");
			return -ENOMEM;
		}
		port->data = data;
		port->base = (data->base + LOGII2S_INST_OFFSET) +
			      (LOGII2S_INST_OFFSET * i);
		port->id = i;

		data->port[i] = port;

		ret = xylon_i2s_get_port_of_parameters(port);
		if (!ret) {
			logii2s_port_reset(port);

			pcm = devm_kzalloc(dev, sizeof(*pcm), GFP_KERNEL);
			if (!pcm) {
				dev_err(dev, "failed allocate pcm\n");
				goto free_card;
			}

			pcm->port = port;
			pcm->port->private = pcm;

			spin_lock_init(&pcm->lock);

			ret = xylon_i2s_pcm_new(pcm, card, i);
			if (ret) {
				dev_err(dev, "failed pcm create\n");
				goto free_card;
			}
		} else {
			goto free_card;
		}
	}

	strcpy(card->driver, DRIVER_NAME);
	strcpy(card->shortname, DRIVER_NAME);
	sprintf(card->longname, "xylon-"DRIVER_NAME" %i", pdev->id + 1);

	card->private_data = (void *)data;

	snd_card_set_dev(card, dev);

	if (snd_card_register(card)) {
		dev_err(dev, "failed card register\n");
		goto free_card;
	}

	platform_set_drvdata(pdev, card);

	return 0;

free_card:
	snd_card_free(card);

	return ret;
}

static int xylon_i2s_remove(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);

	XYLONI2S_DBG("\n");

	snd_card_free(card);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id xylon_i2s_of_match[] = {
	{ .compatible = "xylon,logii2s-2.00.a" },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, xylon_i2s_of_match);

static struct platform_driver xylon_i2s_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = xylon_i2s_of_match,
	},
	.probe = xylon_i2s_probe,
	.remove = xylon_i2s_remove,
};
module_platform_driver(xylon_i2s_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(LOGII2S_DRIVER_DESCRIPTION);
MODULE_VERSION(LOGII2S_DRIVER_VERSION);
