/*
	Copyright (c) 2002, Micrel Kendin Operations

	Written 2002 by LIQUN RUAN

	This software may be used and distributed according to the terms of 
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice. This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	The author may be reached as lruan@kendin.com
	Micrel Kendin Operations
	486 Mercury Dr.
	Sunnyvale, CA 94085

	This driver is for Kendin's KS8695 SOHO Router Chipset as ethernet driver.

	Support and updates available at
	www.kendin.com/ks8695/

*/
#ifndef __KS8695_CACHE_H
#define __KS8695_CACHE_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "ks8695_drv.h"

/* start ICache lockdown at ICACHE_VICTIM_BASE */
#define	ICACHE_VICTIM_BASE		0
#define	ICACHE_VICTIM_INDEX		26		/* victim index bit, specific to ARM922T */
#define	ICACHE_ASSOCITIVITY		64		/* 64-WAY, specific to ARM922T */
#define	ICACHE_BYTES_PER_LINE	128		/* 8 * 4 * 4, specific to ARM922T */

extern void ks8695_icache_change_policy(int bRoundRobin);
/*extern int ks8695_icache_lock(void *icache_start, void *icache_end);*/
extern void ks8695_icache_unlock(void);
extern int ks8695_icache_is_locked(void);
extern void ks8695_icache_read_c9(void);
void ks8695_power_saving(int bSaving);
void ks8695_enable_power_saving(int bEnablePowerSaving);

#endif /* __KS8695_CACHE_H */
