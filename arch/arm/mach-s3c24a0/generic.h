/*
 *  /arch/arm/mach-s3c24a0/generic.h
 *
 * 	$Id: generic.h,v 1.2 2005/11/28 03:55:09 gerg Exp $
 * 	
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
extern void __init elfin_map_io(void);
extern void __init elfin_init_irq(void);

/* drivers/serial/serial_s3c24a0.c */
extern void __init elfin_register_uart(int idx, int port);
