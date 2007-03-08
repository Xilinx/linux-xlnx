/*
 * arch/arm/mach-ixp4xx/sg-setup.c
 *
 * SnapGear/Cyberguard board-setup 
 *
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 * Copyright (C) 2004-2006 Greg Ungerer <gerg@snapgear.com>
 *
 * Original Author: Deepak Saxena <dsaxena@mvista.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#ifdef	__ARMEB__
#define	REG_OFFSET	3
#else
#define	REG_OFFSET	0
#endif

extern void ixp4xx_map_io(void);
extern void ixp4xx_init_irq(void);

/*
 *	Console serial port (always the high speed serial port)
 */
static struct resource sg_uart_resources[] = {
	{
		.start		= IXP4XX_UART1_BASE_PHYS,
		.end		= IXP4XX_UART1_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM
	},
	{
		.start		= IXP4XX_UART2_BASE_PHYS,
		.end		= IXP4XX_UART2_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM
	}
};

static struct plat_serial8250_port sg_uart_data[] = {
	{
		.mapbase	= (IXP4XX_UART1_BASE_PHYS),
		.membase	= (char*)(IXP4XX_UART1_BASE_VIRT + REG_OFFSET),
		.irq		= IRQ_IXP4XX_UART1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,	
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL
	},
	{
		.mapbase	= (IXP4XX_UART2_BASE_PHYS),
		.membase	= (char*)(IXP4XX_UART2_BASE_VIRT + REG_OFFSET),
		.irq		= IRQ_IXP4XX_UART2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,	
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL
	},
	{ },
};

static struct platform_device sg_uart = {
	.name			= "serial8250",
	.id			= 0,
	.dev.platform_data	= sg_uart_data,
	.num_resources		= 2,
	.resource		= sg_uart_resources
};

void __init sg_map_io(void) 
{
	ixp4xx_map_io();
}

static struct platform_device *sg_devices[] __initdata = {
	&sg_uart
};

static void __init sg_init(void)
{
	ixp4xx_sys_init();
	platform_add_devices(sg_devices, ARRAY_SIZE(sg_devices));
}

#ifdef CONFIG_ARCH_SE4000
MACHINE_START(SE4000, "SnapGear SE4000")
	/* Maintainer: SnapGear Inc. */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= sg_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x100,
	.init_machine	= sg_init,
MACHINE_END
#endif

#if defined(CONFIG_MACH_SG640) || defined(CONFIG_MACH_SGARMAUTO)
MACHINE_START(SG640, "SecureComputing SG640")
	/* Maintainer: Secure Computing Inc. */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= sg_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x100,
	.init_machine	= sg_init,
MACHINE_END
#endif

#if defined(CONFIG_MACH_SG560) || defined(CONFIG_MACH_SGARMAUTO)
MACHINE_START(SG560, "CyberGuard SG560")
	/* Maintainer: Cyberguard Inc. */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= sg_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x100,
	.init_machine	= sg_init,
MACHINE_END
#endif

#if defined(CONFIG_MACH_SG565) || defined(CONFIG_MACH_SGARMAUTO)
MACHINE_START(SG565, "CyberGuard SG565")
	/* Maintainer: Cyberguard Inc. */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= sg_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x100,
	.init_machine	= sg_init,
MACHINE_END
#endif

#if defined(CONFIG_MACH_SG580) || defined(CONFIG_MACH_SGARMAUTO)
MACHINE_START(SG580, "CyberGuard SG580")
	/* Maintainer: Cyberguard Inc. */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= sg_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x100,
	.init_machine	= sg_init,
MACHINE_END
#endif

#if defined(CONFIG_MACH_SG590) || defined(CONFIG_MACH_SGARMAUTO)
MACHINE_START(SG590, "Secure Computing SG590")
	/* Maintainer: Secure Computing Inc. */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= sg_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x100,
	.init_machine	= sg_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_SE5100
MACHINE_START(SE5100, "CyberGuard SE5100")
	/* Maintainer: Cyberguard Inc. */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= sg_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x100,
	.init_machine	= sg_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_ESS710
/*
 *  Hard set the ESS710 memory size to be 128M. Early boot loaders
 *  passed in 64MB in their boot tags, but now we really can use the
 *  128M that the hardware has.
 */

static void __init
ess710_fixup(
	struct machine_desc *mdesc,
	struct tag *tags,
	char **cmdline,
	struct meminfo *mi)
{
	struct tag *t = tags;

	for (; t->hdr.size; t = tag_next(t)) {
		if (t->hdr.tag == ATAG_MEM) {
			printk("ESS710: fixing memory size from %dMiB to 128MiB\n",
				t->u.mem.size / (1024 * 1024));
			t->u.mem.start = PHYS_OFFSET;
			t->u.mem.size  = (128*1024*1024);
			break;
		}
	}
}

MACHINE_START(ESS710, "CyberGuard SG710")
	/* Maintainer: Cyberguard Inc. */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= sg_map_io,
	.fixup		= ess710_fixup,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x100,
	.init_machine	= sg_init,
MACHINE_END
#endif

#if defined(CONFIG_MACH_SG720)
MACHINE_START(SG720, "Secure Computing SG720")
	/* Maintainer: Cyberguard Inc. */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= sg_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x100,
	.init_machine	= sg_init,
MACHINE_END
#endif

#ifdef CONFIG_MACH_SG8100
MACHINE_START(SG8100, "Secure Computing SG8100")
	/* Maintainer: Secure Computing Inc. */
	.phys_io	= IXP4XX_PERIPHERAL_BASE_PHYS,
	.io_pg_offst	= ((IXP4XX_PERIPHERAL_BASE_VIRT) >> 18) & 0xfffc,
	.map_io		= sg_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x100,
	.init_machine	= sg_init,
MACHINE_END
#endif

