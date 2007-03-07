/*
 * arch/microblaze/kernel/process.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/hardirq.h>
#include <linux/seq_file.h>

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	printk("unexpected IRQ trap at vector %02x\n", irq);
}

extern void ledoff(void);

void do_IRQ(struct pt_regs *regs)
{
	unsigned int irq;

	irq_enter();

	irq = get_irq(regs);
	BUG_ON(irq == -1U);
	__do_IRQ(irq, regs);

	irq_exit();
}

int show_interrupts(struct seq_file *p, void *v)
{
/* TBD (used by procfs) */
	return 0;
}
