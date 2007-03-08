/*
 *  linux/include/asm-arm/arch-s5c7375/timex.h
 *
 *  Copyright (C) 2003 SAMSUNG ELECTRONICS 
 *                           Hyok S. Choi <hyok.choi@samsung.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <asm/arch/s5c7375.h>

#define CLOCK_TICK_RATE	 (ECLK/ (SYS_TIMER03_PRESCALER +1))

	/*
	 * 27M / (0x6B +1) = 4usec
	 */
