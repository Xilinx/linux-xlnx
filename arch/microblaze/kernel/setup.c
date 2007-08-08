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

#ifdef CONFIG_VT
#include <linux/console.h>
#endif

#include <asm/setup.h>
#include <asm/cacheflush.h>
#include <asm/sections.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/bug.h>

#if defined CONFIG_MTD_ATTACHED_ROMFS
#include <linux/romfs_fs.h>
#endif

extern void __init uart_16550_early_init(void);
extern void early_printk(const char *fmt, ...);
extern void irq_early_init(void);
extern int __init setup_early_printk(char *opt);
extern void __init paging_init(void);

static char command_line[COMMAND_LINE_SIZE];

void __init setup_arch(char **cmdline_p)
{
	console_verbose();

	strlcpy(saved_command_line, command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

#if XPAR_MICROBLAZE_0_USE_ICACHE==1
	__flush_icache_all();
	__enable_icache();
#endif

#if XPAR_MICROBLAZE_0_USE_DCACHE==1
	__flush_dcache_all();
	__enable_dcache();
#endif

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

	uart_16550_early_init();

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

void machine_early_init(const char *cmdline)
{
	unsigned long *src, *dst = (unsigned long *)0x0;


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


	if (cmdline) 
		strlcpy(command_line, cmdline, COMMAND_LINE_SIZE);
	else
		command_line[0] = 0;


	for (src = __ivt_start; src < __ivt_end; src++, dst++)
		*dst = *src;

	/* Initialize global data */
	*((unsigned long *)0x68) = 0x1; /* in kernel mode */
	*((unsigned long *)0x64) = (unsigned long)current;

/*!!!!!!!!!! These two shall be removed in release !!!!!!!!!!!*/
	*((unsigned long *)0x70) = 0x0; /* current syscall */
	*((unsigned long *)0x74) = 0x0; /* debug */

	irq_early_init();
}

void machine_restart(char * cmd)
{
	dump_stack();
	BUG();
}

void machine_shutdown(char * cmd)
{
}

void machine_halt(void)
{
	BUG();
}

void machine_power_off(void)
{
	BUG();
}

int show_cpuinfo(struct seq_file *m, void *v)
{
/* TBD (used by procfs) */
	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	int i = *pos;

	return i <= NR_CPUS? (void *) (i + 1): NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	.start =c_start,
	.next =	c_next,
	.stop =	c_stop,
	.show =	show_cpuinfo,
};

