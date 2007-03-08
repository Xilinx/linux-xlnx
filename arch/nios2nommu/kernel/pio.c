/*
 *  linux/arch/nios2nommu/kernel/pio.c
 *  "Example" drivers(LEDs and 7 seg displays) of the PIO interface
 *  on Nios Development Kit.
 *
 *  Copyright (C) 2004 Microtronix Datacom Ltd
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 * 
 *  Written by Wentao Xu <wentao@microtronix.com>
 */
 
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <asm/io.h>

MODULE_AUTHOR("Microtronix Datacom Ltd.");
MODULE_DESCRIPTION("Drivers of PIO devices (LEDs and 7 seg) on Nios kit");
MODULE_LICENSE("GPL");

#undef CONFIG_PIO_SEG
#ifdef na_seven_seg_pio
#define CONFIG_PIO_SEG
#define PIO_SEG_IO	na_seven_seg_pio
#endif

#undef CONFIG_PIO_LED
#ifdef na_led_pio
#define CONFIG_PIO_LED
#define PIO_LED_IO		na_led_pio
#endif

#define PDEBUG printk

/* routines for 7-segment hex display */
#ifdef CONFIG_PIO_SEG
static unsigned char _hex_digits_data[] = {
	0x01, 0x4f, 0x12, 0x06, 0x4c, /* 0-4 */
	0x24, 0x20, 0x0f, 0x00, 0x04, /* 5-9 */
	0x08, 0x60, 0x72, 0x42, 0x30, /* a-e */
	0x38                      	  /* f   */
};

void pio_seg_write(int value)
{
  int led_value;

  /* Left Hand Digit, goes to PIO bits 8-14 */
  led_value = _hex_digits_data[value & 0xF];
  led_value |= (_hex_digits_data[(value >> 4) & 0xF]) << 8;

  outl(led_value, &(PIO_SEG_IO->np_piodata));
}

static void __init pio_seg_init(void)
{
	pio_seg_write(0);
}
#endif


/* routines for LED display */
#ifdef CONFIG_PIO_LED
void pio_led_write(int value)
{
	np_pio *pio=(np_pio *)(PIO_LED_IO);
	
    //outl(-1, &pio->np_piodirection); 
   	outl(value, &pio->np_piodata);
}

static void __init pio_led_init(void)
{
	np_pio *pio=(np_pio *)(PIO_LED_IO);
	
    outl(-1, &pio->np_piodirection); 
    outl(0x0, &pio->np_piodata);
}
#endif

/* timing routines */
#if defined(CONFIG_PIO_SEG) || defined(CONFIG_PIO_LED)
static struct timer_list display_timer;
static int restart_timer=1;
static int timer_counter=0;
static void display_timeout(unsigned long unused)
{
#ifdef CONFIG_PIO_SEG
	pio_seg_write(++timer_counter);
#endif	

#ifdef CONFIG_PIO_LED
	pio_led_write(timer_counter);
#endif
	if (restart_timer) {
		display_timer.expires = jiffies + HZ; /* one second */
		add_timer(&display_timer);
	}
}
#endif

int __init pio_init(void)
{
#ifdef CONFIG_PIO_SEG
	request_mem_region((unsigned long)PIO_SEG_IO, sizeof(np_pio), "pio_7seg");
	pio_seg_init();
#endif	

#ifdef CONFIG_PIO_LED
	request_mem_region((unsigned long)PIO_LED_IO, sizeof(np_pio), "pio_led");
	pio_led_init();
#endif

#if defined(CONFIG_PIO_SEG) || defined(CONFIG_PIO_LED)
	/* init timer */
	init_timer(&display_timer);
	display_timer.function = display_timeout;
	display_timer.data = 0;
	display_timer.expires = jiffies + HZ * 10; /* 10 seconds */
	add_timer(&display_timer);
#endif

	return 0;
}

static void __exit pio_exit(void)
{
#ifdef CONFIG_PIO_SEG
	pio_seg_write(0);
	release_mem_region((unsigned long)PIO_SEG_IO, sizeof(np_pio));
#endif	

#ifdef CONFIG_PIO_LED
	pio_led_write(0);
	release_mem_region((unsigned long)PIO_LED_IO, sizeof(np_pio));
#endif

#if defined(CONFIG_PIO_SEG) || defined(CONFIG_PIO_LED)
	restart_timer=0;
	del_timer_sync(&display_timer);
#endif
}
module_init(pio_init);
module_exit(pio_exit);

