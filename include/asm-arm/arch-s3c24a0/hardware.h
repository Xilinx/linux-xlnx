/*
 * include/asm-arm/arch-s3c24a0/hardware.h
 *
 * $Id: hardware.h,v 1.3 2006/12/12 13:13:07 gerg Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * based on S3C2410.h modified by hcyun <heechul.yun@samsung.com>
 * modified for hybrid MM support (uClinux and Linux) by Hyok S. Choi
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#ifndef CONFIG_MMU
/* called on reserve_node_zero() for reserving mmu section table */
#ifndef CONFIG_DRAM_BASE
  #define CONFIG_DRAM_BASE 0x10000000
#endif

#define MACH_RESERVE_BOOTMEM()  do { \
    reserve_bootmem_node(pgdat, (CONFIG_DRAM_BASE + 0x4000), 0x4000); \
  } while(0)

#define MACH_FREE_BOOTMEM()

#endif /* !CONFIG_MMU */

#define PCIO_BASE               0

#if defined(CONFIG_DRAM_BASE) && defined(CONFIG_DRAM_SIZE)
  #define PA_SDRAM_BASE          (CONFIG_DRAM_BASE)
  #define MEM_SIZE               (CONFIG_DRAM_SIZE)
#else
  #define PA_SDRAM_BASE          0x10000000
  #define MEM_SIZE               0x04000000
#endif

/*
 * S3C24A0 internal I/O mappings
 *
 * We have the following mapping:
 *              phys            virt
 *              40000000        e0000000
 */

#ifdef CONFIG_MMU

#define VIO_BASE                0xe0000000      /* virtual start of IO space */
#define PIO_START               0x40000000      /* physical start of IO space */

#define io_p2v(x) ((x) | 0xa0000000)
#define io_v2p(x) ((x) & ~0xa0000000)

#define io_p2v_isp(x) ((x) + 0xec000000)
#define io_v2p_isp(x) ((x) - 0xec000000)


#else /* UCLINUX */

#define PIO_START               0x40000000
#define VIO_BASE                PIO_START

#define io_p2v(x) (x)
#define io_v2p(x) (x)

#define io_p2v_isp(x) (x)
#define io_v2p_isp(x) (x)


#endif  /* CONFIG_MMU */


#ifndef __ASSEMBLY__
#include <asm/types.h>

/*
 * This __REG() version gives the same results as the one above, except
 * that we are fooling gcc some how so it generates far better and smaller
 * assembly code for access to contigous registers. It's a shame that gcc
 * doesn't guess this by itself
 */
typedef struct { volatile u32 offset[4096]; } __regbase;
#define __REGP(x)       ((__regbase *)((x)&~4095))->offset[((x)&4095)>>2]
#define __REG(x)        __REGP(io_p2v(x))

/* Let's kick gcc's ass again... */
# define __REG2(x,y)    \
        ( __builtin_constant_p(y) ? (__REG((x) + (y))) \
                                  : (*(volatile u32 *)((u32)&__REG(x) + (y))) )

#define __PREG(x)       (io_v2p((u32)&(x)))

/*SEO add  to allocate vertual memory address for ISP1583 */
#define __REG_ISP(x)    io_p2v_isp(x)
#define __PREG_ISP(x)   io_v2p_isp(x)


#else   /* __ASSEMBLY__ */

# define __REG(x)       io_p2v(x)
# define __PREG(x)      io_v2p(x)

#endif  /* __ASSEMBLY__ */

#include "S3C24A0.h"

#ifndef __ASSEMBLY__

#define EINT_PULLUP_EN          (0)
#define EINT_PULLUP_DIS         (1)

#define EINT_LOW_LEVEL          (0x0)
#define EINT_HIGH_LEVEL         (0x1)
#define EINT_FALLING_EDGE       (0x2)
#define EINT_RISING_EDGE        (0x4)
#define EINT_BOTH_EDGES         (0x6)

extern int set_external_irq(int irq, int edge, int pullup);

#endif

#ifdef CONFIG_ARCH_SMDK24A0
#include "smdk.h"
#else
#error not defined board
#endif

#endif /* __ASM_ARCH_HARDWARE_H */
