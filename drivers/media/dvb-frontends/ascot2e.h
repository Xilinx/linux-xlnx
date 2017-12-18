/*
 * ascot2e.h
 *
 * Sony Ascot3E DVB-T/T2/C/C2 tuner driver
 *
 * Copyright 2012 Sony Corporation
 * Copyright (C) 2014 NetUP Inc.
 * Copyright (C) 2014 Sergey Kozlov <serjk@netup.ru>
 * Copyright (C) 2014 Abylay Ospan <aospan@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
  */

#ifndef __DVB_ASCOT2E_H__
#define __DVB_ASCOT2E_H__

#include <linux/dvb/frontend.h>
#include <linux/i2c.h>

/**
 * struct ascot2e_config - the configuration of Ascot2E tuner driver
 * @i2c_address:	I2C address of the tuner
 * @xtal_freq_mhz:	Oscillator frequency, MHz
 * @set_tuner_priv:	Callback function private context
 * @set_tuner_callback:	Callback function that notifies the parent driver
 *			which tuner is active now
 */
struct ascot2e_config {
	u8	i2c_address;
	u8	xtal_freq_mhz;
	void	*set_tuner_priv;
	int	(*set_tuner_callback)(void *, int);
};

#if IS_REACHABLE(CONFIG_DVB_ASCOT2E)
extern struct dvb_frontend *ascot2e_attach(struct dvb_frontend *fe,
					const struct ascot2e_config *config,
					struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *ascot2e_attach(struct dvb_frontend *fe,
					const struct ascot2e_config *config,
					struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
