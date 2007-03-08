/*--------------------------------------------------------------------
 *
 * Optimized IO string functions.
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


#include <asm/io.h>

void insl(unsigned long port, void *dst, unsigned long count)
{
	unsigned long read32;

	if ((unsigned long)dst & 2){
		/* Unaligned destination pointer, need to do
		 * two 16 bit writes for each read.
		 */
		unsigned short *p=(unsigned short*)dst;
		while (count--){
			read32 = inl(port);
			*p++ = read32 & 0xFFFF;
			*p++ = read32 >> 16;
		}
	}
	else {
		unsigned long *p=(unsigned long*)dst;
		while (count--)
			*p++ = inl(port);
	}
}

void insw(unsigned long port, void *dst, unsigned long count)
{
	unsigned long dst1=(unsigned long)dst;
	if (count > 8) {
		/* Long word align buffer ptr */
		if	(dst1 & 2) {
			*(unsigned short*)dst1 = inw(port);
			dst1 += sizeof(unsigned short);
			count--;
		}

		/* Input pairs of short and store as longs */
		while (count >= 8) {
			*((unsigned long *)dst1) = inw(port) + (inw(port) << 16); dst1+=sizeof(unsigned long);
			*((unsigned long *)dst1) = inw(port) + (inw(port) << 16); dst1+=sizeof(unsigned long);
			*((unsigned long *)dst1) = inw(port) + (inw(port) << 16); dst1+=sizeof(unsigned long);
			*((unsigned long *)dst1) = inw(port) + (inw(port) << 16); dst1+=sizeof(unsigned long);
			count -= 8;
		}
	}

	/* Input remaining shorts */
	while (count--) {
		*((unsigned short *)dst1) = inw(port);
		dst1 += sizeof(unsigned short);
	}
}


void outsl(unsigned long port, void *src, unsigned long count)
{
	unsigned long src1=(unsigned long)src;
	unsigned long write32;
	
	if (src1 & 2){
		/* Unaligned source pointer, need to read
		 * two 16 bit shorts before writing to register.
		 */
		while (count--){
			write32 = *(unsigned short *)src1;
			src1+=sizeof(unsigned short);
			write32 |= *((unsigned short *)src1) << 16;
			src1+=sizeof(unsigned short);
			outl(write32,port);
		}
	}
	else {
		while (count--) {
			outl(*(unsigned long *)src1,port);
			src1+=sizeof(unsigned long);
		}
	}
}

void outsw(unsigned long port, void *src, unsigned long count)
{
	unsigned int lw;
	unsigned long src1=(unsigned long)src;

	if (count > 8) {
		/* Long word align buffer ptr */
		if	(src1 & 2) {
			outw( *(unsigned short *)src1, port );
			count--;
			src1 += sizeof(unsigned short);
		}

		/* Read long words and output as pairs of short */
		while (count >= 8) {
			lw = *(unsigned long *)src1;
			src1+=sizeof(unsigned long);
			outw(lw, port);
			outw((lw >> 16), port);
			lw = *(unsigned long *)src1;
			src1+=sizeof(unsigned long);
			outw(lw, port);
			outw((lw >> 16), port);
			lw = *(unsigned long *)src1;
			src1+=sizeof(unsigned long);
			outw(lw, port);
			outw((lw >> 16), port);
			lw = *(unsigned long *)src1;
			src1+=sizeof(unsigned long);
			outw(lw, port);
			outw((lw >> 16), port);
			count -= 8;
		}
	}

	/* Output remaining shorts */
	while (count--) {
		outw( *(unsigned short *)src1, port );
		src1 += sizeof(unsigned short);
	}
}
