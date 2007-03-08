#ifndef _ASM_SPI_H_
#define _ASM_SPI_H_ 1

/*--------------------------------------------------------------------
 *
 * include/asm-nios2nommu/spi.h
 *
 * Derived from various works, Alpha, ix86, M68K, Sparc, ...et al
 *
 * Copyright (C) 2004   Microtronix Datacom Ltd
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
 *
 *
 * Jan/20/2004		dgt	    NiosII
 *
 ---------------------------------------------------------------------*/


#include <asm/nios.h>

int  register_NIOS_SPI( void );
void unregister_NIOS_SPI( void );

#if defined(MODULE)
void cleanup_module( void );
int  init_module( void );
#endif

#if defined(__KERNEL__)
int  spi_reset  ( void );
#endif


#define clockCS 0x01
#define temperatureCS 0x02

#define clock_read_base 0x00
#define clock_write_base 0x80
#define clock_read_control 0x0F
#define clock_read_trickle 0x11

#define clock_read_sec 0x00
#define clock_read_min 0x01
#define clock_read_hour 0x02
#define clock_read_day 0x03
#define clock_read_date 0x04
#define clock_read_month 0x05
#define clock_read_year 0x06

#define clock_write_control 0x8F
#define clock_write_trickle 0x91
#define clock_write_sec 0x80
#define clock_write_min 0x81
#define clock_write_hour 0x82
#define clock_write_day 0x83
#define clock_write_date 0x84
#define clock_write_month 0x85
#define clock_write_year 0x86

#define clock_write_ram_start 0xA0
#define clock_write_ram_end 0x100
#define clock_read_ram_start 0x20
#define clock_read_ram_end 0x80


#define	clock_sec_def 0x11
#define clock_min_def 0x59
#define clock_hour_def 0x71
#define clock_day_def 0x00
#define clock_date_def 0x20
#define clock_month_def 0x12
#define clock_year_def 0x34

#define temp_read_base 0x00
#define temp_write_base 0x80
#define temp_read_control 0x00
#define temp_write_control 0x80
#define temp_read_msb 0x02
#define temp_read_lsb 0x01

#define MAX_TEMP_VAR 10

#endif /*_ASM_SPI_H_*/
