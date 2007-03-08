/*
 *  arch/arm/mach-s3c24a0/leds-smdk.c
 *
 *  $Id: leds-smdk.c,v 1.3 2006/12/12 13:38:48 gerg Exp $
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/system.h>
#include <asm/arch/smdk.h>

#include "leds.h"

#define LED_STATE_ENABLED	1
#define LED_STATE_CLAIMED	2

#define LED0		(1 << 0)
#define LED1		(1 << 1)
#define LED2		(1 << 2)
#define LED3		(1 << 3)	

static unsigned int led_state;
static unsigned int hw_led_state;

static inline void
led_update(unsigned int state)
{
	write_gpio_bit(SMDK_LED4, (state & LED0));
	write_gpio_bit(SMDK_LED5, ((state & LED1) >> 1));
	write_gpio_bit(SMDK_LED6, ((state & LED2) >> 2));
	write_gpio_bit(SMDK_LED7, ((state & LED3) >> 3));
}

void
smdk_leds_event(led_event_t evt)
{
	unsigned long flags;

	local_irq_save(flags);

	switch (evt) {
	case led_start:
		hw_led_state = (LED1 | LED2 | LED3);
		led_state = LED_STATE_ENABLED;
		break;

	case led_stop:
		led_state &= ~LED_STATE_ENABLED;
		hw_led_state = (LED0 | LED1 | LED2 | LED3);
		led_update(hw_led_state);
		break;

	case led_claim:
		led_state |= LED_STATE_CLAIMED;
		hw_led_state = (LED0 | LED1 | LED2 | LED3);
		break;

	case led_release:
		led_state &= ~LED_STATE_CLAIMED;
		hw_led_state = (LED1 | LED2 | LED3);
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state ^= LED3;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state |= LED2;
		break;

	case led_idle_end:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state &= ~LED2;
		break;
#endif

	case led_halted:
		break;

	case led_green_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~LED2;
		break;

	case led_green_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= LED2;
		break;

	case led_amber_on:
		break;

	case led_amber_off:
		break;

	case led_red_on:
		break;

	case led_red_off:
		break;

	default:
		break;
	}

	if (led_state & LED_STATE_ENABLED)
		led_update(hw_led_state);

	local_irq_restore(flags);
}
