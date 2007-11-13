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
#include <asm/cpuinfo.h>

/* We always align cache instructions.  Previously, this was done with
   FSL memory interfaces, but not PLB interfaces.  Since PLB
   interfaces are not present in current microblazes, we just assume
   that these always have to be aligned.
 */
#define ALIGN_DCACHE_INSTRUCTIONS 1
#define ALIGN_ICACHE_INSTRUCTIONS 1

/* Exported functions.  */

void __invalidate_icache_all (void)
{
	unsigned int i;
	unsigned flags;
        unsigned int cache_size = cpuinfo->icache_size;
        unsigned int line_size = cpuinfo->icache_line;

        if(!cpuinfo->use_icache)
            return;

	local_irq_save(flags);
	__disable_icache();

	/* Just loop through cache size and invalidate, no need to add
	   CACHE_BASE address */
	for(i=0;i<cache_size;i+=line_size)
		__invalidate_icache(i);

        /* Note that the cache will be returned to its original state
           when the status register is restored.*/
	local_irq_restore(flags);
}

void __invalidate_icache_range (unsigned long start, unsigned long end)
{ 
	unsigned int i;
	unsigned flags;
        unsigned int cache_size = cpuinfo->icache_size;
        unsigned int line_size = cpuinfo->icache_line;
#if ALIGN_ICACHE_INSTRUCTIONS==1
	unsigned int align = ~(line_size - 1);
#endif

        if(!cpuinfo->use_icache)
            return;

/* No need to cover entire cache range, just cover cache footprint */
	end=min(start+cache_size, end);
#if ALIGN_ICACHE_INSTRUCTIONS==1
	start &= align;		            /* Make sure we are aligned */
	end  = ((end & align) + line_size); /* Push end up to the next cache line */
#endif
	local_irq_save(flags);
	__disable_icache();

	for(i=start;i<end;i+=line_size)
		__invalidate_icache(i);

        /* Note that the cache will be returned to its original state
           when the status register is restored.*/
	local_irq_restore(flags);
}

void __invalidate_dcache_all (void)
{
	unsigned int i;
	unsigned flags;
        unsigned int cache_size = cpuinfo->dcache_size;
        unsigned int line_size = cpuinfo->dcache_line;

        if(!cpuinfo->use_dcache)
            return;

	local_irq_save(flags);
	__disable_dcache();

	/* Just loop through cache size and invalidate, no need to add
	   CACHE_BASE address */
	for(i=0;i<cache_size;i+=line_size)
		__invalidate_dcache(i);

        /* Note that the cache will be returned to its original state
           when the status register is restored.*/
	local_irq_restore(flags);
}

void __invalidate_dcache_range (unsigned long start, unsigned long end)
{ 
	unsigned int i;
	unsigned flags;
        unsigned int cache_size = cpuinfo->dcache_size;
        unsigned int line_size = cpuinfo->dcache_line;
#if ALIGN_DCACHE_INSTRUCTIONS==1
	unsigned int align = ~(line_size - 1);
#endif

        if(!cpuinfo->use_dcache)
            return;

        /* No need to cover entire cache range, just cover cache footprint */
	end=min(start+cache_size, end);
#if ALIGN_DCACHE_INSTRUCTIONS==1
	start &= align;		            /* Make sure we are aligned */
	end  = ((end & align) + line_size); /* Push end up to the next cache line */
#endif
	local_irq_save(flags);
	__disable_dcache();

	for(i=start;i<end;i+=line_size)
		__invalidate_dcache(i);

        /* Note that the cache will be returned to its original state
           when the status register is restored.*/
	local_irq_restore(flags);
}

