/*
 *  linux/arch/nio2nommu/mm/memory.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 *  Copyright (C) 1998  Kenneth Albanowski <kjahds@kjahds.com>,
 *  Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *  Copyright (C) 2004  Microtronix Datacom Ltd.
 *
 *  Based on:
 *
 *  linux/arch/m68k/mm/memory.c
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

#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/slab.h>

#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/io.h>

/*
 * cache_clear() semantics: Clear any cache entries for the area in question,
 * without writing back dirty entries first. This is useful if the data will
 * be overwritten anyway, e.g. by DMA to memory. The range is defined by a
 * _physical_ address.
 */

void cache_clear (unsigned long paddr, int len)
{
}


/*
 *	Define cache invalidate functions. The instruction and data cache 
 *	will need to be flushed. Write back the dirty data cache and invalidate
 *	the instruction cache for the range.
 *
 */

static __inline__ void cache_invalidate_inst(unsigned long paddr, int len)
{
	unsigned long	sset, eset;

	sset = (paddr & (nasys_icache_size - 1)) & (~(nasys_icache_line_size - 1));
	eset = (((paddr & (nasys_icache_size - 1)) + len) & (~(nasys_icache_line_size - 1))) + nasys_icache_line_size;

	__asm__ __volatile__ (
	"1:\n\t"
	"flushi	%0\n\t"
	"add	%0,%0,%2\n\t"
	"blt	%0,%1,1b\n\t"
	"flushp\n\t"
	: : "r" (sset), "r" (eset), "r" (nasys_icache_line_size));

}

static __inline__ void cache_invalidate_data(unsigned long paddr, int len)
{
	unsigned long	sset, eset;

	sset = (paddr & (nasys_dcache_size - 1)) & (~(nasys_dcache_line_size - 1));
	eset = (((paddr & (nasys_dcache_size - 1)) + len) & (~(nasys_dcache_line_size - 1))) + nasys_dcache_line_size;

	__asm__ __volatile__ (
	"1:\n\t"
	"flushd	0(%0)\n\t"
	"add	%0,%0,%2\n\t"
	"blt	%0,%1,1b\n\t"
	: : "r" (sset),"r" (eset), "r" (nasys_dcache_line_size));

}

static __inline__ void cache_invalidate_lines(unsigned long paddr, int len)
{
	unsigned long	sset, eset;

	sset = (paddr & (nasys_dcache_size - 1)) & (~(nasys_dcache_line_size - 1));
	eset = (((paddr & (nasys_dcache_size - 1)) + len) & (~(nasys_dcache_line_size - 1))) + nasys_dcache_line_size;

	__asm__ __volatile__ (
	"1:\n\t"
	"flushd	0(%0)\n\t"
	"add	%0,%0,%2\n\t"
	"blt	%0,%1,1b\n\t"
	: : "r" (sset),"r" (eset), "r" (nasys_dcache_line_size));

	sset = (paddr & (nasys_icache_size - 1)) & (~(nasys_icache_line_size - 1));
	eset = (((paddr & (nasys_icache_size - 1)) + len) & (~(nasys_icache_line_size - 1))) + nasys_icache_line_size;

	__asm__ __volatile__ (
	"1:\n\t"
	"flushi	%0\n\t"
	"add	%0,%0,%2\n\t"
	"blt	%0,%1,1b\n\t"
	"flushp\n\t"
	: : "r" (sset), "r" (eset), "r" (nasys_icache_line_size));

}

/*
 * cache_push() semantics: Write back any dirty cache data in the given area,
 * and invalidate the range in the instruction cache. It needs not (but may)
 * invalidate those entries also in the data cache. The range is defined by a
 * _physical_ address.
 */

void cache_push (unsigned long paddr, int len)
{
	cache_invalidate_lines(paddr, len);
}


/*
 * cache_push_v() semantics: Write back any dirty cache data in the given
 * area, and invalidate those entries at least in the instruction cache. This
 * is intended to be used after data has been written that can be executed as
 * code later. The range is defined by a _user_mode_ _virtual_ address.
 */

void cache_push_v (unsigned long vaddr, int len)
{
	cache_invalidate_lines(vaddr, len);
}

/*
 * cache_push_all() semantics: Invalidate instruction cache and write back
 * dirty data cache & invalidate.
 */
void cache_push_all (void)
{
	__asm__ __volatile__ (
	"1:\n\t"
	"flushd	0(%0)\n\t"
	"sub	%0,%0,%1\n\t"
	"bgt	%0,r0,1b\n\t"
	: : "r" (nasys_dcache_size), "r" (nasys_dcache_line_size));

	__asm__ __volatile__ (
	"1:\n\t"
	"flushi	%0\n\t"
	"sub	%0,%0,%1\n\t"
	"bgt	%0,r0,1b\n\t"
	"flushp\n\t"
	: : "r" (nasys_icache_size), "r" (nasys_icache_line_size));

}

/*
 * dcache_push() semantics: Write back and dirty data cache and invalidate
 * the range.
 */
void dcache_push (unsigned long vaddr, int len)
{
	cache_invalidate_data(vaddr, len);
}

/*
 * icache_push() semantics: Invalidate instruction cache in the range.
 */
void icache_push (unsigned long vaddr, int len)
{
	cache_invalidate_inst(vaddr, len);
}

/* Map some physical address range into the kernel address space. The
 * code is copied and adapted from map_chunk().
 */

unsigned long kernel_map(unsigned long paddr, unsigned long size,
			 int nocacheflag, unsigned long *memavailp )
{
	return paddr;
}


int is_in_rom(unsigned long addr)
{
	extern unsigned long _ramstart, _ramend;

	/*
	 *	What we are really trying to do is determine if addr is
	 *	in an allocated kernel memory region. If not then assume
	 *	we cannot free it or otherwise de-allocate it. Ideally
	 *	we could restrict this to really being in a ROM or flash,
	 *	but that would need to be done on a board by board basis,
	 *	not globally.
	 */
	if ((addr < _ramstart) || (addr >= _ramend))
		return(1);

	/* Default case, not in ROM */
	return(0);
}

int __handle_mm_fault(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long address, int write_access)
{
  BUG();
		return VM_FAULT_OOM;
}
