/*
 * linux/include/asm-armnommu/arch-s5c7375/blkmem.h
 *
 * Copyright (c) 2003 Hyok S. Choi, Samsung Electronics Co.,Ltd.
 * <hyok.choi@samsung.com>
 *
 * Contains configuration settings for the blkmem driver.
 */
#ifndef __ASM_ARCH_BLKMEM_H
#define __ASM_ARCH_BLKMEM_H

#define CAT_ROMARRAY


#ifndef HYOK_ROMFS_BOOT	
    extern char _end[];
    #define FIXUP_ARENAS \
	arena[0].address = ((unsigned long)_end + DRAM_BASE + 0x2000);	// ram
#else
    extern char __bss_start[];
    #define FIXUP_ARENAS \
	arena[0].address = ((unsigned long)__bss_start + FLASH_MEM_BASE); // rom
#endif

#endif
