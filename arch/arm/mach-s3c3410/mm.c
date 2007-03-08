/*
 *  linux/arch/armnommu/mach-s3c3410/mm.c
 *
 *  Copyright(C)2003 SAMSUNG ELECTRONICS Co.,Ltd.
 *         Hyok S. Choi (hyok.choi@samsung.com)
 *
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
 
#include <asm/mach/map.h>

static void __init s3c3410_map_io(void)
{
}
