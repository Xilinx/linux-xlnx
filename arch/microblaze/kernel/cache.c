/*
 * arch/microblaze/kernel/microblaze_cache.c -- 
 *    Cache control for MicroBlaze cache memories
 *
 *  Copyright (C) 2007  PetaLogix
 *  Copyright (C) 2007  John Williams <john.williams@petalogix.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 */

#include <asm/cacheflush.h>
#include <asm/cache.h>

/* Exported functions.  */

void flush_icache (void)
{
#if XPAR_MICROBLAZE_0_USE_ICACHE==1
	unsigned int i;
	unsigned flags;

	local_irq_save(flags);
	__disable_icache();

	/* Just loop through cache size and invalidate, no need to add
	   CACHE_BASE address */
	for(i=0;i<XPAR_MICROBLAZE_0_CACHE_BYTE_SIZE;i+=ICACHE_LINE_SIZE)                __invalidate_icache(i);

	__enable_icache();
	local_irq_restore(flags);
#endif /* XPAR_MICROBLAZE_0_USE_ICACHE */
}

void flush_icache_range (unsigned long start, unsigned long end)
{ 
#if XPAR_MICROBLAZE_0_USE_ICACHE==1
	unsigned int i;
	unsigned flags;
#if XPAR_MICROBLAZE_0_ICACHE_USE_FSL==1
	unsigned int align = ~(ICACHE_LINE_SIZE - 1);
#endif

/* No need to cover entire cache range, just cover cache footprint */
	end=min(start+XPAR_MICROBLAZE_0_CACHE_BYTE_SIZE, end);
#if XPAR_MICROBLAZE_0_ICACHE_USE_FSL==1
	start &= align;		/* Make sure we are aligned */
	end  = ((end & align) + ICACHE_LINE_SIZE);              /* Push end up to the next cache line */
#endif
	local_irq_save(flags);
	__disable_icache();

	for(i=start;i<end;i+=ICACHE_LINE_SIZE)
		__invalidate_icache(i);

	__enable_icache();
	local_irq_restore(flags);
#endif /* XPAR_MICROBLAZE_0_USE_ICACHE */
}

void flush_icache_page (struct vm_area_struct *vma, struct page *page)
{
	flush_icache();
}

void flush_icache_user_range (struct vm_area_struct *vma, struct page *page,
			      unsigned long adr, int len)
{
	flush_icache();
}

void flush_cache_sigtramp (unsigned long addr)
{
	flush_icache_range(addr, addr+8);
}
