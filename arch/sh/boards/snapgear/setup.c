/*
 * linux/arch/sh/boards/snapgear/setup.c
 *
 * Copyright (C) 2002  David McCullough <davidm@snapgear.com>
 * Copyright (C) 2003  Paul Mundt <lethal@linux-sh.org>
 *
 * Based on files with the following comments:
 *
 *           Copyright (C) 2000  Kazumoto Kojima
 *
 *           Modified for 7751 Solution Engine by
 *           Ian da Silva and Jeremy Siegel, 2001.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <asm/machvec.h>
#include <asm/snapgear.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/rtc.h>
#include <asm/cpu/timer.h>

#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/plat-ram.h>

extern void pcibios_init(void);

/****************************************************************************/
/*
 * EraseConfig handling functions
 */

static irqreturn_t eraseconfig_interrupt(int irq, void *dev_id)
{
	volatile char dummy __attribute__((unused)) = * (volatile char *)0xb8000000;

#ifdef CONFIG_LEDMAN
	extern void ledman_signalreset(void);
	ledman_signalreset();
#else
	printk("SnapGear: erase switch interrupt!\n");
#endif

	return IRQ_HANDLED;
}

static int __init eraseconfig_init(void)
{
	printk("SnapGear: EraseConfig init\n");
	/* Setup "EraseConfig" switch on external IRQ 0 */
	if (request_irq(IRL0_IRQ, eraseconfig_interrupt, IRQF_DISABLED,
				"Erase Config", NULL))
		printk("SnapGear: failed to register IRQ%d for Reset witch\n",
				IRL0_IRQ);
	else
		printk("SnapGear: registered EraseConfig switch on IRQ%d\n",
				IRL0_IRQ);
	return(0);
}

module_init(eraseconfig_init);

/****************************************************************************/
/*
 * Initialize IRQ setting
 *
 * IRL0 = erase switch
 * IRL1 = eth0
 * IRL2 = eth1
 * IRL3 = crypto
 */

static struct ipr_data snapgear_ipr_map[] = {
	{ IRL0_IRQ, IRL0_IPR_ADDR, IRL0_IPR_POS, IRL0_PRIORITY },
	{ IRL1_IRQ, IRL1_IPR_ADDR, IRL1_IPR_POS, IRL1_PRIORITY },
	{ IRL2_IRQ, IRL2_IPR_ADDR, IRL2_IPR_POS, IRL2_PRIORITY },
	{ IRL3_IRQ, IRL3_IPR_ADDR, IRL3_IPR_POS, IRL3_PRIORITY },
	{ RTC_PRI_IRQ, RTC_IPR_ADDR, RTC_IPR_POS, RTC_PRIORITY },
	{ RTC_CUI_IRQ, RTC_IPR_ADDR, RTC_IPR_POS, RTC_PRIORITY },
	{ RTC_ATI_IRQ, RTC_IPR_ADDR, RTC_IPR_POS, RTC_PRIORITY },
};

static void __init init_snapgear_IRQ(void)
{
	/* enable individual interrupt mode for externals */
	ctrl_outw(ctrl_inw(INTC_ICR) | INTC_ICR_IRLM, INTC_ICR);

	printk("Setup SnapGear IRQ/IPR ...\n");

	make_ipr_irq(snapgear_ipr_map, ARRAY_SIZE(snapgear_ipr_map));
}

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	((unsigned char *)empty_zero_page)

#define LOADER_TYPE (*(unsigned long *) (PARAM+0x00c))
#define INITRD_START (*(unsigned long *) (PARAM+0x010))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x014))

static struct resource sg_mtd_ram_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platdata_mtd_ram sg_mtd_ram_data = {
	.mapname	= "Romfs",
	.bankwidth	= 1,
	.root_dev	= 1,
};

static struct platform_device sg_mtd_ram_device = {
	.name                   = "mtd-ram",
	.id                     = 0,
	.dev.platform_data      = &sg_mtd_ram_data,
	.num_resources          = 1,
	.resource               = &sg_mtd_ram_resource,
};

#ifdef CONFIG_RTC_DRV_DS1302
static struct platform_device sg_rtc_device = {
	.name           = "ds1302",
	.id             = -1,
	.num_resources  = 0,
};
#endif

#ifdef CONFIG_RTC_DRV_SH
static struct resource sh_rtc_resources[] = {
	[0] = {
		.start	= RTC_BASE,
		.end	= RTC_BASE + 0x58 - 1,
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		/* Period IRQ */
		.start	= RTC_PRI_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* Carry IRQ */
		.start	= RTC_CUI_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		/* Alarm IRQ */
		.start	= RTC_ATI_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sh_rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(sh_rtc_resources),
	.resource	= sh_rtc_resources,
};
#endif

static int __init sg_devices_setup(void)
{
	int ret = 0, ret2 = 0;

	if (sg_mtd_ram_resource.start)
		ret = platform_device_register(&sg_mtd_ram_device);

#ifdef CONFIG_RTC_DRV_DS1302
	ret2 = platform_device_register(&sg_rtc_device);
	if (ret2)
#endif
	{
#ifdef CONFIG_RTC_DRV_SH
		ret2 = platform_device_register(&sh_rtc_device);
#endif
	}
	return ret ? ret : ret2;
}

__initcall(sg_devices_setup);

/*
 * Initialize the board
 */
static void __init snapgear_setup(char **cmdline_p)
{
	if (!LOADER_TYPE && INITRD_START) {
		sg_mtd_ram_resource.start = INITRD_START;
		sg_mtd_ram_resource.end = INITRD_START + INITRD_SIZE - 1;
	}
}

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_snapgear __initmv = {
	.mv_name		= "SnapGear SecureEdge5410",
	.mv_setup		= snapgear_setup,
	.mv_nr_irqs		= 72,

	.mv_inb			= snapgear_inb,
	.mv_inw			= snapgear_inw,
	.mv_inl			= snapgear_inl,
	.mv_outb		= snapgear_outb,
	.mv_outw		= snapgear_outw,
	.mv_outl		= snapgear_outl,

	.mv_inb_p		= snapgear_inb_p,
	.mv_inw_p		= snapgear_inw,
	.mv_inl_p		= snapgear_inl,
	.mv_outb_p		= snapgear_outb_p,
	.mv_outw_p		= snapgear_outw,
	.mv_outl_p		= snapgear_outl,

	.mv_init_irq		= init_snapgear_IRQ,
};
ALIAS_MV(snapgear)
