/*
 * max9867.h -- MAX9867 ALSA SoC Audio driver
 *
 * Copyright 2013-2015 Maxim Integrated Products
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MAX9867_H
#define _MAX9867_H

/* MAX9867 register space */

#define MAX9867_STATUS        0x00
#define MAX9867_JACKSTATUS   0x01
#define MAX9867_AUXHIGH      0x02
#define MAX9867_AUXLOW       0x03
#define MAX9867_INTEN        0x04
#define MAX9867_SYSCLK       0x05
#define MAX9867_FREQ_MASK    0xF
#define MAX9867_PSCLK_SHIFT  0x4
#define MAX9867_PSCLK_WIDTH  0x2
#define MAX9867_PSCLK_MASK   (0x03<<MAX9867_PSCLK_SHIFT)
#define MAX9867_PSCLK_10_20  0x1
#define MAX9867_PSCLK_20_40  0x2
#define MAX9867_PSCLK_40_60  0x3
#define MAX9867_AUDIOCLKHIGH 0x06
#define MAX9867_NI_HIGH_WIDTH 0x7
#define MAX9867_NI_HIGH_MASK 0x7F
#define MAX9867_NI_LOW_MASK 0x7F
#define MAX9867_NI_LOW_SHIFT 0x1
#define MAX9867_PLL     (1<<7)
#define MAX9867_AUDIOCLKLOW  0x07
#define MAX9867_RAPID_LOCK   0x01
#define MAX9867_IFC1A        0x08
#define MAX9867_MASTER       (1<<7)
#define MAX9867_I2S_DLY      (1<<4)
#define MAX9867_SDOUT_HIZ    (1<<3)
#define MAX9867_TDM_MODE     (1<<2)
#define MAX9867_WCI_MODE     (1<<6)
#define MAX9867_BCI_MODE     (1<<5)
#define MAX9867_IFC1B        0x09
#define MAX9867_IFC1B_BCLK_MASK 7
#define MAX9867_IFC1B_32BIT  0x01
#define MAX9867_IFC1B_24BIT  0x02
#define MAX9867_IFC1B_PCLK_2 4
#define MAX9867_IFC1B_PCLK_4 5
#define MAX9867_IFC1B_PCLK_8 6
#define MAX9867_IFC1B_PCLK_16 7
#define MAX9867_CODECFLTR    0x0a
#define MAX9867_DACGAIN      0x0b
#define MAX9867_DACLEVEL     0x0c
#define MAX9867_DAC_MUTE_SHIFT 0x6
#define MAX9867_DAC_MUTE_WIDTH 0x1
#define MAX9867_DAC_MUTE_MASK (0x1<<MAX9867_DAC_MUTE_SHIFT)
#define MAX9867_ADCLEVEL     0x0d
#define MAX9867_LEFTLINELVL  0x0e
#define MAX9867_RIGTHLINELVL 0x0f
#define MAX9867_LEFTVOL      0x10
#define MAX9867_RIGHTVOL     0x11
#define MAX9867_LEFTMICGAIN  0x12
#define MAX9867_RIGHTMICGAIN 0x13
#define MAX9867_INPUTCONFIG  0x14
#define MAX9867_INPUT_SHIFT  0x6
#define MAX9867_MICCONFIG    0x15
#define MAX9867_MODECONFIG   0x16
#define MAX9867_PWRMAN       0x17
#define MAX9867_SHTDOWN_MASK (1<<7)
#define MAX9867_REVISION     0xff

#define MAX9867_CACHEREGNUM 10

/* codec private data */
struct max9867_priv {
	struct regmap *regmap;
	struct snd_soc_codec *codec;
	unsigned int sysclk;
	unsigned int pclk;
	unsigned int master;
};
#endif
