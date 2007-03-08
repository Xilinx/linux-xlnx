/*$)C
 *  arch/arm/mach-s3c24a0/irq.c
 *
 *  Generic S3C24A0 IRQ handling.
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <linux/sysdev.h>

/* Pendng registers
 *
 * INTPND               0x40200010
 * SUBINTPND    0x40200018
 * EINTPND              0x44800038
 */
static const unsigned long p_regs[3] = { 0x40200010, 0x40200018, 0x44800038 };

/* Mask registers
 *
 * INTMSK               0x40200008
 * SUBINTMSK    0x4020001c
 * EINTMSK              0x44800034
 */

static const unsigned long m_regs[3] = { 0x40200008, 0x4020001c, 0x44800034 };

/*
 * Interrupt table
 */
static const int r_irqs[NR_IRQS] = {
        96, 96, 96, 96, 96,  5,  6,  7,  8,  9, 10, 11, 12, 96, 96, 15,
        96, 96, 18, 19, 96, 21, 22, 96, 96, 25, 26, 27, 96, 29, 30, 96,
        17, 17, 17, 23, 23, 23, 28, 28, 96, 96, 96, 13, 13, 16, 16, 14,
        14, 31, 31, 31, 14, 24, 24, 29, 29, 20, 20, 20, 20, 96, 96, 96,
         0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  4,
         4,  4,  4, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96
};

static inline void clear_pending(int irq)
{
        int i = r_irqs[irq];
        if (i == NR_IRQS)
                return;

        *(volatile unsigned long *)(io_p2v(p_regs[irq >> 5])) = (1 << (irq % 32));
        SRCPND = (1 << i);
        INTPND = INTPND;
        INTPND;
}

static inline int read_pending(int irq)
{
        return ((*(volatile unsigned long *)(io_p2v(p_regs[irq >> 5]))) & (1 << (irq % 32)));
}

static inline void mask_irq(int irq)
{
        *(volatile unsigned long *)(io_p2v(m_regs[irq >> 5])) |= (1 << (irq % 32));
}

static inline void unmask_irq(int irq)
{
        *(volatile unsigned long *)(io_p2v(m_regs[irq >> 5])) &= ~(1 << (irq % 32));
}

//#define DEBUG // hcyun
#undef DEBUG

static inline int find_irq(int irq)
{
        int i;

#ifdef DEBUG
        if ( irq == 4 || irq == 3 ) printk("find_irq: irq=%d\n", irq);
#endif

        for (i = IRQ_GRP1_START; i < NR_IRQS; i++) {
                if (r_irqs[i] == irq) {
#ifdef DEBUG
                        if ( i >= 64 ) printk("Externel IRQ %d\n", i);
                        else if ( i >= 32 ) printk("Sub IRQ %d\n", i);
#endif

                        if (read_pending(i)) {
#ifdef DEBUG
                                if ( i >= 64 ) printk("OK there's external pending IRQ %d\n", i);
                                else if ( i >= 32 ) printk("OK there's sub pending IRQ %d\n", i);
#endif
                                return i;
                        }
                }
        }
        return NR_IRQS;
}

int fixup_irq(int irq)
{
        int retval = NR_IRQS;

        if (irq >= IRQ_GRP1_START)
                return retval;

        if ((r_irqs[irq]) == NR_IRQS) {
                retval = find_irq(irq);
        } else {
                retval = irq;
        }

        return retval;
}

static void elfin_mask_ack_irq(unsigned int irq)
{
        mask_irq(irq);
        clear_pending(irq);
}

static void elfin_ack_irq(unsigned int irq)
{
        clear_pending(irq);
}

static void elfin_mask_irq(unsigned int irq)
{
        mask_irq(irq);
}

static void elfin_unmask_irq(unsigned int irq)
{
        unmask_irq(irq);
}

static struct irqchip s3c24a0_irq_chip = {
        .ack    = elfin_ack_irq,  // irq_ack
        .mask   = elfin_mask_irq,  // irq_mask
        .unmask = elfin_unmask_irq   // irq_unmak
};



#ifdef CONFIG_PM
static unsigned long ic_irq_enable;

static int irq_suspend(struct sys_device *dev, u32 state)
{
        return 0;
}

static int irq_resume(struct sys_device *dev)
{
        /* disable all irq sources */
        return 0;
}
#else
#define irq_suspend NULL
#define irq_resume NULL
#endif

static struct sysdev_class irq_class = {
        set_kset_name("irq"),
        .suspend        = irq_suspend,
        .resume         = irq_resume,
};

static struct sys_device irq_device = {
        .id     = 0,
        .cls    = &irq_class,
};

static int __init irq_init_sysfs(void)
{
        int ret = sysdev_class_register(&irq_class);
        if (ret == 0)
                ret = sysdev_register(&irq_device);
        return ret;
}

device_initcall(irq_init_sysfs);

void __init elfin_init_irq(void)
{
        int irq;

        unsigned int flags;

        /* disable all interrupts */
        INTSUBMSK = 0xffffffff;
        EINTMASK = 0xffffffff;
        INTMSK = 0xffffffff;

        /* clear status registers */
        EINTPEND = EINTPEND;
        SUBSRCPND = SUBSRCPND;
        SRCPND = SRCPND;
        INTPND = INTPND;

        /* all interrupts set as IRQ */
        INTMOD = 0x00000000;

        /* we have three groups */
        for (irq = 0; irq < NR_IRQS; irq++) {
                flags = IRQF_PROBE;

                /* external IRQ */
                if ((r_irqs[irq]) == NR_IRQS) {
                        if (irq < IRQ_GRP1_START)
                                INTMSK &= ~(1 << irq);
                }
                /* main IRQ */
                else if ( irq < IRQ_GRP2_START )
                        flags |= IRQF_VALID;

                set_irq_chip(irq, &s3c24a0_irq_chip);
                set_irq_handler(irq, do_edge_IRQ);
                set_irq_flags(irq, flags);
        }
}

/*
 * S3C24A0 , External Interrupt setting interface
 *
 *  1) GPIO-A8: external irq$)C
 *  2) Edge setting
 *                         |<--ECTRL-->|<--GPIO--->|
 *                           bit   reg   bit   reg
 *                           ofs   ofs   ofs   ofs
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * |4-bit|4-bit|4-bit|4-bit|4-bit|4-bit|4-bit|4-bit|
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 */

static const unsigned long garbage[] = {
        0xffff0000,     /* EINT 0 */
        0xffff1010,     /* EINT 1 */
        0xffff2020,     /* EINT 2 */
        0xffff0130,     /* EINT 3 */
        0xffff1140,     /* EINT 4 */
        0xffff2150,     /* EINT 5 */
        0xffff3160,     /* EINT 6 */
        0xffff4170,     /* EINT 7 */
        0xffff5180,     /* EINT 8 */
        0xffff6190,     /* EINT 9 */
        0xffff71a0,     /* EINT 10 */
        0xffff0201,     /* EINT 11 */
        0xffff1211,     /* EINT 12 */
        0xffff2221,     /* EINT 13 */
        0xffff3231,     /* EINT 14 */
        0xffff4241,     /* EINT 15 */
        0xffff5251,     /* EINT 16 */
        0xffff6261,     /* EINT 17 */
        0xffff7271,     /* EINT 18 */
};

int set_external_irq(int irq, int edge, int pullup)
{
        int phy_irq = EINTIRQ_DEC(irq); /* physical irq number */
        unsigned long g;
        struct irqdesc *desc;


        if (phy_irq > 18)
                return -EINVAL;

        g = garbage[phy_irq];

        /* GPIO setting */
        *(volatile unsigned long *)(io_p2v(0x44800008 - (0x4 * (g & 0x0000000f)))) &= ~(0x3 << (((g & 0x000000f0) >> 0x4) * 0x2));
        *(volatile unsigned long *)(io_p2v(0x44800008 - (0x4 * (g & 0x0000000f)))) |= (0x2 << (((g & 0x000000f0) >> 0x4) * 0x2));

#if 0
        printk("GPIO(0x%x) = 0x%x\n", io_p2v(0x44800008), *(volatile unsigned long *)io_p2v(0x44800008));
        printk("GPIO(0x%x) = 0x%x\n", io_p2v(0x44800004), *(volatile unsigned long *)io_p2v(0x44800004));
#endif

        /* edge setting */
        *(volatile unsigned long *)(io_p2v(0x44800018 + (0x4 * ((g & 0x00000f00) >> 0x8)))) &= ~(0x7 << (((g & 0x0000f000) >> 0xc) * 0x4));
        *(volatile unsigned long *)(io_p2v(0x44800018 + (0x4 * ((g & 0x00000f00) >> 0x8)))) |= (edge << (((g & 0x0000f000) >> 0xc) * 0x4));

        /* Set pullup */
        GPUP &= ~(1 << phy_irq);
        GPUP |= pullup;

        desc = irq_desc + irq;
        desc->valid = 1;

        switch ( edge ) {
        case EINT_FALLING_EDGE:
        case EINT_RISING_EDGE:
        case EINT_BOTH_EDGES:
                set_irq_handler(irq, do_edge_IRQ);
                break;
        case EINT_LOW_LEVEL:
        case EINT_HIGH_LEVEL:
                set_irq_handler(irq, do_level_IRQ);
                break;
        }
        clear_pending(irq);
        return 0;
}

EXPORT_SYMBOL(set_external_irq);
