/* $Id: a.out.h,v 1.1 2006/07/05 06:20:25 gerg Exp $ */
/*
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef __NIOS2NOMMU_A_OUT_H__
#define __NIOS2NOMMU_A_OUT_H__

#define SPARC_PGSIZE    0x1000        /* Thanks to the sun4 architecture... */
#define SEGMENT_SIZE    SPARC_PGSIZE  /* whee... */

struct exec {
	unsigned char a_dynamic:1;      /* A __DYNAMIC is in this image */
	unsigned char a_toolversion:7;
	unsigned char a_machtype;
	unsigned short a_info;
	unsigned long a_text;		/* length of text, in bytes */
	unsigned long a_data;		/* length of data, in bytes */
	unsigned long a_bss;		/* length of bss, in bytes */
	unsigned long a_syms;		/* length of symbol table, in bytes */
	unsigned long a_entry;		/* where program begins */
	unsigned long a_trsize;
	unsigned long a_drsize;
};

#define INIT_EXEC {				\
	.a_dynamic	= 0,			\
	.a_toolversion	= 0,			\
	.a_machtype	= 0,			\
	.a_info		= 0,			\
	.a_text		= 0,			\
	.a_data		= 0,			\
	.a_bss		= 0,			\
	.a_syms		= 0,			\
	.a_entry	= 0,			\
	.a_trsize	= 0,			\
	.a_drsize	= 0,			\
}

/* Where in the file does the text information begin? */
#define N_TXTOFF(x)     (N_MAGIC(x) == ZMAGIC ? 0 : sizeof (struct exec))

/* Where do the Symbols start? */
#define N_SYMOFF(x)     (N_TXTOFF(x) + (x).a_text +   \
                         (x).a_data + (x).a_trsize +  \
                         (x).a_drsize)

/* Where does text segment go in memory after being loaded? */
#define N_TXTADDR(x)    (((N_MAGIC(x) == ZMAGIC) &&        \
	                 ((x).a_entry < SPARC_PGSIZE)) ?   \
                          0 : SPARC_PGSIZE)

/* And same for the data segment.. */
#define N_DATADDR(x) (N_MAGIC(x)==OMAGIC ?         \
                      (N_TXTADDR(x) + (x).a_text)  \
                       : (_N_SEGMENT_ROUND (_N_TXTENDADDR(x))))

#define N_TRSIZE(a)	((a).a_trsize)
#define N_DRSIZE(a)	((a).a_drsize)
#define N_SYMSIZE(a)	((a).a_syms)

#ifdef __KERNEL__

#define STACK_TOP	TASK_SIZE

#endif

#endif /* __NIOS2NOMMU_A_OUT_H__ */
