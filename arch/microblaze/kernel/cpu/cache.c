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

#include <asm/xparameters.h>
#include <asm/cacheflush.h>
#include <asm/cache.h>

/* Exported functions.  */

void __invalidate_icache_all (void)
{
#if XPAR_MICROBLAZE_0_USE_ICACHE==1
	unsigned int i;
	unsigned flags;

	local_irq_save(flags);
	__disable_icache();

	/* Just loop through cache size and invalidate, no need to add
	   CACHE_BASE address */
	for(i=0;i<XPAR_MICROBLAZE_0_CACHE_BYTE_SIZE;i+=ICACHE_LINE_SIZE)
		__invalidate_icache(i);

	__enable_icache();
	local_irq_restore(flags);
#endif /* XPAR_MICROBLAZE_0_USE_ICACHE */
}

void __invalidate_icache_range (unsigned long start, unsigned long end)
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

void __invalidate_dcache_all (void)
{
#if XPAR_MICROBLAZE_0_USE_DCACHE==1
	unsigned int i;
	unsigned flags;

	local_irq_save(flags);
	__disable_dcache();

	/* Just loop through cache size and invalidate, no need to add
	   CACHE_BASE address */
	for(i=0;i<XPAR_MICROBLAZE_0_DCACHE_BYTE_SIZE;i+=DCACHE_LINE_SIZE)
		__invalidate_dcache(i);

	__enable_dcache();
	local_irq_restore(flags);
#endif /* XPAR_MICROBLAZE_0_USE_DCACHE */
}

void __invalidate_dcache_range (unsigned long start, unsigned long end)
{ 
#if XPAR_MICROBLAZE_0_USE_DCACHE==1
	unsigned int i;
	unsigned flags;
#if XPAR_MICROBLAZE_0_DCACHE_USE_FSL==1
	unsigned int align = ~(DCACHE_LINE_SIZE - 1);
#endif

/* No need to cover entire cache range, just cover cache footprint */
	end=min(start+XPAR_MICROBLAZE_0_DCACHE_BYTE_SIZE, end);
#if XPAR_MICROBLAZE_0_DCACHE_USE_FSL==1
	start &= align;		/* Make sure we are aligned */
	end  = ((end & align) + DCACHE_LINE_SIZE);              /* Push end up to the next cache line */
#endif
	local_irq_save(flags);
	__disable_dcache();

	for(i=start;i<end;i+=DCACHE_LINE_SIZE)
		__invalidate_dcache(i);

	__enable_dcache();
	local_irq_restore(flags);
#endif /* XPAR_MICROBLAZE_0_USE_DCACHE */
}

