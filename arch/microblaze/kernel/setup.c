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

#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/bug.h>
#include <asm/xparameters.h>

#if defined CONFIG_MTD_ATTACHED_ROMFS
#include <linux/romfs_fs.h>
#endif

#ifdef CONFIG_BLUECAT_RFS
#include <linux/fs.h>
#include <linux/root_dev.h>
extern unsigned long bluecat_rfs_phys;
extern unsigned long bluecat_rfs_size;
extern int bluecat_rfs_in_rom;
extern int root_mountflags;
#endif

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

#ifdef CONFIG_BLUECAT_RFS
	root_mountflags &= ~MS_RDONLY;
	early_printk("BlueCat RFS phys = %lX\n", bluecat_rfs_phys);
	early_printk("BlueCat RFS size = %lX\n", bluecat_rfs_size);
#endif

	panic_timeout = 120;

	setup_memory();
	paging_init();
}

void machine_early_init(const char *cmdline)
{
	unsigned long *src, *dst = (unsigned long *)0x0;
	unsigned char buf[4];
	unsigned long temp_buf[2];

#ifdef __bluecat__
	cmdline = (char *)_stext - 0x200 - 0x6c00 + 12;
#endif

#ifdef CONFIG_MTD_ATTACHED_ROMFS
	{
		int size;
		extern char *klimit;

		/* if CONFIG_MTD_ATTACHED_ROMFS is defined, assume ROMFS is at the
		 * end of kernel, which is ROMFS_LOCATION defined above. */
		size = romfs_get_size((struct romfs_super_block *)__init_end);
		early_printk("#### size %x ####\n", size);
		early_printk("#### klimit %p ####\n", klimit);
		BUG_ON(size < 0); /* What else can we do? */

		/* Use memmove to handle likely case of memory overlap */
		memmove(klimit, __init_end, size);

		/* update klimit */
		klimit += PAGE_ALIGN(size);
	}
#endif


#ifdef CONFIG_BLUECAT_RFS
	buf[0] = *(unsigned char *)(_stext - 0x7000 + 0x3F9);
	buf[1] = *(unsigned char *)(_stext - 0x7000 + 0x3F8);
	buf[2] = *(unsigned char *)(_stext - 0x7000 + 0x3F7);
	buf[3] = *(unsigned char *)(_stext - 0x7000 + 0x3F6);
	temp_buf[0] = *(unsigned long *)buf;

	buf[0] = *(unsigned char *)(_stext - 0x7000 + 0x3FD);
	buf[1] = *(unsigned char *)(_stext - 0x7000 + 0x3FC);
	buf[2] = *(unsigned char *)(_stext - 0x7000 + 0x3FB);
	buf[3] = *(unsigned char *)(_stext - 0x7000 + 0x3FA);
	temp_buf[1] = *(unsigned long *)buf;
	temp_buf[1] += (unsigned long)(_stext - 0x7000);

	memmove(_end, (void *)temp_buf[1], temp_buf[0]);
	temp_buf[1] = (unsigned long)_end;
#endif

	memset(__bss_start, 0, __bss_stop-__bss_start);
	memset(_ssbss, 0, _esbss-_ssbss);


#ifdef CONFIG_BLUECAT_RFS
	bluecat_rfs_size = temp_buf[0];
	bluecat_rfs_phys = temp_buf[1];
	ROOT_DEV = Root_RAM0;
	bluecat_rfs_in_rom = 0;
	rd_prompt = 0;
#endif

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

#ifdef CONFIG_BLUECAT_LOADER

extern char bluecat_loader_mover_start[];
extern char bluecat_loader_mover_end[];
static asmlinkage void (*bluecat_loader_mover)(unsigned long dst,
					       unsigned long src,
					       unsigned long len);

int bluecat_load(unsigned long kernel_image, unsigned long kernel_size,
		 unsigned long rootfs_image, unsigned long rootfs_size,
		 unsigned short rootdev,
		 unsigned short ramdisk_flags,
		 int mount_root_readonly,
		 char * command_line)
{
	char *cmd_line = (char *)_stext - 0x200 - 0x6c00 + 12;
	unsigned char buf[4];

	local_irq_disable();

	/* Store the kernel command line (no checking for overflow) */
	if (command_line)
		strcpy(cmd_line, command_line);
	else
		cmd_line[0] = '\0';
	if (rootfs_size > 0)
		strcat(cmd_line, " root=/dev/ram");

	/* Store the RFS offset */
	*(unsigned long *)buf = rootfs_image - (unsigned long)_stext + 0x7000;
        *(unsigned char *)(_stext - 0x7000 + 0x3FD) = buf[0];
        *(unsigned char *)(_stext - 0x7000 + 0x3FC) = buf[1];
        *(unsigned char *)(_stext - 0x7000 + 0x3FB) = buf[2];
        *(unsigned char *)(_stext - 0x7000 + 0x3FA) = buf[3];

	/* Store the RFS size */
	*(unsigned long *)buf = rootfs_size;
        *(unsigned char *)(_stext - 0x7000 + 0x3F9) = buf[0];
        *(unsigned char *)(_stext - 0x7000 + 0x3F8) = buf[1];
        *(unsigned char *)(_stext - 0x7000 + 0x3F7) = buf[2];
        *(unsigned char *)(_stext - 0x7000 + 0x3F6) = buf[3];

	/* Jump to the new kernel (never returns) */
	bluecat_loader_mover = (void*)(_stext - 0x7000);
	memmove((void*)(_stext - 0x7000),
		bluecat_loader_mover_start,
		bluecat_loader_mover_end - bluecat_loader_mover_start);
	(*bluecat_loader_mover)((unsigned long)_stext, kernel_image, kernel_size);

	return -1;
}

#endif /* CONFIG_BLUECAT_LOADER */
