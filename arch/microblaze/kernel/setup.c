/*
 * arch/microblaze/kernel/setup.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#include <linux/init.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/cpu.h>
#include <linux/initrd.h>
#include <linux/console.h>

#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/page.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/bug.h>
#include <asm/param.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>
#include <asm/entry.h>
#include <asm/cpuinfo.h>

#if defined CONFIG_MTD_ATTACHED_ROMFS
#include <linux/romfs_fs.h>
#endif

DEFINE_PER_CPU(unsigned int, KSP);	/* Saved kernel stack pointer */
DEFINE_PER_CPU(unsigned int, KM);	/* Kernel/user mode */
DEFINE_PER_CPU(unsigned int, ENTRY_SP);	/* Saved SP on kernel entry */
DEFINE_PER_CPU(unsigned int, R11_SAVE);	/* Temp variable for entry */
DEFINE_PER_CPU(unsigned int, CURRENT_SAVE);	/* Saved current pointer */

u32 boot_cpuid;
EXPORT_SYMBOL_GPL(boot_cpuid);
u32 memory_limit;
EXPORT_SYMBOL_GPL(memory_limit);

char __attribute ((weak)) _binary_arch_microblaze_kernel_system_dtb_start[];
char __attribute ((weak)) _binary_arch_microblaze_kernel_system_dtb_end[];

extern void early_printk(const char *fmt, ...);
extern void irq_early_init(void);
extern int __init setup_early_printk(char *opt);
extern void __init paging_init(void);

static char default_command_line[COMMAND_LINE_SIZE] = CONFIG_CMDLINE;
char command_line[COMMAND_LINE_SIZE];


void __init setup_arch(char **cmdline_p)
{
	setup_cpuinfo();
	console_verbose();

#ifdef CONFIG_DEVICE_TREE
	early_init_devtree(_binary_arch_microblaze_kernel_system_dtb_start);
	unflatten_device_tree();
#else
	strlcpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
#endif

	*cmdline_p = command_line;
        parse_early_param();

        /* Invalidate and enable all the caches, if necessary. */
        invalidate_icache();
        enable_icache();
        invalidate_dcache();
        enable_dcache();

	panic_timeout = 120;

	setup_memory();
	paging_init();

#ifdef CONFIG_VT
#if defined(CONFIG_XILINX_CONSOLE)
	conswitchp = &xil_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif

#endif
}

#ifdef CONFIG_MTD_UCLINUX_EBSS
/* Return starting point of romfs image */
inline unsigned *get_romfs_base(void)
{
	/* For now, assume "Standard" model of bss_start */
	return (unsigned *)&__bss_start;
}

/* Handle both romfs and cramfs types, without generating unnecessary
   code (ie no point checking for CRAMFS if it's not even enabled) */
inline unsigned get_romfs_len(unsigned *addr)
{
#ifdef CONFIG_ROMFS_FS
	if (memcmp(&addr[0], "-rom1fs-", 8) == 0)       /* romfs */
		return be32_to_cpu(addr[2]);
#endif

#ifdef CONFIG_CRAMFS
	if (addr[0] == le32_to_cpu(0x28cd3d45)) /* cramfs */
		return le32_to_cpu(addr[1]);
#endif
	return 0;
}
#endif 	/* CONFIG_MTD_UCLINUX_EBSS */

static void initialize_interrupt_and_exception_table() {
    unsigned long *src, *dst = (unsigned long *)0x0;

    /* Initialize the interrupt vector table, which is in low memory. */
    for (src = __ivt_start; src < __ivt_end; src++, dst++)
        *dst = *src;
}

/* This code is called before the kernel proper is started */
void machine_early_init(const char *cmdline)
{
#ifdef CONFIG_MTD_UCLINUX_EBSS
	{
		int size;
		extern char *klimit;
		extern char *_ebss;

		/* if CONFIG_MTD_UCLINUX_EBSS is defined, assume ROMFS is at the
		 * end of kernel, which is ROMFS_LOCATION defined above. */
		size = PAGE_ALIGN(get_romfs_len(get_romfs_base()));
		early_printk("Found romfs @ 0x%08x (0x%08x)\n",
				get_romfs_base(), size);
		BUG_ON(size < 0); /* What else can we do? */

		/* Use memmove to handle likely case of memory overlap */
		memmove(&_ebss, get_romfs_base(), size);

		/* update klimit */
		klimit += PAGE_ALIGN(size);
	}
#endif


	memset(__bss_start, 0, __bss_stop-__bss_start);
	memset(_ssbss, 0, _esbss-_ssbss);

	/* Copy command line passed from bootloader, or use default
	   if none provided, or forced */
#ifndef CONFIG_CMDLINE_FORCE
	if (cmdline && cmdline[0]!='\0')
		strlcpy(command_line, cmdline, COMMAND_LINE_SIZE);
	else
#endif
		strlcpy(command_line, default_command_line, COMMAND_LINE_SIZE);

        initialize_interrupt_and_exception_table();

	/* Initialize global data */
	per_cpu(KM,0)= 0x1;	/* We start in kernel mode */
	per_cpu(CURRENT_SAVE,0) = (unsigned long)current;

	irq_early_init();
}

void machine_restart(char * cmd)
{
	printk("Machine restart...\n");
	dump_stack();
	while(1)
		;
}

void machine_shutdown(char * cmd)
{
	printk("Machine shutdown...\n");
	while(1)
		;
}

void machine_halt(void)
{
	printk("Machine halt...\n");
	while(1)
		;
}

void machine_power_off(void)
{
	printk("Machine power off...\n");
	while(1)
		;
}

