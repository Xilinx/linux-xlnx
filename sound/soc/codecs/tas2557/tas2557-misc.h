/*
** =============================================================================
** Copyright (c) 2016  Texas Instruments Inc.
**
** This program is free software; you can redistribute it and/or modify it under
** the terms of the GNU General Public License as published by the Free Software
** Foundation; version 2.
**
** This program is distributed in the hope that it will be useful, but WITHOUT
** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
** FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
**
** File:
**     tas2557-misc.h
**
** Description:
**     header file for tas2557-misc.c
**
** =============================================================================
*/

#ifndef _TAS2557_MISC_H
#define _TAS2557_MISC_H

#define	FW_NAME_SIZE			64
#define	FW_DESCRIPTION_SIZE		256
#define	PROGRAM_BUF_SIZE		(5 + FW_NAME_SIZE + FW_DESCRIPTION_SIZE)
#define	CONFIGURATION_BUF_SIZE	(8 + FW_NAME_SIZE + FW_DESCRIPTION_SIZE)

#define	TIAUDIO_CMD_REG_WITE			1
#define	TIAUDIO_CMD_REG_READ			2
#define	TIAUDIO_CMD_DEBUG_ON			3
#define	TIAUDIO_CMD_PROGRAM				4
#define	TIAUDIO_CMD_CONFIGURATION		5
#define	TIAUDIO_CMD_FW_TIMESTAMP		6
#define	TIAUDIO_CMD_CALIBRATION			7
#define	TIAUDIO_CMD_SAMPLERATE			8
#define	TIAUDIO_CMD_BITRATE				9
#define	TIAUDIO_CMD_DACVOLUME			10
#define	TIAUDIO_CMD_SPEAKER				11
#define	TIAUDIO_CMD_FW_RELOAD			12

#define	TAS2557_MAGIC_NUMBER	0x32353537	/* '2557' */

#define	SMARTPA_SPK_DAC_VOLUME				_IOWR(TAS2557_MAGIC_NUMBER, 1, unsigned long)
#define	SMARTPA_SPK_POWER_ON				_IOWR(TAS2557_MAGIC_NUMBER, 2, unsigned long)
#define	SMARTPA_SPK_POWER_OFF				_IOWR(TAS2557_MAGIC_NUMBER, 3, unsigned long)
#define	SMARTPA_SPK_SWITCH_PROGRAM			_IOWR(TAS2557_MAGIC_NUMBER, 4, unsigned long)
#define	SMARTPA_SPK_SWITCH_CONFIGURATION	_IOWR(TAS2557_MAGIC_NUMBER, 5, unsigned long)
#define	SMARTPA_SPK_SWITCH_CALIBRATION		_IOWR(TAS2557_MAGIC_NUMBER, 6, unsigned long)
#define	SMARTPA_SPK_SET_SAMPLERATE			_IOWR(TAS2557_MAGIC_NUMBER, 7, unsigned long)
#define	SMARTPA_SPK_SET_BITRATE				_IOWR(TAS2557_MAGIC_NUMBER, 8, unsigned long)

int tas2557_register_misc(struct tas2557_priv *pTAS2557);
int tas2557_deregister_misc(struct tas2557_priv *pTAS2557);

#endif /* _TAS2557_MISC_H */
