/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Arnaud Pouliquen <arnaud.pouliquen@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mfd/syscon.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>

/* chipID supported */
#define CHIPID_STIH416 0
#define CHIPID_STIH407 1

/* DAC definitions */

/* stih416 DAC registers */
/* sysconf 2517: Audio-DAC-Control */
#define STIH416_AUDIO_DAC_CTRL 0x00000814
/* sysconf 2519: Audio-Gue-Control */
#define STIH416_AUDIO_GLUE_CTRL 0x0000081C

#define STIH416_DAC_NOT_STANDBY	0x3
#define STIH416_DAC_SOFTMUTE	0x4
#define STIH416_DAC_ANA_NOT_PWR	0x5
#define STIH416_DAC_NOT_PNDBG	0x6

#define STIH416_DAC_NOT_STANDBY_MASK	BIT(STIH416_DAC_NOT_STANDBY)
#define STIH416_DAC_SOFTMUTE_MASK	BIT(STIH416_DAC_SOFTMUTE)
#define STIH416_DAC_ANA_NOT_PWR_MASK	BIT(STIH416_DAC_ANA_NOT_PWR)
#define STIH416_DAC_NOT_PNDBG_MASK	BIT(STIH416_DAC_NOT_PNDBG)

/* stih407 DAC registers */
/* sysconf 5041: Audio-Gue-Control */
#define STIH407_AUDIO_GLUE_CTRL 0x000000A4
/* sysconf 5042: Audio-DAC-Control */
#define STIH407_AUDIO_DAC_CTRL 0x000000A8

/* DAC definitions */
#define STIH407_DAC_SOFTMUTE		0x0
#define STIH407_DAC_STANDBY_ANA		0x1
#define STIH407_DAC_STANDBY		0x2

#define STIH407_DAC_SOFTMUTE_MASK	BIT(STIH407_DAC_SOFTMUTE)
#define STIH407_DAC_STANDBY_ANA_MASK    BIT(STIH407_DAC_STANDBY_ANA)
#define STIH407_DAC_STANDBY_MASK        BIT(STIH407_DAC_STANDBY)

/* SPDIF definitions */
#define SPDIF_BIPHASE_ENABLE		0x6
#define SPDIF_BIPHASE_IDLE		0x7

#define SPDIF_BIPHASE_ENABLE_MASK	BIT(SPDIF_BIPHASE_ENABLE)
#define SPDIF_BIPHASE_IDLE_MASK		BIT(SPDIF_BIPHASE_IDLE)

enum {
	STI_SAS_DAI_SPDIF_OUT,
	STI_SAS_DAI_ANALOG_OUT,
};

static const struct reg_default stih416_sas_reg_defaults[] = {
	{ STIH407_AUDIO_GLUE_CTRL, 0x00000040 },
	{ STIH407_AUDIO_DAC_CTRL, 0x000000000 },
};

static const struct reg_default stih407_sas_reg_defaults[] = {
	{ STIH416_AUDIO_DAC_CTRL, 0x000000000 },
	{ STIH416_AUDIO_GLUE_CTRL, 0x00000040 },
};

struct sti_dac_audio {
	struct regmap *regmap;
	struct regmap *virt_regmap;
	struct regmap_field  **field;
	struct reset_control *rst;
	int mclk;
};

struct sti_spdif_audio {
	struct regmap *regmap;
	struct regmap_field  **field;
	int mclk;
};

/* device data structure */
struct sti_sas_dev_data {
	const int chipid; /* IC version */
	const struct regmap_config *regmap;
	const struct snd_soc_dai_ops *dac_ops;  /* DAC function callbacks */
	const struct snd_soc_dapm_widget *dapm_widgets; /* dapms declaration */
	const int num_dapm_widgets; /* dapms declaration */
	const struct snd_soc_dapm_route *dapm_routes; /* route declaration */
	const int num_dapm_routes; /* route declaration */
};

/* driver data structure */
struct sti_sas_data {
	struct device *dev;
	const struct sti_sas_dev_data *dev_data;
	struct sti_dac_audio dac;
	struct sti_spdif_audio spdif;
};

/* Read a register from the sysconf reg bank */
static int sti_sas_read_reg(void *context, unsigned int reg,
			    unsigned int *value)
{
	struct sti_sas_data *drvdata = context;
	int status;
	u32 val;

	status = regmap_read(drvdata->dac.regmap, reg, &val);
	*value = (unsigned int)val;

	return status;
}

/* Read a register from the sysconf reg bank */
static int sti_sas_write_reg(void *context, unsigned int reg,
			     unsigned int value)
{
	struct sti_sas_data *drvdata = context;
	int status;

	status = regmap_write(drvdata->dac.regmap, reg, value);

	return status;
}

static int  sti_sas_init_sas_registers(struct snd_soc_codec *codec,
				       struct sti_sas_data *data)
{
	int ret;
	/*
	 * DAC and SPDIF are activated by default
	 * put them in IDLE to save power
	 */

	/* Initialise bi-phase formatter to disabled */
	ret = snd_soc_update_bits(codec, STIH407_AUDIO_GLUE_CTRL,
				  SPDIF_BIPHASE_ENABLE_MASK, 0);

	if (!ret)
		/* Initialise bi-phase formatter idle value to 0 */
		ret = snd_soc_update_bits(codec, STIH407_AUDIO_GLUE_CTRL,
					  SPDIF_BIPHASE_IDLE_MASK, 0);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to update SPDIF registers");
		return ret;
	}

	/* Init DAC configuration */
	switch (data->dev_data->chipid) {
	case CHIPID_STIH407:
		/* init configuration */
		ret =  snd_soc_update_bits(codec, STIH407_AUDIO_DAC_CTRL,
					   STIH407_DAC_STANDBY_MASK,
					   STIH407_DAC_STANDBY_MASK);

		if (!ret)
			ret = snd_soc_update_bits(codec, STIH407_AUDIO_DAC_CTRL,
						  STIH407_DAC_STANDBY_ANA_MASK,
						  STIH407_DAC_STANDBY_ANA_MASK);
		if (!ret)
			ret = snd_soc_update_bits(codec, STIH407_AUDIO_DAC_CTRL,
						  STIH407_DAC_SOFTMUTE_MASK,
						  STIH407_DAC_SOFTMUTE_MASK);
		break;
	case CHIPID_STIH416:
		ret =  snd_soc_update_bits(codec, STIH416_AUDIO_DAC_CTRL,
					   STIH416_DAC_NOT_STANDBY_MASK, 0);
		if (!ret)
			ret =  snd_soc_update_bits(codec,
						   STIH416_AUDIO_DAC_CTRL,
						   STIH416_DAC_ANA_NOT_PWR, 0);
		if (!ret)
			ret =  snd_soc_update_bits(codec,
						   STIH416_AUDIO_DAC_CTRL,
						   STIH416_DAC_NOT_PNDBG_MASK,
						   0);
		if (!ret)
			ret =  snd_soc_update_bits(codec,
						   STIH416_AUDIO_DAC_CTRL,
						   STIH416_DAC_SOFTMUTE_MASK,
						   STIH416_DAC_SOFTMUTE_MASK);
		break;
	default:
		return -EINVAL;
	}

	if (ret < 0) {
		dev_err(codec->dev, "Failed to update DAC registers");
		return ret;
	}

	return ret;
}

/*
 * DAC
 */
static int sti_sas_dac_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	/* Sanity check only */
	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS) {
		dev_err(dai->codec->dev,
			"%s: ERROR: Unsupporter master mask 0x%x\n",
			__func__, fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	return 0;
}

static int stih416_dac_probe(struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sti_sas_data *drvdata = dev_get_drvdata(codec->dev);
	struct sti_dac_audio *dac = &drvdata->dac;

	/* Get reset control */
	dac->rst = devm_reset_control_get(codec->dev, "dac_rst");
	if (IS_ERR(dac->rst)) {
		dev_err(dai->codec->dev,
			"%s: ERROR: DAC reset control not defined !\n",
			__func__);
		dac->rst = NULL;
		return -EFAULT;
	}
	/* Put the DAC into reset */
	reset_control_assert(dac->rst);

	return 0;
}

static const struct snd_soc_dapm_widget stih416_sas_dapm_widgets[] = {
	SND_SOC_DAPM_PGA("DAC bandgap", STIH416_AUDIO_DAC_CTRL,
			 STIH416_DAC_NOT_PNDBG_MASK, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("DAC standby ana", STIH416_AUDIO_DAC_CTRL,
			     STIH416_DAC_ANA_NOT_PWR, 0, NULL, 0),
	SND_SOC_DAPM_DAC("DAC standby",  "dac_p", STIH416_AUDIO_DAC_CTRL,
			 STIH416_DAC_NOT_STANDBY, 0),
	SND_SOC_DAPM_OUTPUT("DAC Output"),
};

static const struct snd_soc_dapm_widget stih407_sas_dapm_widgets[] = {
	SND_SOC_DAPM_OUT_DRV("DAC standby ana", STIH407_AUDIO_DAC_CTRL,
			     STIH407_DAC_STANDBY_ANA, 1, NULL, 0),
	SND_SOC_DAPM_DAC("DAC standby",  "dac_p", STIH407_AUDIO_DAC_CTRL,
			 STIH407_DAC_STANDBY, 1),
	SND_SOC_DAPM_OUTPUT("DAC Output"),
};

static const struct snd_soc_dapm_route stih416_sas_route[] = {
	{"DAC Output", NULL, "DAC bandgap"},
	{"DAC Output", NULL, "DAC standby ana"},
	{"DAC standby ana", NULL, "DAC standby"},
};

static const struct snd_soc_dapm_route stih407_sas_route[] = {
	{"DAC Output", NULL, "DAC standby ana"},
	{"DAC standby ana", NULL, "DAC standby"},
};

static int stih416_sas_dac_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_codec *codec = dai->codec;

	if (mute) {
		return snd_soc_update_bits(codec, STIH416_AUDIO_DAC_CTRL,
					    STIH416_DAC_SOFTMUTE_MASK,
					    STIH416_DAC_SOFTMUTE_MASK);
	} else {
		return snd_soc_update_bits(codec, STIH416_AUDIO_DAC_CTRL,
					    STIH416_DAC_SOFTMUTE_MASK, 0);
	}
}

static int stih407_sas_dac_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_codec *codec = dai->codec;

	if (mute) {
		return snd_soc_update_bits(codec, STIH407_AUDIO_DAC_CTRL,
					    STIH407_DAC_SOFTMUTE_MASK,
					    STIH407_DAC_SOFTMUTE_MASK);
	} else {
		return snd_soc_update_bits(codec, STIH407_AUDIO_DAC_CTRL,
					    STIH407_DAC_SOFTMUTE_MASK,
					    0);
	}
}

/*
 * SPDIF
 */
static int sti_sas_spdif_set_fmt(struct snd_soc_dai *dai,
				 unsigned int fmt)
{
	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS) {
		dev_err(dai->codec->dev,
			"%s: ERROR: Unsupporter master mask 0x%x\n",
			__func__, fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}

	return 0;
}

/*
 * sti_sas_spdif_trigger:
 * Trigger function is used to ensure that BiPhase Formater is disabled
 * before CPU dai is stopped.
 * This is mandatory to avoid that BPF is stalled
 */
static int sti_sas_spdif_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		return snd_soc_update_bits(codec, STIH407_AUDIO_GLUE_CTRL,
					    SPDIF_BIPHASE_ENABLE_MASK,
					    SPDIF_BIPHASE_ENABLE_MASK);
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		return snd_soc_update_bits(codec, STIH407_AUDIO_GLUE_CTRL,
					    SPDIF_BIPHASE_ENABLE_MASK,
					    0);
	default:
		return -EINVAL;
	}
}

static bool sti_sas_volatile_register(struct device *dev, unsigned int reg)
{
	if (reg == STIH407_AUDIO_GLUE_CTRL)
		return true;

	return false;
}

/*
 * CODEC DAIS
 */

/*
 * sti_sas_set_sysclk:
 * get MCLK input frequency to check that MCLK-FS ratio is coherent
 */
static int sti_sas_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			      unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sti_sas_data *drvdata = dev_get_drvdata(codec->dev);

	if (dir == SND_SOC_CLOCK_OUT)
		return 0;

	if (clk_id != 0)
		return -EINVAL;

	switch (dai->id) {
	case STI_SAS_DAI_SPDIF_OUT:
		drvdata->spdif.mclk = freq;
		break;

	case STI_SAS_DAI_ANALOG_OUT:
		drvdata->dac.mclk = freq;
		break;
	}

	return 0;
}

static int sti_sas_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct sti_sas_data *drvdata = dev_get_drvdata(codec->dev);
	struct snd_pcm_runtime *runtime = substream->runtime;

	switch (dai->id) {
	case STI_SAS_DAI_SPDIF_OUT:
		if ((drvdata->spdif.mclk / runtime->rate) != 128) {
			dev_err(codec->dev, "unexpected mclk-fs ratio");
			return -EINVAL;
		}
		break;
	case STI_SAS_DAI_ANALOG_OUT:
		if ((drvdata->dac.mclk / runtime->rate) != 256) {
			dev_err(codec->dev, "unexpected mclk-fs ratio");
			return -EINVAL;
		}
		break;
	}

	return 0;
}

static const struct snd_soc_dai_ops stih416_dac_ops = {
	.set_fmt = sti_sas_dac_set_fmt,
	.mute_stream = stih416_sas_dac_mute,
	.prepare = sti_sas_prepare,
	.set_sysclk = sti_sas_set_sysclk,
};

static const struct snd_soc_dai_ops stih407_dac_ops = {
	.set_fmt = sti_sas_dac_set_fmt,
	.mute_stream = stih407_sas_dac_mute,
	.prepare = sti_sas_prepare,
	.set_sysclk = sti_sas_set_sysclk,
};

static const struct regmap_config stih407_sas_regmap = {
	.reg_bits = 32,
	.val_bits = 32,
	.fast_io = true,
	.max_register = STIH407_AUDIO_DAC_CTRL,
	.reg_defaults = stih407_sas_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(stih407_sas_reg_defaults),
	.volatile_reg = sti_sas_volatile_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_read = sti_sas_read_reg,
	.reg_write = sti_sas_write_reg,
};

static const struct regmap_config stih416_sas_regmap = {
	.reg_bits = 32,
	.val_bits = 32,

	.max_register = STIH416_AUDIO_DAC_CTRL,
	.reg_defaults = stih416_sas_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(stih416_sas_reg_defaults),
	.volatile_reg = sti_sas_volatile_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_read = sti_sas_read_reg,
	.reg_write = sti_sas_write_reg,
};

static const struct sti_sas_dev_data stih416_data = {
	.chipid = CHIPID_STIH416,
	.regmap = &stih416_sas_regmap,
	.dac_ops = &stih416_dac_ops,
	.dapm_widgets = stih416_sas_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(stih416_sas_dapm_widgets),
	.dapm_routes =	stih416_sas_route,
	.num_dapm_routes = ARRAY_SIZE(stih416_sas_route),
};

static const struct sti_sas_dev_data stih407_data = {
	.chipid = CHIPID_STIH407,
	.regmap = &stih407_sas_regmap,
	.dac_ops = &stih407_dac_ops,
	.dapm_widgets = stih407_sas_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(stih407_sas_dapm_widgets),
	.dapm_routes =	stih407_sas_route,
	.num_dapm_routes = ARRAY_SIZE(stih407_sas_route),
};

static struct snd_soc_dai_driver sti_sas_dai[] = {
	{
		.name = "sas-dai-spdif-out",
		.id = STI_SAS_DAI_SPDIF_OUT,
		.playback = {
			.stream_name = "spdif_p",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_64000 |
				 SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
				 SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = (struct snd_soc_dai_ops[]) {
			{
				.set_fmt = sti_sas_spdif_set_fmt,
				.trigger = sti_sas_spdif_trigger,
				.set_sysclk = sti_sas_set_sysclk,
				.prepare = sti_sas_prepare,
			}
		},
	},
	{
		.name = "sas-dai-dac",
		.id = STI_SAS_DAI_ANALOG_OUT,
		.playback = {
			.stream_name = "dac_p",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
	},
};

#ifdef CONFIG_PM_SLEEP
static int sti_sas_resume(struct snd_soc_codec *codec)
{
	struct sti_sas_data *drvdata = dev_get_drvdata(codec->dev);

	return sti_sas_init_sas_registers(codec, drvdata);
}
#else
#define sti_sas_resume NULL
#endif

static int sti_sas_codec_probe(struct snd_soc_codec *codec)
{
	struct sti_sas_data *drvdata = dev_get_drvdata(codec->dev);
	int ret;

	ret = sti_sas_init_sas_registers(codec, drvdata);

	return ret;
}

static struct snd_soc_codec_driver sti_sas_driver = {
	.probe = sti_sas_codec_probe,
	.resume = sti_sas_resume,
};

static const struct of_device_id sti_sas_dev_match[] = {
	{
		.compatible = "st,stih416-sas-codec",
		.data = &stih416_data,
	},
	{
		.compatible = "st,stih407-sas-codec",
		.data = &stih407_data,
	},
	{},
};

static int sti_sas_driver_probe(struct platform_device *pdev)
{
	struct device_node *pnode = pdev->dev.of_node;
	struct sti_sas_data *drvdata;
	const struct of_device_id *of_id;

	/* Allocate device structure */
	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct sti_sas_data),
			       GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	/* Populate data structure depending on compatibility */
	of_id = of_match_node(sti_sas_dev_match, pnode);
	if (!of_id->data) {
		dev_err(&pdev->dev, "data associated to device is missing");
		return -EINVAL;
	}

	drvdata->dev_data = (struct sti_sas_dev_data *)of_id->data;

	/* Initialise device structure */
	drvdata->dev = &pdev->dev;

	/* Request the DAC & SPDIF registers memory region */
	drvdata->dac.virt_regmap = devm_regmap_init(&pdev->dev, NULL, drvdata,
						    drvdata->dev_data->regmap);
	if (IS_ERR(drvdata->dac.virt_regmap)) {
		dev_err(&pdev->dev, "audio registers not enabled\n");
		return PTR_ERR(drvdata->dac.virt_regmap);
	}

	/* Request the syscon region */
	drvdata->dac.regmap =
		syscon_regmap_lookup_by_phandle(pnode, "st,syscfg");
	if (IS_ERR(drvdata->dac.regmap)) {
		dev_err(&pdev->dev, "syscon registers not available\n");
		return PTR_ERR(drvdata->dac.regmap);
	}
	drvdata->spdif.regmap = drvdata->dac.regmap;

	/* Set DAC dai probe */
	if (drvdata->dev_data->chipid == CHIPID_STIH416)
		sti_sas_dai[STI_SAS_DAI_ANALOG_OUT].probe = stih416_dac_probe;

	sti_sas_dai[STI_SAS_DAI_ANALOG_OUT].ops = drvdata->dev_data->dac_ops;

	/* Set dapms*/
	sti_sas_driver.component_driver.dapm_widgets = drvdata->dev_data->dapm_widgets;
	sti_sas_driver.component_driver.num_dapm_widgets = drvdata->dev_data->num_dapm_widgets;

	sti_sas_driver.component_driver.dapm_routes = drvdata->dev_data->dapm_routes;
	sti_sas_driver.component_driver.num_dapm_routes = drvdata->dev_data->num_dapm_routes;

	/* Store context */
	dev_set_drvdata(&pdev->dev, drvdata);

	return snd_soc_register_codec(&pdev->dev, &sti_sas_driver,
					sti_sas_dai,
					ARRAY_SIZE(sti_sas_dai));
}

static int sti_sas_driver_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static struct platform_driver sti_sas_platform_driver = {
	.driver = {
		.name = "sti-sas-codec",
		.of_match_table = sti_sas_dev_match,
	},
	.probe = sti_sas_driver_probe,
	.remove = sti_sas_driver_remove,
};

module_platform_driver(sti_sas_platform_driver);

MODULE_DESCRIPTION("audio codec for STMicroelectronics sti platforms");
MODULE_AUTHOR("Arnaud.pouliquen@st.com");
MODULE_LICENSE("GPL v2");
