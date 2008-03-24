/*
 * Basic Virtex platform defines, included by <asm/ibm4xx.h>
 *
 * 2005-2007 (c) Secret Lab Technologies Ltd.
 * 2002-2004 (c) MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_VIRTEX_H__
#define __ASM_VIRTEX_H__

/* We have to distinguish between the PPC405 based Virtex chips and the PPC440
 * based chipts (Virtex 5). At this point we are still using virtex.[ch],
 * however in the future we may be transitioning to the flat device tree and
 * therefore eliminating virtex.[ch]. For the time being, though, we add the
 * PPC440 includes here.
 */
#if defined(CONFIG_XILINX_ML5XX)
  /* PPC 440 based boards */
  #include <asm/ibm44x.h>
#else
  /* PPC405 based boards */
  #include <asm/ibm405.h>
#endif
#include <asm/ppcboot.h>

/* Ugly, ugly, ugly! BASE_BAUD defined here to keep 8250.c happy. */
#if !defined(BASE_BAUD)
#define BASE_BAUD		(0)	/* dummy value; not used */
#endif

/* Virtual address used to set up fixed TLB entry for UART mapping if kernel
 * debugging is enabled. This can be any address as long as it does not overlap
 * with any other mapped io address space.
 */
#define UART0_IO_BASE		0xD0000000

#ifndef __ASSEMBLY__
extern const char *virtex_machine_name;
#define PPC4xx_MACHINE_NAME (virtex_machine_name)
#endif				/* !__ASSEMBLY__ */

/* We don't need anything mapped.  Size of zero will accomplish that. */
#define PPC4xx_ONB_IO_PADDR	0u
#define PPC4xx_ONB_IO_VADDR	0u
#define PPC4xx_ONB_IO_SIZE	0u

#endif				/* __ASM_VIRTEX_H__ */
#endif				/* __KERNEL__ */
