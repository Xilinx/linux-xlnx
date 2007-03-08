/*
 *  linux/include/asm-arm/hardware.h
 *
 *  Copyright (C) 1996 Russell King
 *  Copyright (C) 2004 Hyok S. Choi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Common hardware definitions
 */

#ifndef __ASM_HARDWARE_H
#define __ASM_HARDWARE_H

#include <asm/arch/hardware.h>

#ifndef CONFIG_MMU

#ifndef __ASSEMBLY__

/* the machine dependent  bootmem reserve and free routines */
#ifndef MACH_RESERVE_BOOTMEM
#define MACH_RESERVE_BOOTMEM()
#endif

#ifndef MACH_FREE_BOOTMEM
#define MACH_FREE_BOOTMEM()
#endif

/* by default, initmem is freed */
#ifndef DO_FREE_INITMEM
#define DO_FREE_INITMEM() 	(1)
#endif

#endif /* !__ASSEMBLY__ */

#endif /* !CONFIG_MMU */

#endif
