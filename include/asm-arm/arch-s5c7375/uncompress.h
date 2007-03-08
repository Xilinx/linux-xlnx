/*
 *  linux/include/asm-armnommu/arch-s5c7375/uncompress.h
 *
 *  Copyright (C) 2004 Hyok S. Choi, Samsung Electronics Co.,Ltd.
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
#include <asm-armnommu/hardware/dcc.h>

#ifndef __UNCOMPRESS_H__
#define __UNCOMPRESS_H__

/*
 * just use DCC JTAG1 port
 */
static inline void puts(const char *s)
{
	dcc_puts(s);
}

static void puts_hex(unsigned long i)
{
	char lhex_buf[]="0x00000000";
	unsigned long ii,v;

	for(ii=9;ii>1;ii--)
	{
		v=(((0x0000000F << ((9-ii)*4)) & i) >> ((9-ii)*4));
		if(v>9)
			lhex_buf[ii]=(char)('A'+v-10);
		else
			lhex_buf[ii]=(char)('0'+v);
	}

	dcc_puts(lhex_buf);
}


/*
 * nothing to do
 */
#define arch_decomp_setup()

#define arch_decomp_wdog()

#endif
