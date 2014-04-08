/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARC_ASM_CACHE_H
#define __ARC_ASM_CACHE_H

/* In case $$ not config, setup a dummy number for rest of kernel */
#ifndef CONFIG_ARC_CACHE_LINE_SHIFT
#define L1_CACHE_SHIFT		6
#else
#define L1_CACHE_SHIFT		CONFIG_ARC_CACHE_LINE_SHIFT
#endif

#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)
#define CACHE_LINE_MASK		(~(L1_CACHE_BYTES - 1))

/*
 * ARC700 doesn't cache any access in top 256M.
 * Ideal for wiring memory mapped peripherals as we don't need to do
 * explicit uncached accesses (LD.di/ST.di) hence more portable drivers
 */
#define ARC_UNCACHED_ADDR_SPACE	0xc0000000

#ifndef __ASSEMBLY__

/* Uncached access macros */
#define arc_read_uncached_32(ptr)	\
({					\
	unsigned int __ret;		\
	__asm__ __volatile__(		\
	"	ld.di %0, [%1]	\n"	\
	: "=r"(__ret)			\
	: "r"(ptr));			\
	__ret;				\
})

#define arc_write_uncached_32(ptr, data)\
({					\
	__asm__ __volatile__(		\
	"	st.di %0, [%1]	\n"	\
	:				\
	: "r"(data), "r"(ptr));		\
})

#define ARCH_DMA_MINALIGN      L1_CACHE_BYTES

extern void arc_cache_init(void);
extern char *arc_cache_mumbojumbo(int cpu_id, char *buf, int len);
extern void read_decode_cache_bcr(void);

#endif	/* !__ASSEMBLY__ */

#endif /* _ASM_CACHE_H */
