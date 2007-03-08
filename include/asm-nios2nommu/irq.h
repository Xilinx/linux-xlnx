/*
 * 21Mar2001    1.1    dgt/microtronix
 *
 * Copyright (C) 2004, Microtronix Datacom Ltd.
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


#ifndef _NIOS2NOMMU_IRQ_H_
#define _NIOS2NOMMU_IRQ_H_

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#include <linux/interrupt.h>

#define	SYS_IRQS	32
#define	NR_IRQS		SYS_IRQS

/*
 * Interrupt source definitions
 * General interrupt sources are the level 1-7.
 * Adding an interrupt service routine for one of these sources
 * results in the addition of that routine to a chain of routines.
 * Each one is called in succession.  Each individual interrupt
 * service routine should determine if the device associated with
 * that routine requires service.
 */

#define IRQ01		(1)	/* level 1  interrupt */
#define IRQ02		(2)	/* level 2  interrupt */
#define IRQ03		(3)	/* level 3  interrupt */
#define IRQ04		(4)	/* level 4  interrupt */
#define IRQ05		(5)	/* level 5  interrupt */
#define IRQ06		(6)	/* level 6  interrupt */
#define IRQ07		(7)	/* level 7  interrupt */
#define IRQ08		(8)	/* level 8  interrupt */
#define IRQ09		(9)	/* level 9  interrupt */
#define IRQ0A		(10)	/* level 10 interrupt */
#define IRQ0B		(11)	/* level 11 interrupt */
#define IRQ0C		(12)	/* level 12 interrupt */
#define IRQ0D		(13)	/* level 13 interrupt */
#define IRQ0E		(14)	/* level 14 interrupt */
#define IRQ0F		(15)	/* level 15 interrupt */
#define IRQ10		(16)	/* level 16 interrupt */
#define IRQ12		(17)	/* level 17 interrupt */
#define IRQ13		(18)	/* level 18 interrupt */
#define IRQ14		(19)	/* level 19 interrupt */
#define IRQ15		(20)	/* level 20 interrupt */
#define IRQ16		(21)	/* level 21 interrupt */
#define IRQ17		(22)	/* level 22 interrupt */
#define IRQ18		(23)	/* level 23 interrupt */
#define IRQ19		(24)	/* level 24 interrupt */
#define IRQ1A		(25)	/* level 25 interrupt */
#define IRQ1B		(26)	/* level 26 interrupt */
#define IRQ1C		(27)	/* level 27 interrupt */
#define IRQ1D		(28)	/* level 28 interrupt */
#define IRQ1E		(29)	/* level 29 interrupt */
#define IRQ1F		(30)	/* level 30 interrupt */
#define IRQ20		(31)	/* level 31 interrupt */
#define IRQ21		(32)	/* level 32 interrupt */

#define IRQMAX		IRQ21

/*
 * "Generic" interrupt sources
 */

/*
 * Machine specific interrupt sources.
 *
 * Adding an interrupt service routine for a source with this bit
 * set indicates a special machine specific interrupt source.
 * The machine specific files define these sources.
 *
 * Removed, they are not used by any one.
 */

/*
 * various flags for request_irq()
 */
#define IRQ_FLG_LOCK	(0x0001)	/* handler is not replaceable	*/
#define IRQ_FLG_REPLACE	(0x0002)	/* replace existing handler	*/
#define IRQ_FLG_FAST	(0x0004)
#define IRQ_FLG_SLOW	(0x0008)
#define IRQ_FLG_STD	(0x8000)	/* internally used		*/

/*
 * Functions to set and clear the interrupt mask.
 */

/*
 * Use a zero to clean the bit.
 */
static inline void clrimr(int mask)
{
	int flags;

	local_irq_save(flags);
	__asm__ __volatile__(
	"rdctl	r8, ienable\n"
	"and	r8,r8,%0\n"
	"wrctl	ienable, r8\n"
	: /* No output */
	: "r" (mask)
	: "r8");
	local_irq_restore(flags);
}

/*
 * Use a one to set the bit.
 */
static inline void setimr(int mask)
{
	int flags;

	local_irq_save(flags);
	__asm__ __volatile__(
	"rdctl	r8, ienable\n"
	"or	r8,r8,%0\n"
	"wrctl	ienable, r8\n"
	: /* No output */
	: "r" (mask)
	: "r8");
	local_irq_restore(flags);
}

/*
 * This structure is used to chain together the ISRs for a particular
 * interrupt source (if it supports chaining).
 */
typedef struct irq_node {
	irq_handler_t	handler;
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
	struct irq_node *next;
} irq_node_t;

/*
 * This function returns a new irq_node_t
 */
extern irq_node_t *new_irq_node(void);

/*
 * This structure has only 4 elements for speed reasons
 */
typedef struct irq_hand {
	irq_handler_t	handler;
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
} irq_hand_t;

/* count of spurious interrupts */
extern volatile unsigned int num_spurious;

#define disable_irq_nosync(i) disable_irq(i)

#ifndef irq_canonicalize
#define irq_canonicalize(i)	(i)
#endif

#endif /* _NIOS2NOMMU_IRQ_H_ */
