/*
 * arch/ppc/platforms/4xx/xilinx_ml507.c
 *
 * Xilinx ML5 PPC440 EMULATION board initialization
 *
 * Author: Grant Likely <grant.likely@secretlab.ca>
 * 	   Wolfgang Reissnegger <w.reissnegger@gmx.net>
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

const char* virtex_machine_name = "Xilinx ML507 Reference System";

#if defined(XPAR_POWER_0_POWERDOWN_BASEADDR)
static volatile unsigned *powerdown_base =
    (volatile unsigned *) XPAR_POWER_0_POWERDOWN_BASEADDR;

static void
xilinx_power_off(void)
{
	local_irq_disable();
	out_be32(powerdown_base, XPAR_POWER_0_POWERDOWN_VALUE);
	while (1) ;
}
#endif

#include <asm/pgtable.h>
void __init
ml507_map_io(void)
{
#if defined(XPAR_POWER_0_POWERDOWN_BASEADDR)
	powerdown_base = ioremap((unsigned long) powerdown_base,
				 XPAR_POWER_0_POWERDOWN_HIGHADDR -
				 XPAR_POWER_0_POWERDOWN_BASEADDR + 1);
#endif
}


void __init
ml507_setup_arch(void)
{
	virtex_early_serial_map();

#ifdef CONFIG_PCI
	ppc4xx_find_bridges();
#endif

	/* Identify the system */
	printk(KERN_INFO "Xilinx ML507 Reference System\n");
}

void __init
ml507_init_irq(void)
{
	ppc4xx_pic_init();

	/*
	 * For PowerPC 405 cores the default value for NR_IRQS is 32.
	 * See include/asm-ppc/irq.h for details.
	 * This is just fine for ML300, ML403 and ML5xx
	 */
#if (NR_IRQS != 32)
#error NR_IRQS must be 32 for ML300/ML403/ML5xx
#endif
}

/*
 * Return the virtual address representing the top of physical RAM.
 */
static unsigned long __init
ml507_find_end_of_memory(void)
{
	// wgr HACK
	//
	unsigned long size = 64 * 1024 * 1024;

	printk("*** HACK: Assuming %lu MB memory size. %s, line %d\n",
			size / (1024 * 1024), __FILE__, __LINE__ +1);
	return size;
	// wgr HACK end
}


static void __init
ml507_calibrate_decr(void)
{
	unsigned int freq;

	freq = XPAR_CORE_CLOCK_FREQ_HZ;

	ibm44x_calibrate_decr(freq);
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
	ppc_md.setup_io_mappings	= ml507_map_io;
	ppc_md.init_IRQ			= ml507_init_irq;
	ppc_md.find_end_of_memory	= ml507_find_end_of_memory;
	ppc_md.calibrate_decr		= ml507_calibrate_decr;

#if defined(XPAR_POWER_0_POWERDOWN_BASEADDR)
	ppc_md.power_off		= xilinx_power_off;
#endif

#ifdef CONFIG_KGDB
	ppc_md.early_serial_map		= virtex_early_serial_map;
#endif
}

