/*
 * eukrea-tlv320.c  --  SoC audio for eukrea_cpuimxXX in I2S mode
 *
 * Copyright 2010 Eric Bénard, Eukréa Electromatique <eric@eukrea.com>
 *
 * based on sound/soc/s3c24xx/s3c24xx_simtec_tlv320aic23.c
 * which is Copyright 2009 Simtec Electronics
 * and on sound/soc/imx/phycore-ac97.c which is
 * Copyright 2009 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <asm/mach-types.h>

#include "../codecs/tlv320aic23.h"
#include "imx-ssi.h"
#include "imx-audmux.h"

#define CODEC_CLOCK 12000000

static int eukrea_tlv320_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (ret) {
		dev_err(cpu_dai->dev,
			"Failed to set the cpu dai format.\n");
		return ret;
	}

	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (ret) {
		dev_err(cpu_dai->dev,
			"Failed to set the codec format.\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
				     CODEC_CLOCK, SND_SOC_CLOCK_OUT);
	if (ret) {
		dev_err(cpu_dai->dev,
			"Failed to set the codec sysclk.\n");
		return ret;
	}
	snd_soc_dai_set_tdm_slot(cpu_dai, 0xffffffc, 0xffffffc, 2, 0);

	ret = snd_soc_dai_set_sysclk(cpu_dai, IMX_SSP_SYS_CLK, 0,
				SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(cpu_dai->dev,
			"Can't set the IMX_SSP_SYS_CLK CPU system clock.\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_ops eukrea_tlv320_snd_ops = {
	.hw_params	= eukrea_tlv320_hw_params,
};

static struct snd_soc_dai_link eukrea_tlv320_dai = {
	.name		= "tlv320aic23",
	.stream_name	= "TLV320AIC23",
	.codec_dai_name	= "tlv320aic23-hifi",
	.platform_name	= "imx-ssi.0",
	.codec_name	= "tlv320aic23-codec.0-001a",
	.cpu_dai_name	= "imx-ssi.0",
	.ops		= &eukrea_tlv320_snd_ops,
};

static struct snd_soc_card eukrea_tlv320 = {
	.name		= "cpuimx-audio",
	.owner		= THIS_MODULE,
	.dai_link	= &eukrea_tlv320_dai,
	.num_links	= 1,
};

static int eukrea_tlv320_probe(struct platform_device *pdev)
{
	int ret;
	int int_port = 0, ext_port;

	if (machine_is_eukrea_cpuimx27()) {
		imx_audmux_v1_configure_port(MX27_AUDMUX_HPCR1_SSI0,
			IMX_AUDMUX_V1_PCR_SYN |
			IMX_AUDMUX_V1_PCR_TFSDIR |
			IMX_AUDMUX_V1_PCR_TCLKDIR |
			IMX_AUDMUX_V1_PCR_RFSDIR |
			IMX_AUDMUX_V1_PCR_RCLKDIR |
			IMX_AUDMUX_V1_PCR_TFCSEL(MX27_AUDMUX_HPCR3_SSI_PINS_4) |
			IMX_AUDMUX_V1_PCR_RFCSEL(MX27_AUDMUX_HPCR3_SSI_PINS_4) |
			IMX_AUDMUX_V1_PCR_RXDSEL(MX27_AUDMUX_HPCR3_SSI_PINS_4)
		);
		imx_audmux_v1_configure_port(MX27_AUDMUX_HPCR3_SSI_PINS_4,
			IMX_AUDMUX_V1_PCR_SYN |
			IMX_AUDMUX_V1_PCR_RXDSEL(MX27_AUDMUX_HPCR1_SSI0)
		);
	} else if (machine_is_eukrea_cpuimx25sd() ||
		   machine_is_eukrea_cpuimx35sd() ||
		   machine_is_eukrea_cpuimx51sd()) {
		ext_port = machine_is_eukrea_cpuimx25sd() ? 4 : 3;
		imx_audmux_v2_configure_port(int_port,
			IMX_AUDMUX_V2_PTCR_SYN |
			IMX_AUDMUX_V2_PTCR_TFSDIR |
			IMX_AUDMUX_V2_PTCR_TFSEL(ext_port) |
			IMX_AUDMUX_V2_PTCR_TCLKDIR |
			IMX_AUDMUX_V2_PTCR_TCSEL(ext_port),
			IMX_AUDMUX_V2_PDCR_RXDSEL(ext_port)
		);
		imx_audmux_v2_configure_port(ext_port,
			IMX_AUDMUX_V2_PTCR_SYN,
			IMX_AUDMUX_V2_PDCR_RXDSEL(int_port)
		);
	} else {
		/* return happy. We might run on a totally different machine */
		return 0;
	}

	eukrea_tlv320.dev = &pdev->dev;
	ret = snd_soc_register_card(&eukrea_tlv320);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);

	return ret;
}

static int eukrea_tlv320_remove(struct platform_device *pdev)
{
	snd_soc_unregister_card(&eukrea_tlv320);

	return 0;
}

static struct platform_driver eukrea_tlv320_driver = {
	.driver = {
		.name = "eukrea_tlv320",
		.owner = THIS_MODULE,
	},
	.probe = eukrea_tlv320_probe,
	.remove = eukrea_tlv320_remove,
};

module_platform_driver(eukrea_tlv320_driver);

MODULE_AUTHOR("Eric Bénard <eric@eukrea.com>");
MODULE_DESCRIPTION("CPUIMX ALSA SoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:eukrea_tlv320");
