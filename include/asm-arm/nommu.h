/*
 *  linux/include/asm-arm/nommu.h
 *
 *  Copyright (C) 2002, David McCullough <davidm@snapgear.com>
 *  modified for 2.6 by Hyok S. Choi <hyok.choi@samsung.com>
 */

#ifndef __ARM_NOMMU_H
#define __ARM_NOMMU_H

typedef struct {
	struct vm_list_struct	*vmlist;
	unsigned long		end_brk;
} mm_context_t;

#endif
