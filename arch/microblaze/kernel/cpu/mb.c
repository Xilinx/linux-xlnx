/*
 * arch/microblaze/kernel/cpu/mb4.c
 *
 * CPU-version specific code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 PetaLogix
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
#include <asm/param.h>

static int show_cpuinfo (struct seq_file *m, void *v)
{
  extern unsigned long loops_per_jiffy;
  int count=0;
  count = seq_printf (m,
      "CPU-Family:	Microblaze\n"
      "FPGA-Arch:	%s\n"
      "CPU-Ver:	%s\n"
      "CPU-MHz:   %d.%02d\n"
      "BogoMips:	%lu.%02lu\n",
      XPAR_MICROBLAZE_0_FAMILY,
      XPAR_MICROBLAZE_0_HW_VER,
      XPAR_CPU_CLOCK_FREQ/1000000,
      XPAR_CPU_CLOCK_FREQ % 1000000,
      loops_per_jiffy/(500000/HZ),
      (loops_per_jiffy/(5000/HZ)) % 100);

  count += seq_printf(m,
      "HW-Div:         %s\n"
      "HW-Shift:       %s\n",
      XPAR_MICROBLAZE_0_USE_DIV ? "yes":"no",
      XPAR_MICROBLAZE_0_USE_BARREL ? "yes":"no");

  if(XPAR_MICROBLAZE_0_USE_ICACHE)
    count +=seq_printf (m,
        "Icache:        %ukB\n",
        XPAR_MICROBLAZE_0_CACHE_BYTE_SIZE >> 10);
  else
    count +=seq_printf (m,
        "Icache:         no\n");

  if(XPAR_MICROBLAZE_0_USE_DCACHE)
    count +=seq_printf (m,
        "Dcache:       %ukB\n",
        XPAR_MICROBLAZE_0_DCACHE_BYTE_SIZE >> 10);
  else
    count +=seq_printf (m,
        "Dcache:         no\n");

  count += seq_printf(m,
      "HW-Debug:       %s\n",
      XPAR_MICROBLAZE_0_DEBUG_ENABLED ? "yes":"no");

  return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	int i = *pos;

	return i < NR_CPUS? (void *) (i + 1): NULL;
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

