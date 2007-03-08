/*
 * linux/include/asm-arm/arch-s5c7375/memory.h
 *
 * Copyright (C) 2003 SAMSUNG ELECTRONICS 
 *		Hyok S. Choi <hyok.choi@samsung.com>
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

#define TASK_SIZE	(0x01a00000UL)
#define TASK_SIZE_26	TASK_SIZE

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */

#define PHYS_OFFSET	(CONFIG_DRAM_BASE)
#define PAGE_OFFSET 	(PHYS_OFFSET)
#define END_MEM     	(CONFIG_DRAM_BASE + CONFIG_DRAM_SIZE)

#define __virt_to_phys(vpage) ((unsigned long) (vpage))
#define __phys_to_virt(ppage) ((unsigned long) (ppage))
#define __virt_to_bus(vpage) ((unsigned long) (vpage))
#define __bus_to_virt(ppage) ((unsigned long) (ppage))

#endif /*  __ASM_ARCH_MEMORY_H */
