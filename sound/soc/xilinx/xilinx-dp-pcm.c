/*
 * Xilinx DisplayPort Sound PCM support
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

#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/soc.h>

static const struct snd_pcm_hardware xilinx_pcm_hw = {
	.info			= SNDRV_PCM_INFO_MMAP |
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

static const struct snd_dmaengine_pcm_config xilinx_dmaengine_pcm_config = {
	.pcm_hardware = &xilinx_pcm_hw,
	.prealloc_buffer_size = 64 * 1024,
};

static int xilinx_dp_pcm_probe(struct platform_device *pdev)
{
	int ret;

	dev_set_name(&pdev->dev, pdev->dev.of_node->name);
	pdev->name = dev_name(&pdev->dev);
	ret = devm_snd_dmaengine_pcm_register(&pdev->dev,
					      &xilinx_dmaengine_pcm_config, 0);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "Xilinx DisplayPort Sound PCM probed\n");

	return 0;
}

static const struct of_device_id xilinx_dp_pcm_of_match[] = {
	{ .compatible = "xlnx,dp-snd-pcm", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xilinx_dp_pcm_of_match);

static struct platform_driver xilinx_dp_pcm_driver = {
	.driver	= {
		.name		= "xilinx-dp-snd-pcm",
		.of_match_table	= xilinx_dp_pcm_of_match,
	},
	.probe	= xilinx_dp_pcm_probe,
};
module_platform_driver(xilinx_dp_pcm_driver);

MODULE_DESCRIPTION("Xilinx DisplayPort Sound PCM module");
MODULE_LICENSE("GPL v2");
