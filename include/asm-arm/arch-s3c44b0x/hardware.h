/*
 *  linux/include/asm-arm/arch-s3c44b0x/hardware.h
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>
#include <asm/arch/s3c44b0x.h>

#ifndef __ASSEMBLY__

#define HARD_RESET_NOW()

/* the machine dependent  bootmem reserve and free routines */
#define MACH_RESERVE_BOOTMEM()
#define MACH_FREE_BOOTMEM()

/* yes, freeing initmem is okay */
#define DO_FREE_INITMEM() 	(1)

#endif

#define MEM_SIZE	CONFIG_DRAM_SIZE
#define PA_SDRAM_BASE	CONFIG_DRAM_BASE

/* The default system speed this box runs */
#if	!defined(CONFIG_ARM_CLK)
#define	CONFIG_ARM_CLK	60000000
#endif

#if	!defined(CONFIG_ARM_CLK_FIN) 
#define	CONFIG_ARM_CLK_FIN	8000000
#endif

#endif  /*   END OF __ASM_ARCH_HARDWARE_H */
