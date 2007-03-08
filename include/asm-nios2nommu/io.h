/*
 * Copyright (C) 2004, Microtronix Datacom Ltd.
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

#ifndef __NIOS2_IO_H
#define __NIOS2_IO_H

#ifdef __KERNEL__

#include <linux/kernel.h>

#include <asm/page.h>      /* IO address mapping routines need this */
#include <asm/system.h>
#include <asm/unaligned.h>

extern void insw(unsigned long port, void *dst, unsigned long count);
extern void outsw(unsigned long port, void *src, unsigned long count);
extern void insl(unsigned long port, void *dst, unsigned long count);
extern void outsl(unsigned long port, void *src, unsigned long count);

#define readsb(p,d,l)		insb(p,d,l)
#define readsw(p,d,l)		insw(p,d,l)
#define readsl(p,d,l)		insl(p,d,l)
#define writesb(p,d,l)		outsb(p,d,l)
#define writesw(p,d,l)		outsw(p,d,l)
#define writesl(p,d,l)		outsl(p,d,l)
#ifndef irq_canonicalize
#define irq_canonicalize(i)	(i)
#endif

/*
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the Nios architecture, we just read/write the
 * memory location directly.
 */

#define readb(addr) 	\
({						\
	unsigned char __res;\
	__asm__ __volatile__( \
		"ldbuio %0, 0(%1)" \
		: "=r"(__res)	\
		: "r" (addr));	\
	__res;				\
})

#define readw(addr) 	\
({						\
	unsigned short __res;\
	__asm__ __volatile__( \
		"ldhuio %0, 0(%1)" \
		: "=r"(__res)	\
		: "r" (addr));	\
	__res;				\
})

#define readl(addr) 	\
({						\
	unsigned int __res;\
	__asm__ __volatile__( \
		"ldwio %0, 0(%1)" \
		: "=r"(__res)	\
		: "r" (addr));	\
	__res;				\
})

#define writeb(b,addr)	\
({						\
	__asm__ __volatile__( \
		"stbio %0, 0(%1)" \
		: : "r"(b), "r" (addr));	\
})

#define writew(b,addr)	\
({						\
	__asm__ __volatile__( \
		"sthio %0, 0(%1)" \
		: : "r"(b), "r" (addr));	\
})

#define writel(b,addr)	\
({						\
	__asm__ __volatile__( \
		"stwio %0, 0(%1)" \
		: : "r"(b), "r" (addr));	\
})

#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel

#define mmiowb()

/*
 *	make the short names macros so specific devices
 *	can override them as required
 */

#define memset_io(addr,c,len)	memset((void *)(((unsigned int)(addr)) | 0x80000000),(c),(len))
#define memcpy_fromio(to,from,len)	memcpy((to),(void *)(((unsigned int)(from)) | 0x80000000),(len))
#define memcpy_toio(to,from,len)	memcpy((void *)(((unsigned int)(to)) | 0x80000000),(from),(len))

#define inb(addr)    readb(addr)
#define inw(addr)    readw(addr)
#define inl(addr)    readl(addr)

#define outb(x,addr) ((void) writeb(x,addr))
#define outw(x,addr) ((void) writew(x,addr))
#define outl(x,addr) ((void) writel(x,addr))

#define inb_p(addr)    inb(addr)
#define inw_p(addr)    inw(addr)
#define inl_p(addr)    inl(addr)

#define outb_p(x,addr) outb(x,addr)
#define outw_p(x,addr) outw(x,addr)
#define outl_p(x,addr) outl(x,addr)



extern inline void insb(unsigned long port, void *dst, unsigned long count)
{
	unsigned char *p=(unsigned char*)dst;
	while (count--)
		*p++ = inb(port);
}

/* See arch/niosnommu/io.c for optimized version */
extern inline void _insw(unsigned long port, void *dst, unsigned long count)
{
	unsigned short *p=(unsigned short*)dst;
	while (count--)
		*p++ = inw(port);
}

/* See arch/niosnommu/kernel/io.c for unaligned destination pointer */
extern inline void _insl(unsigned long port, void *dst, unsigned long count)
{
	unsigned long *p=(unsigned long*)dst;
	while (count--)
		*p++ = inl(port);
}

extern inline void outsb(unsigned long port, void *src, unsigned long count)
{
	unsigned char *p=(unsigned char*)src;
	while (count--) 
        outb( *p++, port );
}

/* See arch/niosnommu/io.c for optimized version */
extern inline void _outsw(unsigned long port, void *src, unsigned long count)
{
	unsigned short *p=(unsigned short*)src;
	while (count--) 
        outw( *p++, port );
}

/* See arch/niosnommu/kernel/io.c for unaligned source pointer */
extern inline void _outsl(unsigned long port, void *src, unsigned long count)
{
	unsigned long *p=(unsigned long*)src;
	while (count--) 
        outl( *p++, port );
}



extern inline void mapioaddr(unsigned long physaddr, unsigned long virt_addr,
			     int bus, int rdonly)
{
	return;
}

//vic - copied from m68knommu

/* Values for nocacheflag and cmode */
#define IOMAP_FULL_CACHING		0
#define IOMAP_NOCACHE_SER		1
#define IOMAP_NOCACHE_NONSER		2
#define IOMAP_WRITETHROUGH		3

extern void *__ioremap(unsigned long physaddr, unsigned long size, int cacheflag);
extern void __iounmap(void *addr, unsigned long size);

extern inline void *ioremap(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}
extern inline void *ioremap_nocache(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}
extern inline void *ioremap_writethrough(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_WRITETHROUGH);
}
extern inline void *ioremap_fullcache(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_FULL_CACHING);
}

extern void iounmap(void *addr);


#define IO_SPACE_LIMIT 0xffffffff

#define dma_cache_inv(_start,_size)		dcache_push(_start,_size)
#define dma_cache_wback(_start,_size)		dcache_push(_start,_size)
#define dma_cache_wback_inv(_start,_size)	dcache_push(_start,_size)

/* Pages to physical address... */
#define page_to_phys(page)      page_to_virt(page)
#define page_to_bus(page)       page_to_virt(page)

#define mm_ptov(vaddr)		((void *) (vaddr))
#define mm_vtop(vaddr)		((unsigned long) (vaddr))
#define phys_to_virt(vaddr)	((void *) (vaddr))
#define virt_to_phys(vaddr)	((unsigned long) (vaddr))

#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif /* __KERNEL__ */

#endif /* !(__NIOS2_IO_H) */

