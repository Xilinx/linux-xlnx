/*
 * include/asm-arm/arch-s3c24a0/memory.h
 *
 * $Id: memory.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 *
 * Copyright (C) Heechul Yun <heechul.yun@samsung.com>
 * Copyright (C) Hyok S. Choi <hyok.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MEMORY_H_
#define __ASM_ARCH_MEMORY_H_

#ifndef CONFIG_MMU

#ifndef TASK_SIZE_26
#define TASK_SIZE       END_MEM
#define TASK_SIZE_26    TASK_SIZE
#endif

#define PAGE_OFFSET (PHYS_OFFSET)


#define __virt_to_phys(vpage) ((unsigned long) (vpage))
#define __phys_to_virt(ppage) ((unsigned long) (ppage))

#endif /* !CONFIG_MMU */

#ifndef CONFIG_DRAM_BASE
#define PHYS_OFFSET (0x10000000UL)
#define END_MEM     (0x13000000UL)
#else
#define PHYS_OFFSET (CONFIG_DRAM_BASE)
#define END_MEM     (CONFIG_DRAM_BASE + CONFIG_DRAM_SIZE)
#endif

#define __virt_to_bus(x) __virt_to_phys(x)
#define __bus_to_virt(x) __phys_to_virt(x)


#endif /* __ASM_ARCH_MEMORY_H_ */
