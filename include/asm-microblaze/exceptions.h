/*
 * Preliminary support for HW exception handing for Microblaze
 *
 * Copyright (C) 2005 John Williams <jwilliams@itee.uq.edu.au>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 */
#ifndef _ASM_EXCEPTIONS_H
#define _ASM_EXCEPTIONS_H

#include <linux/config.h>
#include <asm/xparameters.h>

/* Some helper macros to let the exception handler code cleaner */

/* Are there *any* HW exceptions enabled? */
#define MICROBLAZE_EXCEPTIONS_ENABLED 				\
	(XPAR_MICROBLAZE_0_UNALIGNED_EXCEPTIONS ||		\
	 XPAR_MICROBLAZE_0_ILL_OPCODE_EXCEPTION ||		\
	 XPAR_MICROBLAZE_0_IOPB_BUS_EXCEPTION ||		\
	 XPAR_MICROBLAZE_0_DOPB_BUS_EXCEPTION ||		\
	 XPAR_MICROBLAZE_0_DIV_ZERO_EXCEPTION ||		\
	 XPAR_MICROBLAZE_0_FPU_EXCEPTION)

/* Are there any HW exceptions *other than* the unaligned exception? */
#define OTHER_EXCEPTIONS_ENABLED				\
	(XPAR_MICROBLAZE_0_ILL_OPCODE_EXCEPTION ||		\
	 XPAR_MICROBLAZE_0_IOPB_BUS_EXCEPTION ||		\
	 XPAR_MICROBLAZE_0_DOPB_BUS_EXCEPTION ||		\
	 XPAR_MICROBLAZE_0_DIV_ZERO_EXCEPTION ||		\
	 XPAR_MICROBLAZE_0_FPU_EXCEPTION)

#ifndef __ASSEMBLY__

extern void initialize_exception_handlers(void);

/* Macros to enable and disable HW exceptions in the MSR */
/* Define MSR enable bit for HW exceptions */
#define HWEX_MSR_BIT (1 << 8)

#if MICROBLAZE_EXCEPTIONS_ENABLED

#if XPAR_MICROBLAZE_0_USE_MSR_INSTR
#define __enable_hw_exceptions()					\
	__asm__ __volatile__ ("	msrset	r0, %0;				\
				nop;"					\
				: 					\
				: "i" (HWEX_MSR_BIT)			\
				: "memory")

#define __disable_hw_exceptions()					\
	__asm__ __volatile__ ("	msrclr r0, %0;				\
				nop;"					\
				: 					\
				: "i" (HWEX_MSR_BIT)			\
				: "memory")


#else /* !XPAR_MICROBLAZE_0_USE_MSR_INSTR */
#define __enable_hw_exceptions()					\
	__asm__ __volatile__ ("						\
				mfs	r12, rmsr;			\
				ori	r12, r12, %0;			\
				mts	rmsr, r12;			\
				nop;"					\
				: 					\
				: "i" (HWEX_MSR_BIT)			\
				: "memory", "r12")

#define __disable_hw_exceptions()					\
	__asm__ __volatile__ ("						\
				mfs	r12, rmsr;			\
				andi	r12, r12, ~%0;			\
				mts	rmsr, r12;			\
				nop;"					\
				: 					\
				: "i" (HWEX_MSR_BIT)			\
				: "memory", "r12")

 
#endif /* XPAR_MICROBLAZE_0_USE_MSR_INSTR */

#else		/* MICROBLAZE_EXCEPTIONS_ENABLED */
#define __enable_hw_exceptions()
#define __disable_hw_exceptions()
#endif		/* MICROBLAZE_EXCEPTIONS_ENABLED */

#endif /*__ASSEMBLY__ */


#endif
