/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2013-2020 Xilinx, Inc.
 */

#ifndef __ASM_MICROBLAZE_SPINLOCK_TYPES_H
#define __ASM_MICROBLAZE_SPINLOCK_TYPES_H

#ifndef __LINUX_SPINLOCK_TYPES_H
# error "please don't include this file directly"
#endif

typedef struct {
	volatile unsigned int lock;
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ 0 }

typedef struct {
	volatile signed int lock;
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED		{ 0 }

#endif
