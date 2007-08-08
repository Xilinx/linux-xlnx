/*
 * include/asm-microblaze/cacheflush.h
 *
 *  Copyright (C) 2007 PetaLogix
 *  Copyright (C) 2007 John Williams <john.williams@petalogix.com>
 *      based on v850 version which was 
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *  Copyright (C) 2000 Lineo, David McCullough <davidm@lineo.com>
 *  Copyright (C) 2001 Lineo, Greg Ungerer <gerg@snapgear.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 */

#ifndef __MICROBLAZE_CACHEFLUSH_H__
#define __MICROBLAZE_CACHEFLUSH_H__
#include <linux/kernel.h>	/* For min/max macros */
#include <linux/mm.h>	/* For min/max macros */
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/xparameters.h>

/*
 * Cache handling functions.
 * Microblaze has a write-through data cache, meaning that the data cache
 * never needs to be flushed.  The only flushing operations that are
 * implemented are to invalidate the instruction cache.  These are called
 * after loading a user application into memory, we must invalidate the
 * instruction cache to make sure we don't fetch old, bad code.
 */
void __flush_icache_all (void);
void __flush_icache_range (unsigned long start, unsigned long end);
void __flush_icache_page (struct vm_area_struct *vma, struct page *page);
void __flush_icache_user_range (struct vm_area_struct *vma, struct page *page,
			      unsigned long adr, int len);

void __flush_cache_sigtramp (unsigned long addr);

void __flush_dcache_all (void);
void __flush_dcache_range (unsigned long start, unsigned long end);
void __flush_dcache_page (struct vm_area_struct *vma, struct page *page);
void __flush_dcache_user_range (struct vm_area_struct *vma, struct page *page,
			      unsigned long adr, int len);


#if XPAR_MICROBLAZE_0_DCACHE_USE_FSL == 1
#define flush_cache_all()			__flush_icache_all()
#define flush_cache_mm(mm)			do { } while(0)
#define flush_cache_range(mm, start, end)	do { } while(0)
#define flush_cache_page(vma, vmaddr)		do { } while(0)
#define flush_page_to_ram(page)			do { } while(0)
#define flush_dcache_page(page)			do { } while(0)
#define flush_dcache_range(start, end)		__flush_dcache_all()
#define flush_icache_range(start, end)		__flush_icache_all()
#define flush_icache_user_range(vma,pg,adr,len) __flush_icache_all()
#define flush_icache_page(vma,pg)		__flush_icache_all()
#define flush_icache()				__flush_icache_all()
#define flush_cache_sigtramp(vaddr)		__flush_icache_all()
#else
#define flush_cache_all()			__flush_icache_all()
#define flush_cache_mm(mm)			do { } while(0)
#define flush_cache_range(mm, start, end)	do { } while(0)
#define flush_cache_page(vma, vmaddr)		do { } while(0)
#define flush_page_to_ram(page)			do { } while(0)
#define flush_dcache_page(page)			do { } while(0)
#define flush_dcache_range(start, end)		__flush_dcache_range(start,end)
#define flush_icache_range(start, end)		__flush_icache_range(start,end)
#define flush_icache_user_range(vma,pg,adr,len) __flush_icache_all()
#define flush_icache_page(vma,pg)		__flush_icache_all()
#define flush_icache()				__flush_icache_all()
#define flush_cache_sigtramp(vaddr)		__flush_icache_range(vaddr,vaddr+8)
#endif

#define flush_dcache_mmap_lock(mapping)		do {} while(0)
#define flush_dcache_mmap_unlock(mapping)	do {} while(0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len)	\
	memcpy((dst), (src), (len))

#define copy_from_user_page(vma, page, vaddr, dst, src, len)	\
	memcpy((dst), (src), (len))


#endif /* __MICROBLAZE_CACHEFLUSH_H__ */
