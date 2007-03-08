/*
 * linux/arch/$(ARCH)/irq.c -- general exception handling code
 *
 * Cloned from Linux/m68k.
 *
 * No original Copyright holder listed,
 * Probabily original (C) Roman Zippel (assigned DJD, 1999)
 *
 * Copyright 1999-2000 D. Jeff Dionne, <jeff@rt-control.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/seq_file.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/nios.h>
#include <asm/hardirq.h>

/* table for system interrupt handlers */
irq_hand_t irq_list[NR_IRQS];

/* The number of spurious interrupts */
volatile unsigned int num_spurious;

#define NUM_IRQ_NODES 16
static irq_node_t nodes[NUM_IRQ_NODES];

void __init init_irq_proc(void)
{
	/* Insert /proc/irq driver here */
}

static irqreturn_t default_irq_handler(int irq, void *ptr)
{
#if 1
	printk(KERN_INFO "%s(%d): default irq handler vec=%d [0x%x]\n",
		__FILE__, __LINE__, irq, irq);
#endif
	disable_irq(irq);
	return(IRQ_NONE);
}

/*
 * void init_IRQ(void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function should be called during kernel startup to initialize
 * the IRQ handling routines.
 */

void __init init_IRQ(void)
{
	int i;

	for (i = 0; i < NR_IRQS; i++) {
		irq_list[i].handler = default_irq_handler;
		irq_list[i].flags   = IRQ_FLG_STD;
		irq_list[i].dev_id  = NULL;
		irq_list[i].devname = NULL;
	}

	for (i = 0; i < NUM_IRQ_NODES; i++)
		nodes[i].handler = NULL;

	/* turn off all interrupts */
	clrimr(0);

#ifdef DEBUG
	printk("init_IRQ done\n");
#endif
}

irq_node_t *new_irq_node(void)
{
	irq_node_t *node;
	short i;

	for (node = nodes, i = NUM_IRQ_NODES-1; i >= 0; node++, i--)
		if (!node->handler)
			return node;

	printk (KERN_INFO "new_irq_node: out of nodes\n");
	return NULL;
}

int request_irq(unsigned int irq,
		irq_handler_t handler,
                unsigned long flags,
		const char *devname,
		void *dev_id)
{
	if (irq >= NR_IRQS) {
		printk (KERN_ERR "%s: Unknown IRQ %d from %s\n", __FUNCTION__, irq, devname);
		return -ENXIO;
	}

	if (!(irq_list[irq].flags & IRQ_FLG_STD)) {
		if (irq_list[irq].flags & IRQ_FLG_LOCK) {
			printk(KERN_ERR "%s: IRQ %d from %s is not replaceable\n",
			       __FUNCTION__, irq, irq_list[irq].devname);
			return -EBUSY;
		}
		if (flags & IRQ_FLG_REPLACE) {
			printk(KERN_ERR "%s: %s can't replace IRQ %d from %s\n",
			       __FUNCTION__, devname, irq, irq_list[irq].devname);
			return -EBUSY;
		}
	}
	irq_list[irq].handler = handler;
	irq_list[irq].flags   = flags;
	irq_list[irq].dev_id  = dev_id;
	irq_list[irq].devname = devname;

	setimr(1<<irq);

	return 0;
}

void free_irq(unsigned int irq, void *dev_id)
{
	if (irq >= NR_IRQS) {
		printk (KERN_ERR "%s: Unknown IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (irq_list[irq].dev_id != dev_id)
		printk(KERN_ERR "%s: Removing probably wrong IRQ %d from %s\n",
		       __FUNCTION__, irq, irq_list[irq].devname);

	irq_list[irq].handler = default_irq_handler;
	irq_list[irq].flags   = IRQ_FLG_STD;
	irq_list[irq].dev_id  = NULL;
	irq_list[irq].devname = NULL;

	clrimr(~(1<<irq));
}

/* usually not useful in embedded systems */
unsigned long probe_irq_on (void)
{
	return 0;
}

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

void enable_irq(unsigned int irq)
{
	setimr(1<<irq);
}

void disable_irq(unsigned int irq)
{
	clrimr(~(1<<irq));
}

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v;

	if (i == 0) {
		seq_printf(p, "   : %10u   spurious\n", num_spurious);
	}

	if ((i < NR_IRQS) && (!(irq_list[i].flags & IRQ_FLG_STD))) {
		seq_printf(p, "%3d: %10u ", i, kstat_cpu(0).irqs[i]);
		if (irq_list[i].flags & IRQ_FLG_LOCK)
			seq_printf(p, "L ");
		else
			seq_printf(p, "  ");
		seq_printf(p, "%s\n", irq_list[i].devname);
	}

	return 0;
}

#ifdef CONFIG_PREEMPT_TIMES
extern void latency_cause(int,int);
#else
#define latency_cause(a, b)
#endif
asmlinkage void process_int(unsigned long vec, struct pt_regs *fp)
{

	/* give the machine specific code a crack at it first */
	irq_enter();
	kstat_cpu(0).irqs[vec]++;
	latency_cause(-99,~vec);

	if (irq_list[vec].handler) {
		if ((irq_list[vec].handler(vec, irq_list[vec].dev_id))==IRQ_NONE)
			;
	} else
#ifdef DEBUG
		{
		printk(KERN_ERR "No interrupt handler for level %ld\n", vec);
////		asm("trap 5");
		}
#else
  #if 1
		printk(KERN_ERR "Ignoring interrupt %ld: no handler\n", vec);
  #else
		panic("No interrupt handler for level %ld\n", vec);
  #endif
#endif

	irq_exit();
}

int get_irq_list(char *buf)
{
	int i, len = 0;

	/* autovector interrupts */
	for (i = 0; i < NR_IRQS; i++) {
		if (irq_list[i].handler) {
			len += sprintf(buf+len, "auto %2d: %10u ", i,
				       i ? kstat_cpu(0).irqs[i] : num_spurious);
			if (irq_list[i].flags & IRQ_FLG_LOCK)
				len += sprintf(buf+len, "L ");
			else
				len += sprintf(buf+len, "  ");
			len += sprintf(buf+len, "%s\n", irq_list[i].devname);
		}
	}
	return len;
}
EXPORT_SYMBOL(request_irq);
EXPORT_SYMBOL(free_irq);
