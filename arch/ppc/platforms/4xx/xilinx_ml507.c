/*
 * arch/ppc/platforms/4xx/xilinx_ml507.c
 *
 * Initialization for Xilinx boards with PowerPC 440
 *
 * Author: Grant Likely <grant.likely@secretlab.ca>
 * 	   Wolfgang Reissnegger <w.reissnegger@gmx.net>
 *         Peter Ryser <peter.ryser@xilinx.com>
 *
 * 2007 (c) Xilinx, Inc.
 * 2005 (c) Secret Lab Technologies Ltd.
 * 2002-2004 (c) MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/serialP.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ppc4xx_pic.h>
#include <asm/time.h>

#include <syslib/ibm44x_common.h>

#include <syslib/gen550.h>
#include <syslib/virtex_devices.h>
#include <platforms/4xx/xparameters/xparameters.h>

#if defined(CONFIG_XILINX_VIRTEX_5_FXT)
#define XILINX_ARCH "Virtex-5 FXT"
#else
#error "No Xilinx Architecture recognized."
#endif

#if defined(CONFIG_XILINX_ML507)
const char *virtex_machine_name = "Xilinx ML507";
#else
const char *virtex_machine_name = "Unknown Xilinx with PowerPC 440";
#endif

extern bd_t __res;

void __init
ml507_setup_arch(void)
{
	virtex_early_serial_map();

	/* Identify the system */
	printk(KERN_INFO
	       "Xilinx Generic PowerPC 440 board support package (%s) (%s)\n",
	       PPC4xx_MACHINE_NAME, XILINX_ARCH);
}

void __init
ml507_init_irq(void)
{
	ppc4xx_pic_init();

	/*
	 * For PowerPC 440 cores the default value for NR_IRQS is 32.
	 * See include/asm-ppc/irq.h for details.
	 * This is just fine for ML5xx
	 */
#if (NR_IRQS != 32)
#error NR_IRQS must be 32 for ML5xx
#endif
}

/*
 * Return the virtual address representing the top of physical RAM.
 */
static unsigned long __init
ml507_find_end_of_memory(void)
{
	bd_t *bip = &__res;

	return bip->bi_memsize;
}


static void __init
ml507_calibrate_decr(void)
{
	bd_t *bip = &__res;

	ibm44x_calibrate_decr(bip->bi_intfreq);
}


void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	/* Calling ppc4xx_init will set up the default values for ppc_md.
	 */
	ibm44x_platform_init(r3, r4, r5, r6, r7);


	/* Overwrite the default settings with our platform specific hooks.
	 */
	ppc_md.setup_arch		= ml507_setup_arch;
	ppc_md.init_IRQ			= ml507_init_irq;
	ppc_md.find_end_of_memory	= ml507_find_end_of_memory;
	ppc_md.calibrate_decr		= ml507_calibrate_decr;

#ifdef CONFIG_KGDB
	ppc_md.early_serial_map		= virtex_early_serial_map;
#endif
}
