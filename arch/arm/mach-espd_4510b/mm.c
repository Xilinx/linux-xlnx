/*
 *  linux/arch/armnommu/mach-espd_4510b/mm.c
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
