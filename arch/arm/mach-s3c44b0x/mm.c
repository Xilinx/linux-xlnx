/*
 *  linux/arch/armnommu/mach-s3c44b0x/mm.c
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
 
#include <asm/mach/map.h>

static void __init s3c44b0x_map_io(void)
{
	return;
}
