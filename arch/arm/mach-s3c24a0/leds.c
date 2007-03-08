/*
 *  /arch/arm/mach-s3c24a0/leds.c
 *
 * 	$Id: leds.c,v 1.2 2005/11/28 03:55:09 gerg Exp $
 * 	
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>

#include <asm/leds.h>
#include <asm/mach-types.h>

#include "leds.h"

static int __init
s3c24a0_leds_init(void)
{
#ifdef CONFIG_BOARD_SMDK24A0
	if (machine_is_s3c24a0())
		leds_event = smdk_leds_event;
#endif

	leds_event(led_start);
	return 0;
}

__initcall(s3c24a0_leds_init);
