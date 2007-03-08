/*
 *  linux/include/asm-arm/arch-s3c3410/hardware.h
 *
 *  Copyright (C) 2003 SAMSUNG ELECTRONICS 
 *                   Hyok S. Choi (hyok.choi@samsung.com)
 *
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>
#include <asm/arch/s3c3410.h>

#ifndef __ASSEMBLY__

#define HARD_RESET_NOW()

/* the machine dependent  bootmem reserve and free routines */
#define MACH_RESERVE_BOOTMEM()
#define MACH_FREE_BOOTMEM()

/* yes, freeing initmem is okay */
#define DO_FREE_INITMEM() 	(1)

#endif

#endif  /*   END OF __ASM_ARCH_HARDWARE_H */
