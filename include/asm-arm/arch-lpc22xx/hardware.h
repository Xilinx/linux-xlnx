/*
 *  linux/include/asm-arm/arch-lpc22xx/hardware.h
 *
 *  Copyright (C) 2004 Philips Semiconductors
 *
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>
#include <asm/arch/lpc22xx.h>

#ifndef __ASSEMBLY__

#define HARD_RESET_NOW()

/* the machine dependent  bootmem reserve and free routines */
#define MACH_RESERVE_BOOTMEM()

#define MACH_FREE_BOOTMEM()

/* yes, freeing initmem is okay */
#define DO_FREE_INITMEM() 	(1)

#endif

#define LPC22xx_MEM_SIZE     (CONFIG_DRAM_SIZE) 
#define MEM_SIZE             LPC22xx_MEM_SIZE
#define PA_SDRAM_BASE        CONFIG_DRAM_BASE

#endif  /*   END OF __ASM_ARCH_HARDWARE_H */
